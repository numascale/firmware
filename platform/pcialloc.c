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
#include "../node.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../opteron/msrs.h"
#include "../numachip2/numachip.h"
#include "../platform/devices.h"
#include "../platform/options.h"

Allocator *Device::alloc;
#ifdef UNUSED
static bool compare1(const BAR* lhs, const BAR* rhs)
{
	return lhs->len < rhs->len;
}

static bool compare2(const BAR* lhs, const BAR* rhs)
{
	return lhs->len > rhs->len;
}
#endif
uint64_t Allocator::alloc(const bool s64, const bool pref, const uint64_t len, const unsigned vfs = 0)
{
	if (s64 && pref) {
		uint64_t ret = pos64;
		pos64 += len;
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
	return pos32_nonpref;
}

void Allocator::round_node()
{
	pos32_pref = roundup(pos32_pref, 1 << Numachip2::MMIO32_ATT_SHIFT);
	pos32_nonpref = pos32_nonpref &~ ((1 << Numachip2::MMIO32_ATT_SHIFT) - 1);
	pos64 = roundup(pos64, 1ULL << Numachip2::SIU_ATT_SHIFT);
}

void Allocator::report()
{
	printf("%s left\n", lib::pr_size(pos32_nonpref - pos32_pref));
}

void BAR::print() const
{
	printf(" %s,%s,%u", lib::pr_size(len), io ? "I" : pref ? "P" : "NP", s64 ? 64 : 32);
	if (addr)
		printf(",0x%llx", addr);
}

void Device::print() const
{
	if (bars_io.size() || bars_nonpref32.size() || bars_pref32.size() || bars_pref64.size()) {
		printf("%03x.%02x:%02x.%x:", sci, bus, dev, fn);
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
unsigned probe_bar(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t fn, const unsigned offset, Endpoint *ep, const uint16_t vfs = 0)
{
	uint32_t cmd = lib::mcfg_read32(sci, bus, dev, fn, 4);
	lib::mcfg_write32(sci, bus, dev, fn, 4, 0);

	uint32_t save = lib::mcfg_read32(sci, bus, dev, fn, offset);
	bool io = save & 1;
	uint32_t mask = io ? 1 : 15;
	uint64_t assigned = save & ~mask;
	bool s64 = ((save >> 1) & 3) == 2;
	bool pref = (save >> 3) & 1;

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
		BAR *bar = new BAR(io, s64, pref, len, assigned, vfs);
		ep->add(bar);
	}

	lib::mcfg_write32(sci, bus, dev, fn, offset, save);
	lib::mcfg_write32(sci, bus, dev, fn, 4, cmd);

	// skip second register if a 64-bit BAR
	return s64 ? 4 : 0;
}

void scan_device(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t fn, Endpoint *ep)
{
	for (unsigned offset = 0x10; offset <= 0x30; offset += 4) {
		// skip gap between last BAR and expansion ROM address
		if (offset == 0x28)
			offset = 0x30;

		offset += probe_bar(sci, bus, dev, fn, offset, ep);
	}

	// assign BARs in capabilities
	uint16_t cap = extcapability(sci, bus, dev, fn, PCI_ECAP_SRIOV);
	if (cap != PCI_CAP_NONE) {
		// PCI SR-IOV spec needs the number of Virtual Functions times the BAR in space
		const uint16_t vfs = lib::mcfg_read32(sci, bus, dev, fn, cap + 0x0c) >> 16;

		for (unsigned offset = 0x24; offset <= 0x38; offset += 4)
			offset += probe_bar(sci, bus, dev, fn, cap + offset, ep, vfs);
	}
}

static void populate(const sci_t sci, const uint8_t bus, Bridge *br)
{
	for (uint8_t dev = 0; dev < 32; dev++) {
		for (uint8_t fn = 0; fn < 8; fn++) {
			uint32_t val = lib::mcfg_read32(sci, bus, dev, fn, 0xc);
			// PCI device functions are not necessarily contiguous
			if (val == 0xffffffff)
				continue;

			uint8_t type = val >> 16;

			// recurse down bridges
			if ((type & 0x7f) == 0x01) {
				Bridge *sub = new Bridge(sci, br, bus, dev, fn);
				uint8_t sec = (lib::mcfg_read32(sci, bus, dev, fn, 0x18) >> 8) & 0xff;
				populate(sci, sec, sub);
			} else {
				Endpoint *ep = new Endpoint(sci, br, bus, dev, fn);
				scan_device(sci, bus, dev, fn, ep);
			}

			// if not multi-function, break out of function loop
			if (!fn && !(type & 0x80))
				break;
		}
	}
}

void pci_realloc()
{
	Vector<Bridge*> roots;
	Device::alloc = new Allocator(lib::rdmsr(MSR_TOPMEM), 0x10000000000);

	lib::critical_enter();

	// phase 1
	foreach_node(node) {
		Bridge* b0 = new Bridge((*node)->sci, NULL, 0, 0, 0); // host bridge
		populate((*node)->sci, 0, b0);
		roots.push_back(b0);
	}
	lib::critical_leave();

	for (Bridge **br = roots.elements; br < roots.limit; br++)
		(*br)->classify();

	for (Bridge **br = roots.elements; br < roots.limit; br++) {
		Device::alloc->round_node();
		(*br)->assign();
	}

	if (options->debug.remote_io) {
		printf("PCI scan:\n");
		for (Bridge **br = roots.elements; br < roots.limit; br++)
			(*br)->print();
	}

	Device::alloc->report();
}
