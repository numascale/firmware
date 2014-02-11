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

#define FUNC0_HT    0
#define FUNC1_MAPS  1
#define FUNC2_DRAM  2
#define FUNC3_MISC  3
#define FUNC4_LINK  4
#define FUNC5_EXTD  5

struct reg {
	uint16_t base, limit, high;
};

class Opteron;

class MmioMap {
	const Opteron &opteron;
	const int ranges;

	struct reg setup(const int range);
	int unused(void);
public:
	MmioMap(const Opteron &_opteron);
	void remove(const ht_t range);
	bool read(const int range, uint64_t *base, uint64_t *limit, int *dest, int *link, bool *lock);
	void add(const uint64_t base, const uint64_t limit, const ht_t dest, const int link);
};

class DramMap {
	const Opteron &opteron;
	const int ranges;

	int unused(void);
public:
	DramMap(const Opteron &_opteron);
	void remove(const int range);
	bool read(const int range, uint64_t *base, uint64_t *limit, int *dest);
	void print(const int range);
	void add(const int range, const uint64_t base, const uint64_t limit, const ht_t dest);
};

class Opteron {
	enum reset {Warm, Cold};
	uint8_t smi_state;

	void reset(const enum reset mode, const int last);
public:
	ht_t nb_ht_min, nb_ht_max;
	int family;
	uint32_t ioh_vendev;
	static uint32_t tsc_mhz;
	const sci_t sci;

	Opteron(const sci_t _sci);
	~Opteron(void);
	void disable_smi(void);
	void enable_smi(void);
	void critical_enter(void);
	void critical_leave(void);
	void cht_print(int neigh, int link);
	uint32_t get_phy_register(int node, int link, int idx, int direct);
	void ht_optimize_link(int nc, int neigh, int link);
	int ht_fabric_fixup(uint32_t *p_chip_rev);
};

#endif
