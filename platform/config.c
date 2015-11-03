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
#include "../platform/os.h"
#include "../node.h"
#include "options.h"
#include "config.h"

#ifndef SIM
extern "C" {
	#include "com32.h"
}
#endif

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
	uint32_t val;
	int errors = 0;

	const json_t *fab = json_find_first_label(root, "fabric");
	if (!fab)
		fatal("Label <fabric> not found in fabric configuration file");

	if (!parse_json_num(fab->child, "x-size", &size[0], 0)) {
		error("Label <x-size> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_num(fab->child, "y-size", &size[1], 0)) {
		error("Label <y-size> not found in fabric configuration file");
		errors++;
	}

	if (!parse_json_num(fab->child, "z-size", &size[2], 0)) {
		error("Label <z-size> not found in fabric configuration file");
		errors++;
	}

	const json_t *list = json_find_first_label(fab->child, "nodes");
	if (!(list && list->child && list->child->type == JSON_ARRAY))
		fatal("Label <nodes> not found in fabric configuration file");

	const json_t *obj;
	for (nnodes = 0, obj = list->child->child; obj; obj = obj->next)
		nnodes++;

	nodes = (struct node *)zalloc(nnodes * sizeof(*nodes));
	xassert(nodes);

	int i;
	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		nodes[i].id = i;

		parse_json_num(obj, "uuid", &nodes[i].uuid, 1); /* Optional */
		if (!parse_json_num(obj, "sci", &nodes[i].position, 0)) {
			error("Label <sci> not found in fabric configuration file");
			errors++;
		}

		if (parse_json_num(obj, "partition", &nodes[i].partition, 1))
			assertf(nodes[i].partition, "Partition 0 is invalid");
		else
			nodes[i].partition = 0;

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

		if (parse_json_bool(obj, "devices", &val, 1))
			nodes[i].devices = val;

		if (parse_json_bool(obj, "master", &val, 1))
			nodes[i].master = val;
	}

	if (errors)
		fatal("%d errors found in fabric configuration file", errors);
}

Config::Config(void)
{
	size[0] = 1;
	size[1] = 0;
	size[2] = 0;

	nnodes = 1;
	nodes = (struct node *)zalloc(sizeof(*nodes));
	xassert(nodes);

	nodes->uuid = ::local_node->numachip->uuid;
	nodes->position = 0;
	nodes->partition = 0;
	nodes->master = 1;
	nodes->id = 0;
	strcpy(nodes->hostname, "self");

	local_node = nodes;
}

struct Config::node *Config::find(const sci_t id)
{
	for (unsigned n = 0; n < nnodes; n++)
		if (nodes[n].id == id)
			return &nodes[n];

	fatal("Failed to find %03x in configuration", id);
}

Config::Config(const char *filename)
{
	size_t len;
	printf("Config %s", filename);
	const char *data = os->read_file(filename, &len);
	assertf(data && len > 0, "Failed to open file");

#ifdef DEBUG
	if (options->debug.config)
		printf("content:\n%s", data);
#endif

	json_t *root = NULL;
	enum json_error err = json_parse_document(&root, data);
	assertf(err == JSON_OK, "Fabric configuration file malformed (%s)", json_errors[err]);

	parse_json(root);
	lfree((char *)data);

	if (options->debug.config) {
		printf("; geometry %dx%xx%d\n", size[0], size[1], size[2]);

		for (unsigned i = 0; i < nnodes; i++) {
			printf("Node %u: hostname %s, MAC %02x:%02x:%02x:%02x:%02x:%02x, ",
			  i, nodes[i].hostname, nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
			  nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);

			if (nodes[i].uuid != 0xffffffff)
				printf("UUID %08X, ", nodes[i].uuid);

			printf("%03x, ", nodes[i].id);
			if (nodes[i].partition)
				printf("partition %u\n", nodes[i].partition);
			else
				printf("observer\n");
		}
	}

	/* Locate UUID or hostname */
	for (unsigned i = 0; i < nnodes; i++) {
#ifdef FIXME /* Uncomment when Numachip EEPROM has UUID */
		if (local_node->numachip->uuid == nodes[i].uuid) {
			if (options->debug.config)
				printf("UUID matches node %u", i);
			node = &nodes[i];
			break;
		}
#endif

		if (!memcmp(os->mac, nodes[i].mac, sizeof(os->mac))) {
			if (options->debug.config)
				printf("MAC matches node %u", i);
			local_node = &nodes[i];
			break;
		}

		if (!strcmp(os->hostname, nodes[i].hostname)) {
			if (options->debug.config)
				printf("Hostname matches node %u", i);
			local_node = &nodes[i];
			break;
		}
	}

	assertf(local_node, "Failed to find entry matching this node with UUID, MAC address or hostname");

	// ensure master occurs 0 or 1 times in this partition
	unsigned count = 0;

	for (unsigned i = 0; i < nnodes; i++) {
		if (local_node->partition == nodes[i].partition && nodes[i].master) {
			master = &nodes[i];
			count++;
		}
	}

	assertf(count <= 1, "More than one node specified as master in partition %u", local_node->partition);

	// if not specfied, select first node in this partition as master
	if (count == 0) {
		for (unsigned i = 0; i < nnodes; i++) {
			if (local_node->partition == nodes[i].partition) {
				nodes[i].master = 1;
				master = &nodes[i];
				break;
			}
		}
	}

	if (local_node->partition)
		printf("; partition %u", local_node->partition);
	else
		printf("; observer");
	printf("; %03x; %s\n", local_node->id, local_node->master ? "master" : "slave");
}
