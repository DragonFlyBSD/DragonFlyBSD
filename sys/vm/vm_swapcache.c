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
#include <sys/eventhandler.h>

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
#include <sys/spinlock2.h>
#include <vm/vm_page2.h>

/* the kernel process "vm_pageout"*/
static int vm_swapcached_flush (vm_page_t m, int isblkdev);
static int vm_swapcache_test(vm_page_t m);
static int vm_swapcache_writing_heuristic(void);
static int vm_swapcache_writing(vm_page_t marker, int count, int scount);
static void vm_swapcache_cleaning(vm_object_t marker, int *swindexp);
static void vm_swapcache_movemarker(vm_object_t marker, int swindex,
				vm_object_t object);
struct thread *swapcached_thread;

SYSCTL_NODE(_vm, OID_AUTO, swapcache, CTLFLAG_RW, NULL, NULL);

int vm_swapcache_read_enable;
int vm_swapcache_inactive_heuristic;
static int vm_swapcache_sleep;
static int vm_swapcache_maxscan = PQ_L2_SIZE * 8;
static int vm_swapcache_maxlaunder = PQ_L2_SIZE * 4;
static int vm_swapcache_data_enable = 0;
static int vm_swapcache_meta_enable = 0;
static int vm_swapcache_maxswappct = 75;
static int vm_swapcache_hysteresis;
static int vm_swapcache_min_hysteresis;
int vm_swapcache_use_chflags = 1;	/* require chflags cache */
static int64_t vm_swapcache_minburst = 10000000LL;	/* 10MB */
static int64_t vm_swapcache_curburst = 4000000000LL;	/* 4G after boot */
static int64_t vm_swapcache_maxburst = 2000000000LL;	/* 2G nominal max */
static int64_t vm_swapcache_accrate = 100000LL;		/* 100K/s */
static int64_t vm_swapcache_write_count;
static int64_t vm_swapcache_maxfilesize;
static int64_t vm_swapcache_cleanperobj = 16*1024*1024;

SYSCTL_INT(_vm_swapcache, OID_AUTO, maxlaunder,
	CTLFLAG_RW, &vm_swapcache_maxlaunder, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, maxscan,
	CTLFLAG_RW, &vm_swapcache_maxscan, 0, "");

SYSCTL_INT(_vm_swapcache, OID_AUTO, data_enable,
	CTLFLAG_RW, &vm_swapcache_data_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, meta_enable,
	CTLFLAG_RW, &vm_swapcache_meta_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, read_enable,
	CTLFLAG_RW, &vm_swapcache_read_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, maxswappct,
	CTLFLAG_RW, &vm_swapcache_maxswappct, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, hysteresis,
	CTLFLAG_RD, &vm_swapcache_hysteresis, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, min_hysteresis,
	CTLFLAG_RW, &vm_swapcache_min_hysteresis, 0, "");
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
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, cleanperobj,
	CTLFLAG_RW, &vm_swapcache_cleanperobj, 0, "");

#define SWAPMAX(adj)	\
	((int64_t)vm_swap_max * (vm_swapcache_maxswappct + (adj)) / 100)

/*
 * When shutting down the machine we want to stop swapcache operation
 * immediately so swap is not accessed after devices have been shuttered.
 */
static void
shutdown_swapcache(void *arg __unused)
{
	vm_swapcache_read_enable = 0;
	vm_swapcache_data_enable = 0;
	vm_swapcache_meta_enable = 0;
	wakeup(&vm_swapcache_sleep);	/* shortcut 5-second wait */
}

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
	static struct vm_page page_marker[PQ_L2_SIZE];
	static struct vm_object swmarker;
	static int swindex;
	int q;

	/*
	 * Thread setup
	 */
	curthread->td_flags |= TDF_SYSTHREAD;
	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_kproc,
			      swapcached_thread, SHUTDOWN_PRI_FIRST);
	EVENTHANDLER_REGISTER(shutdown_pre_sync, shutdown_swapcache,
			      NULL, SHUTDOWN_PRI_SECOND);

	/*
	 * Initialize our marker for the inactive scan (SWAPC_WRITING)
	 */
	bzero(&page_marker, sizeof(page_marker));
	for (q = 0; q < PQ_L2_SIZE; ++q) {
		page_marker[q].flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
		page_marker[q].queue = PQ_INACTIVE + q;
		page_marker[q].pc = q;
		page_marker[q].wire_count = 1;
		vm_page_queues_spin_lock(PQ_INACTIVE + q);
		TAILQ_INSERT_HEAD(
			&vm_page_queues[PQ_INACTIVE + q].pl,
			&page_marker[q], pageq);
		vm_page_queues_spin_unlock(PQ_INACTIVE + q);
	}

	vm_swapcache_min_hysteresis = 1024;
	vm_swapcache_hysteresis = vm_swapcache_min_hysteresis;
	vm_swapcache_inactive_heuristic = -vm_swapcache_hysteresis;

	/*
	 * Initialize our marker for the vm_object scan (SWAPC_CLEANING)
	 */
	bzero(&swmarker, sizeof(swmarker));
	swmarker.type = OBJT_MARKER;
	swindex = 0;
	lwkt_gettoken(&vmobj_tokens[swindex]);
	TAILQ_INSERT_HEAD(&vm_object_lists[swindex],
			  &swmarker, object_list);
	lwkt_reltoken(&vmobj_tokens[swindex]);

	for (;;) {
		int reached_end;
		int scount;
		int count;

		/*
		 * Handle shutdown
		 */
		kproc_suspend_loop();

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
			if (vm_swap_cache_use < SWAPMAX(-10))
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
		if (state != SWAPC_WRITING) {
			vm_swapcache_cleaning(&swmarker, &swindex);
			continue;
		}
		if (vm_swapcache_curburst < vm_swapcache_accrate)
			continue;

		reached_end = 0;
		count = vm_swapcache_maxlaunder / PQ_L2_SIZE + 2;
		scount = vm_swapcache_maxscan / PQ_L2_SIZE + 2;

		if (burst == SWAPB_BURSTING) {
			if (vm_swapcache_writing_heuristic()) {
				for (q = 0; q < PQ_L2_SIZE; ++q) {
					reached_end +=
						vm_swapcache_writing(
							&page_marker[q],
							count,
							scount);
				}
			}
			if (vm_swapcache_curburst <= 0)
				burst = SWAPB_RECOVERING;
		} else if (vm_swapcache_curburst > vm_swapcache_minburst) {
			if (vm_swapcache_writing_heuristic()) {
				for (q = 0; q < PQ_L2_SIZE; ++q) {
					reached_end +=
						vm_swapcache_writing(
							&page_marker[q],
							count,
							scount);
				}
			}
			burst = SWAPB_BURSTING;
		}
		if (reached_end == PQ_L2_SIZE) {
			vm_swapcache_inactive_heuristic =
				-vm_swapcache_hysteresis;
		}
	}

	/*
	 * Cleanup (NOT REACHED)
	 */
	for (q = 0; q < PQ_L2_SIZE; ++q) {
		vm_page_queues_spin_lock(PQ_INACTIVE + q);
		TAILQ_REMOVE(
			&vm_page_queues[PQ_INACTIVE + q].pl,
			&page_marker[q], pageq);
		vm_page_queues_spin_unlock(PQ_INACTIVE + q);
	}

	lwkt_gettoken(&vmobj_tokens[swindex]);
	TAILQ_REMOVE(&vm_object_lists[swindex], &swmarker, object_list);
	lwkt_reltoken(&vmobj_tokens[swindex]);
}

static struct kproc_desc swpc_kp = {
	"swapcached",
	vm_swapcached_thread,
	&swapcached_thread
};
SYSINIT(swapcached, SI_SUB_KTHREAD_PAGE, SI_ORDER_SECOND, kproc_start, &swpc_kp)

/*
 * Deal with an overflow of the heuristic counter or if the user
 * manually changes the hysteresis.
 *
 * Try to avoid small incremental pageouts by waiting for enough
 * pages to buildup in the inactive queue to hopefully get a good
 * burst in.  This heuristic is bumped by the VM system and reset
 * when our scan hits the end of the queue.
 *
 * Return TRUE if we need to take a writing pass.
 */
static int
vm_swapcache_writing_heuristic(void)
{
	int hyst;

	hyst = vmstats.v_inactive_count / 4;
	if (hyst < vm_swapcache_min_hysteresis)
		hyst = vm_swapcache_min_hysteresis;
	cpu_ccfence();
	vm_swapcache_hysteresis = hyst;

	if (vm_swapcache_inactive_heuristic < -hyst)
		vm_swapcache_inactive_heuristic = -hyst;

	return (vm_swapcache_inactive_heuristic >= 0);
}

/*
 * Take a writing pass on one of the inactive queues, return non-zero if
 * we hit the end of the queue.
 */
static int
vm_swapcache_writing(vm_page_t marker, int count, int scount)
{
	vm_object_t object;
	struct vnode *vp;
	vm_page_t m;
	int isblkdev;

	/*
	 * Scan the inactive queue from our marker to locate
	 * suitable pages to push to the swap cache.
	 *
	 * We are looking for clean vnode-backed pages.
	 */
	vm_page_queues_spin_lock(marker->queue);
	while ((m = TAILQ_NEXT(marker, pageq)) != NULL &&
	       count > 0 && scount-- > 0) {
		KKASSERT(m->queue == marker->queue);

		if (vm_swapcache_curburst < 0)
			break;
		TAILQ_REMOVE(
			&vm_page_queues[marker->queue].pl, marker, pageq);
		TAILQ_INSERT_AFTER(
			&vm_page_queues[marker->queue].pl, m, marker, pageq);

		/*
		 * Ignore markers and ignore pages that already have a swap
		 * assignment.
		 */
		if (m->flags & (PG_MARKER | PG_SWAPPED))
			continue;
		if (vm_page_busy_try(m, TRUE))
			continue;
		vm_page_queues_spin_unlock(marker->queue);

		if ((object = m->object) == NULL) {
			vm_page_wakeup(m);
			vm_page_queues_spin_lock(marker->queue);
			continue;
		}
		vm_object_hold(object);
		if (m->object != object) {
			vm_object_drop(object);
			vm_page_wakeup(m);
			vm_page_queues_spin_lock(marker->queue);
			continue;
		}
		if (vm_swapcache_test(m)) {
			vm_object_drop(object);
			vm_page_wakeup(m);
			vm_page_queues_spin_lock(marker->queue);
			continue;
		}

		vp = object->handle;
		if (vp == NULL) {
			vm_object_drop(object);
			vm_page_wakeup(m);
			vm_page_queues_spin_lock(marker->queue);
			continue;
		}

		switch(vp->v_type) {
		case VREG:
			/*
			 * PG_NOTMETA generically means 'don't swapcache this',
			 * and HAMMER will set this for regular data buffers
			 * (and leave it unset for meta-data buffers) as
			 * appropriate when double buffering is enabled.
			 */
			if (m->flags & PG_NOTMETA) {
				vm_object_drop(object);
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker->queue);
				continue;
			}

			/*
			 * If data_enable is 0 do not try to swapcache data.
			 * If use_chflags is set then only swapcache data for
			 * VSWAPCACHE marked vnodes, otherwise any vnode.
			 */
			if (vm_swapcache_data_enable == 0 ||
			    ((vp->v_flag & VSWAPCACHE) == 0 &&
			     vm_swapcache_use_chflags)) {
				vm_object_drop(object);
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker->queue);
				continue;
			}
			if (vm_swapcache_maxfilesize &&
			    object->size >
			    (vm_swapcache_maxfilesize >> PAGE_SHIFT)) {
				vm_object_drop(object);
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker->queue);
				continue;
			}
			isblkdev = 0;
			break;
		case VCHR:
			/*
			 * PG_NOTMETA generically means 'don't swapcache this',
			 * and HAMMER will set this for regular data buffers
			 * (and leave it unset for meta-data buffers) as
			 * appropriate when double buffering is enabled.
			 */
			if (m->flags & PG_NOTMETA) {
				vm_object_drop(object);
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker->queue);
				continue;
			}
			if (vm_swapcache_meta_enable == 0) {
				vm_object_drop(object);
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker->queue);
				continue;
			}
			isblkdev = 1;
			break;
		default:
			vm_object_drop(object);
			vm_page_wakeup(m);
			vm_page_queues_spin_lock(marker->queue);
			continue;
		}


		/*
		 * Assign swap and initiate I/O.
		 *
		 * (adjust for the --count which also occurs in the loop)
		 */
		count -= vm_swapcached_flush(m, isblkdev);

		/*
		 * Setup for next loop using marker.
		 */
		vm_object_drop(object);
		vm_page_queues_spin_lock(marker->queue);
	}

	/*
	 * The marker could wind up at the end, which is ok.  If we hit the
	 * end of the list adjust the heuristic.
	 *
	 * Earlier inactive pages that were dirty and become clean
	 * are typically moved to the end of PQ_INACTIVE by virtue
	 * of vfs_vmio_release() when they become unwired from the
	 * buffer cache.
	 */
	vm_page_queues_spin_unlock(marker->queue);

	/*
	 * m invalid but can be used to test for NULL
	 */
	return (m == NULL);
}

/*
 * Flush the specified page using the swap_pager.  The page
 * must be busied by the caller and its disposition will become
 * the responsibility of this function.
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
	int error;

	vm_page_io_start(m);
	vm_page_protect(m, VM_PROT_READ);
	object = m->object;
	vm_object_hold(object);

	/*
	 * Try to cluster around (m), keeping in mind that the swap pager
	 * can only do SMAP_META_PAGES worth of continguous write.
	 */
	x = (int)m->pindex & SWAP_META_MASK;
	marray[x] = m;
	basei = m->pindex;
	vm_page_wakeup(m);

	for (i = x - 1; i >= 0; --i) {
		m = vm_page_lookup_busy_try(object, basei - x + i,
					    TRUE, &error);
		if (error || m == NULL)
			break;
		if (vm_swapcache_test(m)) {
			vm_page_wakeup(m);
			break;
		}
		if (isblkdev && (m->flags & PG_NOTMETA)) {
			vm_page_wakeup(m);
			break;
		}
		vm_page_io_start(m);
		vm_page_protect(m, VM_PROT_READ);
		if (m->queue - m->pc == PQ_CACHE) {
			vm_page_unqueue_nowakeup(m);
			vm_page_deactivate(m);
		}
		marray[i] = m;
		vm_page_wakeup(m);
	}
	++i;

	for (j = x + 1; j < SWAP_META_PAGES; ++j) {
		m = vm_page_lookup_busy_try(object, basei - x + j,
					    TRUE, &error);
		if (error || m == NULL)
			break;
		if (vm_swapcache_test(m)) {
			vm_page_wakeup(m);
			break;
		}
		if (isblkdev && (m->flags & PG_NOTMETA)) {
			vm_page_wakeup(m);
			break;
		}
		vm_page_io_start(m);
		vm_page_protect(m, VM_PROT_READ);
		if (m->queue - m->pc == PQ_CACHE) {
			vm_page_unqueue_nowakeup(m);
			vm_page_deactivate(m);
		}
		marray[j] = m;
		vm_page_wakeup(m);
	}

	count = j - i;
	vm_object_pip_add(object, count);
	swap_pager_putpages(object, marray + i, count, FALSE, rtvals + i);
	vm_swapcache_write_count += count * PAGE_SIZE;
	vm_swapcache_curburst -= count * PAGE_SIZE;

	while (i < j) {
		if (rtvals[i] != VM_PAGER_PEND) {
			vm_page_busy_wait(marray[i], FALSE, "swppgfd");
			vm_page_io_finish(marray[i]);
			vm_page_wakeup(marray[i]);
			vm_object_pip_wakeup(object);
		}
		++i;
	}
	vm_object_drop(object);
	return(count);
}

/*
 * Test whether a VM page is suitable for writing to the swapcache.
 * Does not test m->queue, PG_MARKER, or PG_SWAPPED.
 *
 * Returns 0 on success, 1 on failure
 */
static int
vm_swapcache_test(vm_page_t m)
{
	vm_object_t object;

	if (m->flags & PG_UNMANAGED)
		return(1);
	if (m->hold_count || m->wire_count)
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
 * Cleaning pass.
 *
 * We clean whole objects up to 16MB
 */
static
void
vm_swapcache_cleaning(vm_object_t marker, int *swindexp)
{
	vm_object_t object;
	struct vnode *vp;
	int count;
	int scount;
	int n;

	count = vm_swapcache_maxlaunder;
	scount = vm_swapcache_maxscan;

	/*
	 * Look for vnode objects
	 */
	lwkt_gettoken(&vmobj_tokens[*swindexp]);

outerloop:
	while ((object = TAILQ_NEXT(marker, object_list)) != NULL) {
		/*
		 * We have to skip markers.  We cannot hold/drop marker
		 * objects!
		 */
		if (object->type == OBJT_MARKER) {
			vm_swapcache_movemarker(marker, *swindexp, object);
			continue;
		}

		/*
		 * Safety, or in case there are millions of VM objects
		 * without swapcache backing.
		 */
		if (--scount <= 0)
			goto breakout;

		/*
		 * We must hold the object before potentially yielding.
		 */
		vm_object_hold(object);
		lwkt_yield();

		/* 
		 * Only operate on live VNODE objects that are either
		 * VREG or VCHR (VCHR for meta-data).
		 */
		if ((object->type != OBJT_VNODE) ||
		    ((object->flags & OBJ_DEAD) ||
		     object->swblock_count == 0) ||
		    ((vp = object->handle) == NULL) ||
		    (vp->v_type != VREG && vp->v_type != VCHR)) {
			vm_object_drop(object);
			/* object may be invalid now */
			vm_swapcache_movemarker(marker, *swindexp, object);
			continue;
		}

		/*
		 * Reset the object pindex stored in the marker if the
		 * working object has changed.
		 */
		if (marker->backing_object != object) {
			marker->size = 0;
			marker->backing_object_offset = 0;
			marker->backing_object = object;
		}

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
		 *
		 * Scan the object linearly and try to batch large sets of
		 * blocks that are likely to clean out entire swap radix
		 * tree leafs.
		 */
		lwkt_token_swap();
		lwkt_reltoken(&vmobj_tokens[*swindexp]);

		n = swap_pager_condfree(object, &marker->size,
				    (count + SWAP_META_MASK) & ~SWAP_META_MASK);

		vm_object_drop(object);		/* object may be invalid now */
		lwkt_gettoken(&vmobj_tokens[*swindexp]);

		/*
		 * If we have exhausted the object or deleted our per-pass
		 * page limit then move us to the next object.  Note that
		 * the current object may no longer be on the vm_object_list.
		 */
		if (n <= 0 ||
		    marker->backing_object_offset > vm_swapcache_cleanperobj) {
			vm_swapcache_movemarker(marker, *swindexp, object);
		}

		/*
		 * If we have exhausted our max-launder stop for now.
		 */
		count -= n;
		marker->backing_object_offset += n * PAGE_SIZE;
		if (count < 0)
			goto breakout;
	}

	/*
	 * Iterate vm_object_lists[] hash table
	 */
	TAILQ_REMOVE(&vm_object_lists[*swindexp], marker, object_list);
	lwkt_reltoken(&vmobj_tokens[*swindexp]);
	if (++*swindexp >= VMOBJ_HSIZE)
		*swindexp = 0;
	lwkt_gettoken(&vmobj_tokens[*swindexp]);
	TAILQ_INSERT_HEAD(&vm_object_lists[*swindexp], marker, object_list);

	if (*swindexp != 0)
		goto outerloop;

breakout:
	lwkt_reltoken(&vmobj_tokens[*swindexp]);
}

/*
 * Move the marker past the current object.  Object can be stale, but we
 * still need it to determine if the marker has to be moved.  If the object
 * is still the 'current object' (object after the marker), we hop-scotch
 * the marker past it.
 */
static void
vm_swapcache_movemarker(vm_object_t marker, int swindex, vm_object_t object)
{
	if (TAILQ_NEXT(marker, object_list) == object) {
		TAILQ_REMOVE(&vm_object_lists[swindex], marker, object_list);
		TAILQ_INSERT_AFTER(&vm_object_lists[swindex], object,
				   marker, object_list);
	}
}
