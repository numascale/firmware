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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <console.h>
#include <com32.h>
#include <inttypes.h>
#include <syslinux/pxe.h>
#include <sys/io.h>

#include "nc2-defs.h"
#include "nc2-bootloader.h"
#include "nc2-access.h"

static void mmio_range(const int ht, uint8_t range, uint64_t base, uint64_t limit, const int dest)
{
	if (verbose > 1)
		printf("Adding MMIO range %d on HT#%x from 0x%012llx to 0x%012llx towards %d\n",
		       range, ht, base, limit, dest);

	if (family >= 0x15) {
		assert(range < 12);

		int loff = 0, hoff = 0;
		if (range > 7) {
			loff = 0xe0;
			hoff = 0x20;
		}

		uint32_t val = cht_readl(ht, FUNC1_MAPS, 0x80 + loff + range * 8);
		if (val & (1 << 3))
			return; /* Locked */
		assert((val & 3) == 0); /* Unused */

		cht_writel(ht, FUNC1_MAPS, 0x180 + hoff + range * 4, ((limit >> 40) << 16) | (base >> 40));
		cht_writel(ht, FUNC1_MAPS, 0x84 + loff + range * 8, ((limit >> 16) << 8) | dest);
		cht_writel(ht, FUNC1_MAPS, 0x80 + loff + range * 8, ((base >> 16) << 8 | 3));
		return;
	}

	/* Family 10h */
	if (range < 8) {
		assert(limit < (1ULL << 40));
		uint32_t val = cht_readl(ht, FUNC1_MAPS, 0x80 + range * 8);
		if (val & (1 << 3))
			return; /* Locked */
		assert((val & 3) == 0); /* Unused */

		cht_writel(ht, FUNC1_MAPS, 0x84 + range * 8, ((limit >> 16) << 8) | dest);
		cht_writel(ht, FUNC1_MAPS, 0x80 + range * 8, ((base >> 16) << 8 | 3));
		return;
	}

	assert(range < 12);
	range -= 8;

	/* Reading an uninitialised extended MMIO ranges results in MCE, so can't assert */

	uint64_t mask = 0;
	base  >>= 27;
	limit >>= 27;

	while ((base | mask) != (limit | mask))
		mask = (mask << 1) | 1;

	cht_writel(ht, FUNC1_MAPS, 0x110, (2 << 28) | range);
	cht_writel(ht, FUNC1_MAPS, 0x114, (base << 8) | dest);
	cht_writel(ht, FUNC1_MAPS, 0x110, (3 << 28) | range);
	cht_writel(ht, FUNC1_MAPS, 0x114, (mask << 8) | 1);
}

static uint32_t get_phy_register(int node, int link, int idx, int direct)
{
	int base = 0x180 + link * 8;
	int i;
	uint32_t reg;
	cht_writel(node, FUNC4_LINK, base, idx | (direct << 29));

	for (i = 0; i < 1000; i++) {
		reg = cht_readl(node, FUNC4_LINK, base);
		if (reg & 0x80000000)
			return cht_readl(node, FUNC4_LINK, base + 4);
	}

	printf("Read from phy register HT#%d F4x%x idx %x did not complete\n",
	       node, base, idx);
	return 0;
}

static void cht_print(int neigh, int link)
{
	uint32_t val;
	printf("HT#%d L%d Link Control       : 0x%08x\n", neigh, link,
	      cht_readl(neigh, FUNC0_HT, 0x84 + link * 0x20));
	printf("HT#%d L%d Link Freq/Revision : 0x%08x\n", neigh, link,
	       cht_readl(neigh, FUNC0_HT, 0x88 + link * 0x20));
	printf("HT#%d L%d Link Ext. Control  : 0x%08x\n", neigh, link,
	       cht_readl(neigh, 0, 0x170 + link * 4));
	val = get_phy_register(neigh, link, 0xe0, 0); /* Link phy compensation and calibration control 1 */
	printf("HT#%d L%d Link Phy Settings  : Rtt=%d Ron=%d\n", neigh, link, (val >> 23) & 0x1f, (val >> 18) & 0x1f);
}

static void ht_optimize_link(int nc, int neigh, int link)
{
	bool reboot = false;
	int ganged;
	uint32_t val;

	if ((neigh < 0) || (link < 0)) {
		int next, i;
		uint32_t rqrt;
		/* Start looking from node 0 */
		neigh = 0;

		while (1) {
			next = 0;
			rqrt = cht_readl(neigh, FUNC0_HT, 0x40 + 4 * nc) & 0x1f;

			/* Look for other CPUs routed on same link as NC */
			for (i = 0; i < nc; i++) {
				if (rqrt == (cht_readl(neigh, FUNC0_HT, 0x40 + 4 * i) & 0x1f)) {
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

	ganged = cht_readl(neigh, 0, 0x170 + link * 4) & 1;
	printf("Found %s link to NC on HT#%d L%d\n", ganged ? "ganged" : "unganged", neigh, link);

	cht_print(neigh, link);

	printf("Checking HT width/freq.");

	/* Gang link when appropriate, as the BIOS may not */
	printf("*");
	val = cht_readl(neigh, FUNC0_HT, 0x170 + link * 4);
	printf(".");
	if ((val & 1) == 0) {
		printf("<ganging>");
		cht_writel(neigh, FUNC0_HT, 0x170 + link * 4, val | 1);
		reboot = true;
	}

	/* Optimize width (16b) */
	printf("+");
	val = cht_readl(nc, 0, NC2_F0_LINK_CONTROL_REGISTER);
	printf(".");

	if (!ht_8bit_only && (ganged && ((val >> 16) == 0x11))) {
		if ((val >> 24) != 0x11) {
			printf("<NC width>");
			cht_writel(nc, 0, NC2_F0_LINK_CONTROL_REGISTER, (val & 0x00ffffff) | 0x11000000);
			reboot = true;
		}

		printf(".");
		val = cht_readl(neigh, FUNC0_HT, 0x84 + link * 0x20);
		printf(".");

		if ((val >> 24) != 0x11) {
			printf("<CPU width>");
			cht_writel(neigh, FUNC0_HT, 0x84 + link * 0x20, (val & 0x00ffffff) | 0x11000000);
			reboot = true;
		}
	}

	/* Optimize link frequency, if option to disable this is not set */
	if (!ht_200mhz_only) {
		printf("+");
		val = cht_readl(nc, 0, NC2_F0_LINK_FREQUENCY_REVISION_REGISTER);
		printf(".");

		if (((val >> 8) & 0xf) != 0x2) {
			printf("<NC freq>");
			cht_writel(nc, 0, NC2_F0_LINK_FREQUENCY_REVISION_REGISTER, (val & ~0xf00) | (0x2 << 8));
			reboot = true;
		}

		printf(".");
		val = cht_readl(neigh, FUNC0_HT, 0x88 + link * 0x20);
		printf(".");

		if (((val >> 8) & 0xf) != 0x2) {
			printf("<CPU freq>");
			cht_writel(neigh, FUNC0_HT, 0x88 + link * 0x20, (val & ~0xf00) | (0x2 << 8));
			reboot = true;
		}
	}

	printf("done\n");

	if (reboot) {
		printf("Rebooting to make new link settings effective...\n");
		/* Ensure last lines were sent from management controller */
		fflush(stdout);
		fflush(stderr);
		udelay(2500000);
		reset_cf9(2, nc - 1);
	}
}

int ht_fabric_fixup(uint32_t *p_chip_rev)
{
	int nodes, nc = -1;
	uint32_t val;
	const uint64_t bar0 = 0xf0000000ULL;

	val = cht_readl(0, FUNC0_HT, 0x60);
	nodes = (val >> 4) & 7;

	/* Check the last cHT node for our VID/DID incase it's already been included in the cHT fabric */
	val = cht_readl(nodes, 0, NC2_F0_DEVICE_VENDOR_ID_REGISTER);
	if (val == VENDEV_NC2) {
		nc = nodes;
		*p_chip_rev = cht_readl(nc, 0, NC2_F0_CLASS_CODE_REVISION_ID_REGISTER) & 0xffff;
		printf("NumaChip-II rev %d already present on HT node %d\n", *p_chip_rev, nc);
		/* Chip already found; make sure the desired width/frequency is set */
		ht_optimize_link(nc, -1, -1);
	}
	else {
		/* Last node wasn't our VID/DID, try to look for it */
		int neigh, link = 0, rt, i;
		bool use = true;

		for (neigh = 0; neigh <= nodes; neigh++) {
			uint32_t aggr = cht_readl(neigh, FUNC0_HT, 0x164);

			for (link = 0; link < 4; link++) {
				val = cht_readl(neigh, FUNC0_HT, 0x98 + link * 0x20);

				if ((val & 0x1f) != 0x3)
					continue; /* Not coherent */

				use = false;

				if (aggr & (0x10000 << link))
					use = true;

				for (rt = 0; rt <= nodes; rt++) {
					val = cht_readl(neigh, FUNC0_HT, 0x40 + rt * 4);

					if (val & (2 << link))
						use = true; /* Routing entry "rt" uses link "link" */
				}

				if (!use)
					break;
			}

			if (!use)
				break;
		}

		if (use) {
			printf("Error: No unrouted coherent links found\n");
			return -1;
		}

		printf("HT#%d L%d is coherent and unrouted\n", neigh, link);

		nc = nodes + 1;
		/* "neigh" request/response routing, copy bcast values from self */
		val = cht_readl(neigh, FUNC0_HT, 0x40 + neigh * 4);
		cht_writel(neigh, FUNC0_HT, 0x40 + nc * 4,
			   (val & 0x07fc0000) | (0x402 << link));

		for (i = 0; i <= nodes; i++) {
			val = cht_readl(i, FUNC0_HT, 0x68);
			cht_writel(i, FUNC0_HT, 0x68, val & ~(1 << 15)); /* LimitCldtCfg */

			if (i == neigh)
				continue;

			/* Route "nc" same as "neigh" for all other nodes */
			val = cht_readl(i, FUNC0_HT, 0x40 + neigh * 4);
			cht_writel(i, FUNC0_HT, 0x40 + nc * 4, val);
		}

		val = cht_readl(nc, 0, NC2_F0_DEVICE_VENDOR_ID_REGISTER);
		if (val != VENDEV_NC2) {
			printf("Error: Unrouted coherent device %08x is not NumaChip-II\n", val);
			for (i = 0; i <= nodes; i++) {
				/* Reassert LimitCldtCfg */
				val = cht_readl(i, FUNC0_HT, 0x68);
				cht_writel(i, FUNC0_HT, 0x68, val | (1 << 15));
			}
			return -1;
		}

		*p_chip_rev = cht_readl(nc, 0, NC2_F0_CLASS_CODE_REVISION_ID_REGISTER) & 0xffff;
		printf("NumaChip-II rev %d found connected to HT%d L%d\n", *p_chip_rev, neigh, link);

		/* Ramp up link speed and width before adding to coherent fabric */
		ht_optimize_link(nc, neigh, link);

		printf("Adjusting HT fabric...");
		critical_enter();

		for (i = nodes; i >= 0; i--) {
			uint32_t ltcr, val2;
			/* Disable probes while adjusting */
			ltcr = cht_readl(i, FUNC0_HT, 0x68);
			cht_writel(i, FUNC0_HT, 0x68,
				   ltcr | (1 << 10) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0));
			/* Update "neigh" bcast values for node about to increment fabric size */
			val = cht_readl(neigh, FUNC0_HT, 0x40 + i * 4);
			val2 = cht_readl(i, FUNC0_HT, 0x60);
			cht_writel(neigh, FUNC0_HT, 0x40 + i * 4, val | (0x80000 << link));
			/* FIXME: Race condition observered to cause lockups at this point */
			/* Increase fabric size */
			cht_writel(i, FUNC0_HT, 0x60, val2 + (1 << 4));
			/* Reassert LimitCldtCfg */
			cht_writel(i, FUNC0_HT, 0x68, ltcr | (1 << 15));
		}

		critical_leave();
		printf("done\n");
	}

	val = cht_readl(0, FUNC0_HT, 0x60);
	cht_writel(nc, 0,
		   NC2_F0_NODE_ID_REGISTER,
		   (((val >> 12) & 7) << 24) | /* LkNode */
		   (((val >> 8)  & 7) << 16) | /* SbNode */
		   (nc << 8) | /* NodeCnt */
		   nc); /* NodeId */

	printf("Setting MemorySpaceEnable\n");
	cht_writel(nc, 0, NC2_F0_STATUS_COMMAND_REGISTER, 2);

	printf("Setting BAR0 to %012llx\n", bar0);
	cht_writel(nc, 0, NC2_F0_BASE_ADDRESS_REGISTER_0, bar0 & 0xff000000);
	cht_writel(nc, 0, NC2_F0_BASE_ADDRESS_REGISTER_0+4, bar0 >> 32);

	printf("Setting HT maps...\n");
	for (int i = 0; i <= nodes; i++)
		mmio_range(i, 7, bar0, bar0 + 0xffffffULL, nc);

	return nc;
}


