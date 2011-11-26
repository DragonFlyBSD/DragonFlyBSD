/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
/*
 * void bzero(void *buf, u_int len)	(arguments passed on stack)
 */

#include <machine/asmacros.h>
#include <machine/cputypes.h>
#include <machine/pmap.h>
#include <machine/specialreg.h>

#include "assym.s"

	.text

/*
 * NOTE: GCC-4.x may call memset directly, we can't use an indirect pointer.
 */
ENTRY(memset)
	pushl	%edi
	movl	4+4(%esp),%edi
	movl	12+4(%esp),%ecx
	movzbl	8+4(%esp),%eax
	movl	%eax,%edx
	shll	$8,%edx
	orl	%edx,%eax
	movl	%eax,%edx
	shll	$16,%edx
	orl	%edx,%eax
	jmp	2f

/*
 * Ignore inefficiencies due to alignment.  Most callers will supply
 * reasonably aligned pointers.
 */
ENTRY(bzero)
	pushl	%edi
	subl	%eax,%eax
	movl	4+4(%esp),%edi
	movl	8+4(%esp),%ecx
	jmp	2f
	SUPERALIGN_TEXT
1:
	movl	%eax,(%edi)
	movl	%eax,4(%edi)
	addl	$8,%edi
2:
	subl	$8,%ecx
	jae	1b
	addl	$8,%ecx
	jz	3f
	cld
	rep
	stosb
3:
	popl	%edi
	ret

ENTRY(i686_pagezero)
	pushl	%edi
	pushl	%ebx

	movl	12(%esp), %edi
	movl	$1024, %ecx
	cld

	ALIGN_TEXT
1:
	xorl	%eax, %eax
	repe
	scasl	
	jnz	2f

	popl	%ebx
	popl	%edi
	ret

	ALIGN_TEXT

2:
	incl	%ecx
	subl	$4, %edi

	movl	%ecx, %edx
	cmpl	$16, %ecx

	jge	3f

	movl	%edi, %ebx
	andl	$0x3f, %ebx
	shrl	%ebx
	shrl	%ebx
	movl	$16, %ecx
	subl	%ebx, %ecx

3:
	subl	%ecx, %edx
	rep
	stosl

	movl	%edx, %ecx
	testl	%edx, %edx
	jnz	1b

	popl	%ebx
	popl	%edi
	ret

