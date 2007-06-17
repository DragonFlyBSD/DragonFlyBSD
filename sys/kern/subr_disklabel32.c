/*
 * Copyright (c) 2003-2007 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
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
 *	@(#)ufs_disksubr.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/subr_disk.c,v 1.20.2.6 2001/10/05 07:14:57 peter Exp $
 * $FreeBSD: src/sys/ufs/ufs/ufs_disksubr.c,v 1.44.2.3 2001/03/05 05:42:19 obrien Exp $
 * $DragonFly: src/sys/kern/subr_disklabel32.c,v 1.1 2007/06/17 23:50:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/dtype.h>		/* DTYPE_* constants */
#include <sys/sysctl.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>

#include <vfs/ufs/dinode.h>	/* XXX used only for fs.h */
#include <vfs/ufs/fs.h>		/* XXX used only to get BBSIZE/SBSIZE */

static void partition_info(const char *sname, int part, struct partition *pp);
static void slice_info(const char *sname, struct diskslice *sp);

/*
 * Retrieve the partition start and extent, in blocks.  Return 0 on success,
 * EINVAL on error.
 */
int
getpartbounds(struct disklabel *lp, u_int32_t part,
	      u_int64_t *start, u_int64_t *blocks)
{
	struct partition *pp;

	if (part >= lp->d_npartitions)
		return (EINVAL);
	pp = &lp->d_partitions[part];
	*blocks = pp->p_size;
	*start = pp->p_offset;
	return(0);
}

int
getpartfstype(struct disklabel *lp, u_int32_t part)
{
	struct partition *pp;

	if (part >= lp->d_npartitions)
		return (0);
	pp = &lp->d_partitions[part];
	return(pp->p_fstype);
}

u_int32_t
getnumparts(struct disklabel *lp)
{
	return(lp->d_npartitions);
}

/*
 * Attempt to read a disk label from a device using the indicated strategy
 * routine.  The label must be partly set up before this: secpercyl, secsize
 * and anything required in the strategy routine (e.g., dummy bounds for the
 * partition containing the label) must be filled in before calling us.
 * Returns NULL on success and an error string on failure.
 */
char *
readdisklabel(cdev_t dev, struct disklabel *lp)
{
	struct buf *bp;
	struct disklabel *dlp;
	char *msg = NULL;

	bp = geteblk((int)lp->d_secsize);
	bp->b_bio1.bio_offset = (off_t)LABELSECTOR * lp->d_secsize;
	bp->b_bcount = lp->d_secsize;
	bp->b_flags &= ~B_INVAL;
	bp->b_cmd = BUF_CMD_READ;
	dev_dstrategy(dev, &bp->b_bio1);
	if (biowait(bp))
		msg = "I/O error";
	else for (dlp = (struct disklabel *)bp->b_data;
	    dlp <= (struct disklabel *)((char *)bp->b_data +
	    lp->d_secsize - sizeof(*dlp));
	    dlp = (struct disklabel *)((char *)dlp + sizeof(long))) {
		if (dlp->d_magic != DISKMAGIC || dlp->d_magic2 != DISKMAGIC) {
			if (msg == NULL)
				msg = "no disk label";
		} else if (dlp->d_npartitions > MAXPARTITIONS ||
			   dkcksum(dlp) != 0)
			msg = "disk label corrupted";
		else {
			*lp = *dlp;
			msg = NULL;
			break;
		}
	}
	bp->b_flags |= B_INVAL | B_AGE;
	brelse(bp);
	return (msg);
}

/*
 * Check new disk label for sensibility before setting it.
 */
int
setdisklabel(struct disklabel *olp, struct disklabel *nlp,
	     struct diskslice *sp, u_int32_t *openmask)
{
	struct partition *opp, *npp;
	int part;
	int i;

	/*
	 * Check it is actually a disklabel we are looking at.
	 */
	if (nlp->d_magic != DISKMAGIC || nlp->d_magic2 != DISKMAGIC ||
	    dkcksum(nlp) != 0)
		return (EINVAL);

	/*
	 * For each partition that we think is open, check the new disklabel
	 * for compatibility.  Ignore special partitions (>= 128).
	 */
	i = 0;
	while (i < 128) {
		if (openmask[i >> 5] == 0) {
			i += 32;
			continue;
		}
		if ((openmask[i >> 5] & (1 << (i & 31))) == 0) {
			++i;
			continue;
		}
		if (nlp->d_npartitions <= i)
			return (EBUSY);
		opp = &olp->d_partitions[i];
		npp = &nlp->d_partitions[i];
		if (npp->p_offset != opp->p_offset || npp->p_size < opp->p_size)
			return (EBUSY);
		/*
		 * Copy internally-set partition information
		 * if new label doesn't include it.		XXX
		 * (If we are using it then we had better stay the same type)
		 * This is possibly dubious, as someone else noted (XXX)
		 */
		if (npp->p_fstype == FS_UNUSED && opp->p_fstype != FS_UNUSED) {
			npp->p_fstype = opp->p_fstype;
			npp->p_fsize = opp->p_fsize;
			npp->p_frag = opp->p_frag;
			npp->p_cpg = opp->p_cpg;
		}
		++i;
	}
 	nlp->d_checksum = 0;
 	nlp->d_checksum = dkcksum(nlp);
	*olp = *nlp;

	if (olp->d_partitions[RAW_PART].p_offset)
		return (EXDEV);
	if (olp->d_secperunit > sp->ds_size)
		return (ENOSPC);
	for (part = 0; part < olp->d_npartitions; ++part) {
		if (olp->d_partitions[part].p_size > sp->ds_size)
			return(ENOSPC);
	}
	return (0);
}

/*
 * Write disk label back to device after modification.
 */
int
writedisklabel(cdev_t dev, struct disklabel *lp)
{
	struct buf *bp;
	struct disklabel *dlp;
	int error = 0;

	if (lp->d_partitions[RAW_PART].p_offset != 0)
		return (EXDEV);			/* not quite right */
	bp = geteblk((int)lp->d_secsize);
	bp->b_bio1.bio_offset = (off_t)LABELSECTOR * lp->d_secsize;
	bp->b_bcount = lp->d_secsize;
#if 1
	/*
	 * We read the label first to see if it's there,
	 * in which case we will put ours at the same offset into the block..
	 * (I think this is stupid [Julian])
	 * Note that you can't write a label out over a corrupted label!
	 * (also stupid.. how do you write the first one? by raw writes?)
	 */
	bp->b_flags &= ~B_INVAL;
	bp->b_cmd = BUF_CMD_READ;
	dev_dstrategy(dkmodpart(dev, WHOLE_SLICE_PART), &bp->b_bio1);
	error = biowait(bp);
	if (error)
		goto done;
	for (dlp = (struct disklabel *)bp->b_data;
	    dlp <= (struct disklabel *)
	      ((char *)bp->b_data + lp->d_secsize - sizeof(*dlp));
	    dlp = (struct disklabel *)((char *)dlp + sizeof(long))) {
		if (dlp->d_magic == DISKMAGIC && dlp->d_magic2 == DISKMAGIC &&
		    dkcksum(dlp) == 0) {
			*dlp = *lp;
			bp->b_cmd = BUF_CMD_WRITE;
			dev_dstrategy(dkmodpart(dev, WHOLE_SLICE_PART), &bp->b_bio1);
			error = biowait(bp);
			goto done;
		}
	}
	error = ESRCH;
done:
#else
	bzero(bp->b_data, lp->d_secsize);
	dlp = (struct disklabel *)bp->b_data;
	*dlp = *lp;
	bp->b_flags &= ~B_INVAL;
	bp->b_cmd = BUF_CMD_WRITE;
	BUF_STRATEGY(bp, 1);
	error = biowait(bp);
#endif
	bp->b_flags |= B_INVAL | B_AGE;
	brelse(bp);
	return (error);
}

/*
 * Create a disklabel based on a disk_info structure, initializing
 * the appropriate fields and creating a raw partition that covers the
 * whole disk.
 *
 * If a diskslice is passed, the label is truncated to the slice
 */
struct disklabel *
clone_label(struct disk_info *info, struct diskslice *sp)
{
	struct disklabel *lp1;

	lp1 = kmalloc(sizeof *lp1, M_DEVBUF, M_WAITOK | M_ZERO);
	lp1->d_nsectors = info->d_secpertrack;
	lp1->d_ntracks = info->d_nheads;
	lp1->d_secpercyl = info->d_secpercyl;
	lp1->d_secsize = info->d_media_blksize;

	if (sp)
		lp1->d_secperunit = (u_int)sp->ds_size;
	else
		lp1->d_secperunit = (u_int)info->d_media_blocks;

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

	/*
	 * Used by various devices to create a compatibility slice which
	 * allows us to mount root from devices which do not have a
	 * disklabel.  Particularly: CDs.
	 */
	lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	if (info->d_dsflags & DSO_COMPATPARTA) {
		lp1->d_partitions[0].p_size = lp1->d_secperunit;
		lp1->d_partitions[0].p_fstype = FS_OTHER;
	}
	lp1->d_magic = DISKMAGIC;
	lp1->d_magic2 = DISKMAGIC;
	lp1->d_checksum = dkcksum(lp1);
	return (lp1);
}

void
makevirginlabel(struct disklabel *lp, struct diskslices *ssp,
		struct diskslice *sp, struct disk_info *info)
{
	struct partition *pp;

	if (ssp->dss_slices[WHOLE_DISK_SLICE].ds_label) {
		bcopy(ssp->dss_slices[WHOLE_DISK_SLICE].ds_label, lp,
		      sizeof(*lp));
	} else {
		bzero(lp, sizeof(*lp));
	}
	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;

	lp->d_npartitions = MAXPARTITIONS;
	if (lp->d_interleave == 0)
		lp->d_interleave = 1;
	if (lp->d_rpm == 0)
		lp->d_rpm = 3600;
	if (lp->d_nsectors == 0)	/* sectors per track */
		lp->d_nsectors = 32;
	if (lp->d_ntracks == 0)		/* heads */
		lp->d_ntracks = 64;
	lp->d_ncylinders = 0;
	lp->d_bbsize = BBSIZE;
	lp->d_sbsize = SBSIZE;

	/*
	 * If the slice or GPT partition is really small we could
	 * wind up with an absurd calculation for ncylinders.
	 */
	while (lp->d_ncylinders < 4) {
		if (lp->d_ntracks > 1)
			lp->d_ntracks >>= 1;
		else if (lp->d_nsectors > 1)
			lp->d_nsectors >>= 1;
		else
			break;
		lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
		lp->d_ncylinders = sp->ds_size / lp->d_secpercyl;
	}

	/*
	 * Set or Modify the partition sizes to accomodate the slice,
	 * since we started with a copy of the virgin label stored
	 * in the whole-disk-slice and we are probably not a
	 * whole-disk slice.
	 */
	lp->d_secperunit = sp->ds_size;
	pp = &lp->d_partitions[RAW_PART];
	pp->p_offset = 0;
	pp->p_size = lp->d_secperunit;
	if (info->d_dsflags & DSO_COMPATPARTA) {
		pp = &lp->d_partitions[0];
		pp->p_offset = 0;
		pp->p_size = lp->d_secperunit;
		pp->p_fstype = FS_OTHER;
	}
	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
}

char *
fixlabel(const char *sname, struct diskslice *sp, struct disklabel *lp, int writeflag)
{
	u_int64_t start;
	u_int64_t end;
	u_int64_t offset;
	int part;
	struct partition *pp;
	int warned;

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

void
adjust_label_reserved(struct diskslices *ssp, int slice, struct diskslice *sp)
{
	struct disklabel *lp = sp->ds_label;

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
			 * BSD uses in-band labels, meaning the label itself
			 * is accessible from partitions within the label.
			 * We must reserved the area taken up by the label
			 * itself to prevent mistakes from wiping it.
			 */
			sp->ds_reserved = SBSIZE / ssp->dss_secsize;
		} else {
			sp->ds_reserved = 0;
		}
	}
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

