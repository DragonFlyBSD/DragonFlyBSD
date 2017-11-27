/*-
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
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
 */

/* local prototypes */
static int ata_jmicron_chipinit(device_t dev);
static int ata_jmicron_allocate(device_t dev);
static void ata_jmicron_reset(device_t dev);
static void ata_jmicron_dmainit(device_t dev);
static void ata_jmicron_setmode(device_t dev, int mode);

/*
 * JMicron chipset support functions
 */
int
ata_jmicron_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    const struct ata_chip_id *idx;
    static const struct ata_chip_id ids[] =
    {{ ATA_JMB360, 0, 1, 0, ATA_SA300, "JMB360" },
     { ATA_JMB361, 0, 1, 1, ATA_SA300, "JMB361" },
     { ATA_JMB363, 0, 2, 1, ATA_SA300, "JMB363" },
     { ATA_JMB365, 0, 1, 2, ATA_SA300, "JMB365" },
     { ATA_JMB366, 0, 2, 2, ATA_SA300, "JMB366" },
     { ATA_JMB368, 0, 0, 1, ATA_UDMA6, "JMB368" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];

    if (pci_get_vendor(dev) != ATA_JMICRON_ID)
	return ENXIO;

    if (!(idx = ata_match_chip(dev, ids)))
        return ENXIO;

    if ((pci_read_config(dev, 0xdf, 1) & 0x40) &&
	(pci_get_function(dev) == (pci_read_config(dev, 0x40, 1) & 0x02 >> 1)))
	ksnprintf(buffer, sizeof(buffer), "JMicron %s %s controller",
		idx->text, ata_mode2str(ATA_UDMA6));
    else
	ksnprintf(buffer, sizeof(buffer), "JMicron %s %s controller",
		idx->text, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_jmicron_chipinit;
    return 0;
}

static int
ata_jmicron_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int error;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    /* do we have multiple PCI functions ? */
    if (pci_read_config(dev, 0xdf, 1) & 0x40) {
	/* are we on the AHCI part ? */
	if (ata_ahci_chipinit(dev) != ENXIO)
	    return 0;

	/* otherwise we are on the PATA part */
	ctlr->allocate = ata_pci_allocate;
	ctlr->reset = ata_generic_reset;
	ctlr->dmainit = ata_pci_dmainit;
	ctlr->setmode = ata_jmicron_setmode;
	ctlr->channels = ctlr->chip->cfg2;
    }
    else {
	/* set controller configuration to a combined setup we support */
	pci_write_config(dev, 0x40, 0x80c0a131, 4);
	pci_write_config(dev, 0x80, 0x01200000, 4);

	if (ctlr->chip->cfg1 && (error = ata_ahci_chipinit(dev))) {
	    ata_teardown_interrupt(dev);
	    return error;
	}

	ctlr->allocate = ata_jmicron_allocate;
	ctlr->reset = ata_jmicron_reset;
	ctlr->dmainit = ata_jmicron_dmainit;
	ctlr->setmode = ata_jmicron_setmode;

	/* set the number of HW channels */
	ctlr->channels = ctlr->chip->cfg1 + ctlr->chip->cfg2;
    }
    return 0;
}

static int
ata_jmicron_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (ch->unit >= ctlr->chip->cfg1) {
	ch->unit -= ctlr->chip->cfg1;
	error = ata_pci_allocate(dev);
	ch->unit += ctlr->chip->cfg1;
    }
    else
	error = ata_ahci_allocate(dev);
    return error;
}

static void
ata_jmicron_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ch->unit >= ctlr->chip->cfg1)
	ata_generic_reset(dev);
    else
	ata_ahci_reset(dev);
}

static void
ata_jmicron_dmainit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ch->unit >= ctlr->chip->cfg1)
	ata_pci_dmainit(dev);
    else
	ata_ahci_dmainit(dev);
}

static void
ata_jmicron_setmode(device_t dev, int mode)
{
    struct ata_pci_controller *ctlr = device_get_softc(GRANDPARENT(dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));

    if (pci_read_config(dev, 0xdf, 1) & 0x40 || ch->unit >= ctlr->chip->cfg1) {
	struct ata_device *atadev = device_get_softc(dev);

	/* check for 80pin cable present */
	if (pci_read_config(dev, 0x40, 1) & 0x08)
	    mode = ata_limit_mode(dev, mode, ATA_UDMA2);
	else
	    mode = ata_limit_mode(dev, mode, ATA_UDMA6);

	if (!ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode))
	    atadev->mode = mode;
    }
    else
	ata_sata_setmode(dev, mode);
}
