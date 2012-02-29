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

struct hammer2_chain;
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
 *
 * When a blockref is added or deleted the related chain element is marked
 * modified and all of its parents are marked SUBMODIFIED (the parent
 * recursion can stop once we hit a node that is already marked SUBMODIFIED).
 * A deleted chain element must remain intact until synchronized against
 * its parent.
 *
 * The blockref at (parent, index) is not adjusted until the modified chain
 * element is flushed and unmarked.  Until then the child's blockref may
 * not match the blockref at (parent, index).
 */
SPLAY_HEAD(hammer2_chain_splay, hammer2_chain);

struct hammer2_chain {
	struct hammer2_blockref	bref;
	struct hammer2_chain *parent;	/* return chain to root */
	struct hammer2_chain_splay shead;
	SPLAY_ENTRY(hammer2_chain) snode;
	union {
		struct hammer2_inode *ip;
		struct hammer2_indblock *np;
		struct hammer2_data *dp;
		void *mem;
	} u;

	struct buf	*bp;		/* buffer cache (ro) */
	hammer2_media_data_t *data;	/* modified copy of data (rw) */

	struct lock	lk;		/* lockmgr lock */
	int		index;		/* index in parent */
	u_int		refs;
	u_int		busy;		/* soft-busy */
	u_int		flags;
};

typedef struct hammer2_chain hammer2_chain_t;

int hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2);
SPLAY_PROTOTYPE(hammer2_chain_splay, hammer2_chain, snode, hammer2_chain_cmp);

#define HAMMER2_CHAIN_MODIFIED1		0x00000001	/* active mods */
#define HAMMER2_CHAIN_MODIFIED2		0x00000002	/* queued mods */
#define HAMMER2_CHAIN_UNUSED0004	0x00000004
#define HAMMER2_CHAIN_SUBMODIFIED	0x00000008	/* 1+ subs modified */
#define HAMMER2_CHAIN_DELETED		0x00000010
#define HAMMER2_CHAIN_INITIAL		0x00000020	/* initial write */
#define HAMMER2_CHAIN_FLUSHED		0x00000040	/* flush on unlock */
#define HAMMER2_CHAIN_MOVED		0x00000080	/* moved */
#define HAMMER2_CHAIN_IOFLUSH		0x00000100	/* bawrite on put */
#define HAMMER2_CHAIN_WAS_MODIFIED	0x00000200	/* used w/rename */

/*
 * Flags passed to hammer2_chain_lookup() and hammer2_chain_next()
 */
#define HAMMER2_LOOKUP_NOLOCK		0x00000001	/* ref only */

/*
 * HAMMER2 IN-MEMORY CACHE OF MEDIA STRUCTURES
 *
 * There is an in-memory representation of all on-media data structure.
 *
 * When accessed read-only the data will be mapped to the related buffer
 * cache buffer.
 *
 * When accessed read-write (marked modified) a kmalloc()'d copy of the
 * is created which can then be modified.  The copy is destroyed when a
 * filesystem block is allocated to replace it.
 *
 * Active inodes (those with vnodes attached) will maintain the kmalloc()'d
 * copy for both the read-only and the read-write case.  The combination of
 * (bp) and (data) determines whether (data) was allocated or not.
 *
 * The in-memory representation may remain cached (for example in order to
 * placemark clustering locks) even after the related data has been
 * detached.
 */

/*
 * A hammer2 inode.
 */
struct hammer2_inode {
	struct hammer2_mount	*hmp;
	struct hammer2_inode	*pip;		/* parent inode */
	struct vnode		*vp;
	hammer2_chain_t		chain;
	struct hammer2_inode_data ip_data;
	struct lockf		advlock;
};

typedef struct hammer2_inode hammer2_inode_t;

/*
 * A hammer2 indirect block
 */
struct hammer2_indblock {
	hammer2_chain_t		chain;
};

typedef struct hammer2_indblock hammer2_indblock_t;

#define np_data		chain.data->npdata

/*
 * A hammer2 data block
 */
struct hammer2_data {
	hammer2_chain_t		chain;
};

#define dp_data		chain.data->buf

typedef struct hammer2_data hammer2_data_t;

/*
 * Governing mount structure for filesystem (aka vp->v_mount)
 */
struct hammer2_mount {
	struct mount	*mp;		/* kernel mount */
	struct vnode	*devvp;		/* device vnode */
	struct netexport export;	/* nfs export */
	int		ronly;		/* read-only mount */

	struct malloc_type *minode;
	int 		ninodes;
	int 		maxinodes;

	struct malloc_type *mchain;
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

extern int hammer2_debug;

/*
 * hammer2_subr.c
 */
void hammer2_inode_lock_ex(hammer2_inode_t *ip);
void hammer2_inode_unlock_ex(hammer2_inode_t *ip);
void hammer2_inode_lock_sh(hammer2_inode_t *ip);
void hammer2_inode_unlock_sh(hammer2_inode_t *ip);
void hammer2_inode_busy(hammer2_inode_t *ip);
void hammer2_inode_unbusy(hammer2_inode_t *ip);

void hammer2_mount_exlock(hammer2_mount_t *hmp);
void hammer2_mount_shlock(hammer2_mount_t *hmp);
void hammer2_mount_unlock(hammer2_mount_t *hmp);

int hammer2_get_dtype(hammer2_inode_t *ip);
int hammer2_get_vtype(hammer2_inode_t *ip);
u_int8_t hammer2_get_obj_type(enum vtype vtype);
void hammer2_time_to_timespec(u_int64_t xtime, struct timespec *ts);
u_int32_t hammer2_to_unix_xid(uuid_t *uuid);

hammer2_key_t hammer2_dirhash(const unsigned char *name, size_t len);

/*
 * hammer2_inode.c
 */
struct vnode *hammer2_igetv(hammer2_inode_t *ip, int *errorp);
hammer2_inode_t *hammer2_inode_alloc(hammer2_mount_t *hmp, void *data);
void hammer2_inode_free(hammer2_inode_t *ip);
void hammer2_inode_ref(hammer2_inode_t *ip);
void hammer2_inode_drop(hammer2_inode_t *ip);

int hammer2_inode_create(hammer2_mount_t *hmp,
			struct vattr *vap, struct ucred *cred,
			hammer2_inode_t *dip,
			const uint8_t *name, size_t name_len,
			hammer2_inode_t **nipp);

int hammer2_inode_connect(hammer2_inode_t *dip, hammer2_inode_t *nip,
			const uint8_t *name, size_t name_len);

int hammer2_hardlink_create(hammer2_inode_t *ip, hammer2_inode_t *dip,
			const uint8_t *name, size_t name_len);

/*
 * hammer2_chain.c
 */
hammer2_chain_t *hammer2_chain_alloc(hammer2_mount_t *hmp,
				hammer2_blockref_t *bref);
void hammer2_chain_free(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_ref(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_drop(hammer2_mount_t *hmp, hammer2_chain_t *chain);
int hammer2_chain_lock(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_modify(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_unlock(hammer2_mount_t *hmp, hammer2_chain_t *chain);
hammer2_chain_t *hammer2_chain_find(hammer2_mount_t *hmp,
				hammer2_chain_t *parent, int index);
hammer2_chain_t *hammer2_chain_get(hammer2_mount_t *hmp,
				hammer2_chain_t *parent,
				int index, int flags);
void hammer2_chain_put(hammer2_mount_t *hmp, hammer2_chain_t *chain);
hammer2_chain_t *hammer2_chain_lookup(hammer2_mount_t *hmp,
				hammer2_chain_t **parentp,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int flags);
hammer2_chain_t *hammer2_chain_next(hammer2_mount_t *hmp,
				hammer2_chain_t **parentp,
				hammer2_chain_t *chain,
				hammer2_key_t key_beg, hammer2_key_t key_end,
				int flags);
hammer2_chain_t *hammer2_chain_create(hammer2_mount_t *hmp,
				hammer2_chain_t *parent,
				hammer2_chain_t *chain,
				hammer2_key_t key, int keybits,
				int type, size_t bytes);
void hammer2_chain_delete(hammer2_mount_t *hmp, hammer2_chain_t *parent,
				hammer2_chain_t *chain);
void hammer2_chain_flush(hammer2_mount_t *hmp, hammer2_chain_t *chain);
void hammer2_chain_commit(hammer2_mount_t *hmp, hammer2_chain_t *chain);

/*
 * hammer2_freemap.c
 */
hammer2_off_t hammer2_freemap_alloc(hammer2_mount_t *hmp, size_t bytes);
int hammer2_freemap_bytes_to_radix(size_t bytes);

#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
