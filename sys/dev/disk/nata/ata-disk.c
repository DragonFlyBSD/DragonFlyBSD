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
 * $FreeBSD: src/sys/dev/ata/ata-disk.c,v 1.199 2006/09/14 19:12:29 sos Exp $
 */

#include "opt_ata.h"

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/nata.h>
#include <sys/systm.h>

#include <vm/pmap.h>

#include <machine/md_var.h>

#include "ata-all.h"
#include "ata-disk.h"
#include "ata_if.h"

/* device structure */
static	d_open_t	ad_open;
static	d_close_t	ad_close;
static	d_ioctl_t	ad_ioctl;
static	d_strategy_t	ad_strategy;
static	d_dump_t	ad_dump;
static struct dev_ops ad_ops = {
	{ "ad", 0, D_DISK },
	.d_open =	ad_open,
	.d_close =	ad_close,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	ad_ioctl,
	.d_strategy =	ad_strategy,
	.d_dump =	ad_dump,
};

/* prototypes */
static void ad_init(device_t);
static void ad_done(struct ata_request *);
static void ad_describe(device_t dev);
static int ad_version(u_int16_t);

/* local vars */
static MALLOC_DEFINE(M_AD, "ad_driver", "ATA disk driver");

static int
ad_probe(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (!(atadev->param.config & ATA_PROTO_ATAPI) ||
	(atadev->param.config == ATA_CFA_MAGIC1) ||
	(atadev->param.config == ATA_CFA_MAGIC2) ||
	(atadev->param.config == ATA_CFA_MAGIC3))
	return 0;
    else
	return ENXIO;
}

static int
ad_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct disk_info info;
    struct ad_softc *adp;
    cdev_t cdev;
    u_int32_t lbasize;
    u_int64_t lbasize48;

    /* check that we have a virgin disk to attach */
    if (device_get_ivars(dev))
	return EEXIST;

    adp = kmalloc(sizeof(struct ad_softc), M_AD, M_INTWAIT | M_ZERO);
    device_set_ivars(dev, adp);

    if ((atadev->param.atavalid & ATA_FLAG_54_58) &&
	atadev->param.current_heads && atadev->param.current_sectors) {
	adp->heads = atadev->param.current_heads;
	adp->sectors = atadev->param.current_sectors;
	adp->total_secs = (u_int32_t)atadev->param.current_size_1 |
			  ((u_int32_t)atadev->param.current_size_2 << 16);
    }
    else {
	adp->heads = atadev->param.heads;
	adp->sectors = atadev->param.sectors;
	adp->total_secs = atadev->param.cylinders * adp->heads * adp->sectors;  
    }
    lbasize = (u_int32_t)atadev->param.lba_size_1 |
	      ((u_int32_t)atadev->param.lba_size_2 << 16);

    /* does this device need oldstyle CHS addressing */
    if (!ad_version(atadev->param.version_major) || !lbasize)
	atadev->flags |= ATA_D_USE_CHS;

    /* use the 28bit LBA size if valid or bigger than the CHS mapping */
    if (atadev->param.cylinders == 16383 || adp->total_secs < lbasize)
	adp->total_secs = lbasize;

    /* use the 48bit LBA size if valid */
    lbasize48 = ((u_int64_t)atadev->param.lba_size48_1) |
		((u_int64_t)atadev->param.lba_size48_2 << 16) |
		((u_int64_t)atadev->param.lba_size48_3 << 32) |
		((u_int64_t)atadev->param.lba_size48_4 << 48);
    if ((atadev->param.support.command2 & ATA_SUPPORT_ADDRESS48) &&
	lbasize48 > ATA_MAX_28BIT_LBA)
	adp->total_secs = lbasize48;

    /* init device parameters */
    ad_init(dev);

    /* create the disk device */
    /* XXX TGEN Maybe use DEVSTAT_ALL_SUPPORTED, DEVSTAT_TYPE_DIRECT,
       DEVSTAT_PRIORITY_MAX. */
    devstat_add_entry(&adp->stats, "ad", device_get_unit(dev), DEV_BSIZE,
		      DEVSTAT_NO_ORDERED_TAGS,
		      DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_IDE,
		      DEVSTAT_PRIORITY_DISK);
    cdev = disk_create(device_get_unit(dev), &adp->disk, &ad_ops);
    cdev->si_drv1 = dev;
    if (ch->dma)
        cdev->si_iosize_max = ch->dma->max_iosize;
    else
        cdev->si_iosize_max = min(MAXPHYS,64*1024);
    adp->cdev = cdev;

    bzero(&info, sizeof(info));
    info.d_media_blksize = DEV_BSIZE;		/* mandatory */
    info.d_media_blocks = adp->total_secs;

    info.d_secpertrack = adp->sectors;		/* optional */
    info.d_nheads = adp->heads;
    info.d_ncylinders = adp->total_secs/(adp->heads*adp->sectors);
    info.d_secpercyl = adp->sectors * adp->heads;
    info.d_serialno = atadev->param.serial;

    device_add_child(dev, "subdisk", device_get_unit(dev));
    bus_generic_attach(dev);

    /* announce we are here */
    ad_describe(dev);

    disk_setdiskinfo(&adp->disk, &info);

    return 0;
}

static int
ad_detach(device_t dev)
{
    struct ad_softc *adp = device_get_ivars(dev);
    device_t *children;
    int nchildren, i;

    /* check that we have a valid disk to detach */
    if (!adp)
	return ENXIO;

#if 0 /* XXX TGEN Probably useless, we fail the queue below. */
    /* check that the disk is closed */
    if (adp->ad_flags & AD_DISK_OPEN)
        return EBUSY;
#endif /* 0 */
    
    /* detach & delete all children */
    if (!device_get_children(dev, &children, &nchildren)) {
	for (i = 0; i < nchildren; i++)
	    if (children[i])
		device_delete_child(dev, children[i]);
	kfree(children, M_TEMP);
    }

    /* detroy disk from the system so we dont get any further requests */
    disk_invalidate(&adp->disk);
    disk_destroy(&adp->disk);

    /* fail requests on the queue and any thats "in flight" for this device */
    ata_fail_requests(dev);

    /* dont leave anything behind */
    /* disk_destroy() already took care of the dev_ops */
    devstat_remove_entry(&adp->stats);
    device_set_ivars(dev, NULL);
    kfree(adp, M_AD);
    return 0;
}

static void
ad_shutdown(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (atadev->param.support.command2 & ATA_SUPPORT_FLUSHCACHE)
	ata_controlcmd(dev, ATA_FLUSHCACHE, 0, 0, 0);
}

static int
ad_reinit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);

    /* if detach pending, return error */
    if (((atadev->unit == ATA_MASTER) && !(ch->devices & ATA_ATA_MASTER)) ||
	((atadev->unit == ATA_SLAVE) && !(ch->devices & ATA_ATA_SLAVE))) {
	return 1;
    }
    ad_init(dev);
    return 0;
}

static int
ad_open(struct dev_open_args *ap)
{
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ad_softc *adp = device_get_ivars(dev);

    if (!adp || adp->cdev == NULL)
	return ENXIO;
    if(!device_is_attached(dev))
	return EBUSY;

#if 0 /* XXX TGEN Probably useless, queue will be failed on detach. */
    adp->ad_flags &= AD_DISK_OPEN;
#endif /* 0 */
    return 0;
}

static int
ad_close(struct dev_close_args *ap)
{
#if 0 /* XXX TGEN Probably useless, queue will be failed on detach. */
    device_t dev = ap->a_head.a_dev->si_drv1;
    struct ad_softc *adp = device_get_ivars(dev);
    adp->ad_flags |= AD_DISK_OPEN;
#endif /* 0 */
    return 0;
}

static int
ad_ioctl(struct dev_ioctl_args *ap)
{
    return ata_device_ioctl(ap->a_head.a_dev->si_drv1, ap->a_cmd, ap->a_data);
}

static int
ad_strategy(struct dev_strategy_args *ap)
{
    device_t dev =  ap->a_head.a_dev->si_drv1;
    struct bio *bp = ap->a_bio;
    struct buf *bbp = bp->bio_buf;
    struct ata_device *atadev = device_get_softc(dev);
    struct ata_request *request;
    struct ad_softc *adp = device_get_ivars(dev);

    if (!(request = ata_alloc_request())) {
	device_printf(dev, "FAILURE - out of memory in strategy\n");
	bbp->b_flags |= B_ERROR;
	bbp->b_error = ENOMEM;
	biodone(bp);
	return(0);
    }

    /* setup request */
    request->dev = dev;
    request->bio = bp;
    request->callback = ad_done;
    request->timeout = ATA_DEFAULT_TIMEOUT;
    request->retries = 2;
    request->data = bbp->b_data;
    request->bytecount = bbp->b_bcount;
    /* lba is block granularity, convert byte granularity bio_offset */
    request->u.ata.lba = (u_int64_t)(bp->bio_offset >> DEV_BSHIFT);
    request->u.ata.count = request->bytecount / DEV_BSIZE;
    request->transfersize = min(bbp->b_bcount, atadev->max_iosize);

    switch (bbp->b_cmd) {
    case BUF_CMD_READ:
	request->flags = ATA_R_READ;
	if (atadev->mode >= ATA_DMA) {
	    request->u.ata.command = ATA_READ_DMA;
	    request->flags |= ATA_R_DMA;
	}
	else if (request->transfersize > DEV_BSIZE)
	    request->u.ata.command = ATA_READ_MUL;
	else
	    request->u.ata.command = ATA_READ;
	break;
    case BUF_CMD_WRITE:
	request->flags = ATA_R_WRITE;
	if (atadev->mode >= ATA_DMA) {
	    request->u.ata.command = ATA_WRITE_DMA;
	    request->flags |= ATA_R_DMA;
	}
	else if (request->transfersize > DEV_BSIZE)
	    request->u.ata.command = ATA_WRITE_MUL;
	else
	    request->u.ata.command = ATA_WRITE;
	break;
    case BUF_CMD_FLUSH:
	request->u.ata.lba = 0;
	request->u.ata.count = 0;
	request->u.ata.feature = 0;
	request->bytecount = 0;
	request->transfersize = 0;
	request->flags = ATA_R_CONTROL;
	request->u.ata.command = ATA_FLUSHCACHE;
	/* ATA FLUSHCACHE requests may take up to 30 sec to timeout */
	request->timeout = 30;
	break;
    default:
	device_printf(dev, "FAILURE - unknown BUF operation\n");
	ata_free_request(request);
	bbp->b_flags |= B_ERROR;
	bbp->b_error = EIO;
	biodone(bp);
	return(0);
    }
    request->flags |= ATA_R_ORDERED;
    devstat_start_transaction(&adp->stats);
    ata_queue_request(request);
    return(0);
}

static void
ad_done(struct ata_request *request)
{
    struct ad_softc *adp = device_get_ivars(request->dev);
    struct bio *bp = request->bio;
    struct buf *bbp = bp->bio_buf;

    /* finish up transfer */
    if ((bbp->b_error = request->result))
	bbp->b_flags |= B_ERROR;
    bbp->b_resid = bbp->b_bcount - request->donecount;
    devstat_end_transaction_buf(&adp->stats, bbp);
    biodone(bp);
    ata_free_request(request);
}

static int
ad_dump(struct dev_dump_args *ap)
{
	device_t dev = ap->a_head.a_dev->si_drv1;
	struct ata_device *atadev = device_get_softc(dev);
	struct ata_request request;

	ata_drop_requests(dev);
	/*
	 * 0 length means flush buffers and return
	 */
	if (ap->a_length == 0) {
		/* flush buffers to media */
		if (atadev->param.support.command2 & ATA_SUPPORT_FLUSHCACHE)
			return ata_controlcmd(dev, ATA_FLUSHCACHE, 0, 0, 0);
		else
			return ENXIO;
	}

	bzero(&request, sizeof(struct ata_request));
	request.dev = dev;

	request.data = ap->a_virtual;
	request.bytecount = ap->a_length;
	request.transfersize = min(request.bytecount, atadev->max_iosize);
	request.flags = ATA_R_WRITE;

	if (atadev->mode >= ATA_DMA) {
		request.u.ata.command = ATA_WRITE_DMA;
		request.flags |= ATA_DMA;
	} else if (request.transfersize > DEV_BSIZE)
		request.u.ata.command = ATA_WRITE_MUL;
	else
		request.u.ata.command = ATA_WRITE;
	request.u.ata.lba = ap->a_offset / DEV_BSIZE;
	request.u.ata.count = request.bytecount / DEV_BSIZE;

	request.timeout = ATA_DEFAULT_TIMEOUT;
	request.retries = 2;

	ata_queue_request(&request);
	return request.result;
}

static void
ad_init(device_t dev)
{
    struct ata_device *atadev = device_get_softc(dev);

    ATA_SETMODE(device_get_parent(dev), dev);

    /* enable readahead caching */
    if (atadev->param.support.command1 & ATA_SUPPORT_LOOKAHEAD)
	ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_ENAB_RCACHE, 0, 0);

    /* enable write caching if supported and configured */
    if (atadev->param.support.command1 & ATA_SUPPORT_WRITECACHE) {
	if (ata_wc)
	    ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_ENAB_WCACHE, 0, 0);
	else
	    ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_DIS_WCACHE, 0, 0);
    }

    /* use multiple sectors/interrupt if device supports it */
    if (ad_version(atadev->param.version_major)) {
	int secsperint = max(1, min(atadev->param.sectors_intr & 0xff, 16));

	if (!ata_controlcmd(dev, ATA_SET_MULTI, 0, 0, secsperint))
	    atadev->max_iosize = secsperint * DEV_BSIZE;
    }
    else
	atadev->max_iosize = DEV_BSIZE;
}

void
ad_describe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    struct ad_softc *adp = device_get_ivars(dev);
    u_int8_t *marker, vendor[64], product[64];

    /* try to seperate the ATA model string into vendor and model parts */
    if ((marker = index(atadev->param.model, ' ')) ||
	(marker = index(atadev->param.model, '-'))) {
	int len = (marker - atadev->param.model);

	strncpy(vendor, atadev->param.model, len);
	vendor[len++] = 0;
	strcat(vendor, " ");
	strncpy(product, atadev->param.model + len, 40 - len);
	vendor[40 - len] = 0;
    }
    else {
	if (!strncmp(atadev->param.model, "ST", 2))
	    strcpy(vendor, "Seagate ");
	else if (!strncmp(atadev->param.model, "HDS", 3))
	    strcpy(vendor, "Hitachi ");
	else
	    strcpy(vendor, "");
	strncpy(product, atadev->param.model, 40);
    }

    device_printf(dev, "%lluMB <%s%s %.8s> at ata%d-%s %s%s\n",
		  (unsigned long long)(adp->total_secs / (1048576 / DEV_BSIZE)),
		  vendor, product, atadev->param.revision,
		  device_get_unit(ch->dev),
		  (atadev->unit == ATA_MASTER) ? "master" : "slave",
		  (adp->flags & AD_F_TAG_ENABLED) ? "tagged " : "",
		  ata_mode2str(atadev->mode));
    if (bootverbose) {
	device_printf(dev, "%llu sectors [%lluC/%dH/%dS] "
		      "%d sectors/interrupt %d depth queue\n",
		      (unsigned long long)adp->total_secs,(unsigned long long)(
		      adp->total_secs / (adp->heads * adp->sectors)),
		      adp->heads, adp->sectors, atadev->max_iosize / DEV_BSIZE,
		      adp->num_tags + 1);
    }
}

static int
ad_version(u_int16_t version)
{
    int bit;

    if (version == 0xffff)
	return 0;
    for (bit = 15; bit >= 0; bit--)
	if (version & (1<<bit))
	    return bit;
    return 0;
}

static device_method_t ad_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     ad_probe),
    DEVMETHOD(device_attach,    ad_attach),
    DEVMETHOD(device_detach,    ad_detach),
    DEVMETHOD(device_shutdown,  ad_shutdown),

    /* ATA methods */
    DEVMETHOD(ata_reinit,       ad_reinit),

    DEVMETHOD_END
};

static driver_t ad_driver = {
    "ad",
    ad_methods,
    0,
};

devclass_t ad_devclass;

DRIVER_MODULE(ad, ata, ad_driver, ad_devclass, NULL, NULL);
MODULE_VERSION(ad, 1);
MODULE_DEPEND(ad, ata, 1, 1, 1);
