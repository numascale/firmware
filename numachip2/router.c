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
#include <stdio.h>
#include <string.h>

void Router::find(const nodeid_t pos, const nodeid_t dst, const unsigned hops, const unsigned _usage, deps_t _deps, const lc_t last_lc)
{
	for (unsigned i = 0; i < hops; i++)
		printf(" ");

	printf("%03x hops=%u _usage=%u", pos, hops, _usage);

	// no room
	if (hops > nodes)
		return;

	if (hops > best.hops) {
		printf(" overhops\n");
		return;
	}

	if (_usage > best.usage) {
		printf(" overusage\n");
		return;
	}

	// if reached goal, update best
	if (pos == dst) {
		if ((hops * HOP_COST + _usage) < (best.hops * HOP_COST + best.usage)) {
//		if ((hops < best.hops) || (hops == best.hops && _usage < best.usage)) {
			memcpy(best.route, route, hops * sizeof(route[0]));
			best.deps = _deps;
//			memcpy(best.deps, _deps, sizeof(_deps));
			best.route[hops] = 0; // route to local Numachip
			best.hops = hops;
			best.usage = _usage;
		}
		return;
	}

	for (lc_t lc = 1; lc <= LCS; lc++) {
		nodeid_t next = neigh[pos][lc-1];
		if (next == NODE_NONE)
			continue;

		// check if cyclic
		if (lc && last_lc) {
			if (_deps.table[next][lc-1][pos][last_lc-1]) {
				printf(" %03x:%u already depends on %03x:%u\n", next, lc, pos, last_lc);
				continue;
			}

			_deps.table[next][lc-1][pos][last_lc-1] = 1;
		}

		printf(" lc=%u next=%03x \n", lc, next);
		route[hops] = lc;
		find(next, dst, hops + 1, _usage + usage[pos][lc-1], _deps, lc);
	}
}

void Router::update(const nodeid_t src, const nodeid_t dst)
{
	printf("update hops=%u usage=%u:\n", best.hops, best.usage);
	nodeid_t pos = src;

	for (unsigned hop = 0; best.route[hop]; hop++) {
		printf(" hop=%u", hop);
		assert(hop < nodes);
		lc_t lc = best.route[hop];
		printf(" lc=%u pos=%03x", lc, pos);
		usage[pos][lc-1]++;
		pos = neigh[pos][lc-1];
		printf("->%03x\n", pos);
	}

	assert(pos == dst);
}

Router::Router(const unsigned _nodes): nodes(_nodes)
{
	memset(neigh, ~0, sizeof(neigh));
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

			printf("%03x->%03x:\n", src, dst);
			find(src, dst, 0, 0, deps, 0); // calculate optimal route
			printf("\n");
			update(src, dst); // increment path usage

			dump();
			printf("\n\n");
		}
	}
}

void Router::dump() const
{
	printf("usage:");
	for (lc_t lc = 1; lc <= LCS; lc++)
		printf(" %3x", lc);
	printf("\n");

	for (nodeid_t node = 0; node < nodes; node++) {
		printf("  %03x:", node);
		for (lc_t lc = 1; lc <= LCS; lc++)
			printf(" %3x", usage[node][lc-1]);
		printf("\n");
	}
}
