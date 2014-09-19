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

#include "numachip.h"
#include "../bootloader.h"

Numachip2::MmioMap::MmioMap(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::MmioMap::add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht)
{
	if (options->debug.maps)
		printf("Adding NC MMIO range %d on SCI%03x: 0x%08llx:0x%08llx to %d\n",
			range, numachip.sci, base, limit, dht);

	xassert(limit > base);
	xassert(range < 8);
	xassert((base & 0xffff) == 0);
	xassert((limit & 0xffff) == 0xffff);

	uint32_t a = ((base >> 16) << 8) | 3;
	uint32_t b = ((limit >> 16) << 8) | dht;

	numachip.write32(MAP_INDEX, range);
	numachip.write32(MMIO_MAP_BASE, a);
	numachip.write32(MMIO_MAP_LIMIT, b);
}

void Numachip2::MmioMap::del(const int range)
{
	if (options->debug.maps)
		printf("Deleting NC MMIO range %d on SCI%03x\n", range, numachip.sci);

	xassert(range < 8);

	numachip.write32(MAP_INDEX, range);
	numachip.write32(MMIO_MAP_BASE, 0);
	numachip.write32(MMIO_MAP_LIMIT, 0);
}

bool Numachip2::MmioMap::read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht)
{
	xassert(range < 8);

	numachip.write32(MAP_INDEX, range);
	uint32_t a = numachip.read32(MMIO_MAP_BASE);
	uint32_t b = numachip.read32(MMIO_MAP_LIMIT);

	*base = (uint64_t)(a & ~0xff) << (16 - 8);
	*limit = ((uint64_t)(b & ~0xff) << (16 - 8)) | 0xffff;
	*dht = b & 7;

	/* Ensure read and write bits are consistent */
	xassert(!(a & 1) == !(a & 2));

	return a & 3;
}

void Numachip2::MmioMap::print(const int range)
{
	uint64_t base, limit;
	uint8_t dht;

	if (read(range, &base, &limit, &dht))
		printf("MMIO range %d on SCI%03x: 0x%08llx:0x%08llx to %d\n", range, numachip.sci, base, limit, dht);
}

Numachip2::DramMap::DramMap(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::DramMap::add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht)
{
	if (options->debug.maps)
		printf("Adding NC DRAM range %d on SCI%03x: 0x%012llx:0x%012llx to %d\n",
			range, numachip.sci, base, limit, dht);

	xassert(limit > base);
	xassert(range < 8);
	xassert((base & 0xffffff) == 0);
	xassert((limit & 0xffffff) == 0xffffff);

	uint32_t a = ((base >> 24) << 8) | 3;
	uint32_t b = ((limit >> 24) << 8) | dht;

	numachip.write32(MAP_INDEX, range);
	numachip.write32(DRAM_MAP_BASE, a);
	numachip.write32(DRAM_MAP_LIMIT, b);
}

void Numachip2::DramMap::del(const int range)
{
	if (options->debug.maps)
		printf("Deleting NC DRAM range %d on SCI%03x\n", range, numachip.sci);

	xassert(range < 8);

	numachip.write32(MAP_INDEX, range);
	numachip.write32(DRAM_MAP_BASE, 0);
	numachip.write32(DRAM_MAP_LIMIT, 0);
}

bool Numachip2::DramMap::read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht)
{
	numachip.write32(MAP_INDEX, range);
	uint32_t a = numachip.read32(DRAM_MAP_BASE);
	uint32_t b = numachip.read32(DRAM_MAP_LIMIT);

	*base = (uint64_t)(a & ~0xff) << (24 - 8);
	*limit = ((uint64_t)(b & ~0xff) << (24 - 8)) | 0xffffff;
	*dht = b & 7;

	/* Ensure read and write bits are consistent */
	xassert(!(a & 1) == !(a & 2));

	return a & 3;
}

void Numachip2::DramMap::print(const int range)
{
	uint64_t base, limit;
	uint8_t dht;

	if (read(range, &base, &limit, &dht))
		printf("NC DRAM range %d on SCI%03x: 0x%012llx:0x%012llx to %d\n", range, numachip.sci, base, limit, dht);
}

