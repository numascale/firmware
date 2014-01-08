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

#include "numachip.h"
#include "../bootloader.h"

void Numachip2::read_spd_info(const int spd_no, const ddr3_spd_eeprom_t *spd)
{
	const uint8_t spd_device_adr = 0x50 + spd_no;

	i2c_master_seq_read(spd_device_adr, 0x00, sizeof(ddr3_spd_eeprom_t), (uint8_t *)spd);
	ddr3_spd_check(spd);

	printf("DIMM%d is a %s module\n", spd_no, nc2_ddr3_module_type(spd->module_type));
}

uint32_t Numachip2::identify_eeprom(char p_type[16])
{
	uint8_t p_uuid[4];

	/* Read print type */
	(void)spi_master_read(0xffc0, 16, (uint8_t *)p_type);
	p_type[15] = '\0';

	/* Read UUID */
	(void)spi_master_read(0xfffc, 4, p_uuid);
	return *((uint32_t *)p_uuid);
}

Numachip2::Numachip2(void)
{
	ht = opteron->ht_fabric_fixup(&chip_rev);
	assertf(ht, "NumaChip-II not found");

	printf("NumaChip-II incorporated as HT node %d\n", ht);

	uuid = identify_eeprom(card_type);
	printf("UUID: %08X, TYPE: %s\n", uuid, card_type);

	/* Read the SPD info from our DIMMs to see if they are supported */
	for (int i = 0; i < 2; i++)
		read_spd_info(i, &spd_eeproms[i]);
}

