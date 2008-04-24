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
 * $DragonFly: src/sys/vfs/hammer/hammer_flusher.c,v 1.2 2008/04/24 21:20:33 dillon Exp $
 */
/*
 * HAMMER dependancy flusher thread
 *
 * Meta data updates create buffer dependancies which are arranged as a
 * hierarchy of lists.
 */

#include "hammer.h"

static void hammer_flusher_thread(void *arg);
static void hammer_flusher_flush(hammer_mount_t hmp);

void
hammer_flusher_sync(hammer_mount_t hmp)
{
	int seq;

	seq = ++hmp->flusher_seq;
	wakeup(&hmp->flusher_seq);
	while ((int)(seq - hmp->flusher_act) > 0)
		tsleep(&hmp->flusher_act, 0, "hmrfls", 0);
}

void
hammer_flusher_async(hammer_mount_t hmp)
{
	++hmp->flusher_seq;
	wakeup(&hmp->flusher_seq);
}

void
hammer_flusher_create(hammer_mount_t hmp)
{
	lwkt_create(hammer_flusher_thread, hmp, &hmp->flusher_td, NULL,
		    0, -1, "hammer");
}

void
hammer_flusher_destroy(hammer_mount_t hmp)
{
	hmp->flusher_exiting = 1;
	++hmp->flusher_seq;
	wakeup(&hmp->flusher_seq);
	while (hmp->flusher_td)
		tsleep(&hmp->flusher_exiting, 0, "hmrwex", 0);
}

static void
hammer_flusher_thread(void *arg)
{
	hammer_mount_t hmp = arg;
	int seq;

	for (;;) {
		seq = hmp->flusher_seq;
		while (TAILQ_FIRST(&hmp->flush_list) != NULL)
			hammer_flusher_flush(hmp);
		hmp->flusher_act = seq;
		wakeup(&hmp->flusher_act);
		if (hmp->flusher_exiting)
			break;
		while (hmp->flusher_seq == hmp->flusher_act)
			tsleep(&hmp->flusher_seq, 0, "hmrflt", 0);
	}
	hmp->flusher_td = NULL;
	wakeup(&hmp->flusher_exiting);
	lwkt_exit();
}

/*
 * Flush stuff
 */
static void
hammer_flusher_flush(hammer_mount_t hmp)
{
	hammer_inode_t ip;

	while ((ip = TAILQ_FIRST(&hmp->flush_list)) != NULL) {
		TAILQ_REMOVE(&hmp->flush_list, ip, flush_entry);

		/*
		 * We inherit the inode ref from the flush list
		 */
		ip->error = hammer_sync_inode(ip, (ip->vp ? 0 : 1));
		hammer_flush_inode_done(ip);
	}
}

#if 0

static __inline
int
undo_seq_cmp(hammer_mount_t hmp, hammer_off_t seq1, hammer_off_t seq2)
{
	int delta;

	delta = (int)(seq1 - seq2) & hmp->undo_mask;
	if (delta == 0)
		return(0);
	if (delta > (hmp->undo_mask >> 1))
		return(-1);
	return(1);
}

static void
hammer_flusher_flush(hammer_mount_t hmp)
{
	hammer_off_t undo_seq;
	hammer_buffer_t buffer;
	hammer_volume_t root_volume;
	hammer_blockmap_t rootmap;
	int count;
	int error;

	/*
	 * The undo space is sequenced via the undo zone.
	 */
	root_volume = hammer_get_root_volume(hmp, &error);
	if (root_volume == NULL) {
		panic("hammer: can't get root volume");
		return;
	}

	/*
	 * Flush pending undo buffers.  The kernel may also flush these
	 * asynchronously.  This list may also contain pure data buffers
	 * (which do not overwrite pre-existing data).
	 *
	 * The flush can occur simultaniously with new appends, only flush
	 * through undo_seq.  If an I/O is already in progress the call to
	 * hammer_ref_buffer() will wait for it to complete.
	 *
	 * Note that buffers undergoing I/O not initiated by us are not
	 * removed from the list until the I/O is complete, so they are
	 * still visible to us to block on.
	 */

	/*
	 * Lock the meta-data buffers
	 */
	undo_seq = hmp->undo_zone.next_offset;
	TAILQ_FOREACH(buffer, &hmp->undo_dep_list, undo_entry) {
		KKASSERT(buffer->io.undo_type == HAMMER_UNDO_TYPE_DEP);
		buffer->io.undo_type = HAMMER_UNDO_TYPE_DEP_LOCKED;
		if (undo_seq_cmp(hmp, buffer->io.undo_off, undo_seq) >= 0)
			break;
	}

	/*
	 * Initiate I/O for the undo fifo buffers
	 */
	count = 0;
	while ((buffer = TAILQ_FIRST(&hmp->undo_buf_list)) != NULL) {
		if (undo_seq_cmp(hmp, buffer->io.undo_off, undo_seq) >= 0) {
			break;
		}
		hammer_ref_buffer(buffer);

		if (buffer != (void *)TAILQ_FIRST(&hmp->undo_buf_list)) {
			hammer_rel_buffer(buffer, 0);
		} else {
			TAILQ_REMOVE(&hmp->undo_buf_list, buffer, undo_entry);
			buffer->io.undo_type = HAMMER_UNDO_TYPE_NONE;
			if (buffer->io.modified) {
				buffer->io.decount = &count;
				++count;
			}
			hammer_rel_buffer(buffer, 1);
		}
	}

	/*
	 * Wait for completion
	 */
	crit_enter();
	while (count)
		tsleep(&count, 0, "hmrfwt", 0);
	crit_exit();

	/*
	 * The root volume must be updated.  The previous push is now fully
	 * synchronized.  { first_offset, next_offset } tell the mount code
	 * what needs to be undone.
	 */
	hammer_modify_volume(NULL, root_volume, NULL, 0);
	rootmap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	rootmap->first_offset = rootmap->next_offset;
	rootmap->next_offset = undo_seq;
	hammer_modify_volume_done(NULL, root_volume);

	/*
	 * cache the first_offset update.  the cached next_offset is the
	 * current next_offset, not the undo_seq that we synchronized to disk.
	 * XXX
	 */
	hmp->undo_zone.first_offset = rootmap->first_offset;

	++count;
	root_volume->io.decount = &count;
	hammer_rel_volume(root_volume, 2);

	/*
	 * Wait for completion
	 */
	crit_enter();
	while (count)
		tsleep(&count, 0, "hmrfwt", 0);
	crit_exit();

	/*
	 * Now we can safely push out buffers containing meta-data
	 * modifications.  If we crash while doing this, the changes will
	 * be undone on mount.
	 */
	while ((buffer = TAILQ_FIRST(&hmp->undo_dep_list)) != NULL) {
		if (buffer->io.undo_type != HAMMER_UNDO_TYPE_DEP_LOCKED)
			break;
		hammer_ref_buffer(buffer);

		if (buffer != TAILQ_FIRST(&hmp->undo_dep_list)) {
			hammer_rel_buffer(buffer, 0);
		} else {
			TAILQ_REMOVE(&hmp->undo_dep_list, buffer, undo_entry);
			if (buffer->io.modified) {
				buffer->io.decount = &count;
				++count;
				hammer_rel_buffer(buffer, 2);
			} else {
				hammer_rel_buffer(buffer, 0);
			}
		}
	}

	/*
	 * Wait for completion
	 */
	crit_enter();
	while (count)
		tsleep(&count, 0, "hmrfwt", 0);
	crit_exit();

	/*
	 * The undo bit is only cleared if no new undo's were entered into
	 * the cache, and first_offset == next_offset. 
	 */
	if (hmp->undo_zone.next_offset == undo_seq &&
	    rootmap->first_offset == rootmap->next_offset) {
		hmp->hflags &= ~HMNT_UDIRTY;
	}
}

#endif
