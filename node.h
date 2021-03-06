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

#ifndef NODE_H
#define NODE_H

#include "library/base.h"
#include "opteron/opteron.h"
#include "opteron/sr56x0.h"
#include "numachip2/numachip.h"
#include "platform/config.h"

#define foreach_node(x) for (Node *const *x = &nodes[0]; (x) < &nodes[nnodes]; (x)++)
#define foreach_nb(x, y) for (Opteron **y = &(*(x))->opterons[0]; (y) < &(*(x))->opterons[(*(x))->nopterons]; (y)++)

class Node {
	bool local;
	const sci_t master_sci;
public:
	ht_t neigh_ht;
	link_t neigh_link;
	uint32_t apics[56];
	uint8_t napics;
	unsigned nopterons;
	unsigned cores;
	Opteron *opterons[7];
	SR56x0 *iohub;
	Numachip2 *numachip;
	Config::node *config;
	uint64_t dram_base, dram_size, dram_end;
	uint64_t trace_base, trace_lim;
	uint64_t mmio32_base, mmio32_limit;
	uint64_t mmio64_base, mmio64_limit;
	uint16_t apic_offset;

	void init(void);
	bool check(void);
	void tracing_arm(void);
	void tracing_start(void);
	void tracing_stop(void);
	void trim_dram_maps(void);

	Node(Config::node *_config, const ht_t ht);
	Node(Config::node *_config, const sci_t _master_sci);
};

extern Node *local_node;
extern Node **nodes;
extern unsigned nnodes;

#endif
