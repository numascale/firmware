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

#define VECTOR_TRAMPOLINE 1
#define VECTOR_PROBEFILTER_EARLY_f10 2
#define VECTOR_PROBEFILTER_EARLY_f15 3
#define VECTOR_ENABLE_CACHE 4
#define VECTOR_READBACK_MSR 5
#define VECTOR_READBACK_APIC 6

#define APIC_VECTOR_MASKED 0x00010000
#define MSR_VECTOR_MASKED 0xc010000001000000
#define E820_MAP_MAX 2560

#ifndef __ASSEMBLER__
#define IMPORT_RELOCATED(sym) extern volatile uint8_t sym ## _relocate
#define REL8(sym) ((uint8_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL16(sym) ((uint16_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL32(sym) ((uint32_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))
#define REL64(sym) ((uint64_t *)((volatile uint8_t *)asm_relocated + ((volatile uint8_t *)&sym ## _relocate - (volatile uint8_t *)&asm_relocate_start)))

extern unsigned char asm_relocate_start, asm_relocate_end;
extern char *asm_relocated;

IMPORT_RELOCATED(init_dispatch);
IMPORT_RELOCATED(cpu_status);
IMPORT_RELOCATED(cpu_apic_renumber);
IMPORT_RELOCATED(cpu_apic_hi);
IMPORT_RELOCATED(msr_mcfg);
IMPORT_RELOCATED(msr_topmem);
IMPORT_RELOCATED(msr_topmem2);
IMPORT_RELOCATED(msr_cpuwdt);
IMPORT_RELOCATED(mtrr_default);
IMPORT_RELOCATED(fixed_mtrr_regs);
IMPORT_RELOCATED(new_mtrr_fixed);
IMPORT_RELOCATED(new_mtrr_var_base);
IMPORT_RELOCATED(new_mtrr_var_mask);
IMPORT_RELOCATED(msr_syscfg);
IMPORT_RELOCATED(msr_topmem_rem);
IMPORT_RELOCATED(msr_smm_base_rem);
IMPORT_RELOCATED(msr_hwcr);
IMPORT_RELOCATED(msr_mc4_misc0);
IMPORT_RELOCATED(msr_mc4_misc1);
IMPORT_RELOCATED(msr_mc4_misc2);
IMPORT_RELOCATED(msr_osvw_id_len);
IMPORT_RELOCATED(msr_osvw_status);
IMPORT_RELOCATED(msr_int_halt);
IMPORT_RELOCATED(msr_readback);
IMPORT_RELOCATED(apic_offset);
IMPORT_RELOCATED(apic_readback);
IMPORT_RELOCATED(msr_lscfg);
IMPORT_RELOCATED(msr_cucfg2);
IMPORT_RELOCATED(old_int15_vec);
IMPORT_RELOCATED(new_e820_len);
IMPORT_RELOCATED(new_e820_map);
IMPORT_RELOCATED(new_e820_handler);
IMPORT_RELOCATED(new_mpfp);
IMPORT_RELOCATED(new_mptable);
#endif
