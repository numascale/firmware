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

void Numachip2::dram_check(void)
{
	uint32_t val = read32(NCACHE_CTRL);
	if (val & (3 << 7))
		warning("NumaChip DRAM issue 0x%08x on %03x", val, sci);
}

void Numachip2::dram_test(void)
{
	write32(NCACHE_CTRL, 3 << 9);

	printf("Writing:");
	write32(NCACHE_MCTR_ADDR, 0);

	for (uint64_t qw = 0; qw < (1ULL << (dram_total_shift - 3)); qw++) {
		if (!(qw % 0x100000))
			printf(" %llu", qw >> (20 - 3));

		write64_split(NCACHE_MCTR_DATA, lib::hash64(qw));
	}
	printf("\n");

	printf("Readback:");
	write32(NCACHE_MCTR_ADDR, 0);

	for (uint64_t qw = 0; qw < (1ULL << (dram_total_shift - 3)); qw++) {
		if (!(qw % 0x100000))
			printf(" %llu", qw >> (20 - 3));

		xassert(read64(NCACHE_MCTR_DATA) == lib::hash64(qw));
	}

	write32(NCACHE_CTRL, 0);
	printf("\n");
}

void Numachip2::dram_clear(void)
{
	printf("Clearing:");
	write32(NCACHE_CTRL, 1 << 9);

	// prepare block
	for (unsigned qw = 0; qw < 8; qw++) {
		write32(NCACHE_MCTR_ADDR, qw);
		write64_split(NCACHE_MCTR_DATA, 0);
	}

	for (uint64_t blk = 0; blk < (1ULL << (dram_total_shift - 6)); blk++) {
		write32(NCACHE_MCTR_ADDR, (blk << (6 - 3)) | 7);
		write32(NCACHE_MCTR_DATA + 4, 0);
	}

	write32(NCACHE_CTRL, 0);
	printf("\n");
}

void Numachip2::dram_verify(void)
{
	printf("Verifying:");
	write32(NCACHE_CTRL, 3 << 9);
	write32(NCACHE_MCTR_ADDR, 0);

	for (uint64_t qw = 0; qw < (1ULL << (dram_total_shift - 3)); qw++) {
		if (!(qw % 0x100000))
			printf(" %llu", qw >> (20 - 3));

		uint64_t val = read64(NCACHE_MCTR_DATA);
		assertf(!val, "Address 0x%llx contains 0x%llx", qw, val);
	}

	write32(NCACHE_CTRL, 0);
	printf("\n");
}

void Numachip2::dram_init(void)
{
	printf("DRAM init: ");
	i2c_master_seq_read(0x50, 0x00, sizeof(spd_eeprom), (uint8_t *)&spd_eeprom);
	ddr3_spd_check(&spd_eeprom);

	const uint32_t density_shift = ((spd_eeprom.density_banks & 0xf) + 25);
	const uint32_t ranks_shift = (spd_eeprom.organization >> 3) & 0x7;
	const uint32_t devices_shift = 4 - (spd_eeprom.organization & 0x7);
	dram_total_shift = density_shift + ranks_shift + devices_shift;
	const uint64_t total = 1ULL << dram_total_shift; // bytes
	const uint64_t hosttotal = e820->memlimit();

	printf("%uGB DIMM", 1 << (dram_total_shift - 30));

	// wait for phy init done; shared among all ports
	while (!(read32(MTAG_BASE + TAG_CTRL) & (1 << 6)))
		cpu_relax();

	write32(MTAG_BASE + TAG_CTRL, 0);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, 0);
	write32(MTAG_BASE + TAG_MCTR_MASK, ~0);
	write32(MTAG_BASE + TAG_CTRL, ((dram_total_shift - 27) << 3) | 1);

	// wait for memory init done
	while (!(read32(MTAG_BASE + TAG_CTRL) & (1 << 1)))
		cpu_relax();

	write32(MTAG_BASE + TAG_CTRL, 0);

	switch (total) {
	case 4ULL << 30:
		// 0-1024MB nCache; 1024-1152MB CTag; 1152-2048MB unused; 2048-4096MB MTag
		write32(CTAG_BASE + TAG_ADDR_MASK, 0);
		write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);
		write32(CTAG_BASE + TAG_MCTR_OFFSET, 0x800);
		write32(CTAG_BASE + TAG_MCTR_MASK, 0xff);
		write32(MTAG_BASE + TAG_MCTR_OFFSET, 0x1000);
		write32(MTAG_BASE + TAG_MCTR_MASK, 0xfff);
		write32(NCACHE_MCTR_OFFSET, 0);
		write32(NCACHE_MCTR_MASK, 0x7ff);
		break;
	case 8ULL << 30:
		// 0-2048MB nCache; 2048-2304MB CTag; 2304-4096MB unused; 4096-8192MB MTag
		write32(CTAG_BASE + TAG_ADDR_MASK, 1);
		write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);
		write32(CTAG_BASE + TAG_MCTR_OFFSET, 0x1000);
		write32(CTAG_BASE + TAG_MCTR_MASK, 0x1ff);
		write32(MTAG_BASE + TAG_MCTR_OFFSET, 0x2000);
		write32(MTAG_BASE + TAG_MCTR_MASK, 0x1fff);
		write32(NCACHE_MCTR_OFFSET, 0);
		write32(NCACHE_MCTR_MASK, 0xfff);
		break;
	default:
		error("Unexpected Numachip2 DIMM size of %lluMB", total);
	}

	uint32_t val = read32(MTAG_BASE + TAG_CTRL);
	assertf(!(val & (1 << 8)), "Uncorrectable ECC errors detected on NumaConnect DIMM");
	if (val & (1 << 7))
		warning("Correctable ECC errors detected on NumaConnect DIMM");

#ifdef BROKEN
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
	write32(NCACHE_MCTR_OFFSET, 0 >> 19);
	write32(NCACHE_MCTR_MASK, (ncache - 1) >> 19);
	write32(CTAG_BASE + TAG_ADDR_MASK, (ncache >> 30) - 1);
	write32(MTAG_BASE + TAG_ADDR_MASK, 0x7f);     /* No Tag comparison mask for MTag */
	write32(CTAG_BASE + TAG_MCTR_OFFSET, ncache >> 19);
	write32(CTAG_BASE + TAG_MCTR_MASK, (ctag >> 19) - 1);
	write32(MTAG_BASE + TAG_MCTR_OFFSET, (ncache + ctag) >> 19);
	write32(MTAG_BASE + TAG_MCTR_MASK, (mtag >> 19) - 1);

	xassert(read32(NCACHE_CTRL) & (1 << 6));
	printf("%s %s partitions: %lluMB nCache",
	  lib::pr_size(total), nc2_ddr3_module_type(spd_eeprom.module_type), ncache >> 20);

	for (int port = 0; port < 2; port++)
		write32(MTAG_BASE + port * MCTL_SIZE + TAG_CTRL,
		  ((total_shift - 30) << 3) | 1);

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
#endif

	printf("\n");
}
