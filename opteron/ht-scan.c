/*
 * Copyright (C) 2008-2014 Numascale AS, support@numascale.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/io.h>

#include "../platform/acpi.h"
#include "../bootloader.h"
#include "../platform/options.h"
#include "../platform/devices.h"
#include "../platform/ipmi.h"
#include "../library/access.h"
#include "../library/utils.h"

void Opteron::platform_reset_warm(void)
{
	/* Ensure console drains */
	lib::udelay(1000000);

	outb((0 << 3) | (0 << 2) | (1 << 1), 0xcf9);
	outb((0 << 3) | (1 << 2) | (1 << 1), 0xcf9);

	/* Cold reset */
	outb((1 << 3) | (0 << 2) | (1 << 1), 0xcf9);
	outb((1 << 3) | (1 << 2) | (1 << 1), 0xcf9);
}

uint32_t Opteron::phy_read32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct)
{
	lib::cht_write32(ht, LINK_PHY_OFFSET + link * 8, (direct << 29) | reg);

	while (1) {
		uint32_t stat = lib::cht_read32(ht, LINK_PHY_OFFSET + link * 8);
		if (stat & (1 << 31))
			return lib::cht_read32(ht, LINK_PHY_DATA + link * 8);
		cpu_relax();
	}
}

void Opteron::phy_write32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct, const uint32_t val)
{
	lib::cht_write32(ht, LINK_PHY_DATA + link * 8, val);
	lib::cht_write32(ht, LINK_PHY_OFFSET + link * 8, (1 << 30) | (direct << 29) | reg);

	while (1) {
		uint32_t stat = lib::cht_read32(ht, LINK_PHY_OFFSET + link * 8);
		if (stat & (1 << 31))
			return;
		cpu_relax();
	}
}

void Opteron::cht_print(const ht_t neigh, const link_t link)
{
	uint32_t val = phy_read32(neigh, link, PHY_COMPCAL_CTRL1, 0);
	uint8_t rtt = (val >> 23) & 0x1f;
	uint8_t ron = (val >> 18) & 0x1f;

	printf("Link Ctrl 0x%08x, Freq/Rev 0x%08x, Ext Ctrl 0x%08x, Rtt %u, Ron %u\n",
	  lib::cht_read32(neigh, LINK_CTRL + link * 0x20),
	  lib::cht_read32(neigh, LINK_FREQ_REV + link * 0x20),
	  lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4),
	  rtt, ron);

	if (rtt < 7 || rtt > 13)
		warning("Rtt %u is different than expected value of 12", rtt);

	if (ron < 11 || rtt >> 15)
		warning("Ron %u is different than expected value of 11", ron);
}

static bool proc_lessthan_b0(const ht_t ht)
{
	// AMD Fam15h BKDG p48: OR_B0 = {06h, 01h, 0h}
	uint32_t val = lib::cht_read32(ht, Opteron::NB_CPUID);
	uint8_t extFamily = (val >> 20) & 0xff;
	uint8_t model = ((val >> 8) & 0xf) | ((val >> 12) & 0xf0);
	// stepping is 0, so comparison will always be false

	if (extFamily < 6 || model < 1)
		return 1;
	return 0;
}

void Opteron::optimise_linkbuffers(const ht_t nb, const int link)
{
	const unsigned IsocRspData = 0, IsocNpReqData = 0, IsocRspCmd = 0, IsocPReq = 0, IsocNpReqCmd = 0;
	const unsigned RspData = 3, NpReqData = 0, ProbeCmd = 12, RspCmd = 4, PReq = 0, NpReqCmd = 8;
	unsigned FreeCmd = 32 - NpReqCmd - PReq - RspCmd - ProbeCmd - IsocNpReqCmd - IsocPReq - IsocRspCmd;
	unsigned FreeData = 8 - NpReqData - RspData - PReq - IsocPReq - IsocNpReqData - IsocRspData;

	// constraints
	xassert(RspData <= 3);
	xassert(NpReqData <= 3);
	xassert(ProbeCmd <= 15);
	xassert(RspCmd <= 15);
	xassert(PReq <= 7);
	xassert(NpReqCmd <= 31);
	xassert(FreeCmd <= 31);
	xassert(FreeData <= 7);
	xassert((NpReqCmd + PReq + RspCmd + ProbeCmd + FreeCmd + IsocNpReqCmd + IsocPReq + IsocRspCmd) <= 32);
	xassert((NpReqData + RspData + PReq + FreeData + IsocPReq + IsocNpReqData + IsocRspData) <= 8);
	xassert((ProbeCmd + RspCmd + PReq + NpReqCmd + IsocRspCmd + IsocPReq + IsocNpReqCmd) <= 24);

	uint32_t val = NpReqCmd | (PReq << 5) | (RspCmd << 8) | (ProbeCmd << 12) |
	  (NpReqData << 16) | (RspData << 18) | (FreeCmd << 20) | (FreeData << 25);
	lib::cht_write32(nb, LINK_BASE_BUF_CNT + link * 0x20, val | (1 << 31));

	val = (IsocNpReqCmd << 16) | (IsocPReq << 19) | (IsocRspCmd << 22) |
	  (IsocNpReqData << 25) | (IsocRspData << 27);
	lib::cht_write32(nb, LINK_ISOC_BUF_CNT + link * 0x20, val);
}

void Opteron::ht_optimize_link(const ht_t nc, const ht_t neigh, const link_t link)
{
	bool reboot = 0;
	uint32_t val;
	bool ganged = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4) & 1;
	printf("HT %sganged @ ", ganged ? "" : "un");

	for (unsigned i = 0; i < nc; i++) {
		// disable additional CRC insertion, as it causes HT failure on Numachip2
		val = lib::cht_read32(i, LINK_RETRY_CTRL);
		val &= ~(3 << 12); // disable dynamic command packing/CRC insertion
		val &= ~(7 << 9); // no additional CRC insertion
		val |= 1 << 8; // ensure command packing enabled
		lib::cht_write32(i, LINK_RETRY_CTRL, val);
	}

	if (options->ht_slowmode)
		return;

	/* Optimize link frequency, if option to disable this is not set */
	uint8_t max_supported = 0;
	val = lib::cht_read32(nc, Numachip2::LINK_FREQ_REV);

	// find maximum supported frequency
	for (int i = 0; i < 16; i++)
		if (val >> (16 + i) & 1)
			max_supported = i;

	if (((val >> 8) & 0xf) != max_supported) {
		printf("200MHz");
		lib::cht_write32(nc, Numachip2::LINK_FREQ_REV, (val & ~0xf00) | (max_supported << 8));

		// increase HT phy FIFO pointer distance for HT3
		phy_write32(neigh, link, 0xcf, 0, (0xa << 8) | (2 << 4) | 0xa);
		phy_write32(neigh, link, 0xdf, 0, (0xa << 8) | (2 << 4) | 0xa);
#ifdef UNNEEDED
		// set HT phy Post1 -2.5dB deemphasis to compensate for attenuation
		phy_write32(neigh, link, 0x700c, 1, 32 << 16);

		// enable phy Loop Filter Counter and set limits
		phy_write32(neigh, link, 0xc1, 0, 0x8040280);
		phy_write32(neigh, link, 0xd1, 0, 0x8040280);
#endif

		reboot = 1;
	} else
		printf("1600MHz");

	/* Optimize width (16b), if option to disable this is not set */
	val = lib::cht_read32(nc, Numachip2::LINK_CTRL);
	if ((val >> 16) == 0x11) {
		printf("/8b");

		if ((val >> 24) != 0x11) {
			lib::cht_write32(nc, Numachip2::LINK_CTRL, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}

		/* Gang link when appropriate, as the BIOS may not */
		val = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4);
		if ((val & 1) == 0) {
			lib::cht_write32(neigh, LINK_EXT_CTRL + link * 4, val | 1);
			reboot = 1;
		}

		val = lib::cht_read32(neigh, LINK_CTRL + link * 0x20);
		if ((val >> 24) != 0x11) {
			lib::cht_write32(neigh, LINK_CTRL + link * 0x20, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}
	} else
		printf("/16b");

	val = lib::cht_read32(neigh, LINK_FREQ_REV + link * 0x20);
	uint32_t val2 = lib::cht_read32(neigh, LINK_FREQ_EXT + link * 0x20);
	uint8_t freq = ((val >> 8) & 0xf) | ((val2 & 1) << 4);

	if (freq != max_supported) {
		lib::cht_write32(neigh, LINK_FREQ_REV + link * 0x20, (val & ~0xf00) | ((max_supported & 0xf) << 8));
		lib::cht_write32(neigh, LINK_FREQ_EXT + link * 0x20, (val & ~1) | (max_supported >> 4));

		// update as per BKDG Fam15h p513 for HT3 frequencies
		if (family >= 0x15 && max_supported >= 7) {
			const uint32_t link_prod_info = lib::cht_read32(neigh, LINK_PROD_INFO);
			if (link_prod_info || !proc_lessthan_b0(neigh)) {
				// start from 1.2GHz, ends at 3.2GHz
				const uint8_t DllProcessFreqCtlIndex2a[] = {0xa, 0xa, 0x7, 0x7, 0x5, 0x5, 0x4, 0x3, 0xff, 0xff, 0x3, 0x2, 0x2};
				const uint8_t DllProcessFreqCtlIndex2b[] = {0, 0, 4, 4, 8, 8, 12, 16, 0xff, 0xff, 20, 24, 28};

				uint32_t temp = link_prod_info ? ((link_prod_info >> DllProcessFreqCtlIndex2b[max_supported - 7]) & 0xf)
				  : DllProcessFreqCtlIndex2a[max_supported - 7];
				xassert(temp != 0xff);

				val = phy_read32(neigh, link, PHY_RX_PROC_CTRL_CADIN0, 1);
				bool sign = (val >> 13) & 1; // FuseProcDllProcessComp[2]
				uint8_t offset = (val >> 11) & 3; // FuseProcDllProcessComp[1:0]
				uint32_t tempdata = phy_read32(neigh, link, PHY_RX_DLL_CTRL5_CADIN0, 1);
				tempdata |= 1 << 12; // DllProcessFreqCtlOverride
				uint32_t adjtemp;

				if (sign) {
					if (temp < offset)
						adjtemp = 0;
					else
						adjtemp = temp - offset;
				} else {
					if ((temp + offset) > 0xf)
						adjtemp = 0xf;
					else
						adjtemp = temp + offset;
				}

				tempdata &= ~0xf;
				tempdata |= adjtemp; // DllProcessFreqCtlIndex2

				phy_write32(neigh, link, PHY_RX_DLL_CTRL5_16, 1, tempdata); // Broadcast 16-bit
			}
		}

		reboot = 1;
	}

	/* If HT3, enable scrambling and retry mode */
	if (max_supported >= 7) {
		val = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4);
		if ((val & 8) == 0) {
			lib::cht_write32(neigh, LINK_EXT_CTRL + link * 4, val | 8);
			reboot = 1;
		}

		val = lib::cht_read32(neigh, LINK_RETRY + link * 4);
		if ((val & 1) == 0) {
			lib::cht_write32(neigh, LINK_RETRY + link * 4, val | 1);
			reboot = 1;
		}
	}

	printf("\n");

	if (reboot) {
		printf(BANNER "Warm-booting to make new link settings effective...\n");
		platform_reset_warm();
	}
}

void Opteron::ht_reconfig(const ht_t neigh, const link_t link, const ht_t hts)
{
	uint32_t scrub[7];
	uint32_t val;
	int pf_enabled = lib::cht_read32(0, PROBEFILTER_CTRL) & 3;

	printf("Adjusting HT fabric (PF %s)",
	       (pf_enabled == 2) ? "enabled, 4-way" :
	       (pf_enabled == 3) ? "enabled, 8-way" : "disabled");

	/* Disable the L3 and DRAM scrubbers on all nodes in the system */
	for (ht_t ht = 0; ht <= hts; ht++) {
		/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
		   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
		   See erratum 505 */
		if (family >= 0x15)
			lib::cht_write32(ht, DCT_CONF_SEL, 0);
		scrub[ht] = lib::cht_read32(ht, SCRUB_RATE_CTRL);
		lib::cht_write32(ht, SCRUB_RATE_CTRL, scrub[ht] & ~0x1f00001f);
		val = lib::cht_read32(ht, SCRUB_ADDR_LOW);
		lib::cht_write32(ht, SCRUB_ADDR_LOW, val & ~1);
	}
#ifdef DEBUG
	printf(".");
#endif
	/* Wait 40us for outstanding scrub requests to complete */
	lib::udelay(40);
	lib::critical_enter();

	if (family >= 0x15) {
		/* Disable all cache activity in the system by setting
		   CR0.CD for all active cores in the system */
		/* Issue WBINVD on all active cores in the system */
		caches(0);

		/* Set F0x68[ATMModeEn]=0 and F3x1B8[L3ATMModeEn]=0 */
		for (ht_t ht = 0; ht <= hts; ht++) {
			val = lib::cht_read32(ht, LINK_TRANS_CTRL);
			val &= ~(1 << 12);
#ifdef MEASURE
			val &= ~(3 << 21); // no downstream IO non-posted request limit
			val |= 1 << 7; // read responses are allowed to pass posted writes
#endif
			lib::cht_write32(ht, LINK_TRANS_CTRL, val);
			val = lib::cht_read32(ht, L3_CTRL);
			lib::cht_write32(ht, L3_CTRL, val & ~(1 << 27));
		}

		/* Enable all cache activity in the system by clearing
		   CR0.CD for all active cores in the system */
		caches(1);
	}
#ifdef DEBUG
	printf("+");
#endif
	for (int i = hts; i >= 0; i--) {
		/* Update "neigh" bcast values for node about to increment fabric size */
		val = lib::cht_read32(neigh, ROUTING + i * 4);
		lib::cht_write32(neigh, ROUTING + i * 4, val | (0x80000 << link));
		/* Increase fabric size */
		val = lib::cht_read32(i, HT_NODE_ID);
		lib::cht_write32(i, HT_NODE_ID, val + (1 << 4));
	}
#ifdef DEBUG
	printf("*");
#endif
	/* Reassert LimitCldtCfg */
	for (ht_t ht = 0; ht <= hts; ht++) {
		val = lib::cht_read32(ht, LINK_TRANS_CTRL);
		lib::cht_write32(ht, LINK_TRANS_CTRL, val | (1 << 15));
	}

	/* Restore L3 and DRAM scrubber register values */
	for (ht_t ht = 0; ht <= hts; ht++) {
		/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
		   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
		   See erratum 505 */
		if (family >= 0x15)
			lib::cht_write32(ht, DCT_CONF_SEL, 0);
		lib::cht_write32(ht, SCRUB_RATE_CTRL, scrub[ht]);
		val = lib::cht_read32(ht, SCRUB_ADDR_LOW);
		lib::cht_write32(ht, SCRUB_ADDR_LOW, val | 1);
	}
#ifdef DEBUG
	printf(".");
#endif
	lib::critical_leave();
	printf("\n");
}

ht_t Opteron::ht_fabric_fixup(ht_t &neigh, link_t &link, const uint32_t vendev)
{
	ht_t nc;
	uint32_t val = lib::cht_read32(0, HT_NODE_ID);
	ht_t hts = (val >> 4) & 7;

	/* Check the last cHT node for our VID/DID incase it's already been included in the cHT fabric */
	val = lib::cht_read32(hts, Numachip2::VENDEV);
	if (val == vendev) {
		nc = hts;
		uint16_t rev = lib::cht_read32(nc, Numachip2::CLASS_CODE_REV) & 0xffff;
		printf("NumaChip2 rev %d already at HT%d\n", rev, nc);

		uint32_t rqrt;
		/* Start looking from node 0 */
		neigh = 0;

		while (1) {
			int next = 0;
			rqrt = lib::cht_read32(neigh, ROUTING + 4 * nc) & 0x1f;

			/* Look for other CPUs routed on same link as NC */
			for (int i = 0; i < nc; i++) {
				if (rqrt == (lib::cht_read32(neigh, ROUTING + 4 * i) & 0x1f)) {
					next = i;
					break;
				}
			}

			if (next > 0)
				neigh = next;
			else
				break;
		}

		link = 0;

		while ((2U << link) < rqrt)
			link ++;
	} else {
		/* Last node wasn't our VID/DID, try to look for it */
		int rt;
		bool use = 1;

		for (neigh = 0; neigh <= hts; neigh++) {
			uint32_t aggr = lib::cht_read32(neigh, COH_LINK_TRAF_DIST);

			for (link = 0; link < 4; link++) {
				val = lib::cht_read32(neigh, LINK_TYPE + link * 0x20);
				if ((val & 0x1f) != 0x3)
					continue; /* Not coherent */
				uint32_t val2 = lib::cht_read32(neigh, LINK_CTRL + link * 0x20);
				if (val2 & 0x300)
					warning("HT%d.%d CRC error", neigh, link);
				if (val2 & 0x10)
					warning("HT%d.%d failure", neigh, link);
				if (options->debug.ht)
					printf("HT%d.%d LinkControl = 0x%08x\n", neigh, link, val2);
				if (options->debug.ht)
					printf("HT%d.%d LinkBaseBufCnt = 0x%08x\n", neigh, link, lib::cht_read32(neigh, LINK_BASE_BUF_CNT + link * 0x20));
				if (options->debug.ht)
					printf("HT%d.%d LinkIsocBufCnt = 0x%08x\n", neigh, link, lib::cht_read32(neigh, LINK_ISOC_BUF_CNT + link * 0x20));

				use = 0;

				if (aggr & (0x10000 << link))
					use = 1;

				for (rt = 0; rt <= hts; rt++) {
					val = lib::cht_read32(neigh, ROUTING + rt * 4);

					if (val & (2 << link))
						use = 1; /* Routing entry "rt" uses link "link" */
				}

				if (!use)
					break;
			}

			if (!use)
				break;
		}

		if (use) {
			printf(BANNER "Cold-booting to discover NumaConnect HT link...\n");
			ipmi->reset_cold();
		}

		printf("HT%u.%u is coherent and unrouted\n", neigh, link);

		nc = hts + 1;
		/* "neigh" request/response routing, copy bcast values from self */
		val = lib::cht_read32(neigh, ROUTING + neigh * 4);
		lib::cht_write32(neigh, ROUTING + nc * 4,
			   (val & 0x07fc0000) | (0x402 << link));

		/* Deassert LimitCldtCfg so we can talk to nodes > NodeCnt */
		for (ht_t ht = 0; ht <= hts; ht++) {
			val = lib::cht_read32(ht, LINK_TRANS_CTRL);
			lib::cht_write32(ht, LINK_TRANS_CTRL, val & ~(1 << 15));

			if (ht == neigh)
				continue;

			/* Route "nc" same as "neigh" for all other nodes */
			val = lib::cht_read32(ht, ROUTING + neigh * 4);
			lib::cht_write32(ht, ROUTING + nc * 4, val);
		}

		/* If link retry enabled, check behaviour */
		val = lib::cht_read32(neigh, LINK_RETRY + link * 4);
		if (val & 1) {
			printf("Testing HT error-retry");
			for (unsigned i = 0; i < 1000000; i++) {
#ifdef ERRATA
				if ((i % 64) == 0)
					lib::cht_write32(neigh, LINK_RETRY + link * 4, val | 2);
#endif

				uint32_t val2 = lib::cht_read32(nc, Numachip2::VENDEV);
				if (val2 != vendev) {
					printf("\n" BANNER "Expected Numachip2 vendev %08x but got %08x; cold-booting...", vendev, val2);
					ipmi->reset_cold();
				}
			}

			printf("\n");
		}

		uint16_t rev = lib::cht_read32(nc, Numachip2::CLASS_CODE_REV) & 0xffff;
		printf("NumaChip2 rev %d found on HT%u.%u\n", rev, neigh, link);

		/* Add NC to coherent fabric */
		ht_reconfig(neigh, link, hts);
	}
#ifdef EARLYEXIT
	uint64_t msr = lib::rdmsr(0xc0010015);
	lib::wrmsr(0xc0010015, msr & ~(1ULL << 17));
	os->exec("ubuntu-1504-live");
#endif
	val = lib::cht_read32(0, HT_NODE_ID);
	lib::cht_write32(nc, Numachip2::HT_NODE_ID,
		   (((val >> 12) & 7) << 24) | /* LkNode */
		   (((val >> 8)  & 7) << 16) | /* SbNode */
		   (nc << 8) | /* NodeCnt */
		   nc); /* NodeId */

	return nc;
}
