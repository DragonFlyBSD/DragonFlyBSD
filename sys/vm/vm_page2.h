/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)vmmeter.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD: src/sys/sys/vmmeter.h,v 1.21.2.2 2002/10/10 19:28:21 dillon Exp $
 * $DragonFly: src/sys/vm/vm_page2.h,v 1.3 2008/04/14 20:00:29 dillon Exp $
 */

#ifndef _VM_VM_PAGE2_H_
#define _VM_VM_PAGE2_H_

#ifndef _SYS_VMMETER_H_
#include <sys/vmmeter.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _VM_PAGE_H_
#include <vm/vm_page.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_SPINLOCK2_H_
#include <sys/spinlock2.h>
#endif

#ifdef _KERNEL

/*
 * Return TRUE if we are under our severe low-free-pages threshold
 *
 * This causes user processes to stall to avoid exhausting memory that
 * the kernel might need.
 *
 * reserved < severe < minimum < target < paging_target
 */
static __inline 
int
vm_page_count_severe(void)
{
    return (vmstats.v_free_severe >
	    vmstats.v_free_count + vmstats.v_cache_count ||
	    vmstats.v_free_reserved > vmstats.v_free_count);
}

/*
 * Return TRUE if we are under our minimum low-free-pages threshold.
 * This activates the pageout demon.  The pageout demon tries to
 * reach the target but may stop once it satisfies the minimum.
 *
 * reserved < severe < minimum < target < paging_target
 */
static __inline 
int
vm_page_count_min(int donotcount)
{
    return (vmstats.v_free_min + donotcount >
	    (vmstats.v_free_count + vmstats.v_cache_count) ||
	    vmstats.v_free_reserved > vmstats.v_free_count);
}

/*
 * Return TRUE if we are under our free page target.  The pageout demon
 * tries to reach the target but may stop once it gets past the min.
 *
 * User threads doing normal allocations might wait based on this
 * function but MUST NOT wait in a loop based on this function as the
 * VM load may prevent the target from being reached.
 */
static __inline 
int
vm_page_count_target(void)
{
    return (vmstats.v_free_target >
	    (vmstats.v_free_count + vmstats.v_cache_count) ||
	    vmstats.v_free_reserved > vmstats.v_free_count);
}

/*
 * Return the number of pages the pageout daemon needs to move into the
 * cache or free lists.  A negative number means we have sufficient free
 * pages.
 *
 * The target free+cache is greater than vm_page_count_target().  The
 * frontend uses vm_page_count_target() while the backend continue freeing
 * based on vm_paging_target().
 *
 * This function DOES NOT return TRUE or FALSE.
 */
static __inline 
int
vm_paging_target(void)
{
    return (
	(vmstats.v_free_target + vmstats.v_cache_min) - 
	(vmstats.v_free_count + vmstats.v_cache_count)
    );
}

/*
 * Return TRUE if hysteresis dictates we should nominally wakeup the
 * pageout daemon to start working on freeing up some memory.  This
 * routine should NOT be used to determine when to block on the VM system.
 * We want to wakeup the pageout daemon before we might otherwise block.
 *
 * Paging begins when cache+free drops below cache_min + free_min.
 */
static __inline 
int
vm_paging_needed(void)
{
    if (vmstats.v_free_min + vmstats.v_cache_min >
	vmstats.v_free_count + vmstats.v_cache_count) {
		return 1;
    }
    if (vmstats.v_free_min > vmstats.v_free_count)
		return 1;
    return 0;
}

static __inline
void
vm_page_event(vm_page_t m, vm_page_event_t event)
{
    if (m->flags & PG_ACTIONLIST)
	vm_page_event_internal(m, event);
}

static __inline
void
vm_page_init_action(vm_page_t m, vm_page_action_t action,
		    void (*func)(vm_page_t, vm_page_action_t), void *data)
{
    action->m = m;
    action->func = func;
    action->data = data;
}

/*
 * Clear dirty bits in the VM page but truncate the
 * end to a DEV_BSIZE'd boundary.
 *
 * Used when reading data in, typically via getpages.
 * The partial device block at the end of the truncation
 * range should not lose its dirty bit.
 *
 * NOTE: This function does not clear the pmap modified bit.
 */
static __inline
void
vm_page_clear_dirty_end_nonincl(vm_page_t m, int base, int size)
{
    size = (base + size) & ~DEV_BMASK;
    if (base < size)
	vm_page_clear_dirty(m, base, size - base);
}

/*
 * Clear dirty bits in the VM page but truncate the
 * beginning to a DEV_BSIZE'd boundary.
 *
 * Used when truncating a buffer.  The partial device
 * block at the beginning of the truncation range
 * should not lose its dirty bit.
 *
 * NOTE: This function does not clear the pmap modified bit.
 */
static __inline
void
vm_page_clear_dirty_beg_nonincl(vm_page_t m, int base, int size)
{
    size += base;
    base = (base + DEV_BMASK) & ~DEV_BMASK;
    if (base < size)
	vm_page_clear_dirty(m, base, size - base);
}

static __inline
void
vm_page_spin_lock(vm_page_t m)
{
    spin_pool_lock(m);
}

static __inline
void
vm_page_spin_unlock(vm_page_t m)
{
    spin_pool_unlock(m);
}

/*
 * Wire a vm_page that is already wired.  Does not require a busied
 * page.
 */
static __inline
void
vm_page_wire_quick(vm_page_t m)
{
    if (atomic_fetchadd_int(&m->wire_count, 1) == 0)
	panic("vm_page_wire_quick: wire_count was 0");
}

/*
 * Unwire a vm_page quickly, does not require a busied page.
 *
 * This routine refuses to drop the wire_count to 0 and will return
 * TRUE if it would have had to (instead of decrementing it to 0).
 * The caller can then busy the page and deal with it.
 */
static __inline
int
vm_page_unwire_quick(vm_page_t m)
{
    KKASSERT(m->wire_count > 0);
    for (;;) {
	u_int wire_count = m->wire_count;

	cpu_ccfence();
	if (wire_count == 1)
		return TRUE;
	if (atomic_cmpset_int(&m->wire_count, wire_count, wire_count - 1))
		return FALSE;
    }
}

#endif	/* _KERNEL */
#endif	/* _VM_VM_PAGE2_H_ */

