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
static int ata_cmd_allocate(device_t dev);
static int ata_cmd_status(device_t dev);
static void ata_cmd_setmode(device_t dev, int mode);
static int ata_sii_allocate(device_t dev);
static int ata_sii_status(device_t dev);
static void ata_sii_reset(device_t dev);
static void ata_sii_setmode(device_t dev, int mode);
static int ata_siiprb_allocate(device_t dev);
static int ata_siiprb_status(device_t dev);
static int ata_siiprb_begin_transaction(struct ata_request *request);
static int ata_siiprb_end_transaction(struct ata_request *request);
static void ata_siiprb_reset(device_t dev);
static void ata_siiprb_dmasetprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static void ata_siiprb_dmainit(device_t dev);

/* misc defines */
#define SII_MEMIO	1
#define SII_PRBIO	2
#define SII_INTR	0x01
#define SII_SETCLK	0x02
#define SII_BUG		0x04
#define SII_4CH		0x08

/*
 * Silicon Image Inc. (SiI) (former CMD) chipset support functions
 */
int
ata_sii_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_SII3114,   0x00, SII_MEMIO, SII_4CH,    ATA_SA150, "SiI 3114" },
     { ATA_SII3512,   0x02, SII_MEMIO, 0,          ATA_SA150, "SiI 3512" },
     { ATA_SII3112,   0x02, SII_MEMIO, 0,          ATA_SA150, "SiI 3112" },
     { ATA_SII3112_1, 0x02, SII_MEMIO, 0,          ATA_SA150, "SiI 3112" },
     { ATA_SII3512,   0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "SiI 3512" },
     { ATA_SII3112,   0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "SiI 3112" },
     { ATA_SII3112_1, 0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "SiI 3112" },
     { ATA_SII3124,   0x00, SII_PRBIO, SII_4CH,    ATA_SA300, "SiI 3124" },
     { ATA_SII3132,   0x00, SII_PRBIO, 0,          ATA_SA300, "SiI 3132" },
     { ATA_SII0680,   0x00, SII_MEMIO, SII_SETCLK, ATA_UDMA6, "SiI 0680" },
     { ATA_CMD649,    0x00, 0,         SII_INTR,   ATA_UDMA5, "CMD 649" },
     { ATA_CMD648,    0x00, 0,         SII_INTR,   ATA_UDMA4, "CMD 648" },
     { ATA_CMD646,    0x07, 0,         0,          ATA_UDMA2, "CMD 646U2" },
     { ATA_CMD646,    0x00, 0,         0,          ATA_WDMA2, "CMD 646" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_SILICON_IMAGE_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_sii_chipinit;
    return 0;
}

static int
ata_sii_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev))
	return ENXIO;

    switch (ctlr->chip->cfg1) {
    case SII_PRBIO:
	ctlr->r_type1 = SYS_RES_MEMORY;
	ctlr->r_rid1 = PCIR_BAR(0);
	if (!(ctlr->r_res1 = bus_alloc_resource_any(dev, ctlr->r_type1,
						    &ctlr->r_rid1, RF_ACTIVE))){
	    ata_teardown_interrupt(dev);
	    return ENXIO;
	}

	ctlr->r_rid2 = PCIR_BAR(2);
	ctlr->r_type2 = SYS_RES_MEMORY;
	if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						    &ctlr->r_rid2, RF_ACTIVE))){
	    bus_release_resource(dev, ctlr->r_type1, ctlr->r_rid1,ctlr->r_res1);
	    ata_teardown_interrupt(dev);
	    return ENXIO;
	}
	ctlr->allocate = ata_siiprb_allocate;
	ctlr->reset = ata_siiprb_reset;
	ctlr->dmainit = ata_siiprb_dmainit;
	ctlr->setmode = ata_sata_setmode;
	ctlr->channels = (ctlr->chip->cfg2 == SII_4CH) ? 4 : 2;

	/* reset controller */
	ATA_OUTL(ctlr->r_res1, 0x0040, 0x80000000);
	DELAY(10000);
	ATA_OUTL(ctlr->r_res1, 0x0040, 0x0000000f);

	/* enable PCI interrupt */
	pci_write_config(dev, PCIR_COMMAND,
			 pci_read_config(dev, PCIR_COMMAND, 2) & ~0x0400, 2);
	break;

    case SII_MEMIO:
	ctlr->r_type2 = SYS_RES_MEMORY;
	ctlr->r_rid2 = PCIR_BAR(5);
	if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						    &ctlr->r_rid2, RF_ACTIVE))){
	    ata_teardown_interrupt(dev);
	    return ENXIO;
	}

	if (ctlr->chip->cfg2 & SII_SETCLK) {
	    if ((pci_read_config(dev, 0x8a, 1) & 0x30) != 0x10)
		pci_write_config(dev, 0x8a,
				 (pci_read_config(dev, 0x8a, 1) & 0xcf)|0x10,1);
	    if ((pci_read_config(dev, 0x8a, 1) & 0x30) != 0x10)
		device_printf(dev, "%s could not set ATA133 clock\n",
			      ctlr->chip->text);
	}

	/* if we have 4 channels enable the second set */
	if (ctlr->chip->cfg2 & SII_4CH) {
	    ATA_OUTL(ctlr->r_res2, 0x0200, 0x00000002);
	    ctlr->channels = 4;
	}

	/* dont block interrupts from any channel */
	pci_write_config(dev, 0x48,
			 (pci_read_config(dev, 0x48, 4) & ~0x03c00000), 4);

	/* enable PCI interrupt as BIOS might not */
	pci_write_config(dev, 0x8a, (pci_read_config(dev, 0x8a, 1) & 0x3f), 1);

	ctlr->allocate = ata_sii_allocate;
	if (ctlr->chip->max_dma >= ATA_SA150) {
	    ctlr->reset = ata_sii_reset;
	    ctlr->setmode = ata_sata_setmode;
	}
	else
	    ctlr->setmode = ata_sii_setmode;
	break;

    default:
	if ((pci_read_config(dev, 0x51, 1) & 0x08) != 0x08) {
	    device_printf(dev, "HW has secondary channel disabled\n");
	    ctlr->channels = 1;
	}

	/* enable interrupt as BIOS might not */
	pci_write_config(dev, 0x71, 0x01, 1);

	ctlr->allocate = ata_cmd_allocate;
	ctlr->setmode = ata_cmd_setmode;
	break;
    }
    return 0;
}

static int
ata_cmd_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* setup the usual register normal pci style */
    if (ata_pci_allocate(dev))
	return ENXIO;

    if (ctlr->chip->cfg2 & SII_INTR)
	ch->hw.status = ata_cmd_status;

    return 0;
}

static int
ata_cmd_status(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    u_int8_t reg71;

    if (((reg71 = pci_read_config(device_get_parent(ch->dev), 0x71, 1)) &
	 (ch->unit ? 0x08 : 0x04))) {
	pci_write_config(device_get_parent(ch->dev), 0x71,
			 reg71 & ~(ch->unit ? 0x04 : 0x08), 1);
	return ata_pci_status(dev);
    }
    return 0;
}

static void
ata_cmd_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
    int error;
	int treg = 0x54 + ((devno < 3) ? (devno << 1) : 7);
	int ureg = ch->unit ? 0x7b : 0x73;
	static const uint8_t piotimings[] =
	    { 0xa9, 0x57, 0x44, 0x32, 0x3f };
	static const uint8_t dmatimings[] = { 0x87, 0x32, 0x3f };
	static const uint8_t udmatimings[][2] =
	    { { 0x31,  0xc2 }, { 0x21,  0x82 }, { 0x11,  0x42 },
	      { 0x25,  0x8a }, { 0x15,  0x4a }, { 0x05,  0x0a } };

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    mode = ata_check_80pin(dev, mode);

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		      (error) ? "FAILURE " : "",
		      ata_mode2str(mode), ctlr->chip->text);
    if (!error) {
	if (mode >= ATA_UDMA0) {
	    u_int8_t umode = pci_read_config(gparent, ureg, 1);

	    umode &= ~(atadev->unit == ATA_MASTER ? 0x35 : 0xca);
	    umode |= udmatimings[mode & ATA_MODE_MASK][ATA_DEV(atadev->unit)];
	    pci_write_config(gparent, ureg, umode, 1);
	}
	else if (mode >= ATA_WDMA0) {
	    pci_write_config(gparent, treg, dmatimings[mode & ATA_MODE_MASK],1);
	    pci_write_config(gparent, ureg,
			     pci_read_config(gparent, ureg, 1) &
			     ~(atadev->unit == ATA_MASTER ? 0x35 : 0xca), 1);
	}
	else {
	    pci_write_config(gparent, treg,
			     piotimings[(mode & ATA_MODE_MASK) - ATA_PIO0], 1);
	    pci_write_config(gparent, ureg,
			     pci_read_config(gparent, ureg, 1) &
			     ~(atadev->unit == ATA_MASTER ? 0x35 : 0xca), 1);
	}
	atadev->mode = mode;
    }
}

static int
ata_sii_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int unit01 = (ch->unit & 1), unit10 = (ch->unit & 2);
    int i;

    for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
	ch->r_io[i].res = ctlr->r_res2;
	ch->r_io[i].offset = 0x80 + i + (unit01 << 6) + (unit10 << 8);
    }
    ch->r_io[ATA_CONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_CONTROL].offset = 0x8a + (unit01 << 6) + (unit10 << 8);
    ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
    ata_default_registers(dev);

    ch->r_io[ATA_BMCMD_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMCMD_PORT].offset = 0x00 + (unit01 << 3) + (unit10 << 8);
    ch->r_io[ATA_BMSTAT_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMSTAT_PORT].offset = 0x02 + (unit01 << 3) + (unit10 << 8);
    ch->r_io[ATA_BMDTP_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMDTP_PORT].offset = 0x04 + (unit01 << 3) + (unit10 << 8);

    if (ctlr->chip->max_dma >= ATA_SA150) {
	ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
	ch->r_io[ATA_SSTATUS].offset = 0x104 + (unit01 << 7) + (unit10 << 8);
	ch->r_io[ATA_SERROR].res = ctlr->r_res2;
	ch->r_io[ATA_SERROR].offset = 0x108 + (unit01 << 7) + (unit10 << 8);
	ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
	ch->r_io[ATA_SCONTROL].offset = 0x100 + (unit01 << 7) + (unit10 << 8);
	ch->flags |= ATA_NO_SLAVE;

	/* enable PHY state change interrupt */
	ATA_OUTL(ctlr->r_res2, 0x148 + (unit01 << 7) + (unit10 << 8),(1 << 16));
    }

    if ((ctlr->chip->cfg2 & SII_BUG) && ch->dma) {
	/* work around errata in early chips */
	ch->dma->boundary = 16 * DEV_BSIZE;
	ch->dma->segsize = 15 * DEV_BSIZE;
    }

    ata_pci_hw(dev);
    ch->hw.status = ata_sii_status;
    return 0;
}

static int
ata_sii_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset0 = ((ch->unit & 1) << 3) + ((ch->unit & 2) << 8);
    int offset1 = ((ch->unit & 1) << 6) + ((ch->unit & 2) << 8);

    /* do we have any PHY events ? */
    if (ctlr->chip->max_dma >= ATA_SA150 &&
	(ATA_INL(ctlr->r_res2, 0x10 + offset0) & 0x00000010))
	ata_sata_phy_check_events(dev);

    if (ATA_INL(ctlr->r_res2, 0xa0 + offset1) & 0x00000800)
	return ata_pci_status(dev);
    else
	return 0;
}

static void
ata_sii_reset(device_t dev)
{
    if (ata_sata_phy_reset(dev))
	ata_generic_reset(dev);
}

static void
ata_sii_setmode(device_t dev, int mode)
{
	device_t gparent = GRANDPARENT(dev);
	struct ata_pci_controller *ctlr = device_get_softc(gparent);
	struct ata_channel *ch = device_get_softc(device_get_parent(dev));
	struct ata_device *atadev = device_get_softc(dev);
	int rego = (ch->unit << 4) + (ATA_DEV(atadev->unit) << 1);
	int mreg = ch->unit ? 0x84 : 0x80;
	int mask = 0x03 << (ATA_DEV(atadev->unit) << 2);
	int mval = pci_read_config(gparent, mreg, 1) & ~mask;
	int error;
	u_int8_t preg = 0xa4 + rego;
	u_int8_t dreg = 0xa8 + rego;
	u_int8_t ureg = 0xac + rego;
	static const uint16_t piotimings[] =
	    { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1 };
	static const uint16_t dmatimings[] = { 0x2208, 0x10c2, 0x10c1 };
	static const uint8_t udmatimings[] =
	    { 0xf, 0xb, 0x7, 0x5, 0x3, 0x2, 0x1 };

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    if (ctlr->chip->cfg2 & SII_SETCLK) {
	if (mode > ATA_UDMA2 && (pci_read_config(gparent, 0x79, 1) &
				 (ch->unit ? 0x02 : 0x01))) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
    }
    else
	mode = ata_check_80pin(dev, mode);

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		      (error) ? "FAILURE " : "",
		      ata_mode2str(mode), ctlr->chip->text);
    if (error)
	return;

    if (mode >= ATA_UDMA0) {
	pci_write_config(gparent, mreg,
			 mval | (0x03 << (ATA_DEV(atadev->unit) << 2)), 1);
	pci_write_config(gparent, ureg,
			 (pci_read_config(gparent, ureg, 1) & ~0x3f) |
			 udmatimings[mode & ATA_MODE_MASK], 1);

    }
    else if (mode >= ATA_WDMA0) {
	pci_write_config(gparent, mreg,
			 mval | (0x02 << (ATA_DEV(atadev->unit) << 2)), 1);
	pci_write_config(gparent, dreg, dmatimings[mode & ATA_MODE_MASK], 2);

    }
    else {
	pci_write_config(gparent, mreg,
			 mval | (0x01 << (ATA_DEV(atadev->unit) << 2)), 1);
	pci_write_config(gparent, preg, piotimings[mode & ATA_MODE_MASK], 2);
    }
    atadev->mode = mode;
}

struct ata_siiprb_dma_prdentry {
    u_int64_t addr;
    u_int32_t count;
    u_int32_t control;
} __packed;

struct ata_siiprb_ata_command {
    u_int32_t reserved0;
    struct ata_siiprb_dma_prdentry prd[126];
} __packed;

struct ata_siiprb_atapi_command {
    u_int8_t cdb[16];
    struct ata_siiprb_dma_prdentry prd[125];
} __packed;

struct ata_siiprb_command {
    u_int16_t control;
    u_int16_t protocol_override;
    u_int32_t transfer_count;
    u_int8_t fis[20];
    union {
	struct ata_siiprb_ata_command ata;
	struct ata_siiprb_atapi_command atapi;
    } u;
} __packed;

static int
ata_siiprb_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = ch->unit * 0x2000;

    /* set the SATA resources */
    ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
    ch->r_io[ATA_SSTATUS].offset = 0x1f04 + offset;
    ch->r_io[ATA_SERROR].res = ctlr->r_res2;
    ch->r_io[ATA_SERROR].offset = 0x1f08 + offset;
    ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_SCONTROL].offset = 0x1f00 + offset;
    ch->r_io[ATA_SACTIVE].res = ctlr->r_res2;
    ch->r_io[ATA_SACTIVE].offset = 0x1f0c + offset;

    ch->hw.begin_transaction = ata_siiprb_begin_transaction;
    ch->hw.end_transaction = ata_siiprb_end_transaction;
    ch->hw.status = ata_siiprb_status;
    ch->hw.command = NULL;	/* not used here */
    return 0;
}

static int
ata_siiprb_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = ch->unit * 0x2000;

    if ((ATA_INL(ctlr->r_res1, 0x0044) & (1 << ch->unit))) {
	u_int32_t istatus = ATA_INL(ctlr->r_res2, 0x1008 + offset);

	/* do we have any PHY events ? */
	ata_sata_phy_check_events(dev);

	/* clear interrupt(s) */
	ATA_OUTL(ctlr->r_res2, 0x1008 + offset, istatus);

	/* do we have any device action ? */
	return (istatus & 0x00000001);
    }
    return 0;
}

static int
ata_siiprb_begin_transaction(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(GRANDPARENT(request->dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(request->dev));
    struct ata_siiprb_command *prb;
    int offset = ch->unit * 0x2000;
    u_int64_t prb_bus;
    int tag = 0, dummy;

    /* check for 48 bit access and convert if needed */
    ata_modify_if_48bit(request);

    /* get a piece of the workspace for this request */
    prb = (struct ata_siiprb_command *)
	(ch->dma->work + (sizeof(struct ata_siiprb_command) * tag));

    /* set basic prd options ata/atapi etc etc */
    bzero(prb, sizeof(struct ata_siiprb_command));

    /* setup the FIS for this request */
    if (!ata_request2fis_h2d(request, &prb->fis[0])) {
        device_printf(request->dev, "setting up SATA FIS failed\n");
        request->result = EIO;
        return ATA_OP_FINISHED;
    }

    /* if request moves data setup and load SG list */
    if (request->flags & (ATA_R_READ | ATA_R_WRITE)) {
	struct ata_siiprb_dma_prdentry *prd;

	if (request->flags & ATA_R_ATAPI)
	    prd = &prb->u.atapi.prd[0];
	else
	    prd = &prb->u.ata.prd[0];
	if (ch->dma->load(ch->dev, request->data, request->bytecount,
			  request->flags & ATA_R_READ, prd, &dummy)) {
	    device_printf(request->dev, "setting up DMA failed\n");
	    request->result = EIO;
	    return ATA_OP_FINISHED;
	}
    }

    /* activate the prb */
    prb_bus = ch->dma->work_bus + (sizeof(struct ata_siiprb_command) * tag);
    ATA_OUTL(ctlr->r_res2,
	     0x1c00 + offset + (tag * sizeof(u_int64_t)), prb_bus);
    ATA_OUTL(ctlr->r_res2,
	     0x1c04 + offset + (tag * sizeof(u_int64_t)), prb_bus>>32);

    /* start the timeout */
    callout_reset(&request->callout, request->timeout * hz,
                  (timeout_t*)ata_timeout, request);
    return ATA_OP_CONTINUES;
}

static int
ata_siiprb_end_transaction(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(GRANDPARENT(request->dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(request->dev));
    struct ata_siiprb_command *prb;
    int offset = ch->unit * 0x2000;
    int error, tag = 0;

    /* kill the timeout */
    callout_stop_sync(&request->callout);

    prb = (struct ata_siiprb_command *)
	((u_int8_t *)rman_get_virtual(ctlr->r_res2) + (tag << 7) + offset);

    /* if error status get details */
    request->status = prb->fis[2];
    if (request->status & ATA_S_ERROR)
	request->error = prb->fis[3];

    /* update progress */
    if (!(request->status & ATA_S_ERROR) && !(request->flags & ATA_R_TIMEOUT)) {
	if (request->flags & ATA_R_READ)
	    request->donecount = prb->transfer_count;
	else
	    request->donecount = request->bytecount;
    }

    /* any controller errors flagged ? */
    if ((error = ATA_INL(ctlr->r_res2, 0x1024 + offset))) {
	kprintf("ata_siiprb_end_transaction %s error=%08x\n",
		ata_cmd2str(request), error);
    }

    /* release SG list etc */
    ch->dma->unload(ch->dev);

    return ATA_OP_FINISHED;
}

static void
ata_siiprb_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = ch->unit * 0x2000;
    struct ata_siiprb_command *prb;
    u_int64_t prb_bus;
    u_int32_t status, signature;
    int timeout, tag = 0;

    /* reset channel HW */
    ATA_OUTL(ctlr->r_res2, 0x1000 + offset, 0x00000001);
    DELAY(1000);
    ATA_OUTL(ctlr->r_res2, 0x1004 + offset, 0x00000001);
    DELAY(10000);

    /* poll for channel ready */
    for (timeout = 0; timeout < 1000; timeout++) {
        if ((status = ATA_INL(ctlr->r_res2, 0x1000 + offset)) & 0x00040000)
            break;
        DELAY(1000);
    }
    if (timeout >= 1000) {
	device_printf(ch->dev, "channel HW reset timeout reset failure\n");
	ch->devices = 0;
	goto finish;
    }
    if (bootverbose)
	device_printf(ch->dev, "channel HW reset time=%dms\n", timeout * 1);

    /* reset phy */
    if (!ata_sata_phy_reset(dev)) {
	if (bootverbose)
	    device_printf(ch->dev, "phy reset found no device\n");
	ch->devices = 0;
	goto finish;
    }

    /* get a piece of the workspace for a soft reset request */
    prb = (struct ata_siiprb_command *)
	(ch->dma->work + (sizeof(struct ata_siiprb_command) * tag));
    bzero(prb, sizeof(struct ata_siiprb_command));
    prb->control = htole16(0x0080);

    /* activate the soft reset prb */
    prb_bus = ch->dma->work_bus + (sizeof(struct ata_siiprb_command) * tag);
    ATA_OUTL(ctlr->r_res2,
	     0x1c00 + offset + (tag * sizeof(u_int64_t)), prb_bus);
    ATA_OUTL(ctlr->r_res2,
	     0x1c04 + offset + (tag * sizeof(u_int64_t)), prb_bus>>32);

    /* poll for channel ready */
    for (timeout = 0; timeout < 1000; timeout++) {
        DELAY(1000);
        if ((status = ATA_INL(ctlr->r_res2, 0x1008 + offset)) & 0x00010000)
            break;
    }
    if (timeout >= 1000) {
	device_printf(ch->dev, "reset timeout - no device found\n");
	ch->devices = 0;
	goto finish;
    }
    if (bootverbose)
	device_printf(ch->dev, "soft reset exec time=%dms status=%08x\n",
			timeout, status);

    /* find out whats there */
    prb = (struct ata_siiprb_command *)
	((u_int8_t *)rman_get_virtual(ctlr->r_res2) + (tag << 7) + offset);
    signature =
	prb->fis[12]|(prb->fis[4]<<8)|(prb->fis[5]<<16)|(prb->fis[6]<<24);
    if (bootverbose)
	device_printf(ch->dev, "signature=%08x\n", signature);
    switch (signature) {
    case 0xeb140101:
	ch->devices = ATA_ATAPI_MASTER;
	device_printf(ch->dev, "SATA ATAPI devices not supported yet\n");
	ch->devices = 0;
	break;
    case 0x96690101:
	ch->devices = ATA_PORTMULTIPLIER;
	device_printf(ch->dev, "Portmultipliers not supported yet\n");
	ch->devices = 0;
	break;
    case 0x00000101:
	ch->devices = ATA_ATA_MASTER;
	break;
    default:
	ch->devices = 0;
    }

finish:
    /* clear interrupt(s) */
    ATA_OUTL(ctlr->r_res2, 0x1008 + offset, 0x000008ff);

    /* require explicit interrupt ack */
    ATA_OUTL(ctlr->r_res2, 0x1000 + offset, 0x00000008);

    /* 64bit mode */
    ATA_OUTL(ctlr->r_res2, 0x1004 + offset, 0x00000400);

    /* enable interrupts wanted */
    ATA_OUTL(ctlr->r_res2, 0x1010 + offset, 0x000000ff);
}

static void
ata_siiprb_dmasetprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
    struct ata_dmasetprd_args *args = xsc;
    struct ata_siiprb_dma_prdentry *prd = args->dmatab;
    int i;

    if ((args->error = error))
	return;

    for (i = 0; i < nsegs; i++) {
	prd[i].addr = htole64(segs[i].ds_addr);
	prd[i].count = htole32(segs[i].ds_len);
    }
    prd[i - 1].control = htole32(ATA_DMA_EOT);
}

static void
ata_siiprb_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_dmainit(dev);
    if (ch->dma) {
	/* note start and stop are not used here */
	ch->dma->setprd = ata_siiprb_dmasetprd;
	ch->dma->max_address = BUS_SPACE_MAXADDR;
    }
}
