#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>

#include "hammer2.h"


/*
 * Last reference to a vnode is going away but it is still cached.
 */
int
hammer2_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;

	kprintf("hammer2_inactive\n");

	vp = ap->a_vp;
	ip = VTOI(vp);

	return (0);
}

/*
 * Reclaim a vnode so that it can be reused; after the inode is
 * disassociated, the filesystem must manage it alone.
 */
int
hammer2_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;

	kprintf("hammer2_reclaim\n");

	/* Is the vnode locked? Must it be on exit? */

	vp = ap->a_vp;
	ip = VTOI(vp);
	hmp = ip->mp;

	vp->v_data = NULL;
	ip->vp = NULL;

	return (0);
}


int
hammer2_fsync(struct vop_fsync_args *ap)
{
	kprintf("hammer2_fsync\n");
	return (EOPNOTSUPP);
}

int
hammer2_access(struct vop_access_args *ap)
{
	kprintf("hammer2_access\n");
	return (0);
}

int
hammer2_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp;
	struct vattr *vap;
	struct hammer2_inode *ip;

	vp = ap->a_vp;
	vap = ap->a_vap;

	kprintf("hammer2_getattr\n");

	ip = VTOI(vp);
	hammer2_inode_lock_sh(ip);

	vap->va_type = vp->v_type;
	vap->va_mode = 0777;
	vap->va_nlink = 1;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_size = 0;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_flags = 0;

	hammer2_inode_unlock_sh(ip);

	return (0);
}

int
hammer2_readdir(struct vop_readdir_args *ap)
{
	kprintf("hammer2_readdir\n");
	return (EOPNOTSUPP);
}

int
hammer2_read(struct vop_read_args *ap)
{
	return (EOPNOTSUPP);
}

int
hammer2_write(struct vop_write_args *ap)
{
	return (EOPNOTSUPP);
}

int
hammer2_nresolve(struct vop_nresolve_args *ap)
{
	kprintf("hammer2_nresolve\n");
	return (EOPNOTSUPP);
}

int
hammer2_bmap(struct vop_bmap_args *ap)
{
	kprintf("hammer2_bmap\n");
	return (EOPNOTSUPP);
}

int
hammer2_open(struct vop_open_args *ap)
{
	kprintf("hammer2_open\n");
	return vop_stdopen(ap);
}

int
hammer2_strategy(struct vop_strategy_args *ap)
{
	struct vnode *vp;
	struct bio *biop;
	struct buf *bp;
	struct hammer2_inode *ip;
	int error;

	vp = ap->a_vp;
	biop = ap->a_bio;
	bp = biop->bio_buf;
	ip = VTOI(vp);

	switch(bp->b_cmd) {
	case (BUF_CMD_READ):
	case (BUF_CMD_WRITE):
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(biop);
		break;
	}

	return (error);
}

int 
hammer2_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	struct hammer2_mount *hmp;
	int rc;

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		mp = ap->a_head.a_ops->head.vv_mount;
		hmp = MPTOH2(mp);

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &hmp->hm_export,
				(const struct export_args *) ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}
	return (rc);
}

struct vop_ops hammer2_vnode_vops = {
	.vop_default	= vop_defaultop,
	.vop_fsync	= hammer2_fsync,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_access	= hammer2_access,
	.vop_getattr	= hammer2_getattr,
	.vop_readdir	= hammer2_readdir,
	.vop_read	= hammer2_read,
	.vop_write	= hammer2_write,
	.vop_open	= hammer2_open,
	.vop_inactive	= hammer2_inactive,
	.vop_reclaim 	= hammer2_reclaim,
	.vop_nresolve	= hammer2_nresolve,
	.vop_mountctl	= hammer2_mountctl,
	.vop_bmap	= hammer2_bmap,
	.vop_strategy	= hammer2_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};
