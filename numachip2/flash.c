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
#include "../library/base.h"

namespace
{
	void progress(const uint32_t a, const uint32_t b)
	{
		static uint8_t state;
		uint8_t perc = 100 * a / b;
		if (perc == state)
			return;

		printf("\b\b\b%2u%%", perc);
		state = perc;
	}

	uint8_t reverse_bits(uint8_t b)
	{
		b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
		b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
		b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
		return b;
	}
}

void Numachip2::flash(const char *image, const size_t len)
{
	const int offset = 0x1000000;
	unsigned pos = 0;

	const unsigned pages = len / 256;
	const unsigned remainder = len % 256;
	unsigned sectors = len / 65536;

	write32(FLASH_REG3, 0xab88ef77);
	write32(FLASH_REG0, 0x2);
	while (1) {
		if (!(read32(FLASH_REG0) & 0x1))
			break;
		cpu_relax();
	}

	if ((len % 65536) != 0)
		sectors++;

	printf("Erasing  0%%");
	for (unsigned i = 0; i < sectors; i++) {
		write32(FLASH_REG1, offset + i * 65536);
		write32(FLASH_REG0, 0x7);
		while (1) {
			if (!(read32(FLASH_REG0) & 0x1))
				break;
			cpu_relax();
		}

		progress(i, sectors);
	}
	printf("\n");

	printf("Writing  0%%");
	for (unsigned p = 0; p < pages; p++) {
		write32(FLASH_REG1, offset + p * 256);
		for (unsigned i = 0; i < 256; i++) {
			write32(FLASH_REG2, reverse_bits(image[pos++]));
			write32(FLASH_REG0, 0x3);
		}

		write32(FLASH_REG0, 0x4);
		while (1) {
			if (!(read32(FLASH_REG0) & 0x1))
				break;
			cpu_relax();
		}

		progress(p, pages);
	}

	write32(FLASH_REG1, offset + pages * 256);
	for (unsigned i = 0; i < remainder; i++) {
		write32(FLASH_REG2, reverse_bits(image[pos++]));
		write32(FLASH_REG0, 0x3);
	}

	write32(FLASH_REG0, 0x4);
	while (1) {
		if (!(read32(FLASH_REG0) & 0x1))
			break;
		cpu_relax();
	}
	printf("\n");
}
