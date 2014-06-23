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

#include "../library/base.h"

class SR56x0 {
	static const uint32_t VENDEV_SR5690	   = 0x5a101002;
	static const uint32_t VENDEV_SR5670	   = 0x5a121002;
	static const uint32_t VENDEV_SR5650	   = 0x5a131002;
	static const reg_t HTIU_TOM            = 0x16;
	static const reg_t HTIU_TOM2LO         = 0x30;
	static const reg_t HTIU_TOM2HI         = 0x31;
	static const reg_t MISC_TOM3           = 0x4e;
	const sci_t sci;
	const bool local;

	uint32_t read32(const uint16_t reg);
	void write32(const uint16_t reg, const uint32_t val);
	uint32_t nbmiscind_read(uint8_t reg);
	void nbmiscind_write(const uint8_t reg, const uint32_t val);
	uint32_t htiu_read(const uint8_t reg);
	void htiu_write(const uint8_t reg, const uint32_t val);
public:
	static bool probe(const sci_t sci);
	SR56x0(const sci_t sci, const bool local);
	void smi_disable(void);
	void smi_enable(void);
	void limits(const uint64_t limit);
};
