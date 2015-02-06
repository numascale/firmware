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

void Numachip2::fabric_check(void)
{
	uint32_t val = read32(SIU_EVENTSTAT);

	if (val != 0)
		warning("SIU on %03x has issues 0x%08x", sci, val);

	for (int lc = 0; lc < 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		lcs[lc]->check();
	}
}

// goal: read all phy status successfully 5M times; if any error encountered, reset and restart
void Numachip2::fabric_train(void)
{
	int i;
	printf("Fabric connected:");

	bool errors;

	do {
		fabric_reset();

		// wait until all links are up
		for (i = training_period; i > 0; i--) {
			bool allup = 1;

			for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
				allup &= (*lc)->is_up();

			// exit early if all up
			if (allup)
				break;
			cpu_relax();
		}

		// not all links are up; restart training
		if (i == 0) {
			if (options->debug.fabric) {
				printf("<links not up:");
				for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
					printf(" %"PRIx64, (*lc)->status());
				printf(">");
			}
			continue;
		}

		// clear link errors
		for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
			(*lc)->clear();

		// check for errors over period
		for (i = stability_period; i; i--) {
			errors = 0;

			for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
				errors |= ((*lc)->status() & 7) > 0;

			// exit early if all up
			if (errors) {
				if (options->debug.fabric) {
					printf("<errors:");
					for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
						printf(" %016" PRIx64, (*lc)->status());
					printf(">");
				}
				break;
			}
		}

		// no errors found, exit
		if (!i)
			break;
	} while (errors);

	for (LC **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
		printf(" %u", (*lc)->index);
	printf("\n");
}

uint8_t Numachip2::next(sci_t src, sci_t dst) const
{
	if (src == dst)
		return 0;

	uint8_t dim = 0;
	sci_t src2 = src;
	sci_t dst2 = dst;

	while ((src2 ^ dst2) & ~0xf) {
		dim++;
		src2 >>= 4;
		dst2 >>= 4;
	}
	src2 &= 0xf;
	dst2 &= 0xf;

	xassert(dim < 3);
	int out = dim * 2 + 1;
#if 0
	// Simple load balance
	out +=  ((dst & 0xf) + ((dst >> 4) & 0xf) + ((dst >> 8) & 0xf) +
		 (src & 0xf) + ((src >> 4) & 0xf) + ((src >> 8) & 0xf)) & 1;
#endif
#if 0
	// Shortest path routing
	int len = config->size[dim];
	int forward = ((len - src2) + dst2) % len;
	int backward = ((src2 + (len - dst2)) + len) % len;

	out += (forward == backward) ? (src2 & 1) :
	       (backward < forward) ? 1 : 0;
#endif
	// 2QOS routing only on LC5 (otherwise we have credit loops)
	out += (dst2 < src2) ? 1 : 0;
	return out;
}

// on LC 'in', route packets to SCI 'dest' via LC 'out'
void Numachip2::route(const uint8_t in, const uint16_t dest, const uint8_t out)
{
	const unsigned regoffset = dest >> 4;
	const unsigned bitoffset = dest & 0xf;

	for (unsigned bit = 0; bit < 3; bit++) {
		uint16_t *ent = &routes[in][regoffset][bit];
		*ent &= ~(1 << bitoffset);
		*ent |= ((out >> bit) & 1) << bitoffset;
	}
}

void Numachip2::routing_dump(void)
{
	printf("Routing tables:\n");

	for (unsigned in = 0; in <= 6; in++) {
		if (!config->size[(in - 1) / 2])
			continue;

		for (unsigned dest = 0; dest < 4096; dest++) {
			const unsigned regoffset = dest >> 4;
			const unsigned bitoffset = dest & 0xf;
			uint8_t out = 0;

			for (unsigned bit = 0; bit < 3; bit++)
				out |= ((routes[in][regoffset][bit] >> bitoffset) & 1) << bit;

			if (out != 7)
				printf("- on LC%u, SCI%03x via LC%d\n", in, dest, out);
		}
	}
}

void Numachip2::routing_write(void)
{
	const uint8_t chunk_lim = 7;
	const uint8_t offset_lim = 7;
	const uint8_t bit_lim = 2;
	unsigned lc = 0;

	printf("Writing routes (%d chunks)", chunk_lim + 1);

	for (unsigned xbarid = 0; xbarid <= 6; xbarid++) {
		if (xbarid && !config->size[(xbarid - 1) / 2])
			continue;

		const uint16_t tablebase = xbarid ? lcs[lc]->tableaddr : SIU_XBAR;

		for (unsigned chunk = 0; chunk <= chunk_lim; chunk++) {
			write32(xbarid ? lcs[lc]->chunkaddr : XBAR_CHUNK, chunk);
			for (unsigned offset = 0; offset <= offset_lim; offset++)
				for (unsigned bit = 0; bit <= bit_lim; bit++)
					write32(tablebase + bit * XBAR_TABLE_SIZE + offset * 4, routes[xbarid][(chunk<<4)+offset][bit]);
		}

		if (xbarid) lc++;
	}

	printf("\n");
}

void Numachip2::fabric_routing(void)
{
	// default route is to link 7 to trap unexpected behaviour
	memset(routes, 0xff, sizeof(routes));

	for (unsigned node = 0; node < config->nnodes; node++) {
		uint8_t out = next(sci, config->nodes[node].sci);
		printf("- to SCI%03x via port%d\n", config->nodes[node].sci, out);

		for (unsigned xbarid = 0; xbarid <= 6; xbarid++) {
			if (xbarid && !config->size[(xbarid - 1) / 2])
				continue;

			route(xbarid, config->nodes[node].sci, out);
		}
	}

	routing_dump();
	routing_write();
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
}
