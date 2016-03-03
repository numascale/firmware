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

//#include "../library/base.h"
#include <stdint.h>
#include <assert.h>

#define HOP_COST 10
#define LC_BITS  3
#define LCS      6
#define MAX_NODE 64
#define MAX_ROUTE (MAX_NODE / 2) // safe estimate
#define NODE_NONE ((lc_t)~0U)

typedef uint8_t nodeid_t;
typedef uint8_t lc_t;
struct deps {
	bool table[MAX_NODE][LCS][MAX_NODE][LCS];
};
typedef struct deps deps_t;

class Router {
	// fabric state
	unsigned nodes;

	// built-up state
	unsigned usage[MAX_NODE][LCS];
	//       < dest node   >< depends on  >
//	bool deps[MAX_NODE][LCS][MAX_NODE][LCS];
	deps_t deps;
	lc_t table[MAX_NODE][LCS][MAX_NODE];

	// per-route state
	lc_t route[MAX_ROUTE];
	struct {
		lc_t route[MAX_ROUTE];
		unsigned hops, usage;
//		bool deps[MAX_NODE][LCS][MAX_NODE][LCS];
		deps_t deps;
	} best;

	void find(const nodeid_t src, const nodeid_t dst, const unsigned hops, const unsigned usage, deps_t _deps, const lc_t last_lc);
	void update(const nodeid_t src, const nodeid_t dst);
public:
	nodeid_t neigh[MAX_NODE][LCS]; // defined by wiring

	Router(const unsigned _nodes);
	void run();
	void dump() const;
};
