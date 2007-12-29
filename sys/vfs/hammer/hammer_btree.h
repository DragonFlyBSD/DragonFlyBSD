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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.h,v 1.7 2007/12/29 09:01:27 dillon Exp $
 */

/*
 * HAMMER B-Tree index
 *
 * HAMMER implements a modified B+Tree.   B+Trees store records only
 * at their leaves and HAMMER's modification is to adjust the internal
 * elements so there is a boundary element on each side instead of sub-tree
 * pointers.
 *
 * We just call our modified B+Tree a 'B-Tree' in HAMMER documentation to
 * reduce confusion.
 *
 * A B-Tree internal node looks like this:
 *
 *	B N N N N N N B   <-- boundary and internal elements
 *       S S S S S S S    <-- subtree pointers
 *
 * A B-Tree leaf node looks like this:
 *
 *	L L L L L L L L   <-- leaf elemenets
 *			      (there is also a previous and next-leaf pointer)
 *
 * The recursion radix of an internal node is reduced by 1 relative to
 * a normal B-Tree in order to accomodate the right-hand boundary.
 *
 * The big benefit to using a B-Tree with built-in bounds information is
 * that it makes it possible to cache pointers into the middle of the tree
 * and not have to start searches, insertions, OR deletions at the root node.
 * The boundary elements allow searches to progress in a definitive direction
 * from any point in the tree without revisting nodes.  It is also possible
 * to terminate searches early and make minor adjustments to the boundaries
 * (within the confines of the parent's boundaries) on the fly.  This greatly
 * improves the efficiency of many operations, most especially record appends.
 *
 * HAMMER B-Trees are per-cluster.  The global multi-cluster B-Tree is
 * constructed by allowing internal nodes to link to the roots of other
 * clusters.  Fields in the cluster header then reference back to its
 * parent and use the cluster generation number to detect stale linkages.
 *
 * The B-Tree balancing code can operate within a cluster or across the
 * filesystem's ENTIRE B-Tree super-structure.  A cluster's B-Tree root
 * can be a leaf node in the worse case.  A cluster is guarenteed to have
 * sufficient free space to hold a single completely full leaf in the
 * degenerate case.
 *
 * All of the structures below are on-disk structures.
 */

/*
 * Common base for all B-Tree element types (40 bytes)
 *
 * obj_type is set to the object type the record represents if an inode,
 * directory entry, or an inter-cluster reference.  A cluster range is
 * special in that the B-Tree nodes represent a range within the B-Tree
 * inclusive of rec_type field, so obj_type must be used to detect the
 * cluster range entries.
 *
 * subtree_type is only used by internal B-Tree elements and indicates
 * whether we are referencing a cluster, a leaf, or an internal node.
 */
struct hammer_base_elm {
	int64_t	obj_id;		/* 00 object record is associated with */
	int64_t key;		/* 08 indexing key (offset or namekey) */

	hammer_tid_t create_tid; /* 10 transaction id for record creation */
	hammer_tid_t delete_tid; /* 18 transaction id for record update/del */

	u_int16_t rec_type;	/* 20 _RECTYPE_ */
	u_int8_t obj_type;	/* 22 _OBJTYPE_ (restricted) */
	u_int8_t subtree_type;	/* 23 B-Tree element type */
	int32_t reserved07;	/* 24 (future) */
				/* 28 */
};

typedef struct hammer_base_elm *hammer_base_elm_t;

/*
 * Internal element (40 + 16 = 56 bytes).
 *
 * An internal element contains the left-hand boundary and a recursion to
 * another B-Tree node.  If rec_offset is 0 it points to another node in the
 * current cluster at subtree_offset.  Otherwise it points to the root
 * of another cluster at volno/cluid.
 *
 * sub-cluster references have an associated record while intra-cluster
 * references do not.
 *
 * subtree_count is the number of elements in an intra-cluster reference.
 * For inter-cluster references subtree_count is chaining depth heuristic
 * used to help balance the B-Tree globally.
 */
struct hammer_btree_internal_elm {
	struct hammer_base_elm base;
	int32_t rec_offset;	/* 0 indicates internal reference */
	int32_t subtree_offset;	/* offset or cluster id */
	int32_t subtree_vol_no;	/* unused or volume number */
	int32_t subtree_count;	/* hint: can be too small, but not too big */
};

#define subtree_clu_no	subtree_offset
#define subtree_type	base.subtree_type

/*
 * Leaf B-Tree element (40 + 16 = 56 bytes).
 *
 * A leaf
 */
struct hammer_btree_leaf_elm {
	struct hammer_base_elm base;
	int32_t rec_offset;
	int32_t data_offset;
	int32_t data_len;
	u_int32_t data_crc;
};

/*
 * Rollup btree leaf element types - 56 byte structure
 */
union hammer_btree_elm {
	struct hammer_base_elm base;
	struct hammer_btree_leaf_elm leaf;
	struct hammer_btree_internal_elm internal;
};

typedef union hammer_btree_elm *hammer_btree_elm_t;

/*
 * B-Tree node (normal or meta) - 24 + 56 * 14 = 808 bytes (8-byte aligned)
 *
 * 20 B-Tree nodes fit in a 16K filesystem buffer, leaving us room for
 * the 128 byte filesystem buffer header and another 96 bytes of filler.
 *
 * Each node contains 14 elements.  The last element for an internal node
 * is the right-boundary so internal nodes have one fewer logical elements
 * then leaf nodes.
 *
 * 'count' always refers to the number of elements and is non-inclusive of
 * the right-hand boundary for an internal node.
 *
 * NOTE: The node head for an internal does not contain the subtype
 * (The B-Tree node type for the nodes referenced by its elements). 
 * Instead, each element specifies the subtype (elm->base.subtype).
 * This allows us to maintain an unbalanced B-Tree and to easily identify
 * special inter-cluster link elements.
 */
#define HAMMER_BTREE_LEAF_ELMS	14
#define HAMMER_BTREE_INT_ELMS	(HAMMER_BTREE_LEAF_ELMS - 1)
#define HAMMER_BTREE_NODES	20

/*
 * It is safe to combine two adjacent nodes if the total number of elements
 * is less then or equal to the *_FILL constant.
 */
#define HAMMER_BTREE_LEAF_FILL	(HAMMER_BTREE_LEAF_ELMS - 3)
#define HAMMER_BTREE_INT_FILL	(HAMMER_BTREE_INT_ELMS - 3)

#define HAMMER_BTREE_TYPE_INTERNAL	((u_int8_t)'I')
#define HAMMER_BTREE_TYPE_LEAF		((u_int8_t)'L')
#define HAMMER_BTREE_TYPE_CLUSTER	((u_int8_t)'C')

struct hammer_node_ondisk {
	/*
	 * B-Tree node header (24 bytes)
	 */
	int32_t		count;
	int32_t		parent;		/* 0 if at root of cluster */
	u_int8_t	type;
	u_int8_t	reserved01;
	u_int16_t	reserved02;
	int32_t		reserved03;	/* future heuristic */
	int32_t		reserved04;	/* future link_left */
	int32_t		reserved05;	/* future link_right */

	/*
	 * Element array.  Internal nodes have one less logical element
	 * (meaning: the same number of physical elements) in order to
	 * accomodate the right-hand boundary.  The left-hand boundary
	 * is integrated into the first element.  Leaf nodes have no
	 * boundary elements.
	 */
	union hammer_btree_elm elms[HAMMER_BTREE_LEAF_ELMS];
};

typedef struct hammer_node_ondisk *hammer_node_ondisk_t;

/*
 * B-Tree filesystem buffer (16K exactly)
 */
struct hammer_fsbuf_btree {
        struct hammer_fsbuf_head        head;		/* 128 */
	char				filler[96];
        struct hammer_node_ondisk	nodes[HAMMER_BTREE_NODES];
};

#if HAMMER_BTREE_NODES * (HAMMER_BTREE_LEAF_ELMS * 56 + 24) + 128 + 96 != 16384
#error "Sanity check hammer_fsbuf_btree"
#endif
