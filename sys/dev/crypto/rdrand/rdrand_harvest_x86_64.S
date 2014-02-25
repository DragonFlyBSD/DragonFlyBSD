/*
 * Copyright (c) 2012 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asmacros.h>

	.text

/* int rdrand_rng(uint8_t *out, long limit) */
ENTRY(rdrand_rng)
	movq	%rsi,	%rcx
	shrq	$3,	%rcx	/* Divide by 8 to get 64-bit word count */

	xorq	%r11, 	%r11
loop:
	/*
	 * rdx: (output)	entropy
	 */
	rdrand	%rdx		/* rdrand64 */

	/*
	 * The carry flag (CF) determines if the instruction was
	 * successful or not.
	 *
	 * CF = 0: Random value not available at time of execution.
	 */
	jae	out

	/*
	 * The operation was successful, so store the random data
	 * from the register into our buffer.
	 */
	movq	%rdx,	(%rdi)

	/*
	 * Increment the count of stored random words as well as the
	 * buffer pointer in rdi.
	 */
	addq	$1, 	%r11
	addq	$8,	%rdi

	/*
	 * If we haven't already read enough random words,
	 * loop again.
	 */
	cmpq	%rcx, 	%r11
	jl	loop
out:
	/* return the number of stored random bytes (random words * 8) */
	shlq	$3,	%r11
	movq	%r11, 	%rax
	ret
END(rdrand_rng)
