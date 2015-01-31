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
	int seq;

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

	/*
	 * Avoid deadlocking the buffer cache
	 */
	seq = trans->hmp->flusher.done;
	while (hammer_flusher_meta_halflimit(trans->hmp) ||
	       hammer_flusher_undo_exhausted(trans, 2)) {
		hammer_flusher_wait(trans->hmp, seq);
		seq = hammer_flusher_async_one(trans->hmp);
	}
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

/************************************************************************
 *				LIVE DEDUP 				*
 ************************************************************************
 *
 * HAMMER Live Dedup (aka as efficient cp(1) implementation)
 *
 * The dedup cache is operated in a LRU fashion and destroyed on
 * unmount, so essentially this is a live dedup on a cached dataset and
 * not a full-fledged fs-wide one - we have a batched dedup for that.
 * We allow duplicate entries in the buffer cache, data blocks are
 * deduplicated only on their way to media.  By default the cache is
 * populated on reads only, but it can be populated on writes too.
 *
 * The main implementation gotcha is on-media requirement - in order for
 * a data block to be added to a dedup cache it has to be present on
 * disk.  This simplifies cache logic a lot - once data is laid out on
 * media it remains valid on media all the way up to the point where the
 * related big block the data was stored in is freed - so there is only
 * one place we need to bother with invalidation code.
 */

/*
 * RB-Tree support for dedup cache structures
 */
RB_GENERATE2(hammer_dedup_crc_rb_tree, hammer_dedup_cache, crc_entry,
		hammer_dedup_crc_rb_compare, hammer_crc_t, crc);
RB_GENERATE2(hammer_dedup_off_rb_tree, hammer_dedup_cache, off_entry,
		hammer_dedup_off_rb_compare, hammer_off_t, data_offset);

struct hammer_dedup_inval {
	struct hammer_mount *hmp;
	hammer_off_t base_offset;
};

static int hammer_dedup_scancmp(hammer_dedup_cache_t dc, void *data);
static int hammer_dedup_cache_inval_callback(hammer_dedup_cache_t dc,
		void *data);
static __inline int _vnode_validate(hammer_dedup_cache_t dcp, void *data,
		int *errorp);
static __inline int _dev_validate(hammer_dedup_cache_t dcp, void *data,
		int *errorp);

int
hammer_dedup_crc_rb_compare(hammer_dedup_cache_t dc1,
				hammer_dedup_cache_t dc2)
{
	if (dc1->crc < dc2->crc)
		return (-1);
	if (dc1->crc > dc2->crc)
		return (1);

	return (0);
}

int
hammer_dedup_off_rb_compare(hammer_dedup_cache_t dc1,
				hammer_dedup_cache_t dc2)
{
	if (dc1->data_offset < dc2->data_offset)
		return (-1);
	if (dc1->data_offset > dc2->data_offset)
		return (1);

	return (0);
}

static int
hammer_dedup_scancmp(hammer_dedup_cache_t dc, void *data)
{
	hammer_off_t off = ((struct hammer_dedup_inval *)data)->base_offset;

	if (dc->data_offset < off)
		return (-1);
	if (dc->data_offset >= off + HAMMER_BIGBLOCK_SIZE)
		return (1);

	return (0);
}

static int
hammer_dedup_cache_inval_callback(hammer_dedup_cache_t dc, void *data)
{
	hammer_mount_t hmp = ((struct hammer_dedup_inval *)data)->hmp;

	RB_REMOVE(hammer_dedup_crc_rb_tree, &hmp->rb_dedup_crc_root, dc);
	RB_REMOVE(hammer_dedup_off_rb_tree, &hmp->rb_dedup_off_root, dc);
	TAILQ_REMOVE(&hmp->dedup_lru_list, dc, lru_entry);

	--hmp->dedup_cache_count;
	kfree(dc, hmp->m_misc);

	return (0);
}

hammer_dedup_cache_t
hammer_dedup_cache_add(hammer_inode_t ip, hammer_btree_leaf_elm_t leaf)
{
	hammer_dedup_cache_t dcp, tmp;
	hammer_mount_t hmp = ip->hmp;

	if (hmp->dedup_free_cache == NULL) {
		tmp = kmalloc(sizeof(*tmp), hmp->m_misc, M_WAITOK | M_ZERO);
		if (hmp->dedup_free_cache == NULL)
			hmp->dedup_free_cache = tmp;
		else
			kfree(tmp, hmp->m_misc);
	}

	KKASSERT(leaf != NULL);

	dcp = RB_LOOKUP(hammer_dedup_crc_rb_tree,
				&hmp->rb_dedup_crc_root, leaf->data_crc);
	if (dcp != NULL) {
		RB_REMOVE(hammer_dedup_off_rb_tree,
				&hmp->rb_dedup_off_root, dcp);
		TAILQ_REMOVE(&hmp->dedup_lru_list, dcp, lru_entry);
		goto populate;
	}

	if (hmp->dedup_cache_count < hammer_live_dedup_cache_size) {
		dcp = hmp->dedup_free_cache;
		hmp->dedup_free_cache = NULL;
		++hmp->dedup_cache_count;
	} else {
		dcp = TAILQ_FIRST(&hmp->dedup_lru_list);
		RB_REMOVE(hammer_dedup_crc_rb_tree,
				&hmp->rb_dedup_crc_root, dcp);
		RB_REMOVE(hammer_dedup_off_rb_tree,
				&hmp->rb_dedup_off_root, dcp);
		TAILQ_REMOVE(&hmp->dedup_lru_list, dcp, lru_entry);
	}

	dcp->crc = leaf->data_crc;
	tmp = RB_INSERT(hammer_dedup_crc_rb_tree, &hmp->rb_dedup_crc_root, dcp);
	KKASSERT(tmp == NULL);

populate:
	dcp->hmp = ip->hmp;
	dcp->obj_id = ip->obj_id;
	dcp->localization = ip->obj_localization;
	dcp->file_offset = leaf->base.key - leaf->data_len;
	dcp->bytes = leaf->data_len;
	dcp->data_offset = leaf->data_offset;

	tmp = RB_INSERT(hammer_dedup_off_rb_tree, &hmp->rb_dedup_off_root, dcp);
	KKASSERT(tmp == NULL);
	TAILQ_INSERT_TAIL(&hmp->dedup_lru_list, dcp, lru_entry);

	return (dcp);
}

__inline hammer_dedup_cache_t
hammer_dedup_cache_lookup(hammer_mount_t hmp, hammer_crc_t crc)
{
	hammer_dedup_cache_t dcp;

	dcp = RB_LOOKUP(hammer_dedup_crc_rb_tree,
				&hmp->rb_dedup_crc_root, crc);
	return dcp;
}

void hammer_dedup_cache_inval(hammer_mount_t hmp, hammer_off_t base_offset)
{
	struct hammer_dedup_inval di;

	di.hmp = hmp;
	di.base_offset = base_offset;

	RB_SCAN(hammer_dedup_off_rb_tree, &hmp->rb_dedup_off_root,
		hammer_dedup_scancmp, hammer_dedup_cache_inval_callback, &di);
}

void
hammer_destroy_dedup_cache(hammer_mount_t hmp)
{
	hammer_dedup_cache_t dcp;

	while ((dcp = TAILQ_FIRST(&hmp->dedup_lru_list)) != NULL) {
		RB_REMOVE(hammer_dedup_crc_rb_tree, &hmp->rb_dedup_crc_root, dcp);
		RB_REMOVE(hammer_dedup_off_rb_tree, &hmp->rb_dedup_off_root, dcp);
		TAILQ_REMOVE(&hmp->dedup_lru_list, dcp, lru_entry);
		--hmp->dedup_cache_count;
		kfree(dcp, hmp->m_misc);
	}

	KKASSERT(RB_EMPTY(&hmp->rb_dedup_crc_root));
	KKASSERT(RB_EMPTY(&hmp->rb_dedup_off_root));
	KKASSERT(TAILQ_EMPTY(&hmp->dedup_lru_list));

	KKASSERT(hmp->dedup_cache_count == 0);
}

int
hammer_dedup_validate(hammer_dedup_cache_t dcp, int zone, int bytes,
		      void *data)
{
	int error;

	/*
	 * Zone validation
	 */
	if (HAMMER_ZONE_DECODE(dcp->data_offset) != zone)
		return (0);

	/*
	 * Block length validation
	 */
	if (dcp->bytes != bytes)
		return (0);

	/*
	 * Byte-by-byte data comparison
	 *
	 * The data we need for validation may already be present in the
	 * buffer cache in two flavours: vnode-based buffer or
	 * block-device-based buffer.  In case vnode-based buffer wasn't
	 * there or if a non-blocking attempt to acquire it failed use
	 * device-based buffer (for large-zone data blocks it will
	 * generate a separate read).
	 *
	 * XXX vnode-based checking is not MP safe so when live-dedup
	 *     is turned on we must always use the device buffer.
	 */
#if 0
	if (hammer_double_buffer) {
		error = 1;
	} else if (_vnode_validate(dcp, data, &error)) {
		hammer_live_dedup_vnode_bcmps++;
		return (1);
	} else {
		if (error == 3)
			hammer_live_dedup_findblk_failures++;
	}

	/*
	 * If there was an error previously or if double buffering is
	 * enabled.
	 */
	if (error) {
		if (_dev_validate(dcp, data, &error)) {
			hammer_live_dedup_device_bcmps++;
			return (1);
		}
	}
#endif
	if (_dev_validate(dcp, data, &error)) {
		hammer_live_dedup_device_bcmps++;
		return (1);
	}

	return (0);
}

static __inline int
_vnode_validate(hammer_dedup_cache_t dcp, void *data, int *errorp)
{
	struct hammer_transaction trans;
	hammer_inode_t ip;
	struct vnode *vp;
	struct buf *bp;
	off_t dooffset;
	int result, error;

	result = error = 0;
	*errorp = 0;

	hammer_simple_transaction(&trans, dcp->hmp);

	ip = hammer_get_inode(&trans, NULL, dcp->obj_id, HAMMER_MAX_TID,
	    dcp->localization, 0, &error);
	if (ip == NULL) {
		kprintf("dedup: unable to find objid %016jx:%08x\n",
		    (intmax_t)dcp->obj_id, dcp->localization);
		*errorp = 1;
		goto failed2;
	}

	error = hammer_get_vnode(ip, &vp);
	if (error) {
		kprintf("dedup: unable to acquire vnode for %016jx:%08x\n",
		    (intmax_t)dcp->obj_id, dcp->localization);
		*errorp = 2;
		goto failed;
	}

	if ((bp = findblk(ip->vp, dcp->file_offset, FINDBLK_NBLOCK)) != NULL) {
		bremfree(bp);

		/* XXX if (mapped to userspace) goto done, *errorp = 4 */

		if ((bp->b_flags & B_CACHE) == 0 || bp->b_flags & B_DIRTY) {
			*errorp = 5;
			goto done;
		}

		if (bp->b_bio2.bio_offset != dcp->data_offset) {
			error = VOP_BMAP(ip->vp, dcp->file_offset, &dooffset,
			    NULL, NULL, BUF_CMD_READ);
			if (error) {
				*errorp = 6;
				goto done;
			}

			if (dooffset != dcp->data_offset) {
				*errorp = 7;
				goto done;
			}
			hammer_live_dedup_bmap_saves++;
		}

		if (bcmp(data, bp->b_data, dcp->bytes) == 0)
			result = 1;

done:
		bqrelse(bp);
	} else {
		*errorp = 3;
	}
	vput(vp);

failed:
	hammer_rel_inode(ip, 0);
failed2:
	hammer_done_transaction(&trans);
	return (result);
}

static __inline int
_dev_validate(hammer_dedup_cache_t dcp, void *data, int *errorp)
{
	hammer_buffer_t buffer = NULL;
	void *ondisk_data;
	int result, error;

	result = error = 0;
	*errorp = 0;

	ondisk_data = hammer_bread_ext(dcp->hmp, dcp->data_offset,
	    dcp->bytes, &error, &buffer);
	if (error) {
		*errorp = 1;
		goto failed;
	}

	if (bcmp(data, ondisk_data, dcp->bytes) == 0)
		result = 1;

failed:
	if (buffer)
		hammer_rel_buffer(buffer, 0);

	return (result);
}
