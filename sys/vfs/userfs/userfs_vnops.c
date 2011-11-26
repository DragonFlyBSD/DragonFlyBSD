/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/vfs/userfs/userfs_vnops.c,v 1.4 2007/11/20 21:03:51 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/namecache.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/syslink.h>
#include <sys/syslink_vfs.h>
#include <sys/unistd.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>

#include "userfs.h"

/*
 * USERFS VNOPS
 */
/*static int user_vop_vnoperate(struct vop_generic_args *);*/
static int user_vop_fsync (struct vop_fsync_args *);
static int user_vop_read (struct vop_read_args *);
static int user_vop_write (struct vop_write_args *);
static int user_vop_access (struct vop_access_args *);
static int user_vop_advlock (struct vop_advlock_args *);
static int user_vop_close (struct vop_close_args *);
static int user_vop_ncreate (struct vop_ncreate_args *);
static int user_vop_getattr (struct vop_getattr_args *);
static int user_vop_nresolve (struct vop_nresolve_args *);
static int user_vop_nlookupdotdot (struct vop_nlookupdotdot_args *);
static int user_vop_nlink (struct vop_nlink_args *);
static int user_vop_nmkdir (struct vop_nmkdir_args *);
static int user_vop_nmknod (struct vop_nmknod_args *);
static int user_vop_open (struct vop_open_args *);
static int user_vop_pathconf (struct vop_pathconf_args *);
static int user_vop_print (struct vop_print_args *);
static int user_vop_readdir (struct vop_readdir_args *);
static int user_vop_readlink (struct vop_readlink_args *);
static int user_vop_nremove (struct vop_nremove_args *);
static int user_vop_nrename (struct vop_nrename_args *);
static int user_vop_nrmdir (struct vop_nrmdir_args *);
static int user_vop_setattr (struct vop_setattr_args *);
static int user_vop_strategy (struct vop_strategy_args *);
static int user_vop_nsymlink (struct vop_nsymlink_args *);
static int user_vop_nwhiteout (struct vop_nwhiteout_args *);
static int user_vop_bmap (struct vop_bmap_args *);

struct vop_ops userfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		user_vop_fsync,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_read =		user_vop_read,
	.vop_write =		user_vop_write,
	.vop_access =		user_vop_access,
	.vop_advlock =		user_vop_advlock,
	.vop_close =		user_vop_close,
	.vop_ncreate =		user_vop_ncreate,
	.vop_getattr =		user_vop_getattr,
	.vop_inactive =		user_vop_inactive,
	.vop_reclaim =		user_vop_reclaim,
	.vop_nresolve =		user_vop_nresolve,
	.vop_nlookupdotdot =	user_vop_nlookupdotdot,
	.vop_nlink =		user_vop_nlink,
	.vop_nmkdir =		user_vop_nmkdir,
	.vop_nmknod =		user_vop_nmknod,
	.vop_open =		user_vop_open,
	.vop_pathconf =		user_vop_pathconf,
	.vop_print =		user_vop_print,
	.vop_readdir =		user_vop_readdir,
	.vop_readlink =		user_vop_readlink,
	.vop_nremove =		user_vop_nremove,
	.vop_nrename =		user_vop_nrename,
	.vop_nrmdir =		user_vop_nrmdir,
	.vop_setattr =		user_vop_setattr,
	.vop_strategy =		user_vop_strategy,
	.vop_nsymlink =		user_vop_nsymlink,
	.vop_nwhiteout =	user_vop_nwhiteout,
	.vop_bmap =		user_vop_bmap
};

#if 0
static
int
user_vop_vnoperate(struct vop_generic_args *)
{
	return (VOCALL(&userfs_vnode_vops, ap));
}
#endif

/*
 * vop_fsync(struct vnode *vp, int waitfor)
 */
static
int
user_vop_fsync (struct vop_fsync_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_FSYNC);
	user_elm_push_vnode(par, ap->a_vp);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_fsync\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_FSYNC|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_read(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
 */
static
int
user_vop_read (struct vop_read_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct uio *uio;
	struct buf *bp;
	int error;
	int offset;
	int n;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;
	uio = ap->a_uio;

	if (uio->uio_offset < 0)
		return (EINVAL);
	if (vp->v_type != VREG)
		return (EINVAL);

	kprintf("userfs_read\n");
	error = 0;
	while (uio->uio_resid > 0 && uio->uio_offset < ip->filesize) {
		/*
		 * Use buffer cache I/O (via user_vop_strategy), aligned
		 * on USERFS_BSIZE boundaries.
		 */
		offset = (int)uio->uio_offset & USERFS_BMASK;
		error = bread(vp, uio->uio_offset - offset, USERFS_BSIZE, &bp);
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Figure out how many bytes we can actually copy this loop.
		 */
		n = USERFS_BSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->filesize - uio->uio_offset)
			n = (int)(ip->filesize - uio->uio_offset);

		error = uiomove((char *)bp->b_data + offset, n, uio);
		bqrelse(bp);
		if (error)
			break;
	}
	kprintf("userfs_read error %d\n", error);
	return(error);
}

/*
 * vop_write(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
 */
static
int
user_vop_write (struct vop_write_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct buf *bp;
	struct uio *uio;
	int error;
	off_t loffset;
	size_t offset;
	size_t n;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;
	uio = ap->a_uio;

	if (vp->v_type != VREG)
		return (EINVAL);
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = ip->filesize;

	/*
	 * Check for illegal write offsets.  Valid range is 0...2^63-1
	 */
	loffset = uio->uio_offset;
	if (loffset < 0)
		return (EFBIG);
	if (uio->uio_resid) {
		/* GCC4 - workaround optimization */
		loffset += uio->uio_resid;
		if (loffset <= 0)
			return (EFBIG);
	}

	kprintf("userfs_write\n");
	error = 0;
	while (uio->uio_resid > 0) {
		/*
		 * Use buffer cache I/O (via user_vop_strategy), aligned
		 * on USERFS_BSIZE boundaries.
		 *
		 * XXX not optimized for complete write-overs or file
		 * extensions.  Note: must bread on UIO_NOCOPY writes.
		 *
		 * XXX No need to read if strictly appending.
		 */
		offset = (size_t)uio->uio_offset & USERFS_BMASK;
		/* if offset == ip->filesize use getblk instead */
		error = bread(vp, uio->uio_offset - offset, USERFS_BSIZE, &bp);
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Figure out how many bytes we can actually copy this loop.
		 */
		n = USERFS_BSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->filesize - uio->uio_offset)
			n = (size_t)(ip->filesize - uio->uio_offset);

		error = uiomove((char *)bp->b_data + offset, n, uio);
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Extend the file's size if necessary
		 */
		if (ip->filesize < uio->uio_offset)
			ip->filesize = uio->uio_offset;

		/*
		 * The data has been loaded into the buffer, write it out.
		 */
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if (ap->a_ioflag & IO_DIRECT) {
			bp->b_flags |= B_CLUSTEROK;
			bawrite(bp);
		} else {
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
		}
	}
	kprintf("userfs_write error %d\n", error);
	return(error);
}

/*
 * vop_access(struct vnode *vp, int mode, struct ucred *cred)
 */
static
int
user_vop_access (struct vop_access_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_ACCESS);
	user_elm_push_vnode(par, vp);
	user_elm_push_mode(par, ap->a_mode);
	user_elm_push_cred(par, ap->a_cred);
	sl_msg_fini(slmsg->msg);

	/*
	 * Issue the request and do basic validation of the response
	 */
	kprintf("userfs_access\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) != 0)
		goto done;
	par = &slmsg->rep->msg->sm_head;
	if (par->se_cmd != (SLVFS_CMD_VOP_ACCESS|SE_CMDF_REPLY)) {
		error = EBADRPC;
		goto done;
	}

done:
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_advlock(struct vnode *vp, caddr_t id, int op, struct flock *fl,
 *	       int flags)
 *
 * This vop is handled directly by the kernel.
 */
static
int
user_vop_advlock (struct vop_advlock_args *ap)
{
	struct user_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = vp->v_data;

	return (lf_advlock(ap, &ip->lockf, ip->filesize));
}

/*
 * vop_open(struct vnode *vp, int mode, struct ucred *cred, struct file *file)
 *
 * This vop is handled directly by the kernel.
 */
static
int
user_vop_open (struct vop_open_args *ap)
{
	return (vop_stdopen(ap));
}

/*
 * vop_close(struct vnode *vp, int fflag)
 *
 * This vop is handled directly by the kernel.
 */
static
int
user_vop_close (struct vop_close_args *ap)
{
	return (vop_stdclose(ap));
}

/*
 * vop_getattr(struct vnode *vp, struct vattr *vap)
 */
static
int
user_vop_getattr (struct vop_getattr_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	syslink_elm_t elm;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_GETATTR);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_getattr\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) != 0)
		goto done;
	par = &slmsg->rep->msg->sm_head;
	if (par->se_cmd != (SLVFS_CMD_VOP_GETATTR|SE_CMDF_REPLY)) {
		error = EBADRPC;
		goto done;
	}

	/*
	 * Parse reply content
	 */
	SL_FOREACH_ELEMENT(par, elm) {
		switch(elm->se_cmd) {
		case SLVFS_ELM_VATTR:
			error = user_elm_parse_vattr(elm, ap->a_vap);
			break;
		default:
			break;
		}
		if (error)
			break;
	}
done:
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_pathconf(int name, int *retval)
 *
 * This vop is handled directly by the kernel.
 */
static
int
user_vop_pathconf (struct vop_pathconf_args *ap)
{
	int error = 0;

	switch(ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		break;
	case _PC_MAX_CANON:
		*ap->a_retval = MAX_CANON;
		break;
	case _PC_MAX_INPUT:
		*ap->a_retval = MAX_INPUT;
		break;
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_VDISABLE:
		*ap->a_retval = _POSIX_VDISABLE;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * vop_print(int name, int *retval)
 *
 * This vop is handled directly by the kernel.
 */
static
int
user_vop_print (struct vop_print_args *ap)
{
	return(0);
}

/*
 * vop_readdir(struct vnode *vp, struct uio *uio, struct ucred *cred,
 *	       int *eofflag, int *ncookies, off_t **a_cookies)
 */
static
int
user_vop_readdir (struct vop_readdir_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_READDIR);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_readdir\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_READDIR|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_readlink(struct vnode *vp, struct uio *uio, struct ucred *cred)
 */
static
int
user_vop_readlink (struct vop_readlink_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_READLINK);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_readlink\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_READLINK|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_setattr(struct vnode *vp, struct vattr *vap, struct ucred *cred)
 */
static
int
user_vop_setattr (struct vop_setattr_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_SETATTR);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_setattr\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_SETATTR|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * user_vop_strategy() - I/O strategy routine.
 *
 * Note that userfs interfaces fake-up BMAP so the strategy call just
 * uses the passed bio instead of pushing a bio to get to the (faked)
 * device block cache.
 */
static void user_strategy_callback(struct slmsg *msg, void *arg, int error);

static
int
user_vop_strategy (struct vop_strategy_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct bio *bio;
	struct buf *bp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;
	bio = ap->a_bio;
	bp = bio->bio_buf;

	bio->bio_driver_info = ump;

	slmsg = syslink_kallocmsg();
	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
				      SLVFS_CMD_VOP_STRATEGY_READ);
		break;
	case BUF_CMD_WRITE:
		par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
				      SLVFS_CMD_VOP_STRATEGY_WRITE);
		break;
	default:
		par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
				      SLVFS_CMD_VOP_STRATEGY_MISC);
		break;
	}
	user_elm_push_vnode(par, vp);
	user_elm_push_offset(par, bio->bio_offset);
	user_elm_push_bio(par, bp->b_cmd, bp->b_bcount);
	syslink_kdmabuf_data(slmsg, bp->b_data, bp->b_bcount);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_strategy\n");
	error = syslink_ksendmsg(ump->sldesc, slmsg,
				 user_strategy_callback, bio);
	if (error)
		syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * This callback is made in the context of the responding process which
 * may or may not be the process the message was sent to.
 */
static void
user_strategy_callback(struct slmsg *slmsg, void *arg, int error)
{
	struct bio *bio = arg;
	struct buf *bp = bio->bio_buf;
	struct user_mount *ump;
	syslink_elm_t par;

	kprintf("user_strategy_callback\n");
	if (error == 0) {
		par = &slmsg->rep->msg->sm_head;
		if (par->se_cmd != (slmsg->msg->sm_head.se_cmd | SE_CMDF_REPLY)) {
			error = EBADRPC;
		}
	}
	if (error) {
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
	}
	ump = bio->bio_driver_info;
	syslink_kfreemsg(ump->sldesc, slmsg);
	biodone(bio);
}

/*
 * vop_bmap(struct vnode *vp, off_t loffset, off_t *doffsetp,
 *	    int *runp, int *runb)
 *
 * Dummy up the bmap op so the kernel will cluster I/Os.  The strategy
 * code will ignore the dummied up device block translation.
 */
static
int
user_vop_bmap(struct vop_bmap_args *ap)
{
	int cluster_off;

	*ap->a_doffsetp = ap->a_loffset;
	cluster_off = (int)(*ap->a_doffsetp & (MAXPHYS - 1));

	if (ap->a_runp)
		*ap->a_runp = MAXPHYS - cluster_off;
	if (ap->a_runb)
		*ap->a_runb = cluster_off;
	return(0);
}


/*
 * vop_ncreate(struct nchandle *nch, struct vnode *dvp, struct vnode **vpp,
 *	       struct ucred *cred, struct vattr *vap)
 */
static
int
user_vop_ncreate (struct vop_ncreate_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	syslink_elm_t elm;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ap->a_dvp;

	if ((error = vget(dvp, LK_SHARED)) != 0)
		return (error);

	ip = dvp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NCREATE);
	user_elm_push_nch(par, ap->a_nch);
	user_elm_push_vnode(par, dvp);
	user_elm_push_cred(par, ap->a_cred);
	user_elm_push_vattr(par, ap->a_vap);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_ncreate\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) != 0)
		goto done;
	par = &slmsg->rep->msg->sm_head;
	if (par->se_cmd != (SLVFS_CMD_VOP_NCREATE|SE_CMDF_REPLY)) {
		error = EBADRPC;
		goto done;
	}

	/*
	 * Parse reply - extract the inode number of the newly created
	 * object and construct a vnode using it.
	 */
	SL_FOREACH_ELEMENT(par, elm) {
		switch(elm->se_cmd) {
		case SLVFS_ELM_INUM:
			/* XXX */
			break;
		default:
			break;
		}
		if (error)
			break;
	}
	/* XXX construct vnode using fileid */
	error = EINVAL;

done:
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	vput(dvp);
	return(error);
}

/*
 * vop_nresolve(struct nchandle *nch, struct vnode *dvp, struct ucred *cred)
 */
static
int
user_vop_nresolve (struct vop_nresolve_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	syslink_elm_t elm;
	int error;
	int flags;
	ino_t inum;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ap->a_dvp;
	if ((error = vget(dvp, LK_SHARED)) != 0)
		return (error);
	vn_unlock(dvp);

	ip = dvp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NRESOLVE);
	user_elm_push_nch(par, ap->a_nch);
	user_elm_push_vnode(par, dvp);
	user_elm_push_cred(par, ap->a_cred);
	sl_msg_fini(slmsg->msg);

	/*
	 * Run the RPC.  The response must still be parsed for a ENOENT
	 * error to extract the whiteout flag.
	 */
	kprintf("userfs_nresolve\n");
	error = syslink_kdomsg(ump->sldesc, slmsg);
	if (error && error != ENOENT)
		goto done;
	par = &slmsg->rep->msg->sm_head;
	if (par->se_cmd != (SLVFS_CMD_VOP_NRESOLVE|SE_CMDF_REPLY)) {
		error = EBADRPC;
		goto done;
	}

	/*
	 * Parse reply - returns inode number of resolved vnode
	 */
	flags = 0;
	inum = 0;
	SL_FOREACH_ELEMENT(par, elm) {
		switch(elm->se_cmd) {
		case SLVFS_ELM_INUM:
			/* XXX */
			break;
		case SLVFS_ELM_NCPFLAG:
			/* flags = & NCF_WHITEOUT */
			break;
		default:
			break;
		}
	}

	if (error == 0) {
		error = EINVAL;
		/*vp = user_getvp(inum);*/
		/* XXX construct vp cache_setvp(nch, vp); */
	} else {
		ncp->nc_flag |= flags;
		cache_setvp(ap->a_nch, NULL);
	}
done:
	syslink_kfreemsg(ump->sldesc, slmsg);
	vrele(dvp);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nlookupdotdot(struct vnode *dvp, struct vnode **vpp, struct ucred *cred)
 *
 * Lookup the parent of dvp. dvp is ref'd but not locked.  The returned
 * vnode should be ref'd and locked.
 */
static
int
user_vop_nlookupdotdot (struct vop_nlookupdotdot_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	syslink_elm_t elm;
	int error;
	ino_t inum;

	cred = ap->a_cred;
	dvp = ap->a_dvp;
	vp = NULL;	/* XXX */
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NLOOKUPDOTDOT);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nlookupdotdot\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) != 0)
		goto done;
	par = &slmsg->rep->msg->sm_head;

	if (par->se_cmd != (SLVFS_CMD_VOP_NLOOKUPDOTDOT|SE_CMDF_REPLY)) {
		error = EBADRPC;
		goto done;
	}

	/*
	 * Parse reply - inumber of parent directory
	 */
	inum = 0;
	SL_FOREACH_ELEMENT(par, elm) {
		switch(elm->se_cmd) {
		case SLVFS_ELM_INUM:
			/* XXX */
			break;
		case SLVFS_ELM_NCPFLAG:
			/* flags = & NCF_WHITEOUT */
			break;
		default:
			break;
		}
	}

	/* construct parent vnode */

done:
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nlink(struct nchandle *nch, struct vnode *dvp, struct vnode *vp,
 *	     struct ucred *cred)
 */
static
int
user_vop_nlink (struct vop_nlink_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	vp = ap->a_vp;
	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NLINK);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nlink\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NLINK|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nmkdir(struct nchandle *nch, struct vnode *dvp, struct vnode **vpp,
 *	     struct ucred *cred, struct vattr *vap)
 */
static
int
user_vop_nmkdir (struct vop_nmkdir_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ap->a_dvp;
	if ((error = vget(dvp, LK_SHARED)) != 0)
		return (error);

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NMKDIR);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nmkdir\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NMKDIR|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	vput(dvp);
	return(error);
}

/*
 * vop_nmknod(struct nchandle *nch, struct vnode *dvp, struct vnode **vpp,
 *	     struct ucred *cred, struct vattr *vap)
 */
static
int
user_vop_nmknod (struct vop_nmknod_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ncp->nc_parent->nc_vp;	/* needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NMKNOD);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nmknod\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NMKNOD|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nremove(struct nchandle *nch, struct vnode *dvp, struct ucred *cred)
 */
static
int
user_vop_nremove (struct vop_nremove_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ncp->nc_parent->nc_vp;	/* needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NREMOVE);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nremove\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NREMOVE|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nrename(struct nchandle *fnch, struct nchandle *tnch,
 *	       struct vnode *fdvp, struct vnode *tdvp,
 *	       struct ucred *cred)
 */
static
int
user_vop_nrename (struct vop_nrename_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *fncp;
	struct namecache *tncp;
	struct ucred *cred;
	struct vnode *fdvp;
	struct vnode *tdvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	fncp = ap->a_fnch->ncp;
	fdvp = ap->a_fdvp;	/* XXX needs vget */
	tncp = ap->a_tnch->ncp;
	tdvp = ap->a_tdvp;	/* XXX needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NRENAME);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nrename\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NRENAME|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

/*
 * vop_nrmdir(struct nchandle *nch, struct vnode *dvp, struct ucred *cred)
 */
static
int
user_vop_nrmdir (struct vop_nrmdir_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ncp->nc_parent->nc_vp;	/* needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NRMDIR);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nrmdir\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NRMDIR|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

static
int
user_vop_nsymlink (struct vop_nsymlink_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ncp->nc_parent->nc_vp;	/* needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NSYMLINK);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nsymlink\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NSYMLINK|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

static
int
user_vop_nwhiteout (struct vop_nwhiteout_args *ap)
{
	struct user_mount *ump;
	struct user_inode *ip;
	struct namecache *ncp;
	struct ucred *cred;
	struct vnode *dvp;
	struct vnode *vp;
	struct slmsg *slmsg;
	syslink_elm_t par;
	int error;

	cred = ap->a_cred;
	ncp = ap->a_nch->ncp;
	dvp = ncp->nc_parent->nc_vp;	/* needs vget */

	vp = NULL;	/* XXX */

	ip = vp->v_data;
	ump = ip->ump;

	slmsg = syslink_kallocmsg();
	par = sl_msg_init_cmd(slmsg->msg, SMPROTO_BSDVFS,
			      SLVFS_CMD_VOP_NWHITEOUT);
	sl_msg_fini(slmsg->msg);

	kprintf("userfs_nwhiteout\n");
	if ((error = syslink_kdomsg(ump->sldesc, slmsg)) == 0) {
		par = &slmsg->rep->msg->sm_head;

		if (par->se_cmd == (SLVFS_CMD_VOP_NWHITEOUT|SE_CMDF_REPLY)) {
			;
		} else {
			error = EBADRPC;
		}
	}
	syslink_kfreemsg(ump->sldesc, slmsg);
	kprintf("error %d\n", error);
	return(error);
}

