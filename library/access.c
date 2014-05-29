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
	void wait_key(const char *msg)
	{
		puts(msg);
		char ch;

		do {
			fread(&ch, 1, 1, stdin);
		} while (ch != 0x0a); // enter
	}

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

	uint8_t mem_read8(const uint64_t addr)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx -> ", addr);
		uint8_t val;
		cli();
		setup_fs(addr);
		asm volatile("movb %%fs:(0), %%al" : "=a"(val));
		sti();
		if (options->debug.access > 1)
			printf("0x%02x\n", val);
		return val;
	}

	uint16_t mem_read16(const uint64_t addr)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx -> ", addr);
		uint16_t val;
		cli();
		setup_fs(addr);
		asm volatile("movw %%fs:(0), %%ax" : "=a"(val));
		sti();
		if (options->debug.access > 1)
			printf("0x%04x\n", val);
		return val;
	}

	uint32_t mem_read32(const uint64_t addr)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx -> ", addr);
		uint32_t val;
		cli();
		setup_fs(addr);
		asm volatile("mov %%fs:(0), %%eax" : "=a"(val));
		sti();
		if (options->debug.access > 1)
			printf("0x%08x\n", val);
		return val;
	}

	uint64_t mem_read64(const uint64_t addr)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx -> ", addr);
		uint64_t val;
		cli();
		setup_fs(addr);
		asm volatile("movq %%fs:(0), %%mm0; movq %%mm0, (%0)" : :"r"(&val) :"memory");
		sti();
		if (options->debug.access > 1)
			printf("0x%016llx\n", val);
		return val;
	}

	void mem_write8(const uint64_t addr, const uint8_t val)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx <- 0x%02x", addr, val);
		cli();
		setup_fs(addr);
		asm volatile("movb %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access > 1)
			printf("\n");
	}

	void mem_write16(const uint64_t addr, const uint16_t val)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx <- 0x%04x", addr, val);
		cli();
		setup_fs(addr);
		asm volatile("movw %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access > 1)
			printf("\n");
	}

	void mem_write32(const uint64_t addr, const uint32_t val)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx <- 0x%08x", addr, val);
		cli();
		setup_fs(addr);
		asm volatile("mov %0, %%fs:(0)" :: "a"(val));
		sti();
		if (options->debug.access > 1)
			printf("\n");
	}

	void mem_write64(const uint64_t addr, const uint64_t val)
	{
		if (options->debug.access > 1)
			printf("MEM:0x%016llx <- 0x%016llx", addr, val);
		cli();
		setup_fs(addr);
		asm volatile("movq (%0), %%mm0; movq %%mm0, %%fs:(0)" : :"r"(&val) :"memory");
		sti();
		if (options->debug.access > 1)
			printf("\n");
	}

	uint8_t mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint8_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		ret = mem_read8((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access)
			printf("%02x\n", ret);
		return ret;
	}

	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint16_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		ret = mem_read16((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access)
			printf("%04x\n", ret);
		return ret;
	}

	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint32_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		ret = mem_read32((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access)
			printf("%08x\n", ret);
		return ret;
	}

	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		uint64_t ret;
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x -> ", sci, bus, dev, func, reg);
		ret = mem_read64((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg));
		if (options->debug.access)
			printf("%016llx\n", ret);
		return ret;
	}

	void mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %02x", sci, bus, dev, func, reg, val);
		mem_write8((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		sti();
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %04x", sci, bus, dev, func, reg, val);
		mem_write16((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %08x", sci, bus, dev, func, reg, val);
		mem_write32((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg), val);
		if (options->debug.access)
			printf("\n");
	}

	void mcfg_write64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val)
	{
		if (options->debug.access)
			printf("MCFG:SCI%03x:%02x:%02x.%x %03x <- %016llx", sci, bus, dev, func, reg, val);
		mem_write64((rdmsr(MSR_MCFG_BASE) & ~0xfffff) | ((uint64_t)sci << 28) | PCI_MMIO_CONF(bus, dev, func, reg), val);
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

	#define PRETTY_SIZE 16
	#define PRETTY_COUNT 4

	const char *pr_size(uint64_t size)
	{
		static char pretty[PRETTY_COUNT][PRETTY_SIZE];
		static unsigned index = 0;
		const char units[] = {0, 'K', 'M', 'G', 'T', 'P'};

		unsigned i = 0;
		while (size >= 1024 && i < sizeof(units)) {
			size /= 1024;
			i++;
		}

		if (units[i])
			snprintf(pretty[index], PRETTY_SIZE, "%llu%cB", size, units[i]);
		else
			snprintf(pretty[index], PRETTY_SIZE, "%lluB", size);

		const char *ret = pretty[index];
		index = (index + 1) % PRETTY_COUNT;
		return ret;
	}

	void dump(const void *addr, const unsigned len)
	{
		const unsigned char *addr2 = (const unsigned char *)addr;
		unsigned i = 0;

		while (i < len) {
			for (int j = 0; j < 8 && (i + j) < len; j++)
				printf(" %02x", addr2[i + j]);
			i += 8;
			printf("\n");
		}
	}

	void memcpy(void *dst, const void *src, size_t n)
	{
		printf("Copying %u bytes from %p to %p:\n", n, src, dst);
		dump(src, n);
		::memcpy(dst, src, n);
	}
}
