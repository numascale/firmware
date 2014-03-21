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

#define E820_MAX_LEN 4096

struct e820entry {
	uint64_t base;
	uint64_t length;
	uint32_t type;
} __attribute__((packed));

class E820 {
private:
	struct e820entry *map;
	uint16_t *used;
	struct e820entry *position(const uint64_t base);
	void insert(struct e820entry *pos);
	static const char *names[6];
public:
	static const uint64_t RAM = 1;
	static const uint64_t RESERVED = 2;
	static const uint64_t ACPI = 3;
	static const uint64_t NVS = 4;
	static const uint64_t UNUSABLE = 5;

	E820(void);
	void dump(void);
	void add(const uint64_t base, const uint64_t length, const uint32_t type);
	uint64_t memlimit(void);
	void install(void);
	void test(void);
};
