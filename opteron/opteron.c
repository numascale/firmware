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
	/* Enable CF8 extended access, we use it extensively */
	uint64_t val = rdmsr(MSR_NB_CFG);
	wrmsr(MSR_NB_CFG, val | (1ULL << 46));

	/* Disable 32-bit address wrapping to allow 64-bit access in 32-bit code */
	val = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, val | (1ULL << 17));
}

Opteron::~Opteron(void)
{
	/* Restore 32-bit only access */
	uint64_t val = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, val & ~(1ULL << 17));
}
