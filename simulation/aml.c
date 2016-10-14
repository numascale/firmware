#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "../platform/aml.h"
#include "../platform/acpi.h"
#include "../platform/config.h"
#include "../node.h"

#include <assert.h>

uint16_t dnc_node_count = 0;
int verbose = 2;
bool remote_io = 1;
bool test_manufacture = 0;
Node **nodes;
Node *local_node;
unsigned nnodes;

/* Insert SSDT dumped from booting with verbose=2 into array */
static uint32_t table[] = {
};

uint8_t checksum(const acpi_sdt *addr, const int len)
{
	uint8_t sum = 0;

	for (int i = 0; i < len; i++)
		sum -= *((uint8_t *)addr + i);

	return sum;
}

static void launch(const char *cmdline)
{
	printf("[%s]\n", cmdline);
	int rc = system(cmdline);
	if (rc)
		 printf("warning: command returned rc %d\n", rc);
}

static void gen(void)
{
	char filename[32];

	snprintf(filename, sizeof(filename), "SSDT-%dnodes.amx", nnodes);

	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	assert(fd != -1);

	if (sizeof(table)) {
		assert(write(fd, table, sizeof(table)) == sizeof(table));
		printf("wrote %s (%zd bytes, %d nodes)\n", filename, sizeof(table), nnodes);
	} else {
		uint32_t extra_len;
		dnc_node_count = nnodes;
		char *extra = remote_aml(&extra_len);

		acpi_sdt *ssdt = (acpi_sdt *)malloc(sizeof(struct acpi_sdt) + extra_len);
		memcpy(ssdt->sig.s, "SSDT", 4);
		ssdt->revision = 2; // FIXME: check
		memcpy(ssdt->oemid, "NUMASC", 6);
		memcpy(ssdt->oemtableid, "NCONNECT", 8);
		ssdt->oemrev = 0;
		memcpy(ssdt->creatorid, "INTL", 4);
		ssdt->creatorrev = 0x20100528;
		memcpy(ssdt->data, extra, extra_len);
		ssdt->len = offsetof(struct acpi_sdt, data) + extra_len;
		ssdt->checksum = 0;
		ssdt->checksum = checksum(ssdt, ssdt->len);

		assert(write(fd, ssdt, ssdt->len) == ssdt->len);
		printf("wrote %s (%d bytes, %d nodes)\n", filename, ssdt->len, nnodes);
		free(ssdt);
	}

	assert(close(fd) == 0);

	char cmdline[64], filename2[32];

	/* Disassemble generated AML to .dsl file */
	snprintf(cmdline, sizeof(cmdline), "iasl -vs -w3 -d %s 2>/dev/null", filename);
	launch(cmdline);
	strncpy(filename2, filename, sizeof(filename2));
	strcpy(strrchr(filename2, '.'), ".dsl");

	/* Reassemble to .aml file */
	snprintf(cmdline, sizeof(cmdline), "iasl -vs -w3 %s >/dev/null", filename2);
	launch(cmdline);
	strcpy(strrchr(filename2, '.'), ".aml");

	/* Diff output */
	snprintf(cmdline, sizeof(cmdline), "diff %s %s", filename, filename2);
	launch(cmdline);
}

int main(void)
{
	nnodes = 8;
	nodes = (Node **)malloc(sizeof(Node *) * nnodes);
	assert(nodes);

	struct Config::node cfg = {};

	for (sci_t n = 0; n < nnodes; n++)
		nodes[n] = new Node(&cfg, n);

	gen();

	free(nodes);
	return 0;
}

