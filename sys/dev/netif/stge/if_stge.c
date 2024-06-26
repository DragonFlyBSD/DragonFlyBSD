/*	$NetBSD: if_stge.c,v 1.32 2005/12/11 12:22:49 christos Exp $	*/
/*	$FreeBSD: src/sys/dev/stge/if_stge.c,v 1.2 2006/08/12 01:21:36 yongari Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for the Sundance Tech. TC9021 10/100/1000
 * Ethernet controller.
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "if_stgereg.h"
#include "if_stgevar.h"

#define	STGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Devices supported by this driver.
 */
static struct stge_product {
	uint16_t	stge_vendorid;
	uint16_t	stge_deviceid;
	const char	*stge_name;
} stge_products[] = {
	{ VENDOR_SUNDANCETI,	DEVICEID_SUNDANCETI_ST1023,
	  "Sundance ST-1023 Gigabit Ethernet" },

	{ VENDOR_SUNDANCETI,	DEVICEID_SUNDANCETI_ST2021,
	  "Sundance ST-2021 Gigabit Ethernet" },

	{ VENDOR_TAMARACK,	DEVICEID_TAMARACK_TC9021,
	  "Tamarack TC9021 Gigabit Ethernet" },

	{ VENDOR_TAMARACK,	DEVICEID_TAMARACK_TC9021_ALT,
	  "Tamarack TC9021 Gigabit Ethernet" },

	/*
	 * The Sundance sample boards use the Sundance vendor ID,
	 * but the Tamarack product ID.
	 */
	{ VENDOR_SUNDANCETI,	DEVICEID_TAMARACK_TC9021,
	  "Sundance TC9021 Gigabit Ethernet" },

	{ VENDOR_SUNDANCETI,	DEVICEID_TAMARACK_TC9021_ALT,
	  "Sundance TC9021 Gigabit Ethernet" },

	{ VENDOR_DLINK,		DEVICEID_DLINK_DL2000,
	  "D-Link DL-2000 Gigabit Ethernet" },

	{ VENDOR_ANTARES,	DEVICEID_ANTARES_TC9021,
	  "Antares Gigabit Ethernet" },

	{ 0, 0, NULL }
};

static int	stge_probe(device_t);
static int	stge_attach(device_t);
static int	stge_detach(device_t);
static void	stge_shutdown(device_t);
static int	stge_suspend(device_t);
static int	stge_resume(device_t);

static int	stge_encap(struct stge_softc *, struct mbuf **);
static void	stge_start(struct ifnet *, struct ifaltq_subque *);
static void	stge_watchdog(struct ifnet *);
static int	stge_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	stge_init(void *);
static void	stge_vlan_setup(struct stge_softc *);
static void	stge_stop(struct stge_softc *);
static void	stge_start_tx(struct stge_softc *);
static void	stge_start_rx(struct stge_softc *);
static void	stge_stop_tx(struct stge_softc *);
static void	stge_stop_rx(struct stge_softc *);

static void	stge_reset(struct stge_softc *, uint32_t);
static int	stge_eeprom_wait(struct stge_softc *);
static void	stge_read_eeprom(struct stge_softc *, int, uint16_t *);
static void	stge_tick(void *);
static void	stge_stats_update(struct stge_softc *);
static void	stge_set_filter(struct stge_softc *);
static void	stge_set_multi(struct stge_softc *);

static void	stge_link(struct stge_softc *);
static void	stge_intr(void *);
static __inline int stge_tx_error(struct stge_softc *);
static void	stge_txeof(struct stge_softc *);
static void	stge_rxeof(struct stge_softc *, int);
static __inline void stge_discard_rxbuf(struct stge_softc *, int);
static int	stge_newbuf(struct stge_softc *, int, int);
#ifndef __x86_64__
static __inline struct mbuf *stge_fixup_rx(struct stge_softc *, struct mbuf *);
#endif

static void	stge_mii_sync(struct stge_softc *);
static void	stge_mii_send(struct stge_softc *, uint32_t, int);
static int	stge_mii_readreg(struct stge_softc *, struct stge_mii_frame *);
static int	stge_mii_writereg(struct stge_softc *, struct stge_mii_frame *);
static int	stge_miibus_readreg(device_t, int, int);
static int	stge_miibus_writereg(device_t, int, int, int);
static void	stge_miibus_statchg(device_t);
static int	stge_mediachange(struct ifnet *);
static void	stge_mediastatus(struct ifnet *, struct ifmediareq *);

static int	stge_dma_alloc(struct stge_softc *);
static void	stge_dma_free(struct stge_softc *);
static void	stge_dma_wait(struct stge_softc *);
static void	stge_init_tx_ring(struct stge_softc *);
static int	stge_init_rx_ring(struct stge_softc *);
#ifdef IFPOLL_ENABLE
static void	stge_npoll(struct ifnet *, struct ifpoll_info *);
static void	stge_npoll_compat(struct ifnet *, void *, int);
#endif

static int	sysctl_hw_stge_rxint_nframe(SYSCTL_HANDLER_ARGS);
static int	sysctl_hw_stge_rxint_dmawait(SYSCTL_HANDLER_ARGS);

static device_method_t stge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		stge_probe),
	DEVMETHOD(device_attach,	stge_attach),
	DEVMETHOD(device_detach,	stge_detach),
	DEVMETHOD(device_shutdown,	stge_shutdown),
	DEVMETHOD(device_suspend,	stge_suspend),
	DEVMETHOD(device_resume,	stge_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	stge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	stge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	stge_miibus_statchg),

	DEVMETHOD_END

};

static driver_t stge_driver = {
	"stge",
	stge_methods,
	sizeof(struct stge_softc)
};

static devclass_t stge_devclass;

DECLARE_DUMMY_MODULE(if_stge);
MODULE_DEPEND(if_stge, miibus, 1, 1, 1);
DRIVER_MODULE(if_stge, pci, stge_driver, stge_devclass, NULL, NULL);
DRIVER_MODULE(miibus, stge, miibus_driver, miibus_devclass, NULL, NULL);

#define	MII_SET(x)	\
	CSR_WRITE_1(sc, STGE_PhyCtrl, CSR_READ_1(sc, STGE_PhyCtrl) | (x))
#define	MII_CLR(x)	\
	CSR_WRITE_1(sc, STGE_PhyCtrl, CSR_READ_1(sc, STGE_PhyCtrl) & ~(x))

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
stge_mii_sync(struct stge_softc	*sc)
{
	int i;

	MII_SET(PC_MgmtDir | PC_MgmtData);

	for (i = 0; i < 32; i++) {
		MII_SET(PC_MgmtClk);
		DELAY(1);
		MII_CLR(PC_MgmtClk);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
stge_mii_send(struct stge_softc *sc, uint32_t bits, int cnt)
{
	int i;

	MII_CLR(PC_MgmtClk);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i)
			MII_SET(PC_MgmtData);
                else
			MII_CLR(PC_MgmtData);
		DELAY(1);
		MII_CLR(PC_MgmtClk);
		DELAY(1);
		MII_SET(PC_MgmtClk);
	}
}

/*
 * Read an PHY register through the MII.
 */
static int
stge_mii_readreg(struct stge_softc *sc, struct stge_mii_frame *frame)
{
	int i, ack;

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = STGE_MII_STARTDELIM;
	frame->mii_opcode = STGE_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;

	CSR_WRITE_1(sc, STGE_PhyCtrl, 0 | sc->sc_PhyCtrl);
	/*
 	 * Turn on data xmit.
	 */
	MII_SET(PC_MgmtDir);

	stge_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	stge_mii_send(sc, frame->mii_stdelim, 2);
	stge_mii_send(sc, frame->mii_opcode, 2);
	stge_mii_send(sc, frame->mii_phyaddr, 5);
	stge_mii_send(sc, frame->mii_regaddr, 5);

	/* Turn off xmit. */
	MII_CLR(PC_MgmtDir);

	/* Idle bit */
	MII_CLR((PC_MgmtClk | PC_MgmtData));
	DELAY(1);
	MII_SET(PC_MgmtClk);
	DELAY(1);

	/* Check for ack */
	MII_CLR(PC_MgmtClk);
	DELAY(1);
	ack = CSR_READ_1(sc, STGE_PhyCtrl) & PC_MgmtData;
	MII_SET(PC_MgmtClk);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(PC_MgmtClk);
			DELAY(1);
			MII_SET(PC_MgmtClk);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(PC_MgmtClk);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_1(sc, STGE_PhyCtrl) & PC_MgmtData)
				frame->mii_data |= i;
			DELAY(1);
		}
		MII_SET(PC_MgmtClk);
		DELAY(1);
	}

fail:
	MII_CLR(PC_MgmtClk);
	DELAY(1);
	MII_SET(PC_MgmtClk);
	DELAY(1);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
static int
stge_mii_writereg(struct stge_softc *sc, struct stge_mii_frame *frame)
{

	/*
	 * Set up frame for TX.
	 */
	frame->mii_stdelim = STGE_MII_STARTDELIM;
	frame->mii_opcode = STGE_MII_WRITEOP;
	frame->mii_turnaround = STGE_MII_TURNAROUND;

	/*
 	 * Turn on data output.
	 */
	MII_SET(PC_MgmtDir);

	stge_mii_sync(sc);

	stge_mii_send(sc, frame->mii_stdelim, 2);
	stge_mii_send(sc, frame->mii_opcode, 2);
	stge_mii_send(sc, frame->mii_phyaddr, 5);
	stge_mii_send(sc, frame->mii_regaddr, 5);
	stge_mii_send(sc, frame->mii_turnaround, 2);
	stge_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(PC_MgmtClk);
	DELAY(1);
	MII_CLR(PC_MgmtClk);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(PC_MgmtDir);

	return(0);
}

/*
 * sc_miibus_readreg:	[mii interface function]
 *
 *	Read a PHY register on the MII of the TC9021.
 */
static int
stge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct stge_softc *sc;
	struct stge_mii_frame frame;
	int error;

	sc = device_get_softc(dev);

	if (reg == STGE_PhyCtrl) {
		/* XXX allow ip1000phy read STGE_PhyCtrl register. */
		error = CSR_READ_1(sc, STGE_PhyCtrl);
		return (error);
	}
	bzero(&frame, sizeof(frame));
	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;

	error = stge_mii_readreg(sc, &frame);

	if (error != 0) {
		/* Don't show errors for PHY probe request */
		if (reg != 1)
			device_printf(sc->sc_dev, "phy read fail\n");
		return (0);
	}
	return (frame.mii_data);
}

/*
 * stge_miibus_writereg:	[mii interface function]
 *
 *	Write a PHY register on the MII of the TC9021.
 */
static int
stge_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct stge_softc *sc;
	struct stge_mii_frame frame;
	int error;

	sc = device_get_softc(dev);

	bzero(&frame, sizeof(frame));
	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = val;

	error = stge_mii_writereg(sc, &frame);

	if (error != 0)
		device_printf(sc->sc_dev, "phy write fail\n");
	return (0);
}

/*
 * stge_miibus_statchg:	[mii interface function]
 *
 *	Callback from MII layer when media changes.
 */
static void
stge_miibus_statchg(device_t dev)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->sc_miibus);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)
		return;

	sc->sc_MACCtrl = 0;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_FDX) != 0)
		sc->sc_MACCtrl |= MC_DuplexSelect;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_FLAG0) != 0)
		sc->sc_MACCtrl |= MC_RxFlowControlEnable;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_FLAG1) != 0)
		sc->sc_MACCtrl |= MC_TxFlowControlEnable;

	stge_link(sc);
}

/*
 * stge_mediastatus:	[ifmedia interface function]
 *
 *	Get the current interface media status.
 */
static void
stge_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sc_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

/*
 * stge_mediachange:	[ifmedia interface function]
 *
 *	Set hardware to newly-selected media.
 */
static int
stge_mediachange(struct ifnet *ifp)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sc_miibus);
	mii_mediachg(mii);

	return (0);
}

static int
stge_eeprom_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		DELAY(1000);
		if ((CSR_READ_2(sc, STGE_EepromCtrl) & EC_EepromBusy) == 0)
			return (0);
	}
	return (1);
}

/*
 * stge_read_eeprom:
 *
 *	Read data from the serial EEPROM.
 */
static void
stge_read_eeprom(struct stge_softc *sc, int offset, uint16_t *data)
{

	if (stge_eeprom_wait(sc))
		device_printf(sc->sc_dev, "EEPROM failed to come ready\n");

	CSR_WRITE_2(sc, STGE_EepromCtrl,
	    EC_EepromAddress(offset) | EC_EepromOpcode(EC_OP_RR));
	if (stge_eeprom_wait(sc))
		device_printf(sc->sc_dev, "EEPROM read timed out\n");
	*data = CSR_READ_2(sc, STGE_EepromData);
}


static int
stge_probe(device_t dev)
{
	struct stge_product *sp;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	
	for (sp = stge_products; sp->stge_name != NULL; sp++) {
		if (vendor == sp->stge_vendorid &&
		    devid == sp->stge_deviceid) {
			device_set_desc(dev, sp->stge_name);
			return (0);
		}
	}

	return (ENXIO);
}

static int
stge_attach(device_t dev)
{
	struct stge_softc *sc;
	struct ifnet *ifp;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t enaddr[ETHER_ADDR_LEN];
	int error, i;
	uint16_t cmd;
	uint32_t val;

	error = 0;
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	ifp = &sc->arpcom.ac_if;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	callout_init(&sc->sc_tick_ch);

#ifndef BURN_BRIDGES
	/*
	 * Handle power management nonsense.
	 */
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t iobase, membase, irq;

		/* Save important PCI config data. */
		iobase = pci_read_config(dev, STGE_PCIR_LOIO, 4);
		membase = pci_read_config(dev, STGE_PCIR_LOMEM, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
			      "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, STGE_PCIR_LOIO, iobase, 4);
		pci_write_config(dev, STGE_PCIR_LOMEM, membase, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif

	/*
	 * Map the device.
	 */
	pci_enable_busmaster(dev);
	cmd = pci_read_config(dev, PCIR_COMMAND, 2);
	val = pci_read_config(dev, STGE_PCIR_LOMEM, 4);

	if ((val & 0x01) != 0) {
		sc->sc_res_rid = STGE_PCIR_LOMEM;
		sc->sc_res_type = SYS_RES_MEMORY;
	} else {
		sc->sc_res_rid = STGE_PCIR_LOIO;
		sc->sc_res_type = SYS_RES_IOPORT;

		val = pci_read_config(dev, sc->sc_res_rid, 4);
		if ((val & 0x01) == 0) {
			device_printf(dev, "couldn't locate IO BAR\n");
			return ENXIO;
		}
	}

	sc->sc_res = bus_alloc_resource_any(dev, sc->sc_res_type,
					    &sc->sc_res_rid, RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(dev, "couldn't allocate resource\n");
		return ENXIO;
	}
	sc->sc_btag = rman_get_bustag(sc->sc_res);
	sc->sc_bhandle = rman_get_bushandle(sc->sc_res);

	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
					    &sc->sc_irq_rid,
					    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "couldn't allocate IRQ\n");
		error = ENXIO;
		goto fail;
	}

	sc->sc_rev = pci_get_revid(dev);

	sc->sc_rxint_nframe = STGE_RXINT_NFRAME_DEFAULT;
	sc->sc_rxint_dmawait = STGE_RXINT_DMAWAIT_DEFAULT;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "rxint_nframe", CTLTYPE_INT|CTLFLAG_RW, &sc->sc_rxint_nframe, 0,
	    sysctl_hw_stge_rxint_nframe, "I", "stge rx interrupt nframe");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "rxint_dmawait", CTLTYPE_INT|CTLFLAG_RW, &sc->sc_rxint_dmawait, 0,
	    sysctl_hw_stge_rxint_dmawait, "I", "stge rx interrupt dmawait");

	error = stge_dma_alloc(sc);
	if (error != 0)
		goto fail;

	/*
	 * Determine if we're copper or fiber.  It affects how we
	 * reset the card.
	 */
	if (CSR_READ_4(sc, STGE_AsicCtrl) & AC_PhyMedia)
		sc->sc_usefiber = 1;
	else
		sc->sc_usefiber = 0;

	/* Load LED configuration from EEPROM. */
	stge_read_eeprom(sc, STGE_EEPROM_LEDMode, &sc->sc_led);

	/*
	 * Reset the chip to a known state.
	 */
	stge_reset(sc, STGE_RESET_FULL);

	/*
	 * Reading the station address from the EEPROM doesn't seem
	 * to work, at least on my sample boards.  Instead, since
	 * the reset sequence does AutoInit, read it from the station
	 * address registers. For Sundance 1023 you can only read it
	 * from EEPROM.
	 */
	if (pci_get_device(dev) != DEVICEID_SUNDANCETI_ST1023) {
		uint16_t v;

		v = CSR_READ_2(sc, STGE_StationAddress0);
		enaddr[0] = v & 0xff;
		enaddr[1] = v >> 8;
		v = CSR_READ_2(sc, STGE_StationAddress1);
		enaddr[2] = v & 0xff;
		enaddr[3] = v >> 8;
		v = CSR_READ_2(sc, STGE_StationAddress2);
		enaddr[4] = v & 0xff;
		enaddr[5] = v >> 8;
		sc->sc_stge1023 = 0;
	} else {
		uint16_t myaddr[ETHER_ADDR_LEN / 2];
		for (i = 0; i <ETHER_ADDR_LEN / 2; i++) {
			stge_read_eeprom(sc, STGE_EEPROM_StationAddress0 + i,
			    &myaddr[i]);
			myaddr[i] = le16toh(myaddr[i]);
		}
		bcopy(myaddr, enaddr, sizeof(enaddr));
		sc->sc_stge1023 = 1;
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = stge_ioctl;
	ifp->if_start = stge_start;
	ifp->if_watchdog = stge_watchdog;
	ifp->if_init = stge_init;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = stge_npoll;
#endif
	ifp->if_mtu = ETHERMTU;
	ifq_set_maxlen(&ifp->if_snd, STGE_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);
	/* Revision B3 and earlier chips have checksum bug. */
	if (sc->sc_rev >= 0x0c) {
		ifp->if_hwassist = STGE_CSUM_FEATURES;
		ifp->if_capabilities = IFCAP_HWCSUM;
	} else {
		ifp->if_hwassist = 0;
		ifp->if_capabilities = 0;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Read some important bits from the PhyCtrl register.
	 */
	sc->sc_PhyCtrl = CSR_READ_1(sc, STGE_PhyCtrl) &
	    (PC_PhyDuplexPolarity | PC_PhyLnkPolarity);

	/* Set up MII bus. */
	if ((error = mii_phy_probe(sc->sc_dev, &sc->sc_miibus, stge_mediachange,
	    stge_mediastatus)) != 0) {
		device_printf(sc->sc_dev, "no PHY found!\n");
		goto fail;
	}

	ether_ifattach(ifp, enaddr, NULL);

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->sc_npoll, ctx, (struct sysctl_oid *)tree,
	    device_get_unit(dev), ifp->if_serializer);
#endif

	/* VLAN capability setup */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
#ifdef notyet
	if (sc->sc_rev >= 0x0c)
		ifp->if_capabilities |= IFCAP_VLAN_HWCSUM;
#endif
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * The manual recommends disabling early transmit, so we
	 * do.  It's disabled anyway, if using IP checksumming,
	 * since the entire packet must be in the FIFO in order
	 * for the chip to perform the checksum.
	 */
	sc->sc_txthresh = 0x0fff;

	/*
	 * Disable MWI if the PCI layer tells us to.
	 */
	sc->sc_DMACtrl = 0;
	if ((cmd & PCIM_CMD_MWRICEN) == 0)
		sc->sc_DMACtrl |= DMAC_MWIDisable;

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq));

	/*
	 * Hookup IRQ
	 */
	error = bus_setup_intr(dev, sc->sc_irq, INTR_MPSAFE, stge_intr, sc,
			       &sc->sc_ih, ifp->if_serializer);
	if (error != 0) {
		ether_ifdetach(ifp);
		device_printf(sc->sc_dev, "couldn't set up IRQ\n");
		goto fail;
	}

fail:
	if (error != 0)
		stge_detach(dev);

	return (error);
}

static int
stge_detach(device_t dev)
{
	struct stge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		/* XXX */
		sc->sc_detach = 1;
		stge_stop(sc);
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->sc_miibus != NULL)
		device_delete_child(dev, sc->sc_miibus);
	bus_generic_detach(dev);

	stge_dma_free(sc);

	if (sc->sc_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq);
	}
	if (sc->sc_res != NULL) {
		bus_release_resource(dev, sc->sc_res_type, sc->sc_res_rid,
				     sc->sc_res);
	}

	return (0);
}

static int
stge_dma_alloc(struct stge_softc *sc)
{
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	int error, i;

	/* create parent tag. */
	error = bus_dma_tag_create(NULL,	/* parent */
		    1, 0,			/* algnmnt, boundary */
		    STGE_DMA_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
		    0,				/* nsegments */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
		    0,				/* flags */
		    &sc->sc_cdata.stge_parent_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to create parent DMA tag\n");
		return error;
	}

	/* allocate Tx ring. */
	sc->sc_rdata.stge_tx_ring =
		bus_dmamem_coherent_any(sc->sc_cdata.stge_parent_tag,
			STGE_RING_ALIGN, STGE_TX_RING_SZ,
			BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&sc->sc_cdata.stge_tx_ring_tag,
			&sc->sc_cdata.stge_tx_ring_map,
			&sc->sc_rdata.stge_tx_ring_paddr);
	if (sc->sc_rdata.stge_tx_ring == NULL) {
		device_printf(sc->sc_dev,
		    "failed to allocate Tx ring\n");
		return ENOMEM;
	}

	/* allocate Rx ring. */
	sc->sc_rdata.stge_rx_ring =
		bus_dmamem_coherent_any(sc->sc_cdata.stge_parent_tag,
			STGE_RING_ALIGN, STGE_RX_RING_SZ,
			BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&sc->sc_cdata.stge_rx_ring_tag,
			&sc->sc_cdata.stge_rx_ring_map,
			&sc->sc_rdata.stge_rx_ring_paddr);
	if (sc->sc_rdata.stge_rx_ring == NULL) {
		device_printf(sc->sc_dev,
		    "failed to allocate Rx ring\n");
		return ENOMEM;
	}

	/* create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    STGE_JUMBO_FRAMELEN,	/* maxsize */
		    STGE_MAXTXSEGS,		/* nsegments */
		    STGE_MAXSGSIZE,		/* maxsegsize */
		    BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,/* flags */
		    &sc->sc_cdata.stge_tx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to allocate Tx DMA tag\n");
		return error;
	}

	/* create DMA maps for Tx buffers. */
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		txd = &sc->sc_cdata.stge_txdesc[i];
		error = bus_dmamap_create(sc->sc_cdata.stge_tx_tag,
				BUS_DMA_WAITOK, &txd->tx_dmamap);
		if (error != 0) {
			int j;

			for (j = 0; j < i; ++j) {
				txd = &sc->sc_cdata.stge_txdesc[j];
				bus_dmamap_destroy(sc->sc_cdata.stge_tx_tag,
					txd->tx_dmamap);
			}
			bus_dma_tag_destroy(sc->sc_cdata.stge_tx_tag);
			sc->sc_cdata.stge_tx_tag = NULL;

			device_printf(sc->sc_dev,
			    "failed to create Tx dmamap\n");
			return error;
		}
	}

	/* create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    MCLBYTES,			/* maxsize */
		    1,				/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,/* flags */
		    &sc->sc_cdata.stge_rx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to allocate Rx DMA tag\n");
		return error;
	}

	/* create DMA maps for Rx buffers. */
	error = bus_dmamap_create(sc->sc_cdata.stge_rx_tag, BUS_DMA_WAITOK,
			&sc->sc_cdata.stge_rx_sparemap);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to create spare Rx dmamap\n");
		bus_dma_tag_destroy(sc->sc_cdata.stge_rx_tag);
		sc->sc_cdata.stge_rx_tag = NULL;
		return error;
	}
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		rxd = &sc->sc_cdata.stge_rxdesc[i];
		error = bus_dmamap_create(sc->sc_cdata.stge_rx_tag,
				BUS_DMA_WAITOK, &rxd->rx_dmamap);
		if (error != 0) {
			int j;

			for (j = 0; j < i; ++j) {
				rxd = &sc->sc_cdata.stge_rxdesc[j];
				bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
					rxd->rx_dmamap);
			}
			bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
				sc->sc_cdata.stge_rx_sparemap);
			bus_dma_tag_destroy(sc->sc_cdata.stge_rx_tag);
			sc->sc_cdata.stge_rx_tag = NULL;

			device_printf(sc->sc_dev,
			    "failed to create Rx dmamap\n");
			return error;
		}
	}
	return 0;
}

static void
stge_dma_free(struct stge_softc *sc)
{
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	int i;

	/* Tx ring */
	if (sc->sc_cdata.stge_tx_ring_tag) {
		bus_dmamap_unload(sc->sc_cdata.stge_tx_ring_tag,
		    sc->sc_cdata.stge_tx_ring_map);
		bus_dmamem_free(sc->sc_cdata.stge_tx_ring_tag,
		    sc->sc_rdata.stge_tx_ring,
		    sc->sc_cdata.stge_tx_ring_map);
		bus_dma_tag_destroy(sc->sc_cdata.stge_tx_ring_tag);
	}

	/* Rx ring */
	if (sc->sc_cdata.stge_rx_ring_tag) {
		bus_dmamap_unload(sc->sc_cdata.stge_rx_ring_tag,
		    sc->sc_cdata.stge_rx_ring_map);
		bus_dmamem_free(sc->sc_cdata.stge_rx_ring_tag,
		    sc->sc_rdata.stge_rx_ring,
		    sc->sc_cdata.stge_rx_ring_map);
		bus_dma_tag_destroy(sc->sc_cdata.stge_rx_ring_tag);
	}

	/* Tx buffers */
	if (sc->sc_cdata.stge_tx_tag) {
		for (i = 0; i < STGE_TX_RING_CNT; i++) {
			txd = &sc->sc_cdata.stge_txdesc[i];
			bus_dmamap_destroy(sc->sc_cdata.stge_tx_tag,
			    txd->tx_dmamap);
		}
		bus_dma_tag_destroy(sc->sc_cdata.stge_tx_tag);
	}

	/* Rx buffers */
	if (sc->sc_cdata.stge_rx_tag) {
		for (i = 0; i < STGE_RX_RING_CNT; i++) {
			rxd = &sc->sc_cdata.stge_rxdesc[i];
			bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
			    rxd->rx_dmamap);
		}
		bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
		    sc->sc_cdata.stge_rx_sparemap);
		bus_dma_tag_destroy(sc->sc_cdata.stge_rx_tag);
	}

	/* Top level tag */
	if (sc->sc_cdata.stge_parent_tag)
		bus_dma_tag_destroy(sc->sc_cdata.stge_parent_tag);
}

/*
 * stge_shutdown:
 *
 *	Make sure the interface is stopped at reboot time.
 */
static void
stge_shutdown(device_t dev)
{
	struct stge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	stge_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
stge_suspend(device_t dev)
{
	struct stge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	stge_stop(sc);
	sc->sc_suspended = 1;
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

static int
stge_resume(device_t dev)
{
	struct stge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	if (ifp->if_flags & IFF_UP)
		stge_init(sc);
	sc->sc_suspended = 0;
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

static void
stge_dma_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		DELAY(2);
		if ((CSR_READ_4(sc, STGE_DMACtrl) & DMAC_TxDMAInProg) == 0)
			break;
	}

	if (i == STGE_TIMEOUT)
		device_printf(sc->sc_dev, "DMA wait timed out\n");
}

static int
stge_encap(struct stge_softc *sc, struct mbuf **m_head)
{
	struct stge_txdesc *txd;
	struct stge_tfd *tfd;
	struct mbuf *m;
	bus_dma_segment_t txsegs[STGE_MAXTXSEGS];
	int error, i, si, nsegs;
	uint64_t csum_flags, tfc;

	txd = STAILQ_FIRST(&sc->sc_cdata.stge_txfreeq);
	KKASSERT(txd != NULL);

	error =  bus_dmamap_load_mbuf_defrag(sc->sc_cdata.stge_tx_tag,
			txd->tx_dmamap, m_head,
			txsegs, STGE_MAXTXSEGS, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(*m_head);
		*m_head = NULL;
		return (error);
	}
	bus_dmamap_sync(sc->sc_cdata.stge_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	m = *m_head;

	csum_flags = 0;
	if ((m->m_pkthdr.csum_flags & STGE_CSUM_FEATURES) != 0) {
		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= TFD_IPChecksumEnable;
		if (m->m_pkthdr.csum_flags & CSUM_TCP)
			csum_flags |= TFD_TCPChecksumEnable;
		else if (m->m_pkthdr.csum_flags & CSUM_UDP)
			csum_flags |= TFD_UDPChecksumEnable;
	}

	si = sc->sc_cdata.stge_tx_prod;
	tfd = &sc->sc_rdata.stge_tx_ring[si];
	for (i = 0; i < nsegs; i++) {
		tfd->tfd_frags[i].frag_word0 =
		    htole64(FRAG_ADDR(txsegs[i].ds_addr) |
		    FRAG_LEN(txsegs[i].ds_len));
	}
	sc->sc_cdata.stge_tx_cnt++;

	tfc = TFD_FrameId(si) | TFD_WordAlign(TFD_WordAlign_disable) |
	    TFD_FragCount(nsegs) | csum_flags;
	if (sc->sc_cdata.stge_tx_cnt >= STGE_TX_HIWAT)
		tfc |= TFD_TxDMAIndicate;

	/* Update producer index. */
	sc->sc_cdata.stge_tx_prod = (si + 1) % STGE_TX_RING_CNT;

	/* Check if we have a VLAN tag to insert. */
	if (m->m_flags & M_VLANTAG)
		tfc |= TFD_VLANTagInsert | TFD_VID(m->m_pkthdr.ether_vlantag);
	tfd->tfd_control = htole64(tfc);

	/* Update Tx Queue. */
	STAILQ_REMOVE_HEAD(&sc->sc_cdata.stge_txfreeq, tx_q);
	STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txbusyq, txd, tx_q);
	txd->tx_m = m;

	return (0);
}

/*
 * stge_start:		[ifnet interface function]
 *
 *	Start packet transmission on the interface.
 */
static void
stge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct stge_softc *sc;
	struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	enq = 0;
	while (!ifq_is_empty(&ifp->if_snd)) {
		if (sc->sc_cdata.stge_tx_cnt >= STGE_TX_HIWAT) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (stge_encap(sc, &m_head)) {
			if (sc->sc_cdata.stge_tx_cnt == 0) {
				continue;
			} else {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}
		}
		enq = 1;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq) {
		/* Transmit */
		CSR_WRITE_4(sc, STGE_DMACtrl, DMAC_TxDMAPollNow);

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}
}

/*
 * stge_watchdog:	[ifnet interface function]
 *
 *	Watchdog timer handler.
 */
static void
stge_watchdog(struct ifnet *ifp)
{
	ASSERT_SERIALIZED(ifp->if_serializer);

	if_printf(ifp, "device timeout\n");
	IFNET_STAT_INC(ifp, oerrors, 1);
	stge_init(ifp->if_softc);
}

/*
 * stge_ioctl:		[ifnet interface function]
 *
 *	Handle control requests from the operator.
 */
static int
stge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct stge_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error, mask;

	ASSERT_SERIALIZED(ifp->if_serializer);

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > STGE_JUMBO_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			stge_init(sc);
		}
		break;
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_flags & IFF_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->sc_if_flags)
				    & IFF_PROMISC) != 0)
					stge_set_filter(sc);
			} else {
				if (sc->sc_detach == 0)
					stge_init(sc);
			}
		} else {
			if ((ifp->if_flags & IFF_RUNNING) != 0)
				stge_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if ((ifp->if_flags & IFF_RUNNING) != 0)
			stge_set_multi(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->sc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_HWCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_HWCSUM;
			if ((IFCAP_HWCSUM & ifp->if_capenable) != 0 &&
			    (IFCAP_HWCSUM & ifp->if_capabilities) != 0)
				ifp->if_hwassist = STGE_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if (ifp->if_flags & IFF_RUNNING)
				stge_vlan_setup(sc);
		}
#if 0
		VLAN_CAPABILITIES(ifp);
#endif
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
stge_link(struct stge_softc *sc)
{
	uint32_t v, ac;
	int i;

	/*
	 * Update STGE_MACCtrl register depending on link status.
	 * (duplex, flow control etc)
	 */
	v = ac = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v &= ~(MC_DuplexSelect|MC_RxFlowControlEnable|MC_TxFlowControlEnable);
	v |= sc->sc_MACCtrl;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	if (((ac ^ sc->sc_MACCtrl) & MC_DuplexSelect) != 0) {
		/* Duplex setting changed, reset Tx/Rx functions. */
		ac = CSR_READ_4(sc, STGE_AsicCtrl);
		ac |= AC_TxReset | AC_RxReset;
		CSR_WRITE_4(sc, STGE_AsicCtrl, ac);
		for (i = 0; i < STGE_TIMEOUT; i++) {
			DELAY(100);
			if ((CSR_READ_4(sc, STGE_AsicCtrl) & AC_ResetBusy) == 0)
				break;
		}
		if (i == STGE_TIMEOUT)
			device_printf(sc->sc_dev, "reset failed to complete\n");
	}
}

static __inline int
stge_tx_error(struct stge_softc *sc)
{
	uint32_t txstat;
	int error;

	for (error = 0;;) {
		txstat = CSR_READ_4(sc, STGE_TxStatus);
		if ((txstat & TS_TxComplete) == 0)
			break;
		/* Tx underrun */
		if ((txstat & TS_TxUnderrun) != 0) {
			/*
			 * XXX
			 * There should be a more better way to recover
			 * from Tx underrun instead of a full reset.
			 */
			if (sc->sc_nerr++ < STGE_MAXERR)
				device_printf(sc->sc_dev, "Tx underrun, "
				    "resetting...\n");
			if (sc->sc_nerr == STGE_MAXERR)
				device_printf(sc->sc_dev, "too many errors; "
				    "not reporting any more\n");
			error = -1;
			break;
		}
		/* Maximum/Late collisions, Re-enable Tx MAC. */
		if ((txstat & (TS_MaxCollisions|TS_LateCollision)) != 0)
			CSR_WRITE_4(sc, STGE_MACCtrl,
			    (CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK) |
			    MC_TxEnable);
	}

	return (error);
}

/*
 * stge_intr:
 *
 *	Interrupt service routine.
 */
static void
stge_intr(void *arg)
{
	struct stge_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int reinit;
	uint16_t status;

	ASSERT_SERIALIZED(ifp->if_serializer);

	status = CSR_READ_2(sc, STGE_IntStatus);
	if (sc->sc_suspended || (status & IS_InterruptStatus) == 0)
		return;

	/* Disable interrupts. */
	for (reinit = 0;;) {
		status = CSR_READ_2(sc, STGE_IntStatusAck);
		status &= sc->sc_IntEnable;
		if (status == 0)
			break;
		/* Host interface errors. */
		if ((status & IS_HostError) != 0) {
			device_printf(sc->sc_dev,
			    "Host interface error, resetting...\n");
			reinit = 1;
			goto force_init;
		}

		/* Receive interrupts. */
		if ((status & IS_RxDMAComplete) != 0) {
			stge_rxeof(sc, -1);
			if ((status & IS_RFDListEnd) != 0)
				CSR_WRITE_4(sc, STGE_DMACtrl,
				    DMAC_RxDMAPollNow);
		}

		/* Transmit interrupts. */
		if ((status & (IS_TxDMAComplete | IS_TxComplete)) != 0)
			stge_txeof(sc);

		/* Transmission errors.*/
		if ((status & IS_TxComplete) != 0) {
			if ((reinit = stge_tx_error(sc)) != 0)
				break;
		}
	}

force_init:
	if (reinit != 0)
		stge_init(sc);

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/* Try to get more packets going. */
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * stge_txeof:
 *
 *	Helper; handle transmit interrupts.
 */
static void
stge_txeof(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct stge_txdesc *txd;
	uint64_t control;
	int cons;

	txd = STAILQ_FIRST(&sc->sc_cdata.stge_txbusyq);
	if (txd == NULL)
		return;

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (cons = sc->sc_cdata.stge_tx_cons;;
	    cons = (cons + 1) % STGE_TX_RING_CNT) {
		if (sc->sc_cdata.stge_tx_cnt <= 0)
			break;
		control = le64toh(sc->sc_rdata.stge_tx_ring[cons].tfd_control);
		if ((control & TFD_TFDDone) == 0)
			break;
		sc->sc_cdata.stge_tx_cnt--;

		bus_dmamap_unload(sc->sc_cdata.stge_tx_tag, txd->tx_dmamap);

		/* Output counter is updated with statistics register */
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		STAILQ_REMOVE_HEAD(&sc->sc_cdata.stge_txbusyq, tx_q);
		STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txfreeq, txd, tx_q);
		txd = STAILQ_FIRST(&sc->sc_cdata.stge_txbusyq);
	}
	sc->sc_cdata.stge_tx_cons = cons;

	if (sc->sc_cdata.stge_tx_cnt < STGE_TX_HIWAT)
		ifq_clr_oactive(&ifp->if_snd);
	if (sc->sc_cdata.stge_tx_cnt == 0)
		ifp->if_timer = 0;
}

static __inline void
stge_discard_rxbuf(struct stge_softc *sc, int idx)
{
	struct stge_rfd *rfd;

	rfd = &sc->sc_rdata.stge_rx_ring[idx];
	rfd->rfd_status = 0;
}

#ifndef __x86_64__
/*
 * It seems that TC9021's DMA engine has alignment restrictions in
 * DMA scatter operations. The first DMA segment has no address
 * alignment restrictins but the rest should be aligned on 4(?) bytes
 * boundary. Otherwise it would corrupt random memory. Since we don't
 * know which one is used for the first segment in advance we simply
 * don't align at all.
 * To avoid copying over an entire frame to align, we allocate a new
 * mbuf and copy ethernet header to the new mbuf. The new mbuf is
 * prepended into the existing mbuf chain.
 */
static __inline struct mbuf *
stge_fixup_rx(struct stge_softc *sc, struct mbuf *m)
{
	struct mbuf *n;

	n = NULL;
	if (m->m_len <= (MCLBYTES - ETHER_HDR_LEN)) {
		bcopy(m->m_data, m->m_data + ETHER_HDR_LEN, m->m_len);
		m->m_data += ETHER_HDR_LEN;
		n = m;
	} else {
		MGETHDR(n, M_NOWAIT, MT_DATA);
		if (n != NULL) {
			bcopy(m->m_data, n->m_data, ETHER_HDR_LEN);
			m->m_data += ETHER_HDR_LEN;
			m->m_len -= ETHER_HDR_LEN;
			n->m_len = ETHER_HDR_LEN;
			M_MOVE_PKTHDR(n, m);
			n->m_next = m;
		} else
			m_freem(m);
	}

	return (n);
}
#endif

/*
 * stge_rxeof:
 *
 *	Helper; handle receive interrupts.
 */
static void
stge_rxeof(struct stge_softc *sc, int count)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct stge_rxdesc *rxd;
	struct mbuf *mp, *m;
	uint64_t status64;
	uint32_t status;
	int cons, prog;

	prog = 0;
	for (cons = sc->sc_cdata.stge_rx_cons; prog < STGE_RX_RING_CNT;
	    prog++, cons = (cons + 1) % STGE_RX_RING_CNT) {
#ifdef IFPOLL_ENABLE
		if (count >= 0 && count-- == 0)
			break;
#endif

		status64 = le64toh(sc->sc_rdata.stge_rx_ring[cons].rfd_status);
		status = RFD_RxStatus(status64);
		if ((status & RFD_RFDDone) == 0)
			break;

		prog++;
		rxd = &sc->sc_cdata.stge_rxdesc[cons];
		mp = rxd->rx_m;

		/*
		 * If the packet had an error, drop it.  Note we count
		 * the error later in the periodic stats update.
		 */
		if ((status & RFD_FrameEnd) != 0 && (status &
		    (RFD_RxFIFOOverrun | RFD_RxRuntFrame |
		    RFD_RxAlignmentError | RFD_RxFCSError |
		    RFD_RxLengthError)) != 0) {
			stge_discard_rxbuf(sc, cons);
			if (sc->sc_cdata.stge_rxhead != NULL) {
				m_freem(sc->sc_cdata.stge_rxhead);
				STGE_RXCHAIN_RESET(sc);
			}
			continue;
		}
		/*
		 * Add a new receive buffer to the ring.
		 */
		if (stge_newbuf(sc, cons, 0) != 0) {
			IFNET_STAT_INC(ifp, iqdrops, 1);
			stge_discard_rxbuf(sc, cons);
			if (sc->sc_cdata.stge_rxhead != NULL) {
				m_freem(sc->sc_cdata.stge_rxhead);
				STGE_RXCHAIN_RESET(sc);
			}
			continue;
		}

		if ((status & RFD_FrameEnd) != 0)
			mp->m_len = RFD_RxDMAFrameLen(status) -
			    sc->sc_cdata.stge_rxlen;
		sc->sc_cdata.stge_rxlen += mp->m_len;

		/* Chain mbufs. */
		if (sc->sc_cdata.stge_rxhead == NULL) {
			sc->sc_cdata.stge_rxhead = mp;
			sc->sc_cdata.stge_rxtail = mp;
		} else {
			mp->m_flags &= ~M_PKTHDR;
			sc->sc_cdata.stge_rxtail->m_next = mp;
			sc->sc_cdata.stge_rxtail = mp;
		}

		if ((status & RFD_FrameEnd) != 0) {
			m = sc->sc_cdata.stge_rxhead;
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = sc->sc_cdata.stge_rxlen;

			if (m->m_pkthdr.len > sc->sc_if_framesize) {
				m_freem(m);
				STGE_RXCHAIN_RESET(sc);
				continue;
			}
			/*
			 * Set the incoming checksum information for
			 * the packet.
			 */
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
				if ((status & RFD_IPDetected) != 0) {
					m->m_pkthdr.csum_flags |=
						CSUM_IP_CHECKED;
					if ((status & RFD_IPError) == 0)
						m->m_pkthdr.csum_flags |=
						    CSUM_IP_VALID;
				}
				if (((status & RFD_TCPDetected) != 0 &&
				    (status & RFD_TCPError) == 0) ||
				    ((status & RFD_UDPDetected) != 0 &&
				    (status & RFD_UDPError) == 0)) {
					m->m_pkthdr.csum_flags |=
					    (CSUM_DATA_VALID |
					     CSUM_PSEUDO_HDR |
					     CSUM_FRAG_NOT_CHECKED);
					m->m_pkthdr.csum_data = 0xffff;
				}
			}

#ifndef __x86_64__
			if (sc->sc_if_framesize > (MCLBYTES - ETHER_ALIGN)) {
				if ((m = stge_fixup_rx(sc, m)) == NULL) {
					STGE_RXCHAIN_RESET(sc);
					continue;
				}
			}
#endif

			/* Check for VLAN tagged packets. */
			if ((status & RFD_VLANDetected) != 0 &&
			    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
				m->m_flags |= M_VLANTAG;
				m->m_pkthdr.ether_vlantag = RFD_TCI(status64);
			}
			/* Pass it on. */
			ifp->if_input(ifp, m, NULL, -1);

			STGE_RXCHAIN_RESET(sc);
		}
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->sc_cdata.stge_rx_cons = cons;
	}
}

#ifdef IFPOLL_ENABLE

static void
stge_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct stge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_npoll.ifpc_stcount-- == 0) {
		uint16_t status;

		sc->sc_npoll.ifpc_stcount = sc->sc_npoll.ifpc_stfrac;

		status = CSR_READ_2(sc, STGE_IntStatus);
		status &= sc->sc_IntEnable;
		if (status != 0) {
			if (status & IS_HostError) {
				device_printf(sc->sc_dev,
				"Host interface error, "
				"resetting...\n");
				stge_init(sc);
			}
			if ((status & IS_TxComplete) != 0 &&
			    stge_tx_error(sc) != 0)
				stge_init(sc);
		}
	}

	stge_rxeof(sc, count);
	stge_txeof(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
stge_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct stge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->sc_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = stge_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			CSR_WRITE_2(sc, STGE_IntEnable, 0);
			sc->sc_npoll.ifpc_stcount = 0;
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq));
	}
}

#endif	/* IFPOLL_ENABLE */

/*
 * stge_tick:
 *
 *	One second timer, used to tick the MII.
 */
static void
stge_tick(void *arg)
{
	struct stge_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	lwkt_serialize_enter(ifp->if_serializer);

	mii = device_get_softc(sc->sc_miibus);
	mii_tick(mii);

	/* Update statistics counters. */
	stge_stats_update(sc);

	/*
	 * Relcaim any pending Tx descriptors to release mbufs in a
	 * timely manner as we don't generate Tx completion interrupts
	 * for every frame. This limits the delay to a maximum of one
	 * second.
	 */
	if (sc->sc_cdata.stge_tx_cnt != 0)
		stge_txeof(sc);

	callout_reset(&sc->sc_tick_ch, hz, stge_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

/*
 * stge_stats_update:
 *
 *	Read the TC9021 statistics counters.
 */
static void
stge_stats_update(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	CSR_READ_4(sc,STGE_OctetRcvOk);

	IFNET_STAT_INC(ifp, ipackets, CSR_READ_4(sc, STGE_FramesRcvdOk));

	IFNET_STAT_INC(ifp, ierrors, CSR_READ_2(sc, STGE_FramesLostRxErrors));

	CSR_READ_4(sc, STGE_OctetXmtdOk);

	IFNET_STAT_INC(ifp, opackets, CSR_READ_4(sc, STGE_FramesXmtdOk));

	IFNET_STAT_INC(ifp, collisions,
	    CSR_READ_4(sc, STGE_LateCollisions) +
	    CSR_READ_4(sc, STGE_MultiColFrames) +
	    CSR_READ_4(sc, STGE_SingleColFrames));

	IFNET_STAT_INC(ifp, oerrors,
	    CSR_READ_2(sc, STGE_FramesAbortXSColls) +
	    CSR_READ_2(sc, STGE_FramesWEXDeferal));
}

/*
 * stge_reset:
 *
 *	Perform a soft reset on the TC9021.
 */
static void
stge_reset(struct stge_softc *sc, uint32_t how)
{
	uint32_t ac;
	uint8_t v;
	int i, dv;

	dv = 5000;
	ac = CSR_READ_4(sc, STGE_AsicCtrl);
	switch (how) {
	case STGE_RESET_TX:
		ac |= AC_TxReset | AC_FIFO;
		dv = 100;
		break;
	case STGE_RESET_RX:
		ac |= AC_RxReset | AC_FIFO;
		dv = 100;
		break;
	case STGE_RESET_FULL:
	default:
		/*
		 * Only assert RstOut if we're fiber.  We need GMII clocks
		 * to be present in order for the reset to complete on fiber
		 * cards.
		 */
		ac |= AC_GlobalReset | AC_RxReset | AC_TxReset |
		    AC_DMA | AC_FIFO | AC_Network | AC_Host | AC_AutoInit |
		    (sc->sc_usefiber ? AC_RstOut : 0);
		break;
	}

	CSR_WRITE_4(sc, STGE_AsicCtrl, ac);

	/* Account for reset problem at 10Mbps. */
	DELAY(dv);

	for (i = 0; i < STGE_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, STGE_AsicCtrl) & AC_ResetBusy) == 0)
			break;
		DELAY(dv);
	}

	if (i == STGE_TIMEOUT)
		device_printf(sc->sc_dev, "reset failed to complete\n");

	/* Set LED, from Linux IPG driver. */
	ac = CSR_READ_4(sc, STGE_AsicCtrl);
	ac &= ~(AC_LEDMode | AC_LEDSpeed | AC_LEDModeBit1);
	if ((sc->sc_led & 0x01) != 0)
		ac |= AC_LEDMode;
	if ((sc->sc_led & 0x03) != 0)
		ac |= AC_LEDModeBit1;
	if ((sc->sc_led & 0x08) != 0)
		ac |= AC_LEDSpeed;
	CSR_WRITE_4(sc, STGE_AsicCtrl, ac);

	/* Set PHY, from Linux IPG driver */
	v = CSR_READ_1(sc, STGE_PhySet);
	v &= ~(PS_MemLenb9b | PS_MemLen | PS_NonCompdet);
	v |= ((sc->sc_led & 0x70) >> 4);
	CSR_WRITE_1(sc, STGE_PhySet, v);
}

/*
 * stge_init:		[ ifnet interface function ]
 *
 *	Initialize the interface.
 */
static void
stge_init(void *xsc)
{
	struct stge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	uint16_t eaddr[3];
	uint32_t v;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->sc_miibus);

	/*
	 * Cancel any pending I/O.
	 */
	stge_stop(sc);

	/* Init descriptors. */
	error = stge_init_rx_ring(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "initialization failed: no memory for rx buffers\n");
		stge_stop(sc);
		goto out;
	}
	stge_init_tx_ring(sc);

	/* Set the station address. */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_2(sc, STGE_StationAddress0, htole16(eaddr[0]));
	CSR_WRITE_2(sc, STGE_StationAddress1, htole16(eaddr[1]));
	CSR_WRITE_2(sc, STGE_StationAddress2, htole16(eaddr[2]));

	/*
	 * Set the statistics masks.  Disable all the RMON stats,
	 * and disable selected stats in the non-RMON stats registers.
	 */
	CSR_WRITE_4(sc, STGE_RMONStatisticsMask, 0xffffffff);
	CSR_WRITE_4(sc, STGE_StatisticsMask,
	    (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 5) |
	    (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10) |
	    (1U << 13) | (1U << 14) | (1U << 15) | (1U << 19) | (1U << 20) |
	    (1U << 21));

	/* Set up the receive filter. */
	stge_set_filter(sc);
	/* Program multicast filter. */
	stge_set_multi(sc);

	/*
	 * Give the transmit and receive ring to the chip.
	 */
	CSR_WRITE_4(sc, STGE_TFDListPtrHi,
	    STGE_ADDR_HI(STGE_TX_RING_ADDR(sc, 0)));
	CSR_WRITE_4(sc, STGE_TFDListPtrLo,
	    STGE_ADDR_LO(STGE_TX_RING_ADDR(sc, 0)));

	CSR_WRITE_4(sc, STGE_RFDListPtrHi,
	    STGE_ADDR_HI(STGE_RX_RING_ADDR(sc, 0)));
	CSR_WRITE_4(sc, STGE_RFDListPtrLo,
	    STGE_ADDR_LO(STGE_RX_RING_ADDR(sc, 0)));

	/*
	 * Initialize the Tx auto-poll period.  It's OK to make this number
	 * large (255 is the max, but we use 127) -- we explicitly kick the
	 * transmit engine when there's actually a packet.
	 */
	CSR_WRITE_1(sc, STGE_TxDMAPollPeriod, 127);

	/* ..and the Rx auto-poll period. */
	CSR_WRITE_1(sc, STGE_RxDMAPollPeriod, 1);

	/* Initialize the Tx start threshold. */
	CSR_WRITE_2(sc, STGE_TxStartThresh, sc->sc_txthresh);

	/* Rx DMA thresholds, from Linux */
	CSR_WRITE_1(sc, STGE_RxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_RxDMAUrgentThresh, 0x30);

	/* Rx early threhold, from Linux */
	CSR_WRITE_2(sc, STGE_RxEarlyThresh, 0x7ff);

	/* Tx DMA thresholds, from Linux */
	CSR_WRITE_1(sc, STGE_TxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_TxDMAUrgentThresh, 0x04);

	/*
	 * Initialize the Rx DMA interrupt control register.  We
	 * request an interrupt after every incoming packet, but
	 * defer it for sc_rxint_dmawait us. When the number of
	 * interrupts pending reaches STGE_RXINT_NFRAME, we stop
	 * deferring the interrupt, and signal it immediately.
	 */
	CSR_WRITE_4(sc, STGE_RxDMAIntCtrl,
	    RDIC_RxFrameCount(sc->sc_rxint_nframe) |
	    RDIC_RxDMAWaitTime(STGE_RXINT_USECS2TICK(sc->sc_rxint_dmawait)));

	/*
	 * Initialize the interrupt mask.
	 */
	sc->sc_IntEnable = IS_HostError | IS_TxComplete |
	    IS_TxDMAComplete | IS_RxDMAComplete | IS_RFDListEnd;
#ifdef IFPOLL_ENABLE
	/* Disable interrupts if we are polling. */
	if (ifp->if_flags & IFF_NPOLLING) {
		CSR_WRITE_2(sc, STGE_IntEnable, 0);
		sc->sc_npoll.ifpc_stcount = 0;
	} else
#endif
	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/*
	 * Configure the DMA engine.
	 * XXX Should auto-tune TxBurstLimit.
	 */
	CSR_WRITE_4(sc, STGE_DMACtrl, sc->sc_DMACtrl | DMAC_TxBurstLimit(3));

	/*
	 * Send a PAUSE frame when we reach 29,696 bytes in the Rx
	 * FIFO, and send an un-PAUSE frame when we reach 3056 bytes
	 * in the Rx FIFO.
	 */
	CSR_WRITE_2(sc, STGE_FlowOnTresh, 29696 / 16);
	CSR_WRITE_2(sc, STGE_FlowOffThresh, 3056 / 16);

	/*
	 * Set the maximum frame size.
	 */
	sc->sc_if_framesize = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
	CSR_WRITE_2(sc, STGE_MaxFrameSize, sc->sc_if_framesize);

	/*
	 * Initialize MacCtrl -- do it before setting the media,
	 * as setting the media will actually program the register.
	 *
	 * Note: We have to poke the IFS value before poking
	 * anything else.
	 */
	/* Tx/Rx MAC should be disabled before programming IFS.*/
	CSR_WRITE_4(sc, STGE_MACCtrl, MC_IFSSelect(MC_IFS96bit));

	stge_vlan_setup(sc);

	if (sc->sc_rev >= 6) {		/* >= B.2 */
		/* Multi-frag frame bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0200);

		/* Tx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0010);
		/* Tx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0020);
	}

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v |= MC_StatisticsEnable | MC_TxEnable | MC_RxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	/*
	 * It seems that transmitting frames without checking the state of
	 * Rx/Tx MAC wedge the hardware.
	 */
	stge_start_tx(sc);
	stge_start_rx(sc);

	/*
	 * Set the current media.
	 */
	mii_mediachg(mii);

	/*
	 * Start the one second MII clock.
	 */
	callout_reset(&sc->sc_tick_ch, hz, stge_tick, sc);

	/*
	 * ...all done!
	 */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

 out:
	if (error != 0)
		device_printf(sc->sc_dev, "interface not running\n");
}

static void
stge_vlan_setup(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t v;

	/*
	 * The NIC always copy a VLAN tag regardless of STGE_MACCtrl
	 * MC_AutoVLANuntagging bit.
	 * MC_AutoVLANtagging bit selects which VLAN source to use
	 * between STGE_VLANTag and TFC. However TFC TFD_VLANTagInsert
	 * bit has priority over MC_AutoVLANtagging bit. So we always
	 * use TFC instead of STGE_VLANTag register.
	 */
	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		v |= MC_AutoVLANuntagging;
	else
		v &= ~MC_AutoVLANuntagging;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
}

/*
 *	Stop transmission on the interface.
 */
static void
stge_stop(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	uint32_t v;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * Stop the one second clock.
	 */
	callout_stop(&sc->sc_tick_ch);

	/*
	 * Reset the chip to a known state.
	 */
	stge_reset(sc, STGE_RESET_FULL);

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_2(sc, STGE_IntEnable, 0);

	/*
	 * Stop receiver, transmitter, and stats update.
	 */
	stge_stop_rx(sc);
	stge_stop_tx(sc);
	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v |= MC_StatisticsDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);

	/*
	 * Stop the transmit and receive DMA.
	 */
	stge_dma_wait(sc);
	CSR_WRITE_4(sc, STGE_TFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_TFDListPtrLo, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrLo, 0);

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		rxd = &sc->sc_cdata.stge_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_unload(sc->sc_cdata.stge_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		txd = &sc->sc_cdata.stge_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_unload(sc->sc_cdata.stge_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
        }

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
}

static void
stge_start_tx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_TxEnabled) != 0)
		return;
	v |= MC_TxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	CSR_WRITE_1(sc, STGE_TxDMAPollPeriod, 127);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_TxEnabled) != 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Starting Tx MAC timed out\n");
}

static void
stge_start_rx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_RxEnabled) != 0)
		return;
	v |= MC_RxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	CSR_WRITE_1(sc, STGE_RxDMAPollPeriod, 1);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_RxEnabled) != 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Starting Rx MAC timed out\n");
}

static void
stge_stop_tx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_TxEnabled) == 0)
		return;
	v |= MC_TxDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_TxEnabled) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Stopping Tx MAC timed out\n");
}

static void
stge_stop_rx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_RxEnabled) == 0)
		return;
	v |= MC_RxDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_RxEnabled) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Stopping Rx MAC timed out\n");
}

static void
stge_init_tx_ring(struct stge_softc *sc)
{
	struct stge_ring_data *rd;
	struct stge_txdesc *txd;
	bus_addr_t addr;
	int i;

	STAILQ_INIT(&sc->sc_cdata.stge_txfreeq);
	STAILQ_INIT(&sc->sc_cdata.stge_txbusyq);

	sc->sc_cdata.stge_tx_prod = 0;
	sc->sc_cdata.stge_tx_cons = 0;
	sc->sc_cdata.stge_tx_cnt = 0;

	rd = &sc->sc_rdata;
	bzero(rd->stge_tx_ring, STGE_TX_RING_SZ);
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		if (i == (STGE_TX_RING_CNT - 1))
			addr = STGE_TX_RING_ADDR(sc, 0);
		else
			addr = STGE_TX_RING_ADDR(sc, i + 1);
		rd->stge_tx_ring[i].tfd_next = htole64(addr);
		rd->stge_tx_ring[i].tfd_control = htole64(TFD_TFDDone);
		txd = &sc->sc_cdata.stge_txdesc[i];
		STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txfreeq, txd, tx_q);
	}
}

static int
stge_init_rx_ring(struct stge_softc *sc)
{
	struct stge_ring_data *rd;
	bus_addr_t addr;
	int i;

	sc->sc_cdata.stge_rx_cons = 0;
	STGE_RXCHAIN_RESET(sc);

	rd = &sc->sc_rdata;
	bzero(rd->stge_rx_ring, STGE_RX_RING_SZ);
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		if (stge_newbuf(sc, i, 1) != 0)
			return (ENOBUFS);
		if (i == (STGE_RX_RING_CNT - 1))
			addr = STGE_RX_RING_ADDR(sc, 0);
		else
			addr = STGE_RX_RING_ADDR(sc, i + 1);
		rd->stge_rx_ring[i].rfd_next = htole64(addr);
		rd->stge_rx_ring[i].rfd_status = 0;
	}
	return (0);
}

/*
 * stge_newbuf:
 *
 *	Add a receive buffer to the indicated descriptor.
 */
static int
stge_newbuf(struct stge_softc *sc, int idx, int waitok)
{
	struct stge_rxdesc *rxd;
	struct stge_rfd *rfd;
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	int error, nseg;

	m = m_getcl(waitok ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return ENOBUFS;
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * The hardware requires 4bytes aligned DMA address when JUMBO
	 * frame is used.
	 */
	if (sc->sc_if_framesize <= (MCLBYTES - ETHER_ALIGN))
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(sc->sc_cdata.stge_rx_tag,
			sc->sc_cdata.stge_rx_sparemap, m,
			&seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return error;
	}

	rxd = &sc->sc_cdata.stge_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sc_cdata.stge_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_cdata.stge_rx_tag, rxd->rx_dmamap);
	}

	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->sc_cdata.stge_rx_sparemap;
	sc->sc_cdata.stge_rx_sparemap = map;

	rxd->rx_m = m;

	rfd = &sc->sc_rdata.stge_rx_ring[idx];
	rfd->rfd_frag.frag_word0 =
	    htole64(FRAG_ADDR(seg.ds_addr) | FRAG_LEN(seg.ds_len));
	rfd->rfd_status = 0;

	return 0;
}

/*
 * stge_set_filter:
 *
 *	Set up the receive filter.
 */
static void
stge_set_filter(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t mode;

	mode = CSR_READ_2(sc, STGE_ReceiveMode);
	mode |= RM_ReceiveUnicast;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		mode |= RM_ReceiveBroadcast;
	else
		mode &= ~RM_ReceiveBroadcast;
	if ((ifp->if_flags & IFF_PROMISC) != 0)
		mode |= RM_ReceiveAllFrames;
	else
		mode &= ~RM_ReceiveAllFrames;

	CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
}

static void
stge_set_multi(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint16_t mode;
	int count;

	mode = CSR_READ_2(sc, STGE_ReceiveMode);
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			mode |= RM_ReceiveAllFrames;
		else if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			mode |= RM_ReceiveMulticast;
		CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
		return;
	}

	/* clear existing filters. */
	CSR_WRITE_4(sc, STGE_HashTable0, 0);
	CSR_WRITE_4(sc, STGE_HashTable1, 0);

	/*
	 * Set up the multicast address filter by passing all multicast
	 * addresses through a CRC generator, and then using the low-order
	 * 6 bits as an index into the 64 bit multicast hash table.  The
	 * high order bits select the register, while the rest of the bits
	 * select the bit within the register.
	 */

	bzero(mchash, sizeof(mchash));

	count = 0;
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);

		/* Just want the 6 least significant bits. */
		crc &= 0x3f;

		/* Set the corresponding bit in the hash table. */
		mchash[crc >> 5] |= 1 << (crc & 0x1f);
		count++;
	}

	mode &= ~(RM_ReceiveMulticast | RM_ReceiveAllFrames);
	if (count > 0)
		mode |= RM_ReceiveMulticastHash;
	else
		mode &= ~RM_ReceiveMulticastHash;

	CSR_WRITE_4(sc, STGE_HashTable0, mchash[0]);
	CSR_WRITE_4(sc, STGE_HashTable1, mchash[1]);
	CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
}

static int
sysctl_hw_stge_rxint_nframe(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    STGE_RXINT_NFRAME_MIN, STGE_RXINT_NFRAME_MAX));
}

static int
sysctl_hw_stge_rxint_dmawait(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    STGE_RXINT_DMAWAIT_MIN, STGE_RXINT_DMAWAIT_MAX));
}
