/*
 * (MPSAFE)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	from: @(#)vm_pager.c	8.6 (Berkeley) 1/12/94
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
 * $FreeBSD: src/sys/vm/vm_pager.c,v 1.54.2.2 2001/11/18 07:11:00 dillon Exp $
 */

/*
 *	Paging space routine stubs.  Emulates a matchmaker-like interface
 *	for builtin pagers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/ucred.h>
#include <sys/dsched.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>
#include <vm/vm_page2.h>

static	pgo_dealloc_t		dead_pager_dealloc;
static	pgo_getpage_t		dead_pager_getpage;
static	pgo_putpages_t		dead_pager_putpages;
static	pgo_haspage_t		dead_pager_haspage;

static struct pagerops deadpagerops = {
	.pgo_dealloc =		dead_pager_dealloc,
	.pgo_getpage =		dead_pager_getpage,
	.pgo_putpages =		dead_pager_putpages,
	.pgo_haspage =		dead_pager_haspage
};

extern struct pagerops defaultpagerops;
extern struct pagerops swappagerops;
extern struct pagerops vnodepagerops;
extern struct pagerops devicepagerops;
extern struct pagerops physpagerops;

/*
 * No requirements.
 */
static int
dead_pager_getpage(vm_object_t obj, vm_page_t *mpp, int seqaccess)
{
	return VM_PAGER_FAIL;
}

/*
 * No requirements.
 */
static void
dead_pager_putpages(vm_object_t object, vm_page_t *m, int count, int flags,
		    int *rtvals)
{
	int i;

	for (i = 0; i < count; i++) {
		rtvals[i] = VM_PAGER_AGAIN;
	}
}

/*
 * No requirements.
 */
static boolean_t
dead_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	return FALSE;
}

/*
 * No requirements.
 */
static void
dead_pager_dealloc(vm_object_t object)
{
	KKASSERT(object->swblock_count == 0);
	return;
}

struct pagerops *pagertab[] = {
	&defaultpagerops,	/* OBJT_DEFAULT */
	&swappagerops,		/* OBJT_SWAP */
	&vnodepagerops,		/* OBJT_VNODE */
	&devicepagerops,	/* OBJT_DEVICE */
	&devicepagerops,	/* OBJT_MGTDEVICE */
	&physpagerops,		/* OBJT_PHYS */
	&deadpagerops		/* OBJT_DEAD */
};

int npagers = NELEM(pagertab);

/*
 * Kernel address space for mapping pages.
 * Used by pagers where KVAs are needed for IO.
 *
 * XXX needs to be large enough to support the number of pending async
 * cleaning requests (NPENDINGIO == 64) * the maximum swap cluster size
 * (MAXPHYS == 64k) if you want to get the most efficiency.
 */
#define PAGER_MAP_SIZE	(8 * 1024 * 1024)

#define BSWHSIZE	16
#define BSWHMASK	(BSWHSIZE - 1)

TAILQ_HEAD(swqueue, buf);

int pager_map_size = PAGER_MAP_SIZE;
struct vm_map pager_map;

static vm_offset_t swapbkva_mem;	/* swap buffers kva */
static vm_offset_t swapbkva_kva;	/* swap buffers kva */
static struct swqueue bswlist_mem[BSWHSIZE];	/* with preallocated memory */
static struct swqueue bswlist_kva[BSWHSIZE];	/* with kva */
static struct swqueue bswlist_raw[BSWHSIZE];	/* without kva */
static struct spinlock bswspin_mem[BSWHSIZE];
static struct spinlock bswspin_kva[BSWHSIZE];
static struct spinlock bswspin_raw[BSWHSIZE];
static int pbuf_raw_count;
static int pbuf_kva_count;
static int pbuf_mem_count;

SYSCTL_INT(_vm, OID_AUTO, pbuf_raw_count, CTLFLAG_RD, &pbuf_raw_count, 0,
    "Kernel pbuf raw reservations");
SYSCTL_INT(_vm, OID_AUTO, pbuf_kva_count, CTLFLAG_RD, &pbuf_kva_count, 0,
    "Kernel pbuf kva reservations");
SYSCTL_INT(_vm, OID_AUTO, pbuf_mem_count, CTLFLAG_RD, &pbuf_mem_count, 0,
    "Kernel pbuf mem reservations");

/*
 * Initialize the swap buffer list.
 *
 * Called from the low level boot code only.
 */
static void
vm_pager_init(void *arg __unused)
{
	int i;

	for (i = 0; i < BSWHSIZE; ++i) {
		TAILQ_INIT(&bswlist_mem[i]);
		TAILQ_INIT(&bswlist_kva[i]);
		TAILQ_INIT(&bswlist_raw[i]);
		spin_init(&bswspin_mem[i], "bswmem");
		spin_init(&bswspin_kva[i], "bswkva");
		spin_init(&bswspin_raw[i], "bswraw");
	}
}
SYSINIT(vm_mem, SI_BOOT1_VM, SI_ORDER_SECOND, vm_pager_init, NULL);

/*
 * Called from the low level boot code only.
 */
static
void
vm_pager_bufferinit(void *dummy __unused)
{
	struct buf *bp;
	long i;

	/*
	 * Reserve KVM space for pbuf data.
	 */
	swapbkva_mem = kmem_alloc_pageable(&pager_map, nswbuf_mem * MAXPHYS,
					   VM_SUBSYS_BUFDATA);
	if (!swapbkva_mem)
		panic("Not enough pager_map VM space for physical buffers");
	swapbkva_kva = kmem_alloc_pageable(&pager_map, nswbuf_kva * MAXPHYS,
					   VM_SUBSYS_BUFDATA);
	if (!swapbkva_kva)
		panic("Not enough pager_map VM space for physical buffers");

	/*
	 * Initial pbuf setup.
	 *
	 * mem - These pbufs have permanently allocated memory
	 * kva - These pbufs have unallocated kva reservations
	 * raw - These pbufs have no kva reservations
	 */

	/*
	 * Buffers with pre-allocated kernel memory can be convenient for
	 * copyin/copyout because no SMP page invalidation or other pmap
	 * operations are needed.
	 */
	bp = swbuf_mem;
	for (i = 0; i < nswbuf_mem; ++i, ++bp) {
		vm_page_t m;
		vm_pindex_t pg;
		int j;

		bp->b_kvabase = (caddr_t)((intptr_t)i * MAXPHYS) + swapbkva_mem;
		bp->b_kvasize = MAXPHYS;
		bp->b_swindex = i & BSWHMASK;
		bp->b_cpumask = smp_active_mask;
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
		TAILQ_INSERT_HEAD(&bswlist_mem[i & BSWHMASK], bp, b_freelist);
		atomic_add_int(&pbuf_mem_count, 1);
		bp->b_data = bp->b_kvabase;
		bp->b_bcount = MAXPHYS;
		bp->b_xio.xio_pages = bp->b_xio.xio_internal_pages;

		pg = (vm_offset_t)bp->b_kvabase >> PAGE_SHIFT;
		vm_object_hold(&kernel_object);
		for (j = 0; j < MAXPHYS / PAGE_SIZE; ++j) {
			m = vm_page_alloc(&kernel_object, pg, VM_ALLOC_NORMAL |
							      VM_ALLOC_SYSTEM);
			KKASSERT(m != NULL);
			bp->b_xio.xio_internal_pages[j] = m;
			vm_page_wire(m);
			/* early boot, no other cpus running yet */
			pmap_kenter_noinval(pg * PAGE_SIZE, VM_PAGE_TO_PHYS(m));
			cpu_invlpg((void *)(pg * PAGE_SIZE));
			vm_page_wakeup(m);
			++pg;
		}
		vm_object_drop(&kernel_object);
		bp->b_xio.xio_npages = j;
	}

	/*
	 * Buffers with pre-assigned KVA bases.  The KVA has no memory pages
	 * assigned to it.  Saves the caller from having to reserve KVA for
	 * the page map.
	 */
	bp = swbuf_kva;
	for (i = 0; i < nswbuf_kva; ++i, ++bp) {
		bp->b_kvabase = (caddr_t)((intptr_t)i * MAXPHYS) + swapbkva_kva;
		bp->b_kvasize = MAXPHYS;
		bp->b_swindex = i & BSWHMASK;
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
		TAILQ_INSERT_HEAD(&bswlist_kva[i & BSWHMASK], bp, b_freelist);
		atomic_add_int(&pbuf_kva_count, 1);
	}

	/*
	 * RAW buffers with no KVA mappings.
	 *
	 * NOTE: We use KM_NOTLBSYNC here to reduce unnecessary IPIs
	 *	 during startup, which can really slow down emulated
	 *	 systems.
	 */
	nswbuf_raw = nbuf * 2;
	swbuf_raw = (void *)kmem_alloc3(&kernel_map,
				round_page(nswbuf_raw * sizeof(struct buf)),
				VM_SUBSYS_BUFDATA,
				KM_NOTLBSYNC);
	smp_invltlb();
	bp = swbuf_raw;
	for (i = 0; i < nswbuf_raw; ++i, ++bp) {
		bp->b_swindex = i & BSWHMASK;
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
		TAILQ_INSERT_HEAD(&bswlist_raw[i & BSWHMASK], bp, b_freelist);
		atomic_add_int(&pbuf_raw_count, 1);
	}
}

SYSINIT(do_vmpg, SI_BOOT2_MACHDEP, SI_ORDER_FIRST, vm_pager_bufferinit, NULL);

/*
 * No requirements.
 */
void
vm_pager_deallocate(vm_object_t object)
{
	(*pagertab[object->type]->pgo_dealloc) (object);
}

/*
 * vm_pager_get_pages() - inline, see vm/vm_pager.h
 * vm_pager_put_pages() - inline, see vm/vm_pager.h
 * vm_pager_has_page() - inline, see vm/vm_pager.h
 * vm_pager_page_inserted() - inline, see vm/vm_pager.h
 * vm_pager_page_removed() - inline, see vm/vm_pager.h
 */

/*
 * Search the specified pager object list for an object with the
 * specified handle.  If an object with the specified handle is found,
 * increase its reference count and return it.  Otherwise, return NULL.
 *
 * The pager object list must be locked.
 */
vm_object_t
vm_pager_object_lookup(struct pagerlst *pg_list, void *handle)
{
	vm_object_t object;

	TAILQ_FOREACH(object, pg_list, pager_object_entry) {
		if (object->handle == handle) {
			VM_OBJECT_LOCK(object);
			if ((object->flags & OBJ_DEAD) == 0) {
				vm_object_reference_locked(object);
				VM_OBJECT_UNLOCK(object);
				break;
			}
			VM_OBJECT_UNLOCK(object);
		}
	}
	return (object);
}

/*
 * Initialize a physical buffer.
 *
 * No requirements.
 */
static void
initpbuf(struct buf *bp)
{
	bp->b_qindex = 0;		/* BQUEUE_NONE */
	bp->b_data = bp->b_kvabase;	/* NULL if pbuf sans kva */
	bp->b_flags = B_PAGING;
	bp->b_cmd = BUF_CMD_DONE;
	bp->b_error = 0;
	bp->b_bcount = 0;
	bp->b_bufsize = MAXPHYS;
	initbufbio(bp);
	xio_init(&bp->b_xio);
	BUF_LOCK(bp, LK_EXCLUSIVE);
}

/*
 * Allocate a physical buffer
 *
 * If (pfreecnt != NULL) then *pfreecnt will be decremented on return and
 * the function will block while it is <= 0.
 *
 * Physical buffers can be with or without KVA space reserved.  There
 * are severe limitations on the ones with KVA reserved, and fewer
 * limitations on the ones without.  getpbuf() gets one without,
 * getpbuf_kva() gets one with.
 *
 * No requirements.
 */
struct buf *
getpbuf(int *pfreecnt)
{
	struct buf *bp;
	int iter;
	int loops;

	for (;;) {
		while (pfreecnt && *pfreecnt <= 0) {
			tsleep_interlock(pfreecnt, 0);
			if ((int)atomic_fetchadd_int(pfreecnt, 0) <= 0)
				tsleep(pfreecnt, PINTERLOCKED, "wswbuf0", 0);
		}
		if (pbuf_raw_count <= 0) {
			tsleep_interlock(&pbuf_raw_count, 0);
			if ((int)atomic_fetchadd_int(&pbuf_raw_count, 0) <= 0)
				tsleep(&pbuf_raw_count, PINTERLOCKED,
				       "wswbuf1", 0);
			continue;
		}
		iter = mycpuid & BSWHMASK;
		for (loops = BSWHSIZE; loops; --loops) {
			if (TAILQ_FIRST(&bswlist_raw[iter]) == NULL) {
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			spin_lock(&bswspin_raw[iter]);
			if ((bp = TAILQ_FIRST(&bswlist_raw[iter])) == NULL) {
				spin_unlock(&bswspin_raw[iter]);
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			TAILQ_REMOVE(&bswlist_raw[iter], bp, b_freelist);
			atomic_add_int(&pbuf_raw_count, -1);
			if (pfreecnt)
				atomic_add_int(pfreecnt, -1);
			spin_unlock(&bswspin_raw[iter]);
			initpbuf(bp);

			return bp;
		}
	}
	/* not reached */
}

struct buf *
getpbuf_kva(int *pfreecnt)
{
	struct buf *bp;
	int iter;
	int loops;

	for (;;) {
		while (pfreecnt && *pfreecnt <= 0) {
			tsleep_interlock(pfreecnt, 0);
			if ((int)atomic_fetchadd_int(pfreecnt, 0) <= 0)
				tsleep(pfreecnt, PINTERLOCKED, "wswbuf2", 0);
		}
		if (pbuf_kva_count <= 0) {
			tsleep_interlock(&pbuf_kva_count, 0);
			if ((int)atomic_fetchadd_int(&pbuf_kva_count, 0) <= 0)
				tsleep(&pbuf_kva_count, PINTERLOCKED,
				       "wswbuf3", 0);
			continue;
		}
		iter = mycpuid & BSWHMASK;
		for (loops = BSWHSIZE; loops; --loops) {
			if (TAILQ_FIRST(&bswlist_kva[iter]) == NULL) {
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			spin_lock(&bswspin_kva[iter]);
			if ((bp = TAILQ_FIRST(&bswlist_kva[iter])) == NULL) {
				spin_unlock(&bswspin_kva[iter]);
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			TAILQ_REMOVE(&bswlist_kva[iter], bp, b_freelist);
			atomic_add_int(&pbuf_kva_count, -1);
			if (pfreecnt)
				atomic_add_int(pfreecnt, -1);
			spin_unlock(&bswspin_kva[iter]);
			initpbuf(bp);

			return bp;
		}
	}
	/* not reached */
}

/*
 * Allocate a pbuf with kernel memory already preallocated.  Caller must
 * not change the mapping.
 */
struct buf *
getpbuf_mem(int *pfreecnt)
{
	struct buf *bp;
	int iter;
	int loops;

	for (;;) {
		while (pfreecnt && *pfreecnt <= 0) {
			tsleep_interlock(pfreecnt, 0);
			if ((int)atomic_fetchadd_int(pfreecnt, 0) <= 0)
				tsleep(pfreecnt, PINTERLOCKED, "wswbuf4", 0);
		}
		if (pbuf_mem_count <= 0) {
			tsleep_interlock(&pbuf_mem_count, 0);
			if ((int)atomic_fetchadd_int(&pbuf_mem_count, 0) <= 0)
				tsleep(&pbuf_mem_count, PINTERLOCKED,
				       "wswbuf5", 0);
			continue;
		}
		iter = mycpuid & BSWHMASK;
		for (loops = BSWHSIZE; loops; --loops) {
			if (TAILQ_FIRST(&bswlist_mem[iter]) == NULL) {
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			spin_lock(&bswspin_mem[iter]);
			if ((bp = TAILQ_FIRST(&bswlist_mem[iter])) == NULL) {
				spin_unlock(&bswspin_mem[iter]);
				iter = (iter + 1) & BSWHMASK;
				continue;
			}
			TAILQ_REMOVE(&bswlist_mem[iter], bp, b_freelist);
			atomic_add_int(&pbuf_mem_count, -1);
			if (pfreecnt)
				atomic_add_int(pfreecnt, -1);
			spin_unlock(&bswspin_mem[iter]);
			initpbuf(bp);

			return bp;
		}
	}
	/* not reached */
}

/*
 * Allocate a physical buffer, if one is available.
 *
 * Note that there is no NULL hack here - all subsystems using this
 * call are required to use a non-NULL pfreecnt.
 *
 * No requirements.
 */
struct buf *
trypbuf(int *pfreecnt)
{
	struct buf *bp;
	int iter = mycpuid & BSWHMASK;
	int loops;

	for (loops = BSWHSIZE; loops; --loops) {
		if (*pfreecnt <= 0 || TAILQ_FIRST(&bswlist_raw[iter]) == NULL) {
			iter = (iter + 1) & BSWHMASK;
			continue;
		}
		spin_lock(&bswspin_raw[iter]);
		if (*pfreecnt <= 0 ||
		    (bp = TAILQ_FIRST(&bswlist_raw[iter])) == NULL) {
			spin_unlock(&bswspin_raw[iter]);
			iter = (iter + 1) & BSWHMASK;
			continue;
		}
		TAILQ_REMOVE(&bswlist_raw[iter], bp, b_freelist);
		atomic_add_int(&pbuf_raw_count, -1);
		atomic_add_int(pfreecnt, -1);

		spin_unlock(&bswspin_raw[iter]);

		initpbuf(bp);

		return bp;
	}
	return NULL;
}

struct buf *
trypbuf_kva(int *pfreecnt)
{
	struct buf *bp;
	int iter = mycpuid & BSWHMASK;
	int loops;

	for (loops = BSWHSIZE; loops; --loops) {
		if (*pfreecnt <= 0 || TAILQ_FIRST(&bswlist_kva[iter]) == NULL) {
			iter = (iter + 1) & BSWHMASK;
			continue;
		}
		spin_lock(&bswspin_kva[iter]);
		if (*pfreecnt <= 0 ||
		    (bp = TAILQ_FIRST(&bswlist_kva[iter])) == NULL) {
			spin_unlock(&bswspin_kva[iter]);
			iter = (iter + 1) & BSWHMASK;
			continue;
		}
		TAILQ_REMOVE(&bswlist_kva[iter], bp, b_freelist);
		atomic_add_int(&pbuf_kva_count, -1);
		atomic_add_int(pfreecnt, -1);

		spin_unlock(&bswspin_kva[iter]);

		initpbuf(bp);

		return bp;
	}
	return NULL;
}

/*
 * Release a physical buffer
 *
 *	NOTE: pfreecnt can be NULL, but this 'feature' will be removed
 *	relatively soon when the rest of the subsystems get smart about it. XXX
 *
 * No requirements.
 */
void
relpbuf(struct buf *bp, int *pfreecnt)
{
	int wake = 0;
	int wake_free = 0;
	int iter = bp->b_swindex;

	KKASSERT(bp->b_flags & B_PAGING);
	dsched_buf_exit(bp);

	BUF_UNLOCK(bp);

	if (bp >= swbuf_mem && bp < &swbuf_mem[nswbuf_mem]) {
		KKASSERT(bp->b_kvabase);
		spin_lock(&bswspin_mem[iter]);
		TAILQ_INSERT_HEAD(&bswlist_mem[iter], bp, b_freelist);
		if (atomic_fetchadd_int(&pbuf_mem_count, 1) == nswbuf_mem / 4)
			wake = 1;
		if (pfreecnt) {
			if (atomic_fetchadd_int(pfreecnt, 1) == 1)
				wake_free = 1;
		}
		spin_unlock(&bswspin_mem[iter]);
		if (wake)
			wakeup(&pbuf_mem_count);
	} else if (bp >= swbuf_kva && bp < &swbuf_kva[nswbuf_kva]) {
		KKASSERT(bp->b_kvabase);
		CPUMASK_ASSZERO(bp->b_cpumask);
		spin_lock(&bswspin_kva[iter]);
		TAILQ_INSERT_HEAD(&bswlist_kva[iter], bp, b_freelist);
		if (atomic_fetchadd_int(&pbuf_kva_count, 1) == nswbuf_kva / 4)
			wake = 1;
		if (pfreecnt) {
			if (atomic_fetchadd_int(pfreecnt, 1) == 1)
				wake_free = 1;
		}
		spin_unlock(&bswspin_kva[iter]);
		if (wake)
			wakeup(&pbuf_kva_count);
	} else {
		KKASSERT(bp->b_kvabase == NULL);
		KKASSERT(bp >= swbuf_raw && bp < &swbuf_raw[nswbuf_raw]);
		CPUMASK_ASSZERO(bp->b_cpumask);
		spin_lock(&bswspin_raw[iter]);
		TAILQ_INSERT_HEAD(&bswlist_raw[iter], bp, b_freelist);
		if (atomic_fetchadd_int(&pbuf_raw_count, 1) == nswbuf_raw / 4)
			wake = 1;
		if (pfreecnt) {
			if (atomic_fetchadd_int(pfreecnt, 1) == 1)
				wake_free = 1;
		}
		spin_unlock(&bswspin_raw[iter]);
		if (wake)
			wakeup(&pbuf_raw_count);
	}
	if (wake_free)
		wakeup(pfreecnt);
}

void
pbuf_adjcount(int *pfreecnt, int n)
{
	if (n) {
		atomic_add_int(pfreecnt, n);
		wakeup(pfreecnt);
	}
}
