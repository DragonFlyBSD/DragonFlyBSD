/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 2008-2018 The DragonFly Project.
 * Copyright (c) 2008 Jordan Gordeev.
 *
 * This code is derived from software contributed to Berkeley by
 * the University of Utah, and William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: @(#)trap.c	7.4 (Berkeley) 5/13/91
 * $FreeBSD: src/sys/i386/i386/trap.c,v 1.147.2.11 2003/02/27 19:09:59 luoqi Exp $
 */

/*
 * x86_64 Trap and System call handling
 */

#include "use_isa.h"

#include "opt_ddb.h"
#include "opt_ktrace.h"

#include <machine/frame.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/proc.h>
#include <sys/pioctl.h>
#include <sys/types.h>
#include <sys/signal2.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <sys/ktr.h>
#include <sys/sysmsg.h>

#include <vm/pmap.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/smp.h>
#include <machine/thread.h>
#include <machine/clock.h>
#include <machine/vmparam.h>
#include <machine/md_var.h>
#include <machine_base/isa/isa_intr.h>
#include <machine_base/apic/lapic.h>

#include <ddb/ddb.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

/*
 * These %rip's are used to detect a historical CPU artifact on syscall or
 * int $3 entry, if not shortcutted in exception.S via
 * DIRECT_DISALLOW_SS_CPUBUG.
 */
extern void Xbpt(void);
extern void Xfast_syscall(void);
#define IDTVEC(vec)	X##vec

extern void trap(struct trapframe *frame);

static int trap_pfault(struct trapframe *, int);
static void trap_fatal(struct trapframe *, vm_offset_t);
void dblfault_handler(struct trapframe *frame);

#define MAX_TRAP_MSG		30
static char *trap_msg[] = {
	"",					/*  0 unused */
	"privileged instruction fault",		/*  1 T_PRIVINFLT */
	"",					/*  2 unused */
	"breakpoint instruction fault",		/*  3 T_BPTFLT */
	"",					/*  4 unused */
	"",					/*  5 unused */
	"arithmetic trap",			/*  6 T_ARITHTRAP */
	"system forced exception",		/*  7 T_ASTFLT */
	"",					/*  8 unused */
	"general protection fault",		/*  9 T_PROTFLT */
	"trace trap",				/* 10 T_TRCTRAP */
	"",					/* 11 unused */
	"page fault",				/* 12 T_PAGEFLT */
	"",					/* 13 unused */
	"alignment fault",			/* 14 T_ALIGNFLT */
	"",					/* 15 unused */
	"",					/* 16 unused */
	"",					/* 17 unused */
	"integer divide fault",			/* 18 T_DIVIDE */
	"non-maskable interrupt trap",		/* 19 T_NMI */
	"overflow trap",			/* 20 T_OFLOW */
	"FPU bounds check fault",		/* 21 T_BOUND */
	"FPU device not available",		/* 22 T_DNA */
	"double fault",				/* 23 T_DOUBLEFLT */
	"FPU operand fetch fault",		/* 24 T_FPOPFLT */
	"invalid TSS fault",			/* 25 T_TSSFLT */
	"segment not present fault",		/* 26 T_SEGNPFLT */
	"stack fault",				/* 27 T_STKFLT */
	"machine check trap",			/* 28 T_MCHK */
	"SIMD floating-point exception",	/* 29 T_XMMFLT */
	"reserved (unknown) fault",		/* 30 T_RESERVED */
};

#ifdef DDB
static int ddb_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, ddb_on_nmi, CTLFLAG_RW,
	&ddb_on_nmi, 0, "Go to DDB on NMI");
static int ddb_on_seg_fault = 0;
SYSCTL_INT(_machdep, OID_AUTO, ddb_on_seg_fault, CTLFLAG_RW,
	&ddb_on_seg_fault, 0, "Go to DDB on user seg-fault");
__read_mostly static int freeze_on_seg_fault = 0;
SYSCTL_INT(_machdep, OID_AUTO, freeze_on_seg_fault, CTLFLAG_RW,
	&freeze_on_seg_fault, 0, "Go to DDB on user seg-fault");
#endif
static int panic_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, panic_on_nmi, CTLFLAG_RW,
	&panic_on_nmi, 0, "Panic on NMI");

/*
 * System call debugging records the worst-case system call
 * overhead (inclusive of blocking), but may be inaccurate.
 */
/*#define SYSCALL_DEBUG*/
#ifdef SYSCALL_DEBUG

#define SCWC_MAXT	30

struct syscallwc {
	uint32_t idx;
	uint32_t dummy;
	uint64_t tot[SYS_MAXSYSCALL];
	uint64_t timings[SYS_MAXSYSCALL][SCWC_MAXT];
} __cachealign;

struct syscallwc SysCallsWorstCase[MAXCPU];

#endif

/*
 * Passively intercepts the thread switch function to increase
 * the thread priority from a user priority to a kernel priority, reducing
 * syscall and trap overhead for the case where no switch occurs.
 *
 * Synchronizes td_ucred with p_ucred.  This is used by system calls,
 * signal handling, faults, AST traps, and anything else that enters the
 * kernel from userland and provides the kernel with a stable read-only
 * copy of the process ucred.
 *
 * To avoid races with another thread updating p_ucred we obtain p_spin.
 * The other thread doing the update will obtain both p_token and p_spin.
 * In the case where the cached cred pointer matches, we will already have
 * the ref and we don't have to do one blessed thing.
 */
static __inline void
userenter(struct thread *curtd, struct proc *curp)
{
	struct ucred *ocred;
	struct ucred *ncred;

	curtd->td_release = lwkt_passive_release;

	if (__predict_false(curtd->td_ucred != curp->p_ucred)) {
		spin_lock(&curp->p_spin);
		ncred = crhold(curp->p_ucred);
		spin_unlock(&curp->p_spin);
		ocred = curtd->td_ucred;
		curtd->td_ucred = ncred;
		if (ocred)
			crfree(ocred);
	}
}

/*
 * Handle signals, upcalls, profiling, and other AST's and/or tasks that
 * must be completed before we can return to or try to return to userland.
 *
 * Note that td_sticks is a 64 bit quantity, but there's no point doing 64
 * arithmatic on the delta calculation so the absolute tick values are
 * truncated to an integer.
 */
static void
userret(struct lwp *lp, struct trapframe *frame, int sticks)
{
	struct proc *p = lp->lwp_proc;
	int sig;
	int ptok;

	/*
	 * Charge system time if profiling.  Note: times are in microseconds.
	 * This may do a copyout and block, so do it first even though it
	 * means some system time will be charged as user time.
	 */
	if (__predict_false(p->p_flags & P_PROFIL)) {
		addupc_task(p, frame->tf_rip,
			(u_int)((int)lp->lwp_thread->td_sticks - sticks));
	}

recheck:
	/*
	 * Specific on-return-to-usermode checks (LWP_MP_WEXIT,
	 * LWP_MP_VNLRU, etc).
	 */
	if (lp->lwp_mpflags & LWP_MP_URETMASK)
		lwpuserret(lp);

	/*
	 * Block here if we are in a stopped state.
	 */
	if (__predict_false(STOPLWP(p, lp))) {
		lwkt_gettoken(&p->p_token);
		tstop();
		lwkt_reltoken(&p->p_token);
		goto recheck;
	}
	while (__predict_false(dump_stop_usertds)) {
		tsleep(&dump_stop_usertds, 0, "dumpstp", 0);
	}

	/*
	 * Post any pending upcalls.  If running a virtual kernel be sure
	 * to restore the virtual kernel's vmspace before posting the upcall.
	 */
	if (__predict_false(p->p_flags & (P_SIGVTALRM | P_SIGPROF))) {
		lwkt_gettoken(&p->p_token);
		if (p->p_flags & P_SIGVTALRM) {
			p->p_flags &= ~P_SIGVTALRM;
			ksignal(p, SIGVTALRM);
		}
		if (p->p_flags & P_SIGPROF) {
			p->p_flags &= ~P_SIGPROF;
			ksignal(p, SIGPROF);
		}
		lwkt_reltoken(&p->p_token);
		goto recheck;
	}

	/*
	 * Post any pending signals.  If running a virtual kernel be sure
	 * to restore the virtual kernel's vmspace before posting the signal.
	 *
	 * WARNING!  postsig() can exit and not return.
	 */
	if (__predict_false((sig = CURSIG_LCK_TRACE(lp, &ptok)) != 0)) {
		postsig(sig, ptok);
		goto recheck;
	}

	/*
	 * In a multi-threaded program it is possible for a thread to change
	 * signal state during a system call which temporarily changes the
	 * signal mask.  In this case postsig() might not be run and we
	 * have to restore the mask ourselves.
	 */
	if (__predict_false(lp->lwp_flags & LWP_OLDMASK)) {
		lp->lwp_flags &= ~LWP_OLDMASK;
		lp->lwp_sigmask = lp->lwp_oldsigmask;
		goto recheck;
	}
}

/*
 * Cleanup from userenter and any passive release that might have occured.
 * We must reclaim the current-process designation before we can return
 * to usermode.  We also handle both LWKT and USER reschedule requests.
 */
static __inline void
userexit(struct lwp *lp)
{
	struct thread *td = lp->lwp_thread;
	/* globaldata_t gd = td->td_gd; */

	/*
	 * Handle stop requests at kernel priority.  Any requests queued
	 * after this loop will generate another AST.
	 */
	while (__predict_false(STOPLWP(lp->lwp_proc, lp))) {
		lwkt_gettoken(&lp->lwp_proc->p_token);
		tstop();
		lwkt_reltoken(&lp->lwp_proc->p_token);
	}

	/*
	 * Reduce our priority in preparation for a return to userland.  If
	 * our passive release function was still in place, our priority was
	 * never raised and does not need to be reduced.
	 */
	lwkt_passive_recover(td);

	/* WARNING: we may have migrated cpu's */
	/* gd = td->td_gd; */

	/*
	 * Become the current user scheduled process if we aren't already,
	 * and deal with reschedule requests and other factors.
	 *
	 * Do a silly hack to avoid RETPOLINE nonsense.
	 */
	if (lp->lwp_proc->p_usched == &usched_dfly)
		dfly_acquire_curproc(lp);
	else
		lp->lwp_proc->p_usched->acquire_curproc(lp);
}

/*
 * A page fault on a userspace address is classified as SMAP-induced
 * if:
 *	- SMAP is supported
 *	- kernel mode accessed present data page
 *	- rflags.AC was cleared
 */
static int
trap_is_smap(struct trapframe *frame)
{
        if ((cpu_stdext_feature & CPUID_STDEXT_SMAP) != 0 &&
            (frame->tf_err & (PGEX_P | PGEX_U | PGEX_I | PGEX_RSV)) == PGEX_P &&
	    (frame->tf_rflags & PSL_AC) == 0) {
		return 1;
	} else {
		return 0;
	}
}

#if !defined(KTR_KERNENTRY)
#define	KTR_KERNENTRY	KTR_ALL
#endif
KTR_INFO_MASTER(kernentry);
KTR_INFO(KTR_KERNENTRY, kernentry, trap, 0,
	 "TRAP(pid %d, tid %d, trapno %ld, eva %lu)",
	 pid_t pid, lwpid_t tid,  register_t trapno, vm_offset_t eva);
KTR_INFO(KTR_KERNENTRY, kernentry, trap_ret, 0, "TRAP_RET(pid %d, tid %d)",
	 pid_t pid, lwpid_t tid);
KTR_INFO(KTR_KERNENTRY, kernentry, syscall, 0, "SYSC(pid %d, tid %d, nr %ld)",
	 pid_t pid, lwpid_t tid,  register_t trapno);
KTR_INFO(KTR_KERNENTRY, kernentry, syscall_ret, 0, "SYSRET(pid %d, tid %d, err %d)",
	 pid_t pid, lwpid_t tid,  int err);
KTR_INFO(KTR_KERNENTRY, kernentry, fork_ret, 0, "FORKRET(pid %d, tid %d)",
	 pid_t pid, lwpid_t tid);

/*
 * Exception, fault, and trap interface to the kernel.
 * This common code is called from assembly language IDT gate entry
 * routines that prepare a suitable stack frame, and restore this
 * frame after the exception has been processed.
 *
 * This function is also called from doreti in an interlock to handle ASTs.
 * For example:  hardwareint->INTROUTINE->(set ast)->doreti->trap
 *
 * NOTE!  We have to retrieve the fault address prior to potentially
 *	  blocking, including blocking on any token.
 *
 * NOTE!  NMI and kernel DBG traps remain on their respective pcpu IST
 *	  stacks if taken from a kernel RPL. trap() cannot block in this
 *	  situation.  DDB entry or a direct report-and-return is ok.
 *
 * XXX gd_trap_nesting_level currently prevents lwkt_switch() from panicing
 * if an attempt is made to switch from a fast interrupt or IPI.
 */
void
trap(struct trapframe *frame)
{
	static struct krate sscpubugrate = { 1 };
	struct globaldata *gd = mycpu;
	struct thread *td = gd->gd_curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p;
	int sticks = 0;
	int i = 0, ucode = 0, type, code;
#ifdef INVARIANTS
	int crit_count = td->td_critcount;
	lwkt_tokref_t curstop = td->td_toks_stop;
#endif
	vm_offset_t eva;

	p = td->td_proc;
	clear_quickret();

#ifdef DDB
        /*
	 * We need to allow T_DNA faults when the debugger is active since
	 * some dumping paths do large bcopy() which use the floating
	 * point registers for faster copying.
	 */
	if (db_active && frame->tf_trapno != T_DNA) {
		eva = (frame->tf_trapno == T_PAGEFLT ? frame->tf_addr : 0);
		++gd->gd_trap_nesting_level;
		trap_fatal(frame, eva);
		--gd->gd_trap_nesting_level;
		goto out2;
	}
#endif

	eva = 0;

	if ((frame->tf_rflags & PSL_I) == 0) {
		/*
		 * Buggy application or kernel code has disabled interrupts
		 * and then trapped.  Enabling interrupts now is wrong, but
		 * it is better than running with interrupts disabled until
		 * they are accidentally enabled later.
		 */

		type = frame->tf_trapno;
		if (ISPL(frame->tf_cs) == SEL_UPL) {
			/* JG curproc can be NULL */
			kprintf(
			    "pid %ld (%s): trap %d with interrupts disabled\n",
			    (long)curproc->p_pid, curproc->p_comm, type);
		} else if ((type == T_STKFLT || type == T_PROTFLT ||
			    type == T_SEGNPFLT) &&
			   frame->tf_rip == (long)doreti_iret) {
			/*
			 * iretq fault from kernel mode during return to
			 * userland.
			 *
			 * This situation is expected, don't complain.
			 */
		} else if (type != T_NMI && type != T_BPTFLT &&
			   type != T_TRCTRAP) {
			/*
			 * XXX not quite right, since this may be for a
			 * multiple fault in user mode.
			 */
			kprintf("kernel trap %d (%s @ 0x%016jx) with "
				"interrupts disabled\n",
				type,
				td->td_comm,
				frame->tf_rip);
		}
		cpu_enable_intr();
	}

	type = frame->tf_trapno;
	code = frame->tf_err;

	if (ISPL(frame->tf_cs) == SEL_UPL) {
		/* user trap */

		KTR_LOG(kernentry_trap, p->p_pid, lp->lwp_tid,
			frame->tf_trapno, eva);

		userenter(td, p);

		sticks = (int)td->td_sticks;
		KASSERT(lp->lwp_md.md_regs == frame,
			("Frame mismatch %p %p", lp->lwp_md.md_regs, frame));

		switch (type) {
		case T_PRIVINFLT:	/* privileged instruction fault */
			i = SIGILL;
			ucode = ILL_PRVOPC;
			break;

		case T_BPTFLT:		/* bpt instruction fault */
		case T_TRCTRAP:		/* trace trap */
			frame->tf_rflags &= ~PSL_T;
			i = SIGTRAP;
			ucode = (type == T_TRCTRAP ? TRAP_TRACE : TRAP_BRKPT);
			break;

		case T_ARITHTRAP:	/* arithmetic trap */
			ucode = code;
			i = SIGFPE;
			break;

		case T_ASTFLT:		/* Allow process switch */
			mycpu->gd_cnt.v_soft++;
			if (mycpu->gd_reqflags & RQF_AST_OWEUPC) {
				atomic_clear_int(&mycpu->gd_reqflags,
						 RQF_AST_OWEUPC);
				addupc_task(p, p->p_prof.pr_addr,
					    p->p_prof.pr_ticks);
			}
			goto out;

		case T_PROTFLT:		/* general protection fault */
			i = SIGBUS;
			ucode = BUS_OBJERR;
			break;
		case T_STKFLT:		/* stack fault */
		case T_SEGNPFLT:	/* segment not present fault */
			i = SIGBUS;
			ucode = BUS_ADRERR;
			break;
		case T_TSSFLT:		/* invalid TSS fault */
		case T_DOUBLEFLT:	/* double fault */
		default:
			i = SIGBUS;
			ucode = BUS_OBJERR;
			break;

		case T_PAGEFLT:		/* page fault */
			i = trap_pfault(frame, TRUE);
#ifdef DDB
			if (frame->tf_rip == 0) {
				/* used for kernel debugging only */
				while (freeze_on_seg_fault)
					tsleep(p, 0, "freeze", hz * 20);
			}
#endif
			if (i == -1 || i == 0)
				goto out;
			if (i == SIGSEGV) {
				ucode = SEGV_MAPERR;
			} else {
				i = SIGSEGV;
				ucode = SEGV_ACCERR;
			}
			break;

		case T_DIVIDE:		/* integer divide fault */
			ucode = FPE_INTDIV;
			i = SIGFPE;
			break;

#if NISA > 0
		case T_NMI:
			/* machine/parity/power fail/"kitchen sink" faults */
			if (isa_nmi(code) == 0) {
#ifdef DDB
				/*
				 * NMI can be hooked up to a pushbutton
				 * for debugging.
				 */
				if (ddb_on_nmi) {
					kprintf ("NMI ... going to debugger\n");
					kdb_trap(type, 0, frame);
				}
#endif /* DDB */
				goto out2;
			} else if (panic_on_nmi)
				panic("NMI indicates hardware failure");
			break;
#endif /* NISA > 0 */

		case T_OFLOW:		/* integer overflow fault */
			ucode = FPE_INTOVF;
			i = SIGFPE;
			break;

		case T_BOUND:		/* bounds check fault */
			ucode = FPE_FLTSUB;
			i = SIGFPE;
			break;

		case T_DNA:
			/*
			 * Virtual kernel intercept - pass the DNA exception
			 * to the virtual kernel if it asked to handle it.
			 * This occurs when the virtual kernel is holding
			 * onto the FP context for a different emulated
			 * process then the one currently running.
			 *
			 * We must still call npxdna() since we may have
			 * saved FP state that the virtual kernel needs
			 * to hand over to a different emulated process.
			 */
			if (lp->lwp_vkernel && lp->lwp_vkernel->ve &&
			    (td->td_pcb->pcb_flags & FP_VIRTFP)
			) {
				npxdna();
				break;
			}

			/*
			 * The kernel may have switched out the FP unit's
			 * state, causing the user process to take a fault
			 * when it tries to use the FP unit.  Restore the
			 * state here
			 */
			if (npxdna()) {
				gd->gd_cnt.v_trap++;
				goto out;
			}
			i = SIGFPE;
			ucode = FPE_FPU_NP_TRAP;
			break;

		case T_FPOPFLT:		/* FPU operand fetch fault */
			ucode = ILL_COPROC;
			i = SIGILL;
			break;

		case T_XMMFLT:		/* SIMD floating-point exception */
			ucode = 0; /* XXX */
			i = SIGFPE;
			break;
		}
	} else {
		/* kernel trap */

		switch (type) {
		case T_PAGEFLT:			/* page fault */
			trap_pfault(frame, FALSE);
			goto out2;

		case T_DNA:
			/*
			 * The kernel is apparently using fpu for copying.
			 * XXX this should be fatal unless the kernel has
			 * registered such use.
			 */
			if (npxdna()) {
				gd->gd_cnt.v_trap++;
				goto out2;
			}
			break;

		case T_STKFLT:		/* stack fault */
		case T_PROTFLT:		/* general protection fault */
		case T_SEGNPFLT:	/* segment not present fault */
			/*
			 * Invalid segment selectors and out of bounds
			 * %rip's and %rsp's can be set up in user mode.
			 * This causes a fault in kernel mode when the
			 * kernel tries to return to user mode.  We want
			 * to get this fault so that we can fix the
			 * problem here and not have to check all the
			 * selectors and pointers when the user changes
			 * them.
			 */
			if (mycpu->gd_intr_nesting_level == 0) {
				/*
				 * NOTE: in 64-bit mode traps push rsp/ss
				 *	 even if no ring change occurs.
				 */
				if (td->td_pcb->pcb_onfault &&
				    td->td_pcb->pcb_onfault_sp ==
				    frame->tf_rsp) {
					frame->tf_rip = (register_t)
						td->td_pcb->pcb_onfault;
					goto out2;
				}

				/*
				 * If the iretq in doreti faults during
				 * return to user, it will be special-cased
				 * in IDTVEC(prot) to get here.  We want
				 * to 'return' to doreti_iret_fault in
				 * ipl.s in approximately the same state we
				 * were in at the iretq.
				 */
				if (frame->tf_rip == (long)doreti_iret) {
					frame->tf_rip = (long)doreti_iret_fault;
					goto out2;
				}
			}
			break;

		case T_TSSFLT:
			/*
			 * PSL_NT can be set in user mode and isn't cleared
			 * automatically when the kernel is entered.  This
			 * causes a TSS fault when the kernel attempts to
			 * `iret' because the TSS link is uninitialized.  We
			 * want to get this fault so that we can fix the
			 * problem here and not every time the kernel is
			 * entered.
			 */
			if (frame->tf_rflags & PSL_NT) {
				frame->tf_rflags &= ~PSL_NT;
#if 0
				/* do we need this? */
				if (frame->tf_rip == (long)doreti_iret)
					frame->tf_rip = (long)doreti_iret_fault;
#endif
				goto out2;
			}
			break;

		case T_TRCTRAP:	 /* trace trap */
			/*
			 * Detect historical CPU artifact on syscall or int $3
			 * entry (if not shortcutted in exception.s via
			 * DIRECT_DISALLOW_SS_CPUBUG).
			 */
			gd->gd_cnt.v_trap++;
			if (frame->tf_rip == (register_t)IDTVEC(fast_syscall)) {
				krateprintf(&sscpubugrate,
					"Caught #DB at syscall cpu artifact\n");
				goto out2;
			}
			if (frame->tf_rip == (register_t)IDTVEC(bpt)) {
				krateprintf(&sscpubugrate,
					"Caught #DB at int $N cpu artifact\n");
				goto out2;
			}

			/*
			 * Ignore debug register trace traps due to
			 * accesses in the user's address space, which
			 * can happen under several conditions such as
			 * if a user sets a watchpoint on a buffer and
			 * then passes that buffer to a system call.
			 * We still want to get TRCTRAPS for addresses
			 * in kernel space because that is useful when
			 * debugging the kernel.
			 */
			if (user_dbreg_trap()) {
				/*
				 * Reset breakpoint bits because the
				 * processor doesn't
				 */
				load_dr6(rdr6() & ~0xf);
				goto out2;
			}
			/*
			 * FALLTHROUGH (TRCTRAP kernel mode, kernel address)
			 */
		case T_BPTFLT:
			/*
			 * If DDB is enabled, let it handle the debugger trap.
			 * Otherwise, debugger traps "can't happen".
			 */
			ucode = TRAP_BRKPT;
#ifdef DDB
			if (kdb_trap(type, 0, frame))
				goto out2;
#endif
			break;

#if NISA > 0
		case T_NMI:
			/* machine/parity/power fail/"kitchen sink" faults */
			if (isa_nmi(code) == 0) {
#ifdef DDB
				/*
				 * NMI can be hooked up to a pushbutton
				 * for debugging.
				 */
				if (ddb_on_nmi) {
					kprintf ("NMI ... going to debugger\n");
					kdb_trap(type, 0, frame);
				}
#endif /* DDB */
				goto out2;
			} else if (panic_on_nmi == 0)
				goto out2;
#endif /* NISA > 0 */
			break;
		default:
			if (type >= T_RESERVED && type < T_RESERVED + 256) {
				kprintf("Ignoring spurious unknown "
					"cpu trap T_RESERVED+%d\n",
					type - T_RESERVED);
				gd->gd_cnt.v_trap++;
				goto out2;
			}
			break;
		}
		trap_fatal(frame, 0);
		goto out2;
	}

	/*
	 * Fault from user mode, virtual kernel interecept.
	 *
	 * If the fault is directly related to a VM context managed by a
	 * virtual kernel then let the virtual kernel handle it.
	 */
	if (lp->lwp_vkernel && lp->lwp_vkernel->ve) {
		vkernel_trap(lp, frame);
		goto out;
	}

	/* Translate fault for emulators (e.g. Linux) */
	if (*p->p_sysent->sv_transtrap)
		i = (*p->p_sysent->sv_transtrap)(i, type);

	gd->gd_cnt.v_trap++;
	trapsignal(lp, i, ucode);

#ifdef DEBUG
	if (type <= MAX_TRAP_MSG) {
		uprintf("fatal process exception: %s",
			trap_msg[type]);
		if ((type == T_PAGEFLT) || (type == T_PROTFLT))
			uprintf(", fault VA = 0x%lx", frame->tf_addr);
		uprintf("\n");
	}
#endif

out:
	userret(lp, frame, sticks);
	userexit(lp);
out2:	;
	if (p != NULL && lp != NULL)
		KTR_LOG(kernentry_trap_ret, p->p_pid, lp->lwp_tid);
#ifdef INVARIANTS
	KASSERT(crit_count == td->td_critcount,
		("trap: critical section count mismatch! %d/%d",
		crit_count, td->td_pri));
	KASSERT(curstop == td->td_toks_stop,
		("trap: extra tokens held after trap! %ld/%ld (%s)",
		curstop - &td->td_toks_base,
		td->td_toks_stop - &td->td_toks_base,
		td->td_toks_stop[-1].tr_tok->t_desc));
#endif
}

void
trap_handle_userenter(struct thread *td)
{
	userenter(td, td->td_proc);
}

void
trap_handle_userexit(struct trapframe *frame, int sticks)
{
	struct lwp *lp = curthread->td_lwp;

	if (lp) {
		userret(lp, frame, sticks);
		userexit(lp);
	}
}

static int
trap_pfault(struct trapframe *frame, int usermode)
{
	vm_offset_t va;
	struct vmspace *vm = NULL;
	vm_map_t map;
	int rv = 0;
	int fault_flags;
	vm_prot_t ftype;
	thread_t td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p;

	va = trunc_page(frame->tf_addr);
	if (va >= VM_MIN_KERNEL_ADDRESS) {
		/*
		 * Don't allow user-mode faults in kernel address space.
		 */
		if (usermode) {
			fault_flags = -1;
			ftype = -1;
			goto nogo;
		}

		map = &kernel_map;
	} else {
		/*
		 * This is a fault on non-kernel virtual memory.
		 * vm is initialized above to NULL. If curproc is NULL
		 * or curproc->p_vmspace is NULL the fault is fatal.
		 */
		if (lp != NULL)
			vm = lp->lwp_vmspace;

		if (vm == NULL) {
			fault_flags = -1;
			ftype = -1;
			goto nogo;
		}

		if (usermode == 0) {
#ifdef DDB
			/*
			 * Debugging, catch kernel faults on the user address
			 * space when not inside on onfault (e.g. copyin/
			 * copyout) routine.
			 */
			if (td->td_pcb == NULL ||
			    td->td_pcb->pcb_onfault == NULL) {
				if (freeze_on_seg_fault) {
					kprintf("trap_pfault: user address "
						"fault from kernel mode "
						"%016lx\n",
						(long)frame->tf_addr);
					while (freeze_on_seg_fault) {
						    tsleep(&freeze_on_seg_fault,
							   0,
							   "frzseg",
							   hz * 20);
					}
				}
			}
#endif
			if (td->td_gd->gd_intr_nesting_level ||
			    trap_is_smap(frame) ||
			    td->td_pcb == NULL ||
			    td->td_pcb->pcb_onfault == NULL) {
				kprintf("Fatal user address access "
					"from kernel mode from %s at %016jx\n",
					td->td_comm, frame->tf_rip);
				trap_fatal(frame, frame->tf_addr);
				return (-1);
			}
		}
		map = &vm->vm_map;
	}

	/*
	 * PGEX_I is defined only if the execute disable bit capability is
	 * supported and enabled.
	 */
	if (frame->tf_err & PGEX_W)
		ftype = VM_PROT_WRITE;
	else if (frame->tf_err & PGEX_I)
		ftype = VM_PROT_EXECUTE;
	else
		ftype = VM_PROT_READ;

	lwkt_tokref_t stop = td->td_toks_stop;

	if (map != &kernel_map) {
		/*
		 * Keep swapout from messing with us during this
		 *	critical time.
		 */
		PHOLD(lp->lwp_proc);

		/*
		 * Issue fault
		 */
		fault_flags = 0;
		if (usermode)
			fault_flags |= VM_FAULT_BURST | VM_FAULT_USERMODE;
		if (ftype & VM_PROT_WRITE)
			fault_flags |= VM_FAULT_DIRTY;
		else
			fault_flags |= VM_FAULT_NORMAL;
		rv = vm_fault(map, va, ftype, fault_flags);
		if (td->td_toks_stop != stop) {
			stop = td->td_toks_stop - 1;
			kprintf("A-HELD TOKENS DURING PFAULT td=%p(%s) map=%p va=%p ftype=%d fault_flags=%d\n", td, td->td_comm, map, (void *)va, ftype, fault_flags);
			panic("held tokens");
		}

		PRELE(lp->lwp_proc);
	} else {
		/*
		 * Don't have to worry about process locking or stacks in the
		 * kernel.
		 */
		fault_flags = VM_FAULT_NORMAL;
		rv = vm_fault(map, va, ftype, VM_FAULT_NORMAL);
		if (td->td_toks_stop != stop) {
			stop = td->td_toks_stop - 1;
			kprintf("B-HELD TOKENS DURING PFAULT td=%p(%s) map=%p va=%p ftype=%d fault_flags=%d\n", td, td->td_comm, map, (void *)va, ftype, VM_FAULT_NORMAL);
			panic("held tokens");
		}
	}
	if (rv == KERN_SUCCESS)
		return (0);
nogo:
	if (!usermode) {
		/*
		 * NOTE: in 64-bit mode traps push rsp/ss
		 *	 even if no ring change occurs.
		 */
		if (td->td_pcb->pcb_onfault &&
		    td->td_pcb->pcb_onfault_sp == frame->tf_rsp &&
		    td->td_gd->gd_intr_nesting_level == 0) {
			frame->tf_rip = (register_t)td->td_pcb->pcb_onfault;
			return (0);
		}
		trap_fatal(frame, frame->tf_addr);
		return (-1);
	}

	/*
	 * NOTE: on x86_64 we have a tf_addr field in the trapframe, no
	 * kludge is needed to pass the fault address to signal handlers.
	 */
	p = td->td_proc;
#ifdef DDB
	if (td->td_lwp->lwp_vkernel == NULL) {
		while (freeze_on_seg_fault) {
			tsleep(p, 0, "freeze", hz * 20);
		}
		if (ddb_on_seg_fault)
			Debugger("ddb_on_seg_fault");
	}
#endif

	return((rv == KERN_PROTECTION_FAILURE) ? SIGBUS : SIGSEGV);
}

static void
trap_fatal(struct trapframe *frame, vm_offset_t eva)
{
	int code, ss;
	u_int type;
	long rsp;
	struct soft_segment_descriptor softseg;

	code = frame->tf_err;
	type = frame->tf_trapno;
	sdtossd(&gdt[IDXSEL(frame->tf_cs & 0xffff)], &softseg);

	kprintf("\n\nFatal trap %d: ", type);
	if (type <= MAX_TRAP_MSG)
		kprintf("%s ", trap_msg[type]);
	else
		kprintf("rsvd(%d) ", type - T_RESERVED);

	kprintf("while in %s mode\n",
		ISPL(frame->tf_cs) == SEL_UPL ? "user" : "kernel");

	/* three separate prints in case of a trap on an unmapped page */
	kprintf("cpuid = %d; ", mycpu->gd_cpuid);
	if (lapic_usable)
		kprintf("lapic id = %u\n", LAPIC_READID);
	if (type == T_PAGEFLT) {
		kprintf("fault virtual address	= 0x%lx\n", eva);
		kprintf("fault code		= %s %s %s, %s\n",
			code & PGEX_U ? "user" : "supervisor",
			code & PGEX_W ? "write" : "read",
			code & PGEX_I ? "instruction" : "data",
			code & PGEX_P ? "protection violation" : "page not present");
	}
	kprintf("instruction pointer	= 0x%lx:0x%lx\n",
	       frame->tf_cs & 0xffff, frame->tf_rip);
        if (ISPL(frame->tf_cs) == SEL_UPL) {
		ss = frame->tf_ss & 0xffff;
		rsp = frame->tf_rsp;
	} else {
		/*
		 * NOTE: in 64-bit mode traps push rsp/ss even if no ring
		 *	 change occurs.
		 */
		ss = GSEL(GDATA_SEL, SEL_KPL);
		rsp = frame->tf_rsp;
	}
	kprintf("stack pointer	        = 0x%x:0x%lx\n", ss, rsp);
	kprintf("frame pointer	        = 0x%x:0x%lx\n", ss, frame->tf_rbp);
	kprintf("code segment		= base 0x%lx, limit 0x%lx, type 0x%x\n",
	       softseg.ssd_base, softseg.ssd_limit, softseg.ssd_type);
	kprintf("			= DPL %d, pres %d, long %d, def32 %d, gran %d\n",
	       softseg.ssd_dpl, softseg.ssd_p, softseg.ssd_long, softseg.ssd_def32,
	       softseg.ssd_gran);
	kprintf("processor eflags	= ");
	if (frame->tf_rflags & PSL_T)
		kprintf("trace trap, ");
	if (frame->tf_rflags & PSL_I)
		kprintf("interrupt enabled, ");
	if (frame->tf_rflags & PSL_NT)
		kprintf("nested task, ");
	if (frame->tf_rflags & PSL_RF)
		kprintf("resume, ");
	if (frame->tf_rflags & PSL_AC)
		kprintf("smap_open, ");
	kprintf("IOPL = %ld\n", (frame->tf_rflags & PSL_IOPL) >> 12);
	kprintf("current process		= ");
	if (curproc) {
		kprintf("%lu\n",
		    (u_long)curproc->p_pid);
	} else {
		kprintf("Idle\n");
	}
	kprintf("current thread          = pri %d ", curthread->td_pri);
	if (curthread->td_critcount)
		kprintf("(CRIT)");
	kprintf("\n");

#ifdef DDB
	if ((debugger_on_panic || db_active) && kdb_trap(type, code, frame))
		return;
#endif
	kprintf("trap number		= %d\n", type);
	if (type <= MAX_TRAP_MSG)
		panic("%s", trap_msg[type]);
	else
		panic("unknown/reserved trap");
}

/*
 * Double fault handler. Called when a fault occurs while writing
 * a frame for a trap/exception onto the stack. This usually occurs
 * when the stack overflows (such is the case with infinite recursion,
 * for example).
 */
static __inline
int
in_kstack_guard(register_t rptr)
{
	thread_t td = curthread;

	if ((char *)rptr >= td->td_kstack &&
	    (char *)rptr < td->td_kstack + PAGE_SIZE) {
		return 1;
	}
	return 0;
}

void
dblfault_handler(struct trapframe *frame)
{
	thread_t td = curthread;

	if (in_kstack_guard(frame->tf_rsp) || in_kstack_guard(frame->tf_rbp)) {
		kprintf("DOUBLE FAULT - KERNEL STACK GUARD HIT!\n");
		if (in_kstack_guard(frame->tf_rsp))
			frame->tf_rsp = (register_t)(td->td_kstack + PAGE_SIZE);
		if (in_kstack_guard(frame->tf_rbp))
			frame->tf_rbp = (register_t)(td->td_kstack + PAGE_SIZE);
	} else {
		kprintf("DOUBLE FAULT\n");
	}
	kprintf("\nFatal double fault\n");
	kprintf("rip = 0x%lx\n", frame->tf_rip);
	kprintf("rsp = 0x%lx\n", frame->tf_rsp);
	kprintf("rbp = 0x%lx\n", frame->tf_rbp);
	/* three separate prints in case of a trap on an unmapped page */
	kprintf("cpuid = %d; ", mycpu->gd_cpuid);
	if (lapic_usable)
		kprintf("lapic id = %u\n", LAPIC_READID);
	panic("double fault");
}

/*
 * syscall2 -	MP aware system call request C handler
 *
 * A system call is essentially treated as a trap except that the
 * MP lock is not held on entry or return.  We are responsible for
 * obtaining the MP lock if necessary and for handling ASTs
 * (e.g. a task switch) prior to return.
 */
void
syscall2(struct trapframe *frame)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct lwp *lp = td->td_lwp;
	struct sysent *callp;
	register_t orig_tf_rflags;
	int sticks;
	int error;
	int narg;
#ifdef INVARIANTS
	int crit_count = td->td_critcount;
#endif
	struct sysmsg sysmsg;
	union sysunion *argp;
	u_int code;
	const int regcnt = 6;	/* number of args passed in registers */

	mycpu->gd_cnt.v_syscall++;

#ifdef DIAGNOSTIC
	if (__predict_false(ISPL(frame->tf_cs) != SEL_UPL)) {
		panic("syscall");
		/* NOT REACHED */
	}
#endif

	KTR_LOG(kernentry_syscall, p->p_pid, lp->lwp_tid,
		frame->tf_rax);

	userenter(td, p);	/* lazy raise our priority */

	/*
	 * Misc
	 */
	sticks = (int)td->td_sticks;
	orig_tf_rflags = frame->tf_rflags;

	/*
	 * Virtual kernel intercept - if a VM context managed by a virtual
	 * kernel issues a system call the virtual kernel handles it, not us.
	 * Restore the virtual kernel context and return from its system
	 * call.  The current frame is copied out to the virtual kernel.
	 */
	if (__predict_false(lp->lwp_vkernel && lp->lwp_vkernel->ve)) {
		vkernel_trap(lp, frame);
		error = EJUSTRETURN;
		callp = NULL;
		code = 0;
		goto out;
	}

	/*
	 * Get the system call parameters and account for time
	 */
#ifdef DIAGNOSTIC
	KASSERT(lp->lwp_md.md_regs == frame,
		("Frame mismatch %p %p", lp->lwp_md.md_regs, frame));
#endif

	code = (u_int)frame->tf_rax;
	if (code >= p->p_sysent->sv_size)
		code = SYS___nosys;

	argp = (union sysunion *)&frame->tf_rdi;
	callp = &p->p_sysent->sv_table[code];

	/*
	 * On x86_64 we get up to six arguments in registers. The rest are
	 * on the stack. The first six members of 'struct trapframe' happen
	 * to be the registers used to pass arguments, in exactly the right
	 * order.
	 *
	 * Any arguments beyond available argument-passing registers must
	 * be copyin()'d from the user stack.
	 */
	narg = callp->sy_narg;
	if (__predict_false(narg > regcnt)) {
		register_t *argsdst;
		caddr_t params;

		argsdst = (register_t *)&sysmsg.extargs;
		bcopy(argp, argsdst, sizeof(register_t) * regcnt);
		params = (caddr_t)frame->tf_rsp + sizeof(register_t);
		error = copyin(params, &argsdst[regcnt],
			       (narg - regcnt) * sizeof(register_t));
		argp = (void *)argsdst;
		if (error) {
#ifdef KTRACE
			if (KTRPOINTP(p, td, KTR_SYSCALL)) {
				ktrsyscall(lp, code, narg, argp);
			}
#endif
			goto bad;
		}
	}

#ifdef KTRACE
	if (KTRPOINTP(p, td, KTR_SYSCALL)) {
		ktrsyscall(lp, code, narg, argp);
	}
#endif

	/*
	 * Default return value is 0 (will be copied to %rax).  Double-value
	 * returns use %rax and %rdx.  %rdx is left unchanged for system
	 * calls which return only one result.
	 */
	sysmsg.sysmsg_fds[0] = 0;
	sysmsg.sysmsg_fds[1] = frame->tf_rdx;

	/*
	 * The syscall might manipulate the trap frame. If it does it
	 * will probably return EJUSTRETURN.
	 */
	sysmsg.sysmsg_frame = frame;

	STOPEVENT(p, S_SCE, narg);	/* MP aware */

	/*
	 * NOTE: All system calls run MPSAFE now.  The system call itself
	 *	 is responsible for getting the MP lock.
	 */
#ifdef SYSCALL_DEBUG
	tsc_uclock_t tscval = rdtsc();
#endif
	error = (*callp->sy_call)(&sysmsg, argp);
#ifdef SYSCALL_DEBUG
	tscval = rdtsc() - tscval;
	tscval = tscval * 1000000 / (tsc_frequency / 1000);	/* ns */
	{
		struct syscallwc *scwc = &SysCallsWorstCase[mycpu->gd_cpuid];
		int idx = scwc->idx++ % SCWC_MAXT;

		scwc->tot[code] += tscval - scwc->timings[code][idx];
		scwc->timings[code][idx] = tscval;
	}
#endif

out:
	/*
	 * MP SAFE (we may or may not have the MP lock at this point)
	 */
	//kprintf("SYSMSG %d ", error);
	if (__predict_true(error == 0)) {
		/*
		 * Reinitialize proc pointer `p' as it may be different
		 * if this is a child returning from fork syscall.
		 */
		p = curproc;
		lp = curthread->td_lwp;
		frame->tf_rax = sysmsg.sysmsg_fds[0];
		frame->tf_rdx = sysmsg.sysmsg_fds[1];
		frame->tf_rflags &= ~PSL_C;
	} else if (error == ERESTART) {
		/*
		 * Reconstruct pc, we know that 'syscall' is 2 bytes.
		 * We have to do a full context restore so that %r10
		 * (which was holding the value of %rcx) is restored for
		 * the next iteration.
		 */
		if (frame->tf_err != 0 && frame->tf_err != 2)
			kprintf("lp %s:%d frame->tf_err is weird %ld\n",
				td->td_comm, lp->lwp_proc->p_pid, frame->tf_err);
		frame->tf_rip -= frame->tf_err;
		frame->tf_r10 = frame->tf_rcx;
	} else if (error == EJUSTRETURN) {
		/* do nothing */
	} else if (error == EASYNC) {
		panic("Unexpected EASYNC return value (for now)");
	} else {
bad:
		if (p->p_sysent->sv_errsize) {
			if (error >= p->p_sysent->sv_errsize)
				error = -1;	/* XXX */
			else
				error = p->p_sysent->sv_errtbl[error];
		}
		frame->tf_rax = error;
		frame->tf_rflags |= PSL_C;
	}

	/*
	 * Traced syscall.  trapsignal() should now be MP aware
	 */
	if (__predict_false(orig_tf_rflags & PSL_T)) {
		frame->tf_rflags &= ~PSL_T;
		trapsignal(lp, SIGTRAP, TRAP_TRACE);
	}

	/*
	 * Handle reschedule and other end-of-syscall issues
	 */
	userret(lp, frame, sticks);

#ifdef KTRACE
	if (KTRPOINTP(p, td, KTR_SYSRET)) {
		ktrsysret(lp, code, error, sysmsg.sysmsg_result);
	}
#endif

	/*
	 * This works because errno is findable through the
	 * register set.  If we ever support an emulation where this
	 * is not the case, this code will need to be revisited.
	 */
	STOPEVENT(p, S_SCX, code);

	userexit(lp);
	KTR_LOG(kernentry_syscall_ret, p->p_pid, lp->lwp_tid, error);
#ifdef INVARIANTS
	KASSERT(crit_count == td->td_critcount,
		("syscall: critical section count mismatch! %d/%d",
		crit_count, td->td_pri));
	KASSERT(&td->td_toks_base == td->td_toks_stop,
		("syscall: %ld extra tokens held after trap! syscall %p",
		td->td_toks_stop - &td->td_toks_base,
		callp->sy_call));
#endif
}

/*
 * Handles the syscall() and __syscall() API
 */
void xsyscall(struct sysmsg *sysmsg, struct nosys_args *uap);

int
sys_xsyscall(struct sysmsg *sysmsg, const struct nosys_args *uap)
{
	struct trapframe *frame;
	struct sysent *callp;
	union sysunion *argp;
	struct thread *td;
	struct proc *p;
	const int regcnt = 5;	/* number of args passed in registers */
	u_int code;
	int error;
	int narg;

	td = curthread;
	p = td->td_proc;
	frame = sysmsg->sysmsg_frame;
	code = (u_int)frame->tf_rdi;
	if (code >= p->p_sysent->sv_size)
		code = SYS___nosys;
	argp = (union sysunion *)(&frame->tf_rdi + 1);
	callp = &p->p_sysent->sv_table[code];
	narg = callp->sy_narg;

	/*
	 * On x86_64 we get up to six arguments in registers.  The rest are
	 * on the stack.  However, for syscall() and __syscall() the syscall
	 * number is inserted as the first argument, so the limit is reduced
	 * by one to five.
	 */
	if (__predict_false(narg > regcnt)) {
		register_t *argsdst;
		caddr_t params;

		argsdst = (register_t *)&sysmsg->extargs;
		bcopy(argp, argsdst, sizeof(register_t) * regcnt);
		params = (caddr_t)frame->tf_rsp + sizeof(register_t);
		error = copyin(params, &argsdst[regcnt],
			       (narg - regcnt) * sizeof(register_t));
		argp = (void *)argsdst;
		if (error) {
#ifdef KTRACE
			if (KTRPOINTP(p, td, KTR_SYSCALL)) {
				ktrsyscall(td->td_lwp, code, narg, argp);
			}
			if (KTRPOINTP(p, td, KTR_SYSRET)) {
				ktrsysret(td->td_lwp, code, error,
					  sysmsg->sysmsg_result);
			}
#endif
			return error;
		}
	}

#ifdef KTRACE
	if (KTRPOINTP(p, td, KTR_SYSCALL)) {
		ktrsyscall(td->td_lwp, code, narg, argp);
	}
#endif

	error = (*callp->sy_call)(sysmsg, argp);

#ifdef KTRACE
	if (KTRPOINTP(p, td, KTR_SYSRET)) {
		ktrsysret(td->td_lwp, code, error, sysmsg->sysmsg_result);
	}
#endif

	return error;
}

void
fork_return(struct lwp *lp, struct trapframe *frame)
{
	frame->tf_rax = 0;		/* Child returns zero */
	frame->tf_rflags &= ~PSL_C;	/* success */
	frame->tf_rdx = 1;

	generic_lwp_return(lp, frame);
	KTR_LOG(kernentry_fork_ret, lp->lwp_proc->p_pid, lp->lwp_tid);
}

/*
 * Simplified back end of syscall(), used when returning from fork()
 * directly into user mode.
 *
 * This code will return back into the fork trampoline code which then
 * runs doreti.
 */
void
generic_lwp_return(struct lwp *lp, struct trapframe *frame)
{
	struct proc *p = lp->lwp_proc;

	/*
	 * Check for exit-race.  If one lwp exits the process concurrent with
	 * another lwp creating a new thread, the two operations may cross
	 * each other resulting in the newly-created lwp not receiving a
	 * KILL signal.
	 */
	if (p->p_flags & P_WEXIT) {
		lwpsignal(p, lp, SIGKILL);
	}

	/*
	 * Newly forked processes are given a kernel priority.  We have to
	 * adjust the priority to a normal user priority and fake entry
	 * into the kernel (call userenter()) to install a passive release
	 * function just in case userret() decides to stop the process.  This
	 * can occur when ^Z races a fork.  If we do not install the passive
	 * release function the current process designation will not be
	 * released when the thread goes to sleep.
	 */
	lwkt_setpri_self(TDPRI_USER_NORM);
	userenter(lp->lwp_thread, p);
	userret(lp, frame, 0);
#ifdef KTRACE
	if (KTRPOINTP(p, lp->lwp_thread, KTR_SYSRET))
		ktrsysret(lp, SYS_fork, 0, 0);
#endif
	lp->lwp_flags |= LWP_PASSIVE_ACQ;
	userexit(lp);
	lp->lwp_flags &= ~LWP_PASSIVE_ACQ;
}

/*
 * If PGEX_FPFAULT is set then set FP_VIRTFP in the PCB to force a T_DNA
 * fault (which is then passed back to the virtual kernel) if an attempt is
 * made to use the FP unit.
 *
 * XXX this is a fairly big hack.
 */
void
set_vkernel_fp(struct trapframe *frame)
{
	struct thread *td = curthread;

	if (frame->tf_xflags & PGEX_FPFAULT) {
		td->td_pcb->pcb_flags |= FP_VIRTFP;
		if (mdcpu->gd_npxthread == td)
			npxexit();
	} else {
		td->td_pcb->pcb_flags &= ~FP_VIRTFP;
	}
}

/*
 * Called from vkernel_trap() to fixup the vkernel's syscall
 * frame for vmspace_ctl() return.
 */
void
cpu_vkernel_trap(struct trapframe *frame, int error)
{
	frame->tf_rax = error;
	if (error)
		frame->tf_rflags |= PSL_C;
	else
		frame->tf_rflags &= ~PSL_C;
}
