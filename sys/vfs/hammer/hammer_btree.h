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
 * $DragonFly: src/sys/vfs/hammer/hammer_btree.h,v 1.24 2008/06/26 04:06:22 dillon Exp $
 */

#ifndef VFS_HAMMER_BTREE_H_
#define VFS_HAMMER_BTREE_H_

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
 * The left-hand boundary (B in the left) is integrated into the first
 * element so it doesn't require 2 elements to accomodate boundaries.
 *
 * The big benefit to using a B-Tree with built-in bounds information is
 * that it makes it possible to cache pointers into the middle of the tree
 * and not have to start searches, insertions, OR deletions at the root node.
 * The boundary elements allow searches to progress in a definitive direction
 * from any point in the tree without revisting nodes.  It is also possible
 * to terminate searches early and make minor adjustments to the boundaries
 * (within the confines of the parent's boundaries) on the fly.  This greatly
 * improves the efficiency of many operations.
 *
 * All of the structures below are on-disk structures.
 */

/*
 * Common base for all B-Tree element types (40 bytes)
 *
 * btype field represents a type of B-Tree ondisk structure that this
 * B-Tree element points to, but not a type of B-Tree node that this
 * B-Tree element is a part of.  btype could be HAMMER_BTREE_TYPE_RECORD
 * as well as HAMMER_BTREE_TYPE_INTERNAL and HAMMER_BTREE_TYPE_LEAF,
 * while B-Tree node type is never HAMMER_BTREE_TYPE_RECORD.
 *
 * The following fields are keys used by hammer_btree_cmp() to compare
 * B-Tree elements listed from higher to lower priority on comparison.
 * B-Tree elements are first grouped by localization value, and then
 * obj_id within a subtree of the same localization value, and so on.
 *
 * 1. localization
 * 2. obj_id
 * 3. rec_type
 * 4. key
 * 5. create_tid
 */
typedef struct hammer_base_elm {
	int64_t	obj_id;		/* 00 object record is associated with */
	int64_t key;		/* 08 indexing key (offset or namekey) */

	hammer_tid_t create_tid; /* 10 transaction id for record creation */
	hammer_tid_t delete_tid; /* 18 transaction id for record update/del */

	uint16_t rec_type;	/* 20 HAMMER_RECTYPE_ */
	uint8_t obj_type;	/* 22 HAMMER_OBJTYPE_ */
	uint8_t btype;		/* 23 B-Tree element type */
	uint32_t localization;	/* 24 B-Tree localization parameter */
				/* 28 */
} *hammer_base_elm_t;

/*
 * Localization has sorting priority over the obj_id,rec_type,key,tid
 * and is used to localize inodes for very fast directory scans.
 *
 * Localization can also be used to create pseudo-filesystems within
 * a HAMMER filesystem.  Pseudo-filesystems would be suitable
 * replication targets.
 *
 * Upper 16 bits of the localization field in struct hammer_base_elm
 * represents pseudo-filesystem id ranging from default 0 to 65535,
 * and lower 16 bits represents its localization type in bitfield
 * where 0x1 means the element is for inode and 0x2 means the element
 * is for anything other than inode.  Note that 0x3 (0x1|0x2) is not
 * a valid type, while 0 and 0xFFFF are valid types for some cases.
 *
 * The root inode (not the PFS root inode but the real root) uses
 * HAMMER_DEF_LOCALIZATION for its incore ip->obj_localization.
 * HAMMER_DEF_LOCALIZATION implies PFS#0 and no localization type.
 */
#define HAMMER_LOCALIZE_INODE		0x00000001
#define HAMMER_LOCALIZE_MISC		0x00000002  /* not inode */
#define HAMMER_LOCALIZE_MASK		0x0000FFFF
#define HAMMER_LOCALIZE_PSEUDOFS_MASK	0xFFFF0000

#define HAMMER_MIN_LOCALIZATION		0x00000000U
#define HAMMER_MAX_LOCALIZATION		0x0000FFFFU
#define HAMMER_DEF_LOCALIZATION		0x00000000U

#define HAMMER_MIN_ONDISK_LOCALIZATION	\
	HAMMER_MIN_LOCALIZATION
#define HAMMER_MAX_ONDISK_LOCALIZATION	\
	(HAMMER_MAX_LOCALIZATION | HAMMER_LOCALIZE_PSEUDOFS_MASK)

#define lo_to_pfs(lo)					\
	((int)(((lo) & HAMMER_LOCALIZE_PSEUDOFS_MASK) >> 16))
#define pfs_to_lo(pfs)					\
	((((uint32_t)(pfs)) << 16) & HAMMER_LOCALIZE_PSEUDOFS_MASK)

/*
 * Internal element (40 + 24 = 64 bytes).
 *
 * An internal element contains a recursion to another B-Tree node.
 */
typedef struct hammer_btree_internal_elm {
	struct hammer_base_elm base;
	hammer_tid_t	mirror_tid;		/* mirroring support */
	hammer_off_t	subtree_offset;
	int32_t		reserved01;
	int32_t		reserved02;
} *hammer_btree_internal_elm_t;

/*
 * Leaf B-Tree element (40 + 24 = 64 bytes).
 *
 * NOTE: create_ts/delete_ts are not used by any core algorithms, they are
 *       used only by userland to present nominal real-time date strings
 *	 to the user.
 */
typedef struct hammer_btree_leaf_elm {
	struct hammer_base_elm base;
	uint32_t	create_ts;
	uint32_t	delete_ts;
	hammer_off_t	data_offset;
	int32_t		data_len;
	hammer_crc_t	data_crc;
} *hammer_btree_leaf_elm_t;

/*
 * Rollup btree leaf element types - 64 byte structure
 */
typedef union hammer_btree_elm {
	struct hammer_base_elm		base;
	struct hammer_btree_leaf_elm	leaf;
	struct hammer_btree_internal_elm internal;
} *hammer_btree_elm_t;

/*
 * B-Tree node (64x64 = 4K structure)
 *
 * Each node contains 63 elements.  The last element for an internal node
 * is the right-boundary so internal nodes have one fewer logical elements
 * then leaf nodes.
 *
 * 'count' always refers to the number of elements and is non-inclusive of
 * the right-hand boundary for an internal node. For a leaf node, 'count'
 * refers to the number of elements and there is no idea of boundaries.
 *
 * The use of a fairly large radix is designed to reduce the number of
 * discrete disk accesses required to locate something.  Keep in mind
 * that nodes are allocated out of 16K hammer buffers so supported values
 * are (256-1), (128-1), (64-1), (32-1), or (16-1). HAMMER uses 63-way
 * so the node size is (64x(1+(64-1))) = 4KB.
 *
 * NOTE: FUTURE EXPANSION: The reserved fields in hammer_node_ondisk are
 * reserved for left/right leaf linkage fields, flags, and other future
 * features.
 */
#define HAMMER_BTREE_TYPE_INTERNAL	((uint8_t)'I')
#define HAMMER_BTREE_TYPE_LEAF		((uint8_t)'L')
#define HAMMER_BTREE_TYPE_RECORD	((uint8_t)'R')
#define HAMMER_BTREE_TYPE_DELETED	((uint8_t)'D')
#define HAMMER_BTREE_TYPE_NONE		((uint8_t)0)

#define HAMMER_BTREE_LEAF_ELMS	63
#define HAMMER_BTREE_INT_ELMS	(HAMMER_BTREE_LEAF_ELMS - 1)

typedef struct hammer_node_ondisk {
	/*
	 * B-Tree node header (64 bytes)
	 */
	hammer_crc_t	crc;		/* MUST BE FIRST FIELD OF STRUCTURE */
	uint32_t	reserved01;
	hammer_off_t	parent;		/* 0 if at root of B-Tree */
	int32_t		count;		/* maximum 62 for INTERNAL, 63 for LEAF */
	uint8_t		type;		/* B-Tree node type (INTERNAL or LEAF) */
	uint8_t		reserved02;
	uint16_t	reserved03;
	hammer_off_t	reserved04;
	hammer_off_t	reserved05;
	hammer_off_t	reserved06;
	hammer_off_t	reserved07;
	hammer_tid_t	mirror_tid;	/* mirroring support (aggregator) */

	/*
	 * B-Tree node element array (64x63 bytes)
	 *
	 * Internal nodes have one less logical element
	 * (meaning: the same number of physical elements) in order to
	 * accomodate the right-hand boundary.  The left-hand boundary
	 * is integrated into the first element.  Leaf nodes have no
	 * boundary elements.
	 */
	union hammer_btree_elm elms[HAMMER_BTREE_LEAF_ELMS];
} *hammer_node_ondisk_t;

#define HAMMER_BTREE_CRCSIZE	\
	(sizeof(struct hammer_node_ondisk) - sizeof(hammer_crc_t))

/*
 * Return 1 if elm is a node element of an internal node,
 * otherwise return 0.
 */
static __inline
int
hammer_is_internal_node_elm(hammer_btree_elm_t elm)
{
	switch (elm->base.btype) {
	case HAMMER_BTREE_TYPE_INTERNAL:
	case HAMMER_BTREE_TYPE_LEAF:
		return(1);
	}
	return(0);
}

/*
 * Return 1 if elm is a node element of a leaf node,
 * otherwise return 0.
 */
static __inline
int
hammer_is_leaf_node_elm(hammer_btree_elm_t elm)
{
	switch (elm->base.btype) {
	case HAMMER_BTREE_TYPE_RECORD:
		return(1);
	}
	return(0);
}

static __inline
int
hammer_node_max_elements(uint8_t type)
{
	switch (type) {
	case HAMMER_BTREE_TYPE_LEAF:
		return(HAMMER_BTREE_LEAF_ELMS);
	case HAMMER_BTREE_TYPE_INTERNAL:
		return(HAMMER_BTREE_INT_ELMS);
	}
	return(-1);  /* invalid type */
}

static __inline
char
hammer_elm_btype(hammer_btree_elm_t elm)
{
	switch(elm->base.btype) {
	case HAMMER_BTREE_TYPE_INTERNAL:
	case HAMMER_BTREE_TYPE_LEAF:
	case HAMMER_BTREE_TYPE_RECORD:
	case HAMMER_BTREE_TYPE_DELETED:
		return(elm->base.btype);  /* ascii */
	case HAMMER_BTREE_TYPE_NONE:
		return('*');
	default:
		return('?');
	}
}

#endif /* !VFS_HAMMER_BTREE_H_ */
