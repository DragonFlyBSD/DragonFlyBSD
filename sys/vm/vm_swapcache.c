/*
 * (MPSAFE)
 *
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 * Implement the swapcache daemon.  When enabled swap is assumed to be
 * configured on a fast storage device such as a SSD.  Swap is assigned
 * to clean vnode-backed pages in the inactive queue, clustered by object
 * if possible, and written out.  The swap assignment sticks around even
 * after the underlying pages have been recycled.
 *
 * The daemon manages write bandwidth based on sysctl settings to control
 * wear on the SSD.
 *
 * The vnode strategy code will check for the swap assignments and divert
 * reads to the swap device when the data is present in the swapcache.
 *
 * This operates on both regular files and the block device vnodes used by
 * filesystems to manage meta-data.
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

#define INACTIVE_LIST	(&vm_page_queues[PQ_INACTIVE].pl)

/* the kernel process "vm_pageout"*/
static int vm_swapcached_flush (vm_page_t m, int isblkdev);
static int vm_swapcache_test(vm_page_t m);
static void vm_swapcache_writing(vm_page_t marker);
static void vm_swapcache_cleaning(vm_object_t marker);
struct thread *swapcached_thread;

SYSCTL_NODE(_vm, OID_AUTO, swapcache, CTLFLAG_RW, NULL, NULL);

int vm_swapcache_read_enable;
int vm_swapcache_inactive_heuristic;
static int vm_swapcache_sleep;
static int vm_swapcache_maxlaunder = 256;
static int vm_swapcache_data_enable = 0;
static int vm_swapcache_meta_enable = 0;
static int vm_swapcache_maxswappct = 75;
static int vm_swapcache_hysteresis;
static int vm_swapcache_use_chflags = 1;	/* require chflags cache */
static int64_t vm_swapcache_minburst = 10000000LL;	/* 10MB */
static int64_t vm_swapcache_curburst = 4000000000LL;	/* 4G after boot */
static int64_t vm_swapcache_maxburst = 2000000000LL;	/* 2G nominal max */
static int64_t vm_swapcache_accrate = 100000LL;		/* 100K/s */
static int64_t vm_swapcache_write_count;
static int64_t vm_swapcache_maxfilesize;

SYSCTL_INT(_vm_swapcache, OID_AUTO, maxlaunder,
	CTLFLAG_RW, &vm_swapcache_maxlaunder, 0, "");

SYSCTL_INT(_vm_swapcache, OID_AUTO, data_enable,
	CTLFLAG_RW, &vm_swapcache_data_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, meta_enable,
	CTLFLAG_RW, &vm_swapcache_meta_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, read_enable,
	CTLFLAG_RW, &vm_swapcache_read_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, maxswappct,
	CTLFLAG_RW, &vm_swapcache_maxswappct, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, hysteresis,
	CTLFLAG_RW, &vm_swapcache_hysteresis, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, use_chflags,
	CTLFLAG_RW, &vm_swapcache_use_chflags, 0, "");

SYSCTL_QUAD(_vm_swapcache, OID_AUTO, minburst,
	CTLFLAG_RW, &vm_swapcache_minburst, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, curburst,
	CTLFLAG_RW, &vm_swapcache_curburst, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, maxburst,
	CTLFLAG_RW, &vm_swapcache_maxburst, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, maxfilesize,
	CTLFLAG_RW, &vm_swapcache_maxfilesize, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, accrate,
	CTLFLAG_RW, &vm_swapcache_accrate, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, write_count,
	CTLFLAG_RW, &vm_swapcache_write_count, 0, "");

#define SWAPMAX(adj)	\
	((int64_t)vm_swap_max * (vm_swapcache_maxswappct + (adj)) / 100)

/*
 * vm_swapcached is the high level pageout daemon.
 *
 * No requirements.
 */
static void
vm_swapcached_thread(void)
{
	enum { SWAPC_WRITING, SWAPC_CLEANING } state = SWAPC_WRITING;
	enum { SWAPB_BURSTING, SWAPB_RECOVERING } burst = SWAPB_BURSTING;
	struct vm_page page_marker;
	struct vm_object object_marker;

	/*
	 * Thread setup
	 */
	curthread->td_flags |= TDF_SYSTHREAD;

	lwkt_gettoken(&vm_token);
	crit_enter();

	/*
	 * Initialize our marker for the inactive scan (SWAPC_WRITING)
	 */
	bzero(&page_marker, sizeof(page_marker));
	page_marker.flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
	page_marker.queue = PQ_INACTIVE;
	page_marker.wire_count = 1;
	TAILQ_INSERT_HEAD(INACTIVE_LIST, &page_marker, pageq);
	vm_swapcache_hysteresis = vmstats.v_inactive_target / 2;
	vm_swapcache_inactive_heuristic = -vm_swapcache_hysteresis;

	/*
	 * Initialize our marker for the vm_object scan (SWAPC_CLEANING)
	 */
	bzero(&object_marker, sizeof(object_marker));
	object_marker.type = OBJT_MARKER;
	lwkt_gettoken(&vmobj_token);
	TAILQ_INSERT_HEAD(&vm_object_list, &object_marker, object_list);
	lwkt_reltoken(&vmobj_token);

	for (;;) {
		/*
		 * Check every 5 seconds when not enabled or if no swap
		 * is present.
		 */
		if ((vm_swapcache_data_enable == 0 &&
		     vm_swapcache_meta_enable == 0) ||
		    vm_swap_max == 0) {
			tsleep(&vm_swapcache_sleep, 0, "csleep", hz * 5);
			continue;
		}

		/*
		 * Polling rate when enabled is approximately 10 hz.
		 */
		tsleep(&vm_swapcache_sleep, 0, "csleep", hz / 10);

		/*
		 * State hysteresis.  Generate write activity up to 75% of
		 * swap, then clean out swap assignments down to 70%, then
		 * repeat.
		 */
		if (state == SWAPC_WRITING) {
			if (vm_swap_cache_use > SWAPMAX(0))
				state = SWAPC_CLEANING;
		} else {
			if (vm_swap_cache_use < SWAPMAX(-5))
				state = SWAPC_WRITING;
		}

		/*
		 * We are allowed to continue accumulating burst value
		 * in either state.  Allow the user to set curburst > maxburst
		 * for the initial load-in.
		 */
		if (vm_swapcache_curburst < vm_swapcache_maxburst) {
			vm_swapcache_curburst += vm_swapcache_accrate / 10;
			if (vm_swapcache_curburst > vm_swapcache_maxburst)
				vm_swapcache_curburst = vm_swapcache_maxburst;
		}

		/*
		 * We don't want to nickle-and-dime the scan as that will
		 * create unnecessary fragmentation.  The minimum burst
		 * is one-seconds worth of accumulation.
		 */
		if (state == SWAPC_WRITING) {
			if (vm_swapcache_curburst >= vm_swapcache_accrate) {
				if (burst == SWAPB_BURSTING) {
					vm_swapcache_writing(&page_marker);
					if (vm_swapcache_curburst <= 0)
						burst = SWAPB_RECOVERING;
				} else if (vm_swapcache_curburst >
					   vm_swapcache_minburst) {
					vm_swapcache_writing(&page_marker);
					burst = SWAPB_BURSTING;
				}
			}
		} else {
			vm_swapcache_cleaning(&object_marker);
		}
	}

	/*
	 * Cleanup (NOT REACHED)
	 */
	TAILQ_REMOVE(INACTIVE_LIST, &page_marker, pageq);
	crit_exit();
	lwkt_reltoken(&vm_token);

	lwkt_gettoken(&vmobj_token);
	TAILQ_REMOVE(&vm_object_list, &object_marker, object_list);
	lwkt_reltoken(&vmobj_token);
}

static struct kproc_desc swpc_kp = {
	"swapcached",
	vm_swapcached_thread,
	&swapcached_thread
};
SYSINIT(swapcached, SI_SUB_KTHREAD_PAGE, SI_ORDER_SECOND, kproc_start, &swpc_kp)

/*
 * The caller must hold vm_token.
 */
static void
vm_swapcache_writing(vm_page_t marker)
{
	vm_object_t object;
	struct vnode *vp;
	vm_page_t m;
	int count;
	int isblkdev;

	/*
	 * Deal with an overflow of the heuristic counter or if the user
	 * manually changes the hysteresis.
	 *
	 * Try to avoid small incremental pageouts by waiting for enough
	 * pages to buildup in the inactive queue to hopefully get a good
	 * burst in.  This heuristic is bumped by the VM system and reset
	 * when our scan hits the end of the queue.
	 */
	if (vm_swapcache_inactive_heuristic < -vm_swapcache_hysteresis)
		vm_swapcache_inactive_heuristic = -vm_swapcache_hysteresis;
	if (vm_swapcache_inactive_heuristic < 0)
		return;

	/*
	 * Scan the inactive queue from our marker to locate
	 * suitable pages to push to the swap cache.
	 *
	 * We are looking for clean vnode-backed pages.
	 *
	 * NOTE: PG_SWAPPED pages in particular are not part of
	 *	 our count because once the cache stabilizes we
	 *	 can end up with a very high datarate of VM pages
	 *	 cycling from it.
	 */
	m = marker;
	count = vm_swapcache_maxlaunder;

	while ((m = TAILQ_NEXT(m, pageq)) != NULL && count--) {
		if (m->flags & (PG_MARKER | PG_SWAPPED)) {
			++count;
			continue;
		}
		if (vm_swapcache_curburst < 0)
			break;
		if (vm_swapcache_test(m))
			continue;
		object = m->object;
		vp = object->handle;
		if (vp == NULL)
			continue;

		switch(vp->v_type) {
		case VREG:
			/*
			 * If data_enable is 0 do not try to swapcache data.
			 * If use_chflags is set then only swapcache data for
			 * VSWAPCACHE marked vnodes, otherwise any vnode.
			 */
			if (vm_swapcache_data_enable == 0 ||
			    ((vp->v_flag & VSWAPCACHE) == 0 &&
			     vm_swapcache_use_chflags)) {
				continue;
			}
			if (vm_swapcache_maxfilesize &&
			    object->size >
			    (vm_swapcache_maxfilesize >> PAGE_SHIFT)) {
				continue;
			}
			isblkdev = 0;
			break;
		case VCHR:
			/*
			 * The PG_NOTMETA flag only applies to pages
			 * associated with block devices.
			 */
			if (m->flags & PG_NOTMETA)
				continue;
			if (vm_swapcache_meta_enable == 0)
				continue;
			isblkdev = 1;
			break;
		default:
			continue;
		}

		/*
		 * Ok, move the marker and soft-busy the page.
		 */
		TAILQ_REMOVE(INACTIVE_LIST, marker, pageq);
		TAILQ_INSERT_AFTER(INACTIVE_LIST, m, marker, pageq);

		/*
		 * Assign swap and initiate I/O.
		 *
		 * (adjust for the --count which also occurs in the loop)
		 */
		count -= vm_swapcached_flush(m, isblkdev) - 1;

		/*
		 * Setup for next loop using marker.
		 */
		m = marker;
	}

	/*
	 * Cleanup marker position.  If we hit the end of the
	 * list the marker is placed at the tail.  Newly deactivated
	 * pages will be placed after it.
	 *
	 * Earlier inactive pages that were dirty and become clean
	 * are typically moved to the end of PQ_INACTIVE by virtue
	 * of vfs_vmio_release() when they become unwired from the
	 * buffer cache.
	 */
	TAILQ_REMOVE(INACTIVE_LIST, marker, pageq);
	if (m) {
		TAILQ_INSERT_BEFORE(m, marker, pageq);
	} else {
		TAILQ_INSERT_TAIL(INACTIVE_LIST, marker, pageq);
		vm_swapcache_inactive_heuristic = -vm_swapcache_hysteresis;
	}
}

/*
 * Flush the specified page using the swap_pager.
 *
 * Try to collect surrounding pages, including pages which may
 * have already been assigned swap.  Try to cluster within a
 * contiguous aligned SMAP_META_PAGES (typ 16 x PAGE_SIZE) block
 * to match what swap_pager_putpages() can do.
 *
 * We also want to try to match against the buffer cache blocksize
 * but we don't really know what it is here.  Since the buffer cache
 * wires and unwires pages in groups the fact that we skip wired pages
 * should be sufficient.
 *
 * Returns a count of pages we might have flushed (minimum 1)
 *
 * The caller must hold vm_token.
 */
static
int
vm_swapcached_flush(vm_page_t m, int isblkdev)
{
	vm_object_t object;
	vm_page_t marray[SWAP_META_PAGES];
	vm_pindex_t basei;
	int rtvals[SWAP_META_PAGES];
	int x;
	int i;
	int j;
	int count;

	vm_page_io_start(m);
	vm_page_protect(m, VM_PROT_READ);
	object = m->object;

	/*
	 * Try to cluster around (m), keeping in mind that the swap pager
	 * can only do SMAP_META_PAGES worth of continguous write.
	 */
	x = (int)m->pindex & SWAP_META_MASK;
	marray[x] = m;
	basei = m->pindex;

	for (i = x - 1; i >= 0; --i) {
		m = vm_page_lookup(object, basei - x + i);
		if (m == NULL)
			break;
		if (vm_swapcache_test(m))
			break;
		if (isblkdev && (m->flags & PG_NOTMETA))
			break;
		vm_page_io_start(m);
		vm_page_protect(m, VM_PROT_READ);
		if (m->queue - m->pc == PQ_CACHE) {
			vm_page_unqueue_nowakeup(m);
			vm_page_deactivate(m);
		}
		marray[i] = m;
	}
	++i;

	for (j = x + 1; j < SWAP_META_PAGES; ++j) {
		m = vm_page_lookup(object, basei - x + j);
		if (m == NULL)
			break;
		if (vm_swapcache_test(m))
			break;
		if (isblkdev && (m->flags & PG_NOTMETA))
			break;
		vm_page_io_start(m);
		vm_page_protect(m, VM_PROT_READ);
		if (m->queue - m->pc == PQ_CACHE) {
			vm_page_unqueue_nowakeup(m);
			vm_page_deactivate(m);
		}
		marray[j] = m;
	}

	count = j - i;
	vm_object_pip_add(object, count);
	swap_pager_putpages(object, marray + i, count, FALSE, rtvals + i);
	vm_swapcache_write_count += count * PAGE_SIZE;
	vm_swapcache_curburst -= count * PAGE_SIZE;

	while (i < j) {
		if (rtvals[i] != VM_PAGER_PEND) {
			vm_page_io_finish(marray[i]);
			vm_object_pip_wakeup(object);
		}
		++i;
	}
	return(count);
}

/*
 * Test whether a VM page is suitable for writing to the swapcache.
 * Does not test m->queue, PG_MARKER, or PG_SWAPPED.
 *
 * Returns 0 on success, 1 on failure
 *
 * The caller must hold vm_token.
 */
static int
vm_swapcache_test(vm_page_t m)
{
	vm_object_t object;

	if (m->flags & (PG_BUSY | PG_UNMANAGED))
		return(1);
	if (m->busy || m->hold_count || m->wire_count)
		return(1);
	if (m->valid != VM_PAGE_BITS_ALL)
		return(1);
	if (m->dirty & m->valid)
		return(1);
	if ((object = m->object) == NULL)
		return(1);
	if (object->type != OBJT_VNODE ||
	    (object->flags & OBJ_DEAD)) {
		return(1);
	}
	vm_page_test_dirty(m);
	if (m->dirty & m->valid)
		return(1);
	return(0);
}

/*
 * Cleaning pass
 *
 * The caller must hold vm_token.
 */
static
void
vm_swapcache_cleaning(vm_object_t marker)
{
	vm_object_t object;
	struct vnode *vp;
	int count;
	int n;

	object = marker;
	count = vm_swapcache_maxlaunder;

	/*
	 * Look for vnode objects
	 */
	lwkt_gettoken(&vm_token);
	lwkt_gettoken(&vmobj_token);

	while ((object = TAILQ_NEXT(object, object_list)) != NULL && count--) {
		if (object->type != OBJT_VNODE)
			continue;
		if ((object->flags & OBJ_DEAD) || object->swblock_count == 0)
			continue;
		if ((vp = object->handle) == NULL)
			continue;
		if (vp->v_type != VREG && vp->v_type != VCHR)
			continue;

		/*
		 * Adjust iterator.
		 */
		if (marker->backing_object != object)
			marker->size = 0;

		/*
		 * Move the marker so we can work on the VM object
		 */
		TAILQ_REMOVE(&vm_object_list, marker, object_list);
		TAILQ_INSERT_AFTER(&vm_object_list, object,
				   marker, object_list);

		/*
		 * Look for swblocks starting at our iterator.
		 *
		 * The swap_pager_condfree() function attempts to free
		 * swap space starting at the specified index.  The index
		 * will be updated on return.  The function will return
		 * a scan factor (NOT the number of blocks freed).
		 *
		 * If it must cut its scan of the object short due to an
		 * excessive number of swblocks, or is able to free the
		 * requested number of blocks, it will return n >= count
		 * and we break and pick it back up on a future attempt.
		 */
		n = swap_pager_condfree(object, &marker->size, count);
		count -= n;
		if (count < 0)
			break;

		/*
		 * Setup for loop.
		 */
		marker->size = 0;
		object = marker;
	}

	/*
	 * Adjust marker so we continue the scan from where we left off.
	 * When we reach the end we start back at the beginning.
	 */
	TAILQ_REMOVE(&vm_object_list, marker, object_list);
	if (object)
		TAILQ_INSERT_BEFORE(object, marker, object_list);
	else
		TAILQ_INSERT_HEAD(&vm_object_list, marker, object_list);
	marker->backing_object = object;

	lwkt_reltoken(&vmobj_token);
	lwkt_reltoken(&vm_token);
}
