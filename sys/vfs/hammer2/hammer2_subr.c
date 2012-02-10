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

void hammer2_inode_lock_sh(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_SHARED);
}

void hammer2_inode_lock_up(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
	++ip->busy;
	lockmgr(&ip->lk, LK_DOWNGRADE);
}

void hammer2_inode_lock_ex(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_EXCLUSIVE);
}

void hammer2_inode_unlock_ex(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

void hammer2_inode_unlock_up(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_UPGRADE);
	--ip->busy;
	lockmgr(&ip->lk, LK_RELEASE);
}

void hammer2_inode_unlock_sh(struct hammer2_inode *ip)
{
	lockmgr(&ip->lk, LK_RELEASE);
}

/*
 * Mount-wide locks
 */

void
hammer2_mount_exlock(struct hammer2_mount *hmp)
{
	lockmgr(&hmp->hm_lk, LK_EXCLUSIVE);
}

void
hammer2_mount_shlock(struct hammer2_mount *hmp)
{
	lockmgr(&hmp->hm_lk, LK_SHARED);
}

void
hammer2_mount_unlock(struct hammer2_mount *hmp)
{
	lockmgr(&hmp->hm_lk, LK_RELEASE);
}

/*
 * Inode/vnode subroutines
 */

/*
 * igetv:
 *
 *	Get a vnode associated with the given inode. If one exists, return it,
 *	locked and ref-ed. Otherwise, a new vnode is allocated and associated
 *	with the vnode.
 *
 *	The lock prevents the inode from being reclaimed, I believe (XXX)
 */
struct vnode *
igetv(struct hammer2_inode *ip, int *error)
{
	struct vnode *vp;
	struct hammer2_mount *hmp;
	int rc;

	hmp = ip->mp;
	rc = 0;

	kprintf("igetv\n");
	tsleep(&igetv, 0, "", hz * 10);

	hammer2_inode_lock_ex(ip);
	do {
		/* Reuse existing vnode */
		vp = ip->vp;
		if (vp) {
			/* XXX: Is this necessary? */
			vx_lock(vp);
			break;
		}

		/* Allocate and initialize a new vnode */
		rc = getnewvnode(VT_HAMMER2, H2TOMP(hmp), &vp,
				    VLKTIMEOUT, LK_CANRECURSE);
		if (rc) {
			vp = NULL;
			break;
		}

		kprintf("igetv new\n");
		switch (ip->type & HAMMER2_INODE_TYPE_MASK) {
		case HAMMER2_INODE_TYPE_DIR:
			vp->v_type = VDIR;
			break;
		case HAMMER2_INODE_TYPE_FILE:
			vp->v_type = VREG;
				/*XXX: Init w/ true file size; 0*/
			vinitvmio(vp, 0, PAGE_SIZE, -1);
			break;
		default:
			break;
		}

		if (ip->type & HAMMER2_INODE_TYPE_ROOT)
			vsetflags(vp, VROOT);

		vp->v_data = ip;
		ip->vp = vp;
	} while (0);
	hammer2_inode_unlock_ex(ip);

	/*
	 * XXX: Under what conditions can a vnode be reclaimed? How do we want
	 * to interlock against vreclaim calls into hammer2? When do we need to?
	 */

	kprintf("igetv exit\n");

	/* vp is either NULL or a locked, ref-ed vnode referring to inode ip */
	*error = rc;
	return (vp);
}

/*
 * alloci:
 *
 *	Allocate an inode in a HAMMER2 mount. The returned inode is locked
 *	exclusively. The HAMMER2 mountpoint must be locked on entry.
 */
struct hammer2_inode *alloci(struct hammer2_mount *hmp) {
	struct hammer2_inode *ip;

	kprintf("alloci\n");

	ip = kmalloc(sizeof(struct hammer2_inode), hmp->hm_inodes,
		     M_WAITOK | M_ZERO);
	if (!ip) {
		/* XXX */
	}

	++hmp->hm_ninodes;

	ip->type = 0;
	ip->mp = hmp;
	lockinit(&ip->lk, "h2inode", 0, 0);
	ip->vp = NULL;

	hammer2_inode_lock_ex(ip);

	return (ip);
}

