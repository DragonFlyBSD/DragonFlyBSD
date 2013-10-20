/* $FreeBSD: src/sys/msdosfs/msdosfs_denode.c,v 1.47.2.3 2002/08/22 16:20:15 trhodes Exp $ */
/*	$NetBSD: msdosfs_denode.c,v 1.28 1998/02/10 14:10:00 mrg Exp $	*/

/*-
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#include "bpb.h"
#include "msdosfsmount.h"
#include "direntry.h"
#include "denode.h"
#include "fat.h"

static int msdosfs_hashins (struct denode *dep);
static void msdosfs_hashrem (struct denode *dep);

static MALLOC_DEFINE(M_MSDOSFSNODE, "MSDOSFS node", "MSDOSFS vnode private part");

/*
 * Hash table caching denode instances.
 *
 * denodes are keyed by the disk location (cluster num, entry offset) of the
 * directory entry of the file they represent.
 *
 * denodes representing deleted but still opened files are left in this cache
 * until reclaimed.  Deleted directory entries can be reused when files are
 * renamed or new files created.  As a consequence, several denodes associated
 * with the same entry may coexist in this cache as long as a single one of
 * them map to an existing file (de_refcnt > 0).
 *
 * R/w access to this cache is protected by dehash_token.
 */
static struct denode **dehashtbl;
static u_long dehash;			/* size of hash table - 1 */
#define	DEHASH(dev, dcl, doff)	(dehashtbl[(minor(dev) + (dcl) + (doff) / 	\
				sizeof(struct direntry)) & dehash])
static struct lwkt_token dehash_token;

union _qcvt {
	quad_t qcvt;
	long val[2];
};
#define SETHIGH(q, h) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_HIGHWORD] = (h); \
	(q) = tmp.qcvt; \
}
#define SETLOW(q, l) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_LOWWORD] = (l); \
	(q) = tmp.qcvt; \
}

static struct denode *
		msdosfs_hashget (cdev_t dev, u_long dirclust, u_long diroff);

/*ARGSUSED*/
int 
msdosfs_init(struct vfsconf *vfsp)
{
	dehash = 16;
	while (dehash < desiredvnodes)
		dehash <<= 1;
	dehashtbl = kmalloc(sizeof(void *) * dehash, M_MSDOSFSMNT,
			   M_WAITOK|M_ZERO);
	--dehash;
	lwkt_token_init(&dehash_token, "msdosihash");
	return (0);
}

int 
msdosfs_uninit(struct vfsconf *vfsp)
{

	if (dehashtbl)
		kfree(dehashtbl, M_MSDOSFSMNT);
	return (0);
}

static struct denode *
msdosfs_hashget(cdev_t dev, u_long dirclust, u_long diroff)
{
	struct denode *dep;
	struct vnode *vp;

	lwkt_gettoken(&dehash_token);
loop:
	for (dep = DEHASH(dev, dirclust, diroff); dep; dep = dep->de_next) {
		if (dirclust != dep->de_dirclust
		    || diroff != dep->de_diroffset
		    || dev != dep->de_dev
		    || dep->de_refcnt <= 0) {
			continue;
		}
		vp = DETOV(dep);
		if (vget(vp, LK_EXCLUSIVE))
			goto loop;

		/*
		 * We must check to see if the inode has been ripped
		 * out from under us after blocking.
		 */
		for (dep = DEHASH(dev, dirclust, diroff); dep; dep = dep->de_next) {
			if (dirclust == dep->de_dirclust
			    && diroff == dep->de_diroffset
			    && dev == dep->de_dev
			    && dep->de_refcnt > 0) {
				break;
			}
		}
		if (dep == NULL || DETOV(dep) != vp) {
			vput(vp);
			goto loop;
		}
		lwkt_reltoken(&dehash_token);
		return (dep);
	}
	lwkt_reltoken(&dehash_token);
	return (NULL);
}

/*
 * Try to insert specified denode into the hash table.  Return 0 on success
 * and EBUSY if there is already a denode with the same key.
 */
static
int
msdosfs_hashins(struct denode *dep)
{
	struct denode **depp, *deq;

	lwkt_gettoken(&dehash_token);
	depp = &DEHASH(dep->de_dev, dep->de_dirclust, dep->de_diroffset);
	while ((deq = *depp) != NULL) {
		if (deq->de_dev == dep->de_dev &&
		    deq->de_dirclust == dep->de_dirclust &&
		    deq->de_diroffset == dep->de_diroffset &&
		    deq->de_refcnt > 0) {
			lwkt_reltoken(&dehash_token);
			return(EBUSY);
		}
		depp = &deq->de_next;
	}
	dep->de_next = NULL;
	*depp = dep;
	lwkt_reltoken(&dehash_token);
	return(0);
}

static
void
msdosfs_hashrem(struct denode *dep)
{
	struct denode **depp, *deq;

	lwkt_gettoken(&dehash_token);
	depp = &DEHASH(dep->de_dev, dep->de_dirclust, dep->de_diroffset);
	while ((deq = *depp) != NULL) {
		if (dep == deq)
			break;
		depp = &deq->de_next;
	}
	KKASSERT(dep == deq);
	*depp = dep->de_next;
	dep->de_next = NULL;
	lwkt_reltoken(&dehash_token);
}

void
msdosfs_reinsert(struct denode *ip, u_long new_dirclust, u_long new_diroffset)
{
	int error;

	lwkt_gettoken(&dehash_token);
	msdosfs_hashrem(ip);
	ip->de_dirclust = new_dirclust;
	ip->de_diroffset = new_diroffset;
	error = msdosfs_hashins(ip);
	KASSERT(!error, ("msdosfs_reinsert: insertion failed %d", error));
	lwkt_reltoken(&dehash_token);
}

/*
 * If deget() succeeds it returns with the gotten denode locked().
 *
 * pmp	     - address of msdosfsmount structure of the filesystem containing
 *	       the denode of interest.  The pm_dev field and the address of
 *	       the msdosfsmount structure are used.
 * dirclust  - which cluster bp contains, if dirclust is 0 (root directory)
 *	       diroffset is relative to the beginning of the root directory,
 *	       otherwise it is cluster relative.
 * diroffset - offset past begin of cluster of denode we want
 * depp	     - returns the address of the gotten denode.
 */
int
deget(struct msdosfsmount *pmp,	/* so we know the maj/min number */
      u_long dirclust,		/* cluster this dir entry came from */
      u_long diroffset,		/* index of entry within the cluster */
      struct denode **depp)	/* returns the addr of the gotten denode */
{
	int error;
	cdev_t dev = pmp->pm_dev;
	struct mount *mntp = pmp->pm_mountp;
	struct direntry *direntptr;
	struct denode *ldep;
	struct vnode *nvp;
	struct buf *bp;
	struct timeval tv;

#ifdef MSDOSFS_DEBUG
	kprintf("deget(pmp %p, dirclust %lu, diroffset %lx, depp %p)\n",
	    pmp, dirclust, diroffset, depp);
#endif

	/*
	 * On FAT32 filesystems, root is a (more or less) normal
	 * directory
	 */
	if (FAT32(pmp) && dirclust == MSDOSFSROOT)
		dirclust = pmp->pm_rootdirblk;

again:
	/*
	 * See if the denode is in the denode cache. Use the location of
	 * the directory entry to compute the hash value. For subdir use
	 * address of "." entry. For root dir (if not FAT32) use cluster
	 * MSDOSFSROOT, offset MSDOSFSROOT_OFS
	 *
	 * NOTE: The check for de_refcnt > 0 below insures the denode being
	 * examined does not represent an unlinked but still open file.
	 * These files are not to be accessible even when the directory
	 * entry that represented the file happens to be reused while the
	 * deleted file is still open.
	 */
	ldep = msdosfs_hashget(dev, dirclust, diroffset);
	if (ldep) {
		*depp = ldep;
		return (0);
	}

	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	ldep = kmalloc(sizeof(struct denode), M_MSDOSFSNODE,
		       M_WAITOK | M_ZERO);

	/*
	 * Directory entry was not in cache, have to create a vnode and
	 * copy it from the passed disk buffer.
	 */

	/* getnewvnode() does a vref() on the vnode */
	error = getnewvnode(VT_MSDOSFS, mntp, &nvp, VLKTIMEOUT, 0);
	if (error) {
		*depp = NULL;
		kfree(ldep, M_MSDOSFSNODE);
		return error;
	}

	ldep->de_vnode = nvp;
	ldep->de_flag = 0;
	ldep->de_devvp = 0;
	ldep->de_dev = dev;
	ldep->de_dirclust = dirclust;
	ldep->de_diroffset = diroffset;
	fc_purge(ldep, 0);	/* init the fat cache for this denode */

	/*
	 * Insert the denode into the hash queue.  If a collision occurs
	 * throw away the vnode and try again.
	 */
	error = msdosfs_hashins(ldep);
	if (error == EBUSY) {
		nvp->v_type = VBAD;
		vx_put(nvp);
		kfree(ldep, M_MSDOSFSNODE);
		goto again;
	} else if (error) {
		nvp->v_type = VBAD;
		vx_put(nvp);
		kfree(ldep, M_MSDOSFSNODE);
		*depp = NULL;
		return (EINVAL);
	}
	nvp->v_data = ldep;
	ldep->de_pmp = pmp;
	ldep->de_refcnt = 1;
	/*
	 * Copy the directory entry into the denode area of the vnode.
	 */
	if ((dirclust == MSDOSFSROOT
	     || (FAT32(pmp) && dirclust == pmp->pm_rootdirblk))
	    && diroffset == MSDOSFSROOT_OFS) {
		/*
		 * Directory entry for the root directory. There isn't one,
		 * so we manufacture one. We should probably rummage
		 * through the root directory and find a label entry (if it
		 * exists), and then use the time and date from that entry
		 * as the time and date for the root denode.
		 */
		vsetflags(nvp, VROOT); /* should be further down XXX */

		ldep->de_Attributes = ATTR_DIRECTORY;
		ldep->de_LowerCase = 0;
		if (FAT32(pmp))
			ldep->de_StartCluster = pmp->pm_rootdirblk;
			/* de_FileSize will be filled in further down */
		else {
			ldep->de_StartCluster = MSDOSFSROOT;
			ldep->de_FileSize = pmp->pm_rootdirsize * DEV_BSIZE;
		}
		/*
		 * fill in time and date so that dos2unixtime() doesn't
		 * spit up when called from msdosfs_getattr() with root
		 * denode
		 */
		ldep->de_CHun = 0;
		ldep->de_CTime = 0x0000;	/* 00:00:00	 */
		ldep->de_CDate = (0 << DD_YEAR_SHIFT) | (1 << DD_MONTH_SHIFT)
		    | (1 << DD_DAY_SHIFT);
		/* Jan 1, 1980	 */
		ldep->de_ADate = ldep->de_CDate;
		ldep->de_MTime = ldep->de_CTime;
		ldep->de_MDate = ldep->de_CDate;
		/* leave the other fields as garbage */
	} else {
		error = readep(pmp, dirclust, diroffset, &bp, &direntptr);
		if (error) {
			/*
			 * The denode does not contain anything useful, so
			 * it would be wrong to leave it on its hash chain.
			 * Arrange for vput() to just forget about it.
			 */
			ldep->de_Name[0] = SLOT_DELETED;
			nvp->v_type = VBAD;

			vx_put(nvp);
			*depp = NULL;
			return (error);
		}
		DE_INTERNALIZE(ldep, direntptr);
		brelse(bp);
	}

	/*
	 * Fill in a few fields of the vnode and finish filling in the
	 * denode.  Then return the address of the found denode.
	 */
	if (ldep->de_Attributes & ATTR_DIRECTORY) {
		/*
		 * Since DOS directory entries that describe directories
		 * have 0 in the filesize field, we take this opportunity
		 * to find out the length of the directory and plug it into
		 * the denode structure.
		 */
		u_long size;

		/*
		 * XXX Sometimes, these arrives that . entry have cluster
		 * number 0, when it shouldn't.  Use real cluster number
		 * instead of what is written in directory entry.
		 */
		if ((diroffset == 0) && (ldep->de_StartCluster != dirclust)) {
			kprintf("deget(): . entry at clust %ld != %ld\n",
					dirclust, ldep->de_StartCluster);
			ldep->de_StartCluster = dirclust;
		}

		nvp->v_type = VDIR;
		if (ldep->de_StartCluster != MSDOSFSROOT) {
			error = pcbmap(ldep, 0xffff, NULL, &size, NULL);
			if (error == E2BIG) {
				ldep->de_FileSize = de_cn2off(pmp, size);
				error = 0;
			} else
				kprintf("deget(): pcbmap returned %d\n", error);
		}
	} else {
		nvp->v_type = VREG;
	}
	getmicrouptime(&tv);
	SETHIGH(ldep->de_modrev, tv.tv_sec);
	SETLOW(ldep->de_modrev, tv.tv_usec * 4294);
	ldep->de_devvp = pmp->pm_devvp;
	vref(ldep->de_devvp);
	vinitvmio(nvp, ldep->de_FileSize, PAGE_SIZE, -1);
	/*
	 * Leave nvp locked and refd so the returned inode is effectively
	 * locked and refd.
	 */
	*depp = ldep;
	return (0);
}

int
deupdat(struct denode *dep, int waitfor)
{
	int error;
	struct buf *bp;
	struct direntry *dirp;
	struct timespec ts;

	if (DETOV(dep)->v_mount->mnt_flag & MNT_RDONLY)
		return (0);
	getnanotime(&ts);
	DETIMES(dep, &ts, &ts, &ts);
	if ((dep->de_flag & DE_MODIFIED) == 0)
		return (0);
	dep->de_flag &= ~DE_MODIFIED;
	if (dep->de_Attributes & ATTR_DIRECTORY)
		return (0);
	if (dep->de_refcnt <= 0)
		return (0);
	error = readde(dep, &bp, &dirp);
	if (error)
		return (error);
	DE_EXTERNALIZE(dirp, dep);
	if (waitfor)
		return (bwrite(bp));
	else {
		bdwrite(bp);
		return (0);
	}
}

/*
 * Truncate the file described by dep to the length specified by length.
 */
int
detrunc(struct denode *dep, u_long length, int flags)
{
	int error;
	int allerror;
	u_long eofentry;
	u_long chaintofree;
	daddr_t bn, cn;
	int boff;
	int isadir = dep->de_Attributes & ATTR_DIRECTORY;
	struct buf *bp;
	struct msdosfsmount *pmp = dep->de_pmp;

#ifdef MSDOSFS_DEBUG
	kprintf("detrunc(): file %s, length %lu, flags %x\n", dep->de_Name, length, flags);
#endif

	/*
	 * Disallow attempts to truncate the root directory since it is of
	 * fixed size.  That's just the way dos filesystems are.  We use
	 * the VROOT bit in the vnode because checking for the directory
	 * bit and a startcluster of 0 in the denode is not adequate to
	 * recognize the root directory at this point in a file or
	 * directory's life.
	 */
	if ((DETOV(dep)->v_flag & VROOT) && !FAT32(pmp)) {
		kprintf("detrunc(): can't truncate root directory, clust %ld, offset %ld\n",
		    dep->de_dirclust, dep->de_diroffset);
		return (EINVAL);
	}


	if (dep->de_FileSize < length) {
		vnode_pager_setsize(DETOV(dep), length);
		return deextend(dep, length);
	}

	/*
	 * If the desired length is 0 then remember the starting cluster of
	 * the file and set the StartCluster field in the directory entry
	 * to 0.  If the desired length is not zero, then get the number of
	 * the last cluster in the shortened file.  Then get the number of
	 * the first cluster in the part of the file that is to be freed.
	 * Then set the next cluster pointer in the last cluster of the
	 * file to CLUST_EOFE.
	 */
	if (length == 0) {
		chaintofree = dep->de_StartCluster;
		dep->de_StartCluster = 0;
		eofentry = ~0;
	} else {
		error = pcbmap(dep, de_clcount(pmp, length) - 1,
			       NULL, &eofentry, NULL);
		if (error) {
#ifdef MSDOSFS_DEBUG
			kprintf("detrunc(): pcbmap fails %d\n", error);
#endif
			return (error);
		}
	}

	fc_purge(dep, de_clcount(pmp, length));

	/*
	 * If the new length is not a multiple of the cluster size then we
	 * must zero the tail end of the new last cluster in case it
	 * becomes part of the file again because of a seek.
	 */
	if ((boff = length & pmp->pm_crbomask) != 0) {
		if (isadir) {
			bn = xcntobn(pmp, eofentry);
			error = bread(pmp->pm_devvp, de_bntodoff(pmp, bn), pmp->pm_bpcluster, &bp);
		} else {
			cn = de_cluster(pmp, length);
			error = bread(DETOV(dep), de_cn2doff(pmp, cn), pmp->pm_bpcluster, &bp);
		}
		if (error) {
			brelse(bp);
#ifdef MSDOSFS_DEBUG
			kprintf("detrunc(): bread fails %d\n", error);
#endif
			return (error);
		}
		/*
		 * is this the right place for it?
		 */
		bzero(bp->b_data + boff, pmp->pm_bpcluster - boff);
		if (flags & IO_SYNC)
			bwrite(bp);
		else
			bdwrite(bp);
	}

	/*
	 * Write out the updated directory entry.  Even if the update fails
	 * we free the trailing clusters.
	 */
	dep->de_FileSize = length;
	if (!isadir)
		dep->de_flag |= DE_UPDATE|DE_MODIFIED;
	allerror = vtruncbuf(DETOV(dep), length, pmp->pm_bpcluster);
#ifdef MSDOSFS_DEBUG
	if (allerror)
		kprintf("detrunc(): vtruncbuf error %d\n", allerror);
#endif
	error = deupdat(dep, 1);
	if (error && (allerror == 0))
		allerror = error;
#ifdef MSDOSFS_DEBUG
	kprintf("detrunc(): allerror %d, eofentry %lu\n",
	       allerror, eofentry);
#endif

	/*
	 * If we need to break the cluster chain for the file then do it
	 * now.
	 */
	if (eofentry != ~0) {
		error = fatentry(FAT_GET_AND_SET, pmp, eofentry,
				 &chaintofree, CLUST_EOFE);
		if (error) {
#ifdef MSDOSFS_DEBUG
			kprintf("detrunc(): fatentry errors %d\n", error);
#endif
			return (error);
		}
		fc_setcache(dep, FC_LASTFC, de_cluster(pmp, length - 1),
			    eofentry);
	}

	/*
	 * Now free the clusters removed from the file because of the
	 * truncation.
	 */
	if (chaintofree != 0 && !MSDOSFSEOF(pmp, chaintofree))
		freeclusterchain(pmp, chaintofree);

	return (allerror);
}

/*
 * Extend the file described by dep to length specified by length.
 */
int
deextend(struct denode *dep, u_long length)
{
	struct msdosfsmount *pmp = dep->de_pmp;
	u_long count;
	int error;

	/*
	 * The root of a DOS filesystem cannot be extended.
	 */
	if ((DETOV(dep)->v_flag & VROOT) && !FAT32(pmp))
		return (EINVAL);

	/*
	 * Directories cannot be extended.
	 */
	if (dep->de_Attributes & ATTR_DIRECTORY)
		return (EISDIR);

	if (length <= dep->de_FileSize)
		panic("deextend: file too large");

	/*
	 * Compute the number of clusters to allocate.
	 */
	count = de_clcount(pmp, length) - de_clcount(pmp, dep->de_FileSize);
	if (count > 0) {
		if (count > pmp->pm_freeclustercount)
			return (ENOSPC);
		error = extendfile(dep, count, NULL, NULL, DE_CLEAR);
		if (error) {
			/* truncate the added clusters away again */
			detrunc(dep, dep->de_FileSize, 0);
			return (error);
		}
	}
	dep->de_FileSize = length;
	dep->de_flag |= DE_UPDATE|DE_MODIFIED;
	return (deupdat(dep, 1));
}

/*
 * msdosfs_reclaim(struct vnode *a_vp)
 */
int
msdosfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_reclaim(): dep %p, file %s, refcnt %ld\n",
	    dep, dep ? (char *)dep->de_Name : "?", dep ? dep->de_refcnt : -1);
#endif

	if (prtactive && VREFCNT(vp) > 1)
		vprint("msdosfs_reclaim(): pushing active", vp);
	/*
	 * Remove the denode from its hash chain.
	 */
	vp->v_data = NULL;
	if (dep) {
		msdosfs_hashrem(dep);
		if (dep->de_devvp) {
			vrele(dep->de_devvp);
			dep->de_devvp = 0;
		}
		kfree(dep, M_MSDOSFSNODE);
	}
	return (0);
}

/*
 * msdosfs_inactive(struct vnode *a_vp)
 */
int
msdosfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	int error = 0;

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_inactive(): dep %p, de_Name[0] %x\n",
		dep, (dep ? dep->de_Name[0] : 0));
#endif

	if (prtactive && VREFCNT(vp) > 1)
		vprint("msdosfs_inactive(): pushing active", vp);

	/*
	 * Ignore denodes related to stale file handles.
	 */
	if (dep == NULL || dep->de_Name[0] == SLOT_DELETED)
		goto out;

	/*
	 * If the file has been deleted and it is on a read/write
	 * filesystem, then truncate the file, and mark the directory slot
	 * as empty.  (This may not be necessary for the dos filesystem.)
	 */
#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_inactive(): dep %p, refcnt %ld, mntflag %x, MNT_RDONLY %x\n",
	       dep, dep->de_refcnt, vp->v_mount->mnt_flag, MNT_RDONLY);
#endif
	if (dep->de_refcnt <= 0 && (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		error = detrunc(dep, (u_long) 0, 0);
		dep->de_flag |= DE_UPDATE;
		dep->de_Name[0] = SLOT_DELETED;
	}
	deupdat(dep, 0);

out:
	/*
	 * If we are done with the denode, reclaim it
	 * so that it can be reused immediately.
	 */
#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_inactive(): v_refcnt 0x%08x, de_Name[0] %x\n",
		vp->v_refcnt, (dep ? dep->de_Name[0] : 0));
#endif
	if (dep == NULL || dep->de_Name[0] == SLOT_DELETED)
		vrecycle(vp);
	return (error);
}
