/*
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
 *	@(#)ffs_inode.c	8.13 (Berkeley) 4/21/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_inode.c,v 1.56.2.5 2002/02/05 18:35:03 dillon Exp $
 */

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include "quota.h"
#include "ufsmount.h"
#include "inode.h"
#include "ufs_extern.h"

#include "fs.h"
#include "ffs_extern.h"

#include <vm/vm_page2.h>
#include <sys/buf2.h>

static int ffs_indirtrunc (struct inode *, ufs_daddr_t, ufs_daddr_t,
	    ufs_daddr_t, int, long *);

/*
 * Update the access, modified, and inode change times as specified by the
 * IN_ACCESS, IN_UPDATE, and IN_CHANGE flags respectively.  Write the inode
 * to disk if the IN_MODIFIED flag is set (it may be set initially, or by
 * the timestamp update).  The IN_LAZYMOD flag is set to force a write
 * later if not now.  If we write now, then clear both IN_MODIFIED and
 * IN_LAZYMOD to reflect the presumably successful write, and if waitfor is
 * set, then wait for the write to complete.
 */
int
ffs_update(struct vnode *vp, int waitfor)
{
	struct fs *fs;
	struct buf *bp;
	struct inode *ip;
	int error;

	ufs_itimes(vp);
	ip = VTOI(vp);
	if ((ip->i_flag & IN_MODIFIED) == 0 && waitfor == 0)
		return (0);
	ip->i_flag &= ~(IN_LAZYMOD | IN_MODIFIED);
	fs = ip->i_fs;
	if (fs->fs_ronly)
		return (0);

	/*
	 * The vnode type is usually set to VBAD if an unrecoverable I/O
	 * error has occured (such as when reading the inode).  Clear the
	 * modified bits but do not write anything out in this case.
	 */
	if (vp->v_type == VBAD)
		return (0);
	/*
	 * Ensure that uid and gid are correct. This is a temporary
	 * fix until fsck has been changed to do the update.
	 */
	if (fs->fs_inodefmt < FS_44INODEFMT) {		/* XXX */
		ip->i_din.di_ouid = ip->i_uid;		/* XXX */
		ip->i_din.di_ogid = ip->i_gid;		/* XXX */
	}						/* XXX */
	error = bread(ip->i_devvp, 
		      fsbtodoff(fs, ino_to_fsba(fs, ip->i_number)),
		      (int)fs->fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	if (DOINGSOFTDEP(vp))
		softdep_update_inodeblock(ip, bp, waitfor);
	else if (ip->i_effnlink != ip->i_nlink)
		panic("ffs_update: bad link cnt");
	*((struct ufs1_dinode *)bp->b_data +
	    ino_to_fsbo(fs, ip->i_number)) = ip->i_din;
	if (waitfor && !DOINGASYNC(vp)) {
		return (bwrite(bp));
	} else if (vm_page_count_severe() || buf_dirty_count_severe()) {
		return (bwrite(bp));
	} else {
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		bdwrite(bp);
		return (0);
	}
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */
/*
 * Truncate the inode oip to at most length size, freeing the
 * disk blocks.
 */
int
ffs_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred)
{
	struct vnode *ovp = vp;
	ufs_daddr_t lastblock;
	struct inode *oip;
	ufs_daddr_t bn, lbn, lastiblock[NIADDR], indir_lbn[NIADDR];
	ufs_daddr_t oldblks[NDADDR + NIADDR], newblks[NDADDR + NIADDR];
	struct fs *fs;
	struct buf *bp;
	int offset, size, level;
	long count, nblocks, blocksreleased = 0;
	int i;
	int aflags, error, allerror;
	off_t osize;

	oip = VTOI(ovp);
	fs = oip->i_fs;
	if (length < 0)
		return (EINVAL);
	if (length > fs->fs_maxfilesize)
		return (EFBIG);
	if (ovp->v_type == VLNK &&
	    (oip->i_size < ovp->v_mount->mnt_maxsymlinklen || oip->i_din.di_blocks == 0)) {
#ifdef DIAGNOSTIC
		if (length != 0)
			panic("ffs_truncate: partial truncate of symlink");
#endif /* DIAGNOSTIC */
		bzero((char *)&oip->i_shortlink, (uint)oip->i_size);
		oip->i_size = 0;
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ffs_update(ovp, 1));
	}
	if (oip->i_size == length) {
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ffs_update(ovp, 0));
	}
	if (fs->fs_ronly)
		panic("ffs_truncate: read-only filesystem");
#ifdef QUOTA
	error = ufs_getinoquota(oip);
	if (error)
		return (error);
#endif
	if (DOINGSOFTDEP(ovp)) {
		if (length > 0 || softdep_slowdown(ovp)) {
			/*
			 * If a file is only partially truncated, then
			 * we have to clean up the data structures
			 * describing the allocation past the truncation
			 * point. Finding and deallocating those structures
			 * is a lot of work. Since partial truncation occurs
			 * rarely, we solve the problem by syncing the file
			 * so that it will have no data structures left.
			 */
			if ((error = VOP_FSYNC(ovp, MNT_WAIT, 0)) != 0)
				return (error);
		} else {
#ifdef QUOTA
			(void) ufs_chkdq(oip, -oip->i_blocks, NOCRED, 0);
#endif
			softdep_setup_freeblocks(oip, length);
			vinvalbuf(ovp, 0, 0, 0);
			nvnode_pager_setsize(ovp, 0, fs->fs_bsize, 0);
			oip->i_flag |= IN_CHANGE | IN_UPDATE;
			return (ffs_update(ovp, 0));
		}
	}
	osize = oip->i_size;

	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 *
	 * nvextendbuf() only breads the old buffer.  The blocksize
	 * of the new buffer must be specified so it knows how large
	 * to make the VM object.
	 */
	if (osize < length) {
		nvextendbuf(vp, osize, length,
			    blkoffsize(fs, oip, osize),	/* oblksize */
			    blkoffresize(fs, length),	/* nblksize */
			    blkoff(fs, osize),
			    blkoff(fs, length),
			    0);

		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		/* BALLOC will reallocate the fragment at the old EOF */
		error = VOP_BALLOC(ovp, length - 1, 1, cred, aflags, &bp);
		if (error)
			return (error);
		oip->i_size = length;
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (aflags & B_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (ffs_update(ovp, 1));
	}

	/*
	 * Shorten the size of the file.
	 *
	 * NOTE: The block size specified in nvtruncbuf() is the blocksize
	 *	 of the buffer containing length prior to any reallocation
	 *	 of the block.
	 */
	allerror = nvtruncbuf(ovp, length, blkoffsize(fs, oip, length),
			      blkoff(fs, length), 0);
	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;
	} else {
		lbn = lblkno(fs, length);
		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		error = VOP_BALLOC(ovp, length - 1, 1, cred, aflags, &bp);
		if (error)
			return (error);

		/*
		 * When we are doing soft updates and the UFS_BALLOC
		 * above fills in a direct block hole with a full sized
		 * block that will be truncated down to a fragment below,
		 * we must flush out the block dependency with an FSYNC
		 * so that we do not get a soft updates inconsistency
		 * when we create the fragment below.
		 *
		 * nvtruncbuf() may have re-dirtied the underlying block
		 * as part of its truncation zeroing code.  To avoid a
		 * 'locking against myself' panic in the second fsync we
		 * can simply undirty the bp since the redirtying was
		 * related to areas of the buffer that we are going to
		 * throw away anyway, and we will b*write() the remainder
		 * anyway down below.
		 */
		if (DOINGSOFTDEP(ovp) && lbn < NDADDR &&
		    fragroundup(fs, blkoff(fs, length)) < fs->fs_bsize) {
			bundirty(bp);
			error = VOP_FSYNC(ovp, MNT_WAIT, 0);
			if (error) {
				bdwrite(bp);
				return (error);
			}
		}
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
#if 0
		/* remove - nvtruncbuf deals with this */
		if (ovp->v_type != VDIR)
			bzero((char *)bp->b_data + offset,
			    (uint)(size - offset));
#endif
		/* Kirk's code has reallocbuf(bp, size, 1) here */
		allocbuf(bp, size);
		if (bp->b_bufsize == fs->fs_bsize)
			bp->b_flags |= B_CLUSTEROK;
		if (aflags & B_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
	}
	/*
	 * Calculate index into inode's block list of
	 * last direct and indirect blocks (if any)
	 * which we want to keep.  Lastblock is -1 when
	 * the file is truncated to 0.
	 */
	lastblock = lblkno(fs, length + fs->fs_bsize - 1) - 1;
	lastiblock[SINGLE] = lastblock - NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->fs_bsize);

	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ffs_indirtrunc below.
	 */
	bcopy((caddr_t)&oip->i_db[0], (caddr_t)oldblks, sizeof oldblks);
	for (level = TRIPLE; level >= SINGLE; level--)
		if (lastiblock[level] < 0) {
			oip->i_ib[level] = 0;
			lastiblock[level] = -1;
		}
	for (i = NDADDR - 1; i > lastblock; i--)
		oip->i_db[i] = 0;
	oip->i_flag |= IN_CHANGE | IN_UPDATE;
	error = ffs_update(ovp, 1);
	if (error && allerror == 0)
		allerror = error;
	
	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	bcopy((caddr_t)&oip->i_db[0], (caddr_t)newblks, sizeof newblks);
	bcopy((caddr_t)oldblks, (caddr_t)&oip->i_db[0], sizeof oldblks);
	oip->i_size = osize;

	if (error && allerror == 0)
		allerror = error;

	/*
	 * Indirect blocks first.
	 */
	indir_lbn[SINGLE] = -NDADDR;
	indir_lbn[DOUBLE] = indir_lbn[SINGLE] - NINDIR(fs) - 1;
	indir_lbn[TRIPLE] = indir_lbn[DOUBLE] - NINDIR(fs) * NINDIR(fs) - 1;
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = oip->i_ib[level];
		if (bn != 0) {
			error = ffs_indirtrunc(oip, indir_lbn[level],
			    fsbtodb(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				oip->i_ib[level] = 0;
				ffs_blkfree(oip, bn, fs->fs_bsize);
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks or frags.
	 */
	for (i = NDADDR - 1; i > lastblock; i--) {
		long bsize;

		bn = oip->i_db[i];
		if (bn == 0)
			continue;
		oip->i_db[i] = 0;
		bsize = blksize(fs, oip, i);
		ffs_blkfree(oip, bn, bsize);
		blocksreleased += btodb(bsize);
	}
	if (lastblock < 0)
		goto done;

	/*
	 * Finally, look for a change in size of the
	 * last direct block; release any frags.
	 */
	bn = oip->i_db[lastblock];
	if (bn != 0) {
		long oldspace, newspace;

		/*
		 * Calculate amount of space we're giving
		 * back as old block size minus new block size.
		 */
		oldspace = blksize(fs, oip, lastblock);
		oip->i_size = length;
		newspace = blksize(fs, oip, lastblock);
		if (newspace == 0)
			panic("ffs_truncate: newspace");
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			ffs_blkfree(oip, bn, oldspace - newspace);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
#ifdef DIAGNOSTIC
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[NDADDR + level] != oip->i_ib[level])
			panic("ffs_truncate1");
	for (i = 0; i < NDADDR; i++)
		if (newblks[i] != oip->i_db[i])
			panic("ffs_truncate2");
	if (length == 0 && !RB_EMPTY(&ovp->v_rbdirty_tree))
		panic("ffs_truncate3");
#endif /* DIAGNOSTIC */
	/*
	 * Put back the real size.
	 */
	oip->i_size = length;
	oip->i_blocks -= blocksreleased;

	if (oip->i_blocks < 0)			/* sanity */
		oip->i_blocks = 0;
	oip->i_flag |= IN_CHANGE;
#ifdef QUOTA
	(void) ufs_chkdq(oip, -blocksreleased, NOCRED, 0);
#endif
	return (allerror);
}

/*
 * Release blocks associated with the inode ip and stored in the indirect
 * block bn.  Blocks are free'd in LIFO order up to (but not including)
 * lastbn.  If level is greater than SINGLE, the block is an indirect block
 * and recursive calls to indirtrunc must be used to cleanse other indirect
 * blocks.
 *
 * NB: triple indirect blocks are untested.
 */
static int
ffs_indirtrunc(struct inode *ip, ufs_daddr_t lbn, ufs_daddr_t dbn,
	       ufs_daddr_t lastbn, int level, long *countp)
{
	int i;
	struct buf *bp;
	struct fs *fs = ip->i_fs;
	ufs_daddr_t *bap;
	struct vnode *vp;
	ufs_daddr_t *copy = NULL, nb, nlbn, last;
	long blkcount, factor;
	int nblocks, blocksreleased = 0;
	int error = 0, allerror = 0;

	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 */
	factor = 1;
	for (i = SINGLE; i < level; i++)
		factor *= NINDIR(fs);
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btodb(fs->fs_bsize);
	/*
	 * Get buffer of block pointers, zero those entries corresponding
	 * to blocks to be free'd, and update on disk copy first.  Since
	 * double(triple) indirect before single(double) indirect, calls
	 * to bmap on these blocks will fail.  However, we already have
	 * the on disk address, so we have to set the bio_offset field
	 * explicitly instead of letting bread do everything for us.
	 */
	vp = ITOV(ip);
	bp = getblk(vp, lblktodoff(fs, lbn), (int)fs->fs_bsize, 0, 0);
	if ((bp->b_flags & B_CACHE) == 0) {
		bp->b_flags &= ~(B_ERROR|B_INVAL);
		bp->b_cmd = BUF_CMD_READ;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ffs_indirtrunc: bad buffer size");
		/*
		 * BIO is bio2 which chains back to bio1.  We wait
		 * on bio1.
		 */
		bp->b_bio2.bio_offset = dbtodoff(fs, dbn);
		bp->b_bio1.bio_done = biodone_sync;
		bp->b_bio1.bio_flags |= BIO_SYNC;
		vfs_busy_pages(vp, bp);
		/*
		 * Access the block device layer using the device vnode
		 * and the translated block number (bio2) instead of the
		 * file vnode (vp) and logical block number (bio1).
		 *
		 * Even though we are bypassing the vnode layer, we still
		 * want the vnode state to indicate that an I/O on its behalf
		 * is in progress.
		 */
		bio_start_transaction(&bp->b_bio1, &vp->v_track_read);
		vn_strategy(ip->i_devvp, &bp->b_bio2);
		error = biowait(&bp->b_bio1, "biord");
	}
	if (error) {
		brelse(bp);
		*countp = 0;
		return (error);
	}

	bap = (ufs_daddr_t *)bp->b_data;
	if (lastbn != -1) {
		copy = kmalloc(fs->fs_bsize, M_TEMP, M_WAITOK);
		bcopy((caddr_t)bap, (caddr_t)copy, (uint)fs->fs_bsize);
		bzero((caddr_t)&bap[last + 1],
		    (uint)(NINDIR(fs) - (last + 1)) * sizeof (ufs_daddr_t));
		if (DOINGASYNC(vp)) {
			bawrite(bp);
		} else {
			error = bwrite(bp);
			if (error)
				allerror = error;
		}
		bap = copy;
	}

	/*
	 * Recursively free totally unused blocks.
	 */
	for (i = NINDIR(fs) - 1, nlbn = lbn + 1 - i * factor; i > last;
	    i--, nlbn += factor) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			if ((error = ffs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    (ufs_daddr_t)-1, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
		ffs_blkfree(ip, nb, fs->fs_bsize);
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0) {
			error = ffs_indirtrunc(ip, nlbn, fsbtodb(fs, nb),
			    last, level - 1, &blkcount);
			if (error)
				allerror = error;
			blocksreleased += blkcount;
		}
	}
	if (copy != NULL) {
		kfree(copy, M_TEMP);
	} else {
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
	}
		
	*countp = blocksreleased;
	return (allerror);
}
