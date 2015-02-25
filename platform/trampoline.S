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
#define STATUS(val) movl $val, %cs:SYM_OFFSET(status)

	.code16
	.text
	.align 4096

	.global asm_relocate_start
asm_relocate_start:
EXPORT(entry)
	cli

	mov	%cs, %ax
	add	$SYM_OFFSET(stack_start) / 16, %ax
	mov	%ax, %ss
	mov	$(stack_end - stack_start), %sp

	movl	%cs:SYM_OFFSET(status), %edx

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
1:	cli
	hlt
	jmp	1b

cache_disable:
	mov	%cr0, %eax
	or	$(1 << 30), %eax
	mov	%eax, %cr0
	wbinvd
	STATUS(VECTOR_SUCCESS)
1:	cli
	hlt
	jmp	1b

cache_enable:
	mov	%cr0, %eax
	and	$~((1 << 30) | (1 << 29)), %eax
	mov	%eax, %cr0
	STATUS(VECTOR_SUCCESS)
1:	cli
	hlt
	jmp	1b

setup:
	mov	$MSR_APIC_BAR, %ecx
	rdmsr
	push	%eax

	mov	$0x70800, %eax
	mov	$MSR_APIC_BAR, %ecx
	wrmsr

	mov	$0x7000, %ax
	mov	%ax, %es

	mov	%cs:SYM_OFFSET(apic_local), %al
	shl	$24, %eax
	mov	%eax, %es:0x20

	// clear BSP flag to let core accept INIT and STARTUP IPIs
	pop	%eax
	and	$~0x100, %eax
	mov	$MSR_APIC_BAR, %ecx
	wrmsr

setup_observer:
	mov	$SYM_OFFSET(msrs), %edi
1:	mov	%cs:(%edi), %ecx // MSR number
	add	$4, %edi

	cmp	$0, %ecx
	je	2f

	mov	%cs:(%edi), %eax // value[0]
	add	$4, %edi
	mov	%cs:(%edi), %edx // value[1]
	add	$4, %edi

	wrmsr
	jmp	1b

2:	STATUS(VECTOR_SUCCESS)
3:	cli
	hlt
	jmp	3b

// seed passed in EBX
// random number returned in EAX
prng:
	push	%edx
	mov	$1103515245, %eax
	mul	%ebx
	add	$12345, %eax
	pop	%edx
	ret

// address passed in EDX:EAX
// data returned in ECX
read32:
	mov	$MSR_FS_BASE, %ecx
	wrmsr
	mov	%fs:(0), %ecx
	ret

test:
	mov	$1, %ebx
	STATUS(VECTOR_SUCCESS)
1:	call	prng
	and	$~63, %eax // align access
	mov	$0x1, %edx // 4GB base
	call	read32
	inc	%ebx
	jmp	1b

	.align 64
EXPORT(msrs)
	.skip MSR_MAX * 12, 0
EXPORT(status)
	.long 0
EXPORT(old_int15_vec)
	.long 0
EXPORT(new_e820_map)
	.skip E820_MAP_MAX, 0
EXPORT(new_e820_len)
	.word 0
EXPORT(apic_local)
	.byte 0

	.align 64
stack_start:  .skip 1024, 0
stack_end:

	.align 64
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
	and	$~1, 4(%esp)	// clear carry flag
	cmp	%cs:SYM_OFFSET(new_e820_len), %bx
	jge	2f
	iret

1:	or	$1, 4(%esp)	// set carry flag to signify error
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
