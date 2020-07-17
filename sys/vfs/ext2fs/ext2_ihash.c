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
 * @(#)ufs_ihash.c	8.7 (Berkeley) 5/17/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_ihash.c,v 1.20 1999/08/28 00:52:29 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <vfs/ext2fs/fs.h>
#include <vfs/ext2fs/ext2fs.h>
#include <vfs/ext2fs/inode.h>
#include <vfs/ext2fs/ext2_dinode.h>
#include <vfs/ext2fs/ext2_extern.h>

static MALLOC_DEFINE(M_EXT2IHASH, "EXT2 ihash", "EXT2 Inode hash tables");
/*
 * Structures associated with inode cacheing.
 */
static struct inode **ext2_ihashtbl;
static u_long	ext2_ihash;		/* size of hash table - 1 */
static struct lwkt_token ext2_ihash_token;

#define	INOHASH(device, inum)	(&ext2_ihashtbl[(minor(device) + (inum)) & ext2_ihash])

/*
 * Initialize inode hash table.
 */
void
ext2_ihashinit(void)
{
	ext2_ihash = vfs_inodehashsize();
	ext2_ihashtbl = malloc(sizeof(void *) * ext2_ihash, M_EXT2IHASH,
	    M_WAITOK | M_ZERO);
	--ext2_ihash;
	lwkt_token_init(&ext2_ihash_token, "ext2ihash");
}

void
ext2_ihashuninit(void)
{
	lwkt_gettoken(&ext2_ihash_token);
	if (ext2_ihashtbl)
		free(ext2_ihashtbl, M_EXT2IHASH);
	lwkt_reltoken(&ext2_ihash_token);
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, but locked, wait for it.
 *
 * Note that the serializing tokens do not prevent other processes from
 * playing with the data structure being protected while we are blocked.
 * They do protect us from preemptive interrupts which might try to
 * play with the protected data structure.
 */
struct vnode *
ext2_ihashget(cdev_t dev, ino_t inum)
{
	struct inode *ip;
	struct vnode *vp;

	lwkt_gettoken(&ext2_ihash_token);
loop:
	for (ip = *INOHASH(dev, inum); ip; ip = ip->i_next) {
		if (inum != ip->i_number || dev != ip->i_dev)
			continue;
		vp = ITOV(ip);
		if (vget(vp, LK_EXCLUSIVE))
			goto loop;
		/*
		 * We must check to see if the inode has been ripped
		 * out from under us after blocking.
		 */
		for (ip = *INOHASH(dev, inum); ip; ip = ip->i_next) {
			if (inum == ip->i_number && dev == ip->i_dev)
				break;
		}
		if (ip == NULL || ITOV(ip) != vp) {
			vput(vp);
			goto loop;
		}
		lwkt_reltoken(&ext2_ihash_token);
		return (vp);
	}
	lwkt_reltoken(&ext2_ihash_token);
	return (NULL);
}

/*
 * Insert the inode into the hash table, and return it locked.
 */
int
ext2_ihashins(struct inode *ip)
{
	struct inode **ipp;
	struct inode *iq;

	KKASSERT((ip->i_flag & IN_HASHED) == 0);
	lwkt_gettoken(&ext2_ihash_token);
	ipp = INOHASH(ip->i_dev, ip->i_number);
	while ((iq = *ipp) != NULL) {
		if (ip->i_dev == iq->i_dev && ip->i_number == iq->i_number) {
			lwkt_reltoken(&ext2_ihash_token);
			return (EBUSY);
		}
		ipp = &iq->i_next;
	}
	ip->i_next = NULL;
	*ipp = ip;
	ip->i_flag |= IN_HASHED;
	lwkt_reltoken(&ext2_ihash_token);
	return (0);
}

/*
 * Remove the inode from the hash table.
 */
void
ext2_ihashrem(struct inode *ip)
{
	struct inode **ipp;
	struct inode *iq;

	lwkt_gettoken(&ext2_ihash_token);
	if (ip->i_flag & IN_HASHED) {
		ipp = INOHASH(ip->i_dev, ip->i_number);
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
	lwkt_reltoken(&ext2_ihash_token);
}
