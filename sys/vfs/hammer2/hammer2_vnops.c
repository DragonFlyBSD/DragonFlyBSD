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
#include <sys/dirent.h>

#include "hammer2.h"

/*
 * Last reference to a vnode is going away but it is still cached.
 */
static
int
hammer2_vop_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
#if 0
	struct hammer2_mount *hmp;
#endif

	kprintf("hammer2_inactive\n");

	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(vp);
		return (0);
	}

	return (0);
}

/*
 * Reclaim a vnode so that it can be reused; after the inode is
 * disassociated, the filesystem must manage it alone.
 */
static
int
hammer2_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct hammer2_inode *ip;
	struct hammer2_mount *hmp;

	kprintf("hammer2_reclaim\n");
	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL)
		return(0);

	hmp = ip->hmp;
	hammer2_inode_lock_ex(ip);
	vp->v_data = NULL;
	ip->vp = NULL;
	hammer2_inode_unlock_ex(ip);
	hammer2_chain_drop(hmp, &ip->chain);	/* vp ref removed */

	/*
	 * XXX handle background sync when ip dirty, kernel will no longer
	 * notify us regarding this inode because there is no longer a
	 * vnode attached to it.
	 */

	return (0);
}

static
int
hammer2_vop_fsync(struct vop_fsync_args *ap)
{
	kprintf("hammer2_fsync\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	kprintf("hammer2_access\n");
	return (0);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	hmp = ip->hmp;

	kprintf("hammer2_getattr iplock %p\n", &ip->chain.lk);

	hammer2_inode_lock_sh(ip);

	vap->va_fsid = hmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ip->ip_data.inum;
	vap->va_mode = ip->ip_data.mode;
	vap->va_nlink = ip->ip_data.nlinks;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->ip_data.size;
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = ip->ip_data.uflags;
	hammer2_time_to_timespec(ip->ip_data.ctime, &vap->va_ctime);
	hammer2_time_to_timespec(ip->ip_data.mtime, &vap->va_mtime);
	hammer2_time_to_timespec(ip->ip_data.mtime, &vap->va_atime);
	vap->va_gen = 1;
	vap->va_bytes = vap->va_size;
	vap->va_type = hammer2_get_vtype(ip);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ip->ip_data.uid;
	vap->va_gid_uuid = ip->ip_data.gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock_sh(ip);

	return (0);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
	hammer2_inode_t *xip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t lkey;
	struct uio *uio;
	off_t *cookies;
	off_t saveoff;
	int cookie_index;
	int ncookies;
	int error;
	int dtype;
	int r;

	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	uio = ap->a_uio;
	saveoff = uio->uio_offset;

	/*
	 * Setup cookies directory entry cookies if requested
	 */
	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
	} else {
		ncookies = -1;
		cookies = NULL;
	}
	cookie_index = 0;

	/*
	 * Handle artificial entries.  To ensure that only positive 64 bit
	 * quantities are returned to userland we always strip off bit 63.
	 * The hash code is designed such that codes 0x0000-0x7FFF are not
	 * used, allowing us to use these codes for articial entries.
	 *
	 * Entry 0 is used for '.' and entry 1 is used for '..'.  Do not
	 * allow '..' to cross the mount point into (e.g.) the super-root.
	 */
	error = 0;
	chain = (void *)(intptr_t)-1;	/* non-NULL early done means not eof */

	if (saveoff == 0) {
		r = vop_write_dirent(&error, uio,
				     ip->ip_data.inum &
					HAMMER2_DIRHASH_USERMSK,
				     DT_DIR, 1, ".");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}
	if (saveoff == 1) {
		if (ip->pip == NULL || ip == hmp->iroot)
			xip = ip;
		else
			xip = ip->pip;

		r = vop_write_dirent(&error, uio,
				     xip->ip_data.inum &
				      HAMMER2_DIRHASH_USERMSK,
				     DT_DIR, 2, "..");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}

	lkey = saveoff | HAMMER2_DIRHASH_VISIBLE;

	parent = &ip->chain;
	hammer2_chain_ref(hmp, parent);
	error = hammer2_chain_lock(hmp, parent);
	if (error) {
		hammer2_chain_put(hmp, parent);
		goto done;
	}
	chain = hammer2_chain_lookup(hmp, &parent, lkey, (hammer2_key_t)-1);
	while (chain) {
		/* XXX chain error */
		if (chain->bref.type != HAMMER2_BREF_TYPE_INODE)
			continue;
		dtype = hammer2_get_dtype(chain->u.ip);
		saveoff = chain->bref.key & HAMMER2_DIRHASH_USERMSK;
		r = vop_write_dirent(&error, uio,
				     chain->u.ip->ip_data.inum &
				      HAMMER2_DIRHASH_USERMSK,
				     dtype, chain->u.ip->ip_data.name_len,
				     chain->u.ip->ip_data.filename);
		if (r)
			break;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			break;

		chain = hammer2_chain_next(hmp, &parent, chain,
					   lkey, (hammer2_key_t)-1);
	}
	hammer2_chain_put(hmp, parent);
done:
	if (ap->a_eofflag)
		*ap->a_eofflag = (chain == NULL);
	uio->uio_offset = saveoff;
	if (error && cookie_index == 0) {
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
	return (error);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_write(struct vop_write_args *ap)
{
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	kprintf("hammer2_nresolve\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_bmap(struct vop_bmap_args *ap)
{
	kprintf("hammer2_bmap\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
	kprintf("hammer2_open\n");
	return vop_stdopen(ap);
}

static
int
hammer2_vop_strategy(struct vop_strategy_args *ap)
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
	case BUF_CMD_READ:
	case BUF_CMD_WRITE:
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(biop);
		break;
	}

	return (error);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
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
			rc = vfs_export(mp, &hmp->export,
					(const struct export_args *)ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}
	return (rc);
}

struct vop_ops hammer2_vnode_vops = {
	.vop_default	= vop_defaultop,
	.vop_fsync	= hammer2_vop_fsync,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_access	= hammer2_vop_access,
	.vop_getattr	= hammer2_vop_getattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_read	= hammer2_vop_read,
	.vop_write	= hammer2_vop_write,
	.vop_open	= hammer2_vop_open,
	.vop_inactive	= hammer2_vop_inactive,
	.vop_reclaim 	= hammer2_vop_reclaim,
	.vop_nresolve	= hammer2_vop_nresolve,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};
