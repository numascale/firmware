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

#ifndef __OPTERON_H
#define __OPTERON_H

#include "../library/base.h"

struct reg {
	uint16_t base, limit, high;
};

class Opteron {
	class MmioMap {
		const Opteron &opteron;
		int ranges;

		struct reg setup(const int range);
		int unused(void);
	public:
		void print(const int range);
		MmioMap(Opteron &_opteron);
		void remove(int range);
		bool read(int range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock);
		void add(int range, uint64_t base, uint64_t limit, const ht_t dest, const link_t link);
		void add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link);
	};

	class DramMap {
		const Opteron &opteron;
		static const int ranges = 8;

		int unused(void);
	public:
		DramMap(Opteron &_opteron);
		void remove(const int range);
		bool read(const int range, uint64_t *base, uint64_t *limit, ht_t *dest);
		void print(const int range);
		void add(const int range, const uint64_t base, const uint64_t limit, const ht_t dest);
	};

	enum reset {Warm, Cold};

	static void reset(const enum reset mode, const int last);
public:
	static const msr_t TOPMEM              = 0xc001001a;
	static const msr_t TOPMEM2             = 0xc001001d;
	static const msr_t APIC_BAR            = 0x0000001b;
	static const msr_t FS_BASE             = 0xc0000100;
	static const msr_t SYSCFG              = 0xc0010010;
	static const msr_t HWCR                = 0xc0010015;
	static const msr_t NB_CFG              = 0xc001001f;
	static const msr_t INT_HALT            = 0xc0010055;
	static const msr_t MCFG_BASE           = 0xc0010058;
	static const msr_t COFVID_STAT         = 0xc0010071;
	static const msr_t SMM_BASE            = 0xc0010111;
	static const msr_t OSVW_ID_LEN         = 0xc0010140;
	static const msr_t OSVW_STATUS         = 0xc0010141;
	static const msr_t NODE_ID             = 0xc001100c;
	static const msr_t LSCFG               = 0xc0011020;
	static const msr_t CU_CFG2             = 0xc001102a;
	static const msr_t CU_CFG3             = 0xc001102b;
	static const msr_t MTRR_PHYS_BASE0     = 0x00000200;
	static const msr_t MTRR_PHYS_BASE1     = 0x00000202;
	static const msr_t MTRR_PHYS_BASE2     = 0x00000204;
	static const msr_t MTRR_PHYS_BASE3     = 0x00000206;
	static const msr_t MTRR_PHYS_BASE4     = 0x00000208;
	static const msr_t MTRR_PHYS_BASE5     = 0x0000020a;
	static const msr_t MTRR_PHYS_BASE6     = 0x0000020c;
	static const msr_t MTRR_PHYS_BASE7     = 0x0000020e;
	static const msr_t MTRR_PHYS_MASK0     = 0x00000201;
	static const msr_t MTRR_PHYS_MASK1     = 0x00000203;
	static const msr_t MTRR_PHYS_MASK2     = 0x00000205;
	static const msr_t MTRR_PHYS_MASK3     = 0x00000207;
	static const msr_t MTRR_PHYS_MASK4     = 0x00000209;
	static const msr_t MTRR_PHYS_MASK5     = 0x0000020b;
	static const msr_t MTRR_PHYS_MASK6     = 0x0000020d;
	static const msr_t MTRR_PHYS_MASK7     = 0x0000020f;
	static const msr_t PERF_CTL0           = 0xc0010001;
	static const msr_t PERF_CTR0           = 0xc0010005;
	static const msr_t IORR_PHYS_BASE0     = 0xc0010016;
	static const msr_t IORR_PHYS_BASE1     = 0xc0010018;
	static const msr_t IORR_PHYS_MASK0     = 0xc0010017;
	static const msr_t IORR_PHYS_MASK1     = 0xc0010019;
	static const msr_t MTRR_FIX64K_00000   = 0x00000250;
	static const msr_t MTRR_FIX16K_80000   = 0x00000258;
	static const msr_t MTRR_FIX16K_A0000   = 0x00000259;
	static const msr_t MTRR_FIX4K_C0000    = 0x00000268;
	static const msr_t MTRR_FIX4K_C8000    = 0x00000269;
	static const msr_t MTRR_FIX4K_D0000    = 0x0000026a;
	static const msr_t MTRR_FIX4K_D8000    = 0x0000026b;
	static const msr_t MTRR_FIX4K_E0000    = 0x0000026c;
	static const msr_t MTRR_FIX4K_E8000    = 0x0000026d;
	static const msr_t MTRR_FIX4K_F0000    = 0x0000026e;
	static const msr_t MTRR_FIX4K_F8000    = 0x0000026f;
	static const msr_t MTRR_DEFAULT        = 0x000002ff;

	static const uint32_t VENDEV_SR5690	   = 0x5a101002;
	static const uint32_t VENDEV_SR5670	   = 0x5a121002;
	static const uint32_t VENDEV_SR5650	   = 0x5a131002;
	static const uint32_t VENDEV_MCP55	   = 0x036910de;
	static const uint32_t VENDEV_OPTERON   = 0x12001022;

	static const reg_t VENDEV              = 0x0000;
	static const reg_t ROUTING             = 0x0040;
	static const reg_t HT_NODE_ID          = 0x0060;
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
	static const reg_t MMIO_MAP_HIGH       = 0x1180;
	static const reg_t EXTMMIO_MAP_CTRL    = 0x1110;
	static const reg_t EXTMMIO_MAP_DATA    = 0x1114;
	static const reg_t DRAM_BASE           = 0x1120;
	static const reg_t DRAM_LIMIT          = 0x1124;
	static const reg_t DRAM_MAP_BASE_HIGH  = 0x1140;
	static const reg_t DRAM_MAP_LIMIT_HIGH = 0x1144;
	static const reg_t MCA_NB_CONF         = 0x3044;
	static const reg_t NB_CONF_1H          = 0x308c;
	static const reg_t CLK_CTRL_0          = 0x30d4;
	static const reg_t NB_CPUID            = 0x30fc;
	static const reg_t NB_PSTATE_0         = 0x5160;

	uint64_t dram_base, dram_size;
	static int family;
	static uint32_t ioh_vendev;
	static uint32_t tsc_mhz;
	sci_t sci;
	const ht_t ht;
	MmioMap mmiomap;
	DramMap drammap;
	friend class MmioMap;
	friend class DramMap;

	static void prepare(void);
	Opteron(const sci_t _sci, const ht_t _ht);
	~Opteron(void);
	static void disable_smi(void);
	static void enable_smi(void);
	static void critical_enter(void);
	static void critical_leave(void);
	static void cht_print(int neigh, int link);
	static uint32_t get_phy_register(const ht_t ht, const link_t link, const int idx, const bool direct);
	static void ht_optimize_link(int nc, int neigh, int link);
	static ht_t ht_fabric_fixup(const uint32_t vendev);

	uint32_t read32(const reg_t reg) const;
	void write32(const reg_t reg, const uint32_t val) const;
	void set32(const reg_t reg, const uint32_t mask) const;
	void clear32(const reg_t reg, const uint32_t mask) const;
};
#endif
