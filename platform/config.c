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

#include "../library/base.h"
#include "../bootloader.h"
#include "options.h"
#include "config.h"

extern "C" {
	#include "com32.h"
}

static const char *json_errors[] = {
	"Unknown",
	"Success",
	"Incomplete",
	"Extra characters at end",
	"Structural errors detected",
	"Incorrect type detected",
	"Out of memory",
	"Unexpected character detected",
	"Malformed tree structure",
	"Maximum allowed size exceeded",
	"Unclassified problem",
};

bool Config::parse_json_bool(const json_t *obj, const char *label, uint32_t *val, const bool opt)
{
	*val = -1;
	json_t *item = json_find_first_label(obj, label);

	if (!(item && item->child)) {
		if (!opt)
			warning("Label <%s> not found in fabric configuration file", label);
		return 0;
	}

	if (item->child->type == JSON_TRUE) {
		*val = 1;
	} else if (item->child->type == JSON_FALSE) {
		*val = 0;
	} else {
		warning("Label <%s> has bad type %d in fabric configuration file", label, item->child->type);
		return 0;
	}

	return 1;
}

bool Config::parse_json_num(const json_t *obj, const char *label, uint32_t *val, const int opt)
{
	*val = -1;
	json_t *item = json_find_first_label(obj, label);

	if (!(item && item->child)) {
		if (!opt)
			warning("Label <%s> not found in fabric configuration file", label);

		return 0;
	}

	char *end;
	if (item->child->type == JSON_NUMBER) {
		*val = strtol(item->child->text, &end, 10);
	} else if (item->child->type == JSON_STRING) {
		*val = strtol(item->child->text, &end, 16);
	} else {
		warning("Label <%s> has bad type %d in fabric configuration file", label, item->child->type);
		return 0;
	}

	if (end[0] != '\0') {
		warning("Label <%s> value bad format in fabric configuration file", label);
		*val = -1;
		return 0;
	}

	return 1;
}

bool Config::parse_json_str(const json_t *obj, const char *label, char *val, int len, const int opt)
{
	val[0] = '\0';
	json_t *item = json_find_first_label(obj, label);

	if (!(item && item->child)) {
		if (!opt)
			warning("Label <%s> not found in fabric configuration file", label);
		return 0;
	}

	if (item->child->type == JSON_STRING) {
		strncpy(val, item->child->text, len);
		val[len - 1] = '\0';
	} else {
		warning("Label <%s> has bad type %d in fabric configuration file", label, item->child->type);
		return 0;
	}

	return 1;
}

void Config::parse_json(json_t *root)
{
	int errors = 0;

	const json_t *fab = json_find_first_label(root, "fabric");
	if (!fab) {
		error("Label <fabric> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_num(fab->child, "x-size", &x_size, 0)) {
		error("Label <x-size> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_num(fab->child, "y-size", &y_size, 0)) {
		error("Label <y-size> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_num(fab->child, "z-size", &z_size, 0)) {
		error("Label <z-size> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_bool(fab->child, "strict", &strict, 1))
		strict = 0;

	const json_t *list = json_find_first_label(fab->child, "nodes");
	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <nodes> not found in fabric configuration file");
		errors++;
	}

	const json_t *obj;
	for (nnodes = 0, obj = list->child->child; obj; obj = obj->next)
		nnodes++;

	nodes = (struct node *)malloc(nnodes * sizeof(*nodes));
	assert(nodes);

	int i;
	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		parse_json_num(obj, "uuid", &nodes[i].uuid, 1); /* Optional */
		if (!parse_json_num(obj, "sci", &nodes[i].sci, 0)) {
			error("Label <sci> not found in fabric configuration file");
			errors++;
		}

		if (!parse_json_num(obj, "partition", &nodes[i].partition, 0)) {
			error("Label <partition> not found in fabric configuration file");
			errors++;
		}

		if (!parse_json_str(obj, "hostname", nodes[i].hostname, sizeof(nodes[i].hostname), 0)) {
			error("Label <hostname> not found in fabric configuration file");
			errors++;
		}

		/* Optional MAC address */
		char mac[18];
		if (parse_json_str(obj, "mac", mac, sizeof(mac), 1)) {
			unsigned j = 0;

			char *token = strtok(mac, ":");
			while (token && j < sizeof(nodes[i].mac)) {
				nodes[i].mac[j++] = strtoul(token, NULL, 16);
				token = strtok(NULL, ":");
			}
		}

		uint32_t val;
		if (parse_json_num(obj, "sync-only", &val, 1))
			nodes[i].sync_only = val;
		else
			nodes[i].sync_only = 0;
	}

	list = json_find_first_label(fab->child, "partitions");
	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <partitions> not found in fabric configuration file");
		errors++;
	}

	for (npartitions = 0, obj = list->child->child; obj; obj = obj->next)
		npartitions++;

	partitions = (struct partition *)malloc(npartitions * sizeof(*partitions));
	assert(partitions);

	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		if (!parse_json_num(obj, "master", &partitions[i].master, 0)) {
			error("Label <master> not found in fabric configuration file");
			errors++;
		}

		if (!parse_json_num(obj, "builder", &partitions[i].builder, 0)) {
			error("Label <builder> not found in fabric configuration file");
			errors++;
		}
	}

	if (errors)
		fatal("%d errors found in fabric configuration file", errors);
}

Config::Config(void)
{
	x_size = 1;
	y_size = 0;
	z_size = 0;
	ringmask = ((!!x_size) * 3) | ((!!y_size) * 3 << 2) | ((!!z_size) * 3 << 4);

	nnodes = 1;
	nodes = (struct node *)malloc(sizeof(*nodes));
	assert(nodes);

	nodes->uuid = local_node->numachip->uuid;
	nodes->sci = 0;
	nodes->partition = 0;
	strcpy(nodes->hostname, "self");
	nodes->sync_only = 1;
	npartitions = 1;
	partitions = (struct partition *)malloc(sizeof(*partitions));
	assert(partitions);
	partitions->master = 0;
	partitions->builder = 0;

	node = nodes;
	partition = partitions;
}

Config::Config(const char *filename)
{
	int len;
	const char *data = syslinux->read_file(filename, &len);

#ifdef DEBUG
	if (options->debug.config)
		printf("Fabric configuration file:\n%s", data);
#endif

	json_t *root = NULL;
	enum json_error err = json_parse_document(&root, data);
	assertf(err == JSON_OK, "Fabric configuration file malformed (%s)", json_errors[err]);

	parse_json(root);
	lfree((char *)data);

	ringmask = ((!!x_size) * 3) | ((!!y_size) * 3 << 2) | ((!!z_size) * 3 << 4);

	if (options->debug.config) {
		printf("Fabric configuration:: x %d, y %x, z %d\n", x_size, y_size, z_size);

		for (int i = 0; i < nnodes; i++)
			printf("Node %d: hostname %s, MAC %02x:%02x:%02x:%02x:%02x:%02x, UUID %08X, SCI%03x, partition %d, sync-only %d\n",
		      i, nodes[i].hostname, nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
		      nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5], nodes[i].uuid,
		      nodes[i].sci, nodes[i].partition, nodes[i].sync_only);

		for (int i = 0; i < npartitions; i++)
			printf("Partition %d: master SCI%03x, builder SCI%03x\n",
		      i, partitions[i].master, partitions[i].builder);
	}

	/* Locate UUID or hostname */
	for (int i = 0; i < nnodes; i++) {
#ifdef FIXME /* Uncomment when Numachip EEPROM has UUID */
		if (local_node->numachip->uuid == nodes[i].uuid) {
			if (options->debug.config)
				printf("UUID matches node %d\n", i);
			node = &nodes[i];
			break;
		}
#endif

		if (!memcmp(syslinux->mac, nodes[i].mac, sizeof(syslinux->mac))) {
			if (options->debug.config)
				printf("MAC matches node %d\n", i);
			node = &nodes[i];
			break;
		}

		if (!strcmp(syslinux->hostname, nodes[i].hostname)) {
			if (options->debug.config)
				printf("Hostname matches node %d\n", i);
			node = &nodes[i];
			break;
		}
	}

	assertf(node, "Failed to find entry matching this node with UUID %08X or hostname %s", local_node->numachip->uuid, syslinux->hostname ? syslinux->hostname : "<none>");
	partition = &partitions[node->partition];
}

