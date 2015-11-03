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

#include "numachip.h"
#include "lc.h"
#include "../platform/config.h"
#include "../bootloader.h"

void Numachip2::fabric_reset(void)
{
	if (options->debug.fabric)
		printf("<reset>");

	// ensure all links are in reset
	uint32_t mask = 0x3f;
	write32(HSS_PLLCTL, mask);
	uint32_t val = read32(HSS_PLLCTL);

	// bring configured links out of reset
	for (unsigned i = 0; i < 3; i++) {
		if (::config->size[i]) {
			assertf(((val >> (i * 2)) & 3) == 3, "Image doesn't support configured %c axis", 'X' + i);
			mask &= ~(3 << (i * 2));
		}
	}

	write32(HSS_PLLCTL, mask);
}

void Numachip2::fabric_check(void) const
{
	uint32_t val = read32(SIU_EVENTSTAT);

	if (val) {
		warning("SIU on %s has issues 0x%08x", config->hostname, val);

		if (val & (1ULL << 22)) printf(" RS master illegal packet\n");
		if (val & (1ULL << 21)) printf(" CC master illegal packet\n");
		if (val & (1ULL << 20)) printf(" MC master illegal packet\n");

		if (val & (1ULL << 18)) printf(" RS slave packet dropped\n");
		if (val & (1ULL << 17)) printf(" CC slave packet dropped\n");
		if (val & (1ULL << 16)) printf(" MC slave packet dropped\n");

		if (val & (1ULL << 14)) printf(" RS master list parity error\n");
		if (val & (1ULL << 13)) printf(" CC master list parity error\n");
		if (val & (1ULL << 12)) printf(" MC master list parity error\n");

		if (val & (1ULL << 10)) printf(" RS slave buffer overflow\n");
		if (val & (1ULL <<  9)) printf(" CC slave buffer overflow\n");
		if (val & (1ULL <<  8)) printf(" MC slave buffer overflow\n");

		if (val & (1ULL <<  6)) printf(" RS master buffer overflow\n");
		if (val & (1ULL <<  5)) printf(" CC master buffer overflow\n");
		if (val & (1ULL <<  4)) printf(" MC master buffer overflow\n");

		if (val & (1ULL <<  2)) printf(" RS slave CRC error\n");
		if (val & (1ULL <<  1)) printf(" CC slave CRC error\n");
		if (val & (1ULL <<  0)) printf(" MC slave CRC error\n");

		// clear
		write32(SIU_EVENTSTAT, val);
	}

	if (fabric_trained)
		foreach_lc(lc)
			(*lc)->check();
}

// goal: read all phy status successfully 5M times; if any error encountered, reset and restart
void Numachip2::fabric_train(void)
{
	int i = 0;
	bool errors = 1;

	printf("Fabric connected:");

	fabric_reset();

	do {
		// clear link errors
		foreach_lc(lc)
			(*lc)->clear();

		// wait until all links are up
		for (i = fabric_training_period; i > 0; i--) {
			bool allup = 1;

			foreach_lc(lc)
				allup &= (*lc)->is_up();

			// exit early if all up
			if (allup)
				break;
			cpu_relax();
		}

		// not all links are up; restart training if we have errors
		if (i == 0) {
			if (options->debug.fabric) {
				printf("<links not up:");
				foreach_lc(lc)
					printf(" %s(%"PRIx64")", (*lc)->is_up() ? "up" : "down", (*lc)->status());
				printf(">");
			}
			errors = 1;
			continue;
		}

		if (options->debug.fabric) {
			printf("<up:");
			foreach_lc(lc)
				printf(" %s(%"PRIx64")", (*lc)->is_up() ? "up" : "down", (*lc)->status());
			printf(">");
		}

		// clear link errors
		foreach_lc(lc)
			(*lc)->clear();

		// check for errors over period
		for (i = stability_period; i; i--) {
			errors = 0;

			foreach_lc(lc)
				errors |= (*lc)->status() > 0;

			// exit early if errors detected
			if (errors) {
				if (options->debug.fabric) {
					printf("<errors:");
					foreach_lc(lc)
						printf(" %016" PRIx64, (*lc)->status());
					printf(">");
				}
				break;
			}
		}

		// no errors found, exit
		if (i == 0)
			break;
	} while (errors);

	foreach_lc(lc)
		printf(" %u", (*lc)->index);
	printf("\n");

	fabric_trained = 1;
}

// on LC 'in', route packets to SCI 'dest' via LC 'out'
void Numachip2::xbar_route(const sci_t dst, const uint8_t out)
{
	const unsigned regoffset = dst >> 4;
	const unsigned bitoffset = dst & 0xf;

	for (unsigned bit = 0; bit < lc_bits; bit++) {
		uint16_t *ent = &xbar_routes[regoffset][bit];
		*ent &= ~(1 << bitoffset);
		*ent |= ((out >> bit) & 1) << bitoffset;
	}
}

void Numachip2::fabric_routing(void)
{
	// default route is to link 7 to trap unexpected behaviour
	memset(xbar_routes, 0xff, sizeof(xbar_routes));

	printf("Routing:");
	for (unsigned node = 0; node < ::config->nnodes; node++) {
		uint8_t out = lcs[0]->route1(config->position, ::config->nodes[node].position);
		printf(" %03x:%u->%03x", config->id, out, ::config->nodes[node].id);
		xbar_route(::config->nodes[node].id, out);

		foreach_lc(lc)
			(*lc)->add_route(::config->nodes[node].id, out);
	}
	printf("\n");

	// SIU routing table
	for (unsigned chunk = 0; chunk < lc_chunks; chunk++) {
		write32(SIU_XBAR_CHUNK, chunk);
		for (unsigned offset = 0; offset < lc_offsets; offset++)
			for (unsigned bit = 0; bit < lc_bits; bit++)
				write32(SIU_XBAR_TABLE + bit * SIU_XBAR_TABLE_SIZE + offset * 4, xbar_routes[(chunk<<4)+offset][bit]);
	}

	foreach_lc(lc)
		(*lc)->commit();
}

void Numachip2::fabric_init(void)
{
	uint32_t val = read32(HSS_PLLCTL);
	bool is_lc4 = (val >> 16) == 0x0704;

	if (local)
		printf("Fabric is %s\n", is_lc4 ? "LC4" : "LC5");

	for (unsigned index = 0; index < 6; index++) {
		if (!::config->size[index / 2])
			continue;

		if (is_lc4) {
			lcs[nlcs++] = new LC4(*this, index);
		} else {
			xassert((val >> 16) == 0x0705);
			lcs[nlcs++] = new LC5(*this, index);
		}
	}

	// enable SIU CRC if LC5
	if (!is_lc4) {
		val = read32(SIU_NODEID);
		write32(SIU_NODEID, val | (1<<31));
	}
}
