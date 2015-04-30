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

#pragma once

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

class IPMI
{
	const uint16_t port_data;
	const uint16_t port_cmdstat;
	static const uint8_t status_OBF     = (1 << 0);
	static const uint8_t status_IBF     = (1 << 1);
	static const uint8_t state_mask     = (3 << 6);
	static const uint16_t write_start   = 0x61;
	static const uint16_t write_end     = 0x62;
	static const uint16_t read_byte     = 0x68;
	static const uint8_t state_idle     = (0 << 6);
	static const uint8_t state_read     = (1 << 6);
	static const uint8_t state_write    = (2 << 6);
	static const uint8_t state_error    = (3 << 6);
	static const uint8_t completion_success = 0x00;
	static const uint8_t CMD_RESET_COLD = 2;
	static const uint8_t FN_APP_REQ     = 6;

	void write(const uint8_t *cmd, const size_t len) const;
	uint8_t read(uint8_t *res, const size_t len) const;
public:
	IPMI(const uint16_t port_base);
	void reset_cold(void);
};

extern IPMI *ipmi;
