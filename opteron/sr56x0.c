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

#include "../library/access.h"
#include "opteron.h"
#include "sr56x0.h"

uint32_t SR56x0::read32(const uint16_t reg)
{
	return lib::mcfg_read32(sci, 0, 0, 0, reg);
}

void SR56x0::write32(const uint16_t reg, const uint32_t val)
{
	lib::mcfg_write32(sci, 0, 0, 0, reg, val);
}

uint32_t SR56x0::nbmiscind_read(uint8_t reg)
{
	write32(0x60, reg);
	return read32(0x64);
}

void SR56x0::nbmiscind_write(const uint8_t reg, const uint32_t val)
{
	write32(0x60, reg | 0x80);
	write32(0x64, val);
}

uint32_t SR56x0::htiu_read(const uint8_t reg)
{
	write32(0x94, reg);
	return read32(0x98);
}

void SR56x0::htiu_write(const uint8_t reg, const uint32_t val)
{
	write32(0x94, reg | 0x100);
	write32(0x98, val);
}

bool SR56x0::probe(const sci_t sci)
{
	uint32_t vendev = lib::mcfg_read32(sci, 0, 0, 0, 0);
	switch (vendev) {
	case VENDEV_SR5690:
	case VENDEV_SR5670:
	case VENDEV_SR5650:
		return 1;
	}

	return 0;
}

SR56x0::SR56x0(const sci_t _sci, const bool _local): sci(_sci), local(_local)
{
	if (local) {
		// enable 52-bit PCIe address generation
		uint32_t val = read32(0xc8);
		write32(0xc8, val | (1 << 15));

		// SERR_EN: initiate sync flood when PCIe System Error detected in IOH
		val = read32(0x4);
		write32(0x4, val & ~(1 << 8));
	}
}

void SR56x0::limits(uint64_t limit)
{
	printf("Setting limits on %03x IOH to 0x%llx", sci, limit);
	xassert((limit & ((1ULL << 24) - 1)) == (1ULL << 24) - 1);

	// limit to HyperTransport range
	limit = min(Opteron::HT_BASE, limit);
	htiu_write(HTIU_TOM2LO, (limit & 0xff800000) | 1);
	htiu_write(HTIU_TOM2HI, limit >> 32);

	if (limit > 1ULL << 40)
		nbmiscind_write(MISC_TOM3, ((limit << 22) - 1) | (1 << 31));
	else
		nbmiscind_write(MISC_TOM3, 0);
	printf("\n");
}

void SR56x0::smi_disable(void)
{
	const uint8_t val = lib::pmio_read8(0x53);
	lib::pmio_write8(0x53, val | (1 << 3));
}

void SR56x0::smi_enable(void)
{
	const uint8_t val = lib::pmio_read8(0x53);
	lib::pmio_write8(0x53, val & ~(1 << 3));
}

void SR56x0::ldtstop(void)
{
	uint8_t val8 = lib::pmio_read8(0x8a);
	lib::pmio_write8(0x8a, 0xf0);
	lib::pmio_write8(0x87, 1);
	lib::pmio_write8(0x8a, val8);
}
