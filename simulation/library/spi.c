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

#include "../../numachip2/numachip.h"
#include <stdint.h>

void Numachip2::spi_master_enable(void)
{
}

void Numachip2::spi_master_disable(void)
{
}

uint8_t Numachip2::spi_master_read_fifo(void)
{
	return 0;
}

void Numachip2::spi_master_read(const uint16_t addr, const int len, uint8_t *data)
{
}
