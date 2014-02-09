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

#include "numachip.h"
#include "registers.h"

void Numachip2::dram_init(void)
{
	i2c_master_seq_read(0x50, 0x00, sizeof(spd_eeprom), (uint8_t *)&spd_eeprom);
	ddr3_spd_check(&spd_eeprom);

	write32(CTAG_BASE + TAG_ADDR_MASK, 0x0);      /* 1GB nCache Tag comparison mask */
	write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);     /* No Tag comparison mask for MTag */
	write32(CTAG_BASE + TAG_MCTR_OFFSET, 0x800);  /* Start CTag @ 1024 MByte MCTR offset */
	write32(CTAG_BASE + TAG_MCTR_MASK, 0xff);     /* Use 128 MByte DDR for CTag */
	write32(MTAG_BASE + TAG_MCTR_OFFSET, 0x1000); /* Start MTag @ 2048 MByte MCTR offset */
	write32(MTAG_BASE + TAG_MCTR_MASK, 0xfff);    /* Use 2048 MByte DDR for MTag */

	printf("%dGB %s ports:", 1 << (spd_eeprom.density_banks - 2),
	  nc2_ddr3_module_type(spd_eeprom.module_type));

	for (int port = 0; port < 3; port++)
		write32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL,
		  ((spd_eeprom.density_banks + 1) << 3) | (1 << 2) | 1);

	const char *mctls[] = {"MTag", "CTag", "NCache"};

	for (int port = 0; port < 3; port++) {
		printf(" %s", mctls[port]);

		while ((read32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL) & 0x42) != 0x42)
			cpu_relax();
	}

	printf("\n");
}
