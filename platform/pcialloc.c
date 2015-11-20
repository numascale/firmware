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

Allocator::Allocator(const uint64_t _start32, const uint64_t _start64):
	start32(_start32), end32(Opteron::MMIO32_LIMIT), start64(_start64), pos32_pref(_start32), pos32_nonpref(end32), pos64(start64)
{
	blocks = (end32 - start32) >> Numachip2::MMIO32_ATT_SHIFT;
	map32 = (sci_t *)malloc(blocks * sizeof(*map32));

	for (unsigned i = 0; i < blocks; i++)
		map32[i] = 0x000; // FIXME: ALLOC_UNALLOCATED;
}

void Allocator::reserve(const uint64_t base, const uint64_t limit, const sci_t sci)
{
	printf("Reserving 0x%llx:0x%llx to %03x\n", base, limit, sci);

	uint32_t mask = (1 << Numachip2::MMIO32_ATT_SHIFT) - 1;
	xassert(!(base & mask));
	xassert(!(limit & mask));

	xassert(base >= start32);
	xassert(limit <= 0x100000000);

	for (unsigned i = (base - start32) >> Numachip2::MMIO32_ATT_SHIFT; i < (limit - start32) >> Numachip2::MMIO32_ATT_SHIFT; i++)
		map32[i] = sci;
}

// setup map entries from one node's PoV
void Allocator::maps32(Node *const node)
{
	printf("\nMMIO32 routing on %03x:\n", node->config->id);
	sci_t dest = map32[0];
	uint64_t addr = start32;
	unsigned i;

	// FIXME: coallesce MMIO32 ranges to remote nodes
	for (i = 1; i < blocks; i++) {
		if (map32[i] == dest)
			continue;

		uint64_t limit = (i << Numachip2::MMIO32_ATT_SHIFT) + start32 - 1;
		ht_t dht = dest == node->numachip->config->id ? node->opterons[0]->ioh_ht : node->numachip->ht;
		link_t dlink = dest == node->numachip->config->id ? node->opterons[0]->ioh_link : 0;

		if (dest != ALLOC_UNALLOCATED && dest != ALLOC_LOCAL) {
			node->numachip->mmioatt.range(addr, limit, dest);
			foreach_nb(&node, nb)
				(*nb)->mmiomap->add(addr, limit, dht, dlink);
		}

		dest = map32[i];
		addr = (i << Numachip2::MMIO32_ATT_SHIFT) + start32;
	}

	if (dest != ALLOC_UNALLOCATED && dest != ALLOC_LOCAL) {
		// final entry needed
		uint64_t limit = (i << Numachip2::MMIO32_ATT_SHIFT) + start32 - 1;
		ht_t dht = dest == node->numachip->config->id ? node->opterons[0]->ioh_ht : node->numachip->ht;
		link_t dlink = dest == node->numachip->config->id ? node->opterons[0]->ioh_link : 0;

		node->numachip->mmioatt.range(addr, limit, dest);
		foreach_nb(&node, nb)
			(*nb)->mmiomap->add(addr, limit, dht, dlink);
	}
}

uint64_t Allocator::alloc(const bool s64, const bool pref, const uint64_t len, const unsigned vfs = 0)
{
	if (s64 && pref) {
		uint64_t ret = pos64;
		pos64 += len;
		printf("<64P 0x%llx>", ret);
		return ret;
	}

	if (pref) {
		// align BAR
		uint64_t ret = roundup(pos32_pref, len);
		uint64_t pos32_pref2 = ret + len * vfs;

		// fail allocation if no space
		if (((int64_t)pos32_nonpref - (int64_t)pos32_pref2) < 0) {
			printf("allocation of %s P failed\n", lib::pr_size(len));
			return 0;
		}

		pos32_pref = pos32_pref2;
		printf("<32P 0x%llx>", ret);
		return ret;
	}

	uint64_t pos32_nonpref2 = pos32_nonpref - len * vfs;
	pos32_nonpref2 &= ~(len - 1); // align

	// fail allocation if no space
	if (((int64_t)pos32_nonpref2 - (int64_t)pos32_pref) < 0) {
		printf("allocation of %s NP failed\n", lib::pr_size(len));
		return 0;
	}

	pos32_nonpref = pos32_nonpref2;
	printf("<32NP 0x%llx>", pos32_nonpref);
	return pos32_nonpref;
}

void Allocator::round_node()
{
	pos32_pref = roundup(pos32_pref, 1 << Numachip2::MMIO32_ATT_SHIFT);
	pos32_nonpref = pos32_nonpref &~ ((1 << Numachip2::MMIO32_ATT_SHIFT) - 1);
	pos64 = roundup(pos64, 1ULL << Numachip2::SIU_ATT_SHIFT);
}

void BAR::assign(const uint64_t _addr)
{
	addr = _addr;

	lib::mcfg_write32(sci, bus, dev, fn, offset, addr);
	if (s64)
		lib::mcfg_write32(sci, bus, dev, fn, offset + 4, addr >> 32);
}

void BAR::print() const
{
	printf(" %s,%s,%u@%02x:%02x.%x", lib::pr_size(len), io ? "I" : pref ? "P" : "NP", s64 ? 64 : 32, bus, dev, fn);
	if (addr)
		printf(",0x%llx", addr);
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

	if (bar->s64 && !bar->pref) {
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

	// if there are 64-bit prefetchable BARs and 32-bit ones, assign 64-bit ones in 32-bit space, as bridge only supports one prefetchable range
	if (bars_pref64.size() && bars_pref32.size()) {
		printf("demoting 64-bit pref BARs\n");
		while (bars_pref64.size()) {
			BAR *bar = bars_pref64.pop();
			bars_pref32.push_back(bar);
		}
	}
}

void Device::assign()
{
	uint64_t pos32_pref = alloc->pos32_pref;
	uint64_t pos32_nonpref = alloc->pos32_nonpref;
	uint64_t pos64 = alloc->pos64;

	if (!node->config->master) {
		// disable IO, DMA and legacy interrupts; enable memory decode
		lib::mcfg_write32(node->config->id, bus, dev, fn, 0x4, 0x0402);

		// clear IO BARs
		for (BAR **bar = bars_io.elements; bar < bars_io.limit; bar++)
			(*bar)->assign(0);

		// set Interrupt Line register to 0 (unallocated)
		lib::mcfg_write32(node->config->id, bus, dev, fn, 0x3c, 0);
	}

	bars_nonpref32.sort();
	for (BAR **bar = bars_nonpref32.elements; bar < bars_nonpref32.limit; bar++) {
		uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	bars_pref32.sort();
	for (BAR **bar = bars_pref32.elements; bar < bars_pref32.limit; bar++) {
		uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	bars_pref64.sort();
	for (BAR **bar = bars_pref64.elements; bar < bars_pref64.limit; bar++) {
		uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len, (*bar)->vfs);
		(*bar)->assign(addr);
	}

	for (Device **d = children.elements; d < children.limit; d++)
		(*d)->assign();

	// assign bridge windows
	if (bridge) {
		printf("bridge %03x:%02x:%02x.%u pref32@0x%llx:0x%llx nonpref32@0x%llx:0x%llx pref64@0x%llx-0x%llx\n",
			node->config->id, bus, dev, fn, pos32_pref, alloc->pos32_pref,
			alloc->pos32_nonpref, pos32_nonpref, pos64, alloc->pos64);

		// zero IO BARs on slaves only
		if (!node->config->master) {
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x1c, 0xf0);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x30, 0);
		}

		uint32_t start, end;
		if (alloc->pos32_pref - pos32_pref > 0) {
			start = pos32_pref;
			end = pos32_pref;
		} else {
			start = pos32_nonpref;
			end = alloc->pos32_nonpref;
		}

		if (end - start > 0) {
			uint32_t val = (start >> 16) | ((end - 1) & 0xffff0000);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x20, val);
		} else
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x20, 0x0000fffff);

		if (alloc->pos64 - pos64 > 0) {
			uint32_t val = (pos64 >> 16) | ((alloc->pos64 - 1) & 0xffff0000);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x24, val);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x28, pos64 >> 32);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x2c, (alloc->pos64 - 1) >> 32);
		} else {
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x24, 0x0000ffff);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x28, 0x00000000);
			lib::mcfg_write32(node->config->id, bus, dev, fn, 0x2c, 0x00000000);
		}
	}
}

void Device::print() const
{
	if (bars_io.size() || bars_nonpref32.size() || bars_pref32.size() || bars_pref64.size()) {
		printf("%03x.%02x:%02x.%x:", node->config->id, bus, dev, fn);
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
unsigned probe_bar(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t fn, const unsigned offset, Device *pdev, const uint16_t vfs = 0)
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
	for (unsigned offset = 0x10; offset <= 0x30; offset += 4) {
		// skip gap between last BAR and expansion ROM address
		if (offset == 0x28)
			offset = 0x30;

		offset += probe_bar(sci, bus, dev, fn, offset, pdev);
	}

	// assign BARs in capabilities
	uint16_t cap = extcapability(sci, bus, dev, fn, PCI_ECAP_SRIOV);
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
				Bridge *sub = new Bridge(br->node, br, bus, dev, fn);
				probe_bar(br->node->config->id, bus, dev, fn, 0x10, sub);
				probe_bar(br->node->config->id, bus, dev, fn, 0x14, sub);
				probe_bar(br->node->config->id, bus, dev, fn, 0x38, sub);

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
		// ioh_ioapicind_write(node->sci, 0, 0);
	}
}

void pci_realloc()
{
	Vector<Bridge*> roots;
	// start MMIO64 after the HyperTransport decode range to avoid interference
	Device::alloc = new Allocator(lib::rdmsr(MSR_TOPMEM), max(dram_top, Opteron::HT_LIMIT));

	lib::critical_enter();

	// phase 1
	foreach_node(node) {
		pci_prepare(*node);

		Bridge* b0 = new Bridge(*node, NULL, 0, 0, 0); // host bridge
		populate(b0, 0);
		roots.push_back(b0);
	}
	lib::critical_leave();

	// phase 2
	for (Bridge **br = roots.elements; br < roots.limit; br++)
		(*br)->classify();

	// phase 3
	for (Bridge **br = roots.elements; br < roots.limit; br++) {
		(*br)->node->mmio64_base = Device::alloc->pos64;
		uint64_t pos32_pref = Device::alloc->pos32_pref;
		uint64_t pos32_nonpref = Device::alloc->pos32_nonpref;

		(*br)->assign();
		Device::alloc->round_node();

		if (Device::alloc->pos32_pref != pos32_pref) {
			(*br)->node->mmio32_base = pos32_pref;
			(*br)->node->mmio32_limit = Device::alloc->pos32_pref;
		} else {
			(*br)->node->mmio32_base = Device::alloc->pos32_nonpref;
			(*br)->node->mmio32_limit = pos32_nonpref;
		}

		Device::alloc->reserve((*br)->node->mmio32_base, (*br)->node->mmio32_limit, (*br)->node->config->id);

		(*br)->node->mmio64_limit = Device::alloc->pos64;
		printf("%03x: 0x%llx-0x%llx 0x%llx-0x%llx\n\n", (*br)->node->config->id, (*br)->node->mmio32_base, (*br)->node->mmio32_limit, (*br)->node->mmio64_base, (*br)->node->mmio64_limit);
	}

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
			(*node)->numachip->mmiomap.add(Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, (*node)->opterons[0]->ioh_ht);
		else {
			// adapt to ATT granularity
			uint64_t base = Opteron::MMIO_VGA_BASE & ~((1ULL << Numachip2::MMIO32_ATT_SHIFT) - 1);
			uint64_t limit = Opteron::MMIO_VGA_LIMIT | ((1ULL << Numachip2::MMIO32_ATT_SHIFT) - 1);
			(*node)->numachip->mmioatt.range(base, limit, (*node)->config->id);
		}

		// MMIO decode
		(*node)->numachip->mmiomap.add(lib::rdmsr(MSR_TOPMEM), 0xffffffff, (*node)->opterons[0]->ioh_ht);

		Device::alloc->maps32(*node);

		foreach_nb(node, nb) {
			link_t link = node == &node[0] ? (*node)->opterons[0]->ioh_link : 0;

			// remote MMIO64 below
			if ((*node)->mmio64_base > Device::alloc->start64)
				(*nb)->mmiomap->add(Device::alloc->start64, (*node)->mmio64_base - 1, (*node)->numachip->ht, 0);

			// local MMIO64
			if ((*node)->mmio64_limit > (*node)->mmio64_base)
				(*nb)->mmiomap->add((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->opterons[0]->ioh_ht, link);

			// remote MMIO64 above
			if (Device::alloc->pos64 > (*node)->mmio64_limit)
				(*nb)->mmiomap->add((*node)->mmio64_limit, Device::alloc->pos64 - 1, (*node)->numachip->ht, 0);
		}

		if ((*node)->mmio64_limit > (*node)->mmio64_base) {
			(*node)->numachip->mmiomap.add((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->opterons[0]->ioh_ht);

			foreach_node(dnode)
				(*dnode)->numachip->dramatt.range((*node)->mmio64_base, (*node)->mmio64_limit - 1, (*node)->config->id);
		}

		(*node)->iohub->limits(Device::alloc->pos64 - 1);
	}

	if (options->debug.remote_io) {
		printf("PCI scan:\n");
		for (Bridge **br = roots.elements; br < roots.limit; br++)
			(*br)->print();
	}
}
