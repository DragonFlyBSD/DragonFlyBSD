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
 * $DragonFly: src/sbin/hammer/Attic/super_alist.c,v 1.3 2007/11/27 07:44:38 dillon Exp $
 */
/*
 * Implement the super-cluster A-list recursion for the cluster allocator.
 *
 * Volume A-list -> supercluster A-list -> cluster
 */

#include "newfs_hammer.h"

static int
super_alist_init(void *info, int32_t blk, int32_t radix)
{
	struct volume_info *vol = info;
	struct supercl_info *supercl;
	int32_t sclno;

	/*
	 * Calculate the super-cluster number containing the cluster (blk)
	 * and obtain the super-cluster buffer.
	 */
	sclno = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = get_supercl(vol, sclno);
	return(0);
}

static int
super_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	return(0);
}

static int
super_alist_alloc_fwd(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct volume_info *vol = info;
	struct supercl_info *supercl;
	int32_t sclno;
	int32_t r;

	sclno = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = get_supercl(vol, sclno);
	r = hammer_alist_alloc_fwd(&supercl->clu_alist, count, atblk - blk);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	*fullp = hammer_alist_isfull(&supercl->clu_alist);
	return(r);
}

static int
super_alist_alloc_rev(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct volume_info *vol = info;
	struct supercl_info *supercl;
	int32_t sclno;
	int32_t r;

	sclno = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = get_supercl(vol, sclno);
	r = hammer_alist_alloc_rev(&supercl->clu_alist, count, atblk - blk);
	if (r != HAMMER_ALIST_BLOCK_NONE)
		r += blk;
	*fullp = hammer_alist_isfull(&supercl->clu_alist);
	return(r);
}

static void
super_alist_free(void *info, int32_t blk, int32_t radix,
		 int32_t base_blk, int32_t count, int32_t *emptyp)
{
	struct volume_info *vol = info;
	struct supercl_info *supercl;
	int32_t sclno;

	sclno = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = get_supercl(vol, sclno);
	hammer_alist_free(&supercl->clu_alist, base_blk, count);
	*emptyp = hammer_alist_isempty(&supercl->clu_alist);
}

static void
super_alist_print(void *info, int32_t blk, int32_t radix, int tab)
{
}

void
hammer_super_alist_template(hammer_alist_config_t config)
{
        config->bl_radix_init = super_alist_init;
        config->bl_radix_destroy = super_alist_destroy;
        config->bl_radix_alloc_fwd = super_alist_alloc_fwd;
        config->bl_radix_alloc_rev = super_alist_alloc_rev;
        config->bl_radix_free = super_alist_free;
        config->bl_radix_print = super_alist_print;
}

