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

/* int rdrand_rng(uint8_t *out, int limit) */
ENTRY(rdrand_rng)
	movl	%esi,	%ecx
	shrl	$3,	%ecx	/* Divide by 8 to get 64-bit word count */

	xorq	%rbx, 	%rbx
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
	addl	$1, 	%ebx
	addq	$8,	%rdi

	/*
	 * If we haven't already read enough random words,
	 * loop again.
	 */
	cmpl	%ecx, 	%ebx
	jl	loop
out:
	/* return the number of stored random bytes (random words * 8) */
	shll	$3,	%ebx
	movl	%ebx, 	%eax
	ret
END(rdrand_rng)
