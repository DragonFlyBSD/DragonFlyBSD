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
 * $DragonFly: src/sbin/hammer/Attic/buffer_alist.c,v 1.1 2007/10/16 18:30:53 dillon Exp $
 */
/*
 * Implement the super-cluster A-list recursion for the cluster allocator.
 *
 * Volume A-list -> supercluster A-list -> cluster
 */

#include "newfs_hammer.h"

static int
buffer_alist_init(void *info, int32_t blk, int32_t radix)
{
	struct cluster_info *cluster = info;
	struct buffer_info *buf;
	int32_t buf_no;

	/*
	 * Calculate the buffer number, initialize based on the buffer type.
	 * The buffer has already been allocated to assert that it has been
	 * initialized.
	 */
	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buf = get_buffer(cluster, buf_no, 0);
	assert(buf->ondisk->head.buf_type != 0);

	return(0);
}

static int
buffer_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	return(0);
}

static int
buffer_alist_alloc_fwd(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct cluster_info *cluster = info;
	struct buffer_info *buf;
	int32_t buf_no;
	int32_t r;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buf = get_buffer(cluster, buf_no, 0);
	assert(buf->ondisk->head.buf_type != 0);

	r = hammer_alist_alloc_fwd(&buf->alist, count, atblk - blk);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	*fullp = hammer_alist_isfull(&buf->alist);
	return(r);
}

static int
buffer_alist_alloc_rev(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct cluster_info *cluster = info;
	struct buffer_info *buf;
	int32_t buf_no;
	int32_t r;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buf = get_buffer(cluster, buf_no, 0);
	assert(buf->ondisk->head.buf_type != 0);

	r = hammer_alist_alloc_rev(&buf->alist, count, atblk - blk);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	*fullp = hammer_alist_isfull(&buf->alist);
	return(r);
}

static void
buffer_alist_free(void *info, int32_t blk, int32_t radix,
		 int32_t base_blk, int32_t count, int32_t *emptyp)
{
	struct cluster_info *cluster = info;
	struct buffer_info *buf;
	int32_t buf_no;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buf = get_buffer(cluster, buf_no, 0);
	assert(buf->ondisk->head.buf_type != 0);
	hammer_alist_free(&buf->alist, base_blk, count);
	*emptyp = hammer_alist_isempty(&buf->alist);
}

static void
buffer_alist_print(void *info, int32_t blk, int32_t radix, int tab)
{
}

void
hammer_buffer_alist_template(hammer_alist_config_t config)
{
        config->bl_radix_init = buffer_alist_init;
        config->bl_radix_destroy = buffer_alist_destroy;
        config->bl_radix_alloc_fwd = buffer_alist_alloc_fwd;
        config->bl_radix_alloc_rev = buffer_alist_alloc_rev;
        config->bl_radix_free = buffer_alist_free;
        config->bl_radix_print = buffer_alist_print;
}

