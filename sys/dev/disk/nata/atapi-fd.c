/*-
 * Copyright (c) 1998 - 2006 Søren Schmidt <sos@FreeBSD.org>
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
 * $FreeBSD: src/sys/dev/ata/atapi-fd.c,v 1.109 2006/03/30 05:29:57 marcel Exp $
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/nata.h>
#include <sys/systm.h>
#include <sys/udev.h>

#include "ata-all.h"
#include "atapi-fd.h"
#include "ata_if.h"

/* device structure */
static	d_open_t	afd_open;
static	d_close_t	afd_close;
static	d_ioctl_t	afd_ioctl;
static	d_strategy_t	afd_strategy;
static struct dev_ops afd_ops = {
	{ "afd", 118, D_DISK | D_TRACKCLOSE },
	.d_open =	afd_open,
	.d_close =	afd_close,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	afd_ioctl,
	.d_strategy =	afd_strategy,
};

/* prototypes */
static int afd_sense(device_t);
static void afd_describe(device_t);
static void afd_done(struct ata_request *);
static int afd_prevent_allow(device_t, int);
static int afd_test_ready(device_t);

/* internal vars */
static MALLOC_DEFINE(M_AFD, "afd_driver", "ATAPI floppy driver buffers");

static int 
afd_probe(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);
    if ((atadev->param.config & ATA_PROTO_ATAPI) &&
	(atadev->param.config & ATA_ATAPI_TYPE_MASK) == ATA_ATAPI_TYPE_DIRECT)
	return 0;  
    else
	return ENXIO;
}

static int 
afd_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp;
    cdev_t cdev;

    fdp = kmalloc(sizeof(struct afd_softc), M_AFD, M_WAITOK | M_ZERO);
    device_set_ivars(dev, fdp);
    ATA_SETMODE(device_get_parent(dev), dev);

    if (afd_sense(dev)) {
	device_set_ivars(dev, NULL);
	kfree(fdp, M_AFD);
	return ENXIO;
    }
    atadev->flags |= ATA_D_MEDIA_CHANGED;

    /* create the disk device */
    devstat_add_entry(&fdp->stats, "afd", device_get_unit(dev), DEV_BSIZE,
		      DEVSTAT_NO_ORDERED_TAGS, DEVSTAT_TYPE_DIRECT |
		      DEVSTAT_TYPE_IF_IDE, DEVSTAT_PRIORITY_WFD);
    cdev = disk_create(device_get_unit(dev), &fdp->disk, &afd_ops);
    disk_setdisktype(&fdp->disk, "floppy");
    cdev->si_drv1 = dev;
    if (ch->dma)
	cdev->si_iosize_max = ch->dma->max_iosize;
    else
	cdev->si_iosize_max = min(MAXPHYS,64*1024);
    fdp->cdev = cdev;

    /* announce we are here */
    afd_describe(dev);
    return 0;
}

static int
afd_detach(device_t dev)
{   
    struct afd_softc *fdp = device_get_ivars(dev);

    /* check that we have a valid device to detach */
    if (!device_get_ivars(dev))
        return ENXIO;
    
    /* detroy disk from the system so we dont get any further requests */
    disk_invalidate(&fdp->disk);
    disk_destroy(&fdp->disk);

    /* fail requests on the queue and any thats "in flight" for this device */
    ata_fail_requests(dev);

    /* dont leave anything behind */
    /* disk_destroy() already took care of the dev_ops */
    devstat_remove_entry(&fdp->stats);
    device_set_ivars(dev, NULL);
    kfree(fdp, M_AFD);
    return 0;
}

static void
afd_shutdown(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (atadev->param.support.command2 & ATA_SUPPORT_FLUSHCACHE)
	ata_controlcmd(dev, ATA_FLUSHCACHE, 0, 0, 0);
}

static int
afd_reinit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp = device_get_ivars(dev);
    
    if (((atadev->unit == ATA_MASTER) && !(ch->devices & ATA_ATAPI_MASTER)) ||
	((atadev->unit == ATA_SLAVE) && !(ch->devices & ATA_ATAPI_SLAVE))) {
	device_set_ivars(dev, NULL);
	kfree(fdp, M_AFD);
	return 1;
    }
    ATA_SETMODE(device_get_parent(dev), dev);
    return 0;
}

static int
afd_open(struct dev_open_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp = device_get_ivars(dev);
    struct disk_info info;

    if (!fdp) 
	return ENXIO;
    if (!device_is_attached(dev))
	return EBUSY;

    afd_test_ready(dev);
    afd_prevent_allow(dev, 1);

    if (afd_sense(dev))
	device_printf(dev, "sense media type failed\n");
    atadev->flags &= ~ATA_D_MEDIA_CHANGED;

    if (!fdp->mediasize)
	return ENXIO;

    bzero(&info, sizeof(info));
    info.d_media_blksize = fdp->sectorsize;	/* mandatory */
    info.d_media_size = fdp->mediasize;		/* (this is in bytes) */

    info.d_secpertrack = fdp->sectors;		/* optional */
    info.d_nheads = fdp->heads;
    info.d_ncylinders =
	   ((fdp->mediasize/fdp->sectorsize)/fdp->sectors)/fdp->heads;
    info.d_secpercyl = fdp->sectors * fdp->heads;

    disk_setdiskinfo(&fdp->disk, &info);
    return 0;
}

static int 
afd_close(struct dev_close_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct afd_softc *fdp = device_get_ivars(dev);

    if (count_dev(fdp->cdev) == 1)
	afd_prevent_allow(dev, 0); 
    return 0;
}

static int
afd_ioctl(struct dev_ioctl_args *ap)
{
    return ata_device_ioctl(ap->a_head.a_dev->si_drv1, ap->a_cmd, ap->a_data);
}

static int 
afd_strategy(struct dev_strategy_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct bio *bp = ap->a_bio;
    struct buf *bbp = bp->bio_buf;
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp = device_get_ivars(dev);
    struct ata_request *request;
    u_int32_t lba;
    u_int16_t count;
    int8_t ccb[16];

    /* if it's a null transfer, return immediatly. */
    if (bbp->b_bcount == 0) {
	bbp->b_resid = 0;
	biodone(bp);
	return 0;
    }

    /* should reject all queued entries if media have changed. */
    if (atadev->flags & ATA_D_MEDIA_CHANGED) {
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EIO;
	biodone(bp);
	return 0;
    }

    lba = bp->bio_offset / fdp->sectorsize;
    count = bbp->b_bcount / fdp->sectorsize;
    bbp->b_resid = bbp->b_bcount; 

    bzero(ccb, sizeof(ccb));

    switch(bbp->b_cmd) {
    case BUF_CMD_READ:
	ccb[0] = ATAPI_READ_BIG;
	break;
    case BUF_CMD_WRITE:
	ccb[0] = ATAPI_WRITE_BIG;
	break;
    default:
	device_printf(dev, "unknown BUF operation\n");
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EIO;
	biodone(bp);
	return 0;
    }

    ccb[2] = lba >> 24;
    ccb[3] = lba >> 16;
    ccb[4] = lba >> 8;
    ccb[5] = lba;
    ccb[7] = count>>8;
    ccb[8] = count;

    if (!(request = ata_alloc_request())) {
	bbp->b_flags |= B_ERROR;
	bbp->b_error = ENOMEM;
	biodone(bp);
	return 0;
    }
    request->dev = dev;
    request->bio = bp;
    bcopy(ccb, request->u.atapi.ccb,
	  (atadev->param.config & ATA_PROTO_MASK) == 
	  ATA_PROTO_ATAPI_12 ? 16 : 12);
    request->data = bbp->b_data;
    request->bytecount = count * fdp->sectorsize;
    request->transfersize = min(request->bytecount, 65534);
    request->timeout = (ccb[0] == ATAPI_WRITE_BIG) ? 60 : 30;
    request->retries = 2;
    request->callback = afd_done;

    switch (bbp->b_cmd) {
    case BUF_CMD_READ:
	request->flags = (ATA_R_ATAPI | ATA_R_READ);
	break;
    case BUF_CMD_WRITE:
	request->flags = (ATA_R_ATAPI | ATA_R_WRITE);
	break;
    default:
	panic("bbp->b_cmd");
    }
    if (atadev->mode >= ATA_DMA)
	request->flags |= ATA_R_DMA;
    request->flags |= ATA_R_ORDERED;
    devstat_start_transaction(&fdp->stats);
    ata_queue_request(request);
    return 0;
}

static void 
afd_done(struct ata_request *request)
{
    struct afd_softc *fdp = device_get_ivars(request->dev);
    struct bio *bp = request->bio;
    struct buf *bbp = bp->bio_buf;

    /* finish up transfer */
    if ((bbp->b_error = request->result))
	bbp->b_flags |= B_ERROR;
    bbp->b_resid = bbp->b_bcount - request->donecount;
    devstat_end_transaction_buf(&fdp->stats, bbp);
    biodone(bp);
    ata_free_request(request);
}

static int 
afd_sense(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp = device_get_ivars(dev);
    struct afd_capacity capacity;
    struct afd_capacity_big capacity_big;
    struct afd_capabilities capabilities;
    int8_t ccb1[16] = { ATAPI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0 };
    int8_t ccb2[16] = { ATAPI_READ_CAPACITY_16, 0x10, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, sizeof(struct afd_capacity_big) & 0xff, 0, 0 };
    int8_t ccb3[16] = { ATAPI_MODE_SENSE_BIG, 0, ATAPI_REWRITEABLE_CAP_PAGE,
		        0, 0, 0, 0, sizeof(struct afd_capabilities) >> 8,
		        sizeof(struct afd_capabilities) & 0xff,
			0, 0, 0, 0, 0, 0, 0 };
    int timeout = 20;
    int error, count;

    fdp->mediasize = 0;

    /* wait for device to get ready */
    while ((error = afd_test_ready(dev)) && timeout--) {
	DELAY(100000);
    }
    if (error == EBUSY)
	return 1;

    /* The IOMEGA Clik! doesn't support reading the cap page, fake it */
    if (!strncmp(atadev->param.model, "IOMEGA Clik!", 12)) {
	fdp->heads = 1;
	fdp->sectors = 2;
	fdp->mediasize = 39441 * 1024;
	fdp->sectorsize = 512;
	afd_test_ready(dev);
	return 0;
    }

    /* get drive capacity */
    if (!ata_atapicmd(dev, ccb1, (caddr_t)&capacity,
		      sizeof(struct afd_capacity), ATA_R_READ, 30)) {
	fdp->heads = 16;
	fdp->sectors = 63;
	fdp->sectorsize = be32toh(capacity.blocksize);
	fdp->mediasize = (u_int64_t)be32toh(capacity.capacity)*fdp->sectorsize; 
	afd_test_ready(dev);
	return 0;
    }

    /* get drive capacity big */
    if (!ata_atapicmd(dev, ccb2, (caddr_t)&capacity_big,
		      sizeof(struct afd_capacity_big),
		      ATA_R_READ | ATA_R_QUIET, 30)) {
	fdp->heads = 16;
	fdp->sectors = 63;
	fdp->sectorsize = be32toh(capacity_big.blocksize);
	fdp->mediasize = be64toh(capacity_big.capacity)*fdp->sectorsize;
	afd_test_ready(dev);
	return 0;
    }

    /* get drive capabilities, some bugridden drives needs this repeated */
    for (count = 0 ; count < 5 ; count++) {
	if (!ata_atapicmd(dev, ccb3, (caddr_t)&capabilities,
			  sizeof(struct afd_capabilities), ATA_R_READ, 30) &&
	    capabilities.page_code == ATAPI_REWRITEABLE_CAP_PAGE) {
	    fdp->heads = capabilities.heads;
	    fdp->sectors = capabilities.sectors;
	    fdp->sectorsize = be16toh(capabilities.sector_size);
	    fdp->mediasize = be16toh(capabilities.cylinders) *
			     fdp->heads * fdp->sectors * fdp->sectorsize;
	    if (!capabilities.medium_type)
		fdp->mediasize = 0;
	    return 0;
	}
    }
    return 1;
}

static int
afd_prevent_allow(device_t dev, int lock)
{
    struct ata_device *atadev = device_get_softc(dev);
    int8_t ccb[16] = { ATAPI_PREVENT_ALLOW, 0, 0, 0, lock,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    
    if (!strncmp(atadev->param.model, "IOMEGA Clik!", 12))
	return 0;
    return ata_atapicmd(dev, ccb, NULL, 0, 0, 30);
}

static int
afd_test_ready(device_t dev)
{
    int8_t ccb[16] = { ATAPI_TEST_UNIT_READY, 0, 0, 0, 0,
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return ata_atapicmd(dev, ccb, NULL, 0, 0, 30);
}

static void 
afd_describe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct afd_softc *fdp = device_get_ivars(dev);
    char sizestring[16];

    if (fdp->mediasize > 1048576 * 5)
	ksprintf(sizestring, "%lluMB", (unsigned long long)
		(fdp->mediasize / 1048576));
    else if (fdp->mediasize)
	ksprintf(sizestring, "%lluKB", (unsigned long long)
		(fdp->mediasize / 1024));
    else
	strcpy(sizestring, "(no media)");
 
    device_printf(dev, "%s <%.40s %.8s> at ata%d-%s %s\n",
		  sizestring, atadev->param.model, atadev->param.revision,
		  device_get_unit(ch->dev),
		  (atadev->unit == ATA_MASTER) ? "master" : "slave",
		  ata_mode2str(atadev->mode));
    if (bootverbose) {
	device_printf(dev, "%llu sectors [%lluC/%dH/%dS]\n",
	    	      (unsigned long long)(fdp->mediasize / fdp->sectorsize),
	    	      (unsigned long long)
		     (fdp->mediasize/(fdp->sectorsize*fdp->sectors*fdp->heads)),
	    	      fdp->heads, fdp->sectors);
    }
}

static device_method_t afd_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     afd_probe),
    DEVMETHOD(device_attach,    afd_attach),
    DEVMETHOD(device_detach,    afd_detach),
    DEVMETHOD(device_shutdown,  afd_shutdown),
    
    /* ATA methods */
    DEVMETHOD(ata_reinit,       afd_reinit),
    
    DEVMETHOD_END
};
    
static driver_t afd_driver = {
    "afd",
    afd_methods,
    0,
};

static devclass_t afd_devclass;

DRIVER_MODULE(afd, ata, afd_driver, afd_devclass, NULL, NULL);
MODULE_VERSION(afd, 1);
MODULE_DEPEND(afd, ata, 1, 1, 1);
