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

#define FLASH_PAGE_SIZE   256
#define FLASH_SECTOR_SIZE 65536

#define ASMI_CMD_READ_STATUS    1
#define ASMI_CMD_SET_4BYTE_MODE 2
#define ASMI_CMD_WRITE          3
#define ASMI_CMD_PAGE_PROGRAM   4
#define ASMI_CMD_READ           5
#define ASMI_CMD_PROTECT        6
#define ASMI_CMD_ERASE          7
#define ASMI_CMD_READ_MEMSIZE   8
#define ASMI_CMD_BULK_ERASE     9
#define ASMI_CMD_RU_READ        10
#define ASMI_CMD_RU_WRITE       11

namespace
{
	void progress(const uint32_t a, const uint32_t b)
	{
		static uint8_t state;
		const uint32_t c = (b > 1) ? b - 1 : b;
		uint8_t perc = 100 * a / c;
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

void Numachip2::flash(const uint8_t *image, const size_t len)
{
	const int offset = 0x1000000;
	unsigned pos = 0;

	const unsigned pages = len / FLASH_PAGE_SIZE;
	const unsigned remainder = len % FLASH_PAGE_SIZE;
	unsigned sectors = len / FLASH_SECTOR_SIZE;

	// Set magic value
	write32(FLASH_REG3, 0xab88ef77);
	(void)read32(FLASH_REG3);

	// Set 4byte mode
	write32(FLASH_REG0, ASMI_CMD_SET_4BYTE_MODE);
	while (1) {
		if (!(read32(FLASH_REG0) & 0x1))
			break;
		cpu_relax();
	}

	if ((len % FLASH_SECTOR_SIZE) != 0)
		sectors++;

	printf("Erasing  0%%");
	for (unsigned i = 0; i < sectors; i++) {
		write32(FLASH_REG1, offset + i * FLASH_SECTOR_SIZE);
		write32(FLASH_REG0, ASMI_CMD_ERASE);
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
		write32(FLASH_REG1, offset + p * FLASH_PAGE_SIZE);
		for (unsigned i = 0; i < FLASH_PAGE_SIZE; i++) {
			write32(FLASH_REG2, reverse_bits(image[pos++]));
			write32(FLASH_REG0, ASMI_CMD_WRITE);
		}

		write32(FLASH_REG0, ASMI_CMD_PAGE_PROGRAM);
		while (1) {
			if (!(read32(FLASH_REG0) & 0x1))
				break;
			cpu_relax();
		}

		progress(p, pages);
	}

	write32(FLASH_REG1, offset + pages * FLASH_PAGE_SIZE);
	for (unsigned i = 0; i < remainder; i++) {
		write32(FLASH_REG2, reverse_bits(image[pos++]));
		write32(FLASH_REG0, ASMI_CMD_WRITE);
	}

	write32(FLASH_REG0, ASMI_CMD_PAGE_PROGRAM);
	while (1) {
		if (!(read32(FLASH_REG0) & 0x1))
			break;
		cpu_relax();
	}
	printf("\n");

	printf("Verifying  0%%");
	for (unsigned i = 0; i < len; i++) {
		write32(FLASH_REG1, offset + i);
		write32(FLASH_REG0, ASMI_CMD_READ);

		while (1) {
			if (!(read32(FLASH_REG0) & 0x1))
				break;
			cpu_relax();
		}

		uint8_t val = reverse_bits(read32(FLASH_REG2) & 0xff);
		xassert(val == image[i]);

		progress(i, len);
	}

	printf("\n");
}
