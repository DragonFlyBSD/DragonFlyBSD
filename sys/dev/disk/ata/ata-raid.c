/*-
 * Copyright (c) 2000,2001,2002 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/ata-raid.c,v 1.3.2.19 2003/01/30 07:19:59 sos Exp $
 */

#include "opt_ata.h"
#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/ata.h> 
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/devicestat.h>
#include <sys/cons.h>
#include <sys/rman.h>
#include <sys/proc.h>
#include <sys/buf2.h>
#include <sys/thread2.h>

#include "ata-all.h"
#include "ata-disk.h"
#include "ata-raid.h"

/* device structures */
static d_open_t		aropen;
static d_strategy_t	arstrategy;

static struct dev_ops ar_ops = {
	{ "ar", 0, D_DISK },
	.d_open =	aropen,
	.d_close =	nullclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_strategy =	arstrategy,
};  

/* prototypes */
static void ar_attach_raid(struct ar_softc *, int);
static void ar_done(struct bio *);
static void ar_config_changed(struct ar_softc *, int);
static int ar_rebuild(struct ar_softc *);
static int ar_highpoint_read_conf(struct ad_softc *, struct ar_softc **);
static int ar_highpoint_write_conf(struct ar_softc *);
static int ar_promise_read_conf(struct ad_softc *, struct ar_softc **, int);
static int ar_promise_write_conf(struct ar_softc *);
static int ar_rw(struct ad_softc *, u_int32_t, int, caddr_t, int);
static struct ata_device *ar_locate_disk(int);

/* internal vars */
static struct ar_softc **ar_table = NULL;
static MALLOC_DEFINE(M_AR, "AR driver", "ATA RAID driver");

int
ata_raiddisk_attach(struct ad_softc *adp)
{
    struct ar_softc *rdp;
    int array, disk;

    if (ar_table) {
	for (array = 0; array < MAX_ARRAYS; array++) {
	    if (!(rdp = ar_table[array]) || !rdp->flags)
		continue;
   
	    for (disk = 0; disk < rdp->total_disks; disk++) {
		if ((rdp->disks[disk].flags & AR_DF_ASSIGNED) &&
		    rdp->disks[disk].device == adp->device) {
		    ata_prtdev(rdp->disks[disk].device,
			       "inserted into ar%d disk%d as spare\n",
			       array, disk);
		    rdp->disks[disk].flags |= (AR_DF_PRESENT | AR_DF_SPARE);
		    AD_SOFTC(rdp->disks[disk])->flags = AD_F_RAID_SUBDISK;
		    ar_config_changed(rdp, 1);
		    return 1;
		}
	    }
	}
    }

    if (!ar_table) {
	ar_table = kmalloc(sizeof(struct ar_soft *) * MAX_ARRAYS,
			  M_AR, M_WAITOK | M_ZERO);
    }

    switch(adp->device->channel->chiptype) {
    case 0x4d33105a: case 0x4d38105a: case 0x4d30105a:
    case 0x0d30105a: case 0x4d68105a: case 0x6268105a:
    case 0x4d69105a: case 0x5275105a: case 0x6269105a:
    case 0x7275105a:
	/* test RAID bit in PCI reg XXX */
	return (ar_promise_read_conf(adp, ar_table, 0));

    case 0x00041103: case 0x00051103: case 0x00081103:
	return (ar_highpoint_read_conf(adp, ar_table));

    default:
	return (ar_promise_read_conf(adp, ar_table, 1));
    }
    return 0;
}

int
ata_raiddisk_detach(struct ad_softc *adp)
{
    struct ar_softc *rdp;
    int array, disk;

    if (ar_table) {
	for (array = 0; array < MAX_ARRAYS; array++) {
	    if (!(rdp = ar_table[array]) || !rdp->flags)
		continue; 
	    for (disk = 0; disk < rdp->total_disks; disk++) {
		if (rdp->disks[disk].device == adp->device) {
		    ata_prtdev(rdp->disks[disk].device,
			       "deleted from ar%d disk%d\n", array, disk);
		    rdp->disks[disk].flags &= ~(AR_DF_PRESENT | AR_DF_ONLINE);
		    AD_SOFTC(rdp->disks[disk])->flags &= ~AD_F_RAID_SUBDISK;
		    ar_config_changed(rdp, 1);
		    return 1;
		}
	    }
	}
    }
    return 0;
}

void
ata_raid_attach()
{
    struct ar_softc *rdp;
    int array;

    if (!ar_table)
	return;

    for (array = 0; array < MAX_ARRAYS; array++) {
	if (!(rdp = ar_table[array]) || !rdp->flags)
	    continue;
	ar_attach_raid(rdp, 0);
    }
}

static void
ar_attach_raid(struct ar_softc *rdp, int update)
{
	struct disk_info info;
    cdev_t dev;
    int disk;

    ar_config_changed(rdp, update);
    dev = disk_create(rdp->lun, &rdp->disk, &ar_ops);
    dev->si_drv1 = rdp;
    dev->si_iosize_max = 256 * DEV_BSIZE;
    rdp->dev = dev;

	/*
	 * Set disk info, as it appears that all needed data is available already.
	 * Setting the disk info will also cause the probing to start.
	 */
    bzero(&info, sizeof(info));
    info.d_media_blksize = DEV_BSIZE;		/* mandatory */
    info.d_media_blocks = rdp->total_sectors;

    info.d_secpertrack = rdp->sectors;		/* optional */
    info.d_nheads = rdp->heads;
    info.d_ncylinders = rdp->cylinders;
    info.d_secpercyl = rdp->sectors * rdp->heads;

    kprintf("ar%d: %lluMB <ATA ", rdp->lun, (unsigned long long)
	   (rdp->total_sectors / ((1024L * 1024L) / DEV_BSIZE)));
    switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
    case AR_F_RAID0:
	kprintf("RAID0 "); break;
    case AR_F_RAID1:
	kprintf("RAID1 "); break;
    case AR_F_SPAN:
	kprintf("SPAN "); break;
    case (AR_F_RAID0 | AR_F_RAID1):
	kprintf("RAID0+1 "); break;
    default:
	kprintf("unknown 0x%x> ", rdp->flags);
	return;
    }
    kprintf("array> [%d/%d/%d] status: ",
	   rdp->cylinders, rdp->heads, rdp->sectors);
    switch (rdp->flags & (AR_F_DEGRADED | AR_F_READY)) {
    case AR_F_READY:
	kprintf("READY");
	break;
    case (AR_F_DEGRADED | AR_F_READY):
	kprintf("DEGRADED");
	break;
    default:
	kprintf("BROKEN");
	break;
    }
    kprintf(" subdisks:\n");
    for (disk = 0; disk < rdp->total_disks; disk++) {
	if (rdp->disks[disk].flags & AR_DF_PRESENT) {
	    if (rdp->disks[disk].flags & AR_DF_ONLINE)
		kprintf(" %d READY ", disk);
	    else if (rdp->disks[disk].flags & AR_DF_SPARE)
		kprintf(" %d SPARE ", disk);
	    else
		kprintf(" %d FREE  ", disk);
	    ad_print(AD_SOFTC(rdp->disks[disk]));
	    kprintf("         ");
	    ata_enclosure_print(AD_SOFTC(rdp->disks[disk])->device);
	}
	else if (rdp->disks[disk].flags & AR_DF_ASSIGNED)
	    kprintf(" %d DOWN\n", disk);
	else
	    kprintf(" %d INVALID no RAID config info on this disk\n", disk);
    }
    disk_setdiskinfo(&rdp->disk, &info);
}

int
ata_raid_create(struct raid_setup *setup)
{
    struct ata_device *atadev;
    struct ar_softc *rdp;
    int array, disk;
    int ctlr = 0, disk_size = 0, total_disks = 0;

    if (!ar_table) {
	ar_table = kmalloc(sizeof(struct ar_soft *) * MAX_ARRAYS,
			  M_AR, M_WAITOK | M_ZERO);
    }
    for (array = 0; array < MAX_ARRAYS; array++) {
	if (!ar_table[array])
	    break;
    }
    if (array >= MAX_ARRAYS)
	return ENOSPC;

    rdp = kmalloc(sizeof(struct ar_softc), M_AR, M_WAITOK | M_ZERO);

    for (disk = 0; disk < setup->total_disks; disk++) {
	if ((atadev = ar_locate_disk(setup->disks[disk]))) {
	    rdp->disks[disk].device = atadev;
	    if (AD_SOFTC(rdp->disks[disk])->flags & AD_F_RAID_SUBDISK) {
		setup->disks[disk] = -1;
		kfree(rdp, M_AR);
		return EBUSY;
	    }

	    switch (rdp->disks[disk].device->channel->chiptype & 0xffff) {
	    case 0x1103:
		ctlr |= AR_F_HIGHPOINT_RAID;
		rdp->disks[disk].disk_sectors =
		    AD_SOFTC(rdp->disks[disk])->total_secs;
		break;

	    default:
		ctlr |= AR_F_FREEBSD_RAID;
		/* FALLTHROUGH */

	    case 0x105a:	
		ctlr |= AR_F_PROMISE_RAID;
		rdp->disks[disk].disk_sectors =
		    PR_LBA(AD_SOFTC(rdp->disks[disk]));
		break;
	    }
	    if ((rdp->flags & (AR_F_PROMISE_RAID|AR_F_HIGHPOINT_RAID)) &&
		(rdp->flags & (AR_F_PROMISE_RAID|AR_F_HIGHPOINT_RAID)) !=
		 (ctlr & (AR_F_PROMISE_RAID|AR_F_HIGHPOINT_RAID))) {
		kfree(rdp, M_AR);
		return EXDEV;
	    }
	    else
		rdp->flags |= ctlr;
	    
	    if (disk_size)
	    	disk_size = min(rdp->disks[disk].disk_sectors, disk_size);
	    else
		disk_size = rdp->disks[disk].disk_sectors;
	    rdp->disks[disk].flags = 
		(AR_DF_PRESENT | AR_DF_ASSIGNED | AR_DF_ONLINE);

	    total_disks++;
	}
	else {
	    setup->disks[disk] = -1;
	    kfree(rdp, M_AR);
	    return ENXIO;
	}
    }
    if (!total_disks) {
	kfree(rdp, M_AR);
	return ENODEV;
    }

    switch (setup->type) {
    case 1:
	rdp->flags |= AR_F_RAID0;
	break;
    case 2:
	rdp->flags |= AR_F_RAID1;
	if (total_disks != 2) {
	    kfree(rdp, M_AR);
	    return EPERM;
	}
	break;
    case 3:
	rdp->flags |= (AR_F_RAID0 | AR_F_RAID1);
	if (total_disks % 2 != 0) {
	    kfree(rdp, M_AR);
	    return EPERM;
	}
	break;
    case 4:
	rdp->flags |= AR_F_SPAN;
	break;
    }

    for (disk = 0; disk < total_disks; disk++)
	AD_SOFTC(rdp->disks[disk])->flags = AD_F_RAID_SUBDISK;

    rdp->lun = array;
    if (rdp->flags & AR_F_RAID0) {
	int bit = 0;

	while (setup->interleave >>= 1)
	    bit++;
	if (rdp->flags & AR_F_PROMISE_RAID)
	    rdp->interleave = min(max(2, 1 << bit), 2048);
	if (rdp->flags & AR_F_HIGHPOINT_RAID)
	    rdp->interleave = min(max(32, 1 << bit), 128);
    }
    rdp->total_disks = total_disks;
    rdp->width = total_disks / ((rdp->flags & AR_F_RAID1) ? 2 : 1);	
    rdp->total_sectors = disk_size * rdp->width;
    rdp->heads = 255;
    rdp->sectors = 63;
    rdp->cylinders = rdp->total_sectors / (255 * 63);
    if (rdp->flags & AR_F_PROMISE_RAID) {
	rdp->offset = 0;
	rdp->reserved = 63;
    }
    if (rdp->flags & AR_F_HIGHPOINT_RAID) {
	rdp->offset = HPT_LBA + 1;
	rdp->reserved = HPT_LBA + 1;
    }
    rdp->lock_start = rdp->lock_end = 0xffffffff;
    rdp->flags |= AR_F_READY;

    ar_table[array] = rdp;
    ar_attach_raid(rdp, 1);
    setup->unit = array;
    return 0;
}

int
ata_raid_delete(int array)
{
    struct ar_softc *rdp;
    int disk;

    if (!ar_table) {
	kprintf("ar: no memory for ATA raid array\n");
	return 0;
    }
    if (!(rdp = ar_table[array]))
	return ENXIO;
    
    rdp->flags &= ~AR_F_READY;
    for (disk = 0; disk < rdp->total_disks; disk++) {
	if ((rdp->disks[disk].flags&AR_DF_PRESENT) && rdp->disks[disk].device) {
	    AD_SOFTC(rdp->disks[disk])->flags &= ~AD_F_RAID_SUBDISK;
	    ata_enclosure_leds(rdp->disks[disk].device, ATA_LED_GREEN);
	    rdp->disks[disk].flags = 0;
	}
    }
    if (rdp->flags & AR_F_PROMISE_RAID)
	ar_promise_write_conf(rdp);
    else
	ar_highpoint_write_conf(rdp);
    disk_invalidate(&rdp->disk);
    disk_destroy(&rdp->disk);
    kfree(rdp, M_AR);
    ar_table[array] = NULL;
    return 0;
}

int
ata_raid_status(int array, struct raid_status *status)
{
    struct ar_softc *rdp;
    int i;

    if (!ar_table || !(rdp = ar_table[array]))
	return ENXIO;

    switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
    case AR_F_RAID0:
	status->type = AR_RAID0;
	break;
    case AR_F_RAID1:
	status->type = AR_RAID1;
	break;
    case AR_F_RAID0 | AR_F_RAID1:
	status->type = AR_RAID0 | AR_RAID1;
	break;
    case AR_F_SPAN:
	status->type = AR_SPAN;
	break;
    }
    status->total_disks = rdp->total_disks;
    for (i = 0; i < rdp->total_disks; i++ ) {
	if ((rdp->disks[i].flags & AR_DF_PRESENT) && rdp->disks[i].device)
	    status->disks[i] = AD_SOFTC(rdp->disks[i])->lun;
	else
	    status->disks[i] = -1;
    }
    status->interleave = rdp->interleave;
    status->status = 0;
    if (rdp->flags & AR_F_READY)
	status->status |= AR_READY;
    if (rdp->flags & AR_F_DEGRADED)
	status->status |= AR_DEGRADED;
    if (rdp->flags & AR_F_REBUILDING) {
	status->status |= AR_REBUILDING;
	status->progress = 100*rdp->lock_start/(rdp->total_sectors/rdp->width);
    }
    return 0;
}

int
ata_raid_rebuild(int array)
{
    struct ar_softc *rdp;

    if (!ar_table || !(rdp = ar_table[array]))
	return ENXIO;
    if (rdp->flags & AR_F_REBUILDING)
	return EBUSY;
    /* create process here XXX SOS */
    return ar_rebuild(rdp);
}

static int
aropen(struct dev_open_args *ap)
{
#if 0
    struct ar_softc *rdp = ap->a_head.a_dev->si_drv1;
    struct disk_info info;

    bzero(&info, sizeof(info));
    info.d_media_blksize = DEV_BSIZE;		/* mandatory */
    info.d_media_blocks = rdp->total_sectors;

    info.d_secpertrack = rdp->sectors;		/* optional */
    info.d_nheads = rdp->heads;
    info.d_ncylinders = rdp->cylinders;
    info.d_secpercyl = rdp->sectors * rdp->heads;
    disk_setdiskinfo(&rdp->disk, &info);
    return 0;
#endif
}

static int
arstrategy(struct dev_strategy_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct bio *bio = ap->a_bio;
    struct buf *bp = bio->bio_buf;
    struct ar_softc *rdp = dev->si_drv1;
    int blkno, count, chunk, lba, lbs, tmplba;
    int orig_blkno;
    int buf1_blkno;
    int drv = 0, change = 0;
    caddr_t data;

    if (!(rdp->flags & AR_F_READY)) {
	bp->b_flags |= B_ERROR;
	bp->b_error = EIO;
	biodone(bio);
	return(0);
    }

    KKASSERT((bio->bio_offset & DEV_BMASK) == 0);

    bp->b_resid = bp->b_bcount;
    blkno = (int)(bio->bio_offset >> DEV_BSHIFT);
    orig_blkno = blkno;
    data = bp->b_data;

    for (count = howmany(bp->b_bcount, DEV_BSIZE); count > 0; 
	 count -= chunk, blkno += chunk, data += (chunk * DEV_BSIZE)) {
	struct ar_buf *buf1, *buf2;

	switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
	case AR_F_SPAN:
	    lba = blkno;
	    while (lba >= AD_SOFTC(rdp->disks[drv])->total_secs-rdp->reserved)
		lba -= AD_SOFTC(rdp->disks[drv++])->total_secs-rdp->reserved;
	    chunk = min(AD_SOFTC(rdp->disks[drv])->total_secs-rdp->reserved-lba,
			count);
	    break;
	
	case AR_F_RAID0:
	case AR_F_RAID0 | AR_F_RAID1:
	    tmplba = blkno / rdp->interleave;
	    chunk = blkno % rdp->interleave;
	    if (tmplba == rdp->total_sectors / rdp->interleave) {
		lbs = (rdp->total_sectors-(tmplba*rdp->interleave))/rdp->width;
		drv = chunk / lbs;
		lba = ((tmplba/rdp->width)*rdp->interleave) + chunk%lbs;
		chunk = min(count, lbs);
	    }
	    else {
		drv = tmplba % rdp->width;
		lba = ((tmplba / rdp->width) * rdp->interleave) + chunk;
		chunk = min(count, rdp->interleave - chunk);
	    }
	    break;

	case AR_F_RAID1:
	    drv = 0;
	    lba = blkno;
	    chunk = count;
	    break;

	default:
	    kprintf("ar%d: unknown array type in arstrategy\n", rdp->lun);
	    bp->b_flags |= B_ERROR;
	    bp->b_error = EIO;
	    biodone(bio);
	    return(0);
	}

	buf1 = kmalloc(sizeof(struct ar_buf), M_AR, M_INTWAIT | M_ZERO);
	initbufbio(&buf1->bp);
	BUF_LOCK(&buf1->bp, LK_EXCLUSIVE);
	buf1->bp.b_bio1.bio_offset = (off_t)lba << DEV_BSHIFT;
	if ((buf1->drive = drv) > 0)
	    buf1->bp.b_bio1.bio_offset += (off_t)rdp->offset << DEV_BSHIFT;
	buf1->bp.b_bio1.bio_caller_info1.ptr = (void *)rdp;
	buf1->bp.b_bcount = chunk * DEV_BSIZE;
	buf1->bp.b_data = data;
	buf1->bp.b_flags = bp->b_flags | B_PAGING;
	buf1->bp.b_cmd = bp->b_cmd;
	buf1->bp.b_bio1.bio_done = ar_done;
	buf1->org = bio;
	buf1_blkno = (int)(buf1->bp.b_bio1.bio_offset >> DEV_BSHIFT);

	switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
	case AR_F_SPAN:
	case AR_F_RAID0:
	    if ((rdp->disks[buf1->drive].flags &
		 (AR_DF_PRESENT|AR_DF_ONLINE))==(AR_DF_PRESENT|AR_DF_ONLINE) &&
		!AD_SOFTC(rdp->disks[buf1->drive])->dev) {
		rdp->disks[buf1->drive].flags &= ~AR_DF_ONLINE;
		ar_config_changed(rdp, 1);
		BUF_UNLOCK(&buf1->bp);
		uninitbufbio(&buf1->bp);
		kfree(buf1, M_AR);
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		biodone(bio);
		return(0);
	    }
	    dev_dstrategy(AD_SOFTC(rdp->disks[buf1->drive])->dev,
			  &buf1->bp.b_bio1);
	    break;

	case AR_F_RAID1:
	case AR_F_RAID0 | AR_F_RAID1:
	    if ((rdp->flags & AR_F_REBUILDING) && bp->b_cmd != BUF_CMD_READ) {
		if ((orig_blkno >= rdp->lock_start &&
		     orig_blkno < rdp->lock_end) ||
		    ((orig_blkno + chunk) > rdp->lock_start &&
		     (orig_blkno + chunk) <= rdp->lock_end)) {
		    tsleep(rdp, 0, "arwait", 0);
		}
	    }
	    if ((rdp->disks[buf1->drive].flags &
		 (AR_DF_PRESENT|AR_DF_ONLINE))==(AR_DF_PRESENT|AR_DF_ONLINE) &&
		!AD_SOFTC(rdp->disks[buf1->drive])->dev) {
		rdp->disks[buf1->drive].flags &= ~AR_DF_ONLINE;
		change = 1;
	    }
	    if ((rdp->disks[buf1->drive + rdp->width].flags &
		 (AR_DF_PRESENT|AR_DF_ONLINE))==(AR_DF_PRESENT|AR_DF_ONLINE) &&
		!AD_SOFTC(rdp->disks[buf1->drive + rdp->width])->dev) {
		rdp->disks[buf1->drive + rdp->width].flags &= ~AR_DF_ONLINE;
		change = 1;
	    }
	    if (change)
		ar_config_changed(rdp, 1);
		
	    if (!(rdp->flags & AR_F_READY)) {
		BUF_UNLOCK(&buf1->bp);
		uninitbufbio(&buf1->bp);
		kfree(buf1, M_AR);
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		biodone(bio);
		return(0);
	    }
	    if (bp->b_cmd == BUF_CMD_READ) {
		if ((buf1_blkno <
		     (rdp->disks[buf1->drive].last_lba - AR_PROXIMITY) ||
		     buf1_blkno >
		     (rdp->disks[buf1->drive].last_lba + AR_PROXIMITY) ||
		     !(rdp->disks[buf1->drive].flags & AR_DF_ONLINE)) &&
		     (rdp->disks[buf1->drive+rdp->width].flags & AR_DF_ONLINE))
			buf1->drive = buf1->drive + rdp->width;
	    } else {
		if ((rdp->disks[buf1->drive+rdp->width].flags & AR_DF_ONLINE) ||
		    ((rdp->flags & AR_F_REBUILDING) &&
		     (rdp->disks[buf1->drive+rdp->width].flags & AR_DF_SPARE) &&
		     buf1_blkno < rdp->lock_start)) {
		    if ((rdp->disks[buf1->drive].flags & AR_DF_ONLINE) ||
			((rdp->flags & AR_F_REBUILDING) &&
			 (rdp->disks[buf1->drive].flags & AR_DF_SPARE) &&
			 buf1_blkno < rdp->lock_start)) {
			buf2 = kmalloc(sizeof(struct ar_buf), M_AR, M_INTWAIT);
			bcopy(buf1, buf2, sizeof(struct ar_buf));
			initbufbio(&buf2->bp);
			BUF_LOCK(&buf2->bp, LK_EXCLUSIVE);
			buf2->bp.b_bio1.bio_offset = buf1->bp.b_bio1.bio_offset;
			buf1->mirror = buf2;
			buf2->mirror = buf1;
			buf2->drive = buf1->drive + rdp->width;
			dev_dstrategy(AD_SOFTC(rdp->disks[buf2->drive])->dev,
				      &buf2->bp.b_bio1);
			rdp->disks[buf2->drive].last_lba = buf1_blkno + chunk;
			/* XXX free buf2? */
		    }
		    else
			buf1->drive = buf1->drive + rdp->width;
		}
	    }
	    dev_dstrategy(AD_SOFTC(rdp->disks[buf1->drive])->dev,
			  &buf1->bp.b_bio1);
	    rdp->disks[buf1->drive].last_lba = buf1_blkno + chunk;
	    break;

	default:
	    kprintf("ar%d: unknown array type in arstrategy\n", rdp->lun);
	}
    }
    return(0);
}

static void
ar_done(struct bio *bio)
{
    struct ar_softc *rdp = (struct ar_softc *)bio->bio_caller_info1.ptr;
    struct ar_buf *buf = (struct ar_buf *)bio->bio_buf;

    get_mplock();

    switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
    case AR_F_SPAN:
    case AR_F_RAID0:
	if (buf->bp.b_flags & B_ERROR) {
	    rdp->disks[buf->drive].flags &= ~AR_DF_ONLINE;
	    ar_config_changed(rdp, 1);
	    buf->org->bio_buf->b_flags |= B_ERROR;
	    buf->org->bio_buf->b_error = EIO;
	    biodone(buf->org);
	}
	else {
	    buf->org->bio_buf->b_resid -= buf->bp.b_bcount;
	    if (buf->org->bio_buf->b_resid == 0)
		biodone(buf->org);
	}
	break;

    case AR_F_RAID1:
    case AR_F_RAID0 | AR_F_RAID1:
	if (buf->bp.b_flags & B_ERROR) {
	    rdp->disks[buf->drive].flags &= ~AR_DF_ONLINE;
	    ar_config_changed(rdp, 1);
	    if (rdp->flags & AR_F_READY) {
		if (buf->bp.b_cmd == BUF_CMD_READ) {
		    if (buf->drive < rdp->width)
			buf->drive = buf->drive + rdp->width;
		    else
			buf->drive = buf->drive - rdp->width;
		    buf->bp.b_flags = buf->org->bio_buf->b_flags | B_PAGING;
		    buf->bp.b_error = 0;
		    dev_dstrategy(AD_SOFTC(rdp->disks[buf->drive])->dev,
				  &buf->bp.b_bio1);
		    rel_mplock();
		    return;
		}
		else {
		    if (buf->flags & AB_F_DONE) {
			buf->org->bio_buf->b_resid -= buf->bp.b_bcount;
			if (buf->org->bio_buf->b_resid == 0)
			    biodone(buf->org);
		    }
		    else
			buf->mirror->flags |= AB_F_DONE;
		}
	    }
	    else {
		buf->org->bio_buf->b_flags |= B_ERROR;
		buf->org->bio_buf->b_error = EIO;
		biodone(buf->org);
	    }
	} 
	else {
	    if (buf->bp.b_cmd != BUF_CMD_READ) {
		if (buf->mirror && !(buf->flags & AB_F_DONE)){
		    buf->mirror->flags |= AB_F_DONE;
		    break;
		}
	    }
	    buf->org->bio_buf->b_resid -= buf->bp.b_bcount;
	    if (buf->org->bio_buf->b_resid == 0)
		biodone(buf->org);
	}
	break;
	
    default:
	kprintf("ar%d: unknown array type in ar_done\n", rdp->lun);
    }
    BUF_UNLOCK(&buf->bp);
    uninitbufbio(&buf->bp);
    kfree(buf, M_AR);
    rel_mplock();
}

static void
ar_config_changed(struct ar_softc *rdp, int writeback)
{
    int disk, flags;

    flags = rdp->flags;
    rdp->flags |= AR_F_READY;
    rdp->flags &= ~AR_F_DEGRADED;

    for (disk = 0; disk < rdp->total_disks; disk++)
	if (!(rdp->disks[disk].flags & AR_DF_PRESENT))
	    rdp->disks[disk].flags &= ~AR_DF_ONLINE;

    for (disk = 0; disk < rdp->total_disks; disk++) {
	switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
	case AR_F_SPAN:
	case AR_F_RAID0:
	    if (!(rdp->disks[disk].flags & AR_DF_ONLINE)) {
		rdp->flags &= ~AR_F_READY;
		kprintf("ar%d: ERROR - array broken\n", rdp->lun);
	    }
	    break;

	case AR_F_RAID1:
	case AR_F_RAID0 | AR_F_RAID1:
	    if (disk < rdp->width) {
		if (!(rdp->disks[disk].flags & AR_DF_ONLINE) &&
		    !(rdp->disks[disk + rdp->width].flags & AR_DF_ONLINE)) {
		    rdp->flags &= ~AR_F_READY;
		    kprintf("ar%d: ERROR - array broken\n", rdp->lun);
		}
		else if (((rdp->disks[disk].flags & AR_DF_ONLINE) &&
			  !(rdp->disks
			    [disk + rdp->width].flags & AR_DF_ONLINE))||
			 (!(rdp->disks[disk].flags & AR_DF_ONLINE) &&
			  (rdp->disks
			   [disk + rdp->width].flags & AR_DF_ONLINE))) {
		    rdp->flags |= AR_F_DEGRADED;
		    if (!(flags & AR_F_DEGRADED))
			kprintf("ar%d: WARNING - mirror lost\n", rdp->lun);
		}
	    }
	    break;
	}
	if ((rdp->disks[disk].flags&AR_DF_PRESENT) && rdp->disks[disk].device) {
	    if (rdp->disks[disk].flags & AR_DF_ONLINE)
		ata_enclosure_leds(rdp->disks[disk].device, ATA_LED_GREEN);
	    else
		ata_enclosure_leds(rdp->disks[disk].device, ATA_LED_RED);
	}
    }
    if (writeback) {
	if (rdp->flags & AR_F_PROMISE_RAID)
	    ar_promise_write_conf(rdp);
	if (rdp->flags & AR_F_HIGHPOINT_RAID)
	    ar_highpoint_write_conf(rdp);
    }
}

static int
ar_rebuild(struct ar_softc *rdp)
{
    int disk, count = 0, error = 0;
    caddr_t buffer;

    if ((rdp->flags & (AR_F_READY|AR_F_DEGRADED)) != (AR_F_READY|AR_F_DEGRADED))
	return EEXIST;

    for (disk = 0; disk < rdp->total_disks; disk++) {
	if (((rdp->disks[disk].flags&(AR_DF_PRESENT|AR_DF_ONLINE|AR_DF_SPARE))==
	     (AR_DF_PRESENT | AR_DF_SPARE)) && rdp->disks[disk].device) {
	    if (AD_SOFTC(rdp->disks[disk])->total_secs <
		rdp->disks[disk].disk_sectors) {
		ata_prtdev(rdp->disks[disk].device,
			   "disk capacity too small for this RAID config\n");
#if 0
		rdp->disks[disk].flags &= ~AR_DF_SPARE;
		AD_SOFTC(rdp->disks[disk])->flags &= ~AD_F_RAID_SUBDISK;
#endif
		continue;
	    }
	    ata_enclosure_leds(rdp->disks[disk].device, ATA_LED_ORANGE);
	    count++;
	}
    }
    if (!count)
	return ENODEV;

    /* setup start conditions */
    crit_enter();
    rdp->lock_start = 0;
    rdp->lock_end = rdp->lock_start + 256;
    rdp->flags |= AR_F_REBUILDING;
    crit_exit();
    buffer = kmalloc(256 * DEV_BSIZE, M_AR, M_WAITOK | M_ZERO);

    /* now go copy entire disk(s) */
    while (rdp->lock_end < (rdp->total_sectors / rdp->width)) {
	int size = min(256, (rdp->total_sectors / rdp->width) - rdp->lock_end);

	for (disk = 0; disk < rdp->width; disk++) {
	    struct ad_softc *adp;

	    if (((rdp->disks[disk].flags & AR_DF_ONLINE) &&
		 (rdp->disks[disk + rdp->width].flags & AR_DF_ONLINE)) ||
		((rdp->disks[disk].flags & AR_DF_ONLINE) && 
		 !(rdp->disks[disk + rdp->width].flags & AR_DF_SPARE)) ||
		((rdp->disks[disk + rdp->width].flags & AR_DF_ONLINE) &&
		 !(rdp->disks[disk].flags & AR_DF_SPARE)))
		continue;

	    if (rdp->disks[disk].flags & AR_DF_ONLINE)
		adp = AD_SOFTC(rdp->disks[disk]);
	    else
		adp = AD_SOFTC(rdp->disks[disk + rdp->width]);
	    if ((error = ar_rw(adp, rdp->lock_start,
			       size * DEV_BSIZE, buffer, AR_READ | AR_WAIT)))
		break;

	    if (rdp->disks[disk].flags & AR_DF_ONLINE)
		adp = AD_SOFTC(rdp->disks[disk + rdp->width]);
	    else
		adp = AD_SOFTC(rdp->disks[disk]);
	    if ((error = ar_rw(adp, rdp->lock_start,
			       size * DEV_BSIZE, buffer, AR_WRITE | AR_WAIT)))
		break;
	}
	if (error) {
	    wakeup(rdp);
	    kfree(buffer, M_AR);
	    return error;
	}
	crit_enter();
	rdp->lock_start = rdp->lock_end;
	rdp->lock_end = rdp->lock_start + size;
	crit_exit();
	wakeup(rdp);
    }
    kfree(buffer, M_AR);
    for (disk = 0; disk < rdp->total_disks; disk++) {
	if ((rdp->disks[disk].flags&(AR_DF_PRESENT|AR_DF_ONLINE|AR_DF_SPARE))==
	    (AR_DF_PRESENT | AR_DF_SPARE)) {
	    rdp->disks[disk].flags &= ~AR_DF_SPARE;
	    rdp->disks[disk].flags |= (AR_DF_ASSIGNED | AR_DF_ONLINE);
	}
    }
    crit_enter();
    rdp->lock_start = 0xffffffff;
    rdp->lock_end = 0xffffffff;
    rdp->flags &= ~AR_F_REBUILDING;
    crit_exit();
    ar_config_changed(rdp, 1);
    return 0;
}

static int
ar_highpoint_read_conf(struct ad_softc *adp, struct ar_softc **raidp)
{
    struct highpoint_raid_conf *info;
    struct ar_softc *raid = NULL;
    int array, disk_number = 0, retval = 0;

    info = kmalloc(sizeof(struct highpoint_raid_conf), M_AR, M_INTWAIT|M_ZERO);

    if (ar_rw(adp, HPT_LBA, sizeof(struct highpoint_raid_conf),
	      (caddr_t)info, AR_READ | AR_WAIT)) {
	if (bootverbose)
	    kprintf("ar: HighPoint read conf failed\n");
	goto highpoint_out;
    }

    /* check if this is a HighPoint RAID struct */
    if (info->magic != HPT_MAGIC_OK && info->magic != HPT_MAGIC_BAD) {
	if (bootverbose)
	    kprintf("ar: HighPoint check1 failed\n");
	goto highpoint_out;
    }

    /* is this disk defined, or an old leftover/spare ? */
    if (!info->magic_0) {
	if (bootverbose)
	    kprintf("ar: HighPoint check2 failed\n");
	goto highpoint_out;
    }

    /* now convert HighPoint config info into our generic form */
    for (array = 0; array < MAX_ARRAYS; array++) {
	if (!raidp[array]) {
	    raidp[array] = kmalloc(sizeof(struct ar_softc), M_AR,
					 M_INTWAIT | M_ZERO);
	}
	raid = raidp[array];
	if (raid->flags & AR_F_PROMISE_RAID)
	    continue;

	switch (info->type) {
	case HPT_T_RAID0:
	    if ((info->order & (HPT_O_RAID0|HPT_O_OK))==(HPT_O_RAID0|HPT_O_OK))
		goto highpoint_raid1;
	    if (info->order & (HPT_O_RAID0 | HPT_O_RAID1))
		goto highpoint_raid01;
	    if (raid->magic_0 && raid->magic_0 != info->magic_0)
		continue;
	    raid->magic_0 = info->magic_0;
	    raid->flags |= AR_F_RAID0;
	    raid->interleave = 1 << info->stripe_shift;
	    disk_number = info->disk_number;
	    if (!(info->order & HPT_O_OK))
		info->magic = 0;	/* mark bad */
	    break;

	case HPT_T_RAID1:
highpoint_raid1:
	    if (raid->magic_0 && raid->magic_0 != info->magic_0)
		continue;
	    raid->magic_0 = info->magic_0;
	    raid->flags |= AR_F_RAID1;
	    disk_number = (info->disk_number > 0);
	    break;

	case HPT_T_RAID01_RAID0:
highpoint_raid01:
	    if (info->order & HPT_O_RAID0) {
		if ((raid->magic_0 && raid->magic_0 != info->magic_0) ||
		    (raid->magic_1 && raid->magic_1 != info->magic_1))
		    continue;
		raid->magic_0 = info->magic_0;
		raid->magic_1 = info->magic_1;
		raid->flags |= (AR_F_RAID0 | AR_F_RAID1);
		raid->interleave = 1 << info->stripe_shift;
		disk_number = info->disk_number;
	    }
	    else {
		if (raid->magic_1 && raid->magic_1 != info->magic_1)
		    continue;
		raid->magic_1 = info->magic_1;
		raid->flags |= (AR_F_RAID0 | AR_F_RAID1);
		raid->interleave = 1 << info->stripe_shift;
		disk_number = info->disk_number + info->array_width;
		if (!(info->order & HPT_O_RAID1))
		    info->magic = 0;	/* mark bad */
	    }
	    break;

	case HPT_T_SPAN:
	    if (raid->magic_0 && raid->magic_0 != info->magic_0)
		continue;
	    raid->magic_0 = info->magic_0;
	    raid->flags |= AR_F_SPAN;
	    disk_number = info->disk_number;
	    break;

	default:
	    kprintf("ar%d: HighPoint unknown RAID type 0x%02x\n",
		   array, info->type);
	    goto highpoint_out;
	}

	raid->flags |= AR_F_HIGHPOINT_RAID;
	raid->disks[disk_number].device = adp->device;
	raid->disks[disk_number].flags = (AR_DF_PRESENT | AR_DF_ASSIGNED);
	raid->lun = array;
	if (info->magic == HPT_MAGIC_OK) {
	    raid->disks[disk_number].flags |= AR_DF_ONLINE;
	    raid->flags |= AR_F_READY;
	    raid->width = info->array_width;
	    raid->heads = 255;
	    raid->sectors = 63;
	    raid->cylinders = info->total_sectors / (63 * 255);
	    raid->total_sectors = info->total_sectors;
	    raid->offset = HPT_LBA + 1;
	    raid->reserved = HPT_LBA + 1;
	    raid->lock_start = raid->lock_end = info->rebuild_lba;
	    raid->disks[disk_number].disk_sectors =
		info->total_sectors / info->array_width;
	}
	else
	    raid->disks[disk_number].flags &= ~ AR_DF_ONLINE;

	if ((raid->flags & AR_F_RAID0) && (raid->total_disks < raid->width))
	    raid->total_disks = raid->width;
	if (disk_number >= raid->total_disks)
	    raid->total_disks = disk_number + 1;
	retval = 1;
	break;
    }
highpoint_out:
    kfree(info, M_AR);
    return retval;
}

static int
ar_highpoint_write_conf(struct ar_softc *rdp)
{
    struct highpoint_raid_conf *config;
    struct timeval timestamp;
    int disk;

    microtime(&timestamp);
    rdp->magic_0 = timestamp.tv_sec + 2;
    rdp->magic_1 = timestamp.tv_sec;
   
    for (disk = 0; disk < rdp->total_disks; disk++) {
	config = kmalloc(sizeof(struct highpoint_raid_conf),
		     M_AR, M_INTWAIT | M_ZERO);
	if ((rdp->disks[disk].flags & (AR_DF_PRESENT | AR_DF_ONLINE)) ==
	    (AR_DF_PRESENT | AR_DF_ONLINE))
	    config->magic = HPT_MAGIC_OK;
	if (rdp->disks[disk].flags & AR_DF_ASSIGNED) {
	    config->magic_0 = rdp->magic_0;
	    strcpy(config->name_1, "FreeBSD");
	}
	config->disk_number = disk;

	switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
	case AR_F_RAID0:
	    config->type = HPT_T_RAID0;
	    strcpy(config->name_2, "RAID 0");
	    if (rdp->disks[disk].flags & AR_DF_ONLINE)
		config->order = HPT_O_OK;
	    break;

	case AR_F_RAID1:
	    config->type = HPT_T_RAID0;	/* bogus but old HPT BIOS need it */
	    strcpy(config->name_2, "RAID 1");
	    config->disk_number = (disk < rdp->width) ? disk : disk + 5;
	    config->order = HPT_O_RAID0 | HPT_O_OK;
	    break;

	case AR_F_RAID0 | AR_F_RAID1:
	    config->type = HPT_T_RAID01_RAID0;
	    strcpy(config->name_2, "RAID 0+1");
	    if (rdp->disks[disk].flags & AR_DF_ONLINE) {
		if (disk < rdp->width) {
		    config->order = (HPT_O_RAID0 | HPT_O_RAID1);
		    config->magic_0 = rdp->magic_0 - 1;
		}
		else {
		    config->order = HPT_O_RAID1;
		    config->disk_number -= rdp->width;
		}
	    }
	    else
		config->magic_0 = rdp->magic_0 - 1;
	    config->magic_1 = rdp->magic_1;
	    break;

	case AR_F_SPAN:
	    config->type = HPT_T_SPAN;
	    strcpy(config->name_2, "SPAN");
	    break;
	}

	config->array_width = rdp->width;
	config->stripe_shift = (rdp->width > 1) ? (ffs(rdp->interleave)-1) : 0;
	config->total_sectors = rdp->total_sectors;
	config->rebuild_lba = rdp->lock_start;

	if ((rdp->disks[disk].device && rdp->disks[disk].device->driver) &&
	    !(rdp->disks[disk].device->flags & ATA_D_DETACHING)) {

	    if (ar_rw(AD_SOFTC(rdp->disks[disk]), HPT_LBA,
		      sizeof(struct highpoint_raid_conf),
		      (caddr_t)config, AR_WRITE)) {
		kprintf("ar%d: Highpoint write conf failed\n", rdp->lun);
		return -1;
	    }
	}
    }
    return 0;
}

static int
ar_promise_read_conf(struct ad_softc *adp, struct ar_softc **raidp, int local)
{
    struct promise_raid_conf *info;
    struct ar_softc *raid;
    u_int32_t magic, cksum, *ckptr;
    int array, count, disk, disksum = 0, retval = 0; 

    info = kmalloc(sizeof(struct promise_raid_conf), M_AR, M_INTWAIT | M_ZERO);

    if (ar_rw(adp, PR_LBA(adp), sizeof(struct promise_raid_conf),
	      (caddr_t)info, AR_READ | AR_WAIT)) {
	if (bootverbose)
	    kprintf("ar: %s read conf failed\n", local ? "FreeBSD" : "Promise");
	goto promise_out;
    }

    /* check if this is a Promise RAID struct (or our local one) */
    if (local) {
	if (strncmp(info->promise_id, ATA_MAGIC, sizeof(ATA_MAGIC))) {
	    if (bootverbose)
		kprintf("ar: FreeBSD check1 failed\n");
	    goto promise_out;
	}
    }
    else {
	if (strncmp(info->promise_id, PR_MAGIC, sizeof(PR_MAGIC))) {
	    if (bootverbose)
		kprintf("ar: Promise check1 failed\n");
	    goto promise_out;
	}
    }

    /* check if the checksum is OK */
    for (cksum = 0, ckptr = (int32_t *)info, count = 0; count < 511; count++)
	cksum += *ckptr++;
    if (cksum != *ckptr) {  
	if (bootverbose)
	    kprintf("ar: %s check2 failed\n", local ? "FreeBSD" : "Promise");	     
	goto promise_out;
    }

    /* now convert Promise config info into our generic form */
    if (info->raid.integrity != PR_I_VALID) {
	if (bootverbose)
	    kprintf("ar: %s check3 failed\n", local ? "FreeBSD" : "Promise");	     
	goto promise_out;
    }

    for (array = 0; array < MAX_ARRAYS; array++) {
	if (!raidp[array]) {
	    raidp[array] = kmalloc(sizeof(struct ar_softc), M_AR,
					M_INTWAIT | M_ZERO);
	}
	raid = raidp[array];
	if (raid->flags & AR_F_HIGHPOINT_RAID)
	    continue;

	magic = (adp->device->channel->chiptype >> 16) |
		(info->raid.array_number << 16);

	if ((raid->flags & AR_F_PROMISE_RAID) && magic != raid->magic_0)
	    continue;

	/* update our knowledge about the array config based on generation */
	if (!info->raid.generation || info->raid.generation > raid->generation){
	    raid->generation = info->raid.generation;
	    raid->flags = AR_F_PROMISE_RAID;
    	    if (local)
		raid->flags |= AR_F_FREEBSD_RAID;
	    raid->magic_0 = magic;
	    raid->lun = array;
	    if ((info->raid.status &
		 (PR_S_VALID | PR_S_ONLINE | PR_S_INITED | PR_S_READY)) ==
		(PR_S_VALID | PR_S_ONLINE | PR_S_INITED | PR_S_READY)) {
		raid->flags |= AR_F_READY;
		if (info->raid.status & PR_S_DEGRADED)
		    raid->flags |= AR_F_DEGRADED;
	    }
	    else
		raid->flags &= ~AR_F_READY;

	    switch (info->raid.type) {
	    case PR_T_RAID0:
		raid->flags |= AR_F_RAID0;
		break;

	    case PR_T_RAID1:
		raid->flags |= AR_F_RAID1;
		if (info->raid.array_width > 1)
		    raid->flags |= AR_F_RAID0;
		break;

	    case PR_T_SPAN:
		raid->flags |= AR_F_SPAN;
		break;

	    default:
		kprintf("ar%d: %s unknown RAID type 0x%02x\n",
		       array, local ? "FreeBSD" : "Promise", info->raid.type);
		goto promise_out;
	    }
	    raid->interleave = 1 << info->raid.stripe_shift;
	    raid->width = info->raid.array_width;
	    raid->total_disks = info->raid.total_disks;
	    raid->heads = info->raid.heads + 1;
	    raid->sectors = info->raid.sectors;
	    raid->cylinders = info->raid.cylinders + 1;
	    raid->total_sectors = info->raid.total_sectors;
	    raid->offset = 0;
	    raid->reserved = 63;
	    raid->lock_start = raid->lock_end = info->raid.rebuild_lba;

	    /* convert disk flags to our internal types */
	    for (disk = 0; disk < info->raid.total_disks; disk++) {
		raid->disks[disk].flags = 0;
		disksum += info->raid.disk[disk].flags;
		if (info->raid.disk[disk].flags & PR_F_ONLINE)
		    raid->disks[disk].flags |= AR_DF_ONLINE;
		if (info->raid.disk[disk].flags & PR_F_ASSIGNED)
		    raid->disks[disk].flags |= AR_DF_ASSIGNED;
		if (info->raid.disk[disk].flags & PR_F_SPARE) {
		    raid->disks[disk].flags &= ~AR_DF_ONLINE;
		    raid->disks[disk].flags |= AR_DF_SPARE;
		}
		if (info->raid.disk[disk].flags & (PR_F_REDIR | PR_F_DOWN))
		    raid->disks[disk].flags &= ~AR_DF_ONLINE;
	    }
	    if (!disksum) {
		kfree(raidp[array], M_AR);
		raidp[array] = NULL;
		goto promise_out;
	    }
	}
	if (raid->disks[info->raid.disk_number].flags && adp->device) {
	    raid->disks[info->raid.disk_number].device = adp->device;
	    raid->disks[info->raid.disk_number].flags |= AR_DF_PRESENT;
	    raid->disks[info->raid.disk_number].disk_sectors =
		info->raid.total_sectors / info->raid.array_width;
		/*info->raid.disk_sectors;*/
	    retval = 1;
	}
	break;
    }
promise_out:
    kfree(info, M_AR);
    return retval;
}

static int
ar_promise_write_conf(struct ar_softc *rdp)
{
    struct promise_raid_conf *config;
    struct timeval timestamp;
    u_int32_t *ckptr;
    int count, disk, drive;
    int local = rdp->flags & AR_F_FREEBSD_RAID;

    rdp->generation++;
    microtime(&timestamp);

    for (disk = 0; disk < rdp->total_disks; disk++) {
	config = kmalloc(sizeof(struct promise_raid_conf), M_AR, M_INTWAIT);
	for (count = 0; count < sizeof(struct promise_raid_conf); count++)
	    *(((u_int8_t *)config) + count) = 255 - (count % 256);

	if (local)
	    bcopy(ATA_MAGIC, config->promise_id, sizeof(ATA_MAGIC));
	else
	    bcopy(PR_MAGIC, config->promise_id, sizeof(PR_MAGIC));
	config->dummy_0 = 0x00020000;
	config->magic_0 = PR_MAGIC0(rdp->disks[disk]) | timestamp.tv_sec;
	config->magic_1 = timestamp.tv_sec >> 16;
	config->magic_2 = timestamp.tv_sec;
	config->raid.integrity = PR_I_VALID;

	config->raid.disk_number = disk;
	if ((rdp->disks[disk].flags&AR_DF_PRESENT) && rdp->disks[disk].device) {
	    config->raid.channel = rdp->disks[disk].device->channel->unit;
	    config->raid.device = (rdp->disks[disk].device->unit != 0);
	    if (AD_SOFTC(rdp->disks[disk])->dev)
		config->raid.disk_sectors = PR_LBA(AD_SOFTC(rdp->disks[disk]));
	    /*config->raid.disk_offset*/
	}
	config->raid.magic_0 = config->magic_0;
	config->raid.rebuild_lba = rdp->lock_start;
	config->raid.generation = rdp->generation;

	if (rdp->flags & AR_F_READY) {
	    config->raid.flags = (PR_F_VALID | PR_F_ASSIGNED | PR_F_ONLINE);
	    config->raid.status = 
		(PR_S_VALID | PR_S_ONLINE | PR_S_INITED | PR_S_READY);
	    if (rdp->flags & AR_F_DEGRADED)
		config->raid.status |= PR_S_DEGRADED;
	    else
		config->raid.status |= PR_S_FUNCTIONAL;
	}
	else {
	    config->raid.status = 0;
	    config->raid.flags = PR_F_DOWN;
	}

	switch (rdp->flags & (AR_F_RAID0 | AR_F_RAID1 | AR_F_SPAN)) {
	case AR_F_RAID0:
	    config->raid.type = PR_T_RAID0;
	    break;
	case AR_F_RAID1:
	    config->raid.type = PR_T_RAID1;
	    break;
	case AR_F_RAID0 | AR_F_RAID1:
	    config->raid.type = PR_T_RAID1;
	    break;
	case AR_F_SPAN:
	    config->raid.type = PR_T_SPAN;
	    break;
	}

	config->raid.total_disks = rdp->total_disks;
	config->raid.stripe_shift = ffs(rdp->interleave) - 1;
	config->raid.array_width = rdp->width;
	config->raid.array_number = rdp->lun;
	config->raid.total_sectors = rdp->total_sectors;
	config->raid.cylinders = rdp->cylinders - 1;
	config->raid.heads = rdp->heads - 1;
	config->raid.sectors = rdp->sectors;
	config->raid.magic_1 = (u_int64_t)config->magic_2<<16 | config->magic_1;

	bzero(&config->raid.disk, 8 * 12);
	for (drive = 0; drive < rdp->total_disks; drive++) {
	    config->raid.disk[drive].flags = 0;
	    if (rdp->disks[drive].flags & AR_DF_PRESENT)
		config->raid.disk[drive].flags |= PR_F_VALID;
	    if (rdp->disks[drive].flags & AR_DF_ASSIGNED)
		config->raid.disk[drive].flags |= PR_F_ASSIGNED;
	    if (rdp->disks[drive].flags & AR_DF_ONLINE)
		config->raid.disk[drive].flags |= PR_F_ONLINE;
	    else
		if (rdp->disks[drive].flags & AR_DF_PRESENT)
		    config->raid.disk[drive].flags = (PR_F_REDIR | PR_F_DOWN);
	    if (rdp->disks[drive].flags & AR_DF_SPARE)
		config->raid.disk[drive].flags |= PR_F_SPARE;
	    config->raid.disk[drive].dummy_0 = 0x0;
	    if (rdp->disks[drive].device) {
		config->raid.disk[drive].channel =
		    rdp->disks[drive].device->channel->unit;
		config->raid.disk[drive].device =
		    (rdp->disks[drive].device->unit != 0);
	    }
	    config->raid.disk[drive].magic_0 =
		PR_MAGIC0(rdp->disks[drive]) | timestamp.tv_sec;
	}

	config->checksum = 0;
	for (ckptr = (int32_t *)config, count = 0; count < 511; count++)
	    config->checksum += *ckptr++;
	if (rdp->disks[disk].device && rdp->disks[disk].device->driver &&
	    !(rdp->disks[disk].device->flags & ATA_D_DETACHING)) {
	    if (ar_rw(AD_SOFTC(rdp->disks[disk]),
		      PR_LBA(AD_SOFTC(rdp->disks[disk])),
		      sizeof(struct promise_raid_conf),
		      (caddr_t)config, AR_WRITE)) {
		kprintf("ar%d: %s write conf failed\n",
		       rdp->lun, local ? "FreeBSD" : "Promise");
		return -1;
	    }
	}
    }
    return 0;
}

static void
ar_rw_done(struct bio *bio)
{
    struct buf *bp = bio->bio_buf;

    BUF_UNLOCK(bp);
    uninitbufbio(bp);
    kfree(bp->b_data, M_AR);
    kfree(bp, M_AR);
}

static int
ar_rw(struct ad_softc *adp, u_int32_t lba, int count, caddr_t data, int flags)
{
    struct buf *bp;
    int retry = 0, error = 0;

    bp = kmalloc(sizeof(struct buf), M_AR, M_INTWAIT|M_ZERO);
    initbufbio(bp);
    BUF_LOCK(bp, LK_EXCLUSIVE);
    bp->b_data = data;
    bp->b_bio1.bio_offset = (off_t)lba << DEV_BSHIFT;
    bp->b_bcount = count;
    if (flags & AR_WAIT) {
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bio1.bio_done = biodone_sync;
    } else {
	bp->b_bio1.bio_done = ar_rw_done;
    }
    if (flags & AR_READ)
	bp->b_cmd = BUF_CMD_READ;
    if (flags & AR_WRITE)
	bp->b_cmd = BUF_CMD_WRITE;
    KKASSERT(bp->b_cmd != BUF_CMD_DONE);

    dev_dstrategy(adp->dev, &bp->b_bio1);

    if (flags & AR_WAIT) {
	while (retry++ < (15*hz/10))
	    error = biowait_timeout(&bp->b_bio1, "arrw", 10);
	if (!error && (bp->b_flags & B_ERROR))
	    error = bp->b_error;
	if (error == EWOULDBLOCK) {
	    bp->b_bio1.bio_done = ar_rw_done;
	} else {
	    BUF_UNLOCK(bp);
	    uninitbufbio(bp);
	    kfree(bp, M_AR);
	}
    }
    return error;
}

static struct ata_device *
ar_locate_disk(int diskno)
{
    struct ata_channel *ch;
    int ctlr;

    for (ctlr = 0; ctlr < devclass_get_maxunit(ata_devclass); ctlr++) {
	if (!(ch = devclass_get_softc(ata_devclass, ctlr)))
	    continue;
	if (ch->devices & ATA_ATA_MASTER)
	    if (ch->device[MASTER].driver &&
		((struct ad_softc *)(ch->device[MASTER].driver))->lun == diskno)
		return &ch->device[MASTER];
	if (ch->devices & ATA_ATA_SLAVE)
	    if (ch->device[SLAVE].driver &&
		((struct ad_softc *)(ch->device[SLAVE].driver))->lun == diskno)
		return &ch->device[SLAVE];
    }
    return NULL;
}
