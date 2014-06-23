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

void E820::dump(void)
{
	uint64_t last_base = map->base, last_length = map->length;

	for (int i = 0; i < *used; i++) {
		printf(" %011llx:%011llx (%011llx) %s\n",
		  map[i].base, map[i].base + map[i].length, map[i].length, names[map[i].type]);

		if (i) {
			lassert(map[i].base >= (last_base + last_length));
			lassert(map[i].length);
			lassert(map[i].length < (16ULL << 40));
			last_base = map[i].base;
			last_length = map[i].length;
		}
	}
}

struct e820entry *E820::position(const uint64_t addr)
{
	int i;

	for (i = 0; i < *used; i++)
		if (addr < map[i].base + map[i].length)
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

	*used += 1;
	assert(*used < E820_MAP_MAX);
}

void E820::remove(struct e820entry *start, struct e820entry *end)
{
	const struct e820entry *last = map + *used;
	memmove(start, end, (size_t)last - (size_t)end);
	*used -= end - start;
}

bool E820::overlap(const uint64_t a1, const uint64_t a2, const uint64_t b1, const uint64_t b2) const
{
	if (a2 > b1 && a1 < b2)
		return 1;
	if (a1 <= b1 && a2 >= b2)
		return 1;
	if (a1 >= b1 && a2 <= b2)
		return 1;

	return 0;
}

void E820::add(const uint64_t base, const uint64_t length, const uint32_t type)
{
	if (options->debug.e820)
		printf("Adding e820 %011llx:%011llx (%011llx) %s\n", base, base + length, length, names[type]);

	assert(base < (base + length));

	struct e820entry *last = map + *used;
	struct e820entry *spos = position(base);

	// if valid entries
	if (last > map && spos < last) {
		// split head
		if (overlap(base, base + length, spos->base, spos->base + spos->length) && base != spos->base) {
			insert(spos);
			spos++;
			last++;
			spos->base = base;
			spos->length = (spos-1)->base + (spos-1)->length - base;
			spos->type = (spos-1)->type;
			(spos-1)->length = base - (spos-1)->base;
		}
	}

	struct e820entry *epos = position(base + length);

	// if valid entries
	if (last > map && epos < last) {
		// split tail
		if (overlap(base, base + length, epos->base, epos->base + epos->length) && base + length != epos->base + epos->length) {
			epos++;
			insert(epos);
			last++;
			epos->type = (epos-1)->type;
			epos->base = base + length;
			epos->length = (epos-1)->base + (epos-1)->length - epos->base;
			(epos-1)->length = base + length - (epos-1)->base;
		}
	}

	// delete all covered entries
	remove(spos, epos);

	// insert new entry
	insert(spos);
	spos->base = base;
	spos->length = length;
	spos->type = type;

	// merge adjacent entries of the same type
	unsigned pos = *used - 1;
	while (pos > 0) {
		struct e820entry *cur = map + pos;
		struct e820entry *bef = cur - 1;

		if (bef->base + bef->length == cur->base && bef->type == cur->type) {
			cur->length += bef->length;
			cur->base = bef->base;
			remove(bef, cur);
			continue;
		}

		pos--;
	}
}

uint64_t E820::expand(const uint64_t type, const uint64_t size)
{
	uint64_t base;

	for (int i = 0; i < *used; i++) {
		if (map[i].type != type)
			continue;

		// check next extry is adjacent and is usable
		if (i < *used-1 && map[i+1].base == map[i].base + map[i].length && map[i+1].type == RAM) {
			base = map[i+1].base;
			goto out;
		}

		// check previous extry is adjacent and is usable
		if (i > 0 && map[i-1].base + map[i-1].length == map[i].base && map[i-1].type == RAM) {
			base = map[i].base - size;
			goto out;
		}
	}

	fatal("Unable to find region of type %llu with adjacent space", type);
out:
	if (options->debug.e820)
		printf("Expanding: ");
	add(base, size, type);
	return base;
}

E820::E820(void)
{
	// setup relocated area
	uint32_t relocate_size = roundup(&asm_relocate_end - &asm_relocate_start, 1024);
	// see http://groups.google.com/group/comp.lang.asm.x86/msg/9b848f2359f78cdf
	uint32_t tom_lower = *((uint16_t *)0x413) << 10;
	asm_relocated = (char *)((tom_lower - relocate_size) & ~0xfff);
	printf("Trampoline at 0x%x:0x%x\n", (unsigned)asm_relocated, (unsigned)(asm_relocated + relocate_size));

	// copy trampoline data
	memcpy(asm_relocated, &asm_relocate_start, relocate_size);
	map = (e820entry *)REL32(new_e820_map);
	used = REL16(new_e820_len);

	// read existing E820 entries
	uint64_t base, length, type;
	syslinux->memmap_start();

	bool last;
	do {
		last = syslinux->memmap_entry(&base, &length, &type);
		add(base, length, type);
	} while (last);

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

void E820::test_address(const uint64_t addr, const uint64_t val)
{
	lib::mem_write64(addr, val);
	uint64_t val2 = lib::mem_read64(addr);

	if (val2 != val) {
		test_errors++;
		warning("Readback of 0x%llx after writing 0x%016llx gives 0x%016llx", addr, val, val2);
	}
}

void E820::test_location(const uint64_t addr)
{
	uint64_t val = lib::mem_read64(addr);
	test_address(addr, PATTERN);
	test_address(addr, ~PATTERN);
	test_address(addr, ~0ULL);
	test_address(addr, val);
}

void E820::test_range(const uint64_t start, const uint64_t end)
{
	// memory on the first northbridge is actively used
	if (start < 0x000428000000) // FIXME
		return;

	const unsigned STEP_MIN = 64, STEP_MAX = 4 << 20;
	uint64_t pos = start;
	const uint64_t mid = start + (end - start) / 2;
	uint64_t step = STEP_MIN;

	printf(" [");
	while (pos < mid) {
		test_location(pos);
		pos = (pos + step) & ~7;
		step = min(step << 1, STEP_MAX);
	}

	while (pos < end) {
		test_location(pos);
		step = min((end - pos) / 2, STEP_MAX);
		pos += max(step, STEP_MIN) & ~7;
	}
	printf("accessible]");
}

void E820::test(void)
{
	printf("Testing e820 handler and access:\n");

	// read existing E820 entries
	uint64_t base, length, type;
	syslinux->memmap_start();

	bool left;
	test_errors = 0;

	do {
		left = syslinux->memmap_entry(&base, &length, &type);
		printf("%011llx:%011llx (%011llx) %s", base, base + length, length, names[type]);
		if (type == RAM)
			test_range(base, base + length);
		printf("\n");
	} while (left);

	if (test_errors)
		warning("%llu errors", test_errors);
}
