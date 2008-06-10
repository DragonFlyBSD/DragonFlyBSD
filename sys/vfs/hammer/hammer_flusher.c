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
 * $DragonFly: src/sys/vfs/hammer/hammer_flusher.c,v 1.22 2008/06/10 05:06:20 dillon Exp $
 */
/*
 * HAMMER dependancy flusher thread
 *
 * Meta data updates create buffer dependancies which are arranged as a
 * hierarchy of lists.
 */

#include "hammer.h"

static void hammer_flusher_thread(void *arg);
static void hammer_flusher_clean_loose_ios(hammer_mount_t hmp);
static void hammer_flusher_flush(hammer_mount_t hmp);
static void hammer_flusher_flush_inode(hammer_inode_t ip,
					hammer_transaction_t trans);
static int hammer_must_finalize_undo(hammer_mount_t hmp);
static void hammer_flusher_finalize(hammer_transaction_t trans, int final);

#define HAMMER_FLUSHER_IMMEDIATE	16

void
hammer_flusher_sync(hammer_mount_t hmp)
{
	int seq;

	if (hmp->flusher_td) {
		seq = hmp->flusher_next;
		if (hmp->flusher_signal++ == 0)
			wakeup(&hmp->flusher_signal);
		while ((int)(seq - hmp->flusher_done) > 0)
			tsleep(&hmp->flusher_done, 0, "hmrfls", 0);
	}
}

void
hammer_flusher_async(hammer_mount_t hmp)
{
	if (hmp->flusher_td) {
		if (hmp->flusher_signal++ == 0)
			wakeup(&hmp->flusher_signal);
	}
}

void
hammer_flusher_create(hammer_mount_t hmp)
{
	hmp->flusher_signal = 0;
	hmp->flusher_act = 0;
	hmp->flusher_done = 0;
	hmp->flusher_next = 1;
	lwkt_create(hammer_flusher_thread, hmp, &hmp->flusher_td, NULL,
		    0, -1, "hammer");
}

void
hammer_flusher_destroy(hammer_mount_t hmp)
{
	if (hmp->flusher_td) {
		hmp->flusher_exiting = 1;
		while (hmp->flusher_td) {
			++hmp->flusher_signal;
			wakeup(&hmp->flusher_signal);
			tsleep(&hmp->flusher_exiting, 0, "hmrwex", 0);
		}
	}
}

static void
hammer_flusher_thread(void *arg)
{
	hammer_mount_t hmp = arg;

	for (;;) {
		while (hmp->flusher_lock)
			tsleep(&hmp->flusher_lock, 0, "hmrhld", 0);
		kprintf("S");
		hmp->flusher_act = hmp->flusher_next;
		++hmp->flusher_next;
		hammer_flusher_clean_loose_ios(hmp);
		hammer_flusher_flush(hmp);
		hammer_flusher_clean_loose_ios(hmp);
		hmp->flusher_done = hmp->flusher_act;

		wakeup(&hmp->flusher_done);

		/*
		 * Wait for activity.
		 */
		if (hmp->flusher_exiting && TAILQ_EMPTY(&hmp->flush_list))
			break;

		/*
		 * This is a hack until we can dispose of frontend buffer
		 * cache buffers on the frontend.
		 */
		while (hmp->flusher_signal == 0)
			tsleep(&hmp->flusher_signal, 0, "hmrwwa", 0);
		hmp->flusher_signal = 0;
	}
	hmp->flusher_td = NULL;
	wakeup(&hmp->flusher_exiting);
	lwkt_exit();
}

static void
hammer_flusher_clean_loose_ios(hammer_mount_t hmp)
{
	hammer_buffer_t buffer;
	hammer_io_t io;

	/*
	 * loose ends - buffers without bp's aren't tracked by the kernel
	 * and can build up, so clean them out.  This can occur when an
	 * IO completes on a buffer with no references left.
	 */
	while ((io = TAILQ_FIRST(&hmp->lose_list)) != NULL) {
		KKASSERT(io->mod_list == &hmp->lose_list);
		TAILQ_REMOVE(io->mod_list, io, mod_entry);
		io->mod_list = NULL;
		hammer_ref(&io->lock);
		buffer = (void *)io;
		hammer_rel_buffer(buffer, 0);
	}
}

/*
 * Flush all inodes in the current flush group.
 */
static void
hammer_flusher_flush(hammer_mount_t hmp)
{
	struct hammer_transaction trans;
	hammer_inode_t ip;
	hammer_reserve_t resv;

	/*
	 * Flush the inodes
	 */
	hammer_start_transaction_fls(&trans, hmp);
	while ((ip = TAILQ_FIRST(&hmp->flush_list)) != NULL) {
		if (ip->flush_group != hmp->flusher_act)
			break;
		TAILQ_REMOVE(&hmp->flush_list, ip, flush_entry);
		hammer_flusher_flush_inode(ip, &trans);
	}
	hammer_flusher_finalize(&trans, 1);
	hmp->flusher_tid = trans.tid;

	/*
	 * Clean up any freed big-blocks (typically zone-2). 
	 * resv->flush_group is typically set several flush groups ahead
	 * of the free to ensure that the freed block is not reused until
	 * it can no longer be reused.
	 */
	while ((resv = TAILQ_FIRST(&hmp->delay_list)) != NULL) {
		if (resv->flush_group != hmp->flusher_act)
			break;
		TAILQ_REMOVE(&hmp->delay_list, resv, delay_entry);
		hammer_blockmap_reserve_complete(hmp, resv);
	}


	hammer_done_transaction(&trans);
}

/*
 * Flush a single inode that is part of a flush group.
 */
static
void
hammer_flusher_flush_inode(hammer_inode_t ip, hammer_transaction_t trans)
{
	hammer_mount_t hmp = ip->hmp;

	/*hammer_lock_ex(&ip->lock);*/
	ip->error = hammer_sync_inode(ip);
	hammer_flush_inode_done(ip);
	/*hammer_unlock(&ip->lock);*/

	if (hammer_must_finalize_undo(hmp)) {
		kprintf("HAMMER: Warning: UNDO area too small!");
		hammer_flusher_finalize(trans, 1);
	} else if (trans->hmp->locked_dirty_count +
		   trans->hmp->io_running_count > hammer_limit_dirtybufs) {
		kprintf("t");
		hammer_flusher_finalize(trans, 0);
	}
}

/*
 * If the UNDO area gets over half full we have to flush it.  We can't
 * afford the UNDO area becoming completely full as that would break
 * the crash recovery atomicy.
 */
static
int
hammer_must_finalize_undo(hammer_mount_t hmp)
{
	if (hammer_undo_space(hmp) < hammer_undo_max(hmp) / 2) {
		hkprintf("*");
		return(1);
	} else {
		return(0);
	}
}

/*
 * Flush all pending UNDOs, wait for write completion, update the volume
 * header with the new UNDO end position, and flush it.  Then
 * asynchronously flush the meta-data.
 *
 * If this is the last finalization in a flush group we also synchronize
 * our cached blockmap and set hmp->flusher_undo_start and our cached undo
 * fifo first_offset so the next flush resets the FIFO pointers.
 */
static
void
hammer_flusher_finalize(hammer_transaction_t trans, int final)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t cundomap, dundomap;
	hammer_mount_t hmp;
	hammer_io_t io;
	int count;
	int i;

	hmp = trans->hmp;
	root_volume = trans->rootvol;

	/*
	 * Flush data buffers.  This can occur asynchronously and at any
	 * time.  We must interlock against the frontend direct-data write
	 * but do not have to acquire the sync-lock yet.
	 */
	count = 0;
	while ((io = TAILQ_FIRST(&hmp->data_list)) != NULL) {
		hammer_ref(&io->lock);
		hammer_io_write_interlock(io);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_flush(io);
		hammer_io_done_interlock(io);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		++count;
	}

	/*
	 * The sync-lock is required for the remaining sequence.  This lock
	 * prevents meta-data from being modified.
	 */
	hammer_sync_lock_ex(trans);

	/*
	 * If we have been asked to finalize the volume header sync the
	 * cached blockmap to the on-disk blockmap.  Generate an UNDO
	 * record for the update.
	 */
	if (final) {
		cundomap = &hmp->blockmap[0];
		dundomap = &root_volume->ondisk->vol0_blockmap[0];
		if (root_volume->io.modified) {
			hammer_modify_volume(trans, root_volume,
					     dundomap, sizeof(hmp->blockmap));
			for (i = 0; i < HAMMER_MAX_ZONES; ++i)
				hammer_crc_set_blockmap(&cundomap[i]);
			bcopy(cundomap, dundomap, sizeof(hmp->blockmap));
			hammer_modify_volume_done(root_volume);
		}
	}

	/*
	 * Flush UNDOs
	 */
	count = 0;
	while ((io = TAILQ_FIRST(&hmp->undo_list)) != NULL) {
		KKASSERT(io->modify_refs == 0);
		hammer_ref(&io->lock);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_flush(io);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		++count;
	}

	/*
	 * Wait for I/Os to complete
	 */
	crit_enter();
	while (hmp->io_running_count)
		tsleep(&hmp->io_running_count, 0, "hmrfl1", 0);
	crit_exit();

	/*
	 * Update the on-disk volume header with new UNDO FIFO end position
	 * (do not generate new UNDO records for this change).  We have to
	 * do this for the UNDO FIFO whether (final) is set or not.
	 *
	 * Also update the on-disk next_tid field.  This does not require
	 * an UNDO.  However, because our TID is generated before we get
	 * the sync lock another sync may have beat us to the punch.
	 *
	 * The volume header will be flushed out synchronously.
	 */
	dundomap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	cundomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];

	if (dundomap->first_offset != cundomap->first_offset ||
	    dundomap->next_offset != cundomap->next_offset) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		dundomap->first_offset = cundomap->first_offset;
		dundomap->next_offset = cundomap->next_offset;
		hammer_crc_set_blockmap(dundomap);
		hammer_crc_set_volume(root_volume->ondisk);
		if (root_volume->ondisk->vol0_next_tid < trans->tid)
			root_volume->ondisk->vol0_next_tid = trans->tid;
		hammer_modify_volume_done(root_volume);
	}

	if (root_volume->io.modified) {
		hammer_io_flush(&root_volume->io);
	}

	/*
	 * Wait for I/Os to complete
	 */
	crit_enter();
	while (hmp->io_running_count)
		tsleep(&hmp->io_running_count, 0, "hmrfl2", 0);
	crit_exit();

	/*
	 * Flush meta-data.  The meta-data will be undone if we crash
	 * so we can safely flush it asynchronously.
	 *
	 * Repeated catchups will wind up flushing this update's meta-data
	 * and the UNDO buffers for the next update simultaniously.  This
	 * is ok.
	 */
	count = 0;
	while ((io = TAILQ_FIRST(&hmp->meta_list)) != NULL) {
		KKASSERT(io->modify_refs == 0);
		hammer_ref(&io->lock);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_flush(io);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		++count;
	}

	/*
	 * If this is the final finalization for the flush group set
	 * up for the next sequence by setting a new first_offset in
	 * our cached blockmap and
	 * clearing the undo history.
	 */
	if (final) {
		cundomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
		cundomap->first_offset = cundomap->next_offset;
		hammer_clear_undo_history(hmp);
	}

	hammer_sync_unlock(trans);
}

