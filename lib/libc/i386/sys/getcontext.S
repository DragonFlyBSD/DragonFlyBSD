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
 * $DragonFly: src/lib/libc/i386/sys/getcontext.S,v 1.2 2007/02/04 20:28:21 corecode Exp $
 */

#include <machine/asm.h>
#include <asmcontext.h>

	/*
	 * This function is special-cased because the context it saves
	 * includes a stale stack context (because it returns before the
	 * caller presumably makes the call to setcontext()).  
	 */
	.weak	getcontext
	.set	getcontext,_getcontext
ENTRY(_getcontext)
	/*
	 * Retrieve the current signal mask and save it in &ucp->uc_sigmask.
	 */
	movl	4(%esp),%eax		/* ucontext_t pointer */
	addl	$UC_SIGMASK,%eax	/* pointer to signal mask */
	PIC_PROLOGUE
	pushl	%eax
	pushl	$0
	pushl	$SIG_BLOCK
	call	PIC_PLT(_sigprocmask)	/* retrieve & save signal mask */
	addl	$12,%esp
	PIC_EPILOGUE

	/*
	 * Save what we need because our stack context is going stale.
	 */
	movl	4(%esp),%eax
	movl	%eax,%ecx		/* save ucontext_t in %ecx */
	movl	(%esp),%edx		/* save return PC in %edx */
	addl	$UC_MCONTEXT,%eax
	pushl	%eax
	call	get_mcontext		/* returns non-zero on resume */
	addl	$4,%esp
	cmpl	$0,%eax
	je	2f
	/*
	 * On resume, resave the stale return pc and restore the signal
	 * mask (signals are blocked right now from the setcontext call).
	 */
	movl	%edx,(%esp)		/* re-save the return PC */
	addl	$UC_SIGMASK,%ecx
	PIC_PROLOGUE
	pushl	$0
	pushl	%ecx
	pushl	$SIG_SETMASK
	call	PIC_PLT(_sigprocmask)	/* retrieve & save signal mask */
	addl	$12,%esp
	PIC_EPILOGUE
2:
	movl	$0,%eax			/* return success */
	ret

1:
	PIC_PROLOGUE
	jmp	PIC_PLT(HIDENAME(cerror))
