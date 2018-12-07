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
#include "lc.h"
#include "spi.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../platform/ipmi.h"
#include "../platform/options.h"
#include "../platform/config.h"
#include "../bootloader.h"

uint32_t Numachip2::read64(const sci_t sci, const ht_t ht, const reg_t reg)
{
	return lib::mcfg_read64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

uint64_t Numachip2::read64(const reg_t reg) const
{
	return lib::mcfg_read64(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write64_split(const reg_t reg, const uint64_t val) const
{
	lib::mcfg_write64_split(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint32_t Numachip2::read32(const sci_t sci, const ht_t ht, const reg_t reg)
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

uint32_t Numachip2::read32(const reg_t reg) const
{
	return lib::mcfg_read32(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write32(const sci_t sci, const ht_t ht, const reg_t reg, const uint32_t val)
{
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Numachip2::write32(const reg_t reg, const uint32_t val) const
{
	lib::mcfg_write32(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint16_t Numachip2::read16(const reg_t reg) const
{
	return lib::mcfg_read16(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write16(const reg_t reg, const uint16_t val) const
{
	lib::mcfg_write16(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

uint8_t Numachip2::read8(const reg_t reg) const
{
	return lib::mcfg_read8(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Numachip2::write8(const reg_t reg, const uint8_t val) const
{
	lib::mcfg_write8(config->id, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
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

	// read timeout
	if (vendev == 0xffffffff) {
		printf("<%03x timeout>", sci);
		return 0;
	}

	if (vendev != Opteron::VENDEV_FAM10H &&
	    vendev != Opteron::VENDEV_FAM15H)
		fatal("Expected Opteron at %03x but found 0x%08x", sci, vendev);

	uint8_t val = (lib::mcfg_read32(sci, 0, 24 + 0, Opteron::HT_NODE_ID >> 12, Opteron::HT_NODE_ID & 0xfff) >> 4) & 7;

	vendev = lib::mcfg_read32(sci, 0, 24 + val, VENDEV >> 12, VENDEV & 0xfff);
	assertf(vendev == VENDEV_NC2, "Expected Numachip2 at %03x#%d but found 0x%08x", sci, val, vendev);

	uint32_t remote_sci = lib::mcfg_read32(sci, 0, 24 + val, SIU_NODEID >> 12, SIU_NODEID & 0xfff) & ~(1<<31); // mask out bit31 which is CRC enable bit
	assertf(remote_sci == sci, "Reading from %03x gives %03x\n", sci, remote_sci);

	return val;
}

ht_t Numachip2::probe_slave(const sci_t sci)
{
	ht_t ht = probe(sci);

	if (ht) {
		uint32_t control = lib::mcfg_read32(sci, 0, 24 + ht, INFO >> 12, INFO & 0xfff);
		assertf(!(control & ~(1 << 29)), "Unexpected control value on %03x of 0x%08x", sci, control);
		if (control == 1 << 29) {
			printf("Found %03x", sci);
			// tell slave to proceed
			lib::mcfg_write32(sci, 0, 24 + ht, INFO >> 12, INFO & 0xfff, 3 << 29);
			return ht;
		}
	}

	return 0;
}

void Numachip2::late_init(void)
{
	dramatt.init();
	mmioatt.init();

	dram_init();
	fabric_init();
	pe_init();

	lib::mem_write64(PIU_TIMER_RESET, 0);

	prev_tval = lib::mem_read64(PIU_TIMER_NOW);

	write32(TIMEOUT_RESP, TIMEOUT_VAL);

	for (unsigned pe = 0; pe < PE_UNITS; pe++)
		write32(PE_CTRL + pe * PE_OFFSET, (1 << 31) | (0 << 28) | (3 << 26)); // 335ms timeout
}

void Numachip2::finished(void)
{
	for (unsigned pe = 0; pe < PE_UNITS; pe++)
		write32(PE_CTRL + pe * PE_OFFSET, 1 << 31); // disable timeout
}

uint32_t Numachip2::rom_read(const uint8_t reg)
{
	write32(IMG_PROP_ADDR, reg);
	return read32(IMG_PROP_DATA);
}

bool Numachip2::check(const sci_t sci, const ht_t ht)
{
	unsigned errors = 0;

	// ensure the correct ID Numachip responds
	uint32_t val = read32(sci, ht, SIU_NODEID) & 0xfff;
	if (val != sci) {
		printf("%03x SCI ID %03x\n", sci, val);
		errors++;
	}

	val = read32(sci, ht, HT_LINK_STAT) & ~1;
	if (val) {
		printf("%03x HT link status %08x\n", sci, val);
		errors++;
	}

	val = read32(sci, ht, MTAG_BASE) & 0x8000000;
	if (val) {
		printf("%03x MTAG base %08x\n", sci, val);
		errors++;
	}

	val = read32(sci, ht, CTAG_BASE) & 0x8000000;
	if (val) {
		printf("%03x CTAG base %08x\n", sci, val);
		errors++;
	}

	val = read32(sci, ht, NCACHE_CTRL) & ~0x00000018;
	if (val) {
		printf("%03x NCACHE_CTRL %08x\n", sci, val);
		errors++;
	}

	val = read32(sci, ht, SIU_EVENTSTAT);
	if (val) {
		printf("%03x SIU_EVENTSTAT %08x\n", sci, val);
		errors++;
	}

	for (unsigned pe = 0; pe < PE_UNITS; pe++) {
		val = read32(sci, ht, PE_STATUS + pe * PE_OFFSET) & ~0x800f0000;
		if (val) {
			printf("%03x PE %u status %08x\n", sci, pe, val);
			errors++;
		}

		for (unsigned mcms = 0; mcms < PE_MCMS_UNITS; mcms++) {
			val = read32(sci, ht, PE_MCMS_STAT + pe * PE_OFFSET + mcms * PE_MCMS_OFFSET);
			if (val & (1 << 14)) {
				printf("%03x PE %u MCMS %u status %08x\n", sci, pe, mcms, val);
				errors++;
			}
		}
	}

	for (unsigned link = 0; link < LC5::LINKS; link++) {
		val = read32(sci, ht, LC5::LINKSTAT + link * LC5::SIZE) & ~0x80000000;
		if (val) {
			printf("%03x LC5 %u link status %08x\n", sci, link, val);
			errors++;
		}

		val = read32(sci, ht, LC5::EVENTSTAT);
		if (val) {
			printf("%03x LC5 %u event status %08x\n", sci, link, val);
			errors++;
		}

		val = read32(sci, ht, LC5::ERRORCNT);
		if (val) {
			printf("%03x LC5 %u error count %08x\n", sci, link, val);
			errors++;
		}
	}

	val = read32(sci, ht, MCTR_ECC_STATUS);
	if (val) {
		printf("%03x DRAM status %08x\n", sci, val);
		errors++;
	}

	return errors > 0;
}

bool Numachip2::check(void) const
{
	return check(config->id, ht);
}

void Numachip2::update_board_info(void)
{
	char sernostr[50] = {0};
	int idx = 0;

	if ((board_info.part_no[0] == 'N') && (board_info.part_no[1] == '3')) {
		sprintf(sernostr, "%s%s%c%c%c%s", board_info.part_no, board_info.pcb_type, board_info.pcb_rev, board_info.eco_level, board_info.model, board_info.serial_no);
		printf("The serial number is %s.\nPlease press CR for no change, or enter new serial number: ", sernostr);
	} else
		printf("Please enter the serial number: ");

	sernostr[idx] = '\0';

	while (!sernostr[idx]) {
		if (fgets(&sernostr[idx], 2, stdin) != NULL) {
			// Handle backspace. Remove previous char from string and screen
			if (sernostr[idx] == '\b' || sernostr[idx] == 0x7f) {
				if (idx > 0) {
					printf("\b \b");
					sernostr[--idx]='\0';
				} else
					sernostr[idx]='\0';
			} else if (sernostr[idx] != '\r') {
				// All other chars except CR, add to string. If CR continue (break out of while loop since serno[idx]!='\0'
				if (idx < 16)
					printf("%c", sernostr[idx++]);
				sernostr[idx]='\0';
			}
		}
	}
	printf("\n");
	sernostr[idx] = '\0';
	sernostr[16]  = '\0';

	if (idx != 0) {
		strcpy(board_info.serial_no, &sernostr[10]);
		board_info.model = sernostr[9];
		board_info.eco_level = sernostr[8];
		board_info.pcb_rev = sernostr[7];
		sernostr[7] = '\0';
		strcpy(board_info.pcb_type, &sernostr[4]);
		sernostr[4] = '\0';
		strcpy(board_info.part_no, &sernostr[0]);
		spi_write(SPI_BOARD_INFO_BASE, sizeof(board_info), (unsigned char *)&board_info);
	}
}

Numachip2::Numachip2(const Config::node *_config, const ht_t _ht, const bool _local, const sci_t master_id):
  local(_local), config(_config), ht(_ht), mmiomap(*this), drammap(*this), dramatt(*this), mmioatt(*this)
{
	xassert(ht);

	if (!local) {
		while (read32(INFO) != 7U << 29)
			cpu_relax();

		fabric_init();
		return;
	}

#ifndef SIM
	uint32_t vendev = read32(VENDEV);
	xassert(vendev == VENDEV_NC2);
#endif
	const bool is_stratixv = !!(read32(LINK_FREQ_REV) & 0xffc00000);
	assertf(is_stratixv, "FPGA platform not supported");

	if (options->test_boardinfo)
		update_board_info();

	spi_read(SPI_BOARD_INFO_BASE, sizeof(board_info), (unsigned char *)&board_info);

	printf("NumaConnect2 ");
	if ((board_info.part_no[0] == 'N') && (board_info.part_no[1] == '3')) {
		printf("%s%s%c%c%c%s",
		       board_info.part_no, board_info.pcb_type, board_info.pcb_rev,
		       board_info.eco_level, board_info.model, board_info.serial_no);
	} else
		printf("(board info not available)");

	printf(" at HT%u\n", ht);

	write32(IMG_PROP_TEMP, 1 << 31);
	int fpga_temp = (read32(IMG_PROP_TEMP) & 0xff) - 128;

	uint16_t spd_temp;
	i2c_master_seq_read(0x18, 0x05, sizeof(spd_temp), (uint8_t *)&spd_temp);
	int dimm_temp = ((spd_temp >> 8 | (spd_temp & 0xff) << 8) & 0x1fff) >> 4;

	printf("- core @ %2dC, DIMM @ %2dC\n", fpga_temp, dimm_temp);

	struct spi_image_info image_info;
	spi_read(SPI_IMAGE_INFO_BASE, sizeof(image_info), (unsigned char *)&image_info);

	// date string is stored last byte first, so we read dwords backwards and byte-swap them
	char buildtime[17];
	const unsigned buildlen = ((sizeof(buildtime) - 1) / sizeof(uint32_t));
	for (unsigned i = 0; i < buildlen; i++)
		*(uint32_t *)(buildtime + i * 4) = lib::bswap32(rom_read(IMG_PROP_STRING + buildlen - 1 - i));
	buildtime[sizeof(buildtime) - 1] = '\0'; // terminate

	printf("- image %s, checksum %u, built %s\n", image_info.name, image_info.checksum, buildtime);

	assertf(fpga_temp <= 80, "Chip overtemperature; check heatsink is correctly mounted, fan rotates and the server fans are all installed and operational");

	// Higher temperatures can cause state loss
	if (fpga_temp >= 65)
		warning("Please ensure all the server fans are installed and operational");

	if (options->debug.access) {
		printf("Testing CSR response");
		for (unsigned i = 0x0000; i < 0x4000; i += 4)
			read32(i);
		printf("\n");
	}

	if (options->flash) { // flashing supported on Altera only
		size_t len = 0;
		uint8_t *buf = (uint8_t *)os->read_file(options->flash, &len);
		assertf(buf && len > 0, "Image %s not found or permission issues", options->flash);

		uint32_t checksum = lib::checksum((unsigned char *)buf, len);
		if (image_info.checksum != checksum || (read32(FLASH_REG0) >> 28) != 0xa) {
			printf("Flashing %zuMB image %s with checksum %u\n", len >> 20, options->flash, checksum);
			flash(buf, len);

			// store filename for printing
			memset(&image_info, 0, sizeof(image_info));

			// drop file extension
			char *suffix = strrchr(options->flash, '.');
			if (suffix)
				*suffix = '\0';

			strncpy(image_info.name, options->flash, sizeof(image_info.name));
			image_info.checksum = checksum;
			spi_write(SPI_IMAGE_INFO_BASE, sizeof(image_info), (unsigned char *)&image_info);

			printf("Power cycling");
			ipmi->powercycle();
		} else
			warning("Image already loaded");
	}

	if (read32(FLASH_REG0) >> 28 != 0xa) {
		warning("Non-application image detected; forcing init-only option");
		options->init_only = 1;
	}

	// set local SIU ID and ensure config cycles are routed
	write32(SIU_NODEID, config->id);
	write32(HT_INIT_CTRL, 0);

	// set master SCI ID for PCI IO and CF8 config routing
	write32(PIU_PCIIO_NODE, master_id | ((uint32_t)master_id << 16) | (::config->local_node->master << 31));
}
