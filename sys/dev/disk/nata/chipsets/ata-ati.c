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
static int ata_ati_chipinit(device_t dev);
static void ata_ati_setmode(device_t dev, int mode);

/* misc defines */
#define ATI_AHCI	0x04
#define SII_MEMIO	1

/*
 * ATI chipset support functions
 */
int
ata_ati_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_ATI_IXP200,    0x00, 0,        0, ATA_UDMA5, "IXP200" },
     { ATA_ATI_IXP300,    0x00, 0,        0, ATA_UDMA6, "IXP300" },
     { ATA_ATI_IXP300_S1, 0x00, SII_MEMIO, 0, ATA_SA150, "IXP300" },
     { ATA_ATI_IXP400,    0x00, 0,        0, ATA_UDMA6, "IXP400" },
     { ATA_ATI_IXP400_S1, 0x00, SII_MEMIO, 0, ATA_SA150, "IXP400" },
     { ATA_ATI_IXP400_S2, 0x00, SII_MEMIO, 0, ATA_SA150, "IXP400" },
     { ATA_ATI_IXP600,    0x00, 0,        0, ATA_UDMA6, "IXP600" },
     { ATA_ATI_IXP600_S1, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP600" },
     { ATA_ATI_IXP600_S2, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP600" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_ATI_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);

    /*
     * The ATI SATA controllers are actually a SiI 3112 controller, except
     * for the SB600.
     */
    if (ctlr->chip->cfg1 & SII_MEMIO)
	ctlr->chipinit = ata_sii_chipinit;
    else
	ctlr->chipinit = ata_ati_chipinit;
    return 0;
}

static int
ata_ati_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    /* The SB600 needs special treatment. */
    if (ctlr->chip->cfg1 & ATI_AHCI) {
	/* Check if the chip is configured as an AHCI part. */
	if ((pci_get_subclass(dev) == PCIS_STORAGE_SATA) &&
	    (pci_read_config(dev, PCIR_PROGIF, 1) == PCIP_STORAGE_SATA_AHCI_1_0)) {
	    if (ata_ahci_chipinit(dev) != ENXIO)
		return 0;
	}
    }

    ctlr->setmode = ata_ati_setmode;
    return 0;
}

static void
ata_ati_setmode(device_t dev, int mode)
{
	device_t gparent = GRANDPARENT(dev);
	struct ata_pci_controller *ctlr = device_get_softc(gparent);
	struct ata_channel *ch = device_get_softc(device_get_parent(dev));
	struct ata_device *atadev = device_get_softc(dev);
	int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
	int offset = (devno ^ 0x01) << 3;
	int error;
	static const uint8_t piotimings[] =
			    { 0x5d, 0x47, 0x34, 0x22, 0x20, 0x34, 0x22, 0x20,
			      0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };
	static const uint8_t dmatimings[] = { 0x77, 0x21, 0x20 };

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    mode = ata_check_80pin(dev, mode);

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		      (error) ? "FAILURE " : "",
		      ata_mode2str(mode), ctlr->chip->text);
    if (!error) {
	if (mode >= ATA_UDMA0) {
	    pci_write_config(gparent, 0x56,
			     (pci_read_config(gparent, 0x56, 2) &
			      ~(0xf << (devno << 2))) |
			     ((mode & ATA_MODE_MASK) << (devno << 2)), 2);
	    pci_write_config(gparent, 0x54,
			     pci_read_config(gparent, 0x54, 1) |
			     (0x01 << devno), 1);
	    pci_write_config(gparent, 0x44,
			     (pci_read_config(gparent, 0x44, 4) &
			      ~(0xff << offset)) |
			     (dmatimings[2] << offset), 4);
	}
	else if (mode >= ATA_WDMA0) {
	    pci_write_config(gparent, 0x54,
			     pci_read_config(gparent, 0x54, 1) &
			      ~(0x01 << devno), 1);
	    pci_write_config(gparent, 0x44,
			     (pci_read_config(gparent, 0x44, 4) &
			      ~(0xff << offset)) |
			     (dmatimings[mode & ATA_MODE_MASK] << offset), 4);
	}
	else
	    pci_write_config(gparent, 0x54,
			     pci_read_config(gparent, 0x54, 1) &
			     ~(0x01 << devno), 1);

	pci_write_config(gparent, 0x4a,
			 (pci_read_config(gparent, 0x4a, 2) &
			  ~(0xf << (devno << 2))) |
			 (((mode - ATA_PIO0) & ATA_MODE_MASK) << (devno<<2)),2);
	pci_write_config(gparent, 0x40,
			 (pci_read_config(gparent, 0x40, 4) &
			  ~(0xff << offset)) |
			 (piotimings[ata_mode2idx(mode)] << offset), 4);
	atadev->mode = mode;
    }
}
