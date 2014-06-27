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
#include "platform/syslinux.h"
#include "platform/e820.h"
#include "platform/config.h"
#include "platform/acpi.h"
#include "platform/trampoline.h"
#include "platform/devices.h"
#include "opteron/msrs.h"
#include "numachip2/numachip.h"

Syslinux *syslinux;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;
ACPI *acpi;
char *asm_relocated;

static uint64_t dram_top;
static unsigned nnodes;

static void scan(void)
{
	printf("Map scan:\n");
	dram_top = 0;

	// setup local DRAM windows
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		(*node)->dram_base = dram_top;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			(*nb)->dram_base = dram_top;
			dram_top += (*nb)->dram_size;
		}

		dram_top = roundup(dram_top, 1ULL << Numachip2::SIU_ATT_SHIFT);
		(*node)->dram_end = dram_top - 1;

		if (options->debug.maps)
			printf("SCI%03x dram_base=0x%llx dram_size=0x%llx dram_end=%llx\n",
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
	node.numachip->mmioatt.range(*REL64(msr_topmem), 0xffffffff, local_node->sci);

	// 8. forward VGA and MMIO32 regions to master
	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		range = 0;
		(*nb)->mmiomap.add(range++, Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, node.numachip->ht, 0);
		(*nb)->mmiomap.add(range++, (uint32_t)*REL64(msr_topmem), 0xffffffff, node.numachip->ht, 0);

		while (range < 8)
			(*nb)->mmiomap.remove(range++);

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

		for (unsigned i = 0; i < local_node->nopterons; i++) {
			uint64_t base = 1ULL << Numachip2::GSM_SHIFT;
			local_node->opterons[i]->mmiomap.add(9, base, base + (1ULL << Numachip2::GSM_SIZE_SHIFT) - 1, local_node->numachip->ht, 0);
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
			assert(limit > base);

			const uint64_t mask = (1ULL << Numachip2::SIU_ATT_SHIFT) - 1;
			assert((base & mask) == 0);
			assert((limit & mask) == mask);

			lib::mcfg_write32(config->nodes[n].sci, 0, 24 + (*node)->numachip->ht, Numachip2::SIU_ATT_INDEX >> 12,
			  Numachip2::SIU_ATT_INDEX & 0xfff, (1 << 31) | (base >> Numachip2::SIU_ATT_SHIFT));

			for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << Numachip2::SIU_ATT_SHIFT)
				lib::mcfg_write32(config->nodes[n].sci, 0, 24 + (*node)->numachip->ht,
				  Numachip2::SIU_ATT_ENTRY >> 12, Numachip2::SIU_ATT_ENTRY & 0xfff, dest);
		}
	}
	printf("\n");
}

static void remap(void)
{
	// 1. setup local NumaChip DRAM ranges
	unsigned range = 0;
	for (Opteron **nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		local_node->numachip->drammap.add(range++, (*nb)->dram_base, (*nb)->dram_base + (*nb)->dram_size - 1, (*nb)->ht);

	// 2. route higher access to NumaChip
	for (Opteron **nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		(*nb)->drammap.add(range, nodes[1]->opterons[0]->dram_base, dram_top - 1, local_node->numachip->ht);

	// 3. setup NumaChip DRAM registers
	local_node->numachip->write32(Numachip2::DRAM_SHARED_BASE, local_node->dram_base >> 24);
	local_node->numachip->write32(Numachip2::DRAM_SHARED_LIMIT, (local_node->dram_end - 1) >> 24);

	// 4. setup DRAM ATT routing
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Node **dnode = &nodes[0]; dnode < &nodes[nnodes]; dnode++)
			(*node)->numachip->dramatt.range((*dnode)->dram_base, (*dnode)->dram_end, (*dnode)->sci);

	// 5. set top of memory
	*REL64(msr_topmem) = lib::rdmsr(MSR_TOPMEM);
	assert(*REL64(msr_topmem) < 0xffffffff);
	*REL64(msr_topmem2) = dram_top;
	lib::wrmsr(MSR_TOPMEM2, *REL64(msr_topmem2));

	// 6. add NumaChip MMIO32 ranges
	local_node->numachip->mmiomap.add(0, Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, local_node->opterons[0]->ioh_ht);
	local_node->numachip->mmiomap.add(1, *REL64(msr_topmem), 0xffffffff, local_node->opterons[0]->ioh_ht);

	for (Node *const *node = &nodes[1]; node < &nodes[nnodes]; node++)
		add(**node);

	// update e820 map
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			// master always first in array
			if (!(node == &nodes[0]))
				e820->add((*nb)->dram_base, (*nb)->dram_size, E820::RAM);

			if (options->tracing) {
				uint64_t base = (*nb)->dram_base + (*nb)->dram_size - options->tracing;
				uint64_t limit = base + options->tracing - 1;
				assert((base & 0xffffff) == 0);
				assert((limit & 0xffffff) == 0xffffff);

				// disable DRAM stutter scrub
				uint32_t val = (*nb)->read32(Opteron::CLK_CTRL_0);
				(*nb)->write32(Opteron::CLK_CTRL_0, val & ~(1 << 15));

				(*nb)->write32(0x20b8, ((base & ((1ULL << 40) - 1)) >> 24) | (((limit & ((1ULL << 40) - 1)) >> 24) << 16));
				(*nb)->write32(0x2120, (base >> 40) | ((limit >> 40) << 8));
				(*nb)->write32(0x20bc, base >> 6);
				e820->add(base, limit - base + 1, E820::RESERVED);
			} else {
				(*nb)->write32(0x20b8, 0);
				(*nb)->write32(0x2120, 0);
				(*nb)->write32(0x20bc, 0);
			}
			(*nb)->write32(0x20c0, 0);
		}
	}

	// setup IOH limits
#ifdef FIXME /* hangs on remote node */
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->iohub->limits(dram_top - 1);
#else
	local_node->iohub->limits(dram_top - 1);
#endif

	handover_legacy(local_node->sci);
}

static void acpi_tables(void)
{
	AcpiTable mcfg("MCFG", 1);

	// MCFG 'reserved field'
	const uint64_t reserved = 0;
	mcfg.append((const char *)&reserved, sizeof(reserved));

	uint16_t segment = 0;
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		struct acpi_mcfg ent = {
			.address = NC_MCFG_BASE | ((uint64_t)(*node)->sci << 28ULL),
			.pci_segment = segment++,
			.start_bus_number = 0,
			.end_bus_number = 255,
			.reserved = 0,
		};
		mcfg.append((const char *)&ent, sizeof(ent));
	}

	acpi->replace(mcfg);

	// cores
	const acpi_sdt *oapic = acpi->find_sdt("APIC");
	AcpiTable apic("APIC", 3);

	for (unsigned i = 0; i < acpi->napics; i++) {
		struct acpi_local_x2apic ent = {
			.type = 9, .len = 16, .reserved = 0, .x2apic_id = acpi->apics[i], .flags = 1, .proc_uid = 0};
		apic.append((const char *)&ent, sizeof(ent));
	}

	acpi->replace(apic);

	const acpi_sdt *oslit = acpi->find_sdt("SLIT");
	uint8_t *odist = NULL;
	if (oslit) {
		odist = (uint8_t *)&(oslit->data[8]);
		assert(odist[0] <= 13);
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

	acpi->replace(slit);

	acpi->check();
}

static void finalise(void)
{

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->status();

	printf("Clearing DRAM");

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

	printf("\n");

	if (!options->tracing) {
		printf("Enabling scrubbers");

		// enable DRAM scrubbers
		for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
			for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
				(*nb)->dram_scrub_enable();

		printf("\n");
	}

	e820->test();
	acpi_tables();

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->status();

#ifdef NOTNEEDED
	Opteron::restore();
#endif
}

static void finished(void)
{
	if (options->boot_wait)
		lib::wait_key("Press enter to boot");

	printf("Unification succeeded; executing syslinux label %s\n", options->next_label);
#ifdef NOTNEEDED
	Opteron::restore();
#endif
	syslinux->exec(options->next_label);
}

int main(const int argc, const char *argv[])
{
	syslinux = new Syslinux(); // needed first for console access

	printf(CLEAR BANNER "NumaConnect2 unification " VER " at 20%02d-%02d-%02d %02d:%02d:%02d" COL_DEFAULT "\n",
	  lib::rtc_read(RTC_YEAR), lib::rtc_read(RTC_MONTH), lib::rtc_read(RTC_DAY),
	  lib::rtc_read(RTC_HOURS), lib::rtc_read(RTC_MINUTES), lib::rtc_read(RTC_SECONDS));

	printf("Host MAC %02x:%02x:%02x:%02x:%02x:%02x, IP %s, hostname %s\n",
		syslinux->mac[0], syslinux->mac[1], syslinux->mac[2],
		syslinux->mac[3], syslinux->mac[4], syslinux->mac[5],
		inet_ntoa(syslinux->ip), syslinux->hostname ? syslinux->hostname : "<none>");

	options = new Options(argc, argv); // needed before first PCI access
	Opteron::prepare();
	acpi = new ACPI();

	// SMI often assumes HT nodes are Northbridges, so handover early
	if (options->handover_acpi)
		acpi->handover();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	e820 = new E820();
	local_node = new Node((sci_t)config->local_node->sci, (sci_t)config->master->sci);

	if (options->init_only) {
		printf("Initialization succeeded; executing syslinux label %s\n", options->next_label);
		syslinux->exec(options->next_label);
		return 0;
	}

	// add global MCFG maps
	for (unsigned i = 0; i < local_node->nopterons; i++)
		local_node->opterons[i]->mmiomap.add(8, NC_MCFG_BASE, NC_MCFG_LIM, local_node->numachip->ht, 0);

	// reserve HT decode and MCFG address range so Linux accepts it
	e820->add(Opteron::HT_BASE, Opteron::HT_LIMIT - Opteron::HT_BASE, E820::RESERVED);
	e820->add(NC_MCFG_BASE, NC_MCFG_LIM - NC_MCFG_BASE + 1, E820::RESERVED);

	// setup local MCFG access
	uint64_t val6 = NC_MCFG_BASE | ((uint64_t)config->local_node->sci << 28ULL) | 0x21ULL;
	lib::wrmsr(MSR_MCFG_BASE, val6);

	if (options->tracing)
		setup_gsm_early();

	local_node->numachip->fabric_train();

	if (!config->local_node->partition)
		finished();

	nnodes = 0;
	for (unsigned n = 0; n < config->nnodes; n++)
		if (config->nodes[n].partition == config->local_node->partition)
			nnodes++;

	// slaves
	if (!config->local_node->master) {
		printf("Waiting for SCI%03x/%s", config->master->sci, config->master->hostname);
		local_node->numachip->write32(Numachip2::FABRIC_CTRL, 1 << 29);

		// wait for 'ready'
		while (local_node->numachip->read32(Numachip2::FABRIC_CTRL) != 3 << 29)
			cpu_relax();

		syslinux->cleanup();
		acpi->handover();
		handover_legacy(local_node->sci);

		local_node->iohub->smi_disable();
		disable_dma_all(local_node->sci);

		// clear BSP flag
		uint64_t val = lib::rdmsr(MSR_APIC_BAR);
		lib::wrmsr(MSR_APIC_BAR, val & ~(1ULL << 8));

		// disable XT-PIC
		inb(PIC_MASTER_IMR);
		outb(0xff, PIC_MASTER_IMR);
		inb(PIC_SLAVE_IMR);
		outb(0xff, PIC_SLAVE_IMR);

		printf(BANNER "\nThis server SCI%03x/%s is part of a %d-server NumaConnect2 system\n"
		  "Refer to the console on SCI%03x/%s ", config->local_node->sci, config->local_node->hostname,
		  nnodes, config->master->sci, config->master->hostname);

		// set 'go-ahead'
		local_node->numachip->write32(Numachip2::FABRIC_CTRL, 7 << 29);

		disable_cache();
		asm volatile("mfence" ::: "memory");

		// reenable wrap32
		val = lib::rdmsr(MSR_HWCR);
		lib::wrmsr(MSR_HWCR, val & ~(1ULL << 17));

		while (1) {
#ifdef DEBUG
			for (unsigned i = 0; i < local_node->nopterons; i++)
				local_node->opterons[i]->check();

			lib::udelay(1000000);
			printf("wake ");
#else
			cli();
			asm volatile("hlt" ::: "memory");
#endif
		}
	}

	config->local_node->added = 1;

	nodes = (Node **)zalloc(sizeof(void *) * nnodes);
	assert(nodes);
	nodes[0] = local_node;

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
	if (options->tracing)
		setup_gsm();
	finalise();
	finished();
}
