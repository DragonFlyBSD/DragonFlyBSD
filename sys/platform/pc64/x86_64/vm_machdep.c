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
#include <sys/mplock2.h>

#include <bus/isa/isa.h>

static void	cpu_reset_real (void);

int spectre_mitigation = -1;

static int spectre_ibrs_mode = 0;
SYSCTL_INT(_machdep, OID_AUTO, spectre_ibrs_mode, CTLFLAG_RD,
	&spectre_ibrs_mode, 0, "current IBRS mode");
static int spectre_ibpb_mode = 0;
SYSCTL_INT(_machdep, OID_AUTO, spectre_ibpb_mode, CTLFLAG_RD,
	&spectre_ibpb_mode, 0, "current IBPB mode");
static int spectre_ibrs_supported = 0;
SYSCTL_INT(_machdep, OID_AUTO, spectre_ibrs_supported, CTLFLAG_RD,
	&spectre_ibrs_supported, 0, "IBRS mode supported");
static int spectre_ibpb_supported = 0;
SYSCTL_INT(_machdep, OID_AUTO, spectre_ibpb_supported, CTLFLAG_RD,
	&spectre_ibpb_supported, 0, "IBPB mode supported");

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

/*
 * Convert kernel VA to physical address
 */
vm_paddr_t
kvtop(void *addr)
{
	vm_paddr_t pa;

	pa = pmap_kextract((vm_offset_t)addr);
	if (pa == 0)
		panic("kvtop: zero page frame");
	return (pa);
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
void spectre_vm_setup(void *arg);

/*
 * Check for IBPB and IBRS support
 *
 * Returns a mask: 	0x1	IBRS supported
 *			0x2	IBPB supported
 */
static
int
spectre_check_support(void)
{
	uint32_t p[4];
	int rv = 0;

	/*
	 * SPEC_CTRL (bit 26) and STIBP support (bit 27)
	 *
	 * XXX Not sure what the STIBP flag is meant to be used for.
	 *
	 * SPEC_CTRL indicates IBRS and IBPB support.
	 */
	p[0] = 0;
	p[1] = 0;
	p[2] = 0;
	p[3] = 0;
	cpuid_count(7, 0, p);
	if (p[3] & CPUID_7_0_I3_SPEC_CTRL)
		rv |= 3;

	/*
	 * 0x80000008 p[1] bit 12 indicates IBPB support
	 *
	 * This bit might be set even though SPEC_CTRL is not set.
	 */
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		p[0] = 0;
		p[1] = 0;
		p[2] = 0;
		p[3] = 0;
		do_cpuid(0x80000008U, p);
		if (p[1] & CPUID_80000008_I1_IBPB_SUPPORT)
			rv |= 2;
	}

	return rv;
}

/*
 * Iterate CPUs and adjust MSR for global operations, since
 * the KMMU* code won't do it if spectre_mitigation is 0 or 2.
 */
static
void
spectre_sysctl_changed(void)
{
	globaldata_t save_gd;
	struct trampframe *tr;
	int n;

	/*
	 * Console message on mitigation mode change
	 */
	kprintf("machdep.spectre_mitigation=%d: ", spectre_mitigation);

	if (spectre_ibrs_supported == 0) {
		kprintf("IBRS=NOSUPPORT, ");
	} else {
		switch(spectre_mitigation & 3) {
		case 0:
			kprintf("IBRS=0 (disabled), ");
			break;
		case 1:
			kprintf("IBRS=1 (kern-only), ");
			break;
		case 2:
			kprintf("IBRS=2 (always-on), ");
			break;
		case 3:
			kprintf("IBRS=?, ");
			break;
		}
	}

	if (spectre_ibpb_supported == 0) {
		kprintf("IBPB=NOSUPPORT\n");
	} else {
		switch(spectre_mitigation & 4) {
		case 0:
			kprintf("IBPB=0 (disabled)\n");
			break;
		case 4:
			kprintf("IBPB=1 (enabled)\n");
			break;
		}
	}

	/*
	 * Fixup state
	 */
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
		 */
		tr->tr_pcb_gflags &= ~(PCB_IBRS1 | PCB_IBRS2 | PCB_IBPB);
		spectre_ibrs_mode = 0;
		spectre_ibpb_mode = 0;

		/*
		 * Don't try to parse if not available
		 */
		if (spectre_mitigation < 0)
			continue;

		/*
		 * IBRS mode
		 */
		switch(spectre_mitigation & 3) {
		case 0:
			/*
			 * Disable IBRS
			 *
			 * Make sure IBRS is turned off in case we were in
			 * a global mode before.
			 */
			if (spectre_ibrs_supported)
				wrmsr(MSR_SPEC_CTRL, 0);
			break;
		case 1:
			/*
			 * IBRS in kernel
			 */
			if (spectre_ibrs_supported) {
				tr->tr_pcb_gflags |= PCB_IBRS1;
				wrmsr(MSR_SPEC_CTRL, 1);
				spectre_ibrs_mode = 1;
			}
			break;
		case 2:
			/*
			 * IBRS at all times
			 */
			if (spectre_ibrs_supported) {
				tr->tr_pcb_gflags |= PCB_IBRS2;
				wrmsr(MSR_SPEC_CTRL, 1);
				spectre_ibrs_mode = 2;
			}
			break;
		}

		/*
		 * IBPB mode
		 */
		if (spectre_mitigation & 4) {
			if (spectre_ibpb_supported) {
				tr->tr_pcb_gflags |= PCB_IBPB;
				spectre_ibpb_mode = 1;
			}
		}
	}
	lwkt_setcpu_self(save_gd);
	cpu_ccfence();
}

/*
 * User changes sysctl value
 */
static int
sysctl_spectre_mitigation(SYSCTL_HANDLER_ARGS)
{
	int spectre;
	int error;

	spectre = spectre_mitigation;
	error = sysctl_handle_int(oidp, &spectre, 0, req);
	if (error || req->newptr == NULL)
		return error;
	spectre_mitigation = spectre;
	spectre_sysctl_changed();

	return 0;
}

SYSCTL_PROC(_machdep, OID_AUTO, spectre_mitigation, CTLTYPE_INT | CTLFLAG_RW,
	0, 0, sysctl_spectre_mitigation, "I", "Spectre exploit mitigation");

/*
 * NOTE: Called at SI_BOOT2_MACHDEP and also when the microcode is
 *	 updated.  Microcode updates must be applied to all cpus
 *	 for support to be recognized.
 */
void
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
		spectre_ibrs_supported = 0;
		spectre_ibpb_supported = 0;
		return;
	}

	/*
	 * IBRS support
	 */
	if (supmask & 1)
		spectre_ibrs_supported = 1;
	else
		spectre_ibrs_supported = 0;

	/*
	 * IBPB support.
	 */
	if (supmask & 2)
		spectre_ibpb_supported = 1;
	else
		spectre_ibpb_supported = 0;

	/*
	 * Enable spectre_mitigation, set defaults if -1, adjust
	 * tuned value according to support if not.
	 *
	 * NOTE!  We do not enable IBPB for user->kernel transitions
	 *	  by default, so this code is commented out for now.
	 */
	if (spectre_ibrs_supported || spectre_ibpb_supported) {
		if (spectre_mitigation < 0) {
			spectre_mitigation = 0;
			if (spectre_ibrs_supported)
				spectre_mitigation |= 1;
#if 0
			if (spectre_ibpb_supported)
				spectre_mitigation |= 4;
#endif
		}
		if (spectre_ibrs_supported == 0)
			spectre_mitigation &= ~3;
		if (spectre_ibpb_supported == 0)
			spectre_mitigation &= ~4;
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

SYSINIT(spectre_vm_setup, SI_BOOT2_MACHDEP, SI_ORDER_ANY,
	spectre_vm_setup, NULL);

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
