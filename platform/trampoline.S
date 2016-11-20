/* -*- Mode: Asm; indent-tabs-mode:t ; -*- */
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
#define RELOCATED(sym) ((sym) - asm_relocate_start)
#define STATUS(val) movl $val, %cs:RELOCATED(vector)
#define SEM_POST() lock decw %cs:RELOCATED(pending)
#define INC_ERROR() lock incl %cs:RELOCATED(errors)

	.text
	.code16
	.balign 4096

	.global asm_relocate_start
asm_relocate_start:
EXPORT(entry)
	cli

	// invalidate TLB
	xorl	%eax, %eax
	movl	%eax, %cr3

//	ljmp	$0x8800, $RELOCATED(1f)
	// setup segments
1:	mov	%cs, %ax
	add	$RELOCATED(stack_start) / 16, %ax
	mov	%ax, %ss
	movl	$(stack_end - stack_start), %esp

	movl	%cs:RELOCATED(vector), %edx

	// vector jump table
	cmpl	$VECTOR_CACHE_DISABLE, %edx
	je	cache_disable
	cmpl	$VECTOR_CACHE_ENABLE, %edx
	je	cache_enable
	cmpl	$VECTOR_SETUP, %edx
	je	setup
	cmpl	$VECTOR_SETUP_OBSERVER, %edx
	je	setup_observer
	cmpl	$VECTOR_TEST, %edx
	je	test
	// unknown vector
	STATUS(84)
1:	cli
	hlt
	jmp	1b

setup:
	STATUS(70)

	// load APIC BAR into FS
	mov	$MSR_APIC_BAR, %ecx
	rdmsr
	and	$~0xfff, %eax
	mov	$MSR_FS_BASE, %ecx
	wrmsr

	STATUS(71)

	// set APIC ID
	mov	%cs:RELOCATED(apic_local), %al
	shl	$24, %eax
	mov	%eax, %fs:(0x20)

	// clear FS
	xor	%eax, %eax
	xor	%edx, %edx
	mov	$MSR_FS_BASE, %ecx
	wrmsr

	// fall through to common code for observer
setup_observer:
	STATUS(72)

	mov	$RELOCATED(msrs), %edi
1:	mov	%cs:(%edi), %ecx // MSR number
	add	$4, %edi

	cmp	$0, %ecx
	je	cache_enable

	mov	%cs:(%edi), %eax // value[0]
	add	$4, %edi
	mov	%cs:(%edi), %edx // value[1]
	add	$4, %edi

	wrmsr
	jmp	1b
	STATUS(74)

	// fall through to enable cache
cache_enable:
	mov	%cr0, %eax
	and	$~((1 << 30) | (1 << 29)), %eax
	mov	%eax, %cr0

	SEM_POST()
1:	cli
	hlt
	jmp	1b

cache_disable:
	// detect cpu family
	mov	$1, %eax
	xor	%ecx,%ecx
	cpuid
	mov	%eax,%edx
	shr	$0x8,%edx
	and	$0xf,%edx
	shr	$0x14,%eax
	add	%edx,%eax
	cmp	$0x15,%eax
	jl	1f

	// ensure CD bit is shared amongst cores on Fam15h
	mov	$MSR_CU_CFG3, %ecx
	rdmsr
	or	$(1 << (49 - 32)), %edx
	wrmsr
1:
	mov	%cr0, %eax
	or	$(1 << 30), %eax
	mov	%eax, %cr0
	wbinvd

	SEM_POST()
2:	cli
	hlt
	jmp	2b

test:
	SEM_POST()

	// disable wrap32
	mov	$MSR_HWCR, %ecx
	rdmsr
	or	$(1 << 17), %eax
	wrmsr

	// load top of DRAM into ECX:EAX
	mov		$MSR_TOPMEM2, %ecx
	rdmsr
	mov		%edx, %ebp // store top[63:32] into EBP

	// seed using APIC ID
	mov		$0x8000001e, %eax
	cpuid

	// store seed in EDI:ESI
	mov		$0, %edi
	mov		%eax, %esi

1:	mov		%edi, %edx
	mov		%esi, %eax

	// 48-bit Linear Congruntial Generator: next = (25214903917ULL * last + 11) mod 2^48
	imul		$0xdeece66d, %edx, %ebx
	mov		%eax, %ecx
	lea		(%eax, %eax, 4), %eax
	add		%eax, %ebx
	mov		$0xdeece66d, %eax
	mul		%ecx
	add		%ebx, %edx
	add		$0xb, %eax
	adc		$0x0, %edx
	and		$0xffff, %edx

	// store 48-bit result in EDI:ESI for next iteration
	mov		%edx, %edi
	mov		%eax, %esi

	// get addr[63:32] % top[63:32]
	mov		$0, %edx
	mov		%edi, %eax
	div		%ebp

	// start base at 4GB to avoid corrupting state
	inc		%edx

	// if in HT decode range, skip
	cmpl		$0xfd, %edx
	je			1b

	// load addr[31:0] and align access
	mov		%esi, %eax
	andl		$~3, %eax

	// generate 64-bit RMW using FS
	mov		$MSR_FS_BASE, %ecx
	wrmsr
	movl 		$0, %fs:(0)

	// check for termination condition
	cmpl	$VECTOR_TEST_FINISH, %cs:RELOCATED(vector)
	jne	1b

	// reenable wrap32
	mov	$MSR_HWCR, %ecx
	rdmsr
	and	$~(1 << 17), %eax
	wrmsr

	// clear FS
	xor	%eax, %eax
	xor	%edx, %edx
	mov	$MSR_FS_BASE, %ecx
	wrmsr

	SEM_POST()
3:	cli
	hlt
	jmp	3b

	.balign 64
EXPORT(vector)
	.long 0
EXPORT(pending)
	.long 0
EXPORT(errors)
	.long 0
EXPORT(old_int15_vec)
	.long 0
EXPORT(new_e820_map)
	.skip E820_MAP_MAX, 0
EXPORT(msrs)
	.skip MSR_MAX * 12, 0
EXPORT(new_e820_len)
	.word 0
EXPORT(apic_local)
	.byte 0

	.balign 64
stack_start:
	.skip 1024, 0
stack_end:

	.balign 64
EXPORT(new_e820_handler)
	cmp	$0xe820, %eax
	jne	1f
	cmp	$0x534d4150, %edx
	je	2f
1:	ljmp	%cs:*RELOCATED(old_int15_vec)

2:	mov	$0x534d4150, %eax
	cmp	$20, %ecx
	jl	1f
	test	$0xffff0000, %ebx
	jnz	1f
	cmp	%cs:RELOCATED(new_e820_len), %bx
	jg	1f

	push	%ds
	push	%cs
	pop	%ds
	push	%di
	push	%esi
	shl	$2, %ebx
	leal	RELOCATED(new_e820_map)(%ebx, %ebx, 4), %esi
	mov	$5, %ecx
	rep	movsl
	pop	%esi
	pop	%di
	pop	%ds
	shr	$2, %ebx
	inc	%ebx
	mov	$20, %ecx
	and	$~1, 4(%esp)	// clear carry flag
	cmp	%cs:RELOCATED(new_e820_len), %bx
	jge	2f
	iret

1:	or	$1, 4(%esp)	// set carry flag to signify error
2:	xor	%ebx, %ebx
	iret

	.global asm_relocate_end
asm_relocate_end:

	// replacement code for brute-force SMM disable
	.text
	.code16
	.balign 4096

	.global smm_handler_start
smm_handler_start:
	rsm
	.global smm_handler_end
smm_handler_end:
