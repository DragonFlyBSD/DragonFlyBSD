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
 * $DragonFly: src/sys/vfs/hammer/hammer_inode.c,v 1.20 2008/01/16 01:15:36 dillon Exp $
 */

#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

/*
 * The kernel is not actively referencing this vnode but is still holding
 * it cached.
 */
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
	if (ip->ino_rec.ino_nlinks == 0)
		vrecycle(ap->a_vp);
	return(0);
}

/*
 * Release the vnode association.  This is typically (but not always)
 * the last reference on the inode and will flush the inode to the
 * buffer cache.
 *
 * XXX Currently our sync code only runs through inodes with vnode
 * associations, so we depend on hammer_rel_inode() to sync any inode
 * record data to the block device prior to losing the association.
 * Otherwise transactions that the user expected to be distinct by
 * doing a manual sync may be merged.
 */
int
hammer_vop_reclaim(struct vop_reclaim_args *ap)
{
	struct hammer_inode *ip;
	struct vnode *vp;

	vp = ap->a_vp;

	if ((ip = vp->v_data) != NULL) {
		vp->v_data = NULL;
		ip->vp = NULL;
		hammer_rel_inode(ip, 0);
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
	ip = hammer_get_inode(hmp, NULL, ino, hmp->asof, 0, &error);
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

			switch(ip->ino_rec.base.base.obj_type) {
			case HAMMER_OBJTYPE_CDEV:
			case HAMMER_OBJTYPE_BDEV:
				vp->v_ops = &ip->hmp->mp->mnt_vn_spec_ops;
				addaliasu(vp, ip->ino_data.rmajor,
					  ip->ino_data.rminor);
				break;
			case HAMMER_OBJTYPE_FIFO:
				vp->v_ops = &ip->hmp->mp->mnt_vn_fifo_ops;
				break;
			default:
				break;
			}
			if (ip->obj_id == HAMMER_OBJID_ROOT)
				vp->v_flag |= VROOT;

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
 *
 * The flags argument is only applied for newly created inodes, and only
 * certain flags are inherited.
 */
struct hammer_inode *
hammer_get_inode(struct hammer_mount *hmp, struct hammer_node **cache,
		 u_int64_t obj_id, hammer_tid_t asof, int flags, int *errorp)
{
	struct hammer_inode_info iinfo;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;

	/*
	 * Determine if we already have an inode cached.  If we do then
	 * we are golden.
	 */
	iinfo.obj_id = obj_id;
	iinfo.obj_asof = asof;
loop:
	ip = hammer_ino_rb_tree_RB_LOOKUP_INFO(&hmp->rb_inos_root, &iinfo);
	if (ip) {
		hammer_ref(&ip->lock);
		*errorp = 0;
		return(ip);
	}

	ip = kmalloc(sizeof(*ip), M_HAMMER, M_WAITOK|M_ZERO);
	++hammer_count_inodes;
	ip->obj_id = obj_id;
	ip->obj_asof = iinfo.obj_asof;
	ip->hmp = hmp;
	ip->flags = flags & HAMMER_INODE_RO;
	if (hmp->ronly)
		ip->flags |= HAMMER_INODE_RO;
	RB_INIT(&ip->rec_tree);

	/*
	 * Locate the on-disk inode.
	 */
	hammer_init_cursor_hmp(&cursor, cache, hmp);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.key = 0;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
	cursor.key_beg.obj_type = 0;
	cursor.asof = iinfo.obj_asof;
	cursor.flags = HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_GET_DATA |
		       HAMMER_CURSOR_ASOF;

	*errorp = hammer_btree_lookup(&cursor);

	/*
	 * On success the B-Tree lookup will hold the appropriate
	 * buffer cache buffers and provide a pointer to the requested
	 * information.  Copy the information to the in-memory inode
	 * and cache the B-Tree node to improve future operations.
	 */
	if (*errorp == 0) {
		ip->ino_rec = cursor.record->inode;
		ip->ino_data = cursor.data->inode;
		hammer_cache_node(cursor.node, &ip->cache[0]);
		if (cache)
			hammer_cache_node(cursor.node, cache);
	}

	/*
	 * On success load the inode's record and data and insert the
	 * inode into the B-Tree.  It is possible to race another lookup
	 * insertion of the same inode so deal with that condition too.
	 *
	 * The cursor's locked node interlocks against others creating and
	 * destroying ip while we were blocked.
	 */
	if (*errorp == 0) {
		hammer_ref(&ip->lock);
		if (RB_INSERT(hammer_ino_rb_tree, &hmp->rb_inos_root, ip)) {
			hammer_uncache_node(&ip->cache[0]);
			hammer_uncache_node(&ip->cache[1]);
			hammer_unref(&ip->lock);
			--hammer_count_inodes;
			kfree(ip, M_HAMMER);
			hammer_done_cursor(&cursor);
			goto loop;
		}
		ip->flags |= HAMMER_INODE_ONDISK;
	} else {
		--hammer_count_inodes;
		kfree(ip, M_HAMMER);
		ip = NULL;
	}
	hammer_done_cursor(&cursor);
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
	++hammer_count_inodes;
	ip->obj_id = hammer_alloc_tid(trans);
	KKASSERT(ip->obj_id != 0);
	ip->obj_asof = hmp->asof;
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

	switch(ip->ino_rec.base.base.obj_type) {
	case HAMMER_OBJTYPE_CDEV:
	case HAMMER_OBJTYPE_BDEV:
		ip->ino_data.rmajor = vap->va_rmajor;
		ip->ino_data.rminor = vap->va_rminor;
		break;
	default:
		break;
	}

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

/*
 * Called by hammer_sync_inode().
 */
static int
hammer_update_inode(hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	struct hammer_cursor *spike = NULL;
	hammer_record_t record;
	int error;
	hammer_tid_t last_tid;

	/*
	 * Locate the record on-disk and mark it as deleted.  Both the B-Tree
	 * node and the record must be marked deleted.  The record may or
	 * may not be physically deleted, depending on the retention policy.
	 *
	 * If the inode has already been deleted on-disk we have nothing
	 * to do.
	 *
	 * XXX Update the inode record and data in-place if the retention
	 * policy allows it.
	 */
	last_tid = ip->last_tid;
retry:
	error = 0;

	if ((ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DELONDISK)) ==
	    HAMMER_INODE_ONDISK) {
		hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);
		cursor.key_beg.obj_id = ip->obj_id;
		cursor.key_beg.key = 0;
		cursor.key_beg.create_tid = 0;
		cursor.key_beg.delete_tid = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.obj_type = 0;
		cursor.asof = ip->obj_asof;
		cursor.flags |= HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_ASOF;

		error = hammer_btree_lookup(&cursor);

		if (error == 0) {
			error = hammer_ip_delete_record(&cursor, last_tid);
			if (error == 0)
				ip->flags |= HAMMER_INODE_DELONDISK;
		}
		hammer_cache_node(cursor.node, &ip->cache[0]);
		hammer_done_cursor(&cursor);
	}

	/*
	 * Write out a new record if the in-memory inode is not marked
	 * as having been deleted.  Update our inode statistics if this
	 * is the first application of the inode on-disk.
	 *
	 * If the inode has been deleted permanently, HAMMER_INODE_DELONDISK
	 * will remain set and prevent further updates.
	 */
	if (error == 0 && (ip->flags & HAMMER_INODE_DELETED) == 0) { 
		record = hammer_alloc_mem_record(ip);
		record->rec.inode = ip->ino_rec;
		record->rec.inode.base.base.create_tid = last_tid;
		record->rec.inode.base.data_len = sizeof(ip->ino_data);
		record->data = (void *)&ip->ino_data;
		error = hammer_ip_sync_record(record, &spike);
		record->flags |= HAMMER_RECF_DELETED;
		hammer_rel_mem_record(record);
		if (error == ENOSPC) {
			error = hammer_spike(&spike);
			if (error == 0)
				goto retry;
		}
		KKASSERT(spike == NULL);
		if (error == 0) {
			ip->flags &= ~(HAMMER_INODE_RDIRTY |
				       HAMMER_INODE_DDIRTY |
				       HAMMER_INODE_DELONDISK |
				       HAMMER_INODE_ITIMES);
			if ((ip->flags & HAMMER_INODE_ONDISK) == 0) {
				hammer_modify_volume(ip->hmp->rootvol);
				++ip->hmp->rootvol->ondisk->vol0_stat_inodes;
				ip->flags |= HAMMER_INODE_ONDISK;
			}
		}
	}
	return(error);
}

/*
 * Update only the itimes fields.  This is done no-historically.  The
 * record is updated in-place on the disk.
 */
static int
hammer_update_itimes(hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	struct hammer_inode_record *rec;
	int error;

	error = 0;
	if ((ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DELONDISK)) ==
	    HAMMER_INODE_ONDISK) {
		hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);
		cursor.key_beg.obj_id = ip->obj_id;
		cursor.key_beg.key = 0;
		cursor.key_beg.create_tid = 0;
		cursor.key_beg.delete_tid = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.obj_type = 0;
		cursor.asof = ip->obj_asof;
		cursor.flags |= HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_ASOF;

		error = hammer_btree_lookup(&cursor);

		if (error == 0) {
			rec = &cursor.record->inode;
			hammer_modify_buffer(cursor.record_buffer);
			rec->ino_atime = ip->ino_rec.ino_atime;
			rec->ino_mtime = ip->ino_rec.ino_mtime;
			ip->flags &= ~HAMMER_INODE_ITIMES;
			/* XXX recalculate crc */
		}
		hammer_cache_node(cursor.node, &ip->cache[0]);
		hammer_done_cursor(&cursor);
	}
	return(error);
}

/*
 * Release a reference on an inode.  If asked to flush the last release
 * will flush the inode.
 */
void
hammer_rel_inode(struct hammer_inode *ip, int flush)
{
	hammer_unref(&ip->lock);
	if (flush)
		ip->flags |= HAMMER_INODE_FLUSH;
	if (ip->lock.refs == 0) {
		if (ip->flags & HAMMER_INODE_FLUSH)
			hammer_unload_inode(ip, (void *)MNT_WAIT);
		else
			hammer_unload_inode(ip, (void *)MNT_NOWAIT);
	}
}

/*
 * Unload and destroy the specified inode.
 *
 * (called via RB_SCAN)
 */
int
hammer_unload_inode(struct hammer_inode *ip, void *data)
{
	int error;

	KASSERT(ip->lock.refs == 0,
		("hammer_unload_inode: %d refs\n", ip->lock.refs));
	KKASSERT(ip->vp == NULL);
	hammer_ref(&ip->lock);

	error = hammer_sync_inode(ip, (int)data, 1);
	if (error)
		kprintf("hammer_sync_inode failed error %d\n", error);
	if (ip->lock.refs == 1) {
		KKASSERT(RB_EMPTY(&ip->rec_tree));
		RB_REMOVE(hammer_ino_rb_tree, &ip->hmp->rb_inos_root, ip);

		hammer_uncache_node(&ip->cache[0]);
		hammer_uncache_node(&ip->cache[1]);
		--hammer_count_inodes;
		kfree(ip, M_HAMMER);
	} else {
		hammer_unref(&ip->lock);
	}
	return(0);
}

/*
 * A transaction has modified an inode, requiring updates as specified by
 * the passed flags.
 *
 * HAMMER_INODE_RDIRTY:	Inode record has been updated
 * HAMMER_INODE_DDIRTY: Inode data has been updated
 * HAMMER_INODE_DELETED: Inode record/data must be deleted
 * HAMMER_INODE_ITIMES: mtime/atime has been updated
 *
 * last_tid is the TID to use to generate the correct TID when the inode
 * is synced to disk.
 */
void
hammer_modify_inode(struct hammer_transaction *trans,
		    struct hammer_inode *ip, int flags)
{
	KKASSERT ((ip->flags & HAMMER_INODE_RO) == 0 ||
		  (HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY|
		   HAMMER_INODE_DELETED|HAMMER_INODE_ITIMES) == 0);

	if (flags &
	    (HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY|HAMMER_INODE_DELETED)) {
		if (hammer_debug_tid) {
			kprintf("hammer_modify_inode: %016llx (%08x)\n", 
				trans->tid, (int)(trans->tid / 1000000000LL));
		}
		ip->last_tid = trans->tid;
	}
	ip->flags |= flags;
}

/*
 * Sync any dirty buffers and records associated with an inode.  The
 * inode's last_tid field is used as the transaction id for the sync,
 * overriding any intermediate TIDs that were used for records.  Note
 * that the dirty buffer cache buffers do not have any knowledge of
 * the transaction id they were modified under.
 *
 * If we can't sync due to a cluster becoming full the spike structure
 * will be filled in and ENOSPC returned.  We must return -ENOSPC to
 * terminate the RB_SCAN.
 */
static int
hammer_sync_inode_callback(hammer_record_t rec, void *data)
{
	struct hammer_cursor **spike = data;
	int error;

	hammer_ref(&rec->lock);
	error = hammer_ip_sync_record(rec, spike);
	hammer_rel_mem_record(rec);

	if (error) {
		error = -error;
		if (error != -ENOSPC) {
			kprintf("hammer_sync_inode_callback: sync failed rec "
				"%p, error %d\n", rec, error);
		}
	}
	return(error);
}

/*
 * XXX error handling
 */
int
hammer_sync_inode(hammer_inode_t ip, int waitfor, int handle_delete)
{
	struct hammer_transaction trans;
	struct hammer_cursor *spike = NULL;
	int error;

	if ((ip->flags & HAMMER_INODE_MODMASK) == 0) {
		return(0);
	}

	hammer_lock_ex(&ip->lock);

	/*
	 * Use the transaction id of the last operation to sync.
	 */
	if (ip->last_tid)
		hammer_start_transaction_tid(&trans, ip->hmp, ip->last_tid);
	else
		hammer_start_transaction(&trans, ip->hmp);

	/*
	 * If the inode has been deleted (nlinks == 0), and the OS no longer
	 * has any references to it (handle_delete != 0), clean up in-memory
	 * data.
	 *
	 * NOTE: We do not set the RDIRTY flag when updating the delete_tid,
	 * setting HAMMER_INODE_DELETED takes care of it.
	 *
	 * NOTE: Because we may sync records within this new transaction,
	 * force the inode update later on to use our transaction id or
	 * the delete_tid of the inode may be less then the create_tid of
	 * the inode update.  XXX shouldn't happen but don't take the chance.
	 *
	 * NOTE: The call to hammer_ip_delete_range() cannot return ENOSPC
	 * so we can pass a NULL spike structure, because no partial data
	 * deletion can occur (yet).
	 */
	if (ip->ino_rec.ino_nlinks == 0 && handle_delete && 
	    (ip->flags & HAMMER_INODE_GONE) == 0) {
		ip->flags |= HAMMER_INODE_GONE;
		if (ip->vp)
			vtruncbuf(ip->vp, 0, HAMMER_BUFSIZE);
		error = hammer_ip_delete_range_all(&trans, ip);
		KKASSERT(RB_EMPTY(&ip->rec_tree));
		ip->ino_rec.base.base.delete_tid = trans.tid;
		hammer_modify_inode(&trans, ip, HAMMER_INODE_DELETED);
		hammer_modify_volume(ip->hmp->rootvol);
		--ip->hmp->rootvol->ondisk->vol0_stat_inodes;
	}

	/*
	 * Sync the buffer cache.
	 */
	if (ip->vp != NULL) {
		error = vfsync(ip->vp, waitfor, 1, NULL, NULL);
		if (RB_ROOT(&ip->vp->v_rbdirty_tree) == NULL)
			ip->flags &= ~HAMMER_INODE_BUFS;
	} else {
		error = 0;
	}


	/*
	 * Now sync related records
	 */
	for (;;) {
		error = RB_SCAN(hammer_rec_rb_tree, &ip->rec_tree, NULL,
				hammer_sync_inode_callback, &spike);
		KKASSERT(error <= 0);
		if (error < 0)
			error = -error;
		if (error == ENOSPC) {
			error = hammer_spike(&spike);
			if (error == 0)
				continue;
		}
		break;
	}
	if (RB_EMPTY(&ip->rec_tree))
		ip->flags &= ~HAMMER_INODE_XDIRTY;

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
		ip->flags &= ~(HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY|
			       HAMMER_INODE_XDIRTY|HAMMER_INODE_ITIMES);
		while (RB_ROOT(&ip->rec_tree)) {
			hammer_record_t rec = RB_ROOT(&ip->rec_tree);
			hammer_ref(&rec->lock);
			rec->flags |= HAMMER_RECF_DELETED;
			hammer_rel_mem_record(rec);
		}
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
	 * If RDIRTY or DDIRTY is set, write out a new record.  If the inode
	 * is already on-disk the old record is marked as deleted.
	 *
	 * If DELETED is set hammer_update_inode() will delete the existing
	 * record without writing out a new one.
	 *
	 * If *ONLY* the ITIMES flag is set we can update the record in-place.
	 */
	if ((ip->flags & (HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY |
			 HAMMER_INODE_ITIMES | HAMMER_INODE_DELETED)) ==
	    HAMMER_INODE_ITIMES) {
		error = hammer_update_itimes(ip);
	} else
	if (ip->flags & (HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY |
			 HAMMER_INODE_ITIMES | HAMMER_INODE_DELETED)) {
		error = hammer_update_inode(ip);
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

