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
#include <stdio.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../library/base.h"
#include "../library/access.h"
#include "../bootloader.h"

void Numachip2::htphy_avalon_write(const int nc, const uint32_t addr, const uint32_t data)
{
	lib::cht_write32(nc, HT_RECFG_DATA, data);
	lib::cht_write32(nc, HT_RECFG_ADDR, (addr & 0x7F));
}

uint32_t Numachip2::htphy_avalon_read(const int nc, const uint32_t addr)
{
	lib::cht_write32(nc, HT_RECFG_ADDR, (addr & 0x7F));
	return lib::cht_read32(nc, HT_RECFG_DATA);
}

void Numachip2::htphy_pma_write(const int nc, const uint32_t logical_channel, const uint32_t offset, const uint32_t data)
{
	htphy_avalon_write(nc, 0x08, logical_channel);
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	htphy_avalon_write(nc, 0x0B, offset);
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	htphy_avalon_write(nc, 0x0C, data);
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	htphy_avalon_write(nc, 0x0A, 0x1);    // Write
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
}

uint32_t Numachip2::htphy_pma_read(const int nc, const uint32_t logical_channel, const uint32_t offset, const uint32_t data)
{
	htphy_avalon_write(nc, 0x08, logical_channel);
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	htphy_avalon_write(nc, 0x0B, offset);
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	htphy_avalon_write(nc, 0x0A, 0x2);    // Read
	while (htphy_avalon_read(nc, 0x0A) & 0x100) {
		cpu_relax();
	}
	return htphy_avalon_read(nc, 0x0C);
}

void Numachip2::htphy_set_deemphasis(const int nc)
{
	// Each value on the deemphasis setting (legal 0-31) is worth +5mV on the first symbol and -20mV on the second.
	const int deemphasis_value = 12;

	for (int channel = 0; channel < 20; channel++)
		htphy_pma_write(nc, channel, 0x02, deemphasis_value);
}
