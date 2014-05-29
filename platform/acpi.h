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

#ifndef __ACPI_H
#define __ACPI_H

#include <inttypes.h>
#include <stdbool.h>

#include "../library/base.h"
#include "smbios.h"

struct acpi_rsdp {
	unsigned char sig[8];
	uint8_t checksum;
	unsigned char oemid[6];
	uint8_t revision;
	uint32_t rsdt_addr;
	uint32_t len;
	uint64_t xsdt_addr;
	uint8_t echecksum;
} __attribute__((packed));

struct acpi_sdt {
	union {
		char s[4];
		uint32_t l;
	} sig;
	uint32_t len;
	uint8_t revision;
	uint8_t checksum;
	unsigned char oemid[6];
	unsigned char oemtableid[8];
	uint32_t oemrev;
	unsigned char creatorid[4];
	uint32_t creatorrev;
	unsigned char data[0];
} __attribute__((packed));

struct acpi_core_affinity {
	uint8_t type;
	uint8_t len;
	uint8_t prox_low;
	uint8_t apic_id;
	unsigned int enabled: 1;
	unsigned int flags: 31;
	uint8_t sapic_eid;
	unsigned int prox_hi: 24;
	char reserved[4];
} __attribute__((packed));

struct acpi_mem_affinity {
	uint8_t type;
	uint8_t len;
	uint32_t prox_dom;
	char reserved1[2];
	uint64_t mem_base;
	uint64_t mem_size;
	char reserved2[4];
	unsigned int enabled: 1;
	unsigned int hotplug: 1;
	unsigned int nonvol: 1;
	unsigned int reserved3: 29;
	char reserved4[8];
} __attribute__((packed));

struct acpi_x2apic_affinity {
	uint8_t type;
	uint8_t len;
	char reserved1[2];
	uint32_t prox_dom;
	uint32_t x2apic_id;
	unsigned int enabled: 1;
	unsigned int flags: 31;
	uint32_t clock_dom;
	char reserved2[4];
} __attribute__((packed));

struct acpi_local_apic {
	uint8_t type;
	uint8_t len;
	uint8_t proc_id;
	uint8_t apic_id;
	uint32_t flags;
} __attribute__((packed));

struct acpi_local_x2apic {
	uint8_t type;
	uint8_t len;
	char reserved1[2];
	uint32_t x2apic_id;
	uint32_t flags;
	uint32_t proc_uid;
} __attribute__((packed));

struct acpi_mcfg {
	uint64_t address;		/* Base address, processor-relative */
	uint16_t pci_segment;	/* PCI segment group number */
	uint8_t start_bus_number;	/* Starting PCI Bus number */
	uint8_t end_bus_number;	/* Final PCI Bus number */
	uint32_t reserved;
} __attribute__((packed));

class AcpiTable {
	static const unsigned acpi_rev = 2; // 64-bit pointers; ACPI 2-5
	static const unsigned chunk = 1024;
	void checksum(void);
public:
	struct acpi_sdt header;
	char *payload;
	unsigned allocated, used;

	AcpiTable(const char *name);
	void append(const char *data, const unsigned len);
};

class ACPI {
	SMBIOS smbios;
	struct acpi_rsdp *rptr;
	struct acpi_sdt *rsdt, *xsdt;
	bool bios_shadowed;

	void shadow_bios(void);
	acpi_rsdp *find_rsdp(const char *start, int len);
	acpi_sdt *find_child(const char *sig, const acpi_sdt *parent, const int ptrsize);
	uint32_t slack(acpi_sdt *parent);
	static void dump(const acpi_sdt *table, const unsigned limit);
public:
	static void assert_checksum(const acpi_sdt *table, const int len);
	static checked uint8_t checksum(const char *addr, const int len);
	void check(void);
	checked bool replace_child(const char *sig, const acpi_sdt *replacement, acpi_sdt *parent, const unsigned int ptrsize);
	void add_child(const acpi_sdt *replacement, acpi_sdt *parent, unsigned int ptrsize);
	checked acpi_sdt *find_root(const char *sig);
	checked bool replace_root(const char *sig, const acpi_sdt *replacement);
	checked acpi_sdt *find_sdt(const char *sig);
	checked bool append(const acpi_sdt *parent, const int ptrsize, const char *sig, const unsigned char *extra, const uint32_t extra_len);
	void handover(void);
	ACPI(void);
	void replace(const AcpiTable &table);
};

#endif
