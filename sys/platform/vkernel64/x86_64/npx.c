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
#ifndef CPU_DISABLE_SSE
#define	fxrstor(addr)		__asm("fxrstor %0" : : "m" (*(addr)))
#define	fxsave(addr)		__asm __volatile("fxsave %0" : "=m" (*(addr)))
#endif

typedef u_char bool_t;
#ifndef CPU_DISABLE_SSE
static	void	fpu_clean_state(void);
#define ldmxcsr(csr)            __asm __volatile("ldmxcsr %0" : : "m" (csr))
#endif

int cpu_fxsr = 0;

static struct krate badfprate = { 1 };

/*static	int	npx_attach	(device_t dev);*/
static	void	fpusave		(union savefpu *);
static	void	fpurstor	(union savefpu *);

uint32_t npx_mxcsr_mask = 0xFFBF;

#ifndef CPU_DISABLE_SSE
int mmxopt = 1;
SYSCTL_INT(_kern, OID_AUTO, mmxopt, CTLFLAG_RD, &mmxopt, 0,
	"MMX/XMM optimized bcopy/copyin/copyout support");
#endif

static int      hw_instruction_sse;
SYSCTL_INT(_hw, OID_AUTO, instruction_sse, CTLFLAG_RD,
    &hw_instruction_sse, 0, "SIMD/MMX2 instructions available in CPU");

#if 0
/*
 * Attach routine - announce which it is, and wire into system
 */
int
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
	fpusave(curthread->td_savefpu);
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

#if 0
/*
 * The following mechanism is used to ensure that the FPE_... value
 * that is passed as a trapcode to the signal handler of the user
 * process does not have more than one bit set.
 *
 * Multiple bits may be set if the user process modifies the control
 * word while a status word bit is already set.  While this is a sign
 * of bad coding, we have no choise than to narrow them down to one
 * bit, since we must not send a trapcode that is not exactly one of
 * the FPE_ macros.
 *
 * The mechanism has a static table with 127 entries.  Each combination
 * of the 7 FPU status word exception bits directly translates to a
 * position in this table, where a single FPE_... value is stored.
 * This FPE_... value stored there is considered the "most important"
 * of the exception bits and will be sent as the signal code.  The
 * precedence of the bits is based upon Intel Document "Numerical
 * Applications", Chapter "Special Computational Situations".
 *
 * The macro to choose one of these values does these steps: 1) Throw
 * away status word bits that cannot be masked.  2) Throw away the bits
 * currently masked in the control word, assuming the user isn't
 * interested in them anymore.  3) Reinsert status word bit 7 (stack
 * fault) if it is set, which cannot be masked but must be presered.
 * 4) Use the remaining bits to point into the trapcode table.
 *
 * The 6 maskable bits in order of their preference, as stated in the
 * above referenced Intel manual:
 * 1  Invalid operation (FP_X_INV)
 * 1a   Stack underflow
 * 1b   Stack overflow
 * 1c   Operand of unsupported format
 * 1d   SNaN operand.
 * 2  QNaN operand (not an exception, irrelavant here)
 * 3  Any other invalid-operation not mentioned above or zero divide
 *      (FP_X_INV, FP_X_DZ)
 * 4  Denormal operand (FP_X_DNML)
 * 5  Numeric over/underflow (FP_X_OFL, FP_X_UFL)
 * 6  Inexact result (FP_X_IMP)
 */
static char fpetable[128] = {
	0,
	FPE_FLTINV,	/*  1 - INV */
	FPE_FLTUND,	/*  2 - DNML */
	FPE_FLTINV,	/*  3 - INV | DNML */
	FPE_FLTDIV,	/*  4 - DZ */
	FPE_FLTINV,	/*  5 - INV | DZ */
	FPE_FLTDIV,	/*  6 - DNML | DZ */
	FPE_FLTINV,	/*  7 - INV | DNML | DZ */
	FPE_FLTOVF,	/*  8 - OFL */
	FPE_FLTINV,	/*  9 - INV | OFL */
	FPE_FLTUND,	/*  A - DNML | OFL */
	FPE_FLTINV,	/*  B - INV | DNML | OFL */
	FPE_FLTDIV,	/*  C - DZ | OFL */
	FPE_FLTINV,	/*  D - INV | DZ | OFL */
	FPE_FLTDIV,	/*  E - DNML | DZ | OFL */
	FPE_FLTINV,	/*  F - INV | DNML | DZ | OFL */
	FPE_FLTUND,	/* 10 - UFL */
	FPE_FLTINV,	/* 11 - INV | UFL */
	FPE_FLTUND,	/* 12 - DNML | UFL */
	FPE_FLTINV,	/* 13 - INV | DNML | UFL */
	FPE_FLTDIV,	/* 14 - DZ | UFL */
	FPE_FLTINV,	/* 15 - INV | DZ | UFL */
	FPE_FLTDIV,	/* 16 - DNML | DZ | UFL */
	FPE_FLTINV,	/* 17 - INV | DNML | DZ | UFL */
	FPE_FLTOVF,	/* 18 - OFL | UFL */
	FPE_FLTINV,	/* 19 - INV | OFL | UFL */
	FPE_FLTUND,	/* 1A - DNML | OFL | UFL */
	FPE_FLTINV,	/* 1B - INV | DNML | OFL | UFL */
	FPE_FLTDIV,	/* 1C - DZ | OFL | UFL */
	FPE_FLTINV,	/* 1D - INV | DZ | OFL | UFL */
	FPE_FLTDIV,	/* 1E - DNML | DZ | OFL | UFL */
	FPE_FLTINV,	/* 1F - INV | DNML | DZ | OFL | UFL */
	FPE_FLTRES,	/* 20 - IMP */
	FPE_FLTINV,	/* 21 - INV | IMP */
	FPE_FLTUND,	/* 22 - DNML | IMP */
	FPE_FLTINV,	/* 23 - INV | DNML | IMP */
	FPE_FLTDIV,	/* 24 - DZ | IMP */
	FPE_FLTINV,	/* 25 - INV | DZ | IMP */
	FPE_FLTDIV,	/* 26 - DNML | DZ | IMP */
	FPE_FLTINV,	/* 27 - INV | DNML | DZ | IMP */
	FPE_FLTOVF,	/* 28 - OFL | IMP */
	FPE_FLTINV,	/* 29 - INV | OFL | IMP */
	FPE_FLTUND,	/* 2A - DNML | OFL | IMP */
	FPE_FLTINV,	/* 2B - INV | DNML | OFL | IMP */
	FPE_FLTDIV,	/* 2C - DZ | OFL | IMP */
	FPE_FLTINV,	/* 2D - INV | DZ | OFL | IMP */
	FPE_FLTDIV,	/* 2E - DNML | DZ | OFL | IMP */
	FPE_FLTINV,	/* 2F - INV | DNML | DZ | OFL | IMP */
	FPE_FLTUND,	/* 30 - UFL | IMP */
	FPE_FLTINV,	/* 31 - INV | UFL | IMP */
	FPE_FLTUND,	/* 32 - DNML | UFL | IMP */
	FPE_FLTINV,	/* 33 - INV | DNML | UFL | IMP */
	FPE_FLTDIV,	/* 34 - DZ | UFL | IMP */
	FPE_FLTINV,	/* 35 - INV | DZ | UFL | IMP */
	FPE_FLTDIV,	/* 36 - DNML | DZ | UFL | IMP */
	FPE_FLTINV,	/* 37 - INV | DNML | DZ | UFL | IMP */
	FPE_FLTOVF,	/* 38 - OFL | UFL | IMP */
	FPE_FLTINV,	/* 39 - INV | OFL | UFL | IMP */
	FPE_FLTUND,	/* 3A - DNML | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3B - INV | DNML | OFL | UFL | IMP */
	FPE_FLTDIV,	/* 3C - DZ | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3D - INV | DZ | OFL | UFL | IMP */
	FPE_FLTDIV,	/* 3E - DNML | DZ | OFL | UFL | IMP */
	FPE_FLTINV,	/* 3F - INV | DNML | DZ | OFL | UFL | IMP */
	FPE_FLTSUB,	/* 40 - STK */
	FPE_FLTSUB,	/* 41 - INV | STK */
	FPE_FLTUND,	/* 42 - DNML | STK */
	FPE_FLTSUB,	/* 43 - INV | DNML | STK */
	FPE_FLTDIV,	/* 44 - DZ | STK */
	FPE_FLTSUB,	/* 45 - INV | DZ | STK */
	FPE_FLTDIV,	/* 46 - DNML | DZ | STK */
	FPE_FLTSUB,	/* 47 - INV | DNML | DZ | STK */
	FPE_FLTOVF,	/* 48 - OFL | STK */
	FPE_FLTSUB,	/* 49 - INV | OFL | STK */
	FPE_FLTUND,	/* 4A - DNML | OFL | STK */
	FPE_FLTSUB,	/* 4B - INV | DNML | OFL | STK */
	FPE_FLTDIV,	/* 4C - DZ | OFL | STK */
	FPE_FLTSUB,	/* 4D - INV | DZ | OFL | STK */
	FPE_FLTDIV,	/* 4E - DNML | DZ | OFL | STK */
	FPE_FLTSUB,	/* 4F - INV | DNML | DZ | OFL | STK */
	FPE_FLTUND,	/* 50 - UFL | STK */
	FPE_FLTSUB,	/* 51 - INV | UFL | STK */
	FPE_FLTUND,	/* 52 - DNML | UFL | STK */
	FPE_FLTSUB,	/* 53 - INV | DNML | UFL | STK */
	FPE_FLTDIV,	/* 54 - DZ | UFL | STK */
	FPE_FLTSUB,	/* 55 - INV | DZ | UFL | STK */
	FPE_FLTDIV,	/* 56 - DNML | DZ | UFL | STK */
	FPE_FLTSUB,	/* 57 - INV | DNML | DZ | UFL | STK */
	FPE_FLTOVF,	/* 58 - OFL | UFL | STK */
	FPE_FLTSUB,	/* 59 - INV | OFL | UFL | STK */
	FPE_FLTUND,	/* 5A - DNML | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5B - INV | DNML | OFL | UFL | STK */
	FPE_FLTDIV,	/* 5C - DZ | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5D - INV | DZ | OFL | UFL | STK */
	FPE_FLTDIV,	/* 5E - DNML | DZ | OFL | UFL | STK */
	FPE_FLTSUB,	/* 5F - INV | DNML | DZ | OFL | UFL | STK */
	FPE_FLTRES,	/* 60 - IMP | STK */
	FPE_FLTSUB,	/* 61 - INV | IMP | STK */
	FPE_FLTUND,	/* 62 - DNML | IMP | STK */
	FPE_FLTSUB,	/* 63 - INV | DNML | IMP | STK */
	FPE_FLTDIV,	/* 64 - DZ | IMP | STK */
	FPE_FLTSUB,	/* 65 - INV | DZ | IMP | STK */
	FPE_FLTDIV,	/* 66 - DNML | DZ | IMP | STK */
	FPE_FLTSUB,	/* 67 - INV | DNML | DZ | IMP | STK */
	FPE_FLTOVF,	/* 68 - OFL | IMP | STK */
	FPE_FLTSUB,	/* 69 - INV | OFL | IMP | STK */
	FPE_FLTUND,	/* 6A - DNML | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6B - INV | DNML | OFL | IMP | STK */
	FPE_FLTDIV,	/* 6C - DZ | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6D - INV | DZ | OFL | IMP | STK */
	FPE_FLTDIV,	/* 6E - DNML | DZ | OFL | IMP | STK */
	FPE_FLTSUB,	/* 6F - INV | DNML | DZ | OFL | IMP | STK */
	FPE_FLTUND,	/* 70 - UFL | IMP | STK */
	FPE_FLTSUB,	/* 71 - INV | UFL | IMP | STK */
	FPE_FLTUND,	/* 72 - DNML | UFL | IMP | STK */
	FPE_FLTSUB,	/* 73 - INV | DNML | UFL | IMP | STK */
	FPE_FLTDIV,	/* 74 - DZ | UFL | IMP | STK */
	FPE_FLTSUB,	/* 75 - INV | DZ | UFL | IMP | STK */
	FPE_FLTDIV,	/* 76 - DNML | DZ | UFL | IMP | STK */
	FPE_FLTSUB,	/* 77 - INV | DNML | DZ | UFL | IMP | STK */
	FPE_FLTOVF,	/* 78 - OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 79 - INV | OFL | UFL | IMP | STK */
	FPE_FLTUND,	/* 7A - DNML | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7B - INV | DNML | OFL | UFL | IMP | STK */
	FPE_FLTDIV,	/* 7C - DZ | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7D - INV | DZ | OFL | UFL | IMP | STK */
	FPE_FLTDIV,	/* 7E - DNML | DZ | OFL | UFL | IMP | STK */
	FPE_FLTSUB,	/* 7F - INV | DNML | DZ | OFL | UFL | IMP | STK */
};
#endif

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
	if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~0xFFBF) && cpu_fxsr) {
		krateprintf(&badfprate,
			    "FXRSTR: illegal FP MXCSR %08x didinit = %d\n",
			    td->td_savefpu->sv_xmm.sv_env.en_mxcsr, didinit);
		td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= 0xFFBF;
		lwpsignal(curproc, curthread->td_lwp, SIGFPE);
	}
	fpurstor(curthread->td_savefpu);
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
	fpusave(addr);
	mdcpu->gd_npxthread = NULL;
	fninit();
	/*start_emulating();*/
	crit_exit();
}

static void
fpusave(union savefpu *addr)
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
		mctx->mc_fpformat =
#ifndef CPU_DISABLE_SSE
			(cpu_fxsr) ? _MC_FPFMT_XMM :
#endif
			_MC_FPFMT_387;
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
		if ((td->td_savefpu->sv_xmm.sv_env.en_mxcsr & ~0xFFBF) &&
		    cpu_fxsr) {
			krateprintf(&badfprate,
				    "pid %d (%s) signal return from user: "
				    "illegal FP MXCSR %08x\n",
				    td->td_proc->p_pid,
				    td->td_proc->p_comm,
				    td->td_savefpu->sv_xmm.sv_env.en_mxcsr);
			td->td_savefpu->sv_xmm.sv_env.en_mxcsr &= 0xFFBF;
		}
		td->td_flags |= TDF_USINGFP;
		break;
	}
}


#ifndef CPU_DISABLE_SSE
/*
 * On AuthenticAMD processors, the fxrstor instruction does not restore
 * the x87's stored last instruction pointer, last data pointer, and last
 * opcode values, except in the rare case in which the exception summary
 * (ES) bit in the x87 status word is set to 1.
 *
 * In order to avoid leaking this information across processes, we clean
 * these values by performing a dummy load before executing fxrstor().
 */
static	double	dummy_variable = 0.0;
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
	__asm __volatile("ffree %%st(7); fld %0" : : "m" (dummy_variable));
}
#endif /* CPU_DISABLE_SSE */

static void
fpurstor(union savefpu *addr)
{
#ifndef CPU_DISABLE_SSE
	if (cpu_fxsr) {
		fpu_clean_state();
		fxrstor(addr);
	} else {
		frstor(addr);
	}
#else
	frstor(addr);
#endif
}
