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
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../bootloader.h"
#include "../opteron/msrs.h"
#include "../platform/devices.h"
#include "access.h"

#define PMIO_PORT 0xcd6

#define PCI_CONF_SEL 0xcf8
#define PCI_CONF_DATA 0xcfc

#define PIC_MASTER_IMR 0x21
#define PIC_SLAVE_IMR 0xa1

#define PCI_EXT_CONF(bus, device, func, reg) \
	(0x80000000 | (((reg) & 0xF00) << 16) |	\
	 ((bus) << 16) | ((device) << 11) | ((func) << 8) | ((reg) & 0xFC))
#define PCI_MMIO_CONF(bus, device, func, reg) \
	(((bus) << 20) | ((device) << 15) | ((func) << 12) | (reg))

/* Since we use FS to access these areas, the address needs to be in canonical form (sign extended from bit47) */
#define canonicalize(a) (((a) & (1ULL << 47)) ? ((a) | (0xffffULL << 48)) : (a))
#define setup_fs(addr) do { \
  asm volatile("mov %%ds, %%ax; mov %%ax, %%fs" ::: "eax"); \
  asm volatile("wrmsr" :: "A"(canonicalize(addr)), "c"(MSR_FS_BASE)); \
  } while(0)

extern "C" {
	int lirq_nest = 0;
}

namespace lib
{
	static uint8_t pic1_mask, pic2_mask;
	static bool xtpic_disabled = 0;

	static inline void native_apic_mem_write(const uint32_t apic_base, const uint32_t reg, const uint32_t v)
	{
		*((volatile uint32_t *)(apic_base + reg)) = v;
	}

	static inline uint32_t native_apic_mem_read(const uint32_t apic_base, const uint32_t reg)
	{
		return *((volatile uint32_t *)(apic_base + reg));
	}

	void native_apic_icr_write(const uint32_t low, const uint32_t apicid)
	{
		uint32_t apic_base = ((uint32_t)rdmsr(MSR_APIC_BAR) & ~0xfff);
		cli();
		native_apic_mem_write(apic_base, APIC_ICR2, SET_APIC_DEST_FIELD(apicid));
		native_apic_mem_write(apic_base, APIC_ICR, low);
		sti();
		while (native_apic_mem_read(apic_base, APIC_ICR) & APIC_ICR_BUSY)
			cpu_relax();
		xassert(!native_apic_mem_read(apic_base, APIC_ESR));
	}

	void critical_enter(void)
	{
		cli();

		// FIXME: abstract
		// disable IOH SMI generation
		const uint8_t val = pmio_read8(0x53);
		pmio_write8(0x53, val | (1 << 3));
	}

	void critical_leave(void)
	{
		// FIXME: abstract
		// enable IOH SMI generation
		const uint8_t val = pmio_read8(0x53);
		pmio_write8(0x53, val & ~(1 << 3));

		sti();
	}

	void disable_xtpic(void)
	{
		// disable XT-PIC
		pic1_mask = inb(PIC_MASTER_IMR);
		outb(0xff, PIC_MASTER_IMR);
		pic2_mask = inb(PIC_SLAVE_IMR);
		outb(0xff, PIC_SLAVE_IMR);
		xtpic_disabled = 1;
	}

	void enable_xtpic(void)
	{
		xassert(xtpic_disabled);
		// enable XT-PIC
		inb(PIC_MASTER_IMR);
		outb(pic1_mask, PIC_MASTER_IMR);
		inb(PIC_SLAVE_IMR);
		outb(pic2_mask, PIC_SLAVE_IMR);
	}

	uint8_t rtc_read(const int addr)
	{
		outb(addr, 0x70);
		uint8_t val = inb(0x71);

		/* Convert from BCD if needed */
		outb(RTC_SETTINGS, 0x70);
		uint8_t settings = inb(0x71);
		if (!(settings & 4))
			return (val & 0xf) + (val / 16) * 10;

		return val;
	}

	uint8_t pmio_read8(const uint16_t offset)
	{
		outb(offset, PMIO_PORT /* PMIO index */);
		return inb(PMIO_PORT + 1 /* PMIO data */);
	}

	uint16_t pmio_read16(const uint16_t offset)
	{
		xassert(!(offset & 1));
		uint16_t val = 0;

		for (unsigned i = 0; i < sizeof(val); i++)
			val |= pmio_read8(offset + i) << (i * 8);
		return val;
	}

	uint32_t pmio_read32(const uint16_t offset)
	{
		xassert(!(offset & 3));
		uint32_t val = 0;

		for (unsigned i = 0; i < sizeof(val); i++)
			val |= pmio_read8(offset + i) << (i * 8);
		return val;
	}

	void pmio_write8(const uint16_t offset, const uint8_t val)
	{
		/* Write offset and value in single 16-bit write */
		outw(offset | val << 8, PMIO_PORT);
	}

	void pmio_write16(const uint16_t offset, const uint16_t val)
	{
		xassert(!(offset & 1));
		for (unsigned i = 0; i < sizeof(val); i++)
			pmio_write8(offset + i, val >> (i * 8));
	}

	void pmio_write32(const uint16_t offset, const uint32_t val)
	{
		xassert(!(offset & 3));
		for (unsigned i = 0; i < sizeof(val); i++)
			pmio_write8(offset + i, val >> (i * 8));
	}

	uint8_t mem_read8(const uint64_t addr)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" -> ", addr);
		uint8_t val;
		cli();
		setup_fs(addr);
		asm volatile("movb %%fs:(0), %%al" : "=a"(val) :: "memory");
		sti();
		if (options->debug.access & 2)
			printf("0x%02x\n", val);
		return val;
	}

	uint16_t mem_read16(const uint64_t addr)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" -> ", addr);
		xassert(!(addr & 1));
		uint16_t val;
		cli();
		setup_fs(addr);
		asm volatile("movw %%fs:(0), %%ax" : "=a"(val) :: "memory");
		sti();
		if (options->debug.access & 2)
			printf("0x%04x\n", val);
		return val;
	}

	uint32_t mem_read32(const uint64_t addr)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" -> ", addr);
		xassert(!(addr & 3));
		uint32_t val;
		cli();
		setup_fs(addr);
		asm volatile("mov %%fs:(0), %%eax" : "=a"(val) :: "memory");
		sti();
		if (options->debug.access & 2)
			printf("0x%08x\n", val);
		return val;
	}

	uint64_t mem_read64(const uint64_t addr)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" -> ", addr);
		xassert(!(addr & 7));
		uint64_t val;
		cli();
		setup_fs(addr);
		asm volatile("movq %%fs:(0), %%mm0; movq %%mm0, (%0)" :: "r"(&val) : "memory");
		sti();
		if (options->debug.access & 2)
			printf("0x%016"PRIx64"\n", val);
		return val;
	}

	void mem_write8(const uint64_t addr, const uint8_t val)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" <- 0x%02x", addr, val);
		cli();
		setup_fs(addr);
		asm volatile("movb %0, %%fs:(0)" :: "a"(val) : "memory");
		sti();
		if (options->debug.access & 2)
			printf("\n");
	}

	void mem_write16(const uint64_t addr, const uint16_t val)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" <- 0x%04x", addr, val);
		xassert(!(addr & 1));
		cli();
		setup_fs(addr);
		asm volatile("movw %0, %%fs:(0)" :: "a"(val) : "memory");
		sti();
		if (options->debug.access & 2)
			printf("\n");
	}

	void mem_write32(const uint64_t addr, const uint32_t val)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" <- 0x%08x", addr, val);
		xassert(!(addr & 3));
		cli();
		setup_fs(addr);
		asm volatile("mov %0, %%fs:(0)" :: "a"(val) : "memory");
		sti();
		if (options->debug.access & 2)
			printf("\n");
	}

	void mem_write64(const uint64_t addr, const uint64_t val)
	{
		if (options->debug.access & 2)
			printf("MEM:0x%016"PRIx64" <- 0x%016"PRIx64, addr, val);
		xassert(!(addr & 7));
		cli();
		setup_fs(addr);
		asm volatile("movq (%0), %%mm0; movq %%mm0, %%fs:(0)" :: "r"(&val) : "memory");
		sti();
		if (options->debug.access & 2)
			printf("\n");
	}

	uint64_t mcfg_base(const sci_t sci)
	{
		uint64_t base = (rdmsr(MSR_MCFG) & ~0xfffff);
		if (base < (1ULL << 32)) {
			xassert(sci == SCI_LOCAL || sci == config->local_node->sci);
			return base;
		}

		if (sci == SCI_LOCAL)
			return base;

		return (base & ~(0xfffULL << 28)) | ((uint64_t)sci << 28);
	}

	uint8_t mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint8_t ret;
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		xassert(reg < 0xfff);

		ret = mem_read8(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access & 1)
			printf("%02x\n", ret);
		return ret;
	}

	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint16_t ret;
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		xassert(!(reg & 1) && reg < 0xfff);

		ret = mem_read16(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access & 1)
			printf("%04x\n", ret);
		return ret;
	}

	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint32_t ret;
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		xassert(!(reg & 3) && reg < 0xfff);

		ret = mem_read32(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access & 1)
			printf("%08x\n", ret);
		return ret;
	}

	// requires CU_CFG2[50] to be set
	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint64_t ret;
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		xassert(!(reg & 7) && reg < 0xfff);

		ret = mem_read64(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access & 1)
			printf("%016"PRIx64"\n", ret);
		return ret;
	}

	void mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val)
	{
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x <- %02x", sci, bus, dev, func, reg, val);
		xassert(reg < 0xfff);

		mem_write8(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		if (options->debug.access & 1)
			printf("\n");
	}

	void mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val)
	{
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x <- %04x", sci, bus, dev, func, reg, val);
		xassert(!(reg & 1) && reg < 0xfff);

		mem_write16(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		if (options->debug.access & 1)
			printf("\n");
	}

	void mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x <- %08x", sci, bus, dev, func, reg, val);
		xassert(!(reg & 3) && reg < 0xfff);

		mem_write32(mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		if (options->debug.access & 1)
			printf("\n");
	}

	void mcfg_write64_split(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val)
	{
		if (options->debug.access & 1)
			printf("MCFG:%03x:%02x:%02x.%x %03x <- %016"PRIx64, sci, bus, dev, func, reg, val);
		xassert(!(reg & 7) && reg < 0xfff);

		const uint64_t addr = mcfg_base(sci) | PCI_MMIO_CONF(bus, dev, func, reg);
		mem_write32(addr, val);
		mem_write32(addr + 4, val >> 32);
		if (options->debug.access & 1)
			printf("\n");
	}

	uint32_t cf8_read32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		xassert(!(reg & 3) && reg < 0xfff);

		uint32_t ret;
		if (options->debug.access & 1)
			printf("CF8:%02x:%02x.%x %03x -> ", bus, dev, func, reg);
		cli();
		outl(PCI_EXT_CONF(bus, dev, func, reg), PCI_CONF_SEL);
		ret = inl(PCI_CONF_DATA);
		sti();
		if (options->debug.access & 1)
			printf("%08x\n", ret);
		return ret;
	}

	void cf8_write32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
		xassert(!(reg & 3) && reg < 0xfff);

		if (options->debug.access & 1)
			printf("CF8:%02x:%02x.%x %03x <- %08x", bus, dev, func, reg, val);
		cli();
		outl(PCI_EXT_CONF(bus, dev, func, reg), PCI_CONF_SEL);
		outl(val, PCI_CONF_DATA);
		sti();
		if (options->debug.access & 1)
			printf("\n");
	}

	void memcpy64(uint64_t dest, uint64_t src, size_t n)
	{
		for (size_t i = 0; i < n; i++)
			mem_write8(dest + i, mem_read8(src + i));
	}
}
