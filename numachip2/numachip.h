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

class LC5;

class Numachip2 {
	char card_type[16];
	const uint32_t rev;
	struct ddr3_spd_eeprom spd_eeprom;
	LC5 *lcs[6];

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

	/* dram.c */
	void dram_init(void);

	/* fabric.h */
	void fabric_init(void);

	void routing_init(void);
public:
	static const uint32_t vendev = 0x07001b47;

	sci_t sci;
	const ht_t ht;
	uint32_t uuid;

	uint32_t read32(const uint16_t reg);
	void write32(const uint16_t reg, const uint32_t val);
	uint8_t read8(const uint16_t reg);
	void write8(const uint16_t reg, const uint8_t val);
	Numachip2(const sci_t _sci, const ht_t _ht, const uint32_t _rev);
	void set_sci(const sci_t _sci);
};

#endif
