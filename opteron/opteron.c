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
#include "../library/base.h"
#include "../library/access.h"
#include "../library/utils.h"

// approximation before probing
uint32_t Opteron::tsc_mhz = 2200;
uint32_t Opteron::ioh_vendev;
int Opteron::family;

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
	uint64_t msr = lib::rdmsr(MSR_NB_CFG);
	lib::wrmsr(MSR_NB_CFG, msr | (1ULL << 46));

	// disable 32-bit address wrapping to allow 64-bit access in 32-bit code
	msr = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, msr | (1ULL << 17));

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

void Opteron::dram_scrub_disable(void)
{
	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	// disable DRAM scrubbers
	scrub = read32(SCRUB_RATE_CTL);
	if (scrub & 0x1f) {
		write32(SCRUB_RATE_CTL, scrub & ~0x1f);
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

	write32(SCRUB_RATE_CTL, scrub);
	set32(SCRUB_ADDR_LOW, 1);
}

void Opteron::init(void)
{
	uint32_t vendev = read32(VENDEV);
	assert(vendev == VENDEV_OPTERON);

	// detect amount of memory
	dram_base = (uint64_t)(read32(DRAM_BASE) & 0x1fffff) << 27;
	uint64_t dram_limit = ((uint64_t)(read32(DRAM_LIMIT) & 0x1fffff) << 27) | 0x7ffffff;
	dram_size = dram_limit - dram_base + 1;

	// if slave, subtract and disable MMIO hole
	if (!local) {
		uint32_t val = read32(DRAM_HOLE);
		if (val & 1) {
			dram_size -= (val & 0xff00) << (23 - 7);
			write32(DRAM_HOLE, val & ~0xff81);
		}
	}

	// detect number of cores
	cores = 1;
	if (family < 0x15) {
		uint32_t val = read32(LINK_TRANS_CTRL);
		if (val & 0x20)
			cores++; /* Cpu1En */

		val = read32(EXT_LINK_TRANS_CTRL);
		for (int i = 0; i <= 3; i++)
			if (val & (1 << i))
				cores++;
	} else {
		uint32_t val = read32(NB_CAP_2);
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

//	clear32(MCA_NB_CONF, 3 << 20); // prevent watchdog timeout causing syncflood
}

Opteron::~Opteron(void)
{
	// restore 32-bit only access
	uint64_t val = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, val & ~(1ULL << 17));
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
