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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/io.h>

#include "../platform/acpi.h"
#include "../bootloader.h"
#include "../platform/options.h"
#include "../library/access.h"

void Opteron::reset(const enum reset mode, const int last)
{
	/* Prevent some BIOSs reprogramming the link back to 200MHz */
	for (int i = 0; i <= last; i++) {
		uint32_t val = lib::cht_read32(i, LINK_INIT_CTRL);
		val &= ~(1 << 5);
		lib::cht_write32(i, LINK_INIT_CTRL, val);
	}

	/* Ensure console drains */
	lib::udelay(1000000);

	if (mode == Warm) {
		outb((0 << 3) | (0 << 2) | (1 << 1), 0xcf9);
		outb((0 << 3) | (1 << 2) | (1 << 1), 0xcf9);
	}

	/* Cold reset */
	outb((1 << 3) | (0 << 2) | (1 << 1), 0xcf9);
	outb((1 << 3) | (1 << 2) | (1 << 1), 0xcf9);
}

/* Mask southbridge SMI generation */
void Opteron::disable_smi(void)
{
	switch (ioh_vendev) {
	case VENDEV_SR5690:
	case VENDEV_SR5670:
	case VENDEV_SR5650:
		const uint8_t state = lib::pmio_read8(0x53);
		lib::pmio_write8(0x53, state | (1 << 3));
	}
}

/* Restore previous southbridge SMI mask */
void Opteron::enable_smi(void)
{
	switch (ioh_vendev) {
	case VENDEV_SR5690:
	case VENDEV_SR5670:
	case VENDEV_SR5650:
		const uint8_t state = lib::pmio_read8(0x53);
		lib::pmio_write8(0x53, state & ~(1 << 3));
	}
}

void Opteron::critical_enter(void)
{
	cli();
	disable_smi();
}

void Opteron::critical_leave(void)
{
	enable_smi();
	sti();
}

uint32_t Opteron::get_phy_register(const ht_t ht, const link_t link, const int idx, const bool direct)
{
	const int base = 0x180 + link * 8;
	uint32_t reg;
	lib::cht_write32(ht, 0x4000 | base, idx | (direct << 29));

	for (int i = 0; i < 1000; i++) {
		reg = lib::cht_read32(ht, 0x4000 | base);
		if (reg & 0x80000000)
			return lib::cht_read32(ht, 0x4000 | (base + 4));
		cpu_relax();
	}

	printf("Read from phy register HT#%d F4x%x idx %x did not complete\n", ht, base, idx);
	return 0;
}

void Opteron::cht_print(int neigh, int link)
{
	uint32_t val;
	printf("HT#%d L%d Link Control       : 0x%08x\n", neigh, link,
	      lib::cht_read32(neigh, LINK_CTRL + link * 0x20));
	printf("HT#%d L%d Link Freq/Revision : 0x%08x\n", neigh, link,
	       lib::cht_read32(neigh, LINK_FREQ_REV + link * 0x20));
	printf("HT#%d L%d Link Ext. Control  : 0x%08x\n", neigh, link,
	       lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4));
	val = get_phy_register(neigh, link, 0xe0, 0); /* Link phy compensation and calibration control 1 */
	printf("HT#%d L%d Link Phy Settings  : Rtt=%d Ron=%d\n", neigh, link, (val >> 23) & 0x1f, (val >> 18) & 0x1f);
}

void Opteron::ht_optimize_link(int nc, int neigh, int link)
{
	bool reboot = 0;
	int ganged;
	uint32_t val;

	if ((neigh < 0) || (link < 0)) {
		int i;
		uint32_t rqrt;
		/* Start looking from node 0 */
		neigh = 0;

		while (1) {
			int next = 0;
			rqrt = lib::cht_read32(neigh, ROUTING + 4 * nc) & 0x1f;

			/* Look for other CPUs routed on same link as NC */
			for (i = 0; i < nc; i++) {
				if (rqrt == (lib::cht_read32(neigh, ROUTING + 4 * i) & 0x1f)) {
					next = i;
					break;
				}
			}

			if (next > 0)
				neigh = next;
			else
				break;
		}

		link = 0;

		while ((2U << link) < rqrt)
			link ++;
	}

	ganged = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4) & 1;
	printf("Found %s link to NC on HT#%d L%d\n", ganged ? "ganged" : "unganged", neigh, link);

	if (options->debug.ht)
		cht_print(neigh, link);

	printf("Checking HT width/freq.");

	/* Gang link when appropriate, as the BIOS may not */
	printf("*");
	val = lib::cht_read32(neigh, LINK_EXT_CTRL + link * 4);
	printf(".");
	if ((val & 1) == 0) {
		printf("<ganging>");
		lib::cht_write32(neigh, LINK_EXT_CTRL + link * 4, val | 1);
		reboot = 1;
	}

	/* Optimize width (16b) */
	printf("+");
	val = lib::cht_read32(nc, Numachip2::LINK_CTRL);
	printf(".");

	if (!options->ht_8bit_only && (ganged && ((val >> 16) == 0x11))) {
		if ((val >> 24) != 0x11) {
			printf("<NC width>");
			lib::cht_write32(nc, Numachip2::LINK_CTRL, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}

		printf(".");
		val = lib::cht_read32(neigh, LINK_CTRL + link * 0x20);
		printf(".");

		if ((val >> 24) != 0x11) {
			printf("<CPU width>");
			lib::cht_write32(neigh, LINK_CTRL + link * 0x20, (val & 0x00ffffff) | 0x11000000);
			reboot = 1;
		}
	}

	/* Optimize link frequency, if option to disable this is not set */
	if (!options->ht_200mhz_only) {
		uint8_t max_supported = 0;

		printf("+");
		val = lib::cht_read32(nc, Numachip2::LINK_FREQ_REV);
		printf(".");

		/* Find maximum supported frequency */
		for (int i = 0; i < 16; i++)
			if (val >> (16+i) & 1) max_supported = i;

		if (((val >> 8) & 0xf) != max_supported) {
			printf("<NC freq=%d>",max_supported);
			lib::cht_write32(nc, Numachip2::LINK_FREQ_REV, (val & ~0xf00) | (max_supported << 8));
			reboot = 1;
		}

		printf(".");
		val = lib::cht_read32(neigh, LINK_FREQ_REV + link * 0x20);
		printf(".");

		if (((val >> 8) & 0xf) != max_supported) {
			printf("<CPU freq=%d>",max_supported);
			lib::cht_write32(neigh, LINK_FREQ_REV + link * 0x20, (val & ~0xf00) | (max_supported << 8));
			reboot = 1;
		}
	}

	printf("\n");

	if (!options->fast && reboot) {
		printf("Rebooting to make new link settings effective...\n");
		reset(Warm, nc - 1);
		/* Does not return */
	}
}

ht_t Opteron::ht_fabric_fixup(const uint32_t vendev)
{
	ht_t nc;
	uint32_t val = lib::cht_read32(0, HT_NODE_ID);
	ht_t nnodes = (val >> 4) & 7;

	/* Check the last cHT node for our VID/DID incase it's already been included in the cHT fabric */
	val = lib::cht_read32(nnodes, Numachip2::VENDEV);
	if (val == vendev) {
		nc = nnodes;
		uint16_t rev = lib::cht_read32(nc, Numachip2::CLASS_CODE_REV) & 0xffff;
		printf("NumaChip2 rev %d already at HT%d\n", rev, nc);
		/* Chip already found; make sure the desired width/frequency is set */
		ht_optimize_link(nc, -1, -1);
	} else {
		/* Last node wasn't our VID/DID, try to look for it */
		int neigh, link = 0, rt, i;
		bool use = 1;

		for (neigh = 0; neigh <= nnodes; neigh++) {
			uint32_t aggr = lib::cht_read32(neigh, COH_LINK_TRAF_DIST);

			for (link = 0; link < 4; link++) {
				val = lib::cht_read32(neigh, LINK_TYPE + link * 0x20);
				uint32_t val2 = lib::cht_read32(neigh, LINK_CTRL + link * 0x20);
				if (options->debug.ht)
					printf("HT%d.%d LinkControl = 0x%08x\n", neigh, link, val2);
				if ((val & 0x1f) != 0x3)
					continue; /* Not coherent */

				use = 0;

				if (aggr & (0x10000 << link))
					use = 1;

				for (rt = 0; rt <= nnodes; rt++) {
					val = lib::cht_read32(neigh, ROUTING + rt * 4);

					if (val & (2 << link))
						use = 1; /* Routing entry "rt" uses link "link" */
				}

				if (!use)
					break;
			}

			if (!use)
				break;
		}

		if (use) {
			printf("Error: No unrouted coherent links found, issuing COLD reboot\n");
			reset(Cold, nnodes);
			/* Does not return */
		}

		printf("HT#%d L%d is coherent and unrouted\n", neigh, link);

		nc = nnodes + 1;
		/* "neigh" request/response routing, copy bcast values from self */
		val = lib::cht_read32(neigh, ROUTING + neigh * 4);
		lib::cht_write32(neigh, ROUTING + nc * 4,
			   (val & 0x07fc0000) | (0x402 << link));

		for (i = 0; i <= nnodes; i++) {
			val = lib::cht_read32(i, LINK_TRANS_CTRL);
			lib::cht_write32(i, LINK_TRANS_CTRL, val & ~(1 << 15)); /* LimitCldtCfg */

			if (i == neigh)
				continue;

			/* Route "nc" same as "neigh" for all other nodes */
			val = lib::cht_read32(i, ROUTING + neigh * 4);
			lib::cht_write32(i, ROUTING + nc * 4, val);
		}

		val = lib::cht_read32(nc, Numachip2::VENDEV);
		assertf(val == vendev, "Unrouted coherent device %08x is not NumaChip2\n", val);

		uint16_t rev = lib::cht_read32(nc, Numachip2::CLASS_CODE_REV) & 0xffff;
		printf("NumaChip2 rev %d found on HT%d.%d\n", rev, neigh, link);

		/* Ramp up link speed and width before adding to coherent fabric */
		ht_optimize_link(nc, neigh, link);

		printf("Adjusting HT fabric");

		critical_enter();

		for (i = nnodes; i >= 0; i--) {
			uint32_t ltcr, val2;
			/* Disable probes while adjusting */
			ltcr = lib::cht_read32(i, LINK_TRANS_CTRL);
			lib::cht_write32(i, LINK_TRANS_CTRL,
				   ltcr | (1 << 10) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0));
			/* Update "neigh" bcast values for node about to increment fabric size */
			val = lib::cht_read32(neigh, ROUTING + i * 4);
			val2 = lib::cht_read32(i, HT_NODE_ID);
			lib::cht_write32(neigh, ROUTING + i * 4, val | (0x80000 << link));
			/* FIXME: Race condition observered to cause lockups at this point */
			/* Increase fabric size */
			lib::cht_write32(i, HT_NODE_ID, val2 + (1 << 4));
			/* Reassert LimitCldtCfg */
			lib::cht_write32(i, LINK_TRANS_CTRL, ltcr | (1 << 15));
		}

		critical_leave();
		printf("\n");
	}

	val = lib::cht_read32(0, HT_NODE_ID);
	lib::cht_write32(nc, Numachip2::HT_NODE_ID,
		   (((val >> 12) & 7) << 24) | /* LkNode */
		   (((val >> 8)  & 7) << 16) | /* SbNode */
		   (nc << 8) | /* NodeCnt */
		   nc); /* NodeId */

	return nc;
}

