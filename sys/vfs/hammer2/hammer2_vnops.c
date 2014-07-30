/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression) 
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
/*
 * Kernel Filesystem interface
 *
 * NOTE! local ipdata pointers must be reloaded on any modifying operation
 *	 to the inode as its underlying chain may have changed.
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
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>

#include "hammer2.h"
#include "hammer2_lz4.h"

#include "zlib/hammer2_zlib.h"

#define ZFOFFSET	(-2LL)

static int hammer2_read_file(hammer2_inode_t *ip, struct uio *uio,
				int seqcount);
static int hammer2_write_file(hammer2_inode_t *ip, struct uio *uio,
				int ioflag, int seqcount);
static void hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize);
static void hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize);

struct objcache *cache_buffer_read;
struct objcache *cache_buffer_write;

/* 
 * Callback used in read path in case that a block is compressed with LZ4.
 */
static
void
hammer2_decompress_LZ4_callback(const char *data, u_int bytes, struct bio *bio)
{
	struct buf *bp;
	char *compressed_buffer;
	int compressed_size;
	int result;

	bp = bio->bio_buf;

#if 0
	if bio->bio_caller_info2.index &&
	      bio->bio_caller_info1.uvalue32 !=
	      crc32(bp->b_data, bp->b_bufsize) --- return error
#endif

	KKASSERT(bp->b_bufsize <= HAMMER2_PBUFSIZE);
	compressed_size = *(const int *)data;
	KKASSERT(compressed_size <= bytes - sizeof(int));

	compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
	result = LZ4_decompress_safe(__DECONST(char *, &data[sizeof(int)]),
				     compressed_buffer,
				     compressed_size,
				     bp->b_bufsize);
	if (result < 0) {
		kprintf("READ PATH: Error during decompression."
			"bio %016jx/%d\n",
			(intmax_t)bio->bio_offset, bytes);
		/* make sure it isn't random garbage */
		bzero(compressed_buffer, bp->b_bufsize);
	}
	KKASSERT(result <= bp->b_bufsize);
	bcopy(compressed_buffer, bp->b_data, bp->b_bufsize);
	if (result < bp->b_bufsize)
		bzero(bp->b_data + result, bp->b_bufsize - result);
	objcache_put(cache_buffer_read, compressed_buffer);
	bp->b_resid = 0;
	bp->b_flags |= B_AGE;
}

/*
 * Callback used in read path in case that a block is compressed with ZLIB.
 * It is almost identical to LZ4 callback, so in theory they can be unified,
 * but we didn't want to make changes in bio structure for that.
 */
static
void
hammer2_decompress_ZLIB_callback(const char *data, u_int bytes, struct bio *bio)
{
	struct buf *bp;
	char *compressed_buffer;
	z_stream strm_decompress;
	int result;
	int ret;

	bp = bio->bio_buf;

	KKASSERT(bp->b_bufsize <= HAMMER2_PBUFSIZE);
	strm_decompress.avail_in = 0;
	strm_decompress.next_in = Z_NULL;

	ret = inflateInit(&strm_decompress);

	if (ret != Z_OK)
		kprintf("HAMMER2 ZLIB: Fatal error in inflateInit.\n");

	compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
	strm_decompress.next_in = __DECONST(char *, data);

	/* XXX supply proper size, subset of device bp */
	strm_decompress.avail_in = bytes;
	strm_decompress.next_out = compressed_buffer;
	strm_decompress.avail_out = bp->b_bufsize;

	ret = inflate(&strm_decompress, Z_FINISH);
	if (ret != Z_STREAM_END) {
		kprintf("HAMMER2 ZLIB: Fatar error during decompression.\n");
		bzero(compressed_buffer, bp->b_bufsize);
	}
	bcopy(compressed_buffer, bp->b_data, bp->b_bufsize);
	result = bp->b_bufsize - strm_decompress.avail_out;
	if (result < bp->b_bufsize)
		bzero(bp->b_data + result, strm_decompress.avail_out);
	objcache_put(cache_buffer_read, compressed_buffer);
	ret = inflateEnd(&strm_decompress);

	bp->b_resid = 0;
	bp->b_flags |= B_AGE;
}

static __inline
void
hammer2_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}

/*
 * Last reference to a vnode is going away but it is still cached.
 */
static
int
hammer2_vop_inactive(struct vop_inactive_args *ap)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cluster;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(vp);
		return (0);
	}

	/*
	 * Detect updates to the embedded data which may be synchronized by
	 * the strategy code.  Simply mark the inode modified so it gets
	 * picked up by our normal flush.
	 */
	cluster = hammer2_inode_lock_ex(ip);
	KKASSERT(cluster);
	ripdata = &hammer2_cluster_data(cluster)->ipdata;

	/*
	 * Check for deleted inodes and recycle immediately.
	 */
	if (ripdata->nlinks == 0) {
		hammer2_key_t lbase;
		int nblksize;

		nblksize = hammer2_calc_logical(ip, 0, &lbase, NULL);
		nvtruncbuf(vp, 0, nblksize, 0, 0);

		hammer2_inode_unlock_ex(ip, cluster);
		vrecycle(vp);
	} else {
		hammer2_inode_unlock_ex(ip, cluster);
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
	const hammer2_inode_data_t *ripdata;
	hammer2_cluster_t *cluster;
	hammer2_inode_t *ip;
	hammer2_pfsmount_t *pmp;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL)
		return(0);

	/*
	 * Inode must be locked for reclaim.
	 */
	pmp = ip->pmp;
	cluster = hammer2_inode_lock_ex(ip);
	ripdata = &hammer2_cluster_data(cluster)->ipdata;

	/*
	 * The final close of a deleted file or directory marks it for
	 * destruction.  The DELETED flag allows the flusher to shortcut
	 * any modified blocks still unflushed (that is, just ignore them).
	 *
	 * HAMMER2 usually does not try to optimize the freemap by returning
	 * deleted blocks to it as it does not usually know how many snapshots
	 * might be referencing portions of the file/dir.
	 */
	vp->v_data = NULL;
	ip->vp = NULL;

	/*
	 * NOTE! We do not attempt to flush chains here, flushing is
	 *	 really fragile and could also deadlock.
	 */
	vclrisdirty(vp);

	/*
	 * A reclaim can occur at any time so we cannot safely start a
	 * transaction to handle reclamation of unlinked files.  Instead,
	 * the ip is left with a reference and placed on a linked list and
	 * handled later on.
	 */
	if (ripdata->nlinks == 0) {
		hammer2_inode_unlink_t *ipul;

		ipul = kmalloc(sizeof(*ipul), pmp->minode, M_WAITOK | M_ZERO);
		ipul->ip = ip;

		spin_lock(&pmp->list_spin);
		TAILQ_INSERT_TAIL(&pmp->unlinkq, ipul, entry);
		spin_unlock(&pmp->list_spin);
		hammer2_inode_unlock_ex(ip, cluster);	/* unlock */
		/* retain ref from vp for ipul */
	} else {
		hammer2_inode_unlock_ex(ip, cluster);	/* unlock */
		hammer2_inode_drop(ip);			/* vp ref */
	}
	/* cluster no longer referenced */
	/* cluster = NULL; not needed */

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
	hammer2_inode_t *ip;
	hammer2_trans_t trans;
	hammer2_cluster_t *cluster;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

#if 0
	/* XXX can't do this yet */
	hammer2_trans_init(&trans, ip->pmp, HAMMER2_TRANS_ISFLUSH);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);
#endif
	hammer2_trans_init(&trans, ip->pmp, 0);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);

	/*
	 * Calling chain_flush here creates a lot of duplicative
	 * COW operations due to non-optimal vnode ordering.
	 *
	 * Only do it for an actual fsync() syscall.  The other forms
	 * which call this function will eventually call chain_flush
	 * on the volume root as a catch-all, which is far more optimal.
	 */
	cluster = hammer2_inode_lock_ex(ip);
	atomic_clear_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	vclrisdirty(vp);
	if (ip->flags & (HAMMER2_INODE_RESIZED|HAMMER2_INODE_MTIME))
		hammer2_inode_fsync(&trans, ip, cluster);

#if 0
	/*
	 * XXX creates discontinuity w/modify_tid
	 */
	if (ap->a_flags & VOP_FSYNC_SYSCALL) {
		hammer2_flush(&trans, cluster);
	}
#endif
	hammer2_inode_unlock_ex(ip, cluster);
	hammer2_trans_done(&trans);

	return (0);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	const hammer2_inode_data_t *ipdata;
	hammer2_cluster_t *cluster;
	uid_t uid;
	gid_t gid;
	int error;

	cluster = hammer2_inode_lock_sh(ip);
	ipdata = &hammer2_cluster_data(cluster)->ipdata;
	uid = hammer2_to_unix_xid(&ipdata->uid);
	gid = hammer2_to_unix_xid(&ipdata->gid);
	error = vop_helper_access(ap, uid, gid, ipdata->mode, ipdata->uflags);
	hammer2_inode_unlock_sh(ip, cluster);

	return (error);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	const hammer2_inode_data_t *ipdata;
	hammer2_cluster_t *cluster;
	hammer2_pfsmount_t *pmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	pmp = ip->pmp;

	cluster = hammer2_inode_lock_sh(ip);
	ipdata = &hammer2_cluster_data(cluster)->ipdata;
	KKASSERT(hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE);

	vap->va_fsid = pmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ipdata->inum;
	vap->va_mode = ipdata->mode;
	vap->va_nlink = ipdata->nlinks;
	vap->va_uid = hammer2_to_unix_xid(&ipdata->uid);
	vap->va_gid = hammer2_to_unix_xid(&ipdata->gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->size;	/* protected by shared lock */
	vap->va_blocksize = HAMMER2_PBUFSIZE;
	vap->va_flags = ipdata->uflags;
	hammer2_time_to_timespec(ipdata->ctime, &vap->va_ctime);
	hammer2_time_to_timespec(ipdata->mtime, &vap->va_mtime);
	hammer2_time_to_timespec(ipdata->mtime, &vap->va_atime);
	vap->va_gen = 1;
	vap->va_bytes = vap->va_size;	/* XXX */
	vap->va_type = hammer2_get_vtype(ipdata);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ipdata->uid;
	vap->va_gid_uuid = ipdata->gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock_sh(ip, cluster);

	return (0);
}

static
int
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_data_t *wipdata;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cluster;
	hammer2_trans_t trans;
	struct vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	int domtime = 0;
	int dosync = 0;
	uint64_t ctime;

	vp = ap->a_vp;
	vap = ap->a_vap;
	hammer2_update_time(&ctime);

	ip = VTOI(vp);

	if (ip->pmp->ronly)
		return(EROFS);

	hammer2_pfs_memory_wait(ip->pmp);
	hammer2_trans_init(&trans, ip->pmp, 0);
	cluster = hammer2_inode_lock_ex(ip);
	ripdata = &hammer2_cluster_data(cluster)->ipdata;
	error = 0;

	if (vap->va_flags != VNOVAL) {
		u_int32_t flags;

		flags = ripdata->uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer2_to_unix_xid(&ripdata->uid),
					 ap->a_cred);
		if (error == 0) {
			if (ripdata->uflags != flags) {
				wipdata = hammer2_cluster_modify_ip(&trans, ip,
								    cluster, 0);
				wipdata->uflags = flags;
				wipdata->ctime = ctime;
				kflags |= NOTE_ATTRIB;
				dosync = 1;
				ripdata = wipdata;
			}
			if (ripdata->uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ripdata->uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ripdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ripdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ripdata->gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer2_guid_to_uuid(&uuid_uid, cur_uid);
			hammer2_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ripdata->uid, sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ripdata->gid, sizeof(uuid_gid)) ||
			    ripdata->mode != cur_mode
			) {
				wipdata = hammer2_cluster_modify_ip(&trans, ip,
								    cluster, 0);
				wipdata->uid = uuid_uid;
				wipdata->gid = uuid_gid;
				wipdata->mode = cur_mode;
				wipdata->ctime = ctime;
				dosync = 1;
				ripdata = wipdata;
			}
			kflags |= NOTE_ATTRIB;
		}
	}

	/*
	 * Resize the file
	 */
	if (vap->va_size != VNOVAL && ip->size != vap->va_size) {
		switch(vp->v_type) {
		case VREG:
			if (vap->va_size == ip->size)
				break;
			hammer2_inode_unlock_ex(ip, cluster);
			if (vap->va_size < ip->size) {
				hammer2_truncate_file(ip, vap->va_size);
			} else {
				hammer2_extend_file(ip, vap->va_size);
			}
			cluster = hammer2_inode_lock_ex(ip);
			/* RELOAD */
			ripdata = &hammer2_cluster_data(cluster)->ipdata;
			domtime = 1;
			break;
		default:
			error = EINVAL;
			goto done;
		}
	}
#if 0
	/* atime not supported */
	if (vap->va_atime.tv_sec != VNOVAL) {
		wipdata = hammer2_cluster_modify_ip(&trans, ip, cluster, 0);
		wipdata->atime = hammer2_timespec_to_time(&vap->va_atime);
		kflags |= NOTE_ATTRIB;
		dosync = 1;
		ripdata = wipdata;
	}
#endif
	if (vap->va_mtime.tv_sec != VNOVAL) {
		wipdata = hammer2_cluster_modify_ip(&trans, ip, cluster, 0);
		wipdata->mtime = hammer2_timespec_to_time(&vap->va_mtime);
		kflags |= NOTE_ATTRIB;
		domtime = 0;
		dosync = 1;
		ripdata = wipdata;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t cur_mode = ripdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ripdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ripdata->gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ripdata->mode != cur_mode) {
			wipdata = hammer2_cluster_modify_ip(&trans, ip,
							    cluster, 0);
			wipdata->mode = cur_mode;
			wipdata->ctime = ctime;
			kflags |= NOTE_ATTRIB;
			dosync = 1;
			ripdata = wipdata;
		}
	}

	/*
	 * If a truncation occurred we must call inode_fsync() now in order
	 * to trim the related data chains, otherwise a later expansion can
	 * cause havoc.
	 */
	if (dosync) {
		hammer2_cluster_modsync(cluster);
		dosync = 0;
	}
	hammer2_inode_fsync(&trans, ip, cluster);

	/*
	 * Cleanup.  If domtime is set an additional inode modification
	 * must be flagged.  All other modifications will have already
	 * set INODE_MODIFIED and called vsetisdirty().
	 */
done:
	if (domtime) {
		atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED |
					   HAMMER2_INODE_MTIME);
		vsetisdirty(ip->vp);
	}
	if (dosync)
		hammer2_cluster_modsync(cluster);
	hammer2_inode_unlock_ex(ip, cluster);
	hammer2_trans_done(&trans);
	hammer2_knote(ip->vp, kflags);

	return (error);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	const hammer2_inode_data_t *ipdata;
	hammer2_inode_t *ip;
	hammer2_inode_t *xip;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *xcluster;
	hammer2_blockref_t bref;
	hammer2_tid_t inum;
	hammer2_key_t key_next;
	hammer2_key_t lkey;
	struct uio *uio;
	off_t *cookies;
	off_t saveoff;
	int cookie_index;
	int ncookies;
	int error;
	int dtype;
	int ddflag;
	int r;

	ip = VTOI(ap->a_vp);
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

	cparent = hammer2_inode_lock_sh(ip);
	ipdata = &hammer2_cluster_data(cparent)->ipdata;

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
	cluster = (void *)(intptr_t)-1;	/* non-NULL for early goto done case */

	if (saveoff == 0) {
		inum = ipdata->inum & HAMMER2_DIRHASH_USERMSK;
		r = vop_write_dirent(&error, uio, inum, DT_DIR, 1, ".");
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
		/*
		 * Be careful with lockorder when accessing ".."
		 *
		 * (ip is the current dir. xip is the parent dir).
		 */
		inum = ipdata->inum & HAMMER2_DIRHASH_USERMSK;
		while (ip->pip != NULL && ip != ip->pmp->iroot) {
			xip = ip->pip;
			hammer2_inode_ref(xip);
			hammer2_inode_unlock_sh(ip, cparent);
			xcluster = hammer2_inode_lock_sh(xip);
			cparent = hammer2_inode_lock_sh(ip);
			hammer2_inode_drop(xip);
			ipdata = &hammer2_cluster_data(cparent)->ipdata;
			if (xip == ip->pip) {
				inum = hammer2_cluster_data(xcluster)->
					ipdata.inum & HAMMER2_DIRHASH_USERMSK;
				hammer2_inode_unlock_sh(xip, xcluster);
				break;
			}
			hammer2_inode_unlock_sh(xip, xcluster);
		}
		r = vop_write_dirent(&error, uio, inum, DT_DIR, 2, "..");
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
	if (hammer2_debug & 0x0020)
		kprintf("readdir: lkey %016jx\n", lkey);

	/*
	 * parent is the inode cluster, already locked for us.  Don't
	 * double lock shared locks as this will screw up upgrades.
	 */
	if (error) {
		goto done;
	}
	cluster = hammer2_cluster_lookup(cparent, &key_next, lkey, lkey,
				     HAMMER2_LOOKUP_SHARED, &ddflag);
	if (cluster == NULL) {
		cluster = hammer2_cluster_lookup(cparent, &key_next,
					     lkey, (hammer2_key_t)-1,
					     HAMMER2_LOOKUP_SHARED, &ddflag);
	}
	if (cluster)
		hammer2_cluster_bref(cluster, &bref);
	while (cluster) {
		if (hammer2_debug & 0x0020)
			kprintf("readdir: p=%p chain=%p %016jx (next %016jx)\n",
				cparent->focus, cluster->focus,
				bref.key, key_next);

		if (bref.type == HAMMER2_BREF_TYPE_INODE) {
			ipdata = &hammer2_cluster_data(cluster)->ipdata;
			dtype = hammer2_get_dtype(ipdata);
			saveoff = bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     ipdata->inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype,
					     ipdata->name_len,
					     ipdata->filename);
			if (r)
				break;
			if (cookies)
				cookies[cookie_index] = saveoff;
			++cookie_index;
		} else {
			/* XXX chain error */
			kprintf("bad chain type readdir %d\n", bref.type);
		}

		/*
		 * Keys may not be returned in order so once we have a
		 * placemarker (cluster) the scan must allow the full range
		 * or some entries will be missed.
		 */
		cluster = hammer2_cluster_next(cparent, cluster, &key_next,
					       key_next, (hammer2_key_t)-1,
					       HAMMER2_LOOKUP_SHARED);
		if (cluster) {
			hammer2_cluster_bref(cluster, &bref);
			saveoff = (bref.key & HAMMER2_DIRHASH_USERMSK) + 1;
		} else {
			saveoff = (hammer2_key_t)-1;
		}
		if (cookie_index == ncookies)
			break;
	}
	if (cluster)
		hammer2_cluster_unlock(cluster);
done:
	hammer2_inode_unlock_sh(ip, cparent);
	if (ap->a_eofflag)
		*ap->a_eofflag = (cluster == NULL);
	if (hammer2_debug & 0x0020)
		kprintf("readdir: done at %016jx\n", saveoff);
	uio->uio_offset = saveoff & ~HAMMER2_DIRHASH_VISIBLE;
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

/*
 * hammer2_vop_readlink { vp, uio, cred }
 */
static
int
hammer2_vop_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp;
	hammer2_inode_t *ip;
	int error;

	vp = ap->a_vp;
	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);

	error = hammer2_read_file(ip, ap->a_uio, 0);
	return (error);
}

static
int
hammer2_vop_read(struct vop_read_args *ap)
{
	struct vnode *vp;
	hammer2_inode_t *ip;
	struct uio *uio;
	int error;
	int seqcount;
	int bigread;

	/*
	 * Read operations supported on this vnode?
	 */
	vp = ap->a_vp;
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Misc
	 */
	ip = VTOI(vp);
	uio = ap->a_uio;
	error = 0;

	seqcount = ap->a_ioflag >> 16;
	bigread = (uio->uio_resid > 100 * 1024 * 1024);

	error = hammer2_read_file(ip, uio, seqcount);
	return (error);
}

static
int
hammer2_vop_write(struct vop_write_args *ap)
{
	hammer2_inode_t *ip;
	hammer2_trans_t trans;
	thread_t td;
	struct vnode *vp;
	struct uio *uio;
	int error;
	int seqcount;
	int bigwrite;

	/*
	 * Read operations supported on this vnode?
	 */
	vp = ap->a_vp;
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * Misc
	 */
	ip = VTOI(vp);
	uio = ap->a_uio;
	error = 0;
	if (ip->pmp->ronly)
		return (EROFS);

	seqcount = ap->a_ioflag >> 16;
	bigwrite = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * Check resource limit
	 */
	if (uio->uio_resid > 0 && (td = uio->uio_td) != NULL && td->td_proc &&
	    uio->uio_offset + uio->uio_resid >
	     td->td_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		lwpsignal(td->td_proc, td->td_lwp, SIGXFSZ);
		return (EFBIG);
	}

	bigwrite = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * The transaction interlocks against flushes initiations
	 * (note: but will run concurrently with the actual flush).
	 */
	hammer2_trans_init(&trans, ip->pmp, 0);
	error = hammer2_write_file(ip, uio, ap->a_ioflag, seqcount);
	hammer2_trans_done(&trans);

	return (error);
}

/*
 * Perform read operations on a file or symlink given an UNLOCKED
 * inode and uio.
 *
 * The passed ip is not locked.
 */
static
int
hammer2_read_file(hammer2_inode_t *ip, struct uio *uio, int seqcount)
{
	hammer2_off_t size;
	struct buf *bp;
	int error;

	error = 0;

	/*
	 * UIO read loop.
	 */
	ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);
	size = ip->size;
	ccms_thread_unlock(&ip->topo_cst);

	while (uio->uio_resid > 0 && uio->uio_offset < size) {
		hammer2_key_t lbase;
		hammer2_key_t leof;
		int lblksize;
		int loff;
		int n;

		lblksize = hammer2_calc_logical(ip, uio->uio_offset,
						&lbase, &leof);

		error = cluster_read(ip->vp, leof, lbase, lblksize,
				     uio->uio_resid, seqcount * BKVASIZE,
				     &bp);

		if (error)
			break;
		loff = (int)(uio->uio_offset - lbase);
		n = lblksize - loff;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > size - uio->uio_offset)
			n = (int)(size - uio->uio_offset);
		bp->b_flags |= B_AGE;
		uiomove((char *)bp->b_data + loff, n, uio);
		bqrelse(bp);
	}
	return (error);
}

/*
 * Write to the file represented by the inode via the logical buffer cache.
 * The inode may represent a regular file or a symlink.
 *
 * The inode must not be locked.
 */
static
int
hammer2_write_file(hammer2_inode_t *ip,
		   struct uio *uio, int ioflag, int seqcount)
{
	hammer2_key_t old_eof;
	hammer2_key_t new_eof;
	struct buf *bp;
	int kflags;
	int error;
	int modified;

	/*
	 * Setup if append
	 */
	ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);
	if (ioflag & IO_APPEND)
		uio->uio_offset = ip->size;
	old_eof = ip->size;
	ccms_thread_unlock(&ip->topo_cst);

	/*
	 * Extend the file if necessary.  If the write fails at some point
	 * we will truncate it back down to cover as much as we were able
	 * to write.
	 *
	 * Doing this now makes it easier to calculate buffer sizes in
	 * the loop.
	 */
	kflags = 0;
	error = 0;
	modified = 0;

	if (uio->uio_offset + uio->uio_resid > old_eof) {
		new_eof = uio->uio_offset + uio->uio_resid;
		modified = 1;
		hammer2_extend_file(ip, new_eof);
		kflags |= NOTE_EXTEND;
	} else {
		new_eof = old_eof;
	}
	
	/*
	 * UIO write loop
	 */
	while (uio->uio_resid > 0) {
		hammer2_key_t lbase;
		int trivial;
		int endofblk;
		int lblksize;
		int loff;
		int n;

		/*
		 * Don't allow the buffer build to blow out the buffer
		 * cache.
		 */
		if ((ioflag & IO_RECURSE) == 0)
			bwillwrite(HAMMER2_PBUFSIZE);

		/*
		 * This nominally tells us how much we can cluster and
		 * what the logical buffer size needs to be.  Currently
		 * we don't try to cluster the write and just handle one
		 * block at a time.
		 */
		lblksize = hammer2_calc_logical(ip, uio->uio_offset,
						&lbase, NULL);
		loff = (int)(uio->uio_offset - lbase);
		
		KKASSERT(lblksize <= 65536);

		/*
		 * Calculate bytes to copy this transfer and whether the
		 * copy completely covers the buffer or not.
		 */
		trivial = 0;
		n = lblksize - loff;
		if (n > uio->uio_resid) {
			n = uio->uio_resid;
			if (loff == lbase && uio->uio_offset + n == new_eof)
				trivial = 1;
			endofblk = 0;
		} else {
			if (loff == 0)
				trivial = 1;
			endofblk = 1;
		}

		/*
		 * Get the buffer
		 */
		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(ip->vp, lbase, lblksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ip->vp, lbase, lblksize, &bp);
			}
		} else if (trivial) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a
			 * mmap/write visibility issue.
			 */
			bp = getblk(ip->vp, lbase, lblksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 *
			 * (The strategy code will detect zero-fill physical
			 * blocks for this case).
			 */
			error = bread(ip->vp, lbase, lblksize, &bp);
			if (error == 0)
				bheavy(bp);
		}

		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * Ok, copy the data in
		 */
		error = uiomove(bp->b_data + loff, n, uio);
		kflags |= NOTE_WRITE;
		modified = 1;
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * WARNING: Pageout daemon will issue UIO_NOCOPY writes
		 *	    with IO_SYNC or IO_ASYNC set.  These writes
		 *	    must be handled as the pageout daemon expects.
		 */
		if (ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ioflag & IO_DIRECT) && endofblk) {
			bawrite(bp);
		} else if (ioflag & IO_ASYNC) {
			bawrite(bp);
		} else {
			bdwrite(bp);
		}
	}

	/*
	 * Cleanup.  If we extended the file EOF but failed to write through
	 * the entire write is a failure and we have to back-up.
	 */
	if (error && new_eof != old_eof) {
		hammer2_truncate_file(ip, old_eof);
	} else if (modified) {
		ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);
		hammer2_update_time(&ip->mtime);
		atomic_set_int(&ip->flags, HAMMER2_INODE_MTIME);
		ccms_thread_unlock(&ip->topo_cst);
	}
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	hammer2_knote(ip->vp, kflags);
	vsetisdirty(ip->vp);

	return error;
}

/*
 * Truncate the size of a file.  The inode must not be locked.
 *
 * NOTE: Caller handles setting HAMMER2_INODE_MODIFIED
 */
static
void
hammer2_truncate_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_key_t lbase;
	int nblksize;

	if (ip->vp) {
		nblksize = hammer2_calc_logical(ip, nsize, &lbase, NULL);
		nvtruncbuf(ip->vp, nsize,
			   nblksize, (int)nsize & (nblksize - 1),
			   0);
	}
	ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);
	ip->size = nsize;
	atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);
	ccms_thread_unlock(&ip->topo_cst);
}

/*
 * Extend the size of a file.  The inode must not be locked.
 *
 * NOTE: Caller handles setting HAMMER2_INODE_MODIFIED
 */
static
void
hammer2_extend_file(hammer2_inode_t *ip, hammer2_key_t nsize)
{
	hammer2_key_t lbase;
	hammer2_key_t osize;
	int oblksize;
	int nblksize;

	ccms_thread_lock(&ip->topo_cst, CCMS_STATE_EXCLUSIVE);
	osize = ip->size;
	ip->size = nsize;
	ccms_thread_unlock(&ip->topo_cst);

	if (ip->vp) {
		oblksize = hammer2_calc_logical(ip, osize, &lbase, NULL);
		nblksize = hammer2_calc_logical(ip, nsize, &lbase, NULL);
		nvextendbuf(ip->vp,
			    osize, nsize,
			    oblksize, nblksize,
			    -1, -1, 0);
	}
	atomic_set_int(&ip->flags, HAMMER2_INODE_RESIZED);
}

static
int
hammer2_vop_nresolve(struct vop_nresolve_args *ap)
{
	hammer2_inode_t *ip;
	hammer2_inode_t *dip;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	const hammer2_inode_data_t *ipdata;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error = 0;
	int ddflag;
	struct vnode *vp;

	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	cparent = hammer2_inode_lock_sh(dip);
	cluster = hammer2_cluster_lookup(cparent, &key_next,
					 lhc, lhc + HAMMER2_DIRHASH_LOMASK,
					 HAMMER2_LOOKUP_SHARED, &ddflag);
	while (cluster) {
		if (hammer2_cluster_type(cluster) == HAMMER2_BREF_TYPE_INODE) {
			ipdata = &hammer2_cluster_data(cluster)->ipdata;
			if (ipdata->name_len == name_len &&
			    bcmp(ipdata->filename, name, name_len) == 0) {
				break;
			}
		}
		cluster = hammer2_cluster_next(cparent, cluster, &key_next,
					       key_next,
					       lhc + HAMMER2_DIRHASH_LOMASK,
					       HAMMER2_LOOKUP_SHARED);
	}
	hammer2_inode_unlock_sh(dip, cparent);

	/*
	 * Resolve hardlink entries before acquiring the inode.
	 */
	if (cluster) {
		ipdata = &hammer2_cluster_data(cluster)->ipdata;
		if (ipdata->type == HAMMER2_OBJTYPE_HARDLINK) {
			hammer2_tid_t inum = ipdata->inum;
			error = hammer2_hardlink_find(dip, NULL, cluster);
			if (error) {
				kprintf("hammer2: unable to find hardlink "
					"0x%016jx\n", inum);
				hammer2_cluster_unlock(cluster);
				return error;
			}
		}
	}

	/*
	 * nresolve needs to resolve hardlinks, the original cluster is not
	 * sufficient.
	 */
	if (cluster) {
		ip = hammer2_inode_get(dip->pmp, dip, cluster);
		ipdata = &hammer2_cluster_data(cluster)->ipdata;
		if (ipdata->type == HAMMER2_OBJTYPE_HARDLINK) {
			kprintf("nresolve: fixup hardlink\n");
			hammer2_inode_ref(ip);
			hammer2_inode_unlock_ex(ip, NULL);
			hammer2_cluster_unlock(cluster);
			cluster = hammer2_inode_lock_ex(ip);
			ipdata = &hammer2_cluster_data(cluster)->ipdata;
			kprintf("nresolve: fixup to type %02x\n", ipdata->type);
		}
	} else {
		ip = NULL;
	}

#if 0
	/*
	 * Deconsolidate any hardlink whos nlinks == 1.  Ignore errors.
	 * If an error occurs chain and ip are left alone.
	 *
	 * XXX upgrade shared lock?
	 */
	if (ochain && chain &&
	    chain->data->ipdata.nlinks == 1 && !dip->pmp->ronly) {
		kprintf("hammer2: need to unconsolidate hardlink for %s\n",
			chain->data->ipdata.filename);
		/* XXX retain shared lock on dip? (currently not held) */
		hammer2_trans_init(&trans, dip->pmp, 0);
		hammer2_hardlink_deconsolidate(&trans, dip, &chain, &ochain);
		hammer2_trans_done(&trans);
	}
#endif

	/*
	 * Acquire the related vnode
	 *
	 * NOTE: For error processing, only ENOENT resolves the namecache
	 *	 entry to NULL, otherwise we just return the error and
	 *	 leave the namecache unresolved.
	 *
	 * NOTE: multiple hammer2_inode structures can be aliased to the
	 *	 same chain element, for example for hardlinks.  This
	 *	 use case does not 'reattach' inode associations that
	 *	 might already exist, but always allocates a new one.
	 *
	 * WARNING: inode structure is locked exclusively via inode_get
	 *	    but chain was locked shared.  inode_unlock_ex()
	 *	    will handle it properly.
	 */
	if (cluster) {
		vp = hammer2_igetv(ip, cluster, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
		} else if (error == ENOENT) {
			cache_setvp(ap->a_nch, NULL);
		}
		hammer2_inode_unlock_ex(ip, cluster);

		/*
		 * The vp should not be released until after we've disposed
		 * of our locks, because it might cause vop_inactive() to
		 * be called.
		 */
		if (vp)
			vrele(vp);
	} else {
		error = ENOENT;
		cache_setvp(ap->a_nch, NULL);
	}
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, ap));
	return error;
}

static
int
hammer2_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cparent;
	int error;

	dip = VTOI(ap->a_dvp);

	if ((ip = dip->pip) == NULL) {
		*ap->a_vpp = NULL;
		return ENOENT;
	}
	cparent = hammer2_inode_lock_ex(ip);
	*ap->a_vpp = hammer2_igetv(ip, cparent, &error);
	hammer2_inode_unlock_ex(ip, cparent);

	return error;
}

static
int
hammer2_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *cluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	cluster = NULL;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &cluster, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, cluster, &error);
		hammer2_inode_unlock_ex(nip, cluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

/*
 * Return the largest contiguous physical disk range for the logical
 * request, in bytes.
 *
 * (struct vnode *vp, off_t loffset, off_t *doffsetp, int *runp, int *runb)
 *
 * Basically disabled, the logical buffer write thread has to deal with
 * buffers one-at-a-time.
 */
static
int
hammer2_vop_bmap(struct vop_bmap_args *ap)
{
	*ap->a_doffsetp = NOOFFSET;
	if (ap->a_runp)
		*ap->a_runp = 0;
	if (ap->a_runb)
		*ap->a_runb = 0;
	return (EOPNOTSUPP);
}

static
int
hammer2_vop_open(struct vop_open_args *ap)
{
	return vop_stdopen(ap);
}

/*
 * hammer2_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer2_vop_advlock(struct vop_advlock_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	const hammer2_inode_data_t *ipdata;
	hammer2_cluster_t *cparent;
	hammer2_off_t size;

	cparent = hammer2_inode_lock_sh(ip);
	ipdata = &hammer2_cluster_data(cparent)->ipdata;
	size = ipdata->size;
	hammer2_inode_unlock_sh(ip, cparent);
	return (lf_advlock(ap, &ip->advlock, size));
}


static
int
hammer2_vop_close(struct vop_close_args *ap)
{
	return vop_stdclose(ap);
}

/*
 * hammer2_vop_nlink { nch, dvp, vp, cred }
 *
 * Create a hardlink from (vp) to {dvp, nch}.
 */
static
int
hammer2_vop_nlink(struct vop_nlink_args *ap)
{
	hammer2_inode_t *fdip;	/* target directory to create link in */
	hammer2_inode_t *tdip;	/* target directory to create link in */
	hammer2_inode_t *cdip;	/* common parent directory */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *fdcluster;
	hammer2_cluster_t *tdcluster;
	hammer2_cluster_t *cdcluster;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	tdip = VTOI(ap->a_dvp);
	if (tdip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	/*
	 * ip represents the file being hardlinked.  The file could be a
	 * normal file or a hardlink target if it has already been hardlinked.
	 * If ip is a hardlinked target then ip->pip represents the location
	 * of the hardlinked target, NOT the location of the hardlink pointer.
	 *
	 * Bump nlinks and potentially also create or move the hardlink
	 * target in the parent directory common to (ip) and (tdip).  The
	 * consolidation code can modify ip->cluster and ip->pip.  The
	 * returned cluster is locked.
	 */
	ip = VTOI(ap->a_vp);
	hammer2_pfs_memory_wait(ip->pmp);
	hammer2_trans_init(&trans, ip->pmp, HAMMER2_TRANS_NEWINODE);

	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 */
	fdip = ip->pip;
	cdip = hammer2_inode_common_parent(fdip, tdip);
	cdcluster = hammer2_inode_lock_ex(cdip);
	fdcluster = hammer2_inode_lock_ex(fdip);
	tdcluster = hammer2_inode_lock_ex(tdip);
	cluster = hammer2_inode_lock_ex(ip);
	error = hammer2_hardlink_consolidate(&trans, ip, &cluster,
					     cdip, cdcluster, 1);
	if (error)
		goto done;

	/*
	 * Create a directory entry connected to the specified cluster.
	 *
	 * WARNING! chain can get moved by the connect (indirectly due to
	 *	    potential indirect block creation).
	 */
	error = hammer2_inode_connect(&trans, &cluster, 1,
				      tdip, tdcluster,
				      name, name_len, 0);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
done:
	hammer2_inode_unlock_ex(ip, cluster);
	hammer2_inode_unlock_ex(tdip, tdcluster);
	hammer2_inode_unlock_ex(fdip, fdcluster);
	hammer2_inode_unlock_ex(cdip, cdcluster);
	hammer2_trans_done(&trans);

	return error;
}

/*
 * hammer2_vop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer2_vop_ncreate(struct vop_ncreate_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *ncluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncluster = NULL;

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &ncluster, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, ncluster, &error);
		hammer2_inode_unlock_ex(nip, ncluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

/*
 * Make a device node (typically a fifo)
 */
static
int
hammer2_vop_nmknod(struct vop_nmknod_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_cluster_t *ncluster;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncluster = NULL;

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &ncluster, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, ncluster, &error);
		hammer2_inode_unlock_ex(nip, ncluster);
	}
	hammer2_trans_done(&trans);

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
	}
	return error;
}

/*
 * hammer2_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer2_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_cluster_t *ncparent;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;
	
	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return (EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, HAMMER2_TRANS_NEWINODE);
	ncparent = NULL;

	ap->a_vap->va_type = VLNK;	/* enforce type */

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &ncparent, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		hammer2_trans_done(&trans);
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, ncparent, &error);

	/*
	 * Build the softlink (~like file data) and finalize the namecache.
	 */
	if (error == 0) {
		size_t bytes;
		struct uio auio;
		struct iovec aiov;
		hammer2_inode_data_t *nipdata;

		nipdata = &hammer2_cluster_wdata(ncparent)->ipdata;
		/* nipdata = &nip->chain->data->ipdata;XXX */
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER2_EMBEDDED_BYTES) {
			KKASSERT(nipdata->op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			bcopy(ap->a_target, nipdata->u.data, bytes);
			nipdata->size = bytes;
			nip->size = bytes;
			hammer2_cluster_modsync(ncparent);
			hammer2_inode_unlock_ex(nip, ncparent);
			/* nipdata = NULL; not needed */
		} else {
			hammer2_inode_unlock_ex(nip, ncparent);
			/* nipdata = NULL; not needed */
			bzero(&auio, sizeof(auio));
			bzero(&aiov, sizeof(aiov));
			auio.uio_iov = &aiov;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_resid = bytes;
			auio.uio_iovcnt = 1;
			auio.uio_td = curthread;
			aiov.iov_base = ap->a_target;
			aiov.iov_len = bytes;
			error = hammer2_write_file(nip, &auio, IO_APPEND, 0);
			/* XXX handle error */
			error = 0;
		}
	} else {
		hammer2_inode_unlock_ex(nip, ncparent);
	}
	hammer2_trans_done(&trans);

	/*
	 * Finalize namecache
	 */
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *ap->a_vpp);
		/* hammer2_knote(ap->a_dvp, NOTE_WRITE); */
	}
	return error;
}

/*
 * hammer2_vop_nremove { nch, dvp, cred }
 */
static
int
hammer2_vop_nremove(struct vop_nremove_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return(EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	error = hammer2_unlink_file(&trans, dip, name, name_len,
				    0, NULL, ap->a_nch, -1);
	hammer2_trans_done(&trans);
	if (error == 0)
		cache_unlink(ap->a_nch);
	return (error);
}

/*
 * hammer2_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer2_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_trans_t trans;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error;

	dip = VTOI(ap->a_dvp);
	if (dip->pmp->ronly)
		return(EROFS);

	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;

	hammer2_pfs_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	hammer2_run_unlinkq(&trans, dip->pmp);
	error = hammer2_unlink_file(&trans, dip, name, name_len,
				    1, NULL, ap->a_nch, -1);
	hammer2_trans_done(&trans);
	if (error == 0)
		cache_unlink(ap->a_nch);
	return (error);
}

/*
 * hammer2_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer2_vop_nrename(struct vop_nrename_args *ap)
{
	struct namecache *fncp;
	struct namecache *tncp;
	hammer2_inode_t *cdip;
	hammer2_inode_t *fdip;
	hammer2_inode_t *tdip;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *fdcluster;
	hammer2_cluster_t *tdcluster;
	hammer2_cluster_t *cdcluster;
	hammer2_trans_t trans;
	const uint8_t *fname;
	size_t fname_len;
	const uint8_t *tname;
	size_t tname_len;
	int error;
	int hlink;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);	/* source directory */
	tdip = VTOI(ap->a_tdvp);	/* target directory */

	if (fdip->pmp->ronly)
		return(EROFS);

	fncp = ap->a_fnch->ncp;		/* entry name in source */
	fname = fncp->nc_name;
	fname_len = fncp->nc_nlen;

	tncp = ap->a_tnch->ncp;		/* entry name in target */
	tname = tncp->nc_name;
	tname_len = tncp->nc_nlen;

	hammer2_pfs_memory_wait(tdip->pmp);
	hammer2_trans_init(&trans, tdip->pmp, 0);

	/*
	 * ip is the inode being renamed.  If this is a hardlink then
	 * ip represents the actual file and not the hardlink marker.
	 */
	ip = VTOI(fncp->nc_vp);
	cluster = NULL;


	/*
	 * The common parent directory must be locked first to avoid deadlocks.
	 * Also note that fdip and/or tdip might match cdip.
	 *
	 * WARNING! fdip may not match ip->pip.  That is, if the source file
	 *	    is already a hardlink then what we are renaming is the
	 *	    hardlink pointer, not the hardlink itself.  The hardlink
	 *	    directory (ip->pip) will already be at a common parent
	 *	    of fdrip.
	 *
	 *	    Be sure to use ip->pip when finding the common parent
	 *	    against tdip or we might accidently move the hardlink
	 *	    target into a subdirectory that makes it inaccessible to
	 *	    other pointers.
	 */
	cdip = hammer2_inode_common_parent(ip->pip, tdip);
	cdcluster = hammer2_inode_lock_ex(cdip);
	fdcluster = hammer2_inode_lock_ex(fdip);
	tdcluster = hammer2_inode_lock_ex(tdip);

	/*
	 * Keep a tight grip on the inode so the temporary unlinking from
	 * the source location prior to linking to the target location
	 * does not cause the cluster to be destroyed.
	 *
	 * NOTE: To avoid deadlocks we cannot lock (ip) while we are
	 *	 unlinking elements from their directories.  Locking
	 *	 the nlinks field does not lock the whole inode.
	 */
	hammer2_inode_ref(ip);

	/*
	 * Remove target if it exists
	 */
	error = hammer2_unlink_file(&trans, tdip, tname, tname_len,
				    -1, NULL, ap->a_tnch, -1);
	if (error && error != ENOENT)
		goto done;
	cache_setunresolved(ap->a_tnch);

	/*
	 * When renaming a hardlinked file we may have to re-consolidate
	 * the location of the hardlink target.
	 *
	 * If ip represents a regular file the consolidation code essentially
	 * does nothing other than return the same locked cluster that was
	 * passed in.
	 *
	 * The returned cluster will be locked.
	 *
	 * WARNING!  We do not currently have a local copy of ipdata but
	 *	     we do use one later remember that it must be reloaded
	 *	     on any modification to the inode, including connects.
	 */
	cluster = hammer2_inode_lock_ex(ip);
	error = hammer2_hardlink_consolidate(&trans, ip, &cluster,
					     cdip, cdcluster, 0);
	if (error)
		goto done;

	/*
	 * Disconnect (fdip, fname) from the source directory.  This will
	 * disconnect (ip) if it represents a direct file.  If (ip) represents
	 * a hardlink the HARDLINK pointer object will be removed but the
	 * hardlink will stay intact.
	 *
	 * Always pass nch as NULL because we intend to reconnect the inode,
	 * so we don't want hammer2_unlink_file() to rename it to the hidden
	 * open-but-unlinked directory.
	 *
	 * The target cluster may be marked DELETED but will not be destroyed
	 * since we retain our hold on ip and cluster.
	 *
	 * NOTE: We pass nlinks as 0 (not -1) in order to retain the file's
	 *	 link count.
	 */
	error = hammer2_unlink_file(&trans, fdip, fname, fname_len,
				    -1, &hlink, NULL, 0);
	KKASSERT(error != EAGAIN);
	if (error)
		goto done;

	/*
	 * Reconnect ip to target directory using cluster.  Chains cannot
	 * actually be moved, so this will duplicate the cluster in the new
	 * spot and assign it to the ip, replacing the old cluster.
	 *
	 * WARNING: Because recursive locks are allowed and we unlinked the
	 *	    file that we have a cluster-in-hand for just above, the
	 *	    cluster might have been delete-duplicated.  We must
	 *	    refactor the cluster.
	 *
	 * WARNING: Chain locks can lock buffer cache buffers, to avoid
	 *	    deadlocks we want to unlock before issuing a cache_*()
	 *	    op (that might have to lock a vnode).
	 *
	 * NOTE:    Pass nlinks as 0 because we retained the link count from
	 *	    the unlink, so we do not have to modify it.
	 */
	error = hammer2_inode_connect(&trans, &cluster, hlink,
				      tdip, tdcluster,
				      tname, tname_len, 0);
	if (error == 0) {
		KKASSERT(cluster != NULL);
		hammer2_inode_repoint(ip, (hlink ? ip->pip : tdip), cluster);
	}
done:
	hammer2_inode_unlock_ex(ip, cluster);
	hammer2_inode_unlock_ex(tdip, tdcluster);
	hammer2_inode_unlock_ex(fdip, fdcluster);
	hammer2_inode_unlock_ex(cdip, cdcluster);
	hammer2_inode_drop(ip);
	hammer2_trans_done(&trans);

	/*
	 * Issue the namecache update after unlocking all the internal
	 * hammer structures, otherwise we might deadlock.
	 */
	if (error == 0)
		cache_rename(ap->a_fnch, ap->a_tnch);

	return (error);
}

/*
 * Strategy code
 *
 * WARNING: The strategy code cannot safely use hammer2 transactions
 *	    as this can deadlock against vfs_sync's vfsync() call
 *	    if multiple flushes are queued.
 */
static int hammer2_strategy_read(struct vop_strategy_args *ap);
static int hammer2_strategy_write(struct vop_strategy_args *ap);
static void hammer2_strategy_read_callback(hammer2_io_t *dio,
				hammer2_cluster_t *cluster,
				hammer2_chain_t *chain,
				void *arg_p, off_t arg_o);

static
int
hammer2_vop_strategy(struct vop_strategy_args *ap)
{
	struct bio *biop;
	struct buf *bp;
	int error;

	biop = ap->a_bio;
	bp = biop->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer2_strategy_read(ap);
		++hammer2_iod_file_read;
		break;
	case BUF_CMD_WRITE:
		error = hammer2_strategy_write(ap);
		++hammer2_iod_file_write;
		break;
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
hammer2_strategy_read(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct bio *bio;
	struct bio *nbio;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_key_t key_dummy;
	hammer2_key_t lbase;
	int ddflag;
	uint8_t btype;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	nbio = push_bio(bio);

	lbase = bio->bio_offset;
	KKASSERT(((int)lbase & HAMMER2_PBUFMASK) == 0);

	cparent = hammer2_inode_lock_sh(ip);
	cluster = hammer2_cluster_lookup(cparent, &key_dummy,
				       lbase, lbase,
				       HAMMER2_LOOKUP_NODATA |
				       HAMMER2_LOOKUP_SHARED,
				       &ddflag);
	hammer2_inode_unlock_sh(ip, cparent);

	/*
	 * Data is zero-fill if no cluster could be found
	 * (XXX or EIO on a cluster failure).
	 */
	if (cluster == NULL) {
		bp->b_resid = 0;
		bp->b_error = 0;
		bzero(bp->b_data, bp->b_bcount);
		biodone(nbio);
		return(0);
	}

	/*
	 * Cluster elements must be type INODE or type DATA, but the
	 * compression mode (or not) for DATA chains can be different for
	 * each chain.  This will be handled by the callback.
	 */
	btype = hammer2_cluster_type(cluster);
	if (btype != HAMMER2_BREF_TYPE_INODE &&
	    btype != HAMMER2_BREF_TYPE_DATA) {
		panic("READ PATH: hammer2_strategy_read: unknown bref type");
	}
	hammer2_chain_load_async(cluster, hammer2_strategy_read_callback, nbio);
	return(0);
}

/*
 * Read callback for block that is not compressed.
 */
static
void
hammer2_strategy_read_callback(hammer2_io_t *dio,
			       hammer2_cluster_t *cluster,
			       hammer2_chain_t *chain,
			       void *arg_p, off_t arg_o)
{
	struct bio *bio = arg_p;
	struct buf *bp = bio->bio_buf;
	char *data;
	int i;

	/*
	 * Extract data and handle iteration on I/O failure.  arg_o is the
	 * cluster index for iteration.
	 */
	if (dio) {
		if (dio->bp->b_flags & B_ERROR) {
			i = (int)arg_o + 1;
			if (i >= cluster->nchains) {
				bp->b_flags |= B_ERROR;
				bp->b_error = dio->bp->b_error;
				biodone(bio);
				hammer2_cluster_unlock(cluster);
			} else {
				chain = cluster->array[i];
				kprintf("hammer2: IO CHAIN-%d %p\n", i, chain);
				hammer2_adjreadcounter(&chain->bref,
						       chain->bytes);
				hammer2_io_breadcb(chain->hmp,
						   chain->bref.data_off,
						   chain->bytes,
					       hammer2_strategy_read_callback,
						   cluster, chain,
						   arg_p, (off_t)i);
			}
			return;
		}
		data = hammer2_io_data(dio, chain->bref.data_off);
	} else {
		data = (void *)chain->data;
	}

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		/*
		 * Data is embedded in the inode (copy from inode).
		 */
		bcopy(((hammer2_inode_data_t *)data)->u.data,
		      bp->b_data, HAMMER2_EMBEDDED_BYTES);
		bzero(bp->b_data + HAMMER2_EMBEDDED_BYTES,
		      bp->b_bcount - HAMMER2_EMBEDDED_BYTES);
		bp->b_resid = 0;
		bp->b_error = 0;
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		/*
		 * Data is on-media, issue device I/O and copy.
		 *
		 * XXX direct-IO shortcut could go here XXX.
		 */
		switch (HAMMER2_DEC_COMP(chain->bref.methods)) {
		case HAMMER2_COMP_LZ4:
			hammer2_decompress_LZ4_callback(data, chain->bytes,
							bio);
			break;
		case HAMMER2_COMP_ZLIB:
			hammer2_decompress_ZLIB_callback(data, chain->bytes,
							 bio);
			break;
		case HAMMER2_COMP_NONE:
			KKASSERT(chain->bytes <= bp->b_bcount);
			bcopy(data, bp->b_data, chain->bytes);
			if (chain->bytes < bp->b_bcount) {
				bzero(bp->b_data + chain->bytes,
				      bp->b_bcount - chain->bytes);
			}
			bp->b_flags |= B_NOTMETA;
			bp->b_resid = 0;
			bp->b_error = 0;
			break;
		default:
			panic("hammer2_strategy_read: "
			      "unknown compression type");
		}
	} else {
		/* bqrelse the dio to help stabilize the call to panic() */
		if (dio)
			hammer2_io_bqrelse(&dio);
		panic("hammer2_strategy_read: unknown bref type");
	}
	hammer2_cluster_unlock(cluster);
	biodone(bio);
}

static
int
hammer2_strategy_write(struct vop_strategy_args *ap)
{	
	hammer2_pfsmount_t *pmp;
	struct bio *bio;
	struct buf *bp;
	hammer2_inode_t *ip;
	
	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	pmp = ip->pmp;
	
	hammer2_lwinprog_ref(pmp);
	mtx_lock(&pmp->wthread_mtx);
	if (TAILQ_EMPTY(&pmp->wthread_bioq.queue)) {
		bioq_insert_tail(&pmp->wthread_bioq, ap->a_bio);
		mtx_unlock(&pmp->wthread_mtx);
		wakeup(&pmp->wthread_bioq);
	} else {
		bioq_insert_tail(&pmp->wthread_bioq, ap->a_bio);
		mtx_unlock(&pmp->wthread_mtx);
	}
	hammer2_lwinprog_wait(pmp);

	return(0);
}

/*
 * hammer2_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer2_vop_ioctl(struct vop_ioctl_args *ap)
{
	hammer2_inode_t *ip;
	int error;

	ip = VTOI(ap->a_vp);

	error = hammer2_ioctl(ip, ap->a_command, (void *)ap->a_data,
			      ap->a_fflag, ap->a_cred);
	return (error);
}

static
int 
hammer2_vop_mountctl(struct vop_mountctl_args *ap)
{
	struct mount *mp;
	hammer2_pfsmount_t *pmp;
	int rc;

	switch (ap->a_op) {
	case (MOUNTCTL_SET_EXPORT):
		mp = ap->a_head.a_ops->head.vv_mount;
		pmp = MPTOPMP(mp);

		if (ap->a_ctllen != sizeof(struct export_args))
			rc = (EINVAL);
		else
			rc = vfs_export(mp, &pmp->export,
					(const struct export_args *)ap->a_ctl);
		break;
	default:
		rc = vop_stdmountctl(ap);
		break;
	}
	return (rc);
}

/*
 * This handles unlinked open files after the vnode is finally dereferenced.
 */
void
hammer2_run_unlinkq(hammer2_trans_t *trans, hammer2_pfsmount_t *pmp)
{
	const hammer2_inode_data_t *ripdata;
	hammer2_inode_unlink_t *ipul;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *cparent;

	if (TAILQ_EMPTY(&pmp->unlinkq))
		return;

	spin_lock(&pmp->list_spin);
	while ((ipul = TAILQ_FIRST(&pmp->unlinkq)) != NULL) {
		TAILQ_REMOVE(&pmp->unlinkq, ipul, entry);
		spin_unlock(&pmp->list_spin);
		ip = ipul->ip;
		kfree(ipul, pmp->minode);

		cluster = hammer2_inode_lock_ex(ip);
		ripdata = &hammer2_cluster_data(cluster)->ipdata;
		kprintf("hammer2: unlink on reclaim: %s\n",
			cluster->focus->data->ipdata.filename);
		KKASSERT(ripdata->nlinks == 0);

		cparent = hammer2_cluster_parent(cluster);
		hammer2_cluster_delete(trans, cparent, cluster,
				       HAMMER2_DELETE_PERMANENT);
		hammer2_inode_unlock_ex(ip, cluster);	/* inode lock */
		hammer2_inode_drop(ip);			/* ipul ref */

		spin_lock(&pmp->list_spin);
	}
	spin_unlock(&pmp->list_spin);
}


/*
 * KQFILTER
 */
static void filt_hammer2detach(struct knote *kn);
static int filt_hammer2read(struct knote *kn, long hint);
static int filt_hammer2write(struct knote *kn, long hint);
static int filt_hammer2vnode(struct knote *kn, long hint);

static struct filterops hammer2read_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2read };
static struct filterops hammer2write_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2write };
static struct filterops hammer2vnode_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE,
	  NULL, filt_hammer2detach, filt_hammer2vnode };

static
int
hammer2_vop_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &hammer2read_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &hammer2write_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &hammer2vnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);

	return(0);
}

static void
filt_hammer2detach(struct knote *kn)
{
	struct vnode *vp = (void *)kn->kn_hook;

	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
}

static int
filt_hammer2read(struct knote *kn, long hint)
{
	struct vnode *vp = (void *)kn->kn_hook;
	hammer2_inode_t *ip = VTOI(vp);
	off_t off;

	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return(1);
	}
	off = ip->size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI)
		return(1);
	return (kn->kn_data != 0);
}


static int
filt_hammer2write(struct knote *kn, long hint)
{
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
	kn->kn_data = 0;
	return (1);
}

static int
filt_hammer2vnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}
	return (kn->kn_fflags != 0);
}

/*
 * FIFO VOPS
 */
static
int
hammer2_vop_markatime(struct vop_markatime_args *ap)
{
	hammer2_inode_t *ip;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

	if (ip->pmp->ronly)
		return(EROFS);
	return(0);
}

static
int
hammer2_vop_fifokqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = hammer2_vop_kqfilter(ap);
	return(error);
}

/*
 * VOPS vector
 */
struct vop_ops hammer2_vnode_vops = {
	.vop_default	= vop_defaultop,
	.vop_fsync	= hammer2_vop_fsync,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_access	= hammer2_vop_access,
	.vop_advlock	= hammer2_vop_advlock,
	.vop_close	= hammer2_vop_close,
	.vop_nlink	= hammer2_vop_nlink,
	.vop_ncreate	= hammer2_vop_ncreate,
	.vop_nsymlink	= hammer2_vop_nsymlink,
	.vop_nremove	= hammer2_vop_nremove,
	.vop_nrmdir	= hammer2_vop_nrmdir,
	.vop_nrename	= hammer2_vop_nrename,
	.vop_getattr	= hammer2_vop_getattr,
	.vop_setattr	= hammer2_vop_setattr,
	.vop_readdir	= hammer2_vop_readdir,
	.vop_readlink	= hammer2_vop_readlink,
	.vop_getpages	= vop_stdgetpages,
	.vop_putpages	= vop_stdputpages,
	.vop_read	= hammer2_vop_read,
	.vop_write	= hammer2_vop_write,
	.vop_open	= hammer2_vop_open,
	.vop_inactive	= hammer2_vop_inactive,
	.vop_reclaim 	= hammer2_vop_reclaim,
	.vop_nresolve	= hammer2_vop_nresolve,
	.vop_nlookupdotdot = hammer2_vop_nlookupdotdot,
	.vop_nmkdir 	= hammer2_vop_nmkdir,
	.vop_nmknod 	= hammer2_vop_nmknod,
	.vop_ioctl	= hammer2_vop_ioctl,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
        .vop_kqfilter	= hammer2_vop_kqfilter
};

struct vop_ops hammer2_spec_vops = {
        .vop_default =          vop_defaultop,
        .vop_fsync =            hammer2_vop_fsync,
        .vop_read =             vop_stdnoread,
        .vop_write =            vop_stdnowrite,
        .vop_access =           hammer2_vop_access,
        .vop_close =            hammer2_vop_close,
        .vop_markatime =        hammer2_vop_markatime,
        .vop_getattr =          hammer2_vop_getattr,
        .vop_inactive =         hammer2_vop_inactive,
        .vop_reclaim =          hammer2_vop_reclaim,
        .vop_setattr =          hammer2_vop_setattr
};

struct vop_ops hammer2_fifo_vops = {
        .vop_default =          fifo_vnoperate,
        .vop_fsync =            hammer2_vop_fsync,
#if 0
        .vop_read =             hammer2_vop_fiforead,
        .vop_write =            hammer2_vop_fifowrite,
#endif
        .vop_access =           hammer2_vop_access,
#if 0
        .vop_close =            hammer2_vop_fifoclose,
#endif
        .vop_markatime =        hammer2_vop_markatime,
        .vop_getattr =          hammer2_vop_getattr,
        .vop_inactive =         hammer2_vop_inactive,
        .vop_reclaim =          hammer2_vop_reclaim,
        .vop_setattr =          hammer2_vop_setattr,
        .vop_kqfilter =         hammer2_vop_fifokqfilter
};

