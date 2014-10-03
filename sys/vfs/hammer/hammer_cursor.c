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
 * HAMMER B-Tree index - cursor support routines
 */
#include "hammer.h"

static int hammer_load_cursor_parent(hammer_cursor_t cursor, int try_exclusive);

/*
 * Initialize a fresh cursor using the B-Tree node cache.  If the cache
 * is not available initialize a fresh cursor at the root of the filesystem.
 */
int
hammer_init_cursor(hammer_transaction_t trans, hammer_cursor_t cursor,
		   hammer_node_cache_t cache, hammer_inode_t ip)
{
	hammer_volume_t volume;
	hammer_node_t node;
	hammer_mount_t hmp;
	u_int tticks;
	int error;

	bzero(cursor, sizeof(*cursor));

	cursor->trans = trans;
	hmp = trans->hmp;

	/*
	 * As the number of inodes queued to the flusher increases we use
	 * time-domain multiplexing to control read vs flush performance.
	 * We have to do it here, before acquiring any ip or node locks,
	 * to avoid deadlocking or excessively delaying the flusher.
	 *
	 * The full time period is hammer_tdmux_ticks, typically 1/5 of
	 * a second.
	 *
	 * inode allocation begins to get restrained at 2/4 the limit
	 * via the "hmrrcm" mechanism in hammer_inode.  We want to begin
	 * limiting read activity before that to try to avoid processes
	 * stalling out in "hmrrcm".
	 */
	tticks = hammer_tdmux_ticks;
	if (trans->type != HAMMER_TRANS_FLS && tticks &&
	    hmp->count_reclaims > hammer_limit_reclaims / tticks &&
	    hmp->count_reclaims > hammer_autoflush * 2 &&
	    hammer_flusher_running(hmp)) {
		u_int rticks;
		u_int xticks;
		u_int dummy;

		/*
		 * 0 ... xticks ... tticks
		 *
		 * rticks is the calculated position, xticks is the demarc
		 * where values below xticks are reserved for the flusher
		 * and values >= to xticks may be used by the frontend.
		 *
		 * At least one tick is always made available for the
		 * frontend.
		 */
		rticks = (u_int)ticks % tticks;
		xticks = hmp->count_reclaims * tticks / hammer_limit_reclaims;

		/*
		 * Ensure rticks and xticks are stable
		 */
		cpu_ccfence();
		if (rticks < xticks) {
			if (hammer_debug_general & 0x0004)
				kprintf("rt %3u, xt %3u, tt %3u\n",
					rticks, xticks, tticks);
			tsleep(&dummy, 0, "htdmux", xticks - rticks);
		}
	}

	/*
	 * If the cursor operation is on behalf of an inode, lock
	 * the inode.
	 *
	 * When acquiring a shared lock on an inode on which the backend
	 * flusher deadlocked, wait up to hammer_tdmux_ticks (1 second)
	 * for the deadlock to clear.
	 */
	if ((cursor->ip = ip) != NULL) {
		++ip->cursor_ip_refs;
		if (trans->type == HAMMER_TRANS_FLS) {
			hammer_lock_ex(&ip->lock);
		} else {
#if 0
			if (ip->cursor_exclreq_count) {
				tsleep(&ip->cursor_exclreq_count, 0,
				       "hstag1", hammer_tdmux_ticks);
			}
#endif
			hammer_lock_sh(&ip->lock);
		}
	}

	/*
	 * Step 1 - acquire a locked node from the cache if possible
	 */
	if (cache && cache->node) {
		node = hammer_ref_node_safe(trans, cache, &error);
		if (error == 0) {
			hammer_lock_sh(&node->lock);
			if (node->flags & HAMMER_NODE_DELETED) {
				hammer_unlock(&node->lock);
				hammer_rel_node(node);
				node = NULL;
			}
		}
		if (node == NULL)
			++hammer_stats_btree_root_iterations;
	} else {
		node = NULL;
		++hammer_stats_btree_root_iterations;
	}

	/*
	 * Step 2 - If we couldn't get a node from the cache, get
	 * the one from the root of the filesystem.
	 */
	while (node == NULL) {
		volume = hammer_get_root_volume(hmp, &error);
		if (error)
			break;
		node = hammer_get_node(trans, volume->ondisk->vol0_btree_root,
				       0, &error);
		hammer_rel_volume(volume, 0);
		if (error)
			break;
		/*
		 * When the frontend acquires the root b-tree node while the
		 * backend is deadlocked on it, wait up to hammer_tdmux_ticks
		 * (1 second) for the deadlock to clear.
		 */
#if 0
		if (node->cursor_exclreq_count &&
		    cursor->trans->type != HAMMER_TRANS_FLS) {
			tsleep(&node->cursor_exclreq_count, 0,
			       "hstag3", hammer_tdmux_ticks);
		}
#endif
		hammer_lock_sh(&node->lock);

		/*
		 * If someone got in before we could lock the node, retry.
		 */
		if (node->flags & HAMMER_NODE_DELETED) {
			hammer_unlock(&node->lock);
			hammer_rel_node(node);
			node = NULL;
			continue;
		}
		if (volume->ondisk->vol0_btree_root != node->node_offset) {
			hammer_unlock(&node->lock);
			hammer_rel_node(node);
			node = NULL;
			continue;
		}
	}

	/*
	 * Step 3 - finish initializing the cursor by acquiring the parent
	 */
	cursor->node = node;
	if (error == 0)
		error = hammer_load_cursor_parent(cursor, 0);
	KKASSERT(error == 0);
	/* if (error) hammer_done_cursor(cursor); */
	return(error);
}

/*
 * Normalize a cursor.  Sometimes cursors can be left in a state
 * where node is NULL.  If the cursor is in this state, cursor up.
 */
void
hammer_normalize_cursor(hammer_cursor_t cursor)
{
	if (cursor->node == NULL) {
		KKASSERT(cursor->parent != NULL);
		hammer_cursor_up(cursor);
	}
}


/*
 * We are finished with a cursor.  We NULL out various fields as sanity
 * check, in case the structure is inappropriately used afterwords.
 */
void
hammer_done_cursor(hammer_cursor_t cursor)
{
	hammer_inode_t ip;

	KKASSERT((cursor->flags & HAMMER_CURSOR_TRACKED) == 0);
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
	if ((ip = cursor->ip) != NULL) {
                KKASSERT(ip->cursor_ip_refs > 0);
                --ip->cursor_ip_refs;
		hammer_unlock(&ip->lock);
                cursor->ip = NULL;
        }
	if (cursor->iprec) {
		hammer_rel_mem_record(cursor->iprec);
		cursor->iprec = NULL;
	}

	/*
	 * If we deadlocked this node will be referenced.  Do a quick
	 * lock/unlock to wait for the deadlock condition to clear.
	 *
	 * Maintain exclreq_count / wakeup as necessary to notify new
	 * entrants into ip.  We continue to hold the fs_token so our
	 * EDEADLK retry loop should get its chance before another thread
	 * steals the lock.
	 */
	if (cursor->deadlk_node) {
#if 0
		if (ip && cursor->trans->type == HAMMER_TRANS_FLS)
			++ip->cursor_exclreq_count;
		++cursor->deadlk_node->cursor_exclreq_count;
#endif
		hammer_lock_ex_ident(&cursor->deadlk_node->lock, "hmrdlk");
		hammer_unlock(&cursor->deadlk_node->lock);
#if 0
		if (--cursor->deadlk_node->cursor_exclreq_count == 0)
			wakeup(&cursor->deadlk_node->cursor_exclreq_count);
		if (ip && cursor->trans->type == HAMMER_TRANS_FLS) {
			if (--ip->cursor_exclreq_count == 0)
				wakeup(&ip->cursor_exclreq_count);
		}
#endif
		hammer_rel_node(cursor->deadlk_node);
		cursor->deadlk_node = NULL;
	}
	if (cursor->deadlk_rec) {
		hammer_wait_mem_record_ident(cursor->deadlk_rec, "hmmdlr");
		hammer_rel_mem_record(cursor->deadlk_rec);
		cursor->deadlk_rec = NULL;
	}

	cursor->data = NULL;
	cursor->leaf = NULL;
	cursor->left_bound = NULL;
	cursor->right_bound = NULL;
	cursor->trans = NULL;
}

/*
 * Upgrade cursor->node and cursor->parent to exclusive locks.  This
 * function can return EDEADLK.
 *
 * The lock must already be either held shared or already held exclusively
 * by us.
 *
 * We upgrade the parent first as it is the most likely to collide first
 * with the downward traversal that the frontend typically does.
 *
 * If we fail to upgrade the lock and cursor->deadlk_node is NULL, 
 * we add another reference to the node that failed and set
 * cursor->deadlk_node so hammer_done_cursor() can block on it.
 */
int
hammer_cursor_upgrade(hammer_cursor_t cursor)
{
	int error;

	if (cursor->parent) {
		error = hammer_lock_upgrade(&cursor->parent->lock, 1);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->parent;
			hammer_ref_node(cursor->deadlk_node);
		}
	} else {
		error = 0;
	}
	if (error == 0) {
		error = hammer_lock_upgrade(&cursor->node->lock, 1);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->node;
			hammer_ref_node(cursor->deadlk_node);
		}
	}
#if 0
	error = hammer_lock_upgrade(&cursor->node->lock, 1);
	if (error && cursor->deadlk_node == NULL) {
		cursor->deadlk_node = cursor->node;
		hammer_ref_node(cursor->deadlk_node);
	} else if (error == 0 && cursor->parent) {
		error = hammer_lock_upgrade(&cursor->parent->lock, 1);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->parent;
			hammer_ref_node(cursor->deadlk_node);
		}
	}
#endif
	return(error);
}

int
hammer_cursor_upgrade_node(hammer_cursor_t cursor)
{
	int error;

	error = hammer_lock_upgrade(&cursor->node->lock, 1);
	if (error && cursor->deadlk_node == NULL) {
		cursor->deadlk_node = cursor->node;
		hammer_ref_node(cursor->deadlk_node);
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
	if (hammer_lock_excl_owned(&cursor->node->lock, curthread))
		hammer_lock_downgrade(&cursor->node->lock, 1);
	if (cursor->parent &&
	    hammer_lock_excl_owned(&cursor->parent->lock, curthread)) {
		hammer_lock_downgrade(&cursor->parent->lock, 1);
	}
}

/*
 * Upgrade and downgrade pairs of cursors.  This is used by the dedup
 * code which must deal with two cursors.  A special function is needed
 * because some of the nodes may be shared between the two cursors,
 * resulting in share counts > 1 which will normally cause an upgrade
 * to fail.
 */
static __noinline
int
collect_node(hammer_node_t *array, int *counts, int n, hammer_node_t node)
{
	int i;

	for (i = 0; i < n; ++i) {
		if (array[i] == node)
			break;
	}
	if (i == n) {
		array[i] = node;
		counts[i] = 1;
		++i;
	} else {
		++counts[i];
	}
	return(i);
}

int
hammer_cursor_upgrade2(hammer_cursor_t cursor1, hammer_cursor_t cursor2)
{
	hammer_node_t nodes[4];
	int counts[4];
	int error;
	int i;
	int n;

	n = collect_node(nodes, counts, 0, cursor1->node);
	if (cursor1->parent)
		n = collect_node(nodes, counts, n, cursor1->parent);
	n = collect_node(nodes, counts, n, cursor2->node);
	if (cursor2->parent)
		n = collect_node(nodes, counts, n, cursor2->parent);

	error = 0;
	for (i = 0; i < n; ++i) {
		error = hammer_lock_upgrade(&nodes[i]->lock, counts[i]);
		if (error)
			break;
	}
	if (error) {
		while (--i >= 0)
			hammer_lock_downgrade(&nodes[i]->lock, counts[i]);
	}
	return (error);
}

void
hammer_cursor_downgrade2(hammer_cursor_t cursor1, hammer_cursor_t cursor2)
{
	hammer_node_t nodes[4];
	int counts[4];
	int i;
	int n;

	n = collect_node(nodes, counts, 0, cursor1->node);
	if (cursor1->parent)
		n = collect_node(nodes, counts, n, cursor1->parent);
	n = collect_node(nodes, counts, n, cursor2->node);
	if (cursor2->parent)
		n = collect_node(nodes, counts, n, cursor2->parent);

	for (i = 0; i < n; ++i)
		hammer_lock_downgrade(&nodes[i]->lock, counts[i]);
}

/*
 * Seek the cursor to the specified node and index.
 *
 * The caller must ref the node prior to calling this routine and release
 * it after it returns.  If the seek succeeds the cursor will gain its own
 * ref on the node.
 */
int
hammer_cursor_seek(hammer_cursor_t cursor, hammer_node_t node, int index)
{
	int error;

	hammer_cursor_downgrade(cursor);
	error = 0;

	if (cursor->node != node) {
		hammer_unlock(&cursor->node->lock);
		hammer_rel_node(cursor->node);
		cursor->node = node;
		hammer_ref_node(node);
		hammer_lock_sh(&node->lock);
		KKASSERT ((node->flags & HAMMER_NODE_DELETED) == 0);

		if (cursor->parent) {
			hammer_unlock(&cursor->parent->lock);
			hammer_rel_node(cursor->parent);
			cursor->parent = NULL;
			cursor->parent_index = 0;
		}
		error = hammer_load_cursor_parent(cursor, 0);
	}
	cursor->index = index;
	return (error);
}

/*
 * Load the parent of cursor->node into cursor->parent.
 */
static
int
hammer_load_cursor_parent(hammer_cursor_t cursor, int try_exclusive)
{
	hammer_mount_t hmp;
	hammer_node_t parent;
	hammer_node_t node;
	hammer_btree_elm_t elm;
	int error;
	int parent_index;

	hmp = cursor->trans->hmp;

	if (cursor->node->ondisk->parent) {
		node = cursor->node;
		parent = hammer_btree_get_parent(cursor->trans, node,
						 &parent_index,
						 &error, try_exclusive);
		if (error == 0) {
			elm = &parent->ondisk->elms[parent_index];
			cursor->parent = parent;
			cursor->parent_index = parent_index;
			cursor->left_bound = &elm[0].internal.base;
			cursor->right_bound = &elm[1].internal.base;
		}
	} else {
		cursor->parent = NULL;
		cursor->parent_index = 0;
		cursor->left_bound = &hmp->root_btree_beg;
		cursor->right_bound = &hmp->root_btree_end;
		error = 0;
	}
	return(error);
}

/*
 * Cursor up to our parent node.  Return ENOENT if we are at the root of
 * the filesystem.
 */
int
hammer_cursor_up(hammer_cursor_t cursor)
{
	int error;

	hammer_cursor_downgrade(cursor);

	/*
	 * If the parent is NULL we are at the root of the B-Tree and
	 * return ENOENT.
	 */
	if (cursor->parent == NULL)
		return (ENOENT);

	/*
	 * Set the node to its parent. 
	 */
	hammer_unlock(&cursor->node->lock);
	hammer_rel_node(cursor->node);
	cursor->node = cursor->parent;
	cursor->index = cursor->parent_index;
	cursor->parent = NULL;
	cursor->parent_index = 0;

	error = hammer_load_cursor_parent(cursor, 0);
	return(error);
}

/*
 * Special cursor up given a locked cursor.  The orignal node is not
 * unlocked or released and the cursor is not downgraded.
 *
 * This function can fail with EDEADLK.
 *
 * This function is only run when recursively deleting parent nodes
 * to get rid of an empty leaf.
 */
int
hammer_cursor_up_locked(hammer_cursor_t cursor)
{
	hammer_node_t save;
	int error;
	int save_index;

	/*
	 * If the parent is NULL we are at the root of the B-Tree and
	 * return ENOENT.
	 */
	if (cursor->parent == NULL)
		return (ENOENT);

	save = cursor->node;
	save_index = cursor->index;

	/*
	 * Set the node to its parent. 
	 */
	cursor->node = cursor->parent;
	cursor->index = cursor->parent_index;
	cursor->parent = NULL;
	cursor->parent_index = 0;

	/*
	 * load the new parent, attempt to exclusively lock it.  Note that
	 * we are still holding the old parent (now cursor->node) exclusively
	 * locked.
	 *
	 * This can return EDEADLK.  Undo the operation on any error.  These
	 * up sequences can occur during iterations so be sure to restore
	 * the index.
	 */
	error = hammer_load_cursor_parent(cursor, 1);
	if (error) {
		cursor->parent = cursor->node;
		cursor->parent_index = cursor->index;
		cursor->node = save;
		cursor->index = save_index;
	}
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
	 */
	switch(elm->base.btype) {
	case HAMMER_BTREE_TYPE_INTERNAL:
	case HAMMER_BTREE_TYPE_LEAF:
		KKASSERT(node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL);
		KKASSERT(elm->internal.subtree_offset != 0);
		cursor->left_bound = &elm[0].internal.base;
		cursor->right_bound = &elm[1].internal.base;
		node = hammer_get_node(cursor->trans,
				       elm->internal.subtree_offset, 0, &error);
		if (error == 0) {
			KASSERT(elm->base.btype == node->ondisk->type, ("BTYPE MISMATCH %c %c NODE %p", elm->base.btype, node->ondisk->type, node));
			if (node->ondisk->parent != cursor->parent->node_offset)
				panic("node %p %016llx vs %016llx", node, (long long)node->ondisk->parent, (long long)cursor->parent->node_offset);
			KKASSERT(node->ondisk->parent == cursor->parent->node_offset);
		}
		break;
	default:
		panic("hammer_cursor_down: illegal btype %02x (%c)",
		      elm->base.btype,
		      (elm->base.btype ? elm->base.btype : '?'));
		break;
	}

	/*
	 * If no error occured we can lock the new child node.  If the
	 * node is deadlock flagged wait up to hammer_tdmux_ticks (1 second)
	 * for the deadlock to clear.  Otherwise a large number of concurrent
	 * readers can continuously stall the flusher.
	 *
	 * We specifically do this in the cursor_down() code in order to
	 * deal with frontend top-down searches smashing against bottom-up
	 * flusher-based mirror updates.  These collisions typically occur
	 * above the inode in the B-Tree and are not covered by the
	 * ip->cursor_exclreq_count logic.
	 */
	if (error == 0) {
#if 0
		if (node->cursor_exclreq_count &&
		    cursor->trans->type != HAMMER_TRANS_FLS) {
			tsleep(&node->cursor_exclreq_count, 0,
			       "hstag2", hammer_tdmux_ticks);
		}
#endif
		hammer_lock_sh(&node->lock);
		KKASSERT ((node->flags & HAMMER_NODE_DELETED) == 0);
		cursor->node = node;
		cursor->index = 0;
	}
	return(error);
}

/************************************************************************
 *				DEADLOCK RECOVERY			*
 ************************************************************************
 *
 * These are the new deadlock recovery functions.  Currently they are only
 * used for the mirror propagation and physical node removal cases but
 * ultimately the intention is to use them for all deadlock recovery
 * operations.
 *
 * WARNING!  The contents of the cursor may be modified while unlocked.
 *	     passive modifications including adjusting the node, parent,
 *	     indexes, and leaf pointer.
 *
 *	     An outright removal of the element the cursor was pointing at
 *	     will cause the HAMMER_CURSOR_TRACKED_RIPOUT flag to be set,
 *	     which chains to causing the HAMMER_CURSOR_RETEST to be set
 *	     when the cursor is locked again.
 */
void
hammer_unlock_cursor(hammer_cursor_t cursor)
{
	hammer_node_t node;

	KKASSERT((cursor->flags & HAMMER_CURSOR_TRACKED) == 0);
	KKASSERT(cursor->node);

	/*
	 * Release the cursor's locks and track B-Tree operations on node.
	 * While being tracked our cursor can be modified by other threads
	 * and the node may be replaced.
	 */
	if (cursor->parent) {
		hammer_unlock(&cursor->parent->lock);
		hammer_rel_node(cursor->parent);
		cursor->parent = NULL;
	}
	node = cursor->node;
	cursor->flags |= HAMMER_CURSOR_TRACKED;
	TAILQ_INSERT_TAIL(&node->cursor_list, cursor, deadlk_entry);
	hammer_unlock(&node->lock);
}

/*
 * Get the cursor heated up again.  The cursor's node may have
 * changed and we might have to locate the new parent.
 *
 * If the exact element we were on got deleted RIPOUT will be
 * set and we must clear ATEDISK so an iteration does not skip
 * the element after it.
 */
int
hammer_lock_cursor(hammer_cursor_t cursor)
{
	hammer_node_t node;
	int error;

	KKASSERT(cursor->flags & HAMMER_CURSOR_TRACKED);

	/*
	 * Relock the node
	 */
	for (;;) {
		node = cursor->node;
		hammer_ref_node(node);
		hammer_lock_sh(&node->lock);
		if (cursor->node == node) {
			hammer_rel_node(node);
			break;
		}
		hammer_unlock(&node->lock);
		hammer_rel_node(node);
	}

	/*
	 * Untrack the cursor, clean up, and re-establish the parent node.
	 */
	TAILQ_REMOVE(&node->cursor_list, cursor, deadlk_entry);
	cursor->flags &= ~HAMMER_CURSOR_TRACKED;

	/*
	 * If a ripout has occured iterations must re-test the (new)
	 * current element.  Clearing ATEDISK prevents the element from
	 * being skipped and RETEST causes it to be re-tested.
	 */
	if (cursor->flags & HAMMER_CURSOR_TRACKED_RIPOUT) {
		cursor->flags &= ~HAMMER_CURSOR_TRACKED_RIPOUT;
		cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		cursor->flags |= HAMMER_CURSOR_RETEST;
	}
	error = hammer_load_cursor_parent(cursor, 0);
	return(error);
}

/*
 * Recover from a deadlocked cursor, tracking any node removals or
 * replacements.  If the cursor's current node is removed by another
 * thread (via btree_remove()) the cursor will be seeked upwards.
 *
 * The caller is working a modifying operation and must be holding the
 * sync lock (shared).  We do not release the sync lock because this
 * would break atomicy.
 */
int
hammer_recover_cursor(hammer_cursor_t cursor)
{
	hammer_transaction_t trans __debugvar;
#if 0
	hammer_inode_t ip;
#endif
	int error;

	hammer_unlock_cursor(cursor);

#if 0
	ip = cursor->ip;
#endif
	trans = cursor->trans;
	KKASSERT(trans->sync_lock_refs > 0);

	/*
	 * Wait for the deadlock to clear.
	 *
	 * Maintain exclreq_count / wakeup as necessary to notify new
	 * entrants into ip.  We continue to hold the fs_token so our
	 * EDEADLK retry loop should get its chance before another thread
	 * steals the lock.
	 */
	if (cursor->deadlk_node) {
#if 0
		if (ip && trans->type == HAMMER_TRANS_FLS)
			++ip->cursor_exclreq_count;
		++cursor->deadlk_node->cursor_exclreq_count;
#endif
		hammer_lock_ex_ident(&cursor->deadlk_node->lock, "hmrdlk");
		hammer_unlock(&cursor->deadlk_node->lock);
#if 0
		if (--cursor->deadlk_node->cursor_exclreq_count == 0)
			wakeup(&cursor->deadlk_node->cursor_exclreq_count);
		if (ip && trans->type == HAMMER_TRANS_FLS) {
			if (--ip->cursor_exclreq_count == 0)
				wakeup(&ip->cursor_exclreq_count);
		}
#endif
		hammer_rel_node(cursor->deadlk_node);
		cursor->deadlk_node = NULL;
	}
	if (cursor->deadlk_rec) {
		hammer_wait_mem_record_ident(cursor->deadlk_rec, "hmmdlr");
		hammer_rel_mem_record(cursor->deadlk_rec);
		cursor->deadlk_rec = NULL;
	}
	error = hammer_lock_cursor(cursor);
	return(error);
}

/*
 * Dup ocursor to ncursor.  ncursor inherits ocursor's locks and ocursor
 * is effectively unlocked and becomes tracked.  If ocursor was not locked
 * then ncursor also inherits the tracking.
 *
 * After the caller finishes working with ncursor it must be cleaned up
 * with hammer_done_cursor(), and the caller must re-lock ocursor.
 */
hammer_cursor_t
hammer_push_cursor(hammer_cursor_t ocursor)
{
	hammer_cursor_t ncursor;
	hammer_inode_t ip;
	hammer_node_t node;
	hammer_mount_t hmp;

	hmp = ocursor->trans->hmp;
	ncursor = kmalloc(sizeof(*ncursor), hmp->m_misc, M_WAITOK | M_ZERO);
	bcopy(ocursor, ncursor, sizeof(*ocursor));

	node = ocursor->node;
	hammer_ref_node(node);
	if ((ocursor->flags & HAMMER_CURSOR_TRACKED) == 0) {
		ocursor->flags |= HAMMER_CURSOR_TRACKED;
		TAILQ_INSERT_TAIL(&node->cursor_list, ocursor, deadlk_entry);
	}
	if (ncursor->parent)
		ocursor->parent = NULL;
	ocursor->data_buffer = NULL;
	ocursor->leaf = NULL;
	ocursor->data = NULL;
	if (ncursor->flags & HAMMER_CURSOR_TRACKED)
		TAILQ_INSERT_TAIL(&node->cursor_list, ncursor, deadlk_entry);
	if ((ip = ncursor->ip) != NULL) {
                ++ip->cursor_ip_refs;
	}
	if (ncursor->iprec)
		hammer_ref(&ncursor->iprec->lock);
	return(ncursor);
}

/*
 * Destroy ncursor and restore ocursor
 *
 * This is a temporary hack for the release.  We can't afford to lose
 * the IP lock until the IP object scan code is able to deal with it,
 * so have ocursor inherit it back.
 */
void
hammer_pop_cursor(hammer_cursor_t ocursor, hammer_cursor_t ncursor)
{
	hammer_mount_t hmp;
	hammer_inode_t ip;

	hmp = ncursor->trans->hmp;
	ip = ncursor->ip;
	ncursor->ip = NULL;
	if (ip)
                --ip->cursor_ip_refs;
	hammer_done_cursor(ncursor);
	kfree(ncursor, hmp->m_misc);
	KKASSERT(ocursor->ip == ip);
	hammer_lock_cursor(ocursor);
}

/*
 * onode is being replaced by nnode by the reblocking code.
 */
void
hammer_cursor_replaced_node(hammer_node_t onode, hammer_node_t nnode)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;
	hammer_node_ondisk_t nndisk;

	ondisk = onode->ondisk;
	nndisk = nnode->ondisk;

	while ((cursor = TAILQ_FIRST(&onode->cursor_list)) != NULL) {
		TAILQ_REMOVE(&onode->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		KKASSERT(cursor->node == onode);
		if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
			cursor->leaf = &nndisk->elms[cursor->index].leaf;
		cursor->node = nnode;
		hammer_ref_node(nnode);
		hammer_rel_node(onode);
	}
}

/*
 * We have removed <node> from the parent and collapsed the parent.
 *
 * Cursors in deadlock recovery are seeked upward to the parent so the
 * btree_remove() recursion works properly even though we have marked
 * the cursor as requiring a reseek.
 *
 * This is the only cursor function which sets HAMMER_CURSOR_ITERATE_CHECK,
 * meaning the cursor is no longer definitively pointing at an element
 * within its iteration (if the cursor is being used to iterate).  The
 * iteration code will take this into account instead of asserting if the
 * cursor is outside the iteration range.
 */
void
hammer_cursor_removed_node(hammer_node_t node, hammer_node_t parent, int index)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;

	KKASSERT(parent != NULL);
	ondisk = node->ondisk;

	while ((cursor = TAILQ_FIRST(&node->cursor_list)) != NULL) {
		KKASSERT(cursor->node == node);
		KKASSERT(cursor->index == 0);
		TAILQ_REMOVE(&node->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&parent->cursor_list, cursor, deadlk_entry);
		if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
			cursor->leaf = NULL;
		cursor->flags |= HAMMER_CURSOR_TRACKED_RIPOUT;
		cursor->flags |= HAMMER_CURSOR_ITERATE_CHECK;
		cursor->node = parent;
		cursor->index = index;
		hammer_ref_node(parent);
		hammer_rel_node(node);
	}
}

/*
 * node was split at (onode, index) with elements >= index moved to nnode.
 */
void
hammer_cursor_split_node(hammer_node_t onode, hammer_node_t nnode, int index)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;
	hammer_node_ondisk_t nndisk;

	ondisk = onode->ondisk;
	nndisk = nnode->ondisk;

again:
	TAILQ_FOREACH(cursor, &onode->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == onode);
		if (cursor->index < index)
			continue;
		TAILQ_REMOVE(&onode->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
			cursor->leaf = &nndisk->elms[cursor->index - index].leaf;
		cursor->node = nnode;
		cursor->index -= index;
		hammer_ref_node(nnode);
		hammer_rel_node(onode);
		goto again;
	}
}

/*
 * An element was moved from one node to another or within a node.  The
 * index may also represent the end of the node (index == numelements).
 *
 * {oparent,pindex} is the parent node's pointer to onode/oindex.
 *
 * This is used by the rebalancing code.  This is not an insertion or
 * deletion and any additional elements, including the degenerate case at
 * the end of the node, will be dealt with by additional distinct calls.
 */
void
hammer_cursor_moved_element(hammer_node_t oparent, int pindex,
			    hammer_node_t onode, int oindex,
			    hammer_node_t nnode, int nindex)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;
	hammer_node_ondisk_t nndisk;

	/*
	 * Adjust any cursors pointing at the element
	 */
	ondisk = onode->ondisk;
	nndisk = nnode->ondisk;
again1:
	TAILQ_FOREACH(cursor, &onode->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == onode);
		if (cursor->index != oindex)
			continue;
		TAILQ_REMOVE(&onode->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		if (cursor->leaf == &ondisk->elms[oindex].leaf)
			cursor->leaf = &nndisk->elms[nindex].leaf;
		cursor->node = nnode;
		cursor->index = nindex;
		hammer_ref_node(nnode);
		hammer_rel_node(onode);
		goto again1;
	}

	/*
	 * When moving the first element of onode to a different node any
	 * cursor which is pointing at (oparent,pindex) must be repointed
	 * to nnode and ATEDISK must be cleared.
	 *
	 * This prevents cursors from losing track due to insertions.
	 * Insertions temporarily release the cursor in order to update
	 * the mirror_tids.  It primarily effects the mirror_write code.
	 * The other code paths generally only do a single insertion and
	 * then relookup or drop the cursor.
	 */
	if (onode == nnode || oindex)
		return;
	ondisk = oparent->ondisk;
again2:
	TAILQ_FOREACH(cursor, &oparent->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == oparent);
		if (cursor->index != pindex)
			continue;
		kprintf("HAMMER debug: shifted cursor pointing at parent\n"
			"parent %016jx:%d onode %016jx:%d nnode %016jx:%d\n",
			(intmax_t)oparent->node_offset, pindex,
			(intmax_t)onode->node_offset, oindex,
			(intmax_t)nnode->node_offset, nindex);
		TAILQ_REMOVE(&oparent->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		if (cursor->leaf == &ondisk->elms[oindex].leaf)
			cursor->leaf = &nndisk->elms[nindex].leaf;
		cursor->node = nnode;
		cursor->index = nindex;
		cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		hammer_ref_node(nnode);
		hammer_rel_node(oparent);
		goto again2;
	}
}

/*
 * The B-Tree element pointing to the specified node was moved from (oparent)
 * to (nparent, nindex).  We must locate any tracked cursors pointing at
 * node and adjust their parent accordingly.
 *
 * This is used by the rebalancing code when packing elements causes an
 * element to shift from one node to another.
 */
void
hammer_cursor_parent_changed(hammer_node_t node, hammer_node_t oparent,
			     hammer_node_t nparent, int nindex)
{
	hammer_cursor_t cursor;

again:
	TAILQ_FOREACH(cursor, &node->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == node);
		if (cursor->parent == oparent) {
			cursor->parent = nparent;
			cursor->parent_index = nindex;
			hammer_ref_node(nparent);
			hammer_rel_node(oparent);
			goto again;
		}
	}
}

/*
 * Deleted element at (node, index)
 *
 * Shift indexes >= index
 */
void
hammer_cursor_deleted_element(hammer_node_t node, int index)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;

	ondisk = node->ondisk;

	TAILQ_FOREACH(cursor, &node->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == node);
		if (cursor->index == index) {
			cursor->flags |= HAMMER_CURSOR_TRACKED_RIPOUT;
			if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
				cursor->leaf = NULL;
		} else if (cursor->index > index) {
			if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
				cursor->leaf = &ondisk->elms[cursor->index - 1].leaf;
			--cursor->index;
		}
	}
}

/*
 * Inserted element at (node, index)
 *
 * Shift indexes >= index
 */
void
hammer_cursor_inserted_element(hammer_node_t node, int index)
{
	hammer_cursor_t cursor;
	hammer_node_ondisk_t ondisk;

	ondisk = node->ondisk;

	TAILQ_FOREACH(cursor, &node->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == node);
		if (cursor->index >= index) {
			if (cursor->leaf == &ondisk->elms[cursor->index].leaf)
				cursor->leaf = &ondisk->elms[cursor->index + 1].leaf;
			++cursor->index;
		}
	}
}

/*
 * Invalidate the cached data buffer associated with a cursor.
 *
 * This needs to be done when the underlying block is being freed or
 * the referenced buffer can prevent the related buffer cache buffer
 * from being properly invalidated.
 */
void
hammer_cursor_invalidate_cache(hammer_cursor_t cursor)
{
        if (cursor->data_buffer) {
                hammer_rel_buffer(cursor->data_buffer, 0);
                cursor->data_buffer = NULL;
		cursor->data = NULL;
        }
}
