/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $FreeBSD-4.7: /usr/src/sys/pci/silan.c,v 1.0 2003/01/10 gaoyonghong $
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <machine/clock.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/ifq_var.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <vm/pmap.h>
#include <vm/vm.h>

#include "if_slnreg.h"
#include "if_slnvar.h"

/* Default to using PIO access for netcard driver */
#define SL_USEIOSPACE

#ifdef SLN_DEBUG
#define PDEBUG(fmt, args...)	kprintf("%s: " fmt "\n" , __func__ , ## args)
#else
#define PDEBUG(fmt, args...)
#endif

static const struct sln_dev {
	uint16_t vid;
	uint16_t did;
	const char *desc;
} sln_devs[] = {
	{PCI_VENDOR_SILAN, PCI_PRODUCT_SILAN_SC92031,
	 "Silan SC92031 Fast Ethernet" },
	{PCI_VENDOR_SILAN, PCI_PRODUCT_SILAN_8139D,
	 "Silan Rsltek 8139D Fast Ethernet" },
	{0, 0, NULL}
};

static int	sln_probe(device_t);
static int	sln_attach(device_t);
static int	sln_detach(device_t);
static int	sln_shutdown(device_t);
static int	sln_suspend(device_t);
static int	sln_resume(device_t);

static void	sln_reset(struct sln_softc *);
static void	sln_init(void *);

static void	sln_tx(struct ifnet *, struct ifaltq_subque *);
static void	sln_rx(struct sln_softc *);
static void	sln_tx_intr(struct sln_softc *);
static void	sln_media_intr(struct sln_softc *);
static void	sln_interrupt(void *);
static int	sln_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	sln_stop(struct sln_softc *);
static void	sln_watchdog(struct ifnet *);

static int	sln_media_upd(struct ifnet *);

static void	sln_media_stat(struct ifnet *, struct ifmediareq *);
static void	sln_mii_cmd(struct sln_softc *, uint32_t, u_long *);
static void	sln_media_cfg(struct sln_softc *);
static void	sln_mac_cfg(struct sln_softc *);
static uint32_t	sln_ether_crc32(caddr_t);
static void	sln_set_multi(struct sln_softc *);
static void	sln_init_tx(struct sln_softc *);
static void	sln_tick(void *);

#ifdef SL_USEIOSPACE
#define SL_RID	SL_PCI_IOAD
#define SL_RES	SYS_RES_IOPORT
#else
#define SL_RID	SL_PCI_MEMAD
#define SL_RES	SYS_RES_MEMORY
#endif

static device_method_t sln_methods[] = {
	DEVMETHOD(device_probe,		sln_probe),
	DEVMETHOD(device_attach,	sln_attach),
	DEVMETHOD(device_detach,	sln_detach),
	DEVMETHOD(device_shutdown,	sln_shutdown),
	DEVMETHOD(device_suspend,	sln_suspend),
	DEVMETHOD(device_resume,	sln_resume),

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	DEVMETHOD_END
};

static driver_t sln_driver = {
	"sln",
	sln_methods,
	sizeof(struct sln_softc)
};

static devclass_t sln_devclass;

DRIVER_MODULE(sln, pci, sln_driver, sln_devclass, NULL, NULL);

static int
sln_probe(struct device *dev)
{
	const struct sln_dev *d;
	uint16_t did, vid;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = sln_devs; d->desc != NULL; d++) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			return 0;
		}
	}
	return ENXIO;
}

/* the chip reset */
static void
sln_reset(struct sln_softc *sc)
{
	SLN_WRITE_4(sc, SL_CFG0, SL_SOFT_RESET);
	DELAY(200000);
	SLN_WRITE_4(sc, SL_CFG0, 0x0);
	DELAY(10000);
}

/* Attach the interface. Allocate softc structures */
static int
sln_attach(device_t dev)
{
	struct sln_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	unsigned char eaddr[ETHER_ADDR_LEN];
	int rid;
	int error = 0;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* TODO: power state change */

	pci_enable_busmaster(dev);

	rid = SL_RID;
	sc->sln_res = bus_alloc_resource_any(dev, SL_RES, &rid, RF_ACTIVE);
	if (sc->sln_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}
	sc->sln_bustag = rman_get_bustag(sc->sln_res);
	sc->sln_bushandle = rman_get_bushandle(sc->sln_res);

	/* alloc pci irq */
	rid = 0;
	sc->sln_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sln_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		bus_release_resource(dev, SL_RES, SL_RID, sc->sln_res);
		error = ENXIO;
		goto fail;
	}

	/* Get MAC address */
	((uint32_t *)(&eaddr))[0] = be32toh(SLN_READ_4(sc, SL_MAC_ADDR0));
	((uint16_t *)(&eaddr))[2] = be16toh(SLN_READ_4(sc, SL_MAC_ADDR1));

	/* alloc rx buffer space */
	sc->sln_bufdata.sln_rx_buf = contigmalloc(SL_RX_BUFLEN,
	    M_DEVBUF, M_WAITOK, 0, 0xffffffff, PAGE_SIZE, 0);
	if (sc->sln_bufdata.sln_rx_buf == NULL) {
		device_printf(dev, "no memory for rx buffers!\n");
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sln_irq);
		bus_release_resource(dev, SL_RES, SL_RID, sc->sln_res);
		error = ENXIO;
		goto fail;
	}
	callout_init(&sc->sln_state);

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = sln_init;
	ifp->if_start = sln_tx;
	ifp->if_ioctl = sln_ioctl;
	ifp->if_watchdog = sln_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* initial media */
	ifmedia_init(&sc->ifmedia, 0, sln_media_upd, sln_media_stat);

	/* supported media types */
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_HDX, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_HDX, 0, NULL);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);

	/* Choose a default media. */
	ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_AUTO);

	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sln_irq));

	error = bus_setup_intr(dev, sc->sln_irq, INTR_MPSAFE, sln_interrupt, sc,
			       &sc->sln_intrhand, ifp->if_serializer);
	if (error) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sln_irq);
		bus_release_resource(dev, SL_RES, SL_RID, sc->sln_res);
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

	return 0;
fail:
	return error;
}

/* Stop the adapter and free any mbufs allocated to the RX and TX buffers */
static void
sln_stop(struct sln_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_timer = 0;
	callout_stop(&sc->sln_state);

	/* disable Tx/Rx */
	sc->txcfg &= ~SL_TXCFG_EN;
	sc->rxcfg &= ~SL_RXCFG_EN;
	SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
	SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);

	/* Clear interrupt */
	SLN_WRITE_4(sc, SL_INT_MASK, 0);
	SLN_READ_4(sc, SL_INT_STATUS);

	/* Free the TX list buffers */
	for (i = 0; i < SL_TXD_CNT; i++) {
		if (sc->sln_bufdata.sln_tx_buf[i] != NULL) {
			m_freem(sc->sln_bufdata.sln_tx_buf[i]);
			sc->sln_bufdata.sln_tx_buf[i] = NULL;
			SLN_WRITE_4(sc, SL_TSAD0 + i * 4, 0);
		}
	}

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

static int
sln_detach(device_t dev)
{
	struct sln_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	sln_stop(sc);
	bus_teardown_intr(dev, sc->sln_irq, sc->sln_intrhand);
	lwkt_serialize_exit(ifp->if_serializer);

	ether_ifdetach(ifp);

	bus_generic_detach(dev);

	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sln_irq);
	bus_release_resource(dev, SL_RES, SL_RID, sc->sln_res);
	
	contigfree(sc->sln_bufdata.sln_rx_buf, SL_RX_BUFLEN, M_DEVBUF);

	return 0;
}

static int
sln_media_upd(struct ifnet *ifp)
{
	struct sln_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->ifmedia;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return EINVAL;

	if (ifp->if_flags & IFF_UP)
		sln_init(sc);

	return 0;
}

static void
sln_media_stat(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sln_softc *sc = ifp->if_softc;
	u_long phys[2];
	uint32_t temp;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	phys[0] = SL_MII_STAT;
	sln_mii_cmd(sc, SL_MII0_READ, phys);

	if (phys[1] & SL_MIISTAT_LINK)
		ifmr->ifm_status |= IFM_ACTIVE;

	temp = SLN_READ_4(sc, SL_PHY_CTRL);

	if ((temp & (SL_PHYCTL_DUX | SL_PHYCTL_SPD100 | SL_PHYCTL_SPD10)) == 0x60800000)
		ifmr->ifm_active |= IFM_AUTO;
	else if ((temp & (SL_PHYCTL_DUX | SL_PHYCTL_SPD100)) == 0x40800000)
		ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
	else if ((temp & SL_PHYCTL_SPD100) == 0x40000000)
		ifmr->ifm_active |= IFM_100_TX | IFM_HDX;
	else if ((temp & (SL_PHYCTL_DUX | SL_PHYCTL_SPD10)) == 0x20800000)
		ifmr->ifm_active |= IFM_10_T | IFM_FDX;
	else if ((temp & SL_PHYCTL_SPD10) == 0x20000000)
		ifmr->ifm_active |= IFM_10_T | IFM_HDX;

	sln_mii_cmd(sc, SL_MII0_SCAN, phys);
}

/* command selected in MII command register  */
static void
sln_mii_cmd(struct sln_softc *sc, uint32_t cmd, u_long *phys)
{
	uint32_t mii_status;

	SLN_WRITE_4(sc, SL_MII_CMD0, SL_MII0_DIVEDER);

	do {
		mii_status = 0;
		DELAY(10);
		mii_status = SLN_READ_4(sc, SL_MII_STATUS);
	} while (mii_status & SL_MIISTAT_BUSY);

	switch (cmd) {
	case SL_MII0_SCAN:
		SLN_WRITE_4(sc, SL_MII_CMD1, 0x1 << 6);
		SLN_WRITE_4(sc, SL_MII_CMD0, SL_MII0_DIVEDER | SL_MII0_SCAN);
		break;

	case SL_MII0_READ:
		SLN_WRITE_4(sc, SL_MII_CMD1, phys[0] << 6);
		SLN_WRITE_4(sc, SL_MII_CMD0, SL_MII0_DIVEDER | SL_MII0_READ);
		break;

	default:		/* WRITE */
		SLN_WRITE_4(sc, SL_MII_CMD1, phys[0] << 6 | phys[1] << 11);
		SLN_WRITE_4(sc, SL_MII_CMD0, SL_MII0_DIVEDER | SL_MII0_WRITE);
		break;
	}

	do {
		DELAY(10);
		mii_status = SLN_READ_4(sc, SL_MII_STATUS);
	} while (mii_status & SL_MIISTAT_BUSY);

	if (SL_MII0_READ == cmd)
		phys[1] = (mii_status >> 13) & 0xffff;
}

/* Set media speed and duplex mode */
static void
sln_media_cfg(struct sln_softc *sc)
{
	u_long phys[2];
	uint32_t mediatype;
	uint32_t temp;

	mediatype = (&sc->ifmedia)->ifm_cur->ifm_media;

	temp = SLN_READ_4(sc, SL_PHY_CTRL);
	temp &= ~(SL_PHYCTL_DUX | SL_PHYCTL_SPD100 | SL_PHYCTL_SPD10);
	temp |= (SL_PHYCTL_ANE | SL_PHYCTL_RESET);

	/************************************************/
	/* currently set media word by selected media   */
	/*                                              */
	/* IFM_ETHER = 0x00000020                       */
	/* IFM_AUTO=0, IFM_10_T=3,  IFM_100_TX=6        */
	/* IFM_FDX=0x00100000    IFM_HDX=0x00200000     */
	/************************************************/
	switch (mediatype) {
	case 0x00000020:
		PDEBUG(" autoselet supported\n");
		temp |= (SL_PHYCTL_DUX | SL_PHYCTL_SPD100 | SL_PHYCTL_SPD10);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_AUTO;
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_AUTO);
		break;
	case 0x23:
	case 0x00200023:
		PDEBUG(" 10Mbps half_duplex supported\n");
		temp |= SL_PHYCTL_SPD10;
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_10_T | IFM_HDX;
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_HDX);
		break;

	case 0x00100023:
		PDEBUG("10Mbps full_duplex supported\n");
		temp |= (SL_PHYCTL_SPD10 | SL_PHYCTL_DUX);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_10_T | IFM_FDX;
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX);
		break;

	case 0x26:
	case 0x00200026:
		PDEBUG("100Mbps half_duplex supported\n");
		temp |= SL_PHYCTL_SPD100;
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_100_TX | IFM_HDX;
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_HDX);
		break;

	case 0x00100026:
		PDEBUG("100Mbps full_duplex supported\n");
		temp |= (SL_PHYCTL_SPD100 | SL_PHYCTL_DUX);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_100_TX | IFM_FDX;
		ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX);
		break;

	default:
		break;
	}

	SLN_WRITE_4(sc, SL_PHY_CTRL, temp);

	DELAY(10000);
	temp &= ~SL_PHYCTL_RESET;
	SLN_WRITE_4(sc, SL_PHY_CTRL, temp);

	DELAY(1000);
	phys[0] = SL_MII_JAB;
	phys[1] = SL_PHY_16_JAB_ENB | SL_PHY_16_PORT_ENB;
	sln_mii_cmd(sc, SL_MII0_WRITE, phys);

	sc->connect = 0;
	sln_mii_cmd(sc, SL_MII0_SCAN, phys);
}

static void
sln_mac_cfg(struct sln_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	u_long flowcfg = 0;

	/* Set the initial TX/RX/Flow Control configuration */
	sc->rxcfg = SL_RXCFG_LOW_THRESHOLD | SL_RXCFG_HIGH_THRESHOLD;
	sc->txcfg = TX_CFG_DEFAULT;

	if (sc->txenablepad)
		sc->txcfg |= 0x20000000;

	if (sc->media_speed == IFM_10_T)
		sc->txcfg |= SL_TXCFG_DATARATE;

	if (sc->media_duplex == IFM_FDX) {
		sc->rxcfg |= SL_RXCFG_FULLDX;
		sc->txcfg |= SL_TXCFG_FULLDX;
		flowcfg = SL_FLOWCTL_FULLDX | SL_FLOWCTL_EN;
	} else {
		sc->rxcfg &= ~SL_RXCFG_FULLDX;
		sc->txcfg &= ~SL_TXCFG_FULLDX;
	}

	/* if promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		sc->rxcfg |= (SL_RXCFG_EN | SL_RXCFG_RCV_SMALL | SL_RXCFG_RCV_HUGE | SL_RXCFG_RCV_ERR | SL_RXCFG_RCV_BROAD | SL_RXCFG_RCV_MULTI | SL_RXCFG_RCV_ALL);
	else
		sc->rxcfg &= ~(SL_RXCFG_EN | SL_RXCFG_RCV_SMALL | SL_RXCFG_RCV_HUGE | SL_RXCFG_RCV_ERR | SL_RXCFG_RCV_BROAD | SL_RXCFG_RCV_MULTI | SL_RXCFG_RCV_ALL);

	/* Set capture broadcast bit to capture broadcast frames */
	if (ifp->if_flags & IFF_BROADCAST)
		sc->rxcfg |= SL_RXCFG_EN | SL_RXCFG_RCV_BROAD;
	else
		sc->rxcfg &= ~(SL_RXCFG_EN | SL_RXCFG_RCV_BROAD);

	/* Program the multicast filter, if necessary */
	sln_set_multi(sc);

	SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);
	SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
	SLN_WRITE_4(sc, SL_FLOW_CTRL, flowcfg);
}

static u_char shade_map[] = { 0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
			      0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf };

/* Calculate CRC32 of a multicast group address */
static uint32_t
sln_ether_crc32(caddr_t addr)
{
	uint32_t crc, crcr;
	int i, j;
	unsigned char data = 0;
	/* Compute CRC for the address value. */

	crc = 0xFFFFFFFF;	/* initial value */

	for (i = ETHER_ADDR_LEN; i > 0; i--) {
		data = *addr++;

		for (j = 0; j < 8; j++) {
			if (((data & 0x1) ^ (crc & 0x1)) != 0) {
				crc >>= 1;
				crc ^= 0xEDB88320;
			} else {
				crc >>= 1;
			}
			data >>= 1;
		}
	}

	crcr = shade_map[crc >> 28];
	crcr |= (shade_map[(crc >> 24) & 0xf] << 4);
	crcr |= (shade_map[(crc >> 20) & 0xf] << 8);
	crcr |= (shade_map[(crc >> 16) & 0xf] << 12);
	crcr |= (shade_map[(crc >> 12) & 0xf] << 16);
	crcr |= (shade_map[(crc >> 8) & 0xf] << 20);
	crcr |= (shade_map[(crc >> 4) & 0xf] << 24);
	crcr |= (shade_map[crc & 0xf] << 28);

	return crcr;
}

/* Program the 64-bit multicast hash filter */
static void
sln_set_multi(struct sln_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t crc = 0;
	uint32_t mc_g[2] = {0, 0};
	struct ifmultiaddr *ifma;
	int j;

	if (ifp->if_flags & IFF_PROMISC) {
		kprintf("Promisc mode is enabled\n");
		sc->rxcfg |= SL_RXCFG_EN | SL_RXCFG_RCV_MULTI;
		mc_g[0] = mc_g[1] = 0xFFFFFFFF;
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		kprintf("Allmulti mode is enabled\n");
		sc->rxcfg |= SL_RXCFG_EN | SL_RXCFG_RCV_MULTI;
		mc_g[0] = mc_g[1] = 0xFFFFFFFF;
	} else if (ifp->if_flags & IFF_MULTICAST) {
		kprintf("Multicast mode is enabled\n");
		sc->rxcfg |= SL_RXCFG_EN | SL_RXCFG_RCV_MULTI;

		/* first, zero all the existing hash bits */
		mc_g[0] = mc_g[1] = 0;

		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			j = 0;

			if ((ifma->ifma_addr->sa_family) != AF_LINK)
				continue;

			crc = ~sln_ether_crc32(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			crc >>= 24;

			if (crc & 0x1)
				j |= 0x2;
			if (crc & 0x2)
				j |= 0x1;
			if (crc & 0x10)
				j |= 0x20;
			if (crc & 0x20)
				j |= 0x10;
			if (crc & 0x40)
				j |= 0x8;
			if (crc & 0x80)
				j |= 0x4;

			if (j > 31)
				mc_g[0] |= (0x1 << (j - 32));
			else
				mc_g[1] |= (0x1 << j);
		}
	} else {
		sc->rxcfg &= ~(SL_RXCFG_EN | SL_RXCFG_RCV_MULTI);
	}

	SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);
	SLN_WRITE_4(sc, SL_MULTI_GROUP0, mc_g[0]);
	SLN_WRITE_4(sc, SL_MULTI_GROUP1, mc_g[1]);
}

/* Initialize the TX/Rx descriptors */
static void
sln_init_tx(struct sln_softc *sc)
{
	int i;

	sc->sln_bufdata.cur_tx = 0;
	sc->sln_bufdata.dirty_tx = 0;

	for (i = 0; i < SL_TXD_CNT; i++) {
		sc->sln_bufdata.sln_tx_buf[i] = NULL;
		SLN_WRITE_4(sc, SL_TSAD0 + (i * 4), 0);
	}
}

/* Software & Hardware Initialize */
static void
sln_init(void *x)
{
	struct sln_softc *sc = x;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	PDEBUG("sln_init\n");

	ASSERT_SERIALIZED(ifp->if_serializer);

	sln_stop(sc);

	/* soft reset the chip */
	sln_reset(sc);

	/* disable interrupt */
	SLN_WRITE_4(sc, SL_INT_MASK, 0);

	/* SLN_WRITE_4(sc, SL_MII_CMD0, SL_MII0_DIVEDER); */

	/* clear multicast address */
	SLN_WRITE_4(sc, SL_MULTI_GROUP0, 0);
	SLN_WRITE_4(sc, SL_MULTI_GROUP1, 0);

	/* Init the RX buffer start address register. */
	SLN_WRITE_4(sc, SL_RBSA, vtophys(sc->sln_bufdata.sln_rx_buf));
	sc->sln_bufdata.dirty_rx = vtophys(sc->sln_bufdata.sln_rx_buf);

	/* Init TX descriptors. */
	sln_init_tx(sc);

	/* configure RX buffer size */
	if (sc->tx_early_ctrl && sc->rx_early_ctrl)
		SLN_WRITE_4(sc, SL_CFG1, SL_EARLY_RX | SL_EARLY_TX | SL_RXBUF_64 | SL_RXFIFO_1024BYTES);
	else if (sc->tx_early_ctrl)
		SLN_WRITE_4(sc, SL_CFG1, SL_EARLY_TX | SL_RXBUF_64);
	else if (sc->rx_early_ctrl)
		SLN_WRITE_4(sc, SL_CFG1, SL_EARLY_RX | SL_RXBUF_64 | SL_RXFIFO_1024BYTES);
	else
		SLN_WRITE_4(sc, SL_CFG1, SL_RXBUF_64);

	/* MII media configuration */
	sln_media_cfg(sc);

	if (sc->connect) {
		/* Enable transmit and receive */
		sc->rxcfg |= SL_RXCFG_EN;
		sc->txcfg |= SL_TXCFG_EN;
	} else {
		sc->rxcfg &= ~SL_RXCFG_EN;
		sc->txcfg &= ~SL_TXCFG_EN;
	}

	SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
	SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);

	/* Enable interrupts */
	SLN_WRITE_4(sc, SL_INT_MASK, SL_INRTS);

	sc->suspended = 0;

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->sln_state, hz, sln_tick, sc);
}

/* Transmit Packet */
static void
sln_tx(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct sln_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	struct mbuf *m_new = NULL;
	int entry;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if (!sc->connect) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	while (SL_CUR_TXBUF(sc) == NULL) {	/* SL_CUR_TXBUF(x) = x->sln_bufdata.sln_tx_buf[x->sln_bufdata.cur_tx] */
		entry = sc->sln_bufdata.cur_tx;

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		MGETHDR(m_new, MB_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			if_printf(ifp, "no memory for tx descriptor");
			m_freem(m_head);
			break;
		}
		if ((m_head->m_pkthdr.len > MHLEN) || (60 > MHLEN)) {
			MCLGET(m_new, MB_DONTWAIT);
			if (!(m_new->m_flags & M_EXT)) {
				m_freem(m_new);
				m_freem(m_head);
				if_printf(ifp, "no memory for tx descriptor");
				break;
			}
		}
		m_copydata(m_head, 0, m_head->m_pkthdr.len, mtod(m_new, caddr_t));
		m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;
		m_freem(m_head);
		m_head = m_new;
		SL_CUR_TXBUF(sc) = m_head;

		/*
		 * if there's a BPF listener, bounce a copy of this frame to
		 * him
		 */
		BPF_MTAP(ifp, SL_CUR_TXBUF(sc));

		/* Transmit the frame */
		SLN_WRITE_4(sc, ((entry * 4) + SL_TSAD0),
		    vtophys(mtod(SL_CUR_TXBUF(sc), caddr_t)));

		/* calculate length of the frame */
		if ((SL_CUR_TXBUF(sc)->m_pkthdr.len < 60) && (!sc->txenablepad)) {
			memset(mtod(m_head, char *)+m_head->m_pkthdr.len, 0x20, 60 - m_head->m_pkthdr.len);
			SLN_WRITE_4(sc, (entry * 4) + SL_TSD0, 60);
		} else if (SL_CUR_TXBUF(sc)->m_pkthdr.len < 100)
			SLN_WRITE_4(sc, (entry * 4) + SL_TSD0, SL_CUR_TXBUF(sc)->m_pkthdr.len);
		else if (SL_CUR_TXBUF(sc)->m_pkthdr.len < 300)
			SLN_WRITE_4(sc, (entry * 4) + SL_TSD0, 0x30000 | SL_CUR_TXBUF(sc)->m_pkthdr.len);
		else
			SLN_WRITE_4(sc, (entry * 4) + SL_TSD0, 0x50000 | SL_CUR_TXBUF(sc)->m_pkthdr.len);
		sc->sln_bufdata.cur_tx = (entry + 1) % SL_TXD_CNT;

		PDEBUG("Queue tx packet size %d to tx-descriptor %d.\n", m_head->m_pkthdr.len, entry);
	}

	/* Tx buffer chain full */
	if (SL_CUR_TXBUF(sc) != NULL)
		ifq_set_oactive(&ifp->if_snd);

	/* Set a timeout in case the chip goes out to lunch */
	ifp->if_timer = 5;
}

/* Receive Data handler */
static void
sln_rx(struct sln_softc *sc)
{
	struct mbuf *m;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t rxstat = 0;
	uint32_t rx_offset;
	caddr_t rx_bufpos = NULL;
	uint32_t cur_rx = 0;
	uint32_t dirty_rx;
	long rx_len;
	u_long rx_space;
	u_long rx_size = 0;
	u_long rx_size_align = 0;
	u_long pkt_size = 0;

	cur_rx = SLN_READ_4(sc, SL_RBW_PTR);
	dirty_rx = sc->sln_bufdata.dirty_rx;

	/*
	 * cur_rx is only 17 bits in the RxBufWPtr register. if cur_rx can be
	 * used in physical space, we need to change it to 32 bits physical
	 * address
	 */
	cur_rx |= vtophys(sc->sln_bufdata.sln_rx_buf) & (~(u_long) (SL_RX_BUFLEN - 1));

	if (cur_rx < vtophys(sc->sln_bufdata.sln_rx_buf))
		cur_rx += SL_RX_BUFLEN;

	if (cur_rx >= dirty_rx)
		rx_len = (long)(cur_rx - dirty_rx);
	else
		rx_len = SL_RX_BUFLEN - (long)(dirty_rx - cur_rx);

	if ((rx_len > SL_RX_BUFLEN) || (rx_len < 0)) {
		if_printf(ifp, "rx len is fail\n");
		return;
	}
	if (rx_len == 0)
		return;

	rx_offset = (dirty_rx - vtophys(sc->sln_bufdata.sln_rx_buf)) & (u_long) (SL_RX_BUFLEN - 1);

	while (rx_len > 0) {
#ifdef SLN_DEBUG
		u_long ipkts;
#endif

		rx_bufpos = sc->sln_bufdata.sln_rx_buf + rx_offset;
		rxstat = *(uint32_t *) rx_bufpos;
		rx_size = (rxstat >> 20) & 0x0FFF;
		rx_size_align = (rx_size + 3) & ~3;	/* for 4 bytes aligned */
		pkt_size = rx_size - ETHER_CRC_LEN;	/* Omit the four octet
							 * CRC from the length. */

		PDEBUG("rx len: %ld  rx frame size:%ld  rx state:0x%x\n", rx_len, rx_size, rxstat);

		/* errors receive packets caculatation */
		if (rxstat == 0 || rx_size < 16 || !(rxstat & SL_RXSTAT_RXOK)) {
			IFNET_STAT_INC(ifp, ierrors, 1);

			if (!(rxstat & SL_RXSTAT_RXOK))
				if_printf(ifp, "receiver ok error\n");

			if (!(rxstat & SL_RXSTAT_CRCOK))
				if_printf(ifp, "crc error\n");

			if (rxstat & SL_RXSTAT_ALIGNERR)
				if_printf(ifp, "frame alignment error\n");

			if (rxstat & (SL_RXSTAT_HUGEFRM | SL_RXSTAT_SMALLFRM))
				if_printf(ifp, "received frame length is error\n");

			break;
		}
		rx_len -= (long)(rx_size_align + 4);	/* 4 bytes for receive
							 * frame head */

		if (rx_len < 0) {
			kprintf("rx packets len is too small\n");
			break;
		}
#ifdef SLN_PDEBUG
		caddr_t p = NULL;

		if_printf(ifp, "rx frame content\n");
		p = rx_bufpos;
		for (i = 0; i < 30; i++, p++) {
			if (i % 10 == 0)
				kprintf("\n");
			if_printf(ifp, "%x  ", (u_char)*p);
		}
		if_printf(ifp, "\n");
#endif
		/* No errors; receive the packet. */
		if (rx_bufpos == (sc->sln_bufdata.sln_rx_buf + SL_RX_BUFLEN))
			rx_bufpos = sc->sln_bufdata.sln_rx_buf;

		rx_bufpos = rx_bufpos + 4;	/* 4 bytes for receive frame
						 * header */
		rx_space = (u_long)((sc->sln_bufdata.sln_rx_buf + SL_RX_BUFLEN) - rx_bufpos);

		if (pkt_size > rx_space) {
			m = m_devget(rx_bufpos - 2, pkt_size + 2, 0, ifp, NULL);	/* 2 for etherer head
											 * align */

			if (m == NULL) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				if_printf(ifp,
				    "out of mbufs, tried to copy %ld bytes\n",
				    rx_space);
			} else {
				m_adj(m, 2);
				m_copyback(m, rx_space, pkt_size - rx_space, sc->sln_bufdata.sln_rx_buf);
			}
		} else {
			m = m_devget(rx_bufpos - 2, pkt_size + 2, 0, ifp, NULL);

			if (m == NULL) {
				u_long ierr;

				IFNET_STAT_INC(ifp, ierrors, 1);
				if_printf(ifp,
				    "out of mbufs, tried to copy %ld bytes\n",
				    pkt_size);

				IFNET_STAT_GET(ifp, ierrors, ierr);
				if_printf(ifp, "ierrors = %lu\n", ierr);
			} else {
				m_adj(m, 2);
			}
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
#ifdef SLN_DEBUG
		IFNET_STAT_GET(ifp, ipackets, ipkts);
		PDEBUG("ipackets = %lu\n", ipkts);
#endif

		ifp->if_input(ifp, m, NULL, -1);

		rx_offset = (rx_offset + rx_size + 4) & (u_long) (SL_RX_BUFLEN - 1);	/* 4 bytes for receive
											 * frame head */
	}

	sc->sln_bufdata.dirty_rx = cur_rx;

	SLN_WRITE_4(sc, SL_RBR_PTR, cur_rx);
}

/* Transmit OK/ERR handler */
static void
sln_tx_intr(struct sln_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t txstat;
	int entry;

	do {
		entry = sc->sln_bufdata.dirty_tx;
		txstat = SLN_READ_4(sc, SL_TSD0 + entry * 4);

		if (!(txstat & (SL_TXSD_TOK | SL_TXSD_TUN | SL_TXSD_TABT)))
			break;	/* It still hasn't been sent */

		if (SL_DIRTY_TXBUF(sc) != NULL) {	/* SL_DIRTY_TXBUF(x) =
							 * x->sln_bufdata.sln_tx_
							 * buf[x->sln_bufdata.dir
							 * ty_tx] */
			m_freem(SL_DIRTY_TXBUF(sc));
			SL_DIRTY_TXBUF(sc) = NULL;
		}
		if (txstat & SL_TXSD_TOK) {
#ifdef SLN_DEBUG
			u_long opkts;
#endif

			IFNET_STAT_INC(ifp, opackets, 1);
			IFNET_STAT_INC(ifp, obytes, txstat & SL_TXSD_LENMASK);
#ifdef SLN_DEBUG
			IFNET_STAT_GET(ifp, opackets, opkts);
			PDEBUG("opackets = %lu\n", opkts);
#endif
			IFNET_STAT_INC(ifp, collisions,
			    (txstat & SL_TXSD_NCC) >> 22);
		} else {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if ((txstat & (SL_TXSD_TABT | SL_TXSD_OWC))) {
				sc->txcfg = TX_CFG_DEFAULT;

				if (sc->txenablepad)
					sc->txcfg |= 0x20000000;

				SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
			}
		}
		PDEBUG("tx done descriprtor %x\n", entry);
		sc->sln_bufdata.dirty_tx = (entry + 1) % SL_TXD_CNT;

		ifq_clr_oactive(&ifp->if_snd);
	} while (sc->sln_bufdata.dirty_tx != sc->sln_bufdata.cur_tx);

	if (sc->sln_bufdata.dirty_tx == sc->sln_bufdata.cur_tx)
		ifp->if_timer = 0;
	else
		ifp->if_timer = 5;
}

static void
sln_media_intr(struct sln_softc *sc)
{
	u_long phys[2];
	struct ifnet *ifp = &sc->arpcom.ac_if;

	phys[0] = SL_MII_STAT;
	sln_mii_cmd(sc, SL_MII0_READ, phys);

	PDEBUG("mii_stat:0x%lx\n", phys[1]);

	if (0 == (phys[1] & SL_MIISTAT_LINK)) {
		kprintf("media is unconnect,linked down,or uncompatible\n");
		sc->connect = 0;
		sln_mii_cmd(sc, SL_MII0_SCAN, phys);
		/* disable tx/rx */
		sc->txcfg &= ~SL_TXCFG_EN;
		sc->rxcfg &= ~SL_RXCFG_EN;
		SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
		SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);

		return;
	}
	/* Link is good. Report modes and set duplex mode. */
	PDEBUG("media is connecting---> ");
	sc->connect = 1;

	phys[0] = SL_MII_STAT_OUTPUT;
	sln_mii_cmd(sc, SL_MII0_READ, phys);
	sc->media_duplex = ((phys[1] & 0x0004) == 0) ? IFM_HDX : IFM_FDX;
	sc->media_speed = ((phys[1] & 0x0002) == 0) ? IFM_10_T : IFM_100_TX;

	if_printf(ifp, "media option:%dM %s-duplex\n",
	    sc->media_speed == 0x6 ? 100 : 10,
	    sc->media_duplex == 0x100000 ? "full" : "half");

	sln_mii_cmd(sc, SL_MII0_SCAN, phys);

	sln_mac_cfg(sc);

	/* Enable tx/rx */
	sc->rxcfg |= SL_RXCFG_EN;
	sc->txcfg |= SL_TXCFG_EN;
	SLN_WRITE_4(sc, SL_TX_CONFIG, sc->txcfg);
	SLN_WRITE_4(sc, SL_RX_CONFIG, sc->rxcfg);
}

/* Interrupt Handler */
static void
sln_interrupt(void *arg)
{
	struct sln_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t int_status;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->suspended || (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	/* Disable interrupts. */
	SLN_WRITE_4(sc, SL_INT_MASK, 0);

	int_status = SLN_READ_4(sc, SL_INT_STATUS);

	if ((int_status == 0xffffffff) || (int_status & SL_INRTS) == 0)
		goto back;

	int_status = int_status & SL_INRTS;
	PDEBUG("int_status = 0x%x\n", int_status);

	while (0 != int_status) {
		if (int_status & SL_INT_ROK)
			sln_rx(sc);

		if (int_status & SL_INT_TOK)
			sln_tx_intr(sc);

		if (int_status & SL_INT_RBO) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			PDEBUG("rx buffer is overflow\n");
		}

		if (int_status & (SL_INT_LINKFAIL | SL_INT_LINKOK))
			sln_media_intr(sc);

		int_status = SLN_READ_4(sc, SL_INT_STATUS);
	}

	/* Data in Tx buffer waiting for transimission */
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
back:
	/* Re-enable interrupts. */
	SLN_WRITE_4(sc, SL_INT_MASK, SL_INRTS);
}

static void
sln_tick(void *x)
{
	struct sln_softc *sc = x;

	callout_reset(&sc->sln_state, hz, sln_tick, sc);
}

static int
sln_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct sln_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				sln_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sln_stop(sc);
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		sln_set_multi(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, cmd);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static void
sln_watchdog(struct ifnet *ifp)
{
	struct sln_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if_printf(ifp, "watchdog timeout!\n");
	IFNET_STAT_INC(ifp, oerrors, 1);

	sln_tx_intr(sc);
	sln_rx(sc);
	sln_init(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/* Stop all chip I/O */
static int
sln_shutdown(device_t dev)
{
	struct sln_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	sln_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

/* device suspend routine */
static int
sln_suspend(device_t dev)
{
	struct sln_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	sln_stop(sc);
	sc->suspended = 1;
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

/* device resume routine */
static int
sln_resume(device_t dev)
{
	struct sln_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	if (ifp->if_flags & IFF_UP)
		sln_init(sc);
	sc->suspended = 0;
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}
