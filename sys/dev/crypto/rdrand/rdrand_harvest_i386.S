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
	movl	4(%esp),%edi
	movl	8(%esp),%ecx
	pushl	%ebx

	shrl	$2,	%ecx	/* Divide by 4 to get 32-bit word count */

	xorl	%ebx, 	%ebx
loop:
	/*
	 * edx: (output)	entropy
	 */
	rdrand	%edx		/* rdrand32 */

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
	movl	%edx,	(%edi)

	/*
	 * Increment the count of stored random words as well as the
	 * buffer pointer in edi.
	 */
	addl	$1, 	%ebx
	addl	$4,	%edi

	/*
	 * If we haven't already read enough random words,
	 * loop again.
	 */
	cmpl	%ecx, 	%ebx
	jl	loop
out:
	/* return the number of stored random bytes (random words * 4) */
	shll	$2,	%ebx
	movl	%ebx, 	%eax
	popl	%ebx
	ret
END(rdrand_rng)
