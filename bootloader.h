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

#include <inttypes.h>
#include <stdbool.h>

#include "platform/config.h"
#include "platform/syslinux.h"
#include "platform/options.h"
#include "platform/e820.h"
#include "library/access.h"
#include "opteron/opteron.h"
#include "numachip2/numachip.h"

#define foreach_node(x) for (Node **(x) = &nodes[0]; (x) < &nodes[config->nnodes]; (x)++)
#define foreach_nb(x, y) for (Opteron **(y) = &(*(x))->opterons[0]; (y) < &(*(x))->opterons[(*(x))->nopterons]; (y)++)

class Node {
public:
	sci_t sci;
	unsigned nopterons;
	unsigned cores;
	Opteron *opterons[7];
	Numachip2 *numachip;
	uint64_t dram_base, dram_size, dram_end;

	void init(void);
	Node(void);
	Node(const sci_t _sci, const ht_t ht);
	void set_sci(const sci_t _sci);
};

/* Global constants found in initialization */
extern Syslinux *syslinux;
extern Options *options;
extern Config *config;
extern Opteron *opteron;
extern Numachip2 *numachip;
extern E820 *e820;
extern Node *local_node;
extern Node **nodes;
