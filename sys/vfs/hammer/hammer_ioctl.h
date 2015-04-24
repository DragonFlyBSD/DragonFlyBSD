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
 * $DragonFly: src/sys/vfs/hammer/hammer_ioctl.h,v 1.23 2008/11/13 02:18:43 dillon Exp $
 */
/*
 * HAMMER ioctl's.  This file can be #included from userland
 */

#ifndef VFS_HAMMER_IOCTL_H_
#define VFS_HAMMER_IOCTL_H_

#include <sys/param.h>
#include <sys/ioccom.h>
#include "hammer_disk.h"

/*
 * Common HAMMER ioctl header
 *
 * Global flags are stored in the upper 16 bits.
 */
struct hammer_ioc_head {
	int32_t		flags;
	int32_t		error;
	int32_t		reserved02[4];
};

#define HAMMER_IOC_HEAD_ERROR	0x00008000
#define HAMMER_IOC_HEAD_INTR	0x00010000
#define HAMMER_IOC_DO_BTREE	0x00020000	/* reblocker */
#define HAMMER_IOC_DO_INODES	0x00040000	/* reblocker */
#define HAMMER_IOC_DO_DATA	0x00080000	/* reblocker */
#define HAMMER_IOC_DO_DIRS	0x00100000	/* reblocker */

#define HAMMER_IOC_DO_FLAGS	(HAMMER_IOC_DO_BTREE |	\
				 HAMMER_IOC_DO_INODES |	\
				 HAMMER_IOC_DO_DATA |	\
				 HAMMER_IOC_DO_DIRS)

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

	struct hammer_base_elm key_beg;	/* stop forward scan (reverse scan) */
	struct hammer_base_elm key_end;	/* start forward scan (reverse scan) */
	struct hammer_base_elm key_cur;	/* scan interruption point */

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
 * HAMMERIOC_REPACK
 *
 * Forward scan leaf-up B-Tree packing.  The saturation point is typically
 * set to HAMMER_BTREE_LEAF_ELMS * 2 / 3 for 2/3rds fill.  Referenced nodes
 * have to be skipped, we can't track cursors through pack ops.
 */
struct hammer_ioc_rebalance {
	struct hammer_ioc_head head;
	int		saturation;	/* saturation pt elements/node */
	int		reserved02;

	struct hammer_base_elm key_beg;	/* start forward scan */
	struct hammer_base_elm key_end; /* stop forward scan (inclusive) */
	struct hammer_base_elm key_cur; /* current scan index */

	int64_t		stat_ncount;	/* number of nodes scanned */
	int64_t		stat_deletions; /* number of nodes deleted */
	int64_t		stat_collisions;/* number of collision retries */
	int64_t		stat_nrebal;	/* number of btree-nodes rebalanced */
	int32_t		allpfs;		/* rebalance all PFS if set */
	int32_t		unused04;
};

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

typedef struct hammer_ioc_hist_entry {
	hammer_tid_t	tid;
	u_int32_t	time32;
	u_int32_t	unused;
} *hammer_ioc_hist_entry_t;

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
	struct hammer_ioc_hist_entry hist_ary[HAMMER_MAX_HISTORY_ELMS];
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

	struct hammer_base_elm key_beg;		/* start forward scan */
	struct hammer_base_elm key_end;		/* stop forward scan */
	struct hammer_base_elm key_cur;		/* scan interruption point */

	int64_t		btree_count;		/* B-Tree nodes checked */
	int64_t		record_count;		/* Records checked */
	int64_t		data_count;		/* Data segments checked */
	int64_t		data_byte_count;	/* Data bytes checked */

	int64_t		btree_moves;		/* B-Tree nodes moved */
	int64_t		record_moves;		/* Records moved */
	int64_t		data_moves;		/* Data segments moved */
	int64_t		data_byte_moves;	/* Data bytes moved */

	int32_t		allpfs;			/* Reblock all PFS if set */
	int32_t		unused03;
};

/*
 * HAMMERIOC_SYNCTID
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

/*
 * HAMMERIOC_GET_INFO
 */
struct hammer_ioc_info {
	struct hammer_ioc_head		head;

	char		vol_name[64];
	uuid_t		vol_fsid;
	uuid_t		vol_fstype;

	int		version;
	int		nvolumes;
	int		reserved01;
	int		reserved02;

	int64_t		bigblocks;
	int64_t		freebigblocks;
	int64_t		rsvbigblocks;
	int64_t		inodes;

	int64_t		reservedext[16];
};

/*
 * HAMMERIOC_PFS_ITERATE
 */
struct hammer_ioc_pfs_iterate {
	struct hammer_ioc_head  head;
	uint32_t pos;  /* set PFS id here */
	struct hammer_pseudofs_data *ondisk;
};

/*
 * HAMMERIOC_GET_PSEUDOFS
 * HAMMERIOC_SET_PSEUDOFS
 */
struct hammer_ioc_pseudofs_rw {
	struct hammer_ioc_head	head;
	int			pfs_id;
	u_int32_t		bytes;
	u_int32_t		version;
	u_int32_t		flags;
	struct hammer_pseudofs_data *ondisk;
};

#define HAMMER_IOC_PSEUDOFS_VERSION	1

#define HAMMER_IOC_PFS_SYNC_BEG		0x0001
#define HAMMER_IOC_PFS_SYNC_END		0x0002
#define HAMMER_IOC_PFS_SHARED_UUID	0x0004
#define HAMMER_IOC_PFS_MIRROR_UUID	0x0008
#define HAMMER_IOC_PFS_MASTER_ID	0x0010
#define HAMMER_IOC_PFS_MIRROR_FLAGS	0x0020
#define HAMMER_IOC_PFS_LABEL		0x0040

#define HAMMER_MAX_PFS			65536

/*
 * HAMMERIOC_MIRROR_READ/WRITE
 */
struct hammer_ioc_mirror_rw {
	struct hammer_ioc_head	head;
	struct hammer_base_elm 	key_beg;	/* start forward scan */
	struct hammer_base_elm 	key_end;	/* stop forward scan */
	struct hammer_base_elm	key_cur;	/* interruption point */
	hammer_tid_t		tid_beg;	/* filter modification range */
	hammer_tid_t		tid_end;	/* filter modification range */
	void			*ubuf;		/* user buffer */
	int			count;		/* current size */
	int			size;		/* max size */
	int			pfs_id;		/* PFS id being read/written */
	int			reserved01;
	uuid_t			shared_uuid;	/* validator for safety */
};

#define HAMMER_IOC_MIRROR_NODATA	0x0001	/* do not include bulk data */

/*
 * NOTE: crc is for the data block starting at rec_size, not including the
 * data[] array.
 */
struct hammer_ioc_mrecord_head {
	u_int32_t		signature;	/* signature for byte order */
	u_int32_t		rec_crc;
	u_int32_t		rec_size;
	u_int32_t		type;
	/* extended */
};

typedef struct hammer_ioc_mrecord_head *hammer_ioc_mrecord_head_t;

struct hammer_ioc_mrecord_rec {
	struct hammer_ioc_mrecord_head	head;
	struct hammer_btree_leaf_elm	leaf;
	/* extended by data */
};

struct hammer_ioc_mrecord_skip {
	struct hammer_ioc_mrecord_head	head;
	struct hammer_base_elm	 	skip_beg;
	struct hammer_base_elm 		skip_end;
};

struct hammer_ioc_mrecord_update {
	struct hammer_ioc_mrecord_head	head;
	hammer_tid_t			tid;
};

struct hammer_ioc_mrecord_sync {
	struct hammer_ioc_mrecord_head	head;
};

struct hammer_ioc_mrecord_pfs {
	struct hammer_ioc_mrecord_head	head;
	u_int32_t			version;
	u_int32_t			reserved01;
	struct hammer_pseudofs_data	pfsd;
};

struct hammer_ioc_version {
	struct hammer_ioc_head head;
	u_int32_t		cur_version;
	u_int32_t		min_version;
	u_int32_t		wip_version;
	u_int32_t		max_version;
	char			description[64];
};

struct hammer_ioc_volume {
	struct hammer_ioc_head head;
	char			device_name[MAXPATHLEN];
	int64_t			vol_size;
	int64_t			boot_area_size;
	int64_t			mem_area_size;
};

struct hammer_ioc_volume_list {
	struct hammer_ioc_volume *vols;
	int nvols;
};

union hammer_ioc_mrecord_any {
	struct hammer_ioc_mrecord_head	head;
	struct hammer_ioc_mrecord_rec	rec;
	struct hammer_ioc_mrecord_skip	skip;
	struct hammer_ioc_mrecord_update update;
	struct hammer_ioc_mrecord_update sync;
	struct hammer_ioc_mrecord_pfs	pfs;
	struct hammer_ioc_version	version;
};

typedef union hammer_ioc_mrecord_any *hammer_ioc_mrecord_any_t;

/*
 * MREC types.  Flags are in the upper 16 bits but some are also included
 * in the type mask to force them into any switch() on the type.
 *
 * NOTE: Any record whos data is CRC-errored will have HAMMER_MRECF_CRC set,
 *	 and the bit is also part of the type mask.
 */
#define HAMMER_MREC_TYPE_RESERVED	0
#define HAMMER_MREC_TYPE_REC		1	/* record w/ data */
#define HAMMER_MREC_TYPE_PFSD		2	/* (userland only) */
#define HAMMER_MREC_TYPE_UPDATE		3	/* (userland only) */
#define HAMMER_MREC_TYPE_SYNC		4	/* (userland only) */
#define HAMMER_MREC_TYPE_SKIP		5	/* skip-range */
#define HAMMER_MREC_TYPE_PASS		6	/* record for cmp only (pass) */
#define HAMMER_MREC_TYPE_TERM		7	/* (userland only) */
#define HAMMER_MREC_TYPE_IDLE		8	/* (userland only) */

#define HAMMER_MREC_TYPE_REC_BADCRC	(HAMMER_MREC_TYPE_REC | \
					 HAMMER_MRECF_CRC_ERROR)
#define HAMMER_MREC_TYPE_REC_NODATA	(HAMMER_MREC_TYPE_REC | \
					 HAMMER_MRECF_NODATA)

#define HAMMER_MRECF_TYPE_LOMASK	0x000000FF
#define HAMMER_MRECF_TYPE_MASK		0x800000FF
#define HAMMER_MRECF_CRC_ERROR		0x80000000

#define HAMMER_MRECF_DATA_CRC_BAD	0x40000000
#define HAMMER_MRECF_RECD_CRC_BAD	0x20000000
#define HAMMER_MRECF_NODATA		0x10000000

#define HAMMER_MREC_CRCOFF	(offsetof(struct hammer_ioc_mrecord_head, rec_size))
#define HAMMER_MREC_HEADSIZE	sizeof(struct hammer_ioc_mrecord_head)

#define HAMMER_IOC_MIRROR_SIGNATURE	0x4dd97272U
#define HAMMER_IOC_MIRROR_SIGNATURE_REV	0x7272d94dU

/*
 * HAMMERIOC_ADD_SNAPSHOT - Add snapshot tid(s).
 * HAMMERIOC_DEL_SNAPSHOT - Delete snapshot tids.
 * HAMMERIOC_GET_SNAPSHOT - Get/continue retrieving snapshot tids.
 *			    (finds restart point based on last snaps[] entry)
 *
 * These are per-PFS operations.
 *
 * NOTE: There is no limit on the number of snapshots, but there is a limit
 *	 on how many can be set or returned in each ioctl.
 *
 * NOTE: ADD and DEL start at snap->index.  If an error occurs the index will
 *	 point at the errored record.  snap->index must be set to 0 for GET.
 */
#define HAMMER_SNAPS_PER_IOCTL		16

#define HAMMER_IOC_SNAPSHOT_EOF		0x0008	/* no more keys */

struct hammer_ioc_snapshot {
	struct hammer_ioc_head	head;
	int			unused01;
	u_int32_t		index;
	u_int32_t		count;
	struct hammer_snapshot_data snaps[HAMMER_SNAPS_PER_IOCTL];
};

/*
 * HAMMERIOC_GET_CONFIG
 * HAMMERIOC_SET_CONFIG
 *
 * The configuration space is a freeform nul-terminated string, typically
 * a text file.  It is per-PFS and used by the 'hammer cleanup' utility.
 *
 * The configuration space is NOT mirrored.  mirror-write will ignore
 * configuration space records.
 */
struct hammer_ioc_config {
	struct hammer_ioc_head	head;
	u_int32_t		reserved01;
	u_int32_t		reserved02;
	u_int64_t		reserved03[4];
	struct hammer_config_data config;
};

/*
 * HAMMERIOC_DEDUP
 */
struct hammer_ioc_dedup {
	struct hammer_ioc_head	head;
	struct hammer_base_elm	elm1;
	struct hammer_base_elm	elm2; /* candidate for dedup */
};

#define HAMMER_IOC_DEDUP_CMP_FAILURE	0x0001 /* verification failed */
#define HAMMER_IOC_DEDUP_UNDERFLOW	0x0002 /* bigblock underflow */
#define HAMMER_IOC_DEDUP_INVALID_ZONE	0x0004 /* we can't dedup all zones */

/*
 * HAMMERIOC_GET_DATA
 */
struct hammer_ioc_data {
	struct hammer_ioc_head		head;
	struct hammer_base_elm		elm;	/* btree key to lookup */
	struct hammer_btree_leaf_elm	leaf;
	void				*ubuf;	/* user buffer */
	int				size;	/* max size */
};

/*
 * Ioctl cmd ids
 */
#define HAMMERIOC_PRUNE		_IOWR('h',1,struct hammer_ioc_prune)
#define HAMMERIOC_GETHISTORY	_IOWR('h',2,struct hammer_ioc_history)
#define HAMMERIOC_REBLOCK	_IOWR('h',3,struct hammer_ioc_reblock)
#define HAMMERIOC_SYNCTID	_IOWR('h',4,struct hammer_ioc_synctid)
#define HAMMERIOC_SET_PSEUDOFS	_IOWR('h',5,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_GET_PSEUDOFS	_IOWR('h',6,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_MIRROR_READ	_IOWR('h',7,struct hammer_ioc_mirror_rw)
#define HAMMERIOC_MIRROR_WRITE	_IOWR('h',8,struct hammer_ioc_mirror_rw)
#define HAMMERIOC_UPG_PSEUDOFS	_IOWR('h',9,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_DGD_PSEUDOFS	_IOWR('h',10,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_RMR_PSEUDOFS	_IOWR('h',11,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_WAI_PSEUDOFS	_IOWR('h',12,struct hammer_ioc_pseudofs_rw)
#define HAMMERIOC_GET_VERSION	_IOWR('h',13,struct hammer_ioc_version)
#define HAMMERIOC_SET_VERSION	_IOWR('h',14,struct hammer_ioc_version)
#define HAMMERIOC_REBALANCE	_IOWR('h',15,struct hammer_ioc_rebalance)
#define HAMMERIOC_GET_INFO	_IOR('h',16,struct hammer_ioc_info)
#define HAMMERIOC_ADD_VOLUME 	_IOWR('h',17,struct hammer_ioc_volume)
#define HAMMERIOC_ADD_SNAPSHOT	_IOWR('h',18,struct hammer_ioc_snapshot)
#define HAMMERIOC_DEL_SNAPSHOT	_IOWR('h',19,struct hammer_ioc_snapshot)
#define HAMMERIOC_GET_SNAPSHOT	_IOWR('h',20,struct hammer_ioc_snapshot)
#define HAMMERIOC_GET_CONFIG	_IOWR('h',21,struct hammer_ioc_config)
#define HAMMERIOC_SET_CONFIG	_IOWR('h',22,struct hammer_ioc_config)
#define HAMMERIOC_DEL_VOLUME 	_IOWR('h',24,struct hammer_ioc_volume)
#define HAMMERIOC_DEDUP		_IOWR('h',25,struct hammer_ioc_dedup)
#define HAMMERIOC_GET_DATA	_IOWR('h',26,struct hammer_ioc_data)
#define HAMMERIOC_LIST_VOLUMES	_IOWR('h',27,struct hammer_ioc_volume_list)
#define HAMMERIOC_PFS_ITERATE	_IOWR('h',28,struct hammer_ioc_pfs_iterate)

#endif

