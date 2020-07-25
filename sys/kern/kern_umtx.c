/*
 * Copyright (c) 2003,2004,2010,2017 The DragonFly Project.
 * All rights reserved.
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
#include <sys/cdefs.h>
#include <sys/kernel.h>
#include <sys/sysmsg.h>
#include <sys/sysent.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/thread.h>
#include <sys/proc.h>

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

/*
 * Improve umtx performance by polling for 4000nS before going to sleep.
 * This can avoid many IPIs in typical pthreads mutex situations.
 */
#ifdef _RDTSC_SUPPORTED_
static int umtx_delay = 4000;		/* nS */
SYSCTL_INT(_kern, OID_AUTO, umtx_delay, CTLFLAG_RW,
	   &umtx_delay, 0, "");
#endif
static int umtx_timeout_max = 2000000;	/* microseconds */
SYSCTL_INT(_kern, OID_AUTO, umtx_timeout_max, CTLFLAG_RW,
	   &umtx_timeout_max, 0, "");

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
 * For performance reasons, we do not try to track the underlying page for
 * mapping changes.  Instead, the timeout is capped at kern.umtx_timeout_max
 * (default 1 second) and the caller is expected to retry.  The kernel
 * will wake all umtx_sleep()s if the process fork()s, but not if it vfork()s.
 * Other mapping changes must be caught by the timeout.
 *
 * umtx_sleep { const int *ptr, int value, int timeout }
 */
int
sys_umtx_sleep(struct sysmsg *sysmsg, const struct umtx_sleep_args *uap)
{
    void *waddr;
    void *uptr;
    int offset;
    int timeout;
    int error;
    int value;
    int fail_counter;
    thread_t td;
    volatile const int *ptr = uap->ptr;

    if (uap->timeout < 0)
	return (EINVAL);
    td = curthread;

    if (td->td_vmm) {
	register_t gpa;
	vmm_vm_get_gpa(td->td_proc, &gpa, (register_t)ptr);
	ptr = (const int *)gpa;
    }

    uptr = __DEQUALIFY(void *, ptr);
    if ((vm_offset_t)uptr & (sizeof(int) - 1))
	return EFAULT;

    offset = (vm_offset_t)uptr & PAGE_MASK;

    /*
     * Resolve the physical address.  We allow the case where there are
     * sometimes discontinuities (causing a 2 second retry timeout).
     */
retry_on_discontinuity:
    fail_counter = 10000;
    do {
	if (--fail_counter == 0) {
		kprintf("umtx_sleep() (X): ERROR Discontinuity %p (%s %d/%d)\n",
			uptr, td->td_comm,
			(int)td->td_proc->p_pid,
			(int)td->td_lwp->lwp_tid);
		return EINVAL;
	}
	value = fuwordadd32(uptr, 0);
	waddr = (void *)(intptr_t)uservtophys((intptr_t)uptr);
    } while (waddr == (void *)(intptr_t)-1 && value != -1);

    if (value == -1 && waddr == (void *)(intptr_t)-1) {
	kprintf("umtx_sleep() (A): WARNING can't translate %p (%s %d/%d)\n",
		uptr, td->td_comm,
		(int)td->td_proc->p_pid,
		(int)td->td_lwp->lwp_tid);
	return EINVAL;
    }

    error = EBUSY;
    if (value == uap->value) {
#ifdef _RDTSC_SUPPORTED_
	/*
	 * Poll a little while before sleeping, most mutexes are
	 * short-lived.
	 */
	if (umtx_delay) {
		int64_t tsc_target;
		int good = 0;

		tsc_target = tsc_get_target(umtx_delay);
		while (tsc_test_target(tsc_target) == 0) {
			cpu_lfence();
			if (fuwordadd32(uptr, 0) != uap->value) {
				good = 1;
				break;
			}
			cpu_pause();
		}
		if (good) {
			error = EBUSY;
			goto done;
		}
	}
#endif
	/*
	 * Calculate the timeout.  This will be acccurate to within ~2 ticks.
	 * uap->timeout is in microseconds.
	 */
	timeout = umtx_timeout_max;
	if (uap->timeout && uap->timeout < timeout)
		timeout = uap->timeout;
	timeout = (timeout / 1000000) * hz +
		  ((timeout % 1000000) * hz + 999999) / 1000000;

	/*
	 * Wake us up if the memory location COWs while we are sleeping.
	 * Use a critical section to tighten up the interlock.  Also,
	 * tsleep_remove() requires the caller be in a critical section.
	 */
	crit_enter();

	/*
	 * We must interlock just before sleeping.  If we interlock before
	 * registration the lock operations done by the registration can
	 * interfere with it.
	 *
	 * We cannot leave our interlock hanging on return because this
	 * will interfere with umtx_wakeup() calls with limited wakeup
	 * counts.
	 */
	tsleep_interlock(waddr, PCATCH | PDOMAIN_UMTX);

	/*
	 * Check physical address changed
	 */
	cpu_lfence();
	if ((void *)(intptr_t)uservtophys((intptr_t)uptr) != waddr) {
		crit_exit();
		goto retry_on_discontinuity;
	}

	/*
	 * Re-read value
	 */
	value = fuwordadd32(uptr, 0);

	if (value == uap->value) {
		error = tsleep(waddr, PCATCH | PINTERLOCKED | PDOMAIN_UMTX,
			       "umtxsl", timeout);
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
done:
    return(error);
}

/*
 * umtx_wakeup { const int *ptr, int count }
 *
 * Wakeup the specified number of processes held in umtx_sleep() on the
 * specified user address.  A count of 0 wakes up all waiting processes.
 */
int
sys_umtx_wakeup(struct sysmsg *sysmsg, const struct umtx_wakeup_args *uap)
{
    int offset;
    int error;
    int fail_counter;
    int32_t value;
    void *waddr;
    void *uptr;
    volatile const int *ptr = uap->ptr;
    thread_t td;

    td = curthread;

    if (td->td_vmm) {
	register_t gpa;
	vmm_vm_get_gpa(td->td_proc, &gpa, (register_t)ptr);
	ptr = (const int *)gpa;
    }

    /*
     * WARNING! We can only use vm_fault_page*() for reading data.  We
     *		cannot use it for writing data because there is no pmap
     *	        interlock to protect against flushes/pageouts.
     */
    cpu_mfence();
    if ((vm_offset_t)ptr & (sizeof(int) - 1))
	return EFAULT;

    offset = (vm_offset_t)ptr & PAGE_MASK;
    uptr = __DEQUALIFY(void *, ptr);

    fail_counter = 10000;
    do {
	if (--fail_counter == 0) {
		kprintf("umtx_wakeup() (X): ERROR Discontinuity "
			"%p (%s %d/%d)\n",
			uptr, td->td_comm,
			(int)td->td_proc->p_pid,
			(int)td->td_lwp->lwp_tid);
		return EINVAL;
	}
	value = fuwordadd32(uptr, 0);
	waddr = (void *)(intptr_t)uservtophys((intptr_t)uptr);
    } while (waddr == (void *)(intptr_t)-1 && value != -1);

    if (value == -1 && waddr == (void *)(intptr_t)-1) {
	kprintf("umtx_wakeup() (A): WARNING can't translate %p (%s %d/%d)\n",
		uptr, td->td_comm,
		(int)td->td_proc->p_pid,
		(int)td->td_lwp->lwp_tid);
	return EINVAL;
    }

    if (uap->count == 1) {
	wakeup_domain_one(waddr, PDOMAIN_UMTX);
    } else {
	/* XXX wakes them all up for now */
	wakeup_domain(waddr, PDOMAIN_UMTX);
    }
    error = 0;

    return(error);
}
