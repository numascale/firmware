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

#include "opteron.h"
#include "msrs.h"
#include "../bootloader.h"
#include "../library/base.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../platform/trampoline.h"
#include "../platform/options.h"

// approximation before probing
uint32_t Opteron::tsc_mhz = 2200;
uint32_t Opteron::ioh_vendev;
uint8_t Opteron::mc_banks;
uint8_t Opteron::family;

void Opteron::check(void)
{
#ifdef LOCAL
	for (unsigned bank = 0; bank < mc_banks; bank++) {
		const uint32_t msr = MSR_MC0_STATUS + bank * 4;
		uint64_t val = lib::rdmsr(msr);
		if (!(val & (1ULL << 63)))
			continue;

		printf("SCI%03x#%u: MSR%08x=0x%llx\n", sci, ht, msr, val);
		lib::wrmsr(msr, 0);
	}
#endif
	for (unsigned link = 0; link < 4; link++) {
		uint32_t val = read32(LINK_CTRL + link * 0x20);
		if (val & 0x300)
			warning("HT link %u CRC error", link);

		if (val & 0x10)
			warning("HT link %u failure", link);
	}

	uint64_t s = read64(MC_NB_STAT);
	if (s & (1ULL << 63)) {
		const char *sig[] = {
		  NULL, "CRC Error", "Sync Error", "Mst Abort", "Tgt Abort",
		  "GART Error", "RMW Error", "WDT Error", "ECC Error", NULL,
		  "Link Data Error", "Protocol Error", "NB Array Error",
		  "DRAM Parity Error", "Link Retry", "GART Table Walk Data Error",
		  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		  "L3 Cache Data Error", "L3 Cache Tag Error", "L3 Cache LRU Error",
		  "Probe Filter Error", "Compute Unit Data Error"};

		warning("%s on SCI%03x#%u:", sig[(s >> 16) & 0x1f], sci, ht);
		printf("- ErrorCode=0x%llx ErrorCodeExt=0x%llx Syndrome=0x%llx\n",
		  s & 0xffff, (s >> 16) & 0xf, ((s >> 16) & 0xff00) | ((s >> 47) & 0xff));
		printf("- Link=%llu Scrub=%llu SubLink=%llu McaStatSubCache=%llu\n",
		  (s >> 26) & 0xf, (s >> 40) & 1, (s >> 41) & 1, (s >> 42) & 3);
		printf("- UECC=%llu CECC=%llu PCC=%llu\n",
		  (s >> 45) & 1, (s >> 46) & 1, (s >> 57) & 1);
		printf("- MiscV=%llu En=%llu UC=%llu Overflow=%llu\n",
		  (s >> 59) & 1, (s >> 60) & 1, (s >> 61) & 1, (s >> 62) & 1);

		if ((s >> 56) & 1) // ErrCoreIdVal
			printf(" ErrCoreId=%llu", (s >> 32) & 0xf);

		if ((s >> 58) & 1) // AddrV
			printf(" Address=0x%016llx", read64(MC_NB_ADDR));

		write64_split(MC_NB_ADDR, 0);
		write64_split(MC_NB_STAT, 0);
		printf("\n");
	}

	uint32_t v = read32(MC_NB_DRAM);
	if (v & 0xff) { // looks like bits 7:0 only are usable
		warning("DRAM machine check 0x%08x on SCI%03x#%u", v, sci, ht);
		write32(MC_NB_DRAM, 0);
	}

	v = read32(MC_NB_LINK);
	if (v & 0xfff) {
		warning("HT Link machine check 0x%08x on SCI%03x#%u", v, sci, ht);
		write32(MC_NB_LINK, 0);
	}

	v = read32(MC_NB_L3C);
	if (v & 0xfff) {
		warning("L3 Cache machine check 0x%08x on SCI%03x#%u", v, sci, ht);
		write32(MC_NB_L3C, 0);
	}
}

void Opteron::disable_syncflood(const ht_t nb)
{
	warning_once("Disabling sync-flood");

	uint32_t val = lib::cht_read32(nb, MC_NB_CONF);
	val &= ~(1 << 2);  // SyncOnUcEccEn: sync flood on uncorrectable ECC error enable
	val |= 1 << 3;     // SyncPktGenDis: sync packet generation disable
	val |= 1 << 4;     // SyncPktPropDis: sync packet propagation disable
	val &= ~(1 << 20); // SyncOnWDTEn: sync flood on watchdog timer error enable
	val &= ~(1 << 21); // SyncOnAnyErrEn: sync flood on any error enable
	val &= ~(1 << 30); // SyncOnDramAdrParErrEn: sync flood on DRAM address parity error enable
	val |= 1 << 8;     // disable WDT
	lib::cht_write32(nb, MC_NB_CONF, val);

	val = lib::cht_read32(nb, MC_NB_CONF_EXT);
	val &= ~(1 << 1);  // SyncFloodOnUsPwDataErr: sync flood on upstream posted write data error
	val &= ~(1 << 6);  // SyncFloodOnDatErr
	val &= ~(1 << 7);  // SyncFloodOnTgtAbtErr
	val &= ~(1 << 8);  // SyncOnProtEn: sync flood on protocol error enable
	val &= ~(1 << 9);  // SyncOnUncNbAryEn: sync flood on uncorrectable NB array error enable
	val &= ~(1 << 20); // SyncFloodOnL3LeakErr: sync flood on L3 cache leak error enable
	val &= ~(1 << 21); // SyncFloodOnCpuLeakErr: sync flood on CPU leak error enable
	val &= ~(1 << 22); // SyncFloodOnTblWalkErr: sync flood on table walk error enable
	lib::cht_write32(nb, MC_NB_CONF_EXT, val);

	for (unsigned l = 0; l < 4; l++) {
		// CrcFloodEn: Enable sync flood propagation upon link failure
		val = read32(0x0084 + l * 0x20);
		write32(0x0084 + l * 0x20, val & ~2);
	}
}

uint64_t Opteron::read64(const reg_t reg) const
{
	return lib::mcfg_read64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

uint32_t Opteron::read32(const reg_t reg) const
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Opteron::write64_split(const reg_t reg, const uint64_t val) const
{
	lib::mcfg_write64_split(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Opteron::write32(const reg_t reg, const uint32_t val) const
{
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Opteron::set32(const reg_t reg, const uint32_t mask) const
{
	uint32_t val = read32(reg);
	write32(reg, val | mask);
}

void Opteron::clear32(const reg_t reg, const uint32_t mask) const
{
	uint32_t val = read32(reg);
	write32(reg, val & ~mask);
}

void Opteron::clearset32(const reg_t reg, const uint32_t clearmask, const uint32_t setmask) const
{
	uint32_t val = read32(reg);
	write32(reg, (val & ~clearmask) | setmask);
}

void Opteron::prepare(void)
{
	// disable 32-bit address wrapping to allow 64-bit access in 32-bit code
	uint64_t msr = lib::rdmsr(MSR_HWCR) | (1ULL << 17);
	lib::wrmsr(MSR_HWCR, msr);

	// enable 64-bit config access
	msr = lib::rdmsr(MSR_CU_CFG2) | (1ULL << 50);

	// workaround F15h Errata 572: Access to PCI Extended Configuration Space in SMM is blocked
	if (family >= 0x15)
		msr |= 1ULL << 27;
	lib::wrmsr(MSR_CU_CFG2, msr);

	// detect processor family
	uint32_t val = lib::cht_read32(0, NB_CPUID);
	family = ((val >> 20) & 0xf) + ((val >> 8) & 0xf);
	if (family >= 0x15) {
		val = lib::cht_read32(0, NB_PSTATE_0);
		tsc_mhz = 200 * (((val >> 1) & 0x1f) + 4) / (1 + ((val >> 7) & 1));
	} else {
		val = lib::cht_read32(0, CLK_CTRL_0);
		uint64_t val6 = lib::rdmsr(MSR_COFVID_STAT);
		tsc_mhz = 200 * ((val & 0x1f) + 4) / (1 + ((val6 >> 22) & 1));
	}

	if (family == 0x10) {
		// disable HT lock mechanism as unsupported by Numachip
		msr = lib::rdmsr(MSR_LSCFG) | (1ULL << 44);
		lib::wrmsr(MSR_LSCFG, msr);
	}

	printf("Family %xh Opteron with %dMHz NB TSC frequency\n", family, tsc_mhz);

	// check number of MCA banks
	mc_banks = lib::rdmsr(MSR_MC_CAP) & 0xff;

	// disable core WDT
	lib::wrmsr(MSR_CPUWDT, 0);
}

void Opteron::dram_scrub_disable(void)
{
	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	// disable DRAM scrubbers
	scrub = read32(SCRUB_RATE_CTRL);
	write32(SCRUB_RATE_CTRL, scrub & ~0x1f);
	lib::udelay(40); // allow outstanding scrub requests to finish

	clear32(SCRUB_ADDR_LOW, 1);
}

void Opteron::dram_scrub_enable(void)
{
	uint32_t redir = read32(SCRUB_ADDR_LOW) & 1;
	write64_split(SCRUB_ADDR_LOW, dram_base | redir);

	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	write32(SCRUB_RATE_CTRL, scrub);
	set32(SCRUB_ADDR_LOW, 1);
}

void Opteron::optimise_linkbuffers(void)
{
	const int IsocRspData = 0, IsocNpReqData = 0, IsocRspCmd = 0, IsocPReq = 0, IsocNpReqCmd = 1;
	const int RspData = 3, NpReqData = 3, ProbeCmd = 4, RspCmd = 9, PReq = 2, NpReqCmd = 8;

	/* Ensure constraints are met */
	int FreeCmd = 32 - NpReqCmd - PReq - RspCmd - ProbeCmd - IsocNpReqCmd - IsocPReq - IsocRspCmd;
	int FreeData = 8 - NpReqData - RspData - PReq - IsocPReq - IsocNpReqData - IsocRspData;
	xassert((ProbeCmd + RspCmd + PReq + NpReqCmd + IsocRspCmd + IsocPReq + IsocNpReqCmd) <= 24);

	uint32_t b1 = NpReqCmd | (PReq << 5) | (RspCmd << 8) | (ProbeCmd << 12) |
	  (NpReqData << 16) | (RspData << 18) | (FreeCmd << 20) | (FreeData << 25);
	uint32_t b2 = (IsocNpReqCmd << 16) | (IsocPReq << 19) | (IsocRspCmd << 22) |
	  (IsocNpReqData << 25) | (IsocRspData << 27);

	for (int link = 0; link < 4; link++) {
		// Probe Filter doesn't affect IO link buffering
		uint32_t val = read32(LINK_TYPE + link * 0x20);
		if ((!(val & 1)) || (val & 4))
			continue;

		printf("BEFORE b1=%08x b2=%08x\n",
		  read32(LINK_BASE_BUF_CNT + link * 0x20),
		  read32(LINK_ISOC_BUF_CNT + link * 0x20));

		write32(LINK_BASE_BUF_CNT + link * 0x20, b1 | (1 << 31));
		write32(LINK_ISOC_BUF_CNT + link * 0x20, b2);

		printf("AFTER b1=%08x b2=%08x\n",
		  read32(LINK_BASE_BUF_CNT + link * 0x20),
		  read32(LINK_ISOC_BUF_CNT + link * 0x20));
	}
}

void Opteron::init(void)
{
	uint32_t val = read32(CLK_CTRL_0);
	if (val & (1 << 13))
		fatal("Please disable C1E support in the BIOS");

	if (family >= 0x15)
		mmiomap = new MmioMap15(*this);
	else
		mmiomap = new MmioMap10(*this);

	ioh_ht = (read32(HT_NODE_ID) >> 8) & 7;
	ioh_link = (read32(UNIT_ID) >> 8) & 7; // only valid for NB with IOH link

	// detect amount of memory
	dram_base = (uint64_t)(read32(DRAM_BASE) & 0x1fffff) << 27;
	uint64_t dram_limit = ((uint64_t)(read32(DRAM_LIMIT) & 0x1fffff) << 27) | 0x7ffffff;
	dram_size = dram_limit - dram_base + 1;

	if (options->debug.northbridge)
		disable_syncflood(ht);

	// if slave, subtract and disable MMIO hole
	if (!local) {
		val = read32(DRAM_HOLE);
		if (val & 1) {
			dram_size -= (val & 0xff00) << (23 - 7);
			write32(DRAM_HOLE, val & ~0xff81);
		}
	}

	// detect number of cores
	cores = 1;
	if (family < 0x15) {
		val = read32(LINK_TRANS_CTRL);
		if (val & 0x20)
			cores++; /* Cpu1En */

		val = read32(EXT_LINK_TRANS_CTRL);
		for (int i = 0; i <= 3; i++)
			if (val & (1 << i))
				cores++;
	} else {
		val = read32(NB_CAP_2);
		cores += val & 0xff;

		val = read32(DOWNCORE_CTRL);
		while (val) {
			if (val & 1)
				cores--;
			val >>= 1;
		}
	}

	dram_scrub_disable();
}

Opteron::Opteron(const sci_t _sci, const ht_t _ht, const bool _local):
  local(_local), sci(_sci), ht(_ht), drammap(*this)
{
	init();

	if (!local)
		return;

	// FIXME set DisOrderRdRsp

	// set CHtExtAddrEn, ApicExtId, ApicExtBrdCst
	uint32_t val = read32(LINK_TRANS_CTRL);
	if ((val & ((1 << 25) | (1 << 18) | (1 << 17))) != ((1 << 25) | (1 << 18) | (1 << 17)))
		write32(LINK_TRANS_CTRL, val | (1 << 25) | (1 << 18) | (1 << 17));

	// enable 128MB-granularity on extended MMIO maps
	if (Opteron::family < 0x15) {
		val = read32(EXT_LINK_TRANS_CTRL);
		write32(EXT_LINK_TRANS_CTRL, (val & ~0x300) | 0x200);
	}

	// enable Addr64BitEn on IO links
	for (int i = 0; i < 4; i++) {
		// skip coherent/disabled links
		val = read32(LINK_TYPE + i * 0x20);
		if (!(val & (1 << 2)))
			continue;

		val = read32(LINK_CTRL + i * 0x20);
		if (!(val & (1 << 15)))
			write32(LINK_CTRL + i * 0x20, val | (1 << 15));
	}

	// Numachip can't handle Coherent Prefetch Probes, required disabled for PF anyway
	// FIXME: check if needed
	val = read32(MCTL_EXT_CONF_LOW);
	write32(MCTL_EXT_CONF_LOW, val & ~(7 <<8));

	// disable traffic distribution for directed probes
	// FIXME: check if needed
	val = read32(COH_LINK_TRAF_DIST);
	write32(COH_LINK_TRAF_DIST, val & ~1);

	// disable legacy GARTs
	for (uint16_t reg = 0x3090; reg <= 0x309c; reg += 4)
		write32(reg, 0);

	printf("DRAM ranges on SCI%03x#%d:\n", sci, ht);
	for (int range = 0; range < 8; range++)
		drammap.print(range);
}

void Opteron::dram_clear_start(void)
{
	set32(MCTL_CONF_HIGH, 3 << 12); // disable memory controller prefetch
	set32(MCTL_SEL_LOW, 1 << 3); // start memory clearing
}

void Opteron::dram_clear_wait(void)
{
	// poll until done indicated
	while (read32(MCTL_SEL_LOW) & (1 << 9))
		cpu_relax();

	clear32(MCTL_CONF_HIGH, 3 << 12); // reenable memory controller prefetch
}

void Opteron::discover(void)
{
	uint32_t links = read32(LINK_INIT_STATUS); // F0x1A0
	xassert(links & (1 << 31)); // ensure valid
	for (unsigned l = 0; l < 3; l++) {
		// skip connected links
		if (!(links & (1 << (1 + l * 2))))
			continue;

		printf("HT%u.%u ", ht, l);

		set32(LINK_GLO_CTRL_EXT, 1 << 8); // set GlblLinkTrain[ConnDly]
		set32(LINK_EXT_CTRL + l * 4, 1); // set LinkTrain[Ganged]
		clear32(LINK_RETRY + l * 4, 1);  // adjust Retry Control[Retry Enable]
		const uint8_t freq = 0; // 200MHz freq
		clearset32(LINK_FREQ_REV + l * 0x20, 0x1f << 8, (freq & 15) << 8);
		clearset32(LINK_FREQ_EXT + l * 0x20, 1, freq >> 4);
		const uint8_t width = 0; // 8-bit width
		clearset32(LINK_CTRL + l * 0x20, (7 << 28) | (7 << 24), (width << 28) | (width << 24));

		clear32(LINK_CTRL + l * 0x20, 1 << 6); // clear Link Control[End Of Chain]
		clear32(LINK_CTRL + l * 0x20, 1 << 7); // clear Link Control[TXOff]

		local_node->iohub->ldtstop();

		// wait for InitComplete and not ConnPend
		uint32_t val;
		do {
			cpu_relax();
			val = read32(LINK_TYPE + l * 0x20);
		} while ((val & (1 << 4)) || (!(val & 1)));

		printf("%sconnected and %scoherent\n", val & 1 ? "" : "not ", val & 4 ? "non-" : "");
	}
}
