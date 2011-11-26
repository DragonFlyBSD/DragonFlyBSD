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
 */

#include <sys/devicestat.h>
#include <dev/raid/ips/ips.h>
#include <dev/raid/ips/ips_disk.h>
#include <sys/stat.h>
#include <sys/dtype.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/md_var.h>

static int ipsd_probe(device_t dev);
static int ipsd_attach(device_t dev);
static int ipsd_detach(device_t dev);

static void ipsd_dump_map_sg(void *arg, bus_dma_segment_t *segs, int nsegs,
			     int error);
static void ipsd_dump_block_complete(ips_command_t *command);

static d_open_t ipsd_open;
static d_close_t ipsd_close;
static d_strategy_t ipsd_strategy;
static d_dump_t ipsd_dump;

static struct dev_ops ipsd_ops = {
	{ "ipsd", 0, D_DISK },
	.d_open	=	ipsd_open,
	.d_close =	ipsd_close,
	.d_strategy =	ipsd_strategy,
	.d_read	=	physread,
	.d_write =	physwrite,
	.d_dump	=	ipsd_dump,
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
DRIVER_MODULE(ipsd, ips, ipsd_driver, ipsd_devclass, NULL, NULL);

/*
 * handle opening of disk device.  It must set up all information about
 * the geometry and size of the disk
 */
static int
ipsd_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	ipsdisk_softc_t *dsc = dev->si_drv1;

	if (dsc == NULL)
		return (ENXIO);
	dsc->state |= IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm open\n");
	return 0;
}

static int
ipsd_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	ipsdisk_softc_t *dsc = dev->si_drv1;

	dsc->state &= ~IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm closed for the day\n");
	return 0;
}

/* ipsd_finish is called to clean up and return a completed IO request */
void
ipsd_finish(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	ipsdisk_softc_t *dsc;

	dsc = bio->bio_driver_info;
	if (bp->b_flags & B_ERROR) {
		device_printf(dsc->dev, "iobuf error %d\n", bp->b_error);
	} else {
		bp->b_resid = 0;
	}
	devstat_end_transaction_buf(&dsc->stats, bp);
	biodone(bio);
	ips_start_io_request(dsc->sc);
}


static int
ipsd_strategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	ipsdisk_softc_t *dsc;

	dsc = dev->si_drv1;
	DEVICE_PRINTF(8, dsc->dev, "in strategy\n");
	bio->bio_driver_info = dsc;
	devstat_start_transaction(&dsc->stats);
	lockmgr(&dsc->sc->queue_lock, LK_EXCLUSIVE|LK_RETRY);
	bioqdisksort(&dsc->sc->bio_queue, bio);
	ips_start_io_request(dsc->sc);
	lockmgr(&dsc->sc->queue_lock, LK_RELEASE);
	return(0);
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
	struct disk_info info;
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
	dsc->ipsd_dev_t = disk_create(dsc->unit, &dsc->ipsd_disk, &ipsd_ops);
	dsc->ipsd_dev_t->si_drv1 = dsc;
	dsc->ipsd_dev_t->si_iosize_max = IPS_MAX_IO_SIZE;

	bzero(&info, sizeof(info));
	info.d_media_blksize	= IPS_BLKSIZE;		/* mandatory */
	info.d_media_blocks	= totalsectors;

	info.d_type		= DTYPE_ESDI;		/* optional */
	info.d_nheads		= nheads;
	info.d_secpertrack	= nsectors;
	info.d_ncylinders	= totalsectors / nheads / nsectors;
	info.d_secpercyl	= nsectors / nheads;

	disk_setdiskinfo(&dsc->ipsd_disk, &info);

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


static int
ipsd_dump(struct dev_dump_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	ips_softc_t *sc;
	ips_command_t *command;
	ips_io_cmd *command_struct;
	ipsdisk_softc_t *dsc;
	off_t off;
	uint8_t *va;
	size_t len;
	int error = 0;

	dsc = dev->si_drv1;
	if (dsc == NULL)
		return (EINVAL);
	sc = dsc->sc;

	if (ips_get_free_cmd(sc, &command, 0) != 0) {
		kprintf("ipsd: failed to get cmd for dump\n");
		return (ENOMEM);
	}

	command->data_dmatag = sc->sg_dmatag;
	command->callback = ipsd_dump_block_complete;

	command_struct = (ips_io_cmd *)command->command_buffer;
	command_struct->id = command->id;
	command_struct->drivenum = sc->drives[dsc->disk_number].drivenum;

	off = ap->a_offset;
	va = ap->a_virtual;

	size_t length = ap->a_length;
	while (length > 0) {
		len = length > IPS_MAX_IO_SIZE ? IPS_MAX_IO_SIZE : length;
		command_struct->lba = off / IPS_BLKSIZE;
		if (bus_dmamap_load(command->data_dmatag, command->data_dmamap,
		    va, len, ipsd_dump_map_sg, command, 0) != 0) {
			error = EIO;
			break;
		}
		if (COMMAND_ERROR(&command->status)) {
			error = EIO;
			break;
		}

		length -= len;
		off += len;
		va += len;
	}
	ips_insert_free_cmd(command->sc, command);
	return(error);
}

static void
ipsd_dump_map_sg(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	ips_softc_t *sc;
	ips_command_t *command;
	ips_sg_element_t *sg_list;
	ips_io_cmd *command_struct;
	int i, length;

	command = (ips_command_t *)arg;
	sc = command->sc;
	length = 0;

	if (error) {
		kprintf("ipsd_dump_map_sg: error %d\n", error);
		command->status.value = IPS_ERROR_STATUS;
		return;
	}

	command_struct = (ips_io_cmd *)command->command_buffer;

	if (nsegs != 1) {
		command_struct->segnum = nsegs;
		sg_list = (ips_sg_element_t *)((uint8_t *)
		    command->command_buffer + IPS_COMMAND_LEN);
		for (i = 0; i < nsegs; i++) {
			sg_list[i].addr = segs[i].ds_addr;
			sg_list[i].len = segs[i].ds_len;
			length += segs[i].ds_len;
		}
		command_struct->buffaddr =
		    (uint32_t)command->command_phys_addr + IPS_COMMAND_LEN;
		command_struct->command = IPS_SG_WRITE_CMD;
	} else {
		command_struct->buffaddr = segs[0].ds_addr;
		length = segs[0].ds_len;
		command_struct->segnum = 0;
		command_struct->command = IPS_WRITE_CMD;
	}

	length = (length + IPS_BLKSIZE - 1) / IPS_BLKSIZE;
	command_struct->length = length;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap,
	    BUS_DMASYNC_PREWRITE);

	sc->ips_issue_cmd(command);
	sc->ips_poll_cmd(command);
	return;
}

static void
ipsd_dump_block_complete(ips_command_t *command)
{
	if (COMMAND_ERROR(&command->status)) {
		kprintf("ipsd_dump completion error= 0x%x\n",
		       command->status.value);
	}
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);
}
