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
 * with no valid sub-tree pointer for the right-most element.
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
 * The left-hand boundary (B in the left) is integrated into the first
 * element so it doesn't require 2 elements to accomodate boundaries.
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
 * tree and will recursively remove nodes that become empty.  If a
 * deadlock occurs a deletion may not be able to remove an empty leaf.
 * Deletions never allow internal nodes to become empty (that would blow
 * up the boundaries).
 */
#include "hammer.h"
#include <sys/buf.h>
#include <sys/buf2.h>

static int btree_search(hammer_cursor_t cursor, int flags);
static int btree_split_internal(hammer_cursor_t cursor);
static int btree_split_leaf(hammer_cursor_t cursor);
static int btree_remove(hammer_cursor_t cursor);
static __inline int btree_node_is_full(hammer_node_ondisk_t node);
static __inline int btree_max_elements(u_int8_t type);
static int hammer_btree_mirror_propagate(hammer_cursor_t cursor,	
			hammer_tid_t mirror_tid);
static void hammer_make_separator(hammer_base_elm_t key1,
			hammer_base_elm_t key2, hammer_base_elm_t dest);
static void hammer_cursor_mirror_filter(hammer_cursor_t cursor);

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
 * If HAMMER_CURSOR_ITERATE_CHECK is set it is possible that the cursor
 * was reverse indexed due to being moved to a parent while unlocked,
 * and something else might have inserted an element outside the iteration
 * range.  When this case occurs the iterator just keeps iterating until
 * it gets back into the iteration range (instead of asserting).
 *
 * NOTE!  EDEADLK *CANNOT* be returned by this procedure.
 */
int
hammer_btree_iterate(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	hammer_mount_t hmp;
	int error = 0;
	int r;
	int s;

	/*
	 * Skip past the current record
	 */
	hmp = cursor->trans->hmp;
	node = cursor->node->ondisk;
	if (node == NULL)
		return(ENOENT);
	if (cursor->index < node->count && 
	    (cursor->flags & HAMMER_CURSOR_ATEDISK)) {
		++cursor->index;
	}

	/*
	 * HAMMER can wind up being cpu-bound.
	 */
	if (++hmp->check_yield > hammer_yield_check) {
		hmp->check_yield = 0;
		lwkt_user_yield();
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
		++hammer_stats_btree_iterations;
		hammer_flusher_clean_loose_ios(hmp);

		if (cursor->index == node->count) {
			if (hammer_debug_btree) {
				kprintf("BRACKETU %016llx[%d] -> %016llx[%d] (td=%p)\n",
					(long long)cursor->node->node_offset,
					cursor->index,
					(long long)(cursor->parent ? cursor->parent->node_offset : -1),
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

			/*
			 * If we are reblocking we want to return internal
			 * nodes.  Note that the internal node will be
			 * returned multiple times, on each upward recursion
			 * from its children.  The caller selects which
			 * revisit it cares about (usually first or last only).
			 */
			if (cursor->flags & HAMMER_CURSOR_REBLOCKING) {
				cursor->flags |= HAMMER_CURSOR_ATEDISK;
				return(0);
			}
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
				kprintf("BRACKETL %016llx[%d] %016llx %02x "
					"key=%016llx lo=%02x %d (td=%p)\n",
					(long long)cursor->node->node_offset,
					cursor->index,
					(long long)elm[0].internal.base.obj_id,
					elm[0].internal.base.rec_type,
					(long long)elm[0].internal.base.key,
					elm[0].internal.base.localization,
					r,
					curthread
				);
				kprintf("BRACKETR %016llx[%d] %016llx %02x "
					"key=%016llx lo=%02x %d\n",
					(long long)cursor->node->node_offset,
					cursor->index + 1,
					(long long)elm[1].internal.base.obj_id,
					elm[1].internal.base.rec_type,
					(long long)elm[1].internal.base.key,
					elm[1].internal.base.localization,
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

			/*
			 * Better not be zero
			 */
			KKASSERT(elm->internal.subtree_offset != 0);

			if (s <= 0) {
				/*
				 * If running the mirror filter see if we
				 * can skip one or more entire sub-trees.
				 * If we can we return the internal node
				 * and the caller processes the skipped
				 * range (see mirror_read).
				 */
				if (cursor->flags &
				    HAMMER_CURSOR_MIRROR_FILTERED) {
					if (elm->internal.mirror_tid <
					    cursor->cmirror->mirror_tid) {
						hammer_cursor_mirror_filter(cursor);
						return(0);
					}
				}
			} else {
				/*
				 * Normally it would be impossible for the
				 * cursor to have gotten back-indexed,
				 * but it can happen if a node is deleted
				 * and the cursor is moved to its parent
				 * internal node.  ITERATE_CHECK will be set.
				 */
				KKASSERT(cursor->flags &
					 HAMMER_CURSOR_ITERATE_CHECK);
				kprintf("hammer_btree_iterate: "
					"DEBUG: Caught parent seek "
					"in internal iteration\n");
			}

			error = hammer_cursor_down(cursor);
			if (error)
				break;
			KKASSERT(cursor->index == 0);
			/* reload stale pointer */
			node = cursor->node->ondisk;
			continue;
		} else {
			elm = &node->elms[cursor->index];
			r = hammer_btree_cmp(&cursor->key_end, &elm->base);
			if (hammer_debug_btree) {
				kprintf("ELEMENT  %016llx:%d %c %016llx %02x "
					"key=%016llx lo=%02x %d\n",
					(long long)cursor->node->node_offset,
					cursor->index,
					(elm[0].leaf.base.btype ?
					 elm[0].leaf.base.btype : '?'),
					(long long)elm[0].leaf.base.obj_id,
					elm[0].leaf.base.rec_type,
					(long long)elm[0].leaf.base.key,
					elm[0].leaf.base.localization,
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

			/*
			 * If ITERATE_CHECK is set an unlocked cursor may
			 * have been moved to a parent and the iterate can
			 * happen upon elements that are not in the requested
			 * range.
			 */
			if (cursor->flags & HAMMER_CURSOR_ITERATE_CHECK) {
				s = hammer_btree_cmp(&cursor->key_beg,
						     &elm->base);
				if (s > 0) {
					kprintf("hammer_btree_iterate: "
						"DEBUG: Caught parent seek "
						"in leaf iteration\n");
					++cursor->index;
					continue;
				}
			}
			cursor->flags &= ~HAMMER_CURSOR_ITERATE_CHECK;

			/*
			 * Return the element
			 */
			switch(elm->leaf.base.btype) {
			case HAMMER_BTREE_TYPE_RECORD:
				if ((cursor->flags & HAMMER_CURSOR_ASOF) &&
				    hammer_btree_chkts(cursor->asof, &elm->base)) {
					++cursor->index;
					continue;
				}
				error = 0;
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error)
				break;
		}

		/*
		 * Return entry
		 */
		if (hammer_debug_btree) {
			int i = cursor->index;
			hammer_btree_elm_t elm = &cursor->node->ondisk->elms[i];
			kprintf("ITERATE  %p:%d %016llx %02x "
				"key=%016llx lo=%02x\n",
				cursor->node, i,
				(long long)elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				(long long)elm->internal.base.key,
				elm->internal.base.localization
			);
		}
		return(0);
	}
	return(error);
}

/*
 * We hit an internal element that we could skip as part of a mirroring
 * scan.  Calculate the entire range being skipped.
 *
 * It is important to include any gaps between the parent's left_bound
 * and the node's left_bound, and same goes for the right side.
 */
static void
hammer_cursor_mirror_filter(hammer_cursor_t cursor)
{
	struct hammer_cmirror *cmirror;
	hammer_node_ondisk_t ondisk;
	hammer_btree_elm_t elm;

	ondisk = cursor->node->ondisk;
	cmirror = cursor->cmirror;

	/*
	 * Calculate the skipped range
	 */
	elm = &ondisk->elms[cursor->index];
	if (cursor->index == 0)
		cmirror->skip_beg = *cursor->left_bound;
	else
		cmirror->skip_beg = elm->internal.base;
	while (cursor->index < ondisk->count) {
		if (elm->internal.mirror_tid >= cmirror->mirror_tid)
			break;
		++cursor->index;
		++elm;
	}
	if (cursor->index == ondisk->count)
		cmirror->skip_end = *cursor->right_bound;
	else
		cmirror->skip_end = elm->internal.base;

	/*
	 * clip the returned result.
	 */
	if (hammer_btree_cmp(&cmirror->skip_beg, &cursor->key_beg) < 0)
		cmirror->skip_beg = cursor->key_beg;
	if (hammer_btree_cmp(&cmirror->skip_end, &cursor->key_end) > 0)
		cmirror->skip_end = cursor->key_end;
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
	hammer_mount_t hmp;
	int error = 0;
	int r;
	int s;

	/* mirror filtering not supported for reverse iteration */
	KKASSERT ((cursor->flags & HAMMER_CURSOR_MIRROR_FILTERED) == 0);

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
	 * HAMMER can wind up being cpu-bound.
	 */
	hmp = cursor->trans->hmp;
	if (++hmp->check_yield > hammer_yield_check) {
		hmp->check_yield = 0;
		lwkt_user_yield();
	}

	/*
	 * Loop until an element is found or we are done.
	 */
	for (;;) {
		++hammer_stats_btree_iterations;
		hammer_flusher_clean_loose_ios(hmp);

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
				kprintf("BRACKETL %016llx[%d] %016llx %02x "
					"key=%016llx lo=%02x %d (td=%p)\n",
					(long long)cursor->node->node_offset,
					cursor->index,
					(long long)elm[0].internal.base.obj_id,
					elm[0].internal.base.rec_type,
					(long long)elm[0].internal.base.key,
					elm[0].internal.base.localization,
					r,
					curthread
				);
				kprintf("BRACKETR %016llx[%d] %016llx %02x "
					"key=%016llx lo=%02x %d\n",
					(long long)cursor->node->node_offset,
					cursor->index + 1,
					(long long)elm[1].internal.base.obj_id,
					elm[1].internal.base.rec_type,
					(long long)elm[1].internal.base.key,
					elm[1].internal.base.localization,
					s
				);
			}

			if (s >= 0) {
				error = ENOENT;
				break;
			}

			/*
			 * It shouldn't be possible to be seeked past key_end,
			 * even if the cursor got moved to a parent.
			 */
			KKASSERT(r >= 0);

			/*
			 * Better not be zero
			 */
			KKASSERT(elm->internal.subtree_offset != 0);

			error = hammer_cursor_down(cursor);
			if (error)
				break;
			KKASSERT(cursor->index == 0);
			/* reload stale pointer */
			node = cursor->node->ondisk;

			/* this can assign -1 if the leaf was empty */
			cursor->index = node->count - 1;
			continue;
		} else {
			elm = &node->elms[cursor->index];
			s = hammer_btree_cmp(&cursor->key_beg, &elm->base);
			if (hammer_debug_btree) {
				kprintf("ELEMENTR %016llx:%d %c %016llx %02x "
					"key=%016llx lo=%02x %d\n",
					(long long)cursor->node->node_offset,
					cursor->index,
					(elm[0].leaf.base.btype ?
					 elm[0].leaf.base.btype : '?'),
					(long long)elm[0].leaf.base.obj_id,
					elm[0].leaf.base.rec_type,
					(long long)elm[0].leaf.base.key,
					elm[0].leaf.base.localization,
					s
				);
			}
			if (s > 0) {
				error = ENOENT;
				break;
			}

			/*
			 * It shouldn't be possible to be seeked past key_end,
			 * even if the cursor got moved to a parent.
			 */
			cursor->flags &= ~HAMMER_CURSOR_ITERATE_CHECK;

			/*
			 * Return the element
			 */
			switch(elm->leaf.base.btype) {
			case HAMMER_BTREE_TYPE_RECORD:
				if ((cursor->flags & HAMMER_CURSOR_ASOF) &&
				    hammer_btree_chkts(cursor->asof, &elm->base)) {
					--cursor->index;
					continue;
				}
				error = 0;
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error)
				break;
		}

		/*
		 * Return entry
		 */
		if (hammer_debug_btree) {
			int i = cursor->index;
			hammer_btree_elm_t elm = &cursor->node->ondisk->elms[i];
			kprintf("ITERATER %p:%d %016llx %02x "
				"key=%016llx lo=%02x\n",
				cursor->node, i,
				(long long)elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				(long long)elm->internal.base.key,
				elm->internal.base.localization
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
 * AS-OF timestamp of 17).  Note that LEAF1 could be empty, requiring
 * further iterations.
 *
 * If this case occurs btree_search() will set HAMMER_CURSOR_CREATE_CHECK
 * and the cursor->create_check TID if an iteration might be needed.
 * In the above example create_check would be set to 14.
 */
int
hammer_btree_lookup(hammer_cursor_t cursor)
{
	int error;

	cursor->flags &= ~HAMMER_CURSOR_ITERATE_CHECK;
	KKASSERT ((cursor->flags & HAMMER_CURSOR_INSERT) == 0 ||
		  cursor->trans->sync_lock_refs > 0);
	++hammer_stats_btree_lookups;
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
					(long long)cursor->create_check);
			}
			cursor->key_beg.create_tid = cursor->create_check;
			/* loop */
		}
	} else {
		error = btree_search(cursor, 0);
	}
	if (error == 0)
		error = hammer_btree_extract(cursor, cursor->flags);
	return(error);
}

/*
 * Execute the logic required to start an iteration.  The first record
 * located within the specified range is returned and iteration control
 * flags are adjusted for successive hammer_btree_iterate() calls.
 *
 * Set ATEDISK so a low-level caller can call btree_first/btree_iterate
 * in a loop without worrying about it.  Higher-level merged searches will
 * adjust the flag appropriately.
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
 *
 * Set ATEDISK when iterating backwards to skip the current entry,
 * which after an ENOENT lookup will be pointing beyond our end point.
 *
 * Set ATEDISK so a low-level caller can call btree_last/btree_iterate_reverse
 * in a loop without worrying about it.  Higher-level merged searches will
 * adjust the flag appropriately.
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
		cursor->flags |= HAMMER_CURSOR_ATEDISK;
		error = hammer_btree_iterate_reverse(cursor);
	}
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	return(error);
}

/*
 * Extract the record and/or data associated with the cursor's current
 * position.  Any prior record or data stored in the cursor is replaced.
 *
 * NOTE: All extractions occur at the leaf of the B-Tree.
 */
int
hammer_btree_extract(hammer_cursor_t cursor, int flags)
{
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	hammer_off_t data_off;
	hammer_mount_t hmp;
	int32_t data_len;
	int error;

	/*
	 * The case where the data reference resolves to the same buffer
	 * as the record reference must be handled.
	 */
	node = cursor->node->ondisk;
	elm = &node->elms[cursor->index];
	cursor->data = NULL;
	hmp = cursor->node->hmp;

	/*
	 * There is nothing to extract for an internal element.
	 */
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL)
		return(EINVAL);

	/*
	 * Only record types have data.
	 */
	KKASSERT(node->type == HAMMER_BTREE_TYPE_LEAF);
	cursor->leaf = &elm->leaf;

	if ((flags & HAMMER_CURSOR_GET_DATA) == 0)
		return(0);
	if (elm->leaf.base.btype != HAMMER_BTREE_TYPE_RECORD)
		return(0);
	data_off = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	if (data_off == 0)
		return(0);

	/*
	 * Load the data
	 */
	KKASSERT(data_len >= 0 && data_len <= HAMMER_XBUFSIZE);
	cursor->data = hammer_bread_ext(hmp, data_off, data_len,
					&error, &cursor->data_buffer);

	/*
	 * Mark the data buffer as not being meta-data if it isn't
	 * meta-data (sometimes bulk data is accessed via a volume
	 * block device).
	 */
	if (error == 0) {
		switch(elm->leaf.base.rec_type) {
		case HAMMER_RECTYPE_DATA:
		case HAMMER_RECTYPE_DB:
			if ((data_off & HAMMER_ZONE_LARGE_DATA) == 0)
				break;
			if (hammer_double_buffer == 0 ||
			    (cursor->flags & HAMMER_CURSOR_NOSWAPCACHE)) {
				hammer_io_notmeta(cursor->data_buffer);
			}
			break;
		default:
			break;
		}
	}

	/*
	 * Deal with CRC errors on the extracted data.
	 */
	if (error == 0 &&
	    hammer_crc_test_leaf(cursor->data, &elm->leaf) == 0) {
		kprintf("CRC DATA @ %016llx/%d FAILED\n",
			(long long)elm->leaf.data_offset, elm->leaf.data_len);
		if (hammer_debug_critical)
			Debugger("CRC FAILED: DATA");
		if (cursor->trans->flags & HAMMER_TRANSF_CRCDOM)
			error = EDOM;	/* less critical (mirroring) */
		else
			error = EIO;	/* critical */
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
 * called. ENOSPC is returned if there is no room to insert a new record.
 *
 * The caller may depend on the cursor's exclusive lock after return to
 * interlock frontend visibility (see HAMMER_RECF_CONVERT_DELETE).
 */
int
hammer_btree_insert(hammer_cursor_t cursor, hammer_btree_leaf_elm_t elm,
		    int *doprop)
{
	hammer_node_ondisk_t node;
	int i;
	int error;

	*doprop = 0;
	if ((error = hammer_cursor_upgrade_node(cursor)) != 0)
		return(error);
	++hammer_stats_btree_inserts;

	/*
	 * Insert the element at the leaf node and update the count in the
	 * parent.  It is possible for parent to be NULL, indicating that
	 * the filesystem's ROOT B-Tree node is a leaf itself, which is
	 * possible.  The root inode can never be deleted so the leaf should
	 * never be empty.
	 *
	 * Remember that leaf nodes do not have boundaries.
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
	node->elms[i].leaf = *elm;
	++node->count;
	hammer_cursor_inserted_element(cursor->node, i);

	/*
	 * Update the leaf node's aggregate mirror_tid for mirroring
	 * support.
	 */
	if (node->mirror_tid < elm->base.delete_tid) {
		node->mirror_tid = elm->base.delete_tid;
		*doprop = 1;
	}
	if (node->mirror_tid < elm->base.create_tid) {
		node->mirror_tid = elm->base.create_tid;
		*doprop = 1;
	}
	hammer_modify_node_done(cursor->node);

	/*
	 * Debugging sanity checks.
	 */
	KKASSERT(hammer_btree_cmp(cursor->left_bound, &elm->base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound, &elm->base) > 0);
	if (i) {
		KKASSERT(hammer_btree_cmp(&node->elms[i-1].leaf.base, &elm->base) < 0);
	}
	if (i != node->count - 1)
		KKASSERT(hammer_btree_cmp(&node->elms[i+1].leaf.base, &elm->base) > 0);

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
 * direction, but will terminate rather then deadlock.  Empty internal nodes
 * are never allowed by a deletion which deadlocks may end up giving us an
 * empty leaf.  The pruner will clean up and rebalance the tree.
 *
 * This function can return EDEADLK, requiring the caller to retry the
 * operation after clearing the deadlock.
 */
int
hammer_btree_delete(hammer_cursor_t cursor)
{
	hammer_node_ondisk_t ondisk;
	hammer_node_t node;
	hammer_node_t parent __debugvar;
	int error;
	int i;

	KKASSERT (cursor->trans->sync_lock_refs > 0);
	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		return(error);
	++hammer_stats_btree_deletes;

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
	hammer_cursor_deleted_element(node, i);

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
	 * was unable to recurse and had to leave us with an empty leaf. 
	 */
	KKASSERT(cursor->index <= ondisk->count);
	if (ondisk->count == 0) {
		error = btree_remove(cursor);
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
 * PRIMARY B-TREE SEARCH SUPPORT PROCEDURE
 *
 * Search the filesystem B-Tree for cursor->key_beg, return the matching node.
 *
 * The search can begin ANYWHERE in the B-Tree.  As a first step the search
 * iterates up the tree as necessary to properly position itself prior to
 * actually doing the sarch.
 * 
 * INSERTIONS: The search will split full nodes and leaves on its way down
 * and guarentee that the leaf it ends up on is not full.  If we run out
 * of space the search continues to the leaf, but ENOSPC is returned.
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
	++hammer_stats_btree_searches;

	if (hammer_debug_btree) {
		kprintf("SEARCH   %016llx[%d] %016llx %02x key=%016llx cre=%016llx lo=%02x (td=%p)\n",
			(long long)cursor->node->node_offset,
			cursor->index,
			(long long)cursor->key_beg.obj_id,
			cursor->key_beg.rec_type,
			(long long)cursor->key_beg.key,
			(long long)cursor->key_beg.create_tid,
			cursor->key_beg.localization, 
			curthread
		);
		if (cursor->parent)
		    kprintf("SEARCHP  %016llx[%d] (%016llx/%016llx %016llx/%016llx) (%p/%p %p/%p)\n",
			(long long)cursor->parent->node_offset,
			cursor->parent_index,
			(long long)cursor->left_bound->obj_id,
			(long long)cursor->parent->ondisk->elms[cursor->parent_index].internal.base.obj_id,
			(long long)cursor->right_bound->obj_id,
			(long long)cursor->parent->ondisk->elms[cursor->parent_index+1].internal.base.obj_id,
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
		++hammer_stats_btree_iterations;
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
	while (flags & HAMMER_CURSOR_INSERT) {
		if (btree_node_is_full(cursor->node->ondisk) == 0)
			break;
		if (cursor->node->ondisk->parent == 0 ||
		    cursor->parent->ondisk->count != HAMMER_BTREE_INT_ELMS) {
			break;
		}
		++hammer_stats_btree_iterations;
		error = hammer_cursor_up(cursor);
		/* node may have become stale */
		if (error)
			goto done;
	}

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
		++hammer_stats_btree_iterations;
		if (hammer_debug_btree) {
			kprintf("SEARCH-I %016llx count=%d\n",
				(long long)cursor->node->node_offset,
				node->count);
		}

		/*
		 * Try to shortcut the search before dropping into the
		 * linear loop.  Locate the first node where r <= 1.
		 */
		i = hammer_btree_search_node(&cursor->key_beg, node);
		while (i <= node->count) {
			++hammer_stats_btree_elements;
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
			++i;
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
			if ((flags & (HAMMER_CURSOR_INSERT |
				      HAMMER_CURSOR_PRUNING)) == 0) {
				cursor->index = 0;
				return(ENOENT);
			}

			/*
			 * Correct a left-hand boundary mismatch.
			 *
			 * We can only do this if we can upgrade the lock,
			 * and synchronized as a background cursor (i.e.
			 * inserting or pruning).
			 *
			 * WARNING: We can only do this if inserting, i.e.
			 * we are running on the backend.
			 */
			if ((error = hammer_cursor_upgrade(cursor)) != 0)
				return(error);
			KKASSERT(cursor->flags & HAMMER_CURSOR_BACKEND);
			hammer_modify_node_field(cursor->trans, cursor->node,
						 elms[0]);
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
			if ((flags & (HAMMER_CURSOR_INSERT |
				      HAMMER_CURSOR_PRUNING)) == 0) {
				cursor->index = i;
				return (ENOENT);
			}

			/*
			 * Correct a right-hand boundary mismatch.
			 * (actual push-down record is i-2 prior to
			 * adjustments to i).
			 *
			 * We can only do this if we can upgrade the lock,
			 * and synchronized as a background cursor (i.e.
			 * inserting or pruning).
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
				"key=%016llx cre=%016llx lo=%02x\n",
				(long long)cursor->node->node_offset,
				i,
				(long long)elm->internal.base.obj_id,
				elm->internal.base.rec_type,
				(long long)elm->internal.base.key,
				(long long)elm->internal.base.create_tid,
				elm->internal.base.localization
			);
		}

		/*
		 * We better have a valid subtree offset.
		 */
		KKASSERT(elm->internal.subtree_offset != 0);

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
		 * If we run out of space we set enospc but continue on
		 * to a leaf.
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
	 * On success the index is set to the matching element and 0
	 * is returned.
	 *
	 * On failure the index is set to the insertion point and ENOENT
	 * is returned.
	 *
	 * Boundaries are not stored in leaf nodes, so the index can wind
	 * up to the left of element 0 (index == 0) or past the end of
	 * the array (index == node->count).  It is also possible that the
	 * leaf might be empty.
	 */
	++hammer_stats_btree_iterations;
	KKASSERT (node->type == HAMMER_BTREE_TYPE_LEAF);
	KKASSERT(node->count <= HAMMER_BTREE_LEAF_ELMS);
	if (hammer_debug_btree) {
		kprintf("SEARCH-L %016llx count=%d\n",
			(long long)cursor->node->node_offset,
			node->count);
	}

	/*
	 * Try to shortcut the search before dropping into the
	 * linear loop.  Locate the first node where r <= 1.
	 */
	i = hammer_btree_search_node(&cursor->key_beg, node);
	while (i < node->count) {
		++hammer_stats_btree_elements;
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
		if (r > 1) {
			++i;
			continue;
		}

		/*
		 * Check our as-of timestamp against the element.
		 */
		if (flags & HAMMER_CURSOR_ASOF) {
			if (hammer_btree_chkts(cursor->asof,
					       &node->elms[i].base) != 0) {
				++i;
				continue;
			}
			/* success */
		} else {
			if (r > 0) {	/* can only be +1 */
				++i;
				continue;
			}
			/* success */
		}
		cursor->index = i;
		error = 0;
		if (hammer_debug_btree) {
			kprintf("RESULT-L %016llx[%d] (SUCCESS)\n",
				(long long)cursor->node->node_offset, i);
		}
		goto done;
	}

	/*
	 * The search of the leaf node failed.  i is the insertion point.
	 */
failed:
	if (hammer_debug_btree) {
		kprintf("RESULT-L %016llx[%d] (FAILED)\n",
			(long long)cursor->node->node_offset, i);
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
	 * (ENOENT) or unable to insert (ENOSPC).
	 */
	error = enospc ? ENOSPC : ENOENT;
done:
	return(error);
}

/*
 * Heuristical search for the first element whos comparison is <= 1.  May
 * return an index whos compare result is > 1 but may only return an index
 * whos compare result is <= 1 if it is the first element with that result.
 */
int
hammer_btree_search_node(hammer_base_elm_t elm, hammer_node_ondisk_t node)
{
	int b;
	int s;
	int i;
	int r;

	/*
	 * Don't bother if the node does not have very many elements
	 */
	b = 0;
	s = node->count;
	while (s - b > 4) {
		i = b + (s - b) / 2;
		++hammer_stats_btree_elements;
		r = hammer_btree_cmp(elm, &node->elms[i].leaf.base);
		if (r <= 1) {
			s = i;
		} else {
			b = i;
		}
	}
	return(b);
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
	struct hammer_node_lock lockroot;
	hammer_mount_t hmp = cursor->trans->hmp;
	int parent_index;
	int made_root;
	int split;
	int error;
	int i;
	const int esize = sizeof(*elm);

	hammer_node_lock_init(&lockroot, cursor->node);
	error = hammer_btree_lock_children(cursor, 1, &lockroot, NULL);
	if (error)
		goto done;
	if ((error = hammer_cursor_upgrade(cursor)) != 0)
		goto done;
	++hammer_stats_btree_splits;

	/* 
	 * Calculate the split point.  If the insertion point is at the
	 * end of the leaf we adjust the split point significantly to the
	 * right to try to optimize node fill and flag it.  If we hit
	 * that same leaf again our heuristic failed and we don't try
	 * to optimize node fill (it could lead to a degenerate case).
	 */
	node = cursor->node;
	ondisk = node->ondisk;
	KKASSERT(ondisk->count > 4);
	if (cursor->index == ondisk->count &&
	    (node->flags & HAMMER_NODE_NONLINEAR) == 0) {
		split = (ondisk->count + 1) * 3 / 4;
		node->flags |= HAMMER_NODE_NONLINEAR;
	} else {
		/*
		 * We are splitting but elms[split] will be promoted to
		 * the parent, leaving the right hand node with one less
		 * element.  If the insertion point will be on the
		 * left-hand side adjust the split point to give the
		 * right hand side one additional node.
		 */
		split = (ondisk->count + 1) / 2;
		if (cursor->index <= split)
			--split;
	}

	/*
	 * If we are at the root of the filesystem, create a new root node
	 * with 1 element and split normally.  Avoid making major
	 * modifications until we know the whole operation will work.
	 */
	if (ondisk->parent == 0) {
		parent = hammer_alloc_btree(cursor->trans, 0, &error);
		if (parent == NULL)
			goto done;
		hammer_lock_ex(&parent->lock);
		hammer_modify_node_noundo(cursor->trans, parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->mirror_tid = node->ondisk->mirror_tid;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = hmp->root_btree_beg;
		ondisk->elms[0].base.btype = node->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = node->node_offset;
		ondisk->elms[0].internal.mirror_tid = ondisk->mirror_tid;
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
	 *  B O O O P N N B	<-- P = node->elms[split] (index 4)
	 *   0 1 2 3 4 5 6	<-- subtree indices
	 *
	 *       x x P x x
	 *        s S S s  
	 *         /   \
	 *  B O O O B    B N N B	<--- inner boundary points are 'P'
	 *   0 1 2 3      4 5 6  
	 */
	new_node = hammer_alloc_btree(cursor->trans, 0, &error);
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
	new_node->ondisk->mirror_tid = ondisk->mirror_tid;
	KKASSERT(ondisk->type == new_node->ondisk->type);
	hammer_cursor_split_node(node, new_node, split);

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
	parent_elm->internal.mirror_tid = new_node->ondisk->mirror_tid;
	++ondisk->count;
	hammer_modify_node_done(parent);
	hammer_cursor_inserted_element(parent, parent_index + 1);

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
	hammer_btree_unlock_children(cursor->trans->hmp, &lockroot, NULL);
	hammer_cursor_downgrade(cursor);
	return (error);
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
	++hammer_stats_btree_splits;

	KKASSERT(hammer_btree_cmp(cursor->left_bound,
		 &cursor->node->ondisk->elms[0].leaf.base) <= 0);
	KKASSERT(hammer_btree_cmp(cursor->right_bound,
		 &cursor->node->ondisk->elms[cursor->node->ondisk->count-1].leaf.base) > 0);

	/* 
	 * Calculate the split point.  If the insertion point is at the
	 * end of the leaf we adjust the split point significantly to the
	 * right to try to optimize node fill and flag it.  If we hit
	 * that same leaf again our heuristic failed and we don't try
	 * to optimize node fill (it could lead to a degenerate case).
	 */
	leaf = cursor->node;
	ondisk = leaf->ondisk;
	KKASSERT(ondisk->count > 4);
	if (cursor->index == ondisk->count &&
	    (leaf->flags & HAMMER_NODE_NONLINEAR) == 0) {
		split = (ondisk->count + 1) * 3 / 4;
		leaf->flags |= HAMMER_NODE_NONLINEAR;
	} else {
		split = (ondisk->count + 1) / 2;
	}

#if 0
	/*
	 * If the insertion point is at the split point shift the
	 * split point left so we don't have to worry about
	 */
	if (cursor->index == split)
		--split;
#endif
	KKASSERT(split > 0 && split < ondisk->count);

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
		parent = hammer_alloc_btree(cursor->trans, 0, &error);
		if (parent == NULL)
			goto done;
		hammer_lock_ex(&parent->lock);
		hammer_modify_node_noundo(cursor->trans, parent);
		ondisk = parent->ondisk;
		ondisk->count = 1;
		ondisk->parent = 0;
		ondisk->mirror_tid = leaf->ondisk->mirror_tid;
		ondisk->type = HAMMER_BTREE_TYPE_INTERNAL;
		ondisk->elms[0].base = hmp->root_btree_beg;
		ondisk->elms[0].base.btype = leaf->ondisk->type;
		ondisk->elms[0].internal.subtree_offset = leaf->node_offset;
		ondisk->elms[0].internal.mirror_tid = ondisk->mirror_tid;
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
	new_leaf = hammer_alloc_btree(cursor->trans, 0, &error);
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
	new_leaf->ondisk->mirror_tid = ondisk->mirror_tid;
	KKASSERT(ondisk->type == new_leaf->ondisk->type);
	hammer_modify_node_done(new_leaf);
	hammer_cursor_split_node(leaf, new_leaf, split);

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
	parent_elm->internal.mirror_tid = new_leaf->ondisk->mirror_tid;
	mid_boundary = &parent_elm->base;
	++ondisk->count;
	hammer_modify_node_done(parent);
	hammer_cursor_inserted_element(parent, parent_index + 1);

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

#if 0

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
	struct hammer_mount *hmp;
	struct hammer_rhb_list rhb_list;
	hammer_base_elm_t elm;
	hammer_node_t orig_node;
	struct hammer_rhb *rhb;
	int orig_index;
	int error;

	TAILQ_INIT(&rhb_list);
	hmp = cursor->trans->hmp;

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

		rhb = kmalloc(sizeof(*rhb), hmp->m_misc, M_WAITOK|M_ZERO);
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
		if (error)
			break;
		TAILQ_REMOVE(&rhb_list, rhb, entry);
		hammer_unlock(&rhb->node->lock);
		hammer_rel_node(rhb->node);
		kfree(rhb, hmp->m_misc);

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
		kfree(rhb, hmp->m_misc);
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
	struct hammer_mount *hmp;
	int error;

	TAILQ_INIT(&rhb_list);
	hmp = cursor->trans->hmp;

	cmp = &cursor->node->ondisk->elms[cursor->index].base;

	/*
	 * Record the node and traverse down the left-hand side for all
	 * matching records needing a boundary correction.
	 */
	error = 0;
	for (;;) {
		rhb = kmalloc(sizeof(*rhb), hmp->m_misc, M_WAITOK|M_ZERO);
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
		kfree(rhb, hmp->m_misc);

		elm = &cursor->node->ondisk->elms[cursor->index].base;
		if (cursor->node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			hammer_modify_node(cursor->trans, cursor->node,
					   &elm->create_tid,
					   sizeof(elm->create_tid));
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
		kfree(rhb, hmp->m_misc);
	}
	return (error);
}

#endif

/*
 * Attempt to remove the locked, empty or want-to-be-empty B-Tree node at
 * (cursor->node).  Returns 0 on success, EDEADLK if we could not complete
 * the operation due to a deadlock, or some other error.
 *
 * This routine is initially called with an empty leaf and may be
 * recursively called with single-element internal nodes.
 *
 * It should also be noted that when removing empty leaves we must be sure
 * to test and update mirror_tid because another thread may have deadlocked
 * against us (or someone) trying to propagate it up and cannot retry once
 * the node has been deleted.
 *
 * On return the cursor may end up pointing to an internal node, suitable
 * for further iteration but not for an immediate insertion or deletion.
 */
static int
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
	ondisk = node->ondisk;
	if (ondisk->parent == 0) {
		KKASSERT(cursor->parent == NULL);
		hammer_modify_node_all(cursor->trans, node);
		KKASSERT(ondisk == node->ondisk);
		ondisk->type = HAMMER_BTREE_TYPE_LEAF;
		ondisk->count = 0;
		hammer_modify_node_done(node);
		cursor->index = 0;
		return(0);
	}

	parent = cursor->parent;

	/*
	 * Attempt to remove the parent's reference to the child.  If the
	 * parent would become empty we have to recurse.  If we fail we 
	 * leave the parent pointing to an empty leaf node.
	 *
	 * We have to recurse successfully before we can delete the internal
	 * node as it is illegal to have empty internal nodes.  Even though
	 * the operation may be aborted we must still fixup any unlocked
	 * cursors as if we had deleted the element prior to recursing
	 * (by calling hammer_cursor_deleted_element()) so those cursors
	 * are properly forced up the chain by the recursion.
	 */
	if (parent->ondisk->count == 1) {
		/*
		 * This special cursor_up_locked() call leaves the original
		 * node exclusively locked and referenced, leaves the
		 * original parent locked (as the new node), and locks the
		 * new parent.  It can return EDEADLK.
		 *
		 * We cannot call hammer_cursor_removed_node() until we are
		 * actually able to remove the node.  If we did then tracked
		 * cursors in the middle of iterations could be repointed
		 * to a parent node.  If this occurs they could end up
		 * scanning newly inserted records into the node (that could
		 * not be deleted) when they push down again.
		 *
		 * Due to the way the recursion works the final parent is left
		 * in cursor->parent after the recursion returns.  Each
		 * layer on the way back up is thus able to call
		 * hammer_cursor_removed_node() and 'jump' the node up to
		 * the (same) final parent.
		 *
		 * NOTE!  The local variable 'parent' is invalid after we
		 *	  call hammer_cursor_up_locked().
		 */
		error = hammer_cursor_up_locked(cursor);
		parent = NULL;

		if (error == 0) {
			hammer_cursor_deleted_element(cursor->node, 0);
			error = btree_remove(cursor);
			if (error == 0) {
				KKASSERT(node != cursor->node);
				hammer_cursor_removed_node(
					node, cursor->node,
					cursor->index);
				hammer_modify_node_all(cursor->trans, node);
				ondisk = node->ondisk;
				ondisk->type = HAMMER_BTREE_TYPE_DELETED;
				ondisk->count = 0;
				hammer_modify_node_done(node);
				hammer_flush_node(node, 0);
				hammer_delete_node(cursor->trans, node);
			} else {
				/*
				 * Defer parent removal because we could not
				 * get the lock, just let the leaf remain
				 * empty.
				 */
				/**/
			}
			hammer_unlock(&node->lock);
			hammer_rel_node(node);
		} else {
			/*
			 * Defer parent removal because we could not
			 * get the lock, just let the leaf remain
			 * empty.
			 */
			/**/
		}
	} else {
		KKASSERT(parent->ondisk->count > 1);

		hammer_modify_node_all(cursor->trans, parent);
		ondisk = parent->ondisk;
		KKASSERT(ondisk->type == HAMMER_BTREE_TYPE_INTERNAL);

		elm = &ondisk->elms[cursor->parent_index];
		KKASSERT(elm->internal.subtree_offset == node->node_offset);
		KKASSERT(ondisk->count > 0);

		/*
		 * We must retain the highest mirror_tid.  The deleted
		 * range is now encompassed by the element to the left.
		 * If we are already at the left edge the new left edge
		 * inherits mirror_tid.
		 *
		 * Note that bounds of the parent to our parent may create
		 * a gap to the left of our left-most node or to the right
		 * of our right-most node.  The gap is silently included
		 * in the mirror_tid's area of effect from the point of view
		 * of the scan.
		 */
		if (cursor->parent_index) {
			if (elm[-1].internal.mirror_tid <
			    elm[0].internal.mirror_tid) {
				elm[-1].internal.mirror_tid =
				    elm[0].internal.mirror_tid;
			}
		} else {
			if (elm[1].internal.mirror_tid <
			    elm[0].internal.mirror_tid) {
				elm[1].internal.mirror_tid =
				    elm[0].internal.mirror_tid;
			}
		}

		/*
		 * Delete the subtree reference in the parent.  Include
		 * boundary element at end.
		 */
		bcopy(&elm[1], &elm[0],
		      (ondisk->count - cursor->parent_index) * esize);
		--ondisk->count;
		hammer_modify_node_done(parent);
		hammer_cursor_removed_node(node, parent, cursor->parent_index);
		hammer_cursor_deleted_element(parent, cursor->parent_index);
		hammer_flush_node(node, 0);
		hammer_delete_node(cursor->trans, node);

		/*
		 * cursor->node is invalid, cursor up to make the cursor
		 * valid again.  We have to flag the condition in case
		 * another thread wiggles an insertion in during an
		 * iteration.
		 */
		cursor->flags |= HAMMER_CURSOR_ITERATE_CHECK;
		error = hammer_cursor_up(cursor);
	}
	return (error);
}

/*
 * Propagate cursor->trans->tid up the B-Tree starting at the current
 * cursor position using pseudofs info gleaned from the passed inode.
 *
 * The passed inode has no relationship to the cursor position other
 * then being in the same pseudofs as the insertion or deletion we
 * are propagating the mirror_tid for.
 *
 * WARNING!  Because we push and pop the passed cursor, it may be
 *	     modified by other B-Tree operations while it is unlocked
 *	     and things like the node & leaf pointers, and indexes might
 *	     change.
 */
void
hammer_btree_do_propagation(hammer_cursor_t cursor,
			    hammer_pseudofs_inmem_t pfsm,
			    hammer_btree_leaf_elm_t leaf)
{
	hammer_cursor_t ncursor;
	hammer_tid_t mirror_tid;
	int error __debugvar;

	/*
	 * We do not propagate a mirror_tid if the filesystem was mounted
	 * in no-mirror mode.
	 */
	if (cursor->trans->hmp->master_id < 0)
		return;

	/*
	 * This is a bit of a hack because we cannot deadlock or return
	 * EDEADLK here.  The related operation has already completed and
	 * we must propagate the mirror_tid now regardless.
	 *
	 * Generate a new cursor which inherits the original's locks and
	 * unlock the original.  Use the new cursor to propagate the
	 * mirror_tid.  Then clean up the new cursor and reacquire locks
	 * on the original.
	 *
	 * hammer_dup_cursor() cannot dup locks.  The dup inherits the
	 * original's locks and the original is tracked and must be
	 * re-locked.
	 */
	mirror_tid = cursor->node->ondisk->mirror_tid;
	KKASSERT(mirror_tid != 0);
	ncursor = hammer_push_cursor(cursor);
	error = hammer_btree_mirror_propagate(ncursor, mirror_tid);
	KKASSERT(error == 0);
	hammer_pop_cursor(cursor, ncursor);
	/* WARNING: cursor's leaf pointer may change after pop */
}


/*
 * Propagate a mirror TID update upwards through the B-Tree to the root.
 *
 * A locked internal node must be passed in.  The node will remain locked
 * on return.
 *
 * This function syncs mirror_tid at the specified internal node's element,
 * adjusts the node's aggregation mirror_tid, and then recurses upwards.
 */
static int
hammer_btree_mirror_propagate(hammer_cursor_t cursor, hammer_tid_t mirror_tid)
{
	hammer_btree_internal_elm_t elm;
	hammer_node_t node;
	int error;

	for (;;) {
		error = hammer_cursor_up(cursor);
		if (error == 0)
			error = hammer_cursor_upgrade(cursor);

		/*
		 * We can ignore HAMMER_CURSOR_ITERATE_CHECK, the
		 * cursor will still be properly positioned for
		 * mirror propagation, just not for iterations.
		 */
		while (error == EDEADLK) {
			hammer_recover_cursor(cursor);
			error = hammer_cursor_upgrade(cursor);
		}
		if (error)
			break;

		/*
		 * If the cursor deadlocked it could end up at a leaf
		 * after we lost the lock.
		 */
		node = cursor->node;
		if (node->ondisk->type != HAMMER_BTREE_TYPE_INTERNAL)
			continue;

		/*
		 * Adjust the node's element
		 */
		elm = &node->ondisk->elms[cursor->index].internal;
		if (elm->mirror_tid >= mirror_tid)
			break;
		hammer_modify_node(cursor->trans, node, &elm->mirror_tid,
				   sizeof(elm->mirror_tid));
		elm->mirror_tid = mirror_tid;
		hammer_modify_node_done(node);
		if (hammer_debug_general & 0x0002) {
			kprintf("mirror_propagate: propagate "
				"%016llx @%016llx:%d\n",
				(long long)mirror_tid,
				(long long)node->node_offset,
				cursor->index);
		}


		/*
		 * Adjust the node's mirror_tid aggregator
		 */
		if (node->ondisk->mirror_tid >= mirror_tid)
			return(0);
		hammer_modify_node_field(cursor->trans, node, mirror_tid);
		node->ondisk->mirror_tid = mirror_tid;
		hammer_modify_node_done(node);
		if (hammer_debug_general & 0x0002) {
			kprintf("mirror_propagate: propagate "
				"%016llx @%016llx\n",
				(long long)mirror_tid,
				(long long)node->node_offset);
		}
	}
	if (error == ENOENT)
		error = 0;
	return(error);
}

hammer_node_t
hammer_btree_get_parent(hammer_transaction_t trans, hammer_node_t node,
			int *parent_indexp, int *errorp, int try_exclusive)
{
	hammer_node_t parent;
	hammer_btree_elm_t elm;
	int i;

	/*
	 * Get the node
	 */
	parent = hammer_get_node(trans, node->ondisk->parent, 0, errorp);
	if (*errorp) {
		KKASSERT(parent == NULL);
		return(NULL);
	}
	KKASSERT ((parent->flags & HAMMER_NODE_DELETED) == 0);

	/*
	 * Lock the node
	 */
	if (try_exclusive) {
		if (hammer_lock_ex_try(&parent->lock)) {
			hammer_rel_node(parent);
			*errorp = EDEADLK;
			return(NULL);
		}
	} else {
		hammer_lock_sh(&parent->lock);
	}

	/*
	 * Figure out which element in the parent is pointing to the
	 * child.
	 */
	if (node->ondisk->count) {
		i = hammer_btree_search_node(&node->ondisk->elms[0].base,
					     parent->ondisk);
	} else {
		i = 0;
	}
	while (i < parent->ondisk->count) {
		elm = &parent->ondisk->elms[i];
		if (elm->internal.subtree_offset == node->node_offset)
			break;
		++i;
	}
	if (i == parent->ondisk->count) {
		hammer_unlock(&parent->lock);
		panic("Bad B-Tree link: parent %p node %p", parent, node);
	}
	*parent_indexp = i;
	KKASSERT(*errorp == 0);
	return(parent);
}

/*
 * The element (elm) has been moved to a new internal node (node).
 *
 * If the element represents a pointer to an internal node that node's
 * parent must be adjusted to the element's new location.
 *
 * XXX deadlock potential here with our exclusive locks
 */
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
		child = hammer_get_node(trans, elm->internal.subtree_offset,
					0, &error);
		if (error == 0) {
			hammer_modify_node_field(trans, child, parent);
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
 * Initialize the root of a recursive B-Tree node lock list structure.
 */
void
hammer_node_lock_init(hammer_node_lock_t parent, hammer_node_t node)
{
	TAILQ_INIT(&parent->list);
	parent->parent = NULL;
	parent->node = node;
	parent->index = -1;
	parent->count = node->ondisk->count;
	parent->copy = NULL;
	parent->flags = 0;
}

/*
 * Initialize a cache of hammer_node_lock's including space allocated
 * for node copies.
 *
 * This is used by the rebalancing code to preallocate the copy space
 * for ~4096 B-Tree nodes (16MB of data) prior to acquiring any HAMMER
 * locks, otherwise we can blow out the pageout daemon's emergency
 * reserve and deadlock it.
 *
 * NOTE: HAMMER_NODE_LOCK_LCACHE is not set on items cached in the lcache.
 *	 The flag is set when the item is pulled off the cache for use.
 */
void
hammer_btree_lcache_init(hammer_mount_t hmp, hammer_node_lock_t lcache,
			 int depth)
{
	hammer_node_lock_t item;
	int count;

	for (count = 1; depth; --depth)
		count *= HAMMER_BTREE_LEAF_ELMS;
	bzero(lcache, sizeof(*lcache));
	TAILQ_INIT(&lcache->list);
	while (count) {
		item = kmalloc(sizeof(*item), hmp->m_misc, M_WAITOK|M_ZERO);
		item->copy = kmalloc(sizeof(*item->copy),
				     hmp->m_misc, M_WAITOK);
		TAILQ_INIT(&item->list);
		TAILQ_INSERT_TAIL(&lcache->list, item, entry);
		--count;
	}
}

void
hammer_btree_lcache_free(hammer_mount_t hmp, hammer_node_lock_t lcache)
{
	hammer_node_lock_t item;

	while ((item = TAILQ_FIRST(&lcache->list)) != NULL) {
		TAILQ_REMOVE(&lcache->list, item, entry);
		KKASSERT(item->copy);
		KKASSERT(TAILQ_EMPTY(&item->list));
		kfree(item->copy, hmp->m_misc);
		kfree(item, hmp->m_misc);
	}
	KKASSERT(lcache->copy == NULL);
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
 *
 * The caller is responsible for managing parent->node, the root's node
 * is usually aliased from a cursor.
 */
int
hammer_btree_lock_children(hammer_cursor_t cursor, int depth,
			   hammer_node_lock_t parent,
			   hammer_node_lock_t lcache)
{
	hammer_node_t node;
	hammer_node_lock_t item;
	hammer_node_ondisk_t ondisk;
	hammer_btree_elm_t elm;
	hammer_node_t child;
	struct hammer_mount *hmp;
	int error;
	int i;

	node = parent->node;
	ondisk = node->ondisk;
	error = 0;
	hmp = cursor->trans->hmp;

	/*
	 * We really do not want to block on I/O with exclusive locks held,
	 * pre-get the children before trying to lock the mess.  This is
	 * only done one-level deep for now.
	 */
	for (i = 0; i < ondisk->count; ++i) {
		++hammer_stats_btree_elements;
		elm = &ondisk->elms[i];
		if (elm->base.btype != HAMMER_BTREE_TYPE_LEAF &&
		    elm->base.btype != HAMMER_BTREE_TYPE_INTERNAL) {
			continue;
		}
		child = hammer_get_node(cursor->trans,
					elm->internal.subtree_offset,
					0, &error);
		if (child)
			hammer_rel_node(child);
	}

	/*
	 * Do it for real
	 */
	for (i = 0; error == 0 && i < ondisk->count; ++i) {
		++hammer_stats_btree_elements;
		elm = &ondisk->elms[i];

		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_INTERNAL:
		case HAMMER_BTREE_TYPE_LEAF:
			KKASSERT(elm->internal.subtree_offset != 0);
			child = hammer_get_node(cursor->trans,
						elm->internal.subtree_offset,
						0, &error);
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
				if (lcache) {
					item = TAILQ_FIRST(&lcache->list);
					KKASSERT(item != NULL);
					item->flags |= HAMMER_NODE_LOCK_LCACHE;
					TAILQ_REMOVE(&lcache->list,
						     item, entry);
				} else {
					item = kmalloc(sizeof(*item),
						       hmp->m_misc,
						       M_WAITOK|M_ZERO);
					TAILQ_INIT(&item->list);
				}

				TAILQ_INSERT_TAIL(&parent->list, item, entry);
				item->parent = parent;
				item->node = child;
				item->index = i;
				item->count = child->ondisk->count;

				/*
				 * Recurse (used by the rebalancing code)
				 */
				if (depth > 1 && elm->base.btype == HAMMER_BTREE_TYPE_INTERNAL) {
					error = hammer_btree_lock_children(
							cursor,
							depth - 1,
							item,
							lcache);
				}
			}
		}
	}
	if (error)
		hammer_btree_unlock_children(hmp, parent, lcache);
	return(error);
}

/*
 * Create an in-memory copy of all B-Tree nodes listed, recursively,
 * including the parent.
 */
void
hammer_btree_lock_copy(hammer_cursor_t cursor, hammer_node_lock_t parent)
{
	hammer_mount_t hmp = cursor->trans->hmp;
	hammer_node_lock_t item;

	if (parent->copy == NULL) {
		KKASSERT((parent->flags & HAMMER_NODE_LOCK_LCACHE) == 0);
		parent->copy = kmalloc(sizeof(*parent->copy),
				       hmp->m_misc, M_WAITOK);
	}
	KKASSERT((parent->flags & HAMMER_NODE_LOCK_UPDATED) == 0);
	*parent->copy = *parent->node->ondisk;
	TAILQ_FOREACH(item, &parent->list, entry) {
		hammer_btree_lock_copy(cursor, item);
	}
}

/*
 * Recursively sync modified copies to the media.
 */
int
hammer_btree_sync_copy(hammer_cursor_t cursor, hammer_node_lock_t parent)
{
	hammer_node_lock_t item;
	int count = 0;

	if (parent->flags & HAMMER_NODE_LOCK_UPDATED) {
		++count;
		hammer_modify_node_all(cursor->trans, parent->node);
		*parent->node->ondisk = *parent->copy;
                hammer_modify_node_done(parent->node);
		if (parent->copy->type == HAMMER_BTREE_TYPE_DELETED) {
			hammer_flush_node(parent->node, 0);
			hammer_delete_node(cursor->trans, parent->node);
		}
	}
	TAILQ_FOREACH(item, &parent->list, entry) {
		count += hammer_btree_sync_copy(cursor, item);
	}
	return(count);
}

/*
 * Release previously obtained node locks.  The caller is responsible for
 * cleaning up parent->node itself (its usually just aliased from a cursor),
 * but this function will take care of the copies.
 *
 * NOTE: The root node is not placed in the lcache and node->copy is not
 *	 deallocated when lcache != NULL.
 */
void
hammer_btree_unlock_children(hammer_mount_t hmp, hammer_node_lock_t parent,
			     hammer_node_lock_t lcache)
{
	hammer_node_lock_t item;
	hammer_node_ondisk_t copy;

	while ((item = TAILQ_FIRST(&parent->list)) != NULL) {
		TAILQ_REMOVE(&parent->list, item, entry);
		hammer_btree_unlock_children(hmp, item, lcache);
		hammer_unlock(&item->node->lock);
		hammer_rel_node(item->node);
		if (lcache) {
			/*
			 * NOTE: When placing the item back in the lcache
			 *	 the flag is cleared by the bzero().
			 *	 Remaining fields are cleared as a safety
			 *	 measure.
			 */
			KKASSERT(item->flags & HAMMER_NODE_LOCK_LCACHE);
			KKASSERT(TAILQ_EMPTY(&item->list));
			copy = item->copy;
			bzero(item, sizeof(*item));
			TAILQ_INIT(&item->list);
			item->copy = copy;
			if (copy)
				bzero(copy, sizeof(*copy));
			TAILQ_INSERT_TAIL(&lcache->list, item, entry);
		} else {
			kfree(item, hmp->m_misc);
		}
	}
	if (parent->copy && (parent->flags & HAMMER_NODE_LOCK_LCACHE) == 0) {
		kfree(parent->copy, hmp->m_misc);
		parent->copy = NULL;	/* safety */
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
	if (key1->localization < key2->localization)
		return(-5);
	if (key1->localization > key2->localization)
		return(5);

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
	dest->obj_id = key2->obj_id;
	dest->create_tid = key2->create_tid;

	MAKE_SEPARATOR(key1, key2, dest, localization);
	if (key1->localization == key2->localization) {
		MAKE_SEPARATOR(key1, key2, dest, obj_id);
		if (key1->obj_id == key2->obj_id) {
			MAKE_SEPARATOR(key1, key2, dest, rec_type);
			if (key1->rec_type == key2->rec_type) {
				MAKE_SEPARATOR(key1, key2, dest, key);
				/*
				 * Don't bother creating a separator for
				 * create_tid, which also conveniently avoids
				 * having to handle the create_tid == 0
				 * (infinity) case.  Just leave create_tid
				 * set to key2.
				 *
				 * Worst case, dest matches key2 exactly,
				 * which is acceptable.
				 */
			}
		}
	}
}

#undef MAKE_SEPARATOR

/*
 * Return whether a generic internal or leaf node is full
 */
static __inline
int
btree_node_is_full(hammer_node_ondisk_t node)
{
	return(btree_max_elements(node->type) == node->count);
}

static __inline
int
btree_max_elements(u_int8_t type)
{
	if (type == HAMMER_BTREE_TYPE_LEAF)
		return(HAMMER_BTREE_LEAF_ELMS);
	if (type == HAMMER_BTREE_TYPE_INTERNAL)
		return(HAMMER_BTREE_INT_ELMS);
	panic("btree_max_elements: bad type %d", type);
}

void
hammer_print_btree_node(hammer_node_ondisk_t ondisk)
{
	hammer_btree_elm_t elm;
	int i;

	kprintf("node %p count=%d parent=%016llx type=%c\n",
		ondisk, ondisk->count,
		(long long)ondisk->parent, ondisk->type);

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
	kprintf("\tobj_id       = %016llx\n", (long long)elm->base.obj_id);
	kprintf("\tkey          = %016llx\n", (long long)elm->base.key);
	kprintf("\tcreate_tid   = %016llx\n", (long long)elm->base.create_tid);
	kprintf("\tdelete_tid   = %016llx\n", (long long)elm->base.delete_tid);
	kprintf("\trec_type     = %04x\n", elm->base.rec_type);
	kprintf("\tobj_type     = %02x\n", elm->base.obj_type);
	kprintf("\tbtype 	= %02x (%c)\n",
		elm->base.btype,
		(elm->base.btype ? elm->base.btype : '?'));
	kprintf("\tlocalization	= %02x\n", elm->base.localization);

	switch(type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		kprintf("\tsubtree_off  = %016llx\n",
			(long long)elm->internal.subtree_offset);
		break;
	case HAMMER_BTREE_TYPE_RECORD:
		kprintf("\tdata_offset  = %016llx\n",
			(long long)elm->leaf.data_offset);
		kprintf("\tdata_len     = %08x\n", elm->leaf.data_len);
		kprintf("\tdata_crc     = %08x\n", elm->leaf.data_crc);
		break;
	}
}
