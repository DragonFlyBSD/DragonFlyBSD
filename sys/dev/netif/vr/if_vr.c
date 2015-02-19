/*
 * Copyright (c) 1997, 1998
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_vr.c,v 1.26.2.13 2003/02/06 04:46:20 silby Exp $
 */

/*
 * VIA Rhine fast ethernet PCI NIC driver
 *
 * Supports various network adapters based on the VIA Rhine
 * and Rhine II PCI controllers, including the D-Link DFE530TX.
 * Datasheets are available at http://www.via.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The VIA Rhine controllers are similar in some respects to the
 * the DEC tulip chips, except less complicated. The controller
 * uses an MII bus and an external physical layer interface. The
 * receiver has a one entry perfect filter and a 64-bit hash table
 * multicast filter. Transmit and receive descriptors are similar
 * to the tulip.
 *
 * The Rhine has a serious flaw in its transmit DMA mechanism:
 * transmit buffers must be longword aligned. Unfortunately,
 * FreeBSD doesn't guarantee that mbufs will be filled in starting
 * at longword boundaries, so we have to do a buffer copy before
 * transmission.
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/serialize.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/thread2.h>
#include <sys/interrupt.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#define VR_USEIOSPACE

#include <dev/netif/vr/if_vrreg.h>

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#undef VR_USESWSHIFT

/*
 * Various supported device vendors/types and their names.
 */
static struct vr_type vr_devs[] = {
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT3043,
		"VIA VT3043 Rhine I 10/100BaseTX" },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT86C100A,
		"VIA VT86C100A Rhine II 10/100BaseTX" },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT6102,
		"VIA VT6102 Rhine II 10/100BaseTX" },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT6105,
		"VIA VT6105 Rhine III 10/100BaseTX" },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT6105M,
		"VIA VT6105M Rhine III 10/100BaseTX" },
	{ PCI_VENDOR_DELTA, PCI_PRODUCT_DELTA_RHINEII,
		"Delta Electronics Rhine II 10/100BaseTX" },
	{ PCI_VENDOR_ADDTRON, PCI_PRODUCT_ADDTRON_RHINEII,
		"Addtron Technology Rhine II 10/100BaseTX" },
	{ 0, 0, NULL }
};

static int	vr_probe(device_t);
static int	vr_attach(device_t);
static int	vr_detach(device_t);

static int	vr_newbuf(struct vr_softc *, struct vr_chain_onefrag *,
			  struct mbuf *);
static int	vr_encap(struct vr_softc *, int, struct mbuf * );

static void	vr_rxeof(struct vr_softc *);
static void	vr_rxeoc(struct vr_softc *);
static void	vr_txeof(struct vr_softc *);
static void	vr_txeoc(struct vr_softc *);
static void	vr_tick(void *);
static void	vr_intr(void *);
static void	vr_start(struct ifnet *, struct ifaltq_subque *);
static int	vr_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	vr_init(void *);
static void	vr_stop(struct vr_softc *);
static void	vr_watchdog(struct ifnet *);
static void	vr_shutdown(device_t);
static int	vr_ifmedia_upd(struct ifnet *);
static void	vr_ifmedia_sts(struct ifnet *, struct ifmediareq *);

#ifdef VR_USESWSHIFT
static void	vr_mii_sync(struct vr_softc *);
static void	vr_mii_send(struct vr_softc *, uint32_t, int);
#endif
static int	vr_mii_readreg(struct vr_softc *, struct vr_mii_frame *);
static int	vr_mii_writereg(struct vr_softc *, struct vr_mii_frame *);
static int	vr_miibus_readreg(device_t, int, int);
static int	vr_miibus_writereg(device_t, int, int, int);
static void	vr_miibus_statchg(device_t);

static void	vr_setcfg(struct vr_softc *, int);
static void	vr_setmulti(struct vr_softc *);
static void	vr_reset(struct vr_softc *);
static int	vr_list_rx_init(struct vr_softc *);
static int	vr_list_tx_init(struct vr_softc *);
#ifdef IFPOLL_ENABLE
static void	vr_npoll(struct ifnet *, struct ifpoll_info *);
static void	vr_npoll_compat(struct ifnet *, void *, int);
#endif

#ifdef VR_USEIOSPACE
#define VR_RES			SYS_RES_IOPORT
#define VR_RID			VR_PCI_LOIO
#else
#define VR_RES			SYS_RES_MEMORY
#define VR_RID			VR_PCI_LOMEM
#endif

static device_method_t vr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vr_probe),
	DEVMETHOD(device_attach,	vr_attach),
	DEVMETHOD(device_detach, 	vr_detach),
	DEVMETHOD(device_shutdown,	vr_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	vr_miibus_readreg),
	DEVMETHOD(miibus_writereg,	vr_miibus_writereg),
	DEVMETHOD(miibus_statchg,	vr_miibus_statchg),

	DEVMETHOD_END
};

static driver_t vr_driver = {
	"vr",
	vr_methods,
	sizeof(struct vr_softc)
};

static devclass_t vr_devclass;

DECLARE_DUMMY_MODULE(if_vr);
DRIVER_MODULE(if_vr, pci, vr_driver, vr_devclass, NULL, NULL);
DRIVER_MODULE(miibus, vr, miibus_driver, miibus_devclass, NULL, NULL);

#define VR_SETBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) | (x))

#define VR_CLRBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) & ~(x))

#define VR_SETBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) | (x))

#define VR_CLRBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) & ~(x))

#define VR_SETBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define VR_CLRBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) | (x))

#define SIO_CLR(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) & ~(x))

#ifdef VR_USESWSHIFT
/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
vr_mii_sync(struct vr_softc *sc)
{
	int i;

	SIO_SET(VR_MIICMD_DIR|VR_MIICMD_DATAIN);

	for (i = 0; i < 32; i++) {
		SIO_SET(VR_MIICMD_CLK);
		DELAY(1);
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
vr_mii_send(struct vr_softc *sc, uint32_t bits, int cnt)
{
	int i;

	SIO_CLR(VR_MIICMD_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i)
			SIO_SET(VR_MIICMD_DATAIN);
                else
			SIO_CLR(VR_MIICMD_DATAIN);
		DELAY(1);
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
		SIO_SET(VR_MIICMD_CLK);
	}
}
#endif

/*
 * Read an PHY register through the MII.
 */
static int
vr_mii_readreg(struct vr_softc *sc, struct vr_mii_frame *frame)
#ifdef VR_USESWSHIFT	
{
	int i, ack;

	/* Set up frame for RX. */
	frame->mii_stdelim = VR_MII_STARTDELIM;
	frame->mii_opcode = VR_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_1(sc, VR_MIICMD, 0);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_DIRECTPGM);

	/* Turn on data xmit. */
	SIO_SET(VR_MIICMD_DIR);

	vr_mii_sync(sc);

	/* Send command/address info. */
	vr_mii_send(sc, frame->mii_stdelim, 2);
	vr_mii_send(sc, frame->mii_opcode, 2);
	vr_mii_send(sc, frame->mii_phyaddr, 5);
	vr_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit. */
	SIO_CLR((VR_MIICMD_CLK|VR_MIICMD_DATAIN));
	DELAY(1);
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(VR_MIICMD_DIR);

	/* Check for ack */
	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, VR_MIICMD) & VR_MIICMD_DATAOUT;
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(VR_MIICMD_CLK);
			DELAY(1);
			SIO_SET(VR_MIICMD_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, VR_MIICMD) & VR_MIICMD_DATAOUT)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(VR_MIICMD_CLK);
		DELAY(1);
	}

fail:
	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);

	if (ack)
		return(1);
	return(0);
}
#else
{
	int i;

  	/* Set the PHY address. */
	CSR_WRITE_1(sc, VR_PHYADDR, (CSR_READ_1(sc, VR_PHYADDR)& 0xe0)|
	    frame->mii_phyaddr);

	/* Set the register address. */
	CSR_WRITE_1(sc, VR_MIIADDR, frame->mii_regaddr);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_READ_ENB);
	
	for (i = 0; i < 10000; i++) {
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_READ_ENB) == 0)
			break;
		DELAY(1);
	}
	frame->mii_data = CSR_READ_2(sc, VR_MIIDATA);

	return(0);
}
#endif


/*
 * Write to a PHY register through the MII.
 */
static int
vr_mii_writereg(struct vr_softc *sc, struct vr_mii_frame *frame)
#ifdef VR_USESWSHIFT	
{
	CSR_WRITE_1(sc, VR_MIICMD, 0);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_DIRECTPGM);

	/* Set up frame for TX. */
	frame->mii_stdelim = VR_MII_STARTDELIM;
	frame->mii_opcode = VR_MII_WRITEOP;
	frame->mii_turnaround = VR_MII_TURNAROUND;
	
	/* Turn on data output. */
	SIO_SET(VR_MIICMD_DIR);

	vr_mii_sync(sc);

	vr_mii_send(sc, frame->mii_stdelim, 2);
	vr_mii_send(sc, frame->mii_opcode, 2);
	vr_mii_send(sc, frame->mii_phyaddr, 5);
	vr_mii_send(sc, frame->mii_regaddr, 5);
	vr_mii_send(sc, frame->mii_turnaround, 2);
	vr_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);
	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(VR_MIICMD_DIR);

	return(0);
}
#else
{
	int i;

  	/* Set the PHY-adress */
	CSR_WRITE_1(sc, VR_PHYADDR, (CSR_READ_1(sc, VR_PHYADDR)& 0xe0)|
		    frame->mii_phyaddr);

	/* Set the register address and data to write. */
	CSR_WRITE_1(sc, VR_MIIADDR, frame->mii_regaddr);
	CSR_WRITE_2(sc, VR_MIIDATA, frame->mii_data);

	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_WRITE_ENB);

	for (i = 0; i < 10000; i++) {
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_WRITE_ENB) == 0)
			break;
		DELAY(1);
	}
	return(0);
}
#endif

static int
vr_miibus_readreg(device_t dev, int phy, int reg)
{
	struct vr_mii_frame frame;
	struct vr_softc *sc;

	sc = device_get_softc(dev);

	switch (sc->vr_revid) {
	case REV_ID_VT6102_APOLLO:
		if (phy != 1)
			return(0);
		break;
	default:
		break;
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	vr_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

static int
vr_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct vr_mii_frame frame;
	struct vr_softc *sc;

	sc = device_get_softc(dev);

	switch (sc->vr_revid) {
	case REV_ID_VT6102_APOLLO:
		if (phy != 1)
			return 0;
		break;
	default:
		break;
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	vr_mii_writereg(sc, &frame);

	return(0);
}

static void
vr_miibus_statchg(device_t dev)
{
	struct mii_data *mii;
	struct vr_softc *sc;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->vr_miibus);
	vr_setcfg(sc, mii->mii_media_active);
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
vr_setmulti(struct vr_softc *sc)
{
	struct ifnet *ifp;
	uint32_t hashes[2] = { 0, 0 };
	struct ifmultiaddr *ifma;
	uint8_t rxfilt;
	int mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	rxfilt = CSR_READ_1(sc, VR_RXCFG);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= VR_RXCFG_RX_MULTI;
		CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
		CSR_WRITE_4(sc, VR_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, VR_MAR1, 0xFFFFFFFF);
		return;
	}

	/* First, zero out all the existing hash bits. */
	CSR_WRITE_4(sc, VR_MAR0, 0);
	CSR_WRITE_4(sc, VR_MAR1, 0);

	/* Now program new ones. */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		int h;

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		/* use the lower 6 bits */
		h = (ether_crc32_be(
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			ETHER_ADDR_LEN) >> 26) & 0x0000003F;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}

	if (mcnt)
		rxfilt |= VR_RXCFG_RX_MULTI;
	else
		rxfilt &= ~VR_RXCFG_RX_MULTI;

	CSR_WRITE_4(sc, VR_MAR0, hashes[0]);
	CSR_WRITE_4(sc, VR_MAR1, hashes[1]);
	CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
}

/*
 * In order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
static void
vr_setcfg(struct vr_softc *sc, int media)
{
	int restart = 0;

	if (CSR_READ_2(sc, VR_COMMAND) & (VR_CMD_TX_ON|VR_CMD_RX_ON)) {
		restart = 1;
		VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_TX_ON|VR_CMD_RX_ON));
	}

	if ((media & IFM_GMASK) == IFM_FDX)
		VR_SETBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);
	else
		VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);

	if (restart)
		VR_SETBIT16(sc, VR_COMMAND, VR_CMD_TX_ON|VR_CMD_RX_ON);
}

static void
vr_reset(struct vr_softc *sc)
{
	int i;

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RESET);

	for (i = 0; i < VR_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_2(sc, VR_COMMAND) & VR_CMD_RESET))
			break;
	}
	if (i == VR_TIMEOUT) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		if (sc->vr_revid < REV_ID_VT3065_A) {
			if_printf(ifp, "reset never completed!\n");
		} else {
			/* Use newer force reset command */
			if_printf(ifp, "Using force reset command.\n");
			VR_SETBIT(sc, VR_MISC_CR1, VR_MISCCR1_FORSRST);
		}
	}

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
}

/*
 * Probe for a VIA Rhine chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
vr_probe(device_t dev)
{
	struct vr_type *t;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (t = vr_devs; t->vr_name != NULL; ++t) {
		if (vid == t->vr_vid && did == t->vr_did) {
			device_set_desc(dev, t->vr_name);
			return(0);
		}
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
vr_attach(device_t dev)
{
	int i;
	uint8_t eaddr[ETHER_ADDR_LEN];
	struct vr_softc *sc;
	struct ifnet *ifp;
	int error = 0, rid;

	sc = device_get_softc(dev);
	callout_init(&sc->vr_stat_timer);

	/*
	 * Handle power management nonsense.
	 */
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t iobase, membase, irq;

		/* Save important PCI config data. */
		iobase = pci_read_config(dev, VR_PCI_LOIO, 4);
		membase = pci_read_config(dev, VR_PCI_LOMEM, 4);
		irq = pci_read_config(dev, VR_PCI_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
		"-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, VR_PCI_LOIO, iobase, 4);
		pci_write_config(dev, VR_PCI_LOMEM, membase, 4);
		pci_write_config(dev, VR_PCI_INTLINE, irq, 4);
	}

	pci_enable_busmaster(dev);

	sc->vr_revid = pci_get_revid(dev);

	rid = VR_RID;
	sc->vr_res = bus_alloc_resource_any(dev, VR_RES, &rid, RF_ACTIVE);

	if (sc->vr_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		return ENXIO;
	}

	sc->vr_btag = rman_get_bustag(sc->vr_res);
	sc->vr_bhandle = rman_get_bushandle(sc->vr_res);

	/* Allocate interrupt */
	rid = 0;
	sc->vr_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					    RF_SHAREABLE | RF_ACTIVE);

	if (sc->vr_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Windows may put the chip in suspend mode when it
	 * shuts down. Be sure to kick it in the head to wake it
	 * up again.
	 */
	VR_CLRBIT(sc, VR_STICKHW, (VR_STICKHW_DS0|VR_STICKHW_DS1));

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* Reset the adapter. */
	vr_reset(sc);

        /*
	 * Turn on bit2 (MIION) in PCI configuration register 0x53 during
	 * initialization and disable AUTOPOLL.
	 */
        pci_write_config(dev, VR_PCI_MODE,
	    pci_read_config(dev, VR_PCI_MODE, 4) | (VR_MODE3_MIION << 24), 4);
	VR_CLRBIT(sc, VR_MIICMD, VR_MIICMD_AUTOPOLL);

	/*
	 * Get station address. The way the Rhine chips work,
	 * you're not allowed to directly access the EEPROM once
	 * they've been programmed a special way. Consequently,
	 * we need to read the node address from the PAR0 and PAR1
	 * registers.
	 */
	VR_SETBIT(sc, VR_EECSR, VR_EECSR_LOAD);
	DELAY(200);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		eaddr[i] = CSR_READ_1(sc, VR_PAR0 + i);

	sc->vr_ldata = contigmalloc(sizeof(struct vr_list_data), M_DEVBUF,
	    M_WAITOK | M_ZERO, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->vr_ldata == NULL) {
		device_printf(dev, "no memory for list buffers!\n");
		error = ENXIO;
		goto fail;
	}

	/* Initialize TX buffer */
	sc->vr_cdata.vr_tx_buf = contigmalloc(VR_TX_BUF_SIZE, M_DEVBUF,
	    M_WAITOK, 0, 0xffffffff, PAGE_SIZE, 0);
	if (sc->vr_cdata.vr_tx_buf == NULL) {
		device_printf(dev, "can't allocate tx buffer!\n");
		error = ENXIO;
		goto fail;
	}

	/* Set various TX indexes to invalid value */
	sc->vr_cdata.vr_tx_free_idx = -1;
	sc->vr_cdata.vr_tx_tail_idx = -1;
	sc->vr_cdata.vr_tx_head_idx = -1;


	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vr_ioctl;
	ifp->if_start = vr_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = vr_npoll;
#endif
	ifp->if_watchdog = vr_watchdog;
	ifp->if_init = vr_init;
	ifp->if_baudrate = 10000000;
	ifq_set_maxlen(&ifp->if_snd, VR_TX_LIST_CNT - 1);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * Do MII setup.
	 */
	if (mii_phy_probe(dev, &sc->vr_miibus,
	    vr_ifmedia_upd, vr_ifmedia_sts)) {
		if_printf(ifp, "MII without any phy!\n");
		error = ENXIO;
		goto fail;
	}

	/* Call MI attach routine. */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->vr_irq));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->vr_npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif

	error = bus_setup_intr(dev, sc->vr_irq, INTR_MPSAFE,
			       vr_intr, sc, &sc->vr_intrhand, 
			       ifp->if_serializer);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	return 0;

fail:
	vr_detach(dev);
	return(error);
}

static int
vr_detach(device_t dev)
{
	struct vr_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		vr_stop(sc);
		bus_teardown_intr(dev, sc->vr_irq, sc->vr_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}
	if (sc->vr_miibus != NULL)
		device_delete_child(dev, sc->vr_miibus);
	bus_generic_detach(dev);

	if (sc->vr_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->vr_irq);
	if (sc->vr_res != NULL)
		bus_release_resource(dev, VR_RES, VR_RID, sc->vr_res);
	if (sc->vr_ldata != NULL)
		contigfree(sc->vr_ldata, sizeof(struct vr_list_data), M_DEVBUF);
	if (sc->vr_cdata.vr_tx_buf != NULL)
		contigfree(sc->vr_cdata.vr_tx_buf, VR_TX_BUF_SIZE, M_DEVBUF);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
vr_list_tx_init(struct vr_softc *sc)
{
	struct vr_chain_data *cd;
	struct vr_list_data *ld;
	struct vr_chain *tx_chain;
	int i;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;
	tx_chain = cd->vr_tx_chain;

	for (i = 0; i < VR_TX_LIST_CNT; i++) {
		tx_chain[i].vr_ptr = &ld->vr_tx_list[i];
		if (i == (VR_TX_LIST_CNT - 1))
			tx_chain[i].vr_next_idx = 0;
		else
			tx_chain[i].vr_next_idx = i + 1;
	}

	for (i = 0; i < VR_TX_LIST_CNT; ++i) {
		void *tx_buf;
		int next_idx;

		tx_buf = VR_TX_BUF(sc, i);
		next_idx = tx_chain[i].vr_next_idx;

		tx_chain[i].vr_next_desc_paddr =
			vtophys(tx_chain[next_idx].vr_ptr);
		tx_chain[i].vr_buf_paddr = vtophys(tx_buf);
	}

	cd->vr_tx_free_idx = 0;
	cd->vr_tx_tail_idx = cd->vr_tx_head_idx = -1;

	return 0;
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
vr_list_rx_init(struct vr_softc *sc)
{
	struct vr_chain_data *cd;
	struct vr_list_data *ld;
	int i, nexti;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;

	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		cd->vr_rx_chain[i].vr_ptr = (struct vr_desc *)&ld->vr_rx_list[i];
		if (vr_newbuf(sc, &cd->vr_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (VR_RX_LIST_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		cd->vr_rx_chain[i].vr_nextdesc = &cd->vr_rx_chain[nexti];
		ld->vr_rx_list[i].vr_next = vtophys(&ld->vr_rx_list[nexti]);
	}

	cd->vr_rx_head = &cd->vr_rx_chain[0];

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 * Note: the length fields are only 11 bits wide, which means the
 * largest size we can specify is 2047. This is important because
 * MCLBYTES is 2048, so we have to subtract one otherwise we'll
 * overflow the field and make a mess.
 */
static int
vr_newbuf(struct vr_softc *sc, struct vr_chain_onefrag *c, struct mbuf *m)
{
	struct mbuf *m_new = NULL;

	if (m == NULL) {
		m_new = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m_new == NULL)
			return (ENOBUFS);
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, sizeof(uint64_t));

	c->vr_mbuf = m_new;
	c->vr_ptr->vr_status = VR_RXSTAT;
	c->vr_ptr->vr_data = vtophys(mtod(m_new, caddr_t));
	c->vr_ptr->vr_ctl = VR_RXCTL | VR_RXLEN;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
vr_rxeof(struct vr_softc *sc)
{
        struct mbuf *m;
        struct ifnet *ifp;
	struct vr_chain_onefrag *cur_rx;
	int total_len = 0;
	uint32_t rxstat;

	ifp = &sc->arpcom.ac_if;

	while(!((rxstat = sc->vr_cdata.vr_rx_head->vr_ptr->vr_status) &
							VR_RXSTAT_OWN)) {
		struct mbuf *m0 = NULL;

		cur_rx = sc->vr_cdata.vr_rx_head;
		sc->vr_cdata.vr_rx_head = cur_rx->vr_nextdesc;
		m = cur_rx->vr_mbuf;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & VR_RXSTAT_RXERR) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			if_printf(ifp, "rx error (%02x):", rxstat & 0x000000ff);
			if (rxstat & VR_RXSTAT_CRCERR)
				kprintf(" crc error");
			if (rxstat & VR_RXSTAT_FRAMEALIGNERR)
				kprintf(" frame alignment error\n");
			if (rxstat & VR_RXSTAT_FIFOOFLOW)
				kprintf(" FIFO overflow");
			if (rxstat & VR_RXSTAT_GIANT)
				kprintf(" received giant packet");
			if (rxstat & VR_RXSTAT_RUNT)
				kprintf(" received runt packet");
			if (rxstat & VR_RXSTAT_BUSERR)
				kprintf(" system bus error");
			if (rxstat & VR_RXSTAT_BUFFERR)
				kprintf("rx buffer error");
			kprintf("\n");
			vr_newbuf(sc, cur_rx, m);
			continue;
		}

		/* No errors; receive the packet. */	
		total_len = VR_RXBYTES(cur_rx->vr_ptr->vr_status);

		/*
		 * XXX The VIA Rhine chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
	 	 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
		    total_len + ETHER_ALIGN, 0, ifp, NULL);
		vr_newbuf(sc, cur_rx, m);
		if (m0 == NULL) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}
		m_adj(m0, ETHER_ALIGN);
		m = m0;

		IFNET_STAT_INC(ifp, ipackets, 1);
		ifp->if_input(ifp, m, NULL, -1);
	}
}

static void
vr_rxeoc(struct vr_softc *sc)
{
	struct ifnet *ifp;
	int i;

	ifp = &sc->arpcom.ac_if;

	IFNET_STAT_INC(ifp, ierrors, 1);

	VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);	
        DELAY(10000);

	/* Wait for receiver to stop */
	for (i = 0x400;
	     i && (CSR_READ_2(sc, VR_COMMAND) & VR_CMD_RX_ON);
	     i--)
		;	/* Wait for receiver to stop */

	if (i == 0) {
		if_printf(ifp, "rx shutdown error!\n");
		sc->vr_flags |= VR_F_RESTART;
		return;
	}

	vr_rxeof(sc);

	CSR_WRITE_4(sc, VR_RXADDR, vtophys(sc->vr_cdata.vr_rx_head->vr_ptr));
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_GO);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
vr_txeof(struct vr_softc *sc)
{
	struct vr_chain_data *cd;
	struct vr_chain *tx_chain;
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;
	cd = &sc->vr_cdata;

	/* Reset the timeout timer; if_txeoc will clear it. */
	ifp->if_timer = 5;

	/* Sanity check. */
	if (cd->vr_tx_head_idx == -1)
		return;

	tx_chain = cd->vr_tx_chain;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	while(tx_chain[cd->vr_tx_head_idx].vr_buf != NULL) {
		struct vr_chain *cur_tx;
		uint32_t txstat;
		int i;

		cur_tx = &tx_chain[cd->vr_tx_head_idx];
		txstat = cur_tx->vr_ptr->vr_status;

		if ((txstat & VR_TXSTAT_ABRT) ||
		    (txstat & VR_TXSTAT_UDF)) {
			for (i = 0x400;
			     i && (CSR_READ_2(sc, VR_COMMAND) & VR_CMD_TX_ON);
			     i--)
				;	/* Wait for chip to shutdown */
			if (i == 0) {
				if_printf(ifp, "tx shutdown timeout\n");
				sc->vr_flags |= VR_F_RESTART;
				break;
			}
			VR_TXOWN(cur_tx) = VR_TXSTAT_OWN;
			CSR_WRITE_4(sc, VR_TXADDR, vtophys(cur_tx->vr_ptr));
			break;
		}

		if (txstat & VR_TXSTAT_OWN)
			break;

		if (txstat & VR_TXSTAT_ERRSUM) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (txstat & VR_TXSTAT_DEFER)
				IFNET_STAT_INC(ifp, collisions, 1);
			if (txstat & VR_TXSTAT_LATECOLL)
				IFNET_STAT_INC(ifp, collisions, 1);
		}

		IFNET_STAT_INC(ifp, collisions,
		    (txstat & VR_TXSTAT_COLLCNT) >> 3);

		IFNET_STAT_INC(ifp, opackets, 1);
		cur_tx->vr_buf = NULL;

		if (cd->vr_tx_head_idx == cd->vr_tx_tail_idx) {
			cd->vr_tx_head_idx = -1;
			cd->vr_tx_tail_idx = -1;
			break;
		}

		cd->vr_tx_head_idx = cur_tx->vr_next_idx;
	}
}

/*
 * TX 'end of channel' interrupt handler.
 */
static void
vr_txeoc(struct vr_softc *sc)
{
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;

	if (sc->vr_cdata.vr_tx_head_idx == -1) {
		ifq_clr_oactive(&ifp->if_snd);
		sc->vr_cdata.vr_tx_tail_idx = -1;
		ifp->if_timer = 0;
	}
}

static void
vr_tick(void *xsc)
{
	struct vr_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->vr_flags & VR_F_RESTART) {
		if_printf(&sc->arpcom.ac_if, "restarting\n");
		vr_stop(sc);
		vr_reset(sc);
		vr_init(sc);
		sc->vr_flags &= ~VR_F_RESTART;
	}

	mii = device_get_softc(sc->vr_miibus);
	mii_tick(mii);

	callout_reset(&sc->vr_stat_timer, hz, vr_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
vr_intr(void *arg)
{
	struct vr_softc *sc;
	struct ifnet *ifp;
	uint16_t status;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Supress unwanted interrupts. */
	if (!(ifp->if_flags & IFF_UP)) {
		vr_stop(sc);
		return;
	}

	/* Disable interrupts. */
	if ((ifp->if_flags & IFF_NPOLLING) == 0)
		CSR_WRITE_2(sc, VR_IMR, 0x0000);

	for (;;) {
		status = CSR_READ_2(sc, VR_ISR);
		if (status)
			CSR_WRITE_2(sc, VR_ISR, status);

		if ((status & VR_INTRS) == 0)
			break;

		if (status & VR_ISR_RX_OK)
			vr_rxeof(sc);

		if (status & VR_ISR_RX_DROPPED) {
			if_printf(ifp, "rx packet lost\n");
			IFNET_STAT_INC(ifp, ierrors, 1);
			}

		if ((status & VR_ISR_RX_ERR) || (status & VR_ISR_RX_NOBUF) ||
		    (status & VR_ISR_RX_OFLOW)) {
			if_printf(ifp, "receive error (%04x)", status);
			if (status & VR_ISR_RX_NOBUF)
				kprintf(" no buffers");
			if (status & VR_ISR_RX_OFLOW)
				kprintf(" overflow");
			if (status & VR_ISR_RX_DROPPED)
				kprintf(" packet lost");
			kprintf("\n");
			vr_rxeoc(sc);
		}

		if ((status & VR_ISR_BUSERR) || (status & VR_ISR_TX_UNDERRUN)) {
			vr_reset(sc);
			vr_init(sc);
			break;
		}

		if ((status & VR_ISR_TX_OK) || (status & VR_ISR_TX_ABRT) ||
		    (status & VR_ISR_TX_ABRT2) || (status & VR_ISR_UDFI)) {
			vr_txeof(sc);
			if ((status & VR_ISR_UDFI) ||
			    (status & VR_ISR_TX_ABRT2) ||
			    (status & VR_ISR_TX_ABRT)) {
				IFNET_STAT_INC(ifp, oerrors, 1);
				if (sc->vr_cdata.vr_tx_head_idx != -1) {
					VR_SETBIT16(sc, VR_COMMAND,
						    VR_CMD_TX_ON);
					VR_SETBIT16(sc, VR_COMMAND,
						    VR_CMD_TX_GO);
				}
			} else {
				vr_txeoc(sc);
			}
		}

	}

	/* Re-enable interrupts. */
	if ((ifp->if_flags & IFF_NPOLLING) == 0)
		CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
vr_encap(struct vr_softc *sc, int chain_idx, struct mbuf *m_head)
{
	struct vr_chain *c;
	struct vr_desc *f;
	caddr_t tx_buf;
	int len;

	KASSERT(chain_idx >= 0 && chain_idx < VR_TX_LIST_CNT,
		("%s: chain idx(%d) out of range 0-%d",
		 sc->arpcom.ac_if.if_xname, chain_idx, VR_TX_LIST_CNT));

	/*
	 * The VIA Rhine wants packet buffers to be longword
	 * aligned, but very often our mbufs aren't. Rather than
	 * waste time trying to decide when to copy and when not
	 * to copy, just do it all the time.
	 */
	tx_buf = VR_TX_BUF(sc, chain_idx);
	m_copydata(m_head, 0, m_head->m_pkthdr.len, tx_buf);
	len = m_head->m_pkthdr.len;

	/*
	 * The Rhine chip doesn't auto-pad, so we have to make
	 * sure to pad short frames out to the minimum frame length
	 * ourselves.
	 */
	if (len < VR_MIN_FRAMELEN) {
		bzero(tx_buf + len, VR_MIN_FRAMELEN - len);
		len = VR_MIN_FRAMELEN;
 	}

	c = &sc->vr_cdata.vr_tx_chain[chain_idx];
	c->vr_buf = tx_buf;

	f = c->vr_ptr;
	f->vr_data = c->vr_buf_paddr;
	f->vr_ctl = len;
	f->vr_ctl |= (VR_TXCTL_TLINK | VR_TXCTL_FIRSTFRAG);
	f->vr_ctl |= (VR_TXCTL_LASTFRAG | VR_TXCTL_FINT);
	f->vr_status = 0;
	f->vr_next = c->vr_next_desc_paddr;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
static void
vr_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct vr_softc *sc;
	struct vr_chain_data *cd;
	struct vr_chain *tx_chain;
	int cur_tx_idx, start_tx_idx, prev_tx_idx;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	sc = ifp->if_softc;
	cd = &sc->vr_cdata;
	tx_chain = cd->vr_tx_chain;

	start_tx_idx = cd->vr_tx_free_idx;
	cur_tx_idx = prev_tx_idx = -1;

	/* Check for an available queue slot. If there are none, punt. */
	if (tx_chain[start_tx_idx].vr_buf != NULL) {
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	while (tx_chain[cd->vr_tx_free_idx].vr_buf == NULL) {
		struct mbuf *m_head;
		struct vr_chain *cur_tx;

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		cur_tx_idx = cd->vr_tx_free_idx;
		cur_tx = &tx_chain[cur_tx_idx];

		/* Pack the data into the descriptor. */
		if (vr_encap(sc, cur_tx_idx, m_head)) {
			ifq_set_oactive(&ifp->if_snd);
			cur_tx_idx = prev_tx_idx;
			break;
		}

		/* XXX */
		if (cur_tx_idx != start_tx_idx)
			VR_TXOWN(cur_tx) = VR_TXSTAT_OWN;

		BPF_MTAP(ifp, m_head);
		m_freem(m_head);

		VR_TXOWN(cur_tx) = VR_TXSTAT_OWN;
		VR_SETBIT16(sc, VR_COMMAND, /*VR_CMD_TX_ON|*/VR_CMD_TX_GO);

		/* Iff everything went OK, we bump up free index. */
		prev_tx_idx = cur_tx_idx;
		cd->vr_tx_free_idx = cur_tx->vr_next_idx;
	}

	/* If there are no frames queued, bail. */
	if (cur_tx_idx == -1)
		return;

	sc->vr_cdata.vr_tx_tail_idx = cur_tx_idx;

	if (sc->vr_cdata.vr_tx_head_idx == -1)
		sc->vr_cdata.vr_tx_head_idx = start_tx_idx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
vr_init(void *xsc)
{
	struct vr_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	int i;

	mii = device_get_softc(sc->vr_miibus);

	/* Cancel pending I/O and free all RX/TX buffers. */
	vr_stop(sc);
	vr_reset(sc);

	/* Set our station address. */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VR_PAR0 + i, sc->arpcom.ac_enaddr[i]);

	/* Set DMA size. */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_DMA_LENGTH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_DMA_STORENFWD);

	/*
	 * BCR0 and BCR1 can override the RXCFG and TXCFG registers,
	 * so we must set both.
	 */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_RX_THRESH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_RXTHRESH128BYTES);

	VR_CLRBIT(sc, VR_BCR1, VR_BCR1_TX_THRESH);
	VR_SETBIT(sc, VR_BCR1, VR_BCR1_TXTHRESHSTORENFWD);

	VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_THRESH);
	VR_SETBIT(sc, VR_RXCFG, VR_RXTHRESH_128BYTES);

	VR_CLRBIT(sc, VR_TXCFG, VR_TXCFG_TX_THRESH);
	VR_SETBIT(sc, VR_TXCFG, VR_TXTHRESH_STORENFWD);

	/* Init circular RX list. */
	if (vr_list_rx_init(sc) == ENOBUFS) {
		vr_stop(sc);
		if_printf(ifp, "initialization failed: no memory for rx buffers\n");
		return;
	}

	/* Init tx descriptors. */
	vr_list_tx_init(sc);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		VR_SETBIT(sc, VR_RXCFG, VR_RXCFG_RX_PROMISC);
	else
		VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_PROMISC);

	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		VR_SETBIT(sc, VR_RXCFG, VR_RXCFG_RX_BROAD);
	else
		VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_BROAD);

	/*
	 * Program the multicast filter, if necessary.
	 */
	vr_setmulti(sc);

	/*
	 * Load the address of the RX list.
	 */
	CSR_WRITE_4(sc, VR_RXADDR, vtophys(sc->vr_cdata.vr_rx_head->vr_ptr));

	/* Enable receiver and transmitter. */
	CSR_WRITE_2(sc, VR_COMMAND, VR_CMD_TX_NOPOLL|VR_CMD_START|
				    VR_CMD_TX_ON|VR_CMD_RX_ON|
				    VR_CMD_RX_GO);

	CSR_WRITE_4(sc, VR_TXADDR, vtophys(&sc->vr_ldata->vr_tx_list[0]));

	/*
	 * Enable interrupts, unless we are polling.
	 */
	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
#ifdef IFPOLL_ENABLE
	if ((ifp->if_flags & IFF_NPOLLING) == 0)
#endif
		CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

	mii_mediachg(mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->vr_stat_timer, hz, vr_tick, sc);
}

/*
 * Set media options.
 */
static int
vr_ifmedia_upd(struct ifnet *ifp)
{
	struct vr_softc *sc;

	sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		vr_init(sc);

	return(0);
}

/*
 * Report current media status.
 */
static void
vr_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vr_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->vr_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
vr_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct vr_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0;

	switch(command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			vr_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vr_stop(sc);
		}
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		vr_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->vr_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

#ifdef IFPOLL_ENABLE

static void
vr_npoll_compat(struct ifnet *ifp, void *arg __unused, int count __unused)
{
	struct vr_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);
	vr_intr(sc);
}

static void
vr_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct vr_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->vr_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = vr_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			/* disable interrupts */
			CSR_WRITE_2(sc, VR_IMR, 0x0000);
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING) {
			/* enable interrupts */
			CSR_WRITE_2(sc, VR_IMR, VR_INTRS);
		}
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->vr_irq));
	}
}

#endif	/* IFPOLL_ENABLE */

static void
vr_watchdog(struct ifnet *ifp)
{
	struct vr_softc *sc;

	sc = ifp->if_softc;

	IFNET_STAT_INC(ifp, oerrors, 1);
	if_printf(ifp, "watchdog timeout\n");

	vr_stop(sc);
	vr_reset(sc);
	vr_init(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
vr_stop(struct vr_softc *sc)
{
	int i;
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	callout_stop(&sc->vr_stat_timer);

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_STOP);
	VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_RX_ON|VR_CMD_TX_ON));
	CSR_WRITE_2(sc, VR_IMR, 0x0000);
	CSR_WRITE_4(sc, VR_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, VR_RXADDR, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		if (sc->vr_cdata.vr_rx_chain[i].vr_mbuf != NULL) {
			m_freem(sc->vr_cdata.vr_rx_chain[i].vr_mbuf);
			sc->vr_cdata.vr_rx_chain[i].vr_mbuf = NULL;
		}
	}
	bzero(&sc->vr_ldata->vr_rx_list, sizeof(sc->vr_ldata->vr_rx_list));

	/*
	 * Reset the TX list buffer pointers.
	 */
	for (i = 0; i < VR_TX_LIST_CNT; i++)
		sc->vr_cdata.vr_tx_chain[i].vr_buf = NULL;

	bzero(&sc->vr_ldata->vr_tx_list, sizeof(sc->vr_ldata->vr_tx_list));

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
vr_shutdown(device_t dev)
{
	struct vr_softc *sc;

	sc = device_get_softc(dev);

	vr_stop(sc);
}
