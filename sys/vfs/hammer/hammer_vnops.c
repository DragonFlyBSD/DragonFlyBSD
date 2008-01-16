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
 * $DragonFly: src/sys/vfs/hammer/hammer_vnops.c,v 1.20 2008/01/16 01:15:37 dillon Exp $
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
#include <sys/dirent.h>
#include <vm/vm_extern.h>
#include <vfs/fifofs/fifo.h>
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

static int hammer_vop_fifoclose (struct vop_close_args *);
static int hammer_vop_fiforead (struct vop_read_args *);
static int hammer_vop_fifowrite (struct vop_write_args *);

static int hammer_vop_specclose (struct vop_close_args *);
static int hammer_vop_specread (struct vop_read_args *);
static int hammer_vop_specwrite (struct vop_write_args *);

struct vop_ops hammer_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
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

struct vop_ops hammer_spec_vops = {
	.vop_default =		spec_vnoperate,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_specread,
	.vop_write =		hammer_vop_specwrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_specclose,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr
};

struct vop_ops hammer_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_fiforead,
	.vop_write =		hammer_vop_fifowrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_fifoclose,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr
};

static int hammer_dounlink(struct nchandle *nch, struct vnode *dvp,
			   struct ucred *cred, int flags);
static int hammer_vop_strategy_read(struct vop_strategy_args *ap);
static int hammer_vop_strategy_write(struct vop_strategy_args *ap);

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
	hammer_inode_t ip;
	int error;

	ip = VTOI(ap->a_vp);
	error = hammer_sync_inode(ip, ap->a_waitfor, 0);
	return (error);
}

/*
 * hammer_vop_read { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_read(struct vop_read_args *ap)
{
	struct hammer_transaction trans;
	hammer_inode_t ip;
	off_t offset;
	struct buf *bp;
	struct uio *uio;
	int error;
	int n;
	int seqcount;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;
	seqcount = ap->a_ioflag >> 16;

	hammer_start_transaction(&trans, ip->hmp);

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	uio = ap->a_uio;
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ino_rec.ino_size) {
		offset = uio->uio_offset & HAMMER_BUFMASK;
#if 0
		error = cluster_read(ap->a_vp, ip->ino_rec.ino_size,
				     uio->uio_offset - offset, HAMMER_BUFSIZE,
				     MAXBSIZE, seqcount, &bp);
#endif
		error = bread(ap->a_vp, uio->uio_offset - offset,
			      HAMMER_BUFSIZE, &bp);
		if (error) {
			brelse(bp);
			break;
		}
		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		n = HAMMER_BUFSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->ino_rec.ino_size - uio->uio_offset)
			n = (int)(ip->ino_rec.ino_size - uio->uio_offset);
		error = uiomove((char *)bp->b_data + offset, n, uio);
		if (error) {
			bqrelse(bp);
			break;
		}
		if ((ip->flags & HAMMER_INODE_RO) == 0) {
			ip->ino_rec.ino_atime = trans.tid;
			hammer_modify_inode(&trans, ip, HAMMER_INODE_ITIMES);
		}
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
	int flags;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;

	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, ip->hmp);
	uio = ap->a_uio;

	/*
	 * Check append mode
	 */
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = ip->ino_rec.ino_size;

	/*
	 * Check for illegal write offsets.  Valid range is 0...2^63-1
	 */
	if (uio->uio_offset < 0 || uio->uio_offset + uio->uio_resid <= 0) {
		hammer_commit_transaction(&trans);
		return (EFBIG);
	}

	/*
	 * Access the data in HAMMER_BUFSIZE blocks via the buffer cache.
	 */
	while (uio->uio_resid > 0) {
		offset = uio->uio_offset & HAMMER_BUFMASK;
		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(ap->a_vp, uio->uio_offset, HAMMER_BUFSIZE,
				    GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ap->a_vp,
					      uio->uio_offset - offset,
					      HAMMER_BUFSIZE, &bp);
				if (error) {
					brelse(bp);
					break;
				}
			}
		} else if (offset == 0 && uio->uio_resid >= HAMMER_BUFSIZE) {
			/*
			 * entirely overwrite the buffer
			 */
			bp = getblk(ap->a_vp, uio->uio_offset, HAMMER_BUFSIZE,
				    GETBLK_BHEAVY, 0);
		} else if (offset == 0 && uio->uio_offset >= ip->ino_rec.ino_size) {
			/*
			 * XXX
			 */
			bp = getblk(ap->a_vp, uio->uio_offset, HAMMER_BUFSIZE,
				    GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 */
			error = bread(ap->a_vp, uio->uio_offset - offset,
				      HAMMER_BUFSIZE, &bp);
			if (error) {
				brelse(bp);
				break;
			}
			bheavy(bp);
		}
		n = HAMMER_BUFSIZE - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		error = uiomove((char *)bp->b_data + offset, n, uio);
		if (error) {
			brelse(bp);
			break;
		}
		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		if (ip->ino_rec.ino_size < uio->uio_offset) {
			ip->ino_rec.ino_size = uio->uio_offset;
			flags = HAMMER_INODE_RDIRTY;
			vnode_pager_setsize(ap->a_vp, ip->ino_rec.ino_size);
		} else {
			flags = 0;
		}
		ip->ino_rec.ino_mtime = trans.tid;
		flags |= HAMMER_INODE_ITIMES | HAMMER_INODE_BUFS;
		hammer_modify_inode(&trans, ip, flags);
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if (ap->a_ioflag & IO_DIRECT) {
			bawrite(bp);
		} else {
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
	return (vop_stdclose(ap));
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

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */

	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error)
		kprintf("hammer_create_inode error %d\n", error);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);
	if (error)
		kprintf("hammer_ip_add_directory error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
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
	    ip->obj_asof == XXX
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

	switch (ip->ino_rec.base.base.obj_type) {
	case HAMMER_OBJTYPE_CDEV:
	case HAMMER_OBJTYPE_BDEV:
		vap->va_rmajor = ip->ino_data.rmajor;
		vap->va_rminor = ip->ino_data.rminor;
		break;
	default:
		break;
	}

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
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	hammer_tid_t asof;
	struct hammer_cursor cursor;
	union hammer_record_ondisk *rec;
	struct vnode *vp;
	int64_t namekey;
	int error;
	int i;
	int nlen;
	int flags;

	/*
	 * Misc initialization, plus handle as-of name extensions.  Look for
	 * the '@@' extension.  Note that as-of files and directories cannot
	 * be modified.
	 */
	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	asof = dip->obj_asof;
	nlen = ncp->nc_nlen;
	flags = dip->flags;

	for (i = 0; i < nlen; ++i) {
		if (ncp->nc_name[i] == '@' && ncp->nc_name[i+1] == '@') {
			asof = hammer_str_to_tid(ncp->nc_name + i + 2);
			kprintf("ASOF %016llx\n", asof);
			flags |= HAMMER_INODE_RO;
			break;
		}
	}
	nlen = i;

	/*
	 * If there is no path component the time extension is relative to
	 * dip.
	 */
	if (nlen == 0) {
		ip = hammer_get_inode(dip->hmp, &dip->cache[1], dip->obj_id,
				      asof, flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, LK_EXCLUSIVE, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
		return(error);
	}

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(ncp->nc_name, nlen);

	hammer_init_cursor_hmp(&cursor, &dip->cache[0], dip->hmp);
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor, dip);
	rec = NULL;
	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		rec = cursor.record;
		if (nlen == rec->entry.base.data_len &&
		    bcmp(ncp->nc_name, cursor.data, nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}
	if (error == 0) {
		ip = hammer_get_inode(dip->hmp, &dip->cache[1],
				      rec->entry.obj_id, asof,
				      flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, LK_EXCLUSIVE, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
	} else if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
	}
	hammer_done_cursor(&cursor);
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
	struct hammer_inode *ip;
	u_int64_t parent_obj_id;
	int error;

	dip = VTOI(ap->a_dvp);
	if ((parent_obj_id = dip->ino_data.parent_obj_id) == 0) {
		*ap->a_vpp = NULL;
		return ENOENT;
	}

	ip = hammer_get_inode(dip->hmp, &dip->cache[1], parent_obj_id,
			      dip->obj_asof, dip->flags, &error);
	if (ip == NULL) {
		*ap->a_vpp = NULL;
		return(error);
	}
	error = hammer_get_vnode(ip, LK_EXCLUSIVE, ap->a_vpp);
	hammer_rel_inode(ip, 0);
	return (error);
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

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Add the filesystem object to the directory.  Note that neither
	 * dip nor ip are referenced or locked, but their vnodes are
	 * referenced.  This function will bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, ip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_abort_transaction(&trans);
	} else {
		cache_setunresolved(nch);
		cache_setvp(nch, ap->a_vp);
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

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error)
		kprintf("hammer_mkdir error %d\n", error);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);
	if (error)
		kprintf("hammer_mkdir (add) error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
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

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
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
	if ((ap->a_mode & FWRITE) && (VTOI(ap->a_vp)->flags & HAMMER_INODE_RO))
		return (EROFS);

	return(vop_stdopen(ap));
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
 * hammer_vop_readdir { vp, uio, cred, *eofflag, *ncookies, off_t **cookies }
 */
static
int
hammer_vop_readdir(struct vop_readdir_args *ap)
{
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	struct uio *uio;
	hammer_record_ondisk_t rec;
	hammer_base_elm_t base;
	int error;
	int cookie_index;
	int ncookies;
	off_t *cookies;
	off_t saveoff;
	int r;

	ip = VTOI(ap->a_vp);
	uio = ap->a_uio;
	saveoff = uio->uio_offset;

	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		cookie_index = 0;
	} else {
		ncookies = -1;
		cookies = NULL;
		cookie_index = 0;
	}

	/*
	 * Handle artificial entries
	 */
	error = 0;
	if (saveoff == 0) {
		r = vop_write_dirent(&error, uio, ip->obj_id, DT_DIR, 1, ".");
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
		if (ip->ino_data.parent_obj_id) {
			r = vop_write_dirent(&error, uio,
					     ip->ino_data.parent_obj_id,
					     DT_DIR, 2, "..");
		} else {
			r = vop_write_dirent(&error, uio,
					     ip->obj_id, DT_DIR, 2, "..");
		}
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = saveoff;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	error = hammer_ip_first(&cursor, ip);

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		rec = cursor.record;
		base = &rec->base.base;
		saveoff = base->key;

		if (base->obj_id != ip->obj_id)
			panic("readdir: bad record at %p", cursor.node);

		r = vop_write_dirent(
			     &error, uio, rec->entry.obj_id,
			     hammer_get_dtype(rec->entry.base.base.obj_type),
			     rec->entry.base.data_len,
			     (void *)cursor.data);
		if (r)
			break;
		++saveoff;
		if (cookies)
			cookies[cookie_index] = base->key;
		++cookie_index;
		if (cookie_index == ncookies)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);

done:
	if (ap->a_eofflag)
		*ap->a_eofflag = (error == ENOENT);
	uio->uio_offset = saveoff;
	if (error && cookie_index == 0) {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
	return(error);
}

/*
 * hammer_vop_readlink { vp, uio, cred }
 */
static
int
hammer_vop_readlink(struct vop_readlink_args *ap)
{
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	int error;

	ip = VTOI(ap->a_vp);
	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_FIX;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = HAMMER_FIXKEY_SYMLINK;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	error = hammer_ip_lookup(&cursor, ip);
	if (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error == 0) {
			error = uiomove((char *)cursor.data,
					cursor.record->generic.base.data_len,
					ap->a_uio);
		}
	}
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * hammer_vop_nremove { nch, dvp, cred }
 */
static
int
hammer_vop_nremove(struct vop_nremove_args *ap)
{
	return(hammer_dounlink(ap->a_nch, ap->a_dvp, ap->a_cred, 0));
}

/*
 * hammer_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer_vop_nrename(struct vop_nrename_args *ap)
{
	struct hammer_transaction trans;
	struct namecache *fncp;
	struct namecache *tncp;
	struct hammer_inode *fdip;
	struct hammer_inode *tdip;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	union hammer_record_ondisk *rec;
	int64_t namekey;
	int error;

	fdip = VTOI(ap->a_fdvp);
	tdip = VTOI(ap->a_tdvp);
	fncp = ap->a_fnch->ncp;
	tncp = ap->a_tnch->ncp;
	ip = VTOI(fncp->nc_vp);
	KKASSERT(ip != NULL);

	if (fdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (tdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	hammer_start_transaction(&trans, fdip->hmp);

	/*
	 * Remove tncp from the target directory and then link ip as
	 * tncp. XXX pass trans to dounlink
	 */
	error = hammer_dounlink(ap->a_tnch, ap->a_tdvp, ap->a_cred, 0);
	if (error == 0 || error == ENOENT)
		error = hammer_ip_add_directory(&trans, tdip, tncp, ip);
	if (error)
		goto failed; /* XXX */

	/*
	 * Locate the record in the originating directory and remove it.
	 *
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(fncp->nc_name, fncp->nc_nlen);

	hammer_init_cursor_hmp(&cursor, &fdip->cache[0], fdip->hmp);
        cursor.key_beg.obj_id = fdip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = fdip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor, fdip);
	while (error == 0) {
		if (hammer_ip_resolve_data(&cursor) != 0)
			break;
		rec = cursor.record;
		if (fncp->nc_nlen == rec->entry.base.data_len &&
		    bcmp(fncp->nc_name, cursor.data, fncp->nc_nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 */
	if (error)
		goto done;
	error = hammer_ip_del_directory(&trans, &cursor, fdip, ip);

	if (error == 0)
		cache_rename(ap->a_fnch, ap->a_tnch);
done:
        hammer_done_cursor(&cursor);
failed:
	if (error == 0) {
		hammer_commit_transaction(&trans);
	} else {
		hammer_abort_transaction(&trans);
	}
	return (error);
}

/*
 * hammer_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	return(hammer_dounlink(ap->a_nch, ap->a_dvp, ap->a_cred, 0));
}

/*
 * hammer_vop_setattr { vp, vap, cred }
 */
static
int
hammer_vop_setattr(struct vop_setattr_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor *spike = NULL;
	struct vattr *vap;
	struct hammer_inode *ip;
	int modflags;
	int error;
	int64_t aligned_size;
	u_int32_t flags;
	uuid_t uuid;

	vap = ap->a_vap;
	ip = ap->a_vp->v_data;
	modflags = 0;

	if (ap->a_vp->v_mount->mnt_flag & MNT_RDONLY)
		return(EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	hammer_start_transaction(&trans, ip->hmp);
	error = 0;

	if (vap->va_flags != VNOVAL) {
		flags = ip->ino_data.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer_to_unix_xid(&ip->ino_data.uid),
					 ap->a_cred);
		if (error == 0) {
			if (ip->ino_data.uflags != flags) {
				ip->ino_data.uflags = flags;
				modflags |= HAMMER_INODE_DDIRTY;
			}
			if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL) {
		hammer_guid_to_uuid(&uuid, vap->va_uid);
		if (bcmp(&uuid, &ip->ino_data.uid, sizeof(uuid)) != 0) {
			ip->ino_data.uid = uuid;
			modflags |= HAMMER_INODE_DDIRTY;
		}
	}
	if (vap->va_gid != (uid_t)VNOVAL) {
		hammer_guid_to_uuid(&uuid, vap->va_gid);
		if (bcmp(&uuid, &ip->ino_data.gid, sizeof(uuid)) != 0) {
			ip->ino_data.gid = uuid;
			modflags |= HAMMER_INODE_DDIRTY;
		}
	}
	while (vap->va_size != VNOVAL && ip->ino_rec.ino_size != vap->va_size) {
		switch(ap->a_vp->v_type) {
		case VREG:
			if (vap->va_size < ip->ino_rec.ino_size) {
				vtruncbuf(ap->a_vp, vap->va_size,
					  HAMMER_BUFSIZE);
			} else if (vap->va_size > ip->ino_rec.ino_size) {
				vnode_pager_setsize(ap->a_vp, vap->va_size);
			}
			aligned_size = (vap->va_size + HAMMER_BUFMASK) &
					~(int64_t)HAMMER_BUFMASK;
			error = hammer_ip_delete_range(&trans, ip,
						    aligned_size,
						    0x7FFFFFFFFFFFFFFFLL,
						    &spike);
			ip->ino_rec.ino_size = vap->va_size;
			modflags |= HAMMER_INODE_RDIRTY;
			break;
		case VDATABASE:
			error = hammer_ip_delete_range(&trans, ip,
						    vap->va_size,
						    0x7FFFFFFFFFFFFFFFLL,
						    &spike);
			ip->ino_rec.ino_size = vap->va_size;
			modflags |= HAMMER_INODE_RDIRTY;
			break;
		default:
			error = EINVAL;
			goto done;
		}
		if (error == ENOSPC) {
			error = hammer_spike(&spike);
			if (error == 0)
				continue;
		}
		KKASSERT(spike == NULL);
		break;
	}
	if (vap->va_atime.tv_sec != VNOVAL) {
		ip->ino_rec.ino_atime =
			hammer_timespec_to_transid(&vap->va_atime);
		modflags |= HAMMER_INODE_ITIMES;
	}
	if (vap->va_mtime.tv_sec != VNOVAL) {
		ip->ino_rec.ino_mtime =
			hammer_timespec_to_transid(&vap->va_mtime);
		modflags |= HAMMER_INODE_ITIMES;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (ip->ino_data.mode != vap->va_mode) {
			ip->ino_data.mode = vap->va_mode;
			modflags |= HAMMER_INODE_DDIRTY;
		}
	}
done:
	if (error) {
		hammer_abort_transaction(&trans);
	} else {
		hammer_modify_inode(&trans, ip, modflags);
		hammer_commit_transaction(&trans);
	}
	return (error);
}

/*
 * hammer_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	hammer_record_t record;
	int error;
	int bytes;

	ap->a_vap->va_type = VLNK;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */

	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred, dip, &nip);
	if (error) {
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip, nch->ncp, nip);

	/*
	 * Add a record representing the symlink.  symlink stores the link
	 * as pure data, not a string, and is no \0 terminated.
	 */
	if (error == 0) {
		record = hammer_alloc_mem_record(nip);
		bytes = strlen(ap->a_target);

		record->rec.generic.base.base.key = HAMMER_FIXKEY_SYMLINK;
		record->rec.generic.base.base.rec_type = HAMMER_RECTYPE_FIX;
		record->rec.generic.base.data_len = bytes;
		if (bytes <= sizeof(record->rec.generic.filler)) {
			record->data = (void *)record->rec.generic.filler;
			bcopy(ap->a_target, record->data, bytes);
		} else {
			record->data = (void *)ap->a_target;
			/* will be reallocated by routine below */
		}
		error = hammer_ip_add_record(&trans, record);
	}

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_abort_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		hammer_commit_transaction(&trans);
		error = hammer_get_vnode(nip, LK_EXCLUSIVE, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	return (error);
}

/*
 * hammer_vop_nwhiteout { nch, dvp, cred, flags }
 */
static
int
hammer_vop_nwhiteout(struct vop_nwhiteout_args *ap)
{
	return(hammer_dounlink(ap->a_nch, ap->a_dvp, ap->a_cred, ap->a_flags));
}

/*
 * hammer_vop_strategy { vp, bio }
 *
 * Strategy call, used for regular file read & write only.  Note that the
 * bp may represent a cluster.
 *
 * To simplify operation and allow better optimizations in the future,
 * this code does not make any assumptions with regards to buffer alignment
 * or size.
 */
static
int
hammer_vop_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp;
	int error;

	bp = ap->a_bio->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer_vop_strategy_read(ap);
		break;
	case BUF_CMD_WRITE:
		error = hammer_vop_strategy_write(ap);
		break;
	default:
		error = EINVAL;
		break;
	}
	bp->b_error = error;
	if (error)
		bp->b_flags |= B_ERROR;
	biodone(ap->a_bio);
	return (error);
}

/*
 * Read from a regular file.  Iterate the related records and fill in the
 * BIO/BUF.  Gaps are zero-filled.
 *
 * The support code in hammer_object.c should be used to deal with mixed
 * in-memory and on-disk records.
 *
 * XXX atime update
 */
static
int
hammer_vop_strategy_read(struct vop_strategy_args *ap)
{
	struct hammer_inode *ip = ap->a_vp->v_data;
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	hammer_base_elm_t base;
	struct bio *bio;
	struct buf *bp;
	int64_t rec_offset;
	int64_t ran_end;
	int64_t tmp64;
	int error;
	int boff;
	int roff;
	int n;

	bio = ap->a_bio;
	bp = bio->bio_buf;

	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);

	/*
	 * Key range (begin and end inclusive) to scan.  Note that the key's
	 * stored in the actual records represent BASE+LEN, not BASE.  The
	 * first record containing bio_offset will have a key > bio_offset.
	 */
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = bio->bio_offset + 1;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	cursor.key_end = cursor.key_beg;
	if (ip->ino_rec.base.base.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
	} else {
		ran_end = bio->bio_offset + bp->b_bufsize;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
		tmp64 = ran_end + MAXPHYS + 1;	/* work-around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor.key_end.key = ran_end + MAXPHYS + 1;
	}
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor, ip);
	boff = 0;

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		rec = cursor.record;
		base = &rec->base.base;

		rec_offset = base->key - rec->data.base.data_len;

		/*
		 * Calculate the gap, if any, and zero-fill it.
		 */
		n = (int)(rec_offset - (bio->bio_offset + boff));
		if (n > 0) {
			if (n > bp->b_bufsize - boff)
				n = bp->b_bufsize - boff;
			kprintf("zfill %d bytes\n", n);
			bzero((char *)bp->b_data + boff, n);
			boff += n;
			n = 0;
		}

		/*
		 * Calculate the data offset in the record and the number
		 * of bytes we can copy.
		 *
		 * Note there is a degenerate case here where boff may
		 * already be at bp->b_bufsize.
		 */
		roff = -n;
		n = rec->data.base.data_len - roff;
		KKASSERT(n > 0);
		if (n > bp->b_bufsize - boff)
			n = bp->b_bufsize - boff;
		bcopy((char *)cursor.data + roff, (char *)bp->b_data + boff, n);
		boff += n;
		if (boff == bp->b_bufsize)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);

	/*
	 * There may have been a gap after the last record
	 */
	if (error == ENOENT)
		error = 0;
	if (error == 0 && boff != bp->b_bufsize) {
		KKASSERT(boff < bp->b_bufsize);
		bzero((char *)bp->b_data + boff, bp->b_bufsize - boff);
		/* boff = bp->b_bufsize; */
	}
	bp->b_resid = 0;
	return(error);
}

/*
 * Write to a regular file.  Iterate the related records and mark for
 * deletion.  If existing edge records (left and right side) overlap our
 * write they have to be marked deleted and new records created, usually
 * referencing a portion of the original data.  Then add a record to
 * represent the buffer.
 *
 * The support code in hammer_object.c should be used to deal with mixed
 * in-memory and on-disk records.
 */
static
int
hammer_vop_strategy_write(struct vop_strategy_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor *spike = NULL;
	hammer_inode_t ip;
	struct bio *bio;
	struct buf *bp;
	int error;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = ap->a_vp->v_data;

	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	hammer_start_transaction(&trans, ip->hmp);

retry:
	/*
	 * Delete any records overlapping our range.  This function will
	 * (eventually) properly truncate partial overlaps.
	 */
	if (ip->ino_rec.base.base.obj_type == HAMMER_OBJTYPE_DBFILE) {
		error = hammer_ip_delete_range(&trans, ip, bio->bio_offset,
					       bio->bio_offset, &spike);
	} else {
		error = hammer_ip_delete_range(&trans, ip, bio->bio_offset,
					       bio->bio_offset +
						bp->b_bufsize - 1,
					       &spike);
	}

	/*
	 * Add a single record to cover the write
	 */
	if (error == 0) {
		error = hammer_ip_sync_data(&trans, ip, bio->bio_offset,
					    bp->b_data, bp->b_bufsize,
					    &spike);
	}

	/*
	 * If we ran out of space the spike structure will be filled in
	 * and we must call hammer_spike with it, then retry.
	 */
	if (error == ENOSPC) {
		error = hammer_spike(&spike);
		if (error == 0)
			goto retry;
	}
	KKASSERT(spike == NULL);

	/*
	 * If an error occured abort the transaction
	 */
	if (error) {
		/* XXX undo deletion */
		hammer_abort_transaction(&trans);
		bp->b_resid = bp->b_bufsize;
	} else {
		hammer_commit_transaction(&trans);
		bp->b_resid = 0;
	}
	return(error);
}

/*
 * dounlink - disconnect a directory entry
 *
 * XXX whiteout support not really in yet
 */
static int
hammer_dounlink(struct nchandle *nch, struct vnode *dvp, struct ucred *cred,
		int flags)
{
	struct hammer_transaction trans;
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	hammer_record_ondisk_t rec;
	struct hammer_cursor cursor;
	int64_t namekey;
	int error;

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	dip = VTOI(dvp);
	ncp = nch->ncp;

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	namekey = hammer_directory_namekey(ncp->nc_name, ncp->nc_nlen);

	hammer_init_cursor_hmp(&cursor, &dip->cache[0], dip->hmp);
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key |= 0xFFFFFFFFULL;
	cursor.asof = dip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	hammer_start_transaction(&trans, dip->hmp);

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.  info->last_error contains the
	 * error code on search termination and could be 0, ENOENT, or
	 * something else.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor, dip);
	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		rec = cursor.record;
		if (ncp->nc_nlen == rec->entry.base.data_len &&
		    bcmp(ncp->nc_name, cursor.data, ncp->nc_nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 *
	 * If the target is a directory, it must be empty.
	 */
	if (error == 0) {
		ip = hammer_get_inode(dip->hmp, &dip->cache[1],
				      rec->entry.obj_id,
				      dip->hmp->asof, 0, &error);
		if (error == 0 && ip->ino_rec.base.base.obj_type ==
				  HAMMER_OBJTYPE_DIRECTORY) {
			error = hammer_ip_check_directory_empty(&trans, ip);
		}
		if (error == 0)
			error = hammer_ip_del_directory(&trans, &cursor, dip, ip);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, NULL);
			/* XXX locking */
			if (ip->vp)
				cache_inval_vp(ip->vp, CINV_DESTROY);
		}
		hammer_rel_inode(ip, 0);
	}

	if (error == 0)
		hammer_commit_transaction(&trans);
	else
		hammer_abort_transaction(&trans);
        hammer_done_cursor(&cursor);
	return (error);
}

/************************************************************************
 *			    FIFO AND SPECFS OPS				*
 ************************************************************************
 *
 */

static int
hammer_vop_fifoclose (struct vop_close_args *ap)
{
	/* XXX update itimes */
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

static int
hammer_vop_fiforead (struct vop_read_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static int
hammer_vop_fifowrite (struct vop_write_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static int
hammer_vop_specclose (struct vop_close_args *ap)
{
	/* XXX update itimes */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

static int
hammer_vop_specread (struct vop_read_args *ap)
{
	/* XXX update access time */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

static int
hammer_vop_specwrite (struct vop_write_args *ap)
{
	/* XXX update last change time */
	return (VOCALL(&spec_vnode_vops, &ap->a_head));
}

