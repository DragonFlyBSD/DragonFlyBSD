/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 * by Daniel Flores (GSOC 2013 - mentored by Matthew Dillon, compression) 
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
 * This module handles low level logical file I/O (strategy) which backs
 * the logical buffer cache.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/objcache.h>
#include <sys/event.h>
#include <sys/file.h>
#include <vfs/fifofs/fifo.h>

#include "hammer2.h"
#include "hammer2_lz4.h"

#include "zlib/hammer2_zlib.h"

struct objcache *cache_buffer_read;
struct objcache *cache_buffer_write;

/*
 * Strategy code (async logical file buffer I/O from system)
 *
 * WARNING: The strategy code cannot safely use hammer2 transactions
 *	    as this can deadlock against vfs_sync's vfsync() call
 *	    if multiple flushes are queued.  All H2 structures must
 *	    already be present and ready for the DIO.
 *
 *	    Reads can be initiated asynchronously, writes have to be
 *	    spooled to a separate thread for action to avoid deadlocks.
 */
static int hammer2_strategy_read(struct vop_strategy_args *ap);
static int hammer2_strategy_write(struct vop_strategy_args *ap);
static void hammer2_strategy_read_callback(hammer2_iocb_t *iocb);

int
hammer2_vop_strategy(struct vop_strategy_args *ap)
{
	struct bio *biop;
	struct buf *bp;
	int error;

	biop = ap->a_bio;
	bp = biop->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer2_strategy_read(ap);
		++hammer2_iod_file_read;
		break;
	case BUF_CMD_WRITE:
		error = hammer2_strategy_write(ap);
		++hammer2_iod_file_write;
		break;
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(biop);
		break;
	}
	return (error);
}

/*
 * Return the largest contiguous physical disk range for the logical
 * request, in bytes.
 *
 * (struct vnode *vp, off_t loffset, off_t *doffsetp, int *runp, int *runb)
 *
 * Basically disabled, the logical buffer write thread has to deal with
 * buffers one-at-a-time.
 */
int
hammer2_vop_bmap(struct vop_bmap_args *ap)
{
	*ap->a_doffsetp = NOOFFSET;
	if (ap->a_runp)
		*ap->a_runp = 0;
	if (ap->a_runb)
		*ap->a_runb = 0;
	return (EOPNOTSUPP);
}

/****************************************************************************
 *				READ SUPPORT				    *
 ****************************************************************************/
/* 
 * Callback used in read path in case that a block is compressed with LZ4.
 */
static
void
hammer2_decompress_LZ4_callback(const char *data, u_int bytes, struct bio *bio)
{
	struct buf *bp;
	char *compressed_buffer;
	int compressed_size;
	int result;

	bp = bio->bio_buf;

#if 0
	if bio->bio_caller_info2.index &&
	      bio->bio_caller_info1.uvalue32 !=
	      crc32(bp->b_data, bp->b_bufsize) --- return error
#endif

	KKASSERT(bp->b_bufsize <= HAMMER2_PBUFSIZE);
	compressed_size = *(const int *)data;
	KKASSERT(compressed_size <= bytes - sizeof(int));

	compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
	result = LZ4_decompress_safe(__DECONST(char *, &data[sizeof(int)]),
				     compressed_buffer,
				     compressed_size,
				     bp->b_bufsize);
	if (result < 0) {
		kprintf("READ PATH: Error during decompression."
			"bio %016jx/%d\n",
			(intmax_t)bio->bio_offset, bytes);
		/* make sure it isn't random garbage */
		bzero(compressed_buffer, bp->b_bufsize);
	}
	KKASSERT(result <= bp->b_bufsize);
	bcopy(compressed_buffer, bp->b_data, bp->b_bufsize);
	if (result < bp->b_bufsize)
		bzero(bp->b_data + result, bp->b_bufsize - result);
	objcache_put(cache_buffer_read, compressed_buffer);
	bp->b_resid = 0;
	bp->b_flags |= B_AGE;
}

/*
 * Callback used in read path in case that a block is compressed with ZLIB.
 * It is almost identical to LZ4 callback, so in theory they can be unified,
 * but we didn't want to make changes in bio structure for that.
 */
static
void
hammer2_decompress_ZLIB_callback(const char *data, u_int bytes, struct bio *bio)
{
	struct buf *bp;
	char *compressed_buffer;
	z_stream strm_decompress;
	int result;
	int ret;

	bp = bio->bio_buf;

	KKASSERT(bp->b_bufsize <= HAMMER2_PBUFSIZE);
	strm_decompress.avail_in = 0;
	strm_decompress.next_in = Z_NULL;

	ret = inflateInit(&strm_decompress);

	if (ret != Z_OK)
		kprintf("HAMMER2 ZLIB: Fatal error in inflateInit.\n");

	compressed_buffer = objcache_get(cache_buffer_read, M_INTWAIT);
	strm_decompress.next_in = __DECONST(char *, data);

	/* XXX supply proper size, subset of device bp */
	strm_decompress.avail_in = bytes;
	strm_decompress.next_out = compressed_buffer;
	strm_decompress.avail_out = bp->b_bufsize;

	ret = inflate(&strm_decompress, Z_FINISH);
	if (ret != Z_STREAM_END) {
		kprintf("HAMMER2 ZLIB: Fatar error during decompression.\n");
		bzero(compressed_buffer, bp->b_bufsize);
	}
	bcopy(compressed_buffer, bp->b_data, bp->b_bufsize);
	result = bp->b_bufsize - strm_decompress.avail_out;
	if (result < bp->b_bufsize)
		bzero(bp->b_data + result, strm_decompress.avail_out);
	objcache_put(cache_buffer_read, compressed_buffer);
	ret = inflateEnd(&strm_decompress);

	bp->b_resid = 0;
	bp->b_flags |= B_AGE;
}

/*
 * Logical buffer I/O, async read.
 */
static
int
hammer2_strategy_read(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct bio *bio;
	struct bio *nbio;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cparent;
	hammer2_cluster_t *cluster;
	hammer2_key_t key_dummy;
	hammer2_key_t lbase;
	uint8_t btype;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	nbio = push_bio(bio);

	lbase = bio->bio_offset;
	KKASSERT(((int)lbase & HAMMER2_PBUFMASK) == 0);

	/*
	 * Lookup the file offset.
	 */
	hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS |
			       HAMMER2_RESOLVE_SHARED);
	cparent = hammer2_inode_cluster(ip, HAMMER2_RESOLVE_ALWAYS |
					    HAMMER2_RESOLVE_SHARED);
	cluster = hammer2_cluster_lookup(cparent, &key_dummy,
				       lbase, lbase,
				       HAMMER2_LOOKUP_NODATA |
				       HAMMER2_LOOKUP_SHARED);
	hammer2_inode_unlock(ip, cparent);

	/*
	 * Data is zero-fill if no cluster could be found
	 * (XXX or EIO on a cluster failure).
	 */
	if (cluster == NULL) {
		bp->b_resid = 0;
		bp->b_error = 0;
		bzero(bp->b_data, bp->b_bcount);
		biodone(nbio);
		return(0);
	}

	/*
	 * Cluster elements must be type INODE or type DATA, but the
	 * compression mode (or not) for DATA chains can be different for
	 * each chain.  This will be handled by the callback.
	 *
	 * If the cluster already has valid data the callback will be made
	 * immediately/synchronously.
	 */
	btype = hammer2_cluster_type(cluster);
	if (btype != HAMMER2_BREF_TYPE_INODE &&
	    btype != HAMMER2_BREF_TYPE_DATA) {
		panic("READ PATH: hammer2_strategy_read: unknown bref type");
	}
	hammer2_cluster_load_async(cluster, hammer2_strategy_read_callback,
				   nbio);
	return(0);
}

/*
 * Read callback for hammer2_cluster_load_async().  The load function may
 * start several actual I/Os but will only make one callback, typically with
 * the first valid I/O XXX
 */
static
void
hammer2_strategy_read_callback(hammer2_iocb_t *iocb)
{
	struct bio *bio = iocb->ptr;	/* original logical buffer */
	struct buf *bp = bio->bio_buf;	/* original logical buffer */
	hammer2_chain_t *chain;
	hammer2_cluster_t *cluster;
	hammer2_io_t *dio;
	char *data;
	int i;

	/*
	 * Extract data and handle iteration on I/O failure.  iocb->off
	 * is the cluster index for iteration.
	 */
	cluster = iocb->cluster;
	dio = iocb->dio;	/* can be NULL if iocb not in progress */

	/*
	 * Work to do if INPROG set, else dio is already good or dio is
	 * NULL (which is the shortcut case if chain->data is already good).
	 */
	if (iocb->flags & HAMMER2_IOCB_INPROG) {
		/*
		 * Read attempt not yet made.  Issue an asynchronous read
		 * if necessary and return, operation will chain back to
		 * this function.
		 */
		if ((iocb->flags & HAMMER2_IOCB_READ) == 0) {
			if (dio->bp == NULL ||
			    (dio->bp->b_flags & B_CACHE) == 0) {
				if (dio->bp) {
					bqrelse(dio->bp);
					dio->bp = NULL;
				}
				iocb->flags |= HAMMER2_IOCB_READ;
				breadcb(dio->hmp->devvp,
					dio->pbase, dio->psize,
					hammer2_io_callback, iocb);
				return;
			}
		}
	}

	/*
	 * If we have a DIO it is now done, check for an error and
	 * calculate the data.
	 *
	 * If there is no DIO it is an optimization by
	 * hammer2_cluster_load_async(), the data is available in
	 * chain->data.
	 */
	if (dio) {
		if (dio->bp->b_flags & B_ERROR) {
			i = (int)iocb->lbase + 1;
			if (i >= cluster->nchains) {
				bp->b_flags |= B_ERROR;
				bp->b_error = dio->bp->b_error;
				hammer2_io_complete(iocb);
				biodone(bio);
				hammer2_cluster_unlock(cluster);
				hammer2_cluster_drop(cluster);
			} else {
				hammer2_io_complete(iocb); /* XXX */
				chain = cluster->array[i].chain;
				kprintf("hammer2: IO CHAIN-%d %p\n", i, chain);
				hammer2_adjreadcounter(&chain->bref,
						       chain->bytes);
				iocb->chain = chain;
				iocb->lbase = (off_t)i;
				iocb->flags = 0;
				iocb->error = 0;
				hammer2_io_getblk(chain->hmp,
						  chain->bref.data_off,
						  chain->bytes,
						  iocb);
			}
			return;
		}
		chain = iocb->chain;
		data = hammer2_io_data(dio, chain->bref.data_off);
	} else {
		/*
		 * Special synchronous case, data present in chain->data.
		 */
		chain = iocb->chain;
		data = (void *)chain->data;
	}

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		/*
		 * Data is embedded in the inode (copy from inode).
		 */
		bcopy(((hammer2_inode_data_t *)data)->u.data,
		      bp->b_data, HAMMER2_EMBEDDED_BYTES);
		bzero(bp->b_data + HAMMER2_EMBEDDED_BYTES,
		      bp->b_bcount - HAMMER2_EMBEDDED_BYTES);
		bp->b_resid = 0;
		bp->b_error = 0;
	} else if (chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		/*
		 * Data is on-media, issue device I/O and copy.
		 *
		 * XXX direct-IO shortcut could go here XXX.
		 */
		switch (HAMMER2_DEC_COMP(chain->bref.methods)) {
		case HAMMER2_COMP_LZ4:
			hammer2_decompress_LZ4_callback(data, chain->bytes,
							bio);
			break;
		case HAMMER2_COMP_ZLIB:
			hammer2_decompress_ZLIB_callback(data, chain->bytes,
							 bio);
			break;
		case HAMMER2_COMP_NONE:
			KKASSERT(chain->bytes <= bp->b_bcount);
			bcopy(data, bp->b_data, chain->bytes);
			if (chain->bytes < bp->b_bcount) {
				bzero(bp->b_data + chain->bytes,
				      bp->b_bcount - chain->bytes);
			}
			bp->b_flags |= B_NOTMETA;
			bp->b_resid = 0;
			bp->b_error = 0;
			break;
		default:
			panic("hammer2_strategy_read: "
			      "unknown compression type");
		}
	} else {
		/* bqrelse the dio to help stabilize the call to panic() */
		if (dio)
			hammer2_io_bqrelse(&dio);
		panic("hammer2_strategy_read: unknown bref type");
	}

	/*
	 * Once the iocb is cleaned up the DIO (if any) will no longer be
	 * in-progress but will still have a ref.  Be sure to release
	 * the ref.
	 */
	hammer2_io_complete(iocb);		/* physical management */
	if (dio)				/* physical dio & buffer */
		hammer2_io_bqrelse(&dio);
	hammer2_cluster_unlock(cluster);	/* cluster management */
	hammer2_cluster_drop(cluster);		/* cluster management */
	biodone(bio);				/* logical buffer */
}

/****************************************************************************
 *				WRITE SUPPORT				    *
 ****************************************************************************/

/* 
 * Functions for compression in threads,
 * from hammer2_vnops.c
 */
static void hammer2_write_file_core(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_cluster_t *cparent,
				hammer2_key_t lbase, int ioflag, int pblksize,
				int *errorp);
static void hammer2_compress_and_write(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_cluster_t *cparent,
				hammer2_key_t lbase, int ioflag,
				int pblksize, int *errorp,
				int comp_algo, int check_algo);
static void hammer2_zero_check_and_write(struct buf *bp,
				hammer2_trans_t *trans, hammer2_inode_t *ip,
				hammer2_cluster_t *cparent,
				hammer2_key_t lbase,
				int ioflag, int pblksize, int *errorp,
				int check_algo);
static int test_block_zeros(const char *buf, size_t bytes);
static void zero_write(struct buf *bp, hammer2_trans_t *trans,
				hammer2_inode_t *ip,
				hammer2_cluster_t *cparent,
				hammer2_key_t lbase,
				int *errorp);
static void hammer2_write_bp(hammer2_cluster_t *cluster, struct buf *bp,
				int ioflag, int pblksize, int *errorp,
				int check_algo);


static
int
hammer2_strategy_write(struct vop_strategy_args *ap)
{	
	hammer2_pfs_t *pmp;
	struct bio *bio;
	struct buf *bp;
	hammer2_inode_t *ip;
	
	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	pmp = ip->pmp;
	
	hammer2_lwinprog_ref(pmp);
	hammer2_trans_assert_strategy(pmp);
	hammer2_mtx_ex(&pmp->wthread_mtx);
	if (TAILQ_EMPTY(&pmp->wthread_bioq.queue)) {
		bioq_insert_tail(&pmp->wthread_bioq, ap->a_bio);
		hammer2_mtx_unlock(&pmp->wthread_mtx);
		wakeup(&pmp->wthread_bioq);
	} else {
		bioq_insert_tail(&pmp->wthread_bioq, ap->a_bio);
		hammer2_mtx_unlock(&pmp->wthread_mtx);
	}
	hammer2_lwinprog_wait(pmp);

	return(0);
}

/*
 * Thread to handle bioq for strategy write (started from hammer2_vfsops.c)
 */
void
hammer2_write_thread(void *arg)
{
	hammer2_pfs_t *pmp;
	struct bio *bio;
	struct buf *bp;
	hammer2_trans_t trans;
	struct vnode *vp;
	hammer2_inode_t *ip;
	hammer2_cluster_t *cparent;
	hammer2_key_t lbase;
	int lblksize;
	int pblksize;
	int error;
	
	pmp = arg;
	
	hammer2_mtx_ex(&pmp->wthread_mtx);
	for (;;) {
		/*
		 * Wait for work.  Break out and destroy the thread only if
		 * requested and no work remains.
		 */
		if (bioq_first(&pmp->wthread_bioq) == NULL) {
			if (pmp->wthread_destroy)
				break;
			mtxsleep(&pmp->wthread_bioq, &pmp->wthread_mtx,
				 0, "h2bioqw", 0);
			continue;
		}

		/*
		 * Special transaction for logical buffer cache writes.
		 */
		hammer2_trans_init(&trans, pmp, HAMMER2_TRANS_BUFCACHE);

		while ((bio = bioq_takefirst(&pmp->wthread_bioq)) != NULL) {
			/*
			 * dummy bio for synchronization.  The transaction
			 * must be terminated.
			 */
			if (bio->bio_buf == NULL) {
				bio->bio_flags |= BIO_DONE;
				/* bio will become invalid after DONE set */
				wakeup(bio);
				break;
			}

			/*
			 * else normal bio processing
			 */
			hammer2_mtx_unlock(&pmp->wthread_mtx);

			hammer2_lwinprog_drop(pmp);
			
			error = 0;
			bp = bio->bio_buf;
			vp = bp->b_vp;
			ip = VTOI(vp);

			/*
			 * Inode is modified, flush size and mtime changes
			 * to ensure that the file size remains consistent
			 * with the buffers being flushed.
			 *
			 * NOTE: The inode_fsync() call only flushes the
			 *	 inode's meta-data state, it doesn't try
			 *	 to flush underlying buffers or chains.
			 *
			 * NOTE: hammer2_write_file_core() may indirectly
			 *	 modify and modsync the inode.
			 */
			hammer2_inode_lock(ip, HAMMER2_RESOLVE_ALWAYS);
			cparent = hammer2_inode_cluster(ip,
							HAMMER2_RESOLVE_ALWAYS);
			if (ip->flags & (HAMMER2_INODE_RESIZED |
					 HAMMER2_INODE_MTIME)) {
				hammer2_inode_fsync(&trans, ip, cparent);
			}
			lblksize = hammer2_calc_logical(ip, bio->bio_offset,
							&lbase, NULL);
			pblksize = hammer2_calc_physical(ip, lbase);
			hammer2_write_file_core(bp, &trans, ip,
						cparent,
						lbase, IO_ASYNC,
						pblksize, &error);
			hammer2_inode_unlock(ip, cparent);
			if (error) {
				kprintf("hammer2: error in buffer write\n");
				bp->b_flags |= B_ERROR;
				bp->b_error = EIO;
			}
			biodone(bio);
			hammer2_mtx_ex(&pmp->wthread_mtx);
		}
		hammer2_trans_done(&trans);
	}
	pmp->wthread_destroy = -1;
	wakeup(&pmp->wthread_destroy);
	
	hammer2_mtx_unlock(&pmp->wthread_mtx);
}

/*
 * Wait for pending I/O to complete
 */
void
hammer2_bioq_sync(hammer2_pfs_t *pmp)
{
	struct bio sync_bio;

	bzero(&sync_bio, sizeof(sync_bio));	/* dummy with no bio_buf */
	hammer2_mtx_ex(&pmp->wthread_mtx);
	if (pmp->wthread_destroy == 0 &&
	    TAILQ_FIRST(&pmp->wthread_bioq.queue)) {
		bioq_insert_tail(&pmp->wthread_bioq, &sync_bio);
		while ((sync_bio.bio_flags & BIO_DONE) == 0)
			mtxsleep(&sync_bio, &pmp->wthread_mtx, 0, "h2bioq", 0);
	}
	hammer2_mtx_unlock(&pmp->wthread_mtx);
}

/* 
 * Create a new cluster at (cparent, lbase) and assign physical storage,
 * returning a cluster suitable for I/O.  The cluster will be in a modified
 * state.
 *
 * cparent can wind up being anything.
 *
 * NOTE: Special case for data embedded in inode.
 */
static
hammer2_cluster_t *
hammer2_assign_physical(hammer2_trans_t *trans,
			hammer2_inode_t *ip, hammer2_cluster_t *cparent,
			hammer2_key_t lbase, int pblksize, int *errorp)
{
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *dparent;
	hammer2_key_t key_dummy;
	int pradix = hammer2_getradix(pblksize);

	/*
	 * Locate the chain associated with lbase, return a locked chain.
	 * However, do not instantiate any data reference (which utilizes a
	 * device buffer) because we will be using direct IO via the
	 * logical buffer cache buffer.
	 */
	*errorp = 0;
	KKASSERT(pblksize >= HAMMER2_ALLOC_MIN);
retry:
	dparent = hammer2_cluster_lookup_init(cparent, 0);
	cluster = hammer2_cluster_lookup(dparent, &key_dummy,
				     lbase, lbase,
				     HAMMER2_LOOKUP_NODATA);

	if (cluster == NULL) {
		/*
		 * We found a hole, create a new chain entry.
		 *
		 * NOTE: DATA chains are created without device backing
		 *	 store (nor do we want any).
		 */
		*errorp = hammer2_cluster_create(trans, dparent, &cluster,
					       lbase, HAMMER2_PBUFRADIX,
					       HAMMER2_BREF_TYPE_DATA,
					       pblksize, 0);
		if (cluster == NULL) {
			hammer2_cluster_lookup_done(dparent);
			panic("hammer2_cluster_create: par=%p error=%d\n",
				dparent->focus, *errorp);
			goto retry;
		}
		/*ip->delta_dcount += pblksize;*/
	} else {
		switch (hammer2_cluster_type(cluster)) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * The data is embedded in the inode, which requires
			 * a bit more finess.
			 */
			hammer2_cluster_modify_ip(trans, ip, cluster, 0);
			break;
		case HAMMER2_BREF_TYPE_DATA:
			if (hammer2_cluster_need_resize(cluster, pblksize)) {
				hammer2_cluster_resize(trans, ip,
						     dparent, cluster,
						     pradix,
						     HAMMER2_MODIFY_OPTDATA);
			}

			/*
			 * DATA buffers must be marked modified whether the
			 * data is in a logical buffer or not.  We also have
			 * to make this call to fixup the chain data pointers
			 * after resizing in case this is an encrypted or
			 * compressed buffer.
			 */
			hammer2_cluster_modify(trans, cluster,
					       HAMMER2_MODIFY_OPTDATA);
			break;
		default:
			panic("hammer2_assign_physical: bad type");
			/* NOT REACHED */
			break;
		}
	}

	/*
	 * Cleanup.  If cluster wound up being the inode itself, i.e.
	 * the DIRECTDATA case for offset 0, then we need to update cparent.
	 * The caller expects cparent to not become stale.
	 */
	hammer2_cluster_lookup_done(dparent);
	/* dparent = NULL; safety */
	return (cluster);
}

/* 
 * hammer2_write_file_core() - hammer2_write_thread() helper
 *
 * The core write function which determines which path to take
 * depending on compression settings.  We also have to locate the
 * related clusters so we can calculate and set the check data for
 * the blockref.
 */
static
void
hammer2_write_file_core(struct buf *bp, hammer2_trans_t *trans,
			hammer2_inode_t *ip,
			hammer2_cluster_t *cparent,
			hammer2_key_t lbase, int ioflag, int pblksize,
			int *errorp)
{
	hammer2_cluster_t *cluster;

	switch(HAMMER2_DEC_ALGO(ip->meta.comp_algo)) {
	case HAMMER2_COMP_NONE:
		/*
		 * We have to assign physical storage to the buffer
		 * we intend to dirty or write now to avoid deadlocks
		 * in the strategy code later.
		 *
		 * This can return NOOFFSET for inode-embedded data.
		 * The strategy code will take care of it in that case.
		 */
		cluster = hammer2_assign_physical(trans, ip, cparent,
						lbase, pblksize,
						errorp);
		if (cluster->ddflag) {
			hammer2_inode_data_t *wipdata;

			wipdata = hammer2_cluster_modify_ip(trans, ip,
							    cluster, 0);
			KKASSERT(wipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bcopy(bp->b_data, wipdata->u.data,
			      HAMMER2_EMBEDDED_BYTES);
			hammer2_cluster_modsync(cluster);
		} else {
			hammer2_write_bp(cluster, bp, ioflag, pblksize,
					 errorp, ip->meta.check_algo);
		}
		if (cluster) {
			hammer2_cluster_unlock(cluster);
			hammer2_cluster_drop(cluster);
		}
		break;
	case HAMMER2_COMP_AUTOZERO:
		/*
		 * Check for zero-fill only
		 */
		hammer2_zero_check_and_write(bp, trans, ip,
				    cparent, lbase,
				    ioflag, pblksize, errorp,
				    ip->meta.check_algo);
		break;
	case HAMMER2_COMP_LZ4:
	case HAMMER2_COMP_ZLIB:
	default:
		/*
		 * Check for zero-fill and attempt compression.
		 */
		hammer2_compress_and_write(bp, trans, ip,
					   cparent,
					   lbase, ioflag,
					   pblksize, errorp,
					   ip->meta.comp_algo,
					   ip->meta.check_algo);
		break;
	}
}

/*
 * Helper
 *
 * Generic function that will perform the compression in compression
 * write path. The compression algorithm is determined by the settings
 * obtained from inode.
 */
static
void
hammer2_compress_and_write(struct buf *bp, hammer2_trans_t *trans,
	hammer2_inode_t *ip,
	hammer2_cluster_t *cparent,
	hammer2_key_t lbase, int ioflag, int pblksize,
	int *errorp, int comp_algo, int check_algo)
{
	hammer2_cluster_t *cluster;
	hammer2_chain_t *chain;
	int comp_size;
	int comp_block_size;
	int i;
	char *comp_buffer;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, trans, ip, cparent, lbase, errorp);
		return;
	}

	comp_size = 0;
	comp_buffer = NULL;

	KKASSERT(pblksize / 2 <= 32768);
		
	if (ip->comp_heuristic < 8 || (ip->comp_heuristic & 7) == 0) {
		z_stream strm_compress;
		int comp_level;
		int ret;

		switch(HAMMER2_DEC_ALGO(comp_algo)) {
		case HAMMER2_COMP_LZ4:
			comp_buffer = objcache_get(cache_buffer_write,
						   M_INTWAIT);
			comp_size = LZ4_compress_limitedOutput(
					bp->b_data,
					&comp_buffer[sizeof(int)],
					pblksize,
					pblksize / 2 - sizeof(int));
			/*
			 * We need to prefix with the size, LZ4
			 * doesn't do it for us.  Add the related
			 * overhead.
			 */
			*(int *)comp_buffer = comp_size;
			if (comp_size)
				comp_size += sizeof(int);
			break;
		case HAMMER2_COMP_ZLIB:
			comp_level = HAMMER2_DEC_LEVEL(comp_algo);
			if (comp_level == 0)
				comp_level = 6;	/* default zlib compression */
			else if (comp_level < 6)
				comp_level = 6;
			else if (comp_level > 9)
				comp_level = 9;
			ret = deflateInit(&strm_compress, comp_level);
			if (ret != Z_OK) {
				kprintf("HAMMER2 ZLIB: fatal error "
					"on deflateInit.\n");
			}

			comp_buffer = objcache_get(cache_buffer_write,
						   M_INTWAIT);
			strm_compress.next_in = bp->b_data;
			strm_compress.avail_in = pblksize;
			strm_compress.next_out = comp_buffer;
			strm_compress.avail_out = pblksize / 2;
			ret = deflate(&strm_compress, Z_FINISH);
			if (ret == Z_STREAM_END) {
				comp_size = pblksize / 2 -
					    strm_compress.avail_out;
			} else {
				comp_size = 0;
			}
			ret = deflateEnd(&strm_compress);
			break;
		default:
			kprintf("Error: Unknown compression method.\n");
			kprintf("Comp_method = %d.\n", comp_algo);
			break;
		}
	}

	if (comp_size == 0) {
		/*
		 * compression failed or turned off
		 */
		comp_block_size = pblksize;	/* safety */
		if (++ip->comp_heuristic > 128)
			ip->comp_heuristic = 8;
	} else {
		/*
		 * compression succeeded
		 */
		ip->comp_heuristic = 0;
		if (comp_size <= 1024) {
			comp_block_size = 1024;
		} else if (comp_size <= 2048) {
			comp_block_size = 2048;
		} else if (comp_size <= 4096) {
			comp_block_size = 4096;
		} else if (comp_size <= 8192) {
			comp_block_size = 8192;
		} else if (comp_size <= 16384) {
			comp_block_size = 16384;
		} else if (comp_size <= 32768) {
			comp_block_size = 32768;
		} else {
			panic("hammer2: WRITE PATH: "
			      "Weird comp_size value.");
			/* NOT REACHED */
			comp_block_size = pblksize;
		}
	}

	cluster = hammer2_assign_physical(trans, ip, cparent,
					  lbase, comp_block_size,
					  errorp);
	if (*errorp) {
		kprintf("WRITE PATH: An error occurred while "
			"assigning physical space.\n");
		KKASSERT(cluster == NULL);
		goto done;
	}

	if (cluster->ddflag) {
		hammer2_inode_data_t *wipdata;

		wipdata = &hammer2_cluster_wdata(cluster)->ipdata;
		KKASSERT(wipdata->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA);
		KKASSERT(bp->b_loffset == 0);
		bcopy(bp->b_data, wipdata->u.data, HAMMER2_EMBEDDED_BYTES);
		hammer2_cluster_modsync(cluster);
	} else
	for (i = 0; i < cluster->nchains; ++i) {
		hammer2_io_t *dio;
		char *bdata;

		/* XXX hackx */

		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		chain = cluster->array[i].chain;	/* XXX */
		if (chain == NULL)
			continue;
		KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);

		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			panic("hammer2_write_bp: unexpected inode\n");
			break;
		case HAMMER2_BREF_TYPE_DATA:
			/*
			 * Optimize out the read-before-write
			 * if possible.
			 */
			*errorp = hammer2_io_newnz(chain->hmp,
						   chain->bref.data_off,
						   chain->bytes,
						   &dio);
			if (*errorp) {
				hammer2_io_brelse(&dio);
				kprintf("hammer2: WRITE PATH: "
					"dbp bread error\n");
				break;
			}
			bdata = hammer2_io_data(dio, chain->bref.data_off);

			/*
			 * When loading the block make sure we don't
			 * leave garbage after the compressed data.
			 */
			if (comp_size) {
				chain->bref.methods =
					HAMMER2_ENC_COMP(comp_algo) +
					HAMMER2_ENC_CHECK(check_algo);
				bcopy(comp_buffer, bdata, comp_size);
				if (comp_size != comp_block_size) {
					bzero(bdata + comp_size,
					      comp_block_size - comp_size);
				}
			} else {
				chain->bref.methods =
					HAMMER2_ENC_COMP(
						HAMMER2_COMP_NONE) +
					HAMMER2_ENC_CHECK(check_algo);
				bcopy(bp->b_data, bdata, pblksize);
			}

			/*
			 * The flush code doesn't calculate check codes for
			 * file data (doing so can result in excessive I/O),
			 * so we do it here.
			 */
			hammer2_chain_setcheck(chain, bdata);

			/*
			 * Device buffer is now valid, chain is no longer in
			 * the initial state.
			 *
			 * (No blockref table worries with file data)
			 */
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

			/* Now write the related bdp. */
			if (ioflag & IO_SYNC) {
				/*
				 * Synchronous I/O requested.
				 */
				hammer2_io_bwrite(&dio);
			/*
			} else if ((ioflag & IO_DIRECT) &&
				   loff + n == pblksize) {
				hammer2_io_bdwrite(&dio);
			*/
			} else if (ioflag & IO_ASYNC) {
				hammer2_io_bawrite(&dio);
			} else {
				hammer2_io_bdwrite(&dio);
			}
			break;
		default:
			panic("hammer2_write_bp: bad chain type %d\n",
				chain->bref.type);
			/* NOT REACHED */
			break;
		}
	}
done:
	if (cluster) {
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
	}
	if (comp_buffer)
		objcache_put(cache_buffer_write, comp_buffer);
}

/*
 * Helper
 *
 * Function that performs zero-checking and writing without compression,
 * it corresponds to default zero-checking path.
 */
static
void
hammer2_zero_check_and_write(struct buf *bp, hammer2_trans_t *trans,
	hammer2_inode_t *ip,
	hammer2_cluster_t *cparent,
	hammer2_key_t lbase, int ioflag, int pblksize, int *errorp,
	int check_algo)
{
	hammer2_cluster_t *cluster;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, trans, ip, cparent, lbase, errorp);
	} else {
		cluster = hammer2_assign_physical(trans, ip, cparent,
						  lbase, pblksize, errorp);
		hammer2_write_bp(cluster, bp, ioflag, pblksize, errorp,
				 check_algo);
		if (cluster) {
			hammer2_cluster_unlock(cluster);
			hammer2_cluster_drop(cluster);
		}
	}
}

/*
 * Helper
 *
 * A function to test whether a block of data contains only zeros,
 * returns TRUE (non-zero) if the block is all zeros.
 */
static
int
test_block_zeros(const char *buf, size_t bytes)
{
	size_t i;

	for (i = 0; i < bytes; i += sizeof(long)) {
		if (*(const long *)(buf + i) != 0)
			return (0);
	}
	return (1);
}

/*
 * Helper
 *
 * Function to "write" a block that contains only zeros.
 */
static
void
zero_write(struct buf *bp, hammer2_trans_t *trans,
	   hammer2_inode_t *ip,
	   hammer2_cluster_t *cparent,
	   hammer2_key_t lbase, int *errorp __unused)
{
	hammer2_cluster_t *cluster;
	hammer2_key_t key_dummy;

	cparent = hammer2_cluster_lookup_init(cparent, 0);
	cluster = hammer2_cluster_lookup(cparent, &key_dummy, lbase, lbase,
				     HAMMER2_LOOKUP_NODATA);
	if (cluster) {
		if (cluster->ddflag) {
			hammer2_inode_data_t *wipdata;

			wipdata = hammer2_cluster_modify_ip(trans, ip,
							    cluster, 0);
			KKASSERT(wipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bzero(wipdata->u.data, HAMMER2_EMBEDDED_BYTES);
			hammer2_cluster_modsync(cluster);
		} else {
			hammer2_cluster_delete(trans, cparent, cluster,
					       HAMMER2_DELETE_PERMANENT);
		}
		hammer2_cluster_unlock(cluster);
		hammer2_cluster_drop(cluster);
	}
	hammer2_cluster_lookup_done(cparent);
}

/*
 * Helper
 *
 * Function to write the data as it is, without performing any sort of
 * compression. This function is used in path without compression and
 * default zero-checking path.
 */
static
void
hammer2_write_bp(hammer2_cluster_t *cluster, struct buf *bp, int ioflag,
				int pblksize, int *errorp, int check_algo)
{
	hammer2_chain_t *chain;
	hammer2_inode_data_t *wipdata;
	hammer2_io_t *dio;
	char *bdata;
	int error;
	int i;

	error = 0;	/* XXX TODO below */

	for (i = 0; i < cluster->nchains; ++i) {
		if ((cluster->array[i].flags & HAMMER2_CITEM_FEMOD) == 0)
			continue;
		chain = cluster->array[i].chain;	/* XXX */
		if (chain == NULL)
			continue;
		KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);

		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			wipdata = &hammer2_chain_wdata(chain)->ipdata;
			KKASSERT(wipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bcopy(bp->b_data, wipdata->u.data,
			      HAMMER2_EMBEDDED_BYTES);
			error = 0;
			break;
		case HAMMER2_BREF_TYPE_DATA:
			error = hammer2_io_newnz(chain->hmp,
						 chain->bref.data_off,
						 chain->bytes, &dio);
			if (error) {
				hammer2_io_bqrelse(&dio);
				kprintf("hammer2: WRITE PATH: "
					"dbp bread error\n");
				break;
			}
			bdata = hammer2_io_data(dio, chain->bref.data_off);

			chain->bref.methods = HAMMER2_ENC_COMP(
							HAMMER2_COMP_NONE) +
					      HAMMER2_ENC_CHECK(check_algo);
			bcopy(bp->b_data, bdata, chain->bytes);

			/*
			 * The flush code doesn't calculate check codes for
			 * file data (doing so can result in excessive I/O),
			 * so we do it here.
			 */
			hammer2_chain_setcheck(chain, bdata);

			/*
			 * Device buffer is now valid, chain is no longer in
			 * the initial state.
			 *
			 * (No blockref table worries with file data)
			 */
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

			if (ioflag & IO_SYNC) {
				/*
				 * Synchronous I/O requested.
				 */
				hammer2_io_bwrite(&dio);
			/*
			} else if ((ioflag & IO_DIRECT) &&
				   loff + n == pblksize) {
				hammer2_io_bdwrite(&dio);
			*/
			} else if (ioflag & IO_ASYNC) {
				hammer2_io_bawrite(&dio);
			} else {
				hammer2_io_bdwrite(&dio);
			}
			break;
		default:
			panic("hammer2_write_bp: bad chain type %d\n",
			      chain->bref.type);
			/* NOT REACHED */
			error = 0;
			break;
		}
		KKASSERT(error == 0);	/* XXX TODO */
	}
	*errorp = error;
}
