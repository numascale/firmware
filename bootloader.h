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

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include <inttypes.h>
#include <stdbool.h>

#include "platform/config.h"
#include "platform/syslinux.h"
#include "platform/options.h"
#include "library/access.h"
#include "opteron/opteron.h"
#include "numachip2/numachip.h"

class Nodes
{
	struct ht {
		uint32_t base;		/* Start of DRAM at individual HT nodes, in 16MB chunks */
		uint32_t size;		/* Amount of DRAM at individual HT nodes, in 16MB chunks */
		uint16_t pdom;		/* Proximity domain of individual HT nodes */
		uint16_t cores;		/* Number of cores at individual HT nodes */
		uint16_t apic_base;
		uint32_t scrub;
	};

	struct node {
		sci_t sci;                  /* Maps logical DNC node ids to physical (SCI) ids */
		uint16_t apic_offset;       /* Offset to shift APIC ids by when unifying */
		uint32_t memory;            /* Amount of DRAM at dnc nodes, in 16MB chunks */
		uint32_t dram_base, dram_limit;
		uint32_t mmio32_base, mmio32_limit;
		uint64_t mmio64_base, mmio64_limit;
		uint32_t io_base, io_limit;
		ht_t bsp_ht : 3;            /* Bootstrap processor HT ID (may be renumbered) */
		ht_t nb_ht_lo : 3;          /* Lowest Northbridge HT ID */
		ht_t nb_ht_hi : 3;          /* Highest Northbridge HT ID */
		ht_t nc_ht : 3;             /* HT id of Numachip */
		ht_t nc_neigh_ht : 3;       /* Nearest northbridge to Numachip */
		uint8_t nc_neigh_link : 2;
		struct ht ht[8];
	};
public:
	Nodes(const int nnodes, const sci_t sci);
};

/* Global constants found in initialization */
extern Syslinux *syslinux;
extern Options *options;
extern Config *config;
extern Opteron *opteron;
extern Numachip2 *numachip;
extern Nodes *nodes;

void udelay(const uint32_t usecs);
void wait_key(void);
int smbios_parse(const char **biosver, const char **biosdate,
		 const char **sysmanuf, const char **sysproduct,
		 const char **boardmanuf, const char **boardproduct);

#endif
