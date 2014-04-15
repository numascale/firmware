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
#include <stdio.h>
#include <stdlib.h>

#include "../bootloader.h"
#include "smbios.h"

const char *SMBIOS::string(const char *table, uint8_t index)
{
	while (--index)
		table += strlen(table) + 1;
	return table;
}

SMBIOS::SMBIOS(void)
{
	const char *mem = (const char *)0xf0000;
	const char *buf, *data;
	size_t fp;
	int i = 0;

	/* Search for signature */
	for (fp = 0; fp <= 0xfff0; fp += 16)
		if (!memcmp(mem + fp, "_SM_", 4))
			break;

	assertf(fp < 0x10000, "Failed to find SMBIOS signature");

	uint16_t len = *(uint16_t *)(mem + fp + 0x16);
	uint16_t num = *(uint16_t *)(mem + fp + 0x1c);
	fp = *(uint32_t *)(mem + fp + 0x18);

	buf = data = (const char *)fp;

	while (i < num && data + 4 <= buf + len) {
		const char *next;
		struct smbios_header *h = (struct smbios_header *)data;

		assert(h->length >= 4);
		if (h->type == 127)
			break;

		next = data + h->length;

		if (h->type == 0) {
			biosver = string(next, data[5]);
			biosdate = string(next, data[8]);
		} else if (h->type == 1) {
			sysmanuf = string(next, data[4]);
			sysproduct = string(next, data[5]);
		} else if (h->type == 2) {
			boardmanuf = string(next, data[4]);
			boardproduct = string(next, data[5]);
		}

		while (next - buf + 1 < len && (next[0] != 0 || next[1] != 0))
			next++;
		next += 2;
		data = next;
		i++;
	}

	assert(biosver);
	assert(biosdate);
	assert(sysmanuf);
	assert(sysproduct);
	assert(boardmanuf);
	assert(boardproduct);

	printf("Motherboard is %s %s/%s %s with BIOS %s %s", sysmanuf, sysproduct, boardmanuf, boardproduct, biosver, biosdate);
}
