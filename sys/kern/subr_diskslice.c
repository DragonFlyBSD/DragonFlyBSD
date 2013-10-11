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
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/disklabel32.h>
#include <sys/disklabel64.h>
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
#include <sys/devfs.h>

static int  dsreadandsetlabel(cdev_t dev, u_int flags,
			   struct diskslices *ssp, struct diskslice *sp,
			   struct disk_info *info);
static void free_ds_label (struct diskslices *ssp, int slice);
static void set_ds_label (struct diskslices *ssp, int slice, disklabel_t lp,
			   disklabel_ops_t ops);
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
	disklabel_t lp;
	disklabel_ops_t ops;
	long nsec;
	u_int64_t secno;
	u_int64_t endsecno;
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
			devtoname(dev), (long long)bio->bio_offset);
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
		 * This really puts the nail in the coffin.
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
		lp.opaque = NULL;
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
		} else {
			/*
			 * If writing to the raw disk request a
			 * reprobe on the last close.
			 */
			if (bp->b_cmd == BUF_CMD_WRITE)
				sp->ds_flags |= DSF_REPROBE;
		}

		/*
		 * sp->ds_size is for the whole disk in the WHOLE_DISK_SLICE,
		 * there are no reserved areas.
		 */
		endsecno = sp->ds_size;
		slicerel_secno = secno;
	} else if (part == WHOLE_SLICE_PART) {
		/* 
		 * NOTE! opens on a whole-slice partition will not attempt
		 * to read a disklabel in, so there may not be an in-core
		 * disklabel even if there is one on the disk.
		 */
		endsecno = sp->ds_size;
		slicerel_secno = secno;
	} else if ((lp = sp->ds_label).opaque != NULL) {
		/*
		 * A label is present, extract the partition.  Snooping of
		 * the disklabel is not supported even if accessible.  Of
		 * course, the reserved area is still write protected.
		 */
		ops = sp->ds_ops;
		if (ops->op_getpartbounds(ssp, lp, part,
					  &slicerel_secno, &endsecno)) {
			kprintf("dscheck(%s): partition %d out of bounds\n",
				devtoname(dev), part);
			goto bad;
		}
		slicerel_secno += secno;
	} else {
		/*
		 * Attempt to access partition when no disklabel present
		 */
		kprintf("dscheck(%s): attempt to access non-existent partition\n",
			devtoname(dev));
		goto bad;
	}

	/*
	 * Disallow writes to reserved areas unless ds_wlabel allows it.
	 * If the reserved area is written to request a reprobe of the
	 * disklabel when the slice is closed.
	 */
	if (slicerel_secno < sp->ds_reserved && nsec &&
	    bp->b_cmd == BUF_CMD_WRITE) {
		if (sp->ds_wlabel == 0) {
			bp->b_error = EROFS;
			goto error;
		}
		sp->ds_flags |= DSF_REPROBE;
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
			devtoname(dev), (long long)bio->bio_offset,
			ssp->dss_secsize);
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
	return (nbio);

bad_bcount:
	kprintf(
	"dscheck(%s): b_bcount %d is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), bp->b_bcount, ssp->dss_secsize);
	goto bad;

bad_blkno:
	kprintf(
	"dscheck(%s): bio_offset %lld is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), (long long)bio->bio_offset, ssp->dss_secsize);
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

/*
 * dsclose() - close a cooked disk slice.
 *
 * WARNING!  The passed diskslices and related diskslice structures may
 *	     be invalidated or replaced by this function, callers must
 *	     reload from the disk structure for continued access.
 */
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
		if (sp->ds_flags & DSF_REPROBE) {
			sp->ds_flags &= ~DSF_REPROBE;
			if (slice == WHOLE_DISK_SLICE) {
				disk_msg_send_sync(DISK_DISK_REPROBE,
						   dev->si_disk, NULL);
				devfs_config();
			} else {
				disk_msg_send_sync(DISK_SLICE_REPROBE,
						   dev->si_disk, sp);
				devfs_config();
			}
			/* ssp and sp may both be invalid after reprobe */
		}
	}
}

void
dsgone(struct diskslices **sspp)
{
	int slice;
	struct diskslices *ssp;

	if ((ssp = *sspp) != NULL) {
		for (slice = 0; slice < ssp->dss_nslices; slice++)
			free_ds_label(ssp, slice);
		kfree(ssp, M_DEVBUF);
		*sspp = NULL;
	}
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
	disklabel_t lp;
	disklabel_t lptmp;
	disklabel_ops_t ops;
	int old_wlabel;
	u_int32_t openmask[DKMAXPARTITIONS/(sizeof(u_int32_t)*8)];
	int part;
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;

	slice = dkslice(dev);
	part = dkpart(dev);
	ssp = *sspp;
	if (ssp == NULL)
		return (EINVAL);
	if (slice >= ssp->dss_nslices)
		return (EINVAL);
	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	ops = sp->ds_ops;	/* may be NULL if no label */

	switch (cmd) {
	case DIOCGDVIRGIN32:
		ops = &disklabel32_ops;
		/* fall through */
	case DIOCGDVIRGIN64:
		if (cmd != DIOCGDVIRGIN32)
			ops = &disklabel64_ops;
		/*
		 * You can only retrieve a virgin disklabel on the whole
		 * disk slice or whole-slice partition.
		 */
		if (slice != WHOLE_DISK_SLICE &&
		    part != WHOLE_SLICE_PART) {
			return(EINVAL);
		}

		lp.opaque = data;
		ops->op_makevirginlabel(lp, ssp, sp, info);
		return (0);

	case DIOCGDINFO32:
	case DIOCGDINFO64:
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
		if (sp->ds_label.opaque == NULL) {
			error = dsreadandsetlabel(dev, info->d_dsflags,
						  ssp, sp, info);
			ops = sp->ds_ops;	/* may be NULL */
		}

		/*
		 * The type of label we found must match the type of
		 * label requested.
		 */
		if (error == 0 && IOCPARM_LEN(cmd) != ops->labelsize)
			error = ENOATTR;
		if (error == 0)
			bcopy(sp->ds_label.opaque, data, ops->labelsize);
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
			if (sp->ds_label.opaque == NULL &&
			    part == WHOLE_SLICE_PART &&
			    slice != WHOLE_DISK_SLICE) {
				dsreadandsetlabel(dev, info->d_dsflags,
						  ssp, sp, info);
				ops = sp->ds_ops;	/* may be NULL */
			}

			bzero(dpart, sizeof(*dpart));
			dpart->media_offset   = (u_int64_t)sp->ds_offset *
						info->d_media_blksize;
			dpart->media_size     = (u_int64_t)sp->ds_size *
						info->d_media_blksize;
			dpart->media_blocks   = sp->ds_size;
			dpart->media_blksize  = info->d_media_blksize;
			dpart->reserved_blocks= sp->ds_reserved;
			dpart->fstype_uuid = sp->ds_type_uuid;
			dpart->storage_uuid = sp->ds_stor_uuid;

			if (slice != WHOLE_DISK_SLICE &&
			    part != WHOLE_SLICE_PART) {
				u_int64_t start;
				u_int64_t blocks;
				if (lp.opaque == NULL)
					return(EINVAL);
				if (ops->op_getpartbounds(ssp, lp, part,
							  &start, &blocks)) {
					return(EINVAL);
				}
				ops->op_loadpartinfo(lp, part, dpart);
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

	case DIOCSDINFO32:
		ops = &disklabel32_ops;
		/* fall through */
	case DIOCSDINFO64:
		if (cmd != DIOCSDINFO32)
			ops = &disklabel64_ops;
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

		/*
		 * If an existing label is present it must be the same
		 * type as the label being passed by the ioctl.
		 */
		if (sp->ds_label.opaque && sp->ds_ops != ops)
			return (ENOATTR);

		/*
		 * Create a temporary copy of the existing label
		 * (if present) so setdisklabel can compare it against
		 * the new label.
		 */
		lp.opaque = kmalloc(ops->labelsize, M_DEVBUF, M_WAITOK);
		if (sp->ds_label.opaque == NULL)
			bzero(lp.opaque, ops->labelsize);
		else
			bcopy(sp->ds_label.opaque, lp.opaque, ops->labelsize);
		if (sp->ds_label.opaque == NULL) {
			bzero(openmask, sizeof(openmask));
		} else {
			bcopy(sp->ds_openmask, openmask, sizeof(openmask));
		}
		lptmp.opaque = data;
		error = ops->op_setdisklabel(lp, lptmp, ssp, sp, openmask);
		disk_msg_send_sync(DISK_SLICE_REPROBE, dev->si_disk, sp);
		devfs_config();
		if (error != 0) {
			kfree(lp.opaque, M_DEVBUF);
			return (error);
		}
		free_ds_label(ssp, slice);
		set_ds_label(ssp, slice, lp, ops);
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

		disk_msg_send_sync(DISK_DISK_REPROBE, dev->si_disk, NULL);
		devfs_config();
		return 0;

	case DIOCWDINFO32:
	case DIOCWDINFO64:
		error = dsioctl(dev, ((cmd == DIOCWDINFO32) ?
					DIOCSDINFO32 : DIOCSDINFO64),
				data, flags, &ssp, info);
		if (error == 0 && sp->ds_label.opaque == NULL)
			error = EINVAL;
		if (part != WHOLE_SLICE_PART)
			error = EINVAL;
		if (error != 0)
			return (error);

		/*
		 * Allow the reserved area to be written, reload ops
		 * because the DIOCSDINFO op above may have installed
		 * a new label type.
		 */
		ops = sp->ds_ops;
		old_wlabel = sp->ds_wlabel;
		set_ds_wlabel(ssp, slice, TRUE);
		error = ops->op_writedisklabel(dev, ssp, sp, sp->ds_label);
		disk_msg_send_sync(DISK_SLICE_REPROBE, dev->si_disk, sp);
		devfs_config();
		set_ds_wlabel(ssp, slice, old_wlabel);
		/* XXX should invalidate in-core label if write failed. */
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
	return dev->si_name;
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
	struct diskslice *sp;
	struct diskslices *ssp;
	int slice;
	int part;

	ssp = *sspp;
	dev->si_bsize_phys = info->d_media_blksize;
	slice = dkslice(dev);
	part = dkpart(dev);
	sp = &ssp->dss_slices[slice];
	dssetmask(sp, part);

	return 0;
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
	disklabel_t lp;
	disklabel_ops_t ops;
	const char *msg;
	const char *sname;
	char partname[2];
	int slice = dkslice(dev);

	/*
	 * Probe the disklabel
	 */
	lp.opaque = NULL;
	sname = dsname(dev, dkunit(dev), slice, WHOLE_SLICE_PART, partname);
	ops = &disklabel32_ops;
	msg = ops->op_readdisklabel(dev, sp, &lp, info);
	if (msg && strcmp(msg, "no disk label") == 0) {
		ops = &disklabel64_ops;
		msg = disklabel64_ops.op_readdisklabel(dev, sp, &lp, info);
	}

	/*
	 * If we failed and COMPATLABEL is set, create a dummy disklabel.
	 */
	if (msg != NULL && (flags & DSO_COMPATLABEL)) {
		msg = NULL;
		if (sp->ds_size >= 0x100000000ULL)
			ops = &disklabel64_ops;
		else
			ops = &disklabel32_ops;
		lp = ops->op_clone_label(info, sp);
	}
	if (msg != NULL) {
		if (sp->ds_type == DOSPTYP_386BSD /* XXX */)
			log(LOG_WARNING, "%s: cannot find label (%s)\n",
			    sname, msg);
		if (lp.opaque)
			kfree(lp.opaque, M_DEVBUF);
	} else {
		set_ds_label(ssp, slice, lp, ops);
		set_ds_wlabel(ssp, slice, FALSE);
	}
	return (msg ? EINVAL : 0);
}

int64_t
dssize(cdev_t dev, struct diskslices **sspp)
{
	disklabel_t lp;
	disklabel_ops_t ops;
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
	if (part == WHOLE_SLICE_PART) {
		blocks = ssp->dss_slices[slice].ds_size;
	} else if (lp.opaque == NULL) {
		blocks = (u_int64_t)-1;
	} else {
		ops = ssp->dss_slices[slice].ds_ops;
		if (ops->op_getpartbounds(ssp, lp, part, &start, &blocks))
			return (-1);
	}
	return ((int64_t)blocks);
}

static void
free_ds_label(struct diskslices *ssp, int slice)
{
	struct diskslice *sp;
	disklabel_t lp;

	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	if (lp.opaque != NULL) {
		kfree(lp.opaque, M_DEVBUF);
		lp.opaque = NULL;
		set_ds_label(ssp, slice, lp, NULL);
	}
}

static void
set_ds_label(struct diskslices *ssp, int slice,
	     disklabel_t lp, disklabel_ops_t ops)
{
	struct diskslice *sp = &ssp->dss_slices[slice];

	sp->ds_label = lp;
	sp->ds_ops = ops;
	if (lp.opaque && slice != WHOLE_DISK_SLICE)
		ops->op_adjust_label_reserved(ssp, slice, sp);
	else
		sp->ds_reserved = 0;
}

static void
set_ds_wlabel(struct diskslices *ssp, int slice, int wlabel)
{
	ssp->dss_slices[slice].ds_wlabel = wlabel;
}

