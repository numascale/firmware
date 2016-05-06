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
#include "../numachip2/router.h"
#include "../node.h"
#include "options.h"
#include "config.h"

#ifndef SIM
extern "C" {
	#include "com32.h"
}
#endif

Config::Config(void)
{
	nnodes = 1;

	nodes[0].partition = 0;
	nodes[0].master = 1;
	nodes[0].id = 0;
	strcpy(prefix, "self");

	local_node = &nodes[0];
}

struct Config::node *Config::find(const sci_t id)
{
	for (unsigned n = 0; n < nnodes; n++)
		if (nodes[n].id == id)
			return &nodes[n];

	fatal("Failed to find %03x in configuration", id);
}

bool Config::parse_blank(const char *data)
{
	return *data == '#' || *data == '\n' || *data == '\0';
}

bool Config::parse_prefix(const char *data)
{
	return sscanf(data, "prefix=%s\n", prefix);
}

bool Config::parse_partition(const char *data)
{
	char part[32];
	char unified[8];
	int ret = sscanf(data, "label=%s unified=%s\n", part, unified);
	if (!ret)
		return 0;

	if (ret == 1)
		fatal("Partition line missing 'unified=<true|false>' argument");

	xassert(ret == 2);
	strcpy(partitions[npartitions].label, part);
	partitions[npartitions].unified = !strcmp(unified, "true");
	npartitions++;

	return ret;
}

bool Config::parse_node(char const *data)
{
	nodeid_t suffix;
	char mac[32];
	unsigned partition;
	char ports[32];
	memset(ports, 0, sizeof(ports));

	int ret = sscanf(data, "suffix=%hhu mac=%s partition=%u ports=%[A-F0-6, ]", &suffix, mac, &partition, ports);
	if (ret > 0 && ret < 3) // ports arguments needs to be optional
		fatal("Malformed config file node line; syntax is eg 'suffix=01 mac=0025905a7810 partition=1 ports= , ,02A,03A,04A' but only %d parsed\nInput is [%s]", ret, data);

	xassert(suffix > 0 && suffix < 64);
	nodes[nnodes].id = suffix - 1;

	// parse MAC address
	for (unsigned i = 0; i < 6; i++) {
		nodes[nnodes].mac[i] = strtoul(&mac[i*3], NULL, 16);
		if (i < 5 && mac[i*3+2] != ':')
			fatal("MAC address separator should be ':' not '%c'", mac[i*3+2]);
	}

	char *p = ports;
	unsigned q = 1;

	// parse ports
	while (1) {
		// skip whitespace
		if (*p == ' ') {
			p++;
			continue;
		}

		if (*p == ',') {
			p++;
			q++;
			continue;
		}

		char *end;
		unsigned rnode = strtoul(p, &end, 10);
		if (end == p)
			break;

		p = end; // move to suffix
		unsigned port = *p - 'A' + 1;
		xassert(port <= 6);
		p++; // move to next char

		rnode--; // array starts from 0, not 1
		router->neigh[nnodes][q] = {(nodeid_t)rnode, (xbarid_t)port};
		nodes[nnodes].portmask |= 1 << (q - 1);

		// setup the reverse of the connection
		router->neigh[rnode][port] = {(nodeid_t)nnodes, (xbarid_t)q};
		nodes[rnode].portmask |= 1 << (port - 1);

		if (options->debug.config)
			printf(", %02u%c-%02u%c", nnodes+1, 'A'+q-1, rnode+1, 'A'+port-1);
	}

	nodes[nnodes].partition = partition - 1; // starts from 1
	nodes[nnodes].master = nnodes == 0; // FIXME
	nnodes++;

	return !!ret;
}

void Config::parse(const char *pos)
{
	while (1) {
		char *eol = strchr(pos, '\n');
		if (!eol)
			break;

		// parse
		if (!parse_blank(pos) && !parse_prefix(pos) && !parse_partition(pos) && !parse_node(pos))
			fatal("unexpected line in config file:\n%s<END>", pos);

		pos = eol + 1;
	}
}

Config::Config(const char *filename)
{
	size_t len;
	printf("Config %s", filename);
	const char *data = os->read_file(filename, &len);
	assertf(data && len > 0, "Failed to open file");

	parse(data);
	lfree((char *)data);

	if (options->debug.config) {
		printf("\n");

		for (unsigned i = 0; i < nnodes; i++) {
			printf("Node %u: hostname %s%02u, MAC %02x:%02x:%02x:%02x:%02x:%02x, ",
			  i, prefix, nodes[i].id, nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
			  nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);

			printf("%03x, ", nodes[i].id);
			printf("partition %u (%sunified)\n", nodes[i].partition, partitions[nodes[i].partition].unified ? "" : "non-");
		}
	}

	// find local MAC address
	for (unsigned i = 0; i < nnodes; i++) {
		if (!memcmp(os->mac, nodes[i].mac, sizeof(os->mac))) {
			if (options->debug.config)
				printf("MAC matches node %u", i);
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

	printf("; partition %u %sunified; %03x; %s\n", local_node->partition, partitions[local_node->partition].unified ? "" : "non-", local_node->id, local_node->master ? "master" : "slave");
}
