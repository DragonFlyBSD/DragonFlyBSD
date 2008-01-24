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
 * $DragonFly: src/sys/vfs/hammer/hammer_recover.c,v 1.6 2008/01/24 02:14:45 dillon Exp $
 */

#include "hammer.h"

static int hammer_recover_buffer_stage2(hammer_cluster_t cluster,
				int32_t buf_no);
static int hammer_recover_record(hammer_cluster_t cluster,
				hammer_buffer_t buffer, int32_t rec_offset,
				hammer_record_ondisk_t rec);
static int hammer_recover_btree(hammer_cluster_t cluster,
				hammer_buffer_t buffer, int32_t rec_offset,
				hammer_record_ondisk_t rec);

/*
 * Recover a cluster.  The caller has referenced and locked the cluster.
 * 
 * Generally returns 0 on success and EIO if the recovery was unsuccessful.
 *
 * WARNING!  The cluster being recovered must not have any cached buffers
 * (and hence no cached b-tree nodes).  Any cached nodes will become seriously
 * corrupted since we rip it all up and regenerate the B-Tree.
 */
int
hammer_recover(hammer_cluster_t cluster)
{
	int buf_no;
	int rec_no;
	int maxblk;
	int nbuffers;
	int buffer_count;
	int record_count;
	static int count;	/* XXX temporary */

	kprintf("HAMMER_RECOVER %d:%d\n",
		cluster->volume->vol_no, cluster->clu_no);
	KKASSERT(cluster->ondisk->synchronized_rec_id);
	if (RB_ROOT(&cluster->rb_bufs_root)) {
		panic("hammer_recover: cluster %d:%d has cached buffers!",
			cluster->volume->vol_no,
			cluster->clu_no);
	}

	if (hammer_alist_find(&cluster->volume->alist, cluster->clu_no,
			      cluster->clu_no + 1, 0) != cluster->clu_no) {
		Debugger("hammer_recover: cluster not allocated!");
	}

	nbuffers = cluster->ondisk->clu_limit / HAMMER_BUFSIZE;
	hammer_modify_cluster(cluster);

	/*
	 * Clear statistics.
	 */
	cluster->ondisk->stat_inodes = 0;
	cluster->ondisk->stat_records = 0;
	cluster->ondisk->stat_data_bufs = 0;
	cluster->ondisk->stat_rec_bufs = 0;
	cluster->ondisk->stat_idx_bufs = 0;

	/*
	 * Reset allocation heuristics.
	 */
	cluster->ondisk->idx_data = 1 * HAMMER_FSBUF_MAXBLKS;
	cluster->ondisk->idx_index = 0 * HAMMER_FSBUF_MAXBLKS;
	cluster->ondisk->idx_record = nbuffers * HAMMER_FSBUF_MAXBLKS;

	/*
	 * Re-initialize the master, B-Tree, and mdata A-lists, and
	 * recover the record A-list.
	 */
	hammer_alist_init(&cluster->alist_master, 1, nbuffers - 1,
			  HAMMER_ASTATE_FREE);
	hammer_alist_init(&cluster->alist_btree,
			  HAMMER_FSBUF_MAXBLKS,
			  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS,
			  HAMMER_ASTATE_ALLOC);
	hammer_alist_init(&cluster->alist_mdata,
			  HAMMER_FSBUF_MAXBLKS,
			  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS,
			  HAMMER_ASTATE_ALLOC);
	hammer_alist_recover(&cluster->alist_record,
			  0,
			  HAMMER_FSBUF_MAXBLKS,
			  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS);
	kprintf("\n");

	kprintf("hammer_recover(1): cluster_free %d\n",
		cluster->alist_master.meta->bm_alist_freeblks);

	/*
	 * The cluster is now in good enough shape that general allocations
	 * are possible.  Construct an empty B-Tree root.
	 */
	{
		hammer_node_t croot;
		int error;

		croot = hammer_alloc_btree(cluster, &error);
		if (error == 0) {
			hammer_modify_node(croot);
			bzero(croot->ondisk, sizeof(*croot->ondisk));
			croot->ondisk->count = 0;
			croot->ondisk->type = HAMMER_BTREE_TYPE_LEAF;
			cluster->ondisk->clu_btree_root = croot->node_offset;
			hammer_rel_node(croot);
		}
		KKASSERT(error == 0);
	}
	kprintf("hammer_recover(2): cluster_free %d\n",
		cluster->alist_master.meta->bm_alist_freeblks);

	/*
	 * Scan the cluster's recovered record A-list.  Just get the meta
	 * blocks and ignore all-allocated/uninitialized sections (which
	 * we use to indicate reserved areas not assigned to record buffers).
	 *
	 * The all-free sections are initialized and this is indicated by
	 * the alist config's bl_inverted flag being set.  These sections
	 * will be returned for recovery purposes.
	 */
	buffer_count = 0;
	record_count = 0;

	rec_no = HAMMER_FSBUF_MAXBLKS;
	maxblk = nbuffers * HAMMER_FSBUF_MAXBLKS;
	for (;;) {
		rec_no = hammer_alist_find(&cluster->alist_record,
					   rec_no,
					   maxblk,
					   HAMMER_ALIST_FIND_NOSTACK |
					   HAMMER_ALIST_FIND_INITONLY);
		if (rec_no == HAMMER_ALIST_BLOCK_NONE)
			break;
		buf_no = rec_no / HAMMER_FSBUF_MAXBLKS;
		KKASSERT(buf_no > 0 && buf_no <= nbuffers);
		++buffer_count;
		kprintf("(%d)", buf_no);
		record_count += hammer_recover_buffer_stage2(cluster, buf_no);
		rec_no += HAMMER_FSBUF_MAXBLKS;
	}
	kprintf("HAMMER_RECOVER DONE %d:%d buffers=%d records=%d\n",
		cluster->volume->vol_no, cluster->clu_no,
		buffer_count, record_count);

	/*
	 * Validate the parent cluster pointer. XXX
	 */

	/*
	 * On successful recovery mark the cluster validated.
	 */
	cluster->io.validated = 1;
	return(0);
}

/*
 * This is used in the alist callback and must return a negative error
 * code or a positive free block count.
 */
int
buffer_alist_recover(void *info, int32_t blk, int32_t radix, int32_t count)
{
	hammer_cluster_t cluster;
	hammer_record_ondisk_t rec;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int32_t rec_no;
	int32_t rec_offset;
	int32_t r;
	int error;
	int xcount;

	/*
	 * Extract cluster and buffer number to recover
	 */
	cluster = info;
	buf_no = blk / HAMMER_FSBUF_MAXBLKS;

	kprintf("(%d)", buf_no);
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (error) {
		/*
		 * If we are unable to access the buffer leave it in a
		 * reserved state on the master alist.
		 */
		kprintf("hammer_recover_buffer_stage1: error "
			"recovering %d:%d:%d\n",
			cluster->volume->vol_no, cluster->clu_no, buf_no);
		r = hammer_alist_alloc_fwd(&cluster->alist_master, 1, buf_no);
		KKASSERT(r == buf_no);
		return(-error);
	}
	KKASSERT(buffer->buf_type == HAMMER_FSBUF_RECORDS);

	/*
	 * If the buffer contains no allocated records tell our parent to
	 * mark it as all-allocated/uninitialized and do not reserve it
	 * in the master list.
	 */
	if (hammer_alist_find(&buffer->alist, 0, HAMMER_RECORD_NODES, 0) ==
	    HAMMER_ALIST_BLOCK_NONE) {
		kprintf("GENERAL RECOVERY BUFFER %d\n",
			blk / HAMMER_FSBUF_MAXBLKS);
		hammer_rel_buffer(buffer, 0);
		return(-EDOM);
	}


	/*
	 * Mark the buffer as allocated in the cluster's master A-list.
	 */
	r = hammer_alist_alloc_fwd(&cluster->alist_master, 1, buf_no);
	KKASSERT(r == buf_no);
	++cluster->ondisk->stat_rec_bufs;

	kprintf("recover buffer1 %d:%d:%d cluster_free %d\n",
		cluster->volume->vol_no,
		cluster->clu_no, buf_no,
		cluster->alist_master.meta->bm_alist_freeblks);

	/*
	 * Recover the buffer, scan and validate allocated records.  Records
	 * which cannot be recovered are freed.
	 *
	 * The parent a-list must be properly adjusted so don't just call
	 * hammer_alist_recover() on the underlying buffer.  Go through the
	 * parent.
	 */
	hammer_modify_buffer(buffer);
	count = hammer_alist_recover(&buffer->alist, 0, 0, HAMMER_RECORD_NODES);
	xcount = 0;
	kprintf("hammer_recover_buffer count1 %d/%d\n",
		HAMMER_RECORD_NODES - count, HAMMER_RECORD_NODES);
	rec_no = 0;
	for (;;) {
		rec_no = hammer_alist_find(&buffer->alist, rec_no,
					   HAMMER_RECORD_NODES, 0);
		if (rec_no == HAMMER_ALIST_BLOCK_NONE)
			break;
#if 0
		kprintf("recover record %d:%d:%d %d\n",
			cluster->volume->vol_no,
			cluster->clu_no, buf_no, rec_no);
#endif
		rec_offset = offsetof(union hammer_fsbuf_ondisk,
				      record.recs[rec_no]);
		rec_offset += buf_no * HAMMER_BUFSIZE;
		rec = &buffer->ondisk->record.recs[rec_no];
		error = hammer_recover_record(cluster, buffer, rec_offset, rec);
		if (error) {
			kprintf("hammer_recover_record: failed %d:%d@%d\n",
				cluster->clu_no, buffer->buf_no, rec_offset);
			hammer_alist_free(&buffer->alist, rec_no, 1);
			Debugger("FAILED");
			++count;	/* free count */
			--xcount;
		}
		++rec_no;
		++xcount;
	}
	kprintf("hammer_recover_buffer count2 %d/%d/%d\n",
		HAMMER_RECORD_NODES - count, xcount, HAMMER_RECORD_NODES);
	KKASSERT(HAMMER_RECORD_NODES - count == xcount);
	hammer_rel_buffer(buffer, 0);
	return(count);
}

/*
 * Recover a record, at least into a state that doesn't blow up the
 * filesystem.  Returns 0 on success, non-zero if the record is
 * unrecoverable.
 */
static int
hammer_recover_record(hammer_cluster_t cluster, hammer_buffer_t buffer,
			     int32_t rec_offset, hammer_record_ondisk_t rec)
{
	hammer_buffer_t dbuf;
	u_int64_t syncid = cluster->ondisk->synchronized_rec_id;
	int32_t data_offset;
	int32_t data_len;
	int32_t nblks;
	int32_t dbuf_no;
	int32_t dblk_no;
	int32_t base_blk;
	int32_t r;
	int error = 0;

	/*
	 * We have to discard any records with rec_id's greater then the
	 * last sync of the cluster header (which guarenteed all related
	 * buffers had been synced).  Otherwise the record may reference
	 * information that was never synced to disk.
	 */
	if (rec->base.rec_id >= syncid) {
		kprintf("recover record: syncid too large %016llx/%016llx\n",
			rec->base.rec_id, syncid);
		Debugger("DebugSyncid");
		return(EINVAL);
	}

#if 0
	/* XXX undo incomplete deletions */
	if (rec->base.base.delete_tid > syncid)
		rec->base.base.delete_tid = 0;
#endif

	/*
	 * Validate the record's B-Tree key
	 */
	KKASSERT(rec->base.base.rec_type != 0);
	if (rec->base.base.rec_type != HAMMER_RECTYPE_CLUSTER) {
		if (hammer_btree_cmp(&rec->base.base,
				     &cluster->ondisk->clu_btree_beg) < 0)  {
			kprintf("recover record: range low\n");
			Debugger("RANGE LOW");
			return(EINVAL);
		}
		if (hammer_btree_cmp(&rec->base.base,
				     &cluster->ondisk->clu_btree_end) >= 0)  {
			kprintf("recover record: range high\n");
			Debugger("RANGE HIGH");
			return(EINVAL);
		}
	}

	/*
	 * Validate the record's data.  If the offset is 0 there is no data
	 * (or it is zero-fill) and we can return success immediately.
	 * Otherwise make sure everything is ok.
	 */
	data_offset = rec->base.data_offset;
	data_len = rec->base.data_len;

	if (data_len == 0)
		rec->base.data_offset = data_offset = 0;
	if (data_offset == 0)
		goto done;

	/*
	 * Non-zero data offset, recover the data
	 */
	if (data_offset < HAMMER_BUFSIZE ||
	    data_offset >= cluster->ondisk->clu_limit ||
	    data_len < 0 || data_len > HAMMER_MAXDATA ||
	    data_offset + data_len > cluster->ondisk->clu_limit) {
		kprintf("recover record: bad offset/len %d/%d\n",
			data_offset, data_len);
		Debugger("BAD OFFSET");
		return(EINVAL);
	}

	/*
	 * Check data_offset relative to rec_offset
	 */
	if (data_offset < rec_offset && data_offset + data_len > rec_offset) {
		kprintf("recover record: bad offset: overlapping1\n");
		Debugger("BAD OFFSET - OVERLAP1");
		return(EINVAL);
	}
	if (data_offset >= rec_offset &&
	    data_offset < rec_offset + sizeof(struct hammer_base_record)) {
		kprintf("recover record: bad offset: overlapping2\n");
		Debugger("BAD OFFSET - OVERLAP2");
		return(EINVAL);
	}

	/*
	 * Check for data embedded in the record
	 */
	if (data_offset >= rec_offset &&
	    data_offset < rec_offset + HAMMER_RECORD_SIZE) {
		if (data_offset + data_len > rec_offset + HAMMER_RECORD_SIZE) {
			kprintf("recover record: bad offset: overlapping3\n");
			Debugger("BAD OFFSET - OVERLAP3");
			return(EINVAL);
		}
		goto done;
	}

	KKASSERT(cluster->io.modified);
	/*
	 * Recover the allocated data either out of the cluster's master alist
	 * or as a buffer sub-allocation.
	 */
	if ((data_len & HAMMER_BUFMASK) == 0) {
		if (data_offset & HAMMER_BUFMASK) {
			kprintf("recover record: bad offset: unaligned\n");
			Debugger("BAD OFFSET - UNALIGNED");
			return(EINVAL);
		}
		nblks = data_len / HAMMER_BUFSIZE;
		dbuf_no = data_offset / HAMMER_BUFSIZE;
		/* XXX power-of-2 check data_len */

		r = hammer_alist_alloc_fwd(&cluster->alist_master,
					   nblks, dbuf_no);
		if (r == HAMMER_ALIST_BLOCK_NONE) {
			kprintf("recover record: cannot recover offset1\n");
			Debugger("CANNOT ALLOC DATABUFFER");
			return(EINVAL);
		}
		if (r != dbuf_no) {
			kprintf("recover record: cannot recover offset2\n");
			hammer_alist_free(&cluster->alist_master, r, nblks);
			KKASSERT(0);
			return(EINVAL);
		}
		++cluster->ondisk->stat_data_bufs;
	} else {
		if ((data_offset & ~HAMMER_BUFMASK) !=
		    ((data_offset + data_len - 1) & ~HAMMER_BUFMASK)) {
			kprintf("recover record: overlaps multiple bufs\n");
			Debugger("OVERLAP MULT");
			return(EINVAL);
		}
		if ((data_offset & HAMMER_BUFMASK) <
		    sizeof(struct hammer_fsbuf_head)) {
			kprintf("recover record: data in header area\n");
			Debugger("DATA IN HEADER AREA");
			return(EINVAL);
		}
		if (data_offset & HAMMER_DATA_BLKMASK) {
			kprintf("recover record: data blk unaligned\n");
			Debugger("DATA BLK UNALIGNED");
			return(EINVAL);
		}

		/*
		 * Ok, recover the space in the data buffer.
		 */
		dbuf_no = data_offset / HAMMER_BUFSIZE;
		r = hammer_alist_alloc_fwd(&cluster->alist_master, 1, dbuf_no);
		if (r != dbuf_no && r != HAMMER_ALIST_BLOCK_NONE)
			hammer_alist_free(&cluster->alist_master, r, 1);
		if (r == dbuf_no) {
			/*
			 * This is the first time we've tried to recover
			 * data in this data buffer, reinit it (but don't
			 * zero it out, obviously).
			 *
			 * Calling initbuffer marks the data blocks within
			 * the buffer as being all-allocated.  We have to
			 * mark it free.
			 */
			dbuf = hammer_get_buffer(cluster, dbuf_no,
						 0, &error);
			if (error == 0) {
				KKASSERT(dbuf->buf_type == HAMMER_FSBUF_DATA);
				hammer_modify_buffer(dbuf);
				hammer_initbuffer(&dbuf->alist,	
						  &dbuf->ondisk->head,
						  HAMMER_FSBUF_DATA);
				/*dbuf->buf_type = HAMMER_FSBUF_DATA;*/
				base_blk = dbuf_no * HAMMER_FSBUF_MAXBLKS;
				hammer_alist_free(&cluster->alist_mdata,
						  base_blk,
						  HAMMER_DATA_NODES);
				kprintf("FREE DATA %d/%d\n", base_blk, HAMMER_DATA_NODES);
				++cluster->ondisk->stat_data_bufs;
			}
		} else {
			/*
			 * We've seen this data buffer before.
			 */
			dbuf = hammer_get_buffer(cluster, dbuf_no,
						 0, &error);
		}
		if (error) {
			kprintf("recover record: data: getbuf failed\n");
			KKASSERT(0);
			return(EINVAL);
		}

		if (dbuf->buf_type != HAMMER_FSBUF_DATA) {
			hammer_rel_buffer(dbuf, 0);
			kprintf("recover record: data: wrong buffer type\n");
			KKASSERT(0);
			return(EINVAL);
		}

		/*
		 * Figure out the data block number and number of blocks.
		 */
		nblks = (data_len + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
		nblks /= HAMMER_DATA_BLKSIZE;
		dblk_no = ((data_offset & HAMMER_BUFMASK) - offsetof(union hammer_fsbuf_ondisk, data.data)) / HAMMER_DATA_BLKSIZE;
		if ((data_offset & HAMMER_BUFMASK) != offsetof(union hammer_fsbuf_ondisk, data.data[dblk_no])) {
			kprintf("dblk_no %d does not match data_offset %d/%d\n",
				dblk_no,
				offsetof(union hammer_fsbuf_ondisk, data.data[dblk_no]),
				(data_offset & HAMMER_BUFMASK));
			hammer_rel_buffer(dbuf, 0);
			kprintf("recover record: data: not block aligned\n");
			Debugger("bad data");
			return(EINVAL);
		}
		hammer_modify_buffer(dbuf);
		dblk_no += dbuf_no * HAMMER_FSBUF_MAXBLKS;
		r = hammer_alist_alloc_fwd(&cluster->alist_mdata, nblks, dblk_no);
		if (r != dblk_no) {
			if (r != HAMMER_ALIST_BLOCK_NONE)
				hammer_alist_free(&cluster->alist_mdata, r, nblks);
			hammer_rel_buffer(dbuf, 0);
			kprintf("recover record: data: unable to realloc dbuf %d dblk %d\n", dbuf_no, dblk_no % HAMMER_FSBUF_MAXBLKS);
			KKASSERT(0);
			return(EINVAL);
		}
		hammer_rel_buffer(dbuf, 0);
	}
done:
	return(0);
}

/*
 * Rebuild the B-Tree for the records residing in the specified buffer.
 *
 * Return the number of records recovered.
 */
static int
hammer_recover_buffer_stage2(hammer_cluster_t cluster, int32_t buf_no)
{
	hammer_record_ondisk_t rec;
	hammer_buffer_t buffer;
	int32_t rec_no;
	int32_t rec_offset;
	int record_count = 0;
	int error;

	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (error) {
		/*
		 * If we are unable to access the buffer leave it in a
		 * reserved state on the master alist.
		 */
		kprintf("hammer_recover_buffer_stage2: error "
			"recovering %d:%d:%d\n",
			cluster->volume->vol_no, cluster->clu_no, buf_no);
		Debugger("RECOVER BUFFER STAGE2 FAIL");
		return(0);
	}

	/*
	 * Recover the buffer, scan and validate allocated records.  Records
	 * which cannot be recovered are freed.
	 */
	rec_no = 0;
	for (;;) {
		rec_no = hammer_alist_find(&buffer->alist, rec_no,
					   HAMMER_RECORD_NODES, 0);
		if (rec_no == HAMMER_ALIST_BLOCK_NONE)
			break;
		rec_offset = offsetof(union hammer_fsbuf_ondisk,
				      record.recs[rec_no]);
		rec_offset += buf_no * HAMMER_BUFSIZE;
		rec = &buffer->ondisk->record.recs[rec_no];
		error = hammer_recover_btree(cluster, buffer, rec_offset, rec);
		if (error) {
			kprintf("hammer_recover_btree: failed %d:%d@%08x "
				"error %d buffer %p rec %p rec_no %d "
				" cluster_free %d\n",
				cluster->clu_no, buffer->buf_no, rec_offset,
				error, buffer, rec, rec_no,
				cluster->alist_master.meta->bm_alist_freeblks
			);
			Debugger("recover_btree failed");
			/* XXX free the record and its data? */
			/*hammer_alist_free(&buffer->alist, rec_no, 1);*/
		} else {
			++record_count;
		}
		++rec_no;
	}
	hammer_rel_buffer(buffer, 0);
	return(record_count);
}

/*
 * Enter a single record into the B-Tree.
 */
static int
hammer_recover_btree(hammer_cluster_t cluster, hammer_buffer_t buffer,
		      int32_t rec_offset, hammer_record_ondisk_t rec)
{
	struct hammer_cursor cursor;
	union hammer_btree_elm elm;
	hammer_cluster_t ncluster;
	int error = 0;

	/*
	 * Check for a spike record.  When spiking into a new cluster do
	 * NOT allow a recursive recovery to occur.  We use a lot of 
	 * stack and the only thing we actually modify in the target
	 * cluster is its parent pointer.
	 */
	if (rec->base.base.rec_type == HAMMER_RECTYPE_CLUSTER) {
		hammer_volume_t ovolume = cluster->volume;
		hammer_volume_t nvolume;

		nvolume = hammer_get_volume(ovolume->hmp, rec->spike.vol_no,
					    &error);
		if (error) {
			Debugger("recover_btree1");
			return(error);
		}
		ncluster = hammer_get_cluster(nvolume, rec->spike.clu_no,
					      &error, GET_CLUSTER_NORECOVER);
		hammer_rel_volume(nvolume, 0);
		if (error) {
			Debugger("recover_btree2");
			return(error);
		}

		/*
		 * Validate the cluster.  Allow the offset to be fixed up.
		 */
		if (ncluster->ondisk->clu_btree_parent_vol_no != ovolume->vol_no ||
		    ncluster->ondisk->clu_btree_parent_clu_no != cluster->clu_no) {
			kprintf("hammer_recover: Bad cluster spike hookup: "
				"%d:%d != %d:%d\n",
				ncluster->ondisk->clu_btree_parent_vol_no,
				ncluster->ondisk->clu_btree_parent_clu_no,
				ovolume->vol_no,
				cluster->clu_no);
			error = EINVAL;
			hammer_rel_cluster(ncluster, 0);
			Debugger("recover_btree3");
			return(error);
		}
	} else {
		ncluster = NULL;
	}

	/*
	 * Locate the insertion point.  Note that we are using the cluster-
	 * localized cursor init so parent will start out NULL.
	 *
	 * The key(s) used for spike's are bounds and different from the
	 * key embedded in the spike record.  A special B-Tree insertion
	 * call is made to deal with spikes.
	 */
	error = hammer_init_cursor_cluster(&cursor, cluster);
	if (error) {
		Debugger("recover_btree6");
		goto failed;
	}
	KKASSERT(cursor.node);
	if (ncluster)
		cursor.key_beg = ncluster->ondisk->clu_btree_beg;
	else
		cursor.key_beg = rec->base.base;
	cursor.flags |= HAMMER_CURSOR_INSERT | HAMMER_CURSOR_RECOVER;

	error = hammer_btree_lookup(&cursor);
	KKASSERT(error != EDEADLK);
	KKASSERT(cursor.node);
	if (error == 0) {
		kprintf("hammer_recover_btree: Duplicate record cursor %p rec %p ncluster %p\n",
			&cursor, rec, ncluster);
		hammer_print_btree_elm(&cursor.node->ondisk->elms[cursor.index], HAMMER_BTREE_TYPE_LEAF, cursor.index);
		Debugger("duplicate record");
	}
	if (error != ENOENT) {
		Debugger("recover_btree5");
		goto failed;
	}


	if (ncluster) {
		/*
		 * Spike record
		 */
		kprintf("recover spike clu %d %016llx-%016llx clusterfree %d\n",
			ncluster->clu_no,
			ncluster->ondisk->clu_btree_beg.obj_id,
			ncluster->ondisk->clu_btree_end.obj_id,
			cluster->alist_master.meta->bm_alist_freeblks);
		error = hammer_btree_insert_cluster(&cursor, ncluster,
						    rec_offset);
		kprintf("recover spike record error %d clusterfree %d\n",
			error, 
			cluster->alist_master.meta->bm_alist_freeblks);
		KKASSERT(error != EDEADLK);
		if (error)
			Debugger("spike recovery");
	} else {
		/*
		 * Normal record
		 */
#if 0
		kprintf("recover recrd clu %d %016llx\n",
			cluster->clu_no, rec->base.base.obj_id);
#endif
		elm.leaf.base = rec->base.base;
		elm.leaf.rec_offset = rec_offset;
		elm.leaf.data_offset = rec->base.data_offset;
		elm.leaf.data_len = rec->base.data_len;
		elm.leaf.data_crc = rec->base.data_crc;

		error = hammer_btree_insert(&cursor, &elm);
		KKASSERT(error != EDEADLK);
	}

	/*
	 * Success if error is 0!
	 */
	if (error == 0) {
		/*
		 * Update the cluster header's statistics count.  stat_records
		 * is very important for proper reservation of B-Tree space.
		 * Note that a spike record counts as 2.
		 */
		++cluster->ondisk->stat_records;
		if (rec->base.base.rec_type == HAMMER_RECTYPE_INODE)
			++cluster->ondisk->stat_inodes;
		if (rec->base.base.rec_type == HAMMER_RECTYPE_CLUSTER)
			++cluster->ondisk->stat_records;
	}
	if (error) {
		kprintf("hammer_recover_btree: insertion failed\n");
	}

failed:
	if (ncluster)
		hammer_rel_cluster(ncluster, 0);
	hammer_done_cursor(&cursor);
	return(error);
}

