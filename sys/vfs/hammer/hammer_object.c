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
 * $DragonFly: src/sys/vfs/hammer/hammer_object.c,v 1.17 2008/01/09 04:05:37 dillon Exp $
 */

#include "hammer.h"

static int hammer_mem_add(hammer_transaction_t trans,
			     hammer_record_t record);
static int hammer_mem_lookup(hammer_cursor_t cursor, hammer_inode_t ip);
static int hammer_mem_first(hammer_cursor_t cursor, hammer_inode_t ip);

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
	if (info->rec_type < rec->rec.base.base.rec_type)
		return(-3);
	if (info->rec_type > rec->rec.base.base.rec_type)
		return(3);

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
 * RB_SCAN comparison code for hammer_mem_first().  The argument order
 * is reversed so the comparison result has to be negated.  key_beg and
 * key_end are both range-inclusive.
 *
 * The creation timestamp can cause hammer_rec_compare() to return -1 or +1.
 * These do not stop the scan.
 *
 * Localized deletions are not cached in-memory.
 */
static
int
hammer_rec_scan_cmp(hammer_record_t rec, void *data)
{
	hammer_cursor_t cursor = data;
	int r;

	r = hammer_rec_compare(&cursor->key_beg, rec);
	if (r > 1)
		return(-1);
	if (r == 0)
		return(0);
	r = hammer_rec_compare(&cursor->key_end, rec);
	if (r < -1)
		return(1);
	return(0);
}

RB_GENERATE(hammer_rec_rb_tree, hammer_record, rb_node, hammer_rec_rb_compare);
RB_GENERATE_XLOOKUP(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
		    hammer_rec_compare, hammer_base_elm_t);

/*
 * Allocate a record for the caller to finish filling in.  The record is
 * returned referenced.
 */
hammer_record_t
hammer_alloc_mem_record(hammer_inode_t ip)
{
	hammer_record_t record;

	++hammer_count_records;
	record = kmalloc(sizeof(*record), M_HAMMER, M_WAITOK|M_ZERO);
	record->ip = ip;
	hammer_ref(&record->lock);
	return (record);
}

/*
 * Release a memory record.  Records marked for deletion are immediately
 * removed from the RB-Tree but otherwise left intact until the last ref
 * goes away.
 */
void
hammer_rel_mem_record(struct hammer_record *record)
{
	hammer_unref(&record->lock);
	if (record->flags & HAMMER_RECF_DELETED) {
		if (record->flags & HAMMER_RECF_ONRBTREE) {
			RB_REMOVE(hammer_rec_rb_tree, &record->ip->rec_tree,
				  record);
			record->flags &= ~HAMMER_RECF_ONRBTREE;
		}
		if (record->lock.refs == 0) {
			if (record->flags & HAMMER_RECF_ALLOCDATA) {
				--hammer_count_record_datas;
				kfree(record->data, M_HAMMER);
				record->flags &= ~HAMMER_RECF_ALLOCDATA;
			}
			record->data = NULL;
			--hammer_count_records;
			kfree(record, M_HAMMER);
		}
	}
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

	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
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
 * hammer_mem_first() - locate the first in-memory record matching the
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

	/*
	 * Skip if not visible due to our as-of TID
	 */
        if (cursor->key_beg.create_tid) {
                if (cursor->key_beg.create_tid < rec->rec.base.base.create_tid)
                        return(0);
                if (rec->rec.base.base.delete_tid &&
		    cursor->key_beg.create_tid >=
		     rec->rec.base.base.delete_tid) {
                        return(0);
		}
        }

	/*
	 * Return the first matching record and stop the scan
	 */
	if (cursor->iprec == NULL) {
		cursor->iprec = rec;
		hammer_ref(&rec->lock);
		return(-1);
	}
	return(0);
}

static
int
hammer_mem_first(hammer_cursor_t cursor, hammer_inode_t ip)
{
	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}
	if (cursor->ip) {
		hammer_rec_rb_tree_scan_info_done(&cursor->scan,
						  &cursor->ip->rec_tree);
	}
	cursor->ip = ip;
	hammer_rec_rb_tree_scan_info_link(&cursor->scan, &ip->rec_tree);

	cursor->scan.node = NULL;
	hammer_rec_rb_tree_RB_SCAN(&ip->rec_tree, hammer_rec_scan_cmp,
				   hammer_rec_scan_callback, cursor);

	/*
	 * Adjust scan.node and keep it linked into the RB-tree so we can
	 * hold the cursor through third party modifications of the RB-tree.
	 */
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
 */
int
hammer_ip_add_directory(struct hammer_transaction *trans,
		     struct hammer_inode *dip, struct namecache *ncp,
		     struct hammer_inode *ip)
{
	hammer_record_t record;
	int error;
	int bytes;

	record = hammer_alloc_mem_record(dip);

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
		++hammer_count_record_datas;
		record->data = kmalloc(bytes, M_HAMMER, M_WAITOK);
		record->flags |= HAMMER_RECF_ALLOCDATA;
	}
	bcopy(ncp->nc_name, record->data, bytes);
	record->rec.entry.base.data_len = bytes;
	++ip->ino_rec.ino_nlinks;
	hammer_modify_inode(trans, ip, HAMMER_INODE_RDIRTY);
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

	error = hammer_ip_delete_record(cursor, trans->tid);

	/*
	 * One less link.  The file may still be open in the OS even after
	 * all links have gone away so we only try to sync if the OS has
	 * no references and nlinks falls to 0.
	 */
	if (error == 0) {
		--ip->ino_rec.ino_nlinks;
		hammer_modify_inode(trans, ip, HAMMER_INODE_RDIRTY);
		if (ip->ino_rec.ino_nlinks == 0 &&
		    (ip->vp == NULL || (ip->vp->v_flag & VINACTIVE))) {
			hammer_sync_inode(ip, MNT_NOWAIT, 1);
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
 * record->rec.entry.base.base.key
 * record->rec.entry.base.base.rec_type
 * record->rec.entry.base.base.data_len
 * record->data		(a copy will be kmalloc'd if not embedded)
 */
int
hammer_ip_add_record(struct hammer_transaction *trans, hammer_record_t record)
{
	hammer_inode_t ip = record->ip;
	int error;
	int bytes;
	void *data;

	record->rec.base.base.obj_id = ip->obj_id;
	record->rec.base.base.create_tid = trans->tid;
	record->rec.base.base.obj_type = ip->ino_rec.base.base.obj_type;
	bytes = record->rec.base.data_len;

	if (record->data) {
		if ((char *)record->data < (char *)&record->rec ||
		    (char *)record->data >= (char *)(&record->rec + 1)) {
			++hammer_count_record_datas;
			data = kmalloc(bytes, M_HAMMER, M_WAITOK);
			record->flags |= HAMMER_RECF_ALLOCDATA;
			bcopy(record->data, data, bytes);
			record->data = data;
		} else {
			record->flags |= HAMMER_RECF_EMBEDDED_DATA;
		}
	}
	hammer_modify_inode(trans, ip, HAMMER_INODE_RDIRTY);
	error = hammer_mem_add(trans, record);
	return(error);
}

/*
 * Sync data from a buffer cache buffer (typically) to the filesystem.  This
 * is called via the strategy called from a cached data source.  This code
 * is responsible for actually writing a data record out to the disk.
 */
int
hammer_ip_sync_data(hammer_transaction_t trans, hammer_inode_t ip,
		       int64_t offset, void *data, int bytes,
		       struct hammer_cursor **spike)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	union hammer_btree_elm elm;
	void *bdata;
	int error;

	error = hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);
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
	hammer_modify_buffer(cursor.record_buffer);
	rec->base.base = cursor.key_beg;
	rec->base.data_crc = crc32(data, bytes);
	rec->base.rec_id = 0;	/* XXX */
	rec->base.data_offset = hammer_bclu_offset(cursor.data_buffer, bdata);
	rec->base.data_len = bytes;
	hammer_modify_buffer_done(cursor.record_buffer);

	hammer_modify_buffer(cursor.data_buffer);
	bcopy(data, bdata, bytes);
	hammer_modify_buffer_done(cursor.data_buffer);

	elm.leaf.base = cursor.key_beg;
	elm.leaf.rec_offset = hammer_bclu_offset(cursor.record_buffer, rec);
	elm.leaf.data_offset = rec->base.data_offset;
	elm.leaf.data_len = bytes;
	elm.leaf.data_crc = rec->base.data_crc;

	/*
	 * Data records can wind up on-disk before the inode itself is
	 * on-disk.  One must assume data records may be on-disk if either
	 * HAMMER_INODE_DONDISK or HAMMER_INODE_ONDISK is set
	 */
	ip->flags |= HAMMER_INODE_DONDISK;

	error = hammer_btree_insert(&cursor, &elm);
	if (error == 0) {
		hammer_update_syncid(cursor.record_buffer->cluster, trans->tid);
		goto done;
	}

	hammer_free_record_ptr(cursor.record_buffer, rec);
fail1:
	hammer_free_data_ptr(cursor.data_buffer, bdata, bytes);
done:
	/*
	 * If ENOSPC in cluster fill in the spike structure and return
	 * ENOSPC.
	 */
	if (error == ENOSPC)
		hammer_load_spike(&cursor, spike);
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Sync an in-memory record to the disk.  this is typically called via fsync
 * from a cached record source.  This code is responsible for actually
 * writing a record out to the disk.
 */
int
hammer_ip_sync_record(hammer_record_t record, struct hammer_cursor **spike)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	hammer_mount_t hmp;
	union hammer_btree_elm elm;
	void *bdata;
	int error;

	error = hammer_init_cursor_hmp(&cursor, &record->ip->cache[0],
				       record->ip->hmp);
	if (error)
		return(error);
	cursor.key_beg = record->rec.base.base;
	cursor.flags = HAMMER_CURSOR_INSERT;

	/*
	 * Issue a lookup to position the cursor and locate the cluster.  The
	 * target key should not exist.  If we are creating a directory entry
	 * we may have to iterate the low 32 bits of the key to find an unused
	 * key.
	 *
	 * If we run out of space trying to adjust the B-Tree for the
	 * insert, re-lookup without the insert flag so the cursor
	 * is properly positioned for the spike.
	 */
again:
	error = hammer_btree_lookup(&cursor);
	if (error == 0) {
		if (record->rec.base.base.rec_type == HAMMER_RECTYPE_DIRENTRY) {
			hmp = cursor.node->cluster->volume->hmp;
			if (++hmp->namekey_iterator == 0)
				++hmp->namekey_iterator;
			record->rec.base.base.key &= ~(0xFFFFFFFFLL);
			record->rec.base.base.key |= hmp->namekey_iterator;
			goto again;
		}
		kprintf("hammer_ip_sync_record: duplicate rec at (%016llx)\n",
			record->rec.base.base.key);
		Debugger("duplicate record1");
		error = EIO;
	}
	if (error != ENOENT)
		goto done;

	/*
	 * Mark the record as undergoing synchronization.  Our cursor is
	 * holding a locked B-Tree node for the insertion which interlocks
	 * anyone trying to access this record.
	 *
	 * XXX There is still a race present related to iterations.  An
	 * iteration may process the record, a sync may occur, and then
	 * later process the B-Tree element for the same record.
	 *
	 * We do not try to synchronize a deleted record.
	 */
	if (record->flags & (HAMMER_RECF_DELETED | HAMMER_RECF_SYNCING)) {
		error = 0;
		goto done;
	}
	record->flags |= HAMMER_RECF_SYNCING;

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
			goto fail2;
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
	hammer_modify_buffer(cursor.record_buffer);
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
				 rec->base.data_offset + rec->base.data_len <=
				  sizeof(*rec));
			rec->base.data_offset += hammer_bclu_offset(cursor.record_buffer, rec);
		} else {
			/*
			 * Data separate from record
			 */
			rec->base.data_offset = hammer_bclu_offset(cursor.data_buffer,bdata);
			hammer_modify_buffer(cursor.data_buffer);
			bcopy(record->data, bdata, rec->base.data_len);
			hammer_modify_buffer_done(cursor.data_buffer);
		}
	}
	rec->base.rec_id = 0;	/* XXX */
	hammer_modify_buffer_done(cursor.record_buffer);

	elm.leaf.base = cursor.key_beg;
	elm.leaf.rec_offset = hammer_bclu_offset(cursor.record_buffer, rec);
	elm.leaf.data_offset = rec->base.data_offset;
	elm.leaf.data_len = rec->base.data_len;
	elm.leaf.data_crc = rec->base.data_crc;

	error = hammer_btree_insert(&cursor, &elm);

	/*
	 * Clean up on success, or fall through on error.
	 */
	if (error == 0) {
		record->flags |= HAMMER_RECF_DELETED;
		record->flags &= ~HAMMER_RECF_SYNCING;
		hammer_update_syncid(cursor.record_buffer->cluster,
				     record->rec.base.base.create_tid);
		goto done;
	}

	hammer_free_record_ptr(cursor.record_buffer, rec);
fail1:
	if (record->data && (record->flags & HAMMER_RECF_EMBEDDED_DATA) == 0) {
		hammer_free_data_ptr(cursor.data_buffer, bdata,
				     record->rec.base.data_len);
	}
fail2:
	record->flags &= ~HAMMER_RECF_SYNCING;
done:
	/*
	 * If ENOSPC in cluster fill in the spike structure and return
	 * ENOSPC.
	 */
	if (error == ENOSPC)
		hammer_load_spike(&cursor, spike);
	hammer_done_cursor(&cursor);
	return(error);
}

/*
 * Write out a record using the specified cursor.  The caller does not have
 * to seek the cursor.  The flags are used to determine whether the data
 * (if any) is embedded in the record or not.
 *
 * The target cursor will be modified by this call.  Note in particular
 * that HAMMER_CURSOR_INSERT is set.
 */
int
hammer_write_record(hammer_cursor_t cursor, hammer_record_ondisk_t orec,
		    void *data, int cursor_flags)
{
	union hammer_btree_elm elm;
	hammer_record_ondisk_t nrec;
	void *bdata;
	int error;

	cursor->key_beg = orec->base.base;
	cursor->flags |= HAMMER_CURSOR_INSERT;

	/*
	 * Issue a lookup to position the cursor and locate the cluster.  The
	 * target key should not exist.
	 *
	 * If we run out of space trying to adjust the B-Tree for the
	 * insert, re-lookup without the insert flag so the cursor
	 * is properly positioned for the spike.
	 */
	error = hammer_btree_lookup(cursor);
	if (error == 0) {
		kprintf("hammer_ip_sync_record: duplicate rec at (%016llx)\n",
			orec->base.base.key);
		Debugger("duplicate record2");
		error = EIO;
	}
	if (error != ENOENT)
		goto done;

	/*
	 * Allocate record and data space now that we know which cluster
	 * the B-Tree node ended up in.
	 */
	if (data == NULL ||
	    (cursor_flags & HAMMER_RECF_EMBEDDED_DATA)) {
		bdata = data;
	} else {
		bdata = hammer_alloc_data(cursor->node->cluster,
					  orec->base.data_len, &error,
					  &cursor->data_buffer);
		if (bdata == NULL)
			goto done;
	}
	nrec = hammer_alloc_record(cursor->node->cluster, &error,
				  &cursor->record_buffer);
	if (nrec == NULL)
		goto fail1;

	/*
	 * Fill everything in and insert our B-Tree node.
	 *
	 * XXX assign rec_id here
	 */
	hammer_modify_buffer(cursor->record_buffer);
	*nrec = *orec;
	nrec->base.data_offset = 0;
	if (bdata) {
		nrec->base.data_crc = crc32(bdata, nrec->base.data_len);
		if (cursor_flags & HAMMER_RECF_EMBEDDED_DATA) {
			/*
			 * Data embedded in record
			 */
			nrec->base.data_offset = ((char *)bdata - (char *)orec);
			KKASSERT(nrec->base.data_offset >= 0 && 
				 nrec->base.data_offset + nrec->base.data_len <
				  sizeof(*nrec));
			nrec->base.data_offset += hammer_bclu_offset(cursor->record_buffer, nrec);
		} else {
			/*
			 * Data separate from record
			 */
			nrec->base.data_offset = hammer_bclu_offset(cursor->data_buffer, bdata);
			hammer_modify_buffer(cursor->data_buffer);
			bcopy(data, bdata, nrec->base.data_len);
			hammer_modify_buffer_done(cursor->data_buffer);
		}
	}
	nrec->base.rec_id = 0;	/* XXX */
	hammer_modify_buffer_done(cursor->record_buffer);

	elm.leaf.base = nrec->base.base;
	elm.leaf.rec_offset = hammer_bclu_offset(cursor->record_buffer, nrec);
	elm.leaf.data_offset = nrec->base.data_offset;
	elm.leaf.data_len = nrec->base.data_len;
	elm.leaf.data_crc = nrec->base.data_crc;

	error = hammer_btree_insert(cursor, &elm);
	if (error == 0) {
		hammer_update_syncid(cursor->record_buffer->cluster,
				     nrec->base.base.create_tid);
		goto done;
	}

	hammer_free_record_ptr(cursor->record_buffer, nrec);
fail1:
	if (data && (cursor_flags & HAMMER_RECF_EMBEDDED_DATA) == 0) {
		hammer_free_data_ptr(cursor->data_buffer, bdata,
				     orec->base.data_len);
	}
done:
	/* leave cursor intact */
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
 */
static
int
hammer_mem_add(struct hammer_transaction *trans, hammer_record_t record)
{
	while (RB_INSERT(hammer_rec_rb_tree, &record->ip->rec_tree, record)) {
		if (record->rec.base.base.rec_type != HAMMER_RECTYPE_DIRENTRY){
			record->flags |= HAMMER_RECF_DELETED;
			hammer_rel_mem_record(record);
			return (EEXIST);
		}
		if (++trans->hmp->namekey_iterator == 0)
			++trans->hmp->namekey_iterator;
		record->rec.base.base.key &= ~(0xFFFFFFFFLL);
		record->rec.base.base.key |= trans->hmp->namekey_iterator;
	}
	record->flags |= HAMMER_RECF_ONRBTREE;
	hammer_modify_inode(trans, record->ip, HAMMER_INODE_XDIRTY);
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
	if ((ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DONDISK)) == 0)
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
	 */
	if (ip->flags & (HAMMER_INODE_ONDISK|HAMMER_INODE_DONDISK)) {
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
	 * B-Tree search, mem_first checks for records in the range.
	 */
	error = hammer_mem_first(cursor, ip);
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
			if (error == 0)
				cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
			else
				cursor->flags |= HAMMER_CURSOR_DISKEOF |
						 HAMMER_CURSOR_ATEDISK;
		}
	}

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
			if (cursor->iprec) {
				hammer_rel_mem_record(cursor->iprec);
				cursor->iprec = NULL;
			}
			rec = cursor->scan.node;	/* next node */
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
				cursor->scan.node =
					hammer_rec_rb_tree_RB_NEXT(rec);
			} else {
				cursor->flags |= HAMMER_CURSOR_MEMEOF;
			}
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
		r = hammer_btree_cmp(&elm->base, &cursor->iprec->rec.base.base);
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
 *
 * NOTE: The spike structure must be filled in if we return ENOSPC.
 */
int
hammer_ip_delete_range(hammer_transaction_t trans, hammer_inode_t ip,
		       int64_t ran_beg, int64_t ran_end,
		       struct hammer_cursor **spike)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	hammer_base_elm_t base;
	int error;
	int64_t off;

	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);

	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = ip->obj_asof;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	if (ip->ino_rec.base.base.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor.key_beg.key = ran_beg;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.key = ran_end;
	} else {
		/*
		 * The key in the B-Tree is (base+bytes), so the first possible
		 * matching key is ran_beg + 1.
		 */
		int64_t tmp64;

		cursor.key_beg.key = ran_beg + 1;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;

		tmp64 = ran_end + MAXPHYS + 1;	/* work around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor.key_end.key = ran_end + MAXPHYS + 1;
	}
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

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
#if 0
		kprintf("delete_range rec_type %02x\n", base->rec_type);
#endif
		if (base->rec_type == HAMMER_RECTYPE_DATA) {
#if 0
			kprintf("delete_range loop key %016llx\n",
				base->key - rec->base.data_len);
#endif
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
			 *
			 * XXX theory-check this test at some point, are
			 * we missing a + 1 somewhere?  Note that ran_end
			 * could overflow.
			 */
			if (base->key - 1 > ran_end) {
				if (base->key - rec->base.data_len > ran_end)
					break;
				panic("hammer right edge case\n");
			}
		}

		/*
		 * Mark the record and B-Tree entry as deleted.  This will
		 * also physically delete the B-Tree entry, record, and
		 * data if the retention policy dictates.  The function
		 * will set HAMMER_CURSOR_DELBTREE which hammer_ip_next()
		 * uses to perform a fixup.
		 */
		error = hammer_ip_delete_record(&cursor, trans->tid);
		if (error)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);
	if (error == ENOENT)
		error = 0;
	return(error);
}

/*
 * Delete all records associated with an inode except the inode record
 * itself.
 */
int
hammer_ip_delete_range_all(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	hammer_record_ondisk_t rec;
	hammer_base_elm_t base;
	int error;

	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);

	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = ip->obj_asof;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE + 1;
	cursor.key_beg.key = HAMMER_MIN_KEY;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.rec_type = 0xFFFF;
	cursor.key_end.key = HAMMER_MAX_KEY;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor, ip);

	/*
	 * Iterate through matching records and mark them as deleted.
	 */
	while (error == 0) {
		rec = cursor.record;
		base = &rec->base.base;

		KKASSERT(base->delete_tid == 0);

		/*
		 * Mark the record and B-Tree entry as deleted.  This will
		 * also physically delete the B-Tree entry, record, and
		 * data if the retention policy dictates.  The function
		 * will set HAMMER_CURSOR_DELBTREE which hammer_ip_next()
		 * uses to perform a fixup.
		 */
		error = hammer_ip_delete_record(&cursor, trans->tid);
		if (error)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);
	if (error == ENOENT)
		error = 0;
	return(error);
}

/*
 * Delete the record at the current cursor
 */
int
hammer_ip_delete_record(hammer_cursor_t cursor, hammer_tid_t tid)
{
	hammer_btree_elm_t elm;
	hammer_mount_t hmp;
	int error;

	/*
	 * In-memory (unsynchronized) records can simply be freed.
	 */
	if (cursor->record == &cursor->iprec->rec) {
		cursor->iprec->flags |= HAMMER_RECF_DELETED;
		return(0);
	}

	/*
	 * On-disk records are marked as deleted by updating their delete_tid.
	 */
	error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_RECORD);
	elm = NULL;
	hmp = cursor->node->cluster->volume->hmp;

	if (error == 0) {
		hammer_modify_buffer(cursor->record_buffer);
		cursor->record->base.base.delete_tid = tid;

		hammer_modify_buffer_done(cursor->record_buffer);
		hammer_modify_node(cursor->node);
		elm = &cursor->node->ondisk->elms[cursor->index];
		elm->leaf.base.delete_tid = tid;
		hammer_modify_node_done(cursor->node);
		hammer_update_syncid(cursor->record_buffer->cluster, tid);
	}

	/*
	 * If we were mounted with the nohistory option, we physically
	 * delete the record.
	 */
	if (error == 0 && (hmp->hflags & HMNT_NOHISTORY)) {
		int32_t rec_offset;
		int32_t data_offset;
		int32_t data_len;
		hammer_cluster_t cluster;

		rec_offset = elm->leaf.rec_offset;
		data_offset = elm->leaf.data_offset;
		data_len = elm->leaf.data_len;
#if 0
		kprintf("hammer_ip_delete_record: %08x %08x/%d\n",
			rec_offset, data_offset, data_len);
#endif
		cluster = cursor->node->cluster;
		hammer_ref_cluster(cluster);

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
			hammer_free_record(cluster, rec_offset);
			if (data_offset && (data_offset - rec_offset < 0 ||
			    data_offset - rec_offset >= HAMMER_RECORD_SIZE)) {
				hammer_free_data(cluster, data_offset,data_len);
			}
		}
		hammer_rel_cluster(cluster, 0);
		if (error) {
			panic("hammer_ip_delete_record: unable to physically delete the record!\n");
			error = 0;
		}
	}
	return(error);
}

/*
 * Determine whether a directory is empty or not.  Returns 0 if the directory
 * is empty, ENOTEMPTY if it isn't, plus other possible errors.
 */
int
hammer_ip_check_directory_empty(hammer_transaction_t trans, hammer_inode_t ip)
{
	struct hammer_cursor cursor;
	int error;

	hammer_init_cursor_hmp(&cursor, &ip->cache[0], ip->hmp);

	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = ip->obj_asof;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_INODE + 1;
	cursor.key_beg.key = HAMMER_MIN_KEY;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.rec_type = 0xFFFF;
	cursor.key_end.key = HAMMER_MAX_KEY;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor, ip);
	if (error == ENOENT)
		error = 0;
	else if (error == 0)
		error = ENOTEMPTY;
	hammer_done_cursor(&cursor);
	return(error);
}

