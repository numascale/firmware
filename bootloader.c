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
#include "platform/config.h"
#include "numachip2/numachip.h"

Syslinux *syslinux;
Options *options;
Config *config;
Opteron *opteron;
Numachip2 *numachip;
Nodes *nodes;

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
	uint64_t limit = rdtscll() + (uint64_t)usecs * opteron->tsc_mhz;

	while (rdtscll() < limit)
		cpu_relax();
}

void wait_key(void)
{
	char ch;
	printf("Press any key to continue");

	while (fread(&ch, 1, 1, stdin) == 0)
		;

	printf("\n");
}

int main(const int argc, const char *argv[])
{
	syslinux = new Syslinux(); /* Needed first for console access */

	printf(CLEAR BANNER "NumaConnect unification " VER " on %s/%s at 20%02d-%02d-%02d %02d:%02d:%02d" COL_DEFAULT "\n",
	  inet_ntoa(syslinux->myip), syslinux->hostname ? syslinux->hostname : "<none>",
	  rtc_read(RTC_YEAR), rtc_read(RTC_MONTH), rtc_read(RTC_DAY),
	  rtc_read(RTC_HOURS), rtc_read(RTC_MINUTES), rtc_read(RTC_SECONDS));

	options = new Options(argc, argv);
	platform_quirks();
	/* SMI often assumes HT nodes are Northbridges, so handover early */
	if (options->handover_acpi)
		stop_acpi();

	opteron = new Opteron(); /* Needed before any config access */
	numachip = new Numachip2();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	numachip->set_sci(config->node->sci);

	if (!config->node->sync_only) {

	}

	printf("Unification succeeded; loading %s...\n", options->next_label);
	if (options->boot_wait)
		wait_key();

	syslinux->exec(options->next_label);

	return 0;
}
