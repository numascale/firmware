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
#include "../library/base.h"
#include "../bootloader.h"

extern "C" {
	#include "com32.h"
}

const char *E820::names[] = {"", "usable", "reserved", "ACPI data", "ACPI NVS", "unusable"};

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
			return &map[i];

	return &map[i];
}

void E820::insert(struct e820entry *pos)
{
	int n = *used - (pos - map);
	if (n > 0)
		memmove(pos + 1, pos, sizeof(*pos) * n);

	(*used)++;
}

void E820::add(const uint64_t base, const uint64_t length, const uint32_t type)
{
#ifdef UNUSED
	if (options->debug.e820)
		printf("Adding e820 %011llx:%011llx (%011llx) [%d]\n", base, base + length, length, type);
#endif
	struct e820entry *end = map + *used;
	struct e820entry *pos = position(base);

	/* Extend end of existing range if adjacent */
	if (base == pos->base + pos->length && type == pos->type) {
		pos->length += length;
		return;
	}

	/* Extend start of existing range if adjacent */
	if (base + length == pos->base) {
		pos->base -= length;
		pos->length += length;
		return;
	}

	const uint64_t orig_base = pos->base, orig_length = pos->length;
	const uint32_t orig_type = pos->type;

	/* Split start of existing memory range */
	if (pos < end && base > pos->base) {
		pos->length = base - pos->base;
		pos++;
	}

	/* Add new range */
	insert(pos);
	pos->base = base;
	pos->length = length;
	pos->type = type;
	pos++;

	/* Need to split end of existing memory range */
	if (pos < end && (base + length) < (orig_base + orig_length)) {
		insert(pos);
		pos->base = base + length;
		pos->length = (orig_base + orig_length) - pos->base;
		pos->type = orig_type;
	}
}

E820::E820(void)
{
	/* FIXME: use map in secondary
	 * map = (e820entry *)REL32(e820_map);
	 * used = *REL16(e820_used); */
	map = (struct e820entry *)lzalloc(4096);
	assert(map);
	used = (uint16_t *)malloc(sizeof(*used));
	assert(used);
	*used = 0;

	struct e820entry *ent = (struct e820entry *)lzalloc(sizeof(*ent));

	com32sys_t rm;
	rm.eax.l = 0x0000e820;
	rm.edx.l = STR_DW_N("SMAP");
	rm.ebx.l = 0;
	rm.ecx.l = sizeof(*ent);
	rm.edi.w[0] = OFFS(ent);
	rm.es = SEG(ent);
	__intcall(0x15, &rm, &rm);
	assert(rm.eax.l == STR_DW_N("SMAP"));
	add(ent->base, ent->length, ent->type);

	while (rm.ebx.l > 0) {
		rm.eax.l = 0x0000e820;
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
}

uint64_t E820::memlimit(void)
{
	assert(*used > 0);
	struct e820entry *pos = map + *used - 1;

	/* Assume some usable memory exists */
	while (pos->type != RAM)
		pos--;

	const uint64_t limit = pos->base + pos->length;
	printf("Memory limit at %lluGB\n", limit >> 30);

	return limit;
}
