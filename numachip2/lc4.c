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
#include "../platform/config.h"

// returns 1 when link is up
bool LC4::is_up(void)
{
	uint32_t val = numachip.read32(INIT_STATE + index * SIZE) & 0xf;
	link_up = ((val == 2) || (val == 4));
	return link_up;
}

uint64_t LC4::status(void)
{
	uint32_t val = numachip.read32(ERROR_COUNT + index * SIZE);
	return (uint64_t) val;
}

void LC4::check(void)
{
	uint64_t val = status();
	if (val)
		warning("Fabric link %u on %03x has issues 0x%016" PRIx64, index, numachip.sci, val);
}

void LC4::clear(void)
{
	// Clear link error bits, and error counter
	numachip.write32(ERROR_COUNT + index * SIZE, 0);
	numachip.write32(ELOG0 + index * SIZE, 0);
	numachip.write32(ELOG1 + index * SIZE, 0);
}

uint8_t LC4::route1(const sci_t src, const sci_t dst)
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
	// Shortest path routing
	int len = config->size[dim];
	int forward = ((len - src2) + dst2) % len;
	int backward = ((src2 + (len - dst2)) + len) % len;

	out += (forward == backward) ? (src2 & 1) :
	       (backward < forward) ? 1 : 0;
	return out;
}

void LC4::add_route(const sci_t dst, const uint8_t out)
{
	// don't touch packets already on correct dim
	if ((out == 0) || (index / 2  != (out - 1) / 2)) {
		const unsigned regoffset = dst >> 4;
		const unsigned bitoffset = dst & 0xf;
		uint16_t *ent = &link_routes[regoffset];
		*ent |= 1 << bitoffset;
	}
}

void LC4::commit(void)
{
	for (unsigned chunk = 0; chunk <= numachip.chunk_lim; chunk++) {
		numachip.write32(ROUT_CTRL + index * SIZE, (2 << 4) | chunk); // set table routing mode and chunk address
		for (unsigned offset = 0; offset <= numachip.offset_lim; offset++) {
			for (unsigned bit = 0; bit <= numachip.bit_lim; bit++)
				numachip.write32(ROUTE_RAM + index * SIZE + bit * TABLE_SIZE + offset * 4, numachip.xbar_routes[(chunk<<4)+offset][bit]);
			// link routing table
			numachip.write32(SCIROUTE + index * SIZE + offset * 4, link_routes[(chunk<<4)+offset]);
		}
	}
}

LC4::LC4(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index)
{
	memset(link_routes, 0x00, sizeof(link_routes));
}
