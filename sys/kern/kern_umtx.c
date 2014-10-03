/*
 * (MPSAFE)
 *
 * Copyright (c) 2003,2004,2010 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and David Xu <davidxu@freebsd.org>
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
 * This module implements userland mutex helper functions.  umtx_sleep()
 * handling blocking and umtx_wakeup() handles wakeups.  The sleep/wakeup
 * functions operate on user addresses.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/sysunion.h>
#include <sys/sysent.h>
#include <sys/syscall.h>
#include <sys/module.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_pageout.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#include <vm/vm_page2.h>

#include <machine/vmm.h>

static void umtx_sleep_page_action_cow(vm_page_t m, vm_page_action_t action);

/*
 * If the contents of the userland-supplied pointer matches the specified
 * value enter an interruptable sleep for up to <timeout> microseconds.
 * If the contents does not match then return immediately.
 *
 * Returns 0 if we slept and were woken up, -1 and EWOULDBLOCK if we slept
 * and timed out, and EBUSY if the contents of the pointer already does
 * not match the specified value.  A timeout of 0 indicates an unlimited sleep.
 * EINTR is returned if the call was interrupted by a signal (even if
 * the signal specifies that the system call should restart).
 *
 * This function interlocks against call to umtx_wakeup.  It does NOT interlock
 * against changes in *ptr.  However, it does not have to.  The standard use
 * of *ptr is to differentiate between an uncontested and a contested mutex
 * and call umtx_wakeup when releasing a contested mutex.  Therefore we can
 * safely race against changes in *ptr as long as we are properly interlocked
 * against the umtx_wakeup() call.
 *
 * The VM page associated with the mutex is held in an attempt to keep
 * the mutex's physical address consistent, allowing umtx_sleep() and
 * umtx_wakeup() to use the physical address as their rendezvous.  BUT
 * situations can arise where the physical address may change, particularly
 * if a threaded program fork()'s and the mutex's memory becomes
 * copy-on-write.  We register an event on the VM page to catch COWs.
 *
 * umtx_sleep { const int *ptr, int value, int timeout }
 */
int
sys_umtx_sleep(struct umtx_sleep_args *uap)
{
    struct lwbuf lwb_cache;
    struct lwbuf *lwb;
    struct vm_page_action action;
    vm_page_t m;
    void *waddr;
    int offset;
    int timeout;
    int error = EBUSY;

    if (uap->timeout < 0)
	return (EINVAL);

    if (curthread->td_vmm) {
	register_t gpa;
	vmm_vm_get_gpa(curproc, &gpa, (register_t) uap->ptr);
	uap->ptr = (const int *)gpa;
    }

    if ((vm_offset_t)uap->ptr & (sizeof(int) - 1))
	return (EFAULT);

    /*
     * When faulting in the page, force any COW pages to be resolved.
     * Otherwise the physical page we sleep on my not match the page
     * being woken up.
     */
    m = vm_fault_page_quick((vm_offset_t)uap->ptr,
			    VM_PROT_READ|VM_PROT_WRITE, &error);
    if (m == NULL) {
	error = EFAULT;
	goto done;
    }
    lwb = lwbuf_alloc(m, &lwb_cache);
    offset = (vm_offset_t)uap->ptr & PAGE_MASK;

    /*
     * The critical section is required to interlock the tsleep against
     * a wakeup from another cpu.  The lfence forces synchronization.
     */
    if (*(int *)(lwbuf_kva(lwb) + offset) == uap->value) {
	if ((timeout = uap->timeout) != 0) {
	    timeout = (timeout / 1000000) * hz +
		      ((timeout % 1000000) * hz + 999999) / 1000000;
	}
	waddr = (void *)((intptr_t)VM_PAGE_TO_PHYS(m) + offset);
	crit_enter();
	tsleep_interlock(waddr, PCATCH | PDOMAIN_UMTX);
	if (*(int *)(lwbuf_kva(lwb) + offset) == uap->value) {
	    vm_page_init_action(m, &action, umtx_sleep_page_action_cow, waddr);
	    vm_page_register_action(&action, VMEVENT_COW);
	    if (*(int *)(lwbuf_kva(lwb) + offset) == uap->value) {
		    error = tsleep(waddr, PCATCH | PINTERLOCKED | PDOMAIN_UMTX,
				   "umtxsl", timeout);
	    } else {
		    error = EBUSY;
	    }
	    vm_page_unregister_action(&action);
	} else {
	    error = EBUSY;
	}
	crit_exit();
	/* Always break out in case of signal, even if restartable */
	if (error == ERESTART)
		error = EINTR;
    } else {
	error = EBUSY;
    }

    lwbuf_free(lwb);
    /*vm_page_dirty(m); we don't actually dirty the page */
    vm_page_unhold(m);
done:
    return(error);
}

/*
 * If this page is being copied it may no longer represent the page
 * underlying our virtual address.  Wake up any umtx_sleep()'s
 * that were waiting on its physical address to force them to retry.
 */
static void
umtx_sleep_page_action_cow(vm_page_t m, vm_page_action_t action)
{
    wakeup_domain(action->data, PDOMAIN_UMTX);
}

/*
 * umtx_wakeup { const int *ptr, int count }
 *
 * Wakeup the specified number of processes held in umtx_sleep() on the
 * specified user address.  A count of 0 wakes up all waiting processes.
 *
 * XXX assumes that the physical address space does not exceed the virtual
 * address space.
 */
int
sys_umtx_wakeup(struct umtx_wakeup_args *uap)
{
    vm_page_t m;
    int offset;
    int error;
    void *waddr;

    if (curthread->td_vmm) {
	register_t gpa;
	vmm_vm_get_gpa(curproc, &gpa, (register_t) uap->ptr);
	uap->ptr = (const int *)gpa;
    }

    cpu_mfence();
    if ((vm_offset_t)uap->ptr & (sizeof(int) - 1))
	return (EFAULT);
    m = vm_fault_page_quick((vm_offset_t)uap->ptr, VM_PROT_READ, &error);
    if (m == NULL) {
	error = EFAULT;
	goto done;
    }
    offset = (vm_offset_t)uap->ptr & PAGE_MASK;
    waddr = (void *)((intptr_t)VM_PAGE_TO_PHYS(m) + offset);

    if (uap->count == 1) {
	wakeup_domain_one(waddr, PDOMAIN_UMTX);
    } else {
	/* XXX wakes them all up for now */
	wakeup_domain(waddr, PDOMAIN_UMTX);
    }
    vm_page_unhold(m);
    error = 0;
done:
    return(error);
}
