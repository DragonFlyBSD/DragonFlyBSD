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
#include <sys/buf2.h>
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

static MALLOC_DEFINE(M_EXT2EXTENTS, "ext2_extents", "EXT2 extents");

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
	return ((struct ext4_extent_header *)ip->i_db);
}

static inline struct ext4_extent_header *
ext4_ext_block_header(char *bdata)
{
	return ((struct ext4_extent_header *)bdata);
}

static inline unsigned short
ext4_ext_inode_depth(struct inode *ip)
{
	struct ext4_extent_header *ehp;

	ehp = (struct ext4_extent_header *)ip->i_data;
	return (le16toh(ehp->eh_depth));
}

static inline e4fs_daddr_t
ext4_ext_index_pblock(struct ext4_extent_index *index)
{
	e4fs_daddr_t blk;

	blk = le32toh(index->ei_leaf_lo);
	blk |= (e4fs_daddr_t)le16toh(index->ei_leaf_hi) << 32;

	return (blk);
}

static inline void
ext4_index_store_pblock(struct ext4_extent_index *index, e4fs_daddr_t pb)
{
}

static inline e4fs_daddr_t
ext4_ext_extent_pblock(struct ext4_extent *extent)
{
	e4fs_daddr_t blk;

	blk = le32toh(extent->e_start_lo);
	blk |= (e4fs_daddr_t)le16toh(extent->e_start_hi) << 32;

	return (blk);
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

static inline int
ext4_ext_space_root(struct inode *ip)
{
	int size;

	size = sizeof(ip->i_data);
	size -= sizeof(struct ext4_extent_header);
	size /= sizeof(struct ext4_extent);

	return (size);
}

static inline int
ext4_ext_space_block(struct inode *ip)
{
	struct m_ext2fs *fs;
	int size;

	fs = ip->i_e2fs;

	size = (fs->e2fs_bsize - sizeof(struct ext4_extent_header)) /
	    sizeof(struct ext4_extent);

	return (size);
}

static inline int
ext4_ext_space_root_idx(struct inode *ip)
{
	int size;

	size = sizeof(ip->i_data);
	size -= sizeof(struct ext4_extent_header);
	size /= sizeof(struct ext4_extent_index);

	return (size);
}

static inline int
ext4_ext_space_block_idx(struct inode *ip)
{
	struct m_ext2fs *fs;
	int size;

	fs = ip->i_e2fs;

	size = (fs->e2fs_bsize - sizeof(struct ext4_extent_header)) /
	    sizeof(struct ext4_extent_index);

	return (size);
}

static int
ext4_ext_max_entries(struct inode *ip, int depth)
{
	if (depth == ext4_ext_inode_depth(ip)) {
		if (depth == 0)
			return (ext4_ext_space_root(ip));
		else
			return (ext4_ext_space_root_idx(ip));
	} else {
		if (depth == 0)
			return (ext4_ext_space_block(ip));
		else
			return (ext4_ext_space_block_idx(ip));
	}
}

static inline uint16_t
ext4_ext_get_actual_len(struct ext4_extent *ext)
{
	return (le16toh(ext->e_len) <= EXT_INIT_MAX_LEN ?
	    le16toh(ext->e_len) : (le16toh(ext->e_len) - EXT_INIT_MAX_LEN));
}

static int
ext4_inode_block_validate(struct inode *ip, e4fs_daddr_t start_blk,
    unsigned int count)
{
	struct m_ext2fs *fs;

	fs = ip->i_e2fs;

	if ((start_blk <= le32toh(fs->e2fs->e2fs_first_dblock)) ||
	    (start_blk + count < start_blk) ||
	    (start_blk + count > fs->e2fs_bcount))
		return (EIO);

	return (0);
}

static int
ext4_validate_extent(struct inode *ip, struct ext4_extent *ext)
{
	e4fs_daddr_t blk = ext4_ext_extent_pblock(ext);
	uint32_t lblk = le32toh(ext->e_blk);
	int len = ext4_ext_get_actual_len(ext);

	if (lblk + len <= lblk)
		return (EIO);

	return (ext4_inode_block_validate(ip, blk, len));
}

static int
ext4_validate_extent_idx(struct inode *ip, struct ext4_extent_index *ext_idx)
{
	e4fs_daddr_t blk = ext4_ext_index_pblock(ext_idx);

	return (ext4_inode_block_validate(ip, blk, 1));
}

static int
ext4_validate_extent_entries(struct inode *ip, struct ext4_extent_header *eh,
    int depth)
{
	unsigned int count;

	count = le16toh(eh->eh_ecount);
	if (count == 0)
		return (0);

	if (depth == 0) {
		struct ext4_extent *ext = EXT_FIRST_EXTENT(eh);
		uint32_t lblk = 0;
		uint32_t next = 0;
		int len = 0;

		while (count) {
			if (ext4_validate_extent(ip, ext))
				return (EIO);

			lblk = le32toh(ext->e_blk);
			len = ext4_ext_get_actual_len(ext);
			if (lblk < next)
				return (EIO);

			ext++;
			count--;
			next = lblk + len;
		}
	} else {
		struct ext4_extent_index *ext_idx = EXT_FIRST_INDEX(eh);
		uint32_t ilblk = 0;
		uint32_t icur = 0;

		while (count) {
			if (ext4_validate_extent_idx(ip, ext_idx))
				return (EIO);

			ilblk = le32toh(ext_idx->ei_blk);
			if (ilblk < icur)
				return (EIO);
			icur = ilblk + 1;

			ext_idx++;
			count--;
		}
	}

	return (0);
}

static int
ext4_ext_check_header(struct inode *ip, struct ext4_extent_header *eh,
    int depth)
{
	char *error_msg = NULL;

	if (le16toh(eh->eh_magic) != EXT4_EXT_MAGIC) {
		error_msg = "header: invalid magic";
		goto corrupted;
	}
	if (le16toh(eh->eh_depth) != depth ||
	    le16toh(eh->eh_depth) > EXT4_EXT_DEPTH_MAX) {
		error_msg = "header: invalid eh_depth";
		goto corrupted;
	}
	if (eh->eh_max == 0) {
		error_msg = "header: invalid eh_max";
		goto corrupted;
	}
	if (le16toh(eh->eh_max) > ext4_ext_max_entries(ip, depth)) {
		error_msg = "header: too large eh_max";
		goto corrupted;
	}
	if (le16toh(eh->eh_ecount) > le16toh(eh->eh_max)) {
		error_msg = "header: invalid eh_entries";
		goto corrupted;
	}
	if (le16toh(eh->eh_ecount) == 0 && depth > 0) {
		error_msg = "header: empty index node";
		goto corrupted;
	}
	if (ext4_validate_extent_entries(ip, eh, depth)) {
		error_msg = "header: invalid extent entries";
		goto corrupted;
	}

	return (0);

corrupted:
	SDT_PROBE2(ext2fs, , trace, extents, 1, error_msg);
	return (EIO);
}

static void
ext4_ext_binsearch_index(struct ext4_extent_path *path, int blk)
{
	struct ext4_extent_header *eh;
	struct ext4_extent_index *r, *l, *m;

	eh = path->ep_header;

	KASSERT(le16toh(eh->eh_ecount) <= le16toh(eh->eh_max) &&
	    le16toh(eh->eh_ecount) > 0,
	    ("ext4_ext_binsearch_index: bad args"));

	l = EXT_FIRST_INDEX(eh) + 1;
	r = EXT_FIRST_INDEX(eh) + le16toh(eh->eh_ecount) - 1;
	while (l <= r) {
		m = l + (r - l) / 2;
		if (blk < le32toh(m->ei_blk))
			r = m - 1;
		else
			l = m + 1;
	}

	path->ep_index = l - 1;
}

static void
ext4_ext_binsearch_ext(struct ext4_extent_path *path, int blk)
{
	struct ext4_extent_header *eh;
	struct ext4_extent *r, *l, *m;

	eh = path->ep_header;

	KASSERT(le16toh(eh->eh_ecount) <= le16toh(eh->eh_max),
	    ("ext4_ext_binsearch_ext: bad args"));

	if (eh->eh_ecount == 0)
		return;

	l = EXT_FIRST_EXTENT(eh) + 1;
	r = EXT_FIRST_EXTENT(eh) + le16toh(eh->eh_ecount) - 1;

	while (l <= r) {
		m = l + (r - l) / 2;
		if (blk < le32toh(m->e_blk))
			r = m - 1;
		else
			l = m + 1;
	}

	path->ep_ext = l - 1;
}

static int
ext4_ext_fill_path_bdata(struct ext4_extent_path *path,
    struct buf *bp, uint64_t blk)
{
	KASSERT(path->ep_data == NULL,
	    ("ext4_ext_fill_path_bdata: bad ep_data"));

	path->ep_data = malloc(bp->b_bufsize, M_EXT2EXTENTS, M_WAITOK);
	memcpy(path->ep_data, bp->b_data, bp->b_bufsize);
	path->ep_blk = blk;

	return (0);
}

static void
ext4_ext_drop_refs(struct ext4_extent_path *path)
{
	int depth, i;

	if (!path)
		return;

	depth = path->ep_depth;
	for (i = 0; i <= depth; i++, path++)
		if (path->ep_data) {
			free(path->ep_data, M_EXT2EXTENTS);
			path->ep_data = NULL;
		}
}

void
ext4_ext_path_free(struct ext4_extent_path *path)
{
	if (!path)
		return;

	ext4_ext_drop_refs(path);
	free(path, M_EXT2EXTENTS);
}

int
ext4_ext_find_extent(struct inode *ip, daddr_t block,
    struct ext4_extent_path **ppath)
{
	struct ext4_extent_header *eh;
	struct ext4_extent_path *path;
	struct buf *bp;
	uint64_t blk;
	int error, depth, i, ppos, alloc;

	eh = ext4_ext_inode_header(ip);
	depth = ext4_ext_inode_depth(ip);
	ppos = 0;
	alloc = 0;

	error = ext4_ext_check_header(ip, eh, depth);
	if (error)
		return (error);

	if (ppath == NULL)
		return (EINVAL);

	path = *ppath;
	if (path == NULL) {
		path = malloc((EXT4_EXT_DEPTH_MAX + 1) *
		    sizeof(struct ext4_extent_path),
		    M_EXT2EXTENTS, M_WAITOK | M_ZERO);
		*ppath = path;
		alloc = 1;
	}

	path[0].ep_header = eh;
	path[0].ep_data = NULL;

	i = depth;
	while (i) {
		ext4_ext_binsearch_index(&path[ppos], block);
		blk = ext4_ext_index_pblock(path[ppos].ep_index);
		path[ppos].ep_depth = i;
		path[ppos].ep_ext = NULL;

		error = bread(ip->i_ump->um_devvp, fsbtodoff(ip->i_e2fs, blk),
		    ip->i_e2fs->e2fs_bsize, &bp);
		if (error) {
			brelse(bp);
			goto error;
		}

		ppos++;
		if (ppos > depth) {
			SDT_PROBE2(ext2fs, , trace, extents, 1,
			    "ppos > depth => extent corrupted");
			error = EIO;
			brelse(bp);
			goto error;
		}

		ext4_ext_fill_path_bdata(&path[ppos], bp, blk);
		bqrelse(bp);

		eh = ext4_ext_block_header(path[ppos].ep_data);
		if (ext4_ext_check_header(ip, eh, i - 1) ||
		    ext2_extent_blk_csum_verify(ip, path[ppos].ep_data)) {
			error = EIO;
			goto error;
		}

		path[ppos].ep_header = eh;

		i--;
	}

	error = ext4_ext_check_header(ip, eh, 0);
	if (error)
		goto error;

	path[ppos].ep_depth = i;
	path[ppos].ep_header = eh;
	path[ppos].ep_ext = NULL;
	path[ppos].ep_index = NULL;
	ext4_ext_binsearch_ext(&path[ppos], block);
	return (0);

error:
	ext4_ext_drop_refs(path);
	if (alloc)
		free(path, M_EXT2EXTENTS);

	*ppath = NULL;

	return (error);
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
