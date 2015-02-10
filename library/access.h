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
#include <sys/io.h>

#include "base.h"

#define cli() { asm volatile("cli"); }
#define sti() { asm volatile("sti"); }

#define cpu_relax() asm volatile("pause" ::: "memory")

#define RTC_SECONDS     0
#define RTC_MINUTES     2
#define RTC_HOURS       4
#define RTC_DAY_OF_WEEK 6
#define RTC_DAY         7
#define RTC_MONTH       8
#define RTC_YEAR        9
#define RTC_SETTINGS    11

#define PIC_MASTER_IMR          0x21
#define PIC_SLAVE_IMR           0xa1

static inline void disable_cache(void)
{
#ifndef SIM
	asm volatile("mov %%cr0, %%eax\n"
	  "or $0x40000000, %%eax\n"
	  "mov %%eax, %%cr0\n"
	  "wbinvd\n" ::: "eax", "memory");
#endif
}

static inline void enable_cache(void)
{
#ifndef SIM
	asm volatile("mov %%cr0, %%eax\n"
	  "and $~0x40000000, %%eax\n"
	  "mov %%eax, %%cr0\n" ::: "eax", "memory");
#endif
}

namespace lib
{
	static inline uint32_t uint32_tbswap(uint32_t val)
	{
		asm volatile("bswap %0" : "+r"(val));
		return val;
	}

	static inline uint64_t rdmsr(const msr_t msr)
	{
		uint64_t val;
		asm volatile("rdmsr" : "=A" (val) : "c" (msr));
		return val;
	}

	static inline void wrmsr(const msr_t msr, const uint64_t val)
	{
		asm volatile("wrmsr" :: "c" (msr), "A" (val));
	}

	void critical_enter(void);
	void critical_leave(void);
	checked uint8_t rtc_read(const int addr);
	uint8_t  pmio_read8(const uint16_t offset);
	uint16_t pmio_read16(const uint16_t offset);
	uint32_t pmio_read32(const uint16_t offset);
	void     pmio_write8(const uint16_t offset, const uint8_t val);
	void     pmio_write16(const uint16_t offset, const uint16_t val);
	void     pmio_write32(const uint16_t offset, const uint32_t val);
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
	void     mcfg_write64_split(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val);
	void memcpy64(uint64_t dest, uint64_t src, size_t n);

	static inline uint32_t cht_read32(const ht_t ht, const reg_t reg)
	{
		return mcfg_read32(SCI_NONE, 0, 24 + ht, reg >> 12, reg & 0xfff);
	}

	static inline uint64_t cht_read64(const ht_t ht, const reg_t reg)
	{
		return mcfg_read64(SCI_NONE, 0, 24 + ht, reg >> 12, reg & 0xfff);
	}

	static inline void cht_write32(const ht_t ht, const reg_t reg, const uint32_t val)
	{
		mcfg_write32(SCI_NONE, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
	}

	static inline void cht_write64(const ht_t ht, const reg_t reg, const uint64_t val)
	{
		mcfg_write64_split(SCI_NONE, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
	}
}
