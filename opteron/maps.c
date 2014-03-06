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

Opteron::MmioMap::MmioMap(Opteron &_opteron): opteron(_opteron)
{
	if (family >= 0x15)
		ranges = 12;
	else
		ranges = 8 + 16;
}

/* Setup register offsets */
struct reg Opteron::MmioMap::setup(const int range)
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

void Opteron::MmioMap::remove(const int range)
{
	if (options->debug.maps)
		printf("Deleting MMIO range %d on SCI%03x\n", range, opteron.sci);

	struct reg reg = setup(range);

	opteron.write32(0x1000 | reg.base, 0);
	opteron.write32(0x1000 | reg.limit, 0);
	opteron.write32(0x1000 | reg.high, 0);
}

bool Opteron::MmioMap::read(int range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock)
{
	if (family >= 0x15) {
		assert(range < 12);

		int loff = 0, hoff = 0;
		if (range > 7) {
			loff = 0xe0;
			hoff = 0x20;
		}

		/* Skip disabled ranges */
		uint32_t a = opteron.read32(MMIO_MAP_BASE + loff + range * 8);
		uint32_t b = opteron.read32(MMIO_MAP_LIMIT + loff + range * 8);
		uint32_t c = opteron.read32(MMIO_MAP_HIGH + hoff + range * 4);

		*base = ((uint64_t)(a & ~0xff) << 8) | ((uint64_t)(c & 0xff) << 40);
		*limit = ((uint64_t)(b & ~0xff) << 8) | ((uint64_t)(c & 0xff0000) << (40 - 16)) | 0xffff;
		*dest = b & 7;
		*link = (b >> 4) & 3;

		/* Ensure read and write bits are consistent */
		assert(!(a & 1) == !(a & 2));
		*lock = a & 8;
		return a & 3;
	}

	/* Family 10h */
	if (range < 8) {
		/* Skip disabled ranges */
		uint32_t a = opteron.read32(MMIO_MAP_BASE + range * 8);
		uint32_t b = opteron.read32(MMIO_MAP_LIMIT + range * 8);

		*base = (uint64_t)(a & ~0xff) << 8;
		*limit = ((uint64_t)(b & ~0xff) << 8) | 0xffff;
		*dest = b & 7;
		*link = (b >> 4) & 3;
		*lock = a & 8;
		return a & 3;
	}

	assert(range < 12);
	range -= 8;

	opteron.write32(EXTMMIO_MAP_CTRL, (2 << 28) | range);
	uint32_t a = opteron.read32(EXTMMIO_MAP_DATA);
	opteron.write32(EXTMMIO_MAP_CTRL, (3 << 28) | range);
	uint32_t b = opteron.read32(EXTMMIO_MAP_DATA);

	/* 128MB granularity is setup earlier */
	*base = (a & ~0xe00000ff) << (27 - 8);
	*limit = (~((b >> 8) & 0x1fffff)) << 20;
	*lock = 0;
	if (a & (1 << 6)) {
		*dest = 0;
		*link = a & 3;
		/* Assert sublink is zero as we ignore it */
		assert((a & 4) == 0);
	} else {
		*dest = a & 7;
		*link = 0;
	}

	return b & 1;

#ifdef NEWWORLD
	struct reg reg = setup(range);
	/* Skip disabled ranges */
	uint32_t a = opteron.read32(0x1000 | reg.base);
	uint32_t b = opteron.read32(0x1000 | reg.limit);
	uint32_t c = opteron.read32(0x1000 | reg.high);

	*base = ((uint64_t)(a & ~0xff) << 8) | ((uint64_t)(c & 0xff) << 40);
	*limit = ((uint64_t)(b & ~0xff) << 8) | ((uint64_t)(c & 0xff0000) << (40 - 16)) | 0xffff;
	*dest = b & 7;
	*link = (b >> 4) & 3;

	/* Ensure read and write bits are consistent */
	assert(!(a & 1) == !(a & 2));
	*lock = a & 8;
	return a & 3;
#endif
}

void Opteron::MmioMap::print(const int range)
{
	uint64_t base, limit;
	ht_t dest;
	link_t link;
	bool lock;

	assert(range < ranges);

	if (read(range, &base, &limit, &dest, &link, &lock))
		printf("SCI%03x MMIO range %d: 0x%08llx:0x%08llx to %d.%d\n", opteron.sci, range, base, limit, dest, link);
}

int Opteron::MmioMap::unused(void)
{
	uint64_t base, limit;
	ht_t dest;
	link_t link;
	bool lock;

	printf("MMIO ranges:\n");
	for (int range = 0; range < ranges; range++)
		print(range);

	for (int range = 0; range < ranges; range++)
		if (!read(range, &base, &limit, &dest, &link, &lock))
			return range;

	fatal("No free MMIO ranges");
}

void Opteron::MmioMap::add(int range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link)
{
	const bool ovw = 1;

	if (options->debug.maps)
		printf("Adding MMIO range %d on SCI%03x#%x: 0x%08llx:0x%08llx to %d.%d\n",
			range, opteron.sci, opteron.ht, base, limit, dest, link);

	assert(limit > base);
	assert((base & 0xffff) == 0);
	assert((limit & 0xffff) == 0xffff);

	if (family >= 0x15) {
		assert(range < 12);

		int loff = 0, hoff = 0;
		if (range > 7) {
			loff = 0xe0;
			hoff = 0x20;
		}

		uint32_t val = opteron.read32(MMIO_MAP_BASE + loff + range * 8);
		if ((val & 3) && !ovw) {
			uint64_t base2, limit2;
			ht_t dest2;
			link_t link2;
			bool lock2;
			read(range, &base2, &limit2, &dest2, &link2, &lock2);
			fatal("Overwriting SCI%03x#%d MMIO range %d on 0x%08llx:0x%08llx to %d.%d%s", opteron.sci, opteron.ht, range, base2, limit2, dest2, link2, lock2 ? " locked" : "");
		}

		uint32_t val2 = ((base >> 16) << 8) | 3;
		uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);
		uint32_t val4 = ((limit >> 40) << 16) | (base >> 40);

		/* Check if locked */
		if ((val & 8) && ((val2 != (val & ~8))
		  || (val3 != opteron.read32(MMIO_MAP_LIMIT + range * 8))
		  || (val4 != opteron.read32(MMIO_MAP_HIGH + hoff + range * 4)))) {
			uint64_t old_base, old_limit;
			ht_t old_dest;
			link_t old_link;
			bool old_lock;

			read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
			warning("Unable to overwrite locked MMIO range %d on SCI%03x#%d 0x%llx:0x%llx to %d.%d with 0x%llx:0x%llx to %d.%d",
				range, opteron.sci, opteron.ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
			return;
		}

		opteron.write32(MMIO_MAP_HIGH + hoff + range * 4, val4);
		opteron.write32(MMIO_MAP_LIMIT + loff + range * 8, val3);
		opteron.write32(MMIO_MAP_BASE + loff + range * 8, val2);
		return;
	}

	/* Family 10h */
	if (range < 8) {
		assert(limit < (1ULL << 40));
		uint32_t val = opteron.read32(MMIO_MAP_BASE + range * 8);
		if ((val & 3) && !ovw) {
			uint64_t base2, limit2;
			ht_t dest2;
			link_t link2;
			bool lock2;
			read(range, &base2, &limit2, &dest2, &link2, &lock2);
			fatal("Overwriting SCI%03x#%d MMIO range %d on 0x%08llx:0x%08llx to %d.%d%s", opteron.sci, opteron.ht, range, base2, limit2, dest2, link2, lock2 ? " locked" : "");
		}

		uint32_t val2 = ((base >> 16) << 8) | 3;
		uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);

		/* Check if locked */
		if ((val & 8) && ((val2 != (val & ~8))
		  || (val3 != opteron.read32(MMIO_MAP_LIMIT + range * 8)))) {
			uint64_t old_base, old_limit;
			ht_t old_dest;
			link_t old_link;
			bool old_lock;

			read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
			warning("Unable to overwrite locked MMIO range %d on SCI%03x#%d 0x%llx:0x%llx to %d.%d with 0x%llx:0x%llx to %d.%d",
				range, opteron.sci, opteron.ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
			return;
		}

		opteron.write32(MMIO_MAP_LIMIT + range * 8, val3);
		opteron.write32(MMIO_MAP_BASE + range * 8, val2);
		return;
	}

	assert(range < 24);
	assert((base & 0xffffffff) == 0);
	assert((limit & 0xffffffff) == 0xffffffff);
	range -= 8;

	/* Reading an uninitialised extended MMIO ranges results in MCE, so can't assert */

	uint64_t mask = 0;
	base  >>= 27;
	limit >>= 27;

	while ((base | mask) != (limit | mask))
		mask = (mask << 1) | 1;

	opteron.write32(EXTMMIO_MAP_CTRL, (2 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, (base << 8) | dest);
	opteron.write32(EXTMMIO_MAP_CTRL, (3 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, (mask << 8) | 1);

#ifdef NEWWORLD
	struct reg reg = setup(range);

	if (options->debug.maps)
		printf("Adding host MMIO range %d on SCI%03x: 0x%08llx:0x%08llx to %d.%d\n",
			range, opteron.sci, base, limit, dest, link);

	assert(limit > base);
	assert((base & 0xffff) == 0);
	assert((limit & 0xffff) == 0xffff);
	assert(range < ranges);

	uint32_t val = opteron.read32(0x1000 | reg.base);
	if (val & 3) {
		uint64_t base2, limit2;
		ht_t dest2;
		link_t link2;
		bool lock2;
		read(range, &base2, &limit2, &dest2, &link2, &lock2);
		fatal("Overwriting SCI%03x#%d MMIO range %d on 0x%08llx:0x%08llx to %d.%d%s", opteron.sci, opteron.ht, range, base2, limit2, dest2, link2, lock2 ? " locked" : "");
	}

	uint32_t val2 = ((base >> 16) << 8) | 3;
	uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);
	uint32_t val4 = ((limit >> 40) << 16) | (base >> 40);

	/* Check if locked */
	if ((val & 8) && ((val2 != (val & ~8))
	  || (val3 != opteron.read32(0x1000 | reg.base))
	  || (val4 != opteron.read32(0x1000 | reg.high)))) {
		uint64_t old_base, old_limit;
		ht_t old_dest;
		link_t old_link;
		bool old_lock;

		read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
		warning("Unable to overwrite locked MMIO range %d on SCI%03x#%d 0x%llx:0x%llx to %d.%d with 0x%llx:0x%llx to %d.%d",
			range, opteron.sci, opteron.ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
		return;
	}

	opteron.write32(0x1000 | reg.high, val4);
	opteron.write32(0x1000 | reg.limit, val3);
	opteron.write32(0x1000 | reg.base, val2);
#endif
}

void Opteron::MmioMap::add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link)
{
	const int range = unused();
	add(range, base, limit, dest, link);
}

Opteron::DramMap::DramMap(Opteron &_opteron): opteron(_opteron)
{
}

void Opteron::DramMap::remove(int range)
{
	if (options->debug.maps)
		printf("Deleting MMIO range %d on SCI%03x#%x\n", range, opteron.sci, opteron.ht);

	if (family >= 0x15) {
		assert(range < 12);

		int loff = 0, hoff = 0;
		if (range > 7) {
			loff = 0xe0;
			hoff = 0x20;
		}

		opteron.write32(MMIO_MAP_BASE + loff + range * 8, 0);
		opteron.write32(MMIO_MAP_LIMIT + loff + range * 8, 0);
		opteron.write32(MMIO_MAP_HIGH + hoff + range * 4, 0);
		return;
	}

	/* Family 10h */
	if (range < 8) {
		opteron.write32(MMIO_MAP_LIMIT + range * 8, 0);
		opteron.write32(MMIO_MAP_BASE + range * 8, 0);
		return;
	}

	assert(range < 12);
	range -= 8;

	opteron.write32(EXTMMIO_MAP_CTRL, (2 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, 0);
	opteron.write32(EXTMMIO_MAP_CTRL, (3 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, 0);

#ifdef NEWWORLD
	assert(range < ranges);
	if (options->debug.maps)
		printf("Deleting DRAM range %d on SCI%03x\n", range, opteron.sci);

	opteron.write32(DRAM_MAP_BASE_LIMIT_HIGH + range * 8, 0);
	opteron.write32(DRAM_MAP_LIMIT + range * 8, 0);
	opteron.write32(DRAM_MAP_BASE_HIGH + range * 8, 0);
	opteron.write32(DRAM_MAP_BASE + range * 8, 0);
#endif
}

bool Opteron::DramMap::read(const int range, uint64_t *base, uint64_t *limit, ht_t *dest)
{
	assert(range < ranges);

	uint32_t base_l = opteron.read32(DRAM_MAP_BASE + range * 8);
	uint32_t limit_l = opteron.read32(DRAM_MAP_LIMIT + range * 8);
	uint32_t base_h = opteron.read32(DRAM_MAP_BASE_HIGH + range * 8);
	uint32_t limit_h = opteron.read32(DRAM_MAP_LIMIT_HIGH + range * 8);

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

int Opteron::DramMap::unused(void)
{
	uint64_t base, limit;
	ht_t dest;

	for (int range = 0; range < ranges; range++)
		if (!read(range, &base, &limit, &dest))
			return range;

	fatal("No free DRAM ranges on SCI%03x\n", opteron.sci);
}

void Opteron::DramMap::print(const int range)
{
	uint64_t base, limit;
	ht_t dest;

	assert(range < ranges);

	if (read(range, &base, &limit, &dest))
		printf("SCI%03x DRAM range %d: 0x%012llx:0x%012llx to %d\n", opteron.sci, range, base, limit, dest);
}

void Opteron::DramMap::add(const int range, const uint64_t base, const uint64_t limit, const ht_t dest)
{
	if (options->debug.maps)
		printf("SCI%03x adding DRAM range %d: 0x%012llx:0x%012llx to %d\n", opteron.sci, range, base, limit, dest);

	assert(range < ranges);
	assert(limit > base);
	assert((base & 0xffffff) == 0);
	assert((limit & 0xffffff) == 0xffffff);

	opteron.write32(DRAM_MAP_LIMIT_HIGH + range * 8, limit >> 40);
	opteron.write32(DRAM_MAP_LIMIT + range * 8, ((limit >> 8) & ~0xffff) | dest);
	opteron.write32(DRAM_MAP_BASE_HIGH + range * 8, base >> 40);
	opteron.write32(DRAM_MAP_BASE + range * 8, (base >> 8) | 3);
}
