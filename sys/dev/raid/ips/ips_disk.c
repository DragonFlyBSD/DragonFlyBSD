/*-
 * Written by: David Jeffery
 * Copyright (c) 2002 Adaptec Inc.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ips/ips_disk.c,v 1.4 2003/09/22 04:59:07 njl Exp $
 * $DragonFly: src/sys/dev/raid/ips/ips_disk.c,v 1.5 2004/10/06 02:12:31 y0netan1 Exp $
 */

#include <sys/devicestat.h>
#include <dev/raid/ips/ips.h>
#include <dev/raid/ips/ips_disk.h>
#include <sys/stat.h>

static int ipsd_probe(device_t dev);
static int ipsd_attach(device_t dev);
static int ipsd_detach(device_t dev);

static disk_open_t ipsd_open;
static disk_close_t ipsd_close;
static disk_strategy_t ipsd_strategy;

static struct cdevsw ipsd_cdevsw = {
	.d_name		= "ipsd",
	.d_maj		= IPSD_CDEV_MAJOR,
	.d_flags	= D_DISK,
	.d_port		= NULL,
	.d_clone	= NULL,
	.old_open	= ipsd_open,
	.old_close	= ipsd_close,
	.old_strategy	= ipsd_strategy,
	.old_read	= physread,
	.old_write	= physwrite,
};

static device_method_t ipsd_methods[] = {
	DEVMETHOD(device_probe,		ipsd_probe),
	DEVMETHOD(device_attach,	ipsd_attach),
	DEVMETHOD(device_detach,	ipsd_detach),
	{ 0, 0 }
};


static driver_t ipsd_driver = {
	"ipsd",
	ipsd_methods,
	sizeof(ipsdisk_softc_t)
};

static devclass_t ipsd_devclass;
DRIVER_MODULE(ipsd, ips, ipsd_driver, ipsd_devclass, 0, 0);

/*
 * handle opening of disk device.  It must set up all information about
 * the geometry and size of the disk
 */
static int
ipsd_open(dev_t dev, int oflags, int devtype, d_thread_t *td)
{
	ipsdisk_softc_t *dsc = dev->si_drv1;

	if (dsc == NULL)
		return (ENXIO);
	dsc->state |= IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm open\n");
	return 0;
}

static int
ipsd_close(dev_t dev, int oflags, int devtype, d_thread_t *td)
{
	ipsdisk_softc_t *dsc = dev->si_drv1;

	dsc->state &= ~IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm closed for the day\n");
	return 0;
}

/* ipsd_finish is called to clean up and return a completed IO request */
void
ipsd_finish(struct bio *iobuf)
{
	ipsdisk_softc_t *dsc;

	dsc = iobuf->bio_disk->d_drv1;
	if (iobuf->bio_flags & BIO_ERROR) {
		device_printf(dsc->dev, "iobuf error %d\n", iobuf->bio_error);
	} else
		iobuf->bio_resid = 0;
	devstat_end_transaction_buf(&dsc->stats, iobuf);
	biodone(iobuf);
}


static void
ipsd_strategy(struct bio *iobuf)
{
	ipsdisk_softc_t *dsc;

	dsc = iobuf->bio_disk->d_drv1;
	DEVICE_PRINTF(8, dsc->dev, "in strategy\n");
	iobuf->bio_driver1 = (void *)(uintptr_t)dsc->sc->drives[dsc->disk_number].drivenum;
	devstat_start_transaction(&dsc->stats);
	ips_start_io_request(dsc->sc, iobuf);
}

static int
ipsd_probe(device_t dev)
{
	DEVICE_PRINTF(2, dev, "in probe\n");
	device_set_desc(dev, "Logical Drive");
	return 0;
}

static int
ipsd_attach(device_t dev)
{
	device_t adapter;
	ipsdisk_softc_t *dsc;
	struct	disklabel *label;
	u_int totalsectors;
	u_int nheads, nsectors;

	DEVICE_PRINTF(2, dev, "in attach\n");
	dsc = (ipsdisk_softc_t *)device_get_softc(dev);
	bzero(dsc, sizeof(ipsdisk_softc_t));
	adapter = device_get_parent(dev);
	dsc->dev = dev;
	dsc->sc = device_get_softc(adapter);
	dsc->unit = device_get_unit(dev);
	dsc->disk_number = (uintptr_t) device_get_ivars(dev);
	totalsectors = dsc->sc->drives[dsc->disk_number].sector_count;
	if ((totalsectors > 0x400000) &&
	    ((dsc->sc->adapter_info.miscflags & 0x8) == 0)) {
		nheads = IPS_NORM_HEADS;
		nsectors = IPS_NORM_SECTORS;
	} else {
		nheads = IPS_COMP_HEADS;
		nsectors = IPS_COMP_SECTORS;
	}
	devstat_add_entry(&dsc->stats, "ipsd", dsc->unit, DEV_BSIZE,
			  DEVSTAT_NO_ORDERED_TAGS,
			  DEVSTAT_TYPE_DIRECT | DEVSTAT_TYPE_IF_SCSI,
			  DEVSTAT_PRIORITY_DISK);
	dsc->ipsd_dev_t = disk_create(dsc->unit, &dsc->ipsd_disk, 0,
	    &ipsd_cdevsw);
	dsc->ipsd_dev_t->si_drv1 = dsc;
	dsc->ipsd_dev_t->si_iosize_max = IPS_MAX_IO_SIZE;
	label = &dsc->ipsd_disk.d_label;
	bzero(label, sizeof(*label));
	label->d_ntracks    = nheads;
	label->d_nsectors   = nsectors;
	label->d_type       = DTYPE_ESDI;
	label->d_secsize    = IPS_BLKSIZE;
	label->d_ncylinders = totalsectors / nheads / nsectors;
	label->d_secpercyl  = nsectors / nheads;
	label->d_secperunit = totalsectors;
	device_printf(dev, "Logical Drive  (%dMB)\n",
	    dsc->sc->drives[dsc->disk_number].sector_count >> 11);
	return 0;
}

static int
ipsd_detach(device_t dev)
{
	ipsdisk_softc_t *dsc;

	DEVICE_PRINTF(2, dev, "in detach\n");
	dsc = (ipsdisk_softc_t *)device_get_softc(dev);
	if (dsc->state & IPS_DEV_OPEN)
		return (EBUSY);
	devstat_remove_entry(&dsc->stats);
	disk_destroy(&dsc->ipsd_disk);
	return 0;
}

