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

Numachip2::DramAtt::DramAtt(Numachip2 &_numachip): numachip(_numachip)
{
	// detect ATT depth
	numachip.write32(SIU_ATT_INDEX, 0xffff);
	uint32_t val = numachip.read32(SIU_ATT_INDEX) & 0xffff;

	int i;
	for (i = 15; i >= 11; i--)
		if (val & (1 << i))
			break;

	depth = i + 35;
}

void Numachip2::DramAtt::init(void)
{
	if (numachip.local) {
		printf("SIU ATT limited to %dTB\n", 1 << (depth - 40));
		range(0, (1ULL << depth) -1, 0xfff);
	}
}

void Numachip2::DramAtt::range(const uint64_t base, const uint64_t limit, const sci_t dest)
{
	if (options->debug.maps)
		printf("SCI%03x: DRAM ATT 0x%"PRIx64":0x%"PRIx64" to SCI%03x", numachip.sci, base, limit, dest);

	xassert(limit > base);
	xassert(limit < (1ULL << depth));

	const uint64_t mask = (1ULL << SIU_ATT_SHIFT) - 1;
	xassert((base & mask) == 0);
	xassert((limit & mask) == mask);

	numachip.write32(SIU_ATT_INDEX, (1 << 31) | (base >> SIU_ATT_SHIFT));

	for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << SIU_ATT_SHIFT)
		numachip.write32(SIU_ATT_ENTRY, dest);

	if (options->debug.maps)
		printf("\n");
}

Numachip2::MmioAtt::MmioAtt(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::MmioAtt::init(void)
{
	if (numachip.local)
		range(0, (1ULL << 32) - 1, 0xfff);
}

void Numachip2::MmioAtt::range(const uint64_t base, const uint64_t limit, const sci_t dest)
{
	if (options->debug.maps)
		printf("SCI%03x: MMIO32 ATT 0x%"PRIx64":0x%"PRIx64" to SCI%03x", numachip.sci, base, limit, dest);

	xassert(limit > base);
	const uint64_t mask = (1ULL << MMIO32_ATT_SHIFT) - 1;
	xassert((base & mask) == 0);
	xassert((limit & mask) == mask);

	numachip.write32(PIU_ATT_INDEX, (1 << 31) | (0 << 30) | (base >> MMIO32_ATT_SHIFT));

	for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << MMIO32_ATT_SHIFT)
		numachip.write32(PIU_ATT_ENTRY, dest);

	if (options->debug.maps)
		printf("\n");
}
