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

#include "opteron.h"

#define TCB 1
#define TCB_SIZE (64 * 8)

void Opteron::tracing_arm(void)
{
	assert((trace_base & 0xffffff) == 0);
	assert((trace_limit & 0xffffff) == 0xffffff);

	write32(TRACE_BUF_BASELIM, ((trace_base >> 24) & 0xffff) | (((trace_limit >> 24) & 0xffff) << 16));
	write32(TRACE_BUF_ADDR_HIGH, (trace_base >> 40) | ((trace_limit >> 40) << 8) | ((trace_base >> 40) << 16));
	write32(TRACE_BUF_ADDR, trace_base >> 6);
}

void Opteron::tracing_start(void)
{
	write32(TRACE_START, 0);
	write32(TRACE_STOP, 1 < 31);

	if (TCB) {
		for (unsigned i = 0; i < TCB_SIZE / 4; i++) {
			write32(ARRAY_ADDR, 0xa0000000 | i);
			write32(ARRAY_DATA, 0);
		}
	}

	tracing_arm();

	uint32_t val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val & ~1);
	write32(TRACE_BUF_CTRL, 1 | (TCB << 1) | (0 << 4) | (1 << 13) | (1 << 20) |
	  (1 << 20) | (0 << 21) | (0 << 23) | (1 << 25));
	write32(TRACE_STOP, 1 | (1 << 4) | (1 << 8) | (1 << 12) | (1 << 16));
	write32(TRACE_CAPTURE, (1 << 31) | (1 << 30) | (0x3f << 24) | (0x3f << 16));

	write32(TRACE_START, 1 << 31);
}

void Opteron::tracing_stop(void)
{
	write32(TRACE_STOP, 1 << 31);

	uint32_t val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val | (1 << 12));
	val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val & ~1);
}
