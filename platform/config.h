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

#include "../library/base.h"

class Config {
public:
	struct node {
		sci_t id;
		uint8_t mac[6];
		uint8_t partition;
		bool master;
		bool seen;
		bool added;
		uint8_t portmask; // bitmask of used ports for releasing reset
	};
	struct partition_s {
		char label[32];
		bool unified;
		bool monitor;
	};
private:
	struct node *find(const sci_t id) nonnull;
	bool parse_blank(const char *data);
	bool parse_prefix(const char *data);
	bool parse_partition(const char *data);
	bool parse_node(char const *data);
	void parse(const char *pos);
public:
	char prefix[16];
	unsigned nnodes;
	struct node nodes[MAX_NODE];
	struct node *local_node, *master;

	unsigned npartitions;
	struct partition_s partitions[MAX_PARTITIONS];

	explicit Config();
	explicit Config(const char *filename);
};

extern Config *config;
extern const char *pr_node(const sci_t id);

