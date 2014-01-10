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

#define NC2_LC_BASE        ((2 << 16) | 0x800)
#define NC2_LC_SIZE        ((2 << 16) | 0x100)
#define NC2_LC_LINKSTAT    ((2 << 16) | 0x0c4)
#define NC2_HSS_PLLCTL     ((2 << 16) | 0xf00)
#define NC2_NODEID         ((2 << 16) | 0x2c4)

#endif
