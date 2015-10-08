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

void Numachip2::dram_check(void) const
{
	uint32_t val = read32(MCTR_ECC_STATUS);
	if (val & (1 << 0))
		warning("Correctable ECC issue occurred on Numachip at %03x", sci);

	assertf(!(val & (1 << 1)), "Uncorrectable ECC issue occurred on Numachip at %03x", sci);
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

	printf("%uGB %s %s ", 1 << (dram_total_shift - 30), nc2_ddr3_module_type(spd_eeprom.module_type),
	       spd_eeprom.mpart[0] ? (char *)spd_eeprom.mpart : "unknown");

	// make sure Function3 MCTR CSR is available (it's not on older images)
	assertf(read32(0x3000) == 0x07031b47, "MCTR CSR Block not available");

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

	write32(MTAG_BASE + TAG_CTRL, 0);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, 0);
	write32(MTAG_BASE + TAG_MCTR_MASK, ~0);
	write32(MTAG_BASE + TAG_CTRL, ((dram_total_shift - 27) << 3) | 1);

	// wait for memory init done
	printf("<zeroing");
	while (read32(MTAG_BASE + TAG_CTRL) & 1)
		cpu_relax();
	printf(">");

	switch (total) {
	case 4ULL << 30:
		// 0-1024MB nCache; 1024-1152MB CTag; 1152-2048MB unused; 2048-4096MB MTag
		write32(CTAG_BASE + TAG_ADDR_MASK, 0);
		write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);
		write32(CTAG_BASE + TAG_MCTR_OFFSET, 0x800);
		write32(CTAG_BASE + TAG_MCTR_MASK, 0xff);
		write32(MTAG_BASE + TAG_MCTR_OFFSET, 0x1000);
		write32(MTAG_BASE + TAG_MCTR_MASK, 0xfff);
		write32(NCACHE_CTRL, (0 << 3)); // 1 GByte nCache
		options->memlimit = 64ULL << 30; // Max 64G per node
		break;
	case 8ULL << 30:
		// 0-2048MB nCache; 2048-2304MB CTag; 2304-4096MB unused; 4096-8192MB MTag
		write32(CTAG_BASE + TAG_ADDR_MASK, 1);
		write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);
		write32(CTAG_BASE + TAG_MCTR_OFFSET, 0x1000);
		write32(CTAG_BASE + TAG_MCTR_MASK, 0x1ff);
		write32(MTAG_BASE + TAG_MCTR_OFFSET, 0x2000);
		write32(MTAG_BASE + TAG_MCTR_MASK, 0x1fff);
		write32(NCACHE_CTRL, (1 << 3)); // 2 GByte nCache
		options->memlimit = 128ULL << 30; // Max 128G per node
		break;
	default:
		error("Unexpected Numachip2 DIMM size of %"PRIu64"MB", total);
	}

	dram_check();

#ifdef BROKEN
	const uint64_t hosttotal = e820->memlimit();
	uint64_t ncache = 1ULL << 30; /* Minimum */
	uint64_t ctag = ncache >> 3;
	/* Round up to mask constraints to allow manipulation */
	uint64_t mtag = roundup((hosttotal >> 5) + 1, 1 << 19);

	// check if insufficient MTag
	if (mtag > (total - ncache - ctag)) {
		// round down to mask constraint
		mtag = (total - ncache - ctag) & ~((1 << 19) - 1);
		warning("Limiting local memory from %s to %s", lib::pr_size(hosttotal), lib::pr_size(mtag >> 5));
		if (total < (32ULL << 30)) /* FIXME: check */
			warning("Please use NumaConnect2 adapters supporting more server memory");
	} else {
		// check if nCache can use more space
		while (ncache + ctag + mtag <= total) {
			ncache *= 2;
			ctag = ncache >> 3;
		}

		// too large, use next size down
		ncache /= 2;
		ctag = ncache >> 3;
	}

	/* nCache, then CTag, then MTag */
	write32(CTAG_BASE + TAG_ADDR_MASK, (ncache >> 30) - 1);
	write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);     /* No Tag comparison mask for MTag */
	write32(CTAG_BASE + TAG_MCTR_OFFSET, ncache >> 19);
	write32(CTAG_BASE + TAG_MCTR_MASK, (ctag >> 19) - 1);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, (ncache + ctag) >> 19);
	write32(MTAG_BASE + TAG_MCTR_MASK, (mtag >> 19) - 1);

	xassert(read32(NCACHE_CTRL) & (1 << 6));
	printf("%s %s partitions: %"PRIu64"MB nCache",
	  lib::pr_size(total), nc2_ddr3_module_type(spd_eeprom.module_type), ncache >> 20);

	for (int port = 0; port < 2; port++)
		write32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL,
		  ((total_shift - 30) << 3) | 1);

	const char *mctls[] = {"MTag", "CTag"};
	const uint64_t part[] = {mtag >> 20, ctag >> 20};


	/* FIXME: add NCache back in when implemented */
	for (int port = 0; port < 2; port++) {
		printf(" %"PRIu64"MB %s", part[port], mctls[port]);

		uint32_t val;
		do {
			cpu_relax();
			val = read32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL);
		} while ((val & 0x42) != 0x42);
	}
#endif

	printf("\n");
}
