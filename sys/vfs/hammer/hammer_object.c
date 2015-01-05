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
 */

#include "hammer.h"

static int hammer_mem_lookup(hammer_cursor_t cursor);
static void hammer_mem_first(hammer_cursor_t cursor);
static int hammer_frontend_trunc_callback(hammer_record_t record,
				void *data __unused);
static int hammer_bulk_scan_callback(hammer_record_t record, void *data);
static int hammer_record_needs_overwrite_delete(hammer_record_t record);
static int hammer_delete_general(hammer_cursor_t cursor, hammer_inode_t ip,
				hammer_btree_leaf_elm_t leaf);
static int hammer_cursor_localize_data(hammer_data_ondisk_t data,
				hammer_btree_leaf_elm_t leaf);

struct rec_trunc_info {
	u_int16_t	rec_type;
	int64_t		trunc_off;
};

struct hammer_bulk_info {
	hammer_record_t record;
	hammer_record_t conflict;
};

/*
 * Red-black tree support.  Comparison code for insertion.
 */
static int
hammer_rec_rb_compare(hammer_record_t rec1, hammer_record_t rec2)
{
	if (rec1->leaf.base.rec_type < rec2->leaf.base.rec_type)
		return(-1);
	if (rec1->leaf.base.rec_type > rec2->leaf.base.rec_type)
		return(1);

	if (rec1->leaf.base.key < rec2->leaf.base.key)
		return(-1);
	if (rec1->leaf.base.key > rec2->leaf.base.key)
		return(1);

	/*
	 * For search & insertion purposes records deleted by the
	 * frontend or deleted/committed by the backend are silently
	 * ignored.  Otherwise pipelined insertions will get messed
	 * up.
	 *
	 * rec1 is greater then rec2 if rec1 is marked deleted.
	 * rec1 is less then rec2 if rec2 is marked deleted.
	 *
	 * Multiple deleted records may be present, do not return 0
	 * if both are marked deleted.
	 */
	if (rec1->flags & (HAMMER_RECF_DELETED_FE | HAMMER_RECF_DELETED_BE |
			   HAMMER_RECF_COMMITTED)) {
		return(1);
	}
	if (rec2->flags & (HAMMER_RECF_DELETED_FE | HAMMER_RECF_DELETED_BE |
			   HAMMER_RECF_COMMITTED)) {
		return(-1);
	}

        return(0);
}

/*
 * Basic record comparison code similar to hammer_btree_cmp().
 *
 * obj_id is not compared and may not yet be assigned in the record.
 */
static int
hammer_rec_cmp(hammer_base_elm_t elm, hammer_record_t rec)
{
	if (elm->rec_type < rec->leaf.base.rec_type)
		return(-3);
	if (elm->rec_type > rec->leaf.base.rec_type)
		return(3);

        if (elm->key < rec->leaf.base.key)
                return(-2);
        if (elm->key > rec->leaf.base.key)
                return(2);

	/*
	 * Never match against an item deleted by the frontend
	 * or backend, or committed by the backend.
	 *
	 * elm is less then rec if rec is marked deleted.
	 */
	if (rec->flags & (HAMMER_RECF_DELETED_FE | HAMMER_RECF_DELETED_BE |
			  HAMMER_RECF_COMMITTED)) {
		return(-1);
	}
        return(0);
}

/*
 * Ranged scan to locate overlapping record(s).  This is used by
 * hammer_ip_get_bulk() to locate an overlapping record.  We have
 * to use a ranged scan because the keys for data records with the
 * same file base offset can be different due to differing data_len's.
 *
 * NOTE: The base file offset of a data record is (key - data_len), not (key).
 */
static int
hammer_rec_overlap_cmp(hammer_record_t rec, void *data)
{
	struct hammer_bulk_info *info = data;
	hammer_btree_leaf_elm_t leaf = &info->record->leaf;

	if (rec->leaf.base.rec_type < leaf->base.rec_type)
		return(-3);
	if (rec->leaf.base.rec_type > leaf->base.rec_type)
		return(3);

	/*
	 * Overlap compare
	 */
	if (leaf->base.rec_type == HAMMER_RECTYPE_DATA) {
		/* rec_beg >= leaf_end */
		if (rec->leaf.base.key - rec->leaf.data_len >= leaf->base.key)
			return(2);
		/* rec_end <= leaf_beg */
		if (rec->leaf.base.key <= leaf->base.key - leaf->data_len)
			return(-2);
	} else {
		if (rec->leaf.base.key < leaf->base.key)
			return(-2);
		if (rec->leaf.base.key > leaf->base.key)
			return(2);
	}

	/*
	 * We have to return 0 at this point, even if DELETED_FE is set,
	 * because returning anything else will cause the scan to ignore
	 * one of the branches when we really want it to check both.
	 */
        return(0);
}

/*
 * RB_SCAN comparison code for hammer_mem_first().  The argument order
 * is reversed so the comparison result has to be negated.  key_beg and
 * key_end are both range-inclusive.
 *
 * Localized deletions are not cached in-memory.
 */
static
int
hammer_rec_scan_cmp(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;
	int r;

	r = hammer_rec_cmp(&cursor->key_beg, rec);
	if (r > 1)
		return(-1);
	r = hammer_rec_cmp(&cursor->key_end, rec);
	if (r < -1)
		return(1);
	return(0);
}

/*
 * This compare function is used when simply looking up key_beg.
 */
static
int
hammer_rec_find_cmp(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;
	int r;

	r = hammer_rec_cmp(&cursor->key_beg, rec);
	if (r > 1)
		return(-1);
	if (r < -1)
		return(1);
	return(0);
}

/*
 * Locate blocks within the truncation range.  Partial blocks do not count.
 */
static
int
hammer_rec_trunc_cmp(hammer_record_t rec, void *data)
{
	struct rec_trunc_info *info = data;

	if (rec->leaf.base.rec_type < info->rec_type)
		return(-1);
	if (rec->leaf.base.rec_type > info->rec_type)
		return(1);

	switch(rec->leaf.base.rec_type) {
	case HAMMER_RECTYPE_DB:
		/*
		 * DB record key is not beyond the truncation point, retain.
		 */
		if (rec->leaf.base.key < info->trunc_off)
			return(-1);
		break;
	case HAMMER_RECTYPE_DATA:
		/*
		 * DATA record offset start is not beyond the truncation point,
		 * retain.
		 */
		if (rec->leaf.base.key - rec->leaf.data_len < info->trunc_off)
			return(-1);
		break;
	default:
		panic("hammer_rec_trunc_cmp: unexpected record type");
	}

	/*
	 * The record start is >= the truncation point, return match,
	 * the record should be destroyed.
	 */
	return(0);
}

RB_GENERATE(hammer_rec_rb_tree, hammer_record, rb_node, hammer_rec_rb_compare);

/*
 * Allocate a record for the caller to finish filling in.  The record is
 * returned referenced.
 */
hammer_record_t
hammer_alloc_mem_record(hammer_inode_t ip, int data_len)
{
	hammer_record_t record;
	hammer_mount_t hmp;

	hmp = ip->hmp;
	++hammer_count_records;
	record = kmalloc(sizeof(*record), hmp->m_misc,
			 M_WAITOK | M_ZERO | M_USE_RESERVE);
	record->flush_state = HAMMER_FST_IDLE;
	record->ip = ip;
	record->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	record->leaf.data_len = data_len;
	hammer_ref(&record->lock);

	if (data_len) {
		record->data = kmalloc(data_len, hmp->m_misc, M_WAITOK | M_ZERO);
		record->flags |= HAMMER_RECF_ALLOCDATA;
		++hammer_count_record_datas;
	}

	return (record);
}

void
hammer_wait_mem_record_ident(hammer_record_t record, const char *ident)
{
	while (record->flush_state == HAMMER_FST_FLUSH) {
		record->flags |= HAMMER_RECF_WANTED;
		tsleep(record, 0, ident, 0);
	}
}

/*
 * Called from the backend, hammer_inode.c, after a record has been
 * flushed to disk.  The record has been exclusively locked by the
 * caller and interlocked with BE.
 *
 * We clean up the state, unlock, and release the record (the record
 * was referenced by the fact that it was in the HAMMER_FST_FLUSH state).
 */
void
hammer_flush_record_done(hammer_record_t record, int error)
{
	hammer_inode_t target_ip;

	KKASSERT(record->flush_state == HAMMER_FST_FLUSH);
	KKASSERT(record->flags & HAMMER_RECF_INTERLOCK_BE);

	/*
	 * If an error occured, the backend was unable to sync the
	 * record to its media.  Leave the record intact.
	 */
	if (error) {
		hammer_critical_error(record->ip->hmp, record->ip, error,
				      "while flushing record");
	}

	--record->flush_group->refs;
	record->flush_group = NULL;

	/*
	 * Adjust the flush state and dependancy based on success or
	 * failure.
	 */
	if (record->flags & (HAMMER_RECF_DELETED_BE | HAMMER_RECF_COMMITTED)) {
		if ((target_ip = record->target_ip) != NULL) {
			TAILQ_REMOVE(&target_ip->target_list, record,
				     target_entry);
			record->target_ip = NULL;
			hammer_test_inode(target_ip);
		}
		record->flush_state = HAMMER_FST_IDLE;
	} else {
		if (record->target_ip) {
			record->flush_state = HAMMER_FST_SETUP;
			hammer_test_inode(record->ip);
			hammer_test_inode(record->target_ip);
		} else {
			record->flush_state = HAMMER_FST_IDLE;
		}
	}
	record->flags &= ~HAMMER_RECF_INTERLOCK_BE;

	/*
	 * Cleanup
	 */
	if (record->flags & HAMMER_RECF_WANTED) {
		record->flags &= ~HAMMER_RECF_WANTED;
		wakeup(record);
	}
	hammer_rel_mem_record(record);
}

/*
 * Release a memory record.  Records marked for deletion are immediately
 * removed from the RB-Tree but otherwise left intact until the last ref
 * goes away.
 */
void
hammer_rel_mem_record(struct hammer_record *record)
{
	hammer_mount_t hmp;
	hammer_reserve_t resv;
	hammer_inode_t ip;
	hammer_inode_t target_ip;
	int diddrop;

	hammer_rel(&record->lock);

	if (hammer_norefs(&record->lock)) {
		/*
		 * Upon release of the last reference wakeup any waiters.
		 * The record structure may get destroyed so callers will
		 * loop up and do a relookup.
		 *
		 * WARNING!  Record must be removed from RB-TREE before we
		 * might possibly block.  hammer_test_inode() can block!
		 */
		ip = record->ip;
		hmp = ip->hmp;

		/*
		 * Upon release of the last reference a record marked deleted
		 * by the front or backend, or committed by the backend,
		 * is destroyed.
		 */
		if (record->flags & (HAMMER_RECF_DELETED_FE |
				     HAMMER_RECF_DELETED_BE |
				     HAMMER_RECF_COMMITTED)) {
			KKASSERT(hammer_isactive(&ip->lock) > 0);
			KKASSERT(record->flush_state != HAMMER_FST_FLUSH);

			/*
			 * target_ip may have zero refs, we have to ref it
			 * to prevent it from being ripped out from under
			 * us.
			 */
			if ((target_ip = record->target_ip) != NULL) {
				TAILQ_REMOVE(&target_ip->target_list,
					     record, target_entry);
				record->target_ip = NULL;
				hammer_ref(&target_ip->lock);
			}

			/*
			 * Remove the record from the RB-Tree
			 */
			if (record->flags & HAMMER_RECF_ONRBTREE) {
				RB_REMOVE(hammer_rec_rb_tree,
					  &record->ip->rec_tree,
					  record);
				record->flags &= ~HAMMER_RECF_ONRBTREE;
				KKASSERT(ip->rsv_recs > 0);
				if (RB_EMPTY(&record->ip->rec_tree)) {
					record->ip->flags &=
							~HAMMER_INODE_XDIRTY;
					record->ip->sync_flags &=
							~HAMMER_INODE_XDIRTY;
				}
				diddrop = 1;
			} else {
				diddrop = 0;
			}

			/*
			 * We must wait for any direct-IO to complete before
			 * we can destroy the record because the bio may
			 * have a reference to it.
			 */
			if (record->gflags &
			   (HAMMER_RECG_DIRECT_IO | HAMMER_RECG_DIRECT_INVAL)) {
				hammer_io_direct_wait(record);
			}

			/*
			 * Account for the completion after the direct IO
			 * has completed.
			 */
			if (diddrop) {
				--hmp->rsv_recs;
				--ip->rsv_recs;
				hmp->rsv_databytes -= record->leaf.data_len;

				if (RB_EMPTY(&record->ip->rec_tree))
					hammer_test_inode(record->ip);
				if ((ip->flags & HAMMER_INODE_RECSW) &&
				    ip->rsv_recs <= hammer_limit_inode_recs/2) {
					ip->flags &= ~HAMMER_INODE_RECSW;
					wakeup(&ip->rsv_recs);
				}
			}

			/*
			 * Do this test after removing record from the RB-Tree.
			 */
			if (target_ip) {
				hammer_test_inode(target_ip);
				hammer_rel_inode(target_ip, 0);
			}

			if (record->flags & HAMMER_RECF_ALLOCDATA) {
				--hammer_count_record_datas;
				kfree(record->data, hmp->m_misc);
				record->flags &= ~HAMMER_RECF_ALLOCDATA;
			}

			/*
			 * Release the reservation.
			 *
			 * If the record was not committed we can theoretically
			 * undo the reservation.  However, doing so might
			 * create weird edge cases with the ordering of
			 * direct writes because the related buffer cache
			 * elements are per-vnode.  So we don't try.
			 */
			if ((resv = record->resv) != NULL) {
				/* XXX undo leaf.data_offset,leaf.data_len */
				hammer_blockmap_reserve_complete(hmp, resv);
				record->resv = NULL;
			}
			record->data = NULL;
			--hammer_count_records;
			kfree(record, hmp->m_misc);
		}
	}
}

/*
 * Record visibility depends on whether the record is being accessed by
 * the backend or the frontend.  Backend tests ignore the frontend delete
 * flag.  Frontend tests do NOT ignore the backend delete/commit flags and
 * must also check for commit races.
 *
 * Return non-zero if the record is visible, zero if it isn't or if it is
 * deleted.  Returns 0 if the record has been comitted (unless the special
 * delete-visibility flag is set).  A committed record must be located
 * via the media B-Tree.  Returns non-zero if the record is good.
 *
 * If HAMMER_CURSOR_DELETE_VISIBILITY is set we allow deleted memory
 * records to be returned.  This is so pending deletions are detected
 * when using an iterator to locate an unused hash key, or when we need
 * to locate historical records on-disk to destroy.
 */
static __inline
int
hammer_ip_iterate_mem_good(hammer_cursor_t cursor, hammer_record_t record)
{
	if (cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY)
		return(1);
	if (cursor->flags & HAMMER_CURSOR_BACKEND) {
		if (record->flags & (HAMMER_RECF_DELETED_BE |
				     HAMMER_RECF_COMMITTED)) {
			return(0);
		}
	} else {
		if (record->flags & (HAMMER_RECF_DELETED_FE |
				     HAMMER_RECF_DELETED_BE |
				     HAMMER_RECF_COMMITTED)) {
			return(0);
		}
	}
	return(1);
}

/*
 * This callback is used as part of the RB_SCAN function for in-memory
 * records.  We terminate it (return -1) as soon as we get a match.
 *
 * This routine is used by frontend code.
 *
 * The primary compare code does not account for ASOF lookups.  This
 * code handles that case as well as a few others.
 */
static
int
hammer_rec_scan_callback(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;

	/*
	 * We terminate on success, so this should be NULL on entry.
	 */
	KKASSERT(cursor->iprec == NULL);

	/*
	 * Skip if the record was marked deleted or committed.
	 */
	if (hammer_ip_iterate_mem_good(cursor, rec) == 0)
		return(0);

	/*
	 * Skip if not visible due to our as-of TID
	 */
        if (cursor->flags & HAMMER_CURSOR_ASOF) {
                if (cursor->asof < rec->leaf.base.create_tid)
                        return(0);
                if (rec->leaf.base.delete_tid &&
		    cursor->asof >= rec->leaf.base.delete_tid) {
                        return(0);
		}
        }

	/*
	 * ref the record.  The record is protected from backend B-Tree
	 * interactions by virtue of the cursor's IP lock.
	 */
	hammer_ref(&rec->lock);

	/*
	 * The record may have been deleted or committed while we
	 * were blocked.  XXX remove?
	 */
	if (hammer_ip_iterate_mem_good(cursor, rec) == 0) {
		hammer_rel_mem_record(rec);
		return(0);
	}

	/*
	 * Set the matching record and stop the scan.
	 */
	cursor->iprec = rec;
	return(-1);
}


/*
 * Lookup an in-memory record given the key specified in the cursor.  Works
 * just like hammer_btree_lookup() but operates on an inode's in-memory
 * record list.
 *
 * The lookup must fail if the record is marked for deferred deletion.
 *
 * The API for mem/btree_lookup() does not mess with the ATE/EOF bits.
 */
static
int
hammer_mem_lookup(hammer_cursor_t cursor)
{
	KKASSERT(cursor->ip);
	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
	hammer_rec_rb_tree_RB_SCAN(&cursor->ip->rec_tree, hammer_rec_find_cmp,
				   hammer_rec_scan_callback, cursor);

	return (cursor->iprec ? 0 : ENOENT);
}

/*
 * hammer_mem_first() - locate the first in-memory record matching the
 * cursor within the bounds of the key range.
 *
 * WARNING!  API is slightly different from btree_first().  hammer_mem_first()
 * will set ATEMEM the same as MEMEOF, and does not return any error.
 */
static
void
hammer_mem_first(hammer_cursor_t cursor)
{
	hammer_inode_t ip;

	ip = cursor->ip;
	KKASSERT(ip != NULL);

	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
	hammer_rec_rb_tree_RB_SCAN(&ip->rec_tree, hammer_rec_scan_cmp,
				   hammer_rec_scan_callback, cursor);

	if (cursor->iprec)
		cursor->flags &= ~(HAMMER_CURSOR_MEMEOF | HAMMER_CURSOR_ATEMEM);
	else
		cursor->flags |= HAMMER_CURSOR_MEMEOF | HAMMER_CURSOR_ATEMEM;
}

/************************************************************************
 *		     HAMMER IN-MEMORY RECORD FUNCTIONS			*
 ************************************************************************
 *
 * These functions manipulate in-memory records.  Such records typically
 * exist prior to being committed to disk or indexed via the on-disk B-Tree.
 */

/*
 * Add a directory entry (dip,ncp) which references inode (ip).
 *
 * Note that the low 32 bits of the namekey are set temporarily to create
 * a unique in-memory record, and may be modified a second time when the
 * record is synchronized to disk.  In particular, the low 32 bits cannot be
 * all 0's when synching to disk, which is not handled here.
 *
 * NOTE: bytes does not include any terminating \0 on name, and name might
 * not be terminated.
 */
int
hammer_ip_add_directory(struct hammer_transaction *trans,
		     struct hammer_inode *dip, const char *name, int bytes,
		     struct hammer_inode *ip)
{
	struct hammer_cursor cursor;
	hammer_record_t record;
	int error;
	u_int32_t max_iterations;

	KKASSERT(dip->ino_data.obj_type == HAMMER_OBJTYPE_DIRECTORY);

	record = hammer_alloc_mem_record(dip, HAMMER_ENTRY_SIZE(bytes));

	record->type = HAMMER_MEM_RECORD_ADD;
	record->leaf.base.localization = dip->obj_localization +
					 hammer_dir_localization(dip);
	record->leaf.base.obj_id = dip->obj_id;
	record->leaf.base.key = hammer_directory_namekey(dip, name, bytes,
							 &max_iterations);
	record->leaf.base.rec_type = HAMMER_RECTYPE_DIRENTRY;
	record->leaf.base.obj_type = ip->ino_leaf.base.obj_type;
	record->data->entry.obj_id = ip->obj_id;
	record->data->entry.localization = ip->obj_localization;
	bcopy(name, record->data->entry.name, bytes);

	++ip->ino_data.nlinks;
	ip->ino_data.ctime = trans->time;
	hammer_modify_inode(trans, ip, HAMMER_INODE_DDIRTY);

	/*
	 * Find an unused namekey.  Both the in-memory record tree and
	 * the B-Tree are checked.  We do not want historically deleted
	 * names to create a collision as our iteration space may be limited,
	 * and since create_tid wouldn't match anyway an ASOF search
	 * must be used to locate collisions.
	 *
	 * delete-visibility is set so pending deletions do not give us
	 * a false-negative on our ability to use an iterator.
	 *
	 * The iterator must not rollover the key.  Directory keys only
	 * use the positive key space.
	 */
	hammer_init_cursor(trans, &cursor, &dip->cache[1], dip);
	cursor.key_beg = record->leaf.base;
	cursor.flags |= HAMMER_CURSOR_ASOF;
	cursor.flags |= HAMMER_CURSOR_DELETE_VISIBILITY;
	cursor.asof = ip->obj_asof;

	while (hammer_ip_lookup(&cursor) == 0) {
		++record->leaf.base.key;
		KKASSERT(record->leaf.base.key > 0);
		cursor.key_beg.key = record->leaf.base.key;
		if (--max_iterations == 0) {
			hammer_rel_mem_record(record);
			error = ENOSPC;
			goto failed;
		}
	}

	/*
	 * The target inode and the directory entry are bound together.
	 */
	record->target_ip = ip;
	record->flush_state = HAMMER_FST_SETUP;
	TAILQ_INSERT_TAIL(&ip->target_list, record, target_entry);

	/*
	 * The inode now has a dependancy and must be taken out of the idle
	 * state.  An inode not in an idle state is given an extra reference.
	 *
	 * When transitioning to a SETUP state flag for an automatic reflush
	 * when the dependancies are disposed of if someone is waiting on
	 * the inode.
	 */
	if (ip->flush_state == HAMMER_FST_IDLE) {
		hammer_ref(&ip->lock);
		ip->flush_state = HAMMER_FST_SETUP;
		if (ip->flags & HAMMER_INODE_FLUSHW)
			ip->flags |= HAMMER_INODE_REFLUSH;
	}
	error = hammer_mem_add(record);
	if (error == 0) {
		dip->ino_data.mtime = trans->time;
		hammer_modify_inode(trans, dip, HAMMER_INODE_MTIME);
	}
failed:
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Delete the directory entry and update the inode link count.  The
 * cursor must be seeked to the directory entry record being deleted.
 *
 * The related inode should be share-locked by the caller.  The caller is
 * on the frontend.  It could also be NULL indicating that the directory
 * entry being removed has no related inode.
 *
 * This function can return EDEADLK requiring the caller to terminate
 * the cursor, any locks, wait on the returned record, and retry.
 */
int
hammer_ip_del_directory(struct hammer_transaction *trans,
		     hammer_cursor_t cursor, struct hammer_inode *dip,
		     struct hammer_inode *ip)
{
	hammer_record_t record;
	int error;

	if (hammer_cursor_inmem(cursor)) {
		/*
		 * In-memory (unsynchronized) records can simply be freed.
		 *
		 * Even though the HAMMER_RECF_DELETED_FE flag is ignored
		 * by the backend, we must still avoid races against the
		 * backend potentially syncing the record to the media.
		 *
		 * We cannot call hammer_ip_delete_record(), that routine may
		 * only be called from the backend.
		 */
		record = cursor->iprec;
		if (record->flags & (HAMMER_RECF_INTERLOCK_BE |
				     HAMMER_RECF_DELETED_BE |
				     HAMMER_RECF_COMMITTED)) {
			KKASSERT(cursor->deadlk_rec == NULL);
			hammer_ref(&record->lock);
			cursor->deadlk_rec = record;
			error = EDEADLK;
		} else {
			KKASSERT(record->type == HAMMER_MEM_RECORD_ADD);
			record->flags |= HAMMER_RECF_DELETED_FE;
			error = 0;
		}
	} else {
		/*
		 * If the record is on-disk we have to queue the deletion by
		 * the record's key.  This also causes lookups to skip the
		 * record (lookups for the purposes of finding an unused
		 * directory key do not skip the record).
		 */
		KKASSERT(dip->flags &
			 (HAMMER_INODE_ONDISK | HAMMER_INODE_DONDISK));
		record = hammer_alloc_mem_record(dip, 0);
		record->type = HAMMER_MEM_RECORD_DEL;
		record->leaf.base = cursor->leaf->base;
		KKASSERT(dip->obj_id == record->leaf.base.obj_id);

		/*
		 * ip may be NULL, indicating the deletion of a directory
		 * entry which has no related inode.
		 */
		record->target_ip = ip;
		if (ip) {
			record->flush_state = HAMMER_FST_SETUP;
			TAILQ_INSERT_TAIL(&ip->target_list, record,
					  target_entry);
		} else {
			record->flush_state = HAMMER_FST_IDLE;
		}

		/*
		 * The inode now has a dependancy and must be taken out of
		 * the idle state.  An inode not in an idle state is given
		 * an extra reference.
		 *
		 * When transitioning to a SETUP state flag for an automatic
		 * reflush when the dependancies are disposed of if someone
		 * is waiting on the inode.
		 */
		if (ip && ip->flush_state == HAMMER_FST_IDLE) {
			hammer_ref(&ip->lock);
			ip->flush_state = HAMMER_FST_SETUP;
			if (ip->flags & HAMMER_INODE_FLUSHW)
				ip->flags |= HAMMER_INODE_REFLUSH;
		}

		error = hammer_mem_add(record);
	}

	/*
	 * One less link.  The file may still be open in the OS even after
	 * all links have gone away.
	 *
	 * We have to terminate the cursor before syncing the inode to
	 * avoid deadlocking against ourselves.  XXX this may no longer
	 * be true.
	 *
	 * If nlinks drops to zero and the vnode is inactive (or there is
	 * no vnode), call hammer_inode_unloadable_check() to zonk the
	 * inode.  If we don't do this here the inode will not be destroyed
	 * on-media until we unmount.
	 */
	if (error == 0) {
		if (ip) {
			--ip->ino_data.nlinks;	/* do before we might block */
			ip->ino_data.ctime = trans->time;
		}
		dip->ino_data.mtime = trans->time;
		hammer_modify_inode(trans, dip, HAMMER_INODE_MTIME);
		if (ip) {
			hammer_modify_inode(trans, ip, HAMMER_INODE_DDIRTY);
			if (ip->ino_data.nlinks == 0 &&
			    (ip->vp == NULL || (ip->vp->v_flag & VINACTIVE))) {
				hammer_done_cursor(cursor);
				hammer_inode_unloadable_check(ip, 1);
				hammer_flush_inode(ip, 0);
			}
		}

	}
	return(error);
}

/*
 * Add a record to an inode.
 *
 * The caller must allocate the record with hammer_alloc_mem_record(ip) and
 * initialize the following additional fields:
 *
 * The related inode should be share-locked by the caller.  The caller is
 * on the frontend.
 *
 * record->rec.entry.base.base.key
 * record->rec.entry.base.base.rec_type
 * record->rec.entry.base.base.data_len
 * record->data		(a copy will be kmalloc'd if it cannot be embedded)
 */
int
hammer_ip_add_record(struct hammer_transaction *trans, hammer_record_t record)
{
	hammer_inode_t ip = record->ip;
	int error;

	KKASSERT(record->leaf.base.localization != 0);
	record->leaf.base.obj_id = ip->obj_id;
	record->leaf.base.obj_type = ip->ino_leaf.base.obj_type;
	error = hammer_mem_add(record);
	return(error);
}

/*
 * Locate a pre-existing bulk record in memory.  The caller wishes to
 * replace the record with a new one.  The existing record may have a
 * different length (and thus a different key) so we have to use an
 * overlap check function.
 */
static hammer_record_t
hammer_ip_get_bulk(hammer_record_t record)
{
	struct hammer_bulk_info info;
	hammer_inode_t ip = record->ip;

	info.record = record;
	info.conflict = NULL;
	hammer_rec_rb_tree_RB_SCAN(&ip->rec_tree, hammer_rec_overlap_cmp,
				   hammer_bulk_scan_callback, &info);

	return(info.conflict);	/* may be NULL */
}

/*
 * Take records vetted by overlap_cmp.  The first non-deleted record
 * (if any) stops the scan.
 */
static int
hammer_bulk_scan_callback(hammer_record_t record, void *data)
{
	struct hammer_bulk_info *info = data;

	if (record->flags & (HAMMER_RECF_DELETED_FE | HAMMER_RECF_DELETED_BE |
			     HAMMER_RECF_COMMITTED)) {
		return(0);
	}
	hammer_ref(&record->lock);
	info->conflict = record;
	return(-1);			/* stop scan */
}

/*
 * Reserve blockmap space placemarked with an in-memory record.  
 *
 * This routine is called by the frontend in order to be able to directly
 * flush a buffer cache buffer.  The frontend has locked the related buffer
 * cache buffers and we should be able to manipulate any overlapping
 * in-memory records.
 *
 * The caller is responsible for adding the returned record and deleting
 * the returned conflicting record (if any), typically by calling
 * hammer_ip_replace_bulk() (via hammer_io_direct_write()).
 */
hammer_record_t
hammer_ip_add_bulk(hammer_inode_t ip, off_t file_offset, void *data, int bytes,
		   int *errorp)
{
	hammer_record_t record;
	hammer_dedup_cache_t dcp;
	hammer_crc_t crc;
	int zone;

	/*
	 * Create a record to cover the direct write.  The record cannot
	 * be added to the in-memory RB tree here as it might conflict
	 * with an existing memory record.  See hammer_io_direct_write().
	 *
	 * The backend is responsible for finalizing the space reserved in
	 * this record.
	 *
	 * XXX bytes not aligned, depend on the reservation code to
	 * align the reservation.
	 */
	record = hammer_alloc_mem_record(ip, 0);
	zone = (bytes >= HAMMER_BUFSIZE) ? HAMMER_ZONE_LARGE_DATA_INDEX :
					   HAMMER_ZONE_SMALL_DATA_INDEX;
	if (bytes == 0)
		crc = 0;
	else
		crc = crc32(data, bytes);

	if (hammer_live_dedup == 0)
		goto nodedup;
	if ((dcp = hammer_dedup_cache_lookup(ip->hmp, crc)) != NULL) {
		struct hammer_dedup_cache tmp = *dcp;

		record->resv = hammer_blockmap_reserve_dedup(ip->hmp, zone,
			bytes, tmp.data_offset, errorp);
		if (record->resv == NULL)
			goto nodedup;

		if (!hammer_dedup_validate(&tmp, zone, bytes, data)) {
			hammer_blockmap_reserve_complete(ip->hmp, record->resv);
			goto nodedup;
		}

		record->leaf.data_offset = tmp.data_offset;
		record->flags |= HAMMER_RECF_DEDUPED;
	} else {
nodedup:
		record->resv = hammer_blockmap_reserve(ip->hmp, zone, bytes,
		       &record->leaf.data_offset, errorp);
		if (record->resv == NULL) {
			kprintf("hammer_ip_add_bulk: reservation failed\n");
			hammer_rel_mem_record(record);
			return(NULL);
		}
	}

	record->type = HAMMER_MEM_RECORD_DATA;
	record->leaf.base.rec_type = HAMMER_RECTYPE_DATA;
	record->leaf.base.obj_type = ip->ino_leaf.base.obj_type;
	record->leaf.base.obj_id = ip->obj_id;
	record->leaf.base.key = file_offset + bytes;
	record->leaf.base.localization = ip->obj_localization +
					 HAMMER_LOCALIZE_MISC;
	record->leaf.data_len = bytes;
	record->leaf.data_crc = crc;
	KKASSERT(*errorp == 0);

	return(record);
}

/*
 * Called by hammer_io_direct_write() prior to any possible completion
 * of the BIO to emplace the memory record associated with the I/O and
 * to replace any prior memory record which might still be active.
 *
 * Setting the FE deleted flag on the old record (if any) avoids any RB
 * tree insertion conflict, amoung other things.
 *
 * This has to be done prior to the caller completing any related buffer
 * cache I/O or a reinstantiation of the buffer may load data from the
 * old media location instead of the new media location.  The holding
 * of the locked buffer cache buffer serves to interlock the record
 * replacement operation.
 */
void
hammer_ip_replace_bulk(hammer_mount_t hmp, hammer_record_t record)
{
	hammer_record_t conflict;
	int error __debugvar;

	while ((conflict = hammer_ip_get_bulk(record)) != NULL) {
		if ((conflict->flags & HAMMER_RECF_INTERLOCK_BE) == 0) {
			conflict->flags |= HAMMER_RECF_DELETED_FE;
			break;
		}
		conflict->flags |= HAMMER_RECF_WANTED;
		tsleep(conflict, 0, "hmrrc3", 0);
		hammer_rel_mem_record(conflict);
	}
	error = hammer_mem_add(record);
	if (conflict)
		hammer_rel_mem_record(conflict);
	KKASSERT(error == 0);
}

/*
 * Frontend truncation code.  Scan in-memory records only.  On-disk records
 * and records in a flushing state are handled by the backend.  The vnops
 * setattr code will handle the block containing the truncation point.
 *
 * Partial blocks are not deleted.
 *
 * This code is only called on regular files.
 */
int
hammer_ip_frontend_trunc(struct hammer_inode *ip, off_t file_size)
{
	struct rec_trunc_info info;

	switch(ip->ino_data.obj_type) {
	case HAMMER_OBJTYPE_REGFILE:
		info.rec_type = HAMMER_RECTYPE_DATA;
		break;
	case HAMMER_OBJTYPE_DBFILE:
		info.rec_type = HAMMER_RECTYPE_DB;
		break;
	default:
		return(EINVAL);
	}
	info.trunc_off = file_size;
	hammer_rec_rb_tree_RB_SCAN(&ip->rec_tree, hammer_rec_trunc_cmp,
				   hammer_frontend_trunc_callback, &info);
	return(0);
}

/*
 * Scan callback for frontend records to destroy during a truncation.
 * We must ensure that DELETED_FE is set on the record or the frontend
 * will get confused in future read() calls.
 *
 * NOTE: DELETED_FE cannot be set while the record interlock (BE) is held.
 *	 In this rare case we must wait for the interlock to be cleared.
 *
 * NOTE: This function is only called on regular files.  There are further
 *	 restrictions to the setting of DELETED_FE on directory records
 *	 undergoing a flush due to sensitive inode link count calculations.
 */
static int
hammer_frontend_trunc_callback(hammer_record_t record, void *data __unused)
{
	if (record->flags & HAMMER_RECF_DELETED_FE)
		return(0);
#if 0
	if (record->flush_state == HAMMER_FST_FLUSH)
		return(0);
#endif
	hammer_ref(&record->lock);
	while (record->flags & HAMMER_RECF_INTERLOCK_BE)
		hammer_wait_mem_record_ident(record, "hmmtrr");
	record->flags |= HAMMER_RECF_DELETED_FE;
	hammer_rel_mem_record(record);
	return(0);
}

/*
 * Return 1 if the caller must check for and delete existing records
 * before writing out a new data record.
 *
 * Return 0 if the caller can just insert the record into the B-Tree without
 * checking.
 */
static int
hammer_record_needs_overwrite_delete(hammer_record_t record)
{
	hammer_inode_t ip = record->ip;
	int64_t file_offset;
	int r;

	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE)
		file_offset = record->leaf.base.key;
	else
		file_offset = record->leaf.base.key - record->leaf.data_len;
	r = (file_offset < ip->save_trunc_off);
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		if (ip->save_trunc_off <= record->leaf.base.key)
			ip->save_trunc_off = record->leaf.base.key + 1;
	} else {
		if (ip->save_trunc_off < record->leaf.base.key)
			ip->save_trunc_off = record->leaf.base.key;
	}
	return(r);
}

/*
 * Backend code.  Sync a record to the media.
 */
int
hammer_ip_sync_record_cursor(hammer_cursor_t cursor, hammer_record_t record)
{
	hammer_transaction_t trans = cursor->trans;
	int64_t file_offset;
	int bytes;
	void *bdata;
	int error;
	int doprop;

	KKASSERT(record->flush_state == HAMMER_FST_FLUSH);
	KKASSERT(record->flags & HAMMER_RECF_INTERLOCK_BE);
	KKASSERT(record->leaf.base.localization != 0);

	/*
	 * Any direct-write related to the record must complete before we
	 * can sync the record to the on-disk media.
	 */
	if (record->gflags & (HAMMER_RECG_DIRECT_IO | HAMMER_RECG_DIRECT_INVAL))
		hammer_io_direct_wait(record);

	/*
	 * If this is a bulk-data record placemarker there may be an existing
	 * record on-disk, indicating a data overwrite.  If there is the
	 * on-disk record must be deleted before we can insert our new record.
	 *
	 * We've synthesized this record and do not know what the create_tid
	 * on-disk is, nor how much data it represents.
	 *
	 * Keep in mind that (key) for data records is (base_offset + len),
	 * not (base_offset).  Also, we only want to get rid of on-disk
	 * records since we are trying to sync our in-memory record, call
	 * hammer_ip_delete_range() with truncating set to 1 to make sure
	 * it skips in-memory records.
	 *
	 * It is ok for the lookup to return ENOENT.
	 *
	 * NOTE OPTIMIZATION: sync_trunc_off is used to determine if we have
	 * to call hammer_ip_delete_range() or not.  This also means we must
	 * update sync_trunc_off() as we write.
	 */
	if (record->type == HAMMER_MEM_RECORD_DATA &&
	    hammer_record_needs_overwrite_delete(record)) {
		file_offset = record->leaf.base.key - record->leaf.data_len;
		bytes = (record->leaf.data_len + HAMMER_BUFMASK) & 
			~HAMMER_BUFMASK;
		KKASSERT((file_offset & HAMMER_BUFMASK) == 0);
		error = hammer_ip_delete_range(
				cursor, record->ip,
				file_offset, file_offset + bytes - 1,
				1);
		if (error && error != ENOENT)
			goto done;
	}

	/*
	 * If this is a general record there may be an on-disk version
	 * that must be deleted before we can insert the new record.
	 */
	if (record->type == HAMMER_MEM_RECORD_GENERAL) {
		error = hammer_delete_general(cursor, record->ip,
					      &record->leaf);
		if (error && error != ENOENT)
			goto done;
	}

	/*
	 * Setup the cursor.
	 */
	hammer_normalize_cursor(cursor);
	cursor->key_beg = record->leaf.base;
	cursor->flags &= ~HAMMER_CURSOR_INITMASK;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	cursor->flags &= ~HAMMER_CURSOR_INSERT;

	/*
	 * Records can wind up on-media before the inode itself is on-media.
	 * Flag the case.
	 */
	record->ip->flags |= HAMMER_INODE_DONDISK;

	/*
	 * If we are deleting a directory entry an exact match must be
	 * found on-disk.
	 */
	if (record->type == HAMMER_MEM_RECORD_DEL) {
		error = hammer_btree_lookup(cursor);
		if (error == 0) {
			KKASSERT(cursor->iprec == NULL);
			error = hammer_ip_delete_record(cursor, record->ip,
							trans->tid);
			if (error == 0) {
				record->flags |= HAMMER_RECF_DELETED_BE |
						 HAMMER_RECF_COMMITTED;
				++record->ip->rec_generation;
			}
		}
		goto done;
	}

	/*
	 * We are inserting.
	 *
	 * Issue a lookup to position the cursor and locate the insertion
	 * point.  The target key should not exist.  If we are creating a
	 * directory entry we may have to iterate the low 32 bits of the
	 * key to find an unused key.
	 */
	hammer_sync_lock_sh(trans);
	cursor->flags |= HAMMER_CURSOR_INSERT;
	error = hammer_btree_lookup(cursor);
	if (hammer_debug_inode)
		kprintf("DOINSERT LOOKUP %d\n", error);
	if (error == 0) {
		kprintf("hammer_ip_sync_record: duplicate rec "
			"at (%016llx)\n", (long long)record->leaf.base.key);
		if (hammer_debug_critical)
			Debugger("duplicate record1");
		error = EIO;
	}
#if 0
	if (record->type == HAMMER_MEM_RECORD_DATA)
		kprintf("sync_record  %016llx ---------------- %016llx %d\n",
			record->leaf.base.key - record->leaf.data_len,
			record->leaf.data_offset, error);
#endif

	if (error != ENOENT)
		goto done_unlock;

	/*
	 * Allocate the record and data.  The result buffers will be
	 * marked as being modified and further calls to
	 * hammer_modify_buffer() will result in unneeded UNDO records.
	 *
	 * Support zero-fill records (data == NULL and data_len != 0)
	 */
	if (record->type == HAMMER_MEM_RECORD_DATA) {
		/*
		 * The data portion of a bulk-data record has already been
		 * committed to disk, we need only adjust the layer2
		 * statistics in the same transaction as our B-Tree insert.
		 */
		KKASSERT(record->leaf.data_offset != 0);
		error = hammer_blockmap_finalize(trans,
						 record->resv,
						 record->leaf.data_offset,
						 record->leaf.data_len);

		if (hammer_live_dedup == 2 &&
		    (record->flags & HAMMER_RECF_DEDUPED) == 0) {
			hammer_dedup_cache_add(record->ip, &record->leaf);
		}
	} else if (record->data && record->leaf.data_len) {
		/*
		 * Wholely cached record, with data.  Allocate the data.
		 */
		bdata = hammer_alloc_data(trans, record->leaf.data_len,
					  record->leaf.base.rec_type,
					  &record->leaf.data_offset,
					  &cursor->data_buffer,
					  0, &error);
		if (bdata == NULL)
			goto done_unlock;
		hammer_crc_set_leaf(record->data, &record->leaf);
		hammer_modify_buffer(trans, cursor->data_buffer, NULL, 0);
		bcopy(record->data, bdata, record->leaf.data_len);
		hammer_modify_buffer_done(cursor->data_buffer);
	} else {
		/*
		 * Wholely cached record, without data.
		 */
		record->leaf.data_offset = 0;
		record->leaf.data_crc = 0;
	}

	error = hammer_btree_insert(cursor, &record->leaf, &doprop);
	if (hammer_debug_inode && error) {
		kprintf("BTREE INSERT error %d @ %016llx:%d key %016llx\n",
			error,
			(long long)cursor->node->node_offset,
			cursor->index,
			(long long)record->leaf.base.key);
	}

	/*
	 * Our record is on-disk and we normally mark the in-memory version
	 * as having been committed (and not BE-deleted).
	 *
	 * If the record represented a directory deletion but we had to
	 * sync a valid directory entry to disk due to dependancies,
	 * we must convert the record to a covering delete so the
	 * frontend does not have visibility on the synced entry.
	 *
	 * WARNING: cursor's leaf pointer may have changed after do_propagation
	 *	    returns!
	 */
	if (error == 0) {
		if (doprop) {
			hammer_btree_do_propagation(cursor,
						    record->ip->pfsm,
						    &record->leaf);
		}
		if (record->flags & HAMMER_RECF_CONVERT_DELETE) {
			/*
			 * Must convert deleted directory entry add
			 * to a directory entry delete.
			 */
			KKASSERT(record->type == HAMMER_MEM_RECORD_ADD);
			record->flags &= ~HAMMER_RECF_DELETED_FE;
			record->type = HAMMER_MEM_RECORD_DEL;
			KKASSERT(record->ip->obj_id == record->leaf.base.obj_id);
			KKASSERT(record->flush_state == HAMMER_FST_FLUSH);
			record->flags &= ~HAMMER_RECF_CONVERT_DELETE;
			KKASSERT((record->flags & (HAMMER_RECF_COMMITTED |
						 HAMMER_RECF_DELETED_BE)) == 0);
			/* converted record is not yet committed */
			/* hammer_flush_record_done takes care of the rest */
		} else {
			/*
			 * Everything went fine and we are now done with
			 * this record.
			 */
			record->flags |= HAMMER_RECF_COMMITTED;
			++record->ip->rec_generation;
		}
	} else {
		if (record->leaf.data_offset) {
			hammer_blockmap_free(trans, record->leaf.data_offset,
					     record->leaf.data_len);
		}
	}
done_unlock:
	hammer_sync_unlock(trans);
done:
	return(error);
}

/*
 * Add the record to the inode's rec_tree.  The low 32 bits of a directory
 * entry's key is used to deal with hash collisions in the upper 32 bits.
 * A unique 64 bit key is generated in-memory and may be regenerated a
 * second time when the directory record is flushed to the on-disk B-Tree.
 *
 * A referenced record is passed to this function.  This function
 * eats the reference.  If an error occurs the record will be deleted.
 *
 * A copy of the temporary record->data pointer provided by the caller
 * will be made.
 */
int
hammer_mem_add(hammer_record_t record)
{
	hammer_mount_t hmp = record->ip->hmp;

	/*
	 * Make a private copy of record->data
	 */
	if (record->data)
		KKASSERT(record->flags & HAMMER_RECF_ALLOCDATA);

	/*
	 * Insert into the RB tree.  A unique key should have already
	 * been selected if this is a directory entry.
	 */
	if (RB_INSERT(hammer_rec_rb_tree, &record->ip->rec_tree, record)) {
		record->flags |= HAMMER_RECF_DELETED_FE;
		hammer_rel_mem_record(record);
		return (EEXIST);
	}
	++hmp->count_newrecords;
	++hmp->rsv_recs;
	++record->ip->rsv_recs;
	record->ip->hmp->rsv_databytes += record->leaf.data_len;
	record->flags |= HAMMER_RECF_ONRBTREE;
	hammer_modify_inode(NULL, record->ip, HAMMER_INODE_XDIRTY);
	hammer_rel_mem_record(record);
	return(0);
}

/************************************************************************
 *		     HAMMER INODE MERGED-RECORD FUNCTIONS		*
 ************************************************************************
 *
 * These functions augment the B-Tree scanning functions in hammer_btree.c
 * by merging in-memory records with on-disk records.
 */

/*
 * Locate a particular record either in-memory or on-disk.
 *
 * NOTE: This is basically a standalone routine, hammer_ip_next() may
 * NOT be called to iterate results.
 */
int
hammer_ip_lookup(hammer_cursor_t cursor)
{
	int error;

	/*
	 * If the element is in-memory return it without searching the
	 * on-disk B-Tree
	 */
	KKASSERT(cursor->ip);
	error = hammer_mem_lookup(cursor);
	if (error == 0) {
		cursor->leaf = &cursor->iprec->leaf;
		return(error);
	}
	if (error != ENOENT)
		return(error);

	/*
	 * If the inode has on-disk components search the on-disk B-Tree.
	 */
	if ((cursor->ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DONDISK)) == 0)
		return(error);
	error = hammer_btree_lookup(cursor);
	if (error == 0)
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_LEAF);
	return(error);
}

/*
 * Helper for hammer_ip_first()/hammer_ip_next()
 *
 * NOTE: Both ATEDISK and DISKEOF will be set the same.  This sets up
 * hammer_ip_first() for calling hammer_ip_next(), and sets up the re-seek
 * state if hammer_ip_next() needs to re-seek.
 */
static __inline
int
_hammer_ip_seek_btree(hammer_cursor_t cursor)
{
	hammer_inode_t ip = cursor->ip;
	int error;

	if (ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DONDISK)) {
		error = hammer_btree_lookup(cursor);
		if (error == ENOENT || error == EDEADLK) {
			if (hammer_debug_general & 0x2000) {
				kprintf("error %d node %p %016llx index %d\n",
					error, cursor->node,
					(long long)cursor->node->node_offset,
					cursor->index);
			}
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
			error = hammer_btree_iterate(cursor);
		}
		if (error == 0) {
			cursor->flags &= ~(HAMMER_CURSOR_DISKEOF |
					   HAMMER_CURSOR_ATEDISK);
		} else {
			cursor->flags |= HAMMER_CURSOR_DISKEOF |
					 HAMMER_CURSOR_ATEDISK;
			if (error == ENOENT)
				error = 0;
		}
	} else {
		cursor->flags |= HAMMER_CURSOR_DISKEOF | HAMMER_CURSOR_ATEDISK;
		error = 0;
	}
	return(error);
}

/*
 * Helper for hammer_ip_next()
 *
 * The caller has determined that the media cursor is further along than the
 * memory cursor and must be reseeked after a generation number change.
 */
static
int
_hammer_ip_reseek(hammer_cursor_t cursor)
{
	struct hammer_base_elm save;
	hammer_btree_elm_t elm;
	int error __debugvar;
	int r;
	int again = 0;

	/*
	 * Do the re-seek.
	 */
	kprintf("HAMMER: Debug: re-seeked during scan @ino=%016llx\n",
		(long long)cursor->ip->obj_id);
	save = cursor->key_beg;
	cursor->key_beg = cursor->iprec->leaf.base;
	error = _hammer_ip_seek_btree(cursor);
	KKASSERT(error == 0);
	cursor->key_beg = save;

	/*
	 * If the memory record was previous returned to
	 * the caller and the media record matches
	 * (-1/+1: only create_tid differs), then iterate
	 * the media record to avoid a double result.
	 */
	if ((cursor->flags & HAMMER_CURSOR_ATEDISK) == 0 &&
	    (cursor->flags & HAMMER_CURSOR_LASTWASMEM)) {
		elm = &cursor->node->ondisk->elms[cursor->index];
		r = hammer_btree_cmp(&elm->base,
				     &cursor->iprec->leaf.base);
		if (cursor->flags & HAMMER_CURSOR_ASOF) {
			if (r >= -1 && r <= 1) {
				kprintf("HAMMER: Debug: iterated after "
					"re-seek (asof r=%d)\n", r);
				cursor->flags |= HAMMER_CURSOR_ATEDISK;
				again = 1;
			}
		} else {
			if (r == 0) {
				kprintf("HAMMER: Debug: iterated after "
					"re-seek\n");
				cursor->flags |= HAMMER_CURSOR_ATEDISK;
				again = 1;
			}
		}
	}
	return(again);
}

/*
 * Locate the first record within the cursor's key_beg/key_end range,
 * restricted to a particular inode.  0 is returned on success, ENOENT
 * if no records matched the requested range, or some other error.
 *
 * When 0 is returned hammer_ip_next() may be used to iterate additional
 * records within the requested range.
 *
 * This function can return EDEADLK, requiring the caller to terminate
 * the cursor and try again.
 */

int
hammer_ip_first(hammer_cursor_t cursor)
{
	hammer_inode_t ip __debugvar = cursor->ip;
	int error;

	KKASSERT(ip != NULL);

	/*
	 * Clean up fields and setup for merged scan
	 */
	cursor->flags &= ~HAMMER_CURSOR_RETEST;

	/*
	 * Search the in-memory record list (Red-Black tree).  Unlike the
	 * B-Tree search, mem_first checks for records in the range.
	 *
	 * This function will setup both ATEMEM and MEMEOF properly for
	 * the ip iteration.  ATEMEM will be set if MEMEOF is set.
	 */
	hammer_mem_first(cursor);

	/*
	 * Detect generation changes during blockages, including
	 * blockages which occur on the initial btree search.
	 */
	cursor->rec_generation = cursor->ip->rec_generation;

	/*
	 * Initial search and result
	 */
	error = _hammer_ip_seek_btree(cursor);
	if (error == 0)
		error = hammer_ip_next(cursor);

	return (error);
}

/*
 * Retrieve the next record in a merged iteration within the bounds of the
 * cursor.  This call may be made multiple times after the cursor has been
 * initially searched with hammer_ip_first().
 *
 * There are numerous special cases in this code to deal with races between
 * in-memory records and on-media records.
 *
 * 0 is returned on success, ENOENT if no further records match the
 * requested range, or some other error code is returned.
 */
int
hammer_ip_next(hammer_cursor_t cursor)
{
	hammer_btree_elm_t elm;
	hammer_record_t rec;
	hammer_record_t tmprec;
	int error;
	int r;

again:
	/*
	 * Get the next on-disk record
	 *
	 * NOTE: If we deleted the last on-disk record we had scanned
	 * 	 ATEDISK will be clear and RETEST will be set, forcing
	 *	 a call to iterate.  The fact that ATEDISK is clear causes
	 *	 iterate to re-test the 'current' element.  If ATEDISK is
	 *	 set, iterate will skip the 'current' element.
	 */
	error = 0;
	if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
		if (cursor->flags & (HAMMER_CURSOR_ATEDISK |
				     HAMMER_CURSOR_RETEST)) {
			error = hammer_btree_iterate(cursor);
			cursor->flags &= ~HAMMER_CURSOR_RETEST;
			if (error == 0) {
				cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
				hammer_cache_node(&cursor->ip->cache[1],
						  cursor->node);
			} else if (error == ENOENT) {
				cursor->flags |= HAMMER_CURSOR_DISKEOF |
						 HAMMER_CURSOR_ATEDISK;
				error = 0;
			}
		}
	}

	/*
	 * If the generation changed the backend has deleted or committed
	 * one or more memory records since our last check.
	 *
	 * When this case occurs if the disk cursor is > current memory record
	 * or the disk cursor is at EOF, we must re-seek the disk-cursor.
	 * Since the cursor is ahead it must have not yet been eaten (if
	 * not at eof anyway). (XXX data offset case?)
	 *
	 * NOTE: we are not doing a full check here.  That will be handled
	 * later on.
	 *
	 * If we have exhausted all memory records we do not have to do any
	 * further seeks.
	 */
	while (cursor->rec_generation != cursor->ip->rec_generation &&
	       error == 0
	) {
		kprintf("HAMMER: Debug: generation changed during scan @ino=%016llx\n", (long long)cursor->ip->obj_id);
		cursor->rec_generation = cursor->ip->rec_generation;
		if (cursor->flags & HAMMER_CURSOR_MEMEOF)
			break;
		if (cursor->flags & HAMMER_CURSOR_DISKEOF) {
			r = 1;
		} else {
			KKASSERT((cursor->flags & HAMMER_CURSOR_ATEDISK) == 0);
			elm = &cursor->node->ondisk->elms[cursor->index];
			r = hammer_btree_cmp(&elm->base,
					     &cursor->iprec->leaf.base);
		}

		/*
		 * Do we re-seek the media cursor?
		 */
		if (r > 0) {
			if (_hammer_ip_reseek(cursor))
				goto again;
		}
	}

	/*
	 * We can now safely get the next in-memory record.  We cannot
	 * block here.
	 *
	 * hammer_rec_scan_cmp:  Is the record still in our general range,
	 *			 (non-inclusive of snapshot exclusions)?
	 * hammer_rec_scan_callback: Is the record in our snapshot?
	 */
	tmprec = NULL;
	if ((cursor->flags & HAMMER_CURSOR_MEMEOF) == 0) {
		/*
		 * If the current memory record was eaten then get the next
		 * one.  Stale records are skipped.
		 */
		if (cursor->flags & HAMMER_CURSOR_ATEMEM) {
			tmprec = cursor->iprec;
			cursor->iprec = NULL;
			rec = hammer_rec_rb_tree_RB_NEXT(tmprec);
			while (rec) {
				if (hammer_rec_scan_cmp(rec, cursor) != 0)
					break;
				if (hammer_rec_scan_callback(rec, cursor) != 0)
					break;
				rec = hammer_rec_rb_tree_RB_NEXT(rec);
			}
			if (cursor->iprec) {
				KKASSERT(cursor->iprec == rec);
				cursor->flags &= ~HAMMER_CURSOR_ATEMEM;
			} else {
				cursor->flags |= HAMMER_CURSOR_MEMEOF;
			}
			cursor->flags &= ~HAMMER_CURSOR_LASTWASMEM;
		}
	}

	/*
	 * MEMORY RECORD VALIDITY TEST
	 *
	 * (We still can't block, which is why tmprec is being held so
	 * long).
	 *
	 * If the memory record is no longer valid we skip it.  It may
	 * have been deleted by the frontend.  If it was deleted or
	 * committed by the backend the generation change re-seeked the
	 * disk cursor and the record will be present there.
	 */
	if (error == 0 && (cursor->flags & HAMMER_CURSOR_MEMEOF) == 0) {
		KKASSERT(cursor->iprec);
		KKASSERT((cursor->flags & HAMMER_CURSOR_ATEMEM) == 0);
		if (!hammer_ip_iterate_mem_good(cursor, cursor->iprec)) {
			cursor->flags |= HAMMER_CURSOR_ATEMEM;
			if (tmprec)
				hammer_rel_mem_record(tmprec);
			goto again;
		}
	}
	if (tmprec)
		hammer_rel_mem_record(tmprec);

	/*
	 * Extract either the disk or memory record depending on their
	 * relative position.
	 */
	error = 0;
	switch(cursor->flags & (HAMMER_CURSOR_ATEDISK | HAMMER_CURSOR_ATEMEM)) {
	case 0:
		/*
		 * Both entries valid.   Compare the entries and nominally
		 * return the first one in the sort order.  Numerous cases
		 * require special attention, however.
		 */
		elm = &cursor->node->ondisk->elms[cursor->index];
		r = hammer_btree_cmp(&elm->base, &cursor->iprec->leaf.base);

		/*
		 * If the two entries differ only by their key (-2/2) or
		 * create_tid (-1/1), and are DATA records, we may have a
		 * nominal match.  We have to calculate the base file
		 * offset of the data.
		 */
		if (r <= 2 && r >= -2 && r != 0 &&
		    cursor->ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE &&
		    cursor->iprec->type == HAMMER_MEM_RECORD_DATA) {
			int64_t base1 = elm->leaf.base.key - elm->leaf.data_len;
			int64_t base2 = cursor->iprec->leaf.base.key -
					cursor->iprec->leaf.data_len;
			if (base1 == base2)
				r = 0;
		}

		if (r < 0) {
			error = hammer_btree_extract(cursor,
						     HAMMER_CURSOR_GET_LEAF);
			cursor->flags |= HAMMER_CURSOR_ATEDISK;
			cursor->flags &= ~HAMMER_CURSOR_LASTWASMEM;
			break;
		}

		/*
		 * If the entries match exactly the memory entry is either
		 * an on-disk directory entry deletion or a bulk data
		 * overwrite.  If it is a directory entry deletion we eat
		 * both entries.
		 *
		 * For the bulk-data overwrite case it is possible to have
		 * visibility into both, which simply means the syncer
		 * hasn't gotten around to doing the delete+insert sequence
		 * on the B-Tree.  Use the memory entry and throw away the
		 * on-disk entry.
		 *
		 * If the in-memory record is not either of these we
		 * probably caught the syncer while it was syncing it to
		 * the media.  Since we hold a shared lock on the cursor,
		 * the in-memory record had better be marked deleted at
		 * this point.
		 */
		if (r == 0) {
			if (cursor->iprec->type == HAMMER_MEM_RECORD_DEL) {
				if ((cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY) == 0) {
					cursor->flags |= HAMMER_CURSOR_ATEDISK;
					cursor->flags |= HAMMER_CURSOR_ATEMEM;
					goto again;
				}
			} else if (cursor->iprec->type == HAMMER_MEM_RECORD_DATA) {
				if ((cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY) == 0) {
					cursor->flags |= HAMMER_CURSOR_ATEDISK;
				}
				/* fall through to memory entry */
			} else {
				panic("hammer_ip_next: duplicate mem/b-tree entry %p %d %08x", cursor->iprec, cursor->iprec->type, cursor->iprec->flags);
				cursor->flags |= HAMMER_CURSOR_ATEMEM;
				goto again;
			}
		}
		/* fall through to the memory entry */
	case HAMMER_CURSOR_ATEDISK:
		/*
		 * Only the memory entry is valid.
		 */
		cursor->leaf = &cursor->iprec->leaf;
		cursor->flags |= HAMMER_CURSOR_ATEMEM;
		cursor->flags |= HAMMER_CURSOR_LASTWASMEM;

		/*
		 * If the memory entry is an on-disk deletion we should have
		 * also had found a B-Tree record.  If the backend beat us
		 * to it it would have interlocked the cursor and we should
		 * have seen the in-memory record marked DELETED_FE.
		 */
		if (cursor->iprec->type == HAMMER_MEM_RECORD_DEL &&
		    (cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY) == 0) {
			panic("hammer_ip_next: del-on-disk with no b-tree entry iprec %p flags %08x", cursor->iprec, cursor->iprec->flags);
		}
		break;
	case HAMMER_CURSOR_ATEMEM:
		/*
		 * Only the disk entry is valid
		 */
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_LEAF);
		cursor->flags |= HAMMER_CURSOR_ATEDISK;
		cursor->flags &= ~HAMMER_CURSOR_LASTWASMEM;
		break;
	default:
		/*
		 * Neither entry is valid
		 *
		 * XXX error not set properly
		 */
		cursor->flags &= ~HAMMER_CURSOR_LASTWASMEM;
		cursor->leaf = NULL;
		error = ENOENT;
		break;
	}
	return(error);
}

/*
 * Resolve the cursor->data pointer for the current cursor position in
 * a merged iteration.
 */
int
hammer_ip_resolve_data(hammer_cursor_t cursor)
{
	hammer_record_t record;
	int error;

	if (hammer_cursor_inmem(cursor)) {
		/*
		 * The data associated with an in-memory record is usually
		 * kmalloced, but reserve-ahead data records will have an
		 * on-disk reference.
		 *
		 * NOTE: Reserve-ahead data records must be handled in the
		 * context of the related high level buffer cache buffer
		 * to interlock against async writes.
		 */
		record = cursor->iprec;
		cursor->data = record->data;
		error = 0;
		if (cursor->data == NULL) {
			KKASSERT(record->leaf.base.rec_type ==
				 HAMMER_RECTYPE_DATA);
			cursor->data = hammer_bread_ext(cursor->trans->hmp,
						    record->leaf.data_offset,
						    record->leaf.data_len,
						    &error,
						    &cursor->data_buffer);
		}
	} else {
		cursor->leaf = &cursor->node->ondisk->elms[cursor->index].leaf;
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_DATA);
	}
	return(error);
}

/*
 * Backend truncation / record replacement - delete records in range.
 *
 * Delete all records within the specified range for inode ip.  In-memory
 * records still associated with the frontend are ignored. 
 *
 * If truncating is non-zero in-memory records associated with the back-end
 * are ignored.  If truncating is > 1 we can return EWOULDBLOCK.
 *
 * NOTES:
 *
 *	* An unaligned range will cause new records to be added to cover
 *        the edge cases. (XXX not implemented yet).
 *
 *	* Replacement via reservations (see hammer_ip_sync_record_cursor())
 *        also do not deal with unaligned ranges.
 *
 *	* ran_end is inclusive (e.g. 0,1023 instead of 0,1024).
 *
 *	* Record keys for regular file data have to be special-cased since
 * 	  they indicate the end of the range (key = base + bytes).
 *
 *	* This function may be asked to delete ridiculously huge ranges, for
 *	  example if someone truncates or removes a 1TB regular file.  We
 *	  must be very careful on restarts and we may have to stop w/
 *	  EWOULDBLOCK to avoid blowing out the buffer cache.
 */
int
hammer_ip_delete_range(hammer_cursor_t cursor, hammer_inode_t ip,
		       int64_t ran_beg, int64_t ran_end, int truncating)
{
	hammer_transaction_t trans = cursor->trans;
	hammer_btree_leaf_elm_t leaf;
	int error;
	int64_t off;
	int64_t tmp64;

#if 0
	kprintf("delete_range %p %016llx-%016llx\n", ip, ran_beg, ran_end);
#endif

	KKASSERT(trans->type == HAMMER_TRANS_FLS);
retry:
	hammer_normalize_cursor(cursor);
	cursor->key_beg.localization = ip->obj_localization +
				       HAMMER_LOCALIZE_MISC;
	cursor->key_beg.obj_id = ip->obj_id;
	cursor->key_beg.create_tid = 0;
	cursor->key_beg.delete_tid = 0;
	cursor->key_beg.obj_type = 0;

	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor->key_beg.key = ran_beg;
		cursor->key_beg.rec_type = HAMMER_RECTYPE_DB;
	} else {
		/*
		 * The key in the B-Tree is (base+bytes), so the first possible
		 * matching key is ran_beg + 1.
		 */
		cursor->key_beg.key = ran_beg + 1;
		cursor->key_beg.rec_type = HAMMER_RECTYPE_DATA;
	}

	cursor->key_end = cursor->key_beg;
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor->key_end.key = ran_end;
	} else {
		tmp64 = ran_end + MAXPHYS + 1;	/* work around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor->key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor->key_end.key = ran_end + MAXPHYS + 1;
	}

	cursor->asof = ip->obj_asof;
	cursor->flags &= ~HAMMER_CURSOR_INITMASK;
	cursor->flags |= HAMMER_CURSOR_ASOF;
	cursor->flags |= HAMMER_CURSOR_DELETE_VISIBILITY;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	cursor->flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(cursor);

	/*
	 * Iterate through matching records and mark them as deleted.
	 */
	while (error == 0) {
		leaf = cursor->leaf;

		KKASSERT(leaf->base.delete_tid == 0);
		KKASSERT(leaf->base.obj_id == ip->obj_id);

		/*
		 * There may be overlap cases for regular file data.  Also
		 * remember the key for a regular file record is (base + len),
		 * NOT (base).
		 *
		 * Note that due to duplicates (mem & media) allowed by
		 * DELETE_VISIBILITY, off can wind up less then ran_beg.
		 */
		if (leaf->base.rec_type == HAMMER_RECTYPE_DATA) {
			off = leaf->base.key - leaf->data_len;
			/*
			 * Check the left edge case.  We currently do not
			 * split existing records.
			 */
			if (off < ran_beg && leaf->base.key > ran_beg) {
				panic("hammer left edge case %016llx %d",
					(long long)leaf->base.key,
					leaf->data_len);
			}

			/*
			 * Check the right edge case.  Note that the
			 * record can be completely out of bounds, which
			 * terminates the search.
			 *
			 * base->key is exclusive of the right edge while
			 * ran_end is inclusive of the right edge.  The
			 * (key - data_len) left boundary is inclusive.
			 *
			 * XXX theory-check this test at some point, are
			 * we missing a + 1 somewhere?  Note that ran_end
			 * could overflow.
			 */
			if (leaf->base.key - 1 > ran_end) {
				if (leaf->base.key - leaf->data_len > ran_end)
					break;
				panic("hammer right edge case");
			}
		} else {
			off = leaf->base.key;
		}

		/*
		 * Delete the record.  When truncating we do not delete
		 * in-memory (data) records because they represent data
		 * written after the truncation.
		 *
		 * This will also physically destroy the B-Tree entry and
		 * data if the retention policy dictates.  The function
		 * will set HAMMER_CURSOR_RETEST to cause hammer_ip_next()
		 * to retest the new 'current' element.
		 */
		if (truncating == 0 || hammer_cursor_ondisk(cursor)) {
			error = hammer_ip_delete_record(cursor, ip, trans->tid);
			/*
			 * If we have built up too many meta-buffers we risk
			 * deadlocking the kernel and must stop.  This can
			 * occur when deleting ridiculously huge files.
			 * sync_trunc_off is updated so the next cycle does
			 * not re-iterate records we have already deleted.
			 *
			 * This is only done with formal truncations.
			 */
			if (truncating > 1 && error == 0 &&
			    hammer_flusher_meta_limit(ip->hmp)) {
				ip->sync_trunc_off = off;
				error = EWOULDBLOCK;
			}
		}
		if (error)
			break;
		ran_beg = off;	/* for restart */
		error = hammer_ip_next(cursor);
	}
	if (cursor->node)
		hammer_cache_node(&ip->cache[1], cursor->node);

	if (error == EDEADLK) {
		hammer_done_cursor(cursor);
		error = hammer_init_cursor(trans, cursor, &ip->cache[1], ip);
		if (error == 0)
			goto retry;
	}
	if (error == ENOENT)
		error = 0;
	return(error);
}

/*
 * This backend function deletes the specified record on-disk, similar to
 * delete_range but for a specific record.  Unlike the exact deletions
 * used when deleting a directory entry this function uses an ASOF search 
 * like delete_range.
 *
 * This function may be called with ip->obj_asof set for a slave snapshot,
 * so don't use it.  We always delete non-historical records only.
 */
static int
hammer_delete_general(hammer_cursor_t cursor, hammer_inode_t ip,
		      hammer_btree_leaf_elm_t leaf)
{
	hammer_transaction_t trans = cursor->trans;
	int error;

	KKASSERT(trans->type == HAMMER_TRANS_FLS);
retry:
	hammer_normalize_cursor(cursor);
	cursor->key_beg = leaf->base;
	cursor->asof = HAMMER_MAX_TID;
	cursor->flags &= ~HAMMER_CURSOR_INITMASK;
	cursor->flags |= HAMMER_CURSOR_ASOF;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	cursor->flags &= ~HAMMER_CURSOR_INSERT;

	error = hammer_btree_lookup(cursor);
	if (error == 0) {
		error = hammer_ip_delete_record(cursor, ip, trans->tid);
	}
	if (error == EDEADLK) {
		hammer_done_cursor(cursor);
		error = hammer_init_cursor(trans, cursor, &ip->cache[1], ip);
		if (error == 0)
			goto retry;
	}
	return(error);
}

/*
 * This function deletes remaining auxillary records when an inode is
 * being deleted.  This function explicitly does not delete the
 * inode record, directory entry, data, or db records.  Those must be
 * properly disposed of prior to this call.
 */
int
hammer_ip_delete_clean(hammer_cursor_t cursor, hammer_inode_t ip, int *countp)
{
	hammer_transaction_t trans = cursor->trans;
	hammer_btree_leaf_elm_t leaf __debugvar;
	int error;

	KKASSERT(trans->type == HAMMER_TRANS_FLS);
retry:
	hammer_normalize_cursor(cursor);
	cursor->key_beg.localization = ip->obj_localization +
				       HAMMER_LOCALIZE_MISC;
	cursor->key_beg.obj_id = ip->obj_id;
	cursor->key_beg.create_tid = 0;
	cursor->key_beg.delete_tid = 0;
	cursor->key_beg.obj_type = 0;
	cursor->key_beg.rec_type = HAMMER_RECTYPE_CLEAN_START;
	cursor->key_beg.key = HAMMER_MIN_KEY;

	cursor->key_end = cursor->key_beg;
	cursor->key_end.rec_type = HAMMER_RECTYPE_MAX;
	cursor->key_end.key = HAMMER_MAX_KEY;

	cursor->asof = ip->obj_asof;
	cursor->flags &= ~HAMMER_CURSOR_INITMASK;
	cursor->flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;
	cursor->flags |= HAMMER_CURSOR_DELETE_VISIBILITY;
	cursor->flags |= HAMMER_CURSOR_BACKEND;

	error = hammer_ip_first(cursor);

	/*
	 * Iterate through matching records and mark them as deleted.
	 */
	while (error == 0) {
		leaf = cursor->leaf;

		KKASSERT(leaf->base.delete_tid == 0);

		/*
		 * Mark the record and B-Tree entry as deleted.  This will
		 * also physically delete the B-Tree entry, record, and
		 * data if the retention policy dictates.  The function
		 * will set HAMMER_CURSOR_RETEST to cause hammer_ip_next()
		 * to retest the new 'current' element.
		 *
		 * Directory entries (and delete-on-disk directory entries)
		 * must be synced and cannot be deleted.
		 */
		error = hammer_ip_delete_record(cursor, ip, trans->tid);
		++*countp;
		if (error)
			break;
		error = hammer_ip_next(cursor);
	}
	if (cursor->node)
		hammer_cache_node(&ip->cache[1], cursor->node);
	if (error == EDEADLK) {
		hammer_done_cursor(cursor);
		error = hammer_init_cursor(trans, cursor, &ip->cache[1], ip);
		if (error == 0)
			goto retry;
	}
	if (error == ENOENT)
		error = 0;
	return(error);
}

/*
 * Delete the record at the current cursor.  On success the cursor will
 * be positioned appropriately for an iteration but may no longer be at
 * a leaf node.
 *
 * This routine is only called from the backend.
 *
 * NOTE: This can return EDEADLK, requiring the caller to terminate the
 * cursor and retry.
 */
int
hammer_ip_delete_record(hammer_cursor_t cursor, hammer_inode_t ip,
			hammer_tid_t tid)
{
	hammer_record_t iprec;
	int error;

	KKASSERT(cursor->flags & HAMMER_CURSOR_BACKEND);
	KKASSERT(tid != 0);

	/*
	 * In-memory (unsynchronized) records can simply be freed.  This
	 * only occurs in range iterations since all other records are
	 * individually synchronized.  Thus there should be no confusion with
	 * the interlock.
	 *
	 * An in-memory record may be deleted before being committed to disk,
	 * but could have been accessed in the mean time.  The reservation
	 * code will deal with the case.
	 */
	if (hammer_cursor_inmem(cursor)) {
		iprec = cursor->iprec;
		KKASSERT((iprec->flags & HAMMER_RECF_INTERLOCK_BE) ==0);
		iprec->flags |= HAMMER_RECF_DELETED_FE;
		iprec->flags |= HAMMER_RECF_DELETED_BE;
		KKASSERT(iprec->ip == ip);
		++ip->rec_generation;
		return(0);
	}

	/*
	 * On-disk records are marked as deleted by updating their delete_tid.
	 * This does not effect their position in the B-Tree (which is based
	 * on their create_tid).
	 *
	 * Frontend B-Tree operations track inodes so we tell 
	 * hammer_delete_at_cursor() not to.
	 */
	error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_LEAF);

	if (error == 0) {
		error = hammer_delete_at_cursor(
				cursor,
				HAMMER_DELETE_ADJUST | hammer_nohistory(ip),
				cursor->trans->tid,
				cursor->trans->time32,
				0, NULL);
	}
	return(error);
}

/*
 * Used to write a generic record w/optional data to the media b-tree
 * when no inode context is available.  Used by the mirroring and
 * snapshot code.
 *
 * Caller must set cursor->key_beg to leaf->base.  The cursor must be
 * flagged for backend operation and not flagged ASOF (since we are
 * doing an insertion).
 *
 * This function will acquire the appropriate sync lock and will set
 * the cursor insertion flag for the operation, do the btree lookup,
 * and the insertion, and clear the insertion flag and sync lock before
 * returning.  The cursor state will be such that the caller can continue
 * scanning (used by the mirroring code).
 *
 * mode: HAMMER_CREATE_MODE_UMIRROR	copyin data, check crc
 *	 HAMMER_CREATE_MODE_SYS		bcopy data, generate crc
 *
 * NOTE: EDEADLK can be returned.  The caller must do deadlock handling and
 *		  retry.
 *
 *	 EALREADY can be returned if the record already exists (WARNING,
 *	 	  because ASOF cannot be used no check is made for illegal
 *		  duplicates).
 *
 * NOTE: Do not use the function for normal inode-related records as this
 *	 functions goes directly to the media and is not integrated with
 *	 in-memory records.
 */
int
hammer_create_at_cursor(hammer_cursor_t cursor, hammer_btree_leaf_elm_t leaf,
			void *udata, int mode)
{
	hammer_transaction_t trans;
	hammer_buffer_t data_buffer;
	hammer_off_t ndata_offset;
	hammer_tid_t high_tid;
	void *ndata;
	int error;
	int doprop;

	trans = cursor->trans;
	data_buffer = NULL;
	ndata_offset = 0;
	doprop = 0;

	KKASSERT((cursor->flags &
		  (HAMMER_CURSOR_BACKEND | HAMMER_CURSOR_ASOF)) ==
		  (HAMMER_CURSOR_BACKEND));

	hammer_sync_lock_sh(trans);

	if (leaf->data_len) {
		ndata = hammer_alloc_data(trans, leaf->data_len,
					  leaf->base.rec_type,
					  &ndata_offset, &data_buffer,
					  0, &error);
		if (ndata == NULL) {
			hammer_sync_unlock(trans);
			return (error);
		}
		leaf->data_offset = ndata_offset;
		hammer_modify_buffer(trans, data_buffer, NULL, 0);

		switch(mode) {
		case HAMMER_CREATE_MODE_UMIRROR:
			error = copyin(udata, ndata, leaf->data_len);
			if (error == 0) {
				if (hammer_crc_test_leaf(ndata, leaf) == 0) {
					kprintf("data crc mismatch on pipe\n");
					error = EINVAL;
				} else {
					error = hammer_cursor_localize_data(
							ndata, leaf);
				}
			}
			break;
		case HAMMER_CREATE_MODE_SYS:
			bcopy(udata, ndata, leaf->data_len);
			error = 0;
			hammer_crc_set_leaf(ndata, leaf);
			break;
		default:
			panic("hammer: hammer_create_at_cursor: bad mode %d",
				mode);
			break; /* NOT REACHED */
		}
		hammer_modify_buffer_done(data_buffer);
	} else {
		leaf->data_offset = 0;
		error = 0;
		ndata = NULL;
	}
	if (error)
		goto failed;

	/*
	 * Do the insertion.  This can fail with a EDEADLK or EALREADY
	 */
	cursor->flags |= HAMMER_CURSOR_INSERT;
	error = hammer_btree_lookup(cursor);
	if (error != ENOENT) {
		if (error == 0)
			error = EALREADY;
		goto failed;
	}
	error = hammer_btree_insert(cursor, leaf, &doprop);

	/*
	 * Cursor is left on current element, we want to skip it now.
	 * (in case the caller is scanning)
	 */
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	cursor->flags &= ~HAMMER_CURSOR_INSERT;

	/*
	 * If the insertion happens to be creating (and not just replacing)
	 * an inode we have to track it.
	 */
	if (error == 0 &&
	    leaf->base.rec_type == HAMMER_RECTYPE_INODE &&
	    leaf->base.delete_tid == 0) {
		hammer_modify_volume_field(trans, trans->rootvol,
					   vol0_stat_inodes);
		++trans->hmp->rootvol->ondisk->vol0_stat_inodes;
		hammer_modify_volume_done(trans->rootvol);
	}

	/*
	 * vol0_next_tid must track the highest TID stored in the filesystem.
	 * We do not need to generate undo for this update.
	 */
	high_tid = leaf->base.create_tid;
	if (high_tid < leaf->base.delete_tid)
		high_tid = leaf->base.delete_tid;
	if (trans->rootvol->ondisk->vol0_next_tid < high_tid) {
		hammer_modify_volume(trans, trans->rootvol, NULL, 0);
		trans->rootvol->ondisk->vol0_next_tid = high_tid;
		hammer_modify_volume_done(trans->rootvol);
	}

	/*
	 * WARNING!  cursor's leaf pointer may have changed after
	 * 	     do_propagation returns.
	 */
	if (error == 0 && doprop)
		hammer_btree_do_propagation(cursor, NULL, leaf);

failed:
	/*
	 * Cleanup
	 */
	if (error && leaf->data_offset) {
		hammer_blockmap_free(trans, leaf->data_offset, leaf->data_len);

	}
	hammer_sync_unlock(trans);
	if (data_buffer)
		hammer_rel_buffer(data_buffer, 0);
	return (error);
}

/*
 * Delete the B-Tree element at the current cursor and do any necessary
 * mirror propagation.
 *
 * The cursor must be properly positioned for an iteration on return but
 * may be pointing at an internal element.
 *
 * An element can be un-deleted by passing a delete_tid of 0 with
 * HAMMER_DELETE_ADJUST.
 */
int
hammer_delete_at_cursor(hammer_cursor_t cursor, int delete_flags,
			hammer_tid_t delete_tid, u_int32_t delete_ts,
			int track, int64_t *stat_bytes)
{
	struct hammer_btree_leaf_elm save_leaf;
	hammer_transaction_t trans;
	hammer_btree_leaf_elm_t leaf;
	hammer_node_t node;
	hammer_btree_elm_t elm;
	hammer_off_t data_offset;
	int32_t data_len;
	int error;
	int icount;
	int doprop;

	error = hammer_cursor_upgrade(cursor);
	if (error)
		return(error);

	trans = cursor->trans;
	node = cursor->node;
	elm = &node->ondisk->elms[cursor->index];
	leaf = &elm->leaf;
	KKASSERT(elm->base.btype == HAMMER_BTREE_TYPE_RECORD);

	hammer_sync_lock_sh(trans);
	doprop = 0;
	icount = 0;

	/*
	 * Adjust the delete_tid.  Update the mirror_tid propagation field
	 * as well.  delete_tid can be 0 (undelete -- used by mirroring).
	 */
	if (delete_flags & HAMMER_DELETE_ADJUST) {
		if (elm->base.rec_type == HAMMER_RECTYPE_INODE) {
			if (elm->leaf.base.delete_tid == 0 && delete_tid)
				icount = -1;
			if (elm->leaf.base.delete_tid && delete_tid == 0)
				icount = 1;
		}

		hammer_modify_node(trans, node, elm, sizeof(*elm));
		elm->leaf.base.delete_tid = delete_tid;
		elm->leaf.delete_ts = delete_ts;
		hammer_modify_node_done(node);

		if (elm->leaf.base.delete_tid > node->ondisk->mirror_tid) {
			hammer_modify_node_field(trans, node, mirror_tid);
			node->ondisk->mirror_tid = elm->leaf.base.delete_tid;
			hammer_modify_node_done(node);
			doprop = 1;
			if (hammer_debug_general & 0x0002) {
				kprintf("delete_at_cursor: propagate %016llx"
					" @%016llx\n",
					(long long)elm->leaf.base.delete_tid,
					(long long)node->node_offset);
			}
		}

		/*
		 * Adjust for the iteration.  We have deleted the current
		 * element and want to clear ATEDISK so the iteration does
		 * not skip the element after, which now becomes the current
		 * element.  This element must be re-tested if doing an
		 * iteration, which is handled by the RETEST flag.
		 */
		if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
			cursor->flags |= HAMMER_CURSOR_RETEST;
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		}

		/*
		 * An on-disk record cannot have the same delete_tid
		 * as its create_tid.  In a chain of record updates
		 * this could result in a duplicate record.
		 */
		KKASSERT(elm->leaf.base.delete_tid !=
			 elm->leaf.base.create_tid);
	}

	/*
	 * Destroy the B-Tree element if asked (typically if a nohistory
	 * file or mount, or when called by the pruning code).
	 *
	 * Adjust the ATEDISK flag to properly support iterations.
	 */
	if (delete_flags & HAMMER_DELETE_DESTROY) {
		data_offset = elm->leaf.data_offset;
		data_len = elm->leaf.data_len;
		if (doprop) {
			save_leaf = elm->leaf;
			leaf = &save_leaf;
		}
		if (elm->base.rec_type == HAMMER_RECTYPE_INODE &&
		    elm->leaf.base.delete_tid == 0) {
			icount = -1;
		}

		error = hammer_btree_delete(cursor);
		if (error == 0) {
			/*
			 * The deletion moves the next element (if any) to
			 * the current element position.  We must clear
			 * ATEDISK so this element is not skipped and we
			 * must set RETEST to force any iteration to re-test
			 * the element.
			 */
			if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
				cursor->flags |= HAMMER_CURSOR_RETEST;
				cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
			}
		}
		if (error == 0) {
			switch(data_offset & HAMMER_OFF_ZONE_MASK) {
			case HAMMER_ZONE_LARGE_DATA:
			case HAMMER_ZONE_SMALL_DATA:
			case HAMMER_ZONE_META:
				hammer_blockmap_free(trans,
						     data_offset, data_len);
				break;
			default:
				break;
			}
		}
	}

	/*
	 * Track inode count and next_tid.  This is used by the mirroring
	 * and PFS code.  icount can be negative, zero, or positive.
	 */
	if (error == 0 && track) {
		if (icount) {
			hammer_modify_volume_field(trans, trans->rootvol,
						   vol0_stat_inodes);
			trans->rootvol->ondisk->vol0_stat_inodes += icount;
			hammer_modify_volume_done(trans->rootvol);
		}
		if (trans->rootvol->ondisk->vol0_next_tid < delete_tid) {
			hammer_modify_volume(trans, trans->rootvol, NULL, 0);
			trans->rootvol->ondisk->vol0_next_tid = delete_tid;
			hammer_modify_volume_done(trans->rootvol);
		}
	}

	/*
	 * mirror_tid propagation occurs if the node's mirror_tid had to be
	 * updated while adjusting the delete_tid.
	 *
	 * This occurs when deleting even in nohistory mode, but does not
	 * occur when pruning an already-deleted node.
	 *
	 * cursor->ip is NULL when called from the pruning, mirroring,
	 * and pfs code.  If non-NULL propagation will be conditionalized
	 * on whether the PFS is in no-history mode or not.
	 *
	 * WARNING: cursor's leaf pointer may have changed after do_propagation
	 *	    returns!
	 */
	if (doprop) {
		if (cursor->ip)
			hammer_btree_do_propagation(cursor, cursor->ip->pfsm, leaf);
		else
			hammer_btree_do_propagation(cursor, NULL, leaf);
	}
	hammer_sync_unlock(trans);
	return (error);
}

/*
 * Determine whether we can remove a directory.  This routine checks whether
 * a directory is empty or not and enforces flush connectivity.
 *
 * Flush connectivity requires that we block if the target directory is
 * currently flushing, otherwise it may not end up in the same flush group.
 *
 * Returns 0 on success, ENOTEMPTY or EDEADLK (or other errors) on failure.
 */
int
hammer_ip_check_directory_empty(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	int error;

	/*
	 * Check directory empty
	 */
	hammer_init_cursor(trans, &cursor, &ip->cache[1], ip);

	cursor.key_beg.localization = ip->obj_localization +
				      hammer_dir_localization(ip);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE + 1;
	cursor.key_beg.key = HAMMER_MIN_KEY;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.rec_type = 0xFFFF;
	cursor.key_end.key = HAMMER_MAX_KEY;

	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	error = hammer_ip_first(&cursor);
	if (error == ENOENT)
		error = 0;
	else if (error == 0)
		error = ENOTEMPTY;
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Localize the data payload.  Directory entries may need their
 * localization adjusted.
 */
static
int
hammer_cursor_localize_data(hammer_data_ondisk_t data,
			    hammer_btree_leaf_elm_t leaf)
{
	u_int32_t localization;

	if (leaf->base.rec_type == HAMMER_RECTYPE_DIRENTRY) {
		localization = leaf->base.localization &
			       HAMMER_LOCALIZE_PSEUDOFS_MASK;
		if (data->entry.localization != localization) {
			data->entry.localization = localization;
			hammer_crc_set_leaf(data, leaf);
		}
	}
	return(0);
}
