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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer.h,v 1.69 2008/05/18 01:48:50 dillon Exp $
 */
/*
 * This header file contains structures used internally by the HAMMERFS
 * implementation.  See hammer_disk.h for on-disk structures.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mountctl.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/globaldata.h>

#include <sys/buf2.h>
#include <sys/signal2.h>
#include "hammer_disk.h"
#include "hammer_mount.h"
#include "hammer_ioctl.h"

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

MALLOC_DECLARE(M_HAMMER);

struct hammer_mount;

/*
 * Key structure used for custom RB tree inode lookups.  This prototypes
 * the function hammer_ino_rb_tree_RB_LOOKUP_INFO(root, info).
 */
typedef struct hammer_inode_info {
	int64_t		obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
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
	hammer_tid_t	time;
	int		sync_lock_refs;
	struct hammer_volume *rootvol;
};

typedef struct hammer_transaction *hammer_transaction_t;

/*
 * HAMMER locks
 */
struct hammer_lock {
	int	refs;		/* active references delay writes */
	int	lockcount;	/* lock count for exclusive/shared access */
	int	wanted;
	struct thread *locktd;
};

static __inline int
hammer_islocked(struct hammer_lock *lock)
{
	return(lock->lockcount != 0);
}

static __inline int
hammer_isactive(struct hammer_lock *lock)
{
	return(lock->refs != 0);
}

static __inline int
hammer_islastref(struct hammer_lock *lock)
{
	return(lock->refs == 1);
}

/*
 * Return if we specifically own the lock exclusively.
 */
static __inline int
hammer_lock_excl_owned(struct hammer_lock *lock, thread_t td)
{
	if (lock->lockcount > 0 && lock->locktd == td)
		return(1);
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
 * Cache object ids.  A fixed number of objid cache structures are
 * created to reserve object id's for newly created files in multiples
 * of 100,000, localized to a particular directory, and recycled as
 * needed.  This allows parallel create operations in different
 * directories to retain fairly localized object ids which in turn
 * improves reblocking performance and layout.
 */
#define OBJID_CACHE_SIZE	128
#define OBJID_CACHE_BULK	100000

typedef struct hammer_objid_cache {
	TAILQ_ENTRY(hammer_objid_cache) entry;
	struct hammer_inode		*dip;
	hammer_tid_t			next_tid;
	int				count;
} *hammer_objid_cache_t;

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

struct hammer_rec_rb_tree;
struct hammer_record;
RB_HEAD(hammer_rec_rb_tree, hammer_record);
RB_PROTOTYPEX(hammer_rec_rb_tree, INFO, hammer_record, rb_node,
	      hammer_rec_rb_compare, hammer_base_elm_t);

TAILQ_HEAD(hammer_node_list, hammer_node);

struct hammer_inode {
	RB_ENTRY(hammer_inode)	rb_node;
	hammer_inode_state_t	flush_state;
	int			flush_group;
	TAILQ_ENTRY(hammer_inode) flush_entry;
	struct hammer_record_list target_list;	/* target of dependant recs */
	u_int64_t		obj_id;		/* (key) object identifier */
	hammer_tid_t		obj_asof;	/* (key) snapshot or 0 */
	struct hammer_mount 	*hmp;
	hammer_objid_cache_t 	objid_cache;
	int			flags;
	int			error;		/* flush error */
	int			cursor_ip_refs;	/* sanity */
	struct vnode		*vp;
	struct lockf		advlock;
	struct hammer_lock	lock;		/* sync copy interlock */
	TAILQ_HEAD(, bio)	bio_list;	/* BIOs to flush out */
	TAILQ_HEAD(, bio)	bio_alt_list;	/* BIOs to flush out */
	off_t			trunc_off;
	struct hammer_btree_leaf_elm ino_leaf;  /* in-memory cache */
	struct hammer_inode_data ino_data;	/* in-memory cache */
	struct hammer_rec_rb_tree rec_tree;	/* in-memory cache */
	struct hammer_node	*cache[2];	/* search initiate cache */

	/*
	 * When a demark is created to synchronize an inode to
	 * disk, certain fields are copied so the front-end VOPs
	 * can continue to run in parallel with the synchronization
	 * occuring in the background.
	 */
	int		sync_flags;		/* to-sync flags cache */
	off_t		sync_trunc_off;		/* to-sync truncation */
	struct hammer_btree_leaf_elm sync_ino_leaf; /* to-sync cache */
	struct hammer_inode_data sync_ino_data; /* to-sync cache */
};

typedef struct hammer_inode *hammer_inode_t;

#define VTOI(vp)	((struct hammer_inode *)(vp)->v_data)

#define HAMMER_INODE_DDIRTY	0x0001	/* in-memory ino_data is dirty */
#define HAMMER_INODE_UNUSED0002	0x0002
#define HAMMER_INODE_ITIMES	0x0004	/* in-memory mtime/atime modified */
#define HAMMER_INODE_XDIRTY	0x0008	/* in-memory records */
#define HAMMER_INODE_ONDISK	0x0010	/* inode is on-disk (else not yet) */
#define HAMMER_INODE_FLUSH	0x0020	/* flush on last ref */
#define HAMMER_INODE_DELETED	0x0080	/* inode delete (backend) */
#define HAMMER_INODE_DELONDISK	0x0100	/* delete synchronized to disk */
#define HAMMER_INODE_RO		0x0200	/* read-only (because of as-of) */
#define HAMMER_INODE_VHELD	0x0400	/* vnode held on sync */
#define HAMMER_INODE_DONDISK	0x0800	/* data records may be on disk */
#define HAMMER_INODE_BUFS	0x1000	/* dirty high level bps present */
#define HAMMER_INODE_REFLUSH	0x2000	/* pipelined flush during flush */
#define HAMMER_INODE_WRITE_ALT	0x4000	/* strategy writes to alt bioq */
#define HAMMER_INODE_FLUSHW	0x8000	/* Someone waiting for flush */

#define HAMMER_INODE_TRUNCATED	0x00010000
#define HAMMER_INODE_DELETING	0x00020000 /* inode delete request (frontend)*/
#define HAMMER_INODE_RESIGNAL	0x00040000 /* re-signal on re-flush */
#define HAMMER_INODE_RESIGNAL	0x00040000 /* re-signal on re-flush */

#define HAMMER_INODE_MODMASK	(HAMMER_INODE_DDIRTY|			    \
				 HAMMER_INODE_XDIRTY|HAMMER_INODE_BUFS|	    \
				 HAMMER_INODE_ITIMES|HAMMER_INODE_TRUNCATED|\
				 HAMMER_INODE_DELETING)

#define HAMMER_INODE_MODMASK_NOXDIRTY \
				(HAMMER_INODE_MODMASK & ~HAMMER_INODE_XDIRTY)

#define HAMMER_MAX_INODE_CURSORS	4

#define HAMMER_FLUSH_SIGNAL	0x0001
#define HAMMER_FLUSH_RECURSION	0x0002

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
	HAMMER_MEM_RECORD_DEL		/* negative delete-on-disk record */
} hammer_record_type_t;

struct hammer_record {
	RB_ENTRY(hammer_record)		rb_node;
	TAILQ_ENTRY(hammer_record)	target_entry;
	hammer_inode_state_t		flush_state;
	int				flush_group;
	hammer_record_type_t		type;
	struct hammer_lock		lock;
	struct hammer_inode		*ip;
	struct hammer_inode		*target_ip;
	struct hammer_btree_leaf_elm	leaf;
	union hammer_data_ondisk	*data;
	int				flags;
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
#define HAMMER_RECF_UNUSED0010		0x0010
#define HAMMER_RECF_INTERLOCK_BE	0x0020	/* backend interlock */
#define HAMMER_RECF_WANTED		0x0040
#define HAMMER_RECF_CONVERT_DELETE 	0x0100 /* special case */

/*
 * In-memory structures representing on-disk structures.
 */
struct hammer_volume;
struct hammer_buffer;
struct hammer_node;
struct hammer_undo;
RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_buf_rb_tree, hammer_buffer);
RB_HEAD(hammer_nod_rb_tree, hammer_node);
RB_HEAD(hammer_und_rb_tree, hammer_undo);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	      hammer_buf_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_nod_rb_tree, hammer_node, rb_node,
	      hammer_nod_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_und_rb_tree, hammer_undo, rb_node,
	      hammer_und_rb_compare, hammer_off_t);

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
	HAMMER_STRUCTURE_DATA_BUFFER
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
	TAILQ_ENTRY(hammer_io)	mod_entry; /* list entry if modified */
	hammer_io_list_t	mod_list;
	struct buf		*bp;
	int64_t			offset;
	int			loading;   /* loading/unloading interlock */
	int			modify_refs;

	u_int		modified : 1;	/* bp's data was modified */
	u_int		released : 1;	/* bp released (w/ B_LOCKED set) */
	u_int		running : 1;	/* bp write IO in progress */
	u_int		waiting : 1;	/* someone is waiting on us */
	u_int		validated : 1;	/* ondisk has been validated */
	u_int		waitdep : 1;	/* flush waits for dependancies */
};

typedef struct hammer_io *hammer_io_t;

/*
 * In-memory volume representing on-disk buffer
 */
struct hammer_volume {
	struct hammer_io io;
	RB_ENTRY(hammer_volume) rb_node;
	struct hammer_buf_rb_tree rb_bufs_root;
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
	struct hammer_volume *volume;
	hammer_off_t zone2_offset;
	hammer_off_t zoneX_offset;
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
	struct hammer_node	**cache1;	/* passive cache(s) */
	struct hammer_node	**cache2;
	int			flags;
	int			loading;	/* load interlock */
};

#define HAMMER_NODE_DELETED	0x0001
#define HAMMER_NODE_FLUSH	0x0002

typedef struct hammer_node	*hammer_node_t;

/*
 * List of locked nodes.
 */
struct hammer_node_locklist {
	struct hammer_node_locklist *next;
	hammer_node_t	node;
};

typedef struct hammer_node_locklist *hammer_node_locklist_t;


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
 * Allocation holes are recorded for a short period of time in an attempt
 * to use up the space.
 */

#define HAMMER_MAX_HOLES	8

struct hammer_hole;

struct hammer_holes {
	TAILQ_HEAD(, hammer_hole) list;
	int	count;
};

typedef struct hammer_holes *hammer_holes_t;

struct hammer_hole {
	TAILQ_ENTRY(hammer_hole) entry;
	hammer_off_t	offset;
	int		bytes;
};

typedef struct hammer_hole *hammer_hole_t;

#include "hammer_cursor.h"

/*
 * Undo history tracking
 */
#define HAMMER_MAX_UNDOS	256

struct hammer_undo {
	RB_ENTRY(hammer_undo)	rb_node;
	TAILQ_ENTRY(hammer_undo) lru_entry;
	hammer_off_t		offset;
	int			bytes;
};

typedef struct hammer_undo *hammer_undo_t;

/*
 * Internal hammer mount data structure
 */
struct hammer_mount {
	struct mount *mp;
	/*struct vnode *rootvp;*/
	struct hammer_ino_rb_tree rb_inos_root;
	struct hammer_vol_rb_tree rb_vols_root;
	struct hammer_nod_rb_tree rb_nods_root;
	struct hammer_und_rb_tree rb_undo_root;
	struct hammer_volume *rootvol;
	struct hammer_base_elm root_btree_beg;
	struct hammer_base_elm root_btree_end;
	char	*zbuf;	/* HAMMER_BUFSIZE bytes worth of all-zeros */
	int	hflags;
	int	ronly;
	int	nvolumes;
	int	volume_iterator;
	int	flusher_signal;	/* flusher thread sequencer */
	int	flusher_act;	/* currently active flush group */
	int	flusher_done;	/* set to act when complete */
	int	flusher_next;	/* next flush group */
	int	flusher_lock;	/* lock sequencing of the next flush */
	int	flusher_exiting;
	hammer_tid_t flusher_tid; /* last flushed transaction id */
	hammer_off_t flusher_undo_start; /* UNDO window for flushes */
	int	reclaim_count;
	thread_t flusher_td;
	u_int	check_interrupt;
	uuid_t	fsid;
	udev_t	fsid_udev;
	struct hammer_io_list volu_list;	/* dirty undo buffers */
	struct hammer_io_list undo_list;	/* dirty undo buffers */
	struct hammer_io_list data_list;	/* dirty data buffers */
	struct hammer_io_list meta_list;	/* dirty meta bufs    */
	struct hammer_io_list lose_list;	/* loose buffers      */
	int	locked_dirty_count;		/* meta/volu count    */
	int	io_running_count;
	int	objid_cache_count;
	hammer_tid_t asof;
	hammer_off_t next_tid;
	u_int32_t namekey_iterator;
	hammer_off_t zone_limits[HAMMER_MAX_ZONES];
	struct netexport export;
	struct hammer_lock sync_lock;
	struct hammer_lock free_lock;
	struct lock blockmap_lock;
	struct hammer_blockmap  blockmap[HAMMER_MAX_ZONES];
	struct hammer_holes	holes[HAMMER_MAX_ZONES];
	struct hammer_undo	undos[HAMMER_MAX_UNDOS];
	int			undo_alloc;
	TAILQ_HEAD(, hammer_undo)  undo_lru_list;
	TAILQ_HEAD(, hammer_inode) flush_list;
	TAILQ_HEAD(, hammer_objid_cache) objid_cache_list;
};

typedef struct hammer_mount	*hammer_mount_t;

struct hammer_sync_info {
	int error;
	int waitfor;
};

#endif

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
extern int hammer_count_inodes;
extern int hammer_count_records;
extern int hammer_count_record_datas;
extern int hammer_count_volumes;
extern int hammer_count_buffers;
extern int hammer_count_nodes;
extern int hammer_count_dirtybufs;
extern int hammer_limit_dirtybufs;
extern int hammer_bio_count;
extern int64_t hammer_contention_count;

int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_get_vnode(struct hammer_inode *ip, struct vnode **vpp);
struct hammer_inode *hammer_get_inode(hammer_transaction_t trans,
			struct hammer_node **cache,
			u_int64_t obj_id, hammer_tid_t asof, int flags,
			int *errorp);
void	hammer_put_inode(struct hammer_inode *ip);
void	hammer_put_inode_ref(struct hammer_inode *ip);

int	hammer_unload_volume(hammer_volume_t volume, void *data __unused);
int	hammer_unload_buffer(hammer_buffer_t buffer, void *data __unused);
int	hammer_install_volume(hammer_mount_t hmp, const char *volname);

int	hammer_ip_lookup(hammer_cursor_t cursor);
int	hammer_ip_first(hammer_cursor_t cursor);
int	hammer_ip_next(hammer_cursor_t cursor);
int	hammer_ip_resolve_data(hammer_cursor_t cursor);
int	hammer_ip_delete_record(hammer_cursor_t cursor, hammer_tid_t tid);
int	hammer_delete_at_cursor(hammer_cursor_t cursor, int64_t *stat_bytes);
int	hammer_ip_check_directory_empty(hammer_transaction_t trans,
			hammer_inode_t ip);
int	hammer_sync_hmp(hammer_mount_t hmp, int waitfor);
int	hammer_queue_inodes_flusher(hammer_mount_t hmp, int waitfor);


hammer_record_t
	hammer_alloc_mem_record(hammer_inode_t ip, int data_len);
void	hammer_flush_record_done(hammer_record_t record, int error);
void	hammer_wait_mem_record(hammer_record_t record);
void	hammer_rel_mem_record(hammer_record_t record);

int	hammer_cursor_up(hammer_cursor_t cursor);
int	hammer_cursor_up_locked(hammer_cursor_t cursor);
int	hammer_cursor_down(hammer_cursor_t cursor);
int	hammer_cursor_upgrade(hammer_cursor_t cursor);
void	hammer_cursor_downgrade(hammer_cursor_t cursor);
int	hammer_cursor_seek(hammer_cursor_t cursor, hammer_node_t node,
			int index);
void	hammer_lock_ex(struct hammer_lock *lock);
int	hammer_lock_ex_try(struct hammer_lock *lock);
void	hammer_lock_sh(struct hammer_lock *lock);
int	hammer_lock_upgrade(struct hammer_lock *lock);
void	hammer_lock_downgrade(struct hammer_lock *lock);
void	hammer_unlock(struct hammer_lock *lock);
void	hammer_ref(struct hammer_lock *lock);
void	hammer_unref(struct hammer_lock *lock);

void	hammer_sync_lock_ex(hammer_transaction_t trans);
void	hammer_sync_lock_sh(hammer_transaction_t trans);
void	hammer_sync_unlock(hammer_transaction_t trans);

u_int32_t hammer_to_unix_xid(uuid_t *uuid);
void hammer_guid_to_uuid(uuid_t *uuid, u_int32_t guid);
void	hammer_to_timespec(hammer_tid_t tid, struct timespec *ts);
hammer_tid_t hammer_timespec_to_transid(struct timespec *ts);
hammer_tid_t hammer_now_tid(void);
hammer_tid_t hammer_str_to_tid(const char *str);
hammer_tid_t hammer_alloc_objid(hammer_transaction_t trans, hammer_inode_t dip);
void hammer_clear_objid(hammer_inode_t dip);
void hammer_destroy_objid_cache(hammer_mount_t hmp);

int hammer_enter_undo_history(hammer_mount_t hmp, hammer_off_t offset,
			      int bytes);
void hammer_clear_undo_history(hammer_mount_t hmp);
enum vtype hammer_get_vnode_type(u_int8_t obj_type);
int hammer_get_dtype(u_int8_t obj_type);
u_int8_t hammer_get_obj_type(enum vtype vtype);
int64_t hammer_directory_namekey(void *name, int len);

int	hammer_init_cursor(hammer_transaction_t trans, hammer_cursor_t cursor,
			   struct hammer_node **cache, hammer_inode_t ip);
int	hammer_reinit_cursor(hammer_cursor_t cursor);
void	hammer_normalize_cursor(hammer_cursor_t cursor);
void	hammer_done_cursor(hammer_cursor_t cursor);
void	hammer_mem_done(hammer_cursor_t cursor);

int	hammer_btree_lookup(hammer_cursor_t cursor);
int	hammer_btree_first(hammer_cursor_t cursor);
int	hammer_btree_last(hammer_cursor_t cursor);
int	hammer_btree_extract(hammer_cursor_t cursor, int flags);
int	hammer_btree_iterate(hammer_cursor_t cursor);
int	hammer_btree_iterate_reverse(hammer_cursor_t cursor);
int	hammer_btree_insert(hammer_cursor_t cursor,
			    hammer_btree_leaf_elm_t elm);
int	hammer_btree_delete(hammer_cursor_t cursor);
int	hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
int	hammer_btree_chkts(hammer_tid_t ts, hammer_base_elm_t key);
int	hammer_btree_correct_rhb(hammer_cursor_t cursor, hammer_tid_t tid);
int	hammer_btree_correct_lhb(hammer_cursor_t cursor, hammer_tid_t tid);

int	btree_set_parent(hammer_transaction_t trans, hammer_node_t node,
                        hammer_btree_elm_t elm);
int	hammer_btree_lock_children(hammer_cursor_t cursor,
                        struct hammer_node_locklist **locklistp);
void	hammer_btree_unlock_children(struct hammer_node_locklist **locklistp);


void	hammer_print_btree_node(hammer_node_ondisk_t ondisk);
void	hammer_print_btree_elm(hammer_btree_elm_t elm, u_int8_t type, int i);

void	*hammer_bread(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);
void	*hammer_bnew(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);

hammer_volume_t hammer_get_root_volume(hammer_mount_t hmp, int *errorp);
int	hammer_dowrite(hammer_cursor_t cursor, hammer_inode_t ip,
			struct bio *bio);

hammer_volume_t	hammer_get_volume(hammer_mount_t hmp,
			int32_t vol_no, int *errorp);
hammer_buffer_t	hammer_get_buffer(hammer_mount_t hmp,
			hammer_off_t buf_offset, int isnew, int *errorp);
void		hammer_clrxlate_buffer(hammer_mount_t hmp,
			hammer_off_t buf_offset);
void	hammer_uncache_buffer(struct hammer_mount *hmp, hammer_off_t off);

int		hammer_ref_volume(hammer_volume_t volume);
int		hammer_ref_buffer(hammer_buffer_t buffer);
void		hammer_flush_buffer_nodes(hammer_buffer_t buffer);

void		hammer_rel_volume(hammer_volume_t volume, int flush);
void		hammer_rel_buffer(hammer_buffer_t buffer, int flush);

int		hammer_vfs_export(struct mount *mp, int op,
			const struct export_args *export);
hammer_node_t	hammer_get_node(hammer_mount_t hmp, hammer_off_t node_offset,
			int isnew, int *errorp);
void		hammer_ref_node(hammer_node_t node);
hammer_node_t	hammer_ref_node_safe(struct hammer_mount *hmp,
			struct hammer_node **cache, int *errorp);
void		hammer_rel_node(hammer_node_t node);
void		hammer_delete_node(hammer_transaction_t trans,
			hammer_node_t node);
void		hammer_cache_node(hammer_node_t node,
			struct hammer_node **cache);
void		hammer_uncache_node(struct hammer_node **cache);
void		hammer_flush_node(hammer_node_t node);

void hammer_dup_buffer(struct hammer_buffer **bufferp,
			struct hammer_buffer *buffer);
hammer_node_t hammer_alloc_btree(hammer_transaction_t trans, int *errorp);
void *hammer_alloc_record(hammer_transaction_t trans,
			hammer_off_t *rec_offp, u_int16_t rec_type,
			struct hammer_buffer **rec_bufferp,
			int32_t data_len, void **datap,
			hammer_off_t *data_offp,
			struct hammer_buffer **data_bufferp, int *errorp);
void *hammer_alloc_data(hammer_transaction_t trans, int32_t data_len,
			hammer_off_t *data_offsetp,
			struct hammer_buffer **data_bufferp, int *errorp);

int hammer_generate_undo(hammer_transaction_t trans, hammer_io_t io,
			hammer_off_t zone1_offset, void *base, int len);

void hammer_put_volume(struct hammer_volume *volume, int flush);
void hammer_put_buffer(struct hammer_buffer *buffer, int flush);

hammer_off_t hammer_freemap_alloc(hammer_transaction_t trans,
			hammer_off_t owner, int *errorp);
void hammer_freemap_free(hammer_transaction_t trans, hammer_off_t phys_offset,
			hammer_off_t owner, int *errorp);
hammer_off_t hammer_blockmap_alloc(hammer_transaction_t trans, int zone,
			int bytes, int *errorp);
void hammer_blockmap_free(hammer_transaction_t trans,
			hammer_off_t bmap_off, int bytes);
int hammer_blockmap_getfree(hammer_mount_t hmp, hammer_off_t bmap_off,
			int *curp, int *errorp);
hammer_off_t hammer_blockmap_lookup(hammer_mount_t hmp, hammer_off_t bmap_off,
			int *errorp);
hammer_off_t hammer_undo_lookup(hammer_mount_t hmp, hammer_off_t bmap_off,
			int *errorp);
int64_t hammer_undo_used(hammer_mount_t hmp);
int64_t hammer_undo_space(hammer_mount_t hmp);
int64_t hammer_undo_max(hammer_mount_t hmp);


void hammer_start_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_simple_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_start_transaction_fls(struct hammer_transaction *trans,
			          struct hammer_mount *hmp);
void hammer_done_transaction(struct hammer_transaction *trans);

void hammer_modify_inode(struct hammer_transaction *trans,
			hammer_inode_t ip, int flags);
void hammer_flush_inode(hammer_inode_t ip, int flags);
void hammer_flush_inode_done(hammer_inode_t ip);
void hammer_wait_inode(hammer_inode_t ip);

int  hammer_create_inode(struct hammer_transaction *trans, struct vattr *vap,
			struct ucred *cred, struct hammer_inode *dip,
			struct hammer_inode **ipp);
void hammer_rel_inode(hammer_inode_t ip, int flush);
int hammer_sync_inode(hammer_inode_t ip);
void hammer_test_inode(hammer_inode_t ip);
void hammer_inode_unloadable_check(hammer_inode_t ip, int getvp);

int  hammer_ip_add_directory(struct hammer_transaction *trans,
			hammer_inode_t dip, struct namecache *ncp,
			hammer_inode_t nip);
int  hammer_ip_del_directory(struct hammer_transaction *trans,
			hammer_cursor_t cursor, hammer_inode_t dip,
			hammer_inode_t ip);
int  hammer_ip_add_record(struct hammer_transaction *trans,
			hammer_record_t record);
int  hammer_ip_delete_range(hammer_cursor_t cursor, hammer_inode_t ip,
			int64_t ran_beg, int64_t ran_end);
int  hammer_ip_delete_range_all(hammer_cursor_t cursor, hammer_inode_t ip,
			int *countp);
int  hammer_ip_sync_data(hammer_cursor_t cursor, hammer_inode_t ip,
			int64_t offset, void *data, int bytes);
int  hammer_ip_sync_record(hammer_transaction_t trans, hammer_record_t rec);
int  hammer_ip_sync_record_cursor(hammer_cursor_t cursor, hammer_record_t rec);

int hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
			struct ucred *cred);

void hammer_io_init(hammer_io_t io, hammer_mount_t hmp,
			enum hammer_io_type type);
void hammer_io_reinit(hammer_io_t io, enum hammer_io_type type);
int hammer_io_read(struct vnode *devvp, struct hammer_io *io,
			hammer_off_t limit);
int hammer_io_new(struct vnode *devvp, struct hammer_io *io);
void hammer_io_release(struct hammer_io *io, int flush);
void hammer_io_flush(struct hammer_io *io);
int hammer_io_checkflush(hammer_io_t io);
void hammer_io_clear_modify(struct hammer_io *io);
void hammer_io_waitdep(struct hammer_io *io);

void hammer_modify_volume(hammer_transaction_t trans, hammer_volume_t volume,
			void *base, int len);
void hammer_modify_buffer(hammer_transaction_t trans, hammer_buffer_t buffer,
			void *base, int len);
void hammer_modify_volume_done(hammer_volume_t volume);
void hammer_modify_buffer_done(hammer_buffer_t buffer);

int hammer_ioc_reblock(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_reblock *reblock);
int hammer_ioc_prune(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_prune *prune);

void hammer_init_holes(hammer_mount_t hmp, hammer_holes_t holes);
void hammer_free_holes(hammer_mount_t hmp, hammer_holes_t holes);
int hammer_signal_check(hammer_mount_t hmp);

void hammer_flusher_create(hammer_mount_t hmp);
void hammer_flusher_destroy(hammer_mount_t hmp);
void hammer_flusher_sync(hammer_mount_t hmp);
void hammer_flusher_async(hammer_mount_t hmp);

int hammer_recover(hammer_mount_t hmp, hammer_volume_t rootvol);

void hammer_crc_set_blockmap(hammer_blockmap_t blockmap);
void hammer_crc_set_volume(hammer_volume_ondisk_t ondisk);

int hammer_crc_test_blockmap(hammer_blockmap_t blockmap);
int hammer_crc_test_volume(hammer_volume_ondisk_t ondisk);
int hammer_crc_test_btree(hammer_node_ondisk_t ondisk);
void hkprintf(const char *ctl, ...);

#endif

static __inline void
hammer_modify_node_noundo(hammer_transaction_t trans, hammer_node_t node)
{
	hammer_modify_buffer(trans, node->buffer, NULL, 0);
}

static __inline void
hammer_modify_node_all(hammer_transaction_t trans, struct hammer_node *node)
{
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
	hammer_modify_buffer(trans, node->buffer, base, len);
	crcptr = &node->ondisk->crc;
	hammer_modify_buffer(trans, node->buffer, crcptr, sizeof(hammer_crc_t));
	--node->buffer->io.modify_refs;	/* only want one ref */
}

static __inline void
hammer_modify_node_done(hammer_node_t node)
{
	node->ondisk->crc = crc32(&node->ondisk->crc + 1, HAMMER_BTREE_CRCSIZE);
	hammer_modify_buffer_done(node->buffer);
}

#define hammer_modify_volume_field(trans, vol, field)		\
	hammer_modify_volume(trans, vol, &(vol)->ondisk->field,	\
			     sizeof((vol)->ondisk->field))

#define hammer_modify_node_field(trans, node, field)		\
	hammer_modify_node(trans, node, &(node)->ondisk->field,	\
			     sizeof((node)->ondisk->field))

