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

#include <stdio.h>
#include <unistd.h>

#include "nc2-defs.h"
#include "nc2-bootloader.h"
#include "nc2-access.h"

#define PCI_CONF_SEL 0xcf8
#define PCI_CONF_DATA 0xcfc
#define PMIO_PORT 0xcd6

#define PCI_EXT_CONF(bus, devfn, reg)                           \
	(0x80000000 | (((reg) & 0xF00) << 16) | ((bus) << 16)	\
	 | ((devfn) << 8) | ((reg) & 0xFC))

int lirq_nest = 0;

/* #define DEBUG(...) printf(__VA_ARGS__) */
#define DEBUG(...) do { } while (0)

uint8_t pmio_readb(uint16_t offset)
{
	outb(offset, PMIO_PORT /* PMIO index */);
	return inb(PMIO_PORT + 1 /* PMIO data */);
}

void pmio_writeb(uint16_t offset, uint8_t val)
{
	/* Write offset and value in single 16-bit write */
	outw(offset | val << 8, PMIO_PORT);
}

uint32_t extpci_readl(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg)
{
	uint32_t ret;
	DEBUG("pci:%02x:%02x.%x %03x -> ",
	      bus, dev, func, reg);
	cli();
	outl(PCI_EXT_CONF(bus, ((dev << 3) | func), reg), PCI_CONF_SEL);
	ret = inl(PCI_CONF_DATA + (reg & 3));
	sti();
	DEBUG("%08x\n", ret);
	return ret;
}

uint8_t extpci_readb(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg)
{
	uint8_t ret;
	DEBUG("pci:%02x:%02x.%x %03x -> ",
	      bus, dev, func, reg);
	cli();
	outl(PCI_EXT_CONF(bus, ((dev << 3) | func), reg), PCI_CONF_SEL);
	ret = inb(PCI_CONF_DATA + (reg & 3));
	sti();
	DEBUG("%02x\n", ret);
	return ret;
}

void extpci_writel(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint32_t val)
{
	DEBUG("pci:%02x:%02x.%x %03x <- %08x",
	      bus, dev, func, reg, val);
	cli();
	outl(PCI_EXT_CONF(bus, ((dev << 3) | func), reg), PCI_CONF_SEL);
	outl(val, PCI_CONF_DATA + (reg & 3));
	sti();
	DEBUG("\n");
}

void extpci_writeb(uint8_t bus, uint8_t dev, uint8_t func, uint16_t reg, uint8_t val)
{
	DEBUG("pci:%02x:%02x.%x %03x <- %02x",
	      bus, dev, func, reg, val);
	cli();
	outl(PCI_EXT_CONF(bus, ((dev << 3) | func), reg), PCI_CONF_SEL);
	outb(val, PCI_CONF_DATA + (reg & 3));
	sti();
	DEBUG("\n");
}

uint32_t cht_readl(uint8_t node, uint8_t func, uint16_t reg)
{
	return extpci_readl(0, 24 + node, func, reg);
}

uint8_t cht_readb(uint8_t node, uint8_t func, uint16_t reg)
{
	return extpci_readb(0, 24 + node, func, reg);
}

void cht_writel(uint8_t node, uint8_t func, uint16_t reg, uint32_t val)
{
	extpci_writel(0, 24 + node, func, reg, val);
}

void cht_writeb(uint8_t node, uint8_t func, uint16_t reg, uint8_t val)
{
	extpci_writeb(0, 24 + node, func, reg, val);
}

void reset_cf9(enum reboot_mode mode, int last)
{
	uint8_t code = mode == REBOOT_WARM ? 0x02 : 0x0A;
	if (mode == REBOOT_WARM) {
		for (int i = 0; i <= last; i++) {
			uint32_t val = cht_readl(i, FUNC0_HT, 0x6c);
			val |=  (1<<5); /* BiosRstDet */
			cht_writel(i, FUNC0_HT, 0x6c, val);
		}
	}
	outb(code, 0xcf9);
	udelay(50);
	outb(code|4, 0xcf9);
}

static uint8_t smi_state;

/* Mask southbridge SMI generation */
void disable_smi(void)
{
	if (southbridge_id == VENDEV_SP5100) {
		smi_state = pmio_readb(0x53);
		pmio_writeb(0x53, smi_state | (1 << 3));
	}
}

/* Restore previous southbridge SMI mask */
void enable_smi(void)
{
	if (southbridge_id == VENDEV_SP5100) {
		pmio_writeb(0x53, smi_state);
	}
}

void critical_enter(void)
{
	cli();
	disable_smi();
}

void critical_leave(void)
{
	enable_smi();
	sti();
}
