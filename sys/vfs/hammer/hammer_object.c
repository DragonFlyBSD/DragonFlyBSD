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
 * $DragonFly: src/sys/vfs/hammer/hammer_object.c,v 1.6 2007/11/26 21:38:37 dillon Exp $
 */

#include "hammer.h"

static int hammer_mem_add(hammer_transaction_t trans,
			     hammer_record_t record);
static int hammer_mem_lookup(hammer_cursor_t cursor, hammer_inode_t ip);
static int hammer_mem_search(hammer_cursor_t cursor, hammer_inode_t ip);

/*
 * Red-black tree support.
 */
static int
hammer_rec_rb_compare(hammer_record_t rec1, hammer_record_t rec2)
{
	if (rec1->rec.base.base.rec_type < rec2->rec.base.base.rec_type)
		return(-1);
	if (rec1->rec.base.base.rec_type > rec2->rec.base.base.rec_type)
		return(1);

	if (rec1->rec.base.base.key < rec2->rec.base.base.key)
		return(-1);
	if (rec1->rec.base.base.key > rec2->rec.base.base.key)
		return(1);

	if (rec1->rec.base.base.create_tid < rec2->rec.base.base.create_tid)
		return(-1);
	if (rec1->rec.base.base.create_tid > rec2->rec.base.base.create_tid)
		return(1);
        return(0);
}

static int
hammer_rec_compare(hammer_base_elm_t info, hammer_record_t rec)
{
        /*
         * A key1->rec_type of 0 matches any record type.
         */
        if (info->rec_type) {
                if (info->rec_type < rec->rec.base.base.rec_type)
                        return(-3);
                if (info->rec_type > rec->rec.base.base.rec_type)
                        return(3);
        }

        /*
         * There is no special case for key.  0 means 0.
         */
        if (info->key < rec->rec.base.base.key)
                return(-2);
        if (info->key > rec->rec.base.base.key)
                return(2);

        /*
         * This test has a number of special cases.  create_tid in key1 is
         * the as-of transction id, and delete_tid in key1 is NOT USED.
         *
         * A key1->create_tid of 0 matches any record regardles of when
         * it was created or destroyed.  0xFFFFFFFFFFFFFFFFULL should be
         * used to search for the most current state of the object.
         *
         * key2->create_tid is a HAMMER record and will never be
         * 0.   key2->delete_tid is the deletion transaction id or 0 if
         * the record has not yet been deleted.
         */
        if (info->create_tid) {
                if (info->create_tid < rec->rec.base.base.create_tid)
                        return(-1);
                if (rec->rec.base.base.delete_tid &&
		    info->create_tid >= rec->rec.base.base.delete_tid) {
                        return(1);
		}
        }
        return(0);
}

/*
 * RB_SCAN comparison code for hammer_mem_search().  The argument order
 * is reversed so the comparison result has to be negated.  key_beg and
 * key_end are both inclusive boundaries.
 */
static
int
hammer_rec_scan_cmp(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;
	int r;

	r = hammer_rec_compare(&cursor->key_beg, rec);
	if (r > 0)
		return(-1);
	if (r == 0)
		return(0);
	r = hammer_rec_compare(&cursor->key_end, rec);
	if (r <= 0)
		return(1);
	return(0);
}

RB_GENERATE(hammer_rec_rb_tree, hammer_record, rb_node, hammer_rec_rb_compare);
RB_GENERATE_XLOOKUP(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
		    hammer_rec_compare, hammer_base_elm_t);

/*
 * Allocate a record for the caller to finish filling in
 */
hammer_record_t
hammer_alloc_mem_record(struct hammer_transaction *trans, hammer_inode_t ip)
{
	hammer_record_t record;

	record = kmalloc(sizeof(*record), M_HAMMER, M_WAITOK|M_ZERO);
	record->ip = ip;
	return (record);
}

/*
 * Release a memory record.  If the record is marked for defered deletion,
 * destroy the record when the last reference goes away.
 */
void
hammer_rel_mem_record(struct hammer_record **recordp)
{
	hammer_record_t rec;

	if ((rec = *recordp) != NULL) {
		if (hammer_islastref(&rec->lock)) {
			hammer_unref(&rec->lock);
			if (rec->flags & HAMMER_RECF_DELETED)
				hammer_free_mem_record(rec);
		} else {
			hammer_unref(&rec->lock);
		}
		*recordp = NULL;
	}
}

/*
 * Free a record.  Clean the structure up even though we are throwing it
 * away as a sanity check.  The actual free operation is delayed while
 * the record is referenced.  However, the record is removed from the RB
 * tree immediately.
 */
void
hammer_free_mem_record(hammer_record_t record)
{
	if (record->flags & HAMMER_RECF_ONRBTREE) {
		RB_REMOVE(hammer_rec_rb_tree, &record->ip->rec_tree, record);
		record->flags &= ~HAMMER_RECF_ONRBTREE;
	}
	if (record->lock.refs) {
		record->flags |= HAMMER_RECF_DELETED;
		return;
	}
	if (record->flags & HAMMER_RECF_ALLOCDATA) {
		kfree(record->data, M_HAMMER);
		record->flags &= ~HAMMER_RECF_ALLOCDATA;
	}
	record->data = NULL;
	kfree(record, M_HAMMER);
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
hammer_mem_lookup(hammer_cursor_t cursor, hammer_inode_t ip)
{
	int error;

	if (cursor->iprec)
		hammer_rel_mem_record(&cursor->iprec);
	if (cursor->ip) {
		hammer_rec_rb_tree_scan_info_done(&cursor->scan,
						  &cursor->ip->rec_tree);
	}
	cursor->ip = ip;
	hammer_rec_rb_tree_scan_info_link(&cursor->scan, &ip->rec_tree);
	cursor->scan.node = NULL;
	cursor->iprec = hammer_rec_rb_tree_RB_LOOKUP_INFO(
				&ip->rec_tree, &cursor->key_beg);
	if (cursor->iprec == NULL) {
		error = ENOENT;
	} else {
		hammer_ref(&cursor->iprec->lock);
		error = 0;
	}
	return(error);
}

/*
 * hammer_mem_search() - locate the first in-memory record matching the
 * cursor.
 *
 * The RB_SCAN function we use is designed as a callback.  We terminate it
 * (return -1) as soon as we get a match.
 */
static
int
hammer_rec_scan_callback(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;

	if (cursor->iprec == NULL) {
		cursor->iprec = rec;
		hammer_ref(&rec->lock);
		return(-1);
	}
	return(0);
}

static
int
hammer_mem_search(hammer_cursor_t cursor, hammer_inode_t ip)
{
	if (cursor->iprec)
		hammer_rel_mem_record(&cursor->iprec);
	if (cursor->ip) {
		hammer_rec_rb_tree_scan_info_done(&cursor->scan,
						  &cursor->ip->rec_tree);
	}
	cursor->ip = ip;
	hammer_rec_rb_tree_scan_info_link(&cursor->scan, &ip->rec_tree);
	cursor->scan.node = NULL;
	hammer_rec_rb_tree_RB_SCAN(&ip->rec_tree, hammer_rec_scan_cmp,
				   hammer_rec_scan_callback, cursor);
	if (cursor->iprec) {
		cursor->scan.node = hammer_rec_rb_tree_RB_NEXT(cursor->iprec);
		return(0);
	}
	return(ENOENT);
}

void
hammer_mem_done(hammer_cursor_t cursor)
{
	if (cursor->ip) {
		hammer_rec_rb_tree_scan_info_done(&cursor->scan,
						  &cursor->ip->rec_tree);
		cursor->ip = NULL;
	}
        if (cursor->iprec)
		hammer_rel_mem_record(&cursor->iprec);
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
 */
int
hammer_ip_add_directory(struct hammer_transaction *trans,
		     struct hammer_inode *dip, struct namecache *ncp,
		     struct hammer_inode *ip)
{
	hammer_record_t record;
	int error;
	int bytes;

	record = hammer_alloc_mem_record(trans, dip);

	bytes = ncp->nc_nlen;	/* NOTE: terminating \0 is NOT included */
	if (++trans->hmp->namekey_iterator == 0)
		++trans->hmp->namekey_iterator;

	record->rec.entry.base.base.obj_id = dip->obj_id;
	record->rec.entry.base.base.key =
		hammer_directory_namekey(ncp->nc_name, bytes);
	record->rec.entry.base.base.key += trans->hmp->namekey_iterator;
	record->rec.entry.base.base.create_tid = trans->tid;
	record->rec.entry.base.base.rec_type = HAMMER_RECTYPE_DIRENTRY;
	record->rec.entry.base.base.obj_type = ip->ino_rec.base.base.obj_type;
	record->rec.entry.obj_id = ip->obj_id;
	if (bytes <= sizeof(record->rec.entry.den_name)) {
		record->data = (void *)record->rec.entry.den_name;
		record->flags |= HAMMER_RECF_EMBEDDED_DATA;
	} else {
		record->data = kmalloc(bytes, M_HAMMER, M_WAITOK);
		record->flags |= HAMMER_RECF_ALLOCDATA;
	}
	bcopy(ncp->nc_name, record->data, bytes);
	record->rec.entry.base.data_len = bytes;
	++ip->ino_rec.ino_nlinks;
	hammer_modify_inode(trans, ip,
			    HAMMER_INODE_RDIRTY | HAMMER_INODE_TID);
	error = hammer_mem_add(trans, record);
	return(error);
}

/*
 * Delete the directory entry and update the inode link count.  The
 * cursor must be seeked to the directory entry record being deleted.
 *
 * NOTE: HAMMER_CURSOR_DELETE may not have been set.  XXX remove flag.
 */
int
hammer_ip_del_directory(struct hammer_transaction *trans,
		     hammer_cursor_t cursor, struct hammer_inode *dip,
		     struct hammer_inode *ip)
{
	int error;

	if (cursor->record == &cursor->iprec->rec) {
		/*
		 * The directory entry was in-memory, just scrap the
		 * record.
		 */
		kprintf("del directory mem record\n");
		hammer_free_mem_record(cursor->iprec);
		error = 0;
	} else {
		/*
		 * The directory entry was on-disk, mark the record and
		 * B-Tree entry as deleted.  The B-Tree entry does not
		 * have to be reindexed because a 'current' delete transid
		 * will wind up in the same position as the live record.
		 */
		KKASSERT(ip->flags & HAMMER_INODE_ONDISK);
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_RECORD);
		if (error == 0) {
			cursor->node->ondisk->elms[cursor->index].base.delete_tid = trans->tid;
			cursor->record->base.base.delete_tid = trans->tid;
			hammer_modify_node(cursor->node);
			hammer_modify_buffer(cursor->record_buffer);
		}
	}

	/*
	 * One less link.  The file may still be open in the OS even after
	 * all links have gone away so we don't destroy the inode's data
	 * here.
	 */
	if (error == 0) {
		--ip->ino_rec.ino_nlinks;
		hammer_modify_inode(trans, ip,
				    HAMMER_INODE_RDIRTY | HAMMER_INODE_TID);
		if (ip->vp == NULL || (ip->vp->v_flag & VINACTIVE))
			hammer_sync_inode(ip, MNT_NOWAIT, 1);

	}
	return(error);
}

/*
 * Sync data from a buffer cache buffer (typically) to the filesystem.  This
 * is called via the strategy called from a cached data source.  This code
 * is responsible for actually writing a data record out to the disk.
 */
int
hammer_ip_sync_data(hammer_transaction_t trans, hammer_inode_t ip,
		       int64_t offset, void *data, int bytes)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	union hammer_btree_elm elm;
	void *bdata;
	int error;

	error = hammer_init_cursor_ip(&cursor, ip);
	if (error)
		return(error);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.key = offset + bytes;
	cursor.key_beg.create_tid = trans->tid;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
	cursor.flags = HAMMER_CURSOR_INSERT;

	/*
	 * Issue a lookup to position the cursor and locate the cluster
	 */
	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		kprintf("hammer_ip_sync_data: duplicate data at (%lld,%d)\n",
			offset, bytes);
		hammer_print_btree_elm(&cursor.node->ondisk->elms[cursor.index],
				       HAMMER_BTREE_TYPE_LEAF, cursor.index);
		error = EIO;
	}
	if (error != ENOENT)
		goto done;

	/*
	 * Allocate record and data space now that we know which cluster
	 * the B-Tree node ended up in.
	 */
	bdata = hammer_alloc_data(cursor.node->cluster, bytes, &error,
				  &cursor.data_buffer);
	if (bdata == NULL)
		goto done;
	rec = hammer_alloc_record(cursor.node->cluster, &error,
				  &cursor.record_buffer);
	if (rec == NULL)
		goto fail1;

	/*
	 * Fill everything in and insert our B-Tree node.
	 */
	rec->base.base = cursor.key_beg;
	rec->base.data_crc = crc32(data, bytes);
	rec->base.rec_id = 0;	/* XXX */
	rec->base.data_offset = hammer_bclu_offset(cursor.data_buffer, bdata);
	rec->base.data_len = bytes;
	hammer_modify_buffer(cursor.record_buffer);

	bcopy(data, bdata, bytes);
	hammer_modify_buffer(cursor.data_buffer);

	elm.leaf.base = cursor.key_beg;
	elm.leaf.rec_offset = hammer_bclu_offset(cursor.record_buffer, rec);
	elm.leaf.data_offset = rec->base.data_offset;
	elm.leaf.data_len = bytes;
	elm.leaf.data_crc = rec->base.data_crc;

	error = hammer_btree_insert(&cursor, &elm);
	if (error == 0)
		goto done;

	hammer_free_record_ptr(cursor.record_buffer, rec);
fail1:
	hammer_free_data_ptr(cursor.data_buffer, bdata, bytes);
done:
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Sync an in-memory record to the disk.  this is typically called via fsync
 * from a cached record source.  This code is responsible for actually
 * writing a record out to the disk.
 */
int
hammer_ip_sync_record(hammer_record_t record)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	union hammer_btree_elm elm;
	void *bdata;
	int error;

	error = hammer_init_cursor_ip(&cursor, record->ip);
	if (error)
		return(error);
	cursor.key_beg = record->rec.base.base;
	cursor.flags = HAMMER_CURSOR_INSERT;

	/*
	 * Issue a lookup to position the cursor and locate the cluster
	 */
	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		kprintf("hammer_ip_sync_record: duplicate rec at (%016llx)\n",
			record->rec.base.base.key);
		error = EIO;
	}
	if (error != ENOENT)
		goto done;

	/*
	 * Allocate record and data space now that we know which cluster
	 * the B-Tree node ended up in.
	 */
	if (record->data == NULL ||
	    (record->flags & HAMMER_RECF_EMBEDDED_DATA)) {
		bdata = record->data;
	} else {
		bdata = hammer_alloc_data(cursor.node->cluster,
					  record->rec.base.data_len, &error,
					  &cursor.data_buffer);
		if (bdata == NULL)
			goto done;
	}
	rec = hammer_alloc_record(cursor.node->cluster, &error,
				  &cursor.record_buffer);
	if (rec == NULL)
		goto fail1;

	/*
	 * Fill everything in and insert our B-Tree node.
	 *
	 * XXX assign rec_id here
	 */
	*rec = record->rec;
	if (bdata) {
		rec->base.data_crc = crc32(record->data,
					   record->rec.base.data_len);
		if (record->flags & HAMMER_RECF_EMBEDDED_DATA) {
			/*
			 * Data embedded in record
			 */
			rec->base.data_offset = ((char *)bdata -
						 (char *)&record->rec);
			KKASSERT(rec->base.data_offset >= 0 && 
				 rec->base.data_offset + rec->base.data_len <
				  sizeof(*rec));
			rec->base.data_offset += hammer_bclu_offset(cursor.record_buffer, rec);
		} else {
			/*
			 * Data separate from record
			 */
			rec->base.data_offset = hammer_bclu_offset(cursor.data_buffer,bdata);
			bcopy(record->data, bdata, rec->base.data_len);
			hammer_modify_buffer(cursor.data_buffer);
		}
	}
	rec->base.rec_id = 0;	/* XXX */

	hammer_modify_buffer(cursor.record_buffer);

	elm.leaf.base = cursor.key_beg;
	elm.leaf.rec_offset = hammer_bclu_offset(cursor.record_buffer, rec);
	elm.leaf.data_offset = rec->base.data_offset;
	elm.leaf.data_len = rec->base.data_len;
	elm.leaf.data_crc = rec->base.data_crc;

	error = hammer_btree_insert(&cursor, &elm);
	if (error == 0)
		goto done;

	hammer_free_record_ptr(cursor.record_buffer, rec);
fail1:
	if (record->data && (record->flags & HAMMER_RECF_EMBEDDED_DATA) == 0) {
		hammer_free_data_ptr(cursor.data_buffer, bdata,
				     rec->base.data_len);
	}
done:
	hammer_done_cursor(&cursor);
	return(error);
}


/*
 * Add the record to the inode's rec_tree.  The low 32 bits of a directory
 * entry's key is used to deal with hash collisions in the upper 32 bits.
 * A unique 64 bit key is generated in-memory and may be regenerated a
 * second time when the directory record is flushed to the on-disk B-Tree.
 */
static
int
hammer_mem_add(struct hammer_transaction *trans, hammer_record_t record)
{
	while (RB_INSERT(hammer_rec_rb_tree, &record->ip->rec_tree, record)) {
		if (record->rec.base.base.rec_type != HAMMER_RECTYPE_DIRENTRY){
			hammer_free_mem_record(record);
			return (EEXIST);
		}
		if (++trans->hmp->namekey_iterator == 0)
			++trans->hmp->namekey_iterator;
		record->rec.base.base.key &= ~(0xFFFFFFFFLL);
		record->rec.base.base.key |= trans->hmp->namekey_iterator;
	}
	record->flags |= HAMMER_RECF_ONRBTREE;
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
hammer_ip_lookup(hammer_cursor_t cursor, struct hammer_inode *ip)
{
	int error;

	/*
	 * If the element is in-memory return it without searching the
	 * on-disk B-Tree
	 */
	error = hammer_mem_lookup(cursor, ip);
	if (error == 0) {
		cursor->record = &cursor->iprec->rec;
		return(error);
	}
	if (error != ENOENT)
		return(error);

	/*
	 * If the inode has on-disk components search the on-disk B-Tree.
	 */
	if ((ip->flags & HAMMER_INODE_ONDISK) == 0)
		return(error);
	error = hammer_btree_lookup(cursor);
	if (error == 0)
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_RECORD);
	return(error);
}

/*
 * Locate the first record within the cursor's key_beg/key_end range,
 * restricted to a particular inode.  0 is returned on success, ENOENT
 * if no records matched the requested range, or some other error.
 *
 * When 0 is returned hammer_ip_next() may be used to iterate additional
 * records within the requested range.
 */
int
hammer_ip_first(hammer_cursor_t cursor, struct hammer_inode *ip)
{
	int error;

	/*
	 * Clean up fields and setup for merged scan
	 */
	cursor->flags |= HAMMER_CURSOR_ATEDISK | HAMMER_CURSOR_ATEMEM;
	cursor->flags |= HAMMER_CURSOR_DISKEOF | HAMMER_CURSOR_MEMEOF;
	if (cursor->iprec)
		hammer_rel_mem_record(&cursor->iprec);

	/*
	 * Search the on-disk B-Tree.  hammer_btree_lookup() only does an
	 * exact lookup so if we get ENOENT we have to call the iterate
	 * function to validate the first record after the begin key.
	 *
	 * The ATEDISK flag is used by hammer_btree_iterate to determine
	 * whether it must index forwards or not.
	 */
	if (ip->flags & HAMMER_INODE_ONDISK) {
		error = hammer_btree_lookup(cursor);
		if (error == ENOENT) {
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
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
	 * B-Tree search, mem_search checks for records in the range.
	 */
	error = hammer_mem_search(cursor, ip);
	if (error && error != ENOENT)
		return(error);
	if (error == 0) {
		cursor->flags &= ~HAMMER_CURSOR_MEMEOF;
		cursor->flags &= ~HAMMER_CURSOR_ATEMEM;
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
	hammer_record_t rec;
	int error;
	int r;

	/*
	 * Load the current on-disk and in-memory record.  If we ate any
	 * records we have to get the next one. 
	 *
	 * Get the next on-disk record
	 */
	if (cursor->flags & HAMMER_CURSOR_ATEDISK) {
		if ((cursor->flags & HAMMER_CURSOR_DISKEOF) == 0) {
			error = hammer_btree_iterate(cursor);
			if (error == 0)
				cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
			else
				cursor->flags |= HAMMER_CURSOR_DISKEOF;
		}
	}

	/*
	 * Get the next in-memory record.  The record can be ripped out
	 * of the RB tree so we maintain a scan_info structure to track
	 * the next node.
	 */
	if (cursor->flags & HAMMER_CURSOR_ATEMEM) {
		if ((cursor->flags & HAMMER_CURSOR_MEMEOF) == 0) {
			rec = cursor->scan.node;	/* next node */
			if (rec) {
				cursor->flags &= ~HAMMER_CURSOR_ATEMEM;
				hammer_ref(&rec->lock);
				cursor->scan.node =
					hammer_rec_rb_tree_RB_NEXT(rec);
			} else {
				cursor->flags |= HAMMER_CURSOR_MEMEOF;
			}
			hammer_rel_mem_record(&cursor->iprec);
			cursor->iprec = rec;
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
		 * Both entries valid
		 */
		elm = &cursor->node->ondisk->elms[cursor->index];
		r = hammer_btree_cmp(&elm->base,
				     &cursor->iprec->rec.base.base);
		if (r < 0) {
			error = hammer_btree_extract(cursor,
						     HAMMER_CURSOR_GET_RECORD);
			cursor->flags |= HAMMER_CURSOR_ATEDISK;
			break;
		}
		/* fall through to the memory entry */
	case HAMMER_CURSOR_ATEDISK:
		/*
		 * Only the memory entry is valid
		 */
		cursor->record = &cursor->iprec->rec;
		cursor->flags |= HAMMER_CURSOR_ATEMEM;
		break;
	case HAMMER_CURSOR_ATEMEM:
		/*
		 * Only the disk entry is valid
		 */
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_RECORD);
		cursor->flags |= HAMMER_CURSOR_ATEDISK;
		break;
	default:
		/*
		 * Neither entry is valid
		 *
		 * XXX error not set properly
		 */
		cursor->record = NULL;
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
	int error;

	if (cursor->iprec && cursor->record == &cursor->iprec->rec) {
		cursor->data = cursor->iprec->data;
		error = 0;
	} else {
		error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_DATA);
	}
	return(error);
}

/*
 * Delete all records within the specified range for inode ip.
 *
 * NOTE: An unaligned range will cause new records to be added to cover
 * the edge cases. (XXX not implemented yet).
 *
 * NOTE: ran_end is inclusive (e.g. 0,1023 instead of 0,1024).
 *
 * NOTE: Record keys for regular file data have to be special-cased since
 * they indicate the end of the range (key = base + bytes).
 */
int
hammer_ip_delete_range(hammer_transaction_t trans, hammer_inode_t ip,
		       int64_t ran_beg, int64_t ran_end)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	hammer_base_elm_t base;
	int error;
	int isregfile;
	int64_t off;

	hammer_init_cursor_ip(&cursor, ip);

	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = ip->obj_asof;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;

	/*
	 * The key in the B-Tree is (base+bytes), so the first possible
	 * matching key is ran_beg + 1.
	 */
	cursor.key_beg.key = ran_beg + 1;
	cursor.key_end = cursor.key_beg;
	if (ip->ino_rec.base.base.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.key = ran_end;
		isregfile = 0;
	} else {
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
		if (ran_end + MAXPHYS + 1 < ran_end)
			cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor.key_end.key = ran_end + MAXPHYS + 1;
		isregfile = 1;
	}

	error = hammer_ip_first(&cursor, ip);

	/*
	 * Iterate through matching records and mark them as deleted.
	 */
	while (error == 0) {
		rec = cursor.record;
		base = &rec->base.base;

		KKASSERT(base->delete_tid == 0);

		/*
		 * There may be overlap cases for regular file data.  Also
		 * remember the key for a regular file record is the offset
		 * of the last byte of the record (base + len - 1), NOT the
		 * base offset.
		 */
		if (base->rec_type == HAMMER_RECTYPE_DATA) {
			off = base->key - rec->base.data_len;
			/*
			 * Check the left edge case.  We currently do not
			 * split existing records.
			 */
			if (off < ran_beg) {
				panic("hammer left edge case %016llx %d\n",
					base->key, rec->base.data_len);
			}

			/*
			 * Check the right edge case.  Note that the
			 * record can be completely out of bounds, which
			 * terminates the search.
			 *
			 * base->key is exclusive of the right edge while
			 * ran_end is inclusive of the right edge.  The
			 * (key - data_len) left boundary is inclusive.
			 */
			if (base->key > ran_end + 1) {
				if (base->key - rec->base.data_len > ran_end) {
					kprintf("right edge OOB\n");
					break;
				}
				panic("hammer right edge case\n");
			}
		}

		/*
		 * Mark the record and B-Tree entry as deleted
		 */
		if (cursor.record == &cursor.iprec->rec) {
			hammer_free_mem_record(cursor.iprec);
		} else {
			cursor.node->ondisk->
			    elms[cursor.index].base.delete_tid = trans->tid;
			cursor.record->base.base.delete_tid = trans->tid;
			hammer_modify_node(cursor.node);
			hammer_modify_buffer(cursor.record_buffer);
		}
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);
	if (error == ENOENT)
		error = 0;
	return(error);
}

