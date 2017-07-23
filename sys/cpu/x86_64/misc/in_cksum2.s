/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include <machine/asmacros.h>		/* miscellaneous asm macros */

#include "assym.s"

	.text

	/*
	 * asm_ones32(32bitalignedbuffer, numberof32bitwords)
	 *
	 * Returns the 32 bit one complement partial checksum.  This is
	 * basically a 1's complement checksum without the inversion (~)
	 * at the end.  A 32 bit value is returned.  If the caller is
	 * calculating a 16 bit 1's complement checksum the caller must
	 * collapse the 32 bit return value via:
	 *
	 *	result = (result >> 16) + (result & 0xFFFF)
	 *	if (result > 0xFFFF)
	 *	    result -= 0xFFFF;	<<< same as (result + 1) & 0xFFFF
	 *				    within the range of result.
	 * Note that worst case 0xFFFFFFFF + 0xFFFFFFFF = 0xFFFFFFFE + CARRY,
	 * so no double-carry ever occurs.
	 */
	.p2align 4
ENTRY(asm_ones32)
	movq	%rdi,%rdx	/* %rdx = buffer pointer */
	movl	%esi,%ecx	/* %ecx = counter */
	xorl	%eax,%eax	/* %eax = checksum */
	cmpl	$5,%ecx
	jl	2f
1:
	subl	$5,%ecx
	addl	(%rdx),%eax
	adcl	4(%rdx),%eax
	adcl	8(%rdx),%eax
	adcl	12(%rdx),%eax
	adcl	16(%rdx),%eax
	adcl	$0,%eax
	addq	$20,%rdx
	cmpl	$5,%ecx
	jge	1b
2:
	testl	%ecx,%ecx
	je	4f
3:
	addl	(%rdx),%eax
	adcl	$0,%eax
	addq	$4,%rdx
	decl	%ecx
	jnz	3b
4:
	ret
END(asm_ones32)
