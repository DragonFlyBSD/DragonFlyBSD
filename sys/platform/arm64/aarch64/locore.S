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
 * This is a minimal stub kernel for MVP Part 2. It receives control from
 * the EFI loader, prints a message to the PL011 UART, and halts.
 *
 * Entry:
 *   x0 = modulep (pointer to preload metadata)
 *
 * QEMU virt machine PL011 UART is at 0x09000000
 */

	.text
	.globl	_start
	.type	_start, @function

_start:
	/*
	 * Save modulep in a callee-saved register in case we need it later.
	 * For now, we just print and halt.
	 */
	mov	x19, x0			/* Save modulep */

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

	.size	_start, . - _start
