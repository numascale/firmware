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

#pragma once

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define xoffsetof(a, b) ((uint64_t)(b)-(uint64_t)(a))
#define roundup(x, n) (((x) + ((n) - 1)) & (~((n) - 1)))
#define poweroftwo(x) (!((x) & ((x) - 1)))
#define roundup_pow2(x, y) ({uint64_t power = (y); while (power < (x)) power <<=1; power;})
#define cpu_relax() asm volatile("pause" ::: "memory")
#define halt() while (1) asm volatile("cli; hlt" ::: "memory")

#define PRInode "node 0x%03x (%s)"

#define checked __attribute__ ((warn_unused_result))
#define nonnull __attribute__ ((nonnull))

/* ASCII-Art */
#define COL_DEFAULT   "\033[0m"
#define COL_RED       "\033[31m"
#define COL_YELLOW    "\033[33m"
#define CLEAR         "\033e\033%%@\033)0\033(B\3#007\033[?25h\033[2J\033[H"
#define BANNER        "\033[1m"

#define SCI_LOCAL 0xfff

#define STR_DW_N(a) (uint32_t)((a[0] << 24) + (a[1] << 16) + (a[2] << 8) + a[3])
#define STR_DW_H(a) (uint32_t)(a[0] + (a[1] << 8) + (a[2] << 16) + (a[3] << 24))

// prevent null syslinux assert being used
#undef assert

#define xassert(cond) do { if (!(cond)) {				\
			printf(COL_RED "Error: assertion '%s' failed in %s at %s:%d\n",	\
			       #cond, __FUNCTION__, __FILE__, __LINE__); \
			printf(COL_DEFAULT);				\
			halt();						\
		} } while (0)

#define fatal(format, args...) do {					\
		printf(COL_RED "Error: ");				\
		printf(format, ## args);				\
		printf(COL_DEFAULT);					\
		halt();							\
	} while (0)

#define warning(format, args...) do {					\
		printf(COL_YELLOW "Warning: ");				\
		printf(format, ## args);				\
		printf(COL_DEFAULT "\n");				\
	} while (0)

#define warning_once(format, args...) do {		\
		static bool printed = 0;		\
		if (!printed) {				\
			printf(COL_YELLOW "Warning: ");	\
			printf(format, ## args);	\
			printf(COL_DEFAULT "\n");	\
			printed = 1;			\
		}} while (0)

#define error(format, args...) do {					\
		printf(COL_RED "Error: ");				\
		printf(format, ## args);				\
		printf(COL_DEFAULT "\n");				\
	} while (0)

#define error_remote(sci, name, ip, msg) do {				\
		if (sci != 0xffffffff)					\
			printf(COL_RED "Error on SCI%03x/%s: %s" COL_DEFAULT "\n", sci, name, msg); \
		else							\
			printf(COL_RED "Error on %d.%d.%d.%d: %s" COL_DEFAULT "\n", \
			       ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff, msg); \
	} while (0)

#define assertf(cond, format, args...) do { if (!(cond)) {		\
			printf(COL_RED "Error: ");			\
			printf(format, ## args);			\
			printf(COL_DEFAULT);				\
			halt();						\
		}} while(0)

typedef uint8_t link_t;
typedef uint8_t ht_t;
typedef uint16_t sci_t;
typedef uint16_t reg_t;
typedef uint32_t msr_t;
typedef uint16_t apic_t;

#ifdef SIM
inline void lfree(void *ptr)
{
	free(ptr);
}

inline void *zalloc(size_t size)
{
	void *addr = malloc(size);
	xassert(addr);
	memset(addr, 0, size);
	return addr;
}
#endif

template<class T> class Vector {
	unsigned lim;

	void ensure(void) {
		xassert(used <= lim);
		if (used == lim) {
			lim += 8;
			elements = (T *)realloc((void *)elements, sizeof(T) * lim);
		}
	}
public:
	unsigned used;
	T *elements, *limit;

	Vector(void): lim(0), used(0), elements(NULL), limit(NULL) {}
	~Vector(void) {
		free(elements);
	}

	void add(T elem) {
		ensure();
		elements[used++] = elem;
		limit = &elements[used];
	}

	void del(const unsigned offset) {
		xassert(offset < used);
		memmove(&elements[offset], &elements[offset + 1], (used - offset - 1) * sizeof(T));
		used--;
		limit = &elements[used];
	}

	void insert(T elem, const unsigned pos) {
		ensure();

		/* Move any later elements down */
		if (used > pos)
			memmove(&elements[pos + 1], &elements[pos], (used - pos) * sizeof(T));

		elements[pos] = elem;
		used++;
		limit = &elements[used];
	}
};
