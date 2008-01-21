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
 * $DragonFly: src/sbin/hammer/cmd_show.c,v 1.3 2008/01/21 00:03:31 dillon Exp $
 */

#include "hammer.h"

#define FLAG_TOOFARLEFT		0x0001
#define FLAG_TOOFARRIGHT	0x0002
#define FLAG_BADTYPE		0x0004

static void print_btree_node(struct cluster_info *cluster,
			int32_t node_offset, int depth, int spike,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);
static void print_btree_elm(hammer_btree_elm_t elm, int i, u_int8_t type,
			int flags, const char *label);
static int print_elm_flags(hammer_node_ondisk_t node, hammer_btree_elm_t elm,
			u_int8_t btype, hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);

void
hammer_cmd_show(int32_t vol_no, int32_t clu_no, int depth)
{
	struct volume_info *volume;
	struct cluster_info *cluster;
	int32_t node_offset;

	if (vol_no == -1) {
		if (RootVolNo < 0)
			errx(1, "hammer show: root volume number unknown");
		vol_no = RootVolNo;
	}
	volume = get_volume(vol_no);
	if (volume == NULL)
		errx(1, "hammer show: Unable to locate volume %d", vol_no);
	if (clu_no == -1)
		clu_no = volume->ondisk->vol0_root_clu_no;
	cluster = get_cluster(volume, clu_no, 0);
	printf("show %d:%d root@%08x parent@%d:%d:%08x depth %d\n",
	       vol_no, clu_no,
	       cluster->ondisk->clu_btree_root,
	       cluster->ondisk->clu_btree_parent_vol_no,
	       cluster->ondisk->clu_btree_parent_clu_no,
	       cluster->ondisk->clu_btree_parent_offset,
	       depth);
	node_offset = cluster->ondisk->clu_btree_root;
	print_btree_node(cluster, node_offset, depth, 0,
			 &cluster->ondisk->clu_btree_beg,
			 &cluster->ondisk->clu_btree_end);
	print_btree_node(cluster, node_offset, depth, 1,
			 &cluster->ondisk->clu_btree_beg,
			 &cluster->ondisk->clu_btree_end);
	rel_cluster(cluster);
	rel_volume(volume);
}

static void
print_btree_node(struct cluster_info *cluster, int32_t node_offset, int depth,
		 int spike,
		 hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;
	int flags;

	node = get_node(cluster, node_offset, &buffer);

	if (spike == 0) {
		printf("    NODE %d:%d:%08x count=%d parent=%08x "
		       "type=%c depth=%d {\n",
		       cluster->volume->vol_no,
		       cluster->clu_no,
			node_offset, node->count, node->parent,
			(node->type ? node->type : '?'),
			depth);

		for (i = 0; i < node->count; ++i) {
			elm = &node->elms[i];
			flags = print_elm_flags(node, elm, elm->base.btype,
						left_bound, right_bound);
			print_btree_elm(elm, i, node->type, flags, "ELM");
		}
		if (node->type == HAMMER_BTREE_TYPE_INTERNAL) {
			elm = &node->elms[i];
			flags = print_elm_flags(node, elm, 'I',
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
				print_btree_node(cluster,
						 elm->internal.subtree_offset,
						 depth + 1, spike,
						 &elm[0].base, &elm[1].base);
			}
			break;
		case HAMMER_BTREE_TYPE_LEAF:
			if (RecurseOpt && spike &&
			    elm->leaf.base.btype == HAMMER_BTREE_TYPE_SPIKE_END) {
				hammer_cmd_show(elm->leaf.spike_vol_no,
						elm->leaf.spike_clu_no,
						depth + 1);
			}
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

	printf("%s\t%s %2d %c ",
	       flagstr, label, i,
	       (elm->base.btype ? elm->base.btype : '?'));
	printf("obj=%016llx key=%016llx rt=%02x ot=%02x\n",
	       elm->base.obj_id,
	       elm->base.key,
	       elm->base.rec_type,
	       elm->base.obj_type);
	printf("\t         tids %016llx:%016llx ",
	       elm->base.create_tid,
	       elm->base.delete_tid);

	switch(type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		printf("suboff=%08x", elm->internal.subtree_offset);
		break;
	case HAMMER_BTREE_TYPE_LEAF:
		switch(elm->base.btype) {
		case HAMMER_BTREE_TYPE_RECORD:
			break;
		case HAMMER_BTREE_TYPE_SPIKE_BEG:
		case HAMMER_BTREE_TYPE_SPIKE_END:
			printf("spike %d:%d",
			       elm->leaf.spike_vol_no,
			       elm->leaf.spike_clu_no);
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
print_elm_flags(hammer_node_ondisk_t node, hammer_btree_elm_t elm,
		u_int8_t btype,
		hammer_base_elm_t left_bound, hammer_base_elm_t right_bound)
{
	int flags = 0;

	switch(node->type) {
	case HAMMER_BTREE_TYPE_INTERNAL:
		switch(btype) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) > 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		case HAMMER_BTREE_TYPE_LEAF:
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
		case HAMMER_BTREE_TYPE_SPIKE_END:
			if (hammer_btree_cmp(&elm->base, left_bound) < 0)
				flags |= FLAG_TOOFARLEFT;
			if (hammer_btree_cmp(&elm->base, right_bound) > 0)
				flags |= FLAG_TOOFARRIGHT;
			break;
		case HAMMER_BTREE_TYPE_SPIKE_BEG:
		case HAMMER_BTREE_TYPE_RECORD:
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

