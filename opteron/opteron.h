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

class Opteron;

class MmioMap {
	Opteron &opteron;
	int ranges;

	struct reg setup(const int range);
	void print(const int range);
	int unused(void);
public:
	MmioMap(Opteron &_opteron);
	void remove(const int range);
	bool read(const int range, uint64_t *base, uint64_t *limit, ht_t *dest, link_t *link, bool *lock);
	void add(const uint64_t base, const uint64_t limit, const ht_t dest, const link_t link);
};

class DramMap {
	Opteron &opteron;
	static const int ranges = 8;

	int unused(void);
public:
	DramMap(Opteron &_opteron);
	void remove(const int range);
	bool read(const int range, uint64_t *base, uint64_t *limit, ht_t *dest);
	void print(const int range);
	void add(const int range, const uint64_t base, const uint64_t limit, const ht_t dest);
};

class Opteron {
	enum reset {Warm, Cold};

	static void reset(const enum reset mode, const int last);
protected:
	static const int F0_HT   = 0;
	static const int F1_MAPS = 1;
	static const int F2_DRAM = 2;
	static const int F3_MISC = 3;
	static const int F4_LINK = 4;
	static const int F5_EXTD = 5;
public:
	static int family;
	static uint32_t ioh_vendev;
	static uint32_t tsc_mhz;
	const sci_t sci;
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
	static ht_t ht_fabric_fixup(const uint32_t vendev, uint32_t *p_chip_rev);
};

#endif
