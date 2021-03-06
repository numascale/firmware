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

#include "numachip.h"
#include "../bootloader.h"
#include "../library/utils.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define MTAG16_SHIFT 5
#define MTAG8_SHIFT 6
#define CTAG_SHIFT 3

bool Numachip2::dram_check(void) const
{
	uint32_t val = read32(MCTR_ECC_STATUS);
	assertf(!(val & (1 << 1)), "Uncorrectable ECC issue occurred on Numachip at on %s", pr_node(config->id));

	if (val & (1 << 0)) {
		warning("Correctable ECC issue occurred on Numachip on %s", pr_node(config->id));
		return 1;
	}

	return 0;
}

void Numachip2::dram_reset(void)
{
	write32(MCTR_PHY_STATUS, 1);
	if (options->debug.mctr)
		printf("<mctr PHY reset>");
}

void Numachip2::dram_init(void)
{
	int i;

	printf("DRAM init: ");
	i2c_master_seq_read(0x50, 0x00, sizeof(spd_eeprom), (uint8_t *)&spd_eeprom);
	ddr3_spd_check(&spd_eeprom);

	const uint32_t density_shift = ((spd_eeprom.density_banks & 0xf) + 25);
	const uint32_t ranks_shift = (spd_eeprom.organization >> 3) & 0x7;
	const uint32_t devices_shift = 4 - (spd_eeprom.organization & 0x7);
	dram_total_shift = density_shift + ranks_shift + devices_shift;
	const uint64_t total = 1ULL << dram_total_shift; // bytes

	printf("%dGB %s %s ", 1 << (dram_total_shift - 30), nc2_ddr3_module_type(spd_eeprom.module_type),
	       spd_eeprom.mpart[0] ? (char *)spd_eeprom.mpart : "unknown");

	bool errors;

	do {
		// wait for phy init done; shared among all ports
		for (i = dram_training_period; i > 0; i--) {
			bool allup = read32(MCTR_PHY_STATUS) & (1 << 24);
			// exit early if all up
			if (allup)
				break;
			cpu_relax();
		}

		// mctr PHY are not up; restart training
		if (i == 0) {
			if (options->debug.mctr)
				printf("<mctr PHY not up %08x %08x>", read32(MCTR_PHY_STATUS), read32(MCTR_PHY_STATUS2));
			errors = 1;
			dram_reset();
			continue;
		}

		errors = 0;

	} while (errors);

	if (options->debug.mctr)
		printf("<mctr PHY up>");

	// test memory
	write32(MCTR_BIST_ADDR, 0);
	if (options->dimmtest) {
		const char baton[] = "-\\|/";
		write32(MCTR_BIST_CTRL, ((options->dimmtest & 0xff) << 16) | ((dram_total_shift - 3) << 3) | (1<<2) | (1<<1) | (1<<0));
		printf("<testing ");
		while (read32(MCTR_BIST_CTRL) & 1) {
			printf("%c\b", baton[i++ % (sizeof(baton) - 1)]);
			lib::udelay(1000000);
		}
		printf("\b>");
		assertf(((read32(MCTR_BIST_CTRL) >> 9) & 3) == 3, "NumaConnect DIMM failure");
	}

	// start zeroing and wait
	write32(MCTR_BIST_CTRL, (1<<12) | ((dram_total_shift - 3) << 3) | (1<<2) | (1<<0));
	printf("<zeroing");
	while (read32(MCTR_BIST_CTRL) & 1)
		cpu_relax();
	printf(">");

	const uint64_t hosttotal = e820->memlimit();
	bool mtag_byte_mode = ((read32(PE_STATUS + 1 * PE_OFFSET) & (1<<31)) != 0);
	unsigned ncache_shift = 30; // minimum
	uint64_t ctag = 1ULL << (ncache_shift - CTAG_SHIFT);
	// round up to mask constraints to allow manipulation
	uint64_t mtag = roundup((hosttotal >> (mtag_byte_mode ? MTAG8_SHIFT : MTAG16_SHIFT)) + 1, 1 << 19);

	// check if insufficient MTag
	if (mtag > (total - (1ULL << ncache_shift) - ctag)) {
		// round down to mask constraint
		mtag = (total - (1ULL << ncache_shift) - ctag) & ~((1 << 19) - 1);
	} else {
		// check if nCache can use more space
		while ((1ULL << ncache_shift) + ctag + mtag <= total) {
			ncache_shift++;
			ctag = 1ULL << (ncache_shift - CTAG_SHIFT);
		}

		// too large, use next size down
		ncache_shift--;
		ctag = 1ULL << (ncache_shift - CTAG_SHIFT);
	}

	uint64_t mtag_base = (1ULL << ncache_shift) + ctag;
	uint64_t mtag_mask = roundup_pow2(mtag, 1 << 19);
	options->memlimit = (total - mtag_base) << (mtag_byte_mode ? MTAG8_SHIFT : MTAG16_SHIFT);

	// nCache, then CTag, then MTag
	write32(NCACHE_CTRL, (ncache_shift - 30) << 3);
	write32(CTAG_BASE + TAG_ADDR_MASK, (1 << (ncache_shift - 30)) - 1);
	write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);     // no tag comparison mask for MTag
	write32(CTAG_BASE + TAG_MCTR_OFFSET, 1ULL << (ncache_shift - 19));
	write32(CTAG_BASE + TAG_MCTR_MASK, (ctag >> 19) - 1);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, mtag_base >> 19);
	write32(MTAG_BASE + TAG_MCTR_MASK, (mtag_mask >> 19) - 1);

	printf("%dMB nCache", 1 << (ncache_shift - 20));

#ifdef DEBUG
	printf("CTag TAG_ADDR_MASK   %08x\n", read32(CTAG_BASE + TAG_ADDR_MASK));
	printf("MTag TAG_ADDR_MASK   %08x\n", read32(MTAG_BASE + TAG_ADDR_MASK));
	printf("CTag TAG_MCTR_OFFSET %08x\n", read32(CTAG_BASE + TAG_MCTR_OFFSET));
	printf("CTag TAG_MCTR_MASK   %08x\n", read32(CTAG_BASE + TAG_MCTR_MASK));
	printf("MTag TAG_MCTR_OFFSET %08x\n", read32(MTAG_BASE + TAG_MCTR_OFFSET));
	printf("MTag TAG_MCTR_MASK   %08x\n", read32(MTAG_BASE + TAG_MCTR_MASK));
#endif
	dram_check();
	printf("\n");
}
