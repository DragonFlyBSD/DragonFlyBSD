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

int
hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
	     struct ucred *cred)
{
	struct hammer_transaction trans;
	int error;

	error = priv_check_cred(cred, PRIV_ROOT, PRISON_ROOT);

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
	case HAMMERIOC_SET_VERSION:
		if (error == 0) {
			error = hammer_ioc_set_version(&trans, ip, 
					    (struct hammer_ioc_version *)data);
		}
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
			 "2.0 - First HAMMER release");
		break;
	case 2:
		ksnprintf(ver->description, sizeof(ver->description),
			 "2.2 - New directory hash");
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
	struct hammer_cursor cursor;
	hammer_volume_t volume;
	int error;

	if (ver->cur_version < trans->hmp->version)
		return(EINVAL);
	if (ver->cur_version == trans->hmp->version)
		return(0);
	if (ver->cur_version > HAMMER_VOL_VERSION_MAX)
		return(EINVAL);
	if (trans->hmp->ronly)
		return(EROFS);

	/*
	 * Update the root volume header and the version cached in
	 * the hammer_mount structure.
	 */
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error)
		goto failed;
	hammer_sync_lock_sh(trans);

	volume = hammer_get_root_volume(cursor.trans->hmp, &error);
	KKASSERT(error == 0);
	hammer_modify_volume_field(cursor.trans, volume, vol_version);
	volume->ondisk->vol_version = ver->cur_version;
	cursor.trans->hmp->version = ver->cur_version;
	hammer_modify_volume_done(volume);
	hammer_rel_volume(volume, 0);

	hammer_sync_unlock(trans);
failed:
	ver->head.error = error;
	hammer_done_cursor(&cursor);
	return(0);
}



