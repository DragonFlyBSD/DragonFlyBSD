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
 * $DragonFly: src/sys/vfs/hammer/hammer.h,v 1.14 2007/12/29 09:01:27 dillon Exp $
 */
/*
 * This header file contains structures used internally by the HAMMERFS
 * implementation.  See hammer_disk.h for on-disk structures.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/globaldata.h>

#include <sys/buf2.h>
#include "hammer_alist.h"
#include "hammer_disk.h"
#include "hammer_mount.h"

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

MALLOC_DECLARE(M_HAMMER);

struct hammer_mount;

/*
 * Key structure used for custom RB tree inode lookups.  This prototypes
 * the function hammer_ino_rb_tree_RB_LOOKUP_INFO(root, info).
 */
typedef struct hammer_inode_info {
	u_int64_t	obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
} *hammer_inode_info_t;

/*
 * HAMMER Transaction tracking
 */
struct hammer_transaction {
	struct hammer_mount *hmp;
	hammer_tid_t	tid;
	struct hammer_volume *rootvol;
};

typedef struct hammer_transaction *hammer_transaction_t;

/*
 * HAMMER locks
 */
struct hammer_lock {
	int	refs;
	int	lockcount;
	int	wanted;
	struct thread *locktd;
};

static __inline int
hammer_islocked(struct hammer_lock *lock)
{
	return(lock->lockcount != 0);
}

static __inline int
hammer_islastref(struct hammer_lock *lock)
{
	return(lock->refs == 1);
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
	struct hammer_mount *hmp;
	int		flags;
	struct vnode	*vp;
	struct lockf	advlock;
	struct hammer_lock lock;
	struct hammer_inode_record ino_rec;
	struct hammer_inode_data ino_data;
	struct hammer_rec_rb_tree rec_tree;	/* red-black record tree */
	struct hammer_node	*cache;	/* cached B-Tree node shortcut */
};

typedef struct hammer_inode *hammer_inode_t;

#define VTOI(vp)	((struct hammer_inode *)(vp)->v_data)

#define HAMMER_INODE_DDIRTY	0x0001	/* in-memory ino_data is dirty */
#define HAMMER_INODE_RDIRTY	0x0002	/* in-memory ino_rec is dirty */
#define HAMMER_INODE_ITIMES	0x0004	/* in-memory mtime/atime modified */
#define HAMMER_INODE_ONDISK	0x0010	/* inode is on-disk (else not yet) */
#define HAMMER_INODE_FLUSH	0x0020	/* flush on last ref */
#define HAMMER_INODE_TID	0x0040	/* update in-memory last_tid */
#define HAMMER_INODE_DELETED	0x0080	/* inode ready for deletion */
#define HAMMER_INODE_DELONDISK	0x0100	/* delete synchronized to disk */

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
 */
struct hammer_record {
	RB_ENTRY(hammer_record)		rb_node;
	struct hammer_lock		lock;
	struct hammer_inode		*ip;
	union hammer_record_ondisk	rec;
	union hammer_data_ondisk	*data;
	int				flags;
};

typedef struct hammer_record *hammer_record_t;

#define HAMMER_RECF_ALLOCDATA		0x0001
#define HAMMER_RECF_ONRBTREE		0x0002
#define HAMMER_RECF_DELETED		0x0004
#define HAMMER_RECF_EMBEDDED_DATA	0x0008

/*
 * Structures used to internally represent a volume and a cluster
 */
struct hammer_volume;
struct hammer_cluster;
struct hammer_supercl;
struct hammer_buffer;
struct hammer_node;
RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_clu_rb_tree, hammer_cluster);
RB_HEAD(hammer_scl_rb_tree, hammer_supercl);
RB_HEAD(hammer_buf_rb_tree, hammer_buffer);
RB_HEAD(hammer_nod_rb_tree, hammer_node);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_clu_rb_tree, hammer_cluster, rb_node,
	      hammer_clu_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_scl_rb_tree, hammer_supercl, rb_node,
	      hammer_scl_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	      hammer_buf_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_nod_rb_tree, hammer_node, rb_node,
	      hammer_nod_rb_compare, int32_t);

/*
 * IO management - embedded at the head of various in-memory structures
 */
enum hammer_io_type { HAMMER_STRUCTURE_VOLUME,
		      HAMMER_STRUCTURE_SUPERCL,
		      HAMMER_STRUCTURE_CLUSTER,
		      HAMMER_STRUCTURE_BUFFER };

union hammer_io_structure;

struct worklist {
	LIST_ENTRY(worklist) node;
};

struct hammer_io {
	struct worklist worklist;
	struct hammer_lock lock;
	enum hammer_io_type type;
	struct buf	*bp;
	int64_t		offset;
	u_int		modified : 1;	/* bp's data was modified */
	u_int		released : 1;	/* bp released (w/ B_LOCKED set) */
};

typedef struct hammer_io *hammer_io_t;

/*
 * In-memory volume representing on-disk buffer
 */
struct hammer_volume {
	struct hammer_io io;
	RB_ENTRY(hammer_volume) rb_node;
	struct hammer_clu_rb_tree rb_clus_root;
	struct hammer_scl_rb_tree rb_scls_root;
	struct hammer_volume_ondisk *ondisk;
	struct hammer_alist_live alist;
	int32_t	vol_no;
	int32_t	vol_clsize;
	int32_t clu_iterator;	/* cluster allocation iterator */
	int64_t nblocks;	/* note: special calculation for statfs */
	int64_t cluster_base;	/* base offset of cluster 0 */
	char	*vol_name;
	struct vnode *devvp;
	struct hammer_mount *hmp;
	int	vol_flags;
};

typedef struct hammer_volume *hammer_volume_t;

/*
 * In-memory super-cluster representing on-disk buffer
 */
struct hammer_supercl {
	struct hammer_io io;
	RB_ENTRY(hammer_supercl) rb_node;
	struct hammer_supercl_ondisk *ondisk;
	struct hammer_volume *volume;
	struct hammer_alist_live alist;
	int32_t	scl_no;
};

typedef struct hammer_supercl *hammer_supercl_t;

enum hammer_cluster_state {
	HAMMER_CLUSTER_IDLE,
	HAMMER_CLUSTER_ASYNC,
	HAMMER_CLUSTER_OPEN
};

/*
 * In-memory cluster representing on-disk buffer
 *
 * The cluster's indexing range is cached in hammer_cluster, separate
 * from the ondisk info in order to allow cursors to point to it.
 */
struct hammer_cluster {
	struct hammer_io io;
	RB_ENTRY(hammer_cluster) rb_node;
	struct hammer_buf_rb_tree rb_bufs_root;
	struct hammer_cluster_ondisk *ondisk;
	struct hammer_volume *volume;
	struct hammer_alist_live alist_master;
	struct hammer_alist_live alist_btree;
	struct hammer_alist_live alist_record;
	struct hammer_alist_live alist_mdata;
	struct hammer_nod_rb_tree rb_nods_root;	/* cursors in cluster */
	struct hammer_base_elm clu_btree_beg;	/* copy of on-disk info */
	struct hammer_base_elm clu_btree_end;	/* copy of on-disk info */
	int32_t clu_no;
	enum hammer_cluster_state state;
};

typedef struct hammer_cluster *hammer_cluster_t;

/*
 * In-memory buffer (other then volume, super-cluster, or cluster),
 * representing an on-disk buffer.
 */
struct hammer_buffer {
	struct hammer_io io;
	RB_ENTRY(hammer_buffer) rb_node;
	hammer_fsbuf_ondisk_t ondisk;
	struct hammer_volume *volume;
	struct hammer_cluster *cluster;
	int32_t buf_no;
	u_int64_t buf_type;
	struct hammer_alist_live alist;
	struct hammer_node_list clist;
	struct hammer_node *save_scan;
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
	int32_t			node_offset;	/* cluster-rel offset */
	struct hammer_cluster	*cluster;
	struct hammer_buffer	*buffer;	/* backing buffer */
	hammer_node_ondisk_t	ondisk;		/* ptr to on-disk structure */
	struct hammer_node	**cache1;	/* passive cache(s) */
	struct hammer_node	**cache2;
};

typedef struct hammer_node	*hammer_node_t;

/*
 * Common I/O management structure - embedded in in-memory structures
 * which are backed by filesystem buffers.
 */
union hammer_io_structure {
	struct hammer_io	io;
	struct hammer_volume	volume;
	struct hammer_supercl	supercl;
	struct hammer_cluster	cluster;
	struct hammer_buffer	buffer;
};

#define HAMFS_CLUSTER_DIRTY	0x0001

#include "hammer_cursor.h"

/*
 * Internal hammer mount data structure
 */
struct hammer_mount {
	struct mount *mp;
	/*struct vnode *rootvp;*/
	struct hammer_ino_rb_tree rb_inos_root;
	struct hammer_vol_rb_tree rb_vols_root;
	struct hammer_volume *rootvol;
	struct hammer_cluster *rootcl;
	char	*zbuf;	/* HAMMER_BUFSIZE bytes worth of all-zeros */
	int	hflags;
	int	ronly;
	int	nvolumes;
	int	volume_iterator;
	uuid_t	fsid;
	udev_t	fsid_udev;
	hammer_tid_t asof;
	u_int32_t namekey_iterator;
};

typedef struct hammer_mount	*hammer_mount_t;

struct hammer_sync_info {
	int error;
	int waitfor;
};

#endif

#if defined(_KERNEL)

extern struct vop_ops hammer_vnode_vops;
extern struct hammer_alist_config Buf_alist_config;
extern struct hammer_alist_config Vol_normal_alist_config;
extern struct hammer_alist_config Vol_super_alist_config;
extern struct hammer_alist_config Supercl_alist_config;
extern struct hammer_alist_config Clu_master_alist_config;
extern struct hammer_alist_config Clu_slave_alist_config;
extern struct bio_ops hammer_bioops;

int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_vfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp);
int	hammer_get_vnode(struct hammer_inode *ip, int lktype,
			struct vnode **vpp);
struct hammer_inode *hammer_get_inode(hammer_mount_t hmp,
			u_int64_t obj_id, hammer_tid_t asof, int *errorp);
int	hammer_update_inode(hammer_inode_t ip);
void	hammer_put_inode(struct hammer_inode *ip);
void	hammer_put_inode_ref(struct hammer_inode *ip);

int	hammer_unload_inode(hammer_inode_t ip, void *data __unused);
int	hammer_unload_volume(hammer_volume_t volume, void *data __unused);
int	hammer_unload_supercl(hammer_supercl_t supercl, void *data __unused);
int	hammer_unload_cluster(hammer_cluster_t cluster, void *data __unused);
int	hammer_unload_buffer(hammer_buffer_t buffer, void *data __unused);
int	hammer_install_volume(hammer_mount_t hmp, const char *volname);

int	hammer_ip_lookup(hammer_cursor_t cursor, hammer_inode_t ip);
int	hammer_ip_first(hammer_cursor_t cursor, hammer_inode_t ip);
int	hammer_ip_next(hammer_cursor_t cursor);
int	hammer_ip_resolve_data(hammer_cursor_t cursor);
int	hammer_ip_delete_record(hammer_cursor_t cursor, hammer_tid_t tid);

int	hammer_sync_hmp(hammer_mount_t hmp, int waitfor);
int	hammer_sync_volume(hammer_volume_t volume, void *data);
int	hammer_sync_cluster(hammer_cluster_t cluster, void *data);
int	hammer_sync_buffer(hammer_buffer_t buffer, void *data);

hammer_record_t
	hammer_alloc_mem_record(hammer_inode_t ip);
void	hammer_rel_mem_record(struct hammer_record **recordp);
void	hammer_drop_mem_record(hammer_record_t record, int delete);

int	hammer_cursor_up(hammer_cursor_t cursor, int nonblock);
int	hammer_cursor_toroot(hammer_cursor_t cursor);
int	hammer_cursor_down(hammer_cursor_t cursor);

void	hammer_lock_ex(struct hammer_lock *lock);
int	hammer_lock_ex_try(struct hammer_lock *lock);
void	hammer_lock_sh(struct hammer_lock *lock);
void	hammer_unlock(struct hammer_lock *lock);
void	hammer_ref(struct hammer_lock *lock);
void	hammer_unref(struct hammer_lock *lock);
void	hammer_downgrade(struct hammer_lock *lock);

u_int32_t hammer_to_unix_xid(uuid_t *uuid);
void hammer_guid_to_uuid(uuid_t *uuid, u_int32_t guid);
void	hammer_to_timespec(hammer_tid_t tid, struct timespec *ts);
hammer_tid_t hammer_timespec_to_transid(struct timespec *ts);
hammer_tid_t hammer_alloc_tid(hammer_transaction_t trans);
hammer_tid_t hammer_now_tid(void);
hammer_tid_t hammer_alloc_recid(hammer_transaction_t trans);

enum vtype hammer_get_vnode_type(u_int8_t obj_type);
int hammer_get_dtype(u_int8_t obj_type);
u_int8_t hammer_get_obj_type(enum vtype vtype);
int64_t hammer_directory_namekey(void *name, int len);

int	hammer_init_cursor_hmp(hammer_cursor_t cursor, hammer_mount_t hmp);
int	hammer_init_cursor_cluster(hammer_cursor_t cursor, hammer_cluster_t cluster);
int	hammer_init_cursor_ip(hammer_cursor_t cursor, hammer_inode_t ip);

void	hammer_done_cursor(hammer_cursor_t cursor);
void	hammer_mem_done(hammer_cursor_t cursor);

int	hammer_btree_lookup(hammer_cursor_t cursor);
int	hammer_btree_first(hammer_cursor_t cursor);
int	hammer_btree_extract(hammer_cursor_t cursor, int flags);
int	hammer_btree_iterate(hammer_cursor_t cursor);
int	hammer_btree_insert(hammer_cursor_t cursor, hammer_btree_elm_t elm);
int	hammer_btree_delete(hammer_cursor_t cursor);
int	hammer_btree_cmp(hammer_base_elm_t key1, hammer_base_elm_t key2);
int	hammer_btree_chkts(hammer_tid_t ts, hammer_base_elm_t key);
void	hammer_print_btree_node(hammer_node_ondisk_t ondisk);
void	hammer_print_btree_elm(hammer_btree_elm_t elm, u_int8_t type, int i);

void	*hammer_bread(struct hammer_cluster *cluster, int32_t cloff,
		      u_int64_t buf_type, int *errorp,
		      struct hammer_buffer **bufferp);

hammer_volume_t hammer_get_root_volume(hammer_mount_t hmp, int *errorp);
hammer_cluster_t hammer_get_root_cluster(hammer_mount_t hmp, int *errorp);

hammer_volume_t	hammer_get_volume(hammer_mount_t hmp,
			int32_t vol_no, int *errorp);
hammer_supercl_t hammer_get_supercl(hammer_volume_t volume,
			int32_t scl_no, int *errorp, int isnew);
hammer_cluster_t hammer_get_cluster(hammer_volume_t volume,
			int32_t clu_no, int *errorp, int isnew);
hammer_buffer_t	hammer_get_buffer(hammer_cluster_t cluster,
			int32_t buf_no, u_int64_t buf_type, int *errorp);

int		hammer_ref_volume(hammer_volume_t volume);
int		hammer_ref_cluster(hammer_cluster_t cluster);
int		hammer_ref_buffer(hammer_buffer_t buffer);
void		hammer_flush_buffer_nodes(hammer_buffer_t buffer);


void		hammer_rel_volume(hammer_volume_t volume, int flush);
void		hammer_rel_supercl(hammer_supercl_t supercl, int flush);
void		hammer_rel_cluster(hammer_cluster_t cluster, int flush);
void		hammer_rel_buffer(hammer_buffer_t buffer, int flush);

hammer_node_t	hammer_get_node(hammer_cluster_t cluster,
			int32_t node_offset, int *errorp);
int		hammer_ref_node(hammer_node_t node);
void		hammer_rel_node(hammer_node_t node);
void		hammer_cache_node(hammer_node_t node,
			struct hammer_node **cache);
void		hammer_uncache_node(struct hammer_node **cache);
void		hammer_flush_node(hammer_node_t node);

void hammer_dup_buffer(struct hammer_buffer **bufferp,
			struct hammer_buffer *buffer);
void hammer_dup_cluster(struct hammer_cluster **clusterp,
			struct hammer_cluster *cluster);
hammer_cluster_t hammer_alloc_cluster(hammer_mount_t hmp,
			hammer_cluster_t cluster_hint, int *errorp);
void hammer_init_cluster(hammer_cluster_t cluster,
			hammer_base_elm_t left_bound,
			hammer_base_elm_t right_bound);
hammer_node_t hammer_alloc_btree(struct hammer_cluster *cluster, int *errorp);
void *hammer_alloc_data(struct hammer_cluster *cluster, int32_t bytes,
			int *errorp, struct hammer_buffer **bufferp);
void *hammer_alloc_record(struct hammer_cluster *cluster,
			int *errorp, struct hammer_buffer **bufferp);
void hammer_free_data_ptr(struct hammer_buffer *buffer, 
			void *data, int bytes);
void hammer_free_record_ptr(struct hammer_buffer *buffer,
			union hammer_record_ondisk *rec);
void hammer_free_cluster(hammer_cluster_t cluster);
void hammer_free_btree(struct hammer_cluster *cluster, int32_t bclu_offset);
void hammer_free_data(struct hammer_cluster *cluster, int32_t bclu_offset,
			int32_t bytes);
void hammer_free_record(struct hammer_cluster *cluster, int32_t bclu_offset);

void hammer_put_volume(struct hammer_volume *volume, int flush);
void hammer_put_supercl(struct hammer_supercl *supercl, int flush);
void hammer_put_cluster(struct hammer_cluster *cluster, int flush);
void hammer_put_buffer(struct hammer_buffer *buffer, int flush);

void hammer_init_alist_config(void);

void hammer_start_transaction(struct hammer_transaction *trans,
			      struct hammer_mount *hmp);
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
int  hammer_ip_delete_range(struct hammer_transaction *trans,
			hammer_inode_t ip, int64_t ran_beg, int64_t ran_end,
			struct hammer_cursor **spikep);
int  hammer_ip_sync_data(struct hammer_transaction *trans,
			hammer_inode_t ip, int64_t offset,
			void *data, int bytes, struct hammer_cursor **spikep);
int  hammer_ip_sync_record(hammer_record_t rec, struct hammer_cursor **spikep);
int  hammer_write_record(hammer_cursor_t cursor, hammer_record_ondisk_t rec,
			void *data, int cursor_flags);

void hammer_load_spike(hammer_cursor_t cursor, struct hammer_cursor **spikep);
int hammer_spike(struct hammer_cursor **spikep);

int hammer_io_read(struct vnode *devvp, struct hammer_io *io);
int hammer_io_new(struct vnode *devvp, struct hammer_io *io);
void hammer_io_release(struct hammer_io *io, int flush);
int hammer_io_checkflush(hammer_io_t io);
void hammer_io_notify_cluster(hammer_cluster_t cluster);
void hammer_io_flush(struct hammer_io *io, struct hammer_sync_info *info);

#endif

/*
 * Inline support functions (not kernel specific)
 */
static __inline void
hammer_modify_volume(struct hammer_volume *volume)
{
	volume->io.modified = 1;
}

static __inline void
hammer_modify_supercl(struct hammer_supercl *supercl)
{
	supercl->io.modified = 1;
}

static __inline void
hammer_modify_cluster(struct hammer_cluster *cluster)
{
	cluster->io.modified = 1;
}

static __inline void
hammer_modify_buffer(struct hammer_buffer *buffer)
{
	hammer_io_notify_cluster(buffer->cluster);
	buffer->io.modified = 1;
}

static __inline void
hammer_modify_node(struct hammer_node *node)
{
	hammer_modify_buffer(node->buffer);
}

/*
 * Return the cluster-relative byte offset of an element within a buffer
 */
static __inline int
hammer_bclu_offset(struct hammer_buffer *buffer, void *ptr)
{
	int bclu_offset;

	bclu_offset = buffer->buf_no * HAMMER_BUFSIZE + 
		      ((char *)ptr - (char *)buffer->ondisk);
	return(bclu_offset);
}

