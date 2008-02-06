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
 * $DragonFly: src/sys/vfs/hammer/hammer_ioctl.h,v 1.3 2008/02/06 08:59:28 dillon Exp $
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

#define HAMMER_MAX_PRUNE_ELMS	64

struct hammer_ioc_prune {
	int		nelms;
	int		flags;
	int64_t		beg_obj_id;
	int64_t		cur_obj_id;	/* initialize to end_obj_id */
	int64_t		cur_key;	/* initialize to HAMMER_MAX_KEY */
	int64_t		end_obj_id;	 /* (range-exclusive) */
	int64_t		stat_scanrecords;/* number of records scanned */
	int64_t		stat_rawrecords; /* number of raw records pruned */
	int64_t		stat_dirrecords; /* number of dir records pruned */
	int64_t		stat_bytes;	 /* number of data bytes pruned */
	int64_t		stat_realignments; /* number of raw records realigned */
	int64_t		reserved02[7];
	struct hammer_ioc_prune_elm elms[HAMMER_MAX_PRUNE_ELMS];
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
	int64_t		obj_id;
	hammer_tid_t	beg_tid;
	hammer_tid_t	nxt_tid;
	hammer_tid_t	end_tid;
	int64_t		key;
	int64_t		nxt_key;
	int		count;
	int		flags;
	hammer_tid_t	tid_ary[HAMMER_MAX_HISTORY_ELMS];
};

#define HAMMER_IOC_HISTORY_ATKEY	0x0001
#define HAMMER_IOC_HISTORY_NEXT_TID	0x0002	/* iterate via nxt_tid */
#define HAMMER_IOC_HISTORY_NEXT_KEY	0x0004	/* iterate via nxt_key */
#define HAMMER_IOC_HISTORY_EOF		0x0008	/* no more keys */
#define HAMMER_IOC_HISTORY_UNSYNCED	0x0010	/* unsynced info in inode */

#define HAMMERIOC_PRUNE		_IOWR('h',1,struct hammer_ioc_prune)
#define HAMMERIOC_GETHISTORY	_IOWR('h',2,struct hammer_ioc_history)

#endif
