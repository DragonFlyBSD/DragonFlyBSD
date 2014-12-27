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
 *	@(#)ffs_alloc.c	8.18 (Berkeley) 5/26/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_alloc.c,v 1.64.2.2 2001/09/21 19:15:21 dillon Exp $
 */

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <sys/taskqueue.h>
#include <machine/inttypes.h>

#include <sys/buf2.h>

#include "quota.h"
#include "inode.h"
#include "ufs_extern.h"
#include "ufsmount.h"

#include "fs.h"
#include "ffs_extern.h"

typedef ufs_daddr_t allocfcn_t (struct inode *ip, int cg, ufs_daddr_t bpref,
				  int size);

static ufs_daddr_t ffs_alloccg (struct inode *, int, ufs_daddr_t, int);
static ufs_daddr_t
	      ffs_alloccgblk (struct inode *, struct buf *, ufs_daddr_t);
static void ffs_blkfree_cg(struct fs *, struct vnode *, cdev_t , ino_t,
			   uint32_t , ufs_daddr_t, long );
#ifdef DIAGNOSTIC
static int	ffs_checkblk (struct inode *, ufs_daddr_t, long);
#endif
static void	ffs_clusteracct	(struct fs *, struct cg *, ufs_daddr_t,
				     int);
static ufs_daddr_t ffs_clusteralloc (struct inode *, int, ufs_daddr_t,
	    int);
static ino_t	ffs_dirpref (struct inode *);
static ufs_daddr_t ffs_fragextend (struct inode *, int, long, int, int);
static void	ffs_fserr (struct fs *, uint, char *);
static u_long	ffs_hashalloc
		    (struct inode *, int, long, int, allocfcn_t *);
static ino_t	ffs_nodealloccg (struct inode *, int, ufs_daddr_t, int);
static ufs_daddr_t ffs_mapsearch (struct fs *, struct cg *, ufs_daddr_t,
	    int);

/*
 * Allocate a block in the filesystem.
 *
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 * If no block preference is given the following heirarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *      inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 */
int
ffs_alloc(struct inode *ip, ufs_daddr_t lbn, ufs_daddr_t bpref, int size,
	  struct ucred *cred, ufs_daddr_t *bnp)
{
	struct fs *fs;
	ufs_daddr_t bno;
	int cg;
#ifdef QUOTA
	int error;
#endif

	*bnp = 0;
	fs = ip->i_fs;
#ifdef DIAGNOSTIC
	if ((uint)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		kprintf("dev = %s, bsize = %ld, size = %d, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, size,
		    fs->fs_fsmnt);
		panic("ffs_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_alloc: missing credential");
#endif /* DIAGNOSTIC */
	if (size == fs->fs_bsize && fs->fs_cstotal.cs_nbfree == 0)
		goto nospace;
	if (cred->cr_uid != 0 &&
	    freespace(fs, fs->fs_minfree) - numfrags(fs, size) < 0)
		goto nospace;
#ifdef QUOTA
	error = ufs_chkdq(ip, (long)btodb(size), cred, 0);
	if (error)
		return (error);
#endif
	if (bpref >= fs->fs_size)
		bpref = 0;
	if (bpref == 0)
		cg = ino_to_cg(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);
	bno = (ufs_daddr_t)ffs_hashalloc(ip, cg, (long)bpref, size,
					 ffs_alloccg);
	if (bno > 0) {
		ip->i_blocks += btodb(size);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
#ifdef QUOTA
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) ufs_chkdq(ip, (long)-btodb(size), cred, FORCE);
#endif
nospace:
	ffs_fserr(fs, cred->cr_uid, "filesystem full");
	uprintf("\n%s: write failed, filesystem is full\n", fs->fs_fsmnt);
	return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified. The allocator attempts to extend
 * the original block. Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
int
ffs_realloccg(struct inode *ip, ufs_daddr_t lbprev, ufs_daddr_t bpref,
	      int osize, int nsize, struct ucred *cred, struct buf **bpp)
{
	struct fs *fs;
	struct buf *bp;
	int cg, request, error;
	ufs_daddr_t bprev, bno;

	*bpp = NULL;
	fs = ip->i_fs;
#ifdef DIAGNOSTIC
	if ((uint)osize > fs->fs_bsize || fragoff(fs, osize) != 0 ||
	    (uint)nsize > fs->fs_bsize || fragoff(fs, nsize) != 0) {
		kprintf(
		"dev = %s, bsize = %ld, osize = %d, nsize = %d, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, osize,
		    nsize, fs->fs_fsmnt);
		panic("ffs_realloccg: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_realloccg: missing credential");
#endif /* DIAGNOSTIC */
	if (cred->cr_uid != 0 &&
	    freespace(fs, fs->fs_minfree) -  numfrags(fs, nsize - osize) < 0)
		goto nospace;
	if ((bprev = ip->i_db[lbprev]) == 0) {
		kprintf("dev = %s, bsize = %ld, bprev = %ld, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_bsize, (long)bprev,
		    fs->fs_fsmnt);
		panic("ffs_realloccg: bad bprev");
	}
	/*
	 * Allocate the extra space in the buffer.
	 */
	error = bread(ITOV(ip), lblktodoff(fs, lbprev), osize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	if(bp->b_bio2.bio_offset == NOOFFSET) {
		if( lbprev >= NDADDR)
			panic("ffs_realloccg: lbprev out of range");
		bp->b_bio2.bio_offset = fsbtodoff(fs, bprev);
	}

#ifdef QUOTA
	error = ufs_chkdq(ip, (long)btodb(nsize - osize), cred, 0);
	if (error) {
		brelse(bp);
		return (error);
	}
#endif
	/*
	 * Check for extension in the existing location.
	 */
	cg = dtog(fs, bprev);
	bno = ffs_fragextend(ip, cg, (long)bprev, osize, nsize);
	if (bno) {
		if (bp->b_bio2.bio_offset != fsbtodoff(fs, bno))
			panic("ffs_realloccg: bad blockno");
		ip->i_blocks += btodb(nsize - osize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bzero((char *)bp->b_data + osize, (uint)nsize - osize);
		*bpp = bp;
		return (0);
	}
	/*
	 * Allocate a new disk location.
	 */
	if (bpref >= fs->fs_size)
		bpref = 0;
	switch ((int)fs->fs_optim) {
	case FS_OPTSPACE:
		/*
		 * Allocate an exact sized fragment. Although this makes
		 * best use of space, we will waste time relocating it if
		 * the file continues to grow. If the fragmentation is
		 * less than half of the minimum free reserve, we choose
		 * to begin optimizing for time.
		 */
		request = nsize;
		if (fs->fs_minfree <= 5 ||
		    fs->fs_cstotal.cs_nffree >
		    (off_t)fs->fs_dsize * fs->fs_minfree / (2 * 100))
			break;
		log(LOG_NOTICE, "%s: optimization changed from SPACE to TIME\n",
			fs->fs_fsmnt);
		fs->fs_optim = FS_OPTTIME;
		break;
	case FS_OPTTIME:
		/*
		 * At this point we have discovered a file that is trying to
		 * grow a small fragment to a larger fragment. To save time,
		 * we allocate a full sized block, then free the unused portion.
		 * If the file continues to grow, the `ffs_fragextend' call
		 * above will be able to grow it in place without further
		 * copying. If aberrant programs cause disk fragmentation to
		 * grow within 2% of the free reserve, we choose to begin
		 * optimizing for space.
		 */
		request = fs->fs_bsize;
		if (fs->fs_cstotal.cs_nffree <
		    (off_t)fs->fs_dsize * (fs->fs_minfree - 2) / 100)
			break;
		log(LOG_NOTICE, "%s: optimization changed from TIME to SPACE\n",
			fs->fs_fsmnt);
		fs->fs_optim = FS_OPTSPACE;
		break;
	default:
		kprintf("dev = %s, optim = %ld, fs = %s\n",
		    devtoname(ip->i_dev), (long)fs->fs_optim, fs->fs_fsmnt);
		panic("ffs_realloccg: bad optim");
		/* NOTREACHED */
	}
	bno = (ufs_daddr_t)ffs_hashalloc(ip, cg, (long)bpref, request,
					 ffs_alloccg);
	if (bno > 0) {
		bp->b_bio2.bio_offset = fsbtodoff(fs, bno);
		if (!DOINGSOFTDEP(ITOV(ip)))
			ffs_blkfree(ip, bprev, (long)osize);
		if (nsize < request)
			ffs_blkfree(ip, bno + numfrags(fs, nsize),
			    (long)(request - nsize));
		ip->i_blocks += btodb(nsize - osize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		allocbuf(bp, nsize);
		bzero((char *)bp->b_data + osize, (uint)nsize - osize);
		*bpp = bp;
		return (0);
	}
#ifdef QUOTA
	/*
	 * Restore user's disk quota because allocation failed.
	 */
	(void) ufs_chkdq(ip, (long)-btodb(nsize - osize), cred, FORCE);
#endif
	brelse(bp);
nospace:
	/*
	 * no space available
	 */
	ffs_fserr(fs, cred->cr_uid, "filesystem full");
	uprintf("\n%s: write failed, filesystem is full\n", fs->fs_fsmnt);
	return (ENOSPC);
}

SYSCTL_NODE(_vfs, OID_AUTO, ffs, CTLFLAG_RW, 0, "FFS filesystem");

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous is given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible to
 * an fs_rotdelay offset from the end of the allocation for the logical
 * block immediately preceeding the current range. If successful, the
 * physical block numbers in the buffer pointers and in the inode are
 * changed to reflect the new allocation. If unsuccessful, the allocation
 * is left unchanged. The success in doing the reallocation is returned.
 * Note that the error return is not reflected back to the user. Rather
 * the previous block allocation will be used.
 */
static int doasyncfree = 1;
SYSCTL_INT(_vfs_ffs, FFS_ASYNCFREE, doasyncfree, CTLFLAG_RW, &doasyncfree, 0, "");

static int doreallocblks = 1;
SYSCTL_INT(_vfs_ffs, FFS_REALLOCBLKS, doreallocblks, CTLFLAG_RW, &doreallocblks, 0, "");

#ifdef DEBUG
static volatile int prtrealloc = 0;
#endif

/*
 * ffs_reallocblks(struct vnode *a_vp, struct cluster_save *a_buflist)
 */
int
ffs_reallocblks(struct vop_reallocblks_args *ap)
{
	struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	ufs_daddr_t *bap, *sbap, *ebap = NULL;
	struct cluster_save *buflist;
	ufs_daddr_t start_lbn, end_lbn, soff, newblk, blkno;
#ifdef DIAGNOSTIC
	off_t boffset;
#endif
	struct indir start_ap[NIADDR + 1], end_ap[NIADDR + 1], *idp;
	int i, len, slen, start_lvl, end_lvl, pref, ssize;

	if (doreallocblks == 0)
		return (ENOSPC);
	vp = ap->a_vp;
	ip = VTOI(vp);
	fs = ip->i_fs;
	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = lblkno(fs, buflist->bs_children[0]->b_loffset);
	end_lbn = start_lbn + len - 1;
#ifdef DIAGNOSTIC
	for (i = 0; i < len; i++)
		if (!ffs_checkblk(ip,
		   dofftofsb(fs, buflist->bs_children[i]->b_bio2.bio_offset), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 1");
	for (i = 1; i < len; i++) {
		if (buflist->bs_children[i]->b_loffset != lblktodoff(fs, start_lbn) + lblktodoff(fs, i))
			panic("ffs_reallocblks: non-logical cluster");
	}
	boffset = buflist->bs_children[0]->b_bio2.bio_offset;
	ssize = (int)fsbtodoff(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_bio2.bio_offset != boffset + (i * ssize))
			panic("ffs_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dofftofsb(fs, buflist->bs_children[0]->b_bio2.bio_offset)) !=
	    dtog(fs, dofftofsb(fs, buflist->bs_children[len - 1]->b_bio2.bio_offset)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block and
	 * the number of blocks that will fit into sbap starting at soff.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_db[0];
		soff = start_lbn;
		slen = NDADDR - soff;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, lblktodoff(fs, idp->in_lbn), (int)fs->fs_bsize, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (ufs_daddr_t *)sbp->b_data;
		soff = idp->in_off;
		slen = fs->fs_nindir - soff;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	pref = ffs_blkpref(ip, start_lbn, soff, sbap);

	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef DIAGNOSTIC
		if (start_ap[start_lvl-1].in_lbn == idp->in_lbn)
			panic("ffs_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, lblktodoff(fs, idp->in_lbn), (int)fs->fs_bsize, &ebp))
			goto fail;
		ebap = (ufs_daddr_t *)ebp->b_data;
	}

	/*
	 * Make sure we aren't spanning more then two blockmaps.  ssize is
	 * our calculation of the span we have to scan in the first blockmap,
	 * while slen is our calculation of the number of entries available
	 * in the first blockmap (from soff).
	 */
	if (ssize > slen) {
		panic("ffs_reallocblks: range spans more then two blockmaps!"
			" start_lbn %ld len %d (%d/%d)",
			(long)start_lbn, len, slen, ssize);
	}
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = (ufs_daddr_t)ffs_hashalloc(ip, dtog(fs, pref), (long)pref,
	    len, ffs_clusteralloc)) == 0)
		goto fail;
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		kprintf("realloc: ino %ju, lbns %d-%d\n\told:",
		    (uintmax_t)ip->i_number, start_lbn, end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dofftofsb(fs, buflist->bs_children[i]->b_bio2.bio_offset), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 2");
		if (dofftofsb(fs, buflist->bs_children[i]->b_bio2.bio_offset) != *bap)
			panic("ffs_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			kprintf(" %d,", *bap);
#endif
		if (DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_db[0] && i < ssize)
				softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ffs_update(vp, 1);
	}
	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		kprintf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (!DOINGSOFTDEP(vp) &&
		    buflist->bs_children[i]->b_bio2.bio_offset != NOOFFSET) {
			ffs_blkfree(ip,
			    dofftofsb(fs, buflist->bs_children[i]->b_bio2.bio_offset),
			    fs->fs_bsize);
		}
		buflist->bs_children[i]->b_bio2.bio_offset = fsbtodoff(fs, blkno);
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dofftofsb(fs, buflist->bs_children[i]->b_bio2.bio_offset), fs->fs_bsize))
			panic("ffs_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			kprintf(" %d,", blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		kprintf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_db[0])
		brelse(sbp);
	return (ENOSPC);
}

/*
 * Allocate an inode in the filesystem.
 *
 * If allocating a directory, use ffs_dirpref to select the inode.
 * If allocating in a directory, the following hierarchy is followed:
 *   1) allocate the preferred inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 * If no inode preference is given the following heirarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available inode is located.
 */
int
ffs_valloc(struct vnode *pvp, int mode, struct ucred *cred, struct vnode **vpp)
{
	struct inode *pip;
	struct fs *fs;
	struct inode *ip;
	ino_t ino, ipref;
	int cg, error;

	*vpp = NULL;
	pip = VTOI(pvp);
	fs = pip->i_fs;
	if (fs->fs_cstotal.cs_nifree == 0)
		goto noinodes;

	if ((mode & IFMT) == IFDIR)
		ipref = ffs_dirpref(pip);
	else
		ipref = pip->i_number;
	if (ipref >= fs->fs_ncg * fs->fs_ipg)
		ipref = 0;
	cg = ino_to_cg(fs, ipref);
	/*
	 * Track number of dirs created one after another
	 * in a same cg without intervening by files.
	 */
	if ((mode & IFMT) == IFDIR) {
		if (fs->fs_contigdirs[cg] < 255)
			fs->fs_contigdirs[cg]++;
	} else {
		if (fs->fs_contigdirs[cg] > 0)
			fs->fs_contigdirs[cg]--;
	}
	ino = (ino_t)ffs_hashalloc(pip, cg, (long)ipref, mode,
					(allocfcn_t *)ffs_nodealloccg);
	if (ino == 0)
		goto noinodes;
	error = VFS_VGET(pvp->v_mount, NULL, ino, vpp);
	if (error) {
		ffs_vfree(pvp, ino, mode);
		return (error);
	}
	ip = VTOI(*vpp);
	if (ip->i_mode) {
		kprintf("mode = 0%o, inum = %lu, fs = %s\n",
		    ip->i_mode, (u_long)ip->i_number, fs->fs_fsmnt);
		panic("ffs_valloc: dup alloc");
	}
	if (ip->i_blocks) {				/* XXX */
		kprintf("free inode %s/%lu had %ld blocks\n",
		    fs->fs_fsmnt, (u_long)ino, (long)ip->i_blocks);
		ip->i_blocks = 0;
	}
	ip->i_flags = 0;
	/*
	 * Set up a new generation number for this inode.
	 */
	if (ip->i_gen == 0 || ++ip->i_gen == 0)
		ip->i_gen = krandom() / 2 + 1;
	return (0);
noinodes:
	ffs_fserr(fs, cred->cr_uid, "out of inodes");
	uprintf("\n%s: create/symlink failed, no inodes free\n", fs->fs_fsmnt);
	return (ENOSPC);
}

/*
 * Find a cylinder group to place a directory.
 *
 * The policy implemented by this algorithm is to allocate a
 * directory inode in the same cylinder group as its parent
 * directory, but also to reserve space for its files inodes
 * and data. Restrict the number of directories which may be
 * allocated one after another in the same cylinder group
 * without intervening allocation of files.
 *
 * If we allocate a first level directory then force allocation
 * in another cylinder group.
 */
static ino_t
ffs_dirpref(struct inode *pip)
{
	struct fs *fs;
	int cg, prefcg, dirsize, cgsize;
	int64_t dirsize64;
	int avgifree, avgbfree, avgndir, curdirsize;
	int minifree, minbfree, maxndir;
	int mincg, minndir;
	int maxcontigdirs;

	fs = pip->i_fs;

	avgifree = fs->fs_cstotal.cs_nifree / fs->fs_ncg;
	avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
	avgndir = fs->fs_cstotal.cs_ndir / fs->fs_ncg;

	/*
	 * Force allocation in another cg if creating a first level dir.
	 */
	if (ITOV(pip)->v_flag & VROOT) {
		prefcg = karc4random() % fs->fs_ncg;
		mincg = prefcg;
		minndir = fs->fs_ipg;
		for (cg = prefcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		for (cg = 0; cg < prefcg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		return ((ino_t)(fs->fs_ipg * mincg));
	}

	/*
	 * Count various limits which used for
	 * optimal allocation of a directory inode.
	 */
	maxndir = min(avgndir + fs->fs_ipg / 16, fs->fs_ipg);
	minifree = avgifree - avgifree / 4;
	if (minifree < 1)
		minifree = 1;
	minbfree = avgbfree - avgbfree / 4;
	if (minbfree < 1)
		minbfree = 1;
	cgsize = fs->fs_fsize * fs->fs_fpg;

	/*
	 * fs_avgfilesize and fs_avgfpdir are user-settable entities and
	 * multiplying them may overflow a 32 bit integer.
	 */
	dirsize64 = fs->fs_avgfilesize * (int64_t)fs->fs_avgfpdir;
	if (dirsize64 > 0x7fffffff) {
		maxcontigdirs = 1;
	} else {
		dirsize = (int)dirsize64;
		curdirsize = avgndir ?
			(cgsize - avgbfree * fs->fs_bsize) / avgndir : 0;
		if (dirsize < curdirsize)
			dirsize = curdirsize;
		maxcontigdirs = min((avgbfree * fs->fs_bsize) / dirsize, 255);
		if (fs->fs_avgfpdir > 0)
			maxcontigdirs = min(maxcontigdirs,
				    fs->fs_ipg / fs->fs_avgfpdir);
		if (maxcontigdirs == 0)
			maxcontigdirs = 1;
	}

	/*
	 * Limit number of dirs in one cg and reserve space for 
	 * regular files, but only if we have no deficit in
	 * inodes or space.
	 */
	prefcg = ino_to_cg(fs, pip->i_number);
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				return ((ino_t)(fs->fs_ipg * cg));
		}
	/*
	 * This is a backstop when we have deficit in space.
	 */
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			return ((ino_t)(fs->fs_ipg * cg));
	for (cg = 0; cg < prefcg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			break;
	return ((ino_t)(fs->fs_ipg * cg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. If no blocks have been allocated in any other section, the
 * policy is to place the section in a cylinder group with a greater than
 * average number of free blocks.  An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block, the information on the previous allocation is unavailable;
 * here a best guess is made based upon the logical block number being
 * allocated.
 *
 * If a section is already partially allocated, the policy is to
 * contiguously allocate fs_maxcontig blocks.  The end of one of these
 * contiguous blocks and the beginning of the next is physically separated
 * so that the disk head will be in transit between them for at least
 * fs_rotdelay milliseconds.  This is to allow time for the processor to
 * schedule another I/O transfer.
 */
ufs_daddr_t
ffs_blkpref(struct inode *ip, ufs_daddr_t lbn, int indx, ufs_daddr_t *bap)
{
	struct fs *fs;
	int cg;
	int avgbfree, startcg;
	ufs_daddr_t nextblk;

	fs = ip->i_fs;
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		if (lbn < NDADDR + NINDIR(fs)) {
			cg = ino_to_cg(fs, ip->i_number);
			return (fs->fs_fpg * cg + fs->fs_frag);
		}
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg =
			    ino_to_cg(fs, ip->i_number) + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (fs->fs_fpg * cg + fs->fs_frag);
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				return (fs->fs_fpg * cg + fs->fs_frag);
			}
		return (0);
	}
	/*
	 * One or more previous blocks have been laid out. If less
	 * than fs_maxcontig previous blocks are contiguous, the
	 * next block is requested contiguously, otherwise it is
	 * requested rotationally delayed by fs_rotdelay milliseconds.
	 */
	nextblk = bap[indx - 1] + fs->fs_frag;
	if (fs->fs_rotdelay == 0 || indx < fs->fs_maxcontig ||
	    bap[indx - fs->fs_maxcontig] +
	    blkstofrags(fs, fs->fs_maxcontig) != nextblk)
		return (nextblk);
	/*
	 * Here we convert ms of delay to frags as:
	 * (frags) = (ms) * (rev/sec) * (sect/rev) /
	 *	((sect/frag) * (ms/sec))
	 * then round up to the next block.
	 */
	nextblk += roundup(fs->fs_rotdelay * fs->fs_rps * fs->fs_nsect /
	    (NSPF(fs) * 1000), fs->fs_frag);
	return (nextblk);
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 */
/*VARARGS5*/
static u_long
ffs_hashalloc(struct inode *ip, int cg, long pref,
	      int size,	/* size for data blocks, mode for inodes */
	      allocfcn_t *allocator)
{
	struct fs *fs;
	long result;	/* XXX why not same type as we return? */
	int i, icg = cg;

	fs = ip->i_fs;
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->fs_ncg)
			cg -= fs->fs_ncg;
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->fs_ncg;
	for (i = 2; i < fs->fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
		cg++;
		if (cg == fs->fs_ncg)
			cg = 0;
	}
	return (0);
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and
 * if they are, allocate them.
 */
static ufs_daddr_t
ffs_fragextend(struct inode *ip, int cg, long bprev, int osize, int nsize)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	long bno;
	int frags, bbase;
	int i, error;
	uint8_t *blksfree;

	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nffree < numfrags(fs, nsize - osize))
		return (0);
	frags = numfrags(fs, nsize);
	bbase = fragnum(fs, bprev);
	if (bbase > fragnum(fs, (bprev + frags - 1))) {
		/* cannot extend across a block boundary */
		return (0);
	}
	KKASSERT(blknum(fs, bprev) == blknum(fs, bprev + frags - 1));
	error = bread(ip->i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		(int)fs->fs_cgsize, &bp);
	if (error) {
		brelse(bp);
		return (0);
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp)) {
		brelse(bp);
		return (0);
	}
	cgp->cg_time = time_second;
	bno = dtogd(fs, bprev);
	blksfree = cg_blksfree(cgp);
	for (i = numfrags(fs, osize); i < frags; i++) {
		if (isclr(blksfree, bno + i)) {
			brelse(bp);
			return (0);
		}
	}

	/*
	 * the current fragment can be extended
	 * deduct the count on fragment being extended into
	 * increase the count on the remaining fragment (if any)
	 * allocate the extended piece
	 *
	 * ---oooooooooonnnnnnn111----
	 *    [-----frags-----]
	 *    ^                       ^
	 *    bbase                   fs_frag
	 */
	for (i = frags; i < fs->fs_frag - bbase; i++) {
		if (isclr(blksfree, bno + i))
			break;
	}

	/*
	 * Size of original free frag is [i - numfrags(fs, osize)]
	 * Size of remaining free frag is [i - frags]
	 */
	cgp->cg_frsum[i - numfrags(fs, osize)]--;
	if (i != frags)
		cgp->cg_frsum[i - frags]++;
	for (i = numfrags(fs, osize); i < frags; i++) {
		clrbit(blksfree, bno + i);
		cgp->cg_cs.cs_nffree--;
		fs->fs_cstotal.cs_nffree--;
		fs->fs_cs(fs, cg).cs_nffree--;
	}
	fs->fs_fmod = 1;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, bprev);
	bdwrite(bp);
	return (bprev);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 */
static ufs_daddr_t
ffs_alloccg(struct inode *ip, int cg, ufs_daddr_t bpref, int size)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int i;
	ufs_daddr_t bno, blkno;
	int allocsiz, error, frags;
	uint8_t *blksfree;

	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nbfree == 0 && size == fs->fs_bsize)
		return (0);
	error = bread(ip->i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		(int)fs->fs_cgsize, &bp);
	if (error) {
		brelse(bp);
		return (0);
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp) ||
	    (cgp->cg_cs.cs_nbfree == 0 && size == fs->fs_bsize)) {
		brelse(bp);
		return (0);
	}
	cgp->cg_time = time_second;
	if (size == fs->fs_bsize) {
		bno = ffs_alloccgblk(ip, bp, bpref);
		bdwrite(bp);
		return (bno);
	}
	/*
	 * Check to see if any fragments of sufficient size are already
	 * available.  Fit the data into a larger fragment if necessary,
	 * before allocating a whole new block.
	 */
	blksfree = cg_blksfree(cgp);
	frags = numfrags(fs, size);
	for (allocsiz = frags; allocsiz < fs->fs_frag; allocsiz++) {
		if (cgp->cg_frsum[allocsiz] != 0)
			break;
	}
	if (allocsiz == fs->fs_frag) {
		/*
		 * No fragments were available, allocate a whole block and
		 * cut the requested fragment (of size frags) out of it.
		 */
		if (cgp->cg_cs.cs_nbfree == 0) {
			brelse(bp);
			return (0);
		}
		bno = ffs_alloccgblk(ip, bp, bpref);
		bpref = dtogd(fs, bno);
		for (i = frags; i < fs->fs_frag; i++)
			setbit(blksfree, bpref + i);

		/*
		 * Calculate the number of free frags still remaining after
		 * we have cut out the requested allocation.  Indicate that
		 * a fragment of that size is now available for future
		 * allocation.
		 */
		i = fs->fs_frag - frags;
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		fs->fs_fmod = 1;
		cgp->cg_frsum[i]++;
		bdwrite(bp);
		return (bno);
	}

	/*
	 * cg_frsum[] has told us that a free fragment of allocsiz size is
	 * available.  Find it, then clear the bitmap bits associated with
	 * the size we want.
	 */
	bno = ffs_mapsearch(fs, cgp, bpref, allocsiz);
	if (bno < 0) {
		brelse(bp);
		return (0);
	}
	for (i = 0; i < frags; i++)
		clrbit(blksfree, bno + i);
	cgp->cg_cs.cs_nffree -= frags;
	fs->fs_cstotal.cs_nffree -= frags;
	fs->fs_cs(fs, cg).cs_nffree -= frags;
	fs->fs_fmod = 1;

	/*
	 * Account for the allocation.  The original searched size that we
	 * found is no longer available.  If we cut out a smaller piece then
	 * a smaller fragment is now available.
	 */
	cgp->cg_frsum[allocsiz]--;
	if (frags != allocsiz)
		cgp->cg_frsum[allocsiz - frags]++;
	blkno = cg * fs->fs_fpg + bno;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, blkno);
	bdwrite(bp);
	return ((u_long)blkno);
}

/*
 * Allocate a block in a cylinder group.
 *
 * This algorithm implements the following policy:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate the next available block on the block rotor for the
 *      specified cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
static ufs_daddr_t
ffs_alloccgblk(struct inode *ip, struct buf *bp, ufs_daddr_t bpref)
{
	struct fs *fs;
	struct cg *cgp;
	ufs_daddr_t bno, blkno;
	int cylno, pos, delta;
	short *cylbp;
	int i;
	uint8_t *blksfree;

	fs = ip->i_fs;
	cgp = (struct cg *)bp->b_data;
	blksfree = cg_blksfree(cgp);
	if (bpref == 0 || dtog(fs, bpref) != cgp->cg_cgx) {
		bpref = cgp->cg_rotor;
		goto norot;
	}
	bpref = blknum(fs, bpref);
	bpref = dtogd(fs, bpref);
	/*
	 * if the requested block is available, use it
	 */
	if (ffs_isblock(fs, blksfree, fragstoblks(fs, bpref))) {
		bno = bpref;
		goto gotit;
	}
	if (fs->fs_nrpos <= 1 || fs->fs_cpc == 0) {
		/*
		 * Block layout information is not available.
		 * Leaving bpref unchanged means we take the
		 * next available free block following the one
		 * we just allocated. Hopefully this will at
		 * least hit a track cache on drives of unknown
		 * geometry (e.g. SCSI).
		 */
		goto norot;
	}
	/*
	 * check for a block available on the same cylinder
	 */
	cylno = cbtocylno(fs, bpref);
	if (cg_blktot(cgp)[cylno] == 0)
		goto norot;
	/*
	 * check the summary information to see if a block is
	 * available in the requested cylinder starting at the
	 * requested rotational position and proceeding around.
	 */
	cylbp = cg_blks(fs, cgp, cylno);
	pos = cbtorpos(fs, bpref);
	for (i = pos; i < fs->fs_nrpos; i++)
		if (cylbp[i] > 0)
			break;
	if (i == fs->fs_nrpos)
		for (i = 0; i < pos; i++)
			if (cylbp[i] > 0)
				break;
	if (cylbp[i] > 0) {
		/*
		 * found a rotational position, now find the actual
		 * block. A panic if none is actually there.
		 */
		pos = cylno % fs->fs_cpc;
		bno = (cylno - pos) * fs->fs_spc / NSPB(fs);
		if (fs_postbl(fs, pos)[i] == -1) {
			kprintf("pos = %d, i = %d, fs = %s\n",
			    pos, i, fs->fs_fsmnt);
			panic("ffs_alloccgblk: cyl groups corrupted");
		}
		for (i = fs_postbl(fs, pos)[i];; ) {
			if (ffs_isblock(fs, blksfree, bno + i)) {
				bno = blkstofrags(fs, (bno + i));
				goto gotit;
			}
			delta = fs_rotbl(fs)[i];
			if (delta <= 0 ||
			    delta + i > fragstoblks(fs, fs->fs_fpg))
				break;
			i += delta;
		}
		kprintf("pos = %d, i = %d, fs = %s\n", pos, i, fs->fs_fsmnt);
		panic("ffs_alloccgblk: can't find blk in cyl");
	}
norot:
	/*
	 * no blocks in the requested cylinder, so take next
	 * available one in this cylinder group.
	 */
	bno = ffs_mapsearch(fs, cgp, bpref, (int)fs->fs_frag);
	if (bno < 0)
		return (0);
	cgp->cg_rotor = bno;
gotit:
	blkno = fragstoblks(fs, bno);
	ffs_clrblock(fs, blksfree, (long)blkno);
	ffs_clusteracct(fs, cgp, blkno, -1);
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;
	cylno = cbtocylno(fs, bno);
	cg_blks(fs, cgp, cylno)[cbtorpos(fs, bno)]--;
	cg_blktot(cgp)[cylno]--;
	fs->fs_fmod = 1;
	blkno = cgp->cg_cgx * fs->fs_fpg + bno;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, blkno);
	return (blkno);
}

/*
 * Determine whether a cluster can be allocated.
 *
 * We do not currently check for optimal rotational layout if there
 * are multiple choices in the same cylinder group. Instead we just
 * take the first one that we find following bpref.
 */
static ufs_daddr_t
ffs_clusteralloc(struct inode *ip, int cg, ufs_daddr_t bpref, int len)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int i, got, run, bno, bit, map;
	u_char *mapp;
	int32_t *lp;
	uint8_t *blksfree;

	fs = ip->i_fs;
	if (fs->fs_maxcluster[cg] < len)
		return (0);
	if (bread(ip->i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		  (int)fs->fs_cgsize, &bp)) {
		goto fail;
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp))
		goto fail;

	/*
	 * Check to see if a cluster of the needed size (or bigger) is
	 * available in this cylinder group.
	 */
	lp = &cg_clustersum(cgp)[len];
	for (i = len; i <= fs->fs_contigsumsize; i++)
		if (*lp++ > 0)
			break;
	if (i > fs->fs_contigsumsize) {
		/*
		 * This is the first time looking for a cluster in this
		 * cylinder group. Update the cluster summary information
		 * to reflect the true maximum sized cluster so that
		 * future cluster allocation requests can avoid reading
		 * the cylinder group map only to find no clusters.
		 */
		lp = &cg_clustersum(cgp)[len - 1];
		for (i = len - 1; i > 0; i--)
			if (*lp-- > 0)
				break;
		fs->fs_maxcluster[cg] = i;
		goto fail;
	}
	/*
	 * Search the cluster map to find a big enough cluster.
	 * We take the first one that we find, even if it is larger
	 * than we need as we prefer to get one close to the previous
	 * block allocation. We do not search before the current
	 * preference point as we do not want to allocate a block
	 * that is allocated before the previous one (as we will
	 * then have to wait for another pass of the elevator
	 * algorithm before it will be read). We prefer to fail and
	 * be recalled to try an allocation in the next cylinder group.
	 */
	if (dtog(fs, bpref) != cg)
		bpref = 0;
	else
		bpref = fragstoblks(fs, dtogd(fs, blknum(fs, bpref)));
	mapp = &cg_clustersfree(cgp)[bpref / NBBY];
	map = *mapp++;
	bit = 1 << (bpref % NBBY);
	for (run = 0, got = bpref; got < cgp->cg_nclusterblks; got++) {
		if ((map & bit) == 0) {
			run = 0;
		} else {
			run++;
			if (run == len)
				break;
		}
		if ((got & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	if (got >= cgp->cg_nclusterblks)
		goto fail;
	/*
	 * Allocate the cluster that we have found.
	 */
	blksfree = cg_blksfree(cgp);
	for (i = 1; i <= len; i++) {
		if (!ffs_isblock(fs, blksfree, got - run + i))
			panic("ffs_clusteralloc: map mismatch");
	}
	bno = cg * fs->fs_fpg + blkstofrags(fs, got - run + 1);
	if (dtog(fs, bno) != cg)
		panic("ffs_clusteralloc: allocated out of group");
	len = blkstofrags(fs, len);
	for (i = 0; i < len; i += fs->fs_frag) {
		if ((got = ffs_alloccgblk(ip, bp, bno + i)) != bno + i)
			panic("ffs_clusteralloc: lost block");
	}
	bdwrite(bp);
	return (bno);

fail:
	brelse(bp);
	return (0);
}

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *      inode in the specified cylinder group.
 *   3) the inode must not already be in the inode hash table.  We
 *	can encounter such a case because the vnode reclamation sequence
 *	frees the bit
 *   3) the inode must not already be in the inode hash, otherwise it
 *	may be in the process of being deallocated.  This can occur
 *	because the bitmap is updated before the inode is removed from
 *	hash.  If we were to reallocate the inode the caller could wind
 *	up returning a vnode/inode combination which is in an indeterminate
 *	state.
 */
static ino_t
ffs_nodealloccg(struct inode *ip, int cg, ufs_daddr_t ipref, int mode)
{
	struct ufsmount *ump;
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	uint8_t *inosused;
	uint8_t map;
	int error, len, arraysize, i;
	int icheckmiss;
	ufs_daddr_t ibase;
	struct vnode *vp;

	vp = ITOV(ip);
	ump = VFSTOUFS(vp->v_mount);
	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		return (0);
	error = bread(ip->i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		      (int)fs->fs_cgsize, &bp);
	if (error) {
		brelse(bp);
		return (0);
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp) || cgp->cg_cs.cs_nifree == 0) {
		brelse(bp);
		return (0);
	}
	inosused = cg_inosused(cgp);
	icheckmiss = 0;

	/*
	 * Quick check, reuse the most recently free inode or continue
	 * a scan from where we left off the last time.
	 */
	ibase = cg * fs->fs_ipg;
	if (ipref) {
		ipref %= fs->fs_ipg;
		if (isclr(inosused, ipref)) {
			if (ufs_ihashcheck(ump, ip->i_dev, ibase + ipref) == 0)
				goto gotit;
		}
	}

	/*
	 * Scan the inode bitmap starting at irotor, be sure to handle
	 * the edge case by going back to the beginning of the array.
	 *
	 * If the number of inodes is not byte-aligned, the unused bits
	 * should be set to 1.  This will be sanity checked in gotit.  Note
	 * that we have to be sure not to overlap the beginning and end
	 * when irotor is in the middle of a byte as this will cause the
	 * same bitmap byte to be checked twice.  To solve this problem we
	 * just convert everything to a byte index for the loop.
	 */
	ipref = (cgp->cg_irotor % fs->fs_ipg) >> 3;	/* byte index */
	len = (fs->fs_ipg + 7) >> 3;			/* byte size */
	arraysize = len;

	while (len > 0) {
		map = inosused[ipref];
		if (map != 255) {
			for (i = 0; i < NBBY; ++i) {
				/*
				 * If we find a free bit we have to make sure
				 * that the inode is not in the middle of
				 * being destroyed.  The inode should not exist
				 * in the inode hash.
				 *
				 * Adjust the rotor to try to hit the 
				 * quick-check up above.
				 */
				if ((map & (1 << i)) == 0) {
					if (ufs_ihashcheck(ump, ip->i_dev, ibase + (ipref << 3) + i) == 0) {
						ipref = (ipref << 3) + i;
						cgp->cg_irotor = (ipref + 1) % fs->fs_ipg;
						goto gotit;
					}
					++icheckmiss;
				}
			}
		}

		/*
		 * Setup for the next byte, start at the beginning again if
		 * we hit the end of the array.
		 */
		if (++ipref == arraysize)
			ipref = 0;
		--len;
	}
	if (icheckmiss == cgp->cg_cs.cs_nifree) {
		brelse(bp);
		return(0);
	}
	kprintf("fs = %s\n", fs->fs_fsmnt);
	panic("ffs_nodealloccg: block not in map, icheckmiss/nfree %d/%d",
		icheckmiss, cgp->cg_cs.cs_nifree);
	/* NOTREACHED */

	/*
	 * ipref is a bit index as of the gotit label.
	 */
gotit:
	KKASSERT(ipref >= 0 && ipref < fs->fs_ipg);
	cgp->cg_time = time_second;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_inomapdep(bp, ip, ibase + ipref);
	setbit(inosused, ipref);
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	fs->fs_fmod = 1;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir++;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}
	bdwrite(bp);
	return (ibase + ipref);
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible
 * block reassembly is checked.
 */
void
ffs_blkfree_cg(struct fs * fs, struct vnode * i_devvp, cdev_t i_dev, ino_t i_number,
	        uint32_t i_din_uid, ufs_daddr_t bno, long size)
{
	struct cg *cgp;
	struct buf *bp;
	ufs_daddr_t blkno;
	int i, error, cg, blk, frags, bbase;
	uint8_t *blksfree;

	VOP_FREEBLKS(i_devvp, fsbtodoff(fs, bno), size);
	if ((uint)size > fs->fs_bsize || fragoff(fs, size) != 0 ||
	    fragnum(fs, bno) + numfrags(fs, size) > fs->fs_frag) {
		kprintf("dev=%s, bno = %ld, bsize = %ld, size = %ld, fs = %s\n",
		    devtoname(i_dev), (long)bno, (long)fs->fs_bsize, size,
		    fs->fs_fsmnt);
		panic("ffs_blkfree: bad size");
	}
	cg = dtog(fs, bno);
	if ((uint)bno >= fs->fs_size) {
		kprintf("bad block %ld, ino %lu\n",
		    (long)bno, (u_long)i_number);
		ffs_fserr(fs, i_din_uid, "bad block");
		return;
	}

	/*
	 * Load the cylinder group
	 */
	error = bread(i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		      (int)fs->fs_cgsize, &bp);
	if (error) {
		brelse(bp);
		return;
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp)) {
		brelse(bp);
		return;
	}
	cgp->cg_time = time_second;
	bno = dtogd(fs, bno);
	blksfree = cg_blksfree(cgp);

	if (size == fs->fs_bsize) {
		/*
		 * Free a whole block
		 */
		blkno = fragstoblks(fs, bno);
		if (!ffs_isfreeblock(fs, blksfree, blkno)) {
			kprintf("dev = %s, block = %ld, fs = %s\n",
			    devtoname(i_dev), (long)bno, fs->fs_fsmnt);
			panic("ffs_blkfree: freeing free block");
		}
		ffs_setblock(fs, blksfree, blkno);
		ffs_clusteracct(fs, cgp, blkno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;
		i = cbtocylno(fs, bno);
		cg_blks(fs, cgp, i)[cbtorpos(fs, bno)]++;
		cg_blktot(cgp)[i]++;
	} else {
		/*
		 * Free a fragment within a block.
		 *
		 * bno is the starting block number of the fragment being
		 * freed.
		 *
		 * bbase is the starting block number for the filesystem
		 * block containing the fragment.
		 *
		 * blk is the current bitmap for the fragments within the
		 * filesystem block containing the fragment.
		 *
		 * frags is the number of fragments being freed
		 *
		 * Call ffs_fragacct() to account for the removal of all
		 * current fragments, then adjust the bitmap to free the
		 * requested fragment, and finally call ffs_fragacct() again
		 * to regenerate the accounting.
		 */
		bbase = bno - fragnum(fs, bno);
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isset(blksfree, bno + i)) {
				kprintf("dev = %s, block = %ld, fs = %s\n",
				    devtoname(i_dev), (long)(bno + i),
				    fs->fs_fsmnt);
				panic("ffs_blkfree: freeing free frag");
			}
			setbit(blksfree, bno + i);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;

		/*
		 * Add back in counts associated with the new frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);

		/*
		 * If a complete block has been reassembled, account for it
		 */
		blkno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, blksfree, blkno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			ffs_clusteracct(fs, cgp, blkno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;
			i = cbtocylno(fs, bbase);
			cg_blks(fs, cgp, i)[cbtorpos(fs, bbase)]++;
			cg_blktot(cgp)[i]++;
		}
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
}

struct ffs_blkfree_trim_params {
	struct task task;
	ufs_daddr_t bno;
	long size;

	/* 
	 * With TRIM,  inode pointer is gone in the callback but we still need 
	 * the following fields for  ffs_blkfree_cg() 
	 */
	struct vnode *i_devvp;
	struct fs *i_fs;
	cdev_t i_dev; 
	ino_t i_number;
	uint32_t i_din_uid;
};

        
static void
ffs_blkfree_trim_task(void *ctx, int pending)
{
	struct ffs_blkfree_trim_params *tp;

	tp = ctx;
	ffs_blkfree_cg(tp->i_fs, tp->i_devvp, tp->i_dev, tp->i_number,
	    tp->i_din_uid, tp->bno, tp->size);
	kfree(tp, M_TEMP);
}



static void
ffs_blkfree_trim_completed(struct bio *biop)
{
	struct buf *bp = biop->bio_buf;
	struct ffs_blkfree_trim_params *tp;

	tp = bp->b_bio1.bio_caller_info1.ptr;
	TASK_INIT(&tp->task, 0, ffs_blkfree_trim_task, tp);
	tp = biop->bio_caller_info1.ptr;
	taskqueue_enqueue(taskqueue_swi, &tp->task);
	biodone(biop);
}


/*
 * If TRIM is enabled, we TRIM the blocks first then free them. We do this 
 * after TRIM is finished and the callback handler is called. The logic here
 * is that we free the blocks before updating the bitmap so that we don't
 * reuse a block before we actually trim it, which would result in trimming
 * a valid block.
 */
void
ffs_blkfree(struct inode *ip, ufs_daddr_t bno, long size) 
{
	struct mount *mp = ip->i_devvp->v_mount;
	struct ffs_blkfree_trim_params *tp;

	if (!(mp->mnt_flag & MNT_TRIM)) {
		ffs_blkfree_cg(ip->i_fs, ip->i_devvp,ip->i_dev,ip->i_number,
		    ip->i_uid, bno, size);
		return;
	}

	struct buf *bp;	

	tp = kmalloc(sizeof(struct ffs_blkfree_trim_params), M_TEMP, M_WAITOK);
	tp->bno = bno;
	tp->i_fs= ip->i_fs;
	tp->i_devvp = ip->i_devvp;
	tp->i_dev = ip->i_dev;
	tp->i_din_uid = ip->i_uid;
	tp->i_number = ip->i_number;
	tp->size = size;

	bp = getnewbuf(0,0,0,1);
	BUF_KERNPROC(bp);
	bp->b_cmd = BUF_CMD_FREEBLKS;
	bp->b_bio1.bio_offset =  fsbtodoff(ip->i_fs, bno);
	bp->b_bcount = size;
	bp->b_bio1.bio_caller_info1.ptr = tp;
	bp->b_bio1.bio_done = ffs_blkfree_trim_completed;
	vn_strategy(ip->i_devvp, &bp->b_bio1);	
}

#ifdef DIAGNOSTIC
/*
 * Verify allocation of a block or fragment. Returns true if block or
 * fragment is allocated, false if it is free.
 */
static int
ffs_checkblk(struct inode *ip, ufs_daddr_t bno, long size)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int i, error, frags, free;
	uint8_t *blksfree;

	fs = ip->i_fs;
	if ((uint)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		kprintf("bsize = %ld, size = %ld, fs = %s\n",
		    (long)fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_checkblk: bad size");
	}
	if ((uint)bno >= fs->fs_size)
		panic("ffs_checkblk: bad block %d", bno);
	error = bread(ip->i_devvp, fsbtodoff(fs, cgtod(fs, dtog(fs, bno))),
		      (int)fs->fs_cgsize, &bp);
	if (error)
		panic("ffs_checkblk: cg bread failed");
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp))
		panic("ffs_checkblk: cg magic mismatch");
	blksfree = cg_blksfree(cgp);
	bno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		free = ffs_isblock(fs, blksfree, fragstoblks(fs, bno));
	} else {
		frags = numfrags(fs, size);
		for (free = 0, i = 0; i < frags; i++)
			if (isset(blksfree, bno + i))
				free++;
		if (free != 0 && free != frags)
			panic("ffs_checkblk: partially free fragment");
	}
	brelse(bp);
	return (!free);
}
#endif /* DIAGNOSTIC */

/*
 * Free an inode.
 */
int
ffs_vfree(struct vnode *pvp, ino_t ino, int mode)
{
	if (DOINGSOFTDEP(pvp)) {
		softdep_freefile(pvp, ino, mode);
		return (0);
	}
	return (ffs_freefile(pvp, ino, mode));
}

/*
 * Do the actual free operation.
 * The specified inode is placed back in the free map.
 */
int
ffs_freefile(struct vnode *pvp, ino_t ino, int mode)
{
	struct fs *fs;
	struct cg *cgp;
	struct inode *pip;
	struct buf *bp;
	int error, cg;
	uint8_t *inosused;

	pip = VTOI(pvp);
	fs = pip->i_fs;
	if ((uint)ino >= fs->fs_ipg * fs->fs_ncg)
		panic("ffs_vfree: range: dev = (%d,%d), ino = %"PRId64", fs = %s",
		    major(pip->i_dev), minor(pip->i_dev), ino, fs->fs_fsmnt);
	cg = ino_to_cg(fs, ino);
	error = bread(pip->i_devvp, fsbtodoff(fs, cgtod(fs, cg)),
		      (int)fs->fs_cgsize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp)) {
		brelse(bp);
		return (0);
	}
	cgp->cg_time = time_second;
	inosused = cg_inosused(cgp);
	ino %= fs->fs_ipg;
	if (isclr(inosused, ino)) {
		kprintf("dev = %s, ino = %lu, fs = %s\n",
		    devtoname(pip->i_dev), (u_long)ino, fs->fs_fsmnt);
		if (fs->fs_ronly == 0)
			panic("ffs_vfree: freeing free inode");
	}
	clrbit(inosused, ino);
	if (ino < cgp->cg_irotor)
		cgp->cg_irotor = ino;
	cgp->cg_cs.cs_nifree++;
	fs->fs_cstotal.cs_nifree++;
	fs->fs_cs(fs, cg).cs_nifree++;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir--;
		fs->fs_cstotal.cs_ndir--;
		fs->fs_cs(fs, cg).cs_ndir--;
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
	return (0);
}

/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
static ufs_daddr_t
ffs_mapsearch(struct fs *fs, struct cg *cgp, ufs_daddr_t bpref, int allocsiz)
{
	ufs_daddr_t bno;
	int start, len, loc, i;
	int blk, field, subfield, pos;
	uint8_t *blksfree;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern.
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = cgp->cg_frotor / NBBY;
	blksfree = cg_blksfree(cgp);
	len = howmany(fs->fs_fpg, NBBY) - start;
	loc = scanc((uint)len, (u_char *)&blksfree[start],
		(u_char *)fragtbl[fs->fs_frag],
		(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
	if (loc == 0) {
		len = start + 1;	/* XXX why overlap here? */
		start = 0;
		loc = scanc((uint)len, (u_char *)&blksfree[0],
			(u_char *)fragtbl[fs->fs_frag],
			(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
		if (loc == 0) {
			kprintf("start = %d, len = %d, fs = %s\n",
			    start, len, fs->fs_fsmnt);
			panic("ffs_alloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	bno = (start + len - loc) * NBBY;
	cgp->cg_frotor = bno;
	/*
	 * found the byte in the map
	 * sift through the bits to find the selected frag
	 */
	for (i = bno + NBBY; bno < i; bno += fs->fs_frag) {
		blk = blkmap(fs, blksfree, bno);
		blk <<= 1;
		field = around[allocsiz];
		subfield = inside[allocsiz];
		for (pos = 0; pos <= fs->fs_frag - allocsiz; pos++) {
			if ((blk & field) == subfield)
				return (bno + pos);
			field <<= 1;
			subfield <<= 1;
		}
	}
	kprintf("bno = %lu, fs = %s\n", (u_long)bno, fs->fs_fsmnt);
	panic("ffs_alloccg: block not in map");
	return (-1);
}

/*
 * Update the cluster map because of an allocation or free.
 *
 * Cnt == 1 means free; cnt == -1 means allocating.
 */
static void
ffs_clusteracct(struct fs *fs, struct cg *cgp, ufs_daddr_t blkno, int cnt)
{
	int32_t *sump;
	int32_t *lp;
	u_char *freemapp, *mapp;
	int i, start, end, forw, back, map, bit;

	if (fs->fs_contigsumsize <= 0)
		return;
	freemapp = cg_clustersfree(cgp);
	sump = cg_clustersum(cgp);
	/*
	 * Allocate or clear the actual block.
	 */
	if (cnt > 0)
		setbit(freemapp, blkno);
	else
		clrbit(freemapp, blkno);
	/*
	 * Find the size of the cluster going forward.
	 */
	start = blkno + 1;
	end = start + fs->fs_contigsumsize;
	if (end >= cgp->cg_nclusterblks)
		end = cgp->cg_nclusterblks;
	mapp = &freemapp[start / NBBY];
	map = *mapp++;
	bit = 1 << (start % NBBY);
	for (i = start; i < end; i++) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	forw = i - start;
	/*
	 * Find the size of the cluster going backward.
	 */
	start = blkno - 1;
	end = start - fs->fs_contigsumsize;
	if (end < 0)
		end = -1;
	mapp = &freemapp[start / NBBY];
	map = *mapp--;
	bit = 1 << (start % NBBY);
	for (i = start; i > end; i--) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != 0) {
			bit >>= 1;
		} else {
			map = *mapp--;
			bit = 1 << (NBBY - 1);
		}
	}
	back = start - i;
	/*
	 * Account for old cluster and the possibly new forward and
	 * back clusters.
	 */
	i = back + forw + 1;
	if (i > fs->fs_contigsumsize)
		i = fs->fs_contigsumsize;
	sump[i] += cnt;
	if (back > 0)
		sump[back] -= cnt;
	if (forw > 0)
		sump[forw] -= cnt;
	/*
	 * Update cluster summary information.
	 */
	lp = &sump[fs->fs_contigsumsize];
	for (i = fs->fs_contigsumsize; i > 0; i--)
		if (*lp-- > 0)
			break;
	fs->fs_maxcluster[cgp->cg_cgx] = i;
}

/*
 * Fserr prints the name of a filesystem with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
static void
ffs_fserr(struct fs *fs, uint uid, char *cp)
{
	struct thread *td = curthread;
	struct proc *p;

	if ((p = td->td_proc) != NULL) {
	    log(LOG_ERR, "pid %d (%s), uid %d on %s: %s\n", p ? p->p_pid : -1,
		    p ? p->p_comm : "-", uid, fs->fs_fsmnt, cp);
	} else {
	    log(LOG_ERR, "system thread %p, uid %d on %s: %s\n",
		    td, uid, fs->fs_fsmnt, cp);
	}
}
