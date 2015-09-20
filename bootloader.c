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
#include "platform/ipmi.h"
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
IPMI *ipmi;
char *asm_relocated;

static uint64_t dram_top;
static unsigned nnodes;

void check(void)
{
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->check();
}

static void scan(void)
{
	dram_top = 0;

	// setup local DRAM windows
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		// trim nodes that are over sized and not according to granularity
		(*node)->trim_dram_maps();
		(*node)->dram_base = dram_top;

		for (Opteron *const *nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			(*nb)->dram_base = dram_top;
			dram_top += (*nb)->dram_size;
		}

		dram_top = roundup(dram_top, 1ULL << Numachip2::SIU_ATT_SHIFT);
		(*node)->dram_end = dram_top - 1;

		if (options->debug.maps)
			printf("%03x dram_base=0x%"PRIx64" dram_size=0x%"PRIx64" dram_end=0x%"PRIx64"\n",
				(*node)->sci, (*node)->dram_base, (*node)->dram_size, (*node)->dram_end);
	}
}

static void add(const Node &node)
{
	unsigned range;

	// 7. setup MMIO32 ATT to master
	// forward everything for now, even regions that map to dram (in case of non-coherent accesses to dram)
	node.numachip->mmioatt.range(0, 0xffffffff, local_node->sci);

	// 8. set Opteron maps to forward VGA and MMIO32 regions to NumaChip (which then forwards according to MMIO32 ATT)
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
			(*nb)->drammap.add(range + 1, node.dram_end + 1, dram_top - 1, node.numachip->ht);

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

			// We must probe first to find NumaChip HT node (it might be different from others)
			ht_t ht = Numachip2::probe(config->nodes[n].sci);
			if (ht) {
				if (options->debug.maps)
					printf("\n%03x: DRAM ATT 0x%"PRIx64":0x%"PRIx64" to %03x", config->nodes[n].sci, base, limit, dest);

				// FIXME: use observer instance
				xassert(limit > base);

				const uint64_t mask = (1ULL << Numachip2::SIU_ATT_SHIFT) - 1;
				xassert((base & mask) == 0);
				xassert((limit & mask) == mask);

				lib::mcfg_write32(config->nodes[n].sci, 0, 24 + ht, Numachip2::SIU_ATT_INDEX >> 12,
						  Numachip2::SIU_ATT_INDEX & 0xfff, (1 << 31) | (base >> Numachip2::SIU_ATT_SHIFT));

				for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << Numachip2::SIU_ATT_SHIFT)
					lib::mcfg_write32(config->nodes[n].sci, 0, 24 + ht,
							  Numachip2::SIU_ATT_ENTRY >> 12, Numachip2::SIU_ATT_ENTRY & 0xfff, dest);

			}
		}
	}
	printf("\n");
}

static void setup_info(void)
{
	xassert(sizeof(struct numachip_info) <= 32);
	struct numachip_info *infop;
	uint32_t info[sizeof(*infop) / 4];
	memset(info, 0, sizeof(info));

	infop = (struct numachip_info *)&info;

	infop->layout = LAYOUT;
	infop->size_x = config->size[0];
	infop->size_y = config->size[1];
	infop->size_z = config->size[2];
	infop->northbridges = local_node->numachip->ht;
	infop->neigh_ht = local_node->neigh_ht;
	infop->neigh_link = local_node->neigh_link;
	infop->symmetric = 1;
	infop->renumbering = 0;
	infop->devices = config->local_node->devices;
	infop->observer = !config->local_node->partition;
	infop->cores = acpi->napics;
	infop->ht = local_node->numachip->ht;
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
	xassert(sizeof(VER) <= sizeof(infop->firmware_ver));
	strncpy(infop->firmware_ver, VER, sizeof(infop->firmware_ver));

	// write to numachip
	for (unsigned n = 0; n < nnodes; n++) {
		for (unsigned i = 0; i < sizeof(info)/sizeof(info[0]); i++) {
			xassert(i < Numachip2::INFO_SIZE);
			nodes[n]->numachip->write32(Numachip2::INFO + i*4, info[i]);
		}
	}
}

static void remap(void)
{
	// 1. reprogram local opteron ranges in case of trimming
	for (ht_t i = 0; i < local_node->nopterons; i++) {
		const uint64_t base  = local_node->opterons[i]->dram_base;
		const uint64_t limit = local_node->opterons[i]->dram_base + local_node->opterons[i]->dram_size - 1;

		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++) {
			for (unsigned range = 0; range < 8; range++) {
				uint64_t orig_base, orig_limit;
				ht_t dst;

				if ((*nb)->drammap.read(range, &orig_base, &orig_limit, &dst) && dst == i) {
					(*nb)->drammap.add(range, base, limit, i);
					break;
				}
			}
		}

		// reprogram local range
		local_node->opterons[i]->write32(Opteron::DRAM_BASE, base >> 27);
		local_node->opterons[i]->write32(Opteron::DRAM_LIMIT, limit >> 27);
	}

	// 2. setup local NumaChip DRAM ranges
	unsigned range = 0;
	for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		local_node->numachip->drammap.add(range++, (*nb)->dram_base, (*nb)->dram_base + (*nb)->dram_size - 1, (*nb)->ht);

	// 3. route higher access to NumaChip
	if (nnodes > 1)
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			(*nb)->drammap.add(range, nodes[1]->opterons[0]->dram_base, dram_top - 1, local_node->numachip->ht);

	// 4. setup NumaChip DRAM registers
	local_node->numachip->write32(Numachip2::DRAM_SHARED_BASE, local_node->dram_base >> 24);
	local_node->numachip->write32(Numachip2::DRAM_SHARED_LIMIT, (local_node->dram_end - 1) >> 24);

	// 5. setup DRAM ATT routing
	for (Node *const *node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Node *const *dnode = &nodes[0]; dnode < &nodes[nnodes]; dnode++)
			(*node)->numachip->dramatt.range((*dnode)->dram_base, (*dnode)->dram_end, (*dnode)->sci);

	// 6. set top of memory
	lib::wrmsr(MSR_TOPMEM2, dram_top);
	push_msr(MSR_TOPMEM2, dram_top);

	uint64_t topmem = lib::rdmsr(MSR_TOPMEM);
	push_msr(MSR_TOPMEM, topmem);

	// 7. add NumaChip MMIO32 ranges
	local_node->numachip->mmiomap.add(0, Opteron::MMIO_VGA_BASE, Opteron::MMIO_VGA_LIMIT, local_node->opterons[0]->ioh_ht);
	local_node->numachip->mmiomap.add(1, lib::rdmsr(MSR_TOPMEM), 0xffffffff, local_node->opterons[0]->ioh_ht);

	for (Node *const *node = &nodes[1]; node < &nodes[nnodes]; node++)
		add(**node);

	// trim e820 map to first node, as DRAM top may have been trimmed
	struct e820entry *top = e820->position(nodes[0]->dram_end);
	top->length = nodes[0]->dram_end + 1 - top->base;

	// 8. update e820 map
	for (Node *const *node = &nodes[0]; node < &nodes[nnodes]; node++) {
		for (Opteron *const *nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			// master always first in array
			if (node != &nodes[0])
				e820->add((*nb)->dram_base, (*nb)->dram_size, E820::RAM);

			if (options->tracing/* && node == &nodes[0] && nb == &(*node)->opterons[0]*/) {
				(*nb)->trace_base = (*nb)->dram_base + (*nb)->dram_size - options->tracing;
				(*nb)->trace_limit = (*nb)->trace_base + options->tracing - 1;

				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);
			}
		}
	}

	// 9. setup IOH limits
#ifdef FIXME /* hangs on remote node due to config map being cleared earlier */
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
		(*node)->neigh_ht = local_node->neigh_ht;
		(*node)->neigh_link = local_node->neigh_link;
		printf("%03x Numachip @ HT%u.%u\n", (*node)->sci, (*node)->neigh_ht,
		       (*node)->neigh_link);
	}
}

static void tracing_arm(void)
{
	if (!options->tracing)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_arm();
}

static void tracing_start(void)
{
	if (!options->tracing)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_start();
}

static void tracing_stop(void)
{
	if (!options->tracing)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		(*node)->tracing_stop();
}

static void boot_core_host(const uint32_t apicid, const uint32_t vector)
{
	*REL32(vector) = vector;
	uint32_t start_eip = (uint32_t)REL32(entry);
	xassert(!(start_eip & ~0xff000));

	// ensure semaphore set
	xassert(trampoline_sem_getvalue());

	// init IPI
	lib::native_apic_icr_write(APIC_INT_ASSERT | APIC_DM_INIT, apicid);
	// startup IPI
	lib::native_apic_icr_write(APIC_INT_ASSERT | APIC_DM_STARTUP | (start_eip >> 12), apicid);
}

static void boot_core(const uint32_t apicid, const uint32_t vector)
{
	*REL32(vector) = vector;
	uint32_t start_eip = (uint32_t)REL32(entry);
	xassert(!(start_eip & ~0xff000));

	// ensure semaphore set
	xassert(trampoline_sem_getvalue());

	// init	IPI
	local_node->numachip->apic_icr_write(APIC_DM_INIT, apicid);
	// startup IPI
	local_node->numachip->apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12), apicid);
}

static void setup_cores_observer(void)
{
	printf("APICs");

	for (unsigned n = 1; n < acpi->napics; n++) {
		trampoline_sem_init(1);
		boot_core(((uint32_t)local_node->sci << 8) | acpi->apics[n], VECTOR_SETUP_OBSERVER);
		if (trampoline_sem_wait())
			fatal("%u cores failed to complete observer setup (status %u)", trampoline_sem_getvalue(), *REL32(vector));
	}

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

	// ensure alignment
	xassert(!(mtrr_fixed & 7));
	xassert(!(fixed_mtrr_regs & 3));
	xassert(!(mtrr_var_base & 7));
	xassert(!(mtrr_var_mask & 7));

	printf("Variable MTRRs:\n");
	for (unsigned i = 0; i < 8; i++) {
		mtrr_var_base[i] = lib::rdmsr(MSR_MTRR_PHYS_BASE0 + i * 2);
		mtrr_var_mask[i] = lib::rdmsr(MSR_MTRR_PHYS_MASK0 + i * 2);

		if (mtrr_var_mask[i] & 0x800ULL)
			printf("- 0x%011"PRIx64":0x%011"PRIx64" %s\n", mtrr_var_base[i] & ~0xfffULL,
			  mtrr_var_mask[i] & ~0xfffULL, MTRR_TYPE(mtrr_var_base[i] & 0xffULL));
	}
#endif

	xassert(!((unsigned long)REL32(vector) & 3));
	xassert(!((unsigned long)REL32(entry) & 3));
	xassert(!((unsigned long)REL32(pending) & 1));

	lib::critical_enter();

	// setup cores
	printf("APICs");
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
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
			trampoline_sem_init(1);
			boot_core((*node)->apics[n], VECTOR_SETUP);
			if (trampoline_sem_wait()) {
				tracing_stop();
				fatal("APIC 0x%x failed to complete setup", (*node)->apics[n]);
			}
		}
		(*node)->napics = acpi->napics;
	}
	printf("\n");
	lib::critical_leave();
}

static void test_prepare(void)
{
	for (unsigned i = 0; i < TEST_SIZE / 4; i++)
		lib::mem_write32(((uint64_t)TEST_BASE_HIGH << 32) + TEST_BASE_LOW + i * 4, lib::hash32(i));
}

static void test_verify(void)
{
	unsigned errors = 0;

	for (unsigned i = 0; i < TEST_SIZE / 4; i++) {
		uint32_t corr = lib::hash32(i);
		uint64_t addr = ((uint64_t)TEST_BASE_HIGH << 32) + TEST_BASE_LOW + i * 4;
		uint32_t val = lib::mem_read32(addr);

		if (val != corr && val != ~corr) {
			if (!errors)
				printf("\n");
			printf("address 0x%llx should have 0x%08x or 0x%08x, but has 0x%08x\n", addr, corr, ~corr, val);
			errors++;
		}
	}

	check();
	if (errors || *REL32(errors)) {
		tracing_stop();
		fatal("%u errors detected during test", errors+*REL32(errors));
	}
}

static void test_cores(void)
{
	uint16_t cores = 0;
	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		cores += (*node)->napics;
	cores -= 1; // exclude BSC

	if (options->boot_wait)
		lib::wait_key("Press enter to start core testing");

	const unsigned iterations = 30;
	printf("Testing %u cores for %u iterations:", cores, iterations);
	test_prepare();
	lib::critical_enter();

	for (unsigned loop = 0; loop < iterations; loop++) {
		*REL32(errors) = 0; // clear error counter
		trampoline_sem_init(cores);
		tracing_start();

		for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
			for (unsigned n = 0; n < (*node)->napics; n++) {
				if (node == &nodes[0] && n == 0) // skip BSC
					continue;
				boot_core((*node)->apics[n], VECTOR_TEST);
			}
		}
		if (trampoline_sem_wait()) {
			tracing_stop();
			fatal("%u cores failed to start test (status %u)", trampoline_sem_getvalue(), *REL32(vector));
		}

		lib::udelay(1500000);

		// initiate finish, order here is important re-initialize the semaphore first
		trampoline_sem_init(cores);
		*REL32(vector) = VECTOR_TEST_FINISH;

		if (trampoline_sem_wait()) {
			tracing_stop();
			fatal("%u cores failed to finish test", trampoline_sem_getvalue());
		}

		test_verify();
		tracing_stop();
		printf(" %u", loop);
	}
	lib::critical_leave();
	printf("\n");
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

	struct acpi_mem_affinity ment;
	ment.type = 1;
	ment.length = 40;
	ment.reserved1 = 0;
	ment.proximity = domain;
	ment.reserved2 = 0;
	ment.flags = 1;
	ment.reserved3[0] = 0;
	ment.reserved3[1] = 0;

	// add memory below VGA MMIO area
	ment.base = 0;
	ment.lengthlo = 0xa0000 - ment.base;
	ment.lengthhi = 0;
	srat.append((const char *)&ment, sizeof(ment));

	// add memory before MMIO32 area
	ment.base = 0x100000;
	ment.lengthlo = lib::rdmsr(MSR_TOPMEM) - ment.base;
	ment.lengthhi = 0;
	srat.append((const char *)&ment, sizeof(ment));

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned n = 0;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			ment.proximity = domain;
			ment.base =(*nb)->dram_base;
			ment.lengthlo = (uint32_t)(*nb)->dram_size;
			ment.lengthhi = (uint32_t)((*nb)->dram_size >> 32);

			// if this is the entry covering the MMIO hole, raise
			if (ment.base < (4ULL << 30)) {
				ment.base = 4ULL << 30;
				ment.lengthhi -= 1;
			}

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

	if (options->debug.acpi) {
		printf("Topology distances:\n   ");
		for (Node **snode = &nodes[0]; snode < &nodes[nnodes]; snode++)
			for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++)
				printf(" %3u", (snode - nodes) * (*snode)->nopterons + (snb - (*snode)->opterons));
		printf("\n");
	}

	for (Node **snode = &nodes[0]; snode < &nodes[nnodes]; snode++) {
		for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++) {
			if (options->debug.acpi)
				printf("%3u", (snode - nodes) * (*snode)->nopterons + (snb - (*snode)->opterons));

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

					if (options->debug.acpi)
						printf(" %3u", dist);
					slit.append((const char *)&dist, sizeof(dist));
				}
			}
			if (options->debug.acpi)
				printf("\n");
			(*n)++;
		}
	}

	acpi->allocate((sizeof(struct acpi_sdt) + 64) * 4 + mcfg.used + apic.used + srat.used + slit.used);
	acpi->replace(mcfg);
	acpi->replace(apic);
	acpi->replace(srat);
	acpi->replace(slit);
	acpi->check();
}

static void clear_dram(void)
{
	printf("Clearing DRAM");
	lib::critical_enter();

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_start();
	}

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_wait();
	}

	lib::critical_leave();
	printf("\n");

	// avoid noise when tracing
	if (options->tracing)
		return;

	for (Node **node = &nodes[0]; node < &nodes[nnodes]; node++)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_scrub_enable();
}

static void finished(const char *label)
{
	check();

	if (options->boot_wait)
		lib::wait_key("Press enter to boot");

	// reenable wrap32
	uint64_t msr = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, msr & ~(1ULL << 17));

	if (config->local_node->partition)
		printf("Partition %u unification", config->local_node->partition);
	else
		printf("Observer setup");

	printf(" succeeded; executing syslinux label %s\n", label);
	os->exec(label);
}

void caches(const bool enable)
{
	if (!enable) {
		if (Opteron::family >= 0x15) {
			// ensure CombineCr0Cd is set on fam15h
			uint64_t msr = lib::rdmsr(MSR_CU_CFG3) | (1ULL << 49);
			lib::wrmsr(MSR_CU_CFG3, msr);
		}
		disable_cache();
	} else
		enable_cache();

	trampoline_sem_init(acpi->napics - 1);
	for (unsigned n = 1; n < acpi->napics; n++)
		boot_core_host(acpi->apics[n], enable ? VECTOR_CACHE_ENABLE : VECTOR_CACHE_DISABLE);
	if (trampoline_sem_wait())
		fatal("%u cores did not complete requested operation", trampoline_sem_getvalue());

}

static void wait_status(void)
{
	printf("Waiting for");

	for (unsigned n = 0; n < config->nnodes; n++) {
		if (&config->nodes[n] == config->local_node) /* Self */
			continue;

		if (!config->nodes[n].seen)
			printf(" %03x/%s", config->nodes[n].sci, config->nodes[n].hostname);
	}

	printf("\n");
}

#define NODE_SYNC_STATES(state)			\
	state(CMD_STARTUP)			\
	state(RSP_SLAVE_READY)			\
	state(CMD_RESET_FABRIC)			\
	state(RSP_RESET_COMPLETE)		\
	state(CMD_TRAIN_FABRIC)			\
	state(RSP_PHY_TRAINED)			\
	state(RSP_PHY_NOT_TRAINED)		\
	state(CMD_VALIDATE_RINGS)		\
	state(RSP_RINGS_OK)			\
	state(RSP_RINGS_NOT_OK)			\
	state(CMD_SETUP_FABRIC)			\
	state(RSP_FABRIC_READY)			\
	state(RSP_FABRIC_NOT_READY)		\
	state(CMD_VALIDATE_FABRIC)		\
	state(RSP_FABRIC_OK)			\
	state(RSP_FABRIC_NOT_OK)		\
	state(CMD_CONTINUE)			\
	state(RSP_ERROR)			\
	state(RSP_NONE)

#define ENUM_DEF(state) state,
#define ENUM_NAMES(state) #state,
#define UDP_SIG 0xdeafcafa
#define UDP_MAXLEN 256

enum node_state { NODE_SYNC_STATES(ENUM_DEF) };

static const char *node_state_name[] = { NODE_SYNC_STATES(ENUM_NAMES) };

struct state_bcast {
	uint32_t sig;
	enum node_state state;
	uint8_t mac[6];
	uint8_t rsv[2];
	uint32_t sci;
	uint32_t tid;
} __attribute__ ((packed));

static bool handle_command(const enum node_state cstate, enum node_state *rstate)
{
	switch (cstate) {
		case CMD_RESET_FABRIC:
			local_node->numachip->fabric_reset();
			*rstate = RSP_RESET_COMPLETE;
			return 1;
		case CMD_TRAIN_FABRIC:
			local_node->numachip->fabric_train();
			*rstate = RSP_PHY_TRAINED;
			return 1;
		case CMD_VALIDATE_RINGS:
			/* Need anything here ?? */
			*rstate = RSP_RINGS_OK;
			return 1;
		case CMD_SETUP_FABRIC:
			local_node->numachip->fabric_routing();
			*rstate = RSP_FABRIC_READY;
			return 1;
		case CMD_VALIDATE_FABRIC:
			/* Need anything here ?? */
			*rstate = RSP_FABRIC_OK;
			return 1;
		default:
			return 0;
	}
	return 1;
}

static void wait_for_slaves(void)
{
	struct state_bcast cmd;
	bool ready_pending = 1;
	int count, backoff, last_stat;
	bool do_restart = 0;
	enum node_state waitfor, own_state;
	uint32_t last_cmd = ~0;
	char buf[UDP_MAXLEN];
	struct state_bcast *rsp = (struct state_bcast *)buf;

	os->udp_open();

	cmd.sig = UDP_SIG;
	cmd.state = CMD_STARTUP;
	memcpy(cmd.mac, config->local_node->mac, 6);
	cmd.sci = config->local_node->sci;
	cmd.tid = 0; /* Must match initial rsp.tid for RSP_SLAVE_READY */
	waitfor = RSP_SLAVE_READY;
	printf("Waiting for %u servers...\n", config->nnodes - 1);
	count = 0;
	backoff = 1;
	last_stat = 0;

	while (1) {
		uint32_t ip = 0;
		size_t len;

		if (++count >= backoff) {
			os->udp_write(&cmd, sizeof(cmd), 0xffffffff);

			lib::udelay(100 * backoff);
			last_stat += backoff;

			if (backoff < 32)
				backoff = backoff * 2;

			count = 0;
		}

		if (cmd.state == CMD_CONTINUE)
			break;

		if (last_cmd != cmd.tid) {
			/* Perform commands locally as well */
			if (handle_command(cmd.state, &own_state))
				do_restart = own_state != waitfor;

			if (do_restart)
				printf("Command did not complete successfully on master (reason %s), resetting...\n",
				       node_state_name[own_state]);

			config->local_node->seen = 1;
			last_cmd = cmd.tid;
		}

		if (config->nnodes > 1) {
			len = os->udp_read(rsp, UDP_MAXLEN, &ip);
			if (!do_restart) {
				if (last_stat > 64) {
					last_stat = 0;
					wait_status();
				}

				if (!len)
					continue;
			}
		} else
			len = 0;

		if (len >= sizeof(rsp) && rsp->sig == UDP_SIG) {
/*
			printf("Got rsp packet from %d.%d.%d.%d (%02x:%02x:%02x:%02x:%02x:%02x) (state %s, sciid %03x, tid %d)\n",
			       ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
			       rsp->mac[0], rsp->mac[1], rsp->mac[2],
			       rsp->mac[3], rsp->mac[4], rsp->mac[5],
			       (rsp->state > RSP_NONE) ? "UNKNOWNN" : node_state_name[rsp->state], rsp->sci, rsp->tid);
*/
			for (unsigned n = 0; n < config->nnodes; n++) {
				if (memcmp(&config->nodes[n].mac, rsp->mac, 6) == 0) {
					if ((rsp->state == waitfor) && (rsp->tid == cmd.tid)) {
						config->nodes[n].seen = 1;
					} else if ((rsp->state == RSP_PHY_NOT_TRAINED) ||
						   (rsp->state == RSP_RINGS_NOT_OK) ||
						   (rsp->state == RSP_FABRIC_NOT_READY) ||
						   (rsp->state == RSP_FABRIC_NOT_OK)) {
						if (!config->nodes[n].seen) {
							printf("%03x/%s failed with %s; restarting synchronisation\n",
							       rsp->sci, config->nodes[n].hostname, node_state_name[rsp->state]);
							do_restart = 1;
							config->nodes[n].seen = 1;
						}
					} else if (rsp->state == RSP_ERROR)
						error_remote(rsp->sci, config->nodes[n].hostname, ip, (char *)rsp + sizeof(struct state_bcast));
					break;
				}
			}
		}

		ready_pending = 0;

		for (unsigned n = 0; n < config->nnodes; n++) {
			if (&config->nodes[n] == config->local_node) /* Self */
				continue;

			if (!config->nodes[n].seen) {
				ready_pending = 1;
				break;
			}
		}

		if (!ready_pending || do_restart) {
			if (do_restart) {
				cmd.state = CMD_RESET_FABRIC;
				waitfor = RSP_RESET_COMPLETE;
				do_restart = 0;
			} else if (cmd.state == CMD_STARTUP) {
				/* Skip over resetting fabric, as that's just if training fails */
				cmd.state = CMD_TRAIN_FABRIC;
				waitfor = RSP_PHY_TRAINED;
			} else if (cmd.state == CMD_TRAIN_FABRIC) {
				cmd.state = CMD_VALIDATE_RINGS;
				waitfor = RSP_RINGS_OK;
			} else if (cmd.state == CMD_RESET_FABRIC) {
				/* When invoked, continue at fabric training */
				cmd.state = CMD_TRAIN_FABRIC;
				waitfor = RSP_PHY_TRAINED;
			} else if (cmd.state == CMD_VALIDATE_RINGS) {
				cmd.state = CMD_SETUP_FABRIC;
				waitfor = RSP_FABRIC_READY;
			} else if (cmd.state == CMD_SETUP_FABRIC) {
				cmd.state = CMD_VALIDATE_FABRIC;
				waitfor = RSP_FABRIC_OK;
			} else if (cmd.state == CMD_VALIDATE_FABRIC) {
				cmd.state = CMD_CONTINUE;
				waitfor = RSP_NONE;
			}

			/* Clear seen flag */
			for (unsigned n = 0; n < config->nnodes; n++)
				config->nodes[n].seen = 0;

			cmd.tid++;
			count = 0;
			backoff = 1;
			printf("Issuing %s; expecting %s\n",
			       node_state_name[cmd.state], node_state_name[waitfor]);
		}
	}
}

static void wait_for_master(void)
{
	struct state_bcast rsp, cmd;
	int count, backoff;
	int go_ahead = 0;
	uint32_t last_cmd = ~0;
	uint32_t ip;

	os->udp_open();

	rsp.sig = UDP_SIG;
	rsp.state = RSP_SLAVE_READY;
	memcpy(rsp.mac, config->local_node->mac, 6);
	rsp.sci = config->local_node->sci;
	rsp.tid = 0;

	count = 0;
	backoff = 1;

	while (!go_ahead) {
		if (++count >= backoff) {
			local_node->check();
			printf("Replying with %s\n", node_state_name[rsp.state]);
			os->udp_write(&rsp, sizeof(rsp), 0xffffffff);
			lib::udelay(100 * backoff);

			if (backoff < 32)
				backoff = backoff * 2;

			count = 0;
		}

		/* In order to avoid jamming, broadcast own status at least
		 * once every 2*cfg_nodes packet seen */
		for (unsigned n = 0; n < 2 * config->nnodes; n++) {
			int len = os->udp_read(&cmd, sizeof(cmd), &ip);

			if (!len)
				break;

			if (len != sizeof(cmd) || cmd.sig != UDP_SIG)
				continue;
/*
			printf("Got cmd packet from %d.%d.%d.%d (%02x:%02x:%02x:%02x:%02x:%02x) (state %s, sciid %03x, tid %d)\n",
			       ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
			       cmd.mac[0], cmd.mac[1], cmd.mac[2],
			       cmd.mac[3], cmd.mac[4], cmd.mac[5],
			       (cmd.state > RSP_NONE) ? "UNKNOWNN" : node_state_name[cmd.state], cmd.sci, cmd.tid);
*/
			if (memcmp(config->nodes[0].mac, cmd.mac, 6) == 0) {
				if (cmd.tid == last_cmd) {
					/* Ignoring seen command */
					continue;
				}

				last_cmd = cmd.tid;
				count = 0;
				backoff = 1;

				if (handle_command(cmd.state, &rsp.state)) {
					rsp.tid = cmd.tid;
				} else if (cmd.state == CMD_CONTINUE) {
					printf("Master signalled go-ahead\n");
					/* Belt and suspenders: slaves re-broadcast go-ahead command */
					os->udp_write(&cmd, sizeof(cmd), 0xffffffff);
					go_ahead = 1;
					break;
				}
			}
		}
	}
}

static void test_map(void)
{
	const uint64_t stride = 1ULL << 20;

	printf("Testing memory map");
	for (uint64_t pos = 0; pos < dram_top + stride * 16; pos += stride)
		lib::mem_read32(pos);
	printf("\n");
}

int main(const int argc, char *const argv[])
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

	uint16_t reason = lib::pmio_read16(0x44);
	if (reason & ~((1 << 2) | (1 << 6))) /* Mask out CF9 and keyboard reset */
		warning("Last reboot reason (PM44h) was 0x%x", reason);

	if (lib::mcfg_read32(SCI_LOCAL, 0, 0x14, 0, 0x4c) & (1 << 30))
		warning("Last reboot is due to BootFail timer");

	if (!lib::rdmsr(MSR_PATCHLEVEL))
		warning("BIOS hasn't updated processor microcode; please use newer BIOS");

	// SMI often assumes HT nodes are Northbridges, so handover early
	if (options->handover_acpi)
		acpi->handover();

	if (options->singleton)
		config = new Config();
	else
		config = new Config(options->config_filename);

	local_node = new Node((sci_t)config->local_node->sci, (sci_t)config->master->sci);

	// ensure Numachip2 MMIO range is reserved for all cases
	e820->add(Numachip2::LOC_BASE, Numachip2::LOC_LIM - Numachip2::LOC_BASE + 1, E820::RESERVED);

	if (options->init_only) {
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			if (options->tracing)
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);
		finished(options->next_label);
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

	if (config->nnodes > 1) {
		// Use first node in config as "builder", to synchronize all slaves/observers
		if (config->local_node == &config->nodes[0])
			wait_for_slaves();
		else
			wait_for_master();
	}

	if (!config->local_node->partition) {
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			if (options->tracing)
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);

		setup_cores_observer();
		setup_info();
		finished(options->observer_label ? options->observer_label : options->next_label);
	}

	for (unsigned n = 0; n < config->nnodes; n++)
		if (config->nodes[n].partition == config->local_node->partition)
			nnodes++;

	// slaves
	if (!config->local_node->master) {
		os->cleanup();
		if (!options->handover_acpi) // handover performed earlier
			acpi->handover();
		handover_legacy(local_node->sci);

		// read from master after mapped
		printf("Waiting for %03x/%s", config->master->sci, config->master->hostname);
		local_node->numachip->write32(Numachip2::INFO + 4, (uint32_t)local_node);
		local_node->numachip->write32(Numachip2::INFO, 1 << 29);

		// wait for 'ready'
		while (local_node->numachip->read32(Numachip2::INFO) != 3 << 29) {
			local_node->check();
			cpu_relax();
		}

		printf("\n");
		local_node->iohub->smi_disable();
		pci_disable_all(local_node->sci);

		// clear BSP flag
		uint64_t val = lib::rdmsr(MSR_APIC_BAR);
		lib::wrmsr(MSR_APIC_BAR, val & ~(1ULL << 8));

		// disable XT-PIC
		lib::disable_xtpic();

		printf(BANNER "This server %03x/%s is part of a %u-server NumaConnect2 system\n"
		       "Refer to the console on %03x/%s", config->local_node->sci, config->local_node->hostname,
		       nnodes, config->master->sci, config->master->hostname);

		caches(0);

		lib::udelay(1000);

		// set 'go-ahead'
		local_node->numachip->write32(Numachip2::INFO, 7U << 29);

		// reenable wrap32
		val = lib::rdmsr(MSR_HWCR);
		lib::wrmsr(MSR_HWCR, val & ~(1ULL << 17));

		halt();
	}

	config->local_node->added = 1;

	nodes = (Node **)zalloc(sizeof(void *) * nnodes);
	xassert(nodes);
	nodes[0] = local_node;
	printf("Servers ready:\n");

	unsigned done = 1;
	do {
		for (unsigned n = 0; n < config->nnodes; n++) {
			// skip initialised nodes or nodes in other partitions
			if (config->nodes[n].added || config->nodes[n].partition != config->local_node->partition)
				continue;

			ht_t ht = Numachip2::probe_slave(config->nodes[n].sci);
			if (ht) {
				nodes[n] = new Node(config->nodes[n].sci, ht);
				config->nodes[n].added = 1;
				done++;
			}

			cpu_relax();
		}
	} while (done < nnodes);

	scan();
	remap();
	copy_inherit();
	clear_dram();
	if (options->tracing)
		setup_gsm();
	setup_info();
	e820->test();
	setup_cores();
	acpi_tables();
	tracing_arm();
	if (!options->fastboot)
		test_cores();
	test_map();
	finished(options->next_label);
}
