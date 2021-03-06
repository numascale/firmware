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
			ret <<= 40;
			break;
		case 'G':
		case 'g':
			ret <<= 30;
			break;
		case 'M':
		case 'm':
			ret <<= 20;
			break;
		case 'K':
		case 'k':
			ret <<= 10;
			endptr++;
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
		if (!strcmp(pos, "config"))
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
		else if (!strcmp(pos, "wdt"))
			flags->wdt = 1;
		else if (!strcmp(pos, "northbridge"))
			flags->northbridge = 1;
		else if (!strcmp(pos, "cores"))
			flags->cores = 1;
		else if (!strcmp(pos, "mctr"))
			flags->mctr = 1;
		else if (!strcmp(pos, "wdtinfo"))
			flags->wdtinfo = 1;
		else if (!strcmp(pos, "monitor"))
			flags->monitor = 1;
		else
			fatal("Unknown debug flag '%s'", pos);

		pos = strtok(NULL, ",");
	}
}

Options::Options(const int argc, char *const argv[]): config_filename("fabric.txt"), flash(),
	ht_slowmode(0), init_only(0), boot_wait(0), handover_acpi(0),
	fastboot(0), remote_io(1), test_manufacture(0), test_boardinfo(0), dimmtest(2), memlimit(~0), tracing(0)
{
	memset(&debug, 0, sizeof(debug));

	static const struct optargs list[] = {
		{"ht.slowmode",     &Options::parse_bool,   &ht_slowmode},     // enforce 200MHz 8-bit HT link
		{"init-only",       &Options::parse_bool,   &init_only},       // load next-label after adding NumaChip to coherent fabric
		{"wait",            &Options::parse_bool,   &boot_wait},       // wait for keypress before loading next-label
		{"handover-acpi",   &Options::parse_bool,   &handover_acpi},   // workaround Linux failing ACPI handover
		{"config",          &Options::parse_string, &config_filename}, // path to fabric configuration JSON
		{"debug",           &Options::parse_flags,  &debug},           // enable subsystem debug checking/output
		{"fastboot",        &Options::parse_bool,   &fastboot},        // skip/reduce slower testing during unification
		{"remote-io",       &Options::parse_bool,   &remote_io},       // enable experimental remote IO
		{"tracing",         &Options::parse_int64,  &tracing},         // memory per NUMA node reserved for HT tracing
		{"memlimit",        &Options::parse_int64,  &memlimit},        // per-server memory limit
		{"flash",           &Options::parse_string, &flash},           // path to image file to flash
		{"dimmtest",        &Options::parse_int,    &dimmtest},        // run memory controller BIST for DIMM
		{"test.manufacture",&Options::parse_bool,   &test_manufacture},// perform manufacture testing; requires a cable between each port pair
		{"test.boardinfo",  &Options::parse_bool,   &test_boardinfo},  // update board info
	};

	unsigned errors = 0;
	printf("Options:");
	for (int arg = 1; arg < argc; arg++) {
		/* Break into two strings where '=' found */
		char *val = strchr(argv[arg], '=');
		if (val) {
			*val = '\0';
			val++; /* Points to value */
		}

		bool handled = 0;
		for (unsigned i = 0; i < (sizeof(list) / sizeof(list[0])); i++) {
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

	// Enable all BIST sequences if we're doing manufacture testing
	if (test_manufacture)
		dimmtest = 0xff;

	if (fastboot)
		dimmtest = 0;

	if (tracing > 0 && tracing < (16ULL << 20)) {
		warning("%" PRIu64 "MB trace buffers are too small; disabling", tracing >> 20);
		tracing = 0;
	}
}
