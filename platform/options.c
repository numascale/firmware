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

#include "../platform/bootloader.h"
#include "../version.h"

/* Command line arguments */
char *next_label = "menu.c32";
int ht_200mhz_only = 0;
int ht_8bit_only = 0;
bool boot_wait = false;
bool handover_acpi = false;
int verbose = 0;

struct optargs {
	char label[16];
	int (*handler)(const char *, void *);
	void *userdata;
};

static int parse_string(const char *val, void *stringp)
{
	char **string = (char **)stringp;
	*string = strdup(val);
	assert(*string);
	return 1;
}

static int parse_bool(const char *val, void *voidp)
{
	bool *boolp = (bool *)voidp;

	if (val[0] != '\0')
		*boolp = atoi(val) ? true : false;
	else
		*boolp = true;

	return 1;
}

static int parse_int(const char *val, void *intp)
{
	int *int32 = (int *)intp;

	if (val[0] != '\0')
		*int32 = (int)strtol(val, NULL, 0);
	else
		*int32 = 1;

	return 1;
}

static int parse_uint64_t(const char *val, void *intp)
{
	uint64_t *int64 = (uint64_t *)intp;

	if (val[0] != '\0') {
		char *endptr;
		uint64_t ret = strtoull(val, &endptr, 0);

		switch (*endptr) {
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

	return 1;
}

int parse_cmdline(const char *cmdline)
{
	static struct optargs options[] = {
		{"next-label",	    &parse_string, &next_label},      /* Next PXELINUX label to boot after loader */
		{"ht.8bit-only",    &parse_int,    &ht_8bit_only},
		{"ht.200mhz-only",  &parse_int,    &ht_200mhz_only},  /* Disable increase in speed from 200MHz to 800Mhz for HT link to ASIC based NC */
		{"boot-wait",       &parse_bool,   &boot_wait},
		{"handover-acpi",   &parse_bool,   &handover_acpi},   /* Workaround Linux not being able to handover ACPI */
		{"verbose",         &parse_int,    &verbose},
	};
	char arg[256];
	int lstart, lend, aend, i;

	if (!cmdline)
		return 1;

	printf("Options:");
	lstart = 0;

	while (cmdline[lstart] != '\0') {
		while (cmdline[lstart] != '\0' && cmdline[lstart] == ' ')
			lstart++;

		lend = lstart;

		while (cmdline[lend] != '\0' && cmdline[lend] != ' ' && cmdline[lend] != '=')
			lend++;

		aend = lend;

		while (cmdline[aend] != '\0' && cmdline[aend] != ' ')
			aend++;

		if (lstart == lend)
			break;

		if (lend - lstart < (int)(sizeof(options[0].label))) {
			for (i = 0; i < (int)(sizeof(options) / sizeof(options[0])); i++) {
				if (strncmp(&cmdline[lstart], options[i].label, lend - lstart) == 0) {
					memset(arg, 0, sizeof(arg));

					if (cmdline[lend] == '=')
						lend++;

					if (aend - lend >= (int)(sizeof(arg)))
						strncpy(arg, &cmdline[lend], sizeof(arg) - 1);
					else
						strncpy(arg, &cmdline[lend], aend - lend);

					printf(" %s=%s", options[i].label, arg);

					if (options[i].handler(arg, options[i].userdata) < 0) {
						printf("\n");
						return -1;
					}

					break;
				}
			}

			/* Check if option isn't recognised */
			if (i == (int)(sizeof(options) / sizeof(options[0]))) {
				/* Terminate current arg */
				*strchr(&cmdline[lstart], ' ') = '\0';
				printf("\nError: invalid option '%s'\n", &cmdline[lstart]);
				wait_key();
			}
		}

		lstart = aend;
	}

	printf("\n");
	return 1;
}
