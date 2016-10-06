/*
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 * Copyright (c) 2012 Markus Pfeiffer <markus.pfeiffer@morphism.de>
 * All rights reserved.
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
 *
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

typedef void (*func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

/* Prototypes */
static void makectx_wrapper(ucontext_t *ucp, func_t func, uint64_t *args);

/*
 * makecontext() associates a stack with a user thread context and sets
 * up to call the start function when switched to.  The start function
 * returns to _ctx_start which then calls _ctx_done to terminate the
 * context.
 */
void
_makecontext(ucontext_t *ucp, void (*start)(void), int argc, ...)
{
	va_list		ap;
	uint64_t	*stack_top;
	uint64_t	*argp;
	int		i;

	if (ucp == NULL)
		return;

	/*
	 * Invalidate a context which did not have a stack associated with
	 * it or for which the stack was too small.  The stack check is
	 * kinda silly, though, since we have no control over the stack
	 * usage of the code being set up to run.
	 */
	if ((ucp->uc_stack.ss_sp == NULL) ||
	    (ucp->uc_stack.ss_size < MINSIGSTKSZ)) {
		ucp->uc_mcontext.mc_len = 0;
	}
	if (argc < 0 || argc > NCARGS)
		ucp->uc_mcontext.mc_len = 0;

	if (ucp->uc_mcontext.mc_len == sizeof(mcontext_t)) {
		/*
		 */
		stack_top = (uint64_t *)(ucp->uc_stack.ss_sp +
                                         ucp->uc_stack.ss_size);
		stack_top = (uint64_t *)((uint64_t)(stack_top) & ~15UL);

		argp = stack_top - 6;
		stack_top -= 7;

		/* Add all the arguments: */
		va_start(ap, argc);
		for (i = 0; i < argc; i++) {
			argp[i] = va_arg(ap, uint64_t);
		}
		va_end(ap);
		/* The first six arguments are passed via registers. */
		for (i = argc; i < 6; i++) {
			argp[i] = 0;
		}

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
            	ucp->uc_mcontext.mc_rsi = (register_t)start;
            	ucp->uc_mcontext.mc_rdx = (register_t)argp;
            	ucp->uc_mcontext.mc_rbp = 0;
            	ucp->uc_mcontext.mc_rbx = (register_t)stack_top;
		ucp->uc_mcontext.mc_rsp = (register_t)stack_top;
		ucp->uc_mcontext.mc_rip = (register_t)makectx_wrapper;
		ucp->uc_mcontext.mc_ownedfp = _MC_FPOWNED_NONE;
		ucp->uc_mcontext.mc_fpformat = _MC_FPFMT_NODEV;
		ucp->uc_mcontext.mc_cs = GSEL(GUCODE_SEL, SEL_UPL);
		ucp->uc_mcontext.mc_ss = GSEL(GUDATA_SEL, SEL_UPL);
		ucp->uc_mcontext.mc_onstack = 0;
		ucp->uc_mcontext.mc_err = 0;
	}
}

__weak_reference(_makecontext, makecontext);

/* */
static void
makectx_wrapper(ucontext_t *ucp, func_t func, uint64_t *args)
{
	(*func)(args[0], args[1], args[2], args[3], args[4], args[5]);
	if (ucp->uc_link == NULL)
		exit(0);

	setcontext((const ucontext_t *)ucp->uc_link);

	/* should never reach this */
	abort();
}
