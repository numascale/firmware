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

#pragma once

class Options {
	/* Static needed so we can pass the function address */
	static void parse_string(const char *val, void *stringp) nonnull;
	static void parse_bool(const char *val, void *voidp) nonnull;
	static void parse_int(const char *val, void *intp) nonnull;
	static void parse_int64(const char *val, void *intp) nonnull;
	static void parse_flags(const char *val, void *flags) nonnull;
public:
	const char *next_label;
	const char *config_filename;
	bool ht_200mhz_only, ht_8bit_only, ht_selftest;
	bool init_only;
	bool boot_wait;
	bool handover_acpi;
	bool reentrant;
	bool singleton;
	bool fastboot;
	uint64_t tracing;
	struct debug_flags {
		uint8_t config, access, acpi, ht, fabric, maps, remote_io, e820, northbridge, cores, mctr;
	} debug;

	Options(const int argc, char *const argv[]) nonnull;
};

extern Options *options;
