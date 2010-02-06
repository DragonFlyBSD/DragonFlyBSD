/*
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
 * reads to the swap device.
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
static void vm_swapcached (void);
static void vm_swapcached_flush (vm_page_t m);
struct thread *swapcached_thread;

static struct kproc_desc swpc_kp = {
	"swapcached",
	vm_swapcached,
	&swapcached_thread
};
SYSINIT(swapcached, SI_SUB_KTHREAD_PAGE, SI_ORDER_SECOND, kproc_start, &swpc_kp)

SYSCTL_NODE(_vm, OID_AUTO, swapcache, CTLFLAG_RW, NULL, NULL);

int vm_swapcache_read_enable;
static int vm_swapcache_sleep;
static int vm_swapcache_maxlaunder = 256;
static int vm_swapcache_data_enable = 0;
static int vm_swapcache_meta_enable = 0;
static int64_t vm_swapcache_curburst = 1000000000LL;
static int64_t vm_swapcache_maxburst = 1000000000LL;
static int64_t vm_swapcache_accrate = 1000000LL;
static int64_t vm_swapcache_write_count;

SYSCTL_INT(_vm_swapcache, OID_AUTO, maxlaunder,
	CTLFLAG_RW, &vm_swapcache_maxlaunder, 0, "");

SYSCTL_INT(_vm_swapcache, OID_AUTO, data_enable,
	CTLFLAG_RW, &vm_swapcache_data_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, meta_enable,
	CTLFLAG_RW, &vm_swapcache_meta_enable, 0, "");
SYSCTL_INT(_vm_swapcache, OID_AUTO, read_enable,
	CTLFLAG_RW, &vm_swapcache_read_enable, 0, "");

SYSCTL_QUAD(_vm_swapcache, OID_AUTO, curburst,
	CTLFLAG_RW, &vm_swapcache_curburst, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, maxburst,
	CTLFLAG_RW, &vm_swapcache_maxburst, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, accrate,
	CTLFLAG_RW, &vm_swapcache_accrate, 0, "");
SYSCTL_QUAD(_vm_swapcache, OID_AUTO, write_count,
	CTLFLAG_RW, &vm_swapcache_write_count, 0, "");

/*
 * vm_swapcached is the high level pageout daemon.
 */
static void
vm_swapcached(void)
{
	struct vm_page marker;
	vm_object_t object;
	struct vnode *vp;
	vm_page_t m;
	int count;

	/*
	 * Thread setup
	 */
	curthread->td_flags |= TDF_SYSTHREAD;

	/*
	 * Initialize our marker
	 */
	bzero(&marker, sizeof(marker));
	marker.flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
	marker.queue = PQ_INACTIVE;
	marker.wire_count = 1;

	crit_enter();
	TAILQ_INSERT_HEAD(INACTIVE_LIST, &marker, pageq);

	for (;;) {
		/*
		 * Loop once a second or so looking for work when enabled.
		 */
		if (vm_swapcache_data_enable == 0 &&
		    vm_swapcache_meta_enable == 0) {
			tsleep(&vm_swapcache_sleep, 0, "csleep", hz * 5);
			continue;
		}

		/*
		 * Polling rate when enabled is 10 hz.  Deal with write
		 * bandwidth limits.
		 *
		 * We don't want to nickle-and-dime the scan as that will
		 * create unnecessary fragmentation.
		 */
		tsleep(&vm_swapcache_sleep, 0, "csleep", hz / 10);
		vm_swapcache_curburst += vm_swapcache_accrate / 10;
		if (vm_swapcache_curburst > vm_swapcache_maxburst)
			vm_swapcache_curburst = vm_swapcache_maxburst;
		if (vm_swapcache_curburst < vm_swapcache_accrate)
			continue;

		/*
		 * Don't load any more into the cache once we have exceeded
		 * 3/4 of available swap space.  XXX need to start cleaning
		 * it out, though vnode recycling will accomplish that to
		 * some degree.
		 */
		if (vm_swap_cache_use > vm_swap_max * 3 / 4)
			continue;

		/*
		 * Calculate the number of pages to test.  We don't want
		 * to get into a cpu-bound loop.
		 */
		count = vmstats.v_inactive_count;
		if (count > vm_swapcache_maxlaunder)
			count = vm_swapcache_maxlaunder;

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
		m = &marker;
		while ((m = TAILQ_NEXT(m, pageq)) != NULL && count--) {
			if (m->flags & (PG_MARKER | PG_SWAPPED)) {
				++count;
				continue;
			}
			if (vm_swapcache_curburst < 0)
				break;
			if (m->flags & (PG_BUSY | PG_UNMANAGED))
				continue;
			if (m->busy || m->hold_count || m->wire_count)
				continue;
			if (m->valid != VM_PAGE_BITS_ALL)
				continue;
			if (m->dirty & m->valid)
				continue;
			if ((object = m->object) == NULL)
				continue;
			if (object->type != OBJT_VNODE ||
			    (object->flags & OBJ_DEAD)) {
				continue;
			}
			vm_page_test_dirty(m);
			if (m->dirty & m->valid)
				continue;
			vp = object->handle;
			if (vp == NULL)
				continue;
			switch(vp->v_type) {
			case VREG:
				if (vm_swapcache_data_enable == 0)
					continue;
				break;
			case VCHR:
				if (vm_swapcache_meta_enable == 0)
					continue;
				break;
			default:
				continue;
			}

			/*
			 * Ok, move the marker and soft-busy the page.
			 */
			TAILQ_REMOVE(INACTIVE_LIST, &marker, pageq);
			TAILQ_INSERT_AFTER(INACTIVE_LIST, m, &marker, pageq);

			/*
			 * Assign swap and initiate I/O
			 */
			vm_swapcached_flush(m);

			/*
			 * Setup for next loop using marker.
			 */
			m = &marker;
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
		TAILQ_REMOVE(INACTIVE_LIST, &marker, pageq);
		if (m)
			TAILQ_INSERT_BEFORE(m, &marker, pageq);
		else
			TAILQ_INSERT_TAIL(INACTIVE_LIST, &marker, pageq);

	}
	TAILQ_REMOVE(INACTIVE_LIST, &marker, pageq);
	crit_exit();
}

/*
 * Flush the specified page using the swap_pager.
 */
static
void
vm_swapcached_flush(vm_page_t m)
{
	vm_object_t object;
	int rtvals;

	vm_page_io_start(m);
	vm_page_protect(m, VM_PROT_READ);

	object = m->object;
	vm_object_pip_add(object, 1);
	swap_pager_putpages(object, &m, 1, FALSE, &rtvals);
	vm_swapcache_write_count += PAGE_SIZE;
	vm_swapcache_curburst -= PAGE_SIZE;

	if (rtvals != VM_PAGER_PEND) {
		vm_object_pip_wakeup(object);
		vm_page_io_finish(m);
	}
}
