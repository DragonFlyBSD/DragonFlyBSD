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
 * $DragonFly: src/sys/kern/subr_disk.c,v 1.13 2004/07/16 05:51:10 dillon Exp $
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

static d_strategy_t diskstrategy;
static d_open_t diskopen;
static d_close_t diskclose; 
static d_ioctl_t diskioctl;
static d_psize_t diskpsize;
static d_clone_t diskclone;
static int disk_putport(lwkt_port_t port, lwkt_msg_t msg);

static LIST_HEAD(, disk) disklist = LIST_HEAD_INITIALIZER(&disklist);

/*
 * Create a slice and unit managed disk.
 *
 * Our port layer will be responsible for assigning pblkno and handling
 * high level partition operations, then forwarding the requests to the
 * raw device.
 *
 * The raw device (based on rawsw) is returned to the caller, NOT the
 * slice and unit managed cdev.  The caller typically sets various
 * driver parameters and IO limits on the returned rawdev which we must
 * inherit when our managed device is opened.
 */
dev_t
disk_create(int unit, struct disk *dp, int flags, struct cdevsw *rawsw)
{
	dev_t rawdev;
	struct cdevsw *devsw;

	/*
	 * Create the raw backing device
	 */
	compile_devsw(rawsw);
	rawdev = make_dev(rawsw, dkmakeminor(unit, WHOLE_DISK_SLICE, RAW_PART),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", rawsw->d_name, unit);

	/*
	 * Initialize our intercept port
	 */
	bzero(dp, sizeof(*dp));
	lwkt_initport(&dp->d_port, NULL);
	dp->d_port.mp_putport = disk_putport;
	dp->d_rawsw = rawsw;

	/*
	 * We install a custom cdevsw rather then the passed cdevsw,
	 * and save our disk structure in d_data so we can get at it easily
	 * without any complex cloning code.
	 */
	devsw = cdevsw_add_override(rawdev, dkunitmask(), dkmakeunit(unit));
	devsw->d_port = &dp->d_port;
	devsw->d_data = dp;
	devsw->d_clone = diskclone;
	dp->d_devsw = devsw;
	dp->d_rawdev = rawdev;
	dp->d_cdev = make_dev(devsw, 
			    dkmakeminor(unit, WHOLE_DISK_SLICE, RAW_PART),
			    UID_ROOT, GID_OPERATOR, 0640,
			    "%s%d", devsw->d_name, unit);

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
	if (disk->d_devsw) {
	    cdevsw_remove(disk->d_devsw, dkunitmask(), dkunit(disk->d_cdev));
	    LIST_REMOVE(disk, d_list);
	}
	if (disk->d_rawsw)
	    destroy_all_dev(disk->d_rawsw, dkunitmask(), dkunit(disk->d_rawdev));
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
		error = SYSCTL_OUT(req, disk->d_rawdev->si_name, strlen(disk->d_rawdev->si_name));
		if (error)
			return error;
	}
	error = SYSCTL_OUT(req, "", 1);
	return error;
}
 
SYSCTL_PROC(_kern, OID_AUTO, disks, CTLTYPE_STRING | CTLFLAG_RD, 0, NULL, 
    sysctl_disks, "A", "names of available disks");

/*
 * The port intercept functions
 */
static
int
disk_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	struct disk *disk = (struct disk *)port;
	cdevallmsg_t msg = (cdevallmsg_t)lmsg;
	int error;

	switch(msg->am_lmsg.ms_cmd.cm_op) {
	case CDEV_CMD_OPEN:
		error = diskopen(
			    msg->am_open.msg.dev,
			    msg->am_open.oflags,
			    msg->am_open.devtype,
			    msg->am_open.td);
		break;
	case CDEV_CMD_CLOSE:
		error = diskclose(
			    msg->am_close.msg.dev,
			    msg->am_close.fflag,
			    msg->am_close.devtype,
			    msg->am_close.td);
		break;
	case CDEV_CMD_IOCTL:
		error = diskioctl(
			    msg->am_ioctl.msg.dev,
			    msg->am_ioctl.cmd,
			    msg->am_ioctl.data,
			    msg->am_ioctl.fflag,
			    msg->am_ioctl.td);
		break;
	case CDEV_CMD_STRATEGY:
		diskstrategy(msg->am_strategy.bp);
		error = 0;
		break;
	case CDEV_CMD_PSIZE:
		msg->am_psize.result = diskpsize(msg->am_psize.msg.dev);
		error = 0;      /* XXX */
		break;
	case CDEV_CMD_READ:
		error = physio(msg->am_read.msg.dev, 
				msg->am_read.uio, msg->am_read.ioflag);
		break;
	case CDEV_CMD_WRITE:
		error = physio(msg->am_write.msg.dev, 
				msg->am_write.uio, msg->am_write.ioflag);
		break;
	case CDEV_CMD_POLL:
	case CDEV_CMD_KQFILTER:
		error = ENODEV;
	case CDEV_CMD_MMAP:
		error = -1;
		break;
	case CDEV_CMD_DUMP:
		error = disk_dumpcheck(msg->am_dump.msg.dev,
				&msg->am_dump.count,
				&msg->am_dump.blkno,
				&msg->am_dump.secsize);
		if (error == 0) {
			msg->am_dump.msg.dev = disk->d_rawdev;
			error = lwkt_forwardmsg(disk->d_rawdev->si_port,
						&msg->am_dump.msg.msg);
			printf("error2 %d\n", error);
		}
		break;
	default:
		error = ENOTSUP;
		break;
	}
	return(error);
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
diskclone(dev_t dev)
{
	struct disk *dp;

	dp = dev->si_devsw->d_data;
	KKASSERT(dp != NULL);
	dev->si_disk = dp;
	dev->si_iosize_max = dp->d_rawdev->si_iosize_max;
	dev->si_bsize_phys = dp->d_rawdev->si_bsize_phys;
	dev->si_bsize_best = dp->d_rawdev->si_bsize_best;
	return(0);
}

/*
 * Open a disk device or partition.
 */
static
int
diskopen(dev_t dev, int oflags, int devtype, struct thread *td)
{
	struct disk *dp;
	int error;

	/*
	 * dp can't be NULL here XXX.
	 */
	error = 0;
	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);

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
		error = dev_dopen(dp->d_rawdev, oflags, devtype, td);
	}

	/*
	 * Inherit properties from the underlying device now that it is
	 * open.
	 */
	diskclone(dev);

	if (error)
		goto out;
	
	error = dsopen(dev, devtype, dp->d_dsflags, &dp->d_slice, &dp->d_label);

	if (!dsisopen(dp->d_slice)) 
		dev_dclose(dp->d_rawdev, oflags, devtype, td);
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
diskclose(dev_t dev, int fflag, int devtype, struct thread *td)
{
	struct disk *dp;
	int error;

	error = 0;
	dp = dev->si_disk;

	dsclose(dev, devtype, dp->d_slice);
	if (!dsisopen(dp->d_slice))
		error = dev_dclose(dp->d_rawdev, fflag, devtype, td);
	return (error);
}

/*
 * Execute strategy routine
 */
static
void
diskstrategy(struct buf *bp)
{
	struct disk *dp;

	dp = bp->b_dev->si_disk;

	if (dp == NULL) {
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
		return;
	}
	KKASSERT(bp->b_dev->si_disk == dp);

	if (dscheck(bp, dp->d_slice) <= 0) {
		biodone(bp);
		return;
	}
	bp->b_dev = dp->d_rawdev;
	dev_dstrategy(dp->d_rawdev, bp);
}

/*
 * First execute the ioctl on the disk device, and if it isn't supported 
 * try running it on the backing device.
 */
static
int
diskioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct disk *dp;
	int error;

	dp = dev->si_disk;
	if (dp == NULL)
		return (ENXIO);

	error = dsioctl(dev, cmd, data, fflag, &dp->d_slice);
	if (error == ENOIOCTL)
		error = dev_dioctl(dp->d_rawdev, cmd, data, fflag, td);
	return (error);
}

/*
 *
 */
static
int
diskpsize(dev_t dev)
{
	struct disk *dp;

	dp = dev->si_disk;
	if (dp == NULL)
		return (-1);
	return(dssize(dev, &dp->d_slice));
#if 0
	if (dp != dev->si_disk) {
		dev->si_drv1 = pdev->si_drv1;
		dev->si_drv2 = pdev->si_drv2;
		/* XXX: don't set bp->b_dev->si_disk (?) */
	}
#endif
}

SYSCTL_DECL(_debug_sizeof);

SYSCTL_INT(_debug_sizeof, OID_AUTO, disklabel, CTLFLAG_RD, 
    0, sizeof(struct disklabel), "sizeof(struct disklabel)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, diskslices, CTLFLAG_RD, 
    0, sizeof(struct diskslices), "sizeof(struct diskslices)");

SYSCTL_INT(_debug_sizeof, OID_AUTO, disk, CTLFLAG_RD, 
    0, sizeof(struct disk), "sizeof(struct disk)");


/*
 * Seek sort for disks.
 *
 * The buf_queue keep two queues, sorted in ascending block order.  The first
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
bufqdisksort(struct buf_queue_head *bufq, struct buf *bp)
{
	struct buf *bq;
	struct buf *bn;
	struct buf *be;
	
	be = TAILQ_LAST(&bufq->queue, buf_queue);
	/*
	 * If the queue is empty or we are an
	 * ordered transaction, then it's easy.
	 */
	if ((bq = bufq_first(bufq)) == NULL || 
	    (bp->b_flags & B_ORDERED) != 0) {
		bufq_insert_tail(bufq, bp);
		return;
	} else if (bufq->insert_point != NULL) {

		/*
		 * A certain portion of the list is
		 * "locked" to preserve ordering, so
		 * we can only insert after the insert
		 * point.
		 */
		bq = bufq->insert_point;
	} else {

		/*
		 * If we lie before the last removed (currently active)
		 * request, and are not inserting ourselves into the
		 * "locked" portion of the list, then we must add ourselves
		 * to the second request list.
		 */
		if (bp->b_pblkno < bufq->last_pblkno) {

			bq = bufq->switch_point;
			/*
			 * If we are starting a new secondary list,
			 * then it's easy.
			 */
			if (bq == NULL) {
				bufq->switch_point = bp;
				bufq_insert_tail(bufq, bp);
				return;
			}
			/*
			 * If we lie ahead of the current switch point,
			 * insert us before the switch point and move
			 * the switch point.
			 */
			if (bp->b_pblkno < bq->b_pblkno) {
				bufq->switch_point = bp;
				TAILQ_INSERT_BEFORE(bq, bp, b_act);
				return;
			}
		} else {
			if (bufq->switch_point != NULL)
				be = TAILQ_PREV(bufq->switch_point,
						buf_queue, b_act);
			/*
			 * If we lie between last_pblkno and bq,
			 * insert before bq.
			 */
			if (bp->b_pblkno < bq->b_pblkno) {
				TAILQ_INSERT_BEFORE(bq, bp, b_act);
				return;
			}
		}
	}

	/*
	 * Request is at/after our current position in the list.
	 * Optimize for sequential I/O by seeing if we go at the tail.
	 */
	if (bp->b_pblkno > be->b_pblkno) {
		TAILQ_INSERT_AFTER(&bufq->queue, be, bp, b_act);
		return;
	}

	/* Otherwise, insertion sort */
	while ((bn = TAILQ_NEXT(bq, b_act)) != NULL) {
		
		/*
		 * We want to go after the current request if it is the end
		 * of the first request list, or if the next request is a
		 * larger cylinder than our request.
		 */
		if (bn == bufq->switch_point
		 || bp->b_pblkno < bn->b_pblkno)
			break;
		bq = bn;
	}
	TAILQ_INSERT_AFTER(&bufq->queue, bq, bp, b_act);
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
	bp->b_dev = dev;
	bp->b_blkno = LABELSECTOR * ((int)lp->d_secsize/DEV_BSIZE);
	bp->b_bcount = lp->d_secsize;
	bp->b_flags &= ~B_INVAL;
	bp->b_flags |= B_READ;
	BUF_STRATEGY(bp, 1);
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
	bp->b_dev = dkmodpart(dev, RAW_PART);
	bp->b_blkno = LABELSECTOR * ((int)lp->d_secsize/DEV_BSIZE);
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
	bp->b_flags |= B_READ;
	BUF_STRATEGY(bp, 1);
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
			bp->b_flags &= ~(B_DONE | B_READ);
			bp->b_flags |= B_WRITE;
			bp->b_dev = dkmodpart(dev, RAW_PART);
#ifdef __alpha__
			alpha_fix_srm_checksum(bp);
#endif
			BUF_STRATEGY(bp, 1);
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
	bp->b_flags |= B_WRITE;
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
diskerr(struct buf *bp, dev_t dev, char *what, int pri, 
	int blkdone, struct disklabel *lp)
{
	int unit = dkunit(dev);
	int slice = dkslice(dev);
	int part = dkpart(dev);
	char partname[2];
	char *sname;
	daddr_t sn;

	sname = dsname(dev, unit, slice, part, partname);
	printf("%s%s: %s %sing fsbn ", sname, partname, what,
	      bp->b_flags & B_READ ? "read" : "writ");
	sn = bp->b_blkno;
	if (bp->b_bcount <= DEV_BSIZE) {
		printf("%ld", (long)sn);
	} else {
		if (blkdone >= 0) {
			sn += blkdone;
			printf("%ld of ", (long)sn);
		}
		printf("%ld-%ld", (long)bp->b_blkno,
		    (long)(bp->b_blkno + (bp->b_bcount - 1) / DEV_BSIZE));
	}
	if (lp && (blkdone >= 0 || bp->b_bcount <= lp->d_secsize)) {
#ifdef tahoe
		sn *= DEV_BSIZE / lp->d_secsize;		/* XXX */
#endif
		sn += lp->d_partitions[part].p_offset;
		/*
		 * XXX should add slice offset and not print the slice,
		 * but we don't know the slice pointer.
		 * XXX should print bp->b_pblkno so that this will work
		 * independent of slices, labels and bad sector remapping,
		 * but some drivers don't set bp->b_pblkno.
		 */
		printf(" (%s bn %ld; cn %ld", sname, (long)sn,
		    (long)(sn / lp->d_secpercyl));
		sn %= (long)lp->d_secpercyl;
		printf(" tn %ld sn %ld)", (long)(sn / lp->d_nsectors),
		    (long)(sn % lp->d_nsectors));
	}
}
