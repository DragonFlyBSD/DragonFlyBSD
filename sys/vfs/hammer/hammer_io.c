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
 *
 * The io_token is required for anything which might race bioops and bio_done
 * callbacks, with one exception: A successful hammer_try_interlock_norefs().
 * the fs_token will be held in all other cases.
 */

#include "hammer.h"
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/buf.h>

#include <sys/buf2.h>

static void hammer_io_modify(hammer_io_t io, int count);
static void hammer_io_deallocate(struct buf *bp);
static void hammer_indirect_callback(struct bio *bio);
static void hammer_io_direct_write_complete(struct bio *nbio);
static int hammer_io_direct_uncache_callback(hammer_inode_t ip, void *data);
static void hammer_io_set_modlist(struct hammer_io *io);
static void hammer_io_flush_mark(hammer_volume_t volume);

static int
hammer_mod_rb_compare(hammer_io_t io1, hammer_io_t io2)
{
	hammer_off_t io1_offset;
	hammer_off_t io2_offset;

	io1_offset = ((io1->offset & HAMMER_OFF_SHORT_MASK) << 8) |
		     io1->volume->vol_no;
	io2_offset = ((io2->offset & HAMMER_OFF_SHORT_MASK) << 8) |
		     io2->volume->vol_no;

	if (io1_offset < io2_offset)
		return(-1);
	if (io1_offset > io2_offset)
		return(1);
	return(0);
}

RB_GENERATE(hammer_mod_rb_tree, hammer_io, rb_node, hammer_mod_rb_compare);

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
 * structure.  The io must be interlocked and marked appropriately for
 * reclamation.
 *
 * The io must be in a released state with the io->bp owned and
 * locked by the caller of this function.  When not called from an
 * io_deallocate() this cannot race an io_deallocate() since the
 * kernel would be unable to get the buffer lock in that case.
 * (The released state in this case means we own the bp, not the
 * hammer_io structure).
 *
 * The io may have 0 or 1 references depending on who called us.  The
 * caller is responsible for dealing with the refs.
 *
 * This call can only be made when no action is required on the buffer.
 *
 * This function is guaranteed not to race against anything because we
 * own both the io lock and the bp lock and are interlocked with no
 * references.
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
		atomic_add_int(&hammer_count_io_locked, -1);
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
	case HAMMER_STRUCTURE_DUMMY:
		panic("hammer_io_disassociate: bad io type");
		break;
	}
}

/*
 * Wait for any physical IO to complete
 *
 * XXX we aren't interlocked against a spinlock or anything so there
 *     is a small window in the interlock / io->running == 0 test.
 */
void
hammer_io_wait(hammer_io_t io)
{
	if (io->running) {
		hammer_mount_t hmp = io->hmp;

		lwkt_gettoken(&hmp->io_token);
		while (io->running) {
			io->waiting = 1;
			tsleep_interlock(io, 0);
			if (io->running)
				tsleep(io, PINTERLOCKED, "hmrflw", hz);
		}
		lwkt_reltoken(&hmp->io_token);
	}
}

/*
 * Wait for all currently queued HAMMER-initiated I/Os to complete.
 *
 * This is not supposed to count direct I/O's but some can leak
 * through (for non-full-sized direct I/Os).
 */
void
hammer_io_wait_all(hammer_mount_t hmp, const char *ident, int doflush)
{
	struct hammer_io iodummy;
	hammer_io_t io;

	/*
	 * Degenerate case, no I/O is running
	 */
	lwkt_gettoken(&hmp->io_token);
	if (TAILQ_EMPTY(&hmp->iorun_list)) {
		lwkt_reltoken(&hmp->io_token);
		if (doflush)
			hammer_io_flush_sync(hmp);
		return;
	}
	bzero(&iodummy, sizeof(iodummy));
	iodummy.type = HAMMER_STRUCTURE_DUMMY;

	/*
	 * Add placemarker and then wait until it becomes the head of
	 * the list.
	 */
	TAILQ_INSERT_TAIL(&hmp->iorun_list, &iodummy, iorun_entry);
	while (TAILQ_FIRST(&hmp->iorun_list) != &iodummy) {
		tsleep(&iodummy, 0, ident, 0);
	}

	/*
	 * Chain in case several placemarkers are present.
	 */
	TAILQ_REMOVE(&hmp->iorun_list, &iodummy, iorun_entry);
	io = TAILQ_FIRST(&hmp->iorun_list);
	if (io && io->type == HAMMER_STRUCTURE_DUMMY)
		wakeup(io);
	lwkt_reltoken(&hmp->io_token);

	if (doflush)
		hammer_io_flush_sync(hmp);
}

/*
 * Clear a flagged error condition on a I/O buffer.  The caller must hold
 * its own ref on the buffer.
 */
void
hammer_io_clear_error(struct hammer_io *io)
{
	hammer_mount_t hmp = io->hmp;

	lwkt_gettoken(&hmp->io_token);
	if (io->ioerror) {
		io->ioerror = 0;
		hammer_rel(&io->lock);
		KKASSERT(hammer_isactive(&io->lock));
	}
	lwkt_reltoken(&hmp->io_token);
}

void
hammer_io_clear_error_noassert(struct hammer_io *io)
{
	hammer_mount_t hmp = io->hmp;

	lwkt_gettoken(&hmp->io_token);
	if (io->ioerror) {
		io->ioerror = 0;
		hammer_rel(&io->lock);
	}
	lwkt_reltoken(&hmp->io_token);
}

/*
 * This is an advisory function only which tells the buffer cache
 * the bp is not a meta-data buffer, even though it is backed by
 * a block device.
 *
 * This is used by HAMMER's reblocking code to avoid trying to
 * swapcache the filesystem's data when it is read or written
 * by the reblocking code.
 *
 * The caller has a ref on the buffer preventing the bp from
 * being disassociated from it.
 */
void
hammer_io_notmeta(hammer_buffer_t buffer)
{
	if ((buffer->io.bp->b_flags & B_NOTMETA) == 0) {
		hammer_mount_t hmp = buffer->io.hmp;

		lwkt_gettoken(&hmp->io_token);
		buffer->io.bp->b_flags |= B_NOTMETA;
		lwkt_reltoken(&hmp->io_token);
	}
}

/*
 * Load bp for a HAMMER structure.  The io must be exclusively locked by
 * the caller.
 *
 * This routine is mostly used on meta-data and small-data blocks.  Generally
 * speaking HAMMER assumes some locality of reference and will cluster.
 *
 * Note that the caller (hammer_ondisk.c) may place further restrictions
 * on clusterability via the limit (in bytes).  Typically large-data
 * zones cannot be clustered due to their mixed buffer sizes.  This is
 * not an issue since such clustering occurs in hammer_vnops at the
 * regular file layer, whereas this is the buffered block device layer.
 *
 * No I/O callbacks can occur while we hold the buffer locked.
 */
int
hammer_io_read(struct vnode *devvp, struct hammer_io *io, int limit)
{
	struct buf *bp;
	int   error;

	if ((bp = io->bp) == NULL) {
		atomic_add_long(&hammer_count_io_running_read, io->bytes);
		if (hammer_cluster_enable && limit > io->bytes) {
			error = cluster_read(devvp, io->offset + limit,
					     io->offset, io->bytes,
					     HAMMER_CLUSTER_SIZE,
					     HAMMER_CLUSTER_SIZE,
					     &io->bp);
		} else {
			error = bread(devvp, io->offset, io->bytes, &io->bp);
		}
		hammer_stats_disk_read += io->bytes;
		atomic_add_long(&hammer_count_io_running_read, -io->bytes);

		/*
		 * The code generally assumes b_ops/b_dep has been set-up,
		 * even if we error out here.
		 */
		bp = io->bp;
		if ((hammer_debug_io & 0x0001) && (bp->b_flags & B_IODEBUG)) {
			const char *metatype;

			switch(io->type) {
			case HAMMER_STRUCTURE_VOLUME:
				metatype = "volume";
				break;
			case HAMMER_STRUCTURE_META_BUFFER:
				switch(((struct hammer_buffer *)io)->
					zoneX_offset & HAMMER_OFF_ZONE_MASK) {
				case HAMMER_ZONE_BTREE:
					metatype = "btree";
					break;
				case HAMMER_ZONE_META:
					metatype = "meta";
					break;
				case HAMMER_ZONE_FREEMAP:
					metatype = "freemap";
					break;
				default:
					metatype = "meta?";
					break;
				}
				break;
			case HAMMER_STRUCTURE_DATA_BUFFER:
				metatype = "data";
				break;
			case HAMMER_STRUCTURE_UNDO_BUFFER:
				metatype = "undo";
				break;
			default:
				metatype = "unknown";
				break;
			}
			kprintf("doff %016jx %s\n",
				(intmax_t)bp->b_bio2.bio_offset,
				metatype);
		}
		bp->b_flags &= ~B_IODEBUG;
		bp->b_ops = &hammer_bioops;
		KKASSERT(LIST_FIRST(&bp->b_dep) == NULL);

		/* io->worklist is locked by the io lock */
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
 *
 * No I/O callbacks can occur while we hold the buffer locked.
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

		/* io->worklist is locked by the io lock */
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
 * Advance the activity count on the underlying buffer because
 * HAMMER does not getblk/brelse on every access.
 *
 * The io->bp cannot go away while the buffer is referenced.
 */
void
hammer_io_advance(struct hammer_io *io)
{
	if (io->bp)
		buf_act_advance(io->bp);
}

/*
 * Remove potential device level aliases against buffers managed by high level
 * vnodes.  Aliases can also be created due to mixed buffer sizes or via
 * direct access to the backing store device.
 *
 * This is nasty because the buffers are also VMIO-backed.  Even if a buffer
 * does not exist its backing VM pages might, and we have to invalidate
 * those as well or a getblk() will reinstate them.
 *
 * Buffer cache buffers associated with hammer_buffers cannot be
 * invalidated.
 */
int
hammer_io_inval(hammer_volume_t volume, hammer_off_t zone2_offset)
{
	hammer_io_structure_t iou;
	hammer_mount_t hmp;
	hammer_off_t phys_offset;
	struct buf *bp;
	int error;

	hmp = volume->io.hmp;
	lwkt_gettoken(&hmp->io_token);

	/*
	 * If a device buffer already exists for the specified physical
	 * offset use that, otherwise instantiate a buffer to cover any
	 * related VM pages, set BNOCACHE, and brelse().
	 */
	phys_offset = volume->ondisk->vol_buf_beg +
		      (zone2_offset & HAMMER_OFF_SHORT_MASK);
	if ((bp = findblk(volume->devvp, phys_offset, 0)) != NULL)
		bremfree(bp);
	else
		bp = getblk(volume->devvp, phys_offset, HAMMER_BUFSIZE, 0, 0);

	if ((iou = (void *)LIST_FIRST(&bp->b_dep)) != NULL) {
#if 0
		hammer_ref(&iou->io.lock);
		hammer_io_clear_modify(&iou->io, 1);
		bundirty(bp);
		iou->io.released = 0;
		BUF_KERNPROC(bp);
		iou->io.reclaim = 1;
		iou->io.waitdep = 1;	/* XXX this is a fs_token field */
		KKASSERT(hammer_isactive(&iou->io.lock) == 1);
		hammer_rel_buffer(&iou->buffer, 0);
		/*hammer_io_deallocate(bp);*/
#endif
		bqrelse(bp);
		error = EAGAIN;
	} else {
		KKASSERT((bp->b_flags & B_LOCKED) == 0);
		bundirty(bp);
		bp->b_flags |= B_NOCACHE|B_RELBUF;
		brelse(bp);
		error = 0;
	}
	lwkt_reltoken(&hmp->io_token);
	return(error);
}

/*
 * This routine is called on the last reference to a hammer structure.
 * The io must be interlocked with a refcount of zero.  The hammer structure
 * will remain interlocked on return.
 *
 * This routine may return a non-NULL bp to the caller for dispoal.
 * The caller typically brelse()'s the bp.
 *
 * The bp may or may not still be passively associated with the IO.  It
 * will remain passively associated if it is unreleasable (e.g. a modified
 * meta-data buffer).
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
			hammer_io_flush(io, 0);
		} else if (bp->b_flags & B_LOCKED) {
			switch(io->type) {
			case HAMMER_STRUCTURE_DATA_BUFFER:
				hammer_io_flush(io, 0);
				break;
			case HAMMER_STRUCTURE_UNDO_BUFFER:
				hammer_io_flush(io, hammer_undo_reclaim(io));
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
				bp->b_flags |= B_CLUSTEROK;
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
 *
 * The locked io or io reference prevents a flush from being initiated
 * by the kernel.
 */
void
hammer_io_flush(struct hammer_io *io, int reclaim)
{
	struct buf *bp;
	hammer_mount_t hmp;

	/*
	 * Degenerate case - nothing to flush if nothing is dirty.
	 */
	if (io->modified == 0)
		return;

	KKASSERT(io->bp);
	KKASSERT(io->modify_refs <= 0);

	/*
	 * Acquire ownership of the bp, particularly before we clear our
	 * modified flag.
	 *
	 * We are going to bawrite() this bp.  Don't leave a window where
	 * io->released is set, we actually own the bp rather then our
	 * buffer.
	 *
	 * The io_token should not be required here as only
	 */
	hmp = io->hmp;
	bp = io->bp;
	if (io->released) {
		regetblk(bp);
		/* BUF_KERNPROC(io->bp); */
		/* io->released = 0; */
		KKASSERT(io->released);
		KKASSERT(io->bp == bp);
	} else {
		io->released = 1;
	}

	if (reclaim) {
		io->reclaim = 1;
		if ((bp->b_flags & B_LOCKED) == 0) {
			bp->b_flags |= B_LOCKED;
			atomic_add_int(&hammer_count_io_locked, 1);
		}
	}

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
	hammer_rel(&io->lock);

	if (hammer_debug_io & 0x0002)
		kprintf("hammer io_write %016jx\n", bp->b_bio1.bio_offset);

	/*
	 * Transfer ownership to the kernel and initiate I/O.
	 *
	 * NOTE: We do not hold io_token so an atomic op is required to
	 *	 update io_running_space.
	 */
	io->running = 1;
	atomic_add_long(&hmp->io_running_space, io->bytes);
	atomic_add_long(&hammer_count_io_running_write, io->bytes);
	lwkt_gettoken(&hmp->io_token);
	TAILQ_INSERT_TAIL(&hmp->iorun_list, io, iorun_entry);
	lwkt_reltoken(&hmp->io_token);
	cluster_awrite(bp);
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
 *
 * The referenced io prevents races.
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
	KKASSERT(hammer_isactive(&io->lock) && io->bp != NULL);
	io->modify_refs += count;
	if (io->modified && io->released == 0)
		return;

	/*
	 * NOTE: It is important not to set the modified bit
	 *	 until after we have acquired the bp or we risk
	 *	 racing against checkwrite.
	 */
	hammer_lock_ex(&io->lock);
	if (io->released) {
		regetblk(io->bp);
		BUF_KERNPROC(io->bp);
		io->released = 0;
	}
	if (io->modified == 0) {
		hammer_io_set_modlist(io);
		io->modified = 1;
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

/*
 * The write interlock blocks other threads trying to modify a buffer
 * (they block in hammer_io_modify()) after us, or blocks us while other
 * threads are in the middle of modifying a buffer.
 *
 * The caller also has a ref on the io, however if we are not careful
 * we will race bioops callbacks (checkwrite).  To deal with this
 * we must at least acquire and release the io_token, and it is probably
 * better to hold it through the setting of modify_refs.
 */
void
hammer_io_write_interlock(hammer_io_t io)
{
	hammer_mount_t hmp = io->hmp;

	lwkt_gettoken(&hmp->io_token);
	while (io->modify_refs != 0) {
		io->waitmod = 1;
		tsleep(io, 0, "hmrmod", 0);
	}
	io->modify_refs = -1;
	lwkt_reltoken(&hmp->io_token);
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
		hammer_generate_undo(trans,
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
		hammer_generate_undo(trans,
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
	hammer_mount_t hmp;

	/*
	 * io_token is needed to avoid races on mod_root
	 */
	if (io->modified == 0)
		return;
	hmp = io->hmp;
	lwkt_gettoken(&hmp->io_token);
	if (io->modified == 0) {
		lwkt_reltoken(&hmp->io_token);
		return;
	}

	/*
	 * Take us off the mod-list and clear the modified bit.
	 */
	KKASSERT(io->mod_root != NULL);
	if (io->mod_root == &io->hmp->volu_root ||
	    io->mod_root == &io->hmp->meta_root) {
		io->hmp->locked_dirty_space -= io->bytes;
		atomic_add_long(&hammer_count_dirtybufspace, -io->bytes);
	}
	RB_REMOVE(hammer_mod_rb_tree, io->mod_root, io);
	io->mod_root = NULL;
	io->modified = 0;

	lwkt_reltoken(&hmp->io_token);

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
	KKASSERT(hammer_isactive(&io->lock));
}

/*
 * Clear the IO's modify list.  Even though the IO is no longer modified
 * it may still be on the lose_root.  This routine is called just before
 * the governing hammer_buffer is destroyed.
 *
 * mod_root requires io_token protection.
 */
void
hammer_io_clear_modlist(struct hammer_io *io)
{
	hammer_mount_t hmp = io->hmp;

	KKASSERT(io->modified == 0);
	if (io->mod_root) {
		lwkt_gettoken(&hmp->io_token);
		if (io->mod_root) {
			KKASSERT(io->mod_root == &io->hmp->lose_root);
			RB_REMOVE(hammer_mod_rb_tree, io->mod_root, io);
			io->mod_root = NULL;
		}
		lwkt_reltoken(&hmp->io_token);
	}
}

static void
hammer_io_set_modlist(struct hammer_io *io)
{
	struct hammer_mount *hmp = io->hmp;

	lwkt_gettoken(&hmp->io_token);
	KKASSERT(io->mod_root == NULL);

	switch(io->type) {
	case HAMMER_STRUCTURE_VOLUME:
		io->mod_root = &hmp->volu_root;
		hmp->locked_dirty_space += io->bytes;
		atomic_add_long(&hammer_count_dirtybufspace, io->bytes);
		break;
	case HAMMER_STRUCTURE_META_BUFFER:
		io->mod_root = &hmp->meta_root;
		hmp->locked_dirty_space += io->bytes;
		atomic_add_long(&hammer_count_dirtybufspace, io->bytes);
		break;
	case HAMMER_STRUCTURE_UNDO_BUFFER:
		io->mod_root = &hmp->undo_root;
		break;
	case HAMMER_STRUCTURE_DATA_BUFFER:
		io->mod_root = &hmp->data_root;
		break;
	case HAMMER_STRUCTURE_DUMMY:
		panic("hammer_io_set_modlist: bad io type");
		break; /* NOT REACHED */
	}
	if (RB_INSERT(hammer_mod_rb_tree, io->mod_root, io)) {
		panic("hammer_io_set_modlist: duplicate entry");
		/* NOT REACHED */
	}
	lwkt_reltoken(&hmp->io_token);
}

/************************************************************************
 *				HAMMER_BIOOPS				*
 ************************************************************************
 *
 */

/*
 * Pre-IO initiation kernel callback - cluster build only
 *
 * bioops callback - hold io_token
 */
static void
hammer_io_start(struct buf *bp)
{
	/* nothing to do, so io_token not needed */
}

/*
 * Post-IO completion kernel callback - MAY BE CALLED FROM INTERRUPT!
 *
 * NOTE: HAMMER may modify a data buffer after we have initiated write
 *	 I/O.
 *
 * NOTE: MPSAFE callback
 *
 * bioops callback - hold io_token
 */
static void
hammer_io_complete(struct buf *bp)
{
	union hammer_io_structure *iou = (void *)LIST_FIRST(&bp->b_dep);
	struct hammer_mount *hmp = iou->io.hmp;
	struct hammer_io *ionext;

	lwkt_gettoken(&hmp->io_token);

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
			lwkt_gettoken(&hmp->fs_token);
			hammer_critical_error(hmp, NULL, bp->b_error,
					      "while flushing meta-data");
			lwkt_reltoken(&hmp->fs_token);

			switch(iou->io.type) {
			case HAMMER_STRUCTURE_UNDO_BUFFER:
				break;
			default:
				if (iou->io.ioerror == 0) {
					iou->io.ioerror = 1;
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
		atomic_add_long(&hammer_count_io_running_write, -iou->io.bytes);
		atomic_add_long(&hmp->io_running_space, -iou->io.bytes);
		KKASSERT(hmp->io_running_space >= 0);
		iou->io.running = 0;

		/*
		 * Remove from iorun list and wakeup any multi-io waiter(s).
		 */
		if (TAILQ_FIRST(&hmp->iorun_list) == &iou->io) {
			ionext = TAILQ_NEXT(&iou->io, iorun_entry);
			if (ionext && ionext->type == HAMMER_STRUCTURE_DUMMY)
				wakeup(ionext);
		}
		TAILQ_REMOVE(&hmp->iorun_list, &iou->io, iorun_entry);
	} else {
		hammer_stats_disk_read += iou->io.bytes;
	}

	if (iou->io.waiting) {
		iou->io.waiting = 0;
		wakeup(iou);
	}

	/*
	 * If B_LOCKED is set someone wanted to deallocate the bp at some
	 * point, try to do it now.  The operation will fail if there are
	 * refs or if hammer_io_deallocate() is unable to gain the
	 * interlock.
	 */
	if (bp->b_flags & B_LOCKED) {
		atomic_add_int(&hammer_count_io_locked, -1);
		bp->b_flags &= ~B_LOCKED;
		hammer_io_deallocate(bp);
		/* structure may be dead now */
	}
	lwkt_reltoken(&hmp->io_token);
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
 *
 * bioops callback - hold io_token
 */
static void
hammer_io_deallocate(struct buf *bp)
{
	hammer_io_structure_t iou = (void *)LIST_FIRST(&bp->b_dep);
	hammer_mount_t hmp;

	hmp = iou->io.hmp;

	lwkt_gettoken(&hmp->io_token);

	KKASSERT((bp->b_flags & B_LOCKED) == 0 && iou->io.running == 0);
	if (hammer_try_interlock_norefs(&iou->io.lock) == 0) {
		/*
		 * We cannot safely disassociate a bp from a referenced
		 * or interlocked HAMMER structure.
		 */
		bp->b_flags |= B_LOCKED;
		atomic_add_int(&hammer_count_io_locked, 1);
	} else if (iou->io.modified) {
		/*
		 * It is not legal to disassociate a modified buffer.  This
		 * case really shouldn't ever occur.
		 */
		bp->b_flags |= B_LOCKED;
		atomic_add_int(&hammer_count_io_locked, 1);
		hammer_put_interlock(&iou->io.lock, 0);
	} else {
		/*
		 * Disassociate the BP.  If the io has no refs left we
		 * have to add it to the loose list.  The kernel has
		 * locked the buffer and therefore our io must be
		 * in a released state.
		 */
		hammer_io_disassociate(iou);
		if (iou->io.type != HAMMER_STRUCTURE_VOLUME) {
			KKASSERT(iou->io.bp == NULL);
			KKASSERT(iou->io.mod_root == NULL);
			iou->io.mod_root = &hmp->lose_root;
			if (RB_INSERT(hammer_mod_rb_tree, iou->io.mod_root,
				      &iou->io)) {
				panic("hammer_io_deallocate: duplicate entry");
			}
		}
		hammer_put_interlock(&iou->io.lock, 1);
	}
	lwkt_reltoken(&hmp->io_token);
}

/*
 * bioops callback - hold io_token
 */
static int
hammer_io_fsync(struct vnode *vp)
{
	/* nothing to do, so io_token not needed */
	return(0);
}

/*
 * NOTE: will not be called unless we tell the kernel about the
 * bioops.  Unused... we use the mount's VFS_SYNC instead.
 *
 * bioops callback - hold io_token
 */
static int
hammer_io_sync(struct mount *mp)
{
	/* nothing to do, so io_token not needed */
	return(0);
}

/*
 * bioops callback - hold io_token
 */
static void
hammer_io_movedeps(struct buf *bp1, struct buf *bp2)
{
	/* nothing to do, so io_token not needed */
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
 *
 * bioops callback - hold io_token
 */
static int
hammer_io_checkread(struct buf *bp)
{
	/* nothing to do, so io_token not needed */
	return(0);
}

/*
 * The kernel is asking us whether it can write out a dirty buffer or not.
 *
 * bioops callback - hold io_token
 */
static int
hammer_io_checkwrite(struct buf *bp)
{
	hammer_io_t io = (void *)LIST_FIRST(&bp->b_dep);
	hammer_mount_t hmp = io->hmp;

	/*
	 * This shouldn't happen under normal operation.
	 */
	lwkt_gettoken(&hmp->io_token);
	if (io->type == HAMMER_STRUCTURE_VOLUME ||
	    io->type == HAMMER_STRUCTURE_META_BUFFER) {
		if (!panicstr)
			panic("hammer_io_checkwrite: illegal buffer");
		if ((bp->b_flags & B_LOCKED) == 0) {
			bp->b_flags |= B_LOCKED;
			atomic_add_int(&hammer_count_io_locked, 1);
		}
		lwkt_reltoken(&hmp->io_token);
		return(1);
	}

	/*
	 * We have to be able to interlock the IO to safely modify any
	 * of its fields without holding the fs_token.  If we can't lock
	 * it then we are racing someone.
	 *
	 * Our ownership of the bp lock prevents the io from being ripped
	 * out from under us.
	 */
	if (hammer_try_interlock_norefs(&io->lock) == 0) {
		bp->b_flags |= B_LOCKED;
		atomic_add_int(&hammer_count_io_locked, 1);
		lwkt_reltoken(&hmp->io_token);
		return(1);
	}

	/*
	 * The modified bit must be cleared prior to the initiation of
	 * any IO (returning 0 initiates the IO).  Because this is a
	 * normal data buffer hammer_io_clear_modify() runs through a
	 * simple degenerate case.
	 *
	 * Return 0 will cause the kernel to initiate the IO, and we
	 * must normally clear the modified bit before we begin.  If
	 * the io has modify_refs we do not clear the modified bit,
	 * otherwise we may miss changes.
	 *
	 * Only data and undo buffers can reach here.  These buffers do
	 * not have terminal crc functions but we temporarily reference
	 * the IO anyway, just in case.
	 */
	if (io->modify_refs == 0 && io->modified) {
		hammer_ref(&io->lock);
		hammer_io_clear_modify(io, 0);
		hammer_rel(&io->lock);
	} else if (io->modified) {
		KKASSERT(io->type == HAMMER_STRUCTURE_DATA_BUFFER);
	}

	/*
	 * The kernel is going to start the IO, set io->running.
	 */
	KKASSERT(io->running == 0);
	io->running = 1;
	atomic_add_long(&io->hmp->io_running_space, io->bytes);
	atomic_add_long(&hammer_count_io_running_write, io->bytes);
	TAILQ_INSERT_TAIL(&io->hmp->iorun_list, io, iorun_entry);

	hammer_put_interlock(&io->lock, 1);
	lwkt_reltoken(&hmp->io_token);

	return(0);
}

/*
 * Return non-zero if we wish to delay the kernel's attempt to flush
 * this buffer to disk.
 *
 * bioops callback - hold io_token
 */
static int
hammer_io_countdeps(struct buf *bp, int n)
{
	/* nothing to do, so io_token not needed */
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
		hammer_stats_disk_read += bp->b_bufsize;
		vn_strategy(volume->devvp, nbio);
	}
	hammer_rel_volume(volume, 0);
done:
	if (error) {
		kprintf("hammer_direct_read: failed @ %016llx\n",
			(long long)zone2_offset);
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	}
	return(error);
}

/*
 * This works similarly to hammer_io_direct_read() except instead of
 * directly reading from the device into the bio we instead indirectly
 * read through the device's buffer cache and then copy the data into
 * the bio.
 *
 * If leaf is non-NULL and validation is enabled, the CRC will be checked.
 *
 * This routine also executes asynchronously.  It allows hammer strategy
 * calls to operate asynchronously when in double_buffer mode (in addition
 * to operating asynchronously when in normal mode).
 */
int
hammer_io_indirect_read(hammer_mount_t hmp, struct bio *bio,
			hammer_btree_leaf_elm_t leaf)
{
	hammer_off_t buf_offset;
	hammer_off_t zone2_offset;
	hammer_volume_t volume;
	struct buf *bp;
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
		 * Convert to the raw volume->devvp offset and acquire
		 * the buf, issuing async I/O if necessary.
		 */
		buf_offset = volume->ondisk->vol_buf_beg +
			     (zone2_offset & HAMMER_OFF_SHORT_MASK);

		if (leaf && hammer_verify_data) {
			bio->bio_caller_info1.uvalue32 = leaf->data_crc;
			bio->bio_caller_info2.index = 1;
		} else {
			bio->bio_caller_info2.index = 0;
		}
		breadcb(volume->devvp, buf_offset, bp->b_bufsize,
			hammer_indirect_callback, bio);
	}
	hammer_rel_volume(volume, 0);
done:
	if (error) {
		kprintf("hammer_direct_read: failed @ %016llx\n",
			(long long)zone2_offset);
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bio);
	}
	return(error);
}

/*
 * Indirect callback on completion.  bio/bp specify the device-backed
 * buffer.  bio->bio_caller_info1.ptr holds obio.
 *
 * obio/obp is the original regular file buffer.  obio->bio_caller_info*
 * contains the crc specification.
 *
 * We are responsible for calling bpdone() and bqrelse() on bio/bp, and
 * for calling biodone() on obio.
 */
static void
hammer_indirect_callback(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct buf *obp;
	struct bio *obio;

	/*
	 * If BIO_DONE is already set the device buffer was already
	 * fully valid (B_CACHE).  If it is not set then I/O was issued
	 * and we have to run I/O completion as the last bio.
	 *
	 * Nobody is waiting for our device I/O to complete, we are
	 * responsible for bqrelse()ing it which means we also have to do
	 * the equivalent of biowait() and clear BIO_DONE (which breadcb()
	 * may have set).
	 *
	 * Any preexisting device buffer should match the requested size,
	 * but due to bigblock recycling and other factors there is some
	 * fragility there, so we assert that the device buffer covers
	 * the request.
	 */
	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(bp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);

	obio = bio->bio_caller_info1.ptr;
	obp = obio->bio_buf;

	if (bp->b_flags & B_ERROR) {
		obp->b_flags |= B_ERROR;
		obp->b_error = bp->b_error;
	} else if (obio->bio_caller_info2.index &&
		   obio->bio_caller_info1.uvalue32 !=
		    crc32(bp->b_data, bp->b_bufsize)) {
		obp->b_flags |= B_ERROR;
		obp->b_error = EIO;
	} else {
		KKASSERT(bp->b_bufsize >= obp->b_bufsize);
		bcopy(bp->b_data, obp->b_data, obp->b_bufsize);
		obp->b_resid = 0;
		obp->b_flags |= B_AGE;
	}
	biodone(obio);
	bqrelse(bp);
}

/*
 * Write a buffer associated with a front-end vnode directly to the
 * disk media.  The bio may be issued asynchronously.
 *
 * The BIO is associated with the specified record and RECG_DIRECT_IO
 * is set.  The recorded is added to its object.
 */
int
hammer_io_direct_write(hammer_mount_t hmp, struct bio *bio,
		       hammer_record_t record)
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

	/*
	 * Issue or execute the I/O.  The new memory record must replace
	 * the old one before the I/O completes, otherwise a reaquisition of
	 * the buffer will load the old media data instead of the new.
	 */
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
			record->gflags |= HAMMER_RECG_DIRECT_IO |
					 HAMMER_RECG_DIRECT_INVAL;

			/*
			 * Third level bio - raw offset specific to the
			 * correct volume.
			 */
			zone2_offset &= HAMMER_OFF_SHORT_MASK;
			nbio = push_bio(nbio);
			nbio->bio_offset = volume->ondisk->vol_buf_beg +
					   zone2_offset;
			hammer_stats_disk_write += bp->b_bufsize;
			hammer_ip_replace_bulk(hmp, record);
			vn_strategy(volume->devvp, nbio);
			hammer_io_flush_mark(volume);
		}
		hammer_rel_volume(volume, 0);
	} else {
		/* 
		 * Must fit in a standard HAMMER buffer.  In this case all
		 * consumers use the HAMMER buffer system and RECG_DIRECT_IO
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
			hammer_ip_replace_bulk(hmp, record);
			biodone(bio);
		}
	}
	if (error) {
		/*
		 * Major suckage occured.  Also note:  The record was
		 * never added to the tree so we do not have to worry
		 * about the backend.
		 */
		kprintf("hammer_direct_write: failed @ %016llx\n",
			(long long)leaf->data_offset);
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
 *
 * NOTE: MPSAFE callback, only modify fields we have explicit
 *	 access to (the bp and the record->gflags).
 */
static
void
hammer_io_direct_write_complete(struct bio *nbio)
{
	struct bio *obio;
	struct buf *bp;
	hammer_record_t record;
	hammer_mount_t hmp;

	record = nbio->bio_caller_info1.ptr;
	KKASSERT(record != NULL);
	hmp = record->ip->hmp;

	lwkt_gettoken(&hmp->io_token);

	bp = nbio->bio_buf;
	obio = pop_bio(nbio);
	if (bp->b_flags & B_ERROR) {
		lwkt_gettoken(&hmp->fs_token);
		hammer_critical_error(hmp, record->ip,
				      bp->b_error,
				      "while writing bulk data");
		lwkt_reltoken(&hmp->fs_token);
		bp->b_flags |= B_INVAL;
	}
	biodone(obio);

	KKASSERT(record->gflags & HAMMER_RECG_DIRECT_IO);
	if (record->gflags & HAMMER_RECG_DIRECT_WAIT) {
		record->gflags &= ~(HAMMER_RECG_DIRECT_IO |
				    HAMMER_RECG_DIRECT_WAIT);
		/* record can disappear once DIRECT_IO flag is cleared */
		wakeup(&record->flags);
	} else {
		record->gflags &= ~HAMMER_RECG_DIRECT_IO;
		/* record can disappear once DIRECT_IO flag is cleared */
	}
	lwkt_reltoken(&hmp->io_token);
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
	hammer_mount_t hmp = record->ip->hmp;

	/*
	 * Wait for I/O to complete
	 */
	if (record->gflags & HAMMER_RECG_DIRECT_IO) {
		lwkt_gettoken(&hmp->io_token);
		while (record->gflags & HAMMER_RECG_DIRECT_IO) {
			record->gflags |= HAMMER_RECG_DIRECT_WAIT;
			tsleep(&record->flags, 0, "hmdiow", 0);
		}
		lwkt_reltoken(&hmp->io_token);
	}

	/*
	 * Invalidate any related buffer cache aliases associated with the
	 * backing device.  This is needed because the buffer cache buffer
	 * for file data is associated with the file vnode, not the backing
	 * device vnode.
	 *
	 * XXX I do not think this case can occur any more now that
	 * reservations ensure that all such buffers are removed before
	 * an area can be reused.
	 */
	if (record->gflags & HAMMER_RECG_DIRECT_INVAL) {
		KKASSERT(record->leaf.data_offset);
		hammer_del_buffers(hmp, record->leaf.data_offset,
				   record->zone2_offset, record->leaf.data_len,
				   1);
		record->gflags &= ~HAMMER_RECG_DIRECT_INVAL;
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
	hammer_off_t file_offset;
	struct vnode *vp;
	struct buf *bp;
	int blksize;

	if (ip->vp == NULL)
		return(0);
	file_offset = iinfo->u.leaf->base.key - iinfo->u.leaf->data_len;
	blksize = iinfo->u.leaf->data_len;
	KKASSERT((blksize & HAMMER_BUFMASK) == 0);

	/*
	 * Warning: FINDBLK_TEST return stable storage but not stable
	 *	    contents.  It happens to be ok in this case.
	 */
	hammer_ref(&ip->lock);
	if (hammer_get_vnode(ip, &vp) == 0) {
		if ((bp = findblk(ip->vp, file_offset, FINDBLK_TEST)) != NULL &&
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
	atomic_set_int(&volume->vol_flags, HAMMER_VOLF_NEEDFLUSH);
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
			atomic_clear_int(&volume->vol_flags,
					 HAMMER_VOLF_NEEDFLUSH);
			bp = getpbuf(NULL);
			bp->b_bio1.bio_offset = 0;
			bp->b_bufsize = 0;
			bp->b_bcount = 0;
			bp->b_cmd = BUF_CMD_FLUSH;
			bp->b_bio1.bio_caller_info1.cluster_head = bp_base;
			bp->b_bio1.bio_done = biodone_sync;
			bp->b_bio1.bio_flags |= BIO_SYNC;
			bp_base = bp;
			vn_strategy(volume->devvp, &bp->b_bio1);
		}
	}
	while ((bp = bp_base) != NULL) {
		bp_base = bp->b_bio1.bio_caller_info1.cluster_head;
		biowait(&bp->b_bio1, "hmrFLS");
		relpbuf(bp, NULL);
	}
}

/*
 * Limit the amount of backlog which we allow to build up
 */
void
hammer_io_limit_backlog(hammer_mount_t hmp)
{
	waitrunningbufspace();
}
