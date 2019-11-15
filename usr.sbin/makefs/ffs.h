/*	$NetBSD: ffs.h,v 1.2 2004/12/20 20:51:42 jmc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001-2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: head/usr.sbin/makefs/ffs.h 326276 2017-11-27 15:37:16Z pfg $
 */

#ifndef _FFS_H
#define _FFS_H

#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>

typedef struct {
	char	label[MAXVOLLEN];	/* volume name/label */
	int	bsize;		/* block size */
	int	fsize;		/* fragment size */
	int	cpg;		/* cylinders per group */
	int	cpgflg;		/* cpg was specified by user */
	int	density;	/* bytes per inode */
	int	ntracks;	/* number of tracks */
	int	nsectors;	/* number of sectors */
	int	rpm;		/* rpm */
	int	minfree;	/* free space threshold */
	int	optimization;	/* optimization (space or time) */
	int	maxcontig;	/* max contiguous blocks to allocate */
	int	rotdelay;	/* rotational delay between blocks */
	int	maxbpg;		/* maximum blocks per file in a cyl group */
	int	nrpos;		/* # of distinguished rotational positions */
	int	avgfilesize;	/* expected average file size */
	int	avgfpdir;	/* expected # of files per directory */
	int	version;	/* filesystem version (1 = FFS, 2 = UFS2) */
#ifndef __DragonFly__
	int	maxbsize;	/* maximum extent size */
#endif
	int	maxblkspercg;	/* max # of blocks per cylinder group */
	int	softupdates;	/* soft updates */
		/* XXX: support `old' file systems ? */
} ffs_opt_t;

#ifdef __DragonFly__
#define	SBLOCK_UFS1		8192
#define	SBLOCKSIZE		SBSIZE
#define	FS_UFS1_MAGIC		FS_MAGIC

#define	csum_total		csum
#define	ufs1_daddr_t		ufs_daddr_t

#define	fs_old_cgmask		fs_cgmask
#define	fs_old_cgoffset		fs_cgoffset
#define	fs_old_cpc		fs_cpc
#define	fs_old_cpg		fs_cpg
#define	fs_old_flags		fs_flags
#define	fs_old_inodefmt		fs_inodefmt
#define	fs_old_interleave	fs_interleave
#define	fs_old_ncyl		fs_ncyl
#define	fs_old_npsect		fs_npsect
#define	fs_old_nrpos		fs_nrpos
#define	fs_old_nsect		fs_nsect
#define	fs_old_nspf		fs_nspf
#define	fs_old_postblformat	fs_postblformat
#define	fs_old_postbloff	fs_postbloff
#define	fs_old_rotbloff		fs_rotbloff
#define	fs_old_rotdelay		fs_rotdelay
#define	fs_old_rps		fs_rps
#define	fs_old_spc		fs_spc
#define	fs_old_trackskew	fs_trackskew

#define	cg_old_boff		cg_boff
#define	cg_old_btotoff		cg_btotoff
#define	cg_old_ncyl		cg_ncyl
#define	cg_old_niblk		cg_niblk
#define	cg_old_time		cg_time

typedef	__int64_t	makefs_daddr_t;	/* XXX swildner: ours is 32 bits?! */

/*
 * The size of a cylinder group is calculated by CGSIZE. The maximum size
 * is limited by the fact that cylinder groups are at most one block.
 * Its size is derived from the size of the maps maintained in the
 * cylinder group and the (struct cg) size.
 */

/*
 * XXX swildner: go with FreeBSD's CGSIZE macro until I've figured out
 *               what's wrong with ours
 */
#define	FBSD_CGSIZE(fs) \
    /* base cg */	(sizeof(struct cg) + sizeof(int32_t) + \
    /* old btotoff */	(fs)->fs_cpg * sizeof(int32_t) + \
    /* old boff */	(fs)->fs_cpg * sizeof(u_int16_t) + \
    /* inode map */	howmany((fs)->fs_ipg, NBBY) + \
    /* block map */	howmany((fs)->fs_fpg, NBBY) +\
    /* if present */	((fs)->fs_contigsumsize <= 0 ? 0 : \
    /* cluster sum */	(fs)->fs_contigsumsize * sizeof(int32_t) + \
    /* cluster map */	howmany(fragstoblks(fs, (fs)->fs_fpg), NBBY)))
#endif

#endif /* _FFS_H */
