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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "spd.h"
#include "../bootloader.h"

/* CRC16 compute for DDR3 SPD; from DDR3 SPD spec */
static int crc16(char *ptr, int count)
{
	int i, crc = 0;

	while (--count >= 0) {
		crc = crc ^ (int)*ptr++ << 8;
		for (i = 0; i < 8; ++i)
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
	}
	return crc & 0xffff;
}

void ddr3_spd_check(const struct ddr3_spd_eeprom *spd)
{
	char *p = (char *)spd;
	int csum16, len;
	char crc_lsb;	/* byte 126 */
	char crc_msb;	/* byte 127 */

	/* SPD byte0[7] is CRC coverage: 0 = bytes 0-125, 1 = bytes 0-116 */
	len = !(spd->info_size_crc & 0x80) ? 126 : 117;
	csum16 = crc16(p, len);

	crc_lsb = (char)(csum16 & 0xff);
	crc_msb = (char)(csum16 >> 8);

	if (spd->crc[0] != crc_lsb || spd->crc[1] != crc_msb)
		fatal("SPD checksum 0x%02x%02x differs from expected 0x%02x%02x",
			crc_msb, crc_lsb, spd->crc[1], spd->crc[0]);

	xassert(spd->spd_rev >= 0x10);
}

