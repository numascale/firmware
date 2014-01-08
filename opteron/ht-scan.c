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

#include "../opteron/defs.h"
#include "../platform/acpi.h"
#include "../bootloader.h"
#include "../platform/options.h"
#include "../library/access.h"

#define HT_INIT_CONTROL		0x6C
#define HTIC_BIOSR_Detect	(1<<5)

enum reboot_mode {
	REBOOT_WARM,
	REBOOT_COLD,
};

static void reset_cf9(enum reboot_mode mode, int last)
{
	for (int i = 0; i <= last; i++) {
		uint32_t val = cht_readl(i, FUNC0_HT, HT_INIT_CONTROL);
		val &= ~HTIC_BIOSR_Detect;
		cht_writel(i, FUNC0_HT, HT_INIT_CONTROL, val);
	}
	if (southbridge_id == VENDEV_SP5100) {
		uint8_t pm_control = pmio_readb(0x42);
		pm_control |= (1 << 1); /* Clear the DisableBootFailCpuRst bit */
		printf("pm_control = %02x\n", pm_control);
		pmio_writeb(0x42, pm_control | (1 << 3)); /* Set the RstCpuPGEn bit to toggle PWROK */
		wait_key();
	}
	if (mode == REBOOT_COLD) {
#if 0
		if (southbridge_id == VENDEV_SP5100) {
			uint8_t pm_reg_04 = pmio_readb(0x04);
			printf("pm_reg_04 = %02x\n", pm_reg_04);
			pmio_writeb(0x04, pm_reg_04 | (1 << 7)); /* Enable SMI# on sleep command */

			uint8_t pm_reg_41 = pmio_readb(0x41);
			printf("pm_reg_41 = %02x\n", pm_reg_41);
			pmio_writeb(0x41, pm_reg_41 | (1 << 3)); /* Deassert PWROK when entering S3 */

			uint8_t pm_reg_8d = pmio_readb(0x8d);
			printf("pm_reg_8d = %02x\n", pm_reg_8d);
			pmio_writeb(0x8d, pm_reg_8d | (1 << 5)); /* Extend SLP_S3# */

			uint16_t AcpiPm1CntBlk = ((uint16_t)pmio_readb(0x23) << 8) | (uint16_t)pmio_readb(0x22);
			printf("AcpiPm1CntBlk = %04x\n", AcpiPm1CntBlk);
			uint16_t pm_control = inw(AcpiPm1CntBlk);
			printf("pm_control = %04x\n", pm_control);

			wait_key();
			fflush(stdout);
			fflush(stderr);
			udelay(2500000);
			outw(pm_control | (3 << 10) | (1 << 13), AcpiPm1CntBlk); /* Enter S3 state */
			pm_control = inw(AcpiPm1CntBlk);
			printf("pm_control = %04x\n", pm_control);
		}
#endif
		outb((1 << 3) | (0 << 2) | (1 << 1), 0xcf9);
		outb((1 << 3) | (1 << 2) | (1 << 1), 0xcf9);
	} else {
		outb((0 << 3) | (0 << 2) | (1 << 1), 0xcf9);
		outb((0 << 3) | (1 << 2) | (1 << 1), 0xcf9);
	}
}

static uint8_t smi_state;

/* Mask southbridge SMI generation */
static void disable_smi(void)
{
	if (southbridge_id == VENDEV_SP5100) {
		smi_state = pmio_readb(0x53);
		pmio_writeb(0x53, smi_state | (1 << 3));
	}
}

/* Restore previous southbridge SMI mask */
static void enable_smi(void)
{
	if (southbridge_id == VENDEV_SP5100) {
		pmio_writeb(0x53, smi_state);
	}
}

static void critical_enter(void)
{
	cli();
	disable_smi();
}

static void critical_leave(void)
{
	enable_smi();
	sti();
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
		int i;
		uint32_t rqrt;
		/* Start looking from node 0 */
		neigh = 0;

		while (1) {
			int next = 0;
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

	if (!options->ht_8bit_only && (ganged && ((val >> 16) == 0x11))) {
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
	if (!options->ht_200mhz_only) {
		uint8_t max_supported = 0;

		printf("+");
		val = cht_readl(nc, 0, NC2_F0_LINK_FREQUENCY_REVISION_REGISTER);
		printf(".");

		/* Find maximum supported frequency */
		for (int i = 0; i < 16; i++)
			if (val >> (16+i) & 1) max_supported = i;

		if (((val >> 8) & 0xf) != max_supported) {
			printf("<NC freq=%d>",max_supported);
			cht_writel(nc, 0, NC2_F0_LINK_FREQUENCY_REVISION_REGISTER, (val & ~0xf00) | (max_supported << 8));
			reboot = true;
		}

		printf(".");
		val = cht_readl(neigh, FUNC0_HT, 0x88 + link * 0x20);
		printf(".");

		if (((val >> 8) & 0xf) != max_supported) {
			printf("<CPU freq=%d>",max_supported);
			cht_writel(neigh, FUNC0_HT, 0x88 + link * 0x20, (val & ~0xf00) | (max_supported << 8));
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
		reset_cf9(REBOOT_WARM, nc - 1);
		/* Does not return */
	}
}

int ht_fabric_fixup(uint32_t *p_chip_rev)
{
	int nodes, nc = -1;
	uint32_t val;

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
	} else {
		/* Last node wasn't our VID/DID, try to look for it */
		int neigh, link = 0, rt, i;
		bool use = true;

		for (neigh = 0; neigh <= nodes; neigh++) {
			uint32_t aggr = cht_readl(neigh, FUNC0_HT, 0x164);

			for (link = 0; link < 4; link++) {
				val = cht_readl(neigh, FUNC0_HT, 0x98 + link * 0x20);
				uint32_t val2 = cht_readl(neigh, FUNC0_HT, 0x84 + link * 0x20);
				printf("HT%d.%d LinkControl = %08x\n", neigh, link, val2);
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
			printf("Error: No unrouted coherent links found, issuing COLD reboot\n");
			/* Ensure last lines were sent from management controller */
			fflush(stdout);
			fflush(stderr);
			udelay(2500000);
			reset_cf9(REBOOT_COLD, nodes);
			/* Does not return */
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

	return nc;
}

