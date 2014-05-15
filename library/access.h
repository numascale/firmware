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

#include <inttypes.h>
#include <sys/io.h>

#include "base.h"

#define NC_MCFG_BASE 0x3f0000000000ULL
#define NC_MCFG_LIM  ((NC_MCFG_BASE | (0xfffULL << 28)) - 1)

#define cli() { asm volatile("cli"); }
#define sti() { asm volatile("sti"); }

#define cpu_relax() asm volatile("pause" ::: "memory")

#define disable_cache() do { \
    asm volatile( \
	"mov %%cr0, %%eax\n" \
	"or $0x40000000, %%eax\n" \
	"mov %%eax, %%cr0\n" \
	"wbinvd\n" ::: "eax", "memory"); \
	} while (0)

#define enable_cache() do { \
    asm volatile( \
	"mov %%cr0, %%eax\n" \
	"and $~0x40000000, %%eax\n" \
	"mov %%eax, %%cr0\n" ::: "eax", "memory"); \
	} while (0)

#define RTC_SECONDS     0
#define RTC_MINUTES     2
#define RTC_HOURS       4
#define RTC_DAY_OF_WEEK 6
#define RTC_DAY         7
#define RTC_MONTH       8
#define RTC_YEAR        9
#define RTC_SETTINGS    11

namespace lib
{
	static inline uint32_t uint32_tbswap(uint32_t val)
	{
		asm volatile("bswap %0" : "+r"(val));
		return val;
	}

	static inline uint64_t rdmsr(const msr_t msr)
	{
		union {
			uint32_t dw[2];
			uint64_t qw;
		} val;
		asm volatile("rdmsr" : "=d"(val.dw[1]), "=a"(val.dw[0]) : "c"(msr));
		return val.qw;
	}


	static inline void wrmsr(const msr_t msr, const uint64_t v)
	{
		union {
			uint32_t dw[2];
			uint64_t qw;
		} val;
		val.qw = v;
		asm volatile("wrmsr" :: "d"(val.dw[1]), "a"(val.dw[0]), "c"(msr));
	}

	void wait_key(const char *msg);
	checked uint8_t rtc_read(const int addr);
	uint8_t  pmio_read8(const uint16_t offset);
	void     pmio_write8(const uint16_t offset, const uint8_t val);
	uint8_t  mem_read8(const uint64_t addr);
	uint16_t mem_read16(const uint64_t addr);
	uint32_t mem_read32(const uint64_t addr);
	uint64_t mem_read64(const uint64_t addr);
	void     mem_write8(const uint64_t addr, const uint8_t val);
	void     mem_write16(const uint64_t addr, const uint16_t val);
	void     mem_write32(const uint64_t addr, const uint32_t val);
	void     mem_write64(const uint64_t addr, const uint64_t val);
	uint8_t  mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg);
	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg);
	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg);
	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg);
	void     mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val);
	void     mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val);
	void     mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val);
	void     mcfg_write64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val);

	static inline uint32_t cht_read32(const ht_t ht, const reg_t reg)
	{
		return mcfg_read32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff);
	}

	static inline void cht_write32(const ht_t ht, const reg_t reg, const uint32_t val)
	{
		mcfg_write32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
	}
}
