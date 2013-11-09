/*
 * (MPSAFE)
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/reg.h>
#include <vm/vm_pager.h>
#include <vm/vm_zone.h>
#include <vm/vm_object.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/tty.h>
#include <sys/diskslice.h>
#include <sys/sysctl.h>
#include <sys/devfs.h>
#include <sys/pioctl.h>
#include <vfs/fifofs/fifo.h>

#include <machine/limits.h>

#include <sys/buf2.h>
#include <sys/sysref2.h>
#include <sys/mplock2.h>
#include <vm/vm_page2.h>

#ifndef SPEC_CHAIN_DEBUG
#define SPEC_CHAIN_DEBUG 0
#endif

MALLOC_DECLARE(M_DEVFS);
#define DEVFS_BADOP	(void *)devfs_vop_badop

static int devfs_vop_badop(struct vop_generic_args *);
static int devfs_vop_access(struct vop_access_args *);
static int devfs_vop_inactive(struct vop_inactive_args *);
static int devfs_vop_reclaim(struct vop_reclaim_args *);
static int devfs_vop_readdir(struct vop_readdir_args *);
static int devfs_vop_getattr(struct vop_getattr_args *);
static int devfs_vop_setattr(struct vop_setattr_args *);
static int devfs_vop_readlink(struct vop_readlink_args *);
static int devfs_vop_print(struct vop_print_args *);

static int devfs_vop_nresolve(struct vop_nresolve_args *);
static int devfs_vop_nlookupdotdot(struct vop_nlookupdotdot_args *);
static int devfs_vop_nmkdir(struct vop_nmkdir_args *);
static int devfs_vop_nsymlink(struct vop_nsymlink_args *);
static int devfs_vop_nrmdir(struct vop_nrmdir_args *);
static int devfs_vop_nremove(struct vop_nremove_args *);

static int devfs_spec_open(struct vop_open_args *);
static int devfs_spec_close(struct vop_close_args *);
static int devfs_spec_fsync(struct vop_fsync_args *);

static int devfs_spec_read(struct vop_read_args *);
static int devfs_spec_write(struct vop_write_args *);
static int devfs_spec_ioctl(struct vop_ioctl_args *);
static int devfs_spec_kqfilter(struct vop_kqfilter_args *);
static int devfs_spec_strategy(struct vop_strategy_args *);
static void devfs_spec_strategy_done(struct bio *);
static int devfs_spec_freeblks(struct vop_freeblks_args *);
static int devfs_spec_bmap(struct vop_bmap_args *);
static int devfs_spec_advlock(struct vop_advlock_args *);
static void devfs_spec_getpages_iodone(struct bio *);
static int devfs_spec_getpages(struct vop_getpages_args *);

static int devfs_fo_close(struct file *);
static int devfs_fo_read(struct file *, struct uio *, struct ucred *, int);
static int devfs_fo_write(struct file *, struct uio *, struct ucred *, int);
static int devfs_fo_stat(struct file *, struct stat *, struct ucred *);
static int devfs_fo_kqfilter(struct file *, struct knote *);
static int devfs_fo_ioctl(struct file *, u_long, caddr_t,
				struct ucred *, struct sysmsg *);
static __inline int sequential_heuristic(struct uio *, struct file *);

extern struct lock devfs_lock;

/*
 * devfs vnode operations for regular files.  All vnode ops are MPSAFE.
 */
struct vop_ops devfs_vnode_norm_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		devfs_vop_access,
	.vop_advlock =		DEVFS_BADOP,
	.vop_bmap =		DEVFS_BADOP,
	.vop_close =		vop_stdclose,
	.vop_getattr =		devfs_vop_getattr,
	.vop_inactive =		devfs_vop_inactive,
	.vop_ncreate =		DEVFS_BADOP,
	.vop_nresolve =		devfs_vop_nresolve,
	.vop_nlookupdotdot =	devfs_vop_nlookupdotdot,
	.vop_nlink =		DEVFS_BADOP,
	.vop_nmkdir =		devfs_vop_nmkdir,
	.vop_nmknod =		DEVFS_BADOP,
	.vop_nremove =		devfs_vop_nremove,
	.vop_nrename =		DEVFS_BADOP,
	.vop_nrmdir =		devfs_vop_nrmdir,
	.vop_nsymlink =		devfs_vop_nsymlink,
	.vop_open =		vop_stdopen,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		devfs_vop_print,
	.vop_read =		DEVFS_BADOP,
	.vop_readdir =		devfs_vop_readdir,
	.vop_readlink =		devfs_vop_readlink,
	.vop_reclaim =		devfs_vop_reclaim,
	.vop_setattr =		devfs_vop_setattr,
	.vop_write =		DEVFS_BADOP,
	.vop_ioctl =		DEVFS_BADOP
};

/*
 * devfs vnode operations for character devices.  All vnode ops are MPSAFE.
 */
struct vop_ops devfs_vnode_dev_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		devfs_vop_access,
	.vop_advlock =		devfs_spec_advlock,
	.vop_bmap =		devfs_spec_bmap,
	.vop_close =		devfs_spec_close,
	.vop_freeblks =		devfs_spec_freeblks,
	.vop_fsync =		devfs_spec_fsync,
	.vop_getattr =		devfs_vop_getattr,
	.vop_getpages =		devfs_spec_getpages,
	.vop_inactive =		devfs_vop_inactive,
	.vop_open =		devfs_spec_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		devfs_vop_print,
	.vop_kqfilter =		devfs_spec_kqfilter,
	.vop_read =		devfs_spec_read,
	.vop_readdir =		DEVFS_BADOP,
	.vop_readlink =		DEVFS_BADOP,
	.vop_reclaim =		devfs_vop_reclaim,
	.vop_setattr =		devfs_vop_setattr,
	.vop_strategy =		devfs_spec_strategy,
	.vop_write =		devfs_spec_write,
	.vop_ioctl =		devfs_spec_ioctl
};

/*
 * devfs file pointer operations.  All fileops are MPSAFE.
 */
struct vop_ops *devfs_vnode_dev_vops_p = &devfs_vnode_dev_vops;

struct fileops devfs_dev_fileops = {
	.fo_read	= devfs_fo_read,
	.fo_write	= devfs_fo_write,
	.fo_ioctl	= devfs_fo_ioctl,
	.fo_kqfilter	= devfs_fo_kqfilter,
	.fo_stat	= devfs_fo_stat,
	.fo_close	= devfs_fo_close,
	.fo_shutdown	= nofo_shutdown
};

/*
 * These two functions are possibly temporary hacks for devices (aka
 * the pty code) which want to control the node attributes themselves.
 *
 * XXX we may ultimately desire to simply remove the uid/gid/mode
 * from the node entirely.
 *
 * MPSAFE - sorta.  Theoretically the overwrite can compete since they
 *	    are loading from the same fields.
 */
static __inline void
node_sync_dev_get(struct devfs_node *node)
{
	cdev_t dev;

	if ((dev = node->d_dev) && (dev->si_flags & SI_OVERRIDE)) {
		node->uid = dev->si_uid;
		node->gid = dev->si_gid;
		node->mode = dev->si_perms;
	}
}

static __inline void
node_sync_dev_set(struct devfs_node *node)
{
	cdev_t dev;

	if ((dev = node->d_dev) && (dev->si_flags & SI_OVERRIDE)) {
		dev->si_uid = node->uid;
		dev->si_gid = node->gid;
		dev->si_perms = node->mode;
	}
}

/*
 * generic entry point for unsupported operations
 */
static int
devfs_vop_badop(struct vop_generic_args *ap)
{
	return (EIO);
}


static int
devfs_vop_access(struct vop_access_args *ap)
{
	struct devfs_node *node = DEVFS_NODE(ap->a_vp);
	int error;

	if (!devfs_node_is_accessible(node))
		return ENOENT;
	node_sync_dev_get(node);
	error = vop_helper_access(ap, node->uid, node->gid,
				  node->mode, node->flags);

	return error;
}


static int
devfs_vop_inactive(struct vop_inactive_args *ap)
{
	struct devfs_node *node = DEVFS_NODE(ap->a_vp);

	if (node == NULL || (node->flags & DEVFS_NODE_LINKED) == 0)
		vrecycle(ap->a_vp);
	return 0;
}


static int
devfs_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct devfs_node *node;
	struct vnode *vp;
	int locked;

	/*
	 * Check if it is locked already. if not, we acquire the devfs lock
	 */
	if ((lockstatus(&devfs_lock, curthread)) != LK_EXCLUSIVE) {
		lockmgr(&devfs_lock, LK_EXCLUSIVE);
		locked = 1;
	} else {
		locked = 0;
	}

	/*
	 * Get rid of the devfs_node if it is no longer linked into the
	 * topology.
	 */
	vp = ap->a_vp;
	if ((node = DEVFS_NODE(vp)) != NULL) {
		node->v_node = NULL;
		if ((node->flags & DEVFS_NODE_LINKED) == 0)
			devfs_freep(node);
	}

	if (locked)
		lockmgr(&devfs_lock, LK_RELEASE);

	/*
	 * v_rdev needs to be properly released using v_release_rdev
	 * Make sure v_data is NULL as well.
	 */
	vp->v_data = NULL;
	v_release_rdev(vp);
	return 0;
}


static int
devfs_vop_readdir(struct vop_readdir_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_vp);
	struct devfs_node *node;
	int cookie_index;
	int ncookies;
	int error2;
	int error;
	int r;
	off_t *cookies;
	off_t saveoff;

	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_readdir() called!\n");

	if (ap->a_uio->uio_offset < 0 || ap->a_uio->uio_offset > INT_MAX)
		return (EINVAL);
	if ((error = vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		return (error);

	if (!devfs_node_is_accessible(dnode)) {
		vn_unlock(ap->a_vp);
		return ENOENT;
	}

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	saveoff = ap->a_uio->uio_offset;

	if (ap->a_ncookies) {
		ncookies = ap->a_uio->uio_resid / 16 + 1; /* Why / 16 ?? */
		if (ncookies > 256)
			ncookies = 256;
		cookies = kmalloc(256 * sizeof(off_t), M_TEMP, M_WAITOK);
		cookie_index = 0;
	} else {
		ncookies = -1;
		cookies = NULL;
		cookie_index = 0;
	}

	nanotime(&dnode->atime);

	if (saveoff == 0) {
		r = vop_write_dirent(&error, ap->a_uio, dnode->d_dir.d_ino,
				     DT_DIR, 1, ".");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		saveoff++;
		cookie_index++;
		if (cookie_index == ncookies)
			goto done;
	}

	if (saveoff == 1) {
		if (dnode->parent) {
			r = vop_write_dirent(&error, ap->a_uio,
					     dnode->parent->d_dir.d_ino,
					     DT_DIR, 2, "..");
		} else {
			r = vop_write_dirent(&error, ap->a_uio,
					     dnode->d_dir.d_ino,
					     DT_DIR, 2, "..");
		}
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		saveoff++;
		cookie_index++;
		if (cookie_index == ncookies)
			goto done;
	}

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(dnode), link) {
		if ((node->flags & DEVFS_HIDDEN) ||
		    (node->flags & DEVFS_INVISIBLE)) {
			continue;
		}

		/*
		 * If the node type is a valid devfs alias, then we make
		 * sure that the target isn't hidden. If it is, we don't
		 * show the link in the directory listing.
		 */
		if ((node->node_type == Nlink) && (node->link_target != NULL) &&
			(node->link_target->flags & DEVFS_HIDDEN))
			continue;

		if (node->cookie < saveoff)
			continue;

		saveoff = node->cookie;

		error2 = vop_write_dirent(&error, ap->a_uio, node->d_dir.d_ino,
					  node->d_dir.d_type,
					  node->d_dir.d_namlen,
					  node->d_dir.d_name);

		if (error2)
			break;

		saveoff++;

		if (cookies)
			cookies[cookie_index] = node->cookie;
		++cookie_index;
		if (cookie_index == ncookies)
			break;
	}

done:
	lockmgr(&devfs_lock, LK_RELEASE);
	vn_unlock(ap->a_vp);

	ap->a_uio->uio_offset = saveoff;
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


static int
devfs_vop_nresolve(struct vop_nresolve_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);
	struct devfs_node *node, *found = NULL;
	struct namecache *ncp;
	struct vnode *vp = NULL;
	int error = 0;
	int len;
	int depth;

	ncp = ap->a_nch->ncp;
	len = ncp->nc_nlen;

	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	if ((dnode->node_type != Nroot) && (dnode->node_type != Ndir)) {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
		goto out;
	}

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(dnode), link) {
		if (len == node->d_dir.d_namlen) {
			if (!memcmp(ncp->nc_name, node->d_dir.d_name, len)) {
				found = node;
				break;
			}
		}
	}

	if (found) {
		depth = 0;
		while ((found->node_type == Nlink) && (found->link_target)) {
			if (depth >= 8) {
				devfs_debug(DEVFS_DEBUG_SHOW, "Recursive link or depth >= 8");
				break;
			}

			found = found->link_target;
			++depth;
		}

		if (!(found->flags & DEVFS_HIDDEN))
			devfs_allocv(/*ap->a_dvp->v_mount, */ &vp, found);
	}

	if (vp == NULL) {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
		goto out;

	}
	KKASSERT(vp);
	vn_unlock(vp);
	cache_setvp(ap->a_nch, vp);
	vrele(vp);
out:
	lockmgr(&devfs_lock, LK_RELEASE);

	return error;
}


static int
devfs_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);

	*ap->a_vpp = NULL;
	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	if (dnode->parent != NULL) {
		devfs_allocv(ap->a_vpp, dnode->parent);
		vn_unlock(*ap->a_vpp);
	}
	lockmgr(&devfs_lock, LK_RELEASE);

	return ((*ap->a_vpp == NULL) ? ENOENT : 0);
}


static int
devfs_vop_getattr(struct vop_getattr_args *ap)
{
	struct devfs_node *node = DEVFS_NODE(ap->a_vp);
	struct vattr *vap = ap->a_vap;
	struct partinfo pinfo;
	int error = 0;

#if 0
	if (!devfs_node_is_accessible(node))
		return ENOENT;
#endif
	node_sync_dev_get(node);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	/* start by zeroing out the attributes */
	VATTR_NULL(vap);

	/* next do all the common fields */
	vap->va_type = ap->a_vp->v_type;
	vap->va_mode = node->mode;
	vap->va_fileid = DEVFS_NODE(ap->a_vp)->d_dir.d_ino ;
	vap->va_flags = 0;
	vap->va_blocksize = DEV_BSIZE;
	vap->va_bytes = vap->va_size = 0;

	vap->va_fsid = ap->a_vp->v_mount->mnt_stat.f_fsid.val[0];

	vap->va_atime = node->atime;
	vap->va_mtime = node->mtime;
	vap->va_ctime = node->ctime;

	vap->va_nlink = 1; /* number of references to file */

	vap->va_uid = node->uid;
	vap->va_gid = node->gid;

	vap->va_rmajor = 0;
	vap->va_rminor = 0;

	if ((node->node_type == Ndev) && node->d_dev)  {
		reference_dev(node->d_dev);
		vap->va_rminor = node->d_dev->si_uminor;
		release_dev(node->d_dev);
	}

	/* For a softlink the va_size is the length of the softlink */
	if (node->symlink_name != 0) {
		vap->va_bytes = vap->va_size = node->symlink_namelen;
	}

	/*
	 * For a disk-type device, va_size is the size of the underlying
	 * device, so that lseek() works properly.
	 */
	if ((node->d_dev) && (dev_dflags(node->d_dev) & D_DISK)) {
		bzero(&pinfo, sizeof(pinfo));
		error = dev_dioctl(node->d_dev, DIOCGPART, (void *)&pinfo,
				   0, proc0.p_ucred, NULL);
		if ((error == 0) && (pinfo.media_blksize != 0)) {
			vap->va_size = pinfo.media_size;
		} else {
			vap->va_size = 0;
			error = 0;
		}
	}

	lockmgr(&devfs_lock, LK_RELEASE);

	return (error);
}


static int
devfs_vop_setattr(struct vop_setattr_args *ap)
{
	struct devfs_node *node = DEVFS_NODE(ap->a_vp);
	struct vattr *vap;
	uid_t cur_uid;
	gid_t cur_gid;
	mode_t cur_mode;
	int error = 0;

	if (!devfs_node_is_accessible(node))
		return ENOENT;
	node_sync_dev_get(node);

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	vap = ap->a_vap;

	if ((vap->va_uid != (uid_t)VNOVAL) || (vap->va_gid != (gid_t)VNOVAL)) {
		cur_uid = node->uid;
		cur_gid = node->gid;
		cur_mode = node->mode;
		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
		    ap->a_cred, &cur_uid, &cur_gid, &cur_mode);
		if (error)
			goto out;

		if (node->uid != cur_uid || node->gid != cur_gid) {
			node->uid = cur_uid;
			node->gid = cur_gid;
			node->mode = cur_mode;
		}
	}

	if (vap->va_mode != (mode_t)VNOVAL) {
		cur_mode = node->mode;
		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
		    node->uid, node->gid, &cur_mode);
		if (error == 0 && node->mode != cur_mode) {
			node->mode = cur_mode;
		}
	}

out:
	node_sync_dev_set(node);
	nanotime(&node->ctime);
	lockmgr(&devfs_lock, LK_RELEASE);

	return error;
}


static int
devfs_vop_readlink(struct vop_readlink_args *ap)
{
	struct devfs_node *node = DEVFS_NODE(ap->a_vp);
	int ret;

	if (!devfs_node_is_accessible(node))
		return ENOENT;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	ret = uiomove(node->symlink_name, node->symlink_namelen, ap->a_uio);
	lockmgr(&devfs_lock, LK_RELEASE);

	return ret;
}


static int
devfs_vop_print(struct vop_print_args *ap)
{
	return (0);
}

static int
devfs_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);
	struct devfs_node *node;

	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	if ((dnode->node_type != Nroot) && (dnode->node_type != Ndir))
		goto out;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	devfs_allocvp(ap->a_dvp->v_mount, ap->a_vpp, Ndir,
		      ap->a_nch->ncp->nc_name, dnode, NULL);

	if (*ap->a_vpp) {
		node = DEVFS_NODE(*ap->a_vpp);
		node->flags |= DEVFS_USER_CREATED;
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	lockmgr(&devfs_lock, LK_RELEASE);
out:
	return ((*ap->a_vpp == NULL) ? ENOTDIR : 0);
}

static int
devfs_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);
	struct devfs_node *node;
	size_t targetlen;

	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	ap->a_vap->va_type = VLNK;

	if ((dnode->node_type != Nroot) && (dnode->node_type != Ndir))
		goto out;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);
	devfs_allocvp(ap->a_dvp->v_mount, ap->a_vpp, Nlink,
		      ap->a_nch->ncp->nc_name, dnode, NULL);

	targetlen = strlen(ap->a_target);
	if (*ap->a_vpp) {
		node = DEVFS_NODE(*ap->a_vpp);
		node->flags |= DEVFS_USER_CREATED;
		node->symlink_namelen = targetlen;
		node->symlink_name = kmalloc(targetlen + 1, M_DEVFS, M_WAITOK);
		memcpy(node->symlink_name, ap->a_target, targetlen);
		node->symlink_name[targetlen] = '\0';
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	lockmgr(&devfs_lock, LK_RELEASE);
out:
	return ((*ap->a_vpp == NULL) ? ENOTDIR : 0);
}

static int
devfs_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);
	struct devfs_node *node;
	struct namecache *ncp;
	int error = ENOENT;

	ncp = ap->a_nch->ncp;

	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	if ((dnode->node_type != Nroot) && (dnode->node_type != Ndir))
		goto out;

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(dnode), link) {
		if (ncp->nc_nlen != node->d_dir.d_namlen)
			continue;
		if (memcmp(ncp->nc_name, node->d_dir.d_name, ncp->nc_nlen))
			continue;

		/*
		 * only allow removal of user created dirs
		 */
		if ((node->flags & DEVFS_USER_CREATED) == 0) {
			error = EPERM;
			goto out;
		} else if (node->node_type != Ndir) {
			error = ENOTDIR;
			goto out;
		} else if (node->nchildren > 2) {
			error = ENOTEMPTY;
			goto out;
		} else {
			if (node->v_node)
				cache_inval_vp(node->v_node, CINV_DESTROY);
			devfs_unlinkp(node);
			error = 0;
			break;
		}
	}

	cache_unlink(ap->a_nch);
out:
	lockmgr(&devfs_lock, LK_RELEASE);
	return error;
}

static int
devfs_vop_nremove(struct vop_nremove_args *ap)
{
	struct devfs_node *dnode = DEVFS_NODE(ap->a_dvp);
	struct devfs_node *node;
	struct namecache *ncp;
	int error = ENOENT;

	ncp = ap->a_nch->ncp;

	if (!devfs_node_is_accessible(dnode))
		return ENOENT;

	lockmgr(&devfs_lock, LK_EXCLUSIVE);

	if ((dnode->node_type != Nroot) && (dnode->node_type != Ndir))
		goto out;

	TAILQ_FOREACH(node, DEVFS_DENODE_HEAD(dnode), link) {
		if (ncp->nc_nlen != node->d_dir.d_namlen)
			continue;
		if (memcmp(ncp->nc_name, node->d_dir.d_name, ncp->nc_nlen))
			continue;

		/*
		 * only allow removal of user created stuff (e.g. symlinks)
		 */
		if ((node->flags & DEVFS_USER_CREATED) == 0) {
			error = EPERM;
			goto out;
		} else if (node->node_type == Ndir) {
			error = EISDIR;
			goto out;
		} else {
			if (node->v_node)
				cache_inval_vp(node->v_node, CINV_DESTROY);
			devfs_unlinkp(node);
			error = 0;
			break;
		}
	}

	cache_unlink(ap->a_nch);
out:
	lockmgr(&devfs_lock, LK_RELEASE);
	return error;
}


static int
devfs_spec_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *orig_vp = NULL;
	struct devfs_node *node = DEVFS_NODE(vp);
	struct devfs_node *newnode;
	cdev_t dev, ndev = NULL;
	int error = 0;

	if (node) {
		if (node->d_dev == NULL)
			return ENXIO;
		if (!devfs_node_is_accessible(node))
			return ENOENT;
	}

	if ((dev = vp->v_rdev) == NULL)
		return ENXIO;

	vn_lock(vp, LK_UPGRADE | LK_RETRY);

	if (node && ap->a_fp) {
		devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_spec_open: -1.1-\n");
		lockmgr(&devfs_lock, LK_EXCLUSIVE);

		ndev = devfs_clone(dev, node->d_dir.d_name,
				   node->d_dir.d_namlen,
				   ap->a_mode, ap->a_cred);
		if (ndev != NULL) {
			newnode = devfs_create_device_node(
					DEVFS_MNTDATA(vp->v_mount)->root_node,
					ndev, NULL, NULL);
			/* XXX: possibly destroy device if this happens */

			if (newnode != NULL) {
				dev = ndev;
				devfs_link_dev(dev);

				devfs_debug(DEVFS_DEBUG_DEBUG,
						"parent here is: %s, node is: |%s|\n",
						((node->parent->node_type == Nroot) ?
						"ROOT!" : node->parent->d_dir.d_name),
						newnode->d_dir.d_name);
				devfs_debug(DEVFS_DEBUG_DEBUG,
						"test: %s\n",
						((struct devfs_node *)(TAILQ_LAST(DEVFS_DENODE_HEAD(node->parent), devfs_node_head)))->d_dir.d_name);

				/*
				 * orig_vp is set to the original vp if we cloned.
				 */
				/* node->flags |= DEVFS_CLONED; */
				devfs_allocv(&vp, newnode);
				orig_vp = ap->a_vp;
				ap->a_vp = vp;
			}
		}
		lockmgr(&devfs_lock, LK_RELEASE);
	}

	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "devfs_spec_open() called on %s! \n",
		    dev->si_name);

	/*
	 * Make this field valid before any I/O in ->d_open
	 */
	if (!dev->si_iosize_max)
		/* XXX: old DFLTPHYS == 64KB dependency */
		dev->si_iosize_max = min(MAXPHYS,64*1024);

	if (dev_dflags(dev) & D_TTY)
		vsetflags(vp, VISTTY);

	/*
	 * Open underlying device
	 */
	vn_unlock(vp);
	error = dev_dopen(dev, ap->a_mode, S_IFCHR, ap->a_cred);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * Clean up any cloned vp if we error out.
	 */
	if (error) {
		if (orig_vp) {
			vput(vp);
			ap->a_vp = orig_vp;
			/* orig_vp = NULL; */
		}
		return error;
	}

	/*
	 * This checks if the disk device is going to be opened for writing.
	 * It will be only allowed in the cases where securelevel permits it
	 * and it's not mounted R/W.
	 */
	if ((dev_dflags(dev) & D_DISK) && (ap->a_mode & FWRITE) &&
	    (ap->a_cred != FSCRED)) {

		/* Very secure mode. No open for writing allowed */
		if (securelevel >= 2)
			return EPERM;

		/*
		 * If it is mounted R/W, do not allow to open for writing.
		 * In the case it's mounted read-only but securelevel
		 * is >= 1, then do not allow opening for writing either.
		 */
		if (vfs_mountedon(vp)) {
			if (!(dev->si_mountpoint->mnt_flag & MNT_RDONLY))
				return EBUSY;
			else if (securelevel >= 1)
				return EPERM;
		}
	}

	if (dev_dflags(dev) & D_TTY) {
		if (dev->si_tty) {
			struct tty *tp;
			tp = dev->si_tty;
			if (!tp->t_stop) {
				devfs_debug(DEVFS_DEBUG_DEBUG,
					    "devfs: no t_stop\n");
				tp->t_stop = nottystop;
			}
		}
	}


	if (vn_isdisk(vp, NULL)) {
		if (!dev->si_bsize_phys)
			dev->si_bsize_phys = DEV_BSIZE;
		vinitvmio(vp, IDX_TO_OFF(INT_MAX), PAGE_SIZE, -1);
	}

	vop_stdopen(ap);
#if 0
	if (node)
		nanotime(&node->atime);
#endif

	/*
	 * If we replaced the vp the vop_stdopen() call will have loaded
	 * it into fp->f_data and vref()d the vp, giving us two refs.  So
	 * instead of just unlocking it here we have to vput() it.
	 */
	if (orig_vp)
		vput(vp);

	/* Ugly pty magic, to make pty devices appear once they are opened */
	if (node && (node->flags & DEVFS_PTY) == DEVFS_PTY)
		node->flags &= ~DEVFS_INVISIBLE;

	if (ap->a_fp) {
		KKASSERT(ap->a_fp->f_type == DTYPE_VNODE);
		KKASSERT((ap->a_fp->f_flag & FMASK) == (ap->a_mode & FMASK));
		ap->a_fp->f_ops = &devfs_dev_fileops;
		KKASSERT(ap->a_fp->f_data == (void *)vp);
	}

	return 0;
}

static int
devfs_spec_close(struct vop_close_args *ap)
{
	struct devfs_node *node;
	struct proc *p = curproc;
	struct vnode *vp = ap->a_vp;
	cdev_t dev = vp->v_rdev;
	int error = 0;
	int needrelock;

	/*
	 * We do special tests on the opencount so unfortunately we need
	 * an exclusive lock.
	 */
	vn_lock(vp, LK_UPGRADE | LK_RETRY);

	if (dev)
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "devfs_spec_close() called on %s! \n",
			    dev->si_name);
	else
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "devfs_spec_close() called, null vode!\n");

	/*
	 * A couple of hacks for devices and tty devices.  The
	 * vnode ref count cannot be used to figure out the
	 * last close, but we can use v_opencount now that
	 * revoke works properly.
	 *
	 * Detect the last close on a controlling terminal and clear
	 * the session (half-close).
	 */
	if (dev)
		reference_dev(dev);

	if (p && vp->v_opencount <= 1 && vp == p->p_session->s_ttyvp) {
		p->p_session->s_ttyvp = NULL;
		vrele(vp);
	}

	/*
	 * Vnodes can be opened and closed multiple times.  Do not really
	 * close the device unless (1) it is being closed forcibly,
	 * (2) the device wants to track closes, or (3) this is the last
	 * vnode doing its last close on the device.
	 *
	 * XXX the VXLOCK (force close) case can leave vnodes referencing
	 * a closed device.  This might not occur now that our revoke is
	 * fixed.
	 */
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_spec_close() -1- \n");
	if (dev && ((vp->v_flag & VRECLAIMED) ||
	    (dev_dflags(dev) & D_TRACKCLOSE) ||
	    (vp->v_opencount == 1))) {
		/*
		 * Ugly pty magic, to make pty devices disappear again once
		 * they are closed.
		 */
		node = DEVFS_NODE(ap->a_vp);
		if (node && (node->flags & DEVFS_PTY))
			node->flags |= DEVFS_INVISIBLE;

		/*
		 * Unlock around dev_dclose(), unless the vnode is
		 * undergoing a vgone/reclaim (during umount).
		 */
		needrelock = 0;
		if ((vp->v_flag & VRECLAIMED) == 0 && vn_islocked(vp)) {
			needrelock = 1;
			vn_unlock(vp);
		}

		/*
		 * WARNING!  If the device destroys itself the devfs node
		 *	     can disappear here.
		 *
		 * WARNING!  vn_lock() will fail if the vp is in a VRECLAIM,
		 *	     which can occur during umount.
		 */
		error = dev_dclose(dev, ap->a_fflag, S_IFCHR);
		/* node is now stale */

		if (needrelock) {
			if (vn_lock(vp, LK_EXCLUSIVE | LK_RETRY) != 0) {
				panic("devfs_spec_close: vnode %p "
				      "unexpectedly could not be relocked",
				      vp);
			}
		}
	} else {
		error = 0;
	}
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_spec_close() -2- \n");

	/*
	 * Track the actual opens and closes on the vnode.  The last close
	 * disassociates the rdev.  If the rdev is already disassociated or
	 * the opencount is already 0, the vnode might have been revoked
	 * and no further opencount tracking occurs.
	 */
	if (dev)
		release_dev(dev);
	if (vp->v_opencount > 0)
		vop_stdclose(ap);
	return(error);

}


static int
devfs_fo_close(struct file *fp)
{
	struct vnode *vp = (struct vnode *)fp->f_data;
	int error;

	fp->f_ops = &badfileops;
	error = vn_close(vp, fp->f_flag);

	return (error);
}


/*
 * Device-optimized file table vnode read routine.
 *
 * This bypasses the VOP table and talks directly to the device.  Most
 * filesystems just route to specfs and can make this optimization.
 *
 * MPALMOSTSAFE - acquires mplock
 */
static int
devfs_fo_read(struct file *fp, struct uio *uio,
		 struct ucred *cred, int flags)
{
	struct devfs_node *node;
	struct vnode *vp;
	int ioflag;
	int error;
	cdev_t dev;

	KASSERT(uio->uio_td == curthread,
		("uio_td %p is not td %p", uio->uio_td, curthread));

	if (uio->uio_resid == 0)
		return 0;

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD)
		return EBADF;

	node = DEVFS_NODE(vp);

	if ((dev = vp->v_rdev) == NULL)
		return EBADF;

	reference_dev(dev);

	if ((flags & O_FOFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	ioflag = 0;
	if (flags & O_FBLOCKING) {
		/* ioflag &= ~IO_NDELAY; */
	} else if (flags & O_FNONBLOCKING) {
		ioflag |= IO_NDELAY;
	} else if (fp->f_flag & FNONBLOCK) {
		ioflag |= IO_NDELAY;
	}
	if (flags & O_FBUFFERED) {
		/* ioflag &= ~IO_DIRECT; */
	} else if (flags & O_FUNBUFFERED) {
		ioflag |= IO_DIRECT;
	} else if (fp->f_flag & O_DIRECT) {
		ioflag |= IO_DIRECT;
	}
	ioflag |= sequential_heuristic(uio, fp);

	error = dev_dread(dev, uio, ioflag);

	release_dev(dev);
	if (node)
		nanotime(&node->atime);
	if ((flags & O_FOFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;

	return (error);
}


static int
devfs_fo_write(struct file *fp, struct uio *uio,
		  struct ucred *cred, int flags)
{
	struct devfs_node *node;
	struct vnode *vp;
	int ioflag;
	int error;
	cdev_t dev;

	KASSERT(uio->uio_td == curthread,
		("uio_td %p is not p %p", uio->uio_td, curthread));

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD)
		return EBADF;

	node = DEVFS_NODE(vp);

	if (vp->v_type == VREG)
		bwillwrite(uio->uio_resid);

	vp = (struct vnode *)fp->f_data;

	if ((dev = vp->v_rdev) == NULL)
		return EBADF;

	reference_dev(dev);

	if ((flags & O_FOFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	ioflag = IO_UNIT;
	if (vp->v_type == VREG &&
	   ((fp->f_flag & O_APPEND) || (flags & O_FAPPEND))) {
		ioflag |= IO_APPEND;
	}

	if (flags & O_FBLOCKING) {
		/* ioflag &= ~IO_NDELAY; */
	} else if (flags & O_FNONBLOCKING) {
		ioflag |= IO_NDELAY;
	} else if (fp->f_flag & FNONBLOCK) {
		ioflag |= IO_NDELAY;
	}
	if (flags & O_FBUFFERED) {
		/* ioflag &= ~IO_DIRECT; */
	} else if (flags & O_FUNBUFFERED) {
		ioflag |= IO_DIRECT;
	} else if (fp->f_flag & O_DIRECT) {
		ioflag |= IO_DIRECT;
	}
	if (flags & O_FASYNCWRITE) {
		/* ioflag &= ~IO_SYNC; */
	} else if (flags & O_FSYNCWRITE) {
		ioflag |= IO_SYNC;
	} else if (fp->f_flag & O_FSYNC) {
		ioflag |= IO_SYNC;
	}

	if (vp->v_mount && (vp->v_mount->mnt_flag & MNT_SYNCHRONOUS))
		ioflag |= IO_SYNC;
	ioflag |= sequential_heuristic(uio, fp);

	error = dev_dwrite(dev, uio, ioflag);

	release_dev(dev);
	if (node) {
		nanotime(&node->atime);
		nanotime(&node->mtime);
	}

	if ((flags & O_FOFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;

	return (error);
}


static int
devfs_fo_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
	struct vnode *vp;
	struct vattr vattr;
	struct vattr *vap;
	u_short mode;
	cdev_t dev;
	int error;

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD)
		return EBADF;

	error = vn_stat(vp, sb, cred);
	if (error)
		return (error);

	vap = &vattr;
	error = VOP_GETATTR(vp, vap);
	if (error)
		return (error);

	/*
	 * Zero the spare stat fields
	 */
	sb->st_lspare = 0;
	sb->st_qspare1 = 0;
	sb->st_qspare2 = 0;

	/*
	 * Copy from vattr table ... or not in case it's a cloned device
	 */
	if (vap->va_fsid != VNOVAL)
		sb->st_dev = vap->va_fsid;
	else
		sb->st_dev = vp->v_mount->mnt_stat.f_fsid.val[0];

	sb->st_ino = vap->va_fileid;

	mode = vap->va_mode;
	mode |= S_IFCHR;
	sb->st_mode = mode;

	if (vap->va_nlink > (nlink_t)-1)
		sb->st_nlink = (nlink_t)-1;
	else
		sb->st_nlink = vap->va_nlink;

	sb->st_uid = vap->va_uid;
	sb->st_gid = vap->va_gid;
	sb->st_rdev = dev2udev(DEVFS_NODE(vp)->d_dev);
	sb->st_size = vap->va_bytes;
	sb->st_atimespec = vap->va_atime;
	sb->st_mtimespec = vap->va_mtime;
	sb->st_ctimespec = vap->va_ctime;

	/*
	 * A VCHR and VBLK device may track the last access and last modified
	 * time independantly of the filesystem.  This is particularly true
	 * because device read and write calls may bypass the filesystem.
	 */
	if (vp->v_type == VCHR || vp->v_type == VBLK) {
		dev = vp->v_rdev;
		if (dev != NULL) {
			if (dev->si_lastread) {
				sb->st_atimespec.tv_sec = time_second +
							  (time_uptime -
							   dev->si_lastread);
				sb->st_atimespec.tv_nsec = 0;
			}
			if (dev->si_lastwrite) {
				sb->st_atimespec.tv_sec = time_second +
							  (time_uptime -
							   dev->si_lastwrite);
				sb->st_atimespec.tv_nsec = 0;
			}
		}
	}

        /*
	 * According to www.opengroup.org, the meaning of st_blksize is
	 *   "a filesystem-specific preferred I/O block size for this
	 *    object.  In some filesystem types, this may vary from file
	 *    to file"
	 * Default to PAGE_SIZE after much discussion.
	 */

	sb->st_blksize = PAGE_SIZE;

	sb->st_flags = vap->va_flags;

	error = priv_check_cred(cred, PRIV_VFS_GENERATION, 0);
	if (error)
		sb->st_gen = 0;
	else
		sb->st_gen = (u_int32_t)vap->va_gen;

	sb->st_blocks = vap->va_bytes / S_BLKSIZE;

	return (0);
}


static int
devfs_fo_kqfilter(struct file *fp, struct knote *kn)
{
	struct vnode *vp;
	int error;
	cdev_t dev;

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD) {
		error = EBADF;
		goto done;
	}
	if ((dev = vp->v_rdev) == NULL) {
		error = EBADF;
		goto done;
	}
	reference_dev(dev);

	error = dev_dkqfilter(dev, kn);

	release_dev(dev);

done:
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
devfs_fo_ioctl(struct file *fp, u_long com, caddr_t data,
		  struct ucred *ucred, struct sysmsg *msg)
{
#if 0
	struct devfs_node *node;
#endif
	struct vnode *vp;
	struct vnode *ovp;
	cdev_t	dev;
	int error;
	struct fiodname_args *name_args;
	size_t namlen;
	const char *name;

	vp = ((struct vnode *)fp->f_data);

	if ((dev = vp->v_rdev) == NULL)
		return EBADF;		/* device was revoked */

	reference_dev(dev);

#if 0
	node = DEVFS_NODE(vp);
#endif

	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "devfs_fo_ioctl() called! for dev %s\n",
		    dev->si_name);

	if (com == FIODTYPE) {
		*(int *)data = dev_dflags(dev) & D_TYPEMASK;
		error = 0;
		goto out;
	} else if (com == FIODNAME) {
		name_args = (struct fiodname_args *)data;
		name = dev->si_name;
		namlen = strlen(name) + 1;

		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "ioctl, got: FIODNAME for %s\n", name);

		if (namlen <= name_args->len)
			error = copyout(dev->si_name, name_args->name, namlen);
		else
			error = EINVAL;

		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "ioctl stuff: error: %d\n", error);
		goto out;
	}

	error = dev_dioctl(dev, com, data, fp->f_flag, ucred, msg);

#if 0
	if (node) {
		nanotime(&node->atime);
		nanotime(&node->mtime);
	}
#endif
	if (com == TIOCSCTTY) {
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "devfs_fo_ioctl: got TIOCSCTTY on %s\n",
			    dev->si_name);
	}
	if (error == 0 && com == TIOCSCTTY) {
		struct proc *p = curthread->td_proc;
		struct session *sess;

		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "devfs_fo_ioctl: dealing with TIOCSCTTY on %s\n",
			    dev->si_name);
		if (p == NULL) {
			error = ENOTTY;
			goto out;
		}
		sess = p->p_session;

		/*
		 * Do nothing if reassigning same control tty
		 */
		if (sess->s_ttyvp == vp) {
			error = 0;
			goto out;
		}

		/*
		 * Get rid of reference to old control tty
		 */
		ovp = sess->s_ttyvp;
		vref(vp);
		sess->s_ttyvp = vp;
		if (ovp)
			vrele(ovp);
	}

out:
	release_dev(dev);
	devfs_debug(DEVFS_DEBUG_DEBUG, "devfs_fo_ioctl() finished! \n");
	return (error);
}


static int
devfs_spec_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error;

	if (!vn_isdisk(vp, NULL))
		return (0);

	/*
	 * Flush all dirty buffers associated with a block device.
	 */
	error = vfsync(vp, ap->a_waitfor, 10000, NULL, NULL);
	return (error);
}

static int
devfs_spec_read(struct vop_read_args *ap)
{
	struct devfs_node *node;
	struct vnode *vp;
	struct uio *uio;
	cdev_t dev;
	int error;

	vp = ap->a_vp;
	dev = vp->v_rdev;
	uio = ap->a_uio;
	node = DEVFS_NODE(vp);

	if (dev == NULL)		/* device was revoked */
		return (EBADF);
	if (uio->uio_resid == 0)
		return (0);

	vn_unlock(vp);
	error = dev_dread(dev, uio, ap->a_ioflag);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	if (node)
		nanotime(&node->atime);

	return (error);
}

/*
 * Vnode op for write
 *
 * spec_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *	      struct ucred *a_cred)
 */
static int
devfs_spec_write(struct vop_write_args *ap)
{
	struct devfs_node *node;
	struct vnode *vp;
	struct uio *uio;
	cdev_t dev;
	int error;

	vp = ap->a_vp;
	dev = vp->v_rdev;
	uio = ap->a_uio;
	node = DEVFS_NODE(vp);

	KKASSERT(uio->uio_segflg != UIO_NOCOPY);

	if (dev == NULL)		/* device was revoked */
		return (EBADF);

	vn_unlock(vp);
	error = dev_dwrite(dev, uio, ap->a_ioflag);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	if (node) {
		nanotime(&node->atime);
		nanotime(&node->mtime);
	}

	return (error);
}

/*
 * Device ioctl operation.
 *
 * spec_ioctl(struct vnode *a_vp, int a_command, caddr_t a_data,
 *	      int a_fflag, struct ucred *a_cred, struct sysmsg *msg)
 */
static int
devfs_spec_ioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp = ap->a_vp;
#if 0
	struct devfs_node *node;
#endif
	cdev_t dev;

	if ((dev = vp->v_rdev) == NULL)
		return (EBADF);		/* device was revoked */
#if 0
	node = DEVFS_NODE(vp);

	if (node) {
		nanotime(&node->atime);
		nanotime(&node->mtime);
	}
#endif

	return (dev_dioctl(dev, ap->a_command, ap->a_data, ap->a_fflag,
			   ap->a_cred, ap->a_sysmsg));
}

/*
 * spec_kqfilter(struct vnode *a_vp, struct knote *a_kn)
 */
/* ARGSUSED */
static int
devfs_spec_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
#if 0
	struct devfs_node *node;
#endif
	cdev_t dev;

	if ((dev = vp->v_rdev) == NULL)
		return (EBADF);		/* device was revoked (EBADF) */
#if 0
	node = DEVFS_NODE(vp);

	if (node)
		nanotime(&node->atime);
#endif

	return (dev_dkqfilter(dev, ap->a_kn));
}

/*
 * Convert a vnode strategy call into a device strategy call.  Vnode strategy
 * calls are not limited to device DMA limits so we have to deal with the
 * case.
 *
 * spec_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
static int
devfs_spec_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct buf *nbp;
	struct vnode *vp;
	struct mount *mp;
	int chunksize;
	int maxiosize;

	if (bp->b_cmd != BUF_CMD_READ && LIST_FIRST(&bp->b_dep) != NULL)
		buf_start(bp);

	/*
	 * Collect statistics on synchronous and asynchronous read
	 * and write counts for disks that have associated filesystems.
	 */
	vp = ap->a_vp;
	KKASSERT(vp->v_rdev != NULL);	/* XXX */
	if (vn_isdisk(vp, NULL) && (mp = vp->v_rdev->si_mountpoint) != NULL) {
		if (bp->b_cmd == BUF_CMD_READ) {
			if (bp->b_flags & BIO_SYNC)
				mp->mnt_stat.f_syncreads++;
			else
				mp->mnt_stat.f_asyncreads++;
		} else {
			if (bp->b_flags & BIO_SYNC)
				mp->mnt_stat.f_syncwrites++;
			else
				mp->mnt_stat.f_asyncwrites++;
		}
	}

        /*
         * Device iosize limitations only apply to read and write.  Shortcut
         * the I/O if it fits.
         */
	if ((maxiosize = vp->v_rdev->si_iosize_max) == 0) {
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "%s: si_iosize_max not set!\n",
			    dev_dname(vp->v_rdev));
		maxiosize = MAXPHYS;
	}
#if SPEC_CHAIN_DEBUG & 2
	maxiosize = 4096;
#endif
        if (bp->b_bcount <= maxiosize ||
            (bp->b_cmd != BUF_CMD_READ && bp->b_cmd != BUF_CMD_WRITE)) {
                dev_dstrategy_chain(vp->v_rdev, bio);
                return (0);
        }

	/*
	 * Clone the buffer and set up an I/O chain to chunk up the I/O.
	 */
	nbp = kmalloc(sizeof(*bp), M_DEVBUF, M_INTWAIT|M_ZERO);
	initbufbio(nbp);
	buf_dep_init(nbp);
	BUF_LOCK(nbp, LK_EXCLUSIVE);
	BUF_KERNPROC(nbp);
	nbp->b_vp = vp;
	nbp->b_flags = B_PAGING | (bp->b_flags & B_BNOCLIP);
	nbp->b_data = bp->b_data;
	nbp->b_bio1.bio_done = devfs_spec_strategy_done;
	nbp->b_bio1.bio_offset = bio->bio_offset;
	nbp->b_bio1.bio_caller_info1.ptr = bio;

	/*
	 * Start the first transfer
	 */
	if (vn_isdisk(vp, NULL))
		chunksize = vp->v_rdev->si_bsize_phys;
	else
		chunksize = DEV_BSIZE;
	chunksize = maxiosize / chunksize * chunksize;
#if SPEC_CHAIN_DEBUG & 1
	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "spec_strategy chained I/O chunksize=%d\n",
		    chunksize);
#endif
	nbp->b_cmd = bp->b_cmd;
	nbp->b_bcount = chunksize;
	nbp->b_bufsize = chunksize;	/* used to detect a short I/O */
	nbp->b_bio1.bio_caller_info2.index = chunksize;

#if SPEC_CHAIN_DEBUG & 1
	devfs_debug(DEVFS_DEBUG_DEBUG,
		    "spec_strategy: chain %p offset %d/%d bcount %d\n",
		    bp, 0, bp->b_bcount, nbp->b_bcount);
#endif

	dev_dstrategy(vp->v_rdev, &nbp->b_bio1);

	if (DEVFS_NODE(vp)) {
		nanotime(&DEVFS_NODE(vp)->atime);
		nanotime(&DEVFS_NODE(vp)->mtime);
	}

	return (0);
}

/*
 * Chunked up transfer completion routine - chain transfers until done
 *
 * NOTE: MPSAFE callback.
 */
static
void
devfs_spec_strategy_done(struct bio *nbio)
{
	struct buf *nbp = nbio->bio_buf;
	struct bio *bio = nbio->bio_caller_info1.ptr;	/* original bio */
	struct buf *bp = bio->bio_buf;			/* original bp */
	int chunksize = nbio->bio_caller_info2.index;	/* chunking */
	int boffset = nbp->b_data - bp->b_data;

	if (nbp->b_flags & B_ERROR) {
		/*
		 * An error terminates the chain, propogate the error back
		 * to the original bp
		 */
		bp->b_flags |= B_ERROR;
		bp->b_error = nbp->b_error;
		bp->b_resid = bp->b_bcount - boffset +
			      (nbp->b_bcount - nbp->b_resid);
#if SPEC_CHAIN_DEBUG & 1
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "spec_strategy: chain %p error %d bcount %d/%d\n",
			    bp, bp->b_error, bp->b_bcount,
			    bp->b_bcount - bp->b_resid);
#endif
	} else if (nbp->b_resid) {
		/*
		 * A short read or write terminates the chain
		 */
		bp->b_error = nbp->b_error;
		bp->b_resid = bp->b_bcount - boffset +
			      (nbp->b_bcount - nbp->b_resid);
#if SPEC_CHAIN_DEBUG & 1
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "spec_strategy: chain %p short read(1) "
			    "bcount %d/%d\n",
			    bp, bp->b_bcount - bp->b_resid, bp->b_bcount);
#endif
	} else if (nbp->b_bcount != nbp->b_bufsize) {
		/*
		 * A short read or write can also occur by truncating b_bcount
		 */
#if SPEC_CHAIN_DEBUG & 1
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "spec_strategy: chain %p short read(2) "
			    "bcount %d/%d\n",
			    bp, nbp->b_bcount + boffset, bp->b_bcount);
#endif
		bp->b_error = 0;
		bp->b_bcount = nbp->b_bcount + boffset;
		bp->b_resid = nbp->b_resid;
	} else if (nbp->b_bcount + boffset == bp->b_bcount) {
		/*
		 * No more data terminates the chain
		 */
#if SPEC_CHAIN_DEBUG & 1
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "spec_strategy: chain %p finished bcount %d\n",
			    bp, bp->b_bcount);
#endif
		bp->b_error = 0;
		bp->b_resid = 0;
	} else {
		/*
		 * Continue the chain
		 */
		boffset += nbp->b_bcount;
		nbp->b_data = bp->b_data + boffset;
		nbp->b_bcount = bp->b_bcount - boffset;
		if (nbp->b_bcount > chunksize)
			nbp->b_bcount = chunksize;
		nbp->b_bio1.bio_done = devfs_spec_strategy_done;
		nbp->b_bio1.bio_offset = bio->bio_offset + boffset;

#if SPEC_CHAIN_DEBUG & 1
		devfs_debug(DEVFS_DEBUG_DEBUG,
			    "spec_strategy: chain %p offset %d/%d bcount %d\n",
			    bp, boffset, bp->b_bcount, nbp->b_bcount);
#endif

		dev_dstrategy(nbp->b_vp->v_rdev, &nbp->b_bio1);
		return;
	}

	/*
	 * Fall through to here on termination.  biodone(bp) and
	 * clean up and free nbp.
	 */
	biodone(bio);
	BUF_UNLOCK(nbp);
	uninitbufbio(nbp);
	kfree(nbp, M_DEVBUF);
}

/*
 * spec_freeblks(struct vnode *a_vp, daddr_t a_addr, daddr_t a_length)
 */
static int
devfs_spec_freeblks(struct vop_freeblks_args *ap)
{
	struct buf *bp;

	/*
	 * XXX: This assumes that strategy does the deed right away.
	 * XXX: this may not be TRTTD.
	 */
	KKASSERT(ap->a_vp->v_rdev != NULL);
	if ((ap->a_vp->v_rdev->si_flags & SI_CANFREE) == 0)
		return (0);
	bp = geteblk(ap->a_length);
	bp->b_cmd = BUF_CMD_FREEBLKS;
	bp->b_bio1.bio_offset = ap->a_offset;
	bp->b_bcount = ap->a_length;
	dev_dstrategy(ap->a_vp->v_rdev, &bp->b_bio1);
	return (0);
}

/*
 * Implement degenerate case where the block requested is the block
 * returned, and assume that the entire device is contiguous in regards
 * to the contiguous block range (runp and runb).
 *
 * spec_bmap(struct vnode *a_vp, off_t a_loffset,
 *	     off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
devfs_spec_bmap(struct vop_bmap_args *ap)
{
	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = MAXBSIZE;
	if (ap->a_runb != NULL) {
		if (ap->a_loffset < MAXBSIZE)
			*ap->a_runb = (int)ap->a_loffset;
		else
			*ap->a_runb = MAXBSIZE;
	}
	return (0);
}


/*
 * Special device advisory byte-level locks.
 *
 * spec_advlock(struct vnode *a_vp, caddr_t a_id, int a_op,
 *		struct flock *a_fl, int a_flags)
 */
/* ARGSUSED */
static int
devfs_spec_advlock(struct vop_advlock_args *ap)
{
	return ((ap->a_flags & F_POSIX) ? EINVAL : EOPNOTSUPP);
}

/*
 * NOTE: MPSAFE callback.
 */
static void
devfs_spec_getpages_iodone(struct bio *bio)
{
	bio->bio_buf->b_cmd = BUF_CMD_DONE;
	wakeup(bio->bio_buf);
}

/*
 * spec_getpages() - get pages associated with device vnode.
 *
 * Note that spec_read and spec_write do not use the buffer cache, so we
 * must fully implement getpages here.
 */
static int
devfs_spec_getpages(struct vop_getpages_args *ap)
{
	vm_offset_t kva;
	int error;
	int i, pcount, size;
	struct buf *bp;
	vm_page_t m;
	vm_ooffset_t offset;
	int toff, nextoff, nread;
	struct vnode *vp = ap->a_vp;
	int blksiz;
	int gotreqpage;

	error = 0;
	pcount = round_page(ap->a_count) / PAGE_SIZE;

	/*
	 * Calculate the offset of the transfer and do sanity check.
	 */
	offset = IDX_TO_OFF(ap->a_m[0]->pindex) + ap->a_offset;

	/*
	 * Round up physical size for real devices.  We cannot round using
	 * v_mount's block size data because v_mount has nothing to do with
	 * the device.  i.e. it's usually '/dev'.  We need the physical block
	 * size for the device itself.
	 *
	 * We can't use v_rdev->si_mountpoint because it only exists when the
	 * block device is mounted.  However, we can use v_rdev.
	 */
	if (vn_isdisk(vp, NULL))
		blksiz = vp->v_rdev->si_bsize_phys;
	else
		blksiz = DEV_BSIZE;

	size = (ap->a_count + blksiz - 1) & ~(blksiz - 1);

	bp = getpbuf_kva(NULL);
	kva = (vm_offset_t)bp->b_data;

	/*
	 * Map the pages to be read into the kva.
	 */
	pmap_qenter(kva, ap->a_m, pcount);

	/* Build a minimal buffer header. */
	bp->b_cmd = BUF_CMD_READ;
	bp->b_bcount = size;
	bp->b_resid = 0;
	bsetrunningbufspace(bp, size);

	bp->b_bio1.bio_offset = offset;
	bp->b_bio1.bio_done = devfs_spec_getpages_iodone;

	mycpu->gd_cnt.v_vnodein++;
	mycpu->gd_cnt.v_vnodepgsin += pcount;

	/* Do the input. */
	vn_strategy(ap->a_vp, &bp->b_bio1);

	crit_enter();

	/* We definitely need to be at splbio here. */
	while (bp->b_cmd != BUF_CMD_DONE)
		tsleep(bp, 0, "spread", 0);

	crit_exit();

	if (bp->b_flags & B_ERROR) {
		if (bp->b_error)
			error = bp->b_error;
		else
			error = EIO;
	}

	/*
	 * If EOF is encountered we must zero-extend the result in order
	 * to ensure that the page does not contain garabge.  When no
	 * error occurs, an early EOF is indicated if b_bcount got truncated.
	 * b_resid is relative to b_bcount and should be 0, but some devices
	 * might indicate an EOF with b_resid instead of truncating b_bcount.
	 */
	nread = bp->b_bcount - bp->b_resid;
	if (nread < ap->a_count)
		bzero((caddr_t)kva + nread, ap->a_count - nread);
	pmap_qremove(kva, pcount);

	gotreqpage = 0;
	for (i = 0, toff = 0; i < pcount; i++, toff = nextoff) {
		nextoff = toff + PAGE_SIZE;
		m = ap->a_m[i];

		m->flags &= ~PG_ZERO;

		/*
		 * NOTE: vm_page_undirty/clear_dirty etc do not clear the
		 *	 pmap modified bit.  pmap modified bit should have
		 *	 already been cleared.
		 */
		if (nextoff <= nread) {
			m->valid = VM_PAGE_BITS_ALL;
			vm_page_undirty(m);
		} else if (toff < nread) {
			/*
			 * Since this is a VM request, we have to supply the
			 * unaligned offset to allow vm_page_set_valid()
			 * to zero sub-DEV_BSIZE'd portions of the page.
			 */
			vm_page_set_valid(m, 0, nread - toff);
			vm_page_clear_dirty_end_nonincl(m, 0, nread - toff);
		} else {
			m->valid = 0;
			vm_page_undirty(m);
		}

		if (i != ap->a_reqpage) {
			/*
			 * Just in case someone was asking for this page we
			 * now tell them that it is ok to use.
			 */
			if (!error || (m->valid == VM_PAGE_BITS_ALL)) {
				if (m->valid) {
					if (m->flags & PG_REFERENCED) {
						vm_page_activate(m);
					} else {
						vm_page_deactivate(m);
					}
					vm_page_wakeup(m);
				} else {
					vm_page_free(m);
				}
			} else {
				vm_page_free(m);
			}
		} else if (m->valid) {
			gotreqpage = 1;
			/*
			 * Since this is a VM request, we need to make the
			 * entire page presentable by zeroing invalid sections.
			 */
			if (m->valid != VM_PAGE_BITS_ALL)
			    vm_page_zero_invalid(m, FALSE);
		}
	}
	if (!gotreqpage) {
		m = ap->a_m[ap->a_reqpage];
		devfs_debug(DEVFS_DEBUG_WARNING,
	    "spec_getpages:(%s) I/O read failure: (error=%d) bp %p vp %p\n",
			devtoname(vp->v_rdev), error, bp, bp->b_vp);
		devfs_debug(DEVFS_DEBUG_WARNING,
	    "               size: %d, resid: %d, a_count: %d, valid: 0x%x\n",
		    size, bp->b_resid, ap->a_count, m->valid);
		devfs_debug(DEVFS_DEBUG_WARNING,
	    "               nread: %d, reqpage: %d, pindex: %lu, pcount: %d\n",
		    nread, ap->a_reqpage, (u_long)m->pindex, pcount);
		/*
		 * Free the buffer header back to the swap buffer pool.
		 */
		relpbuf(bp, NULL);
		return VM_PAGER_ERROR;
	}
	/*
	 * Free the buffer header back to the swap buffer pool.
	 */
	relpbuf(bp, NULL);
	if (DEVFS_NODE(ap->a_vp))
		nanotime(&DEVFS_NODE(ap->a_vp)->mtime);
	return VM_PAGER_OK;
}

static __inline
int
sequential_heuristic(struct uio *uio, struct file *fp)
{
	/*
	 * Sequential heuristic - detect sequential operation
	 */
	if ((uio->uio_offset == 0 && fp->f_seqcount > 0) ||
	    uio->uio_offset == fp->f_nextoff) {
		/*
		 * XXX we assume that the filesystem block size is
		 * the default.  Not true, but still gives us a pretty
		 * good indicator of how sequential the read operations
		 * are.
		 */
		int tmpseq = fp->f_seqcount;

		tmpseq += (uio->uio_resid + BKVASIZE - 1) / BKVASIZE;
		if (tmpseq > IO_SEQMAX)
			tmpseq = IO_SEQMAX;
		fp->f_seqcount = tmpseq;
		return(fp->f_seqcount << IO_SEQSHIFT);
	}

	/*
	 * Not sequential, quick draw-down of seqcount
	 */
	if (fp->f_seqcount > 1)
		fp->f_seqcount = 1;
	else
		fp->f_seqcount = 0;
	return(0);
}
