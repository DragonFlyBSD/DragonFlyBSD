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
 * 
 * $DragonFly: src/sys/kern/subr_disklabel64.c,v 1.5 2007/07/20 17:21:51 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/disklabel64.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/kern_syscall.h>
#include <sys/buf2.h>

/*
 * Alignment against physical start (verses slice start).  We use a megabyte
 * here.  Why do we use a megabyte?  Because SSDs already use large 128K
 * blocks internally (for MLC) and who the hell knows in the future.
 *
 * This way if the sysop picks sane values for partition sizes everything
 * will be nicely aligned, particularly swap for e.g. swapcache, and
 * clustered operations against larger physical sector sizes for newer HDs,
 * and so forth.
 */
#define PALIGN_SIZE	(1024 * 1024)
#define PALIGN_MASK	(PALIGN_SIZE - 1)

/*
 * Retrieve the partition start and extent, in blocks.  Return 0 on success,
 * EINVAL on error.
 */
static int
l64_getpartbounds(struct diskslices *ssp, disklabel_t lp, u_int32_t part,
		  u_int64_t *start, u_int64_t *blocks)
{
	struct partition64 *pp;

	if (part >= lp.lab64->d_npartitions)
		return (EINVAL);

	pp = &lp.lab64->d_partitions[part];

	if ((pp->p_boffset & (ssp->dss_secsize - 1)) ||
	    (pp->p_bsize & (ssp->dss_secsize - 1))) {
		return (EINVAL);
	}
	*start = pp->p_boffset / ssp->dss_secsize;
	*blocks = pp->p_bsize / ssp->dss_secsize;
	return(0);
}

/*
 * Get the filesystem type XXX - diskslices code needs to use uuids
 */
static void
l64_loadpartinfo(disklabel_t lp, u_int32_t part, struct partinfo *dpart)
{
	struct partition64 *pp;
	const size_t uuid_size = sizeof(struct uuid);

	if (part < lp.lab64->d_npartitions) {
		pp = &lp.lab64->d_partitions[part];
		dpart->fstype_uuid = pp->p_type_uuid;
		dpart->storage_uuid = pp->p_stor_uuid;
		dpart->fstype = pp->p_fstype;
	} else {
		bzero(&dpart->fstype_uuid, uuid_size);
		bzero(&dpart->storage_uuid, uuid_size);
		dpart->fstype = 0;
	}
}

/*
 * Get the number of partitions
 */
static u_int32_t
l64_getnumparts(disklabel_t lp)
{
	return(lp.lab64->d_npartitions);
}

static void
l64_freedisklabel(disklabel_t *lpp)
{
	kfree((*lpp).lab64, M_DEVBUF);
	(*lpp).lab64 = NULL;
}

/*
 * Attempt to read a disk label from a device.  64 bit disklabels are
 * sector-agnostic and begin at offset 0 on the device.  64 bit disklabels
 * may only be used with GPT partitioning schemes.
 *
 * Returns NULL on sucess, and an error string on failure.
 */
static const char *
l64_readdisklabel(cdev_t dev, struct diskslice *sp, disklabel_t *lpp,
		  struct disk_info *info)
{
	struct buf *bp;
	struct disklabel64 *dlp;
	const char *msg;
	uint32_t savecrc;
	size_t dlpcrcsize;
	size_t bpsize;
	int secsize;

	/*
	 * XXX I/O size is subject to device DMA limitations
	 */
	secsize = info->d_media_blksize;
	bpsize = (sizeof(*dlp) + secsize - 1) & ~(secsize - 1);

	bp = geteblk(bpsize);
	bp->b_bio1.bio_offset = 0;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bcount = bpsize;
	bp->b_flags &= ~B_INVAL;
	bp->b_flags |= B_FAILONDIS;
	bp->b_cmd = BUF_CMD_READ;
	dev_dstrategy(dev, &bp->b_bio1);

	if (biowait(&bp->b_bio1, "labrd")) {
		msg = "I/O error";
	} else {
		dlp = (struct disklabel64 *)bp->b_data;
		dlpcrcsize = offsetof(struct disklabel64,
				      d_partitions[dlp->d_npartitions]) -
			     offsetof(struct disklabel64, d_magic);
		savecrc = dlp->d_crc;
		dlp->d_crc = 0;
		if (dlp->d_magic != DISKMAGIC64) {
			msg = "no disk label";
		} else if (dlp->d_npartitions > MAXPARTITIONS64) {
			msg = "disklabel64 corrupted, too many partitions";
		} else if (savecrc != crc32(&dlp->d_magic, dlpcrcsize)) {
			msg = "disklabel64 corrupted, bad CRC";
		} else {
			dlp->d_crc = savecrc;
			(*lpp).lab64 = kmalloc(sizeof(*dlp),
					       M_DEVBUF, M_WAITOK|M_ZERO);
			*(*lpp).lab64 = *dlp;
			msg = NULL;
		}
	}
	bp->b_flags |= B_INVAL | B_AGE;
	brelse(bp);
	return (msg);
}

/*
 * If everything is good, copy olpx to nlpx.  Check to see if any
 * open partitions would change.
 */
static int
l64_setdisklabel(disklabel_t olpx, disklabel_t nlpx, struct diskslices *ssp,
		 struct diskslice *sp, u_int32_t *openmask)
{
	struct disklabel64 *olp, *nlp;
	struct partition64 *opp, *npp;
	uint32_t savecrc;
	uint64_t slicebsize;
	size_t nlpcrcsize;
	int i;

	olp = olpx.lab64;
	nlp = nlpx.lab64;

	slicebsize = (uint64_t)sp->ds_size * ssp->dss_secsize;

	if (nlp->d_magic != DISKMAGIC64)
		return (EINVAL);
	if (nlp->d_npartitions > MAXPARTITIONS64)
		return (EINVAL);
	savecrc = nlp->d_crc;
	nlp->d_crc = 0;
	nlpcrcsize = offsetof(struct disklabel64, 
			      d_partitions[nlp->d_npartitions]) -
		     offsetof(struct disklabel64, d_magic);
	if (crc32(&nlp->d_magic, nlpcrcsize) != savecrc) {
		nlp->d_crc = savecrc;
		return (EINVAL);
	}
	nlp->d_crc = savecrc;

	/*
	 * Check if open partitions have changed
	 */
	i = 0;
	while (i < MAXPARTITIONS64) {
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
		if (npp->p_boffset != opp->p_boffset ||
		    npp->p_bsize < opp->p_bsize) {
			return (EBUSY);
		}

		/*
		 * Do not allow p_type_uuid or p_stor_uuid to change if
		 * the partition is currently open.
		 */
		if (bcmp(&npp->p_type_uuid, &opp->p_type_uuid,
		     sizeof(npp->p_type_uuid)) != 0) {
			return (EBUSY);
		}
		if (bcmp(&npp->p_stor_uuid, &opp->p_stor_uuid,
		     sizeof(npp->p_stor_uuid)) != 0) {
			return (EBUSY);
		}
		++i;
	}

	/*
	 * Make sure the label and partition offsets and sizes are sane.
	 */
	if (nlp->d_total_size > slicebsize)
		return (ENOSPC);
	if (nlp->d_total_size & (ssp->dss_secsize - 1))
		return (EINVAL);
	if (nlp->d_bbase & (ssp->dss_secsize - 1))
		return (EINVAL);
	if (nlp->d_pbase & (ssp->dss_secsize - 1))
		return (EINVAL);
	if (nlp->d_pstop & (ssp->dss_secsize - 1))
		return (EINVAL);
	if (nlp->d_abase & (ssp->dss_secsize - 1))
		return (EINVAL);

	for (i = 0; i < nlp->d_npartitions; ++i) {
		npp = &nlp->d_partitions[i];
		if (npp->p_bsize == 0) {
			if (npp->p_boffset != 0)
				return (EINVAL);
			continue;
		}
		if (npp->p_boffset & (ssp->dss_secsize - 1))
			return (EINVAL);
		if (npp->p_bsize & (ssp->dss_secsize - 1))
			return (EINVAL);
		if (npp->p_boffset < nlp->d_pbase)
			return (ENOSPC);
		if (npp->p_boffset + npp->p_bsize > nlp->d_total_size)
			return (ENOSPC);
	}

	/*
	 * Structurally we may add code to make modifications above in the
	 * future, so regenerate the crc anyway.
	 */
	nlp->d_crc = 0;
	nlp->d_crc = crc32(&nlp->d_magic, nlpcrcsize);
	*olp = *nlp;

	return (0);
}

/*
 * Write disk label back to device after modification.
 */
static int
l64_writedisklabel(cdev_t dev, struct diskslices *ssp,
		   struct diskslice *sp, disklabel_t lpx)
{
	struct disklabel64 *lp;
	struct disklabel64 *dlp;
	struct buf *bp;
	int error = 0;
	size_t bpsize;
	int secsize;

	lp = lpx.lab64;

	/*
	 * XXX I/O size is subject to device DMA limitations
	 */
	secsize = ssp->dss_secsize;
	bpsize = (sizeof(*lp) + secsize - 1) & ~(secsize - 1);

	bp = geteblk(bpsize);
	bp->b_bio1.bio_offset = 0;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bcount = bpsize;
	bp->b_flags |= B_FAILONDIS;

	/*
	 * Because our I/O is larger then the label, and because we do not
	 * write the d_reserved0[] area, do a read-modify-write.
	 */
	bp->b_flags &= ~B_INVAL;
	bp->b_cmd = BUF_CMD_READ;
	KKASSERT(dkpart(dev) == WHOLE_SLICE_PART);
	dev_dstrategy(dev, &bp->b_bio1);
	error = biowait(&bp->b_bio1, "labrd");
	if (error)
		goto done;

	dlp = (void *)bp->b_data;
	bcopy(&lp->d_magic, &dlp->d_magic,
	      sizeof(*lp) - offsetof(struct disklabel64, d_magic));
	bp->b_cmd = BUF_CMD_WRITE;
	bp->b_bio1.bio_done = biodone_sync;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	KKASSERT(dkpart(dev) == WHOLE_SLICE_PART);
	dev_dstrategy(dev, &bp->b_bio1);
	error = biowait(&bp->b_bio1, "labwr");
done:
	bp->b_flags |= B_INVAL | B_AGE;
	brelse(bp);
	return (error);
}

/*
 * Create a disklabel based on a disk_info structure for the purposes of
 * DSO_COMPATLABEL - cases where no real label exists on the storage medium.
 *
 * If a diskslice is passed, the label is truncated to the slice.
 *
 * NOTE!  This is not a legal label because d_bbase and d_pbase are both
 * set to 0.
 */
static disklabel_t
l64_clone_label(struct disk_info *info, struct diskslice *sp)
{
	struct disklabel64 *lp;
	disklabel_t res;
	uint32_t blksize = info->d_media_blksize;
	size_t lpcrcsize;

	lp = kmalloc(sizeof *lp, M_DEVBUF, M_WAITOK | M_ZERO);

	if (sp)
		lp->d_total_size = (uint64_t)sp->ds_size * blksize;
	else
		lp->d_total_size = info->d_media_blocks * blksize;

	lp->d_magic = DISKMAGIC64;
	lp->d_align = blksize;
	lp->d_npartitions = MAXPARTITIONS64;
	lp->d_pstop = lp->d_total_size;

	/*
	 * Create a dummy 'c' part and a dummy 'a' part (if requested).
	 * Note that the 'c' part is really a hack.  64 bit disklabels
	 * do not use 'c' to mean the raw partition.
	 */

	lp->d_partitions[2].p_boffset = 0;
	lp->d_partitions[2].p_bsize = lp->d_total_size;
	/* XXX SET FS TYPE */

	if (info->d_dsflags & DSO_COMPATPARTA) {
		lp->d_partitions[0].p_boffset = 0;
		lp->d_partitions[0].p_bsize = lp->d_total_size;
		/* XXX SET FS TYPE */
	}

	lpcrcsize = offsetof(struct disklabel64,
			     d_partitions[lp->d_npartitions]) -
		    offsetof(struct disklabel64, d_magic);

	lp->d_crc = crc32(&lp->d_magic, lpcrcsize);
	res.lab64 = lp;
	return (res);
}

/*
 * Create a virgin disklabel64 suitable for writing to the media.
 *
 * disklabel64 always reserves 32KB for a boot area and leaves room
 * for up to RESPARTITIONS64 partitions.  
 */
static void
l64_makevirginlabel(disklabel_t lpx, struct diskslices *ssp,
		    struct diskslice *sp, struct disk_info *info)
{
	struct disklabel64 *lp = lpx.lab64;
	struct partition64 *pp;
	uint32_t blksize;
	uint32_t ressize;
	uint64_t blkmask;	/* 64 bits so we can ~ */
	size_t lpcrcsize;

	/*
	 * Setup the initial label.  Use of a block size of at least 4KB
	 * for calculating the initial reserved areas to allow some degree
	 * of portability between media with different sector sizes.
	 *
	 * Note that the modified blksize is stored in d_align as a hint
	 * to the disklabeling program.
	 */
	bzero(lp, sizeof(*lp));
	if ((blksize = info->d_media_blksize) < 4096)
		blksize = 4096;
	blkmask = blksize - 1;

	if (sp)
		lp->d_total_size = (uint64_t)sp->ds_size * ssp->dss_secsize;
	else
		lp->d_total_size = info->d_media_blocks * info->d_media_blksize;

	lp->d_magic = DISKMAGIC64;
	lp->d_align = blksize;
	lp->d_npartitions = MAXPARTITIONS64;
	kern_uuidgen(&lp->d_stor_uuid, 1);

	ressize = offsetof(struct disklabel64, d_partitions[RESPARTITIONS64]);
	ressize = (ressize + (uint32_t)blkmask) & ~blkmask;

	/*
	 * NOTE: When calculating pbase take into account the slice offset
	 *	 so the partitions are at least 32K-aligned relative to the
	 *	 start of the physical disk.  This will accomodate efficient
	 *	 access to 4096 byte physical sector drives.
	 */
	lp->d_bbase = ressize;
	lp->d_pbase = lp->d_bbase + ((32768 + blkmask) & ~blkmask);
	lp->d_pbase = (lp->d_pbase + PALIGN_MASK) & ~(uint64_t)PALIGN_MASK;

	/* adjust for slice offset so we are physically aligned */
	lp->d_pbase += 32768 - (sp->ds_offset * info->d_media_blksize) % 32768;

	lp->d_pstop = (lp->d_total_size - lp->d_bbase) & ~blkmask;
	lp->d_abase = lp->d_pstop;

	/*
	 * All partitions are left empty unless DSO_COMPATPARTA is set
	 */

	if (info->d_dsflags & DSO_COMPATPARTA) {
		pp = &lp->d_partitions[0];
		pp->p_boffset = lp->d_pbase;
		pp->p_bsize = lp->d_pstop - lp->d_pbase;
		/* XXX SET FS TYPE */
	}

	lpcrcsize = offsetof(struct disklabel64,
			     d_partitions[lp->d_npartitions]) -
		    offsetof(struct disklabel64, d_magic);
	lp->d_crc = crc32(&lp->d_magic, lpcrcsize);
}

/*
 * Set the number of blocks at the beginning of the slice which have
 * been reserved for label operations.  This area will be write-protected
 * when accessed via the slice.
 *
 * For now just protect the label area proper.  Do not protect the
 * boot area.  Note partitions in 64 bit disklabels do not overlap
 * the disklabel or boot area.
 */
static void
l64_adjust_label_reserved(struct diskslices *ssp, int slice,
			  struct diskslice *sp)
{
	struct disklabel64 *lp = sp->ds_label.lab64;

	sp->ds_reserved = lp->d_bbase / ssp->dss_secsize;
}

struct disklabel_ops disklabel64_ops = {
	.labelsize = sizeof(struct disklabel64),
	.op_readdisklabel = l64_readdisklabel,
	.op_setdisklabel = l64_setdisklabel,
	.op_writedisklabel = l64_writedisklabel,
	.op_clone_label = l64_clone_label,
	.op_adjust_label_reserved = l64_adjust_label_reserved,
	.op_getpartbounds = l64_getpartbounds,
	.op_loadpartinfo = l64_loadpartinfo,
	.op_getnumparts = l64_getnumparts,
	.op_makevirginlabel = l64_makevirginlabel,
	.op_freedisklabel = l64_freedisklabel
};

