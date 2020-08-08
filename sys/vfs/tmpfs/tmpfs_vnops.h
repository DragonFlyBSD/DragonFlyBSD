/*	$NetBSD: tmpfs_vnops.h,v 1.7 2005/12/03 17:34:44 christos Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/fs/tmpfs/tmpfs_vnops.h,v 1.3 2008/09/03 18:53:48 delphij Exp $
 */

#ifndef _VFS_TMPFS_TMPFS_VNOPS_H_
#define _VFS_TMPFS_TMPFS_VNOPS_H_

#if !defined(_KERNEL)
#error not supposed to be exposed to userland.
#endif

/* --------------------------------------------------------------------- */

extern struct vop_ops tmpfs_vnode_vops;
extern struct vop_ops tmpfs_fifo_vops;
extern int tmpfs_bufcache_mode __read_mostly;

/*
 * Declarations for tmpfs_vnops.c.
 */

int tmpfs_access(struct vop_access_args *);
int tmpfs_getattr(struct vop_getattr_args *);
int tmpfs_getattr_lite(struct vop_getattr_lite_args *);
int tmpfs_setattr(struct vop_setattr_args *);
int tmpfs_reclaim(struct vop_reclaim_args *);

/* --------------------------------------------------------------------- */

#endif /* _VFS_TMPFS_TMPFS_VNOPS_H_ */
