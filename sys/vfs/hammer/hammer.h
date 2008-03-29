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
 * $DragonFly: src/sys/vfs/hammer/hammer.h,v 1.45 2008/03/29 20:12:54 dillon Exp $
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

/*
 * HAMMER Transaction tracking
 */
struct hammer_transaction {
	struct hammer_mount *hmp;
	hammer_tid_t	tid;
	struct hammer_volume *rootvol;
/*	TAILQ_HEAD(, hammer_io) recycle_list;*/
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
	RB_ENTRY(hammer_inode) rb_node;
	u_int64_t	obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
	hammer_tid_t	last_tid;	/* last modified tid (for fsync) */
	hammer_tid_t	sync_tid;	/* last inode tid synced to disk */
	struct hammer_mount *hmp;
	int		flags;
	int		cursor_ip_refs;	/* sanity */
	struct vnode	*vp;
	struct lockf	advlock;
	struct hammer_lock lock;
	struct hammer_inode_record ino_rec;
	struct hammer_inode_data ino_data;
	struct hammer_rec_rb_tree rec_tree;	/* red-black record tree */
	struct hammer_node	*cache[2];	/* search initiate cache */
};

typedef struct hammer_inode *hammer_inode_t;

#define VTOI(vp)	((struct hammer_inode *)(vp)->v_data)

#define HAMMER_INODE_DDIRTY	0x0001	/* in-memory ino_data is dirty */
#define HAMMER_INODE_RDIRTY	0x0002	/* in-memory ino_rec is dirty */
#define HAMMER_INODE_ITIMES	0x0004	/* in-memory mtime/atime modified */
#define HAMMER_INODE_XDIRTY	0x0008	/* in-memory records present */
#define HAMMER_INODE_ONDISK	0x0010	/* inode is on-disk (else not yet) */
#define HAMMER_INODE_FLUSH	0x0020	/* flush on last ref */
#define HAMMER_INODE_DELETED	0x0080	/* inode ready for deletion */
#define HAMMER_INODE_DELONDISK	0x0100	/* delete synchronized to disk */
#define HAMMER_INODE_RO		0x0200	/* read-only (because of as-of) */
#define HAMMER_INODE_GONE	0x0400	/* delete flushed out */
#define HAMMER_INODE_DONDISK	0x0800	/* data records may be on disk */
#define HAMMER_INODE_BUFS	0x1000	/* dirty high level bps present */
#define HAMMER_INODE_TIDLOCKED	0x2000	/* tid locked until inode synced */

#define HAMMER_INODE_MODMASK	(HAMMER_INODE_DDIRTY|HAMMER_INODE_RDIRTY| \
				 HAMMER_INODE_XDIRTY|HAMMER_INODE_BUFS|	  \
				 HAMMER_INODE_ITIMES|HAMMER_INODE_DELETED)

#define HAMMER_MAX_INODE_CURSORS	4

/*
 * Structure used to represent an unsynchronized record in-memory.  This
 * structure is orgranized in a per-inode RB-tree.  If the inode is not
 * on disk then neither are any records and the in-memory record tree
 * represents the entire contents of the inode.  If the inode is on disk
 * then the on-disk B-Tree is scanned in parallel with the in-memory
 * RB-Tree to synthesize the current state of the file.
 *
 * Only current (delete_tid == 0) unsynchronized records are kept in-memory.
 *
 * blocked is the count of the number of cursors (ip_first/ip_next) blocked
 * on the record waiting for a synchronization to complete.
 */
struct hammer_record {
	RB_ENTRY(hammer_record)		rb_node;
	struct hammer_lock		lock;
	struct hammer_inode		*ip;
	union hammer_record_ondisk	rec;
	union hammer_data_ondisk	*data;
	int				flags;
	int				blocked;
};

typedef struct hammer_record *hammer_record_t;

#define HAMMER_RECF_ALLOCDATA		0x0001
#define HAMMER_RECF_ONRBTREE		0x0002
#define HAMMER_RECF_DELETED		0x0004
#define HAMMER_RECF_INBAND		0x0008
#define HAMMER_RECF_SYNCING		0x0010
#define HAMMER_RECF_WANTED		0x0020

/*
 * In-memory structures representing on-disk structures.
 */
struct hammer_volume;
struct hammer_buffer;
struct hammer_node;
RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_buf_rb_tree, hammer_buffer);
RB_HEAD(hammer_nod_rb_tree, hammer_node);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	      hammer_buf_rb_compare, hammer_off_t);
RB_PROTOTYPE2(hammer_nod_rb_tree, hammer_node, rb_node,
	      hammer_nod_rb_compare, hammer_off_t);

/*
 * IO management - embedded at the head of various in-memory structures
 */
enum hammer_io_type { HAMMER_STRUCTURE_VOLUME,
		      HAMMER_STRUCTURE_BUFFER };

union hammer_io_structure;
struct hammer_io;

struct worklist {
	LIST_ENTRY(worklist) node;
};

/*TAILQ_HEAD(hammer_dep_list, hammer_dep);*/

struct hammer_io {
	struct worklist worklist;
	struct hammer_lock lock;
	enum hammer_io_type type;
	struct buf	*bp;
	int64_t		offset;
	int		loading;	/* loading/unloading interlock */
	u_int		modified : 1;	/* bp's data was modified */
	u_int		released : 1;	/* bp released (w/ B_LOCKED set) */
	u_int		running : 1;	/* bp write IO in progress */
	u_int		waiting : 1;	/* someone is waiting on us */
	u_int		validated : 1;	/* ondisk has been validated */
	u_int		flush : 1;	/* flush on last release */
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
	hammer_off_t maxbuf_off; /* Maximum buffer offset */
	char	*vol_name;
	struct vnode *devvp;
	struct hammer_mount *hmp;
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
 * Internal hammer mount data structure
 */
struct hammer_mount {
	struct mount *mp;
	/*struct vnode *rootvp;*/
	struct hammer_ino_rb_tree rb_inos_root;
	struct hammer_vol_rb_tree rb_vols_root;
	struct hammer_nod_rb_tree rb_nods_root;
	struct hammer_volume *rootvol;
	struct hammer_base_elm root_btree_beg;
	struct hammer_base_elm root_btree_end;
	char	*zbuf;	/* HAMMER_BUFSIZE bytes worth of all-zeros */
	int	hflags;
	int	ronly;
	int	nvolumes;
	int	volume_iterator;
	u_int	check_interrupt;
	uuid_t	fsid;
	udev_t	fsid_udev;
	hammer_tid_t asof;
	u_int32_t namekey_iterator;
	hammer_off_t zone_limits[HAMMER_MAX_ZONES];
	struct netexport export;
	struct lock blockmap_lock;
	struct hammer_holes holes[HAMMER_MAX_ZONES];
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

extern int hammer_debug_general;
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

int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_get_vnode(struct hammer_inode *ip, int lktype,
			struct vnode **vpp);
struct hammer_inode *hammer_get_inode(hammer_transaction_t trans,
			struct hammer_node **cache,
			u_int64_t obj_id, hammer_tid_t asof, int flags,
			int *errorp);
void	hammer_put_inode(struct hammer_inode *ip);
void	hammer_put_inode_ref(struct hammer_inode *ip);

int	hammer_unload_inode(hammer_inode_t ip, void *data);
int	hammer_unload_volume(hammer_volume_t volume, void *data __unused);
int	hammer_unload_buffer(hammer_buffer_t buffer, void *data __unused);
int	hammer_install_volume(hammer_mount_t hmp, const char *volname);

int	hammer_ip_lookup(hammer_cursor_t cursor, hammer_inode_t ip);
int	hammer_ip_first(hammer_cursor_t cursor, hammer_inode_t ip);
int	hammer_ip_next(hammer_cursor_t cursor);
int	hammer_ip_resolve_record_and_data(hammer_cursor_t cursor);
int	hammer_ip_resolve_data(hammer_cursor_t cursor);
int	hammer_ip_delete_record(hammer_cursor_t cursor, hammer_tid_t tid);
int	hammer_delete_at_cursor(hammer_cursor_t cursor, int64_t *stat_bytes);
int	hammer_ip_check_directory_empty(hammer_transaction_t trans,
			hammer_inode_t ip);
int	hammer_sync_hmp(hammer_mount_t hmp, int waitfor);
int	hammer_sync_volume(hammer_volume_t volume, void *data);
int	hammer_sync_buffer(hammer_buffer_t buffer, void *data);

hammer_record_t
	hammer_alloc_mem_record(hammer_inode_t ip);
void	hammer_rel_mem_record(hammer_record_t record);

int	hammer_cursor_up(hammer_cursor_t cursor);
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

u_int32_t hammer_to_unix_xid(uuid_t *uuid);
void hammer_guid_to_uuid(uuid_t *uuid, u_int32_t guid);
void	hammer_to_timespec(hammer_tid_t tid, struct timespec *ts);
hammer_tid_t hammer_timespec_to_transid(struct timespec *ts);
hammer_tid_t hammer_alloc_tid(hammer_transaction_t trans);
hammer_tid_t hammer_now_tid(void);
hammer_tid_t hammer_str_to_tid(const char *str);

enum vtype hammer_get_vnode_type(u_int8_t obj_type);
int hammer_get_dtype(u_int8_t obj_type);
u_int8_t hammer_get_obj_type(enum vtype vtype);
int64_t hammer_directory_namekey(void *name, int len);

int	hammer_init_cursor(hammer_transaction_t trans, hammer_cursor_t cursor,
			   struct hammer_node **cache);

void	hammer_done_cursor(hammer_cursor_t cursor);
void	hammer_mem_done(hammer_cursor_t cursor);

int	hammer_btree_lookup(hammer_cursor_t cursor);
int	hammer_btree_first(hammer_cursor_t cursor);
int	hammer_btree_last(hammer_cursor_t cursor);
int	hammer_btree_extract(hammer_cursor_t cursor, int flags);
int	hammer_btree_iterate(hammer_cursor_t cursor);
int	hammer_btree_iterate_reverse(hammer_cursor_t cursor);
int	hammer_btree_insert(hammer_cursor_t cursor, hammer_btree_elm_t elm);
int	hammer_btree_delete(hammer_cursor_t cursor);
int	hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
int	hammer_btree_chkts(hammer_tid_t ts, hammer_base_elm_t key);
int	hammer_btree_correct_rhb(hammer_cursor_t cursor, hammer_tid_t tid);
int	hammer_btree_correct_lhb(hammer_cursor_t cursor, hammer_tid_t tid);


int	hammer_btree_lock_children(hammer_cursor_t cursor,
                        struct hammer_node_locklist **locklistp);

void	hammer_print_btree_node(hammer_node_ondisk_t ondisk);
void	hammer_print_btree_elm(hammer_btree_elm_t elm, u_int8_t type, int i);

void	*hammer_bread(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);
void	*hammer_bnew(struct hammer_mount *hmp, hammer_off_t off,
			int *errorp, struct hammer_buffer **bufferp);

hammer_volume_t hammer_get_root_volume(hammer_mount_t hmp, int *errorp);

hammer_volume_t	hammer_get_volume(hammer_mount_t hmp,
			int32_t vol_no, int *errorp);
hammer_buffer_t	hammer_get_buffer(hammer_mount_t hmp,
			hammer_off_t buf_offset, int isnew, int *errorp);
void	hammer_uncache_buffer(struct hammer_mount *hmp, hammer_off_t off);

int		hammer_ref_volume(hammer_volume_t volume);
int		hammer_ref_buffer(hammer_buffer_t buffer);
void		hammer_flush_buffer_nodes(hammer_buffer_t buffer);

void		hammer_rel_volume(hammer_volume_t volume, int flush);
void		hammer_rel_buffer(hammer_buffer_t buffer, int flush);

int		hammer_vfs_export(struct mount *mp, int op,
			const struct export_args *export);
hammer_node_t	hammer_get_node(hammer_mount_t hmp,
			hammer_off_t node_offset, int *errorp);
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
			struct hammer_buffer **data_bufferp, int *errorp);
void *hammer_alloc_data(hammer_transaction_t trans, int32_t data_len,
			hammer_off_t *data_offsetp,
			struct hammer_buffer **data_bufferp, int *errorp);

int hammer_generate_undo(hammer_transaction_t trans, hammer_off_t zone1_offset,
			void *base, int len);

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

void hammer_start_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_simple_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
void hammer_start_transaction_tid(struct hammer_transaction *trans,
			          struct hammer_mount *hmp, hammer_tid_t tid);
void hammer_commit_transaction(struct hammer_transaction *trans);
void hammer_abort_transaction(struct hammer_transaction *trans);

void hammer_modify_inode(struct hammer_transaction *trans,
			hammer_inode_t ip, int flags);
int  hammer_create_inode(struct hammer_transaction *trans, struct vattr *vap,
			struct ucred *cred, struct hammer_inode *dip,
			struct hammer_inode **ipp);
void hammer_rel_inode(hammer_inode_t ip, int flush);
int hammer_sync_inode(hammer_inode_t ip, int waitfor, int handle_delete);

int  hammer_ip_add_directory(struct hammer_transaction *trans,
			hammer_inode_t dip, struct namecache *ncp,
			hammer_inode_t nip);
int  hammer_ip_del_directory(struct hammer_transaction *trans,
			hammer_cursor_t cursor, hammer_inode_t dip,
			hammer_inode_t ip);
int  hammer_ip_add_record(struct hammer_transaction *trans,
			hammer_record_t record);
int  hammer_ip_delete_range(struct hammer_transaction *trans,
			hammer_inode_t ip, int64_t ran_beg, int64_t ran_end);
int  hammer_ip_delete_range_all(struct hammer_transaction *trans,
			hammer_inode_t ip);
int  hammer_ip_sync_data(struct hammer_transaction *trans,
			hammer_inode_t ip, int64_t offset,
			void *data, int bytes);
int  hammer_ip_sync_record(hammer_transaction_t trans, hammer_record_t rec);

int hammer_ioctl(hammer_inode_t ip, u_long com, caddr_t data, int fflag,
			struct ucred *cred);

void hammer_io_init(hammer_io_t io, enum hammer_io_type type);
int hammer_io_read(struct vnode *devvp, struct hammer_io *io);
int hammer_io_new(struct vnode *devvp, struct hammer_io *io);
void hammer_io_release(struct hammer_io *io);
void hammer_io_flush(struct hammer_io *io);
int hammer_io_checkflush(hammer_io_t io);
void hammer_io_clear_modify(struct hammer_io *io);
void hammer_io_waitdep(struct hammer_io *io);

void hammer_modify_volume(hammer_transaction_t trans, hammer_volume_t volume,
			void *base, int len);
void hammer_modify_buffer(hammer_transaction_t trans, hammer_buffer_t buffer,
			void *base, int len);

int hammer_ioc_reblock(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_reblock *reblock);

void hammer_init_holes(hammer_mount_t hmp, hammer_holes_t holes);
void hammer_free_holes(hammer_mount_t hmp, hammer_holes_t holes);
int hammer_signal_check(hammer_mount_t hmp);

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
	KKASSERT((char *)base >= (char *)node->ondisk &&
		 (char *)base + len <=
		    (char *)node->ondisk + sizeof(*node->ondisk));
	hammer_modify_buffer(trans, node->buffer, base, len);
}

static __inline void
hammer_modify_record(hammer_transaction_t trans, hammer_buffer_t buffer,
		     void *base, int len)
{
	KKASSERT((char *)base >= (char *)buffer->ondisk &&
		 (char *)base + len <= (char *)buffer->ondisk + HAMMER_BUFSIZE);
	hammer_modify_buffer(trans, buffer, base, len);
}

