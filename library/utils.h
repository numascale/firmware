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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

namespace lib
{
	static inline uint64_t rdtscll(void)
	{
		uint64_t val;
		/* rdtscp doesn't work on Fam10h, so use mfence to serialise */
		asm volatile("mfence; rdtsc" : "=A"(val));
		return val;
	}

	void wait_key(const char *msg);
	void udelay(const uint32_t usecs);
	const char *pr_size(uint64_t size);
	void dump(const void *addr, const unsigned len);
	void memcpy(void *dst, const void *src, size_t n);

	static inline uint64_t hash64(uint64_t u) {
		u += 1;
		u ^= u >> 12; // a
		u ^= u << 25; // b
		u ^= u >> 27; // c
		return u * 2685821657736338717LL;
	}

	static inline uint32_t hash32(uint32_t a) {
		a = (a+0x7ed55d16) + (a<<12);
		a = (a^0xc761c23c) ^ (a>>19);
		a = (a+0x165667b1) + (a<<5);
		a = (a+0xd3a2646c) ^ (a<<9);
		a = (a+0xfd7046c5) + (a<<3);
		a = (a^0xb55a4f09) ^ (a>>16);
		return a;
	}

	// allocate memory at top near ACPI area to avoid conflicts
	static inline void *zalloc_top(const size_t size)
	{
		void *allocs[32]; // enough for 4GB of 128MB allocations
		int nallocs = 0;

		// allocate 128MB blocks until exhaustion
		while ((allocs[nallocs] = malloc(128 << 20)))
			nallocs++;

		// free last one guaranteeing space for allocation
		free(allocs[nallocs--]);

		// allocate requested size
		void *ptr = zalloc(size);

		// free remaining allocations
		while (nallocs)
			free(allocs[nallocs--]);

		return ptr;
	}
}
