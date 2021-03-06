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
#include "../bootloader.h"
#include "sr56x0.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

uint32_t SR56x0::read32(const uint16_t reg)
{
	return lib::mcfg_read32(sci, 0, 0, 0, reg);
}

void SR56x0::write32(const uint16_t reg, const uint32_t val)
{
	lib::mcfg_write32(sci, 0, 0, 0, reg, val);
}

uint32_t SR56x0::nbmiscind_read(const uint8_t reg)
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

uint32_t SR56x0::ioapicind_read(const uint8_t reg)
{
	write32(0xf8, reg);
	return read32(0xfc);
}

void SR56x0::ioapicind_write(const uint8_t reg, const uint32_t val)
{
	write32(0xf8, reg);
	write32(0xfc, val);
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

SR56x0::SR56x0(const sci_t _sci, const bool _local): sci(_sci), local(_local), watchdog_base(NULL)
{
	if (local) {
		uint32_t val = read32(0xc8);
		if (val & (1 << 4))
			warning("Last reboot was due to IOH link failure");

		if (val & (3 << 8))
			warning("HT CRC errors detected during last boot");

		if (htiu_read(0x7e) & (1 << 7))
			warning("Last reboot was due to IOH input pin");

		// ensure sync flood generation isn't enabled
		xassert(!read32(0xd8));
		xassert(!nbmiscind_read(0x77));
		xassert(!nbmiscind_read(0x78));
		xassert(!nbmiscind_read(0x79));
		xassert(!nbmiscind_read(0x7a));
		xassert(!htiu_read(0x3a));
		xassert(!htiu_read(0x3b));

		// disable sync flood detection
		val = htiu_read(0x1d);
		htiu_write(0x1d, val | (1 << 4));

		// enable 52-bit PCIe address generation
		val = read32(0xc8);
		write32(0xc8, val | (1 << 15));

		// SERR_EN: initiate sync flood when PCIe System Error detected in IOH
		val = read32(0x4);
		write32(0x4, val & ~(1 << 8));

		// disable CRC syncflood
		val = read32(0xc8);
		write32(0xc8, val | (1 << 1));
	}
}

void SR56x0::limits(const uint64_t limit)
{
	if (options->debug.maps)
		printf("Setting limits on %03x IOH to 0x%" PRIx64 "\n", sci, limit);
	xassert((limit & ((1ULL << 24) - 1)) == (1ULL << 24) - 1);

	// limit to HyperTransport range
	uint64_t l_limit = min(Opteron::HT_BASE, limit);
	htiu_write(HTIU_TOM2LO, (l_limit & 0xff800000) | 1);
	htiu_write(HTIU_TOM2HI, l_limit >> 32);

	// if more than 1TByte we need to set TOM3 correctly
	if (limit > 1ULL << 40)
		nbmiscind_write(MISC_TOM3, ((limit << 22) - 1) | (1 << 31));
	else
		nbmiscind_write(MISC_TOM3, 0);
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

void SR56x0::watchdog_write(const uint8_t reg, const uint32_t val)
{
	xassert(watchdog_base);
	watchdog_base[reg] = val;
}

void SR56x0::watchdog_run(const unsigned centisecs)
{
	watchdog_write(0, 0x81); // WatchDogRunStopB | WatchDogTrigger
	watchdog_write(1, centisecs);
	watchdog_write(0, 0x81);
}

void SR56x0::watchdog_stop(void)
{
	watchdog_write(0, 0);
}

void SR56x0::watchdog_setup(void)
{
	// enable watchdog timer
	uint32_t val = lib::pmio_read8(0x69);
	val &= ~1;
	lib::pmio_write8(0x69, val);

	// enable watchdog decode
	uint32_t val2 = lib::mcfg_read32(sci, 0, 20, 0, 0x41);
	lib::mcfg_write32(sci, 0, 20, 0, 0x41, val2 | (1 << 3));

	// write watchdog base address
	watchdog_base = (uint32_t *)WATCHDOG_BASE;
	lib::pmio_write32(0x6c, (unsigned int)watchdog_base);
}
