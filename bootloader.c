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
#include "opteron/defs.h"
#include "library/base.h"
#include "library/access.h"
#include "platform/acpi.h"
#include "platform/options.h"
#include "platform/syslinux.h"
#include "platform/e820.h"
#include "platform/config.h"
#include "numachip2/numachip.h"

Syslinux *syslinux;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;

static void stop_acpi(void)
{
	printf("ACPI handoff: ");
	acpi_sdt_p fadt = find_sdt("FACP");

	if (!fadt) {
		printf("ACPI FACP table not found\n");
		return;
	}

	uint32_t smi_cmd = *(uint32_t *)&fadt->data[48 - 36];
	uint8_t acpi_enable = fadt->data[52 - 36];

	if (!smi_cmd || !acpi_enable) {
		printf("legacy support not enabled\n");
		return;
	}

	uint32_t acpipm1cntblk = *(uint32_t *)&fadt->data[64 - 36];
	uint16_t sci_en = inb(acpipm1cntblk);
	outb(acpi_enable, smi_cmd);
	int limit = 100;

	do {
		udelay(100);
		sci_en = inb(acpipm1cntblk);

		if ((sci_en & 1) == 1) {
			printf("legacy handoff succeeded\n");
			return;
		}
	} while (--limit);

	printf("ACPI handoff timed out\n");
}

static void platform_quirks(void)
{
	const char *biosver = NULL, *biosdate = NULL;
	const char *sysmanuf = NULL, *sysproduct = NULL;
	const char *boardmanuf = NULL, *boardproduct = NULL;

	smbios_parse(&biosver, &biosdate, &sysmanuf, &sysproduct, &boardmanuf, &boardproduct);
	assert(biosver && biosdate && sysmanuf && sysproduct && boardmanuf && boardproduct);

	printf("Platform is %s %s (%s %s) with BIOS %s %s", sysmanuf, sysproduct, boardmanuf, boardproduct, biosver, biosdate);

	/* Skip if already set */
	if (!options->handover_acpi) {
		/* Systems where ACPI must be handed off early */
		const char *acpi_blacklist[] = {"H8QGL", NULL};

		for (unsigned int i = 0; i < (sizeof acpi_blacklist / sizeof acpi_blacklist[0]); i++) {
			if (!strcmp(boardproduct, acpi_blacklist[i])) {
				printf(" (blacklisted)");
				options->handover_acpi = 1;
				break;
			}
		}
	}

	printf("\n");
}

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

Node::Node(const sci_t _sci): sci(_sci)
{
	uint32_t rev;
	const ht_t nc = Opteron::ht_fabric_fixup(Numachip2::vendev, &rev);
	assertf(nc, "NumaChip2 not found");

	/* Set SCI ID later once mapping is setup */
	numachip = new Numachip2(sci, nc, rev);

	nopterons = nc;

	/* Opterons are on all HT IDs before Numachip */
	for (ht_t nb = 0; nb < nopterons; nb++)
		opterons[nb] = new Opteron(0xfff0, nb);
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
	platform_quirks();
	/* SMI often assumes HT nodes are Northbridges, so handover early */
	if (options->handover_acpi)
		stop_acpi();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	local_node = new Node(config->node->sci);

	/* Add global MCFG maps */
	for (int i = 0; i < local_node->nopterons; i++)
		local_node->opterons[i]->mmiomap.add(8, MCFG_BASE, MCFG_LIM, local_node->numachip->ht, 0);

	local_node->numachip->set_sci(config->node->sci);

	uint64_t val6 = MCFG_BASE | ((uint64_t)config->node->sci << 28ULL) | 0x21ULL;
	lib::wrmsr(MSR_MCFG_BASE, val6);

	sci_t r;
	uint32_t secret;
	if (config->node->sci == 000) {
		r = 0x001;
		secret = 0x182d25a0;
	} else {
		r = 0x000;
		secret = 0x8a2ce729;
	}

	printf("Writing secret %08x into local register\n", secret);
	local_node->numachip->write32(0x0014, secret);

	wait_key("Press enter when remote ready");

	printf("Local secret is %08x\n", lib::mcfg_read32(config->node->sci, 0, 26, 0, 0x14));
	printf("Remote secret is %08x\n", lib::mcfg_read32(r, 0, 26, 0, 0x14));

	e820 = new E820();

	printf("Unification succeeded; loading %s...\n", options->next_label);
	if (options->boot_wait)
		wait_key("Press enter to boot");

	syslinux->exec(options->next_label);

	return 0;
}
