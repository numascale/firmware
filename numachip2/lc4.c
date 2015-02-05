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
bool LC4::status(void)
{
	uint32_t val = numachip.read32(INIT_STATE + index * SIZE) & 0xf;
	return (val == 2) || (val == 4);
}

void LC4::check(void)
{
	uint32_t val = numachip.read32(ERROR_COUNT + index * SIZE);
	if (val)
		warning("Fabric link %u on %03x has issues 0x%08x", index, numachip.sci, val);
}

void LC4::clear(void)
{
}

LC4::LC4(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index, ROUT_CTRL, ROUTE_RAM + _index * SIZE)
{
}