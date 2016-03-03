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

#include "opteron.h"
#include "msrs.h"
#include "../bootloader.h"
#include "../library/base.h"
#include "../library/access.h"
#include "../library/utils.h"
#include "../platform/trampoline.h"
#include "../platform/options.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

// approximation before probing
uint32_t Opteron::tsc_mhz = 2200;
uint32_t Opteron::ioh_vendev;
uint8_t Opteron::mc_banks;
uint8_t Opteron::family;

void Opteron::check(void)
{
#ifdef LOCAL
	for (unsigned bank = 0; bank < mc_banks; bank++) {
		const uint32_t msr = MSR_MC0_STATUS + bank * 4;
		uint64_t val = lib::rdmsr(msr);
		if (!(val & (1ULL << 63)))
			continue;

		printf("%03x#%u MSR%08x=0x%" PRIx64 "\n", sci, ht, msr, val);
		lib::wrmsr(msr, 0);
	}
#endif
	for (unsigned link = 0; link < 4; link++) {
		uint32_t val = read32(LINK_CTRL + link * 0x20);
		if (val & 0x300)
			warning("%03x#%u link %u CRC error", sci, ht, link);

		if (val & 0x10)
			warning("%03x#%u link %u failure", sci, ht, link);

		val = read32(LINK_RETRY + link * 4);
		if (val & 0xffff1f00) {
			printf(COL_RED "%03x#%u link %u: %u retries", sci, ht, link, val >> 16);
			if (val & (1 << 11))
				printf(" DataCorruptOut");
			if (val & (1 << 11))
				printf(" InitFail");
			if (val & (1 << 10))
				printf(" StompedPktDet");
			if (val & (1 << 9))
				printf(" RetryCountRollover");
			printf(COL_DEFAULT "\n");
		}
	}

	uint64_t s = read64(MC_NB_STAT);
	if (s & (1ULL << 63)) {
		const char *sig[] = {
		  NULL, "CRC Error", "Sync Error", "Mst Abort", "Tgt Abort",
		  "GART Error", "RMW Error", "WDT Error", "ECC Error", NULL,
		  "Link Data Error", "Protocol Error", "NB Array Error",
		  "DRAM Parity Error", "Link Retry", "GART Table Walk Data Error",
		  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		  "L3 Cache Data Error", "L3 Cache Tag Error", "L3 Cache LRU Error",
		  "Probe Filter Error", "Compute Unit Data Error"};

		warning("%s on %03x#%u:", sig[(s >> 16) & 0x1f], sci, ht);
		printf("- ErrorCode=0x%" PRIx64 " ErrorCodeExt=0x%" PRIx64 " Syndrome=0x%" PRIx64 "\n",
		  s & 0xffff, (s >> 16) & 0xf, ((s >> 16) & 0xff00) | ((s >> 47) & 0xff));
		printf("- Link=%" PRIu64 " Scrub=%" PRIu64 " SubLink=%" PRIu64 " McaStatSubCache=%" PRIu64 "\n",
		  (s >> 26) & 0xf, (s >> 40) & 1, (s >> 41) & 1, (s >> 42) & 3);
		printf("- UECC=%" PRIu64 " CECC=%" PRIu64 " PCC=%" PRIu64 "\n",
		  (s >> 45) & 1, (s >> 46) & 1, (s >> 57) & 1);
		printf("- MiscV=%" PRIu64 " En=%" PRIu64 " UC=%" PRIu64 " Overflow=%" PRIu64 "\n",
		  (s >> 59) & 1, (s >> 60) & 1, (s >> 61) & 1, (s >> 62) & 1);

		if ((s >> 56) & 1) // ErrCoreIdVal
			printf(" ErrCoreId=%" PRIu64, (s >> 32) & 0xf);

		if ((s >> 58) & 1) // AddrV
			printf(" Address=0x%016" PRIx64, read64(MC_NB_ADDR));

		write64_split(MC_NB_ADDR, 0);
		write64_split(MC_NB_STAT, 0);
		printf("\n");
	}

	uint32_t v = read32(MC_NB_DRAM);
	if (v & 0xff) { // looks like bits 7:0 only are usable
		warning("DRAM machine check 0x%08x on %03x#%u", v, sci, ht);
		write32(MC_NB_DRAM, 0);
	}

	v = read32(MC_NB_LINK);
	if (v & 0xfff) {
		warning("HT Link machine check 0x%08x on %03x#%u", v, sci, ht);
		write32(MC_NB_LINK, 0);
	}

	v = read32(MC_NB_L3C);
	if (v & 0xfff) {
		warning("L3 Cache machine check 0x%08x on %03x#%u", v, sci, ht);
		write32(MC_NB_L3C, 0);
	}
}

void Opteron::disable_syncflood(void)
{
	warning_once("Disabling sync-flood");

	uint32_t val = read32(MC_NB_CONF);
	val &= ~(1 << 2);  // SyncOnUcEccEn: sync flood on uncorrectable ECC error enable
	val |= 1 << 3;     // SyncPktGenDis: sync packet generation disable
	val |= 1 << 4;     // SyncPktPropDis: sync packet propagation disable
	val &= ~(1 << 20); // SyncOnWDTEn: sync flood on watchdog timer error enable
	val &= ~(1 << 21); // SyncOnAnyErrEn: sync flood on any error enable
	val &= ~(1 << 30); // SyncOnDramAdrParErrEn: sync flood on DRAM address parity error enable
	write32(MC_NB_CONF, val);

	val = read32(MC_NB_CONF_EXT);
	val &= ~(1 << 1);  // SyncFloodOnUsPwDataErr: sync flood on upstream posted write data error
	val &= ~(1 << 6);  // SyncFloodOnDatErr
	val &= ~(1 << 7);  // SyncFloodOnTgtAbtErr
	val &= ~(1 << 8);  // SyncOnProtEn: sync flood on protocol error enable
	val &= ~(1 << 9);  // SyncOnUncNbAryEn: sync flood on uncorrectable NB array error enable
	val &= ~(1 << 20); // SyncFloodOnL3LeakErr: sync flood on L3 cache leak error enable
	val &= ~(1 << 21); // SyncFloodOnCpuLeakErr: sync flood on CPU leak error enable
	val &= ~(1 << 22); // SyncFloodOnTblWalkErr: sync flood on table walk error enable
	write32(MC_NB_CONF_EXT, val);

	for (unsigned l = 0; l < 4; l++) {
		// CrcFloodEn: Enable sync flood propagation upon link failure
		val = read32(LINK_CTRL + l * 0x20);
		write32(LINK_CTRL + l * 0x20, val & ~2);
	}
}

void Opteron::disable_nbwdt(void)
{
	warning_once("Disabling NorthBridge WatchDog Timer");

	uint32_t val = read32(MC_NB_CONF);
	val |= 1 << 8;
	write32(MC_NB_CONF, val);
}

uint64_t Opteron::read64(const reg_t reg) const
{
	return lib::mcfg_read64(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

uint32_t Opteron::read32(const reg_t reg) const
{
	return lib::mcfg_read32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff);
}

void Opteron::write64_split(const reg_t reg, const uint64_t val) const
{
	lib::mcfg_write64_split(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Opteron::write32(const reg_t reg, const uint32_t val) const
{
	lib::mcfg_write32(sci, 0, 24 + ht, reg >> 12, reg & 0xfff, val);
}

void Opteron::set32(const reg_t reg, const uint32_t mask) const
{
	uint32_t val = read32(reg);
	write32(reg, val | mask);
}

void Opteron::clear32(const reg_t reg, const uint32_t mask) const
{
	uint32_t val = read32(reg);
	write32(reg, val & ~mask);
}

void Opteron::clearset32(const reg_t reg, const uint32_t clearmask, const uint32_t setmask) const
{
	uint32_t val = read32(reg);
	write32(reg, (val & ~clearmask) | setmask);
}

void Opteron::prepare(void)
{
	// ensure MCEs aren't redirected into SMIs
	xassert(!lib::rdmsr(MSR_MCE_REDIR));

	// set McStatusWrEn in HWCR to allow adjustments later, enable monitor-wait in userspace
	uint64_t msr = lib::rdmsr(MSR_HWCR) | (1ULL << 18) | (1ULL << 10);
	// disable 32-bit address wrapping to allow 64-bit access in 32-bit code only on BSC
	lib::wrmsr(MSR_HWCR, msr | (1ULL << 17));
	push_msr(MSR_HWCR, msr);

	msr = lib::rdmsr(MSR_NB_CFG);
	msr |= 1ULL << 46; // enable CF8 extended configuration cycles
#ifdef MEASURE
	msr |= 1ULL << 50; // allow natural ordering for IO read responses
#endif
	lib::wrmsr(MSR_NB_CFG, msr);
	push_msr(MSR_NB_CFG, msr);

	// detect processor family
	uint32_t val = lib::cht_read32(0, NB_CPUID);
	family = ((val >> 20) & 0xf) + ((val >> 8) & 0xf);

	if (family == 0x10) {
		// ERRATA #N28: Disable HT Lock mechanism on Fam10h
		// AMD Email dated 31.05.2011 :
		// There is a switch that can help with these high contention issues,
		// but it isn't "productized" due to a very rare potential for live lock if turned on.
		// Given that HUGE caveat, here is the information that I got from a good source:
		// LSCFG[44] =1 will disable it. MSR number is C001_1020
		msr = lib::rdmsr(MSR_LSCFG) | (1ULL << 44);
		lib::wrmsr(MSR_LSCFG, msr);
		push_msr(MSR_LSCFG, msr);
	}

	// enable 64-bit config access
	msr = lib::rdmsr(MSR_CU_CFG2) | (1ULL << 50);

	// AMD Fam 15h Errata #572: Access to PCI Extended Configuration Space in SMM is Blocked
	// Suggested Workaround: BIOS should set MSRC001_102A[27] = 1b
	if (family >= 0x15)
		msr |= 1ULL << 27;
	lib::wrmsr(MSR_CU_CFG2, msr);
	push_msr(MSR_CU_CFG2, msr);

	// detect NB TSC frequency
	if (family >= 0x15) {
		val = lib::cht_read32(0, NB_PSTATE_0);
		tsc_mhz = 200 * (((val >> 1) & 0x1f) + 4) / (1 + ((val >> 7) & 1));
	} else {
		val = lib::cht_read32(0, CLK_CTRL_0);
		uint64_t val2 = lib::rdmsr(MSR_COFVID_STAT);
		tsc_mhz = 200 * ((val & 0x1f) + 4) / (1 + ((val2 >> 22) & 1));
	}

	printf("Family %xh Opteron with %dMHz NB TSC frequency\n", family, tsc_mhz);

	// check number of MCA banks
	mc_banks = lib::rdmsr(MSR_MC_CAP) & 0xff;

	// disable core WDT
	if (options->debug.nowdt) {
		lib::wrmsr(MSR_CPUWDT, 0);
		push_msr(MSR_CPUWDT, 0);
	}
}

void Opteron::dram_scrub_disable(void)
{
	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	// disable DRAM scrubbers
	scrub = read32(SCRUB_RATE_CTRL);
	write32(SCRUB_RATE_CTRL, scrub & ~0x1f);
	lib::udelay(40); // allow outstanding scrub requests to finish

	clear32(SCRUB_ADDR_LOW, 1);

	// disable DRAM stutter scrub
	uint32_t val = read32(CLK_CTRL_0);
	write32(CLK_CTRL_0, val & ~(1 << 15));
}

void Opteron::dram_scrub_enable(void)
{
	uint32_t redir = read32(SCRUB_ADDR_LOW) & 1;
	write32(SCRUB_ADDR_LOW, dram_base | redir);
	write32(SCRUB_ADDR_HIGH, dram_base >> 32);

	/* Fam15h: Accesses to this register must first set F1x10C [DctCfgSel]=0;
	   Accesses to this register with F1x10C [DctCfgSel]=1 are undefined;
	   See erratum 505 */
	if (family >= 0x15)
		write32(DCT_CONF_SEL, 0);

	write32(SCRUB_RATE_CTRL, scrub);
	set32(SCRUB_ADDR_LOW, 1);

	// enable DRAM stutter scrub
	uint32_t val = read32(CLK_CTRL_0);
	write32(CLK_CTRL_0, val | (1 << 15));
}

void Opteron::init(void)
{
	uint32_t val = read32(CLK_CTRL_0);
	if (val & (1 << 13))
		fatal("Please disable C1E support in the BIOS");

	if (family >= 0x15)
		mmiomap = new MmioMap15(*this);
	else
		mmiomap = new MmioMap10(*this);

	ioh_ht = (read32(HT_NODE_ID) >> 8) & 7;
	ioh_link = (read32(UNIT_ID) >> 8) & 7; // only valid for NB with IOH link

	unsigned controllers = family >= 0x15 ? 2 : 1;
	// check for failed DIMMs and no per-NUMA-node memory
	for (unsigned dct = 0; dct < controllers; dct++) {
		if (family >= 0x15)
			write32(DCT_CONF_SEL, dct);
		unsigned en = 0;

		for (unsigned dimm = 0; dimm < 8; dimm++) {
			val = read32(DRAM_CS_BASE + dimm * 4);
			if (val & (1 << 2)) {
				error("Failed DIMM detected on %03x#%u; performance will be degraded", sci, ht);
				lib::wait_key("");
			}
			en += val & 1;
		}

		if (!en) {
			error("No DRAM available on %03x#%u DCT%u; performance will be degraded", sci, ht, dct);
			lib::wait_key("");
		}
	}

	// detect amount of memory
	dram_base = (uint64_t)(read32(DRAM_BASE) & 0x1fffff) << 27;
	uint64_t dram_limit = ((uint64_t)(read32(DRAM_LIMIT) & 0x1fffff) << 27) | 0x7ffffff;
	dram_size = dram_limit - dram_base + 1;

	// if slave, subtract and disable MMIO hole
	if (!local) {
		val = read32(DRAM_HOLE);
		if (val & 1) {
			dram_size -= (val & 0xff00) << (23 - 7);
			write32(DRAM_HOLE, val & ~0xff81);
		}
	}

	// round down DRAM size to 2GB to prevent holes with <2GB alignment for kernel
	dram_size &= ~((2ULL << 30) - 1);

	// set up buffer for HT tracing
	if (options->tracing) {
		trace_base = dram_base + dram_size - options->tracing;
		trace_limit = trace_base + options->tracing - 1;
		tracing_arm();
	} else
		tracing_disable();

	// enable reporting of WatchDog error through MCA
	val = read32(MC_NB_CTRL);
	write32(MC_NB_CTRL, val | (1 << 12));

	// upon WDT, use address field for other decoding
	if (options->debug.wdtinfo)
		clear32(MC_NB_CONF_EXT, 1 << 24);

	if (options->debug.northbridge)
		disable_syncflood();

	if (options->debug.nowdt)
		disable_nbwdt();

	// detect number of cores
	cores = 1;
	if (family < 0x15) {
		val = read32(LINK_TRANS_CTRL);
		if (val & 0x20)
			cores++; // Cpu1En

		val = read32(EXT_LINK_TRANS_CTRL);
		for (int i = 0; i <= 3; i++)
			if (val & (1 << i))
				cores++;
	} else {
		val = read32(NB_CAP_2);
		cores += val & 0xff;

		val = read32(DOWNCORE_CTRL);
		while (val) {
			if (val & 1)
				cores--;
			val >>= 1;
		}
	}

	// disable certain sources of SMI
	val = read32(ONLN_SPARE_CTRL);
	if (((val >> 14) & 3) == 2) {
		printf("- disabling ECC error SMI on HT#%d\n", ht);
		write32(ONLN_SPARE_CTRL, val & ~(3 << 14));
	}

	val = read32(ONLN_SPARE_CTRL);
	if (((val >> 12) & 3) == 2) {
		printf("- disabling online DIMM swap complete SMI on HT#%d\n", ht);
		write32(ONLN_SPARE_CTRL, val & ~(3 << 12));
	}

	val = read32(PROBEFILTER_CTRL);
	if (((val >> 22) & 3) == 2) {
		printf("- disabling probe filter error SMI on HT#%d\n", ht);
		write32(PROBEFILTER_CTRL, val & ~(3 << 22));
	}

	dram_scrub_disable();
}

Opteron::Opteron(const sci_t _sci, const ht_t _ht, const bool _local):
  local(_local), sci(_sci), ht(_ht), drammap(*this)
{
	init();

	if (!local)
		return;

	// FIXME set DisOrderRdRsp

	uint32_t val = read32(LINK_TRANS_CTRL);
	val |= (1 << 25) | (1 << 18) | (1 << 17); // set CHtExtAddrEn, ApicExtId, ApicExtBrdCst
	val &= ~(3 << 21); // disable downstream NP request limit "to avoid DMA Deadlock" (SR5690 Programming Requirements p5-5)
	write32(LINK_TRANS_CTRL, val);

	if (family >= 0x15) {
		// due to HT fabric asymmetry, impact of NB P1 transitions can have performance impact,
		// particularly with suboptimal HT connectivity, so disable
		set32(NB_PSTATE_CTRL, 1 << 14);

		// prevent core idle system management messages
		set32(C_STATE_CTRL, 1 << 31);
	} else {
		// enable 128MB-granularity on extended MMIO maps
		val = read32(EXT_LINK_TRANS_CTRL);
		write32(EXT_LINK_TRANS_CTRL, (val & ~0x300) | 0x200);

		// clear all extended MMIO maps
		for (unsigned range = 8; range < mmiomap->ranges; range++)
			mmiomap->remove(range);
	}

	// enable Addr64BitEn on IO links
	for (int i = 0; i < 4; i++) {
		// skip coherent/disabled links
		val = read32(LINK_TYPE + i * 0x20);
		if (!(val & (1 << 2)))
			continue;

		val = read32(LINK_CTRL + i * 0x20);
		if (!(val & (1 << 15)))
			write32(LINK_CTRL + i * 0x20, val | (1 << 15));
	}

	// Numachip can't handle Coherent Prefetch Probes, required disabled for PF anyway
	val = read32(MCTL_EXT_CONF_LOW);
	write32(MCTL_EXT_CONF_LOW, val & ~(7 << 8)); /* CohPrefPrbLimit=000b */

	// In case Traffic distribution is enabled on 2 socket systems, we
	// need to disable it for Directed Probes. Ref email to AMD dated 4/28/2010
	val = read32(COH_LINK_TRAF_DIST);
	write32(COH_LINK_TRAF_DIST, val & ~0x1);
	// make sure coherent link pair traffic distribution is disabled
	xassert(!read32(COH_LINK_PAIR_DIST));

	// disable legacy GARTs
	for (uint16_t reg = GART_APER_BASE; reg <= GART_CACHE_CTRL; reg += 4)
		write32(reg, 0);

	if (options->debug.maps) {
		printf("DRAM ranges on %03x#%d:\n", sci, ht);
		for (unsigned range = 0; range < 8; range++)
			drammap.print(range);
		printf("MMIO ranges on %03x#%d:\n", sci, ht);
		for (unsigned range = 0; range < mmiomap->ranges; range++)
			mmiomap->print(range);
	}

	if (options->debug.northbridge)
		check();
}

void Opteron::dram_clear_start(void)
{
	set32(MCTL_CONF_HIGH, 3 << 12); // disable memory controller prefetch
	set32(MCTL_SEL_LOW, 1 << 3); // start memory clearing
}

void Opteron::dram_clear_wait(void)
{
	// poll until done indicated
	while (read32(MCTL_SEL_LOW) & (1 << 9))
		cpu_relax();

	clear32(MCTL_CONF_HIGH, 3 << 12); // reenable memory controller prefetch
}

void Opteron::discover(void)
{
	uint32_t links = read32(LINK_INIT_STATUS); // F0x1A0
	xassert(links & (1 << 31)); // ensure valid
	for (unsigned l = 0; l < 3; l++) {
		// skip connected links
		if (!(links & (1 << (1 + l * 2))))
			continue;

		printf("HT%u.%u ", ht, l);

		uint32_t value = read32(LINK_GLO_CTRL_EXT);
		set32(LINK_GLO_CTRL_EXT, (value | (1 << 8))); // set GlblLinkTrain[ConnDly]
		set32(LINK_EXT_CTRL + l * 4, 1); // set LinkTrain[Ganged]
		clear32(LINK_RETRY + l * 4, 1);  // adjust Retry Control[Retry Enable]
		const uint8_t freq = 0; // 200MHz freq
		clearset32(LINK_FREQ_REV + l * 0x20, 0x1f << 8, (freq & 15) << 8);
		clearset32(LINK_FREQ_EXT + l * 0x20, 1, freq >> 4);
		const uint8_t width = 0; // 8-bit width
		clearset32(LINK_CTRL + l * 0x20, (7 << 28) | (7 << 24), (width << 28) | (width << 24));

		clear32(LINK_CTRL + l * 0x20, 1 << 6); // clear Link Control[End Of Chain]
		clear32(LINK_CTRL + l * 0x20, 1 << 7); // clear Link Control[TXOff]

		local_node->iohub->ldtstop();

		// wait for InitComplete and not ConnPend
		uint32_t val;
		do {
			cpu_relax();
			val = read32(LINK_TYPE + l * 0x20);
		} while ((val & (1 << 4)) || (!(val & 1)));

		printf("%sconnected and %scoherent\n", (val & 1) ? "" : "not ", (val & 4) ? "non-" : "");
	}
}
