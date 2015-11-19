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

#include "../node.h"
#include "../library/base.h"
#include "../platform/options.h"
#include "../opteron/opteron.h"

#define ALLOC_UNALLOCATED 0xffff
#define ALLOC_LOCAL       0xfffe // LAPIC etc

/*
SETUP PHASES
 1. the tree is built with all BARs added to 32-bit pref, 32-bit non-pref and 64-bit pref lists in the parent bridge
 2. at every bridge, the path to the root bridge is searched for 32-bit prefetchable BARs
  > if any 32-bit pref are found, the subtree and parent BARs are moved from 64-bit to 32-bit
 3. BARs are sorted and assigned addresses, and bridges ranges assigned
 4. setup maps and decode ranges

NORTHBRIDGE MMIO MAP
 1. legacy VGA decode (0xa0000:0xbffff to 0.1)
 2. remote non-prefetchable bottom (starting 0x40000000 to 6.0)
 3. local non-prefetchable mid (to 0.1)
 4. remote non-prefetchable top and remote prefetchable bottom (to 6.0)
 5. local prefetchable mid (to 0.1)
 6. remote prefetchable top (ending 0xdfffffff to 6.0)
 7. legacy MCFG range for SMM (0xe0000000:0xefffffff to 0.1)
 8. NumaChip local (0xf0000000:0xf0ffffff to 6.0)
 9. top of MMIO space (0xf1000000:0xffffffff to 0.1)
 10. global config space (0x3f0000000000:0x3fffffffffff to 6.0)
*/

class Allocator
{
	sci_t *map32;
	unsigned blocks;
public:
	const uint64_t start32, end32, start64;
	uint64_t pos32_pref, pos32_nonpref, pos64;

	Allocator(const uint64_t _start32, const uint64_t _start64);
	void reserve(const uint64_t addr, const uint64_t len, const sci_t sci);
	uint64_t alloc(const bool s64, const bool pref, const uint64_t len, const unsigned vfs);
	void maps32(Node *const node);
	void round_node();
};

class BAR
{
	uint64_t addr;
public:
	const bool io, s64, pref;
	const uint64_t len;
	const unsigned vfs;

	BAR(const bool _io, const bool _s64, const bool _pref, const uint64_t _len, const uint64_t _addr, const unsigned _vfs):
		addr(_addr), io(_io), s64(_s64), pref(_pref), len(_len), vfs(_vfs)
	{}
	void assign(const uint64_t _addr)
	{
		addr = _addr;
	}
	void print(void) const;
	bool operator<(const BAR &rhs) const
	{
		return len < rhs.len;
	}
};

class Device
{
	Vector<Device *> children;
	Vector<BAR *> bars_nonpref32;
	Vector<BAR *> bars_pref32;
	Vector<BAR *> bars_pref64;
	Vector<BAR *> bars_io;
	const bool bridge;
public:
	Node *node;
	Device *parent; // only a bridge in practise
	uint8_t bus, dev, fn;
	static Allocator *alloc;

	Device(Node *const _node, Device *_parent, const uint8_t _bus, const uint8_t _dev, const uint8_t _fn, const bool _bridge):
		bridge(_bridge), node(_node), parent(_parent), bus(_bus), dev(_dev), fn(_fn)
	{
		// add as parent's child
		if (parent)
			parent->children.push_back(this);
	}

	void add(BAR *bar);
	void add(Device *device)
	{
		children.push_back(device);
	}

	void classify();
	void assign();
	void print() const;
};

class Endpoint: public Device
{
public:
	Endpoint(Node *const _node, Device *_parent, const uint8_t _bus, const uint8_t _dev, const uint8_t _fn): Device(_node, _parent, _bus, _dev, _fn, 0)
	{
#ifdef DEBUG
		printf("device @ %02x:%02x.%x\n", _bus, _dev, _fn);
#endif

	}
};

class Bridge: public Device
{
public:
	Bridge(Node *const _node, Device *_parent, const uint8_t _bus, const uint8_t _dev, const uint8_t _fn): Device(_node, _parent, _bus, _dev, _fn, 1)
	{
#ifdef DEBUG
		printf("bridge @ %02x:%02x.%x\n", _bus, _dev, _fn);
#endif
	}
};

extern void pci_realloc();
