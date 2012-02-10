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
 * A hammer2 inode.
 */
struct hammer2_inode {
	struct hammer2_mount	*mp;
	struct lock		lk;
	struct vnode		*vp;
	hammer2_tid_t		inum;
	unsigned char		type;
	int			busy;
};

#define HAMMER2_INODE_TYPE_DIR	0x01
#define HAMMER2_INODE_TYPE_FILE	0x02
#define HAMMER2_INODE_TYPE_ROOT	0x10
#define HAMMER2_INODE_TYPE_MASK	0x07

/*
 * Governing mount structure for filesystem (aka vp->v_mount)
 */
struct hammer2_mount {
	struct mount	*hm_mp;
	int		hm_ronly;	/* block device mounted read-only */
	struct vnode	*hm_devvp;	/* device vnode */
	struct lock	hm_lk;

	/* Root inode */
	struct hammer2_inode	*hm_iroot;

	/* Per-mount inode zone */
	struct malloc_type *hm_inodes;
	int 		hm_ninodes;
	int 		hm_maxinodes;

	struct malloc_type *hm_ipstacks;
	int		hm_nipstacks;
	int		hm_maxipstacks;

	struct hammer2_volume_data hm_sb;

	struct netexport	hm_export;
};

#if defined(_KERNEL)

MALLOC_DECLARE(M_HAMMER2);

static inline struct mount *
H2TOMP(struct hammer2_mount *hmp)
{
	return (struct mount *) hmp->hm_mp;
}

#define VTOI(vp)	((struct hammer2_inode *) (vp)->v_data)
#define ITOV(ip)	((ip)->vp)

static inline struct hammer2_mount *
MPTOH2(struct mount *mp)
{
	return (struct hammer2_mount *) mp->mnt_data;
}

extern struct vop_ops hammer2_vnode_vops;
extern struct vop_ops hammer2_spec_vops;
extern struct vop_ops hammer2_fifo_vops;

/* hammer2_inode.c */

extern int hammer2_inactive(struct vop_inactive_args *);
extern int hammer2_reclaim(struct vop_reclaim_args *);

/* hammer2_subr.c */

extern struct vnode *igetv(struct hammer2_inode *, int *);

extern void hammer2_mount_exlock(struct hammer2_mount *);
extern void hammer2_mount_shlock(struct hammer2_mount *);
extern void hammer2_mount_unlock(struct hammer2_mount *);

#endif /* !_KERNEL */
#endif /* !_VFS_HAMMER2_HAMMER2_H_ */
