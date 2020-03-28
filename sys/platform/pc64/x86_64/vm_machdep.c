/*-
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * Copyright (c) 2008-2018 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
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
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 * $FreeBSD: src/sys/i386/i386/vm_machdep.c,v 1.132.2.9 2003/01/25 19:02:23 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/interrupt.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/lwp.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/pcb.h>
#include <machine/pcb_ext.h>
#include <machine/segments.h>
#include <machine/globaldata.h>	/* npxthread */
#include <machine/specialreg.h>
#include <machine/vmm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>

#include <bus/isa/isa.h>

static void	cpu_reset_real (void);

static int spectre_mitigation = -1;
static int spectre_support = 0;
static int spectre_mode = 0;
SYSCTL_INT(_machdep, OID_AUTO, spectre_mode, CTLFLAG_RD,
	&spectre_mode, 0, "current Spectre enablements");

static int mds_mitigation = -1;
static int mds_support = 0;
static int mds_mode = 0;
SYSCTL_INT(_machdep, OID_AUTO, mds_mode, CTLFLAG_RD,
	&mds_mode, 0, "current MDS enablements");

/*
 * Finish a fork operation, with lwp lp2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct lwp *lp1, struct lwp *lp2, int flags)
{
	struct pcb *pcb2;
	struct pmap *pmap2;

	if ((flags & RFPROC) == 0) {
		if ((flags & RFMEM) == 0) {
			/*
			 * Unshare user LDT.  > 1 test is MPSAFE.  While
			 * it can potentially race a 2->1 transition, the
			 * worst that happens is that we do an unnecessary
			 * ldt replacement.
			 */
			struct pcb *pcb1 = lp1->lwp_thread->td_pcb;
			struct pcb_ldt *pcb_ldt = pcb1->pcb_ldt;

			if (pcb_ldt && pcb_ldt->ldt_refcnt > 1) {
				pcb_ldt = user_ldt_alloc(pcb1,pcb_ldt->ldt_len);
				user_ldt_free(pcb1);
				pcb1->pcb_ldt = pcb_ldt;
				set_user_ldt(pcb1);
			}
		}
		return;
	}

	/* Ensure that lp1's pcb is up to date. */
	if (mdcpu->gd_npxthread == lp1->lwp_thread)
		npxsave(lp1->lwp_thread->td_savefpu);

	/*
	 * Copy lp1's PCB.  This really only applies to the
	 * debug registers and FP state, but its faster to just copy the
	 * whole thing.  Because we only save the PCB at switchout time,
	 * the register state may not be current.
	 */
	pcb2 = lp2->lwp_thread->td_pcb;
	*pcb2 = *lp1->lwp_thread->td_pcb;

	/*
	 * Create a new fresh stack for the new process.
	 * Copy the trap frame for the return to user mode as if from a
	 * syscall.  This copies the user mode register values.
	 *
	 * pcb_rsp must allocate an additional call-return pointer below
	 * the trap frame which will be restored by cpu_heavy_restore from
	 * PCB_RIP, and the thread's td_sp pointer must allocate an
	 * additonal two quadwords below the pcb_rsp call-return pointer to
	 * hold the LWKT restore function pointer and rflags.
	 *
	 * The LWKT restore function pointer must be set to cpu_heavy_restore,
	 * which is our standard heavy-weight process switch-in function.
	 * YYY eventually we should shortcut fork_return and fork_trampoline
	 * to use the LWKT restore function directly so we can get rid of
	 * all the extra crap we are setting up.
	 */
	lp2->lwp_md.md_regs = (struct trapframe *)pcb2 - 1;
	bcopy(lp1->lwp_md.md_regs, lp2->lwp_md.md_regs, sizeof(*lp2->lwp_md.md_regs));

	/*
	 * Set registers for trampoline to user mode.  Leave space for the
	 * return address on stack.  These are the kernel mode register values.
	 *
	 * Set the new pmap CR3.  If the new process uses isolated VM spaces,
	 * also set the isolated CR3.
	 */
	pmap2 = vmspace_pmap(lp2->lwp_proc->p_vmspace);
	pcb2->pcb_cr3 = vtophys(pmap2->pm_pml4);
	if ((pcb2->pcb_flags & PCB_ISOMMU) && pmap2->pm_pmlpv_iso) {
		pcb2->pcb_cr3_iso = vtophys(pmap2->pm_pml4_iso);
	} else {
		pcb2->pcb_flags &= ~PCB_ISOMMU;
		pcb2->pcb_cr3_iso = 0;
	}

#if 0
	/*
	 * Per-process spectre mitigation (future)
	 */
	pcb2->pcb_flags &= ~(PCB_IBRS1 | PCB_IBRS2);
	switch (spectre_mitigation) {
	case 1:
		pcb2->pcb_flags |= PCB_IBRS1;
		break;
	case 2:
		pcb2->pcb_flags |= PCB_IBRS2;
		break;
	default:
		break;
	}
#endif

	pcb2->pcb_rbx = (unsigned long)fork_return;	/* fork_trampoline argument */
	pcb2->pcb_rbp = 0;
	pcb2->pcb_rsp = (unsigned long)lp2->lwp_md.md_regs - sizeof(void *);
	pcb2->pcb_r12 = (unsigned long)lp2;		/* fork_trampoline argument */
	pcb2->pcb_r13 = 0;
	pcb2->pcb_r14 = 0;
	pcb2->pcb_r15 = 0;
	pcb2->pcb_rip = (unsigned long)fork_trampoline;
	lp2->lwp_thread->td_sp = (char *)(pcb2->pcb_rsp - sizeof(void *));
	*(u_int64_t *)lp2->lwp_thread->td_sp = PSL_USER;
	lp2->lwp_thread->td_sp -= sizeof(void *);
	*(void **)lp2->lwp_thread->td_sp = (void *)cpu_heavy_restore;

	/*
	 * pcb2->pcb_ldt:	duplicated below, if necessary.
	 * pcb2->pcb_savefpu:	cloned above.
	 * pcb2->pcb_flags:	cloned above
	 * pcb2->pcb_onfault:	cloned above (always NULL here).
	 * pcb2->pcb_onfault_sp:cloned above (dont care)
	 */

	/*
	 * XXX don't copy the i/o pages.  this should probably be fixed.
	 */
	pcb2->pcb_ext = NULL;

        /* Copy the LDT, if necessary. */
        if (pcb2->pcb_ldt != NULL) {
		if (flags & RFMEM) {
			atomic_add_int(&pcb2->pcb_ldt->ldt_refcnt, 1);
		} else {
			pcb2->pcb_ldt = user_ldt_alloc(pcb2,
						       pcb2->pcb_ldt->ldt_len);
		}
        }
	bcopy(&lp1->lwp_thread->td_tls, &lp2->lwp_thread->td_tls,
	      sizeof(lp2->lwp_thread->td_tls));
	/*
	 * Now, cpu_switch() can schedule the new lwp.
	 * pcb_rsp is loaded pointing to the cpu_switch() stack frame
	 * containing the return address when exiting cpu_switch.
	 * This will normally be to fork_trampoline(), which will have
	 * %rbx loaded with the new lwp's pointer.  fork_trampoline()
	 * will set up a stack to call fork_return(lp, frame); to complete
	 * the return to user-mode.
	 */
}

/*
 * Prepare new lwp to return to the address specified in params.
 */
int
cpu_prepare_lwp(struct lwp *lp, struct lwp_params *params)
{
	struct trapframe *regs = lp->lwp_md.md_regs;
	void *bad_return = NULL;
	int error;

	regs->tf_rip = (long)params->lwp_func;
	regs->tf_rsp = (long)params->lwp_stack;
	/* Set up argument for function call */
	regs->tf_rdi = (long)params->lwp_arg;

	/*
	 * Set up fake return address.  As the lwp function may never return,
	 * we simply copy out a NULL pointer and force the lwp to receive
	 * a SIGSEGV if it returns anyways.
	 */
	regs->tf_rsp -= sizeof(void *);
	error = copyout(&bad_return, (void *)regs->tf_rsp, sizeof(bad_return));
	if (error)
		return (error);

	if (lp->lwp_proc->p_vmm) {
		lp->lwp_thread->td_pcb->pcb_cr3 = KPML4phys;
		cpu_set_fork_handler(lp,
		    (void (*)(void *, struct trapframe *))vmm_lwp_return, lp);
	} else {
		cpu_set_fork_handler(lp,
		    (void (*)(void *, struct trapframe *))generic_lwp_return, lp);
	}
	return (0);
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_set_fork_handler(struct lwp *lp, void (*func)(void *, struct trapframe *),
		     void *arg)
{
	/*
	 * Note that the trap frame follows the args, so the function
	 * is really called like this:  func(arg, frame);
	 */
	lp->lwp_thread->td_pcb->pcb_rbx = (long)func;	/* function */
	lp->lwp_thread->td_pcb->pcb_r12 = (long)arg;	/* first arg */
}

void
cpu_set_thread_handler(thread_t td, void (*rfunc)(void), void *func, void *arg)
{
	td->td_pcb->pcb_rbx = (long)func;
	td->td_pcb->pcb_r12 = (long)arg;
	td->td_switch = cpu_lwkt_switch;
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = rfunc;	/* exit function on return */
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = cpu_kthread_restore;
}

void
cpu_lwp_exit(void)
{
	struct thread *td = curthread;
	struct pcb *pcb;

	pcb = td->td_pcb;

	/* Some x86 functionality was dropped */
	KKASSERT(pcb->pcb_ext == NULL);

	/*
	 * disable all hardware breakpoints
	 */
        if (pcb->pcb_flags & PCB_DBREGS) {
                reset_dbregs();
                pcb->pcb_flags &= ~PCB_DBREGS;
        }
	td->td_gd->gd_cnt.v_swtch++;

	crit_enter_quick(td);
	if (td->td_flags & TDF_TSLEEPQ)
		tsleep_remove(td);
	lwkt_deschedule_self(td);
	lwkt_remove_tdallq(td);
	cpu_thread_exit();
}

/*
 * Terminate the current thread.  The caller must have already acquired
 * the thread's rwlock and placed it on a reap list or otherwise notified
 * a reaper of its existance.  We set a special assembly switch function which
 * releases td_rwlock after it has cleaned up the MMU state and switched
 * out the stack.
 *
 * Must be caller from a critical section and with the thread descheduled.
 */
void
cpu_thread_exit(void)
{
	npxexit();
	curthread->td_switch = cpu_exit_switch;
	curthread->td_flags |= TDF_EXITING;
	lwkt_switch();
	panic("cpu_thread_exit: lwkt_switch() unexpectedly returned");
}

void
cpu_reset(void)
{
	cpu_reset_real();
}

static void
cpu_reset_real(void)
{
	/*
	 * Attempt to do a CPU reset via the keyboard controller,
	 * do not turn off the GateA20, as any machine that fails
	 * to do the reset here would then end up in no man's land.
	 */

#if !defined(BROKEN_KEYBOARD_RESET)
	outb(IO_KBD + 4, 0xFE);
	DELAY(500000);	/* wait 0.5 sec to see if that did it */
	kprintf("Keyboard reset did not work, attempting CPU shutdown\n");
	DELAY(1000000);	/* wait 1 sec for kprintf to complete */
#endif
#if 0 /* JG */
	/* force a shutdown by unmapping entire address space ! */
	bzero((caddr_t) PTD, PAGE_SIZE);
#endif

	/* "good night, sweet prince .... <THUNK!>" */
	cpu_invltlb();
	/* NOTREACHED */
	while(1);
}

static void
swi_vm(void *arg, void *frame)
{
	if (busdma_swi_pending != 0)
		busdma_swi();
}

static void
swi_vm_setup(void *arg)
{
	register_swi_mp(SWI_VM, swi_vm, NULL, "swi_vm", NULL, 0);
}

SYSINIT(swi_vm_setup, SI_BOOT2_MACHDEP, SI_ORDER_ANY, swi_vm_setup, NULL);

/*
 * NOTE: This routine is also called after a successful microcode
 *	 reload on cpu 0.
 */
void mitigation_vm_setup(void *arg);

/*
 * Check for IBPB and IBRS support
 *
 * This bits also specify desired modes in the spectre_mitigation sysctl.
 */
#define IBRS_SUPPORTED		0x0001
#define STIBP_SUPPORTED		0x0002
#define IBPB_SUPPORTED		0x0004
#define IBRS_AUTO_SUPPORTED	0x0008
#define STIBP_AUTO_SUPPORTED	0x0010
#define IBRS_PREFERRED_REQUEST	0x0020

static
int
spectre_check_support(void)
{
	uint32_t p[4];
	int rv = 0;

	/*
	 * Spectre mitigation hw bits
	 *
	 * IBRS		Indirect Branch Restricted Speculation   (isolation)
	 * STIBP	Single Thread Indirect Branch Prediction (isolation)
	 * IBPB		Branch Prediction Barrier		 (barrier)
	 *
	 * IBRS and STIBP must be toggled (enabled on entry to kernel,
	 * disabled on exit, as well as disabled during any MWAIT/HLT).
	 * When *_AUTO bits are available, IBRS and STIBP may be left
	 * turned on and do not have to be toggled on kernel entry/exit.
	 *
	 * All this shit has enormous overhead.  IBPB in particular, and
	 * non-auto modes are disabled by default.
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		p[0] = 0;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0;
		cpuid_count(7, 0, p);
		if (p[3] & CPUID_7_0_I3_SPEC_CTRL)
			rv |= IBRS_SUPPORTED | IBPB_SUPPORTED;
		if (p[3] & CPUID_7_0_I3_STIBP)
			rv |= STIBP_SUPPORTED;

		/*
		 * 0x80000008 p[1] bit 12 indicates IBPB support
		 *
		 * This bit might be set even though SPEC_CTRL is not set.
		 */
		p[0] = 0;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0;
		do_cpuid(0x80000008U, p);
		if (p[1] & CPUID_INTEL_80000008_I1_IBPB_SUPPORT)
			rv |= IBPB_SUPPORTED;
	} else if (cpu_vendor_id == CPU_VENDOR_AMD) {
		/*
		 * 0x80000008 p[1] bit 12 indicates IBPB support
		 *	      p[1] bit 14 indicates IBRS support
		 *	      p[1] bit 15 indicates STIBP support
		 *
		 *	      p[1] bit 16 indicates IBRS auto support
		 *	      p[1] bit 17 indicates STIBP auto support
		 *	      p[1] bit 18 indicates processor prefers using
		 *		IBRS instead of retpoline.
		 */
		p[0] = 0;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0;
		do_cpuid(0x80000008U, p);
		if (p[1] & CPUID_AMD_80000008_I1_IBPB_SUPPORT)
			rv |= IBPB_SUPPORTED;
		if (p[1] & CPUID_AMD_80000008_I1_IBRS_SUPPORT)
			rv |= IBRS_SUPPORTED;
		if (p[1] & CPUID_AMD_80000008_I1_STIBP_SUPPORT)
			rv |= STIBP_SUPPORTED;

		if (p[1] & CPUID_AMD_80000008_I1_IBRS_AUTO)
			rv |= IBRS_AUTO_SUPPORTED;
		if (p[1] & CPUID_AMD_80000008_I1_STIBP_AUTO)
			rv |= STIBP_AUTO_SUPPORTED;
		if (p[1] & CPUID_AMD_80000008_I1_IBRS_REQUESTED)
			rv |= IBRS_PREFERRED_REQUEST;
	}

	return rv;
}

/*
 * Iterate CPUs and adjust MSR for global operations, since
 * the KMMU* code won't do it if spectre_mitigation is 0 or 2.
 */
#define CHECK(flag)	(spectre_mitigation & spectre_support & (flag))

static
void
spectre_sysctl_changed(void)
{
	globaldata_t save_gd;
	struct trampframe *tr;
	int spec_ctrl;
	int spec_mask;
	int mode;
	int n;


	spec_mask = SPEC_CTRL_IBRS | SPEC_CTRL_STIBP |
		    SPEC_CTRL_DUMMY_ENABLE | SPEC_CTRL_DUMMY_IBPB;

	/*
	 * Fixup state
	 */
	mode = 0;
	save_gd = mycpu;
	for (n = 0; n < ncpus; ++n) {
		lwkt_setcpu_self(globaldata_find(n));
		cpu_ccfence();
		tr = &pscpu->trampoline;

		/*
		 * Make sure we are cleaned out.
		 *
		 * XXX cleanup, reusing globals inside the loop (they get
		 * set to the same thing each loop)
		 *
		 * [0] kernel entry (idle exit)
		 * [1] kernel exit  (idle entry)
		 */
		tr->tr_pcb_spec_ctrl[0] &= ~spec_mask;
		tr->tr_pcb_spec_ctrl[1] &= ~spec_mask;

		/*
		 * Don't try to parse if not available
		 */
		if (spectre_mitigation < 0)
			continue;

		/*
		 * IBRS mode.  Auto overrides toggling.
		 *
		 * Only set the ENABLE flag if we have to toggle something
		 * on entry and exit.
		 */
		spec_ctrl = 0;
		if (CHECK(IBRS_AUTO_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_IBRS;
			mode |= IBRS_AUTO_SUPPORTED;
		} else if (CHECK(IBRS_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_IBRS | SPEC_CTRL_DUMMY_ENABLE;
			mode |= IBRS_SUPPORTED;
		}
		if (CHECK(STIBP_AUTO_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_STIBP;
			mode |= STIBP_AUTO_SUPPORTED;
		} else if (CHECK(STIBP_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_STIBP | SPEC_CTRL_DUMMY_ENABLE;
			mode |= STIBP_SUPPORTED;
		}

		/*
		 * IBPB requested and supported.
		 */
		if (CHECK(IBPB_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_DUMMY_IBPB;
			mode |= IBPB_SUPPORTED;
		}

		/*
		 * Update the MSR if the cpu supports the modes to ensure
		 * proper disablement if the user disabled the mode.
		 */
		if (spectre_support & (IBRS_SUPPORTED | IBRS_AUTO_SUPPORTED |
				    STIBP_SUPPORTED | STIBP_AUTO_SUPPORTED)) {
			wrmsr(MSR_SPEC_CTRL,
			      spec_ctrl & (SPEC_CTRL_IBRS|SPEC_CTRL_STIBP));
		}

		/*
		 * Update spec_ctrl fields in the trampoline.
		 *
		 * [0] on-kernel-entry (on-idle-exit)
		 * [1] on-kernel-exit  (on-idle-entry)
		 *
		 * When auto mode is supported we leave the bit set, otherwise
		 * we clear the bits.
		 */
		tr->tr_pcb_spec_ctrl[0] |= spec_ctrl;
		if (CHECK(IBRS_AUTO_SUPPORTED) == 0)
			spec_ctrl &= ~SPEC_CTRL_IBRS;
		if (CHECK(STIBP_AUTO_SUPPORTED) == 0)
			spec_ctrl &= ~SPEC_CTRL_STIBP;
		tr->tr_pcb_spec_ctrl[1] |= spec_ctrl;

		/*
		 * Make sure we set this on the first loop.  It will be
		 * the same value on remaining loops.
		 */
		spectre_mode = mode;
	}
	lwkt_setcpu_self(save_gd);
	cpu_ccfence();

	/*
	 * Console message on mitigation mode change
	 */
	kprintf("Spectre: support=(");
	if (spectre_support == 0) {
		kprintf(" none");
	} else {
		if (spectre_support & IBRS_SUPPORTED)
			kprintf(" IBRS");
		if (spectre_support & STIBP_SUPPORTED)
			kprintf(" STIBP");
		if (spectre_support & IBPB_SUPPORTED)
			kprintf(" IBPB");
		if (spectre_support & IBRS_AUTO_SUPPORTED)
			kprintf(" IBRS_AUTO");
		if (spectre_support & STIBP_AUTO_SUPPORTED)
			kprintf(" STIBP_AUTO");
		if (spectre_support & IBRS_PREFERRED_REQUEST)
			kprintf(" IBRS_REQUESTED");
	}
	kprintf(" ) req=%04x operating=(", (uint16_t)spectre_mitigation);
	if (spectre_mode == 0) {
		kprintf(" none");
	} else {
		if (spectre_mode & IBRS_SUPPORTED)
			kprintf(" IBRS");
		if (spectre_mode & STIBP_SUPPORTED)
			kprintf(" STIBP");
		if (spectre_mode & IBPB_SUPPORTED)
			kprintf(" IBPB");
		if (spectre_mode & IBRS_AUTO_SUPPORTED)
			kprintf(" IBRS_AUTO");
		if (spectre_mode & STIBP_AUTO_SUPPORTED)
			kprintf(" STIBP_AUTO");
		if (spectre_mode & IBRS_PREFERRED_REQUEST)
			kprintf(" IBRS_REQUESTED");
	}
	kprintf(" )\n");
}

#undef CHECK

/*
 * User changes sysctl value
 */
static int
sysctl_spectre_mitigation(SYSCTL_HANDLER_ARGS)
{
	char buf[128];
	char *ptr;
	char *iter;
	size_t len;
	int spectre;
	int error = 0;
	int loop = 0;

	/*
	 * Return current operating mode or support.
	 */
	if (oidp->oid_kind & CTLFLAG_WR)
		spectre = spectre_mode;
	else
		spectre = spectre_support;

	spectre &= (IBRS_SUPPORTED | IBRS_AUTO_SUPPORTED |
		    STIBP_SUPPORTED | STIBP_AUTO_SUPPORTED |
		    IBPB_SUPPORTED);
	while (spectre) {
		if (error)
			break;
		if (loop++) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				break;
		}
		if (spectre & IBRS_SUPPORTED) {
			spectre &= ~IBRS_SUPPORTED;
			error = SYSCTL_OUT(req, "IBRS", 4);
		} else
		if (spectre & IBRS_AUTO_SUPPORTED) {
			spectre &= ~IBRS_AUTO_SUPPORTED;
			error = SYSCTL_OUT(req, "IBRS_AUTO", 9);
		} else
		if (spectre & STIBP_SUPPORTED) {
			spectre &= ~STIBP_SUPPORTED;
			error = SYSCTL_OUT(req, "STIBP", 5);
		} else
		if (spectre & STIBP_AUTO_SUPPORTED) {
			spectre &= ~STIBP_AUTO_SUPPORTED;
			error = SYSCTL_OUT(req, "STIBP_AUTO", 10);
		} else
		if (spectre & IBPB_SUPPORTED) {
			spectre &= ~IBPB_SUPPORTED;
			error = SYSCTL_OUT(req, "IBPB", 4);
		}
	}
	if (loop == 0) {
		error = SYSCTL_OUT(req, "NONE", 4);
	}

	if (error || req->newptr == NULL)
		return error;
	if ((oidp->oid_kind & CTLFLAG_WR) == 0)
		return error;

	/*
	 * Change current operating mode
	 */
	len = req->newlen - req->newidx;
	if (len >= sizeof(buf)) {
		error = EINVAL;
		len = 0;
	} else {
		error = SYSCTL_IN(req, buf, len);
	}
	buf[len] = 0;
	iter = &buf[0];
	spectre = 0;

	while (error == 0 && iter) {
		ptr = strsep(&iter, " ,\t\r\n");
		if (*ptr == 0)
			continue;
		if (strcasecmp(ptr, "NONE") == 0)
			spectre |= 0;
		else if (strcasecmp(ptr, "IBRS") == 0)
			spectre |= IBRS_SUPPORTED;
		else if (strcasecmp(ptr, "IBRS_AUTO") == 0)
			spectre |= IBRS_AUTO_SUPPORTED;
		else if (strcasecmp(ptr, "STIBP") == 0)
			spectre |= STIBP_SUPPORTED;
		else if (strcasecmp(ptr, "STIBP_AUTO") == 0)
			spectre |= STIBP_AUTO_SUPPORTED;
		else if (strcasecmp(ptr, "IBPB") == 0)
			spectre |= IBPB_SUPPORTED;
		else
			error = ENOENT;
	}
	if (error == 0) {
		spectre_mitigation = spectre;
		spectre_sysctl_changed();
	}
	return error;
}

SYSCTL_PROC(_machdep, OID_AUTO, spectre_mitigation,
	CTLTYPE_STRING | CTLFLAG_RW,
	0, 0, sysctl_spectre_mitigation, "A", "Spectre exploit mitigation");
SYSCTL_PROC(_machdep, OID_AUTO, spectre_support,
	CTLTYPE_STRING | CTLFLAG_RD,
	0, 0, sysctl_spectre_mitigation, "A", "Spectre supported features");

/*
 * NOTE: Called at SI_BOOT2_MACHDEP and also when the microcode is
 *	 updated.  Microcode updates must be applied to all cpus
 *	 for support to be recognized.
 */
static void
spectre_vm_setup(void *arg)
{
	int inconsistent = 0;
	int supmask;

	/*
	 * Fetch tunable in auto mode
	 */
	if (spectre_mitigation < 0) {
		TUNABLE_INT_FETCH("machdep.spectre_mitigation",
				  &spectre_mitigation);
	}

	if ((supmask = spectre_check_support()) != 0) {
		/*
		 * Must be supported on all cpus before we
		 * can enable it.  Returns silently if it
		 * isn't.
		 *
		 * NOTE! arg != NULL indicates we were called
		 *	 from cpuctl after a successful microcode
		 *	 update.
		 */
		if (arg != NULL) {
			globaldata_t save_gd;
			int n;

			save_gd = mycpu;
			for (n = 0; n < ncpus; ++n) {
				lwkt_setcpu_self(globaldata_find(n));
				cpu_ccfence();
				if (spectre_check_support() !=
				    supmask) {
					inconsistent = 1;
					break;
				}
			}
			lwkt_setcpu_self(save_gd);
			cpu_ccfence();
		}
	}

	/*
	 * Be silent while microcode is being loaded on various CPUs,
	 * until all done.
	 */
	if (inconsistent) {
		spectre_mitigation = -1;
		spectre_support = 0;
		return;
	}

	/*
	 * IBRS support
	 */
	spectre_support = supmask;

	/*
	 * Enable spectre_mitigation, set defaults if -1, adjust
	 * tuned value according to support if not.
	 *
	 * NOTE!  We do not enable IBPB for user->kernel transitions
	 *	  by default, so this code is commented out for now.
	 */
	if (spectre_support) {
		if (spectre_mitigation < 0) {
			spectre_mitigation = 0;

			/*
			 * IBRS toggling not currently recommended as a
			 * default.
			 */
			if (spectre_support & IBRS_AUTO_SUPPORTED)
				spectre_mitigation |= IBRS_AUTO_SUPPORTED;
			else if (spectre_support & IBRS_SUPPORTED)
				spectre_mitigation |= 0;

			/*
			 * STIBP toggling not currently recommended as a
			 * default.
			 */
			if (spectre_support & STIBP_AUTO_SUPPORTED)
				spectre_mitigation |= STIBP_AUTO_SUPPORTED;
			else if (spectre_support & STIBP_SUPPORTED)
				spectre_mitigation |= 0;

			/*
			 * IBPB adds enormous (~2uS) overhead to system
			 * calls etc, we do not enable it by default.
			 */
			if (spectre_support & IBPB_SUPPORTED)
				spectre_mitigation |= 0;
		}
	} else {
		spectre_mitigation = -1;
	}

	/*
	 * Disallow sysctl changes when there is no support (otherwise
	 * the wrmsr will cause a protection fault).
	 */
	if (spectre_mitigation < 0)
		sysctl___machdep_spectre_mitigation.oid_kind &= ~CTLFLAG_WR;
	else
		sysctl___machdep_spectre_mitigation.oid_kind |= CTLFLAG_WR;

	spectre_sysctl_changed();
}

#define MDS_AVX512_4VNNIW_SUPPORTED	0x0001
#define MDS_AVX512_4FMAPS_SUPPORTED	0x0002
#define MDS_MD_CLEAR_SUPPORTED		0x0004
#define MDS_TSX_FORCE_ABORT_SUPPORTED	0x0008
#define MDS_NOT_REQUIRED		0x8000

static
int
mds_check_support(void)
{
	uint64_t msr;
	uint32_t p[4];
	int rv = 0;

	/*
	 * MDS mitigation hw bits
	 *
	 * MD_CLEAR	Use microcode-supported verf insn.  This is the
	 *		only mode we really support.
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		p[0] = 0;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0;
		cpuid_count(7, 0, p);
		if (p[3] & CPUID_SEF_ARCH_CAP) {
			msr = rdmsr(MSR_IA32_ARCH_CAPABILITIES);
			if (msr & IA32_ARCH_MDS_NO)
				rv = MDS_NOT_REQUIRED;
		}
		if (p[3] & CPUID_SEF_AVX512_4VNNIW)
			rv |= MDS_AVX512_4VNNIW_SUPPORTED;
		if (p[3] & CPUID_SEF_AVX512_4FMAPS)
			rv |= MDS_AVX512_4FMAPS_SUPPORTED;
		if (p[3] & CPUID_SEF_MD_CLEAR)
			rv |= MDS_MD_CLEAR_SUPPORTED;
		if (p[3] & CPUID_SEF_TSX_FORCE_ABORT)
			rv |= MDS_TSX_FORCE_ABORT_SUPPORTED;
	} else {
		rv = MDS_NOT_REQUIRED;
	}

	return rv;
}

/*
 * Iterate CPUs and adjust MSR for global operations, since
 * the KMMU* code won't do it if spectre_mitigation is 0 or 2.
 */
#define CHECK(flag)	(mds_mitigation & mds_support & (flag))

static
void
mds_sysctl_changed(void)
{
	globaldata_t save_gd;
	struct trampframe *tr;
	int spec_ctrl;
	int spec_mask;
	int mode;
	int n;

	spec_mask = SPEC_CTRL_MDS_ENABLE;

	/*
	 * Fixup state
	 */
	mode = 0;
	save_gd = mycpu;
	for (n = 0; n < ncpus; ++n) {
		lwkt_setcpu_self(globaldata_find(n));
		cpu_ccfence();
		tr = &pscpu->trampoline;

		/*
		 * Make sure we are cleaned out.
		 *
		 * XXX cleanup, reusing globals inside the loop (they get
		 * set to the same thing each loop)
		 *
		 * [0] kernel entry (idle exit)
		 * [1] kernel exit  (idle entry)
		 */
		tr->tr_pcb_spec_ctrl[0] &= ~spec_mask;
		tr->tr_pcb_spec_ctrl[1] &= ~spec_mask;

		/*
		 * Don't try to parse if not available
		 */
		if (mds_mitigation < 0)
			continue;

		spec_ctrl = 0;
		if (CHECK(MDS_MD_CLEAR_SUPPORTED)) {
			spec_ctrl |= SPEC_CTRL_MDS_ENABLE;
			mode |= MDS_MD_CLEAR_SUPPORTED;
		}

		/*
		 * Update spec_ctrl fields in the trampoline.
		 *
		 * [0] on-kernel-entry (on-idle-exit)
		 * [1] on-kernel-exit  (on-idle-entry)
		 *
		 * The MDS stuff is only needed on kernel-exit or idle-entry
		 */
		/* tr->tr_pcb_spec_ctrl[0] |= spec_ctrl; */
		tr->tr_pcb_spec_ctrl[1] |= spec_ctrl;

		/*
		 * Make sure we set this on the first loop.  It will be
		 * the same value on remaining loops.
		 */
		mds_mode = mode;
	}
	lwkt_setcpu_self(save_gd);
	cpu_ccfence();

	/*
	 * Console message on mitigation mode change
	 */
	kprintf("MDS: support=(");
	if (mds_support == 0) {
		kprintf(" none");
	} else {
		if (mds_support & MDS_AVX512_4VNNIW_SUPPORTED)
			kprintf(" AVX512_4VNNIW");
		if (mds_support & MDS_AVX512_4FMAPS_SUPPORTED)
			kprintf(" AVX512_4FMAPS");
		if (mds_support & MDS_MD_CLEAR_SUPPORTED)
			kprintf(" MD_CLEAR");
		if (mds_support & MDS_TSX_FORCE_ABORT_SUPPORTED)
			kprintf(" TSX_FORCE_ABORT");
		if (mds_support & MDS_NOT_REQUIRED)
			kprintf(" MDS_NOT_REQUIRED");
	}
	kprintf(" ) req=%04x operating=(", (uint16_t)mds_mitigation);
	if (mds_mode == 0) {
		kprintf(" none");
	} else {
		if (mds_mode & MDS_AVX512_4VNNIW_SUPPORTED)
			kprintf(" AVX512_4VNNIW");
		if (mds_mode & MDS_AVX512_4FMAPS_SUPPORTED)
			kprintf(" AVX512_4FMAPS");
		if (mds_mode & MDS_MD_CLEAR_SUPPORTED)
			kprintf(" MD_CLEAR");
		if (mds_mode & MDS_TSX_FORCE_ABORT_SUPPORTED)
			kprintf(" TSX_FORCE_ABORT");
		if (mds_mode & MDS_NOT_REQUIRED)
			kprintf(" MDS_NOT_REQUIRED");
	}
	kprintf(" )\n");
}

#undef CHECK

/*
 * User changes sysctl value
 */
static int
sysctl_mds_mitigation(SYSCTL_HANDLER_ARGS)
{
	char buf[128];
	char *ptr;
	char *iter;
	size_t len;
	int mds;
	int error = 0;
	int loop = 0;

	/*
	 * Return current operating mode or support.
	 */
	if (oidp->oid_kind & CTLFLAG_WR)
		mds = mds_mode;
	else
		mds = mds_support;

	mds &= MDS_AVX512_4VNNIW_SUPPORTED |
	       MDS_AVX512_4FMAPS_SUPPORTED |
	       MDS_MD_CLEAR_SUPPORTED |
	       MDS_TSX_FORCE_ABORT_SUPPORTED |
	       MDS_NOT_REQUIRED;

	while (mds) {
		if (error)
			break;
		if (loop++) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				break;
		}
		if (mds & MDS_AVX512_4VNNIW_SUPPORTED) {
			mds &= ~MDS_AVX512_4VNNIW_SUPPORTED;
			error = SYSCTL_OUT(req, "AVX512_4VNNIW", 13);
		} else
		if (mds & MDS_AVX512_4FMAPS_SUPPORTED) {
			mds &= ~MDS_AVX512_4FMAPS_SUPPORTED;
			error = SYSCTL_OUT(req, "AVX512_4FMAPS", 13);
		} else
		if (mds & MDS_MD_CLEAR_SUPPORTED) {
			mds &= ~MDS_MD_CLEAR_SUPPORTED;
			error = SYSCTL_OUT(req, "MD_CLEAR", 8);
		} else
		if (mds & MDS_TSX_FORCE_ABORT_SUPPORTED) {
			mds &= ~MDS_TSX_FORCE_ABORT_SUPPORTED;
			error = SYSCTL_OUT(req, "TSX_FORCE_ABORT", 15);
		} else
		if (mds & MDS_NOT_REQUIRED) {
			mds &= ~MDS_NOT_REQUIRED;
			error = SYSCTL_OUT(req, "MDS_NOT_REQUIRED", 16);
		}
	}
	if (loop == 0) {
		error = SYSCTL_OUT(req, "NONE", 4);
	}

	if (error || req->newptr == NULL)
		return error;
	if ((oidp->oid_kind & CTLFLAG_WR) == 0)
		return error;

	/*
	 * Change current operating mode
	 */
	len = req->newlen - req->newidx;
	if (len >= sizeof(buf)) {
		error = EINVAL;
		len = 0;
	} else {
		error = SYSCTL_IN(req, buf, len);
	}
	buf[len] = 0;
	iter = &buf[0];
	mds = 0;

	while (error == 0 && iter) {
		ptr = strsep(&iter, " ,\t\r\n");
		if (*ptr == 0)
			continue;
		if (strcasecmp(ptr, "NONE") == 0)
			mds |= 0;
		else if (strcasecmp(ptr, "AVX512_4VNNIW") == 0)
			mds |= MDS_AVX512_4VNNIW_SUPPORTED;
		else if (strcasecmp(ptr, "AVX512_4FMAPS") == 0)
			mds |= MDS_AVX512_4FMAPS_SUPPORTED;
		else if (strcasecmp(ptr, "MD_CLEAR") == 0)
			mds |= MDS_MD_CLEAR_SUPPORTED;
		else if (strcasecmp(ptr, "TSX_FORCE_ABORT") == 0)
			mds |= MDS_TSX_FORCE_ABORT_SUPPORTED;
		else if (strcasecmp(ptr, "MDS_NOT_REQUIRED") == 0)
			mds |= MDS_NOT_REQUIRED;
		else
			error = ENOENT;
	}
	if (error == 0) {
		mds_mitigation = mds;
		mds_sysctl_changed();
	}
	return error;
}

SYSCTL_PROC(_machdep, OID_AUTO, mds_mitigation,
	CTLTYPE_STRING | CTLFLAG_RW,
	0, 0, sysctl_mds_mitigation, "A", "MDS exploit mitigation");
SYSCTL_PROC(_machdep, OID_AUTO, mds_support,
	CTLTYPE_STRING | CTLFLAG_RD,
	0, 0, sysctl_mds_mitigation, "A", "MDS supported features");

/*
 * NOTE: Called at SI_BOOT2_MACHDEP and also when the microcode is
 *	 updated.  Microcode updates must be applied to all cpus
 *	 for support to be recognized.
 */
static void
mds_vm_setup(void *arg)
{
	int inconsistent = 0;
	int supmask;

	/*
	 * Fetch tunable in auto mode
	 */
	if (mds_mitigation < 0) {
		TUNABLE_INT_FETCH("machdep.mds_mitigation", &mds_mitigation);
	}

	if ((supmask = mds_check_support()) != 0) {
		/*
		 * Must be supported on all cpus before we
		 * can enable it.  Returns silently if it
		 * isn't.
		 *
		 * NOTE! arg != NULL indicates we were called
		 *	 from cpuctl after a successful microcode
		 *	 update.
		 */
		if (arg != NULL) {
			globaldata_t save_gd;
			int n;

			save_gd = mycpu;
			for (n = 0; n < ncpus; ++n) {
				lwkt_setcpu_self(globaldata_find(n));
				cpu_ccfence();
				if (mds_check_support() != supmask) {
					inconsistent = 1;
					break;
				}
			}
			lwkt_setcpu_self(save_gd);
			cpu_ccfence();
		}
	}

	/*
	 * Be silent while microcode is being loaded on various CPUs,
	 * until all done.
	 */
	if (inconsistent) {
		mds_mitigation = -1;
		mds_support = 0;
		return;
	}

	/*
	 * IBRS support
	 */
	mds_support = supmask;

	/*
	 * Enable mds_mitigation, set defaults if -1, adjust
	 * tuned value according to support if not.
	 *
	 * NOTE!  MDS is not enabled by default.
	 */
	if (mds_support) {
		if (mds_mitigation < 0) {
			mds_mitigation = 0;

			if ((mds_support & MDS_NOT_REQUIRED) == 0 &&
			    (mds_support & MDS_MD_CLEAR_SUPPORTED)) {
				/* mds_mitigation |= MDS_MD_CLEAR_SUPPORTED; */
			}
		}
	} else {
		mds_mitigation = -1;
	}

	/*
	 * Disallow sysctl changes when there is no support (otherwise
	 * the wrmsr will cause a protection fault).
	 */
	if (mds_mitigation < 0)
		sysctl___machdep_mds_mitigation.oid_kind &= ~CTLFLAG_WR;
	else
		sysctl___machdep_mds_mitigation.oid_kind |= CTLFLAG_WR;

	mds_sysctl_changed();
}

/*
 * NOTE: Called at SI_BOOT2_MACHDEP and also when the microcode is
 *	 updated.  Microcode updates must be applied to all cpus
 *	 for support to be recognized.
 */
void
mitigation_vm_setup(void *arg)
{
	spectre_vm_setup(arg);
	mds_vm_setup(arg);
}

SYSINIT(mitigation_vm_setup, SI_BOOT2_MACHDEP, SI_ORDER_ANY,
	mitigation_vm_setup, NULL);

/*
 * platform-specific vmspace initialization (nothing for x86_64)
 */
void
cpu_vmspace_alloc(struct vmspace *vm __unused)
{
}

void
cpu_vmspace_free(struct vmspace *vm __unused)
{
}

int
kvm_access_check(vm_offset_t saddr, vm_offset_t eaddr, int prot)
{
	vm_offset_t addr;

	if (saddr < KvaStart)
		return EFAULT;
	if (eaddr >= KvaEnd)
		return EFAULT;
	for (addr = saddr; addr < eaddr; addr += PAGE_SIZE)  {
		if (pmap_kextract(addr) == 0)
			return EFAULT;
	}
	if (!kernacc((caddr_t)saddr, eaddr - saddr, prot))
		return EFAULT;
	return 0;
}

#if 0

void _test_frame_enter(struct trapframe *frame);
void _test_frame_exit(struct trapframe *frame);

void
_test_frame_enter(struct trapframe *frame)
{
	thread_t td = curthread;

	if (ISPL(frame->tf_cs) == SEL_UPL) {
		KKASSERT(td->td_lwp);
                KASSERT(td->td_lwp->lwp_md.md_regs == frame,
                        ("_test_frame_exit: Frame mismatch %p %p",
			td->td_lwp->lwp_md.md_regs, frame));
	    td->td_lwp->lwp_saveusp = (void *)frame->tf_rsp;
	    td->td_lwp->lwp_saveupc = (void *)frame->tf_rip;
	}
	if ((char *)frame < td->td_kstack ||
	    (char *)frame > td->td_kstack + td->td_kstack_size) {
		panic("_test_frame_exit: frame not on kstack %p kstack=%p",
			frame, td->td_kstack);
	}
}

void
_test_frame_exit(struct trapframe *frame)
{
	thread_t td = curthread;

	if (ISPL(frame->tf_cs) == SEL_UPL) {
		KKASSERT(td->td_lwp);
                KASSERT(td->td_lwp->lwp_md.md_regs == frame,
                        ("_test_frame_exit: Frame mismatch %p %p",
			td->td_lwp->lwp_md.md_regs, frame));
		if (td->td_lwp->lwp_saveusp != (void *)frame->tf_rsp) {
			kprintf("_test_frame_exit: %s:%d usp mismatch %p/%p\n",
				td->td_comm, td->td_proc->p_pid,
				td->td_lwp->lwp_saveusp,
				(void *)frame->tf_rsp);
		}
		if (td->td_lwp->lwp_saveupc != (void *)frame->tf_rip) {
			kprintf("_test_frame_exit: %s:%d upc mismatch %p/%p\n",
				td->td_comm, td->td_proc->p_pid,
				td->td_lwp->lwp_saveupc,
				(void *)frame->tf_rip);
		}

		/*
		 * adulterate the fields to catch entries that
		 * don't run through test_frame_enter
		 */
		td->td_lwp->lwp_saveusp =
			(void *)~(intptr_t)td->td_lwp->lwp_saveusp;
		td->td_lwp->lwp_saveupc =
			(void *)~(intptr_t)td->td_lwp->lwp_saveupc;
	}
	if ((char *)frame < td->td_kstack ||
	    (char *)frame > td->td_kstack + td->td_kstack_size) {
		panic("_test_frame_exit: frame not on kstack %p kstack=%p",
			frame, td->td_kstack);
	}
}

#endif
