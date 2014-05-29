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

extern "C" {
	#include <com32.h>
}

#include "version.h"
#include "bootloader.h"
#include "library/base.h"
#include "library/access.h"
#include "platform/acpi.h"
#include "platform/options.h"
#include "platform/syslinux.h"
#include "platform/e820.h"
#include "platform/config.h"
#include "platform/acpi.h"
#include "opteron/msrs.h"
#include "numachip2/numachip.h"

Syslinux *syslinux;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;
ACPI *acpi;
char *asm_relocated;

void Node::init(void)
{
	dram_base = -1;

	for (ht_t n = 0; n < nopterons; n++) {
		Opteron *nb = opterons[n];
		if (nb->dram_base < dram_base)
			dram_base = nb->dram_base;

		dram_size += nb->dram_size;
		cores += nb->cores;
	}

	printf("SCI%03x (%lluGB, %u cores)\n", sci, dram_size >> 30, cores);
}

// instantiated for remote nodes
Node::Node(const sci_t _sci, const ht_t ht): sci(_sci), nopterons(ht)
{
	for (ht_t n = 0; n < nopterons; n++)
		opterons[n] = new Opteron(sci, n);

	numachip = new Numachip2(sci, ht);
	init();
}

// instantiated for local nodes
Node::Node(void): sci(SCI_LOCAL)
{
	const ht_t nc = Opteron::ht_fabric_fixup(Numachip2::VENDEV_NC2);
	assertf(nc, "NumaChip2 not found");

	// set SCI ID later once mapping is setup
	numachip = new Numachip2(nc);
	nopterons = nc;

	// Opterons are on all HT IDs before Numachip
	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb] = new Opteron(nb);

	init();
}

void Node::set_sci(const sci_t _sci)
{
	sci = _sci;

	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb]->sci = sci;

	numachip->set_sci(sci);
}

void scan(void)
{
	printf("Map scan:\n");
	uint64_t dram_top = 0;

	// setup local DRAM windows
	for (Node **node = &nodes[0]; node < &nodes[config->nnodes]; node++) {
		(*node)->dram_base = dram_top;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			(*nb)->dram_base = dram_top;
			dram_top += (*nb)->dram_size;

			uint64_t limit = (*nb)->dram_base + (*nb)->dram_size;
			(*node)->numachip->drammap.add((*nb)->ht, (*nb)->dram_base, limit - 1, (*nb)->ht);

			if ((*node)->sci != config->master->sci)
				e820->add((*nb)->dram_base, (*nb)->dram_size, E820::RAM);

			if (options->tracing)
				e820->add(limit - options->tracing, options->tracing, E820::RESERVED);
		}

		// setup Numachip DRAM decoding
		(*node)->numachip->write32(Numachip2::DRAM_BASE, (*node)->dram_base >> 24);
		(*node)->numachip->write32(Numachip2::DRAM_LIMIT, (dram_top - 1) >> 24);

		dram_top = roundup(dram_top, 1ULL << Numachip2::SIU_ATT_SHIFT);
		(*node)->dram_end = dram_top - 1;

		if (options->debug.maps)
			printf("SCI%03x dram_base=0x%llx dram_size=0x%llx dram_end=%llx\n",
				(*node)->sci, (*node)->dram_base, (*node)->dram_size, (*node)->dram_end);
	}

	for (Node **node = &nodes[0]; node < &nodes[config->nnodes]; node++) {
		// route DRAM access in HT fabric
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			unsigned range = 0;

			for (Opteron **dnb = &(*node)->opterons[0]; dnb < &(*node)->opterons[(*node)->nopterons]; dnb++)
				(*nb)->drammap.add(range++, (*dnb)->dram_base, (*dnb)->dram_base + (*dnb)->dram_size - 1, (*dnb)->ht);

			// add below remote DRAM range
			if (node > &nodes[0])
				(*nb)->drammap.add(range++, nodes[0]->dram_base, (*(node - 1))->dram_end, (*node)->numachip->ht);

			if (node < &nodes[config->nnodes - 1])
				(*nb)->drammap.add(range++, (*(node + 1))->dram_base, dram_top - 1, (*node)->numachip->ht);

			// clear rest of ranges
			while (range < (*nb)->drammap.ranges)
				(*nb)->drammap.remove(range++);

			(*nb)->write32(Opteron::DRAM_BASE, (*nb)->dram_base >> 27);
			(*nb)->write32(Opteron::DRAM_LIMIT, (dram_top - 1) >> 27);
		}

		// route DRAM access in NumaConnect fabric
		for (Node **dnode = &nodes[0]; dnode < &nodes[config->nnodes]; dnode++)
			(*node)->numachip->dramatt.range(
			  (*dnode)->dram_base, (*dnode)->dram_end, (*dnode)->sci);
	}

	printf("New DRAM limit %lluGB\n", dram_top >> 30);
	lib::wrmsr(MSR_TOPMEM2, dram_top);
}

void finalise(void)
{
	printf("Clearing DRAM");
	// start clearing DRAM
	for (Node **node = &nodes[1]; node < &nodes[config->nnodes]; node++)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_start();

	// wait for clear to complete
	for (Node **node = &nodes[1]; node < &nodes[config->nnodes]; node++)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_wait();

	printf("\n");

	if (!options->tracing) {
		printf("Enabling scrubbers");

		// enable DRAM scrubbers
		for (Node **node = &nodes[0]; node < &nodes[config->nnodes]; node++)
			for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
				(*nb)->dram_scrub_enable();

		printf("\n");
	}

	AcpiTable mcfg("MCFG");

	// MCFG 'reserved field'
	const uint64_t reserved = 0;
	mcfg.append((const char *)&reserved, sizeof(reserved));

	uint16_t segment = 0;
	for (Node **node = &nodes[0]; node < &nodes[config->nnodes]; node++) {
		struct acpi_mcfg ent = {
			.address = NC_MCFG_BASE | ((uint64_t)(*node)->sci << 28ULL),
			.pci_segment = segment++,
			.start_bus_number = 0,
			.end_bus_number = 255,
			.reserved = 0,
		};
		mcfg.append((const char *)&ent, sizeof(ent));
	}
	acpi->replace(mcfg);
	acpi->check();

	e820->test();
}

int main(const int argc, const char *argv[])
{
	syslinux = new Syslinux(); // needed first for console access

	printf(CLEAR BANNER "NumaConnect unification " VER " at 20%02d-%02d-%02d %02d:%02d:%02d" COL_DEFAULT "\n",
	  lib::rtc_read(RTC_YEAR), lib::rtc_read(RTC_MONTH), lib::rtc_read(RTC_DAY),
	  lib::rtc_read(RTC_HOURS), lib::rtc_read(RTC_MINUTES), lib::rtc_read(RTC_SECONDS));

	printf("Host MAC %02x:%02x:%02x:%02x:%02x:%02x, IP %s, hostname %s\n",
		syslinux->mac[0], syslinux->mac[1], syslinux->mac[2],
		syslinux->mac[3], syslinux->mac[4], syslinux->mac[5],
		inet_ntoa(syslinux->ip), syslinux->hostname ? syslinux->hostname : "<none>");

	options = new Options(argc, argv); // needed before first PCI access
	Opteron::prepare();
	acpi = new ACPI();

	// SMI often assumes HT nodes are Northbridges, so handover early
	if (options->handover_acpi)
		acpi->handover();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	e820 = new E820();
	local_node = new Node();

	// add global MCFG maps
	for (unsigned i = 0; i < local_node->nopterons; i++)
		local_node->opterons[i]->mmiomap.add(9, NC_MCFG_BASE, NC_MCFG_LIM, local_node->numachip->ht, 0);

	// reserve HT decode and MCFG address range so Linux accepts it
	e820->add(Opteron::HT_BASE, Opteron::HT_LIMIT - Opteron::HT_BASE, E820::RESERVED);
	e820->add(NC_MCFG_BASE, NC_MCFG_LIM - NC_MCFG_BASE + 1, E820::RESERVED);

	// setup local MCFG access
	uint64_t val6 = NC_MCFG_BASE | ((uint64_t)config->local_node->sci << 28ULL) | 0x21ULL;
	lib::wrmsr(MSR_MCFG_BASE, val6);

	local_node->set_sci(config->local_node->sci);
	local_node->numachip->fabric_train();

	if (!config->master_local) {
		// set ready flag for master
		local_node->numachip->write32(Numachip2::FABRIC_CTRL, 1 << 30);

		printf("Waiting for SCI%03x/%s", config->master->sci, config->master->hostname);

		while (!(local_node->numachip->read32(Numachip2::FABRIC_CTRL) & (1 << 31)))
			cpu_relax();

		printf(BANNER "\nThis server SCI%03x/%s is part of a %d-server NumaConnect system\n"
		  "Refer to the console on SCI%03x/%s ", config->local_node->sci, config->local_node->hostname,
		  config->nnodes, config->master->sci, config->master->hostname);

		Opteron::disable_smi();

		while (1) {
			cli();
			asm volatile("hlt" ::: "memory");
			printf("wake ");
		}
	}

	nodes = (Node **)zalloc(sizeof(void *) * config->nnodes);
	assert(nodes);
	nodes[0] = local_node;

	int left = config->nnodes - 1;

	printf("Servers ready:\n");

	while (left) {
		for (int n = 0; n < config->nnodes; n++) {
			// skip initialised nodes
			if (nodes[n])
				continue;

			ht_t ht = Numachip2::probe(config->nodes[n].sci);
			if (ht) {
				nodes[n] = new Node(config->nodes[n].sci, ht);
				left--;
			}

			cpu_relax();
		}
	}

	scan();
	finalise();

	if (options->boot_wait)
		lib::wait_key("Press enter to boot");

	printf("Unification succeeded; executing syslinux label %s\n", options->next_label);
	syslinux->exec(options->next_label);

	return 0;
}
