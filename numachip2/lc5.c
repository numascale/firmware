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

#include <string.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "lc.h"
#include "../bootloader.h"
#include "../library/utils.h"

// returns 1 when link is up
bool LC5::is_up(void)
{
	link_up = numachip.read32(LINKSTAT + index * SIZE) >> 31;
	return link_up;
}

uint64_t LC5::status(void)
{
	uint64_t val = numachip.read32(LINKSTAT + index * SIZE) & ~(1<<31); // mask out link_up bit
	val |= (uint64_t)numachip.read32(EVENTSTAT + index * SIZE) << 32;
	return val;
}

void LC5::check(void)
{
	const uint64_t val = status();

	if (val) {
		warning("Fabric LC5 link %u on %s has issues 0x%016" PRIx64 ":", index, pr_node(numachip.config->id), val);

		if (val & (1ULL << 58)) printf(" Sequencer received an Ack/Nack that was not in the Retry Buffer\n");
		if (val & (1ULL << 56)) printf(" Sequence Retry Buffer overflow\n");
		if (val & (1ULL << 55)) printf(" Sequence retries has reached maximum\n");
		if (val & (1ULL << 54)) printf(" RS credit underflow\n");
		if (val & (1ULL << 53)) printf(" CC credit underflow\n");
		if (val & (1ULL << 52)) printf(" MC credit underflow\n");
		if (val & (1ULL << 51)) printf(" Cross point buffer overflow\n");
		if (val & (1ULL << 49)) printf(" Send Buffer Overflow\n");
		if (val & (1ULL << 48)) printf(" Double Bit ECC error detected on output of Send Buffer\n");
		if (val & (1ULL << 36)) printf(" Output Buffer arbitration error\n");
		if (val & (1ULL << 44)) printf(" Output Buffer RS FIFO overflow\n");
		if (val & (1ULL << 42)) printf(" Output Buffer CC FIFO overflow\n");
		if (val & (1ULL << 40)) printf(" Output Buffer MC FIFO overflow\n");
		if (val & (1ULL << 39)) printf(" Receive Retime Buffer overflow\n");
		if (val & (1ULL << 38)) printf(" Received an oversized packet\n");
		if (val & (1ULL << 37)) printf(" Receive Buffer received unsupported Output Port from Routing table\n");
		if (val & (1ULL << 35)) printf(" Receive Buffer bad parity detected on List Ram data\n");
		if (val & (1ULL << 34)) printf(" Receive Buffer received a packet that was larger than maximum supported size\n");
		if (val & (1ULL << 33)) printf(" Receive Buffer overflow\n");
		if (val & (1ULL << 32)) printf(" Receive Buffer received SOP in middle of a packet\n");
		if (val & (1ULL << 02)) printf(" phy Channel Hard Error\n");
		if (val & (1ULL << 01)) printf(" phy Channel Soft Error\n");
		if (val & (1ULL << 00)) printf(" phy Frame Error\n");

		// ratelimit
		lib::udelay(1000000);
	}

	clear();

	// link up/down reporting
	if ( link_up && !is_up()) {
		warning("Fabric link %u is down!", index);
		// ratelimit
		lib::udelay(1000000);
	}
	if (!link_up && is_up()) {
		warning("Fabric link %u is up!", index);
		// ratelimit
		lib::udelay(1000000);
	}
}

void LC5::clear(void)
{
	// clear link error bits and all event bits
	numachip.write32(LINKSTAT + index * SIZE, 7);
	numachip.write32(EVENTSTAT + index * SIZE, 0xffffffff);
}

// on LC, route packets to SCI 'dest' via LC 'out'
void LC5::add_route(const sci_t dst, const uint8_t out)
{
	const unsigned regoffset = dst >> 4;
	const unsigned bitoffset = dst & 0xf;

	for (unsigned bit = 0; bit < numachip.lc_bits; bit++) {
		uint16_t *ent = &lc_routes[regoffset][bit];
		*ent &= ~(1 << bitoffset);
		*ent |= ((out >> bit) & 1) << bitoffset;
	}
}

void LC5::commit(void)
{
	for (unsigned chunk = 0; chunk < numachip.lc_chunks; chunk++) {
		numachip.write32(ROUTE_CHUNK + index * SIZE, chunk);

		for (unsigned offset = 0; offset < numachip.lc_offsets; offset++)
			for (unsigned bit = 0; bit < numachip.lc_bits; bit++)
				numachip.write32(ROUTE_RAM + index * SIZE + bit * TABLE_SIZE + offset * 4, lc_routes[(chunk<<4)+offset][bit]);
	}
}

LC5::LC5(Numachip2& _numachip, const uint8_t _index): LC(_numachip, _index)
{
	// default route is to link 7 to trap unexpected behaviour
	memset(lc_routes, 0xff, sizeof(lc_routes));
}
