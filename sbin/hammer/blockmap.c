/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/blockmap.c,v 1.1 2008/02/23 03:01:06 dillon Exp $
 */

#include "hammer.h"

hammer_off_t
blockmap_lookup(hammer_off_t bmap_off,
		struct hammer_blockmap_layer1 *save_layer1,
		struct hammer_blockmap_layer2 *save_layer2)
{
	struct volume_info *root_volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	struct buffer_info *buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t result_offset;
	int zone;

	zone = HAMMER_ZONE_DECODE(bmap_off);
	assert(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	assert(RootVolNo >= 0);
	root_volume = get_volume(RootVolNo);
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	assert(rootmap->phys_offset != 0);
	assert(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	assert(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);

	if (bmap_off >= rootmap->alloc_offset) {
		panic("hammer_blockmap_lookup: %016llx beyond EOF %016llx",
		      bmap_off, rootmap->alloc_offset);
		result_offset = 0;
		goto done;
	}

	/*
	 * Dive layer 1.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(bmap_off);
	layer1 = get_buffer_data(layer1_offset, &buffer, 0);
	assert(layer1->phys_offset);
	if (save_layer1)
		*save_layer1 = *layer1;

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = get_buffer_data(layer2_offset, &buffer, 0);
	if (save_layer2)
		*save_layer2 = *layer2;

	assert(layer2->u.phys_offset);

	result_offset = layer2->u.phys_offset +
			(bmap_off & HAMMER_LARGEBLOCK_MASK64);
done:
	if (buffer)
		rel_buffer(buffer);
	rel_volume(root_volume);
	return(result_offset);
}

