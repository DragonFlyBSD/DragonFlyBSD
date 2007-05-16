/*-
 * Copyright (c) 1994 Bruce D. Evans.
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
 * $FreeBSD: src/sys/sys/diskslice.h,v 1.36.2.1 2001/01/29 01:50:50 ken Exp $
 * $DragonFly: src/sys/sys/diskslice.h,v 1.12 2007/05/16 05:20:25 dillon Exp $
 */

#ifndef	_SYS_DISKSLICE_H_
#define	_SYS_DISKSLICE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_IOCCOM_H_
#include <sys/ioccom.h>
#endif

#define	BASE_SLICE		2	/* e.g. ad0s1 */
#define	COMPATIBILITY_SLICE	0	/* e.g. ad0a-j */
#define	DIOCGSLICEINFO		_IOR('d', 111, struct diskslices)
#define	DIOCSYNCSLICEINFO	_IOW('d', 112, int)
#define	MAX_SLICES		16
#define	WHOLE_DISK_SLICE	1	/* e.g. ad0 */

#ifdef MAXPARTITIONS			/* XXX don't depend on disklabel.h */
#if MAXPARTITIONS !=	16		/* but check consistency if possible */
#error "inconsistent MAXPARTITIONS"
#endif
#else
#define	MAXPARTITIONS	16
#endif

/*
 * diskslice structure - slices up the disk and indicates where the
 * BSD labels are, if any.
 *
 * ds_skip_platform  -	sectors reserved by the platform abstraction,
 *		      	typically to hold boot sectors and other junk.
 *		      	The BSD label is placed after the reserved sectors.
 *
 *			This field is typically non-zero for dos slices.
 *			It will always be 0 for the whole-disk slice.
 *			
 * ds_skip_bsdlabel  -	sectors reserved by the BSD label.  Always 0 when
 *			the disk is accessed via the whole-disk slice.
 *
 *			This field includes any sectors reserved by the
 *			platform. e.g. in a dos slice the platform uses
 *			1 sector (the boot code sector) and the disklabel
 *			uses 15 sectors.  This field will be set to 16.
 *			
 *			This field would end up being set to one less for
 *			a directly labeled disk, at least for a standard
 *			bsd disklabel vs MBR + bsd disklabel.
 */
struct diskslice {
	u_int64_t	ds_offset;	/* starting sector */
	u_int64_t	ds_size;	/* number of sectors */
	u_int32_t	ds_skip_platform;	/* in sectors */
	u_int32_t	ds_skip_bsdlabel;	/* in sectors */
	int		ds_type;	/* (foreign) slice type */
	struct disklabel *ds_label;	/* BSD label, if any */
	void		*ds_dev;	/* devfs token for raw whole slice */
	void		*ds_devs[MAXPARTITIONS]; /* XXX s.b. in label */
	u_char		ds_openmask;	/* devs open */
	u_char		ds_wlabel;	/* nonzero if label is writable */
};

struct diskslices {
	struct cdevsw *dss_cdevsw;	/* for containing device */
	int	dss_first_bsd_slice;	/* COMPATIBILITY_SLICE is mapped here */
	u_int	dss_nslices;		/* actual dimension of dss_slices[] */
	u_int	dss_oflags;		/* copy of flags for "first" open */
	int	dss_secmult;		/* block to sector multiplier */
	int	dss_secshift;		/* block to sector shift (or -1) */
	int	dss_secsize;		/* sector size */
	struct diskslice
		dss_slices[MAX_SLICES];	/* actually usually less */
};

/*
 * DIOCGPART ioctl - returns information about a disk, slice, or partition.
 * This ioctl is primarily used to get the block size and media size.
 *
 * NOTE: media_offset currently represents the byte offset on the raw device,
 * it is not a partition relative offset.
 *
 * skip_platform and skip_bsdlabel work as with the diskslice
 * structure.  For partitions within a disklabel these fields are usually
 * 0 except for partitions which overlap the label or slice reserved area
 * itself.  Those partitions will set these fields appropriately (relative
 * to the partition).  In particular, the 'a' and 'c' partitions are
 * protected.
 */
struct partinfo {
	u_int64_t	media_offset;	/* byte offset in parent layer */
	u_int64_t	media_size;	/* media size in bytes */
	u_int64_t	media_blocks;	/* media size in blocks */
	int		media_blksize;	/* block size in bytes (sector size) */

	u_int32_t	skip_platform;	/* in sectors */
	u_int32_t	skip_bsdlabel;	/* in sectors */
	int		fstype;		/* filesystem type if numeric */
	char		fstypestr[16];	/* filesystem type as ascii */
};

#define DIOCGPART	_IOR('d', 104, struct partinfo)	/* get partition */


#ifdef _KERNEL

#define	dsgetlabel(dev, ssp)	(ssp->dss_slices[dkslice(dev)].ds_label)

struct buf;
struct bio;
struct disklabel;
struct disk_info;

int	mbrinit (cdev_t dev, struct disk_info *info,
		 struct diskslices **sspp);
struct bio *dscheck (cdev_t dev, struct bio *bio, struct diskslices *ssp);
void	dsclose (cdev_t dev, int mode, struct diskslices *ssp);
void	dsgone (struct diskslices **sspp);
int	dsioctl (cdev_t dev, u_long cmd, caddr_t data, int flags,
		     struct diskslices **sspp, struct disk_info *info);
int	dsisopen (struct diskslices *ssp);
struct diskslices *dsmakeslicestruct (int nslices, struct disk_info *info);
char	*dsname (cdev_t dev, int unit, int slice, int part,
		     char *partname);
int	dsopen (cdev_t dev, int mode, u_int flags,
		    struct diskslices **sspp, struct disk_info *info);
int64_t	dssize (cdev_t dev, struct diskslices **sspp);

#endif /* _KERNEL */

#endif /* !_SYS_DISKSLICE_H_ */
