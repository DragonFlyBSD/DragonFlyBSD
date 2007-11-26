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
 * $DragonFly: src/sys/vfs/hammer/hammer_inode.c,v 1.7 2007/11/26 05:03:11 dillon Exp $
 */

#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

int
hammer_vop_inactive(struct vop_inactive_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);

	/*
	 * Degenerate case
	 */
	if (ip == NULL) {
		vrecycle(ap->a_vp);
		return(0);
	}

	/*
	 * If the inode no longer has any references we recover its
	 * in-memory resources immediately.
	 */
	if (ip->ino_rec.ino_nlinks == 0 &&
	    (ip->hmp->mp->mnt_flag & MNT_RDONLY) == 0) {
		hammer_sync_inode(ip, MNT_NOWAIT, 1);
	}
	return(0);
}

int
hammer_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct hammer_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;

	/*
	 * Release the vnode association and ask that the inode be flushed.
	 */
	if ((ip = vp->v_data) != NULL) {
		vp->v_data = NULL;
		ip->vp = NULL;
		hammer_rel_inode(ip, 1);
	}
	return(0);
}

/*
 * Obtain a vnode for the specified inode number.  An exclusively locked
 * vnode is returned.
 */
int
hammer_vfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	struct hammer_inode *ip;
	int error;

	/*
	 * Get/allocate the hammer_inode structure.  The structure must be
	 * unlocked while we manipulate the related vnode to avoid a
	 * deadlock.
	 */
	ip = hammer_get_inode(hmp, ino, &error);
	if (ip == NULL) {
		*vpp = NULL;
		return(error);
	}
	error = hammer_get_vnode(ip, LK_EXCLUSIVE, vpp);
	hammer_rel_inode(ip, 0);
	return (error);
}

/*
 * Return a locked vnode for the specified inode.  The inode must be
 * referenced but NOT LOCKED on entry and will remain referenced on
 * return.
 */
int
hammer_get_vnode(struct hammer_inode *ip, int lktype, struct vnode **vpp)
{
	struct vnode *vp;
	int error = 0;

	for (;;) {
		if ((vp = ip->vp) == NULL) {
			error = getnewvnode(VT_HAMMER, ip->hmp->mp, vpp, 0, 0);
			if (error)
				break;
			hammer_lock_ex(&ip->lock);
			if (ip->vp != NULL) {
				hammer_unlock(&ip->lock);
				vp->v_type = VBAD;
				vx_put(vp);
				continue;
			}
			hammer_ref(&ip->lock);
			vp = *vpp;
			ip->vp = vp;
			vp->v_type = hammer_get_vnode_type(
					    ip->ino_rec.base.base.obj_type);
			vp->v_data = (void *)ip;
			/* vnode locked by getnewvnode() */
			/* make related vnode dirty if inode dirty? */
			hammer_unlock(&ip->lock);
			if (vp->v_type == VREG)
				vinitvmio(vp, ip->ino_rec.ino_size);
			break;
		}

		/*
		 * loop if the vget fails (aka races), or if the vp
		 * no longer matches ip->vp.
		 */
		if (vget(vp, LK_EXCLUSIVE) == 0) {
			if (vp == ip->vp)
				break;
			vput(vp);
		}
	}
	*vpp = vp;
	return(error);
}

/*
 * Acquire a HAMMER inode.  The returned inode is not locked.  These functions
 * do not attach or detach the related vnode (use hammer_get_vnode() for
 * that).
 */
struct hammer_inode *
hammer_get_inode(struct hammer_mount *hmp, u_int64_t obj_id, int *errorp)
{
	struct hammer_inode_info iinfo;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;

	/*
	 * Determine if we already have an inode cached.  If we do then
	 * we are golden.
	 */
	iinfo.obj_id = obj_id;
	iinfo.obj_asof = HAMMER_MAX_TID;	/* XXX */
loop:
	ip = hammer_ino_rb_tree_RB_LOOKUP_INFO(&hmp->rb_inos_root, &iinfo);
	if (ip) {
		hammer_ref(&ip->lock);
		*errorp = 0;
		return(ip);
	}

	ip = kmalloc(sizeof(*ip), M_HAMMER, M_WAITOK|M_ZERO);
	ip->obj_id = obj_id;
	ip->obj_asof = iinfo.obj_asof;
	ip->hmp = hmp;
	RB_INIT(&ip->rec_tree);

	/*
	 * Locate the on-disk inode.
	 * If we do not have an inode cached search the HAMMER on-disk B-Tree
	 * for it.
	 */

	hammer_init_cursor_hmp(&cursor, hmp);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.key = 0;
	cursor.key_beg.create_tid = iinfo.obj_asof;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
	cursor.key_beg.obj_type = 0;
	cursor.flags = HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_GET_DATA;

	*errorp = hammer_btree_lookup(&cursor);

	/*
	 * On success the B-Tree lookup will hold the appropriate
	 * buffer cache buffers and provide a pointer to the requested
	 * information.  Copy the information to the in-memory inode.
	 */
	if (*errorp == 0) {
		ip->ino_rec = cursor.record->inode;
		ip->ino_data = cursor.data->inode;
	}
	hammer_cache_node(cursor.node, &ip->cache);
	hammer_done_cursor(&cursor);

	/*
	 * On success load the inode's record and data and insert the
	 * inode into the B-Tree.  It is possible to race another lookup
	 * insertion of the same inode so deal with that condition too.
	 */
	if (*errorp == 0) {
		hammer_ref(&ip->lock);
		if (RB_INSERT(hammer_ino_rb_tree, &hmp->rb_inos_root, ip)) {
			hammer_uncache_node(&ip->cache);
			hammer_unref(&ip->lock);
			kfree(ip, M_HAMMER);
			goto loop;
		}
		ip->flags |= HAMMER_INODE_ONDISK;
	} else {
		kfree(ip, M_HAMMER);
		ip = NULL;
	}
	return (ip);
}

/*
 * Create a new filesystem object, returning the inode in *ipp.  The
 * returned inode will be referenced but not locked.
 *
 * The inode is created in-memory and will be delay-synchronized to the
 * disk.
 */
int
hammer_create_inode(hammer_transaction_t trans, struct vattr *vap,
		    struct ucred *cred, hammer_inode_t dip,
		    struct hammer_inode **ipp)
{
	hammer_mount_t hmp;
	hammer_inode_t ip;
	uid_t xuid;

	hmp = trans->hmp;
	ip = kmalloc(sizeof(*ip), M_HAMMER, M_WAITOK|M_ZERO);
	ip->obj_id = hammer_alloc_tid(trans);
	KKASSERT(ip->obj_id != 0);
	ip->obj_asof = HAMMER_MAX_TID;	/* XXX */
	ip->hmp = hmp;
	ip->flags = HAMMER_INODE_DDIRTY | HAMMER_INODE_RDIRTY |
		    HAMMER_INODE_ITIMES;
	ip->last_tid = trans->tid;

	RB_INIT(&ip->rec_tree);

	ip->ino_rec.ino_atime = trans->tid;
	ip->ino_rec.ino_mtime = trans->tid;
	ip->ino_rec.ino_size = 0;
	ip->ino_rec.ino_nlinks = 0;
	/* XXX */
	kprintf("rootvol %p ondisk %p\n", hmp->rootvol, hmp->rootvol->ondisk);
	ip->ino_rec.base.rec_id = hammer_alloc_recid(trans);
	KKASSERT(ip->ino_rec.base.rec_id != 0);
	ip->ino_rec.base.base.obj_id = ip->obj_id;
	ip->ino_rec.base.base.key = 0;
	ip->ino_rec.base.base.create_tid = trans->tid;
	ip->ino_rec.base.base.delete_tid = 0;
	ip->ino_rec.base.base.rec_type = HAMMER_RECTYPE_INODE;
	ip->ino_rec.base.base.obj_type = hammer_get_obj_type(vap->va_type);

	ip->ino_data.version = HAMMER_INODE_DATA_VERSION;
	ip->ino_data.mode = vap->va_mode;
	ip->ino_data.ctime = trans->tid;
	ip->ino_data.parent_obj_id = (dip) ? dip->ino_rec.base.base.obj_id : 0;

	/*
	 * Calculate default uid/gid and overwrite with information from
	 * the vap.
	 */
	xuid = hammer_to_unix_xid(&dip->ino_data.uid);
	ip->ino_data.gid = dip->ino_data.gid;
	xuid = vop_helper_create_uid(hmp->mp, dip->ino_data.mode, xuid, cred,
				     &vap->va_mode);
	ip->ino_data.mode = vap->va_mode;

	if (vap->va_vaflags & VA_UID_UUID_VALID)
		ip->ino_data.uid = vap->va_uid_uuid;
	else if (vap->va_uid != (uid_t)VNOVAL)
		hammer_guid_to_uuid(&ip->ino_data.uid, xuid);
	if (vap->va_vaflags & VA_GID_UUID_VALID)
		ip->ino_data.gid = vap->va_gid_uuid;
	else if (vap->va_gid != (gid_t)VNOVAL)
		hammer_guid_to_uuid(&ip->ino_data.gid, vap->va_gid);

	hammer_ref(&ip->lock);
	if (RB_INSERT(hammer_ino_rb_tree, &hmp->rb_inos_root, ip)) {
		hammer_unref(&ip->lock);
		panic("hammer_create_inode: duplicate obj_id %llx", ip->obj_id);
	}
	*ipp = ip;
	return(0);
}

int
hammer_update_inode(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	hammer_record_t record;
	int error;

	/*
	 * Locate the record on-disk and mark it as deleted
	 *
	 * XXX Update the inode record and data in-place if the retention
	 * policy allows it.
	 */
	error = 0;

	if (ip->flags & HAMMER_INODE_ONDISK) {
		hammer_init_cursor_ip(&cursor, ip);
		cursor.key_beg.obj_id = ip->obj_id;
		cursor.key_beg.key = 0;
		cursor.key_beg.create_tid = ip->obj_asof;
		cursor.key_beg.delete_tid = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.obj_type = 0;
		cursor.flags = HAMMER_CURSOR_GET_RECORD;

		error = hammer_btree_lookup(&cursor);

		if (error == 0) {
			cursor.record->base.base.delete_tid = trans->tid;
			hammer_modify_buffer(cursor.record_buffer);
		}
		hammer_cache_node(cursor.node, &ip->cache);
		hammer_done_cursor(&cursor);
	}

	/*
	 * Write out a new record if the in-memory inode is not marked
	 * as having been deleted.
	 */
	if (error == 0 && (ip->flags & HAMMER_INODE_DELETED) == 0) { 
		record = hammer_alloc_mem_record(trans, ip);
		record->rec.inode = ip->ino_rec;
		record->rec.inode.base.base.create_tid = trans->tid;
		record->rec.inode.base.data_len = sizeof(ip->ino_data);
		record->data = (void *)&ip->ino_data;
		error = hammer_ip_sync_record(record);
		hammer_free_mem_record(record);
		ip->flags &= ~(HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY);
		ip->flags |= HAMMER_INODE_ONDISK;
	}
	return(error);
}

/*
 * Release a reference on an inode and unload it if told to flush.
 */
void
hammer_rel_inode(struct hammer_inode *ip, int flush)
{
	hammer_unref(&ip->lock);
	if (flush || ip->ino_rec.ino_nlinks == 0)
		ip->flags |= HAMMER_INODE_FLUSH;
	if (ip->lock.refs == 0 && (ip->flags & HAMMER_INODE_FLUSH))
		hammer_unload_inode(ip, NULL);
}

/*
 * Unload and destroy the specified inode.
 *
 * (called via RB_SCAN)
 */
int
hammer_unload_inode(struct hammer_inode *ip, void *data __unused)
{
	int error;

	KASSERT(ip->lock.refs == 0,
		("hammer_unload_inode: %d refs\n", ip->lock.refs));
	KKASSERT(ip->vp == NULL);
	hammer_ref(&ip->lock);

	error = hammer_sync_inode(ip, MNT_WAIT, 1);
	if (error)
		kprintf("hammer_sync_inode failed error %d\n", error);

	RB_REMOVE(hammer_ino_rb_tree, &ip->hmp->rb_inos_root, ip);

	hammer_uncache_node(&ip->cache);
	kfree(ip, M_HAMMER);
	return(0);
}

/*
 * A transaction has modified an inode, requiring a new record and possibly
 * also data to be written out.
 */
void
hammer_modify_inode(struct hammer_transaction *trans,
		    struct hammer_inode *ip, int flags)
{
	ip->flags |= flags;
	if (flags & HAMMER_INODE_TID)
		ip->last_tid = trans->tid;
}

/*
 * Sync any dirty buffers and records associated with an inode.  The
 * inode's last_tid field is used as the transaction id for the sync,
 * overriding any intermediate TIDs that were used for records.  Note
 * that the dirty buffer cache buffers do not have any knowledge of
 * the transaction id they were modified under.
 */
static int
hammer_sync_inode_callback(hammer_record_t rec, void *data __unused)
{
	int error;

	error = 0;
	if ((rec->flags & HAMMER_RECF_DELETED) == 0)
		error = hammer_ip_sync_record(rec);

	if (error) {
		kprintf("hammer_sync_inode_callback: sync failed rec %p\n",
			rec);
		return(-1);
	}
	hammer_free_mem_record(rec);
	return(0);
}

/*
 * XXX error handling
 */
int
hammer_sync_inode(hammer_inode_t ip, int waitfor, int handle_delete)
{
	struct hammer_transaction trans;
	int error;
	int r;

	hammer_lock_ex(&ip->lock);
	hammer_start_transaction(&trans, ip->hmp);

	/*
	 * If the inode has been deleted (nlinks == 0), and the OS no longer
	 * has any references to it (handle_delete != 0), clean up in-memory
	 * data.
	 *
	 * NOTE: We do not set the RDIRTY flag when updating the delete_tid,
	 * setting HAMMER_INODE_DELETED takes care of it.
	 */
	if (ip->ino_rec.ino_nlinks == 0 && handle_delete) {
		if (ip->vp)
			vtruncbuf(ip->vp, 0, HAMMER_BUFSIZE);
		error = hammer_ip_delete_range(&trans, ip,
					       HAMMER_MIN_KEY, HAMMER_MAX_KEY);
		KKASSERT(RB_EMPTY(&ip->rec_tree));
		ip->ino_rec.base.base.delete_tid = trans.tid;
		hammer_modify_inode(&trans, ip,
				    HAMMER_INODE_DELETED | HAMMER_INODE_TID);
	}

	/*
	 * Sync the buffer cache
	 */
	if (ip->vp != NULL)
		error = vfsync(ip->vp, waitfor, 1, NULL, NULL);
	else
		error = 0;

	/*
	 * Now sync related records
	 */
	if (error == 0) {
		r = RB_SCAN(hammer_rec_rb_tree, &ip->rec_tree, NULL,
			    hammer_sync_inode_callback, NULL);
		if (r < 0)
			error = EIO;
	}

	/*
	 * Now update the inode's on-disk inode-data and/or on-disk record.
	 */
	switch(ip->flags & (HAMMER_INODE_DELETED|HAMMER_INODE_ONDISK)) {
	case HAMMER_INODE_DELETED|HAMMER_INODE_ONDISK:
		/*
		 * If deleted and on-disk, don't set any additional flags.
		 * the delete flag takes care of things.
		 */
		break;
	case HAMMER_INODE_DELETED:
		/*
		 * Take care of the case where a deleted inode was never
		 * flushed to the disk in the first place.
		 */
		ip->flags &= ~(HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY);
		while (RB_ROOT(&ip->rec_tree))
			hammer_free_mem_record(RB_ROOT(&ip->rec_tree));
		break;
	case HAMMER_INODE_ONDISK:
		/*
		 * If already on-disk, do not set any additional flags.
		 */
		break;
	default:
		/*
		 * If not on-disk and not deleted, set both dirty flags
		 * to force an initial record to be written.
		 */
		ip->flags |= HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY;
		break;
	}

	/*
	 * If RDIRTY or DDIRTY is set, write out a new record.  If the
	 * inode is already on-disk, the old record is marked as deleted.
	 */
	if (ip->flags & (HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY |
			 HAMMER_INODE_DELETED)) {
		error = hammer_update_inode(&trans, ip);
	}
	hammer_commit_transaction(&trans);
	hammer_unlock(&ip->lock);
	return(error);
}

/*
 * Access the filesystem buffer containing the cluster-relative byte
 * offset, validate the buffer type, load *bufferp and return a
 * pointer to the requested data.  The buffer is reference and locked on
 * return.
 *
 * If buf_type is 0 the buffer is assumed to be a pure-data buffer and
 * no type or crc check is performed.
 *
 * If *bufferp is not NULL on entry it is assumed to contain a locked
 * and referenced buffer which will then be replaced.
 *
 * If the caller is holding another unrelated buffer locked it must be
 * passed in reorderbuf so we can properly order buffer locks.
 *
 * XXX add a flag for the buffer type and check the CRC here XXX
 */
void *
hammer_bread(hammer_cluster_t cluster, int32_t cloff,
	     u_int64_t buf_type, int *errorp,
	     struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t buf_no;
	int32_t buf_off;

	/*
	 * Load the correct filesystem buffer, replacing *bufferp.
	 */
	buf_no = cloff / HAMMER_BUFSIZE;
	buffer = *bufferp;
	if (buffer == NULL || buffer->cluster != cluster ||
	    buffer->buf_no != buf_no) {
		if (buffer) {
			/*hammer_unlock(&buffer->io.lock);*/
			hammer_rel_buffer(buffer, 0);
		}
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
		if (buffer == NULL)
			return(NULL);
		/*hammer_lock_ex(&buffer->io.lock);*/
	}

	/*
	 * Validate the buffer type
	 */
	buf_off = cloff & HAMMER_BUFMASK;
	if (buf_type) {
		if (buf_type != buffer->ondisk->head.buf_type) {
			kprintf("BUFFER HEAD TYPE MISMATCH %llx %llx\n",
				buf_type, buffer->ondisk->head.buf_type);
			*errorp = EIO;
			return(NULL);
		}
		if (buf_off < sizeof(buffer->ondisk->head)) {
			kprintf("BUFFER OFFSET TOO LOW %d\n", buf_off);
			*errorp = EIO;
			return(NULL);
		}
	}

	/*
	 * Return a pointer to the buffer data.
	 */
	*errorp = 0;
	return((char *)buffer->ondisk + buf_off);
}

