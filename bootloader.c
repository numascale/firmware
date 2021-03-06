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

// FIXME: drop config->{local_node,master} in favour of {local_node,master}->config
// FIXME: server add name should use id+1, not id

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/io.h>

#define SYNC_DEBUG 0

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
#include "platform/aml.h"
#include "platform/trampoline.h"
#include "platform/devices.h"
#include "platform/pcialloc.h"
#include "opteron/msrs.h"
#include "numachip2/numachip.h"
#include "numachip2/router.h"

OS *os;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
Node **nodes;
ACPI *acpi;
IPMI *ipmi;
Router *router;
char *asm_relocated;

uint64_t dram_top;
unsigned nnodes;

bool check(void)
{
	bool ret = 0;

	foreach_node(node)
		ret |= (*node)->check();

	return ret;
}

static void scan(void)
{
	dram_top = 0;

	// setup local DRAM windows
	foreach_node(node) {
		// trim nodes that are over sized and not according to granularity
		(*node)->trim_dram_maps();

		// if node DRAM range overlaps HT decode range, move up
		if ((dram_top < Opteron::HT_LIMIT) && ((dram_top + (*node)->dram_size) > Opteron::HT_BASE)) {
			dram_top = Opteron::HT_LIMIT;

			// move previous node's DRAM end to the HT decode range end, so cycles above the DRAM size are aborted, rather than hanging
			(*(node-1))->dram_end = Opteron::HT_LIMIT - 1;
		}

		(*node)->dram_base = dram_top;

		for (Opteron *const *nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			(*nb)->dram_base = dram_top;
			dram_top += (*nb)->dram_size;
		}

		dram_top = roundup(dram_top, 1ULL << Numachip2::SIU_ATT_SHIFT);
		(*node)->dram_end = dram_top - 1;

		if (options->debug.maps)
			printf("%s dram_base=0x%" PRIx64 " dram_size=0x%" PRIx64 " dram_end=0x%" PRIx64 "\n",
				pr_node((*node)->config->id), (*node)->dram_base, (*node)->dram_size, (*node)->dram_end);
	}
}

static void add(const Node &node)
{
	uint32_t memhole = local_node->opterons[0]->read32(Opteron::DRAM_HOLE);
	unsigned range;

	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		// 9. setup DRAM hole
		(*nb)->write32(Opteron::DRAM_HOLE, memhole & 0xff000002);

		range = 0;

		// 10. add below-node DRAM ranges
		(*nb)->drammap.set(range++, local_node->opterons[0]->dram_base, node.dram_base - 1, node.numachip->ht);

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

		uint64_t limit;

		// if last opteron in server, use DRAM limit rounded up to cover map holes (eg HT decoding)
		// so transactions are aborted rather than hanging
		if (range < node.nopterons - 1)
			limit = base + node.opterons[range]->dram_size - 1;
		else
			limit = node.dram_end;

		node.numachip->drammap.set(range, base, limit, node.opterons[range]->ht);

		for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++)
			(*nb)->drammap.set(range + 1, base, limit, range);

		range++;
	}

	// 14. redirect above last local DRAM address to NumaChip
	if (&node < nodes[nnodes - 1])
		for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++)
			(*nb)->drammap.set(range + 1, node.dram_end + 1, dram_top - 1, node.numachip->ht);

	// 15. point IO and config maps to Numachip
	for (Opteron *const *nb = &node.opterons[0]; nb < &node.opterons[node.nopterons]; nb++) {
		(*nb)->write32(Opteron::IO_MAP_LIMIT, 0xfff000 | node.numachip->ht);
		(*nb)->write32(Opteron::IO_MAP_BASE, 0x3);

		for (unsigned r = 1; r < 4; r++) {
			(*nb)->write32(Opteron::IO_MAP_LIMIT + r * 8, 0);
			(*nb)->write32(Opteron::IO_MAP_BASE + r * 8, 0);
		}
	}
}

static void setup_gsm_early(void)
{
	for (unsigned n = 0; n < config->nnodes; n++) {
		if (config->partitions[config->nodes[n].partition].unified)
			continue;

		// setup read-only map on observer
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++) {
			uint64_t base = 1ULL << Numachip2::GSM_SHIFT;
			(*nb)->mmiomap->set(9, base, base + (1ULL << Numachip2::GSM_SIZE_SHIFT) - 1, local_node->numachip->ht, 0, 1);
		}
	}
}

static void setup_gsm(void)
{
	printf("Setting up GSM to");
	for (unsigned n = 0; n < config->nnodes; n++)
		if (!config->partitions[config->nodes[n].partition].unified)
			printf(" %s", pr_node(config->nodes[n].id));

	foreach_node(node) {
		uint64_t base = (1ULL << Numachip2::GSM_SHIFT) + (*node)->dram_base;
		uint64_t limit = (1ULL << Numachip2::GSM_SHIFT) + (*node)->dram_end;

		(*node)->numachip->write32(Numachip2::GSM_MASK, (1ULL << (Numachip2::GSM_SHIFT - 36)) - 1);

		// setup ATT on observers for GSM
		for (unsigned n = 0; n < config->nnodes; n++) {
			if (config->partitions[config->nodes[n].partition].unified)
				continue;

			// We must probe first to find NumaChip HT node (it might be different from others)
			ht_t ht = Numachip2::probe(config->nodes[n].id);
			if (ht) {
				if (options->debug.maps)
					printf("\n%s: DRAM ATT 0x%012" PRIx64 ":0x%012" PRIx64 " to %s", pr_node(config->nodes[n].id), base, limit, pr_node((*node)->config->id));

				// FIXME: use observer instance
				xassert(limit > base);

				const uint64_t mask = (1ULL << Numachip2::SIU_ATT_SHIFT) - 1;
				xassert((base & mask) == 0);
				xassert((limit & mask) == mask);

				lib::mcfg_write32(config->nodes[n].id, 0, 24 + ht, Numachip2::SIU_ATT_INDEX >> 12,
						  Numachip2::SIU_ATT_INDEX & 0xfff, (1 << 31) | (base >> Numachip2::SIU_ATT_SHIFT));

				for (uint64_t addr = base; addr < (limit + 1U); addr += 1ULL << Numachip2::SIU_ATT_SHIFT)
					lib::mcfg_write32(config->nodes[n].id, 0, 24 + ht,
							  Numachip2::SIU_ATT_ENTRY >> 12, Numachip2::SIU_ATT_ENTRY & 0xfff, (*node)->config->id);
			}
		}
	}
	printf("\n");
}

static void setup_info(void)
{
	xassert(sizeof(struct numachip_info) <= Numachip2::INFO_SIZE * 4);
	struct numachip_info *infop;
	uint32_t info[sizeof(*infop) / 4];
	memset(info, 0, sizeof(info));

	for (unsigned n = 0; n < nnodes; n++) {
		infop = (struct numachip_info *)&info;
		infop->self = nodes[n]->config->id;

		bool unified = config->partitions[nodes[n]->config->partition].unified;
		infop->partition = unified ? nodes[n]->config->partition + 1 : 0;
		infop->master = unified ? config->master->id : 0xfff;

		struct Config::node *cur = nodes[n]->config;

		while (1) {
			// increment and wrap
			cur++;
			if (cur > &config->nodes[config->nnodes - 1])
				cur = &config->nodes[0];

			// couldn't find a match, so fully wrapped
			if (cur->id == local_node->config->id) {
				infop->next_master = 0xfff;
				break;
			}

			if (cur->partition && cur->partition != config->local_node->partition) {
				infop->next_master = cur->id;
				break;
			}
		}

		if (!config->partitions[config->local_node->partition].unified)
			infop->next = 0xfff;
		else {
			cur = nodes[n]->config;

			while (1) {
				// increment and wrap
				cur++;
				if (cur > &config->nodes[config->nnodes - 1])
					cur = &config->nodes[0];

				if (cur->partition == config->local_node->partition)
					break;
			}

			// last node in partition has next as 0xfff
			infop->next = (n == (nnodes - 1)) ? 0xfff : cur->id;
		}

		infop->hts = nodes[n]->numachip->ht + 1;
		infop->cores = nodes[n]->cores;
		infop->ht = nodes[n]->numachip->ht;
		infop->neigh_ht = nodes[n]->neigh_ht;
		infop->neigh_link = nodes[n]->neigh_link;
		infop->linkmask = nodes[n]->numachip->linkmask;
		strncpy(infop->firmware, VER, sizeof(infop->firmware));
#ifdef DEBUG
		printf("Firmware %s, self %03x, partition %u, master %03x, "
                        "next_master %03x, next %03x, hts %u, cores %u, "
                        "ht %u, neigh_ht %u, neigh_link %u\n",
                        infop->firmware, infop->self, infop->partition, infop->master,
                        infop->next_master, infop->next, infop->hts, infop->cores,
                        infop->ht, infop->neigh_ht, infop->neigh_link);
#endif
		// write to numachip
		for (unsigned i = 0; i < sizeof(info)/sizeof(info[0]); i++)
			nodes[n]->numachip->write32(Numachip2::INFO + i * 4, info[i]);
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
					(*nb)->drammap.set(range, base, limit, i);
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
		local_node->numachip->drammap.set(range++, (*nb)->dram_base, (*nb)->dram_base + (*nb)->dram_size - 1, (*nb)->ht);

	// 3. route higher access to NumaChip
	if (nnodes > 1)
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			(*nb)->drammap.set(range, nodes[1]->opterons[0]->dram_base, dram_top - 1, local_node->numachip->ht);

	// 5. setup DRAM ATT routing
	foreach_node(node)
		foreach_node(dnode)
			(*node)->numachip->dramatt.range((*dnode)->dram_base, (*dnode)->dram_end, (*dnode)->config->id);

	// 6. set top of memory
	lib::wrmsr(MSR_TOPMEM2, dram_top);
	push_msr(MSR_TOPMEM2, dram_top);

	uint64_t topmem = lib::rdmsr(MSR_TOPMEM);
	push_msr(MSR_TOPMEM, topmem);

	for (Node *const *node = &nodes[1]; node < &nodes[nnodes]; node++)
		add(**node);

	// trim e820 map to first node, as DRAM top may have been trimmed
	struct e820entry *top = e820->position(nodes[0]->dram_end);
	top->length = nodes[0]->dram_end + 1 - top->base;

	// 8. update e820 map
	foreach_node(node) {
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

	// workaround BIOS bug which can corrupt higher area of memory due to incomplete SMM address mask
	e820->add(0x1003ff00000, 1 << 20, E820::RESERVED);
	e820->add(0x2003ff00000, 1 << 20, E820::RESERVED);

	// FIXME: reserve 8MB at top of DRAM to prevent igb tx queue hangs
	uint64_t len = 8ULL << 20;
	e820->add(dram_top - len, len, E820::RESERVED);
}

static void enable_coherency(void)
{
	foreach_node(node) {
		(*node)->numachip->write32(Numachip2::DRAM_SHARED_BASE, (*node)->dram_base >> 24);
		(*node)->numachip->write32(Numachip2::DRAM_SHARED_LIMIT, ((*node)->dram_end - 1) >> 24);
	}
}

#define MTRR_TYPE(x) (x) == 0 ? "uncacheable" : (x) == 1 ? "write-combining" : (x) == 4 ? "write-through" : (x) == 5 ? "write-protect" : (x) == 6 ? "write-back" : "unknown"

static void copy_inherit(void)
{
	for (Node **node = &nodes[1]; node < &nodes[nnodes]; node++) {
#ifdef BROKEN
		const uint64_t rnode = (*node)->dram_base + (*node)->numachip->read32(Numachip2::INFO + 4);
		lib::memcpy64((uint64_t)&(*node)->neigh_ht, rnode + xoffsetof(local_node->neigh_ht, local_node), sizeof((*node)->neigh_ht));
#endif
		(*node)->neigh_ht = local_node->neigh_ht;
		(*node)->neigh_link = local_node->neigh_link;
		printf("%03x Numachip @ HT%u.%u\n", (*node)->config->id, (*node)->neigh_ht,
		       (*node)->neigh_link);
	}
}

static void tracing_arm(void)
{
	if (!options->tracing)
		return;

	foreach_node(node)
		(*node)->tracing_arm();
}
#ifdef TRACE
static void tracing_start(void)
{
	if (!options->tracing)
		return;

	foreach_node(node)
		(*node)->tracing_start();
}

static void tracing_stop(void)
{
	if (!options->tracing)
		return;

	foreach_node(node)
		(*node)->tracing_stop();
}
#endif
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

	// init IPI
	local_node->numachip->apic_icr_write(APIC_DM_INIT, apicid);
	// startup IPI
	local_node->numachip->apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12), apicid);
}

static void caches_global(const bool enable)
{
	if (enable)
		enable_cache();

	foreach_node(node) {
		for (unsigned n = 0; n < (*node)->napics; n++) {
			if (node == &nodes[0] && n == 0) // skip BSC
				continue;

			trampoline_sem_init(1);
			boot_core((*node)->apics[n], enable ? VECTOR_CACHE_ENABLE : VECTOR_CACHE_DISABLE);

			if (trampoline_sem_wait())
				fatal("%u cores did not complete requested operation", trampoline_sem_getvalue());
		}
	}

	if (!enable) {
		if (Opteron::family >= 0x15) {
			// ensure CombineCr0Cd is set on fam15h
			uint64_t msr = lib::rdmsr(MSR_CU_CFG3) | (1ULL << 49);
			lib::wrmsr(MSR_CU_CFG3, msr);
		}
		disable_cache();
	}
}

static void setup_cores_observer(void)
{
	printf("APICs");

	for (unsigned n = 1; n < acpi->napics; n++) {
		trampoline_sem_init(1);
		boot_core(((uint32_t)local_node->config->id << 8) | acpi->apics[n], VECTOR_SETUP_OBSERVER);
		if (trampoline_sem_wait())
			fatal("%u cores failed to complete observer setup (status %u)", trampoline_sem_getvalue(), *REL32(vector));
	}

	printf("\n");
}

static void setup_apicids(void)
{
	foreach_node(node)
		for (unsigned n = 0; n < acpi->napics; n++)
			(*node)->apics[n] = ((uint32_t)(*node)->config->id << 8) | acpi->apics[n];
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
		printf("- 0x%016" PRIx64 "\n", mtrr_fixed[i]);
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
			printf("- 0x%011" PRIx64 ":0x%011" PRIx64 " %s\n", mtrr_var_base[i] & ~0xfffULL,
			  mtrr_var_mask[i] & ~0xfffULL, MTRR_TYPE(mtrr_var_base[i] & 0xffULL));
	}
#endif

	xassert(!((unsigned long)REL32(vector) & 3));
	xassert(!((unsigned long)REL32(entry) & 3));
	xassert(!((unsigned long)REL32(pending) & 1));

	lib::critical_enter();

	// setup cores
	printf("APICs");
	foreach_node(node) {
		// set correct MCFG base per node
		const uint64_t mcfg = Numachip2::MCFG_BASE | ((uint64_t)(*node)->config->id << 28) | 0x21;
		push_msr(MSR_MCFG, mcfg);

		for (unsigned n = 0; n < acpi->napics; n++) {
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
#ifdef TRACE
				tracing_stop();
#endif
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
#ifdef TRACE
		tracing_stop();
#endif
		fatal("%u errors detected during test", errors+*REL32(errors));
	}

	for (unsigned i = 0; i < TEST_SIZE / 4; i++) {
		uint64_t addr = ((uint64_t)TEST_BASE_HIGH << 32) + TEST_BASE_LOW + i * 4;
		lib::mem_write32(addr, 0);
	}
}

static void test_cores(void)
{
	uint16_t cores = 0;
	foreach_node(node)
		cores += (*node)->napics;
	cores -= 1; // exclude BSC

	if (options->boot_wait)
		lib::wait_key("Press enter to start core testing");

	printf("Validating with %u cores", cores);
	test_prepare();
	lib::critical_enter();

	*REL32(errors) = 0; // clear error counter
	trampoline_sem_init(cores);

	uint64_t finish = lib::rdtscll() + (uint64_t)1e6 * Opteron::tsc_mhz;

#ifdef TRACE
	tracing_start();
#endif
	foreach_node(node) {
		for (unsigned n = 0; n < (*node)->napics; n++) {
			if (node == &nodes[0] && n == 0) // skip BSC
				continue;
			boot_core((*node)->apics[n], VECTOR_TEST);
			if (check())
				lib::wait_key("Press any key to continue");
		}
	}
	if (trampoline_sem_wait()) {
#ifdef TRACE
		tracing_stop();
#endif
		fatal("%u cores failed to start test (status %u)", trampoline_sem_getvalue(), *REL32(vector));
	}

	for (unsigned i = 0; i < 10; i++) {
		while (lib::rdtscll() < finish) {
			if (check())
				lib::wait_key("Press any key to continue");
			cpu_relax();
		}

		finish = lib::rdtscll() + (uint64_t)1e6 * Opteron::tsc_mhz;
		printf(".");
	}

	// initiate finish, order here is important re-initialize the semaphore first
	trampoline_sem_init(cores);
	*REL32(vector) = VECTOR_TEST_FINISH;

	if (trampoline_sem_wait()) {
#ifdef TRACE
		tracing_stop();
#endif
		fatal("%u cores failed to finish test", trampoline_sem_getvalue());
	}

#ifdef TRACE
	tracing_stop();
#endif
	test_verify();
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
	foreach_node(node) {
		struct acpi_mcfg ent;
		memset(&ent, 0, sizeof(ent));
		ent.address = Numachip2::MCFG_BASE | ((uint64_t)(*node)->config->id << 28ULL);
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
	foreach_node(node) {
		unsigned n = 0;

		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++) {
			for (unsigned m = 0; m < (*nb)->cores; m++) {
				struct acpi_x2apic_apic ent;
				memset(&ent, 0, sizeof(ent));
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
	memset(&ment, 0, sizeof(ment));
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

	foreach_node(node) {
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
				memset(&ent, 0, sizeof(ent));
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
	const unsigned n_offset = slit.reserve(8);

	if (options->debug.acpi) {
		printf("Topology distances:\n   ");
		foreach_node(snode)
			for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++)
				printf(" %3u", (snode - nodes) * (*snode)->nopterons + (snb - (*snode)->opterons));
		printf("\n");
	}

	foreach_node(snode) {
		for (Opteron **snb = &(*snode)->opterons[0]; snb < &(*snode)->opterons[(*snode)->nopterons]; snb++) {
			if (options->debug.acpi)
				printf("%3u", (snode - nodes) * (*snode)->nopterons + (snb - (*snode)->opterons));

			foreach_node(dnode) {
				for (Opteron **dnb = &(*dnode)->opterons[0]; dnb < &(*dnode)->opterons[(*dnode)->nopterons]; dnb++) {
					const unsigned soffset = snb - (*snode)->opterons;
					const unsigned doffset = dnb - (*dnode)->opterons;
					uint8_t dist;

					if (*snode == *dnode) {
						if (*snb == *dnb)
							dist = 10;
						else
							dist = odist[soffset + doffset * (*snode)->nopterons];
					} else {
						xassert(router->dist[snode - nodes][dnode - nodes]);
						dist = 70 + (router->dist[snode - nodes][dnode - nodes] + router->dist[dnode - nodes][snode - nodes]) * 10;

						// add latency from egress Numachip NUMA node
						dist += odist[(*snode)->neigh_ht + doffset * (*snode)->nopterons];
					}

					if (options->debug.acpi)
						printf(" %3u", dist);
					slit.append((const char *)&dist, sizeof(dist));
				}
			}
			if (options->debug.acpi)
				printf("\n");
			slit.increment64(n_offset);
		}
	}

	// ensure BIOS-provided DSDT uses ACPI revision 2, to 64-bit intergers are accepted
	acpi_sdt *dsdt = acpi->find_sdt("DSDT");
	if (dsdt->revision < 2) {
		dsdt->revision = 2;
		dsdt->checksum += ACPI::checksum((const char *)dsdt, dsdt->len);
	}

	uint32_t extra_len;
	const char *extra = remote_aml(&extra_len);
	AcpiTable ssdt("SSDT", 2);
	ssdt.append(extra, extra_len);

	acpi->allocate((sizeof(struct acpi_sdt) + 64) * 5 + mcfg.used + apic.used + srat.used + slit.used + ssdt.used);
	acpi->replace(mcfg);
	acpi->replace(apic);
	acpi->replace(srat);
	acpi->replace(slit);
	if (options->remote_io)
		acpi->add(ssdt);
	acpi->check();
}

static void clear_dram(void)
{
	printf("Clearing DRAM");
	lib::critical_enter();

	foreach_node(node) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_start();
	}

	foreach_node(node) {
		unsigned start = node == nodes ? 1 : 0;
		for (Opteron **nb = &(*node)->opterons[start]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_clear_wait();
	}

	lib::critical_leave();
	printf("\n");

	// avoid noise when tracing
	if (options->tracing)
		return;

	foreach_node(node)
		for (Opteron **nb = &(*node)->opterons[0]; nb < &(*node)->opterons[(*node)->nopterons]; nb++)
			(*nb)->dram_scrub_enable();
}

static void monitor()
{
	uint64_t stats[MAX_NODE];
	uint8_t coretemp[MAX_NODE];

	while (1) {
		for (unsigned i = 0; i < 12; i++) {
			for (unsigned n = 0; n < config->nnodes; n++) {
				Numachip2::check(config->nodes[n].id, local_node->numachip->ht); // FIXME: assumed same HT

				for (unsigned ht = 0; ht < local_node->numachip->ht; ht++)
					Opteron::check(config->nodes[n].id, ht);
			}

			lib::udelay(300000);
		}

		printf("%02d:%02d:%02d",
			lib::rtc_read(RTC_HOURS), lib::rtc_read(RTC_MINUTES), lib::rtc_read(RTC_SECONDS));

		for (unsigned n = 0; n < config->nnodes; n++) {
			if (config->partitions[config->nodes[n].partition].monitor)
				continue;

			// FIXME: assuming same HT
			stats[n] = Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PIU_UTIL)
				| ((uint64_t)Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PIU_UTIL+4) << 32);
			coretemp[n] = (Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::IMG_PROP_TEMP) & 0xff) - 128;
			printf(" %03u", config->nodes[n].id);
		}

		printf("\nHREQ/16 ");

		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", stats[n] & 0xff);

		printf("\nHPRB/16 ");

		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", (stats[n] >> 16) & 0xff);

		printf("\nPREQ/8  ");

		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", (stats[n] >> 24) & 0xff);

		printf("\nPPRB/8  ");
		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", (stats[n] >> 8) & 0xff);

		printf("\nRMPE/16 ");
		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", (stats[n] >> 32) & 0xff);

		printf("\nLMPE/16 ");
		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3llu", (stats[n] >> 40) & 0xff);

		printf("\ncoretemp");
		for (unsigned n = 0; n < config->nnodes; n++)
			if (!config->partitions[config->nodes[n].partition].monitor)
				printf(" %3u", coretemp[n]);

		if (options->debug.monitor) {
			printf("\n");

			// assume all control words are the same
			uint32_t ctrl = Numachip2::read32(config->nodes[0].id, local_node->numachip->ht, Numachip2::PE_CTRL);

			for (unsigned n = 0; n < config->nnodes; n++) {
				if (config->partitions[config->nodes[n].partition].monitor)
					continue;

				for (unsigned pe = 0; pe < Numachip2::PE_UNITS; pe++) {
					for (unsigned c = 0; c < Numachip2::PE_CNTXTS; c++) {
						Numachip2::write32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PE_CTRL + pe * Numachip2::PE_OFFSET, ctrl | (c << 20));
						uint32_t stat = Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PE_CNTXT_STATUS + pe * Numachip2::PE_OFFSET);

						if ((stat >> 10) & 7) { // skip free contexts
							uint64_t addr = Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PE_CNTXT_ADDR + pe * Numachip2::PE_OFFSET)
								| ((uint64_t)Numachip2::read32(config->nodes[n].id, local_node->numachip->ht, Numachip2::PE_CNTXT_ADDR+4 + pe * Numachip2::PE_OFFSET) << 32);
							printf(" %03x%c%010llx", config->nodes[n].id, pe ? 'L' : 'R', addr);
						}
					}
				}
			}
		}

		printf("\n\n");
	}
}

static void finished(const char *label)
{
	check();

	if (options->boot_wait)
		lib::wait_key("Press enter to boot");

	if (config->partitions[config->local_node->partition].monitor)
		monitor();

	// disable remote timeout on bootable nodes
	foreach_node(node)
		(*node)->numachip->finished();

	// reenable wrap32
	uint64_t msr = lib::rdmsr(MSR_HWCR);
	lib::wrmsr(MSR_HWCR, msr & ~(1ULL << 17));

	if (config->partitions[config->local_node->partition].unified)
		printf("Partition %d unification", config->local_node->partition + 1);
	else
		printf("Observer setup");

	printf(" succeeded; executing syslinux label %s\n", label);
	os->exec(label);
}

void caches(const bool enable)
{
	if (enable)
		enable_cache();

	trampoline_sem_init(acpi->napics - 1);
	for (unsigned n = 1; n < acpi->napics; n++)
		boot_core_host(acpi->apics[n], enable ? VECTOR_CACHE_ENABLE : VECTOR_CACHE_DISABLE);
	if (trampoline_sem_wait())
		fatal("%u cores did not complete requested operation", trampoline_sem_getvalue());

	if (!enable) {
		if (Opteron::family >= 0x15) {
			// ensure CombineCr0Cd is set on fam15h
			uint64_t msr = lib::rdmsr(MSR_CU_CFG3) | (1ULL << 49);
			lib::wrmsr(MSR_CU_CFG3, msr);
		}
		disable_cache();
	}
}

static void wait_status(void)
{
	printf("\nWaiting for");

	for (unsigned n = 0; n < config->nnodes; n++) {
		if (&config->nodes[n] == config->local_node) /* Self */
			continue;

		if (!config->nodes[n].seen)
			printf(" %s", pr_node(config->nodes[n].id));
	}
}

#define NODE_SYNC_STATES(state)			\
	state(CMD_STARTUP)			\
	state(RSP_SLAVE_READY)			\
	state(CMD_RESET_FABRIC)			\
	state(RSP_RESET_OK)			\
	state(CMD_TRAIN_PHYS)			\
	state(RSP_PHY_TRAINED)			\
	state(RSP_PHY_NOT_TRAINED)		\
	state(CMD_SETUP_ROUTING)		\
	state(RSP_ROUTING_OK)			\
	state(CMD_LOAD_FABRIC)			\
	state(RSP_FABRIC_READY)			\
	state(CMD_CHECK_FABRIC)			\
	state(RSP_FABRIC_OK)			\
	state(RSP_FABRIC_NOT_OK)		\
	state(CMD_WARM_RESET)			\
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
			lib::udelay(500000);
			local_node->numachip->fabric_reset();
			*rstate = RSP_RESET_OK;
			return 1;
		case CMD_TRAIN_PHYS:
			lib::udelay(500000);
			if (local_node->numachip->fabric_train())
				*rstate = RSP_PHY_TRAINED;
			else
				*rstate = RSP_PHY_NOT_TRAINED;
			return 1;
		case CMD_SETUP_ROUTING:
			lib::udelay(500000);
			local_node->numachip->fabric_routing();
			*rstate = RSP_ROUTING_OK;
			return 1;
		case CMD_LOAD_FABRIC:
			lib::udelay(500000);
			*rstate = RSP_FABRIC_READY;
			printf("Early fabric validation");

			for (unsigned i = 0; i < 3000000; i++) {
				if (i % 200000 == 0) printf(".");
				for (unsigned n = 0; n < config->nnodes; n++) {
					uint32_t vendev = lib::mcfg_read32(config->nodes[n].id, 0, 24 + local_node->numachip->ht, 0, 0);
					if (vendev != Numachip2::VENDEV_NC2) // stop testing to prevent collateral
						return 1;
				}
			}
			printf("\n");
			return 1;
		case CMD_CHECK_FABRIC:
			lib::udelay(500000);
			*rstate = local_node->check() ? RSP_FABRIC_NOT_OK : RSP_FABRIC_OK;
			printf("Fabric %s\n", *rstate == RSP_FABRIC_OK ? "validates" : "failed validation");
			return 1;
		case CMD_WARM_RESET:
			printf(BANNER "Warm-booting to clear error...\n");
			lib::udelay(500000);
			Opteron::platform_reset_warm();
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
	bool do_restart = 0, do_reboot = 0;
	enum node_state waitfor, own_state;
	uint32_t last_cmd = ~0;
	char buf[UDP_MAXLEN];
	struct state_bcast *rsp = (struct state_bcast *)buf;

	os->udp_open();

	memset(&cmd, 0, sizeof(cmd));
	cmd.sig = UDP_SIG;
	cmd.state = CMD_STARTUP;
	memcpy(cmd.mac, config->local_node->mac, 6);
	cmd.sci = config->local_node->id;
	cmd.tid = 0; /* Must match initial rsp.tid for RSP_SLAVE_READY */
	waitfor = RSP_SLAVE_READY;
	printf("Waiting for %u servers", config->nnodes - 1);
	unsigned count = 0, backoff = 1, last_stat = 150, progress = 0;

	while (1) {
		uint32_t ip = 0;
		size_t len;

		if (++count >= backoff) {
			os->udp_write(&cmd, sizeof(cmd), 0xffffffff);

			lib::udelay(100 * backoff);
			last_stat += backoff;

			if (backoff < 32)
				backoff *= 2;

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
				if (last_stat > 200) {
					last_stat = 0;
					wait_status();
				} else if (last_stat / 8 != progress) {
					printf(".");
					progress = last_stat / 8;
				}

				if (!len)
					continue;
			}
		} else
			len = 0;

		if (len >= sizeof(rsp) && rsp->sig == UDP_SIG) {
#if SYNC_DEBUG
			printf("Got rsp packet from %d.%d.%d.%d (%02x:%02x:%02x:%02x:%02x:%02x) (state %s, sciid %03x, tid %d)\n",
			       ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
			       rsp->mac[0], rsp->mac[1], rsp->mac[2],
			       rsp->mac[3], rsp->mac[4], rsp->mac[5],
			       (rsp->state > RSP_NONE) ? "UNKNOWNN" : node_state_name[rsp->state], rsp->sci, rsp->tid);
#endif
			for (unsigned n = 0; n < config->nnodes; n++) {
				if (memcmp(&config->nodes[n].mac, rsp->mac, 6) == 0) {
					if ((rsp->state == waitfor) && (rsp->tid == cmd.tid)) {
						config->nodes[n].seen = 1;
					} else if (rsp->state == RSP_PHY_NOT_TRAINED) {
						if (!config->nodes[n].seen) {
							printf("\n%s failed with %s; restarting synchronisation\n",
							       pr_node(config->nodes[n].id), node_state_name[rsp->state]);
							do_restart = 1;
							config->nodes[n].seen = 1;
						}
					} else if (rsp->state == RSP_FABRIC_NOT_OK) {
						do_reboot = 1;
					} else if (rsp->state == RSP_ERROR) {
						char name[32];
						snprintf(name, sizeof(name), "\n%s", pr_node(config->nodes[n].id));
						error_remote(rsp->sci, name, ip, (char *)rsp + sizeof(struct state_bcast));
					}
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
				if (cmd.state == CMD_CHECK_FABRIC) {
					cmd.state = CMD_WARM_RESET;
				} else {
					cmd.state = CMD_RESET_FABRIC;
					waitfor = RSP_RESET_OK;
				}
				do_restart = 0;
			} else if (do_reboot) {
				cmd.state = CMD_WARM_RESET;
				waitfor = RSP_NONE;
				do_reboot = 0;
			} else if (cmd.state == CMD_STARTUP) {
				/* Skip over resetting fabric, as that's just if training fails */
				cmd.state = CMD_TRAIN_PHYS;
				waitfor = RSP_PHY_TRAINED;
			} else if (cmd.state == CMD_TRAIN_PHYS) {
				cmd.state = CMD_SETUP_ROUTING;
				waitfor = RSP_ROUTING_OK;
			} else if (cmd.state == CMD_RESET_FABRIC) {
				/* When invoked, continue at fabric training */
				cmd.state = CMD_TRAIN_PHYS;
				waitfor = RSP_PHY_TRAINED;
			} else if (cmd.state == CMD_SETUP_ROUTING) {
				cmd.state = CMD_LOAD_FABRIC;
				waitfor = RSP_FABRIC_READY;
			} else if (cmd.state == CMD_LOAD_FABRIC) {
				cmd.state = CMD_CHECK_FABRIC;
				waitfor = RSP_FABRIC_OK;
			} else if (cmd.state == CMD_CHECK_FABRIC) {
				cmd.state = CMD_CONTINUE;
				waitfor = RSP_NONE;
			}

			/* Clear seen flag */
			for (unsigned n = 0; n < config->nnodes; n++)
				config->nodes[n].seen = 0;

			cmd.tid++;
			count = 0;
			backoff = 1;
			printf("\nIssuing %s; expecting %s\n",
			       node_state_name[cmd.state], node_state_name[waitfor]);
		}
	}

	printf("\n");
}

static void wait_for_master(void)
{
	struct state_bcast rsp, cmd;
	int count, backoff;
	int go_ahead = 0;
	uint32_t last_cmd = ~0;
	uint32_t ip;
	enum node_state last_state = RSP_NONE;

	os->udp_open();

	memset(&rsp, 0, sizeof(rsp));
	rsp.sig = UDP_SIG;
	rsp.state = RSP_SLAVE_READY;
	memcpy(rsp.mac, config->local_node->mac, sizeof(config->local_node->mac));
	rsp.sci = config->local_node->id;
	rsp.tid = 0;

	count = 0;
	backoff = 1;

	while (!go_ahead) {
		if (++count >= backoff) {
			if (last_state != rsp.state) {
				printf("Replying with %s", node_state_name[rsp.state]);
				last_state = rsp.state;
			} else
				printf(".");
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
#if SYNC_DEBUG
			printf("Got cmd packet from %d.%d.%d.%d (%02x:%02x:%02x:%02x:%02x:%02x) (state %s, sciid %03x, tid %d)\n",
			       ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
			       cmd.mac[0], cmd.mac[1], cmd.mac[2],
			       cmd.mac[3], cmd.mac[4], cmd.mac[5],
			       (cmd.state > RSP_NONE) ? "UNKNOWNN" : node_state_name[cmd.state], cmd.sci, cmd.tid);
#endif
			if (memcmp(config->nodes[0].mac, cmd.mac, 6) == 0) {
				if (cmd.tid == last_cmd) {
					/* Ignoring seen command */
					continue;
				}

				last_cmd = cmd.tid;
				count = 0;
				backoff = 1;

				if (cmd.state != CMD_STARTUP)
					printf("\n");
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
#ifdef DEBUG
static void test_map(void)
{
	const uint64_t stride = 4ULL << 10;

	printf("Testing memory map");
	for (uint64_t pos = 0; pos < dram_top + stride * 16; pos += stride)
		if (pos < Opteron::HT_BASE || pos >= Opteron::HT_LIMIT)
			lib::mem_read32(pos);

	printf("\n");
}
#endif
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
	router = new Router();

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

	if (options->test_manufacture)
		config = new Config();
	else
		config = new Config(options->config_filename);

	local_node = new Node(config->local_node, (sci_t)config->master->id);

	// ensure low-memory is reserved
	e820->add(0x83000, 0x1c00, E820::RESERVED);

	// ensure Numachip2 MMIO range is reserved for all cases
	e820->add(Numachip2::LOC_BASE, Numachip2::LOC_LIM - Numachip2::LOC_BASE + 1, E820::RESERVED);

	if (options->init_only) {
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			if (options->tracing)
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);
		finished(config->partitions[config->local_node->partition].label);
	}

	// initialize SPI/SPD, DRAM, NODEID etc
	local_node->numachip->late_init();

	// add global MCFG maps
	for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
		(*nb)->mmiomap->set(8, Numachip2::MCFG_BASE, Numachip2::MCFG_LIM, local_node->numachip->ht, 0);

	// reserve HT decode and MCFG address range so Linux accepts it
	e820->add(Opteron::HT_BASE, Opteron::HT_LIMIT - Opteron::HT_BASE, E820::RESERVED);
	e820->add(Numachip2::MCFG_BASE, Numachip2::MCFG_LIM - Numachip2::MCFG_BASE + 1, E820::RESERVED);

	// setup local MCFG access
	const uint64_t mcfg = Numachip2::MCFG_BASE | ((uint64_t)config->local_node->id << 28) | 0x21;
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

	if (options->test_manufacture) {
		int i = 0;
		for (i = 10; i > 0; i--)
			if (!local_node->numachip->fabric_train())
				break;
		if (i==0)
			printf("MANUFACTURE TEST PASSED\n");
		else
			printf("MANUFACTURE TEST FAILED\n");
		halt();
	}

	if (config->partitions[config->local_node->partition].unified) {
		for (unsigned n = 0; n < config->nnodes; n++)
			if (config->nodes[n].partition == config->local_node->partition)
				nnodes++;
	} else
		nnodes = 1;

	nodes = (Node **)zalloc(sizeof(void *) * nnodes);
	xassert(nodes);
	nodes[0] = local_node;

	if (!config->partitions[config->local_node->partition].unified) {
		for (Opteron *const *nb = &local_node->opterons[0]; nb < &local_node->opterons[local_node->nopterons]; nb++)
			if (options->tracing)
				e820->add((*nb)->trace_base, (*nb)->trace_limit - (*nb)->trace_base + 1, E820::RESERVED);

		setup_cores_observer();
		setup_info();
		finished(config->partitions[config->local_node->partition].label);
	}

	// slaves
	if (!config->local_node->master) {
		os->cleanup();
		if (!options->handover_acpi) // handover performed earlier
			acpi->handover();
		handover_legacy(local_node->config->id);

		// hide SMBus for remote-IO
//		lib::pmio_write8(0xba, 64); // FIXME: check?

		// read from master after mapped
		printf("Waiting for %s", pr_node(config->master->id));
		local_node->numachip->write32(Numachip2::INFO + 4, (uint32_t)local_node);
		local_node->numachip->write32(Numachip2::INFO, 1 << 29);

		// wait for 'ready'
		while (local_node->numachip->read32(Numachip2::INFO) != 3 << 29) {
			local_node->check();
			cpu_relax();
		}

		printf("\n");
		local_node->iohub->smi_disable();
		pci_setup(local_node->config->id);

		// clear BSP flag
		uint64_t val = lib::rdmsr(MSR_APIC_BAR);
		lib::wrmsr(MSR_APIC_BAR, val & ~(1ULL << 8));

		// disable XT-PIC
		lib::disable_xtpic();

		printf(BANNER "This server %s is part of a %u-server NumaConnect2 system\n"
		       "Refer to the console on %s", pr_node(config->master->id),
		       nnodes, pr_node(config->master->id));

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

	printf("Servers ready:\n");

	unsigned pos = 1;

	// FIXME: nodes must be added in the expected sequence
	for (unsigned n = 0; n < config->nnodes; n++) {
		// skip nodes not in this partition
		if (config->nodes[n].added || config->nodes[n].partition != config->local_node->partition)
			continue;

		ht_t ht;
		while (1) {
			ht = Numachip2::probe_slave(config->nodes[n].id);
			if (ht)
				break;
			cpu_relax();
		}

		nodes[pos++] = new Node(&config->nodes[n], ht);
		config->nodes[n].added = 1;
	}

	scan();
	pci_realloc();
	remap();

	if (options->debug.maps) {
		printf("\nDRAM maps:\n");
		foreach_node(node) {
			(*node)->opterons[0]->drammap.print();
			(*node)->numachip->drammap.print();
		}

		printf("\nMMIO maps:\n");
		foreach_node(node) {
			(*node)->opterons[0]->mmiomap->print();
			(*node)->numachip->mmiomap.print();
		}
	}

	setup_apicids();
	copy_inherit();
	if (options->tracing)
		setup_gsm();
	setup_info();
	acpi_tables();
	tracing_arm();
	enable_coherency();
	setup_cores();
#ifdef DEBUG
	test_map();
#endif
	if (!options->fastboot)
		test_cores();
	clear_dram();
	finished(config->partitions[config->local_node->partition].label);
}
