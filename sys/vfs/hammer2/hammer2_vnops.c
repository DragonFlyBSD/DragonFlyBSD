/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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
static void hammer2_decompress_LZ4_callback(struct bio *bio);
static void hammer2_decompress_ZLIB_callback(struct bio *bio);

struct objcache *cache_buffer_read;
struct objcache *cache_buffer_write;

/* 
 * Callback used in read path in case that a block is compressed with LZ4.
 */
static
void
hammer2_decompress_LZ4_callback(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct buf *obp;
	struct bio *obio;
	int loff;

	/*
	 * If BIO_DONE is already set the device buffer was already
	 * fully valid (B_CACHE).  If it is not set then I/O was issued
	 * and we have to run I/O completion as the last bio.
	 *
	 * Nobody is waiting for our device I/O to complete, we are
	 * responsible for bqrelse()ing it which means we also have to do
	 * the equivalent of biowait() and clear BIO_DONE (which breadcb()
	 * may have set).
	 *
	 * Any preexisting device buffer should match the requested size,
	 * but due to bigblock recycling and other factors there is some
	 * fragility there, so we assert that the device buffer covers
	 * the request.
	 */
	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(bp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);

	obio = bio->bio_caller_info1.ptr;
	obp = obio->bio_buf;
	loff = obio->bio_caller_info3.value;

	if (bp->b_flags & B_ERROR) {
		obp->b_flags |= B_ERROR;
		obp->b_error = bp->b_error;
	} else if (obio->bio_caller_info2.index &&
		   obio->bio_caller_info1.uvalue32 !=
		    crc32(bp->b_data, bp->b_bufsize)) {
		obp->b_flags |= B_ERROR;
		obp->b_error = EIO;
	} else {
		KKASSERT(obp->b_bufsize <= 65536);
		
		char *buffer;
		char *compressed_buffer;
		int *compressed_size;
		
		buffer = bp->b_data + loff;
		compressed_size = (int*)buffer;
		compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
		KKASSERT((unsigned int)*compressed_size <= 65536);
		int result = LZ4_decompress_safe(&buffer[sizeof(int)],
			compressed_buffer, *compressed_size, obp->b_bufsize);
		if (result < 0) {
			kprintf("READ PATH: Error during decompression."
				"bio %016jx/%d loff=%d\n",
				(intmax_t)bio->bio_offset, bio->bio_buf->b_bufsize, loff);
			/* make sure it isn't random garbage */
			bzero(compressed_buffer, obp->b_bufsize);
		}
		KKASSERT(result <= obp->b_bufsize);
		bcopy(compressed_buffer, obp->b_data, obp->b_bufsize);
		if (result < obp->b_bufsize)
			bzero(obp->b_data + result, obp->b_bufsize - result);
		objcache_put(cache_buffer_read, compressed_buffer);
		obp->b_resid = 0;
		obp->b_flags |= B_AGE;
	}
	biodone(obio);
	bqrelse(bp);
}

/*
 * Callback used in read path in case that a block is compressed with ZLIB.
 * It is almost identical to LZ4 callback, so in theory they can be unified,
 * but we didn't want to make changes in bio structure for that.
 */
static
void
hammer2_decompress_ZLIB_callback(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct buf *obp;
	struct bio *obio;
	int loff;

	/*
	 * If BIO_DONE is already set the device buffer was already
	 * fully valid (B_CACHE).  If it is not set then I/O was issued
	 * and we have to run I/O completion as the last bio.
	 *
	 * Nobody is waiting for our device I/O to complete, we are
	 * responsible for bqrelse()ing it which means we also have to do
	 * the equivalent of biowait() and clear BIO_DONE (which breadcb()
	 * may have set).
	 *
	 * Any preexisting device buffer should match the requested size,
	 * but due to bigblock recycling and other factors there is some
	 * fragility there, so we assert that the device buffer covers
	 * the request.
	 */
	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(bp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);

	obio = bio->bio_caller_info1.ptr;
	obp = obio->bio_buf;
	loff = obio->bio_caller_info3.value;

	if (bp->b_flags & B_ERROR) {
		obp->b_flags |= B_ERROR;
		obp->b_error = bp->b_error;
	} else if (obio->bio_caller_info2.index &&
		   obio->bio_caller_info1.uvalue32 !=
		    crc32(bp->b_data, bp->b_bufsize)) {
		obp->b_flags |= B_ERROR;
		obp->b_error = EIO;
	} else {
		KKASSERT(obp->b_bufsize <= 65536);
		
		char *buffer;
		char *compressed_buffer;
		int ret;
		
		z_stream strm_decompress;

		strm_decompress.avail_in = 0;
		strm_decompress.next_in = Z_NULL;
		
		ret = inflateInit(&strm_decompress);
		
		if (ret != Z_OK)
				kprintf("HAMMER2 ZLIB: Fatal error in inflateInit.\n");
		
		buffer = bp->b_data + loff;
		compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
		strm_decompress.next_in = buffer;

		/* XXX supply proper size, subset of device bp */
		strm_decompress.avail_in = bp->b_bufsize - loff;
		strm_decompress.next_out = compressed_buffer;
		strm_decompress.avail_out = obp->b_bufsize;
		
		ret = inflate(&strm_decompress, Z_FINISH);
		if (ret != Z_STREAM_END) {
			kprintf("HAMMER2 ZLIB: Fatar error during decompression.\n");
			bzero(compressed_buffer, obp->b_bufsize);
		}
		bcopy(compressed_buffer, obp->b_data, obp->b_bufsize);
		int result = obp->b_bufsize - strm_decompress.avail_out;
		if (result < obp->b_bufsize)
			bzero(obp->b_data + result, strm_decompress.avail_out);
		objcache_put(cache_buffer_read, compressed_buffer);
		obp->b_resid = 0;
		obp->b_flags |= B_AGE;
		ret = inflateEnd(&strm_decompress);
	}
	biodone(obio);
	bqrelse(bp);
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
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
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
	parent = hammer2_inode_lock_ex(ip);
	KKASSERT(parent);

	/*
	 * Check for deleted inodes and recycle immediately.
	 */
	if (parent->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_inode_unlock_ex(ip, parent);
		vrecycle(vp);
	} else {
		hammer2_inode_unlock_ex(ip, parent);
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
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
#if 0
	hammer2_trans_t trans;
#endif
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);
	if (ip == NULL)
		return(0);

	/*
	 * Set SUBMODIFIED so we can detect and propagate the DESTROYED
	 * bit in the flush code.
	 *
	 * ip->chain might be stale, correct it before checking as older
	 * versions of the chain are likely marked deleted even if the
	 * file hasn't been.  XXX ip->chain should never be stale on
	 * reclaim.
	 */
	chain = hammer2_inode_lock_ex(ip);
#if 0
	if (chain->next_parent)
		kprintf("RECLAIM DUPLINKED IP: %p ip->ch=%p ch=%p np=%p\n",
			ip, ip->chain, chain, chain->next_parent);
#endif

	/*
	 * The final close of a deleted file or directory marks it for
	 * destruction.  The DESTROYED flag allows the flusher to shortcut
	 * any modified blocks still unflushed (that is, just ignore them).
	 *
	 * HAMMER2 usually does not try to optimize the freemap by returning
	 * deleted blocks to it as it does not usually know how many snapshots
	 * might be referencing portions of the file/dir.  XXX TODO.
	 *
	 * XXX TODO - However, any modified file as-of when a snapshot is made
	 *	      cannot use this optimization as some of the modifications
	 *	      may wind up being part of the snapshot.
	 */
	vp->v_data = NULL;
	ip->vp = NULL;
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		KKASSERT(chain->flags & HAMMER2_CHAIN_DELETED);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DESTROYED |
					      HAMMER2_CHAIN_SUBMODIFIED);
	}
#if 0
	/*
	 * XXX chains will be flushed on sync, no need to do it here.
	 */
	if (chain->flags & (HAMMER2_CHAIN_MODIFIED |
			    HAMMER2_CHAIN_DELETED |
			    HAMMER2_CHAIN_SUBMODIFIED)) {
		hammer2_trans_init(&trans, ip->pmp, HAMMER2_TRANS_ISFLUSH);
		hammer2_chain_flush(&trans, chain);
		hammer2_trans_done(&trans);
	}
#endif
	vclrisdirty(vp);
	hammer2_inode_unlock_ex(ip, chain);		/* unlock */
	hammer2_inode_drop(ip);				/* vp ref */
	/* chain no longer referenced */
	/* chain = NULL; not needed */

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
	hammer2_chain_t *chain;
	struct vnode *vp;

	vp = ap->a_vp;
	ip = VTOI(vp);

	/*
	 * WARNING: The vfsync interacts with the buffer cache and might
	 *	    block, we can't hold the inode lock and we can't
	 *	    have a flush transaction pending.
	 */
	hammer2_trans_init(&trans, ip->pmp, HAMMER2_TRANS_ISFLUSH);
	vfsync(vp, ap->a_waitfor, 1, NULL, NULL);

	/*
	 * Calling chain_flush here creates a lot of duplicative
	 * COW operations due to non-optimal vnode ordering.
	 *
	 * Only do it for an actual fsync() syscall.  The other forms
	 * which call this function will eventually call chain_flush
	 * on the volume root as a catch-all, which is far more optimal.
	 */
	chain = hammer2_inode_lock_ex(ip);
	atomic_clear_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	vclrisdirty(vp);
	if (ip->flags & (HAMMER2_INODE_RESIZED|HAMMER2_INODE_MTIME))
		hammer2_inode_fsync(&trans, ip, &chain);

	if (ap->a_flags & VOP_FSYNC_SYSCALL) {
		hammer2_chain_flush(&trans, chain);
	}
	hammer2_inode_unlock_ex(ip, chain);
	hammer2_trans_done(&trans);

	return (0);
}

static
int
hammer2_vop_access(struct vop_access_args *ap)
{
	hammer2_inode_t *ip = VTOI(ap->a_vp);
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *chain;
	uid_t uid;
	gid_t gid;
	int error;

	chain = hammer2_inode_lock_sh(ip);
	ipdata = &chain->data->ipdata;
	uid = hammer2_to_unix_xid(&ipdata->uid);
	gid = hammer2_to_unix_xid(&ipdata->gid);
	error = vop_helper_access(ap, uid, gid, ipdata->mode, ipdata->uflags);
	hammer2_inode_unlock_sh(ip, chain);

	return (error);
}

static
int
hammer2_vop_getattr(struct vop_getattr_args *ap)
{
	hammer2_inode_data_t *ipdata;
	hammer2_chain_t *chain;
	hammer2_pfsmount_t *pmp;
	hammer2_inode_t *ip;
	struct vnode *vp;
	struct vattr *vap;

	vp = ap->a_vp;
	vap = ap->a_vap;

	ip = VTOI(vp);
	pmp = ip->pmp;

	chain = hammer2_inode_lock_sh(ip);
	ipdata = &chain->data->ipdata;

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
	vap->va_type = hammer2_get_vtype(chain);
	vap->va_filerev = 0;
	vap->va_uid_uuid = ipdata->uid;
	vap->va_gid_uuid = ipdata->gid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	hammer2_inode_unlock_sh(ip, chain);

	return (0);
}

static
int
hammer2_vop_setattr(struct vop_setattr_args *ap)
{
	hammer2_inode_data_t *ipdata;
	hammer2_inode_t *ip;
	hammer2_chain_t *chain;
	hammer2_trans_t trans;
	struct vnode *vp;
	struct vattr *vap;
	int error;
	int kflags = 0;
	int domtime = 0;
	uint64_t ctime;

	vp = ap->a_vp;
	vap = ap->a_vap;
	hammer2_update_time(&ctime);

	ip = VTOI(vp);

	if (ip->pmp->ronly)
		return(EROFS);

	hammer2_chain_memory_wait(ip->pmp);
	hammer2_trans_init(&trans, ip->pmp, 0);
	chain = hammer2_inode_lock_ex(ip);
	ipdata = &chain->data->ipdata;
	error = 0;

	if (vap->va_flags != VNOVAL) {
		u_int32_t flags;

		flags = ipdata->uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer2_to_unix_xid(&ipdata->uid),
					 ap->a_cred);
		if (error == 0) {
			if (ipdata->uflags != flags) {
				ipdata = hammer2_chain_modify_ip(&trans, ip,
								 &chain, 0);
				ipdata->uflags = flags;
				ipdata->ctime = ctime;
				kflags |= NOTE_ATTRIB;
			}
			if (ipdata->uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ipdata->uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ipdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ipdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ipdata->gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer2_guid_to_uuid(&uuid_uid, cur_uid);
			hammer2_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ipdata->uid, sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ipdata->gid, sizeof(uuid_gid)) ||
			    ipdata->mode != cur_mode
			) {
				ipdata = hammer2_chain_modify_ip(&trans, ip,
								 &chain, 0);
				ipdata->uid = uuid_uid;
				ipdata->gid = uuid_gid;
				ipdata->mode = cur_mode;
				ipdata->ctime = ctime;
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
			hammer2_inode_unlock_ex(ip, chain);
			if (vap->va_size < ip->size) {
				hammer2_truncate_file(ip, vap->va_size);
			} else {
				hammer2_extend_file(ip, vap->va_size);
			}
			chain = hammer2_inode_lock_ex(ip);
			ipdata = &chain->data->ipdata; /* RELOAD */
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
		ipdata = hammer2_chain_modify_ip(&trans, ip, &chain, 0);
		ipdata->atime = hammer2_timespec_to_time(&vap->va_atime);
		kflags |= NOTE_ATTRIB;
	}
#endif
	if (vap->va_mtime.tv_sec != VNOVAL) {
		ipdata = hammer2_chain_modify_ip(&trans, ip, &chain, 0);
		ipdata->mtime = hammer2_timespec_to_time(&vap->va_mtime);
		kflags |= NOTE_ATTRIB;
		domtime = 0;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t cur_mode = ipdata->mode;
		uid_t cur_uid = hammer2_to_unix_xid(&ipdata->uid);
		gid_t cur_gid = hammer2_to_unix_xid(&ipdata->gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ipdata->mode != cur_mode) {
			ipdata = hammer2_chain_modify_ip(&trans, ip, &chain, 0);
			ipdata->mode = cur_mode;
			ipdata->ctime = ctime;
			kflags |= NOTE_ATTRIB;
		}
	}

	/*
	 * If a truncation occurred we must call inode_fsync() now in order
	 * to trim the related data chains, otherwise a later expansion can
	 * cause havoc.
	 */
	hammer2_inode_fsync(&trans, ip, &chain);

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
	hammer2_inode_unlock_ex(ip, chain);
	hammer2_trans_done(&trans);
	hammer2_knote(ip->vp, kflags);

	return (error);
}

static
int
hammer2_vop_readdir(struct vop_readdir_args *ap)
{
	hammer2_inode_data_t *ipdata;
	hammer2_inode_t *ip;
	hammer2_inode_t *xip;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *xchain;
	hammer2_tid_t inum;
	hammer2_key_t key_next;
	hammer2_key_t lkey;
	struct uio *uio;
	off_t *cookies;
	off_t saveoff;
	int cookie_index;
	int cache_index = -1;
	int ncookies;
	int error;
	int dtype;
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

	parent = hammer2_inode_lock_sh(ip);
	ipdata = &parent->data->ipdata;

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
	chain = (void *)(intptr_t)-1;	/* non-NULL for early goto done case */

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
			hammer2_inode_unlock_sh(ip, parent);
			xchain = hammer2_inode_lock_sh(xip);
			parent = hammer2_inode_lock_sh(ip);
			hammer2_inode_drop(xip);
			if (xip == ip->pip) {
				inum = xchain->data->ipdata.inum &
				       HAMMER2_DIRHASH_USERMSK;
				hammer2_inode_unlock_sh(xip, xchain);
				break;
			}
			hammer2_inode_unlock_sh(xip, xchain);
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

	/*
	 * parent is the inode chain, already locked for us.  Don't
	 * double lock shared locks as this will screw up upgrades.
	 */
	if (error) {
		goto done;
	}
	chain = hammer2_chain_lookup(&parent, &key_next, lkey, lkey,
				     &cache_index, HAMMER2_LOOKUP_SHARED);
	if (chain == NULL) {
		chain = hammer2_chain_lookup(&parent, &key_next,
					     lkey, (hammer2_key_t)-1,
					     &cache_index,
					     HAMMER2_LOOKUP_SHARED);
	}
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			dtype = hammer2_get_dtype(chain);
			saveoff = chain->bref.key & HAMMER2_DIRHASH_USERMSK;
			r = vop_write_dirent(&error, uio,
					     chain->data->ipdata.inum &
					      HAMMER2_DIRHASH_USERMSK,
					     dtype,
					     chain->data->ipdata.name_len,
					     chain->data->ipdata.filename);
			if (r)
				break;
			if (cookies)
				cookies[cookie_index] = saveoff;
			++cookie_index;
		} else {
			/* XXX chain error */
			kprintf("bad chain type readdir %d\n",
				chain->bref.type);
		}

		/*
		 * Keys may not be returned in order so once we have a
		 * placemarker (chain) the scan must allow the full range
		 * or some entries will be missed.
		 */
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next, (hammer2_key_t)-1,
					   &cache_index, HAMMER2_LOOKUP_SHARED);
		if (chain) {
			saveoff = (chain->bref.key &
				   HAMMER2_DIRHASH_USERMSK) + 1;
		} else {
			saveoff = (hammer2_key_t)-1;
		}
		if (cookie_index == ncookies)
			break;
	}
	if (chain)
		hammer2_chain_unlock(chain);
done:
	hammer2_inode_unlock_sh(ip, parent);
	if (ap->a_eofflag)
		*ap->a_eofflag = (chain == NULL);
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
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *ochain;
	hammer2_trans_t trans;
	hammer2_key_t key_next;
	hammer2_key_t lhc;
	struct namecache *ncp;
	const uint8_t *name;
	size_t name_len;
	int error = 0;
	int cache_index = -1;
	struct vnode *vp;

	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	name = ncp->nc_name;
	name_len = ncp->nc_nlen;
	lhc = hammer2_dirhash(name, name_len);

	/*
	 * Note: In DragonFly the kernel handles '.' and '..'.
	 */
	parent = hammer2_inode_lock_sh(dip);
	chain = hammer2_chain_lookup(&parent, &key_next,
				     lhc, lhc + HAMMER2_DIRHASH_LOMASK,
				     &cache_index, HAMMER2_LOOKUP_SHARED);
	while (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    name_len == chain->data->ipdata.name_len &&
		    bcmp(name, chain->data->ipdata.filename, name_len) == 0) {
			break;
		}
		chain = hammer2_chain_next(&parent, chain, &key_next,
					   key_next,
					   lhc + HAMMER2_DIRHASH_LOMASK,
					   &cache_index, HAMMER2_LOOKUP_SHARED);
	}
	hammer2_inode_unlock_sh(dip, parent);

	/*
	 * If the inode represents a forwarding entry for a hardlink we have
	 * to locate the actual inode.  The original ip is saved for possible
	 * deconsolidation.  (ip) will only be set to non-NULL when we have
	 * to locate the real file via a hardlink.  ip will be referenced but
	 * not locked in that situation.  chain is passed in locked and
	 * returned locked.
	 *
	 * XXX what kind of chain lock?
	 */
	ochain = NULL;
	if (chain && chain->data->ipdata.type == HAMMER2_OBJTYPE_HARDLINK) {
		error = hammer2_hardlink_find(dip, &chain, &ochain);
		if (error) {
			kprintf("hammer2: unable to find hardlink\n");
			if (chain) {
				hammer2_chain_unlock(chain);
				chain = NULL;
			}
			goto failed;
		}
	}

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
	if (chain) {
		ip = hammer2_inode_get(dip->pmp, dip, chain);
		vp = hammer2_igetv(ip, &error);
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
		} else if (error == ENOENT) {
			cache_setvp(ap->a_nch, NULL);
		}
		hammer2_inode_unlock_ex(ip, chain);

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
failed:
	KASSERT(error || ap->a_nch->ncp->nc_vp != NULL,
		("resolve error %d/%p chain %p ap %p\n",
		 error, ap->a_nch->ncp->nc_vp, chain, ap));
	if (ochain)
		hammer2_chain_drop(ochain);
	return error;
}

static
int
hammer2_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	int error;

	dip = VTOI(ap->a_dvp);

	if ((ip = dip->pip) == NULL) {
		*ap->a_vpp = NULL;
		return ENOENT;
	}
	parent = hammer2_inode_lock_ex(ip);
	*ap->a_vpp = hammer2_igetv(ip, &error);
	hammer2_inode_unlock_ex(ip, parent);

	return error;
}

static
int
hammer2_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	hammer2_inode_t *dip;
	hammer2_inode_t *nip;
	hammer2_trans_t trans;
	hammer2_chain_t *chain;
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

	hammer2_chain_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &chain, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock_ex(nip, chain);
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
	hammer2_chain_t *parent;
	hammer2_off_t size;

	parent = hammer2_inode_lock_sh(ip);
	size = parent->data->ipdata.size;
	hammer2_inode_unlock_sh(ip, parent);
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
	hammer2_inode_t *dip;	/* target directory to create link in */
	hammer2_inode_t *ip;	/* inode we are hardlinking to */
	hammer2_chain_t *chain;
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

	/*
	 * ip represents the file being hardlinked.  The file could be a
	 * normal file or a hardlink target if it has already been hardlinked.
	 * If ip is a hardlinked target then ip->pip represents the location
	 * of the hardlinked target, NOT the location of the hardlink pointer.
	 *
	 * Bump nlinks and potentially also create or move the hardlink
	 * target in the parent directory common to (ip) and (dip).  The
	 * consolidation code can modify ip->chain and ip->pip.  The
	 * returned chain is locked.
	 */
	ip = VTOI(ap->a_vp);
	hammer2_chain_memory_wait(ip->pmp);
	hammer2_trans_init(&trans, ip->pmp, 0);

	chain = hammer2_inode_lock_ex(ip);
	error = hammer2_hardlink_consolidate(&trans, ip, &chain, dip, 1);
	if (error)
		goto done;

	/*
	 * Create a directory entry connected to the specified chain.
	 * The hardlink consolidation code has already adjusted ip->pip
	 * to the common parent directory containing the actual hardlink
	 *
	 * (which may be different from dip where we created our hardlink
	 * entry. ip->chain always represents the actual hardlink and not
	 * any of the pointers to the actual hardlink).
	 */
	error = hammer2_inode_connect(&trans, 1,
				      dip, &chain,
				      name, name_len);
	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, ap->a_vp);
	}
done:
	hammer2_inode_unlock_ex(ip, chain);
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
	hammer2_chain_t *nchain;
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
	hammer2_chain_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &nchain, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
	} else {
		*ap->a_vpp = hammer2_igetv(nip, &error);
		hammer2_inode_unlock_ex(nip, nchain);
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
	hammer2_chain_t *nparent;
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
	hammer2_chain_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);

	ap->a_vap->va_type = VLNK;	/* enforce type */

	nip = hammer2_inode_create(&trans, dip, ap->a_vap, ap->a_cred,
				   name, name_len, &nparent, &error);
	if (error) {
		KKASSERT(nip == NULL);
		*ap->a_vpp = NULL;
		hammer2_trans_done(&trans);
		return error;
	}
	*ap->a_vpp = hammer2_igetv(nip, &error);

	/*
	 * Build the softlink (~like file data) and finalize the namecache.
	 */
	if (error == 0) {
		size_t bytes;
		struct uio auio;
		struct iovec aiov;
		hammer2_inode_data_t *nipdata;

		nipdata = &nip->chain->data->ipdata;
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER2_EMBEDDED_BYTES) {
			KKASSERT(nipdata->op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			bcopy(ap->a_target, nipdata->u.data, bytes);
			nipdata->size = bytes;
			nip->size = bytes;
			hammer2_inode_unlock_ex(nip, nparent);
		} else {
			hammer2_inode_unlock_ex(nip, nparent);
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
			nipdata = &nip->chain->data->ipdata; /* RELOAD */
			/* XXX handle error */
			error = 0;
		}
	} else {
		hammer2_inode_unlock_ex(nip, nparent);
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
	hammer2_chain_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	error = hammer2_unlink_file(&trans, dip, name, name_len, 0, NULL);
	hammer2_trans_done(&trans);
	if (error == 0) {
		cache_unlink(ap->a_nch);
	}
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

	hammer2_chain_memory_wait(dip->pmp);
	hammer2_trans_init(&trans, dip->pmp, 0);
	error = hammer2_unlink_file(&trans, dip, name, name_len, 1, NULL);
	hammer2_trans_done(&trans);
	if (error == 0) {
		cache_unlink(ap->a_nch);
	}
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
	hammer2_inode_t *fdip;
	hammer2_inode_t *tdip;
	hammer2_inode_t *ip;
	hammer2_chain_t *chain;
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

	hammer2_chain_memory_wait(tdip->pmp);
	hammer2_trans_init(&trans, tdip->pmp, 0);

	/*
	 * ip is the inode being renamed.  If this is a hardlink then
	 * ip represents the actual file and not the hardlink marker.
	 */
	ip = VTOI(fncp->nc_vp);
	chain = NULL;

	/*
	 * Keep a tight grip on the inode so the temporary unlinking from
	 * the source location prior to linking to the target location
	 * does not cause the chain to be destroyed.
	 *
	 * NOTE: To avoid deadlocks we cannot lock (ip) while we are
	 *	 unlinking elements from their directories.  Locking
	 *	 the nlinks field does not lock the whole inode.
	 */
	hammer2_inode_ref(ip);

	/*
	 * Remove target if it exists
	 */
	error = hammer2_unlink_file(&trans, tdip, tname, tname_len, -1, NULL);
	if (error && error != ENOENT)
		goto done;
	cache_setunresolved(ap->a_tnch);

	/*
	 * When renaming a hardlinked file we may have to re-consolidate
	 * the location of the hardlink target.  Since the element is simply
	 * being moved, nlinks is not modified in this case.
	 *
	 * If ip represents a regular file the consolidation code essentially
	 * does nothing other than return the same locked chain that was
	 * passed in.
	 *
	 * The returned chain will be locked.
	 *
	 * WARNING!  We do not currently have a local copy of ipdata but
	 *	     we do use one later remember that it must be reloaded
	 *	     on any modification to the inode, including connects.
	 */
	chain = hammer2_inode_lock_ex(ip);
	error = hammer2_hardlink_consolidate(&trans, ip, &chain, tdip, 0);
	if (error)
		goto done;

	/*
	 * Disconnect (fdip, fname) from the source directory.  This will
	 * disconnect (ip) if it represents a direct file.  If (ip) represents
	 * a hardlink the HARDLINK pointer object will be removed but the
	 * hardlink will stay intact.
	 *
	 * The target chain may be marked DELETED but will not be destroyed
	 * since we retain our hold on ip and chain.
	 */
	error = hammer2_unlink_file(&trans, fdip, fname, fname_len, -1, &hlink);
	KKASSERT(error != EAGAIN);
	if (error)
		goto done;

	/*
	 * Reconnect ip to target directory using chain.  Chains cannot
	 * actually be moved, so this will duplicate the chain in the new
	 * spot and assign it to the ip, replacing the old chain.
	 *
	 * WARNING: chain locks can lock buffer cache buffers, to avoid
	 *	    deadlocks we want to unlock before issuing a cache_*()
	 *	    op (that might have to lock a vnode).
	 */
	error = hammer2_inode_connect(&trans, hlink,
				      tdip, &chain,
				      tname, tname_len);
	if (error == 0) {
		KKASSERT(chain != NULL);
		hammer2_inode_repoint(ip, (hlink ? ip->pip : tdip), chain);
		cache_rename(ap->a_fnch, ap->a_tnch);
	}
done:
	hammer2_inode_unlock_ex(ip, chain);
	hammer2_inode_drop(ip);
	hammer2_trans_done(&trans);

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
static void hammer2_strategy_read_callback(hammer2_chain_t *chain,
				struct buf *dbp, char *data, void *arg);

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
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_dummy;
	hammer2_key_t lbase;
	int loff;
	int cache_index = -1;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	nbio = push_bio(bio);

	lbase = bio->bio_offset;
	chain = NULL;
	KKASSERT(((int)lbase & HAMMER2_PBUFMASK) == 0);

	parent = hammer2_inode_lock_sh(ip);
	chain = hammer2_chain_lookup(&parent, &key_dummy,
				     lbase, lbase,
				     &cache_index,
				     HAMMER2_LOOKUP_NODATA |
				     HAMMER2_LOOKUP_SHARED);

	if (chain == NULL) {
		/*
		 * Data is zero-fill
		 */
		bp->b_resid = 0;
		bp->b_error = 0;
		bzero(bp->b_data, bp->b_bcount);
		biodone(nbio);
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		/*
		 * Data is embedded in the inode (copy from inode).
		 */
		hammer2_chain_load_async(chain, hammer2_strategy_read_callback,
					 nbio);
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		/*
		 * Data is on-media, issue device I/O and copy.
		 *
		 * XXX direct-IO shortcut could go here XXX.
		 */
		if (HAMMER2_DEC_COMP(chain->bref.methods) == HAMMER2_COMP_LZ4) {
			/*
			 * Block compression is determined by bref.methods value.
			 */
			hammer2_blockref_t *bref;
			hammer2_off_t pbase;
			hammer2_off_t pmask;
			size_t psize;
				
			bref = &chain->bref;
			psize = hammer2_devblksize(chain->bytes);
			pmask = (hammer2_off_t)psize - 1;
			pbase = bref->data_off & ~pmask;
			loff = (int)((bref->data_off &
				      ~HAMMER2_OFF_MASK_RADIX) - pbase);
			nbio->bio_caller_info3.value = loff;
			breadcb(chain->hmp->devvp, pbase, psize,
				hammer2_decompress_LZ4_callback, nbio);
			/* XXX async read dev blk not protected by chain lk */
			hammer2_chain_unlock(chain);
		} else if (HAMMER2_DEC_COMP(chain->bref.methods) == HAMMER2_COMP_ZLIB) {
			hammer2_blockref_t *bref;
			hammer2_off_t pbase;
			hammer2_off_t pmask;
			size_t psize;
				
			bref = &chain->bref;
			psize = hammer2_devblksize(chain->bytes);
			pmask = (hammer2_off_t)psize - 1;
			pbase = bref->data_off & ~pmask;
			loff = (int)((bref->data_off &
				      ~HAMMER2_OFF_MASK_RADIX) - pbase);
			nbio->bio_caller_info3.value = loff;
			breadcb(chain->hmp->devvp, pbase, psize,
				hammer2_decompress_ZLIB_callback, nbio);
			/* XXX async read dev blk not protected by chain lk */
			hammer2_chain_unlock(chain);
		}
		else {
			hammer2_chain_load_async(chain,
						 hammer2_strategy_read_callback,
						 nbio);
		}
	} else {
		panic("READ PATH: hammer2_strategy_read: unknown bref type");
		chain = NULL;
	}
	hammer2_inode_unlock_sh(ip, parent);
	return (0);
}

/*
 * Read callback for block that is not compressed.
 */
static
void
hammer2_strategy_read_callback(hammer2_chain_t *chain, struct buf *dbp,
			       char *data, void *arg)
{
	struct bio *nbio = arg;
	struct buf *bp = nbio->bio_buf;

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
		hammer2_chain_unlock(chain);
		biodone(nbio);
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		/*
		 * Data is on-media, issue device I/O and copy.
		 *
		 * XXX direct-IO shortcut could go here XXX.
		 */
		KKASSERT(chain->bytes <= bp->b_bcount);
		bcopy(data, bp->b_data, chain->bytes);
		if (chain->bytes < bp->b_bcount); {
			bzero(bp->b_data + chain->bytes,
			      bp->b_bcount - chain->bytes);
		}
		bp->b_flags |= B_NOTMETA;
		bp->b_resid = 0;
		bp->b_error = 0;
		hammer2_chain_unlock(chain);
		biodone(nbio);
	} else {
		if (dbp)
			bqrelse(dbp);
		panic("hammer2_strategy_read: unknown bref type");
		/*hammer2_chain_unlock(chain);*/
		/*chain = NULL;*/
	}
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
	
	mtx_lock(&pmp->wthread_mtx);
	bioq_insert_tail(&pmp->wthread_bioq, ap->a_bio);
	wakeup(&pmp->wthread_bioq);
	mtx_unlock(&pmp->wthread_mtx);

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
	.vop_ioctl	= hammer2_vop_ioctl,
	.vop_mountctl	= hammer2_vop_mountctl,
	.vop_bmap	= hammer2_vop_bmap,
	.vop_strategy	= hammer2_vop_strategy,
};

struct vop_ops hammer2_spec_vops = {

};

struct vop_ops hammer2_fifo_vops = {

};

