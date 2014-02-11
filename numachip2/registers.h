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

#define MCFG_BASE 0x3f0000000000ULL
#define MCFG_LIM  0x3ffeffffffffULL

#define SIU_ATT_RANGE 2 /* 3 = 47:36, 2 = 43:32, 1 = 39:28, 0 = 35:24 */
#define SIU_ATT_SHIFT (24 + SIU_ATT_RANGE * 4)

/* Function in bits 15:12 */
#define LC_BASE         0x2800
#define LC_SIZE         0x0100
#define LC_LINKSTAT     0x00c4

#define I2C_REG0        0x2040
#define I2C_REG1        0x2044
#define SPI_REG0        0x2048
#define SPI_REG1        0x204c

#define MTAG_BASE       0x2080
#define CTAG_BASE       0x20a0
#define NUMACACHE_BASE  0x20c0
#define MCTL_SIZE       0x20

#define TAG_CTRL        0x00
#define TAG_ADDR_MASK   0x04
#define TAG_MCTR_OFFSET 0x08
#define TAG_MCTR_MASK   0x0c
#define TAG_CPU_ADDR    0x10
#define TAG_CPU_DATA    0x18

#define SIU_XBAR_ROUTE  0x2200
#define SIU_XBAR_CHUNK  0x22c0
#define SIU_NODEID      0x22c4
#define SIU_ATT_INDEX   0x2300
#define SIU_ATT_ENTRY   0x2304
#define SIU_STATUS      0x2308

#define HSS_PLLCTL      0x2f00

#endif
