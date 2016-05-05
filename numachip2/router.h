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

#include <stdint.h>
#include <assert.h>

#define HOP_COST 10
#define MAX_ROUTE (MAX_NODE / 2) // safe estimate

// NOTE: congestion is modelled at the link controller send buffer

typedef struct {
	bool table[MAX_NODE][XBAR_PORTS][MAX_NODE][XBAR_PORTS];
} deps_t;

class Router {
	unsigned nnodes;

	// built-up state
	unsigned usage[MAX_NODE][XBAR_PORTS];
	deps_t deps;

	// per-route state
	xbarid_t route[MAX_ROUTE];
	struct {
		xbarid_t route[MAX_ROUTE];
		unsigned hops, usage;
		deps_t deps;
	} best;

	void find(const nodeid_t src, const nodeid_t dst, const unsigned hops, const unsigned usage, deps_t _deps, const xbarid_t last_xbarid);
	void update(const nodeid_t src, const nodeid_t dst);
public:
	dest_t neigh[MAX_NODE][XBAR_PORTS]; 	// fabric state
	xbarid_t routes[MAX_NODE][XBAR_PORTS][MAX_NODE]; // built-up state
	uint8_t dist[MAX_NODE][MAX_NODE]; // used in ACPI SLIT table

	Router();
	void run(const unsigned _nnodes);
	void dump() const;
};

extern Router *router;
