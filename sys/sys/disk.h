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
 * $DragonFly: src/sys/sys/disk.h,v 1.18 2007/07/30 08:02:40 dillon Exp $
 */

#ifndef _SYS_DISK_H_
#define	_SYS_DISK_H_

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

#ifndef _SYS_DISKSLICE_H_
#include <sys/diskslice.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif

/*
 * Media information structure - filled in by the media driver.
 *
 * NOTE: d_serialno is copied on the call to setdiskinfo and need
 *	 not be valid after that.
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
	char			*d_serialno;
};

/*
 * d_dsflags, also used for dsopen() - control disklabel processing
 *
 * COMPATPARTA	- used by scsi devices to allow CDs to boot from cd0a.
 *		  cd's don't have disklabels and the default compat label
 *		  does not implement an 'a' partition.
 *
 * COMPATMBR	- used by the vn device to request that one sector be
 *		  reserved as if an MBR were present even when one isn't.
 *
 * MBRQUIET	- silently ignore MBR probe if unable to read sector 0.
 *		  used by VN.
 *
 * DEVICEMAPPER	- used by the device mapper (dm). Adds a '.' between the
 *		  device name and the slice/part stuff (i.e. foo.s0).
 */
#define DSO_NOLABELS		0x0001
#define DSO_ONESLICE		0x0002
#define DSO_COMPATLABEL		0x0004
#define DSO_COMPATPARTA		0x0008
#define DSO_COMPATMBR		0x0010
#define DSO_RAWEXTENSIONS	0x0020
#define DSO_MBRQUIET		0x0040
#define DSO_DEVICEMAPPER	0x0080

/*
 * Disk management structure - automated disklabel support.
 */
struct disk {
	struct dev_ops		*d_dev_ops;	/* our device switch */
	struct dev_ops		*d_raw_ops;	/* the raw device switch */
	u_int			d_flags;
	int			d_opencount;	/* The current open count */
	cdev_t			d_rawdev;	/* backing raw device */
	cdev_t			d_cdev;		/* special whole-disk part */
	struct diskslices	*d_slice;
	struct disk_info	d_info;		/* info structure for media */
	void			*d_dsched_priv1;/* I/O scheduler priv. data */
	void			*d_dsched_priv2;/* I/O scheduler priv. data */
	struct dsched_policy	*d_sched_policy;/* I/O scheduler policy */
	const char		*d_disktype;	/* Disk type information */
	LIST_ENTRY(disk)	d_list;
};

/*
 * d_flags
 */
#define DISKFLAG_LOCK		0x1
#define DISKFLAG_WANTED		0x2

#ifdef _KERNEL
cdev_t disk_create (int unit, struct disk *disk, struct dev_ops *raw_ops);
cdev_t disk_create_clone (int unit, struct disk *disk, struct dev_ops *raw_ops);
cdev_t disk_create_named(const char *name, int unit, struct disk *dp, struct dev_ops *raw_ops);
cdev_t disk_create_named_clone(const char *name, int unit, struct disk *dp, struct dev_ops *raw_ops);
cdev_t disk_locate (const char *devname);
void disk_destroy (struct disk *disk);
void disk_setdiskinfo (struct disk *disk, struct disk_info *info);
int disk_setdisktype(struct disk *disk, const char *type);
int disk_getopencount(struct disk *disk);
void disk_setdiskinfo_sync(struct disk *disk, struct disk_info *info);
int disk_dumpcheck (cdev_t dev, u_int64_t *count, u_int64_t *blkno, u_int *secsize);
int disk_dumpconf(cdev_t dev, u_int onoff);
struct disk *disk_enumerate (struct disk *disk);
void disk_invalidate (struct disk *disk);
void disk_unprobe(struct disk *disk);

void disk_msg_send(uint32_t cmd, void *load, void *load2);
void disk_msg_send_sync(uint32_t cmd, void *load, void *load2);
void disk_config(void *);

int bounds_check_with_mediasize(struct bio *bio, int secsize, uint64_t mediasize);

typedef struct disk_msg {
	struct lwkt_msg hdr;
	void	*load;
	void	*load2;
} *disk_msg_t;

#define DISK_DISK_PROBE		0x01
#define DISK_DISK_DESTROY	0x02
#define DISK_SLICE_REPROBE	0x03
#define DISK_DISK_REPROBE	0x04
#define DISK_UNPROBE		0x05
#define DISK_SYNC			0x99


#endif /* _KERNEL */

#endif /* _SYS_DISK_H_ */
