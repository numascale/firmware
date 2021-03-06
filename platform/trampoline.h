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

#ifndef __ASSEMBLER__
#include "../library/base.h"
#include "../platform/options.h"
#endif

#define VECTOR_CACHE_DISABLE  1
#define VECTOR_CACHE_ENABLE   2
#define VECTOR_SETUP          3
#define VECTOR_SETUP_OBSERVER 4
#define VECTOR_TEST           5
#define VECTOR_TEST_FINISH    6

#define E820_MAP_MAX 4096
#define MSR_MAX 32
#define CORE_SPINS        100000000
#define TEST_BASE_HIGH 0x1 // 4GB base
#define TEST_BASE_LOW  0x88000
#define TEST_SIZE      (1 << 12)

#ifndef __ASSEMBLER__
#define IMPORT_RELOCATED(sym) extern volatile uint8_t sym ## _relocate
#define REL8(sym) ((uint8_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL16(sym) ((uint16_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL32(sym) ((uint32_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL64(sym) ((uint64_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))

extern unsigned char asm_relocate_start, asm_relocate_end;
extern char *asm_relocated;

IMPORT_RELOCATED(entry);
IMPORT_RELOCATED(msrs);
IMPORT_RELOCATED(vector);
IMPORT_RELOCATED(pending);
IMPORT_RELOCATED(errors);
IMPORT_RELOCATED(apic_local);
IMPORT_RELOCATED(old_int15_vec);
IMPORT_RELOCATED(new_e820_len);
IMPORT_RELOCATED(new_e820_map);
IMPORT_RELOCATED(new_e820_handler);

struct msr_ent {
	uint32_t num;
	uint64_t val;
};

static inline void trampoline_sem_init(const uint16_t val)
{
	*REL16(pending) = val;
}

static inline uint16_t trampoline_sem_getvalue(void)
{
	return *REL16(pending);
}

static inline bool trampoline_sem_wait(void)
{
	for (unsigned spin = 0; spin < CORE_SPINS; spin++) {
		if (*REL16(pending) == 0)
			return 0;

		cpu_relax();
	}

	return 1;
}

static inline void push_msr(const uint32_t num, const uint64_t val)
{
	if (options->debug.cores)
		printf("Global MSR%08x -> %016" PRIx64 "\n", num, val);
	xassert(asm_relocated);
	struct msr_ent *msrp = (struct msr_ent *)REL32(msrs);
	xassert(!((unsigned long)msrp & 3)); // ensure alignment
	unsigned i = 0;

	while (msrp[i].num) {
		// update existing entry
		if (msrp[i].num == num) {
			msrp[i].val = val;
			break;
		}
		xassert(++i < MSR_MAX);
	}

	// add new extry
	msrp[i].num = num;
	msrp[i].val = val;
}

#endif
