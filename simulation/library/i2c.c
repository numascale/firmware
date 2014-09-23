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

void Numachip2::i2c_master_init(void)
{
}

uint8_t Numachip2::i2c_master_irqwait(void)
{
	return 0;
}

void Numachip2::i2c_master_busywait(void)
{
}

void Numachip2::i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const unsigned len, uint8_t *data)
{
}
