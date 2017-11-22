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
static int ata_promise_chipinit(device_t dev);
static int ata_promise_allocate(device_t dev);
static int ata_promise_status(device_t dev);
static int ata_promise_dmastart(device_t dev);
static int ata_promise_dmastop(device_t dev);
static void ata_promise_dmareset(device_t dev);
static void ata_promise_dmainit(device_t dev);
static void ata_promise_setmode(device_t dev, int mode);
static int ata_promise_tx2_allocate(device_t dev);
static int ata_promise_tx2_status(device_t dev);
static int ata_promise_mio_allocate(device_t dev);
static void ata_promise_mio_intr(void *data);
static int ata_promise_mio_status(device_t dev);
static int ata_promise_mio_command(struct ata_request *request);
static void ata_promise_mio_reset(device_t dev);
static void ata_promise_mio_dmainit(device_t dev);
static void ata_promise_mio_setmode(device_t dev, int mode);
static void ata_promise_sx4_intr(void *data);
static int ata_promise_sx4_command(struct ata_request *request);
static int ata_promise_apkt(u_int8_t *bytep, struct ata_request *request);
static void ata_promise_queue_hpkt(struct ata_pci_controller *ctlr, u_int32_t hpkt);
static void ata_promise_next_hpkt(struct ata_pci_controller *ctlr);

#define ATA_PDC_APKT_OFFSET     0x00000010
#define ATA_PDC_HPKT_OFFSET     0x00000040
#define ATA_PDC_ASG_OFFSET      0x00000080
#define ATA_PDC_LSG_OFFSET      0x000000c0
#define ATA_PDC_HSG_OFFSET      0x00000100
#define ATA_PDC_CHN_OFFSET      0x00000400
#define ATA_PDC_BUF_BASE        0x00400000
#define ATA_PDC_BUF_OFFSET      0x00100000
#define ATA_PDC_MAX_HPKT        8
#define ATA_PDC_WRITE_REG       0x00
#define ATA_PDC_WRITE_CTL       0x0e
#define ATA_PDC_WRITE_END       0x08
#define ATA_PDC_WAIT_NBUSY      0x10
#define ATA_PDC_WAIT_READY      0x18
#define ATA_PDC_1B              0x20
#define ATA_PDC_2B              0x40

struct host_packet {
    u_int32_t                   addr;
    TAILQ_ENTRY(host_packet)    chain;
};

struct ata_promise_sx4 {
    struct lock                 mtx;
    TAILQ_HEAD(, host_packet)   queue;
    int                         busy;
};

/*
 * Promise chipset support functions
 */
int
ata_promise_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    struct ata_chip_id *idx;
    static struct ata_chip_id ids[] =
    {{ ATA_PDC20246,  0, PROLD, 0x00,    ATA_UDMA2, "PDC20246" },
     { ATA_PDC20262,  0, PRNEW, 0x00,    ATA_UDMA4, "PDC20262" },
     { ATA_PDC20263,  0, PRNEW, 0x00,    ATA_UDMA4, "PDC20263" },
     { ATA_PDC20265,  0, PRNEW, 0x00,    ATA_UDMA5, "PDC20265" },
     { ATA_PDC20267,  0, PRNEW, 0x00,    ATA_UDMA5, "PDC20267" },
     { ATA_PDC20268,  0, PRTX,  PRTX4,   ATA_UDMA5, "PDC20268" },
     { ATA_PDC20269,  0, PRTX,  0x00,    ATA_UDMA6, "PDC20269" },
     { ATA_PDC20270,  0, PRTX,  PRTX4,   ATA_UDMA5, "PDC20270" },
     { ATA_PDC20271,  0, PRTX,  0x00,    ATA_UDMA6, "PDC20271" },
     { ATA_PDC20275,  0, PRTX,  0x00,    ATA_UDMA6, "PDC20275" },
     { ATA_PDC20276,  0, PRTX,  PRSX6K,  ATA_UDMA6, "PDC20276" },
     { ATA_PDC20277,  0, PRTX,  0x00,    ATA_UDMA6, "PDC20277" },
     { ATA_PDC20318,  0, PRMIO, PRSATA,  ATA_SA150, "PDC20318" },
     { ATA_PDC20319,  0, PRMIO, PRSATA,  ATA_SA150, "PDC20319" },
     { ATA_PDC20371,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20371" },
     { ATA_PDC20375,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20375" },
     { ATA_PDC20376,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20376" },
     { ATA_PDC20377,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20377" },
     { ATA_PDC20378,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20378" },
     { ATA_PDC20379,  0, PRMIO, PRCMBO,  ATA_SA150, "PDC20379" },
     { ATA_PDC20571,  0, PRMIO, PRCMBO2, ATA_SA150, "PDC20571" },
     { ATA_PDC20575,  0, PRMIO, PRCMBO2, ATA_SA150, "PDC20575" },
     { ATA_PDC20579,  0, PRMIO, PRCMBO2, ATA_SA150, "PDC20579" },
     { ATA_PDC20771,  0, PRMIO, PRCMBO2, ATA_SA300, "PDC20771" },
     { ATA_PDC40775,  0, PRMIO, PRCMBO2, ATA_SA300, "PDC40775" },
     { ATA_PDC20617,  0, PRMIO, PRPATA,  ATA_UDMA6, "PDC20617" },
     { ATA_PDC20618,  0, PRMIO, PRPATA,  ATA_UDMA6, "PDC20618" },
     { ATA_PDC20619,  0, PRMIO, PRPATA,  ATA_UDMA6, "PDC20619" },
     { ATA_PDC20620,  0, PRMIO, PRPATA,  ATA_UDMA6, "PDC20620" },
     { ATA_PDC20621,  0, PRMIO, PRSX4X,  ATA_UDMA5, "PDC20621" },
     { ATA_PDC20622,  0, PRMIO, PRSX4X,  ATA_SA150, "PDC20622" },
     { ATA_PDC40518,  0, PRMIO, PRSATA2, ATA_SA150, "PDC40518" },
     { ATA_PDC40519,  0, PRMIO, PRSATA2, ATA_SA150, "PDC40519" },
     { ATA_PDC40718,  0, PRMIO, PRSATA2, ATA_SA300, "PDC40718" },
     { ATA_PDC40719,  0, PRMIO, PRSATA2, ATA_SA300, "PDC40719" },
     { ATA_PDC40779,  0, PRMIO, PRSATA2, ATA_SA300, "PDC40779" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];
    uintptr_t devid = 0;

    if (!(idx = ata_match_chip(dev, ids)))
	return ENXIO;

    /* if we are on a SuperTrak SX6000 dont attach */
    if ((idx->cfg2 & PRSX6K) && pci_get_class(GRANDPARENT(dev))==PCIC_BRIDGE &&
	!BUS_READ_IVAR(device_get_parent(GRANDPARENT(dev)),
		       GRANDPARENT(dev), PCI_IVAR_DEVID, &devid) &&
	devid == ATA_I960RM)
	return ENXIO;

    strcpy(buffer, "Promise ");
    strcat(buffer, idx->text);

    /* if we are on a FastTrak TX4, adjust the interrupt resource */
    if ((idx->cfg2 & PRTX4) && pci_get_class(GRANDPARENT(dev))==PCIC_BRIDGE &&
	!BUS_READ_IVAR(device_get_parent(GRANDPARENT(dev)),
		       GRANDPARENT(dev), PCI_IVAR_DEVID, &devid) &&
	((devid == ATA_DEC_21150) || (devid == ATA_DEC_21150_1))) {
	static long start = 0, end = 0;

	if (pci_get_slot(dev) == 1) {
	    bus_get_resource(dev, SYS_RES_IRQ, 0, &start, &end);
	    strcat(buffer, " (channel 0+1)");
	}
	else if (pci_get_slot(dev) == 2 && start && end) {
	    bus_set_resource(dev, SYS_RES_IRQ, 0, start, end,
	        machintr_legacy_intr_cpuid(start));
	    strcat(buffer, " (channel 2+3)");
	}
	else {
	    start = end = 0;
	}
    }
    ksprintf(buffer, "%s %s controller", buffer, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_promise_chipinit;
    return 0;
}

static int
ata_promise_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int fake_reg, stat_reg;

    if (ata_setup_interrupt(dev))
	return ENXIO;

    switch  (ctlr->chip->cfg1) {
    case PRNEW:
	/* setup clocks */
	ATA_OUTB(ctlr->r_res1, 0x11, ATA_INB(ctlr->r_res1, 0x11) | 0x0a);

	ctlr->dmainit = ata_promise_dmainit;
	/* FALLTHROUGH */

    case PROLD:
	/* enable burst mode */
	ATA_OUTB(ctlr->r_res1, 0x1f, ATA_INB(ctlr->r_res1, 0x1f) | 0x01);
	ctlr->allocate = ata_promise_allocate;
	ctlr->setmode = ata_promise_setmode;
	return 0;

    case PRTX:
	ctlr->allocate = ata_promise_tx2_allocate;
	ctlr->setmode = ata_promise_setmode;
	return 0;

    case PRMIO:
	ctlr->r_type1 = SYS_RES_MEMORY;
	ctlr->r_rid1 = PCIR_BAR(4);
	if (!(ctlr->r_res1 = bus_alloc_resource_any(dev, ctlr->r_type1,
						    &ctlr->r_rid1, RF_ACTIVE)))
	    goto failnfree;

	ctlr->r_type2 = SYS_RES_MEMORY;
	ctlr->r_rid2 = PCIR_BAR(3);
	if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						    &ctlr->r_rid2, RF_ACTIVE)))
	    goto failnfree;

	if (ctlr->chip->cfg2 == PRSX4X) {
	    struct ata_promise_sx4 *hpkt;
	    u_int32_t dimm = ATA_INL(ctlr->r_res2, 0x000c0080);

	    if (bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle) ||
		bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS,
			       ata_promise_sx4_intr, ctlr, &ctlr->handle, NULL)) {
		device_printf(dev, "unable to setup interrupt\n");
		goto failnfree;
	    }

	    /* print info about cache memory */
	    device_printf(dev, "DIMM size %dMB @ 0x%08x%s\n",
			  (((dimm >> 16) & 0xff)-((dimm >> 24) & 0xff)+1) << 4,
			  ((dimm >> 24) & 0xff),
			  ATA_INL(ctlr->r_res2, 0x000c0088) & (1<<16) ?
			  " ECC enabled" : "" );

	    /* adjust cache memory parameters */
	    ATA_OUTL(ctlr->r_res2, 0x000c000c,
		     (ATA_INL(ctlr->r_res2, 0x000c000c) & 0xffff0000));

	    /* setup host packet controls */
	    hpkt = kmalloc(sizeof(struct ata_promise_sx4),
			  M_TEMP, M_INTWAIT | M_ZERO);
	    lockinit(&hpkt->mtx, "chipinit", 0, 0);
	    TAILQ_INIT(&hpkt->queue);
	    hpkt->busy = 0;
	    device_set_ivars(dev, hpkt);
	    ctlr->allocate = ata_promise_mio_allocate;
	    ctlr->reset = ata_promise_mio_reset;
	    ctlr->dmainit = ata_promise_mio_dmainit;
	    ctlr->setmode = ata_promise_setmode;
	    ctlr->channels = 4;
	    return 0;
	}

	/* mio type controllers need an interrupt intercept */
	if (bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle) ||
	    bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS,
			       ata_promise_mio_intr, ctlr, &ctlr->handle, NULL)) {
		device_printf(dev, "unable to setup interrupt\n");
		goto failnfree;
	}

	switch (ctlr->chip->cfg2) {
	case PRPATA:
	    ctlr->channels = ((ATA_INL(ctlr->r_res2, 0x48) & 0x01) > 0) +
			     ((ATA_INL(ctlr->r_res2, 0x48) & 0x02) > 0) + 2;
	    goto sata150;
	case PRCMBO:
	    ctlr->channels = 3;
	    goto sata150;
	case PRSATA:
	    ctlr->channels = 4;
sata150:
	    fake_reg = 0x60;
	    stat_reg = 0x6c;
	    break;

	case PRCMBO2:
	    ctlr->channels = 3;
	    goto sataii;
	case PRSATA2:
	default:
	    ctlr->channels = 4;
sataii:
	    fake_reg = 0x54;
	    stat_reg = 0x60;
	    break;
	}

	/* prime fake interrupt register */
	ATA_OUTL(ctlr->r_res2, fake_reg, 0xffffffff);

	/* clear SATA status */
	ATA_OUTL(ctlr->r_res2, stat_reg, 0x000000ff);

	ctlr->allocate = ata_promise_mio_allocate;
	ctlr->reset = ata_promise_mio_reset;
	ctlr->dmainit = ata_promise_mio_dmainit;
	ctlr->setmode = ata_promise_mio_setmode;

	return 0;
    }

failnfree:
    if (ctlr->r_res2)
	bus_release_resource(dev, ctlr->r_type2, ctlr->r_rid2, ctlr->r_res2);
    if (ctlr->r_res1)
	bus_release_resource(dev, ctlr->r_type1, ctlr->r_rid1, ctlr->r_res1);
    return ENXIO;
}

static int
ata_promise_allocate(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_pci_allocate(dev))
	return ENXIO;

    ch->hw.status = ata_promise_status;
    return 0;
}

static int
ata_promise_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ATA_INL(ctlr->r_res1, 0x1c) & (ch->unit ? 0x00004000 : 0x00000400)) {
	return ata_pci_status(dev);
    }
    return 0;
}

static int
ata_promise_dmastart(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(GRANDPARENT(dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev  = device_get_softc(dev);

    if (atadev->flags & ATA_D_48BIT_ACTIVE) {
	ATA_OUTB(ctlr->r_res1, 0x11,
		 ATA_INB(ctlr->r_res1, 0x11) | (ch->unit ? 0x08 : 0x02));
	ATA_OUTL(ctlr->r_res1, ch->unit ? 0x24 : 0x20,
		 ((ch->dma->flags & ATA_DMA_READ) ? 0x05000000 : 0x06000000) |
		 (ch->dma->cur_iosize >> 1));
    }
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, (ATA_IDX_INB(ch, ATA_BMSTAT_PORT) |
		 (ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR)));
    ATA_IDX_OUTL(ch, ATA_BMDTP_PORT, ch->dma->sg_bus);
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ((ch->dma->flags & ATA_DMA_READ) ? ATA_BMCMD_WRITE_READ : 0) |
		 ATA_BMCMD_START_STOP);
    ch->flags |= ATA_DMA_ACTIVE;
    return 0;
}

static int
ata_promise_dmastop(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(GRANDPARENT(dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev  = device_get_softc(dev);
    int error;

    if (atadev->flags & ATA_D_48BIT_ACTIVE) {
	ATA_OUTB(ctlr->r_res1, 0x11,
		 ATA_INB(ctlr->r_res1, 0x11) & ~(ch->unit ? 0x08 : 0x02));
	ATA_OUTL(ctlr->r_res1, ch->unit ? 0x24 : 0x20, 0);
    }
    error = ATA_IDX_INB(ch, ATA_BMSTAT_PORT);
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    ch->flags &= ~ATA_DMA_ACTIVE;
    return error;
}

static void
ata_promise_dmareset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    ch->flags &= ~ATA_DMA_ACTIVE;
}

static void
ata_promise_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_dmainit(dev);
    if (ch->dma) {
	ch->dma->start = ata_promise_dmastart;
	ch->dma->stop = ata_promise_dmastop;
	ch->dma->reset = ata_promise_dmareset;
    }
}

static void
ata_promise_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
    int error;
    u_int32_t timings[][2] = {
    /*    PROLD       PRNEW                mode */
	{ 0x004ff329, 0x004fff2f },     /* PIO 0 */
	{ 0x004fec25, 0x004ff82a },     /* PIO 1 */
	{ 0x004fe823, 0x004ff026 },     /* PIO 2 */
	{ 0x004fe622, 0x004fec24 },     /* PIO 3 */
	{ 0x004fe421, 0x004fe822 },     /* PIO 4 */
	{ 0x004567f3, 0x004acef6 },     /* MWDMA 0 */
	{ 0x004467f3, 0x0048cef6 },     /* MWDMA 1 */
	{ 0x004367f3, 0x0046cef6 },     /* MWDMA 2 */
	{ 0x004367f3, 0x0046cef6 },     /* UDMA 0 */
	{ 0x004247f3, 0x00448ef6 },     /* UDMA 1 */
	{ 0x004127f3, 0x00436ef6 },     /* UDMA 2 */
	{ 0,          0x00424ef6 },     /* UDMA 3 */
	{ 0,          0x004127f3 },     /* UDMA 4 */
	{ 0,          0x004127f3 }      /* UDMA 5 */
    };

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    switch (ctlr->chip->cfg1) {
    case PROLD:
    case PRNEW:
	if (mode > ATA_UDMA2 && (pci_read_config(gparent, 0x50, 2) &
				 (ch->unit ? 1 << 11 : 1 << 10))) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	if (ata_atapi(dev) && mode > ATA_PIO_MAX)
	    mode = ata_limit_mode(dev, mode, ATA_PIO_MAX);
	break;

    case PRTX:
	ATA_IDX_OUTB(ch, ATA_BMDEVSPEC_0, 0x0b);
	if (mode > ATA_UDMA2 &&
	    ATA_IDX_INB(ch, ATA_BMDEVSPEC_1) & 0x04) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	break;

    case PRMIO:
	if (mode > ATA_UDMA2 &&
	    (ATA_INL(ctlr->r_res2,
		     (ctlr->chip->cfg2 & PRSX4X ? 0x000c0260 : 0x0260) +
		     (ch->unit << 7)) & 0x01000000)) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	break;
    }

    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);

    if (bootverbose)
	device_printf(dev, "%ssetting %s on %s chip\n",
		     (error) ? "FAILURE " : "",
		     ata_mode2str(mode), ctlr->chip->text);
    if (!error) {
	if (ctlr->chip->cfg1 < PRTX)
	    pci_write_config(gparent, 0x60 + (devno << 2),
			     timings[ata_mode2idx(mode)][ctlr->chip->cfg1], 4);
	atadev->mode = mode;
    }
    return;
}

static int
ata_promise_tx2_allocate(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_pci_allocate(dev))
	return ENXIO;

    ch->hw.status = ata_promise_tx2_status;
    return 0;
}

static int
ata_promise_tx2_status(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ATA_IDX_OUTB(ch, ATA_BMDEVSPEC_0, 0x0b);
    if (ATA_IDX_INB(ch, ATA_BMDEVSPEC_1) & 0x20) {
	return ata_pci_status(dev);
    }
    return 0;
}

static int
ata_promise_mio_allocate(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = (ctlr->chip->cfg2 & PRSX4X) ? 0x000c0000 : 0;
    int i;

    for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
	ch->r_io[i].res = ctlr->r_res2;
	ch->r_io[i].offset = offset + 0x0200 + (i << 2) + (ch->unit << 7);
    }
    ch->r_io[ATA_CONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_CONTROL].offset = offset + 0x0238 + (ch->unit << 7);
    ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
    ata_default_registers(dev);
    if ((ctlr->chip->cfg2 & (PRSATA | PRSATA2)) ||
	((ctlr->chip->cfg2 & (PRCMBO | PRCMBO2)) && ch->unit < 2)) {
	ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
	ch->r_io[ATA_SSTATUS].offset = 0x400 + (ch->unit << 8);
	ch->r_io[ATA_SERROR].res = ctlr->r_res2;
	ch->r_io[ATA_SERROR].offset = 0x404 + (ch->unit << 8);
	ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
	ch->r_io[ATA_SCONTROL].offset = 0x408 + (ch->unit << 8);
	ch->flags |= ATA_NO_SLAVE;
    }
    ch->flags |= ATA_USE_16BIT;

    ata_generic_hw(dev);
    if (ctlr->chip->cfg2 & PRSX4X) {
	ch->hw.command = ata_promise_sx4_command;
    }
    else {
	ch->hw.command = ata_promise_mio_command;
	ch->hw.status = ata_promise_mio_status;
     }
    return 0;
}

static void
ata_promise_mio_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    u_int32_t vector;
    int unit, fake_reg;

    switch (ctlr->chip->cfg2) {
    case PRPATA:
    case PRCMBO:
    case PRSATA:
	fake_reg = 0x60;
	break;
    case PRCMBO2:
    case PRSATA2:
    default:
	fake_reg = 0x54;
	break;
    }

    /*
     * since reading interrupt status register on early "mio" chips
     * clears the status bits we cannot read it for each channel later on
     * in the generic interrupt routine.
     * store the bits in an unused register in the chip so we can read
     * it from there safely to get around this "feature".
     */
    vector = ATA_INL(ctlr->r_res2, 0x040);
    ATA_OUTL(ctlr->r_res2, 0x040, vector);
    ATA_OUTL(ctlr->r_res2, fake_reg, vector);

    for (unit = 0; unit < ctlr->channels; unit++) {
	if ((ch = ctlr->interrupt[unit].argument))
	    ctlr->interrupt[unit].function(ch);
    }

    ATA_OUTL(ctlr->r_res2, fake_reg, 0xffffffff);
}

static int
ata_promise_mio_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_connect_task *tp;
    u_int32_t fake_reg, stat_reg, vector, status;

    switch (ctlr->chip->cfg2) {
    case PRPATA:
    case PRCMBO:
    case PRSATA:
	fake_reg = 0x60;
	stat_reg = 0x6c;
	break;
    case PRCMBO2:
    case PRSATA2:
    default:
	fake_reg = 0x54;
	stat_reg = 0x60;
	break;
    }

    /* read and acknowledge interrupt */
    vector = ATA_INL(ctlr->r_res2, fake_reg);

    /* read and clear interface status */
    status = ATA_INL(ctlr->r_res2, stat_reg);
    ATA_OUTL(ctlr->r_res2, stat_reg, status & (0x00000011 << ch->unit));

    /* check for and handle disconnect events */
    if ((status & (0x00000001 << ch->unit)) &&
	(tp = (struct ata_connect_task *)
	      kmalloc(sizeof(struct ata_connect_task),
		     M_ATA, M_INTWAIT | M_ZERO))) {

	if (bootverbose)
	    device_printf(ch->dev, "DISCONNECT requested\n");
	tp->action = ATA_C_DETACH;
	tp->dev = ch->dev;
	TASK_INIT(&tp->task, 0, ata_sata_phy_event, tp);
	taskqueue_enqueue(taskqueue_thread[mycpuid], &tp->task);
    }

    /* check for and handle connect events */
    if ((status & (0x00000010 << ch->unit)) &&
	(tp = (struct ata_connect_task *)
	      kmalloc(sizeof(struct ata_connect_task),
		     M_ATA, M_INTWAIT | M_ZERO))) {

	if (bootverbose)
	    device_printf(ch->dev, "CONNECT requested\n");
	tp->action = ATA_C_ATTACH;
	tp->dev = ch->dev;
	TASK_INIT(&tp->task, 0, ata_sata_phy_event, tp);
	taskqueue_enqueue(taskqueue_thread[mycpuid], &tp->task);
    }

    /* do we have any device action ? */
    return (vector & (1 << (ch->unit + 1)));
}

static int
ata_promise_mio_command(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(GRANDPARENT(request->dev));
    struct ata_channel *ch = device_get_softc(device_get_parent(request->dev));
    u_int32_t *wordp = (u_int32_t *)ch->dma->work;

    ATA_OUTL(ctlr->r_res2, (ch->unit + 1) << 2, 0x00000001);

    /* XXX SOS add ATAPI commands support later */
    switch (request->u.ata.command) {
    default:
	return ata_generic_command(request);

    case ATA_READ_DMA:
    case ATA_READ_DMA48:
	wordp[0] = htole32(0x04 | ((ch->unit + 1) << 16) | (0x00 << 24));
	break;

    case ATA_WRITE_DMA:
    case ATA_WRITE_DMA48:
	wordp[0] = htole32(0x00 | ((ch->unit + 1) << 16) | (0x00 << 24));
	break;
    }
    wordp[1] = htole32(ch->dma->sg_bus);
    wordp[2] = 0;
    ata_promise_apkt((u_int8_t*)wordp, request);

    ATA_OUTL(ctlr->r_res2, 0x0240 + (ch->unit << 7), ch->dma->work_bus);
    return 0;
}

static void
ata_promise_mio_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_promise_sx4 *hpktp;

    switch (ctlr->chip->cfg2) {
    case PRSX4X:

	/* softreset channel ATA module */
	hpktp = device_get_ivars(ctlr->dev);
	ATA_OUTL(ctlr->r_res2, 0xc0260 + (ch->unit << 7), ch->unit + 1);
	ata_udelay(1000);
	ATA_OUTL(ctlr->r_res2, 0xc0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0xc0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	/* softreset HOST module */ /* XXX SOS what about other outstandings */
	lockmgr(&hpktp->mtx, LK_EXCLUSIVE);
	ATA_OUTL(ctlr->r_res2, 0xc012c,
		 (ATA_INL(ctlr->r_res2, 0xc012c) & ~0x00000f9f) | (1 << 11));
	DELAY(10);
	ATA_OUTL(ctlr->r_res2, 0xc012c,
		 (ATA_INL(ctlr->r_res2, 0xc012c) & ~0x00000f9f));
	hpktp->busy = 0;
	lockmgr(&hpktp->mtx, LK_RELEASE);
	ata_generic_reset(dev);
	break;

    case PRPATA:
    case PRCMBO:
    case PRSATA:
	if ((ctlr->chip->cfg2 == PRSATA) ||
	    ((ctlr->chip->cfg2 == PRCMBO) && (ch->unit < 2))) {

	    /* mask plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x06c, (0x00110000 << ch->unit));
	}

	/* softreset channels ATA module */
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7), (1 << 11));
	ata_udelay(10000);
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0x0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	if ((ctlr->chip->cfg2 == PRSATA) ||
	    ((ctlr->chip->cfg2 == PRCMBO) && (ch->unit < 2))) {

	    if (ata_sata_phy_reset(dev))
		ata_generic_reset(dev);

	    /* reset and enable plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x06c, (0x00000011 << ch->unit));
	}
	else
	    ata_generic_reset(dev);
	break;

    case PRCMBO2:
    case PRSATA2:
	if ((ctlr->chip->cfg2 == PRSATA2) ||
	    ((ctlr->chip->cfg2 == PRCMBO2) && (ch->unit < 2))) {
	    /* set portmultiplier port */
	    ATA_OUTL(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x0f);

	    /* mask plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x060, (0x00110000 << ch->unit));
	}

	/* softreset channels ATA module */
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7), (1 << 11));
	ata_udelay(10000);
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0x0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	if ((ctlr->chip->cfg2 == PRSATA2) ||
	    ((ctlr->chip->cfg2 == PRCMBO2) && (ch->unit < 2))) {

	    /* set PHY mode to "improved" */
	    ATA_OUTL(ctlr->r_res2, 0x414 + (ch->unit << 8),
		     (ATA_INL(ctlr->r_res2, 0x414 + (ch->unit << 8)) &
		     ~0x00000003) | 0x00000001);

	    if (ata_sata_phy_reset(dev))
		ata_generic_reset(dev);

	    /* reset and enable plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x060, (0x00000011 << ch->unit));

	    /* set portmultiplier port */
	    ATA_OUTL(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x00);
	}
	else
	    ata_generic_reset(dev);
	break;

    }
}

static void
ata_promise_mio_dmainit(device_t dev)
{
    /* note start and stop are not used here */
    ata_dmainit(dev);
}

static void
ata_promise_mio_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));

    if ( (ctlr->chip->cfg2 == PRSATA) ||
	((ctlr->chip->cfg2 == PRCMBO) && (ch->unit < 2)) ||
	(ctlr->chip->cfg2 == PRSATA2) ||
	((ctlr->chip->cfg2 == PRCMBO2) && (ch->unit < 2)))
	ata_sata_setmode(dev, mode);
    else
	ata_promise_setmode(dev, mode);
}

static void
ata_promise_sx4_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    u_int32_t vector = ATA_INL(ctlr->r_res2, 0x000c0480);
    int unit;

    for (unit = 0; unit < ctlr->channels; unit++) {
	if (vector & (1 << (unit + 1)))
	    if ((ch = ctlr->interrupt[unit].argument))
		ctlr->interrupt[unit].function(ch);
	if (vector & (1 << (unit + 5)))
	    if ((ch = ctlr->interrupt[unit].argument))
		ata_promise_queue_hpkt(ctlr,
				       htole32((ch->unit * ATA_PDC_CHN_OFFSET) +
					       ATA_PDC_HPKT_OFFSET));
	if (vector & (1 << (unit + 9))) {
	    ata_promise_next_hpkt(ctlr);
	    if ((ch = ctlr->interrupt[unit].argument))
		ctlr->interrupt[unit].function(ch);
	}
	if (vector & (1 << (unit + 13))) {
	    ata_promise_next_hpkt(ctlr);
	    if ((ch = ctlr->interrupt[unit].argument))
		ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
			 htole32((ch->unit * ATA_PDC_CHN_OFFSET) +
			 ATA_PDC_APKT_OFFSET));
	}
    }
}

static int
ata_promise_sx4_command(struct ata_request *request)
{
    device_t gparent = GRANDPARENT(request->dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(request->dev));
    struct ata_dma_prdentry *prd = ch->dma->sg;
    caddr_t window = rman_get_virtual(ctlr->r_res1);
    u_int32_t *wordp;
    int i, idx, length = 0;

    /* XXX SOS add ATAPI commands support later */
    switch (request->u.ata.command) {

    default:
	return -1;

    case ATA_ATA_IDENTIFY:
    case ATA_READ:
    case ATA_READ48:
    case ATA_READ_MUL:
    case ATA_READ_MUL48:
    case ATA_WRITE:
    case ATA_WRITE48:
    case ATA_WRITE_MUL:
    case ATA_WRITE_MUL48:
	ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit + 1) << 2), 0x00000001);
	return ata_generic_command(request);

    case ATA_SETFEATURES:
    case ATA_FLUSHCACHE:
    case ATA_FLUSHCACHE48:
    case ATA_SLEEP:
    case ATA_SET_MULTI:
	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET);
	wordp[0] = htole32(0x08 | ((ch->unit + 1)<<16) | (0x00 << 24));
	wordp[1] = 0;
	wordp[2] = 0;
	ata_promise_apkt((u_int8_t *)wordp, request);
	ATA_OUTL(ctlr->r_res2, 0x000c0484, 0x00000001);
	ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit + 1) << 2), 0x00000001);
	ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
		 htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_APKT_OFFSET));
	return 0;

    case ATA_READ_DMA:
    case ATA_READ_DMA48:
    case ATA_WRITE_DMA:
    case ATA_WRITE_DMA48:
	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HSG_OFFSET);
	i = idx = 0;
	do {
	    wordp[idx++] = prd[i].addr;
	    wordp[idx++] = prd[i].count;
	    length += (prd[i].count & ~ATA_DMA_EOT);
	} while (!(prd[i++].count & ATA_DMA_EOT));

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_LSG_OFFSET);
	wordp[0] = htole32((ch->unit * ATA_PDC_BUF_OFFSET) + ATA_PDC_BUF_BASE);
	wordp[1] = htole32(request->bytecount | ATA_DMA_EOT);

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_ASG_OFFSET);
	wordp[0] = htole32((ch->unit * ATA_PDC_BUF_OFFSET) + ATA_PDC_BUF_BASE);
	wordp[1] = htole32(request->bytecount | ATA_DMA_EOT);

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HPKT_OFFSET);
	if (request->flags & ATA_R_READ)
	    wordp[0] = htole32(0x14 | ((ch->unit+9)<<16) | ((ch->unit+5)<<24));
	if (request->flags & ATA_R_WRITE)
	    wordp[0] = htole32(0x00 | ((ch->unit+13)<<16) | (0x00<<24));
	wordp[1] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_HSG_OFFSET);
	wordp[2] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_LSG_OFFSET);
	wordp[3] = 0;

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET);
	if (request->flags & ATA_R_READ)
	    wordp[0] = htole32(0x04 | ((ch->unit+5)<<16) | (0x00<<24));
	if (request->flags & ATA_R_WRITE)
	    wordp[0] = htole32(0x10 | ((ch->unit+1)<<16) | ((ch->unit+13)<<24));
	wordp[1] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_ASG_OFFSET);
	wordp[2] = 0;
	ata_promise_apkt((u_int8_t *)wordp, request);
	ATA_OUTL(ctlr->r_res2, 0x000c0484, 0x00000001);

	if (request->flags & ATA_R_READ) {
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+5)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+9)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
		htole32((ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET));
	}
	if (request->flags & ATA_R_WRITE) {
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+1)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+13)<<2), 0x00000001);
	    ata_promise_queue_hpkt(ctlr,
		htole32((ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HPKT_OFFSET));
	}
	return 0;
    }
}

static int
ata_promise_apkt(u_int8_t *bytep, struct ata_request *request)
{
    struct ata_device *atadev = device_get_softc(request->dev);
    int i = 12;

    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_PDC_WAIT_NBUSY|ATA_DRIVE;
    bytep[i++] = ATA_D_IBM | ATA_D_LBA | atadev->unit;
    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_CTL;
    bytep[i++] = ATA_A_4BIT;

    if (atadev->flags & ATA_D_48BIT_ACTIVE) {
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_FEATURE;
	bytep[i++] = request->u.ata.feature >> 8;
	bytep[i++] = request->u.ata.feature;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_COUNT;
	bytep[i++] = request->u.ata.count >> 8;
	bytep[i++] = request->u.ata.count;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_SECTOR;
	bytep[i++] = request->u.ata.lba >> 24;
	bytep[i++] = request->u.ata.lba;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_CYL_LSB;
	bytep[i++] = request->u.ata.lba >> 32;
	bytep[i++] = request->u.ata.lba >> 8;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_CYL_MSB;
	bytep[i++] = request->u.ata.lba >> 40;
	bytep[i++] = request->u.ata.lba >> 16;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_DRIVE;
	bytep[i++] = ATA_D_LBA | atadev->unit;
    }
    else {
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_FEATURE;
	bytep[i++] = request->u.ata.feature;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_COUNT;
	bytep[i++] = request->u.ata.count;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_SECTOR;
	bytep[i++] = request->u.ata.lba;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_CYL_LSB;
	bytep[i++] = request->u.ata.lba >> 8;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_CYL_MSB;
	bytep[i++] = request->u.ata.lba >> 16;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_DRIVE;
	bytep[i++] = (atadev->flags & ATA_D_USE_CHS ? 0 : ATA_D_LBA) |
		   ATA_D_IBM | atadev->unit | ((request->u.ata.lba >> 24)&0xf);
    }
    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_END | ATA_COMMAND;
    bytep[i++] = request->u.ata.command;
    return i;
}

static void
ata_promise_queue_hpkt(struct ata_pci_controller *ctlr, u_int32_t hpkt)
{
    struct ata_promise_sx4 *hpktp = device_get_ivars(ctlr->dev);

    lockmgr(&hpktp->mtx, LK_EXCLUSIVE);
    if (hpktp->busy) {
	struct host_packet *hp =
	    kmalloc(sizeof(struct host_packet), M_TEMP, M_INTWAIT | M_ZERO);
	hp->addr = hpkt;
	TAILQ_INSERT_TAIL(&hpktp->queue, hp, chain);
    }
    else {
	hpktp->busy = 1;
	ATA_OUTL(ctlr->r_res2, 0x000c0100, hpkt);
    }
    lockmgr(&hpktp->mtx, LK_RELEASE);
}

static void
ata_promise_next_hpkt(struct ata_pci_controller *ctlr)
{
    struct ata_promise_sx4 *hpktp = device_get_ivars(ctlr->dev);
    struct host_packet *hp;

    lockmgr(&hpktp->mtx, LK_EXCLUSIVE);
    if ((hp = TAILQ_FIRST(&hpktp->queue))) {
	TAILQ_REMOVE(&hpktp->queue, hp, chain);
	ATA_OUTL(ctlr->r_res2, 0x000c0100, hp->addr);
	kfree(hp, M_TEMP);
    }
    else
	hpktp->busy = 0;
    lockmgr(&hpktp->mtx, LK_RELEASE);
}
