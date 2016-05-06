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
#include "spi.h"
#include "../library/base.h"
#include "../platform/config.h"

class LC;

class Numachip2 {
	class MmioMap {
		const Numachip2 &numachip;
		unsigned used;
	public:
		explicit MmioMap(Numachip2 &_numachip);
		void set(const unsigned range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void add(const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const unsigned range);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, uint8_t *dht) nonnull;
		void print(const unsigned range);
		void print();
	};

	class DramMap {
		const Numachip2 &numachip;
	public:
		explicit DramMap(Numachip2 &_numachip);
		void set(const unsigned range, const uint64_t base, const uint64_t limit, const uint8_t dht);
		void del(const unsigned range);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, uint8_t *dht) nonnull;
		void print(const unsigned range);
		void print();
	};

	class DramAtt {
		const Numachip2 &numachip;
		unsigned depth;
	public:
		explicit DramAtt(Numachip2 &_numachip);
		void init(void);
		void range(const uint64_t base, const uint64_t limit, const sci_t dest);
	};

	class MmioAtt {
		const Numachip2 &numachip;
	public:
		explicit MmioAtt(Numachip2 &_numachip);
		void init(void);
		void range(const uint64_t base, const uint64_t limit, const sci_t dest);
	};

	static const unsigned fabric_training_period = 3000000;
	static const unsigned stability_period = 500000;
	static const unsigned dram_training_period = 500000;
	static const unsigned i2c_timeout = 1000;
	static const unsigned spi_timeout = 1000;
	char card_type[16];
	struct ddr3_spd_eeprom spd_eeprom;
	LC *lcs[6];
	uint8_t nlcs;
	const bool local;
	unsigned dram_total_shift;
	struct spi_board_info board_info;
	uint64_t prev_tval;

	void update_board_info(void);

	/* i2c-master.c */
	void i2c_master_init(void);
	uint8_t i2c_master_irqwait(void);
	void i2c_master_busywait(void);
	void i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const unsigned len, uint8_t *data) nonnull;

	/* spi-master.c */
	void spi_enable(void);
	void spi_disable(void);
	uint8_t spi_read_fifo(void);
	void spi_read(const uint16_t addr, const unsigned len, uint8_t *data) nonnull;
	void spi_write(const uint16_t addr, const unsigned len, uint8_t *data) nonnull;

	/* dram.c */
	void dram_reset(void);
	void dram_init(void);

	/* pe.c */
	void pe_load_microcode(const bool lmpe);
	void pe_init(void);

	/* fabric.c */
	void siu_route(const sci_t sci, const uint8_t out);
	void fabric_init(void);
	bool fabric_trained;
public:
	static const uint64_t MCFG_BASE        = 0x3f0000000000;
	static const uint64_t MCFG_LIM         = 0x3fffffffffff;
	static const uint64_t LOC_BASE         = 0xf0000000;
	static const uint64_t LOC_LIM          = 0xf0ffffff;
	static const uint64_t PIU_APIC_ICR     = LOC_BASE + 0x100000;
	static const uint64_t PIU_TIMER_NOW    = LOC_BASE + 0x200018;
	static const uint64_t PIU_TIMER_RESET  = LOC_BASE + 0x200020;
	static const uint64_t PIU_TIMER_COMP   = LOC_BASE + 0x200028;

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
	static const reg_t HT_INIT_CTRL      = 0x00d8;

	static const reg_t MAP_INDEX         = 0x1044;
	static const reg_t DRAM_MAP_BASE     = 0x1048;
	static const reg_t DRAM_MAP_LIMIT    = 0x104c;
	static const reg_t MMIO_MAP_BASE     = 0x1050;
	static const reg_t MMIO_MAP_LIMIT    = 0x1054;
	static const reg_t MMIO_MAP_HIGH     = 0x1058;
	static const reg_t DRAM_SHARED_BASE  = 0x1070;
	static const reg_t DRAM_SHARED_LIMIT = 0x1074;
	static const reg_t PIU_ATT_INDEX     = 0x1078;
	static const reg_t PIU_ATT_ENTRY     = 0x107c;
	static const reg_t PIU_PCIIO_NODE    = 0x1084;
	static const reg_t INFO              = 0x1090;
	static const reg_t INFO_SIZE         = 0x8;
	static const reg_t TIMEOUT_RESP      = 0x10b0;
	static const reg_t GSM_MASK          = 0x10b4;

	static const reg_t I2C_REG0          = 0x2040;
	static const reg_t I2C_REG1          = 0x2044;
	static const reg_t SPI_REG0          = 0x2048;
	static const reg_t SPI_REG1          = 0x204c;
	static const reg_t FLASH_REG0        = 0x2050;
	static const reg_t FLASH_REG1        = 0x2054;
	static const reg_t FLASH_REG2        = 0x2058;
	static const reg_t FLASH_REG3        = 0x205c;
	static const reg_t MTAG_BASE         = 0x2080;
	static const reg_t CTAG_BASE         = 0x20a0;
	static const reg_t NCACHE_CTRL       = 0x20c0;
	static const reg_t HSS_PLLCTL        = 0x20e0;
	static const reg_t MCTL_SIZE         = 0x20;
	static const reg_t TAG_CTRL          = 0x00;
	static const reg_t TAG_ADDR_MASK     = 0x04;
	static const reg_t TAG_MCTR_OFFSET   = 0x08;
	static const reg_t TAG_MCTR_MASK     = 0x0c;
	static const reg_t TAG_CPU_ADDR      = 0x10;
	static const reg_t TAG_CPU_DATA      = 0x18;
	static const reg_t IMG_PROP_ADDR     = 0x2064;
	static const reg_t IMG_PROP_DATA     = 0x2068;
	static const reg_t IMG_PROP_TEMP     = 0x206c;
	static const reg_t IMG_PROP_FLAGS    = 0x00;
	static const reg_t IMG_PROP_HASH     = 0x01;
	static const reg_t IMG_PROP_STRING   = 0x06;
	static const reg_t HT_RECFG_DATA     = 0x20f0;
	static const reg_t HT_RECFG_ADDR     = 0x20f4;
	static const reg_t FABRIC_RECFG_DATA = 0x20f8;
	static const reg_t FABRIC_RECFG_ADDR = 0x20fc;
	static const reg_t RMPE_CTRL         = 0x2100;
	static const reg_t RMPE_STATUS       = 0x2104;
	static const reg_t RMPE_SEQ_INDEX    = 0x2130;
	static const reg_t RMPE_WCS_ENTRY    = 0x2134;
	static const reg_t RMPE_JUMP_ENTRY   = 0x2138;
	static const reg_t LMPE_CTRL         = 0x2180;
	static const reg_t LMPE_STATUS       = 0x2184;
	static const reg_t LMPE_SEQ_INDEX    = 0x21b0;
	static const reg_t LMPE_WCS_ENTRY    = 0x21b4;
	static const reg_t LMPE_JUMP_ENTRY   = 0x21b8;
	static const reg_t SIU_XBAR_TABLE    = 0x2200;
	static const reg_t SIU_XBAR_CHUNK    = 0x22c0;
	static const reg_t SIU_NODEID        = 0x22c4;
	static const reg_t SIU_ATT_INDEX     = 0x2300;
	static const reg_t SIU_ATT_ENTRY     = 0x2304;
	static const reg_t SIU_EVENTSTAT     = 0x2308;
	static const reg_t SIU_XBAR_TABLE_SIZE = 0x40;
	static const reg_t MCTR_BIST_CTRL    = 0x3040;
	static const reg_t MCTR_BIST_ADDR    = 0x3044;
	static const reg_t MCTR_PHY_STATUS   = 0x3810;
	static const reg_t MCTR_PHY_STATUS2  = 0x3818;
	static const reg_t MCTR_ECC_CONTROL  = 0x3cc0;
	static const reg_t MCTR_ECC_STATUS   = 0x3cc4;

	static const unsigned SIU_ATT_SHIFT  = 34;
	static const unsigned MMIO32_ATT_SHIFT = 20;
	static const unsigned GSM_SHIFT      = 43;
	static const unsigned GSM_SIZE_SHIFT = 43;
	static const unsigned DRAM_RANGES    = 8;
	static const unsigned MMIO_RANGES    = 8;

	static const uint32_t VENDEV_NC2 = 0x07001b47;
	static const uint32_t TIMEOUT_VAL = 0xdeadbeef;

	const Config::node *config;
	const ht_t ht;
	uint8_t linkmask;

	uint16_t siu_routes[256][3];
	static const uint8_t lc_chunks = 4;
	static const uint8_t lc_offsets = 16;
	static const uint8_t lc_bits = 3;

	MmioMap mmiomap;
	friend class MmioMap;
	DramMap drammap;
	friend class DramMap;
	DramAtt dramatt;
	friend class DramAtt;
	MmioAtt mmioatt;
	friend class MmioAtt;

	uint64_t read64(const reg_t reg) const;
	void write64_split(const reg_t reg, const uint64_t val) const;
	uint32_t read32(const reg_t reg) const;
	void write32(const reg_t reg, const uint32_t val) const;
	uint16_t read16(const reg_t reg) const;
	void write16(const reg_t reg, const uint16_t val) const;
	uint8_t read8(const reg_t reg) const;
	void write8(const reg_t reg, const uint8_t val) const;
	void apic_icr_write(const uint32_t low, const uint32_t apicid);
	static ht_t probe(const sci_t sci);
	static ht_t probe_slave(const sci_t sci);
	void late_init(void);
	uint32_t rom_read(const uint8_t reg);
	Numachip2(const Config::node *_config, const ht_t _ht, const bool _local, const sci_t master_id);
	bool fabric_train(void);
	void fabric_routing(void);
	void fabric_check(void) const;
	void fabric_reset(void);
	void dram_check(void) const;
	void check(void) const;
	void flash(const uint8_t *buf, const size_t len);
};

extern Numachip2 *numachip;
