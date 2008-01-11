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
 * $DragonFly: src/sys/vfs/hammer/hammer_io.c,v 1.16 2008/01/11 05:45:19 dillon Exp $
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

static void hammer_io_deallocate(struct buf *bp);
static int hammer_io_checkwrite(struct buf *bp);

/*
 * Initialize an already-zero'd hammer_io structure
 */
void
hammer_io_init(hammer_io_t io, enum hammer_io_type type)
{
	io->type = type;
	TAILQ_INIT(&io->deplist);
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

	KKASSERT(TAILQ_EMPTY(&iou->io.deplist) && iou->io.modified == 0);
	buf_dep_init(bp);
	iou->io.bp = NULL;
	bp->b_flags &= ~B_LOCKED;
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
		iou->volume.alist.meta = NULL;
		break;
	case HAMMER_STRUCTURE_SUPERCL:
		iou->supercl.ondisk = NULL;
		iou->supercl.alist.meta = NULL;
		break;
	case HAMMER_STRUCTURE_CLUSTER:
		iou->cluster.ondisk = NULL;
		iou->cluster.alist_master.meta = NULL;
		iou->cluster.alist_btree.meta = NULL;
		iou->cluster.alist_record.meta = NULL;
		iou->cluster.alist_mdata.meta = NULL;
		break;
	case HAMMER_STRUCTURE_BUFFER:
		iou->buffer.ondisk = NULL;
		iou->buffer.alist.meta = NULL;
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

void
hammer_io_waitdep(hammer_io_t io)
{
	while (TAILQ_FIRST(&io->deplist)) {
		kprintf("waitdep %p\n", io);
		tsleep(io, 0, "hmrdep", hz);
	}
}

/*
 * Load bp for a HAMMER structure.  The io is exclusively locked by the
 * caller.
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
		io->running = 0;
		io->waiting = 0;
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Similar to hammer_io_read() but returns a zero'd out buffer instead.
 * vfs_bio_clrbuf() is kinda nasty, enforce serialization against background
 * I/O so we can call it.
 *
 * The caller is responsible for calling hammer_modify_*() on the appropriate
 * HAMMER structure.
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
		io->modified = 0;
		io->released = 0;
		io->running = 0;
		io->waiting = 0;
		BUF_KERNPROC(bp);
	} else {
		if (io->released) {
			regetblk(bp);
			BUF_KERNPROC(bp);
			io->released = 0;
		}
	}
	vfs_bio_clrbuf(bp);
	return(0);
}

/*
 * This routine is called on the last reference to a hammer structure.
 * The io is usually locked exclusively (but may not be during unmount).
 *
 * If flush is 1, or B_LOCKED was set indicating that the kernel
 * wanted to recycle the buffer, and there are no dependancies, this
 * function will issue an asynchronous write.
 *
 * If flush is 2 this function waits until all I/O has completed and
 * disassociates the bp from the IO before returning, unless there
 * are still other references.
 */
void
hammer_io_release(struct hammer_io *io, int flush)
{
	struct buf *bp;

	if ((bp = io->bp) == NULL)
		return;

#if 0
	/*
	 * If flush is 2 wait for dependancies
	 */
	while (flush == 2 && TAILQ_FIRST(&io->deplist)) {
		hammer_io_wait(TAILQ_FIRST(&io->deplist));
	}
#endif

	/*
	 * Try to flush a dirty IO to disk if asked to by the caller
	 * or if the kernel tried to flush the buffer in the past.
	 *
	 * The flush will fail if any dependancies are present.
	 */
	if (io->modified && (flush || bp->b_flags & B_LOCKED))
		hammer_io_flush(io);

	/*
	 * If flush is 2 we wait for the IO to complete.
	 */
	if (flush == 2 && io->running) {
		hammer_io_wait(io);
	}

	/*
	 * Actively or passively release the buffer.  Modified IOs with
	 * dependancies cannot be released.
	 */
	if (flush && io->modified == 0 && io->running == 0) {
		KKASSERT(TAILQ_EMPTY(&io->deplist));
		if (io->released) {
			regetblk(bp);
			io->released = 0;
		}
		hammer_io_disassociate((hammer_io_structure_t)io, 1);
	} else if (io->modified) {
		if (io->released == 0 && TAILQ_EMPTY(&io->deplist)) {
			io->released = 1;
			bdwrite(bp);
		}
	} else if (io->released == 0) {
		io->released = 1;
		bqrelse(bp);
	}
}

/*
 * This routine is called with a locked IO when a flush is desired.
 */
void
hammer_io_flush(struct hammer_io *io)
{
	struct buf *bp;

	/*
	 * Can't flush if the IO isn't modified or if it has dependancies.
	 */
	if (io->modified == 0)
		return;
	if (TAILQ_FIRST(&io->deplist))
		return;

	KKASSERT(io->bp);

	bp = io->bp;

	/*
	 * If we are trying to flush a buffer we have to wait until the
	 * cluster header for the mark-OPEN has completed its I/O.
	 */
	if (io->type == HAMMER_STRUCTURE_BUFFER) {
		hammer_io_structure_t iou = (void *)io;
		hammer_cluster_t cluster = iou->buffer.cluster;

		if (cluster->io.running) {
			kprintf("WAIT CLUSTER OPEN %d\n", cluster->clu_no);
			hammer_io_wait(&cluster->io);
			kprintf("WAIT CLUSTER OPEN OK\n");
		}
	}
	if (io->type == HAMMER_STRUCTURE_CLUSTER) {
		/*
		 * Mark the cluster closed if we can
		 */
		hammer_io_checkwrite(io->bp);
	}
	if (io->released) {
		regetblk(bp);
		/* BUF_KERNPROC(io->bp); */
		io->released = 0;
	}
	io->released = 1;
	io->running = 1;
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
 * Ensure that the bp is acquired and return non-zero on a 0->1 transition
 * of the modified bit.
 */
static __inline
int
hammer_io_modify(hammer_io_t io, struct hammer_io_list *list)
{
	int r = 0;

	KKASSERT(io->lock.refs != 0 && io->bp != NULL);
	if (io->modified == 0) {
		hammer_lock_ex(&io->lock);
		if (io->modified == 0) {
			if (io->released) {
				regetblk(io->bp);
				BUF_KERNPROC(io->bp);
				io->released = 0;
			}
			io->modified = 1;
			io->entry_list = list;
			if (list)
				TAILQ_INSERT_TAIL(list, io, entry);
			r = 1;
		}
		hammer_unlock(&io->lock);
	} else if (io->released) {
		/*
		 * Make sure no IO is occuring while we modify the contents
		 * of the buffer. XXX should be able to avoid doing this.
		 */
		hammer_lock_ex(&io->lock);
		if (io->released) {
			regetblk(io->bp);
			BUF_KERNPROC(io->bp);
			io->released = 0;
		}
		hammer_unlock(&io->lock);
	}
	return(r);
}

void
hammer_modify_volume(hammer_volume_t volume)
{
	hammer_io_modify(&volume->io, NULL);
}

void
hammer_modify_supercl(hammer_supercl_t supercl)
{
	hammer_io_modify(&supercl->io, &supercl->volume->io.deplist);
}

/*
 * Caller intends to modify a cluster's ondisk structure. 
 */
void
hammer_modify_cluster(hammer_cluster_t cluster)
{
	hammer_io_modify(&cluster->io, &cluster->volume->io.deplist);
}

/*
 * Caller intends to modify a buffer's ondisk structure.  The related
 * cluster must be marked open prior to being able to flush the modified
 * buffer so get that I/O going now.
 */
void
hammer_modify_buffer(hammer_buffer_t buffer)
{
	hammer_cluster_t cluster = buffer->cluster;

	if (hammer_io_modify(&buffer->io, &cluster->io.deplist)) {
		hammer_modify_cluster(cluster);
		if ((cluster->ondisk->clu_flags & HAMMER_CLUF_OPEN) == 0) {
			hammer_lock_ex(&cluster->io.lock);
			if ((cluster->ondisk->clu_flags & HAMMER_CLUF_OPEN) == 0) {
				KKASSERT(cluster->io.released == 0);
				cluster->ondisk->clu_flags |= HAMMER_CLUF_OPEN;
				cluster->io.released = 1;
				cluster->io.running = 1;
				bawrite(cluster->io.bp);
				kprintf("OPEN CLUSTER %d:%d\n",
					cluster->volume->vol_no,
					cluster->clu_no);
			}
			hammer_unlock(&cluster->io.lock);
		}
	}
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
	if ((bp = io->bp) != NULL) {
		if (io->released) {
			regetblk(bp);
			/* BUF_KERNPROC(io->bp); */
		} else {
			io->released = 1;
		}
		if (io->modified == 0) {
			kprintf("hammer_io_clear_modify: cleared %p\n", io);
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
 */
static void
hammer_io_complete(struct buf *bp)
{
	union hammer_io_structure *iou = (void *)LIST_FIRST(&bp->b_dep);

	KKASSERT(iou->io.released == 1);

	if (iou->io.modified == 0)
		return;

	/*
	 * If we were writing the cluster header out and CLUF_OPEN is set,
	 * do NOT clear the modify bit.  Just clear the IO running bit
	 * and do a wakeup.
	 */
	if (iou->io.type == HAMMER_STRUCTURE_CLUSTER) {
		if (iou->cluster.ondisk->clu_flags & HAMMER_CLUF_OPEN) {
			iou->io.running = 0;
			if (iou->io.waiting) {
				iou->io.waiting = 0;
				wakeup(iou);
			}
			return;
		}
	}
	 

	/*
	 * If this was a write then clear the modified status and remove us
	 * from the dependancy list.
	 *
	 * If no lock references remain and we can acquire the IO lock and
	 * someone at some point wanted us to flush (B_LOCKED test), then
	 * try to dispose of the IO.
	 */
	iou->io.modified = 0;
	if (iou->io.entry_list) {
		TAILQ_REMOVE(iou->io.entry_list, &iou->io, entry);
		iou->io.entry_list = NULL;
	}
	iou->io.running = 0;
	if (iou->io.waiting) {
		iou->io.waiting = 0;
		wakeup(iou);
	}

	/*
	 * Someone wanted us to flush, try to clean out the buffer. 
	 */
	if ((bp->b_flags & B_LOCKED) && iou->io.lock.refs == 0) {
		hammer_io_deallocate(bp);
		/* structure may be dead now */
	}
}

/*
 * Callback from kernel when it wishes to deallocate a passively
 * associated structure.  This case can only occur with read-only
 * bp's.
 *
 * If we cannot disassociate we set B_LOCKED to prevent the buffer
 * from getting reused.
 */
static void
hammer_io_deallocate(struct buf *bp)
{
	hammer_io_structure_t iou = (void *)LIST_FIRST(&bp->b_dep);

	KKASSERT((bp->b_flags & B_LOCKED) == 0 && iou->io.running == 0);
	if (iou->io.modified) {
		bp->b_flags |= B_LOCKED;
		return;
	}
	hammer_ref(&iou->io.lock);
	if (iou->io.lock.refs > 1 || iou->io.modified) {
		hammer_unref(&iou->io.lock);
		bp->b_flags |= B_LOCKED;
	} else {
		hammer_io_disassociate(iou, 0);

		switch(iou->io.type) {
		case HAMMER_STRUCTURE_VOLUME:
			hammer_rel_volume(&iou->volume, 1);
			break;
		case HAMMER_STRUCTURE_SUPERCL:
			hammer_rel_supercl(&iou->supercl, 1);
			break;
		case HAMMER_STRUCTURE_CLUSTER:
			hammer_rel_cluster(&iou->cluster, 1);
			break;
		case HAMMER_STRUCTURE_BUFFER:
			hammer_rel_buffer(&iou->buffer, 1);
			break;
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
 * dependancy.
 *
 * This code enforces the following write ordering: buffers, then cluster
 * headers, then volume headers.
 */
static int
hammer_io_checkread(struct buf *bp)
{
	return(0);
}

static int
hammer_io_checkwrite(struct buf *bp)
{
	union hammer_io_structure *iou = (void *)LIST_FIRST(&bp->b_dep);

	/*
	 * A modified cluster with no dependancies can be closed.
	 */
	if (iou->io.type == HAMMER_STRUCTURE_CLUSTER && iou->io.modified) {
		hammer_cluster_t cluster = &iou->cluster;

		if (TAILQ_EMPTY(&cluster->io.deplist)) {
			cluster->ondisk->clu_flags &= ~HAMMER_CLUF_OPEN;
			kprintf("CLOSE CLUSTER %d:%d\n",
				cluster->volume->vol_no,
				cluster->clu_no);
		}
	}
	return(0);
}

/*
 * Return non-zero if the caller should flush the structure associated
 * with this io sub-structure.
 */
int
hammer_io_checkflush(struct hammer_io *io)
{
	if (io->bp == NULL || (io->bp->b_flags & B_LOCKED)) {
		return(1);
	}
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

