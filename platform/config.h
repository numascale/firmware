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

#ifndef __CONFIG_H
#define __CONFIG_H

#include "../json-1.5/src/json.h"

class Config {
	struct node {
		uint32_t uuid;
		uint32_t sci;
		uint32_t partition;
		char hostname[32];
		uint8_t mac[6];
		bool sync_only;
	};

	struct partition {
		uint32_t master;
		uint32_t builder;
	};

	bool parse_json_bool(const json_t *obj, const char *label, uint32_t *val, const bool opt);
	bool parse_json_num(const json_t *obj, const char *label, uint32_t *val, const int opt);
	bool parse_json_str(const json_t *obj, const char *label, char *val, const int len, const int opt);
	void parse_json(json_t *root);
public:
	uint32_t x_size, y_size, z_size;
	uint32_t strict;

	bool master;
	int ringmask : 6;
	int nnodes, npartitions;
	struct node *node, *nodes;
	struct partition *partition, *partitions;

	Config(void);
	Config(const char *filename);
};

#endif
