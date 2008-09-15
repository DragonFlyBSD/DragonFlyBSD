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
 * $DragonFly: src/sys/vfs/hammer/hammer_io.c,v 1.55 2008/09/15 17:02:49 dillon Exp $
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
#if 0
static void hammer_io_direct_read_complete(struct bio *nbio);
#endif
static void hammer_io_direct_write_complete(struct bio *nbio);
static int hammer_io_direct_uncache_callback(hammer_inode_t ip, void *data);
static void hammer_io_set_modlist(struct hammer_io *io);
static void hammer_io_flush_mark(hammer_volume_t volume);
static void hammer_io_flush_sync_done(struct bio *bio);


/*
 * Initialize a new, already-zero'd hammer_io structure, or reinitialize
 * an existing hammer_io structure which may have switched to another type.
 */
void
hammer_io_init(hammer_io_t io, hammer_volume_t volume, enum hammer_io_type type)
{
	io->volume = volume;
	io->hmp = volume->io.hmp;
	io->type = type;
}

/*
 * Helper routine to disassociate a buffer cache buffer from an I/O
 * structure.  The buffer is unlocked and marked appropriate for reclamation.
 *
 * The io may have 0 or 1 references depending on who called us.  The
 * caller is responsible for dealing with the refs.
 *
 * This call can only be made when no action is required on the buffer.
 *
 * The caller must own the buffer and the IO must indicate that the
 * structure no longer owns it (io.released != 0).
 */
static void
hammer_io_disassociate(hammer_io_structure_t iou)
{
	struct buf *bp = iou->io.bp;

	KKASSERT(iou->io.released);
	KKASSERT(iou->io.modified == 0);
	KKASSERT(LIST_FIRST(&bp->b_dep) == (void *)iou);
	buf_dep_init(bp);
	iou->io.bp = NULL;

	/*
	 * If the buffer was locked someone wanted to get rid of it.
	 */
	if (bp->b_flags & B_LOCKED) {
		--hammer_count_io_locked;
		bp->b_flags &= ~B_LOCKED;
	}
	if (iou->io.reclaim) {
		bp->b_flags |= B_NOCACHE|B_RELBUF;
		iou->io.reclaim = 0;
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
void
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

/*
 * Wait for all hammer_io-initated write I/O's to complete.  This is not
 * supposed to count direct I/O's but some can leak through (for
 * non-full-sized direct I/Os).
 */
void
hammer_io_wait_all(hammer_mount_t hmp, const char *ident)
{
	hammer_io_flush_sync(hmp);
	crit_enter();
	while (hmp->io_running_space)
		tsleep(&hmp->io_running_space, 0, ident, 0);
	crit_exit();
}

#define HAMMER_MAXRA	4

/*
 * Load bp for a HAMMER structure.  The io must be exclusively locked by
 * the caller.
 *
 * This routine is mostly used on meta-data and small-data blocks.  Generally
 * speaking HAMMER assumes some locality of reference and will cluster 
 * a 64K read.
 *
 * Note that clustering occurs at the device layer, not the logical layer.
 * If the buffers do not apply to the current operation they may apply to
 * some other.
 */
int
hammer_io_read(struct vnode *devvp, struct hammer_io *io, hammer_off_t limit)
{
	struct buf *bp;
	int   error;

	if ((bp = io->bp) == NULL) {
		hammer_count_io_running_read += io->bytes;
		if (hammer_cluster_enable) {
			error = cluster_read(devvp, limit,
					     io->offset, io->bytes,
					     HAMMER_CLUSTER_SIZE,
					     HAMMER_CLUSTER_BUFS, &io->bp);
		} else {
			error = bread(devvp, io->offset, io->bytes, &io->bp);
		}
		hammer_stats_disk_read += io->bytes;
		hammer_count_io_running_read -= io->bytes;

		/*
		 * The code generally assumes b_ops/b_dep has been set-up,
		 * even if we error out here.
		 */
		bp = io->bp;
		bp->b_ops = &hammer_bioops;
		KKASSERT(LIST_FIRST(&bp->b_dep) == NULL);
		LIST_INSERT_HEAD(&bp->b_dep, &io->worklist, node);
		BUF_KERNPROC(bp);
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
		io->bp = getblk(devvp, io->offset, io->bytes, 0, 0);
		bp = io->bp;
		bp->b_ops = &hammer_bioops;
		KKASSERT(LIST_FIRST(&bp->b_dep) == NULL);
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
 * vnodes.  Aliases can also be created due to mixed buffer sizes.
 *
 * This is nasty because the buffers are also VMIO-backed.  Even if a buffer
 * does not exist its backing VM pages might, and we have to invalidate
 * those as well or a getblk() will reinstate them.
 */
void
hammer_io_inval(hammer_volume_t volume, hammer_off_t zone2_offset)
{
	hammer_io_structure_t iou;
	hammer_off_t phys_offset;
	struct buf *bp;

	phys_offset = volume->ondisk->vol_buf_beg +
		      (zone2_offset & HAMMER_OFF_SHORT_MASK);
	crit_enter();
	if ((bp = findblk(volume->devvp, phys_offset)) != NULL)
		bp = getblk(volume->devvp, phys_offset, bp->b_bufsize, 0, 0);
	else
		bp = getblk(volume->devvp, phys_offset, HAMMER_BUFSIZE, 0, 0);
	if ((iou = (void *)LIST_FIRST(&bp->b_dep)) != NULL) {
		hammer_ref(&iou->io.lock);
		hammer_io_clear_modify(&iou->io, 1);
		bundirty(bp);
		iou->io.released = 0;
		BUF_KERNPROC(bp);
		iou->io.reclaim = 1;
		iou->io.waitdep = 1;
		KKASSERT(iou->io.lock.refs == 1);
		hammer_rel_buffer(&iou->buffer, 0);
		/*hammer_io_deallocate(bp);*/
	} else {
		KKASSERT((bp->b_flags & B_LOCKED) == 0);
		bundirty(bp);
		bp->b_flags |= B_NOCACHE|B_RELBUF;
		brelse(bp);
	}
	crit_exit();
}

/*
 * This routine is called on the last reference to a hammer structure.
 * The io is usually interlocked with io.loading and io.refs must be 1.
 *
 * This routine may return a non-NULL bp to the caller for dispoal.  Disposal
 * simply means the caller finishes decrementing the ref-count on the 
 * IO structure then brelse()'s the bp.  The bp may or may not still be
 * passively associated with the IO.
 * 
 * The only requirement here is that modified meta-data and volume-header
 * buffer may NOT be disassociated from the IO structure, and consequently
 * we also leave such buffers actively associated with the IO if they already
 * are (since the kernel can't do anything with them anyway).  Only the
 * flusher is allowed to write such buffers out.  Modified pure-data and
 * undo buffers are returned to the kernel but left passively associated
 * so we can track when the kernel writes the bp out.
 */
struct buf *
hammer_io_release(struct hammer_io *io, int flush)
{
	union hammer_io_structure *iou = (void *)io;
	struct buf *bp;

	if ((bp = io->bp) == NULL)
		return(NULL);

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
	 * Wait for the IO to complete if asked to.  This occurs when
	 * the buffer must be disposed of definitively during an umount
	 * or buffer invalidation.
	 */
	if (io->waitdep && io->running) {
		hammer_io_wait(io);
	}

	/*
	 * Return control of the buffer to the kernel (with the provisio
	 * that our bioops can override kernel decisions with regards to
	 * the buffer).
	 */
	if ((flush || io->reclaim) && io->modified == 0 && io->running == 0) {
		/*
		 * Always disassociate the bp if an explicit flush
		 * was requested and the IO completed with no error
		 * (so unmount can really clean up the structure).
		 */
		if (io->released) {
			regetblk(bp);
			BUF_KERNPROC(bp);
		} else {
			io->released = 1;
		}
		hammer_io_disassociate((hammer_io_structure_t)io);
		/* return the bp */
	} else if (io->modified) {
		/*
		 * Only certain IO types can be released to the kernel if
		 * the buffer has been modified.
		 *
		 * volume and meta-data IO types may only be explicitly
		 * flushed by HAMMER.
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
		bp = NULL;	/* bp left associated */
	} else if (io->released == 0) {
		/*
		 * Clean buffers can be generally released to the kernel.
		 * We leave the bp passively associated with the HAMMER
		 * structure and use bioops to disconnect it later on
		 * if the kernel wants to discard the buffer.
		 *
		 * We can steal the structure's ownership of the bp.
		 */
		io->released = 1;
		if (bp->b_flags & B_LOCKED) {
			hammer_io_disassociate(iou);
			/* return the bp */
		} else {
			if (io->reclaim) {
				hammer_io_disassociate(iou);
				/* return the bp */
			} else {
				/* return the bp (bp passively associated) */
			}
		}
	} else {
		/*
		 * A released buffer is passively associate with our
		 * hammer_io structure.  The kernel cannot destroy it
		 * without making a bioops call.  If the kernel (B_LOCKED)
		 * or we (reclaim) requested that the buffer be destroyed
		 * we destroy it, otherwise we do a quick get/release to
		 * reset its position in the kernel's LRU list.
		 *
		 * Leaving the buffer passively associated allows us to
		 * use the kernel's LRU buffer flushing mechanisms rather
		 * then rolling our own.
		 *
		 * XXX there are two ways of doing this.  We can re-acquire
		 * and passively release to reset the LRU, or not.
		 */
		if (io->running == 0) {
			regetblk(bp);
			if ((bp->b_flags & B_LOCKED) || io->reclaim) {
				hammer_io_disassociate(iou);
				/* return the bp */
			} else {
				/* return the bp (bp passively associated) */
			}
		} else {
			/*
			 * bp is left passively associated but we do not
			 * try to reacquire it.  Interactions with the io
			 * structure will occur on completion of the bp's
			 * I/O.
			 */
			bp = NULL;
		}
	}
	return(bp);
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
	 * NOTE: This call also finalizes the buffer's content (inval == 0).
	 *
	 * This is only legal when lock.refs == 1 (otherwise we might clear
	 * the modified bit while there are still users of the cluster
	 * modifying the data).
	 *
	 * Do this before potentially blocking so any attempt to modify the
	 * ondisk while we are blocked blocks waiting for us.
	 */
	hammer_ref(&io->lock);
	hammer_io_clear_modify(io, 0);
	hammer_unref(&io->lock);

	/*
	 * Transfer ownership to the kernel and initiate I/O.
	 */
	io->running = 1;
	io->hmp->io_running_space += io->bytes;
	hammer_count_io_running_write += io->bytes;
	bawrite(bp);
	hammer_io_flush_mark(io->volume);
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
		hammer_io_set_modlist(io);
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
 * Mark an entity as not being dirty any more and finalize any
 * delayed adjustments to the buffer.
 *
 * Delayed adjustments are an important performance enhancement, allowing
 * us to avoid recalculating B-Tree node CRCs over and over again when
 * making bulk-modifications to the B-Tree.
 *
 * If inval is non-zero delayed adjustments are ignored.
 *
 * This routine may dereference related btree nodes and cause the
 * buffer to be dereferenced.  The caller must own a reference on io.
 */
void
hammer_io_clear_modify(struct hammer_io *io, int inval)
{
	if (io->modified == 0)
		return;

	/*
	 * Take us off the mod-list and clear the modified bit.
	 */
	KKASSERT(io->mod_list != NULL);
	if (io->mod_list == &io->hmp->volu_list ||
	    io->mod_list == &io->hmp->meta_list) {
		io->hmp->locked_dirty_space -= io->bytes;
		hammer_count_dirtybufspace -= io->bytes;
	}
	TAILQ_REMOVE(io->mod_list, io, mod_entry);
	io->mod_list = NULL;
	io->modified = 0;

	/*
	 * If this bit is not set there are no delayed adjustments.
	 */
	if (io->gencrc == 0)
		return;
	io->gencrc = 0;

	/*
	 * Finalize requested CRCs.  The NEEDSCRC flag also holds a reference
	 * on the node (& underlying buffer).  Release the node after clearing
	 * the flag.
	 */
	if (io->type == HAMMER_STRUCTURE_META_BUFFER) {
		hammer_buffer_t buffer = (void *)io;
		hammer_node_t node;

restart:
		TAILQ_FOREACH(node, &buffer->clist, entry) {
			if ((node->flags & HAMMER_NODE_NEEDSCRC) == 0)
				continue;
			node->flags &= ~HAMMER_NODE_NEEDSCRC;
			KKASSERT(node->ondisk);
			if (inval == 0)
				node->ondisk->crc = crc32(&node->ondisk->crc + 1, HAMMER_BTREE_CRCSIZE);
			hammer_rel_node(node);
			goto restart;
		}
	}
	/* caller must still have ref on io */
	KKASSERT(io->lock.refs > 0);
}

/*
 * Clear the IO's modify list.  Even though the IO is no longer modified
 * it may still be on the lose_list.  This routine is called just before
 * the governing hammer_buffer is destroyed.
 */
void
hammer_io_clear_modlist(struct hammer_io *io)
{
	KKASSERT(io->modified == 0);
	if (io->mod_list) {
		crit_enter();	/* biodone race against list */
		KKASSERT(io->mod_list == &io->hmp->lose_list);
		TAILQ_REMOVE(io->mod_list, io, mod_entry);
		io->mod_list = NULL;
		crit_exit();
	}
}

static void
hammer_io_set_modlist(struct hammer_io *io)
{
	struct hammer_mount *hmp = io->hmp;

	KKASSERT(io->mod_list == NULL);

	switch(io->type) {
	case HAMMER_STRUCTURE_VOLUME:
		io->mod_list = &hmp->volu_list;
		hmp->locked_dirty_space += io->bytes;
		hammer_count_dirtybufspace += io->bytes;
		break;
	case HAMMER_STRUCTURE_META_BUFFER:
		io->mod_list = &hmp->meta_list;
		hmp->locked_dirty_space += io->bytes;
		hammer_count_dirtybufspace += io->bytes;
		break;
	case HAMMER_STRUCTURE_UNDO_BUFFER:
		io->mod_list = &hmp->undo_list;
		break;
	case HAMMER_STRUCTURE_DATA_BUFFER:
		io->mod_list = &hmp->data_list;
		break;
	}
	TAILQ_INSERT_TAIL(io->mod_list, io, mod_entry);
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
 * Post-IO completion kernel callback - MAY BE CALLED FROM INTERRUPT!
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

	/*
	 * Deal with people waiting for I/O to drain
	 */
	if (iou->io.running) {
		/*
		 * Deal with critical write errors.  Once a critical error
		 * has been flagged in hmp the UNDO FIFO will not be updated.
		 * That way crash recover will give us a consistent
		 * filesystem.
		 *
		 * Because of this we can throw away failed UNDO buffers.  If
		 * we throw away META or DATA buffers we risk corrupting
		 * the now read-only version of the filesystem visible to
		 * the user.  Clear B_ERROR so the buffer is not re-dirtied
		 * by the kernel and ref the io so it doesn't get thrown
		 * away.
		 */
		if (bp->b_flags & B_ERROR) {
			hammer_critical_error(iou->io.hmp, NULL, bp->b_error,
					      "while flushing meta-data");
			switch(iou->io.type) {
			case HAMMER_STRUCTURE_UNDO_BUFFER:
				break;
			default:
				if (iou->io.ioerror == 0) {
					iou->io.ioerror = 1;
					if (iou->io.lock.refs == 0)
						++hammer_count_refedbufs;
					hammer_ref(&iou->io.lock);
				}
				break;
			}
			bp->b_flags &= ~B_ERROR;
			bundirty(bp);
#if 0
			hammer_io_set_modlist(&iou->io);
			iou->io.modified = 1;
#endif
		}
		hammer_stats_disk_write += iou->io.bytes;
		hammer_count_io_running_write -= iou->io.bytes;
		iou->io.hmp->io_running_space -= iou->io.bytes;
		if (iou->io.hmp->io_running_space == 0)
			wakeup(&iou->io.hmp->io_running_space);
		KKASSERT(iou->io.hmp->io_running_space >= 0);
		iou->io.running = 0;
	} else {
		hammer_stats_disk_read += iou->io.bytes;
	}

	if (iou->io.waiting) {
		iou->io.waiting = 0;
		wakeup(iou);
	}

	/*
	 * If B_LOCKED is set someone wanted to deallocate the bp at some
	 * point, do it now if refs has become zero.
	 */
	if ((bp->b_flags & B_LOCKED) && iou->io.lock.refs == 0) {
		KKASSERT(iou->io.modified == 0);
		--hammer_count_io_locked;
		bp->b_flags &= ~B_LOCKED;
		hammer_io_deallocate(bp);
		/* structure may be dead now */
	}
}

/*
 * Callback from kernel when it wishes to deallocate a passively
 * associated structure.  This mostly occurs with clean buffers
 * but it may be possible for a holding structure to be marked dirty
 * while its buffer is passively associated.  The caller owns the bp.
 *
 * If we cannot disassociate we set B_LOCKED to prevent the buffer
 * from getting reused.
 *
 * WARNING: Because this can be called directly by getnewbuf we cannot
 * recurse into the tree.  If a bp cannot be immediately disassociated
 * our only recourse is to set B_LOCKED.
 *
 * WARNING: This may be called from an interrupt via hammer_io_complete()
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
		++hammer_count_io_locked;
	} else {
		/*
		 * Disassociate the BP.  If the io has no refs left we
		 * have to add it to the loose list.
		 */
		hammer_io_disassociate(iou);
		if (iou->io.type != HAMMER_STRUCTURE_VOLUME) {
			KKASSERT(iou->io.bp == NULL);
			KKASSERT(iou->io.mod_list == NULL);
			crit_enter();	/* biodone race against list */
			iou->io.mod_list = &iou->io.hmp->lose_list;
			TAILQ_INSERT_TAIL(iou->io.mod_list, &iou->io, mod_entry);
			crit_exit();
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
		if ((bp->b_flags & B_LOCKED) == 0) {
			bp->b_flags |= B_LOCKED;
			++hammer_count_io_locked;
		}
		return(1);
	}

	/*
	 * We can only clear the modified bit if the IO is not currently
	 * undergoing modification.  Otherwise we may miss changes.
	 *
	 * Only data and undo buffers can reach here.  These buffers do
	 * not have terminal crc functions but we temporarily reference
	 * the IO anyway, just in case.
	 */
	if (io->modify_refs == 0 && io->modified) {
		hammer_ref(&io->lock);
		hammer_io_clear_modify(io, 0);
		hammer_unref(&io->lock);
	} else if (io->modified) {
		KKASSERT(io->type == HAMMER_STRUCTURE_DATA_BUFFER);
	}

	/*
	 * The kernel is going to start the IO, set io->running.
	 */
	KKASSERT(io->running == 0);
	io->running = 1;
	io->hmp->io_running_space += io->bytes;
	hammer_count_io_running_write += io->bytes;
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
 * disk media.  The bio may be issued asynchronously.  If leaf is non-NULL
 * we validate the CRC.
 *
 * We must check for the presence of a HAMMER buffer to handle the case
 * where the reblocker has rewritten the data (which it does via the HAMMER
 * buffer system, not via the high-level vnode buffer cache), but not yet
 * committed the buffer to the media. 
 */
int
hammer_io_direct_read(hammer_mount_t hmp, struct bio *bio,
		      hammer_btree_leaf_elm_t leaf)
{
	hammer_off_t buf_offset;
	hammer_off_t zone2_offset;
	hammer_volume_t volume;
	struct buf *bp;
	struct bio *nbio;
	int vol_no;
	int error;

	buf_offset = bio->bio_offset;
	KKASSERT((buf_offset & HAMMER_OFF_ZONE_MASK) ==
		 HAMMER_ZONE_LARGE_DATA);

	/*
	 * The buffer cache may have an aliased buffer (the reblocker can
	 * write them).  If it does we have to sync any dirty data before
	 * we can build our direct-read.  This is a non-critical code path.
	 */
	bp = bio->bio_buf;
	hammer_sync_buffers(hmp, buf_offset, bp->b_bufsize);

	/*
	 * Resolve to a zone-2 offset.  The conversion just requires
	 * munging the top 4 bits but we want to abstract it anyway
	 * so the blockmap code can verify the zone assignment.
	 */
	zone2_offset = hammer_blockmap_lookup(hmp, buf_offset, &error);
	if (error)
		goto done;
	KKASSERT((zone2_offset & HAMMER_OFF_ZONE_MASK) ==
		 HAMMER_ZONE_RAW_BUFFER);

	/*
	 * Resolve volume and raw-offset for 3rd level bio.  The
	 * offset will be specific to the volume.
	 */
	vol_no = HAMMER_VOL_DECODE(zone2_offset);
	volume = hammer_get_volume(hmp, vol_no, &error);
	if (error == 0 && zone2_offset >= volume->maxbuf_off)
		error = EIO;

	if (error == 0) {
		/*
		 * 3rd level bio
		 */
		nbio = push_bio(bio);
		nbio->bio_offset = volume->ondisk->vol_buf_beg +
				   (zone2_offset & HAMMER_OFF_SHORT_MASK);
#if 0
		/*
		 * XXX disabled - our CRC check doesn't work if the OS
		 * does bogus_page replacement on the direct-read.
		 */
		if (leaf && hammer_verify_data) {
			nbio->bio_done = hammer_io_direct_read_complete;
			nbio->bio_caller_info1.uvalue32 = leaf->data_crc;
		}
#endif
		hammer_stats_disk_read += bp->b_bufsize;
		vn_strategy(volume->devvp, nbio);
	}
	hammer_rel_volume(volume, 0);
done:
	if (error) {
		kprintf("hammer_direct_read: failed @ %016llx\n",
			zone2_offset);
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	}
	return(error);
}

#if 0
/*
 * On completion of the BIO this callback must check the data CRC
 * and chain to the previous bio.
 */
static
void
hammer_io_direct_read_complete(struct bio *nbio)
{
	struct bio *obio;
	struct buf *bp;
	u_int32_t rec_crc = nbio->bio_caller_info1.uvalue32;

	bp = nbio->bio_buf;
	if (crc32(bp->b_data, bp->b_bufsize) != rec_crc) {
		kprintf("HAMMER: data_crc error @%016llx/%d\n",
			nbio->bio_offset, bp->b_bufsize);
		if (hammer_debug_debug)
			Debugger("");
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
	}
	obio = pop_bio(nbio);
	biodone(obio);
}
#endif

/*
 * Write a buffer associated with a front-end vnode directly to the
 * disk media.  The bio may be issued asynchronously.
 *
 * The BIO is associated with the specified record and RECF_DIRECT_IO
 * is set.  The recorded is added to its object.
 */
int
hammer_io_direct_write(hammer_mount_t hmp, hammer_record_t record,
		       struct bio *bio)
{
	hammer_btree_leaf_elm_t leaf = &record->leaf;
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
	    leaf->data_len >= HAMMER_BUFSIZE) {
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
			bp = bio->bio_buf;
			KKASSERT((bp->b_bufsize & HAMMER_BUFMASK) == 0);
			/*
			hammer_del_buffers(hmp, buf_offset,
					   zone2_offset, bp->b_bufsize);
			*/

			/*
			 * Second level bio - cached zone2 offset.
			 *
			 * (We can put our bio_done function in either the
			 *  2nd or 3rd level).
			 */
			nbio = push_bio(bio);
			nbio->bio_offset = zone2_offset;
			nbio->bio_done = hammer_io_direct_write_complete;
			nbio->bio_caller_info1.ptr = record;
			record->zone2_offset = zone2_offset;
			record->flags |= HAMMER_RECF_DIRECT_IO |
					 HAMMER_RECF_DIRECT_INVAL;

			/*
			 * Third level bio - raw offset specific to the
			 * correct volume.
			 */
			zone2_offset &= HAMMER_OFF_SHORT_MASK;
			nbio = push_bio(nbio);
			nbio->bio_offset = volume->ondisk->vol_buf_beg +
					   zone2_offset;
			hammer_stats_disk_write += bp->b_bufsize;
			vn_strategy(volume->devvp, nbio);
			hammer_io_flush_mark(volume);
		}
		hammer_rel_volume(volume, 0);
	} else {
		/* 
		 * Must fit in a standard HAMMER buffer.  In this case all
		 * consumers use the HAMMER buffer system and RECF_DIRECT_IO
		 * does not need to be set-up.
		 */
		KKASSERT(((buf_offset ^ (buf_offset + leaf->data_len - 1)) & ~HAMMER_BUFMASK64) == 0);
		buffer = NULL;
		ptr = hammer_bread(hmp, buf_offset, &error, &buffer);
		if (error == 0) {
			bp = bio->bio_buf;
			bp->b_flags |= B_AGE;
			hammer_io_modify(&buffer->io, 1);
			bcopy(bp->b_data, ptr, leaf->data_len);
			hammer_io_modify_done(&buffer->io);
			hammer_rel_buffer(buffer, 0);
			bp->b_resid = 0;
			biodone(bio);
		}
	}
	if (error == 0) {
		/*
		 * The record is all setup now, add it.  Potential conflics
		 * have already been dealt with.
		 */
		error = hammer_mem_add(record);
		KKASSERT(error == 0);
	} else {
		/*
		 * Major suckage occured.
		 */
		kprintf("hammer_direct_write: failed @ %016llx\n",
			leaf->data_offset);
		bp = bio->bio_buf;
		bp->b_resid = 0;
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
		biodone(bio);
		record->flags |= HAMMER_RECF_DELETED_FE;
		hammer_rel_mem_record(record);
	}
	return(error);
}

/*
 * On completion of the BIO this callback must disconnect
 * it from the hammer_record and chain to the previous bio.
 *
 * An I/O error forces the mount to read-only.  Data buffers
 * are not B_LOCKED like meta-data buffers are, so we have to
 * throw the buffer away to prevent the kernel from retrying.
 */
static
void
hammer_io_direct_write_complete(struct bio *nbio)
{
	struct bio *obio;
	struct buf *bp;
	hammer_record_t record = nbio->bio_caller_info1.ptr;

	bp = nbio->bio_buf;
	obio = pop_bio(nbio);
	if (bp->b_flags & B_ERROR) {
		hammer_critical_error(record->ip->hmp, record->ip,
				      bp->b_error,
				      "while writing bulk data");
		bp->b_flags |= B_INVAL;
	}
	biodone(obio);

	KKASSERT(record != NULL);
	KKASSERT(record->flags & HAMMER_RECF_DIRECT_IO);
	record->flags &= ~HAMMER_RECF_DIRECT_IO;
	if (record->flags & HAMMER_RECF_DIRECT_WAIT) {
		record->flags &= ~HAMMER_RECF_DIRECT_WAIT;
		wakeup(&record->flags);
	}
}


/*
 * This is called before a record is either committed to the B-Tree
 * or destroyed, to resolve any associated direct-IO. 
 *
 * (1) We must wait for any direct-IO related to the record to complete.
 *
 * (2) We must remove any buffer cache aliases for data accessed via
 *     leaf->data_offset or zone2_offset so non-direct-IO consumers  
 *     (the mirroring and reblocking code) do not see stale data.
 */
void
hammer_io_direct_wait(hammer_record_t record)
{
	/*
	 * Wait for I/O to complete
	 */
	if (record->flags & HAMMER_RECF_DIRECT_IO) {
		crit_enter();
		while (record->flags & HAMMER_RECF_DIRECT_IO) {
			record->flags |= HAMMER_RECF_DIRECT_WAIT;
			tsleep(&record->flags, 0, "hmdiow", 0);
		}
		crit_exit();
	}

	/*
	 * Invalidate any related buffer cache aliases.
	 */
	if (record->flags & HAMMER_RECF_DIRECT_INVAL) {
		KKASSERT(record->leaf.data_offset);
		hammer_del_buffers(record->ip->hmp,
				   record->leaf.data_offset,
				   record->zone2_offset,
				   record->leaf.data_len);
		record->flags &= ~HAMMER_RECF_DIRECT_INVAL;
	}
}

/*
 * This is called to remove the second-level cached zone-2 offset from
 * frontend buffer cache buffers, now stale due to a data relocation.
 * These offsets are generated by cluster_read() via VOP_BMAP, or directly
 * by hammer_vop_strategy_read().
 *
 * This is rather nasty because here we have something like the reblocker
 * scanning the raw B-Tree with no held references on anything, really,
 * other then a shared lock on the B-Tree node, and we have to access the
 * frontend's buffer cache to check for and clean out the association.
 * Specifically, if the reblocker is moving data on the disk, these cached
 * offsets will become invalid.
 *
 * Only data record types associated with the large-data zone are subject
 * to direct-io and need to be checked.
 *
 */
void
hammer_io_direct_uncache(hammer_mount_t hmp, hammer_btree_leaf_elm_t leaf)
{
	struct hammer_inode_info iinfo;
	int zone;

	if (leaf->base.rec_type != HAMMER_RECTYPE_DATA)
		return;
	zone = HAMMER_ZONE_DECODE(leaf->data_offset);
	if (zone != HAMMER_ZONE_LARGE_DATA_INDEX)
		return;
	iinfo.obj_id = leaf->base.obj_id;
	iinfo.obj_asof = 0;	/* unused */
	iinfo.obj_localization = leaf->base.localization &
				 HAMMER_LOCALIZE_PSEUDOFS_MASK;
	iinfo.u.leaf = leaf;
	hammer_scan_inode_snapshots(hmp, &iinfo,
				    hammer_io_direct_uncache_callback,
				    leaf);
}

static int
hammer_io_direct_uncache_callback(hammer_inode_t ip, void *data)
{
	hammer_inode_info_t iinfo = data;
	hammer_off_t data_offset;
	hammer_off_t file_offset;
	struct vnode *vp;
	struct buf *bp;
	int blksize;

	if (ip->vp == NULL)
		return(0);
	data_offset = iinfo->u.leaf->data_offset;
	file_offset = iinfo->u.leaf->base.key - iinfo->u.leaf->data_len;
	blksize = iinfo->u.leaf->data_len;
	KKASSERT((blksize & HAMMER_BUFMASK) == 0);

	hammer_ref(&ip->lock);
	if (hammer_get_vnode(ip, &vp) == 0) {
		if ((bp = findblk(ip->vp, file_offset)) != NULL &&
		    bp->b_bio2.bio_offset != NOOFFSET) {
			bp = getblk(ip->vp, file_offset, blksize, 0, 0);
			bp->b_bio2.bio_offset = NOOFFSET;
			brelse(bp);
		}
		vput(vp);
	}
	hammer_rel_inode(ip, 0);
	return(0);
}


/*
 * This function is called when writes may have occured on the volume,
 * indicating that the device may be holding cached writes.
 */
static void
hammer_io_flush_mark(hammer_volume_t volume)
{
	volume->vol_flags |= HAMMER_VOLF_NEEDFLUSH;
}

/*
 * This function ensures that the device has flushed any cached writes out.
 */
void
hammer_io_flush_sync(hammer_mount_t hmp)
{
	hammer_volume_t volume;
	struct buf *bp_base = NULL;
	struct buf *bp;

	RB_FOREACH(volume, hammer_vol_rb_tree, &hmp->rb_vols_root) {
		if (volume->vol_flags & HAMMER_VOLF_NEEDFLUSH) {
			volume->vol_flags &= ~HAMMER_VOLF_NEEDFLUSH;
			bp = getpbuf(NULL);
			bp->b_bio1.bio_offset = 0;
			bp->b_bufsize = 0;
			bp->b_bcount = 0;
			bp->b_cmd = BUF_CMD_FLUSH;
			bp->b_bio1.bio_caller_info1.cluster_head = bp_base;
			bp->b_bio1.bio_done = hammer_io_flush_sync_done;
			bp->b_flags |= B_ASYNC;
			bp_base = bp;
			vn_strategy(volume->devvp, &bp->b_bio1);
		}
	}
	while ((bp = bp_base) != NULL) {
		bp_base = bp->b_bio1.bio_caller_info1.cluster_head;
		while (bp->b_cmd != BUF_CMD_DONE) {
			crit_enter();
			tsleep_interlock(&bp->b_cmd);
			if (bp->b_cmd != BUF_CMD_DONE)
				tsleep(&bp->b_cmd, 0, "hmrFLS", 0);
			crit_exit();
		}
		bp->b_flags &= ~B_ASYNC;
		relpbuf(bp, NULL);
	}
}

/*
 * Callback to deal with completed flush commands to the device.
 */
static void
hammer_io_flush_sync_done(struct bio *bio)
{
	struct buf *bp;

	bp = bio->bio_buf;
	bp->b_cmd = BUF_CMD_DONE;
	wakeup(&bp->b_cmd);
}

