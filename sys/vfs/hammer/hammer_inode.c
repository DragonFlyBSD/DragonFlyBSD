/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_inode.c,v 1.40 2008/04/26 19:08:14 dillon Exp $
 */

#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

static int hammer_unload_inode(struct hammer_inode *ip);
static void hammer_flush_inode_copysync(hammer_inode_t ip);
static int hammer_mark_record_callback(hammer_record_t rec, void *data);

/*
 * The kernel is not actively referencing this vnode but is still holding
 * it cached.
 *
 * This is called from the frontend.
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
	 *
	 * NOTE: called from frontend, use ino_rec instead of sync_ino_rec.
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

		/*
		 * Don't let too many dependancies build up on unreferenced
		 * inodes or we could run ourselves out of memory.
		 */
		if (TAILQ_FIRST(&ip->depend_list)) {
			ip->hmp->reclaim_count += ip->depend_count;
			if (ip->hmp->reclaim_count > 256) {
				ip->hmp->reclaim_count = 0;
				hammer_flusher_async(ip->hmp);
			}
		}
		hammer_rel_inode(ip, 1);
	}
	return(0);
}

/*
 * Return a locked vnode for the specified inode.  The inode must be
 * referenced but NOT LOCKED on entry and will remain referenced on
 * return.
 *
 * Called from the frontend.
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

			/*
			 * Only mark as the root vnode if the ip is not
			 * historical, otherwise the VFS cache will get
			 * confused.  The other half of the special handling
			 * is in hammer_vop_nlookupdotdot().
			 */
			if (ip->obj_id == HAMMER_OBJID_ROOT &&
			    ip->obj_asof == ip->hmp->asof) {
				vp->v_flag |= VROOT;
			}

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
 *
 * Called from the frontend.
 */
struct hammer_inode *
hammer_get_inode(hammer_transaction_t trans, struct hammer_node **cache,
		 u_int64_t obj_id, hammer_tid_t asof, int flags, int *errorp)
{
	hammer_mount_t hmp = trans->hmp;
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
	ip->trunc_off = 0x7FFFFFFFFFFFFFFFLL;
	if (hmp->ronly)
		ip->flags |= HAMMER_INODE_RO;
	RB_INIT(&ip->rec_tree);
	TAILQ_INIT(&ip->bio_list);
	TAILQ_INIT(&ip->bio_alt_list);
	TAILQ_INIT(&ip->depend_list);

	/*
	 * Locate the on-disk inode.
	 */
retry:
	hammer_init_cursor(trans, &cursor, cache);
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
	if (*errorp == EDEADLK) {
		hammer_done_cursor(&cursor);
		goto retry;
	}

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
			KKASSERT(ip->lock.refs == 1);
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
 * returned inode will be referenced and shared-locked.  The caller
 * must unlock and release it when finished.
 *
 * The inode is created in-memory.
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
	ip->flush_state = HAMMER_FST_IDLE;
	ip->flags = HAMMER_INODE_DDIRTY | HAMMER_INODE_RDIRTY |
		    HAMMER_INODE_ITIMES;

	RB_INIT(&ip->rec_tree);
	TAILQ_INIT(&ip->bio_list);
	TAILQ_INIT(&ip->bio_alt_list);
	TAILQ_INIT(&ip->depend_list);

	ip->ino_rec.ino_atime = trans->time;
	ip->ino_rec.ino_mtime = trans->time;
	ip->ino_rec.ino_size = 0;
	ip->ino_rec.ino_nlinks = 0;
	/* XXX */
	ip->ino_rec.base.base.btype = HAMMER_BTREE_TYPE_RECORD;
	ip->ino_rec.base.base.obj_id = ip->obj_id;
	ip->ino_rec.base.base.key = 0;
	ip->ino_rec.base.base.create_tid = 0;
	ip->ino_rec.base.base.delete_tid = 0;
	ip->ino_rec.base.base.rec_type = HAMMER_RECTYPE_INODE;
	ip->ino_rec.base.base.obj_type = hammer_get_obj_type(vap->va_type);

	ip->ino_data.version = HAMMER_INODE_DATA_VERSION;
	ip->ino_data.mode = vap->va_mode;
	ip->ino_data.ctime = trans->time;
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
	hammer_lock_sh(&ip->lock);
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
hammer_update_inode(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	hammer_record_t record;
	int error;

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
retry:
	error = 0;

	if ((ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DELONDISK)) ==
	    HAMMER_INODE_ONDISK) {
		hammer_init_cursor(trans, &cursor, &ip->cache[0]);
		cursor.key_beg.obj_id = ip->obj_id;
		cursor.key_beg.key = 0;
		cursor.key_beg.create_tid = 0;
		cursor.key_beg.delete_tid = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.obj_type = 0;
		cursor.asof = ip->obj_asof;
		cursor.flags |= HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_ASOF;
		cursor.flags |= HAMMER_CURSOR_BACKEND;

		error = hammer_btree_lookup(&cursor);
		if (error) {
			kprintf("error %d\n", error);
			Debugger("hammer_update_inode");
		}


		if (error == 0) {
			error = hammer_ip_delete_record(&cursor, trans->tid);
			if (error && error != EDEADLK) {
				kprintf("error %d\n", error);
				Debugger("hammer_update_inode2");
			}
			if (error == 0)
				ip->flags |= HAMMER_INODE_DELONDISK;
			hammer_cache_node(cursor.node, &ip->cache[0]);
		}
		hammer_done_cursor(&cursor);
		if (error == EDEADLK)
			goto retry;
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
		record->state = HAMMER_FST_FLUSH;
		record->rec.inode = ip->sync_ino_rec;
		record->rec.inode.base.base.create_tid = trans->tid;
		record->rec.inode.base.data_len = sizeof(ip->sync_ino_data);
		record->data = (void *)&ip->sync_ino_data;
		record->flags |= HAMMER_RECF_INTERLOCK_BE;
		error = hammer_ip_sync_record(trans, record);
		if (error) {
			kprintf("error %d\n", error);
			Debugger("hammer_update_inode3");
		}

		/*
		 * The record isn't managed by the inode's record tree,
		 * destroy it whether we succeed or fail.
		 */
		record->flags &= ~HAMMER_RECF_INTERLOCK_BE;
		record->flags |= HAMMER_RECF_DELETED_FE;
		record->state = HAMMER_FST_IDLE;
		KKASSERT(TAILQ_FIRST(&record->depend_list) == NULL);
		hammer_rel_mem_record(record);

		if (error == 0) {
			ip->sync_flags &= ~(HAMMER_INODE_RDIRTY |
					    HAMMER_INODE_DDIRTY |
					    HAMMER_INODE_ITIMES);
			ip->flags &= ~HAMMER_INODE_DELONDISK;
			if ((ip->flags & HAMMER_INODE_ONDISK) == 0) {
				hammer_modify_volume(trans, trans->rootvol,
						     NULL, 0);
				++ip->hmp->rootvol->ondisk->vol0_stat_inodes;
				hammer_modify_volume_done(trans->rootvol);
				ip->flags |= HAMMER_INODE_ONDISK;
			}
		}
	}
	if (error == 0 && (ip->flags & HAMMER_INODE_DELETED)) { 
		/*
		 * Clean out any left-over flags if the inode has been
		 * destroyed.
		 */
		ip->sync_flags &= ~(HAMMER_INODE_RDIRTY |
				    HAMMER_INODE_DDIRTY |
				    HAMMER_INODE_ITIMES);
	}
	return(error);
}

/*
 * Update only the itimes fields.  This is done no-historically.  The
 * record is updated in-place on the disk.
 */
static int
hammer_update_itimes(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	struct hammer_inode_record *rec;
	int error;

retry:
	error = 0;
	if ((ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DELONDISK)) ==
	    HAMMER_INODE_ONDISK) {
		hammer_init_cursor(trans, &cursor, &ip->cache[0]);
		cursor.key_beg.obj_id = ip->obj_id;
		cursor.key_beg.key = 0;
		cursor.key_beg.create_tid = 0;
		cursor.key_beg.delete_tid = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.obj_type = 0;
		cursor.asof = ip->obj_asof;
		cursor.flags |= HAMMER_CURSOR_GET_RECORD | HAMMER_CURSOR_ASOF;
		cursor.flags |= HAMMER_CURSOR_BACKEND;

		error = hammer_btree_lookup(&cursor);
		if (error) {
			kprintf("error %d\n", error);
			Debugger("hammer_update_itimes1");
		}
		if (error == 0) {
			/*
			 * Do not generate UNDO records for atime/mtime
			 * updates.
			 */
			rec = &cursor.record->inode;
			hammer_modify_buffer(cursor.trans, cursor.record_buffer,
					     NULL, 0);
			rec->ino_atime = ip->sync_ino_rec.ino_atime;
			rec->ino_mtime = ip->sync_ino_rec.ino_mtime;
			hammer_modify_buffer_done(cursor.record_buffer);
			ip->sync_flags &= ~HAMMER_INODE_ITIMES;
			/* XXX recalculate crc */
			hammer_cache_node(cursor.node, &ip->cache[0]);
		}
		hammer_done_cursor(&cursor);
		if (error == EDEADLK)
			goto retry;
	}
	return(error);
}

/*
 * Release a reference on an inode.  If asked to flush the last release
 * will flush the inode.
 *
 * On the last reference we queue the inode to the flusher for its final
 * disposition.
 */
void
hammer_rel_inode(struct hammer_inode *ip, int flush)
{
	/*
	 * Handle disposition when dropping the last ref.
	 */
	while (ip->lock.refs == 1) {
#if 0
		/*
		 * XXX this can create a deep stack recursion
		 */
		if (curthread == ip->hmp->flusher_td) {
			/*
			 * We are the flusher, do any required flushes
			 * before unloading the inode.
			 */
			int error = 0;

			KKASSERT(ip->flush_state == HAMMER_FST_IDLE);
			while (error == 0 &&
			       (ip->flags & HAMMER_INODE_MODMASK)) {
				hammer_ref(&ip->lock);
				hammer_flush_inode_copysync(ip);
				error = hammer_sync_inode(ip, 1);
				hammer_flush_inode_done(ip);
			}
			if (error)
				kprintf("hammer_sync_inode failed error %d\n",
					error);
			if (ip->lock.refs > 1)
				continue;
			hammer_unload_inode(ip);
			return;
		}
#endif
		if ((ip->flags & HAMMER_INODE_MODMASK) == 0) {
			hammer_unload_inode(ip);
			return;
		}

		/*
		 * Hand the inode over to the flusher, which will
		 * add another ref to it.
		 */
		if (++ip->hmp->reclaim_count > 256) {
			ip->hmp->reclaim_count = 0;
			hammer_flush_inode(ip, HAMMER_FLUSH_FORCE |
						HAMMER_FLUSH_SIGNAL);
		} else {
			hammer_flush_inode(ip, HAMMER_FLUSH_FORCE);
		}
		/* retry */
	}

	/*
	 * The inode still has multiple refs, drop one ref.  If a flush was
	 * requested make sure the flusher sees it.
	 */
	if (flush && ip->flush_state == HAMMER_FST_IDLE)
		hammer_flush_inode(ip, HAMMER_FLUSH_RELEASE);
	else
		hammer_unref(&ip->lock);
}

/*
 * Unload and destroy the specified inode.  Must be called with one remaining
 * reference.  The reference is disposed of.
 *
 * This can only be called in the context of the flusher.
 */
static int
hammer_unload_inode(struct hammer_inode *ip)
{

	KASSERT(ip->lock.refs == 1,
		("hammer_unload_inode: %d refs\n", ip->lock.refs));
	KKASSERT(ip->vp == NULL);
	KKASSERT(ip->flush_state == HAMMER_FST_IDLE);
	KKASSERT(ip->cursor_ip_refs == 0);
	KKASSERT((ip->flags & HAMMER_INODE_MODMASK) == 0);

	KKASSERT(RB_EMPTY(&ip->rec_tree));
	KKASSERT(TAILQ_EMPTY(&ip->bio_list));
	KKASSERT(TAILQ_EMPTY(&ip->bio_alt_list));

	RB_REMOVE(hammer_ino_rb_tree, &ip->hmp->rb_inos_root, ip);

	hammer_uncache_node(&ip->cache[0]);
	hammer_uncache_node(&ip->cache[1]);
	--hammer_count_inodes;
	kfree(ip, M_HAMMER);

	return(0);
}

/*
 * A transaction has modified an inode, requiring updates as specified by
 * the passed flags.
 *
 * HAMMER_INODE_RDIRTY:	Inode record has been updated
 * HAMMER_INODE_DDIRTY: Inode data has been updated
 * HAMMER_INODE_XDIRTY: Dirty frontend buffer cache buffer strategized
 * HAMMER_INODE_DELETED: Inode record/data must be deleted
 * HAMMER_INODE_ITIMES: mtime/atime has been updated
 */
void
hammer_modify_inode(hammer_transaction_t trans, hammer_inode_t ip, int flags)
{
	KKASSERT ((ip->flags & HAMMER_INODE_RO) == 0 ||
		  (flags & (HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY|
		   HAMMER_INODE_XDIRTY|
		   HAMMER_INODE_DELETED|HAMMER_INODE_ITIMES)) == 0);

	ip->flags |= flags;
}

/*
 * Flush an inode.  If the inode is already being flushed wait for
 * it to complete, then flush it again.  The interlock is against
 * front-end transactions, the backend flusher does not hold the lock.
 *
 * The flusher must distinguish between the records that are part of the
 * flush and any new records created in parallel with the flush.  The
 * inode data and truncation fields are also copied.  BIOs are a bit more
 * troublesome because some dirty buffers may not have been queued yet.
 */
void
hammer_flush_inode(hammer_inode_t ip, int flags)
{
	if (ip->flush_state != HAMMER_FST_IDLE &&
	    (ip->flags & HAMMER_INODE_MODMASK)) {
		ip->flags |= HAMMER_INODE_REFLUSH;
		if (flags & HAMMER_FLUSH_RELEASE) {
			hammer_unref(&ip->lock);
			KKASSERT(ip->lock.refs > 0);
		}
		return;
	}
	if (ip->flush_state == HAMMER_FST_IDLE) {
		if ((ip->flags & HAMMER_INODE_MODMASK) ||
		    (flags & HAMMER_FLUSH_FORCE)) {
			/*
			 * Add a reference to represent the inode being queued
			 * to the flusher.  If the caller wants us to 
			 * release a reference the two cancel each other out.
			 */
			if ((flags & HAMMER_FLUSH_RELEASE) == 0)
				hammer_ref(&ip->lock);

			hammer_flush_inode_copysync(ip);
			/*
			 * Move the inode to the flush list and add a ref to
			 * it representing it on the list.
			 */
			TAILQ_INSERT_TAIL(&ip->hmp->flush_list, ip, flush_entry);
			if (flags & HAMMER_FLUSH_SIGNAL)
				hammer_flusher_async(ip->hmp);
		}
	}
}

/*
 * Helper routine to copy the frontend synchronization state to the backend.
 * This routine may be called by either the frontend or the backend.
 */
static void
hammer_flush_inode_copysync(hammer_inode_t ip)
{
	int error;
	int count;

	/*
	 * Prevent anyone else from trying to do the same thing.
	 */
	ip->flush_state = HAMMER_FST_SETUP;

	/*
	 * Sync the buffer cache.  This will queue the BIOs.  If called
	 * from the context of the flusher the BIO's are thrown into bio_list
	 * regardless of ip->flush_state.
	 */
	if (ip->vp != NULL)
		error = vfsync(ip->vp, MNT_NOWAIT, 1, NULL, NULL);
	else
		error = 0;

	/*
	 * This freezes strategy writes, any further BIOs will be
	 * queued to alt_bio (unless we are 
	 */
	ip->flush_state = HAMMER_FST_FLUSH;

	/*
	 * Snapshot the state of the inode for the backend flusher.
	 *
	 * The truncation must be retained in the frontend until after
	 * we've actually performed the record deletion.
	 */
	ip->sync_flags = (ip->flags & HAMMER_INODE_MODMASK);
	ip->sync_trunc_off = ip->trunc_off;
	ip->sync_ino_rec = ip->ino_rec;
	ip->sync_ino_data = ip->ino_data;
	ip->flags &= ~HAMMER_INODE_MODMASK |
		     HAMMER_INODE_TRUNCATED | HAMMER_INODE_BUFS;

	/*
	 * Fix up the dirty buffer status.
	 */
	if (ip->vp == NULL || RB_ROOT(&ip->vp->v_rbdirty_tree) == NULL)
		ip->flags &= ~HAMMER_INODE_BUFS;
	if (TAILQ_FIRST(&ip->bio_list))
		ip->sync_flags |= HAMMER_INODE_BUFS;
	else
		ip->sync_flags &= ~HAMMER_INODE_BUFS;

	/*
	 * Set the state for the inode's in-memory records.  If some records
	 * could not be marked for backend flush (i.e. deleted records),
	 * re-set the XDIRTY flag.
	 */
	count = RB_SCAN(hammer_rec_rb_tree, &ip->rec_tree, NULL,
			hammer_mark_record_callback, NULL);
	if (count)
		ip->flags |= HAMMER_INODE_XDIRTY;
}

/*
 * Mark records for backend flush, accumulate a count of the number of
 * records which could not be marked.  Records marked for deletion
 * by the frontend never make it to the media.  It is possible for
 * a record queued to the backend to wind up with FE set after the
 * fact, as long as BE has not yet been set.  The backend deals with
 * this race by syncing the record as if FE had not been set, and
 * then converting the record to a delete-on-disk record.
 */
static int
hammer_mark_record_callback(hammer_record_t rec, void *data)
{
	if (rec->state == HAMMER_FST_FLUSH) {
		return(0);
	} else if ((rec->flags & HAMMER_RECF_DELETED_FE) == 0) {
		rec->state = HAMMER_FST_FLUSH;
		hammer_ref(&rec->lock);
		return(0);
	} else {
		return(1);
	}
}



/*
 * Wait for a previously queued flush to complete
 */
void
hammer_wait_inode(hammer_inode_t ip)
{
	while (ip->flush_state == HAMMER_FST_FLUSH) {
		ip->flags |= HAMMER_INODE_FLUSHW;
		tsleep(&ip->flags, 0, "hmrwin", 0);
	}
}

/*
 * Called by the backend code when a flush has been completed.
 * The inode has already been removed from the flush list.
 *
 * A pipelined flush can occur, in which case we must re-enter the
 * inode on the list and re-copy its fields.
 */
void
hammer_flush_inode_done(hammer_inode_t ip)
{
	struct bio *bio;

	KKASSERT(ip->flush_state == HAMMER_FST_FLUSH);

	if (ip->sync_flags)
		kprintf("ip %p leftover sync_flags %08x\n", ip, ip->sync_flags);
	ip->flags |= ip->sync_flags;
	ip->flush_state = HAMMER_FST_IDLE;

	/*
	 * Reflush any BIOs that wound up in the alt list.  Our inode will
	 * also wind up at the end of the flusher's list.
	 */
	while ((bio = TAILQ_FIRST(&ip->bio_alt_list)) != NULL) {
		TAILQ_REMOVE(&ip->bio_alt_list, bio, bio_act);
		TAILQ_INSERT_TAIL(&ip->bio_list, bio, bio_act);
		ip->flags |= HAMMER_INODE_XDIRTY;
		ip->flags |= HAMMER_INODE_REFLUSH;
		kprintf("rebio %p ip %p @%016llx,%d\n", bio, ip, bio->bio_offset, bio->bio_buf->b_bufsize);
	}

	/*
	 * If the frontend made more changes and requested another flush,
	 * do it. 
	 */
	if (ip->flags & HAMMER_INODE_REFLUSH) {
		ip->flags &= ~HAMMER_INODE_REFLUSH;
		hammer_flush_inode(ip, 0);
	} else {
		if (ip->flags & HAMMER_INODE_FLUSHW) {
			ip->flags &= ~HAMMER_INODE_FLUSHW;
			wakeup(&ip->flags);
		}
	}
	hammer_rel_inode(ip, 0);
}

/*
 * Called from hammer_sync_inode() to synchronize in-memory records
 * to the media.
 */
static int
hammer_sync_record_callback(hammer_record_t record, void *data)
{
	hammer_transaction_t trans = data;
	int error;

	/*
	 * Skip records that do not belong to the current flush.  Records
	 * belonging to the flush will have been referenced for us.
	 */
	if (record->state != HAMMER_FST_FLUSH)
		return(0);

	/*
	 * Interlock the record using the BE flag.  Once BE is set the
	 * frontend cannot change the state of FE.
	 *
	 * NOTE: If FE is set prior to us setting BE we still sync the
	 * record out, but the flush completion code converts it to 
	 * a delete-on-disk record instead of destroying it.
	 */
	hammer_lock_ex(&record->lock);
	if (record->flags & HAMMER_RECF_INTERLOCK_BE) {
		hammer_unlock(&record->lock);
		return(0);
	}
	record->flags |= HAMMER_RECF_INTERLOCK_BE;

	/*
	 * If DELETED_FE is set we may have already sent dependant pieces
	 * to the disk and we must flush the record as if it hadn't been
	 * deleted.  This creates a bit of a mess because we have to
	 * have ip_sync_record convert the record to DELETE_ONDISK before
	 * it inserts the B-Tree record.  Otherwise the media sync might
	 * be visible to the frontend.
	 */
	if (record->flags & HAMMER_RECF_DELETED_FE)
		record->flags |= HAMMER_RECF_CONVERT_DELETE_ONDISK;

	/*
	 * Assign the create_tid for new records.  Deletions already
	 * have the record's entire key properly set up.
	 */
	if ((record->flags & HAMMER_RECF_DELETE_ONDISK) == 0)
		record->rec.inode.base.base.create_tid = trans->tid;
	error = hammer_ip_sync_record(trans, record);

	if (error) {
		error = -error;
		if (error != -ENOSPC) {
			kprintf("hammer_sync_record_callback: sync failed rec "
				"%p, error %d\n", record, error);
			Debugger("sync failed rec");
		}
	}
	hammer_flush_record_done(record, error);
	return(error);
}

/*
 * XXX error handling
 */
int
hammer_sync_inode(hammer_inode_t ip, int handle_delete)
{
	struct hammer_transaction trans;
	struct bio *bio;
	hammer_depend_t depend;
	int error, tmp_error;

	if ((ip->sync_flags & HAMMER_INODE_MODMASK) == 0 &&
	    handle_delete == 0) {
		return(0);
	}


	hammer_lock_ex(&ip->lock);

	hammer_start_transaction_fls(&trans, ip->hmp);

	/*
	 * Any (directory) records this inode depends on must also be
	 * synchronized.  The directory itself only needs to be flushed
	 * if its inode is not already on-disk.
	 */
	while ((depend = TAILQ_FIRST(&ip->depend_list)) != NULL) {
		hammer_record_t record;

		record = depend->record;
                TAILQ_REMOVE(&depend->record->depend_list, depend, rec_entry);
                TAILQ_REMOVE(&ip->depend_list, depend, ip_entry);
		--ip->depend_count;
		if (record->state != HAMMER_FST_FLUSH) {
			record->state = HAMMER_FST_FLUSH;
			/* add ref (steal ref from dependancy) */
		} else {
			/* remove ref related to dependancy */
			/* record still has at least one ref from state */
			hammer_unref(&record->lock);
			KKASSERT(record->lock.refs > 0);
		}
		if (record->ip->flags & HAMMER_INODE_ONDISK) {
			kprintf("I");
			hammer_sync_record_callback(record, &trans);
		} else {
			kprintf("J");
			hammer_flush_inode(record->ip, 0);
		}
		hammer_unref(&ip->lock);
		KKASSERT(ip->lock.refs > 0);
                kfree(depend, M_HAMMER);
	}


	/*
	 * Sync inode deletions and truncations.
	 */
	if (ip->sync_ino_rec.ino_nlinks == 0 && handle_delete && 
	    (ip->flags & HAMMER_INODE_GONE) == 0) {
		/*
		 * Handle the case where the inode has been completely deleted
		 * and is no longer referenceable from the filesystem
		 * namespace.
		 *
		 * NOTE: We do not set the RDIRTY flag when updating the
		 * delete_tid, setting HAMMER_INODE_DELETED takes care of it.
		 */

		ip->flags |= HAMMER_INODE_GONE | HAMMER_INODE_DELETED;
		ip->flags &= ~HAMMER_INODE_TRUNCATED;
		ip->sync_flags &= ~HAMMER_INODE_TRUNCATED;
		if (ip->vp)
			vtruncbuf(ip->vp, 0, HAMMER_BUFSIZE);
		error = hammer_ip_delete_range_all(&trans, ip);
		if (error)
			Debugger("hammer_ip_delete_range_all errored");

		/*
		 * Sanity check.  The only records that remain should be
		 * marked for back-end deletion.
		 */
		{
			hammer_record_t rec;

			RB_FOREACH(rec, hammer_rec_rb_tree, &ip->rec_tree) {
				KKASSERT(rec->state == HAMMER_FST_FLUSH);
			}
		}

		/*
		 * Set delete_tid in both the frontend and backend
		 * copy of the inode record.
		 */
		ip->ino_rec.base.base.delete_tid = trans.tid;
		ip->sync_ino_rec.base.base.delete_tid = trans.tid;

		/*
		 * Indicate that the inode has/is-being deleted.
		 */
		ip->flags |= HAMMER_NODE_DELETED;
		hammer_modify_inode(&trans, ip, HAMMER_INODE_RDIRTY);
		hammer_modify_volume(&trans, trans.rootvol, NULL, 0);
		--ip->hmp->rootvol->ondisk->vol0_stat_inodes;
		hammer_modify_volume_done(trans.rootvol);
	} else if (ip->sync_flags & HAMMER_INODE_TRUNCATED) {
		/*
		 * Interlock trunc_off.  The VOP front-end may continue to
		 * make adjustments to it while we are blocked.
		 */
		off_t trunc_off;
		off_t aligned_trunc_off;

		trunc_off = ip->sync_trunc_off;
		aligned_trunc_off = (trunc_off + HAMMER_BUFMASK) &
				    ~HAMMER_BUFMASK64;

		/*
		 * Delete any whole blocks on-media.  The front-end has
		 * already cleaned out any partial block and made it
		 * pending.  The front-end may have updated trunc_off
		 * while we were blocked so do not just unconditionally
		 * set it to the maximum offset.
		 */
		kprintf("sync truncation range @ %016llx\n", aligned_trunc_off);
		error = hammer_ip_delete_range(&trans, ip,
						aligned_trunc_off,
						0x7FFFFFFFFFFFFFFFLL);
		if (error)
			Debugger("hammer_ip_delete_range errored");
		ip->sync_flags &= ~HAMMER_INODE_TRUNCATED;
		if (ip->trunc_off >= trunc_off) {
			ip->trunc_off = 0x7FFFFFFFFFFFFFFFLL;
			ip->flags &= ~HAMMER_INODE_TRUNCATED;
		}
	}

	error = 0;	/* XXX vfsync used to be here */

	/*
	 * Flush any queued BIOs.
	 */
	while ((bio = TAILQ_FIRST(&ip->bio_list)) != NULL) {
		TAILQ_REMOVE(&ip->bio_list, bio, bio_act);
#if 0
		kprintf("dowrite %016llx ip %p bio %p @ %016llx\n", trans.tid, ip, bio, bio->bio_offset);
#endif
		tmp_error = hammer_dowrite(&trans, ip, bio);
		if (tmp_error)
			error = tmp_error;
	}
	ip->sync_flags &= ~HAMMER_INODE_BUFS;

	/*
	 * Now sync related records.
	 */
	for (;;) {
		tmp_error = RB_SCAN(hammer_rec_rb_tree, &ip->rec_tree, NULL,
				hammer_sync_record_callback, &trans);
		KKASSERT(error <= 0);
		if (tmp_error < 0)
			tmp_error = -error;
		if (tmp_error)
			error = tmp_error;
		break;
	}

	/*
	 * XDIRTY represents rec_tree and bio_list.  However, rec_tree may
	 * contain new front-end records so short of scanning it we can't
	 * just test whether it is empty or not. 
	 *
	 * If no error occured assume we succeeded.
	 */
	if (error == 0)
		ip->sync_flags &= ~HAMMER_INODE_XDIRTY;

	if (error)
		Debugger("RB_SCAN errored");

	/*
	 * Now update the inode's on-disk inode-data and/or on-disk record.
	 * DELETED and ONDISK are managed only in ip->flags.
	 */
	switch(ip->flags & (HAMMER_INODE_DELETED | HAMMER_INODE_ONDISK)) {
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
		ip->sync_flags &= ~(HAMMER_INODE_RDIRTY|HAMMER_INODE_DDIRTY|
				    HAMMER_INODE_XDIRTY|HAMMER_INODE_ITIMES);
		while (RB_ROOT(&ip->rec_tree)) {
			hammer_record_t record = RB_ROOT(&ip->rec_tree);
			hammer_ref(&record->lock);
			KKASSERT(record->lock.refs == 1);
			record->flags |= HAMMER_RECF_DELETED_FE;
			record->flags |= HAMMER_RECF_DELETED_BE;
			hammer_cleardep_mem_record(record);
			hammer_rel_mem_record(record);
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
		 * to force an initial record to be written.  Also set
		 * the create_tid for the inode.
		 *
		 * Set create_tid in both the frontend and backend
		 * copy of the inode record.
		 */
		ip->ino_rec.base.base.create_tid = trans.tid;
		ip->sync_ino_rec.base.base.create_tid = trans.tid;
		ip->sync_flags |= HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY;
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
	if (ip->flags & HAMMER_INODE_DELETED) {
		error = hammer_update_inode(&trans, ip);
	} else 
	if ((ip->sync_flags & (HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY |
			       HAMMER_INODE_ITIMES)) == HAMMER_INODE_ITIMES) {
		error = hammer_update_itimes(&trans, ip);
	} else
	if (ip->sync_flags & (HAMMER_INODE_RDIRTY | HAMMER_INODE_DDIRTY |
			      HAMMER_INODE_ITIMES)) {
		error = hammer_update_inode(&trans, ip);
	}
	if (error)
		Debugger("hammer_update_itimes/inode errored");

	/*
	 * Save the TID we used to sync the inode with to make sure we
	 * do not improperly reuse it.
	 */
	hammer_unlock(&ip->lock);
	hammer_done_transaction(&trans);
	return(error);
}

