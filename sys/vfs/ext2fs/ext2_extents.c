/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Zheng Liu <lz@freebsd.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/mutex.h>

#include <vfs/ext2fs/ext2_mount.h>
#include <vfs/ext2fs/fs.h>
#include <vfs/ext2fs/inode.h>
#include <vfs/ext2fs/ext2fs.h>
#include <vfs/ext2fs/ext2_extents.h>
#include <vfs/ext2fs/ext2_extern.h>

SDT_PROVIDER_DECLARE(ext2fs);
/*
 * ext2fs trace probe:
 * arg0: verbosity. Higher numbers give more verbose messages
 * arg1: Textual message
 */
SDT_PROBE_DEFINE2(ext2fs, , trace, extents, "int", "char*");

#ifdef EXT2FS_PRINT_EXTENTS
static void
ext4_ext_print_extent(struct ext4_extent *ep)
{
}

static void
ext4_ext_print_index(struct inode *ip, struct ext4_extent_index *ex, int do_walk)
{
}

static void
ext4_ext_print_header(struct inode *ip, struct ext4_extent_header *ehp)
{
}

static void
ext4_ext_print_path(struct inode *ip, struct ext4_extent_path *path)
{
}

void
ext4_ext_print_extent_tree_status(struct inode *ip)
{
}
#endif

static inline struct ext4_extent_header *
ext4_ext_inode_header(struct inode *ip)
{
	return (NULL);
}

static inline struct ext4_extent_header *
ext4_ext_block_header(char *bdata)
{
	return (NULL);
}

static inline unsigned short
ext4_ext_inode_depth(struct inode *ip)
{
	return (0);
}

static inline e4fs_daddr_t
ext4_ext_index_pblock(struct ext4_extent_index *index)
{
	return (0);
}

static inline void
ext4_index_store_pblock(struct ext4_extent_index *index, e4fs_daddr_t pb)
{
}

static inline e4fs_daddr_t
ext4_ext_extent_pblock(struct ext4_extent *extent)
{
	return (0);
}

static inline void
ext4_ext_store_pblock(struct ext4_extent *ex, e4fs_daddr_t pb)
{
}

int
ext4_ext_in_cache(struct inode *ip, daddr_t lbn, struct ext4_extent *ep)
{
	return (EXT4_EXT_CACHE_NO);
}

void
ext4_ext_path_free(struct ext4_extent_path *path)
{
}

int
ext4_ext_find_extent(struct inode *ip, daddr_t block,
    struct ext4_extent_path **ppath)
{
	return (EINVAL);
}

static inline int
ext4_ext_space_root(struct inode *ip)
{
	return (0);
}

static inline int
ext4_ext_space_block(struct inode *ip)
{
	return (0);
}

static inline int
ext4_ext_space_block_index(struct inode *ip)
{
	return (0);
}

void
ext4_ext_tree_init(struct inode *ip)
{
}

static inline void
ext4_ext_put_in_cache(struct inode *ip, uint32_t blk,
			uint32_t len, uint32_t start, int type)
{
}

static inline int
ext4_can_extents_be_merged(struct ext4_extent *ex1,
    struct ext4_extent *ex2)
{
	return (0);
}

int
ext4_ext_get_blocks(struct inode *ip, e4fs_daddr_t iblk,
    unsigned long max_blocks, struct ucred *cred, struct buf **bpp,
    int *pallocated, daddr_t *nb)
{
	return (EINVAL);
}

static inline uint16_t
ext4_ext_get_actual_len(struct ext4_extent *ext)
{
	return (0);
}

static inline struct ext4_extent_header *
ext4_ext_header(struct inode *ip)
{
	return (NULL);
}

static inline int
ext4_ext_more_to_rm(struct ext4_extent_path *path)
{
	return (0);
}

int
ext4_ext_remove_space(struct inode *ip, off_t length, int flags,
    struct ucred *cred, struct thread *td)
{
	return (EINVAL);
}
