/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
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

/* int padlock_rng(uint8_t *out, int limit) */
ENTRY(padlock_rng)
	movl	4(%esp),%edi
	movl	8(%esp),%ecx
	pushl	%ebx

	movl	$3,	%edx
	xorl	%ebx, 	%ebx
loop:
	/*
	 * edx: (input)		quality factor of rng entropy
	 * edi: (input)		buffer for random data
	 			autoincremented at end of instruction
	 * eax: (output)	rng status word
	 */
	.byte	0x0f, 0xa7, 0xc0 /* xstore-rng */


	/*
	 * The lower 5 bits of %eax contain the number of random
	 * bytes stored.
	 * If no bytes were stored, there is no more entropy
	 * available, so we finish up.
	 */
	andl	$0x1f, 	%eax
	jz	out

	/*
	 * Increment the count of stored random bytes. The buffer pointer
	 * in edi is auto-incremented by the xstore-rng instruction.
	 */
	addl	%eax, 	%ebx

	/*
	 * If we haven't already read enough random bytes,
	 * loop again.
	 */
	cmpl	%ecx, 	%ebx
	jl	loop
out:
	/* return the number of stored random bytes. */
	movl	%ebx, 	%eax
	popl	%ebx
	ret
END(padlock_rng)
