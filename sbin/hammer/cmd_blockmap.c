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
 * $DragonFly: src/sbin/hammer/cmd_blockmap.c,v 1.4 2008/07/19 18:48:14 dillon Exp $
 */

#include "hammer.h"

static void dump_blockmap(const char *label, int zone);

void
hammer_cmd_blockmap(void)
{
	dump_blockmap("btree", HAMMER_ZONE_FREEMAP_INDEX);
#if 0
	dump_blockmap("btree", HAMMER_ZONE_BTREE_INDEX);
	dump_blockmap("meta", HAMMER_ZONE_META_INDEX);
	dump_blockmap("large-data", HAMMER_ZONE_LARGE_DATA_INDEX);
	dump_blockmap("small-data", HAMMER_ZONE_SMALL_DATA_INDEX);
#endif
}

#if 1

static
void
dump_blockmap(const char *label, int zone)
{
	struct volume_info *root_volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	struct buffer_info *buffer1 = NULL;
	struct buffer_info *buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t scan1;
	hammer_off_t scan2;

	assert(RootVolNo >= 0);
	root_volume = get_volume(RootVolNo);
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	assert(rootmap->phys_offset != 0);

	printf("zone %-16s next %016llx alloc %016llx\n",
		label, rootmap->next_offset, rootmap->alloc_offset);

	for (scan1 = HAMMER_ZONE_ENCODE(zone, 0);
	     scan1 < HAMMER_ZONE_ENCODE(zone, HAMMER_OFF_LONG_MASK);
	     scan1 += HAMMER_BLOCKMAP_LAYER2) {
		/*
		 * Dive layer 1.
		 */
		layer1_offset = rootmap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(scan1);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL)
			continue;
		printf(" layer1 %016llx @%016llx blocks-free %lld\n",
			scan1, layer1->phys_offset, layer1->blocks_free);
		if (layer1->phys_offset == HAMMER_BLOCKMAP_FREE)
			continue;
		for (scan2 = scan1; 
		     scan2 < scan1 + HAMMER_BLOCKMAP_LAYER2;
		     scan2 += HAMMER_LARGEBLOCK_SIZE
		) {
			/*
			 * Dive layer 2, each entry represents a large-block.
			 */
			layer2_offset = layer1->phys_offset +
					HAMMER_BLOCKMAP_LAYER2_OFFSET(scan2);
			layer2 = get_buffer_data(layer2_offset, &buffer2, 0);
			printf("        %016llx zone=%d app=%-7d free=%-7d\n",
				scan2,
				layer2->zone,
				layer2->append_off,
				layer2->bytes_free);
		}
	}
	if (buffer1)
		rel_buffer(buffer1);
	if (buffer2)
		rel_buffer(buffer2);
	rel_volume(root_volume);
}

#endif
