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

#pragma once

#include "spd.h"
#include "../library/base.h"

class LC;

class Numachip2 {
	class MmioMap {
		const Numachip2 &numachip;
	public:
		MmioMap(Numachip2 &_numachip);
		void add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const int range);
		bool read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht);
		void print(const int range);
	};

	class DramMap {
		const Numachip2 &numachip;
	public:
		DramMap(Numachip2 &_numachip);
		void add(const int range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const int range);
		bool read(const int range, uint64_t *base, uint64_t *limit, uint8_t *dht);
		void print(const int range);
	};

	class DramAtt {
		const Numachip2 &numachip;
		unsigned depth;
	public:
		DramAtt(Numachip2 &_numachip);
		void range(const uint64_t base, const uint64_t limit, const sci_t dest);
	};

	class MmioAtt {
		const Numachip2 &numachip;
	public:
		MmioAtt(Numachip2 &_numachip);
		void range(const uint64_t base, const uint64_t limit, const sci_t dest);
	};

	static const unsigned training_period = 20000;
	static const unsigned dram_training_period = 500000;
	static const unsigned stability_period = 500000;
	static const unsigned i2c_timeout = 1000;
	static const unsigned spi_timeout = 1000;
	char card_type[16];
	struct ddr3_spd_eeprom spd_eeprom;
	LC *lcs[6];
	uint16_t routes[7][256][3];
	uint8_t nlcs;
	const bool local;
	const sci_t master;
	unsigned dram_total_shift;

	/* i2c-master.c */
	void i2c_master_init(void);
	uint8_t i2c_master_irqwait(void);
	void i2c_master_busywait(void);
	void i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const unsigned len, uint8_t *data);

	/* spi-master.c */
	void spi_master_enable(void);
	void spi_master_disable(void);
	uint8_t spi_master_read_fifo(void);
	void spi_master_read(const uint16_t addr, const unsigned len, uint8_t *data);

	/* htphy.c */
	static void htphy_avalon_write(const int nc, const uint32_t addr, const uint32_t data);
	static uint32_t htphy_avalon_read(const int nc, const uint32_t addr);
	static void htphy_pma_write(const int nc, const uint32_t logical_channel, const uint32_t offset, const uint32_t data);
	static uint32_t htphy_pma_read(const int nc, const uint32_t logical_channel, const uint32_t offset, const uint32_t data);

	/* dram.c */
	void dram_test(void);
	void dram_clear(void);
	void dram_verify(void);
	void dram_reset(void);
	void dram_init(void);

	uint8_t next(sci_t src, sci_t dst) const;
	void route(const uint8_t in, const sci_t sci, const uint8_t out);
	void fabric_routing(void);
	void routing_dump(void);
	void routing_write(void);
	void fabric_init(void);
public:
	/* Registers; function in bits 15:12 */
	static const reg_t VENDEV            = 0x0000;
	static const reg_t STAT_COMMAND      = 0x0004;
	static const reg_t CLASS_CODE_REV    = 0x0008;
	static const reg_t HEADER_TYPE       = 0x000c;
	static const reg_t BASE_ADDR_0       = 0x0010;
	static const reg_t CAP_PTR           = 0x0034;
	static const reg_t LINK_CTRL         = 0x0084;
	static const reg_t LINK_FREQ_REV     = 0x0088;
	static const reg_t UNIT_ID           = 0x00d0;
	static const reg_t HT_NODE_ID        = 0x00c8;

	static const reg_t MAP_INDEX         = 0x1044;
	static const reg_t DRAM_MAP_BASE     = 0x1048;
	static const reg_t DRAM_MAP_LIMIT    = 0x104c;
	static const reg_t MMIO_MAP_BASE     = 0x1050;
	static const reg_t MMIO_MAP_LIMIT    = 0x1054;
	static const reg_t EXTMMIO_MAP_BASE  = 0x1058;
	static const reg_t EXTMMIO_MAP_LIMIT = 0x105c;
	static const reg_t DRAM_SHARED_BASE  = 0x1070;
	static const reg_t DRAM_SHARED_LIMIT = 0x1074;
	static const reg_t PIU_ATT_INDEX     = 0x1078;
	static const reg_t PIU_ATT_ENTRY     = 0x107c;
	static const reg_t PIU_APIC          = 0x1080;
	static const reg_t PIU_PCIIO_NODE    = 0x1084;
	static const reg_t INFO              = 0x1090;
	static const reg_t INFO_SIZE         = 0x8;
	static const reg_t TIMEOUT_RESP      = 0x10b0;
	static const reg_t GSM_MASK          = 0x10b4;

	static const reg_t I2C_REG0          = 0x2040;
	static const reg_t I2C_REG1          = 0x2044;
	static const reg_t SPI_REG0          = 0x2048;
	static const reg_t SPI_REG1          = 0x204c;
	static const reg_t MTAG_BASE         = 0x2080;
	static const reg_t CTAG_BASE         = 0x20a0;
	static const reg_t NCACHE_CTRL       = 0x20c0;
	static const reg_t NCACHE_MCTR_OFFSET= 0x20c4;
	static const reg_t NCACHE_MCTR_MASK  = 0x20c8;
	static const reg_t NCACHE_MCTR_ADDR  = 0x20cc;
	static const reg_t NCACHE_MCTR_DATA  = 0x20d0;
	static const reg_t HSS_PLLCTL        = 0x20e0;
	static const reg_t MCTL_SIZE         = 0x20;
	static const reg_t TAG_CTRL          = 0x00;
	static const reg_t TAG_ADDR_MASK     = 0x04;
	static const reg_t TAG_MCTR_OFFSET   = 0x08;
	static const reg_t TAG_MCTR_MASK     = 0x0c;
	static const reg_t TAG_CPU_ADDR      = 0x10;
	static const reg_t TAG_CPU_DATA      = 0x18;
	static const reg_t HT_RECFG_DATA     = 0x20F0;
	static const reg_t HT_RECFG_ADDR     = 0x20F4;
	static const reg_t FABRIC_RECFG_DATA = 0x20F8;
	static const reg_t FABRIC_RECFG_ADDR = 0x20FC;
	static const reg_t RMPE_CTRL         = 0x2100;
	static const reg_t LMPE_CTRL         = 0x2180;
	static const reg_t SIU_XBAR          = 0x2200;
	static const reg_t XBAR_TABLE_SIZE   = 0x40;
	static const reg_t XBAR_CHUNK        = 0xc0;
	static const reg_t SIU_NODEID        = 0x22c4;
	static const reg_t SIU_ATT_INDEX     = 0x2300;
	static const reg_t SIU_ATT_ENTRY     = 0x2304;
	static const reg_t SIU_EVENTSTAT     = 0x2308;

	static const unsigned SIU_ATT_SHIFT  = 34;
	static const unsigned MMIO32_ATT_SHIFT = 20;
	static const unsigned GSM_SHIFT      = 43;
	static const unsigned GSM_SIZE_SHIFT = 43;

	static const uint32_t VENDEV_NC2 = 0x07001b47;
	static const uint32_t TIMEOUT_VAL = 0xdeadbeef;

	const static char *ringnames[6];

	const sci_t sci;
	const ht_t ht;

	MmioMap mmiomap;
	friend class MmioMap;
	DramMap drammap;
	friend class DramMap;
	DramAtt dramatt;
	friend class DramAtt;
	MmioAtt mmioatt;
	friend class MmioAtt;

	uint32_t uuid;

	uint64_t read64(const reg_t reg) const;
	void write64_split(const reg_t reg, const uint64_t val) const;
	uint32_t read32(const reg_t reg) const;
	void write32(const reg_t reg, const uint32_t val) const;
	uint8_t read8(const reg_t reg) const;
	void write8(const reg_t reg, const uint8_t val) const;
	static ht_t probe(const sci_t sci);
	void late_init(void);
	Numachip2(const sci_t sci, const ht_t _ht, const bool _local, const sci_t master);
	void fabric_train(void);
	void fabric_check(void);
	void fabric_reset(void);
	void dram_check(void);
	static void htphy_set_deemphasis(const int nc);
};

extern Numachip2 *numachip;
