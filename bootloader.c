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
#include <stdio.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/io.h>

extern "C" {
	#include <com32.h>
}

#include "version.h"
#include "bootloader.h"
#include "library/base.h"
#include "library/access.h"
#include "library/utils.h"
#include "platform/acpi.h"
#include "platform/options.h"
#include "platform/os.h"
#include "platform/e820.h"
#include "platform/config.h"
#include "platform/acpi.h"
#include "platform/trampoline.h"
#include "platform/devices.h"
#include "opteron/msrs.h"
#include "numachip2/numachip.h"

OS *os;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;
ACPI *acpi;
char *asm_relocated;

static uint64_t dram_top;
static unsigned nnodes;

static void check(void)
{
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->check();
}

static void scan(void)
{
	printf("Map scan:\n");
	dram_top = 0;

	// setup local DRAM windows
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		(*node)->dram_base = dram_top;

		for (Opteron *const *nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			(*nb)->dram_base = dram_top;
			dram_top += (*nb)->dram_size;
		}

		dram_top = roundup(dram_top, 1ULL << Numachip2::SIU_ATT_SHIFT);
		(*node)->dram_end = dram_top - 1;

		if (options->debug.maps)
			printf("SCI%03x dram_base=0x%"PRIx64" dram_size=0x%"PRIx64" dram_end=%"PRIx64"\n",
				(*node)->sci, (*node)->dram_base, (*node)->dram_size, (*node)->dram_end);
	}
}

static void add(const Node &node)
{
	unsigned range;

	uint64_t vga_base = Opteron::MMIO_VGA_BASE & ~((1 << Numachip2::MMIO32_ATT_SHIFT) - 1);
	uint64_t vga_limit = Opteron::MMIO_VGA_LIMIT | ((1 << Numachip2::MMIO32_ATT_SHIFT) - 1);

	// 7. setup MMIO32 ATT to master
	node.numachip->mmioatt.range(vga_base, vga_limit, local_node->sci);
	node.numachip->mmioatt.range(lib::rdmsr(MSR_TOPMEM), 0xffffffff, local_node->sci);

	// 8. forward VGA and MMIO32 regions to master
	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		range = 0;
		(*nb)->mmiomap->add(range++, Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, node.numachip->ht, 0);
		(*nb)->mmiomap->add(range++, (uint32_t)lib::rdmsr(MSR_TOPMEM), 0xffffffff, node.numachip->ht, 0);

		while (range < 8)
			(*nb)->mmiomap->remove(range++);

		(*nb)->write32(Opteron::VGA_ENABLE, 0);
		if ((*nb)->read32(Opteron::VGA_ENABLE))
			warning_once("Legacy VGA access is locked to local server; some video card BIOSs may cause any X servers to fail to complete initialisation");
	}

	uint32_t memhole = local_node->opterons[0]->read32(Opteron::DRAM_HOLE);

	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		// 9. setup DRAM hole
		(*nb)->write32(Opteron::DRAM_HOLE, memhole & 0xff000002);

		range = 0;

		// 10. add below-node DRAM ranges
		(*nb)->drammap.add(range++, local_node->opterons[0]->dram_base, node.dram_base - 1, node.numachip->ht);

		// 11. clear remaining entries
		while (range < 8)
			(*nb)->drammap.remove(range++);
	}

	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		assertf(!((*nb)->read32(Opteron::MCTL_SEL_LOW) & 1), "Unganged DRAM channels are unsupported");

		// 12. reprogram local range
		(*nb)->write32(Opteron::DRAM_BASE, (*nb)->dram_base >> 27);
		(*nb)->write32(Opteron::DRAM_LIMIT, ((*nb)->dram_base + (*nb)->dram_size - 1) >> 27);
	}

	range = 0;

	// 13. program local DRAM ranges
	while (range < node.nopterons) {
		uint64_t base = node.opterons[range]->dram_base;
		uint64_t limit = base + node.opterons[range]->dram_size - 1;

		node.numachip->drammap.add(range, base, limit, node.opterons[range]->ht);

		for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++)
			(*nb)->drammap.add(range + 1, base, limit, range);

		range++;
	}

	// 14. redirect above last local DRAM address to NumaChip
	if (&node < nodes[nnodes - 1])
		for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++)
			(*nb)->drammap.add(range, node.dram_end + 1, dram_top - 1, node.numachip->ht);
	range++;

	// 15. point IO and config maps to Numachip
	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		(*nb)->write32(Opteron::IO_MAP_LIMIT, 0xfff000 | node.numachip->ht);
		(*nb)->write32(Opteron::IO_MAP_BASE, 0x3);
		for (unsigned r = 1; r < 4; r++) {
			(*nb)->write32(Opteron::IO_MAP_LIMIT + r * 8, 0);
			(*nb)->write32(Opteron::IO_MAP_BASE + r * 8, 0);
		}

		(*nb)->write32(Opteron::CONF_MAP, 0xff000003 | (node.numachip->ht << 4));
		for (unsigned r = 1; r < 4; r++)
			(*nb)->write32(Opteron::CONF_MAP + r * 4, 0);
	}

	// 16. set DRAM range on NumaChip
	node.numachip->write32(Numachip2::DRAM_SHARED_BASE, node.dram_base >> 24);
	node.numachip->write32(Numachip2::DRAM_SHARED_LIMIT, (node.dram_end - 1) >> 24);
}

static void setup_gsm_early(void)
{
	for (unsigned n = 0; n < config->nnodes; n++) {
		if (config->nodes[n].partition)
			continue;

		// setup read-only map on observer
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++) {
			uint64_t base = 1ULL << Numachip2::GSM_SHIFT;
			(*nb)->mmiomap->add(9, base, base + (1ULL << Numachip2::GSM_SIZE_SHIFT) - 1, local_node->numachip->ht, 0, 1);
		}
	}
}

static void setup_gsm(void)
{
	printf("Setting up GSM to");
	for (unsigned n = 0; n < config->nnodes; n++)
		if (!config->nodes[n].partition)
			printf(" %03x", config->nodes[n].sci);

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		uint64_t base = (1ULL << Numachip2::GSM_SHIFT) + (*node)->dram_base;
		uint64_t limit = (1ULL << Numachip2::GSM_SHIFT) + (*node)->dram_end;
		sci_t dest = (*node)->sci;

		(*node)->numachip->write32(Numachip2::GSM_MASK, (1ULL << (Numachip2::GSM_SHIFT - 36)) - 1);

		// setup ATT on observers for GSM
		for (unsigned n = 0; n < config->nnodes; n++) {
			if (config->nodes[n].partition)
				continue;

			// FIXME: use observer instance
			xassert(limit > base);

			const uint64_t mask = (1ULL << Numachip2::SIU_ATT_SHIFT) - 1;
			xassert((base & mask) == 0);
			xassert((limit & mask) == mask);

			lib::mcfg_write32(config->nodes[n].sci, 0, 24 + (*node)->numachip->ht, Numachip2::SIU_ATT_INDEX >> 12,
			  Numachip2::SIU_ATT_INDEX & 0xfff, (1 << 31) | (base >> Numachip2::SIU_ATT_SHIFT));

			for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << Numachip2::SIU_ATT_SHIFT)
				lib::mcfg_write32(config->nodes[n].sci, 0, 24 + (*node)->numachip->ht,
				  Numachip2::SIU_ATT_ENTRY >> 12, Numachip2::SIU_ATT_ENTRY & 0xfff, dest);
		}
	}
	printf("\n");
}

static void setup_info(void)
{
	uint32_t info[Numachip2::INFO_SIZE];
	struct info *infop = (struct info *)&info;

	memset(info, 0, sizeof(info));
	infop->partition = config->local_node->partition;
	infop->fabric_nodes = config->nnodes;

	// find first node of partition
	for (unsigned n = 0; n < config->nnodes; n++) {
		if (config->nodes[n].partition == config->local_node->partition) {
			infop->part_start = config->nodes[n].sci;
			break;
		}
	}

	infop->part_nodes = nnodes;
	infop->ver = 0;
	infop->neigh_ht = local_node->neigh_ht;
	infop->neigh_link = local_node->neigh_link;
	infop->neigh_sublink = 0;
	infop->symmetric = 1;
	infop->devices = config->local_node->devices;

	for (unsigned n = 0; n < nnodes; n++)
		for (unsigned i = 0; i < sizeof(info)/sizeof(info[0]); i++)
			nodes[n]->numachip->write32(Numachip2::INFO + i*4, info[i]);
}

static void remap(void)
{
	// 1. setup local NumaChip DRAM ranges
	unsigned range = 0;
	for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		local_node->numachip->drammap.add(range++, (*nb)->dram_base, (*nb)->dram_base + (*nb)->dram_size - 1, (*nb)->ht);

	// 2. route higher access to NumaChip
	if (nnodes > 1)
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			(*nb)->drammap.add(range, nodes[1]->opterons[0]->dram_base, dram_top - 1, local_node->numachip->ht);

	// 3. setup NumaChip DRAM registers
	local_node->numachip->write32(Numachip2::DRAM_SHARED_BASE, local_node->dram_base >> 24);
	local_node->numachip->write32(Numachip2::DRAM_SHARED_LIMIT, (local_node->dram_end - 1) >> 24);

	// 4. setup DRAM ATT routing
	for (Node *const *node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Node *const *dnode = &nodes[0]; dnode < &nodes[nnodes]; dnode++)
			(*node)->numachip->dramatt.range((*dnode)->dram_base, (*dnode)->dram_end, (*dnode)->sci);

	// 5. set top of memory
	lib::wrmsr(MSR_TOPMEM2, dram_top);
	push_msr(MSR_TOPMEM2, dram_top);

	// 6. add NumaChip MMIO32 ranges
	local_node->numachip->mmiomap.add(0, Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, local_node->opterons[0]->ioh_ht);
	local_node->numachip->mmiomap.add(1, lib::rdmsr(MSR_TOPMEM), 0xffffffff, local_node->opterons[0]->ioh_ht);

	for (Node *const *node = &nodes[1]; node < &nodes[nnodes]; node++)
		add(**node);

	// update e820 map
	for (Node *const *node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (Opteron *const *nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			// master always first in array
			if (!(node == &nodes[0]))
				e820->add((*nb)->dram_base, (*nb)->dram_size, E820::RAM);

			if (options->tracing) {
				(*nb)->trace_base = (*nb)->dram_base + (*nb)->dram_size - options->tracing;
				(*nb)->trace_limit = (*nb)->trace_base + options->tracing - 1;

				// disable DRAM stutter scrub
				uint32_t val = (*nb)->read32(Opteron::CLK_CTRL_0);
				(*nb)->write32(Opteron::CLK_CTRL_0, val & ~(1 << 15));
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);
			}
		}
	}

	// setup IOH limits
#ifdef FIXME /* hangs on remote node */
	for (Node *const *node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->iohub->limits(dram_top - 1);
#else
	local_node->iohub->limits(dram_top - 1);
#endif
}

#define MTRR_TYPE(x) (x) == 0 ? "uncacheable" : (x) == 1 ? "write-combining" : (x) == 4 ? "write-through" : (x) == 5 ? "write-protect" : (x) == 6 ? "write-back" : "unknown"

static void copy_inherit(void)
{
	for (Node **node = &nodes[1]; node < &nodes[nnodes]; node++) {
		const uint64_t rnode = (*node)->dram_base + (*node)->numachip->read32(Numachip2::INFO + 4);
		lib::memcpy64((uint64_t)&(*node)->neigh_ht, rnode + xoffsetof(local_node->neigh_ht, local_node), sizeof((*node)->neigh_ht));

		// FIXME
		(*node)->neigh_ht = 2;
		printf("%03x Numachip @ HT%u.%u\n", (*node)->sci, (*node)->neigh_ht,
		       (*node)->neigh_link);
	}
}

static bool boot_core(const uint32_t apic, const uint32_t vector, const uint32_t status)
{
	*REL32(status) = vector;
	local_node->numachip->write32(Numachip2::PIU_APIC, (apic << 12) | (5 << 8)); // init
	local_node->numachip->write32(Numachip2::PIU_APIC, (apic << 12) |
	  (6 << 8) | ((uint32_t)REL32(vector) >> 12)); // startup

	printf(" 0x%05x", apic);

	for (unsigned i = 0; i < 1000000; i++) {
		if (*REL32(status) == status)
			return 0;
		cpu_relax();
	}

	return 1;
}

static void setup_cores_observer(void)
{
	printf("APICs:");

	lib::critical_enter();
	for (unsigned n = 1; n < acpi->napics; n++) {
		uint32_t apic = ((uint32_t)local_node->sci << 8) | acpi->apics[n];

		if (boot_core(apic, VECTOR_SETUP_OBSERVER, VECTOR_SETUP_DONE))
			fatal("APIC 0x%05x has status 0x%x", apic, *REL32(status));
	}

	lib::critical_leave();
	printf("\n");
}

static void setup_cores(void)
{
#ifdef UNNEEDED
	// read fixed MSRs
	uint64_t *mtrr_fixed = REL64(mtrr_fixed);
	uint32_t *fixed_mtrr_regs = REL32(fixed_mtrr_regs);

	printf("Fixed MTRRs:\n");
	for (unsigned i = 0; fixed_mtrr_regs[i] != 0xffffffff; i++) {
		mtrr_fixed[i] = lib::rdmsr(fixed_mtrr_regs[i]);
		printf("- 0x%016"PRIx64"\n", mtrr_fixed[i]);
	}

	// read variable MSRs
	uint64_t *mtrr_var_base = REL64(mtrr_var_base);
	uint64_t *mtrr_var_mask = REL64(mtrr_var_mask);
	printf("Variable MTRRs:\n");

	for (int i = 0; i < 8; i++) {
		mtrr_var_base[i] = lib::rdmsr(MSR_MTRR_PHYS_BASE0 + i * 2);
		mtrr_var_mask[i] = lib::rdmsr(MSR_MTRR_PHYS_MASK0 + i * 2);

		if (mtrr_var_mask[i] & 0x800ULL)
			printf("- 0x%011"PRIx64":0x%011"PRIx64" %s\n", mtrr_var_base[i] & ~0xfffULL,
			  mtrr_var_mask[i] & ~0xfffULL, MTRR_TYPE(mtrr_var_base[i] & 0xffULL));
	}
#endif

	lib::critical_enter();

	// boot cores
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		printf("APICs on %03x:", (*node)->sci);

		// set correct MCFG base per node
		const uint64_t mcfg = Numachip2::MCFG_BASE | ((uint64_t)(*node)->sci << 28) | 0x21;
		push_msr(MSR_MCFG, mcfg);

		for (unsigned n = 0; n < acpi->napics; n++) {
			(*node)->apics[n] = ((uint32_t)(*node)->sci << 8) | acpi->apics[n];
			// renumber BSP APICID
			if (node == &nodes[0] && n == 0) {
				volatile uint32_t *apic = (uint32_t *)(lib::rdmsr(MSR_APIC_BAR) & ~0xfff);
				apic[0x20/4] = (apic[0x20/4] & 0xffffff) | ((*node)->apics[n] << 24);
				continue;
			}

			*REL8(apic_local) = (*node)->apics[n] & 0xff;
			if (boot_core((*node)->apics[n], VECTOR_SETUP, VECTOR_SETUP_DONE))
				fatal("APIC 0x%02x->0x%05x has status 0x%x", acpi->apics[n], (*node)->apics[n], *REL32(status));
			printf("->0x%05x", (*node)->apics[n]);
		}
		(*node)->napics = acpi->napics;
		printf("\n");
	}

	lib::critical_leave();
}

static void tracing_arm(void)
{
	if (options->tracing != 2)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_arm();
}

static void tracing_start(void)
{
	if (options->tracing != 2)
		return;

	printf("Tracing started on:");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			printf(" %03x#%u", (*nb)->sci, (*nb)->ht);
	printf("\n");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_start();
}

static void tracing_stop(void)
{
	if (options->tracing != 2)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_stop();
	printf("Tracing stopped on:");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			printf(" %03x#%u", (*nb)->sci, (*nb)->ht);
	printf("\n");
}

static void test_cores(void)
{
	lib::critical_enter();
	tracing_start();

	printf("Testing APICs:");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (unsigned n = 0; n < (*node)->napics; n++) {
			if (node == &nodes[0] && n == 0)
				continue; // skip BSC

			if (boot_core((*node)->apics[n], VECTOR_TEST_START, VECTOR_TEST_STARTED)) {
				if (options->tracing)
					tracing_stop();
				fatal("APIC 0x%05x has status 0x%x", (*node)->apics[n], *REL32(status));
			}

			lib::udelay(100000);

			if (boot_core((*node)->apics[n], VECTOR_TEST_STOP, VECTOR_TEST_STOPPED)) {
				tracing_stop();
				fatal("APIC 0x%05x has status 0x%x", (*node)->apics[n], *REL32(status));
			}
		}
	}
	printf("\n");

	printf("Starting APICs:");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (unsigned n = 0; n < (*node)->napics; n++) {
			// skip BSC
			if (node == &nodes[0] && n == 0)
				continue;

			if (boot_core((*node)->apics[n], VECTOR_TEST_START, VECTOR_TEST_STARTED)) {
				tracing_stop();
				fatal("APIC 0x%05x has status 0x%x", (*node)->apics[n], *REL32(status));
			}
		}
	}
	printf("\n");

	lib::udelay(5000000);
	printf("Stopping APICs:");

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (unsigned n = 0; n < (*node)->napics; n++) {
			// skip BSC
			if (node == &nodes[0] && n == 0)
				continue;

			if (boot_core((*node)->apics[n], VECTOR_TEST_STOP, VECTOR_TEST_STOPPED)) {
				tracing_stop();
				fatal("APIC 0x%05x has status 0x%x", (*node)->apics[n], *REL32(status));
			}
		}
	}
	printf("\n");

	tracing_stop();
	tracing_arm();
	lib::critical_leave();
}

static void acpi_tables(void)
{
	AcpiTable mcfg("MCFG", 1);

	// MCFG 'reserved field'
	const uint64_t reserved = 0;
	mcfg.append((const char *)&reserved, sizeof(reserved));

	uint16_t segment = 0;
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		struct acpi_mcfg ent;
		ent.address = Numachip2::MCFG_BASE | ((uint64_t)(*node)->sci << 28ULL);
		ent.pci_segment = segment++;
		ent.start_bus_number = 0;
		ent.end_bus_number = 255;
		ent.reserved = 0;
		mcfg.append((const char *)&ent, sizeof(ent));
	}

	// cores
	const acpi_sdt *oapic = acpi->find_sdt("APIC");
	AcpiTable apic("APIC", 3);
	apic.append((const char *)&oapic->data[0], 4); // Local Interrupt Controller Address
	apic.append((const char *)&oapic->data[4], 4); // Flags

	// append new x2APIC entries
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned n = 0;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			for (unsigned m = 0; m < (*nb)->cores; m++) {
				struct acpi_x2apic_apic ent;
				ent.type = 9;
				ent.length = 16;
				ent.reserved = 0;
				ent.x2apic_id = (*node)->apics[n + m];
				ent.flags = 1;
				ent.acpi_uid = 0;
				apic.append((const char *)&ent, sizeof(ent));
			}

			n += (*nb)->cores;
		}
	}

	// copying all entries except x2APIC (9) and APIC (0)
	unsigned pos = 8;
	while (pos < (oapic->len - sizeof(struct acpi_sdt))) {
		uint8_t type = oapic->data[pos];
		uint8_t len = oapic->data[pos + 1];
		xassert(len > 0 && len < 64);

		if (type != 0 && type != 9)
			apic.append((const char *)&oapic->data[pos], len);
		pos += len;
	}

	// core to node mapping
	AcpiTable srat("SRAT", 3);

	uint32_t reserved1 = 1;
	srat.append((const char *)&reserved1, sizeof(reserved1));
	uint64_t reserved2 = 0;
	srat.append((const char *)&reserved2, sizeof(reserved2));

	uint32_t domain = 0;
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned n = 0;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			struct acpi_mem_affinity ment;
			ment.type = 1;
			ment.length = 40;
			ment.proximity = domain;
			ment.reserved1 = 0;
			ment.base = (*nb)->dram_base;
			ment.lengthlo = (uint32_t)(*nb)->dram_size;
			ment.lengthhi = (uint32_t)((*nb)->dram_size >> 32);
			ment.reserved2 = 0;
			ment.flags = 1;
			ment.reserved3[0] = 0;
			ment.reserved3[1] = 0;
			srat.append((const char *)&ment, sizeof(ment));

			for (unsigned m = 0; m < (*nb)->cores; m++) {
				struct acpi_x2apic_affinity ent;
				ent.type = 2;
				ent.length = 24;
				ent.reserved1 = 0;
				ent.proximity = domain;
				ent.x2apicid = (*node)->apics[n + m];
				ent.flags = 1;
				ent.clock = (uint32_t)(node - nodes);
				ent.reserved2 = 0;
				srat.append((const char *)&ent, sizeof(ent));
			}

			n += (*nb)->cores;
			domain++;
		}
	}

	const acpi_sdt *oslit = acpi->find_sdt("SLIT");
	uint8_t *odist = NULL;
	if (oslit) {
		odist = (uint8_t *)&(oslit->data[8]);
		xassert(odist[0] <= 13);
	}

	AcpiTable slit("SLIT", 1);

	// number of localities
	uint64_t *n = (uint64_t *)slit.reserve(8);
	*n = 0;

	printf("Topology distances:\n   ");
	for (Node **snode = &nodes[0]; snode < &nodes[nnodes]; snode++)
		for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++)
			printf(" %3u", (snode - nodes) * (*snode)->nopterons + (snb - (*snode)->opterons));
	printf("\n");

	for (Node **snode = &nodes[0]; snode < &nodes[nnodes]; snode++) {
		for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++) {
			printf("%3u", 000); // FIXME

			for (Node **dnode = &nodes[0]; dnode < &nodes[nnodes]; dnode++) {
				for (Opteron **dnb = &(*dnode)->opterons[0]; dnb < &(*dnode)->opterons[(*dnode)->nopterons]; dnb++) {
					uint8_t dist;
					if (*snode == *dnode) {
						if (*snb == *dnb)
							dist = 10;
						else
							dist = oslit ? odist[(snb - (*snode)->opterons) + (dnb - (*dnode)->opterons) * (*snode)->nopterons] : 16;
					} else
						dist = 160;

					printf(" %3u", dist);
					slit.append((const char *)&dist, sizeof(dist));
				}
			}
			printf("\n");
			(*n)++;
		}
	}

	acpi->allocate(1024 + sizeof(struct acpi_sdt) * 4 + mcfg.used + apic.used + srat.used + slit.used);
	acpi->replace(mcfg);
	acpi->replace(apic);
	acpi->replace(srat);
	acpi->replace(slit);
	acpi->check();
}

static void finalise(void)
{
	printf("Clearing DRAM");
	lib::critical_enter();

	// start clearing DRAM
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_start();
	}

	// wait for clear to complete
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_wait();
	}

	lib::critical_leave();
	printf("\n");

	if (!options->tracing) {
		printf("Enabling scrubbers");

		// enable DRAM scrubbers
		for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
			for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
				(*nb)->dram_scrub_enable();

		printf("\n");
	} else {
		// Arm the tracer
		for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
			(*node)->tracing_arm();
	}

	e820->test();
	setup_cores();
	acpi_tables();
	test_cores();
	check();
}

static void finished(void)
{
	asm volatile("mfence; wbinvd" ::: "memory");
	if (options->boot_wait)
		lib::wait_key("Press enter to boot");

	printf("Unification succeeded; executing syslinux label %s\n", options->next_label);
	os->exec(options->next_label);
}

int main(const int argc, char *argv[])
{
	os = new OS(); // needed first for console access

	printf(CLEAR BANNER "NumaConnect2 system unification " VER " at 20%02d-%02d-%02d %02d:%02d:%02d" COL_DEFAULT "\n",
	  lib::rtc_read(RTC_YEAR), lib::rtc_read(RTC_MONTH), lib::rtc_read(RTC_DAY),
	  lib::rtc_read(RTC_HOURS), lib::rtc_read(RTC_MINUTES), lib::rtc_read(RTC_SECONDS));

	printf("Host MAC %02x:%02x:%02x:%02x:%02x:%02x, IP %s, hostname %s\n",
		os->mac[0], os->mac[1], os->mac[2],
		os->mac[3], os->mac[4], os->mac[5],
		inet_ntoa(os->ip), os->hostname ? os->hostname : "<none>");

	options = new Options(argc, argv); // needed before first PCI access
	e820 = new E820();
	Opteron::prepare();
	acpi = new ACPI();

	// SMI often assumes HT nodes are Northbridges, so handover early
	if (options->handover_acpi)
		acpi->handover();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	local_node = new Node((sci_t)config->local_node->sci, (sci_t)config->master->sci);

	uint16_t reason = lib::pmio_read16(0x44);
	if (reason & ~((1 << 2) | (1 << 6))) /* Mask out CF9 and keyboard reset */
		warning("Last reboot reason (PM44h) was 0x%x", reason);

	if (lib::mcfg_read32(0x000, 0, 0x20, 0, 0x4c) & (1 << 30))
		warning("Last reboot reason by BootFail timer");

	// adjust down MMIO range to prevent overlap
	for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++) {
		uint64_t base, limit;
		ht_t dest;
		link_t link;
		bool lock;
		unsigned range;

		for (range = 0; range < (*nb)->mmiomap->ranges; range++) {
			if ((*nb)->mmiomap->read(range, &base, &limit, &dest, &link, &lock)) {
				if ((base <= Numachip2::LOC_BASE) && (limit >= Numachip2::LOC_LIM)) {
					if (options->debug.maps)
						printf("Found matching local MMIO range %u on SCI%03x#%d: 0x%08"PRIx64":0x%08"PRIx64" to %d.%d%s\n",
						       range, local_node->sci, (*nb)->ht, base, limit, dest, link, lock ? "locked" : "");
					(*nb)->mmiomap->remove(range);
					(*nb)->mmiomap->add(range, Numachip2::LOC_LIM+1, limit, dest, link);
					break;
				}
			}
		}

		// Find highest available MMIO map entry
		for (range = 7; range > 0; range--)
			if (!(*nb)->mmiomap->read(range, &base, &limit, &dest, &link, &lock))
				break;

		xassert(range > 0);
		// Numachip local registers
		(*nb)->mmiomap->add(range, Numachip2::LOC_BASE, Numachip2::LOC_LIM, local_node->numachip->ht, 0);
	}

	if (options->init_only) {
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++) {
			if (options->tracing) {
				(*nb)->tracing_arm();
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);
			}
			(*nb)->check();
		}
		finished();
	}

	// initialize SPI/SPD, DRAM, NODEID etc
	local_node->numachip->late_init();

	// add global MCFG maps
	for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		(*nb)->mmiomap->add(8, Numachip2::MCFG_BASE, Numachip2::MCFG_LIM, local_node->numachip->ht, 0);

	// reserve HT decode and MCFG address range so Linux accepts it
	e820->add(Opteron::HT_BASE, Opteron::HT_LIMIT - Opteron::HT_BASE, E820::RESERVED);
	e820->add(Numachip2::MCFG_BASE, Numachip2::MCFG_LIM - Numachip2::MCFG_BASE + 1, E820::RESERVED);

	// setup local MCFG access
	const uint64_t mcfg = Numachip2::MCFG_BASE | ((uint64_t)config->local_node->sci << 28) | 0x21;
	lib::wrmsr(MSR_MCFG, mcfg);
	push_msr(MSR_MCFG, mcfg);

	if (options->tracing)
		setup_gsm_early();

	if (!options->singleton)
		local_node->numachip->fabric_train();

	if (!config->local_node->partition) {
		setup_cores_observer();
		setup_info();
		finished();
	}

	for (unsigned n = 0; n < config->nnodes; n++)
		if (config->nodes[n].partition == config->local_node->partition)
			nnodes++;

	// slaves
	if (!config->local_node->master) {
		// read from master after mapped
		local_node->numachip->write32(Numachip2::INFO + 4, (uint32_t)local_node);

		printf("Waiting for SCI%03x/%s", config->master->sci, config->master->hostname);
		local_node->numachip->write32(Numachip2::INFO, 1 << 29);

		// wait for 'ready'
		while (local_node->numachip->read32(Numachip2::INFO) != 3 << 29) {
			local_node->check();
			cpu_relax();
		}

		printf("\n");
		os->cleanup();
		if (!options->handover_acpi) // handover performed earlier
			acpi->handover();
		handover_legacy(local_node->sci);

		local_node->iohub->smi_disable();
		pci_disable_all(local_node->sci);

		// clear BSP flag
		uint64_t val = lib::rdmsr(MSR_APIC_BAR);
		lib::wrmsr(MSR_APIC_BAR, val & ~(1ULL << 8));

		// disable XT-PIC
		inb(PIC_MASTER_IMR);
		outb(0xff, PIC_MASTER_IMR);
		inb(PIC_SLAVE_IMR);
		outb(0xff, PIC_SLAVE_IMR);

		printf(BANNER "\nThis server SCI%03x/%s is part of a %u-server NumaConnect2 system\n"
		  "Refer to the console on SCI%03x/%s ", config->local_node->sci, config->local_node->hostname,
		  nnodes, config->master->sci, config->master->hostname);

		disable_cache();

		// set 'go-ahead'
		local_node->numachip->write32(Numachip2::INFO, 7 << 29);

		// reenable wrap32
		val = lib::rdmsr(MSR_HWCR);
		lib::wrmsr(MSR_HWCR, val & ~(1ULL << 17));

		while (1) {
			cli();
			asm volatile("hlt" ::: "memory");
		}
	}

	config->local_node->added = 1;

	nodes = (Node **)zalloc(sizeof(void *) * nnodes);
	xassert(nodes);
	nodes[0] = local_node;
	local_node->check();
	printf("Servers ready:\n");

	unsigned pos = 1;
	do {
		for (unsigned n = 0; n < config->nnodes; n++) {
			// skip initialised nodes or nodes in other partitions
			if (config->nodes[n].added || config->nodes[n].partition != config->local_node->partition)
				continue;

			ht_t ht = Numachip2::probe(config->nodes[n].sci);
			if (ht) {
				nodes[pos++] = new Node(config->nodes[n].sci, ht);
				config->nodes[n].added = 1;
			}

			cpu_relax();
		}
	} while (pos < nnodes);

	scan();
	remap();
	copy_inherit();
	if (options->tracing)
		setup_gsm();
	setup_info();
	finalise();
	finished();
}
