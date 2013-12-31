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
//#include <console.h>
//#include <com32.h>
#include <inttypes.h>

#include "../platform/bootloader.h"
#include "../version.h"

/* Command line arguments */
const char *next_label = "menu.c32";
int ht_200mhz_only = 0;
int ht_8bit_only = 0;
bool boot_wait = false;
bool handover_acpi = false;
int verbose = 0;

struct optargs {
	const char label[20];
	void (*handler)(const char *, void *);
	void *result;
};

static void parse_string(const char *val, void *stringp)
{
	assert(val);
	char **string = (char **)stringp;
	*string = strdup(val);
	assert(*string);
}

static void parse_bool(const char *val, void *voidp)
{
	bool *boolp = (bool *)voidp;

	if (val && val[0] != '\0') {
		int res = atoi(val);
		assertf(res == !!res, "Boolean option doesn't take value %d", res);
		*boolp = res;
	} else
		*boolp = true;
}

static void parse_int(const char *val, void *intp)
{
	int *int32 = (int *)intp;
	if (val && val[0] != '\0')
		*int32 = (int)strtol(val, NULL, 0);
	else
		*int32 = 1;
}

static void parse_int64(const char *val, void *intp)
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

void parse_cmdline(const int argc, const char *argv[])
{
	static const struct optargs options[] = {
		{"next-label",	    &parse_string, &next_label},      /* Next PXELINUX label to boot after loader */
		{"ht.8bit-only",    &parse_bool,   &ht_8bit_only},
		{"ht.200mhz-only",  &parse_int,    &ht_200mhz_only},  /* Disable increase in speed from 200MHz to 800Mhz for HT link to ASIC based NC */
		{"boot-wait",       &parse_bool,   &boot_wait},
		{"handover-acpi",   &parse_bool,   &handover_acpi},   /* Workaround Linux not being able to handover ACPI */
		{"verbose",         &parse_int,    &verbose},
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
		for (unsigned int i = 0; i < (sizeof(options) / sizeof(options[0])); i++) {
			if (!strcmp(argv[arg], options[i].label)) {
				printf(" %s", argv[arg]);
				if (val)
					printf("=%s", val);

				options[i].handler(val, options[i].result);
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
}

