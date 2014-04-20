/*-
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1982, 1987, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 * $FreeBSD: src/sys/i386/i386/machdep.c,v 1.385.2.30 2003/05/31 08:48:05 alc Exp $
 */

#include "use_npx.h"
#include "opt_compat.h"
#include "opt_ddb.h"
#include "opt_directio.h"
#include "opt_inet.h"
#include "opt_msgbuf.h"
#include "opt_swap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/bus.h>
#include <sys/usched.h>
#include <sys/reg.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <sys/user.h>
#include <sys/exec.h>
#include <sys/cons.h>

#include <ddb/ddb.h>

#include <machine/cpu.h>
#include <machine/clock.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>
#include <machine/pcb_ext.h>		/* pcb.h included via sys/user.h */
#include <machine/globaldata.h>		/* CPU_prvspace */
#include <machine/smp.h>
#ifdef PERFMON
#include <machine/perfmon.h>
#endif
#include <machine/cputypes.h>

#include <bus/isa/rtc.h>
#include <machine/vm86.h>
#include <sys/random.h>
#include <sys/ptrace.h>
#include <machine/sigframe.h>
#include <unistd.h>		/* umtx_* functions */
#include <pthread.h>		/* pthread_yield */

extern void dblfault_handler (void);

#ifndef CPU_DISABLE_SSE
static void set_fpregs_xmm (struct save87 *, struct savexmm *);
static void fill_fpregs_xmm (struct savexmm *, struct save87 *);
#endif /* CPU_DISABLE_SSE */
#ifdef DIRECTIO
extern void ffs_rawread_setup(void);
#endif /* DIRECTIO */

int64_t tsc_offsets[MAXCPU];

#if defined(SWTCH_OPTIM_STATS)
extern int swtch_optim_stats;
SYSCTL_INT(_debug, OID_AUTO, swtch_optim_stats,
	CTLFLAG_RD, &swtch_optim_stats, 0, "");
SYSCTL_INT(_debug, OID_AUTO, tlb_flush_count,
	CTLFLAG_RD, &tlb_flush_count, 0, "");
#endif

static int
sysctl_hw_physmem(SYSCTL_HANDLER_ARGS)
{
	u_long pmem = ctob(physmem);

	int error = sysctl_handle_long(oidp, &pmem, 0, req);
	return (error);
}

SYSCTL_PROC(_hw, HW_PHYSMEM, physmem, CTLTYPE_ULONG|CTLFLAG_RD,
	0, 0, sysctl_hw_physmem, "LU", "Total system memory in bytes (number of pages * page size)");

static int
sysctl_hw_usermem(SYSCTL_HANDLER_ARGS)
{
	int error = sysctl_handle_int(oidp, 0,
		ctob((int)Maxmem - vmstats.v_wire_count), req);
	return (error);
}

SYSCTL_PROC(_hw, HW_USERMEM, usermem, CTLTYPE_INT|CTLFLAG_RD,
	0, 0, sysctl_hw_usermem, "IU", "");

SYSCTL_ULONG(_hw, OID_AUTO, availpages, CTLFLAG_RD, &Maxmem, 0, "");

#if 0

static int
sysctl_machdep_msgbuf(SYSCTL_HANDLER_ARGS)
{
	int error;

	/* Unwind the buffer, so that it's linear (possibly starting with
	 * some initial nulls).
	 */
	error=sysctl_handle_opaque(oidp,msgbufp->msg_ptr+msgbufp->msg_bufr,
		msgbufp->msg_size-msgbufp->msg_bufr,req);
	if(error) return(error);
	if(msgbufp->msg_bufr>0) {
		error=sysctl_handle_opaque(oidp,msgbufp->msg_ptr,
			msgbufp->msg_bufr,req);
	}
	return(error);
}

SYSCTL_PROC(_machdep, OID_AUTO, msgbuf, CTLTYPE_STRING|CTLFLAG_RD,
	0, 0, sysctl_machdep_msgbuf, "A","Contents of kernel message buffer");

static int msgbuf_clear;

static int
sysctl_machdep_msgbuf_clear(SYSCTL_HANDLER_ARGS)
{
	int error;
	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2,
		req);
	if (!error && req->newptr) {
		/* Clear the buffer and reset write pointer */
		bzero(msgbufp->msg_ptr,msgbufp->msg_size);
		msgbufp->msg_bufr=msgbufp->msg_bufx=0;
		msgbuf_clear=0;
	}
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, msgbuf_clear, CTLTYPE_INT|CTLFLAG_RW,
	&msgbuf_clear, 0, sysctl_machdep_msgbuf_clear, "I",
	"Clear kernel message buffer");

#endif

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * at top to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
void
sendsig(sig_t catcher, int sig, sigset_t *mask, u_long code)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = lp->lwp_proc;
	struct trapframe *regs;
	struct sigacts *psp = p->p_sigacts;
	struct sigframe sf, *sfp;
	int oonstack;

	regs = lp->lwp_md.md_regs;
	oonstack = (lp->lwp_sigstk.ss_flags & SS_ONSTACK) ? 1 : 0;

	/* save user context */
	bzero(&sf, sizeof(struct sigframe));
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack = lp->lwp_sigstk;
	sf.sf_uc.uc_mcontext.mc_onstack = oonstack;
	bcopy(regs, &sf.sf_uc.uc_mcontext.mc_gs, sizeof(struct trapframe));

	/* make the size of the saved context visible to userland */
	sf.sf_uc.uc_mcontext.mc_len = sizeof(sf.sf_uc.uc_mcontext); 

	/* Allocate and validate space for the signal handler context. */
        if ((lp->lwp_flags & LWP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sfp = (struct sigframe *)(lp->lwp_sigstk.ss_sp +
		    lp->lwp_sigstk.ss_size - sizeof(struct sigframe));
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	}
	else
		sfp = (struct sigframe *)regs->tf_esp - 1;

	/* Translate the signal is appropriate */
	if (p->p_sysent->sv_sigtbl) {
		if (sig <= p->p_sysent->sv_sigsize)
			sig = p->p_sysent->sv_sigtbl[_SIG_IDX(sig)];
	}

	/* Build the argument list for the signal handler. */
	sf.sf_signum = sig;
	sf.sf_ucontext = (register_t)&sfp->sf_uc;
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		sf.sf_siginfo = (register_t)&sfp->sf_si;
		sf.sf_ahu.sf_action = (__siginfohandler_t *)catcher;

		/* fill siginfo structure */
		sf.sf_si.si_signo = sig;
		sf.sf_si.si_code = code;
		sf.sf_si.si_addr = (void*)regs->tf_err;
	}
	else {
		/* Old FreeBSD-style arguments. */
		sf.sf_siginfo = code;
		sf.sf_addr = regs->tf_err;
		sf.sf_ahu.sf_handler = catcher;
	}

#if 0
	/*
	 * If we're a vm86 process, we want to save the segment registers.
	 * We also change eflags to be our emulated eflags, not the actual
	 * eflags.
	 */
	if (regs->tf_eflags & PSL_VM) {
		struct trapframe_vm86 *tf = (struct trapframe_vm86 *)regs;
		struct vm86_kernel *vm86 = &lp->lwp_thread->td_pcb->pcb_ext->ext_vm86;

		sf.sf_uc.uc_mcontext.mc_gs = tf->tf_vm86_gs;
		sf.sf_uc.uc_mcontext.mc_fs = tf->tf_vm86_fs;
		sf.sf_uc.uc_mcontext.mc_es = tf->tf_vm86_es;
		sf.sf_uc.uc_mcontext.mc_ds = tf->tf_vm86_ds;

		if (vm86->vm86_has_vme == 0)
			sf.sf_uc.uc_mcontext.mc_eflags =
			    (tf->tf_eflags & ~(PSL_VIF | PSL_VIP)) |
			    (vm86->vm86_eflags & (PSL_VIF | PSL_VIP));

		/*
		 * Clear PSL_NT to inhibit T_TSSFLT faults on return from
		 * syscalls made by the signal handler.  This just avoids
		 * wasting time for our lazy fixup of such faults.  PSL_NT
		 * does nothing in vm86 mode, but vm86 programs can set it
		 * almost legitimately in probes for old cpu types.
		 */
		tf->tf_eflags &= ~(PSL_VM | PSL_NT | PSL_VIF | PSL_VIP);
	}
#endif
	
	/*
	 * Save the FPU state and reinit the FP unit
	 */
	npxpush(&sf.sf_uc.uc_mcontext);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(struct sigframe)) != 0) {
		/*
		 * Something is wrong with the stack pointer.
		 * ...Kill the process.
		 */
		sigexit(lp, SIGILL);
	}

	regs->tf_esp = (int)sfp;
	regs->tf_eip = PS_STRINGS - *(p->p_sysent->sv_szsigcode);

	/*
	 * i386 abi specifies that the direction flag must be cleared
	 * on function entry
	 */
	regs->tf_eflags &= ~(PSL_T|PSL_D);

	regs->tf_cs = _ucodesel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	if (regs->tf_trapno == T_PROTFLT) {
		regs->tf_fs = _udatasel;
		regs->tf_gs = _udatasel;
	}
	regs->tf_ss = _udatasel;
}

/*
 * Sanitize the trapframe for a virtual kernel passing control to a custom
 * VM context.
 *
 * Allow userland to set or maintain PSL_RF, the resume flag.  This flag
 * basically controls whether the return PC should skip the first instruction
 * (as in an explicit system call) or re-execute it (as in an exception).
 */
int
cpu_sanitize_frame(struct trapframe *frame)
{
	frame->tf_cs = _ucodesel;
	frame->tf_ds = _udatasel;
	frame->tf_es = _udatasel;
#if 0
	frame->tf_fs = _udatasel;
	frame->tf_gs = _udatasel;
#endif
	frame->tf_ss = _udatasel;
	frame->tf_eflags &= (PSL_RF | PSL_USERCHANGE);
	frame->tf_eflags |= PSL_RESERVED_DEFAULT | PSL_I;
	return(0);
}

int
cpu_sanitize_tls(struct savetls *tls)
{
	 struct segment_descriptor *desc;
	 int i;

	 for (i = 0; i < NGTLS; ++i) {
		desc = &tls->tls[i];
		if (desc->sd_dpl == 0 && desc->sd_type == 0)
			continue;
		if (desc->sd_def32 == 0)
			return(ENXIO);
		if (desc->sd_type != SDT_MEMRWA)
			return(ENXIO);
		if (desc->sd_dpl != SEL_UPL)
			return(ENXIO);
		if (desc->sd_xx != 0 || desc->sd_p != 1)
			return(ENXIO);
	 }
	 return(0);
}

/*
 * sigreturn(ucontext_t *sigcntxp)
 *
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * state to gain improper privileges.
 *
 * MPSAFE
 */
#define	EFL_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)
#define	CS_SECURE(cs)		(ISPL(cs) == SEL_UPL)

int
sys_sigreturn(struct sigreturn_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct trapframe *regs;
	ucontext_t ucp;
	int cs;
	int eflags;
	int error;

	error = copyin(uap->sigcntxp, &ucp, sizeof(ucp));
	if (error)
		return (error);

	regs = lp->lwp_md.md_regs;
	eflags = ucp.uc_mcontext.mc_eflags;

#if 0
	if (eflags & PSL_VM) {
		struct trapframe_vm86 *tf = (struct trapframe_vm86 *)regs;
		struct vm86_kernel *vm86;

		/*
		 * if pcb_ext == 0 or vm86_inited == 0, the user hasn't
		 * set up the vm86 area, and we can't enter vm86 mode.
		 */
		if (lp->lwp_thread->td_pcb->pcb_ext == 0)
			return (EINVAL);
		vm86 = &lp->lwp_thread->td_pcb->pcb_ext->ext_vm86;
		if (vm86->vm86_inited == 0)
			return (EINVAL);

		/* go back to user mode if both flags are set */
		if ((eflags & PSL_VIP) && (eflags & PSL_VIF))
			trapsignal(lp->lwp_proc, SIGBUS, 0);

		if (vm86->vm86_has_vme) {
			eflags = (tf->tf_eflags & ~VME_USERCHANGE) |
			    (eflags & VME_USERCHANGE) | PSL_VM;
		} else {
			vm86->vm86_eflags = eflags;	/* save VIF, VIP */
			eflags = (tf->tf_eflags & ~VM_USERCHANGE) |					    (eflags & VM_USERCHANGE) | PSL_VM;
		}
		bcopy(&ucp.uc_mcontext.mc_gs, tf, sizeof(struct trapframe));
		tf->tf_eflags = eflags;
		tf->tf_vm86_ds = tf->tf_ds;
		tf->tf_vm86_es = tf->tf_es;
		tf->tf_vm86_fs = tf->tf_fs;
		tf->tf_vm86_gs = tf->tf_gs;
		tf->tf_ds = _udatasel;
		tf->tf_es = _udatasel;
#if 0
		tf->tf_fs = _udatasel;
		tf->tf_gs = _udatasel;
#endif
	} else 
#endif
	{
		/*
		 * Don't allow users to change privileged or reserved flags.
		 */
		/*
		 * XXX do allow users to change the privileged flag PSL_RF.
		 * The cpu sets PSL_RF in tf_eflags for faults.  Debuggers
		 * should sometimes set it there too.  tf_eflags is kept in
		 * the signal context during signal handling and there is no
		 * other place to remember it, so the PSL_RF bit may be
		 * corrupted by the signal handler without us knowing.
		 * Corruption of the PSL_RF bit at worst causes one more or
		 * one less debugger trap, so allowing it is fairly harmless.
		 */
		if (!EFL_SECURE(eflags & ~PSL_RF, regs->tf_eflags & ~PSL_RF)) {
			kprintf("sigreturn: eflags = 0x%x\n", eflags);
	    		return(EINVAL);
		}

		/*
		 * Don't allow users to load a valid privileged %cs.  Let the
		 * hardware check for invalid selectors, excess privilege in
		 * other selectors, invalid %eip's and invalid %esp's.
		 */
		cs = ucp.uc_mcontext.mc_cs;
		if (!CS_SECURE(cs)) {
			kprintf("sigreturn: cs = 0x%x\n", cs);
			trapsignal(lp, SIGBUS, T_PROTFLT);
			return(EINVAL);
		}
		bcopy(&ucp.uc_mcontext.mc_gs, regs, sizeof(struct trapframe));
	}

	/*
	 * Restore the FPU state from the frame
	 */
	crit_enter();
	npxpop(&ucp.uc_mcontext);

	if (ucp.uc_mcontext.mc_onstack & 1)
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	else
		lp->lwp_sigstk.ss_flags &= ~SS_ONSTACK;

	lp->lwp_sigmask = ucp.uc_sigmask;
	SIG_CANTMASK(lp->lwp_sigmask);
	crit_exit();
	return(EJUSTRETURN);
}

/*
 * cpu_idle() represents the idle LWKT.  You cannot return from this function
 * (unless you want to blow things up!).  Instead we look for runnable threads
 * and loop or halt as appropriate.  Giant is not held on entry to the thread.
 *
 * The main loop is entered with a critical section held, we must release
 * the critical section before doing anything else.  lwkt_switch() will
 * check for pending interrupts due to entering and exiting its own 
 * critical section.
 *
 * Note on cpu_idle_hlt:  On an SMP system we rely on a scheduler IPI
 * to wake a HLTed cpu up.
 */
static int	cpu_idle_hlt = 1;
static int	cpu_idle_hltcnt;
static int	cpu_idle_spincnt;
SYSCTL_INT(_machdep, OID_AUTO, cpu_idle_hlt, CTLFLAG_RW,
    &cpu_idle_hlt, 0, "Idle loop HLT enable");
SYSCTL_INT(_machdep, OID_AUTO, cpu_idle_hltcnt, CTLFLAG_RW,
    &cpu_idle_hltcnt, 0, "Idle loop entry halts");
SYSCTL_INT(_machdep, OID_AUTO, cpu_idle_spincnt, CTLFLAG_RW,
    &cpu_idle_spincnt, 0, "Idle loop entry spins");

void
cpu_idle(void)
{
	struct thread *td = curthread;
	struct mdglobaldata *gd = mdcpu;
	int reqflags;

	crit_exit();
	KKASSERT(td->td_critcount == 0);
	cpu_enable_intr();
	for (;;) {
		/*
		 * See if there are any LWKTs ready to go.
		 */
		lwkt_switch();

		/*
		 * The idle loop halts only if no threads are scheduleable
		 * and no signals have occured.
		 */
		if (cpu_idle_hlt &&
		    (td->td_gd->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
			splz();
			KKASSERT(MP_LOCK_HELD() == 0);
			if ((td->td_gd->gd_reqflags & RQF_IDLECHECK_WK_MASK) == 0) {
#ifdef DEBUGIDLE
				struct timeval tv1, tv2;
				gettimeofday(&tv1, NULL);
#endif
				reqflags = gd->mi.gd_reqflags &
					   ~RQF_IDLECHECK_WK_MASK;
				umtx_sleep(&gd->mi.gd_reqflags, reqflags,
					   1000000);
#ifdef DEBUGIDLE
				gettimeofday(&tv2, NULL);
				if (tv2.tv_usec - tv1.tv_usec +
				    (tv2.tv_sec - tv1.tv_sec) * 1000000 
				    > 500000) {
					kprintf("cpu %d idlelock %08x %08x\n",
						gd->mi.gd_cpuid,
						gd->mi.gd_reqflags,
						gd->gd_fpending);
				}
#endif
			}
			++cpu_idle_hltcnt;
		} else {
			splz();
			__asm __volatile("pause");
			++cpu_idle_spincnt;
		}
	}
}

/*
 * Called by the spinlock code with or without a critical section held
 * when a spinlock is found to be seriously constested.
 *
 * We need to enter a critical section to prevent signals from recursing
 * into pthreads.
 */
void
cpu_spinlock_contested(void)
{
	cpu_pause();
}

/*
 * Clear registers on exec
 */
void
exec_setregs(u_long entry, u_long stack, u_long ps_strings)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct trapframe *regs = lp->lwp_md.md_regs;
	struct pcb *pcb = lp->lwp_thread->td_pcb;

	/* was i386_user_cleanup() in NetBSD */
	user_ldt_free(pcb);
  
	bzero((char *)regs, sizeof(struct trapframe));
	regs->tf_eip = entry;
	regs->tf_esp = stack;
	regs->tf_eflags = PSL_USER | (regs->tf_eflags & PSL_T);
	regs->tf_ss = 0;
	regs->tf_ds = 0;
	regs->tf_es = 0;
	regs->tf_fs = 0;
	regs->tf_gs = 0;
	regs->tf_cs = 0;

	/* PS_STRINGS value for BSD/OS binaries.  It is 0 for non-BSD/OS. */
	regs->tf_ebx = ps_strings;

        /*
         * Reset the hardware debug registers if they were in use.
         * They won't have any meaning for the newly exec'd process.  
         */
        if (pcb->pcb_flags & PCB_DBREGS) {
                pcb->pcb_dr0 = 0;
                pcb->pcb_dr1 = 0;
                pcb->pcb_dr2 = 0;
                pcb->pcb_dr3 = 0;
                pcb->pcb_dr6 = 0;
                pcb->pcb_dr7 = 0;
                if (pcb == td->td_pcb) {
		        /*
			 * Clear the debug registers on the running
			 * CPU, otherwise they will end up affecting
			 * the next process we switch to.
			 */
		        reset_dbregs();
                }
                pcb->pcb_flags &= ~PCB_DBREGS;
        }

	/*
	 * Initialize the math emulator (if any) for the current process.
	 * Actually, just clear the bit that says that the emulator has
	 * been initialized.  Initialization is delayed until the process
	 * traps to the emulator (if it is done at all) mainly because
	 * emulators don't provide an entry point for initialization.
	 */
	pcb->pcb_flags &= ~FP_SOFTFP;

	/*
	 * note: do not set CR0_TS here.  npxinit() must do it after clearing
	 * gd_npxthread.  Otherwise a preemptive interrupt thread may panic
	 * in npxdna().
	 */
	crit_enter();
#if 0
	load_cr0(rcr0() | CR0_MP);
#endif

#if NNPX > 0
	/* Initialize the npx (if any) for the current process. */
	npxinit(__INITIAL_NPXCW__);
#endif
	crit_exit();

	/*
	 * note: linux emulator needs edx to be 0x0 on entry, which is
	 * handled in execve simply by setting the 64 bit syscall
	 * return value to 0.
	 */
}

void
cpu_setregs(void)
{
#if 0
	unsigned int cr0;

	cr0 = rcr0();
	cr0 |= CR0_NE;			/* Done by npxinit() */
	cr0 |= CR0_MP | CR0_TS;		/* Done at every execve() too. */
	cr0 |= CR0_WP | CR0_AM;
	load_cr0(cr0);
	load_gs(_udatasel);
#endif
}

static int
sysctl_machdep_adjkerntz(SYSCTL_HANDLER_ARGS)
{
	int error;
	error = sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2,
		req);
	if (!error && req->newptr)
		resettodr();
	return (error);
}

SYSCTL_PROC(_machdep, CPU_ADJKERNTZ, adjkerntz, CTLTYPE_INT|CTLFLAG_RW,
	&adjkerntz, 0, sysctl_machdep_adjkerntz, "I", "");

extern u_long bootdev;		/* not a cdev_t - encoding is different */
SYSCTL_ULONG(_machdep, OID_AUTO, guessed_bootdev,
	CTLFLAG_RD, &bootdev, 0, "Boot device (not in cdev_t format)");

/*
 * Initialize 386 and configure to run kernel
 */

/*
 * Initialize segments & interrupt table
 */

extern  struct user *proc0paddr;

#if 0

extern inthand_t
	IDTVEC(div), IDTVEC(dbg), IDTVEC(nmi), IDTVEC(bpt), IDTVEC(ofl),
	IDTVEC(bnd), IDTVEC(ill), IDTVEC(dna), IDTVEC(fpusegm),
	IDTVEC(tss), IDTVEC(missing), IDTVEC(stk), IDTVEC(prot),
	IDTVEC(page), IDTVEC(mchk), IDTVEC(fpu), IDTVEC(align),
	IDTVEC(xmm), IDTVEC(syscall),
	IDTVEC(rsvd0);
extern inthand_t
	IDTVEC(int0x80_syscall);

#endif

#ifdef DEBUG_INTERRUPTS
extern inthand_t *Xrsvdary[256];
#endif

int
ptrace_set_pc(struct lwp *lp, unsigned long addr)
{
	lp->lwp_md.md_regs->tf_eip = addr;
	return (0);
}

int
ptrace_single_step(struct lwp *lp)
{
	lp->lwp_md.md_regs->tf_eflags |= PSL_T;
	return (0);
}

int
fill_regs(struct lwp *lp, struct reg *regs)
{
	struct trapframe *tp;

	if ((tp = lp->lwp_md.md_regs) == NULL)
		return EINVAL;
	regs->r_gs = tp->tf_gs;
	regs->r_fs = tp->tf_fs;
	regs->r_es = tp->tf_es;
	regs->r_ds = tp->tf_ds;
	regs->r_edi = tp->tf_edi;
	regs->r_esi = tp->tf_esi;
	regs->r_ebp = tp->tf_ebp;
	regs->r_ebx = tp->tf_ebx;
	regs->r_edx = tp->tf_edx;
	regs->r_ecx = tp->tf_ecx;
	regs->r_eax = tp->tf_eax;
	regs->r_eip = tp->tf_eip;
	regs->r_cs = tp->tf_cs;
	regs->r_eflags = tp->tf_eflags;
	regs->r_esp = tp->tf_esp;
	regs->r_ss = tp->tf_ss;
	return (0);
}

int
set_regs(struct lwp *lp, struct reg *regs)
{
	struct trapframe *tp;

	tp = lp->lwp_md.md_regs;
	if (!EFL_SECURE(regs->r_eflags, tp->tf_eflags) ||
	    !CS_SECURE(regs->r_cs))
		return (EINVAL);
	tp->tf_gs = regs->r_gs;
	tp->tf_fs = regs->r_fs;
	tp->tf_es = regs->r_es;
	tp->tf_ds = regs->r_ds;
	tp->tf_edi = regs->r_edi;
	tp->tf_esi = regs->r_esi;
	tp->tf_ebp = regs->r_ebp;
	tp->tf_ebx = regs->r_ebx;
	tp->tf_edx = regs->r_edx;
	tp->tf_ecx = regs->r_ecx;
	tp->tf_eax = regs->r_eax;
	tp->tf_eip = regs->r_eip;
	tp->tf_cs = regs->r_cs;
	tp->tf_eflags = regs->r_eflags;
	tp->tf_esp = regs->r_esp;
	tp->tf_ss = regs->r_ss;
	return (0);
}

#ifndef CPU_DISABLE_SSE
static void
fill_fpregs_xmm(struct savexmm *sv_xmm, struct save87 *sv_87)
{
	struct env87 *penv_87 = &sv_87->sv_env;
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	int i;

	/* FPU control/status */
	penv_87->en_cw = penv_xmm->en_cw;
	penv_87->en_sw = penv_xmm->en_sw;
	penv_87->en_tw = penv_xmm->en_tw;
	penv_87->en_fip = penv_xmm->en_fip;
	penv_87->en_fcs = penv_xmm->en_fcs;
	penv_87->en_opcode = penv_xmm->en_opcode;
	penv_87->en_foo = penv_xmm->en_foo;
	penv_87->en_fos = penv_xmm->en_fos;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		sv_87->sv_ac[i] = sv_xmm->sv_fp[i].fp_acc;
}

static void
set_fpregs_xmm(struct save87 *sv_87, struct savexmm *sv_xmm)
{
	struct env87 *penv_87 = &sv_87->sv_env;
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	int i;

	/* FPU control/status */
	penv_xmm->en_cw = penv_87->en_cw;
	penv_xmm->en_sw = penv_87->en_sw;
	penv_xmm->en_tw = penv_87->en_tw;
	penv_xmm->en_fip = penv_87->en_fip;
	penv_xmm->en_fcs = penv_87->en_fcs;
	penv_xmm->en_opcode = penv_87->en_opcode;
	penv_xmm->en_foo = penv_87->en_foo;
	penv_xmm->en_fos = penv_87->en_fos;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		sv_xmm->sv_fp[i].fp_acc = sv_87->sv_ac[i];
}
#endif /* CPU_DISABLE_SSE */

int
fill_fpregs(struct lwp *lp, struct fpreg *fpregs)
{
	if (lp->lwp_thread == NULL || lp->lwp_thread->td_pcb == NULL)
		return EINVAL;
#ifndef CPU_DISABLE_SSE
	if (cpu_fxsr) {
		fill_fpregs_xmm(&lp->lwp_thread->td_pcb->pcb_save.sv_xmm,
				(struct save87 *)fpregs);
		return (0);
	}
#endif /* CPU_DISABLE_SSE */
	bcopy(&lp->lwp_thread->td_pcb->pcb_save.sv_87, fpregs, sizeof *fpregs);
	return (0);
}

int
set_fpregs(struct lwp *lp, struct fpreg *fpregs)
{
#ifndef CPU_DISABLE_SSE
	if (cpu_fxsr) {
		set_fpregs_xmm((struct save87 *)fpregs,
			       &lp->lwp_thread->td_pcb->pcb_save.sv_xmm);
		return (0);
	}
#endif /* CPU_DISABLE_SSE */
	bcopy(fpregs, &lp->lwp_thread->td_pcb->pcb_save.sv_87, sizeof *fpregs);
	return (0);
}

int
fill_dbregs(struct lwp *lp, struct dbreg *dbregs)
{
	return (ENOSYS);
}

int
set_dbregs(struct lwp *lp, struct dbreg *dbregs)
{
	return (ENOSYS);
}

#if 0
/*
 * Return > 0 if a hardware breakpoint has been hit, and the
 * breakpoint was in user space.  Return 0, otherwise.
 */
int
user_dbreg_trap(void)
{
        u_int32_t dr7, dr6; /* debug registers dr6 and dr7 */
        u_int32_t bp;       /* breakpoint bits extracted from dr6 */
        int nbp;            /* number of breakpoints that triggered */
        caddr_t addr[4];    /* breakpoint addresses */
        int i;
        
        dr7 = rdr7();
        if ((dr7 & 0x000000ff) == 0) {
                /*
                 * all GE and LE bits in the dr7 register are zero,
                 * thus the trap couldn't have been caused by the
                 * hardware debug registers
                 */
                return 0;
        }

        nbp = 0;
        dr6 = rdr6();
        bp = dr6 & 0x0000000f;

        if (!bp) {
                /*
                 * None of the breakpoint bits are set meaning this
                 * trap was not caused by any of the debug registers
                 */
                return 0;
        }

        /*
         * at least one of the breakpoints were hit, check to see
         * which ones and if any of them are user space addresses
         */

        if (bp & 0x01) {
                addr[nbp++] = (caddr_t)rdr0();
        }
        if (bp & 0x02) {
                addr[nbp++] = (caddr_t)rdr1();
        }
        if (bp & 0x04) {
                addr[nbp++] = (caddr_t)rdr2();
        }
        if (bp & 0x08) {
                addr[nbp++] = (caddr_t)rdr3();
        }

        for (i=0; i<nbp; i++) {
                if (addr[i] <
                    (caddr_t)VM_MAX_USER_ADDRESS) {
                        /*
                         * addr[i] is in user space
                         */
                        return nbp;
                }
        }

        /*
         * None of the breakpoints are in user space.
         */
        return 0;
}

#endif

void
identcpu(void)
{
	int regs[4];

	do_cpuid(1, regs);
	cpu_feature = regs[3];
}


#ifndef DDB
void
Debugger(const char *msg)
{
	kprintf("Debugger(\"%s\") called.\n", msg);
}
#endif /* no DDB */

