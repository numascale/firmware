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
EXPORT(vector)
	cli

	mov	%cs, %ax
	add	$SYM_OFFSET(stack_start) / 16, %ax
	mov	%ax, %ss
	mov	$(stack_end - stack_start), %sp

	movl %cs:SYM_OFFSET(status), %edx
	STATUS(0x80);
	cmpl $VECTOR_SETUP, %edx
	je setup
	cmpl $VECTOR_SETUP_OBSERVER, %edx
	je setup_observer
	cmpl $VECTOR_TEST_START, %edx
	je test_start
	cmpl $VECTOR_TEST_STOP, %edx
	je test_stop

	STATUS(0x81);
1:	cli
	hlt
	jmp	1b
setup:
STATUS(0x90);
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

	STATUS(0x82)

	mov	%cs:SYM_OFFSET(apic_low), %al
	shl	$24, %eax
	mov	%eax, %es:0x20
	mov	%es:0x20, %eax
	call print_hex32

	STATUS(0x83)

	pop	%eax
	and	$~0x100, %eax	// clear BSP flag to let core accept INIT and STARTUP IPIs
	mov	$MSR_APIC_BAR, %ecx
	wrmsr

	STATUS(0x84)

	mov	$MSR_NODE_ID, %ecx
	rdmsr

	/* "Bios scratch" given as [11:6], so we're limited to an
	   8-bit prefix here for the time being.  Ideally we want 8 bits,
	   and since all upper bits of this MSR appear to be r/w, we
	   could just take some liberties with the register. */
	and	$~0xfc0, %eax
	xor	%ebx, %ebx
	mov	%cs:SYM_OFFSET(apic_high), %bl
	shl	$6, %ebx
	or	%ebx, %eax
	wrmsr

setup_observer:
	mov	$SYM_OFFSET(msrs), %edi
1:	mov	%cs:(%edi), %ecx // MSR number
	add	$4, %edi

	cmp $0, %ecx
	je 2f

	mov	%cs:(%edi), %eax // value[0]
	add	$4, %edi
	mov	%cs:(%edi), %edx // value[1]
	add	$4, %edi

	wrmsr
	jmp	1b
2:	STATUS(VECTOR_SETUP_DONE)
3:	cli
	hlt
	jmp	3b

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
	STATUS(VECTOR_TEST_STARTED)
	mov $1, %ebx
1:	call prng
	and $~63, %eax // align access
	mov $0x1, %edx // 4GB base
	call read32
	inc %ebx
	jmp 1b

test_stop:
	STATUS(VECTOR_TEST_STOPPED)
1:	cli
	hlt
	jmp 1b

// print asciiz string at by cs:si
print_strz:
#ifdef DEBUG
	xor	%bx, %bx
	xor	%dx, %dx
1:	mov	%cs:(%si), %al
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
#ifdef DEBUG
	push	%eax
	push	%ebx
	push	%ecx
	push	%edx
	push	%esi
	mov	%eax, %ebx
	mov	$8, %ecx
	mov	$SYM_OFFSET(hex32_buf) + 8, %si

1:	mov	%bl, %al
	and	$0x0f, %al
	add	$0x30, %al
	cmp	$0x3a, %al
	jb	2f
	add	$0x27, %al

2:	mov	%al, %cs:(%si)
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
EXPORT(msrs)
	.skip MSR_MAX * 12, 0
EXPORT(status)
	.long 0
EXPORT(apic_low)
	.byte 0
EXPORT(apic_high)
	.byte 0
#ifdef DEBUG
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
