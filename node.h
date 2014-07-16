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

#include "library/base.h"
#include "opteron/opteron.h"
#include "opteron/sr56x0.h"
#include "numachip2/numachip.h"

class Node {
	bool local;
	const sci_t master;
public:
	sci_t sci;
	unsigned nopterons;
	unsigned cores;
	Opteron *opterons[7];
	SR56x0 *iohub;
	Numachip2 *numachip;
	uint64_t dram_base, dram_size, dram_end;

	void init(void);
	void check(void);
	Node(const sci_t _sci, const ht_t ht);
	Node(const sci_t _sci, const sci_t master);
};
