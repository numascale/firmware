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

const char *Numachip2::ringnames[] = {"XA", "XB", "YA", "YB", "ZA", "ZB"};

uint32_t Numachip2::read32(const reg_t reg) const
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write32(const reg_t reg, const uint32_t val) const
{
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint8_t Numachip2::read8(const reg_t reg) const
{
	return lib::mcfg_read8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write8(const reg_t reg, const uint8_t val) const
{
	lib::mcfg_write8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

// check NC2 position in remote system and readiness
ht_t Numachip2::probe(const sci_t sci)
{
	// read node count
	uint32_t vendev = lib::mcfg_read32(sci, 0, 24 + 0, 0, 0x0);
	assertf(vendev == Opteron::VENDEV_OPTERON, "Expected Opteron at SCI%03x but found 0x%08x", sci, vendev);
	ht_t ht = (lib::mcfg_read32(sci, 0, 24 + 0, 0, 0x60) >> 4) & 7;

	vendev = lib::mcfg_read32(sci, 0, 24 + ht, 0, 0x0);
	assert(vendev == VENDEV_NC2);

	uint32_t remote_sci = lib::mcfg_read32(sci, 0, 24 + ht, 0, SIU_NODEID);
	assertf(remote_sci == sci, "Reading from SCI%03x gives SCI%03x\n", sci, remote_sci);

	uint32_t control = lib::mcfg_read32(sci, 0, 24 + ht, 0, FABRIC_CTRL);
	if (control == (1 << 30))
		return ht;

	return 0;
}

// used for remote card
Numachip2::Numachip2(const sci_t _sci, const ht_t _ht):
  local(0), sci(_sci), ht(_ht), mmiomap(*this), drammap(*this), dramatt(*this), mmioatt(*this)
{
	assert(ht > 0);
	write32(FABRIC_CTRL, 3 << 30);
}

// used for local card
Numachip2::Numachip2(const ht_t _ht):
  local(1), sci(SCI_LOCAL), ht(_ht), mmiomap(*this), drammap(*this), dramatt(*this), mmioatt(*this)
{
	assert(ht > 0);

	uint32_t vendev = read32(VENDEV);
	assert(vendev == VENDEV_NC2);

	spi_master_read(0xffc0, sizeof(card_type), (uint8_t *)card_type);
	spi_master_read(0xfffc, sizeof(uuid), (uint8_t *)uuid);
	printf("NumaChip2 type %s incorporated as HT%d, UUID %08X\n", card_type, ht, uuid);

	selftest();
	dram_init();
	fabric_init();

	write32(TIMEOUT_RESP, TIMEOUT_VAL);
	// 1.31ms transaction timeout
	write32(RMPE_CTRL, (1 << 31) | (2 << 28) | (2 << 26));
}

void Numachip2::set_sci(const sci_t _sci)
{
	write32(SIU_NODEID, _sci);
	sci = _sci;
	fabric_routing();
}
