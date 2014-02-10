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
#include "registers.h"
#include "../bootloader.h"

void Numachip2::selftest(void)
{
	if (options->fast)
		return;

	int errors = 0;

	printf("Selftest SIU-ATT");

	write32(SIU_ATT_INDEX, (1 << 28) | (1 << 31));
	for (unsigned i = 0; i < 4096; i++)
		write32(SIU_ATT_ENTRY, i);
		
	write32(SIU_ATT_INDEX, (1 << 28) | (1 << 31));
	for (unsigned i = 0; i < 4096; i++)
		if (read32(SIU_ATT_ENTRY) != i) errors++;

	write32(SIU_ATT_INDEX, (1 << 28) | (1 << 31));
	for (unsigned i = 0; i < 4096; i++)
		write32(SIU_ATT_ENTRY, 0x000);

	/* FIXME use Fatal() when implemented */
	if (errors)
		warning("%d errors", errors);

	printf(" SIU-XBar");
	for (unsigned i = 0; i < 16; i++) {
		write32(SIU_XBAR_CHUNK, i);
		for (unsigned j = 0; j < 0x40; j++) {
			write32(SIU_XBAR_ROUTE + 0x00 + j, i + j);
			write32(SIU_XBAR_ROUTE + 0x40 + j, i + j);
			write32(SIU_XBAR_ROUTE + 0x80 + j, i + j);
		}
	}

	for (unsigned i = 0; i < 16; i++) {
		write32(SIU_XBAR_CHUNK, i);
		for (unsigned j = 0; j < 0x40; j++) {
			if (read32(SIU_XBAR_ROUTE + 0x00 + j) != i + j) errors++;
			if (read32(SIU_XBAR_ROUTE + 0x40 + j) != i + j) errors++;
			if (read32(SIU_XBAR_ROUTE + 0x80 + j) != i + j) errors++;
		}
	}

	for (unsigned i = 0; i < 16; i++) {
		write32(SIU_XBAR_CHUNK, i);
		for (unsigned j = 0; j < 0x40; j++) {
			write32(SIU_XBAR_ROUTE + 0x00 + j, 0x000);
			write32(SIU_XBAR_ROUTE + 0x40 + j, 0x000);
			write32(SIU_XBAR_ROUTE + 0x80 + j, 0x000);
		}
	}

	/* FIXME use Fatal() when implemented */
	if (errors)
		warning("%d errors", errors);

	printf(" passed\n");
}
