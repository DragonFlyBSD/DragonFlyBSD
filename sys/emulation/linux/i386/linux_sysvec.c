/*-
 * Copyright (c) 1994-1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/linux/linux_sysvec.c,v 1.55.2.9 2002/01/12 11:03:30 bde Exp $
 */

/* XXX we use functions that might not exist. */
#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/imgact_elf.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/eventhandler.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/cpu.h>

#include "linux.h"
#include "linux_proto.h"
#include "../linux_signal.h"
#include "../linux_util.h"
#include "../linux_futex.h"
#include "../linux_emuldata.h"

MODULE_VERSION(linux, 1);

MALLOC_DEFINE(M_LINUX, "linux", "Linux mode structures");

#if BYTE_ORDER == LITTLE_ENDIAN
#define SHELLMAGIC      0x2123 /* #! */
#else
#define SHELLMAGIC      0x2321
#endif

/*
 * Allow the sendsig functions to use the ldebug() facility
 * even though they are not syscalls themselves. Map them
 * to syscall 0. This is slightly less bogus than using
 * ldebug(sigreturn).
 */
#define	LINUX_SYS_linux_rt_sendsig	0
#define	LINUX_SYS_linux_sendsig		0

extern char linux_sigcode[];
extern int linux_szsigcode;

extern struct sysent linux_sysent[LINUX_SYS_MAXSYSCALL];

static int	linux_fixup (register_t **stack_base,
				 struct image_params *iparams);
static int	elf_linux_fixup (register_t **stack_base,
				     struct image_params *iparams);
static void	linux_prepsyscall (struct trapframe *tf, int *args,
				       u_int *code, caddr_t *params);
static void     linux_sendsig (sig_t catcher, int sig, sigset_t *mask,
				   u_long code);
static boolean_t linux_trans_osrel(const Elf_Note *note, int32_t *osrel);

static eventhandler_tag linux_exec_tag;
static eventhandler_tag linux_exit_tag;

/*
 * Linux syscalls return negative errno's, we do positive and map them
 */
static int bsd_to_linux_errno[ELAST + 1] = {
  	-0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,  -8,  -9,
 	-10, -35, -12, -13, -14, -15, -16, -17, -18, -19,
 	-20, -21, -22, -23, -24, -25, -26, -27, -28, -29,
 	-30, -31, -32, -33, -34, -11,-115,-114, -88, -89,
 	-90, -91, -92, -93, -94, -95, -96, -97, -98, -99,
	-100,-101,-102,-103,-104,-105,-106,-107,-108,-109,
	-110,-111, -40, -36,-112,-113, -39, -11, -87,-122,
	-116, -66,  -6,  -6,  -6,  -6,  -6, -37, -38,  -9,
  	-6, -6, -43, -42, -75, -6, -84
};

int bsd_to_linux_signal[LINUX_SIGTBLSZ] = {
	LINUX_SIGHUP, LINUX_SIGINT, LINUX_SIGQUIT, LINUX_SIGILL,
	LINUX_SIGTRAP, LINUX_SIGABRT, 0, LINUX_SIGFPE,
	LINUX_SIGKILL, LINUX_SIGBUS, LINUX_SIGSEGV, LINUX_SIGSYS,
	LINUX_SIGPIPE, LINUX_SIGALRM, LINUX_SIGTERM, LINUX_SIGURG,
	LINUX_SIGSTOP, LINUX_SIGTSTP, LINUX_SIGCONT, LINUX_SIGCHLD,
	LINUX_SIGTTIN, LINUX_SIGTTOU, LINUX_SIGIO, LINUX_SIGXCPU,
	LINUX_SIGXFSZ, LINUX_SIGVTALRM, LINUX_SIGPROF, LINUX_SIGWINCH,
	0, LINUX_SIGUSR1, LINUX_SIGUSR2
};

int linux_to_bsd_signal[LINUX_SIGTBLSZ] = {
	SIGHUP, SIGINT, SIGQUIT, SIGILL,
	SIGTRAP, SIGABRT, SIGBUS, SIGFPE,
	SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2,
	SIGPIPE, SIGALRM, SIGTERM, SIGBUS,
	SIGCHLD, SIGCONT, SIGSTOP, SIGTSTP,
	SIGTTIN, SIGTTOU, SIGURG, SIGXCPU,
	SIGXFSZ, SIGVTALRM, SIGPROF, SIGWINCH,
	SIGIO, SIGURG, SIGSYS
};

#define LINUX_T_UNKNOWN  255
static int _bsd_to_linux_trapcode[] = {
	LINUX_T_UNKNOWN,	/* 0 */
	6,			/* 1  T_PRIVINFLT */
	LINUX_T_UNKNOWN,	/* 2 */
	3,			/* 3  T_BPTFLT */
	LINUX_T_UNKNOWN,	/* 4 */
	LINUX_T_UNKNOWN,	/* 5 */
	16,			/* 6  T_ARITHTRAP */
	254,			/* 7  T_ASTFLT */
	LINUX_T_UNKNOWN,	/* 8 */
	13,			/* 9  T_PROTFLT */
	1,			/* 10 T_TRCTRAP */
	LINUX_T_UNKNOWN,	/* 11 */
	14,			/* 12 T_PAGEFLT */
	LINUX_T_UNKNOWN,	/* 13 */
	17,			/* 14 T_ALIGNFLT */
	LINUX_T_UNKNOWN,	/* 15 */
	LINUX_T_UNKNOWN,	/* 16 */
	LINUX_T_UNKNOWN,	/* 17 */
	0,			/* 18 T_DIVIDE */
	2,			/* 19 T_NMI */
	4,			/* 20 T_OFLOW */
	5,			/* 21 T_BOUND */
	7,			/* 22 T_DNA */
	8,			/* 23 T_DOUBLEFLT */
	9,			/* 24 T_FPOPFLT */
	10,			/* 25 T_TSSFLT */
	11,			/* 26 T_SEGNPFLT */
	12,			/* 27 T_STKFLT */
	18,			/* 28 T_MCHK */
	19,			/* 29 T_XMMFLT */
	15			/* 30 T_RESERVED */
};
#define bsd_to_linux_trapcode(code) \
    ((code)<NELEM(_bsd_to_linux_trapcode)? \
     _bsd_to_linux_trapcode[(code)]: \
     LINUX_T_UNKNOWN)

/*
 * If FreeBSD & Linux have a difference of opinion about what a trap
 * means, deal with it here.
 */
static int
translate_traps(int signal, int trap_code)
{
	if (signal != SIGBUS)
		return signal;
	switch (trap_code) {
	case T_PROTFLT:
	case T_TSSFLT:
	case T_DOUBLEFLT:
	case T_PAGEFLT:
		return SIGSEGV;
	default:
		return signal;
	}
}

static int
linux_fixup(register_t **stack_base, struct image_params *imgp)
{
	register_t *argv, *envp;

	argv = *stack_base;
	envp = *stack_base + (imgp->args->argc + 1);
	(*stack_base)--;
	**stack_base = (intptr_t)(void *)envp;
	(*stack_base)--;
	**stack_base = (intptr_t)(void *)argv;
	(*stack_base)--;
	**stack_base = imgp->args->argc;
	return 0;
}

static int
elf_linux_fixup(register_t **stack_base, struct image_params *imgp)
{
	Elf32_Auxargs *args = (Elf32_Auxargs *)imgp->auxargs;
	register_t *pos;
             
	pos = *stack_base + (imgp->args->argc + imgp->args->envc + 2);  
    
	if (args->execfd != -1) {
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	}       
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	AUXARGS_ENTRY(pos, AT_UID, imgp->proc->p_ucred->cr_ruid);
	AUXARGS_ENTRY(pos, AT_EUID, imgp->proc->p_ucred->cr_svuid);
	AUXARGS_ENTRY(pos, AT_GID, imgp->proc->p_ucred->cr_rgid);
	AUXARGS_ENTRY(pos, AT_EGID, imgp->proc->p_ucred->cr_svgid);
	AUXARGS_ENTRY(pos, AT_NULL, 0);
	
	kfree(imgp->auxargs, M_TEMP);      
	imgp->auxargs = NULL;

	(*stack_base)--;
	**stack_base = (long)imgp->args->argc;
	return 0;
}

extern int _ucodesel, _udatasel;
extern unsigned long linux_sznonrtsigcode;

static void
linux_rt_sendsig(sig_t catcher, int sig, sigset_t *mask, u_long code)
{
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	struct trapframe *regs;
	struct l_rt_sigframe *fp, frame;
	int oonstack;

	regs = lp->lwp_md.md_regs;
	oonstack = lp->lwp_sigstk.ss_flags & SS_ONSTACK;

#ifdef DEBUG
	if (ldebug(rt_sendsig))
		kprintf(ARGS(rt_sendsig, "%p, %d, %p, %lu"),
		    catcher, sig, (void*)mask, code);
#endif
	/*
	 * Allocate space for the signal handler context.
	 */
	if ((lp->lwp_flags & LWP_ALTSTACK) && !oonstack &&
	    SIGISMEMBER(p->p_sigacts->ps_sigonstack, sig)) {
		fp = (struct l_rt_sigframe *)(lp->lwp_sigstk.ss_sp +
		    lp->lwp_sigstk.ss_size - sizeof(struct l_rt_sigframe));
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	} else
		fp = (struct l_rt_sigframe *)regs->tf_esp - 1;

	/*
	 * grow() will return FALSE if the fp will not fit inside the stack
	 *	and the stack can not be grown. useracc will return FALSE
	 *	if access is denied.
	 */
	if ((vm_map_growstack(p, (vm_offset_t)fp) != KERN_SUCCESS) ||
	    !useracc((caddr_t)fp, sizeof (struct l_rt_sigframe), 
	    VM_PROT_WRITE)) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		SIGACTION(p, SIGILL) = SIG_DFL;
		SIGDELSET(p->p_sigignore, SIGILL);
		SIGDELSET(p->p_sigcatch, SIGILL);
		SIGDELSET(lp->lwp_sigmask, SIGILL);
#ifdef DEBUG
		if (ldebug(rt_sendsig))
			kprintf(LMSG("rt_sendsig: bad stack %p, oonstack=%x"),
			    fp, oonstack);
#endif
		lwpsignal(p, lp, SIGILL);
		return;
	}

	/*
	 * Build the argument list for the signal handler.
	 */
	if (p->p_sysent->sv_sigtbl)
		if (sig <= p->p_sysent->sv_sigsize)
			sig = p->p_sysent->sv_sigtbl[_SIG_IDX(sig)];

	frame.sf_handler = catcher;
	frame.sf_sig = sig;
	frame.sf_siginfo = &fp->sf_si;
	frame.sf_ucontext = &fp->sf_sc;

	/* Fill siginfo structure. */
	frame.sf_si.lsi_signo = sig;
	frame.sf_si.lsi_code = code;
	frame.sf_si.lsi_addr = (void *)regs->tf_err;

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	frame.sf_sc.uc_flags = 0;		/* XXX ??? */
	frame.sf_sc.uc_link = NULL;		/* XXX ??? */

	frame.sf_sc.uc_stack.ss_sp = lp->lwp_sigstk.ss_sp;
	frame.sf_sc.uc_stack.ss_size = lp->lwp_sigstk.ss_size;
	frame.sf_sc.uc_stack.ss_flags = (lp->lwp_flags & LWP_ALTSTACK)
	    ? ((oonstack) ? LINUX_SS_ONSTACK : 0) : LINUX_SS_DISABLE;

	bsd_to_linux_sigset(mask, &frame.sf_sc.uc_sigmask);

	frame.sf_sc.uc_mcontext.sc_mask   = frame.sf_sc.uc_sigmask.__bits[0];
	frame.sf_sc.uc_mcontext.sc_gs     = regs->tf_gs;
	frame.sf_sc.uc_mcontext.sc_fs     = regs->tf_fs;
	frame.sf_sc.uc_mcontext.sc_es     = regs->tf_es;
	frame.sf_sc.uc_mcontext.sc_ds     = regs->tf_ds;
	frame.sf_sc.uc_mcontext.sc_edi    = regs->tf_edi;
	frame.sf_sc.uc_mcontext.sc_esi    = regs->tf_esi;
	frame.sf_sc.uc_mcontext.sc_ebp    = regs->tf_ebp;
	frame.sf_sc.uc_mcontext.sc_ebx    = regs->tf_ebx;
	frame.sf_sc.uc_mcontext.sc_edx    = regs->tf_edx;
	frame.sf_sc.uc_mcontext.sc_ecx    = regs->tf_ecx;
	frame.sf_sc.uc_mcontext.sc_eax    = regs->tf_eax;
	frame.sf_sc.uc_mcontext.sc_eip    = regs->tf_eip;
	frame.sf_sc.uc_mcontext.sc_cs     = regs->tf_cs;
	frame.sf_sc.uc_mcontext.sc_eflags = regs->tf_eflags;
	frame.sf_sc.uc_mcontext.sc_esp_at_signal = regs->tf_esp;
	frame.sf_sc.uc_mcontext.sc_ss     = regs->tf_ss;
	frame.sf_sc.uc_mcontext.sc_err    = regs->tf_err;
	frame.sf_sc.uc_mcontext.sc_trapno = bsd_to_linux_trapcode(code);

#ifdef DEBUG
	if (ldebug(rt_sendsig))
		kprintf(LMSG("rt_sendsig flags: 0x%x, sp: %p, ss: 0x%x, mask: 0x%x"),
		    frame.sf_sc.uc_stack.ss_flags, lp->lwp_sigstk.ss_sp,
		    lp->lwp_sigstk.ss_size, frame.sf_sc.uc_mcontext.sc_mask);
#endif

	if (copyout(&frame, fp, sizeof(frame)) != 0) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		sigexit(lp, SIGILL);
		/* NOTREACHED */
	}

	/*
	 * Build context to run handler in.
	 */
	regs->tf_esp = (int)fp;
	regs->tf_eip = PS_STRINGS - *(p->p_sysent->sv_szsigcode) + 
	    linux_sznonrtsigcode;

	/*
	 * i386 abi specifies that the direction flag must be cleared
	 * on function entry
	 */
	regs->tf_eflags &= ~(PSL_T | PSL_VM | PSL_D);

	regs->tf_cs = _ucodesel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	/* allow %fs and %gs to be inherited by the signal handler */
	/*
	regs->tf_fs = _udatasel;
	regs->tf_gs = _udatasel;
	*/
	regs->tf_ss = _udatasel;
	clear_quickret();
}


/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * in u. to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */

static void
linux_sendsig(sig_t catcher, int sig, sigset_t *mask, u_long code)
{
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	struct trapframe *regs;
	struct l_sigframe *fp, frame;
	l_sigset_t lmask;
	int oonstack, i;

	if (SIGISMEMBER(p->p_sigacts->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		linux_rt_sendsig(catcher, sig, mask, code);
		return;
	}

	regs = lp->lwp_md.md_regs;
	oonstack = lp->lwp_sigstk.ss_flags & SS_ONSTACK;

#ifdef DEBUG
	if (ldebug(sendsig))
		kprintf(ARGS(sendsig, "%p, %d, %p, %lu"),
		    catcher, sig, (void*)mask, code);
#endif

	/*
	 * Allocate space for the signal handler context.
	 */
	if ((lp->lwp_flags & LWP_ALTSTACK) && !oonstack &&
	    SIGISMEMBER(p->p_sigacts->ps_sigonstack, sig)) {
		fp = (struct l_sigframe *)(lp->lwp_sigstk.ss_sp +
		    lp->lwp_sigstk.ss_size - sizeof(struct l_sigframe));
		lp->lwp_sigstk.ss_flags |= SS_ONSTACK;
	} else
		fp = (struct l_sigframe *)regs->tf_esp - 1;

	/*
	 * grow() will return FALSE if the fp will not fit inside the stack
	 *	and the stack can not be grown. useracc will return FALSE
	 *	if access is denied.
	 */
	if ((vm_map_growstack(p, (vm_offset_t)fp) != KERN_SUCCESS) ||
	    !useracc((caddr_t)fp, sizeof (struct l_sigframe), 
	    VM_PROT_WRITE)) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		SIGACTION(p, SIGILL) = SIG_DFL;
		SIGDELSET(p->p_sigignore, SIGILL);
		SIGDELSET(p->p_sigcatch, SIGILL);
		SIGDELSET(lp->lwp_sigmask, SIGILL);
		lwpsignal(p, lp, SIGILL);
		return;
	}

	/*
	 * Build the argument list for the signal handler.
	 */
	if (p->p_sysent->sv_sigtbl)
		if (sig <= p->p_sysent->sv_sigsize)
			sig = p->p_sysent->sv_sigtbl[_SIG_IDX(sig)];

	frame.sf_handler = catcher;
	frame.sf_sig = sig;

	bsd_to_linux_sigset(mask, &lmask);

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	frame.sf_sc.sc_mask   = lmask.__bits[0];
	frame.sf_sc.sc_gs     = regs->tf_gs;
	frame.sf_sc.sc_fs     = regs->tf_fs;
	frame.sf_sc.sc_es     = regs->tf_es;
	frame.sf_sc.sc_ds     = regs->tf_ds;
	frame.sf_sc.sc_edi    = regs->tf_edi;
	frame.sf_sc.sc_esi    = regs->tf_esi;
	frame.sf_sc.sc_ebp    = regs->tf_ebp;
	frame.sf_sc.sc_ebx    = regs->tf_ebx;
	frame.sf_sc.sc_edx    = regs->tf_edx;
	frame.sf_sc.sc_ecx    = regs->tf_ecx;
	frame.sf_sc.sc_eax    = regs->tf_eax;
	frame.sf_sc.sc_eip    = regs->tf_eip;
	frame.sf_sc.sc_cs     = regs->tf_cs;
	frame.sf_sc.sc_eflags = regs->tf_eflags;
	frame.sf_sc.sc_esp_at_signal = regs->tf_esp;
	frame.sf_sc.sc_ss     = regs->tf_ss;
	frame.sf_sc.sc_err    = regs->tf_err;
	frame.sf_sc.sc_trapno = bsd_to_linux_trapcode(code);

	bzero(&frame.sf_fpstate, sizeof(struct l_fpstate));

	for (i = 0; i < (LINUX_NSIG_WORDS-1); i++)
		frame.sf_extramask[i] = lmask.__bits[i+1];

	if (copyout(&frame, fp, sizeof(frame)) != 0) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		sigexit(lp, SIGILL);
		/* NOTREACHED */
	}

	/*
	 * Build context to run handler in.
	 */
	regs->tf_esp = (int)fp;
	regs->tf_eip = PS_STRINGS - *(p->p_sysent->sv_szsigcode);

	/*
	 * i386 abi specifies that the direction flag must be cleared
	 * on function entry
	 */
	regs->tf_eflags &= ~(PSL_T | PSL_VM | PSL_D);

	regs->tf_cs = _ucodesel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	/* Allow %fs and %gs to be inherited by the signal handler */
	/*
	regs->tf_fs = _udatasel;
	regs->tf_gs = _udatasel;
	*/
	regs->tf_ss = _udatasel;
	clear_quickret();
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psl to gain improper privileges or to cause
 * a machine fault.
 *
 * MPSAFE
 */
int
sys_linux_sigreturn(struct linux_sigreturn_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	struct l_sigframe frame;
	struct trapframe *regs;
	l_sigset_t lmask;
	int eflags, i;

	regs = lp->lwp_md.md_regs;

#ifdef DEBUG
	if (ldebug(sigreturn))
		kprintf(ARGS(sigreturn, "%p"), (void *)args->sfp);
#endif
	/*
	 * The trampoline code hands us the sigframe.
	 * It is unsafe to keep track of it ourselves, in the event that a
	 * program jumps out of a signal handler.
	 */
	if (copyin((caddr_t)args->sfp, &frame, sizeof(frame)) != 0)
		return (EFAULT);

	/*
	 * Check for security violations.
	 */
#define	EFLAGS_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)
	eflags = frame.sf_sc.sc_eflags;
	/*
	 * XXX do allow users to change the privileged flag PSL_RF.  The
	 * cpu sets PSL_RF in tf_eflags for faults.  Debuggers should
	 * sometimes set it there too.  tf_eflags is kept in the signal
	 * context during signal handling and there is no other place
	 * to remember it, so the PSL_RF bit may be corrupted by the
	 * signal handler without us knowing.  Corruption of the PSL_RF
	 * bit at worst causes one more or one less debugger trap, so
	 * allowing it is fairly harmless.
	 */
	if (!EFLAGS_SECURE(eflags & ~PSL_RF, regs->tf_eflags & ~PSL_RF)) {
    		return(EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
#define	CS_SECURE(cs)	(ISPL(cs) == SEL_UPL)
	if (!CS_SECURE(frame.sf_sc.sc_cs)) {
		trapsignal(lp, SIGBUS, T_PROTFLT);
		return(EINVAL);
	}

	lp->lwp_sigstk.ss_flags &= ~SS_ONSTACK;
	lmask.__bits[0] = frame.sf_sc.sc_mask;
	for (i = 0; i < (LINUX_NSIG_WORDS-1); i++)
		lmask.__bits[i+1] = frame.sf_extramask[i];
	linux_to_bsd_sigset(&lmask, &lp->lwp_sigmask);
	SIG_CANTMASK(lp->lwp_sigmask);

	/*
	 * Restore signal context.
	 */
	/* %gs was restored by the trampoline. */
	regs->tf_fs     = frame.sf_sc.sc_fs;
	regs->tf_es     = frame.sf_sc.sc_es;
	regs->tf_ds     = frame.sf_sc.sc_ds;
	regs->tf_edi    = frame.sf_sc.sc_edi;
	regs->tf_esi    = frame.sf_sc.sc_esi;
	regs->tf_ebp    = frame.sf_sc.sc_ebp;
	regs->tf_ebx    = frame.sf_sc.sc_ebx;
	regs->tf_edx    = frame.sf_sc.sc_edx;
	regs->tf_ecx    = frame.sf_sc.sc_ecx;
	regs->tf_eax    = frame.sf_sc.sc_eax;
	regs->tf_eip    = frame.sf_sc.sc_eip;
	regs->tf_cs     = frame.sf_sc.sc_cs;
	regs->tf_eflags = eflags;
	regs->tf_esp    = frame.sf_sc.sc_esp_at_signal;
	regs->tf_ss     = frame.sf_sc.sc_ss;
	clear_quickret();

	return (EJUSTRETURN);
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by rt_sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psl to gain improper privileges or to cause
 * a machine fault.
 *
 * MPSAFE
 */
int
sys_linux_rt_sigreturn(struct linux_rt_sigreturn_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	struct l_ucontext uc;
	struct l_sigcontext *context;
	l_stack_t *lss;
	stack_t ss;
	struct trapframe *regs;
	int eflags;

	regs = lp->lwp_md.md_regs;

#ifdef DEBUG
	if (ldebug(rt_sigreturn))
		kprintf(ARGS(rt_sigreturn, "%p"), (void *)args->ucp);
#endif
	/*
	 * The trampoline code hands us the ucontext.
	 * It is unsafe to keep track of it ourselves, in the event that a
	 * program jumps out of a signal handler.
	 */
	if (copyin((caddr_t)args->ucp, &uc, sizeof(uc)) != 0)
		return (EFAULT);

	context = &uc.uc_mcontext;

	/*
	 * Check for security violations.
	 */
#define	EFLAGS_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)
	eflags = context->sc_eflags;
	/*
	 * XXX do allow users to change the privileged flag PSL_RF.  The
	 * cpu sets PSL_RF in tf_eflags for faults.  Debuggers should
	 * sometimes set it there too.  tf_eflags is kept in the signal
	 * context during signal handling and there is no other place
	 * to remember it, so the PSL_RF bit may be corrupted by the
	 * signal handler without us knowing.  Corruption of the PSL_RF
	 * bit at worst causes one more or one less debugger trap, so
	 * allowing it is fairly harmless.
	 */
	if (!EFLAGS_SECURE(eflags & ~PSL_RF, regs->tf_eflags & ~PSL_RF)) {
    		return(EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
#define	CS_SECURE(cs)	(ISPL(cs) == SEL_UPL)
	if (!CS_SECURE(context->sc_cs)) {
		trapsignal(lp, SIGBUS, T_PROTFLT);
		return(EINVAL);
	}

	lp->lwp_sigstk.ss_flags &= ~SS_ONSTACK;
	linux_to_bsd_sigset(&uc.uc_sigmask, &lp->lwp_sigmask);
	SIG_CANTMASK(lp->lwp_sigmask);

	/*
	 * Restore signal context
	 */
	/* %gs was restored by the trampoline. */
	regs->tf_fs     = context->sc_fs;
	regs->tf_es     = context->sc_es;
	regs->tf_ds     = context->sc_ds;
	regs->tf_edi    = context->sc_edi;
	regs->tf_esi    = context->sc_esi;
	regs->tf_ebp    = context->sc_ebp;
	regs->tf_ebx    = context->sc_ebx;
	regs->tf_edx    = context->sc_edx;
	regs->tf_ecx    = context->sc_ecx;
	regs->tf_eax    = context->sc_eax;
	regs->tf_eip    = context->sc_eip;
	regs->tf_cs     = context->sc_cs;
	regs->tf_eflags = eflags;
	regs->tf_esp    = context->sc_esp_at_signal;
	regs->tf_ss     = context->sc_ss;

	/*
	 * call sigaltstack & ignore results..
	 */
	lss = &uc.uc_stack;
	ss.ss_sp = lss->ss_sp;
	ss.ss_size = lss->ss_size;
	ss.ss_flags = linux_to_bsd_sigaltstack(lss->ss_flags);

#ifdef DEBUG
	if (ldebug(rt_sigreturn))
		kprintf(LMSG("rt_sigret flags: 0x%x, sp: %p, ss: 0x%x, mask: 0x%x"),
		    ss.ss_flags, ss.ss_sp, ss.ss_size, context->sc_mask);
#endif
	kern_sigaltstack(&ss, NULL);
	clear_quickret();

	return (EJUSTRETURN);
}

/*
 * Prep arguments.
 *
 * MUST BE MPSAFE
 */
static void
linux_prepsyscall(struct trapframe *tf, int *args, u_int *code, caddr_t *params)
{
	args[0] = tf->tf_ebx;
	args[1] = tf->tf_ecx;
	args[2] = tf->tf_edx;
	args[3] = tf->tf_esi;
	args[4] = tf->tf_edi;
	args[5] = tf->tf_ebp;
	*params = NULL;		/* no copyin */
}

/*
 * If a linux binary is exec'ing something, try this image activator 
 * first.  We override standard shell script execution in order to
 * be able to modify the interpreter path.  We only do this if a linux
 * binary is doing the exec, so we do not create an EXEC module for it.
 */
static int	exec_linux_imgact_try (struct image_params *iparams);

static int
exec_linux_imgact_try(struct image_params *imgp)
{
    const char *head = (const char *)imgp->image_header;
    int error = -1;

    /*
     * The interpreter for shell scripts run from a linux binary needs
     * to be located in /compat/linux if possible in order to recursively
     * maintain linux path emulation.
     */
    if (((const short *)head)[0] == SHELLMAGIC) {
	    /*
	     * Run our normal shell image activator.  If it succeeds attempt
	     * to use the alternate path for the interpreter.  If an alternate
	     * path is found, use our stringspace to store it.
	     */
	    if ((error = exec_shell_imgact(imgp)) == 0) {
		    linux_translate_path(imgp->interpreter_name,
			MAXSHELLCMDLEN);
	    }
    }
    return(error);
}

struct sysentvec linux_sysvec = {
	.sv_size	= LINUX_SYS_MAXSYSCALL,
	.sv_table	= linux_sysent,
	.sv_mask	= 0xffffffff,
	.sv_sigsize	= LINUX_SIGTBLSZ,
	.sv_sigtbl	= bsd_to_linux_signal,
	.sv_errsize	= ELAST + 1,
	.sv_errtbl	= bsd_to_linux_errno,
	.sv_transtrap	= translate_traps,
	.sv_fixup	= linux_fixup,
	.sv_sendsig	= linux_sendsig,
	.sv_sigcode	= linux_sigcode,
	.sv_szsigcode	= &linux_szsigcode,
	.sv_prepsyscall	= linux_prepsyscall,
	.sv_name	= "Linux a.out",
	.sv_coredump	= NULL,
	.sv_imgact_try	= exec_linux_imgact_try,
	.sv_minsigstksz	= LINUX_MINSIGSTKSZ
};

struct sysentvec elf_linux_sysvec = {
	.sv_size	= LINUX_SYS_MAXSYSCALL,
	.sv_table	= linux_sysent,
	.sv_mask	= 0xffffffff,
	.sv_sigsize	= LINUX_SIGTBLSZ,
	.sv_sigtbl	= bsd_to_linux_signal,
	.sv_errsize	= ELAST + 1,
	.sv_errtbl	= bsd_to_linux_errno,
	.sv_transtrap	= translate_traps,
	.sv_fixup	= elf_linux_fixup,
	.sv_sendsig	= linux_sendsig,
	.sv_sigcode	= linux_sigcode,
	.sv_szsigcode	= &linux_szsigcode,
	.sv_prepsyscall	= linux_prepsyscall,
	.sv_name	= "Linux ELF32",
	.sv_coredump	= elf32_coredump,
	.sv_imgact_try	= exec_linux_imgact_try,
	.sv_minsigstksz	= LINUX_MINSIGSTKSZ
};

static const char GNU_ABI_VENDOR[] = "GNU";
static const char SUSE_ABI_VENDOR[] = "SuSE";
static int        GNULINUX_ABI_DESC = 0;

static boolean_t
linux_trans_osrel(const Elf_Note *note, int32_t *osrel)
{
	const Elf32_Word *desc;
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, sizeof(Elf32_Addr));

	desc = (const Elf32_Word *)p;
	if (desc[0] != GNULINUX_ABI_DESC)
		return (FALSE);
	/*
	 * For Linux we encode osrel as follows:
	 * VVVMMMIII (version, major, minor)
	 */
	*osrel = desc[1] * 1000000 +
		 desc[2] * 1000 +
		 desc[3];

	return (TRUE);
}

static Elf_Brandnote linux32_generic_brandnote = {
	.hdr.n_namesz	= sizeof(GNU_ABI_VENDOR),
	.hdr.n_descsz	= 16,
	.hdr.n_type	= 1,
	.vendor		= GNU_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= linux_trans_osrel,
};

static Elf_Brandnote linux32_suse_brandnote = {
	.hdr.n_namesz	= sizeof(SUSE_ABI_VENDOR),
	.hdr.n_descsz	= 16,
	.hdr.n_type	= 1,
	.vendor		= SUSE_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= linux_trans_osrel,
};

static Elf32_Brandinfo linux32_brand = {
        .brand		= ELFOSABI_LINUX,
        .machine	= EM_386,
        .compat_3_brand	= "Linux",
        .emul_path	= "/compat/linux",
        .interp_path	= "/lib/ld-linux.so.1",
        .sysvec		= &elf_linux_sysvec,
        .interp_newpath	= NULL,
        .flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
        .brand_note	= &linux32_generic_brandnote,
};

static Elf32_Brandinfo linux32_glibc2_brand = {
        .brand		= ELFOSABI_LINUX,
        .machine	= EM_386,
        .compat_3_brand	= "Linux",
        .emul_path	= "/compat/linux",
        .interp_path	= "/lib/ld-linux.so.2",
        .sysvec		= &elf_linux_sysvec,
        .interp_newpath	= NULL,
        .flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
        .brand_note	= &linux32_generic_brandnote,
};

static Elf32_Brandinfo linux32_suse_brand = {
        .brand		= ELFOSABI_LINUX,
        .machine	= EM_386,
        .compat_3_brand	= "Linux",
        .emul_path	= "/compat/linux",
        .interp_path	= "/lib/ld-linux.so.2",
        .sysvec		= &elf_linux_sysvec,
        .interp_newpath	= NULL,
        .flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
        .brand_note	= &linux32_suse_brandnote,
};

Elf32_Brandinfo *linux_brandlist[] = {
        &linux32_brand,
        &linux32_glibc2_brand,
        &linux32_suse_brand,
        NULL
};

static int
linux_elf_modevent(module_t mod, int type, void *data)
{
	Elf32_Brandinfo **brandinfo;
	int error;

	error = 0;

	switch(type) {
	case MOD_LOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		     ++brandinfo)
			if (elf32_insert_brand_entry(*brandinfo) < 0)
				error = EINVAL;
		if (error == 0) {
			if (bootverbose)
				kprintf("Linux ELF exec handler installed\n");
		} else {
			kprintf("cannot insert Linux ELF brand handler\n");
		}
		EMUL_LOCKINIT();
		lockinit(&futex_mtx, "linftxs", 0, LK_CANRECURSE);
		linux_exec_tag = EVENTHANDLER_REGISTER(process_exec, linux_proc_transition,
		    NULL, 1000);
		linux_exit_tag = EVENTHANDLER_REGISTER(process_exit, emuldata_exit,
		    NULL, 1000);
		break;
	case MOD_UNLOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		     ++brandinfo)
			if (elf32_brand_inuse(*brandinfo))
				error = EBUSY;
		if (error == 0) {
			for (brandinfo = &linux_brandlist[0];
			     *brandinfo != NULL; ++brandinfo)
				if (elf32_remove_brand_entry(*brandinfo) < 0)
					error = EINVAL;
		}
		if (error == 0) {
			if (bootverbose)
				kprintf("Linux ELF exec handler removed\n");
		} else {
			kprintf("Could not deinstall ELF interpreter entry\n");
		}
		EVENTHANDLER_DEREGISTER(process_exec, linux_exec_tag);
		EVENTHANDLER_DEREGISTER(process_exit, linux_exit_tag);
		lockuninit(&futex_mtx);
		EMUL_LOCKUNINIT();
		break;
	default:
		break;
	}
	return error;
}

static moduledata_t linux_elf_mod = {
	"linuxelf",
	linux_elf_modevent,
	0
};

DECLARE_MODULE(linuxelf, linux_elf_mod, SI_SUB_EXEC, SI_ORDER_ANY);
