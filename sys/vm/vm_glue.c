/*
 * (MPSAFE)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	from: @(#)vm_glue.c	8.6 (Berkeley) 1/5/94
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/vm/vm_glue.c,v 1.94.2.4 2003/01/13 22:51:17 dillon Exp $
 */

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/buf.h>
#include <sys/shm.h>
#include <sys/vmmeter.h>
#include <sys/sysctl.h>

#include <sys/kernel.h>
#include <sys/unistd.h>

#include <machine/limits.h>
#include <machine/vmm.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <sys/user.h>
#include <vm/vm_page2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>

/*
 * THIS MUST BE THE LAST INITIALIZATION ITEM!!!
 *
 * Note: run scheduling should be divorced from the vm system.
 */
static void scheduler (void *);
SYSINIT(scheduler, SI_SUB_RUN_SCHEDULER, SI_ORDER_FIRST, scheduler, NULL)

#ifdef INVARIANTS

static int swap_debug = 0;
SYSCTL_INT(_vm, OID_AUTO, swap_debug,
	CTLFLAG_RW, &swap_debug, 0, "");

#endif

static int scheduler_notify;

static void swapout (struct proc *);

/*
 * No requirements.
 */
int
kernacc(c_caddr_t addr, int len, int rw)
{
	boolean_t rv;
	vm_offset_t saddr, eaddr;
	vm_prot_t prot;

	KASSERT((rw & (~VM_PROT_ALL)) == 0,
	    ("illegal ``rw'' argument to kernacc (%x)", rw));

	/*
	 * The globaldata space is not part of the kernel_map proper,
	 * check access separately.
	 */
	if (is_globaldata_space((vm_offset_t)addr, (vm_offset_t)(addr + len)))
		return (TRUE);

	/*
	 * Nominal kernel memory access - check access via kernel_map.
	 */
	if ((vm_offset_t)addr + len > kernel_map.max_offset ||
	    (vm_offset_t)addr + len < (vm_offset_t)addr) {
		return (FALSE);
	}
	prot = rw;
	saddr = trunc_page((vm_offset_t)addr);
	eaddr = round_page((vm_offset_t)addr + len);
	rv = vm_map_check_protection(&kernel_map, saddr, eaddr, prot, FALSE);

	return (rv == TRUE);
}

/*
 * No requirements.
 */
int
useracc(c_caddr_t addr, int len, int rw)
{
	boolean_t rv;
	vm_prot_t prot;
	vm_map_t map;
	vm_map_entry_t save_hint;
	vm_offset_t wrap;
	vm_offset_t gpa;

	KASSERT((rw & (~VM_PROT_ALL)) == 0,
	    ("illegal ``rw'' argument to useracc (%x)", rw));
	prot = rw;

	if (curthread->td_vmm) {
		if (vmm_vm_get_gpa(curproc, (register_t *)&gpa, (register_t) addr))
			panic("%s: could not get GPA\n", __func__);
		addr = (c_caddr_t) gpa;
	}

	/*
	 * XXX - check separately to disallow access to user area and user
	 * page tables - they are in the map.
	 */
	wrap = (vm_offset_t)addr + len;
	if (wrap > VM_MAX_USER_ADDRESS || wrap < (vm_offset_t)addr) {
		return (FALSE);
	}
	map = &curproc->p_vmspace->vm_map;
	vm_map_lock_read(map);
	/*
	 * We save the map hint, and restore it.  Useracc appears to distort
	 * the map hint unnecessarily.
	 */
	save_hint = map->hint;
	rv = vm_map_check_protection(map, trunc_page((vm_offset_t)addr),
				     round_page(wrap), prot, TRUE);
	map->hint = save_hint;
	vm_map_unlock_read(map);
	
	return (rv == TRUE);
}

/*
 * No requirements.
 */
void
vslock(caddr_t addr, u_int len)
{
	if (len) {
		vm_map_wire(&curproc->p_vmspace->vm_map,
			    trunc_page((vm_offset_t)addr),
			    round_page((vm_offset_t)addr + len), 0);
	}
}

/*
 * No requirements.
 */
void
vsunlock(caddr_t addr, u_int len)
{
	if (len) {
		vm_map_wire(&curproc->p_vmspace->vm_map,
			    trunc_page((vm_offset_t)addr),
			    round_page((vm_offset_t)addr + len),
			    KM_PAGEABLE);
	}
}

/*
 * Implement fork's actions on an address space.
 * Here we arrange for the address space to be copied or referenced,
 * allocate a user struct (pcb and kernel stack), then call the
 * machine-dependent layer to fill those in and make the new process
 * ready to run.  The new process is set up so that it returns directly
 * to user mode to avoid stack copying and relocation problems.
 *
 * No requirements.
 */
void
vm_fork(struct proc *p1, struct proc *p2, int flags)
{
	if ((flags & RFPROC) == 0) {
		/*
		 * Divorce the memory, if it is shared, essentially
		 * this changes shared memory amongst threads, into
		 * COW locally.
		 */
		if ((flags & RFMEM) == 0) {
			if (p1->p_vmspace->vm_sysref.refcnt > 1) {
				vmspace_unshare(p1);
			}
		}
		cpu_fork(ONLY_LWP_IN_PROC(p1), NULL, flags);
		return;
	}

	if (flags & RFMEM) {
		vmspace_ref(p1->p_vmspace);
		p2->p_vmspace = p1->p_vmspace;
	}

	while (vm_page_count_severe()) {
		vm_wait(0);
	}

	if ((flags & RFMEM) == 0) {
		p2->p_vmspace = vmspace_fork(p1->p_vmspace);

		pmap_pinit2(vmspace_pmap(p2->p_vmspace));

		if (p1->p_vmspace->vm_shm)
			shmfork(p1, p2);
	}

	pmap_init_proc(p2);
}

/*
 * Set default limits for VM system.  Call during proc0's initialization.
 *
 * Called from the low level boot code only.
 */
void
vm_init_limits(struct proc *p)
{
	int rss_limit;

	/*
	 * Set up the initial limits on process VM. Set the maximum resident
	 * set size to be half of (reasonably) available memory.  Since this
	 * is a soft limit, it comes into effect only when the system is out
	 * of memory - half of main memory helps to favor smaller processes,
	 * and reduces thrashing of the object cache.
	 */
	p->p_rlimit[RLIMIT_STACK].rlim_cur = dflssiz;
	p->p_rlimit[RLIMIT_STACK].rlim_max = maxssiz;
	p->p_rlimit[RLIMIT_DATA].rlim_cur = dfldsiz;
	p->p_rlimit[RLIMIT_DATA].rlim_max = maxdsiz;
	/* limit the limit to no less than 2MB */
	rss_limit = max(vmstats.v_free_count, 512);
	p->p_rlimit[RLIMIT_RSS].rlim_cur = ptoa(rss_limit);
	p->p_rlimit[RLIMIT_RSS].rlim_max = RLIM_INFINITY;
}

/*
 * Faultin the specified process.  Note that the process can be in any
 * state.  Just clear P_SWAPPEDOUT and call wakeup in case the process is
 * sleeping.
 *
 * No requirements.
 */
void
faultin(struct proc *p)
{
	if (p->p_flags & P_SWAPPEDOUT) {
		/*
		 * The process is waiting in the kernel to return to user
		 * mode but cannot until P_SWAPPEDOUT gets cleared.
		 */
		lwkt_gettoken(&p->p_token);
		p->p_flags &= ~(P_SWAPPEDOUT | P_SWAPWAIT);
#ifdef INVARIANTS
		if (swap_debug)
			kprintf("swapping in %d (%s)\n", p->p_pid, p->p_comm);
#endif
		wakeup(p);
		lwkt_reltoken(&p->p_token);
	}
}

/*
 * Kernel initialization eventually falls through to this function,
 * which is process 0.
 *
 * This swapin algorithm attempts to swap-in processes only if there
 * is enough space for them.  Of course, if a process waits for a long
 * time, it will be swapped in anyway.
 */
struct scheduler_info {
	struct proc *pp;
	int ppri;
};

static int scheduler_callback(struct proc *p, void *data);

static void
scheduler(void *dummy)
{
	struct scheduler_info info;
	struct proc *p;

	KKASSERT(!IN_CRITICAL_SECT(curthread));
loop:
	scheduler_notify = 0;
	/*
	 * Don't try to swap anything in if we are low on memory.
	 */
	if (vm_page_count_severe()) {
		vm_wait(0);
		goto loop;
	}

	/*
	 * Look for a good candidate to wake up
	 */
	info.pp = NULL;
	info.ppri = INT_MIN;
	allproc_scan(scheduler_callback, &info);

	/*
	 * Nothing to do, back to sleep for at least 1/10 of a second.  If
	 * we are woken up, immediately process the next request.  If
	 * multiple requests have built up the first is processed 
	 * immediately and the rest are staggered.
	 */
	if ((p = info.pp) == NULL) {
		tsleep(&proc0, 0, "nowork", hz / 10);
		if (scheduler_notify == 0)
			tsleep(&scheduler_notify, 0, "nowork", 0);
		goto loop;
	}

	/*
	 * Fault the selected process in, then wait for a short period of
	 * time and loop up.
	 *
	 * XXX we need a heuristic to get a measure of system stress and
	 * then adjust our stagger wakeup delay accordingly.
	 */
	lwkt_gettoken(&proc_token);
	faultin(p);
	p->p_swtime = 0;
	PRELE(p);
	lwkt_reltoken(&proc_token);
	tsleep(&proc0, 0, "swapin", hz / 10);
	goto loop;
}

/*
 * The caller must hold proc_token.
 */
static int
scheduler_callback(struct proc *p, void *data)
{
	struct scheduler_info *info = data;
	struct lwp *lp;
	segsz_t pgs;
	int pri;

	if (p->p_flags & P_SWAPWAIT) {
		pri = 0;
		FOREACH_LWP_IN_PROC(lp, p) {
			/* XXX lwp might need a different metric */
			pri += lp->lwp_slptime;
		}
		pri += p->p_swtime - p->p_nice * 8;

		/*
		 * The more pages paged out while we were swapped,
		 * the more work we have to do to get up and running
		 * again and the lower our wakeup priority.
		 *
		 * Each second of sleep time is worth ~1MB
		 */
		lwkt_gettoken(&p->p_vmspace->vm_map.token);
		pgs = vmspace_resident_count(p->p_vmspace);
		if (pgs < p->p_vmspace->vm_swrss) {
			pri -= (p->p_vmspace->vm_swrss - pgs) /
				(1024 * 1024 / PAGE_SIZE);
		}
		lwkt_reltoken(&p->p_vmspace->vm_map.token);

		/*
		 * If this process is higher priority and there is
		 * enough space, then select this process instead of
		 * the previous selection.
		 */
		if (pri > info->ppri) {
			if (info->pp)
				PRELE(info->pp);
			PHOLD(p);
			info->pp = p;
			info->ppri = pri;
		}
	}
	return(0);
}

/*
 * SMP races ok.
 * No requirements.
 */
void
swapin_request(void)
{
	if (scheduler_notify == 0) {
		scheduler_notify = 1;
		wakeup(&scheduler_notify);
	}
}

#ifndef NO_SWAPPING

#define	swappable(p) \
	(((p)->p_lock == 0) && \
	((p)->p_flags & (P_TRACED|P_SYSTEM|P_SWAPPEDOUT|P_WEXIT)) == 0)


/*
 * Swap_idle_threshold1 is the guaranteed swapped in time for a process
 */
static int swap_idle_threshold1 = 15;
SYSCTL_INT(_vm, OID_AUTO, swap_idle_threshold1,
	CTLFLAG_RW, &swap_idle_threshold1, 0, "Guaranteed process resident time (sec)");

/*
 * Swap_idle_threshold2 is the time that a process can be idle before
 * it will be swapped out, if idle swapping is enabled.  Default is
 * one minute.
 */
static int swap_idle_threshold2 = 60;
SYSCTL_INT(_vm, OID_AUTO, swap_idle_threshold2,
	CTLFLAG_RW, &swap_idle_threshold2, 0, "Time (sec) a process can idle before being swapped");

/*
 * Swapout is driven by the pageout daemon.  Very simple, we find eligible
 * procs and mark them as being swapped out.  This will cause the kernel
 * to prefer to pageout those proc's pages first and the procs in question 
 * will not return to user mode until the swapper tells them they can.
 *
 * If any procs have been sleeping/stopped for at least maxslp seconds,
 * they are swapped.  Else, we swap the longest-sleeping or stopped process,
 * if any, otherwise the longest-resident process.
 */

static int swapout_procs_callback(struct proc *p, void *data);

/*
 * No requirements.
 */
void
swapout_procs(int action)
{
	allproc_scan(swapout_procs_callback, &action);
}

/*
 * The caller must hold proc_token
 */
static int
swapout_procs_callback(struct proc *p, void *data)
{
	struct lwp *lp;
	int action = *(int *)data;
	int minslp = -1;

	if (!swappable(p))
		return(0);

	lwkt_gettoken(&p->p_token);

	/*
	 * We only consider active processes.
	 */
	if (p->p_stat != SACTIVE && p->p_stat != SSTOP) {
		lwkt_reltoken(&p->p_token);
		return(0);
	}

	FOREACH_LWP_IN_PROC(lp, p) {
		/*
		 * do not swap out a realtime process
		 */
		if (RTP_PRIO_IS_REALTIME(lp->lwp_rtprio.type)) {
			lwkt_reltoken(&p->p_token);
			return(0);
		}

		/*
		 * Guarentee swap_idle_threshold time in memory
		 */
		if (lp->lwp_slptime < swap_idle_threshold1) {
			lwkt_reltoken(&p->p_token);
			return(0);
		}

		/*
		 * If the system is under memory stress, or if we
		 * are swapping idle processes >= swap_idle_threshold2,
		 * then swap the process out.
		 */
		if (((action & VM_SWAP_NORMAL) == 0) &&
		    (((action & VM_SWAP_IDLE) == 0) ||
		     (lp->lwp_slptime < swap_idle_threshold2))) {
			lwkt_reltoken(&p->p_token);
			return(0);
		}

		if (minslp == -1 || lp->lwp_slptime < minslp)
			minslp = lp->lwp_slptime;
	}

	/*
	 * If the process has been asleep for awhile, swap
	 * it out.
	 */
	if ((action & VM_SWAP_NORMAL) ||
	    ((action & VM_SWAP_IDLE) &&
	     (minslp > swap_idle_threshold2))) {
		swapout(p);
	}

	/*
	 * cleanup our reference
	 */
	lwkt_reltoken(&p->p_token);

	return(0);
}

/*
 * The caller must hold proc_token and p->p_token
 */
static void
swapout(struct proc *p)
{
#ifdef INVARIANTS
	if (swap_debug)
		kprintf("swapping out %d (%s)\n", p->p_pid, p->p_comm);
#endif
	++p->p_ru.ru_nswap;

	/*
	 * remember the process resident count
	 */
	p->p_vmspace->vm_swrss = vmspace_resident_count(p->p_vmspace);
	p->p_flags |= P_SWAPPEDOUT;
	p->p_swtime = 0;
}

#endif /* !NO_SWAPPING */

