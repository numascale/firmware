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
	class MmioMap {
		Numachip2 &numachip;
	public:
		MmioMap(Numachip2 &_numachip);
		void add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const int range);
		bool read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht);
		void print(const int range);
	};

	class DramMap {
		Numachip2 &numachip;
	public:
		DramMap(Numachip2 &_numachip);
		void add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const int range);
		bool read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht);
		void print(const int range);
	};

	char card_type[16];
	const uint32_t rev;
	struct ddr3_spd_eeprom spd_eeprom;
	LC5 *lcs[6];

	MmioMap mmiomap;
	DramMap drammap;
	friend class MmioMap;
	friend class DramMap;

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
	/* Function in bits 15:12 */
	static const reg_t HT_NODEID       = 0x00c8;
	static const reg_t LC_BASE         = 0x2800;
	static const reg_t LC_SIZE         = 0x0100;
	static const reg_t LC_LINKSTAT     = 0x00c4;
	static const reg_t I2C_REG0        = 0x2040;
	static const reg_t I2C_REG1        = 0x2044;
	static const reg_t SPI_REG0        = 0x2048;
	static const reg_t SPI_REG1        = 0x204c;
	static const reg_t MTAG_BASE       = 0x2080;
	static const reg_t CTAG_BASE       = 0x20a0;
	static const reg_t NCACHE_BASE     = 0x20c0;
	static const reg_t MCTL_SIZE       = 0x20;
	static const reg_t TAG_CTRL        = 0x00;
	static const reg_t TAG_ADDR_MASK   = 0x04;
	static const reg_t TAG_MCTR_OFFSET = 0x08;
	static const reg_t TAG_MCTR_MASK   = 0x0c;
	static const reg_t TAG_CPU_ADDR    = 0x10;
	static const reg_t TAG_CPU_DATA    = 0x18;
	static const reg_t SIU_XBAR_LOW    = 0x2200;
	static const reg_t SIU_XBAR_MID    = 0x2240;
	static const reg_t SIU_XBAR_HIGH   = 0x2280;
	static const reg_t SIU_XBAR_CHUNK  = 0x22c0;
	static const reg_t SIU_NODEID      = 0x22c4;
	static const reg_t SIU_ATT_INDEX   = 0x2300;
	static const reg_t SIU_ATT_ENTRY   = 0x2304;
	static const reg_t SIU_STATUS      = 0x2308;
	static const reg_t HSS_PLLCTL      = 0x2f00;

	/* From Numachip documentation */
	static const reg_t MAP_INDEX      = 0x1044;
	static const reg_t DRAM_BASE      = 0x1048;
	static const reg_t DRAM_LIMIT     = 0x104c;
	static const reg_t MMIO_BASE      = 0x1050;
	static const reg_t MMIO_LIMIT     = 0x1054;
	static const reg_t MMIO_EXTBASE   = 0x1058;
	static const reg_t MMIO_EXTLIMIT  = 0x105c;

	static const uint32_t vendev = 0x07001b47;

	sci_t sci;
	const ht_t ht;
	uint32_t uuid;

	uint32_t read32(const reg_t reg);
	void write32(const reg_t reg, const uint32_t val);
	uint8_t read8(const reg_t reg);
	void write8(const reg_t reg, const uint8_t val);
	Numachip2(const sci_t _sci, const ht_t _ht, const uint32_t _rev);
	void set_sci(const sci_t _sci);
};

#endif
