/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_mirror.c,v 1.1 2008/06/24 17:38:17 dillon Exp $
 */
/*
 * HAMMER mirroring ioctls - serialize and deserialize modifications made
 *			     to a filesystem.
 */

#include "hammer.h"

int
hammer_ioc_mirror_read(hammer_transaction_t trans, hammer_inode_t ip,
		       struct hammer_ioc_mirror_rw *mirror)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;

	if ((mirror->key_beg.localization | mirror->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (hammer_btree_cmp(&mirror->key_beg, &mirror->key_end) > 0)
		return(EINVAL);

	mirror->key_cur = mirror->key_beg;
	mirror->key_cur.localization += ip->obj_localization;

retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg = mirror->key_cur;
	cursor.key_end = mirror->key_end;
	cursor.key_end.localization += ip->obj_localization;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * This flag allows the btree scan code to return internal nodes
	 * at every index, giving the mirroring code the ability to skip
	 * whole sub-trees based on mirror_tid.
	 */
	cursor.flags |= HAMMER_CURSOR_MIRRORING;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		/*
		 * Internal or Leaf node
		 */
		elm = &cursor.node->ondisk->elms[cursor.index];
		reblock->key_cur.obj_id = elm->base.obj_id;
		reblock->key_cur.localization = elm->base.localization;

		/*
		 * Yield to more important tasks
		 */
		if ((error = hammer_signal_check(trans->hmp)) != 0)
			break;
		if (trans->hmp->sync_lock.wanted) {
			tsleep(trans, 0, "hmrslo", hz / 10);
		}
		if (trans->hmp->locked_dirty_count +
		    trans->hmp->io_running_count > hammer_limit_dirtybufs) {
			hammer_flusher_async(trans->hmp);
			tsleep(trans, 0, "hmrslo", hz / 10);
		}

#if 0
		/*
		 * Acquiring the sync_lock prevents the operation from
		 * crossing a synchronization boundary.
		 *
		 * NOTE: cursor.node may have changed on return.
		 */
		hammer_sync_lock_sh(trans);
		error = hammer_reblock_helper(reblock, &cursor, elm);
		hammer_sync_unlock(trans);
#endif
		if (error == 0) {
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			error = hammer_btree_iterate(&cursor);
		}
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	if (error == EINTR) {
		reblock->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
failed:
	mirror->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	return(error);
}

