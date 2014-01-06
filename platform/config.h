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

struct fabric_info {
	uint32_t x_size;
	uint32_t y_size;
	uint32_t z_size;
	uint32_t strict;
};

struct node_info {
	uint32_t uuid;
	uint32_t sci;
	uint32_t partition;
	uint32_t osc;
	char desc[32];
	bool sync_only;
};

struct part_info {
	uint32_t master;
	uint32_t builder;
};

class Config {
	bool parse_json_bool(json_t *obj, const char *label, uint32_t *val, bool opt);
	bool parse_json_num(json_t *obj, const char *label, uint32_t *val, int opt);
	bool parse_json_str(json_t *obj, const char *label, char *val, int len, int opt);
	bool parse_json(json_t *root);
public:
	struct fabric_info cfg_fabric;
	struct node_info *cfg_nodelist;
	struct part_info *cfg_partlist;
	int cfg_nodes, cfg_partitions;
	bool name_matching;
	char *hostname;

	bool parse_config_file(char *data);
	void make_singleton_config(void);
	int config_local(const struct node_info *info, const uint32_t uuid);
};

extern Config config;

#endif

