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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.h,v 1.3 2007/11/01 20:53:05 dillon Exp $
 */

/*
 * HAMMER BH-Tree index
 *
 * HAMMER implements a modified B+Tree.  In all documentation this will
 * simply be refered to as the HAMMER BH-Tree.  Basically la BH-Tree
 * looks like a B+Tree (A B-Tree which stores its records only at the leafs
 * of the tree), but adds two additional boundary elements which describe
 * the left-most and right-most element a node is able to represent.  In
 * otherwords, we have boundary elements at the two ends of a BH-Tree node
 * instead of sub-tree pointers.
 *
 * A BH-Tree internal node looks like this:
 *
 *	B N N N N N N B   <-- boundary and internal elements
 *       S S S S S S S    <-- subtree pointers
 *
 * A BH-Tree leaf node looks like this:
 *
 *	L L L L L L L L   <-- leaf elemenets
 *			      (there is also a previous and next-leaf pointer)
 *
 * The recursion radix is reduced by 2 relative to a normal B-Tree but
 * as a bonus we treat internal nodes completely differently from leaf nodes,
 * which allows us to pack in more elements.
 *
 * The big benefit to using a BH-Tree is that it is possible to cache
 * pointers into the middle of the tree and not have to start searches,
 * insertions, OR deletions at the root node.   In particular, searches are
 * able to progress in a definitive direction from any point in the tree
 * without revisting nodes.  This greatly improves the efficiency of many
 * operations, most especially record appends.
 *
 * BH-Trees also make stacking of trees fairly straightforward.
 *
 * Most of the structures below are on-disk structures.
 */

/*
 * Common base for all B+Tree element types (40 bytes)
 *
 * Note that obj_type is set to the object type of the record represents
 * an inode or a cluster range.  A cluster range is special in that the
 * B-Tree nodes represent a range within the B-Tree inclusive of rec_type
 * field, so obj_type must be used to detect the cluster range entries.
 *
 * The special field is used as a subtree pointer for internal nodes,
 * and reserved for leaf nodes.
 */
struct hammer_base_elm {
	int64_t	obj_id;		/* 00 object record is associated with */
	int64_t key;		/* 08 indexing key (offset or namekey) */

	hammer_tid_t create_tid; /* 10 transaction id for record creation */
	hammer_tid_t delete_tid; /* 18 transaction id for record update/del */

	u_int16_t rec_type;	/* 20 */
	u_int16_t obj_type;	/* 22 (special) */
	int32_t reserved07;	/* 24 (future) */
				/* 28 */
};

typedef struct hammer_base_elm *hammer_base_elm_t;

/*
 * Internal element - 48 bytes.  Internal elements are independantly
 * organized.  Note that internal elements are a different size than
 * leaf elements.
 */
struct hammer_btree_internal_elm {
	struct hammer_base_elm base;
	int32_t	subtree_offset;
	int8_t subtree_count;	/* hint: can be too small, but not too big */
	int8_t reserved01;
	int8_t reserved02;
	int8_t reserved03;
};

/*
 * Leaf B-Tree element (40 + 16 = 56 bytes)
 */
struct hammer_btree_record_elm {
	struct hammer_base_elm base;
	int32_t rec_offset;
	int32_t data_offset;
	int32_t data_len;
	u_int32_t data_crc;
};

/*
 * Leaf Cluster-reference B-Tree element (40 + 16 = 56 bytes)
 */
struct hammer_btree_cluster_elm {
	struct hammer_base_elm base;
	int32_t	rec_offset;		/* cluster recursion record */
	u_int32_t verifier;		/* low 32 bits of target clu_id */
	int32_t	vol_no;
	int32_t	clu_no;
};

/*
 * Rollup btree leaf element types - 56 byte structure
 */
union hammer_btree_leaf_elm {
	struct hammer_base_elm base;
	struct hammer_btree_record_elm record;
	struct hammer_btree_cluster_elm cluster;
};

typedef union hammer_btree_leaf_elm *hammer_btree_leaf_elm_t;

/*
 * B-Tree node (normal or meta) - 56 + 56 * 8 = 504 bytes
 *
 * 32 B-Tree nodes fit in a 16K filesystem buffer.  Remember, the filesystem
 * buffer has a 128 byte header so (16384 - 128) / 32 = 508, but we want
 * our structures to be 8-byte aligned so we use 504.
 *
 * We store B-Tree records as elements in internal nodes, which means we
 * must have a separate index of sub-tree pointers.  The sub-tree pointers
 * interlace the elements (that is, the elements act as separators).  Thus
 * we have to have one more pointer then we do elements so we can represent
 * the gamut - the sub-tree to the left of the first element AND the sub-tree
 * to the right of the last element.
 *
 * subcount represents the number of elements in the subnode (1-8)
 *
 * 'count' always refers to the number of elements.
 */
#define HAMMER_BTREE_LEAF_ELMS	8
#define HAMMER_BTREE_INT_ELMS	9	/* +1 more for the right boundary */

/*
 * It is safe to combine two adjacent nodes if the total number of elements
 * is less then or equal to the *_FILL constant.
 */
#define HAMMER_BTREE_LEAF_FILL	(HAMMER_BTREE_LEAF_ELMS - 3)
#define HAMMER_BTREE_INT_FILL	(HAMMER_BTREE_INT_ELMS - 3)

#define HAMMER_BTREE_INTERNAL_NODE	((u_int32_t)'I')
#define HAMMER_BTREE_LEAF_NODE		((u_int32_t)'L')

struct hammer_base_node {
	int32_t		count;
	int32_t		parent;
	u_int8_t	type;
	u_int8_t	subtype;
	u_int16_t	reserved02;
	int32_t		reserved03;
};

struct hammer_btree_internal_node {
	/*
	 * B-Tree internal node header (24 bytes)
	 */
	struct hammer_base_node base;
	int32_t		reserved[2];

	/*
	 * Internal element array.  The subtree fields are logically to
	 * the right of the element key.  These fields are unused and
	 * must be 0 for the right-hand boundary element.
	 *
	 * The left-hand boundary element is at elms[0], the right-hand
	 * boundary element is at elms[count].  count does not include
	 * the right-hand boundary (so count at the termination of a 
	 * standard for() loop will point at the right-hand boundary).
	 */
	struct hammer_btree_internal_elm elms[HAMMER_BTREE_INT_ELMS+1];
};

/*
 * The hammer leaf node contains a smaller header 
 */
struct hammer_btree_leaf_node {
	/*
	 * B-Tree leaf node header
	 */
	struct hammer_base_node base;
	int32_t		link_left;
	int32_t		link_right;
	int32_t		reserved[8];

	/*
	 * Leaf element array
	 */
	union hammer_btree_leaf_elm elms[HAMMER_BTREE_LEAF_ELMS];
};

union hammer_btree_node {
	struct hammer_base_node	    base;
	struct hammer_btree_internal_node internal;
	struct hammer_btree_leaf_node     leaf;
};

typedef union hammer_btree_node *hammer_btree_node_t;

/*
 * This in-memory structure is used by btree_rebalance_node().  Note
 * that internal elements are a different size than record and cluster
 * elements.
 */
struct hammer_btree_elm_inmemory {
	hammer_btree_node_t owner;
	union {
		struct hammer_base_elm base;
		struct hammer_btree_internal_elm internal;
		union hammer_btree_leaf_elm leaf;
	} u;
};

typedef struct hammer_btree_elm_inmemory *hammer_btree_elm_inmemory_t;


/*
 * B-Tree filesystem buffer
 */
#define HAMMER_BTREE_NODES		\
        ((HAMMER_BUFSIZE - sizeof(struct hammer_fsbuf_head)) / \
        sizeof(union hammer_btree_node))

struct hammer_fsbuf_btree {
        struct hammer_fsbuf_head        head;
	char				unused[128];
        union hammer_btree_node		nodes[HAMMER_BTREE_NODES];
};

/*
 * Key and caching structures used for HAMMER B-Tree lookups.  These are
 * in-memory structures.
 */
struct hammer_btree_cursor {
	hammer_base_elm_t left_bound;
	hammer_base_elm_t right_bound;
	struct hammer_cluster *cluster;
	struct hammer_buffer *parent_buffer;
	struct hammer_btree_internal_node *parent;
	struct hammer_buffer *node_buffer;
	hammer_btree_node_t node;		/* internal or leaf node */
	int index;
	int parent_index;
};

typedef struct hammer_btree_cursor *hammer_btree_cursor_t;

struct hammer_btree_info {
	struct hammer_btree_cursor cursor;
	struct hammer_buffer *data_buffer;
	struct hammer_buffer *record_buffer;
	union hammer_data_ondisk *data;	/* returned data pointer */
	union hammer_record_ondisk *rec;/* returned record pointer */
};

typedef struct hammer_btree_info *hammer_btree_info_t;

#define HAMMER_BTREE_GET_RECORD		0x0001
#define HAMMER_BTREE_GET_DATA		0x0002
#define HAMMER_BTREE_CLUSTER_TAG	0x0004	/* stop at the cluster tag */
#define HAMMER_BTREE_INSERT		0x0008	/* adjust for insert */
#define HAMMER_BTREE_DELETE		0x0010	/* adjust for delete */

