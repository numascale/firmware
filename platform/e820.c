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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "e820.h"
#include "trampoline.h"
#include "../library/base.h"
#include "../library/utils.h"
#include "../bootloader.h"

#ifndef SIM
extern "C" {
	#include "com32.h"
}
#endif

const char *E820::names[] = {"type 0", "usable", "reserved", "ACPI data", "ACPI NVS", "unusable",
  "type 6", "type 7", "type 8", "type 9"};

void E820::dump(void)
{
	uint64_t last_base = map->base, last_length = map->length;

	for (int i = 0; i < *used; i++) {
		printf(" %011"PRIx64":%011"PRIx64" (%011"PRIx64") %s\n",
		  map[i].base, map[i].base + map[i].length, map[i].length, names[map[i].type]);

		if (i) {
			xassert(map[i].base >= (last_base + last_length));
			xassert(map[i].length);
			xassert(map[i].length < (16ULL << 40));
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
				printf("Position at %011"PRIx64":%011"PRIx64" (%011"PRIx64") %s",
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
	xassert(*used < E820_MAP_MAX);
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
		printf("Adding e820 %011"PRIx64":%011"PRIx64" (%011"PRIx64") %s\n", base, base + length, length, names[type]);

	xassert(base < (base + length));

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

	fatal("Insufficient space to expand %s region by %"PRIu64" bytes", names[type], size);
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
	if (options->debug.e820)
		printf("Trampoline at 0x%x:0x%x\n", (unsigned)asm_relocated, (unsigned)(asm_relocated + relocate_size));

	// copy trampoline data
	memcpy(asm_relocated, &asm_relocate_start, relocate_size);
	map = (e820entry *)REL32(new_e820_map);
	xassert(!((unsigned long)map & 3));
	used = REL16(new_e820_len);
	xassert(!((unsigned long)used & 1)); // check alignment

	// read existing E820 entries
	uint64_t base, length, type;
	os->memmap_start();

	bool last;
	do {
		last = os->memmap_entry(&base, &length, &type);
		add(base, length, type);
	} while (last);

	if (options->debug.e820) {
		printf("BIOS-provided e820 map:\n");
		dump();
	}

	add((uint64_t)asm_relocated, relocate_size, RESERVED);

	// install new int15h handler
	volatile uint32_t *int_vecs = 0x0;
	xassert(!((unsigned long)REL32(old_int15_vec) & 3)); // ensure alignment
	*REL32(old_int15_vec) = int_vecs[0x15];
	int_vecs[0x15] = (((uint32_t)asm_relocated) << 12) |
	  ((uint32_t)(&new_e820_handler_relocate - &asm_relocate_start));
}

uint64_t E820::memlimit(void)
{
	xassert(*used > 0);
	struct e820entry *pos = map + *used - 1;

	// assume some usable memory exists
	while (pos->type != RAM)
		pos--;

	const uint64_t limit = pos->base + pos->length;
	printf("Memory limit at %"PRIu64"GB\n", limit >> 30);

	return limit;
}

void E820::test_address(const uint64_t addr, const uint64_t val)
{
	lib::mem_write64(addr, val);
	uint64_t val2 = lib::mem_read64(addr);

	if (val2 != val) {
		test_errors++;
		warning("Readback of 0x%"PRIx64" after writing 0x%016"PRIx64" gives 0x%016"PRIx64, addr, val, val2);
	}
}

void E820::test_location(const uint64_t addr, const test_state state)
{
	// only do read under 4GB
	if (addr < (4ULL << 32)) {
		(void)lib::mem_read64(addr);
		return;
	}

	uint64_t hash, val;

	switch (state) {
	case Seed:
		// memory was zeroed ealier; verify
		val = lib::mem_read64(addr);
		if (val != 0) {
			printf("address 0x%llx was 0x%016llx but should have been 0 (seed)\n", addr, val);
			test_errors++;
		}

		lib::mem_write64(addr, lib::hash64(addr));
		break;
	case Test:
		val = lib::mem_read64(addr);
		hash = lib::hash64(addr);
		if (val != hash) {
			printf("address 0x%llx was 0x%016llx but should have been 0x%016llx (seed)\n", addr, val, hash);
			test_errors++;
		}

		// re-zero memory
		lib::mem_write64(addr, 0);
		break;
	case Rezero:
		// verify zeroing
		val = lib::mem_read64(addr);
		if (val != 0) {
			printf("address 0x%llx was 0x%016llx but should have been 0 (rezero)\n", addr, val);
			test_errors++;
		}
		break;
	}
}

void E820::test_range(const uint64_t start, const uint64_t end, const test_state state)
{
	// memory on the first northbridge is actively used
	if (start < (4ULL << 30))
		return;

	const unsigned STEP_MIN = 64, STEP_MAX = 4 << 20;
	uint64_t pos = start;
	const uint64_t mid = start + (end - start) / 2;
	uint64_t step = STEP_MIN;

	if (options->debug.e820)
		printf(" [");
	while (pos < mid) {
		test_location(pos, state);
		pos = (pos + step) & ~7;
		step = min(step << 1, STEP_MAX);
	}

	while (pos < end) {
		test_location(pos, state);
		step = min((end - pos) / 2, STEP_MAX);
		pos += max(step, STEP_MIN) & ~7;
	}
	if (options->debug.e820)
		printf("accessible]");
}

void E820::test(void)
{
	printf("Testing e820 handler and access");

	// read existing E820 entries
	uint64_t base, length, type;
	os->memmap_start();

	uint64_t last = 0;

	for (test_state phase = Seed; phase <= Rezero; phase = test_state(phase + 1)) {
		bool left;
		test_errors = 0;

		do {
			left = os->memmap_entry(&base, &length, &type);

			if (options->debug.e820)
				printf("\n%011"PRIx64":%011"PRIx64" (%011"PRIx64") %s", base, base + length, length, names[type]);

			if (type == RAM)
				test_range(base, base + length, phase);

			// check every ~4s
			uint64_t now = lib::rdtscll();
			if (now - last > 1e10) {
				check();
				last = now;
			}
		} while (left);
	}

	printf("\n");
	if (test_errors)
		warning("%"PRIu64" errors", test_errors);
}
