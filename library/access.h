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
#include "atomic.h"

extern "C" {
	extern int lirq_nest;
}

#define cli() if (atomic_exchange_and_add(&lirq_nest, 1) == 0) { asm volatile("cli"); }
#define sti() if (atomic_decrement_and_test(&lirq_nest))       { asm volatile("sti"); }

// RTC constants
#define RTC_SECONDS     0
#define RTC_MINUTES     2
#define RTC_HOURS       4
#define RTC_DAY_OF_WEEK 6
#define RTC_DAY         7
#define RTC_MONTH       8
#define RTC_YEAR        9
#define RTC_SETTINGS    11

// APIC constants
#define	APIC_ESR	0x280
#define	APIC_ICR	0x300
#define		APIC_DEST_SELF		0x40000
#define		APIC_DEST_ALLINC	0x80000
#define		APIC_DEST_ALLBUT	0xC0000
#define		APIC_ICR_RR_MASK	0x30000
#define		APIC_ICR_RR_INVALID	0x00000
#define		APIC_ICR_RR_INPROG	0x10000
#define		APIC_ICR_RR_VALID	0x20000
#define		APIC_INT_LEVELTRIG	0x08000
#define		APIC_INT_ASSERT		0x04000
#define		APIC_ICR_BUSY		0x01000
#define		APIC_DEST_LOGICAL	0x00800
#define		APIC_DEST_PHYSICAL	0x00000
#define		APIC_DM_FIXED		0x00000
#define		APIC_DM_FIXED_MASK	0x00700
#define		APIC_DM_LOWEST		0x00100
#define		APIC_DM_SMI		0x00200
#define		APIC_DM_REMRD		0x00300
#define		APIC_DM_NMI		0x00400
#define		APIC_DM_INIT		0x00500
#define		APIC_DM_STARTUP		0x00600
#define		APIC_DM_EXTINT		0x00700
#define		APIC_VECTOR_MASK	0x000FF
#define	APIC_ICR2	0x310
#define		GET_APIC_DEST_FIELD(x)	(((x) >> 24) & 0xFF)
#define		SET_APIC_DEST_FIELD(x)	((x) << 24)

static inline __attribute__((always_inline)) void disable_cache(void)
{
#ifndef SIM
	uint32_t value;
	asm volatile("mov %%cr0, %0\n"
		     "or $(1 << 30), %0\n"
		     "mov %0, %%cr0\n"
		     "wbinvd\n" : "=r" (value) :: "memory");
#endif
}

static inline  __attribute__((always_inline)) void enable_cache(void)
{
#ifndef SIM
	uint32_t value;
	asm volatile("mov %%cr0, %0\n"
		     "and $~((1 << 30) | (1 << 29)), %0\n"
		     "mov %0, %%cr0\n" : "=r" (value) :: "memory");
#endif
}

namespace lib
{
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

	void native_apic_icr_write(const uint32_t low, const uint32_t apicid);
	void critical_enter(void);
	void critical_leave(void);
	void disable_xtpic(void);
	void enable_xtpic(void);
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
	uint32_t cf8_read32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg);
	void     cf8_write32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val);
	void memcpy64(uint64_t dest, uint64_t src, size_t n);

	static inline uint32_t cht_read32(const ht_t ht, const reg_t reg)
	{
//		return cf8_read32(0, 24+ht, reg >> 12, reg & 0xfff);
		return mcfg_read32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff);
	}

	static inline void cht_write32(const ht_t ht, const reg_t reg, const uint32_t val)
	{
//		cf8_write32(0, 24+ht, reg >> 12, reg & 0xfff, val);
		mcfg_write32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
	}
}
