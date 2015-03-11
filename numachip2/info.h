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
	char firmware_ver[24];
	uint8_t partition; // 0 for observer
	uint16_t fabric_nodes : 12;
	uint16_t part_start : 12;
	uint16_t part_nodes : 12;
	uint8_t ver : 4;
	uint8_t ht : 3;
	uint8_t neigh_ht : 3;
	uint8_t neigh_link : 2;
	uint8_t neigh_sublink : 1;
	bool symmetric : 1;
	bool devices : 1;
} __attribute__((packed)) __attribute__((aligned(4)));
