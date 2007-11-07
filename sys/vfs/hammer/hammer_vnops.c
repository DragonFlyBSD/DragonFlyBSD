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
 * $DragonFly: src/sys/vfs/hammer/hammer_vnops.c,v 1.2 2007/11/07 00:43:24 dillon Exp $
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
#include "hammer.h"

/*
 * USERFS VNOPS
 */
/*static int hammer_vop_vnoperate(struct vop_generic_args *);*/
static int hammer_vop_fsync(struct vop_fsync_args *);
static int hammer_vop_read(struct vop_read_args *);
static int hammer_vop_write(struct vop_write_args *);
static int hammer_vop_access(struct vop_access_args *);
static int hammer_vop_advlock(struct vop_advlock_args *);
static int hammer_vop_close(struct vop_close_args *);
static int hammer_vop_ncreate(struct vop_ncreate_args *);
static int hammer_vop_getattr(struct vop_getattr_args *);
static int hammer_vop_nresolve(struct vop_nresolve_args *);
static int hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *);
static int hammer_vop_nlink(struct vop_nlink_args *);
static int hammer_vop_nmkdir(struct vop_nmkdir_args *);
static int hammer_vop_nmknod(struct vop_nmknod_args *);
static int hammer_vop_open(struct vop_open_args *);
static int hammer_vop_pathconf(struct vop_pathconf_args *);
static int hammer_vop_print(struct vop_print_args *);
static int hammer_vop_readdir(struct vop_readdir_args *);
static int hammer_vop_readlink(struct vop_readlink_args *);
static int hammer_vop_nremove(struct vop_nremove_args *);
static int hammer_vop_nrename(struct vop_nrename_args *);
static int hammer_vop_nrmdir(struct vop_nrmdir_args *);
static int hammer_vop_setattr(struct vop_setattr_args *);
static int hammer_vop_strategy(struct vop_strategy_args *);
static int hammer_vop_nsymlink(struct vop_nsymlink_args *);
static int hammer_vop_nwhiteout(struct vop_nwhiteout_args *);

struct vop_ops hammer_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_read,
	.vop_write =		hammer_vop_write,
	.vop_access =		hammer_vop_access,
	.vop_advlock =		hammer_vop_advlock,
	.vop_close =		hammer_vop_close,
	.vop_ncreate =		hammer_vop_ncreate,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_nresolve =		hammer_vop_nresolve,
	.vop_nlookupdotdot =	hammer_vop_nlookupdotdot,
	.vop_nlink =		hammer_vop_nlink,
	.vop_nmkdir =		hammer_vop_nmkdir,
	.vop_nmknod =		hammer_vop_nmknod,
	.vop_open =		hammer_vop_open,
	.vop_pathconf =		hammer_vop_pathconf,
	.vop_print =		hammer_vop_print,
	.vop_readdir =		hammer_vop_readdir,
	.vop_readlink =		hammer_vop_readlink,
	.vop_nremove =		hammer_vop_nremove,
	.vop_nrename =		hammer_vop_nrename,
	.vop_nrmdir =		hammer_vop_nrmdir,
	.vop_setattr =		hammer_vop_setattr,
	.vop_strategy =		hammer_vop_strategy,
	.vop_nsymlink =		hammer_vop_nsymlink,
	.vop_nwhiteout =	hammer_vop_nwhiteout
};

#if 0
static
int
hammer_vop_vnoperate(struct vop_generic_args *)
{
	return (VOCALL(&hammer_vnode_vops, ap));
}
#endif

/*
 * hammer_vop_fsync { vp, waitfor }
 */
static
int
hammer_vop_fsync(struct vop_fsync_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_read { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_read(struct vop_read_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	off_t offset;
	struct buf *bp;
	struct uio *uio;
	int error;
	int n;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;

	hammer_start_transaction(ip->hmp, &trans);

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	uio = ap->a_uio;
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ino_rec.ino_size) {
		offset = uio->uio_offset & HAMMER_BUFMASK;
		error = bread(ap->a_vp, uio->uio_offset - offset,
			      HAMMER_BUFSIZE, &bp);
		if (error) {
			brelse(bp);
			break;
		}
		n = HAMMER_BUFSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->ino_rec.ino_size - uio->uio_offset)
			n = (int)(ip->ino_rec.ino_size - uio->uio_offset);
		error = uiomove((char *)bp->b_data + offset, n, uio);
		if (error) {
			brelse(bp);
			break;
		}
		ip->ino_rec.ino_atime = trans.tid;
		hammer_modify_inode(&trans, ip, HAMMER_INODE_ITIMES);
		bqrelse(bp);
	}
	hammer_commit_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_write { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_write(struct vop_write_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct uio *uio;
	off_t offset;
	struct buf *bp;
	int error;
	int n;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(ip->hmp, &trans);
	uio = ap->a_uio;

	/*
	 * Check append mode
	 */
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = ip->ino_rec.ino_size;

	/*
	 * Check for illegal write offsets.  Valid range is 0...2^63-1
	 */
	if (uio->uio_offset < 0 || uio->uio_offset + uio->uio_resid <= 0)
		return (EFBIG);

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	while (uio->uio_resid > 0) {
		offset = uio->uio_offset & HAMMER_BUFMASK;
		if (offset == 0 && uio->uio_resid >= HAMMER_BUFSIZE) {
			bp = getblk(ap->a_vp, uio->uio_offset, HAMMER_BUFSIZE,
				    0, 0);
		} else if (offset == 0 && uio->uio_offset >= ip->ino_rec.ino_size) {
			bp = getblk(ap->a_vp, uio->uio_offset, HAMMER_BUFSIZE,
				    0, 0);
			vfs_bio_clrbuf(bp);
		} else {
			error = bread(ap->a_vp, uio->uio_offset - offset,
				      HAMMER_BUFSIZE, &bp);
			if (error) {
				brelse(bp);
				break;
			}
		}
		n = HAMMER_BUFSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		error = uiomove((char *)bp->b_data + offset, n, uio);
		if (error) {
			brelse(bp);
			break;
		}
		if (ip->ino_rec.ino_size < uio->uio_offset) {
			ip->ino_rec.ino_size = uio->uio_offset;
			ip->ino_rec.ino_mtime = trans.tid;
			hammer_modify_inode(&trans, ip,
				HAMMER_INODE_RDIRTY | HAMMER_INODE_ITIMES);
		}
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if (ap->a_ioflag & IO_DIRECT) {
			/* XXX B_CLUSTEROK SUPPORT */
			bawrite(bp);
		} else {
			/* XXX B_CLUSTEROK SUPPORT */
			bdwrite(bp);
		}
	}
	if (error)
		hammer_abort_transaction(&trans);
	else
		hammer_commit_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_access { vp, mode, cred }
 */
static
int
hammer_vop_access(struct vop_access_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	uid_t uid;
	gid_t gid;
	int error;

	uid = hammer_to_unix_xid(&ip->ino_data.uid);
	gid = hammer_to_unix_xid(&ip->ino_data.gid);

	error = vop_helper_access(ap, uid, gid, ip->ino_data.mode,
				  ip->ino_data.uflags);
	return (error);
}

/*
 * hammer_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer_vop_advlock(struct vop_advlock_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &ip->advlock, ip->ino_rec.ino_size));
}

/*
 * hammer_vop_close { vp, fflag }
 */
static
int
hammer_vop_close(struct vop_close_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_ncreate(struct vop_ncreate_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(dip->hmp, &trans);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be locked.  We cannot hold the new
	 * inode locked while doing other manipulations.
	 */
	error = hammer_alloc_inode(&trans, ap->a_vap, ap->a_cred, &nip);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}
	hammer_lock_to_ref(&nip->lock);

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_put_inode_ref(nip);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_put_inode_ref(nip);
	}
	return (error);
}

/*
 * hammer_vop_getattr { vp, vap }
 */
static
int
hammer_vop_getattr(struct vop_getattr_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	struct vattr *vap = ap->a_vap;

#if 0
	if (cache_check_fsmid_vp(ap->a_vp, &ip->fsmid) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0 &&
	    ip->obj_asof == 0
	) {
		/* LAZYMOD XXX */
	}
	hammer_itimes(ap->a_vp);
#endif

	vap->va_fsid = ip->hmp->fsid_udev;
	vap->va_fileid = ip->ino_rec.base.base.obj_id;
	vap->va_mode = ip->ino_data.mode;
	vap->va_nlink = ip->ino_rec.ino_nlinks;
	vap->va_uid = hammer_to_unix_xid(&ip->ino_data.uid);
	vap->va_gid = hammer_to_unix_xid(&ip->ino_data.gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->ino_rec.ino_size;
	hammer_to_timespec(ip->ino_rec.ino_atime, &vap->va_atime);
	hammer_to_timespec(ip->ino_rec.ino_mtime, &vap->va_mtime);
	hammer_to_timespec(ip->ino_data.ctime, &vap->va_ctime);
	vap->va_flags = ip->ino_data.uflags;
	vap->va_gen = 1;	/* hammer inums are unique for all time */
	vap->va_blocksize = 32768; /* XXX - extract from root volume */
	vap->va_bytes = ip->ino_rec.ino_size;
	vap->va_type = hammer_get_vnode_type(ip->ino_rec.base.base.obj_type);
	vap->va_filerev = 0; 	/* XXX */
	/* mtime uniquely identifies any adjustments made to the file */
	vap->va_fsmid = ip->ino_rec.ino_mtime;
	vap->va_uid_uuid = ip->ino_data.uid;
	vap->va_gid_uuid = ip->ino_data.gid;
	vap->va_fsid_uuid = ip->hmp->fsid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;
	return(0);
}

/*
 * hammer_vop_nresolve { nch, dvp, cred }
 *
 * Locate the requested directory entry.
 */
static
int
hammer_vop_nresolve(struct vop_nresolve_args *ap)
{
	struct hammer_base_elm key;
	struct namecache *ncp;
	struct hammer_inode *dip;
	struct hammer_btree_info info;
	struct vnode *vp;
	int64_t namekey;
	int error;
	const int flags = HAMMER_BTREE_GET_RECORD | HAMMER_BTREE_GET_DATA;

	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	namekey = hammer_directory_namekey(ncp->nc_name, ncp->nc_nlen);

	hammer_btree_info_init(&info, dip->hmp->rootcl);
        key.obj_id = dip->obj_id;
	key.key = namekey;
        key.create_tid = dip->obj_asof;
        key.delete_tid = 0;
        key.rec_type = HAMMER_RECTYPE_DIRENTRY;
        key.obj_type = 0;

	/*
	 * Issue a lookup on the namekey.  The entry should not be found
	 * since the low bits of the key are 0.  This positions our cursor
	 * properly for the iteration.
	 */
        error = hammer_btree_lookup(&info, &key, 0);
	if (error != ENOENT) {
		if (error == 0)
			error = EIO;
		goto done;
	}

	/*
	 * Iterate through the keys as long as the upper 32 bits are
	 * the same.
	 */
	while ((error = hammer_btree_iterate(&info.cursor, &key)) == 0) {
		if ((error = hammer_btree_extract(&info, flags)) != 0)
			break;
		if ((namekey ^ info.rec->base.base.key) &
		    (int64_t)0xFFFFFFFF00000000ULL) {
			error = ENOENT;
			break;
		}
		if (ncp->nc_nlen == info.rec->base.data_len &&
		    bcmp(ncp->nc_name, (void *)info.data, ncp->nc_nlen) == 0) {
			break;
		}
	}
	if (error == 0) {
		error = hammer_vfs_vget(dip->hmp->mp, info.rec->entry.obj_id, &vp);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
	} else if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
	}
done:
        hammer_btree_info_done(&info);
	return (error);
}

/*
 * hammer_vop_nlookupdotdot { dvp, vpp, cred }
 *
 * Locate the parent directory of a directory vnode.
 *
 * dvp is referenced but not locked.  *vpp must be returned referenced and
 * locked.  A parent_obj_id of 0 does not necessarily indicate that we are
 * at the root, instead it could indicate that the directory we were in was
 * removed.
 */
static
int
hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct hammer_inode *dip;
	u_int64_t parent_obj_id;

	dip = VTOI(ap->a_dvp);
	if ((parent_obj_id = dip->ino_data.parent_obj_id) == 0) {
		*ap->a_vpp = NULL;
		return ENOENT;
	}
	return(hammer_vfs_vget(dip->hmp->mp, parent_obj_id, ap->a_vpp));
}

/*
 * hammer_vop_nlink { nch, dvp, vp, cred }
 */
static
int
hammer_vop_nlink(struct vop_nlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *ip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);
	ip = VTOI(ap->a_vp);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(dip->hmp, &trans);

	/*
	 * Add the filesystem object to the directory.  Note that neither
	 * dip nor ip are referenced or locked, but their vnodes are
	 * referenced.  This function will bump the inode's link count.
	 */
	error = hammer_add_directory(&trans, dip, nch->ncp, ip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_abort_transaction(&trans);
	} else {
		hammer_commit_transaction(&trans);
	}
	return (error);
}

/*
 * hammer_vop_nmkdir { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(dip->hmp, &trans);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be locked.  We cannot hold the new
	 * inode locked while doing other manipulations.
	 */
	error = hammer_alloc_inode(&trans, ap->a_vap, ap->a_cred, &nip);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}
	hammer_lock_to_ref(&nip->lock);

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_put_inode_ref(nip);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_put_inode_ref(nip);
	}
	return (error);
}

/*
 * hammer_vop_nmknod { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmknod(struct vop_nmknod_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(dip->hmp, &trans);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be locked.  We cannot hold the new
	 * inode locked while doing other manipulations.
	 */
	error = hammer_alloc_inode(&trans, ap->a_vap, ap->a_cred, &nip);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}
	hammer_lock_to_ref(&nip->lock);

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_put_inode_ref(nip);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_put_inode_ref(nip);
	}
	return (error);
}

/*
 * hammer_vop_open { vp, mode, cred, fp }
 */
static
int
hammer_vop_open(struct vop_open_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_pathconf { vp, name, retval }
 */
static
int
hammer_vop_pathconf(struct vop_pathconf_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_print { vp }
 */
static
int
hammer_vop_print(struct vop_print_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_readdir { vp, uio, cred, *eofflag }
 */
static
int
hammer_vop_readdir(struct vop_readdir_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_readlink { vp, uio, cred }
 */
static
int
hammer_vop_readlink(struct vop_readlink_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_nremove { nch, dvp, cred }
 */
static
int
hammer_vop_nremove(struct vop_nremove_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer_vop_nrename(struct vop_nrename_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_setattr { vp, vap, cred }
 */
static
int
hammer_vop_setattr(struct vop_setattr_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_nwhiteout { nch, dvp, cred, flags }
 */
static
int
hammer_vop_nwhiteout(struct vop_nwhiteout_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_strategy { vp, bio }
 */
static
int
hammer_vop_strategy(struct vop_strategy_args *ap)
{
	return EOPNOTSUPP;
}

#if 0
	struct hammer_data_record *data;
	struct hammer_base_elm_t key;
	hammer_btree_info info;
	const int flags = HAMMER_BTREE_GET_RECORD | HAMMER_BTREE_GET_DATA;
	int64_t base_offset;
	int didinit;
	int o;

	hammer_btree_info_init(&info, ip->hmp->rootcl);
        key.obj_id = ip->obj_id;
        key.create_tid = ip->obj_asof;
        key.delete_tid = 0;
        key.rec_type = HAMMER_RECTYPE_DATA;
        key.obj_type = 0;
	key.key = uio->uio_offset;

	/*
	 * Iterate through matching records.  Note that for data records
	 * the base offset is the key - data_len, NOT the key.  This way
	 * we don't have to special case a ranged search.
	 */
	error = hammer_btree_lookup(&info, &key, 0);
	if (error && error != ENOENT)
		goto done;
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ino_rec.ino_size) {
		if ((error = hammer_btree_iterate(&info.cursor, &key)) != 0)
			break;
		/*
		 * XXX - possible to optimize the extract
		 */
		if ((error = hammer_btree_extract(&info, flags)) != 0)
			break;
		data = &info.rec->data;
		base_offset = data->base.key - data->base.data_len;
		if (uio->uio_offset < base_offset) {
			if (base_offset - uio->uio_offset > HAMMER_BUFSIZE)
				n = HAMMER_BUFSIZE;
			else
				n = (int)(base_offset - uio->uio_offset);
			error = uiomove(ip->hmp->zbuf, n, uio);
		} else {
			o = (int)uio->uio_offset - base_offset;
			if (o < data->base.data_len) {
				n = data->base.data_len - o;
				if (n > uio->uio_resid)
					n = uio->uio_resid;
				error = uiomove((char *)info.data + o, n, uio);
			}
		}
		if (error)
			break;
	}

	/*
	 * Issue a lookup on the namekey.  The entry should not be found
	 * since the low bits of the key are 0.  This positions our cursor
	 * properly for the iteration.
	 */
	if (error != ENOENT) {
		if (error == 0)
			error = EIO;
		goto done;
	}

	/*
	 * Iterate through the keys as long as the upper 32 bits are
	 * the same.
	 */
	while ((error = hammer_btree_iterate(&info, &key, flags)) == 0) {
		if ((namekey ^ info.rec->base.base.key) &
		    (int64_t)0xFFFFFFFF00000000ULL) {
			error = ENOENT;
			break;
		}
		if (ncp->nc_nlen == info.rec->base.data_len &&
		    bcmp(ncp->nc_name, (void *)info->data, ncp->nc_nlen) == 0) {
			break;
		}
	}
	if (error == 0) {
		error = hammer_vfs_vget(dip->hmp->mp, info->rec->entry.obj_id, &vp);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(nch, vp);
			vrele(vp);
		}
	} else if (error == ENOENT) {
		cache_setvp(nch, NULL);
	}
done:
        hammer_btree_info_done(&info);
	return (error);


	return EOPNOTSUPP;
}

#endif
