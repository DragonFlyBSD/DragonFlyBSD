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
 * $DragonFly: src/sys/vfs/hammer/hammer_ioctl.h,v 1.10 2008/05/31 18:37:57 dillon Exp $
 */
/*
 * HAMMER ioctl's.  This file can be #included from userland
 */

#ifndef VFS_HAMMER_IOCTL_H_
#define VFS_HAMMER_IOCTL_H_

#include <sys/types.h>
#include <sys/ioccom.h>
#include "hammer_disk.h"

/*
 * Common HAMMER ioctl header
 *
 * Global flags are stored in the upper 16 bits.
 */
struct hammer_ioc_head {
	int32_t		flags;
	int32_t		reserved01;
	int32_t		reserved02[4];
};

#define HAMMER_IOC_HEAD_INTR	0x00010000
#define HAMMER_IOC_DO_BTREE	0x00020000	/* reblocker */
#define HAMMER_IOC_DO_INODES	0x00040000	/* reblocker */
#define HAMMER_IOC_DO_DATA	0x00080000	/* reblocker */

#define HAMMER_IOC_DO_FLAGS	(HAMMER_IOC_DO_BTREE |	\
				 HAMMER_IOC_DO_INODES |	\
				 HAMMER_IOC_DO_DATA)

/*
 * HAMMERIOC_PRUNE
 *
 * beg/end TID ranges in the element array must be sorted in descending
 * order, with the most recent (highest) range at elms[0].
 */
struct hammer_ioc_prune_elm {
	hammer_tid_t	beg_tid;	/* starting tid */
	hammer_tid_t	end_tid;	/* ending tid (non inclusive) */
	hammer_tid_t	mod_tid;	/* modulo */
};

#define HAMMER_MAX_PRUNE_ELMS	(1024*1024/24)

struct hammer_ioc_prune {
	struct hammer_ioc_head head;
	int		nelms;
	int		reserved01;
	u_int32_t	beg_localization;
	u_int32_t	cur_localization;
	u_int32_t	end_localization;
	u_int32_t	reserved03;
	int64_t		beg_obj_id;
	int64_t		cur_obj_id;
	int64_t		cur_key;
	int64_t		end_obj_id;	 /* (range-exclusive) */
	int64_t		stat_scanrecords;/* number of records scanned */
	int64_t		stat_rawrecords; /* number of raw records pruned */
	int64_t		stat_dirrecords; /* number of dir records pruned */
	int64_t		stat_bytes;	 /* number of data bytes pruned */
	int64_t		stat_realignments; /* number of raw records realigned */
	hammer_tid_t	stat_oldest_tid; /* oldest create_tid encountered */
	int64_t		reserved02[6];
	struct hammer_ioc_prune_elm *elms; /* user supplied array */
};

#define HAMMER_IOC_PRUNE_ALL	0x0001


/*
 * HAMMERIOC_GETHISTORY
 *
 * Retrieve an array of ordered transaction ids >= beg and < end indicating
 * all changes made to the specified object's inode up to the
 * maximum.
 *
 * If ATKEY is set the key field indicates a particular key within the
 * inode to retrieve the history for.
 *
 * On return count is set to the number of elements returned, nxt_tid is
 * set to the tid the caller should store in beg_tid to continue the
 * iteration, and nxt_key is set to the nearest key boundary > key
 * indicating the range key - nxt_key (nxt_key non-inclusive) the tid
 * array represents.  Also obj_id is set to the object's inode number.
 *
 * nxt_key can be used to iterate the contents of a single file but should
 * not be stored in key until all modifications at key have been retrieved.
 * To work properly nxt_key should be initialized to HAMMER_MAX_KEY.
 * Successive ioctl() calls will reduce nxt_key as appropriate so at the
 * end of your iterating for 'key', key to nxt_key will represent the
 * shortest range of keys that all returned TIDs apply to.
 */

#define HAMMER_MAX_HISTORY_ELMS	64

struct hammer_ioc_history {
	struct hammer_ioc_head head;
	int64_t		obj_id;
	hammer_tid_t	beg_tid;
	hammer_tid_t	nxt_tid;
	hammer_tid_t	end_tid;
	int64_t		key;
	int64_t		nxt_key;
	int		count;
	int		reserve01;
	hammer_tid_t	tid_ary[HAMMER_MAX_HISTORY_ELMS];
};

#define HAMMER_IOC_HISTORY_ATKEY	0x0001
#define HAMMER_IOC_HISTORY_NEXT_TID	0x0002	/* iterate via nxt_tid */
#define HAMMER_IOC_HISTORY_NEXT_KEY	0x0004	/* iterate via nxt_key */
#define HAMMER_IOC_HISTORY_EOF		0x0008	/* no more keys */
#define HAMMER_IOC_HISTORY_UNSYNCED	0x0010	/* unsynced info in inode */

/*
 * Reblock request
 */
struct hammer_ioc_reblock {
	struct hammer_ioc_head head;
	int32_t		free_level;		/* 0 for maximum compaction */
	u_int32_t	reserved01;

	u_int32_t	beg_localization;
	u_int32_t	cur_localization;
	u_int32_t	end_localization;
	u_int32_t	reserved03;

	int64_t		beg_obj_id;
	int64_t		cur_obj_id;		/* Stopped at (interrupt) */
	int64_t		end_obj_id;

	int64_t		btree_count;		/* B-Tree nodes checked */
	int64_t		record_count;		/* Records checked */
	int64_t		data_count;		/* Data segments checked */
	int64_t		data_byte_count;	/* Data bytes checked */

	int64_t		btree_moves;		/* B-Tree nodes moved */
	int64_t		record_moves;		/* Records moved */
	int64_t		data_moves;		/* Data segments moved */
	int64_t		data_byte_moves;	/* Data bytes moved */

	int32_t		unused02;
	int32_t		unused03;
};

/*
 * HAMMER_IOC_SYNCTID
 */
enum hammer_synctid_op {
	HAMMER_SYNCTID_NONE,	/* no sync (TID will not be accurate) */
	HAMMER_SYNCTID_ASYNC,	/* async (TID will not be accurate) */
	HAMMER_SYNCTID_SYNC1,	/* single sync - might undo after crash */
	HAMMER_SYNCTID_SYNC2	/* double sync - guarantee no undo */
};

struct hammer_ioc_synctid {
	struct hammer_ioc_head	head;
	enum hammer_synctid_op	op;
	hammer_tid_t		tid;
};


#define HAMMERIOC_PRUNE		_IOWR('h',1,struct hammer_ioc_prune)
#define HAMMERIOC_GETHISTORY	_IOWR('h',2,struct hammer_ioc_history)
#define HAMMERIOC_REBLOCK	_IOWR('h',3,struct hammer_ioc_reblock)
#define HAMMERIOC_SYNCTID	_IOWR('h',4,struct hammer_ioc_synctid)

#endif
