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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../bootloader.h"
#include "../version.h"
#include "options.h"

struct optargs {
	const char label[20];
	void (*handler)(const char *, void *);
	void *result;
};

void Options::parse_string(const char *val, void *stringp)
{
	xassert(val);
	char **string = (char **)stringp;
	*string = strdup(val);
	xassert(*string);
}

void Options::parse_bool(const char *val, void *voidp)
{
	bool *boolp = (bool *)voidp;

	if (val && val[0] != '\0') {
		int res = atoi(val);
		assertf(res == !!res, "Boolean option doesn't take value %d", res);
		*boolp = res;
	} else
		*boolp = true;
}

void Options::parse_int(const char *val, void *intp)
{
	int *int32 = (int *)intp;
	if (val && val[0] != '\0')
		*int32 = (int)strtol(val, NULL, 0);
	else
		*int32 = 1;
}

void Options::parse_int64(const char *val, void *intp)
{
	uint64_t *int64 = (uint64_t *)intp;

	if (val && val[0] != '\0') {
		char *endptr;
		uint64_t ret = strtoull(val, &endptr, 0);

		switch (*endptr) {
		case 'T':
		case 't':
			ret <<= 10;
		case 'G':
		case 'g':
			ret <<= 10;
		case 'M':
		case 'm':
			ret <<= 10;
		case 'K':
		case 'k':
			ret <<= 10;
			endptr++;
		default:
			break;
		}

		*int64 = ret;
	} else
		*int64 = 1;
}

void Options::parse_flags(const char *val, void *data)
{
	struct debug_flags *flags = (struct debug_flags *)data;

	/* If no args, assume reasonable default */
	if (!val) {
		memset(flags, 0x01, sizeof(Options::debug));
		flags->fabric = flags->access = flags->ht = 0;
		return;
	}

	char params[512];
	strncpy(params, val, sizeof(params));

	const char *pos = strtok(params, ",");
	while (pos) {
		if (!strcmp(pos, "all"))
			memset(flags, 1, sizeof(Options::debug));
		else if (!strcmp(pos, "full"))
			memset(flags, 255, sizeof(Options::debug));
		else if (!strcmp(pos, "config"))
			flags->config = 1;
		else if (!strcmp(pos, "access"))
			flags->access = 1;
		else if (!strcmp(pos, "acpi"))
			flags->acpi = 1;
		else if (!strcmp(pos, "ht"))
			flags->ht = 1;
		else if (!strcmp(pos, "fabric"))
			flags->fabric = 1;
		else if (!strcmp(pos, "maps"))
			flags->maps = 1;
		else if (!strcmp(pos, "remote-io"))
			flags->remote_io = 1;
		else if (!strcmp(pos, "e820"))
			flags->e820 = 1;
		else if (!strcmp(pos, "northbridge"))
			flags->northbridge = 1;

		pos = strtok(NULL, ",");
	}
}

Options::Options(const int argc, char *const argv[]): next_label("menu.c32"), config_filename("nc-config/fabric.json"),
  ht_200mhz_only(0), ht_8bit_only(0), ht_selftest(0), boot_wait(0), handover_acpi(0), reentrant(0), singleton(0), fast(0), tracing(0)
{
	memset(&debug, 0, sizeof(debug));

	static const struct optargs list[] = {
		{"next-label",	    &Options::parse_string, &next_label},      /* Next PXELINUX label to boot after loader */
		{"ht.8bit-only",    &Options::parse_bool,   &ht_8bit_only},
		{"ht.200mhz-only",  &Options::parse_bool,   &ht_200mhz_only},  /* Disable increase in speed from 200MHz to 800Mhz for HT link to ASIC based NC */
		{"ht.selftest",     &Options::parse_bool,   &ht_selftest},
		{"init-only",       &Options::parse_bool,   &init_only},
		{"wait",            &Options::parse_bool,   &boot_wait},
		{"handover-acpi",   &Options::parse_bool,   &handover_acpi},   /* Workaround Linux not being able to handover ACPI */
		{"config",          &Options::parse_string, &config_filename},
		{"reentrant",       &Options::parse_bool,   &reentrant},       /* Allow bootloader reload on error */
		{"debug",           &Options::parse_flags,  &debug},           /* Subsystem debug flags */
		{"singleton",       &Options::parse_bool,   &singleton},       /* Single-card, no config */
		{"fast",            &Options::parse_bool,   &fast},            /* Skip slow phases */
		{"tracing",         &Options::parse_int64,  &tracing},         /* Reserve tracebuffers */
	};

	int errors = 0;
	printf("Options:");
	for (int arg = 1; arg < argc; arg++) {
		/* Break into two strings where '=' found */
		char *val = strchr(argv[arg], '=');
		if (val) {
			*val = '\0';
			val++; /* Points to value */
		}

		bool handled = 0;
		for (unsigned int i = 0; i < (sizeof(list) / sizeof(list[0])); i++) {
			if (!strcmp(argv[arg], list[i].label)) {
				printf(" %s", argv[arg]);
				if (val)
					printf("=%s", val);

				list[i].handler(val, list[i].result);
				handled = 1;
				break;
			}
		}

		if (!handled) {
			printf(" %s (!)", argv[arg]);
			errors++;
		}
	}
	printf("\n");
	assertf(!errors, "Invalid arguments specified");

	if (tracing == 1 || tracing == 2) {
		printf("Defaulting to 512MB trace buffers\n");
		tracing = 512ULL << 20;
	}
}
