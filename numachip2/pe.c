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

void Numachip2::pe_load_microcode(const bool lmpe)
{
	unsigned mseq_ucode_length = sizeof(numachip2_mseq_ucode) / sizeof(numachip2_mseq_ucode[0]);
	unsigned mseq_table_length = sizeof(numachip2_mseq_table) / sizeof(numachip2_mseq_table[0]);

	xassert(mseq_ucode_length <= 4096);
	xassert(mseq_table_length <= 256);

	printf("Loading microcode (%u) and jump table (%u) on %s\n", mseq_ucode_length, mseq_table_length, lmpe ? "LMPE" : "RMPE");

	write32(lmpe ? LMPE_SEQ_INDEX : RMPE_SEQ_INDEX, 1 << 31); // enable AutoInc and zero index
	for (unsigned j = 0; j < mseq_ucode_length; j++)
		write32(lmpe ? LMPE_WCS_ENTRY : RMPE_WCS_ENTRY, numachip2_mseq_ucode[j]);

	write32(lmpe ? LMPE_SEQ_INDEX : RMPE_SEQ_INDEX, 1 << 31); // enable AutoInc and zero index
	for (unsigned j = 0; j < mseq_table_length; j++)
		write32(lmpe ? LMPE_JUMP_ENTRY : RMPE_JUMP_ENTRY, numachip2_mseq_table[j]);

	uint32_t val = read32(lmpe ? LMPE_CTRL : RMPE_CTRL);
	write32(lmpe ? LMPE_CTRL : RMPE_CTRL, val | (1 << 31));
}

void Numachip2::pe_init(void)
{
	if (!(read32(RMPE_CTRL) & (1<<31)))
		pe_load_microcode(0);
	else
		printf("RMPE already operational\n");

	if (!(read32(LMPE_CTRL) & (1<<31)))
		pe_load_microcode(1);
	else
		printf("LMPE already operational\n");
}
