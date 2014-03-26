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

#include "e820.h"
#include "trampoline.h"
#include "../library/base.h"
#include "../bootloader.h"

extern "C" {
	#include "com32.h"
}

const char *E820::names[] = {"", "usable", "reserved", "ACPI data", "ACPI NVS", "unusable"};
char *E820::asm_relocated = 0;

void E820::dump(void)
{
	uint64_t last_base = map->base, last_length = map->length;

	for (int i = 0; i < *used; i++) {
		printf(" %011llx:%011llx (%011llx) %s\n",
		  map[i].base, map[i].base + map[i].length, map[i].length, names[map[i].type]);

		if (i) {
			assert(map[i].base >= (last_base + last_length));
			assert(map[i].length);
			last_base = map[i].base;
			last_length = map[i].length;
		}
	}
}

struct e820entry *E820::position(const uint64_t base)
{
	int i;

	for (i = 0; i < *used; i++)
		if (map[i].base + map[i].length >= base)
			break;

	if (options->debug.e820) {
		if (i < *used) {
			if (options->debug.e820 > 1)
				printf("Position at %011llx:%011llx (%011llx) %s",
				  map[i].base, map[i].base + map[i].length, map[i].length, names[map[i].type]);
		} else {
			if (options->debug.e820 > 1)
				printf("Position at end");
		}
	}

	return &map[i];
}

void E820::insert(struct e820entry *pos)
{
	if (options->debug.e820 > 1)
		printf(", inserting");

	int n = *used - (pos - map);
	if (n > 0)
		memmove(pos + 1, pos, sizeof(*pos) * n);

	(*used)++;
	assert(*used < E820_MAP_MAX);
}

void E820::add(const uint64_t base, const uint64_t length, const uint32_t type)
{
	if (options->debug.e820)
		printf("Adding e820 %011llx:%011llx (%011llx) %s\n", base, base + length, length, names[type]);

	assert(base < (base + length));

	struct e820entry *end = map + *used;
	struct e820entry *pos = position(base);
#ifdef FIXME
	uint64_t orig_base, orig_length;
	uint32_t orig_type;
#endif
	if (type == pos->type) {
		// extend end of existing range if adjacent
		if (base == pos->base + pos->length) {
			if (options->debug.e820 > 1)
				printf(", extending length");
			pos->length += length;
			goto out;
		}

		// extend start of existing range if adjacent
		if (base + length == pos->base) {
			if (options->debug.e820 > 1)
				printf(", lowering base");
			pos->base -= length;
			pos->length += length;
			goto out;
		}
	}

#ifdef FIXME
	orig_base = pos->base;
	orig_length = pos->length;
	orig_type = pos->type;
#endif

	// split start of existing memory range
	if (pos < end && base > pos->base) {
		if (options->debug.e820 > 1)
			printf(", splitting");
		pos->length = base - pos->base;
		pos++;
	}

	// add new range
	insert(pos);
	pos->base = base;
	pos->length = length;
	pos->type = type;
	pos++;

	// need to split end of existing memory range
#ifdef FIXME
	if (pos < end && (base + length) < (orig_base + orig_length)) {
		insert(pos);
		pos->base = base + length;
		pos->length = (orig_base + orig_length) - pos->base;
		pos->type = orig_type;
	}
#endif
out:
	if (options->debug.e820 > 1) {
		printf("\nUpdated e820 map:\n");
		dump();
		printf("\n");
	}
}

E820::E820(void)
{
	// setup relocated area
	uint32_t relocate_size = roundup(&asm_relocate_end - &asm_relocate_start, 1024);
	// see http://groups.google.com/group/comp.lang.asm.x86/msg/9b848f2359f78cdf
	uint32_t tom_lower = *((uint16_t *)0x413) << 10;
	char *asm_relocated = (char *)((tom_lower - relocate_size) & ~0xfff);
	printf("Tramppoline at 0x%p:0x%p\n", asm_relocated, (asm_relocated + relocate_size));

	// copy trampoline data
	memcpy(asm_relocated, &asm_relocate_start, relocate_size);
	map = (e820entry *)REL32(new_e820_map);
	used = REL16(new_e820_len);
	struct e820entry *ent = (struct e820entry *)lzalloc(sizeof(*ent));

	// read existing E820 entries
	com32sys_t rm;
	rm.eax.l = 0xe820;
	rm.edx.l = STR_DW_N("SMAP");
	rm.ebx.l = 0;
	rm.ecx.l = sizeof(*ent);
	rm.edi.w[0] = OFFS(ent);
	rm.es = SEG(ent);
	__intcall(0x15, &rm, &rm);
	assert(rm.eax.l == STR_DW_N("SMAP"));
	add(ent->base, ent->length, ent->type);

	while (rm.ebx.l > 0) {
		rm.eax.l = 0xe820;
		rm.edx.l = STR_DW_N("SMAP");
		rm.ecx.l = sizeof(*ent);
		rm.edi.w[0] = OFFS(ent);
		rm.es = SEG(ent);
		__intcall(0x15, &rm, &rm);
		add(ent->base, ent->length, ent->type);
	}

	lfree(ent);

	printf("BIOS-provided e820 map:\n");
	dump();

	add((uint64_t)asm_relocated, relocate_size, RESERVED);

	// install new int15h handler
	uint32_t *int_vecs = 0x0;
	*REL32(old_int15_vec) = int_vecs[0x15];
	int_vecs[0x15] = (((uint32_t)asm_relocated) << 12) |
	  ((uint32_t)(&new_e820_handler_relocate - &asm_relocate_start));
}

uint64_t E820::memlimit(void)
{
	assert(*used > 0);
	struct e820entry *pos = map + *used - 1;

	// assume some usable memory exists
	while (pos->type != RAM)
		pos--;

	const uint64_t limit = pos->base + pos->length;
	printf("Memory limit at %lluGB\n", limit >> 30);

	return limit;
}

void E820::test_range(const uint64_t start, const uint64_t end)
{
	const unsigned STEP_MIN = 64, STEP_MAX = 4 << 20;
	uint64_t pos = start;
	const uint64_t mid = start + (end - start) / 2;
	uint64_t step = STEP_MIN;

	printf(" [");
	while (pos < mid) {
		lib::mem_write32(pos, lib::mem_read32(pos));
		pos = (pos + step) & ~3;
		step = min(step << 1, STEP_MAX);
	}

	while (pos < end) {
		lib::mem_write32(pos, lib::mem_read32(pos));
		step = min((end - pos) / 2, STEP_MAX);
		pos += max(step, STEP_MIN) & ~3;
	}
	printf("accessible]");
}

void E820::test(void)
{
	struct e820entry *ent = (struct e820entry *)lzalloc(sizeof(*ent));

	printf("Testing e820 handler and access:\n");

	com32sys_t rm;
	rm.eax.l = 0xe820;
	rm.edx.l = STR_DW_N("SMAP");
	rm.ebx.l = 0;
	rm.ecx.l = sizeof(*ent);
	rm.edi.w[0] = OFFS(ent);
	rm.es = SEG(ent);
	__intcall(0x15, &rm, &rm);
	assert(rm.eax.l == STR_DW_N("SMAP"));

	printf("%011llx:%011llx (%011llx) %s",
	  ent->base, ent->base + ent->length, ent->length, names[ent->type]);
	assert(ent->length);
	if (ent->type == RAM)
		test_range(ent->base, ent->base + ent->length);
	printf("\n");

	while (rm.ebx.l > 0) {
		rm.eax.l = 0xe820;
		rm.edx.l = STR_DW_N("SMAP");
		rm.ecx.l = sizeof(*ent);
		rm.edi.w[0] = OFFS(ent);
		rm.es = SEG(ent);
		__intcall(0x15, &rm, &rm);

		printf("%011llx:%011llx (%011llx) %s",
		  ent->base, ent->base + ent->length, ent->length, names[ent->type]);
		if (ent->type == E820::RAM)
			test_range(ent->base, ent->base + ent->length);
		printf("\n");
	}

	lfree(ent);
}
