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
/* FIXME: uncomment when implemented:
	return numachip->csr_read(addr + NC2_LC_LINKSTAT); */
	return 0x00000000;
}

LC5::LC5(uint16_t _addr): addr(_addr)
{
	printf("LC5 @ 0x%x has link status %08x\n", addr, link_status());
}

