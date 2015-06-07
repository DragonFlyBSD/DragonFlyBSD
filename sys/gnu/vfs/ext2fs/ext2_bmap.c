/*
 * Copyright (c) 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * @(#)ufs_bmap.c	8.7 (Berkeley) 3/21/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_bmap.c,v 1.34.2.1 2000/03/17 10:12:14 ps Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/resourcevar.h>
#include <sys/conf.h>

#include "quota.h"
#include "dinode.h"
#include "inode.h"
#include "ext2_fs_sb.h"
#include "ext2_mount.h"
#include "ext2_extern.h"
#include "fs.h"

static int ext2_bmaparray(struct vnode *vp, ext2_daddr_t bn,
			  ext2_daddr_t *bnp, struct indir *ap, int *nump,
			  int *runp, int *runb);

/*
 * Bmap converts the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 *
 * BMAP must return the contiguous before and after run in bytes, inclusive
 * of the returned block.
 *
 * ext2_bmap(struct vnode *a_vp, off_t a_loffset,
 *	    off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
int
ext2_bmap(struct vop_bmap_args *ap)
{
	struct ext2_sb_info *fs;
	ext2_daddr_t lbn;
	ext2_daddr_t dbn;
	int error;

	/*
	 * Check for underlying vnode requests and ensure that logical
	 * to physical mapping is requested.
	 */
	if (ap->a_doffsetp == NULL)
		return (0);

	fs = VTOI(ap->a_vp)->i_e2fs;
	KKASSERT(((int)ap->a_loffset & ((1 << fs->s_bshift) - 1)) == 0);
	lbn = ap->a_loffset >> fs->s_bshift;

	error = ext2_bmaparray(ap->a_vp, lbn, &dbn, NULL, NULL,
			      ap->a_runp, ap->a_runb);

	if (error || dbn == (ext2_daddr_t)-1) {
		*ap->a_doffsetp = NOOFFSET;
	} else {
		*ap->a_doffsetp = dbtodoff(fs, dbn);
		if (ap->a_runp)
			*ap->a_runp = (*ap->a_runp + 1) << fs->s_bshift;
		if (ap->a_runb)
			*ap->a_runb = *ap->a_runb << fs->s_bshift;
	}
	return (error);
}

/*
 * Indirect blocks are now on the vnode for the file.  They are given negative
 * logical block numbers.  Indirect blocks are addressed by the negative
 * address of the first data block to which they point.  Double indirect blocks
 * are addressed by one less than the address of the first indirect block to
 * which they point.  Triple indirect blocks are addressed by one less than
 * the address of the first double indirect block to which they point.
 *
 * ext2_bmaparray does the bmap conversion, and if requested returns the
 * array of logical blocks which must be traversed to get to a block.
 * Each entry contains the offset into that block that gets you to the
 * next block and the disk address of the block (if it is assigned).
 */
static
int
ext2_bmaparray(struct vnode *vp, ext2_daddr_t bn, ext2_daddr_t *bnp,
	      struct indir *ap, int *nump, int *runp, int *runb)
{
	struct inode *ip;
	struct buf *bp;
	struct ext2mount *ump;
	struct mount *mp;
	struct ext2_sb_info *fs;
	struct indir a[NIADDR+1], *xap;
	ext2_daddr_t daddr;
	long metalbn;
	int error, maxrun, num;

	ip = VTOI(vp);
	mp = vp->v_mount;
	ump = VFSTOEXT2(mp);
	fs = ip->i_e2fs;
#ifdef DIAGNOSTIC
	if ((ap != NULL && nump == NULL) || (ap == NULL && nump != NULL))
		panic("ext2_bmaparray: invalid arguments");
#endif

	if (runp) {
		*runp = 0;
	}

	if (runb) {
		*runb = 0;
	}

	maxrun = mp->mnt_iosize_max / mp->mnt_stat.f_iosize - 1;

	xap = ap == NULL ? a : ap;
	if (!nump)
		nump = &num;
	error = ext2_getlbns(vp, bn, xap, nump);
	if (error)
		return (error);

	num = *nump;
	if (num == 0) {
		*bnp = blkptrtodb(ump, ip->i_db[bn]);
		if (*bnp == 0)
			*bnp = -1;
		else if (runp) {
			daddr_t bnb = bn;
			for (++bn; bn < NDADDR && *runp < maxrun &&
			    is_sequential(ump, ip->i_db[bn - 1], ip->i_db[bn]);
			    ++bn, ++*runp);
			bn = bnb;
			if (runb && (bn > 0)) {
				for (--bn; (bn >= 0) && (*runb < maxrun) &&
					is_sequential(ump, ip->i_db[bn],
						ip->i_db[bn+1]);
						--bn, ++*runb);
			}
		}
		return (0);
	}


	/* Get disk address out of indirect block array */
	daddr = ip->i_ib[xap->in_off];

	for (bp = NULL, ++xap; --num; ++xap) {
		/*
		 * Exit the loop if there is no disk address assigned yet and
		 * the indirect block isn't in the cache, or if we were
		 * looking for an indirect block and we've found it.
		 */

		metalbn = xap->in_lbn;
		if ((daddr == 0 &&
		     !findblk(vp, dbtodoff(fs, metalbn), FINDBLK_TEST)) ||
		    metalbn == bn) {
			break;
		}
		/*
		 * If we get here, we've either got the block in the cache
		 * or we have a disk address for it, go fetch it.
		 */
		if (bp)
			bqrelse(bp);

		xap->in_exists = 1;
		bp = getblk(vp, lblktodoff(fs, metalbn),
			    mp->mnt_stat.f_iosize, 0, 0);
		if ((bp->b_flags & B_CACHE) == 0) {
#ifdef DIAGNOSTIC
			if (!daddr)
				panic("ext2_bmaparray: indirect block not in cache");
#endif
			/*
			 * This runs through ext2_strategy using bio2 to
			 * cache the disk offset, then comes back through
			 * bio1.  So we want to wait on bio1
			 */
			bp->b_bio1.bio_done = biodone_sync;
			bp->b_bio1.bio_flags |= BIO_SYNC;
			bp->b_bio2.bio_offset = fsbtodoff(fs, daddr);
			bp->b_flags &= ~(B_INVAL|B_ERROR);
			bp->b_cmd = BUF_CMD_READ;
			vfs_busy_pages(bp->b_vp, bp);
			vn_strategy(bp->b_vp, &bp->b_bio1);
			error = biowait(&bp->b_bio1, "biord");
			if (error) {
				brelse(bp);
				return (error);
			}
		}

		daddr = ((ext2_daddr_t *)bp->b_data)[xap->in_off];
		if (num == 1 && daddr && runp) {
			for (bn = xap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump,
			    ((ext2_daddr_t *)bp->b_data)[bn - 1],
			    ((ext2_daddr_t *)bp->b_data)[bn]);
			    ++bn, ++*runp);
			bn = xap->in_off;
			if (runb && bn) {
				for(--bn; bn >= 0 && *runb < maxrun &&
					is_sequential(ump, ((daddr_t *)bp->b_data)[bn],
					    ((daddr_t *)bp->b_data)[bn+1]);
					--bn, ++*runb);
			}
		}
	}
	if (bp)
		bqrelse(bp);

	daddr = blkptrtodb(ump, daddr);
	*bnp = daddr == 0 ? -1 : daddr;
	return (0);
}

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ib and
 * once with the offset into the page itself.
 */
int
ext2_getlbns(struct vnode *vp, ext2_daddr_t bn, struct indir *ap, int *nump)
{
	long blockcnt, metalbn, realbn;
	struct ext2mount *ump;
	int i, numlevels, off;
	int64_t qblockcnt;

	ump = VFSTOEXT2(vp->v_mount);
	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if ((long)bn < 0)
		bn = -(long)bn;

	/* The first NDADDR blocks are direct blocks. */
	if (bn < NDADDR)
		return (0);

	/*
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the previous level of indirection, and NIADDR - i is the number
	 * of levels of indirection needed to locate the requested block.
	 */
	for (blockcnt = 1, i = NIADDR, bn -= NDADDR;; i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);
		/*
		 * Use int64_t's here to avoid overflow for triple indirect
		 * blocks when longs have 32 bits and the block size is more
		 * than 4K.
		 */
		qblockcnt = (int64_t)blockcnt * MNINDIR(ump);
		if (bn < qblockcnt)
			break;
		blockcnt = qblockcnt;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + NIADDR - i);
	else
		metalbn = -(-realbn - bn + NIADDR - i);

	/*
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = NIADDR - i;
	ap->in_exists = 0;
	ap++;
	for (++numlevels; i <= NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		off = (bn / blockcnt) % MNINDIR(ump);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		ap->in_exists = 0;
		++ap;

		metalbn -= -1 + off * blockcnt;
		blockcnt /= MNINDIR(ump);
	}
	if (nump)
		*nump = numlevels;
	return (0);
}
