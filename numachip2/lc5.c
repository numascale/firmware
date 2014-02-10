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

#include "lc5.h"
#include "registers.h"
#include "../bootloader.h"

uint32_t LC5::link_status(void)
{
	return numachip->read32(addr + LC_LINKSTAT);
}

LC5::LC5(uint16_t _addr): addr(_addr)
{
	uint64_t count = 0;
	while (!(numachip->read32(addr + LC_LINKSTAT) & (1 << 31))) {
		if (count++ % 5000000 == 0) {
			printf("<status=%x> reset", numachip->read32(addr + LC_LINKSTAT));
			numachip->write32(HSS_PLLCTL, 1 << 31);
		}
		cpu_relax();
	}
}

