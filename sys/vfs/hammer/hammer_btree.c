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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.c,v 1.1 2007/11/01 20:53:05 dillon Exp $
 */

/*
 * HAMMER BH-Tree index
 *
 * HAMMER implements a modified B+Tree.  In documentation this will
 * simply be refered to as the HAMMER BH-Tree.  Basically a BH-Tree
 * looks like a B+Tree (A B-Tree which stores its records only at the leafs
 * of the tree), but adds two additional boundary elements which describe
 * the left-most and right-most element a node is able to represent.  In
 * otherwords, we have boundary elements at the two ends of a BH-Tree node
 * instead of sub-tree pointers.
 *
 * A BH-Tree internal node looks like this:
 *
 *	B N N N N N N B   <-- boundary and internal elements
 *       S S S S S S S    <-- subtree pointers
 *
 * A BH-Tree leaf node basically looks like this:
 *
 *	L L L L L L L L   <-- leaf elemenets
 *
 * The recursion radix is reduced by 2 relative to a normal B-Tree but
 * we get a number of significant benefits for our troubles.
 *
 * The big benefit to using a BH-Tree is that it is possible to cache
 * pointers into the middle of the tree and not have to start searches,
 * insertions, OR deletions at the root node.   In particular, searches are
 * able to progress in a definitive direction from any point in the tree
 * without revisting nodes.  This greatly improves the efficiency of many
 * operations, most especially record appends.
 *
 * BH-Trees also make the stacking of trees fairly straightforward.
 */
#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

static int btree_search(hammer_btree_cursor_t cursor, hammer_base_elm_t key,
			int flags);
static int btree_cursor_set_cluster(hammer_btree_cursor_t cursor,
			struct hammer_cluster *cluster);
static int btree_cursor_set_cluster_by_value(hammer_btree_cursor_t cursor,
			int32_t vol_no, int32_t clu_no, hammer_tid_t clu_id);
static int btree_cursor_up(hammer_btree_cursor_t cursor);
static int btree_cursor_down(hammer_btree_cursor_t cursor);
static int btree_split_internal(hammer_btree_cursor_t cursor);
static int btree_split_leaf(hammer_btree_cursor_t cursor);
static int btree_rebalance_node(hammer_btree_cursor_t cursor);
static int btree_collapse(hammer_btree_cursor_t cursor);

/*
 * Compare two BH-Tree elements, return -1, 0, or +1 (e.g. similar to strcmp).
 */
static int
hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2)
{
	if (key1->obj_id < key2->obj_id)
		return(-1);
	if (key1->obj_id > key2->obj_id)
		return(1);

	if (key1->rec_type < key2->rec_type)
		return(-1);
	if (key1->rec_type > key2->rec_type)
		return(1);

	if (key1->key < key2->key)
		return(-1);
	if (key1->key > key2->key)
		return(1);

	if (key1->create_tid < key2->create_tid)
		return(-1);
	if (key1->create_tid > key2->create_tid)
		return(1);

	return(0);
}

/*
 * Create a separator half way inbetween key1 and key2.  For fields just
 * one unit apart, the separator will match key2.
 */
#define MAKE_SEPARATOR(key1, key2, dest, field)	\
	dest->field = key1->field + ((key2->field - key1->field + 1) >> 1);

static void
hammer_make_separator(hammer_base_elm_t key1, hammer_base_elm_t key2,
		      hammer_base_elm_t dest)
{
	MAKE_SEPARATOR(key1, key2, dest, obj_id);
	MAKE_SEPARATOR(key1, key2, dest, rec_type);
	MAKE_SEPARATOR(key1, key2, dest, key);
	MAKE_SEPARATOR(key1, key2, dest, create_tid);
	dest->delete_tid = 0;
	dest->obj_type = 0;
	dest->reserved07 = 0;
}

#undef MAKE_SEPARATOR

/*
 * Return whether a generic internal or leaf node is full
 */
static int
btree_node_is_full(struct hammer_base_node *node)
{
	switch(node->type) {
	case HAMMER_BTREE_INTERNAL_NODE:
		if (node->count == HAMMER_BTREE_INT_ELMS)
			return(1);
		break;
	case HAMMER_BTREE_LEAF_NODE:
		if (node->count == HAMMER_BTREE_LEAF_ELMS)
			return(1);
		break;
	default:
		panic("illegal btree subtype");
	}
	return(0);
}

static int
btree_max_elements(u_int8_t type)
{
	if (type == HAMMER_BTREE_LEAF_NODE)
		return(HAMMER_BTREE_LEAF_ELMS);
	if (type == HAMMER_BTREE_INTERNAL_NODE)
		return(HAMMER_BTREE_INT_ELMS);
	panic("btree_max_elements: bad type %d\n", type);
}

/*
 * Initialize a cursor, setting the starting point for a BH-Tree search.
 *
 * The passed cluster must be locked.  This function will add a reference
 * to it.
 */
int
hammer_btree_cursor_init(hammer_btree_cursor_t cursor,
			 struct hammer_cluster *cluster)
{
	int error;

	cursor->cluster = NULL;
	cursor->node_buffer = NULL;
	cursor->parent_buffer = NULL;
	cursor->node = NULL;
	cursor->parent = NULL;
	cursor->index = 0;
	cursor->parent_index = 0;
	error = btree_cursor_set_cluster(cursor, cluster);
	return(error);
}

/*
 * Clean up a HAMMER BH-Tree cursor after we are finished using it.
 */
void
hammer_btree_cursor_done(hammer_btree_cursor_t cursor)
{
	if (cursor->node_buffer) {
		hammer_put_buffer(cursor->node_buffer);
		cursor->node_buffer = NULL;
	}
	if (cursor->parent_buffer) {
		hammer_put_buffer(cursor->parent_buffer);
		cursor->parent_buffer = NULL;
	}
	if (cursor->cluster) {
		hammer_put_cluster(cursor->cluster);
		cursor->cluster = NULL;
	}
	cursor->node = NULL;
	cursor->parent = NULL;
	cursor->left_bound = NULL;
	cursor->right_bound = NULL;
	cursor->index = 0;
	cursor->parent_index = 0;
}

/*
 * Initialize a btree info structure and its associated cursor prior to
 * running a BH-Tree operation.
 */
int
hammer_btree_info_init(hammer_btree_info_t info, struct hammer_cluster *cluster)
{
	int error;

	error = hammer_btree_cursor_init(&info->cursor, cluster);
	info->data_buffer = NULL;
	info->record_buffer = NULL;
	info->data = NULL;
	info->rec = NULL;
	return (error);
}

/*
 * Clean up a BH-Tree info structure after we are finished using it.
 */
void
hammer_btree_info_done(hammer_btree_info_t info)
{
	hammer_btree_cursor_done(&info->cursor);
	if (info->data_buffer) {
		hammer_put_buffer(info->data_buffer);
		info->data_buffer = NULL;
	}
	if (info->record_buffer) {
		hammer_put_buffer(info->record_buffer);
		info->record_buffer = NULL;
	}
	info->data = NULL;
	info->rec = NULL;
}

/*
 * Search a cluster's BH-Tree.  Return the matching node.  The search
 * normally traverses clusters but will terminate at a cluster entry
 * in a leaf if HAMMER_BTREE_CLUSTER_TAG is specified.  If this flag
 * is specified EIO is returned if the search would otherwise have to
 * cursor up into a parent cluster.
 *
 * The cursor must be completely initialized on entry.  If the cursor is
 * at the root of a cluster, the parent pointer & buffer may be NULL (in
 * that case the bounds point to the bounds in the cluster header).  The
 * node_buffer and node must always be valid.
 *
 * The search code may be forced to iterate up the tree if the conditions
 * required for an insertion or deletion are not met.  This does not occur
 * very often.
 *
 * INSERTIONS: The search will split full nodes and leaves on its way down
 * and guarentee that the leaf it ends up on is not full.
 *
 * DELETIONS: The search will rebalance the tree on its way down.
 */
static 
int
btree_search(hammer_btree_cursor_t cursor, hammer_base_elm_t key, int flags)
{
	struct hammer_btree_leaf_node *leaf;
	int error;
	int i;
	int r;

	/*
	 * Move our cursor up the tree until we find a node whos range covers
	 * the key we are trying to locate.  This may move us between
	 * clusters.
	 *
	 * Since the root cluster always has a root BH-Tree node, info->node
	 * is always non-NULL if no error occured.  The parent field will be
	 * non-NULL unless we are at the root of a cluster.
	 */
	while (hammer_btree_cmp(key, cursor->left_bound) < 0 ||
	       hammer_btree_cmp(key, cursor->right_bound) >= 0) {
		/*
		 * Must stay in current cluster if flagged, code should never
		 * use the flag if it wants us to traverse to the parent
		 * cluster.
		 */
		if (cursor->parent == NULL &&
		    (flags & HAMMER_BTREE_CLUSTER_TAG)) {
			return(EIO);
		}
		error = btree_cursor_up(cursor);
		if (error)
			return(error);
	}
	KKASSERT(cursor->node != NULL);

	/*
	 * If we are inserting we can't start at a full node if the parent
	 * is also full (because there is no way to split the node),
	 * continue running up the tree until we hit the root of the
	 * current cluster or until the requirement is satisfied.
	 */
	while (flags & HAMMER_BTREE_INSERT) {
		if (btree_node_is_full(&cursor->node->base) == 0)
			break;
		if (cursor->parent == NULL)
			break;
		if (cursor->parent->base.count != HAMMER_BTREE_INT_ELMS)
			break;
		error = btree_cursor_up(cursor);
		if (error)
			return (error);
	}

	/*
	 * If we are deleting we can't start at a node with only one element
	 * unless it is root, because all of our code assumes that nodes
	 * will never be empty.
	 *
	 * This handles the case where the cursor is sitting at a leaf and
	 * either the leaf or parent contain an insufficient number of
	 * elements.
	 */
	while (flags & HAMMER_BTREE_DELETE) {
		if (cursor->node->base.count > 1)
			break;
		if (cursor->parent == NULL)
			break;
		KKASSERT(cursor->node->base.count != 0);
		error = btree_cursor_up(cursor);
		if (error)
			return (error);
	}

new_cluster:
	/*
	 * Push down through internal nodes to locate the requested key.
	 */
	while (cursor->node->base.type == HAMMER_BTREE_INTERNAL_NODE) {
		struct hammer_btree_internal_node *node;

		/*
		 * If we are a the root node and deleting, try to collapse
		 * all of the root's children into the root.  This is the
		 * only point where tree depth is reduced.
		 */
		if ((flags & HAMMER_BTREE_DELETE) && cursor->parent == NULL) {
			error = btree_collapse(cursor);
			if (error)
				return (error);
		}

		/*
		 * Scan the node (XXX binary search) to find the subtree
		 * index to push down into.  We go one-past, then back-up.
		 * The key should never be less then the left-hand boundary
		 * so I should never wind up 0.
		 */
		node = &cursor->node->internal;
		for (i = 0; i < node->base.count; ++i) {
			r = hammer_btree_cmp(key, &node->elms[i].base);
			if (r < 0)
				break;
		}
		KKASSERT(i != 0);

		/*
		 * The push-down index is now i - 1.
		 */
		--i;
		cursor->index = i;

		/*
		 * Handle insertion and deletion requirements.
		 *
		 * If inserting split full nodes.  The split code will
		 * adjust cursor->node and cursor->index if the current
		 * index winds up in the new node.
		 */
		if (flags & HAMMER_BTREE_INSERT) {
			if (node->base.count == HAMMER_BTREE_INT_ELMS) {
				error = btree_split_internal(cursor);
				if (error)
					return(error);
				/*
				 * reload stale pointers
				 */
				i = cursor->index;
				node = &cursor->node->internal;
			}
		}

		/*
		 * If deleting rebalance - do not allow the child to have
		 * just one element or we will not be able to delete it.
		 *
		 * Neither internal or leaf nodes (except a root-leaf) are
		 * allowed to drop to 0 elements.
		 *
		 * Our separators may have been reorganized after rebalancing,
		 * so we have to pop back up and rescan.
		 */
		if (flags & HAMMER_BTREE_DELETE) {
			if (node->elms[i].subtree_count <= 1) {
				error = btree_rebalance_node(cursor);
				if (error)
					return(error);
				/* cursor->index is invalid after call */
				goto new_cluster;
			}
		}

		/*
		 * Push down (push into new node, existing node becomes
		 * the parent).
		 */
		error = btree_cursor_down(cursor);
		if (error)
			return (error);
	}


	/*
	 * We are at a leaf, do a linear search of the key array.
	 * (XXX do a binary search).  On success the index is set to the
	 * matching element, on failure the index is set to the insertion
	 * point.
	 *
	 * Boundaries are not stored in leaf nodes, so the index can wind
	 * up to the left of element 0 (index == 0) or past the end of
	 * the array (index == leaf->base.count).
	 */
	leaf = &cursor->node->leaf;
	KKASSERT(leaf->base.count <= HAMMER_BTREE_LEAF_ELMS);

	for (i = 0; i < leaf->base.count; ++i) {
		r = hammer_btree_cmp(key, &leaf->elms[i].base);
		if (r < 0)
			break;
		if (r == 0) {
			/*
			 * Return an exact match unless this is a cluster
			 * element.  If it is, and the cluster tag flag has
			 * not been set, push into the new cluster and
			 * continue the search.
			 */
			cursor->index = i;
			if ((leaf->elms[i].base.obj_type &
			     HAMMER_OBJTYPE_CLUSTER_FLAG) &&
			    (flags & HAMMER_BTREE_CLUSTER_TAG) == 0) {
				error = btree_cursor_down(cursor);
				if (error)
					return (error);
				goto new_cluster;
			}
			return(0);
		}
	}

	/*
	 * We could not find an exact match.  Check for a cluster
	 * recursion.  The cluster's range is bracketed by two
	 * leaf elements.  One of the two must be in this node.
	 */
	if ((flags & HAMMER_BTREE_CLUSTER_TAG) == 0) {
		if (i == leaf->base.count) {
			if (leaf->elms[i-1].base.obj_type &
			    HAMMER_OBJTYPE_CLUSTER_FLAG) {
				cursor->index = i - 1;
				error = btree_cursor_down(cursor);
				if (error)
					return (error);
				goto new_cluster;
			}
		} else {
			if (leaf->elms[i].base.obj_type &
			    HAMMER_OBJTYPE_CLUSTER_FLAG) {
				cursor->index = i;
				error = btree_cursor_down(cursor);
				if (error)
					return (error);
				goto new_cluster;
			}
		}
	}

	/*
	 * If inserting split a full leaf before returning.  This
	 * may have the side effect of adjusting cursor->node and
	 * cursor->index.
	 *
	 * We delayed the split in order to avoid any unnecessary splits.
	 *
	 * XXX parent's parent's subtree_count will be wrong after
	 * this (keep parent of parent around too?  ugh).
	 */
	cursor->index = i;
	if ((flags & HAMMER_BTREE_INSERT) &&
	    leaf->base.count == HAMMER_BTREE_LEAF_ELMS) {
		error = btree_split_leaf(cursor);
		/* NOT USED
		i = cursor->index;
		node = &cursor->node->internal;
		*/
		if (error)
			return(error);
	}
	return(ENOENT);
}

/*
 * Look up the key in the HAMMER BH-Tree and fill in the rest of the
 * info structure with the results according to flags.  0 is returned on
 * success, non-zero on error.
 *
 * The caller initializes (key, cluster) and makes the call.  The cluster
 * must be referenced and locked on call.  The function can chain through
 * multiple clusters and will replace the passed cluster as it does so,
 * dereferencing and unlocking it, and referencing and locking the chain.
 * On return info->cluster will still be referenced and locked but may
 * represent a different cluster.
 */
int
hammer_btree_lookup(hammer_btree_info_t info, hammer_base_elm_t key, int flags)
{
	hammer_btree_leaf_elm_t elm;
	struct hammer_cluster *cluster;
	int32_t cloff;
	int error;
	int iscl;

	error = btree_search(&info->cursor, key, flags);
	if (error)
		return (error);

	/* 
	 * Extract the record and data reference if requested.
	 *
	 * A cluster record type has no data reference, the information
	 * is stored directly in the record and BH-Tree element.
	 *
	 * The case where the data reference resolves to the same buffer
	 * as the record reference must be handled.
	 */
	elm = &info->cursor.node->leaf.elms[info->cursor.index];
	iscl = (elm->base.obj_type & HAMMER_OBJTYPE_CLUSTER_FLAG) != 0;
	cluster = info->cursor.cluster;
	if ((flags & HAMMER_BTREE_GET_RECORD) && error == 0) {
		cloff = iscl ? elm->cluster.rec_offset : elm->record.rec_offset;


		info->rec = hammer_bread(cluster, cloff, HAMMER_FSBUF_RECORDS,
					 &error, &info->record_buffer);
	} else {
		cloff = 0;
	}
	if ((flags & HAMMER_BTREE_GET_DATA) && iscl == 0 && error == 0) {
		if ((cloff ^ elm->record.data_offset) & ~HAMMER_BUFMASK) {
			info->data = hammer_bread(cluster,
						  elm->record.data_offset,
						  HAMMER_FSBUF_DATA,
						  &error, &info->record_buffer);
		} else {
			info->data = (void *)
				((char *)info->data_buffer->ondisk +
				 (elm->record.data_offset & HAMMER_BUFMASK));
		}
	}
	return(error);
}


/*
 * Insert a record into a BH-Tree's cluster.  The caller has already
 * reserved space for the record and data and must handle a ENOSPC
 * return.
 */
int
hammer_btree_insert(struct hammer_btree_info *info, hammer_btree_leaf_elm_t elm)
{
	struct hammer_btree_cursor *cursor;
	struct hammer_btree_internal_node *parent;
	struct hammer_btree_leaf_node *leaf;
	int error;
	int i;

	/*
	 * Issue a search to get our cursor at the right place.  The search
	 * will get us to a leaf node.
	 *
	 * The search also does some setup for our insert, so there is always
	 * room in the leaf.
	 */
	error = btree_search(&info->cursor, &elm->base, HAMMER_BTREE_INSERT);
	if (error != ENOENT) {
		if (error == 0)
			error = EEXIST;
		return (error);
	}

	/*
	 * Insert the element at the leaf node and update the count in the
	 * parent.  It is possible for parent to be NULL, indicating that
	 * the root of the BH-Tree in the cluster is a leaf.  It is also
	 * possible for the leaf to be empty.
	 *
	 * Remember that the right-hand boundary is not included in the
	 * count.
	 */
	cursor = &info->cursor;
	leaf = &cursor->node->leaf;
	i = cursor->index;
	KKASSERT(leaf->base.count < HAMMER_BTREE_LEAF_ELMS);
	bcopy(&leaf->elms[i], &leaf->elms[i+1],
	      (leaf->base.count - i + 1) * sizeof(leaf->elms[0]));
	leaf->elms[i] = *elm;
	++leaf->base.count;

	if ((parent = cursor->parent) != NULL) {
		i = cursor->parent_index;
		++parent->elms[i].subtree_count;
		KKASSERT(parent->elms[i].subtree_count <= leaf->base.count);
		hammer_modify_buffer(cursor->parent_buffer);
	}
	hammer_modify_buffer(cursor->node_buffer);
	return(0);
}

/*
 * Delete a record from the BH-Tree.
 */
int
hammer_btree_delete(struct hammer_btree_info *info, hammer_base_elm_t key)
{
	struct hammer_btree_cursor *cursor;
	struct hammer_btree_internal_node *parent;
	struct hammer_btree_leaf_node *leaf;
	int error;
	int i;

	/*
	 * Locate the leaf element to delete.  The search is also responsible
	 * for doing some of the rebalancing work on its way down.
	 */
	error = btree_search(&info->cursor, key, HAMMER_BTREE_DELETE);
	if (error)
		return (error);

	/*
	 * Delete the element from the leaf node.  We leave empty leafs alone
	 * and instead depend on a future search to locate and destroy them.
	 * Otherwise we would have to recurse back up the tree to adjust
	 * the parent's subtree_count and we do not want to do that.
	 *
	 * Remember that the right-hand boundary is not included in the
	 * count.
	 */
	cursor = &info->cursor;
	leaf = &cursor->node->leaf;
	i = cursor->index;

	KKASSERT(cursor->node->base.type == HAMMER_BTREE_LEAF_NODE);
	bcopy(&leaf->elms[i+1], &leaf->elms[i],
	      (leaf->base.count - i) * sizeof(leaf->elms[0]));
	--leaf->base.count;
	if ((parent = cursor->parent) != NULL) {
		/*
		 * Adjust parent's notion of the leaf's count.  subtree_count
		 * is only approximately, it is allowed to be too small but
		 * never allowed to be too large.  Make sure we don't drop
		 * the count below 0.
		 */
		i = cursor->parent_index;
		if (parent->elms[i].subtree_count)
			--parent->elms[i].subtree_count;
		KKASSERT(parent->elms[i].subtree_count <= leaf->base.count);
		hammer_modify_buffer(cursor->parent_buffer);
	}
	hammer_modify_buffer(cursor->node_buffer);
	return(0);
}

/************************************************************************
 *				CURSOR SUPPORT				*
 ************************************************************************
 *
 * These routines do basic cursor operations.  This support will probably
 * be expanded in the future to add link fields for linear scans.
 */

/*
 * Unconditionally set the cursor to the root of the specified cluster.
 * The current cursor node is set to the root node of the cluster (which
 * can be an internal node or a degenerate leaf), and the parent info
 * is cleaned up and cleared.
 *
 * The passed cluster must be locked.  This function will add a reference
 * to it.  The cursor must already have a cluster assigned to it, which we
 * will replace.
 */
static
int
btree_cursor_set_cluster_by_value(hammer_btree_cursor_t cursor,
			int32_t vol_no, int32_t clu_no, hammer_tid_t clu_id)
{
	struct hammer_cluster *cluster;
	struct hammer_volume *volume;
	int error = 0;

	if (vol_no < 0)
		return(EIO);
	cluster = cursor->cluster;
	KKASSERT(cluster != NULL);
	if (vol_no == cluster->volume->vol_no) {
		cluster = hammer_get_cluster(cluster->volume, clu_no,
					     &error, 0);
	} else {
		volume = hammer_get_volume(cluster->volume->hmp,
					   vol_no, &error);
		if (volume) {
			cluster = hammer_get_cluster(volume, clu_no,
						     &error, 0);
			hammer_put_volume(volume);
		} else {
			cluster = NULL;
		}
	}

	/*
	 * Make sure the cluster id matches.  XXX At the moment the
	 * clu_id in the btree cluster element is only 32 bits, so only
	 * compare the low 32 bits.
	 */
	if (cluster) {
		if ((int32_t)cluster->ondisk->clu_id == (int32_t)clu_id) {
			btree_cursor_set_cluster(cursor, cluster);
		} else {
			error = EIO;
		}
		hammer_put_cluster(cluster);
	}
	return (error);
}

static
int
btree_cursor_set_cluster(hammer_btree_cursor_t cursor,
			 struct hammer_cluster *cluster)
{
	int error = 0;

	hammer_dup_cluster(&cursor->cluster, cluster);
	cursor->node = hammer_bread(cluster,
				    cluster->ondisk->clu_btree_root,
				    HAMMER_FSBUF_BTREE,
				    &error,
				    &cursor->node_buffer);
	cursor->index = 0;
	if (cursor->parent) {
		hammer_put_buffer(cursor->parent_buffer);
		cursor->parent_buffer = NULL;
		cursor->parent = NULL;
		cursor->parent_index = 0;
	}
	cursor->left_bound = &cluster->ondisk->clu_btree_beg;
	cursor->right_bound = &cluster->ondisk->clu_btree_end;
	return (error);
}

/*
 * Cursor the node up to the parent node.  If at the root of a cluster,
 * cursor to the root of the cluster's parent cluster.  Note carefully
 * that we do NOT scan the parent cluster to find the leaf that dropped
 * into our current cluster.
 *
 * This function is primarily used by the search code to avoid having
 * to search from the root of the filesystem BH-Tree.
 */
static
int
btree_cursor_up(hammer_btree_cursor_t cursor)
{
	struct hammer_cluster_ondisk *ondisk;
	struct hammer_btree_internal_node *parent;
	int error;
	int i;
	int r;

	error = 0;
	if (cursor->parent == NULL) {
		/*
		 * We are at the root of the cluster, pop up to the root
		 * of the parent cluster.  Return EIO if we are at the
		 * root cluster of the filesystem.
		 */
		ondisk = cursor->cluster->ondisk;
		error = btree_cursor_set_cluster_by_value(
			    cursor,
			    ondisk->clu_btree_parent_vol_no,
			    ondisk->clu_btree_parent_clu_no,
			    ondisk->clu_btree_parent_clu_id);
	} else {
		/*
		 * Copy the current node's parent info into the node and load
		 * a new parent.
		 */
		cursor->index = cursor->parent_index;
		cursor->node = (hammer_btree_node_t)cursor->parent;
		hammer_dup_buffer(&cursor->node_buffer, cursor->parent_buffer);

		/*
		 * Load the parent's parent into parent and figure out the
		 * parent's element index for its child.  Just NULL it out
		 * if we hit the root of the cluster.
		 */
		if (cursor->parent->base.parent) {
			parent = hammer_bread(cursor->cluster,
					      cursor->node->base.parent,
					      HAMMER_FSBUF_BTREE,
					      &error,
					      &cursor->parent_buffer);
			for (i = 0; i < parent->base.count; ++i) {
				r = hammer_btree_cmp(
					&cursor->node->internal.elms[0].base,
					&parent->elms[i].base);
				if (r < 0)
					break;
			}
			cursor->parent = parent;
			cursor->parent_index = i - 1;
			KKASSERT(parent->elms[i].subtree_offset ==
				 hammer_bclu_offset(cursor->node_buffer,
						    cursor->node));
		} else {
			hammer_put_buffer(cursor->parent_buffer);
			cursor->parent = NULL;
			cursor->parent_buffer = NULL;
			cursor->parent_index = 0;
		}
	}
	return(error);
}

/*
 * Cursor down into (node, index)
 *
 * Push down into the current cursor.  The current cursor becomes the parent.
 * If the current cursor represents a leaf cluster element this function will
 * push into the root of a new cluster and clear the parent fields.
 *
 * Pushin down at a leaf which is not a cluster element returns EIO.
 */
static
int
btree_cursor_down(hammer_btree_cursor_t cursor)
{
	hammer_btree_node_t node;
	int error;

	node = cursor->node;
	if (node->base.type == HAMMER_BTREE_LEAF_NODE) {
		/*
		 * Push into another cluster
		 */
		hammer_btree_leaf_elm_t elm;

		elm = &node->leaf.elms[cursor->index];
		if (elm->base.rec_type == HAMMER_RECTYPE_CLUSTER) {
			error = btree_cursor_set_cluster_by_value(
				    cursor,
				    elm->cluster.vol_no,
				    elm->cluster.clu_no,
				    elm->cluster.verifier);
		} else {
			error = EIO;
		}
	} else {
		/*
		 * Push into another BH-Tree node (internal or leaf)
		 */
		struct hammer_btree_internal_elm *elm;

		KKASSERT(node->base.type == HAMMER_BTREE_INTERNAL_NODE);
		elm = &node->internal.elms[cursor->index];
		KKASSERT(elm->subtree_offset != 0);

		cursor->parent_index = cursor->index;
		cursor->parent = &cursor->node->internal;
		hammer_dup_buffer(&cursor->parent_buffer, cursor->node_buffer);

		cursor->node = hammer_bread(cursor->cluster,
					    elm->subtree_offset,
					    HAMMER_FSBUF_BTREE,
					    &error,
					    &cursor->node_buffer);
		cursor->index = 0;
		KKASSERT(cursor->node == NULL ||
			 cursor->node->base.parent == elm->subtree_offset);
	}
	return(error);
}

/************************************************************************
 *				SPLITTING AND MERGING 			*
 ************************************************************************
 *
 * These routines do all the dirty work required to split and merge nodes.
 */

/*
 * Split an internal into two nodes and move the separator at the split
 * point to the parent.  Note that the parent's parent's element pointing
 * to our parent will have an incorrect subtree_count (we don't update it).
 * It will be low, which is ok.
 *
 * Cursor->index indicates the element the caller intends to push into.
 * We will adjust cursor->node and cursor->index if that element winds
 * up in the split node.
 */
static
int
btree_split_internal(hammer_btree_cursor_t cursor)
{
	struct hammer_btree_internal_node *parent;
	struct hammer_btree_internal_node *node;
	struct hammer_btree_internal_node *new_node;
	struct hammer_btree_internal_elm *elm;
	struct hammer_btree_internal_elm *parent_elm;
	struct hammer_buffer *new_buffer;
	int32_t parent_offset;
	int parent_index;
	int made_root;
	int split;
	int error;
	const size_t esize = sizeof(struct hammer_btree_internal_elm);

	/* 
	 * We are splitting but elms[split] will be promoted to the parent,
	 * leaving the right hand node with one less element.  If the
	 * insertion point will be on the left-hand side adjust the split
	 * point to give the right hand side one additional node.
	 */
	node = &cursor->node->internal;
	split = (node->base.count + 1) / 2;
	if (cursor->index <= split)
		--split;
	error = 0;

	/*
	 * If we are at the root of the tree, create a new root node with
	 * 1 element and split normally.  Avoid making major modifications
	 * until we know the whole operation will work.
	 */
	parent = cursor->parent;
	if (parent == NULL) {
		parent = hammer_alloc_btree(cursor->cluster, &error,
					    &cursor->parent_buffer);
		if (parent == NULL)
			return(error);
		parent->base.count = 1;
		parent->base.parent = 0;
		parent->base.type = HAMMER_BTREE_INTERNAL_NODE;
		parent->base.subtype = node->base.type;
		parent->elms[0].base = cursor->cluster->ondisk->clu_btree_beg;
		parent->elms[0].subtree_offset =
			hammer_bclu_offset(cursor->node_buffer, node);
		parent->elms[1].base = cursor->cluster->ondisk->clu_btree_end;
		made_root = 1;
		cursor->parent_index = 0;	/* insertion point in parent */
	} else {
		made_root = 0;
	}
	parent_offset = hammer_bclu_offset(cursor->parent_buffer, parent);

	/*
	 * Split node into new_node at the split point.
	 *
	 *  B O O O P N N B	<-- P = node->elms[split]
	 *   0 1 2 3 4 5 6	<-- subtree indices
	 *
	 *       x x P x x
	 *        s S S s  
	 *         /   \
	 *  B O O O B    B N N B	<--- inner boundary points are 'P'
	 *   0 1 2 3      4 5 6  
	 *
	 */
	new_buffer = NULL;
	new_node = hammer_alloc_btree(cursor->cluster, &error, &new_buffer);
	if (new_node == NULL) {
		if (made_root)
			hammer_free_btree_ptr(cursor->parent_buffer,
					      (hammer_btree_node_t)parent);
		return(error);
	}

	/*
	 * Create the new node.  P become the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 *
	 * elm is the new separator.
	 */
	elm = &node->elms[split];
	bcopy(elm, &new_node->elms[0], (node->base.count - split + 1) * esize);
	new_node->base.count = node->base.count - split;
	new_node->base.parent = parent_offset;
	new_node->base.type = HAMMER_BTREE_INTERNAL_NODE;
	new_node->base.subtype = node->base.subtype;
	KKASSERT(node->base.type == new_node->base.type);

	/*
	 * Cleanup the original node.  P becomes the new boundary, its
	 * subtree_offset was moved to the new node.  If we had created
	 * a new root its parent pointer may have changed.
	 */
	node->base.parent = parent_offset;
	elm->subtree_offset = 0;

	/*
	 * Insert the separator into the parent, fixup the parent's
	 * reference to the original node, and reference the new node.
	 * The separator is P.
	 *
	 * Remember that base.count does not include the right-hand boundary.
	 */
	parent_index = cursor->parent_index;
	parent->elms[parent_index].subtree_count = split;
	parent_elm = &parent->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (parent->base.count - parent_index) * esize);
	parent_elm->base = elm->base;	/* separator P */
	parent_elm->subtree_offset = hammer_bclu_offset(new_buffer, new_node);
	parent_elm->subtree_count = new_node->base.count;

	hammer_modify_buffer(new_buffer);
	hammer_modify_buffer(cursor->parent_buffer);
	hammer_modify_buffer(cursor->node_buffer);

	/*
	 * The cluster's root pointer may have to be updated.
	 */
	if (made_root) {
		cursor->cluster->ondisk->clu_btree_root = node->base.parent;
		hammer_modify_cluster(cursor->cluster);
	}

	/*
	 * Ok, now adjust the cursor depending on which element the original
	 * index was pointing at.  If we are >= the split point the push node
	 * is now in the new node.
	 *
	 * NOTE: If we are at the split point itself we cannot stay with the
	 * original node because the push index will point at the right-hand
	 * boundary, which is illegal.
	 */
	if (cursor->index >= split) {
		cursor->index -= split;
		cursor->node = (hammer_btree_node_t)new_node;
		hammer_put_buffer(cursor->node_buffer);
		cursor->node_buffer = new_buffer;
	}

	return (0);
}

/*
 * Same as the above, but splits a full leaf node.
 */
static
int
btree_split_leaf(hammer_btree_cursor_t cursor)
{
	struct hammer_btree_internal_node *parent;
	struct hammer_btree_leaf_node *leaf;
	struct hammer_btree_leaf_node *new_leaf;
	union hammer_btree_leaf_elm *elm;
	struct hammer_btree_internal_elm *parent_elm;
	struct hammer_buffer *new_buffer;
	int32_t parent_offset;
	int parent_index;
	int made_root;
	int split;
	int error;
	const size_t esize = sizeof(struct hammer_btree_internal_elm);

	/* 
	 * We are splitting but elms[split] will be promoted to the parent,
	 * leaving the right hand node with one less element.  If the
	 * insertion point will be on the left-hand side adjust the split
	 * point to give the right hand side one additional node.
	 */
	leaf = &cursor->node->leaf;
	split = (leaf->base.count + 1) / 2;
	if (cursor->index <= split)
		--split;
	error = 0;

	/*
	 * If we are at the root of the tree, create a new root node with
	 * 1 element and split normally.  Avoid making major modifications
	 * until we know the whole operation will work.
	 */
	parent = cursor->parent;
	if (parent == NULL) {
		parent = hammer_alloc_btree(cursor->cluster, &error,
					    &cursor->parent_buffer);
		if (parent == NULL)
			return(error);
		parent->base.count = 1;
		parent->base.parent = 0;
		parent->base.type = HAMMER_BTREE_INTERNAL_NODE;
		parent->base.subtype = leaf->base.type;
		parent->elms[0].base = cursor->cluster->ondisk->clu_btree_beg;
		parent->elms[0].subtree_offset =
			hammer_bclu_offset(cursor->node_buffer, leaf);
		parent->elms[1].base = cursor->cluster->ondisk->clu_btree_end;
		made_root = 1;
		cursor->parent_index = 0;	/* insertion point in parent */
	} else {
		made_root = 0;
	}
	parent_offset = hammer_bclu_offset(cursor->parent_buffer, parent);

	/*
	 * Split leaf into new_leaf at the split point.  Select a separator
	 * value in-between the two leafs but with a bent towards the right
	 * leaf since comparisons use an 'elm >= separator' inequality.
	 *
	 *  L L L L L L L L
	 *
	 *       x x P x x
	 *        s S S s  
	 *         /   \
	 *  L L L L     L L L L
	 */
	new_buffer = NULL;
	new_leaf = hammer_alloc_btree(cursor->cluster, &error, &new_buffer);
	if (new_leaf == NULL) {
		if (made_root)
			hammer_free_btree_ptr(cursor->parent_buffer,
					      (hammer_btree_node_t)parent);
		return(error);
	}

	/*
	 * Create the new node.  P become the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 */
	elm = &leaf->elms[split];
	bcopy(elm, &new_leaf->elms[0], (leaf->base.count - split) * esize);
	new_leaf->base.count = leaf->base.count - split;
	new_leaf->base.parent = parent_offset;
	new_leaf->base.type = HAMMER_BTREE_LEAF_NODE;
	new_leaf->base.subtype = 0;
	KKASSERT(leaf->base.type == new_leaf->base.type);

	/*
	 * Cleanup the original node.  P becomes the new boundary, its
	 * subtree_offset was moved to the new node.  If we had created
	 * a new root its parent pointer may have changed.
	 */
	leaf->base.parent = parent_offset;

	/*
	 * Insert the separator into the parent, fixup the parent's
	 * reference to the original node, and reference the new node.
	 * The separator is P.
	 *
	 * Remember that base.count does not include the right-hand boundary.
	 * We are copying parent_index+1 to parent_index+2, not +0 to +1.
	 */
	parent_index = cursor->parent_index;
	parent->elms[parent_index].subtree_count = split;
	parent_elm = &parent->elms[parent_index+1];
	if (parent_index + 1 != parent->base.count) {
		bcopy(parent_elm, parent_elm + 1,
		      (parent->base.count - parent_index - 1) * esize);
	}
	hammer_make_separator(&elm[-1].base, &elm[0].base, &parent_elm->base);
	parent_elm->subtree_offset = hammer_bclu_offset(new_buffer, new_leaf);
	parent_elm->subtree_count = new_leaf->base.count;

	hammer_modify_buffer(new_buffer);
	hammer_modify_buffer(cursor->parent_buffer);
	hammer_modify_buffer(cursor->node_buffer);

	/*
	 * The cluster's root pointer may have to be updated.
	 */
	if (made_root) {
		cursor->cluster->ondisk->clu_btree_root = leaf->base.parent;
		hammer_modify_cluster(cursor->cluster);
	}

	/*
	 * Ok, now adjust the cursor depending on which element the original
	 * index was pointing at.  If we are >= the split point the push node
	 * is now in the new node.
	 *
	 * NOTE: If we are at the split point itself we cannot stay with the
	 * original node because the push index will point at the right-hand
	 * boundary, which is illegal.
	 */
	if (cursor->index >= split) {
		cursor->index -= split;
		cursor->node = (hammer_btree_node_t)new_leaf;
		hammer_put_buffer(cursor->node_buffer);
		cursor->node_buffer = new_buffer;
	}

	return (0);
}

/*
 * This routine is called on an internal node prior to recursing down
 * through (node, index) when (node, index) MIGHT have too few elements for
 * the caller to perform a deletion.
 *
 * cursor->index is invalid on return because the separators may have gotten
 * adjusted, the caller must rescan the node's elements.  The caller may set
 * cursor->index to -1 if it wants us to do a general rebalancing.
 * 
 * NOTE: Because we do not update the parent's parent in the split code,
 * the subtree_count used by the caller may be incorrect.  We correct it
 * here.  Also note that we cannot change the depth of the tree's leaf
 * nodes here (see btree_collapse()).
 *
 * This routine rebalances the children of the node, collapsing children
 * together if possible.  On return each child will have at least L/2-1
 * elements unless the node only has one child.
 */
static
int
btree_rebalance_node(hammer_btree_cursor_t cursor)
{
	struct hammer_btree_internal_node *node;
	hammer_btree_node_t children[HAMMER_BTREE_INT_ELMS];
	struct hammer_buffer *child_buffer[HAMMER_BTREE_INT_ELMS];
	hammer_btree_node_t child;
	hammer_btree_elm_inmemory_t elms;
	int i, j, n, nelms, goal;
	int maxelms, halfelms;
	int error;

	/*
	 * Basic setup
	 */
	node = &cursor->node->internal;
	KKASSERT(node->elms[cursor->index].subtree_offset != 0);
	error = 0;

	/*
	 * Load the children of node and do any necessary corrections
	 * to subtree_count.  subtree_count may be incorrect due to the
	 * way insertions split nodes.  Get a count of the total number
	 * of elements held by our children.
	 */
	error = 0;

	for (i = n = 0; i < node->base.count; ++i) {
		struct hammer_btree_internal_elm *elm;

		elm = &node->elms[i];
		children[i] = NULL;
		child_buffer[i] = NULL;	/* must be preinitialized for bread */
		if (elm->subtree_offset == 0)
			continue;
		child = hammer_bread(cursor->cluster, elm->subtree_offset,
				     HAMMER_FSBUF_BTREE, &error,
				     &child_buffer[i]);
		children[i] = child;
		if (child == NULL)
			continue;
		KKASSERT(node->base.subtype == child->base.type);

		/*
		 * Accumulate n for a good child, update the node's count
		 * if it was wrong.
		 */
		if (node->elms[i].subtree_count != child->base.count) {
			node->elms[i].subtree_count = child->base.count;
		}
		n += node->elms[i].subtree_count;
	}
	if (error)
		goto failed;

	/*
	 * Collect all the children's elements together
	 */
	nelms = n;
	elms = kmalloc(sizeof(*elms) * (nelms + 1), M_HAMMER, M_WAITOK|M_ZERO);
	for (i = n = 0; i < node->base.count; ++i) {
		child = children[i];
		for (j = 0; j < child->base.count; ++j) {
			elms[n].owner = child;
			if (node->base.subtype == HAMMER_BTREE_LEAF_NODE)
				elms[n].u.leaf = child->leaf.elms[j];
			else
				elms[n].u.internal = child->internal.elms[j];
			++n;
		}
	}
	KKASSERT(n == nelms);

	/*
	 * Store a boundary in the elms array to ease the code below.  This
	 * is only used if the children are internal nodes.
	 */
	elms[n].u.internal = node->elms[i];

	/*
	 * Calculate the number of elements each child should have (goal) by
	 * reducing the number of elements until we achieve at least
	 * halfelms - 1 per child, unless we are a degenerate case.
	 */
	maxelms = btree_max_elements(node->base.subtype);
	halfelms = maxelms / 2;

	goal = halfelms - 1;
	while (i && n / i < goal)
		--i;

	/*
	 * Now rebalance using the specified goal
	 */
	for (i = n = 0; i < node->base.count; ++i) {
		struct hammer_buffer *subchild_buffer = NULL;
		struct hammer_btree_internal_node *subchild;

		child = children[i];
		for (j = 0; j < goal && n < nelms; ++j) {
			if (node->base.subtype == HAMMER_BTREE_LEAF_NODE) {
				child->leaf.elms[j] = elms[n].u.leaf;
			} else {
				child->internal.elms[j] = elms[n].u.internal;
			}

			/*
			 * If the element's parent has changed we have to
			 * update the parent pointer.  This is somewhat
			 * expensive.
			 */
			if (elms[n].owner != child &&
			    node->base.subtype == HAMMER_BTREE_INTERNAL_NODE) {
				subchild = hammer_bread(cursor->cluster,
							elms[n].u.internal.subtree_offset,
							HAMMER_FSBUF_BTREE,
							&error,
							&subchild_buffer);
				if (subchild) {
					subchild->base.parent =
					    hammer_bclu_offset(child_buffer[i],
								child);
					hammer_modify_buffer(subchild_buffer);
				}
				/* XXX error */
			}
			++n;
		}
		/* 
		 * Set right boundary if the children are internal nodes.
		 */
		if (node->base.subtype == HAMMER_BTREE_INTERNAL_NODE)
			child->internal.elms[j] = elms[n].u.internal;
		child->base.count = j;
		hammer_modify_buffer(child_buffer[i]);
		if (subchild_buffer)
			hammer_put_buffer(subchild_buffer);

		/*
		 * If we have run out of elements, break out
		 */
		if (n == nelms)
			break;
	}

	/*
	 * Physically destroy any left-over children.  These children's
	 * elements have been packed into prior children.  The node's
	 * right hand boundary and count gets shifted to index i.
	 *
	 * The subtree count in the node's parent MUST be updated because
	 * we are removing elements.  The subtree_count field is allowed to
	 * be too small, but not too large!
	 */
	if (i != node->base.count) {
		n = i;
		node->elms[n] = node->elms[node->base.count];
		while (i < node->base.count) {
			hammer_free_btree_ptr(child_buffer[i], children[i]);
			hammer_put_buffer(child_buffer[i]);
			++i;
		}
		node->base.count = n;
		if (cursor->parent) {
			cursor->parent->elms[cursor->parent_index].subtree_count = n;
			hammer_modify_buffer(cursor->parent_buffer);
		}
	}

	kfree(elms, M_HAMMER);
failed:
	hammer_modify_buffer(cursor->node_buffer);
	for (i = 0; i < node->base.count; ++i) {
		if (child_buffer[i])
			hammer_put_buffer(child_buffer[i]);
	}
	return (error);
}

/*
 * This routine is only called if the cursor is at the root node and the
 * root node is an internal node.  We attempt to collapse the root node
 * by replacing it with all of its children, reducing tree depth by one.
 *
 * This is the only way to reduce tree depth in a HAMMER filesystem.
 * Note that all leaf nodes are at the same depth.
 *
 * This is a fairly expensive operation because we not only have to load
 * the root's children, we also have to scan each child and adjust the
 * parent offset for each element in each child.  Nasty all around.
 */
static
int
btree_collapse(hammer_btree_cursor_t cursor)
{
	hammer_btree_node_t root, child;
	hammer_btree_node_t children[HAMMER_BTREE_INT_ELMS];
	struct hammer_buffer *child_buffer[HAMMER_BTREE_INT_ELMS];
	int count;
	int i, j, n;
	int root_modified;
	int error;
	int32_t root_offset;
	u_int8_t subsubtype;

	root = cursor->node;
	count = root->base.count;
	root_offset = hammer_bclu_offset(cursor->node_buffer, root);

	/*
	 * Sum up the number of children each element has.  This value is
	 * only approximate due to the way the insertion node works.  It
	 * may be too small but it will never be too large.
	 *
	 * Quickly terminate the collapse if the elements have too many
	 * children.
	 */
	KKASSERT(root->base.parent == 0);	/* must be root node */
	KKASSERT(root->base.type == HAMMER_BTREE_INTERNAL_NODE);
	KKASSERT(count <= HAMMER_BTREE_INT_ELMS);

	for (i = n = 0; i < count; ++i) {
		n += root->internal.elms[i].subtree_count;
	}
	if (n > btree_max_elements(root->base.subtype))
		return(0);

	/*
	 * Iterate through the elements again and correct the subtree_count.
	 * Terminate the collapse if we wind up with too many.
	 */
	error = 0;
	root_modified = 0;

	for (i = n = 0; i < count; ++i) {
		struct hammer_btree_internal_elm *elm;

		elm = &root->internal.elms[i];
		child_buffer[i] = NULL;
		children[i] = NULL;
		if (elm->subtree_offset == 0)
			continue;
		child = hammer_bread(cursor->cluster, elm->subtree_offset,
				     HAMMER_FSBUF_BTREE, &error,
				     &child_buffer[i]);
		children[i] = child;
		if (child == NULL)
			continue;
		KKASSERT(root->base.subtype == child->base.type);

		/*
		 * Accumulate n for a good child, update the root's count
		 * if it was wrong.
		 */
		if (root->internal.elms[i].subtree_count != child->base.count) {
			root->internal.elms[i].subtree_count = child->base.count;
			root_modified = 1;
		}
		n += root->internal.elms[i].subtree_count;
	}
	if (error || n > btree_max_elements(root->base.subtype))
		goto done;

	/*
	 * Ok, we can collapse the root.  If the root's children are leafs
	 * the collapse is really simple.  If they are internal nodes the
	 * collapse is not so simple because we have to fixup the parent
	 * pointers for the root's children's children.
	 *
	 * When collapsing an internal node the far left and far right
	 * element's boundaries should match the root's left and right
	 * boundaries.
	 */
	if (root->base.subtype == HAMMER_BTREE_LEAF_NODE) {
		for (i = n = 0; i < count; ++i) {
			child = children[i];
			for (j = 0; j < child->base.count; ++j) {
				root->leaf.elms[n] = child->leaf.elms[j];
				++n;
			}
		}
		root->base.type = root->base.subtype;
		root->base.subtype = 0;
		root->base.count = n;
		root->leaf.link_left = 0;
		root->leaf.link_right = 0;
	} else {
		struct hammer_btree_internal_elm *elm;
		struct hammer_btree_internal_node *subchild;
		struct hammer_buffer *subchild_buffer = NULL;

		if (count) {
			child = children[0];
			subsubtype = child->base.subtype;
			KKASSERT(child->base.count > 0);
			KKASSERT(root->internal.elms[0].base.key ==
				 child->internal.elms[0].base.key);
			child = children[count-1];
			KKASSERT(child->base.count > 0);
			KKASSERT(root->internal.elms[count].base.key ==
			     child->internal.elms[child->base.count].base.key);
		} else {
			subsubtype = 0;
		}
		for (i = n = 0; i < count; ++i) {
			child = children[i];
			KKASSERT(child->base.subtype == subsubtype);
			for (j = 0; j < child->base.count; ++j) {
				elm = &child->internal.elms[j];

				root->internal.elms[n] = *elm;
				subchild = hammer_bread(cursor->cluster,
							elm->subtree_offset,
							HAMMER_FSBUF_BTREE,
							&error,
							&subchild_buffer);
				if (subchild) {
					subchild->base.parent = root_offset;
					hammer_modify_buffer(subchild_buffer);
				}
				++n;
			}
			/* make sure the right boundary is correct */
			/* (this gets overwritten when the loop continues) */
			/* XXX generate a new separator? */
			root->internal.elms[n] = child->internal.elms[j];
		}
		root->base.type = HAMMER_BTREE_INTERNAL_NODE;
		root->base.subtype = subsubtype;
		if (subchild_buffer)
			hammer_put_buffer(subchild_buffer);
	}
	root_modified = 1;

	/*
	 * Cleanup
	 */
done:
	if (root_modified)
		hammer_modify_buffer(cursor->node_buffer);
	for (i = 0; i < count; ++i) {
		if (child_buffer[i])
			hammer_put_buffer(child_buffer[i]);
	}
	return(error);
}

