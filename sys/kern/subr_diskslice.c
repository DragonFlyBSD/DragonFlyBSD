/*-
 * Copyright (c) 1994 Bruce D. Evans.
 * All rights reserved.
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)wd.c	7.2 (Berkeley) 5/9/91
 *	from: wd.c,v 1.55 1994/10/22 01:57:12 phk Exp $
 *	from: @(#)ufs_disksubr.c	7.16 (Berkeley) 5/4/91
 *	from: ufs_disksubr.c,v 1.8 1994/06/07 01:21:39 phk Exp $
 * $FreeBSD: src/sys/kern/subr_diskslice.c,v 1.82.2.6 2001/07/24 09:49:41 dd Exp $
 * $DragonFly: src/sys/kern/subr_diskslice.c,v 1.40 2007/05/20 19:23:33 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/diskmbr.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/thread2.h>

#include <vfs/ufs/dinode.h>	/* XXX used only for fs.h */
#include <vfs/ufs/fs.h>		/* XXX used only to get BBSIZE/SBSIZE */

#define TRACE(str)	do { if (ds_debug) kprintf str; } while (0)

typedef	u_char	bool_t;

static volatile bool_t ds_debug;

static struct disklabel *clone_label (struct disk_info *info,
					struct diskslice *sp);
static void dsiodone (struct bio *bio);
static char *fixlabel (const char *sname, struct diskslice *sp,
			   struct disklabel *lp, int writeflag);
static int  dsreadandsetlabel(cdev_t dev, u_int flags,
			   struct diskslices *ssp, struct diskslice *sp,
			   struct disk_info *info);
static void free_ds_label (struct diskslices *ssp, int slice);
static void partition_info (const char *sname, int part, struct partition *pp);
static void slice_info (const char *sname, struct diskslice *sp);
static void set_ds_label (struct diskslices *ssp, int slice,
			      struct disklabel *lp);
static void set_ds_wlabel (struct diskslices *ssp, int slice, int wlabel);

/*
 * Create a disklabel based on a disk_info structure, initializing
 * the appropriate fields and creating a raw partition that covers the
 * whole disk.
 *
 * If a diskslice is passed, the label is truncated to the slice
 */
static struct disklabel *
clone_label(struct disk_info *info, struct diskslice *sp)
{
	struct disklabel *lp1;

	lp1 = kmalloc(sizeof *lp1, M_DEVBUF, M_WAITOK | M_ZERO);
	lp1->d_nsectors = info->d_secpertrack;
	lp1->d_ntracks = info->d_nheads;
	lp1->d_secpercyl = info->d_secpercyl;
	lp1->d_secsize = info->d_media_blksize;

	if (sp) {
		lp1->d_secperunit = (u_int)sp->ds_size;
		lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	} else {
		lp1->d_secperunit = (u_int)info->d_media_blocks;
		lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	}

	/*
	 * Used by the CD driver to create a compatibility slice which
	 * allows us to mount root from the CD.
	 */
	if (info->d_dsflags & DSO_COMPATPARTA) {
		lp1->d_partitions[0].p_size = lp1->d_secperunit;
		lp1->d_partitions[0].p_fstype = FS_OTHER;
	}

	if (lp1->d_typename[0] == '\0')
		strncpy(lp1->d_typename, "amnesiac", sizeof(lp1->d_typename));
	if (lp1->d_packname[0] == '\0')
		strncpy(lp1->d_packname, "fictitious", sizeof(lp1->d_packname));
	if (lp1->d_nsectors == 0)
		lp1->d_nsectors = 32;
	if (lp1->d_ntracks == 0)
		lp1->d_ntracks = 64;
	lp1->d_secpercyl = lp1->d_nsectors * lp1->d_ntracks;
	lp1->d_ncylinders = lp1->d_secperunit / lp1->d_secpercyl;
	if (lp1->d_rpm == 0)
		lp1->d_rpm = 3600;
	if (lp1->d_interleave == 0)
		lp1->d_interleave = 1;
	if (lp1->d_npartitions < RAW_PART + 1)
		lp1->d_npartitions = MAXPARTITIONS;
	if (lp1->d_bbsize == 0)
		lp1->d_bbsize = BBSIZE;
	if (lp1->d_sbsize == 0)
		lp1->d_sbsize = SBSIZE;
	lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	lp1->d_magic = DISKMAGIC;
	lp1->d_magic2 = DISKMAGIC;
	lp1->d_checksum = dkcksum(lp1);
	return (lp1);
}

/*
 * Determine the size of the transfer, and make sure it is
 * within the boundaries of the partition. Adjust transfer
 * if needed, and signal errors or early completion.
 *
 * XXX TODO:
 *	o Split buffers that are too big for the device.
 *	o Check for overflow.
 *	o Finish cleaning this up.
 *
 * This function returns 1 on success, 0 if transfer equates
 * to EOF (end of disk) or -1 on failure.  The appropriate 
 * 'errno' value is also set in bp->b_error and bp->b_flags
 * is marked with B_ERROR.
 */
struct bio *
dscheck(cdev_t dev, struct bio *bio, struct diskslices *ssp)
{
	struct buf *bp = bio->bio_buf;
	struct bio *nbio;
	struct disklabel *lp;
	char *msg;
	long nsec;
	u_int64_t secno;
	u_int64_t endsecno;
	u_int64_t labelsect;
	u_int64_t slicerel_secno;
	struct diskslice *sp;
	u_int32_t part;
	u_int32_t slice;
	int shift;
	int mask;

	slice = dkslice(dev);
	part  = dkpart(dev);

	if (bio->bio_offset < 0) {
		kprintf("dscheck(%s): negative bio_offset %lld\n", 
			devtoname(dev), bio->bio_offset);
		goto bad;
	}
	if (slice >= ssp->dss_nslices) {
		kprintf("dscheck(%s): slice too large %d/%d\n",
			devtoname(dev), slice, ssp->dss_nslices);
		goto bad;
	}
	sp = &ssp->dss_slices[slice];

	/*
	 * Calculate secno and nsec
	 */
	if (ssp->dss_secmult == 1) {
		shift = DEV_BSHIFT;
		goto doshift;
	} else if (ssp->dss_secshift != -1) {
		shift = DEV_BSHIFT + ssp->dss_secshift;
doshift:
		mask = (1 << shift) - 1;
		if ((int)bp->b_bcount & mask)
			goto bad_bcount;
		if ((int)bio->bio_offset & mask)
			goto bad_blkno;
		secno = bio->bio_offset >> shift;
		nsec = bp->b_bcount >> shift;
	} else {
		if (bp->b_bcount % ssp->dss_secsize)
			goto bad_bcount;
		if (bio->bio_offset % ssp->dss_secsize)
			goto bad_blkno;
		secno = bio->bio_offset / ssp->dss_secsize;
		nsec = bp->b_bcount / ssp->dss_secsize;
	}

	/*
	 * Calculate slice-relative sector number end slice-relative
	 * limit.
	 */
	if (slice == WHOLE_DISK_SLICE) {
		/*
		 * Labels have not been allowed on whole-disks for a while.
		 * This really puts the nail in the coffin... no disk
		 * snooping will occur even if you tried to write a label
		 * without a slice structure.
		 *
		 * Accesses to the WHOLE_DISK_SLICE do not use a disklabel
		 * and partition numbers are special-cased.  Currently numbers
		 * less then 128 are not allowed.  Partition numbers >= 128
		 * are encoded in the high 8 bits of the 64 bit buffer offset
		 * and are fed directly through to the device with no
		 * further interpretation.  In particular, no sector
		 * translation interpretation should occur because the
		 * sector size for the special raw access may not be the
		 * same as the nominal sector size for the device.
		 */
		lp = NULL;
		if (part < 128) {
			kprintf("dscheck(%s): illegal partition number (%d) "
				"for WHOLE_DISK_SLICE access\n",
				devtoname(dev), part);
			goto bad;
		} else if (part != WHOLE_SLICE_PART) {
			nbio = push_bio(bio);
			nbio->bio_offset = bio->bio_offset |
					   (u_int64_t)part << 56;
			return(nbio);
		}

		/*
		 * sp->ds_size is for the whole disk in the WHOLE_DISK_SLICE.
		 */
		labelsect = 0;	/* ignore any reserved sectors, do not sniff */
		endsecno = sp->ds_size;
		slicerel_secno = secno;
	} else if (part == WHOLE_SLICE_PART) {
		/* 
		 * We are accessing a slice.  Snoop the label and check
		 * reserved blocks only if a label is present, otherwise
		 * do not.  A label may be present if (1) there are active
		 * opens on the disk (not necessarily this slice) or
		 * (2) the disklabel program has written an in-core label
		 * and now wants to write it out, or (3) the management layer
		 * is trying to write out an in-core layer.  In case (2) and
		 * (3) we MUST snoop the write or the on-disk version of the
		 * disklabel will not be properly translated.
		 *
		 * NOTE! opens on a whole-slice partition will not attempt
		 * to read a disklabel in.
		 */
		if ((lp = sp->ds_label) != NULL) {
			labelsect = sp->ds_skip_bsdlabel;
		} else {
			labelsect = 0;
		}
		endsecno = sp->ds_size;
		slicerel_secno = secno;
	} else if ((lp = sp->ds_label) && part < lp->d_npartitions) {
		/*
		 * Acesss through disklabel, partition present.
		 */
		struct partition *pp;

		labelsect = sp->ds_skip_bsdlabel;
		pp = &lp->d_partitions[dkpart(dev)];
		endsecno = pp->p_size;
		slicerel_secno = pp->p_offset + secno;
	} else if (lp) {
		/*
		 * Partition out of bounds
		 */
		kprintf("dscheck(%s): partition out of bounds %d/%d\n",
			devtoname(dev),
			part, lp->d_npartitions);
		goto bad;
	} else {
		/*
		 * Attempt to access partition when no disklabel present
		 */
		kprintf("dscheck(%s): attempt to access non-existant partition\n",
			devtoname(dev));
		goto bad;
	}

	/*
	 * labelsect will reflect the extent of any reserved blocks from
	 * the beginning of the slice.  We only check the slice reserved
	 * fields (sp->ds_skip_platform and sp->ds_skip_bsdlabel) if
	 * labelsect is non-zero, otherwise we ignore them.  When labelsect
	 * is non-zero, sp->ds_skip_platform indicates the sector where the
	 * disklabel begins.
	 *
	 * First determine if an attempt is being made to write to a
	 * reserved area when such writes are not allowed.
	 */
#if 0
	if (slicerel_secno < 16 && nsec &&
	    bp->b_cmd != BUF_CMD_READ) {
		kprintf("Attempt to write to reserved sector %lld labelsect %lld label %p/%p skip_plat %d skip_bsd %d WLABEL %d\n",
			slicerel_secno,
			labelsect,
			sp->ds_label, lp,
			sp->ds_skip_platform,
			sp->ds_skip_bsdlabel,
			sp->ds_wlabel);
	}
#endif
	if (slicerel_secno < labelsect && nsec &&
	    bp->b_cmd != BUF_CMD_READ && sp->ds_wlabel == 0) {
		bp->b_error = EROFS;
		goto error;
	}

	/*
	 * If we get here, bio_offset must be on a block boundary and
	 * the sector size must be a power of 2.
	 */
	if ((bio->bio_offset & (ssp->dss_secsize - 1)) ||
	    (ssp->dss_secsize ^ (ssp->dss_secsize - 1)) !=
	    ((ssp->dss_secsize << 1) - 1)) {
		kprintf("%s: invalid BIO offset, not sector aligned or"
			" invalid sector size (not power of 2) %08llx %d\n",
			devtoname(dev), bio->bio_offset, ssp->dss_secsize);
		goto bad;
	}

	/*
	 * EOF handling
	 */
	if (secno + nsec > endsecno) {
		/*
		 * Return an error if beyond the end of the disk, or
		 * if B_BNOCLIP is set.  Tell the system that we do not
		 * need to keep the buffer around.
		 */
		if (secno > endsecno || (bp->b_flags & B_BNOCLIP))
			goto bad;

		/*
		 * If exactly at end of disk, return an EOF.  Throw away
		 * the buffer contents, if any, by setting B_INVAL.
		 */
		if (secno == endsecno) {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_INVAL;
			goto done;
		}

		/*
		 * Else truncate
		 */
		nsec = endsecno - secno;
		bp->b_bcount = nsec * ssp->dss_secsize;
	}

	nbio = push_bio(bio);
	nbio->bio_offset = (off_t)(sp->ds_offset + slicerel_secno) * 
			   ssp->dss_secsize;

	/*
	 * Snoop writes to the label area when labelsect is non-zero.
	 * The label sector starts at sector sp->ds_skip_platform within
	 * the slice and ends before sector sp->ds_skip_bsdlabel.  The
	 * write must contain the label sector for us to be able to snoop it.
	 *
	 * We have to adjust the label's fields to the on-disk format on
	 * a write and then adjust them back on completion of the write,
	 * or on a read.
	 *
	 * SNOOPs are required for disklabel -r and the DIOC* ioctls also
	 * depend on it on the backend for label operations.  XXX
	 *
	 * NOTE! ds_skip_platform is usually set to non-zero by the slice
	 * scanning code, indicating that the slice has reserved boot
	 * sector(s).  It is also set for compatibility reasons via 
	 * the DSO_COMPATMBR flag.  But it is not a requirement and it
	 * can be 0, indicating that the disklabel (if present) is stored
	 * at the beginning of the slice.  In most cases ds_skip_platform
	 * will be '1'.
	 *
	 * ds_skip_bsdlabel is inclusive of ds_skip_platform.  If they are
	 * the same then there is no label present, even if non-zero.
	 */
	if (slicerel_secno < labelsect &&	/* also checks labelsect!=0 */
	    sp->ds_skip_platform < labelsect && /* degenerate case */
	    slicerel_secno <= sp->ds_skip_platform &&
	    slicerel_secno + nsec > sp->ds_skip_platform) {
		/* 
		 * Set up our own callback on I/O completion to handle
		 * undoing the fixup we did for the write as well as
		 * doing the fixup for a read.
		 */
		nbio->bio_done = dsiodone;
		nbio->bio_caller_info1.ptr = sp;
		nbio->bio_caller_info2.offset = 
		    (sp->ds_skip_platform - slicerel_secno) * ssp->dss_secsize;
		if (bp->b_cmd != BUF_CMD_READ) {
			msg = fixlabel(
				NULL, sp,
			       (struct disklabel *)
			       (bp->b_data + (int)nbio->bio_caller_info2.offset),
			       TRUE);
			if (msg != NULL) {
				kprintf("dscheck(%s): %s\n", 
				    devtoname(dev), msg);
				bp->b_error = EROFS;
				pop_bio(nbio);
				goto error;
			}
		}
	}
	return (nbio);

bad_bcount:
	kprintf(
	"dscheck(%s): b_bcount %d is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), bp->b_bcount, ssp->dss_secsize);
	goto bad;

bad_blkno:
	kprintf(
	"dscheck(%s): bio_offset %lld is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), bio->bio_offset, ssp->dss_secsize);
bad:
	bp->b_error = EINVAL;
	/* fall through */
error:
	/*
	 * Terminate the I/O with a ranging error.  Since the buffer is
	 * either illegal or beyond the file EOF, mark it B_INVAL as well.
	 */
	bp->b_resid = bp->b_bcount;
	bp->b_flags |= B_ERROR | B_INVAL;
done:
	/*
	 * Caller must biodone() the originally passed bio if NULL is
	 * returned.
	 */
	return (NULL);
}

void
dsclose(cdev_t dev, int mode, struct diskslices *ssp)
{
	u_int32_t part;
	u_int32_t slice;
	struct diskslice *sp;

	slice = dkslice(dev);
	part  = dkpart(dev);
	if (slice < ssp->dss_nslices) {
		sp = &ssp->dss_slices[slice];
		if (part < sizeof(sp->ds_openmask) * 8)
			sp->ds_openmask &= ~(1 << part);
	}
}

void
dsgone(struct diskslices **sspp)
{
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;

	for (slice = 0, ssp = *sspp; slice < ssp->dss_nslices; slice++) {
		sp = &ssp->dss_slices[slice];
		free_ds_label(ssp, slice);
	}
	kfree(ssp, M_DEVBUF);
	*sspp = NULL;
}

/*
 * For the "write" commands (DIOCSDINFO and DIOCWDINFO), this
 * is subject to the same restriction as dsopen().
 */
int
dsioctl(cdev_t dev, u_long cmd, caddr_t data, int flags,
	struct diskslices **sspp, struct disk_info *info)
{
	int error;
	struct disklabel *lp;
	int old_wlabel;
	u_char openmask;
	int part;
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;
	struct partition *pp;

	slice = dkslice(dev);
	part = dkpart(dev);
	ssp = *sspp;
	if (slice >= ssp->dss_nslices)
		return (EINVAL);
	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	switch (cmd) {

	case DIOCGDVIRGIN:
		/*
		 * You can only retrieve a virgin disklabel on the whole
		 * disk slice or whole-slice partition.
		 */
		if (slice != WHOLE_DISK_SLICE &&
		    part != WHOLE_SLICE_PART) {
			return(EINVAL);
		}

		lp = (struct disklabel *)data;
		if (ssp->dss_slices[WHOLE_DISK_SLICE].ds_label) {
			*lp = *ssp->dss_slices[WHOLE_DISK_SLICE].ds_label;
		} else {
			bzero(lp, sizeof(struct disklabel));
		}

		lp->d_magic = DISKMAGIC;
		lp->d_magic2 = DISKMAGIC;
		pp = &lp->d_partitions[RAW_PART];
		pp->p_offset = 0;
		pp->p_size = sp->ds_size;

		lp->d_npartitions = MAXPARTITIONS;
		if (lp->d_interleave == 0)
			lp->d_interleave = 1;
		if (lp->d_rpm == 0)
			lp->d_rpm = 3600;
		if (lp->d_nsectors == 0)
			lp->d_nsectors = 32;
		if (lp->d_ntracks == 0)
			lp->d_ntracks = 64;

		lp->d_bbsize = BBSIZE;
		lp->d_sbsize = SBSIZE;
		lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
		lp->d_ncylinders = sp->ds_size / lp->d_secpercyl;
		lp->d_secperunit = sp->ds_size;
		lp->d_checksum = 0;
		lp->d_checksum = dkcksum(lp);
		return (0);

	case DIOCGDINFO:
		/*
		 * You can only retrieve a disklabel on the whole
		 * slice partition.
		 *
		 * We do not support labels directly on whole-disks
		 * any more (that is, disks without slices), unless the
		 * device driver has asked for a compatible label (e.g.
		 * for a CD) to allow booting off of storage that is
		 * otherwise unlabeled.
		 */
		error = 0;
		if (part != WHOLE_SLICE_PART)
			return(EINVAL);
		if (slice == WHOLE_DISK_SLICE &&
		    (info->d_dsflags & DSO_COMPATLABEL) == 0) {
			return (ENODEV);
		}
		if (sp->ds_label == NULL) {
			error = dsreadandsetlabel(dev, info->d_dsflags,
						  ssp, sp, info);
		}
		if (error == 0)
			*(struct disklabel *)data = *sp->ds_label;
		return (error);

	case DIOCGPART:
		{
			struct partinfo *dpart = (void *)data;

			/*
			 * If accessing a whole-slice partition the disk
			 * management layer may not have tried to read the
			 * disklabel.  We have to try to read the label
			 * in order to properly initialize the ds_skip_*
			 * fields.
			 *
			 * We ignore any error.
			 */
			if (sp->ds_label == NULL && part == WHOLE_SLICE_PART &&
			    slice != WHOLE_DISK_SLICE) {
				dsreadandsetlabel(dev, info->d_dsflags,
						  ssp, sp, info);
			}

			bzero(dpart, sizeof(*dpart));
			dpart->media_offset   = (u_int64_t)sp->ds_offset *
						info->d_media_blksize;
			dpart->media_size     = (u_int64_t)sp->ds_size *
						info->d_media_blksize;
			dpart->media_blocks   = sp->ds_size;
			dpart->media_blksize  = info->d_media_blksize;
			dpart->skip_platform = sp->ds_skip_platform;
			dpart->skip_bsdlabel = sp->ds_skip_bsdlabel;

			if (slice != WHOLE_DISK_SLICE &&
			    part != WHOLE_SLICE_PART) {
				struct partition *p;

				if (lp == NULL || part >= lp->d_npartitions)
					return(EINVAL);

				p = &lp->d_partitions[part];
				dpart->fstype = p->p_fstype;
				dpart->media_offset += (u_int64_t)p->p_offset *
						       info->d_media_blksize;
				dpart->media_size = (u_int64_t)p->p_size *
						    info->d_media_blksize;
				dpart->media_blocks = (u_int64_t)p->p_size;

				/*
				 * partition starting sector (p_offset)
				 * requires slice's reserved areas to be
				 * adjusted.
				 */
				if (dpart->skip_platform > p->p_offset)
					dpart->skip_platform -= p->p_offset;
				else
					dpart->skip_platform = 0;
				if (dpart->skip_bsdlabel > p->p_offset)
					dpart->skip_bsdlabel -= p->p_offset;
				else
					dpart->skip_bsdlabel = 0;
			}

			/*
			 * Load remaining fields from the info structure
			 */
			dpart->d_nheads =	info->d_nheads;
			dpart->d_ncylinders =	info->d_ncylinders;
			dpart->d_secpertrack =	info->d_secpertrack;
			dpart->d_secpercyl =	info->d_secpercyl;
		}
		return (0);

	case DIOCGSLICEINFO:
		bcopy(ssp, data, (char *)&ssp->dss_slices[ssp->dss_nslices] -
				 (char *)ssp);
		return (0);

	case DIOCSDINFO:
		/*
		 * You can write a disklabel on the whole disk slice or
		 * whole-slice partition.
		 */
		if (slice != WHOLE_DISK_SLICE &&
		    part != WHOLE_SLICE_PART) {
			return(EINVAL);
		}

		/*
		 * We no longer support writing disklabels directly to media
		 * without there being a slice.  Keep this as a separate
		 * conditional.
		 */
		if (slice == WHOLE_DISK_SLICE)
			return (ENODEV);

		if (!(flags & FWRITE))
			return (EBADF);
		lp = kmalloc(sizeof *lp, M_DEVBUF, M_WAITOK);
		if (sp->ds_label == NULL)
			bzero(lp, sizeof *lp);
		else
			bcopy(sp->ds_label, lp, sizeof *lp);
		if (sp->ds_label == NULL) {
			openmask = 0;
		} else {
			openmask = sp->ds_openmask;
			if (slice == COMPATIBILITY_SLICE) {
				openmask |= ssp->dss_slices[
				    ssp->dss_first_bsd_slice].ds_openmask;
			} else if (slice == ssp->dss_first_bsd_slice) {
				openmask |= ssp->dss_slices[
				    COMPATIBILITY_SLICE].ds_openmask;
			}
		}
		error = setdisklabel(lp, (struct disklabel *)data,
				     (u_long)openmask);
		/* XXX why doesn't setdisklabel() check this? */
		if (error == 0 && lp->d_partitions[RAW_PART].p_offset != 0)
			error = EXDEV;
		if (error == 0) {
			if (lp->d_secperunit > sp->ds_size)
				error = ENOSPC;
			for (part = 0; part < lp->d_npartitions; part++)
				if (lp->d_partitions[part].p_size > sp->ds_size)
					error = ENOSPC;
		}
		if (error != 0) {
			kfree(lp, M_DEVBUF);
			return (error);
		}
		free_ds_label(ssp, slice);
		set_ds_label(ssp, slice, lp);
		return (0);

	case DIOCSYNCSLICEINFO:
		/*
		 * This ioctl can only be done on the whole disk
		 */
		if (slice != WHOLE_DISK_SLICE || part != WHOLE_SLICE_PART)
			return (EINVAL);

		if (*(int *)data == 0) {
			for (slice = 0; slice < ssp->dss_nslices; slice++) {
				openmask = ssp->dss_slices[slice].ds_openmask;
				if (openmask &&
				    (slice != WHOLE_DISK_SLICE || 
				     openmask & ~(1 << RAW_PART))) {
					return (EBUSY);
				}
			}
		}

		/*
		 * Temporarily forget the current slices struct and read
		 * the current one.
		 *
		 * NOTE:
		 *
		 * XXX should wait for current accesses on this disk to
		 * complete, then lock out future accesses and opens.
		 */
		*sspp = NULL;
		lp = kmalloc(sizeof *lp, M_DEVBUF, M_WAITOK);
		*lp = *ssp->dss_slices[WHOLE_DISK_SLICE].ds_label;
		error = dsopen(dev, S_IFCHR, ssp->dss_oflags, sspp, info);
		if (error != 0) {
			kfree(lp, M_DEVBUF);
			*sspp = ssp;
			return (error);
		}

		/*
		 * Reopen everything.  This is a no-op except in the "force"
		 * case and when the raw bdev and cdev are both open.  Abort
		 * if anything fails.
		 */
		for (slice = 0; slice < ssp->dss_nslices; slice++) {
			for (openmask = ssp->dss_slices[slice].ds_openmask,
			     part = 0; openmask; openmask >>= 1, part++) {
				if (!(openmask & 1))
					continue;
				error = dsopen(dkmodslice(dkmodpart(dev, part),
							  slice),
					       S_IFCHR, ssp->dss_oflags, sspp,
					       info);
				if (error != 0) {
					kfree(lp, M_DEVBUF);
					*sspp = ssp;
					return (EBUSY);
				}
			}
		}

		kfree(lp, M_DEVBUF);
		dsgone(&ssp);
		return (0);

	case DIOCWDINFO:
		error = dsioctl(dev, DIOCSDINFO, data, flags, &ssp, info);
		if (error != 0)
			return (error);
		/*
		 * XXX this used to hack on dk_openpart to fake opening
		 * partition 0 in case that is used instead of dkpart(dev).
		 */
		old_wlabel = sp->ds_wlabel;
		set_ds_wlabel(ssp, slice, TRUE);
		error = writedisklabel(dev, sp->ds_label);
		/* XXX should invalidate in-core label if write failed. */
		set_ds_wlabel(ssp, slice, old_wlabel);
		return (error);

	case DIOCWLABEL:
		if (slice == WHOLE_DISK_SLICE)
			return (ENODEV);
		if (!(flags & FWRITE))
			return (EBADF);
		set_ds_wlabel(ssp, slice, *(int *)data != 0);
		return (0);

	default:
		return (ENOIOCTL);
	}
}

/*
 * Chain the bio_done.  b_cmd remains valid through such chaining.
 */
static void
dsiodone(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	char *msg;

	if (bp->b_cmd != BUF_CMD_READ
	    || (!(bp->b_flags & B_ERROR) && bp->b_error == 0)) {
		msg = fixlabel(NULL, bio->bio_caller_info1.ptr,
			       (struct disklabel *)
			       (bp->b_data + (int)bio->bio_caller_info2.offset),
			       FALSE);
		if (msg != NULL)
			kprintf("%s\n", msg);
	}
	biodone(bio->bio_prev);
}

int
dsisopen(struct diskslices *ssp)
{
	int slice;

	if (ssp == NULL)
		return (0);
	for (slice = 0; slice < ssp->dss_nslices; slice++) {
		if (ssp->dss_slices[slice].ds_openmask)
			return (1);
	}
	return (0);
}

/*
 * Allocate a slices "struct" and initialize it to contain only an empty
 * compatibility slice (pointing to itself), a whole disk slice (covering
 * the disk as described by the label), and (nslices - BASE_SLICES) empty
 * slices beginning at BASE_SLICE.
 */
struct diskslices *
dsmakeslicestruct(int nslices, struct disk_info *info)
{
	struct diskslice *sp;
	struct diskslices *ssp;

	ssp = kmalloc(offsetof(struct diskslices, dss_slices) +
		     nslices * sizeof *sp, M_DEVBUF, M_WAITOK);
	ssp->dss_first_bsd_slice = COMPATIBILITY_SLICE;
	ssp->dss_nslices = nslices;
	ssp->dss_oflags = 0;

	/*
	 * Figure out if we can use shifts or whether we have to
	 * use mod/multply to translate byte offsets into sector numbers.
	 */
	if ((info->d_media_blksize ^ (info->d_media_blksize - 1)) ==
	     (info->d_media_blksize << 1) - 1) {
		ssp->dss_secmult = info->d_media_blksize / DEV_BSIZE;
		if (ssp->dss_secmult & (ssp->dss_secmult - 1))
			ssp->dss_secshift = -1;
		else
			ssp->dss_secshift = ffs(ssp->dss_secmult) - 1;
	} else {
		ssp->dss_secmult = 0;
		ssp->dss_secshift = -1;
	}
	ssp->dss_secsize = info->d_media_blksize;
	sp = &ssp->dss_slices[0];
	bzero(sp, nslices * sizeof *sp);
	sp[WHOLE_DISK_SLICE].ds_size = info->d_media_blocks;
	return (ssp);
}

char *
dsname(cdev_t dev, int unit, int slice, int part, char *partname)
{
	static char name[32];
	const char *dname;
	int used;

	dname = dev_dname(dev);
	if (strlen(dname) > 16)
		dname = "nametoolong";
	ksnprintf(name, sizeof(name), "%s%d", dname, unit);
	partname[0] = '\0';
	used = strlen(name);

	if (slice != WHOLE_DISK_SLICE) {
		/*
		 * slice or slice + partition.  BASE_SLICE is s1, but
		 * the compatibility slice (0) needs to be s0.
		 */
		used += ksnprintf(name + used, sizeof(name) - used,
				  "s%d", (slice ? slice - BASE_SLICE + 1 : 0));
		if (part != WHOLE_SLICE_PART) {
			used += ksnprintf(name + used, sizeof(name) - used,
					  "%c", 'a' + part);
			partname[0] = 'a' + part;
			partname[1] = 0;
		}
	} else if (part == WHOLE_SLICE_PART) {
		/*
		 * whole-disk-device, raw access to disk
		 */
		/* no string extension */
	} else if (part > 128) {
		/*
		 * whole-disk-device, extended raw access partitions.
		 * (typically used to access CD audio tracks)
		 */
		used += ksnprintf(name + used, sizeof(name) - used,
					  "t%d", part - 128);
	} else {
		/*
		 * whole-disk-device, illegal partition number
		 */
		used += ksnprintf(name + used, sizeof(name) - used,
					  "?%d", part);
	}
	return (name);
}

/*
 * This should only be called when the unit is inactive and the strategy
 * routine should not allow it to become active unless we call it.  Our
 * strategy routine must be special to allow activity.
 */
int
dsopen(cdev_t dev, int mode, u_int flags, 
	struct diskslices **sspp, struct disk_info *info)
{
	cdev_t dev1;
	int error;
	bool_t need_init;
	struct diskslice *sp;
	struct diskslices *ssp;
	int slice;
	int part;

	dev->si_bsize_phys = info->d_media_blksize;

	/*
	 * Do not attempt to read the slice table or disk label when
	 * accessing the whole-disk slice or a while-slice partition.
	 */
	if (dkslice(dev) == WHOLE_DISK_SLICE)
		flags |= DSO_ONESLICE | DSO_NOLABELS;
	if (dkpart(dev) == WHOLE_SLICE_PART)
		flags |= DSO_NOLABELS;

	/*
	 * Reinitialize the slice table unless there is an open device
	 * on the unit.
	 *
	 * It would be nice if we didn't have to do this but when a
	 * user is slicing and partitioning up a disk it is a lot safer
	 * to not take any chances.
	 */
	ssp = *sspp;
	need_init = !dsisopen(ssp);
	if (ssp != NULL && need_init)
		dsgone(sspp);
	if (need_init) {
		/*
		 * Allocate a minimal slices "struct".  This will become
		 * the final slices "struct" if we don't want real slices
		 * or if we can't find any real slices.
		 *
		 * Then scan the disk
		 */
		*sspp = dsmakeslicestruct(BASE_SLICE, info);

		if ((flags & DSO_ONESLICE) == 0) {
			TRACE(("mbrinit\n"));
			error = mbrinit(dev, info, sspp);
			if (error != 0) {
				dsgone(sspp);
				return (error);
			}
		}
		ssp = *sspp;
		ssp->dss_oflags = flags;

		/*
		 * If there are no real slices, then make the compatiblity
		 * slice cover the whole disk.
		 *
		 * no sectors are reserved for the platform (ds_skip_platform
		 * will be 0) in this case.  This means that if a disklabel
		 * is installed it will be directly installed in sector 0
		 * unless DSO_COMPATMBR is requested.
		 */
		if (ssp->dss_nslices == BASE_SLICE) {
			sp = &ssp->dss_slices[COMPATIBILITY_SLICE];

			sp->ds_size = info->d_media_blocks;
			if (info->d_dsflags & DSO_COMPATMBR) {
				sp->ds_skip_platform = 1;
				sp->ds_skip_bsdlabel = sp->ds_skip_platform;
			} else {
				sp->ds_skip_platform = 0;
				sp->ds_skip_bsdlabel = 0;
			}
		}

		/*
		 * Point the compatibility slice at the BSD slice, if any. 
		 */
		for (slice = BASE_SLICE; slice < ssp->dss_nslices; slice++) {
			sp = &ssp->dss_slices[slice];
			if (sp->ds_type == DOSPTYP_386BSD /* XXX */) {
				struct diskslice *csp;

				csp = &ssp->dss_slices[COMPATIBILITY_SLICE];
				ssp->dss_first_bsd_slice = slice;
				csp->ds_offset = sp->ds_offset;
				csp->ds_size = sp->ds_size;
				csp->ds_type = sp->ds_type;
				csp->ds_skip_platform = sp->ds_skip_platform;
				csp->ds_skip_bsdlabel = sp->ds_skip_bsdlabel;
				break;
			}
		}

		/*
		 * By definition accesses via the whole-disk device do not
		 * specify any reserved areas.  The whole disk may be read
		 * or written by the whole-disk device.
		 *
		 * ds_label for a whole-disk device is only used as a
		 * template.
		 */
		sp = &ssp->dss_slices[WHOLE_DISK_SLICE];
		sp->ds_label = clone_label(info, NULL);
		sp->ds_wlabel = TRUE;
		sp->ds_skip_platform = 0;
		sp->ds_skip_bsdlabel = 0;
	}

	/*
	 * Load the disklabel for the slice being accessed unless it is
	 * a whole-disk-slice or a whole-slice-partition (as determined
	 * by DSO_NOLABELS).
	 *
	 * We could scan all slices here and try to load up their
	 * disklabels, but that would cause us to access slices that
	 * the user may otherwise not intend us to access, or corrupted
	 * slices, etc.
	 *
	 * XXX if there are no opens on the slice we may want to re-read
	 * the disklabel anyway, even if we have one cached.
	 */
	slice = dkslice(dev);
	if (slice >= ssp->dss_nslices)
		return (ENXIO);
	sp = &ssp->dss_slices[slice];
	part = dkpart(dev);

	if ((flags & DSO_NOLABELS) == 0 && sp->ds_label == NULL) {
		dev1 = dkmodslice(dkmodpart(dev, WHOLE_SLICE_PART), slice);

		/*
		 * If opening a raw disk we do not try to
		 * read the disklabel now.  No interpretation of raw disks
		 * (e.g. like 'da0') ever occurs.  We will try to read the
		 * disklabel for a raw slice if asked to via DIOC* ioctls.
		 *
		 * Access to the label area is disallowed by default.  Note
		 * however that accesses via WHOLE_DISK_SLICE, and accesses
		 * via WHOLE_SLICE_PART for slices without valid disklabels,
		 * will allow writes and ignore the flag.
		 */
		set_ds_wlabel(ssp, slice, FALSE);
		dsreadandsetlabel(dev1, flags, ssp, sp, info);
	}

	/*
	 * If opening a particular partition the disklabel must exist and
	 * the partition must be present in the label.
	 *
	 * If the partition is the special whole-disk-slice no partition
	 * table need exist.
	 */
	if (part != WHOLE_SLICE_PART && slice != WHOLE_DISK_SLICE) {
		if (sp->ds_label == NULL || part >= sp->ds_label->d_npartitions)
			return (EINVAL);
		if (part < sizeof(sp->ds_openmask) * 8) {
			sp->ds_openmask |= 1 << part;
		}
	}

	/*
	 * Do not allow special raw-extension partitions to be opened
	 * if the device doesn't support them.  Raw-extension partitions
	 * are typically used to handle CD tracks.
	 */
	if (slice == WHOLE_DISK_SLICE && part >= 128 &&
	    part != WHOLE_SLICE_PART) {
		if ((info->d_dsflags & DSO_RAWEXTENSIONS) == 0)
			return (EINVAL);
	}
	return (0);
}

/*
 * Attempt to read the disklabel.  If successful, store it in sp->ds_label.
 *
 * If we cannot read the disklabel and DSO_COMPATLABEL is set, we construct
 * a fake label covering the whole disk.
 */
static
int
dsreadandsetlabel(cdev_t dev, u_int flags,
		  struct diskslices *ssp, struct diskslice *sp,
		  struct disk_info *info)
{
	struct disklabel *lp1;
	const char *msg;
	const char *sname;
	char partname[2];
	int slice = dkslice(dev);

	sname = dsname(dev, dkunit(dev), slice, WHOLE_SLICE_PART, partname);
	lp1 = clone_label(info, sp);
	msg = readdisklabel(dev, lp1);

	if (msg != NULL && (flags & DSO_COMPATLABEL)) {
		msg = NULL;
		kfree(lp1, M_DEVBUF);
		lp1 = clone_label(info, sp);
	}
	if (msg == NULL)
		msg = fixlabel(sname, sp, lp1, FALSE);
	if (msg == NULL && lp1->d_secsize != info->d_media_blksize)
		msg = "inconsistent sector size";
	if (msg != NULL) {
		if (sp->ds_type == DOSPTYP_386BSD /* XXX */)
			log(LOG_WARNING, "%s: cannot find label (%s)\n",
			    sname, msg);
		kfree(lp1, M_DEVBUF);
	} else {
		set_ds_label(ssp, slice, lp1);
		set_ds_wlabel(ssp, slice, FALSE);
	}
	return (msg ? EINVAL : 0);
}

int64_t
dssize(cdev_t dev, struct diskslices **sspp)
{
	struct disklabel *lp;
	int part;
	int slice;
	struct diskslices *ssp;

	slice = dkslice(dev);
	part = dkpart(dev);
	ssp = *sspp;
	if (ssp == NULL || slice >= ssp->dss_nslices
	    || !(ssp->dss_slices[slice].ds_openmask & (1 << part))) {
		if (dev_dopen(dev, FREAD, S_IFCHR, proc0.p_ucred) != 0)
			return (-1);
		dev_dclose(dev, FREAD, S_IFCHR);
		ssp = *sspp;
	}
	lp = ssp->dss_slices[slice].ds_label;
	if (lp == NULL)
		return (-1);
	return ((int64_t)lp->d_partitions[part].p_size);
}

static void
free_ds_label(struct diskslices *ssp, int slice)
{
	struct disklabel *lp;
	struct diskslice *sp;

	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	if (lp == NULL)
		return;
	kfree(lp, M_DEVBUF);
	set_ds_label(ssp, slice, (struct disklabel *)NULL);
}

static char *
fixlabel(const char *sname, struct diskslice *sp, struct disklabel *lp, int writeflag)
{
	u_int64_t start;
	u_int64_t end;
	u_int64_t offset;
	int part;
	struct partition *pp;
	bool_t warned;

	/* These errors "can't happen" so don't bother reporting details. */
	if (lp->d_magic != DISKMAGIC || lp->d_magic2 != DISKMAGIC)
		return ("fixlabel: invalid magic");
	if (dkcksum(lp) != 0)
		return ("fixlabel: invalid checksum");

	pp = &lp->d_partitions[RAW_PART];

	/*
	 * What a mess.  For ages old backwards compatibility the disklabel
	 * on-disk stores absolute offsets instead of slice-relative offsets.
	 * So fix it up when reading, writing, or snooping.
	 *
	 * The in-core label is always slice-relative.
	 */
	if (writeflag) {
		start = 0;
		offset = sp->ds_offset;
	} else {
		start = sp->ds_offset;
		offset = -sp->ds_offset;
	}
	if (pp->p_offset != start) {
		if (sname != NULL) {
			kprintf(
"%s: rejecting BSD label: raw partition offset != slice offset\n",
			       sname);
			slice_info(sname, sp);
			partition_info(sname, RAW_PART, pp);
		}
		return ("fixlabel: raw partition offset != slice offset");
	}
	if (pp->p_size != sp->ds_size) {
		if (sname != NULL) {
			kprintf("%s: raw partition size != slice size\n", sname);
			slice_info(sname, sp);
			partition_info(sname, RAW_PART, pp);
		}
		if (pp->p_size > sp->ds_size) {
			if (sname == NULL)
				return ("fixlabel: raw partition size > slice size");
			kprintf("%s: truncating raw partition\n", sname);
			pp->p_size = sp->ds_size;
		}
	}
	end = start + sp->ds_size;
	if (start > end)
		return ("fixlabel: slice wraps");
	if (lp->d_secpercyl <= 0)
		return ("fixlabel: d_secpercyl <= 0");
	pp -= RAW_PART;
	warned = FALSE;
	for (part = 0; part < lp->d_npartitions; part++, pp++) {
		if (pp->p_offset != 0 || pp->p_size != 0) {
			if (pp->p_offset < start
			    || pp->p_offset + pp->p_size > end
			    || pp->p_offset + pp->p_size < pp->p_offset) {
				if (sname != NULL) {
					kprintf(
"%s: rejecting partition in BSD label: it isn't entirely within the slice\n",
					       sname);
					if (!warned) {
						slice_info(sname, sp);
						warned = TRUE;
					}
					partition_info(sname, part, pp);
				}
				/* XXX else silently discard junk. */
				bzero(pp, sizeof *pp);
			} else {
				pp->p_offset += offset;
			}
		}
	}
	lp->d_ncylinders = sp->ds_size / lp->d_secpercyl;
	lp->d_secperunit = sp->ds_size;
 	lp->d_checksum = 0;
 	lp->d_checksum = dkcksum(lp);
	return (NULL);
}

static void
partition_info(const char *sname, int part, struct partition *pp)
{
	kprintf("%s%c: start %lu, end %lu, size %lu\n", sname, 'a' + part,
	       (u_long)pp->p_offset, (u_long)(pp->p_offset + pp->p_size - 1),
	       (u_long)pp->p_size);
}

static void
slice_info(const char *sname, struct diskslice *sp)
{
	kprintf("%s: start %llu, end %llu, size %llu\n", sname,
	       sp->ds_offset, sp->ds_offset + sp->ds_size - 1, sp->ds_size);
}

static void
set_ds_label(struct diskslices *ssp, int slice, struct disklabel *lp)
{
	struct diskslice *sp1 = &ssp->dss_slices[slice];
	struct diskslice *sp2;

	if (slice == COMPATIBILITY_SLICE)
		sp2 = &ssp->dss_slices[ssp->dss_first_bsd_slice];
	else if (slice == ssp->dss_first_bsd_slice)
		sp2 = &ssp->dss_slices[COMPATIBILITY_SLICE];
	else
		sp2 = NULL;
	sp1->ds_label = lp;
	if (sp2)
		sp2->ds_label = lp;

	/*
	 * If the slice is not the whole-disk slice, setup the reserved
	 * area(s).
	 *
	 * The reserved area for the original bsd disklabel, inclusive of
	 * the label and space for boot2, is 15 sectors.  If you've
	 * noticed people traditionally skipping 16 sectors its because
	 * the sector numbers start at the beginning of the slice rather
	 * then the beginning of the disklabel and traditional dos slices
	 * reserve a sector at the beginning for the boot code.
	 *
	 * NOTE! With the traditional bsdlabel, the first N bytes of boot2
	 * overlap with the disklabel.  The disklabel program checks that
	 * they are 0.
	 *
	 * When clearing a label, the bsdlabel reserved area is reset.
	 */
	if (slice != WHOLE_DISK_SLICE) {
		if (lp) {
			/*
			 * leave room for the disklabel and boot2 -
			 * traditional label only.  XXX bad hack.  Such
			 * labels cannot install a boot area due to
			 * insufficient space.
			 */
			int lsects = SBSIZE / ssp->dss_secsize - 
				     sp1->ds_skip_platform;
			if (lsects <= 0)
				lsects = 1;
			sp1->ds_skip_bsdlabel = sp1->ds_skip_platform + lsects;
			if (sp2)
				sp2->ds_skip_bsdlabel = sp1->ds_skip_bsdlabel;
		} else {
			sp1->ds_skip_bsdlabel = sp1->ds_skip_platform;
			if (sp2)
				sp2->ds_skip_bsdlabel = sp1->ds_skip_platform;
		}
	}
}

static void
set_ds_wlabel(struct diskslices *ssp, int slice, int wlabel)
{
	ssp->dss_slices[slice].ds_wlabel = wlabel;
	if (slice == COMPATIBILITY_SLICE)
		ssp->dss_slices[ssp->dss_first_bsd_slice].ds_wlabel = wlabel;
	else if (slice == ssp->dss_first_bsd_slice)
		ssp->dss_slices[COMPATIBILITY_SLICE].ds_wlabel = wlabel;
}
