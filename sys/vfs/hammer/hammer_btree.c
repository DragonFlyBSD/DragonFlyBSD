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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.c,v 1.18 2008/01/15 06:02:57 dillon Exp $
 */

/*
 * HAMMER B-Tree index
 *
 * HAMMER implements a modified B+Tree.  In documentation this will
 * simply be refered to as the HAMMER B-Tree.  Basically a HAMMER B-Tree
 * looks like a B+Tree (A B-Tree which stores its records only at the leafs
 * of the tree), but adds two additional boundary elements which describe
 * the left-most and right-most element a node is able to represent.  In
 * otherwords, we have boundary elements at the two ends of a B-Tree node
 * instead of sub-tree pointers.
 *
 * A B-Tree internal node looks like this:
 *
 *	B N N N N N N B   <-- boundary and internal elements
 *       S S S S S S S    <-- subtree pointers
 *
 * A B-Tree leaf node basically looks like this:
 *
 *	L L L L L L L L   <-- leaf elemenets
 *
 * The radix for an internal node is 1 less then a leaf but we get a
 * number of significant benefits for our troubles.
 *
 * The big benefit to using a B-Tree containing boundary information
 * is that it is possible to cache pointers into the middle of the tree
 * and not have to start searches, insertions, OR deletions at the root
 * node.   In particular, searches are able to progress in a definitive
 * direction from any point in the tree without revisting nodes.  This
 * greatly improves the efficiency of many operations, most especially
 * record appends.
 *
 * B-Trees also make the stacking of trees fairly straightforward.
 *
 * INTER-CLUSTER ELEMENTS:  An element of an internal node may reference
 * the root of another cluster rather then a node in the current cluster.
 * This is known as an inter-cluster references.  Only B-Tree searches
 * will cross cluster boundaries.  The rebalancing and collapse code does
 * not attempt to move children between clusters.  A major effect of this
 * is that we have to relax minimum element count requirements and allow
 * trees to become somewhat unabalanced.
 *
 * INSERTIONS AND DELETIONS:  When inserting we split full nodes on our
 * way down as an optimization.  I originally experimented with rebalancing
 * nodes on the way down for deletions but it created a huge mess due to
 * the way inter-cluster linkages work.  Instead, now I simply allow
 * the tree to become unbalanced and allow leaf nodes to become empty.
 * The delete code will try to clean things up from the bottom-up but
 * will stop if related elements are not in-core or if it cannot get a node
 * lock.
 */
#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

typedef enum btree_search_edge {
	SEARCH_NONE,
	SEARCH_LEFT_EDGE,
	SEARCH_RIGHT_EDGE 
} btree_search_edge_t;

static int btree_search(hammer_cursor_t cursor, int flags);
static int btree_edge_internal(hammer_cursor_t cursor,
			btree_search_edge_t edge);
static int btree_split_internal(hammer_cursor_t cursor);
static int btree_split_leaf(hammer_cursor_t cursor);
static int btree_remove(hammer_cursor_t cursor);
static int btree_set_parent(hammer_node_t node, hammer_btree_elm_t elm);
#if 0
static int btree_rebalance(hammer_cursor_t cursor);
static int btree_collapse(hammer_cursor_t cursor);
#endif
static int btree_node_is_almost_full(hammer_node_ondisk_t node);
static void hammer_make_separator(hammer_base_elm_t key1,
			hammer_base_elm_t key2, hammer_base_elm_t dest);

/*
 * Iterate records after a search.  The cursor is iterated forwards past
 * the current record until a record matching the key-range requirements
 * is found.  ENOENT is returned if the iteration goes past the ending
 * key.
 *
 * The iteration is inclusive of key_beg and can be inclusive or exclusive
 * of key_end depending on whether HAMMER_CURSOR_END_INCLUSIVE is set.
 *
 * cursor->key_beg may or may not be modified by this function during
 * the iteration.  XXX future - in case of an inverted lock we may have
 * to reinitiate the lookup and set key_beg to properly pick up where we
 * left off.
 */
int
hammer_btree_iterate(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int error;
	int r;
	int s;

	/*
	 * Skip past the current record
	 */
	node = cursor->node->ondisk;
	if (node == NULL)
		return(ENOENT);
	if (cursor->index < node->count && 
	    (cursor->flags & HAMMER_CURSOR_ATEDISK)) {
		++cursor->index;
	}

	/*
	 * Loop until an element is found or we are done.
	 */
	for (;;) {
		/*
		 * We iterate up the tree and then index over one element
		 * while we are at the last element in the current node.
		 *
		 * NOTE: This can pop us up to another cluster.
		 *
		 * If we are at the root of the root cluster, cursor_up
		 * returns ENOENT.
		 *
		 * NOTE: hammer_cursor_up() will adjust cursor->key_beg
		 * when told to re-search for the cluster tag.
		 *
		 * XXX this could be optimized by storing the information in
		 * the parent reference.
		 *
		 * XXX we can lose the node lock temporarily, this could mess
		 * up our scan.
		 */
		if (cursor->index == node->count) {
			error = hammer_cursor_up(cursor, 0);
			if (error)
				break;
			node = cursor->node->ondisk;
			KKASSERT(cursor->index != node->count);
			++cursor->index;
			continue;
		}

		/*
		 * Check internal or leaf element.  Determine if the record
		 * at the cursor has gone beyond the end of our range.
		 *
		 * Generally we recurse down through internal nodes.  An
		 * internal node can only be returned if INCLUSTER is set
		 * and the node represents a cluster-push record.  Internal
		 * elements do not contain create_tid/delete_tid information.
		 */
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm[0].base);
			s = hammer_btree_cmp(&cursor->key_beg, &elm[1].base);
			if (hammer_debug_btree) {
				kprintf("BRACKETL %p:%d %016llx %02x %016llx %d\n",
					cursor->node, cursor->index,
					elm[0].internal.base.obj_id,
					elm[0].internal.base.rec_type,
					elm[0].internal.base.key,
					r
				);
				kprintf("BRACKETR %p:%d %016llx %02x %016llx %d\n",
					cursor->node, cursor->index + 1,
					elm[1].internal.base.obj_id,
					elm[1].internal.base.rec_type,
					elm[1].internal.base.key,
					s
				);
			}

			if (r < 0) {
				error = ENOENT;
				break;
			}
			if (r == 0 && (cursor->flags & HAMMER_CURSOR_END_INCLUSIVE) == 0) {
				error = ENOENT;
				break;
			}
			KKASSERT(s <= 0);
			if ((cursor->flags & HAMMER_CURSOR_INCLUSTER) == 0 ||
			    elm->internal.rec_offset == 0) {
				error = hammer_cursor_down(cursor);
				if (error)
					break;
				KKASSERT(cursor->index == 0);
				node = cursor->node->ondisk;
				continue;
			}
		} else {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm->base);
			if (hammer_debug_btree) {
				kprintf("ELEMENT  %p:%d %016llx %02x %016llx %d\n",
					cursor->node, cursor->index,
					elm[0].leaf.base.obj_id,
					elm[0].leaf.base.rec_type,
					elm[0].leaf.base.key,
					r
				);
			}
			if (r < 0) {
				error = ENOENT;
				break;
			}
			if (r == 0 && (cursor->flags & HAMMER_CURSOR_END_INCLUSIVE) == 0) {
				error = ENOENT;
				break;
			}
			if ((cursor->flags & HAMMER_CURSOR_ALLHISTORY) == 0 &&
			    hammer_btree_chkts(cursor->key_beg.create_tid,
					       &elm->base) != 0) {
				++cursor->index;
				continue;
			}
		}

		/*
		 * Return entry
		 */
		if (hammer_debug_btree) {
			int i = cursor->index;
			hammer_btree_elm_t elm = &cursor->node->ondisk->elms[i];
			kprintf("ITERATE  %p:%d %016llx %02x %016llx\n",
				cursor->node, i,
				elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				elm->internal.base.key
			);
		}
		return(0);
	}
	return(error);
}

/*
 * Lookup cursor->key_beg.  0 is returned on success, ENOENT if the entry
 * could not be found, and a fatal error otherwise.
 * 
 * The cursor is suitably positioned for a deletion on success, and suitably
 * positioned for an insertion on ENOENT.
 *
 * The cursor may begin anywhere, the search will traverse clusters in
 * either direction to locate the requested element.
 */
int
hammer_btree_lookup(hammer_cursor_t cursor)
{
	int error;

	error = btree_search(cursor, 0);
	if (error == 0 && cursor->flags)
		error = hammer_btree_extract(cursor, cursor->flags);
	return(error);
}

/*
 * Execute the logic required to start an iteration.  The first record
 * located within the specified range is returned and iteration control
 * flags are adjusted for successive hammer_btree_iterate() calls.
 */
int
hammer_btree_first(hammer_cursor_t cursor)
{
	int error;

	error = hammer_btree_lookup(cursor);
	if (error == ENOENT) {
		cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		error = hammer_btree_iterate(cursor);
	}
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	return(error);
}

/*
 * Extract the record and/or data associated with the cursor's current
 * position.  Any prior record or data stored in the cursor is replaced.
 * The cursor must be positioned at a leaf node.
 *
 * NOTE: Most extractions occur at the leaf of the B-Tree.  The only
 * 	 extraction allowed at an internal element is at a cluster-push.
 *	 Cluster-push elements have records but no data.
 */
int
hammer_btree_extract(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	hammer_cluster_t cluster;
	u_int64_t buf_type;
	int32_t cloff;
	int32_t roff;
	int error;

	/*
	 * A cluster record type has no data reference, the information
	 * is stored directly in the record and B-Tree element.
	 *
	 * The case where the data reference resolves to the same buffer
	 * as the record reference must be handled.
	 */
	node = cursor->node->ondisk;
	elm = &node->elms[cursor->index];
	cluster = cursor->node->cluster;
	cursor->flags &= ~HAMMER_CURSOR_DATA_EMBEDDED;
	cursor->data = NULL;
	error = 0;

	/*
	 * Internal elements can only be cluster pushes.  A cluster push has
	 * no data reference.
	 */
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
		cloff = elm->internal.rec_offset;
		KKASSERT(cloff != 0);
		cursor->record = hammer_bread(cluster, cloff,
					      HAMMER_FSBUF_RECORDS, &error,
					      &cursor->record_buffer);
		return(error);
	}

	/*
	 * Leaf element.
	 */
	if ((flags & HAMMER_CURSOR_GET_RECORD) && error == 0) {
		cloff = elm->leaf.rec_offset;
		cursor->record = hammer_bread(cluster, cloff,
					      HAMMER_FSBUF_RECORDS, &error,
					      &cursor->record_buffer);
	} else {
		cloff = 0;
	}
	if ((flags & HAMMER_CURSOR_GET_DATA) && error == 0) {
		if ((cloff ^ elm->leaf.data_offset) & ~HAMMER_BUFMASK) {
			/*
			 * The data is not in the same buffer as the last
			 * record we cached, but it could still be embedded
			 * in a record.  Note that we may not have loaded the
			 * record's buffer above, depending on flags.
			 */
			if ((elm->leaf.rec_offset ^ elm->leaf.data_offset) &
			    ~HAMMER_BUFMASK) {
				if (elm->leaf.data_len & HAMMER_BUFMASK)
					buf_type = HAMMER_FSBUF_DATA;
				else
					buf_type = 0;	/* pure data buffer */
			} else {
				buf_type = HAMMER_FSBUF_RECORDS;
			}
			cursor->data = hammer_bread(cluster,
						  elm->leaf.data_offset,
						  buf_type, &error,
						  &cursor->data_buffer);
		} else {
			/*
			 * Data in same buffer as record.  Note that we
			 * leave any existing data_buffer intact, even
			 * though we don't use it in this case, in case
			 * other records extracted during an iteration
			 * go back to it.
			 *
			 * The data must be embedded in the record for this
			 * case to be hit.
			 *
			 * Just assume the buffer type is correct.
			 */
			cursor->data = (void *)
				((char *)cursor->record_buffer->ondisk +
				 (elm->leaf.data_offset & HAMMER_BUFMASK));
			roff = (char *)cursor->data - (char *)cursor->record;
			KKASSERT (roff >= 0 && roff < HAMMER_RECORD_SIZE);
			cursor->flags |= HAMMER_CURSOR_DATA_EMBEDDED;
		}
	}
	return(error);
}


/*
 * Insert a leaf element into the B-Tree at the current cursor position.
 * The cursor is positioned such that the element at and beyond the cursor
 * are shifted to make room for the new record.
 *
 * The caller must call hammer_btree_lookup() with the HAMMER_CURSOR_INSERT
 * flag set and that call must return ENOENT before this function can be
 * called.
 *
 * ENOSPC is returned if there is no room to insert a new record.
 */
int
hammer_btree_insert(hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_node_ondisk_t parent;
	hammer_node_ondisk_t node;
	int i;

	/*
	 * Insert the element at the leaf node and update the count in the
	 * parent.  It is possible for parent to be NULL, indicating that
	 * the root of the B-Tree in the cluster is a leaf.  It is also
	 * possible for the leaf to be empty.
	 *
	 * Remember that the right-hand boundary is not included in the
	 * count.
	 */
	hammer_modify_node(cursor->node);
	node = cursor->node->ondisk;
	i = cursor->index;
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(node->count < HAMMER_BTREE_LEAF_ELMS);
	if (i != node->count) {
		bcopy(&node->elms[i], &node->elms[i+1],
		      (node->count - i) * sizeof(*elm));
	}
	node->elms[i] = *elm;
	++node->count;

	KKASSERT(hammer_btree_cmp(cursor->left_bound, &elm->leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &elm->leaf.base) > 0);
	if (i)
		KKASSERT(hammer_btree_cmp(&node->elms[i-1].leaf.base, &elm->leaf.base) < 0);
	if (i != node->count - 1)
		KKASSERT(hammer_btree_cmp(&node->elms[i+1].leaf.base, &elm->leaf.base) > 0);

	/*
	 * Adjust the sub-tree count in the parent.  note that the parent
	 * may be in a different cluster.
	 */
	if (cursor->parent) {
		hammer_modify_node(cursor->parent);
		parent = cursor->parent->ondisk;
		i = cursor->parent_index;
		++parent->elms[i].internal.subtree_count;
		KKASSERT(parent->elms[i].internal.subtree_count <= node->count);
	}
	return(0);
}

/*
 * Insert a cluster push into the B-Tree at the current cursor position.
 * The cursor is positioned at a leaf after a failed btree_lookup.
 *
 * The caller must call hammer_btree_lookup() with the HAMMER_CURSOR_INSERT
 * flag set and that call must return ENOENT before this function can be
 * called.
 *
 * This routine is used ONLY during a recovery pass while the originating
 * cluster is serialized.  The leaf is broken up into up to three pieces,
 * causing up to an additional internal elements to be added to the parent.
 *
 * ENOSPC is returned if there is no room to insert a new record.
 */
int
hammer_btree_insert_cluster(hammer_cursor_t cursor, hammer_cluster_t ncluster,
			    int32_t rec_offset)
{
	hammer_cluster_t ocluster;
	hammer_node_ondisk_t parent;
	hammer_node_ondisk_t node;
	hammer_node_ondisk_t xnode;	/* additional leaf node */
	hammer_node_t	     new_node;
	hammer_btree_elm_t   elm;
	const int esize = sizeof(*elm);
	u_int8_t save;
	int error = 0;
	int pi, i;

	kprintf("cursor %p ncluster %p\n", cursor, ncluster);
	hammer_modify_node(cursor->node);
	node = cursor->node->ondisk;
	i = cursor->index;
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(node->count < HAMMER_BTREE_LEAF_ELMS);

	/*
	 * Make sure the spike is legal or the B-Tree code will get really
	 * confused.
	 */
	KKASSERT(hammer_btree_cmp(&ncluster->ondisk->clu_btree_beg,
				  cursor->left_bound) >= 0);
	KKASSERT(hammer_btree_cmp(&ncluster->ondisk->clu_btree_end,
				  cursor->right_bound) <= 0);
	if (i != node->count) {
		KKASSERT(hammer_btree_cmp(&ncluster->ondisk->clu_btree_end,
					  &node->elms[i].leaf.base) <= 0);
	}

	/*
	 * If we are at the local root of the cluster a new root node
	 * must be created, because we need an internal node.  The
	 * caller has already marked the source cluster as undergoing
	 * modification.
	 */
	ocluster = cursor->node->cluster;
	if (cursor->parent == NULL) {
		cursor->parent = hammer_alloc_btree(ocluster, &error);
		if (error)
			return(error);
		hammer_lock_ex(&cursor->parent->lock);
		hammer_modify_node(cursor->parent);
		parent = cursor->parent->ondisk;
		parent->count = 1;
		parent->parent = 0;
		parent->type = HAMMER_BTREE_TYPE_INTERNAL;
		parent->elms[0].base = ocluster->clu_btree_beg;
		parent->elms[0].base.subtree_type = node->type;
		parent->elms[0].internal.subtree_offset = cursor->node->node_offset;
		parent->elms[0].internal.subtree_count = node->count;
		parent->elms[1].base = ocluster->clu_btree_end;
		cursor->parent_index = 0;
		cursor->left_bound = &parent->elms[0].base;
		cursor->right_bound = &parent->elms[1].base;
		node->parent = cursor->parent->node_offset;
		ocluster->ondisk->clu_btree_root = cursor->parent->node_offset;
		kprintf("no parent\n");
	} else {
		kprintf("has parent\n");
	}


	KKASSERT(cursor->parent->ondisk->count <= HAMMER_BTREE_INT_ELMS - 2);

	hammer_modify_node(cursor->parent);
	parent = cursor->parent->ondisk;
	pi = cursor->parent_index;

	kprintf("%d node %d/%d (%c) offset=%d parent=%d\n",
		cursor->node->cluster->clu_no,
		i, node->count, node->type, cursor->node->node_offset, node->parent);

	/*
	 * If the insertion point bisects the node we will need to allocate
	 * a second leaf node to copy the right hand side into.
	 */
	if (i != 0 && i != node->count) {
		new_node = hammer_alloc_btree(cursor->node->cluster, &error);
		if (error)
			return(error);
		xnode = new_node->ondisk;
		bcopy(&node->elms[i], &xnode->elms[0],
		      (node->count - i) * esize);
		xnode->count = node->count - i;
		xnode->parent = cursor->parent->node_offset;
		xnode->type = HAMMER_BTREE_TYPE_LEAF;
		node->count = i;
		parent->elms[pi].internal.subtree_count = node->count;
	} else {
		new_node = NULL;
		xnode = NULL;
	}

	/*
	 * Adjust the parent and set pi to point at the internal element
	 * which we intended to hold the spike.
	 */
	if (new_node) {
		/*
		 * Insert spike after parent index.  Spike is at pi + 1.
		 * Also include room after the spike for new_node
		 */
		++pi;
		bcopy(&parent->elms[pi], &parent->elms[pi+2],
		      (parent->count - pi + 1) * esize);
		parent->count += 2;
	} else if (i == 0) {
		/*
		 * Insert spike before parent index.  Spike is at pi.
		 *
		 * cursor->node's index in the parent (cursor->parent_index)
		 * has now shifted over by one.
		 */
		bcopy(&parent->elms[pi], &parent->elms[pi+1],
		      (parent->count - pi + 1) * esize);
		++parent->count;
		++cursor->parent_index;
	} else {
		/*
		 * Insert spike after parent index.  Spike is at pi + 1.
		 */
		++pi;
		bcopy(&parent->elms[pi], &parent->elms[pi+1],
		      (parent->count - pi + 1) * esize);
		++parent->count;
	}

	/*
	 * Load the spike into the parent at (pi).
	 *
	 * WARNING: subtree_type is actually overloaded within base.
	 * WARNING: subtree_clu_no is overloaded on subtree_offset
	 */
	elm = &parent->elms[pi];
	elm[0].internal.base = ncluster->ondisk->clu_btree_beg;
	elm[0].internal.base.subtree_type = HAMMER_BTREE_TYPE_CLUSTER;
	elm[0].internal.rec_offset = rec_offset;
	elm[0].internal.subtree_clu_no = ncluster->clu_no;
	elm[0].internal.subtree_vol_no = ncluster->volume->vol_no;
	elm[0].internal.subtree_count = 0; /* XXX */

	/*
	 * Load the new node into parent at (pi+1) if non-NULL, and also
	 * set the right-hand boundary for the spike.
	 *
	 * Because new_node is a leaf its elements do not point to any
	 * nodes so we don't have to scan it to adjust parent pointers.
	 *
	 * WARNING: subtree_type is actually overloaded within base.
	 * WARNING: subtree_clu_no is overloaded on subtree_offset
	 *
	 * XXX right-boundary may not match clu_btree_end if spike is
	 *     at the end of the internal node.  For now the cursor search
	 *     insertion code will deal with it.
	 */
	if (new_node) {
		elm[1].internal.base = ncluster->ondisk->clu_btree_end;
		elm[1].internal.base.subtree_type = HAMMER_BTREE_TYPE_LEAF;
		elm[1].internal.subtree_offset = new_node->node_offset;
		elm[1].internal.subtree_count = xnode->count;
		elm[1].internal.subtree_vol_no = -1;
		elm[1].internal.rec_offset = 0;
	} else {
		/*
		 * The right boundary is only the base part of elm[1].
		 * The rest belongs to elm[1]'s recursion.  Note however
		 * that subtree_type is overloaded within base so we 
		 * have to retain it as well.
		 */
		save = elm[1].internal.base.subtree_type;
		elm[1].internal.base = ncluster->ondisk->clu_btree_end;
		elm[1].internal.base.subtree_type = save;
	}

	/*
	 * The boundaries stored in the cursor for node are probably all
	 * messed up now, fix them.
	 */
	cursor->left_bound = &parent->elms[cursor->parent_index].base;
	cursor->right_bound = &parent->elms[cursor->parent_index+1].base;

	KKASSERT(hammer_btree_cmp(&ncluster->ondisk->clu_btree_end,
				  &elm[1].internal.base) <= 0);


	/*
	 * Adjust the target cluster's parent offset
	 */
	hammer_modify_cluster(ncluster);
	ncluster->ondisk->clu_btree_parent_offset = cursor->parent->node_offset;

	if (new_node)
		hammer_rel_node(new_node);

	return(0);
}

/*
 * Delete a record from the B-Tree's at the current cursor position.
 * The cursor is positioned such that the current element is the one
 * to be deleted.
 *
 * On return the cursor will be positioned after the deleted element and
 * MAY point to an internal node.  It will be suitable for the continuation
 * of an iteration but not for an insertion or deletion.
 *
 * Deletions will attempt to partially rebalance the B-Tree in an upward
 * direction.  It is possible to end up with empty leafs.  An empty internal
 * node is impossible (worst case: it has one element pointing to an empty
 * leaf).
 */
int
hammer_btree_delete(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t node;
	hammer_node_t parent;
	hammer_btree_elm_t elm;
	int error;
	int i;

	/*
	 * Delete the element from the leaf node. 
	 *
	 * Remember that leaf nodes do not have boundaries.
	 */
	node = cursor->node;
	ondisk = node->ondisk;
	i = cursor->index;

	KKASSERT(ondisk->type == HAMMER_BTREE_TYPE_LEAF);
	hammer_modify_node(node);
	if (i + 1 != ondisk->count) {
		bcopy(&ondisk->elms[i+1], &ondisk->elms[i],
		      (ondisk->count - i - 1) * sizeof(ondisk->elms[0]));
	}
	--ondisk->count;
	if (cursor->parent != NULL) {
		/*
		 * Adjust parent's notion of the leaf's count.  subtree_count
		 * is only approximate, it is allowed to be too small but
		 * never allowed to be too large.  Make sure we don't drop
		 * the count below 0.
		 */
		parent = cursor->parent;
		hammer_modify_node(parent);
		elm = &parent->ondisk->elms[cursor->parent_index];
		if (elm->internal.subtree_count)
			--elm->internal.subtree_count;
		KKASSERT(elm->internal.subtree_count <= ondisk->count);
	}

	/*
	 * It is possible, but not desireable, to stop here.  If the element
	 * count drops to 0 (which is allowed for a leaf), try recursively
	 * remove the B-Tree node.
	 *
	 * XXX rebalancing calls would go here too.
	 *
	 * This may reposition the cursor at one of the parent's of the
	 * current node.
	 */
	KKASSERT(cursor->index <= ondisk->count);
	if (ondisk->count == 0) {
		error = btree_remove(cursor);
		if (error == EAGAIN)
			error = 0;
	} else {
		error = 0;
	}
	return(error);
}

/*
 * PRIMAY B-TREE SEARCH SUPPORT PROCEDURE
 *
 * Search a cluster's B-Tree for cursor->key_beg, return the matching node.
 *
 * The search can begin ANYWHERE in the B-Tree.  As a first step the search
 * iterates up the tree as necessary to properly position itself prior to
 * actually doing the sarch.
 * 
 * INSERTIONS: The search will split full nodes and leaves on its way down
 * and guarentee that the leaf it ends up on is not full.  If we run out
 * of space the search continues to the leaf (to position the cursor for
 * the spike), but ENOSPC is returned.
 *
 * XXX this isn't optimal - we really need to just locate the end point and
 * insert space going up, and if we get a deadlock just release and retry
 * the operation.  Or something like that.  The insertion code can transit
 * multiple clusters and run splits in unnecessary clusters.
 *
 * DELETIONS: The search will rebalance the tree on its way down. XXX
 *
 * The search is only guarenteed to end up on a leaf if an error code of 0
 * is returned, or if inserting and an error code of ENOENT is returned.
 * Otherwise it can stop at an internal node.  On success a search returns
 * a leaf node unless INCLUSTER is set and the search located a cluster push
 * node (which is an internal node).
 */
static 
int
btree_search(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_cluster_t cluster;
	hammer_btree_elm_t elm;
	btree_search_edge_t edge;
	int error;
	int enospc = 0;
	int i;
	int r;

	flags |= cursor->flags;

	if (hammer_debug_btree) {
		kprintf("SEARCH   %p:%d %016llx %02x key=%016llx tid=%016llx\n",
			cursor->node, cursor->index,
			cursor->key_beg.obj_id,
			cursor->key_beg.rec_type,
			cursor->key_beg.key,
			cursor->key_beg.create_tid
		);
	}

	/*
	 * Move our cursor up the tree until we find a node whos range covers
	 * the key we are trying to locate.  This may move us between
	 * clusters.
	 *
	 * The left bound is inclusive, the right bound is non-inclusive.
	 * It is ok to cursor up too far so when cursoring across a cluster
	 * boundary.
	 *
	 * First see if we can skip the whole cluster.  hammer_cursor_up()
	 * handles both cases but this way we don't check the cluster
	 * bounds when going up the tree within a cluster.
	 *
	 * NOTE: If INCLUSTER is set and we are at the root of the cluster,
	 * hammer_cursor_up() will return ENOENT.
	 */
	cluster = cursor->node->cluster;
	while (
	    hammer_btree_cmp(&cursor->key_beg, &cluster->clu_btree_beg) < 0 ||
	    hammer_btree_cmp(&cursor->key_beg, &cluster->clu_btree_end) >= 0) {
		error = hammer_cursor_toroot(cursor);
		if (error)
			goto done;
		KKASSERT(cursor->parent);
		error = hammer_cursor_up(cursor, 0);
		if (error)
			goto done;
		cluster = cursor->node->cluster;
	}

	/*
	 * Deal with normal cursoring within a cluster.  The right bound
	 * is non-inclusive.  That is, the bounds form a separator.
	 */
	while (hammer_btree_cmp(&cursor->key_beg, cursor->left_bound) < 0 ||
	       hammer_btree_cmp(&cursor->key_beg, cursor->right_bound) >= 0) {
		KKASSERT(cursor->parent);
		error = hammer_cursor_up(cursor, 0);
		if (error)
			goto done;
	}

	/*
	 * We better have ended up with a node somewhere, and our second
	 * while loop had better not have traversed up a cluster.
	 */
	KKASSERT(cursor->node != NULL && cursor->node->cluster == cluster);

	/*
	 * If we are inserting we can't start at a full node if the parent
	 * is also full (because there is no way to split the node),
	 * continue running up the tree until we hit the root of the
	 * root cluster or until the requirement is satisfied.
	 *
	 * NOTE: These cursor-up's CAN continue to cross cluster boundaries.
	 *
	 * NOTE: We must guarantee at least two open spots in the parent
	 *       to deal with hammer_btree_insert_cluster().
	 *
	 * XXX as an optimization it should be possible to unbalance the tree
	 * and stop at the root of the current cluster.
	 */
	while ((flags & HAMMER_CURSOR_INSERT) && enospc == 0) {
		if (btree_node_is_almost_full(cursor->node->ondisk) == 0)
			break;
		if (cursor->parent == NULL)
			break;
		if (cursor->parent->ondisk->count != HAMMER_BTREE_INT_ELMS)
			break;
		error = hammer_cursor_up(cursor, 0);
		/* cluster and node are now may become stale */
		if (error)
			goto done;
	}
	/* cluster = cursor->node->cluster; not needed until next cluster = */

#if 0
	/*
	 * If we are deleting we can't start at an internal node with only
	 * one element unless it is root, because all of our code assumes
	 * that internal nodes will never be empty.  Just do this generally
	 * for both leaf and internal nodes to get better balance.
	 *
	 * This handles the case where the cursor is sitting at a leaf and
	 * either the leaf or parent contain an insufficient number of
	 * elements.
	 *
	 * NOTE: These cursor-up's CAN continue to cross cluster boundaries.
	 *
	 * XXX NOTE: Iterations may not set this flag anyway.
	 */
	while (flags & HAMMER_CURSOR_DELETE) {
		if (cursor->node->ondisk->count > 1)
			break;
		if (cursor->parent == NULL)
			break;
		KKASSERT(cursor->node->ondisk->count != 0);
		error = hammer_cursor_up(cursor, 0);
		/* cluster and node are now may become stale */
		if (error)
			goto done;
	}
#endif

/*new_cluster:*/
	/*
	 * Push down through internal nodes to locate the requested key.
	 */
	cluster = cursor->node->cluster;
	node = cursor->node->ondisk;
	while (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
#if 0
		/*
		 * If we are a the root node and deleting, try to collapse
		 * all of the root's children into the root.  This is the
		 * only point where tree depth is reduced.
		 *
		 * XXX NOTE: Iterations may not set this flag anyway.
		 */
		if ((flags & HAMMER_CURSOR_DELETE) && cursor->parent == NULL) {
			error = btree_collapse(cursor);
			/* node becomes stale after call */
			/* XXX ENOSPC */
			if (error)
				goto done;
		}
		node = cursor->node->ondisk;
#endif
		/*
		 * Scan the node to find the subtree index to push down into.
		 * We go one-past, then back-up.
		 *
		 * We have a serious issue with the midpoints for internal
		 * nodes when the midpoint bisects two historical records
		 * (where only create_tid is different).  Short of iterating
		 * through the record's entire history the only solution is
		 * to calculate a midpoint that isn't a midpoint in that
		 * case.   Please see hammer_make_separator() for more
		 * information.
		 *
		 * The right boundary is included in the search.
		 */
		for (i = 0; i <= node->count; ++i) {
			elm = &node->elms[i];
			r = hammer_btree_cmp(&cursor->key_beg, &elm->base);
			if (r < 0)
				break;
		}

		/*
		 * These cases occur when the parent's idea of the boundary
		 * is wider then the child's idea of the boundary, and
		 * require special handling.  If not inserting we can
		 * terminate the search early for these cases but the
		 * child's boundaries cannot be unconditionally modified.
		 */
		edge = SEARCH_NONE;
		if (i == 0) {
			/*
			 * If i == 0 the search terminated to the LEFT of the
			 * left_boundary but to the RIGHT of the parent's left
			 * boundary.
			 */
			u_int8_t save;

			if ((flags & HAMMER_CURSOR_INSERT) == 0) {
				cursor->index = 0;
				return(ENOENT);
			}
			elm = &node->elms[0];

			if (elm->base.subtree_type ==
			    HAMMER_BTREE_TYPE_CLUSTER) {
				edge = SEARCH_LEFT_EDGE;
			} else {
				/*
				 * Correct a left-hand boundary mismatch.
				 */
				hammer_modify_node(cursor->node);
				save = node->elms[0].base.subtree_type;
				node->elms[0].base = *cursor->left_bound;
				node->elms[0].base.subtree_type = save;
			}
		} else if (i == node->count + 1) {
			/*
			 * If i == node->count + 1 the search terminated to
			 * the RIGHT of the right boundary but to the LEFT
			 * of the parent's right boundary.
			 *
			 * Note that the last element in this case is
			 * elms[i-2] prior to adjustments to 'i'.
			 */
			--i;
			if ((flags & HAMMER_CURSOR_INSERT) == 0) {
				cursor->index = i;
				return(ENOENT);
			}

			elm = &node->elms[i];
			if (elm[-1].base.subtree_type ==
			    HAMMER_BTREE_TYPE_CLUSTER) {
				edge = SEARCH_RIGHT_EDGE;
			} else {
				hammer_modify_node(cursor->node);
				elm->base = *cursor->right_bound;
			}
		} else {
			/*
			 * The push-down index is now i - 1.  If we had
			 * terminated on the right boundary this will point
			 * us at the last element.
			 */
			--i;
		}
		cursor->index = i;

		if (hammer_debug_btree) {
			elm = &node->elms[i];
			kprintf("SEARCH-I %p:%d %016llx %02x key=%016llx tid=%016llx\n",
				cursor->node, i,
				elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				elm->internal.base.key,
				elm->internal.base.create_tid
			);
		}

		/*
		 * Handle insertion and deletion requirements.
		 *
		 * If inserting split full nodes.  The split code will
		 * adjust cursor->node and cursor->index if the current
		 * index winds up in the new node.
		 *
		 * If inserting and a left or right edge case was detected,
		 * we cannot correct the left or right boundary and must
		 * prepend and append an empty leaf node in order to make
		 * the boundary correction.
		 *
		 * If we run out of space we set enospc and continue on
		 * to a leaf to provide the spike code with a good point
		 * of entry.  Enospc is reset if we cross a cluster boundary.
		 */
		if ((flags & HAMMER_CURSOR_INSERT) && enospc == 0) {
			if (btree_node_is_almost_full(node)) {
				error = btree_split_internal(cursor);
				if (error) {
					if (error != ENOSPC)
						goto done;
					enospc = 1;
				}
				/*
				 * reload stale pointers
				 */
				i = cursor->index;
				node = cursor->node->ondisk;
			}
			if (edge != SEARCH_NONE && enospc == 0) {
				error = btree_edge_internal(cursor, edge);
				if (error) {
					if (error != ENOSPC)
						goto done;
					enospc = 1;
				}
				/*
				 * reload stale pointers
				 */
				i = cursor->index;
				node = cursor->node->ondisk;
			}
		}

#if 0
		/*
		 * If deleting rebalance - do not allow the child to have
		 * just one element or we will not be able to delete it.
		 *
		 * Neither internal or leaf nodes (except a root-leaf) are
		 * allowed to drop to 0 elements.  (XXX - well, leaf nodes
		 * can at the moment).
		 *
		 * Our separators may have been reorganized after rebalancing,
		 * so we have to pop back up and rescan.
		 *
		 * XXX test for subtree_count < maxelms / 2, minus 1 or 2
		 * for hysteresis?
		 *
		 * XXX NOTE: Iterations may not set this flag anyway.
		 */
		if (flags & HAMMER_CURSOR_DELETE) {
			if (node->elms[i].internal.subtree_count <= 1) {
				error = btree_rebalance(cursor);
				if (error)
					goto done;
				/* cursor->index is invalid after call */
				goto new_cluster;
			}
		}
#endif
		/*
		 * A non-zero rec_offset specifies a cluster push.
		 * If this is a cluster push we reset the enospc flag,
		 * which reenables the insertion code in the new cluster.
		 * This also ensures that if a spike occurs both its node
		 * and its parent will be in the same cluster.
		 *
		 * If INCLUSTER is set we terminate at the cluster boundary.
		 * In this case we must determine whether key_beg is within
		 * the cluster's boundary or not. XXX
		 */
		elm = &node->elms[i];
		if (elm->internal.rec_offset) {
			KKASSERT(elm->base.subtree_type ==
				 HAMMER_BTREE_TYPE_CLUSTER);
			enospc = 0;
			if (flags & HAMMER_CURSOR_INCLUSTER) {
				KKASSERT((flags & HAMMER_CURSOR_INSERT) == 0);
				r = hammer_btree_cmp(&cursor->key_beg,
						     &elm[1].base);
				error = (r < 0) ? 0 : ENOENT;
				goto done;
			}
		}

		/*
		 * Push down (push into new node, existing node becomes
		 * the parent) and continue the search.
		 */
		error = hammer_cursor_down(cursor);
		/* node and cluster become stale */
		if (error)
			goto done;
		node = cursor->node->ondisk;
		cluster = cursor->node->cluster;
	}

	/*
	 * We are at a leaf, do a linear search of the key array.
	 *
	 * On success the index is set to the matching element and 0
	 * is returned.
	 *
	 * On failure the index is set to the insertion point and ENOENT
	 * is returned.
	 *
	 * Boundaries are not stored in leaf nodes, so the index can wind
	 * up to the left of element 0 (index == 0) or past the end of
	 * the array (index == node->count).
	 */
	KKASSERT(node->count <= HAMMER_BTREE_LEAF_ELMS);

	for (i = 0; i < node->count; ++i) {
		r = hammer_btree_cmp(&cursor->key_beg, &node->elms[i].base);

		/*
		 * Stop if we've flipped past key_beg.  This includes a
		 * record whos create_tid is larger then our asof id.
		 */
		if (r < 0)
			break;

		/*
		 * Return an exact match.  In this case we have to do special
		 * checks if the only difference in the records is the
		 * create_ts, in order to properly match against our as-of
		 * query.
		 */
		if (r >= 0 && r <= 1) {
			if ((cursor->flags & HAMMER_CURSOR_ALLHISTORY) == 0 &&
			    hammer_btree_chkts(cursor->key_beg.create_tid,
					       &node->elms[i].base) != 0) {
				continue;
			}
			cursor->index = i;
			error = 0;
			if (hammer_debug_btree) {
				kprintf("SEARCH-L %p:%d (SUCCESS)\n",
					cursor->node, i);
			}
			goto done;
		}
	}

	if (hammer_debug_btree) {
		kprintf("SEARCH-L %p:%d (FAILED)\n",
			cursor->node, i);
	}

	/*
	 * No exact match was found, i is now at the insertion point.
	 *
	 * If inserting split a full leaf before returning.  This
	 * may have the side effect of adjusting cursor->node and
	 * cursor->index.
	 */
	cursor->index = i;
	if ((flags & HAMMER_CURSOR_INSERT) && btree_node_is_almost_full(node)) {
		error = btree_split_leaf(cursor);
		if (error) {
			if (error != ENOSPC)
				goto done;
			enospc = 1;
			flags &= ~HAMMER_CURSOR_INSERT;
		}
		/*
		 * reload stale pointers
		 */
		/* NOT USED
		i = cursor->index;
		node = &cursor->node->internal;
		*/
	}

	/*
	 * We reached a leaf but did not find the key we were looking for.
	 * If this is an insert we will be properly positioned for an insert
	 * (ENOENT) or spike (ENOSPC) operation.
	 */
	error = enospc ? ENOSPC : ENOENT;
done:
	return(error);
}


/************************************************************************
 *			   SPLITTING AND MERGING 			*
 ************************************************************************
 *
 * These routines do all the dirty work required to split and merge nodes.
 */

/*
 * This case occurs when we are trying to insert and have come across a
 * mismatched left or right boundary which could not be adjusted due to
 * being part of a spike.  In order to be able to adjust the boundary
 * we have to prepend or append an empty leaf node.
 */
static
int
btree_edge_internal(hammer_cursor_t cursor, btree_search_edge_t edge)
{
	hammer_node_ondisk_t old_disk;
	hammer_node_ondisk_t new_disk;
	hammer_node_t new_node;
	hammer_btree_elm_t elm;
	int error;
	int n;
	const int esize = sizeof(*elm);

	old_disk = cursor->node->ondisk;
	KKASSERT(old_disk->type == HAMMER_BTREE_TYPE_INTERNAL);
	KKASSERT(old_disk->count < HAMMER_BTREE_INT_ELMS);

	/*
	 * Allocate a new leaf node.
	 */
	new_node = hammer_alloc_btree(cursor->node->cluster, &error);
	if (error)
		return(error);

	hammer_lock_ex(&new_node->lock);
	hammer_modify_node(cursor->node);
	hammer_modify_node(new_node);
	new_disk = new_node->ondisk;
	n = old_disk->count;

	/*
	 * Prepend or append the leaf node and correct the boundary
	 * mismatch.
	 */
	switch(edge) {
	case SEARCH_LEFT_EDGE:
		KKASSERT(cursor->index == 0);
		elm = &old_disk->elms[0];
		bcopy(elm, elm + 1, (n + 1) * esize);
		elm->base = *cursor->left_bound;
		break;
	case SEARCH_RIGHT_EDGE:
		KKASSERT(cursor->index == old_disk->count);
		elm = &old_disk->elms[n];
		elm[1].base = *cursor->right_bound;
		break;
	default:
		panic("btree_edge_internal: bad edge");
		break;
	}
	++old_disk->count;
	elm->base.subtree_type = HAMMER_BTREE_TYPE_LEAF;
	elm->internal.subtree_offset = new_node->node_offset;
	elm->internal.subtree_vol_no = -1;
	elm->internal.subtree_count = 0;

	new_disk->count = 0;
	new_disk->parent = cursor->node->node_offset;
	new_disk->type = HAMMER_BTREE_TYPE_LEAF;

	hammer_unlock(&new_node->lock);
	hammer_rel_node(new_node);

	/*
	 * Cursor->index remains unchanged.  It now points to our new leaf
	 * node and cursor->node's boundaries have been synchronized with
	 * the parent.
	 */
	return(0);
}

/*
 * Split an internal node into two nodes and move the separator at the split
 * point to the parent.  Note that the parent's parent's element pointing
 * to our parent will have an incorrect subtree_count (we don't update it).
 * It will be low, which is ok.
 *
 * (cursor->node, cursor->index) indicates the element the caller intends
 * to push into.  We will adjust node and index if that element winds
 * up in the split node.
 *
 * If we are at the root of a cluster a new root must be created with two
 * elements, one pointing to the original root and one pointing to the
 * newly allocated split node.
 *
 * NOTE! Being at the root of a cluster is different from being at the
 * root of the root cluster.  cursor->parent will not be NULL and
 * cursor->node->ondisk.parent must be tested against 0.  Theoretically
 * we could propogate the algorithm into the parent and deal with multiple
 * 'roots' in the cluster header, but it's easier not to.
 */
static
int
btree_split_internal(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t node;
	hammer_node_t parent;
	hammer_node_t new_node;
	hammer_btree_elm_t elm;
	hammer_btree_elm_t parent_elm;
	int parent_index;
	int made_root;
	int split;
	int error;
	int i;
	const int esize = sizeof(*elm);

	/* 
	 * We are splitting but elms[split] will be promoted to the parent,
	 * leaving the right hand node with one less element.  If the
	 * insertion point will be on the left-hand side adjust the split
	 * point to give the right hand side one additional node.
	 */
	node = cursor->node;
	ondisk = node->ondisk;
	split = (ondisk->count + 1) / 2;
	if (cursor->index <= split)
		--split;
	error = 0;

	/*
	 * If we are at the root of the cluster, create a new root node with
	 * 1 element and split normally.  Avoid making major modifications
	 * until we know the whole operation will work.
	 *
	 * The root of the cluster is different from the root of the root
	 * cluster.  Use the node's on-disk structure's parent offset to
	 * detect the case.
	 */
	if (ondisk->parent == 0) {
		parent = hammer_alloc_btree(node->cluster, &error);
		if (parent == NULL)
			return(error);
		hammer_lock_ex(&parent->lock);
		hammer_modify_node(parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = node->cluster->clu_btree_beg;
		ondisk->elms[0].base.subtree_type = node->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = node->node_offset;
		ondisk->elms[1].base = node->cluster->clu_btree_end;
		/* ondisk->elms[1].base.subtree_Type - not used */
		made_root = 1;
		parent_index = 0;	/* index of current node in parent */
	} else {
		made_root = 0;
		parent = cursor->parent;
		parent_index = cursor->parent_index;
		KKASSERT(parent->cluster == node->cluster);
	}

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
	new_node = hammer_alloc_btree(node->cluster, &error);
	if (new_node == NULL) {
		if (made_root) {
			hammer_unlock(&parent->lock);
			parent->flags |= HAMMER_NODE_DELETED;
			hammer_rel_node(parent);
		}
		return(error);
	}
	hammer_lock_ex(&new_node->lock);

	/*
	 * Create the new node.  P becomes the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 *
	 * elm is the new separator.
	 */
	hammer_modify_node(new_node);
	hammer_modify_node(node);
	ondisk = node->ondisk;
	elm = &ondisk->elms[split];
	bcopy(elm, &new_node->ondisk->elms[0],
	      (ondisk->count - split + 1) * esize);
	new_node->ondisk->count = ondisk->count - split;
	new_node->ondisk->parent = parent->node_offset;
	new_node->ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
	KKASSERT(ondisk->type == new_node->ondisk->type);

	/*
	 * Cleanup the original node.  P becomes the new boundary, its
	 * subtree_offset was moved to the new node.  If we had created
	 * a new root its parent pointer may have changed.
	 */
	elm->internal.subtree_offset = 0;
	elm->internal.rec_offset = 0;
	ondisk->count = split;

	/*
	 * Insert the separator into the parent, fixup the parent's
	 * reference to the original node, and reference the new node.
	 * The separator is P.
	 *
	 * Remember that base.count does not include the right-hand boundary.
	 */
	hammer_modify_node(parent);
	ondisk = parent->ondisk;
	KKASSERT(ondisk->count != HAMMER_BTREE_INT_ELMS);
	ondisk->elms[parent_index].internal.subtree_count = split;
	parent_elm = &ondisk->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (ondisk->count - parent_index) * esize);
	parent_elm->internal.base = elm->base;	/* separator P */
	parent_elm->internal.base.subtree_type = new_node->ondisk->type;
	parent_elm->internal.subtree_offset = new_node->node_offset;
	parent_elm->internal.subtree_count = new_node->ondisk->count;
	parent_elm->internal.subtree_vol_no = 0;
	parent_elm->internal.rec_offset = 0;
	++ondisk->count;

	/*
	 * The children of new_node need their parent pointer set to new_node.
	 */
	for (i = 0; i < new_node->ondisk->count; ++i) {
		elm = &new_node->ondisk->elms[i];
		error = btree_set_parent(new_node, elm);
		if (error) {
			panic("btree_split_internal: btree-fixup problem");
		}
	}

	/*
	 * The cluster's root pointer may have to be updated.
	 */
	if (made_root) {
		hammer_modify_cluster(node->cluster);
		node->cluster->ondisk->clu_btree_root = parent->node_offset;
		node->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
	}


	/*
	 * Ok, now adjust the cursor depending on which element the original
	 * index was pointing at.  If we are >= the split point the push node
	 * is now in the new node.
	 *
	 * NOTE: If we are at the split point itself we cannot stay with the
	 * original node because the push index will point at the right-hand
	 * boundary, which is illegal.
	 *
	 * NOTE: The cursor's parent or parent_index must be adjusted for
	 * the case where a new parent (new root) was created, and the case
	 * where the cursor is now pointing at the split node.
	 */
	if (cursor->index >= split) {
		cursor->parent_index = parent_index + 1;
		cursor->index -= split;
		hammer_unlock(&cursor->node->lock);
		hammer_rel_node(cursor->node);
		cursor->node = new_node;	/* locked and ref'd */
	} else {
		cursor->parent_index = parent_index;
		hammer_unlock(&new_node->lock);
		hammer_rel_node(new_node);
	}

	/*
	 * Fixup left and right bounds
	 */
	parent_elm = &parent->ondisk->elms[cursor->parent_index];
	cursor->left_bound = &parent_elm[0].internal.base;
	cursor->right_bound = &parent_elm[1].internal.base;
	KKASSERT(hammer_btree_cmp(cursor->left_bound,
		 &cursor->node->ondisk->elms[0].internal.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &cursor->node->ondisk->elms[cursor->node->ondisk->count].internal.base) >= 0);

	return (0);
}

/*
 * Same as the above, but splits a full leaf node.
 */
static
int
btree_split_leaf(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t parent;
	hammer_node_t leaf;
	hammer_node_t new_leaf;
	hammer_btree_elm_t elm;
	hammer_btree_elm_t parent_elm;
	hammer_base_elm_t mid_boundary;
	int parent_index;
	int made_root;
	int split;
	int error;
	const size_t esize = sizeof(*elm);

	/* 
	 * Calculate the split point.  If the insertion point will be on
	 * the left-hand side adjust the split point to give the right
	 * hand side one additional node.
	 */
	leaf = cursor->node;
	ondisk = leaf->ondisk;
	split = (ondisk->count + 1) / 2;
	if (cursor->index <= split)
		--split;
	error = 0;

	/*
	 * If we are at the root of the tree, create a new root node with
	 * 1 element and split normally.  Avoid making major modifications
	 * until we know the whole operation will work.
	 */
	if (ondisk->parent == 0) {
		parent = hammer_alloc_btree(leaf->cluster, &error);
		if (parent == NULL)
			return(error);
		hammer_lock_ex(&parent->lock);
		hammer_modify_node(parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = leaf->cluster->clu_btree_beg;
		ondisk->elms[0].base.subtree_type = leaf->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = leaf->node_offset;
		ondisk->elms[1].base = leaf->cluster->clu_btree_end;
		/* ondisk->elms[1].base.subtree_type = not used */
		made_root = 1;
		parent_index = 0;	/* insertion point in parent */
	} else {
		made_root = 0;
		parent = cursor->parent;
		parent_index = cursor->parent_index;
		KKASSERT(parent->cluster == leaf->cluster);
	}

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
	new_leaf = hammer_alloc_btree(leaf->cluster, &error);
	if (new_leaf == NULL) {
		if (made_root) {
			hammer_unlock(&parent->lock);
			parent->flags |= HAMMER_NODE_DELETED;
			hammer_rel_node(parent);
		}
		return(error);
	}
	hammer_lock_ex(&new_leaf->lock);

	/*
	 * Create the new node.  P become the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 */
	hammer_modify_node(leaf);
	hammer_modify_node(new_leaf);
	ondisk = leaf->ondisk;
	elm = &ondisk->elms[split];
	bcopy(elm, &new_leaf->ondisk->elms[0], (ondisk->count - split) * esize);
	new_leaf->ondisk->count = ondisk->count - split;
	new_leaf->ondisk->parent = parent->node_offset;
	new_leaf->ondisk->type = HAMMER_BTREE_TYPE_LEAF;
	KKASSERT(ondisk->type == new_leaf->ondisk->type);

	/*
	 * Cleanup the original node.  Because this is a leaf node and
	 * leaf nodes do not have a right-hand boundary, there
	 * aren't any special edge cases to clean up.  We just fixup the
	 * count.
	 */
	ondisk->count = split;

	/*
	 * Insert the separator into the parent, fixup the parent's
	 * reference to the original node, and reference the new node.
	 * The separator is P.
	 *
	 * Remember that base.count does not include the right-hand boundary.
	 * We are copying parent_index+1 to parent_index+2, not +0 to +1.
	 */
	hammer_modify_node(parent);
	ondisk = parent->ondisk;
	KKASSERT(ondisk->count != HAMMER_BTREE_INT_ELMS);
	ondisk->elms[parent_index].internal.subtree_count = split;
	parent_elm = &ondisk->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (ondisk->count - parent_index) * esize);
	hammer_make_separator(&elm[-1].base, &elm[0].base, &parent_elm->base);
	parent_elm->internal.base.subtree_type = new_leaf->ondisk->type;
	parent_elm->internal.subtree_offset = new_leaf->node_offset;
	parent_elm->internal.subtree_count = new_leaf->ondisk->count;
	parent_elm->internal.subtree_vol_no = 0;
	parent_elm->internal.rec_offset = 0;
	mid_boundary = &parent_elm->base;
	++ondisk->count;

	/*
	 * The cluster's root pointer may have to be updated.
	 */
	if (made_root) {
		hammer_modify_cluster(leaf->cluster);
		leaf->cluster->ondisk->clu_btree_root = parent->node_offset;
		leaf->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
	}

	/*
	 * Ok, now adjust the cursor depending on which element the original
	 * index was pointing at.  If we are >= the split point the push node
	 * is now in the new node.
	 *
	 * NOTE: If we are at the split point itself we need to select the
	 * old or new node based on where key_beg's insertion point will be.
	 * If we pick the wrong side the inserted element will wind up in
	 * the wrong leaf node and outside that node's bounds.
	 */
	if (cursor->index > split ||
	    (cursor->index == split &&
	     hammer_btree_cmp(&cursor->key_beg, mid_boundary) >= 0)) {
		cursor->parent_index = parent_index + 1;
		cursor->index -= split;
		hammer_unlock(&cursor->node->lock);
		hammer_rel_node(cursor->node);
		cursor->node = new_leaf;
	} else {
		cursor->parent_index = parent_index;
		hammer_unlock(&new_leaf->lock);
		hammer_rel_node(new_leaf);
	}

	/*
	 * Fixup left and right bounds
	 */
	parent_elm = &parent->ondisk->elms[cursor->parent_index];
	cursor->left_bound = &parent_elm[0].internal.base;
	cursor->right_bound = &parent_elm[1].internal.base;
	KKASSERT(hammer_btree_cmp(cursor->left_bound,
		 &cursor->node->ondisk->elms[0].leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &cursor->node->ondisk->elms[cursor->node->ondisk->count-1].leaf.base) > 0);

	return (0);
}

/*
 * Attempt to remove the empty B-Tree node at (cursor->node).  Returns 0
 * on success, EAGAIN if we could not acquire the necessary locks, or some
 * other error.
 *
 * On return the cursor may end up pointing at an internal node, suitable
 * for further iteration but not for an immediate insertion or deletion.
 *
 * cursor->node may be an internal node or a leaf node.
 *
 * NOTE: If cursor->node has one element it is the parent trying to delete
 * that element, make sure cursor->index is properly adjusted on success.
 */
int
btree_remove(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_btree_elm_t elm;
	hammer_node_t save;
	hammer_node_t node;
	hammer_node_t parent;
	int error;
	int i;

	/*
	 * If we are at the root of the root cluster there is nothing to
	 * remove, but an internal node at the root of a cluster is not
	 * allowed to be empty so convert it to a leaf node.
	 */
	if (cursor->parent == NULL) {
		hammer_modify_node(cursor->node);
		ondisk = cursor->node->ondisk;
		KKASSERT(ondisk->parent == 0);
		ondisk->type = HAMMER_BTREE_TYPE_LEAF;
		ondisk->count = 0;
		cursor->index = 0;
		kprintf("EMPTY ROOT OF ROOT CLUSTER -> LEAF\n");
		return(0);
	}

	/*
	 * Retain a reference to cursor->node, ex-lock again (2 locks now)
	 * so we do not lose the lock when we cursor around.
	 */
	save = cursor->node;
	hammer_ref_node(save);
	hammer_lock_ex(&save->lock);

	/*
	 * We need to be able to lock the parent of the parent.  Do this
	 * non-blocking and return EAGAIN if the lock cannot be acquired.
	 * non-blocking is required in order to avoid a deadlock.
	 *
	 * After we cursor up, parent is moved to node and the new parent
	 * is the parent of the parent.
	 */
	error = hammer_cursor_up(cursor, 1);
	if (error) {
		kprintf("BTREE_REMOVE: Cannot lock parent, skipping\n");
		goto failure;
	}

	/*
	 * At this point we want to remove the element at (node, index),
	 * which is now the (original) parent pointing to the saved node.
	 * Removing the element allows us to then free the node it was
	 * pointing to.
	 *
	 * However, an internal node is not allowed to have 0 elements, so
	 * if the count would drop to 0 we have to recurse.  It is possible
	 * for the recursion to fail.
	 *
	 * NOTE: The cursor is in an indeterminant position after recursing,
	 * but will still be suitable for an iteration.
	 */
	node = cursor->node;
	KKASSERT(node->ondisk->count > 0);
	if (node->ondisk->count == 1) {
		error = btree_remove(cursor);
		if (error == 0) {
			/*kprintf("BTREE_REMOVE: Successful!\n");*/
			goto success;
		} else {
			kprintf("BTREE_REMOVE: Recursion failed %d\n", error);
			goto failure;
		}
	}

	/*
	 * Remove the element at (node, index) and adjust the parent's
	 * subtree_count.
	 *
	 * NOTE! If removing element 0 an internal node's left-hand boundary
	 * will no longer match its parent.  If removing a mid-element the
	 * boundary will no longer match a child's left hand or right hand
	 * boundary.
	 *
	 *	BxBxBxB		remove a (x[0]): internal node's left-hand
	 *       | | |		                 boundary no longer matches
	 *       a b c				 parent.
	 *
	 *			remove b (x[1]): a's right hand boundary no
	 *					 longer matches parent.
	 *
	 *			remove c (x[2]): b's right hand boundary no
	 *					 longer matches parent.
	 *
	 * These cases are corrected in btree_search().
	 */
#if 0
	kprintf("BTREE_REMOVE: Removing element %d\n", cursor->index);
#endif
	KKASSERT(node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL);
	KKASSERT(cursor->index < node->ondisk->count);
	hammer_modify_node(node);
	ondisk = node->ondisk;
	i = cursor->index;

	/*
	 * WARNING: For historical lookups to work properly we cannot
	 * recalculate the mid-point or we might blow up historical searches
	 * which depend on the mid-point matching the first right-hand element
	 * XXX
	 */
	bcopy(&ondisk->elms[i+1], &ondisk->elms[i],
	      (ondisk->count - i) * sizeof(ondisk->elms[0]));
	--ondisk->count;

	/*
	 * Adjust the parent-parent's (now parent) reference to the parent
	 * (now node).
	 */
	if ((parent = cursor->parent) != NULL) {
		elm = &parent->ondisk->elms[cursor->parent_index];
		if (elm->internal.subtree_count != ondisk->count) {
			hammer_modify_node(parent);
			elm->internal.subtree_count = ondisk->count;
		}
		if (elm->base.subtree_type != HAMMER_BTREE_TYPE_CLUSTER &&
		    elm->base.subtree_type != ondisk->type) {
			hammer_modify_node(parent);
			elm->base.subtree_type = ondisk->type;
		}
	}

success:
	/*
	 * Free the saved node.  If the saved node was the root of a
	 * cluster, free the entire cluster.
	 */
	hammer_flush_node(save);
	save->flags |= HAMMER_NODE_DELETED;

	error = 0;
failure:
	hammer_unlock(&save->lock);
	hammer_rel_node(save);
	return(error);
}

/*
 * The child represented by the element in internal node node needs
 * to have its parent pointer adjusted.
 */
static
int
btree_set_parent(hammer_node_t node, hammer_btree_elm_t elm)
{
	hammer_volume_t volume;
	hammer_cluster_t cluster;
	hammer_node_t child;
	int error;

	error = 0;

	switch(elm->internal.base.subtree_type) {
	case HAMMER_BTREE_TYPE_LEAF:
	case HAMMER_BTREE_TYPE_INTERNAL:
		child = hammer_get_node(node->cluster,
					elm->internal.subtree_offset, &error);
		if (error == 0) {
			hammer_modify_node(child);
			hammer_lock_ex(&child->lock);
			child->ondisk->parent = node->node_offset;
			hammer_unlock(&child->lock);
			hammer_rel_node(child);
		}
		break;
	case HAMMER_BTREE_TYPE_CLUSTER:
		volume = hammer_get_volume(node->cluster->volume->hmp,
					elm->internal.subtree_vol_no, &error);
		if (error)
			break;
		cluster = hammer_get_cluster(volume,
					elm->internal.subtree_clu_no,
					&error, 0);
                hammer_rel_volume(volume, 0);
		if (error)
			break;
		hammer_modify_cluster(cluster);
		hammer_lock_ex(&cluster->io.lock);
		cluster->ondisk->clu_btree_parent_offset = node->node_offset;
		hammer_unlock(&cluster->io.lock);
		KKASSERT(cluster->ondisk->clu_btree_parent_clu_no ==
			 node->cluster->clu_no);
		KKASSERT(cluster->ondisk->clu_btree_parent_vol_no ==
			 node->cluster->volume->vol_no);
		hammer_rel_cluster(cluster, 0);
		break;
	default:
		hammer_print_btree_elm(elm, HAMMER_BTREE_TYPE_INTERNAL, -1);
		panic("btree_set_parent: bad subtree_type");
		break; /* NOT REACHED */
	}
	return(error);
}

/************************************************************************
 *			   MISCELLANIOUS SUPPORT 			*
 ************************************************************************/

/*
 * Compare two B-Tree elements, return -N, 0, or +N (e.g. similar to strcmp).
 *
 * Note that for this particular function a return value of -1, 0, or +1
 * can denote a match if create_tid is otherwise discounted.
 *
 * See also hammer_rec_rb_compare() and hammer_rec_cmp() in hammer_object.c.
 */
int
hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2)
{
	if (key1->obj_id < key2->obj_id)
		return(-4);
	if (key1->obj_id > key2->obj_id)
		return(4);

	if (key1->rec_type < key2->rec_type)
		return(-3);
	if (key1->rec_type > key2->rec_type)
		return(3);

	if (key1->key < key2->key)
		return(-2);
	if (key1->key > key2->key)
		return(2);

	if (key1->create_tid < key2->create_tid)
		return(-1);
	if (key1->create_tid > key2->create_tid)
		return(1);
	return(0);
}

/*
 * Test a non-zero timestamp against an element to determine whether the
 * element is visible.
 */
int
hammer_btree_chkts(hammer_tid_t create_tid, hammer_base_elm_t base)
{
	if (create_tid < base->create_tid)
		return(-1);
	if (base->delete_tid && create_tid >= base->delete_tid)
		return(1);
	return(0);
}

/*
 * Create a separator half way inbetween key1 and key2.  For fields just
 * one unit apart, the separator will match key2.
 *
 * At the moment require that the separator never match key2 exactly.
 *
 * We have to special case the separator between two historical keys,
 * where all elements except create_tid match.  In this case our B-Tree
 * searches can't figure out which branch of an internal node to go down
 * unless the mid point's create_tid is exactly key2.
 * (see btree_search()'s scan code on HAMMER_BTREE_TYPE_INTERNAL).
 */
#define MAKE_SEPARATOR(key1, key2, dest, field)	\
	dest->field = key1->field + ((key2->field - key1->field + 1) >> 1);

static void
hammer_make_separator(hammer_base_elm_t key1, hammer_base_elm_t key2,
		      hammer_base_elm_t dest)
{
	bzero(dest, sizeof(*dest));
	MAKE_SEPARATOR(key1, key2, dest, obj_id);
	MAKE_SEPARATOR(key1, key2, dest, rec_type);
	MAKE_SEPARATOR(key1, key2, dest, key);
	if (key1->obj_id == key2->obj_id &&
	    key1->rec_type == key2->rec_type &&
	    key1->key == key2->key) {
		dest->create_tid = key2->create_tid;
	} else {
		dest->create_tid = 0;
	}
}

#undef MAKE_SEPARATOR

#if 0
/*
 * Return whether a generic internal or leaf node is full
 */
static int
btree_node_is_full(hammer_node_ondisk_t node)
{
	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		if (node->count == HAMMER_BTREE_INT_ELMS)
			return(1);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		if (node->count == HAMMER_BTREE_LEAF_ELMS)
			return(1);
		break;
	default:
		panic("illegal btree subtype");
	}
	return(0);
}
#endif

/*
 * Return whether a generic internal or leaf node is almost full.  This
 * routine is used as a helper for search insertions to guarentee at 
 * least 2 available slots in the internal node(s) leading up to a leaf,
 * so hammer_btree_insert_cluster() will function properly.
 */
static int
btree_node_is_almost_full(hammer_node_ondisk_t node)
{
	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		if (node->count > HAMMER_BTREE_INT_ELMS - 2)
			return(1);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		if (node->count > HAMMER_BTREE_LEAF_ELMS - 2)
			return(1);
		break;
	default:
		panic("illegal btree subtype");
	}
	return(0);
}

#if 0
static int
btree_max_elements(u_int8_t type)
{
	if (type == HAMMER_BTREE_TYPE_LEAF)
		return(HAMMER_BTREE_LEAF_ELMS);
	if (type == HAMMER_BTREE_TYPE_INTERNAL)
		return(HAMMER_BTREE_INT_ELMS);
	panic("btree_max_elements: bad type %d\n", type);
}
#endif

void
hammer_print_btree_node(hammer_node_ondisk_t ondisk)
{
	hammer_btree_elm_t elm;
	int i;

	kprintf("node %p count=%d parent=%d type=%c\n",
		ondisk, ondisk->count, ondisk->parent, ondisk->type);

	/*
	 * Dump both boundary elements if an internal node
	 */
	if (ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
		for (i = 0; i <= ondisk->count; ++i) {
			elm = &ondisk->elms[i];
			hammer_print_btree_elm(elm, ondisk->type, i);
		}
	} else {
		for (i = 0; i < ondisk->count; ++i) {
			elm = &ondisk->elms[i];
			hammer_print_btree_elm(elm, ondisk->type, i);
		}
	}
}

void
hammer_print_btree_elm(hammer_btree_elm_t elm, u_int8_t type, int i)
{
	kprintf("  %2d", i);
	kprintf("\tobjid        = %016llx\n", elm->base.obj_id);
	kprintf("\tkey          = %016llx\n", elm->base.key);
	kprintf("\tcreate_tid   = %016llx\n", elm->base.create_tid);
	kprintf("\tdelete_tid   = %016llx\n", elm->base.delete_tid);
	kprintf("\trec_type     = %04x\n", elm->base.rec_type);
	kprintf("\tobj_type     = %02x\n", elm->base.obj_type);
	kprintf("\tsubtree_type = %02x\n", elm->base.subtree_type);

	if (type == HAMMER_BTREE_TYPE_INTERNAL) {
		if (elm->internal.rec_offset) {
			kprintf("\tcluster_rec  = %08x\n",
				elm->internal.rec_offset);
			kprintf("\tcluster_id   = %08x\n",
				elm->internal.subtree_clu_no);
			kprintf("\tvolno        = %08x\n",
				elm->internal.subtree_vol_no);
		} else {
			kprintf("\tsubtree_off  = %08x\n",
				elm->internal.subtree_offset);
		}
		kprintf("\tsubtree_count= %d\n", elm->internal.subtree_count);
	} else {
		kprintf("\trec_offset   = %08x\n", elm->leaf.rec_offset);
		kprintf("\tdata_offset  = %08x\n", elm->leaf.data_offset);
		kprintf("\tdata_len     = %08x\n", elm->leaf.data_len);
		kprintf("\tdata_crc     = %08x\n", elm->leaf.data_crc);
	}
}
