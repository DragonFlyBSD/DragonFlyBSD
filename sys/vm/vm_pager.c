/*
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
 * $DragonFly: src/sys/vm/vm_pager.c,v 1.24 2007/11/06 03:50:01 dillon Exp $
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
#include <sys/malloc.h>
#include <sys/dsched.h>
#include <sys/proc.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

MALLOC_DEFINE(M_VMPGDATA, "VM pgdata", "XXX: VM pager private data");

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

static int
dead_pager_getpage(vm_object_t obj, vm_page_t *mpp, int seqaccess)
{
	return VM_PAGER_FAIL;
}

static void
dead_pager_putpages(vm_object_t object, vm_page_t *m, int count, int flags,
		    int *rtvals)
{
	int i;

	for (i = 0; i < count; i++) {
		rtvals[i] = VM_PAGER_AGAIN;
	}
}

static int
dead_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	return FALSE;
}

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
	&physpagerops,		/* OBJT_PHYS */
	&deadpagerops		/* OBJT_DEAD */
};

int npagers = sizeof(pagertab) / sizeof(pagertab[0]);

/*
 * Kernel address space for mapping pages.
 * Used by pagers where KVAs are needed for IO.
 *
 * XXX needs to be large enough to support the number of pending async
 * cleaning requests (NPENDINGIO == 64) * the maximum swap cluster size
 * (MAXPHYS == 64k) if you want to get the most efficiency.
 */
#define PAGER_MAP_SIZE	(8 * 1024 * 1024)

int pager_map_size = PAGER_MAP_SIZE;
struct vm_map pager_map;

static int bswneeded;
static vm_offset_t swapbkva;		/* swap buffers kva */
static TAILQ_HEAD(swqueue, buf) bswlist;
static struct spinlock bswspin = SPINLOCK_INITIALIZER(&bswspin);

static void
vm_pager_init(void *arg __unused)
{
	/*
	 * Initialize the swap buffer list.
	 */
	TAILQ_INIT(&bswlist);
}
SYSINIT(vm_mem, SI_BOOT1_VM, SI_ORDER_SECOND, vm_pager_init, NULL)

void
vm_pager_bufferinit(void)
{
	struct buf *bp;
	int i;

	/*
	 * Reserve KVM space for pbuf data.
	 */
	swapbkva = kmem_alloc_pageable(&pager_map, nswbuf * MAXPHYS);
	if (!swapbkva)
		panic("Not enough pager_map VM space for physical buffers");

	/*
	 * Initial pbuf setup.
	 */
	bp = swbuf;
	for (i = 0; i < nswbuf; ++i, ++bp) {
		bp->b_kvabase = (caddr_t)((intptr_t)i * MAXPHYS) + swapbkva;
		bp->b_kvasize = MAXPHYS;
		TAILQ_INSERT_HEAD(&bswlist, bp, b_freelist);
		BUF_LOCKINIT(bp);
		buf_dep_init(bp);
	}

	/*
	 * Allow the clustering code to use half of our pbufs.
	 */
	cluster_pbuf_freecnt = nswbuf / 2;
}

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

#if 0
/*
 *	vm_pager_sync:
 *
 *	Called by pageout daemon before going back to sleep.
 *	Gives pagers a chance to clean up any completed async pageing 
 *	operations.
 */
void
vm_pager_sync(void)
{
	struct pagerops **pgops;

	for (pgops = pagertab; pgops < &pagertab[npagers]; pgops++)
		if (pgops && ((*pgops)->pgo_sync != NULL))
			(*(*pgops)->pgo_sync) ();
}

#endif

/*
 * Initialize a physical buffer.
 */
static void
initpbuf(struct buf *bp)
{
	bp->b_qindex = 0; /* BQUEUE_NONE */
	bp->b_data = bp->b_kvabase;
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
 * MPSAFE
 */
struct buf *
getpbuf(int *pfreecnt)
{
	struct buf *bp;

	spin_lock_wr(&bswspin);

	for (;;) {
		if (pfreecnt) {
			while (*pfreecnt == 0)
				ssleep(pfreecnt, &bswspin, 0, "wswbuf0", 0);
		}

		/* get a bp from the swap buffer header pool */
		if ((bp = TAILQ_FIRST(&bswlist)) != NULL)
			break;
		bswneeded = 1;
		ssleep(&bswneeded, &bswspin, 0, "wswbuf1", 0);
		/* loop in case someone else grabbed one */
	}
	TAILQ_REMOVE(&bswlist, bp, b_freelist);
	if (pfreecnt)
		--*pfreecnt;

	spin_unlock_wr(&bswspin);

	initpbuf(bp);
	KKASSERT(dsched_is_clear_buf_priv(bp));
	return bp;
}

/*
 * Allocate a physical buffer, if one is available.
 *
 *	Note that there is no NULL hack here - all subsystems using this
 *	call understand how to use pfreecnt.
 *
 * MPSAFE
 */
struct buf *
trypbuf(int *pfreecnt)
{
	struct buf *bp;

	spin_lock_wr(&bswspin);

	if (*pfreecnt == 0 || (bp = TAILQ_FIRST(&bswlist)) == NULL) {
		spin_unlock_wr(&bswspin);
		return NULL;
	}
	TAILQ_REMOVE(&bswlist, bp, b_freelist);
	--*pfreecnt;

	spin_unlock_wr(&bswspin);

	initpbuf(bp);

	return bp;
}

/*
 * Release a physical buffer
 *
 *	NOTE: pfreecnt can be NULL, but this 'feature' will be removed
 *	relatively soon when the rest of the subsystems get smart about it. XXX
 *
 * MPSAFE
 */
void
relpbuf(struct buf *bp, int *pfreecnt)
{
	int wake_bsw = 0;
	int wake_freecnt = 0;

	KKASSERT(bp->b_flags & B_PAGING);
	dsched_exit_buf(bp);

	spin_lock_wr(&bswspin);

	BUF_UNLOCK(bp);
	TAILQ_INSERT_HEAD(&bswlist, bp, b_freelist);
	if (bswneeded) {
		bswneeded = 0;
		wake_bsw = 1;
	}
	if (pfreecnt) {
		if (++*pfreecnt == 1)
			wake_freecnt = 1;
	}

	spin_unlock_wr(&bswspin);

	if (wake_bsw)
		wakeup(&bswneeded);
	if (wake_freecnt)
		wakeup(pfreecnt);
}

