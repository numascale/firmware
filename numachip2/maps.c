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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

Numachip2::MmioMap::MmioMap(Numachip2 &_numachip): numachip(_numachip), used(0)
{
}

void Numachip2::MmioMap::set(const unsigned range, const uint64_t base, const uint64_t limit, const uint8_t dht)
{
	if (options->debug.maps)
		printf("Adding NC MMIO range %d on %s: 0x%08" PRIx64 ":0x%08" PRIx64 " to %d\n",
			range, pr_node(numachip.config->id), base, limit, dht);

	xassert(limit > base);
	xassert(range < Numachip2::MMIO_RANGES);
	xassert((base & 0xffff) == 0);
	xassert((limit & 0xffff) == 0xffff);

	uint32_t a = ((base >> 16) << 8) | 3;
	uint32_t b = ((limit >> 16) << 8) | dht;
	uint32_t c = (base >> 40) | ((limit >> 40) << 16);

	numachip.write32(MAP_INDEX, range);
	numachip.write32(MMIO_MAP_BASE, a);
	numachip.write32(MMIO_MAP_LIMIT, b);
	numachip.write32(MMIO_MAP_HIGH, c);
	xassert(numachip.read32(MMIO_MAP_BASE) == a);
	xassert(numachip.read32(MMIO_MAP_LIMIT) == b);
	xassert(numachip.read32(MMIO_MAP_HIGH) == c);
}

void Numachip2::MmioMap::add(const uint64_t base, const uint64_t limit, const uint8_t dht)
{
	xassert(used < Numachip2::MMIO_RANGES);
	set(used, base, limit, dht);
	used++;
}

void Numachip2::MmioMap::del(const unsigned range)
{
	if (options->debug.maps)
		printf("Deleting NC MMIO range %u on %s\n", range, pr_node(numachip.config->id));

	xassert(range < Numachip2::MMIO_RANGES);

	numachip.write32(MAP_INDEX, range);
	numachip.write32(MMIO_MAP_BASE, 0);
	numachip.write32(MMIO_MAP_LIMIT, 0);
	numachip.write32(MMIO_MAP_HIGH, 0);
}

bool Numachip2::MmioMap::read(const unsigned range, uint64_t *base, uint64_t *limit, uint8_t *dht)
{
	xassert(range < Numachip2::MMIO_RANGES);

	numachip.write32(MAP_INDEX, range);
	const uint32_t a = numachip.read32(MMIO_MAP_BASE);
	const uint32_t b = numachip.read32(MMIO_MAP_LIMIT);
	const uint32_t c = numachip.read32(MMIO_MAP_HIGH);

	*base = (uint64_t)(a & ~0xff) << (16 - 8);
	*base |= (uint64_t)(c & 0xff) << 40;
	*limit = ((uint64_t)(b & ~0xff) << (16 - 8)) | 0xffff;
	*limit |= (uint64_t)((c >> 16) & 0xff) << 40;
	*dht = b & 7;

	/* Ensure read and write bits are consistent */
	xassert(!(a & 1) == !(a & 2));

	return a & 3;
}

void Numachip2::MmioMap::print(const unsigned range)
{
	uint64_t base, limit;
	uint8_t dht;

	if (read(range, &base, &limit, &dht))
		printf("NC MMIO range %u on %s: 0x%08" PRIx64 ":0x%08" PRIx64 " to %d\n", range, pr_node(numachip.config->id), base, limit, dht);
}

void Numachip2::MmioMap::print()
{
	for (unsigned range = 0; range < Numachip2::MMIO_RANGES; range++)
		print(range);
}

Numachip2::DramMap::DramMap(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::DramMap::set(const unsigned range, const uint64_t base, const uint64_t limit, const uint8_t dht)
{
	if (options->debug.maps)
		printf("Adding NC DRAM range %u on %s: 0x%012" PRIx64 ":0x%012" PRIx64 " to %d\n",
			range, pr_node(numachip.config->id), base, limit, dht);

	xassert(limit > base);
	xassert(range < Numachip2::DRAM_RANGES);
	xassert((base & 0xffffff) == 0);
	xassert((limit & 0xffffff) == 0xffffff);

	uint32_t a = ((base >> 24) << 8) | 3;
	uint32_t b = ((limit >> 24) << 8) | dht;

	numachip.write32(MAP_INDEX, range);
	numachip.write32(DRAM_MAP_BASE, a);
	numachip.write32(DRAM_MAP_LIMIT, b);
	xassert(numachip.read32(DRAM_MAP_BASE) == a);
	xassert(numachip.read32(DRAM_MAP_LIMIT) == b);
}

void Numachip2::DramMap::del(const unsigned range)
{
	if (options->debug.maps)
		printf("Deleting NC DRAM range %u on %s\n", range, pr_node(numachip.config->id));

	xassert(range < Numachip2::DRAM_RANGES);

	numachip.write32(MAP_INDEX, range);
	numachip.write32(DRAM_MAP_BASE, 0);
	numachip.write32(DRAM_MAP_LIMIT, 0);
}

bool Numachip2::DramMap::read(const unsigned range, uint64_t *base, uint64_t *limit, uint8_t *dht)
{
	xassert(range < Numachip2::DRAM_RANGES);

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

void Numachip2::DramMap::print(const unsigned range)
{
	uint64_t base, limit;
	uint8_t dht;

	if (read(range, &base, &limit, &dht))
		printf("NC DRAM range %u on %s: 0x%012" PRIx64 ":0x%012" PRIx64 " to %d\n", range, pr_node(numachip.config->id), base, limit, dht);
}

void Numachip2::DramMap::print()
{
	for (unsigned range = 0; range < Numachip2::DRAM_RANGES; range++)
		print(range);
}
