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

#include "node.h"
#include "library/access.h"
#include "library/utils.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

void Node::init(void)
{
	if (SR56x0::probe(sci))
		iohub = new SR56x0(sci, local);
	else
		fatal("Unknown IO hub");

	dram_base = -1;

	for (ht_t n = 0; n < nopterons; n++) {
		Opteron *nb = opterons[n];
		if (nb->dram_base < dram_base)
			dram_base = nb->dram_base;

		dram_size += nb->dram_size;
		cores += nb->cores;
	}

	printf("SCI%03x (%"PRIu64"GB, %u cores)\n", sci, dram_size >> 30, cores);
}

void Node::check(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->check();

	numachip->fabric_check();
	numachip->dram_check();
}

void Node::tracing_arm(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_arm();
}

void Node::tracing_start(void)
{
	printf("Tracing started on:");
	for (ht_t n = 0; n < nopterons; n++)
		printf(" %03x#%u", opterons[n]->sci, opterons[n]->ht);
	printf("\n");
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_start();
}

void Node::tracing_stop(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_stop();
	printf("Tracing stopped on:");
	for (ht_t n = 0; n < nopterons; n++)
		printf(" %03x#%u", opterons[n]->sci, opterons[n]->ht);
	printf("\n");
}

// instantiated for remote nodes
Node::Node(const sci_t _sci, const ht_t ht): local(0), master(SCI_NONE), sci(_sci), nopterons(ht)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n] = new Opteron(sci, n, local);

	// FIXME set neigh_ht/link/sublink

	numachip = new Numachip2(sci, ht, local, SCI_NONE);

	init();
}

// instantiated for local nodes
Node::Node(const sci_t _sci, const sci_t _master): local(1), master(_master), sci(_sci)
{
	uint32_t val = lib::cht_read32(0, Opteron::HT_NODE_ID);
	nopterons = ((val >> 4) & 7) + 1;

	// Check if last node is NC2 or not
	val = lib::cht_read32(nopterons-1, Opteron::VENDEV);
	if (val == Numachip2::VENDEV_NC2)
		nopterons--;

	// Opterons are on all HT IDs before Numachip
	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb] = new Opteron(sci, nb, local);

#ifndef SIM
	const ht_t nc = Opteron::ht_fabric_fixup(neigh_ht, neigh_link, Numachip2::VENDEV_NC2);
#else
	const ht_t nc = 1;
#endif
	assertf(nc, "NumaChip2 not found");

	numachip = new Numachip2(sci, nc, local, master);
	xassert(nopterons == nc);

	init();
}
