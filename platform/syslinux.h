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

extern "C" {
	#include <syslinux/pxe.h>
}
#include <netinet/in.h>

class Syslinux
{
	struct e820entry *ent;
	com32sys_t state;

	void get_hostname(void);
public:
	struct in_addr ip;
	const char *hostname;
	uint8_t mac[6];

	Syslinux(void);
	char *read_file(const char *filename, int *const len);
	void exec(const char *label);
	void memmap_start(void);
	bool memmap_entry(uint64_t *base, uint64_t *length, uint64_t *type);
	void cleanup(void);
};
