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
 * $DragonFly: src/sys/vfs/hammer/hammer_io.c,v 1.36 2008/06/09 04:19:10 dillon Exp $
 */
/*
 * IO Primitives and buffer cache management
 *
 * All major data-tracking structures in HAMMER contain a struct hammer_io
 * which is used to manage their backing store.  We use filesystem buffers
 * for backing store and we leave them passively associated with their
 * HAMMER structures.
 *
 * If the kernel tries to destroy a passively associated buf which we cannot
 * yet let go we set B_LOCKED in the buffer and then actively released it
 * later when we can.
 */

#include "hammer.h"
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/buf.h>
#include <sys/buf2.h>

static void hammer_io_modify(hammer_io_t io, int count);
static void hammer_io_deallocate(struct buf *bp);

/*
 * Initialize a new, already-zero'd hammer_io structure, or reinitialize
 * an existing hammer_io structure which may have switched to another type.
 */
void
hammer_io_init(hammer_io_t io, hammer_mount_t hmp, enum hammer_io_type type)
{
	io->hmp = hmp;
	io->type = type;
}

/*
 * Helper routine to disassociate a buffer cache buffer from an I/O
 * structure.  Called with the io structure exclusively locked.
 *
 * The io may have 0 or 1 references depending on who called us.  The
 * caller is responsible for dealing with the refs.
 *
 * This call can only be made when no action is required on the buffer.
 * HAMMER must own the buffer (released == 0) since we mess around with it.
 */
static void
hammer_io_disassociate(hammer_io_structure_t iou, int elseit)
{
	struct buf *bp = iou->io.bp;

	KKASSERT(iou->io.modified == 0);
	buf_dep_init(bp);
	iou->io.bp = NULL;

	/*
	 * If the buffer was locked someone wanted to get rid of it.
	 */
	if (bp->b_flags & B_LOCKED) {
		bp->b_flags &= ~B_LOCKED;
		bp->b_flags |= B_RELBUF;
	}

	/*
	 * elseit is 0 when called from the kernel path, the caller is
	 * holding the buffer locked and will deal with its final disposition.
	 */
	if (elseit) {
		KKASSERT(iou->io.released == 0);
		iou->io.released = 1;
		bqrelse(bp);
	} else {
		KKASSERT(iou->io.released);
	}

	switch(iou->io.type) {
	case HAMMER_STRUCTURE_VOLUME:
		iou->volume.ondisk = NULL;
		break;
	case HAMMER_STRUCTURE_DATA_BUFFER:
	case HAMMER_STRUCTURE_META_BUFFER:
	case HAMMER_STRUCTURE_UNDO_BUFFER:
		iou->buffer.ondisk = NULL;
		break;
	}
}

/*
 * Wait for any physical IO to complete
 */
static void
hammer_io_wait(hammer_io_t io)
{
	if (io->running) {
		crit_enter();
		tsleep_interlock(io);
		io->waiting = 1;
		for (;;) {
			tsleep(io, 0, "hmrflw", 0);
			if (io->running == 0)
				break;
			tsleep_interlock(io);
			io->waiting = 1;
			if (io->running == 0)
				break;
		}
		crit_exit();
	}
}

#define HAMMER_MAXRA	4

/*
 * Load bp for a HAMMER structure.  The io must be exclusively locked by
 * the caller.
 *
 * Generally speaking HAMMER assumes either an optimized layout or that
 * typical access patterns will be close to the original layout when the
 * information was written.  For this reason we try to cluster all reads.
 */
int
hammer_io_read(struct vnode *devvp, struct hammer_io *io, hammer_off_t limit)
{
	struct buf *bp;
	int   error;

	if ((bp = io->bp) == NULL) {
#if 1
		error = cluster_read(devvp, limit, io->offset,
				     HAMMER_BUFSIZE, MAXBSIZE, 16, &io->bp);
#else
		error = bread(devvp, io->offset, HAMMER_BUFSIZE, &io->bp);
#endif

		if (error == 0) {
			bp = io->bp;
			bp->b_ops = &hammer_bioops;
			LIST_INSERT_HEAD(&bp->b_dep, &io->worklist, node);
			BUF_KERNPROC(bp);
		}
		KKASSERT(io->modified == 0);
		KKASSERT(io->running == 0);
		KKASSERT(io->waiting == 0);
		io->released = 0;	/* we hold an active lock on bp */
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Similar to hammer_io_read() but returns a zero'd out buffer instead.
 * Must be called with the IO exclusively locked.
 *
 * vfs_bio_clrbuf() is kinda nasty, enforce serialization against background
 * I/O by forcing the buffer to not be in a released state before calling
 * it.
 *
 * This function will also mark the IO as modified but it will not
 * increment the modify_refs count.
 */
int
hammer_io_new(struct vnode *devvp, struct hammer_io *io)
{
	struct buf *bp;

	if ((bp = io->bp) == NULL) {
		io->bp = getblk(devvp, io->offset, HAMMER_BUFSIZE, 0, 0);
		bp = io->bp;
		bp->b_ops = &hammer_bioops;
		LIST_INSERT_HEAD(&bp->b_dep, &io->worklist, node);
		io->released = 0;
		KKASSERT(io->running == 0);
		io->waiting = 0;
		BUF_KERNPROC(bp);
	} else {
		if (io->released) {
			regetblk(bp);
			BUF_KERNPROC(bp);
			io->released = 0;
		}
	}
	hammer_io_modify(io, 0);
	vfs_bio_clrbuf(bp);
	return(0);
}

/*
 * Remove potential device level aliases against buffers managed by high level
 * vnodes.
 */
void
hammer_io_inval(hammer_volume_t volume, hammer_off_t zone2_offset)
{
	hammer_off_t phys_offset;
	struct buf *bp;

	phys_offset = volume->ondisk->vol_buf_beg +
		      (zone2_offset & HAMMER_OFF_SHORT_MASK);
	if (findblk(volume->devvp, phys_offset)) {
		bp = getblk(volume->devvp, phys_offset, HAMMER_BUFSIZE, 0, 0);
		if (LIST_FIRST(&bp->b_dep) != NULL) {
			hammer_io_deallocate(bp);
		} else {
			bp->b_flags |= B_RELBUF;
			brelse(bp);
		}
	}
}

/*
 * This routine is called on the last reference to a hammer structure.
 * The io is usually locked exclusively (but may not be during unmount).
 *
 * This routine is responsible for the disposition of the buffer cache
 * buffer backing the IO.  Only pure-data and undo buffers can be handed
 * back to the kernel.  Volume and meta-data buffers must be retained
 * by HAMMER until explicitly flushed by the backend.
 */
void
hammer_io_release(struct hammer_io *io, int flush)
{
	union hammer_io_structure *iou = (void *)io;
	struct buf *bp;

	if ((bp = io->bp) == NULL)
		return;

	/*
	 * Try to flush a dirty IO to disk if asked to by the
	 * caller or if the kernel tried to flush the buffer in the past.
	 *
	 * Kernel-initiated flushes are only allowed for pure-data buffers.
	 * meta-data and volume buffers can only be flushed explicitly
	 * by HAMMER.
	 */
	if (io->modified) {
		if (flush) {
			hammer_io_flush(io);
		} else if (bp->b_flags & B_LOCKED) {
			switch(io->type) {
			case HAMMER_STRUCTURE_DATA_BUFFER:
			case HAMMER_STRUCTURE_UNDO_BUFFER:
				hammer_io_flush(io);
				break;
			default:
				break;
			}
		} /* else no explicit request to flush the buffer */
	}

	/*
	 * Wait for the IO to complete if asked to.
	 */
	if (io->waitdep && io->running) {
		hammer_io_wait(io);
	}

	/*
	 * Return control of the buffer to the kernel (with the provisio
	 * that our bioops can override kernel decisions with regards to
	 * the buffer).
	 */
	if (flush && io->modified == 0 && io->running == 0) {
		/*
		 * Always disassociate the bp if an explicit flush
		 * was requested and the IO completed with no error
		 * (so unmount can really clean up the structure).
		 */
		if (io->released) {
			regetblk(bp);
			BUF_KERNPROC(bp);
			io->released = 0;
		}
		hammer_io_disassociate((hammer_io_structure_t)io, 1);
	} else if (io->modified) {
		/*
		 * Only certain IO types can be released to the kernel.
		 * volume and meta-data IO types must be explicitly flushed
		 * by HAMMER.
		 */
		switch(io->type) {
		case HAMMER_STRUCTURE_DATA_BUFFER:
		case HAMMER_STRUCTURE_UNDO_BUFFER:
			if (io->released == 0) {
				io->released = 1;
				bdwrite(bp);
			}
			break;
		default:
			break;
		}
	} else if (io->released == 0) {
		/*
		 * Clean buffers can be generally released to the kernel.
		 * We leave the bp passively associated with the HAMMER
		 * structure and use bioops to disconnect it later on
		 * if the kernel wants to discard the buffer.
		 */
		if (bp->b_flags & B_LOCKED) {
			hammer_io_disassociate(iou, 1);
		} else {
			io->released = 1;
			bqrelse(bp);
		}
	} else {
		/*
		 * A released buffer may have been locked when the kernel
		 * tried to deallocate it while HAMMER still had references
		 * on the hammer_buffer.  We must unlock the buffer or
		 * it will just rot.
		 */
		crit_enter();
		if (io->running == 0 && (bp->b_flags & B_LOCKED)) {
			regetblk(bp);
			if (bp->b_flags & B_LOCKED) {
				io->released = 0;
				hammer_io_disassociate(iou, 1);
			} else {
				bqrelse(bp);
			}
		}
		crit_exit();
	}
}

/*
 * This routine is called with a locked IO when a flush is desired and
 * no other references to the structure exists other then ours.  This
 * routine is ONLY called when HAMMER believes it is safe to flush a
 * potentially modified buffer out.
 */
void
hammer_io_flush(struct hammer_io *io)
{
	struct buf *bp;

	/*
	 * Degenerate case - nothing to flush if nothing is dirty.
	 */
	if (io->modified == 0) {
		return;
	}

	KKASSERT(io->bp);
	KKASSERT(io->modify_refs <= 0);

	/*
	 * Acquire ownership of the bp, particularly before we clear our
	 * modified flag.
	 *
	 * We are going to bawrite() this bp.  Don't leave a window where
	 * io->released is set, we actually own the bp rather then our
	 * buffer.
	 */
	bp = io->bp;
	if (io->released) {
		regetblk(bp);
		/* BUF_KERNPROC(io->bp); */
		/* io->released = 0; */
		KKASSERT(io->released);
		KKASSERT(io->bp == bp);
	}
	io->released = 1;

	/*
	 * Acquire exclusive access to the bp and then clear the modified
	 * state of the buffer prior to issuing I/O to interlock any
	 * modifications made while the I/O is in progress.  This shouldn't
	 * happen anyway but losing data would be worse.  The modified bit
	 * will be rechecked after the IO completes.
	 *
	 * This is only legal when lock.refs == 1 (otherwise we might clear
	 * the modified bit while there are still users of the cluster
	 * modifying the data).
	 *
	 * Do this before potentially blocking so any attempt to modify the
	 * ondisk while we are blocked blocks waiting for us.
	 */
	KKASSERT(io->mod_list != NULL);
	if (io->mod_list == &io->hmp->volu_list ||
	    io->mod_list == &io->hmp->meta_list) {
		--io->hmp->locked_dirty_count;
		--hammer_count_dirtybufs;
	}
	TAILQ_REMOVE(io->mod_list, io, mod_entry);
	io->mod_list = NULL;
	io->modified = 0;

	/*
	 * Transfer ownership to the kernel and initiate I/O.
	 */
	io->running = 1;
	++io->hmp->io_running_count;
	bawrite(bp);
}

/************************************************************************
 *				BUFFER DIRTYING				*
 ************************************************************************
 *
 * These routines deal with dependancies created when IO buffers get
 * modified.  The caller must call hammer_modify_*() on a referenced
 * HAMMER structure prior to modifying its on-disk data.
 *
 * Any intent to modify an IO buffer acquires the related bp and imposes
 * various write ordering dependancies.
 */

/*
 * Mark a HAMMER structure as undergoing modification.  Meta-data buffers
 * are locked until the flusher can deal with them, pure data buffers
 * can be written out.
 */
static
void
hammer_io_modify(hammer_io_t io, int count)
{
	struct hammer_mount *hmp = io->hmp;

	/*
	 * io->modify_refs must be >= 0
	 */
	while (io->modify_refs < 0) {
		io->waitmod = 1;
		tsleep(io, 0, "hmrmod", 0);
	}

	/*
	 * Shortcut if nothing to do.
	 */
	KKASSERT(io->lock.refs != 0 && io->bp != NULL);
	io->modify_refs += count;
	if (io->modified && io->released == 0)
		return;

	hammer_lock_ex(&io->lock);
	if (io->modified == 0) {
		KKASSERT(io->mod_list == NULL);
		switch(io->type) {
		case HAMMER_STRUCTURE_VOLUME:
			io->mod_list = &hmp->volu_list;
			++hmp->locked_dirty_count;
			++hammer_count_dirtybufs;
			break;
		case HAMMER_STRUCTURE_META_BUFFER:
			io->mod_list = &hmp->meta_list;
			++hmp->locked_dirty_count;
			++hammer_count_dirtybufs;
			break;
		case HAMMER_STRUCTURE_UNDO_BUFFER:
			io->mod_list = &hmp->undo_list;
			break;
		case HAMMER_STRUCTURE_DATA_BUFFER:
			io->mod_list = &hmp->data_list;
			break;
		}
		TAILQ_INSERT_TAIL(io->mod_list, io, mod_entry);
		io->modified = 1;
	}
	if (io->released) {
		regetblk(io->bp);
		BUF_KERNPROC(io->bp);
		io->released = 0;
		KKASSERT(io->modified != 0);
	}
	hammer_unlock(&io->lock);
}

static __inline
void
hammer_io_modify_done(hammer_io_t io)
{
	KKASSERT(io->modify_refs > 0);
	--io->modify_refs;
	if (io->modify_refs == 0 && io->waitmod) {
		io->waitmod = 0;
		wakeup(io);
	}
}

void
hammer_io_write_interlock(hammer_io_t io)
{
	while (io->modify_refs != 0) {
		io->waitmod = 1;
		tsleep(io, 0, "hmrmod", 0);
	}
	io->modify_refs = -1;
}

void
hammer_io_done_interlock(hammer_io_t io)
{
	KKASSERT(io->modify_refs == -1);
	io->modify_refs = 0;
	if (io->waitmod) {
		io->waitmod = 0;
		wakeup(io);
	}
}

/*
 * Caller intends to modify a volume's ondisk structure.
 *
 * This is only allowed if we are the flusher or we have a ref on the
 * sync_lock.
 */
void
hammer_modify_volume(hammer_transaction_t trans, hammer_volume_t volume,
		     void *base, int len)
{
	KKASSERT (trans == NULL || trans->sync_lock_refs > 0);

	hammer_io_modify(&volume->io, 1);
	if (len) {
		intptr_t rel_offset = (intptr_t)base - (intptr_t)volume->ondisk;
		KKASSERT((rel_offset & ~(intptr_t)HAMMER_BUFMASK) == 0);
		hammer_generate_undo(trans, &volume->io,
			 HAMMER_ENCODE_RAW_VOLUME(volume->vol_no, rel_offset),
			 base, len);
	}
}

/*
 * Caller intends to modify a buffer's ondisk structure.
 *
 * This is only allowed if we are the flusher or we have a ref on the
 * sync_lock.
 */
void
hammer_modify_buffer(hammer_transaction_t trans, hammer_buffer_t buffer,
		     void *base, int len)
{
	KKASSERT (trans == NULL || trans->sync_lock_refs > 0);

	hammer_io_modify(&buffer->io, 1);
	if (len) {
		intptr_t rel_offset = (intptr_t)base - (intptr_t)buffer->ondisk;
		KKASSERT((rel_offset & ~(intptr_t)HAMMER_BUFMASK) == 0);
		hammer_generate_undo(trans, &buffer->io,
				     buffer->zone2_offset + rel_offset,
				     base, len);
	}
}

void
hammer_modify_volume_done(hammer_volume_t volume)
{
	hammer_io_modify_done(&volume->io);
}

void
hammer_modify_buffer_done(hammer_buffer_t buffer)
{
	hammer_io_modify_done(&buffer->io);
}

/*
 * Mark an entity as not being dirty any more -- this usually occurs when
 * the governing a-list has freed the entire entity.
 *
 * XXX
 */
void
hammer_io_clear_modify(struct hammer_io *io)
{
#if 0
	struct buf *bp;

	io->modified = 0;
	XXX mod_list/entry
	if ((bp = io->bp) != NULL) {
		if (io->released) {
			regetblk(bp);
			/* BUF_KERNPROC(io->bp); */
		} else {
			io->released = 1;
		}
		if (io->modified == 0) {
			bundirty(bp);
			bqrelse(bp);
		} else {
			bdwrite(bp);
		}
	}
#endif
}

/************************************************************************
 *				HAMMER_BIOOPS				*
 ************************************************************************
 *
 */

/*
 * Pre-IO initiation kernel callback - cluster build only
 */
static void
hammer_io_start(struct buf *bp)
{
}

/*
 * Post-IO completion kernel callback
 *
 * NOTE: HAMMER may modify a buffer after initiating I/O.  The modified bit
 * may also be set if we were marking a cluster header open.  Only remove
 * our dependancy if the modified bit is clear.
 */
static void
hammer_io_complete(struct buf *bp)
{
	union hammer_io_structure *iou = (void *)LIST_FIRST(&bp->b_dep);

	KKASSERT(iou->io.released == 1);

	if (iou->io.running) {
		if (--iou->io.hmp->io_running_count == 0)
			wakeup(&iou->io.hmp->io_running_count);
		KKASSERT(iou->io.hmp->io_running_count >= 0);
		iou->io.running = 0;
	}

	/*
	 * If no lock references remain and we can acquire the IO lock and
	 * someone at some point wanted us to flush (B_LOCKED test), then
	 * try to dispose of the IO.
	 */
	if (iou->io.waiting) {
		iou->io.waiting = 0;
		wakeup(iou);
	}

	/*
	 * Someone wanted us to flush, try to clean out the buffer. 
	 */
	if ((bp->b_flags & B_LOCKED) && iou->io.lock.refs == 0) {
		KKASSERT(iou->io.modified == 0);
		bp->b_flags &= ~B_LOCKED;
		hammer_io_deallocate(bp);
		/* structure may be dead now */
	}
}

/*
 * Callback from kernel when it wishes to deallocate a passively
 * associated structure.  This mostly occurs with clean buffers
 * but it may be possible for a holding structure to be marked dirty
 * while its buffer is passively associated.
 *
 * If we cannot disassociate we set B_LOCKED to prevent the buffer
 * from getting reused.
 *
 * WARNING: Because this can be called directly by getnewbuf we cannot
 * recurse into the tree.  If a bp cannot be immediately disassociated
 * our only recourse is to set B_LOCKED.
 */
static void
hammer_io_deallocate(struct buf *bp)
{
	hammer_io_structure_t iou = (void *)LIST_FIRST(&bp->b_dep);

	KKASSERT((bp->b_flags & B_LOCKED) == 0 && iou->io.running == 0);
	if (iou->io.lock.refs > 0 || iou->io.modified) {
		/*
		 * It is not legal to disassociate a modified buffer.  This
		 * case really shouldn't ever occur.
		 */
		bp->b_flags |= B_LOCKED;
	} else {
		/*
		 * Disassociate the BP.  If the io has no refs left we
		 * have to add it to the loose list.
		 */
		hammer_io_disassociate(iou, 0);
		if (iou->io.bp == NULL && 
		    iou->io.type != HAMMER_STRUCTURE_VOLUME) {
			KKASSERT(iou->io.mod_list == NULL);
			iou->io.mod_list = &iou->io.hmp->lose_list;
			TAILQ_INSERT_TAIL(iou->io.mod_list, &iou->io, mod_entry);
		}
	}
}

static int
hammer_io_fsync(struct vnode *vp)
{
	return(0);
}

/*
 * NOTE: will not be called unless we tell the kernel about the
 * bioops.  Unused... we use the mount's VFS_SYNC instead.
 */
static int
hammer_io_sync(struct mount *mp)
{
	return(0);
}

static void
hammer_io_movedeps(struct buf *bp1, struct buf *bp2)
{
}

/*
 * I/O pre-check for reading and writing.  HAMMER only uses this for
 * B_CACHE buffers so checkread just shouldn't happen, but if it does
 * allow it.
 *
 * Writing is a different case.  We don't want the kernel to try to write
 * out a buffer that HAMMER may be modifying passively or which has a
 * dependancy.  In addition, kernel-demanded writes can only proceed for
 * certain types of buffers (i.e. UNDO and DATA types).  Other dirty
 * buffer types can only be explicitly written by the flusher.
 *
 * checkwrite will only be called for bdwrite()n buffers.  If we return
 * success the kernel is guaranteed to initiate the buffer write.
 */
static int
hammer_io_checkread(struct buf *bp)
{
	return(0);
}

static int
hammer_io_checkwrite(struct buf *bp)
{
	hammer_io_t io = (void *)LIST_FIRST(&bp->b_dep);

	/*
	 * This shouldn't happen under normal operation.
	 */
	if (io->type == HAMMER_STRUCTURE_VOLUME ||
	    io->type == HAMMER_STRUCTURE_META_BUFFER) {
		if (!panicstr)
			panic("hammer_io_checkwrite: illegal buffer");
		bp->b_flags |= B_LOCKED;
		return(1);
	}

	/*
	 * We can only clear the modified bit if the IO is not currently
	 * undergoing modification.  Otherwise we may miss changes.
	 */
	if (io->modify_refs == 0 && io->modified) {
		KKASSERT(io->mod_list != NULL);
		if (io->mod_list == &io->hmp->volu_list ||
		    io->mod_list == &io->hmp->meta_list) {
			--io->hmp->locked_dirty_count;
			--hammer_count_dirtybufs;
		}
		TAILQ_REMOVE(io->mod_list, io, mod_entry);
		io->mod_list = NULL;
		io->modified = 0;
	}

	/*
	 * The kernel is going to start the IO, set io->running.
	 */
	KKASSERT(io->running == 0);
	io->running = 1;
	++io->hmp->io_running_count;
	return(0);
}

/*
 * Return non-zero if we wish to delay the kernel's attempt to flush
 * this buffer to disk.
 */
static int
hammer_io_countdeps(struct buf *bp, int n)
{
	return(0);
}

struct bio_ops hammer_bioops = {
	.io_start	= hammer_io_start,
	.io_complete	= hammer_io_complete,
	.io_deallocate	= hammer_io_deallocate,
	.io_fsync	= hammer_io_fsync,
	.io_sync	= hammer_io_sync,
	.io_movedeps	= hammer_io_movedeps,
	.io_countdeps	= hammer_io_countdeps,
	.io_checkread	= hammer_io_checkread,
	.io_checkwrite	= hammer_io_checkwrite,
};

/************************************************************************
 *				DIRECT IO OPS 				*
 ************************************************************************
 *
 * These functions operate directly on the buffer cache buffer associated
 * with a front-end vnode rather then a back-end device vnode.
 */

/*
 * Read a buffer associated with a front-end vnode directly from the
 * disk media.  The bio may be issued asynchronously.
 */
int
hammer_io_direct_read(hammer_mount_t hmp, hammer_btree_leaf_elm_t leaf,
		      struct bio *bio)
{
	hammer_off_t zone2_offset;
	hammer_volume_t volume;
	struct buf *bp;
	struct bio *nbio;
	int vol_no;
	int error;

	KKASSERT(leaf->data_offset >= HAMMER_ZONE_BTREE);
	KKASSERT((leaf->data_offset & HAMMER_BUFMASK) == 0);
	zone2_offset = hammer_blockmap_lookup(hmp, leaf->data_offset, &error);
	if (error == 0) {
		vol_no = HAMMER_VOL_DECODE(zone2_offset);
		volume = hammer_get_volume(hmp, vol_no, &error);
		if (error == 0 && zone2_offset >= volume->maxbuf_off)
			error = EIO;
		if (error == 0) {
			zone2_offset &= HAMMER_OFF_SHORT_MASK;
			nbio = push_bio(bio);
			nbio->bio_offset = volume->ondisk->vol_buf_beg +
					   zone2_offset;
			vn_strategy(volume->devvp, nbio);
		}
		hammer_rel_volume(volume, 0);
	}
	if (error) {
		bp = bio->bio_buf;
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	}
	return(error);
}

/*
 * Write a buffer associated with a front-end vnode directly to the
 * disk media.  The bio may be issued asynchronously.
 */
int
hammer_io_direct_write(hammer_mount_t hmp, hammer_btree_leaf_elm_t leaf,
		       struct bio *bio)
{
	hammer_off_t buf_offset;
	hammer_off_t zone2_offset;
	hammer_volume_t volume;
	hammer_buffer_t buffer;
	struct buf *bp;
	struct bio *nbio;
	char *ptr;
	int vol_no;
	int error;

	buf_offset = leaf->data_offset;

	KKASSERT(buf_offset > HAMMER_ZONE_BTREE);
	KKASSERT(bio->bio_buf->b_cmd == BUF_CMD_WRITE);

	if ((buf_offset & HAMMER_BUFMASK) == 0 &&
	    leaf->data_len == HAMMER_BUFSIZE) {
		/*
		 * We are using the vnode's bio to write directly to the
		 * media, any hammer_buffer at the same zone-X offset will
		 * now have stale data.
		 */
		zone2_offset = hammer_blockmap_lookup(hmp, buf_offset, &error);
		vol_no = HAMMER_VOL_DECODE(zone2_offset);
		volume = hammer_get_volume(hmp, vol_no, &error);

		if (error == 0 && zone2_offset >= volume->maxbuf_off)
			error = EIO;
		if (error == 0) {
			hammer_del_buffers(hmp, buf_offset,
					   zone2_offset, HAMMER_BUFSIZE);
			bp = bio->bio_buf;
			KKASSERT(bp->b_bufsize == HAMMER_BUFSIZE);
			zone2_offset &= HAMMER_OFF_SHORT_MASK;

			nbio = push_bio(bio);
			nbio->bio_offset = volume->ondisk->vol_buf_beg +
					   zone2_offset;
			vn_strategy(volume->devvp, nbio);
		}
		hammer_rel_volume(volume, 0);
	} else {
		KKASSERT(((buf_offset ^ (buf_offset + leaf->data_len - 1)) & ~HAMMER_BUFMASK64) == 0);
		buffer = NULL;
		ptr = hammer_bread(hmp, buf_offset, &error, &buffer);
		if (error == 0) {
			bp = bio->bio_buf;
			hammer_io_modify(&buffer->io, 1);
			bcopy(bp->b_data, ptr, leaf->data_len);
			hammer_io_modify_done(&buffer->io);
			hammer_rel_buffer(buffer, 0);
			bp->b_resid = 0;
			biodone(bio);
		}
	}
	if (error) {
		bp = bio->bio_buf;
		bp->b_resid = 0;
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	}
	return(error);
}


