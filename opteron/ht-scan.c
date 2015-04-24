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
#include "../library/access.h"
#include "../library/utils.h"

void Opteron::reset(const enum reset mode, const int last)
{
	/* Ensure console drains */
	lib::udelay(1000000);

	if (mode == Warm) {
		outb((0 << 3) | (0 << 2) | (1 << 1), 0xcf9);
		outb((0 << 3) | (1 << 2) | (1 << 1), 0xcf9);
	}

	if (mode == Init) {
		outb((0 << 3) | (0 << 2) | (0 << 1), 0xcf9);
		outb((0 << 3) | (1 << 2) | (0 << 1), 0xcf9);
	}

	/* Cold reset */
	outb((1 << 3) | (0 << 2) | (1 << 1), 0xcf9);
	outb((1 << 3) | (1 << 2) | (1 << 1), 0xcf9);
}

uint32_t Opteron::phy_read32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct)
{
	lib::cht_write32(ht, LINK_PHY_OFFSET + link * 8, reg | (direct << 29));

	while (1) {
		uint32_t stat = lib::cht_read32(ht, LINK_PHY_OFFSET + link * 8);
		if (stat & (1 << 31))
			return lib::cht_read32(ht, LINK_PHY_DATA + link * 8);
		cpu_relax();
	}
}

void Opteron::phy_write32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct, const uint32_t val)
{
	lib::cht_write32(ht, LINK_PHY_OFFSET + link * 8, reg | (1 << 30) | (direct << 29));
	lib::cht_write32(ht, LINK_PHY_DATA + link * 8, val);

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

void Opteron::ht_optimize_link(const ht_t nc, const ht_t neigh, const link_t link)
{
	bool reboot = 0;
	uint32_t val;
	bool ganged = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4) & 1;
	printf("Found %s link to NC on HT%u.%u\n", ganged ? "ganged" : "unganged", neigh, link);

	cht_print(neigh, link);

	printf("Checking HT width/freq.");

	/* Optimize width (16b) */
	printf("+");
	val = lib::cht_read32(nc, Numachip2::LINK_CTRL);
	printf(".");

	if (!options->ht_8bit_only && ((val >> 16) == 0x11)) {
		if ((val >> 24) != 0x11) {
			printf("<NC width>");
			lib::cht_write32(nc, Numachip2::LINK_CTRL, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}

		/* Gang link when appropriate, as the BIOS may not */
		printf("*");
		val = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4);
		printf(".");
		if ((val & 1) == 0) {
			printf("<ganging>");
			lib::cht_write32(neigh, LINK_EXT_CTRL + link * 4, val | 1);
			reboot = 1;
		}

		printf(".");
		val = lib::cht_read32(neigh, LINK_CTRL + link * 0x20);
		printf(".");

		if ((val >> 24) != 0x11) {
			printf("<CPU width>");
			lib::cht_write32(neigh, LINK_CTRL + link * 0x20, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}
	}

	/* Optimize link frequency, if option to disable this is not set */
	if (!options->ht_200mhz_only) {
		uint8_t max_supported = 0;

		printf("+");
		val = lib::cht_read32(nc, Numachip2::LINK_FREQ_REV);
		printf(".");

		// find maximum supported frequency
		for (int i = 0; i < 16; i++)
			if (val >> (16 + i) & 1)
				max_supported = i;

		if (((val >> 8) & 0xf) != max_supported) {
			printf("<NC freq=%d>", max_supported);
			lib::cht_write32(nc, Numachip2::LINK_FREQ_REV, (val & ~0xf00) | (max_supported << 8));
			reboot = 1;
		}

		printf(".");
		val = lib::cht_read32(neigh, LINK_FREQ_REV + link * 0x20);
		uint32_t val2 = lib::cht_read32(neigh, LINK_FREQ_EXT + link * 0x20);
		uint8_t freq = ((val >> 8) & 0xf) | ((val2 & 1) << 4);
		printf(".");

		if (freq != max_supported) {
			printf("<CPU freq=%d>", max_supported);
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

					tempdata &= ~ 0xf;
					tempdata |= adjtemp; // DllProcessFreqCtlIndex2

					phy_write32(neigh, link, PHY_RX_DLL_CTRL5_16, 1, tempdata); // Broadcast 16-bit
				}
			}

			reboot = 1;
		}

		/* If HT3, enable scrambling and retry mode */
		if (max_supported >= 7) {
			printf("*");
			val = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4);
			printf(".");
			if ((val & 8) == 0) {
				printf("<scrambling>");
				lib::cht_write32(neigh, LINK_EXT_CTRL + link * 4, val | 8);
				reboot = 1;
			}
			printf("*");
			val = lib::cht_read32(neigh, LINK_RETRY + link * 4);
			printf(".");
			if ((val & 1) == 0) {
				printf("<retry>");
				lib::cht_write32(neigh, LINK_RETRY + link * 4, val | 1);
				reboot = 1;
			}
		}
	}

	printf("\n");

	if (!options->fastboot && reboot) {
		printf("Rebooting to make new link settings effective...\n");
		reset(Warm, nc - 1);
		/* Does not return */
	}
}

void Opteron::ht_reconfig(const ht_t neigh, const link_t link, const ht_t nnodes)
{
	uint32_t scrub[7];
	uint32_t val;
	int pf_enabled = lib::cht_read32(0, PROBEFILTER_CTRL) & 3;

	printf("Adjusting HT fabric (PF %s)",
	       (pf_enabled == 2) ? "enabled, 4-way" :
	       (pf_enabled == 3) ? "enabled, 8-way" : "disabled");

	/* Disable the L3 and DRAM scrubbers on all nodes in the system */
	for (ht_t ht = 0; ht <= nnodes; ht++) {
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

	printf(".");

	/* Wait 40us for outstanding scrub requests to complete */
	lib::udelay(40);
	lib::critical_enter();

	if (family >= 0x15) {
		/* Disable all cache activity in the system by setting
		   CR0.CD for all active cores in the system */
		/* Issue WBINVD on all active cores in the system */
		caches(0);

		/* Set F0x68[ATMModeEn]=0 and F3x1B8[L3ATMModeEn]=0 */
		for (ht_t ht = 0; ht <= nnodes; ht++) {
			val = lib::cht_read32(ht, LINK_TRANS_CTRL);
			lib::cht_write32(ht, LINK_TRANS_CTRL, val & ~(1 << 12));
			val = lib::cht_read32(ht, L3_CTRL);
			lib::cht_write32(ht, L3_CTRL, val & ~(1 << 27));
		}

		/* Enable all cache activity in the system by clearing
		   CR0.CD for all active cores in the system */
		caches(1);
	}

	printf("+");

	for (int i = nnodes; i >= 0; i--) {
		/* Update "neigh" bcast values for node about to increment fabric size */
		val = lib::cht_read32(neigh, ROUTING + i * 4);
		lib::cht_write32(neigh, ROUTING + i * 4, val | (0x80000 << link));
		/* Increase fabric size */
		val = lib::cht_read32(i, HT_NODE_ID);
		lib::cht_write32(i, HT_NODE_ID, val + (1 << 4));
//		printf("%d", i);
	}

	printf("*");

	/* Reassert LimitCldtCfg */
	for (ht_t ht = 0; ht <= nnodes; ht++) {
		val = lib::cht_read32(ht, LINK_TRANS_CTRL);
		lib::cht_write32(ht, LINK_TRANS_CTRL, val | (1 << 15));
	}

	/* Restore L3 and DRAM scrubber register values */
	for (ht_t ht = 0; ht <= nnodes; ht++) {
		/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
		   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
		   See erratum 505 */
		if (family >= 0x15)
			lib::cht_write32(ht, DCT_CONF_SEL, 0);
		lib::cht_write32(ht, SCRUB_RATE_CTRL, scrub[ht]);
		val = lib::cht_read32(ht, SCRUB_ADDR_LOW);
		lib::cht_write32(ht, SCRUB_ADDR_LOW, val | 1);
	}

	printf(".");

	lib::critical_leave();
	printf("\n");
}

ht_t Opteron::ht_fabric_fixup(ht_t &neigh, link_t &link, const uint32_t vendev)
{
	ht_t nc;
	uint32_t val = lib::cht_read32(0, HT_NODE_ID);
	ht_t nnodes = (val >> 4) & 7;

	/* Check the last cHT node for our VID/DID incase it's already been included in the cHT fabric */
	val = lib::cht_read32(nnodes, Numachip2::VENDEV);
	if (val == vendev) {
		nc = nnodes;
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

		/* Chip already found; make sure the desired width/frequency is set */
		ht_optimize_link(nc, neigh, link);
	} else {
		/* Last node wasn't our VID/DID, try to look for it */
		int rt;
		bool use = 1;

		for (neigh = 0; neigh <= nnodes; neigh++) {
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

				for (rt = 0; rt <= nnodes; rt++) {
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

		if (use)
			fatal("No unrouted coherent links found");

		printf("HT%u.%u is coherent and unrouted\n", neigh, link);

		nc = nnodes + 1;
		/* "neigh" request/response routing, copy bcast values from self */
		val = lib::cht_read32(neigh, ROUTING + neigh * 4);
		lib::cht_write32(neigh, ROUTING + nc * 4,
			   (val & 0x07fc0000) | (0x402 << link));

		/* Deassert LimitCldtCfg so we can talk to nodes > NodeCnt */
		for (ht_t ht = 0; ht <= nnodes; ht++) {
			val = lib::cht_read32(ht, LINK_TRANS_CTRL);
			lib::cht_write32(ht, LINK_TRANS_CTRL, val & ~(1 << 15));

			if (ht == neigh)
				continue;

			/* Route "nc" same as "neigh" for all other nodes */
			val = lib::cht_read32(ht, ROUTING + neigh * 4);
			lib::cht_write32(ht, ROUTING + nc * 4, val);
		}

		val = lib::cht_read32(nc, Numachip2::VENDEV);
		assertf(val == vendev, "Unrouted coherent device %08x is not NumaChip2\n", val);

		if (options->ht_selftest) {
			printf("HT selftest");
			for (int i = 0; i < 500000; i++) {
				val = lib::cht_read32(nc, Numachip2::VENDEV);
				assertf(val == vendev, "Unrouted coherent device %08x is not NumaChip2\n", val);
			}
			printf("\n");
		}

		uint16_t rev = lib::cht_read32(nc, Numachip2::CLASS_CODE_REV) & 0xffff;

		printf("NumaChip2 rev %d found on HT%u.%u\n", rev, neigh, link);

		/* Ramp up link speed and width before adding to coherent fabric */
		ht_optimize_link(nc, neigh, link);

		// HOTFIX: On some images DRAM_SHARED_BASE/LIMIT is not reset by warm/cold reset so we do it here
		lib::cht_write32(nc, Numachip2::DRAM_SHARED_BASE, 0);
		lib::cht_write32(nc, Numachip2::DRAM_SHARED_LIMIT, 0);

		/* Add NC to coherent fabric */
		ht_reconfig(neigh, link, nnodes);
	}

	val = lib::cht_read32(0, HT_NODE_ID);
	lib::cht_write32(nc, Numachip2::HT_NODE_ID,
		   (((val >> 12) & 7) << 24) | /* LkNode */
		   (((val >> 8)  & 7) << 16) | /* SbNode */
		   (nc << 8) | /* NodeCnt */
		   nc); /* NodeId */

	return nc;
}
