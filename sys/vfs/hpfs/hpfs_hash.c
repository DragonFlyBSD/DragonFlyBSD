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
 *	@(#)ufs_ihash.c	8.7 (Berkeley) 5/17/95
 * $FreeBSD: src/sys/fs/hpfs/hpfs_hash.c,v 1.1 1999/12/09 19:09:58 semenu Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include "hpfs.h"

MALLOC_DEFINE(M_HPFSHASH, "HPFS hash", "HPFS node hash tables");

/*
 * Structures associated with hpfsnode cacheing.
 */
static LIST_HEAD(hphashhead, hpfsnode) *hpfs_hphashtbl;
static u_long	hpfs_hphash;		/* size of hash table - 1 */
#define	HPNOHASH(dev, lsn)	(&hpfs_hphashtbl[(minor(dev) + (lsn)) & hpfs_hphash])
#ifndef NULL_SIMPLELOCKS
static struct lwkt_token hpfs_hphash_token;
#endif
struct lock hpfs_hphash_lock;

/*
 * Initialize inode hash table.
 */
void
hpfs_hphashinit(void)
{

	lockinit (&hpfs_hphash_lock, "hpfs_hphashlock", 0, 0);
	hpfs_hphashtbl = hashinit(desiredvnodes, M_HPFSHASH, &hpfs_hphash);
	lwkt_token_init(&hpfs_hphash_token, "hpfsihash");
}

/*
 * Free the inode hash.
 */
int
hpfs_hphash_uninit(struct vfsconf *vfc)
{
	lwkt_gettoken(&hpfs_hphash_token);
	if (hpfs_hphashtbl)
		hashdestroy(hpfs_hphashtbl, M_HPFSHASH, hpfs_hphash);
	lwkt_reltoken(&hpfs_hphash_token);

	return 0;
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, return it, even if it is locked.
 */
struct hpfsnode *
hpfs_hphashlookup(cdev_t dev, lsn_t ino)
{
	struct hpfsnode *hp;

	lwkt_gettoken(&hpfs_hphash_token);
	for (hp = HPNOHASH(dev, ino)->lh_first; hp; hp = hp->h_hash.le_next) {
		if (ino == hp->h_no && dev == hp->h_dev)
			break;
	}
	lwkt_reltoken(&hpfs_hphash_token);

	return (hp);
}

struct vnode *
hpfs_hphashvget(cdev_t dev, lsn_t ino)
{
	struct hpfsnode *hp;
	struct vnode *vp;

	lwkt_gettoken(&hpfs_hphash_token);
loop:
	for (hp = HPNOHASH(dev, ino)->lh_first; hp; hp = hp->h_hash.le_next) {
		if (ino != hp->h_no || dev != hp->h_dev)
			continue;
		vp = HPTOV(hp);

		if (vget(vp, LK_EXCLUSIVE))
			goto loop;
		/*
		 * We must check to see if the inode has been ripped
		 * out from under us after blocking.
		 */
		for (hp = HPNOHASH(dev, ino)->lh_first; hp; hp = hp->h_hash.le_next) {
			if (ino == hp->h_no && dev == hp->h_dev)
				break;
		}
		if (hp == NULL || vp != HPTOV(hp)) {
			vput(vp);
			goto loop;
		}

		/*
		 * Or if the vget fails (due to a race)
		 */
		lwkt_reltoken(&hpfs_hphash_token);
		return (vp);
	}
	lwkt_reltoken(&hpfs_hphash_token);
	return (NULLVP);
}

/*
 * Insert the hpfsnode into the hash table.
 */
void
hpfs_hphashins(struct hpfsnode *hp)
{
	struct hphashhead *hpp;

	lwkt_gettoken(&hpfs_hphash_token);
	hpp = HPNOHASH(hp->h_dev, hp->h_no);
	hp->h_flag |= H_HASHED;
	LIST_INSERT_HEAD(hpp, hp, h_hash);
	lwkt_reltoken(&hpfs_hphash_token);
}

/*
 * Remove the inode from the hash table.
 */
void
hpfs_hphashrem(struct hpfsnode *hp)
{
	lwkt_gettoken(&hpfs_hphash_token);
	if (hp->h_flag & H_HASHED) {
		hp->h_flag &= ~H_HASHED;
		LIST_REMOVE(hp, h_hash);
#ifdef DIAGNOSTIC
		hp->h_hash.le_next = NULL;
		hp->h_hash.le_prev = NULL;
#endif
	}
	lwkt_reltoken(&hpfs_hphash_token);
}
