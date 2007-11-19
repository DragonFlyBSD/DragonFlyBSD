/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_io.c,v 1.2 2007/11/19 00:53:40 dillon Exp $
 */
/*
 * IO Primitives and buffer cache management
 *
 * All major data-tracking structures in HAMMER contain a struct hammer_io
 * which is used to manage their backing store.  We use filesystem buffers
 * for backing store and we leave them passively associated with their
 * HAMMER structures.
 *
 * If the kernel tries to release a passively associated buf which we cannot
 * yet let go we set B_LOCKED in the buffer and then actively released it
 * later when we can.
 */

#include "hammer.h"
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/buf.h>
#include <sys/buf2.h>

/*
 * Helper routine which disassociates a buffer cache buf from a
 * hammer structure.
 *
 * If the io structures indicates that the buffer is not in a released
 * state we must dispose of it.
 */
static void
hammer_io_disassociate(union hammer_io_structure *io)
{
	struct buf *bp = io->io.bp;
	int modified;
	int released;

	LIST_INIT(&bp->b_dep);	/* clear the association */
	io->io.bp = NULL;
	modified = io->io.modified;
	released = io->io.released;

	switch(io->io.type) {
	case HAMMER_STRUCTURE_VOLUME:
		io->volume.ondisk = NULL;
		io->volume.alist.meta = NULL;
		io->io.modified = 0;
		break;
	case HAMMER_STRUCTURE_SUPERCL:
		io->supercl.ondisk = NULL;
		io->supercl.alist.meta = NULL;
		io->io.modified = 0;
		break;
	case HAMMER_STRUCTURE_CLUSTER:
		io->cluster.ondisk = NULL;
		io->cluster.alist_master.meta = NULL;
		io->cluster.alist_btree.meta = NULL;
		io->cluster.alist_record.meta = NULL;
		io->cluster.alist_mdata.meta = NULL;
		io->io.modified = 0;
		break;
	case HAMMER_STRUCTURE_BUFFER:
		io->buffer.ondisk = NULL;
		io->buffer.alist.meta = NULL;
		io->io.modified = 0;
		break;
	}
	/*
	 * 'io' now invalid.  If the buffer was not released we have to
	 * dispose of it.
	 *
	 * disassociate can be called via hammer_io_checkwrite() with
	 * the buffer in a released state (possibly with no lock held
	 * at all, in fact).  Don't mess with it if we are in a released
	 * state.
	 */
	if (released == 0) {
		if (modified)
			bdwrite(bp);
		else
			bqrelse(bp);
	}
}

/*
 * Load bp for a HAMMER structure.
 */
int
hammer_io_read(struct vnode *devvp, struct hammer_io *io)
{
	struct buf *bp;
	int error;

	if ((bp = io->bp) == NULL) {
		error = bread(devvp, io->offset, HAMMER_BUFSIZE, &io->bp);
		if (error == 0) {
			bp = io->bp;
			bp->b_ops = &hammer_bioops;
			LIST_INSERT_HEAD(&bp->b_dep, &io->worklist, node);
			BUF_KERNPROC(bp);
		}
		io->modified = 0;	/* no new modifications yet */
		io->released = 0;	/* we hold an active lock on bp */
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Similar to hammer_io_read() but returns a zero'd out buffer instead.
 * vfs_bio_clrbuf() is kinda nasty, enforce serialization against background
 * I/O so we can call it.
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
		io->released = 0;	/* we hold an active lock on bp */
		BUF_KERNPROC(bp);
	} else {
		if (io->released) {
			regetblk(bp);
			io->released = 0;
			BUF_KERNPROC(bp);
		}
	}
	io->modified = 1;
	vfs_bio_clrbuf(bp);
	return(0);
}

/*
 * Release the IO buffer on the last reference to a hammer structure.  At
 * this time the lock still has a reference.
 *
 * We flush and disassociate the bp if flush is non-zero or if the kernel
 * tried to free/reuse the buffer.
 */
void
hammer_io_release(struct hammer_io *io, int flush)
{
	union hammer_io_structure *iou = (void *)io;
	struct buf *bp;

	if ((bp = io->bp) != NULL) {
		if (bp->b_flags & B_LOCKED) {
			/*
			 * The kernel wanted the buffer but couldn't get it,
			 * give it up now.
			 */
			KKASSERT(io->released);
			regetblk(bp);
			io->released = 0;
			BUF_KERNPROC(bp);
			bp->b_flags &= ~B_LOCKED;
			hammer_io_disassociate(iou);
		} else if (io->released == 0) {
			/*
			 * We are holding a real lock on the buffer, release
			 * it passively (hammer_io_deallocate is called
			 * when the kernel really wants to reuse the buffer).
			 */
			if (flush) {
				hammer_io_disassociate(iou);
			} else {
				if (io->modified)
					bdwrite(bp);
				else
					bqrelse(bp);
				io->modified = 0;
				io->released = 1;
			}
		} else if (io->modified && (bp->b_flags & B_DELWRI) == 0) {
			/*
			 * We are holding the buffer passively but made
			 * modifications to it.  The kernel has not initiated
			 * I/O (else B_LOCKED would have been set), so just
			 * check whether B_DELWRI is set.  Since we still
			 * have lock references on the HAMMER structure the
			 * kernel cannot throw the buffer away.
			 *
			 * We have to do this to avoid the situation where
			 * the buffer is not marked B_DELWRI when modified
			 * and in a released state, otherwise the kernel
			 * will never try to flush the modified buffer!
			 */
			regetblk(bp);
			BUF_KERNPROC(bp);
			io->released = 0;
			if (flush) {
				hammer_io_disassociate(iou);
			} else {
				bdwrite(bp);
				io->modified = 0;
			}
		} else if (flush) {
			/*
			 * We are holding the buffer passively but were
			 * asked to disassociate and flush it.
			 */
			regetblk(bp);
			BUF_KERNPROC(bp);
			io->released = 0;
			hammer_io_disassociate(iou);
			/* io->released ignored */
		} /* else just leave it associated in a released state */
	}
}

/*
 * HAMMER_BIOOPS
 */

/*
 * Pre and post I/O callbacks.  No buffer munging is done so there is
 * nothing to do here.
 */
static void hammer_io_deallocate(struct buf *bp);

static void
hammer_io_start(struct buf *bp)
{
}

static void
hammer_io_complete(struct buf *bp)
{
}

/*
 * Callback from kernel when it wishes to deallocate a passively
 * associated structure.  This can only occur if the buffer is
 * passively associated with the structure.  The kernel has locked
 * the buffer.
 *
 * If we cannot disassociate we set B_LOCKED to prevent the buffer
 * from getting reused.
 */
static void
hammer_io_deallocate(struct buf *bp)
{
	union hammer_io_structure *io = (void *)LIST_FIRST(&bp->b_dep);

	/* XXX memory interlock, spinlock to sync cpus */

	KKASSERT(io->io.released);
	crit_enter();

	/*
	 * First, ref the structure to prevent either the buffer or the
	 * structure from going away.
	 */
	hammer_ref(&io->io.lock);

	/*
	 * Buffers can have active references from cached hammer_node's,
	 * even if those nodes are themselves passively cached.  Attempt
	 * to clean them out.  This may not succeed.
	 */
	if (io->io.type == HAMMER_STRUCTURE_BUFFER &&
	    hammer_lock_ex_try(&io->io.lock) == 0) {
		hammer_flush_buffer_nodes(&io->buffer);
		hammer_unlock(&io->io.lock);
	}

	if (hammer_islastref(&io->io.lock)) {
		/*
		 * If we are the only ref left we can disassociate the
		 * I/O.  It had better still be in a released state because
		 * the kernel is holding a lock on the buffer.
		 */
		KKASSERT(io->io.released);
		hammer_io_disassociate(io);
		bp->b_flags &= ~B_LOCKED;

		switch(io->io.type) {
		case HAMMER_STRUCTURE_VOLUME:
			hammer_rel_volume(&io->volume, 1);
			break;
		case HAMMER_STRUCTURE_SUPERCL:
			hammer_rel_supercl(&io->supercl, 1);
			break;
		case HAMMER_STRUCTURE_CLUSTER:
			hammer_rel_cluster(&io->cluster, 1);
			break;
		case HAMMER_STRUCTURE_BUFFER:
			hammer_rel_buffer(&io->buffer, 1);
			break;
		}
		/* NOTE: io may be invalid (kfree'd) here */
	} else {
		/*
		 * Otherwise tell the kernel not to destroy the buffer
		 */
		bp->b_flags |= B_LOCKED;
		hammer_unlock(&io->io.lock);
	}
	crit_exit();
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
 * Writing is a different case.  We don't want to write out a buffer
 * that HAMMER may be modifying passively.
 */
static int
hammer_io_checkread(struct buf *bp)
{
	return(0);
}

static int
hammer_io_checkwrite(struct buf *bp)
{
	union hammer_io_structure *io = (void *)LIST_FIRST(&bp->b_dep);

	if (io->io.lock.refs) {
		bp->b_flags |= B_LOCKED;
		return(-1);
	} else {
		KKASSERT(bp->b_flags & B_DELWRI);
		hammer_io_disassociate(io);
		return(0);
	}
}

/*
 * Return non-zero if the caller should flush the structure associated
 * with this io sub-structure.
 */
int
hammer_io_checkflush(struct hammer_io *io)
{
	if (io->bp == NULL || (io->bp->b_flags & B_LOCKED))
		return(1);
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

