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

uint64_t Numachip2::read64(const reg_t reg) const
{
	xassert(ht);
	return lib::mcfg_read64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write64_split(const reg_t reg, const uint64_t val) const
{
	xassert(ht);
	lib::mcfg_write64_split(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint32_t Numachip2::read32(const reg_t reg) const
{
	xassert(ht);
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write32(const reg_t reg, const uint32_t val) const
{
	xassert(ht);
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint8_t Numachip2::read8(const reg_t reg) const
{
	xassert(ht);
	return lib::mcfg_read8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write8(const reg_t reg, const uint8_t val) const
{
	xassert(ht);
	lib::mcfg_write8(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Numachip2::apic_icr_write(const uint32_t low, const uint32_t apicid)
{
	lib::mem_write32(PIU_APIC_ICR, (apicid << 12) | (low & 0xfff));
}

// check NC2 position in remote system and readiness
ht_t Numachip2::probe(const sci_t sci)
{
	// read node count
	uint32_t vendev = lib::mcfg_read32(sci, 0, 24 + 0, VENDEV >> 12, VENDEV & 0xfff);
	if (vendev != Opteron::VENDEV_FAM10H &&
	    vendev != Opteron::VENDEV_FAM15H) {
		local_node->check();
		fatal("Expected Opteron at SCI%03x but found 0x%08x", sci, vendev);
	}

	uint8_t val = (lib::mcfg_read32(sci, 0, 24 + 0, Opteron::HT_NODE_ID >> 12, Opteron::HT_NODE_ID & 0xfff) >> 4) & 7;

	vendev = lib::mcfg_read32(sci, 0, 24 + val, VENDEV >> 12, VENDEV & 0xfff);
	assertf(vendev == VENDEV_NC2, "Expected Numachip2 at SCI%03x#%d but found 0x%08x", sci, val, vendev);

	uint32_t remote_sci = lib::mcfg_read32(sci, 0, 24 + val, SIU_NODEID >> 12, SIU_NODEID & 0xfff);
	assertf(remote_sci == sci, "Reading from SCI%03x gives SCI%03x\n", sci, remote_sci);

	uint32_t control = lib::mcfg_read32(sci, 0, 24 + val, INFO >> 12, INFO & 0xfff);
	assertf(!(control & ~(1 << 29)), "Unexpected control value on SCI%03x of 0x%08x", sci, control);
	if (control == 1 << 29) {
		printf("Found SCI%03x\n", sci);
		// tell slave to proceed
		lib::mcfg_write32(sci, 0, 24 + val, INFO >> 12, INFO & 0xfff, 3 << 29);
		return val;
	}

	return 0;
}

void Numachip2::late_init(void)
{
	dramatt.init();
	mmioatt.init();

	dram_init();
	fabric_init();

//	write32(TIMEOUT_RESP, TIMEOUT_VAL);
//	write32(RMPE_CTRL, (1 << 31) | (0 << 28) | (3 << 26)); // 335ms timeout

	fabric_routing();
}

uint32_t Numachip2::rom_read(const uint8_t reg)
{
	write32(IMG_PROP_ADDR, reg);
	return read32(IMG_PROP_DATA);
}

void Numachip2::check(void) const
{
	fabric_check();
	dram_check();
}

Numachip2::Numachip2(const sci_t _sci, const ht_t _ht, const bool _local, const sci_t _master):
  local(_local), master(_master), sci(_sci), ht(_ht), mmiomap(*this), drammap(*this), dramatt(*this), mmioatt(*this)
{
	xassert(ht);

	if (!local) {
		printf("Waiting for slave to become ready");
		while (read32(INFO) != 7U << 29)
			cpu_relax();
		printf("\n");

		fabric_init();
		return;
	}

#ifndef SIM
	uint32_t vendev = read32(VENDEV);
	xassert(vendev == VENDEV_NC2);
#endif
#ifdef SPI_FUNCTIONAL
	spi_master_read(0xffc0, sizeof(card_type), (uint8_t *)card_type);
	spi_master_read(0xfffc, sizeof(uuid), (uint8_t *)&uuid);
	printf("NumaChip2 type %s incorporated as HT%d, UUID %08X\n", card_type, ht, uuid);
#else
	printf("NumaChip2 [");
	const bool ht3 = !!(read32(LINK_FREQ_REV) & 0xffc00000);
	if (ht3) {
#ifdef UNIMPLEMENTED
		write32(IMG_PROP_TEMP, 1 << 31);
		uint32_t val;
		while (!(val = read32(IMG_PROP_TEMP) & (1 << 8)))
			cpu_relax();

		char buildtime[17];
		for (unsigned i = 0; i < (sizeof(buildtime) - 1) / sizeof(uint32_t); i++)
			*(uint32_t *)(buildtime + i * 4) = rom_read(i + IMG_PROP_STRING);
		buildtime[sizeof(buildtime) - 1] = '\0'; // terminate

		printf("Stratix, %uC, flags 0x%x, built %s, hash %07x", (val & 0xff) - 128, rom_read(IMG_PROP_FLAGS), buildtime, rom_read(IMG_PROP_HASH) >> 8);
#endif
		printf("Stratix");
	} else
		printf("Virtex");
	printf("] assigned HT%u\n", ht);
#endif
	// set local SIU SCI ID
	write32(SIU_NODEID, sci);

	// set master SCI ID for PCI IO routing
	uint32_t val = read32(PIU_PCIIO_NODE) & ~0xfff;
	write32(PIU_PCIIO_NODE, val | master);
}
