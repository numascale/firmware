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
#include <string.h>

#include "../library/base.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../bootloader.h"
#include "devices.h"

uint64_t Devices::IOAPIC::vectors[Devices::IOAPIC::nvectors];

uint64_t Devices::IOAPIC::read64(const uint8_t reg)
{
	lib::mem_write32(base, reg);
	return lib::mem_read32(base + 0x10);
}

void Devices::IOAPIC::write64(const uint8_t reg, const uint64_t val)
{
	lib::mem_write32(base, reg);
	return lib::mem_write64(base + 0x10, val);
}

void Devices::IOAPIC::inhibit(void)
{
	for (unsigned i = 0; i < nvectors; i++) {
		vectors[i] = read64(i * 8 + 0x10);
		write64(i * 8 + 0x10, vectors[i] | (1 << 16));
	}
}

void Devices::IOAPIC::restore(void)
{
	for (unsigned i = 0; i < nvectors; i++)
		write64(i * 8 + 0x10, vectors[i]);
}

void pci_search(const struct devspec *list, const sci_t sci, const int bus)
{
	const struct devspec *listp;

	for (int dev = 0; dev < (bus == 0 ? 24 : 32); dev++) {
		for (int fn = 0; fn < 8; fn++) {
			uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, 0xc);
			/* PCI device functions are not necessarily contiguous */
			if (val == 0xffffffff)
				continue;

			uint8_t type = val >> 16;
			uint32_t ctlcap = lib::mcfg_read32(sci, bus, dev, fn, 8);

			for (listp = list; listp->classtype != PCI_CLASS_FINAL; listp++)
				if ((listp->classtype == PCI_CLASS_ANY) || ((ctlcap >> ((4 - listp->classlen) * 8)) == listp->classtype))
					if ((listp->type == PCI_TYPE_ANY) || (listp->type == (type & 0x7f)))
						listp->handler(sci, bus, dev, fn);

			/* Recurse down bridges */
			if ((type & 0x7f) == PCI_TYPE_BRIDGE) {
				int sec = (lib::mcfg_read32(sci, bus, dev, fn, 0x18) >> 8) & 0xff;
				pci_search(list, sci, sec);
			}

			/* If not multi-function, break out of function loop */
			if (!fn && !(type & 0x80))
				break;
		}
	}
}

static void pci_search_start(const struct devspec *list, const sci_t sci)
{
	pci_search(list, sci, 0);
}

static uint16_t capability(const uint8_t cap, const sci_t sci, const int bus, const int dev, const int fn)
{
	/* Check for capability list */
	if (!(lib::mcfg_read32(sci, bus, dev, fn, 0x4) & (1 << 20)))
		return PCI_CAP_NONE;

	uint8_t pos = lib::mcfg_read32(sci, bus, dev, fn, 0x34) & 0xff;

	for (int lim = 0; lim < 48 && pos >= 0x40; lim++) {
		pos &= ~3;

		uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, pos + 0);
		if (val == 0xffffffff)
			break;

		if ((val & 0xff) == cap)
			return pos;

		pos = (val >> 8) & 0xfc;
	}

	return PCI_CAP_NONE;
}

uint16_t extcapability(const uint16_t cap, const sci_t sci, const int bus, const int dev, const int fn)
{
	uint16_t cap2 = capability(PCI_CAP_PCIE, sci, bus, dev, fn);

	if (cap2 == PCI_CAP_NONE)
		return PCI_CAP_NONE;

	uint8_t visited[0x1000];
	uint16_t offset = 0x100;

	memset(visited, 0, sizeof(visited));

	do {
		uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, offset);
		if (val == 0xffffffff || val == 0)
			return PCI_CAP_NONE;

		if (cap == (val & 0xffff))
			return offset;

		offset = (val >> 20) & ~3;
	} while (offset > 0xff && visited[offset]++ == 0);

	return PCI_CAP_NONE;
}

static void completion_timeout(const uint16_t sci, const int bus, const int dev, const int fn)
{
	uint32_t val;
	printf("PCI device @ %02x:%02x.%x: ", bus, dev, fn);

	/* For legacy devices */
	val = lib::mcfg_read32(sci, bus, dev, fn, 4);
	lib::mcfg_write32(sci, bus, dev, fn, 4, val & ~(1 << 8));
	printf("disabled SERR");

	uint16_t cap = capability(PCI_CAP_PCIE, sci, bus, dev, fn);
	if (cap != PCI_CAP_NONE) {
		/* Device Control */
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x8);
		lib::mcfg_write32(sci, bus, dev, fn, cap + 0x8, val | (1 << 4) | (1 << 8) | (1 << 11));
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x8);
		if (val & (1 << 4))
			printf("; Relaxed Ordering enabled");
		else
			printf("; failed to enable Relaxed Ordering");

		if (val & (1 << 8))
			printf("; Extended Tag enabled");
		else
			printf("; failed to enable Extended Tag");

		if (val & (1 << 11))
			printf("; No Snoop enabled");
		else
			printf("; failed to enable No Snoop");

		/* Root Control */
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x1c);
		if (val & (1 << 1)) {
			lib::mcfg_write32(sci, bus, dev, fn, cap + 0x1c, val | (1 << 4));
			printf("; disabled SERR on Non-Fatal");
		} else
			printf("; Non-Fatal doesn't trigger SERR");

		/* Device Capabilities/Control 2 */
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x24);

		/* Select Completion Timeout range D if possible */
		if (val & (1 << 3)) {
			uint32_t val2 = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x28);
			lib::mcfg_write32(sci, bus, dev, fn, cap + 0x28, (val2 & ~0xf) | 0xe);
			printf("; Completion Timeout 17-64s");
		} else
			printf("; Setting Completion Timeout unsupported");

		/* Disable Completion Timeout if possible */
		if (val & (1 << 4)) {
			val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x28);
			lib::mcfg_write32(sci, bus, dev, fn, cap + 0x28, val | (1 << 4));
			printf("; Completion Timeout disabled");
		} else
			printf("; Disabling Completion Timeout unsupported");
	}

	cap = extcapability(PCI_ECAP_AER, sci, bus, dev, fn);
	if (cap != PCI_CAP_NONE) {
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x0c);
		if (val & (1 << 14)) {
			lib::mcfg_write32(sci, bus, dev, fn, cap + 0x0c, val & ~(1 << 14));
			val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x0c);
			if (val & (1 << 14))
				printf("; Completion Timeout now non-fatal");
			else
				printf("; failed to set Completion Timeout as non-fatal");
		} else
			printf("; Completion Timeout already non-fatal");

		/* Mask root complex error reporting */
		val = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x2c);
		lib::mcfg_write32(sci, bus, dev, fn, cap + 0x2c, val | ~7);
	} else
		printf("; no AER");

	printf("\n");
}

static void adjust_bridge(const uint16_t sci, const int bus, const int dev, const int fn)
{
	uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, 0x3c);
	val &= ~(1 << 17); /* Disable SERR# Enable */
	val &= ~(1 << 24); /* Set primary Discard Timer to 2^15 cycles */
	val &= ~(1 << 25); /* Set secondary Discard Timer to 2^15 cycles */
	val &= ~(1 << 27); /* Disable Discard Timer SERR# Enable */
	lib::mcfg_write32(sci, bus, dev, fn, 0x3c, val);
}

static void stop_ohci(const uint16_t sci, const int bus, const int dev, const int fn)
{
	uint32_t val, bar0;
	printf("OHCI controller @ %02x:%02x.%x: ", bus, dev, fn);
	bar0 = lib::mcfg_read32(sci, bus, dev, fn, 0x10) & ~0xf;
	if ((bar0 == 0xffffffff) || (bar0 == 0)) {
		printf("BAR not configured\n");
		return;
	}

	val = lib::mem_read32(bar0 + HcControl);
	if (val & OHCI_CTRL_IR) { /* Interrupt routing enabled, we must request change of ownership */
		uint32_t temp;
		/* This timeout is arbitrary.  we make it long, so systems
		 * depending on usb keyboards may be usable even if the
		 * BIOS/SMM code seems pretty broken
		 */
		temp = 1000;
		lib::mem_write32(bar0 + HcInterruptEnable, OHCI_INTR_OC); /* Enable OwnershipChange interrupt */
		lib::mem_write32(bar0 + HcCommandStatus, OHCI_OCR); /* Request OwnershipChange */

		while (lib::mem_read32(bar0 + HcControl) & OHCI_CTRL_IR) {
			lib::udelay(1000);

			if (--temp == 0)
				fatal("legacy handover timed out\n");
		}

		/* Shutdown */
		lib::mem_write32(bar0 + HcInterruptDisable, OHCI_INTR_MIE);
		val = lib::mem_read32(bar0 + HcControl);
		val &= OHCI_CTRL_RWC;
		lib::mem_write32(bar0 + HcControl, val);
		/* Flush the writes */
		val = lib::mem_read32(bar0 + HcControl);
		printf("legacy handover succeeded\n");
	} else {
		printf("legacy support not enabled\n");
	}

	val = lib::mem_read32(bar0 + HcRevision);

	if (val & (1 << 8)) { /* Legacy emulation is supported */
		val = lib::mem_read32(bar0 + HceControl);

		if (val & (1 << 0)) {
			printf("legacy support enabled\n");
		}
	}
}

static void stop_ehci(const uint16_t sci, const int bus, const int dev, const int fn)
{
	printf("EHCI controller @ %02x:%02x.%x: ", bus, dev, fn);
	uint32_t bar0 = lib::mcfg_read32(sci, bus, dev, fn, 0x10) & ~0xf;

	if (bar0 == 0) {
		printf("BAR not configured\n");
		return;
	}

	/* Get EHCI Extended Capabilities Pointer */
	uint32_t ecp = (lib::mem_read32(bar0 + 0x8) >> 8) & 0xff;

	if (ecp == 0) {
		printf("extended capabilities absent\n");
		return;
	}

	/* Check legacy support register shows BIOS ownership */
	uint32_t legsup = lib::mcfg_read32(sci, bus, dev, fn, ecp);

	if ((legsup & 0x10100ff) != 0x0010001) {
		printf("legacy support not enabled\n");
		return;
	}

	/* Set OS owned semaphore */
	legsup |= 1 << 24;
	lib::mcfg_write32(sci, bus, dev, fn, ecp, legsup);
	int limit = 1000;

	do {
		lib::udelay(1000);
		legsup = lib::mcfg_read32(sci, bus, dev, fn, ecp);

		if ((legsup & (1 << 16)) == 0) {
			printf("legacy handover succeeded\n");
			return;
		}
	} while (--limit);

	fatal("legacy handover timed out\n");
}

static void stop_xhci(const uint16_t sci, const int bus, const int dev, const int fn)
{
	printf("XHCI controller @ %02x:%02x.%x: ", bus, dev, fn);
	uint32_t bar0 = lib::mcfg_read32(sci, bus, dev, fn, 0x10) & ~0xf;

	if (bar0 == 0) {
		printf("BAR not configured\n");
		return;
	}

	/* Get XHCI Extended Capabilities Pointer */
	uint32_t ecp = (lib::mem_read32(bar0 + 0x10) & 0xffff0000) >> (16 - 2);

	if (ecp == 0) {
		printf("extended capabilities absent\n");
		return;
	}

	/* Check legacy support register shows BIOS ownership */
	uint32_t legsup = lib::mem_read32(bar0 + ecp);

	if ((legsup & 0x10100ff) != 0x0010001) {
		printf("legacy support not enabled\n");
		return;
	}

	/* Set OS owned semaphore */
	legsup |= 1 << 24;
	lib::mem_write32(bar0 + ecp, legsup);
	int limit = 1000;

	do {
		lib::udelay(1000);
		legsup = lib::mem_read32(bar0 + ecp);

		if ((legsup & (1 << 16)) == 0) {
			printf("legacy handover succeeded\n");
			return;
		}
	} while (--limit);

	fatal("legacy handover timed out\n");
}

static void stop_ahci(const uint16_t sci, const int bus, const int dev, const int fn)
{
	printf("AHCI controller @ %02x:%02x.%x: ", bus, dev, fn);
	/* BAR5 (ABAR) contains legacy control registers */
	uint32_t bar5 = lib::mcfg_read32(sci, bus, dev, fn, 0x24) & ~0xf;

	if (bar5 == 0) {
		printf("BAR not configured\n");
		return;
	}

	/* Check legacy support register shows BIOS ownership */
	uint32_t legsup = lib::mem_read32(bar5 + 0x24);

	if ((legsup & 1) == 0) {
		printf("legacy support not implemented\n");
		return;
	}

	legsup = lib::mem_read32(bar5 + 0x28);

	if ((legsup & 1) != 1) {
		printf("legacy support not enabled\n");
		return;
	}

	/* Set OS owned semaphore */
	legsup |= (1 << 1);
	lib::mem_write32(bar5 + 0x28, legsup);
	int limit = 1000;

	do {
		lib::udelay(1000);
		legsup = lib::mem_read32(bar5 + 0x28);

		if ((legsup & 1) == 0) {
			printf("legacy handover succeeded\n");
			return;
		}
	} while (--limit);

	fatal("legacy handover timed out\n");
}

void handover_legacy(const sci_t sci)
{
	const struct devspec devices[] = {
		{PCI_CLASS_SERIAL_USB_OHCI, 3, PCI_TYPE_ENDPOINT, stop_ohci},
		{PCI_CLASS_SERIAL_USB_EHCI, 3, PCI_TYPE_ENDPOINT, stop_ehci},
		{PCI_CLASS_SERIAL_USB_XHCI, 3, PCI_TYPE_ENDPOINT, stop_xhci},
		{PCI_CLASS_STORAGE_SATA,    2, PCI_TYPE_ENDPOINT, stop_ahci},
		{PCI_CLASS_STORAGE_RAID,    2, PCI_TYPE_ENDPOINT, stop_ahci},
		{PCI_CLASS_FINAL, 0, PCI_TYPE_ANY, NULL}
	};
	pci_search_start(devices, sci);
}

void pci_setup(const sci_t sci)
{
	const struct devspec devices[] = {
		{PCI_CLASS_ANY,             0, PCI_TYPE_ANY, completion_timeout},
		{PCI_CLASS_ANY,             0, PCI_TYPE_BRIDGE, adjust_bridge},
		{PCI_CLASS_FINAL,           0, PCI_TYPE_ANY, NULL}
	};

	printf("Adjusting PCI parameters:\n");
	pci_search_start(devices, sci);
}
