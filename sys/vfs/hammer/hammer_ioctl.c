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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_ioctl.c,v 1.32 2008/11/13 02:23:29 dillon Exp $
 */

#include "hammer.h"

static int hammer_ioc_gethistory(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_history *hist);
static int hammer_ioc_synctid(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_synctid *std);
static int hammer_ioc_get_version(hammer_transaction_t trans,
				hammer_inode_t ip,
				struct hammer_ioc_version *ver);
static int hammer_ioc_set_version(hammer_transaction_t trans,
				hammer_inode_t ip,
				struct hammer_ioc_version *ver);
static int hammer_ioc_get_info(hammer_transaction_t trans,
				struct hammer_ioc_info *info);
static int hammer_ioc_pfs_iterate(hammer_transaction_t trans,
				struct hammer_ioc_pfs_iterate *pi);
static int hammer_ioc_add_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_snapshot *snap);
static int hammer_ioc_del_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_snapshot *snap);
static int hammer_ioc_get_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_snapshot *snap);
static int hammer_ioc_get_config(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_config *snap);
static int hammer_ioc_set_config(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_config *snap);
static int hammer_ioc_get_data(hammer_transaction_t trans, hammer_inode_t ip,
				struct hammer_ioc_data *data);

int
hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
	     struct ucred *cred)
{
	struct hammer_transaction trans;
	int error;

	error = priv_check_cred(cred, PRIV_HAMMER_IOCTL, 0);

	hammer_start_transaction(&trans, ip->hmp);

	switch(com) {
	case HAMMERIOC_PRUNE:
		if (error == 0) {
			error = hammer_ioc_prune(&trans, ip,
					(struct hammer_ioc_prune *)data);
		}
		break;
	case HAMMERIOC_GETHISTORY:
		error = hammer_ioc_gethistory(&trans, ip,
					(struct hammer_ioc_history *)data);
		break;
	case HAMMERIOC_REBLOCK:
		if (error == 0) {
			error = hammer_ioc_reblock(&trans, ip,
					(struct hammer_ioc_reblock *)data);
		}
		break;
	case HAMMERIOC_REBALANCE:
		/*
		 * Rebalancing needs to lock a lot of B-Tree nodes.  The
		 * children and children's children.  Systems with very
		 * little memory will not be able to do it.
		 */
		if (error == 0 && nbuf < HAMMER_REBALANCE_MIN_BUFS) {
			kprintf("hammer: System has insufficient buffers "
				"to rebalance the tree.  nbuf < %d\n",
				HAMMER_REBALANCE_MIN_BUFS);
			error = ENOSPC;
		}
		if (error == 0) {
			error = hammer_ioc_rebalance(&trans, ip,
					(struct hammer_ioc_rebalance *)data);
		}
		break;
	case HAMMERIOC_SYNCTID:
		error = hammer_ioc_synctid(&trans, ip,
					(struct hammer_ioc_synctid *)data);
		break;
	case HAMMERIOC_GET_PSEUDOFS:
		error = hammer_ioc_get_pseudofs(&trans, ip,
				    (struct hammer_ioc_pseudofs_rw *)data);
		break;
	case HAMMERIOC_SET_PSEUDOFS:
		if (error == 0) {
			error = hammer_ioc_set_pseudofs(&trans, ip, cred,
				    (struct hammer_ioc_pseudofs_rw *)data);
		}
		break;
	case HAMMERIOC_UPG_PSEUDOFS:
		if (error == 0) {
			error = hammer_ioc_upgrade_pseudofs(&trans, ip, 
				    (struct hammer_ioc_pseudofs_rw *)data);
		}
		break;
	case HAMMERIOC_DGD_PSEUDOFS:
		if (error == 0) {
			error = hammer_ioc_downgrade_pseudofs(&trans, ip,
				    (struct hammer_ioc_pseudofs_rw *)data);
		}
		break;
	case HAMMERIOC_RMR_PSEUDOFS:
		if (error == 0) {
			error = hammer_ioc_destroy_pseudofs(&trans, ip,
				    (struct hammer_ioc_pseudofs_rw *)data);
		}
		break;
	case HAMMERIOC_WAI_PSEUDOFS:
		if (error == 0) {
			error = hammer_ioc_wait_pseudofs(&trans, ip,
				    (struct hammer_ioc_pseudofs_rw *)data);
		}
		break;
	case HAMMERIOC_MIRROR_READ:
		if (error == 0) {
			error = hammer_ioc_mirror_read(&trans, ip,
				    (struct hammer_ioc_mirror_rw *)data);
		}
		break;
	case HAMMERIOC_MIRROR_WRITE:
		if (error == 0) {
			error = hammer_ioc_mirror_write(&trans, ip,
				    (struct hammer_ioc_mirror_rw *)data);
		}
		break;
	case HAMMERIOC_GET_VERSION:
		error = hammer_ioc_get_version(&trans, ip, 
				    (struct hammer_ioc_version *)data);
		break;
	case HAMMERIOC_GET_INFO:
		error = hammer_ioc_get_info(&trans,
				    (struct hammer_ioc_info *)data);
		break;
	case HAMMERIOC_SET_VERSION:
		if (error == 0) {
			error = hammer_ioc_set_version(&trans, ip, 
					    (struct hammer_ioc_version *)data);
		}
		break;
	case HAMMERIOC_ADD_VOLUME:
		if (error == 0) {
			error = priv_check_cred(cred, PRIV_HAMMER_VOLUME, 0);
			if (error == 0)
				error = hammer_ioc_volume_add(&trans, ip,
					    (struct hammer_ioc_volume *)data);
		}
		break;
	case HAMMERIOC_DEL_VOLUME:
		if (error == 0) {
			error = priv_check_cred(cred, PRIV_HAMMER_VOLUME, 0);
			if (error == 0)
				error = hammer_ioc_volume_del(&trans, ip,
					    (struct hammer_ioc_volume *)data);
		}
		break;
	case HAMMERIOC_LIST_VOLUMES:
		error = hammer_ioc_volume_list(&trans, ip,
		    (struct hammer_ioc_volume_list *)data);
		break;
	case HAMMERIOC_ADD_SNAPSHOT:
		if (error == 0) {
			error = hammer_ioc_add_snapshot(
					&trans, ip, (struct hammer_ioc_snapshot *)data);
		}
		break;
	case HAMMERIOC_DEL_SNAPSHOT:
		if (error == 0) {
			error = hammer_ioc_del_snapshot(
					&trans, ip, (struct hammer_ioc_snapshot *)data);
		}
		break;
	case HAMMERIOC_GET_SNAPSHOT:
		error = hammer_ioc_get_snapshot(
					&trans, ip, (struct hammer_ioc_snapshot *)data);
		break;
	case HAMMERIOC_GET_CONFIG:
		error = hammer_ioc_get_config(
					&trans, ip, (struct hammer_ioc_config *)data);
		break;
	case HAMMERIOC_SET_CONFIG:
		if (error == 0) {
			error = hammer_ioc_set_config(
					&trans, ip, (struct hammer_ioc_config *)data);
		}
		break;
	case HAMMERIOC_DEDUP:
		if (error == 0) {
			error = hammer_ioc_dedup(
					&trans, ip, (struct hammer_ioc_dedup *)data);
		}
		break;
	case HAMMERIOC_GET_DATA:
		if (error == 0) {
			error = hammer_ioc_get_data(
					&trans, ip, (struct hammer_ioc_data *)data);
		}
		break;
	case HAMMERIOC_PFS_ITERATE:
		error = hammer_ioc_pfs_iterate(
			&trans, (struct hammer_ioc_pfs_iterate *)data);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * Iterate through an object's inode or an object's records and record
 * modification TIDs.
 */
static void add_history(hammer_inode_t ip, struct hammer_ioc_history *hist,
			hammer_btree_elm_t elm);

static
int
hammer_ioc_gethistory(hammer_transaction_t trans, hammer_inode_t ip,
		      struct hammer_ioc_history *hist)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;

	/*
	 * Validate the structure and initialize for return.
	 */
	if (hist->beg_tid > hist->end_tid)
		return(EINVAL);
	if (hist->head.flags & HAMMER_IOC_HISTORY_ATKEY) {
		if (hist->key > hist->nxt_key)
			return(EINVAL);
	}

	hist->obj_id = ip->obj_id;
	hist->count = 0;
	hist->nxt_tid = hist->end_tid;
	hist->head.flags &= ~HAMMER_IOC_HISTORY_NEXT_TID;
	hist->head.flags &= ~HAMMER_IOC_HISTORY_NEXT_KEY;
	hist->head.flags &= ~HAMMER_IOC_HISTORY_EOF;
	hist->head.flags &= ~HAMMER_IOC_HISTORY_UNSYNCED;
	if ((ip->flags & HAMMER_INODE_MODMASK) & 
	    ~(HAMMER_INODE_ATIME | HAMMER_INODE_MTIME)) {
		hist->head.flags |= HAMMER_IOC_HISTORY_UNSYNCED;
	}

	/*
	 * Setup the cursor.  We can't handle undeletable records
	 * (create_tid of 0) at the moment.  A create_tid of 0 has
	 * a special meaning and cannot be specified in the cursor.
	 */
	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	cursor.key_beg.obj_id = hist->obj_id;
	cursor.key_beg.create_tid = hist->beg_tid;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	if (cursor.key_beg.create_tid == HAMMER_MIN_TID)
		cursor.key_beg.create_tid = 1;

	cursor.key_end.obj_id = hist->obj_id;
	cursor.key_end.create_tid = hist->end_tid;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_EXCLUSIVE;

	if (hist->head.flags & HAMMER_IOC_HISTORY_ATKEY) {
		/*
		 * key-range within the file.  For a regular file the
		 * on-disk key represents BASE+LEN, not BASE, so the
		 * first possible record containing the offset 'key'
		 * has an on-disk key of (key + 1).
		 */
		cursor.key_beg.key = hist->key;
		cursor.key_end.key = HAMMER_MAX_KEY;
		cursor.key_beg.localization = ip->obj_localization + 
					      HAMMER_LOCALIZE_MISC;
		cursor.key_end.localization = ip->obj_localization + 
					      HAMMER_LOCALIZE_MISC;

		switch(ip->ino_data.obj_type) {
		case HAMMER_OBJTYPE_REGFILE:
			++cursor.key_beg.key;
			cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
			break;
		case HAMMER_OBJTYPE_DIRECTORY:
			cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
			cursor.key_beg.localization = ip->obj_localization +
						hammer_dir_localization(ip);
			cursor.key_end.localization = ip->obj_localization +
						hammer_dir_localization(ip);
			break;
		case HAMMER_OBJTYPE_DBFILE:
			cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
			break;
		default:
			error = EINVAL;
			break;
		}
		cursor.key_end.rec_type = cursor.key_beg.rec_type;
	} else {
		/*
		 * The inode itself.
		 */
		cursor.key_beg.key = 0;
		cursor.key_end.key = 0;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_end.rec_type = HAMMER_RECTYPE_INODE;
		cursor.key_beg.localization = ip->obj_localization +
					      HAMMER_LOCALIZE_INODE;
		cursor.key_end.localization = ip->obj_localization +
					      HAMMER_LOCALIZE_INODE;
	}

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		elm = &cursor.node->ondisk->elms[cursor.index];

		add_history(ip, hist, elm);
		if (hist->head.flags & (HAMMER_IOC_HISTORY_NEXT_TID |
				        HAMMER_IOC_HISTORY_NEXT_KEY |
				        HAMMER_IOC_HISTORY_EOF)) {
			break;
		}
		error = hammer_btree_iterate(&cursor);
	}
	if (error == ENOENT) {
		hist->head.flags |= HAMMER_IOC_HISTORY_EOF;
		error = 0;
	}
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Add the scanned element to the ioctl return structure.  Some special
 * casing is required for regular files to accomodate how data ranges are
 * stored on-disk.
 */
static void
add_history(hammer_inode_t ip, struct hammer_ioc_history *hist,
	    hammer_btree_elm_t elm)
{
	int i;

	if (elm->base.btype != HAMMER_BTREE_TYPE_RECORD)
		return;
	if ((hist->head.flags & HAMMER_IOC_HISTORY_ATKEY) &&
	    ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE) {
		/*
		 * Adjust nxt_key
		 */
		if (hist->nxt_key > elm->leaf.base.key - elm->leaf.data_len &&
		    hist->key < elm->leaf.base.key - elm->leaf.data_len) {
			hist->nxt_key = elm->leaf.base.key - elm->leaf.data_len;
		}
		if (hist->nxt_key > elm->leaf.base.key)
			hist->nxt_key = elm->leaf.base.key;

		/*
		 * Record is beyond MAXPHYS, there won't be any more records
		 * in the iteration covering the requested offset (key).
		 */
		if (elm->leaf.base.key >= MAXPHYS &&
		    elm->leaf.base.key - MAXPHYS > hist->key) {
			hist->head.flags |= HAMMER_IOC_HISTORY_NEXT_KEY;
		}

		/*
		 * Data-range of record does not cover the key.
		 */
		if (elm->leaf.base.key - elm->leaf.data_len > hist->key)
			return;

	} else if (hist->head.flags & HAMMER_IOC_HISTORY_ATKEY) {
		/*
		 * Adjust nxt_key
		 */
		if (hist->nxt_key > elm->leaf.base.key &&
		    hist->key < elm->leaf.base.key) {
			hist->nxt_key = elm->leaf.base.key;
		}

		/*
		 * Record is beyond the requested key.
		 */
		if (elm->leaf.base.key > hist->key)
			hist->head.flags |= HAMMER_IOC_HISTORY_NEXT_KEY;
	}

	/*
	 * Add create_tid if it is in-bounds.
	 */
	i = hist->count;
	if ((i == 0 ||
	     elm->leaf.base.create_tid != hist->hist_ary[i - 1].tid) &&
	    elm->leaf.base.create_tid >= hist->beg_tid &&
	    elm->leaf.base.create_tid < hist->end_tid) {
		if (hist->count == HAMMER_MAX_HISTORY_ELMS) {
			hist->nxt_tid = elm->leaf.base.create_tid;
			hist->head.flags |= HAMMER_IOC_HISTORY_NEXT_TID;
			return;
		}
		hist->hist_ary[i].tid = elm->leaf.base.create_tid;
		hist->hist_ary[i].time32 = elm->leaf.create_ts;
		++hist->count;
	}

	/*
	 * Add delete_tid if it is in-bounds.  Note that different portions
	 * of the history may have overlapping data ranges with different
	 * delete_tid's.  If this case occurs the delete_tid may match the
	 * create_tid of a following record.  XXX
	 *
	 *	[        ]
	 *            [     ]
	 */
	i = hist->count;
	if (elm->leaf.base.delete_tid &&
	    elm->leaf.base.delete_tid >= hist->beg_tid &&
	    elm->leaf.base.delete_tid < hist->end_tid) {
		if (i == HAMMER_MAX_HISTORY_ELMS) {
			hist->nxt_tid = elm->leaf.base.delete_tid;
			hist->head.flags |= HAMMER_IOC_HISTORY_NEXT_TID;
			return;
		}
		hist->hist_ary[i].tid = elm->leaf.base.delete_tid;
		hist->hist_ary[i].time32 = elm->leaf.delete_ts;
		++hist->count;
	}
}

/*
 * Acquire synchronization TID
 */
static
int
hammer_ioc_synctid(hammer_transaction_t trans, hammer_inode_t ip,
		   struct hammer_ioc_synctid *std)
{
	hammer_mount_t hmp = ip->hmp;
	int error = 0;

	switch(std->op) {
	case HAMMER_SYNCTID_NONE:
		std->tid = hmp->flusher.tid;	/* inaccurate */
		break;
	case HAMMER_SYNCTID_ASYNC:
		hammer_queue_inodes_flusher(hmp, MNT_NOWAIT);
		hammer_flusher_async(hmp, NULL);
		std->tid = hmp->flusher.tid;	/* inaccurate */
		break;
	case HAMMER_SYNCTID_SYNC1:
		hammer_queue_inodes_flusher(hmp, MNT_WAIT);
		hammer_flusher_sync(hmp);
		std->tid = hmp->flusher.tid;
		break;
	case HAMMER_SYNCTID_SYNC2:
		hammer_queue_inodes_flusher(hmp, MNT_WAIT);
		hammer_flusher_sync(hmp);
		std->tid = hmp->flusher.tid;
		hammer_flusher_sync(hmp);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return(error);
}

/*
 * Retrieve version info.
 *
 * Load min_version, wip_version, and max_versino.  If cur_version is passed
 * as 0 then load the current version into cur_version.  Load the description
 * for cur_version into the description array.
 *
 * Returns 0 on success, EINVAL if cur_version is non-zero and set to an
 * unsupported value.
 */
static
int
hammer_ioc_get_version(hammer_transaction_t trans, hammer_inode_t ip,
		   struct hammer_ioc_version *ver)
{
	int error = 0;

	ver->min_version = HAMMER_VOL_VERSION_MIN;
	ver->wip_version = HAMMER_VOL_VERSION_WIP;
	ver->max_version = HAMMER_VOL_VERSION_MAX;
	if (ver->cur_version == 0)
		ver->cur_version = trans->hmp->version;
	switch(ver->cur_version) {
	case 1:
		ksnprintf(ver->description, sizeof(ver->description),
			 "First HAMMER release (DragonFly 2.0+)");
		break;
	case 2:
		ksnprintf(ver->description, sizeof(ver->description),
			 "New directory entry layout (DragonFly 2.3+)");
		break;
	case 3:
		ksnprintf(ver->description, sizeof(ver->description),
			 "New snapshot management (DragonFly 2.5+)");
		break;
	case 4:
		ksnprintf(ver->description, sizeof(ver->description),
			 "New undo/flush, faster flush/sync (DragonFly 2.5+)");
		break;
	case 5:
		ksnprintf(ver->description, sizeof(ver->description),
			 "Adjustments for dedup support (DragonFly 2.9+)");
		break;
	case 6:
		ksnprintf(ver->description, sizeof(ver->description),
			  "Directory Hash ALG1 (tmp/rename resistance)");
		break;
	default:
		ksnprintf(ver->description, sizeof(ver->description),
			 "Unknown");
		error = EINVAL;
		break;
	}
	return(error);
};

/*
 * Set version info
 */
static
int
hammer_ioc_set_version(hammer_transaction_t trans, hammer_inode_t ip,
		   struct hammer_ioc_version *ver)
{
	hammer_mount_t hmp = trans->hmp;
	struct hammer_cursor cursor;
	hammer_volume_t volume;
	int error;
	int over = hmp->version;

	/*
	 * Generally do not allow downgrades.  However, version 4 can
	 * be downgraded to version 3.
	 */
	if (ver->cur_version < hmp->version) {
		if (!(ver->cur_version == 3 && hmp->version == 4))
			return(EINVAL);
	}
	if (ver->cur_version == hmp->version)
		return(0);
	if (ver->cur_version > HAMMER_VOL_VERSION_MAX)
		return(EINVAL);
	if (hmp->ronly)
		return(EROFS);

	/*
	 * Update the root volume header and the version cached in
	 * the hammer_mount structure.
	 */
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error)
		goto failed;
	hammer_lock_ex(&hmp->flusher.finalize_lock);
	hammer_sync_lock_ex(trans);
	hmp->version = ver->cur_version;

	/*
	 * If upgrading from version < 4 to version >= 4 the UNDO FIFO
	 * must be reinitialized.
	 */
	if (over < HAMMER_VOL_VERSION_FOUR &&
	    ver->cur_version >= HAMMER_VOL_VERSION_FOUR) {
		kprintf("upgrade undo to version 4\n");
		error = hammer_upgrade_undo_4(trans);
		if (error)
			goto failed;
	}

	/*
	 * Adjust the version in the volume header
	 */
	volume = hammer_get_root_volume(hmp, &error);
	KKASSERT(error == 0);
	hammer_modify_volume_field(cursor.trans, volume, vol_version);
	volume->ondisk->vol_version = ver->cur_version;
	hammer_modify_volume_done(volume);
	hammer_rel_volume(volume, 0);

	hammer_sync_unlock(trans);
	hammer_unlock(&hmp->flusher.finalize_lock);
failed:
	ver->head.error = error;
	hammer_done_cursor(&cursor);
	return(0);
}

/*
 * Get information
 */
static
int
hammer_ioc_get_info(hammer_transaction_t trans, struct hammer_ioc_info *info)
{
	struct hammer_volume_ondisk	*od = trans->hmp->rootvol->ondisk;
	struct hammer_mount 		*hm = trans->hmp;

	/* Fill the structure with the necessary information */
	_hammer_checkspace(hm, HAMMER_CHKSPC_WRITE, &info->rsvbigblocks);
	info->rsvbigblocks = info->rsvbigblocks >> HAMMER_BIGBLOCK_BITS;
	strlcpy(info->vol_name, od->vol_name, sizeof(od->vol_name));

	info->vol_fsid = hm->fsid;
	info->vol_fstype = od->vol_fstype;
	info->version = hm->version;

	info->inodes = od->vol0_stat_inodes;
	info->bigblocks = od->vol0_stat_bigblocks;
	info->freebigblocks = od->vol0_stat_freebigblocks;
	info->nvolumes = hm->nvolumes;

	return 0;
}

/*
 * Add a snapshot transction id(s) to the list of snapshots.
 *
 * NOTE: Records are created with an allocated TID.  If a flush cycle
 *	 is in progress the record may be synced in the current flush
 *	 cycle and the volume header will reflect the allocation of the
 *	 TID, but the synchronization point may not catch up to the
 *	 TID until the next flush cycle.
 */
static
int
hammer_ioc_add_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_snapshot *snap)
{
	hammer_mount_t hmp = ip->hmp;
	struct hammer_btree_leaf_elm leaf;
	struct hammer_cursor cursor;
	int error;

	/*
	 * Validate structure
	 */
	if (snap->count > HAMMER_SNAPS_PER_IOCTL)
		return (EINVAL);
	if (snap->index > snap->count)
		return (EINVAL);

	hammer_lock_ex(&hmp->snapshot_lock);
again:
	/*
	 * Look for keys starting after the previous iteration, or at
	 * the beginning if snap->count is 0.
	 */
	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_BACKEND | HAMMER_CURSOR_ASOF;

	bzero(&leaf, sizeof(leaf));
	leaf.base.obj_id = HAMMER_OBJID_ROOT;
	leaf.base.rec_type = HAMMER_RECTYPE_SNAPSHOT;
	leaf.base.create_tid = hammer_alloc_tid(hmp, 1);
	leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	leaf.base.localization = ip->obj_localization + HAMMER_LOCALIZE_INODE;
	leaf.data_len = sizeof(struct hammer_snapshot_data);

	while (snap->index < snap->count) {
		leaf.base.key = (int64_t)snap->snaps[snap->index].tid;
		cursor.key_beg = leaf.base;
		error = hammer_btree_lookup(&cursor);
		if (error == 0) {
			error = EEXIST;
			break;
		}

		/*
		 * NOTE: Must reload key_beg after an ASOF search because
		 *	 the create_tid may have been modified during the
		 *	 search.
		 */
		cursor.flags &= ~HAMMER_CURSOR_ASOF;
		cursor.key_beg = leaf.base;
		error = hammer_create_at_cursor(&cursor, &leaf,
						&snap->snaps[snap->index],
						HAMMER_CREATE_MODE_SYS);
		if (error == EDEADLK) {
			hammer_done_cursor(&cursor);
			goto again;
		}
		cursor.flags |= HAMMER_CURSOR_ASOF;
		if (error)
			break;
		++snap->index;
	}
	snap->head.error = error;
	hammer_done_cursor(&cursor);
	hammer_unlock(&hmp->snapshot_lock);
	return(0);
}

/*
 * Delete snapshot transaction id(s) from the list of snapshots.
 */
static
int
hammer_ioc_del_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_snapshot *snap)
{
	hammer_mount_t hmp = ip->hmp;
	struct hammer_cursor cursor;
	int error;

	/*
	 * Validate structure
	 */
	if (snap->count > HAMMER_SNAPS_PER_IOCTL)
		return (EINVAL);
	if (snap->index > snap->count)
		return (EINVAL);

	hammer_lock_ex(&hmp->snapshot_lock);
again:
	/*
	 * Look for keys starting after the previous iteration, or at
	 * the beginning if snap->count is 0.
	 */
	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	cursor.key_beg.obj_id = HAMMER_OBJID_ROOT;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_SNAPSHOT;
	cursor.key_beg.localization = ip->obj_localization + HAMMER_LOCALIZE_INODE;
	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	while (snap->index < snap->count) {
		cursor.key_beg.key = (int64_t)snap->snaps[snap->index].tid;
		error = hammer_btree_lookup(&cursor);
		if (error)
			break;
		error = hammer_btree_extract(&cursor, HAMMER_CURSOR_GET_LEAF);
		if (error)
			break;
		error = hammer_delete_at_cursor(&cursor, HAMMER_DELETE_DESTROY,
						0, 0, 0, NULL);
		if (error == EDEADLK) {
			hammer_done_cursor(&cursor);
			goto again;
		}
		if (error)
			break;
		++snap->index;
	}
	snap->head.error = error;
	hammer_done_cursor(&cursor);
	hammer_unlock(&hmp->snapshot_lock);
	return(0);
}

/*
 * Retrieve as many snapshot ids as possible or until the array is
 * full, starting after the last transction id passed in.  If count
 * is 0 we retrieve starting at the beginning.
 *
 * NOTE: Because the b-tree key field is signed but transaction ids
 *       are unsigned the returned list will be signed-sorted instead
 *	 of unsigned sorted.  The Caller must still sort the aggregate
 *	 results.
 */
static
int
hammer_ioc_get_snapshot(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_snapshot *snap)
{
	struct hammer_cursor cursor;
	int error;

	/*
	 * Validate structure
	 */
	if (snap->index != 0)
		return (EINVAL);
	if (snap->count > HAMMER_SNAPS_PER_IOCTL)
		return (EINVAL);

	/*
	 * Look for keys starting after the previous iteration, or at
	 * the beginning if snap->count is 0.
	 */
	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	cursor.key_beg.obj_id = HAMMER_OBJID_ROOT;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_SNAPSHOT;
	cursor.key_beg.localization = ip->obj_localization + HAMMER_LOCALIZE_INODE;
	if (snap->count == 0)
		cursor.key_beg.key = HAMMER_MIN_KEY;
	else
		cursor.key_beg.key = (int64_t)snap->snaps[snap->count - 1].tid + 1;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_END_EXCLUSIVE | HAMMER_CURSOR_ASOF;

	snap->count = 0;

	error = hammer_btree_first(&cursor);
	while (error == 0 && snap->count < HAMMER_SNAPS_PER_IOCTL) {
		error = hammer_btree_extract(&cursor, HAMMER_CURSOR_GET_LEAF);
		if (error)
			break;
		if (cursor.leaf->base.rec_type == HAMMER_RECTYPE_SNAPSHOT) {
			error = hammer_btree_extract(
					     &cursor, HAMMER_CURSOR_GET_LEAF |
						      HAMMER_CURSOR_GET_DATA);
			snap->snaps[snap->count] = cursor.data->snap;

			/*
			 * The snap data tid should match the key but might
			 * not due to a bug in the HAMMER v3 conversion code.
			 *
			 * This error will work itself out over time but we
			 * have to force a match or the snapshot will not
			 * be deletable.
			 */
			if (cursor.data->snap.tid !=
			    (hammer_tid_t)cursor.leaf->base.key) {
				kprintf("HAMMER: lo=%08x snapshot key "
					"0x%016jx data mismatch 0x%016jx\n",
					cursor.key_beg.localization,
					(uintmax_t)cursor.data->snap.tid,
					cursor.leaf->base.key);
				kprintf("HAMMER: Probably left over from the "
					"original v3 conversion, hammer "
					"cleanup should get it eventually\n");
				snap->snaps[snap->count].tid =
					cursor.leaf->base.key;
			}
			++snap->count;
		}
		error = hammer_btree_iterate(&cursor);
	}

	if (error == ENOENT) {
		snap->head.flags |= HAMMER_IOC_SNAPSHOT_EOF;
		error = 0;
	}
	snap->head.error = error;
	hammer_done_cursor(&cursor);
	return(0);
}

/*
 * Retrieve the PFS hammer cleanup utility config record.  This is
 * different (newer than) the PFS config.
 */
static
int
hammer_ioc_get_config(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_config *config)
{
	struct hammer_cursor cursor;
	int error;

	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	cursor.key_beg.obj_id = HAMMER_OBJID_ROOT;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_CONFIG;
	cursor.key_beg.localization = ip->obj_localization + HAMMER_LOCALIZE_INODE;
	cursor.key_beg.key = 0;		/* config space page 0 */

	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		error = hammer_btree_extract(&cursor, HAMMER_CURSOR_GET_LEAF |
						      HAMMER_CURSOR_GET_DATA);
		if (error == 0)
			config->config = cursor.data->config;
	}
	/* error can be ENOENT */
	config->head.error = error;
	hammer_done_cursor(&cursor);
	return(0);
}

/*
 * Retrieve the PFS hammer cleanup utility config record.  This is
 * different (newer than) the PFS config.
 *
 * This is kinda a hack.
 */
static
int
hammer_ioc_set_config(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_config *config)
{
	struct hammer_btree_leaf_elm leaf;
	struct hammer_cursor cursor;
	hammer_mount_t hmp = ip->hmp;
	int error;

again:
	error = hammer_init_cursor(trans, &cursor, &ip->cache[0], NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}

	bzero(&leaf, sizeof(leaf));
	leaf.base.obj_id = HAMMER_OBJID_ROOT;
	leaf.base.rec_type = HAMMER_RECTYPE_CONFIG;
	leaf.base.create_tid = hammer_alloc_tid(hmp, 1);
	leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	leaf.base.localization = ip->obj_localization + HAMMER_LOCALIZE_INODE;
	leaf.base.key = 0;	/* page 0 */
	leaf.data_len = sizeof(struct hammer_config_data);

	cursor.key_beg = leaf.base;

	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_BACKEND | HAMMER_CURSOR_ASOF;

	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		error = hammer_btree_extract(&cursor, HAMMER_CURSOR_GET_LEAF |
						      HAMMER_CURSOR_GET_DATA);
		error = hammer_delete_at_cursor(&cursor, HAMMER_DELETE_DESTROY,
						0, 0, 0, NULL);
		if (error == EDEADLK) {
			hammer_done_cursor(&cursor);
			goto again;
		}
	}
	if (error == ENOENT)
		error = 0;
	if (error == 0) {
		/*
		 * NOTE: Must reload key_beg after an ASOF search because
		 *	 the create_tid may have been modified during the
		 *	 search.
		 */
		cursor.flags &= ~HAMMER_CURSOR_ASOF;
		cursor.key_beg = leaf.base;
		error = hammer_create_at_cursor(&cursor, &leaf,
						&config->config,
						HAMMER_CREATE_MODE_SYS);
		if (error == EDEADLK) {
			hammer_done_cursor(&cursor);
			goto again;
		}
	}
	config->head.error = error;
	hammer_done_cursor(&cursor);
	return(0);
}

static
int
hammer_ioc_pfs_iterate(hammer_transaction_t trans,
    struct hammer_ioc_pfs_iterate *pi)
{
	struct hammer_cursor cursor;
	hammer_inode_t ip;
	int error;

	ip = hammer_get_inode(trans, NULL, HAMMER_OBJID_ROOT, HAMMER_MAX_TID,
	    HAMMER_DEF_LOCALIZATION, 0, &error);

	error = hammer_init_cursor(trans, &cursor,
	    (ip ? &ip->cache[1] : NULL), ip);
	if (error)
		goto out;

	pi->head.flags &= ~HAMMER_PFSD_DELETED;

	cursor.key_beg.localization = HAMMER_DEF_LOCALIZATION +
	    HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = HAMMER_OBJID_ROOT;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_PFS;
	cursor.key_beg.obj_type = 0;
	cursor.key_end = cursor.key_beg;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.asof = HAMMER_MAX_TID;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	if (pi->pos < 0)	/* Sanity check */
		pi->pos = 0;

	pi->pos <<= 16;
	cursor.key_beg.key = pi->pos;
	error = hammer_ip_lookup(&cursor);

	if (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			goto out;
		if (cursor.data->pfsd.mirror_flags & HAMMER_PFSD_DELETED)
			pi->head.flags |= HAMMER_PFSD_DELETED;
		else
			copyout(cursor.data, pi->ondisk, cursor.leaf->data_len);
		pi->pos = (u_int32_t)(cursor.leaf->base.key >> 16);
	}

out:
	hammer_done_cursor(&cursor);
	if (ip)
		hammer_rel_inode(ip, 0);

	return (error);
}

static
int
hammer_ioc_get_data(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_data *data)
{
	struct hammer_cursor cursor;
	int bytes;
	int error;

	/* XXX cached inode ? */
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error)
		goto failed;

	cursor.key_beg = data->elm;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		error = hammer_btree_extract(&cursor, HAMMER_CURSOR_GET_LEAF |
						      HAMMER_CURSOR_GET_DATA);
		if (error == 0) {
			data->leaf = *cursor.leaf;
			bytes = cursor.leaf->data_len;
			if (bytes > data->size)
				bytes = data->size;
			error = copyout(cursor.data, data->ubuf, bytes);
		}
	}

failed:
	hammer_done_cursor(&cursor);
	return (error);
}
