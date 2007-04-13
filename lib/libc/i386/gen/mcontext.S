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
 *
 * $DragonFly: src/lib/libc/i386/gen/mcontext.S,v 1.2 2007/04/13 12:12:27 corecode Exp $
 */

#include <machine/asm.h>
#include <asmcontext.h>

	/*
	 * int get_mcontext(mcontext_t *mcp)
	 *
	 * Copy the caller's context into the mcontext, %eax excepted.
	 */
        .weak   get_mcontext
        .set    get_mcontext,_get_mcontext
ENTRY(_get_mcontext)
	movl	4(%esp),%eax
	/*	mc_onstack,0*4(%eax)	*/
	movw	%gs,1*4(%eax)
	movw	%fs,2*4(%eax)
	movw	%es,3*4(%eax)
	/*	%ds,4*4(%eax)	*/
	movl	%edi,5*4(%eax)
	movl	%esi,6*4(%eax)
	movl	%ebp,7*4(%eax)
	/*	%isp,8*4(%eax)	*/
	movl	%ebx,9*4(%eax)
	movl	%edx,10*4(%eax)
	movl	%ecx,11*4(%eax)
	/*	mc_eax:		12*4(%eax)	*/
	/*	mc_xflags:	13*4(%eax)	*/
	/*	mc_trapno:	14*4(%eax)	*/
	/*	mc_err:		15*4(%eax)	*/
	/*	mc_eip:		16*4(%eax)	*/
	/*	mc_cs:		17*4(%eax)	*/
	/*	mc_eflags:	18*4(%eax)	*/
	/*	mc_esp:		19*4(%eax)	*/

	/*
	 * Saved stack pointer as if we had returned from this
	 * procedure.
	 */
	movl	%esp,19*4(%eax)
	addl	$4,19*4(%eax)

	/*
	 * Save eflags
	 */
	pushfl
	popl	18*4(%eax)

	/*
	 * Saved instruction pointer as if we had returned from
	 * this procedure.
	 */
	movl	(%esp),%edx
	movl	%edx,16*4(%eax)

	/*
	 * On restore as if procedure returned the value 1
	 */
	movl	$1,12*4(%eax)	

	/*
	 * Set MC_LEN
	 */
	movl    $SIZEOF_MCONTEXT_T,MC_LEN(%eax)

	/*
	 * Return 0
	 */
	xorl	%eax,%eax
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
	movl	4(%esp),%eax
	/*	0*4(%eax),mc_onstack	*/
	movw	1*4(%eax),%gs
	movw	2*4(%eax),%fs
	movw	3*4(%eax),%es
	/*	4*4(%eax),%ds	*/
	movl	5*4(%eax),%edi
	movl	6*4(%eax),%esi
	movl	7*4(%eax),%ebp
	/*	8*4(%eax),%isp	*/
	movl	9*4(%eax),%ebx
	movl	10*4(%eax),%edx
	movl	11*4(%eax),%ecx
	/*	mc_eax:		12*4(%eax)	*/
	/*	mc_xflags:	13*4(%eax)	*/
	/*	mc_trapno:	14*4(%eax)	*/
	/*	mc_err:		15*4(%eax)	*/
	/*	mc_eip:		16*4(%eax)	*/
	/*	mc_cs:		17*4(%eax)	*/
	/*	mc_eflags:	18*4(%eax)	*/
	/*	mc_esp:		19*4(%eax)	*/

	/*
	 * Load the new stack pointer
	 */
	movl	19*4(%eax),%esp

	/*
	 * Restore the flags
	 */
	pushl	18*4(%eax)
	popfl

	/*
	 * Push the return pc so we can 'ret' to it.
	 */
	pushl	16*4(%eax)

	/*
	 * Load %eax last, and return.
	 */
	movl	12*4(%eax),%eax
	ret
