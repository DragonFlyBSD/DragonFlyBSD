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

/*
 *	Functions implemented as macros
 */

static __inline void
vm_page_flag_set(vm_page_t m, unsigned int bits)
{
	atomic_set_int(&(m)->flags, bits);
}

static __inline void
vm_page_flag_clear(vm_page_t m, unsigned int bits)
{
	atomic_clear_int(&(m)->flags, bits);
}

/*
 * Wakeup anyone waiting for the page after potentially unbusying
 * (hard or soft) or doing other work on a page that might make a
 * waiter ready.  The setting of PG_WANTED is integrated into the
 * related flags and it can't be set once the flags are already
 * clear, so there should be no races here.
 */

static __inline void
vm_page_flash(vm_page_t m)
{
	if (m->flags & PG_WANTED) {
		vm_page_flag_clear(m, PG_WANTED);
		wakeup(m);
	}
}

/*
 * Reduce the protection of a page.  This routine never raises the
 * protection and therefore can be safely called if the page is already
 * at VM_PROT_NONE (it will be a NOP effectively ).
 *
 * VM_PROT_NONE will remove all user mappings of a page.  This is often
 * necessary when a page changes state (for example, turns into a copy-on-write
 * page or needs to be frozen for write I/O) in order to force a fault, or
 * to force a page's dirty bits to be synchronized and avoid hardware
 * (modified/accessed) bit update races with pmap changes.
 *
 * Since 'prot' is usually a constant, this inline usually winds up optimizing
 * out the primary conditional.
 *
 * WARNING: VM_PROT_NONE can block, but will loop until all mappings have
 * been cleared.  Callers should be aware that other page related elements
 * might have changed, however.
 */
static __inline void
vm_page_protect(vm_page_t m, int prot)
{
	KKASSERT(m->flags & PG_BUSY);
	if (prot == VM_PROT_NONE) {
		if (m->flags & (PG_WRITEABLE|PG_MAPPED)) {
			pmap_page_protect(m, VM_PROT_NONE);
			/* PG_WRITEABLE & PG_MAPPED cleared by call */
		}
	} else if ((prot == VM_PROT_READ) && (m->flags & PG_WRITEABLE)) {
		pmap_page_protect(m, VM_PROT_READ);
		/* PG_WRITEABLE cleared by call */
	}
}

/*
 * Zero-fill the specified page.  The entire contents of the page will be
 * zero'd out.
 */
static __inline boolean_t
vm_page_zero_fill(vm_page_t m)
{
	pmap_zero_page(VM_PAGE_TO_PHYS(m));
	return (TRUE);
}

/*
 * Copy the contents of src_m to dest_m.  The pages must be stable but spl
 * and other protections depend on context.
 */
static __inline void
vm_page_copy(vm_page_t src_m, vm_page_t dest_m)
{
	pmap_copy_page(VM_PAGE_TO_PHYS(src_m), VM_PAGE_TO_PHYS(dest_m));
	dest_m->valid = VM_PAGE_BITS_ALL;
	dest_m->dirty = VM_PAGE_BITS_ALL;
}

/*
 * Free a page.  The page must be marked BUSY.
 *
 * Always clear PG_ZERO when freeing a page, which ensures the flag is not
 * set unless we are absolutely certain the page is zerod.  This is
 * particularly important when the vm_page_alloc*() code moves pages from
 * PQ_CACHE to PQ_FREE.
 */
static __inline void
vm_page_free(vm_page_t m)
{
	vm_page_flag_clear(m, PG_ZERO);
	vm_page_free_toq(m);
}

/*
 * Free a page to the zerod-pages queue.  The caller must ensure that the
 * page has been zerod.
 */
static __inline void
vm_page_free_zero(vm_page_t m)
{
#ifdef PMAP_DEBUG
#ifdef PHYS_TO_DMAP
	char *p = (char *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
	int i;

	for (i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != 0) {
			panic("non-zero page in vm_page_free_zero()");
		}
	}
#endif
#endif
	vm_page_flag_set(m, PG_ZERO);
	vm_page_free_toq(m);
}

/*
 * Set page to not be dirty.  Note: does not clear pmap modify bits .
 */
static __inline void
vm_page_undirty(vm_page_t m)
{
	m->dirty = 0;
}

#endif	/* _KERNEL */
#endif	/* _VM_VM_PAGE2_H_ */

