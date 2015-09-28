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
#include "platform/options.h"

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

	if (local)
		printf("%03x", sci);
	printf(" (%"PRIu64"GB, %u cores)\n", dram_size >> 30, cores);
}

void Node::check(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->check();

	if (numachip)
		numachip->check();
}

void Node::tracing_arm(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_arm();
}

void Node::tracing_start(void)
{
	asm volatile("wbinvd");
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_start();
}

void Node::tracing_stop(void)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n]->tracing_stop();
}

void Node::trim_dram_maps(void)
{
	// trim nodes if over supported or requested memory config
	int64_t over = max((int64_t)(dram_size - options->memlimit), (int64_t)(dram_size & ((1ULL << Numachip2::SIU_ATT_SHIFT) - 1)));
	if (over <= 0)
		return;

	printf("Trimming %03x maps by %"PRIu64"MB\n", sci, over >> 20);

	while (over > 0) {
		uint64_t max = 0;

		// find largest HT node
		for (ht_t n = 0; n < nopterons; n++)
			if (opterons[n]->dram_size > max)
				max = opterons[n]->dram_size;

		// reduce largest HT node by 16MB
		for (Opteron *const *nb = &opterons[0]; nb < &opterons[nopterons]; nb++) {
			if ((*nb)->dram_size == max) {
				(*nb)->dram_size -= 1ULL << 24;
				dram_size -= 1ULL << 24;
				over -= 1ULL << 24;
				break;
			}
		}
	}
}

// instantiated for remote nodes
Node::Node(Config::node *_config, const ht_t ht): local(0), master(SCI_LOCAL), sci(_config->sci), nopterons(ht), config(_config)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n] = new Opteron(sci, n, local);

	// FIXME set neigh_ht/link/sublink

	numachip = new Numachip2(sci, ht, local, SCI_LOCAL);

	init();
}

// instantiated for local nodes
Node::Node(Config::node *_config, const sci_t _master): local(1), master(_master), sci(_config->sci), config(_config)
{
	uint32_t val = lib::cht_read32(0, Opteron::HT_NODE_ID);
	nopterons = ((val >> 4) & 7) + 1;

	// check if last node is NC2 or not
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

	check(); // check for Protocol Error MCEs
	numachip = new Numachip2(sci, nc, local, master);
	xassert(nopterons == nc);

	// add MMIO range for local CSR space
	for (Opteron *const *nb = &opterons[0]; nb < &opterons[nopterons]; nb++) {
		uint64_t base, limit;
		ht_t dest;
		link_t link;
		bool lock;
		unsigned range;

		// modify overlapping entries
		for (range = 0; range < (*nb)->mmiomap->ranges; range++) {
			if ((*nb)->mmiomap->read(range, &base, &limit, &dest, &link, &lock)) {
				if ((base <= Numachip2::LOC_BASE) && (limit >= Numachip2::LOC_LIM)) {
					(*nb)->mmiomap->add(range, Numachip2::LOC_LIM + 1, limit, dest, link);
					break;
				}
			}
		}

		// find highest available MMIO map entry
		for (range = 7; range > 0; range--)
			if (!(*nb)->mmiomap->read(range, &base, &limit, &dest, &link, &lock))
				break;

		xassert(range > 0);

		// add local mapping
		(*nb)->mmiomap->add(range, Numachip2::LOC_BASE, Numachip2::LOC_LIM, numachip->ht, 0);
	}

	init();
}
