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
#include "../bootloader.h"
#include "../library/access.h"

#include "numachip2_mseq.h"

void Numachip2::pe_load_microcode(const unsigned pe)
{
	unsigned mseq_ucode_length = sizeof(numachip2_mseq_ucode) / sizeof(numachip2_mseq_ucode[0]);
	unsigned mseq_table_length = sizeof(numachip2_mseq_table) / sizeof(numachip2_mseq_table[0]);

	xassert(mseq_ucode_length <= 4096);
	xassert(mseq_table_length <= 256);

	write32(PE_SEQ_INDEX + pe * PE_OFFSET, 1 << 31); // enable AutoInc and zero index
	for (unsigned j = 0; j < mseq_ucode_length; j++)
		write32(PE_WCS_ENTRY + pe * PE_OFFSET, numachip2_mseq_ucode[j]);

	write32(PE_SEQ_INDEX + pe * PE_OFFSET, 1 << 31); // enable AutoInc and zero index
	for (unsigned j = 0; j < mseq_table_length; j++)
		write32(PE_JUMP_ENTRY + pe * PE_OFFSET, numachip2_mseq_table[j]);

	uint32_t val = read32(PE_CTRL + pe * PE_OFFSET);
	write32(PE_CTRL + pe * PE_OFFSET, val | (1 << 31));
}

void Numachip2::pe_init(void)
{
	for (unsigned pe = 0; pe < PE_UNITS; pe++) {
		xassert(!(read32(PE_CTRL + pe * PE_OFFSET) & (1<<31)));
		pe_load_microcode(pe);
	}
}
