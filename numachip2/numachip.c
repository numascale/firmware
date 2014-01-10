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

void Numachip2::read_spd(const int spd_no, const ddr3_spd_eeprom_t *spd)
{
	const uint8_t spd_device_adr = 0x50 + spd_no;

	i2c_master_seq_read(spd_device_adr, 0x00, sizeof(ddr3_spd_eeprom_t), (uint8_t *)spd);
	ddr3_spd_check(spd);

	printf("DIMM%d is a %s module\n", spd_no, nc2_ddr3_module_type(spd->module_type));
}

Numachip2::Numachip2(void)
{
	ht = opteron->ht_fabric_fixup(&chip_rev);
	assertf(ht, "NumaChip-II not found");

	memset(card_type, 0, sizeof(card_type));
	spi_master_read(0xffc0, sizeof(card_type), (uint8_t *)card_type);
	spi_master_read(0xfffc, sizeof(uuid), (uint8_t *)uuid);
	printf("NumaChip-II type %s incorporated as HT%d, UUID %08X\n", card_type, ht, uuid);

	/* Read the SPD info from our DIMMs to see if they are supported */
	for (int i = 0; i < 2; i++)
		read_spd(i, &spd_eeproms[i]);
}

uint32_t Numachip2::csr_read(const uint32_t reg)
{
	return cht_readl(ht, reg >> 16, reg & 0xff);
}

void Numachip2::csr_write(const uint32_t reg, const uint32_t val)
{
	cht_writel(ht, reg >> 16, reg & 0xff, val);
}

void Numachip2::set_sci(const sci_t sci)
{
	csr_write(NC2_NODEID, sci);
}

void Numachip2::start_fabric(void)
{
	for (uint16_t i = 0; i < 6; i++)
		lc[i] = new LC5(NC2_LC_BASE + i * NC2_LC_SIZE);
}
