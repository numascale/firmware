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

bool Config::parse_json_bool(json_t *obj, const char *label, uint32_t *val, bool opt)
{
	json_t *item;
	*val = -1;
	item = json_find_first_label(obj, label);

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
		warning("Label <%s> has bad type %d in fabric configuration file",
		       label, item->child->type);
		return 0;
	}

	return 1;
}

bool Config::parse_json_num(json_t *obj, const char *label, uint32_t *val, int opt)
{
	json_t *item;
	char *end;
	*val = -1;
	item = json_find_first_label(obj, label);

	if (!(item && item->child)) {
		if (!opt)
			warning("Label <%s> not found in fabric configuration file", label);

		return 0;
	}

	if (item->child->type == JSON_NUMBER) {
		*val = strtol(item->child->text, &end, 10);
	} else if (item->child->type == JSON_STRING) {
		*val = strtol(item->child->text, &end, 16);
	} else {
		warning("Label <%s> has bad type %d in fabric configuration file",
		       label, item->child->type);
		return 0;
	}

	if (end[0] != '\0') {
		warning("Label <%s> value bad format in fabric configuration file", label);
		*val = -1;
		return 0;
	}

	return 1;
}

bool Config::parse_json_str(json_t *obj, const char *label, char *val, int len, int opt)
{
	json_t *item;
	val[0] = '\0';
	item = json_find_first_label(obj, label);

	if (!(item && item->child)) {
		if (!opt)
			warning("Label <%s> not found in fabric configuration file", label);

		return 0;
	}

	if (item->child->type == JSON_STRING) {
		strncpy(val, item->child->text, len);
		val[len - 1] = '\0';
	} else {
		warning("Label <%s> has bad type %d in fabric configuration file",
		       label, item->child->type);
		return 0;
	}

	return 1;
}

bool Config::parse_json(json_t *root)
{
	json_t *fab, *list, *obj;
	int i;
	uint32_t val;
	fab = json_find_first_label(root, "fabric");

	if (!fab) {
		error("Label <fabric> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_num(fab->child, "x-size", &fabric.x_size, 0)) {
		error("Label <x-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_num(fab->child, "y-size", &fabric.y_size, 0)) {
		error("Label <y-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_num(fab->child, "z-size", &fabric.z_size, 0)) {
		error("Label <z-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_bool(fab->child, "strict", &fabric.strict, 1))
		fabric.strict = 0;

	list = json_find_first_label(fab->child, "nodes");

	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <nodes> not found in fabric configuration file");
		goto out1;
	}

	for (nodes = 0, obj = list->child->child; obj; obj = obj->next)
		nodes++;

	nodelist = (node_info *)malloc(nodes * sizeof(*nodelist));
	assert(nodelist);

	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		/* UUID is optional */
		if (!parse_json_num(obj, "uuid", &nodelist[i].uuid, 1)) {
			nodelist[i].uuid = 0;
			name_matching = 1;
		}

		if (!parse_json_num(obj, "sciid", &nodelist[i].sci, 0)) {
			error("Label <sciid> not found in fabric configuration file");
			goto out2;
		}

		if (!parse_json_num(obj, "partition", &nodelist[i].partition, 0)) {
			error("Label <partition> not found in fabric configuration file");
			goto out2;
		}

		/* OSC is optional */
		if (!parse_json_num(obj, "osc", &nodelist[i].osc, 1))
			nodelist[i].osc = 0;

		if (!parse_json_str(obj, "desc", nodelist[i].desc, 32, 0)) {
			error("Label <desc> not found in fabric configuration file");
			goto out2;
		}

		if (parse_json_num(obj, "sync-only", &val, 1))
			nodelist[i].sync_only = val;
		else
			nodelist[i].sync_only = 0;
	}

	if (name_matching)
		printf("UUIDs omitted - matching hostname to <desc> label\n");

	list = json_find_first_label(fab->child, "partitions");

	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <partitions> not found in fabric configuration file");
		free(nodelist);
		return 0;
	}

	for (partitions = 0, obj = list->child->child; obj; obj = obj->next)
		partitions++;

	partlist = (part_info *)malloc(partitions * sizeof(*partlist));
	assert(partlist);

	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		if (!parse_json_num(obj, "master", &partlist[i].master, 0)) {
			error("Label <master> not found in fabric configuration file");
			goto out3;
		}

		if (!parse_json_num(obj, "builder", &partlist[i].builder, 0)) {
			error("Label <builder> not found in fabric configuration file");
			goto out3;
		}
	}

	return 1;
out3:
	free(nodelist);
out2:
	free(partlist);
out1:
	return 0;
}

void Config::parse_config_file(const char *data)
{
	json_t *root = NULL;
	enum json_error err;
	int i;

	if (options->debug.config)
		printf("Fabric configuration file:\n%s", data);

	err = json_parse_document(&root, data);
	if (err != JSON_OK)
		fatal("%s when parsing fabric configuration", json_errors[err]);

	if (!parse_json(root))
		fatal("Parsing fabric configuration root failed");

	printf("Fabric dimensions: x: %d, y: %x, z: %d\n",
	       fabric.x_size, fabric.y_size, fabric.z_size);

	for (i = 0; i < nodes; i++)
		printf("Node %d: <%s> uuid: %08X, sciid: 0x%03x, partition: %d, osc: %d, sync-only: %d\n",
		       i, nodelist[i].desc, nodelist[i].uuid,
		       nodelist[i].sci, nodelist[i].partition,
		       nodelist[i].osc, nodelist[i].sync_only);

	for (i = 0; i < partitions; i++)
		printf("Partition %d: master: 0x%03x, builder: 0x%03x\n",
		       i, partlist[i].master, partlist[i].builder);
}

void Config::make_singleton_config(void)
{
	fabric.x_size = 1;
	fabric.y_size = 0;
	fabric.z_size = 0;
	nodes = 1;
	nodelist = (node_info *)malloc(sizeof(*nodelist));
	assert(nodelist);

	nodelist[0].uuid = 0; /* FIXME: local_info->uuid; */
	nodelist[0].sci = 0;
	nodelist[0].osc = 0;
	nodelist[0].partition = 0;
	memcpy(nodelist[0].desc, "self", 5);
	nodelist[0].sync_only = 1;
	partitions = 1;
	partlist = (part_info *)malloc(sizeof(*partlist));
	assert(partlist);
	partlist[0].master = 0;
	partlist[0].builder = 0;
}

int Config::config_local(const struct node_info *info, const uint32_t uuid)
{
	if (name_matching && hostname) {
		return strcmp(info->desc, hostname) == 0;
	} else
		return info->uuid == uuid;
}

Config::Config(void)
{
	int len;
	const char *json = syslinux->read_file(options->config_filename, &len);
	parse_config_file(json);
	lfree((char *)json);
}
