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
static int ata_cypress_chipinit(device_t dev);
static void ata_cypress_setmode(device_t dev, int mode);

/*
 * Cypress chipset support functions
 */
int
ata_cypress_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    /*
     * the Cypress chip is a mess, it contains two ATA functions, but
     * both channels are visible on the first one.
     * simply ignore the second function for now, as the right
     * solution (ignoring the second channel on the first function)
     * doesn't work with the crappy ATA interrupt setup on the alpha.
     */
    if (pci_get_devid(dev) == ATA_CYPRESS_82C693 &&
	pci_get_function(dev) == 1 &&
	pci_get_subclass(dev) == PCIS_STORAGE_IDE) {
	device_set_desc(dev, "Cypress 82C693 ATA controller");
	ctlr->chipinit = ata_cypress_chipinit;
	return 0;
    }
    return ENXIO;
}

static int
ata_cypress_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    ctlr->setmode = ata_cypress_setmode;
    return 0;
}

static void
ata_cypress_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int error;

    mode = ata_limit_mode(dev, mode, ATA_WDMA2);

    /* XXX SOS missing WDMA0+1 + PIO modes */
    if (mode == ATA_WDMA2) {
	error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);
	if (bootverbose)
	    device_printf(dev, "%ssetting WDMA2 on Cypress chip\n",
			  error ? "FAILURE " : "");
	if (!error) {
	    pci_write_config(gparent, ch->unit ? 0x4e : 0x4c, 0x2020, 2);
	    atadev->mode = mode;
	    return;
	}
    }
    /* we could set PIO mode timings, but we assume the BIOS did that */
}
