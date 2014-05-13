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

class LC5;

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
	static const unsigned stability_period = 500000;
	static const unsigned i2c_timeout = 1000;
	static const unsigned spi_timeout = 1000;
	char card_type[16];
	struct ddr3_spd_eeprom spd_eeprom;
	LC5 *lcs[6];
	int nlcs;
	const bool local;

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

	uint8_t next(sci_t src, sci_t dst) const;
	void update(const uint16_t dest, const uint8_t bxbarid, const uint8_t link);
	void route(const uint8_t in, const sci_t sci, const uint8_t out);
	void fabric_routing(void);
	void fabric_init(void);
public:
	/* Registers; function in bits 15:12 */
	static const reg_t VENDEV            = 0x0000;
	static const reg_t STAT_COMMAND      = 0x0004;
	static const reg_t CLASS_CODE_REV    = 0x0008;
	static const reg_t HEADER_TYPE       = 0x000c;
	static const reg_t BASE_ADDR_0       = 0x0010;
	static const reg_t FABRIC_CTRL       = 0x0014; // FIXME correct when implemented later
	static const reg_t SCRATCH_0         = 0x0014; // FIXME correct when implemented later
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
	static const reg_t DRAM_BASE         = 0x1070;
	static const reg_t DRAM_LIMIT        = 0x1074;
	static const reg_t PIU_ATT_INDEX     = 0x1078;
	static const reg_t PIU_ATT_ENTRY     = 0x107c;
	static const reg_t PIU_APIC          = 0x1080;
	static const reg_t PIU_APIC_SHIFT    = 0x1084;

	static const reg_t I2C_REG0          = 0x2040;
	static const reg_t I2C_REG1          = 0x2044;
	static const reg_t SPI_REG0          = 0x2048;
	static const reg_t SPI_REG1          = 0x204c;
	static const reg_t MTAG_BASE         = 0x2080;
	static const reg_t CTAG_BASE         = 0x20a0;
	static const reg_t NCACHE_CTRL       = 0x20c0;
	static const reg_t NCACHE_MCTR_OFFSET= 0x20c4;
	static const reg_t NCACHE_MCTR_MASK  = 0x20c8;
	static const reg_t MCTL_SIZE         = 0x20;
	static const reg_t TAG_CTRL          = 0x00;
	static const reg_t TAG_ADDR_MASK     = 0x04;
	static const reg_t TAG_MCTR_OFFSET   = 0x08;
	static const reg_t TAG_MCTR_MASK     = 0x0c;
	static const reg_t TAG_CPU_ADDR      = 0x10;
	static const reg_t TAG_CPU_DATA      = 0x18;
	static const reg_t RMPE_CTRL         = 0x2100;
	static const reg_t LMPE_CTRL         = 0x2180;
	static const reg_t SIU_XBAR          = 0x2200;
	static const reg_t XBAR_TABLE_SIZE   = 0x40;
	static const reg_t XBAR_CHUNK        = 0xc0;
	static const reg_t SIU_NODEID        = 0x22c4;
	static const reg_t SIU_ATT_INDEX     = 0x2300;
	static const reg_t SIU_ATT_ENTRY     = 0x2304;
	static const reg_t SIU_EVENTSTAT     = 0x2308;
	static const reg_t LC_XBAR           = 0x2800;
	static const reg_t LC_SIZE           = 0x100;
	static const reg_t LC_LINKSTAT       = 0xc4;
	static const reg_t LC_EVENTSTAT      = 0xc8;
	static const reg_t LC_ERRORCNT       = 0xcc;
	static const reg_t HSS_PLLCTL        = 0x2f00;

	static const int SIU_ATT_SHIFT = 32;
	static const int MMIO32_ATT_SHIFT = 20;

	static const uint32_t VENDEV_NC2 = 0x07001b47;
	static const uint32_t TIMEOUT_VAL = 0xdeadbeef;

	const static char *ringnames[6];

	sci_t sci;
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

	uint32_t read32(const reg_t reg) const;
	void write32(const reg_t reg, const uint32_t val) const;
	uint8_t read8(const reg_t reg) const;
	void write8(const reg_t reg, const uint8_t val) const;
	static ht_t probe(const sci_t sci);
	Numachip2(const sci_t sci, const ht_t _ht); // remote
	Numachip2(const ht_t _ht); // local
	void set_sci(const sci_t _sci);
	void fabric_train(void);
	void fabric_status(void);
	void fabric_reset(void);
	void routing_dump(void);
};
