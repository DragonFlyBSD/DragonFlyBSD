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
 * $DragonFly: src/sys/vfs/hammer/hammer_flusher.c,v 1.45 2008/07/31 04:42:04 dillon Exp $
 */
/*
 * HAMMER dependancy flusher thread
 *
 * Meta data updates create buffer dependancies which are arranged as a
 * hierarchy of lists.
 */

#include "hammer.h"

static void hammer_flusher_master_thread(void *arg);
static void hammer_flusher_slave_thread(void *arg);
static int hammer_flusher_flush(hammer_mount_t hmp, int *nomorep);
static int hammer_flusher_flush_inode(hammer_inode_t ip, void *data);

RB_GENERATE(hammer_fls_rb_tree, hammer_inode, rb_flsnode,
              hammer_ino_rb_compare);

/*
 * Support structures for the flusher threads.
 */
struct hammer_flusher_info {
	TAILQ_ENTRY(hammer_flusher_info) entry;
	struct hammer_mount *hmp;
	thread_t	td;
	int		runstate;
	int		count;
	hammer_flush_group_t flg;
	struct hammer_transaction trans;        /* per-slave transaction */
};

typedef struct hammer_flusher_info *hammer_flusher_info_t;

/*
 * Sync all inodes pending on the flusher.
 *
 * All flush groups will be flushed.  This does not queue dirty inodes
 * to the flush groups, it just flushes out what has already been queued!
 */
void
hammer_flusher_sync(hammer_mount_t hmp)
{
	int seq;

	seq = hammer_flusher_async(hmp, NULL);
	hammer_flusher_wait(hmp, seq);
}

/*
 * Sync all flush groups through to close_flg - return immediately.
 * If close_flg is NULL all flush groups are synced.
 *
 * Returns the sequence number of the last closed flush group,
 * which may be close_flg.  When syncing to the end if there
 * are no flush groups pending we still cycle the flusher, and
 * must allocate a sequence number to placemark the spot even
 * though no flush group will ever be associated with it.
 */
int
hammer_flusher_async(hammer_mount_t hmp, hammer_flush_group_t close_flg)
{
	hammer_flush_group_t flg;
	int seq;

	/*
	 * Already closed
	 */
	if (close_flg && close_flg->closed)
		return(close_flg->seq);

	/*
	 * Close flush groups until we hit the end of the list
	 * or close_flg.
	 */
	while ((flg = hmp->next_flush_group) != NULL) {
		KKASSERT(flg->closed == 0 && flg->running == 0);
		flg->closed = 1;
		hmp->next_flush_group = TAILQ_NEXT(flg, flush_entry);
		if (flg == close_flg)
			break;
	}

	if (hmp->flusher.td) {
		if (hmp->flusher.signal++ == 0)
			wakeup(&hmp->flusher.signal);
		if (flg) {
			seq = flg->seq;
		} else {
			seq = hmp->flusher.next;
			++hmp->flusher.next;
		}
	} else {
		seq = hmp->flusher.done;
	}
	return(seq);
}

/*
 * Flush the current/next flushable flg.  This function is typically called
 * in a loop along with hammer_flusher_wait(hmp, returned_seq) to iterate
 * flush groups until specific conditions are met.
 *
 * If a flush is currently in progress its seq is returned.
 *
 * If no flush is currently in progress the next available flush group
 * will be flushed and its seq returned.
 *
 * If no flush groups are present a dummy seq will be allocated and
 * returned and the flusher will be activated (e.g. to flush the
 * undo/redo and the volume header).
 */
int
hammer_flusher_async_one(hammer_mount_t hmp)
{
	hammer_flush_group_t flg;
	int seq;

	if (hmp->flusher.td) {
		flg = TAILQ_FIRST(&hmp->flush_group_list);
		seq = hammer_flusher_async(hmp, flg);
	} else {
		seq = hmp->flusher.done;
	}
	return(seq);
}

/*
 * Wait for the flusher to finish flushing the specified sequence
 * number.  The flush is already running and will signal us on
 * each completion.
 */
void
hammer_flusher_wait(hammer_mount_t hmp, int seq)
{
	while ((int)(seq - hmp->flusher.done) > 0)
		tsleep(&hmp->flusher.done, 0, "hmrfls", 0);
}

/*
 * Returns non-zero if the flusher is currently running.  Used for
 * time-domain multiplexing of frontend operations in order to avoid
 * starving the backend flusher.
 */
int
hammer_flusher_running(hammer_mount_t hmp)
{
	int seq = hmp->flusher.next - 1;
	if ((int)(seq - hmp->flusher.done) > 0)
		return(1);
	return (0);
}

void
hammer_flusher_wait_next(hammer_mount_t hmp)
{
	int seq;

	seq = hammer_flusher_async_one(hmp);
	hammer_flusher_wait(hmp, seq);
}

void
hammer_flusher_create(hammer_mount_t hmp)
{
	hammer_flusher_info_t info;
	int i;

	hmp->flusher.signal = 0;
	hmp->flusher.done = 0;
	hmp->flusher.next = 1;
	hammer_ref(&hmp->flusher.finalize_lock);
	TAILQ_INIT(&hmp->flusher.run_list);
	TAILQ_INIT(&hmp->flusher.ready_list);

	lwkt_create(hammer_flusher_master_thread, hmp,
		    &hmp->flusher.td, NULL, 0, -1, "hammer-M");
	for (i = 0; i < HAMMER_MAX_FLUSHERS; ++i) {
		info = kmalloc(sizeof(*info), hmp->m_misc, M_WAITOK|M_ZERO);
		info->hmp = hmp;
		TAILQ_INSERT_TAIL(&hmp->flusher.ready_list, info, entry);
		lwkt_create(hammer_flusher_slave_thread, info,
			    &info->td, NULL, 0, -1, "hammer-S%d", i);
	}
}

void
hammer_flusher_destroy(hammer_mount_t hmp)
{
	hammer_flusher_info_t info;

	/*
	 * Kill the master
	 */
	hmp->flusher.exiting = 1;
	while (hmp->flusher.td) {
		++hmp->flusher.signal;
		wakeup(&hmp->flusher.signal);
		tsleep(&hmp->flusher.exiting, 0, "hmrwex", hz);
	}

	/*
	 * Kill the slaves
	 */
	while ((info = TAILQ_FIRST(&hmp->flusher.ready_list)) != NULL) {
		KKASSERT(info->runstate == 0);
		TAILQ_REMOVE(&hmp->flusher.ready_list, info, entry);
		info->runstate = -1;
		wakeup(&info->runstate);
		while (info->td)
			tsleep(&info->td, 0, "hmrwwc", 0);
		kfree(info, hmp->m_misc);
	}
}

/*
 * The master flusher thread manages the flusher sequence id and
 * synchronization with the slave work threads.
 */
static void
hammer_flusher_master_thread(void *arg)
{
	hammer_mount_t hmp;
	int seq;
	int nomore;

	hmp = arg;

	lwkt_gettoken(&hmp->fs_token);

	for (;;) {
		/*
		 * Flush all sequence numbers up to but not including .next,
		 * or until an open flush group is encountered.
		 */
		for (;;) {
			while (hmp->flusher.group_lock)
				tsleep(&hmp->flusher.group_lock, 0, "hmrhld",0);
			hammer_flusher_clean_loose_ios(hmp);

			seq = hammer_flusher_flush(hmp, &nomore);
			hmp->flusher.done = seq;
			wakeup(&hmp->flusher.done);

			if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
				break;
			if (nomore)
				break;
		}

		/*
		 * Wait for activity.
		 */
		if (hmp->flusher.exiting && TAILQ_EMPTY(&hmp->flush_group_list))
			break;
		while (hmp->flusher.signal == 0)
			tsleep(&hmp->flusher.signal, 0, "hmrwwa", 0);
		hmp->flusher.signal = 0;
	}

	/*
	 * And we are done.
	 */
	hmp->flusher.td = NULL;
	wakeup(&hmp->flusher.exiting);
	lwkt_reltoken(&hmp->fs_token);
	lwkt_exit();
}

/*
 * Flush the next sequence number until an open flush group is encountered
 * or we reach (next).  Not all sequence numbers will have flush groups
 * associated with them.  These require that the UNDO/REDO FIFO still be
 * flushed since it can take at least one additional run to synchronize
 * the FIFO, and more to also synchronize the reserve structures.
 */
static int
hammer_flusher_flush(hammer_mount_t hmp, int *nomorep)
{
	hammer_flusher_info_t info;
	hammer_flush_group_t flg;
	hammer_reserve_t resv;
	int count;
	int seq;

	/*
	 * Just in-case there's a flush race on mount.  Seq number
	 * does not change.
	 */
	if (TAILQ_FIRST(&hmp->flusher.ready_list) == NULL) {
		*nomorep = 1;
		return (hmp->flusher.done);
	}
	*nomorep = 0;

	/*
	 * Flush the next sequence number.  Sequence numbers can exist
	 * without an assigned flush group, indicating that just a FIFO flush
	 * should occur.
	 */
	seq = hmp->flusher.done + 1;
	flg = TAILQ_FIRST(&hmp->flush_group_list);
	if (flg == NULL) {
		if (seq == hmp->flusher.next) {
			*nomorep = 1;
			return (hmp->flusher.done);
		}
	} else if (seq == flg->seq) {
		if (flg->closed) {
			KKASSERT(flg->running == 0);
			flg->running = 1;
			if (hmp->fill_flush_group == flg) {
				hmp->fill_flush_group =
					TAILQ_NEXT(flg, flush_entry);
			}
		} else {
			*nomorep = 1;
			return (hmp->flusher.done);
		}
	} else {
		KKASSERT((int)(flg->seq - seq) > 0);
		flg = NULL;
	}

	/*
	 * We only do one flg but we may have to loop/retry.
	 *
	 * Due to various races it is possible to come across a flush
	 * group which as not yet been closed.
	 */
	count = 0;
	while (flg && flg->running) {
		++count;
		if (hammer_debug_general & 0x0001) {
			kprintf("hammer_flush %d ttl=%d recs=%d\n",
				flg->seq, flg->total_count, flg->refs);
		}
		if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
			break;
		hammer_start_transaction_fls(&hmp->flusher.trans, hmp);

		/*
		 * If the previous flush cycle just about exhausted our
		 * UNDO space we may have to do a dummy cycle to move the
		 * first_offset up before actually digging into a new cycle,
		 * or the new cycle will not have sufficient undo space.
		 */
		if (hammer_flusher_undo_exhausted(&hmp->flusher.trans, 3))
			hammer_flusher_finalize(&hmp->flusher.trans, 0);

		KKASSERT(hmp->next_flush_group != flg);

		/*
		 * Place the flg in the flusher structure and start the
		 * slaves running.  The slaves will compete for inodes
		 * to flush.
		 *
		 * Make a per-thread copy of the transaction.
		 */
		while ((info = TAILQ_FIRST(&hmp->flusher.ready_list)) != NULL) {
			TAILQ_REMOVE(&hmp->flusher.ready_list, info, entry);
			info->flg = flg;
			info->runstate = 1;
			info->trans = hmp->flusher.trans;
			TAILQ_INSERT_TAIL(&hmp->flusher.run_list, info, entry);
			wakeup(&info->runstate);
		}

		/*
		 * Wait for all slaves to finish running
		 */
		while (TAILQ_FIRST(&hmp->flusher.run_list) != NULL)
			tsleep(&hmp->flusher.ready_list, 0, "hmrfcc", 0);

		/*
		 * Do the final finalization, clean up
		 */
		hammer_flusher_finalize(&hmp->flusher.trans, 1);
		hmp->flusher.tid = hmp->flusher.trans.tid;

		hammer_done_transaction(&hmp->flusher.trans);

		/*
		 * Loop up on the same flg.  If the flg is done clean it up
		 * and break out.  We only flush one flg.
		 */
		if (RB_EMPTY(&flg->flush_tree)) {
			KKASSERT(flg->refs == 0);
			TAILQ_REMOVE(&hmp->flush_group_list, flg, flush_entry);
			kfree(flg, hmp->m_misc);
			break;
		}
		KKASSERT(TAILQ_FIRST(&hmp->flush_group_list) == flg);
	}

	/*
	 * We may have pure meta-data to flush, or we may have to finish
	 * cycling the UNDO FIFO, even if there were no flush groups.
	 */
	if (count == 0 && hammer_flusher_haswork(hmp)) {
		hammer_start_transaction_fls(&hmp->flusher.trans, hmp);
		hammer_flusher_finalize(&hmp->flusher.trans, 1);
		hammer_done_transaction(&hmp->flusher.trans);
	}

	/*
	 * Clean up any freed big-blocks (typically zone-2). 
	 * resv->flush_group is typically set several flush groups ahead
	 * of the free to ensure that the freed block is not reused until
	 * it can no longer be reused.
	 */
	while ((resv = TAILQ_FIRST(&hmp->delay_list)) != NULL) {
		if ((int)(resv->flush_group - seq) > 0)
			break;
		hammer_reserve_clrdelay(hmp, resv);
	}
	return (seq);
}


/*
 * The slave flusher thread pulls work off the master flush list until no
 * work is left.
 */
static void
hammer_flusher_slave_thread(void *arg)
{
	hammer_flush_group_t flg;
	hammer_flusher_info_t info;
	hammer_mount_t hmp;

	info = arg;
	hmp = info->hmp;
	lwkt_gettoken(&hmp->fs_token);

	for (;;) {
		while (info->runstate == 0)
			tsleep(&info->runstate, 0, "hmrssw", 0);
		if (info->runstate < 0)
			break;
		flg = info->flg;

		RB_SCAN(hammer_fls_rb_tree, &flg->flush_tree, NULL,
			hammer_flusher_flush_inode, info);

		info->count = 0;
		info->runstate = 0;
		info->flg = NULL;
		TAILQ_REMOVE(&hmp->flusher.run_list, info, entry);
		TAILQ_INSERT_TAIL(&hmp->flusher.ready_list, info, entry);
		wakeup(&hmp->flusher.ready_list);
	}
	info->td = NULL;
	wakeup(&info->td);
	lwkt_reltoken(&hmp->fs_token);
	lwkt_exit();
}

void
hammer_flusher_clean_loose_ios(hammer_mount_t hmp)
{
	hammer_buffer_t buffer;
	hammer_io_t io;

	/*
	 * loose ends - buffers without bp's aren't tracked by the kernel
	 * and can build up, so clean them out.  This can occur when an
	 * IO completes on a buffer with no references left.
	 *
	 * The io_token is needed to protect the list.
	 */
	if ((io = RB_ROOT(&hmp->lose_root)) != NULL) {
		lwkt_gettoken(&hmp->io_token);
		while ((io = RB_ROOT(&hmp->lose_root)) != NULL) {
			KKASSERT(io->mod_root == &hmp->lose_root);
			RB_REMOVE(hammer_mod_rb_tree, io->mod_root, io);
			io->mod_root = NULL;
			hammer_ref(&io->lock);
			buffer = (void *)io;
			hammer_rel_buffer(buffer, 0);
		}
		lwkt_reltoken(&hmp->io_token);
	}
}

/*
 * Flush a single inode that is part of a flush group.
 *
 * Flusher errors are extremely serious, even ENOSPC shouldn't occur because
 * the front-end should have reserved sufficient space on the media.  Any
 * error other then EWOULDBLOCK will force the mount to be read-only.
 */
static
int
hammer_flusher_flush_inode(hammer_inode_t ip, void *data)
{
	hammer_flusher_info_t info = data;
	hammer_mount_t hmp = info->hmp;
	hammer_transaction_t trans = &info->trans;
	int error;

	/*
	 * Several slaves are operating on the same flush group concurrently.
	 * The SLAVEFLUSH flag prevents them from tripping over each other.
	 *
	 * NOTE: It is possible for a EWOULDBLOCK'd ip returned by one slave
	 *	 to be resynced by another, but normally such inodes are not
	 *	 revisited until the master loop gets to them.
	 */
	if (ip->flags & HAMMER_INODE_SLAVEFLUSH)
		return(0);
	ip->flags |= HAMMER_INODE_SLAVEFLUSH;
	++hammer_stats_inode_flushes;

	hammer_flusher_clean_loose_ios(hmp);
	vm_wait_nominal();
	error = hammer_sync_inode(trans, ip);

	/*
	 * EWOULDBLOCK can happen under normal operation, all other errors
	 * are considered extremely serious.  We must set WOULDBLOCK
	 * mechanics to deal with the mess left over from the abort of the
	 * previous flush.
	 */
	if (error) {
		ip->flags |= HAMMER_INODE_WOULDBLOCK;
		if (error == EWOULDBLOCK)
			error = 0;
	}
	hammer_flush_inode_done(ip, error);
	/* ip invalid */

	while (hmp->flusher.finalize_want)
		tsleep(&hmp->flusher.finalize_want, 0, "hmrsxx", 0);
	if (hammer_flusher_undo_exhausted(trans, 1)) {
		kprintf("HAMMER: Warning: UNDO area too small!\n");
		hammer_flusher_finalize(trans, 1);
	} else if (hammer_flusher_meta_limit(trans->hmp)) {
		hammer_flusher_finalize(trans, 0);
	}
	return (0);
}

/*
 * Return non-zero if the UNDO area has less then (QUARTER / 4) of its
 * space left.
 *
 * 1/4 - Emergency free undo space level.  Below this point the flusher
 *	 will finalize even if directory dependancies have not been resolved.
 *
 * 2/4 - Used by the pruning and reblocking code.  These functions may be
 *	 running in parallel with a flush and cannot be allowed to drop
 *	 available undo space to emergency levels.
 *
 * 3/4 - Used at the beginning of a flush to force-sync the volume header
 *	 to give the flush plenty of runway to work in.
 */
int
hammer_flusher_undo_exhausted(hammer_transaction_t trans, int quarter)
{
	if (hammer_undo_space(trans) <
	    hammer_undo_max(trans->hmp) * quarter / 4) {
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
 *
 * If this is not final it is being called because too many dirty meta-data
 * buffers have built up and must be flushed with UNDO synchronization to
 * avoid a buffer cache deadlock.
 */
void
hammer_flusher_finalize(hammer_transaction_t trans, int final)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t cundomap, dundomap;
	hammer_mount_t hmp;
	hammer_io_t io;
	hammer_off_t save_undo_next_offset;
	int count;
	int i;

	hmp = trans->hmp;
	root_volume = trans->rootvol;

	/*
	 * Exclusively lock the flusher.  This guarantees that all dirty
	 * buffers will be idled (have a mod-count of 0).
	 */
	++hmp->flusher.finalize_want;
	hammer_lock_ex(&hmp->flusher.finalize_lock);

	/*
	 * If this isn't the final sync several threads may have hit the
	 * meta-limit at the same time and raced.  Only sync if we really
	 * have to, after acquiring the lock.
	 */
	if (final == 0 && !hammer_flusher_meta_limit(hmp))
		goto done;

	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
		goto done;

	/*
	 * Flush data buffers.  This can occur asynchronously and at any
	 * time.  We must interlock against the frontend direct-data write
	 * but do not have to acquire the sync-lock yet.
	 *
	 * These data buffers have already been collected prior to the
	 * related inode(s) getting queued to the flush group.
	 */
	count = 0;
	while ((io = RB_FIRST(hammer_mod_rb_tree, &hmp->data_root)) != NULL) {
		if (io->ioerror)
			break;
		hammer_ref(&io->lock);
		hammer_io_write_interlock(io);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_flush(io, 0);
		hammer_io_done_interlock(io);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		hammer_io_limit_backlog(hmp);
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
	 * Flush UNDOs.  This can occur concurrently with the data flush
	 * because data writes never overwrite.
	 *
	 * This also waits for I/Os to complete and flushes the cache on
	 * the target disk.
	 *
	 * Record the UNDO append point as this can continue to change
	 * after we have flushed the UNDOs.
	 */
	cundomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	hammer_lock_ex(&hmp->undo_lock);
	save_undo_next_offset = cundomap->next_offset;
	hammer_unlock(&hmp->undo_lock);
	hammer_flusher_flush_undos(hmp, HAMMER_FLUSH_UNDOS_FORCED);

	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
		goto failed;

	/*
	 * HAMMER VERSION < 4:
	 *	Update the on-disk volume header with new UNDO FIFO end
	 *	position (do not generate new UNDO records for this change).
	 *	We have to do this for the UNDO FIFO whether (final) is
	 *	set or not in order for the UNDOs to be recognized on
	 *	recovery.
	 *
	 * HAMMER VERSION >= 4:
	 *	The UNDO FIFO data written above will be recognized on
	 *	recovery without us having to sync the volume header.
	 *
	 * Also update the on-disk next_tid field.  This does not require
	 * an UNDO.  However, because our TID is generated before we get
	 * the sync lock another sync may have beat us to the punch.
	 *
	 * This also has the side effect of updating first_offset based on
	 * a prior finalization when the first finalization of the next flush
	 * cycle occurs, removing any undo info from the prior finalization
	 * from consideration.
	 *
	 * The volume header will be flushed out synchronously.
	 */
	dundomap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	cundomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];

	if (dundomap->first_offset != cundomap->first_offset ||
		   dundomap->next_offset != save_undo_next_offset) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		dundomap->first_offset = cundomap->first_offset;
		dundomap->next_offset = save_undo_next_offset;
		hammer_crc_set_blockmap(dundomap);
		hammer_modify_volume_done(root_volume);
	}

	/*
	 * vol0_next_tid is used for TID selection and is updated without
	 * an UNDO so we do not reuse a TID that may have been rolled-back.
	 *
	 * vol0_last_tid is the highest fully-synchronized TID.  It is
	 * set-up when the UNDO fifo is fully synced, later on (not here).
	 *
	 * The root volume can be open for modification by other threads
	 * generating UNDO or REDO records.  For example, reblocking,
	 * pruning, REDO mode fast-fsyncs, so the write interlock is
	 * mandatory.
	 */
	if (root_volume->io.modified) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		if (root_volume->ondisk->vol0_next_tid < trans->tid)
			root_volume->ondisk->vol0_next_tid = trans->tid;
		hammer_crc_set_volume(root_volume->ondisk);
		hammer_modify_volume_done(root_volume);
		hammer_io_write_interlock(&root_volume->io);
		hammer_io_flush(&root_volume->io, 0);
		hammer_io_done_interlock(&root_volume->io);
	}

	/*
	 * Wait for I/Os to complete.
	 *
	 * For HAMMER VERSION 4+ filesystems we do not have to wait for
	 * the I/O to complete as the new UNDO FIFO entries are recognized
	 * even without the volume header update.  This allows the volume
	 * header to flushed along with meta-data, significantly reducing
	 * flush overheads.
	 */
	hammer_flusher_clean_loose_ios(hmp);
	if (hmp->version < HAMMER_VOL_VERSION_FOUR)
		hammer_io_wait_all(hmp, "hmrfl3", 1);

	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
		goto failed;

	/*
	 * Flush meta-data.  The meta-data will be undone if we crash
	 * so we can safely flush it asynchronously.  There is no need
	 * to wait for I/O to complete (or issue a synchronous disk flush).
	 *
	 * In fact, even if we did wait the meta-data will still be undone
	 * by a crash up until the next flush cycle due to the first_offset
	 * in the volume header for the UNDO FIFO not being adjusted until
	 * the following flush cycle.
	 *
	 * No io interlock is needed, bioops callbacks will not mess with
	 * meta data buffers.
	 */
	count = 0;
	while ((io = RB_FIRST(hammer_mod_rb_tree, &hmp->meta_root)) != NULL) {
		if (io->ioerror)
			break;
		KKASSERT(io->modify_refs == 0);
		hammer_ref(&io->lock);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_flush(io, 0);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		hammer_io_limit_backlog(hmp);
		++count;
	}

	/*
	 * If this is the final finalization for the flush group set
	 * up for the next sequence by setting a new first_offset in
	 * our cached blockmap and clearing the undo history.
	 *
	 * Even though we have updated our cached first_offset, the on-disk
	 * first_offset still governs available-undo-space calculations.
	 *
	 * We synchronize to save_undo_next_offset rather than
	 * cundomap->next_offset because that is what we flushed out
	 * above.
	 *
	 * NOTE! UNDOs can only be added with the sync_lock held
	 *	 so we can clear the undo history without racing.
	 *	 REDOs can be added at any time which is why we
	 *	 have to be careful and use save_undo_next_offset
	 *	 when setting the new first_offset.
	 */
	if (final) {
		cundomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
		if (cundomap->first_offset != save_undo_next_offset) {
			cundomap->first_offset = save_undo_next_offset;
			hmp->hflags |= HMNT_UNDO_DIRTY;
		} else if (cundomap->first_offset != cundomap->next_offset) {
			hmp->hflags |= HMNT_UNDO_DIRTY;
		} else {
			hmp->hflags &= ~HMNT_UNDO_DIRTY;
		}
		hammer_clear_undo_history(hmp);

		/*
		 * Flush tid sequencing.  flush_tid1 is fully synchronized,
		 * meaning a crash will not roll it back.  flush_tid2 has
		 * been written out asynchronously and a crash will roll
		 * it back.  flush_tid1 is used for all mirroring masters.
		 */
		if (hmp->flush_tid1 != hmp->flush_tid2) {
			hmp->flush_tid1 = hmp->flush_tid2;
			wakeup(&hmp->flush_tid1);
		}
		hmp->flush_tid2 = trans->tid;

		/*
		 * Clear the REDO SYNC flag.  This flag is used to ensure
		 * that the recovery span in the UNDO/REDO FIFO contains
		 * at least one REDO SYNC record.
		 */
		hmp->flags &= ~HAMMER_MOUNT_REDO_SYNC;
	}

	/*
	 * Cleanup.  Report any critical errors.
	 */
failed:
	hammer_sync_unlock(trans);

	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR) {
		kprintf("HAMMER(%s): Critical write error during flush, "
			"refusing to sync UNDO FIFO\n",
			root_volume->ondisk->vol_name);
	}

done:
	hammer_unlock(&hmp->flusher.finalize_lock);

	if (--hmp->flusher.finalize_want == 0)
		wakeup(&hmp->flusher.finalize_want);
	hammer_stats_commits += final;
}

/*
 * Flush UNDOs.
 */
void
hammer_flusher_flush_undos(hammer_mount_t hmp, int mode)
{
	hammer_io_t io;
	int count;

	count = 0;
	while ((io = RB_FIRST(hammer_mod_rb_tree, &hmp->undo_root)) != NULL) {
		if (io->ioerror)
			break;
		hammer_ref(&io->lock);
		KKASSERT(io->type != HAMMER_STRUCTURE_VOLUME);
		hammer_io_write_interlock(io);
		hammer_io_flush(io, hammer_undo_reclaim(io));
		hammer_io_done_interlock(io);
		hammer_rel_buffer((hammer_buffer_t)io, 0);
		hammer_io_limit_backlog(hmp);
		++count;
	}
	hammer_flusher_clean_loose_ios(hmp);
	if (mode == HAMMER_FLUSH_UNDOS_FORCED ||
	    (mode == HAMMER_FLUSH_UNDOS_AUTO && count)) {
		hammer_io_wait_all(hmp, "hmrfl1", 1);
	} else {
		hammer_io_wait_all(hmp, "hmrfl2", 0);
	}
}

/*
 * Return non-zero if too many dirty meta-data buffers have built up.
 *
 * Since we cannot allow such buffers to flush until we have dealt with
 * the UNDOs, we risk deadlocking the kernel's buffer cache.
 */
int
hammer_flusher_meta_limit(hammer_mount_t hmp)
{
	if (hmp->locked_dirty_space + hmp->io_running_space >
	    hammer_limit_dirtybufspace) {
		return(1);
	}
	return(0);
}

/*
 * Return non-zero if too many dirty meta-data buffers have built up.
 *
 * This version is used by background operations (mirror, prune, reblock)
 * to leave room for foreground operations.
 */
int
hammer_flusher_meta_halflimit(hammer_mount_t hmp)
{
	if (hmp->locked_dirty_space + hmp->io_running_space >
	    hammer_limit_dirtybufspace / 2) {
		return(1);
	}
	return(0);
}

/*
 * Return non-zero if the flusher still has something to flush.
 */
int
hammer_flusher_haswork(hammer_mount_t hmp)
{
	if (hmp->ronly)
		return(0);
	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR)
		return(0);
	if (TAILQ_FIRST(&hmp->flush_group_list) ||	/* dirty inodes */
	    RB_ROOT(&hmp->volu_root) ||			/* dirty buffers */
	    RB_ROOT(&hmp->undo_root) ||
	    RB_ROOT(&hmp->data_root) ||
	    RB_ROOT(&hmp->meta_root) ||
	    (hmp->hflags & HMNT_UNDO_DIRTY)		/* UNDO FIFO sync */
	) {
		return(1);
	}
	return(0);
}
