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
 * $FreeBSD: src/sys/dev/ata/ata-pci.c,v 1.121 2007/02/23 12:18:33 piso Exp $
 */

#include "opt_ata.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/bus_resource.h>
#include <sys/module.h>
#include <sys/nata.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/machintr.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "ata-all.h"
#include "ata-pci.h"
#include "ata_if.h"

/* misc defines */
#define IOMASK                  0xfffffffc
#define ATA_PROBE_OK            -10

static const struct none_atapci {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
} none_atapci_table[] = {
	/* Appears on Intel PRO/1000 PM */
	{ ATA_INTEL_ID, 0x108d, ATA_INTEL_ID, 0x0000 },
	{ 0xffff, 0, 0, 0 }
};

int
ata_legacy(device_t dev)
{
    return (((pci_read_config(dev, PCIR_PROGIF, 1)&PCIP_STORAGE_IDE_MASTERDEV)&&
	    ((pci_read_config(dev, PCIR_PROGIF, 1) &
	      (PCIP_STORAGE_IDE_MODEPRIM | PCIP_STORAGE_IDE_MODESEC)) !=
	     (PCIP_STORAGE_IDE_MODEPRIM | PCIP_STORAGE_IDE_MODESEC))) ||
	    (!pci_read_config(dev, PCIR_BAR(0), 4) &&
	     !pci_read_config(dev, PCIR_BAR(1), 4) &&
	     !pci_read_config(dev, PCIR_BAR(2), 4) &&
	     !pci_read_config(dev, PCIR_BAR(3), 4) &&
	     !pci_read_config(dev, PCIR_BAR(5), 4)));
}

int
ata_pci_probe(device_t dev)
{
    if (pci_get_class(dev) != PCIC_STORAGE)
	return ENXIO;

    /* if this is an AHCI chipset grab it */
    if (pci_get_subclass(dev) == PCIS_STORAGE_SATA) {
	if (!ata_ahci_ident(dev))
	    return ATA_PROBE_OK;
    }

    /* run through the vendor specific drivers */
    switch (pci_get_vendor(dev)) {
    case ATA_ACARD_ID: 
	if (!ata_acard_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_ACER_LABS_ID:
	if (!ata_ali_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_AMD_ID:
	if (!ata_amd_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_ATI_ID:
	if (!ata_ati_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_CYRIX_ID:
	if (!ata_cyrix_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_CYPRESS_ID:
	if (!ata_cypress_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_HIGHPOINT_ID: 
	if (!ata_highpoint_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_INTEL_ID:
	if (!ata_intel_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_ITE_ID:
	if (!ata_ite_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_JMICRON_ID:
	if (!ata_jmicron_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_MARVELL_ID:
	if (!ata_marvell_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_NATIONAL_ID:
	if (!ata_national_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_NETCELL_ID:
	if (!ata_netcell_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_NVIDIA_ID:
	if (!ata_nvidia_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_PROMISE_ID:
	if (!ata_promise_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_SERVERWORKS_ID: 
	if (!ata_serverworks_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_SILICON_IMAGE_ID:
	if (!ata_sii_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_SIS_ID:
	if (!ata_sis_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_VIA_ID: 
	if (!ata_via_ident(dev))
	    return ATA_PROBE_OK;
	break;
    case ATA_CENATEK_ID:
	if (pci_get_devid(dev) == ATA_CENATEK_ROCKET) {
	    ata_generic_ident(dev);
	    device_set_desc(dev, "Cenatek Rocket Drive controller");
	    return ATA_PROBE_OK;
	}
	break;
    case ATA_MICRON_ID:
	if (pci_get_devid(dev) == ATA_MICRON_RZ1000 ||
	    pci_get_devid(dev) == ATA_MICRON_RZ1001) {
	    ata_generic_ident(dev);
	    device_set_desc(dev, 
		"RZ 100? ATA controller !WARNING! data loss/corruption risk");
	    return ATA_PROBE_OK;
	}
	break;
    }

    /* unknown chipset, try generic AHCI or DMA if it seems possible */
    if (pci_get_subclass(dev) == PCIS_STORAGE_IDE) {
	uint16_t vendor, device, subvendor, subdevice;
	const struct none_atapci *e;

	vendor = pci_get_vendor(dev);
	device = pci_get_device(dev);
	subvendor = pci_get_subvendor(dev);
	subdevice = pci_get_subdevice(dev);
	for (e = none_atapci_table; e->vendor != 0xffff; ++e) {
	    if (e->vendor == vendor && e->device == device &&
		e->subvendor == subvendor && e->subdevice == subdevice)
		return ENXIO;
	}

	if (!ata_generic_ident(dev))
	    return ATA_PROBE_OK;
    }
    return ENXIO;
}

int
ata_pci_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    u_int32_t cmd;
    int unit;

    /* do chipset specific setups only needed once */
    ctlr->legacy = ata_legacy(dev);
    if (ctlr->legacy || pci_read_config(dev, PCIR_BAR(2), 4) & IOMASK)
	ctlr->channels = 2;
    else
	ctlr->channels = 1;
    ctlr->allocate = ata_pci_allocate;
    ctlr->dev = dev;

    /* if needed try to enable busmastering */
    cmd = pci_read_config(dev, PCIR_COMMAND, 2);
    if (!(cmd & PCIM_CMD_BUSMASTEREN)) {
	pci_write_config(dev, PCIR_COMMAND, cmd | PCIM_CMD_BUSMASTEREN, 2);
	cmd = pci_read_config(dev, PCIR_COMMAND, 2);
    }

    /* if busmastering mode "stuck" use it */
    if ((cmd & PCIM_CMD_BUSMASTEREN) == PCIM_CMD_BUSMASTEREN) {
	ctlr->r_type1 = SYS_RES_IOPORT;
	ctlr->r_rid1 = ATA_BMADDR_RID;
	ctlr->r_res1 = bus_alloc_resource_any(dev, ctlr->r_type1, &ctlr->r_rid1,
					      RF_ACTIVE);
	/* Only set a dma init function if the device actually supports it. */
        ctlr->dmainit = ata_pci_dmainit;
    }

    if (ctlr->chipinit(dev))
	return ENXIO;

    /* attach all channels on this controller */
    for (unit = 0; unit < ctlr->channels; unit++) {
	int freeunit = 2;
	if ((unit == 0 || unit == 1) && ctlr->legacy) {
	    device_add_child(dev, "ata", unit);
	    continue;
	}
	/* XXX TGEN devclass_find_free_unit() implementation */
	if (ata_devclass) {
		while (freeunit < devclass_get_maxunit(ata_devclass) &&
		    devclass_get_device(ata_devclass, freeunit) != NULL)
			freeunit++;
	}
	device_add_child(dev, "ata", freeunit);
    }
    bus_generic_attach(dev);
    return 0;
}

int
ata_pci_detach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    device_t *children;
    int nchildren, i;

    /* detach & delete all children */
    if (!device_get_children(dev, &children, &nchildren)) {
	for (i = 0; i < nchildren; i++)
	    device_delete_child(dev, children[i]);
	kfree(children, M_TEMP);
    }

    if (ctlr->r_irq) {
	bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle);
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ctlr->r_irq);
	ctlr->r_irq = NULL;
    }
    if (ctlr->r_res2) {
	bus_release_resource(dev, ctlr->r_type2, ctlr->r_rid2, ctlr->r_res2);
	ctlr->r_res2 = NULL;
    }
    if (ctlr->r_res1) {
	bus_release_resource(dev, ctlr->r_type1, ctlr->r_rid1, ctlr->r_res1);
	ctlr->r_res1 = NULL;
    }

    return 0;
}

struct resource *
ata_pci_alloc_resource(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags, int cpuid)
{
    struct ata_pci_controller *controller = device_get_softc(dev);
    int unit = ((struct ata_channel *)device_get_softc(child))->unit;
    struct resource *res = NULL;
    int myrid;

    if (type == SYS_RES_IOPORT) {
	switch (*rid) {
	case ATA_IOADDR_RID:
	    if (controller->legacy) {
		start = (unit ? ATA_SECONDARY : ATA_PRIMARY);
		count = ATA_IOSIZE;
		end = start + count - 1;
	    }
	    myrid = PCIR_BAR(0) + (unit << 3);
	    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
				     SYS_RES_IOPORT, &myrid,
				     start, end, count, flags, cpuid);
	    break;

	case ATA_CTLADDR_RID:
	    if (controller->legacy) {
		start = (unit ? ATA_SECONDARY : ATA_PRIMARY) + ATA_CTLOFFSET;
		count = ATA_CTLIOSIZE;
		end = start + count - 1;
	    }
	    myrid = PCIR_BAR(1) + (unit << 3);
	    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
				     SYS_RES_IOPORT, &myrid,
				     start, end, count, flags, cpuid);
	    break;
	}
    }
    if (type == SYS_RES_IRQ && *rid == ATA_IRQ_RID) {
	if (controller->legacy) {
	    int irq = (unit == 0 ? 14 : 15);

	    cpuid = machintr_legacy_intr_cpuid(irq);
	    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
				     SYS_RES_IRQ, rid, irq, irq, 1, flags,
				     cpuid);
	}
	else
	    res = controller->r_irq;
    }
    return res;
}

int
ata_pci_release_resource(device_t dev, device_t child, int type, int rid,
			 struct resource *r)
{
    struct ata_pci_controller *controller = device_get_softc(dev);
    int unit = ((struct ata_channel *)device_get_softc(child))->unit;

    if (type == SYS_RES_IOPORT) {
	switch (rid) {
	case ATA_IOADDR_RID:
	    return BUS_RELEASE_RESOURCE(device_get_parent(dev), dev,
					SYS_RES_IOPORT,
					PCIR_BAR(0) + (unit << 3), r);
	    break;

	case ATA_CTLADDR_RID:
	    return BUS_RELEASE_RESOURCE(device_get_parent(dev), dev,
					SYS_RES_IOPORT,
					PCIR_BAR(1) + (unit << 3), r);
	    break;
	default:
	    return ENOENT;
	}
    }
    if (type == SYS_RES_IRQ) {
	if (rid != ATA_IRQ_RID)
	    return ENOENT;

	if (controller->legacy) {
	    return BUS_RELEASE_RESOURCE(device_get_parent(dev), child,
					SYS_RES_IRQ, rid, r);
	}
	else  
	    return 0;
    }
    return EINVAL;
}

int
ata_pci_setup_intr(device_t dev, device_t child, struct resource *irq, 
		   int flags, driver_intr_t *function, void *argument,
		   void **cookiep)
{
    struct ata_pci_controller *controller = device_get_softc(dev);

    if (controller->legacy) {
	return BUS_SETUP_INTR(device_get_parent(dev), child, irq,
			      flags, function, argument, cookiep, NULL, NULL);
    }
    else {
	struct ata_pci_controller *controller = device_get_softc(dev);
	int unit = ((struct ata_channel *)device_get_softc(child))->unit;

	controller->interrupt[unit].function = function;
	controller->interrupt[unit].argument = argument;
	*cookiep = controller;
	return 0;
    }
}

int
ata_pci_teardown_intr(device_t dev, device_t child, struct resource *irq,
		      void *cookie)
{
    struct ata_pci_controller *controller = device_get_softc(dev);

    if (controller->legacy) {
	return BUS_TEARDOWN_INTR(device_get_parent(dev), child, irq, cookie);
    }
    else {
	struct ata_pci_controller *controller = device_get_softc(dev);
	int unit = ((struct ata_channel *)device_get_softc(child))->unit;

	controller->interrupt[unit].function = NULL;
	controller->interrupt[unit].argument = NULL;
	return 0;
    }
}
    
int
ata_pci_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    struct resource *io = NULL, *ctlio = NULL;
    int i, rid;

    rid = ATA_IOADDR_RID;
    if (!(io = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE)))
	return ENXIO;

    rid = ATA_CTLADDR_RID;
    if (!(ctlio = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,RF_ACTIVE))){
	bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID, io);
	return ENXIO;
    }

    for (i = ATA_DATA; i <= ATA_COMMAND; i ++) {
	ch->r_io[i].res = io;
	ch->r_io[i].offset = i;
    }
    ch->r_io[ATA_CONTROL].res = ctlio;
    ch->r_io[ATA_CONTROL].offset = ctlr->legacy ? 0 : 2;
    ch->r_io[ATA_IDX_ADDR].res = io;
    ata_default_registers(dev);
    if (ctlr->r_res1) {
	for (i = ATA_BMCMD_PORT; i <= ATA_BMDTP_PORT; i++) {
	    ch->r_io[i].res = ctlr->r_res1;
	    ch->r_io[i].offset = (i - ATA_BMCMD_PORT) + (ch->unit*ATA_BMIOSIZE);
	}
    }

    ata_pci_hw(dev);
    return 0;
}

void
ata_pci_hw(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_generic_hw(dev);
    ch->hw.status = ata_pci_status;
}

int
ata_pci_status(device_t dev)
{
    struct ata_pci_controller *controller =
	device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if ((dumping || !controller->legacy) &&
	ch->dma && ((ch->flags & ATA_ALWAYS_DMASTAT) ||
		    (ch->dma->flags & ATA_DMA_ACTIVE))) {
	int bmstat = ATA_IDX_INB(ch, ATA_BMSTAT_PORT) & ATA_BMSTAT_MASK;

	/*
	 * Strictly speaking the DMA engine should already be stopped
	 * once we receive the interrupt.
	 * However at least ICH controllers seem to have the habbit
	 * of not clearing the active bit even though the interrupt
	 * is valid.
	 * To make sure we wait a little bit (to make sure that other
	 * buggy systems actually have a chance of finishing their
	 * DMA transaction) and then ignore the active bit.
	 */
	if ((bmstat & (ATA_BMSTAT_ACTIVE | ATA_BMSTAT_INTERRUPT)) ==
		(ATA_BMSTAT_ACTIVE | ATA_BMSTAT_INTERRUPT)) {
	    DELAY(100);
	    bmstat = ATA_IDX_INB(ch, ATA_BMSTAT_PORT) & ATA_BMSTAT_MASK;
	}
	if ((bmstat & ATA_BMSTAT_INTERRUPT) == 0)
	    return 0;
	ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, bmstat & ~ATA_BMSTAT_ERROR);
	DELAY(1);
    }
    if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY) {
	DELAY(100);
	if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY)
	    return 0;
    }
    return 1;
}

static int
ata_pci_dmastart(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    u_int8_t val;

    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, (ATA_IDX_INB(ch, ATA_BMSTAT_PORT) | 
		 (ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR)));
    ATA_IDX_OUTL(ch, ATA_BMDTP_PORT, ch->dma->sg_bus);
    ch->dma->flags |= ATA_DMA_ACTIVE;
    val = ATA_IDX_INB(ch, ATA_BMCMD_PORT);
    if (ch->dma->flags & ATA_DMA_READ)
	val |= ATA_BMCMD_WRITE_READ;
    else
	val &= ~ATA_BMCMD_WRITE_READ;
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, val);

    /*
     * Issue the start command separately from configuration setup,
     * in case the hardware latches portions of the configuration.
     */
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, val | ATA_BMCMD_START_STOP);

    return 0;
}

static int
ata_pci_dmastop(device_t dev)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    int error;

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, 
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ch->dma->flags &= ~ATA_DMA_ACTIVE;
    error = ATA_IDX_INB(ch, ATA_BMSTAT_PORT) & ATA_BMSTAT_MASK;
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    return error;
}

static void
ata_pci_dmareset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, 
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ch->dma->flags &= ~ATA_DMA_ACTIVE;
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    ch->dma->unload(dev);
}

void
ata_pci_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_dmainit(dev);
    if (ch->dma) {
	ch->dma->start = ata_pci_dmastart;
	ch->dma->stop = ata_pci_dmastop;
	ch->dma->reset = ata_pci_dmareset;
    }
}

char *
ata_pcivendor2str(device_t dev)
{
    switch (pci_get_vendor(dev)) {
    case ATA_ACARD_ID:		return "Acard";
    case ATA_ACER_LABS_ID:	return "AcerLabs";
    case ATA_AMD_ID:		return "AMD";
    case ATA_ATI_ID:		return "ATI";
    case ATA_CYRIX_ID:		return "Cyrix";
    case ATA_CYPRESS_ID:	return "Cypress";
    case ATA_HIGHPOINT_ID:	return "HighPoint";
    case ATA_INTEL_ID:		return "Intel";
    case ATA_ITE_ID:		return "ITE";
    case ATA_JMICRON_ID:	return "JMicron";
    case ATA_MARVELL_ID:	return "Marvell";
    case ATA_NATIONAL_ID:	return "National";
    case ATA_NETCELL_ID:	return "Netcell";
    case ATA_NVIDIA_ID:		return "nVidia";
    case ATA_PROMISE_ID:	return "Promise";
    case ATA_SERVERWORKS_ID:	return "ServerWorks";
    case ATA_SILICON_IMAGE_ID:	return "SiI";
    case ATA_SIS_ID:		return "SiS";
    case ATA_VIA_ID:		return "VIA";
    case ATA_CENATEK_ID:	return "Cenatek";
    case ATA_MICRON_ID:		return "Micron";
    default:			return "Generic";
    }
}

static device_method_t ata_pci_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,             ata_pci_probe),
    DEVMETHOD(device_attach,            ata_pci_attach),
    DEVMETHOD(device_detach,            ata_pci_detach),
    DEVMETHOD(device_shutdown,          bus_generic_shutdown),
    DEVMETHOD(device_suspend,           bus_generic_suspend),
    DEVMETHOD(device_resume,            bus_generic_resume),

    /* bus methods */
    DEVMETHOD(bus_alloc_resource,       ata_pci_alloc_resource),
    DEVMETHOD(bus_release_resource,     ata_pci_release_resource),
    DEVMETHOD(bus_activate_resource,    bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,  bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,           ata_pci_setup_intr),
    DEVMETHOD(bus_teardown_intr,        ata_pci_teardown_intr),

    DEVMETHOD_END
};

devclass_t atapci_devclass;

static driver_t ata_pci_driver = {
    "atapci",
    ata_pci_methods,
    sizeof(struct ata_pci_controller),
};

DRIVER_MODULE(atapci, pci, ata_pci_driver, atapci_devclass, NULL, NULL);
MODULE_VERSION(atapci, 1);
MODULE_DEPEND(atapci, ata, 1, 1, 1);

static int
ata_pcichannel_probe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    device_t *children;
    int count, i;
    char buffer[32];

    /* take care of green memory */
    bzero(ch, sizeof(struct ata_channel));

    /* find channel number on this controller */
    device_get_children(device_get_parent(dev), &children, &count);
    for (i = 0; i < count; i++) {
	if (children[i] == dev)
	    ch->unit = i;
    }
    kfree(children, M_TEMP);

    ksprintf(buffer, "ATA channel %d", ch->unit);
    device_set_desc_copy(dev, buffer);

    return ata_probe(dev);
}

static int
ata_pcichannel_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (ctlr->dmainit)
	ctlr->dmainit(dev);
    if (ch->dma)
	ch->dma->alloc(dev);

    if ((error = ctlr->allocate(dev))) {
	if (ch->dma)
	    ch->dma->free(dev);
	return error;
    }

    return ata_attach(dev);
}

static int
ata_pcichannel_detach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if ((error = ata_detach(dev)))
	return error;

    if (ch->dma)
	ch->dma->free(dev);

    /* XXX SOS free resources for io and ctlio ?? */

    return 0;
}

static int
ata_pcichannel_locking(device_t dev, int mode)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ctlr->locking)
	return ctlr->locking(dev, mode);
    else
	return ch->unit;
}

static void
ata_pcichannel_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* if DMA engine present reset it  */
    if (ch->dma) {
	if (ch->dma->reset)
	    ch->dma->reset(dev);
	ch->dma->unload(dev);
    }

    /* reset the controller HW */
    if (ctlr->reset)
	ctlr->reset(dev);
    else
	ata_generic_reset(dev);
}

static void
ata_pcichannel_setmode(device_t parent, device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(GRANDPARENT(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int mode = atadev->mode;

    ctlr->setmode(dev, ATA_PIO_MAX);
    if (mode >= ATA_DMA)
	ctlr->setmode(dev, mode);
}

static device_method_t ata_pcichannel_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     ata_pcichannel_probe),
    DEVMETHOD(device_attach,    ata_pcichannel_attach),
    DEVMETHOD(device_detach,    ata_pcichannel_detach),
    DEVMETHOD(device_shutdown,  bus_generic_shutdown),
    DEVMETHOD(device_suspend,   ata_suspend),
    DEVMETHOD(device_resume,    ata_resume),

    /* ATA methods */
    DEVMETHOD(ata_setmode,      ata_pcichannel_setmode),
    DEVMETHOD(ata_locking,      ata_pcichannel_locking),
    DEVMETHOD(ata_reset,        ata_pcichannel_reset),

    DEVMETHOD_END
};

driver_t ata_pcichannel_driver = {
    "ata",
    ata_pcichannel_methods,
    sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, atapci, ata_pcichannel_driver, ata_devclass, NULL, NULL);
