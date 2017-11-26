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
static int ata_ite_chipinit(device_t dev);
static void ata_ite_setmode(device_t dev, int mode);

/*
 * Integrated Technology Express Inc. (ITE) chipset support functions
 */
int
ata_ite_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_IT8212F, 0x00, 0x00, 0x00, ATA_UDMA6, "IT8212F" },
     { ATA_IT8211F, 0x00, 0x00, 0x00, ATA_UDMA6, "IT8211F" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_ITE_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_ite_chipinit;
    return 0;
}

static int
ata_ite_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    ctlr->setmode = ata_ite_setmode;

    /* set PCI mode and 66Mhz reference clock */
    pci_write_config(dev, 0x50, pci_read_config(dev, 0x50, 1) & ~0x83, 1);

    /* set default active & recover timings */
    pci_write_config(dev, 0x54, 0x31, 1);
    pci_write_config(dev, 0x56, 0x31, 1);
    return 0;
}

static void
ata_ite_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int devno = (ch->unit << 1) + atadev->unit;
    int error;
	static const uint8_t udmatiming[] =
		{ 0x44, 0x42, 0x31, 0x21, 0x11, 0xa2, 0x91 };
	static const uint8_t chtiming[] =
		{ 0xaa, 0xa3, 0xa1, 0x33, 0x31, 0x88, 0x32, 0x31 };

    /* correct the mode for what the HW supports */
    mode = ata_limit_mode(dev, mode, ATA_UDMA6);

    /* check the CBLID bits for 80 conductor cable detection */
    if (mode > ATA_UDMA2 && (pci_read_config(gparent, 0x40, 2) &
			     (ch->unit ? (1<<3) : (1<<2)))) {
	ata_print_cable(dev, "controller");
	mode = ATA_UDMA2;
    }

    /* set the wanted mode on the device */
    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%s setting %s on ITE8212F chip\n",
		      (error) ? "failed" : "success", ata_mode2str(mode));

    /* if the device accepted the mode change, setup the HW accordingly */
    if (!error) {
	if (mode >= ATA_UDMA0) {
	    /* enable UDMA mode */
	    pci_write_config(gparent, 0x50,
			     pci_read_config(gparent, 0x50, 1) &
			     ~(1 << (devno + 3)), 1);

	    /* set UDMA timing */
	    pci_write_config(gparent,
			     0x56 + (ch->unit << 2) + atadev->unit,
			     udmatiming[mode & ATA_MODE_MASK], 1);
	}
	else {
	    /* disable UDMA mode */
	    pci_write_config(gparent, 0x50,
			     pci_read_config(gparent, 0x50, 1) |
			     (1 << (devno + 3)), 1);

	    /* set active and recover timing (shared between master & slave) */
	    if (pci_read_config(gparent, 0x54 + (ch->unit << 2), 1) <
		chtiming[ata_mode2idx(mode)])
		pci_write_config(gparent, 0x54 + (ch->unit << 2),
				 chtiming[ata_mode2idx(mode)], 1);
	}
	atadev->mode = mode;
    }
}
