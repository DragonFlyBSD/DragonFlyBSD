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
 * $DragonFly: src/sys/kern/subr_diskslice.c,v 1.45 2007/06/17 23:50:16 dillon Exp $
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

static void dsiodone (struct bio *bio);
static int  dsreadandsetlabel(cdev_t dev, u_int flags,
			   struct diskslices *ssp, struct diskslice *sp,
			   struct disk_info *info);
static void free_ds_label (struct diskslices *ssp, int slice);
static void set_ds_label (struct diskslices *ssp, int slice,
			      struct disklabel *lp);
static void set_ds_wlabel (struct diskslices *ssp, int slice, int wlabel);

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
	u_int64_t slicerel_secno;
	struct diskslice *sp;
	u_int32_t part;
	u_int32_t slice;
	int shift;
	int mask;
	int snoop;

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
		 * sp->ds_size is for the whole disk in the WHOLE_DISK_SLICE,
		 * there are no reserved areas.
		 */
		endsecno = sp->ds_size;
		slicerel_secno = secno;
		snoop = 0;
	} else if (part == WHOLE_SLICE_PART) {
		/* 
		 * We are accessing a slice.  Enable snooping of the bsd
		 * label.  Note that snooping only occurs if ds_reserved
		 * is also non-zero.  ds_reserved will be non-zero if
		 * an in-core label is present or snooping has been
		 * explicitly requested via an ioctl().
		 *
		 * NOTE! opens on a whole-slice partition will not attempt
		 * to read a disklabel in, so there may not be an in-core
		 * disklabel even if there is one on the disk.
		 */
		endsecno = sp->ds_size;
		slicerel_secno = secno;
		snoop = 1;
	} else if ((lp = sp->ds_label) != NULL) {
		/*
		 * A label is present, extract the partition.  Snooping of
		 * the disklabel is not supported even if accessible.  Of
		 * course, the reserved area is still write protected.
		 */
		if (getpartbounds(lp, part, &slicerel_secno, &endsecno)) {
			kprintf("dscheck(%s): partition %d out of bounds\n",
				devtoname(dev), part);
			goto bad;
		}
		slicerel_secno += secno;
		snoop = 0;
	} else {
		/*
		 * Attempt to access partition when no disklabel present
		 */
		kprintf("dscheck(%s): attempt to access non-existant partition\n",
			devtoname(dev));
		goto bad;
	}

	/*
	 * Disallow writes to reserved areas unless ds_wlabel allows it.
	 */
	if (slicerel_secno < sp->ds_reserved && nsec &&
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
	 * Snoop reads and writes to the label area - only done if
	 * snoop is non-zero, ds_reserved is non-zero, and the
	 * read covers the label sector.
	 */
	if (snoop && slicerel_secno < sp->ds_reserved &&
	    slicerel_secno <= LABELSECTOR &&
	    nsec && slicerel_secno + nsec > LABELSECTOR) {
		/* 
		 * Set up our own callback on I/O completion to handle
		 * undoing the fixup we did for the write as well as
		 * doing the fixup for a read.
		 *
		 * Set info2.offset to the offset within the buffer containing
		 * the start of the label.
		 */
		nbio->bio_done = dsiodone;
		nbio->bio_caller_info1.ptr = sp;
		nbio->bio_caller_info2.offset =
			(LABELSECTOR - slicerel_secno) * ssp->dss_secsize;
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
		dsclrmask(sp, part);
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
	u_int32_t openmask[DKMAXPARTITIONS/(sizeof(u_int32_t)*8)];
	u_int64_t old_reserved;
	int part;
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;

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
		makevirginlabel(lp, ssp, sp, info);
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
			 * The disk management layer may not have read the
			 * disklabel yet because simply opening a slice no
			 * longer 'probes' the disk that way.  Be sure we
			 * have tried.
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
			dpart->reserved_blocks= sp->ds_reserved;

			if (slice != WHOLE_DISK_SLICE &&
			    part != WHOLE_SLICE_PART) {
				u_int64_t start;
				u_int64_t blocks;
				if (lp == NULL)
					return(EINVAL);
				if (getpartbounds(lp, part, &start, &blocks))
					return(EINVAL);
				dpart->fstype = getpartfstype(lp, part);
				dpart->media_offset += start *
						       info->d_media_blksize;
				dpart->media_size = blocks *
						    info->d_media_blksize;
				dpart->media_blocks = blocks;

				/*
				 * partition starting sector (p_offset)
				 * requires slice's reserved areas to be
				 * adjusted.
				 */
				if (dpart->reserved_blocks > start)
					dpart->reserved_blocks -= start;
				else
					dpart->reserved_blocks = 0;
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
			bzero(openmask, sizeof(openmask));
		} else {
			bcopy(sp->ds_openmask, openmask, sizeof(openmask));
		}
		error = setdisklabel(lp, (struct disklabel *)data,
				     sp, openmask);
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
				struct diskslice *ds = &ssp->dss_slices[slice];

				switch(dscountmask(ds)) {
				case 0:
					break;
				case 1:
					if (slice != WHOLE_DISK_SLICE)
						return (EBUSY);
					if (!dschkmask(ds, RAW_PART))
						return (EBUSY);
					break;
				default:
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
			for (part = 0; part < DKMAXPARTITIONS; ++part) {
				if (!dschkmask(&ssp->dss_slices[slice], part))
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
		 * Set the reserved area
		 */
		old_wlabel = sp->ds_wlabel;
		set_ds_wlabel(ssp, slice, TRUE);
		old_reserved = sp->ds_reserved;
		sp->ds_reserved = SBSIZE / ssp->dss_secsize;
		error = writedisklabel(dev, sp->ds_label);
		set_ds_wlabel(ssp, slice, old_wlabel);
		sp->ds_reserved = old_reserved;
		/* XXX should invalidate in-core label if write failed. */
		return (error);

	case DIOCSETSNOOP:
		/*
		 * Set label snooping even if there is no label present.
		 */
		if (slice == WHOLE_DISK_SLICE || part != WHOLE_SLICE_PART)
			return (EINVAL);
		if (lp == NULL) {
			if (*(int *)data) {
				sp->ds_reserved = SBSIZE / ssp->dss_secsize;
			} else {
				sp->ds_reserved = 0;
			}
		}
		return (0);

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
		if (dscountmask(&ssp->dss_slices[slice]))
			return (1);
	}
	return (0);
}

/*
 * Allocate a slices "struct" and initialize it to contain only an empty
 * compatibility slice (pointing to itself), a whole disk slice (covering
 * the disk as described by the label), and (nslices - BASE_SLICES) empty
 * slices beginning at BASE_SLICE.
 *
 * Note that the compatibility slice is no longer really a compatibility
 * slice.  It is slice 0 if a GPT label is present, and the dangerously
 * dedicated slice if no slice table otherwise exists.  Else it is 0-sized.
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
	int need_init;
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
		 */
		if (ssp->dss_nslices == BASE_SLICE) {
			sp = &ssp->dss_slices[COMPATIBILITY_SLICE];

			sp->ds_size = info->d_media_blocks;
			sp->ds_reserved = 0;
		}

		/*
		 * Set dss_first_bsd_slice to point at the first BSD
		 * slice, if any.
		 */
		for (slice = BASE_SLICE; slice < ssp->dss_nslices; slice++) {
			sp = &ssp->dss_slices[slice];
			if (sp->ds_type == DOSPTYP_386BSD /* XXX */) {
#if 0
				struct diskslice *csp;
#endif

				ssp->dss_first_bsd_slice = slice;
#if 0
				/*
				 * no longer supported, s0 is a real slice
				 * for GPT
				 */
				csp = &ssp->dss_slices[COMPATIBILITY_SLICE];
				csp->ds_offset = sp->ds_offset;
				csp->ds_size = sp->ds_size;
				csp->ds_type = sp->ds_type;
				csp->ds_reserved = sp->ds_reserved;
#endif
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
		sp->ds_reserved = 0;
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
		if (sp->ds_label == NULL || part >= getnumparts(sp->ds_label))
			return (EINVAL);
	}
	dssetmask(sp, part);

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
	u_int64_t old_reserved;

	sname = dsname(dev, dkunit(dev), slice, WHOLE_SLICE_PART, partname);
	lp1 = clone_label(info, sp);
	old_reserved = sp->ds_reserved;
	sp->ds_reserved = 0;
	msg = readdisklabel(dev, lp1);
	sp->ds_reserved = old_reserved;

	if (msg != NULL && (flags & DSO_COMPATLABEL)) {
		msg = NULL;
		kfree(lp1, M_DEVBUF);
		lp1 = clone_label(info, sp);
	}
	if (msg == NULL)
		msg = fixlabel(sname, sp, lp1, FALSE);
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
	u_int64_t start;
	u_int64_t blocks;

	slice = dkslice(dev);
	part = dkpart(dev);
	ssp = *sspp;
	if (ssp == NULL || slice >= ssp->dss_nslices
	    || !dschkmask(&ssp->dss_slices[slice], part)) {
		if (dev_dopen(dev, FREAD, S_IFCHR, proc0.p_ucred) != 0)
			return (-1);
		dev_dclose(dev, FREAD, S_IFCHR);
		ssp = *sspp;
	}
	lp = ssp->dss_slices[slice].ds_label;
	if (lp == NULL)
		return (-1);
	if (getpartbounds(lp, part, &start, &blocks))
		return (-1);
	return ((int64_t)blocks);
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

static void
set_ds_label(struct diskslices *ssp, int slice, struct disklabel *lp)
{
	struct diskslice *sp = &ssp->dss_slices[slice];

	sp->ds_label = lp;
	adjust_label_reserved(ssp, slice, sp);
}

static void
set_ds_wlabel(struct diskslices *ssp, int slice, int wlabel)
{
	ssp->dss_slices[slice].ds_wlabel = wlabel;
}

