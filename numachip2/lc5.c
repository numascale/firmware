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

#include <string.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "lc.h"
#include "../bootloader.h"


// returns 1 when link is up
bool LC5::is_up(void)
{
	link_up = numachip.read32(LINKSTAT + index * SIZE) >> 31;
	return link_up;
}

uint64_t LC5::status(void)
{
	uint64_t val = numachip.read32(LINKSTAT + index * SIZE);
	val |= (uint64_t)numachip.read32(EVENTSTAT + index * SIZE) << 32;
	return val;
}

void LC5::check(void)
{
	uint64_t val = status();

	if (val & ~(1ULL<<31))
		warning("Fabric link %u on %03x has issues 0x%016" PRIx64, index, numachip.sci, val);

	/* Clear errors W1TC */
	numachip.write32(LINKSTAT + index * SIZE, val);
	numachip.write32(EVENTSTAT + index * SIZE, val >> 32);

	/* Simple link up/down reporting */
	if ( link_up && ((val & (1ULL<<31)) == 0)) {
		warning("Fabric link %u is down!", index);
		link_up = false;
	}
	if (!link_up && ((val & (1ULL<<31)) != 0)) {
		warning("Fabric link %u is up!", index);
		link_up = true;
	}
}

void LC5::clear(void)
{
	// clear link error bits
	numachip.write32(LINKSTAT + index * SIZE, 7);
}

uint8_t LC5::route1(const sci_t src, const sci_t dst)
{
	if (src == dst)
		return 0;

	uint8_t dim = 0;
	sci_t src2 = src;
	sci_t dst2 = dst;

	while ((src2 ^ dst2) & ~0xf) {
		dim++;
		src2 >>= 4;
		dst2 >>= 4;
	}
	src2 &= 0xf;
	dst2 &= 0xf;

	xassert(dim < 3);
	int out = dim * 2 + 1;
	// 2QOS routing only on LC5 (otherwise we have credit loops)
	out += (dst2 < src2) ? 1 : 0;
	return out;
}

void LC5::commit(void)
{
	for (unsigned chunk = 0; chunk <= numachip.chunk_lim; chunk++) {
		numachip.write32(ROUTE_CHUNK + index * SIZE, chunk);
		for (unsigned offset = 0; offset <= numachip.offset_lim; offset++)
			for (unsigned bit = 0; bit <= numachip.bit_lim; bit++)
				numachip.write32(ROUTE_RAM + index * SIZE + bit * TABLE_SIZE + offset * 4, numachip.xbar_routes[(chunk<<4)+offset][bit]);
	}
}

LC5::LC5(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index)
{
}
