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

LC5::LC5(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index, ROUTE_CHUNK + _index * SIZE, ROUTE_RAM + _index * SIZE)
{
}
