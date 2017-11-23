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
static int ata_intel_chipinit(device_t dev);
static int ata_intel_allocate(device_t dev);
static void ata_intel_reset(device_t dev);
static void ata_intel_old_setmode(device_t dev, int mode);
static void ata_intel_new_setmode(device_t dev, int mode);
static void ata_intel_sata_setmode(device_t dev, int mode);
static int ata_intel_31244_allocate(device_t dev);
static int ata_intel_31244_status(device_t dev);
static int ata_intel_31244_command(struct ata_request *request);
static void ata_intel_31244_reset(device_t dev);

/* misc defines */
#define INTEL_AHCI	1

/*
 * Intel chipset support functions
 */
int
ata_intel_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    struct ata_chip_id *idx;
    static struct ata_chip_id ids[] =
    {{ ATA_I82371FB,     0,          0, 0, ATA_WDMA2, "PIIX" },
     { ATA_I82371SB,     0,          0, 0, ATA_WDMA2, "PIIX3" },
     { ATA_I82371AB,     0,          0, 0, ATA_UDMA2, "PIIX4" },
     { ATA_I82443MX,     0,          0, 0, ATA_UDMA2, "PIIX4" },
     { ATA_I82451NX,     0,          0, 0, ATA_UDMA2, "PIIX4" },
     { ATA_I82801AB,     0,          0, 0, ATA_UDMA2, "ICH0" },
     { ATA_I82801AA,     0,          0, 0, ATA_UDMA4, "ICH" },
     { ATA_I82372FB,     0,          0, 0, ATA_UDMA4, "ICH" },
     { ATA_I82801BA,     0,          0, 0, ATA_UDMA5, "ICH2" },
     { ATA_I82801BA_1,   0,          0, 0, ATA_UDMA5, "ICH2" },
     { ATA_I82801CA,     0,          0, 0, ATA_UDMA5, "ICH3" },
     { ATA_I82801CA_1,   0,          0, 0, ATA_UDMA5, "ICH3" },
     { ATA_I82801DB,     0,          0, 0, ATA_UDMA5, "ICH4" },
     { ATA_I82801DB_1,   0,          0, 0, ATA_UDMA5, "ICH4" },
     { ATA_I82801EB,     0,          0, 0, ATA_UDMA5, "ICH5" },
     { ATA_I82801EB_S1,  0,          0, 0, ATA_SA150, "ICH5" },
     { ATA_I82801EB_R1,  0,          0, 0, ATA_SA150, "ICH5" },
     { ATA_I6300ESB,     0,          0, 0, ATA_UDMA5, "6300ESB" },
     { ATA_I6300ESB_S1,  0,          0, 0, ATA_SA150, "6300ESB" },
     { ATA_I6300ESB_R1,  0,          0, 0, ATA_SA150, "6300ESB" },
     { ATA_I82801FB,     0,          0, 0, ATA_UDMA5, "ICH6" },
     { ATA_I82801FB_S1,  0, INTEL_AHCI, 0, ATA_SA150, "ICH6" },
     { ATA_I82801FB_R1,  0, INTEL_AHCI, 0, ATA_SA150, "ICH6" },
     { ATA_I82801FBM,    0, INTEL_AHCI, 0, ATA_SA150, "ICH6M" },
     { ATA_I82801GB,     0,          0, 0, ATA_UDMA5, "ICH7" },
     { ATA_I82801GB_S1,  0, INTEL_AHCI, 0, ATA_SA300, "ICH7" },
     { ATA_I82801GB_R1,  0, INTEL_AHCI, 0, ATA_SA300, "ICH7" },
     { ATA_I82801GB_AH,  0, INTEL_AHCI, 0, ATA_SA300, "ICH7" },
     { ATA_I82801GBM_S1, 0, INTEL_AHCI, 0, ATA_SA300, "ICH7M" },
     { ATA_I82801GBM_R1, 0, INTEL_AHCI, 0, ATA_SA300, "ICH7M" },
     { ATA_I82801GBM_AH, 0, INTEL_AHCI, 0, ATA_SA300, "ICH7M" },
     { ATA_I63XXESB2,    0,          0, 0, ATA_UDMA5, "63XXESB2" },
     { ATA_I63XXESB2_S1, 0, INTEL_AHCI, 0, ATA_SA300, "63XXESB2" },
     { ATA_I63XXESB2_S2, 0, INTEL_AHCI, 0, ATA_SA300, "63XXESB2" },
     { ATA_I63XXESB2_R1, 0, INTEL_AHCI, 0, ATA_SA300, "63XXESB2" },
     { ATA_I63XXESB2_R2, 0, INTEL_AHCI, 0, ATA_SA300, "63XXESB2" },
     { ATA_I82801HB_S1,  0, INTEL_AHCI, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HB_S2,  0, INTEL_AHCI, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HB_R1,  0, INTEL_AHCI, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HB_AH4, 0, INTEL_AHCI, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HB_AH6, 0, INTEL_AHCI, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HBM_S1, 0,          0, 0, ATA_SA300, "ICH8M" },
     { ATA_I82801HBM_S2, 0, INTEL_AHCI, 0, ATA_SA300, "ICH8M" },
     { ATA_I82801HBM_S3, 0, INTEL_AHCI, 0, ATA_SA300, "ICH8M" },
     { ATA_I82801IB_S1,  0, INTEL_AHCI, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_S2,  0, INTEL_AHCI, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_AH2, 0, INTEL_AHCI, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_AH4, 0, INTEL_AHCI, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_AH6, 0, INTEL_AHCI, 0, ATA_SA300, "ICH9" },
     { ATA_I31244,       0,          0, 0, ATA_SA150, "31244" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];

    if (!(idx = ata_match_chip(dev, ids)))
	return ENXIO;

    ksprintf(buffer, "Intel %s %s controller",
	    idx->text, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_intel_chipinit;
    return 0;
}

static int
ata_intel_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev))
	return ENXIO;

    /* good old PIIX needs special treatment (not implemented) */
    if (ctlr->chip->chipid == ATA_I82371FB) {
	ctlr->setmode = ata_intel_old_setmode;
    }

    /* the intel 31244 needs special care if in DPA mode */
    else if (ctlr->chip->chipid == ATA_I31244) {
	if (pci_get_subclass(dev) != PCIS_STORAGE_IDE) {
	    ctlr->r_type2 = SYS_RES_MEMORY;
	    ctlr->r_rid2 = PCIR_BAR(0);
	    if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
							&ctlr->r_rid2,
							RF_ACTIVE))) {
		ata_teardown_interrupt(dev);
		return ENXIO;
	    }
	    ctlr->channels = 4;
	    ctlr->allocate = ata_intel_31244_allocate;
	    ctlr->reset = ata_intel_31244_reset;
	}
	ctlr->setmode = ata_sata_setmode;
    }

    /* non SATA intel chips goes here */
    else if (ctlr->chip->max_dma < ATA_SA150) {
	ctlr->allocate = ata_intel_allocate;
	ctlr->setmode = ata_intel_new_setmode;
    }

    /* SATA parts can be either compat or AHCI */
    else {
	/* force all ports active "the legacy way" */
	pci_write_config(dev, 0x92, pci_read_config(dev, 0x92, 2) | 0x0f, 2);

	ctlr->allocate = ata_intel_allocate;
	ctlr->reset = ata_intel_reset;

	/*
	 * if we have AHCI capability and AHCI or RAID mode enabled
	 * in BIOS we try for AHCI mode
	 */
	if ((ctlr->chip->cfg1 == INTEL_AHCI) &&
	    (pci_read_config(dev, 0x90, 1) & 0xc0) &&
	    (ata_ahci_chipinit(dev) != ENXIO))
	    return 0;

	/* if BAR(5) is IO it should point to SATA interface registers */
	ctlr->r_type2 = SYS_RES_IOPORT;
	ctlr->r_rid2 = PCIR_BAR(5);
	if ((ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						   &ctlr->r_rid2, RF_ACTIVE)))
	    ctlr->setmode = ata_intel_sata_setmode;
	else
	    ctlr->setmode = ata_sata_setmode;

	/* enable PCI interrupt */
	pci_write_config(dev, PCIR_COMMAND,
			 pci_read_config(dev, PCIR_COMMAND, 2) & ~0x0400, 2);
    }
    return 0;
}

static int
ata_intel_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* setup the usual register normal pci style */
    if (ata_pci_allocate(dev))
	return ENXIO;

    /* if r_res2 is valid it points to SATA interface registers */
    if (ctlr->r_res2) {
	ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
	ch->r_io[ATA_IDX_ADDR].offset = 0x00;
	ch->r_io[ATA_IDX_DATA].res = ctlr->r_res2;
	ch->r_io[ATA_IDX_DATA].offset = 0x04;
    }

    ch->flags |= ATA_ALWAYS_DMASTAT;
    return 0;
}

static void
ata_intel_reset(device_t dev)
{
    device_t parent = device_get_parent(dev);
    struct ata_pci_controller *ctlr = device_get_softc(parent);
    struct ata_channel *ch = device_get_softc(dev);
    int mask, timeout;

    /* ICH6 & ICH7 in compat mode has 4 SATA ports as master/slave on 2 ch's */
    if (ctlr->chip->cfg1) {
	mask = (0x0005 << ch->unit);
    }
    else {
	/* ICH5 in compat mode has SATA ports as master/slave on 1 channel */
	if (pci_read_config(parent, 0x90, 1) & 0x04)
	    mask = 0x0003;
	else {
	    mask = (0x0001 << ch->unit);
	    /* XXX SOS should be in intel_allocate if we grow it */
	    ch->flags |= ATA_NO_SLAVE;
	}
    }
    pci_write_config(parent, 0x92, pci_read_config(parent, 0x92, 2) & ~mask, 2);
    DELAY(10);
    pci_write_config(parent, 0x92, pci_read_config(parent, 0x92, 2) | mask, 2);

    /* wait up to 1 sec for "connect well" */
    for (timeout = 0; timeout < 100 ; timeout++) {
	if (((pci_read_config(parent, 0x92, 2) & (mask << 4)) == (mask << 4)) &&
	    (ATA_IDX_INB(ch, ATA_STATUS) != 0xff))
	    break;
	ata_udelay(10000);
    }
    ata_generic_reset(dev);
}

static void
ata_intel_old_setmode(device_t dev, int mode)
{
    /* NOT YET */
}

static void
ata_intel_new_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
    u_int32_t reg40 = pci_read_config(gparent, 0x40, 4);
    u_int8_t reg44 = pci_read_config(gparent, 0x44, 1);
    u_int8_t reg48 = pci_read_config(gparent, 0x48, 1);
    u_int16_t reg4a = pci_read_config(gparent, 0x4a, 2);
    u_int16_t reg54 = pci_read_config(gparent, 0x54, 2);
    u_int32_t mask40 = 0, new40 = 0;
    u_int8_t mask44 = 0, new44 = 0;
    int error;
    u_int8_t timings[] = { 0x00, 0x00, 0x10, 0x21, 0x23, 0x10, 0x21, 0x23,
			   0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23 };
			/* PIO0  PIO1  PIO2  PIO3  PIO4  WDMA0 WDMA1 WDMA2 */
			/* UDMA0 UDMA1 UDMA2 UDMA3 UDMA4 UDMA5 UDMA6 */

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    if ( mode > ATA_UDMA2 && !(reg54 & (0x10 << devno))) {
	ata_print_cable(dev, "controller");
	mode = ATA_UDMA2;
    }

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		      (error) ? "FAILURE " : "",
		      ata_mode2str(mode), ctlr->chip->text);
    if (error)
	return;

    /*
     * reg48: 1 bit per (primary drive 0, primary drive 1, secondary
     *			 drive 0, secondary drive 1)
     *
     *		0 Disable Ultra DMA mode
     *		1 Enable Ultra DMA mode
     *
     * reg4a: 4 bits per (primary drive 0, primary drive 1, secondary
     *			  drive 0, secondary drive 1).
     *		0000 UDMA mode 0
     *		0001 UDMA mode 1, 3, 5
     *		0010 UDMA mode 2, 4, reserved
     *		0011 reserved
     *		(top two bits for each drive reserved)
     */
#if 0
    device_printf(dev,
		  "regs before 40=%08x 44=%02x 48=%02x 4a=%04x 54=%04x\n",
		  reg40, reg44, reg48 ,reg4a, reg54);
#endif
    reg48 &= ~(0x0001 << devno);
    reg4a &= ~(0x3 << (devno << 2));
    if (mode >= ATA_UDMA0) {
	reg48 |= 0x0001 << devno;
	if (mode > ATA_UDMA0)
	    reg4a |= (1 + !(mode & 0x01)) << (devno << 2);
    }
    pci_write_config(gparent, 0x48, reg48, 2);
    pci_write_config(gparent, 0x4a, reg4a, 2);

    /*
     * reg54:
     *
     *	32:20	reserved
     *	19:18	Secondary ATA signal mode
     *	17:16	Primary ATA signal mode
     *		00 = Normal (enabled)
     *		01 = Tri-state (disabled)
     *		10 = Drive Low (disabled)
     *		11 = Reserved
     *
     *	15	Secondary drive 1	- Base Clock
     *	14	Secondary drive 0	- Base Clock
     *	13	Primary drive 1		- Base Clock
     *	12	Primary drive 0		- Base Clock
     *		0 = Select 33 MHz clock
     *		1 = Select 100 Mhz clock
     *
     *	11	Reserved
     *	10	Vendor specific (set by BIOS?)
     *	09:08	Reserved
     *
     *	07	Secondary drive 1	- Cable Type
     *	06	Secondary drive 0	- Cable Type
     *	05	Primary drive 1		- Cable Type
     *	04	Primary drive 0		- Cable Type
     *		0 = 40 Conductor
     *		1 = 80 Conductor (or high speed cable)
     *
     *	03	Secondary drive 1	- Select 33/66 clock
     *	02	Secondary drive 0	- Select 33/66 clock
     *	01	Primary drive 1		- Select 33/66 clock
     *	00	Primary drive 0		- Select 33/66 clock
     *		0 = Select 33 MHz
     *		1 = Select 66 MHz
     *
     *		It is unclear what this should be set to when operating
     *		in 100MHz mode.
     *
     * NOTE: UDMA2 = 33 MHz
     *	     UDMA3 = 40 MHz (?) - unsupported
     *	     UDMA4 = 66 MHz
     *	     UDMA5 = 100 MHz
     *	     UDMA6 = 133 Mhz
     */
    reg54 |= 0x0400;	/* set vendor specific bit */
    reg54 &= ~((0x1 << devno) | (0x1000 << devno));

    if (mode >= ATA_UDMA5)
	reg54 |= (0x1000 << devno);
    else if (mode >= ATA_UDMA3)	/* XXX should this be ATA_UDMA3 or 4? */
	reg54 |= (0x1 << devno);

    pci_write_config(gparent, 0x54, reg54, 2);

    /*
     * Reg40 (32 bits... well, actually two 16 bit registers)
     *
     * Primary channel bits 15:00, Secondary channel bits 31:00.  Note
     * that slave timings are handled in register 44.
     *
     * 15	ATA Decode Enable (R/W) 1 = enable decoding of I/O ranges
     *
     * 14	Slave ATA Timing Register Enable (R/W)
     *
     * 13:12	IORDY Sample Mode
     *		00	PIO-0
     *		01	PIO-2, SW-2
     *		10	PIO-3, PIO-4, MW-1, MW-2
     *		11	Reserved
     *
     * 11:10	Reserved
     *
     * 09:08	Recovery Mode
     *		00	PIO-0, PIO-2, SW-2
     *		01	PIO-3, MW-1
     *		10	Reserved
     *		11	PIO-4, MW-2
     *
     * 07:04	Secondary Device Control Bits
     * 03:00	Primary Device Control Bits
     *
     *		bit 3	DMA Timing Enable
     *
     *		bit 2	Indicate Presence of ATA(1) or ATAPI(0) device
     *
     *		bit 1	Enable IORDY sample point capability for PIO
     *			xfers.  Always enabled for PIO4 and PIO3, enabled
     *			for PIO2 if indicated by the device, and otherwise
     *			probably should be 0.
     *
     *		bit 0	Fast Drive Timing Enable.  Enables faster then PIO-0
     *			timing modes.
     */

    /*
     * Modify reg40 according to the table
     */
    if (atadev->unit == ATA_MASTER) {
	mask40 = 0x3300;
	new40 = timings[ata_mode2idx(mode)] << 8;
    }
    else {
	mask44 = 0x0f;
	new44 = ((timings[ata_mode2idx(mode)] & 0x30) >> 2) |
		(timings[ata_mode2idx(mode)] & 0x03);
    }

    /*
     * Slave ATA timing register enable
     */
    mask40 |= 0x4000;
    new40  |= 0x4000;

    /*
     * Device control bits 3:0 for master, 7:4 for slave.
     *
     * bit3 DMA Timing enable.
     * bit2 Indicate presence of ATA(1) or ATAPI(0) device, set accordingly
     * bit1 Enable IORDY sample point capability for PIO xfers.  Always
     *	    enabled for PIO4 and PIO3, enabled for PIO2 if indicated by
     *	    the device, and otherwise should be 0.
     * bit0 Fast Drive Timing Enable.  Enable faster then PIO-0 timing modes.
     *
     * Set to: 0 x 1 1
     */

    if (atadev->unit == ATA_MASTER) {
	mask40 |= 0x0F;
	new40 |= 0x03;
	if (!ata_atapi(dev))
	    new40 |= 0x04;
    } else {
	mask40 |= 0xF0;
	new40 |= 0x30;
	if (!ata_atapi(dev))
	    new40 |= 0x40;
    }
    /*
    reg40 &= ~0x00ff00ff;
    reg40 |= 0x40774077;
    */

    /*
     * Primary or Secondary controller
     */
    if (ch->unit) {
	mask40 <<= 16;
	new40 <<= 16;
	mask44 <<= 4;
	new44 <<= 4;
    }
    pci_write_config(gparent, 0x40, (reg40 & ~mask40) | new40, 4);
    pci_write_config(gparent, 0x44, (reg44 & ~mask44) | new44, 1);

#if 0
    reg40 = pci_read_config(gparent, 0x40, 4);
    reg44 = pci_read_config(gparent, 0x44, 1);
    reg48 = pci_read_config(gparent, 0x48, 1);
    reg4a = pci_read_config(gparent, 0x4a, 2);
    reg54 = pci_read_config(gparent, 0x54, 2);
    device_printf(dev,
		  "regs after 40=%08x 44=%02x 48=%02x 4a=%04x 54=%04x\n",
		  reg40, reg44, reg48 ,reg4a, reg54);
#endif

    atadev->mode = mode;
}

static void
ata_intel_sata_setmode(device_t dev, int mode)
{
    struct ata_device *atadev = device_get_softc(dev);

    if (atadev->param.satacapabilities != 0x0000 &&
	atadev->param.satacapabilities != 0xffff) {

	struct ata_channel *ch = device_get_softc(device_get_parent(dev));
	int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);

	/* on some drives we need to set the transfer mode */
	ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0,
		       ata_limit_mode(dev, mode, ATA_UDMA6));

	/* set ATA_SSTATUS register offset */
	ATA_IDX_OUTL(ch, ATA_IDX_ADDR, devno * 0x100);

	/* query SATA STATUS for the speed */
	if ((ATA_IDX_INL(ch, ATA_IDX_DATA) & ATA_SS_CONWELL_MASK) ==
	    ATA_SS_CONWELL_GEN2)
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
ata_intel_31244_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int i;
    int ch_offset;

    ch_offset = 0x200 + ch->unit * 0x200;

    for (i = ATA_DATA; i < ATA_MAX_RES; i++)
	ch->r_io[i].res = ctlr->r_res2;

    /* setup ATA registers */
    ch->r_io[ATA_DATA].offset = ch_offset + 0x00;
    ch->r_io[ATA_FEATURE].offset = ch_offset + 0x06;
    ch->r_io[ATA_COUNT].offset = ch_offset + 0x08;
    ch->r_io[ATA_SECTOR].offset = ch_offset + 0x0c;
    ch->r_io[ATA_CYL_LSB].offset = ch_offset + 0x10;
    ch->r_io[ATA_CYL_MSB].offset = ch_offset + 0x14;
    ch->r_io[ATA_DRIVE].offset = ch_offset + 0x18;
    ch->r_io[ATA_COMMAND].offset = ch_offset + 0x1d;
    ch->r_io[ATA_ERROR].offset = ch_offset + 0x04;
    ch->r_io[ATA_STATUS].offset = ch_offset + 0x1c;
    ch->r_io[ATA_ALTSTAT].offset = ch_offset + 0x28;
    ch->r_io[ATA_CONTROL].offset = ch_offset + 0x29;

    /* setup DMA registers */
    ch->r_io[ATA_SSTATUS].offset = ch_offset + 0x100;
    ch->r_io[ATA_SERROR].offset = ch_offset + 0x104;
    ch->r_io[ATA_SCONTROL].offset = ch_offset + 0x108;

    /* setup SATA registers */
    ch->r_io[ATA_BMCMD_PORT].offset = ch_offset + 0x70;
    ch->r_io[ATA_BMSTAT_PORT].offset = ch_offset + 0x72;
    ch->r_io[ATA_BMDTP_PORT].offset = ch_offset + 0x74;

    ch->flags |= ATA_NO_SLAVE;
    ata_pci_hw(dev);
    ch->hw.status = ata_intel_31244_status;
    ch->hw.command = ata_intel_31244_command;

    /* enable PHY state change interrupt */
    ATA_OUTL(ctlr->r_res2, 0x4,
	     ATA_INL(ctlr->r_res2, 0x04) | (0x01 << (ch->unit << 3)));
    return 0;
}

static int
ata_intel_31244_status(device_t dev)
{
    /* do we have any PHY events ? */
    ata_sata_phy_check_events(dev);

    /* any drive action to take care of ? */
    return ata_pci_status(dev);
}

static int
ata_intel_31244_command(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(device_get_parent(request->dev));
    struct ata_device *atadev = device_get_softc(request->dev);
    u_int64_t lba;

    if (!(atadev->flags & ATA_D_48BIT_ACTIVE))
	    return (ata_generic_command(request));

    lba = request->u.ata.lba;
    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | atadev->unit);
    /* enable interrupt */
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_4BIT);
    ATA_IDX_OUTW(ch, ATA_FEATURE, request->u.ata.feature);
    ATA_IDX_OUTW(ch, ATA_COUNT, request->u.ata.count);
    ATA_IDX_OUTW(ch, ATA_SECTOR, ((lba >> 16) & 0xff00) | (lba & 0x00ff));
    ATA_IDX_OUTW(ch, ATA_CYL_LSB, ((lba >> 24) & 0xff00) |
				  ((lba >> 8) & 0x00ff));
    ATA_IDX_OUTW(ch, ATA_CYL_MSB, ((lba >> 32) & 0xff00) |
				  ((lba >> 16) & 0x00ff));

    /* issue command to controller */
    ATA_IDX_OUTB(ch, ATA_COMMAND, request->u.ata.command);

    return 0;
}

static void
ata_intel_31244_reset(device_t dev)
{
    if (ata_sata_phy_reset(dev))
	ata_generic_reset(dev);
}
