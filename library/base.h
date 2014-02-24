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

#ifndef __BASE_H
#define __BASE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define roundup(x, n) (((x) + ((n) - 1)) & (~((n) - 1)))
#define cpu_relax() asm volatile("pause" ::: "memory")
#define PRInode "node 0x%03x (%s)"

#define checked __attribute__ ((warn_unused_result))

/* ASCII-Art */
#define COL_DEFAULT   "\033[0m"
#define COL_RED       "\033[31m"
#define COL_YELLOW    "\033[33m"
#define CLEAR         "\033\143"
#define BANNER        "\033[1m"

#define STR_DW_N(a) (uint32_t)((a[0] << 24) + (a[1] << 16) + (a[2] << 8) + a[3])
#define STR_DW_H(a) (uint32_t)(a[0] + (a[1] << 8) + (a[2] << 16) + (a[3] << 24))

#define lassert(cond) do { if (!(cond)) { \
        printf("Error: assertion '%s' failed in %s at %s:%d\n", \
            #cond, __FUNCTION__, __FILE__, __LINE__); while (1); \
    } } while (0)

#define assert(cond) do { if (!(cond)) {				\
	printf(COL_RED "Error: assertion '%s' failed in %s at %s:%d\n",	\
	    #cond, __FUNCTION__, __FILE__, __LINE__);			\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
    } } while (0)

#define fatal(format, args...) do {					\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
   } while (0)

#define warning(format, args...) do {					\
	printf(COL_YELLOW "Warning: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT "\n");					\
   } while (0)

#define error(format, args...) do {					\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT "\n");					\
   } while (0)

#define assertf(cond, format, args...) do { if (!(cond)) {		\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
    } } while(0)

#define IMPORT_RELOCATED(sym) extern volatile uint8_t sym ## _relocate
#define REL8(sym) ((uint8_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL16(sym) ((uint16_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL32(sym) ((uint32_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL64(sym) ((uint64_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))

typedef uint8_t link_t;
typedef uint8_t ht_t;
typedef uint16_t sci_t;
typedef uint16_t reg_t;

inline void *operator new(const size_t size)
{
	return zalloc(size);
}

inline void *operator new[](const size_t size)
{
	return zalloc(size);
}

inline void operator delete(void *const p)
{
	free(p);
}

inline void operator delete[](void *const p)
{
	free(p);
}

template<class T> class Vector {
	unsigned lim;

	void ensure(void) {
		lassert(used <= lim);
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
		lassert(offset < used);
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

#endif

