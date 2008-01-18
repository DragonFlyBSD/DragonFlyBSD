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
 * $DragonFly: src/sys/vfs/hammer/hammer_cursor.c,v 1.14 2008/01/18 07:02:41 dillon Exp $
 */

/*
 * HAMMER B-Tree index - cursor support routines
 */
#include "hammer.h"

static int hammer_load_cursor_parent(hammer_cursor_t cursor);
static int hammer_load_cursor_parent_local(hammer_cursor_t cursor);
static int hammer_load_cursor_parent_cluster(hammer_cursor_t cursor);

/*
 * Initialize a fresh cursor using the B-Tree node cache.  If the cache
 * is not available initialize a fresh cursor at the root of the root
 * cluster.
 */
int
hammer_init_cursor_hmp(hammer_cursor_t cursor, struct hammer_node **cache,
		       hammer_mount_t hmp)
{
	hammer_cluster_t cluster;
	hammer_node_t node;
	int error;

	bzero(cursor, sizeof(*cursor));

	/*
	 * Step 1 - acquire a locked node from the cache if possible
	 */
	if (cache && *cache) {
		node = hammer_ref_node_safe(hmp, cache, &error);
		if (error == 0) {
			hammer_lock_sh(&node->lock);
			if (node->flags & HAMMER_NODE_DELETED) {
				hammer_unlock(&node->lock);
				hammer_rel_node(node);
				node = NULL;
			}
		}
	} else {
		node = NULL;
	}

	/*
	 * Step 2 - If we couldn't get a node from the cache, get
	 * the one from the root of the root cluster.
	 */
	while (node == NULL) {
		cluster = hammer_get_root_cluster(hmp, &error);
		if (error)
			break;
		node = hammer_get_node(cluster,
				       cluster->ondisk->clu_btree_root,
				       &error);
		hammer_rel_cluster(cluster, 0);
		if (error)
			break;
		hammer_lock_sh(&node->lock);
		if (node->flags & HAMMER_NODE_DELETED) {
			hammer_unlock(&node->lock);
			hammer_rel_node(node);
			node = NULL;
		}
	}

	/*
	 * Step 3 - finish initializing the cursor by acquiring the parent
	 */
	cursor->node = node;
	if (error == 0)
		error = hammer_load_cursor_parent(cursor);
	KKASSERT(error == 0);
	return(error);
}

/*
 * Initialize a fresh cursor at the root of the specified cluster and
 * limit operations to within the cluster.
 */
int
hammer_init_cursor_cluster(hammer_cursor_t cursor, hammer_cluster_t cluster)
{
	int error;

	bzero(cursor, sizeof(*cursor));
	cursor->flags |= HAMMER_CURSOR_INCLUSTER;
	cursor->node = hammer_get_node(cluster,
				       cluster->ondisk->clu_btree_root,
				       &error);
	if (error == 0) {
		hammer_lock_sh(&cursor->node->lock);
		error = hammer_load_cursor_parent(cursor);
	}
	KKASSERT(error == 0);
	return(error);
}

/*
 * We are finished with a cursor.  We NULL out various fields as sanity
 * check, in case the structure is inappropriately used afterwords.
 */
void
hammer_done_cursor(hammer_cursor_t cursor)
{
	if (cursor->parent) {
		hammer_unlock(&cursor->parent->lock);
		hammer_rel_node(cursor->parent);
		cursor->parent = NULL;
	}
	if (cursor->node) {
		hammer_unlock(&cursor->node->lock);
		hammer_rel_node(cursor->node);
		cursor->node = NULL;
	}
        if (cursor->data_buffer) {
                hammer_rel_buffer(cursor->data_buffer, 0);
                cursor->data_buffer = NULL;
        }
        if (cursor->record_buffer) {
                hammer_rel_buffer(cursor->record_buffer, 0);
                cursor->record_buffer = NULL;
        }
	if (cursor->ip)
		hammer_mem_done(cursor);

	/*
	 * If we deadlocked this node will be referenced.  Do a quick
	 * lock/unlock to wait for the deadlock condition to clear.
	 */
	if (cursor->deadlk_node) {
		hammer_lock_ex(&cursor->deadlk_node->lock);
		hammer_unlock(&cursor->deadlk_node->lock);
		hammer_rel_node(cursor->deadlk_node);
		cursor->deadlk_node = NULL;
	}

	cursor->data = NULL;
	cursor->record = NULL;
	cursor->left_bound = NULL;
	cursor->right_bound = NULL;
}

/*
 * Upgrade cursor->node and cursor->parent to exclusive locks.  This
 * function can return EDEADLK.
 *
 * If we fail to upgrade the lock and cursor->deadlk_node is NULL, 
 * we add another reference to the node that failed and set
 * cursor->deadlk_node so hammer_done_cursor() can block on it.
 */
int
hammer_cursor_upgrade(hammer_cursor_t cursor)
{
	int error;

	if (hammer_lock_held(&cursor->node->lock) < 0) {
		error = hammer_lock_upgrade(&cursor->node->lock);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->node;
			hammer_ref_node(cursor->deadlk_node);
		}
	} else {
		error = 0;
	}
	if (cursor->parent && hammer_lock_held(&cursor->parent->lock) < 0) {
		error = hammer_lock_upgrade(&cursor->parent->lock);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->parent;
			hammer_ref_node(cursor->deadlk_node);
		}
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Downgrade cursor->node and cursor->parent to shared locks.  This
 * function can return EDEADLK.
 */
void
hammer_cursor_downgrade(hammer_cursor_t cursor)
{
	if (hammer_lock_held(&cursor->node->lock) > 0)
		hammer_lock_downgrade(&cursor->node->lock);
	if (cursor->parent && hammer_lock_held(&cursor->parent->lock) > 0)
		hammer_lock_downgrade(&cursor->parent->lock);
}


#if 0

/*
 * Acquire the parent B-Tree node of the specified node, returning a
 * referenced but unlocked node.  NULL can be returned with *errorp == 0
 * if node is the root node of the root cluster.
 */
static
hammer_node_t
hammer_get_parent_node(hammer_node_t node, int *errorp)
{
	hammer_cluster_t cluster;
	hammer_node_t parent;

	cluster = node->cluster;
	if (node->ondisk->parent) {
		/*
		 * Local parent
		 */
		parent = hammer_get_node(cluster, node->ondisk->parent, errorp);
	} else if (cluster->ondisk->clu_btree_parent_vol_no >= 0) {
		/*
		 * At cluster root, locate node in parent cluster
		 */
		hammer_cluster_ondisk_t ondisk;
		hammer_cluster_t pcluster;
		hammer_volume_t pvolume;
		int32_t clu_no;
		int32_t vol_no;

		ondisk = cluster->ondisk;
		vol_no = ondisk->clu_btree_parent_vol_no;
		clu_no = ondisk->clu_btree_parent_clu_no;

		/*
		 * Acquire the node from (volume, cluster, offset)
		 */
		pvolume = hammer_get_volume(cluster->volume->hmp, vol_no,
					    errorp);
		if (*errorp)
			return (NULL);
		pcluster = hammer_get_cluster(pvolume, clu_no, errorp, 0);
		hammer_rel_volume(pvolume, 0);
		if (*errorp)
			return (NULL);
		parent = hammer_get_node(pcluster,
					 ondisk->clu_btree_parent_offset,
					 errorp);
		hammer_rel_cluster(pcluster, 0);
	} else {
		/*
		 * At root of root cluster, there is no parent.
		 */
		KKASSERT(cluster->ondisk->clu_btree_parent_vol_no == -1);
		parent = NULL;
		*errorp = 0;
	}
	return(parent);
}

#endif

/*
 * Load the parent of cursor->node into cursor->parent.  There are several
 * cases.  (1) The parent is in the current cluster.  (2) We are at the
 * root of the cluster and the parent is in another cluster.  (3) We are at
 * the root of the root cluster.
 *
 * If HAMMER_CURSOR_INCLUSTER is set and we are at the root of the cluster,
 * we do not access the parent cluster at all and make the cursor look like
 * its at the root.
 */
static
int
hammer_load_cursor_parent(hammer_cursor_t cursor)
{
	hammer_cluster_t cluster;
	int error;

	cluster = cursor->node->cluster;

	if (cursor->node->ondisk->parent) {
		error = hammer_load_cursor_parent_local(cursor);
	} else if (cluster->ondisk->clu_btree_parent_vol_no >= 0 &&
		   ((cursor->flags & HAMMER_CURSOR_INCLUSTER) == 0)
	) {
		error = hammer_load_cursor_parent_cluster(cursor);
	} else {
		cursor->parent = NULL;
		cursor->parent_index = 0;
		cursor->left_bound = &cluster->ondisk->clu_btree_beg;
		cursor->right_bound = &cluster->ondisk->clu_btree_end;
		error = 0;
	}
	return(error);
}

static
int
hammer_load_cursor_parent_local(hammer_cursor_t cursor)
{
	hammer_node_t node;
	hammer_node_t parent;
	hammer_btree_elm_t elm;
	int error;
	int i;

	node = cursor->node;
	parent = hammer_get_node(node->cluster, node->ondisk->parent, &error);
	if (error)
		return(error);
	elm = NULL;
	for (i = 0; i < parent->ondisk->count; ++i) {
		elm = &parent->ondisk->elms[i];
		if (parent->ondisk->elms[i].internal.subtree_offset ==
		    node->node_offset) {
			break;
		}
	}
	if (i == parent->ondisk->count)
		panic("Bad B-Tree link: parent %p node %p\n", parent, node);
	KKASSERT(i != parent->ondisk->count);
	cursor->parent = parent;
	cursor->parent_index = i;
	cursor->left_bound = &elm[0].internal.base;
	cursor->right_bound = &elm[1].internal.base;

	hammer_lock_sh(&parent->lock);
	return(error);
}

static
int
hammer_load_cursor_parent_cluster(hammer_cursor_t cursor)
{
	hammer_cluster_ondisk_t ondisk;
	hammer_cluster_t pcluster;
	hammer_cluster_t ccluster;
	hammer_volume_t volume;
	hammer_node_t node;
	hammer_node_t parent;
	hammer_btree_elm_t elm;
	int32_t clu_no;
	int32_t vol_no;
	int error;
	int i;

	node = cursor->node;
	ccluster = node->cluster;
	ondisk = ccluster->ondisk;
	vol_no = ondisk->clu_btree_parent_vol_no;
	clu_no = ondisk->clu_btree_parent_clu_no;

	/*
	 * Acquire the node from (volume, cluster, offset).  This should
	 * be a leaf node containing the HAMMER_BTREE_TYPE_SPIKE_END element.
	 */
	volume = hammer_get_volume(ccluster->volume->hmp, vol_no, &error);
	if (error)
		return (error);
	pcluster = hammer_get_cluster(volume, clu_no, &error, 0);
	hammer_rel_volume(volume, 0);
	if (error)
		return (error);
	parent = hammer_get_node(pcluster, ondisk->clu_btree_parent_offset,
				 &error);
	hammer_rel_cluster(pcluster, 0);
	if (error)
		return (error);
	KKASSERT(parent->ondisk->type == HAMMER_BTREE_TYPE_LEAF);

	/* 
	 * Ok, we have the node.  Locate the inter-cluster element
	 */
	elm = NULL;
	for (i = 0; i < parent->ondisk->count; ++i) {
		elm = &parent->ondisk->elms[i];

		if (elm->leaf.base.btype == HAMMER_BTREE_TYPE_SPIKE_END &&
		    elm->leaf.spike_clu_no == cursor->node->cluster->clu_no) {
			break;
		}
	}
	KKASSERT(i != parent->ondisk->count);
	KKASSERT(i && elm[-1].leaf.base.btype == HAMMER_BTREE_TYPE_SPIKE_BEG);
	cursor->parent = parent;
	cursor->parent_index = i;
	cursor->left_bound = &ccluster->ondisk->clu_btree_beg;
	cursor->right_bound = &ccluster->ondisk->clu_btree_end;

	KKASSERT(hammer_btree_cmp(&elm[-1].leaf.base,
		 &ccluster->ondisk->clu_btree_beg) == 0);
	    /*
	     * spike_end is an inclusive boundary and will != clu_btree_end
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &ccluster->ondisk->clu_btree_end) >= 0);
	    */

	hammer_lock_sh(&parent->lock);
	return(0);
}

/*
 * Cursor up to our parent node.  Return ENOENT if we are at the root of
 * the root cluster.
 *
 * If doing a nonblocking cursor-up and we are unable to acquire the lock,
 * the cursor remains unchanged.
 */
int
hammer_cursor_up(hammer_cursor_t cursor)
{
	int error;

	hammer_cursor_downgrade(cursor);

	/*
	 * Set the node to its parent.  If the parent is NULL we are at
	 * the root of the root cluster and return ENOENT.
	 */
	hammer_unlock(&cursor->node->lock);
	hammer_rel_node(cursor->node);
	cursor->node = cursor->parent;
	cursor->index = cursor->parent_index;
	cursor->parent = NULL;
	cursor->parent_index = 0;

	if (cursor->node == NULL) {
		error = ENOENT;
	} else if ((cursor->flags & HAMMER_CURSOR_INCLUSTER) &&
		   cursor->node->ondisk->parent == 0
	) {
		error = ENOENT;
	} else {
		error = hammer_load_cursor_parent(cursor);
	}
	return(error);
}

/*
 * Set the cursor to the root of the current cursor's cluster.
 */
int
hammer_cursor_toroot(hammer_cursor_t cursor)
{
	hammer_cluster_t cluster;
	int error;

	/*
	 * Already at root of cluster?
	 */
	if (cursor->node->ondisk->parent == 0) 
		return (0);

	hammer_cursor_downgrade(cursor);

	/*
	 * Parent is root of cluster?
	 */
	if (cursor->parent->ondisk->parent == 0)
		return (hammer_cursor_up(cursor));

	/*
	 * Ok, reload the cursor with the root of the cluster, then
	 * locate its parent.
	 */
	cluster = cursor->node->cluster;
	error = hammer_ref_cluster(cluster);
	if (error)
		return (error);

	hammer_unlock(&cursor->parent->lock);
	hammer_rel_node(cursor->parent);
	hammer_unlock(&cursor->node->lock);
	hammer_rel_node(cursor->node);
	cursor->parent = NULL;
	cursor->parent_index = 0;

	cursor->node = hammer_get_node(cluster, cluster->ondisk->clu_btree_root,
				       &error);
	cursor->index = 0;
	hammer_lock_sh(&cursor->node->lock);
	hammer_rel_cluster(cluster, 0);
	if (error == 0)
		error = hammer_load_cursor_parent(cursor);
	return(error);
}

/*
 * Cursor down through the current node, which must be an internal node.
 *
 * This routine adjusts the cursor and sets index to 0.
 */
int
hammer_cursor_down(hammer_cursor_t cursor)
{
	hammer_node_t node;
	hammer_btree_elm_t elm;
	hammer_volume_t volume;
	hammer_cluster_t cluster;
	int32_t vol_no;
	int32_t clu_no;
	int error;

	/*
	 * The current node becomes the current parent
	 */
	hammer_cursor_downgrade(cursor);
	node = cursor->node;
	KKASSERT(cursor->index >= 0 && cursor->index < node->ondisk->count);
	if (cursor->parent) {
		hammer_unlock(&cursor->parent->lock);
		hammer_rel_node(cursor->parent);
	}
	cursor->parent = node;
	cursor->parent_index = cursor->index;
	cursor->node = NULL;
	cursor->index = 0;

	/*
	 * Extract element to push into at (node,index), set bounds.
	 */
	elm = &node->ondisk->elms[cursor->parent_index];

	/*
	 * Ok, push down into elm.  If elm specifies an internal or leaf
	 * node the current node must be an internal node.  If elm specifies
	 * a spike then the current node must be a leaf node.
	 *
	 * Cursoring down through a cluster transition when the INCLUSTER
	 * flag is set is not legal.
	 */
	switch(elm->base.btype) {
	case HAMMER_BTREE_TYPE_INTERNAL:
	case HAMMER_BTREE_TYPE_LEAF:
		KKASSERT(node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL);
		KKASSERT(elm->internal.subtree_offset != 0);
		cursor->left_bound = &elm[0].internal.base;
		cursor->right_bound = &elm[1].internal.base;
		node = hammer_get_node(node->cluster,
				       elm->internal.subtree_offset,
				       &error);
		if (error == 0) {
			KKASSERT(elm->base.btype == node->ondisk->type);
			if(node->ondisk->parent != cursor->parent->node_offset)
				kprintf("node %p %d vs %d\n", node, node->ondisk->parent, cursor->parent->node_offset);
			KKASSERT(node->ondisk->parent == cursor->parent->node_offset);
		}
		break;
	case HAMMER_BTREE_TYPE_SPIKE_BEG:
	case HAMMER_BTREE_TYPE_SPIKE_END:
		KKASSERT(node->ondisk->type == HAMMER_BTREE_TYPE_LEAF);
		KKASSERT((cursor->flags & HAMMER_CURSOR_INCLUSTER) == 0);
		vol_no = elm->leaf.spike_vol_no;
		clu_no = elm->leaf.spike_clu_no;
		volume = hammer_get_volume(node->cluster->volume->hmp,
					   vol_no, &error);
		KKASSERT(error != EINVAL);
		if (error)
			return(error);
		cluster = hammer_get_cluster(volume, clu_no, &error, 0);
		KKASSERT(error != EINVAL);
		hammer_rel_volume(volume, 0);
		if (error)
			return(error);
		cursor->left_bound = &cluster->ondisk->clu_btree_beg;
		cursor->right_bound = &cluster->ondisk->clu_btree_end;
		node = hammer_get_node(cluster,
				       cluster->ondisk->clu_btree_root,
				       &error);
		hammer_rel_cluster(cluster, 0);
		break;
	default:
		panic("hammer_cursor_down: illegal btype %02x (%c)\n",
		      elm->base.btype,
		      (elm->base.btype ? elm->base.btype : '?'));
		break;
	}
	if (error == 0) {
		hammer_lock_sh(&node->lock);
		cursor->node = node;
		cursor->index = 0;
	}
	return(error);
}

