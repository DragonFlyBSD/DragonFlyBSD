/*
 * Copyright (c) 2007 Matthew T. Emmerton <matt@gsicomp.on.ca>
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

#include "namespace.h"
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/ucontext.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "un-namespace.h"

/* Prototypes */
__weak_reference(_swapcontext, swapcontext);
__weak_reference(_setcontext, setcontext);

int get_mcontext(mcontext_t *);
int set_mcontext(const mcontext_t *);

/*
 * We need to block most signals during a context switch so we do not
 * dispatch a signal vector during a context switch.
 */
static sigset_t sigset_block_all;

static void __sigset_block_all_setup(void) __attribute__ ((constructor));

static void
__sigset_block_all_setup(void)
{
	sigfillset(&sigset_block_all);
	sigdelset(&sigset_block_all, SIGSEGV);
	sigdelset(&sigset_block_all, SIGBUS);
	sigdelset(&sigset_block_all, SIGILL);
}

/*
 * Save the calling context in (oucp) then switch to (ucp).
 *
 * Block all signals while switching contexts.  get_mcontext() returns zero
 * when retrieving a context.
 *
 * When some other thread calls set_mcontext() to resume our thread, 
 * the resume point causes get_mcontext() to return non-zero to us.
 * Signals will be blocked and we must restore the signal mask before
 * returning.
 */
int
_swapcontext(ucontext_t *oucp, const ucontext_t *ucp)
{
	int ret;

	ret = _sigprocmask(SIG_BLOCK, &sigset_block_all, &oucp->uc_sigmask);
	if (ret == 0) {
		if (get_mcontext(&oucp->uc_mcontext) == 0) {
			ret = set_mcontext(&ucp->uc_mcontext);
		} else {
			ret = _sigprocmask(SIG_SETMASK, &oucp->uc_sigmask, NULL);
		}
	}
	return(ret);
}

/*
 * Switch to the target context.  The current signal mask is saved in ucp
 * and all signals are blocked.  The call to set_mcontext() causes the
 * specified context to be switched to (usually resuming as a return from
 * the get_mcontext() procedure).  The current context is thrown away.
 *
 * The target context being resumed is responsible for restoring the
 * signal mask appropriate for the target context.
 */
int
_setcontext(ucontext_t *ucp)
{
	int ret;

	ret = _sigprocmask(SIG_BLOCK, &sigset_block_all, &ucp->uc_sigmask);
	if (ret == 0)
		ret = set_mcontext(&ucp->uc_mcontext);
	return(ret);
}

