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
static void hammer2_strategy_xop_read(hammer2_xop_t *arg, int clindex);
static void hammer2_strategy_xop_write(hammer2_xop_t *arg, int clindex);
static int hammer2_strategy_read(struct vop_strategy_args *ap);
static int hammer2_strategy_write(struct vop_strategy_args *ap);
static void hammer2_strategy_read_completion(hammer2_chain_t *chain,
				char *data, struct bio *bio);

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
	hammer2_xop_strategy_t *xop;
	struct buf *bp;
	struct bio *bio;
	struct bio *nbio;
	hammer2_inode_t *ip;
	hammer2_key_t lbase;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = VTOI(ap->a_vp);
	nbio = push_bio(bio);

	lbase = bio->bio_offset;
	KKASSERT(((int)lbase & HAMMER2_PBUFMASK) == 0);

	xop = &hammer2_xop_alloc(ip)->xop_strategy;
	xop->finished = 0;
	xop->bio = bio;
	xop->lbase = lbase;
	hammer2_xop_start(&xop->head, hammer2_strategy_xop_read);

	return(0);
}

/*
 * Per-node XOP (threaded), do a synchronous lookup of the chain and
 * its data.  The frontend is asynchronous, so we are also responsible
 * for racing to terminate the frontend.
 */
static
void
hammer2_strategy_xop_read(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_strategy_t *xop = &arg->xop_strategy;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_key_t key_dummy;
	hammer2_key_t lbase;
	struct bio *bio;
	struct buf *bp;
	int cache_index = -1;
	int error;

	lbase = xop->lbase;
	bio = xop->bio;
	bp = bio->bio_buf;

	parent = hammer2_inode_chain(xop->head.ip, clindex,
				     HAMMER2_RESOLVE_ALWAYS |
				     HAMMER2_RESOLVE_SHARED);
	if (parent) {
		chain = hammer2_chain_lookup(&parent, &key_dummy,
					     lbase, lbase,
					     &cache_index,
					     HAMMER2_LOOKUP_ALWAYS |
					     HAMMER2_LOOKUP_SHARED);
		error = chain ? chain->error : 0;
	} else {
		error = EIO;
		chain = NULL;
	}
	error = hammer2_xop_feed(&xop->head, chain, clindex, error);
	if (chain)
		hammer2_chain_drop(chain);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
	chain = NULL;	/* safety */
	parent = NULL;	/* safety */

	/*
	 * Race to finish the frontend
	 */
	if (xop->finished)
		return;
	hammer2_mtx_ex(&xop->head.xgrp->mtx2);
	if (xop->finished) {
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		return;
	}

	/*
	 * Async operation has not completed and we now own the lock.
	 * Determine if we can complete the operation by issuing the
	 * frontend collection non-blocking.
	 */
	error = hammer2_xop_collect(&xop->head, HAMMER2_XOP_COLLECT_NOWAIT);

	switch(error) {
	case 0:
		xop->finished = 1;
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		chain = xop->head.cluster.focus;
		hammer2_strategy_read_completion(chain, (char *)chain->data,
						 xop->bio);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		biodone(bio);
		break;
	case ENOENT:
		xop->finished = 1;
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		bp->b_resid = 0;
		bp->b_error = 0;
		bzero(bp->b_data, bp->b_bcount);
		biodone(bio);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		break;
	case EINPROGRESS:
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		break;
	default:
		xop->finished = 1;
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		biodone(bio);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		break;
	}
}

static
void
hammer2_strategy_read_completion(hammer2_chain_t *chain, char *data,
				 struct bio *bio)
{
	struct buf *bp = bio->bio_buf;

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
		panic("hammer2_strategy_read: unknown bref type");
	}
}

/****************************************************************************
 *				WRITE SUPPORT				    *
 ****************************************************************************/

/* 
 * Functions for compression in threads,
 * from hammer2_vnops.c
 */
static void hammer2_write_file_core(struct buf *bp, hammer2_inode_t *ip,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase, int ioflag, int pblksize,
				int *errorp);
static void hammer2_compress_and_write(struct buf *bp, hammer2_inode_t *ip,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase, int ioflag,
				int pblksize, int *errorp,
				int comp_algo, int check_algo);
static void hammer2_zero_check_and_write(struct buf *bp, hammer2_inode_t *ip,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase,
				int ioflag, int pblksize, int *errorp,
				int check_algo);
static int test_block_zeros(const char *buf, size_t bytes);
static void zero_write(struct buf *bp, hammer2_inode_t *ip,
				hammer2_chain_t **parentp,
				hammer2_key_t lbase,
				int *errorp);
static void hammer2_write_bp(hammer2_chain_t *chain, struct buf *bp,
				int ioflag, int pblksize, int *errorp,
				int check_algo);

static
int
hammer2_strategy_write(struct vop_strategy_args *ap)
{	
	hammer2_xop_strategy_t *xop;
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

	xop = &hammer2_xop_alloc(ip)->xop_strategy;
	xop->finished = 0;
	xop->bio = bio;
	xop->lbase = bio->bio_offset;
	hammer2_xop_start(&xop->head, hammer2_strategy_xop_write);
	/* asynchronous completion */

	hammer2_lwinprog_wait(pmp, hammer2_flush_pipe);

	return(0);
}

/*
 * Per-node XOP (threaded).  Write the logical buffer to the media.
 */
static
void
hammer2_strategy_xop_write(hammer2_xop_t *arg, int clindex)
{
	hammer2_xop_strategy_t *xop = &arg->xop_strategy;
	hammer2_chain_t *parent;
	hammer2_key_t lbase;
	hammer2_inode_t *ip;
	struct bio *bio;
	struct buf *bp;
	int error;
	int lblksize;
	int pblksize;

	lbase = xop->lbase;
	bio = xop->bio;
	bp = bio->bio_buf;
	ip = xop->head.ip;

	/* hammer2_trans_init(parent->hmp->spmp, HAMMER2_TRANS_BUFCACHE); */

	lblksize = hammer2_calc_logical(ip, bio->bio_offset, &lbase, NULL);
	pblksize = hammer2_calc_physical(ip, lbase);
	parent = hammer2_inode_chain(ip, clindex, HAMMER2_RESOLVE_ALWAYS);
	hammer2_write_file_core(bp, ip, &parent,
				lbase, IO_ASYNC,
				pblksize, &error);
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
		parent = NULL;	/* safety */
	}
	error = hammer2_xop_feed(&xop->head, NULL, clindex, error);

	/*
	 * Race to finish the frontend
	 */
	if (xop->finished)
		return;
	hammer2_mtx_ex(&xop->head.xgrp->mtx2);
	if (xop->finished) {
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		return;
	}

	/*
	 * Async operation has not completed and we now own the lock.
	 * Determine if we can complete the operation by issuing the
	 * frontend collection non-blocking.
	 */
	error = hammer2_xop_collect(&xop->head, HAMMER2_XOP_COLLECT_NOWAIT);

	switch(error) {
	case ENOENT:
	case 0:
		xop->finished = 1;
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		bp->b_resid = 0;
		bp->b_error = 0;
		biodone(bio);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		hammer2_lwinprog_drop(ip->pmp);
		break;
	case EINPROGRESS:
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		break;
	default:
		xop->finished = 1;
		hammer2_mtx_unlock(&xop->head.xgrp->mtx2);
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		biodone(bio);
		hammer2_xop_retire(&xop->head, HAMMER2_XOPMASK_VOP);
		hammer2_lwinprog_drop(ip->pmp);
		break;
	}
}

/*
 * Wait for pending I/O to complete
 */
void
hammer2_bioq_sync(hammer2_pfs_t *pmp)
{
	hammer2_lwinprog_wait(pmp, 0);
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
hammer2_chain_t *
hammer2_assign_physical(hammer2_inode_t *ip, hammer2_chain_t **parentp,
			hammer2_key_t lbase, int pblksize, int *errorp)
{
	hammer2_chain_t *chain;
	hammer2_key_t key_dummy;
	int pradix = hammer2_getradix(pblksize);
	int cache_index = -1;

	/*
	 * Locate the chain associated with lbase, return a locked chain.
	 * However, do not instantiate any data reference (which utilizes a
	 * device buffer) because we will be using direct IO via the
	 * logical buffer cache buffer.
	 */
	*errorp = 0;
	KKASSERT(pblksize >= HAMMER2_ALLOC_MIN);
retry:
	chain = hammer2_chain_lookup(parentp, &key_dummy,
				     lbase, lbase,
				     &cache_index,
				     HAMMER2_LOOKUP_NODATA);
	if (chain == NULL) {
		/*
		 * We found a hole, create a new chain entry.
		 *
		 * NOTE: DATA chains are created without device backing
		 *	 store (nor do we want any).
		 */
		*errorp = hammer2_chain_create(parentp, &chain, ip->pmp,
					       lbase, HAMMER2_PBUFRADIX,
					       HAMMER2_BREF_TYPE_DATA,
					       pblksize, 0);
		if (chain == NULL) {
			panic("hammer2_chain_create: par=%p error=%d\n",
			      *parentp, *errorp);
			goto retry;
		}
		/*ip->delta_dcount += pblksize;*/
	} else {
		switch (chain->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * The data is embedded in the inode, which requires
			 * a bit more finess.
			 */
			hammer2_chain_modify_ip(ip, chain, 0);
			break;
		case HAMMER2_BREF_TYPE_DATA:
			if (chain->bytes != pblksize) {
				hammer2_chain_resize(ip, *parentp, chain,
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
			hammer2_chain_modify(chain, HAMMER2_MODIFY_OPTDATA);
			break;
		default:
			panic("hammer2_assign_physical: bad type");
			/* NOT REACHED */
			break;
		}
	}
	return (chain);
}

/* 
 * hammer2_write_file_core() - hammer2_write_thread() helper
 *
 * The core write function which determines which path to take
 * depending on compression settings.  We also have to locate the
 * related chains so we can calculate and set the check data for
 * the blockref.
 */
static
void
hammer2_write_file_core(struct buf *bp, hammer2_inode_t *ip,
			hammer2_chain_t **parentp,
			hammer2_key_t lbase, int ioflag, int pblksize,
			int *errorp)
{
	hammer2_chain_t *chain;

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
		chain = hammer2_assign_physical(ip, parentp,
					        lbase, pblksize, errorp);
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			hammer2_inode_data_t *wipdata;

			wipdata = &chain->data->ipdata;
			KKASSERT(wipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bcopy(bp->b_data, wipdata->u.data,
			      HAMMER2_EMBEDDED_BYTES);
		} else {
			hammer2_write_bp(chain, bp, ioflag, pblksize,
					 errorp, ip->meta.check_algo);
		}
		if (chain) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
		}
		break;
	case HAMMER2_COMP_AUTOZERO:
		/*
		 * Check for zero-fill only
		 */
		hammer2_zero_check_and_write(bp, ip, parentp,
					     lbase, ioflag,
					     pblksize, errorp,
					     ip->meta.check_algo);
		break;
	case HAMMER2_COMP_LZ4:
	case HAMMER2_COMP_ZLIB:
	default:
		/*
		 * Check for zero-fill and attempt compression.
		 */
		hammer2_compress_and_write(bp, ip, parentp,
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
hammer2_compress_and_write(struct buf *bp, hammer2_inode_t *ip,
	hammer2_chain_t **parentp,
	hammer2_key_t lbase, int ioflag, int pblksize,
	int *errorp, int comp_algo, int check_algo)
{
	hammer2_chain_t *chain;
	int comp_size;
	int comp_block_size;
	char *comp_buffer;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, ip, parentp, lbase, errorp);
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

	chain = hammer2_assign_physical(ip, parentp, lbase,
					comp_block_size, errorp);
	if (*errorp) {
		kprintf("WRITE PATH: An error occurred while "
			"assigning physical space.\n");
		KKASSERT(chain == NULL);
		goto done;
	}

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		hammer2_inode_data_t *wipdata;

		hammer2_chain_modify_ip(ip, chain, 0);
		wipdata = &chain->data->ipdata;
		KKASSERT(wipdata->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA);
		KKASSERT(bp->b_loffset == 0);
		bcopy(bp->b_data, wipdata->u.data, HAMMER2_EMBEDDED_BYTES);
	} else {
		hammer2_io_t *dio;
		char *bdata;

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
	if (chain) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
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
hammer2_zero_check_and_write(struct buf *bp, hammer2_inode_t *ip,
	hammer2_chain_t **parentp,
	hammer2_key_t lbase, int ioflag, int pblksize, int *errorp,
	int check_algo)
{
	hammer2_chain_t *chain;

	if (test_block_zeros(bp->b_data, pblksize)) {
		zero_write(bp, ip, parentp, lbase, errorp);
	} else {
		chain = hammer2_assign_physical(ip, parentp, lbase,
						pblksize, errorp);
		hammer2_write_bp(chain, bp, ioflag, pblksize,
				 errorp, check_algo);
		if (chain) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
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
zero_write(struct buf *bp, hammer2_inode_t *ip,
	   hammer2_chain_t **parentp,
	   hammer2_key_t lbase, int *errorp __unused)
{
	hammer2_chain_t *chain;
	hammer2_key_t key_dummy;
	int cache_index = -1;

	chain = hammer2_chain_lookup(parentp, &key_dummy,
				     lbase, lbase,
				     &cache_index,
				     HAMMER2_LOOKUP_NODATA);
	if (chain) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
			hammer2_inode_data_t *wipdata;

			hammer2_chain_modify_ip(ip, chain, 0);
			wipdata = &chain->data->ipdata;
			KKASSERT(wipdata->meta.op_flags &
				 HAMMER2_OPFLAG_DIRECTDATA);
			KKASSERT(bp->b_loffset == 0);
			bzero(wipdata->u.data, HAMMER2_EMBEDDED_BYTES);
		} else {
			hammer2_chain_delete(*parentp, chain,
					     HAMMER2_DELETE_PERMANENT);
		}
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
	}
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
hammer2_write_bp(hammer2_chain_t *chain, struct buf *bp, int ioflag,
		 int pblksize, int *errorp, int check_algo)
{
	hammer2_inode_data_t *wipdata;
	hammer2_io_t *dio;
	char *bdata;
	int error;

	error = 0;	/* XXX TODO below */

	KKASSERT(chain->flags & HAMMER2_CHAIN_MODIFIED);

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		wipdata = &chain->data->ipdata;
		KKASSERT(wipdata->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA);
		KKASSERT(bp->b_loffset == 0);
		bcopy(bp->b_data, wipdata->u.data, HAMMER2_EMBEDDED_BYTES);
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
	*errorp = error;
}
