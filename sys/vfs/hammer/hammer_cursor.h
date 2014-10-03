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
 * $DragonFly: src/sys/vfs/hammer/hammer_cursor.h,v 1.26 2008/08/06 15:38:58 dillon Exp $
 */

struct hammer_cmirror;

/*
 * The hammer_cursor structure is the primary in-memory management structure
 * for B-Tree operations.  
 *
 * The most important issue to make note of is that a hammer_cursor is a
 * tracking structure.  Any active hammer_cursor structure will be linked into
 * hammer_node based lists and B-Tree operations executed by unrelated
 * treads MAY MODIFY YOUR CURSOR when you are not holding an exclusive
 * lock on the cursor's nodes.
 *
 * The cursor module maintains a shared lock on cursor->node and
 * cursor->parent.
 */
struct hammer_cursor {
	/*
	 * Parent B-Tree node, current B-Tree node, and related element
	 * indices.
	 */
	hammer_transaction_t trans;
	hammer_node_t	parent;
	int		parent_index;

	hammer_node_t	node;
	int		index;

	/*
	 * Set if a deadlock occurs.  hammer_done_cursor() will block on
	 * this after releasing parent and node, before returning.  See
	 * also hammer_recover_cursor().
	 */
	TAILQ_ENTRY(hammer_cursor) deadlk_entry;
	hammer_node_t deadlk_node;
	hammer_record_t deadlk_rec;

	/*
	 * Set along with HAMMER_CURSOR_CREATE_CHECK when doing an as-of
	 * search.  If ENOENT is returned and the flag is set, the caller
	 * must iterate with a new delete_tid.
	 */
	hammer_tid_t  create_check;

	/*
	 * Pointer to the current node's bounds.  Typically points to the
	 * appropriate boundary elements in the parent or points to bounds
	 * stored in the cluster.  The right-boundary is range-exclusive.
	 */
	hammer_base_elm_t left_bound;
	hammer_base_elm_t right_bound;

	/*
	 * Key or key range governing search.  The cursor code may adjust
	 * key_beg/key_end if asof is non-zero.
	 */
	struct hammer_base_elm key_beg;
	struct hammer_base_elm key_end;
	hammer_tid_t	asof;
	struct hammer_cmirror *cmirror;

	/*
	 * Related data and record references.  Note that the related buffers
	 * can be NULL when data and/or record is not, typically indicating
	 * information referenced via an in-memory record.
	 */
	struct hammer_buffer *data_buffer;	/* extended data */
	struct hammer_btree_leaf_elm *leaf;
	union hammer_data_ondisk *data;

	/*
	 * Iteration and extraction control variables
	 */
	int flags;
	int rec_generation;

	/*
	 * Merged in-memory/on-disk iterations also use these fields.
	 */
	struct hammer_inode *ip;
	struct hammer_record *iprec;
};

typedef struct hammer_cursor *hammer_cursor_t;

#define HAMMER_CURSOR_GET_LEAF		0x0001
#define HAMMER_CURSOR_GET_DATA		0x0002
#define HAMMER_CURSOR_BACKEND		0x0004	/* cursor run by backend */
#define HAMMER_CURSOR_INSERT		0x0008	/* adjust for insert */
#define HAMMER_CURSOR_DELETE_VISIBILITY	0x0010	/* special del-on-disk recs */
#define HAMMER_CURSOR_END_INCLUSIVE	0x0020	/* key_end is inclusive */
#define HAMMER_CURSOR_END_EXCLUSIVE	0x0040	/* key_end is exclusive (def) */

#define HAMMER_CURSOR_ATEDISK		0x0100
#define HAMMER_CURSOR_ATEMEM		0x0200
#define HAMMER_CURSOR_DISKEOF		0x0400
#define HAMMER_CURSOR_MEMEOF		0x0800
#define HAMMER_CURSOR_RETEST		0x1000	/* retest current element */
#define HAMMER_CURSOR_MIRROR_FILTERED	0x2000	/* mirror_tid filter */
#define HAMMER_CURSOR_ASOF		0x4000	/* as-of lookup */
#define HAMMER_CURSOR_CREATE_CHECK	0x8000	/* as-of lookup */

#define HAMMER_CURSOR_PRUNING		0x00010000
#define HAMMER_CURSOR_REBLOCKING	0x00020000
#define HAMMER_CURSOR_TRACKED		0x00040000
#define HAMMER_CURSOR_TRACKED_RIPOUT	0x00080000
#define HAMMER_CURSOR_LASTWASMEM	0x00100000 /* hammer_ip_next logic */
#define HAMMER_CURSOR_ITERATE_CHECK	0x00200000
#define HAMMER_CURSOR_NOSWAPCACHE	0x00400000 /* applies to LARGE_DATA */

/*
 * Flags we can clear when reusing a cursor (we can clear all of them)
 */
#define HAMMER_CURSOR_INITMASK		(~0)

/*
 * Mirror scan extension structure.  Caller sets mirror_tid to restrict
 * the scan.  If the iteration is able to skip one or more internal nodes
 * it returns an internal node with skip_beg/end set to the skipped range.
 *
 * If the first element of an internal node is skipped skip_beg will use
 * the left_bound inherited from the parent, and the same for the last
 * element.  This is because gaps can develop in the bounds.
 */
struct hammer_cmirror {
	hammer_tid_t	mirror_tid;
	struct hammer_base_elm skip_beg;
	struct hammer_base_elm skip_end;
};

/*
 * NOTE: iprec can be NULL, but the address-of does not indirect through
 * it so we are ok.
 */
#define hammer_cursor_inmem(cursor)		\
			((cursor)->leaf == &(cursor)->iprec->leaf)
#define hammer_cursor_ondisk(cursor)		\
			((cursor)->leaf != &(cursor)->iprec->leaf)
