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
#include "../bootloader.h"

// FIXME: use 0xffff when implemented
#define PATTERN 0x00ff

void Numachip2::selftest(void)
{
	printf("Selftest HT");

	for (uint32_t i = 0; i < 2000000; i++) {
		write32(SCRATCH_0, i);
		assertf(read32(SCRATCH_0) == i, "Failed");
	}
	write32(SCRATCH_0, 0);

	printf(" SIU-ATT");

	write32(SIU_ATT_INDEX, (1 << 28) | (1 << 31));
	for (unsigned i = 0; i < 4096; i++)
		write32(SIU_ATT_ENTRY, PATTERN);

	write32(SIU_ATT_INDEX, (1 << 28) | (1 << 31));
	for (unsigned i = 0; i < 4096; i++)
		assertf(read32(SIU_ATT_ENTRY) == PATTERN, "Readback at %u gave 0x%x instead of 0x%x", i, read32(SIU_ATT_ENTRY), PATTERN);

	for (int lc = 0; lc <= 6; lc++) {
		if (!config->size[lc / 2])
			continue;

		const int regbase = lc ? (LC_XBAR + (lc - 1) * LC_SIZE) : SIU_XBAR;

		printf(" LC%d", lc);

		for (unsigned i = 0; i < 16; i++) {
			write32(regbase + XBAR_CHUNK, i);
			for (unsigned j = 0; j < 0x40; j += 4) {
				write32(regbase + XBAR_TABLE_SIZE * 0 + j, PATTERN);
				write32(regbase + XBAR_TABLE_SIZE * 1 + j, PATTERN);
				write32(regbase + XBAR_TABLE_SIZE * 2 + j, PATTERN);
			}
		}

		for (unsigned i = 0; i < 16; i++) {
			write32(regbase + XBAR_CHUNK, i);
			for (unsigned j = 0; j < 0x40; j += 4) {
				assertf(read32(regbase + XBAR_TABLE_SIZE * 0 + j) == PATTERN, "Readback at low chunk %u, offset %u gave 0x%x instead of 0x%x",
					i, j, read32(regbase + XBAR_TABLE_SIZE * 0 + j), PATTERN);
				assertf(read32(regbase + XBAR_TABLE_SIZE * 1 + j) == PATTERN, "Readback at mid chunk %u, offset %u gave 0x%x instead of 0x%x",
					i, j, read32(regbase + XBAR_TABLE_SIZE * 1 + j), PATTERN);
				assertf(read32(regbase + XBAR_TABLE_SIZE * 2 + j) == PATTERN, "Readback at high chunk %u, offset %u gave 0x%x instead of 0x%x",
					i, j, read32(regbase + XBAR_TABLE_SIZE * 2 + j), PATTERN);
			}
		}
	}

	printf(" passed\n");
}
