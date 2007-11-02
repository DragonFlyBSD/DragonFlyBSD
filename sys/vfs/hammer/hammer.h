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
 * $DragonFly: src/sys/vfs/hammer/hammer.h,v 1.4 2007/11/02 00:57:15 dillon Exp $
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
#include "hammer_alist.h"
#include "hammer_disk.h"
#include "hammer_mount.h"

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

MALLOC_DECLARE(M_HAMMER);

/*
 * Key structure used for custom RB tree inode lookups.  This prototypes
 * the function hammer_ino_rb_tree_RB_LOOKUP_INFO(root, info).
 */
typedef struct hammer_inode_info {
	u_int64_t	obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
} *hammer_inode_info_t;

struct hammer_lock {
	int	refs;
	int	wanted;
	struct thread *locktd;
};

static __inline int
hammer_islocked(struct hammer_lock *lock)
{
	return(lock->refs > 0);
}

static __inline int
hammer_islastref(struct hammer_lock *lock)
{
	return(lock->refs == 1);
}

/*
 * Structures used to internally represent an inode
 */
struct hammer_ino_rb_tree;
struct hammer_inode;
RB_HEAD(hammer_ino_rb_tree, hammer_inode);
RB_PROTOTYPEX(hammer_ino_rb_tree, INFO, hammer_inode, rb_node,
	     hammer_ino_rb_compare, hammer_inode_info_t);

struct hammer_inode {
	RB_ENTRY(hammer_inode) rb_node;
	u_int64_t	obj_id;		/* (key) object identifier */
	hammer_tid_t	obj_asof;	/* (key) snapshot transid or 0 */
	struct vnode	*vp;
	struct hammer_inode_record ino_rec;
	struct hammer_inode_data ino_data;
	struct hammer_lock lock;
};

/*
 * Structures used to internally represent a volume and a cluster
 */

struct hammer_volume;
struct hammer_cluster;
struct hammer_supercl;
struct hammer_buffer;
RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_clu_rb_tree, hammer_cluster);
RB_HEAD(hammer_scl_rb_tree, hammer_supercl);
RB_HEAD(hammer_buf_rb_tree, hammer_buffer);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_clu_rb_tree, hammer_cluster, rb_node,
	      hammer_clu_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_scl_rb_tree, hammer_supercl, rb_node,
	      hammer_scl_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	      hammer_buf_rb_compare, int32_t);

struct hammer_volume {
	RB_ENTRY(hammer_volume) rb_node;
	struct hammer_clu_rb_tree rb_clus_root;
	struct hammer_scl_rb_tree rb_scls_root;
	struct buf *bp;
	struct hammer_volume_ondisk *ondisk;
	struct hammer_alist_live alist;
	int32_t	vol_no;
	int32_t	vol_clsize;
	int64_t cluster_base;	/* base offset of cluster 0 */
	char	*vol_name;
	struct vnode *devvp;
	struct hammer_mount *hmp;
	struct hammer_lock lock;
	int	vol_flags;
	int	modified;
};

struct hammer_supercl {
	RB_ENTRY(hammer_supercl) rb_node;
	struct buf *bp;
	struct hammer_supercl_ondisk *ondisk;
	struct hammer_volume *volume;
	struct hammer_alist_live alist;
	int32_t	scl_no;
	int64_t scl_offset;
	struct hammer_lock lock;
	int	modified;
};

struct hammer_cluster {
	RB_ENTRY(hammer_cluster) rb_node;
	struct hammer_buf_rb_tree rb_bufs_root;
	struct buf *bp;
	struct hammer_cluster_ondisk *ondisk;
	struct hammer_volume *volume;
	struct hammer_alist_live alist_master;
	struct hammer_alist_live alist_btree;
	struct hammer_alist_live alist_record;
	struct hammer_alist_live alist_mdata;
	int32_t clu_no;
	int64_t clu_offset;
	struct hammer_lock lock;
	int	modified;
};

struct hammer_buffer {
	RB_ENTRY(hammer_buffer) rb_node;
	struct buf *bp;
	hammer_fsbuf_ondisk_t ondisk;
	struct hammer_cluster *cluster;
	struct hammer_volume *volume;
	struct hammer_alist_live alist;
	int32_t buf_no;
	int64_t buf_offset;
	struct hammer_lock lock;
	int	modified;
};

#define HAMFS_CLUSTER_DIRTY	0x0001

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
	/* struct hammer_volume *cache_volume */
	/* struct hammer_cluster *cache_cluster */
	/* struct hammer_buffer *cache_buffer */
	uuid_t	fsid;
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

int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_vfs_vget(struct mount *mp, ino_t ino, struct  vnode **vpp);

int	hammer_unload_inode(struct hammer_inode *ip, void *data);
int	hammer_unload_volume(struct hammer_volume *volume, void *data __unused);
int	hammer_load_volume(struct hammer_mount *hmp, const char *volname);
struct hammer_cluster *hammer_load_cluster(struct hammer_volume *volume,
					   int clu_no, int *errorp);

void	hammer_lock(struct hammer_lock *lock);
void	hammer_unlock(struct hammer_lock *lock);

int	hammer_btree_lookup(hammer_btree_info_t info,
			    hammer_base_elm_t key, int flags);
int	hammer_btree_insert(hammer_btree_info_t info,
			    hammer_btree_leaf_elm_t elm);
int	hammer_btree_delete(hammer_btree_info_t info, hammer_base_elm_t key);
int	hammer_btree_cursor_init(hammer_btree_cursor_t cursor,
			    struct hammer_cluster *cluster);
void	hammer_btree_cursor_done(hammer_btree_cursor_t cusor);
int	hammer_btree_info_init(hammer_btree_info_t info,
			    struct hammer_cluster *cluster);
void	hammer_btree_info_done(hammer_btree_info_t info);

void	*hammer_bread(struct hammer_cluster *cluster, int32_t cloff,
		      u_int64_t buf_type,
		      int *errorp, struct hammer_buffer **bufferp);

struct hammer_volume *hammer_get_volume(struct hammer_mount *hmp,
			int32_t vol_no, int *errorp);
struct hammer_supercl *hammer_get_supercl(struct hammer_volume *volume,
			int32_t scl_no, int *errorp, int isnew);
struct hammer_cluster *hammer_get_cluster(struct hammer_volume *volume,
			int32_t clu_no, int *errorp, int isnew);
struct hammer_buffer *hammer_get_buffer(struct hammer_cluster *cluster,
			int32_t buf_no, int64_t buf_type, int *errorp);
void hammer_dup_buffer(struct hammer_buffer **bufferp,
			struct hammer_buffer *buffer);
void hammer_dup_cluster(struct hammer_cluster **clusterp,
			struct hammer_cluster *cluster);
void *hammer_alloc_btree(struct hammer_cluster *cluster,	
			int *errorp, struct hammer_buffer **bufferp);
void *hammer_alloc_data(struct hammer_cluster *cluster, int32_t bytes,
			int *errorp, struct hammer_buffer **bufferp);
void *hammer_alloc_record(struct hammer_cluster *cluster,
			int *errorp, struct hammer_buffer **bufferp);
void hammer_free_btree_ptr(struct hammer_buffer *buffer,
			hammer_btree_node_t node);
void hammer_free_data_ptr(struct hammer_buffer *buffer, 
			void *data, int bytes);
void hammer_free_record_ptr(struct hammer_buffer *buffer,
			union hammer_record_ondisk *rec);
void hammer_free_btree(struct hammer_cluster *cluster, int32_t bclu_offset);
void hammer_free_data(struct hammer_cluster *cluster, int32_t bclu_offset,
			int32_t bytes);
void hammer_free_record(struct hammer_cluster *cluster, int32_t bclu_offset);

void hammer_put_volume(struct hammer_volume *volume);
void hammer_put_supercl(struct hammer_supercl *supercl);
void hammer_put_cluster(struct hammer_cluster *cluster);
void hammer_put_buffer(struct hammer_buffer *buffer);

void hammer_init_alist_config(void);

#endif

/*
 * Inline support functions (not kernel specific)
 */
static __inline void
hammer_modify_volume(struct hammer_volume *volume)
{
	volume->modified = 1;
}

static __inline void
hammer_modify_supercl(struct hammer_supercl *supercl)
{
	supercl->modified = 1;
}

static __inline void
hammer_modify_cluster(struct hammer_cluster *cluster)
{
	cluster->modified = 1;
}

static __inline void
hammer_modify_buffer(struct hammer_buffer *buffer)
{
	buffer->modified = 1;
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

