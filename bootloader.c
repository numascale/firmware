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
#include "numachip2/numachip.h"

Syslinux *syslinux;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;
ACPI *acpi;

void udelay(const uint32_t usecs)
{
	uint64_t limit = lib::rdtscll() + (uint64_t)usecs * Opteron::tsc_mhz;

	while (lib::rdtscll() < limit)
		cpu_relax();
}

void wait_key(const char *msg)
{
	puts(msg);
	char ch;

	do {
		fread(&ch, 1, 1, stdin);
	} while (ch != 0x0a); /* Enter */
}

Node::Node(const sci_t _sci, const ht_t ht): sci(_sci)
{
	numachip = new Numachip2(sci, ht);
}

// instantiated for local nodes
Node::Node(void): sci(0xfff0)
{
	const ht_t nc = Opteron::ht_fabric_fixup(Numachip2::VENDEV_NC2);
	assertf(nc, "NumaChip2 not found");

	/* Set SCI ID later once mapping is setup */
	numachip = new Numachip2(nc);

	nopterons = nc;

	/* Opterons are on all HT IDs before Numachip */
	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb] = new Opteron(0xfff0, nb);
}

void Node::set_sci(const sci_t _sci)
{
	sci = _sci;

	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb]->sci = sci;

	numachip->set_sci(sci);
}

int main(const int argc, const char *argv[])
{
	syslinux = new Syslinux(); /* Needed first for console access */

	printf(CLEAR BANNER "NumaConnect unification " VER " at 20%02d-%02d-%02d %02d:%02d:%02d" COL_DEFAULT "\n",
	  lib::rtc_read(RTC_YEAR), lib::rtc_read(RTC_MONTH), lib::rtc_read(RTC_DAY),
	  lib::rtc_read(RTC_HOURS), lib::rtc_read(RTC_MINUTES), lib::rtc_read(RTC_SECONDS));

	printf("Host MAC %02x:%02x:%02x:%02x:%02x:%02x, IP %s, hostname %s\n",
		syslinux->mac[0], syslinux->mac[1], syslinux->mac[2],
		syslinux->mac[3], syslinux->mac[4], syslinux->mac[5],
		inet_ntoa(syslinux->ip), syslinux->hostname ? syslinux->hostname : "<none>");

	Opteron::prepare();

	options = new Options(argc, argv);
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
	for (int i = 0; i < local_node->nopterons; i++)
		local_node->opterons[i]->mmiomap.add(9, NC_MCFG_BASE, NC_MCFG_LIM, local_node->numachip->ht, 0);

	// setup local MCFG access
	uint64_t val6 = NC_MCFG_BASE | ((uint64_t)config->local_node->sci << 28ULL) | 0x21ULL;
	lib::wrmsr(Opteron::MCFG_BASE, val6);

	local_node->set_sci(config->local_node->sci);

	if (!config->master_local) {
		// set go-ahead for master
		local_node->numachip->write32(Numachip2::FABRIC_CONTROL, 1 << 31);

		printf("Waiting for SCI%03x/%s", config->master->sci, config->master->hostname);

		// FIXME: wait for bit 30 being set when remote writes work
		while (local_node->numachip->read32(Numachip2::FABRIC_CONTROL) & (1 << 31))
			cpu_relax();

		printf(BANNER "\nThis server SCI%03x/%s is part of a %d-server NumaConnect system\n"
		  "Refer to the console on SCI%03x ", config->local_node->sci, config->local_node->hostname,
		  config->nnodes, config->partition->master);

		while (1) {
			cli();
			asm volatile("hlt" ::: "memory");
			printf("wake ");
		}
	}

	nodes = (Node **)zalloc(sizeof(void *) * config->nnodes);
	assert(nodes);
	nodes[0] = local_node; // FIXME: assumption

	int left = config->nnodes - 1;

	// FIXME: remove when remote access doesn't cause local an remote SF
	wait_key("Press enter when slave ready");
	printf("Servers ready:");

	while (left) {
		for (int n = 0; n < config->nnodes; n++) {
			// skip initialised nodes
			if (nodes[n])
				continue;

			ht_t ht = Numachip2::probe(config->nodes[n].sci);
			if (ht) {
				nodes[n] = new Node(config->nodes[n].sci, ht);
				printf(" SCI%03x", config->nodes[n].sci);
				left--;
			}

			cpu_relax();
		}
	}
	printf("\n");

	// FIXME: scan memory

	printf("Unification succeeded; loading %s...\n", options->next_label);
	if (options->boot_wait)
		wait_key("Press enter to boot");

	syslinux->exec(options->next_label);

	return 0;
}
