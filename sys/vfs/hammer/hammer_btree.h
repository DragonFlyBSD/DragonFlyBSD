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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.h,v 1.2 2007/10/12 18:57:45 dillon Exp $
 */

/*
 * HAMMER B-Tree index
 *
 * rec_type is the record type for a leaf node or an extended record type
 * for internal btree nodes and cluster references.
 *
 * A B-Tree diving down into a new cluster will have two B-Tree elements
 * indicating the full sub-range stored in that cluster.  These elements
 * will match the elements stored in the cluster header.
 */

/*
 * Common base for all B+Tree element types (40 bytes)
 *
 * Note that obj_type is set to the object type of the record represents
 * an inode or a cluster range.  A cluster range is special in that the
 * B-Tree nodes represent a range within the B-Tree inclusive of rec_type
 * field, so obj_type must be used to detect the cluster range entries.
 */
struct hammer_base_elm {
	int64_t	obj_id;		/* 00 object record is associated with */
	int64_t key;		/* 08 indexing key (offset or namekey) */

	hammer_tid_t create_tid; /* 10 transaction id for record creation */
	hammer_tid_t delete_tid; /* 18 transaction id for record update/del */

	u_int16_t rec_type;	/* 20 */
	u_int16_t obj_type;	/* 22 (special) */
	int32_t subtree_offset;	/* 24 (B+Tree recursion) */
				/* 28 */
};

/*
 * Leaf B-Tree element (40 + 16 = 56 bytes)
 */
struct hammer_record_elm {
	struct hammer_base_elm base;
	int32_t rec_offset;
	int32_t data_offset;
	int32_t data_len;
	u_int32_t data_crc;
};

/*
 * Leaf Cluster-reference B-Tree element (40 + 16 = 56 bytes)
 */
struct hammer_cluster_elm {
	struct hammer_base_elm base;
	int32_t	rec_offset;		/* cluster recursion record */
	u_int32_t verifier;		/* low 32 bits of target clu_id */
	int32_t	vol_no;
	int32_t	cluster_no;
};

/*
 * Rollup btree element types - 56 byte structure
 */
union hammer_btree_elm {
	struct hammer_base_elm base;
	struct hammer_record_elm record;
	struct hammer_cluster_elm cluster;
};

/*
 * B-Tree node (normal or meta) - 56 + 56 * 8 = 504 bytes
 *
 * 32 B-Tree nodes fit in a 16K filesystem buffer.  Remember, the filesystem
 * buffer has a 128 byte header so (16384 - 128) / 32 = 508, but we want
 * our structures to be 8-byte aligned so we use 504.
 */
#define HAMMER_BTREE_ELMS	8

struct hammer_btree_node {
	/*
	 * B-Tree node header (56 bytes)
	 */
	int32_t		count;	/* number of elements in B-Tree node */
	int32_t		parent;	/* parent B-Tree node in current cluster */
	u_int32_t	reserved[12];

	/*
	 * 8 elements making up the B-Tree node 56x8 = 448 bytes
	 */
	union hammer_btree_elm elms[HAMMER_BTREE_ELMS];
};

/*
 * B-Tree filesystem buffer
 */
#define HAMMER_BTREE_NODES		\
        ((HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head)) / \
        sizeof(struct hammer_btree_node))

struct hammer_fsbuf_btree {
        struct hammer_fsbuf_head        head;
	char				unused[128];
        struct hammer_btree_node	nodes[HAMMER_BTREE_NODES];
};


