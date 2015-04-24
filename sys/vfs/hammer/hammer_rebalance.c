/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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

#include "hammer.h"

static int rebalance_node(struct hammer_ioc_rebalance *rebal,
			hammer_cursor_t cursor, hammer_node_lock_t lcache);
static void rebalance_closeout(hammer_node_lock_t base_item, int base_count,
			hammer_btree_elm_t elm);
static void rebalance_parent_ptrs(hammer_node_lock_t base_item, int index,
			hammer_node_lock_t item, hammer_node_lock_t chld_item);

/*
 * Iterate through the specified range of object ids and rebalance B-Tree
 * leaf and internal nodes we encounter.  A forwards iteration is used.
 *
 * All leafs are at the same depth.  We use the b-tree scan code loosely
 * to position ourselves and create degenerate cases to skip indices
 * that we have rebalanced in bulk.
 */

int
hammer_ioc_rebalance(hammer_transaction_t trans, hammer_inode_t ip,
		 struct hammer_ioc_rebalance *rebal)
{
	struct hammer_cursor cursor;
	struct hammer_node_lock lcache;
	hammer_btree_leaf_elm_t elm;
	int error;
	int seq;
	u_int32_t key_end_localization;

	if ((rebal->key_beg.localization | rebal->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (rebal->key_beg.localization > rebal->key_end.localization)
		return(EINVAL);
	if (rebal->key_beg.localization == rebal->key_end.localization) {
		if (rebal->key_beg.obj_id > rebal->key_end.obj_id)
			return(EINVAL);
		/* key-space limitations - no check needed */
	}
	if (rebal->saturation < HAMMER_BTREE_INT_ELMS / 2)
		rebal->saturation = HAMMER_BTREE_INT_ELMS / 2;
	if (rebal->saturation > HAMMER_BTREE_INT_ELMS)
		rebal->saturation = HAMMER_BTREE_INT_ELMS;

	/*
	 * Ioctl caller has only set localization type to rebalance.
	 * Initialize cursor key localization with ip localization.
	 */
	rebal->key_cur = rebal->key_beg;
	rebal->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	rebal->key_cur.localization += ip->obj_localization;

	key_end_localization = rebal->key_end.localization;
	key_end_localization &= HAMMER_LOCALIZE_MASK;
	key_end_localization += ip->obj_localization;

	hammer_btree_lcache_init(trans->hmp, &lcache, 2);

	seq = trans->hmp->flusher.done;

	/*
	 * Scan forwards.  Retries typically occur if a deadlock is detected.
	 */
retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg = rebal->key_cur;
	cursor.key_end = rebal->key_end;
	cursor.key_end.localization = key_end_localization;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * Cause internal nodes to be returned on the way up.  Internal nodes
	 * are not returned on the way down so we can create a degenerate
	 * case to handle internal nodes as a trailing function of their
	 * sub-trees.
	 *
	 * Note that by not setting INSERTING or PRUNING no boundary
	 * corrections will be made and a sync lock is not needed for the
	 * B-Tree scan itself.
	 */
	cursor.flags |= HAMMER_CURSOR_REBLOCKING;

	error = hammer_btree_first(&cursor);

	while (error == 0) {
		/*
		 * Rebalancing can be hard on the memory allocator, make
		 * sure there is enough free memory before doing it.
		 */
		if (vm_test_nominal()) {
			hammer_unlock_cursor(&cursor);
			vm_wait_nominal();
			hammer_lock_cursor(&cursor);
		}

		/*
		 * We only care about internal nodes visited for the last
		 * time on the way up... that is, a trailing scan of the
		 * internal node after all of its children have been recursed
		 * through.
		 */
		if (cursor.node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			/*
			 * Leave cursor.index alone, we want to recurse
			 * through all children of the internal node before
			 * visiting it.
			 *
			 * Process the internal node on the way up after
			 * the last child's sub-tree has been balanced.
			 */
			if (cursor.index == cursor.node->ondisk->count - 1) {
				hammer_sync_lock_sh(trans);
				error = rebalance_node(rebal, &cursor, &lcache);
				hammer_sync_unlock(trans);
			}
		} else {
			/*
			 * We don't need to iterate through all the leaf
			 * elements, we only care about the parent (internal)
			 * node.
			 */
			cursor.index = cursor.node->ondisk->count - 1;
		}
		if (error)
			break;

		/*
		 * Update returned scan position and do a flush if
		 * necessary.
		 *
		 * WARNING: We extract the base using the leaf element
		 *	    type but this could be an internal node.  The
		 *	    base is the same either way.
		 *
		 *	    However, due to the rebalancing operation the
		 *	    cursor position may have exceeded the right-hand
		 *	    boundary.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor()
		 *	    function.
		 */
		elm = &cursor.node->ondisk->elms[cursor.index].leaf;
		rebal->key_cur = elm->base;
		++rebal->stat_ncount;

		while (hammer_flusher_meta_halflimit(trans->hmp) ||
		       hammer_flusher_undo_exhausted(trans, 2)) {
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async_one(trans->hmp);
		}

		/*
		 * Before iterating check if the rebalance operation caused
		 * the cursor to index past the right-hand boundary and make
		 * sure to stop if it does.  Otherwise the iteration may
		 * panic e.g. due to the key maxing out its fields and no
		 * longer being within the strict bounds of the root node.
		 */
		if (hammer_btree_cmp(&rebal->key_cur, &cursor.key_end) > 0) {
			rebal->key_cur = cursor.key_end;
			break;
		}

		/*
		 * Iterate, stop if a signal was received.
		 */
		if ((error = hammer_signal_check(trans->hmp)) != 0)
			break;
		error = hammer_btree_iterate(&cursor);
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK) {
		++rebal->stat_collisions;
		goto retry;
	}
	if (error == EINTR) {
		rebal->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
failed:
	rebal->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	hammer_btree_lcache_free(trans->hmp, &lcache);
	return(error);
}

/*
 * Rebalance an internal node, called via a trailing upward recursion.
 * All the children have already been individually rebalanced.
 *
 * To rebalance we scan the elements in the children and pack them,
 * so we actually need to lock the children and the children's children.
 *
 *	INTERNAL_NODE
 *	/ / | | | \ \
 *     C C  C C C  C C	children (first level) (internal or leaf nodes)
 *			children's elements (second level)
 *
 *	<<<----------   pack children's elements, possibly remove excess
 *			children after packing.
 *
 * NOTE: The mirror_tids, parent pointers, and child pointers must be updated.
 *       Any live tracked B-Tree nodes must be updated (we worm out of that
 *       by not allowing any).  And boundary elements must be preserved.
 *
 * NOTE: If the children are leaf nodes we may have a degenerate case
 *       case where there are no elements in the leafs.
 *
 * XXX live-tracked
 */
static int
rebalance_node(struct hammer_ioc_rebalance *rebal, hammer_cursor_t cursor,
	       struct hammer_node_lock *lcache)
{
	struct hammer_node_lock lockroot;
	hammer_node_lock_t base_item;
	hammer_node_lock_t chld_item;
	hammer_node_lock_t item;
	hammer_btree_elm_t elm;
	hammer_node_t node;
	hammer_tid_t tid;
	u_int8_t type1 __debugvar;
	int base_count;
	int root_count;
	int avg_elms;
	int count;
	int error;
	int i;
	int n;

	/*
	 * Lock the parent node via the cursor, collect and lock our
	 * children and children's children.
	 *
	 * By the way, this is a LOT of locks.
	 */
	hammer_node_lock_init(&lockroot, cursor->node);
	error = hammer_cursor_upgrade(cursor);
	if (error)
		goto done;
	error = hammer_btree_lock_children(cursor, 2, &lockroot, lcache);
	if (error)
		goto done;

	/*
	 * Make a copy of all the locked on-disk data to simplify the element
	 * shifting we are going to have to do.  We will modify the copy
	 * first.
	 */
	hammer_btree_lock_copy(cursor, &lockroot);

	/*
	 * Look at the first child node.
	 */
	if (TAILQ_FIRST(&lockroot.list) == NULL)
		goto done;
	type1 = TAILQ_FIRST(&lockroot.list)->node->ondisk->type;

	/*
	 * Figure out the total number of children's children and
	 * calculate the average number of elements per child.
	 *
	 * The minimum avg_elms is 1 when count > 0.  avg_elms * root_elms
	 * is always greater or equal to count.
	 *
	 * If count == 0 we hit a degenerate case which will cause
	 * avg_elms to also calculate as 0.
	 */
	if (hammer_debug_general & 0x1000)
		kprintf("lockroot %p count %d\n", &lockroot, lockroot.count);
	count = 0;
	TAILQ_FOREACH(item, &lockroot.list, entry) {
		if (hammer_debug_general & 0x1000)
			kprintf("add count %d\n", item->count);
		count += item->count;
		KKASSERT(item->node->ondisk->type == type1);
	}
	avg_elms = (count + (lockroot.count - 1)) / lockroot.count;
	KKASSERT(avg_elms >= 0);

	/*
	 * If the average number of elements per child is too low then
	 * calculate the desired number of children (n) such that the
	 * average number of elements is reasonable.
	 *
	 * If the desired number of children is 1 then avg_elms will
	 * wind up being count, which may still be smaller then saturation
	 * but that is ok.
	 */
	if (count && avg_elms < rebal->saturation) {
		n = (count + (rebal->saturation - 1)) / rebal->saturation;
		avg_elms = (count + (n - 1)) / n;
	}

	/*
	 * Pack the elements in the children.  Elements for each item is
	 * packed into base_item until avg_elms is reached, then base_item
	 * iterates.
	 *
	 * hammer_cursor_moved_element() is called for each element moved
	 * to update tracked cursors, including the index beyond the last
	 * element (at count).
	 *
	 * Any cursors tracking the internal node itself must also be
	 * updated, potentially repointing them at a leaf and clearing
	 * ATEDISK.
	 */
	base_item = TAILQ_FIRST(&lockroot.list);
	base_count = 0;
	root_count = 0;

	TAILQ_FOREACH(item, &lockroot.list, entry) {
		node = item->node;
		KKASSERT(item->count == node->ondisk->count);
		chld_item = TAILQ_FIRST(&item->list);
		for (i = 0; i < item->count; ++i) {
			/*
			 * Closeout.  If the next element is at index 0
			 * just use the existing separator in the parent.
			 */
			if (base_count == avg_elms) {
				if (i == 0) {
					elm = &lockroot.node->ondisk->elms[
						item->index];
				} else {
					elm = &node->ondisk->elms[i];
				}
				rebalance_closeout(base_item, base_count, elm);
				base_item = TAILQ_NEXT(base_item, entry);
				KKASSERT(base_item);
				base_count = 0;
				++root_count;
			}

			/*
			 * Check degenerate no-work case.  Otherwise pack
			 * the element.
			 *
			 * All changes are made to the copy.
			 */
			if (item == base_item && i == base_count) {
				++base_count;
				if (chld_item)
					chld_item = TAILQ_NEXT(chld_item, entry);
				continue;
			}

			/*
			 * Pack element.
			 */
			elm = &base_item->copy->elms[base_count];
			*elm = node->ondisk->elms[i];
			base_item->flags |= HAMMER_NODE_LOCK_UPDATED;

			/*
			 * Adjust the mirror_tid of the target and the
			 * internal element linkage.
			 *
			 * The parent node (lockroot.node) should already
			 * have an aggregate mirror_tid so we do not have
			 * to update that.  However, it is possible for us
			 * to catch a hammer_btree_mirror_propagate() with
			 * its pants down.  Update the parent if necessary.
			 */
			tid = node->ondisk->mirror_tid;

			if (base_item->copy->mirror_tid < tid) {
				base_item->copy->mirror_tid = tid;
				if (lockroot.copy->mirror_tid < tid) {
					lockroot.copy->mirror_tid = tid;
					lockroot.flags |=
						HAMMER_NODE_LOCK_UPDATED;
				}
				if (lockroot.copy->elms[root_count].
				    internal.mirror_tid < tid) {
					lockroot.copy->elms[root_count].
						internal.mirror_tid = tid;
					lockroot.flags |=
						HAMMER_NODE_LOCK_UPDATED;
				}
				base_item->flags |= HAMMER_NODE_LOCK_UPDATED;
			}

			/*
			 * We moved elm.  The parent pointers for any
			 * children of elm must be repointed.
			 */
			if (item != base_item &&
			    node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
				KKASSERT(chld_item);
				rebalance_parent_ptrs(base_item, base_count,
						      item, chld_item);
			}
			hammer_cursor_moved_element(item->parent->node,
						    item->index,
						    node, i,
						    base_item->node,
						    base_count);
			++base_count;
			if (chld_item)
				chld_item = TAILQ_NEXT(chld_item, entry);
		}

		/*
		 * Always call at the end (i == number of elements) in
		 * case a cursor is sitting indexed there.
		 */
		hammer_cursor_moved_element(item->parent->node, item->index,
					    node, i,
					    base_item->node, base_count);
	}

	/*
	 * Packing complete, close-out base_item using the right-hand
	 * boundary of the original parent.
	 *
	 * If we will be deleting nodes from the root shift the old
	 * right-hand-boundary to the new ending index.
	 */
	elm = &lockroot.node->ondisk->elms[lockroot.node->ondisk->count];
	rebalance_closeout(base_item, base_count, elm);
	++root_count;
	if (lockroot.copy->count != root_count) {
		lockroot.copy->count = root_count;
		lockroot.copy->elms[root_count] = *elm;
		lockroot.flags |= HAMMER_NODE_LOCK_UPDATED;
	}

	/*
	 * Any extra items beyond base_item are now completely empty and
	 * can be destroyed.  Queue the destruction up in the copy.  Note
	 * that none of the destroyed nodes are part of our cursor.
	 *
	 * The cursor is locked so it isn't on the tracking list.  It
	 * should have been pointing at the boundary element (at root_count).
	 * When deleting elements from the root (which is cursor.node), we
	 * have to update the cursor.index manually to keep it in bounds.
	 */
	while ((base_item = TAILQ_NEXT(base_item, entry)) != NULL) {
		hammer_cursor_removed_node(base_item->node, lockroot.node,
					   base_count);
		hammer_cursor_deleted_element(lockroot.node, base_count);
		base_item->copy->type = HAMMER_BTREE_TYPE_DELETED;
		base_item->copy->count = 0;
		base_item->flags |= HAMMER_NODE_LOCK_UPDATED;
		if (cursor->index > lockroot.copy->count)
			--cursor->index;
		++rebal->stat_deletions;
	}

	/*
	 * All done, sync the locked child tree to disk.  This will also
	 * flush and delete deleted nodes.
	 */
	rebal->stat_nrebal += hammer_btree_sync_copy(cursor, &lockroot);
done:
	hammer_btree_unlock_children(cursor->trans->hmp, &lockroot, lcache);
	hammer_cursor_downgrade(cursor);
	return (error);
}

/*
 * Close-out the child base_item.  This node contains base_count
 * elements.
 *
 * If the node is an internal node the right-hand boundary must be
 * set to elm.
 */
static
void
rebalance_closeout(hammer_node_lock_t base_item, int base_count,
		   hammer_btree_elm_t elm)
{
	hammer_node_lock_t parent;
	hammer_btree_elm_t base_elm;
	hammer_btree_elm_t rbound_elm;
	u_int8_t save;

	/*
	 * Update the count.  NOTE:  base_count can be 0 for the
	 * degenerate leaf case.
	 */
	if (hammer_debug_general & 0x1000) {
		kprintf("rebalance_closeout %016llx:",
			(long long)base_item->node->node_offset);
	}
	if (base_item->copy->count != base_count) {
		base_item->flags |= HAMMER_NODE_LOCK_UPDATED;
		base_item->copy->count = base_count;
		if (hammer_debug_general & 0x1000)
			kprintf(" (count update)");
	}

	/*
	 * If we are closing out an internal node we must assign
	 * a right-hand boundary.  Use the element contents as the
	 * right-hand boundary.
	 *
	 * Internal nodes are required to have at least one child,
	 * otherwise the left and right boundary would end up being
	 * the same element.  Only leaf nodes can be empty.
	 *
	 * Rebalancing may cut-off an internal node such that the
	 * new right hand boundary is the next element anyway, but
	 * we still have to make sure that subtree_offset, btype,
	 * and mirror_tid are all 0.
	 */
	if (base_item->copy->type == HAMMER_BTREE_TYPE_INTERNAL) {
		KKASSERT(base_count != 0);
		base_elm = &base_item->copy->elms[base_count];

		if (bcmp(base_elm, elm, sizeof(*elm)) != 0 ||
		    elm->internal.subtree_offset ||
		    elm->internal.mirror_tid ||
		    elm->base.btype) {
			*base_elm = *elm;
			base_elm->internal.subtree_offset = 0;
			base_elm->internal.mirror_tid = 0;
			base_elm->base.btype = 0;
			base_item->flags |= HAMMER_NODE_LOCK_UPDATED;
			if (hammer_debug_general & 0x1000)
				kprintf(" (rhs update)");
		} else {
			if (hammer_debug_general & 0x1000)
				kprintf(" (rhs same)");
		}
	}

	/*
	 * The parent's boundary must be updated.  Be careful to retain
	 * the btype and non-base internal fields as that information is
	 * unrelated.
	 */
	parent = base_item->parent;
	rbound_elm = &parent->copy->elms[base_item->index + 1];
	if (bcmp(&rbound_elm->base, &elm->base, sizeof(elm->base)) != 0) {
		save = rbound_elm->base.btype;
		rbound_elm->base = elm->base;
		rbound_elm->base.btype = save;
		parent->flags |= HAMMER_NODE_LOCK_UPDATED;
		if (hammer_debug_general & 0x1000) {
			kprintf(" (parent bound update %d)",
				base_item->index + 1);
		}
	}
	if (hammer_debug_general & 0x1000)
		kprintf("\n");
}

/*
 * An element in item has moved to base_item.  We must update the parent
 * pointer of the node the element points to (which is chld_item).
 */
static
void
rebalance_parent_ptrs(hammer_node_lock_t base_item, int index,
		      hammer_node_lock_t item, hammer_node_lock_t chld_item)
{
	KKASSERT(chld_item->node->ondisk->parent == item->node->node_offset);
	chld_item->copy->parent = base_item->node->node_offset;
	chld_item->flags |= HAMMER_NODE_LOCK_UPDATED;
	hammer_cursor_parent_changed(chld_item->node,
				     item->node, base_item->node, index);
}
