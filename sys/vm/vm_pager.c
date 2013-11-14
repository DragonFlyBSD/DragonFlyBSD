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
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

extern struct pagerops defaultpagerops;
extern struct pagerops swappagerops;
extern struct pagerops vnodepagerops;
extern struct pagerops devicepagerops;
extern struct pagerops physpagerops;

int cluster_pbuf_freecnt = -1;	/* unlimited to begin with */

static int dead_pager_getpage (vm_object_t, vm_page_t *, int);
static void dead_pager_putpages (vm_object_t, vm_page_t *, int, int, int *);
static boolean_t dead_pager_haspage (vm_object_t, vm_pindex_t);
static void dead_pager_dealloc (vm_object_t);

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
static int
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

static struct pagerops deadpagerops = {
	dead_pager_dealloc,
	dead_pager_getpage,
	dead_pager_putpages,
	dead_pager_haspage
};

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

TAILQ_HEAD(swqueue, buf);

int pager_map_size = PAGER_MAP_SIZE;
struct vm_map pager_map;

static int bswneeded_raw;
static int bswneeded_kva;
static long nswbuf_raw;
static struct buf *swbuf_raw;
static vm_offset_t swapbkva;		/* swap buffers kva */
static struct swqueue bswlist_raw;	/* without kva */
static struct swqueue bswlist_kva;	/* with kva */
static struct spinlock bswspin = SPINLOCK_INITIALIZER(&bswspin);
static int pbuf_raw_count;
static int pbuf_kva_count;

SYSCTL_INT(_vfs, OID_AUTO, pbuf_raw_count, CTLFLAG_RD, &pbuf_raw_count, 0,
    "Kernel virtual address space reservations");
SYSCTL_INT(_vfs, OID_AUTO, pbuf_kva_count, CTLFLAG_RD, &pbuf_kva_count, 0,
    "Kernel raw address space reservations");

/*
 * Initialize the swap buffer list.
 *
 * Called from the low level boot code only.
 */
static void
vm_pager_init(void *arg __unused)
{
	TAILQ_INIT(&bswlist_raw);
	TAILQ_INIT(&bswlist_kva);
}
SYSINIT(vm_mem, SI_BOOT1_VM, SI_ORDER_SECOND, vm_pager_init, NULL)

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
	swapbkva = kmem_alloc_pageable(&pager_map, nswbuf * MAXPHYS);
	if (!swapbkva)
		panic("Not enough pager_map VM space for physical buffers");

	/*
	 * Initial pbuf setup.  These pbufs have KVA reservations.
	 */
	bp = swbuf;
	for (i = 0; i < nswbuf; ++i, ++bp) {
		bp->b_kvabase = (caddr_t)((intptr_t)i * MAXPHYS) + swapbkva;
		bp->b_kvasize = MAXPHYS;
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
		TAILQ_INSERT_HEAD(&bswlist_kva, bp, b_freelist);
		++pbuf_kva_count;
	}

	/*
	 * Initial pbuf setup.  These pbufs do not have KVA reservations,
	 * so we can have a lot more of them.  These are typically used
	 * to massage low level buf/bio requests.
	 */
	nswbuf_raw = nbuf * 2;
	swbuf_raw = (void *)kmem_alloc(&kernel_map,
				round_page(nswbuf_raw * sizeof(struct buf)));
	bp = swbuf_raw;
	for (i = 0; i < nswbuf_raw; ++i, ++bp) {
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
		TAILQ_INSERT_HEAD(&bswlist_raw, bp, b_freelist);
		++pbuf_raw_count;
	}

	/*
	 * Allow the clustering code to use half of our pbufs.
	 */
	cluster_pbuf_freecnt = nswbuf / 2;
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

	TAILQ_FOREACH(object, pg_list, pager_object_list) {
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
 *	There are a limited number (nswbuf) of physical buffers.  We need
 *	to make sure that no single subsystem is able to hog all of them,
 *	so each subsystem implements a counter which is typically initialized
 *	to 1/2 nswbuf.  getpbuf() decrements this counter in allocation and
 *	increments it on release, and blocks if the counter hits zero.  A
 *	subsystem may initialize the counter to -1 to disable the feature,
 *	but it must still be sure to match up all uses of getpbuf() with 
 *	relpbuf() using the same variable.
 *
 *	NOTE: pfreecnt can be NULL, but this 'feature' will be removed
 *	relatively soon when the rest of the subsystems get smart about it. XXX
 *
 *	Physical buffers can be with or without KVA space reserved.  There
 *	are severe limitations on the ones with KVA reserved, and fewer
 *	limitations on the ones without.  getpbuf() gets one without,
 *	getpbuf_kva() gets one with.
 *
 * No requirements.
 */
struct buf *
getpbuf(int *pfreecnt)
{
	struct buf *bp;

	spin_lock(&bswspin);

	for (;;) {
		if (pfreecnt) {
			while (*pfreecnt == 0)
				ssleep(pfreecnt, &bswspin, 0, "wswbuf0", 0);
		}

		/* get a bp from the swap buffer header pool */
		if ((bp = TAILQ_FIRST(&bswlist_raw)) != NULL)
			break;
		bswneeded_raw = 1;
		ssleep(&bswneeded_raw, &bswspin, 0, "wswbuf1", 0);
		/* loop in case someone else grabbed one */
	}
	TAILQ_REMOVE(&bswlist_raw, bp, b_freelist);
	--pbuf_raw_count;
	if (pfreecnt)
		--*pfreecnt;

	spin_unlock(&bswspin);

	initpbuf(bp);
	KKASSERT(dsched_is_clear_buf_priv(bp));

	return (bp);
}

struct buf *
getpbuf_kva(int *pfreecnt)
{
	struct buf *bp;

	spin_lock(&bswspin);

	for (;;) {
		if (pfreecnt) {
			while (*pfreecnt == 0)
				ssleep(pfreecnt, &bswspin, 0, "wswbuf0", 0);
		}

		/* get a bp from the swap buffer header pool */
		if ((bp = TAILQ_FIRST(&bswlist_kva)) != NULL)
			break;
		bswneeded_kva = 1;
		ssleep(&bswneeded_kva, &bswspin, 0, "wswbuf1", 0);
		/* loop in case someone else grabbed one */
	}
	TAILQ_REMOVE(&bswlist_kva, bp, b_freelist);
	--pbuf_kva_count;
	if (pfreecnt)
		--*pfreecnt;

	spin_unlock(&bswspin);

	initpbuf(bp);
	KKASSERT(dsched_is_clear_buf_priv(bp));

	return (bp);
}

/*
 * Allocate a physical buffer, if one is available.
 *
 *	Note that there is no NULL hack here - all subsystems using this
 *	call understand how to use pfreecnt.
 *
 * No requirements.
 */
struct buf *
trypbuf(int *pfreecnt)
{
	struct buf *bp;

	spin_lock(&bswspin);

	if (*pfreecnt == 0 || (bp = TAILQ_FIRST(&bswlist_raw)) == NULL) {
		spin_unlock(&bswspin);
		return NULL;
	}
	TAILQ_REMOVE(&bswlist_raw, bp, b_freelist);
	--pbuf_raw_count;
	--*pfreecnt;

	spin_unlock(&bswspin);

	initpbuf(bp);

	return bp;
}

struct buf *
trypbuf_kva(int *pfreecnt)
{
	struct buf *bp;

	spin_lock(&bswspin);

	if (*pfreecnt == 0 || (bp = TAILQ_FIRST(&bswlist_kva)) == NULL) {
		spin_unlock(&bswspin);
		return NULL;
	}
	TAILQ_REMOVE(&bswlist_kva, bp, b_freelist);
	--pbuf_kva_count;
	--*pfreecnt;

	spin_unlock(&bswspin);

	initpbuf(bp);

	return bp;
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
	int wake_bsw_kva = 0;
	int wake_bsw_raw = 0;
	int wake_freecnt = 0;

	KKASSERT(bp->b_flags & B_PAGING);
	dsched_exit_buf(bp);

	BUF_UNLOCK(bp);

	spin_lock(&bswspin);
	if (bp->b_kvabase) {
		TAILQ_INSERT_HEAD(&bswlist_kva, bp, b_freelist);
		++pbuf_kva_count;
	} else {
		TAILQ_INSERT_HEAD(&bswlist_raw, bp, b_freelist);
		++pbuf_raw_count;
	}
	if (bswneeded_kva) {
		bswneeded_kva = 0;
		wake_bsw_kva = 1;
	}
	if (bswneeded_raw) {
		bswneeded_raw = 0;
		wake_bsw_raw = 1;
	}
	if (pfreecnt) {
		if (++*pfreecnt == 1)
			wake_freecnt = 1;
	}
	spin_unlock(&bswspin);

	if (wake_bsw_kva)
		wakeup(&bswneeded_kva);
	if (wake_bsw_raw)
		wakeup(&bswneeded_raw);
	if (wake_freecnt)
		wakeup(pfreecnt);
}

void
pbuf_adjcount(int *pfreecnt, int n)
{
	if (n) {
		spin_lock(&bswspin);
		*pfreecnt += n;
		spin_unlock(&bswspin);
		wakeup(pfreecnt);
	}
}
