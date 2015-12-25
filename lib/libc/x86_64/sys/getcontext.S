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
#include <cpu/specialreg.h>
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
	movq	$0,UC_LINK(%rdi)	/* make sure UC_LINK is clear */
	movq	%rdi,%r9		/* save ucontext in %r9 */
	movq	(%rsp),%r8		/* save return PC in %r8 */
	addq	$UC_MCONTEXT,%rdi

	movq	$0,MC_ONSTACK(%rdi)	/*	MC_ONSTACK(%rdi)	*/
	movq	%rdi,MC_RDI(%rdi)
	movq	%rsi,MC_RSI(%rdi)
	movq	%rdx,MC_RDX(%rdi)
	movq	%r8,MC_R8(%rdi)
	movq	%r9,MC_R9(%rdi)
	/* movq	%rax,MC_RAX(%rdi) - not needed, replaced below */
	movq	%rbx,MC_RBX(%rdi)
	/* movq %rcx,MC_RCX(%rdi) - not needed, scratch */
	movq	%rbp,MC_RBP(%rdi)
	movq	%r10,MC_R10(%rdi)
	movq	%r11,MC_R11(%rdi)
	movq	%r12,MC_R12(%rdi)
	movq	%r13,MC_R13(%rdi)
	movq	%r14,MC_R14(%rdi)
	movq	%r15,MC_R15(%rdi)
	/*	MC_TRAPNO(%rdi)	*/
	/*	MC_ADDR(%rdi)	*/
	/*	MC_FLAGS(%rdi)	*/
	/*	MC_ERR(%rdi)	*/
	/*	MC_RIP(%rdi)	- see below */
	/*	MC_CS(%rdi)	*/
	movq	$KUCSEL,MC_CS(%rdi)

	pushfq
	popq	MC_RFLAGS(%rdi)
	/*	MC_RSP(%rdi)	- see below */
	movq	$KUDSEL,MC_SS(%rdi)

	/*
	 * FP registers are scratch, do not save or restore, but make
	 * sure the context can be restored by a signal handler (where
	 * they might not be) by properly initializing the FP control
	 * fields.
	 */
	movl	$_MC_FPOWNED_NONE,MC_OWNEDFP(%rdi)
	movl	$_MC_FPFMT_NODEV,MC_FPFORMAT(%rdi)
#if 0
	movl	$_MC_FPFMT_YMM,MC_FPFORMAT(%rdi)
	movq	%rdi,%rsi
	movq	$CPU_XFEATURE_X87 | CPU_XFEATURE_SSE | CPU_XFEATURE_YMM,%rax
	movq	$0,%rdx
	xsave	MC_FPREGS(%rsi)
	movq	%rsi,%rdi
#endif

	/*
	 * Saved stack pointer as if we had returned from this
	 * procedure.
	 */
	movq	%rsp,MC_RSP(%rdi)
	addq	$8,MC_RSP(%rdi)

	/*
	 * Save rflags
	 * XXX
	 */

	/*
	 * Saved instruction pointer as if we had returned from
	 * this procedure.
	 */
	movq	(%rsp),%rdx
	movq	%rdx,MC_RIP(%rdi)

	/*
	 * On restore as if procedure returned the value 1
	 */
	movq	$1,MC_RAX(%rdi)

	/*
	 * Set MC_LEN
	 */
	movl    $SIZEOF_MCONTEXT_T,MC_LEN(%rdi)

	/*
	 * Return 0
	 */
	subq	%rax,%rax
	ret

#if 0
1:
	pushq	%rsi
#ifdef PIC
	movq	PIC_GOT(HIDENAME(cerror)),%rdx
	jmp	*%rdx
#else
	jmp	HIDENAME(cerror)
#endif
#endif
END(_getcontext)

	.section .note.GNU-stack,"",%progbits
