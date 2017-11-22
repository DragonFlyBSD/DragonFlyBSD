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
 * $FreeBSD: src/sys/dev/ata/ata-chipset.c,v 1.196 2007/04/08 19:18:51 sos Exp $
 */

#include "opt_ata.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/bus_dma.h>
#include <sys/bus_resource.h>
#include <sys/callout.h>
#include <sys/endian.h>
#include <sys/libkern.h>
#include <sys/lock.h>		/* for {get,rel}_mplock() */
#include <sys/malloc.h>
#include <sys/nata.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/machintr.h>

#include <sys/mplock2.h>

#include <machine/bus_dma.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "ata-all.h"
#include "ata-pci.h"
#include "ata_if.h"

/* local prototypes */
/* ata-chipset.c */
static int ata_generic_chipinit(device_t dev);
static void ata_generic_intr(void *data);
static void ata_generic_setmode(device_t dev, int mode);

static void ata_sata_phy_check_events(device_t dev);
static void ata_sata_phy_event(void *context, int dummy);
static int ata_sata_connect(struct ata_channel *ch);

/* used by ata-{ahci,intel,marvel,nvidia,promise,siliconimage,sis,via}.c */
static int ata_sata_phy_reset(device_t dev);

/* used by ata-{acerlabs,ahci,intel,jmicron,marvel,nvidia}.c */
/*         ata-{promise,serverworks,siliconimage,sis,via}.c */
static void ata_sata_setmode(device_t dev, int mode);

/* used by ata-{ahci,acerlabs,ati,intel,jmicron,via}.c */
static int ata_ahci_chipinit(device_t dev);

/* used by ata-ahci.c and ata-jmicron.c */
static int ata_ahci_allocate(device_t dev);
static void ata_ahci_dmainit(device_t dev);
static void ata_ahci_reset(device_t dev);

/* used by ata-ahci.c and ata-siliconimage.c */
static int ata_request2fis_h2d(struct ata_request *request, u_int8_t *fis);

/* used by ata-amd.c ata-nvidia.c ata-via.c */
static void ata_via_family_setmode(device_t dev, int mode);

/* ata-ati.c depends on ata-siliconimage.c */
/* used by ata-ati.c and ata-siliconimage.c */
static int ata_sii_chipinit(device_t dev);


/* misc functions */
static struct ata_chip_id *ata_match_chip(device_t dev, struct ata_chip_id *index);
static struct ata_chip_id *ata_find_chip(device_t dev, struct ata_chip_id *index, int slot);
static int ata_setup_interrupt(device_t dev);
static void ata_teardown_interrupt(device_t dev);
static void ata_print_cable(device_t dev, u_int8_t *who);
static int ata_atapi(device_t dev);
static int ata_check_80pin(device_t dev, int mode);
static int ata_mode2idx(int mode);

/*
 * ahci capable chipset support functions
 */
#include "chipsets/ata-ahci.c"

/*
 * various vendor specific chipset support functions
 */
#include "chipsets/ata-acard.c"
#include "chipsets/ata-acerlabs.c"
#include "chipsets/ata-amd.c"
#include "chipsets/ata-ati.c"
#include "chipsets/ata-cypress.c"
#include "chipsets/ata-cyrix.c"
#include "chipsets/ata-highpoint.c"
#include "chipsets/ata-intel.c"
#include "chipsets/ata-ite.c"
#include "chipsets/ata-jmicron.c"
#include "chipsets/ata-marvell.c"
#include "chipsets/ata-national.c"
#include "chipsets/ata-netcell.c"
#include "chipsets/ata-nvidia.c"
#include "chipsets/ata-promise.c"
#include "chipsets/ata-serverworks.c"
#include "chipsets/ata-siliconimage.c"
#include "chipsets/ata-sis.c"
#include "chipsets/ata-via.c"

/*
 * generic ATA support functions
 */
int
ata_generic_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    char buffer[64];

    ksnprintf(buffer, sizeof(buffer),
	      "%s ATA controller", ata_pcivendor2str(dev));
    device_set_desc_copy(dev, buffer);
    ctlr->chipinit = ata_generic_chipinit;
    return 0;
}

static int
ata_generic_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev))
	return ENXIO;
    ctlr->setmode = ata_generic_setmode;
    return 0;
}

static void
ata_generic_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    int unit;

    for (unit = 0; unit < ctlr->channels; unit++) {
	if ((ch = ctlr->interrupt[unit].argument))
	    ctlr->interrupt[unit].function(ch);
    }
}

static void
ata_generic_setmode(device_t dev, int mode)
{
    struct ata_device *atadev = device_get_softc(dev);

    mode = ata_limit_mode(dev, mode, ATA_UDMA2);
    mode = ata_check_80pin(dev, mode);
    if (!ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode))
	atadev->mode = mode;
}

/*
 * SATA support functions
 */
static void
ata_sata_phy_check_events(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    u_int32_t error = ATA_IDX_INL(ch, ATA_SERROR);

    /* clear error bits/interrupt */
    ATA_IDX_OUTL(ch, ATA_SERROR, error);

    /* do we have any events flagged ? */
    if (error) {
	struct ata_connect_task *tp;
	u_int32_t status = ATA_IDX_INL(ch, ATA_SSTATUS);

	/* if we have a connection event deal with it */
	if ((error & ATA_SE_PHY_CHANGED) &&
	    (tp = (struct ata_connect_task *)
		  kmalloc(sizeof(struct ata_connect_task),
			 M_ATA, M_INTWAIT | M_ZERO))) {

	    if (((status & ATA_SS_CONWELL_MASK) == ATA_SS_CONWELL_GEN1) ||
		((status & ATA_SS_CONWELL_MASK) == ATA_SS_CONWELL_GEN2)) {
		if (bootverbose)
		    device_printf(ch->dev, "CONNECT requested\n");
		tp->action = ATA_C_ATTACH;
	    }
	    else {
		if (bootverbose)
		    device_printf(ch->dev, "DISCONNECT requested\n");
		tp->action = ATA_C_DETACH;
	    }
	    tp->dev = ch->dev;
	    TASK_INIT(&tp->task, 0, ata_sata_phy_event, tp);
	    taskqueue_enqueue(taskqueue_thread[mycpuid], &tp->task);
	}
    }
}

static void
ata_sata_phy_event(void *context, int dummy)
{
    struct ata_connect_task *tp = (struct ata_connect_task *)context;
    struct ata_channel *ch = device_get_softc(tp->dev);
    device_t *children;
    int nchildren, i;

    get_mplock();
    if (tp->action == ATA_C_ATTACH) {
	if (bootverbose)
	    device_printf(tp->dev, "CONNECTED\n");
	ATA_RESET(tp->dev);
	ata_identify(tp->dev);
    }
    if (tp->action == ATA_C_DETACH) {
	if (!device_get_children(tp->dev, &children, &nchildren)) {
	    for (i = 0; i < nchildren; i++)
		if (children[i])
		    device_delete_child(tp->dev, children[i]);
	    kfree(children, M_TEMP);
	}
	lockmgr(&ch->state_mtx, LK_EXCLUSIVE);
	ch->state = ATA_IDLE;
	lockmgr(&ch->state_mtx, LK_RELEASE);
	if (bootverbose)
	    device_printf(tp->dev, "DISCONNECTED\n");
    }
    rel_mplock();
    kfree(tp, M_ATA);
}

static int
ata_sata_phy_reset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    int loop, retry;

    if ((ATA_IDX_INL(ch, ATA_SCONTROL) & ATA_SC_DET_MASK) == ATA_SC_DET_IDLE)
	return ata_sata_connect(ch);

    for (retry = 0; retry < 10; retry++) {
	for (loop = 0; loop < 10; loop++) {
	    ATA_IDX_OUTL(ch, ATA_SCONTROL, ATA_SC_DET_RESET);
	    ata_udelay(100);
	    if ((ATA_IDX_INL(ch, ATA_SCONTROL) &
		ATA_SC_DET_MASK) == ATA_SC_DET_RESET)
		break;
	}
	ata_udelay(5000);
	for (loop = 0; loop < 10; loop++) {
	    ATA_IDX_OUTL(ch, ATA_SCONTROL, ATA_SC_DET_IDLE |
					   ATA_SC_IPM_DIS_PARTIAL |
					   ATA_SC_IPM_DIS_SLUMBER);
	    ata_udelay(100);
	    if ((ATA_IDX_INL(ch, ATA_SCONTROL) & ATA_SC_DET_MASK) == 0)
		return ata_sata_connect(ch);
	}
    }
    return 0;
}

static int
ata_sata_connect(struct ata_channel *ch)
{
    u_int32_t status;
    int timeout;

    /* wait up to 1 second for "connect well" */
    for (timeout = 0; timeout < 100 ; timeout++) {
	status = ATA_IDX_INL(ch, ATA_SSTATUS);
	if ((status & ATA_SS_CONWELL_MASK) == ATA_SS_CONWELL_GEN1 ||
	    (status & ATA_SS_CONWELL_MASK) == ATA_SS_CONWELL_GEN2)
	    break;
	ata_udelay(10000);
    }
    if (timeout >= 100) {
	if (bootverbose)
	    device_printf(ch->dev, "SATA connect status=%08x\n", status);
	return 0;
    }

    if (bootverbose)
	device_printf(ch->dev, "SATA connect time=%dms\n", timeout * 10);

    /* clear SATA error register */
    ATA_IDX_OUTL(ch, ATA_SERROR, ATA_IDX_INL(ch, ATA_SERROR));

    return 1;
}

static void
ata_sata_setmode(device_t dev, int mode)
{
    struct ata_device *atadev = device_get_softc(dev);

    /*
     * if we detect that the device isn't a real SATA device we limit
     * the transfer mode to UDMA5/ATA100.
     * this works around the problems some devices has with the
     * Marvell 88SX8030 SATA->PATA converters and UDMA6/ATA133.
     */
    if (atadev->param.satacapabilities != 0x0000 &&
	atadev->param.satacapabilities != 0xffff) {
	struct ata_channel *ch = device_get_softc(device_get_parent(dev));

	/* on some drives we need to set the transfer mode */
	ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0,
		       ata_limit_mode(dev, mode, ATA_UDMA6));

	/* query SATA STATUS for the speed */
        if (ch->r_io[ATA_SSTATUS].res &&
	   ((ATA_IDX_INL(ch, ATA_SSTATUS) & ATA_SS_CONWELL_MASK) ==
	    ATA_SS_CONWELL_GEN2))
	    atadev->mode = ATA_SA300;
	else
	    atadev->mode = ATA_SA150;
    }
    else {
	mode = ata_limit_mode(dev, mode, ATA_UDMA5);
	if (!ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode))
	    atadev->mode = mode;
    }
}

static int
ata_request2fis_h2d(struct ata_request *request, u_int8_t *fis)
{
    struct ata_device *atadev = device_get_softc(request->dev);

    if (request->flags & ATA_R_ATAPI) {
	fis[0] = 0x27;  /* host to device */
	fis[1] = 0x80;  /* command FIS (note PM goes here) */
	fis[2] = ATA_PACKET_CMD;
	if (request->flags & (ATA_R_READ | ATA_R_WRITE))
	    fis[3] = ATA_F_DMA;
	else {
	    fis[5] = request->transfersize;
	    fis[6] = request->transfersize >> 8;
	}
	fis[7] = ATA_D_LBA | atadev->unit;
	fis[15] = ATA_A_4BIT;
	return 20;
    }
    else {
	ata_modify_if_48bit(request);
	fis[0] = 0x27;  /* host to device */
	fis[1] = 0x80;  /* command FIS (note PM goes here) */
	fis[2] = request->u.ata.command;
	fis[3] = request->u.ata.feature;
	fis[4] = request->u.ata.lba;
	fis[5] = request->u.ata.lba >> 8;
	fis[6] = request->u.ata.lba >> 16;
	fis[7] = ATA_D_LBA | atadev->unit;
	if (!(atadev->flags & ATA_D_48BIT_ACTIVE))
	    fis[7] |= (request->u.ata.lba >> 24 & 0x0f);
	fis[8] = request->u.ata.lba >> 24;
	fis[9] = request->u.ata.lba >> 32;
	fis[10] = request->u.ata.lba >> 40;
	fis[11] = request->u.ata.feature >> 8;
	fis[12] = request->u.ata.count;
	fis[13] = request->u.ata.count >> 8;
	fis[15] = ATA_A_4BIT;
	return 20;
    }
    return 0;
}

/* common code for VIA, AMD & nVidia */
static void
ata_via_family_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    u_int8_t timings[] = { 0xa8, 0x65, 0x42, 0x22, 0x20, 0x42, 0x22, 0x20,
			   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };
    int modes[][7] = {
	{ 0xc2, 0xc1, 0xc0, 0x00, 0x00, 0x00, 0x00 },   /* VIA ATA33 */
	{ 0xee, 0xec, 0xea, 0xe9, 0xe8, 0x00, 0x00 },   /* VIA ATA66 */
	{ 0xf7, 0xf6, 0xf4, 0xf2, 0xf1, 0xf0, 0x00 },   /* VIA ATA100 */
	{ 0xf7, 0xf7, 0xf6, 0xf4, 0xf2, 0xf1, 0xf0 },   /* VIA ATA133 */
	{ 0xc2, 0xc1, 0xc0, 0xc4, 0xc5, 0xc6, 0xc7 }};  /* AMD/nVIDIA */
    int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
    int reg = 0x53 - devno;
    int error;

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    if (ctlr->chip->cfg2 & AMDCABLE) {
	if (mode > ATA_UDMA2 &&
	    !(pci_read_config(gparent, 0x42, 1) & (1 << devno))) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
    }
    else
	mode = ata_check_80pin(dev, mode);

    if (ctlr->chip->cfg2 & NVIDIA)
	reg += 0x10;

    if (ctlr->chip->cfg1 != VIA133)
	pci_write_config(gparent, reg - 0x08, timings[ata_mode2idx(mode)], 1);

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		      (error) ? "FAILURE " : "", ata_mode2str(mode),
		      ctlr->chip->text);
    if (!error) {
	if (mode >= ATA_UDMA0)
	    pci_write_config(gparent, reg,
			     modes[ctlr->chip->cfg1][mode & ATA_MODE_MASK], 1);
	else
	    pci_write_config(gparent, reg, 0x8b, 1);
	atadev->mode = mode;
    }
}


/* misc functions */
static struct ata_chip_id *
ata_match_chip(device_t dev, struct ata_chip_id *index)
{
    while (index->chipid != 0) {
	if (pci_get_devid(dev) == index->chipid &&
	    pci_get_revid(dev) >= index->chiprev)
	    return index;
	index++;
    }
    return NULL;
}

static struct ata_chip_id *
ata_find_chip(device_t dev, struct ata_chip_id *index, int slot)
{
    device_t *children;
    int nchildren, i;

    if (device_get_children(device_get_parent(dev), &children, &nchildren))
	return 0;

    while (index->chipid != 0) {
	for (i = 0; i < nchildren; i++) {
	    if (((slot >= 0 && pci_get_slot(children[i]) == slot) ||
		 (slot < 0 && pci_get_slot(children[i]) <= -slot)) &&
		pci_get_devid(children[i]) == index->chipid &&
		pci_get_revid(children[i]) >= index->chiprev) {
		kfree(children, M_TEMP);
		return index;
	    }
	}
	index++;
    }
    kfree(children, M_TEMP);
    return NULL;
}

static int
ata_setup_interrupt(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int rid = ATA_IRQ_RID;

    if (!ctlr->legacy) {
	if (!(ctlr->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
						   RF_SHAREABLE | RF_ACTIVE))) {
	    device_printf(dev, "unable to map interrupt\n");
	    return ENXIO;
	}
	if ((bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS,
			    ata_generic_intr, ctlr, &ctlr->handle, NULL))) {
	    device_printf(dev, "unable to setup interrupt\n");
	    bus_release_resource(dev, SYS_RES_IRQ, rid, ctlr->r_irq);
	    ctlr->r_irq = 0;
	    return ENXIO;
	}
    }
    return 0;
}

static void
ata_teardown_interrupt(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (!ctlr->legacy) {
	if (ctlr->r_irq) {
	    bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle);
	    bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ctlr->r_irq);
	    ctlr->r_irq = 0;
	}
    }
}

static void
ata_print_cable(device_t dev, u_int8_t *who)
{
    device_printf(dev,
		  "DMA limited to UDMA33, %s found non-ATA66 cable\n", who);
}

static int
ata_atapi(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);

    return ((atadev->unit == ATA_MASTER && ch->devices & ATA_ATAPI_MASTER) ||
	    (atadev->unit == ATA_SLAVE && ch->devices & ATA_ATAPI_SLAVE));
}

static int
ata_check_80pin(device_t dev, int mode)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (mode > ATA_UDMA2 && !(atadev->param.hwres & ATA_CABLE_ID)) {
	ata_print_cable(dev, "device");
	mode = ATA_UDMA2;
    }
    return mode;
}

static int
ata_mode2idx(int mode)
{
    if ((mode & ATA_DMA_MASK) == ATA_UDMA0)
	 return (mode & ATA_MODE_MASK) + 8;
    if ((mode & ATA_DMA_MASK) == ATA_WDMA0)
	 return (mode & ATA_MODE_MASK) + 5;
    return (mode & ATA_MODE_MASK) - ATA_PIO0;
}
