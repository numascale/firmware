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

	static inline uint64_t rand64(const uint64_t v)
	{
		return (v * 279470273ULL) % 4294967291ULL;
	}
}
