/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1990 William Jolitz.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
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
 * from: @(#)npx.c	7.2 (Berkeley) 5/12/91
 * $FreeBSD: src/sys/i386/isa/npx.c,v 1.80.2.3 2001/10/20 19:04:38 tegge Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/signalvar.h>
#include <sys/thread2.h>

#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/globaldata.h>

#define	fldcw(addr)		__asm("fldcw %0" : : "m" (*(addr)))
#define	fnclex()		__asm("fnclex")
#define	fninit()		__asm("fninit")
#define	fnop()			__asm("fnop")
#define	fnsave(addr)		__asm __volatile("fnsave %0" : "=m" (*(addr)))
#define	fnstcw(addr)		__asm __volatile("fnstcw %0" : "=m" (*(addr)))
#define	fnstsw(addr)		__asm __volatile("fnstsw %0" : "=m" (*(addr)))
#define	frstor(addr)		__asm("frstor %0" : : "m" (*(addr)))
#define	fxrstor(addr)		__asm("fxrstor64 %0" : : "m" (*(addr)))
#define	fxsave(addr)		__asm __volatile("fxsave64 %0" : "=m" (*(addr)))
#define	ldmxcsr(csr)		__asm __volatile("ldmxcsr %0" : : "m" (csr))

static	void	fpu_clean_state(void);
/*static	int	npx_attach	(device_t dev);*/

static struct krate badfprate = { 1 };

int cpu_fxsr = 0;
uint32_t npx_mxcsr_mask = 0xFFBF;

int mmxopt = 1;
SYSCTL_INT(_kern, OID_AUTO, mmxopt, CTLFLAG_RD, &mmxopt, 0,
	"MMX/XMM optimized bcopy/copyin/copyout support");

static int hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
	&hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");

#if 0
/*
 * Attach routine - announce which it is, and wire into system
 */
static int
npx_attach(device_t dev)
{
	npxinit();
	return (0);
}
#endif

void
init_fpu(int supports_sse)
{
	cpu_fxsr = hw_instruction_sse = supports_sse;
	npxprobemask();
}

/*
 * Probe the npx_mxcsr_mask
 */
void npxprobemask(void)
{
	/*64-Byte alignment required for xsave*/
	static union savefpu dummy __aligned(64);

	crit_enter();
	/*stop_emulating();*/
	fxsave(&dummy);
	npx_mxcsr_mask = ((uint32_t *)&dummy)[7];
	/*stop_emulating();*/
	crit_exit();
}


/*
 * Initialize the floating point unit.
 */
void npxinit(void)
{
	static union savefpu dummy __aligned(16);
	u_short control = __INITIAL_FPUCW__;
	u_int mxcsr = __INITIAL_MXCSR__;

	/*
	 * fninit has the same h/w bugs as fnsave.  Use the detoxified
	 * fnsave to throw away any junk in the fpu.  npxsave() initializes
	 * the fpu and sets npxthread = NULL as important side effects.
	 */
	npxsave(&dummy);
	crit_enter();
	/*stop_emulating();*/
	fldcw(&control);
	ldmxcsr(mxcsr);
	fpusave(curthread->td_savefpu, 0);
	mdcpu->gd_npxthread = NULL;
	/*start_emulating();*/
	crit_exit();
}

/*
 * Free coprocessor (if we have it).
 */
void
npxexit(void)
{
	if (curthread == mdcpu->gd_npxthread)
		npxsave(curthread->td_savefpu);
}


/*
 * Implement the device not available (DNA) exception.  gd_npxthread had
 * better be NULL.  Restore the current thread's FP state and set gd_npxthread
 * to curthread.
 *
 * Interrupts are enabled and preemption can occur.  Enter a critical
 * section to stabilize the FP state.
 */
int
npxdna(struct trapframe *frame)
{
	thread_t td = curthread;
	int didinit = 0;

	if (mdcpu->gd_npxthread != NULL) {
		kprintf("npxdna: npxthread = %p, curthread = %p\n",
			mdcpu->gd_npxthread, td);
		panic("npxdna");
	}

	/*
	 * Setup the initial saved state if the thread has never before
	 * used the FP unit.  This also occurs when a thread pushes a
	 * signal handler and uses FP in the handler.
	 */
	if ((curthread->td_flags & TDF_USINGFP) == 0) {
		curthread->td_flags |= TDF_USINGFP;
		npxinit();
		didinit = 1;
	}

	/*
	 * The setting of gd_npxthread and the call to fpurstor() must not
	 * be preempted by an interrupt thread or we will take an npxdna
	 * trap and potentially save our current fpstate (which is garbage)
	 * and then restore the garbage rather then the originally saved
	 * fpstate.
	 */
	crit_enter();
	/*stop_emulating();*/
	/*
	 * Record new context early in case frstor causes an IRQ13.
	 */
	mdcpu->gd_npxthread = td;
	/*
	 * The following frstor may cause an IRQ13 when the state being
	 * restored has a pending error.  The error will appear to have been
	 * triggered by the current (npx) user instruction even when that
	 * instruction is a no-wait instruction that should not trigger an
	 * error (e.g., fnclex).  On at least one 486 system all of the
	 * no-wait instructions are broken the same as frstor, so our
	 * treatment does not amplify the breakage.  On at least one
	 * 386/Cyrix 387 system, fnclex works correctly while frstor and
	 * fnsave are broken, so our treatment breaks fnclex if it is the
	 * first FPU instruction after a context switch.
	 */
	if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~npx_mxcsr_mask) && cpu_fxsr) {
		td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= npx_mxcsr_mask;
		lwpsignal(curproc, curthread->td_lwp, SIGFPE);
	}
	fpurstor(curthread->td_savefpu, 0);
	crit_exit();

	return (1);
}

/*
 * Wrapper for the fnsave instruction to handle h/w bugs.  If there is an error
 * pending, then fnsave generates a bogus IRQ13 on some systems.  Force
 * any IRQ13 to be handled immediately, and then ignore it.  This routine is
 * often called at splhigh so it must not use many system services.  In
 * particular, it's much easier to install a special handler than to
 * guarantee that it's safe to use npxintr() and its supporting code.
 *
 * WARNING!  This call is made during a switch and the MP lock will be
 * setup for the new target thread rather then the current thread, so we
 * cannot do anything here that depends on the *_mplock() functions as
 * we may trip over their assertions.
 *
 * WARNING!  When using fxsave we MUST fninit after saving the FP state.  The
 * kernel will always assume that the FP state is 'safe' (will not cause
 * exceptions) for mmx/xmm use if npxthread is NULL.  The kernel must still
 * setup a custom save area before actually using the FP unit, but it will
 * not bother calling fninit.  This greatly improves kernel performance when
 * it wishes to use the FP unit.
 */
void
npxsave(union savefpu *addr)
{
	crit_enter();
	/*stop_emulating();*/
	fpusave(addr, 0);
	mdcpu->gd_npxthread = NULL;
	fninit();
	/*start_emulating();*/
	crit_exit();
}

void
fpusave(union savefpu *addr, uint64_t mask __unused)
{
	if (cpu_fxsr)
		fxsave(addr);
	else
		fnsave(addr);
}

/*
 * Save the FP state to the mcontext structure.
 *
 * WARNING: If you want to try to npxsave() directly to mctx->mc_fpregs,
 * then it MUST be 16-byte aligned.  Currently this is not guarenteed.
 */
void
npxpush(mcontext_t *mctx)
{
	thread_t td = curthread;

	if (td->td_flags & TDF_USINGFP) {
		if (mdcpu->gd_npxthread == td) {
			/*
			 * XXX Note: This is a bit inefficient if the signal
			 * handler uses floating point, extra faults will
			 * occur.
			 */
			mctx->mc_ownedfp = _MC_FPOWNED_FPU;
			npxsave(td->td_savefpu);
		} else {
			mctx->mc_ownedfp = _MC_FPOWNED_PCB;
		}
		bcopy(td->td_savefpu, mctx->mc_fpregs, sizeof(mctx->mc_fpregs));
		td->td_flags &= ~TDF_USINGFP;
		mctx->mc_fpformat = cpu_fxsr ? _MC_FPFMT_XMM : _MC_FPFMT_387;
	} else {
		mctx->mc_ownedfp = _MC_FPOWNED_NONE;
		mctx->mc_fpformat = _MC_FPFMT_NODEV;
	}
}

/*
 * Restore the FP state from the mcontext structure.
 */
void
npxpop(mcontext_t *mctx)
{
	thread_t td = curthread;

	switch(mctx->mc_ownedfp) {
	case _MC_FPOWNED_NONE:
		/*
		 * If the signal handler used the FP unit but the interrupted
		 * code did not, release the FP unit.  Clear TDF_USINGFP will
		 * force the FP unit to reinit so the interrupted code sees
		 * a clean slate.
		 */
		if (td->td_flags & TDF_USINGFP) {
			if (td == mdcpu->gd_npxthread)
				npxsave(td->td_savefpu);
			td->td_flags &= ~TDF_USINGFP;
		}
		break;
	case _MC_FPOWNED_FPU:
	case _MC_FPOWNED_PCB:
		/*
		 * Clear ownership of the FP unit and restore our saved state.
		 *
		 * NOTE: The signal handler may have set-up some FP state and
		 * enabled the FP unit, so we have to restore no matter what.
		 *
		 * XXX: This is bit inefficient, if the code being returned
		 * to is actively using the FP this results in multiple
		 * kernel faults.
		 *
		 * WARNING: The saved state was exposed to userland and may
		 * have to be sanitized to avoid a GP fault in the kernel.
		 */
		if (td == mdcpu->gd_npxthread)
			npxsave(td->td_savefpu);
		bcopy(mctx->mc_fpregs, td->td_savefpu, sizeof(*td->td_savefpu));
		if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~npx_mxcsr_mask) &&
		    cpu_fxsr) {
			krateprintf(&badfprate,
				    "pid %d (%s) signal return from user: "
				    "illegal FP MXCSR %08x\n",
				    td->td_proc->p_pid,
				    td->td_proc->p_comm,
				    td->td_savefpu->sv_xmm.sv_env.en_mxcsr);
			td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= npx_mxcsr_mask;
		}
		td->td_flags |= TDF_USINGFP;
		break;
	}
}

/*
 * On AuthenticAMD processors, the fxrstor instruction does not restore
 * the x87's stored last instruction pointer, last data pointer, and last
 * opcode values, except in the rare case in which the exception summary
 * (ES) bit in the x87 status word is set to 1.
 *
 * In order to avoid leaking this information across processes, we clean
 * these values by performing a dummy load before executing fxrstor().
 */
static void
fpu_clean_state(void)
{
	u_short status;

	/*
	 * Clear the ES bit in the x87 status word if it is currently
	 * set, in order to avoid causing a fault in the upcoming load.
	 */
	fnstsw(&status);
	if (status & 0x80)
		fnclex();

	/*
	 * Load the dummy variable into the x87 stack.  This mangles
	 * the x87 stack, but we don't care since we're about to call
	 * fxrstor() anyway.
	 */
	__asm __volatile("ffree %st(7); fldz");
}

void
fpurstor(union savefpu *addr, uint64_t mask __unused)
{
	if (cpu_fxsr) {
		fpu_clean_state();
		fxrstor(addr);
	} else {
		frstor(addr);
	}
}
