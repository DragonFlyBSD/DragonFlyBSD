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
 *	@(#)ffs_subr.c	8.5 (Berkeley) 3/21/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_subr.c,v 1.25 1999/12/29 04:55:04 peter Exp $
 */

#include <sys/param.h>

#ifndef _KERNEL
#include "dinode.h"
#include "fs.h"
extern void panic(const char *, ...) __printflike(1, 2) __dead2;
#else
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <sys/buf2.h>

#include "quota.h"
#include "inode.h"
#include "fs.h"
#include "ffs_extern.h"

/*
 * Return buffer with the contents of block "offset" from the beginning of
 * vnode "vp".  If "res" is non-zero, fill it in with a pointer to the
 * remaining space in the vnode.
 */
int
ffs_blkatoff(struct vnode *vp, off_t uoffset, char **res, struct buf **bpp)
{
	struct inode *ip;
	struct fs *fs;
	struct buf *bp;
	ufs_daddr_t lbn;
	int bsize, error;

	ip = VTOI(vp);
	fs = ip->i_fs;
	lbn = lblkno(fs, uoffset);
	bsize = blksize(fs, ip, lbn);

	*bpp = NULL;
	error = bread(vp, lblktodoff(fs, lbn), bsize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	if (res)
		*res = (char *)bp->b_data + blkoff(fs, uoffset);
	*bpp = bp;
	return (0);
}

/*
 * Return buffer with the contents of block "offset" from the beginning of
 * vnode "vp".  If "res" is non-zero, fill it in with a pointer to the
 * remaining space in the vnode.
 *
 * This version includes a read-ahead optimization.
 */
int
ffs_blkatoff_ra(struct vnode *vp, off_t uoffset, char **res, struct buf **bpp,
		int seqcount)
{
	struct inode *ip;
	struct fs *fs;
	struct buf *bp;
	ufs_daddr_t lbn;
	ufs_daddr_t nextlbn;
	off_t base_loffset;
	off_t next_loffset;
	int bsize, error;
	int nextbsize;

	ip = VTOI(vp);
	fs = ip->i_fs;
	lbn = lblkno(fs, uoffset);
	base_loffset = lblktodoff(fs, lbn);
	bsize = blksize(fs, ip, lbn);

	nextlbn = lbn + 1;
	next_loffset = lblktodoff(fs, nextlbn);


	*bpp = NULL;

	if (next_loffset >= ip->i_size) {
		/*
		 * Do not do readahead if this is the last block,
		 * bsize might represent a fragment.
		 */
		error = bread(vp, base_loffset, bsize, &bp);
	} else if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
		/*
		 * Try to cluster if we allowed to.
		 */
		error = cluster_read(vp, (off_t)ip->i_size,
				     base_loffset, bsize,
				     bsize, seqcount * BKVASIZE, &bp);
	} else if (seqcount > 1) {
		/*
		 * Faked read ahead
		 */
		nextbsize = blksize(fs, ip, nextlbn);
		error = breadn(vp, base_loffset, bsize,
			       &next_loffset, &nextbsize, 1, &bp);
	} else {
		/*
		 * Failing all of the above, just read what the
		 * user asked for. Interestingly, the same as
		 * the first option above.
		 */
		error = bread(vp, base_loffset, bsize, &bp);
	}
	if (error) {
		brelse(bp);
		return (error);
	}
	if (res)
		*res = (char *)bp->b_data + (int)(uoffset - base_loffset);
	*bpp = bp;
	return (0);
}

#endif

/*
 * Update the frsum fields to reflect addition or deletion
 * of some frags.
 */
void
ffs_fragacct(struct fs *fs, int fragmap, int32_t fraglist[], int cnt)
{
	int inblk;
	int field, subfield;
	int siz, pos;

	/*
	 * inblk represents a bitmap of fragment sizes which may be
	 * contained in the data 'fragmap'.  e.g. if a fragment of size
	 * 1 is available, bit 0 would be set.  inblk is shifted left
	 * by one so we do not have to calculate (1 << (siz - 1)).
	 *
	 * fragment represents the data pattern we are trying to decipher,
	 * we shift it left by one to align it with the 'around' and 'inside'
	 * masks.
	 *
	 * around represents the bits around the subfield and is a mask.
	 * inside represents what we must match within the mask, it is
	 * basically the mask with the first and last bit set to 0, allowing
	 * us to represent a whole fragment.
	 *
	 * When we find a match we bump our position by the size of the
	 * matching fragment, then bump the position again:
	 *
	 * 010101010 fragmap (shifted left by 1)
	 *       111 around mask
	 *       010 inside mask
	 *      111     (shifted by siz)
	 *	010
	 *     111	(shifted again)
	 *     010
	 */
	inblk = (int)(fragtbl[fs->fs_frag][fragmap]) << 1;
	fragmap <<= 1;
	for (siz = 1; siz < fs->fs_frag; siz++) {
		if ((inblk & (1 << (siz + (fs->fs_frag % NBBY)))) == 0)
			continue;
		field = around[siz];
		subfield = inside[siz];
		for (pos = siz; pos <= fs->fs_frag; pos++) {
			if ((fragmap & field) == subfield) {
				fraglist[siz] += cnt;
				pos += siz;
				field <<= siz;
				subfield <<= siz;
			}
			field <<= 1;
			subfield <<= 1;
		}
	}
}

/*
 * block operations
 *
 * check if a block is available
 */
int
ffs_isblock(struct fs *fs, unsigned char *cp, ufs_daddr_t h)
{
	unsigned char mask;

	switch ((int)fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
		panic("ffs_isblock");
	}
}

/*
 * check if a block is free
 */
int
ffs_isfreeblock(struct fs *fs, unsigned char *cp, ufs_daddr_t h)
{
	switch ((int)fs->fs_frag) {
	case 8:
		return (cp[h] == 0);
	case 4:
		return ((cp[h >> 1] & (0x0f << ((h & 0x1) << 2))) == 0);
	case 2:
		return ((cp[h >> 2] & (0x03 << ((h & 0x3) << 1))) == 0);
	case 1:
		return ((cp[h >> 3] & (0x01 << (h & 0x7))) == 0);
	default:
		panic("ffs_isfreeblock");
	}
}

/*
 * take a block out of the map
 */
void
ffs_clrblock(struct fs *fs, u_char *cp, ufs_daddr_t h)
{
	switch ((int)fs->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
		panic("ffs_clrblock");
	}
}

/*
 * put a block into the map
 */
void
ffs_setblock(struct fs *fs, unsigned char *cp, ufs_daddr_t h)
{
	switch ((int)fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
		panic("ffs_setblock");
	}
}
