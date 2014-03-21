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
#include <unistd.h>

#include "../bootloader.h"
#include "../opteron/msrs.h"
#include "access.h"

#define PCI_CONF_SEL 0xcf8
#define PCI_CONF_DATA 0xcfc
#define PMIO_PORT 0xcd6

#define PCI_EXT_CONF(bus, devfn, reg) \
  (0x80000000 | (((reg) & 0xF00) << 16) | \
  ((bus) << 16) | ((devfn) << 8) | ((reg) & 0xFC))
#define PCI_MMIO_CONF(bus, device, func, reg) \
  (((bus) << 20) | ((device) << 15) | ((func) << 12) | (reg))
/* Since we use FS to access these areas, the address needs to be in canonical form (sign extended from bit47) */
#define canonicalize(a) (((a) & (1ULL << 47)) ? ((a) | (0xffffULL << 48)) : (a))
#define setup_fs(addr) do { \
  asm volatile("mov %%ds, %%ax\n\tmov %%ax, %%fs" ::: "eax"); \
  asm volatile("wrmsr" :: "A"(canonicalize(addr)), "c"(MSR_FS_BASE)); \
  } while(0)

namespace lib
{
	void udelay(const uint32_t usecs)
	{
		uint64_t limit = lib::rdtscll() + (uint64_t)usecs * Opteron::tsc_mhz;

		while (lib::rdtscll() < limit)
			cpu_relax();
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

	void pmio_write8(const uint16_t offset, const uint8_t val)
	{
		/* Write offset and value in single 16-bit write */
		outw(offset | val << 8, PMIO_PORT);
	}

	uint8_t mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint8_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("movb %%fs:(0), %%al" : "=a"(ret));
		sti();
		if (options->debug.access)
			printf("%02x\n", ret);
		return ret;
	}

	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint16_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("movw %%fs:(0), %%ax" : "=a"(ret));
		sti();
		if (options->debug.access)
			printf("%04x\n", ret);
		return ret;
	}

	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint32_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("mov %%fs:(0), %%eax" : "=a"(ret));
		sti();
		if (options->debug.access)
			printf("%08x\n", ret);
		return ret;
	}

	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint64_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("movq %%fs:(0), %%mm0; movq %%mm0, (%0)" : :"r"(&ret) :"memory");
		sti();
		if (options->debug.access)
			printf("%016llx\n", ret);
		return ret;
	}

	void mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %02x", sci, bus, dev, func, reg, val);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("mov %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %04x", sci, bus, dev, func, reg, val);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("movw %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %08x", sci, bus, dev, func, reg, val);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("mov %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %016llx", sci, bus, dev, func, reg, val);
		cli();
		setup_fs((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		asm volatile("movq (%0), %%mm0; movq %%mm0, %%fs:(0)" : :"r"(&val) :"memory");
		sti();
		if (options->debug.access)
			printf("\n");
	}

	uint32_t cht_read32(const ht_t ht, const reg_t reg)
	{
		return mcfg_read32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff);
	}

	void cht_write32(const ht_t ht, const reg_t reg, const uint32_t val)
	{
		mcfg_write32(SCI_LOCAL, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
	}
}
