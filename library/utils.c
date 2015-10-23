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

#include <stdio.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "utils.h"
#include "../opteron/opteron.h"

#define PRETTY_SIZE 16
#define PRETTY_COUNT 4

void *operator new(const size_t n)
{
	void *p = zalloc(n);
	xassert(p);
	return p;
}

// placement new
void *operator new(const size_t n, void *const p)
{
	memset(p, 0, n);
	return p;
}

void operator delete(void *const p)
{
	free(p);
}

namespace lib
{
	void wait_key(const char *msg)
	{
		puts(msg);
		char ch;

		do {
			fread(&ch, 1, 1, stdin);
			cpu_relax();
		} while (ch != 0x0a && ch != 0x0d); // enter
	}

	void udelay(const uint32_t usecs)
	{
		uint64_t limit = lib::rdtscll() + (uint64_t)usecs * Opteron::tsc_mhz;

		while (lib::rdtscll() < limit)
			cpu_relax();
	}

	const char *pr_size(uint64_t size)
	{
		static char pretty[PRETTY_COUNT][PRETTY_SIZE];
		static unsigned index = 0;
		const char units[] = {0, 'K', 'M', 'G', 'T', 'P'};

		unsigned i = 0;
		while (size >= 1024 && i < sizeof(units) - 1) {
			size /= 1024;
			i++;
		}

		if (units[i])
			snprintf(pretty[index], PRETTY_SIZE, "%"PRIu64"%cB", size, units[i]);
		else
			snprintf(pretty[index], PRETTY_SIZE, "%"PRIu64"B", size);

		const char *ret = pretty[index];
		index = (index + 1) % PRETTY_COUNT;
		return ret;
	}

	void dump(const void *addr, const unsigned len)
	{
		const unsigned char *addr2 = (const unsigned char *)addr;
		unsigned i = 0;

		while (i < len) {
			for (int j = 0; j < 16 && (i + j) < len; j++)
				printf(" %02x", addr2[i + j]);
			i += 16;
			printf("\n");
		}
	}

	void memcpy(void *dst, const void *src, size_t n)
	{
		printf("Copying %zu bytes from %p to %p:\n", n, src, dst);
		dump(src, n);
		::memcpy(dst, src, n);
	}
}
