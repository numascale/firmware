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
#include <stdbool.h>

struct numachip_info {
	char firmware[18];
	unsigned self : 12;
	uint8_t partition;
	unsigned master : 12;
	unsigned next_master : 12; // ie next partition or 0xfff for none
	unsigned next : 12;        // next node in partition
	unsigned hts : 3;          // hypertransport ids
	unsigned cores : 6;
	unsigned ht : 3;           // numachip ht id
	unsigned neigh_ht : 3;
	unsigned neigh_link : 2;
	unsigned linkmask : 6;     // bitmask of links to scan
	bool lc4;                  // else LC5
} __attribute__((packed)) __attribute__((aligned(4)));
