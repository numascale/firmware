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
int Opteron::family;
static uint64_t msr_nb_cfg;

void Opteron::check(void)
{
	const char *sig[] = {
	  NULL, "CRC Error", "Sync Error", "Mst Abort", "Tgt Abort",
	  "GART Error", "RMW Error", "WDT Error", "ECC Error", NULL,
	  "Link Data Error", "Protocol Error", "NB Array Error",
	  "DRAM Parity Error", "Link Retry", "GART Table Walk Data Error",
	  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	  "L3 Cache Data Error", "L3 Cache Tag Error", "L3 Cache LRU Error",
	  "Probe Filter Error", "Compute Unit Data Error"};

	uint64_t s = read64(MC_NB_STAT);
	if (s & (1ULL << 63)) {
		warning("%s on SCI%03x#%u:", sig[(s >> 16) & 0x1f], sci, ht);
		printf("- ErrorCode=0x%llx Syndrome=0x%llx\n",
		  s & 0xffff, ((s >> 16) & 0xff00) | ((s >> 47) & 0xff));
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

		write64(MC_NB_ADDR, 0);
		write64(MC_NB_STAT, 0);
	}

	uint32_t v = read32(MC_NB_DRAM);
	if (v & 0xfff) {
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

uint64_t Opteron::read64(const reg_t reg) const
{
	return lib::mcfg_read64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

uint32_t Opteron::read32(const reg_t reg) const
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Opteron::write64(const reg_t reg, const uint64_t val) const
{
	lib::mcfg_write64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
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

void Opteron::prepare(void)
{
	// ensure MMCFG access is setup
	assert(lib::rdmsr(MSR_MCFG_BASE) &~ 0xfffff);

	// enable CF8 extended access
	msr_nb_cfg = lib::rdmsr(MSR_NB_CFG);
	lib::wrmsr(MSR_NB_CFG, msr_nb_cfg | (1ULL << 46));

	// disable 32-bit address wrapping to allow 64-bit access in 32-bit code
	*REL64(new_hwcr_msr) = lib::rdmsr(MSR_HWCR) | (1ULL << 17);
	lib::wrmsr(MSR_HWCR, *REL64(new_hwcr_msr));

	// enable 64-bit config access
	*REL64(new_cucfg2_msr) = lib::rdmsr(MSR_CU_CFG2) | (1ULL << 50);
	lib::wrmsr(MSR_CU_CFG2, *REL64(new_cucfg2_msr));

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

	printf("Family %xh Opteron with %dMHz NB TSC frequency\n", family, tsc_mhz);

	// detect IOH
	ioh_vendev = lib::mcfg_read32(SCI_LOCAL, 0, 0, 0, 0);
	assert(ioh_vendev != 0xffffffff);

	switch (ioh_vendev) {
	case VENDEV_SR5690:
	case VENDEV_SR5670:
	case VENDEV_SR5650:
		// enable 52-bit PCIe address generation
		val = lib::mcfg_read32(SCI_LOCAL, 0, 0, 0, 0xc8);
		lib::mcfg_write32(SCI_LOCAL, 0, 0, 0, 0xc8, val | (1 << 15));
		break;
	default:
		fatal("Unknown IOH");
	}
}

void Opteron::restore(void)
{
	lib::wrmsr(MSR_NB_CFG, msr_nb_cfg);
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
	if (scrub & 0x1f) {
		write32(SCRUB_RATE_CTRL, scrub & ~0x1f);
		lib::udelay(40); // allow outstanding scrub requests to finish
	}

	clear32(SCRUB_ADDR_LOW, 1);
}

void Opteron::dram_scrub_enable(void)
{
	uint32_t redir = read32(SCRUB_ADDR_LOW) & 1;
	write64(SCRUB_ADDR_LOW, dram_base | redir);

	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	write32(SCRUB_RATE_CTRL, scrub);
	set32(SCRUB_ADDR_LOW, 1);
}

void Opteron::init(void)
{
	uint32_t vendev = read32(VENDEV);
	assert(vendev == VENDEV_OPTERON);

	uint32_t val = read32(PROBEFILTER_CTRL);
	assertf(val & 3, "NumaChip2 requires Probe Filter to be enabled");

	// detect amount of memory
	dram_base = (uint64_t)(read32(DRAM_BASE) & 0x1fffff) << 27;
	uint64_t dram_limit = ((uint64_t)(read32(DRAM_LIMIT) & 0x1fffff) << 27) | 0x7ffffff;
	dram_size = dram_limit - dram_base + 1;

	if (options->debug.northbridge) {
		val = read32(MC_NB_CONF);
		val &= ~(1 << 2);  // SyncOnUcEccEn: sync flood on uncorrectable ECC error enable
		val |= 1 << 3;     // SyncPktGenDis: sync packet generation disable
		val |= 1 << 4;     // SyncPktPropDis: sync packet propagation disable
		val &= ~(1 << 20); // SyncOnWDTEn: sync flood on watchdog timer error enable
		val &= ~(1 << 21); // SyncOnAnyErrEn: sync flood on any error enable
		val &= ~(1 << 30); // SyncOnDramAdrParErrEn: sync flood on DRAM address parity error enable
		write32(MC_NB_CONF, val);

		val = read32(MC_NB_CONF_EXT);
		val &= ~(1 << 1);  // SyncFloodOnUsPwDataErr: sync flood on upstream posted write data error
		val &= ~(1 << 6);  // SyncFloodOnDatErr
		val &= ~(1 << 7);  // SyncFloodOnTgtAbtErr
		val &= ~(1 << 8);  // SyncOnProtEn: sync flood on protocol error enable
		val &= ~(1 << 9);  // SyncOnUncNbAryEn: sync flood on uncorrectable NB array error enable
		val &= ~(1 << 20); // SyncFloodOnL3LeakErr: sync flood on L3 cache leak error enable
		val &= ~(1 << 21); // SyncFloodOnCpuLeakErr: sync flood on CPU leak error enable
		val &= ~(1 << 22); // SyncFloodOnTblWalkErr: sync flood on table walk error enable
		write32(MC_NB_CONF_EXT, val);
	}

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

// remote instantiation
Opteron::Opteron(const sci_t _sci, const ht_t _ht):
  local(0), sci(_sci), ht(_ht), mmiomap(*this), drammap(*this)
{
	init();
}

// local instantiation; SCI is set later
Opteron::Opteron(const ht_t _ht):
  local(1), sci(SCI_LOCAL), ht(_ht), mmiomap(*this), drammap(*this)
{
	init();

	// enable CF8 extended access; Linux needs this later */
	uint32_t val = read32(NB_CONF_1H);
	write32(NB_CONF_1H, val | (1 << (46 - 32)));

	// set CHtExtAddrEn, ApicExtId, ApicExtBrdCst
	val = read32(LINK_TRANS_CTRL);
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
