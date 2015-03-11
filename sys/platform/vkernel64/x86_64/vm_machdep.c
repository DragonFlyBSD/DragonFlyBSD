/*-
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * Copyright (c) 2008 The DragonFly Project.
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

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/pcb.h>
#include <machine/pcb_ext.h>
#include <machine/segments.h>
#include <machine/globaldata.h>	/* npxthread */

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>

#include <bus/isa/isa.h>

char machine[] = MACHINE;
SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
	      machine, 0, "Machine class");

/*
 * Finish a fork operation, with lwp lp2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct lwp *lp1, struct lwp *lp2, int flags)
{
	struct pcb *pcb2;

	if ((flags & RFPROC) == 0) {
		if ((flags & RFMEM) == 0) {
			/* unshare user LDT */
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
	 */
	pcb2->pcb_unused01 = 0;
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
	 * pcb2->pcb_flags:	cloned above (always 0 here?).
	 * pcb2->pcb_onfault:	cloned above (always NULL here?).
	 */

	/*
	 * XXX don't copy the i/o pages.  this should probably be fixed.
	 */
	pcb2->pcb_ext = NULL;

        /* Copy the LDT, if necessary. */
        if (pcb2->pcb_ldt != NULL) {
		if (flags & RFMEM) {
			pcb2->pcb_ldt->ldt_refcnt++;
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
	regs->tf_rdi = (long)params->lwp_arg; /* JG Can this be in userspace addresses? */
	/*
	 * Set up fake return address.  As the lwp function may never return,
	 * we simply copy out a NULL pointer and force the lwp to receive
	 * a SIGSEGV if it returns anyways.
	 */
	regs->tf_rsp -= sizeof(void *);
	error = copyout(&bad_return, (void *)regs->tf_rsp, sizeof(bad_return));
	if (error)
		return (error);

	cpu_set_fork_handler(lp,
	    (void (*)(void *, struct trapframe *))generic_lwp_return, lp);
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

	/* Some i386 functionality was dropped */
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

int
grow_stack(struct proc *p, u_long sp)
{
	int rv;

	rv = vm_map_growstack (p, sp);
	if (rv != KERN_SUCCESS)
		return (0);

	return (1);
}

/*
 * Used by /dev/kmem to determine if we can safely read or write
 * the requested KVA range.  Some portions of kernel memory are
 * not governed by our virtual page table.
 */
extern int64_t _end;
extern void _start(void);

int
kvm_access_check(vm_offset_t saddr, vm_offset_t eaddr, int prot)
{
	vm_offset_t addr;

	if (saddr >= trunc_page((vm_offset_t)&_start) && eaddr <= round_page((vm_offset_t)&_end))
		return 0;
	if (saddr < KvaStart)
		return EFAULT;
	if (eaddr >= KvaEnd)
		return EFAULT;
	for (addr = saddr; addr < eaddr; addr += PAGE_SIZE)  {
		if (pmap_extract(&kernel_pmap, addr) == 0)
			return EFAULT;
	}
	if (!kernacc((caddr_t)saddr, eaddr - saddr, prot))
		return EFAULT;
	return 0;
}
