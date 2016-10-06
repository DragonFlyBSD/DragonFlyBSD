/*
 * Copyright (c) 2015 Matthew Dillon, All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/ucontext.h>

#include <machine/frame.h>
#include <machine/tss.h>
#include <machine/segments.h>

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

/* Prototypes */

static void makectx_quick_wrapper(ucontext_t *ucp, uint64_t *stack_top);

/*
 * makecontext_quick() associates a stack with a user thread context
 * setup to execute a cofunc sequence.  The caller only initializes the
 * uc_stack.* fields, uc_cofunc, and uc_arg.  This function will zero or
 * initialize all other fields.  Upon return the caller can optionally
 * also initialize uc_link.
 *
 * These 'quick' calls do not mess with the signal mask and do not require
 * kernel intervention.  Scratch registers (including FP regs, which are also
 * scratch registers) are not saved or restored.  Cofunction loops also
 * optimize cofunc call loops by not saving the register state when
 * switching away to double performance.  Of course, swapcontext_quick()
 * still saves the register state.  There is no getcontext_quick() call
 * on purpose.
 */
void
_makecontext_quick(ucontext_t *ucp)
{
	uint64_t *stack_top;

	if (ucp == NULL)
		return;
	bzero(&ucp->uc_sigmask, sizeof(ucp->uc_sigmask));
	bzero(&ucp->uc_mcontext, sizeof(ucp->uc_mcontext));
	ucp->uc_link = NULL;
	ucp->uc_mcontext.mc_len = sizeof(mcontext_t);

	stack_top = (uint64_t *)(ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
	stack_top = (uint64_t *)((uint64_t)(stack_top) & ~63UL);
	stack_top -= 1;

	/*
	 * Set the machine context to point to the top of the
	 * stack and the program counter to the context start
	 * wrapper.  Note that setcontext() pushes the return
	 * address onto the top of the stack, so allow for this
	 * by adjusting the stack downward 1 slot.  Also set
	 * %rbp to point to the base of the stack where ucp
	 * is stored.
	 */
	ucp->uc_mcontext.mc_rdi = (register_t)ucp;
	ucp->uc_mcontext.mc_rsi = (register_t)stack_top;
	ucp->uc_mcontext.mc_rsp = (register_t)stack_top;
	ucp->uc_mcontext.mc_rip = (register_t)makectx_quick_wrapper;
	ucp->uc_mcontext.mc_ownedfp = _MC_FPOWNED_NONE;
	ucp->uc_mcontext.mc_fpformat = _MC_FPFMT_NODEV;
	ucp->uc_mcontext.mc_cs = GSEL(GUCODE_SEL, SEL_UPL);
	ucp->uc_mcontext.mc_ss = GSEL(GUDATA_SEL, SEL_UPL);
}

__weak_reference(_makecontext_quick, makecontext_quick);

/*
 * If the cofunc call returns set the context up to re-execute the
 * wrapper if the linkages eventually link back to this ucp.  The
 * cofunc can also change uc_cofunc and uc_arg as it desires, allowing
 * cofunctions to be optimally linked together.
 */
static void
makectx_quick_wrapper(ucontext_t *ucp, uint64_t *stack_top)
{
	for (;;) {
		ucp->uc_cofunc(ucp, ucp->uc_arg);
		if (ucp->uc_link == ucp)
			continue;
		ucp->uc_mcontext.mc_rdi = (register_t)ucp;
		ucp->uc_mcontext.mc_rsi = (register_t)stack_top;
		ucp->uc_mcontext.mc_rsp = (register_t)stack_top;
		ucp->uc_mcontext.mc_rip = (register_t)makectx_quick_wrapper;
		if (ucp->uc_link)
			setcontext_quick(ucp->uc_link);
		exit(0);
	}
}
