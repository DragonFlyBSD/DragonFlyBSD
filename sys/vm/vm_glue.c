/*
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
#include <vm/vm_page2.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

/*
 * THIS MUST BE THE LAST INITIALIZATION ITEM!!!
 *
 * Process 0 falls into this function, just loop on nothing.
 */

static void scheduler(void *);
SYSINIT(scheduler, SI_SUB_RUN_SCHEDULER, SI_ORDER_FIRST, scheduler, NULL);

#ifdef INVARIANTS

static int swap_debug = 0;
SYSCTL_INT(_vm, OID_AUTO, swap_debug, CTLFLAG_RW, &swap_debug, 0, "");

#endif

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
	if ((vm_offset_t)addr + len > vm_map_max(&kernel_map) ||
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

	rv = vm_map_check_protection(map, trunc_page((vm_offset_t)addr),
				     round_page(wrap), prot, TRUE);
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
 * Implement fork's actions on an address space.  Here we arrange for the
 * address space to be copied or referenced, allocate a user struct (pcb
 * and kernel stack), then call the machine-dependent layer to fill those
 * in and make the new process ready to run.  The new process is set up
 * so that it returns directly to user mode to avoid stack copying and
 * relocation problems.
 *
 * If p2 is NULL and RFPROC is 0 we are just divorcing parts of the process
 * from itself.
 *
 * Otherwise if p2 is NULL the new vmspace is not to be associated with any
 * process or thread (so things like /dev/upmap and /dev/lpmap are not
 * retained).
 *
 * Otherwise if p2 is not NULL then process specific mappings will be forked.
 * If lp2 is not NULL only the thread-specific mappings for lp2 are forked,
 * otherwise no thread-specific mappings are forked.
 *
 * No requirements.
 */
void
vm_fork(struct proc *p1, struct proc *p2, struct lwp *lp2, int flags)
{
	if ((flags & RFPROC) == 0) {
		/*
		 * Divorce the memory, if it is shared, essentially
		 * this changes shared memory amongst threads, into
		 * COW locally.
		 */
		if ((flags & RFMEM) == 0) {
			if (vmspace_getrefs(p1->p_vmspace) > 1) {
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
		p2->p_vmspace = vmspace_fork(p1->p_vmspace, p2, lp2);

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
 * process 0 winds up here after all kernel initialization sysinits have
 * run.
 */
static void
scheduler(void *dummy)
{
	for (;;)
		tsleep(&proc0, 0, "idle", 0);
}
