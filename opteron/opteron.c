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
#include "../library/base.h"
#include "../library/access.h"
#include "defs.h"

Opteron::Opteron(void)
{
	/* Enable CF8 extended access */
	uint64_t msr = rdmsr(MSR_NB_CFG);
	wrmsr(MSR_NB_CFG, msr | (1ULL << 46));

	/* Disable 32-bit address wrapping to allow 64-bit access in 32-bit code */
	msr = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, msr | (1ULL << 17));

	/* Detect processor family */
	uint32_t val = cht_readl(0, FUNC3_MISC, 0xfc);
	family = ((val >> 20) & 0xf) + ((val >> 8) & 0xf);
	if (family >= 0x15) {
		uint32_t val = cht_readl(0, FUNC5_EXTD, 0x160);
		tsc_mhz = 200 * (((val >> 1) & 0x1f) + 4) / (1 + ((val >> 7) & 1));
	} else {
		uint32_t val = cht_readl(0, FUNC3_MISC, 0xd4);
		uint64_t val6 = rdmsr(MSR_COFVID_STAT);
		tsc_mhz = 200 * ((val & 0x1f) + 4) / (1 + ((val6 >> 22) & 1));
	}

	printf("Family %xh Opteron with %dMHz NB TSC frequency\n", family, tsc_mhz);

	/* Detect IOH */
	ioh_vendev = extpci_readl(0, 0, 0, 0);
	assert(ioh_vendev != 0xffffffff);
}

Opteron::~Opteron(void)
{
	/* Restore 32-bit only access */
	uint64_t val = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, val & ~(1ULL << 17));
}
