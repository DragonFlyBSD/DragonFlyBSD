/*
 * Copyright (c) 2003,2004,2006 The DragonFly Project.  All rights reserved.
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
 */

/*
 * Implement upcall registration and dispatch.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/upcall.h>
#include <sys/thread2.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/lock.h>
#include <sys/signalvar.h>

#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <machine/cpu.h>

MALLOC_DEFINE(M_UPCALL, "upcalls", "upcall registration structures");

#ifdef SMP

static void
sigupcall_remote(void *arg)
{
 	struct lwp *lp = arg;
	if (lp == lwkt_preempted_proc())
		sigupcall();
}

#endif

/*
 * upc_register:
 *
 *	Register an upcall context wrapper and procedure.  Note that the
 *	upcall context is set globally for the process, not for each upcall.
 *
 * ARGS(struct upcall *upc, upcall_func_t ctx, upcall_func_t func, void *data)
 *
 * MPALMOSTSAFE
 */
int
sys_upc_register(struct upc_register_args *uap)
{
    struct lwp *lp = curthread->td_lwp;
    struct vmspace *vm = curproc->p_vmspace;
    struct vmupcall *vu;

    /*
     * Note: inconsequential MP race
     */
    if (vm->vm_upccount >= UPCALL_MAXCOUNT)
	return(EFBIG);

    vu = kmalloc(sizeof(struct vmupcall), M_UPCALL, M_WAITOK|M_ZERO);
    vu->vu_ctx = uap->ctxfunc;
    vu->vu_func = uap->func;
    vu->vu_data = uap->data;
    vu->vu_lwp = lp;
    lp->lwp_upcall = uap->upc;

    get_mplock();
    if (vm->vm_upcalls != NULL)
	vu->vu_id = vm->vm_upcalls->vu_id + 1;
    else
	vu->vu_id = UPC_RESERVED;
    vu->vu_next = vm->vm_upcalls;
    vm->vm_upcalls = vu;
    ++vm->vm_upccount;
    rel_mplock();
    uap->sysmsg_result = vu->vu_id;
    return(0);
}

/*
 * upc_control:
 *
 * ARGS(int cmd, int upcid, void *data)
 *
 * MPALMOSTSAFE
 */
int
sys_upc_control(struct upc_control_args *uap)
{
    struct lwp *lp = curthread->td_lwp;
    struct lwp *targlp;
    struct vmspace *vms = curproc->p_vmspace;
    struct vmupcall *vu;
    struct vmupcall *vu_send;
    struct vmupcall **vupp;
    int error;

    get_mplock();
    switch(uap->cmd) {
    case UPC_CONTROL_DISPATCH:
	/*
	 * Dispatch the specified upcall id or the next pending id if -1.
	 * the upcall will be marked pending but an actual upcall will only
	 * occur if userland is not in a critical section and the userland
	 * pending bit is not set.
	 *
	 * You can dispatch an upcall associated with your process or another
	 * process sharing the same VM space.
	 */
	error = (uap->upcid == -1) ? 0 : ENOENT;
	for (vu = vms->vm_upcalls; vu; vu = vu->vu_next) {
	    if (vu->vu_id == uap->upcid || 
		(uap->upcid == -1 &&
		vu->vu_pending >= (int)(intptr_t)uap->data && vu->vu_lwp == lp)
	    ) {
		if (vu->vu_pending < (int)(intptr_t)uap->data)
		    vu->vu_pending = (int)(intptr_t)uap->data;
		error = 0;
		targlp = vu->vu_lwp;
		targlp->lwp_proc->p_flag |= P_UPCALLPEND;	/* XXX lwp flags */
		if (targlp->lwp_proc->p_flag & P_UPCALLWAIT)
		    wakeup(&targlp->lwp_upcall);
#ifdef SMP
		if (targlp->lwp_thread->td_gd != mycpu)
		    lwkt_send_ipiq(targlp->lwp_thread->td_gd, sigupcall_remote, targlp);
		else
		    sigupcall();
#else
		sigupcall();
#endif
		break;
	    }
	}
	break;
    case UPC_CONTROL_NEXT:
	/*
	 * This is used by the context code to fetch the next pending upcall.
	 * The context code has two choices:  (A) it can drop
	 * upcall->crit_count and set upcall->pending then make this call
	 * unconditionally or * (B) it can drop upcall->crit_count and then
	 * test upcall->pending and only make this call if upcall->pending
	 * is set.  If upcall->pending is clear the context code can pop
	 * the upcall stack itself and return without entering into the kernel
	 * again.  (B) is more efficient but leaves a small window of
	 * opportunity where multiple upcalls can pushdown the stack.
	 *
	 * If another upcall is pending the crit_count will be bumped and
	 * the function, data, and context pointers will be returned in
	 * registers (C cannot call this routine).  If no more upcalls are
	 * pending the pending bit will be cleared and the 'data' argument
	 * is expected to be pointing at the upcall context which we will
	 * then pop, returning to the original code that was interrupted
	 * (NOT the context code).
	 */
	vu_send = NULL;
	for (vu = vms->vm_upcalls; vu; vu = vu->vu_next) {
	    if (vu->vu_lwp == lp && vu->vu_pending) {
		if (vu_send)
		    break;
		vu_send = vu;
	    }
	}
	/*
	 * vu_send may be NULL, indicating that no more upcalls are pending
	 * for this cpu.  We set the userland pending bit based on whether
	 * additional upcalls are pending or not.
	 */
	error = fetchupcall(vu_send, vu != NULL, uap->data);
	break;
    case UPC_CONTROL_DELETE:
	/*
	 * Delete the specified upcall id.  If the upcall id is -1, delete
	 * all upcall id's associated with the current process.
	 */
	error = (uap->upcid == -1) ? 0 : ENOENT;
	vupp = &vms->vm_upcalls;
	while ((vu = *vupp) != NULL) {
	    if (vu->vu_id == uap->upcid || 
		(uap->upcid == -1 && vu->vu_lwp == lp)
	    ) {
		*vupp = vu->vu_next;
		error = 0;
		kfree(vu, M_UPCALL);
	    } else {
		vupp = &vu->vu_next;
	    }
	}
	break;
    case UPC_CONTROL_POLL:
    case UPC_CONTROL_POLLANDCLEAR:
    case UPC_CONTROL_WAIT:
	/*
	 * If upcid is -1 poll for the first pending upcall and return the
	 * id or 0 if no upcalls are pending.
	 *
	 * If upcid is a particular upcall then poll that upcall and return
	 * its pending status (0 or 1).  For POLLANDCLEAR, also clear the
	 * pending status.  The userland pending bit is not modified by
	 * this call (maybe we should modify it for poll-and-clear).
	 */
	error = (uap->upcid == -1) ? 0 : ENOENT;
	for (vu = vms->vm_upcalls; vu; vu = vu->vu_next) {
	    if (vu->vu_id == uap->upcid || 
		(uap->upcid == -1 &&
		 vu->vu_pending >= (int)(intptr_t)uap->data && vu->vu_lwp == lp)
	    ) {
		error = 0;
		if (uap->upcid == -1)
		    uap->sysmsg_result = vu->vu_id;
		else
		    uap->sysmsg_result = vu->vu_pending;
		if (uap->cmd == UPC_CONTROL_POLLANDCLEAR)
		    vu->vu_pending = 0;
		break;
	    }
	}
	if (uap->cmd == UPC_CONTROL_WAIT && vu == NULL) {
	    lp->lwp_proc->p_flag |= P_UPCALLWAIT;	/* XXX lwp flags */
	    tsleep(&lp->lwp_upcall, PCATCH, "wupcall", 0);
	    lp->lwp_proc->p_flag &= ~P_UPCALLWAIT;	/* XXX lwp flags */
	}
	break;
    default:
	error = EINVAL;
	break;
    }
    rel_mplock();
    return(error);
}

void
upc_release(struct vmspace *vm, struct lwp *lp)
{
    struct vmupcall **vupp;
    struct vmupcall *vu;

    vupp = &vm->vm_upcalls;
    while ((vu = *vupp) != NULL) {
	if (vu->vu_lwp == lp) {
	    *vupp = vu->vu_next;
	    kfree(vu, M_UPCALL);
	    --vm->vm_upccount;
	} else {
	    vupp = &vu->vu_next;
	}
    }
}

/*
 * XXX eventually we should sort by vu_pending priority and dispatch
 * the highest priority upcall first.
 */
void
postupcall(struct lwp *lp)
{
    struct vmspace *vm = lp->lwp_proc->p_vmspace;
    struct vmupcall *vu;
    struct vmupcall *vu_send = NULL;

    for (vu = vm->vm_upcalls; vu; vu = vu->vu_next) {
	if (vu->vu_lwp == lp && vu->vu_pending) {
	    if (vu_send) {
		sendupcall(vu, 1);
		return;
	    }
	    vu_send = vu;
	}
    }
    if (vu_send)
	sendupcall(vu_send, 0);
}

