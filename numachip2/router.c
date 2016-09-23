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
#include <stdio.h>
#include <string.h>

static bool debug;
static unsigned routes_count, routes_total, routes_min = 10000, routes_max;

void Router::find(const nodeid_t pos, const nodeid_t dst, const unsigned hops, const unsigned _usage, deps_t _deps, const xbarid_t last_xbarid)
{
	if (debug) {
		for (unsigned i = 0; i < hops; i++)
			printf(" ");

		printf("%02u hops=%u usage=%u", pos, hops, _usage);
	}

	// no room
	if (hops > nnodes)
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
		dest_t next = neigh[pos][xbarid];
		if (next.nodeid == NODE_NONE)
			continue;

		// if route already defined, skip alternatives
		// FIXME: move out of loop
		if (routes[pos][last_xbarid][dst] != XBARID_NONE && xbarid != routes[pos][last_xbarid][dst]) {
//			printf("!");
			continue;
		}

		// check if cyclic
		if (xbarid && last_xbarid) {
			if (_deps.table[next.nodeid][xbarid][pos][last_xbarid]) {
				if (debug) printf(" %02u:%u already depends on %02u:%u\n", next.nodeid, xbarid, pos, last_xbarid);
				continue;
			}

			_deps.table[next.nodeid][xbarid][pos][last_xbarid] = 1;
		}

		if (debug) printf(" xbarid=%u next=%02u:%u \n", xbarid, next.nodeid, next.xbarid);
		route[hops] = xbarid;
		find(next.nodeid, dst, hops + 1, _usage + usage[pos][xbarid], _deps, next.xbarid);
	}
}

void Router::update(const nodeid_t src, const nodeid_t dst)
{
	printf("usage %3u:", best.usage);

	xbarid_t xbarid = 0;
	unsigned hop = 0;
	nodeid_t pos = src;

	while (1) {
		printf(" %02u:%u", pos, xbarid);
		xbarid = routes[pos][xbarid][dst] = best.route[hop++];
		usage[pos][xbarid]++; // model congestion at link controller send buffer
		printf("->%u", xbarid);

		if (xbarid == 0)
			break;

		xassert(hop < nnodes);
		dest_t next = neigh[pos][xbarid];
		pos = next.nodeid;
		xbarid = next.xbarid;
	};

	// ignore local route
	if (hop > 1) {
		if (hop < routes_min)
			routes_min = hop - 1;
		if (hop > routes_max)
			routes_max = hop - 1;

		routes_count++;
		routes_total += hop - 1;
	}

	xassert(pos == dst);
	printf("\n");
}

Router::Router()
{
	memset(routes, XBARID_NONE, sizeof(routes));
	memset(usage, 0, sizeof(usage));
	memset(&deps, 0, sizeof(deps));
	memset(neigh, XBARID_NONE, sizeof(neigh));
}

void Router::run(const unsigned _nnodes)
{
	nnodes = _nnodes;

	for (nodeid_t n = 0; n < nnodes; n++) {
		printf("node %2u:", n);

		for (xbarid_t x = 1; x <= 6; x++) {
			if (neigh[n][x].xbarid == XBARID_NONE)
				printf("    ");
			else
				printf(" %02u%c", neigh[n][x].nodeid, 'A' + neigh[n][x].xbarid - 1);
		}

		printf("\n");
	}

	printf("\n");

	// perform routing for all nodes; only write local tables
	for (nodeid_t src = 0; src < nnodes; src++) {
		for (nodeid_t dst = 0; dst < nnodes; dst++) {
			best.hops = ~0U;
			best.usage = ~0U;

			printf("%02u->%02u: ", src, dst);
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
		printf(" %3c", 'A' + xbarid-1);
	printf("\n");

	for (nodeid_t node = 0; node < nnodes; node++) {
		printf("   %02u:", node);
		for (xbarid_t xbarid = 1; xbarid < XBAR_PORTS; xbarid++)
			printf("  %3u", usage[node][xbarid]);
		printf("\n");
	}

	printf("hops: min %u, max %u, average %0.2f\n", routes_min, routes_max, routes_total / (double)routes_count);
}
