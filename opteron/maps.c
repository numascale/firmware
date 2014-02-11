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
#include "../library/access.h"
#include "../bootloader.h"

/* Fam15h for now */

MmioMap::MmioMap(Opteron &_opteron): opteron(_opteron), ranges(12)
{
}

/* Setup register offsets */
struct reg MmioMap::setup(const int range)
{
	struct reg reg;

	if (range < 8) {
		reg.base = 0x80 + range * 8;
		reg.limit = reg.base + 4;
		reg.high = 0x180 + range * 4;
		return reg;
	}

	if (range < 12) {
		reg.base = 0x1a0 + range * 8;
		reg.limit = reg.base + 4;
		reg.high = 0x1c0 + range * 4;
		return reg;
	}

	fatal("Out of ranges");
}

void MmioMap::remove(const ht_t range)
{
	if (options->debug.maps)
		printf("Deleting MMIO range %d on SCI%03x\n", range, opteron.sci);

	struct reg reg = setup(range);

	for (ht_t ht = opteron.nb_ht_min; ht < opteron.nb_ht_max; ht++) {
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.base, 0);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.limit, 0);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.high, 0);
	}
}

bool MmioMap::read(int range, uint64_t *base, uint64_t *limit, int *dest, int *link, bool *lock)
{
	struct reg reg = setup(range);

	/* Skip disabled ranges */
	uint32_t a = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, reg.base);
	uint32_t b = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, reg.limit);
	uint32_t c = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, reg.high);

	*base = ((uint64_t)(a & ~0xff) << 8) | ((uint64_t)(c & 0xff) << 40);
	*limit = ((uint64_t)(b & ~0xff) << 8) | ((uint64_t)(c & 0xff0000) << (40 - 16)) | 0xffff;
	*dest = b & 7;
	*link = (b >> 4) & 3;

	/* Ensure read and write bits are consistent */
	assert(!(a & 1) == !(a & 2));
	*lock = a & 8;
	return a & 3;
}

int MmioMap::unused(void)
{
	uint64_t base, limit;
	int dest, link;
	bool lock;

	for (int range = 0; range < ranges; range++)
		if (!read(range, &base, &limit, &dest, &link, &lock))
			return range;

	fatal("No free MMIO ranges");
}

void MmioMap::add(const uint64_t base, const uint64_t limit, const uint8_t dest, const int link)
{
	const int range = unused();
	struct reg reg = setup(range);

	if (options->debug.maps)
		printf("Adding host MMIO range %d on SCI%03x: 0x%08llx:0x%08llx to %d\n",
			range, opteron.sci, base, limit, dest);

	assert(limit > base);
	assert((base & 0xffff) == 0);
	assert((limit & 0xffff) == 0xffff);
	assert(range < ranges);

	for (ht_t ht = opteron.nb_ht_min; ht < opteron.nb_ht_max; ht++) {
		uint32_t val = lib::extpci_read32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.base);
		if (val & 3) {
			uint64_t base2, limit2;
			int dest2, link2;
			bool lock2;
			read(range, &base2, &limit2, &dest2, &link2, &lock2);
			fatal("Overwriting SCI%03x#%d MMIO range %d on 0x%08llx:0x%08llx to %d.%d%s", opteron.sci, ht, range, base2, limit2, dest2, link2, lock2 ? " locked" : "");
		}

		uint32_t val2 = ((base >> 16) << 8) | 3;
		uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);
		uint32_t val4 = ((limit >> 40) << 16) | (base >> 40);

		/* Check if locked */
		if ((val & 8) && ((val2 != (val & ~8))
		  || (val3 != lib::extpci_read32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.base))
		  || (val4 != lib::extpci_read32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.high)))) {
			uint64_t old_base, old_limit;
			int old_dest, old_link;
			bool old_lock;

			read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
			warning("Unable to overwrite locked MMIO range %d on SCI%03x#%d 0x%llx:0x%llx to %d.%d with 0x%llx:0x%llx to %d.%d",
				range, opteron.sci, ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
			return;
		}

		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.high, val4);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.limit, val3);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, reg.base, val2);
	}
}

DramMap::DramMap(Opteron &_opteron): opteron(_opteron), ranges(8)
{
}

void DramMap::remove(const int range)
{
	assert(range < ranges);
	if (options->debug.maps)
		printf("Deleting DRAM range %d on SCI%03x\n", range, opteron.sci);

	for (ht_t ht = opteron.nb_ht_min; ht < opteron.nb_ht_max; ht++) {
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x144 + range * 8, 0);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x44 + range * 8, 0);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x140 + range * 8, 0);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x40 + range * 8, 0);
	}
}

bool DramMap::read(const int range, uint64_t *base, uint64_t *limit, int *dest)
{
	assert(range < ranges);

	uint32_t base_l = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, 0x40 + range * 8);
	uint32_t limit_l = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, 0x44 + range * 8);
	uint32_t base_h = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, 0x140 + range * 8);
	uint32_t limit_h = lib::extpci_read32(opteron.sci, 0, 24 + opteron.nb_ht_min, Opteron::F1_MAPS, 0x144 + range * 8);

	*base = ((uint64_t)(base_l & ~0xffff) << (24 - 16)) | ((uint64_t)(base_h & 0xff) << 40);
	*limit = ((uint64_t)(limit_l & ~0xffff) << (24 - 16)) | ((uint64_t)(limit_h & 0xff) << 40);
	*dest = limit_l & 7;

	/* Ensure read and write bits are consistent */
	assert(!(base_l & 1) == !(base_l & 2));
	bool en = base_l & 1;
	if (en)
		*limit |= 0xffffff;

	return en;
}

int DramMap::unused(void)
{
	uint64_t base, limit;
	int dest;

	for (int range = 0; range < ranges; range++)
		if (!read(range, &base, &limit, &dest))
			return range;

	fatal("No free DRAM ranges on SCI%03x\n", opteron.sci);
}

void DramMap::print(const int range)
{
	uint64_t base, limit;
	int dest;

	assert(range < ranges);

	if (DramMap::read(range, &base, &limit, &dest))
		printf("SCI%03x DRAM range %d: 0x%012llx:0x%012llx to %d\n", opteron.sci, range, base, limit, dest);
}

void DramMap::add(const int range, const uint64_t base, const uint64_t limit, const ht_t dest)
{
	if (options->debug.maps)
		printf("SCI%03x adding DRAM range %d: 0x%012llx:0x%012llx to %d\n", opteron.sci, range, base, limit, dest);

	assert(range < ranges);
	assert(limit > base);
	assert((base & 0xffffff) == 0);
	assert((limit & 0xffffff) == 0xffffff);

	for (ht_t ht = opteron.nb_ht_min; ht < opteron.nb_ht_max; ht++) {
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x144 + range * 8, limit >> 40);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x44 + range * 8, ((limit >> 8) & ~0xffff) | dest);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x140 + range * 8, base >> 40);
		lib::extpci_write32(opteron.sci, 0, 24 + ht, Opteron::F1_MAPS, 0x40 + range * 8, (base >> 8) | 3);
	}
}
