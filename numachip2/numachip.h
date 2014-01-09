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

#ifndef __NUMACHIP_H
#define __NUMACHIP_H

#include "spd.h"
#include "../library/base.h"

class Numachip2 {
	char card_type[16];
	uint32_t chip_rev;
	ddr3_spd_eeprom_t spd_eeproms[2]; /* 0 - MCTag, 1 - CData */

	void i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const int len, uint8_t *data);
	int spi_master_read(const uint16_t addr, const int len, uint8_t *data);
	void read_spd_info(const int spd_no, const ddr3_spd_eeprom_t *spd);
public:
	uint32_t uuid;
	int ht;

	Numachip2(void);
	void csr_write(const uint32_t reg, const uint32_t val);
	void set_sci(const sci_t sci);
};

#endif
