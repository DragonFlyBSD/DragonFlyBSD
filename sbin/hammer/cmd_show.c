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
 * $DragonFly: src/sbin/hammer/cmd_show.c,v 1.2 2008/01/17 05:14:49 dillon Exp $
 */

#include "hammer.h"

static void print_btree_node(struct cluster_info *cluster,
			int32_t node_offset, int depth);
static void print_btree_elm(hammer_btree_elm_t elm, int i,
			u_int8_t type, const char *label);

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
	printf("show %d:%d root@%08x parent@%d:%d depth %d\n",
	       vol_no, clu_no,
	       cluster->ondisk->clu_btree_root,
	       cluster->ondisk->clu_btree_parent_vol_no,
	       cluster->ondisk->clu_btree_parent_clu_no,
	       depth);
	node_offset = cluster->ondisk->clu_btree_root;
	print_btree_node(cluster, node_offset, depth);
	rel_cluster(cluster);
	rel_volume(volume);
}

static void
print_btree_node(struct cluster_info *cluster, int32_t node_offset, int depth)
{
	struct buffer_info *buffer = NULL;
	hammer_node_ondisk_t node;
	hammer_btree_elm_t elm;
	int i;

	node = get_node(cluster, node_offset, &buffer);

	printf("    NODE %08x count=%d parent=%d type=%c depth=%d {\n",
		node_offset, node->count, node->parent,
		(node->type ? node->type : '?'),
		depth);

	for (i = 0; i < node->count; ++i)
		print_btree_elm(&node->elms[i], i, node->type, "ELM");
	if (node->type == HAMMER_BTREE_TYPE_INTERNAL)
		print_btree_elm(&node->elms[i], i, node->type, "RBN");
	printf("    }\n");

	for (i = 0; i < node->count; ++i) {
		elm = &node->elms[i];

		switch(node->type) {
		case HAMMER_BTREE_TYPE_INTERNAL:
			if (elm->internal.subtree_offset) {
				print_btree_node(cluster,
						 elm->internal.subtree_offset,
						 depth + 1);
			}
			break;
		case HAMMER_BTREE_TYPE_LEAF:
			if (RecurseOpt && elm->leaf.base.btype == 
					    HAMMER_BTREE_TYPE_SPIKE_END) {
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
print_btree_elm(hammer_btree_elm_t elm, int i,
		u_int8_t type, const char *label)
{
	printf("\t%s %2d %c ",
	       label, i,
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

