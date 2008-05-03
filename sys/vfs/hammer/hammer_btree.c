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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.c,v 1.41 2008/05/03 20:21:20 dillon Exp $
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
 * INSERTIONS:  A search performed with the intention of doing
 * an insert will guarantee that the terminal leaf node is not full by
 * splitting full nodes.  Splits occur top-down during the dive down the
 * B-Tree.
 *
 * DELETIONS: A deletion makes no attempt to proactively balance the
 * tree and will recursively remove nodes that become empty.  Empty
 * nodes are not allowed and a deletion may recurse upwards from the leaf.
 * Rather then allow a deadlock a deletion may terminate early by setting
 * an internal node's element's subtree_offset to 0.  The deletion will
 * then be resumed the next time a search encounters the element.
 */
#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

static int btree_search(hammer_cursor_t cursor, int flags);
static int btree_split_internal(hammer_cursor_t cursor);
static int btree_split_leaf(hammer_cursor_t cursor);
static int btree_remove(hammer_cursor_t cursor);
static int btree_remove_deleted_element(hammer_cursor_t cursor);
static int btree_set_parent(hammer_transaction_t trans, hammer_node_t node,
			hammer_btree_elm_t elm);
static int btree_node_is_full(hammer_node_ondisk_t node);
static void hammer_make_separator(hammer_base_elm_t key1,
			hammer_base_elm_t key2, hammer_base_elm_t dest);
static void hammer_btree_unlock_children(
			struct hammer_node_locklist **locklistp);

/*
 * Iterate records after a search.  The cursor is iterated forwards past
 * the current record until a record matching the key-range requirements
 * is found.  ENOENT is returned if the iteration goes past the ending
 * key. 
 *
 * The iteration is inclusive of key_beg and can be inclusive or exclusive
 * of key_end depending on whether HAMMER_CURSOR_END_INCLUSIVE is set.
 *
 * When doing an as-of search (cursor->asof != 0), key_beg.create_tid
 * may be modified by B-Tree functions.
 *
 * cursor->key_beg may or may not be modified by this function during
 * the iteration.  XXX future - in case of an inverted lock we may have
 * to reinitiate the lookup and set key_beg to properly pick up where we
 * left off.
 *
 * NOTE!  EDEADLK *CANNOT* be returned by this procedure.
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
		 * If we are at the root of the filesystem, cursor_up
		 * returns ENOENT.
		 *
		 * XXX this could be optimized by storing the information in
		 * the parent reference.
		 *
		 * XXX we can lose the node lock temporarily, this could mess
		 * up our scan.
		 */
		if (cursor->index == node->count) {
			if (hammer_debug_btree) {
				kprintf("BRACKETU %016llx[%d] -> %016llx[%d] (td=%p)\n",
					cursor->node->node_offset,
					cursor->index,
					(cursor->parent ? cursor->parent->node_offset : -1),
					cursor->parent_index,
					curthread);
			}
			KKASSERT(cursor->parent == NULL || cursor->parent->ondisk->elms[cursor->parent_index].internal.subtree_offset == cursor->node->node_offset);
			error = hammer_cursor_up(cursor);
			if (error)
				break;
			/* reload stale pointer */
			node = cursor->node->ondisk;
			KKASSERT(cursor->index != node->count);
			++cursor->index;
			continue;
		}

		/*
		 * Check internal or leaf element.  Determine if the record
		 * at the cursor has gone beyond the end of our range.
		 *
		 * We recurse down through internal nodes.
		 */
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm[0].base);
			s = hammer_btree_cmp(&cursor->key_beg, &elm[1].base);
			if (hammer_debug_btree) {
				kprintf("BRACKETL %016llx[%d] %016llx %02x %016llx %d (td=%p)\n",
					cursor->node->node_offset,
					cursor->index,
					elm[0].internal.base.obj_id,
					elm[0].internal.base.rec_type,
					elm[0].internal.base.key,
					r,
					curthread
				);
				kprintf("BRACKETR %016llx[%d] %016llx %02x %016llx %d\n",
					cursor->node->node_offset,
					cursor->index + 1,
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
			if (r == 0 && (cursor->flags &
				       HAMMER_CURSOR_END_INCLUSIVE) == 0) {
				error = ENOENT;
				break;
			}
			KKASSERT(s <= 0);

			/*
			 * When iterating try to clean up any deleted
			 * internal elements left over from btree_remove()
			 * deadlocks, but it is ok if we can't.
			 */
			if (elm->internal.subtree_offset == 0) {
				kprintf("REMOVE DELETED ELEMENT\n");
				btree_remove_deleted_element(cursor);
				/* note: elm also invalid */
			} else if (elm->internal.subtree_offset != 0) {
				error = hammer_cursor_down(cursor);
				if (error)
					break;
				KKASSERT(cursor->index == 0);
			}
			/* reload stale pointer */
			node = cursor->node->ondisk;
			continue;
		} else {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm->base);
			if (hammer_debug_btree) {
				kprintf("ELEMENT  %016llx:%d %c %016llx %02x %016llx %d\n",
					cursor->node->node_offset,
					cursor->index,
					(elm[0].leaf.base.btype ?
					 elm[0].leaf.base.btype : '?'),
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

			/*
			 * We support both end-inclusive and
			 * end-exclusive searches.
			 */
			if (r == 0 &&
			   (cursor->flags & HAMMER_CURSOR_END_INCLUSIVE) == 0) {
				error = ENOENT;
				break;
			}

			switch(elm->leaf.base.btype) {
			case HAMMER_BTREE_TYPE_RECORD:
				if ((cursor->flags & HAMMER_CURSOR_ASOF) &&
				    hammer_btree_chkts(cursor->asof, &elm->base)) {
					++cursor->index;
					continue;
				}
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error)
				break;
		}
		/*
		 * node pointer invalid after loop
		 */

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
 * Iterate in the reverse direction.  This is used by the pruning code to
 * avoid overlapping records.
 */
int
hammer_btree_iterate_reverse(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int error;
	int r;
	int s;

	/*
	 * Skip past the current record.  For various reasons the cursor
	 * may end up set to -1 or set to point at the end of the current
	 * node.  These cases must be addressed.
	 */
	node = cursor->node->ondisk;
	if (node == NULL)
		return(ENOENT);
	if (cursor->index != -1 && 
	    (cursor->flags & HAMMER_CURSOR_ATEDISK)) {
		--cursor->index;
	}
	if (cursor->index == cursor->node->ondisk->count)
		--cursor->index;

	/*
	 * Loop until an element is found or we are done.
	 */
	for (;;) {
		/*
		 * We iterate up the tree and then index over one element
		 * while we are at the last element in the current node.
		 */
		if (cursor->index == -1) {
			error = hammer_cursor_up(cursor);
			if (error) {
				cursor->index = 0; /* sanity */
				break;
			}
			/* reload stale pointer */
			node = cursor->node->ondisk;
			KKASSERT(cursor->index != node->count);
			--cursor->index;
			continue;
		}

		/*
		 * Check internal or leaf element.  Determine if the record
		 * at the cursor has gone beyond the end of our range.
		 *
		 * We recurse down through internal nodes. 
		 */
		KKASSERT(cursor->index != node->count);
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm[0].base);
			s = hammer_btree_cmp(&cursor->key_beg, &elm[1].base);
			if (hammer_debug_btree) {
				kprintf("BRACKETL %016llx[%d] %016llx %02x %016llx %d\n",
					cursor->node->node_offset,
					cursor->index,
					elm[0].internal.base.obj_id,
					elm[0].internal.base.rec_type,
					elm[0].internal.base.key,
					r
				);
				kprintf("BRACKETR %016llx[%d] %016llx %02x %016llx %d\n",
					cursor->node->node_offset,
					cursor->index + 1,
					elm[1].internal.base.obj_id,
					elm[1].internal.base.rec_type,
					elm[1].internal.base.key,
					s
				);
			}

			if (s >= 0) {
				error = ENOENT;
				break;
			}
			KKASSERT(r >= 0);

			/*
			 * When iterating try to clean up any deleted
			 * internal elements left over from btree_remove()
			 * deadlocks, but it is ok if we can't.
			 */
			if (elm->internal.subtree_offset == 0) {
				btree_remove_deleted_element(cursor);
				/* note: elm also invalid */
			} else if (elm->internal.subtree_offset != 0) {
				error = hammer_cursor_down(cursor);
				if (error)
					break;
				KKASSERT(cursor->index == 0);
				cursor->index = cursor->node->ondisk->count - 1;
			}
			/* reload stale pointer */
			node = cursor->node->ondisk;
			continue;
		} else {
			elm = &node->elms[cursor->index];
			s = hammer_btree_cmp(&cursor->key_beg, &elm->base);
			if (hammer_debug_btree) {
				kprintf("ELEMENT  %016llx:%d %c %016llx %02x %016llx %d\n",
					cursor->node->node_offset,
					cursor->index,
					(elm[0].leaf.base.btype ?
					 elm[0].leaf.base.btype : '?'),
					elm[0].leaf.base.obj_id,
					elm[0].leaf.base.rec_type,
					elm[0].leaf.base.key,
					s
				);
			}
			if (s > 0) {
				error = ENOENT;
				break;
			}

			switch(elm->leaf.base.btype) {
			case HAMMER_BTREE_TYPE_RECORD:
				if ((cursor->flags & HAMMER_CURSOR_ASOF) &&
				    hammer_btree_chkts(cursor->asof, &elm->base)) {
					--cursor->index;
					continue;
				}
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error)
				break;
		}
		/*
		 * node pointer invalid after loop
		 */

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
 * could not be found, EDEADLK if inserting and a retry is needed, and a
 * fatal error otherwise.  When retrying, the caller must terminate the
 * cursor and reinitialize it.  EDEADLK cannot be returned if not inserting.
 * 
 * The cursor is suitably positioned for a deletion on success, and suitably
 * positioned for an insertion on ENOENT if HAMMER_CURSOR_INSERT was
 * specified.
 *
 * The cursor may begin anywhere, the search will traverse the tree in
 * either direction to locate the requested element.
 *
 * Most of the logic implementing historical searches is handled here.  We
 * do an initial lookup with create_tid set to the asof TID.  Due to the
 * way records are laid out, a backwards iteration may be required if
 * ENOENT is returned to locate the historical record.  Here's the
 * problem:
 *
 * create_tid:    10      15       20
 *		     LEAF1   LEAF2
 * records:         (11)        (18)
 *
 * Lets say we want to do a lookup AS-OF timestamp 17.  We will traverse
 * LEAF2 but the only record in LEAF2 has a create_tid of 18, which is
 * not visible and thus causes ENOENT to be returned.  We really need
 * to check record 11 in LEAF1.  If it also fails then the search fails
 * (e.g. it might represent the range 11-16 and thus still not match our
 * AS-OF timestamp of 17).
 *
 * If this case occurs btree_search() will set HAMMER_CURSOR_CREATE_CHECK
 * and the cursor->create_check TID if an iteration might be needed.
 * In the above example create_check would be set to 14.
 */
int
hammer_btree_lookup(hammer_cursor_t cursor)
{
	int error;

	if (cursor->flags & HAMMER_CURSOR_ASOF) {
		KKASSERT((cursor->flags & HAMMER_CURSOR_INSERT) == 0);
		cursor->key_beg.create_tid = cursor->asof;
		for (;;) {
			cursor->flags &= ~HAMMER_CURSOR_CREATE_CHECK;
			error = btree_search(cursor, 0);
			if (error != ENOENT ||
			    (cursor->flags & HAMMER_CURSOR_CREATE_CHECK) == 0) {
				/*
				 * Stop if no error.
				 * Stop if error other then ENOENT.
				 * Stop if ENOENT and not special case.
				 */
				break;
			}
			if (hammer_debug_btree) {
				kprintf("CREATE_CHECK %016llx\n",
					cursor->create_check);
			}
			cursor->key_beg.create_tid = cursor->create_check;
			/* loop */
		}
	} else {
		error = btree_search(cursor, 0);
	}
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
 * Similarly but for an iteration in the reverse direction.
 */
int
hammer_btree_last(hammer_cursor_t cursor)
{
	struct hammer_base_elm save;
	int error;

	save = cursor->key_beg;
	cursor->key_beg = cursor->key_end;
	error = hammer_btree_lookup(cursor);
	cursor->key_beg = save;
	if (error == ENOENT ||
	    (cursor->flags & HAMMER_CURSOR_END_INCLUSIVE) == 0) {
		cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		error = hammer_btree_iterate_reverse(cursor);
	}
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	return(error);
}

/*
 * Extract the record and/or data associated with the cursor's current
 * position.  Any prior record or data stored in the cursor is replaced.
 * The cursor must be positioned at a leaf node.
 *
 * NOTE: All extractions occur at the leaf of the B-Tree.
 */
int
hammer_btree_extract(hammer_cursor_t cursor, int flags)
{
	hammer_mount_t hmp;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	hammer_off_t rec_off;
	hammer_off_t data_off;
	int error;

	/*
	 * The case where the data reference resolves to the same buffer
	 * as the record reference must be handled.
	 */
	node = cursor->node->ondisk;
	elm = &node->elms[cursor->index];
	cursor->data = NULL;
	hmp = cursor->node->hmp;
	flags |= cursor->flags & HAMMER_CURSOR_DATAEXTOK;

	/*
	 * There is nothing to extract for an internal element.
	 */
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL)
		return(EINVAL);

	/*
	 * Only record types have data.
	 */
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	if (elm->leaf.base.btype != HAMMER_BTREE_TYPE_RECORD)
		flags &= ~HAMMER_CURSOR_GET_DATA;
	data_off = elm->leaf.data_offset;
	if (data_off == 0)
		flags &= ~HAMMER_CURSOR_GET_DATA;
	rec_off = elm->leaf.rec_offset;

	/*
	 * Extract the record if the record was requested or the data
	 * resides in the record buf.
	 */
	if ((flags & HAMMER_CURSOR_GET_RECORD) ||
	    ((flags & HAMMER_CURSOR_GET_DATA) &&
	     ((rec_off ^ data_off) & ~HAMMER_BUFMASK64) == 0)) {
		cursor->record = hammer_bread(hmp, rec_off, &error,
					      &cursor->record_buffer);
	} else {
		rec_off = 0;
		error = 0;
	}
	if ((flags & HAMMER_CURSOR_GET_DATA) && error == 0) {
		if ((rec_off ^ data_off) & ~HAMMER_BUFMASK64) {
			/*
			 * Data and record are in different buffers.
			 */
			cursor->data = hammer_bread(hmp, data_off, &error,
						    &cursor->data_buffer);
		} else {
			/*
			 * Data resides in same buffer as record.
			 */
			cursor->data = (void *)
				((char *)cursor->record_buffer->ondisk +
				((int32_t)data_off & HAMMER_BUFMASK));
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
 * The caller may depend on the cursor's exclusive lock after return to
 * interlock frontend visibility (see HAMMER_RECF_CONVERT_DELETE).
 *
 * ENOSPC is returned if there is no room to insert a new record.
 */
int
hammer_btree_insert(hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_node_ondisk_t node;
	int i;
	int error;

	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);

	/*
	 * Insert the element at the leaf node and update the count in the
	 * parent.  It is possible for parent to be NULL, indicating that
	 * the filesystem's ROOT B-Tree node is a leaf itself, which is
	 * possible.  The root inode can never be deleted so the leaf should
	 * never be empty.
	 *
	 * Remember that the right-hand boundary is not included in the
	 * count.
	 */
	hammer_modify_node_all(cursor->trans, cursor->node);
	node = cursor->node->ondisk;
	i = cursor->index;
	KKASSERT(elm->base.btype != 0);
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(node->count < HAMMER_BTREE_LEAF_ELMS);
	if (i != node->count) {
		bcopy(&node->elms[i], &node->elms[i+1],
		      (node->count - i) * sizeof(*elm));
	}
	node->elms[i] = *elm;
	++node->count;
	hammer_modify_node_done(cursor->node);

	/*
	 * Debugging sanity checks.
	 */
	KKASSERT(hammer_btree_cmp(cursor->left_bound, &elm->leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &elm->leaf.base) > 0);
	if (i) {
		KKASSERT(hammer_btree_cmp(&node->elms[i-1].leaf.base, &elm->leaf.base) < 0);
	}
	if (i != node->count - 1)
		KKASSERT(hammer_btree_cmp(&node->elms[i+1].leaf.base, &elm->leaf.base) > 0);

	return(0);
}

/*
 * Delete a record from the B-Tree at the current cursor position.
 * The cursor is positioned such that the current element is the one
 * to be deleted.
 *
 * On return the cursor will be positioned after the deleted element and
 * MAY point to an internal node.  It will be suitable for the continuation
 * of an iteration but not for an insertion or deletion.
 *
 * Deletions will attempt to partially rebalance the B-Tree in an upward
 * direction, but will terminate rather then deadlock.  Empty leaves are
 * not allowed.  An early termination will leave an internal node with an
 * element whos subtree_offset is 0, a case detected and handled by
 * btree_search().
 *
 * This function can return EDEADLK, requiring the caller to retry the
 * operation after clearing the deadlock.
 */
int
hammer_btree_delete(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t node;
	hammer_node_t parent;
	int error;
	int i;

	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);

	/*
	 * Delete the element from the leaf node. 
	 *
	 * Remember that leaf nodes do not have boundaries.
	 */
	node = cursor->node;
	ondisk = node->ondisk;
	i = cursor->index;

	KKASSERT(ondisk->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(i >= 0 && i < ondisk->count);
	hammer_modify_node_all(cursor->trans, node);
	if (i + 1 != ondisk->count) {
		bcopy(&ondisk->elms[i+1], &ondisk->elms[i],
		      (ondisk->count - i - 1) * sizeof(ondisk->elms[0]));
	}
	--ondisk->count;
	hammer_modify_node_done(node);

	/*
	 * Validate local parent
	 */
	if (ondisk->parent) {
		parent = cursor->parent;

		KKASSERT(parent != NULL);
		KKASSERT(parent->node_offset == ondisk->parent);
	}

	/*
	 * If the leaf becomes empty it must be detached from the parent,
	 * potentially recursing through to the filesystem root.
	 *
	 * This may reposition the cursor at one of the parent's of the
	 * current node.
	 *
	 * Ignore deadlock errors, that simply means that btree_remove
	 * was unable to recurse and had to leave the subtree_offset 
	 * in the parent set to 0.
	 */
	KKASSERT(cursor->index <= ondisk->count);
	if (ondisk->count == 0) {
		do {
			error = btree_remove(cursor);
		} while (error == EAGAIN);
		if (error == EDEADLK)
			error = 0;
	} else {
		error = 0;
	}
	KKASSERT(cursor->parent == NULL ||
		 cursor->parent_index < cursor->parent->ondisk->count);
	return(error);
}

/*
 * PRIMAY B-TREE SEARCH SUPPORT PROCEDURE
 *
 * Search the filesystem B-Tree for cursor->key_beg, return the matching node.
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
 * The search is only guarenteed to end up on a leaf if an error code of 0
 * is returned, or if inserting and an error code of ENOENT is returned.
 * Otherwise it can stop at an internal node.  On success a search returns
 * a leaf node.
 *
 * COMPLEXITY WARNING!  This is the core B-Tree search code for the entire
 * filesystem, and it is not simple code.  Please note the following facts:
 *
 * - Internal node recursions have a boundary on the left AND right.  The
 *   right boundary is non-inclusive.  The create_tid is a generic part
 *   of the key for internal nodes.
 *
 * - Leaf nodes contain terminal elements only now.
 *
 * - Filesystem lookups typically set HAMMER_CURSOR_ASOF, indicating a
 *   historical search.  ASOF and INSERT are mutually exclusive.  When
 *   doing an as-of lookup btree_search() checks for a right-edge boundary
 *   case.  If while recursing down the left-edge differs from the key
 *   by ONLY its create_tid, HAMMER_CURSOR_CREATE_CHECK is set along
 *   with cursor->create_check.  This is used by btree_lookup() to iterate.
 *   The iteration backwards because as-of searches can wind up going
 *   down the wrong branch of the B-Tree.
 */
static 
int
btree_search(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int error;
	int enospc = 0;
	int i;
	int r;
	int s;

	flags |= cursor->flags;

	if (hammer_debug_btree) {
		kprintf("SEARCH   %016llx[%d] %016llx %02x key=%016llx cre=%016llx (td = %p)\n",
			cursor->node->node_offset, 
			cursor->index,
			cursor->key_beg.obj_id,
			cursor->key_beg.rec_type,
			cursor->key_beg.key,
			cursor->key_beg.create_tid, 
			curthread
		);
		if (cursor->parent)
		    kprintf("SEARCHP %016llx[%d] (%016llx/%016llx %016llx/%016llx) (%p/%p %p/%p)\n",
			cursor->parent->node_offset, cursor->parent_index,
			cursor->left_bound->obj_id,
			cursor->parent->ondisk->elms[cursor->parent_index].internal.base.obj_id,
			cursor->right_bound->obj_id,
			cursor->parent->ondisk->elms[cursor->parent_index+1].internal.base.obj_id,
			cursor->left_bound,
			&cursor->parent->ondisk->elms[cursor->parent_index],
			cursor->right_bound,
			&cursor->parent->ondisk->elms[cursor->parent_index+1]
		    );
	}

	/*
	 * Move our cursor up the tree until we find a node whos range covers
	 * the key we are trying to locate.
	 *
	 * The left bound is inclusive, the right bound is non-inclusive.
	 * It is ok to cursor up too far.
	 */
	for (;;) {
		r = hammer_btree_cmp(&cursor->key_beg, cursor->left_bound);
		s = hammer_btree_cmp(&cursor->key_beg, cursor->right_bound);
		if (r >= 0 && s < 0)
			break;
		KKASSERT(cursor->parent);
		error = hammer_cursor_up(cursor);
		if (error)
			goto done;
	}

	/*
	 * The delete-checks below are based on node, not parent.  Set the
	 * initial delete-check based on the parent.
	 */
	if (r == 1) {
		KKASSERT(cursor->left_bound->create_tid != 1);
		cursor->create_check = cursor->left_bound->create_tid - 1;
		cursor->flags |= HAMMER_CURSOR_CREATE_CHECK;
	}

	/*
	 * We better have ended up with a node somewhere.
	 */
	KKASSERT(cursor->node != NULL);

	/*
	 * If we are inserting we can't start at a full node if the parent
	 * is also full (because there is no way to split the node),
	 * continue running up the tree until the requirement is satisfied
	 * or we hit the root of the filesystem.
	 *
	 * (If inserting we aren't doing an as-of search so we don't have
	 *  to worry about create_check).
	 */
	while ((flags & HAMMER_CURSOR_INSERT) && enospc == 0) {
		if (cursor->node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			if (btree_node_is_full(cursor->node->ondisk) == 0)
				break;
		} else {
			if (btree_node_is_full(cursor->node->ondisk) ==0)
				break;
		}
		if (cursor->node->ondisk->parent == 0 ||
		    cursor->parent->ondisk->count != HAMMER_BTREE_INT_ELMS) {
			break;
		}
		error = hammer_cursor_up(cursor);
		/* node may have become stale */
		if (error)
			goto done;
	}

re_search:
	/*
	 * Push down through internal nodes to locate the requested key.
	 */
	node = cursor->node->ondisk;
	while (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
		/*
		 * Scan the node to find the subtree index to push down into.
		 * We go one-past, then back-up.
		 *
		 * We must proactively remove deleted elements which may
		 * have been left over from a deadlocked btree_remove().
		 *
		 * The left and right boundaries are included in the loop
		 * in order to detect edge cases.
		 *
		 * If the separator only differs by create_tid (r == 1)
		 * and we are doing an as-of search, we may end up going
		 * down a branch to the left of the one containing the
		 * desired key.  This requires numerous special cases.
		 */
		if (hammer_debug_btree) {
			kprintf("SEARCH-I %016llx count=%d\n",
				cursor->node->node_offset,
				node->count);
		}
		for (i = 0; i <= node->count; ++i) {
			elm = &node->elms[i];
			r = hammer_btree_cmp(&cursor->key_beg, &elm->base);
			if (hammer_debug_btree > 2) {
				kprintf(" IELM %p %d r=%d\n",
					&node->elms[i], i, r);
			}
			if (r < 0)
				break;
			if (r == 1) {
				KKASSERT(elm->base.create_tid != 1);
				cursor->create_check = elm->base.create_tid - 1;
				cursor->flags |= HAMMER_CURSOR_CREATE_CHECK;
			}
		}
		if (hammer_debug_btree) {
			kprintf("SEARCH-I preI=%d/%d r=%d\n",
				i, node->count, r);
		}

		/*
		 * These cases occur when the parent's idea of the boundary
		 * is wider then the child's idea of the boundary, and
		 * require special handling.  If not inserting we can
		 * terminate the search early for these cases but the
		 * child's boundaries cannot be unconditionally modified.
		 */
		if (i == 0) {
			/*
			 * If i == 0 the search terminated to the LEFT of the
			 * left_boundary but to the RIGHT of the parent's left
			 * boundary.
			 */
			u_int8_t save;

			elm = &node->elms[0];

			/*
			 * If we aren't inserting we can stop here.
			 */
			if ((flags & HAMMER_CURSOR_INSERT) == 0) {
				cursor->index = 0;
				return(ENOENT);
			}

			/*
			 * Correct a left-hand boundary mismatch.
			 *
			 * We can only do this if we can upgrade the lock.
			 *
			 * WARNING: We can only do this if inserting, i.e.
			 * we are running on the backend.
			 */
			if ((error = hammer_cursor_upgrade(cursor)) != 0)
				return(error);
			KKASSERT(cursor->flags & HAMMER_CURSOR_BACKEND);
			hammer_modify_node(cursor->trans, cursor->node,
					   &node->elms[0],
					   sizeof(node->elms[0]));
			save = node->elms[0].base.btype;
			node->elms[0].base = *cursor->left_bound;
			node->elms[0].base.btype = save;
			hammer_modify_node_done(cursor->node);
		} else if (i == node->count + 1) {
			/*
			 * If i == node->count + 1 the search terminated to
			 * the RIGHT of the right boundary but to the LEFT
			 * of the parent's right boundary.  If we aren't
			 * inserting we can stop here.
			 *
			 * Note that the last element in this case is
			 * elms[i-2] prior to adjustments to 'i'.
			 */
			--i;
			if ((flags & HAMMER_CURSOR_INSERT) == 0) {
				cursor->index = i;
				return (ENOENT);
			}

			/*
			 * Correct a right-hand boundary mismatch.
			 * (actual push-down record is i-2 prior to
			 * adjustments to i).
			 *
			 * We can only do this if we can upgrade the lock.
			 *
			 * WARNING: We can only do this if inserting, i.e.
			 * we are running on the backend.
			 */
			if ((error = hammer_cursor_upgrade(cursor)) != 0)
				return(error);
			elm = &node->elms[i];
			KKASSERT(cursor->flags & HAMMER_CURSOR_BACKEND);
			hammer_modify_node(cursor->trans, cursor->node,
					   &elm->base, sizeof(elm->base));
			elm->base = *cursor->right_bound;
			hammer_modify_node_done(cursor->node);
			--i;
		} else {
			/*
			 * The push-down index is now i - 1.  If we had
			 * terminated on the right boundary this will point
			 * us at the last element.
			 */
			--i;
		}
		cursor->index = i;
		elm = &node->elms[i];

		if (hammer_debug_btree) {
			kprintf("RESULT-I %016llx[%d] %016llx %02x "
				"key=%016llx cre=%016llx\n",
				cursor->node->node_offset,
				i,
				elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				elm->internal.base.key,
				elm->internal.base.create_tid
			);
		}

		/*
		 * When searching try to clean up any deleted
		 * internal elements left over from btree_remove()
		 * deadlocks.
		 *
		 * If we fail and we are doing an insertion lookup,
		 * we have to return EDEADLK, because an insertion lookup
		 * must terminate at a leaf.
		 */
		if (elm->internal.subtree_offset == 0) {
			error = btree_remove_deleted_element(cursor);
			if (error == 0)
				goto re_search;
			if (error == EDEADLK &&
			    (flags & HAMMER_CURSOR_INSERT) == 0) {
				error = ENOENT;
			}
			return(error);
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
		 * of entry.
		 */
		if ((flags & HAMMER_CURSOR_INSERT) && enospc == 0) {
			if (btree_node_is_full(node)) {
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
		}

		/*
		 * Push down (push into new node, existing node becomes
		 * the parent) and continue the search.
		 */
		error = hammer_cursor_down(cursor);
		/* node may have become stale */
		if (error)
			goto done;
		node = cursor->node->ondisk;
	}

	/*
	 * We are at a leaf, do a linear search of the key array.
	 *
	 * If we encounter a spike element type within the necessary
	 * range we push into it.
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
	KKASSERT (node->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(node->count <= HAMMER_BTREE_LEAF_ELMS);
	if (hammer_debug_btree) {
		kprintf("SEARCH-L %016llx count=%d\n",
			cursor->node->node_offset,
			node->count);
	}

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];

		r = hammer_btree_cmp(&cursor->key_beg, &elm->leaf.base);

		if (hammer_debug_btree > 1)
			kprintf("  ELM %p %d r=%d\n", &node->elms[i], i, r);

		/*
		 * We are at a record element.  Stop if we've flipped past
		 * key_beg, not counting the create_tid test.  Allow the
		 * r == 1 case (key_beg > element but differs only by its
		 * create_tid) to fall through to the AS-OF check.
		 */
		KKASSERT (elm->leaf.base.btype == HAMMER_BTREE_TYPE_RECORD);

		if (r < 0)
			goto failed;
		if (r > 1)
			continue;

		/*
		 * Check our as-of timestamp against the element.
		 */
		if (flags & HAMMER_CURSOR_ASOF) {
			if (hammer_btree_chkts(cursor->asof,
					       &node->elms[i].base) != 0) {
				continue;
			}
			/* success */
		} else {
			if (r > 0)	/* can only be +1 */
				continue;
			/* success */
		}
		cursor->index = i;
		error = 0;
		if (hammer_debug_btree) {
			kprintf("RESULT-L %016llx[%d] (SUCCESS)\n",
				cursor->node->node_offset, i);
		}
		goto done;
	}

	/*
	 * The search of the leaf node failed.  i is the insertion point.
	 */
failed:
	if (hammer_debug_btree) {
		kprintf("RESULT-L %016llx[%d] (FAILED)\n",
			cursor->node->node_offset, i);
	}

	/*
	 * No exact match was found, i is now at the insertion point.
	 *
	 * If inserting split a full leaf before returning.  This
	 * may have the side effect of adjusting cursor->node and
	 * cursor->index.
	 */
	cursor->index = i;
	if ((flags & HAMMER_CURSOR_INSERT) && enospc == 0 &&
	     btree_node_is_full(node)) {
		error = btree_split_leaf(cursor);
		if (error) {
			if (error != ENOSPC)
				goto done;
			enospc = 1;
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
 * Split an internal node into two nodes and move the separator at the split
 * point to the parent.
 *
 * (cursor->node, cursor->index) indicates the element the caller intends
 * to push into.  We will adjust node and index if that element winds
 * up in the split node.
 *
 * If we are at the root of the filesystem a new root must be created with
 * two elements, one pointing to the original root and one pointing to the
 * newly allocated split node.
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
	hammer_node_locklist_t locklist = NULL;
	hammer_mount_t hmp = cursor->trans->hmp;
	int parent_index;
	int made_root;
	int split;
	int error;
	int i;
	const int esize = sizeof(*elm);

	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);
	error = hammer_btree_lock_children(cursor, &locklist);
	if (error)
		goto done;

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

	/*
	 * If we are at the root of the filesystem, create a new root node
	 * with 1 element and split normally.  Avoid making major
	 * modifications until we know the whole operation will work.
	 */
	if (ondisk->parent == 0) {
		parent = hammer_alloc_btree(cursor->trans, &error);
		if (parent == NULL)
			goto done;
		hammer_lock_ex(&parent->lock);
		hammer_modify_node_noundo(cursor->trans, parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = hmp->root_btree_beg;
		ondisk->elms[0].base.btype = node->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = node->node_offset;
		ondisk->elms[1].base = hmp->root_btree_end;
		hammer_modify_node_done(parent);
		/* ondisk->elms[1].base.btype - not used */
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
	new_node = hammer_alloc_btree(cursor->trans, &error);
	if (new_node == NULL) {
		if (made_root) {
			hammer_unlock(&parent->lock);
			hammer_delete_node(cursor->trans, parent);
			hammer_rel_node(parent);
		}
		goto done;
	}
	hammer_lock_ex(&new_node->lock);

	/*
	 * Create the new node.  P becomes the left-hand boundary in the
	 * new node.  Copy the right-hand boundary as well.
	 *
	 * elm is the new separator.
	 */
	hammer_modify_node_noundo(cursor->trans, new_node);
	hammer_modify_node_all(cursor->trans, node);
	ondisk = node->ondisk;
	elm = &ondisk->elms[split];
	bcopy(elm, &new_node->ondisk->elms[0],
	      (ondisk->count - split + 1) * esize);
	new_node->ondisk->count = ondisk->count - split;
	new_node->ondisk->parent = parent->node_offset;
	new_node->ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
	KKASSERT(ondisk->type == new_node->ondisk->type);

	/*
	 * Cleanup the original node.  Elm (P) becomes the new boundary,
	 * its subtree_offset was moved to the new node.  If we had created
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
	hammer_modify_node_all(cursor->trans, parent);
	ondisk = parent->ondisk;
	KKASSERT(ondisk->count != HAMMER_BTREE_INT_ELMS);
	parent_elm = &ondisk->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (ondisk->count - parent_index) * esize);
	parent_elm->internal.base = elm->base;	/* separator P */
	parent_elm->internal.base.btype = new_node->ondisk->type;
	parent_elm->internal.subtree_offset = new_node->node_offset;
	++ondisk->count;
	hammer_modify_node_done(parent);

	/*
	 * The children of new_node need their parent pointer set to new_node.
	 * The children have already been locked by
	 * hammer_btree_lock_children().
	 */
	for (i = 0; i < new_node->ondisk->count; ++i) {
		elm = &new_node->ondisk->elms[i];
		error = btree_set_parent(cursor->trans, new_node, elm);
		if (error) {
			panic("btree_split_internal: btree-fixup problem");
		}
	}
	hammer_modify_node_done(new_node);

	/*
	 * The filesystem's root B-Tree pointer may have to be updated.
	 */
	if (made_root) {
		hammer_volume_t volume;

		volume = hammer_get_root_volume(hmp, &error);
		KKASSERT(error == 0);

		hammer_modify_volume_field(cursor->trans, volume,
					   vol0_btree_root);
		volume->ondisk->vol0_btree_root = parent->node_offset;
		hammer_modify_volume_done(volume);
		node->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
		hammer_rel_volume(volume, 0);
	}
	hammer_modify_node_done(node);


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

done:
	hammer_btree_unlock_children(&locklist);
	hammer_cursor_downgrade(cursor);
	return (error);
}

/*
 * Same as the above, but splits a full leaf node.
 *
 * This function
 */
static
int
btree_split_leaf(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t parent;
	hammer_node_t leaf;
	hammer_mount_t hmp;
	hammer_node_t new_leaf;
	hammer_btree_elm_t elm;
	hammer_btree_elm_t parent_elm;
	hammer_base_elm_t mid_boundary;
	int parent_index;
	int made_root;
	int split;
	int error;
	const size_t esize = sizeof(*elm);

	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);

	KKASSERT(hammer_btree_cmp(cursor->left_bound,
		 &cursor->node->ondisk->elms[0].leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &cursor->node->ondisk->elms[cursor->node->ondisk->count-1].leaf.base) > 0);

	/* 
	 * Calculate the split point.  If the insertion point will be on
	 * the left-hand side adjust the split point to give the right
	 * hand side one additional node.
	 *
	 * Spikes are made up of two leaf elements which cannot be
	 * safely split.
	 */
	leaf = cursor->node;
	ondisk = leaf->ondisk;
	split = (ondisk->count + 1) / 2;
	if (cursor->index <= split)
		--split;
	error = 0;
	hmp = leaf->hmp;

	elm = &ondisk->elms[split];

	KKASSERT(hammer_btree_cmp(cursor->left_bound, &elm[-1].leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->left_bound, &elm->leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &elm->leaf.base) > 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &elm[1].leaf.base) > 0);

	/*
	 * If we are at the root of the tree, create a new root node with
	 * 1 element and split normally.  Avoid making major modifications
	 * until we know the whole operation will work.
	 */
	if (ondisk->parent == 0) {
		parent = hammer_alloc_btree(cursor->trans, &error);
		if (parent == NULL)
			goto done;
		hammer_lock_ex(&parent->lock);
		hammer_modify_node_noundo(cursor->trans, parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = hmp->root_btree_beg;
		ondisk->elms[0].base.btype = leaf->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = leaf->node_offset;
		ondisk->elms[1].base = hmp->root_btree_end;
		/* ondisk->elms[1].base.btype = not used */
		hammer_modify_node_done(parent);
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
	new_leaf = hammer_alloc_btree(cursor->trans, &error);
	if (new_leaf == NULL) {
		if (made_root) {
			hammer_unlock(&parent->lock);
			hammer_delete_node(cursor->trans, parent);
			hammer_rel_node(parent);
		}
		goto done;
	}
	hammer_lock_ex(&new_leaf->lock);

	/*
	 * Create the new node and copy the leaf elements from the split 
	 * point on to the new node.
	 */
	hammer_modify_node_all(cursor->trans, leaf);
	hammer_modify_node_noundo(cursor->trans, new_leaf);
	ondisk = leaf->ondisk;
	elm = &ondisk->elms[split];
	bcopy(elm, &new_leaf->ondisk->elms[0], (ondisk->count - split) * esize);
	new_leaf->ondisk->count = ondisk->count - split;
	new_leaf->ondisk->parent = parent->node_offset;
	new_leaf->ondisk->type = HAMMER_BTREE_TYPE_LEAF;
	KKASSERT(ondisk->type == new_leaf->ondisk->type);
	hammer_modify_node_done(new_leaf);

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
	hammer_modify_node_all(cursor->trans, parent);
	ondisk = parent->ondisk;
	KKASSERT(split != 0);
	KKASSERT(ondisk->count != HAMMER_BTREE_INT_ELMS);
	parent_elm = &ondisk->elms[parent_index+1];
	bcopy(parent_elm, parent_elm + 1,
	      (ondisk->count - parent_index) * esize);

	hammer_make_separator(&elm[-1].base, &elm[0].base, &parent_elm->base);
	parent_elm->internal.base.btype = new_leaf->ondisk->type;
	parent_elm->internal.subtree_offset = new_leaf->node_offset;
	mid_boundary = &parent_elm->base;
	++ondisk->count;
	hammer_modify_node_done(parent);

	/*
	 * The filesystem's root B-Tree pointer may have to be updated.
	 */
	if (made_root) {
		hammer_volume_t volume;

		volume = hammer_get_root_volume(hmp, &error);
		KKASSERT(error == 0);

		hammer_modify_volume_field(cursor->trans, volume,
					   vol0_btree_root);
		volume->ondisk->vol0_btree_root = parent->node_offset;
		hammer_modify_volume_done(volume);
		leaf->ondisk->parent = parent->node_offset;
		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
		}
		cursor->parent = parent;	/* lock'd and ref'd */
		hammer_rel_volume(volume, 0);
	}
	hammer_modify_node_done(leaf);

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

	/*
	 * Assert that the bounds are correct.
	 */
	KKASSERT(hammer_btree_cmp(cursor->left_bound,
		 &cursor->node->ondisk->elms[0].leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &cursor->node->ondisk->elms[cursor->node->ondisk->count-1].leaf.base) > 0);
	KKASSERT(hammer_btree_cmp(cursor->left_bound, &cursor->key_beg) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &cursor->key_beg) > 0);

done:
	hammer_cursor_downgrade(cursor);
	return (error);
}

/*
 * Recursively correct the right-hand boundary's create_tid to (tid) as
 * long as the rest of the key matches.  We have to recurse upward in
 * the tree as well as down the left side of each parent's right node.
 *
 * Return EDEADLK if we were only partially successful, forcing the caller
 * to try again.  The original cursor is not modified.  This routine can
 * also fail with EDEADLK if it is forced to throw away a portion of its
 * record history.
 *
 * The caller must pass a downgraded cursor to us (otherwise we can't dup it).
 */
struct hammer_rhb {
	TAILQ_ENTRY(hammer_rhb) entry;
	hammer_node_t	node;
	int		index;
};

TAILQ_HEAD(hammer_rhb_list, hammer_rhb);

int
hammer_btree_correct_rhb(hammer_cursor_t cursor, hammer_tid_t tid)
{
	struct hammer_rhb_list rhb_list;
	hammer_base_elm_t elm;
	hammer_node_t orig_node;
	struct hammer_rhb *rhb;
	int orig_index;
	int error;

	TAILQ_INIT(&rhb_list);

	/*
	 * Save our position so we can restore it on return.  This also
	 * gives us a stable 'elm'.
	 */
	orig_node = cursor->node;
	hammer_ref_node(orig_node);
	hammer_lock_sh(&orig_node->lock);
	orig_index = cursor->index;
	elm = &orig_node->ondisk->elms[orig_index].base;

	/*
	 * Now build a list of parents going up, allocating a rhb
	 * structure for each one.
	 */
	while (cursor->parent) {
		/*
		 * Stop if we no longer have any right-bounds to fix up
		 */
		if (elm->obj_id != cursor->right_bound->obj_id ||
		    elm->rec_type != cursor->right_bound->rec_type ||
		    elm->key != cursor->right_bound->key) {
			break;
		}

		/*
		 * Stop if the right-hand bound's create_tid does not
		 * need to be corrected.
		 */
		if (cursor->right_bound->create_tid >= tid)
			break;

		rhb = kmalloc(sizeof(*rhb), M_HAMMER, M_WAITOK|M_ZERO);
		rhb->node = cursor->parent;
		rhb->index = cursor->parent_index;
		hammer_ref_node(rhb->node);
		hammer_lock_sh(&rhb->node->lock);
		TAILQ_INSERT_HEAD(&rhb_list, rhb, entry);

		hammer_cursor_up(cursor);
	}

	/*
	 * now safely adjust the right hand bound for each rhb.  This may
	 * also require taking the right side of the tree and iterating down
	 * ITS left side.
	 */
	error = 0;
	while (error == 0 && (rhb = TAILQ_FIRST(&rhb_list)) != NULL) {
		error = hammer_cursor_seek(cursor, rhb->node, rhb->index);
		kprintf("CORRECT RHB %016llx index %d type=%c\n",
			rhb->node->node_offset,
			rhb->index, cursor->node->ondisk->type);
		if (error)
			break;
		TAILQ_REMOVE(&rhb_list, rhb, entry);
		hammer_unlock(&rhb->node->lock);
		hammer_rel_node(rhb->node);
		kfree(rhb, M_HAMMER);

		switch (cursor->node->ondisk->type) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			/*
			 * Right-boundary for parent at internal node
			 * is one element to the right of the element whos
			 * right boundary needs adjusting.  We must then
			 * traverse down the left side correcting any left
			 * bounds (which may now be too far to the left).
			 */
			++cursor->index;
			error = hammer_btree_correct_lhb(cursor, tid);
			break;
		default:
			panic("hammer_btree_correct_rhb(): Bad node type");
			error = EINVAL;
			break;
		}
	}

	/*
	 * Cleanup
	 */
	while ((rhb = TAILQ_FIRST(&rhb_list)) != NULL) {
		TAILQ_REMOVE(&rhb_list, rhb, entry);
		hammer_unlock(&rhb->node->lock);
		hammer_rel_node(rhb->node);
		kfree(rhb, M_HAMMER);
	}
	error = hammer_cursor_seek(cursor, orig_node, orig_index);
	hammer_unlock(&orig_node->lock);
	hammer_rel_node(orig_node);
	return (error);
}

/*
 * Similar to rhb (in fact, rhb calls lhb), but corrects the left hand
 * bound going downward starting at the current cursor position.
 *
 * This function does not restore the cursor after use.
 */
int
hammer_btree_correct_lhb(hammer_cursor_t cursor, hammer_tid_t tid)
{
	struct hammer_rhb_list rhb_list;
	hammer_base_elm_t elm;
	hammer_base_elm_t cmp;
	struct hammer_rhb *rhb;
	int error;

	TAILQ_INIT(&rhb_list);

	cmp = &cursor->node->ondisk->elms[cursor->index].base;

	/*
	 * Record the node and traverse down the left-hand side for all
	 * matching records needing a boundary correction.
	 */
	error = 0;
	for (;;) {
		rhb = kmalloc(sizeof(*rhb), M_HAMMER, M_WAITOK|M_ZERO);
		rhb->node = cursor->node;
		rhb->index = cursor->index;
		hammer_ref_node(rhb->node);
		hammer_lock_sh(&rhb->node->lock);
		TAILQ_INSERT_HEAD(&rhb_list, rhb, entry);

		if (cursor->node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			/*
			 * Nothing to traverse down if we are at the right
			 * boundary of an internal node.
			 */
			if (cursor->index == cursor->node->ondisk->count)
				break;
		} else {
			elm = &cursor->node->ondisk->elms[cursor->index].base;
			if (elm->btype == HAMMER_BTREE_TYPE_RECORD)
				break;
			panic("Illegal leaf record type %02x", elm->btype);
		}
		error = hammer_cursor_down(cursor);
		if (error)
			break;

		elm = &cursor->node->ondisk->elms[cursor->index].base;
		if (elm->obj_id != cmp->obj_id ||
		    elm->rec_type != cmp->rec_type ||
		    elm->key != cmp->key) {
			break;
		}
		if (elm->create_tid >= tid)
			break;

	}

	/*
	 * Now we can safely adjust the left-hand boundary from the bottom-up.
	 * The last element we remove from the list is the caller's right hand
	 * boundary, which must also be adjusted.
	 */
	while (error == 0 && (rhb = TAILQ_FIRST(&rhb_list)) != NULL) {
		error = hammer_cursor_seek(cursor, rhb->node, rhb->index);
		if (error)
			break;
		TAILQ_REMOVE(&rhb_list, rhb, entry);
		hammer_unlock(&rhb->node->lock);
		hammer_rel_node(rhb->node);
		kfree(rhb, M_HAMMER);

		elm = &cursor->node->ondisk->elms[cursor->index].base;
		if (cursor->node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			kprintf("hammer_btree_correct_lhb-I @%016llx[%d]\n",
				cursor->node->node_offset, cursor->index);
			hammer_modify_node(cursor->trans, cursor->node,
					   elm, sizeof(*elm));
			elm->create_tid = tid;
			hammer_modify_node_done(cursor->node);
		} else {
			panic("hammer_btree_correct_lhb(): Bad element type");
		}
	}

	/*
	 * Cleanup
	 */
	while ((rhb = TAILQ_FIRST(&rhb_list)) != NULL) {
		TAILQ_REMOVE(&rhb_list, rhb, entry);
		hammer_unlock(&rhb->node->lock);
		hammer_rel_node(rhb->node);
		kfree(rhb, M_HAMMER);
	}
	return (error);
}

/*
 * Attempt to remove the empty B-Tree node at (cursor->node).  Returns 0
 * on success, EAGAIN if we could not acquire the necessary locks, or some
 * other error.  This node can be a leaf node or an internal node.
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
	hammer_node_t node;
	hammer_node_t parent;
	const int esize = sizeof(*elm);
	int error;

	node = cursor->node;

	/*
	 * When deleting the root of the filesystem convert it to
	 * an empty leaf node.  Internal nodes cannot be empty.
	 */
	if (node->ondisk->parent == 0) {
		hammer_modify_node_all(cursor->trans, node);
		ondisk = node->ondisk;
		ondisk->type = HAMMER_BTREE_TYPE_LEAF;
		ondisk->count = 0;
		hammer_modify_node_done(node);
		cursor->index = 0;
		return(0);
	}

	/*
	 * Zero-out the parent's reference to the child and flag the
	 * child for destruction.  This ensures that the child is not
	 * reused while other references to it exist.
	 */
	parent = cursor->parent;
	hammer_modify_node_all(cursor->trans, parent);
	ondisk = parent->ondisk;
	KKASSERT(ondisk->type == HAMMER_BTREE_TYPE_INTERNAL);
	elm = &ondisk->elms[cursor->parent_index];
	KKASSERT(elm->internal.subtree_offset == node->node_offset);
	elm->internal.subtree_offset = 0;

	hammer_flush_node(node);
	hammer_delete_node(cursor->trans, node);

	/*
	 * If the parent would otherwise not become empty we can physically
	 * remove the zero'd element.  Note however that in order to
	 * guarentee a valid cursor we still need to be able to cursor up
	 * because we no longer have a node.
	 *
	 * This collapse will change the parent's boundary elements, making
	 * them wider.  The new boundaries are recursively corrected in
	 * btree_search().
	 *
	 * XXX we can theoretically recalculate the midpoint but there isn't
	 * much of a reason to do it.
	 */
	error = hammer_cursor_up(cursor);
	if (error == 0)
		error = hammer_cursor_upgrade(cursor);

	if (error) {
		kprintf("BTREE_REMOVE: Cannot lock parent, skipping\n");
		Debugger("BTREE_REMOVE");
		hammer_modify_node_done(parent);
		return (0);
	}

	/*
	 * Remove the internal element from the parent.  The bcopy must
	 * include the right boundary element.
	 */
	KKASSERT(parent == cursor->node && ondisk == parent->ondisk);
	node = parent;
	parent = NULL;
	/* ondisk is node's ondisk */
	/* elm is node's element */

	/*
	 * Remove the internal element that we zero'd out.  Tell the caller
	 * to loop if it hits zero (to try to avoid eating up precious kernel
	 * stack).
	 */
	KKASSERT(ondisk->count > 0);
	bcopy(&elm[1], &elm[0], (ondisk->count - cursor->index) * esize);
	--ondisk->count;
	if (ondisk->count == 0)
		error = EAGAIN;
	hammer_modify_node_done(node);
	return(error);
}

/*
 * Attempt to remove the deleted internal element at the current cursor
 * position.  If we are unable to remove the element we return EDEADLK.
 *
 * If the current internal node becomes empty we delete it in the parent
 * and cursor up, looping until we finish or we deadlock.
 *
 * On return, if successful, the cursor will be pointing at the next
 * iterative position in the B-Tree.  If unsuccessful the cursor will be
 * pointing at the last deleted internal element that could not be
 * removed.
 */
static 
int
btree_remove_deleted_element(hammer_cursor_t cursor)
{
	hammer_node_t node;
	hammer_btree_elm_t elm; 
	int error;

	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);
	node = cursor->node;
	elm = &node->ondisk->elms[cursor->index];
	if (elm->internal.subtree_offset == 0) {
		do {
			error = btree_remove(cursor);
			kprintf("BTREE REMOVE DELETED ELEMENT %d\n", error);
		} while (error == EAGAIN);
	}
	return(error);
}

/*
 * The element (elm) has been moved to a new internal node (node).
 *
 * If the element represents a pointer to an internal node that node's
 * parent must be adjusted to the element's new location.
 *
 * XXX deadlock potential here with our exclusive locks
 */
static
int
btree_set_parent(hammer_transaction_t trans, hammer_node_t node,
		 hammer_btree_elm_t elm)
{
	hammer_node_t child;
	int error;

	error = 0;

	switch(elm->base.btype) {
	case HAMMER_BTREE_TYPE_INTERNAL:
	case HAMMER_BTREE_TYPE_LEAF:
		child = hammer_get_node(node->hmp,
					elm->internal.subtree_offset, &error);
		if (error == 0) {
			hammer_modify_node(trans, child,
					   &child->ondisk->parent,
					   sizeof(child->ondisk->parent));
			child->ondisk->parent = node->node_offset;
			hammer_modify_node_done(child);
			hammer_rel_node(child);
		}
		break;
	default:
		break;
	}
	return(error);
}

/*
 * Exclusively lock all the children of node.  This is used by the split
 * code to prevent anyone from accessing the children of a cursor node
 * while we fix-up its parent offset.
 *
 * If we don't lock the children we can really mess up cursors which block
 * trying to cursor-up into our node.
 *
 * On failure EDEADLK (or some other error) is returned.  If a deadlock
 * error is returned the cursor is adjusted to block on termination.
 */
int
hammer_btree_lock_children(hammer_cursor_t cursor,
			   struct hammer_node_locklist **locklistp)
{
	hammer_node_t node;
	hammer_node_locklist_t item;
	hammer_node_ondisk_t ondisk;
	hammer_btree_elm_t elm;
	hammer_node_t child;
	int error;
	int i;

	node = cursor->node;
	ondisk = node->ondisk;
	error = 0;
	for (i = 0; error == 0 && i < ondisk->count; ++i) {
		elm = &ondisk->elms[i];

		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_INTERNAL:
		case HAMMER_BTREE_TYPE_LEAF:
			child = hammer_get_node(node->hmp,
						elm->internal.subtree_offset,
						&error);
			break;
		default:
			child = NULL;
			break;
		}
		if (child) {
			if (hammer_lock_ex_try(&child->lock) != 0) {
				if (cursor->deadlk_node == NULL) {
					cursor->deadlk_node = child;
					hammer_ref_node(cursor->deadlk_node);
				}
				error = EDEADLK;
				hammer_rel_node(child);
			} else {
				item = kmalloc(sizeof(*item),
						M_HAMMER, M_WAITOK);
				item->next = *locklistp;
				item->node = child;
				*locklistp = item;
			}
		}
	}
	if (error)
		hammer_btree_unlock_children(locklistp);
	return(error);
}


/*
 * Release previously obtained node locks.
 */
static void
hammer_btree_unlock_children(struct hammer_node_locklist **locklistp)
{
	hammer_node_locklist_t item;

	while ((item = *locklistp) != NULL) {
		*locklistp = item->next;
		hammer_unlock(&item->node->lock);
		hammer_rel_node(item->node);
		kfree(item, M_HAMMER);
	}
}

/************************************************************************
 *			   MISCELLANIOUS SUPPORT 			*
 ************************************************************************/

/*
 * Compare two B-Tree elements, return -N, 0, or +N (e.g. similar to strcmp).
 *
 * Note that for this particular function a return value of -1, 0, or +1
 * can denote a match if create_tid is otherwise discounted.  A create_tid
 * of zero is considered to be 'infinity' in comparisons.
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

	/*
	 * A create_tid of zero indicates a record which is undeletable
	 * and must be considered to have a value of positive infinity.
	 */
	if (key1->create_tid == 0) {
		if (key2->create_tid == 0)
			return(0);
		return(1);
	}
	if (key2->create_tid == 0)
		return(-1);
	if (key1->create_tid < key2->create_tid)
		return(-1);
	if (key1->create_tid > key2->create_tid)
		return(1);
	return(0);
}

/*
 * Test a timestamp against an element to determine whether the
 * element is visible.  A timestamp of 0 means 'infinity'.
 */
int
hammer_btree_chkts(hammer_tid_t asof, hammer_base_elm_t base)
{
	if (asof == 0) {
		if (base->delete_tid)
			return(1);
		return(0);
	}
	if (asof < base->create_tid)
		return(-1);
	if (base->delete_tid && asof >= base->delete_tid)
		return(1);
	return(0);
}

/*
 * Create a separator half way inbetween key1 and key2.  For fields just
 * one unit apart, the separator will match key2.  key1 is on the left-hand
 * side and key2 is on the right-hand side.
 *
 * key2 must be >= the separator.  It is ok for the separator to match key2.
 *
 * NOTE: Even if key1 does not match key2, the separator may wind up matching
 * key2.
 *
 * NOTE: It might be beneficial to just scrap this whole mess and just
 * set the separator to key2.
 */
#define MAKE_SEPARATOR(key1, key2, dest, field)	\
	dest->field = key1->field + ((key2->field - key1->field + 1) >> 1);

static void
hammer_make_separator(hammer_base_elm_t key1, hammer_base_elm_t key2,
		      hammer_base_elm_t dest)
{
	bzero(dest, sizeof(*dest));

	dest->rec_type = key2->rec_type;
	dest->key = key2->key;
	dest->create_tid = key2->create_tid;

	MAKE_SEPARATOR(key1, key2, dest, obj_id);
	if (key1->obj_id == key2->obj_id) {
		MAKE_SEPARATOR(key1, key2, dest, rec_type);
		if (key1->rec_type == key2->rec_type) {
			MAKE_SEPARATOR(key1, key2, dest, key);
			/*
			 * Don't bother creating a separator for create_tid,
			 * which also conveniently avoids having to handle
			 * the create_tid == 0 (infinity) case.  Just leave
			 * create_tid set to key2.
			 *
			 * Worst case, dest matches key2 exactly, which is
			 * acceptable.
			 */
		}
	}
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

	kprintf("node %p count=%d parent=%016llx type=%c\n",
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
	kprintf("\tobj_id       = %016llx\n", elm->base.obj_id);
	kprintf("\tkey          = %016llx\n", elm->base.key);
	kprintf("\tcreate_tid   = %016llx\n", elm->base.create_tid);
	kprintf("\tdelete_tid   = %016llx\n", elm->base.delete_tid);
	kprintf("\trec_type     = %04x\n", elm->base.rec_type);
	kprintf("\tobj_type     = %02x\n", elm->base.obj_type);
	kprintf("\tbtype 	= %02x (%c)\n",
		elm->base.btype,
		(elm->base.btype ? elm->base.btype : '?'));

	switch(type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		kprintf("\tsubtree_off  = %016llx\n",
			elm->internal.subtree_offset);
		break;
	case HAMMER_BTREE_TYPE_RECORD:
		kprintf("\trec_offset   = %016llx\n", elm->leaf.rec_offset);
		kprintf("\tdata_offset  = %016llx\n", elm->leaf.data_offset);
		kprintf("\tdata_len     = %08x\n", elm->leaf.data_len);
		kprintf("\tdata_crc     = %08x\n", elm->leaf.data_crc);
		break;
	}
}
