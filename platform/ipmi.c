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

#include "ipmi.h"
#include "../library/base.h"
#include "../library/utils.h"

#include <stdio.h>
#include <sys/io.h>

IPMI::IPMI(const uint16_t port_base):
	port_data(port_base), port_cmdstat(port_base + 1)
{
	printf("IPMI KCS interface detected at 0x%x\n", port_data);
}

void IPMI::write(const uint8_t *cmd, const size_t len) const
{
	xassert(len >= 2);
	uint8_t val;

	while ((val = inb(port_cmdstat)) & status_IBF)
		cpu_relax();

	outb(write_start, port_cmdstat);

	while ((val = inb(port_cmdstat)) & status_IBF)
		cpu_relax();

	xassert((val & state_mask) == state_write);

	for (unsigned off = 0; off < (len - 1); off++) {
		outb(cmd[off], port_data);

		while ((val = inb(port_cmdstat)) & status_IBF)
			cpu_relax();

		xassert((val & state_mask) == state_write);
	}

	outb(write_end, port_cmdstat);

	while ((val = inb(port_cmdstat)) & status_IBF)
		cpu_relax();

	xassert((val & state_mask) == state_write);
	outb(cmd[len - 1], port_data);
}

uint8_t IPMI::read(uint8_t *res, const size_t len) const
{
	uint8_t val, done = 0;

	while (1) {
		while ((val = inb(port_cmdstat)) & status_IBF)
			cpu_relax();

		if ((val & state_mask) == state_idle)
			break;
		xassert((val & state_mask) == state_read);

		while (!((val = inb(port_cmdstat)) & status_OBF))
			cpu_relax();

		xassert(done <= len);
		res[done] = inb(port_data);
		done++;

		outb(read_byte, port_data);
	}

	while (!((val = inb(port_cmdstat)) & status_OBF))
		cpu_relax();

	inb(port_data); // dummy read
	return done;
}

void IPMI::reset_cold(void) const
{
	const uint8_t buf[] = {0, 2, 3}; // found by tracing
	write(buf, sizeof(buf));
	lib::udelay(3000000);
	fatal("IPMI cold reset failure");
}

void IPMI::poweroff(void) const
{
	const uint8_t buf[] = {0, 6, 0x85, 0}; // IPMI spec v2 p250
	write(buf, sizeof(buf));
	lib::udelay(3000000);
	fatal("IPMI poweroff failure");
}
