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

#include <stdio.h>
#include <string.h>

#include "numachip.h"
#include "registers.h"
#include "../library/access.h"
#include "../bootloader.h"

Numachip2::Numachip2(const sci_t _sci, const ht_t _ht, const uint32_t _rev):rev(_rev),  sci(_sci), ht(_ht)
{
	write32(SIU_NODEID, sci);

	spi_master_read(0xffc0, sizeof(card_type), (uint8_t *)card_type);
	spi_master_read(0xfffc, sizeof(uuid), (uint8_t *)uuid);
	printf("NumaChip2 type %s incorporated as HT%d, UUID %08X\n", card_type, ht, uuid);

	selftest();

	if (!config->node->sync_only)
		fabric_init();
	dram_init();
}

uint32_t Numachip2::read32(const uint16_t reg)
{
	return lib::cht_read32(ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write32(const uint16_t reg, const uint32_t val)
{
	lib::cht_write32(ht, reg >> 12, reg & 0xfff, val);
}

uint8_t Numachip2::read8(const uint16_t reg)
{
	return lib::cht_read32(ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write8(const uint16_t reg, const uint8_t val)
{
	lib::cht_write8(ht, reg >> 12, reg & 0xfff, val);
}

