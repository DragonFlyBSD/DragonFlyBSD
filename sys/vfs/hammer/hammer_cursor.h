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
 * $DragonFly: src/sys/vfs/hammer/hammer_cursor.h,v 1.6 2008/01/16 01:15:36 dillon Exp $
 */

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
 * Most operations maintain an exclusive lock on their node and
 * parent node, but certain cursor operations may temporarily release
 * one or both locks.  In particular, a cursor_up operation may have
 * to temporarily release underlying locks to avoid a deadlock.
 */
struct hammer_cursor {
	/*
	 * Parent B-Tree node, current B-Tree node, and related element
	 * indices.
	 */
	hammer_node_t parent;
	int parent_index;
	hammer_node_t node;
	int index;

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

	/*
	 * Related data and record references.  Note that the related buffers
	 * can be NULL when data and/or record is not, typically indicating
	 * information referenced via an in-memory record.
	 */
	struct hammer_buffer *data_buffer;	/* extracted data & record */
	union hammer_data_ondisk *data;
	struct hammer_buffer *record_buffer;
	union hammer_record_ondisk *record;

	/*
	 * Iteration and extraction control variables
	 */
	int flags;

	/*
	 * Merged in-memory/on-disk iterations also use these fields.
	 */
	struct hammer_inode *ip;
	struct hammer_record *iprec;
	struct hammer_rec_rb_tree_scan_info scan;
};

typedef struct hammer_cursor *hammer_cursor_t;

#define HAMMER_CURSOR_GET_RECORD	0x0001
#define HAMMER_CURSOR_GET_DATA		0x0002
#define HAMMER_CURSOR_INCLUSTER		0x0004	/* stay in the cluster */
#define HAMMER_CURSOR_INSERT		0x0008	/* adjust for insert */
#define HAMMER_CURSOR_DELETE		0x0010	/* adjust for delete */
#define HAMMER_CURSOR_END_INCLUSIVE	0x0020	/* key_end is inclusive */
#define HAMMER_CURSOR_END_EXCLUSIVE	0x0040	/* key_end is exclusive (def) */
#define HAMMER_CURSOR_ALLHISTORY	0x0080	/* return entire history */

#define HAMMER_CURSOR_ATEDISK		0x0100
#define HAMMER_CURSOR_ATEMEM		0x0200
#define HAMMER_CURSOR_DISKEOF		0x0400
#define HAMMER_CURSOR_MEMEOF		0x0800
#define HAMMER_CURSOR_DELBTREE		0x1000	/* ip_delete from b-tree */
#define HAMMER_CURSOR_DATA_EMBEDDED	0x2000	/* embedded flag on extract */
#define HAMMER_CURSOR_ASOF		0x4000	/* as-of lookup */

