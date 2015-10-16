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

#pragma once

#include <stdio.h>
#include <stdint.h>

#include "../library/base.h"

// FIXME: add BARs to vector in bridge

/*
 - phase 1: the tree is built with all BARs added to 32-bit pref, 32-bit non-pref and 64-bit pref lists in the parent bridge
 - phase 2: at every bridge, the path to the root bridge is searched for 32-bit prefetchable BARs
  > if any 32-bit pref are found, the subtree and parent BARs are moved from 64-bit to 32-bit
 - phase 3: BARs are sorted and assigned addresses, and bridges ranges assigned
*/

class Allocator
{
	const uint64_t start32, end32 , start64;
	uint64_t pos32_pref, pos32_nonpref, pos64;
public:
	Allocator(const uint64_t _start32, const uint64_t _start64):
		start32(_start32), end32(0xfdffffff), start64(_start64), pos32_pref(_start32), pos32_nonpref(end32), pos64(start64)
	{}
	void reserve(const uint32_t addr, const uint32_t len)
	{}
	uint64_t alloc(const bool s64, const bool pref, const uint64_t len);
	void round_node();
	void report();
};

class BAR
{
	uint64_t addr;
public:
	const bool io, s64, pref;
	const uint64_t len;

	BAR(const bool _io, const bool _s64, const bool _pref, const uint64_t _len, const uint64_t _addr):
		addr(_addr), io(_io), s64(_s64), pref(_pref), len(_len)
	{}
	void assign(const uint64_t _addr)
	{
		addr = _addr;
	}
	void print(void) const;
};

class Device
{
	const sci_t sci;
	Vector<Device *> children;
	Vector<BAR *> bars_nonpref32;
	Vector<BAR *> bars_pref32;
	Vector<BAR *> bars_pref64;
	Vector<BAR *> bars_io;
public:
	Device *parent; // only a bridge in practise
	uint8_t bus, dev, fn;
	static Allocator *alloc;

	Device(const sci_t _sci, Device *_parent, const uint8_t _bus, const uint8_t _dev, const uint8_t _fn):
		sci(_sci), parent(_parent), bus(_bus), dev(_dev), fn(_fn)
	{
		// add as parent's child
		if (parent)
			parent->children.push_back(this);
	}

	void add(BAR *bar)
	{
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

	void add(Device *device)
	{
		children.push_back(device);
	}

	void classify()
	{
		for (Device **d = children.elements; d < children.limit; d++)
			(*d)->classify();

		// if there are 64-bit prefetchable BARs and 32-bit ones, assign 64-bit ones in 32-bit space, as bridge only supports one prefetchable range
		if (bars_pref64.size() && bars_pref32.size()) {
			printf("demoting 64-bit perf BARs\n");
			while (bars_pref64.size()) {
				BAR *bar = bars_pref64.pop();
				bars_pref32.push_back(bar);
			}
		}
	}

	void assign() const
	{
		// clear remote IO BARs
		// FIXME check for master correctly
		if (sci != 0x000)
			for (BAR **bar = bars_io.elements; bar < bars_io.limit; bar++)
				(*bar)->assign(0);

		// FIXME sort
		// std::sort(bars_nonpref32.begin(), bars_nonpref32.end(), compare1);
		for (BAR **bar = bars_nonpref32.elements; bar < bars_nonpref32.limit; bar++) {
			uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len);
			(*bar)->assign(addr);
		}

		// FIXME sort
		// std::sort(bars_pref32.begin(), bars_pref32.end(), compare2);
		for (BAR **bar = bars_pref32.elements; bar < bars_pref32.limit; bar++) {
			uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len);
			(*bar)->assign(addr);
		}

		// FIXME sort
		// std::sort(bars_pref64.begin(), bars_pref64.end(), compare1);
		for (BAR **bar = bars_pref64.elements; bar < bars_pref64.limit; bar++) {
			uint64_t addr = alloc->alloc((*bar)->s64, (*bar)->pref, (*bar)->len);
			(*bar)->assign(addr);
		}

		for (Device **d = children.elements; d < children.limit; d++)
			(*d)->assign();
	}

	void print() const;
};

class Endpoint: public Device
{
public:
	Endpoint(const sci_t sci, Device *parent, const uint8_t bus, const uint8_t dev, const uint8_t fn): Device(sci, parent, bus, dev, fn)
	{}
};

class Bridge: public Device
{
public:
	Bridge(const sci_t sci, Device *parent, const uint8_t bus, const uint8_t dev, const uint8_t fn): Device(sci, parent, bus, dev, fn)
	{}
};

extern void pci_realloc();
