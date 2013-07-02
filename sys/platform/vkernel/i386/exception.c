
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
 */

#include "opt_ddb.h"
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <ddb/ddb.h>

#include <sys/thread2.h>

#include <machine/trap.h>
#include <machine/md_var.h>
#include <machine/segments.h>
#include <machine/cpu.h>
#include <machine/smp.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
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
 *
 * WARNING: Signals are not physically disabled here so we have to enter
 * our critical section before bumping gd_intr_nesting_level or another
 * interrupt can come along and get really confused.
 */
static
void
ipisig(int nada, siginfo_t *info, void *ctxp)
{
	if (curthread->td_critcount == 0) {
		++curthread->td_critcount;
		++mycpu->gd_intr_nesting_level;
		lwkt_process_ipiq();
		--mycpu->gd_intr_nesting_level;
		--curthread->td_critcount;
	} else {
		need_ipiq();
	}
}

/*
 * Unconditionally stop or restart a cpu.
 *
 * Note: cpu_mask_all_signals() masks all signals except SIGXCPU itself.
 * SIGXCPU itself is blocked on entry to stopsig() by the signal handler
 * itself.
 *
 * WARNING: Signals are not physically disabled here so we have to enter
 * our critical section before bumping gd_intr_nesting_level or another
 * interrupt can come along and get really confused.
 */
static
void
stopsig(int nada, siginfo_t *info, void *ctxp)
{
	sigset_t ss;

	sigemptyset(&ss);
	sigaddset(&ss, SIGALRM);
	sigaddset(&ss, SIGIO);
	sigaddset(&ss, SIGQUIT);
	sigaddset(&ss, SIGUSR1);
	sigaddset(&ss, SIGUSR2);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGWINCH);

	++curthread->td_critcount;
	++mycpu->gd_intr_nesting_level;
	while (stopped_cpus & mycpu->gd_cpumask) {
		sigsuspend(&ss);
	}
	--mycpu->gd_intr_nesting_level;
	--curthread->td_critcount;
}

#if 0

/*
 * SIGIO is used by cothreads to signal back into the virtual kernel.
 */
static
void
iosig(int nada, siginfo_t *info, void *ctxp)
{
	signalintr(4);
}

#endif

static
void
infosig(int nada, siginfo_t *info, void *ctxp)
{
       ucontext_t *ctx = ctxp;
       char buf[256];

       snprintf(buf, sizeof(buf), "lwp %d pc=%p sp=%p\n",
               (int)lwp_gettid(),
               (void *)(intptr_t)ctx->uc_mcontext.mc_eip,
               (void *)(intptr_t)ctx->uc_mcontext.mc_esp);
       write(2, buf, strlen(buf));
}

void
init_exceptions(void)
{
	struct sigaction sa;

	bzero(&sa, sizeof(sa));
	sa.sa_sigaction = exc_segfault;
	sa.sa_flags |= SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGTRAP, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);

	sa.sa_flags &= ~SA_NODEFER;

#ifdef DDB
	sa.sa_sigaction = exc_debugger;
	sigaction(SIGQUIT, &sa, NULL);
#endif
	sa.sa_sigaction = ipisig;
	sigaction(SIGUSR1, &sa, NULL);
	sa.sa_sigaction = stopsig;
	sigaction(SIGXCPU, &sa, NULL);
#if 0
	sa.sa_sigaction = iosig;
	sigaction(SIGIO, &sa, NULL);
#endif
	sa.sa_sigaction = infosig;
	sigaction(SIGINFO, &sa, NULL);
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
