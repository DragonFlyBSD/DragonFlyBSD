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
 * $DragonFly: src/sys/vfs/hammer/hammer_object.c,v 1.75 2008/06/24 17:38:17 dillon Exp $
 */

#include "hammer.h"

static int hammer_mem_add(hammer_record_t record);
static int hammer_mem_lookup(hammer_cursor_t cursor);
static int hammer_mem_first(hammer_cursor_t cursor);
static int hammer_rec_trunc_callback(hammer_record_t record,
				void *data __unused);
static int hammer_record_needs_overwrite_delete(hammer_record_t record);

struct rec_trunc_info {
	u_int16_t	rec_type;
	int64_t		trunc_off;
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
	 * Never match against an item deleted by the front-end.
	 *
	 * rec1 is greater then rec2 if rec1 is marked deleted.
	 * rec1 is less then rec2 if rec2 is marked deleted.
	 *
	 * Multiple deleted records may be present, do not return 0
	 * if both are marked deleted.
	 */
	if (rec1->flags & HAMMER_RECF_DELETED_FE)
		return(1);
	if (rec2->flags & HAMMER_RECF_DELETED_FE)
		return(-1);

        return(0);
}

/*
 * Basic record comparison code similar to hammer_btree_cmp().
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
	 * Never match against an item deleted by the front-end.
	 * elm is less then rec if rec is marked deleted.
	 */
	if (rec->flags & HAMMER_RECF_DELETED_FE)
		return(-1);
        return(0);
}

/*
 * Special LOOKUP_INFO to locate an overlapping record.  This used by
 * the reservation code to implement small-block records (whos keys will
 * be different depending on data_len, when representing the same base
 * offset).
 *
 * NOTE: The base file offset of a data record is (key - data_len), not (key).
 */
static int
hammer_rec_overlap_compare(hammer_btree_leaf_elm_t leaf, hammer_record_t rec)
{
	if (leaf->base.rec_type < rec->leaf.base.rec_type)
		return(-3);
	if (leaf->base.rec_type > rec->leaf.base.rec_type)
		return(3);

	/*
	 * Overlap compare
	 */
	if (leaf->base.rec_type == HAMMER_RECTYPE_DATA) {
		/* leaf_end <= rec_beg */
		if (leaf->base.key <= rec->leaf.base.key - rec->leaf.data_len)
			return(-2);
		/* leaf_beg >= rec_end */
		if (leaf->base.key - leaf->data_len >= rec->leaf.base.key)
			return(2);
	} else {
		if (leaf->base.key < rec->leaf.base.key)
			return(-2);
		if (leaf->base.key > rec->leaf.base.key)
			return(2);
	}

	/*
	 * Never match against an item deleted by the front-end.
	 * leaf is less then rec if rec is marked deleted.
	 *
	 * We must still return the proper code for the scan to continue
	 * along the correct branches.
	 */
	if (rec->flags & HAMMER_RECF_DELETED_FE) {
		if (leaf->base.key < rec->leaf.base.key)
			return(-2);
		if (leaf->base.key > rec->leaf.base.key)
			return(2);
		return(-1);
	}
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
RB_GENERATE_XLOOKUP(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
		    hammer_rec_overlap_compare, hammer_btree_leaf_elm_t);

/*
 * Allocate a record for the caller to finish filling in.  The record is
 * returned referenced.
 */
hammer_record_t
hammer_alloc_mem_record(hammer_inode_t ip, int data_len)
{
	hammer_record_t record;

	++hammer_count_records;
	record = kmalloc(sizeof(*record), M_HAMMER, M_WAITOK | M_ZERO);
	record->flush_state = HAMMER_FST_IDLE;
	record->ip = ip;
	record->leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;
	record->leaf.data_len = data_len;
	hammer_ref(&record->lock);

	if (data_len) {
		record->data = kmalloc(data_len, M_HAMMER, M_WAITOK | M_ZERO);
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

	if (error) {
		/*
		 * An error occured, the backend was unable to sync the
		 * record to its media.  Leave the record intact.
		 */
		Debugger("flush_record_done error");
	}

	if (record->flags & HAMMER_RECF_DELETED_BE) {
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
	hammer_inode_t ip, target_ip;

	hammer_unref(&record->lock);

	if (record->lock.refs == 0) {
		/*
		 * Upon release of the last reference wakeup any waiters.
		 * The record structure may get destroyed so callers will
		 * loop up and do a relookup.
		 *
		 * WARNING!  Record must be removed from RB-TREE before we
		 * might possibly block.  hammer_test_inode() can block!
		 */
		ip = record->ip;

		/*
		 * Upon release of the last reference a record marked deleted
		 * is destroyed.
		 */
		if (record->flags & HAMMER_RECF_DELETED_FE) {
			KKASSERT(ip->lock.refs > 0);
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

			if (record->flags & HAMMER_RECF_ONRBTREE) {
				RB_REMOVE(hammer_rec_rb_tree,
					  &record->ip->rec_tree,
					  record);
				KKASSERT(ip->rsv_recs > 0);
				--ip->hmp->rsv_recs;
				--ip->rsv_recs;
				ip->hmp->rsv_databytes -= record->leaf.data_len;
				record->flags &= ~HAMMER_RECF_ONRBTREE;

				if (RB_EMPTY(&record->ip->rec_tree)) {
					record->ip->flags &= ~HAMMER_INODE_XDIRTY;
					record->ip->sync_flags &= ~HAMMER_INODE_XDIRTY;
					hammer_test_inode(record->ip);
				}
			}

			/*
			 * Do this test after removing record from the B-Tree.
			 */
			if (target_ip) {
				hammer_test_inode(target_ip);
				hammer_rel_inode(target_ip, 0);
			}

			if (record->flags & HAMMER_RECF_ALLOCDATA) {
				--hammer_count_record_datas;
				kfree(record->data, M_HAMMER);
				record->flags &= ~HAMMER_RECF_ALLOCDATA;
			}
			if (record->resv) {
				hammer_blockmap_reserve_complete(ip->hmp,
								 record->resv);
				record->resv = NULL;
			}
			record->data = NULL;
			--hammer_count_records;
			kfree(record, M_HAMMER);
		}
	}
}

/*
 * Record visibility depends on whether the record is being accessed by
 * the backend or the frontend.
 *
 * Return non-zero if the record is visible, zero if it isn't or if it is
 * deleted.
 */
static __inline
int
hammer_ip_iterate_mem_good(hammer_cursor_t cursor, hammer_record_t record)
{
	if (cursor->flags & HAMMER_CURSOR_BACKEND) {
		if (record->flags & HAMMER_RECF_DELETED_BE)
			return(0);
	} else {
		if (record->flags & HAMMER_RECF_DELETED_FE)
			return(0);
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
	 * Skip if the record was marked deleted.
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
	 * If the record is queued to the flusher we have to block until
	 * it isn't.  Otherwise we may see duplication between our memory
	 * cache and the media.
	 */
	hammer_ref(&rec->lock);

#warning "This deadlocks"
#if 0
	if (rec->flush_state == HAMMER_FST_FLUSH)
		hammer_wait_mem_record(rec);
#endif

	/*
	 * The record may have been deleted while we were blocked.
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
 */
static
int
hammer_mem_lookup(hammer_cursor_t cursor)
{
	int error;

	KKASSERT(cursor->ip);
	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
	hammer_rec_rb_tree_RB_SCAN(&cursor->ip->rec_tree, hammer_rec_find_cmp,
				   hammer_rec_scan_callback, cursor);

	if (cursor->iprec == NULL)
		error = ENOENT;
	else
		error = 0;
	return(error);
}

/*
 * hammer_mem_first() - locate the first in-memory record matching the
 * cursor within the bounds of the key range.
 */
static
int
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

	/*
	 * Adjust scan.node and keep it linked into the RB-tree so we can
	 * hold the cursor through third party modifications of the RB-tree.
	 */
	if (cursor->iprec)
		return(0);
	return(ENOENT);
}

void
hammer_mem_done(hammer_cursor_t cursor)
{
        if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
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
	hammer_record_t record;
	int error;

	record = hammer_alloc_mem_record(dip, HAMMER_ENTRY_SIZE(bytes));
	if (++trans->hmp->namekey_iterator == 0)
		++trans->hmp->namekey_iterator;

	record->type = HAMMER_MEM_RECORD_ADD;
	record->leaf.base.localization = dip->obj_localization +
					 HAMMER_LOCALIZE_MISC;
	record->leaf.base.obj_id = dip->obj_id;
	record->leaf.base.key = hammer_directory_namekey(name, bytes);
	record->leaf.base.key += trans->hmp->namekey_iterator;
	record->leaf.base.rec_type = HAMMER_RECTYPE_DIRENTRY;
	record->leaf.base.obj_type = ip->ino_leaf.base.obj_type;
	record->data->entry.obj_id = ip->obj_id;
	record->data->entry.localization = ip->obj_localization;
	bcopy(name, record->data->entry.name, bytes);

	++ip->ino_data.nlinks;
	hammer_modify_inode(ip, HAMMER_INODE_DDIRTY);

	/*
	 * The target inode and the directory entry are bound together.
	 */
	record->target_ip = ip;
	record->flush_state = HAMMER_FST_SETUP;
	TAILQ_INSERT_TAIL(&ip->target_list, record, target_entry);

	/*
	 * The inode now has a dependancy and must be taken out of the idle
	 * state.  An inode not in an idle state is given an extra reference.
	 */
	if (ip->flush_state == HAMMER_FST_IDLE) {
		hammer_ref(&ip->lock);
		ip->flush_state = HAMMER_FST_SETUP;
	}
	error = hammer_mem_add(record);
	return(error);
}

/*
 * Delete the directory entry and update the inode link count.  The
 * cursor must be seeked to the directory entry record being deleted.
 *
 * The related inode should be share-locked by the caller.  The caller is
 * on the frontend.
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
		 * Even though the HAMMER_RECF_DELETED_FE flag is ignored
		 * by the backend, we must still avoid races against the
		 * backend potentially syncing the record to the media. 
		 *
		 * We cannot call hammer_ip_delete_record(), that routine may
		 * only be called from the backend.
		 */
		record = cursor->iprec;
		if (record->flags & HAMMER_RECF_INTERLOCK_BE) {
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
		 * record.
		 */
		KKASSERT(dip->flags &
			 (HAMMER_INODE_ONDISK | HAMMER_INODE_DONDISK));
		record = hammer_alloc_mem_record(dip, 0);
		record->type = HAMMER_MEM_RECORD_DEL;
		record->leaf.base = cursor->leaf->base;

		record->target_ip = ip;
		record->flush_state = HAMMER_FST_SETUP;
		TAILQ_INSERT_TAIL(&ip->target_list, record, target_entry);

		/*
		 * The inode now has a dependancy and must be taken out of
		 * the idle state.  An inode not in an idle state is given
		 * an extra reference.
		 */
		if (ip->flush_state == HAMMER_FST_IDLE) {
			hammer_ref(&ip->lock);
			ip->flush_state = HAMMER_FST_SETUP;
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
		--ip->ino_data.nlinks;
		hammer_modify_inode(ip, HAMMER_INODE_DDIRTY);
		if (ip->ino_data.nlinks == 0 &&
		    (ip->vp == NULL || (ip->vp->v_flag & VINACTIVE))) {
			hammer_done_cursor(cursor);
			hammer_inode_unloadable_check(ip, 1);
			hammer_flush_inode(ip, 0);
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
 * Locate a bulk record in-memory.  Bulk records allow disk space to be
 * reserved so the front-end can flush large data writes without having
 * to queue the BIO to the flusher.  Only the related record gets queued
 * to the flusher.
 */
static hammer_record_t
hammer_ip_get_bulk(hammer_inode_t ip, off_t file_offset, int bytes)
{
	hammer_record_t record;
	struct hammer_btree_leaf_elm leaf;

	bzero(&leaf, sizeof(leaf));
	leaf.base.obj_id = ip->obj_id;
	leaf.base.key = file_offset + bytes;
	leaf.base.create_tid = 0;
	leaf.base.delete_tid = 0;
	leaf.base.rec_type = HAMMER_RECTYPE_DATA;
	leaf.base.obj_type = 0;			/* unused */
	leaf.base.btype = HAMMER_BTREE_TYPE_RECORD;	/* unused */
	leaf.base.localization = ip->obj_localization + HAMMER_LOCALIZE_MISC;
	leaf.data_len = bytes;

	record = hammer_rec_rb_tree_RB_LOOKUP_INFO(&ip->rec_tree, &leaf);
	if (record)
		hammer_ref(&record->lock);
	return(record);
}

/*
 * Reserve blockmap space placemarked with an in-memory record.  
 *
 * This routine is called by the frontend in order to be able to directly
 * flush a buffer cache buffer.  The frontend has locked the related buffer
 * cache buffers and we should be able to manipulate any overlapping
 * in-memory records.
 */
hammer_record_t
hammer_ip_add_bulk(hammer_inode_t ip, off_t file_offset, void *data, int bytes,
		   int *errorp)
{
	hammer_record_t record;
	hammer_record_t conflict;
	int zone;
	int flags;

	/*
	 * Deal with conflicting in-memory records.  We cannot have multiple
	 * in-memory records for the same offset without seriously confusing
	 * the backend, including but not limited to the backend issuing
	 * delete-create-delete sequences and asserting on the delete_tid
	 * being the same as the create_tid.
	 *
	 * If we encounter a record with the backend interlock set we cannot
	 * immediately delete it without confusing the backend.
	 */
	while ((conflict = hammer_ip_get_bulk(ip, file_offset, bytes)) !=NULL) {
		if (conflict->flags & HAMMER_RECF_INTERLOCK_BE) {
			conflict->flags |= HAMMER_RECF_WANTED;
			tsleep(conflict, 0, "hmrrc3", 0);
		} else {
			conflict->flags |= HAMMER_RECF_DELETED_FE;
		}
		hammer_rel_mem_record(conflict);
	}

	/*
	 * Create a record to cover the direct write.  This is called with
	 * the related BIO locked so there should be no possible conflict.
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
	record->resv = hammer_blockmap_reserve(ip->hmp, zone, bytes,
					       &record->leaf.data_offset,
					       errorp);
	if (record->resv == NULL) {
		kprintf("hammer_ip_add_bulk: reservation failed\n");
		hammer_rel_mem_record(record);
		return(NULL);
	}
	record->type = HAMMER_MEM_RECORD_DATA;
	record->leaf.base.rec_type = HAMMER_RECTYPE_DATA;
	record->leaf.base.obj_type = ip->ino_leaf.base.obj_type;
	record->leaf.base.obj_id = ip->obj_id;
	record->leaf.base.key = file_offset + bytes;
	record->leaf.base.localization = ip->obj_localization +
					 HAMMER_LOCALIZE_MISC;
	record->leaf.data_len = bytes;
	hammer_crc_set_leaf(data, &record->leaf);
	flags = record->flags;

	hammer_ref(&record->lock);	/* mem_add eats a reference */
	*errorp = hammer_mem_add(record);
	if (*errorp) {
		conflict = hammer_ip_get_bulk(ip, file_offset, bytes);
		kprintf("hammer_ip_add_bulk: error %d conflict %p file_offset %lld bytes %d\n",
			*errorp, conflict, file_offset, bytes);
		if (conflict)
			kprintf("conflict %lld %d\n", conflict->leaf.base.key, conflict->leaf.data_len);
		if (conflict)
			hammer_rel_mem_record(conflict);
	}
	KKASSERT(*errorp == 0);
	conflict = hammer_ip_get_bulk(ip, file_offset, bytes);
	if (conflict != record) {
		kprintf("conflict mismatch %p %p %08x\n", conflict, record, record->flags);
		if (conflict)
		    kprintf("conflict mismatch %lld/%d %lld/%d\n", conflict->leaf.base.key, conflict->leaf.data_len, record->leaf.base.key, record->leaf.data_len);
	}
	KKASSERT(conflict == record);
	hammer_rel_mem_record(conflict);

	return (record);
}

/*
 * Frontend truncation code.  Scan in-memory records only.  On-disk records
 * and records in a flushing state are handled by the backend.  The vnops
 * setattr code will handle the block containing the truncation point.
 *
 * Partial blocks are not deleted.
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
				   hammer_rec_trunc_callback, &info);
	return(0);
}

static int
hammer_rec_trunc_callback(hammer_record_t record, void *data __unused)
{
	if (record->flags & HAMMER_RECF_DELETED_FE)
		return(0);
	if (record->flush_state == HAMMER_FST_FLUSH)
		return(0);
	KKASSERT((record->flags & HAMMER_RECF_INTERLOCK_BE) == 0);
	hammer_ref(&record->lock);
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
	r = (file_offset < ip->sync_trunc_off);
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		if (ip->sync_trunc_off <= record->leaf.base.key)
			ip->sync_trunc_off = record->leaf.base.key + 1;
	} else {
		if (ip->sync_trunc_off < record->leaf.base.key)
			ip->sync_trunc_off = record->leaf.base.key;
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

	KKASSERT(record->flush_state == HAMMER_FST_FLUSH);
	KKASSERT(record->flags & HAMMER_RECF_INTERLOCK_BE);
	KKASSERT(record->leaf.base.localization != 0);

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
			error = hammer_ip_delete_record(cursor, record->ip,
							trans->tid);
			if (error == 0) {
				record->flags |= HAMMER_RECF_DELETED_FE;
				record->flags |= HAMMER_RECF_DELETED_BE;
			}
		}
		goto done;
	}

	/*
	 * We are inserting.
	 *
	 * Issue a lookup to position the cursor and locate the cluster.  The
	 * target key should not exist.  If we are creating a directory entry
	 * we may have to iterate the low 32 bits of the key to find an unused
	 * key.
	 */
	cursor->flags |= HAMMER_CURSOR_INSERT;

	for (;;) {
		error = hammer_btree_lookup(cursor);
		if (hammer_debug_inode)
			kprintf("DOINSERT LOOKUP %d\n", error);
		if (error)
			break;
		if (record->leaf.base.rec_type != HAMMER_RECTYPE_DIRENTRY) {
			kprintf("hammer_ip_sync_record: duplicate rec "
				"at (%016llx)\n", record->leaf.base.key);
			Debugger("duplicate record1");
			error = EIO;
			break;
		}
		if (++trans->hmp->namekey_iterator == 0)
			++trans->hmp->namekey_iterator;
		record->leaf.base.key &= ~(0xFFFFFFFFLL);
		record->leaf.base.key |= trans->hmp->namekey_iterator;
		cursor->key_beg.key = record->leaf.base.key;
	}
#if 0
	if (record->type == HAMMER_MEM_RECORD_DATA)
		kprintf("sync_record  %016llx ---------------- %016llx %d\n",
			record->leaf.base.key - record->leaf.data_len,
			record->leaf.data_offset, error);
#endif
			

	if (error != ENOENT)
		goto done;

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
		hammer_blockmap_finalize(trans, record->leaf.data_offset,
					 record->leaf.data_len);
		error = 0;
	} else if (record->data && record->leaf.data_len) {
		/*
		 * Wholely cached record, with data.  Allocate the data.
		 */
		bdata = hammer_alloc_data(trans, record->leaf.data_len,
					  record->leaf.base.rec_type,
					  &record->leaf.data_offset,
					  &cursor->data_buffer, &error);
		if (bdata == NULL)
			goto done;
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

	error = hammer_btree_insert(cursor, &record->leaf);
	if (hammer_debug_inode && error)
		kprintf("BTREE INSERT error %d @ %016llx:%d key %016llx\n", error, cursor->node->node_offset, cursor->index, record->leaf.base.key);

	/*
	 * Our record is on-disk, normally mark the in-memory version as
	 * deleted.  If the record represented a directory deletion but
	 * we had to sync a valid directory entry to disk we must convert
	 * the record to a covering delete so the frontend does not have
	 * visibility on the synced entry.
	 */
	if (error == 0) {
		if (record->flags & HAMMER_RECF_CONVERT_DELETE) {
			KKASSERT(record->type == HAMMER_MEM_RECORD_ADD);
			record->flags &= ~HAMMER_RECF_DELETED_FE;
			record->type = HAMMER_MEM_RECORD_DEL;
			KKASSERT(record->flush_state == HAMMER_FST_FLUSH);
			record->flags &= ~HAMMER_RECF_CONVERT_DELETE;
			/* hammer_flush_record_done takes care of the rest */
		} else {
			record->flags |= HAMMER_RECF_DELETED_FE;
			record->flags |= HAMMER_RECF_DELETED_BE;
		}
	} else {
		if (record->leaf.data_offset) {
			hammer_blockmap_free(trans, record->leaf.data_offset,
					     record->leaf.data_len);
		}
	}

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
static
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
	 * Insert into the RB tree, find an unused iterator if this is
	 * a directory entry.
	 */
	while (RB_INSERT(hammer_rec_rb_tree, &record->ip->rec_tree, record)) {
		if (record->leaf.base.rec_type != HAMMER_RECTYPE_DIRENTRY){
			record->flags |= HAMMER_RECF_DELETED_FE;
			hammer_rel_mem_record(record);
			return (EEXIST);
		}
		if (++hmp->namekey_iterator == 0)
			++hmp->namekey_iterator;
		record->leaf.base.key &= ~(0xFFFFFFFFLL);
		record->leaf.base.key |= hmp->namekey_iterator;
	}
	++hmp->count_newrecords;
	++hmp->rsv_recs;
	++record->ip->rsv_recs;
	record->ip->hmp->rsv_databytes += record->leaf.data_len;
	record->flags |= HAMMER_RECF_ONRBTREE;
	hammer_modify_inode(record->ip, HAMMER_INODE_XDIRTY);
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
	hammer_inode_t ip = cursor->ip;
	int error;

	KKASSERT(ip != NULL);

	/*
	 * Clean up fields and setup for merged scan
	 */
	cursor->flags &= ~HAMMER_CURSOR_DELBTREE;
	cursor->flags |= HAMMER_CURSOR_ATEDISK | HAMMER_CURSOR_ATEMEM;
	cursor->flags |= HAMMER_CURSOR_DISKEOF | HAMMER_CURSOR_MEMEOF;
	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}

	/*
	 * Search the on-disk B-Tree.  hammer_btree_lookup() only does an
	 * exact lookup so if we get ENOENT we have to call the iterate
	 * function to validate the first record after the begin key.
	 *
	 * The ATEDISK flag is used by hammer_btree_iterate to determine
	 * whether it must index forwards or not.  It is also used here
	 * to select the next record from in-memory or on-disk.
	 *
	 * EDEADLK can only occur if the lookup hit an empty internal
	 * element and couldn't delete it.  Since this could only occur
	 * in-range, we can just iterate from the failure point.
	 */
	if (ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DONDISK)) {
		error = hammer_btree_lookup(cursor);
		if (error == ENOENT || error == EDEADLK) {
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
			if (hammer_debug_general & 0x2000)
				kprintf("error %d node %p %016llx index %d\n", error, cursor->node, cursor->node->node_offset, cursor->index);
			error = hammer_btree_iterate(cursor);
		}
		if (error && error != ENOENT) 
			return(error);
		if (error == 0) {
			cursor->flags &= ~HAMMER_CURSOR_DISKEOF;
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		} else {
			cursor->flags |= HAMMER_CURSOR_ATEDISK;
		}
	}

	/*
	 * Search the in-memory record list (Red-Black tree).  Unlike the
	 * B-Tree search, mem_first checks for records in the range.
	 */
	error = hammer_mem_first(cursor);
	if (error && error != ENOENT)
		return(error);
	if (error == 0) {
		cursor->flags &= ~HAMMER_CURSOR_MEMEOF;
		cursor->flags &= ~HAMMER_CURSOR_ATEMEM;
		if (hammer_ip_iterate_mem_good(cursor, cursor->iprec) == 0)
			cursor->flags |= HAMMER_CURSOR_ATEMEM;
	}

	/*
	 * This will return the first matching record.
	 */
	return(hammer_ip_next(cursor));
}

/*
 * Retrieve the next record in a merged iteration within the bounds of the
 * cursor.  This call may be made multiple times after the cursor has been
 * initially searched with hammer_ip_first().
 *
 * 0 is returned on success, ENOENT if no further records match the
 * requested range, or some other error code is returned.
 */
int
hammer_ip_next(hammer_cursor_t cursor)
{
	hammer_btree_elm_t elm;
	hammer_record_t rec, save;
	int error;
	int r;

next_btree:
	/*
	 * Load the current on-disk and in-memory record.  If we ate any
	 * records we have to get the next one. 
	 *
	 * If we deleted the last on-disk record we had scanned ATEDISK will
	 * be clear and DELBTREE will be set, forcing a call to iterate. The
	 * fact that ATEDISK is clear causes iterate to re-test the 'current'
	 * element.  If ATEDISK is set, iterate will skip the 'current'
	 * element.
	 *
	 * Get the next on-disk record
	 */
	if (cursor->flags & (HAMMER_CURSOR_ATEDISK|HAMMER_CURSOR_DELBTREE)) {
		if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
			error = hammer_btree_iterate(cursor);
			cursor->flags &= ~HAMMER_CURSOR_DELBTREE;
			if (error == 0) {
				cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
				hammer_cache_node(&cursor->ip->cache[1],
						  cursor->node);
			} else {
				cursor->flags |= HAMMER_CURSOR_DISKEOF |
						 HAMMER_CURSOR_ATEDISK;
			}
		}
	}

next_memory:
	/*
	 * Get the next in-memory record.  The record can be ripped out
	 * of the RB tree so we maintain a scan_info structure to track
	 * the next node.
	 *
	 * hammer_rec_scan_cmp:  Is the record still in our general range,
	 *			 (non-inclusive of snapshot exclusions)?
	 * hammer_rec_scan_callback: Is the record in our snapshot?
	 */
	if (cursor->flags & HAMMER_CURSOR_ATEMEM) {
		if ((cursor->flags & HAMMER_CURSOR_MEMEOF) == 0) {
			save = cursor->iprec;
			cursor->iprec = NULL;
			rec = save ? hammer_rec_rb_tree_RB_NEXT(save) : NULL;
			while (rec) {
				if (hammer_rec_scan_cmp(rec, cursor) != 0)
					break;
				if (hammer_rec_scan_callback(rec, cursor) != 0)
					break;
				rec = hammer_rec_rb_tree_RB_NEXT(rec);
			}
			if (save)
				hammer_rel_mem_record(save);
			if (cursor->iprec) {
				KKASSERT(cursor->iprec == rec);
				cursor->flags &= ~HAMMER_CURSOR_ATEMEM;
			} else {
				cursor->flags |= HAMMER_CURSOR_MEMEOF;
			}
		}
	}

	/*
	 * The memory record may have become stale while being held in
	 * cursor->iprec.  We are interlocked against the backend on 
	 * with regards to B-Tree entries.
	 */
	if ((cursor->flags & HAMMER_CURSOR_ATEMEM) == 0) {
		if (hammer_ip_iterate_mem_good(cursor, cursor->iprec) == 0) {
			cursor->flags |= HAMMER_CURSOR_ATEMEM;
			goto next_memory;
		}
	}

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
					goto next_btree;
				}
			} else if (cursor->iprec->type == HAMMER_MEM_RECORD_DATA) {
				if ((cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY) == 0) {
					cursor->flags |= HAMMER_CURSOR_ATEDISK;
				}
				/* fall through to memory entry */
			} else {
				panic("hammer_ip_next: duplicate mem/b-tree entry");
				cursor->flags |= HAMMER_CURSOR_ATEMEM;
				goto next_memory;
			}
		}
		/* fall through to the memory entry */
	case HAMMER_CURSOR_ATEDISK:
		/*
		 * Only the memory entry is valid.
		 */
		cursor->leaf = &cursor->iprec->leaf;
		cursor->flags |= HAMMER_CURSOR_ATEMEM;

		/*
		 * If the memory entry is an on-disk deletion we should have
		 * also had found a B-Tree record.  If the backend beat us
		 * to it it would have interlocked the cursor and we should
		 * have seen the in-memory record marked DELETED_FE.
		 */
		if (cursor->iprec->type == HAMMER_MEM_RECORD_DEL &&
		    (cursor->flags & HAMMER_CURSOR_DELETE_VISIBILITY) == 0) {
			panic("hammer_ip_next: del-on-disk with no b-tree entry");
		}
		break;
	case HAMMER_CURSOR_ATEMEM:
		/*
		 * Only the disk entry is valid
		 */
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_LEAF);
		cursor->flags |= HAMMER_CURSOR_ATEDISK;
		break;
	default:
		/*
		 * Neither entry is valid
		 *
		 * XXX error not set properly
		 */
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
 * NOTE: An unaligned range will cause new records to be added to cover
 * the edge cases. (XXX not implemented yet).
 *
 * NOTE: Replacement via reservations (see hammer_ip_sync_record_cursor())
 * also do not deal with unaligned ranges.
 *
 * NOTE: ran_end is inclusive (e.g. 0,1023 instead of 0,1024).
 *
 * NOTE: Record keys for regular file data have to be special-cased since
 * they indicate the end of the range (key = base + bytes).
 */
int
hammer_ip_delete_range(hammer_cursor_t cursor, hammer_inode_t ip,
		       int64_t ran_beg, int64_t ran_end, int truncating)
{
	hammer_transaction_t trans = cursor->trans;
	hammer_btree_leaf_elm_t leaf;
	int error;
	int64_t off;

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
	cursor->asof = ip->obj_asof;
	cursor->flags &= ~HAMMER_CURSOR_INITMASK;
	cursor->flags |= HAMMER_CURSOR_ASOF;
	cursor->flags |= HAMMER_CURSOR_DELETE_VISIBILITY;
	cursor->flags |= HAMMER_CURSOR_BACKEND;

	cursor->key_end = cursor->key_beg;
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor->key_beg.key = ran_beg;
		cursor->key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor->key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor->key_end.key = ran_end;
	} else {
		/*
		 * The key in the B-Tree is (base+bytes), so the first possible
		 * matching key is ran_beg + 1.
		 */
		int64_t tmp64;

		cursor->key_beg.key = ran_beg + 1;
		cursor->key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor->key_end.rec_type = HAMMER_RECTYPE_DATA;

		tmp64 = ran_end + MAXPHYS + 1;	/* work around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor->key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor->key_end.key = ran_end + MAXPHYS + 1;
	}
	cursor->flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(cursor);

	/*
	 * Iterate through matching records and mark them as deleted.
	 */
	while (error == 0) {
		leaf = cursor->leaf;

		KKASSERT(leaf->base.delete_tid == 0);

		/*
		 * There may be overlap cases for regular file data.  Also
		 * remember the key for a regular file record is (base + len),
		 * NOT (base).
		 */
		if (leaf->base.rec_type == HAMMER_RECTYPE_DATA) {
			off = leaf->base.key - leaf->data_len;
			/*
			 * Check the left edge case.  We currently do not
			 * split existing records.
			 */
			if (off < ran_beg) {
				panic("hammer left edge case %016llx %d\n",
					leaf->base.key, leaf->data_len);
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
				panic("hammer right edge case\n");
			}
		}

		/*
		 * Delete the record.  When truncating we do not delete
		 * in-memory (data) records because they represent data
		 * written after the truncation.
		 *
		 * This will also physically destroy the B-Tree entry and
		 * data if the retention policy dictates.  The function
		 * will set HAMMER_CURSOR_DELBTREE which hammer_ip_next()
		 * uses to perform a fixup.
		 */
		if (truncating == 0 || hammer_cursor_ondisk(cursor))
			error = hammer_ip_delete_record(cursor, ip, trans->tid);
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
 * Backend truncation - delete all records.
 *
 * Delete all user records associated with an inode except the inode record
 * itself.  Directory entries are not deleted (they must be properly disposed
 * of or nlinks would get upset).
 */
int
hammer_ip_delete_range_all(hammer_cursor_t cursor, hammer_inode_t ip,
			   int *countp)
{
	hammer_transaction_t trans = cursor->trans;
	hammer_btree_leaf_elm_t leaf;
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
	cursor->key_beg.rec_type = HAMMER_RECTYPE_INODE + 1;
	cursor->key_beg.key = HAMMER_MIN_KEY;

	cursor->key_end = cursor->key_beg;
	cursor->key_end.rec_type = 0xFFFF;
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
		 * will set HAMMER_CURSOR_DELBTREE which hammer_ip_next()
		 * uses to perform a fixup.
		 *
		 * Directory entries (and delete-on-disk directory entries)
		 * must be synced and cannot be deleted.
		 */
		if (leaf->base.rec_type != HAMMER_RECTYPE_DIRENTRY) {
			error = hammer_ip_delete_record(cursor, ip, trans->tid);
			++*countp;
		}
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
	hammer_off_t zone2_offset;
	hammer_record_t iprec;
	hammer_btree_elm_t elm;
	hammer_mount_t hmp;
	int error;
	int dodelete;

	KKASSERT(cursor->flags & HAMMER_CURSOR_BACKEND);
	KKASSERT(tid != 0);
	hmp = cursor->node->hmp;

	/*
	 * In-memory (unsynchronized) records can simply be freed.  This
	 * only occurs in range iterations since all other records are
	 * individually synchronized.  Thus there should be no confusion with
	 * the interlock.
	 *
	 * An in-memory record may be deleted before being committed to disk,
	 * but could have been accessed in the mean time.  The backing store
	 * may never been marked allocated and so hammer_blockmap_free() may
	 * never get called on it.  Because of this we have to make sure that
	 * we've gotten rid of any related hammer_buffer or buffer cache
 	 * buffer.
	 */
	if (hammer_cursor_inmem(cursor)) {
		iprec = cursor->iprec;
		KKASSERT((iprec->flags & HAMMER_RECF_INTERLOCK_BE) ==0);
		iprec->flags |= HAMMER_RECF_DELETED_FE;
		iprec->flags |= HAMMER_RECF_DELETED_BE;

		if (iprec->leaf.data_offset && iprec->leaf.data_len) {
			zone2_offset = hammer_blockmap_lookup(hmp, iprec->leaf.data_offset, &error);
			KKASSERT(error == 0);
			hammer_del_buffers(hmp,
					   iprec->leaf.data_offset,
					   zone2_offset,
					   iprec->leaf.data_len);
		}
		return(0);
	}

	/*
	 * On-disk records are marked as deleted by updating their delete_tid.
	 * This does not effect their position in the B-Tree (which is based
	 * on their create_tid).
	 */
	error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_LEAF);
	elm = NULL;

	/*
	 * If we were mounted with the nohistory option, we physically
	 * delete the record.
	 */
	dodelete = hammer_nohistory(ip);

	if (error == 0) {
		error = hammer_cursor_upgrade(cursor);
		if (error == 0) {
			elm = &cursor->node->ondisk->elms[cursor->index];
			hammer_modify_node(cursor->trans, cursor->node,
					   elm, sizeof(*elm));
			elm->leaf.base.delete_tid = tid;
			elm->leaf.delete_ts = cursor->trans->time32;
			hammer_modify_node_done(cursor->node);

			/*
			 * An on-disk record cannot have the same delete_tid
			 * as its create_tid.  In a chain of record updates
			 * this could result in a duplicate record.
			 */
			KKASSERT(elm->leaf.base.delete_tid != elm->leaf.base.create_tid);
		}
	}

	if (error == 0 && dodelete) {
		error = hammer_delete_at_cursor(cursor, NULL);
		if (error) {
			panic("hammer_ip_delete_record: unable to physically delete the record!\n");
			error = 0;
		}
	}
	return(error);
}

int
hammer_delete_at_cursor(hammer_cursor_t cursor, int64_t *stat_bytes)
{
	hammer_btree_elm_t elm;
	hammer_off_t data_offset;
	int32_t data_len;
	u_int16_t rec_type;
	int error;

	elm = &cursor->node->ondisk->elms[cursor->index];
	KKASSERT(elm->base.btype == HAMMER_BTREE_TYPE_RECORD);

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	rec_type = elm->leaf.base.rec_type;

	error = hammer_btree_delete(cursor);
	if (error == 0) {
		/*
		 * This forces a fixup for the iteration because
		 * the cursor is now either sitting at the 'next'
		 * element or sitting at the end of a leaf.
		 */
		if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
			cursor->flags |= HAMMER_CURSOR_DELBTREE;
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		}
	}
	if (error == 0) {
		switch(data_offset & HAMMER_OFF_ZONE_MASK) {
		case HAMMER_ZONE_LARGE_DATA:
		case HAMMER_ZONE_SMALL_DATA:
		case HAMMER_ZONE_META:
			hammer_blockmap_free(cursor->trans,
					     data_offset, data_len);
			break;
		default:
			break;
		}
	}
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
				      HAMMER_LOCALIZE_MISC;
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

