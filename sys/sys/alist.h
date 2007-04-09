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
 * $DragonFly: src/sys/sys/alist.h,v 1.1 2007/04/09 17:09:58 dillon Exp $
 */
/*
 * Implements a power-of-2 aligned and sized resource bitmap.  The
 * number of blocks need not be a power-of-2, but all allocations must
 * be a power of 2 and will be aligned on return.
 *
 * alist = alist_create(blocks, malloctype)
 * (void)  alist_destroy(alist, malloctype)
 * blkno = alist_alloc(alist, count)
 * (void)  alist_free(alist, blkno, count)
 *
 * A radix tree is constructed using radix 16 for the meta nodes and radix 32
 * for the leaf nodes.  Each meta node has a 32 bit bitmap using 2 bits
 * per radix (32 bits), and each leaf node has a 32 bit bitmap using 1 bit
 * per radix.  The 2-bit bit patterns in the meta-node indicate how the
 * sub-tree is allocated.
 *
 *	00	All-allocated
 *	01	Partially allocated
 *	10	(reserved)
 *	11	All-free
 *
 * Each meta-node and leaf has a biggest-hint field indicating the largest
 * possible allocation that can be made in that sub-tree.  The value in
 * this field may be too large but will never be too small.
 */

#ifndef _SYS_ALIST_H_
#define _SYS_ALIST_H_

#ifndef _SYS_TYPES_H_ 
#include <sys/types.h>
#endif

/*
 * blmeta and bl_bitmap_t MUST be a power of 2 in size.
 */

typedef struct almeta {
	u_daddr_t	bm_bitmap;	/* bitmap if we are a leaf	*/
	daddr_t		bm_bighint;	/* biggest contiguous block hint*/
} almeta_t;

typedef struct alist {
	daddr_t		bl_blocks;	/* area of coverage		*/
	int		bl_radix;	/* coverage radix		*/
	daddr_t		bl_skip;	/* starting skip		*/
	daddr_t		bl_free;	/* number of free blocks	*/
	almeta_t	*bl_root;	/* root of radix tree		*/
	daddr_t		bl_rootblks;	/* daddr_t blks allocated for tree */
} *alist_t;

#define ALIST_META_RADIX	(sizeof(u_daddr_t)*4)	/* 16 */
#define ALIST_BMAP_RADIX	(sizeof(u_daddr_t)*8)	/* 32 */
#define ALIST_BLOCK_NONE	((daddr_t)-1)

extern alist_t alist_create(daddr_t blocks, struct malloc_type *mtype);
extern void alist_destroy(alist_t blist, struct malloc_type *mtype);
extern daddr_t alist_alloc(alist_t blist, daddr_t count);
extern void alist_free(alist_t blist, daddr_t blkno, daddr_t count);
extern void alist_print(alist_t blist);

#endif	/* _SYS_ALIST_H_ */

