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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#define SPI_HEADER_BASE       0
#define SPI_BOARD_INFO_BASE   128
#define SPI_LOG_BASE          (16 << 20)
#define SPI_LOG_SIZE          (16 << 20)

struct spi_header {
	char name[64];
	uint32_t flashed; // seconds since epoch
	uint32_t checksum;
};

struct spi_board_info {
	char part_no[8];
	char pcb_type[8];
	char pcb_rev;
	char eco_level;
	char model;
	char serial_no[8];
};
