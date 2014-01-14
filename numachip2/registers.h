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

#ifndef __REGISTERS_H
#define __REGISTERS_H

/* Function in bits 15:12 */
#define LC_BASE         0x2800
#define LC_SIZE         0x0100
#define LC_LINKSTAT     0x00c4

#define I2C_REG0        0x2040
#define I2C_REG1        0x2044
#define SPI_REG0        0x2048
#define SPI_REG1        0x204c

#define MTAG_BASE       0x2080
#define CTAG_BASE       0x2080
#define TAG_CTRL        0x0000

#define CACHE_CTRL      0x20c0

#define SIU_XBAR_ROUTE  0x2200
#define SIU_XBAR_CHUNK  0x22c0
#define SIU_NODEID      0x22c4
#define SIU_ATT_INDEX   0x2300
#define SIU_ATT_ENTRY   0x2304
#define SIU_STATUS      0x2308

#define HSS_PLLCTL      0x2f00

#endif
