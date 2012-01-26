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
	pushq	%rdi			/* save ucontext_t pointer passed */
	movq	%rdi,%rdx
	addq	$UC_SIGMASK,%rdx	/* pointer to signal mask */
	movq	$0,%rsi
	movq	$SIG_BLOCK,%rdi
	call    PIC_PLT(CNAME(_sigprocmask))
	/*
	 * Save what we need because our stack context is going stale.
	 */
	popq	%rdi
	movq	%rdi,%r9
	movq	(%rsp),%r8		/* save return PC in %r9 */
	addq	$UC_MCONTEXT,%rdi
	call	PIC_PLT(CNAME(get_mcontext))	/* returns non-zero on resume */
	cmpl	$0,%eax				/* return type is int */
	je	2f
	/*
	 * On resume, resave the stale return pc and restore the signal
	 * mask (signals are blocked right now from the setcontext call).
	 */
	movq	%r8,(%rsp)		/* re-save the return PC */
	movq	%r9,%rsi 
	addq	$UC_SIGMASK,%rsi
        movq    $0,%rdx
	movq	$SIG_SETMASK,%rdi
	call	PIC_PLT(CNAME(_sigprocmask))	/* retrieve & save signal mask */
2:
	movl	$0,%eax			/* return success */
	ret
1:
	pushq	%rsi
#ifdef PIC
	movq	PIC_GOT(HIDENAME(cerror)),%rdx
	jmp	*%rdx
#else
	jmp	HIDENAME(cerror)
#endif
END(_getcontext)
