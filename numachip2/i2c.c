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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../library/base.h"
#include "../library/access.h"
#include "../bootloader.h"

#define I2C_MASTER_CR_START (1 << 7)
#define I2C_MASTER_CR_STOP  (1 << 6)
#define I2C_MASTER_CR_READ  (1 << 5)
#define I2C_MASTER_CR_WRITE (1 << 4)
#define I2C_MASTER_CR_NACK  (1 << 3)
#define I2C_MASTER_CR_IACK  (1 << 0)

#define I2C_MASTER_SR_RXACK (1 << 7)
#define I2C_MASTER_SR_BUSY  (1 << 6)
#define I2C_MASTER_SR_AL    (1 << 5)
#define I2C_MASTER_SR_TIP   (1 << 1)
#define I2C_MASTER_SR_IRQ   (1 << 0)

void Numachip2::i2c_master_init(void)
{
	const uint16_t prescale_cnt = (read32(FLASH_REG0) == 0) ? 174 : 349; /* For 100MHz coreclk: 174 = 100 kHz, 43 = 400 kHz
									      * For 200MHz coreclk: 349 = 100 kHz, 86 = 400 kHz */
	write16(I2C_REG0, prescale_cnt);
	write8(I2C_REG0 + 2, 0x80); /* core_en */
}

uint8_t Numachip2::i2c_master_irqwait(void)
{
	// wait for completion
	for (unsigned i = i2c_timeout; i; i--) {
		uint8_t val = read8(I2C_REG1);
		if (val & I2C_MASTER_SR_IRQ)
			return val;

		cpu_relax();
	}

	fatal("Timeout waiting for I2C completion");
}

void Numachip2::i2c_master_busywait(void)
{
	// wait for busy to de-assert
	for (unsigned i = i2c_timeout; i; i--) {
		uint8_t val = read8(I2C_REG1);
		if (!(val & I2C_MASTER_SR_BUSY))
			return;

		cpu_relax();
	}

	fatal("Timeout waiting for I2C busy deassertion");
}

void Numachip2::i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const unsigned len, uint8_t *data)
{
	uint8_t val;

	/* If I2C master isn't initialized yet, do so now */
	if (read8(I2C_REG0 + 2) != 0x80)
		i2c_master_init();

	if (read8(I2C_REG1) & I2C_MASTER_SR_BUSY)
		fatal("Last I2C master transaction did not complete!");

	/* Write device addr + rw bit (0=write) in transmit register */
	write8(I2C_REG0 + 3, (device_adr << 1) | 0);

	/* Send start-condition + device addr + rw bit */
	write8(I2C_REG1, I2C_MASTER_CR_START | I2C_MASTER_CR_WRITE | I2C_MASTER_CR_NACK | I2C_MASTER_CR_IACK);
	val = i2c_master_irqwait();
	if (val & I2C_MASTER_SR_RXACK)
		fatal("Got I2C NACK on device address (%02X) : %04X %04X",
		      device_adr, read32(I2C_REG0), read32(I2C_REG1));

	/* Write start byte address in transmit register */
	write8(I2C_REG0 + 3, byte_addr);

	/* Send start byte address */
	write8(I2C_REG1, I2C_MASTER_CR_WRITE | I2C_MASTER_CR_NACK | I2C_MASTER_CR_IACK);
	val = i2c_master_irqwait();
	if (val & I2C_MASTER_SR_RXACK)
		fatal("Got I2C NACK on byte address (%02X) : %04X %04X",
		      byte_addr, read32(I2C_REG0), read32(I2C_REG1));

	/* Write device addr + rw bit (1=read) in transmit register */
	write8(I2C_REG0 + 3, (device_adr << 1) | 1);

	/* Send repeated-start-condition + device addr + rw bit */
	write8(I2C_REG1, I2C_MASTER_CR_START | I2C_MASTER_CR_WRITE | I2C_MASTER_CR_NACK | I2C_MASTER_CR_IACK);
	val = i2c_master_irqwait();
	if (val & I2C_MASTER_SR_RXACK)
		fatal("Got I2C NACK on device address (repeated start) (%02X) : %04X %04X",
		      device_adr, read32(I2C_REG0), read32(I2C_REG1));

	/* read + ack */
	for (unsigned i = 0; i < len; i++) {
		uint8_t nack = (i == len - 1) ? I2C_MASTER_CR_NACK : 0;
		write8(I2C_REG1, I2C_MASTER_CR_READ | nack | I2C_MASTER_CR_IACK);
		val = i2c_master_irqwait();
		data[i] = read8(I2C_REG0 + 3);
	}

	/* Stop condition */
	write8(I2C_REG1, I2C_MASTER_CR_STOP | I2C_MASTER_CR_NACK | I2C_MASTER_CR_IACK);
	(void)i2c_master_irqwait();

	write8(I2C_REG1, I2C_MASTER_CR_IACK); /* Ack last interrupt */
	i2c_master_busywait();
}
