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

void Node::init(void)
{
	dram_base = -1;

	for (ht_t n = 0; n < nopterons; n++) {
		Opteron *nb = opterons[n];
		if (nb->dram_base < dram_base)
			dram_base = nb->dram_base;

		dram_size += nb->dram_size;
		cores += nb->cores;
	}

	printf("SCI%03x (%lluGB, %u cores)\n", sci, dram_size >> 30, cores);
}

void Node::status(void)
{
	numachip->fabric_status();
	printf("SIU status 0x%08x\n", numachip->read32(Numachip2::SIU_EVENTSTAT));
}

// instantiated for remote nodes
Node::Node(const sci_t _sci, const ht_t ht): sci(_sci), nopterons(ht)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n] = new Opteron(sci, n);

	numachip = new Numachip2(sci, ht);
	init();
}

// instantiated for local nodes
Node::Node(void): sci(SCI_LOCAL)
{
	const ht_t nc = Opteron::ht_fabric_fixup(Numachip2::VENDEV_NC2);
	assertf(nc, "NumaChip2 not found");

	// set SCI ID later once mapping is setup
	numachip = new Numachip2(nc);
	nopterons = nc;

	// Opterons are on all HT IDs before Numachip
	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb] = new Opteron(nb);

	init();
}

void Node::set_sci(const sci_t _sci)
{
	sci = _sci;

	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb]->sci = sci;

	numachip->set_sci(sci);
}
