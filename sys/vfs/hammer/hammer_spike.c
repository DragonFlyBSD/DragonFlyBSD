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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammer_spike.c,v 1.15 2008/02/08 08:31:00 dillon Exp $
 */

#include "hammer.h"

#if 0

/*
 * Load spike info given a cursor.  The cursor must point to the leaf node
 * that needs to be spiked after a failed insertion.
 */
void
hammer_load_spike(hammer_cursor_t cursor, struct hammer_cursor **spikep)
{
	hammer_cursor_t spike;

	KKASSERT(cursor->node->ondisk->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(*spikep == NULL);
	*spikep = spike = kmalloc(sizeof(*spike), M_HAMMER, M_WAITOK|M_ZERO);
	++hammer_count_spikes;

	spike->parent = cursor->parent;
	spike->parent_index = cursor->parent_index;
	spike->node = cursor->node;
	spike->index = cursor->index;
	spike->left_bound = cursor->left_bound;
	spike->right_bound = cursor->right_bound;
	spike->key_beg = cursor->key_beg;

	if (spike->parent) {
		hammer_ref_node(spike->parent);
		hammer_lock_sh(&spike->parent->lock);
	}
	hammer_ref_node(spike->node);
	hammer_lock_sh(&spike->node->lock);
	if (hammer_debug_general & 0x40)
		kprintf("LOAD SPIKE %p\n", spike);
}

/*
 * Spike code - make room in a cluster by spiking in a new cluster.
 *
 * The spike structure contains a locked and reference B-Tree leaf node.
 * The spike at a minimum must move the contents of the leaf into a
 * new cluster and replace the leaf with two elements representing the
 * SPIKE_BEG and SPIKE_END.
 *
 * Various optimizations are desireable, including merging the spike node
 * with an adjacent node that has already been spiked, if its cluster is
 * not full, or promoting the spike node to the parent cluster of the current
 * cluster when it represents the right hand boundary leaf node in the
 * cluster (to avoid append chains).
 */
int
hammer_spike(struct hammer_cursor **spikep)
{
	hammer_cursor_t spike;
	struct hammer_cursor ncursor;
	hammer_cluster_t ocluster;
	hammer_cluster_t ncluster;
	hammer_node_ondisk_t ondisk;
	hammer_btree_elm_t elm;
	hammer_node_t onode;
	hammer_record_ondisk_t rec;
	hammer_node_locklist_t locklist = NULL;
	int error;
	int b, e;
	const int esize = sizeof(*elm);

	if (hammer_debug_general & 0x40)
		kprintf("hammer_spike: ENOSPC in cluster, spiking\n");
	/*Debugger("ENOSPC");*/

	/*
	 * Validate and lock the spike.  If this fails due to a deadlock
	 * we still return 0 since a spike is only called when the
	 * caller intends to retry the operation.
	 */
	spike = *spikep;
	KKASSERT(spike != NULL);
	KKASSERT(spike->parent &&
		 spike->parent->cluster == spike->node->cluster);
	KKASSERT(spike->node->ondisk->type == HAMMER_BTREE_TYPE_LEAF);

	error = hammer_cursor_upgrade(spike);
	if (error) {
		error = 0;
		goto failed4;
	}

	/*
	 * Our leaf may contain spikes.  We have to lock the root node
	 * in each target cluster.
	 */
	error = hammer_btree_lock_children(spike, &locklist);
	if (error) {
		error = 0;
		goto failed4;
	}

	onode = spike->node;
	ocluster = onode->cluster;
	ondisk = onode->ondisk;
	hammer_lock_ex(&ocluster->io.lock);

	/*
	 * Calculate the range of elements in the leaf that we will push
	 * down into our spike.  For the moment push them all down.
	 */
	b = 0;
	e = ondisk->count;

	/*
	 * Use left-bound for spike if b == 0, else use the base element
	 * for the item to the left and adjust it past one unit.
	 */
	if (b == 0) {
		spike->key_beg = *spike->left_bound;
	} else {
		spike->key_beg = ondisk->elms[b-1].leaf.base;
		if (spike->key_beg.create_tid != 0) {
			++spike->key_beg.create_tid;
		} else if (spike->key_beg.key != HAMMER_MAX_KEY) {
			++spike->key_beg.key;
			spike->key_beg.create_tid = 1;
		} else if (spike->key_beg.rec_type != HAMMER_MAX_RECTYPE) {
			++spike->key_beg.rec_type;
			spike->key_beg.key = HAMMER_MIN_KEY;
			spike->key_beg.create_tid = 1;
		} else if (spike->key_beg.obj_id != HAMMER_MAX_OBJID) {
			++spike->key_beg.obj_id;
			spike->key_beg.key = HAMMER_MIN_KEY;
			spike->key_beg.create_tid = 1;
			spike->key_beg.rec_type = HAMMER_MIN_RECTYPE;
		} else {
			panic("hammer_spike: illegal key");
		}
		KKASSERT(hammer_btree_cmp(&ondisk->elms[b-1].base, &spike->key_beg) < 0);
	}

	/*
	 * Use the right-bound if e is terminal, otherwise use the element
	 * at [e].  key_end is exclusive for the call to hammer_init_cluster()
	 * and is then made inclusive later to construct the SPIKE_END
	 * element.
	 */
	if (e == ondisk->count)
		spike->key_end = *spike->right_bound;
	else
		spike->key_end = ondisk->elms[e].leaf.base;

	/*
	 * Heuristic:  Attempt to size the spike range according to
	 * expected traffic.  This is primarily responsible for the
	 * initial layout of the filesystem.
	 */
	if (e && b != e) {
		int32_t clsize = ocluster->volume->ondisk->vol_clsize;
		int64_t delta = 1000000000;
		int64_t dkey;

		elm = &ondisk->elms[e-1];
		if (elm->base.obj_id == spike->key_end.obj_id &&
		    elm->base.rec_type == spike->key_end.rec_type) {
			/* 
			 * NOTE: dkey can overflow.
			 */
			dkey = elm->base.key + clsize;
			if (dkey > elm->base.key && dkey < spike->key_end.key)
				spike->key_end.key = elm->base.key + clsize;
		} else if (elm->base.obj_id + delta < spike->key_end.obj_id) {
			spike->key_end.obj_id = elm->base.obj_id + delta;
		}
	}

	/*
	 * Allocate and lock a new cluster, initialize its bounds.
	 */
	ncluster = hammer_alloc_cluster(ocluster->volume->hmp, ocluster,
					&error);
	if (ncluster == NULL)
		goto failed3;
	hammer_init_cluster(ncluster, &spike->key_beg, &spike->key_end);

	/*
	 * Get a cursor for the new cluster.  Operations will be limited to
	 * this cluster.  Set HAMMER_CURSOR_RECOVER to force internal
	 * boundary elements in a way that allows us to copy spikes.
	 */
	error = hammer_init_cursor_cluster(&ncursor, ncluster);
	if (error)
		goto failed1;
	ncursor.flags |= HAMMER_CURSOR_INSERT | HAMMER_CURSOR_RECOVER;

	/*
	 * Copy the elements in the leaf node to the new target cluster.
	 */
	for (spike->index = b; spike->index < e; ++spike->index) {
		elm = &onode->ondisk->elms[spike->index];

		if (elm->leaf.base.btype == HAMMER_BTREE_TYPE_SPIKE_END)
			continue;
		error = hammer_btree_extract(spike,
					     HAMMER_CURSOR_GET_RECORD |
					     HAMMER_CURSOR_GET_DATA);
		if (error == 0) {
			ncursor.key_beg = elm->leaf.base;
			error = hammer_write_record(&ncursor, spike->record,
						    spike->data, spike->flags);
		}

		KKASSERT(error != EDEADLK);
		if (error == ENOSPC) {
			kprintf("impossible ENOSPC error on spike\n");
			error = EIO;
		}
		if (error)
			goto failed1;
	}

	/*
	 * Delete the records and data associated with the old leaf node,
	 * replacing them with the spike elements.
	 *
	 * XXX I/O ordering issue, we're destroying these records too
	 * early, but we need one for the spike allocation.  What to do?
	 */
	for (spike->index = b; spike->index < e; ++spike->index) {
		int32_t roff;
		u_int8_t rec_type;

		elm = &onode->ondisk->elms[spike->index];
		if (elm->leaf.base.btype == HAMMER_BTREE_TYPE_SPIKE_BEG)
			continue;
		KKASSERT(elm->leaf.rec_offset > 0);
		if (elm->leaf.base.btype == HAMMER_BTREE_TYPE_RECORD)
			rec_type = elm->leaf.base.rec_type;
		else
			rec_type = HAMMER_RECTYPE_CLUSTER;
		hammer_free_record(ocluster, elm->leaf.rec_offset, rec_type);
		if (elm->leaf.base.btype == HAMMER_BTREE_TYPE_RECORD &&
		    elm->leaf.data_offset) {
			roff = elm->leaf.data_offset - elm->leaf.rec_offset;
			if (roff < 0 || roff >= HAMMER_RECORD_SIZE) {
				hammer_free_data(ocluster,
						 elm->leaf.data_offset,
						 elm->leaf.data_len);
			}
		}
	}

	/*
	 * Add a record representing the spike using space freed up by the
	 * above deletions.
	 */
	rec = hammer_alloc_record(ocluster, &error,
				  HAMMER_RECTYPE_CLUSTER,
				  &spike->record_buffer);
	KKASSERT(error == 0);
	rec->spike.base.base.btype = HAMMER_BTREE_TYPE_RECORD;
	rec->spike.base.base.rec_type = HAMMER_RECTYPE_CLUSTER;
	rec->spike.base.rec_id = hammer_alloc_recid(ocluster);
	rec->spike.clu_no = ncluster->clu_no;
	rec->spike.vol_no = ncluster->volume->vol_no;
	rec->spike.clu_id = 0;

	/*
	 * Construct the spike elements.  Note that the right boundary
	 * is range-exclusive whereas the SPIKE_END must be range-inclusive.
	 */
	hammer_modify_node(onode);
	ondisk = onode->ondisk;
	elm = &ondisk->elms[b];

	if (e - b != 2)
		bcopy(&elm[e - b], &elm[2], (ondisk->count - e) * esize);
	ondisk->count = ondisk->count - (e - b) + 2;

	elm[0].leaf.base = spike->key_beg;
	elm[0].leaf.base.btype = HAMMER_BTREE_TYPE_SPIKE_BEG;
	elm[0].leaf.rec_offset = hammer_bclu_offset(spike->record_buffer, rec);
	elm[0].leaf.spike_clu_no = ncluster->clu_no;
	elm[0].leaf.spike_vol_no = ncluster->volume->vol_no;
	elm[0].leaf.spike_unused01 = 0;

	elm[1].leaf.base = spike->key_end;
	elm[1].leaf.base.btype = HAMMER_BTREE_TYPE_SPIKE_END;
	elm[1].leaf.rec_offset = elm[0].leaf.rec_offset;
	elm[1].leaf.spike_clu_no = ncluster->clu_no;
	elm[1].leaf.spike_vol_no = ncluster->volume->vol_no;
	elm[1].leaf.spike_unused01 = 0;

	/*
	 * Make the SPIKE_END element inclusive.
	 */
	if (elm[1].leaf.base.create_tid != 1) {
		--elm[1].leaf.base.create_tid;
	} else if (elm[0].leaf.base.key != HAMMER_MIN_KEY) {
		--elm[0].leaf.base.key;
		elm[0].leaf.base.create_tid = 0; /* max value */
	} else if (elm[0].leaf.base.rec_type != HAMMER_MIN_RECTYPE) {
		--elm[0].leaf.base.rec_type;
		elm[0].leaf.base.key = HAMMER_MAX_KEY;
		elm[0].leaf.base.create_tid = 0; /* max value */
	} else if (elm[0].leaf.base.obj_id != HAMMER_MIN_OBJID) {
		--elm[0].leaf.base.obj_id;
		elm[0].leaf.base.rec_type = HAMMER_MAX_RECTYPE;
		elm[0].leaf.base.key = HAMMER_MAX_KEY;
		elm[0].leaf.base.create_tid = 0; /* max value */
	} else {
		panic("hammer_spike: illegal key");
	}

	/*
	 * Adjust ncluster
	 */
	{
		hammer_cluster_ondisk_t ondisk;

		hammer_modify_cluster(ncluster);
		ondisk = ncluster->ondisk;
		ondisk->clu_btree_parent_vol_no = ocluster->volume->vol_no;
		ondisk->clu_btree_parent_clu_no = ocluster->clu_no;
		ondisk->clu_btree_parent_offset = onode->node_offset;
		ondisk->clu_btree_parent_clu_gen = ocluster->ondisk->clu_gen;
	}

	/*
	 * XXX I/O dependancy - new cluster must be flushed before current
	 * cluster can be flushed.
	 */
	/*Debugger("COPY COMPLETE");*/
	hammer_done_cursor(&ncursor);
	goto success;

	/*
	 * Cleanup
	 */
failed1:
	hammer_done_cursor(&ncursor);
	hammer_free_cluster(ncluster);
success:
	hammer_unlock(&ncluster->io.lock);
	hammer_rel_cluster(ncluster, 0);
failed3:
	if (hammer_debug_general & 0x40)
		kprintf("UNLOAD SPIKE %p %d\n", spike, error);
	hammer_unlock(&ocluster->io.lock);
failed4:
	hammer_btree_unlock_children(&locklist);
	hammer_done_cursor(spike);
	--hammer_count_spikes;
	kfree(spike, M_HAMMER);
	*spikep = NULL;
	return (error);
}


#endif
