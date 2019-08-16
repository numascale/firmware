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

#define __STDC_FORMAT_MACROS
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
	char data[0];
} __attribute__((packed));

struct acpi_x2apic_apic {
	uint8_t type, length;
	uint16_t reserved;
	uint32_t x2apic_id, flags, acpi_uid;
} __attribute__((packed));

struct acpi_mem_affinity {
	uint8_t type, length;
	uint32_t proximity;
	uint16_t reserved1;
	uint64_t base;
	uint32_t lengthlo, lengthhi;
	uint32_t reserved2, flags;
	uint32_t reserved3[2];
} __attribute__((packed));

struct acpi_apic_affinity {
	uint8_t type, length;
	uint8_t proximity1, apicid;
	uint32_t flags;
	uint8_t sapicid;
	uint32_t proximity2 : 24;
	uint32_t clock;
} __attribute__((packed));

struct acpi_x2apic_affinity {
	uint8_t type, length;
	uint16_t reserved1;
	uint32_t proximity, x2apicid, flags, clock, reserved2;
} __attribute__((packed));

struct acpi_mcfg {
	uint64_t address;		/* Base address, processor-relative */
	uint16_t pci_segment;	/* PCI segment group number */
	uint8_t start_bus_number;	/* Starting PCI Bus number */
	uint8_t end_bus_number;	/* Final PCI Bus number */
	uint32_t reserved;
} __attribute__((packed));

struct GenericAddressStructure {
	uint8_t AddressSpace;
	uint8_t BitWidth;
	uint8_t BitOffset;
	uint8_t AccessSize;
	uint64_t Address;
} __attribute__((packed));

struct acpi_fadt {
	uint32_t FirmwareCtrl;
	uint32_t Dsdt;
	uint8_t  Reserved;
	uint8_t  PreferredPowerManagementProfile;
	uint16_t SCI_Interrupt;
	uint32_t SMI_CommandPort;
	uint8_t  AcpiEnable;
	uint8_t  AcpiDisable;
	uint8_t  S4BIOS_REQ;
	uint8_t  PSTATE_Control;
	uint32_t PM1aEventBlock;
	uint32_t PM1bEventBlock;
	uint32_t PM1aControlBlock;
	uint32_t PM1bControlBlock;
	uint32_t PM2ControlBlock;
	uint32_t PMTimerBlock;
	uint32_t GPE0Block;
	uint32_t GPE1Block;
	uint8_t  PM1EventLength;
	uint8_t  PM1ControlLength;
	uint8_t  PM2ControlLength;
	uint8_t  PMTimerLength;
	uint8_t  GPE0Length;
	uint8_t  GPE1Length;
	uint8_t  GPE1Base;
	uint8_t  CStateControl;
	uint16_t WorstC2Latency;
	uint16_t WorstC3Latency;
	uint16_t FlushSize;
	uint16_t FlushStride;
	uint8_t  DutyOffset;
	uint8_t  DutyWidth;
	uint8_t  DayAlarm;
	uint8_t  MonthAlarm;
	uint8_t  Century;
	uint16_t BootArchitectureFlags;
	uint8_t  Reserved2;
	uint32_t Flags;
	struct GenericAddressStructure ResetReg;
	uint8_t  ResetValue;
	uint8_t  Reserved3[3];
	uint64_t X_FirmwareControl;
	uint64_t X_Dsdt;
	struct GenericAddressStructure X_PM1aEventBlock;
	struct GenericAddressStructure X_PM1bEventBlock;
	struct GenericAddressStructure X_PM1aControlBlock;
	struct GenericAddressStructure X_PM1bControlBlock;
	struct GenericAddressStructure X_PM2ControlBlock;
	struct GenericAddressStructure X_PMTimerBlock;
	struct GenericAddressStructure X_GPE0Block;
	struct GenericAddressStructure X_GPE1Block;
} __attribute__((packed));

class AcpiTable {
	static const unsigned chunk = 1024;
	void checksum(void);
	void extend(const unsigned len);
public:
	struct acpi_sdt header;
	char *payload;
	unsigned allocated, used;

	AcpiTable(const char *name, const unsigned rev, const bool copy = 0) nonnull;
	void append(const char *data, const unsigned len) nonnull;
	unsigned reserve(const unsigned len);
	void increment64(const unsigned offset) const;
};

class ACPI {
	SMBIOS smbios;
	struct acpi_rsdp *rptr;
	bool bios_shadowed;
	char *allocated;
	unsigned nallocated, used;

	void shadow_bios(void);
	acpi_rsdp *find_rsdp(const char *start, const unsigned len);
	acpi_sdt *find_child(const char *sig, const acpi_sdt *parent, const unsigned ptrsize) nonnull;
	uint32_t slack(const acpi_sdt *parent);
	void get_cores(void);
public:
	struct acpi_sdt *rsdt, *xsdt;
	uint8_t apics[256];
	uint8_t napics;

	void allocate(unsigned len);
	static void dump(const acpi_sdt *table, const unsigned limit = 0);
	static void assert_checksum(const acpi_sdt *table, const unsigned len);
	static checked uint8_t checksum(const char *addr, const unsigned len);
	void check(void);
	checked bool replace_child(const char *sig, const acpi_sdt *replacement, acpi_sdt *const parent, const unsigned ptrsize);
	void add_child(const acpi_sdt *replacement, acpi_sdt *const parent, unsigned ptrsize);
	checked acpi_sdt *find_root(const char *sig);
	checked bool replace_root(const char *sig, const acpi_sdt *replacement);
	checked acpi_sdt *find_sdt(const char *sig);
	checked bool append(const char *sig, const char *extra, const uint32_t extra_len);
	void handover(void);
	ACPI(void);
	void add(const AcpiTable &table);
	void replace(const AcpiTable &table);
};

extern ACPI *acpi;
