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
 * $DragonFly: src/sys/vfs/hammer/hammer_ioctl.c,v 1.1 2008/02/04 08:33:17 dillon Exp $
 */

#include "hammer.h"

static int hammer_ioc_prune(hammer_inode_t ip,
				struct hammer_ioc_prune *prune);
static int hammer_ioc_gethistory(hammer_inode_t ip,
				struct hammer_ioc_history *hist);

int
hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
	     struct ucred *cred)
{
	int error;

	error = suser_cred(cred, PRISON_ROOT);

	switch(com) {
	case HAMMERIOC_PRUNE:
		if (error == 0) {
			error = hammer_ioc_prune(ip,
					(struct hammer_ioc_prune *)data);
		}
		break;
	case HAMMERIOC_GETHISTORY:
		error = hammer_ioc_gethistory(ip,
					(struct hammer_ioc_history *)data);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

/*
 * Iterate through the specified range of object ids and remove any
 * deleted records that fall entirely within a prune modulo.
 */
static int check_prune(struct hammer_ioc_prune *prune, hammer_btree_elm_t elm);

static int
hammer_ioc_prune(hammer_inode_t ip, struct hammer_ioc_prune *prune)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;

	if (prune->nelms < 0 || prune->nelms > HAMMER_MAX_PRUNE_ELMS) {
		return(EINVAL);
	}
	if (prune->beg_obj_id >= prune->end_obj_id) {
		return(EINVAL);
	}

retry:
	error = hammer_init_cursor_hmp(&cursor, NULL, ip->hmp);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}
	cursor.key_beg.obj_id = prune->cur_obj_id;
	cursor.key_beg.key = prune->cur_key;
	cursor.key_beg.create_tid = 1;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_beg.obj_type = 0;

	cursor.key_end.obj_id = prune->end_obj_id;
	cursor.key_end.key = HAMMER_MIN_KEY;
	cursor.key_end.create_tid = 1;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_EXCLUSIVE;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		elm = &cursor.node->ondisk->elms[cursor.index];
		if (check_prune(prune, elm) == 0) {
			if (hammer_debug_general & 0x0200) {
				kprintf("check %016llx %016llx: DELETE\n",
					elm->base.obj_id, elm->base.key);
			}
			/*
			 * NOTE: This can return EDEADLK
			 */
			prune->cur_obj_id = elm->base.obj_id;
			prune->cur_key = elm->base.key;
			if (elm->base.rec_type == HAMMER_RECTYPE_DIRENTRY)
				++prune->stat_dirrecords;
			error = hammer_delete_at_cursor(&cursor,
							&prune->stat_bytes);
			if (error == 0)
				++prune->stat_rawrecords;
			else
				--prune->stat_dirrecords;
		} else {
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			if (hammer_debug_general & 0x0100) {
				kprintf("check %016llx %016llx: SKIP\n",
					elm->base.obj_id, elm->base.key);
			}
		}
		if (error == 0)
			error = hammer_btree_iterate(&cursor);
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	return(error);
}

/*
 * Check pruning list.  The list must be sorted in descending order.
 */
static int
check_prune(struct hammer_ioc_prune *prune, hammer_btree_elm_t elm)
{
	struct hammer_ioc_prune_elm *scan;
	int i;

	if (elm->base.delete_tid == 0)
		return(-1);
	for (i = 0; i < prune->nelms; ++i) {
		scan = &prune->elms[i];
		if (elm->base.create_tid >= scan->end_tid ||
		    elm->base.delete_tid >= scan->end_tid) {
			break;
		}
		if (elm->base.create_tid < scan->beg_tid)
			continue;
		KKASSERT(elm->base.delete_tid >= scan->beg_tid);
		if (elm->base.create_tid / scan->mod_tid ==
		    elm->base.delete_tid / scan->mod_tid) {
			return(0);
		}
	}
	return(-1);
}

/*
 * Iterate through an object's inode or an object's records and record
 * modification TIDs.
 */
static void add_history(hammer_inode_t ip, struct hammer_ioc_history *hist,
			hammer_btree_elm_t elm);

static
int
hammer_ioc_gethistory(hammer_inode_t ip, struct hammer_ioc_history *hist)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;

	/*
	 * Validate the structure and initialize for return.
	 */
	if (hist->beg_tid > hist->end_tid)
		return(EINVAL);
	if (hist->flags & HAMMER_IOC_HISTORY_ATKEY) {
		if (hist->key > hist->nxt_key)
			return(EINVAL);
	}

	hist->obj_id = ip->obj_id;
	hist->count = 0;
	hist->nxt_tid = hist->end_tid;
	hist->flags &= ~HAMMER_IOC_HISTORY_NEXT_TID;
	hist->flags &= ~HAMMER_IOC_HISTORY_NEXT_KEY;
	hist->flags &= ~HAMMER_IOC_HISTORY_EOF;
	hist->flags &= ~HAMMER_IOC_HISTORY_UNSYNCED;
	if ((ip->flags & HAMMER_INODE_MODMASK) & ~HAMMER_INODE_ITIMES)
		hist->flags |= HAMMER_IOC_HISTORY_UNSYNCED;

	/*
	 * Setup the cursor.  We can't handle undeletable records
	 * (create_tid of 0) at the moment.  A create_tid of 0 has
	 * a special meaning and cannot be specified in the cursor.
	 */
	error = hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);
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

	if (hist->flags & HAMMER_IOC_HISTORY_ATKEY) {
		/*
		 * key-range within the file.  For a regular file the
		 * on-disk key represents BASE+LEN, not BASE, so the
		 * first possible record containing the offset 'key'
		 * has an on-disk key of (key + 1).
		 */
		cursor.key_beg.key = hist->key;
		cursor.key_end.key = HAMMER_MAX_KEY;

		switch(ip->ino_rec.base.base.obj_type) {
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
	}

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		elm = &cursor.node->ondisk->elms[cursor.index];

		add_history(ip, hist, elm);
		if (hist->flags & (HAMMER_IOC_HISTORY_NEXT_TID |
				  HAMMER_IOC_HISTORY_NEXT_KEY |
				  HAMMER_IOC_HISTORY_EOF)) {
			break;
		}
		error = hammer_btree_iterate(&cursor);
	}
	if (error == ENOENT) {
		hist->flags |= HAMMER_IOC_HISTORY_EOF;
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
	if (elm->base.btype != HAMMER_BTREE_TYPE_RECORD)
		return;
	if ((hist->flags & HAMMER_IOC_HISTORY_ATKEY) &&
	    ip->ino_rec.base.base.obj_type == HAMMER_OBJTYPE_REGFILE) {
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
			hist->flags |= HAMMER_IOC_HISTORY_NEXT_KEY;
		}

		/*
		 * Data-range of record does not cover the key.
		 */
		if (elm->leaf.base.key - elm->leaf.data_len > hist->key)
			return;

	} else if (hist->flags & HAMMER_IOC_HISTORY_ATKEY) {
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
			hist->flags |= HAMMER_IOC_HISTORY_NEXT_KEY;
	}

	/*
	 * Add create_tid if it is in-bounds.
	 */
	if ((hist->count == 0 ||
	     elm->leaf.base.create_tid != hist->tid_ary[hist->count - 1]) &&
	    elm->leaf.base.create_tid >= hist->beg_tid &&
	    elm->leaf.base.create_tid < hist->end_tid) {
		if (hist->count == HAMMER_MAX_HISTORY_ELMS) {
			hist->nxt_tid = elm->leaf.base.create_tid;
			hist->flags |= HAMMER_IOC_HISTORY_NEXT_TID;
			return;
		}
		hist->tid_ary[hist->count++] = elm->leaf.base.create_tid;
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
	if (elm->leaf.base.delete_tid &&
	    elm->leaf.base.delete_tid >= hist->beg_tid &&
	    elm->leaf.base.delete_tid < hist->end_tid) {
		if (hist->count == HAMMER_MAX_HISTORY_ELMS) {
			hist->nxt_tid = elm->leaf.base.delete_tid;
			hist->flags |= HAMMER_IOC_HISTORY_NEXT_TID;
			return;
		}
		hist->tid_ary[hist->count++] = elm->leaf.base.delete_tid;
	}
}

