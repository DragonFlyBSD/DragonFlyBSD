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

/*
 * Each collect covers 1<<(19+23) bytes address space of layer 1.
 * (plus a copy of 1<<23 bytes that holds layer2 entries in layer 1).
 */
typedef struct collect {
	RB_ENTRY(collect) entry;
	hammer_off_t	phys_offset;  /* layer2 address pointed by layer1 */
	hammer_off_t	*offsets;  /* big-block offset for layer2[i] */
	struct hammer_blockmap_layer2 *track2;  /* track of layer2 entries */
	struct hammer_blockmap_layer2 *layer2;  /* 1<<19 x 16 bytes entries */
	int error;  /* # of inconsistencies */
} *collect_t;

static int
collect_compare(struct collect *c1, struct collect *c2)
{
	if (c1->phys_offset < c2->phys_offset)
		return(-1);
	if (c1->phys_offset > c2->phys_offset)
		return(1);
	return(0);
}

RB_HEAD(collect_rb_tree, collect) CollectTree = RB_INITIALIZER(&CollectTree);
RB_PROTOTYPE2(collect_rb_tree, collect, entry, collect_compare, hammer_off_t);
RB_GENERATE2(collect_rb_tree, collect, entry, collect_compare, hammer_off_t,
	phys_offset);

static void dump_blockmap(const char *label, int zone);
static void check_freemap(hammer_blockmap_t freemap);
static void check_btree_node(hammer_off_t node_offset, int depth);
static void check_undo(hammer_blockmap_t undomap);
static __inline void collect_btree_root(hammer_off_t node_offset);
static __inline void collect_btree_internal(hammer_btree_elm_t elm);
static __inline void collect_btree_leaf(hammer_btree_elm_t elm);
static __inline void collect_freemap_layer1(hammer_blockmap_t freemap);
static __inline void collect_freemap_layer2(struct hammer_blockmap_layer1 *layer1);
static __inline void collect_undo(hammer_off_t scan_offset,
	hammer_fifo_head_t head);
static void collect_blockmap(hammer_off_t offset, int32_t length, int zone);
static struct hammer_blockmap_layer2 *collect_get_track(
	collect_t collect, hammer_off_t offset, int zone,
	struct hammer_blockmap_layer2 *layer2);
static collect_t collect_get(hammer_off_t phys_offset);
static void dump_collect_table(void);
static void dump_collect(collect_t collect, struct zone_stat *stats);

static int num_bad_layer1 = 0;
static int num_bad_layer2 = 0;
static int num_bad_node = 0;

void
hammer_cmd_blockmap(void)
{
	dump_blockmap("freemap", HAMMER_ZONE_FREEMAP_INDEX);
}

static
void
dump_blockmap(const char *label, int zone)
{
	struct volume_info *root_volume;
	hammer_blockmap_t rootmap;
	hammer_blockmap_t blockmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	struct buffer_info *buffer1 = NULL;
	struct buffer_info *buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t phys_offset;
	hammer_off_t block_offset;
	struct zone_stat *stats = NULL;
	int xerr, aerr, ferr;
	int i;

	root_volume = get_root_volume();
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	assert(rootmap->phys_offset != 0);

	printf("                   "
	       "phys             first            next             alloc\n");
	for (i = 0; i < HAMMER_MAX_ZONES; i++) {
		blockmap = &root_volume->ondisk->vol0_blockmap[i];
		if (VerboseOpt || i == zone) {
			printf("zone %-2d %-10s %016jx %016jx %016jx %016jx\n",
				i, (i == zone ? label : ""),
				(uintmax_t)blockmap->phys_offset,
				(uintmax_t)blockmap->first_offset,
				(uintmax_t)blockmap->next_offset,
				(uintmax_t)blockmap->alloc_offset);
		}
	}

	if (VerboseOpt)
		stats = hammer_init_zone_stat();

	for (phys_offset = HAMMER_ZONE_ENCODE(zone, 0);
	     phys_offset < HAMMER_ZONE_ENCODE(zone, HAMMER_OFF_LONG_MASK);
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		/*
		 * Dive layer 1.
		 */
		layer1_offset = rootmap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = get_buffer_data(layer1_offset, &buffer1, 0);

		xerr = ' ';  /* good */
		if (layer1->layer1_crc !=
		    crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
			xerr = 'B';
			++num_bad_layer1;
		}
		if (xerr == ' ' &&
		    layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			continue;
		}
		printf("%c layer1 %016jx @%016jx blocks-free %jd\n",
			xerr,
			(uintmax_t)phys_offset,
			(uintmax_t)layer1->phys_offset,
			(intmax_t)layer1->blocks_free);

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			hammer_off_t zone_offset = phys_offset + block_offset;
			/*
			 * Dive layer 2, each entry represents a big-block.
			 */
			layer2_offset = layer1->phys_offset +
					HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = get_buffer_data(layer2_offset, &buffer2, 0);

			xerr = aerr = ferr = ' ';  /* good */
			if (layer2->entry_crc !=
			    crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
				xerr = 'B';
				++num_bad_layer2;
			}
			if (layer2->append_off > HAMMER_BIGBLOCK_SIZE) {
				aerr = 'A';
				++num_bad_layer2;
			}
			if (layer2->bytes_free < 0 ||
			    layer2->bytes_free > HAMMER_BIGBLOCK_SIZE) {
				ferr = 'F';
				++num_bad_layer2;
			}

			if (VerboseOpt < 2 &&
			    xerr == ' ' && aerr == ' ' && ferr == ' ' &&
			    layer2->zone == HAMMER_ZONE_UNAVAIL_INDEX) {
				break;
			}
			printf("%c%c%c     %016jx zone=%-2d ",
				xerr, aerr, ferr, (uintmax_t)zone_offset, layer2->zone);
			if (VerboseOpt) {
				printf("vol=%-3d L1#=%-6d L2#=%-6d L1=%-7lu L2=%-7lu ",
					HAMMER_VOL_DECODE(zone_offset),
					HAMMER_BLOCKMAP_LAYER1_INDEX(zone_offset),
					HAMMER_BLOCKMAP_LAYER2_INDEX(zone_offset),
					HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset),
					HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset));
			}
			printf("app=%-7d free=%-7d",
				layer2->append_off,
				layer2->bytes_free);
			if (VerboseOpt) {
				double bytes_used;
				if (layer2->zone != HAMMER_ZONE_UNAVAIL_INDEX) {
					bytes_used = HAMMER_BIGBLOCK_SIZE -
						layer2->bytes_free;
				} else {
					/* UNAVAIL zone has 0 for bytes_free */
					bytes_used = 0;
				}
				printf(" fill=%-5.1lf crc=%08x-%08x\n",
					bytes_used * 100 / HAMMER_BIGBLOCK_SIZE,
					layer1->layer1_crc,
					layer2->entry_crc);
			} else {
				printf("\n");
			}

			if (VerboseOpt)
				hammer_add_zone_stat_layer2(stats, layer2);
		}
	}
	rel_buffer(buffer1);
	rel_buffer(buffer2);
	rel_volume(root_volume);

	if (VerboseOpt) {
		hammer_print_zone_stat(stats);
		hammer_cleanup_zone_stat(stats);
	}

	if (num_bad_layer1 || VerboseOpt) {
		printf("%d bad layer1\n", num_bad_layer1);
	}
	if (num_bad_layer2 || VerboseOpt) {
		printf("%d bad layer2\n", num_bad_layer1);
	}
}

void
hammer_cmd_checkmap(void)
{
	struct volume_info *volume;
	hammer_blockmap_t freemap;
	hammer_blockmap_t undomap;
	hammer_off_t node_offset;

	volume = get_root_volume();
	node_offset = volume->ondisk->vol0_btree_root;
	freemap = &volume->ondisk->vol0_blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	undomap = &volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];

	if (QuietOpt < 3) {
		printf("Volume header\tnext_tid=%016jx\n",
		       (uintmax_t)volume->ondisk->vol0_next_tid);
		printf("\t\tbufoffset=%016jx\n",
		       (uintmax_t)volume->ondisk->vol_buf_beg);
		printf("\t\tundosize=%jdMB\n",
		       (intmax_t)((undomap->alloc_offset & HAMMER_OFF_LONG_MASK)
			/ (1024 * 1024)));
	}
	rel_volume(volume);

	AssertOnFailure = (DebugOpt != 0);

	printf("Collecting allocation info from freemap: ");
	fflush(stdout);
	check_freemap(freemap);
	printf("done\n");

	printf("Collecting allocation info from B-Tree: ");
	fflush(stdout);
	check_btree_node(node_offset, 0);
	printf("done\n");

	printf("Collecting allocation info from UNDO: ");
	fflush(stdout);
	check_undo(undomap);
	printf("done\n");

	dump_collect_table();
}

static void
check_freemap(hammer_blockmap_t freemap)
{
	hammer_off_t offset;
	struct buffer_info *buffer1 = NULL;
	struct hammer_blockmap_layer1 *layer1;
	int i;

	collect_freemap_layer1(freemap);

	for (i = 0; i < HAMMER_BLOCKMAP_RADIX1; ++i) {
		offset = freemap->phys_offset + i * sizeof(*layer1);
		layer1 = get_buffer_data(offset, &buffer1, 0);
		if (layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL)
			collect_freemap_layer2(layer1);
	}
	rel_buffer(buffer1);
}

static void
check_btree_node(hammer_off_t node_offset, int depth)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;
	char badc = ' ';  /* good */
	char badm = ' ';  /* good */

	if (depth == 0)
		collect_btree_root(node_offset);
	node = get_node(node_offset, &buffer);

	if (node == NULL) {
		badc = 'B';
		badm = 'I';
	} else if (crc32(&node->crc + 1, HAMMER_BTREE_CRCSIZE) != node->crc) {
		badc = 'B';
	}

	if (badm != ' ' || badc != ' ') {  /* not good */
		++num_bad_node;
		printf("%c%c   NODE %016jx ",
			badc, badm, (uintmax_t)node_offset);
		if (node == NULL) {
			printf("(IO ERROR)\n");
			rel_buffer(buffer);
			return;
		} else {
			printf("cnt=%02d p=%016jx type=%c depth=%d mirror=%016jx\n",
			       node->count,
			       (uintmax_t)node->parent,
			       (node->type ? node->type : '?'),
			       depth,
			       (uintmax_t)node->mirror_tid);
		}
	}

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];

		switch(node->type) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (elm->internal.subtree_offset) {
				collect_btree_internal(elm);
				check_btree_node(elm->internal.subtree_offset,
						 depth + 1);
			}
			break;
		case HAMMER_BTREE_TYPE_LEAF:
			if (elm->leaf.data_offset)
				collect_btree_leaf(elm);
			break;
		default:
			assert(!AssertOnFailure);
			break;
		}
	}
	rel_buffer(buffer);
}

static void
check_undo(hammer_blockmap_t undomap)
{
	struct buffer_info *buffer = NULL;
	hammer_off_t scan_offset;
	hammer_fifo_head_t head;

	scan_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
	while (scan_offset < undomap->alloc_offset) {
		head = get_buffer_data(scan_offset, &buffer, 0);
		switch (head->hdr_type) {
		case HAMMER_HEAD_TYPE_PAD:
		case HAMMER_HEAD_TYPE_DUMMY:
		case HAMMER_HEAD_TYPE_UNDO:
		case HAMMER_HEAD_TYPE_REDO:
			collect_undo(scan_offset, head);
			break;
		default:
			assert(!AssertOnFailure);
			break;
		}
		if ((head->hdr_size & HAMMER_HEAD_ALIGN_MASK) ||
		     head->hdr_size == 0 ||
		     head->hdr_size > HAMMER_UNDO_ALIGN -
			((u_int)scan_offset & HAMMER_UNDO_MASK)) {
			printf("Illegal size, skipping to next boundary\n");
			scan_offset = (scan_offset + HAMMER_UNDO_MASK) &
					~HAMMER_UNDO_MASK64;
		} else {
			scan_offset += head->hdr_size;
		}
	}
	rel_buffer(buffer);
}

static __inline
void
collect_freemap_layer1(hammer_blockmap_t freemap)
{
	/*
	 * This translation is necessary to do checkmap properly
	 * as zone4 is really just zone2 address space.
	 */
	hammer_off_t zone4_offset = hammer_xlate_to_zoneX(
		HAMMER_ZONE_FREEMAP_INDEX, freemap->phys_offset);
	collect_blockmap(zone4_offset, HAMMER_BIGBLOCK_SIZE,
		HAMMER_ZONE_FREEMAP_INDEX);
}

static __inline
void
collect_freemap_layer2(struct hammer_blockmap_layer1 *layer1)
{
	/*
	 * This translation is necessary to do checkmap properly
	 * as zone4 is really just zone2 address space.
	 */
	hammer_off_t zone4_offset = hammer_xlate_to_zoneX(
		HAMMER_ZONE_FREEMAP_INDEX, layer1->phys_offset);
	collect_blockmap(zone4_offset, HAMMER_BIGBLOCK_SIZE,
		HAMMER_ZONE_FREEMAP_INDEX);
}

static __inline
void
collect_btree_root(hammer_off_t node_offset)
{
	collect_blockmap(node_offset,
		sizeof(struct hammer_node_ondisk),  /* 4KB */
		HAMMER_ZONE_BTREE_INDEX);
}

static __inline
void
collect_btree_internal(hammer_btree_elm_t elm)
{
	collect_blockmap(elm->internal.subtree_offset,
		sizeof(struct hammer_node_ondisk),  /* 4KB */
		HAMMER_ZONE_BTREE_INDEX);
}

static __inline
void
collect_btree_leaf(hammer_btree_elm_t elm)
{
	int zone;

	switch (elm->base.rec_type) {
	case HAMMER_RECTYPE_INODE:
	case HAMMER_RECTYPE_DIRENTRY:
	case HAMMER_RECTYPE_EXT:
	case HAMMER_RECTYPE_FIX:
	case HAMMER_RECTYPE_PFS:
	case HAMMER_RECTYPE_SNAPSHOT:
	case HAMMER_RECTYPE_CONFIG:
		zone = HAMMER_ZONE_META_INDEX;
		break;
	case HAMMER_RECTYPE_DATA:
	case HAMMER_RECTYPE_DB:
		zone = hammer_data_zone_index(elm->leaf.data_len);
		break;
	default:
		zone = HAMMER_ZONE_UNAVAIL_INDEX;
		break;
	}
	collect_blockmap(elm->leaf.data_offset,
		(elm->leaf.data_len + 15) & ~15, zone);
}

static __inline
void
collect_undo(hammer_off_t scan_offset, hammer_fifo_head_t head)
{
	collect_blockmap(scan_offset, head->hdr_size,
		HAMMER_ZONE_UNDO_INDEX);
}

static
void
collect_blockmap(hammer_off_t offset, int32_t length, int zone)
{
	struct hammer_blockmap_layer1 layer1;
	struct hammer_blockmap_layer2 layer2;
	struct hammer_blockmap_layer2 *track2;
	hammer_off_t result_offset;
	collect_t collect;
	int error;

	result_offset = blockmap_lookup(offset, &layer1, &layer2, &error);
	if (AssertOnFailure) {
		assert(HAMMER_ZONE_DECODE(offset) == zone);
		assert(HAMMER_ZONE_DECODE(result_offset) ==
			HAMMER_ZONE_RAW_BUFFER_INDEX);
		assert(error == 0);
	}
	collect = collect_get(layer1.phys_offset); /* layer2 address */
	track2 = collect_get_track(collect, result_offset, zone, &layer2);
	track2->bytes_free -= length;
}

static
collect_t
collect_get(hammer_off_t phys_offset)
{
	collect_t collect;

	collect = RB_LOOKUP(collect_rb_tree, &CollectTree, phys_offset);
	if (collect)
		return(collect);

	collect = calloc(sizeof(*collect), 1);
	collect->track2 = malloc(HAMMER_BIGBLOCK_SIZE);  /* 1<<23 bytes */
	collect->layer2 = malloc(HAMMER_BIGBLOCK_SIZE);  /* 1<<23 bytes */
	collect->offsets = malloc(sizeof(hammer_off_t) * HAMMER_BLOCKMAP_RADIX2);
	collect->phys_offset = phys_offset;
	RB_INSERT(collect_rb_tree, &CollectTree, collect);
	bzero(collect->track2, HAMMER_BIGBLOCK_SIZE);
	bzero(collect->layer2, HAMMER_BIGBLOCK_SIZE);

	return (collect);
}

static
void
collect_rel(collect_t collect)
{
	free(collect->offsets);
	free(collect->layer2);
	free(collect->track2);
	free(collect);
}

static
struct hammer_blockmap_layer2 *
collect_get_track(collect_t collect, hammer_off_t offset, int zone,
		  struct hammer_blockmap_layer2 *layer2)
{
	struct hammer_blockmap_layer2 *track2;
	size_t i;

	i = HAMMER_BLOCKMAP_LAYER2_INDEX(offset);
	track2 = &collect->track2[i];
	if (track2->entry_crc == 0) {
		collect->layer2[i] = *layer2;
		collect->offsets[i] = offset & ~HAMMER_BIGBLOCK_MASK64;
		track2->zone = zone;
		track2->bytes_free = HAMMER_BIGBLOCK_SIZE;
		track2->entry_crc = 1;	/* steal field to tag track load */
	}
	return (track2);
}

static
void
dump_collect_table(void)
{
	collect_t collect;
	int error = 0;
	struct zone_stat *stats = NULL;

	if (VerboseOpt)
		stats = hammer_init_zone_stat();

	RB_FOREACH(collect, collect_rb_tree, &CollectTree) {
		dump_collect(collect, stats);
		error += collect->error;
	}

	while ((collect = RB_ROOT(&CollectTree)) != NULL) {
		RB_REMOVE(collect_rb_tree, &CollectTree, collect);
		collect_rel(collect);
	}
	assert(RB_EMPTY(&CollectTree));

	if (VerboseOpt) {
		hammer_print_zone_stat(stats);
		hammer_cleanup_zone_stat(stats);
	}

	if (num_bad_node || VerboseOpt) {
		printf("%d bad nodes\n", num_bad_node);
	}
	if (error || VerboseOpt) {
		printf("%d errors\n", error);
	}
}

static
void
dump_collect(collect_t collect, struct zone_stat *stats)
{
	struct hammer_blockmap_layer2 *track2;
	struct hammer_blockmap_layer2 *layer2;
	hammer_off_t offset;
	int i, zone;

	for (i = 0; i < HAMMER_BLOCKMAP_RADIX2; ++i) {
		track2 = &collect->track2[i];
		layer2 = &collect->layer2[i];
		offset = collect->offsets[i];

		/*
		 * Check big-blocks referenced by freemap, data,
		 * B-Tree nodes and UNDO fifo.
		 */
		if (track2->entry_crc == 0)
			continue;

		zone = layer2->zone;
		if (AssertOnFailure) {
			assert((zone == HAMMER_ZONE_UNDO_INDEX) ||
				(zone == HAMMER_ZONE_FREEMAP_INDEX) ||
				hammer_is_zone2_mapped_index(zone));
		}
		if (VerboseOpt)
			hammer_add_zone_stat_layer2(stats, layer2);

		if (track2->zone != layer2->zone) {
			printf("BZ\tblock=%016jx calc zone=%-2d, got zone=%-2d\n",
				(intmax_t)offset,
				track2->zone,
				layer2->zone);
			collect->error++;
		} else if (track2->bytes_free != layer2->bytes_free) {
			printf("BM\tblock=%016jx zone=%-2d calc %d free, got %d\n",
				(intmax_t)offset,
				layer2->zone,
				track2->bytes_free,
				layer2->bytes_free);
			collect->error++;
		} else if (VerboseOpt) {
			printf("\tblock=%016jx zone=%-2d %d free (correct)\n",
				(intmax_t)offset,
				layer2->zone,
				track2->bytes_free);
		}
	}
}
