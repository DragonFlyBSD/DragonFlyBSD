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
 * $DragonFly: src/sbin/hammer/cmd_show.c,v 1.10 2008/05/13 04:58:10 dillon Exp $
 */

#include "hammer.h"

#define FLAG_TOOFARLEFT		0x0001
#define FLAG_TOOFARRIGHT	0x0002
#define FLAG_BADTYPE		0x0004
#define FLAG_BADCHILDPARENT	0x0008

static void print_btree_node(hammer_off_t node_offset, int depth, int spike,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);
static void print_record(hammer_btree_elm_t elm);
static void print_btree_elm(hammer_btree_elm_t elm, int i, u_int8_t type,
			int flags, const char *label);
static int print_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
			hammer_btree_elm_t elm, u_int8_t btype,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);
static void print_bigblock_fill(hammer_off_t offset);

void
hammer_cmd_show(hammer_off_t node_offset, int depth,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	struct volume_info *volume;

	if (node_offset == (hammer_off_t)-1) {
		volume = get_volume(RootVolNo);
		node_offset = volume->ondisk->vol0_btree_root;
		if (VerboseOpt) {
			printf("\trecords=%lld\n",
			       volume->ondisk->vol0_stat_records);
		}
		rel_volume(volume);
	}
	printf("show %016llx depth %d\n", node_offset, depth);
	print_btree_node(node_offset, depth, 0, left_bound, right_bound);
	print_btree_node(node_offset, depth, 1, left_bound, right_bound);
}

static void
print_btree_node(hammer_off_t node_offset, int depth, int spike,
		 hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;
	int flags;

	node = get_node(node_offset, &buffer);

	if (spike == 0) {
		printf("    NODE %016llx count=%d parent=%016llx "
		       "type=%c depth=%d fill=",
		       node_offset, node->count, node->parent,
		       (node->type ? node->type : '?'), depth);
		if (VerboseOpt)
			print_bigblock_fill(node_offset);
		printf(" {\n");

		for (i = 0; i < node->count; ++i) {
			elm = &node->elms[i];
			flags = print_elm_flags(node, node_offset,
						elm, elm->base.btype,
						left_bound, right_bound);
			print_btree_elm(elm, i, node->type, flags, "ELM");
		}
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			elm = &node->elms[i];
			flags = print_elm_flags(node, node_offset,
						elm, 'I',
						left_bound, right_bound);
			print_btree_elm(elm, i, node->type, flags, "RBN");
		}
		printf("    }\n");
	}

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];

		switch(node->type) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (elm->internal.subtree_offset) {
				print_btree_node(elm->internal.subtree_offset,
						 depth + 1, spike,
						 &elm[0].base, &elm[1].base);
			}
			break;
		default:
			break;
		}
	}
	rel_buffer(buffer);
}

static
void
print_btree_elm(hammer_btree_elm_t elm, int i, u_int8_t type,
		int flags, const char *label)
{
	char flagstr[8] = { 0, '-', '-', '-', '-', '-', '-', 0 };

	flagstr[0] = flags ? 'B' : 'G';
	if (flags & FLAG_TOOFARLEFT)
		flagstr[2] = 'L';
	if (flags & FLAG_TOOFARRIGHT)
		flagstr[3] = 'R';
	if (flags & FLAG_BADTYPE)
		flagstr[4] = 'T';
	if (flags & FLAG_BADCHILDPARENT)
		flagstr[4] = 'C';

	printf("%s\t%s %2d %c ",
	       flagstr, label, i,
	       (elm->base.btype ? elm->base.btype : '?'));
	printf("obj=%016llx key=%016llx rt=%02x ot=%02x\n",
	       elm->base.obj_id,
	       elm->base.key,
	       elm->base.rec_type,
	       elm->base.obj_type);
	printf("\t       %c tids %016llx:%016llx ",
		(elm->base.delete_tid ? 'd' : ' '),
	       elm->base.create_tid,
	       elm->base.delete_tid);

	switch(type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		printf("suboff=%016llx", elm->internal.subtree_offset);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			printf("\n\t         ");
			printf(" dataoff=%016llx/%d",
				elm->leaf.data_offset, elm->leaf.data_len);
			if (VerboseOpt) {
				printf("\n\t         fills=");
				print_bigblock_fill(elm->leaf.data_offset);
			}
			if (VerboseOpt > 1)
				print_record(elm);
			break;
		}
		break;
	default:
		break;
	}
	printf("\n");
}

static
int
print_elm_flags(hammer_node_ondisk_t node, hammer_off_t node_offset,
		hammer_btree_elm_t elm, u_int8_t btype,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	int flags = 0;

	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		if (elm->internal.subtree_offset) {
			struct buffer_info *buffer = NULL;
			hammer_node_ondisk_t subnode;

			subnode = get_node(elm->internal.subtree_offset,
					   &buffer);
			if (subnode->parent != node_offset)
				flags |= FLAG_BADCHILDPARENT;
			rel_buffer(buffer);
		}

		switch(btype) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) > 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		case HAMMER_BTREE_TYPE_LEAF:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) >= 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		default:
			flags |= FLAG_BADTYPE;
			break;
		}
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		switch(btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			if (left_bound == NULL || right_bound == NULL)
				break;
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) >= 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		default:
			flags |= FLAG_BADTYPE;
			break;
		}
		break;
	default:
		flags |= FLAG_BADTYPE;
		break;
	}
	return(flags);
}

static
void
print_bigblock_fill(hammer_off_t offset)
{
	struct hammer_blockmap_layer1 layer1;
	struct hammer_blockmap_layer2 layer2;
	int fill;

	blockmap_lookup(offset, &layer1, &layer2);
	fill = layer2.bytes_free * 100 / HAMMER_LARGEBLOCK_SIZE;
	fill = 100 - fill;

	printf("z%d:%lld=%d%%",
		HAMMER_ZONE_DECODE(offset),
		(offset & ~HAMMER_OFF_ZONE_MASK) / HAMMER_LARGEBLOCK_SIZE,
		fill
	);
}

static
void
print_record(hammer_btree_elm_t elm)
{
	struct buffer_info *data_buffer;
	hammer_off_t data_offset;
	int32_t data_len;
	hammer_data_ondisk_t data;

	data_offset = elm->leaf.data_offset;
	data_len = elm->leaf.data_len;
	data_buffer = NULL;

	if (data_offset)
		data = get_buffer_data(data_offset, &data_buffer, 0);
	else
		data = NULL;

	switch(elm->leaf.base.rec_type) {
	case HAMMER_RECTYPE_INODE:
		printf("\n%17s", "");
		printf("size=%lld nlinks=%lld",
		       data->inode.size, data->inode.nlinks);
		if (VerboseOpt > 2) {
			printf(" mode=%05o uflags=%08x\n",
				data->inode.mode,
				data->inode.uflags);
			printf("%17s", "");
			printf("ctime=%016llx pobjid=%016llx obj_type=%d\n",
				data->inode.ctime, data->inode.parent_obj_id,
				data->inode.obj_type);
			printf("%17s", "");
			printf("mtime=%016llx", data->inode.mtime);
		}
		break;
	case HAMMER_RECTYPE_DIRENTRY:
		printf("\n%17s", "");
		data_len -= HAMMER_ENTRY_NAME_OFF;
		printf("dir-entry ino=%016llx name=\"%*.*s\"",
		       data->entry.obj_id,
		       data_len, data_len, data->entry.name);
		break;
	case HAMMER_RECTYPE_FIX:
		switch(elm->leaf.base.key) {
		case HAMMER_FIXKEY_SYMLINK:
			data_len -= HAMMER_SYMLINK_NAME_OFF;
			printf("\n%17s", "");
			printf("symlink=\"%*.*s\"", data_len, data_len,
				data->symlink.name);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (data_buffer)
		rel_buffer(data_buffer);
}

