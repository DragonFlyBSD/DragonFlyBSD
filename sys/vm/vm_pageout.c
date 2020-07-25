/*
 * Copyright (c) 2003-2020 The DragonFly Project.  All rights reserved.
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
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
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
 *	from: @(#)vm_pageout.c	7.4 (Berkeley) 5/7/91
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
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
 */

/*
 * The proverbial page-out daemon, rewritten many times over the decades.
 */

#include "opt_vm.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/conf.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>

#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

/*
 * System initialization
 */

/* the kernel process "vm_pageout"*/
static int vm_pageout_page(vm_page_t m, long *max_launderp,
			   long *vnodes_skippedp, struct vnode **vpfailedp,
			   int pass, int vmflush_flags, long *counts);
static int vm_pageout_clean_helper (vm_page_t, int);
static void vm_pageout_free_page_calc (vm_size_t count);
static void vm_pageout_page_free(vm_page_t m) ;
__read_frequently struct thread *emergpager;
__read_frequently struct thread *pagethread;
static int sequence_emerg_pager;

#if !defined(NO_SWAPPING)
/* the kernel process "vm_daemon"*/
static void vm_daemon (void);
static struct	thread *vmthread;

static struct kproc_desc vm_kp = {
	"vmdaemon",
	vm_daemon,
	&vmthread
};
SYSINIT(vmdaemon, SI_SUB_KTHREAD_VM, SI_ORDER_FIRST, kproc_start, &vm_kp);
#endif

__read_mostly int vm_pages_needed = 0;	/* pageout daemon tsleep event */
__read_mostly int vm_pageout_deficit = 0;/* Estimated number of pages deficit */
__read_mostly int vm_pageout_pages_needed = 0;/* pageout daemon needs pages */
__read_mostly int vm_page_free_hysteresis = 16;
__read_mostly static int vm_pagedaemon_time;

#if !defined(NO_SWAPPING)
static int vm_daemon_needed;
#endif
__read_mostly static int vm_max_launder = 0;
__read_mostly static int vm_emerg_launder = 100;
__read_mostly static int vm_pageout_stats_max=0, vm_pageout_stats_interval = 0;
__read_mostly static int vm_pageout_full_stats_interval = 0;
__read_mostly static int vm_pageout_stats_free_max=0, vm_pageout_algorithm=0;
__read_mostly static int defer_swap_pageouts=0;
__read_mostly static int disable_swap_pageouts=0;
__read_mostly static u_int vm_anonmem_decline = ACT_DECLINE;
__read_mostly static u_int vm_filemem_decline = ACT_DECLINE * 2;
__read_mostly static int vm_pageout_debug;

#if defined(NO_SWAPPING)
__read_mostly static int vm_swap_enabled=0;
#else
__read_mostly static int vm_swap_enabled=1;
#endif

/* 0-disable, 1-passive, 2-active swp, 3-acive swp + single-queue dirty pages*/
__read_mostly int vm_pageout_memuse_mode=2;
__read_mostly int vm_pageout_allow_active=1;

SYSCTL_UINT(_vm, VM_PAGEOUT_ALGORITHM, anonmem_decline,
	CTLFLAG_RW, &vm_anonmem_decline, 0, "active->inactive anon memory");

SYSCTL_INT(_vm, VM_PAGEOUT_ALGORITHM, filemem_decline,
	CTLFLAG_RW, &vm_filemem_decline, 0, "active->inactive file cache");

SYSCTL_INT(_vm, OID_AUTO, page_free_hysteresis,
	CTLFLAG_RW, &vm_page_free_hysteresis, 0,
	"Free more pages than the minimum required");

SYSCTL_INT(_vm, OID_AUTO, max_launder,
	CTLFLAG_RW, &vm_max_launder, 0, "Limit dirty flushes in pageout");
SYSCTL_INT(_vm, OID_AUTO, emerg_launder,
	CTLFLAG_RW, &vm_emerg_launder, 0, "Emergency pager minimum");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_max,
	CTLFLAG_RW, &vm_pageout_stats_max, 0, "Max pageout stats scan length");

SYSCTL_INT(_vm, OID_AUTO, pageout_full_stats_interval,
	CTLFLAG_RW, &vm_pageout_full_stats_interval, 0, "Interval for full stats scan");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_interval,
	CTLFLAG_RW, &vm_pageout_stats_interval, 0, "Interval for partial stats scan");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_free_max,
	CTLFLAG_RW, &vm_pageout_stats_free_max, 0, "Not implemented");
SYSCTL_INT(_vm, OID_AUTO, pageout_memuse_mode,
	CTLFLAG_RW, &vm_pageout_memuse_mode, 0, "memoryuse resource mode");
SYSCTL_INT(_vm, OID_AUTO, pageout_allow_active,
	CTLFLAG_RW, &vm_pageout_allow_active, 0, "allow inactive+active");
SYSCTL_INT(_vm, OID_AUTO, pageout_debug,
	CTLFLAG_RW, &vm_pageout_debug, 0, "debug pageout pages (count)");


#if defined(NO_SWAPPING)
SYSCTL_INT(_vm, VM_SWAPPING_ENABLED, swap_enabled,
	CTLFLAG_RD, &vm_swap_enabled, 0, "");
#else
SYSCTL_INT(_vm, VM_SWAPPING_ENABLED, swap_enabled,
	CTLFLAG_RW, &vm_swap_enabled, 0, "Enable entire process swapout");
#endif

SYSCTL_INT(_vm, OID_AUTO, defer_swapspace_pageouts,
	CTLFLAG_RW, &defer_swap_pageouts, 0, "Give preference to dirty pages in mem");

SYSCTL_INT(_vm, OID_AUTO, disable_swapspace_pageouts,
	CTLFLAG_RW, &disable_swap_pageouts, 0, "Disallow swapout of dirty pages");

static int pageout_lock_miss;
SYSCTL_INT(_vm, OID_AUTO, pageout_lock_miss,
	CTLFLAG_RD, &pageout_lock_miss, 0, "vget() lock misses during pageout");

int vm_page_max_wired;		/* XXX max # of wired pages system-wide */

#if !defined(NO_SWAPPING)
static void vm_req_vmdaemon (void);
#endif
static void vm_pageout_page_stats(int q);

#define MAXSCAN_DIVIDER		10

/*
 * Calculate approximately how many pages on each queue to try to
 * clean.  An exact calculation creates an edge condition when the
 * queues are unbalanced so add significant slop.  The queue scans
 * will stop early when targets are reached and will start where they
 * left off on the next pass.
 *
 * We need to be generous here because there are all sorts of loading
 * conditions that can cause edge cases if try to average over all queues.
 * In particular, storage subsystems have become so fast that paging
 * activity can become quite frantic.  Eventually we will probably need
 * two paging threads, one for dirty pages and one for clean, to deal
 * with the bandwidth requirements.

 * So what we do is calculate a value that can be satisfied nominally by
 * only having to scan half the queues.
 */
static __inline long
PQAVERAGE(long n)
{
	long avg;

	if (n >= 0) {
		avg = ((n + (PQ_L2_SIZE - 1)) / (PQ_L2_SIZE / 2) + 1);
	} else {
		avg = ((n - (PQ_L2_SIZE - 1)) / (PQ_L2_SIZE / 2) - 1);
	}
	return avg;
}

/*
 * vm_pageout_clean_helper:
 *
 * Clean the page and remove it from the laundry.  The page must be busied
 * by the caller and will be disposed of (put away, flushed) by this routine.
 */
static int
vm_pageout_clean_helper(vm_page_t m, int vmflush_flags)
{
	vm_object_t object;
	vm_page_t mc[BLIST_MAX_ALLOC];
	int error;
	int ib, is, page_base;
	vm_pindex_t pindex = m->pindex;

	object = m->object;

	/*
	 * Don't mess with the page if it's held or special.  Theoretically
	 * we can pageout held pages but there is no real need to press our
	 * luck, so don't.
	 */
	if (m->hold_count != 0 || (m->flags & PG_UNQUEUED)) {
		vm_page_wakeup(m);
		return 0;
	}

	/*
	 * Place page in cluster.  Align cluster for optimal swap space
	 * allocation (whether it is swap or not).  This is typically ~16-32
	 * pages, which also tends to align the cluster to multiples of the
	 * filesystem block size if backed by a filesystem.
	 */
	page_base = pindex % BLIST_MAX_ALLOC;
	mc[page_base] = m;
	ib = page_base - 1;
	is = page_base + 1;

	/*
	 * Scan object for clusterable pages.
	 *
	 * We can cluster ONLY if: ->> the page is NOT
	 * clean, wired, busy, held, or mapped into a
	 * buffer, and one of the following:
	 * 1) The page is inactive, or a seldom used
	 *    active page.
	 * -or-
	 * 2) we force the issue.
	 *
	 * During heavy mmap/modification loads the pageout
	 * daemon can really fragment the underlying file
	 * due to flushing pages out of order and not trying
	 * align the clusters (which leave sporatic out-of-order
	 * holes).  To solve this problem we do the reverse scan
	 * first and attempt to align our cluster, then do a 
	 * forward scan if room remains.
	 */
	vm_object_hold(object);

	while (ib >= 0) {
		vm_page_t p;

		p = vm_page_lookup_busy_try(object, pindex - page_base + ib,
					    TRUE, &error);
		if (error || p == NULL)
			break;
		if ((p->queue - p->pc) == PQ_CACHE ||
		    (p->flags & PG_UNQUEUED)) {
			vm_page_wakeup(p);
			break;
		}
		vm_page_test_dirty(p);
		if (((p->dirty & p->valid) == 0 &&
		     (p->flags & PG_NEED_COMMIT) == 0) ||
		    p->wire_count != 0 ||	/* may be held by buf cache */
		    p->hold_count != 0) {	/* may be undergoing I/O */
			vm_page_wakeup(p);
			break;
		}
		if (p->queue - p->pc != PQ_INACTIVE) {
			if (p->queue - p->pc != PQ_ACTIVE ||
			    (vmflush_flags & OBJPC_ALLOW_ACTIVE) == 0) {
				vm_page_wakeup(p);
				break;
			}
		}

		/*
		 * Try to maintain page groupings in the cluster.
		 */
		if (m->flags & PG_WINATCFLS)
			vm_page_flag_set(p, PG_WINATCFLS);
		else
			vm_page_flag_clear(p, PG_WINATCFLS);
		p->act_count = m->act_count;

		mc[ib] = p;
		--ib;
	}
	++ib;	/* fixup */

	while (is < BLIST_MAX_ALLOC &&
	       pindex - page_base + is < object->size) {
		vm_page_t p;

		p = vm_page_lookup_busy_try(object, pindex - page_base + is,
					    TRUE, &error);
		if (error || p == NULL)
			break;
		if (((p->queue - p->pc) == PQ_CACHE) ||
		    (p->flags & PG_UNQUEUED)) {
			vm_page_wakeup(p);
			break;
		}
		vm_page_test_dirty(p);
		if (((p->dirty & p->valid) == 0 &&
		     (p->flags & PG_NEED_COMMIT) == 0) ||
		    p->wire_count != 0 ||	/* may be held by buf cache */
		    p->hold_count != 0) {	/* may be undergoing I/O */
			vm_page_wakeup(p);
			break;
		}
		if (p->queue - p->pc != PQ_INACTIVE) {
			if (p->queue - p->pc != PQ_ACTIVE ||
			    (vmflush_flags & OBJPC_ALLOW_ACTIVE) == 0) {
				vm_page_wakeup(p);
				break;
			}
		}

		/*
		 * Try to maintain page groupings in the cluster.
		 */
		if (m->flags & PG_WINATCFLS)
			vm_page_flag_set(p, PG_WINATCFLS);
		else
			vm_page_flag_clear(p, PG_WINATCFLS);
		p->act_count = m->act_count;

		mc[is] = p;
		++is;
	}

	vm_object_drop(object);

	/*
	 * we allow reads during pageouts...
	 */
	return vm_pageout_flush(&mc[ib], is - ib, vmflush_flags);
}

/*
 * vm_pageout_flush() - launder the given pages
 *
 *	The given pages are laundered.  Note that we setup for the start of
 *	I/O ( i.e. busy the page ), mark it read-only, and bump the object
 *	reference count all in here rather then in the parent.  If we want
 *	the parent to do more sophisticated things we may have to change
 *	the ordering.
 *
 *	The pages in the array must be busied by the caller and will be
 *	unbusied by this function.
 */
int
vm_pageout_flush(vm_page_t *mc, int count, int vmflush_flags)
{
	vm_object_t object;
	int pageout_status[count];
	int numpagedout = 0;
	int i;

	/*
	 * Initiate I/O.  Bump the vm_page_t->busy counter.
	 */
	for (i = 0; i < count; i++) {
		KASSERT(mc[i]->valid == VM_PAGE_BITS_ALL,
			("vm_pageout_flush page %p index %d/%d: partially "
			 "invalid page", mc[i], i, count));
		vm_page_io_start(mc[i]);
	}

	/*
	 * We must make the pages read-only.  This will also force the
	 * modified bit in the related pmaps to be cleared.  The pager
	 * cannot clear the bit for us since the I/O completion code
	 * typically runs from an interrupt.  The act of making the page
	 * read-only handles the case for us.
	 *
	 * Then we can unbusy the pages, we still hold a reference by virtue
	 * of our soft-busy.
	 */
	for (i = 0; i < count; i++) {
		if (vmflush_flags & OBJPC_TRY_TO_CACHE)
			vm_page_protect(mc[i], VM_PROT_NONE);
		else
			vm_page_protect(mc[i], VM_PROT_READ);
		vm_page_wakeup(mc[i]);
	}

	object = mc[0]->object;
	vm_object_pip_add(object, count);

	vm_pager_put_pages(object, mc, count,
			   (vmflush_flags |
			    ((object == &kernel_object) ?
				OBJPC_SYNC : 0)),
			   pageout_status);

	for (i = 0; i < count; i++) {
		vm_page_t mt = mc[i];

		switch (pageout_status[i]) {
		case VM_PAGER_OK:
			numpagedout++;
			break;
		case VM_PAGER_PEND:
			numpagedout++;
			break;
		case VM_PAGER_BAD:
			/*
			 * Page outside of range of object. Right now we
			 * essentially lose the changes by pretending it
			 * worked.
			 */
			vm_page_busy_wait(mt, FALSE, "pgbad");
			pmap_clear_modify(mt);
			vm_page_undirty(mt);
			vm_page_wakeup(mt);
			break;
		case VM_PAGER_ERROR:
		case VM_PAGER_FAIL:
			/*
			 * A page typically cannot be paged out when we
			 * have run out of swap.  We leave the page
			 * marked inactive and will try to page it out
			 * again later.
			 *
			 * Starvation of the active page list is used to
			 * determine when the system is massively memory
			 * starved.
			 */
			break;
		case VM_PAGER_AGAIN:
			break;
		}

		/*
		 * If not PENDing this was a synchronous operation and we
		 * clean up after the I/O.  If it is PENDing the mess is
		 * cleaned up asynchronously.
		 *
		 * Also nominally act on the caller's wishes if the caller
		 * wants to try to really clean (cache or free) the page.
		 *
		 * Also nominally deactivate the page if the system is
		 * memory-stressed.
		 */
		if (pageout_status[i] != VM_PAGER_PEND) {
			vm_page_busy_wait(mt, FALSE, "pgouw");
			vm_page_io_finish(mt);
			if (vmflush_flags & OBJPC_TRY_TO_CACHE) {
				vm_page_try_to_cache(mt);
			} else if (vm_page_count_severe()) {
				vm_page_deactivate(mt);
				vm_page_wakeup(mt);
			} else {
				vm_page_wakeup(mt);
			}
			vm_object_pip_wakeup(object);
		}
	}
	return numpagedout;
}

#if !defined(NO_SWAPPING)

/*
 * Callback function, page busied for us.  We must dispose of the busy
 * condition.  Any related pmap pages may be held but will not be locked.
 */
static
int
vm_pageout_mdp_callback(struct pmap_pgscan_info *info, vm_offset_t va,
			vm_page_t p)
{
	int actcount;
	int cleanit = 0;

	/*
	 * Basic tests - There should never be a marker, and we can stop
	 *		 once the RSS is below the required level.
	 */
	KKASSERT((p->flags & PG_MARKER) == 0);
	if (pmap_resident_tlnw_count(info->pmap) <= info->limit) {
		vm_page_wakeup(p);
		return(-1);
	}

	mycpu->gd_cnt.v_pdpages++;

	if (p->wire_count || p->hold_count || (p->flags & PG_UNQUEUED)) {
		vm_page_wakeup(p);
		goto done;
	}

	++info->actioncount;

	/*
	 * Check if the page has been referened recently.  If it has,
	 * activate it and skip.
	 */
	actcount = pmap_ts_referenced(p);
	if (actcount) {
		vm_page_flag_set(p, PG_REFERENCED);
	} else if (p->flags & PG_REFERENCED) {
		actcount = 1;
	}

	if (actcount) {
		if (p->queue - p->pc != PQ_ACTIVE) {
			vm_page_and_queue_spin_lock(p);
			if (p->queue - p->pc != PQ_ACTIVE) {
				vm_page_and_queue_spin_unlock(p);
				vm_page_activate(p);
			} else {
				vm_page_and_queue_spin_unlock(p);
			}
		} else {
			p->act_count += actcount;
			if (p->act_count > ACT_MAX)
				p->act_count = ACT_MAX;
		}
		vm_page_flag_clear(p, PG_REFERENCED);
		vm_page_wakeup(p);
		goto done;
	}

	/*
	 * Remove the page from this particular pmap.  Once we do this, our
	 * pmap scans will not see it again (unless it gets faulted in), so
	 * we must actively dispose of or deal with the page.
	 */
	pmap_remove_specific(info->pmap, p);

	/*
	 * If the page is not mapped to another process (i.e. as would be
	 * typical if this were a shared page from a library) then deactivate
	 * the page and clean it in two passes only.
	 *
	 * If the page hasn't been referenced since the last check, remove it
	 * from the pmap.  If it is no longer mapped, deactivate it
	 * immediately, accelerating the normal decline.
	 *
	 * Once the page has been removed from the pmap the RSS code no
	 * longer tracks it so we have to make sure that it is staged for
	 * potential flush action.
	 *
	 * XXX
	 */
	if ((p->flags & PG_MAPPED) == 0 ||
	    (pmap_mapped_sync(p) & PG_MAPPED) == 0) {
		if (p->queue - p->pc == PQ_ACTIVE) {
			vm_page_deactivate(p);
		}
		if (p->queue - p->pc == PQ_INACTIVE) {
			cleanit = 1;
		}
	}

	/*
	 * Ok, try to fully clean the page and any nearby pages such that at
	 * least the requested page is freed or moved to the cache queue.
	 *
	 * We usually do this synchronously to allow us to get the page into
	 * the CACHE queue quickly, which will prevent memory exhaustion if
	 * a process with a memoryuse limit is running away.  However, the
	 * sysadmin may desire to set vm.swap_user_async which relaxes this
	 * and improves write performance.
	 */
	if (cleanit) {
		long max_launder = 0x7FFF;
		long vnodes_skipped = 0;
		long counts[4] = { 0, 0, 0, 0 };
		int vmflush_flags;
		struct vnode *vpfailed = NULL;

		info->offset = va;

		if (vm_pageout_memuse_mode >= 2) {
			vmflush_flags = OBJPC_TRY_TO_CACHE |
					OBJPC_ALLOW_ACTIVE;
			if (swap_user_async == 0)
				vmflush_flags |= OBJPC_SYNC;
			vm_page_flag_set(p, PG_WINATCFLS);
			info->cleancount +=
				vm_pageout_page(p, &max_launder,
						&vnodes_skipped,
						&vpfailed, 1, vmflush_flags,
						counts);
		} else {
			vm_page_wakeup(p);
			++info->cleancount;
		}
	} else {
		vm_page_wakeup(p);
	}

	/*
	 * Must be at end to avoid SMP races.
	 */
done:
	lwkt_user_yield();
	return 0;
}

/*
 * Deactivate some number of pages in a map due to set RLIMIT_RSS limits.
 * that is relatively difficult to do.  We try to keep track of where we
 * left off last time to reduce scan overhead.
 *
 * Called when vm_pageout_memuse_mode is >= 1.
 */
void
vm_pageout_map_deactivate_pages(vm_map_t map, vm_pindex_t limit)
{
	vm_offset_t pgout_offset;
	struct pmap_pgscan_info info;
	int retries = 3;

	pgout_offset = map->pgout_offset;
again:
#if 0
	kprintf("%016jx ", pgout_offset);
#endif
	if (pgout_offset < VM_MIN_USER_ADDRESS)
		pgout_offset = VM_MIN_USER_ADDRESS;
	if (pgout_offset >= VM_MAX_USER_ADDRESS)
		pgout_offset = 0;
	info.pmap = vm_map_pmap(map);
	info.limit = limit;
	info.beg_addr = pgout_offset;
	info.end_addr = VM_MAX_USER_ADDRESS;
	info.callback = vm_pageout_mdp_callback;
	info.cleancount = 0;
	info.actioncount = 0;
	info.busycount = 0;

	pmap_pgscan(&info);
	pgout_offset = info.offset;
#if 0
	kprintf("%016jx %08lx %08lx\n", pgout_offset,
		info.cleancount, info.actioncount);
#endif

	if (pgout_offset != VM_MAX_USER_ADDRESS &&
	    pmap_resident_tlnw_count(vm_map_pmap(map)) > limit) {
		goto again;
	} else if (retries &&
		   pmap_resident_tlnw_count(vm_map_pmap(map)) > limit) {
		--retries;
		goto again;
	}
	map->pgout_offset = pgout_offset;
}
#endif

/*
 * Called when the pageout scan wants to free a page.  We no longer
 * try to cycle the vm_object here with a reference & dealloc, which can
 * cause a non-trivial object collapse in a critical path.
 *
 * It is unclear why we cycled the ref_count in the past, perhaps to try
 * to optimize shadow chain collapses but I don't quite see why it would
 * be necessary.  An OBJ_DEAD object should terminate any and all vm_pages
 * synchronously and not have to be kicked-start.
 */
static void
vm_pageout_page_free(vm_page_t m) 
{
	vm_page_protect(m, VM_PROT_NONE);
	vm_page_free(m);
}

/*
 * vm_pageout_scan does the dirty work for the pageout daemon.
 */
struct vm_pageout_scan_info {
	struct proc *bigproc;
	vm_offset_t bigsize;
};

static int vm_pageout_scan_callback(struct proc *p, void *data);

/*
 * Scan inactive queue
 *
 * WARNING! Can be called from two pagedaemon threads simultaneously.
 */
static int
vm_pageout_scan_inactive(int pass, int q, long avail_shortage,
			 long *vnodes_skipped, long *counts)
{
	vm_page_t m;
	struct vm_page marker;
	struct vnode *vpfailed;		/* warning, allowed to be stale */
	long maxscan;
	long delta = 0;
	long max_launder;
	int isep;
	int vmflush_flags;

	isep = (curthread == emergpager);
	if ((unsigned)pass > 1000)
		pass = 1000;

	/*
	 * This routine is called for each of PQ_L2_SIZE inactive queues.
	 * We want the vm_max_launder parameter to apply to the whole
	 * queue (i.e. per-whole-queue pass, not per-sub-queue).
	 *
	 * In each successive full-pass when the page target is not met we
	 * allow the per-queue max_launder to increase up to a maximum of
	 * vm_max_launder / 16.
	 */
	if (pass)
		max_launder = (long)vm_max_launder * (pass + 1) / PQ_L2_SIZE;
	else
		max_launder = (long)vm_max_launder / PQ_L2_SIZE;
	max_launder /= MAXSCAN_DIVIDER;

	if (max_launder <= 1)
		max_launder = 1;
	if (max_launder >= vm_max_launder / 16)
		max_launder = vm_max_launder / 16 + 1;

	/*
	 * Start scanning the inactive queue for pages we can move to the
	 * cache or free.  The scan will stop when the target is reached or
	 * we have scanned the entire inactive queue.  Note that m->act_count
	 * is not used to form decisions for the inactive queue, only for the
	 * active queue.
	 *
	 * NOTE!  THE EMERGENCY PAGER (isep) DOES NOT LAUNDER VNODE-BACKED
	 *	  PAGES.
	 */

	/*
	 * Initialize our marker
	 */
	bzero(&marker, sizeof(marker));
	marker.flags = PG_FICTITIOUS | PG_MARKER;
	marker.busy_count = PBUSY_LOCKED;
	marker.queue = PQ_INACTIVE + q;
	marker.pc = q;
	marker.wire_count = 1;

	/*
	 * Inactive queue scan.
	 *
	 * We pick off approximately 1/10 of each queue.  Each queue is
	 * effectively organized LRU so scanning the entire queue would
	 * improperly pick up pages that might still be in regular use.
	 *
	 * NOTE: The vm_page must be spinlocked before the queue to avoid
	 *	 deadlocks, so it is easiest to simply iterate the loop
	 *	 with the queue unlocked at the top.
	 */
	vpfailed = NULL;

	vm_page_queues_spin_lock(PQ_INACTIVE + q);
	TAILQ_INSERT_HEAD(&vm_page_queues[PQ_INACTIVE + q].pl, &marker, pageq);
	maxscan = vm_page_queues[PQ_INACTIVE + q].lcnt / MAXSCAN_DIVIDER + 1;

	/*
	 * Queue locked at top of loop to avoid stack marker issues.
	 */
	while ((m = TAILQ_NEXT(&marker, pageq)) != NULL &&
	       maxscan-- > 0 && avail_shortage - delta > 0)
	{
		int count;

		KKASSERT(m->queue == PQ_INACTIVE + q);
		TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE + q].pl,
			     &marker, pageq);
		TAILQ_INSERT_AFTER(&vm_page_queues[PQ_INACTIVE + q].pl, m,
				   &marker, pageq);
		mycpu->gd_cnt.v_pdpages++;

		/*
		 * Skip marker pages (atomic against other markers to avoid
		 * infinite hop-over scans).
		 */
		if (m->flags & PG_MARKER)
			continue;

		/*
		 * Try to busy the page.  Don't mess with pages which are
		 * already busy or reorder them in the queue.
		 */
		if (vm_page_busy_try(m, TRUE))
			continue;

		/*
		 * Remaining operations run with the page busy and neither
		 * the page or the queue will be spin-locked.
		 */
		KKASSERT(m->queue == PQ_INACTIVE + q);
		vm_page_queues_spin_unlock(PQ_INACTIVE + q);

		/*
		 * The emergency pager runs when the primary pager gets
		 * stuck, which typically means the primary pager deadlocked
		 * on a vnode-backed page.  Therefore, the emergency pager
		 * must skip any complex objects.
		 *
		 * We disallow VNODEs unless they are VCHR whos device ops
		 * does not flag D_NOEMERGPGR.
		 */
		if (isep && m->object) {
			struct vnode *vp;

			switch(m->object->type) {
			case OBJT_DEFAULT:
			case OBJT_SWAP:
				/*
				 * Allow anonymous memory and assume that
				 * swap devices are not complex, since its
				 * kinda worthless if we can't swap out dirty
				 * anonymous pages.
				 */
				break;
			case OBJT_VNODE:
				/*
				 * Allow VCHR device if the D_NOEMERGPGR
				 * flag is not set, deny other vnode types
				 * as being too complex.
				 */
				vp = m->object->handle;
				if (vp && vp->v_type == VCHR &&
				    vp->v_rdev && vp->v_rdev->si_ops &&
				    (vp->v_rdev->si_ops->head.flags &
				     D_NOEMERGPGR) == 0) {
					break;
				}
				/* Deny - fall through */
			default:
				/*
				 * Deny
				 */
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(PQ_INACTIVE + q);
				lwkt_yield();
				continue;
			}
		}

		/*
		 * Try to pageout the page and perhaps other nearby pages.
		 * We want to get the pages into the cache eventually (
		 * first or second pass).  Otherwise the pages can wind up
		 * just cycling in the inactive queue, getting flushed over
		 * and over again.
		 *
		 * Generally speaking we recycle dirty pages within PQ_INACTIVE
		 * twice (double LRU) before paging them out.  If the
		 * memuse_mode is >= 3 we run them single-LRU like we do clean
		 * pages.
		 */
		if (vm_pageout_memuse_mode >= 3)
			vm_page_flag_set(m, PG_WINATCFLS);

		vmflush_flags = 0;
		if (vm_pageout_allow_active)
			vmflush_flags |= OBJPC_ALLOW_ACTIVE;
		if (m->flags & PG_WINATCFLS)
			vmflush_flags |= OBJPC_TRY_TO_CACHE;
		count = vm_pageout_page(m, &max_launder, vnodes_skipped,
					&vpfailed, pass, vmflush_flags, counts);
		delta += count;

		/*
		 * Systems with a ton of memory can wind up with huge
		 * deactivation counts.  Because the inactive scan is
		 * doing a lot of flushing, the combination can result
		 * in excessive paging even in situations where other
		 * unrelated threads free up sufficient VM.
		 *
		 * To deal with this we abort the nominal active->inactive
		 * scan before we hit the inactive target when free+cache
		 * levels have reached a reasonable target.
		 *
		 * When deciding to stop early we need to add some slop to
		 * the test and we need to return full completion to the caller
		 * to prevent the caller from thinking there is something
		 * wrong and issuing a low-memory+swap warning or pkill.
		 *
		 * A deficit forces paging regardless of the state of the
		 * VM page queues (used for RSS enforcement).
		 */
		lwkt_yield();
		vm_page_queues_spin_lock(PQ_INACTIVE + q);
		if (vm_paging_target() < -vm_max_launder) {
			/*
			 * Stopping early, return full completion to caller.
			 */
			if (delta < avail_shortage)
				delta = avail_shortage;
			break;
		}
	}

	/* page queue still spin-locked */
	TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE + q].pl, &marker, pageq);
	vm_page_queues_spin_unlock(PQ_INACTIVE + q);

	return (delta);
}

/*
 * Pageout the specified page, return the total number of pages paged out
 * (this routine may cluster).
 *
 * The page must be busied and soft-busied by the caller and will be disposed
 * of by this function.
 */
static int
vm_pageout_page(vm_page_t m, long *max_launderp, long *vnodes_skippedp,
		struct vnode **vpfailedp, int pass, int vmflush_flags,
		long *counts)
{
	vm_object_t object;
	int actcount;
	int count = 0;

	/*
	 * Wiring no longer removes a page from its queue.  The last unwiring
	 * will requeue the page.  Obviously wired pages cannot be paged out
	 * so unqueue it and return.
	 */
	if (m->wire_count) {
		vm_page_unqueue_nowakeup(m);
		vm_page_wakeup(m);
		return 0;
	}

	/*
	 * A held page may be undergoing I/O, so skip it.
	 */
	if (m->hold_count) {
		vm_page_and_queue_spin_lock(m);
		if (m->queue - m->pc == PQ_INACTIVE) {
			TAILQ_REMOVE(
				&vm_page_queues[m->queue].pl, m, pageq);
			TAILQ_INSERT_TAIL(
				&vm_page_queues[m->queue].pl, m, pageq);
		}
		vm_page_and_queue_spin_unlock(m);
		vm_page_wakeup(m);
		return 0;
	}

	if (m->object == NULL || m->object->ref_count == 0) {
		/*
		 * If the object is not being used, we ignore previous
		 * references.
		 */
		vm_page_flag_clear(m, PG_REFERENCED);
		pmap_clear_reference(m);
		/* fall through to end */
	} else if (((m->flags & PG_REFERENCED) == 0) &&
		    (actcount = pmap_ts_referenced(m))) {
		/*
		 * Otherwise, if the page has been referenced while
		 * in the inactive queue, we bump the "activation
		 * count" upwards, making it less likely that the
		 * page will be added back to the inactive queue
		 * prematurely again.  Here we check the page tables
		 * (or emulated bits, if any), given the upper level
		 * VM system not knowing anything about existing
		 * references.
		 */
		++counts[3];
		vm_page_activate(m);
		m->act_count += (actcount + ACT_ADVANCE);
		vm_page_wakeup(m);
		return 0;
	}

	/*
	 * (m) is still busied.
	 *
	 * If the upper level VM system knows about any page
	 * references, we activate the page.  We also set the
	 * "activation count" higher than normal so that we will less
	 * likely place pages back onto the inactive queue again.
	 */
	if ((m->flags & PG_REFERENCED) != 0) {
		vm_page_flag_clear(m, PG_REFERENCED);
		actcount = pmap_ts_referenced(m);
		vm_page_activate(m);
		m->act_count += (actcount + ACT_ADVANCE + 1);
		vm_page_wakeup(m);
		++counts[3];
		return 0;
	}

	/*
	 * If the upper level VM system doesn't know anything about
	 * the page being dirty, we have to check for it again.  As
	 * far as the VM code knows, any partially dirty pages are
	 * fully dirty.
	 *
	 * Pages marked PG_WRITEABLE may be mapped into the user
	 * address space of a process running on another cpu.  A
	 * user process (without holding the MP lock) running on
	 * another cpu may be able to touch the page while we are
	 * trying to remove it.  vm_page_cache() will handle this
	 * case for us.
	 */
	if (m->dirty == 0) {
		vm_page_test_dirty(m);
	} else {
		vm_page_dirty(m);
	}

	if (m->valid == 0 && (m->flags & PG_NEED_COMMIT) == 0) {
		/*
		 * Invalid pages can be easily freed
		 */
		vm_pageout_page_free(m);
		mycpu->gd_cnt.v_dfree++;
		++count;
		++counts[1];
	} else if (m->dirty == 0 && (m->flags & PG_NEED_COMMIT) == 0) {
		/*
		 * Clean pages can be placed onto the cache queue.
		 * This effectively frees them.
		 */
		vm_page_cache(m);
		++count;
		++counts[1];
	} else if ((m->flags & PG_WINATCFLS) == 0 && pass == 0) {
		/*
		 * Dirty pages need to be paged out, but flushing
		 * a page is extremely expensive verses freeing
		 * a clean page.  Rather then artificially limiting
		 * the number of pages we can flush, we instead give
		 * dirty pages extra priority on the inactive queue
		 * by forcing them to be cycled through the queue
		 * twice before being flushed, after which the
		 * (now clean) page will cycle through once more
		 * before being freed.  This significantly extends
		 * the thrash point for a heavily loaded machine.
		 */
		++counts[2];
		vm_page_flag_set(m, PG_WINATCFLS);
		vm_page_and_queue_spin_lock(m);
		if (m->queue - m->pc == PQ_INACTIVE) {
			TAILQ_REMOVE(
				&vm_page_queues[m->queue].pl, m, pageq);
			TAILQ_INSERT_TAIL(
				&vm_page_queues[m->queue].pl, m, pageq);
		}
		vm_page_and_queue_spin_unlock(m);
		vm_page_wakeup(m);
	} else if (*max_launderp > 0) {
		/*
		 * We always want to try to flush some dirty pages if
		 * we encounter them, to keep the system stable.
		 * Normally this number is small, but under extreme
		 * pressure where there are insufficient clean pages
		 * on the inactive queue, we may have to go all out.
		 */
		int swap_pageouts_ok;
		struct vnode *vp = NULL;

		if ((m->flags & PG_WINATCFLS) == 0)
			vm_page_flag_set(m, PG_WINATCFLS);
		swap_pageouts_ok = 0;
		object = m->object;
		if (object &&
		    (object->type != OBJT_SWAP) &&
		    (object->type != OBJT_DEFAULT)) {
			swap_pageouts_ok = 1;
		} else {
			swap_pageouts_ok = !(defer_swap_pageouts ||
					     disable_swap_pageouts);
			swap_pageouts_ok |= (!disable_swap_pageouts &&
					     defer_swap_pageouts &&
					     vm_page_count_min(0));
		}

		/*
		 * We don't bother paging objects that are "dead".
		 * Those objects are in a "rundown" state.
		 */
		if (!swap_pageouts_ok ||
		    (object == NULL) ||
		    (object->flags & OBJ_DEAD)) {
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_INACTIVE) {
				TAILQ_REMOVE(
				    &vm_page_queues[m->queue].pl,
				    m, pageq);
				TAILQ_INSERT_TAIL(
				    &vm_page_queues[m->queue].pl,
				    m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
			vm_page_wakeup(m);
			return 0;
		}

		/*
		 * (m) is still busied.
		 *
		 * The object is already known NOT to be dead.   It
		 * is possible for the vget() to block the whole
		 * pageout daemon, but the new low-memory handling
		 * code should prevent it.
		 *
		 * The previous code skipped locked vnodes and, worse,
		 * reordered pages in the queue.  This results in
		 * completely non-deterministic operation because,
		 * quite often, a vm_fault has initiated an I/O and
		 * is holding a locked vnode at just the point where
		 * the pageout daemon is woken up.
		 *
		 * We can't wait forever for the vnode lock, we might
		 * deadlock due to a vn_read() getting stuck in
		 * vm_wait while holding this vnode.  We skip the
		 * vnode if we can't get it in a reasonable amount
		 * of time.
		 *
		 * vpfailed is used to (try to) avoid the case where
		 * a large number of pages are associated with a
		 * locked vnode, which could cause the pageout daemon
		 * to stall for an excessive amount of time.
		 */
		if (object->type == OBJT_VNODE) {
			int flags;

			vp = object->handle;
			flags = LK_EXCLUSIVE;
			if (vp == *vpfailedp)
				flags |= LK_NOWAIT;
			else
				flags |= LK_TIMELOCK;
			vm_page_hold(m);
			vm_page_wakeup(m);

			/*
			 * We have unbusied (m) temporarily so we can
			 * acquire the vp lock without deadlocking.
			 * (m) is held to prevent destruction.
			 */
			if (vget(vp, flags) != 0) {
				*vpfailedp = vp;
				++pageout_lock_miss;
				if (object->flags & OBJ_MIGHTBEDIRTY)
					    ++*vnodes_skippedp;
				vm_page_unhold(m);
				return 0;
			}

			/*
			 * The page might have been moved to another
			 * queue during potential blocking in vget()
			 * above.  The page might have been freed and
			 * reused for another vnode.  The object might
			 * have been reused for another vnode.
			 */
			if (m->queue - m->pc != PQ_INACTIVE ||
			    m->object != object ||
			    object->handle != vp) {
				if (object->flags & OBJ_MIGHTBEDIRTY)
					++*vnodes_skippedp;
				vput(vp);
				vm_page_unhold(m);
				return 0;
			}

			/*
			 * The page may have been busied during the
			 * blocking in vput();  We don't move the
			 * page back onto the end of the queue so that
			 * statistics are more correct if we don't.
			 */
			if (vm_page_busy_try(m, TRUE)) {
				vput(vp);
				vm_page_unhold(m);
				return 0;
			}
			vm_page_unhold(m);

			/*
			 * If it was wired while we didn't own it.
			 */
			if (m->wire_count) {
				vm_page_unqueue_nowakeup(m);
				vput(vp);
				vm_page_wakeup(m);
				return 0;
			}

			/*
			 * (m) is busied again
			 *
			 * We own the busy bit and remove our hold
			 * bit.  If the page is still held it
			 * might be undergoing I/O, so skip it.
			 */
			if (m->hold_count) {
rebusy_failed:
				vm_page_and_queue_spin_lock(m);
				if (m->queue - m->pc == PQ_INACTIVE) {
					TAILQ_REMOVE(&vm_page_queues[m->queue].pl, m, pageq);
					TAILQ_INSERT_TAIL(&vm_page_queues[m->queue].pl, m, pageq);
				}
				vm_page_and_queue_spin_unlock(m);
				if (object->flags & OBJ_MIGHTBEDIRTY)
					++*vnodes_skippedp;
				vm_page_wakeup(m);
				vput(vp);
				return 0;
			}

			/*
			 * Recheck queue, object, and vp now that we have
			 * rebusied the page.
			 */
			if (m->queue - m->pc != PQ_INACTIVE ||
			    m->object != object ||
			    object->handle != vp) {
				kprintf("vm_pageout_page: "
					"rebusy %p failed(A)\n",
					m);
				goto rebusy_failed;
			}

			/*
			 * Check page validity
			 */
			if (m->valid == 0 && (m->flags & PG_NEED_COMMIT) == 0) {
				kprintf("vm_pageout_page: "
					"rebusy %p failed(B)\n",
					m);
				goto rebusy_failed;
			}
			if (m->dirty == 0 && (m->flags & PG_NEED_COMMIT) == 0) {
				kprintf("vm_pageout_page: "
					"rebusy %p failed(C)\n",
					m);
				goto rebusy_failed;
			}

			/* (m) is left busied as we fall through */
		}

		/*
		 * page is busy and not held here.
		 *
		 * If a page is dirty, then it is either being washed
		 * (but not yet cleaned) or it is still in the
		 * laundry.  If it is still in the laundry, then we
		 * start the cleaning operation.
		 *
		 * decrement inactive_shortage on success to account
		 * for the (future) cleaned page.  Otherwise we
		 * could wind up laundering or cleaning too many
		 * pages.
		 *
		 * NOTE: Cleaning the page here does not cause
		 *	 force_deficit to be adjusted, because the
		 *	 page is not being freed or moved to the
		 *	 cache.
		 */
		count = vm_pageout_clean_helper(m, vmflush_flags);
		counts[0] += count;
		*max_launderp -= count;

		/*
		 * Clean ate busy, page no longer accessible
		 */
		if (vp != NULL)
			vput(vp);
	} else {
		vm_page_wakeup(m);
	}
	return count;
}

/*
 * Scan active queue
 *
 * WARNING! Can be called from two pagedaemon threads simultaneously.
 */
static int
vm_pageout_scan_active(int pass, int q,
		       long avail_shortage, long inactive_shortage,
		       long *recycle_countp)
{
	struct vm_page marker;
	vm_page_t m;
	int actcount;
	long delta = 0;
	long maxscan;
	int isep;

	isep = (curthread == emergpager);

	/*
	 * We want to move pages from the active queue to the inactive
	 * queue to get the inactive queue to the inactive target.  If
	 * we still have a page shortage from above we try to directly free
	 * clean pages instead of moving them.
	 *
	 * If we do still have a shortage we keep track of the number of
	 * pages we free or cache (recycle_count) as a measure of thrashing
	 * between the active and inactive queues.
	 *
	 * If we were able to completely satisfy the free+cache targets
	 * from the inactive pool we limit the number of pages we move
	 * from the active pool to the inactive pool to 2x the pages we
	 * had removed from the inactive pool (with a minimum of 1/5 the
	 * inactive target).  If we were not able to completely satisfy
	 * the free+cache targets we go for the whole target aggressively.
	 *
	 * NOTE: Both variables can end up negative.
	 * NOTE: We are still in a critical section.
	 *
	 * NOTE!  THE EMERGENCY PAGER (isep) DOES NOT LAUNDER VNODE-BACKED
	 *	  PAGES.
	 */

	bzero(&marker, sizeof(marker));
	marker.flags = PG_FICTITIOUS | PG_MARKER;
	marker.busy_count = PBUSY_LOCKED;
	marker.queue = PQ_ACTIVE + q;
	marker.pc = q;
	marker.wire_count = 1;

	vm_page_queues_spin_lock(PQ_ACTIVE + q);
	TAILQ_INSERT_HEAD(&vm_page_queues[PQ_ACTIVE + q].pl, &marker, pageq);
	maxscan = vm_page_queues[PQ_ACTIVE + q].lcnt / MAXSCAN_DIVIDER + 1;

	/*
	 * Queue locked at top of loop to avoid stack marker issues.
	 */
	while ((m = TAILQ_NEXT(&marker, pageq)) != NULL &&
	       maxscan-- > 0 && (avail_shortage - delta > 0 ||
				inactive_shortage > 0))
	{
		KKASSERT(m->queue == PQ_ACTIVE + q);
		TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE + q].pl,
			     &marker, pageq);
		TAILQ_INSERT_AFTER(&vm_page_queues[PQ_ACTIVE + q].pl, m,
				   &marker, pageq);

		/*
		 * Skip marker pages (atomic against other markers to avoid
		 * infinite hop-over scans).
		 */
		if (m->flags & PG_MARKER)
			continue;

		/*
		 * Try to busy the page.  Don't mess with pages which are
		 * already busy or reorder them in the queue.
		 */
		if (vm_page_busy_try(m, TRUE))
			continue;

		/*
		 * Remaining operations run with the page busy and neither
		 * the page or the queue will be spin-locked.
		 */
		KKASSERT(m->queue == PQ_ACTIVE + q);
		vm_page_queues_spin_unlock(PQ_ACTIVE + q);

#if 0
		/*
		 * Don't deactivate pages that are held, even if we can
		 * busy them.  (XXX why not?)
		 */
		if (m->hold_count) {
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_ACTIVE) {
				TAILQ_REMOVE(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
				TAILQ_INSERT_TAIL(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
			vm_page_wakeup(m);
			goto next;
		}
#endif
		/*
		 * We can just remove wired pages from the queue
		 */
		if (m->wire_count) {
			vm_page_unqueue_nowakeup(m);
			vm_page_wakeup(m);
			goto next;
		}

		/*
		 * The emergency pager ignores vnode-backed pages as these
		 * are the pages that probably bricked the main pager.
		 */
		if (isep && m->object && m->object->type == OBJT_VNODE) {
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_ACTIVE) {
				TAILQ_REMOVE(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
				TAILQ_INSERT_TAIL(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
			vm_page_wakeup(m);
			goto next;
		}

		/*
		 * The count for pagedaemon pages is done after checking the
		 * page for eligibility...
		 */
		mycpu->gd_cnt.v_pdpages++;

		/*
		 * Check to see "how much" the page has been used and clear
		 * the tracking access bits.  If the object has no references
		 * don't bother paying the expense.
		 */
		actcount = 0;
		if (m->object && m->object->ref_count != 0) {
			if (m->flags & PG_REFERENCED)
				++actcount;
			actcount += pmap_ts_referenced(m);
			if (actcount) {
				m->act_count += ACT_ADVANCE + actcount;
				if (m->act_count > ACT_MAX)
					m->act_count = ACT_MAX;
			}
		}
		vm_page_flag_clear(m, PG_REFERENCED);

		/*
		 * actcount is only valid if the object ref_count is non-zero.
		 * If the page does not have an object, actcount will be zero.
		 */
		if (actcount && m->object->ref_count != 0) {
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_ACTIVE) {
				TAILQ_REMOVE(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
				TAILQ_INSERT_TAIL(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
			vm_page_wakeup(m);
		} else {
			switch(m->object->type) {
			case OBJT_DEFAULT:
			case OBJT_SWAP:
				m->act_count -= min(m->act_count,
						    vm_anonmem_decline);
				break;
			default:
				m->act_count -= min(m->act_count,
						    vm_filemem_decline);
				break;
			}
			if (vm_pageout_algorithm ||
			    (m->object == NULL) ||
			    (m->object && (m->object->ref_count == 0)) ||
			    m->act_count < pass + 1
			) {
				/*
				 * Deactivate the page.  If we had a
				 * shortage from our inactive scan try to
				 * free (cache) the page instead.
				 *
				 * Don't just blindly cache the page if
				 * we do not have a shortage from the
				 * inactive scan, that could lead to
				 * gigabytes being moved.
				 */
				--inactive_shortage;
				if (avail_shortage - delta > 0 ||
				    (m->object && (m->object->ref_count == 0)))
				{
					if (avail_shortage - delta > 0)
						++*recycle_countp;
					vm_page_protect(m, VM_PROT_NONE);
					if (m->dirty == 0 &&
					    (m->flags & PG_NEED_COMMIT) == 0 &&
					    avail_shortage - delta > 0) {
						vm_page_cache(m);
					} else {
						vm_page_deactivate(m);
						vm_page_wakeup(m);
					}
				} else {
					vm_page_deactivate(m);
					vm_page_wakeup(m);
				}
				++delta;
			} else {
				vm_page_and_queue_spin_lock(m);
				if (m->queue - m->pc == PQ_ACTIVE) {
					TAILQ_REMOVE(
					    &vm_page_queues[PQ_ACTIVE + q].pl,
					    m, pageq);
					TAILQ_INSERT_TAIL(
					    &vm_page_queues[PQ_ACTIVE + q].pl,
					    m, pageq);
				}
				vm_page_and_queue_spin_unlock(m);
				vm_page_wakeup(m);
			}
		}
next:
		lwkt_yield();
		vm_page_queues_spin_lock(PQ_ACTIVE + q);
	}

	/*
	 * Clean out our local marker.
	 *
	 * Page queue still spin-locked.
	 */
	TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE + q].pl, &marker, pageq);
	vm_page_queues_spin_unlock(PQ_ACTIVE + q);

	return (delta);
}

/*
 * The number of actually free pages can drop down to v_free_reserved,
 * we try to build the free count back above v_free_min.  Note that
 * vm_paging_needed() also returns TRUE if v_free_count is not at
 * least v_free_min so that is the minimum we must build the free
 * count to.
 *
 * We use a slightly higher target to improve hysteresis,
 * ((v_free_target + v_free_min) / 2).  Since v_free_target
 * is usually the same as v_cache_min this maintains about
 * half the pages in the free queue as are in the cache queue,
 * providing pretty good pipelining for pageout operation.
 *
 * The system operator can manipulate vm.v_cache_min and
 * vm.v_free_target to tune the pageout demon.  Be sure
 * to keep vm.v_free_min < vm.v_free_target.
 *
 * Note that the original paging target is to get at least
 * (free_min + cache_min) into (free + cache).  The slightly
 * higher target will shift additional pages from cache to free
 * without effecting the original paging target in order to
 * maintain better hysteresis and not have the free count always
 * be dead-on v_free_min.
 *
 * NOTE: we are still in a critical section.
 *
 * Pages moved from PQ_CACHE to totally free are not counted in the
 * pages_freed counter.
 *
 * WARNING! Can be called from two pagedaemon threads simultaneously.
 */
static void
vm_pageout_scan_cache(long avail_shortage, int pass,
		      long vnodes_skipped, long recycle_count)
{
	static int lastkillticks;
	struct vm_pageout_scan_info info;
	vm_page_t m;
	int isep;

	isep = (curthread == emergpager);

	while (vmstats.v_free_count <
	       (vmstats.v_free_min + vmstats.v_free_target) / 2) {
		/*
		 * This steals some code from vm/vm_page.c
		 *
		 * Create two rovers and adjust the code to reduce
		 * chances of them winding up at the same index (which
		 * can cause a lot of contention).
		 */
		static int cache_rover[2] = { 0, PQ_L2_MASK / 2 };

		if (((cache_rover[0] ^ cache_rover[1]) & PQ_L2_MASK) == 0)
			goto next_rover;

		m = vm_page_list_find(PQ_CACHE, cache_rover[isep] & PQ_L2_MASK);
		if (m == NULL)
			break;
		/*
		 * page is returned removed from its queue and spinlocked
		 *
		 * If the busy attempt fails we can still deactivate the page.
		 */
		if (vm_page_busy_try(m, TRUE)) {
			vm_page_deactivate_locked(m);
			vm_page_spin_unlock(m);
			continue;
		}
		vm_page_spin_unlock(m);
		pagedaemon_wakeup();
		lwkt_yield();

		/*
		 * Remaining operations run with the page busy and neither
		 * the page or the queue will be spin-locked.
		 */
		if ((m->flags & (PG_UNQUEUED | PG_NEED_COMMIT)) ||
		    m->hold_count ||
		    m->wire_count) {
			vm_page_deactivate(m);
			vm_page_wakeup(m);
			continue;
		}

		/*
		 * Because the page is in the cache, it shouldn't be mapped.
		 */
		pmap_mapped_sync(m);
		KKASSERT((m->flags & PG_MAPPED) == 0);
		KKASSERT(m->dirty == 0);
		vm_pageout_page_free(m);
		mycpu->gd_cnt.v_dfree++;
next_rover:
		if (isep)
			cache_rover[1] -= PQ_PRIME2;
		else
			cache_rover[0] += PQ_PRIME2;
	}

	/*
	 * If we didn't get enough free pages, and we have skipped a vnode
	 * in a writeable object, wakeup the sync daemon.  And kick swapout
	 * if we did not get enough free pages.
	 */
	if (vm_paging_target() > 0) {
		if (vnodes_skipped && vm_page_count_min(0))
			speedup_syncer(NULL);
#if !defined(NO_SWAPPING)
		if (vm_swap_enabled && vm_page_count_target())
			vm_req_vmdaemon();
#endif
	}

	/*
	 * Handle catastrophic conditions.  Under good conditions we should
	 * be at the target, well beyond our minimum.  If we could not even
	 * reach our minimum the system is under heavy stress.  But just being
	 * under heavy stress does not trigger process killing.
	 *
	 * We consider ourselves to have run out of memory if the swap pager
	 * is full and avail_shortage is still positive.  The secondary check
	 * ensures that we do not kill processes if the instantanious
	 * availability is good, even if the pageout demon pass says it
	 * couldn't get to the target.
	 *
	 * NOTE!  THE EMERGENCY PAGER (isep) DOES NOT HANDLE SWAP FULL
	 *	  SITUATIONS.
	 */
	if (swap_pager_almost_full &&
	    pass > 0 &&
	    isep == 0 &&
	    (vm_page_count_min(recycle_count) || avail_shortage > 0)) {
		kprintf("Warning: system low on memory+swap "
			"shortage %ld for %d ticks!\n",
			avail_shortage, ticks - swap_fail_ticks);
		if (bootverbose)
		kprintf("Metrics: spaf=%d spf=%d pass=%d "
			"avail=%ld target=%ld last=%u\n",
			swap_pager_almost_full,
			swap_pager_full,
			pass,
			avail_shortage,
			vm_paging_target(),
			(unsigned int)(ticks - lastkillticks));
	}
	if (swap_pager_full &&
	    pass > 1 &&
	    isep == 0 &&
	    avail_shortage > 0 &&
	    vm_paging_target() > 0 &&
	    (unsigned int)(ticks - lastkillticks) >= hz) {
		/*
		 * Kill something, maximum rate once per second to give
		 * the process time to free up sufficient memory.
		 */
		lastkillticks = ticks;
		info.bigproc = NULL;
		info.bigsize = 0;
		allproc_scan(vm_pageout_scan_callback, &info, 0);
		if (info.bigproc != NULL) {
			kprintf("Try to kill process %d %s\n",
				info.bigproc->p_pid, info.bigproc->p_comm);
			info.bigproc->p_nice = PRIO_MIN;
			info.bigproc->p_usched->resetpriority(
				FIRST_LWP_IN_PROC(info.bigproc));
			atomic_set_int(&info.bigproc->p_flags, P_LOWMEMKILL);
			killproc(info.bigproc, "out of swap space");
			wakeup(&vmstats.v_free_count);
			PRELE(info.bigproc);
		}
	}
}

static int
vm_pageout_scan_callback(struct proc *p, void *data)
{
	struct vm_pageout_scan_info *info = data;
	vm_offset_t size;

	/*
	 * Never kill system processes or init.  If we have configured swap
	 * then try to avoid killing low-numbered pids.
	 */
	if ((p->p_flags & P_SYSTEM) || (p->p_pid == 1) ||
	    ((p->p_pid < 48) && (vm_swap_size != 0))) {
		return (0);
	}

	lwkt_gettoken(&p->p_token);

	/*
	 * if the process is in a non-running type state,
	 * don't touch it.
	 */
	if (p->p_stat != SACTIVE && p->p_stat != SSTOP && p->p_stat != SCORE) {
		lwkt_reltoken(&p->p_token);
		return (0);
	}

	/*
	 * Get the approximate process size.  Note that anonymous pages
	 * with backing swap will be counted twice, but there should not
	 * be too many such pages due to the stress the VM system is
	 * under at this point.
	 */
	size = vmspace_anonymous_count(p->p_vmspace) +
		vmspace_swap_count(p->p_vmspace);

	/*
	 * If the this process is bigger than the biggest one
	 * remember it.
	 */
	if (info->bigsize < size) {
		if (info->bigproc)
			PRELE(info->bigproc);
		PHOLD(p);
		info->bigproc = p;
		info->bigsize = size;
	}
	lwkt_reltoken(&p->p_token);
	lwkt_yield();

	return(0);
}

/*
 * This old guy slowly walks PQ_HOLD looking for pages which need to be
 * moved back to PQ_FREE.  It is possible for pages to accumulate here
 * when vm_page_free() races against vm_page_unhold(), resulting in a
 * page being left on a PQ_HOLD queue with hold_count == 0.
 *
 * It is easier to handle this edge condition here, in non-critical code,
 * rather than enforce a spin-lock for every 1->0 transition in
 * vm_page_unhold().
 *
 * NOTE: TAILQ_FOREACH becomes invalid the instant we unlock the queue.
 */
static void
vm_pageout_scan_hold(int q)
{
	vm_page_t m;

	vm_page_queues_spin_lock(PQ_HOLD + q);
	TAILQ_FOREACH(m, &vm_page_queues[PQ_HOLD + q].pl, pageq) {
		if (m->flags & PG_MARKER)
			continue;

		/*
		 * Process one page and return
		 */
		if (m->hold_count)
			break;
		kprintf("DEBUG: pageout HOLD->FREE %p\n", m);
		vm_page_hold(m);
		vm_page_queues_spin_unlock(PQ_HOLD + q);
		vm_page_unhold(m);	/* reprocess */
		return;
	}
	vm_page_queues_spin_unlock(PQ_HOLD + q);
}

/*
 * This routine tries to maintain the pseudo LRU active queue,
 * so that during long periods of time where there is no paging,
 * that some statistic accumulation still occurs.  This code
 * helps the situation where paging just starts to occur.
 */
static void
vm_pageout_page_stats(int q)
{
	static int fullintervalcount = 0;
	struct vm_page marker;
	vm_page_t m;
	long pcount, tpcount;		/* Number of pages to check */
	long page_shortage;

	page_shortage = (vmstats.v_inactive_target + vmstats.v_cache_max +
			 vmstats.v_free_min) -
			(vmstats.v_free_count + vmstats.v_inactive_count +
			 vmstats.v_cache_count);

	if (page_shortage <= 0)
		return;

	pcount = vm_page_queues[PQ_ACTIVE + q].lcnt;
	fullintervalcount += vm_pageout_stats_interval;
	if (fullintervalcount < vm_pageout_full_stats_interval) {
		tpcount = (vm_pageout_stats_max * pcount) /
			  vmstats.v_page_count + 1;
		if (pcount > tpcount)
			pcount = tpcount;
	} else {
		fullintervalcount = 0;
	}

	bzero(&marker, sizeof(marker));
	marker.flags = PG_FICTITIOUS | PG_MARKER;
	marker.busy_count = PBUSY_LOCKED;
	marker.queue = PQ_ACTIVE + q;
	marker.pc = q;
	marker.wire_count = 1;

	vm_page_queues_spin_lock(PQ_ACTIVE + q);
	TAILQ_INSERT_HEAD(&vm_page_queues[PQ_ACTIVE + q].pl, &marker, pageq);

	/*
	 * Queue locked at top of loop to avoid stack marker issues.
	 */
	while ((m = TAILQ_NEXT(&marker, pageq)) != NULL &&
	       pcount-- > 0)
	{
		int actcount;

		KKASSERT(m->queue == PQ_ACTIVE + q);
		TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE + q].pl, &marker, pageq);
		TAILQ_INSERT_AFTER(&vm_page_queues[PQ_ACTIVE + q].pl, m,
				   &marker, pageq);

		/*
		 * Skip marker pages (atomic against other markers to avoid
		 * infinite hop-over scans).
		 */
		if (m->flags & PG_MARKER)
			continue;

		/*
		 * Ignore pages we can't busy
		 */
		if (vm_page_busy_try(m, TRUE))
			continue;

		/*
		 * Remaining operations run with the page busy and neither
		 * the page or the queue will be spin-locked.
		 */
		KKASSERT(m->queue == PQ_ACTIVE + q);
		vm_page_queues_spin_unlock(PQ_ACTIVE + q);

		/*
		 * We can just remove wired pages from the queue
		 */
		if (m->wire_count) {
			vm_page_unqueue_nowakeup(m);
			vm_page_wakeup(m);
			goto next;
		}


		/*
		 * We now have a safely busied page, the page and queue
		 * spinlocks have been released.
		 *
		 * Ignore held and wired pages
		 */
		if (m->hold_count || m->wire_count) {
			vm_page_wakeup(m);
			goto next;
		}

		/*
		 * Calculate activity
		 */
		actcount = 0;
		if (m->flags & PG_REFERENCED) {
			vm_page_flag_clear(m, PG_REFERENCED);
			actcount += 1;
		}
		actcount += pmap_ts_referenced(m);

		/*
		 * Update act_count and move page to end of queue.
		 */
		if (actcount) {
			m->act_count += ACT_ADVANCE + actcount;
			if (m->act_count > ACT_MAX)
				m->act_count = ACT_MAX;
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_ACTIVE) {
				TAILQ_REMOVE(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
				TAILQ_INSERT_TAIL(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
			vm_page_wakeup(m);
			goto next;
		}

		if (m->act_count == 0) {
			/*
			 * We turn off page access, so that we have
			 * more accurate RSS stats.  We don't do this
			 * in the normal page deactivation when the
			 * system is loaded VM wise, because the
			 * cost of the large number of page protect
			 * operations would be higher than the value
			 * of doing the operation.
			 *
			 * We use the marker to save our place so
			 * we can release the spin lock.  both (m)
			 * and (next) will be invalid.
			 */
			vm_page_protect(m, VM_PROT_NONE);
			vm_page_deactivate(m);
		} else {
			m->act_count -= min(m->act_count, ACT_DECLINE);
			vm_page_and_queue_spin_lock(m);
			if (m->queue - m->pc == PQ_ACTIVE) {
				TAILQ_REMOVE(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
				TAILQ_INSERT_TAIL(
					&vm_page_queues[PQ_ACTIVE + q].pl,
					m, pageq);
			}
			vm_page_and_queue_spin_unlock(m);
		}
		vm_page_wakeup(m);
next:
		vm_page_queues_spin_lock(PQ_ACTIVE + q);
	}

	/*
	 * Remove our local marker
	 *
	 * Page queue still spin-locked.
	 */
	TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE + q].pl, &marker, pageq);
	vm_page_queues_spin_unlock(PQ_ACTIVE + q);
}

static void
vm_pageout_free_page_calc(vm_size_t count)
{
	/*
	 * v_free_min		normal allocations
	 * v_free_reserved	system allocations
	 * v_pageout_free_min	allocations by pageout daemon
	 * v_interrupt_free_min	low level allocations (e.g swap structures)
	 *
	 * v_free_min is used to generate several other baselines, and they
	 * can get pretty silly on systems with a lot of memory.
	 */
	vmstats.v_free_min = 64 + vmstats.v_page_count / 200;
	vmstats.v_free_reserved = vmstats.v_free_min * 4 / 8 + 7;
	vmstats.v_free_severe = vmstats.v_free_min * 4 / 8 + 0;
	vmstats.v_pageout_free_min = vmstats.v_free_min * 2 / 8 + 7;
	vmstats.v_interrupt_free_min = vmstats.v_free_min * 1 / 8 + 7;
}


/*
 * vm_pageout is the high level pageout daemon.  TWO kernel threads run
 * this daemon, the primary pageout daemon and the emergency pageout daemon.
 *
 * The emergency pageout daemon takes over when the primary pageout daemon
 * deadlocks.  The emergency pageout daemon ONLY pages out to swap, thus
 * avoiding the many low-memory deadlocks which can occur when paging out
 * to VFS's.
 */
static void
vm_pageout_thread(void)
{
	int pass;
	int q;
	int q1iterator = 0;
	int q2iterator = 0;
	int q3iterator = 0;
	int isep;

	curthread->td_flags |= TDF_SYSTHREAD;

	/*
	 * We only need to setup once.
	 */
	isep = 0;
	if (curthread == emergpager) {
		isep = 1;
		goto skip_setup;
	}

	/*
	 * Initialize vm_max_launder per pageout pass to be 1/16
	 * of total physical memory, plus a little slop.
	 */
	if (vm_max_launder == 0)
		vm_max_launder = physmem / 256 + 16;

	/*
	 * Initialize some paging parameters.
	 */
	vm_pageout_free_page_calc(vmstats.v_page_count);

	/*
	 * v_free_target and v_cache_min control pageout hysteresis.  Note
	 * that these are more a measure of the VM cache queue hysteresis
	 * then the VM free queue.  Specifically, v_free_target is the
	 * high water mark (free+cache pages).
	 *
	 * v_free_reserved + v_cache_min (mostly means v_cache_min) is the
	 * low water mark, while v_free_min is the stop.  v_cache_min must
	 * be big enough to handle memory needs while the pageout daemon
	 * is signalled and run to free more pages.
	 */
	vmstats.v_free_target = 4 * vmstats.v_free_min +
				vmstats.v_free_reserved;

	/*
	 * NOTE: With the new buffer cache b_act_count we want the default
	 *	 inactive target to be a percentage of available memory.
	 *
	 *	 The inactive target essentially determines the minimum
	 *	 number of 'temporary' pages capable of caching one-time-use
	 *	 files when the VM system is otherwise full of pages
	 *	 belonging to multi-time-use files or active program data.
	 *
	 * NOTE: The inactive target is aggressively persued only if the
	 *	 inactive queue becomes too small.  If the inactive queue
	 *	 is large enough to satisfy page movement to free+cache
	 *	 then it is repopulated more slowly from the active queue.
	 *	 This allows a general inactive_target default to be set.
	 *
	 *	 There is an issue here for processes which sit mostly idle
	 *	 'overnight', such as sshd, tcsh, and X.  Any movement from
	 *	 the active queue will eventually cause such pages to
	 *	 recycle eventually causing a lot of paging in the morning.
	 *	 To reduce the incidence of this pages cycled out of the
	 *	 buffer cache are moved directly to the inactive queue if
	 *	 they were only used once or twice.
	 *
	 *	 The vfs.vm_cycle_point sysctl can be used to adjust this.
	 *	 Increasing the value (up to 64) increases the number of
	 *	 buffer recyclements which go directly to the inactive queue.
	 */
	if (vmstats.v_free_count > 2048) {
		vmstats.v_cache_min = vmstats.v_free_target;
		vmstats.v_cache_max = 2 * vmstats.v_cache_min;
	} else {
		vmstats.v_cache_min = 0;
		vmstats.v_cache_max = 0;
	}
	vmstats.v_inactive_target = vmstats.v_free_count / 4;

	/* XXX does not really belong here */
	if (vm_page_max_wired == 0)
		vm_page_max_wired = vmstats.v_free_count / 3;

	if (vm_pageout_stats_max == 0)
		vm_pageout_stats_max = vmstats.v_free_target;

	/*
	 * Set interval in seconds for stats scan.
	 */
	if (vm_pageout_stats_interval == 0)
		vm_pageout_stats_interval = 5;
	if (vm_pageout_full_stats_interval == 0)
		vm_pageout_full_stats_interval = vm_pageout_stats_interval * 4;
	

	/*
	 * Set maximum free per pass
	 */
	if (vm_pageout_stats_free_max == 0)
		vm_pageout_stats_free_max = 5;

	swap_pager_swap_init();
	pass = 0;

	atomic_swap_int(&sequence_emerg_pager, 1);
	wakeup(&sequence_emerg_pager);

skip_setup:
	/*
	 * Sequence emergency pager startup
	 */
	if (isep) {
		while (sequence_emerg_pager == 0)
			tsleep(&sequence_emerg_pager, 0, "pstartup", hz);
	}

	/*
	 * The pageout daemon is never done, so loop forever.
	 *
	 * WARNING!  This code is being executed by two kernel threads
	 *	     potentially simultaneously.
	 */
	while (TRUE) {
		int error;
		long avail_shortage;
		long inactive_shortage;
		long vnodes_skipped = 0;
		long recycle_count = 0;
		long tmp;

		/*
		 * Wait for an action request.  If we timeout check to
		 * see if paging is needed (in case the normal wakeup
		 * code raced us).
		 */
		if (isep) {
			/*
			 * Emergency pagedaemon monitors the primary
			 * pagedaemon while vm_pages_needed != 0.
			 *
			 * The emergency pagedaemon only runs if VM paging
			 * is needed and the primary pagedaemon has not
			 * updated vm_pagedaemon_time for more than 2 seconds.
			 */
			if (vm_pages_needed)
				tsleep(&vm_pagedaemon_time, 0, "psleep", hz);
			else
				tsleep(&vm_pagedaemon_time, 0, "psleep", hz*10);
			if (vm_pages_needed == 0) {
				pass = 0;
				continue;
			}
			if ((int)(ticks - vm_pagedaemon_time) < hz * 2) {
				pass = 0;
				continue;
			}
		} else {
			/*
			 * Primary pagedaemon
			 *
			 * NOTE: We unconditionally cleanup PQ_HOLD even
			 *	 when there is no work to do.
			 */
			vm_pageout_scan_hold(q3iterator & PQ_L2_MASK);
			++q3iterator;

			if (vm_pages_needed == 0) {
				error = tsleep(&vm_pages_needed,
					       0, "psleep",
					       vm_pageout_stats_interval * hz);
				if (error &&
				    vm_paging_needed(0) == 0 &&
				    vm_pages_needed == 0) {
					for (q = 0; q < PQ_L2_SIZE; ++q)
						vm_pageout_page_stats(q);
					continue;
				}
				vm_pagedaemon_time = ticks;
				vm_pages_needed = 1;

				/*
				 * Wake the emergency pagedaemon up so it
				 * can monitor us.  It will automatically
				 * go back into a long sleep when
				 * vm_pages_needed returns to 0.
				 */
				wakeup(&vm_pagedaemon_time);
			}
		}

		mycpu->gd_cnt.v_pdwakeups++;

		/*
		 * Scan for INACTIVE->CLEAN/PAGEOUT
		 *
		 * This routine tries to avoid thrashing the system with
		 * unnecessary activity.
		 *
		 * Calculate our target for the number of free+cache pages we
		 * want to get to.  This is higher then the number that causes
		 * allocations to stall (severe) in order to provide hysteresis,
		 * and if we don't make it all the way but get to the minimum
		 * we're happy.  Goose it a bit if there are multiple requests
		 * for memory.
		 *
		 * Don't reduce avail_shortage inside the loop or the
		 * PQAVERAGE() calculation will break.
		 *
		 * NOTE! deficit is differentiated from avail_shortage as
		 *	 REQUIRING at least (deficit) pages to be cleaned,
		 *	 even if the page queues are in good shape.  This
		 *	 is used primarily for handling per-process
		 *	 RLIMIT_RSS and may also see small values when
		 *	 processes block due to low memory.
		 */
		vmstats_rollup();
		if (isep == 0)
			vm_pagedaemon_time = ticks;
		avail_shortage = vm_paging_target() + vm_pageout_deficit;
		vm_pageout_deficit = 0;

		if (avail_shortage > 0) {
			long delta = 0;
			long counts[4] = { 0, 0, 0, 0 };
			int qq;

			if (vm_pageout_debug) {
				kprintf("scan_inactive pass %d isep=%d\t",
					pass / MAXSCAN_DIVIDER, isep);
			}

			qq = q1iterator;
			for (q = 0; q < PQ_L2_SIZE; ++q) {
				delta += vm_pageout_scan_inactive(
					    pass / MAXSCAN_DIVIDER,
					    qq & PQ_L2_MASK,
					    PQAVERAGE(avail_shortage),
					    &vnodes_skipped, counts);
				if (isep)
					--qq;
				else
					++qq;
				if (avail_shortage - delta <= 0)
					break;

				/*
				 * It is possible for avail_shortage to be
				 * very large.  If a large program exits or
				 * frees a ton of memory all at once, we do
				 * not have to continue deactivations.
				 *
				 * (We will still run the active->inactive
				 * target, however).
				 */
				if (!vm_page_count_target() &&
				    !vm_page_count_min(
						vm_page_free_hysteresis)) {
					avail_shortage = 0;
					break;
				}
			}
			if (vm_pageout_debug) {
				kprintf("flushed %ld cleaned %ld "
					"lru2 %ld react %ld "
					"delta %ld\n",
					counts[0], counts[1],
					counts[2], counts[3],
					delta);
			}
			avail_shortage -= delta;
			q1iterator = qq;
		}

		/*
		 * Figure out how many active pages we must deactivate.  If
		 * we were able to reach our target with just the inactive
		 * scan above we limit the number of active pages we
		 * deactivate to reduce unnecessary work.
		 */
		vmstats_rollup();
		if (isep == 0)
			vm_pagedaemon_time = ticks;
		inactive_shortage = vmstats.v_inactive_target -
				    vmstats.v_inactive_count;

		/*
		 * If we were unable to free sufficient inactive pages to
		 * satisfy the free/cache queue requirements then simply
		 * reaching the inactive target may not be good enough.
		 * Try to deactivate pages in excess of the target based
		 * on the shortfall.
		 *
		 * However to prevent thrashing the VM system do not
		 * deactivate more than an additional 1/10 the inactive
		 * target's worth of active pages.
		 */
		if (avail_shortage > 0) {
			tmp = avail_shortage * 2;
			if (tmp > vmstats.v_inactive_target / 10)
				tmp = vmstats.v_inactive_target / 10;
			inactive_shortage += tmp;
		}

		/*
		 * Only trigger a pmap cleanup on inactive shortage.
		 */
		if (isep == 0 && inactive_shortage > 0) {
			pmap_collect();
		}

		/*
		 * Scan for ACTIVE->INACTIVE
		 *
		 * Only trigger on inactive shortage.  Triggering on
		 * avail_shortage can starve the active queue with
		 * unnecessary active->inactive transitions and destroy
		 * performance.
		 *
		 * If this is the emergency pager, always try to move
		 * a few pages from active to inactive because the inactive
		 * queue might have enough pages, but not enough anonymous
		 * pages.
		 */
		if (isep && inactive_shortage < vm_emerg_launder)
			inactive_shortage = vm_emerg_launder;

		if (/*avail_shortage > 0 ||*/ inactive_shortage > 0) {
			long delta = 0;
			int qq;

			qq = q2iterator;
			for (q = 0; q < PQ_L2_SIZE; ++q) {
				delta += vm_pageout_scan_active(
						pass / MAXSCAN_DIVIDER,
						qq & PQ_L2_MASK,
						PQAVERAGE(avail_shortage),
						PQAVERAGE(inactive_shortage),
						&recycle_count);
				if (isep)
					--qq;
				else
					++qq;
				if (inactive_shortage - delta <= 0 &&
				    avail_shortage - delta <= 0) {
					break;
				}

				/*
				 * inactive_shortage can be a very large
				 * number.  This is intended to break out
				 * early if our inactive_target has been
				 * reached due to other system activity.
				 */
				if (vmstats.v_inactive_count >
				    vmstats.v_inactive_target) {
					inactive_shortage = 0;
					break;
				}
			}
			inactive_shortage -= delta;
			avail_shortage -= delta;
			q2iterator = qq;
		}

		/*
		 * Scan for CACHE->FREE
		 *
		 * Finally free enough cache pages to meet our free page
		 * requirement and take more drastic measures if we are
		 * still in trouble.
		 */
		vmstats_rollup();
		if (isep == 0)
			vm_pagedaemon_time = ticks;
		vm_pageout_scan_cache(avail_shortage, pass / MAXSCAN_DIVIDER,
				      vnodes_skipped, recycle_count);

		/*
		 * This is a bit sophisticated because we do not necessarily
		 * want to force paging until our targets are reached if we
		 * were able to successfully retire the shortage we calculated.
		 */
		if (avail_shortage > 0) {
			/*
			 * If we did not retire enough pages continue the
			 * pageout operation until we are able to.  It
			 * takes MAXSCAN_DIVIDER passes to cover the entire
			 * inactive list.
			 */
			++pass;

			if (pass / MAXSCAN_DIVIDER < 10 &&
			    vm_pages_needed > 1) {
				/*
				 * Normal operation, additional processes
				 * have already kicked us.  Retry immediately
				 * unless swap space is completely full in
				 * which case delay a bit.
				 */
				if (swap_pager_full) {
					tsleep(&vm_pages_needed, 0, "pdelay",
						hz / 5);
				} /* else immediate retry */
			} else if (pass / MAXSCAN_DIVIDER < 10) {
				/*
				 * Do a short sleep for the first 10 passes,
				 * allow the sleep to be woken up by resetting
				 * vm_pages_needed to 1 (NOTE: we are still
				 * active paging!).
				 */
				if (isep == 0)
					vm_pages_needed = 1;
				tsleep(&vm_pages_needed, 0, "pdelay", 2);
			} else if (swap_pager_full == 0) {
				/*
				 * We've taken too many passes, force a
				 * longer delay.
				 */
				tsleep(&vm_pages_needed, 0, "pdelay", hz / 10);
			} else {
				/*
				 * Running out of memory, catastrophic
				 * back-off to one-second intervals.
				 */
				tsleep(&vm_pages_needed, 0, "pdelay", hz);
			}
		} else if (vm_pages_needed) {
			/*
			 * We retired our calculated shortage but we may have
			 * to continue paging if threads drain memory too far
			 * below our target.
			 *
			 * Similar to vm_page_free_wakeup() in vm_page.c.
			 */
			pass = 0;
			if (!vm_paging_needed(0)) {
				/* still more than half-way to our target */
				vm_pages_needed = 0;
				wakeup(&vmstats.v_free_count);
			} else
			if (!vm_page_count_min(vm_page_free_hysteresis)) {
				/*
				 * Continue operations with wakeup
				 * (set variable to avoid overflow)
				 */
				vm_pages_needed = 2;
				wakeup(&vmstats.v_free_count);
			} else {
				/*
				 * No wakeup() needed, continue operations.
				 * (set variable to avoid overflow)
				 */
				vm_pages_needed = 2;
			}
		} else {
			/*
			 * Turn paging back on immediately if we are under
			 * minimum.
			 */
			pass = 0;
		}
	}
}

static struct kproc_desc pg1_kp = {
	"pagedaemon",
	vm_pageout_thread,
	&pagethread
};
SYSINIT(pagedaemon, SI_SUB_KTHREAD_PAGE, SI_ORDER_FIRST, kproc_start, &pg1_kp);

static struct kproc_desc pg2_kp = {
	"emergpager",
	vm_pageout_thread,
	&emergpager
};
SYSINIT(emergpager, SI_SUB_KTHREAD_PAGE, SI_ORDER_ANY, kproc_start, &pg2_kp);


/*
 * Called after allocating a page out of the cache or free queue
 * to possibly wake the pagedaemon up to replentish our supply.
 *
 * We try to generate some hysteresis by waking the pagedaemon up
 * when our free+cache pages go below the free_min+cache_min level.
 * The pagedaemon tries to get the count back up to at least the
 * minimum, and through to the target level if possible.
 *
 * If the pagedaemon is already active bump vm_pages_needed as a hint
 * that there are even more requests pending.
 *
 * SMP races ok?
 * No requirements.
 */
void
pagedaemon_wakeup(void)
{
	if (vm_paging_needed(0) && curthread != pagethread) {
		if (vm_pages_needed <= 1) {
			vm_pages_needed = 1;		/* SMP race ok */
			wakeup(&vm_pages_needed);	/* tickle pageout */
		} else if (vm_page_count_min(0)) {
			++vm_pages_needed;		/* SMP race ok */
			/* a wakeup() would be wasted here */
		}
	}
}

#if !defined(NO_SWAPPING)

/*
 * SMP races ok?
 * No requirements.
 */
static void
vm_req_vmdaemon(void)
{
	static int lastrun = 0;

	if ((ticks > (lastrun + hz)) || (ticks < lastrun)) {
		wakeup(&vm_daemon_needed);
		lastrun = ticks;
	}
}

static int vm_daemon_callback(struct proc *p, void *data __unused);

/*
 * No requirements.
 *
 * Scan processes for exceeding their rlimits, deactivate pages
 * when RSS is exceeded.
 */
static void
vm_daemon(void)
{
	while (TRUE) {
		tsleep(&vm_daemon_needed, 0, "psleep", 0);
		allproc_scan(vm_daemon_callback, NULL, 0);
	}
}

static int
vm_daemon_callback(struct proc *p, void *data __unused)
{
	struct vmspace *vm;
	vm_pindex_t limit, size;

	/*
	 * if this is a system process or if we have already
	 * looked at this process, skip it.
	 */
	lwkt_gettoken(&p->p_token);

	if (p->p_flags & (P_SYSTEM | P_WEXIT)) {
		lwkt_reltoken(&p->p_token);
		return (0);
	}

	/*
	 * if the process is in a non-running type state,
	 * don't touch it.
	 */
	if (p->p_stat != SACTIVE && p->p_stat != SSTOP && p->p_stat != SCORE) {
		lwkt_reltoken(&p->p_token);
		return (0);
	}

	/*
	 * get a limit
	 */
	limit = OFF_TO_IDX(qmin(p->p_rlimit[RLIMIT_RSS].rlim_cur,
			        p->p_rlimit[RLIMIT_RSS].rlim_max));

	vm = p->p_vmspace;
	vmspace_hold(vm);
	size = pmap_resident_tlnw_count(&vm->vm_pmap);
	if (limit >= 0 && size > 4096 &&
	    size - 4096 >= limit && vm_pageout_memuse_mode >= 1) {
		vm_pageout_map_deactivate_pages(&vm->vm_map, limit);
	}
	vmspace_drop(vm);

	lwkt_reltoken(&p->p_token);

	return (0);
}

#endif
