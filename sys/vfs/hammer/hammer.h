/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
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
 */
/*
 * This header file contains structures used internally by the HAMMERFS
 * implementation.  See hammer_disk.h for on-disk structures.
 */

#include <sys/param.h>
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/kernel.h>
#endif
#include <sys/conf.h>
#ifdef _KERNEL
#include <sys/systm.h>
#endif
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mountctl.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/stat.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>
#ifdef _KERNEL
#include <sys/signal2.h>
#include <vm/vm_page2.h>
#endif

#include "hammer_disk.h"
#include "hammer_mount.h"
#include "hammer_ioctl.h"

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

MALLOC_DECLARE(M_HAMMER);

/*
 * Kernel trace
 */
#if !defined(KTR_HAMMER)
#define KTR_HAMMER	KTR_ALL
#endif
/* KTR_INFO_MASTER_EXTERN(hammer); */

/*
 * Misc structures
 */
struct hammer_mount;

/*
 * Key structure used for custom RB tree inode lookups.  This prototypes
 * the function hammer_ino_rb_tree_RB_LOOKUP_INFO(root, info).
 */
typedef struct hammer_inode_info {
	int64_t		obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
	u_int32_t	obj_localization; /* (key) pseudo-fs */
	union {
		struct hammer_btree_leaf_elm *leaf;
	} u;
} *hammer_inode_info_t;

typedef enum hammer_transaction_type {
	HAMMER_TRANS_RO,
	HAMMER_TRANS_STD,
	HAMMER_TRANS_FLS
} hammer_transaction_type_t;

/*
 * HAMMER Transaction tracking
 */
struct hammer_transaction {
	hammer_transaction_type_t type;
	struct hammer_mount *hmp;
	hammer_tid_t	tid;
	u_int64_t	time;
	u_int32_t	time32;
	int		sync_lock_refs;
	int		flags;
	struct hammer_volume *rootvol;
};

typedef struct hammer_transaction *hammer_transaction_t;

#define HAMMER_TRANSF_NEWINODE	0x0001
#define HAMMER_TRANSF_DIDIO	0x0002
#define HAMMER_TRANSF_CRCDOM	0x0004	/* EDOM on CRC error, less critical */

/*
 * HAMMER locks
 */
struct hammer_lock {
	volatile u_int	refs;		/* active references */
	volatile u_int	lockval;	/* lock count and control bits */
	struct thread	*lowner;	/* owner if exclusively held */
	struct thread	*rowner;	/* owner if exclusively held */
};

#define HAMMER_REFS_LOCKED	0x40000000	/* transition check */
#define HAMMER_REFS_WANTED	0x20000000	/* transition check */
#define HAMMER_REFS_CHECK	0x10000000	/* transition check */

#define HAMMER_REFS_FLAGS	(HAMMER_REFS_LOCKED | \
				 HAMMER_REFS_WANTED | \
				 HAMMER_REFS_CHECK)

#define HAMMER_LOCKF_EXCLUSIVE	0x40000000
#define HAMMER_LOCKF_WANTED	0x20000000

static __inline int
hammer_notlocked(struct hammer_lock *lock)
{
	return(lock->lockval == 0);
}

static __inline int
hammer_islocked(struct hammer_lock *lock)
{
	return(lock->lockval != 0);
}

/*
 * Returns the number of refs on the object.
 */
static __inline int
hammer_isactive(struct hammer_lock *lock)
{
	return(lock->refs & ~HAMMER_REFS_FLAGS);
}

static __inline int
hammer_oneref(struct hammer_lock *lock)
{
	return((lock->refs & ~HAMMER_REFS_FLAGS) == 1);
}

static __inline int
hammer_norefs(struct hammer_lock *lock)
{
	return((lock->refs & ~HAMMER_REFS_FLAGS) == 0);
}

static __inline int
hammer_norefsorlock(struct hammer_lock *lock)
{
	return(lock->refs == 0);
}

static __inline int
hammer_refsorlock(struct hammer_lock *lock)
{
	return(lock->refs != 0);
}

/*
 * Return if we specifically own the lock exclusively.
 */
static __inline int
hammer_lock_excl_owned(struct hammer_lock *lock, thread_t td)
{
	if ((lock->lockval & HAMMER_LOCKF_EXCLUSIVE) &&
	    lock->lowner == td) {
		return(1);
	}
	return(0);
}

/*
 * Flush state, used by various structures
 */
typedef enum hammer_inode_state {
	HAMMER_FST_IDLE,
	HAMMER_FST_SETUP,
	HAMMER_FST_FLUSH
} hammer_inode_state_t;

TAILQ_HEAD(hammer_record_list, hammer_record);

/*
 * Pseudo-filesystem extended data tracking
 */
struct hammer_pfs_rb_tree;
struct hammer_pseudofs_inmem;
RB_HEAD(hammer_pfs_rb_tree, hammer_pseudofs_inmem);
RB_PROTOTYPE2(hammer_pfs_rb_tree, hammer_pseudofs_inmem, rb_node,
	      hammer_pfs_rb_compare, u_int32_t);

struct hammer_pseudofs_inmem {
	RB_ENTRY(hammer_pseudofs_inmem)	rb_node;
	struct hammer_lock	lock;
	u_int32_t		localization;
	hammer_tid_t		create_tid;
	int			flags;
	udev_t			fsid_udev;
	struct hammer_pseudofs_data pfsd;
};

typedef struct hammer_pseudofs_inmem *hammer_pseudofs_inmem_t;

#define HAMMER_PFSM_DELETED	0x0001

/*
 * Cache object ids.  A fixed number of objid cache structures are
 * created to reserve object id's for newly created files in multiples
 * of 100,000, localized to a particular directory, and recycled as
 * needed.  This allows parallel create operations in different
 * directories to retain fairly localized object ids which in turn
 * improves reblocking performance and layout.
 */
#define OBJID_CACHE_SIZE	2048
#define OBJID_CACHE_BULK_BITS	10		/* 10 bits (1024)	*/
#define OBJID_CACHE_BULK	(32 * 32)	/* two level (1024)	*/
#define OBJID_CACHE_BULK_MASK	(OBJID_CACHE_BULK - 1)
#define OBJID_CACHE_BULK_MASK64	((u_int64_t)(OBJID_CACHE_BULK - 1))

typedef struct hammer_objid_cache {
	TAILQ_ENTRY(hammer_objid_cache) entry;
	struct hammer_inode		*dip;
	hammer_tid_t			base_tid;
	int				count;
	u_int32_t			bm0;
	u_int32_t			bm1[32];
} *hammer_objid_cache_t;

/*
 * Associate an inode with a B-Tree node to cache search start positions
 */
typedef struct hammer_node_cache {
	TAILQ_ENTRY(hammer_node_cache)	entry;
	struct hammer_node		*node;
	struct hammer_inode		*ip;
} *hammer_node_cache_t;

TAILQ_HEAD(hammer_node_cache_list, hammer_node_cache);

/*
 * Live dedup cache
 */
struct hammer_dedup_crc_rb_tree;
RB_HEAD(hammer_dedup_crc_rb_tree, hammer_dedup_cache);
RB_PROTOTYPE2(hammer_dedup_crc_rb_tree, hammer_dedup_cache, crc_entry,
		hammer_dedup_crc_rb_compare, hammer_crc_t);

struct hammer_dedup_off_rb_tree;
RB_HEAD(hammer_dedup_off_rb_tree, hammer_dedup_cache);
RB_PROTOTYPE2(hammer_dedup_off_rb_tree, hammer_dedup_cache, off_entry,
		hammer_dedup_off_rb_compare, hammer_off_t);

#define DEDUP_CACHE_SIZE	4096 /* XXX make it a dynamic tunable */

typedef struct hammer_dedup_cache {
	RB_ENTRY(hammer_dedup_cache) crc_entry;
	RB_ENTRY(hammer_dedup_cache) off_entry;
	TAILQ_ENTRY(hammer_dedup_cache) lru_entry;
	struct hammer_mount *hmp;
	int64_t obj_id;
	u_int32_t localization;
	off_t file_offset;
	int bytes;
	hammer_off_t data_offset; 
	hammer_crc_t crc;
} *hammer_dedup_cache_t;

/*
 * Structure used to organize flush groups.  Flush groups must be
 * organized into chunks in order to avoid blowing out the UNDO FIFO.
 * Without this a 'sync' could end up flushing 50,000 inodes in a single
 * transaction.
 */
struct hammer_fls_rb_tree;
RB_HEAD(hammer_fls_rb_tree, hammer_inode);
RB_PROTOTYPE(hammer_fls_rb_tree, hammer_inode, rb_flsnode,
	      hammer_ino_rb_compare);

struct hammer_flush_group {
	TAILQ_ENTRY(hammer_flush_group)	flush_entry;
	struct hammer_fls_rb_tree	flush_tree;
	int				seq;		/* our seq no */
	int				total_count;	/* record load */
	int				running;	/* group is running */
	int				closed;
	int				refs;
};

typedef struct hammer_flush_group *hammer_flush_group_t;

TAILQ_HEAD(hammer_flush_group_list, hammer_flush_group);

/*
 * Structure used to represent an inode in-memory.
 *
 * The record and data associated with an inode may be out of sync with
 * the disk (xDIRTY flags), or not even on the disk at all (ONDISK flag
 * clear).
 *
 * An inode may also hold a cache of unsynchronized records, used for
 * database and directories only.  Unsynchronized regular file data is
 * stored in the buffer cache.
 *
 * NOTE: A file which is created and destroyed within the initial
 * synchronization period can wind up not doing any disk I/O at all.
 *
 * Finally, an inode may cache numerous disk-referencing B-Tree cursors.
 */
struct hammer_ino_rb_tree;
struct hammer_inode;
RB_HEAD(hammer_ino_rb_tree, hammer_inode);
RB_PROTOTYPEX(hammer_ino_rb_tree, INFO, hammer_inode, rb_node,
	      hammer_ino_rb_compare, hammer_inode_info_t);

struct hammer_redo_rb_tree;
RB_HEAD(hammer_redo_rb_tree, hammer_inode);
RB_PROTOTYPE2(hammer_redo_rb_tree, hammer_inode, rb_redonode,
	      hammer_redo_rb_compare, hammer_off_t);

struct hammer_rec_rb_tree;
struct hammer_record;
RB_HEAD(hammer_rec_rb_tree, hammer_record);
RB_PROTOTYPEX(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
	      hammer_rec_rb_compare, hammer_btree_leaf_elm_t);

TAILQ_HEAD(hammer_node_list, hammer_node);

struct hammer_inode {
	RB_ENTRY(hammer_inode)	rb_node;
	hammer_inode_state_t	flush_state;
	hammer_flush_group_t	flush_group;
	RB_ENTRY(hammer_inode)	rb_flsnode;	/* when on flush list */
	RB_ENTRY(hammer_inode)	rb_redonode;	/* when INODE_RDIRTY is set */
	struct hammer_record_list target_list;	/* target of dependant recs */
	int64_t			obj_id;		/* (key) object identifier */
	hammer_tid_t		obj_asof;	/* (key) snapshot or 0 */
	u_int32_t		obj_localization; /* (key) pseudo-fs */
	struct hammer_mount 	*hmp;
	hammer_objid_cache_t 	objid_cache;
	int			flags;
	int			error;		/* flush error */
	int			cursor_ip_refs;	/* sanity */
	int			cursor_exclreq_count;
	int			rsv_recs;
	struct vnode		*vp;
	hammer_pseudofs_inmem_t	pfsm;
	struct lockf		advlock;
	struct hammer_lock	lock;		/* sync copy interlock */
	off_t			trunc_off;
	struct hammer_btree_leaf_elm ino_leaf;  /* in-memory cache */
	struct hammer_inode_data ino_data;	/* in-memory cache */
	struct hammer_rec_rb_tree rec_tree;	/* in-memory cache */
	int			rec_generation;
	struct hammer_node_cache cache[4];	/* search initiate cache */

	/*
	 * When a demark is created to synchronize an inode to
	 * disk, certain fields are copied so the front-end VOPs
	 * can continue to run in parallel with the synchronization
	 * occuring in the background.
	 */
	int		sync_flags;		/* to-sync flags cache */
	off_t		sync_trunc_off;		/* to-sync truncation */
	off_t		save_trunc_off;		/* write optimization */
	struct hammer_btree_leaf_elm sync_ino_leaf; /* to-sync cache */
	struct hammer_inode_data sync_ino_data; /* to-sync cache */
	size_t		redo_count;

	/*
	 * Track the earliest offset in the UNDO/REDO FIFO containing
	 * REDO records.  This is staged to the backend during flush
	 * sequences.  While the inode is staged redo_fifo_next is used
	 * to track the earliest offset for rotation into redo_fifo_start
	 * on completion of the flush.
	 */
	hammer_off_t	redo_fifo_start;
	hammer_off_t	redo_fifo_next;
};

typedef struct hammer_inode *hammer_inode_t;

#define VTOI(vp)	((struct hammer_inode *)(vp)->v_data)

/*
 * NOTE: DDIRTY does not include atime or mtime and does not include
 *	 write-append size changes.  SDIRTY handles write-append size
 *	 changes.
 *
 *	 REDO indicates that REDO logging is active, creating a definitive
 *	 stream of REDO records in the UNDO/REDO log for writes and
 *	 truncations, including boundary records when/if REDO is turned off.
 *	 REDO is typically enabled by fsync() and turned off if excessive
 *	 writes without an fsync() occurs.
 *
 *	 RDIRTY indicates that REDO records were laid down in the UNDO/REDO
 *	 FIFO (even if REDO is turned off some might still be active) and
 *	 still being tracked for this inode.  See hammer_redo.c
 */
					/* (not including atime/mtime) */
#define HAMMER_INODE_DDIRTY	0x0001	/* in-memory ino_data is dirty */
#define HAMMER_INODE_RSV_INODES	0x0002	/* hmp->rsv_inodes bumped */
#define HAMMER_INODE_CONN_DOWN	0x0004	/* include in downward recursion */
#define HAMMER_INODE_XDIRTY	0x0008	/* in-memory records */
#define HAMMER_INODE_ONDISK	0x0010	/* inode is on-disk (else not yet) */
#define HAMMER_INODE_FLUSH	0x0020	/* flush on last ref */
#define HAMMER_INODE_DELETED	0x0080	/* inode delete (backend) */
#define HAMMER_INODE_DELONDISK	0x0100	/* delete synchronized to disk */
#define HAMMER_INODE_RO		0x0200	/* read-only (because of as-of) */
#define HAMMER_INODE_RECSW	0x0400	/* waiting on data record flush */
#define HAMMER_INODE_DONDISK	0x0800	/* data records may be on disk */
#define HAMMER_INODE_BUFS	0x1000	/* dirty high level bps present */
#define HAMMER_INODE_REFLUSH	0x2000	/* flush on dependancy / reflush */
#define HAMMER_INODE_RECLAIM	0x4000	/* trying to reclaim */
#define HAMMER_INODE_FLUSHW	0x8000	/* Someone waiting for flush */

#define HAMMER_INODE_TRUNCATED	0x00010000
#define HAMMER_INODE_DELETING	0x00020000 /* inode delete request (frontend)*/
#define HAMMER_INODE_RESIGNAL	0x00040000 /* re-signal on re-flush */
#define HAMMER_INODE_ATIME	0x00100000 /* in-memory atime modified */
#define HAMMER_INODE_MTIME	0x00200000 /* in-memory mtime modified */
#define HAMMER_INODE_WOULDBLOCK 0x00400000 /* re-issue to new flush group */
#define HAMMER_INODE_DUMMY 	0x00800000 /* dummy inode covering bad file */
#define HAMMER_INODE_SDIRTY	0x01000000 /* in-memory ino_data.size is dirty*/
#define HAMMER_INODE_REDO	0x02000000 /* REDO logging active */
#define HAMMER_INODE_RDIRTY	0x04000000 /* REDO records active in fifo */
#define HAMMER_INODE_SLAVEFLUSH	0x08000000 /* being flushed by slave */

#define HAMMER_INODE_MODMASK	(HAMMER_INODE_DDIRTY|HAMMER_INODE_SDIRTY|   \
				 HAMMER_INODE_XDIRTY|HAMMER_INODE_BUFS|	    \
				 HAMMER_INODE_ATIME|HAMMER_INODE_MTIME|     \
				 HAMMER_INODE_TRUNCATED|HAMMER_INODE_DELETING)

#define HAMMER_INODE_MODMASK_NOXDIRTY	\
				(HAMMER_INODE_MODMASK & ~HAMMER_INODE_XDIRTY)

#define HAMMER_INODE_MODMASK_NOREDO	\
				(HAMMER_INODE_DDIRTY|			    \
				 HAMMER_INODE_XDIRTY|			    \
				 HAMMER_INODE_TRUNCATED|HAMMER_INODE_DELETING)

#define HAMMER_FLUSH_SIGNAL	0x0001
#define HAMMER_FLUSH_RECURSION	0x0002

/*
 * Used by the inode reclaim code to pipeline reclaims and avoid
 * blowing out kernel memory or letting the flusher get too far
 * behind.  The reclaim wakes up when count reaches 0 or the
 * timer expires.
 */
struct hammer_reclaim {
	TAILQ_ENTRY(hammer_reclaim) entry;
	int	count;
};

/*
 * Track who is creating the greatest burden on the
 * inode cache.
 */
struct hammer_inostats {
	pid_t		pid;	/* track user process */
	int		ltick;	/* last tick */
	int		count;	/* count (degenerates) */
};

#define HAMMER_INOSTATS_HSIZE	32
#define HAMMER_INOSTATS_HMASK	(HAMMER_INOSTATS_HSIZE - 1)

/*
 * Structure used to represent an unsynchronized record in-memory.  These
 * records typically represent directory entries.  Only non-historical
 * records are kept in-memory.
 *
 * Records are organized as a per-inode RB-Tree.  If the inode is not
 * on disk then neither are any records and the in-memory record tree
 * represents the entire contents of the inode.  If the inode is on disk
 * then the on-disk B-Tree is scanned in parallel with the in-memory
 * RB-Tree to synthesize the current state of the file.
 *
 * Records are also used to enforce the ordering of directory create/delete
 * operations.  A new inode will not be flushed to disk unless its related
 * directory entry is also being flushed at the same time.  A directory entry
 * will not be removed unless its related inode is also being removed at the
 * same time.
 */
typedef enum hammer_record_type {
	HAMMER_MEM_RECORD_GENERAL,	/* misc record */
	HAMMER_MEM_RECORD_INODE,	/* inode record */
	HAMMER_MEM_RECORD_ADD,		/* positive memory cache record */
	HAMMER_MEM_RECORD_DEL,		/* negative delete-on-disk record */
	HAMMER_MEM_RECORD_DATA		/* bulk-data record w/on-disk ref */
} hammer_record_type_t;

struct hammer_record {
	RB_ENTRY(hammer_record)		rb_node;
	TAILQ_ENTRY(hammer_record)	target_entry;
	hammer_inode_state_t		flush_state;
	hammer_flush_group_t		flush_group;
	hammer_record_type_t		type;
	struct hammer_lock		lock;
	struct hammer_reserve		*resv;
	struct hammer_inode		*ip;
	struct hammer_inode		*target_ip;
	struct hammer_btree_leaf_elm	leaf;
	union hammer_data_ondisk	*data;
	int				flags;
	int				gflags;
	hammer_off_t			zone2_offset;	/* direct-write only */
};

typedef struct hammer_record *hammer_record_t;

/*
 * Record flags.  Note that FE can only be set by the frontend if the
 * record has not been interlocked by the backend w/ BE.
 */
#define HAMMER_RECF_ALLOCDATA		0x0001
#define HAMMER_RECF_ONRBTREE		0x0002
#define HAMMER_RECF_DELETED_FE		0x0004	/* deleted (frontend) */
#define HAMMER_RECF_DELETED_BE		0x0008	/* deleted (backend) */
#define HAMMER_RECF_COMMITTED		0x0010	/* committed to the B-Tree */
#define HAMMER_RECF_INTERLOCK_BE	0x0020	/* backend interlock */
#define HAMMER_RECF_WANTED		0x0040	/* wanted by the frontend */
#define HAMMER_RECF_DEDUPED		0x0080	/* will be live-dedup'ed */
#define HAMMER_RECF_CONVERT_DELETE 	0x0100	/* special case */
#define HAMMER_RECF_REDO		0x1000	/* REDO was laid down */

/*
 * These flags must be separate to deal with SMP races
 */
#define HAMMER_RECG_DIRECT_IO		0x0001	/* related direct I/O running*/
#define HAMMER_RECG_DIRECT_WAIT		0x0002	/* related direct I/O running*/
#define HAMMER_RECG_DIRECT_INVAL	0x0004	/* buffer alias invalidation */
/*
 * hammer_create_at_cursor() and hammer_delete_at_cursor() flags.
 */
#define HAMMER_CREATE_MODE_UMIRROR	0x0001
#define HAMMER_CREATE_MODE_SYS		0x0002

#define HAMMER_DELETE_ADJUST		0x0001
#define HAMMER_DELETE_DESTROY		0x0002

/*
 * In-memory structures representing on-disk structures.
 */
struct hammer_volume;
struct hammer_buffer;
struct hammer_node;
struct hammer_undo;
struct hammer_reserve;

RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_buf_rb_tree, hammer_buffer);
RB_HEAD(hammer_nod_rb_tree, hammer_node);
RB_HEAD(hammer_und_rb_tree, hammer_undo);
RB_HEAD(hammer_res_rb_tree, hammer_reserve);
RB_HEAD(hammer_mod_rb_tree, hammer_io);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	      hammer_buf_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_nod_rb_tree, hammer_node, rb_node,
	      hammer_nod_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_und_rb_tree, hammer_undo, rb_node,
	      hammer_und_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_res_rb_tree, hammer_reserve, rb_node,
	      hammer_res_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_mod_rb_tree, hammer_io, rb_node,
	      hammer_mod_rb_compare, hammer_off_t);

/*
 * IO management - embedded at the head of various in-memory structures
 *
 * VOLUME	- hammer_volume containing meta-data
 * META_BUFFER	- hammer_buffer containing meta-data
 * DATA_BUFFER	- hammer_buffer containing pure-data
 *
 * Dirty volume headers and dirty meta-data buffers are locked until the
 * flusher can sequence them out.  Dirty pure-data buffers can be written.
 * Clean buffers can be passively released.
 */
typedef enum hammer_io_type {
	HAMMER_STRUCTURE_VOLUME,
	HAMMER_STRUCTURE_META_BUFFER,
	HAMMER_STRUCTURE_UNDO_BUFFER,
	HAMMER_STRUCTURE_DATA_BUFFER,
	HAMMER_STRUCTURE_DUMMY
} hammer_io_type_t;

union hammer_io_structure;
struct hammer_io;

struct worklist {
	LIST_ENTRY(worklist) node;
};

TAILQ_HEAD(hammer_io_list, hammer_io);
typedef struct hammer_io_list *hammer_io_list_t;

struct hammer_io {
	struct worklist		worklist;
	struct hammer_lock	lock;
	enum hammer_io_type	type;
	struct hammer_mount	*hmp;
	struct hammer_volume	*volume;
	RB_ENTRY(hammer_io)	rb_node;     /* if modified */
	TAILQ_ENTRY(hammer_io)	iorun_entry; /* iorun_list */
	struct hammer_mod_rb_tree *mod_root;
	struct buf		*bp;
	int64_t			offset;	   /* zone-2 offset */
	int			bytes;	   /* buffer cache buffer size */
	int			modify_refs;

	/*
	 * These can be modified at any time by the backend while holding
	 * io_token, due to bio_done and hammer_io_complete() callbacks.
	 */
	u_int		running : 1;	/* bp write IO in progress */
	u_int		waiting : 1;	/* someone is waiting on us */
	u_int		ioerror : 1;	/* abort on io-error */
	u_int		unusedA : 29;

	/*
	 * These can only be modified by the frontend while holding
	 * fs_token, or by the backend while holding the io interlocked
	 * with no references (which will block the frontend when it
	 * tries to reference it).
	 *
	 * WARNING! SMP RACES will create havoc if the callbacks ever tried
	 *	    to modify any of these outside the above restrictions.
	 */
	u_int		modified : 1;	/* bp's data was modified */
	u_int		released : 1;	/* bp released (w/ B_LOCKED set) */
	u_int		validated : 1;	/* ondisk has been validated */
	u_int		waitdep : 1;	/* flush waits for dependancies */
	u_int		recovered : 1;	/* has recovery ref */
	u_int		waitmod : 1;	/* waiting for modify_refs */
	u_int		reclaim : 1;	/* reclaim requested */
	u_int		gencrc : 1;	/* crc needs to be generated */
	u_int		unusedB : 24;
};

typedef struct hammer_io *hammer_io_t;

#define HAMMER_CLUSTER_SIZE	(64 * 1024)
#if HAMMER_CLUSTER_SIZE > MAXBSIZE
#undef  HAMMER_CLUSTER_SIZE
#define HAMMER_CLUSTER_SIZE	MAXBSIZE
#endif
#define HAMMER_CLUSTER_BUFS	(HAMMER_CLUSTER_SIZE / HAMMER_BUFSIZE)

/*
 * In-memory volume representing on-disk buffer
 */
struct hammer_volume {
	struct hammer_io io;
	RB_ENTRY(hammer_volume) rb_node;
	struct hammer_volume_ondisk *ondisk;
	int32_t	vol_no;
	int64_t nblocks;	/* note: special calculation for statfs */
	int64_t buffer_base;	/* base offset of buffer 0 */
	hammer_off_t maxbuf_off; /* Maximum buffer offset (zone-2) */
	hammer_off_t maxraw_off; /* Maximum raw offset for device */
	char	*vol_name;
	struct vnode *devvp;
	int	vol_flags;
};

typedef struct hammer_volume *hammer_volume_t;

/*
 * In-memory buffer (other then volume, super-cluster, or cluster),
 * representing an on-disk buffer.
 */
struct hammer_buffer {
	struct hammer_io io;
	RB_ENTRY(hammer_buffer) rb_node;
	void *ondisk;
	hammer_off_t zoneX_offset;
	hammer_off_t zone2_offset;
	struct hammer_reserve *resv;
	struct hammer_node_list clist;
};

typedef struct hammer_buffer *hammer_buffer_t;

/*
 * In-memory B-Tree node, representing an on-disk B-Tree node.
 *
 * This is a hang-on structure which is backed by a hammer_buffer,
 * indexed by a hammer_cluster, and used for fine-grained locking of
 * B-Tree nodes in order to properly control lock ordering.  A hammer_buffer
 * can contain multiple nodes representing wildly disassociated portions
 * of the B-Tree so locking cannot be done on a buffer-by-buffer basis.
 *
 * This structure uses a cluster-relative index to reduce the number
 * of layers required to access it, and also because all on-disk B-Tree
 * references are cluster-relative offsets.
 */
struct hammer_node {
	struct hammer_lock	lock;		/* node-by-node lock */
	TAILQ_ENTRY(hammer_node) entry;		/* per-buffer linkage */
	RB_ENTRY(hammer_node)	rb_node;	/* per-cluster linkage */
	hammer_off_t		node_offset;	/* full offset spec */
	struct hammer_mount	*hmp;
	struct hammer_buffer	*buffer;	/* backing buffer */
	hammer_node_ondisk_t	ondisk;		/* ptr to on-disk structure */
	TAILQ_HEAD(, hammer_cursor) cursor_list;  /* deadlock recovery */
	struct hammer_node_cache_list cache_list; /* passive caches */
	int			flags;
	int			cursor_exclreq_count;
};

#define HAMMER_NODE_DELETED	0x0001
#define HAMMER_NODE_FLUSH	0x0002
#define HAMMER_NODE_CRCGOOD	0x0004
#define HAMMER_NODE_NEEDSCRC	0x0008
#define HAMMER_NODE_NEEDSMIRROR	0x0010
#define HAMMER_NODE_CRCBAD	0x0020
#define HAMMER_NODE_NONLINEAR	0x0040		/* linear heuristic */

#define HAMMER_NODE_CRCANY	(HAMMER_NODE_CRCGOOD | HAMMER_NODE_CRCBAD)

typedef struct hammer_node	*hammer_node_t;

/*
 * List of locked nodes.  This structure is used to lock potentially large
 * numbers of nodes as an aid for complex B-Tree operations.
 */
struct hammer_node_lock;
TAILQ_HEAD(hammer_node_lock_list, hammer_node_lock);

struct hammer_node_lock {
	TAILQ_ENTRY(hammer_node_lock) entry;
	struct hammer_node_lock_list  list;
	struct hammer_node_lock	      *parent;
	hammer_node_t	node;
	hammer_node_ondisk_t copy;	/* copy of on-disk data */
	int		index;		/* index of this node in parent */
	int		count;		/* count children */
	int		flags;
};

typedef struct hammer_node_lock *hammer_node_lock_t;

#define HAMMER_NODE_LOCK_UPDATED	0x0001
#define HAMMER_NODE_LOCK_LCACHE		0x0002

/*
 * Common I/O management structure - embedded in in-memory structures
 * which are backed by filesystem buffers.
 */
union hammer_io_structure {
	struct hammer_io	io;
	struct hammer_volume	volume;
	struct hammer_buffer	buffer;
};

typedef union hammer_io_structure *hammer_io_structure_t;

/*
 * The reserve structure prevents the blockmap from allocating
 * out of a reserved bigblock.  Such reservations are used by
 * the direct-write mechanism.
 *
 * The structure is also used to hold off on reallocations of
 * big blocks from the freemap until flush dependancies have
 * been dealt with.
 */
struct hammer_reserve {
	RB_ENTRY(hammer_reserve) rb_node;
	TAILQ_ENTRY(hammer_reserve) delay_entry;
	int		flush_group;
	int		flags;
	int		refs;
	int		zone;
	int		append_off;
	int32_t		bytes_free;
	hammer_off_t	zone_offset;
};

typedef struct hammer_reserve *hammer_reserve_t;

#define HAMMER_RESF_ONDELAY	0x0001
#define HAMMER_RESF_LAYER2FREE	0x0002

#include "hammer_cursor.h"

/*
 * The undo structure tracks recent undos to avoid laying down duplicate
 * undos within a flush group, saving us a significant amount of overhead.
 *
 * This is strictly a heuristic.
 */
#define HAMMER_MAX_UNDOS		1024
#define HAMMER_MAX_FLUSHERS		4

struct hammer_undo {
	RB_ENTRY(hammer_undo)	rb_node;
	TAILQ_ENTRY(hammer_undo) lru_entry;
	hammer_off_t		offset;
	int			bytes;
};

typedef struct hammer_undo *hammer_undo_t;

struct hammer_flusher_info;
TAILQ_HEAD(hammer_flusher_info_list, hammer_flusher_info);

struct hammer_flusher {
	int		signal;		/* flusher thread sequencer */
	int		done;		/* last completed flush group */
	int		next;		/* next unallocated flg seqno */
	int		group_lock;	/* lock sequencing of the next flush */
	int		exiting;	/* request master exit */
	thread_t	td;		/* master flusher thread */
	hammer_tid_t	tid;		/* last flushed transaction id */
	int		finalize_want;		/* serialize finalization */
	struct hammer_lock finalize_lock;	/* serialize finalization */
	struct hammer_transaction trans;	/* shared transaction */
	struct hammer_flusher_info_list run_list;
	struct hammer_flusher_info_list ready_list;
};

#define HAMMER_FLUSH_UNDOS_RELAXED	0
#define HAMMER_FLUSH_UNDOS_FORCED	1
#define HAMMER_FLUSH_UNDOS_AUTO		2
/*
 * Internal hammer mount data structure
 */
struct hammer_mount {
	struct mount *mp;
	/*struct vnode *rootvp;*/
	struct hammer_ino_rb_tree rb_inos_root;
	struct hammer_redo_rb_tree rb_redo_root;
	struct hammer_vol_rb_tree rb_vols_root;
	struct hammer_nod_rb_tree rb_nods_root;
	struct hammer_und_rb_tree rb_undo_root;
	struct hammer_res_rb_tree rb_resv_root;
	struct hammer_buf_rb_tree rb_bufs_root;
	struct hammer_pfs_rb_tree rb_pfsm_root;

	struct hammer_dedup_crc_rb_tree rb_dedup_crc_root;
	struct hammer_dedup_off_rb_tree rb_dedup_off_root;

	struct hammer_volume *rootvol;
	struct hammer_base_elm root_btree_beg;
	struct hammer_base_elm root_btree_end;

	struct malloc_type	*m_misc;
	struct malloc_type	*m_inodes;

	int	flags;		/* HAMMER_MOUNT_xxx flags */
	int	hflags;
	int	ronly;
	int	nvolumes;
	int	volume_iterator;
	int	master_id;	/* -1 or 0-15 - clustering and mirroring */
	int	version;	/* hammer filesystem version to use */
	int	rsv_inodes;	/* reserved space due to dirty inodes */
	int64_t	rsv_databytes;	/* reserved space due to record data */
	int	rsv_recs;	/* reserved space due to dirty records */
	int	rsv_fromdelay;	/* bigblocks reserved due to flush delay */
	int	undo_rec_limit;	/* based on size of undo area */
	int	last_newrecords;
	int	count_newrecords;

	int	volume_to_remove; /* volume that is currently being removed */

	int	count_inodes;	/* total number of inodes */
	int	count_iqueued;	/* inodes queued to flusher */
	int	count_reclaims; /* inodes pending reclaim by flusher */

	struct hammer_flusher flusher;

	u_int	check_interrupt;
	u_int	check_yield;
	uuid_t	fsid;
	struct hammer_mod_rb_tree volu_root;	/* dirty undo buffers */
	struct hammer_mod_rb_tree undo_root;	/* dirty undo buffers */
	struct hammer_mod_rb_tree data_root;	/* dirty data buffers */
	struct hammer_mod_rb_tree meta_root;	/* dirty meta bufs    */
	struct hammer_mod_rb_tree lose_root;	/* loose buffers      */
	long	locked_dirty_space;		/* meta/volu count    */
	long	io_running_space;		/* io_token */
	int	unused01;
	int	objid_cache_count;
	int	dedup_cache_count;
	int	error;				/* critical I/O error */
	struct krate	krate;			/* rate limited kprintf */
	hammer_tid_t	asof;			/* snapshot mount */
	hammer_tid_t	next_tid;
	hammer_tid_t	flush_tid1;		/* flusher tid sequencing */
	hammer_tid_t	flush_tid2;		/* flusher tid sequencing */
	int64_t copy_stat_freebigblocks;	/* number of free bigblocks */
	u_int32_t	undo_seqno;		/* UNDO/REDO FIFO seqno */
	u_int32_t	recover_stage2_seqno;	/* REDO recovery seqno */
	hammer_off_t	recover_stage2_offset;	/* REDO recovery offset */

	struct netexport export;
	struct hammer_lock sync_lock;
	struct hammer_lock free_lock;
	struct hammer_lock undo_lock;
	struct hammer_lock blkmap_lock;
	struct hammer_lock snapshot_lock;
	struct hammer_lock volume_lock;
	struct hammer_blockmap  blockmap[HAMMER_MAX_ZONES];
	struct hammer_undo	undos[HAMMER_MAX_UNDOS];
	int			undo_alloc;
	TAILQ_HEAD(, hammer_undo)  undo_lru_list;
	TAILQ_HEAD(, hammer_reserve) delay_list;
	struct hammer_flush_group_list	flush_group_list;
	hammer_flush_group_t	fill_flush_group;
	hammer_flush_group_t	next_flush_group;
	TAILQ_HEAD(, hammer_objid_cache) objid_cache_list;
	TAILQ_HEAD(, hammer_dedup_cache) dedup_lru_list;
	hammer_dedup_cache_t	dedup_free_cache;
	TAILQ_HEAD(, hammer_reclaim) reclaim_list;
	TAILQ_HEAD(, hammer_io) iorun_list;

	struct lwkt_token	fs_token;	/* high level */
	struct lwkt_token	io_token;	/* low level (IO callback) */

	struct hammer_inostats	inostats[HAMMER_INOSTATS_HSIZE];
};

typedef struct hammer_mount	*hammer_mount_t;

#define HAMMER_MOUNT_CRITICAL_ERROR	0x0001
#define HAMMER_MOUNT_FLUSH_RECOVERY	0x0002
#define HAMMER_MOUNT_REDO_SYNC		0x0004
#define HAMMER_MOUNT_REDO_RECOVERY_REQ	0x0008
#define HAMMER_MOUNT_REDO_RECOVERY_RUN	0x0010

struct hammer_sync_info {
	int error;
	int waitfor;
};

/*
 * Minium buffer cache bufs required to rebalance the B-Tree.
 * This is because we must hold the children and the children's children
 * locked.  Even this might not be enough if things are horribly out
 * of balance.
 */
#define HAMMER_REBALANCE_MIN_BUFS	\
	(HAMMER_BTREE_LEAF_ELMS * HAMMER_BTREE_LEAF_ELMS)


#endif

/*
 * checkspace slop (8MB chunks), higher numbers are more conservative.
 */
#define HAMMER_CHKSPC_REBLOCK	25
#define HAMMER_CHKSPC_MIRROR	20
#define HAMMER_CHKSPC_WRITE	20
#define HAMMER_CHKSPC_CREATE	20
#define HAMMER_CHKSPC_REMOVE	10
#define HAMMER_CHKSPC_EMERGENCY	0

#if defined(_KERNEL)

extern struct vop_ops hammer_vnode_vops;
extern struct vop_ops hammer_spec_vops;
extern struct vop_ops hammer_fifo_vops;
extern struct bio_ops hammer_bioops;

extern int hammer_debug_io;
extern int hammer_debug_general;
extern int hammer_debug_debug;
extern int hammer_debug_inode;
extern int hammer_debug_locks;
extern int hammer_debug_btree;
extern int hammer_debug_tid;
extern int hammer_debug_recover;
extern int hammer_debug_recover_faults;
extern int hammer_debug_critical;
extern int hammer_cluster_enable;
extern int hammer_live_dedup;
extern int hammer_tdmux_ticks;
extern int hammer_count_fsyncs;
extern int hammer_count_inodes;
extern int hammer_count_iqueued;
extern int hammer_count_reclaims;
extern int hammer_count_records;
extern int hammer_count_record_datas;
extern int hammer_count_volumes;
extern int hammer_count_buffers;
extern int hammer_count_nodes;
extern int64_t hammer_count_extra_space_used;
extern int64_t hammer_stats_btree_lookups;
extern int64_t hammer_stats_btree_searches;
extern int64_t hammer_stats_btree_inserts;
extern int64_t hammer_stats_btree_deletes;
extern int64_t hammer_stats_btree_elements;
extern int64_t hammer_stats_btree_splits;
extern int64_t hammer_stats_btree_iterations;
extern int64_t hammer_stats_btree_root_iterations;
extern int64_t hammer_stats_record_iterations;
extern int64_t hammer_stats_file_read;
extern int64_t hammer_stats_file_write;
extern int64_t hammer_stats_file_iopsr;
extern int64_t hammer_stats_file_iopsw;
extern int64_t hammer_stats_disk_read;
extern int64_t hammer_stats_disk_write;
extern int64_t hammer_stats_inode_flushes;
extern int64_t hammer_stats_commits;
extern int64_t hammer_stats_undo;
extern int64_t hammer_stats_redo;
extern long hammer_count_dirtybufspace;
extern int hammer_count_refedbufs;
extern int hammer_count_reservations;
extern long hammer_count_io_running_read;
extern long hammer_count_io_running_write;
extern int hammer_count_io_locked;
extern long hammer_limit_dirtybufspace;
extern int hammer_limit_recs;
extern int hammer_limit_inode_recs;
extern int hammer_limit_reclaims;
extern int hammer_live_dedup_cache_size;
extern int hammer_limit_redo;
extern int hammer_bio_count;
extern int hammer_verify_zone;
extern int hammer_verify_data;
extern int hammer_write_mode;
extern int hammer_double_buffer;
extern int hammer_btree_full_undo;
extern int hammer_yield_check;
extern int hammer_fsync_mode;
extern int hammer_autoflush;
extern int64_t hammer_contention_count;

extern int64_t hammer_live_dedup_vnode_bcmps;
extern int64_t hammer_live_dedup_device_bcmps;
extern int64_t hammer_live_dedup_findblk_failures;
extern int64_t hammer_live_dedup_bmap_saves;

void	hammer_critical_error(hammer_mount_t hmp, hammer_inode_t ip,
			int error, const char *msg);
int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_get_vnode(struct hammer_inode *ip, struct vnode **vpp);
struct hammer_inode *hammer_get_inode(hammer_transaction_t trans,
			hammer_inode_t dip, int64_t obj_id,
			hammer_tid_t asof, u_int32_t localization,
			int flags, int *errorp);
struct hammer_inode *hammer_get_dummy_inode(hammer_transaction_t trans,
			hammer_inode_t dip, int64_t obj_id,
			hammer_tid_t asof, u_int32_t localization,
			int flags, int *errorp);
struct hammer_inode *hammer_find_inode(hammer_transaction_t trans,
			int64_t obj_id, hammer_tid_t asof,
			u_int32_t localization);
void	hammer_scan_inode_snapshots(hammer_mount_t hmp,
			hammer_inode_info_t iinfo,
			int (*callback)(hammer_inode_t ip, void *data),
			void *data);
void	hammer_put_inode(struct hammer_inode *ip);
void	hammer_put_inode_ref(struct hammer_inode *ip);
void	hammer_inode_waitreclaims(hammer_transaction_t trans);
void	hammer_inode_dirty(struct hammer_inode *ip);

int	hammer_unload_volume(hammer_volume_t volume, void *data __unused);
int	hammer_adjust_volume_mode(hammer_volume_t volume, void *data __unused);

int	hammer_unload_buffer(hammer_buffer_t buffer, void *data);
int	hammer_install_volume(hammer_mount_t hmp, const char *volname,
			struct vnode *devvp);
int	hammer_mountcheck_volumes(hammer_mount_t hmp);

int	hammer_mem_add(hammer_record_t record);
int	hammer_ip_lookup(hammer_cursor_t cursor);
int	hammer_ip_first(hammer_cursor_t cursor);
int	hammer_ip_next(hammer_cursor_t cursor);
int	hammer_ip_resolve_data(hammer_cursor_t cursor);
int	hammer_ip_delete_record(hammer_cursor_t cursor, hammer_inode_t ip,
			hammer_tid_t tid);
int	hammer_create_at_cursor(hammer_cursor_t cursor,
			hammer_btree_leaf_elm_t leaf, void *udata, int mode);
int	hammer_delete_at_cursor(hammer_cursor_t cursor, int delete_flags,
			hammer_tid_t delete_tid, u_int32_t delete_ts,
			int track, int64_t *stat_bytes);
int	hammer_ip_check_directory_empty(hammer_transaction_t trans,
			hammer_inode_t ip);
int	hammer_sync_hmp(hammer_mount_t hmp, int waitfor);
int	hammer_queue_inodes_flusher(hammer_mount_t hmp, int waitfor);

hammer_record_t
	hammer_alloc_mem_record(hammer_inode_t ip, int data_len);
void	hammer_flush_record_done(hammer_record_t record, int error);
void	hammer_wait_mem_record_ident(hammer_record_t record, const char *ident);
void	hammer_rel_mem_record(hammer_record_t record);

int	hammer_cursor_up(hammer_cursor_t cursor);
int	hammer_cursor_up_locked(hammer_cursor_t cursor);
int	hammer_cursor_down(hammer_cursor_t cursor);
int	hammer_cursor_upgrade(hammer_cursor_t cursor);
int	hammer_cursor_upgrade_node(hammer_cursor_t cursor);
void	hammer_cursor_downgrade(hammer_cursor_t cursor);
int	hammer_cursor_upgrade2(hammer_cursor_t c1, hammer_cursor_t c2);
void	hammer_cursor_downgrade2(hammer_cursor_t c1, hammer_cursor_t c2);
int	hammer_cursor_seek(hammer_cursor_t cursor, hammer_node_t node,
			int index);
void	hammer_lock_ex_ident(struct hammer_lock *lock, const char *ident);
int	hammer_lock_ex_try(struct hammer_lock *lock);
void	hammer_lock_sh(struct hammer_lock *lock);
int	hammer_lock_sh_try(struct hammer_lock *lock);
int	hammer_lock_upgrade(struct hammer_lock *lock, int shcount);
void	hammer_lock_downgrade(struct hammer_lock *lock, int shcount);
int	hammer_lock_status(struct hammer_lock *lock);
void	hammer_unlock(struct hammer_lock *lock);
void	hammer_ref(struct hammer_lock *lock);
int	hammer_ref_interlock(struct hammer_lock *lock);
int	hammer_ref_interlock_true(struct hammer_lock *lock);
void	hammer_ref_interlock_done(struct hammer_lock *lock);
void	hammer_rel(struct hammer_lock *lock);
int	hammer_rel_interlock(struct hammer_lock *lock, int locked);
void	hammer_rel_interlock_done(struct hammer_lock *lock, int orig_locked);
int	hammer_get_interlock(struct hammer_lock *lock);
int	hammer_try_interlock_norefs(struct hammer_lock *lock);
void	hammer_put_interlock(struct hammer_lock *lock, int error);

void	hammer_sync_lock_ex(hammer_transaction_t trans);
void	hammer_sync_lock_sh(hammer_transaction_t trans);
int	hammer_sync_lock_sh_try(hammer_transaction_t trans);
void	hammer_sync_unlock(hammer_transaction_t trans);

u_int32_t hammer_to_unix_xid(uuid_t *uuid);
void hammer_guid_to_uuid(uuid_t *uuid, u_int32_t guid);
void	hammer_time_to_timespec(u_int64_t xtime, struct timespec *ts);
u_int64_t hammer_timespec_to_time(struct timespec *ts);
int	hammer_str_to_tid(const char *str, int *ispfsp,
			hammer_tid_t *tidp, u_int32_t *localizationp);
int	hammer_is_atatext(const char *name, int len);
hammer_tid_t hammer_alloc_objid(hammer_mount_t hmp, hammer_inode_t dip,
			int64_t namekey);
void hammer_clear_objid(hammer_inode_t dip);
void hammer_destroy_objid_cache(hammer_mount_t hmp);

int hammer_dedup_crc_rb_compare(hammer_dedup_cache_t dc1,
			hammer_dedup_cache_t dc2);
int hammer_dedup_off_rb_compare(hammer_dedup_cache_t dc1,
			hammer_dedup_cache_t dc2);
hammer_dedup_cache_t hammer_dedup_cache_add(hammer_inode_t ip,
			hammer_btree_leaf_elm_t leaf);
hammer_dedup_cache_t hammer_dedup_cache_lookup(hammer_mount_t hmp,
			hammer_crc_t crc);
void hammer_dedup_cache_inval(hammer_mount_t hmp, hammer_off_t base_offset);
void hammer_destroy_dedup_cache(hammer_mount_t hmp);
void hammer_dump_dedup_cache(hammer_mount_t hmp);
int hammer_dedup_validate(hammer_dedup_cache_t dcp, int zone, int bytes,
			void *data);

int hammer_enter_undo_history(hammer_mount_t hmp, hammer_off_t offset,
			int bytes);
void hammer_clear_undo_history(hammer_mount_t hmp);
enum vtype hammer_get_vnode_type(u_int8_t obj_type);
int hammer_get_dtype(u_int8_t obj_type);
u_int8_t hammer_get_obj_type(enum vtype vtype);
int64_t hammer_directory_namekey(hammer_inode_t dip, const void *name, int len,
			u_int32_t *max_iterationsp);
int	hammer_nohistory(hammer_inode_t ip);

int	hammer_init_cursor(hammer_transaction_t trans, hammer_cursor_t cursor,
			hammer_node_cache_t cache, hammer_inode_t ip);
void	hammer_normalize_cursor(hammer_cursor_t cursor);
void	hammer_done_cursor(hammer_cursor_t cursor);
int	hammer_recover_cursor(hammer_cursor_t cursor);
void	hammer_unlock_cursor(hammer_cursor_t cursor);
int	hammer_lock_cursor(hammer_cursor_t cursor);
hammer_cursor_t	hammer_push_cursor(hammer_cursor_t ocursor);
void	hammer_pop_cursor(hammer_cursor_t ocursor, hammer_cursor_t ncursor);

void	hammer_cursor_replaced_node(hammer_node_t onode, hammer_node_t nnode);
void	hammer_cursor_removed_node(hammer_node_t onode, hammer_node_t parent,
			int index);
void	hammer_cursor_split_node(hammer_node_t onode, hammer_node_t nnode,
			int index);
void	hammer_cursor_moved_element(hammer_node_t oparent, int pindex,
			hammer_node_t onode, int oindex,
			hammer_node_t nnode, int nindex);
void	hammer_cursor_parent_changed(hammer_node_t node, hammer_node_t oparent,
			hammer_node_t nparent, int nindex);
void	hammer_cursor_inserted_element(hammer_node_t node, int index);
void	hammer_cursor_deleted_element(hammer_node_t node, int index);
void	hammer_cursor_invalidate_cache(hammer_cursor_t cursor);

int	hammer_btree_lookup(hammer_cursor_t cursor);
int	hammer_btree_first(hammer_cursor_t cursor);
int	hammer_btree_last(hammer_cursor_t cursor);
int	hammer_btree_extract(hammer_cursor_t cursor, int flags);
int	hammer_btree_iterate(hammer_cursor_t cursor);
int	hammer_btree_iterate_reverse(hammer_cursor_t cursor);
int	hammer_btree_insert(hammer_cursor_t cursor,
			    hammer_btree_leaf_elm_t elm, int *doprop);
int	hammer_btree_delete(hammer_cursor_t cursor, int *ndelete);
void	hammer_btree_do_propagation(hammer_cursor_t cursor,
			    hammer_pseudofs_inmem_t pfsm,
			    hammer_btree_leaf_elm_t leaf);
int	hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
int	hammer_btree_chkts(hammer_tid_t ts, hammer_base_elm_t key);
int	hammer_btree_correct_rhb(hammer_cursor_t cursor, hammer_tid_t tid);
int	hammer_btree_correct_lhb(hammer_cursor_t cursor, hammer_tid_t tid);

int	btree_set_parent(hammer_transaction_t trans, hammer_node_t node,
                        hammer_btree_elm_t elm);
void	hammer_node_lock_init(hammer_node_lock_t parent, hammer_node_t node);
void	hammer_btree_lcache_init(hammer_mount_t hmp, hammer_node_lock_t lcache,
			int depth);
void	hammer_btree_lcache_free(hammer_mount_t hmp, hammer_node_lock_t lcache);
int	hammer_btree_lock_children(hammer_cursor_t cursor, int depth,
			hammer_node_lock_t parent,
			hammer_node_lock_t lcache);
void	hammer_btree_lock_copy(hammer_cursor_t cursor,
			hammer_node_lock_t parent);
int	hammer_btree_sync_copy(hammer_cursor_t cursor,
			hammer_node_lock_t parent);
void	hammer_btree_unlock_children(hammer_mount_t hmp,
			hammer_node_lock_t parent,
			hammer_node_lock_t lcache);
int	hammer_btree_search_node(hammer_base_elm_t elm, hammer_node_ondisk_t node);
hammer_node_t hammer_btree_get_parent(hammer_transaction_t trans,
			hammer_node_t node, int *parent_indexp,
			int *errorp, int try_exclusive);

void	hammer_print_btree_node(hammer_node_ondisk_t ondisk);
void	hammer_print_btree_elm(hammer_btree_elm_t elm, u_int8_t type, int i);

void	*hammer_bread(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);
void	*hammer_bnew(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);
void	*hammer_bread_ext(struct hammer_mount *hmp, hammer_off_t off, int bytes,
			int *errorp, struct hammer_buffer **bufferp);
void	*hammer_bnew_ext(struct hammer_mount *hmp, hammer_off_t off, int bytes,
			int *errorp, struct hammer_buffer **bufferp);

hammer_volume_t hammer_get_root_volume(hammer_mount_t hmp, int *errorp);

hammer_volume_t	hammer_get_volume(hammer_mount_t hmp,
			int32_t vol_no, int *errorp);
hammer_buffer_t	hammer_get_buffer(hammer_mount_t hmp, hammer_off_t buf_offset,
			int bytes, int isnew, int *errorp);
void		hammer_sync_buffers(hammer_mount_t hmp,
			hammer_off_t base_offset, int bytes);
int		hammer_del_buffers(hammer_mount_t hmp,
			hammer_off_t base_offset,
			hammer_off_t zone2_offset, int bytes,
			int report_conflicts);

int		hammer_ref_volume(hammer_volume_t volume);
int		hammer_ref_buffer(hammer_buffer_t buffer);
void		hammer_flush_buffer_nodes(hammer_buffer_t buffer);

void		hammer_rel_volume(hammer_volume_t volume, int locked);
void		hammer_rel_buffer(hammer_buffer_t buffer, int locked);

int		hammer_vfs_export(struct mount *mp, int op,
			const struct export_args *export);
hammer_node_t	hammer_get_node(hammer_transaction_t trans,
			hammer_off_t node_offset, int isnew, int *errorp);
void		hammer_ref_node(hammer_node_t node);
hammer_node_t	hammer_ref_node_safe(hammer_transaction_t trans,
			hammer_node_cache_t cache, int *errorp);
void		hammer_rel_node(hammer_node_t node);
void		hammer_delete_node(hammer_transaction_t trans,
			hammer_node_t node);
void		hammer_cache_node(hammer_node_cache_t cache,
			hammer_node_t node);
void		hammer_uncache_node(hammer_node_cache_t cache);
void		hammer_flush_node(hammer_node_t node, int locked);

void hammer_dup_buffer(struct hammer_buffer **bufferp,
			struct hammer_buffer *buffer);
hammer_node_t hammer_alloc_btree(hammer_transaction_t trans,
			hammer_off_t hint, int *errorp);
void *hammer_alloc_data(hammer_transaction_t trans, int32_t data_len,
			u_int16_t rec_type, hammer_off_t *data_offsetp,
			struct hammer_buffer **data_bufferp,
			hammer_off_t hint, int *errorp);

int hammer_generate_undo(hammer_transaction_t trans,
			hammer_off_t zone_offset, void *base, int len);
int hammer_generate_redo(hammer_transaction_t trans, hammer_inode_t ip,
			hammer_off_t file_offset, u_int32_t flags,
			void *base, int len);
void hammer_generate_redo_sync(hammer_transaction_t trans);
void hammer_redo_fifo_start_flush(hammer_inode_t ip);
void hammer_redo_fifo_end_flush(hammer_inode_t ip);

void hammer_format_undo(void *base, u_int32_t seqno);
int hammer_upgrade_undo_4(hammer_transaction_t trans);

void hammer_put_volume(struct hammer_volume *volume, int flush);
void hammer_put_buffer(struct hammer_buffer *buffer, int flush);

hammer_off_t hammer_freemap_alloc(hammer_transaction_t trans,
			hammer_off_t owner, int *errorp);
void hammer_freemap_free(hammer_transaction_t trans, hammer_off_t phys_offset,
			hammer_off_t owner, int *errorp);
int _hammer_checkspace(hammer_mount_t hmp, int slop, int64_t *resp);
hammer_off_t hammer_blockmap_alloc(hammer_transaction_t trans, int zone,
			int bytes, hammer_off_t hint, int *errorp);
hammer_reserve_t hammer_blockmap_reserve(hammer_mount_t hmp, int zone,
			int bytes, hammer_off_t *zone_offp, int *errorp);
hammer_reserve_t hammer_blockmap_reserve_dedup(hammer_mount_t hmp, int zone,
			int bytes, hammer_off_t zone_offset, int *errorp);
void hammer_blockmap_reserve_complete(hammer_mount_t hmp,
			hammer_reserve_t resv);
void hammer_reserve_clrdelay(hammer_mount_t hmp, hammer_reserve_t resv);
void hammer_blockmap_free(hammer_transaction_t trans,
			hammer_off_t zone_offset, int bytes);
int hammer_blockmap_dedup(hammer_transaction_t trans,
			hammer_off_t zone_offset, int bytes);
int hammer_blockmap_finalize(hammer_transaction_t trans,
			hammer_reserve_t resv,
			hammer_off_t zone_offset, int bytes);
int hammer_blockmap_getfree(hammer_mount_t hmp, hammer_off_t zone_offset,
			int *curp, int *errorp);
hammer_off_t hammer_blockmap_lookup_verify(hammer_mount_t hmp,
			hammer_off_t zone_offset, int *errorp);

hammer_off_t hammer_undo_lookup(hammer_mount_t hmp, hammer_off_t zone_offset,
			int *errorp);
int64_t hammer_undo_used(hammer_transaction_t trans);
int64_t hammer_undo_space(hammer_transaction_t trans);
int64_t hammer_undo_max(hammer_mount_t hmp);
int hammer_undo_reclaim(hammer_io_t io);

void hammer_start_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_simple_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_start_transaction_fls(struct hammer_transaction *trans,
			          struct hammer_mount *hmp);
void hammer_done_transaction(struct hammer_transaction *trans);
hammer_tid_t hammer_alloc_tid(hammer_mount_t hmp, int count);

void hammer_modify_inode(hammer_transaction_t trans, hammer_inode_t ip, int flags);
void hammer_flush_inode(hammer_inode_t ip, int flags);
void hammer_flush_inode_done(hammer_inode_t ip, int error);
void hammer_wait_inode(hammer_inode_t ip);

int  hammer_create_inode(struct hammer_transaction *trans, struct vattr *vap,
			struct ucred *cred, struct hammer_inode *dip,
			const char *name, int namelen,
			hammer_pseudofs_inmem_t pfsm,
			struct hammer_inode **ipp);
void hammer_rel_inode(hammer_inode_t ip, int flush);
int hammer_reload_inode(hammer_inode_t ip, void *arg __unused);
int hammer_ino_rb_compare(hammer_inode_t ip1, hammer_inode_t ip2);
int hammer_redo_rb_compare(hammer_inode_t ip1, hammer_inode_t ip2);
int hammer_destroy_inode_callback(hammer_inode_t ip, void *data __unused);

int hammer_sync_inode(hammer_transaction_t trans, hammer_inode_t ip);
void hammer_test_inode(hammer_inode_t dip);
void hammer_inode_unloadable_check(hammer_inode_t ip, int getvp);
int hammer_update_atime_quick(hammer_inode_t ip);

int  hammer_ip_add_directory(struct hammer_transaction *trans,
			hammer_inode_t dip, const char *name, int bytes,
			hammer_inode_t nip);
int  hammer_ip_del_directory(struct hammer_transaction *trans,
			hammer_cursor_t cursor, hammer_inode_t dip,
			hammer_inode_t ip);
void hammer_ip_replace_bulk(hammer_mount_t hmp, hammer_record_t record);
hammer_record_t hammer_ip_add_bulk(hammer_inode_t ip, off_t file_offset,
			void *data, int bytes, int *errorp);
int  hammer_ip_frontend_trunc(struct hammer_inode *ip, off_t file_size);
int  hammer_ip_add_record(struct hammer_transaction *trans,
			hammer_record_t record);
int  hammer_ip_delete_range(hammer_cursor_t cursor, hammer_inode_t ip,
			int64_t ran_beg, int64_t ran_end, int truncating);
int  hammer_ip_delete_clean(hammer_cursor_t cursor, hammer_inode_t ip,
			int *countp);
int  hammer_ip_sync_data(hammer_cursor_t cursor, hammer_inode_t ip,
			int64_t offset, void *data, int bytes);
int  hammer_ip_sync_record(hammer_transaction_t trans, hammer_record_t rec);
int  hammer_ip_sync_record_cursor(hammer_cursor_t cursor, hammer_record_t rec);
hammer_pseudofs_inmem_t  hammer_load_pseudofs(hammer_transaction_t trans,
			u_int32_t localization, int *errorp);
int  hammer_mkroot_pseudofs(hammer_transaction_t trans, struct ucred *cred,
			hammer_pseudofs_inmem_t pfsm);
int  hammer_save_pseudofs(hammer_transaction_t trans,
			hammer_pseudofs_inmem_t pfsm);
int  hammer_unload_pseudofs(hammer_transaction_t trans, u_int32_t localization);
void hammer_rel_pseudofs(hammer_mount_t hmp, hammer_pseudofs_inmem_t pfsm);
int hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
			struct ucred *cred);

void hammer_io_init(hammer_io_t io, hammer_volume_t volume,
			enum hammer_io_type type);
int hammer_io_read(struct vnode *devvp, struct hammer_io *io, int limit);
void hammer_io_advance(struct hammer_io *io);
int hammer_io_new(struct vnode *devvp, struct hammer_io *io);
int hammer_io_inval(hammer_volume_t volume, hammer_off_t zone2_offset);
struct buf *hammer_io_release(struct hammer_io *io, int flush);
void hammer_io_flush(struct hammer_io *io, int reclaim);
void hammer_io_wait(struct hammer_io *io);
void hammer_io_waitdep(struct hammer_io *io);
void hammer_io_wait_all(hammer_mount_t hmp, const char *ident, int doflush);
int hammer_io_direct_read(hammer_mount_t hmp, struct bio *bio,
			hammer_btree_leaf_elm_t leaf);
int hammer_io_indirect_read(hammer_mount_t hmp, struct bio *bio,
			hammer_btree_leaf_elm_t leaf);
int hammer_io_direct_write(hammer_mount_t hmp, struct bio *bio,
			hammer_record_t record);
void hammer_io_direct_wait(hammer_record_t record);
void hammer_io_direct_uncache(hammer_mount_t hmp, hammer_btree_leaf_elm_t leaf);
void hammer_io_write_interlock(hammer_io_t io);
void hammer_io_done_interlock(hammer_io_t io);
void hammer_io_clear_modify(struct hammer_io *io, int inval);
void hammer_io_clear_modlist(struct hammer_io *io);
void hammer_io_flush_sync(hammer_mount_t hmp);
void hammer_io_clear_error(struct hammer_io *io);
void hammer_io_clear_error_noassert(struct hammer_io *io);
void hammer_io_notmeta(hammer_buffer_t buffer);
void hammer_io_limit_backlog(hammer_mount_t hmp);

void hammer_modify_volume(hammer_transaction_t trans, hammer_volume_t volume,
			void *base, int len);
void hammer_modify_buffer(hammer_transaction_t trans, hammer_buffer_t buffer,
			void *base, int len);
void hammer_modify_volume_done(hammer_volume_t volume);
void hammer_modify_buffer_done(hammer_buffer_t buffer);

int hammer_ioc_reblock(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_reblock *reblock);
int hammer_ioc_rebalance(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_rebalance *rebal);
int hammer_ioc_prune(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_prune *prune);
int hammer_ioc_mirror_read(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_mirror_rw *mirror);
int hammer_ioc_mirror_write(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_mirror_rw *mirror);
int hammer_ioc_set_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct ucred *cred, struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_get_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_destroy_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_downgrade_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_upgrade_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_wait_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_pseudofs_rw *pfs);
int hammer_ioc_iterate_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pfs_iterate *pi);
int hammer_ioc_volume_add(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_volume *ioc);
int hammer_ioc_volume_del(hammer_transaction_t trans, hammer_inode_t ip,
                        struct hammer_ioc_volume *ioc);
int hammer_ioc_volume_list(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_volume_list *ioc);
int hammer_ioc_dedup(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_dedup *dedup);

int hammer_signal_check(hammer_mount_t hmp);

void hammer_flusher_create(hammer_mount_t hmp);
void hammer_flusher_destroy(hammer_mount_t hmp);
void hammer_flusher_sync(hammer_mount_t hmp);
int  hammer_flusher_async(hammer_mount_t hmp, hammer_flush_group_t flg);
int  hammer_flusher_async_one(hammer_mount_t hmp);
int hammer_flusher_running(hammer_mount_t hmp);
void hammer_flusher_wait(hammer_mount_t hmp, int seq);
void hammer_flusher_wait_next(hammer_mount_t hmp);
int  hammer_flusher_meta_limit(hammer_mount_t hmp);
int  hammer_flusher_meta_halflimit(hammer_mount_t hmp);
int  hammer_flusher_undo_exhausted(hammer_transaction_t trans, int quarter);
void hammer_flusher_clean_loose_ios(hammer_mount_t hmp);
void hammer_flusher_finalize(hammer_transaction_t trans, int final);
int  hammer_flusher_haswork(hammer_mount_t hmp);
void hammer_flusher_flush_undos(hammer_mount_t hmp, int already_flushed);

int hammer_recover_stage1(hammer_mount_t hmp, hammer_volume_t rootvol);
int hammer_recover_stage2(hammer_mount_t hmp, hammer_volume_t rootvol);
void hammer_recover_flush_buffers(hammer_mount_t hmp,
			hammer_volume_t root_volume, int final);

void hammer_crc_set_blockmap(hammer_blockmap_t blockmap);
void hammer_crc_set_volume(hammer_volume_ondisk_t ondisk);
void hammer_crc_set_leaf(void *data, hammer_btree_leaf_elm_t leaf);

int hammer_crc_test_blockmap(hammer_blockmap_t blockmap);
int hammer_crc_test_volume(hammer_volume_ondisk_t ondisk);
int hammer_crc_test_btree(hammer_node_ondisk_t ondisk);
int hammer_crc_test_leaf(void *data, hammer_btree_leaf_elm_t leaf);
void hkprintf(const char *ctl, ...) __printflike(1, 2);
udev_t hammer_fsid_to_udev(uuid_t *uuid);


int hammer_blocksize(int64_t file_offset);
int hammer_blockoff(int64_t file_offset);
int64_t hammer_blockdemarc(int64_t file_offset1, int64_t file_offset2);

/*
 * Shortcut for _hammer_checkspace(), used all over the code.
 */
static __inline int
hammer_checkspace(hammer_mount_t hmp, int slop)
{
	return(_hammer_checkspace(hmp, slop, NULL));
}

#endif

#ifdef _KERNEL
static __inline void
hammer_wait_mem_record(hammer_record_t record)
{
	hammer_wait_mem_record_ident(record, "hmmwai");
}

static __inline void
hammer_lock_ex(struct hammer_lock *lock)
{
	hammer_lock_ex_ident(lock, "hmrlck");
}

/*
 * Indicate that a B-Tree node is being modified.
 */
static __inline void
hammer_modify_node_noundo(hammer_transaction_t trans, hammer_node_t node)
{
	KKASSERT((node->flags & HAMMER_NODE_CRCBAD) == 0);
	hammer_modify_buffer(trans, node->buffer, NULL, 0);
}

static __inline void
hammer_modify_node_all(hammer_transaction_t trans, struct hammer_node *node)
{
	KKASSERT((node->flags & HAMMER_NODE_CRCBAD) == 0);
	hammer_modify_buffer(trans, node->buffer,
			     node->ondisk, sizeof(*node->ondisk));
}

static __inline void
hammer_modify_node(hammer_transaction_t trans, hammer_node_t node,
		   void *base, int len)
{
	hammer_crc_t *crcptr;

	KKASSERT((char *)base >= (char *)node->ondisk &&
		 (char *)base + len <=
		    (char *)node->ondisk + sizeof(*node->ondisk));
	KKASSERT((node->flags & HAMMER_NODE_CRCBAD) == 0);

	if (hammer_btree_full_undo) {
		hammer_modify_node_all(trans, node);
	} else {
		hammer_modify_buffer(trans, node->buffer, base, len);
		crcptr = &node->ondisk->crc;
		hammer_modify_buffer(trans, node->buffer,
				     crcptr, sizeof(hammer_crc_t));
		--node->buffer->io.modify_refs;	/* only want one ref */
	}
}

/*
 * Indicate that the specified modifications have been completed.
 *
 * Do not try to generate the crc here, it's very expensive to do and a
 * sequence of insertions or deletions can result in many calls to this
 * function on the same node.
 */
static __inline void
hammer_modify_node_done(hammer_node_t node)
{
	node->flags |= HAMMER_NODE_CRCGOOD;
	if ((node->flags & HAMMER_NODE_NEEDSCRC) == 0) {
		node->flags |= HAMMER_NODE_NEEDSCRC;
		node->buffer->io.gencrc = 1;
		hammer_ref_node(node);
	}
	hammer_modify_buffer_done(node->buffer);
}

/*
 * Translate a zone address to zone-2 address.
 */
#define hammer_xlate_to_zone2(offset) \
	(((offset) & ~HAMMER_OFF_ZONE_MASK) | HAMMER_ZONE_RAW_BUFFER)

/*
 * Lookup a blockmap offset.
 */
static __inline hammer_off_t
hammer_blockmap_lookup(hammer_mount_t hmp, hammer_off_t zone_offset,
			int *errorp)
{
#if defined INVARIANTS
	int zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
#endif

	/*
	 * We can actually skip blockmap verify by default,
	 * as normal blockmaps are now direct-mapped onto the freemap
	 * and so represent zone-2 addresses.
	 */
	if (hammer_verify_zone == 0) {
		*errorp = 0;
		return hammer_xlate_to_zone2(zone_offset);
	}

	return hammer_blockmap_lookup_verify(hmp, zone_offset, errorp);
}
#endif

#define hammer_modify_volume_field(trans, vol, field)		\
	hammer_modify_volume(trans, vol, &(vol)->ondisk->field,	\
			     sizeof((vol)->ondisk->field))

#define hammer_modify_node_field(trans, node, field)		\
	hammer_modify_node(trans, node, &(node)->ondisk->field,	\
			     sizeof((node)->ondisk->field))

#ifdef _KERNEL
/*
 * The HAMMER_INODE_CAP_DIR_LOCAL_INO capability is set on newly
 * created directories for HAMMER version 2 or greater and causes
 * directory entries to be placed the inode localization zone in
 * the B-Tree instead of the misc zone.
 *
 * This greatly improves localization between directory entries and
 * inodes
 */
static __inline u_int32_t
hammer_dir_localization(hammer_inode_t dip)
{
	if (dip->ino_data.cap_flags & HAMMER_INODE_CAP_DIR_LOCAL_INO)
		return(HAMMER_LOCALIZE_INODE);
	else
		return(HAMMER_LOCALIZE_MISC);
}
#endif
