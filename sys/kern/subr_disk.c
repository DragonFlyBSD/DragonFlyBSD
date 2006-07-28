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
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * $DragonFly: src/sys/kern/subr_disk.c,v 1.25 2006/07/28 02:17:40 dillon Exp $
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
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>

static MALLOC_DEFINE(M_DISK, "disk", "disk data");

static d_open_t diskopen;
static d_close_t diskclose; 
static d_ioctl_t diskioctl;
static d_strategy_t diskstrategy;
static d_psize_t diskpsize;
static d_clone_t diskclone;
static d_dump_t diskdump;

static LIST_HEAD(, disk) disklist = LIST_HEAD_INITIALIZER(&disklist);

static struct dev_ops disk_ops = {
	{ "disk" },
	.d_open = diskopen,
	.d_close = diskclose,
	.d_read = physread,
	.d_write = physwrite,
	.d_ioctl = diskioctl,
	.d_strategy = diskstrategy,
	.d_dump = diskdump,
	.d_psize = diskpsize,
	.d_clone = diskclone
};

/*
 * Create a raw device for the dev_ops template (which is returned).  Also
 * create a slice and unit managed disk and overload the user visible
 * device space with it.
 *
 * NOTE: The returned raw device is NOT a slice and unit managed device.
 * It is an actual raw device representing the raw disk as specified by
 * the passed dev_ops.  The disk layer not only returns such a raw device,
 * it also uses it internally when passing (modified) commands through.
 */
dev_t
disk_create(int unit, struct disk *dp, int flags, struct dev_ops *raw_ops)
{
	dev_t rawdev;
	struct dev_ops *dev_ops;

	/*
	 * Create the raw backing device
	 */
	compile_dev_ops(raw_ops);
	rawdev = make_dev(raw_ops,
			    dkmakeminor(unit, WHOLE_DISK_SLICE, RAW_PART),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", raw_ops->head.name, unit);

	bzero(dp, sizeof(*dp));

	/*
	 * We install a custom cdevsw rather then the passed cdevsw,
	 * and save our disk structure in d_data so we can get at it easily
	 * without any complex cloning code.
	 */
	dev_ops = dev_ops_add_override(rawdev, &disk_ops,
				       dkunitmask(), dkmakeunit(unit));
	dev_ops->head.data = dp;

	dp->d_rawdev = rawdev;
	dp->d_raw_ops = raw_ops;
	dp->d_dev_ops = dev_ops;
	dp->d_cdev = make_dev(dev_ops, 
			    dkmakeminor(unit, WHOLE_DISK_SLICE, RAW_PART),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", dev_ops->head.name, unit);

	dp->d_dsflags = flags;
	LIST_INSERT_HEAD(&disklist, dp, d_list);
	return (dp->d_rawdev);
}

/*
 * This routine is called when an adapter detaches.  The higher level
 * managed disk device is destroyed while the lower level raw device is
 * released.
 */
void
disk_destroy(struct disk *disk)
{
	if (disk->d_dev_ops) {
	    dev_ops_remove(disk->d_dev_ops, dkunitmask(), 
			    dkmakeunit(dkunit(disk->d_cdev)));
	    LIST_REMOVE(disk, d_list);
	}
	if (disk->d_raw_ops) {
	    destroy_all_devs(disk->d_raw_ops, dkunitmask(), 
			    dkmakeunit(dkunit(disk->d_rawdev)));
	}
	bzero(disk, sizeof(*disk));
}

int
disk_dumpcheck(dev_t dev, u_int *count, u_int *blkno, u_int *secsize)
{
	struct disk *dp;
	struct disklabel *dl;
	u_int boff;

	dp = dev->si_disk;
	if (!dp)
		return (ENXIO);
	if (!dp->d_slice)
		return (ENXIO);
	dl = dsgetlabel(dev, dp->d_slice);
	if (!dl)
		return (ENXIO);
	*count = Maxmem * (PAGE_SIZE / dl->d_secsize);
	if (dumplo <= LABELSECTOR || 
	    (dumplo + *count > dl->d_partitions[dkpart(dev)].p_size))
		return (EINVAL);
	boff = dl->d_partitions[dkpart(dev)].p_offset +
	    dp->d_slice->dss_slices[dkslice(dev)].ds_offset;
	*blkno = boff + dumplo;
	*secsize = dl->d_secsize;
	return (0);
	
}

void 
disk_invalidate (struct disk *disk)
{
	if (disk->d_slice)
		dsgone(&disk->d_slice);
}

struct disk *
disk_enumerate(struct disk *disk)
{
	if (!disk)
		return (LIST_FIRST(&disklist));
	else
		return (LIST_NEXT(disk, d_list));
}

static 
int
sysctl_disks(SYSCTL_HANDLER_ARGS)
{
	struct disk *disk;
	int error, first;

	disk = NULL;
	first = 1;

	while ((disk = disk_enumerate(disk))) {
		if (!first) {
			error = SYSCTL_OUT(req, " ", 1);
			if (error)
				return error;
		} else {
			first = 0;
		}
		error = SYSCTL_OUT(req, disk->d_rawdev->si_name,
				   strlen(disk->d_rawdev->si_name));
		if (error)
			return error;
	}
	error = SYSCTL_OUT(req, "", 1);
	return error;
}
 
SYSCTL_PROC(_kern, OID_AUTO, disks, CTLTYPE_STRING | CTLFLAG_RD, 0, NULL, 
    sysctl_disks, "A", "names of available disks");

/*
 * Open a disk device or partition.
 */
static
int
diskopen(struct dev_open_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	/*
	 * dp can't be NULL here XXX.
	 */
	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);
	error = 0;

	/*
	 * Deal with open races
	 */
	while (dp->d_flags & DISKFLAG_LOCK) {
		dp->d_flags |= DISKFLAG_WANTED;
		error = tsleep(dp, PCATCH, "diskopen", hz);
		if (error)
			return (error);
	}
	dp->d_flags |= DISKFLAG_LOCK;

	/*
	 * Open the underlying raw device.
	 */
	if (!dsisopen(dp->d_slice)) {
#if 0
		if (!pdev->si_iosize_max)
			pdev->si_iosize_max = dev->si_iosize_max;
#endif
		error = dev_dopen(dp->d_rawdev, ap->a_oflags,
				  ap->a_devtype, ap->a_cred);
	}

	/*
	 * Inherit properties from the underlying device now that it is
	 * open.
	 */
	dev_dclone(dev);

	if (error)
		goto out;
	
	error = dsopen(dev, ap->a_devtype, dp->d_dsflags,
		       &dp->d_slice, &dp->d_label);

	if (!dsisopen(dp->d_slice)) 
		dev_dclose(dp->d_rawdev, ap->a_oflags, ap->a_devtype);
out:	
	dp->d_flags &= ~DISKFLAG_LOCK;
	if (dp->d_flags & DISKFLAG_WANTED) {
		dp->d_flags &= ~DISKFLAG_WANTED;
		wakeup(dp);
	}
	
	return(error);
}

/*
 * Close a disk device or partition
 */
static
int
diskclose(struct dev_close_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	error = 0;
	dp = dev->si_disk;

	dsclose(dev, ap->a_devtype, dp->d_slice);
	if (!dsisopen(dp->d_slice))
		error = dev_dclose(dp->d_rawdev, ap->a_fflag, ap->a_devtype);
	return (error);
}

/*
 * First execute the ioctl on the disk device, and if it isn't supported 
 * try running it on the backing device.
 */
static
int
diskioctl(struct dev_ioctl_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp;
	int error;

	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);
	error = dsioctl(dev, ap->a_cmd, ap->a_data, ap->a_fflag, &dp->d_slice);
	if (error == ENOIOCTL) {
		error = dev_dioctl(dp->d_rawdev, ap->a_cmd, ap->a_data,
				   ap->a_fflag, ap->a_cred);
	}
	return (error);
}

/*
 * Execute strategy routine
 */
static
int
diskstrategy(struct dev_strategy_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct disk *dp;

	dp = dev->si_disk;

	if (dp == NULL) {
		bio->bio_buf->b_error = ENXIO;
		bio->bio_buf->b_flags |= B_ERROR;
		biodone(bio);
		return(0);
	}
	KKASSERT(dev->si_disk == dp);

	/*
	 * The dscheck() function will also transform the slice relative
	 * block number i.e. bio->bio_offset into a block number that can be
	 * passed directly to the underlying raw device.  If dscheck()
	 * returns NULL it will have handled the bio for us (e.g. EOF
	 * or error due to being beyond the device size).
	 */
	if ((nbio = dscheck(dev, bio, dp->d_slice)) != NULL)
		dev_dstrategy(dp->d_rawdev, nbio);
	else
		biodone(bio);
	return(0);
}

/*
 * Return the partition size in ?blocks?
 */
static
int
diskpsize(struct dev_psize_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp;

	dp = dev->si_disk;
	if (dp == NULL)
		return(ENODEV);
	ap->a_result = dssize(dev, &dp->d_slice);
	return(0);
}

/*
 * When new device entries are instantiated, make sure they inherit our
 * si_disk structure and block and iosize limits from the raw device.
 *
 * This routine is always called synchronously in the context of the 
 * client.
 *
 * XXX The various io and block size constraints are not always initialized
 * properly by devices.
 */
static
int
diskclone(struct dev_clone_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp;

	dp = dev->si_ops->head.data;
	KKASSERT(dp != NULL);
	dev->si_disk = dp;
	dev->si_iosize_max = dp->d_rawdev->si_iosize_max;
	dev->si_bsize_phys = dp->d_rawdev->si_bsize_phys;
	dev->si_bsize_best = dp->d_rawdev->si_bsize_best;
	return(0);
}

int
diskdump(struct dev_dump_args *ap)
{
	dev_t dev = ap->a_head.a_dev;
	struct disk *dp = dev->si_ops->head.data;
	int error;

	error = disk_dumpcheck(dev, &ap->a_count, &ap->a_blkno, &ap->a_secsize);
	if (error == 0) {
		ap->a_head.a_dev = dp->d_rawdev;
		error = dev_doperate(&ap->a_head);
	}

	return(error);
}


SYSCTL_INT(_debug_sizeof, OID_AUTO, disklabel, CTLFLAG_RD, 
    0, sizeof(struct disklabel), "sizeof(struct disklabel)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, diskslices, CTLFLAG_RD, 
    0, sizeof(struct diskslices), "sizeof(struct diskslices)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, disk, CTLFLAG_RD, 
    0, sizeof(struct disk), "sizeof(struct disk)");


/*
 * Seek sort for disks.
 *
 * The bio_queue keep two queues, sorted in ascending block order.  The first
 * queue holds those requests which are positioned after the current block
 * (in the first request); the second, which starts at queue->switch_point,
 * holds requests which came in after their block number was passed.  Thus
 * we implement a one way scan, retracting after reaching the end of the drive
 * to the first request on the second queue, at which time it becomes the
 * first queue.
 *
 * A one-way scan is natural because of the way UNIX read-ahead blocks are
 * allocated.
 */
void
bioqdisksort(struct bio_queue_head *bioq, struct bio *bio)
{
	struct bio *bq;
	struct bio *bn;
	struct bio *be;
	
	be = TAILQ_LAST(&bioq->queue, bio_queue);
	/*
	 * If the queue is empty or we are an
	 * ordered transaction, then it's easy.
	 */
	if ((bq = bioq_first(bioq)) == NULL || 
	    (bio->bio_buf->b_flags & B_ORDERED) != 0) {
		bioq_insert_tail(bioq, bio);
		return;
	} else if (bioq->insert_point != NULL) {

		/*
		 * A certain portion of the list is
		 * "locked" to preserve ordering, so
		 * we can only insert after the insert
		 * point.
		 */
		bq = bioq->insert_point;
	} else {

		/*
		 * If we lie before the last removed (currently active)
		 * request, and are not inserting ourselves into the
		 * "locked" portion of the list, then we must add ourselves
		 * to the second request list.
		 */
		if (bio->bio_offset < bioq->last_offset) {
			bq = bioq->switch_point;
			/*
			 * If we are starting a new secondary list,
			 * then it's easy.
			 */
			if (bq == NULL) {
				bioq->switch_point = bio;
				bioq_insert_tail(bioq, bio);
				return;
			}
			/*
			 * If we lie ahead of the current switch point,
			 * insert us before the switch point and move
			 * the switch point.
			 */
			if (bio->bio_offset < bq->bio_offset) {
				bioq->switch_point = bio;
				TAILQ_INSERT_BEFORE(bq, bio, bio_act);
				return;
			}
		} else {
			if (bioq->switch_point != NULL)
				be = TAILQ_PREV(bioq->switch_point,
						bio_queue, bio_act);
			/*
			 * If we lie between last_offset and bq,
			 * insert before bq.
			 */
			if (bio->bio_offset < bq->bio_offset) {
				TAILQ_INSERT_BEFORE(bq, bio, bio_act);
				return;
			}
		}
	}

	/*
	 * Request is at/after our current position in the list.
	 * Optimize for sequential I/O by seeing if we go at the tail.
	 */
	if (bio->bio_offset > be->bio_offset) {
		TAILQ_INSERT_AFTER(&bioq->queue, be, bio, bio_act);
		return;
	}

	/* Otherwise, insertion sort */
	while ((bn = TAILQ_NEXT(bq, bio_act)) != NULL) {
		
		/*
		 * We want to go after the current request if it is the end
		 * of the first request list, or if the next request is a
		 * larger cylinder than our request.
		 */
		if (bn == bioq->switch_point
		 || bio->bio_offset < bn->bio_offset)
			break;
		bq = bn;
	}
	TAILQ_INSERT_AFTER(&bioq->queue, bq, bio, bio_act);
}


/*
 * Attempt to read a disk label from a device using the indicated strategy
 * routine.  The label must be partly set up before this: secpercyl, secsize
 * and anything required in the strategy routine (e.g., dummy bounds for the
 * partition containing the label) must be filled in before calling us.
 * Returns NULL on success and an error string on failure.
 */
char *
readdisklabel(dev_t dev, struct disklabel *lp)
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
setdisklabel(struct disklabel *olp, struct disklabel *nlp, u_long openmask)
{
	int i;
	struct partition *opp, *npp;

	/*
	 * Check it is actually a disklabel we are looking at.
	 */
	if (nlp->d_magic != DISKMAGIC || nlp->d_magic2 != DISKMAGIC ||
	    dkcksum(nlp) != 0)
		return (EINVAL);
	/*
	 * For each partition that we think is open,
	 */
	while ((i = ffs((long)openmask)) != 0) {
		i--;
		/*
	 	 * Check it is not changing....
	 	 */
		openmask &= ~(1 << i);
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
	}
 	nlp->d_checksum = 0;
 	nlp->d_checksum = dkcksum(nlp);
	*olp = *nlp;
	return (0);
}

/*
 * Write disk label back to device after modification.
 */
int
writedisklabel(dev_t dev, struct disklabel *lp)
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
	dev_dstrategy(dkmodpart(dev, RAW_PART), &bp->b_bio1);
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
			dev_dstrategy(dkmodpart(dev, RAW_PART), &bp->b_bio1);
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
 * Disk error is the preface to plaintive error messages
 * about failing disk transfers.  It prints messages of the form

hp0g: hard error reading fsbn 12345 of 12344-12347 (hp0 bn %d cn %d tn %d sn %d)

 * if the offset of the error in the transfer and a disk label
 * are both available.  blkdone should be -1 if the position of the error
 * is unknown; the disklabel pointer may be null from drivers that have not
 * been converted to use them.  The message is printed with printf
 * if pri is LOG_PRINTF, otherwise it uses log at the specified priority.
 * The message should be completed (with at least a newline) with printf
 * or addlog, respectively.  There is no trailing space.
 */
void
diskerr(struct bio *bio, dev_t dev, const char *what, int pri, 
	int donecnt, struct disklabel *lp)
{
	struct buf *bp = bio->bio_buf;
	int unit = dkunit(dev);
	int slice = dkslice(dev);
	int part = dkpart(dev);
	char partname[2];
	char *sname;

	sname = dsname(dev, unit, slice, part, partname);
	printf("%s%s: %s %sing ", sname, partname, what,
	      (bp->b_cmd == BUF_CMD_READ) ? "read" : "writ");
	printf("offset %012llx for %d", bio->bio_offset, bp->b_bcount);
	if (donecnt)
		printf(" (%d bytes completed)", donecnt);
}

