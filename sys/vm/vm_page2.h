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
 *	@(#)vmmeter.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD: src/sys/sys/vmmeter.h,v 1.21.2.2 2002/10/10 19:28:21 dillon Exp $
 */

#ifndef _VM_VM_PAGE2_H_
#define _VM_VM_PAGE2_H_

#ifdef _KERNEL

#ifndef _SYS_VMMETER_H_
#include <sys/vmmeter.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_SPINLOCK2_H_
#include <sys/spinlock2.h>
#endif

/*
 * SMP NOTE
 *
 * VM fault rates are highly dependent on SMP locking conflicts and, on
 * multi-socket systems, cache mastership changes for globals due to atomic
 * ops (even simple atomic_add_*() calls).  Cache mastership changes can
 * limit the aggregate fault rate.
 *
 * For this reason we go through some hoops to access VM statistics for
 * low-memory handling, pageout, and other triggers.  Each cpu collects
 * adjustments in gd->gd_vmstats_adj.  These get rolled up into the global
 * vmstats structure.  The global vmstats structure is then pulled into
 * gd->gd_vmstats by each cpu when it needs it.  Critical path checks always
 * use the pcpu gd->gd_vmstats structure.
 */
/*
 * Return TRUE if we are under our severe low-free-pages threshold
 *
 * This causes user processes to stall to avoid exhausting memory that
 * the kernel might need.
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline 
int
vm_paging_severe(void)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_free_severe >
			    gd->gd_vmstats.v_free_count +
			    gd->gd_vmstats.v_cache_count))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

/*
 * Return TRUE if we are under our minimum low-free-pages threshold.  We
 * will not count (donotcount) free pages as being free (used mainly for
 * hystersis tests).
 *
 * This will cause most normal page faults to block and activate the
 * pageout daemon.
 *
 * The pageout daemon should already be active due to vm_paging_start(n)
 * and will typically continue running until it hits target2
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline 
int
vm_paging_min_dnc(long donotcount)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_free_min + donotcount >
			    (gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count)))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

/*
 * Returns TRUE if the number of FREE+CACHE pages falls below vm_paging_wait,
 * based on the nice value the trip point can be between vm_paging_min and
 * vm_paging_wait.
 *
 * Used by vm_fault (see vm_wait_pfault()) to block a process on low-memory
 * based on the process 'nice' value (-20 to +20).
 */
static __inline
int
vm_paging_min_nice(int nice)
{
	long count;
	long delta;

	count = 0;
	if (nice) {
		delta = vmstats.v_paging_wait - vmstats.v_free_min - 1;
		delta = delta >> 1;
		if (delta > 0) {
			/* range 0-40, 0 is high priority, 40 is low */
			count = (nice + 20) * delta / 40;
		}
	}
	return vm_paging_min_dnc(count);
}

static __inline
int
vm_paging_min(void)
{
	return vm_paging_min_dnc(0);
}

/*
 * Return TRUE if nominal userland / VM-system allocations should slow
 * down (but not stop) due to low free pages in the system.  This is
 * typically 1/2 way between min and start.
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline
int
vm_paging_wait(void)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_paging_wait >
			    (gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count)))
        {
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

/*
 * Return TRUE if the pageout daemon should be started up or continue
 * running.  Available pages have dropped to a level where we need to
 * think about freeing some up.
 *
 * Also handles edge cases for required 'actually-free' pages.
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline
int
vm_paging_start(int adj)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_paging_start >
			    (gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count + adj)))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_min >
			    gd->gd_vmstats.v_free_count + adj))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

/*
 * Return TRUE if the pageout daemon has not yet reached its initial target.
 * The pageout daemon works hard to reach target1.
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline
int
vm_paging_target1(void)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_paging_target1 >
			    (gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count)))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

static __inline
long
vm_paging_target1_count(void)
{
	globaldata_t gd = mycpu;
	long delta;

	delta = gd->gd_vmstats.v_paging_target1 -
		(gd->gd_vmstats.v_free_count + gd->gd_vmstats.v_cache_count);
	return delta;
}

/*
 * Return TRUE if the pageout daemon has not yet reached its final target.
 * The pageout daemon takes it easy on its way between target1 and target2.
 *
 * reserved < severe < minimum < wait < start < target1 < target2
 */
static __inline
int
vm_paging_target2(void)
{
	globaldata_t gd = mycpu;

	if (__predict_false(gd->gd_vmstats.v_paging_target2 >
			    (gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count)))
	{
		return 1;
	}
	if (__predict_false(gd->gd_vmstats.v_free_reserved >
			    gd->gd_vmstats.v_free_count))
	{
		return 1;
	}
	return 0;
}

static __inline
long
vm_paging_target2_count(void)
{
	globaldata_t gd = mycpu;
	long delta;

	delta = gd->gd_vmstats.v_paging_target2 -
		(gd->gd_vmstats.v_free_count + gd->gd_vmstats.v_cache_count);
	return delta;
}

/*
 * Returns TRUE if additional pages must be deactivated, either during a
 * pageout operation or during the page stats scan.
 *
 * Inactive tests are used in two places.  During heavy paging the
 * inactive_target is used to refill the inactive queue in staged.
 * Those pages are then ultimately flushed and moved to the cache or free
 * queues.
 *
 * The inactive queue is also used to manage scans to update page stats
 * (m->act_count).  The page stats scan occurs lazily in small batches to
 * update m->act_count for pages in the active queue and to move pages
 * (limited by inactive_target) to the inactive queue.  Page stats scanning
 * and active deactivations only run while the inactive queue is below target.
 * After this, additional page stats scanning just to update m->act_count
 * (but not do further deactivations) continues to run for a limited period
 * of time after any pageout daemon activity.
 */
static __inline
int
vm_paging_inactive(void)
{
	globaldata_t gd = mycpu;

	if (__predict_false((gd->gd_vmstats.v_free_count +
			     gd->gd_vmstats.v_cache_count +
			     gd->gd_vmstats.v_inactive_count) <
			    (gd->gd_vmstats.v_free_min +
			     gd->gd_vmstats.v_inactive_target)))
	{
		return 1;
	}
	return 0;
}

/*
 * Return number of pages that need to be deactivated to achieve the inactive
 * target as a positive number.  A negative number indicates that there are
 * already a sufficient number of inactive pages.
 */
static __inline
long
vm_paging_inactive_count(void)
{
	globaldata_t gd = mycpu;
	long delta;

	delta = (gd->gd_vmstats.v_free_min + gd->gd_vmstats.v_inactive_target) -
		(gd->gd_vmstats.v_free_count + gd->gd_vmstats.v_cache_count +
		 gd->gd_vmstats.v_inactive_count);

	return delta;
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
    spin_lock(&m->spin);
}

static __inline
void
vm_page_spin_unlock(vm_page_t m)
{
    spin_unlock(&m->spin);
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
 * waiter ready.  The setting of PBUSY_WANTED is integrated into the
 * related flags and it can't be set once the flags are already
 * clear, so there should be no races here.
 */
static __inline void
vm_page_flash(vm_page_t m)
{
	if (m->busy_count & PBUSY_WANTED) {
		atomic_clear_int(&m->busy_count, PBUSY_WANTED);
		wakeup(m);
	}
}

/*
 * Adjust the soft-busy count on a page.  The drop code will issue an
 * integrated wakeup if busy_count becomes 0.
 */
static __inline void
vm_page_sbusy_hold(vm_page_t m)
{
	atomic_add_int(&m->busy_count, 1);
}

static __inline void
vm_page_sbusy_drop(vm_page_t m)
{
	uint32_t ocount;

	ocount = atomic_fetchadd_int(&m->busy_count, -1);
	if (ocount - 1 == PBUSY_WANTED) {
		/* WANTED and no longer BUSY or SBUSY */
		atomic_clear_int(&m->busy_count, PBUSY_WANTED);
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
 * Must be called with (m) hard-busied.
 *
 * WARNING: VM_PROT_NONE can block, but will loop until all mappings have
 *	    been cleared.  Callers should be aware that other page related
 *	    elements might have changed, however.
 */
static __inline void
vm_page_protect(vm_page_t m, int prot)
{
	KKASSERT(m->busy_count & PBUSY_LOCKED);
	if (prot == VM_PROT_NONE) {
		if (pmap_mapped_sync(m) & (PG_MAPPED | PG_WRITEABLE)) {
			pmap_page_protect(m, VM_PROT_NONE);
			/* PG_WRITEABLE & PG_MAPPED cleared by call */
		}
	} else if ((prot == VM_PROT_READ) &&
		   (m->flags & PG_WRITEABLE) &&
		   (pmap_mapped_sync(m) & PG_WRITEABLE)) {
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
 */
static __inline void
vm_page_free(vm_page_t m)
{
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

