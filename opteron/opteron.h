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

class Opteron {
	enum reset {Warm, Cold};
	uint8_t smi_state;

	void reset(const enum reset mode, const int last);
public:
	int family;
	uint32_t southbridge_id;
	uint32_t tsc_mhz;

	Opteron(void);
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
