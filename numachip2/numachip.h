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
#include "lc5.h"
#include "../library/base.h"

class Numachip2 {
	char card_type[16];
	int ht;
	uint32_t chip_rev;
	ddr3_spd_eeprom_t spd_eeproms[2]; /* 0 - MCTag, 1 - CData */
	LC5 *lc[6];

	/* i2c-master.c */
	void i2c_master_init(void);
	uint8_t i2c_master_irqwait(void);
	void i2c_master_busywait(void);
	void i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const int len, uint8_t *data);

	/* spi-master.c */
	void spi_master_enable(void);
	void spi_master_disable(void);
	uint8_t spi_master_read_fifo(void);
	void spi_master_read(const uint16_t addr, const int len, uint8_t *data);

	/* selftest.c */
	void selftest(void);

	void read_spd(const int spd_no, const ddr3_spd_eeprom_t *spd);
public:
	uint32_t uuid;

	uint32_t read32(const uint16_t reg);
	void write32(const uint16_t reg, const uint32_t val);
	uint8_t read8(const uint16_t reg);
	void write8(const uint16_t reg, const uint8_t val);
	Numachip2(void);
	void set_sci(const sci_t sci);
	void start_fabric(void);
};

#endif
