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
#include "syslinux.h"
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

	if (!parse_json_num(fab->child, "x-size", &cfg_fabric.x_size, 0)) {
		error("Label <x-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_num(fab->child, "y-size", &cfg_fabric.y_size, 0)) {
		error("Label <y-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_num(fab->child, "z-size", &cfg_fabric.z_size, 0)) {
		error("Label <z-size> not found in fabric configuration file");
		goto out1;
	}

	if (!parse_json_bool(fab->child, "strict", &cfg_fabric.strict, 1))
		cfg_fabric.strict = 0;

	list = json_find_first_label(fab->child, "nodes");

	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <nodes> not found in fabric configuration file");
		goto out1;
	}

	for (cfg_nodes = 0, obj = list->child->child; obj; obj = obj->next)
		cfg_nodes++;

	cfg_nodelist = (node_info *)malloc(cfg_nodes * sizeof(*cfg_nodelist));
	assert(cfg_nodelist);

	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		/* UUID is optional */
		if (!parse_json_num(obj, "uuid", &cfg_nodelist[i].uuid, 1)) {
			cfg_nodelist[i].uuid = 0;
			name_matching = 1;
		}

		if (!parse_json_num(obj, "sciid", &cfg_nodelist[i].sci, 0)) {
			error("Label <sciid> not found in fabric configuration file");
			goto out2;
		}

		if (!parse_json_num(obj, "partition", &cfg_nodelist[i].partition, 0)) {
			error("Label <partition> not found in fabric configuration file");
			goto out2;
		}

		/* OSC is optional */
		if (!parse_json_num(obj, "osc", &cfg_nodelist[i].osc, 1))
			cfg_nodelist[i].osc = 0;

		if (!parse_json_str(obj, "desc", cfg_nodelist[i].desc, 32, 0)) {
			error("Label <desc> not found in fabric configuration file");
			goto out2;
		}

		if (parse_json_num(obj, "sync-only", &val, 1))
			cfg_nodelist[i].sync_only = val;
		else
			cfg_nodelist[i].sync_only = 0;
	}

	if (name_matching)
		printf("UUIDs omitted - matching hostname to <desc> label\n");

	list = json_find_first_label(fab->child, "partitions");

	if (!(list && list->child && list->child->type == JSON_ARRAY)) {
		error("Label <partitions> not found in fabric configuration file");
		free(cfg_nodelist);
		return 0;
	}

	for (cfg_partitions = 0, obj = list->child->child; obj; obj = obj->next)
		cfg_partitions++;

	cfg_partlist = (part_info *)malloc(cfg_partitions * sizeof(*cfg_partlist));
	assert(cfg_partlist);

	for (i = 0, obj = list->child->child; obj; obj = obj->next, i++) {
		if (!parse_json_num(obj, "master", &cfg_partlist[i].master, 0)) {
			error("Label <master> not found in fabric configuration file");
			goto out3;
		}

		if (!parse_json_num(obj, "builder", &cfg_partlist[i].builder, 0)) {
			error("Label <builder> not found in fabric configuration file");
			goto out3;
		}
	}

	return 1;
out3:
	free(cfg_nodelist);
out2:
	free(cfg_partlist);
out1:
	return 0;
}

bool Config::parse_config_file(char *data)
{
	json_t *root = NULL;
	enum json_error err;
	int i;

	if (options->verbose > 2)
		printf("Fabric configuration file:\n%s", data);

	err = json_parse_document(&root, data);
	if (err != JSON_OK)
		fatal("%s when parsing fabric configuration", json_errors[err]);

	if (!parse_json(root)) {
		error("Parsing fabric configuration root failed");
		json_free_value(&root);
		return 0;
	}

	printf("Fabric dimensions: x: %d, y: %x, z: %d\n",
	       cfg_fabric.x_size, cfg_fabric.y_size, cfg_fabric.z_size);

	for (i = 0; i < cfg_nodes; i++)
		printf("Node %d: <%s> uuid: %08X, sciid: 0x%03x, partition: %d, osc: %d, sync-only: %d\n",
		       i, cfg_nodelist[i].desc, cfg_nodelist[i].uuid,
		       cfg_nodelist[i].sci, cfg_nodelist[i].partition,
		       cfg_nodelist[i].osc, cfg_nodelist[i].sync_only);

	for (i = 0; i < cfg_partitions; i++)
		printf("Partition %d: master: 0x%03x, builder: 0x%03x\n",
		       i, cfg_partlist[i].master, cfg_partlist[i].builder);

	return 1;
}

void Config::make_singleton_config(void)
{
	cfg_fabric.x_size = 1;
	cfg_fabric.y_size = 0;
	cfg_fabric.z_size = 0;
	cfg_nodes = 1;
	cfg_nodelist = (node_info *)malloc(sizeof(*cfg_nodelist));
	assert(cfg_nodelist);

	cfg_nodelist[0].uuid = 0; /* FIXME: local_info->uuid; */
	cfg_nodelist[0].sci = 0;
	cfg_nodelist[0].osc = 0;
	cfg_nodelist[0].partition = 0;
	memcpy(cfg_nodelist[0].desc, "self", 5);
	cfg_nodelist[0].sync_only = 1;
	cfg_partitions = 1;
	cfg_partlist = (part_info *)malloc(sizeof(*cfg_partlist));
	assert(cfg_partlist);
	cfg_partlist[0].master = 0;
	cfg_partlist[0].builder = 0;
}

int Config::config_local(const struct node_info *info, const uint32_t uuid)
{
	if (name_matching && hostname) {
		return strcmp(info->desc, hostname) == 0;
	} else
		return info->uuid == uuid;
}

