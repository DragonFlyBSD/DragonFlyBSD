/*
 * Copyright (c) 2007 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */
/*
 * swapcontext_quick() and setcontext_quick().  There is *NO*
 * getcontext_quick() routine on purpose.
 *
 * Quick routines are not required to save or resetore scratch registers,
 * FP regs (which are scratch), or flags.
 */
#include <machine/asm.h>
#include <cpu/specialreg.h>
#include <asmcontext.h>

	/*
	 * void setcontext_quick(ucontext_t *ucp)
	 *
	 * Load the register context, effectively switching to the
	 * new context.
	 */
        .weak   setcontext_quick
        .set    setcontext_quick,_setcontext_quick
ENTRY(_setcontext_quick)
	addq	$UC_MCONTEXT,%rdi

	/*	MC_ONSTACK(%rdi)	*/
	/*	MC_RDI(%rdi)	- see below */
	movq	MC_RSI(%rdi),%rsi
	movq	MC_RDX(%rdi),%rdx
	movq	MC_R8(%rdi),%r8
	movq	MC_R9(%rdi),%r9
	/*	MC_RAX(%rdi)	- see below */
	movq	MC_RBX(%rdi),%rbx
	movq	MC_RCX(%rdi),%rcx
	movq	MC_RBP(%rdi),%rbp
	movq	MC_R10(%rdi),%r10
	movq	MC_R11(%rdi),%r11
	movq	MC_R12(%rdi),%r12
	movq	MC_R13(%rdi),%r13
	movq	MC_R14(%rdi),%r14
	movq	MC_R15(%rdi),%r15
	/*	MC_TRAPNO(%rdi)	*/
	/*	MC_ADDR(%rdi)	*/
	/*	MC_FLAGS(%rdi)  */
	/*	MC_ERR(%rdi)	*/
	/*	MC_RIP(%rdi)	- see below */
	/*	MC_CS(%rdi)	*/
	/*	MC_RFLAGS(%rdi)	*/
	/*	MC_RSP(%rdi)	- see below */
	/*	MC_SS(%rdi)	*/

	/*
	 * Load the new stack pointer
	 */
	movq	MC_RSP(%rdi),%rsp

	/*
	 * Use %rax to hold the return pc.  function returns void.  Do
	 * not mess with the target stack (particularly not the red-zone!).
	 */
	movq	MC_RIP(%rdi),%rax

	/*
	 * Finally rdi
	 */
	movq	MC_RDI(%rdi),%rdi
	jmp	*%rax
END(_setcontext_quick)


	/*
	 * void swapcontext_quick(ucontex_t *oucp, ucontext_t *nucp)
	 *
	 * Save the current state in oucp and switch to nucp.
	 */
        .weak   swapcontext_quick
        .set    swapcontext_quick,_swapcontext_quick
ENTRY(_swapcontext_quick)
	/*
	 * Save
	 */
	addq	$UC_MCONTEXT,%rdi
	popq	%rax			/* return pc */

	movq	%rsi,MC_RSI(%rdi)
	movq	%rbx,MC_RBX(%rdi)
	movq	%rdx,MC_RDX(%rdi)
	movq	%rbp,MC_RBP(%rdi)
	movq	%r10,MC_R10(%rdi)
	movq	%r11,MC_R11(%rdi)
	movq	%r12,MC_R12(%rdi)
	movq	%r13,MC_R13(%rdi)
	movq	%r14,MC_R14(%rdi)
	movq	%r15,MC_R15(%rdi)
	movq	%rsp,MC_RSP(%rdi)
	movq	%rax,MC_RIP(%rdi)

	/*
	 * Restore (copy of setcontext above)
	 */
	movq	%rsi,%rdi
	addq	$UC_MCONTEXT,%rdi

	/*	MC_ONSTACK(%rdi)	*/
	/*	MC_RDI(%rdi)	- see below */
	movq	MC_RSI(%rdi),%rsi
	movq	MC_RDX(%rdi),%rdx
	/*	MC_RAX(%rdi)	- see below */
	movq	MC_RBX(%rdi),%rbx
	movq	MC_RCX(%rdi),%rcx
	movq	MC_RBP(%rdi),%rbp
	movq	MC_R10(%rdi),%r10
	movq	MC_R11(%rdi),%r11
	movq	MC_R12(%rdi),%r12
	movq	MC_R13(%rdi),%r13
	movq	MC_R14(%rdi),%r14
	movq	MC_R15(%rdi),%r15
	/*	MC_TRAPNO(%rdi)	*/
	/*	MC_ADDR(%rdi)	*/
	/*	MC_FLAGS(%rdi)  */
	/*	MC_ERR(%rdi)	*/
	/*	MC_RIP(%rdi)	- see below */
	/*	MC_CS(%rdi)	*/
	/*	MC_RFLAGS(%rdi)	*/
	/*	MC_RSP(%rdi)	- see below */
	/*	MC_SS(%rdi)	*/

	/*
	 * Load the new stack pointer
	 */
	movq	MC_RSP(%rdi),%rsp

	/*
	 * Use %rax to hold the return pc.  function returns void.  Do
	 * not mess with the target stack (particularly not the red-zone!).
	 */
	movq	MC_RIP(%rdi),%rax

	/*
	 * Finally rdi
	 */
	movq	MC_RDI(%rdi),%rdi
	jmp	*%rax

END(_swapcontext_quick)

	.section .note.GNU-stack,"",%progbits
