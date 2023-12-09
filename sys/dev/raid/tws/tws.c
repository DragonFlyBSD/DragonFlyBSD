/*
 * Copyright (c) 2010, LSI Corp.
 * All rights reserved.
 * Author : Manjunath Ranganathaiah
 * Support: freebsdraid@lsi.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the <ORGANIZATION> nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/tws/tws.c,v 1.3 2007/05/09 04:16:32 mrangana Exp $
 */

#include <dev/raid/tws/tws.h>
#include <dev/raid/tws/tws_services.h>
#include <dev/raid/tws/tws_hdm.h>

#include <bus/cam/cam.h>
#include <bus/cam/cam_ccb.h>
#include <bus/cam/cam_xpt.h>
#include <bus/cam/cam_xpt_periph.h>

static int	tws_msi_enable = 1;

MALLOC_DEFINE(M_TWS, "twsbuf", "buffers used by tws driver");
int tws_queue_depth = TWS_MAX_REQS;

/* externs */
extern int tws_cam_attach(struct tws_softc *sc);
extern void tws_cam_detach(struct tws_softc *sc);
extern int tws_init_ctlr(struct tws_softc *sc);
extern boolean tws_ctlr_ready(struct tws_softc *sc);
extern void tws_turn_off_interrupts(struct tws_softc *sc);
extern void tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req,
                                u_int8_t q_type );
extern struct tws_request *tws_q_remove_request(struct tws_softc *sc,
                                   struct tws_request *req, u_int8_t q_type );
extern struct tws_request *tws_q_remove_head(struct tws_softc *sc,
                                                       u_int8_t q_type );
extern boolean tws_get_response(struct tws_softc *sc, u_int16_t *req_id);
extern boolean tws_ctlr_reset(struct tws_softc *sc);
extern void tws_intr(void *arg);
extern int tws_use_32bit_sgls;


struct tws_request *tws_get_request(struct tws_softc *sc, u_int16_t type);
int tws_init_connect(struct tws_softc *sc, u_int16_t mc);
void tws_send_event(struct tws_softc *sc, u_int8_t event);
uint8_t tws_get_state(struct tws_softc *sc);
void tws_release_request(struct tws_request *req);



/* Function prototypes */
static d_open_t     tws_open;
static d_close_t    tws_close;
static d_read_t     tws_read;
static d_write_t    tws_write;
extern d_ioctl_t    tws_ioctl;

static int tws_init(struct tws_softc *sc);
static void tws_dmamap_cmds_load_cbfn(void *arg, bus_dma_segment_t *segs,
                           int nseg, int error);

static int tws_init_reqs(struct tws_softc *sc, u_int32_t dma_mem_size);
static int tws_init_aen_q(struct tws_softc *sc);
static int tws_init_trace_q(struct tws_softc *sc);
static int tws_setup_irq(struct tws_softc *sc);


/* Character device entry points */

static struct dev_ops tws_ops = {
    { "tws", 0, 0 },
    .d_open =   tws_open,
    .d_close =  tws_close,
    .d_read =   tws_read,
    .d_write =  tws_write,
    .d_ioctl =  tws_ioctl,
};

/*
 * In the cdevsw routines, we find our softc by using the si_drv1 member
 * of struct cdev.  We set this variable to point to our softc in our
 * attach routine when we create the /dev entry.
 */

static int
tws_open(struct dev_open_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, oflags);
    return (0);
}

static int
tws_close(struct dev_close_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, fflag);
    return (0);
}

static int
tws_read(struct dev_read_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, ioflag);
    return (0);
}

static int
tws_write(struct dev_write_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct tws_softc *sc = dev->si_drv1;

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", dev, ioflag);
    return (0);
}

/* PCI Support Functions */

/*
 * Compare the device ID of this device against the IDs that this driver
 * supports.  If there is a match, set the description and return success.
 */
static int
tws_probe(device_t dev)
{
    static u_int8_t first_ctlr = 1;

    if ((pci_get_vendor(dev) == TWS_VENDOR_ID) &&
        (pci_get_device(dev) == TWS_DEVICE_ID)) {
        device_set_desc(dev, "LSI 3ware SAS/SATA Storage Controller");
        if (first_ctlr) {
            kprintf("LSI 3ware device driver for SAS/SATA storage "
                    "controllers, version: %s\n", TWS_DRIVER_VERSION_STRING);
            first_ctlr = 0;
        }

        return(0);
    }
    return (ENXIO);
}

/* Attach function is only called if the probe is successful. */

static int
tws_attach(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);
    u_int32_t cmd, bar;
    int error=0;

    /* no tracing yet */
    /* Look up our softc and initialize its fields. */
    sc->tws_dev = dev;
    sc->device_id = pci_get_device(dev);
    sc->subvendor_id = pci_get_subvendor(dev);
    sc->subdevice_id = pci_get_subdevice(dev);

    /* Intialize mutexes */
    lockinit(&sc->q_lock, "tws_q_lock", 0, LK_CANRECURSE);
    lockinit(&sc->sim_lock, "tws_sim_lock", 0, LK_CANRECURSE);
    lockinit(&sc->gen_lock, "tws_gen_lock", 0, LK_CANRECURSE);
    lockinit(&sc->io_lock, "tws_io_lock", 0, LK_CANRECURSE);

    if ( tws_init_trace_q(sc) == FAILURE )
        kprintf("trace init failure\n");
    /* send init event */
    lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
    tws_send_event(sc, TWS_INIT_START);
    lockmgr(&sc->gen_lock, LK_RELEASE);


#if _BYTE_ORDER == _BIG_ENDIAN
    TWS_TRACE(sc, "BIG endian", 0, 0);
#endif
    SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
		      SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
                      OID_AUTO, "driver_version", CTLFLAG_RD,
                      TWS_DRIVER_VERSION_STRING, 0, "TWS driver version");

    cmd = pci_read_config(dev, PCIR_COMMAND, 2);
    if ( (cmd & PCIM_CMD_PORTEN) == 0) {
        tws_log(sc, PCI_COMMAND_READ);
        goto attach_fail_1;
    }
    /* Force the busmaster enable bit on. */
    cmd |= PCIM_CMD_BUSMASTEREN;
    pci_write_config(dev, PCIR_COMMAND, cmd, 2);

    bar = pci_read_config(dev, TWS_PCI_BAR0, 4);
    TWS_TRACE_DEBUG(sc, "bar0 ", bar, 0);
    bar = pci_read_config(dev, TWS_PCI_BAR1, 4);
    bar = bar & ~TWS_BIT2;
    TWS_TRACE_DEBUG(sc, "bar1 ", bar, 0);

    /* MFA base address is BAR2 register used for
     * push mode. Firmware will evatualy move to
     * pull mode during witch this needs to change
     */
#ifndef TWS_PULL_MODE_ENABLE
    sc->mfa_base = (u_int64_t)pci_read_config(dev, TWS_PCI_BAR2, 4);
    sc->mfa_base = sc->mfa_base & ~TWS_BIT2;
    TWS_TRACE_DEBUG(sc, "bar2 ", sc->mfa_base, 0);
#endif

    /* allocate MMIO register space */
    sc->reg_res_id = TWS_PCI_BAR1; /* BAR1 offset */
    if ((sc->reg_res = bus_alloc_resource(dev, SYS_RES_MEMORY,
                                &(sc->reg_res_id), 0, ~0, 1, RF_ACTIVE))
                                == NULL) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_1;
    }
    sc->bus_tag = rman_get_bustag(sc->reg_res);
    sc->bus_handle = rman_get_bushandle(sc->reg_res);

#ifndef TWS_PULL_MODE_ENABLE
    /* Allocate bus space for inbound mfa */
    sc->mfa_res_id = TWS_PCI_BAR2; /* BAR2 offset */
    if ((sc->mfa_res = bus_alloc_resource(dev, SYS_RES_MEMORY,
                          &(sc->mfa_res_id), 0, ~0, 0x100000, RF_ACTIVE))
                                == NULL) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_2;
    }
    sc->bus_mfa_tag = rman_get_bustag(sc->mfa_res);
    sc->bus_mfa_handle = rman_get_bushandle(sc->mfa_res);
#endif

    /* Allocate and register our interrupt. */
    if ( tws_setup_irq(sc) == FAILURE ) {
        tws_log(sc, ALLOC_MEMORY_RES);
        goto attach_fail_3;
    }

    /* Init callouts. */
    callout_init(&sc->print_stats_handle);
    callout_init(&sc->reset_cb_handle);
    callout_init(&sc->reinit_handle);

    /*
     * Create a /dev entry for this device.  The kernel will assign us
     * a major number automatically.  We use the unit number of this
     * device as the minor number and name the character device
     * "tws<unit>".
     */
    sc->tws_cdev = make_dev(&tws_ops, device_get_unit(dev),
        UID_ROOT, GID_OPERATOR, S_IRUSR | S_IWUSR, "tws%u",
        device_get_unit(dev));
    sc->tws_cdev->si_drv1 = sc;

    if ( tws_init(sc) == FAILURE ) {
        tws_log(sc, TWS_INIT_FAILURE);
        goto attach_fail_4;
    }
    if ( tws_init_ctlr(sc) == FAILURE ) {
        tws_log(sc, TWS_CTLR_INIT_FAILURE);
        goto attach_fail_4;
    }
    if ((error = tws_cam_attach(sc))) {
        tws_log(sc, TWS_CAM_ATTACH);
        goto attach_fail_4;
    }
    /* send init complete event */
    lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
    tws_send_event(sc, TWS_INIT_COMPLETE);
    lockmgr(&sc->gen_lock, LK_RELEASE);

    TWS_TRACE_DEBUG(sc, "attached successfully", 0, sc->device_id);
    return(0);

attach_fail_4:
    if (sc->intr_handle) {
        if ((error = bus_teardown_intr(sc->tws_dev,
                     sc->irq_res, sc->intr_handle)))
            TWS_TRACE(sc, "bus teardown intr", 0, error);
    }
    destroy_dev(sc->tws_cdev);
    dev_ops_remove_minor(&tws_ops, device_get_unit(sc->tws_dev));
attach_fail_3:
    if (sc->irq_res) {
        if (bus_release_resource(sc->tws_dev,
            SYS_RES_IRQ, sc->irq_res_id, sc->irq_res))
            TWS_TRACE(sc, "bus irq res", 0, 0);
    }
#ifndef TWS_PULL_MODE_ENABLE
attach_fail_2:
#endif
    if ( sc->mfa_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->mfa_res_id, sc->mfa_res))
            TWS_TRACE(sc, "bus release ", 0, sc->mfa_res_id);
    }
    if ( sc->reg_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->reg_res_id, sc->reg_res))
            TWS_TRACE(sc, "bus release2 ", 0, sc->reg_res_id);
    }
attach_fail_1:
    lockuninit(&sc->q_lock);
    lockuninit(&sc->sim_lock);
    lockuninit(&sc->gen_lock);
    lockuninit(&sc->io_lock);
    return (ENXIO);
}

/* Detach device. */

static int
tws_detach(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);
    int error;
    u_int32_t reg;

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
    tws_send_event(sc, TWS_UNINIT_START);
    lockmgr(&sc->gen_lock, LK_RELEASE);

    /* needs to disable interrupt before detaching from cam */
    tws_turn_off_interrupts(sc);
    /* clear door bell */
    tws_write_reg(sc, TWS_I2O0_HOBDBC, ~0, 4);
    reg = tws_read_reg(sc, TWS_I2O0_HIMASK, 4);
    TWS_TRACE_DEBUG(sc, "turn-off-intr", reg, 0);
    sc->obfl_q_overrun = false;
    tws_init_connect(sc, 1);

    /* Teardown the state in our softc created in our attach routine. */
    /* Disconnect the interrupt handler. */
    if (sc->intr_handle) {
        if ((error = bus_teardown_intr(sc->tws_dev,
                     sc->irq_res, sc->intr_handle)))
            TWS_TRACE(sc, "bus teardown intr", 0, error);
    }
    /* Release irq resource */
    if (sc->irq_res) {
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_IRQ, sc->irq_res_id, sc->irq_res))
            TWS_TRACE(sc, "bus release irq resource", 0, sc->irq_res_id);
    }
    if (sc->intr_type == PCI_INTR_TYPE_MSI)
        pci_release_msi(sc->tws_dev);

    tws_cam_detach(sc);

    /* Release memory resource */
    if ( sc->mfa_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->mfa_res_id, sc->mfa_res))
            TWS_TRACE(sc, "bus release mem resource", 0, sc->mfa_res_id);
    }
    if ( sc->reg_res ){
        if (bus_release_resource(sc->tws_dev,
                 SYS_RES_MEMORY, sc->reg_res_id, sc->reg_res))
            TWS_TRACE(sc, "bus release mem resource", 0, sc->reg_res_id);
    }

    kfree(sc->reqs, M_TWS);
    kfree(sc->sense_bufs, M_TWS);
    xpt_free_ccb(&sc->scan_ccb->ccb_h);
    kfree(sc->aen_q.q, M_TWS);
    kfree(sc->trace_q.q, M_TWS);
    lockuninit(&sc->q_lock);
    lockuninit(&sc->sim_lock);
    lockuninit(&sc->gen_lock);
    lockuninit(&sc->io_lock);
    destroy_dev(sc->tws_cdev);
    dev_ops_remove_minor(&tws_ops, device_get_unit(sc->tws_dev));
    return (0);
}

static int
tws_setup_irq(struct tws_softc *sc)
{
    u_int16_t cmd;
    u_int irq_flags;

    cmd = pci_read_config(sc->tws_dev, PCIR_COMMAND, 2);

    if (tws_msi_enable)
        cmd |= 0x0400;
    else
        cmd &= ~0x0400;
    pci_write_config(sc->tws_dev, PCIR_COMMAND, cmd, 2);
    sc->irq_res = 0;
    sc->intr_type = pci_alloc_1intr(sc->tws_dev, tws_msi_enable,
        &sc->irq_res_id, &irq_flags);
    sc->irq_res = bus_alloc_resource_any(sc->tws_dev, SYS_RES_IRQ,
        &sc->irq_res_id, irq_flags);
    if (!sc->irq_res)
        return(FAILURE);
    if (bus_setup_intr(sc->tws_dev, sc->irq_res, INTR_MPSAFE, tws_intr, sc,
        &sc->intr_handle, NULL)) {
            tws_log(sc, SETUP_INTR_RES);
            return(FAILURE);
    }
    if (sc->intr_type == PCI_INTR_TYPE_MSI)
            device_printf(sc->tws_dev, "Using MSI\n");
    else
            device_printf(sc->tws_dev, "Using legacy INTx\n");

    return(SUCCESS);
}

static int
tws_init(struct tws_softc *sc)
{

    u_int32_t max_sg_elements;
    u_int32_t dma_mem_size;
    int error;
    u_int32_t reg;

    sc->seq_id = 0;
    if ( tws_queue_depth > TWS_MAX_REQS )
        tws_queue_depth = TWS_MAX_REQS;
    if (tws_queue_depth < TWS_RESERVED_REQS+1)
        tws_queue_depth = TWS_RESERVED_REQS+1;
    sc->is64bit = (sizeof(bus_addr_t) == 8) ? true : false;
    max_sg_elements = (sc->is64bit && !tws_use_32bit_sgls) ?
                                 TWS_MAX_64BIT_SG_ELEMENTS :
                                 TWS_MAX_32BIT_SG_ELEMENTS;
    dma_mem_size = (sizeof(struct tws_command_packet) * tws_queue_depth) +
                             (TWS_SECTOR_SIZE) ;
    if ( bus_dma_tag_create(NULL,                    /* parent */
                            TWS_ALIGNMENT,           /* alignment */
                            0,                       /* boundary */
                            BUS_SPACE_MAXADDR_32BIT, /* lowaddr */
                            BUS_SPACE_MAXADDR,       /* highaddr */
                            BUS_SPACE_MAXSIZE,       /* maxsize */
                            max_sg_elements,         /* numsegs */
                            BUS_SPACE_MAXSIZE,       /* maxsegsize */
                            0,                       /* flags */
                            &sc->parent_tag          /* tag */
                           )) {
        TWS_TRACE_DEBUG(sc, "DMA parent tag Create fail", max_sg_elements,
                                                    sc->is64bit);
        return(ENOMEM);
    }
    /* In bound message frame requires 16byte alignment.
     * Outbound MF's can live with 4byte alignment - for now just
     * use 16 for both.
     */
    if ( bus_dma_tag_create(sc->parent_tag,       /* parent */
                            TWS_IN_MF_ALIGNMENT,  /* alignment */
                            0,                    /* boundary */
                            BUS_SPACE_MAXADDR_32BIT, /* lowaddr */
                            BUS_SPACE_MAXADDR,    /* highaddr */
                            dma_mem_size,         /* maxsize */
                            1,                    /* numsegs */
                            BUS_SPACE_MAXSIZE,    /* maxsegsize */
                            0,                    /* flags */
                            &sc->cmd_tag          /* tag */
                           )) {
        TWS_TRACE_DEBUG(sc, "DMA cmd tag Create fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    if (bus_dmamem_alloc(sc->cmd_tag, &sc->dma_mem,
                    BUS_DMA_NOWAIT, &sc->cmd_map)) {
        TWS_TRACE_DEBUG(sc, "DMA mem alloc fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    /* if bus_dmamem_alloc succeeds then bus_dmamap_load will succeed */
    sc->dma_mem_phys=0;
    error = bus_dmamap_load(sc->cmd_tag, sc->cmd_map, sc->dma_mem,
                    dma_mem_size, tws_dmamap_cmds_load_cbfn,
                    &sc->dma_mem_phys, 0);

    if ( error == EINPROGRESS )
        TWS_TRACE_DEBUG(sc, "req queued", max_sg_elements, sc->is64bit);

   /*
    * Create a dma tag for data buffers; size will be the maximum
    * possible I/O size (128kB).
    */
    if (bus_dma_tag_create(sc->parent_tag,         /* parent */
                           TWS_ALIGNMENT,          /* alignment */
                           0,                      /* boundary */
                           BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
                           BUS_SPACE_MAXADDR,      /* highaddr */
                           TWS_MAX_IO_SIZE,        /* maxsize */
                           max_sg_elements,        /* nsegments */
                           TWS_MAX_IO_SIZE,        /* maxsegsize */
			   BUS_DMA_ALLOCALL |      /* flags */
			   BUS_DMA_ALLOCNOW |
                           BUS_DMA_PRIVBZONE |
			   BUS_DMA_PROTECTED,
                           &sc->data_tag           /* tag */)) {
        TWS_TRACE_DEBUG(sc, "DMA cmd tag Create fail", max_sg_elements, sc->is64bit);
        return(ENOMEM);
    }

    sc->reqs = kmalloc(sizeof(struct tws_request) * tws_queue_depth, M_TWS,
                      M_WAITOK | M_ZERO);
    sc->sense_bufs = kmalloc(sizeof(struct tws_sense) * tws_queue_depth, M_TWS,
                      M_WAITOK | M_ZERO);
    sc->scan_ccb = xpt_alloc_ccb();

    if ( !tws_ctlr_ready(sc) )
        if( !tws_ctlr_reset(sc) )
            return(FAILURE);

    bzero(&sc->stats, sizeof(struct tws_stats));
    tws_init_qs(sc);
    tws_turn_off_interrupts(sc);

    /*
     * enable pull mode by setting bit1 .
     * setting bit0 to 1 will enable interrupt coalesing
     * will revisit.
     */

#ifdef TWS_PULL_MODE_ENABLE

    reg = tws_read_reg(sc, TWS_I2O0_CTL, 4);
    TWS_TRACE_DEBUG(sc, "i20 ctl", reg, TWS_I2O0_CTL);
    tws_write_reg(sc, TWS_I2O0_CTL, reg | TWS_BIT1, 4);

#endif

    TWS_TRACE_DEBUG(sc, "dma_mem_phys", sc->dma_mem_phys, TWS_I2O0_CTL);
    if ( tws_init_reqs(sc, dma_mem_size) == FAILURE )
        return(FAILURE);
    if ( tws_init_aen_q(sc) == FAILURE )
        return(FAILURE);

    return(SUCCESS);

}

static int
tws_init_aen_q(struct tws_softc *sc)
{
    sc->aen_q.head=0;
    sc->aen_q.tail=0;
    sc->aen_q.depth=256;
    sc->aen_q.overflow=0;
    sc->aen_q.q = kmalloc(sizeof(struct tws_event_packet)*sc->aen_q.depth,
                              M_TWS, M_WAITOK | M_ZERO);
    return(SUCCESS);
}

static int
tws_init_trace_q(struct tws_softc *sc)
{
    sc->trace_q.head=0;
    sc->trace_q.tail=0;
    sc->trace_q.depth=256;
    sc->trace_q.overflow=0;
    sc->trace_q.q = kmalloc(sizeof(struct tws_trace_rec)*sc->trace_q.depth,
                              M_TWS, M_WAITOK | M_ZERO);
    return(SUCCESS);
}

static int
tws_init_reqs(struct tws_softc *sc, u_int32_t dma_mem_size)
{

    struct tws_command_packet *cmd_buf;
    cmd_buf = (struct tws_command_packet *)sc->dma_mem;
    int i;

    bzero(cmd_buf, dma_mem_size);
    TWS_TRACE_DEBUG(sc, "phy cmd", sc->dma_mem_phys, 0);
    lockmgr(&sc->q_lock, LK_EXCLUSIVE);
    for ( i=0; i< tws_queue_depth; i++)
    {
        if (bus_dmamap_create(sc->data_tag, 0, &sc->reqs[i].dma_map)) {
            /* log a ENOMEM failure msg here */
            lockmgr(&sc->q_lock, LK_RELEASE);
            return(FAILURE);
        }
        sc->reqs[i].cmd_pkt =  &cmd_buf[i];

        sc->sense_bufs[i].hdr = &cmd_buf[i].hdr ;
        sc->sense_bufs[i].hdr_pkt_phy = sc->dma_mem_phys +
                              (i * sizeof(struct tws_command_packet));
        sc->sense_bufs[i].posted = false;

        sc->reqs[i].cmd_pkt_phy = sc->dma_mem_phys +
                              sizeof(struct tws_command_header) +
                              (i * sizeof(struct tws_command_packet));
        sc->reqs[i].request_id = i;
        sc->reqs[i].sc = sc;

        sc->reqs[i].cmd_pkt->hdr.header_desc.size_header = 128;

        sc->reqs[i].state = TWS_REQ_STATE_FREE;
        if ( i >= TWS_RESERVED_REQS )
            tws_q_insert_tail(sc, &sc->reqs[i], TWS_FREE_Q);
    }
    lockmgr(&sc->q_lock, LK_RELEASE);
    return(SUCCESS);
}

static void
tws_dmamap_cmds_load_cbfn(void *arg, bus_dma_segment_t *segs,
                           int nseg, int error)
{

    /* kprintf("command load done \n"); */

    *((bus_addr_t *)arg) = segs[0].ds_addr;
}

void
tws_send_event(struct tws_softc *sc, u_int8_t event)
{
    KKASSERT(lockstatus(&sc->gen_lock, curthread) != 0);
    TWS_TRACE_DEBUG(sc, "received event ", 0, event);
    switch (event) {

        case TWS_INIT_START:
            sc->tws_state = TWS_INIT;
            break;

        case TWS_INIT_COMPLETE:
            KASSERT(sc->tws_state == TWS_INIT , ("invalid state transition"));
            sc->tws_state = TWS_ONLINE;
            break;

        case TWS_RESET_START:
            /* multiple reset ? */
            KASSERT(sc->tws_state != TWS_RESET, ("invalid state transition"));

            /* we can transition to reset state from any state */
            sc->tws_prev_state = sc->tws_state;
            sc->tws_state = TWS_RESET;
            break;

        case TWS_RESET_COMPLETE:
            KASSERT(sc->tws_state == TWS_RESET, ("invalid state transition"));
            sc->tws_state = sc->tws_prev_state;
            break;

        case TWS_SCAN_FAILURE:
            KASSERT(sc->tws_state == TWS_ONLINE , ("invalid state transition"));
            sc->tws_state = TWS_OFFLINE;
            break;

        case TWS_UNINIT_START:
            KASSERT(sc->tws_state == TWS_ONLINE || sc->tws_state == TWS_OFFLINE,
                           ("invalid state transition"));
            sc->tws_state = TWS_UNINIT;
            break;
    }

}

uint8_t
tws_get_state(struct tws_softc *sc)
{

    return((u_int8_t)sc->tws_state);

}

/* Called during system shutdown after sync. */

static int
tws_shutdown(device_t dev)
{

    struct tws_softc *sc = device_get_softc(dev);

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    tws_turn_off_interrupts(sc);
    tws_init_connect(sc, 1);

    return (0);
}

/*
 * Device suspend routine.
 */
static int
tws_suspend(device_t dev)
{
    struct tws_softc *sc = device_get_softc(dev);

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    return (0);
}

/*
 * Device resume routine.
 */
static int
tws_resume(device_t dev)
{

    struct tws_softc *sc = device_get_softc(dev);

    if ( sc )
        TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    return (0);
}


struct tws_request *
tws_get_request(struct tws_softc *sc, u_int16_t type)
{
    struct tws_request *r = NULL;

    switch ( type ) {
        case TWS_INTERNAL_CMD_REQ :
            lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
            r = &sc->reqs[0];
            if ( r->state != TWS_REQ_STATE_FREE ) {
                r = NULL;
            } else {
                r->state = TWS_REQ_STATE_BUSY;
            }
            lockmgr(&sc->gen_lock, LK_RELEASE);
            break;
        case TWS_AEN_FETCH_REQ :
            lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
            r = &sc->reqs[1];
            if ( r->state != TWS_REQ_STATE_FREE ) {
                r = NULL;
            } else {
                r->state = TWS_REQ_STATE_BUSY;
            }
            lockmgr(&sc->gen_lock, LK_RELEASE);
            break;
        case TWS_PASSTHRU_REQ :
            lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
            r = &sc->reqs[2];
            if ( r->state != TWS_REQ_STATE_FREE ) {
                r = NULL;
            } else {
                r->state = TWS_REQ_STATE_BUSY;
            }
            lockmgr(&sc->gen_lock, LK_RELEASE);
            break;
        case TWS_GETSET_PARAM_REQ :
            lockmgr(&sc->gen_lock, LK_EXCLUSIVE);
            r = &sc->reqs[3];
            if ( r->state != TWS_REQ_STATE_FREE ) {
                r = NULL;
            } else {
                r->state = TWS_REQ_STATE_BUSY;
            }
            lockmgr(&sc->gen_lock, LK_RELEASE);
            break;
        case TWS_SCSI_IO_REQ :
            lockmgr(&sc->q_lock, LK_EXCLUSIVE);
            r = tws_q_remove_head(sc, TWS_FREE_Q);
            if ( r )
                r->state = TWS_REQ_STATE_TRAN;
            lockmgr(&sc->q_lock, LK_RELEASE);
            break;
        default :
            TWS_TRACE_DEBUG(sc, "Unknown req type", 0, type);
            r = NULL;

    }

    if ( r ) {
        bzero(&r->cmd_pkt->cmd, sizeof(struct tws_command_apache));
	callout_init(&r->thandle);
        r->data = NULL;
        r->length = 0;
        r->type = type;
        r->flags = TWS_DIR_UNKNOWN;
        r->error_code = TWS_REQ_ERR_INVALID;
        r->ccb_ptr = NULL;
        r->cb = NULL;
        r->next = r->prev = NULL;
    }
    return(r);
}

void
tws_release_request(struct tws_request *req)
{

    struct tws_softc *sc = req->sc;

    TWS_TRACE_DEBUG(sc, "entry", sc, 0);
    lockmgr(&sc->q_lock, LK_EXCLUSIVE);
    tws_q_insert_tail(sc, req, TWS_FREE_Q);
    lockmgr(&sc->q_lock, LK_RELEASE);
}

static device_method_t tws_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,     tws_probe),
    DEVMETHOD(device_attach,    tws_attach),
    DEVMETHOD(device_detach,    tws_detach),
    DEVMETHOD(device_shutdown,  tws_shutdown),
    DEVMETHOD(device_suspend,   tws_suspend),
    DEVMETHOD(device_resume,    tws_resume),

    DEVMETHOD(bus_print_child,      bus_generic_print_child),
    DEVMETHOD(bus_driver_added,     bus_generic_driver_added),
    DEVMETHOD_END
};

static driver_t tws_driver = {
        "tws",
        tws_methods,
        sizeof(struct tws_softc)
};


static devclass_t tws_devclass;

/* DEFINE_CLASS_0(tws, tws_driver, tws_methods, sizeof(struct tws_softc)); */
DRIVER_MODULE(tws, pci, tws_driver, tws_devclass, NULL, NULL);
MODULE_DEPEND(tws, cam, 1, 1, 1);
MODULE_DEPEND(tws, pci, 1, 1, 1);

TUNABLE_INT("hw.tws.queue_depth", &tws_queue_depth);
TUNABLE_INT("hw.tws.msi.enable", &tws_msi_enable);
