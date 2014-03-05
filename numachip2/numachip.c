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

#include <stdio.h>
#include <string.h>

#include "numachip.h"
#include "../library/access.h"
#include "../bootloader.h"

uint32_t Numachip2::read32(const reg_t reg)
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write32(const reg_t reg, const uint32_t val)
{
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint8_t Numachip2::read8(const reg_t reg)
{
	return lib::mcfg_read8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write8(const reg_t reg, const uint8_t val)
{
	lib::mcfg_write8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Numachip2::routing_init(void)
{
	printf("Initialising XBar routing");
#ifdef TEST
	for (int chunk = 0; chunk < 16; chunk++) {
		write32(SIU_XBAR_CHUNK, chunk);
		const int port = 0; /* Self */
		for (int entry = 0; entry < 0x40; entry++) {
			write32(SIU_XBAR_LOW,  (port >> 0) & 1);
			write32(SIU_XBAR_MID,  (port >> 1) & 1);
			write32(SIU_XBAR_HIGH, (port >> 2) & 1);
		}
	}
#endif
	switch (sci) {
	case 0x000:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 2);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 2);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 2);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	case 0x001:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 1);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 1);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 1);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	default:
		fatal("unexpected");
	}

	printf("\n");
}

// check NC2 position in remote system and readiness
ht_t Numachip2::probe(const sci_t sci)
{
	// read node count
	uint32_t vendev = lib::mcfg_read32(sci, 0, 24 + 0, 0, 0x0);
	assert(vendev == 0x12001022);
	ht_t ht = (lib::mcfg_read32(sci, 0, 24 + 0, 0, 0x60) >> 4) & 7;

	vendev = lib::mcfg_read32(sci, 0, 24 + ht, 0, 0x0);
	assert(vendev == VENDEV);

	uint32_t control = lib::mcfg_read32(sci, 0, 24 + ht, 0, FABRIC_CONTROL);
	if (control & (1 << 31))
		return ht;

	return 0;
}

// used for remote card
Numachip2::Numachip2(const sci_t _sci, const ht_t _ht):
  mmiomap(*this), drammap(*this), sci(_sci), ht(_ht)
{
	write32(FABRIC_CONTROL, 3 << 30);
}

// used for local card
Numachip2::Numachip2(const ht_t _ht):
  mmiomap(*this), drammap(*this), sci(0xfff0), ht(_ht)
{
	uint32_t vendev = read32(0x0);
	assert(vendev == VENDEV);

	spi_master_read(0xffc0, sizeof(card_type), (uint8_t *)card_type);
	spi_master_read(0xfffc, sizeof(uuid), (uint8_t *)uuid);
	printf("NumaChip2 type %s incorporated as HT%d, UUID %08X\n", card_type, ht, uuid);

	selftest();

	if (!config->local_node->sync_only)
		fabric_init();
	dram_init();
}

void Numachip2::set_sci(const sci_t _sci)
{
	printf("Setting Numachip to SCI%03x\n", _sci);
	sci = _sci;

	write32(SIU_NODEID, sci);
	routing_init();
}
