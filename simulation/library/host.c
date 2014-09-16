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

#include "../../platform/os.h"
#include "../../platform/e820.h"
#include <stdlib.h>
#include <stdint.h>

OS::OS(void)
{
}

char *OS::read_file(const char *filename, int *const len)
{
	return 0;
}

void OS::exec(const char *label)
{
	exit(0);
}

void OS::memmap_start(void)
{
}

bool OS::memmap_entry(uint64_t *base, uint64_t *length, uint64_t *type)
{
	*base = 0x0;
	*length = 128ULL << 30;
	*type = E820::RAM;

	return 0;
}
