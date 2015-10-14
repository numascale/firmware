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
#include "../platform/config.h"
#include "../bootloader.h"

void Numachip2::fabric_init(void)
{
	const char *ringnames[] = {"XA", "XB", "YA", "YB", "ZA", "ZB"};

	printf("Fabric connected:");

	for (int lc = 0; lc < 6; lc++) {
		if (!(config->ringmask & (1 << lc)))
			continue;

		printf(" %s", ringnames[lc]);
		lcs[lc] = new LC5(LC_BASE + lc * LC_SIZE);
	}

	printf("\n");
}
