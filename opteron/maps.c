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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

void Opteron::MmioMap10::remove(const unsigned range)
{
	if (options->debug.maps)
		printf("Deleting NB MMIO range %u on SCI%03x#%d\n", range, opteron.sci, opteron.ht);

	xassert(range < 8);
	opteron.write32(MMIO_MAP_BASE + range * 8, 0);
	opteron.write32(MMIO_MAP_BASE + range * 8, 0);
	opteron.write32(MMIO_MAP_HIGH + range * 4, 0);
}

void Opteron::MmioMap15::remove(const unsigned range)
{
	if (options->debug.maps)
		printf("Deleting NB MMIO range %u on SCI%03x#%d\n", range, opteron.sci, opteron.ht);

	xassert(range < ranges);

	uint16_t loff = 0, hoff = 0;
	if (range >= 8) {
		loff = 0xe0;
		hoff = 0x20;
	}

	opteron.write32(MMIO_MAP_BASE + loff + range * 8, 0);
	opteron.write32(MMIO_MAP_LIMIT + loff + range * 8, 0);
	opteron.write32(MMIO_MAP_HIGH + hoff + range * 4, 0);
}

bool Opteron::MmioMap15::read(unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock)
{
	xassert(range < ranges);

	uint16_t loff = 0, hoff = 0;
	if (range >= 8) {
		loff = 0xe0;
		hoff = 0x20;
	}

	// skip disabled ranges
	uint32_t a = opteron.read32(MMIO_MAP_BASE + loff + range * 8);
	uint32_t b = opteron.read32(MMIO_MAP_LIMIT + loff + range * 8);
	uint32_t c = opteron.read32(MMIO_MAP_HIGH + hoff + range * 4);

	*base = ((uint64_t)(a & ~0xff) << 8) | ((uint64_t)(c & 0xff) << 40);
	*limit = ((uint64_t)(b & ~0xff) << 8) | ((uint64_t)(c & 0xff0000) << (40 - 16)) | 0xffff;
	*dest = b & 7;
	*link = (b >> 4) & 3;

	// ensure read and write bits are consistent
	xassert(!(a & 1) == !(a & 2));
	*lock = a & 8;
	return a & 3;
}

bool Opteron::MmioMap10::read(unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock)
{
	xassert(range < ranges);

	if (range < 8) {
		// skip disabled ranges
		uint32_t a = opteron.read32(MMIO_MAP_BASE + range * 8);
		uint32_t b = opteron.read32(MMIO_MAP_LIMIT + range * 8);

		*base = (uint64_t)(a & ~0xff) << 8;
		*limit = ((uint64_t)(b & ~0xff) << 8) | 0xffff;
		*dest = b & 7;
		*link = (b >> 4) & 3;
		*lock = a & 8;
		return a & 3;
	}

	range -= 8;

	opteron.write32(EXTMMIO_MAP_CTRL, (3 << 28) | range);
	uint32_t b = opteron.read32(EXTMMIO_MAP_DATA);
	opteron.write32(EXTMMIO_MAP_CTRL, (2 << 28) | range);
	uint32_t a = opteron.read32(EXTMMIO_MAP_DATA);

	// 128MB granularity is setup earlier
	*base = (a & ~0xe00000ff) << (27 - 8);
	*limit = (~((b >> 8) & 0x1fffff)) << 20;
	*lock = 0;
	if (a & (1 << 6)) {
		*dest = 0;
		*link = a & 3;
		// assert sublink is zero as we ignore it
		xassert((a & 4) == 0);
	} else {
		*dest = a & 7;
		*link = 0;
	}

	return b & 1;
}

void Opteron::MmioMap::print(const unsigned range)
{
	uint64_t base, limit;
	ht_t dest;
	link_t link;
	bool lock;

	xassert(range < ranges);

	if (read(range, &base, &limit, &dest, &link, &lock))
		printf("NB MMIO range %u on SCI%03x#%d: 0x%08"PRIx64":0x%08"PRIx64" to %d.%d\n",
		  range, opteron.sci, opteron.ht, base, limit, dest, link);
}

unsigned Opteron::MmioMap::unused(void)
{
	uint64_t base, limit;
	ht_t dest;
	link_t link;
	bool lock;

	printf("NB MMIO ranges:\n");
	for (unsigned range = 0; range < ranges; range++)
		print(range);

	for (unsigned range = 0; range < ranges; range++)
		if (!read(range, &base, &limit, &dest, &link, &lock))
			return range;

	fatal("No free NB MMIO ranges");
}

void Opteron::MmioMap15::add(unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link, const bool ro)
{
	const bool ovw = 1;

	if (options->debug.maps)
		printf("Adding NB MMIO range %u on SCI%03x#%x: 0x%08"PRIx64":0x%08"PRIx64" to %d.%d\n",
			range, opteron.sci, opteron.ht, base, limit, dest, link);

	xassert(range < ranges);
	xassert(limit > base);
	xassert((base & 0xffff) == 0);
	xassert((limit & 0xffff) == 0xffff);

	int loff = 0, hoff = 0;
	if (range >= 8) {
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
		fatal("Overwriting NB MMIO range %u 0x%08"PRIx64":0x%08"PRIx64" on SCI%03x#%d to %d.%d%s", range, base2, limit2, opteron.sci, opteron.ht, dest2, link2, lock2 ? " locked" : "");
	}

	uint32_t val2 = ((base >> 16) << 8) | (!ro << 1) | 1;
	uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);
	uint32_t val4 = ((limit >> 40) << 16) | (base >> 40);

	// check if locked
	if ((val & 8) && ((val2 != (val & ~8))
	  || (val3 != opteron.read32(MMIO_MAP_LIMIT + range * 8))
	  || (val4 != opteron.read32(MMIO_MAP_HIGH + hoff + range * 4)))) {
		uint64_t old_base, old_limit;
		ht_t old_dest;
		link_t old_link;
		bool old_lock;

		read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
		warning("Unable to overwrite locked NB MMIO range %u on SCI%03x#%d 0x%"PRIx64":0x%"PRIx64" to %d.%d with 0x%"PRIx64":0x%"PRIx64" to %d.%d",
			range, opteron.sci, opteron.ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
		return;
	}

	opteron.write32(MMIO_MAP_HIGH + hoff + range * 4, val4);
	opteron.write32(MMIO_MAP_LIMIT + loff + range * 8, val3);
	opteron.write32(MMIO_MAP_BASE + loff + range * 8, val2);
}

void Opteron::MmioMap10::add(unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link, const bool ro)
{
	const bool ovw = 1;

	if (options->debug.maps)
		printf("Adding NB MMIO range %u on SCI%03x#%x: 0x%08"PRIx64":0x%08"PRIx64" to %d.%d\n",
			range, opteron.sci, opteron.ht, base, limit, dest, link);

	xassert(range < ranges);
	xassert(limit > base);
	xassert((base & 0xffff) == 0);
	xassert((limit & 0xffff) == 0xffff);

	if (range < 8) {
		xassert(limit < (1ULL << 40));
		uint32_t val = opteron.read32(MMIO_MAP_BASE + range * 8);
		if ((val & 3) && !ovw) {
			uint64_t base2, limit2;
			ht_t dest2;
			link_t link2;
			bool lock2;
			read(range, &base2, &limit2, &dest2, &link2, &lock2);
			fatal("Overwriting NB MMIO range %u 0x%08"PRIx64":0x%08"PRIx64" on SCI%03x#%d to %d.%d%s", range, base2, limit2, opteron.sci, opteron.ht, dest2, link2, lock2 ? " locked" : "");
		}

		uint32_t val2 = ((base >> 16) << 8) | (!ro << 1) | 1;
		uint32_t val3 = ((limit >> 16) << 8) | dest | (link << 4);

		// check if locked
		if ((val & 8) && ((val2 != (val & ~8))
		  || (val3 != opteron.read32(MMIO_MAP_LIMIT + range * 8)))) {
			uint64_t old_base, old_limit;
			ht_t old_dest;
			link_t old_link;
			bool old_lock;

			read(range, &old_base, &old_limit, &old_dest, &old_link, &old_lock);
			warning("Unable to overwrite locked NB MMIO range %u on SCI%03x#%d 0x%"PRIx64":0x%"PRIx64" to %d.%d with 0x%"PRIx64":0x%"PRIx64" to %d.%d",
				range, opteron.sci, opteron.ht, old_base, old_limit, old_dest, old_link, base, limit, dest, link);
			return;
		}

		opteron.write32(MMIO_MAP_LIMIT + range * 8, val3);
		opteron.write32(MMIO_MAP_BASE + range * 8, val2);
		return;
	}

	// Fam10h extended MMIO 128MB granularity
	xassert((base & 0x7ffffff) == 0);
	xassert((limit & 0x7ffffff) == 0x7ffffff);
	xassert(poweroftwo(limit - base + 1));
	range -= 8;

	// reading an uninitialised extended MMIO ranges results in MCE, so can't assert

	// FIXME: Use 2's complement
	uint64_t mask = 0;
	base  >>= 27;
	limit >>= 27;

	while ((base | mask) != (limit | mask))
		mask = (mask << 1) | 1;

	opteron.write32(EXTMMIO_MAP_CTRL, (2 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, (base << 8) | dest);
	opteron.write32(EXTMMIO_MAP_CTRL, (3 << 28) | range);
	opteron.write32(EXTMMIO_MAP_DATA, (mask << 8) | 1);
}

void Opteron::MmioMap::add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link)
{
	const unsigned range = unused();
	add(range, base, limit, dest, link);
}

Opteron::DramMap::DramMap(Opteron &_opteron): opteron(_opteron)
{
}

void Opteron::DramMap::remove(unsigned range)
{
	if (options->debug.maps)
		printf("Deleting NB DRAM range %u on SCI%03x#%x\n", range, opteron.sci, opteron.ht);

	xassert(range < 8);

	opteron.write32(DRAM_MAP_LIMIT_HIGH + range * 8, 0);
	opteron.write32(DRAM_MAP_LIMIT + range * 8, 0);
	opteron.write32(DRAM_MAP_BASE_HIGH + range * 8, 0);
	opteron.write32(DRAM_MAP_BASE + range * 8, 0);
}

bool Opteron::DramMap::read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest)
{
	xassert(range < 8);

	uint32_t base_l = opteron.read32(DRAM_MAP_BASE + range * 8);
	uint32_t limit_l = opteron.read32(DRAM_MAP_LIMIT + range * 8);
	uint32_t base_h = opteron.read32(DRAM_MAP_BASE_HIGH + range * 8);
	uint32_t limit_h = opteron.read32(DRAM_MAP_LIMIT_HIGH + range * 8);

	*base = ((uint64_t)(base_l & ~0xffff) << (24 - 16)) | ((uint64_t)(base_h & 0xff) << 40);
	*limit = ((uint64_t)(limit_l & ~0xffff) << (24 - 16)) | ((uint64_t)(limit_h & 0xff) << 40);
	*dest = limit_l & 7;

	// ensure read and write bits are consistent
	xassert(!(base_l & 1) == !(base_l & 2));
	bool en = base_l & 1;
	if (en)
		*limit |= 0xffffff;

	return en;
}

unsigned Opteron::DramMap::unused(void)
{
	uint64_t base, limit;
	ht_t dest;

	for (unsigned range = 0; range < 8; range++)
		if (!read(range, &base, &limit, &dest))
			return range;

	fatal("No free NB DRAM ranges on SCI%03x#%d\n", opteron.sci, opteron.ht);
}

void Opteron::DramMap::print(const unsigned range)
{
	uint64_t base, limit;
	ht_t dest;

	xassert(range < 8);

	if (read(range, &base, &limit, &dest))
		printf("NB DRAM range %u on SCI%03x#%d: 0x%012"PRIx64":0x%012"PRIx64" to %d\n",
		  range, opteron.sci, opteron.ht, base, limit, dest);
}

void Opteron::DramMap::add(const unsigned range, const uint64_t base, const uint64_t limit, const ht_t dest)
{
	if (options->debug.maps)
		printf("Adding NB DRAM range %u on SCI%03x#%d: 0x%012"PRIx64":0x%012"PRIx64" to %d\n",
		  range, opteron.sci, opteron.ht, base, limit, dest);

	xassert(range < 8);
	xassert(limit > base);
	xassert((base & 0xffffff) == 0);
	xassert((limit & 0xffffff) == 0xffffff);

	opteron.write32(DRAM_MAP_LIMIT_HIGH + range * 8, limit >> 40);
	opteron.write32(DRAM_MAP_LIMIT + range * 8, ((limit >> 8) & ~0xffff) | dest);
	opteron.write32(DRAM_MAP_BASE_HIGH + range * 8, base >> 40);
	opteron.write32(DRAM_MAP_BASE + range * 8, (base >> 8) | 3);
}
