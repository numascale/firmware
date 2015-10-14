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
#include "../library/utils.h"
#include "../numachip2/numachip.h"

using namespace std;

static bool compare1(const BAR* lhs, const BAR* rhs)
{
	return lhs->len < rhs->len;
}

static bool compare2(const BAR* lhs, const BAR* rhs)
{
	return lhs->len > rhs->len;
}

uint64_t Allocator::alloc(const bool s64, const bool pref, const uint64_t len)
{
	if (s64 && pref) {
		uint64_t ret = pos64;
		pos64 += len;
		return ret;
	}

	if (pref) {
		// align BAR
		uint64_t ret = roundup(pos32_pref, len);
		uint64_t pos32_pref2 = ret + len;

		// fail allocation if no space
		if (((int64_t)pos32_nonpref - (int64_t)pos32_pref2) < 0) {
			printf("allocation of %s P failed\n", lib::pr_size(len));
			return 0;
		}

		pos32_pref = pos32_pref2;
		return ret;
	}

	uint64_t pos32_nonpref2 = pos32_nonpref - len;
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
	printf(" %s,%s,%u", lib::pr_size(len), pref ? "P" : "NP", s64 ? 64 : 32);
	if (addr)
		printf(",0x%llx", addr);
}

void Device::print() const
{
	printf("%03x.%02x:%02x.%x:", sci, bus, dev, fn);
	for (BAR *bar = bars_nonpref32[0]; bar <= bars_nonpref32[-1]; ++bar)
		bar->print();
	for (BAR *bar = bars_pref32[0]; bar <= bars_pref32[-1]; ++bar)
		bar->print();
	for (BAR *bar = bars_pref64[0]; bar <= bars_pref64[-1]; ++bar)
		bar->print();

	printf("\n");

	for (Device *d = children[0]; d <= children[-1]; ++d)
		d->print();
}

static Bridge* node(const sci_t sci)
{
	Bridge* b0 = new Bridge(sci, NULL, 0, 0, 0); // host bridge
	// FIXME: check if expanision ROM is non-pref
	Bridge* b1 = new Bridge(sci, b0, 0, 2, 0); // sec 1, sub 1

	Bridge* b2 = new Bridge(sci, b0, 0, 3, 0); // sec 2, sub 2

	Bridge* b3 = new Bridge(sci, b0, 0, 4, 0); // sec 3, sub 8

	Bridge* b9 = new Bridge(sci, b0, 0, 9, 0); // sec 9, sub 9

	Bridge* ba = new Bridge(sci, b0, 0, 0x14, 4); // sec 0xa, sub 0xa

	Bridge* b4 = new Bridge(sci, b3, 3, 0x00, 0); // sec 4, sub 8
	b4->add(new BAR(0, 0, 128 << 10, 0xef2e0000));

	Bridge* b5 = new Bridge(sci, b4, 4, 0x00, 0); // sec 5, sub 5

	Bridge* b6 = new Bridge(sci, b4, 4, 0x01, 0); // sec 6, sub 6

	Bridge* b7 = new Bridge(sci, b4, 4, 0x04, 0); // sec 7, sub 7

	Bridge* b8 = new Bridge(sci, b4, 4, 0x05, 0); // sec 8, sub 8

	Endpoint* ep = new Endpoint(sci, b0, 0, 0, 0);

	if (sci == 0x000) {
		ep = new Endpoint(sci, b0, 0, 0x12, 0);
		ep->add(new BAR(0, 0, 4 << 10, 0xef3fb000));

		ep = new Endpoint(sci, b0, 0, 0x12, 1);
		ep->add(new BAR(0, 0, 4 << 10, 0xef3fc000));

		ep = new Endpoint(sci, b0, 0, 0x12, 2);
		ep->add(new BAR(0, 0, 256, 0xef3ffe00));

		ep = new Endpoint(sci, b0, 0, 0x13, 0);
		ep->add(new BAR(0, 0, 4 << 10, 0xef3fd000));

		ep = new Endpoint(sci, b0, 0, 0x13, 1);
		ep->add(new BAR(0, 0, 4 << 10, 0xef3fe000));

		ep = new Endpoint(sci, b0, 0, 0x13, 2);
		ep->add(new BAR(0, 0, 256, 0xef3fff00));
	}

	ep = new Endpoint(sci, b0, 0, 0x14, 0);

	if (sci == 0x000) {
		ep = new Endpoint(sci, b0, 0, 0x14, 3);
	}

	ep = new Endpoint(sci, b1, 1, 0, 0);
	ep->add(new BAR(1, 0, 32 << 20, 0xe6000000));

	ep = new Endpoint(sci, b1, 1, 0, 1);
	ep->add(new BAR(1, 0, 32 << 20, 0xe8000000));

	ep = new Endpoint(sci, b2, 2, 0, 0);
	ep->add(new BAR(1, 0, 32 << 20, 0xea000000));

	ep = new Endpoint(sci, b2, 2, 0, 1);
	ep->add(new BAR(1, 0, 32 << 20, 0xec000000));

	ep = new Endpoint(sci, b5, 5, 0, 0);
	ep->add(new BAR(1, 0, 64 << 10, 0xef1f0000));
	ep->add(new BAR(1, 0, 256 << 10, 0xef180000));
	ep->add(new BAR(0, 0, 1 << 20, 0xef000000)); // ROM

	if (sci == 0x000) {
		ep = new Endpoint(sci, ba, 0xa, 3, 0);
		ep->add(new BAR(0, 1, 8 << 20, 0xe5800000));
		ep->add(new BAR(0, 0, 16 << 10, 0xeeffc000));
		ep->add(new BAR(0, 0, 8 << 20, 0xee000000));
		ep->add(new BAR(0, 0, 64 << 10, 0xee800000)); // ROM
	}

	return b0;
}

static Allocator *Device::alloc;

void pci_realloc()
{
	Vector<Bridge*> roots;

	Allocator alloc(0xe0000000, 0x10000000000);
	Device::alloc = &alloc;

	// phase 1
	roots.push_back(node(0x000));
	roots.push_back(node(0x001));
	roots.push_back(node(0x010));
	roots.push_back(node(0x011));

	for (Bridge *br = roots[0]; br <= roots[-1]; ++br)
		br->classify();

	for (Bridge *br = roots[0]; br <= roots[-1]; ++br) {
		Device::alloc->round_node();
		br->assign();
	}

	for (Bridge *br = roots[0]; br <= roots[-1]; ++br)
		br->print();

	Device::alloc->report();
}
