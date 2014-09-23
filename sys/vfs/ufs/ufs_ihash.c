/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993, 1995
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
 *	@(#)ufs_ihash.c	8.7 (Berkeley) 5/17/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_ihash.c,v 1.20 1999/08/28 00:52:29 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/mount.h>

#include "quota.h"
#include "inode.h"
#include "ufs_extern.h"
#include "ufsmount.h"

static MALLOC_DEFINE(M_UFSIHASH, "UFS ihash", "UFS Inode hash tables");

#define	INOHASH(ump, inum)	\
	(&ump->um_ihashtbl[inum & ump->um_ihash])

/*
 * Initialize inode hash table.
 */
void
ufs_ihashinit(struct ufsmount *ump)
{
	u_long target = desiredvnodes / 4 + 1;

	ump->um_ihash = 16;
	while (ump->um_ihash < target)
		ump->um_ihash <<= 1;
	ump->um_ihashtbl = kmalloc(sizeof(void *) * ump->um_ihash, M_UFSIHASH,
				   M_WAITOK|M_ZERO);
	--ump->um_ihash;
}

void
ufs_ihashuninit(struct ufsmount *ump)
{
	if (ump->um_ihashtbl) {
		kfree(ump->um_ihashtbl, M_UFSIHASH);
		ump->um_ihashtbl = NULL;
	}
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, return it, even if it is locked.
 */
struct vnode *
ufs_ihashlookup(struct ufsmount *ump, cdev_t dev, ino_t inum)
{
	struct inode *ip = NULL;

	for (ip = *INOHASH(ump, inum); ip; ip = ip->i_next) {
		if (inum == ip->i_number && dev == ip->i_dev)
			break;
	}
	if (ip)
		return (ITOV(ip));
	return (NULLVP);
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, but locked, wait for it.
 *
 * This subroutine may block.
 */
struct vnode *
ufs_ihashget(struct ufsmount *ump, cdev_t dev, ino_t inum)
{
	struct inode *ip;
	struct vnode *vp;

loop:
	for (ip = *INOHASH(ump, inum); ip; ip = ip->i_next) {
		if (inum != ip->i_number || dev != ip->i_dev)
			continue;
		vp = ITOV(ip);
		if (vget(vp, LK_EXCLUSIVE))
			goto loop;
		/*
		 * We must check to see if the inode has been ripped
		 * out from under us after blocking.
		 */
		for (ip = *INOHASH(ump, inum); ip; ip = ip->i_next) {
			if (inum == ip->i_number && dev == ip->i_dev)
				break;
		}
		if (ip == NULL || ITOV(ip) != vp) {
			vput(vp);
			goto loop;
		}
		return (vp);
	}
	return (NULL);
}

/*
 * Check to see if an inode is in the hash table.  This is used to interlock
 * file free operations to ensure that the vnode is not reused due to a
 * reallocate of its inode number before we have had a chance to recycle it.
 */
int
ufs_ihashcheck(struct ufsmount *ump, cdev_t dev, ino_t inum)
{
	struct inode *ip;

	for (ip = *INOHASH(ump, inum); ip; ip = ip->i_next) {
		if (inum == ip->i_number && dev == ip->i_dev)
			break;
	}
	return(ip ? 1 : 0);
}

/*
 * Insert the inode into the hash table, and return it locked.
 */
int
ufs_ihashins(struct ufsmount *ump, struct inode *ip)
{
	struct inode **ipp;
	struct inode *iq;

	KKASSERT((ip->i_flag & IN_HASHED) == 0);
	ipp = INOHASH(ump, ip->i_number);
	while ((iq = *ipp) != NULL) {
		if (ip->i_dev == iq->i_dev && ip->i_number == iq->i_number) {
			return(EBUSY);
		}
		ipp = &iq->i_next;
	}
	ip->i_next = NULL;
	*ipp = ip;
	ip->i_flag |= IN_HASHED;
	return(0);
}

/*
 * Remove the inode from the hash table.
 */
void
ufs_ihashrem(struct ufsmount *ump, struct inode *ip)
{
	struct inode **ipp;
	struct inode *iq;

	if (ip->i_flag & IN_HASHED) {
		ipp = INOHASH(ump, ip->i_number);
		while ((iq = *ipp) != NULL) {
			if (ip == iq)
				break;
			ipp = &iq->i_next;
		}
		KKASSERT(ip == iq);
		*ipp = ip->i_next;
		ip->i_next = NULL;
		ip->i_flag &= ~IN_HASHED;
	}
}
