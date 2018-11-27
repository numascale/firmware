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

void Opteron::tracing_arm(void)
{
	xassert((trace_base & 0xffffff) == 0);
	xassert((trace_limit & 0xffffff) == 0xffffff);

	write32(TRACE_BUF_BASELIM, ((trace_base >> 24) & 0xffff) | (((trace_limit >> 24) & 0xffff) << 16));
	write32(TRACE_BUF_ADDR_HIGH, (trace_base >> 40) | ((trace_limit >> 40) << 8) | ((trace_base >> 40) << 16));
	write32(TRACE_BUF_ADDR, trace_base >> 6);
	uint32_t val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, (val & ~(3 << 16)) | (((trace_base >> 38) & 3) << 16));
}

void Opteron::tracing_start(void)
{
	write32(TRACE_START, 0);
	write32(TRACE_STOP, 1U < 31);

	uint32_t val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val & ~1);
	write32(TRACE_BUF_CTRL, 1 | (0 << 1) | (0 << 4) | (1 << 13) |
	 (val & (3 << 16)) |  (1 << 20) | (0 << 21) | (0 << 23) | (1 << 25));
//	write32(TRACE_STOP, 1 | (1 << 4) | (1 << 8) | (1 << 12) | (1 << 16));
	write32(TRACE_STOP, 1 | (1 << 29)/* | (1 << 4) | (1 << 8) | (1 << 12) | (1 << 16)*/);
	write32(TRACE_CAPTURE, (1U << 31) | (1 << 14) | (0x3f << 24) | (0x3f << 16));

	write32(TRACE_START, 1U << 31);
}

void Opteron::tracing_stop(void)
{
	write32(TRACE_STOP, 1U << 31);

	uint32_t val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val | (1 << 12));
	val = read32(TRACE_BUF_CTRL);
	write32(TRACE_BUF_CTRL, val & ~1);
}

void Opteron::tracing_disable(void)
{
	write32(TRACE_BUF_BASELIM, 0);
	write32(TRACE_BUF_ADDR_HIGH, 0);
	write32(TRACE_BUF_ADDR, 0);
	write32(TRACE_BUF_CTRL, 0);
}
