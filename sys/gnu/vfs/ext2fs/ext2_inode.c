/*
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
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
 *	@(#)ext2_inode.c	8.5 (Berkeley) 12/30/93
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_inode.c,v 1.24.2.1 2000/08/03 00:52:57 peter Exp $
 */

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#include "quota.h"
#include "inode.h"
#include "ext2_mount.h"

#include "ext2_fs.h"
#include "ext2_fs_sb.h"
#include "fs.h"
#include "ext2_extern.h"

static int ext2_indirtrunc (struct inode *, daddr_t, off_t, daddr_t,
			    int, long *);

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
ext2_update(struct vnode *vp, int waitfor)
{
	struct ext2_sb_info *fs;
	struct buf *bp;
	struct inode *ip;
	int error;

	ext2_itimes(vp);
	ip = VTOI(vp);
	if ((ip->i_flag & IN_MODIFIED) == 0)
		return (0);
	ip->i_flag &= ~(IN_LAZYMOD | IN_MODIFIED);
	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return (0);
	fs = ip->i_e2fs;
	error = bread(ip->i_devvp,
		      fsbtodoff(fs, ino_to_fsba(fs, ip->i_number)),
		      (int)fs->s_blocksize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	ext2_di2ei( &ip->i_din, (struct ext2_inode *) ((char *)bp->b_data + EXT2_INODE_SIZE(fs) *
	    ino_to_fsbo(fs, ip->i_number)));
/*
	if (waitfor && (vp->v_mount->mnt_flag & MNT_ASYNC) == 0)
		return (bwrite(bp));
	else {
*/
		bdwrite(bp);
		return (0);
/*
	}
*/
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */
/*
 * Truncate the inode oip to at most length size, freeing the
 * disk blocks.
 */
int
ext2_truncate(struct vnode *vp, off_t length, int flags, struct ucred *cred)
{
	struct vnode *ovp = vp;
	daddr_t lastblock;
	struct inode *oip;
	daddr_t bn, lbn, lastiblock[NIADDR], indir_lbn[NIADDR];
	daddr_t oldblks[NDADDR + NIADDR], newblks[NDADDR + NIADDR];
	struct ext2_sb_info *fs;
	struct buf *bp;
	int offset, size, level;
	long count, nblocks, blocksreleased = 0;
	int i;
	int aflags, error, allerror;
	off_t osize;
/*
kprintf("ext2_truncate called %d to %d\n", VTOI(ovp)->i_number, length);
*/	/*
	 * negative file sizes will totally break the code below and
	 * are not meaningful anyways.
	 */
	if (length < 0)
	    return EFBIG;

	oip = VTOI(ovp);
	if (ovp->v_type == VLNK &&
	    oip->i_size < ovp->v_mount->mnt_maxsymlinklen) {
#if DIAGNOSTIC
		if (length != 0)
			panic("ext2_truncate: partial truncate of symlink");
#endif
		bzero((char *)&oip->i_shortlink, (u_int)oip->i_size);
		oip->i_size = 0;
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (EXT2_UPDATE(ovp, 1));
	}
	if (oip->i_size == length) {
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (EXT2_UPDATE(ovp, 0));
	}
#if QUOTA
	if ((error = ext2_getinoquota(oip)) != 0)
		return (error);
#endif
	fs = oip->i_e2fs;
	osize = oip->i_size;
	ext2_discard_prealloc(oip);
	/*
	 * Lengthen the size of the file. We must ensure that the
	 * last byte of the file is allocated. Since the smallest
	 * value of osize is 0, length will be at least 1.
	 */
	if (osize < length) {
		offset = blkoff(fs, length - 1);
		lbn = lblkno(fs, length - 1);
		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		vnode_pager_setsize(ovp, length);
		error = ext2_balloc(oip, lbn, offset + 1, cred, &bp, aflags);
		if (error) {
			vnode_pager_setsize(ovp, osize);
			return (error);
		}
		oip->i_size = length;
		if (aflags & IO_SYNC)
			bwrite(bp);
		else
			bawrite(bp);
		oip->i_flag |= IN_CHANGE | IN_UPDATE;
		return (EXT2_UPDATE(ovp, 1));
	}
	/*
	 * Shorten the size of the file. If the file is not being
	 * truncated to a block boundry, the contents of the
	 * partial block following the end of the file must be
	 * zero'ed in case it ever become accessable again because
	 * of subsequent file growth.
	 */
	/* I don't understand the comment above */
	offset = blkoff(fs, length);
	if (offset == 0) {
		oip->i_size = length;
	} else {
		lbn = lblkno(fs, length);
		aflags = B_CLRBUF;
		if (flags & IO_SYNC)
			aflags |= B_SYNC;
		error = ext2_balloc(oip, lbn, offset, cred, &bp, aflags);
		if (error)
			return (error);
		oip->i_size = length;
		size = blksize(fs, oip, lbn);
		bzero((char *)bp->b_data + offset, (u_int)(size - offset));
		allocbuf(bp, size);
		if (aflags & IO_SYNC)
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
	lastblock = lblkno(fs, length + fs->s_blocksize - 1) - 1;
	lastiblock[SINGLE] = lastblock - NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->s_blocksize);
	/*
	 * Update file and block pointers on disk before we start freeing
	 * blocks.  If we crash before free'ing blocks below, the blocks
	 * will be returned to the free list.  lastiblock values are also
	 * normalized to -1 for calls to ext2_indirtrunc below.
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
	allerror = EXT2_UPDATE(ovp, 1);

	/*
	 * Having written the new inode to disk, save its new configuration
	 * and put back the old block pointers long enough to process them.
	 * Note that we save the new block configuration so we can check it
	 * when we are done.
	 */
	bcopy((caddr_t)&oip->i_db[0], (caddr_t)newblks, sizeof newblks);
	bcopy((caddr_t)oldblks, (caddr_t)&oip->i_db[0], sizeof oldblks);
	oip->i_size = osize;
	error = vtruncbuf(ovp, length, (int)fs->s_blocksize);
	if (error && (allerror == 0))
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
			error = ext2_indirtrunc(oip, indir_lbn[level],
			    fsbtodoff(fs, bn), lastiblock[level], level, &count);
			if (error)
				allerror = error;
			blocksreleased += count;
			if (lastiblock[level] < 0) {
				oip->i_ib[level] = 0;
				ext2_blkfree(oip, bn, fs->s_frag_size);
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
		ext2_blkfree(oip, bn, bsize);
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
			panic("itrunc: newspace");
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			ext2_blkfree(oip, bn, oldspace - newspace);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
#if DIAGNOSTIC
	for (level = SINGLE; level <= TRIPLE; level++)
		if (newblks[NDADDR + level] != oip->i_ib[level])
			panic("itrunc1");
	for (i = 0; i < NDADDR; i++)
		if (newblks[i] != oip->i_db[i])
			panic("itrunc2");
	if (length == 0 && (!RB_EMPTY(&ovp->v_rbdirty_tree) ||
			    !RB_EMPTY(&ovp->v_rbclean_tree)))
		panic("itrunc3");
#endif /* DIAGNOSTIC */
	/*
	 * Put back the real size.
	 */
	oip->i_size = length;
	oip->i_blocks -= blocksreleased;
	if (oip->i_blocks < 0)			/* sanity */
		oip->i_blocks = 0;
	oip->i_flag |= IN_CHANGE;
	vnode_pager_setsize(ovp, length);
#if QUOTA
	ext2_chkdq(oip, -blocksreleased, NOCRED, 0);
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
ext2_indirtrunc(struct inode *ip, daddr_t lbn, off_t doffset, daddr_t lastbn,
		int level, long *countp)
{
	int i;
	struct buf *bp;
	struct ext2_sb_info *fs = ip->i_e2fs;
	daddr_t *bap;
	struct vnode *vp;
	daddr_t *copy, nb, nlbn, last;
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
	nblocks = btodb(fs->s_blocksize);
	/*
	 * Get buffer of block pointers, zero those entries corresponding
	 * to blocks to be free'd, and update on disk copy first.  Since
	 * double(triple) indirect before single(double) indirect, calls
	 * to bmap on these blocks will fail.  However, we already have
	 * the on disk address, so we have to set the bio_offset field
	 * explicitly instead of letting bread do everything for us.
	 */
	vp = ITOV(ip);
	bp = getblk(vp, lblktodoff(fs, lbn), (int)fs->s_blocksize, 0, 0);
	if ((bp->b_flags & B_CACHE) == 0) {
		bp->b_flags &= ~(B_ERROR | B_INVAL);
		bp->b_cmd = BUF_CMD_READ;
		if (bp->b_bcount > bp->b_bufsize)
			panic("ext2_indirtrunc: bad buffer size");
		bp->b_bio2.bio_offset = doffset;
		bp->b_bio1.bio_done = biodone_sync;
		bp->b_bio1.bio_flags |= BIO_SYNC;
		vfs_busy_pages(bp->b_vp, bp);
		vn_strategy(vp, &bp->b_bio1);
		error = biowait(&bp->b_bio1, "biord");
	}
	if (error) {
		brelse(bp);
		*countp = 0;
		return (error);
	}

	bap = (daddr_t *)bp->b_data;
	copy = kmalloc(fs->s_blocksize, M_TEMP, M_WAITOK);
	bcopy((caddr_t)bap, (caddr_t)copy, (u_int)fs->s_blocksize);
	bzero((caddr_t)&bap[last + 1],
	  (u_int)(NINDIR(fs) - (last + 1)) * sizeof (daddr_t));
	if (last == -1)
		bp->b_flags |= B_INVAL;
	error = bwrite(bp);
	if (error)
		allerror = error;
	bap = copy;

	/*
	 * Recursively free totally unused blocks.
	 */
	for (i = NINDIR(fs) - 1, nlbn = lbn + 1 - i * factor; i > last;
	    i--, nlbn += factor) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			if ((error = ext2_indirtrunc(ip, nlbn,
			    fsbtodoff(fs, nb), (daddr_t)-1, level - 1, &blkcount)) != 0)
				allerror = error;
			blocksreleased += blkcount;
		}
		ext2_blkfree(ip, nb, fs->s_blocksize);
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0) {
			error = ext2_indirtrunc(ip, nlbn, fsbtodoff(fs, nb),
						last, level - 1, &blkcount);
			if (error)
				allerror = error;
			blocksreleased += blkcount;
		}
	}
	kfree(copy, M_TEMP);
	*countp = blocksreleased;
	return (allerror);
}

/*
 * Last reference to an inode.  If necessary, write or delete it.
 *
 * ext2_inactive(struct vnode *a_vp)
 */
int
ext2_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int mode, error = 0;

	ext2_discard_prealloc(ip);
	if (prtactive && VREFCNT(vp) > 1)
		vprint("ext2_inactive: pushing active", vp);

	/*
	 * Ignore inodes related to stale file handles.
	 */
	if (ip == NULL || ip->i_mode == 0)
		goto out;
	if (ip->i_nlink <= 0 && (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
#ifdef QUOTA
		if (!ext2_getinoquota(ip))
			(void)ext2_chkiq(ip, -1, NOCRED, FORCE);
#endif
		error = EXT2_TRUNCATE(vp, (off_t)0, 0, NOCRED);
		ip->i_rdev = 0;
		mode = ip->i_mode;
		ip->i_mode = 0;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		EXT2_VFREE(vp, ip->i_number, mode);
	}
	if (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE))
		EXT2_UPDATE(vp, 0);
out:
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip == NULL || ip->i_mode == 0)
		vrecycle(vp);
	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 *
 * ext2_reclaim(struct vnode *a_vp)
 */
int
ext2_reclaim(struct vop_reclaim_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;
#ifdef QUOTA
	int i;
#endif

	if (prtactive && VREFCNT(vp) > 1)
		vprint("ext2_reclaim: pushing active", vp);
	ip = VTOI(vp);

	/*
	 * Lazy updates.
	 */
	if (ip) {
		if (ip->i_flag & IN_LAZYMOD) {
			ip->i_flag |= IN_MODIFIED;
			EXT2_UPDATE(vp, 0);
		}
	}
#ifdef INVARIANTS
	if (ip && (ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE))) {
		kprintf("WARNING: INODE %ld flags %08x: modified inode being released!\n", (long)ip->i_number, (int)ip->i_flag);
		ip->i_flag |= IN_MODIFIED;
		EXT2_UPDATE(vp, 0);
	}
#endif
	/*
	 * Remove the inode from its hash chain and purge namecache
	 * data associated with the vnode.
	 */
	vp->v_data = NULL;
	if (ip) {
		ext2_ihashrem(ip);
		if (ip->i_devvp) {
			vrele(ip->i_devvp);
			ip->i_devvp = 0;
		}
#ifdef QUOTA
		for (i = 0; i < MAXQUOTAS; i++) {
			if (ip->i_dquot[i] != NODQUOT) {
				ext2_dqrele(vp, ip->i_dquot[i]);
				ip->i_dquot[i] = NODQUOT;
			}
		}
#endif
#ifdef UFS_DIRHASH
		if (ip->i_dirhash != NULL)
			ext2dirhash_free(ip);
#endif
		kfree(ip, VFSTOEXT2(vp->v_mount)->um_malloctype);
	}
	return (0);
}
