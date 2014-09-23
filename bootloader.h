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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdbool.h>

#include "platform/config.h"
#include "platform/os.h"
#include "platform/options.h"
#include "platform/e820.h"
#include "platform/acpi.h"
#include "library/access.h"
#include "opteron/opteron.h"
#include "numachip2/numachip.h"
#include "node.h"

#define foreach_node(x) for (Node **(x) = &nodes[0]; (x) < &nodes[config->nnodes]; (x)++)
#define foreach_nb(x, y) for (Opteron **(y) = &(*(x))->opterons[0]; (y) < &(*(x))->opterons[(*(x))->nopterons]; (y)++)

struct info {
	uint8_t partition; // 0 for observer
	uint16_t fabric_nodes : 12;
	uint16_t part_start : 12;
	uint16_t part_nodes : 12;
	uint8_t ver : 4;
	uint8_t neigh_ht : 3;
	uint8_t neigh_link : 2;
	uint8_t neigh_sublink : 1;
	bool symmetric;
	bool io;
} __attribute__((packed));
