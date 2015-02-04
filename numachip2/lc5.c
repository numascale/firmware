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
bool LC5::status(void)
{
	return numachip.read32(LINKSTAT + index * SIZE) >> 31;
}

void LC5::check(void)
{
	const uint64_t val = status();
	if (val != 1ULL << 31)
		warning("Fabric link %u on %03x has issues 0x%016" PRIx64, index, numachip.sci, val);
}

void LC5::clear(void)
{
	// clear link error bits
	numachip.write32(LINKSTAT + index * SIZE, 7);
}

LC5::LC5(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index, ROUTE_CHUNK, ROUTE_RAM + _index * SIZE)
{
}
