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
 * $DragonFly: src/sys/vfs/hammer/hammer_cursor.c,v 1.42 2008/08/06 15:38:58 dillon Exp $
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
	int error;

	bzero(cursor, sizeof(*cursor));

	cursor->trans = trans;

	/*
	 * If the cursor operation is on behalf of an inode, lock
	 * the inode.
	 */
	if ((cursor->ip = ip) != NULL) {
		++ip->cursor_ip_refs;
		if (trans->type == HAMMER_TRANS_FLS)
			hammer_lock_ex(&ip->lock);
		else
			hammer_lock_sh(&ip->lock);
	}

	/*
	 * Step 1 - acquire a locked node from the cache if possible
	 */
	if (cache && cache->node) {
		node = hammer_ref_node_safe(trans->hmp, cache, &error);
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
	 * the one from the root of the filesystem.
	 */
	while (node == NULL) {
		volume = hammer_get_root_volume(trans->hmp, &error);
		if (error)
			break;
		node = hammer_get_node(trans, volume->ondisk->vol0_btree_root,
				       0, &error);
		hammer_rel_volume(volume, 0);
		if (error)
			break;
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
	 */
	if (cursor->deadlk_node) {
		hammer_lock_ex_ident(&cursor->deadlk_node->lock, "hmrdlk");
		hammer_unlock(&cursor->deadlk_node->lock);
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
 * If we fail to upgrade the lock and cursor->deadlk_node is NULL, 
 * we add another reference to the node that failed and set
 * cursor->deadlk_node so hammer_done_cursor() can block on it.
 */
int
hammer_cursor_upgrade(hammer_cursor_t cursor)
{
	int error;

	error = hammer_lock_upgrade(&cursor->node->lock);
	if (error && cursor->deadlk_node == NULL) {
		cursor->deadlk_node = cursor->node;
		hammer_ref_node(cursor->deadlk_node);
	} else if (error == 0 && cursor->parent) {
		error = hammer_lock_upgrade(&cursor->parent->lock);
		if (error && cursor->deadlk_node == NULL) {
			cursor->deadlk_node = cursor->parent;
			hammer_ref_node(cursor->deadlk_node);
		}
	}
	return(error);
}

int
hammer_cursor_upgrade_node(hammer_cursor_t cursor)
{
	int error;

	error = hammer_lock_upgrade(&cursor->node->lock);
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
		hammer_lock_downgrade(&cursor->node->lock);
	if (cursor->parent &&
	    hammer_lock_excl_owned(&cursor->parent->lock, curthread)) {
		hammer_lock_downgrade(&cursor->parent->lock);
	}
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
 * This function will recover from deadlocks.  EDEADLK cannot be returned.
 */
int
hammer_cursor_up_locked(hammer_cursor_t cursor)
{
	hammer_node_t save;
	int error;

	/*
	 * If the parent is NULL we are at the root of the B-Tree and
	 * return ENOENT.
	 */
	if (cursor->parent == NULL)
		return (ENOENT);

	save = cursor->node;

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
	 * locked.  This can return EDEADLK.
	 */
	error = hammer_load_cursor_parent(cursor, 1);
	if (error) {
		cursor->parent = cursor->node;
		cursor->parent_index = cursor->index;
		cursor->node = save;
		cursor->index = 0;
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
			KASSERT(elm->base.btype == node->ondisk->type, ("BTYPE MISMATCH %c %c NODE %p\n", elm->base.btype, node->ondisk->type, node));
			if (node->ondisk->parent != cursor->parent->node_offset)
				panic("node %p %016llx vs %016llx\n", node, node->ondisk->parent, cursor->parent->node_offset);
			KKASSERT(node->ondisk->parent == cursor->parent->node_offset);
		}
		break;
	default:
		panic("hammer_cursor_down: illegal btype %02x (%c)\n",
		      elm->base.btype,
		      (elm->base.btype ? elm->base.btype : '?'));
		break;
	}
	if (error == 0) {
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
 */
void
hammer_unlock_cursor(hammer_cursor_t cursor, int also_ip)
{
	hammer_node_t node;
	hammer_inode_t ip;

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

	if (also_ip && (ip = cursor->ip) != NULL)
		hammer_unlock(&ip->lock);
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
hammer_lock_cursor(hammer_cursor_t cursor, int also_ip)
{
	hammer_inode_t ip;
	hammer_node_t node;
	int error;

	KKASSERT(cursor->flags & HAMMER_CURSOR_TRACKED);

	/*
	 * Relock the inode
	 */
	if (also_ip && (ip = cursor->ip) != NULL) {
		if (cursor->trans->type == HAMMER_TRANS_FLS)
			hammer_lock_ex(&ip->lock);
		else
			hammer_lock_sh(&ip->lock);
	}

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
	int error;

	hammer_unlock_cursor(cursor, 0);
	KKASSERT(cursor->trans->sync_lock_refs > 0);

	/*
	 * Wait for the deadlock to clear
	 */
	if (cursor->deadlk_node) {
		hammer_lock_ex_ident(&cursor->deadlk_node->lock, "hmrdlk");
		hammer_unlock(&cursor->deadlk_node->lock);
		hammer_rel_node(cursor->deadlk_node);
		cursor->deadlk_node = NULL;
	}
	if (cursor->deadlk_rec) {
		hammer_wait_mem_record_ident(cursor->deadlk_rec, "hmmdlr");
		hammer_rel_mem_record(cursor->deadlk_rec);
		cursor->deadlk_rec = NULL;
	}
	error = hammer_lock_cursor(cursor, 0);
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
	hammer_lock_cursor(ocursor, 0);
}

/*
 * onode is being replaced by nnode by the reblocking code.
 */
void
hammer_cursor_replaced_node(hammer_node_t onode, hammer_node_t nnode)
{
	hammer_cursor_t cursor;

	while ((cursor = TAILQ_FIRST(&onode->cursor_list)) != NULL) {
		TAILQ_REMOVE(&onode->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		KKASSERT(cursor->node == onode);
		cursor->node = nnode;
		hammer_ref_node(nnode);
		hammer_rel_node(onode);
	}
}

/*
 * node is being removed, cursors in deadlock recovery are seeked upward
 * to the parent.
 */
void
hammer_cursor_removed_node(hammer_node_t node, hammer_node_t parent, int index)
{
	hammer_cursor_t cursor;

	KKASSERT(parent != NULL);
	while ((cursor = TAILQ_FIRST(&node->cursor_list)) != NULL) {
		KKASSERT(cursor->node == node);
		KKASSERT(cursor->index == 0);
		TAILQ_REMOVE(&node->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&parent->cursor_list, cursor, deadlk_entry);
		cursor->flags |= HAMMER_CURSOR_TRACKED_RIPOUT;
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

again:
	TAILQ_FOREACH(cursor, &onode->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == onode);
		if (cursor->index < index)
			continue;
		TAILQ_REMOVE(&onode->cursor_list, cursor, deadlk_entry);
		TAILQ_INSERT_TAIL(&nnode->cursor_list, cursor, deadlk_entry);
		cursor->node = nnode;
		cursor->index -= index;
		hammer_ref_node(nnode);
		hammer_rel_node(onode);
		goto again;
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

	TAILQ_FOREACH(cursor, &node->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == node);
		if (cursor->index == index) {
			cursor->flags |= HAMMER_CURSOR_TRACKED_RIPOUT;
		} else if (cursor->index > index) {
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

	TAILQ_FOREACH(cursor, &node->cursor_list, deadlk_entry) {
		KKASSERT(cursor->node == node);
		if (cursor->index >= index)
			++cursor->index;
	}
}

