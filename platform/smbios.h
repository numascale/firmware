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

#ifndef __SMBIOS_H
#define __SMBIOS_H

#include <inttypes.h>

class SMBIOS {
	struct smbios_header {
		uint8_t type;
		uint8_t length;
		uint16_t handle;
		uint8_t *data;
	};

	const char *string(const char *table, uint8_t index);
public:
	const char *biosver, *biosdate;
	const char *sysmanuf, *sysproduct;
	const char *boardmanuf, *boardproduct;

	SMBIOS(void);
};

#endif