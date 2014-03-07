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

Numachip2::DramAtt::DramAtt(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::DramAtt::range(const uint64_t base, const uint64_t limit, const sci_t dest)
{
	const uint64_t mask = (1ULL << SIU_ATT_SHIFT) - 1;
	assert((base & mask) == 0);
	assert((limit & mask) == mask);

	numachip.write32(SIU_ATT_INDEX, (1 << 31) | (1 << (SIU_ATT_SHIFT / 4 + 21))
	  | (base >> SIU_ATT_SHIFT));

	uint64_t pos = base;
	while (pos < limit) {
		numachip.write32(SIU_ATT_ENTRY, dest);
		pos += 1ULL << SIU_ATT_SHIFT;
	}
}

Numachip2::MmioAtt::MmioAtt(Numachip2 &_numachip): numachip(_numachip)
{
}

void Numachip2::MmioAtt::range(const uint64_t base, const uint64_t limit, const sci_t dest)
{
	const uint64_t mask = (1ULL << MMIO32_ATT_SHIFT) - 1;
	assert((base & mask) == 0);
	assert((limit & mask) == mask);

	for (uint32_t k = base >> MMIO32_ATT_SHIFT; k < ((limit + 1) >> MMIO32_ATT_SHIFT); k++) {
		numachip.write32(PIU_ATT_INDEX, (1 << 4) | (k / 256));
		numachip.write32(PIU_ATT_ENTRY + (k % 256) * 4, dest);
	}
}
