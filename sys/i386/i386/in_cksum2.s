/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/i386/i386/Attic/in_cksum2.s,v 1.1 2004/02/14 02:09:26 dillon Exp $
 */

#include <machine/asmacros.h>		/* miscellaneous asm macros */
#include <machine/apic.h>
#include <machine/specialreg.h>

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
	movl	4(%esp),%edx	/* %edx = buffer pointer */
	movl	8(%esp),%ecx	/* %ecx = counter */
	subl	%eax,%eax	/* %eax = checksum */
	cmpl	$5,%ecx
	jl	2f
1:
	subl	$5,%ecx
	addl	(%edx),%eax
	adcl	4(%edx),%eax
	adcl	8(%edx),%eax
	adcl	12(%edx),%eax
	adcl	16(%edx),%eax
	adcl	$0,%eax
	addl	$20,%edx
	cmpl	$5,%ecx
	jge	1b
2:
	testl	%ecx,%ecx
	je	4f
3:
	addl	(%edx),%eax
	adcl	$0,%eax
	addl	$4,%edx
	decl	%ecx
	jnz	3b
4:
	ret
