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

#define	SCTLR_M		(1 << 0)
#define	SCTLR_C		(1 << 2)
#define	SCTLR_I		(1 << 12)
#define	SCTLR_RES1	((1 << 29) | (1 << 28) | (1 << 23) | (1 << 22) | \
			 (1 << 20) | (1 << 11) | (1 << 8))
#define	SCTLR_MMU_OFF	(SCTLR_RES1)
#define	SCTLR_MMU_ON	(SCTLR_RES1 | SCTLR_M | SCTLR_C | SCTLR_I)

#define	TCR_T0SZ_SHIFT	0
#define	TCR_T1SZ_SHIFT	16
#define	TCR_TG0_SHIFT	14
#define	TCR_TG1_SHIFT	30
#define	TCR_SH0_SHIFT	12
#define	TCR_SH1_SHIFT	28
#define	TCR_ORGN0_SHIFT	10
#define	TCR_ORGN1_SHIFT	26
#define	TCR_IRGN0_SHIFT	8
#define	TCR_IRGN1_SHIFT	24
#define	TCR_IPS_SHIFT	32

#define	TCR_T0SZ	(16)
#define	TCR_T1SZ	(16)
#define	TCR_TG0_4K	(0)
#define	TCR_TG1_4K	(2)
#define	TCR_SH_INNER	(3)
#define	TCR_WBWA	(1)

#define	TCR_VALUE	((TCR_T0SZ << TCR_T0SZ_SHIFT) | \
			 (TCR_T1SZ << TCR_T1SZ_SHIFT) | \
			 (TCR_TG0_4K << TCR_TG0_SHIFT) | \
			 (TCR_TG1_4K << TCR_TG1_SHIFT) | \
			 (TCR_SH_INNER << TCR_SH0_SHIFT) | \
			 (TCR_SH_INNER << TCR_SH1_SHIFT) | \
			 (TCR_WBWA << TCR_ORGN0_SHIFT) | \
			 (TCR_WBWA << TCR_ORGN1_SHIFT) | \
			 (TCR_WBWA << TCR_IRGN0_SHIFT) | \
			 (TCR_WBWA << TCR_IRGN1_SHIFT))

#define	MAIR_DEVICE_nGnRnE	0x00
#define	MAIR_NORMAL_WB		0xFF
#define	MAIR_VALUE	((MAIR_DEVICE_nGnRnE << 0) | (MAIR_NORMAL_WB << 8))

#define	PTE_BLOCK	(1 << 0)
#define	PTE_TABLE	(3 << 0)
#define	PTE_AF		(1 << 10)
#define	PTE_SH_INNER	(3 << 8)
#define	PTE_ATTRIDX(n)	((n) << 2)
#define	PTE_PXN		0x0020000000000000
#define	PTE_UXN		0x0040000000000000

#define	PTE_BLOCK_DEVICE	(0x00000000 | PTE_BLOCK | PTE_AF | \
					 PTE_SH_INNER | PTE_ATTRIDX(0) | \
					 PTE_PXN | PTE_UXN)
#define	PTE_BLOCK_NORMAL	(0x40000000 | PTE_BLOCK | PTE_AF | \
					 PTE_SH_INNER | PTE_ATTRIDX(1))

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
	bic	x1, x1, #(SCTLR_M | SCTLR_C | SCTLR_I)
	msr	sctlr_el1, x1
	isb

	/* Build minimal identity page tables and enable MMU */
	bl	create_pagetables
	bl	start_mmu

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

	.align	11
exception_vectors:
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.
	b	.

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

	.bss
	.align	12
ttbr0_l0:
	.space	4096
ttbr0_l1:
	.space	4096

	.size	_start, . - _start
