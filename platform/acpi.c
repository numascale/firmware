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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "../bootloader.h"
#include "../platform/options.h"
#include "../opteron/msrs.h"
#include "../library/utils.h"
#include "acpi.h"

#define SHADOW_BASE 0xf0000
#define SHADOW_LEN 65536
#define RSDT_MAX 1024
#define FMTRR_WRITETHROUGH 0x1c1c1c1c1c1c1c1cULL
#define BIOS_ACPI     0x3f000000

void ACPI::shadow_bios(void)
{
	printf("Shadowing BIOS");
	void *area = malloc(SHADOW_LEN);
	xassert(area);
	memcpy(area, (void *)SHADOW_BASE, SHADOW_LEN);

	// disable fixed MTRRs
	uint64_t val = lib::rdmsr(MSR_SYSCFG);
	lib::wrmsr(MSR_SYSCFG, val | (3 << 18));
	disable_cache();
	lib::wrmsr(MSR_MTRR_FIX4K_F0000, FMTRR_WRITETHROUGH);
	lib::wrmsr(MSR_MTRR_FIX4K_F8000, FMTRR_WRITETHROUGH);
	enable_cache();

	// reenable fixed MTRRs
	lib::wrmsr(MSR_SYSCFG, val | (1 << 18));
	memcpy((void *)SHADOW_BASE, area, SHADOW_LEN);
	free(area);
	printf("\n");
	bios_shadowed = 1;
}

uint8_t ACPI::checksum(const char *addr, const unsigned len)
{
	uint8_t sum = 0;
	for (unsigned i = 0; i < len; i++)
		sum -= addr[i];

	return sum;
}

void ACPI::assert_checksum(const acpi_sdt *table, const unsigned len)
{
	if (checksum((const char *)table, len) == 0)
		return;

	dump(table, 256);
	fatal("ACPI table %.4s at 0x%p has bad checksum", table->sig.s, table);
}

acpi_rsdp *ACPI::find_rsdp(const char *start, const unsigned len)
{
	acpi_rsdp *ret = NULL;

	for (unsigned i = 0; i < len; i += 16) {
		if (*(uint32_t *)(start + i) == STR_DW_H("RSD ") &&
		    *(uint32_t *)(start + i + 4) == STR_DW_H("PTR ")) {
			ret = (acpi_rsdp *)(start + i);
			break;
		}
	}

	return ret;
}

acpi_sdt *ACPI::find_child(const char *sig, const acpi_sdt *parent, const unsigned ptrsize)
{
	uint64_t childp;
	acpi_sdt *table;

	// DSDT is linked from FACP table
	if (!strcmp("DSDT", sig)) {
		acpi_sdt *dsdt, *facp = find_child("FACP", parent, ptrsize);
		xassert(facp);
		memcpy(&dsdt, &facp->data[4], sizeof(facp));
		return dsdt;
	}

	for (unsigned i = 0; i + sizeof(*parent) < parent->len; i += ptrsize) {
		childp = 0;
		memcpy(&childp, &parent->data[i], ptrsize);

		if (childp > 0xffffffffULL) {
			printf("Error: Child pointer at %d (%p) outside 32-bit range (0x%" PRIx64 ")",
			       i, &parent->data[i], childp);
			continue;
		}

		memcpy(&table, &childp, sizeof(table));
		assert_checksum(table, table->len);

		if (table->sig.l == STR_DW_H(sig))
			return table;
	}

	return NULL;
}

// return the number of bytes after an ACPI table before the next
uint32_t ACPI::slack(const acpi_sdt *parent)
{
	acpi_sdt *next_table = (acpi_sdt *)0xffffffff;
	uint32_t *rsdt_entries = (uint32_t *) & (rsdt->data);

	for (unsigned i = 0; i * 4 + sizeof(*rsdt) < rsdt->len; i++) {
		acpi_sdt *table = (acpi_sdt *)rsdt_entries[i];
		assert_checksum(table, table->len);

		// find the nearest table after parent
		if (table > parent && table < next_table)
			next_table = table;

		// check the FACP table for the DSDT table entry as well
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt *dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));
			assert_checksum(dsdt, dsdt->len);

			if (dsdt > parent && dsdt < next_table)
				next_table = dsdt;
		}
	}

	// check location of RSDT table also
	if (rsdt > parent && rsdt < next_table)
		next_table = rsdt;

	uint64_t *xsdt_entries = (uint64_t *) & (xsdt->data);

	for (unsigned i = 0; i * 8 + sizeof(*xsdt) < xsdt->len; i++) {
		uint64_t childp = xsdt_entries[i];
		acpi_sdt *table;

		if (childp > 0xffffffffULL) {
			printf("Error: XSDT child pointer at %d (%p) outside 32-bit range (0x%" PRIx64 ")",
			       i, &xsdt_entries[i], childp);
			continue;
		}

		memcpy(&table, &childp, sizeof(table));
		assert_checksum(table, table->len);

		// find the nearest table after parent
		if (table > parent && table < next_table)
			next_table = table;

		// check the FACP table for the DSDT table entry as well
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt *dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));
			assert_checksum(dsdt, dsdt->len);

			if (dsdt > parent && dsdt < next_table)
				next_table = dsdt;
		}
	}

	// check location of XSDT table also
	if (xsdt > parent && xsdt < next_table)
		next_table = xsdt;

	// calculate gap between end of parent and next table
	return (uint32_t)next_table - (uint32_t)parent - parent->len;
}

bool ACPI::replace_child(const char *sig, const acpi_sdt *replacement, acpi_sdt *const parent, const unsigned ptrsize)
{
	uint64_t newp = 0, childp;

	assert_checksum(replacement, replacement->len);
	memcpy(&newp, &replacement, sizeof(replacement));
	unsigned i;

	for (i = 0; i + sizeof(*parent) < parent->len; i += ptrsize) {
		childp = 0;
		memcpy(&childp, &parent->data[i], ptrsize);

		if (childp > 0xffffffffULL) {
			printf("Error: Child pointer at %d (%p) outside 32-bit range (0x%" PRIx64 ")",
			       i, &parent->data[i], childp);
			continue;
		}

		acpi_sdt *table;
		memcpy(&table, &childp, sizeof(table));
		assert_checksum(table, table->len);

		// DSDT is special-cased
		if (table->sig.l == STR_DW_H("FACP") && !strncmp(sig, "DSDT", 4)) {
			struct acpi_fadt *fadt = (struct acpi_fadt *)&table->data;
			fadt->Dsdt = (uint32_t)replacement;
			if (ptrsize == 8)
				fadt->X_Dsdt = (uint64_t)replacement;
			table->checksum = 0;
			table->checksum = checksum((const char *)table, table->len);
			return 1;
		}

		if (table->sig.l == STR_DW_H(sig)) {
			memcpy(&parent->data[i], &newp, ptrsize);
			// check if writing succeeded
			if (memcmp(&parent->data[i], &newp, ptrsize))
				goto again;

			parent->checksum += checksum((const char *)parent, parent->len);
			return 1;
		}
	}

	// handled by caller
	if (slack(parent) < ptrsize)
		return 0;

	// append entry to end of table
	memcpy(&parent->data[i], &newp, ptrsize);

	// check if writing succeeded
	if (memcmp(&parent->data[i], &newp, ptrsize))
		goto again;

	parent->len += ptrsize;
	xassert(parent->len < RSDT_MAX);
	parent->checksum += checksum((const char *)parent, parent->len);
	return 1;

again:
	if (!bios_shadowed) {
		shadow_bios();
		return replace_child(sig, replacement, parent, ptrsize);
	}

	fatal("ACPI tables immutable when replacing child at 0x%p", &parent->data[i]);
}

void ACPI::add_child(const acpi_sdt *replacement, acpi_sdt *const parent, const unsigned ptrsize)
{
	// if insufficient space, replace unimportant tables
	if (slack(parent) < ptrsize) {
		const char *expendable[] = {"FPDT", "EINJ", "TCPA", "BERT", "ERST", "HEST"};
		for (unsigned i = 0; i < (sizeof expendable / sizeof expendable[0]); i++) {
			if (replace_child(expendable[i], replacement, parent, ptrsize)) {
				printf("Replaced %s table\n", expendable[i]);
				return;
			}
		}

		fatal("Out of space when adding entry for ACPI table %s to %s", replacement->sig.s, parent->sig.s);
	}

	assert_checksum(replacement, replacement->len);
	uint64_t newp = 0;
	memcpy(&newp, &replacement, sizeof replacement);
	unsigned i = parent->len - sizeof(*parent);
	memcpy(&parent->data[i], &newp, ptrsize);
	if (memcmp(&parent->data[i], &newp, ptrsize))
		goto again;

	parent->len += ptrsize;
	xassert(parent->len < RSDT_MAX);
	parent->checksum += checksum((const char *)parent, parent->len);
	return;

again:
	if (!bios_shadowed) {
		shadow_bios();
		add_child(replacement, parent, ptrsize);
		return;
	}

	fatal("ACPI tables immutable when adding child at 0x%p", &parent->data[i]);
}

acpi_sdt *ACPI::find_root(const char *sig)
{
	assert_checksum((acpi_sdt *)rptr, 20);

	if (STR_DW_H(sig) == STR_DW_H("RSDT"))
		return (acpi_sdt *)rptr->rsdt_addr;

	if (STR_DW_H(sig) == STR_DW_H("XSDT")) {
		if (rptr->len >= 33) {
			assert_checksum((acpi_sdt *)rptr, rptr->len);
			uint64_t xsdtp = rptr->xsdt_addr;
			if ((xsdtp == 0) || (xsdtp == ~0ULL))
				return NULL;

			xassert(xsdtp < 0x100000000);
			acpi_sdt *val;
			memcpy(&val, &xsdtp, sizeof(xsdt));
			return val;
		}
	}

	return NULL;
}

bool ACPI::replace_root(const char *sig, const acpi_sdt *replacement)
{
	assert_checksum((acpi_sdt *)rptr, 20);

	if (STR_DW_H(sig) == STR_DW_H("RSDT")) {
		rptr->rsdt_addr = (uint32_t)replacement;
		rptr->checksum += checksum((const char *)rptr, 20);

		if (rptr->len > 20)
			rptr->echecksum += checksum((const char *)rptr, rptr->len);

		return 1;
	}

	if (STR_DW_H(sig) == STR_DW_H("XSDT")) {
		if (rptr->len >= 33) {
			assert_checksum((acpi_sdt *)rptr, rptr->len);
			rptr->xsdt_addr = (uint32_t)replacement;
			rptr->echecksum += checksum((const char *)rptr, rptr->len);
			return 1;
		}
	}

	return 0;
}

acpi_sdt *ACPI::find_sdt(const char *sig)
{
	if (xsdt)
		return find_child(sig, xsdt, 8);

	if (rsdt)
		return find_child(sig, rsdt, 4);

	return NULL;
}

void ACPI::allocate(const unsigned len)
{
	allocated = (char *)BIOS_ACPI;
	xassert(allocated);
	nallocated = len;
	used = 0;
	e820->add((uint64_t)(uint32_t)allocated, nallocated, E820::ACPI);
}

void ACPI::dump(const acpi_sdt *table, const unsigned limit)
{
	printf("Dumping %.4s:\n", table->sig.s);

	unsigned n = table->len;
	if (limit)
		n = min(n, limit);

	lib::dump(table, n);
	if (limit && limit < table->len)
		printf("[...]");
	printf("\n");
}

bool ACPI::append(const char *sig, const char *extra, const uint32_t extra_len)
{
	// check if enough space to append
	acpi_sdt *table = find_child(sig, rsdt, 4);

	if (!table || slack(table) < extra_len)
		return 0;

	memcpy((char *)table + table->len, extra, extra_len);
	table->len += extra_len;
	table->checksum += checksum((const char *)table, table->len);

	if (options->debug.acpi)
		dump(table, 0);

	return 1;
}

void ACPI::check(void)
{
	assert_checksum((acpi_sdt *)rptr, 20);
	assert_checksum(rsdt, rsdt->len);

	if (options->debug.acpi) {
		printf("ACPI tables:\n");

		printf(" ptr:   %p, RSDP, %.6s, %d, %08x, %d\n",
			   rptr,
			   rptr->oemid,
			   rptr->revision,
			   rptr->rsdt_addr,
			   rptr->len);

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
	}

	uint32_t *rsdt_entries = (uint32_t *)&(rsdt->data);

	for (unsigned i = 0; i * 4 + sizeof(*rsdt) < rsdt->len; i++) {
		acpi_sdt *table = (acpi_sdt *)rsdt_entries[i];
		assert_checksum(table, table->len);

		if (options->debug.acpi)
			printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
				table, table->sig.l, table->sig.s, table->oemid,
				table->oemtableid, table->checksum, table->revision, table->len);

		// find the DSDT table also
		if (table->sig.l == STR_DW_H("FACP")) {
			acpi_sdt *dsdt;
			memcpy(&dsdt, &table->data[4], sizeof(dsdt));
			assert_checksum(dsdt, dsdt->len);

			if (options->debug.acpi)
				printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
					dsdt, dsdt->sig.l, dsdt->sig.s, dsdt->oemid,
					dsdt->oemtableid, dsdt->checksum, dsdt->revision, dsdt->len);

			for (unsigned j = 0; j < 4; j++) {
				char c = (dsdt->sig.l >> (j * 8)) & 0xff;
				assertf(c >= 65 && c <= 90, "Non-printable characters in table name");
			}
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
		assert_checksum((acpi_sdt *)rptr, rptr->len);

		if ((rptr->xsdt_addr != 0ULL) && (rptr->xsdt_addr != ~0ULL)) {
			acpi_sdt *xsdtp;
			memcpy(&xsdtp, &rptr->xsdt_addr, sizeof(xsdtp));
			assert_checksum(xsdtp, xsdtp->len);

			if (options->debug.acpi)
				printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d, %d\n",
					xsdtp, xsdtp->sig.l, xsdtp->sig.s, xsdtp->oemid,
					xsdtp->oemtableid, xsdtp->checksum, xsdtp->revision, xsdtp->len,
					sizeof(*xsdtp));

			uint64_t *xsdt_entries = (uint64_t *)&(xsdtp->data);

			for (unsigned i = 0; i * 8 + sizeof(*xsdtp) < xsdtp->len; i++) {
				acpi_sdt *table;
				memcpy(&table, &xsdt_entries[i], sizeof(table));
				assert_checksum(table, table->len);

				if (options->debug.acpi)
					printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
						table, table->sig.l, table->sig.s, table->oemid,
						table->oemtableid, table->checksum, table->revision,
						table->len);

				// find the DSDT table also
				if (table->sig.l == STR_DW_H("FACP")) {
					acpi_sdt *dsdt;
					memcpy(&dsdt, &table->data[4], sizeof(dsdt));
					assert_checksum(dsdt, dsdt->len);

					if (options->debug.acpi)
						printf(" table: %p, %08x, %.4s, %.6s, %.8s, %d, %d, %d\n",
							dsdt, dsdt->sig.l, dsdt->sig.s, dsdt->oemid,
							dsdt->oemtableid, dsdt->checksum, dsdt->revision,
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

void ACPI::handover(void)
{
	acpi_sdt *fadt = find_sdt("FACP");
	xassert(fadt);

	char *val = &fadt->data[48 - 36];
	const uint32_t smi_cmd = *(uint32_t *)val;
	const uint8_t acpi_enable = fadt->data[52 - 36];

	if (!smi_cmd || !acpi_enable) {
		printf("ACPI legacy support not enabled\n");
		return;
	}

	val = &fadt->data[64 - 36];
	const uint32_t acpipm1cntblk = *(uint32_t *)val;
	uint16_t sci_en = inb(acpipm1cntblk);

	if ((sci_en & 1) == 1) {
		printf("ACPI already handed over\n");
		return;
	}

	outb(acpi_enable, smi_cmd);
	unsigned limit = 1000;

	do {
		lib::udelay(1000);
		sci_en = inb(acpipm1cntblk);

		if ((sci_en & 1) == 1)
			return;
	} while (--limit);

	fatal("ACPI handover timed out");
}

void ACPI::get_cores(void)
{
	acpi_sdt *srat = find_sdt("SRAT");
	unsigned i = 12;
	napics = 0;

	while (i + sizeof(*srat) < srat->len) {
		switch(srat->data[i]) {
		case 0: {
			struct acpi_apic_affinity *ent =
			    (struct acpi_apic_affinity *)&(srat->data[i]);

			if (ent->flags & 1) {
				xassert(ent->apicid != 0xff);
				apics[napics] = ent->apicid;
				napics++;
			}

			i += ent->length;
			break;
		}
		case 1: {
			struct acpi_mem_affinity *ent =
			    (struct acpi_mem_affinity *)&(srat->data[i]);
			i += ent->length;
			break;
		}
		default:
			fatal("Unexpected SRAT entry %d", srat->data[i]);
		}
	}
}

ACPI::ACPI(void): bios_shadowed(0)
{
	// skip if already set
	if (!options->handover_acpi) {
		// systems where ACPI must be handed off early
		const char *acpi_blacklist[] = {"H8QGL", NULL};

		for (unsigned i = 0; i < (sizeof acpi_blacklist / sizeof acpi_blacklist[0]); i++) {
			if (!strcmp(smbios.boardproduct, acpi_blacklist[i])) {
				printf(" (ACPI workaround)");
				options->handover_acpi = 1;
				break;
			}
		}
	}

	// find table root in EBDA
	rptr = find_rsdp((const char *)(*((unsigned short *)0x40e) * 16), 1024);

	// find table root in alternative location
	if (!rptr)
		rptr = find_rsdp((const char *)0xe0000, 131072);
	xassert(rptr);

	xsdt = find_root("XSDT");
	xassert(xsdt);

	rsdt = find_root("RSDT");
	xassert(rsdt);

	if (options->debug.acpi)
		printf("RSDT at %p; XSDT at %p\n", rsdt, xsdt);

	printf("\n");

	get_cores();
}

AcpiTable::AcpiTable(const char *name, const unsigned rev, const bool copy): payload(0), allocated(0), used(0)
{
	memset(&header, 0, sizeof(header));
	memcpy(&header.sig.s, name, 4);
	header.len = sizeof(header);
	header.revision = rev;
	memcpy(&header.oemid, "NUMASC", 6);
	memcpy(&header.oemtableid, "NCONECT2", 8);
	header.oemrev = 0;
	memcpy(&header.creatorid, "1B47", 4);
	header.creatorrev = 1;
	header.checksum = 0;

	if (copy) {
		acpi_sdt *table = acpi->find_sdt(name);
		xassert(table);
		append(table->data, table->len - sizeof(struct acpi_sdt));
	}
}

void AcpiTable::extend(const unsigned len)
{
	if (used + len > allocated) {
		allocated = roundup(used + len, chunk);
		payload = (char *)realloc((void *)payload, allocated);
		xassert(payload);
	}
}

void AcpiTable::append(const char *data, const unsigned len)
{
	extend(len);

/*	if (options->debug.acpi) {
		printf("ACPI append: ");
		lib::memcpy(payload + used, data, len);
	} else */
		memcpy(payload + used, data, len);

	header.len += len;
	used += len;
}

// return offset from payload of zero-initialised memory
unsigned AcpiTable::reserve(const unsigned len)
{
	extend(len);

	if (options->debug.acpi)
		printf("ACPI extend by %u bytes\n", len);

	memset(payload + used, 0, len);

	header.len += len;

	unsigned offset = used;
	used += len;

	return offset;
}

void AcpiTable::increment64(const unsigned offset) const
{
   uint64_t *p = (uint64_t *)((unsigned)payload + offset);
   (*p)++;
}

void ACPI::add(const AcpiTable &table)
{
	acpi_sdt *table2 = (acpi_sdt *)(allocated + used);
	//(acpi_sdt *)e820->expand(E820::ACPI, table.header.len);

	used += sizeof(struct acpi_sdt) + table.used;
	xassert(used < nallocated);
	memcpy(table2, &table.header, sizeof(struct acpi_sdt));
	memcpy((char *)table2 + sizeof(struct acpi_sdt), table.payload, table.used);

	table2->checksum = 0;
	table2->checksum = checksum((const char *)table2, table2->len);

	add_child(table2, xsdt, 8);
	add_child(table2, rsdt, 4);
}

void ACPI::replace(const AcpiTable &table)
{
	acpi_sdt *table2 = (acpi_sdt *)(allocated + used);
	//(acpi_sdt *)e820->expand(E820::ACPI, table.header.len);

	used += sizeof(struct acpi_sdt) + table.used;
	xassert(used < nallocated);
	memcpy(table2, &table.header, sizeof(struct acpi_sdt));
	memcpy((char *)table2 + sizeof(struct acpi_sdt), table.payload, table.used);

	table2->checksum = 0;
	table2->checksum = checksum((const char *)table2, table2->len);

	if (rsdt)
		xassert(replace_child(table2->sig.s, table2, rsdt, 4));
	if (xsdt)
		xassert(replace_child(table2->sig.s, table2, xsdt, 8));
}
