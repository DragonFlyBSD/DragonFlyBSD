/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/sys/disk.h,v 1.16.2.3 2001/06/20 16:11:01 scottl Exp $
 * $DragonFly: src/sys/sys/disk.h,v 1.11 2007/05/15 00:01:04 dillon Exp $
 */

#ifndef _SYS_DISK_H_
#define	_SYS_DISK_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

#ifndef _SYS_DISKSLICE_H_
#include <sys/diskslice.h>
#endif

#ifndef _SYS_DISKLABEL_H_
#include <sys/disklabel.h>
#endif

/*
 * Media information structure - filled in by the media driver.
 */
struct disk_info {
	/*
	 * These fields are required.  Most drivers will load a disk_info
	 * structure in the device open function with the media parameters
	 * and call disk_setdiskinfo().
	 *
	 * Note that only one of d_media_size or d_media_blocks should be
	 * filled in.
	 *
	 * d_media_size		media size in bytes
	 * d_media_blocks	media size in blocks (e.g. total sectors)
	 * d_media_blksize	media block size / sector size
	 * d_dsflags		disklabel management flags
	 */
	u_int64_t		d_media_size;
	u_int64_t		d_media_blocks;
	int			d_media_blksize;
	u_int			d_dsflags;

	/*
	 * Optional fields, leave 0 if not known
	 */
	u_int			d_type;		/* DTYPE_xxx */
	u_int			d_nheads;
	u_int			d_ncylinders;
	u_int			d_secpertrack;
	u_int			d_secpercyl;
};

/*
 * d_dsflags, also used for dsopen() - control disklabel processing
 */
#define DSO_NOLABELS		0x0001
#define DSO_ONESLICE		0x0002
#define DSO_COMPATLABEL		0x0004
#define DSO_COMPATPARTA		0x0008

/*
 * Disk management structure - automated disklabel support.
 */
struct disk {
	struct dev_ops		*d_dev_ops;	/* our device switch */
	struct dev_ops		*d_raw_ops;	/* the raw device switch */
	u_int			d_flags;
	cdev_t			d_rawdev;	/* backing raw device */
	cdev_t			d_cdev;		/* special whole-disk part */
	struct diskslices	*d_slice;
	struct disk_info	d_info;		/* info structure for media */
	struct disklabel	d_label;
	LIST_ENTRY(disk)	d_list;
};

/*
 * d_flags
 */
#define DISKFLAG_LOCK		0x1
#define DISKFLAG_WANTED		0x2

#ifdef _KERNEL
cdev_t disk_create (int unit, struct disk *disk, struct dev_ops *raw_ops);
void disk_destroy (struct disk *disk);
void disk_setdiskinfo (struct disk *disk, struct disk_info *info);
int disk_dumpcheck (cdev_t dev, u_int *count, u_int *blkno, u_int *secsize);
struct disk *disk_enumerate (struct disk *disk);
void disk_invalidate (struct disk *disk);
#endif /* _KERNEL */

#endif /* _SYS_DISK_H_ */
