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

#ifndef __OPTIONS_H
#define __OPTIONS_H

class Options {
	static void parse_string(const char *val, void *stringp);
	static void parse_bool(const char *val, void *voidp);
	static void parse_int(const char *val, void *intp);
	static void parse_int64(const char *val, void *intp);
public:
	const char *next_label;
	const char *config_filename;
	int ht_200mhz_only;
	int ht_8bit_only;
	bool boot_wait;
	bool handover_acpi;
	int verbose;
	bool reentrant;

	Options(const int argc, const char *argv[]);
};

extern Options *options;

#endif

