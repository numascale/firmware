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

#include "lc5.h"
#include "../bootloader.h"

uint64_t LC5::status(void)
{
	uint64_t val = numachip.read32(addr + Numachip2::LC5_LINKSTAT);
	val |= (uint64_t)numachip.read32(addr + Numachip2::LC5_EVENTSTAT) << 32;
	return val;
}

void LC5::check(void)
{
	const uint64_t val = status();
	if (val != 0x0000000080000000)
		warning("Fabric link %u on %03x has issues 0x%016" PRIx64, num, numachip.sci, val);
}

void LC5::clear(void)
{
	// clear link error bits
	numachip.write32(addr + Numachip2::LC5_LINKSTAT, 7);
}

LC5::LC5(Numachip2& _numachip, const uint16_t _addr, const unsigned _num): numachip(_numachip), addr(_addr), num(_num)
{
}
