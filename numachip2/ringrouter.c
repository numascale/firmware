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

#include "routing.h"
#include "../platform/config.h"
#include <string.h>

/*uint16_t RingRouter::lcbase(const uint8_t lc) const
{
	xassert(lc < 7);
	return lc ? (Numachip2::LC_XBAR + (lc - 1) * Numachip2::LC_SIZE) : Numachip2::SIU_XBAR;
}*/

void RingRouter::find(const sci_t src, const sci_t dst, const unsigned cost, const unsigned offset)
{
	xassert(offset < (sizes[0]-1U + sizes[1]-1U + sizes[2]-1U));

	// if reached goal, update best
	if (src == dst && cost < bestcost) {
		route[offset] = 0;
		memcpy(bestroute, route, offset * sizeof(route[0]));
		return;
	}

	// terminate more expensive paths early
	if (cost >= bestcost)
		return;

	for (uint8_t lc = 1; lc <= 6; lc++) {
		const sci_t next = neigh(src, lc);
		route[offset] = lc;
		find(next, dst, cost + usage[src] + 1, offset + 1);
	}
}

sci_t RingRouter::neigh(const sci_t src, const uint8_t lc) const
{
	return src;
}

void RingRouter::update(const sci_t src, const sci_t dst)
{
	sci_t sci = src;

	for (unsigned offset = 0; bestroute[offset]; offset++) {
		xassert(offset < 45);
		uint8_t lc = bestroute[offset];
		usage[sci]++;
		sci = neigh(sci, lc);
	}

	xassert(sci == dst);
}

RingRouter::RingRouter(void) //uint8_t _sizes[])
{
/*	for (unsigned i = 0; i < 3; i++) {
		xassert(_sizes[i] >= 1);
		xassert(_sizes[i] <= 16);
		sizes[i] = _sizes[i];
	}*/

	// default routes to link 7 (non existent)
//	memset(shadow, 0xff, sizeof(shadow));
	printf("hello\n");

	// perform routing for all nodes; only write local tables
	for (sci_t snode = 0; snode < config->nnodes; snode++) {
		for (sci_t dnode = 0; dnode < config->nnodes; dnode++) {
			if (snode == dnode)
				continue;

			bestcost = ~0;
			find(snode, dnode, bestcost, 0); // calculate optimal route
			update(snode, dnode); // increment path usage
//			write(); // write route into shadow tables
		 }
	}

	// write local routing tables
/*	for (uint8_t lc = 0; lc < 7; lc++)
		 for (uint16_t offset = 0; offset < Numachip2::LC_SIZE; offset += 4)
			numachip.write32(lcbase(lc) + offset, shadow[lc][offset]); */
}
