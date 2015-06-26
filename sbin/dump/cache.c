/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $FreeBSD: src/sbin/dump/cache.c,v 1.1.2.1 2003/01/25 18:54:59 dillon Exp $
 */
/*
 * Block cache for dump
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <vfs/ufs/dir.h>
#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "dump.h"

typedef struct Block {
	struct Block	*b_HNext;	/* must be first field */
	off_t		b_Offset;
	char		*b_Data;
} Block;

#define HFACTOR		4
#define BLKFACTOR	4

static char  *DataBase;
static Block **BlockHash;
static int   BlockSize;
static int   HSize;
static int   NBlocks;

static void
cinit(void)
{
	int i;
	int hi;
	Block *base;

	if ((BlockSize = sblock->fs_bsize * BLKFACTOR) > MAXBSIZE)
		BlockSize = MAXBSIZE;
	NBlocks = cachesize / BlockSize;
	HSize = NBlocks / HFACTOR;

	msg("Cache %d MB, blocksize = %d\n", 
	    NBlocks * BlockSize / (1024 * 1024), BlockSize);

	base = calloc(sizeof(Block), NBlocks);
	BlockHash = calloc(sizeof(Block *), HSize);
	DataBase = mmap(NULL, NBlocks * BlockSize, 
			PROT_READ|PROT_WRITE, MAP_ANON, -1, 0);
	for (i = 0; i < NBlocks; ++i) {
		base[i].b_Data = DataBase + i * BlockSize;
		base[i].b_Offset = (off_t)-1;
		hi = i / HFACTOR;
		base[i].b_HNext = BlockHash[hi];
		BlockHash[hi] = &base[i];
	}
}

ssize_t
cread(int fd, void *buf, size_t nbytes, off_t offset)
{
	Block *blk;
	Block **pblk;
	Block **ppblk;
	int hi;
	int n;
	off_t mask;

	/*
	 * If the cache is disabled, or we do not yet know the filesystem
	 * block size, then revert to pread.  Otherwise initialize the
	 * cache as necessary and continue.
	 */
	if (cachesize <= 0 || sblock->fs_bsize == 0)
		return(pread(fd, buf, nbytes, offset));
	if (DataBase == NULL)
		cinit();

	/*
	 * If the request crosses a cache block boundary, or the
	 * request is larger or equal to the cache block size,
	 * revert to pread().  Full-block-reads are typically
	 * one-time calls and caching would be detrimental.
	 */
	mask = ~(off_t)(BlockSize - 1);
	if (nbytes >= (unsigned)BlockSize ||
	    ((offset ^ (offset + nbytes - 1)) & mask) != 0) {
		return(pread(fd, buf, nbytes, offset));
	}

	/*
	 * Obtain and access the cache block.  Cache a successful
	 * result.  If an error occurs, revert to pread() (this might
	 * occur near the end of the media).
	 */
	hi = (offset / BlockSize) % HSize;
	pblk = &BlockHash[hi];
	ppblk = NULL;
	while ((blk = *pblk) != NULL) {
		if (((blk->b_Offset ^ offset) & mask) == 0)
			break;
		ppblk = pblk;
		pblk = &blk->b_HNext;
	}
	if (blk == NULL) {
		blk = *ppblk;
		pblk = ppblk;
		blk->b_Offset = offset & mask;
		n = pread(fd, blk->b_Data, BlockSize, blk->b_Offset);
		if (n != BlockSize) {
			blk->b_Offset = (off_t)-1;
			blk = NULL;
		}
	}
	if (blk) {
		bcopy(blk->b_Data + (offset - blk->b_Offset), buf, nbytes);
		*pblk = blk->b_HNext;
		blk->b_HNext = BlockHash[hi];
		BlockHash[hi] = blk;
		return(nbytes);
	} else {
		return(pread(fd, buf, nbytes, offset));
	}
}

