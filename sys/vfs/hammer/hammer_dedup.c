/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Ilya Dryomov <idryomov@gmail.com>
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

static __inline int validate_zone(hammer_off_t data_offset);

int
hammer_ioc_dedup(hammer_transaction_t trans, hammer_inode_t ip,
		 struct hammer_ioc_dedup *dedup)
{
	struct hammer_cursor cursor1, cursor2;
	int error;

	/*
	 * Enforce hammer filesystem version requirements
	 */
	if (trans->hmp->version < HAMMER_VOL_VERSION_FIVE) {
		kprintf("hammer: Filesystem must be upgraded to v5 "
			"before you can run dedup\n");
		return (EOPNOTSUPP);
	}

	/*
	 * Cursor1, return an error -> candidate goes to pass2 list
	 */
	error = hammer_init_cursor(trans, &cursor1, NULL, NULL);
	if (error)
		goto done_cursor;
	cursor1.key_beg = dedup->elm1;
	cursor1.flags |= HAMMER_CURSOR_BACKEND;

	error = hammer_btree_lookup(&cursor1);
	if (error)
		goto done_cursor;
	error = hammer_btree_extract(&cursor1, HAMMER_CURSOR_GET_LEAF |
						HAMMER_CURSOR_GET_DATA);
	if (error)
		goto done_cursor;

	/*
	 * Cursor2, return an error -> candidate goes to pass2 list
	 */
	error = hammer_init_cursor(trans, &cursor2, NULL, NULL);
	if (error)
		goto done_cursors;
	cursor2.key_beg = dedup->elm2;
	cursor2.flags |= HAMMER_CURSOR_BACKEND;

	error = hammer_btree_lookup(&cursor2);
	if (error)
		goto done_cursors;
	error = hammer_btree_extract(&cursor2, HAMMER_CURSOR_GET_LEAF |
						HAMMER_CURSOR_GET_DATA);
	if (error)
		goto done_cursors;

	/*
	 * Zone validation. We can't de-dup any of the other zones
	 * (BTREE or META) or bad things will happen.
	 *
	 * Return with error = 0, but set an INVALID_ZONE flag.
	 */
	error = validate_zone(cursor1.leaf->data_offset) +
			    validate_zone(cursor2.leaf->data_offset);
	if (error) {
		dedup->head.flags |= HAMMER_IOC_DEDUP_INVALID_ZONE;
		error = 0;
		goto done_cursors;
	}

	/*
	 * Comparison checks
	 *
	 * If zones don't match or data_len fields aren't the same
	 * we consider it to be a comparison failure.
	 *
	 * Return with error = 0, but set a CMP_FAILURE flag.
	 */
	if ((cursor1.leaf->data_offset & HAMMER_OFF_ZONE_MASK) !=
	    (cursor2.leaf->data_offset & HAMMER_OFF_ZONE_MASK)) {
		dedup->head.flags |= HAMMER_IOC_DEDUP_CMP_FAILURE;
		goto done_cursors;
	}
	if (cursor1.leaf->data_len != cursor2.leaf->data_len) {
		dedup->head.flags |= HAMMER_IOC_DEDUP_CMP_FAILURE;
		goto done_cursors;
	}

	/* byte-by-byte comparison to be sure */
	if (bcmp(cursor1.data, cursor2.data, cursor1.leaf->data_len)) {
		dedup->head.flags |= HAMMER_IOC_DEDUP_CMP_FAILURE;
		goto done_cursors;
	}

	/*
	 * Upgrade both cursors together to an exclusive lock
	 *
	 * Return an error -> candidate goes to pass2 list
	 */
	hammer_sync_lock_sh(trans);
	error = hammer_cursor_upgrade2(&cursor1, &cursor2);
	if (error) {
		hammer_sync_unlock(trans);
		goto done_cursors;
	}

	error = hammer_blockmap_dedup(cursor1.trans,
			cursor1.leaf->data_offset, cursor1.leaf->data_len);
	if (error) {
		if (error == ERANGE) {
			/*
			 * Return with error = 0, but set an UNDERFLOW flag
			 */
			dedup->head.flags |= HAMMER_IOC_DEDUP_UNDERFLOW;
			error = 0;
			goto downgrade_cursors;
		} else {
			/*
			 * Return an error -> block goes to pass2 list
			 */
			goto downgrade_cursors;
		}
	}

	/*
	 * The cursor2's cache must be invalidated before calling
	 * hammer_blockmap_free(), otherwise it will not be able to
	 * invalidate the underlying data buffer.
	 */
	hammer_cursor_invalidate_cache(&cursor2);
	hammer_blockmap_free(cursor2.trans,
			cursor2.leaf->data_offset, cursor2.leaf->data_len);

	hammer_modify_node(cursor2.trans, cursor2.node,
			&cursor2.leaf->data_offset, sizeof(hammer_off_t));
	cursor2.leaf->data_offset = cursor1.leaf->data_offset;
	hammer_modify_node_done(cursor2.node);

downgrade_cursors:
	hammer_cursor_downgrade2(&cursor1, &cursor2);
	hammer_sync_unlock(trans);
done_cursors:
	hammer_done_cursor(&cursor2);
done_cursor:
	hammer_done_cursor(&cursor1);
	return (error);
}

static __inline int
validate_zone(hammer_off_t data_offset)
{
	switch(data_offset & HAMMER_OFF_ZONE_MASK) {
	case HAMMER_ZONE_LARGE_DATA:
	case HAMMER_ZONE_SMALL_DATA:
		return (0);
	default:
		return (1);
	}
}
