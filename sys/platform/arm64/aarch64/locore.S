/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

.equ	BOOT_STACK_SIZE, 16384
/*
 * ARM64 Kernel Entry Point (Stub)
 *
 * Minimal kernel entry for MVP Part 2/3. It receives control from the EFI
 * loader, sets up a basic identity-mapped page table, enables the MMU,
 * prints a message to the PL011 UART, and halts.
 *
 * Entry:
 *   x0 = modulep (pointer to preload metadata)
 *
 * QEMU virt machine PL011 UART is at 0x09000000
 */

.equ	SCTLR_M, 0x00000001
.equ	SCTLR_C, 0x00000004
.equ	SCTLR_I, 0x00001000
.equ	SCTLR_RES1, 0x30d00900
.equ	SCTLR_MMU_OFF, SCTLR_RES1
.equ	SCTLR_MMU_ON, 0x30d01905

.equ	TCR_VALUE, 0xb5103510

.equ	MAIR_VALUE, 0x0000ff00

.equ	PTE_BLOCK, 0x1
.equ	PTE_TABLE, 0x3
.equ	PTE_AF, 0x400
.equ	PTE_SH_INNER, 0x300
.equ	PTE_PXN, 0x0020000000000000
.equ	PTE_UXN, 0x0040000000000000

.equ	PTE_BLOCK_DEVICE, 0x0060000000000701
.equ	PTE_BLOCK_NORMAL, 0x0000000040000705

	.text
	.globl	_start
	.type	_start, @function

_start:
	/*
	 * Save modulep in a callee-saved register in case we need it later.
	 * For now, we just print and halt.
	 */
	mov	x19, x0			/* Save modulep */

	/* Ensure MMU and caches are off before reprogramming TTBRs */
	mrs	x1, sctlr_el1
	bic	x1, x1, #SCTLR_M
	bic	x1, x1, #SCTLR_C
	bic	x1, x1, #SCTLR_I
	msr	sctlr_el1, x1
	isb

	/* Build minimal identity page tables and enable MMU */
	bl	create_pagetables
	bl	start_mmu

	/* Set up a temporary stack for early C code */
	adr	x1, initstack_end
	mov	sp, x1

	/* Call early C entry with modulep */
	mov	x0, x19
	bl	initarm

	/*
	 * Print banner to PL011 UART at 0x09000000
	 * PL011 UARTDR (data register) is at offset 0x00
	 */
	ldr	x1, =0x09000000		/* UART base address */
	adr	x2, banner		/* Address of banner string */

print_loop:
	ldrb	w3, [x2], #1		/* Load byte, post-increment */
	cbz	w3, done		/* If null terminator, we're done */
	strb	w3, [x1]		/* Write byte to UART */
	b	print_loop		/* Continue */

done:
	/*
	 * Halt: infinite loop with WFI (Wait For Interrupt)
	 * This is the proper way to halt on ARM64
	 */
halt:
	wfi
	b	halt

	.align	3
banner:
	.asciz	"\r\n\r\nDragonFly/arm64 kernel started!\r\nmodulep received, halting.\r\n"

exc_sync_msg:
	.asciz	"\r\n[arm64] sync exception: ESR=0x"
exc_far_msg:
	.asciz	" FAR=0x"
exc_elr_msg:
	.asciz	" ELR=0x"
exc_end_msg:
	.asciz	"\r\n"
hex_digits:
	.asciz	"0123456789abcdef"

	.align	11
exception_vectors:
	b	exception_sync
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_sync
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin
	b	exception_spin

exception_sync:
	ldr	x0, =exc_sync_msg
	bl	uart_puts
	mrs	x0, esr_el1
	bl	uart_puthex
	ldr	x0, =exc_far_msg
	bl	uart_puts
	mrs	x0, far_el1
	bl	uart_puthex
	ldr	x0, =exc_elr_msg
	bl	uart_puts
	mrs	x0, elr_el1
	bl	uart_puthex
	ldr	x0, =exc_end_msg
	bl	uart_puts

exception_spin:
	wfi
	b	exception_spin

uart_putc:
	ldr	x1, =0x09000000
	strb	w0, [x1]
	ret

uart_puts:
	ldrb	w1, [x0], #1
	cbz	w1, 1f
	mov	w0, w1
	bl	uart_putc
	b	uart_puts
1:
	ret

uart_puthex:
	ldr	x2, =hex_digits
	mov	x3, #60
1:
	lsr	x4, x0, x3
	and	x4, x4, #0xf
	ldrb	w4, [x2, x4]
	mov	w0, w4
	bl	uart_putc
	subs	x3, x3, #4
	b.ge	1b
	ret

create_pagetables:
	adr	x1, ttbr0_l0
	adr	x2, ttbr0_l1
	mov	x3, xzr
	mov	x4, #0

	/* Clear L0 and L1 tables (2 pages) */
1:
	stp	x3, x3, [x1], #16
	add	x4, x4, #16
	cmp	x4, #(4096 * 2)
	b.lo	1b

	/* L0 entry 0 -> L1 table */
	adr	x1, ttbr0_l0
	adr	x2, ttbr0_l1
	orr	x3, x2, #PTE_TABLE
	str	x3, [x1]

	/* L1 entry 0: device map for 0x00000000-0x3fffffff */
	ldr	x3, =PTE_BLOCK_DEVICE
	str	x3, [x2]

	/* L1 entry 1: normal map for 0x40000000-0x7fffffff */
	ldr	x3, =PTE_BLOCK_NORMAL
	str	x3, [x2, #8]

	ret

start_mmu:
	dsb	sy

	ldr	x0, =exception_vectors
	msr	vbar_el1, x0

	adr	x0, ttbr0_l0
	msr	ttbr0_el1, x0
	msr	ttbr1_el1, xzr
	isb

	tlbi	vmalle1
	dsb	sy
	isb

	ldr	x0, =MAIR_VALUE
	msr	mair_el1, x0
	ldr	x0, =TCR_VALUE
	msr	tcr_el1, x0
	isb

	ldr	x0, =SCTLR_MMU_ON
	msr	sctlr_el1, x0
	isb

	ret

	.size	_start, . - _start

	.bss
	.align	12
initstack:
	.space	BOOT_STACK_SIZE
initstack_end:
	.align	12
ttbr0_l0:
	.space	4096
ttbr0_l1:
	.space	4096
