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

#include <stdint.h>
#include "../library/base.h"

#define E820_MAX_LEN 4096

struct e820entry {
	uint64_t base;
	uint64_t length;
	uint32_t type;
} __attribute__((packed));

class E820 {
private:
	enum test_state {Seed, Test, Rezero};
	uint64_t test_errors;
	struct e820entry *map;
	uint16_t *used;
	static const char *names[10];
	static const uint64_t PATTERN = 0xa0a1a2a3a4a5a6a7ULL;

	void insert(struct e820entry *pos) nonnull;
	void remove(struct e820entry *start, struct e820entry *end) nonnull;
	bool overlap(const uint64_t a1, const uint64_t a2, const uint64_t b1, const uint64_t b2) const;
	void test_address(const uint64_t addr, const uint64_t val);
	void test_location(const uint64_t addr, const test_state state);
	void test_range(const uint64_t start, const uint64_t end, const test_state state);
public:
	static const uint64_t RAM = 1;
	static const uint64_t RESERVED = 2;
	static const uint64_t ACPI = 3;
	static const uint64_t NVS = 4;
	static const uint64_t UNUSABLE = 5;

	struct e820entry *position(const uint64_t addr);
	E820(void);
	bool exists(const uint64_t base, const uint64_t length) const;
	void dump(void);
	void add(const uint64_t base, const uint64_t length, const uint32_t type);
	uint64_t expand(const uint64_t type, const uint64_t size);
	uint64_t memlimit(void);
	void test(void);
};

extern E820 *e820;
