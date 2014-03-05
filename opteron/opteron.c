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
#include "../library/base.h"
#include "../library/access.h"
#include "defs.h"

/* Approximation before probing */
uint32_t Opteron::tsc_mhz = 2200;
uint32_t Opteron::ioh_vendev;
int Opteron::family;

void Opteron::prepare(void)
{
	/* Enable CF8 extended access */
	uint64_t msr = lib::rdmsr(MSR_NB_CFG);
	lib::wrmsr(MSR_NB_CFG, msr | (1ULL << 46));

	/* Disable 32-bit address wrapping to allow 64-bit access in 32-bit code */
	msr = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, msr | (1ULL << 17));

	/* Enable 64-bit config access */
	msr = lib::rdmsr(MSR_CU_CFG2);
	lib::wrmsr(MSR_CU_CFG2, msr | (1ULL << 50));

	/* Detect processor family */
	uint32_t val = lib::cf8_read32(0, 24 + 0, F3_MISC, 0xfc);
	family = ((val >> 20) & 0xf) + ((val >> 8) & 0xf);
	if (family >= 0x15) {
		val = lib::cf8_read32(0, 24 + 0, F5_EXTD, 0x160);
		tsc_mhz = 200 * (((val >> 1) & 0x1f) + 4) / (1 + ((val >> 7) & 1));
	} else {
		val = lib::cf8_read32(0, 24 + 0, F3_MISC, 0xd4);
		uint64_t val6 = lib::rdmsr(MSR_COFVID_STAT);
		tsc_mhz = 200 * ((val & 0x1f) + 4) / (1 + ((val6 >> 22) & 1));
	}

	printf("Family %xh Opteron with %dMHz NB TSC frequency\n", family, tsc_mhz);

	/* Detect IOH */
	ioh_vendev = lib::cf8_read32(0, 0, 0, 0);
	assert(ioh_vendev != 0xffffffff);

	switch (ioh_vendev) {
	case VENDEV_SR5690:
	case VENDEV_SR5670:
	case VENDEV_SR5650:
		/* Enable 52-bit PCIe address generation */
		val = lib::cf8_read32(0, 0, 0, 0xc8);
		lib::cf8_write32(0, 0, 0, 0xc8, val | (1 << 15));
	}
}

Opteron::Opteron(const sci_t _sci, const ht_t _ht): sci(_sci), ht(_ht), mmiomap(*this), drammap(*this)
{
	uint32_t vendev = read32(F0_HT, 0x0);
	assert(vendev == 0x12001022); // FIXME: VENDEV somehow equals 0x00b71efe

	/* Enable CF8 extended access; Linux needs this later */
	uint32_t val = read32(F3_MISC, 0x8c);
	write32(F3_MISC, 0x8c, val | (1 << (46 - 32)));

	/* Set CHtExtAddrEn, ApicExtId, ApicExtBrdCst */
	val = read32(F0_HT, 0x68);
	if ((val & ((1 << 25) | (1 << 18) | (1 << 17))) != ((1 << 25) | (1 << 18) | (1 << 17)))
		write32(F0_HT, 0x68, val | (1 << 25) | (1 << 18) | (1 << 17));

	/* Enable 128MB-granularity on extended MMIO maps */
	if (Opteron::family < 0x15) {
		val = read32(F0_HT, 0x168);
		write32(F0_HT, 0x168, (val & ~0x300) | 0x200);
	}

	/* Enable Addr64BitEn on IO links */
	for (int i = 0; i < 4; i++) {
		/* Skip coherent/disabled links */
		val = read32(F0_HT, 0x98 + i * 0x20);
		if (!(val & (1 << 2)))
			continue;

		val = read32(F0_HT, 0x84 + i * 0x20);
		if (!(val & (1 << 15)))
			write32(F0_HT, 0x84 + i * 0x20, val | (1 << 15));
	}
}

Opteron::~Opteron(void)
{
	/* Restore 32-bit only access */
	uint64_t val = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, val & ~(1ULL << 17));
}

uint32_t Opteron::read32(const uint8_t func, const uint16_t reg)
{
	return lib::mcfg_read32(sci, 0, 24 + ht, func, reg);
}

void Opteron::write32(const uint8_t func, const uint16_t reg, const uint32_t val)
{
	lib::mcfg_write32(sci, 0, 24 + ht, func, reg, val);
}
