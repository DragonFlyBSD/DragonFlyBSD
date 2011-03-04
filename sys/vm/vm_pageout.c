/*
 * (MPSAFE)
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
 *
 * $FreeBSD: src/sys/vm/vm_pageout.c,v 1.151.2.15 2002/12/29 18:21:04 dillon Exp $
 */

/*
 *	The proverbial page-out daemon.
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

#include <sys/thread2.h>
#include <vm/vm_page2.h>

/*
 * System initialization
 */

/* the kernel process "vm_pageout"*/
static int vm_pageout_clean (vm_page_t);
static int vm_pageout_scan (int pass);
static int vm_pageout_free_page_calc (vm_size_t count);
struct thread *pagethread;

#if !defined(NO_SWAPPING)
/* the kernel process "vm_daemon"*/
static void vm_daemon (void);
static struct	thread *vmthread;

static struct kproc_desc vm_kp = {
	"vmdaemon",
	vm_daemon,
	&vmthread
};
SYSINIT(vmdaemon, SI_SUB_KTHREAD_VM, SI_ORDER_FIRST, kproc_start, &vm_kp)
#endif


int vm_pages_needed=0;		/* Event on which pageout daemon sleeps */
int vm_pageout_deficit=0;	/* Estimated number of pages deficit */
int vm_pageout_pages_needed=0;	/* flag saying that the pageout daemon needs pages */

#if !defined(NO_SWAPPING)
static int vm_pageout_req_swapout;	/* XXX */
static int vm_daemon_needed;
#endif
static int vm_max_launder = 32;
static int vm_pageout_stats_max=0, vm_pageout_stats_interval = 0;
static int vm_pageout_full_stats_interval = 0;
static int vm_pageout_stats_free_max=0, vm_pageout_algorithm=0;
static int defer_swap_pageouts=0;
static int disable_swap_pageouts=0;

#if defined(NO_SWAPPING)
static int vm_swap_enabled=0;
static int vm_swap_idle_enabled=0;
#else
static int vm_swap_enabled=1;
static int vm_swap_idle_enabled=0;
#endif

SYSCTL_INT(_vm, VM_PAGEOUT_ALGORITHM, pageout_algorithm,
	CTLFLAG_RW, &vm_pageout_algorithm, 0, "LRU page mgmt");

SYSCTL_INT(_vm, OID_AUTO, max_launder,
	CTLFLAG_RW, &vm_max_launder, 0, "Limit dirty flushes in pageout");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_max,
	CTLFLAG_RW, &vm_pageout_stats_max, 0, "Max pageout stats scan length");

SYSCTL_INT(_vm, OID_AUTO, pageout_full_stats_interval,
	CTLFLAG_RW, &vm_pageout_full_stats_interval, 0, "Interval for full stats scan");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_interval,
	CTLFLAG_RW, &vm_pageout_stats_interval, 0, "Interval for partial stats scan");

SYSCTL_INT(_vm, OID_AUTO, pageout_stats_free_max,
	CTLFLAG_RW, &vm_pageout_stats_free_max, 0, "Not implemented");

#if defined(NO_SWAPPING)
SYSCTL_INT(_vm, VM_SWAPPING_ENABLED, swap_enabled,
	CTLFLAG_RD, &vm_swap_enabled, 0, "");
SYSCTL_INT(_vm, OID_AUTO, swap_idle_enabled,
	CTLFLAG_RD, &vm_swap_idle_enabled, 0, "");
#else
SYSCTL_INT(_vm, VM_SWAPPING_ENABLED, swap_enabled,
	CTLFLAG_RW, &vm_swap_enabled, 0, "Enable entire process swapout");
SYSCTL_INT(_vm, OID_AUTO, swap_idle_enabled,
	CTLFLAG_RW, &vm_swap_idle_enabled, 0, "Allow swapout on idle criteria");
#endif

SYSCTL_INT(_vm, OID_AUTO, defer_swapspace_pageouts,
	CTLFLAG_RW, &defer_swap_pageouts, 0, "Give preference to dirty pages in mem");

SYSCTL_INT(_vm, OID_AUTO, disable_swapspace_pageouts,
	CTLFLAG_RW, &disable_swap_pageouts, 0, "Disallow swapout of dirty pages");

static int pageout_lock_miss;
SYSCTL_INT(_vm, OID_AUTO, pageout_lock_miss,
	CTLFLAG_RD, &pageout_lock_miss, 0, "vget() lock misses during pageout");

int vm_load;
SYSCTL_INT(_vm, OID_AUTO, vm_load,
	CTLFLAG_RD, &vm_load, 0, "load on the VM system");
int vm_load_enable = 1;
SYSCTL_INT(_vm, OID_AUTO, vm_load_enable,
	CTLFLAG_RW, &vm_load_enable, 0, "enable vm_load rate limiting");
#ifdef INVARIANTS
int vm_load_debug;
SYSCTL_INT(_vm, OID_AUTO, vm_load_debug,
	CTLFLAG_RW, &vm_load_debug, 0, "debug vm_load");
#endif

#define VM_PAGEOUT_PAGE_COUNT 16
int vm_pageout_page_count = VM_PAGEOUT_PAGE_COUNT;

int vm_page_max_wired;		/* XXX max # of wired pages system-wide */

#if !defined(NO_SWAPPING)
typedef void freeer_fcn_t (vm_map_t, vm_object_t, vm_pindex_t, int);
static void vm_pageout_map_deactivate_pages (vm_map_t, vm_pindex_t);
static freeer_fcn_t vm_pageout_object_deactivate_pages;
static void vm_req_vmdaemon (void);
#endif
static void vm_pageout_page_stats(void);

/*
 * Update vm_load to slow down faulting processes.
 *
 * SMP races ok.
 * No requirements.
 */
void
vm_fault_ratecheck(void)
{
	if (vm_pages_needed) {
		if (vm_load < 1000)
			++vm_load;
	} else {
		if (vm_load > 0)
			--vm_load;
	}
}

/*
 * vm_pageout_clean:
 *
 * Clean the page and remove it from the laundry.  The page must not be
 * busy on-call.
 * 
 * We set the busy bit to cause potential page faults on this page to
 * block.  Note the careful timing, however, the busy bit isn't set till
 * late and we cannot do anything that will mess with the page.
 *
 * The caller must hold vm_token.
 */
static int
vm_pageout_clean(vm_page_t m)
{
	vm_object_t object;
	vm_page_t mc[2*vm_pageout_page_count];
	int pageout_count;
	int ib, is, page_base;
	vm_pindex_t pindex = m->pindex;

	object = m->object;

	/*
	 * It doesn't cost us anything to pageout OBJT_DEFAULT or OBJT_SWAP
	 * with the new swapper, but we could have serious problems paging
	 * out other object types if there is insufficient memory.  
	 *
	 * Unfortunately, checking free memory here is far too late, so the
	 * check has been moved up a procedural level.
	 */

	/*
	 * Don't mess with the page if it's busy, held, or special
	 */
	if ((m->hold_count != 0) ||
	    ((m->busy != 0) || (m->flags & (PG_BUSY|PG_UNMANAGED)))) {
		return 0;
	}

	mc[vm_pageout_page_count] = m;
	pageout_count = 1;
	page_base = vm_pageout_page_count;
	ib = 1;
	is = 1;

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
more:
	while (ib && pageout_count < vm_pageout_page_count) {
		vm_page_t p;

		if (ib > pindex) {
			ib = 0;
			break;
		}

		if ((p = vm_page_lookup(object, pindex - ib)) == NULL) {
			ib = 0;
			break;
		}
		if (((p->queue - p->pc) == PQ_CACHE) ||
		    (p->flags & (PG_BUSY|PG_UNMANAGED)) || p->busy) {
			ib = 0;
			break;
		}
		vm_page_test_dirty(p);
		if ((p->dirty & p->valid) == 0 ||
		    p->queue != PQ_INACTIVE ||
		    p->wire_count != 0 ||	/* may be held by buf cache */
		    p->hold_count != 0) {	/* may be undergoing I/O */
			ib = 0;
			break;
		}
		mc[--page_base] = p;
		++pageout_count;
		++ib;
		/*
		 * alignment boundry, stop here and switch directions.  Do
		 * not clear ib.
		 */
		if ((pindex - (ib - 1)) % vm_pageout_page_count == 0)
			break;
	}

	while (pageout_count < vm_pageout_page_count && 
	    pindex + is < object->size) {
		vm_page_t p;

		if ((p = vm_page_lookup(object, pindex + is)) == NULL)
			break;
		if (((p->queue - p->pc) == PQ_CACHE) ||
		    (p->flags & (PG_BUSY|PG_UNMANAGED)) || p->busy) {
			break;
		}
		vm_page_test_dirty(p);
		if ((p->dirty & p->valid) == 0 ||
		    p->queue != PQ_INACTIVE ||
		    p->wire_count != 0 ||	/* may be held by buf cache */
		    p->hold_count != 0) {	/* may be undergoing I/O */
			break;
		}
		mc[page_base + pageout_count] = p;
		++pageout_count;
		++is;
	}

	/*
	 * If we exhausted our forward scan, continue with the reverse scan
	 * when possible, even past a page boundry.  This catches boundry
	 * conditions.
	 */
	if (ib && pageout_count < vm_pageout_page_count)
		goto more;

	vm_object_drop(object);

	/*
	 * we allow reads during pageouts...
	 */
	return vm_pageout_flush(&mc[page_base], pageout_count, 0);
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
 * The caller must hold vm_token.
 */
int
vm_pageout_flush(vm_page_t *mc, int count, int flags)
{
	vm_object_t object;
	int pageout_status[count];
	int numpagedout = 0;
	int i;

	ASSERT_LWKT_TOKEN_HELD(&vm_token);

	/*
	 * Initiate I/O.  Bump the vm_page_t->busy counter.
	 */
	for (i = 0; i < count; i++) {
		KASSERT(mc[i]->valid == VM_PAGE_BITS_ALL, ("vm_pageout_flush page %p index %d/%d: partially invalid page", mc[i], i, count));
		vm_page_io_start(mc[i]);
	}

	/*
	 * We must make the pages read-only.  This will also force the
	 * modified bit in the related pmaps to be cleared.  The pager
	 * cannot clear the bit for us since the I/O completion code
	 * typically runs from an interrupt.  The act of making the page
	 * read-only handles the case for us.
	 */
	for (i = 0; i < count; i++) {
		vm_page_protect(mc[i], VM_PROT_READ);
	}

	object = mc[0]->object;
	vm_object_pip_add(object, count);

	vm_pager_put_pages(object, mc, count,
	    (flags | ((object == &kernel_object) ? VM_PAGER_PUT_SYNC : 0)),
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
			pmap_clear_modify(mt);
			vm_page_undirty(mt);
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
		 * If the operation is still going, leave the page busy to
		 * block all other accesses. Also, leave the paging in
		 * progress indicator set so that we don't attempt an object
		 * collapse.
		 *
		 * For any pages which have completed synchronously, 
		 * deactivate the page if we are under a severe deficit.
		 * Do not try to enter them into the cache, though, they
		 * might still be read-heavy.
		 */
		if (pageout_status[i] != VM_PAGER_PEND) {
			if (vm_page_count_severe())
				vm_page_deactivate(mt);
#if 0
			if (!vm_page_count_severe() || !vm_page_try_to_cache(mt))
				vm_page_protect(mt, VM_PROT_READ);
#endif
			vm_page_io_finish(mt);
			vm_object_pip_wakeup(object);
		}
	}
	return numpagedout;
}

#if !defined(NO_SWAPPING)
/*
 *	vm_pageout_object_deactivate_pages
 *
 *	deactivate enough pages to satisfy the inactive target
 *	requirements or if vm_page_proc_limit is set, then
 *	deactivate all of the pages in the object and its
 *	backing_objects.
 *
 * The map must be locked.
 * The caller must hold vm_token.
 * The caller must hold the vm_object.
 */
static int vm_pageout_object_deactivate_pages_callback(vm_page_t, void *);

static void
vm_pageout_object_deactivate_pages(vm_map_t map, vm_object_t object,
				   vm_pindex_t desired, int map_remove_only)
{
	struct rb_vm_page_scan_info info;
	vm_object_t tmp;
	int remove_mode;

	while (object) {
		if (pmap_resident_count(vm_map_pmap(map)) <= desired)
			return;

		vm_object_hold(object);

		if (object->type == OBJT_DEVICE || object->type == OBJT_PHYS) {
			vm_object_drop(object);
			return;
		}
		if (object->paging_in_progress) {
			vm_object_drop(object);
			return;
		}

		remove_mode = map_remove_only;
		if (object->shadow_count > 1)
			remove_mode = 1;

		/*
		 * scan the objects entire memory queue.  spl protection is
		 * required to avoid an interrupt unbusy/free race against
		 * our busy check.
		 */
		crit_enter();
		info.limit = remove_mode;
		info.map = map;
		info.desired = desired;
		vm_page_rb_tree_RB_SCAN(&object->rb_memq, NULL,
				vm_pageout_object_deactivate_pages_callback,
				&info
		);
		crit_exit();
		tmp = object->backing_object;
		vm_object_drop(object);
		object = tmp;
	}
}

/*
 * The caller must hold vm_token.
 * The caller must hold the vm_object.
 */
static int
vm_pageout_object_deactivate_pages_callback(vm_page_t p, void *data)
{
	struct rb_vm_page_scan_info *info = data;
	int actcount;

	if (pmap_resident_count(vm_map_pmap(info->map)) <= info->desired) {
		return(-1);
	}
	mycpu->gd_cnt.v_pdpages++;
	if (p->wire_count != 0 || p->hold_count != 0 || p->busy != 0 ||
	    (p->flags & (PG_BUSY|PG_UNMANAGED)) ||
	    !pmap_page_exists_quick(vm_map_pmap(info->map), p)) {
		return(0);
	}

	actcount = pmap_ts_referenced(p);
	if (actcount) {
		vm_page_flag_set(p, PG_REFERENCED);
	} else if (p->flags & PG_REFERENCED) {
		actcount = 1;
	}

	if ((p->queue != PQ_ACTIVE) &&
		(p->flags & PG_REFERENCED)) {
		vm_page_activate(p);
		p->act_count += actcount;
		vm_page_flag_clear(p, PG_REFERENCED);
	} else if (p->queue == PQ_ACTIVE) {
		if ((p->flags & PG_REFERENCED) == 0) {
			p->act_count -= min(p->act_count, ACT_DECLINE);
			if (!info->limit && (vm_pageout_algorithm || (p->act_count == 0))) {
				vm_page_busy(p);
				vm_page_protect(p, VM_PROT_NONE);
				vm_page_deactivate(p);
				vm_page_wakeup(p);
			} else {
				TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, p, pageq);
				TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, p, pageq);
			}
		} else {
			vm_page_activate(p);
			vm_page_flag_clear(p, PG_REFERENCED);
			if (p->act_count < (ACT_MAX - ACT_ADVANCE))
				p->act_count += ACT_ADVANCE;
			TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, p, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, p, pageq);
		}
	} else if (p->queue == PQ_INACTIVE) {
		vm_page_busy(p);
		vm_page_protect(p, VM_PROT_NONE);
		vm_page_wakeup(p);
	}
	return(0);
}

/*
 * Deactivate some number of pages in a map, try to do it fairly, but
 * that is really hard to do.
 *
 * The caller must hold vm_token.
 */
static void
vm_pageout_map_deactivate_pages(vm_map_t map, vm_pindex_t desired)
{
	vm_map_entry_t tmpe;
	vm_object_t obj, bigobj;
	int nothingwired;

	if (lockmgr(&map->lock, LK_EXCLUSIVE | LK_NOWAIT)) {
		return;
	}

	bigobj = NULL;
	nothingwired = TRUE;

	/*
	 * first, search out the biggest object, and try to free pages from
	 * that.
	 */
	tmpe = map->header.next;
	while (tmpe != &map->header) {
		switch(tmpe->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			obj = tmpe->object.vm_object;
			if ((obj != NULL) && (obj->shadow_count <= 1) &&
				((bigobj == NULL) ||
				 (bigobj->resident_page_count < obj->resident_page_count))) {
				bigobj = obj;
			}
			break;
		default:
			break;
		}
		if (tmpe->wired_count > 0)
			nothingwired = FALSE;
		tmpe = tmpe->next;
	}

	if (bigobj) 
		vm_pageout_object_deactivate_pages(map, bigobj, desired, 0);

	/*
	 * Next, hunt around for other pages to deactivate.  We actually
	 * do this search sort of wrong -- .text first is not the best idea.
	 */
	tmpe = map->header.next;
	while (tmpe != &map->header) {
		if (pmap_resident_count(vm_map_pmap(map)) <= desired)
			break;
		switch(tmpe->maptype) {
		case VM_MAPTYPE_NORMAL:
		case VM_MAPTYPE_VPAGETABLE:
			obj = tmpe->object.vm_object;
			if (obj) 
				vm_pageout_object_deactivate_pages(map, obj, desired, 0);
			break;
		default:
			break;
		}
		tmpe = tmpe->next;
	};

	/*
	 * Remove all mappings if a process is swapped out, this will free page
	 * table pages.
	 */
	if (desired == 0 && nothingwired)
		pmap_remove(vm_map_pmap(map),
			    VM_MIN_USER_ADDRESS, VM_MAX_USER_ADDRESS);
	vm_map_unlock(map);
}
#endif

/*
 * Don't try to be fancy - being fancy can lead to vnode deadlocks.   We
 * only do it for OBJT_DEFAULT and OBJT_SWAP objects which we know can
 * be trivially freed.
 *
 * The caller must hold vm_token.
 *
 * WARNING: vm_object_reference() can block.
 */
static void
vm_pageout_page_free(vm_page_t m) 
{
	vm_object_t object = m->object;
	int type = object->type;

	vm_page_busy(m);
	if (type == OBJT_SWAP || type == OBJT_DEFAULT)
		vm_object_reference(object);
	vm_page_protect(m, VM_PROT_NONE);
	vm_page_free(m);
	if (type == OBJT_SWAP || type == OBJT_DEFAULT)
		vm_object_deallocate(object);
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
 * The caller must hold vm_token.
 */
static int
vm_pageout_scan(int pass)
{
	struct vm_pageout_scan_info info;
	vm_page_t m, next;
	struct vm_page marker;
	struct vnode *vpfailed;		/* warning, allowed to be stale */
	int maxscan, pcount;
	int recycle_count;
	int inactive_shortage, active_shortage;
	int inactive_original_shortage;
	vm_object_t object;
	int actcount;
	int vnodes_skipped = 0;
	int maxlaunder;

	/*
	 * Do whatever cleanup that the pmap code can.
	 */
	pmap_collect();

	/*
	 * Calculate our target for the number of free+cache pages we
	 * want to get to.  This is higher then the number that causes
	 * allocations to stall (severe) in order to provide hysteresis,
	 * and if we don't make it all the way but get to the minimum
	 * we're happy.
	 */
	inactive_shortage = vm_paging_target() + vm_pageout_deficit;
	inactive_original_shortage = inactive_shortage;
	vm_pageout_deficit = 0;

	/*
	 * Initialize our marker
	 */
	bzero(&marker, sizeof(marker));
	marker.flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
	marker.queue = PQ_INACTIVE;
	marker.wire_count = 1;

	/*
	 * Start scanning the inactive queue for pages we can move to the
	 * cache or free.  The scan will stop when the target is reached or
	 * we have scanned the entire inactive queue.  Note that m->act_count
	 * is not used to form decisions for the inactive queue, only for the
	 * active queue.
	 *
	 * maxlaunder limits the number of dirty pages we flush per scan.
	 * For most systems a smaller value (16 or 32) is more robust under
	 * extreme memory and disk pressure because any unnecessary writes
	 * to disk can result in extreme performance degredation.  However,
	 * systems with excessive dirty pages (especially when MAP_NOSYNC is
	 * used) will die horribly with limited laundering.  If the pageout
	 * daemon cannot clean enough pages in the first pass, we let it go
	 * all out in succeeding passes.
	 */
	if ((maxlaunder = vm_max_launder) <= 1)
		maxlaunder = 1;
	if (pass)
		maxlaunder = 10000;

	/*
	 * We will generally be in a critical section throughout the 
	 * scan, but we can release it temporarily when we are sitting on a
	 * non-busy page without fear.  this is required to prevent an
	 * interrupt from unbusying or freeing a page prior to our busy
	 * check, leaving us on the wrong queue or checking the wrong
	 * page.
	 */
	crit_enter();
rescan0:
	vpfailed = NULL;
	maxscan = vmstats.v_inactive_count;
	for (m = TAILQ_FIRST(&vm_page_queues[PQ_INACTIVE].pl);
	     m != NULL && maxscan-- > 0 && inactive_shortage > 0;
	     m = next
	 ) {
		mycpu->gd_cnt.v_pdpages++;

		/*
		 * Give interrupts a chance
		 */
		crit_exit();
		crit_enter();

		/*
		 * It's easier for some of the conditions below to just loop
		 * and catch queue changes here rather then check everywhere
		 * else.
		 */
		if (m->queue != PQ_INACTIVE)
			goto rescan0;
		next = TAILQ_NEXT(m, pageq);

		/*
		 * skip marker pages
		 */
		if (m->flags & PG_MARKER)
			continue;

		/*
		 * A held page may be undergoing I/O, so skip it.
		 */
		if (m->hold_count) {
			TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
			++vm_swapcache_inactive_heuristic;
			continue;
		}

		/*
		 * Dont mess with busy pages, keep in the front of the
		 * queue, most likely are being paged out.
		 */
		if (m->busy || (m->flags & PG_BUSY)) {
			continue;
		}

		if (m->object->ref_count == 0) {
			/*
			 * If the object is not being used, we ignore previous 
			 * references.
			 */
			vm_page_flag_clear(m, PG_REFERENCED);
			pmap_clear_reference(m);

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
			vm_page_activate(m);
			m->act_count += (actcount + ACT_ADVANCE);
			continue;
		}

		/*
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
			continue;
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

		if (m->valid == 0) {
			/*
			 * Invalid pages can be easily freed
			 */
			vm_pageout_page_free(m);
			mycpu->gd_cnt.v_dfree++;
			--inactive_shortage;
		} else if (m->dirty == 0) {
			/*
			 * Clean pages can be placed onto the cache queue.
			 * This effectively frees them.
			 */
			vm_page_busy(m);
			vm_page_cache(m);
			--inactive_shortage;
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
			vm_page_flag_set(m, PG_WINATCFLS);
			TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
			++vm_swapcache_inactive_heuristic;
		} else if (maxlaunder > 0) {
			/*
			 * We always want to try to flush some dirty pages if
			 * we encounter them, to keep the system stable.
			 * Normally this number is small, but under extreme
			 * pressure where there are insufficient clean pages
			 * on the inactive queue, we may have to go all out.
			 */
			int swap_pageouts_ok;
			struct vnode *vp = NULL;

			object = m->object;

			if ((object->type != OBJT_SWAP) && (object->type != OBJT_DEFAULT)) {
				swap_pageouts_ok = 1;
			} else {
				swap_pageouts_ok = !(defer_swap_pageouts || disable_swap_pageouts);
				swap_pageouts_ok |= (!disable_swap_pageouts && defer_swap_pageouts &&
				vm_page_count_min(0));
										
			}

			/*
			 * We don't bother paging objects that are "dead".  
			 * Those objects are in a "rundown" state.
			 */
			if (!swap_pageouts_ok || (object->flags & OBJ_DEAD)) {
				TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
				TAILQ_INSERT_TAIL(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
				++vm_swapcache_inactive_heuristic;
				continue;
			}

			/*
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
				flags = LK_EXCLUSIVE | LK_NOOBJ;
				if (vp == vpfailed)
					flags |= LK_NOWAIT;
				else
					flags |= LK_TIMELOCK;
				if (vget(vp, flags) != 0) {
					vpfailed = vp;
					++pageout_lock_miss;
					if (object->flags & OBJ_MIGHTBEDIRTY)
						    vnodes_skipped++;
					continue;
				}

				/*
				 * The page might have been moved to another
				 * queue during potential blocking in vget()
				 * above.  The page might have been freed and
				 * reused for another vnode.  The object might
				 * have been reused for another vnode.
				 */
				if (m->queue != PQ_INACTIVE ||
				    m->object != object ||
				    object->handle != vp) {
					if (object->flags & OBJ_MIGHTBEDIRTY)
						vnodes_skipped++;
					vput(vp);
					continue;
				}
	
				/*
				 * The page may have been busied during the
				 * blocking in vput();  We don't move the
				 * page back onto the end of the queue so that
				 * statistics are more correct if we don't.
				 */
				if (m->busy || (m->flags & PG_BUSY)) {
					vput(vp);
					continue;
				}

				/*
				 * If the page has become held it might
				 * be undergoing I/O, so skip it
				 */
				if (m->hold_count) {
					TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
					TAILQ_INSERT_TAIL(&vm_page_queues[PQ_INACTIVE].pl, m, pageq);
					++vm_swapcache_inactive_heuristic;
					if (object->flags & OBJ_MIGHTBEDIRTY)
						vnodes_skipped++;
					vput(vp);
					continue;
				}
			}

			/*
			 * If a page is dirty, then it is either being washed
			 * (but not yet cleaned) or it is still in the
			 * laundry.  If it is still in the laundry, then we
			 * start the cleaning operation. 
			 *
			 * This operation may cluster, invalidating the 'next'
			 * pointer.  To prevent an inordinate number of
			 * restarts we use our marker to remember our place.
			 *
			 * decrement inactive_shortage on success to account
			 * for the (future) cleaned page.  Otherwise we
			 * could wind up laundering or cleaning too many
			 * pages.
			 */
			TAILQ_INSERT_AFTER(&vm_page_queues[PQ_INACTIVE].pl, m, &marker, pageq);
			if (vm_pageout_clean(m) != 0) {
				--inactive_shortage;
				--maxlaunder;
			}
			next = TAILQ_NEXT(&marker, pageq);
			TAILQ_REMOVE(&vm_page_queues[PQ_INACTIVE].pl, &marker, pageq);
			if (vp != NULL)
				vput(vp);
		}
	}

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
	 */
	active_shortage = vmstats.v_inactive_target - vmstats.v_inactive_count;
	if (inactive_original_shortage < vmstats.v_inactive_target / 10)
		inactive_original_shortage = vmstats.v_inactive_target / 10;
	if (inactive_shortage <= 0 &&
	    active_shortage > inactive_original_shortage * 2) {
		active_shortage = inactive_original_shortage * 2;
	}

	pcount = vmstats.v_active_count;
	recycle_count = 0;
	m = TAILQ_FIRST(&vm_page_queues[PQ_ACTIVE].pl);

	while ((m != NULL) && (pcount-- > 0) &&
	       (inactive_shortage > 0 || active_shortage > 0)
	) {
		/*
		 * Give interrupts a chance.
		 */
		crit_exit();
		crit_enter();

		/*
		 * If the page was ripped out from under us, just stop.
		 */
		if (m->queue != PQ_ACTIVE)
			break;
		next = TAILQ_NEXT(m, pageq);

		/*
		 * Don't deactivate pages that are busy.
		 */
		if ((m->busy != 0) ||
		    (m->flags & PG_BUSY) ||
		    (m->hold_count != 0)) {
			TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			m = next;
			continue;
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
		if (m->object->ref_count != 0) {
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
		 */
		if (actcount && m->object->ref_count != 0) {
			TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
		} else {
			m->act_count -= min(m->act_count, ACT_DECLINE);
			if (vm_pageout_algorithm ||
			    m->object->ref_count == 0 ||
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
				--active_shortage;
				if (inactive_shortage > 0 ||
				    m->object->ref_count == 0) {
					if (inactive_shortage > 0)
						++recycle_count;
					vm_page_busy(m);
					vm_page_protect(m, VM_PROT_NONE);
					if (m->dirty == 0 &&
					    inactive_shortage > 0) {
						--inactive_shortage;
						vm_page_cache(m);
					} else {
						vm_page_deactivate(m);
						vm_page_wakeup(m);
					}
				} else {
					vm_page_deactivate(m);
				}
			} else {
				TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
				TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			}
		}
		m = next;
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
	 */
	while (vmstats.v_free_count <
	       (vmstats.v_free_min + vmstats.v_free_target) / 2) {
		/*
		 *
		 */
		static int cache_rover = 0;
		m = vm_page_list_find(PQ_CACHE, cache_rover, FALSE);
		if (m == NULL)
			break;
		if ((m->flags & (PG_BUSY|PG_UNMANAGED)) || 
		    m->busy || 
		    m->hold_count || 
		    m->wire_count) {
#ifdef INVARIANTS
			kprintf("Warning: busy page %p found in cache\n", m);
#endif
			vm_page_deactivate(m);
			continue;
		}
		KKASSERT((m->flags & PG_MAPPED) == 0);
		KKASSERT(m->dirty == 0);
		cache_rover = (cache_rover + PQ_PRIME2) & PQ_L2_MASK;
		vm_pageout_page_free(m);
		mycpu->gd_cnt.v_dfree++;
	}

	crit_exit();

#if !defined(NO_SWAPPING)
	/*
	 * Idle process swapout -- run once per second.
	 */
	if (vm_swap_idle_enabled) {
		static long lsec;
		if (time_second != lsec) {
			vm_pageout_req_swapout |= VM_SWAP_IDLE;
			vm_req_vmdaemon();
			lsec = time_second;
		}
	}
#endif
		
	/*
	 * If we didn't get enough free pages, and we have skipped a vnode
	 * in a writeable object, wakeup the sync daemon.  And kick swapout
	 * if we did not get enough free pages.
	 */
	if (vm_paging_target() > 0) {
		if (vnodes_skipped && vm_page_count_min(0))
			speedup_syncer();
#if !defined(NO_SWAPPING)
		if (vm_swap_enabled && vm_page_count_target()) {
			vm_req_vmdaemon();
			vm_pageout_req_swapout |= VM_SWAP_NORMAL;
		}
#endif
	}

	/*
	 * Handle catastrophic conditions.  Under good conditions we should
	 * be at the target, well beyond our minimum.  If we could not even
	 * reach our minimum the system is under heavy stress.
	 *
	 * Determine whether we have run out of memory.  This occurs when
	 * swap_pager_full is TRUE and the only pages left in the page
	 * queues are dirty.  We will still likely have page shortages.
	 *
	 * - swap_pager_full is set if insufficient swap was
	 *   available to satisfy a requested pageout.
	 *
	 * - the inactive queue is bloated (4 x size of active queue),
	 *   meaning it is unable to get rid of dirty pages and.
	 *
	 * - vm_page_count_min() without counting pages recycled from the
	 *   active queue (recycle_count) means we could not recover
	 *   enough pages to meet bare minimum needs.  This test only
	 *   works if the inactive queue is bloated.
	 *
	 * - due to a positive inactive_shortage we shifted the remaining
	 *   dirty pages from the active queue to the inactive queue
	 *   trying to find clean ones to free.
	 */
	if (swap_pager_full && vm_page_count_min(recycle_count))
		kprintf("Warning: system low on memory+swap!\n");
	if (swap_pager_full && vm_page_count_min(recycle_count) &&
	    vmstats.v_inactive_count > vmstats.v_active_count * 4 &&
	    inactive_shortage > 0) {
		/*
		 * Kill something.
		 */
		info.bigproc = NULL;
		info.bigsize = 0;
		allproc_scan(vm_pageout_scan_callback, &info);
		if (info.bigproc != NULL) {
			killproc(info.bigproc, "out of swap space");
			info.bigproc->p_nice = PRIO_MIN;
			info.bigproc->p_usched->resetpriority(
				FIRST_LWP_IN_PROC(info.bigproc));
			wakeup(&vmstats.v_free_count);
			PRELE(info.bigproc);
		}
	}
	return(inactive_shortage);
}

/*
 * The caller must hold vm_token and proc_token.
 */
static int
vm_pageout_scan_callback(struct proc *p, void *data)
{
	struct vm_pageout_scan_info *info = data;
	vm_offset_t size;

	/*
	 * Never kill system processes or init.  If we have configured swap
	 * then try to avoid killing low-numbered pids.
	 */
	if ((p->p_flag & P_SYSTEM) || (p->p_pid == 1) ||
	    ((p->p_pid < 48) && (vm_swap_size != 0))) {
		return (0);
	}

	/*
	 * if the process is in a non-running type state,
	 * don't touch it.
	 */
	if (p->p_stat != SACTIVE && p->p_stat != SSTOP)
		return (0);

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
	return(0);
}

/*
 * This routine tries to maintain the pseudo LRU active queue,
 * so that during long periods of time where there is no paging,
 * that some statistic accumulation still occurs.  This code
 * helps the situation where paging just starts to occur.
 *
 * The caller must hold vm_token.
 */
static void
vm_pageout_page_stats(void)
{
	vm_page_t m,next;
	int pcount,tpcount;		/* Number of pages to check */
	static int fullintervalcount = 0;
	int page_shortage;

	page_shortage = 
	    (vmstats.v_inactive_target + vmstats.v_cache_max + vmstats.v_free_min) -
	    (vmstats.v_free_count + vmstats.v_inactive_count + vmstats.v_cache_count);

	if (page_shortage <= 0)
		return;

	crit_enter();

	pcount = vmstats.v_active_count;
	fullintervalcount += vm_pageout_stats_interval;
	if (fullintervalcount < vm_pageout_full_stats_interval) {
		tpcount = (vm_pageout_stats_max * vmstats.v_active_count) / vmstats.v_page_count;
		if (pcount > tpcount)
			pcount = tpcount;
	} else {
		fullintervalcount = 0;
	}

	m = TAILQ_FIRST(&vm_page_queues[PQ_ACTIVE].pl);
	while ((m != NULL) && (pcount-- > 0)) {
		int actcount;

		if (m->queue != PQ_ACTIVE) {
			break;
		}

		next = TAILQ_NEXT(m, pageq);
		/*
		 * Don't deactivate pages that are busy.
		 */
		if ((m->busy != 0) ||
		    (m->flags & PG_BUSY) ||
		    (m->hold_count != 0)) {
			TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			m = next;
			continue;
		}

		actcount = 0;
		if (m->flags & PG_REFERENCED) {
			vm_page_flag_clear(m, PG_REFERENCED);
			actcount += 1;
		}

		actcount += pmap_ts_referenced(m);
		if (actcount) {
			m->act_count += ACT_ADVANCE + actcount;
			if (m->act_count > ACT_MAX)
				m->act_count = ACT_MAX;
			TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
		} else {
			if (m->act_count == 0) {
				/*
				 * We turn off page access, so that we have
				 * more accurate RSS stats.  We don't do this
				 * in the normal page deactivation when the
				 * system is loaded VM wise, because the
				 * cost of the large number of page protect
				 * operations would be higher than the value
				 * of doing the operation.
				 */
				vm_page_busy(m);
				vm_page_protect(m, VM_PROT_NONE);
				vm_page_deactivate(m);
				vm_page_wakeup(m);
			} else {
				m->act_count -= min(m->act_count, ACT_DECLINE);
				TAILQ_REMOVE(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
				TAILQ_INSERT_TAIL(&vm_page_queues[PQ_ACTIVE].pl, m, pageq);
			}
		}

		m = next;
	}
	crit_exit();
}

/*
 * The caller must hold vm_token.
 */
static int
vm_pageout_free_page_calc(vm_size_t count)
{
	if (count < vmstats.v_page_count)
		 return 0;
	/*
	 * free_reserved needs to include enough for the largest swap pager
	 * structures plus enough for any pv_entry structs when paging.
	 */
	if (vmstats.v_page_count > 1024)
		vmstats.v_free_min = 4 + (vmstats.v_page_count - 1024) / 200;
	else
		vmstats.v_free_min = 4;
	vmstats.v_pageout_free_min = (2*MAXBSIZE)/PAGE_SIZE +
		vmstats.v_interrupt_free_min;
	vmstats.v_free_reserved = vm_pageout_page_count +
		vmstats.v_pageout_free_min + (count / 768) + PQ_L2_SIZE;
	vmstats.v_free_severe = vmstats.v_free_min / 2;
	vmstats.v_free_min += vmstats.v_free_reserved;
	vmstats.v_free_severe += vmstats.v_free_reserved;
	return 1;
}


/*
 * vm_pageout is the high level pageout daemon.
 *
 * No requirements.
 */
static void
vm_pageout_thread(void)
{
	int pass;
	int inactive_shortage;

	/*
	 * Permanently hold vm_token.
	 */
	lwkt_gettoken(&vm_token);

	/*
	 * Initialize some paging parameters.
	 */
	curthread->td_flags |= TDF_SYSTHREAD;

	vmstats.v_interrupt_free_min = 2;
	if (vmstats.v_page_count < 2000)
		vm_pageout_page_count = 8;

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
	if (vmstats.v_free_count > 6144)
		vmstats.v_free_target = 4 * vmstats.v_free_min + vmstats.v_free_reserved;
	else
		vmstats.v_free_target = 2 * vmstats.v_free_min + vmstats.v_free_reserved;

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

	/*
	 * The pageout daemon is never done, so loop forever.
	 */
	while (TRUE) {
		int error;

		/*
		 * Wait for an action request.  If we timeout check to
		 * see if paging is needed (in case the normal wakeup
		 * code raced us).
		 */
		if (vm_pages_needed == 0) {
			error = tsleep(&vm_pages_needed,
				       0, "psleep",
				       vm_pageout_stats_interval * hz);
			if (error &&
			    vm_paging_needed() == 0 &&
			    vm_pages_needed == 0) {
				vm_pageout_page_stats();
				continue;
			}
			vm_pages_needed = 1;
		}

		mycpu->gd_cnt.v_pdwakeups++;

		/*
		 * Scan for pageout.  Try to avoid thrashing the system
		 * with activity.
		 */
		inactive_shortage = vm_pageout_scan(pass);
		if (inactive_shortage > 0) {
			++pass;
			if (swap_pager_full) {
				/*
				 * Running out of memory, catastrophic back-off
				 * to one-second intervals.
				 */
				tsleep(&vm_pages_needed, 0, "pdelay", hz);
			} else if (pass < 10 && vm_pages_needed > 1) {
				/*
				 * Normal operation, additional processes
				 * have already kicked us.  Retry immediately.
				 */
			} else if (pass < 10) {
				/*
				 * Normal operation, fewer processes.  Delay
				 * a bit but allow wakeups.
				 */
				vm_pages_needed = 0;
				tsleep(&vm_pages_needed, 0, "pdelay", hz / 10);
				vm_pages_needed = 1;
			} else {
				/*
				 * We've taken too many passes, forced delay.
				 */
				tsleep(&vm_pages_needed, 0, "pdelay", hz / 10);
			}
		} else {
			/*
			 * Interlocked wakeup of waiters (non-optional)
			 */
			pass = 0;
			if (vm_pages_needed && !vm_page_count_min(0)) {
				wakeup(&vmstats.v_free_count);
				vm_pages_needed = 0;
			}
		}
	}
}

static struct kproc_desc page_kp = {
	"pagedaemon",
	vm_pageout_thread,
	&pagethread
};
SYSINIT(pagedaemon, SI_SUB_KTHREAD_PAGE, SI_ORDER_FIRST, kproc_start, &page_kp)


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
	if (vm_paging_needed() && curthread != pagethread) {
		if (vm_pages_needed == 0) {
			vm_pages_needed = 1;	/* SMP race ok */
			wakeup(&vm_pages_needed);
		} else if (vm_page_count_min(0)) {
			++vm_pages_needed;	/* SMP race ok */
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
 */
static void
vm_daemon(void)
{
	/*
	 * Permanently hold vm_token.
	 */
	lwkt_gettoken(&vm_token);

	while (TRUE) {
		tsleep(&vm_daemon_needed, 0, "psleep", 0);
		if (vm_pageout_req_swapout) {
			swapout_procs(vm_pageout_req_swapout);
			vm_pageout_req_swapout = 0;
		}
		/*
		 * scan the processes for exceeding their rlimits or if
		 * process is swapped out -- deactivate pages
		 */
		allproc_scan(vm_daemon_callback, NULL);
	}
}

/*
 * Caller must hold vm_token and proc_token.
 */
static int
vm_daemon_callback(struct proc *p, void *data __unused)
{
	vm_pindex_t limit, size;

	/*
	 * if this is a system process or if we have already
	 * looked at this process, skip it.
	 */
	if (p->p_flag & (P_SYSTEM | P_WEXIT))
		return (0);

	/*
	 * if the process is in a non-running type state,
	 * don't touch it.
	 */
	if (p->p_stat != SACTIVE && p->p_stat != SSTOP)
		return (0);

	/*
	 * get a limit
	 */
	limit = OFF_TO_IDX(qmin(p->p_rlimit[RLIMIT_RSS].rlim_cur,
			        p->p_rlimit[RLIMIT_RSS].rlim_max));

	/*
	 * let processes that are swapped out really be
	 * swapped out.  Set the limit to nothing to get as
	 * many pages out to swap as possible.
	 */
	if (p->p_flag & P_SWAPPEDOUT)
		limit = 0;

	size = vmspace_resident_count(p->p_vmspace);
	if (limit >= 0 && size >= limit) {
		vm_pageout_map_deactivate_pages(
		    &p->p_vmspace->vm_map, limit);
	}
	return (0);
}

#endif
