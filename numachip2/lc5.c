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

#include "lc5.h"
#include "../bootloader.h"

uint32_t LC5::status(void)
{
	return numachip.read32(addr + Numachip2::LC_LINKSTAT);
}

void LC5::clear(void)
{
	// clear link error bits
	numachip.write32(addr + Numachip2::LC_LINKSTAT, 7);
}

LC5::LC5(Numachip2& _numachip, const uint16_t _addr, const char *_name): numachip(_numachip), addr(_addr)
{
	strncpy(name, _name, sizeof(name));
}
