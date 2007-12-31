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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammer_spike.c,v 1.4 2007/12/31 05:33:12 dillon Exp $
 */

#include "hammer.h"

/*
 * Load spike info given a cursor.  The cursor must point to the leaf node
 * that needs to be spiked.
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

	if (spike->parent) {
		hammer_ref_node(spike->parent);
		hammer_lock_ex(&spike->parent->lock);
	}
	hammer_ref_node(spike->node);
	hammer_lock_ex(&spike->node->lock);
	kprintf("LOAD SPIKE %p\n", spike);
}

/*
 * Spike code - make room in a cluster by spiking in a new cluster.
 *
 * The spike structure contains a locked and reference B-Tree leaf node.
 * The spike at a minimum must replace the node with a cluster reference
 * and then delete the contents of the node.
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
	hammer_node_t onode;
	int error;

	kprintf("hammer_spike: ENOSPC in cluster, spiking\n");
	/*Debugger("ENOSPC");*/

	/*
	 * Lock and flush the cluster XXX
	 */
	spike = *spikep;
	KKASSERT(spike != NULL);
	KKASSERT(spike->parent &&
		 spike->parent->cluster == spike->node->cluster);

	error = 0;
	onode = spike->node;
	ocluster = onode->cluster;
	hammer_lock_ex(&ocluster->io.lock);

	/* XXX */

	/*
	 * Allocate and lock a new cluster, initialize its bounds.
	 */
	ncluster = hammer_alloc_cluster(ocluster->volume->hmp, ocluster,
					&error);
	if (ncluster == NULL)
		goto failed3;
	hammer_init_cluster(ncluster, spike->left_bound, spike->right_bound);

	/*
	 * Get a cursor for the new cluster.  Operations will be limited to
	 * this cluster.
	 */
	error = hammer_init_cursor_cluster(&ncursor, ncluster);
	if (error)
		goto failed2;

	/*
	 * Copy the elements in the leaf node.
	 */
	for (spike->index = 0; spike->index < onode->ondisk->count; 
	     ++spike->index) {
		error = hammer_btree_extract(spike, HAMMER_CURSOR_GET_RECORD |
						    HAMMER_CURSOR_GET_DATA);
		if (error)
			goto failed1;
		kprintf("EXTRACT %04x %016llx %016llx\n",
			spike->record->base.base.rec_type,
			spike->record->base.base.obj_id,
			spike->record->base.base.key);
		error = hammer_write_record(&ncursor, spike->record,
					    spike->data, spike->flags);
		if (error == ENOSPC) {
			kprintf("impossible ENOSPC error on spike\n");
			error = EIO;
		}
		if (error)
			goto failed1;
	}

	/*
	 * Success!  Replace the parent reference to the leaf node with a
	 * parent reference to the new cluster and fixup the new cluster's
	 * parent reference.  This completes the spike operation.
	 */
	{
		hammer_node_ondisk_t ondisk;
		hammer_btree_elm_t elm;
		
		hammer_modify_node(spike->parent);
		ondisk = spike->parent->ondisk;
		elm = &ondisk->elms[spike->parent_index];
		elm->internal.subtree_type = HAMMER_BTREE_TYPE_CLUSTER;
		elm->internal.rec_offset = -1; /* XXX */
		elm->internal.subtree_clu_no = ncluster->clu_no;
		elm->internal.subtree_vol_no = ncluster->volume->vol_no;
		elm->internal.subtree_count = onode->ondisk->count; /*XXX*/
		hammer_modify_node_done(spike->parent);
		onode->flags |= HAMMER_NODE_MODIFIED;
		hammer_flush_node(onode);
	}
	{
		hammer_cluster_ondisk_t ondisk;

		hammer_modify_cluster(ncluster);
		ondisk = ncluster->ondisk;
		ondisk->clu_btree_parent_vol_no = ocluster->volume->vol_no;
		ondisk->clu_btree_parent_clu_no = ocluster->clu_no;
		ondisk->clu_btree_parent_offset = spike->parent->node_offset;
		ondisk->clu_btree_parent_clu_gen = ocluster->ondisk->clu_gen;
		hammer_modify_cluster_done(ncluster);
	}

	/*
	 * Delete the records and data associated with the old leaf node,
	 * then free the old leaf node (nothing references it any more).
	 */
	for (spike->index = 0; spike->index < onode->ondisk->count; 
	     ++spike->index) {
		hammer_btree_elm_t elm;
		int32_t roff;

		elm = &onode->ondisk->elms[spike->index];
		KKASSERT(elm->leaf.rec_offset > 0);
		hammer_free_record(ocluster, elm->leaf.rec_offset);
		if (elm->leaf.data_offset) {
			roff = elm->leaf.data_offset - elm->leaf.rec_offset;
			if (roff < 0 || roff >= HAMMER_RECORD_SIZE) {
				hammer_free_data(ocluster,
						 elm->leaf.data_offset,
						 elm->leaf.data_len);
			}
		}
	}
	onode->flags |= HAMMER_NODE_DELETED;

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
failed2:
	hammer_free_cluster(ncluster);
success:
	hammer_unlock(&ncluster->io.lock);
	hammer_rel_cluster(ncluster, 0);
failed3:
	kprintf("UNLOAD SPIKE %p %d\n", spike, error);
	hammer_unlock(&ocluster->io.lock);
	hammer_done_cursor(spike);
	--hammer_count_spikes;
	kfree(spike, M_HAMMER);
	*spikep = NULL;
	return (error);
}

