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
 * Implements new VFS/VM coherency functions.  For conforming VFSs
 * we treat the backing VM object slightly differently.  Instead of
 * maintaining a number of pages to exactly fit the size of the file
 * we instead maintain pages to fit the entire contents of the last
 * buffer cache buffer used by the file.
 *
 * For VFSs like NFS and HAMMER which use (generally speaking) fixed
 * sized buffers this greatly reduces the complexity of VFS/VM interactions.
 *
 * Truncations no longer invalidate pages covered by the buffer cache
 * beyond the file EOF which still fit within the file's last buffer.
 * We simply unmap them and do not allow userland to fault them in.
 *
 * The VFS is no longer responsible for zero-filling buffers during a
 * truncation, the last buffer will be automatically zero-filled by
 * nvtruncbuf().
 *
 * This code is intended to (eventually) replace vtruncbuf() and
 * vnode_pager_setsize().
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <machine/limits.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <vm/vm_page2.h>

static int nvtruncbuf_bp_trunc_cmp(struct buf *bp, void *data);
static int nvtruncbuf_bp_trunc(struct buf *bp, void *data);
static int nvtruncbuf_bp_metasync_cmp(struct buf *bp, void *data);
static int nvtruncbuf_bp_metasync(struct buf *bp, void *data);

/*
 * Truncate a file's buffer and pages to a specified length. The
 * byte-granular length of the file is specified along with the block
 * size of the buffer containing that offset.
 *
 * If the last buffer straddles the length its contents will be zero-filled
 * as appropriate.  All buffers and pages after the last buffer will be
 * destroyed.  The last buffer itself will be destroyed only if the length
 * is exactly aligned with it.
 *
 * UFS typically passes the old block size prior to the actual truncation,
 * then later resizes the block based on the new file size.  NFS uses a
 * fixed block size and doesn't care.  HAMMER uses a block size based on
 * the offset which is fixed for any particular offset.
 *
 * When zero-filling we must bdwrite() to avoid a window of opportunity
 * where the kernel might throw away a clean buffer and the filesystem
 * then attempts to bread() it again before completing (or as part of)
 * the extension.  The filesystem is still responsible for zero-filling
 * any remainder when writing to the media in the strategy function when
 * it is able to do so without the page being mapped.  The page may still
 * be mapped by userland here.
 *
 * When modifying a buffer we must clear any cached raw disk offset.
 * bdwrite() will call BMAP on it again.  Some filesystems, like HAMMER,
 * never overwrite existing data blocks.
 */

struct truncbuf_info {
	struct vnode *vp;
	off_t truncloffset;	/* truncation point */
	int clean;		/* clean tree, else dirty tree */
};

int
nvtruncbuf(struct vnode *vp, off_t length, int blksize, int boff, int trivial)
{
	struct truncbuf_info info;
	off_t truncboffset;
	const char *filename;
	struct buf *bp;
	int count;
	int error;

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
		info.truncloffset = length + (blksize - boff);
	else
		info.truncloffset = length;
	info.vp = vp;
	lwkt_gettoken(&vp->v_token);
	do {
		info.clean = 1;
		count = RB_SCAN(buf_rb_tree, &vp->v_rbclean_tree,
				nvtruncbuf_bp_trunc_cmp,
				nvtruncbuf_bp_trunc, &info);
		info.clean = 0;
		count += RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
				nvtruncbuf_bp_trunc_cmp,
				nvtruncbuf_bp_trunc, &info);
	} while(count);

	nvnode_pager_setsize(vp, length, blksize, boff);

	/*
	 * Zero-fill the area beyond the file EOF that still fits within
	 * the last buffer.  We must mark the buffer as dirty even though
	 * the modified area is beyond EOF to avoid races where the kernel
	 * might flush the buffer before the filesystem is able to reallocate
	 * the block.
	 *
	 * The VFS is responsible for dealing with the actual truncation.
	 *
	 * Only do this if trivial is zero, otherwise it is up to the
	 * VFS to handle the block straddling the EOF.
	 */
	if (boff && trivial == 0) {
		truncboffset = length - boff;
		error = bread(vp, truncboffset, blksize, &bp);
		if (error == 0) {
			bzero(bp->b_data + boff, blksize - boff);
			if (bp->b_flags & B_DELWRI) {
				if (bp->b_dirtyoff > boff)
					bp->b_dirtyoff = boff;
				if (bp->b_dirtyend > boff)
					bp->b_dirtyend = boff;
			}
			bp->b_bio2.bio_offset = NOOFFSET;
			bdwrite(bp);
		}
	} else {
		error = 0;
	}

	/*
	 * For safety, fsync any remaining metadata if the file is not being
	 * truncated to 0.  Since the metadata does not represent the entire
	 * dirty list we have to rely on the hit count to ensure that we get
	 * all of it.
	 *
	 * This is typically applicable only to UFS.  NFS and HAMMER do
	 * not store indirect blocks in the per-vnode buffer cache.
	 */
	if (length > 0) {
		do {
			count = RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
					nvtruncbuf_bp_metasync_cmp,
					nvtruncbuf_bp_metasync, &info);
		} while (count);
	}

	/*
	 * It is possible to have in-progress I/O from buffers that were
	 * not part of the truncation.  This should not happen if we
	 * are truncating to 0-length.
	 */
	bio_track_wait(&vp->v_track_write, 0, 0);

	/*
	 * Debugging only
	 */
	spin_lock(&vp->v_spin);
	filename = TAILQ_FIRST(&vp->v_namecache) ?
		   TAILQ_FIRST(&vp->v_namecache)->nc_name : "?";
	spin_unlock(&vp->v_spin);

	/*
	 * Make sure no buffers were instantiated while we were trying
	 * to clean out the remaining VM pages.  This could occur due
	 * to busy dirty VM pages being flushed out to disk.
	 */
	do {
		info.clean = 1;
		count = RB_SCAN(buf_rb_tree, &vp->v_rbclean_tree,
				nvtruncbuf_bp_trunc_cmp,
				nvtruncbuf_bp_trunc, &info);
		info.clean = 0;
		count += RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree,
				nvtruncbuf_bp_trunc_cmp,
				nvtruncbuf_bp_trunc, &info);
		if (count) {
			kprintf("Warning: vtruncbuf():  Had to re-clean %d "
			       "left over buffers in %s\n", count, filename);
		}
	} while(count);

	lwkt_reltoken(&vp->v_token);

	return (error);
}

/*
 * The callback buffer is beyond the new file EOF and must be destroyed.
 * Note that the compare function must conform to the RB_SCAN's requirements.
 */
static
int
nvtruncbuf_bp_trunc_cmp(struct buf *bp, void *data)
{
	struct truncbuf_info *info = data;

	if (bp->b_loffset >= info->truncloffset)
		return(0);
	return(-1);
}

static
int
nvtruncbuf_bp_trunc(struct buf *bp, void *data)
{
	struct truncbuf_info *info = data;

	/*
	 * Do not try to use a buffer we cannot immediately lock,
	 * but sleep anyway to prevent a livelock.  The code will
	 * loop until all buffers can be acted upon.
	 */
	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		atomic_add_int(&bp->b_refs, 1);
		if (BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL) == 0)
			BUF_UNLOCK(bp);
		atomic_subtract_int(&bp->b_refs, 1);
	} else if ((info->clean && (bp->b_flags & B_DELWRI)) ||
		   (info->clean == 0 && (bp->b_flags & B_DELWRI) == 0) ||
		   bp->b_vp != info->vp ||
		   nvtruncbuf_bp_trunc_cmp(bp, data)) {
		BUF_UNLOCK(bp);
	} else {
		bremfree(bp);
		bp->b_flags |= (B_INVAL | B_RELBUF | B_NOCACHE);
		brelse(bp);
	}
	lwkt_yield();
	return(1);
}

/*
 * Fsync all meta-data after truncating a file to be non-zero.  Only metadata
 * blocks (with a negative loffset) are scanned.
 * Note that the compare function must conform to the RB_SCAN's requirements.
 */
static int
nvtruncbuf_bp_metasync_cmp(struct buf *bp, void *data __unused)
{
	if (bp->b_loffset < 0)
		return(0);
	lwkt_yield();
	return(1);
}

static int
nvtruncbuf_bp_metasync(struct buf *bp, void *data)
{
	struct truncbuf_info *info = data;

	/*
	 * Do not try to use a buffer we cannot immediately lock,
	 * but sleep anyway to prevent a livelock.  The code will
	 * loop until all buffers can be acted upon.
	 */
	if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
		atomic_add_int(&bp->b_refs, 1);
		if (BUF_LOCK(bp, LK_EXCLUSIVE|LK_SLEEPFAIL) == 0)
			BUF_UNLOCK(bp);
		atomic_subtract_int(&bp->b_refs, 1);
	} else if ((bp->b_flags & B_DELWRI) == 0 ||
		   bp->b_vp != info->vp ||
		   nvtruncbuf_bp_metasync_cmp(bp, data)) {
		BUF_UNLOCK(bp);
	} else {
		bremfree(bp);
		bawrite(bp);
	}
	lwkt_yield();
	return(1);
}

/*
 * Extend a file's buffer and pages to a new, larger size.  The block size
 * at both the old and new length must be passed, but buffer cache operations
 * will only be performed on the old block.  The new nlength/nblksize will
 * be used to properly set the VM object size.
 *
 * To make this explicit we require the old length to passed even though
 * we can acquire it from vp->v_filesize, which also avoids potential
 * corruption if the filesystem and vp get desynchronized somehow.
 *
 * If the caller intends to immediately write into the newly extended
 * space pass trivial == 1.  If trivial is 0 the original buffer will be
 * zero-filled as necessary to clean out any junk in the extended space.
 * If non-zero the original buffer (straddling EOF) is not touched.
 *
 * When zero-filling we must bdwrite() to avoid a window of opportunity
 * where the kernel might throw away a clean buffer and the filesystem
 * then attempts to bread() it again before completing (or as part of)
 * the extension.  The filesystem is still responsible for zero-filling
 * any remainder when writing to the media in the strategy function when
 * it is able to do so without the page being mapped.  The page may still
 * be mapped by userland here.
 *
 * When modifying a buffer we must clear any cached raw disk offset.
 * bdwrite() will call BMAP on it again.  Some filesystems, like HAMMER,
 * never overwrite existing data blocks.
 */
int
nvextendbuf(struct vnode *vp, off_t olength, off_t nlength,
	    int oblksize, int nblksize, int oboff, int nboff, int trivial)
{
	off_t truncboffset;
	struct buf *bp;
	int error;

	error = 0;
	nvnode_pager_setsize(vp, nlength, nblksize, nboff);
	if (trivial == 0) {
		if (oboff < 0)
			oboff = (int)(olength % oblksize);
		truncboffset = olength - oboff;

		if (oboff) {
			error = bread(vp, truncboffset, oblksize, &bp);
			if (error == 0) {
				bzero(bp->b_data + oboff, oblksize - oboff);
				bp->b_bio2.bio_offset = NOOFFSET;
				bdwrite(bp);
			}
		}
	}
	return (error);
}

/*
 * Set vp->v_filesize and vp->v_object->size, destroy pages beyond
 * the last buffer when truncating.
 *
 * This function does not do any zeroing or invalidating of partially
 * overlapping pages.  Zeroing is the responsibility of nvtruncbuf().
 * However, it does unmap VM pages from the user address space on a
 * page-granular (verses buffer cache granular) basis.
 *
 * If boff is passed as -1 the base offset of the buffer cache buffer is
 * calculated from length and blksize.  Filesystems such as UFS which deal
 * with fragments have to specify a boff >= 0 since the base offset cannot
 * be calculated from length and blksize.
 *
 * For UFS blksize is the 'new' blocksize, used only to determine how large
 * the VM object must become.
 */
void
nvnode_pager_setsize(struct vnode *vp, off_t length, int blksize, int boff)
{
	vm_pindex_t nobjsize;
	vm_pindex_t oobjsize;
	vm_pindex_t pi;
	vm_object_t object;
	vm_page_t m;
	off_t truncboffset;

	/*
	 * Degenerate conditions
	 */
	if ((object = vp->v_object) == NULL)
		return;
	vm_object_hold(object);
	if (length == vp->v_filesize) {
		vm_object_drop(object);
		return;
	}

	/*
	 * Calculate the size of the VM object, coverage includes
	 * the buffer straddling EOF.  If EOF is buffer-aligned
	 * we don't bother.
	 *
	 * Buffers do not have to be page-aligned.  Make sure
	 * nobjsize is beyond the last page of the buffer.
	 */
	if (boff < 0)
		boff = (int)(length % blksize);
	truncboffset = length - boff;
	oobjsize = object->size;
	if (boff)
		nobjsize = OFF_TO_IDX(truncboffset + blksize + PAGE_MASK);
	else
		nobjsize = OFF_TO_IDX(truncboffset + PAGE_MASK);
	object->size = nobjsize;

	if (length < vp->v_filesize) {
		/*
		 * File has shrunk, toss any cached pages beyond
		 * the end of the buffer (blksize aligned) for the
		 * new EOF.
		 */
		vp->v_filesize = length;
		if (nobjsize < oobjsize) {
			vm_object_page_remove(object, nobjsize, oobjsize,
					      FALSE);
		}

		/*
		 * Unmap any pages (page aligned) beyond the new EOF.
		 * The pages remain part of the (last) buffer and are not
		 * invalidated.
		 */
		pi = OFF_TO_IDX(length + PAGE_MASK);
		while (pi < nobjsize) {
			m = vm_page_lookup_busy_wait(object, pi, FALSE, "vmpg");
			if (m) {
				vm_page_protect(m, VM_PROT_NONE);
				vm_page_wakeup(m);
			}
			++pi;
			lwkt_yield();
		}
	} else {
		/*
		 * File has expanded.
		 */
		vp->v_filesize = length;
	}
	vm_object_drop(object);
}
