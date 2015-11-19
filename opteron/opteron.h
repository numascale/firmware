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

class Opteron {
	class MmioMap {
	protected:
		const Opteron &opteron;
		unsigned unused(void);
	public:
		const unsigned ranges;
		void print(const unsigned range);
		void print();
		MmioMap(Opteron &_opteron, const unsigned _ranges): opteron(_opteron), ranges(_ranges) {};
		virtual void remove(const unsigned range);
		void remove(const uint64_t base, const uint64_t limit);
		virtual bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock) nonnull;
		virtual void set(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link, const bool ro=0);
		void add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link);
	};
public:
	class MmioMap15: public MmioMap {
	public:
		explicit MmioMap15(Opteron &_opteron): MmioMap(_opteron, 12) {};
		void set(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link, const bool ro=0);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock) nonnull;
		void remove(const unsigned range);
	};

	class MmioMap10: public MmioMap {
	public:
		explicit MmioMap10(Opteron &_opteron): MmioMap(_opteron, 8 + 16) {};
		void set(const unsigned range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link, const bool ro=0);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock) nonnull;
		void remove(const unsigned range);
	};
private:
	class DramMap {
		const Opteron &opteron;
		unsigned unused(void);
	public:
		explicit DramMap(Opteron &_opteron): opteron(_opteron) {};
		void remove(const unsigned range);
		bool read(const unsigned range, uint64_t *base, uint64_t *limit, ht_t *dest) nonnull;
		void print(const unsigned range);
		void print();
		void set(const unsigned range, const uint64_t base, const uint64_t limit, const ht_t dest);
	};

	uint32_t scrub;
	bool local;

	static uint32_t phy_read32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct);
	static void phy_write32(const ht_t ht, const link_t link, const uint16_t reg, const bool direct, const uint32_t val);
	static void platform_reset_cold(void);
	static void platform_reset_warm(void);
	static void cht_print(const ht_t neigh, const link_t link);
	static void optimise_linkbuffers(const ht_t ht, const int link);
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
	static const reg_t LINK_BASE_BUF_CNT   = 0x0090;
	static const reg_t LINK_ISOC_BUF_CNT   = 0x0094;
	static const reg_t LINK_TYPE           = 0x0098;
	static const reg_t LINK_FREQ_EXT       = 0x009c;
	static const reg_t LINK_RETRY          = 0x0130;
	static const reg_t LINK_RETRY_CTRL     = 0x0150;
	static const reg_t COH_LINK_TRAF_DIST  = 0x0164;
	static const reg_t EXT_LINK_TRANS_CTRL = 0x0168;
	static const reg_t LINK_GLO_CTRL_EXT   = 0x016c;
	static const reg_t LINK_EXT_CTRL       = 0x0170;
	static const reg_t LINK_INIT_STATUS    = 0x01a0;
	static const reg_t COH_LINK_PAIR_DIST  = 0x01e0;
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
	static const reg_t DRAM_CS_BASE        = 0x2040;
	static const reg_t TRACE_BUF_BASELIM   = 0x20b8;
	static const reg_t TRACE_BUF_ADDR      = 0x20bc;
	static const reg_t TRACE_BUF_CTRL      = 0x20c0;
	static const reg_t TRACE_START         = 0x20c4;
	static const reg_t TRACE_STOP          = 0x20c8;
	static const reg_t TRACE_CAPTURE       = 0x20cc;
	static const reg_t MCTL_SEL_LOW        = 0x2110;
	static const reg_t MCTL_CONF_HIGH      = 0x211c;
	static const reg_t TRACE_BUF_ADDR_HIGH = 0x2120;
	static const reg_t MCTL_EXT_CONF_LOW   = 0x21b0;
	static const reg_t MC_NB_CTRL          = 0x3040;
	static const reg_t MC_NB_CONF          = 0x3044;
	static const reg_t MC_NB_STAT          = 0x3048;
	static const reg_t MC_NB_ADDR          = 0x3050;
	static const reg_t SCRUB_RATE_CTRL     = 0x3058;
	static const reg_t SCRUB_ADDR_LOW      = 0x305c;
	static const reg_t SCRUB_ADDR_HIGH     = 0x3060;
	static const reg_t GART_APER_BASE      = 0x3090;
	static const reg_t GART_CACHE_CTRL     = 0x309c;
	static const reg_t ONLN_SPARE_CTRL     = 0x30b0;
	static const reg_t ARRAY_ADDR          = 0x30b8;
	static const reg_t ARRAY_DATA          = 0x30bc;
	static const reg_t CLK_CTRL_0          = 0x30d4;
	static const reg_t NB_CPUID            = 0x30fc;
	static const reg_t MC_NB_DRAM          = 0x3160;
	static const reg_t MC_NB_LINK          = 0x3168;
	static const reg_t MC_NB_L3C           = 0x3170;
	static const reg_t MC_NB_CONF_EXT      = 0x3180;
	static const reg_t DOWNCORE_CTRL       = 0x3190;
	static const reg_t L3_CTRL             = 0x31b8;
	static const reg_t L3_CACHE_PARAM      = 0x31c4;
	static const reg_t PROBEFILTER_CTRL    = 0x31d4;
	static const reg_t C_STATE_CTRL        = 0x4128;
	static const reg_t LINK_PHY_OFFSET     = 0x4180;
	static const reg_t LINK_PHY_DATA       = 0x4184;
	static const reg_t NB_CAP_2            = 0x5084;
	static const reg_t NB_CONF_4           = 0x5088;
	static const reg_t NB_PSTATE_0         = 0x5160;
	static const reg_t NB_PSTATE_CTRL      = 0x5170;
	static const reg_t LINK_PROD_INFO      = 0x5190;

	static const reg_t PHY_COMPCAL_CTRL1   = 0x00e0;
	static const reg_t PHY_RX_PROC_CTRL_CADIN0 = 0x4011;
	static const reg_t PHY_RX_DLL_CTRL5_CADIN0 = 0x400f;
	static const reg_t PHY_RX_DLL_CTRL5_16 = 0x500f;

	static const uint64_t HT_BASE          = 0xfd00000000ULL;
	static const uint64_t HT_LIMIT         = 0x10000000000ULL;
	static const uint32_t MMIO_VGA_BASE    = 0xa0000;
	static const uint32_t MMIO_VGA_LIMIT   = 0xbffff;
	static const uint32_t MMIO32_LIMIT     = 0xe0000000;
	static const unsigned DRAM_RANGES      = 8;

	uint64_t dram_base, dram_size;
	uint64_t trace_base, trace_limit;
	static uint8_t family;
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

	static void ht_optimize_link(const ht_t nc, const ht_t neigh, const link_t link);
	void check(void);
	uint64_t read64(const reg_t reg) const;
	uint32_t read32(const reg_t reg) const;
	void write64_split(const reg_t reg, const uint64_t val) const;
	void write32(const reg_t reg, const uint32_t val) const;
	void set32(const reg_t reg, const uint32_t mask) const;
	void clear32(const reg_t reg, const uint32_t mask) const;
	void clearset32(const reg_t reg, const uint32_t setmask, const uint32_t clearmask) const;

	static void prepare(void);
#ifdef NOTNEEDED
	static void restore(void);
#endif
	void dram_scrub_disable(void);
	void dram_scrub_enable(void);
	void disable_syncflood(void);
	void disable_nbwdt(void);
	void init(void);
	Opteron(const sci_t _sci, const ht_t _ht, const bool _local);
	~Opteron(void);
	static ht_t ht_fabric_fixup(ht_t &neigh, link_t &link, const uint32_t vendev);
	static void ht_reconfig(const ht_t neigh, const link_t link, const ht_t nnodes);
	void dram_clear_start(void);
	void dram_clear_wait(void);
	void tracing_arm(void);
	void tracing_start(void);
	void tracing_stop(void);
	void tracing_disable(void);
	void discover(void);
};

extern Opteron *opteron;
