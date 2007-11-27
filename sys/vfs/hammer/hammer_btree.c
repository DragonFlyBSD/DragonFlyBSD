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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.c,v 1.8 2007/11/27 07:48:52 dillon Exp $
 */

/*
 * HAMMER B-Tree index
 *
 * HAMMER implements a modified B+Tree.  In documentation this will
 * simply be refered to as the HAMMER B-Tree.  Basically a B-Tree
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

static int btree_search(hammer_cursor_t cursor, int flags);
static int btree_split_internal(hammer_cursor_t cursor);
static int btree_split_leaf(hammer_cursor_t cursor);
static int btree_remove(hammer_node_t node, int index);
static int btree_set_parent(hammer_node_t node, hammer_btree_elm_t elm);
#if 0
static int btree_rebalance(hammer_cursor_t cursor);
static int btree_collapse(hammer_cursor_t cursor);
#endif
static int btree_node_is_full(hammer_node_ondisk_t node);
static void hammer_make_separator(hammer_base_elm_t key1,
			hammer_base_elm_t key2, hammer_base_elm_t dest);

/*
 * Iterate records after a search.  The cursor is iterated forwards past
 * the current record until a record matching the key-range requirements
 * is found.  ENOENT is returned if the iteration goes past the ending
 * key.
 *
 * key_beg/key_end is an INCLUSVE range.  i.e. if you are scanning to load
 * a 4096 byte buffer key_beg might specify an offset of 0 and key_end an
 * offset of 4095.
 *
 * cursor->key_beg may or may not be modified by this function during
 * the iteration.
 */
int
hammer_btree_iterate(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int error;
	int r;
#if 0
	int s;
	int64_t save_key;
#endif

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
		 */
		if (cursor->index == node->count) {
			error = hammer_cursor_up(cursor);
			if (error)
				break;
			node = cursor->node->ondisk;
			KKASSERT(cursor->index != node->count);
			++cursor->index;
			continue;
		}

		/*
		 * Iterate down the tree while we are at an internal node.
		 * Nodes cannot be empty, assert the case because if one is
		 * we will wind up in an infinite loop.
		 *
		 * We can avoid iterating through large swaths of transaction
		 * id space if the left and right separators are the same
		 * except for their transaction spaces.  We can then skip
		 * the node if the left and right transaction spaces are the
		 * same sign.  This directly optimized accesses to files with
		 * HUGE transactional histories, such as database files,
		 * allowing us to avoid having to iterate through the entire
		 * history.
		 */
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			KKASSERT(node->count != 0);
			elm = &node->elms[cursor->index];
#if 0
			/*
			 * temporarily disable this optimization, it needs
			 * more of a theoretical review.
			 */
			if (elm[0].base.obj_id == elm[1].base.obj_id &&
			    elm[0].base.rec_type == elm[1].base.rec_type &&
			    elm[0].base.key == elm[1].base.key) {
				/*
				 * Left side transaction space
				 */
				save_key = cursor->key_beg.key;
				cursor->key_beg.key = elm[0].base.key;
				r = hammer_btree_cmp(&cursor->key_beg,
						     &elm[0].base);
				cursor->key_beg.key = save_key;

				/*
				 * Right side transaction space
				 */
				save_key = cursor->key_end.key;
				cursor->key_end.key = elm[1].base.key;
				s = hammer_btree_cmp(&cursor->key_end,
						     &elm[1].base);
				cursor->key_end.key = save_key;

				/*
				 * If our range is entirely on one side or
				 * the other side we can skip the sub-tree.
				 */
				if ((r < 0 && s < 0) || (r > 0 && s > 0)) {
					++cursor->index;
					continue;
				}
			}
#endif
			error = hammer_cursor_down(cursor);
			if (error)
				break;
			KKASSERT(cursor->index == 0);
			node = cursor->node->ondisk;
			continue;
		}

		/*
		 * We are at a leaf.
		 *
		 * Determine if the record at the cursor has gone beyond the
		 * end of our range.  Remember that our key range is inclusive.
		 *
		 * When iterating we may have to 'pick out' records matching
		 * our transaction requirements.  A comparison return of
		 * +1 or -1 indicates a transactional record that is too
		 * old or too new but does not terminate the search.
		 */
		elm = &node->elms[cursor->index];
		r = hammer_btree_range_cmp(cursor, &elm->base);
		if (r == -1 || r == 1) {
			++cursor->index;
			continue;
		}

		/*
		 * We either found a match or are now out of bounds.
		 */
		error = r ? ENOENT : 0;
		break;
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
 * Extract the record and/or data associated with the cursor's current
 * position.  Any prior record or data stored in the cursor is replaced.
 * The cursor must be positioned at a leaf node.
 *
 * NOTE: Only records can be extracted from internal B-Tree nodes, and
 *       only for inter-cluster references.  At the moment we only support
 *	 extractions from leaf nodes.
 */
int
hammer_btree_extract(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	hammer_cluster_t cluster;
	u_int64_t buf_type;
	int32_t cloff;
	int error;

	/*
	 * A cluster record type has no data reference, the information
	 * is stored directly in the record and B-Tree element.
	 *
	 * The case where the data reference resolves to the same buffer
	 * as the record reference must be handled.
	 */
	node = cursor->node->ondisk;
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	elm = &node->elms[cursor->index];
	cluster = cursor->node->cluster;
	error = 0;

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
			 * Just assume the buffer type is correct.
			 */
			cursor->data = (void *)
				((char *)cursor->record_buffer->ondisk +
				 (elm->leaf.data_offset & HAMMER_BUFMASK));
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

#if 0
	/* HANDLED BY CALLER */
	/*
	 * Issue a search to get our cursor at the right place.  The search
	 * will get us to a leaf node.
	 *
	 * The search also does some setup for our insert, so there is always
	 * room in the leaf.
	 */
	error = btree_search(cursor, HAMMER_CURSOR_INSERT);
	if (error != ENOENT) {
		if (error == 0)
			error = EEXIST;
		return (error);
	}
#endif

	/*
	 * Insert the element at the leaf node and update the count in the
	 * parent.  It is possible for parent to be NULL, indicating that
	 * the root of the B-Tree in the cluster is a leaf.  It is also
	 * possible for the leaf to be empty.
	 *
	 * Remember that the right-hand boundary is not included in the
	 * count.
	 */
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
	hammer_modify_node(cursor->node);

	/*
	 * Adjust the sub-tree count in the parent.  note that the parent
	 * may be in a different cluster.
	 */
	if (cursor->parent) {
		parent = cursor->parent->ondisk;
		i = cursor->parent_index;
		++parent->elms[i].internal.subtree_count;
		KKASSERT(parent->elms[i].internal.subtree_count <= node->count);
		hammer_modify_node(cursor->parent);
	}
	return(0);
}

/*
 * Delete a record from the B-Tree's at the current cursor position.
 * The cursor is positioned such that the current element is the one
 * to be deleted.
 *
 * The caller must call hammer_btree_lookup() with the HAMMER_CURSOR_DELETE
 * flag set and that call must return 0 before this function can be
 * called.
 *
 * It is possible that we will be asked to delete the last element in a
 * leaf.  This case only occurs if the downward search was unable to
 * rebalance us, which in turn can occur if our parent has inter-cluster
 * elements.  So the 0-element case for a leaf is allowed.
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

#if 0
	/* HANDLED BY CALLER */
	/*
	 * Locate the leaf element to delete.  The search is also responsible
	 * for doing some of the rebalancing work on its way down.
	 */
	error = btree_search(cursor, HAMMER_CURSOR_DELETE);
	if (error)
		return (error);
#endif

	/*
	 * Delete the element from the leaf node. 
	 *
	 * Remember that leaf nodes do not have boundaries.
	 */
	node = cursor->node;
	ondisk = node->ondisk;
	i = cursor->index;

	KKASSERT(ondisk->type == HAMMER_BTREE_TYPE_LEAF);
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
		elm = &parent->ondisk->elms[cursor->parent_index];
		if (elm->internal.subtree_count)
			--elm->internal.subtree_count;
		KKASSERT(elm->internal.subtree_count <= ondisk->count);
		hammer_modify_node(parent);
	}

	/*
	 * If the leaf is empty try to remove the subtree reference
	 * in at (parent, parent_index).  This will unbalance the
	 * tree.
	 *
	 * Note that internal nodes must have at least one element
	 * so their boundary information is properly laid out.  If
	 * we would cause our parent to become empty we try to
	 * recurse up the tree, but if that doesn't work we just
	 * leave the tree with an empty leaf.
	 */
	if (ondisk->count == 0) {
		error = btree_remove(cursor->parent, cursor->parent_index);
		if (error == 0) {
			hammer_free_btree(node->cluster, node->node_offset);
		} else if (error == EAGAIN) {
			hammer_modify_node(node);
			error = 0;
		} /* else a real error occured XXX */
	} else {
		hammer_modify_node(node);
		error = 0;
	}
	return(error);
}

/*
 * PRIMAY B-TREE SEARCH SUPPORT PROCEDURE
 *
 * Search a cluster's B-Tree for cursor->key_beg, return the matching node.
 *
 * The search begins at the current node and will instantiate a NULL
 * parent if necessary and if not at the root of the cluster.  On return
 * parent will be non-NULL unless the cursor is sitting at a root-leaf.
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
btree_search(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_cluster_t cluster;
	int error;
	int i;
	int r;

	flags |= cursor->flags;

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
	 */
	cluster = cursor->node->cluster;
	while (
	    hammer_btree_cmp(&cursor->key_beg, &cluster->clu_btree_beg) < 0 ||
	    hammer_btree_cmp(&cursor->key_beg, &cluster->clu_btree_end) >= 0) {
		error = hammer_cursor_toroot(cursor);
		if (error)
			goto done;
		error = hammer_cursor_up(cursor);
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
		error = hammer_cursor_up(cursor);
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
	 * XXX as an optimization it should be possible to unbalance the tree
	 * and stop at the root of the current cluster.
	 */
	while (flags & HAMMER_CURSOR_INSERT) {
		if (btree_node_is_full(cursor->node->ondisk) == 0)
			break;
		if (cursor->parent == NULL)
			break;
		if (cursor->parent->ondisk->count != HAMMER_BTREE_INT_ELMS)
			break;
		error = hammer_cursor_up(cursor);
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
		error = hammer_cursor_up(cursor);
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
			if (error)
				goto done;
		}
		node = cursor->node->ondisk;
#endif

		/*
		 * Scan the node to find the subtree index to push down into.
		 * We go one-past, then back-up.  The key should never be
		 * less then the left-hand boundary so I should never wind
		 * up 0.
		 */
		for (i = 0; i < node->count; ++i) {
			r = hammer_btree_cmp(&cursor->key_beg,
					     &node->elms[i].base);
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
		if (flags & HAMMER_CURSOR_INSERT) {
			if (node->count == HAMMER_BTREE_INT_ELMS) {
				error = btree_split_internal(cursor);
				if (error)
					goto done;
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
		 * Push down (push into new node, existing node becomes
		 * the parent).
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
	 * (XXX do a binary search).  On success the index is set to the
	 * matching element, on failure the index is set to the insertion
	 * point.
	 *
	 * Boundaries are not stored in leaf nodes, so the index can wind
	 * up to the left of element 0 (index == 0) or past the end of
	 * the array (index == node->count).
	 */
	KKASSERT(node->count <= HAMMER_BTREE_LEAF_ELMS);

	for (i = 0; i < node->count; ++i) {
		r = hammer_btree_cmp(&cursor->key_beg, &node->elms[i].base);

		/*
		 * Stop if we've flipped past key_beg
		 */
		if (r < 0)
			break;

		/*
		 * Return an exact match
		 */
		if (r == 0) {
			cursor->index = i;
			error = 0;
			goto done;
		}
	}

	/*
	 * No exact match was found, i is now at the insertion point.
	 *
	 * If inserting split a full leaf before returning.  This
	 * may have the side effect of adjusting cursor->node and
	 * cursor->index.
	 */
	cursor->index = i;
	if ((flags & HAMMER_CURSOR_INSERT) &&
	    node->count == HAMMER_BTREE_LEAF_ELMS) {
		error = btree_split_leaf(cursor);
		/* NOT USED
		i = cursor->index;
		node = &cursor->node->internal;
		*/
		if (error)
			goto done;
	}
	error = ENOENT;
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
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = node->cluster->clu_btree_beg;
		ondisk->elms[0].internal.subtree_type = node->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = node->node_offset;
		ondisk->elms[1].base = node->cluster->clu_btree_end;
		made_root = 1;
		parent_index = 0;	/* index of current node in parent */
	} else {
		made_root = 0;
		parent = cursor->parent;
		parent_index = cursor->parent_index;
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
			hammer_free_btree(node->cluster, parent->node_offset);
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
	ondisk->count = split;

	/*
	 * Insert the separator into the parent, fixup the parent's
	 * reference to the original node, and reference the new node.
	 * The separator is P.
	 *
	 * Remember that base.count does not include the right-hand boundary.
	 */
	ondisk = parent->ondisk;
	ondisk->elms[parent_index].internal.subtree_count = split;
	parent_elm = &ondisk->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (ondisk->count - parent_index) * esize);
	parent_elm->internal.base = elm->base;	/* separator P */
	parent_elm->internal.subtree_offset = new_node->node_offset;
	parent_elm->internal.subtree_count = new_node->ondisk->count;
	parent_elm->internal.subtree_type = new_node->ondisk->type;
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
		node->cluster->ondisk->clu_btree_root = parent->node_offset;
		hammer_modify_cluster(node->cluster);
		node->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
	}

	hammer_modify_node(node);
	hammer_modify_node(new_node);
	hammer_modify_node(parent);

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
	cursor->left_bound = &elm[0].internal.base;
	cursor->right_bound = &elm[1].internal.base;

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
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = leaf->cluster->clu_btree_beg;
		ondisk->elms[0].internal.subtree_type = leaf->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = leaf->node_offset;
		ondisk->elms[1].base = leaf->cluster->clu_btree_end;
		made_root = 1;
		parent_index = 0;	/* insertion point in parent */
	} else {
		made_root = 0;
		parent = cursor->parent;
		parent_index = cursor->parent_index;
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
			hammer_free_btree(leaf->cluster, parent->node_offset);
			hammer_rel_node(parent);
		}
		return(error);
	}
	hammer_lock_ex(&new_leaf->lock);

	/*
	 * Create the new node.  P become the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 */
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
	ondisk = parent->ondisk;
	ondisk->elms[parent_index].internal.subtree_count = split;
	parent_elm = &ondisk->elms[parent_index+1];
	if (parent_index + 1 != ondisk->count) {
		bcopy(parent_elm, parent_elm + 1,
		      (ondisk->count - parent_index - 1) * esize);
	}
	hammer_make_separator(&elm[-1].base, &elm[0].base, &parent_elm->base);
	parent_elm->internal.subtree_offset = new_leaf->node_offset;
	parent_elm->internal.subtree_count = new_leaf->ondisk->count;
	parent_elm->internal.subtree_type = new_leaf->ondisk->type;
	++ondisk->count;

	/*
	 * The cluster's root pointer may have to be updated.
	 */
	if (made_root) {
		leaf->cluster->ondisk->clu_btree_root = parent->node_offset;
		hammer_modify_cluster(leaf->cluster);
		leaf->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
	}

	hammer_modify_node(leaf);
	hammer_modify_node(new_leaf);
	hammer_modify_node(parent);

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
	cursor->left_bound = &elm[0].internal.base;
	cursor->right_bound = &elm[1].internal.base;

	return (0);
}

/*
 * Remove the element at (node, index).  If the internal node would become
 * empty passively recurse up the tree.
 *
 * A locked internal node is passed to this function, the node remains
 * locked on return.  Leaf nodes cannot be passed to this function.
 *
 * Returns EAGAIN if we were unable to acquire the needed locks.  The caller
 * does not deal with the empty leaf until determines whether this recursion
 * has succeeded or not.
 */
int
btree_remove(hammer_node_t node, int index)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t parent;
	int error;

	ondisk = node->ondisk;
	KKASSERT(ondisk->count > 0);

	/*
	 * Remove the element, shifting remaining elements left one.
	 * Note that our move must include the right-boundary element.
	 */
	if (ondisk->count != 1) {
		bcopy(&ondisk->elms[index+1], &ondisk->elms[index],
		      (ondisk->count - index) * sizeof(ondisk->elms[0]));
		--ondisk->count;
		hammer_modify_node(node);
		return(0);
	}

	/*
	 * Internal nodes cannot drop to 0 elements, so remove the node
	 * from ITS parent.  If the node is the root node, convert it to
	 * an empty leaf node (which can drop to 0 elements).
	 */
	if (ondisk->parent == 0) {
		ondisk->count = 0;
		ondisk->type = HAMMER_BTREE_TYPE_LEAF;
		hammer_modify_node(node);
		return(0);
	}

	/*
	 * Try to remove the node from its parent.  Return EAGAIN if we
	 * cannot.
	 */
	parent = hammer_get_node(node->cluster, ondisk->parent, &error);
	if (hammer_lock_ex_try(&parent->lock)) {
		hammer_rel_node(parent);
		return(EAGAIN);
	}
	ondisk = parent->ondisk;
	for (index = 0; index < ondisk->count; ++index) {
		if (ondisk->elms[index].internal.subtree_offset ==
		    node->node_offset) {
			break;
		}
	}
	if (index == ondisk->count) {
		kprintf("btree_remove: lost parent linkage to node\n");
		error = EIO;
	} else {
		error = btree_remove(parent, index);
		if (error == 0) {
			hammer_free_btree(node->cluster, node->node_offset);
			/* NOTE: node can be reallocated at any time now */
		}
	}
	hammer_unlock(&parent->lock);
	hammer_rel_node(parent);
	return (error);
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

	switch(elm->internal.subtree_type) {
	case HAMMER_BTREE_TYPE_LEAF:
	case HAMMER_BTREE_TYPE_INTERNAL:
		child = hammer_get_node(node->cluster,
					elm->internal.subtree_offset, &error);
		if (error == 0) {
			hammer_lock_ex(&child->lock);
			child->ondisk->parent = node->node_offset;
			hammer_modify_node(child);
			hammer_unlock(&child->lock);
			hammer_rel_node(child);
		}
		break;
	case HAMMER_BTREE_TYPE_CLUSTER:
		volume = hammer_get_volume(node->cluster->volume->hmp,
					elm->internal.subtree_volno, &error);
		if (error)
			break;
		cluster = hammer_get_cluster(volume,
					elm->internal.subtree_cluid,
					&error, 0);
                hammer_rel_volume(volume, 0);
		if (error)
			break;
		hammer_lock_ex(&cluster->io.lock);
		cluster->ondisk->clu_btree_parent_offset = node->node_offset;
		hammer_unlock(&cluster->io.lock);
		KKASSERT(cluster->ondisk->clu_btree_parent_clu_no ==
			 node->cluster->clu_no);
		KKASSERT(cluster->ondisk->clu_btree_parent_vol_no ==
			 node->cluster->volume->vol_no);
		hammer_modify_cluster(cluster);
		hammer_rel_cluster(cluster, 0);
		break;
	default:
		hammer_print_btree_elm(elm, HAMMER_BTREE_TYPE_INTERNAL, -1);
		panic("btree_set_parent: bad subtree_type");
		break; /* NOT REACHED */
	}
	return(error);
}

#if 0

/*
 * This routine is called on the internal node (node) prior to recursing down
 * through (node, index) when the node referenced by (node, index) MIGHT
 * have too few elements for the caller to perform a deletion.
 *
 * cursor->index is invalid on return because the separators may have gotten
 * adjusted, the caller must rescan the node's elements.  The caller may set
 * cursor->index to -1 if it wants us to do a general rebalancing.
 *
 * This routine rebalances the children of the (node), collapsing children
 * together if possible.  On return each child will have at least L/2-1
 * elements unless the node only has one child.
 * 
 * NOTE: Because we do not update the parent's parent in the split code,
 * the subtree_count used by the caller may be incorrect.  We correct it
 * here.  Also note that we cannot change the depth of the tree's leaf
 * nodes here (see btree_collapse()).
 *
 * NOTE: We make no attempt to rebalance inter-cluster elements.
 */
static
int
btree_rebalance(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t node;
	hammer_node_t children[HAMMER_BTREE_INT_ELMS];
	hammer_node_t child;
	hammer_btree_elm_t elm;
	hammer_btree_elm_t elms;
	int i, j, n, nelms, goal;
	int maxelms, halfelms;
	int error;

	/*
	 * If the elm being recursed through is an inter-cluster reference,
	 * don't worry about it.
	 */
	ondisk = cursor->node->ondisk;
	elm = &ondisk->elms[cursor->index];
	if (elm->internal.subtree_type == HAMMER_BTREE_TYPE_CLUSTER)
		return(0);

	KKASSERT(elm->internal.subtree_offset != 0);
	error = 0;

	/*
	 * Load the children of node and do any necessary corrections
	 * to subtree_count.  subtree_count may be too low due to the
	 * way insertions split nodes.  Get a count of the total number
	 * of actual elements held by our children.
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
				     &child_buffer[i], XXX);
		children[i] = child;
		if (child == NULL)
			continue;
		XXX
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
			if (node->base.subtype == HAMMER_BTREE_TYPE_LEAF)
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
			if (node->base.subtype == HAMMER_BTREE_TYPE_LEAF) {
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
			    node->base.subtype == HAMMER_BTREE_TYPE_INTERNAL) {
				subchild = hammer_bread(cursor->cluster,
							elms[n].u.internal.subtree_offset,
							HAMMER_FSBUF_BTREE,
							&error,
							&subchild_buffer, XXX);
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
		if (node->base.subtype == HAMMER_BTREE_TYPE_INTERNAL)
			child->internal.elms[j] = elms[n].u.internal;
		child->base.count = j;
		hammer_modify_buffer(child_buffer[i]);
		if (subchild_buffer)
			hammer_put_buffer(subchild_buffer, 0);

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
			hammer_put_buffer(child_buffer[i], 0);
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
			hammer_put_buffer(child_buffer[i], 0);
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
btree_collapse(hammer_cursor_t cursor)
{
	hammer_btree_node_ondisk_t root, child;
	hammer_btree_node_ondisk_t children[HAMMER_BTREE_INT_ELMS];
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
	KKASSERT(root->base.type == HAMMER_BTREE_TYPE_INTERNAL);
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
				     &child_buffer[i], XXX);
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
	if (root->base.subtype == HAMMER_BTREE_TYPE_LEAF) {
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
							&subchild_buffer,
							XXX);
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
		root->base.type = HAMMER_BTREE_TYPE_INTERNAL;
		root->base.subtype = subsubtype;
		if (subchild_buffer)
			hammer_put_buffer(subchild_buffer, 0);
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
			hammer_put_buffer(child_buffer[i], 0);
	}
	return(error);
}

#endif

/************************************************************************
 *			   MISCELLANIOUS SUPPORT 			*
 ************************************************************************/

/*
 * Compare two B-Tree elements, return -1, 0, or +1 (e.g. similar to strcmp).
 *
 * See also hammer_rec_rb_compare() and hammer_rec_cmp() in hammer_object.c.
 *
 * Note that key1 and key2 are treated differently.  key1 is allowed to
 * wildcard some of its fields by setting them to 0, while key2 is expected
 * to be in an on-disk form (no wildcards).
 */
int
hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2)
{
#if 0
	kprintf("compare obj_id %016llx %016llx\n",
		key1->obj_id, key2->obj_id);
	kprintf("compare rec_type %04x %04x\n",
		key1->rec_type, key2->rec_type);
	kprintf("compare key %016llx %016llx\n",
		key1->key, key2->key);
#endif

	/*
	 * A key1->obj_id of 0 matches any object id
	 */
	if (key1->obj_id) {
		if (key1->obj_id < key2->obj_id)
			return(-4);
		if (key1->obj_id > key2->obj_id)
			return(4);
	}

	/*
	 * A key1->rec_type of 0 matches any record type.
	 */
	if (key1->rec_type) {
		if (key1->rec_type < key2->rec_type)
			return(-3);
		if (key1->rec_type > key2->rec_type)
			return(3);
	}

	/*
	 * There is no special case for key.  0 means 0.
	 */
	if (key1->key < key2->key)
		return(-2);
	if (key1->key > key2->key)
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
	if (key1->create_tid) {
		if (key1->create_tid < key2->create_tid)
			return(-1);
		if (key2->delete_tid && key1->create_tid >= key2->delete_tid)
			return(1);
	}

	return(0);
}

/*
 * Compare the element against the cursor's beginning and ending keys
 */
int
hammer_btree_range_cmp(hammer_cursor_t cursor, hammer_base_elm_t key2)
{
	/*
	 * A cursor->key_beg.obj_id of 0 matches any object id
	 */
	if (cursor->key_beg.obj_id) {
		if (cursor->key_end.obj_id < key2->obj_id)
			return(-4);
		if (cursor->key_beg.obj_id > key2->obj_id)
			return(4);
	}

	/*
	 * A cursor->key_beg.rec_type of 0 matches any record type.
	 */
	if (cursor->key_beg.rec_type) {
		if (cursor->key_end.rec_type < key2->rec_type)
			return(-3);
		if (cursor->key_beg.rec_type > key2->rec_type)
			return(3);
	}

	/*
	 * There is no special case for key.  0 means 0.
	 */
	if (cursor->key_end.key < key2->key)
		return(-2);
	if (cursor->key_beg.key > key2->key)
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
	 *
	 * NOTE: only key_beg.create_tid is used for create_tid, we can only
	 * do as-of scans at the moment.
	 */
	if (cursor->key_beg.create_tid) {
		if (cursor->key_beg.create_tid < key2->create_tid)
			return(-1);
		if (key2->delete_tid && cursor->key_beg.create_tid >= key2->delete_tid)
			return(1);
	}

	return(0);
}

/*
 * Create a separator half way inbetween key1 and key2.  For fields just
 * one unit apart, the separator will match key2.
 *
 * The handling of delete_tid is a little confusing.  It is only possible
 * to have one record in the B-Tree where all fields match except delete_tid.
 * This means, worse case, two adjacent elements may have a create_tid that
 * is one-apart and cause the separator to choose the right-hand element's
 * create_tid.  e.g.  (create,delete):  (1,x)(2,x) -> separator is (2,x).
 *
 * So all we have to do is set delete_tid to the right-hand element to
 * guarentee that the separator is properly between the two elements.
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
	MAKE_SEPARATOR(key1, key2, dest, create_tid);
	dest->delete_tid = key2->delete_tid;
}

#undef MAKE_SEPARATOR

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
	kprintf("\tsubtree_type = %02x\n", elm->subtree_type);

	if (type == HAMMER_BTREE_TYPE_INTERNAL) {
		if (elm->internal.rec_offset) {
			kprintf("\tcluster_rec  = %08x\n",
				elm->internal.rec_offset);
			kprintf("\tcluster_id   = %08x\n",
				elm->internal.subtree_cluid);
			kprintf("\tvolno        = %08x\n",
				elm->internal.subtree_volno);
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
