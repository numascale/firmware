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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "numachip.h"
#include "../bootloader.h"
#include "../library/access.h"
#include "../library/utils.h"

#define SPI_INSTR_WRSR  0x01
#define SPI_INSTR_WRITE 0x02
#define SPI_INSTR_READ  0x03
#define SPI_INSTR_WRDI  0x04
#define SPI_INSTR_RDSR  0x05
#define SPI_INSTR_WREN  0x06

#define SPI_MASTER_CR_SPIE    (1<<7)
#define SPI_MASTER_CR_SPE     (1<<6)
#define SPI_MASTER_CR_CPOL    (1<<4)
#define SPI_MASTER_CR_CPHA    (1<<3)

#define SPI_MASTER_SR_SPIF    (1<<7)
#define SPI_MASTER_SR_WCOL    (1<<6)
#define SPI_MASTER_SR_WFFULL  (1<<3)
#define SPI_MASTER_SR_WFEMPTY (1<<2)
#define SPI_MASTER_SR_RFFULL  (1<<1)
#define SPI_MASTER_SR_RFEMPTY (1<<0)

void Numachip2::spi_master_enable(void)
{
/*
         4'b0000: clkcnt <=  12'h0;   // 2   -- original M68HC11 coding
         4'b0001: clkcnt <=  12'h1;   // 4   -- original M68HC11 coding
         4'b0010: clkcnt <=  12'h3;   // 16  -- original M68HC11 coding
         4'b0011: clkcnt <=  12'hf;   // 32  -- original M68HC11 coding
         4'b0100: clkcnt <=  12'h1f;  // 8
         4'b0101: clkcnt <=  12'h7;   // 64
         4'b0110: clkcnt <=  12'h3f;  // 128
         4'b0111: clkcnt <=  12'h7f;  // 256
         4'b1000: clkcnt <=  12'hff;  // 512
         4'b1001: clkcnt <=  12'h1ff; // 1024
         4'b1010: clkcnt <=  12'h3ff; // 2048
         4'b1011: clkcnt <=  12'h7ff; // 4096
*/
	const uint8_t espr = 6;

	/* Set extended control register first */
	write8(SPI_REG0 + 1, (espr & 0xc) >> 2);

	/* Set clock prescaler, chip enable etc. */
	write8(SPI_REG0, SPI_MASTER_CR_SPE | (espr & 0x3));
}

void Numachip2::spi_master_disable(void)
{
	write16(SPI_REG0, 0);
}

uint8_t Numachip2::spi_master_read_fifo(void)
{
	// wait for read-fifo non-empty
	for (unsigned i = spi_timeout; i; i--) {
		uint8_t val = read8(SPI_REG0 + 2);
		if (!(val & SPI_MASTER_SR_RFEMPTY))
			return read8(SPI_REG1);
		cpu_relax();
	};

	fatal("Timeout waiting for SPI read FIFO to empty");
}

void Numachip2::spi_master_read(const uint16_t addr, const unsigned len, uint8_t *data)
{
	xassert(len > 0);

	/* Enable SPI Core */
	spi_master_enable();

	/* Write SPI Read instruction to the transmit fifo */
	write8(SPI_REG1, SPI_INSTR_READ);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Write SPI Read address byte1 (most significant 8 bit) to the transmit fifo */
	write8(SPI_REG1, (addr >> 8) & 0xff);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Write SPI Read address byte2 (least significant 8 bit) to the transmit fifo */
	write8(SPI_REG1, addr & 0xff);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Read SPI data */
	for (unsigned i = 0; i < len; i++) {
		write8(SPI_REG1, 0); /* Dummy write */
		data[i] = spi_master_read_fifo();
	}

	/* Disable SPI Core */
	spi_master_disable();
}

void Numachip2::spi_master_write(const uint16_t addr, const unsigned len, uint8_t *data)
{
	/* Can only transfer 128 bytes (1 page) at a time and not cross page boundaries */
	xassert(len > 0 && len <= 128);
	xassert((addr & ~0x7f) == ((addr + len) & ~0x7f));

	/* Enable SPI Core */
	spi_master_enable();

	/* Save SPCR value for later */
	uint8_t spcr = read8(SPI_REG0);

	/* Send Write Enable instruction to the transmit fifo */
	write8(SPI_REG1, SPI_INSTR_WREN);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* De-assert Chip Enable */
	write8(SPI_REG0, spcr & ~SPI_MASTER_CR_SPE);

	/* Assert Chip Enable */
	write8(SPI_REG0, spcr | SPI_MASTER_CR_SPE);

	/* Write Read Status Register instruction to the transmit fifo */
	write8(SPI_REG1, SPI_INSTR_RDSR);
	(void)spi_master_read_fifo(); /* Dummy read */

	write8(SPI_REG1, 0); /* Dummy write */
	uint8_t val = spi_master_read_fifo();

	/* De-assert Chip Enable */
	write8(SPI_REG0, spcr & ~SPI_MASTER_CR_SPE);

	assertf(val & 2, "Write Enable Latch did not get set %x", val);

	/* Assert Chip Enable */
	write8(SPI_REG0, spcr | SPI_MASTER_CR_SPE);

	/* Write SPI Write instruction to the transmit fifo */
	write8(SPI_REG1, SPI_INSTR_WRITE);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Write SPI Write address byte1 (most significant 8 bit) to the transmit fifo */
	write8(SPI_REG1, (addr >> 8) & 0xff);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Write SPI Write address byte2 (least significant 8 bit) to the transmit fifo */
	write8(SPI_REG1, addr & 0xff);
	(void)spi_master_read_fifo(); /* Dummy read */

	/* Write SPI data */
	for (unsigned i = 0; i < len; i++) {
		write8(SPI_REG1, data[i]);
		(void)spi_master_read_fifo(); /* Dummy read */
	}

	/* De-assert Chip-Enable to initiate EEPROM write operation */
	write8(SPI_REG0, spcr & ~SPI_MASTER_CR_SPE);

	/* Wait until EEPROM signals Write Completion in the Status Register */
	for (unsigned i = spi_timeout; i; i--) {
		/* Assert Chip Enable */
		write8(SPI_REG0, spcr | SPI_MASTER_CR_SPE);

		/* Write Read Status Register instruction to the transmit fifo */
		write8(SPI_REG1, SPI_INSTR_RDSR);
		(void)spi_master_read_fifo(); /* Dummy read */

		write8(SPI_REG1, 0); /* Dummy write */
		val = spi_master_read_fifo();

		if (!(val & 1)) { /* Check Write In Progress bit */
			/* Disable SPI Core */
			spi_master_disable();
			return;
		}

		/* De-assert Chip Enable */
		write8(SPI_REG0, spcr & ~SPI_MASTER_CR_SPE);
	}

	fatal("WRITE instruction did not complete");
}
