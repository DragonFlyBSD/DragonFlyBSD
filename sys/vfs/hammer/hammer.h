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
 * $DragonFly: src/sys/vfs/hammer/hammer.h,v 1.1 2007/10/10 19:37:25 dillon Exp $
 */
/*
 * This header file contains structures used internally by the HAMMERFS
 * implementation.  See hammerfs.h for on-disk structures.
 */

#include <sys/tree.h>
#include <sys/malloc.h>
#include "hammerfs.h"
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

/*
 * Key and caching structure used for HAMMER B-Tree lookups.
 */
struct hammer_btree_info {
	struct hammer_base_elm key;
};

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
	struct hammer_mount *hmp;
};

/*
 * Structures used to internally represent a volume and a cluster
 */

struct hammer_volume;
struct hammer_cluster;
RB_HEAD(hammer_vol_rb_tree, hammer_volume);
RB_HEAD(hammer_clu_rb_tree, hammer_cluster);

RB_PROTOTYPE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	      hammer_vol_rb_compare, int32_t);
RB_PROTOTYPE2(hammer_clu_rb_tree, hammer_cluster, rb_node,
	      hammer_clu_rb_compare, int32_t);

struct hammer_volume {
	RB_ENTRY(hammer_volume) rb_node;
	struct hammer_clu_rb_tree rb_clus_root;
	struct hammer_volume_ondisk *ondisk;
	int32_t	vol_no;
	int32_t	vol_clsize;
	int64_t cluster_base;	/* base offset of cluster 0 */
	char *vol_name;
	struct vnode *devvp;
	struct hammer_mount *hmp;
};

struct hammer_cluster {
	RB_ENTRY(hammer_cluster) rb_node;
	struct hammer_cluster_ondisk *ondisk;
	struct hammer_volume *volume;
	int32_t clu_no;
};

/*
 * Internal hammer mount data structure
 */
struct hammer_mount {
	struct mount *mp;
	struct vnode *rootvp;
	struct hammer_ino_rb_tree rb_inos_root;
	struct hammer_vol_rb_tree rb_vols_root;
	struct hammer_volume *rootvol;
	struct hammer_cluster *rootcl;
	uuid_t	fsid;
};


#endif

#if defined(_KERNEL)

extern struct vop_ops hammer_vnode_vops;
int	hammer_vop_inactive(struct vop_inactive_args *);
int	hammer_vop_reclaim(struct vop_reclaim_args *);
int	hammer_vfs_vget(struct mount *mp, ino_t ino, struct  vnode **vpp);

int	hammer_unload_inode(struct hammer_inode *inode, void *data __unused);
int	hammer_unload_volume(struct hammer_volume *volume, void *data __unused);
int	hammer_load_volume(struct hammer_mount *hmp, const char *volname);
struct hammer_cluster *hammer_load_cluster(struct hammer_mount *hmp,
				struct hammer_volume *volume, int clu_no,
				int *errorp);

int	hammer_btree_lookup(struct hammer_mount *hmp,
				struct hammer_btree_info *info);


#endif

