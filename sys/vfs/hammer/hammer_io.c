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
 * $DragonFly: src/sys/vfs/hammer/hammer_io.c,v 1.12 2008/01/03 06:48:49 dillon Exp $
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
 * Helper routine to disassociate a buffer cache buffer from an I/O
 * structure.
 */
static void
hammer_io_disassociate(union hammer_io_structure *io)
{
	struct buf *bp = io->io.bp;

	KKASSERT(io->io.released && io->io.modified == 0);
	LIST_INIT(&bp->b_dep);	/* clear the association */
	bp->b_ops = NULL;
	io->io.bp = NULL;
	bp->b_flags &= ~B_LOCKED;

	switch(io->io.type) {
	case HAMMER_STRUCTURE_VOLUME:
		io->volume.ondisk = NULL;
		io->volume.alist.meta = NULL;
		break;
	case HAMMER_STRUCTURE_SUPERCL:
		io->supercl.ondisk = NULL;
		io->supercl.alist.meta = NULL;
		break;
	case HAMMER_STRUCTURE_CLUSTER:
		io->cluster.ondisk = NULL;
		io->cluster.alist_master.meta = NULL;
		io->cluster.alist_btree.meta = NULL;
		io->cluster.alist_record.meta = NULL;
		io->cluster.alist_mdata.meta = NULL;
		break;
	case HAMMER_STRUCTURE_BUFFER:
		io->buffer.ondisk = NULL;
		io->buffer.alist.meta = NULL;
		break;
	}
}

/*
 * Mark a cluster as being closed.  This is done as late as possible,
 * only when we are asked to flush the cluster
 */
static void
hammer_close_cluster(hammer_cluster_t cluster)
{
	while (cluster->state == HAMMER_CLUSTER_ASYNC)
		tsleep(cluster, 0, "hmrdep", 0);
	if (cluster->state == HAMMER_CLUSTER_OPEN) {
		cluster->state = HAMMER_CLUSTER_IDLE;
		hammer_modify_cluster(cluster);
		cluster->ondisk->clu_flags &= ~HAMMER_CLUF_OPEN;
		hammer_modify_cluster_done(cluster);
		kprintf("CLOSE CLUSTER\n");
	}
}

/*
 * Hack XXX - called from kernel syncer via hammer_io_checkwrite() when it
 * wants to flush buffer.
 */
static void
hammer_close_cluster_quick(hammer_cluster_t cluster)
{
	if (cluster->state == HAMMER_CLUSTER_OPEN) {
		cluster->state = HAMMER_CLUSTER_IDLE;
		cluster->io.modified = 1;
		cluster->ondisk->clu_flags &= ~HAMMER_CLUF_OPEN;
		kprintf("CLOSE CLUSTER ON KERNEL WRITE\n");
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
			BUF_KERNPROC(bp);
			io->released = 0;
		}
	}
	io->modified = 1;
	vfs_bio_clrbuf(bp);
	return(0);
}

/*
 * This routine is called when a buffer within a cluster is modified.  We
 * mark the cluster open and immediately initiate asynchronous I/O.  Any
 * related hammer_buffer write I/O blocks until our async write completes.
 * This guarentees (inasmuch as the OS can) that the cluster recovery code
 * will see a cluster marked open if a crash occured while the filesystem
 * still had dirty buffers associated with that cluster.
 *
 * XXX
 */
void
hammer_io_notify_cluster(hammer_cluster_t cluster)
{
	struct hammer_io *io = &cluster->io;

	if (cluster->state == HAMMER_CLUSTER_IDLE) {
		hammer_lock_ex(&cluster->io.lock);
		if (cluster->state == HAMMER_CLUSTER_IDLE) {
			if (io->released)
				regetblk(io->bp);
			else
				io->released = 1;
			kprintf("MARK CLUSTER OPEN\n");
			cluster->ondisk->clu_flags |= HAMMER_CLUF_OPEN;
			cluster->state = HAMMER_CLUSTER_ASYNC;
			cluster->io.modified = 1;
			bawrite(io->bp);
		}
		hammer_unlock(&cluster->io.lock);
	}
}

/*
 * This routine is called on the last reference to a hammer structure.
 * Regardless of the state io->modified must be cleared when we return.
 *
 * If flush is non-zero we have to completely disassociate the bp from the
 * structure (which may involve blocking).  Otherwise we can leave the bp
 * passively associated with the structure.
 *
 * The caller is holding io->lock exclusively.
 */
void
hammer_io_release(struct hammer_io *io, int flush)
{
	union hammer_io_structure *iou = (void *)io;
	hammer_cluster_t cluster;
	struct buf *bp;
	int modified;

	if ((bp = io->bp) != NULL) {
		/*
		 * If neither we nor the kernel want to flush the bp, we can
		 * stop here.  Make sure the bp is passively released
		 * before returning.  Even though we are still holding it,
		 * we want to be notified when the kernel wishes to flush
		 * it out so make sure B_DELWRI is properly set if we had
		 * made modifications.
		 */
		if (flush == 0 && (bp->b_flags & B_LOCKED) == 0) {
			if ((bp->b_flags & B_DELWRI) == 0 && io->modified) {
				if (io->released)
					regetblk(bp);
				else
					io->released = 1;
				io->modified = 0;
				bdwrite(bp);
			} else if (io->released == 0) {
				/* buffer write state already synchronized */
				io->modified = 0;
				io->released = 1;
				bqrelse(bp);
			} else {
				/* buffer write state already synchronized */
				io->modified = 0;
			}
			return;
		}

		/*
		 * Either we want to flush the buffer or the kernel tried.
		 *
		 * If this is a hammer_buffer we may have to wait for the
		 * cluster header write to complete.
		 */
		if (iou->io.type == HAMMER_STRUCTURE_BUFFER &&
		    (io->modified || (bp->b_flags & B_DELWRI))) {
			cluster = iou->buffer.cluster;
			while (cluster->state == HAMMER_CLUSTER_ASYNC)
				tsleep(iou->buffer.cluster, 0, "hmrdep", 0);
		}

		/*
		 * If we have an open cluster header, close it
		 */
		if (iou->io.type == HAMMER_STRUCTURE_CLUSTER) {
			hammer_close_cluster(&iou->cluster);
		}

		/*
		 * Gain ownership of the buffer.  Nothing can take it away
		 * from the io structure while we have it locked, so we
		 * can safely reget.
		 *
		 * Once our thread owns the buffer we can disassociate it
		 * from the io structure.
		 */
		if (io->released)
			regetblk(bp);
		else
			io->released = 1;
		modified = io->modified;
		io->modified = 0;
		hammer_io_disassociate(iou);

		/*
		 * Now dispose of the buffer.  Someone tried to flush, so
		 * issue the I/O immediately.
		 */
		if (modified || (bp->b_flags & B_DELWRI))
			bawrite(bp);
		else
			bqrelse(bp);
	}
}

/*
 * Flush dirty data, if any.
 */
void
hammer_io_flush(struct hammer_io *io, struct hammer_sync_info *info)
{
	struct buf *bp;
	int error;

again:
	if ((bp = io->bp) == NULL)
		return;
	if (bp->b_flags & B_DELWRI)
		io->modified = 1;

	/*
	 * We can't initiate a write while the buffer is being modified
	 * by someone.
	 */
	while (io->lock.modifying) {
		io->lock.wanted = 1;
		kprintf("DELAYING IO FLUSH BP %p TYPE %d REFS %d modifying %d\n",
			bp, io->type, io->lock.refs, io->lock.modifying);
		tsleep(&io->lock, 0, "hmrfls", 0);
	}
	hammer_lock_ex(&io->lock);
	if (io->lock.modifying || io->bp == NULL) {
		hammer_unlock(&io->lock);
		goto again;
	}

	/*
	 * Acquire ownership of the buffer cache buffer so we can flush it
	 * out.
	 */
	if (io->released) {
		if (io->modified == 0)
			goto done;
		regetblk(bp);
	} else {
		io->released = 1;
	}

	/*
	 * Return the bp to the system, issuing I/O if necessary.  The
	 * system will issue a callback to us when it actually wants to
	 * throw the bp away.
	 */
	if (io->modified == 0) {
		bqrelse(bp);
	} else if (info->waitfor & MNT_WAIT) {
		io->modified = 0;
		error = bwrite(bp);
		if (error)
			info->error = error;
	} else {
		io->modified = 0;
		bawrite(bp);
	}
done:
	hammer_unlock(&io->lock);
}

/*
 * Called prior to any modifications being made to ondisk data.  This
 * forces the caller to wait for any writes to complete.  We explicitly
 * avoid the write-modify race.
 *
 * This routine is only called on hammer structures which are already
 * actively referenced.
 */
void
hammer_io_intend_modify(struct hammer_io *io)
{
	KKASSERT(io->lock.refs != 0 && io->bp != NULL);
	if (io->released) {
		hammer_lock_ex(&io->lock);
		if (io->released) {
			regetblk(io->bp);
			BUF_KERNPROC(io->bp);
			io->released = 0;
		}
		hammer_unlock(&io->lock);
	}
}

void
hammer_io_modify_done(struct hammer_io *io)
{
	KKASSERT(io->lock.modifying > 0);
	--io->lock.modifying;
	if (io->lock.wanted && io->lock.modifying == 0) {
		io->lock.wanted = 0;
		wakeup(&io->lock);
	}
}

/*
 * Mark an entity as not being dirty any more -- usually occurs when
 * the governing a-list has freed the entire entity.
 */
void
hammer_io_clear_modify(struct hammer_io *io)
{
	struct buf *bp;

	io->modified = 0;
	if ((bp = io->bp) != NULL) {
		if (io->released)
			regetblk(bp);
		else
			io->released = 1;
		if (io->modified == 0) {
			kprintf("hammer_io_clear_modify: cleared %p\n", io);
			bundirty(bp);
			bqrelse(bp);
		} else {
			bdwrite(bp);
		}
	}
}

/*
 * HAMMER_BIOOPS
 */

/*
 * Pre and post I/O callbacks.
 */
static void hammer_io_deallocate(struct buf *bp);

static void
hammer_io_start(struct buf *bp)
{
#if 0
	union hammer_io_structure *io = (void *)LIST_FIRST(&bp->b_dep);

	if (io->io.type == HAMMER_STRUCTURE_BUFFER) {
		while (io->buffer.cluster->io_in_progress) {
			kprintf("hammer_io_start: wait for cluster\n");
			tsleep(io->buffer.cluster, 0, "hmrdep", 0);
			kprintf("hammer_io_start: wait for cluster done\n");
		}
	}
#endif
}

static void
hammer_io_complete(struct buf *bp)
{
	union hammer_io_structure *io = (void *)LIST_FIRST(&bp->b_dep);

	if (io->io.type == HAMMER_STRUCTURE_CLUSTER) {
		if (io->cluster.state == HAMMER_CLUSTER_ASYNC) {
			io->cluster.state = HAMMER_CLUSTER_OPEN;
			wakeup(&io->cluster);
		}
	}
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

	/*
	 * Since the kernel is passing us a locked buffer, the HAMMER
	 * structure had better not believe it has a lock on the buffer.
	 */
	KKASSERT(io->io.released);
	crit_enter();

	/*
	 * First, ref the structure to prevent either the buffer or the
	 * structure from going away or being unexpectedly flushed.
	 */
	hammer_ref(&io->io.lock);

	/*
	 * Buffers can have active references from cached hammer_node's,
	 * even if those nodes are themselves passively cached.  Attempt
	 * to clean them out.  This may not succeed.
	 *
	 * We have to do some magic with io.released because
	 * hammer_io_intend_modify() can be called indirectly from the
	 * flush code, otherwise we might panic with a recursive bp lock.
	 */
	if (io->io.type == HAMMER_STRUCTURE_BUFFER &&
	    hammer_lock_ex_try(&io->io.lock) == 0) {
		io->io.released = 0;
		hammer_flush_buffer_nodes(&io->buffer);
		KKASSERT(io->io.released == 0);
		io->io.released = 1;
		hammer_unlock(&io->io.lock);
	}

	if (hammer_islastref(&io->io.lock)) {
		/*
		 * If we are the only ref left we can disassociate the I/O.
		 * It had better still be in a released state because the
		 * kernel is holding a lock on the buffer.  Any passive
		 * modifications should have already been synchronized with
		 * the buffer.
		 */
		KKASSERT(io->io.modified == 0);
		hammer_io_disassociate(io);

		/*
		 * Perform final rights on the structure.  This can cause
		 * a chain reaction - e.g. last buffer -> last cluster ->
		 * last supercluster -> last volume.
		 */
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
	} else {
		/*
		 * Otherwise tell the kernel not to destroy the buffer.
		 * 
		 * We have to unref the structure without performing any
		 * final rights to it to avoid a deadlock.
		 */
		bp->b_flags |= B_LOCKED;
		hammer_unref(&io->io.lock);
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

	if (iou->io.type == HAMMER_STRUCTURE_BUFFER &&
	    iou->buffer.cluster->state == HAMMER_CLUSTER_ASYNC) {
		/*
		 * Cannot write out a cluster buffer if the cluster header
		 * I/O opening the cluster has not completed.
		 */
		bp->b_flags |= B_LOCKED;
		return(-1);
	} else if (iou->io.lock.refs) {
		/*
		 * Cannot write out a bp if its associated buffer has active
		 * references.
		 */
		bp->b_flags |= B_LOCKED;
		return(-1);
	} else {
		/*
		 * We're good, but before we can let the kernel proceed we
		 * may have to make some adjustments.
		 *
		 * Since there are no refs on the io structure, HAMMER must
		 * have already synchronized its modify state with the bp
		 * so iou->io.modified should be 0.
		 */
		if (iou->io.type == HAMMER_STRUCTURE_CLUSTER)
			hammer_close_cluster_quick(&iou->cluster);
		hammer_io_disassociate(iou);
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

