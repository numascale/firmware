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

void Numachip2::dram_init(void)
{
	i2c_master_seq_read(0x50, 0x00, sizeof(spd_eeprom), (uint8_t *)&spd_eeprom);
	ddr3_spd_check(&spd_eeprom);

	const uint64_t total = 1 << (spd_eeprom.density_banks - 2 + 30); /* bytes */
	const uint64_t hosttotal = e820->memlimit();

	uint64_t ncache = 1 << 30; /* Minimum */
	uint64_t ctag = ncache >> 3;
	/* Round up to mask constraints to allow manipulation */
	uint64_t mtag = roundup((hosttotal >> 5) + 1, 1 << 19);

	/* Check if insufficient MTag */
	if (mtag > total - ncache - ctag) {
		/* Round down to mask constraint */
		mtag = (total - ncache - ctag) & ~((1 << 19) - 1);
		warning("Limiting local memory from %lluGB to %lluGB", hosttotal >> 30, mtag << (30 - 5));
		if (total < (32ULL << 30)) /* FIXME: check */
			warning("Please use larger NumaConnect adapters for full memory support");
	} else {
		/* Check if nCache can use more space */
		while (ncache + ctag + mtag < total) {
			ncache *= 2;
			ctag = ncache >> 3;
		}
	}

	/* nCache, then CTag, then MTag */
	write32(CTAG_BASE + TAG_ADDR_MASK, (ncache >> 30) - 1);
	write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);     /* No Tag comparison mask for MTag */
	write32(CTAG_BASE + TAG_MCTR_OFFSET, ncache >> 19);
	write32(CTAG_BASE + TAG_MCTR_MASK, (ctag >> 19) - 1);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, (ncache + ctag) >> 19);
	write32(MTAG_BASE + TAG_MCTR_MASK, (mtag >> 19) - 1);

	assert(read32(NCACHE + ) & (1 << 6));
	printf("%lldGB %s partitions: %lluMB nCache",
	  total >> 10, nc2_ddr3_module_type(spd_eeprom.module_type), ncache >> 20);

	for (int port = 0; port < 2; port++)
		write32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL,
		  ((spd_eeprom.density_banks - 2) << 3) | (1 << 2) | 1);

	const char *mctls[] = {"MTag", "CTag"};
	const uint64_t part[] = {mtag >> 20, ctag >> 20};

	/* FIXME: add NCache back in when implemented */
	for (int port = 0; port < 2; port++) {
		printf(" %lluMB %s", part[port], mctls[port]);

		uint32_t val;
		do {
			cpu_relax();
			val = read32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL);
		} while ((val & 0x42) != 0x42);
	}

	printf("\n");
}
