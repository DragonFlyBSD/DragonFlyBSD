/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/platform/vkernel/i386/locore.s,v 1.1 2006/11/07 18:50:07 dillon Exp $
 */

#include <machine/asmacros.h>
#include "assym.s"

	.data
	ALIGN_DATA		/* just to be sure */

	.text
NON_GPROF_ENTRY(btext)

	call	initvkernel
	call	mi_startup
1:
	hlt
	jmp	1b

#if 0
/*
 * Signal trampoline, copied to top of user stack
 */
NON_GPROF_ENTRY(sigcode)
	call	*SIGF_HANDLER(%esp)		/* call signal handler */
	lea	SIGF_UC(%esp),%eax		/* get ucontext_t */
	pushl	%eax
	testl	$PSL_VM,UC_EFLAGS(%eax)
	jne	9f
	movl	UC_GS(%eax),%gs			/* restore %gs */
9:
	movl	$SYS_sigreturn,%eax
	pushl	%eax				/* junk to fake return addr. */
	int	$0x80				/* enter kernel with args */
0:	jmp	0b

	ALIGN_TEXT
esigcode:

	.data
	.globl	szsigcode
szsigcode:
	.long	esigcode - sigcode

#endif

