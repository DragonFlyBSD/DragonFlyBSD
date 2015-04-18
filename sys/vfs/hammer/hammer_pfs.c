/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 */
/*
 * HAMMER PFS ioctls - Manage pseudo-fs configurations
 */

#include "hammer.h"

static int hammer_pfs_autodetect(struct hammer_ioc_pseudofs_rw *pfs,
				hammer_inode_t ip);
static int hammer_pfs_rollback(hammer_transaction_t trans,
				hammer_pseudofs_inmem_t pfsm,
				hammer_tid_t trunc_tid);
static int hammer_pfs_delete_at_cursor(hammer_cursor_t cursor,
				hammer_tid_t trunc_tid);

/*
 * Get mirroring/pseudo-fs information
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_get_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	u_int32_t localization;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;
	pfs->bytes = sizeof(struct hammer_pseudofs_data);
	pfs->version = HAMMER_IOC_PSEUDOFS_VERSION;

	pfsm = hammer_load_pseudofs(trans, localization, &error);
	if (error) {
		hammer_rel_pseudofs(trans->hmp, pfsm);
		return(error);
	}

	/*
	 * If the PFS is a master the sync tid is set by normal operation
	 * rather than the mirroring code, and will always track the
	 * real HAMMER filesystem.
	 *
	 * We use flush_tid1, which is the highest fully committed TID.
	 * flush_tid2 is the TID most recently flushed, but the UNDO hasn't
	 * caught up to it yet so a crash will roll us back to flush_tid1.
	 */
	if ((pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0)
		pfsm->pfsd.sync_end_tid = trans->hmp->flush_tid1;

	/*
	 * Copy out to userland.
	 */
	error = 0;
	if (pfs->ondisk && error == 0)
		error = copyout(&pfsm->pfsd, pfs->ondisk, sizeof(pfsm->pfsd));
	hammer_rel_pseudofs(trans->hmp, pfsm);
	return(error);
}

/*
 * Set mirroring/pseudo-fs information
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_set_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct ucred *cred, struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	u_int32_t localization;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;
	if (pfs->version != HAMMER_IOC_PSEUDOFS_VERSION)
		error = EINVAL;

	if (error == 0 && pfs->ondisk) {
		/*
		 * Load the PFS so we can modify our in-core copy.  Ignore
		 * ENOENT errors.
		 */
		pfsm = hammer_load_pseudofs(trans, localization, &error);
		error = copyin(pfs->ondisk, &pfsm->pfsd, sizeof(pfsm->pfsd));

		/*
		 * Save it back, create a root inode if we are in master
		 * mode and no root exists.
		 *
		 * We do not create root inodes for slaves, the root inode
		 * must be mirrored from the master.
		 */
		if (error == 0 &&
		    (pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
			error = hammer_mkroot_pseudofs(trans, cred, pfsm);
		}
		if (error == 0)
			error = hammer_save_pseudofs(trans, pfsm);

		/*
		 * Wakeup anyone waiting for a TID update for this PFS
		 */
		wakeup(&pfsm->pfsd.sync_end_tid);
		hammer_rel_pseudofs(trans->hmp, pfsm);
	}
	return(error);
}

/*
 * Upgrade a slave to a master
 *
 * This is fairly easy to do, but we must physically undo any partial syncs
 * for transaction ids > sync_end_tid.  Effective, we must do a partial
 * rollback.
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_upgrade_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	u_int32_t localization;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;
	if ((error = hammer_unload_pseudofs(trans, localization)) != 0)
		return(error);

	/*
	 * A master id must be set when upgrading
	 */
	pfsm = hammer_load_pseudofs(trans, localization, &error);
	if (error == 0) {
		if ((pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE) != 0) {
			error = hammer_pfs_rollback(trans, pfsm,
					    pfsm->pfsd.sync_end_tid + 1);
			if (error == 0) {
				pfsm->pfsd.mirror_flags &= ~HAMMER_PFSD_SLAVE;
				error = hammer_save_pseudofs(trans, pfsm);
			}
		}
	}
	hammer_rel_pseudofs(trans->hmp, pfsm);
	if (error == EINTR) {
		pfs->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
	return (error);
}

/*
 * Downgrade a master to a slave
 *
 * This is really easy to do, just set the SLAVE flag and update sync_end_tid.
 *
 * We previously did not update sync_end_tid in consideration for a slave
 * upgraded to a master and then downgraded again, but this completely breaks
 * the case where one starts with a master and then downgrades to a slave,
 * then upgrades again.
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_downgrade_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_mount_t hmp = trans->hmp;
	hammer_pseudofs_inmem_t pfsm;
	u_int32_t localization;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;
	if ((error = hammer_unload_pseudofs(trans, localization)) != 0)
		return(error);

	pfsm = hammer_load_pseudofs(trans, localization, &error);
	if (error == 0) {
		if ((pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE) == 0) {
			pfsm->pfsd.mirror_flags |= HAMMER_PFSD_SLAVE;
			if (pfsm->pfsd.sync_end_tid < hmp->flush_tid1)
				pfsm->pfsd.sync_end_tid = hmp->flush_tid1;
			error = hammer_save_pseudofs(trans, pfsm);
		}
	}
	hammer_rel_pseudofs(trans->hmp, pfsm);
	return (error);
}

/*
 * Destroy a PFS
 *
 * We can destroy a PFS by scanning and deleting all of its records in the
 * B-Tree.  The hammer utility will delete the softlink in the primary
 * filesystem.
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_destroy_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	u_int32_t localization;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;

	if ((error = hammer_unload_pseudofs(trans, localization)) != 0)
		return(error);

	pfsm = hammer_load_pseudofs(trans, localization, &error);
	if (error == 0) {
		error = hammer_pfs_rollback(trans, pfsm, 0);
		if (error == 0) {
			pfsm->pfsd.mirror_flags |= HAMMER_PFSD_DELETED;
			error = hammer_save_pseudofs(trans, pfsm);
		}
	}
	hammer_rel_pseudofs(trans->hmp, pfsm);
	if (error == EINTR) {
		pfs->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
	return(error);
}

/*
 * Wait for the PFS to sync past the specified TID
 */
int
hammer_ioc_wait_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			 struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	struct hammer_pseudofs_data pfsd;
	u_int32_t localization;
	hammer_tid_t tid;
	void *waitp;
	int error;

	if ((error = hammer_pfs_autodetect(pfs, ip)) != 0)
		return(error);
	localization = (u_int32_t)pfs->pfs_id << 16;

	if ((error = copyin(pfs->ondisk, &pfsd, sizeof(pfsd))) != 0)
		return(error);

	pfsm = hammer_load_pseudofs(trans, localization, &error);
	if (error == 0) {
		if (pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE) {
			tid = pfsm->pfsd.sync_end_tid;
			waitp = &pfsm->pfsd.sync_end_tid;
		} else {
			tid = trans->hmp->flush_tid1;
			waitp = &trans->hmp->flush_tid1;
		}
		if (tid <= pfsd.sync_end_tid)
			tsleep(waitp, PCATCH, "hmrmwt", 0);
	}
	hammer_rel_pseudofs(trans->hmp, pfsm);
	if (error == EINTR) {
		pfs->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
	return(error);
}

/*
 * Iterate PFS ondisk data.
 * This function basically does the same as hammer_load_pseudofs()
 * except that the purpose of this function is to retrieve data.
 *
 * NOTE: The ip used for ioctl is not necessarily related to the PFS
 * since this ioctl only requires PFS id (or upper 16 bits of ip localization).
 */
int
hammer_ioc_iterate_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pfs_iterate *pi)
{
	struct hammer_cursor cursor;
	struct hammer_ioc_pseudofs_rw pfs;
	hammer_inode_t dip;
	u_int32_t localization;
	int error;

	/*
	 * struct hammer_ioc_pfs_iterate was never necessary.
	 * This ioctl needs extra code only to do conversion.
	 * The name pi->pos is misleading, but it's been exposed
	 * to userspace header..
	 */
	bzero(&pfs, sizeof(pfs));
	pfs.pfs_id = pi->pos;
	pfs.bytes = sizeof(struct hammer_pseudofs_data);  /* dummy */
	if ((error = hammer_pfs_autodetect(&pfs, ip)) != 0)
		return(error);
	pi->pos = pfs.pfs_id;
	localization = (u_int32_t)pi->pos << 16;

	dip = hammer_get_inode(trans, NULL, HAMMER_OBJID_ROOT, HAMMER_MAX_TID,
		HAMMER_DEF_LOCALIZATION, 0, &error);

	error = hammer_init_cursor(trans, &cursor,
		(dip ? &dip->cache[1] : NULL), dip);
	if (error)
		goto out;

	cursor.key_beg.localization = HAMMER_DEF_LOCALIZATION +
				      HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = HAMMER_OBJID_ROOT;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_PFS;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = localization;
	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	error = hammer_ip_lookup(&cursor);
	if (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error == 0) {
			if (pi->ondisk)
				copyout(cursor.data, pi->ondisk, cursor.leaf->data_len);
			localization = cursor.leaf->base.key;
			pi->pos = localization >> 16;
			/*
			 * Caller needs to increment pi->pos each time calling
			 * this ioctl. This ioctl only restores current PFS id.
			 */
		}
	}
out:
	hammer_done_cursor(&cursor);
	if (dip)
		hammer_rel_inode(dip, 0);
	return(error);
}

/*
 * Auto-detect the pseudofs and do basic bounds checking.
 */
static
int
hammer_pfs_autodetect(struct hammer_ioc_pseudofs_rw *pfs, hammer_inode_t ip)
{
	int error = 0;

	if (pfs->pfs_id == -1)
		pfs->pfs_id = (int)(ip->obj_localization >> 16);
	if (pfs->pfs_id < 0 || pfs->pfs_id >= HAMMER_MAX_PFS)
		error = EINVAL;
	if (pfs->bytes < sizeof(struct hammer_pseudofs_data))
		error = EINVAL;
	return(error);
}

/*
 * Rollback the specified PFS to (trunc_tid - 1), removing everything
 * greater or equal to trunc_tid.  The PFS must not have been in no-mirror
 * mode or the MIRROR_FILTERED scan will not work properly.
 *
 * This is typically used to remove any partial syncs when upgrading a
 * slave to a master.  It can theoretically also be used to rollback
 * any PFS, including PFS#0, BUT ONLY TO POINTS THAT HAVE NOT YET BEEN
 * PRUNED, and to points that are older only if they are on a retained
 * (pruning softlink) boundary.
 *
 * Rollbacks destroy information.  If you don't mind inode numbers changing
 * a better way would be to cpdup a snapshot back onto the master.
 */
static
int
hammer_pfs_rollback(hammer_transaction_t trans,
		    hammer_pseudofs_inmem_t pfsm,
		    hammer_tid_t trunc_tid)
{
	struct hammer_cmirror cmirror;
	struct hammer_cursor cursor;
	struct hammer_base_elm key_cur;
	int error;
	int seq;

	bzero(&cmirror, sizeof(cmirror));
	bzero(&key_cur, sizeof(key_cur));
	key_cur.localization = HAMMER_MIN_LOCALIZATION + pfsm->localization;
	key_cur.obj_id = HAMMER_MIN_OBJID;
	key_cur.key = HAMMER_MIN_KEY;
	key_cur.create_tid = 1;
	key_cur.rec_type = HAMMER_MIN_RECTYPE;

	seq = trans->hmp->flusher.done;

retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg = key_cur;
	cursor.key_end.localization = HAMMER_MAX_LOCALIZATION +
				      pfsm->localization;
	cursor.key_end.obj_id = HAMMER_MAX_OBJID;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.key_end.create_tid = HAMMER_MAX_TID;
	cursor.key_end.rec_type = HAMMER_MAX_RECTYPE;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * Do an optimized scan of only records created or modified
	 * >= trunc_tid, so we can fix up those records.  We must
	 * still check the TIDs but this greatly reduces the size of
	 * the scan.
	 */
	cursor.flags |= HAMMER_CURSOR_MIRROR_FILTERED;
	cursor.cmirror = &cmirror;
	cmirror.mirror_tid = trunc_tid;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		/*
		 * Abort the rollback.
		 */
		if (error == 0) {
			error = hammer_signal_check(trans->hmp);
			if (error)
				break;
		}

		/*
		 * We only care about leafs.  Internal nodes can be returned
		 * in mirror-filtered mode (they are used to generate SKIP
		 * mrecords), but we don't need them for this code.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 */
		cursor.flags |= HAMMER_CURSOR_ATEDISK;
		if (cursor.node->ondisk->type == HAMMER_BTREE_TYPE_LEAF) {
			key_cur = cursor.node->ondisk->elms[cursor.index].base;
			error = hammer_pfs_delete_at_cursor(&cursor, trunc_tid);
		}

		while (hammer_flusher_meta_halflimit(trans->hmp) ||
		       hammer_flusher_undo_exhausted(trans, 2)) {
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async_one(trans->hmp);
		}

		if (error == 0)
			error = hammer_btree_iterate(&cursor);
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
failed:
	return(error);
}

/*
 * Helper function - perform rollback on a B-Tree element given trunc_tid.
 *
 * If create_tid >= trunc_tid the record is physically destroyed.
 * If delete_tid >= trunc_tid it will be set to 0, undeleting the record.
 */
static
int
hammer_pfs_delete_at_cursor(hammer_cursor_t cursor, hammer_tid_t trunc_tid)
{
	hammer_btree_leaf_elm_t elm;
	int error;

	elm = &cursor->node->ondisk->elms[cursor->index].leaf;
	if (elm->base.create_tid < trunc_tid &&
	    elm->base.delete_tid < trunc_tid) {
		return(0);
	}

	if (elm->base.create_tid >= trunc_tid) {
		error = hammer_delete_at_cursor(
				cursor, HAMMER_DELETE_DESTROY,
				cursor->trans->tid, cursor->trans->time32,
				1, NULL);
	} else if (elm->base.delete_tid >= trunc_tid) {
		error = hammer_delete_at_cursor(
				cursor, HAMMER_DELETE_ADJUST,
				0, 0,
				1, NULL);
	} else {
		error = 0;
	}
	return(error);
}

