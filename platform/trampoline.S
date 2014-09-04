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

#include "../opteron/msrs.h"
#include "trampoline.h"

#define EXPORT(sym) .global sym ## _relocate; sym ## _relocate: sym:
#define SYM_OFFSET(sym) ((sym) - asm_relocate_start)
#define STATUS(val) movl $val, %cs:SYM_OFFSET(cpu_status)

	.code16
	.text
	.align 4096

	.global asm_relocate_start
asm_relocate_start:
EXPORT(init_dispatch)
	cli

	mov	%cs, %ax
	add	$SYM_OFFSET(stack_start) / 16, %ax
	mov	%ax, %ss
	mov	$(stack_end - stack_start), %sp

	movl %cs:SYM_OFFSET(cpu_status), %edx
	STATUS(0x9);
	cmpl $VECTOR_SETUP, %edx
	je setup
	cmpl $VECTOR_TEST_START, %edx
	je test_start
	cmpl $VECTOR_TEST_STOP, %edx
	je test_stop

	STATUS(0x10);
1:
	cli
	hlt
	jmp	1b

setup:
	// disable caching
	mov	%cr0, %eax
	or	$0x40000000, %eax
	mov	%eax, %cr0
	wbinvd

	// disable all MTRR entries
	mov	$MSR_MTRR_PHYS_MASK0, %ecx
1:
	xor	%eax, %eax
	xor	%edx, %edx
	wrmsr
	add	$2, %ecx
	cmp	$MSR_MTRR_PHYS_MASK7, %ecx
	jna	1b

	STATUS(0x30)

	// set fixed MTRRs
	mov	$SYM_OFFSET(fixed_mtrr_regs), %edi // 32-bit MSR to use
	mov	$SYM_OFFSET(mtrr_fixed), %esi  // 64-bit value to write
1:
	mov	%cs:(%edi), %ecx // MSR number
	add	$4, %edi

	cmp $0xffffffff, %ecx
	je break

	mov	%cs:(%esi), %eax // value[0]
	add	$4, %esi
	mov	%cs:(%esi), %edx // value[1]
	add	$4, %esi

	wrmsr
	jmp	1b

	STATUS(0x40)

break:
	// set variable MTRRs
	mov	$MSR_MTRR_PHYS_BASE0, %ecx
	mov	$SYM_OFFSET(mtrr_var_base), %esi
	mov	$SYM_OFFSET(mtrr_var_mask), %edi
1:
	mov	%cs:(%esi), %eax
	add	$4, %esi
	mov	%cs:(%esi), %edx
	add	$4, %esi
	wrmsr
	inc	%ecx

	mov	%cs:(%edi), %eax
	add	$4, %edi
	mov	%cs:(%edi), %edx
	add	$4, %edi
	wrmsr
	inc	%ecx

	cmp	$MSR_MTRR_PHYS_MASK7, %ecx
	jna	1b

	// reenable caching
	mov	%cr0, %eax
	and	$~0x40000000, %eax
	mov	%eax, %cr0

	// copy old TOP_MEM value, set global value
	mov	$MSR_TOPMEM, %ecx
	rdmsr
	mov	%eax, %cs:SYM_OFFSET(msr_topmem_rem)
	mov	%edx, %cs:SYM_OFFSET(msr_topmem_rem + 4)
	mov	%cs:SYM_OFFSET(msr_topmem), %eax
	mov	%cs:SYM_OFFSET(msr_topmem + 4), %edx
	wrmsr

	mov	%cs:SYM_OFFSET(msr_topmem2), %eax
	mov	%cs:SYM_OFFSET(msr_topmem2 + 4), %edx
	mov	$MSR_TOPMEM2, %ecx
	wrmsr

	mov	%cs:SYM_OFFSET(msr_cpuwdt), %eax
	mov	%cs:SYM_OFFSET(msr_cpuwdt + 4), %edx
	mov	$MSR_CPUWDT, %ecx
	wrmsr

	mov	%cs:SYM_OFFSET(msr_hwcr), %eax
	mov	%cs:SYM_OFFSET(msr_hwcr + 4), %edx
	mov	$MSR_HWCR, %ecx
	wrmsr

	STATUS(0x50)

	mov	%cs:SYM_OFFSET(msr_lscfg), %eax
	mov	%cs:SYM_OFFSET(msr_lscfg + 4), %edx
	mov	$MSR_LSCFG, %ecx
	wrmsr

	mov	%cs:SYM_OFFSET(msr_cucfg2), %eax
	mov	%cs:SYM_OFFSET(msr_cucfg2 + 4), %edx
	mov	$MSR_CU_CFG2, %ecx
	wrmsr

	STATUS(0x51)

#ifdef DEBUG
	mov	$SYM_OFFSET(cpu_cr), %si
	call	print_strz

	mov	%cr0, %eax
	call	print_hex32

	mov	%cr3, %eax
	call	print_hex32

	mov	%cr4, %eax
	call	print_hex32

	mov	$SYM_OFFSET(cpu_done1), %si
	call	print_strz
#endif
	STATUS(0x52)

	mov	$MSR_APIC_BAR, %ecx
	rdmsr
	push	%eax
	call	print_hex32

	mov	$0x70800, %eax
	mov	$MSR_APIC_BAR, %ecx
	wrmsr
	mov	$MSR_APIC_BAR, %ecx
	rdmsr
	call	print_hex32
	mov	$0x7000, %ax
	mov	%ax, %es
	mov	%es:0x20, %eax
	call	print_hex32
	mov	%es:0x30, %eax
	call	print_hex32

	STATUS(0x53)

	mov	%cs:SYM_OFFSET(cpu_apic_renumber), %al
	shl	$24, %eax
	mov	%eax, %es:0x20
	mov	%es:0x20, %eax
	call print_hex32

	STATUS(0x54)

	pop	%eax
	and	$~0x100, %eax	// clear BSP flag to let core accept INIT and STARTUP IPIs
	mov	$MSR_APIC_BAR, %ecx
	wrmsr
	rdmsr
	call print_hex32

	STATUS(0x60)

	mov	$MSR_NODE_ID, %ecx
	rdmsr

	/* "Bios scratch" given as [11:6], so we're limited to an
	   8-bit prefix here for the time being.  Ideally we want 8 bits,
	   and since all upper bits of this MSR appear to be r/w, we
	   could just take some liberties with the register. */
	and	$~0xfc0, %eax
	xor	%ebx, %ebx
	mov	%cs:SYM_OFFSET(cpu_apic_hi), %bl
	shl	$6, %ebx
	or	%ebx, %eax
	wrmsr

	mov	$MSR_MCFG, %ecx
	mov	%cs:SYM_OFFSET(msr_mcfg), %eax
	mov	%cs:SYM_OFFSET(msr_mcfg + 4), %edx
	wrmsr

	mov	$MSR_SMM_BASE, %ecx
	rdmsr
	mov	%eax, %cs:SYM_OFFSET(msr_smm_base_rem)
	mov	%edx, %cs:SYM_OFFSET(msr_smm_base_rem + 4)

	STATUS(0x70)
1:
	cli
	hlt
	jmp	1b

// seed passed in EBX
// random number returned in EAX
prng:
	push %edx
	mov $1103515245, %eax
	mul %ebx
	add $12345, %eax
	pop %edx
	ret

// address passed in EDX:EAX
// data returned in ECX
read32:
	mov $MSR_FS_BASE, %ecx
	wrmsr
	mov %fs:(0), %ecx
	ret

test_start:
	STATUS(0x80)
	mov $1, %ebx
1:
	call prng
	and $~63, %eax // align access
	mov $0x1, %edx // 4GB base
	call read32
	inc %ebx
	jmp 1b

test_stop:
	STATUS(0x90)
1:
	cli
	hlt
	jmp 1b

init_probefilter_early_f10:
	// disable cacheing
	mov	%cr0, %eax
	or	$0x40000000, %eax
	mov	%eax, %cr0
	wbinvd

	// enable probe filter support
	mov $MSR_CU_CFG2, %ecx
	rdmsr
	or	$(1 << (42 - 32)), %edx
	wrmsr // FIXME: read MSR

	STATUS(0)
1:
	cli
	hlt
	jmp	1b

init_probefilter_early_f15:
	// ensure CD bit is shared amongst cores
	mov $MSR_CU_CFG3, %ecx
	rdmsr
	or	$(1 << (49 - 32)), %edx
	wrmsr // FIXME: read MSR

	// disable cacheing
	mov	%cr0, %eax
	or	$0x40000000, %eax
	mov	%eax, %cr0
	wbinvd

	// enable probe filter support
	mov $MSR_CU_CFG2, %ecx
	rdmsr
	or	$(1 << (42 - 32)), %edx
	wrmsr // FIXME: read MSR

	STATUS(0)
1:
	cli
	hlt
	jmp	1b

init_enable_cache:
	// reenable caching
	mov	%cr0, %eax
	and	$~0x40000000, %eax
	mov	%eax, %cr0

	STATUS(0)
1:
	cli
	hlt
	jmp	1b

init_readback_msr:
	mov	%cs:SYM_OFFSET(msr_readback), %ecx
	rdmsr
	mov	%eax, %cs:SYM_OFFSET(msr_readback)
	mov	%edx, %cs:SYM_OFFSET(msr_readback + 4)

	STATUS(0)
1:
	cli
	hlt
	jmp	1b

init_readback_apic:
	mov $MSR_APIC_BAR, %ecx
	rdmsr

	// store value for later rewrite
	mov %eax, %esi
	mov %edx, %edi

	and $0xfff, %eax // clear base address bits
	mov $0, %edx
	or $SYM_OFFSET(apic_base), %eax
	wrmsr

	mov $SYM_OFFSET(apic_base), %eax
	add %cs:SYM_OFFSET(apic_offset), %eax
	mov (%eax), %ebx
	mov %ebx, %cs:SYM_OFFSET(apic_readback)

	// mask APIC vector to prevent hangs due to SMIs/NMIs
	mov $APIC_VECTOR_MASKED, %ebx
	mov %ebx, (%eax)

	// restore APIC base address
	mov $MSR_APIC_BAR, %ecx
	mov %esi, %eax
	mov %edi, %edx
	wrmsr

	STATUS(0)
1:
	cli
	hlt
	jmp	1b

// print asciiz string at by cs:si
print_strz:
#if DEBUG
	xor	%bx, %bx
	xor	%dx, %dx
1:
	mov	%cs:(%si), %al
	inc	%si
	or	%al, %al
	jz	2f
	mov	$0x0e, %ah
	int	$0x10
	mov	$0x01, %ah
	int	$0x14
	jmp	1b
2:
#endif
	ret

print_hex32:
#if DEBUG
	push	%eax
	push	%ebx
	push	%ecx
	push	%edx
	push	%esi
	mov	%eax, %ebx
	mov	$8, %ecx
	mov	$SYM_OFFSET(hex32_buf) + 8, %si
1:
	mov	%bl, %al
	and	$0x0f, %al
	add	$0x30, %al
	cmp	$0x3a, %al
	jb	2f
	add	$0x27, %al
2:
	mov	%al, %cs:(%si)
	dec	%si
	shr	$4, %ebx
	loop	1b

	mov	$SYM_OFFSET(hex32_buf), %si
	call	print_strz
	pop	%esi
	pop	%edx
	pop	%ecx
	pop	%ebx
	pop	%eax
#endif
	ret

	.align 64
EXPORT(msr_mcfg)
	.long 0, 0
EXPORT(msr_topmem)
	.long 0, 0
EXPORT(msr_topmem2)
	.long 0, 0
EXPORT(msr_cpuwdt)
	.long 0, 0
EXPORT(mtrr_default)
	.long 0, 0
EXPORT(fixed_mtrr_regs)
	.long MSR_MTRR_FIX64K_00000
	.long MSR_MTRR_FIX16K_80000
	.long MSR_MTRR_FIX16K_A0000
	.long MSR_MTRR_FIX4K_C0000
	.long MSR_MTRR_FIX4K_C8000
	.long MSR_MTRR_FIX4K_D0000
	.long MSR_MTRR_FIX4K_D8000
	.long MSR_MTRR_FIX4K_E0000
	.long MSR_MTRR_FIX4K_E8000
	.long MSR_MTRR_FIX4K_F0000
	.long MSR_MTRR_FIX4K_F8000
	.long 0xffffffff
EXPORT(mtrr_fixed)
	.skip 11*8, 0
EXPORT(mtrr_var_base)
	.skip 8*8, 0
EXPORT(mtrr_var_mask)
	.skip 8*8, 0
EXPORT(msr_syscfg)
	.long 0, 0
EXPORT(msr_topmem_rem)
	.long 0, 0
EXPORT(msr_smm_base_rem)
	.long 0, 0
EXPORT(msr_hwcr)
	.long 0, 0
EXPORT(msr_mc4_misc0)
	.long 0, 0
EXPORT(msr_mc4_misc1)
	.long 0, 0
EXPORT(msr_mc4_misc2)
	.long 0, 0
EXPORT(msr_readback)
	.long 0, 0
EXPORT(msr_lscfg)
	.long 0, 0
EXPORT(msr_cucfg2)
	.long 0, 0
EXPORT(apic_offset)
	.long 0
EXPORT(apic_readback)
	.long 0
EXPORT(cpu_status)
	.long 0
EXPORT(cpu_apic_renumber)
	.byte 0
EXPORT(cpu_apic_hi)
	.byte 0
EXPORT(counter)
	.long 0
#if DEBUG
cpu_mtrr:
	.asciz	"\r\nMTRR "
cpu_cr:
	.asciz	"\r\nCRx "
cpu_done1:
	.asciz	"\r\nCPU "
cpu_done2:
	.asciz  " done setting MTRRs\r\n"
hex32_buf:
	.asciz	"[--------] "
#endif
EXPORT(old_int15_vec)
	.word 0,0

	.align 64
EXPORT(new_e820_len)
	.word 0
EXPORT(new_e820_map)
	.skip E820_MAP_MAX, 0

	.align 64
stack_start:  .skip 1024, 0
stack_end:

EXPORT(new_e820_handler)
	cmp	$0xe820, %eax
	jne	1f
	cmp	$0x534d4150, %edx
	je	2f
1:	ljmp	%cs:*SYM_OFFSET(old_int15_vec)

2:	mov	$0x534d4150, %eax
	cmp	$20, %ecx
	jl	1f
	test	$0xffff0000, %ebx
	jnz	1f
	cmp	%cs:SYM_OFFSET(new_e820_len), %bx
	jg	1f

	push	%ds
	push	%cs
	pop	%ds
	push	%di
	push	%esi
	shl	$2, %ebx
	leal	SYM_OFFSET(new_e820_map)(%ebx, %ebx, 4), %esi
	mov	$5, %ecx
	rep	movsl
	pop	%esi
	pop	%di
	pop	%ds
	shr	$2, %ebx
	inc	%ebx
	mov	$20, %ecx
	and	$~1, 4(%esp)	# Clear carry flag
	cmp	%cs:SYM_OFFSET(new_e820_len), %bx
	jge	2f
	iret

1:	or	$1, 4(%esp)	# Set carry flag to signify error
2:	xor	%ebx, %ebx
	iret

	.global asm_relocate_end
asm_relocate_end:

// replacement code for brute-force SMM disable
	.code16
	.text
	.align 4096

	.global smm_handler_start
smm_handler_start:
	rsm
	.global smm_handler_end
smm_handler_end:

	.align 4096
	.global apic_base
apic_base:
