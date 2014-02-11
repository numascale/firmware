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

#include <stdio.h>
#include <string.h>

#include "../bootloader.h"
#include "../platform/options.h"
#include "../opteron/defs.h"
#include "acpi.h"

#define SHADOW_BASE 0xf0000
#define SHADOW_LEN 65536
#define RSDT_MAX 1024
#define FMTRR_WRITETHROUGH 0x1c1c1c1c1c1c1c1cULL

static struct acpi_rsdp *rptr = NULL;
static bool bios_shadowed = 0;

static void shadow_bios(void)
{
	printf("Shadowing BIOS...");
	int *area = (int *)malloc(SHADOW_LEN);
	assert(area);
	memcpy(area, (void *)SHADOW_BASE, SHADOW_LEN);
	uint64_t val = lib::rdmsr(MSR_SYSCFG);
	lib::wrmsr(MSR_SYSCFG, val | (3 << 18));
	disable_cache();
	lib::wrmsr(MSR_MTRR_FIX4K_F0000, FMTRR_WRITETHROUGH);
	lib::wrmsr(MSR_MTRR_FIX4K_F8000, FMTRR_WRITETHROUGH);
	enable_cache();
	lib::wrmsr(MSR_SYSCFG, val | (1 << 18));
	memcpy((void *)SHADOW_BASE, area, SHADOW_LEN);
	free(area);
	printf("done\n");
	bios_shadowed = 1;
}

uint8_t checksum(const acpi_sdt_p addr, const int len)
{
	uint8_t sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum -= *((uint8_t *)addr + i);

	return sum;
}

static void checksum_ok(acpi_sdt_p table, const int len)
{
	assertf(checksum(table, len) == 0, "ACPI table %.4s at 0x%p has bad checksum", table->sig.s, table);
}

static acpi_rsdp *find_rsdp(const char *start, int len)
{
	acpi_rsdp *ret = NULL;
	int i;

	for (i = 0; i < len; i += 16) {
		if (*(uint32_t *)(start + i) == STR_DW_H("RSD ") &&
		    *(uint32_t *)(start + i + 4) == STR_DW_H("PTR ")) {
			ret = (acpi_rsdp *)(start + i);
			break;
		}
	}

	return ret;
}

static bool rdsp_exists(void)
{
	const char *ebda = (char *)(*((unsigned short *)0x40e) * 16);
	rptr = find_rsdp(ebda, 1024);

	if (!rptr)
		rptr = find_rsdp((char *)0x0e0000, 128 * 1024);

	if (!rptr)
		return 0;

	return 1;
}

static acpi_sdt_p find_child(const char *sig, acpi_sdt_p parent, const int ptrsize)
{
	uint64_t childp;
	acpi_sdt_p table;
	int i;

	/* DSDT is linked from FACP table */
	if (!strcmp("DSDT", sig)) {
		acpi_sdt_p dsdt, facp = find_child("FACP", parent, ptrsize);
		assert(facp);
		memcpy(&dsdt, &facp->data[4], sizeof(facp));
		return dsdt;
	}

	for (i = 0; i + sizeof(*parent) < parent->len; i += ptrsize) {
		childp = 0;
		memcpy(&childp, &parent->data[i], ptrsize);

		if (childp > 0xffffffffULL) {
			printf("Error: Child pointer at %d (%p) outside 32-bit range (0x%llx)",
			       i, &parent->data[i], childp);
			continue;
		}

		memcpy(&table, &childp, sizeof(table));
		checksum_ok(table, table->len);

		if (table->sig.l == STR_DW_H(sig))
			return table;
	}

	return NULL;
}

/* Return the number of bytes after an ACPI table before the next */
static uint32_t slack(acpi_sdt_p parent)
{
	acpi_sdt_p next_table = (acpi_sdt_p)0xffffffff;
	acpi_sdt_p rsdt = (acpi_sdt_p)rptr->rsdt_addr;
	uint32_t *rsdt_entries = (uint32_t *) & (rsdt->data);
	uint64_t xsdtp;
	acpi_sdt_p xsdt;
	uint64_t *xsdt_entries;
	int i;

	for (i = 0; i * 4 + sizeof(*rsdt) < rsdt->len; i++) {
		acpi_sdt_p table = (acpi_sdt_p)rsdt_entries[i];
		checksum_ok(table, table->len);

		/* Find the nearest table after parent */
		if (table > parent && table < next_table)
			next_table = table;

		/* Check the FACP table for the DSDT table entry as well */
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt_p dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));
			checksum_ok(dsdt, dsdt->len);

			if (dsdt > parent && dsdt < next_table)
				next_table = dsdt;
		}
	}

	/* Check location of RSDT table also */
	if (rsdt > parent && rsdt < next_table)
		next_table = rsdt;

	xsdtp = rptr->xsdt_addr;

	if ((xsdtp == 0) || (xsdtp == ~0ULL))
		goto out;

	if (xsdtp > 0xffffffffULL) {
		printf("Error: XSDT ptr at (%p) outside 32-bit range (0x%llx)",
		       &rptr->xsdt_addr, xsdtp);
		goto out;
	}

	memcpy(&xsdt, &xsdtp, sizeof(xsdt));
	xsdt_entries = (uint64_t *) & (xsdt->data);

	for (i = 0; i * 8 + sizeof(*xsdt) < xsdt->len; i++) {
		uint64_t childp = xsdt_entries[i];
		acpi_sdt_p table;

		if (childp > 0xffffffffULL) {
			printf("Error: XSDT child pointer at %d (%p) outside 32-bit range (0x%llx)",
			       i, &xsdt_entries[i], childp);
			continue;
		}

		memcpy(&table, &childp, sizeof(table));
		checksum_ok(table, table->len);

		/* Find the nearest table after parent */
		if (table > parent && table < next_table)
			next_table = table;

		/* Check the FACP table for the DSDT table entry as well */
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt_p dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));
			checksum_ok(dsdt, dsdt->len);

			if (dsdt > parent && dsdt < next_table)
				next_table = dsdt;
		}
	}

	/* Check location of XSDT table also */
	if (xsdt > parent && xsdt < next_table)
		next_table = xsdt;

out:
	/* Calculate gap between end of parent and next table */
	return (uint32_t)next_table - (uint32_t)parent - parent->len;
}

bool replace_child(const char *sig, const acpi_sdt_p replacement, const acpi_sdt_p parent, const unsigned int ptrsize)
{
	uint64_t newp, childp;
	acpi_sdt_p table;

	checksum_ok(replacement, replacement->len);

	newp = 0;
	memcpy(&newp, &replacement, sizeof(replacement));
	int i;

	for (i = 0; i + sizeof(*parent) < parent->len; i += ptrsize) {
		childp = 0;
		memcpy(&childp, &parent->data[i], ptrsize);

		if (childp > 0xffffffffULL) {
			printf("Error: Child pointer at %d (%p) outside 32-bit range (0x%llx)",
			       i, &parent->data[i], childp);
			continue;
		}

		memcpy(&table, &childp, sizeof(table));
		checksum_ok(table, table->len);

		if (table->sig.l == STR_DW_H(sig)) {
			memcpy(&parent->data[i], &newp, ptrsize);

			/* Check if writing succeeded */
			if (memcmp(&parent->data[i], &newp, ptrsize))
				goto again;

			parent->checksum += checksum(parent, parent->len);
			return 1;
		}
	}

	/* Handled by caller */
	if (slack(parent) < ptrsize)
		return 0;

	/* Append entry to end of table */
	memcpy(&parent->data[i], &newp, ptrsize);

	/* Check if writing succeeded */
	if (memcmp(&parent->data[i], &newp, ptrsize))
		goto again;

	parent->len += ptrsize;
	assert(parent->len < RSDT_MAX);
	parent->checksum += checksum(parent, parent->len);
	return 1;

again:
	if (!bios_shadowed) {
		shadow_bios();
		return replace_child(sig, replacement, parent, ptrsize);
	}

	fatal("ACPI tables immutable when replacing child at 0x%p", &parent->data[i]);
}

void add_child(const acpi_sdt_p replacement, const acpi_sdt_p parent, const unsigned int ptrsize)
{
	/* If insufficient space, replace unimportant tables */
	if (slack(parent) < ptrsize) {
		const char *expendable[] = {"FPDT", "EINJ", "TCPA", "BERT", "ERST", "HEST"};
		for (unsigned int i = 0; i < (sizeof expendable / sizeof expendable[0]); i++) {
			if (replace_child(expendable[i], replacement, parent, ptrsize)) {
				printf("Replaced %s table\n", expendable[i]);
				return;
			}
		}

		fatal("Out of space when adding entry for ACPI table %s to %s", replacement->sig.s, parent->sig.s);
	}

	checksum_ok(replacement, replacement->len);
	uint64_t newp = 0;
	memcpy(&newp, &replacement, sizeof replacement);
	int i = parent->len - sizeof(*parent);
	memcpy(&parent->data[i], &newp, ptrsize);
	if (memcmp(&parent->data[i], &newp, ptrsize))
		goto again;

	parent->len += ptrsize;
	assert(parent->len < RSDT_MAX);
	parent->checksum += checksum(parent, parent->len);
	return;

again:
	if (!bios_shadowed) {
		shadow_bios();
		add_child(replacement, parent, ptrsize);
		return;
	}

	fatal("ACPI tables immutable when adding child at 0x%p", &parent->data[i]);
}

acpi_sdt_p find_root(const char *sig)
{
	if (!rdsp_exists())
		return NULL;

	checksum_ok((acpi_sdt_p)rptr, 20);

	if (STR_DW_H(sig) == STR_DW_H("RSDT"))
		return (acpi_sdt_p)rptr->rsdt_addr;

	if (STR_DW_H(sig) == STR_DW_H("XSDT")) {
		if (rptr->len >= 33) {
			checksum_ok((acpi_sdt_p)rptr, rptr->len);
			uint64_t xsdtp = rptr->xsdt_addr;
			if ((xsdtp == 0) || (xsdtp == ~0ULL))
				return NULL;

			if (xsdtp > 0xffffffffULL) {
				printf("Error: XSDT pointer at (%p) outside 32-bit range (0x%llx)",
				       &rptr->xsdt_addr, xsdtp);
				return NULL;
			}

			acpi_sdt_p xsdt;
			memcpy(&xsdt, &xsdtp, sizeof(xsdt));
			return xsdt;
		}
	}

	return NULL;
}

bool replace_root(const char *sig, const acpi_sdt_p replacement)
{
	if (!rdsp_exists())
		return 0;

	checksum_ok((acpi_sdt_p)rptr, 20);

	if (STR_DW_H(sig) == STR_DW_H("RSDT")) {
		rptr->rsdt_addr = (uint32_t)replacement;
		rptr->checksum += checksum((acpi_sdt_p)rptr, 20);

		if (rptr->len > 20)
			rptr->echecksum += checksum((acpi_sdt_p)rptr, rptr->len);

		return 1;
	}

	if (STR_DW_H(sig) == STR_DW_H("XSDT")) {
		if (rptr->len >= 33) {
			checksum_ok((acpi_sdt_p)rptr, rptr->len);
			rptr->xsdt_addr = (uint32_t)replacement;
			rptr->echecksum += checksum((acpi_sdt_p)rptr, rptr->len);
			return 1;
		}
	}

	return 0;
}

acpi_sdt_p find_sdt(const char *sig)
{
	acpi_sdt_p root = find_root("XSDT");
	if (root)
		return find_child(sig, root, 8);

	root = find_root("RSDT");
	if (root)
		return find_child(sig, root, 4);

	return NULL;
}

static void acpi_dump(const acpi_sdt_p table)
{
	int i;
	unsigned char *data = (unsigned char *)table;
	printf("Dumping %.4s:\n", table->sig.s);

	while (data < ((unsigned char *)table + table->len)) {
		for (i = 0; i < 8; i++) {
			uint32_t val = *(uint32_t *)data;
			printf(" 0x%08x,", val);
			data += sizeof(val);
		}

		printf("\n");
	}

	printf("\n");
}

bool acpi_append(const acpi_sdt_p parent, const int ptrsize, const char *sig, const unsigned char *extra, const uint32_t extra_len)
{
	/* Check if enough space to append to SSDT */
	acpi_sdt_p table = find_child(sig, parent, ptrsize);

	if (!table || slack(table) < extra_len)
		return 0;

	memcpy((unsigned char *)table + table->len, extra, extra_len);
	table->len += extra_len;
	table->checksum += checksum(table, table->len);

	if (options->debug.acpi)
		acpi_dump(table);

	return 1;
}

void debug_acpi(void)
{
	if (!rdsp_exists())
		return;

	printf("ACPI tables:\n");

	checksum_ok((acpi_sdt_p)rptr, 20);
	printf(" ptr:   %p, RSDP, %.6s, %d, %08x, %d\n",
	       rptr,
	       rptr->oemid,
	       rptr->revision,
	       rptr->rsdt_addr,
	       rptr->len);
	acpi_sdt_p rsdt = (acpi_sdt_p)rptr->rsdt_addr;

	checksum_ok(rsdt, rsdt->len);
	printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d, %d\n",
	       rsdt,
	       rsdt->sig.l,
	       rsdt->sig.s,
	       rsdt->oemid,
	       rsdt->oemtableid,
	       rsdt->checksum,
	       rsdt->revision,
	       rsdt->len,
	       sizeof(*rsdt));
	uint32_t *rsdt_entries = (uint32_t *) & (rsdt->data);

	for (int i = 0; i * 4 + sizeof(*rsdt) < rsdt->len; i++) {
		acpi_sdt_p table = (acpi_sdt_p)rsdt_entries[i];

		checksum_ok(table, table->len);
		printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
		       table,
		       table->sig.l,
		       table->sig.s,
		       table->oemid,
		       table->oemtableid,
		       table->checksum,
		       table->revision,
		       table->len);

		/* Find the DSDT table also */
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt_p dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));

			checksum_ok(dsdt, dsdt->len);
			printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
			       dsdt,
			       dsdt->sig.l,
			       dsdt->sig.s,
			       dsdt->oemid,
			       dsdt->oemtableid,
			       dsdt->checksum,
			       dsdt->revision,
			       dsdt->len);
		}

#ifdef UNUSED
		if (table->sig.l == STR_DW_H("SRAT")) {
			debug_acpi_srat(table);
		} else if (table->sig.l == STR_DW_H("APIC")) {
			debug_acpi_apic(table);
		}
#endif
	}

	if (rptr->len >= 33) {
		checksum_ok((acpi_sdt_p)rptr, rptr->len);

		if ((rptr->xsdt_addr != 0ULL) && (rptr->xsdt_addr != ~0ULL)) {
			acpi_sdt_p xsdt;
			memcpy(&xsdt, &rptr->xsdt_addr, sizeof(xsdt));

			checksum_ok(xsdt, xsdt->len);
			printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d, %d\n",
			       xsdt,
			       xsdt->sig.l,
			       xsdt->sig.s,
			       xsdt->oemid,
			       xsdt->oemtableid,
			       xsdt->checksum,
			       xsdt->revision,
			       xsdt->len,
			       sizeof(*xsdt));
			uint64_t *xsdt_entries = (uint64_t *) &(xsdt->data);
			int i;

			for (i = 0; i * 8 + sizeof(*xsdt) < xsdt->len; i++) {
				acpi_sdt_p table;
				memcpy(&table, &xsdt_entries[i], sizeof(table));

				checksum_ok(table, table->len);
				printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
				       table,
				       table->sig.l,
				       table->sig.s,
				       table->oemid,
				       table->oemtableid,
				       table->checksum,
				       table->revision,
				       table->len);

				/* Find the DSDT table also */
				if (table->sig.l == STR_DW_H("FACP")) {
					acpi_sdt_p dsdt;
					memcpy(&dsdt, &table->data[4], sizeof(dsdt));

					checksum_ok(dsdt, dsdt->len);
					printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
					       dsdt,
					       dsdt->sig.l,
					       dsdt->sig.s,
					       dsdt->oemid,
					       dsdt->oemtableid,
					       dsdt->checksum,
					       dsdt->revision,
					       dsdt->len);
				}

#ifdef UNUSED

				if (table->sig.l == STR_DW_H("SRAT")) {
					debug_acpi_srat(table);
				} else if (table->sig.l == STR_DW_H("APIC")) {
					debug_acpi_apic(table);
				}
#endif
			}
		}
	}
}

