
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
 * $DragonFly: src/sys/platform/vkernel/i386/exception.c,v 1.8 2007/07/01 03:04:14 dillon Exp $
 */

#include "opt_ddb.h"
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/reboot.h>
#include <ddb/ddb.h>

#include <sys/thread2.h>

#include <machine/trap.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/cpu.h>

#include <err.h>
#include <signal.h>
#include <unistd.h>

int _ucodesel = LSEL(LUCODE_SEL, SEL_UPL);
int _udatasel = LSEL(LUDATA_SEL, SEL_UPL);

static void exc_segfault(int signo, siginfo_t *info, void *ctx);
#ifdef DDB
static void exc_debugger(int signo, siginfo_t *info, void *ctx);
#endif

/*
 * IPIs are 'fast' interrupts, so we deal with them directly from our
 * signal handler.
 */

#ifdef SMP

static
void
ipisig(int nada, siginfo_t *info, void *ctxp)
{
	++mycpu->gd_intr_nesting_level;
	if (curthread->td_pri < TDPRI_CRIT) {
		curthread->td_pri += TDPRI_CRIT;
		lwkt_process_ipiq();
		curthread->td_pri -= TDPRI_CRIT;
	} else {
		need_ipiq();
	}
	--mycpu->gd_intr_nesting_level;
}

#endif

void
init_exceptions(void)
{
	struct sigaction sa;

	bzero(&sa, sizeof(sa));
	sa.sa_sigaction = exc_segfault;
	sa.sa_flags |= SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGTRAP, &sa, NULL);

#ifdef DDB
	sa.sa_sigaction = exc_debugger;
	sigaction(SIGQUIT, &sa, NULL);
#endif
#ifdef SMP
	sa.sa_sigaction = ipisig;
	sigaction(SIGUSR1, &sa, NULL);
#endif
}

/*
 * This function handles a segmentation fault.  
 *
 * XXX We assume that trapframe is a subset of ucontext.  It is as of
 *     this writing.
 */
static void
exc_segfault(int signo, siginfo_t *info, void *ctxp)
{
	ucontext_t *ctx = ctxp;

#if 0
	kprintf("CAUGHT SEGFAULT EIP %08x ERR %08x TRAPNO %d err %d\n",
		ctx->uc_mcontext.mc_eip,
		ctx->uc_mcontext.mc_err,
		ctx->uc_mcontext.mc_trapno & 0xFFFF,
		ctx->uc_mcontext.mc_trapno >> 16);
#endif
	kern_trap((struct trapframe *)&ctx->uc_mcontext.mc_gs);
	splz();
}

#ifdef DDB

static void
exc_debugger(int signo, siginfo_t *info, void *ctx)
{
	Debugger("interrupt from console");
}

#endif
