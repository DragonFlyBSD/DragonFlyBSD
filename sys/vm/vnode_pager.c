/*
 * (MPSAFE)
 *
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1993, 1994 John S. Dyson
 * Copyright (c) 1995, David Greenman
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	from: @(#)vnode_pager.c	7.5 (Berkeley) 4/20/91
 * $FreeBSD: src/sys/vm/vnode_pager.c,v 1.116.2.7 2002/12/31 09:34:51 dillon Exp $
 */

/*
 * Page to/from files (vnodes).
 */

/*
 * TODO:
 *	Implement VOP_GETPAGES/PUTPAGES interface for filesystems. Will
 *	greatly re-simplify the vnode_pager.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/vmmeter.h>
#include <sys/conf.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <vm/vnode_pager.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>

#include <sys/thread2.h>
#include <vm/vm_page2.h>

static void vnode_pager_dealloc (vm_object_t);
static int vnode_pager_getpage (vm_object_t, vm_page_t *, int);
static void vnode_pager_putpages (vm_object_t, vm_page_t *, int, int, int *);
static boolean_t vnode_pager_haspage (vm_object_t, vm_pindex_t);

struct pagerops vnodepagerops = {
	vnode_pager_dealloc,
	vnode_pager_getpage,
	vnode_pager_putpages,
	vnode_pager_haspage
};

static struct krate vbadrate = { 1 };
static struct krate vresrate = { 1 };

long vnode_pbuf_freecnt = -1;	/* start out unlimited */

/*
 * Allocate a VM object for a vnode, typically a regular file vnode.
 *
 * Some additional information is required to generate a properly sized
 * object which covers the entire buffer cache buffer straddling the file
 * EOF.  Userland does not see the extra pages as the VM fault code tests
 * against v_filesize.
 */
vm_object_t
vnode_pager_alloc(void *handle, off_t length, vm_prot_t prot, off_t offset,
		  int blksize, int boff)
{
	vm_object_t object;
	struct vnode *vp;
	off_t loffset;
	vm_pindex_t lsize;

	/*
	 * Pageout to vnode, no can do yet.
	 */
	if (handle == NULL)
		return (NULL);

	/*
	 * XXX hack - This initialization should be put somewhere else.
	 */
	if (vnode_pbuf_freecnt < 0) {
	    vnode_pbuf_freecnt = nswbuf / 2 + 1;
	}

	/*
	 * Serialize potential vnode/object teardowns and interlocks
	 */
	vp = (struct vnode *)handle;
	lwkt_gettoken(&vp->v_token);

	/*
	 * If the object is being terminated, wait for it to
	 * go away.
	 */
	object = vp->v_object;
	if (object) {
		vm_object_hold(object);
		KKASSERT((object->flags & OBJ_DEAD) == 0);
	}

	if (VREFCNT(vp) <= 0)
		panic("vnode_pager_alloc: no vnode reference");

	/*
	 * Round up to the *next* block, then destroy the buffers in question.
	 * Since we are only removing some of the buffers we must rely on the
	 * scan count to determine whether a loop is necessary.
	 *
	 * Destroy any pages beyond the last buffer.
	 */
	if (boff < 0)
		boff = (int)(length % blksize);
	if (boff)
		loffset = length + (blksize - boff);
	else
		loffset = length;
	lsize = OFF_TO_IDX(round_page64(loffset));

	if (object == NULL) {
		/*
		 * And an object of the appropriate size
		 */
		object = vm_object_allocate_hold(OBJT_VNODE, lsize);
		object->handle = handle;
		vp->v_object = object;
		vp->v_filesize = length;
		if (vp->v_mount && (vp->v_mount->mnt_kern_flag & MNTK_NOMSYNC))
			vm_object_set_flag(object, OBJ_NOMSYNC);
		vref(vp);
	} else {
		vm_object_reference_quick(object);	/* also vref's */
		if (object->size != lsize) {
			kprintf("vnode_pager_alloc: Warning, objsize "
				"mismatch %jd/%jd vp=%p obj=%p\n",
				(intmax_t)object->size,
				(intmax_t)lsize,
				vp, object);
		}
		if (vp->v_filesize != length) {
			kprintf("vnode_pager_alloc: Warning, filesize "
				"mismatch %jd/%jd vp=%p obj=%p\n",
				(intmax_t)vp->v_filesize,
				(intmax_t)length,
				vp, object);
		}
	}
	vm_object_drop(object);
	lwkt_reltoken(&vp->v_token);

	return (object);
}

/*
 * Add a ref to a vnode's existing VM object, return the object or
 * NULL if the vnode did not have one.  This does not create the
 * object (we can't since we don't know what the proper blocksize/boff
 * is to match the VFS's use of the buffer cache).
 *
 * The vnode must be referenced and is typically open.  The object should
 * be stable in this situation.
 *
 * Returns the object with an additional reference but not locked.
 */
vm_object_t
vnode_pager_reference(struct vnode *vp)
{
	vm_object_t object;

	if ((object = vp->v_object) != NULL)
		vm_object_reference_quick(object); /* also vref's vnode */
	return (object);
}

static void
vnode_pager_dealloc(vm_object_t object)
{
	struct vnode *vp = object->handle;

	if (vp == NULL)
		panic("vnode_pager_dealloc: pager already dealloced");

	vm_object_pip_wait(object, "vnpdea");

	object->handle = NULL;
	object->type = OBJT_DEAD;
	vp->v_object = NULL;
	vp->v_filesize = NOOFFSET;
	vclrflags(vp, VTEXT | VOBJBUF);
	swap_pager_freespace_all(object);
}

/*
 * Return whether the vnode pager has the requested page.  Return the
 * number of disk-contiguous pages before and after the requested page,
 * not including the requested page.
 */
static boolean_t
vnode_pager_haspage(vm_object_t object, vm_pindex_t pindex)
{
	struct vnode *vp = object->handle;
	off_t loffset;
	off_t doffset;
	int voff;
	int bsize;
	int error;

	/*
	 * If no vp or vp is doomed or marked transparent to VM, we do not
	 * have the page.
	 */
	if ((vp == NULL) || (vp->v_flag & VRECLAIMED))
		return FALSE;

	/*
	 * If filesystem no longer mounted or offset beyond end of file we do
	 * not have the page.
	 */
	loffset = IDX_TO_OFF(pindex);

	if (vp->v_mount == NULL || loffset >= vp->v_filesize)
		return FALSE;

	bsize = vp->v_mount->mnt_stat.f_iosize;
	voff = loffset % bsize;

	/*
	 * XXX
	 *
	 * BMAP returns byte counts before and after, where after
	 * is inclusive of the base page.  haspage must return page
	 * counts before and after where after does not include the
	 * base page.
	 *
	 * BMAP is allowed to return a *after of 0 for backwards
	 * compatibility.  The base page is still considered valid if
	 * no error is returned.
	 */
	error = VOP_BMAP(vp, loffset - voff, &doffset, NULL, NULL, 0);
	if (error)
		return TRUE;
	if (doffset == NOOFFSET)
		return FALSE;
	return TRUE;
}

/*
 * Lets the VM system know about a change in size for a file.
 * We adjust our own internal size and flush any cached pages in
 * the associated object that are affected by the size change.
 *
 * NOTE: This routine may be invoked as a result of a pager put
 * operation (possibly at object termination time), so we must be careful.
 *
 * NOTE: vp->v_filesize is initialized to NOOFFSET (-1), be sure that
 * we do not blow up on the case.  nsize will always be >= 0, however.
 */
void
vnode_pager_setsize(struct vnode *vp, vm_ooffset_t nsize)
{
	vm_pindex_t nobjsize;
	vm_pindex_t oobjsize;
	vm_object_t object;

	object = vp->v_object;
	if (object == NULL)
		return;
	vm_object_hold(object);
	KKASSERT(vp->v_object == object);

	/*
	 * Hasn't changed size
	 */
	if (nsize == vp->v_filesize) {
		vm_object_drop(object);
		return;
	}

	/*
	 * Has changed size.  Adjust the VM object's size and v_filesize
	 * before we start scanning pages to prevent new pages from being
	 * allocated during the scan.
	 */
	nobjsize = OFF_TO_IDX(nsize + PAGE_MASK);
	oobjsize = object->size;
	object->size = nobjsize;

	/*
	 * File has shrunk. Toss any cached pages beyond the new EOF.
	 */
	if (nsize < vp->v_filesize) {
		vp->v_filesize = nsize;
		if (nobjsize < oobjsize) {
			vm_object_page_remove(object, nobjsize, oobjsize,
					      FALSE);
		}
		/*
		 * This gets rid of garbage at the end of a page that is now
		 * only partially backed by the vnode.  Since we are setting
		 * the entire page valid & clean after we are done we have
		 * to be sure that the portion of the page within the file
		 * bounds is already valid.  If it isn't then making it
		 * valid would create a corrupt block.
		 */
		if (nsize & PAGE_MASK) {
			vm_offset_t kva;
			vm_page_t m;

			m = vm_page_lookup_busy_wait(object, OFF_TO_IDX(nsize),
						     TRUE, "vsetsz");

			if (m && m->valid) {
				int base = (int)nsize & PAGE_MASK;
				int size = PAGE_SIZE - base;
				struct lwbuf *lwb;
				struct lwbuf lwb_cache;

				/*
				 * Clear out partial-page garbage in case
				 * the page has been mapped.
				 *
				 * This is byte aligned.
				 */
				lwb = lwbuf_alloc(m, &lwb_cache);
				kva = lwbuf_kva(lwb);
				bzero((caddr_t)kva + base, size);
				lwbuf_free(lwb);

				/*
				 * XXX work around SMP data integrity race
				 * by unmapping the page from user processes.
				 * The garbage we just cleared may be mapped
				 * to a user process running on another cpu
				 * and this code is not running through normal
				 * I/O channels which handle SMP issues for
				 * us, so unmap page to synchronize all cpus.
				 *
				 * XXX should vm_pager_unmap_page() have
				 * dealt with this?
				 */
				vm_page_protect(m, VM_PROT_NONE);

				/*
				 * Clear out partial-page dirty bits.  This
				 * has the side effect of setting the valid
				 * bits, but that is ok.  There are a bunch
				 * of places in the VM system where we expected
				 * m->dirty == VM_PAGE_BITS_ALL.  The file EOF
				 * case is one of them.  If the page is still
				 * partially dirty, make it fully dirty.
				 *
				 * NOTE: We do not clear out the valid
				 * bits.  This would prevent bogus_page
				 * replacement from working properly.
				 *
				 * NOTE: We do not want to clear the dirty
				 * bit for a partial DEV_BSIZE'd truncation!
				 * This is DEV_BSIZE aligned!
				 */
				vm_page_clear_dirty_beg_nonincl(m, base, size);
				if (m->dirty != 0)
					m->dirty = VM_PAGE_BITS_ALL;
				vm_page_wakeup(m);
			} else if (m) {
				vm_page_wakeup(m);
			}
		}
	} else {
		vp->v_filesize = nsize;
	}
	vm_object_drop(object);
}

/*
 * Release a page busied for a getpages operation.  The page may have become
 * wired (typically due to being used by the buffer cache) or otherwise been
 * soft-busied and cannot be freed in that case.  A held page can still be
 * freed.
 */
void
vnode_pager_freepage(vm_page_t m)
{
	if (m->busy || m->wire_count || (m->flags & PG_NEED_COMMIT)) {
		vm_page_activate(m);
		vm_page_wakeup(m);
	} else {
		vm_page_free(m);
	}
}

/*
 * EOPNOTSUPP is no longer legal.  For local media VFS's that do not
 * implement their own VOP_GETPAGES, their VOP_GETPAGES should call to
 * vnode_pager_generic_getpages() to implement the previous behaviour.
 *
 * All other FS's should use the bypass to get to the local media
 * backing vp's VOP_GETPAGES.
 */
static int
vnode_pager_getpage(vm_object_t object, vm_page_t *mpp, int seqaccess)
{
	int rtval;
	struct vnode *vp;

	vp = object->handle;
	rtval = VOP_GETPAGES(vp, mpp, PAGE_SIZE, 0, 0, seqaccess);
	if (rtval == EOPNOTSUPP)
		panic("vnode_pager: vfs's must implement vop_getpages");
	return rtval;
}

/*
 * This is now called from local media FS's to operate against their
 * own vnodes if they fail to implement VOP_GETPAGES.
 *
 * With all the caching local media devices do these days there is really
 * very little point to attempting to restrict the I/O size to contiguous
 * blocks on-disk, especially if our caller thinks we need all the specified
 * pages.  Just construct and issue a READ.
 */
int
vnode_pager_generic_getpages(struct vnode *vp, vm_page_t *mpp, int bytecount,
			     int reqpage, int seqaccess)
{
	struct iovec aiov;
	struct uio auio;
	off_t foff;
	int error;
	int count;
	int i;
	int ioflags;

	/*
	 * Do not do anything if the vnode is bad.
	 */
	if (vp->v_mount == NULL)
		return VM_PAGER_BAD;

	/*
	 * Calculate the number of pages.  Since we are paging in whole
	 * pages, adjust bytecount to be an integral multiple of the page
	 * size.  It will be clipped to the file EOF later on.
	 */
	bytecount = round_page(bytecount);
	count = bytecount / PAGE_SIZE;

	/*
	 * We could check m[reqpage]->valid here and shortcut the operation,
	 * but doing so breaks read-ahead.  Instead assume that the VM
	 * system has already done at least the check, don't worry about
	 * any races, and issue the VOP_READ to allow read-ahead to function.
	 *
	 * This keeps the pipeline full for I/O bound sequentially scanned
	 * mmap()'s
	 */
	/* don't shortcut */

	/*
	 * Discard pages past the file EOF.  If the requested page is past
	 * the file EOF we just leave its valid bits set to 0, the caller
	 * expects to maintain ownership of the requested page.  If the
	 * entire range is past file EOF discard everything and generate
	 * a pagein error.
	 */
	foff = IDX_TO_OFF(mpp[0]->pindex);
	if (foff >= vp->v_filesize) {
		for (i = 0; i < count; i++) {
			if (i != reqpage)
				vnode_pager_freepage(mpp[i]);
		}
		return VM_PAGER_ERROR;
	}

	if (foff + bytecount > vp->v_filesize) {
		bytecount = vp->v_filesize - foff;
		i = round_page(bytecount) / PAGE_SIZE;
		while (count > i) {
			--count;
			if (count != reqpage)
				vnode_pager_freepage(mpp[count]);
		}
	}

	/*
	 * The size of the transfer is bytecount.  bytecount will be an
	 * integral multiple of the page size unless it has been clipped
	 * to the file EOF.  The transfer cannot exceed the file EOF.
	 *
	 * When dealing with real devices we must round-up to the device
	 * sector size.
	 */
	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		int secmask = vp->v_rdev->si_bsize_phys - 1;
		KASSERT(secmask < PAGE_SIZE, ("vnode_pager_generic_getpages: sector size %d too large", secmask + 1));
		bytecount = (bytecount + secmask) & ~secmask;
	}

	/*
	 * Severe hack to avoid deadlocks with the buffer cache
	 */
	for (i = 0; i < count; ++i) {
		vm_page_t mt = mpp[i];

		vm_page_io_start(mt);
		vm_page_wakeup(mt);
	}

	/*
	 * Issue the I/O with some read-ahead if bytecount > PAGE_SIZE
	 */
	ioflags = IO_VMIO;
	if (seqaccess)
		ioflags |= IO_SEQMAX << IO_SEQSHIFT;

	aiov.iov_base = NULL;
	aiov.iov_len = bytecount;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = foff;
	auio.uio_segflg = UIO_NOCOPY;
	auio.uio_rw = UIO_READ;
	auio.uio_resid = bytecount;
	auio.uio_td = NULL;
	mycpu->gd_cnt.v_vnodein++;
	mycpu->gd_cnt.v_vnodepgsin += count;

	error = VOP_READ(vp, &auio, ioflags, proc0.p_ucred);

	/*
	 * Severe hack to avoid deadlocks with the buffer cache
	 */
	for (i = 0; i < count; ++i) {
		vm_page_busy_wait(mpp[i], FALSE, "getpgs");
		vm_page_io_finish(mpp[i]);
	}

	/*
	 * Calculate the actual number of bytes read and clean up the
	 * page list.  
	 */
	bytecount -= auio.uio_resid;

	for (i = 0; i < count; ++i) {
		vm_page_t mt = mpp[i];

		if (i != reqpage) {
			if (error == 0 && mt->valid) {
				if (mt->flags & PG_REFERENCED)
					vm_page_activate(mt);
				else
					vm_page_deactivate(mt);
				vm_page_wakeup(mt);
			} else {
				vnode_pager_freepage(mt);
			}
		} else if (mt->valid == 0) {
			if (error == 0) {
				kprintf("page failed but no I/O error page "
					"%p object %p pindex %d\n",
					mt, mt->object, (int) mt->pindex);
				/* whoops, something happened */
				error = EINVAL;
			}
		} else if (mt->valid != VM_PAGE_BITS_ALL) {
			/*
			 * Zero-extend the requested page if necessary (if
			 * the filesystem is using a small block size).
			 */
			vm_page_zero_invalid(mt, TRUE);
		}
	}
	if (error) {
		kprintf("vnode_pager_getpage: I/O read error\n");
	}
	return (error ? VM_PAGER_ERROR : VM_PAGER_OK);
}

/*
 * EOPNOTSUPP is no longer legal.  For local media VFS's that do not
 * implement their own VOP_PUTPAGES, their VOP_PUTPAGES should call to
 * vnode_pager_generic_putpages() to implement the previous behaviour.
 *
 * Caller has already cleared the pmap modified bits, if any.
 *
 * All other FS's should use the bypass to get to the local media
 * backing vp's VOP_PUTPAGES.
 */
static void
vnode_pager_putpages(vm_object_t object, vm_page_t *m, int count,
		     int sync, int *rtvals)
{
	int rtval;
	struct vnode *vp;
	int bytes = count * PAGE_SIZE;

	/*
	 * Force synchronous operation if we are extremely low on memory
	 * to prevent a low-memory deadlock.  VOP operations often need to
	 * allocate more memory to initiate the I/O ( i.e. do a BMAP 
	 * operation ).  The swapper handles the case by limiting the amount
	 * of asynchronous I/O, but that sort of solution doesn't scale well
	 * for the vnode pager without a lot of work.
	 *
	 * Also, the backing vnode's iodone routine may not wake the pageout
	 * daemon up.  This should be probably be addressed XXX.
	 */

	if ((vmstats.v_free_count + vmstats.v_cache_count) <
	    vmstats.v_pageout_free_min) {
		sync |= OBJPC_SYNC;
	}

	/*
	 * Call device-specific putpages function
	 */
	vp = object->handle;
	rtval = VOP_PUTPAGES(vp, m, bytes, sync, rtvals, 0);
	if (rtval == EOPNOTSUPP) {
	    kprintf("vnode_pager: *** WARNING *** stale FS putpages\n");
	    rtval = vnode_pager_generic_putpages( vp, m, bytes, sync, rtvals);
	}
}


/*
 * This is now called from local media FS's to operate against their
 * own vnodes if they fail to implement VOP_PUTPAGES.
 *
 * This is typically called indirectly via the pageout daemon and
 * clustering has already typically occured, so in general we ask the
 * underlying filesystem to write the data out asynchronously rather
 * then delayed.
 */
int
vnode_pager_generic_putpages(struct vnode *vp, vm_page_t *m, int bytecount,
			     int flags, int *rtvals)
{
	int i;
	int maxsize, ncount, count;
	vm_ooffset_t poffset;
	struct uio auio;
	struct iovec aiov;
	int error;
	int ioflags;

	count = bytecount / PAGE_SIZE;

	for (i = 0; i < count; i++)
		rtvals[i] = VM_PAGER_AGAIN;

	if ((int) m[0]->pindex < 0) {
		kprintf("vnode_pager_putpages: attempt to write meta-data!!! -- 0x%lx(%x)\n",
			(long)m[0]->pindex, m[0]->dirty);
		rtvals[0] = VM_PAGER_BAD;
		return VM_PAGER_BAD;
	}

	maxsize = count * PAGE_SIZE;
	ncount = count;

	poffset = IDX_TO_OFF(m[0]->pindex);

	/*
	 * If the page-aligned write is larger then the actual file we
	 * have to invalidate pages occuring beyond the file EOF.
	 *
	 * If the file EOF resides in the middle of a page we still clear
	 * all of that page's dirty bits later on.  If we didn't it would
	 * endlessly re-write.
	 *
	 * We do not under any circumstances truncate the valid bits, as
	 * this will screw up bogus page replacement.
	 *
	 * The caller has already read-protected the pages.  The VFS must
	 * use the buffer cache to wrap the pages.  The pages might not
	 * be immediately flushed by the buffer cache but once under its
	 * control the pages themselves can wind up being marked clean
	 * and their covering buffer cache buffer can be marked dirty.
	 */
	if (poffset + maxsize > vp->v_filesize) {
		if (poffset < vp->v_filesize) {
			maxsize = vp->v_filesize - poffset;
			ncount = btoc(maxsize);
		} else {
			maxsize = 0;
			ncount = 0;
		}
		if (ncount < count) {
			for (i = ncount; i < count; i++) {
				rtvals[i] = VM_PAGER_BAD;
			}
		}
	}

	/*
	 * pageouts are already clustered, use IO_ASYNC to force a bawrite()
	 * rather then a bdwrite() to prevent paging I/O from saturating
	 * the buffer cache.  Dummy-up the sequential heuristic to cause
	 * large ranges to cluster.  If neither IO_SYNC or IO_ASYNC is set,
	 * the system decides how to cluster.
	 */
	ioflags = IO_VMIO;
	if (flags & (VM_PAGER_PUT_SYNC | VM_PAGER_PUT_INVAL))
		ioflags |= IO_SYNC;
	else if ((flags & VM_PAGER_CLUSTER_OK) == 0)
		ioflags |= IO_ASYNC;
	ioflags |= (flags & VM_PAGER_PUT_INVAL) ? IO_INVAL: 0;
	ioflags |= IO_SEQMAX << IO_SEQSHIFT;

	aiov.iov_base = (caddr_t) 0;
	aiov.iov_len = maxsize;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = poffset;
	auio.uio_segflg = UIO_NOCOPY;
	auio.uio_rw = UIO_WRITE;
	auio.uio_resid = maxsize;
	auio.uio_td = NULL;
	error = VOP_WRITE(vp, &auio, ioflags, proc0.p_ucred);
	mycpu->gd_cnt.v_vnodeout++;
	mycpu->gd_cnt.v_vnodepgsout += ncount;

	if (error) {
		krateprintf(&vbadrate,
			    "vnode_pager_putpages: I/O error %d\n", error);
	}
	if (auio.uio_resid) {
		krateprintf(&vresrate,
			    "vnode_pager_putpages: residual I/O %zd at %lu\n",
			    auio.uio_resid, (u_long)m[0]->pindex);
	}
	if (error == 0) {
		for (i = 0; i < ncount; i++) {
			rtvals[i] = VM_PAGER_OK;
			vm_page_undirty(m[i]);
		}
	}
	return rtvals[0];
}

/*
 * Run the chain and if the bottom-most object is a vnode-type lock the
 * underlying vnode.  A locked vnode or NULL is returned.
 */
struct vnode *
vnode_pager_lock(vm_object_t object)
{
	struct vnode *vp = NULL;
	vm_object_t lobject;
	vm_object_t tobject;
	int error;

	if (object == NULL)
		return(NULL);

	ASSERT_LWKT_TOKEN_HELD(vm_object_token(object));
	lobject = object;

	while (lobject->type != OBJT_VNODE) {
		if (lobject->flags & OBJ_DEAD)
			break;
		tobject = lobject->backing_object;
		if (tobject == NULL)
			break;
		vm_object_hold_shared(tobject);
		if (tobject == lobject->backing_object) {
			if (lobject != object) {
				vm_object_lock_swap();
				vm_object_drop(lobject);
			}
			lobject = tobject;
		} else {
			vm_object_drop(tobject);
		}
	}
	while (lobject->type == OBJT_VNODE &&
	       (lobject->flags & OBJ_DEAD) == 0) {
		/*
		 * Extract the vp
		 */
		vp = lobject->handle;
		error = vget(vp, LK_SHARED | LK_RETRY | LK_CANRECURSE);
		if (error == 0) {
			if (lobject->handle == vp)
				break;
			vput(vp);
		} else {
			kprintf("vnode_pager_lock: vp %p error %d "
				"lockstatus %d, retrying\n",
				vp, error,
				lockstatus(&vp->v_lock, curthread));
			tsleep(object->handle, 0, "vnpgrl", hz);
		}
		vp = NULL;
	}
	if (lobject != object)
		vm_object_drop(lobject);
	return (vp);
}
