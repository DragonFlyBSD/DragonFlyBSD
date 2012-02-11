/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * This header file contains structures used internally by the HAMMER2
 * implementation.  See hammer2_disk.h for on-disk structures.
 */

#ifndef _VFS_HAMMER2_HAMMER2_H_
#define _VFS_HAMMER2_HAMMER2_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/mountctl.h>
#include <sys/priv.h>
#include <sys/stat.h>
#include <sys/globaldata.h>
#include <sys/lockf.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/limits.h>
#include <sys/buf2.h>
#include <sys/signal2.h>
#include <sys/tree.h>

#include "hammer2_disk.h"
#include "hammer2_mount.h"

struct hammer2_inode;
struct hammer2_mount;

/*
 * The chain structure tracks blockref recursions all the way to
 * the root volume.  These consist of indirect blocks, inodes,
 * and eventually the volume header.
 *
 * The chain structure is embedded in the hammer2_mount, hammer2_inode,
 * and other system memory structures.  The chain structure typically
 * implements the reference count and busy flag for the larger structure.
 *
 * It is always possible to track a chain element all the way back to the
 * root by following the (parent) links.  (index) is a type-dependent index
 * in the parent indicating where in the parent the chain element resides.
 */
struct hammer2_chain {
	struct hammer2_blockref	bref;
	struct hammer2_chain *parent;	/* return chain to root */
	struct hammer2_chain *subs;	/* children (base) */
	struct hammer2_chain *next;	/* linked list */
	union {
		struct hammer2_inode *ip;
		struct hammer2_indblock *ind;
	} u;
	int	index;			/* index in parent */
	u_int	refs;
	u_int	busy;
};

typedef struct hammer2_chain hammer2_chain_t;

/*
 * A hammer2 inode.
 *
 * NOTE: An inode's ref count shares chain.refs.
 */
struct hammer2_inode {
	struct hammer2_mount	*hmp;
	struct lock		lk;
	struct vnode		*vp;
	hammer2_chain_t		chain;
	hammer2_inode_data_t	data;
};

typedef struct hammer2_inode hammer2_inode_t;

/*
 * A hammer2 indirect block
 */
struct hammer2_indblock {
	struct buf		*bp;
	hammer2_chain_t		chain;
	hammer2_indblock_data_t	*data;
};

typedef struct hammer2_indblock hammer2_indblock_t;

/*
 * Governing mount structure for filesystem (aka vp->v_mount)
 */
struct hammer2_mount {
	struct mount	*mp;		/* kernel mount */
	struct vnode	*devvp;		/* device vnode */
	struct lock	lk;
	struct netexport export;	/* nfs export */
	int		ronly;		/* read-only mount */

	struct malloc_type *inodes;
	int 		ninodes;
	int 		maxinodes;

	struct malloc_type *ipstacks;
	int		nipstacks;
	int		maxipstacks;
	hammer2_chain_t vchain;		/* anchor chain */
	hammer2_chain_t *schain;	/* super-root */
	hammer2_chain_t *rchain;	/* label-root */
	struct hammer2_inode *iroot;

	hammer2_volume_data_t voldata;
	hammer2_off_t	freecache[HAMMER2_MAX_RADIX];
};

typedef struct hammer2_mount hammer2_mount_t;

#if defined(_KERNEL)

MALLOC_DECLARE(M_HAMMER2);

static __inline
struct mount *
H2TOMP(struct hammer2_mount *hmp)
{
	return (struct mount *) hmp->mp;
}

#define VTOI(vp)	((hammer2_inode_t *)(vp)->v_data)
#define ITOV(ip)	((ip)->vp)

static __inline
struct hammer2_mount *
MPTOH2(struct mount *mp)
{
	return (hammer2_mount_t *) mp->mnt_data;
}

extern struct vop_ops hammer2_vnode_vops;
extern struct vop_ops hammer2_spec_vops;
extern struct vop_ops hammer2_fifo_vops;

/*
 * hammer2_subr.c
 */
void hammer2_inode_lock_sh(hammer2_inode_t *ip);
void hammer2_inode_lock_up(hammer2_inode_t *ip);
void hammer2_inode_lock_ex(hammer2_inode_t *ip);
void hammer2_inode_unlock_ex(hammer2_inode_t *ip);
void hammer2_inode_unlock_up(hammer2_inode_t *ip);
void hammer2_inode_unlock_sh(hammer2_inode_t *ip);

void hammer2_mount_exlock(hammer2_mount_t *hmp);
void hammer2_mount_shlock(hammer2_mount_t *hmp);
void hammer2_mount_unlock(hammer2_mount_t *hmp);

hammer2_key_t hammer2_dirhash(const unsigned char *name, size_t len);

/*
 * hammer2_inode.c
 */
struct vnode *hammer2_igetv(hammer2_inode_t *ip, int *errorp);
hammer2_inode_t *hammer2_inode_alloc(hammer2_mount_t *hmp, void *data);
void hammer2_inode_free(hammer2_inode_t *ip);
void hammer2_inode_ref(hammer2_inode_t *ip);
void hammer2_inode_drop(hammer2_inode_t *ip);

/*
 * hammer2_chain.c
 */
void hammer2_chain_ref(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_drop(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_link(hammer2_mount_t *hmp __unused, hammer2_chain_t *parent,
				    int index, hammer2_chain_t *chain);
void hammer2_chain_unlink(hammer2_mount_t *hmp, hammer2_chain_t *chain);

hammer2_chain_t *hammer2_chain_get(hammer2_mount_t *hmp,
				    hammer2_chain_t *parent, int index,
				    hammer2_blockref_t *bref);
void hammer2_chain_put(hammer2_mount_t *hmp, hammer2_chain_t *chain);

hammer2_chain_t *hammer2_chain_push(hammer2_mount_t *hmp,
				    hammer2_chain_t *parent,
				    hammer2_key_t key);

hammer2_chain_t *hammer2_chain_first(hammer2_mount_t *hmp,
				    hammer2_chain_t *parent,
				    hammer2_key_t key,
				    hammer2_key_t mask);

hammer2_chain_t *hammer2_chain_next(hammer2_mount_t *hmp,
				    hammer2_chain_t *current,
				    hammer2_key_t key,
				    hammer2_key_t mask);

/*
 * hammer2_freemap.c
 */
hammer2_off_t hammer_freemap_alloc(hammer2_mount_t *hmp, size_t bytes);

#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
