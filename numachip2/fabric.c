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

	write32(HSS_PLLCTL, 0x3f);
	write32(HSS_PLLCTL, 0);
}

void Numachip2::fabric_check(void) const
{
	uint32_t val = read32(SIU_EVENTSTAT);

	if (val != 0)
		warning("SIU on %03x has issues 0x%08x", sci, val);

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
				errors |= ((*lc)->status() & 7) > 0;

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

	for (unsigned bit = 0; bit <= bit_lim; bit++) {
		uint16_t *ent = &xbar_routes[regoffset][bit];
		*ent &= ~(1 << bitoffset);
		*ent |= ((out >> bit) & 1) << bitoffset;
	}
}

void Numachip2::fabric_routing(void)
{
	// default route is to link 7 to trap unexpected behaviour
	memset(xbar_routes, 0xff, sizeof(xbar_routes));

	for (unsigned node = 0; node < config->nnodes; node++) {
		uint8_t out = lcs[0]->route1(sci, config->nodes[node].sci);
		printf("ROUTING: src=%03x, dst=%03x, port=%d\n", sci, config->nodes[node].sci, out);
		xbar_route(config->nodes[node].sci, out);

		foreach_lc(lc)
			(*lc)->add_route(config->nodes[node].sci, out);
	}

	printf("Writing routes (%d chunks)", chunk_lim + 1);

	// SIU routing table
	for (unsigned chunk = 0; chunk <= chunk_lim; chunk++) {
		write32(SIU_XBAR_CHUNK, chunk);
		for (unsigned offset = 0; offset <= offset_lim; offset++)
			for (unsigned bit = 0; bit <= bit_lim; bit++)
				write32(SIU_XBAR_TABLE + bit * SIU_XBAR_TABLE_SIZE + offset * 4, xbar_routes[(chunk<<4)+offset][bit]);
	}

	foreach_lc(lc)
		(*lc)->commit();

	printf("\n");
}

void Numachip2::fabric_init(void)
{
	uint32_t val = read32(HSS_PLLCTL);
	bool is_lc4 = (val >> 16) == 0x0704;

	printf("Fabric is %s\n", is_lc4 ? "LC4" : "LC5");
	for (unsigned index = 0; index < 6; index++) {
		if (!config->size[index / 2])
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
