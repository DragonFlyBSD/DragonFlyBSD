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
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/ucontext.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "un-namespace.h"

/*
 * We need to block most signals during a context switch so we do not
 * dispatch a signal vector during a context switch.
 */
#if 0
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
#endif

/*
 * Save the calling context in (oucp) then switch to (ucp).
 */
int
_swapcontext(ucontext_t *oucp, const ucontext_t *ucp)
{
	int ret;

	if (getcontext(oucp) == 0) {
		ret = sigreturn(__DECONST(ucontext_t *, ucp));
	} else {
		ret = 0;
	}
	return(ret);
}

/*
 * Switch to the target context, use sigreturn() to properly restore
 * everything, including rflags and to avoid scribbling over the
 * target stack's red-zone.
 *
 * Note that setcontext() can be called with a ucontext from a signal,
 * so the signal state must be restored and there is really no way to
 * avoid making a system call :-(
 */
int
_setcontext(const ucontext_t *ucp)
{
	int ret;

	/* XXX: shouldn't sigreturn() take const? or does it modify ucp? */
	ret = sigreturn(__DECONST(ucontext_t *, ucp));

	return(ret);
}

__weak_reference(_swapcontext, swapcontext);
__weak_reference(_setcontext, setcontext);
