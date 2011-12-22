/*
 * Copyright (c) 2007 Matthew T. Emmerton <matt@gsicomp.on.ca>
 * All rights reserved.
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

#include <machine/asm.h>
#include <asmcontext.h>

	/*
	 * int get_mcontext(mcontext_t *mcp : rdi)
	 *
	 * Copy the caller's context into the mcontext, %rax excepted.
	 */
        .weak   get_mcontext
        .set    get_mcontext,_get_mcontext
ENTRY(_get_mcontext)
	/*	MC_ONSTACK(%rdi)	*/
	movq	%rdi,MC_RDI(%rdi)
	movq	%rsi,MC_RSI(%rdi)
	movq	%rdx,MC_RDX(%rdi)
	movq	%r8,MC_R8(%rdi)
	movq	%r9,MC_R9(%rdi)
	/* movq	%rax,MC_RAX(%rdi) - not needed, replaced below */
	movq	%rbx,MC_RBX(%rdi)
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
	/*	MC_RFLAGS(%rdi)	*/
	/*	MC_RSP(%rdi)	- see below */
	/*	MC_SS(%rdi)	*/

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


	/*
	 * int set_mcontext(mcontext_t *mcp)
	 *
	 * Load the register context, effectively switching to the
	 * new context.
	 */
        .weak   set_mcontext
        .set    set_mcontext,_set_mcontext
ENTRY(_set_mcontext)
	/*	MC_ONSTACK(%rdi)	*/
	/*	MC_RDI(%rdi)	- see below */
	movq	MC_RSI(%rdi),%rsi
	movq	MC_RDX(%rdi),%rdx
	movq	MC_R8(%rdi),%r8
	movq	MC_R9(%rdi),%r9
	/*	MC_RAX(%rdi)	- see below */
	movq	MC_RBX(%rdi),%rbx
	movq	MC_RBP(%rdi),%rbp
	movq	MC_R10(%rdi),%r10
	movq	MC_R11(%rdi),%r11
	movq	MC_R12(%rdi),%r12
	movq	MC_R13(%rdi),%r13
	movq	MC_R14(%rdi),%r14
	movq	MC_R15(%rdi),%r15
	/*	MC_TRAPNO(%rdi)	*/
	/*	MC_ADDR(%rdi)	*/
	/*	MC_FLAGS(%rdi)	*/
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
	 * Restore the flags
	 * XXX
	 */

	/*
	 * Push the return pc so we can 'ret' to it.
	 */
	pushq	MC_RIP(%rdi)

	/*
	 * Finally rax and rdi
	 */
	movq	MC_RAX(%rdi),%rax
	movq	MC_RDI(%rdi),%rdi
	retq

