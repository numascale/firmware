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

#include "numachip.h"
#include "lc5.h"
#include "../platform/config.h"
#include "../bootloader.h"

void Numachip2::fabric_reset(void)
{
	if (options->debug.fabric)
		printf("<reset>");

	write32(Numachip2::HSS_PLLCTL, 0x3f);
	write32(Numachip2::HSS_PLLCTL, 0);
}

void Numachip2::fabric_status(void)
{
	printf("Link status:");
	for (int lc = 0; lc < 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		printf(" %016llx", lcs[lc]->status());
	}
	printf("\n");
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

			for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
				allup &= (*lc)->status() && (1 << 31);

			// exit early if all up
			if (allup)
				break;
		}

		// not all links are up; restart training
		if (i == 0) {
			if (options->debug.fabric) {
				printf("<links not up:");
				for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
					printf(" %llx", (*lc)->status());
				printf(">");
			}
			continue;
		}

		// clear link errors
		for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
			(*lc)->clear();

		// check for errors over period
		for (i = stability_period; i; i--) {
			errors = 0;

			for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
				errors |= ((*lc)->status() & 7) > 0;

			// exit early if all up
			if (errors) {
				if (options->debug.fabric) {
					printf("<errors:");
					for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
						printf(" %016llx", (*lc)->status());
					printf(">");
				}
				break;
			}
		}

		// no errors found, exit
		if (!i)
			break;
	} while (errors);

	for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
		printf(" %s", (*lc)->name);
	printf("\n");
}

uint8_t Numachip2::next(sci_t src, sci_t dst) const
{
	if (src == dst)
		return 0;

	uint8_t dim = 0;

	while ((src ^ dst) & ~0xf) {
		dim++;
		src >>= 4;
		dst >>= 4;
	}

	int out = dim * 2 + 1;
	out += ((dst & 0xf) + ((dst >> 4) & 0xf) + ((dst >> 8) & 0xf) +
		(src & 0xf) + ((src >> 4) & 0xf) + ((src >> 8) & 0xf)) & 1; // load balance
	return out;
}

// on LC5 'in', route packets to SCI 'dest' via LC 'out'
void Numachip2::route(const uint8_t in, const uint16_t dest, const uint8_t out)
{
	// dest[3:0] corresponds to bit offset
	// dest[7:4] corresponds to register offset
	// dest[11:8] corresponds to chunk offset

	const int regbase = in ? (LC_XBAR + (in - 1) * LC_SIZE) : SIU_XBAR;
	const int regoffset = ((dest >> 4) & 0xf) << 2;
	const int chunk = dest >> 8;
	const int bitoffset = dest & 0xf;

	write32(regbase + XBAR_CHUNK, chunk);

	for (int bit = 0; bit < 3; bit++) {
		uint32_t val = read32(regbase + regoffset + bit * XBAR_TABLE_SIZE);
		val &= ~(1 << bitoffset);
		val |= ((out >> bit) & 1) << bitoffset;
		write32(regbase + regoffset + bit * XBAR_TABLE_SIZE, val);
	}
}

void Numachip2::routing_dump(void)
{
	printf("Routing tables:\n");

	for (int in = 0; in <= 6; in++) {
		if (!config->size[in / 2])
			continue;

		const int regbase = in ? (LC_XBAR + (in - 1) * LC_SIZE) : SIU_XBAR;

		for (int dest = 0; dest < config->nnodes; dest++) {
			const int regoffset = ((dest >> 4) & 0xf) << 2;
			const int chunk = dest >> 8;
			const int bitoffset = dest & 0xf;
			uint8_t out = 0;

			write32(regbase + XBAR_CHUNK, chunk);

			for (int bit = 0; bit < 3; bit++)
				out |= ((read32(regbase + regoffset + bit * XBAR_TABLE_SIZE) >> bitoffset) & 1) << bit;

			if (out != 7)
				printf("- on LC%d, SCI%03x via LC%d\n", in, dest, out);
		}
	}
}

void Numachip2::fabric_routing(void)
{
	printf("Initialising XBar routing:\n");

	switch(sci) {
	case 0x000:
		route(0, 0x000, 0);
		route(0, 0x002, 1);
		route(0, 0x001, 2);

		route(1, 0x000, 0);
		route(1, 0x001, 2);
		route(1, 0x002, 2);

		route(2, 0x000, 0);
		route(2, 0x001, 1);
		route(2, 0x002, 1);
		break;
	case 0x001:
		route(0, 0x001, 0);
		route(0, 0x000, 1);
		route(0, 0x002, 2);

		route(1, 0x001, 0);
		route(1, 0x000, 2);
		route(1, 0x002, 2);

		route(2, 0x001, 0);
		route(2, 0x000, 1);
		route(2, 0x002, 1);
		break;
	case 0x002:
		route(0, 0x002, 0);
		route(0, 0x001, 1);
		route(0, 0x000, 2);

		route(1, 0x002, 0);
		route(1, 0x000, 2);
		route(1, 0x001, 2);

		route(2, 0x002, 0);
		route(2, 0x000, 1);
		route(2, 0x001, 1);
		break;
	default:
		error("unexpected");
	}

#ifdef GENERATE
	for (int node = 0; node < config->nnodes; node++) {
		uint8_t out = next(sci, config->nodes[node].sci);
		printf("- to SCI%03x via LC%d\n", config->nodes[node].sci, out);

		for (int lc = 0; lc <= 6; lc++)
			if (!config->size[lc / 2])
				continue;

			route(lc, config->nodes[node].sci, out);
	}
#endif
	routing_dump();
}

void Numachip2::fabric_init(void)
{
	const char *names[6] = {"XA", "XB", "YA", "YB", "ZA", "ZB"};

	for (int lc = 0; lc < 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		lcs[nlcs++] = new LC5(*this, LC_XBAR + lc * LC_SIZE, names[lc]);
	}
}
