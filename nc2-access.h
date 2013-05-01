/*
 * Copyright (C) 2008-2012 Numascale AS, support@numascale.com
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

#ifndef __NC2_ACCESS
#define __NC2_ACCESS 1

#include <inttypes.h>
#include <sys/io.h>

extern int lirq_nest;
#define cli() if (lirq_nest++ == 0) { asm volatile("cli"); }
#define sti() if (--lirq_nest == 0) { asm volatile("sti"); }

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

static inline uint64_t rdtscll(void)
{
	uint64_t val;
	/* rdtscp doesn't work on Fam10h, so use mfence to serialise */
	asm volatile("mfence; rdtsc" : "=A"(val));
	return val;
}

static inline uint32_t uint32_tbswap(uint32_t val)
{
	asm volatile("bswap %0" : "+r"(val));
	return val;
}

static inline uint64_t rdmsr(uint32_t msr)
{
	union {
		uint32_t dw[2];
		uint64_t qw;
	} val;
	asm volatile("rdmsr" : "=d"(val.dw[1]), "=a"(val.dw[0]) : "c"(msr));
	return val.qw;
}


static inline void wrmsr(uint32_t msr, uint64_t v)
{
	union {
		uint32_t dw[2];
		uint64_t qw;
	} val;
	val.qw = v;
	asm volatile("wrmsr" :: "d"(val.dw[1]), "a"(val.dw[0]), "c"(msr));
}

uint8_t pmio_readb(uint16_t offset);
void pmio_writeb(uint16_t offset, uint8_t val);
uint32_t extpci_readl(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg);
uint8_t extpci_readb(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg);
void extpci_writel(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint32_t val);
void extpci_writeb(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint8_t val);
uint32_t cht_readl(uint8_t node, uint8_t func, uint16_t reg);
uint8_t cht_readb(uint8_t node, uint8_t func, uint16_t reg);
void cht_writel(uint8_t node, uint8_t func, uint16_t reg, uint32_t val);
void cht_writeb(uint8_t node, uint8_t func, uint16_t reg, uint8_t val);
void reset_cf9(int mode, int last);
void disable_smi(void);
void enable_smi(void);
void critical_enter(void);
void critical_leave(void);

#endif
