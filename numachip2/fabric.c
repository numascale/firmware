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
		printf("<reset>\n");

	write32(Numachip2::HSS_PLLCTL, 3);
	write32(Numachip2::HSS_PLLCTL, 0);
}

void Numachip2::fabric_status(void)
{
	printf("Link status:");
	for (int lc = 0; lc < 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		printf(" %08x", lcs[lc]->status());
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
					printf(" %x", (*lc)->status());
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
						printf(" %08x", (*lc)->status());
					printf(">");
				}
				break;
			}
		}

		// no errors found, exit
		if (!i)
			break;
	} while (errors);

	printf(" ready\n");
}

uint8_t Numachip2::next(sci_t src, sci_t dst) const {
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

// add route on "bxbarid" towards "dest" over "link"
void Numachip2::update(const uint16_t dest, const uint8_t bxbarid, const uint8_t link)
{
	uint16_t offs = (dest >> 4) & 0xff;
	uint16_t mask = 1 << (dest & 0xf);

	routes_l[bxbarid][offs] |= ((link & 1) ? mask : 0);
	routes_m[bxbarid][offs] |= ((link & 2) ? mask : 0);
	routes_h[bxbarid][offs] |= ((link & 4) ? mask : 0);
}

void Numachip2::fabric_routing(void)
{
	printf("Initialising XBar routing");

#ifdef NOTNEEDED
	// ensure responses get back to SCC
	for (int lc = 1; lc <= 6; lc++)
		update(sci, lc, 0);
#endif
	for (Node **node = &nodes[1]; node < &nodes[config->nnodes]; node++) {
		uint8_t out = next(sci, (*node)->sci);
		update((*node)->sci, 0, out);

		for (int lc = 1; lc <= 6; lc++) {
			// skip unconfigured axes
			if (!config->size[(lc - 1) / 2])
				continue;

			// don't touch packets already on correct dim
			if ((lc - 1) / 2 != (out - 1) / 2)
				update((*node)->sci, lc, out);
		}
	}

	for (int lc = 0; lc <= 6; lc++) {
		// skip unconfigured axes
		if (lc > 0 && !config->size[(lc - 1) / 2])
			continue;

		for (uint16_t chunk = 0; chunk < 16; chunk++) {
			write32(SIU_XBAR_CHUNK, chunk);

			for (uint16_t offs = 0; offs < 16; offs++) {
				write32(SIU_XBAR_LOW  + (offs << 2), routes_l[lc][(chunk << 4) + offs]);
				write32(SIU_XBAR_MID  + (offs << 2), routes_m[lc][(chunk << 4) + offs]);
				write32(SIU_XBAR_HIGH + (offs << 2), routes_h[lc][(chunk << 4) + offs]);
			}
		}
	}

#ifdef TEST
	for (int chunk = 0; chunk < 16; chunk++) {
		write32(SIU_XBAR_CHUNK, chunk);
		const int port = 0; // self
		for (int entry = 0; entry < 0x40; entry++) {
			write32(SIU_XBAR_LOW,  (port >> 0) & 1);
			write32(SIU_XBAR_MID,  (port >> 1) & 1);
			write32(SIU_XBAR_HIGH, (port >> 2) & 1);
		}
	}

	switch (sci) {
	case 0x000:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 2);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 2);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 2);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	case 0x001:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 1);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 1);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 1);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	default:
		fatal("unexpected");
	}
#endif
	printf("\n");
}

void Numachip2::fabric_init(void)
{
	for (int lc = 0; lc < 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		lcs[nlcs++] = new LC5(*this, LC_BASE + lc * LC_SIZE);
	}
}
