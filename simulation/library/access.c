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

#include "../../library/base.h"
#include <stdint.h>

namespace lib
{
	void critical_enter(void) {};
	void critical_leave(void) {};

	uint8_t pmio_read8(const uint16_t offset)
	{
		return 0x0;
	}

	void pmio_write8(const uint16_t offset, const uint8_t val)
	{
	}

	uint8_t mem_read8(const uint64_t addr)
	{
		return 0x0;
	}

	uint16_t mem_read16(const uint64_t addr)
	{
		return 0x0;
	}

	uint32_t mem_read32(const uint64_t addr)
	{
		return 0x0;
	}

	uint64_t mem_read64(const uint64_t addr)
	{
		return 0x0;
	}

	void mem_write8(const uint64_t addr, const uint8_t val)
	{
	}

	void mem_write16(const uint64_t addr, const uint16_t val)
	{
	}

	void mem_write32(const uint64_t addr, const uint32_t val)
	{
	}

	void mem_write64(const uint64_t addr, const uint64_t val)
	{
	}

	uint8_t mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	void mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val)
	{
	}

	void mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val)
	{
	}

	void mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
	}

	void mcfg_write64_split(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val)
	{
	}
}
