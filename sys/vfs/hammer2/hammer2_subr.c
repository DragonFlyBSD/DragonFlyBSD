/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
#include <sys/cdefs.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

/*
 * HAMMER2 inode locks
 *
 * HAMMER2 offers shared locks, update locks, and exclusive locks on inodes.
 *
 * Shared locks allow concurrent access to an inode's fields, but exclude
 * access by concurrent exclusive locks.
 *
 * Update locks are interesting -- an update lock will be taken after all
 * shared locks on an inode are released, but once it is in place, shared
 * locks may proceed. The update field is signalled by a busy flag in the
 * inode. Only one update lock may be in place at a given time on an inode.
 *
 * Exclusive locks prevent concurrent access to the inode.
 *
 * XXX: What do we use each for? How is visibility to the inode controlled?
 */

void
hammer2_inode_lock_sh(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_SHARED);
}

void
hammer2_inode_lock_up(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
	++ip->busy;
	lockmgr(&ip->lk, LK_DOWNGRADE);
}

void
hammer2_inode_lock_ex(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
}

void
hammer2_inode_unlock_ex(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

void
hammer2_inode_unlock_up(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_UPGRADE);
	--ip->busy;
	lockmgr(&ip->lk, LK_RELEASE);
}

void
hammer2_inode_unlock_sh(hammer2_inode_t *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

/*
 * Mount-wide locks
 */

void
hammer2_mount_exlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_EXCLUSIVE);
}

void
hammer2_mount_shlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_SHARED);
}

void
hammer2_mount_unlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->lk, LK_RELEASE);
}

/*
 * Inode/vnode subroutines
 */

/*
 * Get the vnode associated with the given inode, allocating the vnode if
 * necessary.
 *
 * Great care must be taken to avoid deadlocks and vnode acquisition/reclaim
 * races.
 *
 * The vnode will be returned exclusively locked and referenced.  The
 * reference on the vnode prevents it from being reclaimed.
 *
 * The inode (ip) must be referenced by the caller and not locked to avoid
 * it getting ripped out from under us or deadlocked.
 */
struct vnode *
hammer2_igetv(hammer2_inode_t *ip, int *errorp)
{
	struct vnode *vp;
	hammer2_mount_t *hmp;

	hmp = ip->hmp;
	*errorp = 0;

	for (;;) {
		/*
		 * Attempt to reuse an existing vnode assignment.  It is
		 * possible to race a reclaim so the vget() may fail.  The
		 * inode must be unlocked during the vget() to avoid a
		 * deadlock against a reclaim.
		 */
		vp = ip->vp;
		if (vp) {
			/*
			 * Lock the inode and check for a reclaim race
			 */
			hammer2_inode_lock_ex(ip);
			if (ip->vp != vp) {
				hammer2_inode_unlock_ex(ip);
				continue;
			}

			/*
			 * Inode must be unlocked during the vget() to avoid
			 * possible deadlocks, vnode is held to prevent
			 * destruction during the vget().  The vget() can
			 * still fail if we lost a reclaim race on the vnode.
			 */
			vhold_interlocked(vp);
			hammer2_inode_unlock_ex(ip);
			if (vget(vp, LK_EXCLUSIVE)) {
				vdrop(vp);
				continue;
			}
			vdrop(vp);
			/* vp still locked and ref from vget */
			*errorp = 0;
			break;
		}

		/*
		 * No vnode exists, allocate a new vnode.  Beware of
		 * allocation races.  This function will return an
		 * exclusively locked and referenced vnode.
		 */
		*errorp = getnewvnode(VT_HAMMER2, H2TOMP(hmp), &vp, 0, 0);
		if (*errorp) {
			vp = NULL;
			break;
		}

		/*
		 * Lock the inode and check for an allocation race.
		 */
		hammer2_inode_lock_ex(ip);
		if (ip->vp != NULL) {
			vp->v_type = VBAD;
			vx_put(vp);
			hammer2_inode_unlock_ex(ip);
			continue;
		}

		kprintf("igetv new\n");
		switch (ip->type & HAMMER2_INODE_TYPE_MASK) {
		case HAMMER2_INODE_TYPE_DIR:
			vp->v_type = VDIR;
			break;
		case HAMMER2_INODE_TYPE_FILE:
			vp->v_type = VREG;
			vinitvmio(vp, 0, HAMMER2_LBUFSIZE,
				  (int)ip->data.size & HAMMER2_LBUFMASK);
			break;
		/* XXX FIFO */
		default:
			break;
		}

		if (ip->type & HAMMER2_INODE_TYPE_ROOT)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
		hammer2_inode_unlock_ex(ip);
		break;
	}

	/*
	 * Return non-NULL vp and *errorp == 0, or NULL vp and *errorp != 0.
	 */
	return (vp);
}

/*
 * Allocate a HAMMER2 inode memory structure.
 *
 * The returned inode is locked exclusively and referenced.
 * The HAMMER2 mountpoint must be locked on entry.
 */
hammer2_inode_t *
hammer2_alloci(hammer2_mount_t *hmp)
{
	hammer2_inode_t *ip;

	kprintf("alloci\n");

	ip = kmalloc(sizeof(hammer2_inode_t), hmp->inodes, M_WAITOK | M_ZERO);
	if (!ip) {
		/* XXX */
	}

	++hmp->ninodes;

	ip->type = 0;
	ip->hmp = hmp;
	lockinit(&ip->lk, "h2inode", 0, 0);
	ip->vp = NULL;
	ip->refs = 1;
	hammer2_inode_lock_ex(ip);

	return (ip);
}

/*
 * Free a HAMMER2 inode memory structure.
 *
 * The inode must be locked exclusively with one reference and will
 * be destroyed on return.
 */
void
hammer2_freei(hammer2_inode_t *ip)
{
	hammer2_mount_t *hmp = ip->hmp;

	KKASSERT(ip->hmp == NULL);
	KKASSERT(ip->vp == NULL);
	KKASSERT(ip->refs == 1);
	hammer2_inode_unlock_ex(ip);
	kfree(ip, hmp->inodes);
}
