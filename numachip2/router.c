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

#include "router.h"
#include "../library/base.h"
#include "../platform/config.h"
#include <stdio.h>
#include <string.h>

static bool debug = 0;

void Router::find(const nodeid_t pos, const nodeid_t dst, const unsigned hops, const unsigned _usage, deps_t _deps, const xbarid_t last_xbarid)
{
	if (debug) {
		for (unsigned i = 0; i < hops; i++)
			printf(" ");

		printf("%03x hops=%u usage=%u", pos, hops, _usage);
	}

	// no room
	if (hops > nodes)
		return;

	if (hops > best.hops) {
		if (debug) printf(" overhops\n");
		return;
	}

	if (_usage > best.usage) {
		if (debug) printf(" overusage\n");
		return;
	}

	// if reached goal, update best
	if (pos == dst) {
		if ((hops * HOP_COST + _usage) < (best.hops * HOP_COST + best.usage)) {
			memcpy(best.route, route, hops * sizeof(route[0]));
			best.deps = _deps;
			best.route[hops] = 0; // route to local Numachip
			best.hops = hops;
			best.usage = _usage;
		}
		return;
	}

	for (xbarid_t xbarid = 1; xbarid < XBAR_PORTS; xbarid++) {
		dest_t next = ::config->nodes[pos].neigh[xbarid];
		if (next.nodeid == NODE_NONE)
			continue;

		// if route already defined, skip alternatives
		// FIXME: move out of loop
		if (routes[pos][last_xbarid][dst] != XBARID_NONE && xbarid != routes[pos][last_xbarid][dst]) {
			printf("!");
			continue;
		}

		// check if cyclic
		if (xbarid && last_xbarid) {
			if (_deps.table[next.nodeid][xbarid][pos][last_xbarid]) {
				if (debug) printf(" %03x:%u already depends on %03x:%u\n", next.nodeid, xbarid, pos, last_xbarid);
				continue;
			}

			_deps.table[next.nodeid][xbarid][pos][last_xbarid] = 1;
		}

		if (debug) printf(" xbarid=%u next=%03x:%u \n", xbarid, next.nodeid, next.xbarid);
		route[hops] = xbarid;
		find(next.nodeid, dst, hops + 1, _usage + usage[pos][xbarid], _deps, next.xbarid);
	}
}

void Router::update(const nodeid_t src, const nodeid_t dst)
{
	printf("usage=%u:", best.usage);

	xbarid_t xbarid = 0;
	unsigned hop = 0;
	nodeid_t pos = src;

	while (1) {
		printf(" %03x:%u", pos, xbarid);
		xbarid = routes[pos][xbarid][dst] = best.route[hop++];
		usage[pos][xbarid]++; // model congestion at link controller send buffer
		printf("->%u", xbarid);

		if (xbarid == 0)
			break;

		xassert(hop < nodes);
		dest_t next = ::config->nodes[pos].neigh[xbarid];
		pos = next.nodeid;
		xbarid = next.xbarid;
	};

	xassert(pos == dst);
	printf("\n");
}

Router::Router(const unsigned _nodes): nodes(_nodes)
{
	memset(routes, XBARID_NONE, sizeof(routes));
	memset(usage, 0, sizeof(usage));
	memset(&deps, 0, sizeof(deps));
}

void Router::run()
{
	// perform routing for all nodes; only write local tables
	for (nodeid_t src = 0; src < nodes; src++) {
		for (nodeid_t dst = 0; dst < nodes; dst++) {
			best.hops = ~0U;
			best.usage = ~0U;

			printf("%03x->%03x: ", src, dst);
			find(src, dst, 0, 0, deps, 0); // calculate optimal route
			update(src, dst); // increment path usage
			dist[src][dst] = best.hops; // used for ACPI SLIT
		}
	}

	dump();
}

void Router::dump() const
{
	printf("usage:");
	for (xbarid_t xbarid = 1; xbarid < XBAR_PORTS; xbarid++)
		printf(" %3x", xbarid);
	printf("\n");

	for (nodeid_t node = 0; node < nodes; node++) {
		printf("  %03x:", node);
		for (xbarid_t xbarid = 1; xbarid < XBAR_PORTS; xbarid++)
			printf(" %3x", usage[node][xbarid]);
		printf("\n");
	}
}
