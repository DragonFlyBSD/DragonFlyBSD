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
