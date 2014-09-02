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

#include "../library/base.h"

struct reg {
	uint16_t base, limit, high;
};

class Opteron {
	class MmioMap {
	protected:
		const Opteron &opteron;
		const unsigned ranges;
		unsigned unused(void);
	public:
		void print(const unsigned range);
		MmioMap(Opteron &_opteron, const unsigned _ranges): opteron(_opteron), ranges(_ranges) {};
		virtual void remove(const unsigned range);
		virtual bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock);
		virtual void add(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link);
		void add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link);
	};
public:
	class MmioMap15: public MmioMap {
	public:
		MmioMap15(Opteron &_opteron): MmioMap(_opteron, 16) {};
		void add(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock);
		void remove(const unsigned range);
	};

	class MmioMap10: public MmioMap {
		struct reg setup(const unsigned range);
	public:
		void add(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock);
		MmioMap10(Opteron &_opteron): MmioMap(_opteron, 8 + 12) {};
		void remove(const unsigned range);
	};
private:
	class DramMap {
		const Opteron &opteron;

		unsigned unused(void);
	public:
		explicit DramMap(Opteron &_opteron);
		void remove(const unsigned range);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest);
		void print(const unsigned range);
		void add(const unsigned range, const uint64_t base, const uint64_t limit, const ht_t dest);
	};

	enum reset {Warm, Cold};
	uint32_t scrub;
	bool local;

	static void reset(const enum reset mode, const int last);
public:
	static const uint32_t VENDEV_MCP55     = 0x036910de;
	static const uint32_t VENDEV_FAM10H    = 0x12001022;
	static const uint32_t VENDEV_FAM15H    = 0x16001022;

	static const reg_t VENDEV              = 0x0000;
	static const reg_t ROUTING             = 0x0040;
	static const reg_t HT_NODE_ID          = 0x0060;
	static const reg_t UNIT_ID             = 0x0064;
	static const reg_t LINK_TRANS_CTRL     = 0x0068;
	static const reg_t LINK_INIT_CTRL      = 0x006c;
	static const reg_t LINK_CTRL           = 0x0084;
	static const reg_t LINK_FREQ_REV       = 0x0088;
	static const reg_t LINK_TYPE           = 0x0098;
	static const reg_t COH_LINK_TRAF_DIST  = 0x0164;
	static const reg_t EXT_LINK_TRANS_CTRL = 0x0168;
	static const reg_t LINK_EXT_CTRL       = 0x0170;
	static const reg_t DRAM_MAP_BASE       = 0x1040;
	static const reg_t DRAM_MAP_LIMIT      = 0x1044;
	static const reg_t MMIO_MAP_BASE       = 0x1080;
	static const reg_t MMIO_MAP_LIMIT      = 0x1084;
	static const reg_t IO_MAP_BASE         = 0x10c0;
	static const reg_t IO_MAP_LIMIT        = 0x10c4;
	static const reg_t CONF_MAP            = 0x10e0;
	static const reg_t DRAM_HOLE           = 0x10f0;
	static const reg_t VGA_ENABLE          = 0x10f4;
	static const reg_t DCT_CONF_SEL        = 0x110c;
	static const reg_t MMIO_MAP_HIGH       = 0x1180;
	static const reg_t EXTMMIO_MAP_CTRL    = 0x1110;
	static const reg_t EXTMMIO_MAP_DATA    = 0x1114;
	static const reg_t DRAM_BASE           = 0x1120;
	static const reg_t DRAM_LIMIT          = 0x1124;
	static const reg_t DRAM_MAP_BASE_HIGH  = 0x1140;
	static const reg_t DRAM_MAP_LIMIT_HIGH = 0x1144;
	static const reg_t MCTL_SEL_LOW        = 0x2110;
	static const reg_t MCTL_CONF_HIGH      = 0x211c;
	static const reg_t MCTL_EXT_CONF_LOW   = 0x21b0;
	static const reg_t MC_NB_CONF          = 0x3044;
	static const reg_t MC_NB_STAT          = 0x3048;
	static const reg_t MC_NB_ADDR          = 0x3050;
	static const reg_t SCRUB_RATE_CTRL     = 0x3058;
	static const reg_t SCRUB_ADDR_LOW      = 0x305c;
	static const reg_t SCRUB_ADDR_HIGH     = 0x3060;
	static const reg_t NB_CONF_1H          = 0x308c;
	static const reg_t CLK_CTRL_0          = 0x30d4;
	static const reg_t NB_CPUID            = 0x30fc;
	static const reg_t MC_NB_DRAM          = 0x3160;
	static const reg_t MC_NB_LINK          = 0x3168;
	static const reg_t MC_NB_L3C           = 0x3170;
	static const reg_t MC_NB_CONF_EXT      = 0x3180;
	static const reg_t DOWNCORE_CTRL       = 0x3190;
	static const reg_t PROBEFILTER_CTRL    = 0x31d4;
	static const reg_t NB_CAP_2            = 0x5084;
	static const reg_t NB_PSTATE_0         = 0x5160;

	static const uint64_t HT_BASE          = 0xfd00000000ULL;
	static const uint64_t HT_LIMIT         = 0x10000000000ULL;
	static const uint32_t MMIO_VGA_BASE    = 0xa0000;
	static const uint32_t MMIO_VGA_LIMIT   = 0xbffff;

	uint64_t dram_base, dram_size;
	static int family;
	static uint32_t ioh_vendev;
	static uint32_t tsc_mhz;
	static uint8_t mc_banks;
	sci_t sci;
	const ht_t ht;
	ht_t ioh_ht, ioh_link;
	unsigned cores;
	MmioMap *mmiomap;
	DramMap drammap;
	friend class MmioMap;
	friend class MmioMap10;
	friend class MmioMap15;
	friend class DramMap;

	void check(void);
	uint64_t read64(const reg_t reg) const;
	uint32_t read32(const reg_t reg) const;
	void write64_split(const reg_t reg, const uint64_t val) const;
	void write32(const reg_t reg, const uint32_t val) const;
	void set32(const reg_t reg, const uint32_t mask) const;
	void clear32(const reg_t reg, const uint32_t mask) const;

	static void prepare(void);
#ifdef NOTNEEDED
	static void restore(void);
#endif
	void dram_scrub_disable(void);
	void dram_scrub_enable(void);
	void disable_syncflood(const ht_t ht);
	void init(void);
	Opteron(const sci_t _sci, const ht_t _ht, const bool _local);
	~Opteron(void);
	static void cht_print(int neigh, int link);
	static uint32_t get_phy_register(const ht_t ht, const link_t link, const int idx, const bool direct);
	static void ht_optimize_link(int nc, int neigh, int link);
	static ht_t ht_fabric_fixup(const uint32_t vendev);
	void dram_clear_start(void);
	void dram_clear_wait(void);
};
