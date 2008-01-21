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
 * $DragonFly: src/sys/vfs/hammer/Attic/hammer_alist.h,v 1.6 2008/01/21 00:00:19 dillon Exp $
 */

/*
 * The structures below represent HAMMER's on-disk and in-memory A-List
 * support.  An A-list is a radix tree bitmap allocator with hinting.  It
 * is very fast and efficient.
 *
 * A-lists can be single-layered or multi-layered.  A single-layered
 * A-list uses a radix 32 leaf and radix 16 internal nodes.  A multi-layered
 * A-list uses a radix 16 leaf and radix 16 internal nodes.
 *
 * Multi-layered A-lists allow otherwise independant A-lists to be stacked
 * to form a single A-list.
 */

/*
 * This on-disk structure represents the actual information an A-list needs
 * to store to disk.  Each A-list layer is made up of a linear array of
 * meta-elements.  The number of elements needed is calculated using a
 * recursion.  A-lists contain an early-termination feature which allows
 * an A-list's storage to scale to the actual number of blocks that need
 * to be managed regardless of whether they are on a radix boundary or not.
 *
 * The first almeta structure in the array is used for housekeeping and not
 * part of the topology proper.  The second almeta structure is the 'root'
 * meta.
 */
typedef struct hammer_almeta {
	u_int32_t	bm_bitmap;
	int32_t		bm_bighint;
} *hammer_almeta_t;

#define bm_alist_freeblks	bm_bitmap	/* housekeeping */
#define bm_alist_base_freeblks	bm_bighint	/* housekeeping */

#define HAMMER_ALMETA_SIZE	8

enum hammer_alloc_state {
	HAMMER_ASTATE_NONE,
	HAMMER_ASTATE_ALLOC,
	HAMMER_ASTATE_FREE
};

typedef enum hammer_alloc_state hammer_alloc_state_t;

/*
 * This in-memory structure specifies how an a-list is configured and
 * can be shared by multiple live alists.
 */
typedef struct hammer_alist_config {
	int32_t	bl_blocks;	/* area of coverage */
	int32_t	bl_radix;	/* coverage radix */
	int32_t bl_base_radix;	/* chain to other allocators */
	int32_t	bl_skip;	/* starting skip for linear layout */
	int32_t bl_rootblks;	/* meta-blocks allocated for tree */
	int32_t bl_terminal;	/* terminal alist, else layer recursion */
	int	(*bl_radix_init)(void *info, int32_t blk, int32_t radix,
					hammer_alloc_state_t state);
	int32_t	(*bl_radix_recover)(void *info, int32_t blk, int32_t radix,
					int32_t count);
	int32_t	(*bl_radix_find)(void *info, int32_t blk, int32_t radix,
					int32_t atblk);
	int	(*bl_radix_destroy)(void *info, int32_t blk, int32_t radix);
	int32_t (*bl_radix_alloc_fwd)(void *info, int32_t blk, int32_t radix,
					int32_t count, int32_t atblk,
					int32_t *fullp);
	int32_t (*bl_radix_alloc_rev)(void *info, int32_t blk, int32_t radix,
					int32_t count, int32_t atblk,
					int32_t *fullp);
	void	(*bl_radix_free)(void *info, int32_t blk, int32_t radix,
					int32_t base_blk, int32_t count,
					int32_t *emptyp);
	void	(*bl_radix_print)(void *info, int32_t blk, int32_t radix,
					int tab);
} *hammer_alist_config_t;

/*
 * This in-memory structure is needed for each live alist.
 */
typedef struct hammer_alist_live {
	hammer_alist_config_t config;	/* a-list configuration */
	hammer_almeta_t meta;		/* location of meta array */
	void *info;			/* chaining call info argument */
} *hammer_alist_t;

/*
 * In-memory structure used to track A-list recovery operations.
 */
typedef struct hammer_alist_recover {
	hammer_alist_t live;
	int	error;
} *hammer_alist_recover_t;


#define HAMMER_ALIST_META_RADIX	(sizeof(int32_t) * 4)   /* 16 */
#define HAMMER_ALIST_BMAP_RADIX	(sizeof(int32_t) * 8)   /* 32 */
#define HAMMER_ALIST_BLOCK_NONE	((int32_t)-1)
#define HAMMER_ALIST_BLOCK_MAX	((int32_t)0x7fffffff)

/*
 * Hard-code some pre-calculated constants for managing varying numbers
 * of blocks.  These are the number of meta-elements required.
 */
#define HAMMER_ALIST_METAELMS_256_1LYR	11
#define HAMMER_ALIST_METAELMS_32K_1LYR	1095
#define HAMMER_ALIST_METAELMS_16K_2LYR	HAMMER_ALIST_METAELMS_32K_1LYR

#define HAMMER_ALIST_METAELMS_4K_1LYR	139
#define HAMMER_ALIST_METAELMS_4K_2LYR	275

/*
 * Function library support available to kernel and userland
 */
void hammer_alist_template(hammer_alist_config_t bl, int32_t blocks,
                           int32_t base_radix, int32_t maxmeta);
void hammer_alist_init(hammer_alist_t live, int32_t start, int32_t count,
			   hammer_alloc_state_t state);
int32_t hammer_alist_recover(hammer_alist_t live, int32_t blk,
			   int32_t start, int32_t count);
int32_t hammer_alist_alloc(hammer_alist_t live, int32_t count);
int32_t hammer_alist_alloc_fwd(hammer_alist_t live,
			   int32_t count, int32_t atblk);
int32_t hammer_alist_alloc_rev(hammer_alist_t live,
			   int32_t count, int32_t atblk);
int32_t hammer_alist_find(hammer_alist_t live, int32_t atblk, int32_t maxblk);
int hammer_alist_isfull(hammer_alist_t live);
int hammer_alist_isempty(hammer_alist_t live);
void hammer_alist_free(hammer_alist_t live, int32_t blkno, int32_t count);

