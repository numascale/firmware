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

#include "pcialloc.h"
#include "../bootloader.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../opteron/msrs.h"
#include "../numachip2/numachip.h"
#include "../platform/devices.h"

Allocator *Device::alloc;

Allocator::Allocator():
	start32(lib::rdmsr(MSR_TOPMEM)), end32(0xffffffff), pos32(start32), start64(max(dram_top, Opteron::HT_LIMIT)), pos64(start64)
{
}

uint64_t Allocator::alloc64(const uint64_t len, const unsigned vfs = 1)
{
	uint64_t ret = pos64;
	pos64 += len * vfs;
	printf("<64P 0x%llx>", ret);
	return ret;
}

uint64_t Allocator::alloc32(const uint64_t len, const unsigned vfs = 1)
{
	// align BAR
	uint64_t ret = roundup(pos32, len);
	uint64_t pos32_shadow = ret + len * vfs;

	// fail allocation if no space
	if (pos32_shadow > end32) {
		printf("allocation of %s P failed\n", lib::pr_size(len));
		return 0;
	}

	pos32 = pos32_shadow;
	return ret;
}

void Allocator::round_node()
{
	pos32 = roundup(pos32, 1 << Numachip2::MMIO32_ATT_SHIFT);
	pos64 = roundup(pos64, 1ULL << Numachip2::SIU_ATT_SHIFT);
}

void BAR::assign(const uint64_t _addr)
{
	addr = _addr;

	lib::mcfg_write32(sci, bus, dev, fn, offset, addr);
	if (s64)
		lib::mcfg_write32(sci, bus, dev, fn, offset + 4, addr >> 32);

	print();
}

void BAR::print() const
{
	printf(" %s,%s,%u@%02x:%02x.%x 0x%02x @ 0x%llx", lib::pr_size(len), io ? "I" : pref ? "P" : "NP", s64 ? 64 : 32, bus, dev, fn, offset, addr);
	if (vfs > 1)
		printf(" (%u VFS)", vfs);
	if (addr < 0x100000000)
		printf(" (val %08x)", *(uint32_t *)addr);
	printf("\n");
}

void Device::add(BAR *bar)
{
#ifdef DEBUG
	printf("- %s BAR%s%s%s\n", lib::pr_size(bar->len), bar->io ? " IO" : "", bar->s64 ? " 64bit" : "", bar->pref ? " pref" : "");
#endif
	// store in parent if non-root
	Device *target = parent ? parent : this;

	if (bar->io) {
		target->bars_io.push_back(bar);
		return;
	}

	if (bar->s64 && bar->pref) {
		target->bars_pref64.push_back(bar);
		return;
	}

	if (bar->pref)
		target->bars_pref32.push_back(bar);
	else
		target->bars_nonpref32.push_back(bar);
}

void Device::classify()
{
	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->classify();

	// if there are both 64-bit and 32-bit prefetchable BARs, demote the 32-bit BARs to non-prefetchable, so the 64-bit BARs can be in high memory
	if (bars_pref64.size() && bars_pref32.size()) {
		printf("demoting 32-bit pref BARs to non-pref @ %03x %02x:%02x.%x (%u %u %u)\n", node->config->id, bus, dev, fn, bars_pref64.size(), bars_pref32.size(), bars_nonpref32.size());
		while (bars_pref32.size()) {
			BAR *bar = bars_pref32.pop();
			bars_nonpref32.push_back(bar);
		}
	}
}

// called only on master
void Device::scope()
{
	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->scope();

	// detect free range
	for (BAR **bar = bars_nonpref32.elements; bar < bars_nonpref32.limit; bar++)
		if ((*bar)->addr < Device::alloc->end32)
			Device::alloc->end32 = (*bar)->addr;

	for (BAR **bar = bars_pref32.elements; bar < bars_pref32.limit; bar++)
		if ((*bar)->addr < Device::alloc->end32)
			Device::alloc->end32 = (*bar)->addr;

	for (BAR **bar = bars_pref64.elements; bar < bars_pref64.limit; bar++)
		if ((*bar)->addr < Device::alloc->end32)
			Device::alloc->end32 = (*bar)->addr;
}

// called only on slaves
void Device::assign_pref()
{
	const uint64_t start32 = alloc->pos32;
	const uint64_t start64 = alloc->pos64;

	// FIXME: bars_pref32.sort();
	for (BAR **bar = bars_pref32.elements; bar < bars_pref32.limit; bar++) {
		xassert((*bar)->pref);
		xassert(!(*bar)->io);
		uint64_t addr = alloc->alloc32((*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	// FIXME: bars_pref64.sort();
	for (BAR **bar = bars_pref64.elements; bar < bars_pref64.limit; bar++) {
		xassert((*bar)->pref);
		xassert((*bar)->s64);
		xassert(!(*bar)->io);
		uint64_t addr = alloc->alloc64((*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->assign_pref();

	alloc->pos32 = roundup(alloc->pos32, 1 << Numachip2::MMIO32_ATT_SHIFT);

	// for endpoints or non-root bridges
	if (type != TypeHost) {
		uint32_t val = lib::mcfg_read32(node->config->id, bus, dev, fn, 0x4);
		val |= 1 << 10; // disable legacy interrupts
		val &= ~1; // IO space
		lib::mcfg_write32(node->config->id, bus, dev, fn, 0x4, val);

		// clear IO BARs
		for (BAR **bar = bars_io.elements; bar < bars_io.limit; bar++)
			(*bar)->assign(0);

		// set Interrupt Line register to 0 (unallocated)
		lib::mcfg_write32(node->config->id, bus, dev, fn, 0x3c, 0xff00);

		// assign bridge windows
		if (type == TypeBridge) {
			// clear IO and upper IO registers
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x1c, 0x00ff);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x30, 0x0000ffff);

			// clear 32-bit window
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x20, 0x0000ffff);

			// clear 64-bit pref window
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x24, 0x0000ffff);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x28, 0xffffffff);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x2c, 0x00000000);

			uint64_t start, end;

			if (alloc->pos32 > start32) {
				start = start32;
				end = alloc->pos32;
				xassert(alloc->pos64 == start64);
			} else {
				start = start64;
				end = alloc->pos64;
				xassert(alloc->pos32 == start32);
			}

			if (end > start) {
				val = (start >> 16) | ((end - 1) & 0xffff0000);
				lib::mcfg_write32(node->config->id, bus, dev, fn, 0x24, val);
				lib::mcfg_write32(node->config->id, bus, dev, fn, 0x28, start >> 32);
				lib::mcfg_write32(node->config->id, bus, dev, fn, 0x2c, (end - 1) >> 32);
			}
		}
	}
}

// called only on slaves
void Device::assign_nonpref()
{
	const uint64_t start32 = alloc->pos32;

	// FIXME: bars_nonpref32.sort();
	for (BAR **bar = bars_nonpref32.elements; bar < bars_nonpref32.limit; bar++) {
		xassert(!(*bar)->io);
		uint64_t addr = alloc->alloc32((*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->assign_nonpref();

	alloc->pos32 = roundup(alloc->pos32, 1 << Numachip2::MMIO32_ATT_SHIFT);

	// assign bridge windows
	if (type == TypeBridge && alloc->pos32 > start32) {
		uint32_t val = (start32 >> 16) | ((alloc->pos32 - 1) & 0xffff0000);
		lib::mcfg_write32(node->config->id, bus, dev, fn, 0x20, val);
	}
}

void Device::print() const
{
	if (bars_io.size() || bars_nonpref32.size() || bars_pref32.size() || bars_pref64.size()) {
		printf("%03x.%02x:%02x.%x:\n", node->config->id, bus, dev, fn);
		for (BAR **bar = bars_io.elements; bar < bars_io.limit; bar++)
			(*bar)->print();
		for (BAR **bar = bars_nonpref32.elements; bar < bars_nonpref32.limit; bar++)
			(*bar)->print();
		for (BAR **bar = bars_pref32.elements; bar < bars_pref32.limit; bar++)
			(*bar)->print();
		for (BAR **bar = bars_pref64.elements; bar < bars_pref64.limit; bar++)
			(*bar)->print();
		printf("\n");
	}

	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->print();
}

// returns offset to skip for 64-bit BAR
unsigned probe_bar(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t fn, const uint16_t offset, Device *pdev, const uint16_t vfs = 1)
{
	uint32_t cmd = lib::mcfg_read32(sci, bus, dev, fn, 4);
	lib::mcfg_write32(sci, bus, dev, fn, 4, 0);

	uint32_t save = lib::mcfg_read32(sci, bus, dev, fn, offset);
	bool io = save & 1;
	uint32_t mask = io ? 1 : 15;
	uint64_t assigned = save & ~mask;
	bool s64 = ((save >> 1) & 3) == 2;

	// follow Linux in marking PCI endpoint and bridge expansion ROMs as prefetchable
	bool pref = (offset == 0x30 || offset == 0x38) ? 1 : ((save >> 3) & 1);

	lib::mcfg_write32(sci, bus, dev, fn, offset, 0xffffffff);
	uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, offset);

	// skip unimplemented BARs
	if (val != 0x00000000 && val != 0xffffffff) {
		uint64_t len = val & ~mask;

		if (s64) {
			uint32_t save2 = lib::mcfg_read32(sci, bus, dev, fn, offset + 4);
			assigned |= (uint64_t)save2 << 32;
			lib::mcfg_write32(sci, bus, dev, fn, offset + 4, 0xffffffff);
			len |= (uint64_t)lib::mcfg_read32(sci, bus, dev, fn, offset + 4) << 32;
			lib::mcfg_write32(sci, bus, dev, fn, offset + 4, save2);
		}

		len &= ~(len - 1);
		BAR *bar = new BAR(sci, bus, dev, fn, offset, io, s64, pref, len, assigned, vfs);
		pdev->add(bar);
	}

	lib::mcfg_write32(sci, bus, dev, fn, offset, save);
	lib::mcfg_write32(sci, bus, dev, fn, 4, cmd);

	// skip second register if a 64-bit BAR
	return s64 ? 4 : 0;
}

void scan_device(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t fn, Device *pdev)
{
	for (uint16_t offset = 0x10; offset <= 0x30; offset += 4) {
		// skip gap between last BAR and expansion ROM address
		if (offset == 0x28)
			offset = 0x30;

		offset += probe_bar(sci, bus, dev, fn, offset, pdev);
	}

	// assign BARs in capabilities
	uint16_t cap = extcapability(PCI_ECAP_SRIOV, sci, bus, dev, fn);
	if (cap != PCI_CAP_NONE) {
		// PCI SR-IOV spec needs the number of Virtual Functions times the BAR in space
		const uint16_t vfs = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x0c) >> 16;

		for (unsigned offset = 0x24; offset <= 0x38; offset += 4)
			offset += probe_bar(sci, bus, dev, fn, cap + offset, pdev, vfs);
	}
}

static void populate(Bridge *br, const uint8_t bus)
{
	for (uint8_t dev = 0; dev < 32; dev++) {
		for (uint8_t fn = 0; fn < 8; fn++) {
			uint32_t val = lib::mcfg_read32(br->node->config->id, bus, dev, fn, 0xc);
			// PCI device functions are not necessarily contiguous
			if (val == 0xffffffff)
				continue;

			uint8_t type = val >> 16;

			// recurse down bridges
			if ((type & 0x7f) == 0x01) {
				if (probe_bar(br->node->config->id, bus, dev, fn, 0x10, br) == 4)
					probe_bar(br->node->config->id, bus, dev, fn, 0x14, br);
				probe_bar(br->node->config->id, bus, dev, fn, 0x38, br);

				Bridge *sub = new Bridge(br->node, br, bus, dev, fn);
				uint8_t sec = (lib::mcfg_read32(br->node->config->id, bus, dev, fn, 0x18) >> 8) & 0xff;
				populate(sub, sec);
			} else {
				Endpoint *ep = new Endpoint(br->node, br, bus, dev, fn);
				scan_device(br->node->config->id, bus, dev, fn, ep);
			}

			// if not multi-function, break out of function loop
			if (!fn && !(type & 0x80))
				break;
		}
	}
}

static void pci_prepare(const Node *const node)
{
	// enable SP5100 SATA MSI support
	uint32_t val2 = lib::mcfg_read32(node->config->id, 0, 17, 0, 0x40);
	lib::mcfg_write32(node->config->id, 0, 17, 0, 0x40, val2 | 1);
	uint32_t val = lib::mcfg_read32(node->config->id, 0, 17, 0, 0x60);
	lib::mcfg_write32(node->config->id, 0, 17, 0, 0x60, (val & ~0xff00) | 0x5000);
	lib::mcfg_write32(node->config->id, 0, 17, 0, 0x40, val2);

	if (!node->config->master) {
		// disable HPET MMIO decoding
		val = lib::mcfg_read32(node->config->id, 0, 20, 0, 0x40);
		lib::mcfg_write32(node->config->id, 0, 20, 0, 0x40, val & ~(1 << 28));

		// hide IDE controller
		val = lib::mcfg_read32(node->config->id, 0, 20, 0, 0xac);
		lib::mcfg_write32(node->config->id, 0, 20, 0, 0xac, val | (1 << 19));

		// hide the ISA LPC controller
		val = lib::mcfg_read32(node->config->id, 0, 20, 0, 0x64);
		lib::mcfg_write32(node->config->id, 0, 20, 0, 0x64, val & ~(1 << 20));

		// disable and hide all USB controllers
		lib::mcfg_write32(node->config->id, 0, 18, 0, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 18, 1, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 18, 2, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 19, 0, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 19, 1, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 19, 2, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 0, 19, 5, 4, 0x0400);
		val = lib::mcfg_read32(node->config->id, 0, 20, 0, 0x68);
		lib::mcfg_write32(node->config->id, 0, 20, 0, 0x68, val & ~0xf7);

		// disable the ACPI/SMBus function
		lib::mcfg_write32(node->config->id, 0, 20, 0, 4, 0x0400);

		// disable and hide VGA controller
		lib::mcfg_write32(node->config->id, 0, 20, 4, 4, 0x0400);
		lib::mcfg_write32(node->config->id, 1, 4, 0, 4, 0x0400);
		val = lib::mcfg_read32(node->config->id, 0, 20, 4, 0x5c);
		lib::mcfg_write32(node->config->id, 0, 20, 4, 0x5c, val & ~0xffff0000);

		// disable IOAPIC memory decode and IOAPIC
		val = lib::mcfg_read32(node->config->id, 0, 0x14, 0, 0x64);
		lib::mcfg_write32(node->config->id, 0, 0x14, 0, 0x64, val & ~(1 << 3));
		node->iohub->ioapicind_write(0, 0);
	}
}

void pci_realloc()
{
	Vector<Bridge*> roots;
	// start MMIO64 after the HyperTransport decode range to avoid interference

	lib::critical_enter();

	// phase 1
	foreach_node(node) {
		pci_prepare(*node);

		Bridge *b0 = new Bridge(*node, NULL, 0, 0, 0, Device::TypeHost); // host bridge
		populate(b0, 0);
		roots.push_back(b0);
	}
	lib::critical_leave();

	// phase 2
	for (Bridge **br = roots.elements; br < roots.limit; br++)
		(*br)->classify();

	Device::alloc = new Allocator();

	// phase 3
	for (Bridge **br = roots.elements; br < roots.limit; br++) {
		(*br)->node->mmio32_base = Device::alloc->pos32;
		(*br)->node->mmio64_base = Device::alloc->pos64;

		if ((*br)->node->config->master) {
			(*br)->scope();
			(*br)->node->mmio32_base = Device::alloc->end32;
			(*br)->node->mmio32_limit = 0xe0000000;
			printf("usable 0x%08llx-0x%08llx; master MMIO32 0x%llx-0x%llx\n", Device::alloc->start32, Device::alloc->end32, (*br)->node->mmio32_base, (*br)->node->mmio32_limit);
		} else {
			printf("assigning prefetchable on %03x\n", (*br)->node->config->id);
			(*br)->assign_pref();
			printf("\nassigning nonprefetchable on %03x\n", (*br)->node->config->id);
			(*br)->assign_nonpref();
		}

		Device::alloc->round_node();

		if (!(*br)->node->config->master)
			(*br)->node->mmio32_limit = Device::alloc->pos32;
		(*br)->node->mmio64_limit = Device::alloc->pos64;

		printf("%03x: 0x%llx-0x%llx 0x%llx-0x%llx\n", (*br)->node->config->id, (*br)->node->mmio32_base, (*br)->node->mmio32_limit, (*br)->node->mmio64_base, (*br)->node->mmio64_limit);
	}

	// prevent hole in MMIO32 area
	nodes[0]->mmio32_base = Device::alloc->pos32;

	// phase 4
	foreach_node(node) {
		foreach_nb(node, nb) {
			// remove existing MMIO32 range
			(*nb)->mmiomap->remove(lib::rdmsr(MSR_TOPMEM), 0xdfffffff); // FIXME use legacy MCFG start

			if (!(*node)->config->master) {
				// read-add VGA console pointing to master
				(*nb)->mmiomap->remove(Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT);
				(*nb)->mmiomap->add(Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, (*node)->numachip->ht, 0);
			}
		}

		// VGA console decode
		if ((*node)->config->master)
			(*node)->numachip->mmiomap.add(0x0, 0xffffffff, (*node)->opterons[0]->ioh_ht);
		else
			(*node)->numachip->mmioatt.range(0x0, 0xffffffff, nodes[0]->config->id);

		foreach_nb(node, nb) {
			link_t link = node == &node[0] ? (*node)->opterons[0]->ioh_link : 0;

			// remote MMIO below
			if ((*node)->mmio64_base > Device::alloc->start64)
				(*nb)->mmiomap->add(Device::alloc->start64, (*node)->mmio64_base - 1, (*node)->numachip->ht, 0);

			if ((*node)->mmio32_base > Device::alloc->start32)
				(*nb)->mmiomap->add(Device::alloc->start32, (*node)->mmio32_base - 1, (*node)->numachip->ht, 0);

			// local MMIO
			if ((*node)->mmio64_limit > (*node)->mmio64_base)
				(*nb)->mmiomap->add((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->opterons[0]->ioh_ht, link);

			if ((*node)->mmio32_limit > (*node)->mmio32_base)
				(*nb)->mmiomap->add((*node)->mmio32_base, (*node)->mmio32_limit - 1, (*node)->opterons[0]->ioh_ht, link);

			// remote MMIO above
			if (Device::alloc->pos64 > (*node)->mmio64_limit)
				(*nb)->mmiomap->add((*node)->mmio64_limit, Device::alloc->pos64 - 1, (*node)->numachip->ht, 0);

			if (!(*node)->config->master) {
				(*nb)->mmiomap->add((*node)->mmio32_limit, 0xdfffffff, (*node)->numachip->ht, 0);
				(*nb)->mmiomap->remove(0xf1000000, 0xffffffff);
				(*nb)->mmiomap->add(0xf1000000, 0xffffffff, (*node)->numachip->ht, 0);
			}
		}

		if ((*node)->mmio64_limit > (*node)->mmio64_base) {
			(*node)->numachip->mmiomap.add((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->opterons[0]->ioh_ht);

			foreach_node(dnode)
				(*dnode)->numachip->dramatt.range((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->config->id);
		}

		if ((*node)->mmio32_limit > (*node)->mmio32_base) {
			(*node)->numachip->mmiomap.add((*node)->mmio32_base, (*node)->mmio32_limit - 1, (*node)->opterons[0]->ioh_ht);

			foreach_node(dnode)
				(*dnode)->numachip->mmioatt.range((*node)->mmio32_base, (*node)->mmio32_limit - 1, (*node)->config->id);
		}

		// FIXME: if 64-bit BARs aren't used, use DRAM top
		(*node)->iohub->limits(Device::alloc->pos64 - 1);
	}

	if (options->debug.remote_io) {
		printf("PCI scan:\n");
		for (Bridge **br = roots.elements; br < roots.limit; br++)
			(*br)->print();
	}
}
