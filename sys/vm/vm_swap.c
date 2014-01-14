/*
 * (MPSAFE)
 *
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)vm_swap.c	8.5 (Berkeley) 2/17/94
 * $FreeBSD: src/sys/vm/vm_swap.c,v 1.96.2.2 2001/10/14 18:46:47 iedowse Exp $
 */

#include "opt_swap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/nlookup.h>
#include <sys/sysctl.h>
#include <sys/dmap.h>		/* XXX */
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/blist.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/swap_pager.h>
#include <vm/vm_zone.h>
#include <vm/vm_param.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/mutex2.h>
#include <sys/spinlock2.h>

/*
 * Indirect driver for multi-controller paging.
 */

#ifndef NSWAPDEV
#define NSWAPDEV	4
#endif
static struct swdevt should_be_malloced[NSWAPDEV];
struct swdevt *swdevt = should_be_malloced;	/* exported to pstat/systat */
static swblk_t nswap;		/* first block after the interleaved devs */
static struct mtx swap_mtx = MTX_INITIALIZER;
int nswdev = NSWAPDEV;				/* exported to pstat/systat */
int vm_swap_size;
int vm_swap_max;

static int swapoff_one(int index);
struct vnode *swapdev_vp;

/*
 * (struct vnode *a_vp, struct bio *b_bio)
 *
 * vn_strategy() for swapdev_vp.  Perform swap strategy interleave device
 * selection.
 *
 * No requirements.
 */
static int
swapdev_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	int sz, off, seg, index, blkno, nblkno;
	struct swdevt *sp;
	sz = howmany(bp->b_bcount, PAGE_SIZE);
	blkno = (int)(bio->bio_offset >> PAGE_SHIFT);

	/*
	 * Convert interleaved swap into per-device swap.  Note that
	 * the block size is left in PAGE_SIZE'd chunks (for the newswap)
	 * here.
	 */
	nbio = push_bio(bio);
	if (nswdev > 1) {
		off = blkno % dmmax;
		if (off + sz > dmmax) {
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR;
			biodone(bio);
			return 0;
		}
		seg = blkno / dmmax;
		index = seg % nswdev;
		seg /= nswdev;
		nbio->bio_offset = (off_t)(seg * dmmax + off) << PAGE_SHIFT;
	} else {
		index = 0;
		nbio->bio_offset = bio->bio_offset;
	}
	nblkno = (int)(nbio->bio_offset >> PAGE_SHIFT);
	sp = &swdevt[index];
	if (nblkno + sz > sp->sw_nblks) {
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return 0;
	}
	if (sp->sw_vp == NULL) {
		bp->b_error = ENODEV;
		bp->b_flags |= B_ERROR;
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return 0;
	}

	/*
	 * Issue a strategy call on the appropriate swap vnode.  Note that
	 * bp->b_vp is not modified.  Strategy code is always supposed to
	 * use the passed vp.
	 *
	 * We have to use vn_strategy() here even if we know we have a
	 * device in order to properly break up requests which exceed the
	 * device's DMA limits.
	 */
	vn_strategy(sp->sw_vp, nbio);
	return 0;
}

static int
swapdev_inactive(struct vop_inactive_args *ap)
{
	vrecycle(ap->a_vp);
	return(0);
}

static int
swapdev_reclaim(struct vop_reclaim_args *ap)
{
	return(0);
}

/*
 * Create a special vnode op vector for swapdev_vp - we only use
 * vn_strategy(), everything else returns an error.
 */
static struct vop_ops swapdev_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_strategy =		swapdev_strategy,
	.vop_inactive =		swapdev_inactive,
	.vop_reclaim =		swapdev_reclaim
};
static struct vop_ops *swapdev_vnode_vops_p = &swapdev_vnode_vops;

VNODEOP_SET(swapdev_vnode_vops);

/*
 * swapon_args(char *name)
 *
 * System call swapon(name) enables swapping on device name,
 * which must be in the swdevsw.  Return EBUSY
 * if already swapping on this device.
 *
 * No requirements.
 */
int
sys_swapon(struct swapon_args *uap)
{
	struct thread *td = curthread;
	struct vattr attr;
	struct vnode *vp;
	struct nlookupdata nd;
	int error;

	error = priv_check(td, PRIV_ROOT);
	if (error)
		return (error);

	mtx_lock(&swap_mtx);
	get_mplock();
	vp = NULL;
	error = nlookup_init(&nd, uap->name, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error) {
		rel_mplock();
		mtx_unlock(&swap_mtx);
		return (error);
	}

	if (vn_isdisk(vp, &error)) {
		error = swaponvp(td, vp, 0);
	} else if (vp->v_type == VREG && vp->v_tag == VT_NFS &&
		   (error = VOP_GETATTR(vp, &attr)) == 0) {
		/*
		 * Allow direct swapping to NFS regular files in the same
		 * way that nfs_mountroot() sets up diskless swapping.
		 */
		error = swaponvp(td, vp, attr.va_size / DEV_BSIZE);
	}
	if (error)
		vrele(vp);
	rel_mplock();
	mtx_unlock(&swap_mtx);

	return (error);
}

/*
 * Swfree(index) frees the index'th portion of the swap map.
 * Each of the nswdev devices provides 1/nswdev'th of the swap
 * space, which is laid out with blocks of dmmax pages circularly
 * among the devices.
 *
 * The new swap code uses page-sized blocks.  The old swap code used
 * DEV_BSIZE'd chunks.
 *
 * XXX locking when multiple swapon's run in parallel
 */
int
swaponvp(struct thread *td, struct vnode *vp, u_quad_t nblks)
{
	swblk_t aligned_nblks;
	int64_t dpsize;
	struct ucred *cred;
	struct swdevt *sp;
	swblk_t vsbase;
	swblk_t dvbase;
	cdev_t dev;
	int index;
	int error;
	swblk_t blk;

	cred = td->td_ucred;

	lwkt_gettoken(&vm_token);	/* needed for vm_swap_size and blist */
	mtx_lock(&swap_mtx);

	if (!swapdev_vp) {
		error = getspecialvnode(VT_NON, NULL, &swapdev_vnode_vops_p,
				    &swapdev_vp, 0, 0);
		if (error)
			panic("Cannot get vnode for swapdev");
		swapdev_vp->v_type = VNON;	/* Untyped */
		vx_unlock(swapdev_vp);
	}

	for (sp = swdevt, index = 0 ; index < nswdev; index++, sp++) {
		if (sp->sw_vp == vp) {
			error = EBUSY;
			goto done;
		}
		if (!sp->sw_vp)
			goto found;

	}
	error = EINVAL;
	goto done;
    found:
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(vp, FREAD | FWRITE, cred, NULL);
	vn_unlock(vp);
	if (error)
		goto done;

	/*
	 * v_rdev is not valid until after the VOP_OPEN() call.  dev_psize()
	 * must be supported if a character device has been specified.
	 */
	if (vp->v_type == VCHR)
		dev = vp->v_rdev;
	else
		dev = NULL;

	if (nblks == 0 && dev != NULL) {
		dpsize = dev_dpsize(dev);
		if (dpsize == -1) {
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			VOP_CLOSE(vp, FREAD | FWRITE, NULL);
			vn_unlock(vp);
			error = ENXIO;
			goto done;
		}
		nblks = (u_quad_t)dpsize;
	}
	if (nblks == 0) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		VOP_CLOSE(vp, FREAD | FWRITE, NULL);
		vn_unlock(vp);
		error = ENXIO;
		goto done;
	}

	/*
	 * nblks is in DEV_BSIZE'd chunks, convert to PAGE_SIZE'd chunks.
	 * First chop nblks off to page-align it, then convert.
	 * 
	 * sw->sw_nblks is in page-sized chunks now too.
	 */
	nblks &= ~(u_quad_t)(ctodb(1) - 1);
	nblks = dbtoc(nblks);

	/*
	 * Post-conversion nblks must not be >= BLIST_MAXBLKS, and
	 * we impose a 4-swap-device limit so we have to divide it out
	 * further.  Going beyond this will result in overflows in the
	 * blist code.
	 *
	 * Post-conversion nblks must fit within a (swblk_t), which
	 * this test also ensures.
	 */
	if (nblks > BLIST_MAXBLKS / nswdev) {
		kprintf("exceeded maximum of %d blocks per swap unit\n",
			(int)BLIST_MAXBLKS / nswdev);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		VOP_CLOSE(vp, FREAD | FWRITE, NULL);
		vn_unlock(vp);
		error = ENXIO;
		goto done;
	}

	sp->sw_vp = vp;
	sp->sw_dev = dev2udev(dev);
	sp->sw_device = dev;
	sp->sw_flags = SW_FREED;
	sp->sw_nused = 0;

	/*
	 * nblks, nswap, and dmmax are PAGE_SIZE'd parameters now, not
	 * DEV_BSIZE'd.   aligned_nblks is used to calculate the
	 * size of the swap bitmap, taking into account the stripe size.
	 */
	aligned_nblks = (swblk_t)((nblks + (dmmax - 1)) & ~(u_long)(dmmax - 1));
	sp->sw_nblks = aligned_nblks;

	if (aligned_nblks * nswdev > nswap)
		nswap = aligned_nblks * nswdev;

	if (swapblist == NULL)
		swapblist = blist_create(nswap);
	else
		blist_resize(&swapblist, nswap, 0);

	for (dvbase = dmmax; dvbase < aligned_nblks; dvbase += dmmax) {
		blk = min(aligned_nblks - dvbase, dmmax);
		vsbase = index * dmmax + dvbase * nswdev;
		blist_free(swapblist, vsbase, blk);
		vm_swap_size += blk;
		vm_swap_max += blk;
	}
	swap_pager_newswap();
	error = 0;
done:
	mtx_unlock(&swap_mtx);
	lwkt_reltoken(&vm_token);
	return (error);
}

/*
 * swapoff_args(char *name)
 *
 * System call swapoff(name) disables swapping on device name,
 * which must be an active swap device. Return ENOMEM
 * if there is not enough memory to page in the contents of
 * the given device.
 *
 * No requirements.
 */
int
sys_swapoff(struct swapoff_args *uap)
{
	struct vnode *vp;
	struct nlookupdata nd;
	struct swdevt *sp;
	int error, index;

	error = priv_check(curthread, PRIV_ROOT);
	if (error)
		return (error);

	mtx_lock(&swap_mtx);
	get_mplock();
	vp = NULL;
	error = nlookup_init(&nd, uap->name, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error)
		goto done;

	for (sp = swdevt, index = 0; index < nswdev; index++, sp++) {
		if (sp->sw_vp == vp)
			goto found;
	}
	error = EINVAL;
	goto done;
found:
	error = swapoff_one(index);

done:
	rel_mplock();
	mtx_unlock(&swap_mtx);
	return (error);
}

static int
swapoff_one(int index)
{
	swblk_t blk, aligned_nblks;
	swblk_t dvbase, vsbase;
	u_int pq_active_clean, pq_inactive_clean;
	struct swdevt *sp;
	struct vm_page marker;
	vm_page_t m;
	int q;

	mtx_lock(&swap_mtx);

	sp = &swdevt[index];
	aligned_nblks = sp->sw_nblks;
	pq_active_clean = pq_inactive_clean = 0;

	/*
	 * We can turn off this swap device safely only if the
	 * available virtual memory in the system will fit the amount
	 * of data we will have to page back in, plus an epsilon so
	 * the system doesn't become critically low on swap space.
	 */
	for (q = 0; q < PQ_L2_SIZE; ++q) {
		bzero(&marker, sizeof(marker));
		marker.flags = PG_BUSY | PG_FICTITIOUS | PG_MARKER;
		marker.queue = PQ_ACTIVE + q;
		marker.pc = q;
		marker.wire_count = 1;

		vm_page_queues_spin_lock(marker.queue);
		TAILQ_INSERT_HEAD(&vm_page_queues[marker.queue].pl,
				  &marker, pageq);

		while ((m = TAILQ_NEXT(&marker, pageq)) != NULL) {
			TAILQ_REMOVE(&vm_page_queues[marker.queue].pl,
				     &marker, pageq);
			TAILQ_INSERT_AFTER(&vm_page_queues[marker.queue].pl, m,
					   &marker, pageq);
			if (m->flags & (PG_MARKER | PG_FICTITIOUS))
				continue;

			if (vm_page_busy_try(m, FALSE) == 0) {
				vm_page_queues_spin_unlock(marker.queue);
				if (m->dirty == 0) {
					vm_page_test_dirty(m);
					if (m->dirty == 0)
						++pq_active_clean;
				}
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker.queue);
			}
		}
		TAILQ_REMOVE(&vm_page_queues[marker.queue].pl, &marker, pageq);
		vm_page_queues_spin_unlock(marker.queue);

		marker.queue = PQ_INACTIVE + q;
		marker.pc = q;
		vm_page_queues_spin_lock(marker.queue);
		TAILQ_INSERT_HEAD(&vm_page_queues[marker.queue].pl,
				  &marker, pageq);

		while ((m = TAILQ_NEXT(&marker, pageq)) != NULL) {
			TAILQ_REMOVE(
				&vm_page_queues[marker.queue].pl,
				&marker, pageq);
			TAILQ_INSERT_AFTER(
				&vm_page_queues[marker.queue].pl,
				m, &marker, pageq);
			if (m->flags & (PG_MARKER | PG_FICTITIOUS))
				continue;

			if (vm_page_busy_try(m, FALSE) == 0) {
				vm_page_queues_spin_unlock(marker.queue);
				if (m->dirty == 0) {
					vm_page_test_dirty(m);
					if (m->dirty == 0)
						++pq_inactive_clean;
				}
				vm_page_wakeup(m);
				vm_page_queues_spin_lock(marker.queue);
			}
		}
		TAILQ_REMOVE(&vm_page_queues[marker.queue].pl,
			     &marker, pageq);
		vm_page_queues_spin_unlock(marker.queue);
	}

	if (vmstats.v_free_count + vmstats.v_cache_count + pq_active_clean +
	    pq_inactive_clean + vm_swap_size < aligned_nblks + nswap_lowat) {
		mtx_unlock(&swap_mtx);
		return (ENOMEM);
	}

	/*
	 * Prevent further allocations on this device
	 */
	sp->sw_flags |= SW_CLOSING;
	for (dvbase = dmmax; dvbase < aligned_nblks; dvbase += dmmax) {
		blk = min(aligned_nblks - dvbase, dmmax);
		vsbase = index * dmmax + dvbase * nswdev;
		vm_swap_size -= blist_fill(swapblist, vsbase, blk);
		vm_swap_max -= blk;
	}

	/*
	 * Page in the contents of the device and close it.
	 */
	if (swap_pager_swapoff(index) && swap_pager_swapoff(index)) {
		mtx_unlock(&swap_mtx);
		return (EINTR);
	}

	vn_lock(sp->sw_vp, LK_EXCLUSIVE | LK_RETRY);
	VOP_CLOSE(sp->sw_vp, FREAD | FWRITE, NULL);
	vn_unlock(sp->sw_vp);
	vrele(sp->sw_vp);
	bzero(swdevt + index, sizeof(struct swdevt));

	/*
	 * Resize the bitmap based on the nem largest swap device,
	 * or free the bitmap if there are no more devices.
	 */
	for (sp = swdevt, aligned_nblks = 0; sp < swdevt + nswdev; sp++) {
		if (sp->sw_vp)
			aligned_nblks = max(aligned_nblks, sp->sw_nblks);
	}

	nswap = aligned_nblks * nswdev;

	if (nswap == 0) {
		blist_destroy(swapblist);
		swapblist = NULL;
		vrele(swapdev_vp);
		swapdev_vp = NULL;
	} else {
		blist_resize(&swapblist, nswap, 0);
	}

	mtx_unlock(&swap_mtx);
	return (0);
}

/*
 * Account for swap space in individual swdevt's.  The caller ensures
 * that the provided range falls into a single swdevt.
 *
 * +count	space freed
 * -count	space allocated
 */
void
swapacctspace(swblk_t base, swblk_t count)
{
	int index;
	int seg;

	vm_swap_size += count;
	seg = base / dmmax;
	index = seg % nswdev;
	swdevt[index].sw_nused -= count;
}

/*
 * Retrieve swap info
 */
static int
sysctl_vm_swap_info(SYSCTL_HANDLER_ARGS)
{
	struct xswdev xs;
	struct swdevt *sp;
	int	error;
	int	n;

	error = 0;
	for (n = 0; n < nswdev; ++n) {
		sp = &swdevt[n];

		xs.xsw_size = sizeof(xs);
		xs.xsw_version = XSWDEV_VERSION;
		xs.xsw_blksize = PAGE_SIZE;
		xs.xsw_dev = sp->sw_dev;
		xs.xsw_flags = sp->sw_flags;
		xs.xsw_nblks = sp->sw_nblks;
		xs.xsw_used = sp->sw_nused;

		error = SYSCTL_OUT(req, &xs, sizeof(xs));
		if (error)
			break;
	}
	return (error);
}

SYSCTL_INT(_vm, OID_AUTO, nswapdev, CTLFLAG_RD, &nswdev, 0,
	   "Number of swap devices");
SYSCTL_NODE(_vm, OID_AUTO, swap_info_array, CTLFLAG_RD, sysctl_vm_swap_info,
	    "Swap statistics by device");
