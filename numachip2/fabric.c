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
		if (!(config->ringmask & (1 << lc)))
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
			if (options->debug.fabric)
				printf("<links not up:");
			for (LC5 **lc = &lcs[0]; lc < &lcs[nlcs]; lc++)
				printf(" %x", (*lc)->status());
			printf(">");
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

void Numachip2::fabric_init(void)
{
	for (int lc = 0; lc < 6; lc++) {
		if (!(config->ringmask & (1 << lc)))
			continue;

		lcs[nlcs++] = new LC5(*this, LC_BASE + lc * LC_SIZE);
	}
}
