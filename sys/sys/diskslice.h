/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 */
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
 * $DragonFly: src/sys/sys/diskslice.h,v 1.22 2007/06/19 06:07:51 dillon Exp $
 */

#ifndef	_SYS_DISKSLICE_H_
#define	_SYS_DISKSLICE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_DISKLABEL_H_
#include <sys/disklabel.h>
#endif
#ifndef _SYS_UUID_H_
#include <sys/uuid.h>
#endif
#ifndef _SYS_IOCCOM_H_
#include <sys/ioccom.h>
#endif
#if defined(_KERNEL)
#ifndef _SYS_CONF_H_
#include <sys/conf.h>           /* for make_sub_dev() */
#endif
#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>		/* for minor() */
#endif
#endif

#define	BASE_SLICE		2	/* e.g. ad0s1 */
#define	COMPATIBILITY_SLICE	0	/* e.g. ad0a-j */
				/* 101 - compat disklabel DIOCGDINFO	*/
				/* 102 - compat disklabel DIOCSDINFO	*/
				/* 103 - compat disklabel DIOCWDINFO	*/
				/* 104 - DIOCGPART (see below)		*/
				/* 105 - compat disklabel DIOCGDVIRGIN	*/
#define DIOCWLABEL		_IOW('d', 109, int)
#define	DIOCGSLICEINFO		_IOR('d', 111, struct diskslices)
#define	DIOCSYNCSLICEINFO	_IOW('d', 112, int)
#define DIOCGKERNELDUMP		_IOW('d', 133, u_int)   /* Set/Clear kernel dumps */
#define	MAX_SLICES		16

/*
 * Support limits
 */
#define DKMAXUNITS	512	/* maximum supported disk units */
#define DKMAXSLICES	128	/* maximum supported slices (0 & 1 special) */
#define DKRESPARTITIONS	128	/* 128+ have special meanings */
#define DKMAXPARTITIONS	256	/* maximum supported in-kernel partitions */

/*
 * The whole-disk-slice does not try to interpret the MBR.  The whole slice
 * partition does not try to interpret the disklabel within the slice.
 */
#define	WHOLE_DISK_SLICE	1
#define WHOLE_SLICE_PART	(DKMAXPARTITIONS - 1)

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
 * ds_reserved	     -  indicates read-only sectors due to an overlap with
 *			a parent partition or an in-band label.  BSD labels
 *			are in-band labels.  This field is also set if
 *			label snooping has been requested, even if there is
 *			no label present.
 */
struct diskslice {
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
	cdev_t		ds_dev;
#else
	void		*ds_dev;
#endif
	u_int64_t	ds_offset;	/* starting sector */
	u_int64_t	ds_size;	/* number of sectors */
	u_int32_t	ds_reserved;	/* sectors reserved parent overlap */
	struct uuid	ds_type_uuid;	/* slice type uuid */
	struct uuid	ds_stor_uuid;	/* slice storage unique uuid */
	int		ds_type;	/* (foreign) slice type */
	int		ds_flags;	/* DSF_ flags */
	disklabel_t 	ds_label;	/* label, if any */
	struct disklabel_ops *ds_ops;	/* label ops (probe default) */
	//void		*ds_dev;	/* devfs token for raw whole slice */
	void		*ds_devs[MAXPARTITIONS]; /* XXX s.b. in label */
	u_int32_t	ds_openmask[DKMAXPARTITIONS/(sizeof(u_int32_t)*8)];
					/* devs open */
	u_char		ds_wlabel;	/* nonzero if label is writable */
	int		ds_ttlopens;	/* total opens, incl slice & raw */
};

#define DSF_REPROBE	0x0001		/* sniffer wants us to reprobe */

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
 * it is not a partition relative offset.  disklabel(32) uses this field
 * to figure out the slice offset so it fixup raw labels.
 *
 * NOTE: reserved_blocks indicates how many blocks at the beginning of the
 * partition are read-only due to in-band sharing with the parent.  For
 * example, if partition 'a' starts at block 0, it actually overlaps the
 * disklabel itself so numerous sectors at the beginning of 'a' will be
 * reserved.
 */
struct partinfo {
	u_int64_t	media_offset;	/* byte offset in parent layer */
	u_int64_t	media_size;	/* media size in bytes */
	u_int64_t	media_blocks;	/* media size in blocks */
	int		media_blksize;	/* block size in bytes (sector size) */

	u_int64_t	reserved_blocks;/* read-only, in sectors */
	int		fstype;		/* legacy filesystem type or FS_OTHER */
	char		fsreserved[16];	/* reserved for future use */

	/*
	 * These fields are loaded from the diskinfo structure
	 */
	u_int		d_nheads;
	u_int		d_ncylinders;
	u_int		d_secpertrack;
	u_int		d_secpercyl;
	u_int		d_reserved[8];	/* reserved for future use */

	/*
	 * UUIDs can be extracted from GPT slices and disklabel64
	 * partitions.  If not known, they will be set to a nil uuid.
	 *
	 * fstype_uuid represents the slice or partition type, e.g.
	 * like GPT_ENT_TYPE_DRAGONFLY_DISKLABEL32.  If not nil,
	 * storage_uuid uniquely identifies the physical storage.
	 */
	struct uuid	fstype_uuid;
	struct uuid	storage_uuid;
};

#define DIOCGPART	_IOR('d', 104, struct partinfo)	/* get partition */

/*
 * disk unit and slice helper functions
 *
 *     3                   2                   1                   0
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *  _________________________________________________________________
 *  | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 *  -----------------------------------------------------------------
 *  | SL2 | PART3 |UNIT_2 |P| SLICE |  MAJOR?       |  UNIT   |PART |
 *  -----------------------------------------------------------------
 */

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Build a minor device number.
 */
static __inline u_int32_t
dkmakeminor(u_int32_t unit, u_int32_t slice, u_int32_t part)
{
	u_int32_t val;

	val = ((unit & 0x001f) << 3) | ((unit & 0x01e0) << 16) |
	      ((slice & 0x000f) << 16) | ((slice & 0x0070) << 25) |
	      (part & 0x0007) | ((part & 0x0008) << 17) |
	      ((part & 0x00F0) << 21);
	return(val);
}

/*
 * Generate the minor number representing the entire disk, with no
 * mbr or label interpretation.
 */
static __inline u_int32_t
dkmakewholedisk(u_int32_t unit)
{
	return(dkmakeminor(unit, WHOLE_DISK_SLICE, WHOLE_SLICE_PART));
}

/*
 * Generate the minor number representing an entire slice, with no
 * recursive mbr, boot sector, or label interpretation.
 */
static __inline u_int32_t
dkmakewholeslice(u_int32_t unit, u_int32_t slice)
{
	return(dkmakeminor(unit, slice, WHOLE_SLICE_PART));
}

/*
 * Return the unit mask, used in calls to make_dev()
 */
static __inline u_int32_t
dkunitmask(void)
{
	return (0x01e000f8);
}

/*
 * build minor number elements - encode unit number, slice, and partition
 * (OR the results together).
 */
static __inline u_int32_t
dkmakeunit(int unit)
{
        return(dkmakeminor((u_int32_t)unit, 0, 0));
}

static __inline u_int32_t
dkmakeslice(int slice)
{
        return(dkmakeminor(0, (u_int32_t)slice, 0));
}

static __inline u_int32_t
dkmakepart(int part)
{
        return(dkmakeminor(0, 0, (u_int32_t)part));
}

#endif

/*
 * dk*() support functions operating on cdev_t's
 */
#ifdef _KERNEL

static __inline int
dkunit(cdev_t dev)
{
	u_int32_t val = minor(dev);

	val = ((val >> 3) & 0x001f) | ((val >> 16) & 0x01e0);
	return((int)val);
}

static __inline u_int32_t
dkslice(cdev_t dev)
{
	u_int32_t val = minor(dev);

	val = ((val >> 16) & 0x000f) | ((val >> 25) & 0x0070);
	return(val);
}

static __inline u_int32_t
dkpart(cdev_t dev)
{
	u_int32_t val = minor(dev);

	val = (val & 0x0007) | ((val >> 17) & 0x0008) | ((val >> 21) & 0x00f0);
	return(val);
}

#endif

/*
 * Bitmask ops, keeping track of which partitions are open.
 */
static __inline
void
dsclrmask(struct diskslice *ds, int part)
{
	part &= (DKMAXPARTITIONS - 1);
	ds->ds_openmask[part >> 5] &= ~(1 << (part & 31));
}

static __inline
void
dssetmask(struct diskslice *ds, int part)
{
	part &= (DKMAXPARTITIONS - 1);
	ds->ds_openmask[part >> 5] |= (1 << (part & 31));
}

static __inline
int
dschkmask(struct diskslice *ds, int part)
{
	part &= (DKMAXPARTITIONS - 1);
	return (ds->ds_openmask[part >> 5] & (1 << (part & 31)));
}

static __inline
int
dscountmask(struct diskslice *ds)
{
	int count = 0;
	int i;
	int j;

	for (i = 0; i < DKMAXPARTITIONS / 32; ++i) {
		if (ds->ds_openmask[i]) {
			for (j = 0; j < 32; ++j) {
				if (ds->ds_openmask[i] & (1 << j))
					++count;
			}
		}
	}
	return(count);
}

static __inline
void
dssetmaskfrommask(struct diskslice *ds, u_int32_t *tmask)
{
	int i;

	for (i = 0; i < DKMAXPARTITIONS / 32; ++i)
		tmask[i] |= ds->ds_openmask[i];
}

/*
 * disk management functions
 */

#ifdef _KERNEL

struct buf;
struct bio;
struct disk_info;
struct bio_queue_head;

int	mbrinit (cdev_t dev, struct disk_info *info,
		    struct diskslices **sspp);
int	gptinit (cdev_t dev, struct disk_info *info,
		    struct diskslices **sspp);
struct bio *
	dscheck (cdev_t dev, struct bio *bio, struct diskslices *ssp);
void	dsclose (cdev_t dev, int mode, struct diskslices *ssp);
void	dsgone (struct diskslices **sspp);
int	dsioctl (cdev_t dev, u_long cmd, caddr_t data, int flags,
		    struct diskslices **sspp, struct disk_info *info);
int	dsisopen (struct diskslices *ssp);
struct diskslices *
	dsmakeslicestruct (int nslices, struct disk_info *info);
char	*dsname (cdev_t dev, int unit, int slice, int part,
		    char *partname);
int	dsopen (cdev_t dev, int mode, u_int flags,
		    struct diskslices **sspp, struct disk_info *info);
int64_t	dssize (cdev_t dev, struct diskslices **sspp);

/*
 * Ancillary functions
 */

void	diskerr (struct bio *bio, cdev_t dev, const char *what, int pri,
		    int donecnt);
void	bioqdisksort (struct bio_queue_head *ap, struct bio *bio);

#endif /* _KERNEL */

#endif /* !_SYS_DISKSLICE_H_ */
