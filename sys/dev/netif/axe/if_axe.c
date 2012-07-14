/*
 * Copyright (c) 1997, 1998, 1999, 2000-2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/usb/if_axe.c,v 1.10 2003/12/08 07:54:14 obrien Exp $
 */
/*
 * ASIX Electronics AX88172 USB 2.0 ethernet driver. Used in the
 * LinkSys USB200M and various other adapters.
 *
 * Manuals available from:
 * http://www.asix.com.tw/datasheet/mac/Ax88172.PDF
 * Note: you need the manual for the AX88170 chip (USB 1.x ethernet
 * controller) to find the definitions for the RX control register.
 * http://www.asix.com.tw/datasheet/mac/Ax88170.PDF
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Engineer
 * Wind River Systems
 */

/*
 * The AX88172 provides USB ethernet supports at 10 and 100Mbps.
 * It uses an external PHY (reference designs use a RealTek chip),
 * and has a 64-bit multicast hash filter. There is some information
 * missing from the manual which one needs to know in order to make
 * the chip function:
 *
 * - You must set bit 7 in the RX control register, otherwise the
 *   chip won't receive any packets.
 * - You must initialize all 3 IPG registers, or you won't be able
 *   to send any packets.
 *
 * Note that this device appears to only support loading the station
 * address via autload from the EEPROM (i.e. there's no way to manaully
 * set it).
 *
 * (Adam Weinberger wanted me to name this driver if_gir.c.)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbdivar.h>
#include <bus/usb/usb_ethersubr.h>

#include "../mii_layer//mii.h"
#include "../mii_layer/miivar.h"

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#include "if_axereg.h"

/*
 * Various supported device vendors/products.
 */
static struct usb_devno axe_devs[] = {
	{ USB_DEVICE(0x0411, 0x003d) }, /* Melco LUA-U2-KTX */
	{ USB_DEVICE(0x04f1, 0x3008) }, /* JVC MP-PRX1 */
	{ USB_DEVICE(0x077b, 0x2226) }, /* Linksys USB200M */
	{ USB_DEVICE(0x0846, 0x1040) }, /* BayNETGEAR FA120 */
	{ USB_DEVICE(0x086e, 0x1920) }, /* System Talks SGC-X2UL */
	{ USB_DEVICE(0x0b95, 0x1720) }, /* ASIX Electronics AX88172 */
	{ USB_DEVICE(0x2001, 0x1a00) }, /* D-Link DUBE100 */
	{ USB_DEVICE(0x6189, 0x182d) }, /* Sitecom LN029 */
};

static int axe_match(device_t);
static int axe_attach(device_t);
static int axe_detach(device_t);

static int axe_tx_list_init(struct axe_softc *);
static int axe_rx_list_init(struct axe_softc *);
static int axe_newbuf(struct axe_softc *, struct axe_chain *, struct mbuf *);
static int axe_encap(struct axe_softc *, struct mbuf *, int);
static void axe_rxeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void axe_txeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void axe_tick(void *);
static void axe_rxstart(struct ifnet *);
static void axe_start(struct ifnet *);
static int axe_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void axe_init(void *);
static void axe_stop(struct axe_softc *);
static void axe_watchdog(struct ifnet *);
static void axe_shutdown(device_t);
static int axe_miibus_readreg(device_t, int, int);
static int axe_miibus_writereg(device_t, int, int, int);
static void axe_miibus_statchg(device_t);
static int axe_cmd(struct axe_softc *, int, int, int, void *);
static int axe_ifmedia_upd(struct ifnet *);
static void axe_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void axe_setmulti(struct axe_softc *);

static device_method_t axe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		axe_match),
	DEVMETHOD(device_attach,	axe_attach),
	DEVMETHOD(device_detach,	axe_detach),
	DEVMETHOD(device_shutdown,	axe_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	axe_miibus_readreg),
	DEVMETHOD(miibus_writereg,	axe_miibus_writereg),
	DEVMETHOD(miibus_statchg,	axe_miibus_statchg),

	{ 0, 0 }
};

static driver_t axe_driver = {
	"axe",
	axe_methods,
	sizeof(struct axe_softc)
};

static devclass_t axe_devclass;

DRIVER_MODULE(axe, uhub, axe_driver, axe_devclass, usbd_driver_load, NULL);
DRIVER_MODULE(miibus, axe, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_DEPEND(axe, usb, 1, 1, 1);
MODULE_DEPEND(axe, miibus, 1, 1, 1);

static int
axe_cmd(struct axe_softc *sc, int cmd, int index, int val, void *buf)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (sc->axe_dying)
		return(0);

	if (AXE_CMD_DIR(cmd))
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AXE_CMD_CMD(cmd);
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, AXE_CMD_LEN(cmd));

	err = usbd_do_request(sc->axe_udev, &req, buf);

	if (err)
		return(-1);

	return(0);
}

static int
axe_miibus_readreg(device_t dev, int phy, int reg)
{
	struct axe_softc	*sc = device_get_softc(dev);
	usbd_status		err;
	u_int16_t		val;

	if (sc->axe_dying) {
		return(0);
	}

#ifdef notdef
	/*
	 * The chip tells us the MII address of any supported
	 * PHYs attached to the chip, so only read from those.
	 */

	if (sc->axe_phyaddrs[0] != AXE_NOPHY && phy != sc->axe_phyaddrs[0]) {
		return (0);
	}

	if (sc->axe_phyaddrs[1] != AXE_NOPHY && phy != sc->axe_phyaddrs[1]) {
		return (0);
	}
#endif
	if (sc->axe_phyaddrs[0] != 0xFF && sc->axe_phyaddrs[0] != phy) {
		return (0);
	}

	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	err = axe_cmd(sc, AXE_CMD_MII_READ_REG, reg, phy, &val);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);

	if (err) {
		if_printf(&sc->arpcom.ac_if, "read PHY failed\n");
		return(-1);
	}

	if (val)
		sc->axe_phyaddrs[0] = phy;

	return (val);
}

static int
axe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct axe_softc	*sc = device_get_softc(dev);
	usbd_status		err;

	if (sc->axe_dying) {
		return(0);
	}

	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	err = axe_cmd(sc, AXE_CMD_MII_WRITE_REG, reg, phy, &val);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);

	if (err) {
		if_printf(&sc->arpcom.ac_if, "write PHY failed\n");
		return(-1);
	}

	return (0);
}

static void
axe_miibus_statchg(device_t dev)
{
#ifdef notdef
	struct axe_softc	*sc = device_get_softc(dev);
	struct mii_data		*mii = GET_MII(sc);
#endif
	/* doesn't seem to be necessary */

	return;
}

/*
 * Set media options.
 */
static int
axe_ifmedia_upd(struct ifnet *ifp)
{
        struct axe_softc        *sc = ifp->if_softc;
        struct mii_data         *mii = GET_MII(sc);

        sc->axe_link = 0;
        if (mii->mii_instance) {
                struct mii_softc        *miisc;
                LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
                         mii_phy_reset(miisc);
        }
        mii_mediachg(mii);

        return (0);
}

/*
 * Report current media status.
 */
static void
axe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
        struct axe_softc        *sc = ifp->if_softc;
        struct mii_data         *mii = GET_MII(sc);

        mii_pollstat(mii);
        ifmr->ifm_active = mii->mii_media_active;
        ifmr->ifm_status = mii->mii_media_status;

        return;
}

static void
axe_setmulti(struct axe_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr	*ifma;
	u_int32_t		h = 0;
	u_int16_t		rxmode;
	u_int8_t		hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, &rxmode);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxmode |= AXE_RXCMD_ALLMULTI;
		axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);
		return;
	} else
		rxmode &= ~AXE_RXCMD_ALLMULTI;

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			ETHER_ADDR_LEN);
		/* the filter bit position */
		h = (h >> 26) & 0x0000003F;
		hashtbl[h / 8] |= 1 << (h % 8);
	}

	axe_cmd(sc, AXE_CMD_WRITE_MCAST, 0, 0, &hashtbl);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);
}

static void
axe_reset(struct axe_softc *sc)
{
	if (sc->axe_dying)
		return;

	if (usbd_set_config_no(sc->axe_udev, AXE_CONFIG_NO, 1) ||
	    usbd_device2interface_handle(sc->axe_udev, AXE_IFACE_IDX,
	    &sc->axe_iface)) {
		if_printf(&sc->arpcom.ac_if,
			  "getting interface handle failed\n");
	}

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
	return;
}

/*
 * Probe for a AX88172 chip.
 */
static int
axe_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (!uaa->iface)
		return(UMATCH_NONE);

	return (usb_lookup(axe_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
axe_attach(device_t self)
{
	struct axe_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	u_char			eaddr[ETHER_ADDR_LEN];
	struct ifnet		*ifp;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	sc->axe_udev = uaa->device;
	callout_init(&sc->axe_stat_timer);

	if (usbd_set_config_no(sc->axe_udev, AXE_CONFIG_NO, 1)) {
		device_printf(self, "setting config no %d failed\n",
		    AXE_CONFIG_NO);
		return ENXIO;
	}

	if (usbd_device2interface_handle(uaa->device,
	    AXE_IFACE_IDX, &sc->axe_iface)) {
		device_printf(self, "getting interface handle failed\n");
		return ENXIO;
	}

	id = usbd_get_interface_descriptor(sc->axe_iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->axe_iface, i);
		if (!ed) {
			device_printf(self, "couldn't get ep %d\n", i);
			return ENXIO;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axe_ed[AXE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axe_ed[AXE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->axe_ed[AXE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	/*
	 * Get station address.
	 */
	axe_cmd(sc, AXE_CMD_READ_NODEID, 0, 0, &eaddr);

	/*
	 * Load IPG values and PHY indexes.
	 */
	axe_cmd(sc, AXE_CMD_READ_IPG012, 0, 0, &sc->axe_ipgs);
	axe_cmd(sc, AXE_CMD_READ_PHYID, 0, 0, &sc->axe_phyaddrs);

	/*
	 * Work around broken adapters that appear to lie about
	 * their PHY addresses.
	 */
	sc->axe_phyaddrs[0] = sc->axe_phyaddrs[1] = 0xFF;

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(self), device_get_unit(self));
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = axe_ioctl;
	ifp->if_start = axe_start;
	ifp->if_watchdog = axe_watchdog;
	ifp->if_init = axe_init;
	ifp->if_baudrate = 10000000;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	if (mii_phy_probe(self, &sc->axe_miibus,
	    axe_ifmedia_upd, axe_ifmedia_sts)) {
		device_printf(self, "MII without any PHY!\n");
		return ENXIO;
	}

	/*
	 * Call MI attach routine.
	 */

	ether_ifattach(ifp, eaddr, NULL);

	sc->axe_dying = 0;

	usb_register_netisr();

	return 0;
}

static int
axe_detach(device_t dev)
{
	struct axe_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	sc->axe_dying = 1;
	callout_stop(&sc->axe_stat_timer);
	ether_ifdetach(ifp);

	if (sc->axe_ep[AXE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_TX]);
	if (sc->axe_ep[AXE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_RX]);
	if (sc->axe_ep[AXE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_INTR]);

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
axe_newbuf(struct axe_softc *sc, struct axe_chain *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		m_new = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (m_new == NULL) {
			if_printf(&sc->arpcom.ac_if, "no memory for rx list "
			    "-- packet dropped!\n");
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);
	c->axe_mbuf = m_new;

	return(0);
}

static int
axe_rx_list_init(struct axe_softc *sc)
{
	struct axe_cdata	*cd;
	struct axe_chain	*c;
	int			i;

	cd = &sc->axe_cdata;
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		c = &cd->axe_rx_chain[i];
		c->axe_sc = sc;
		c->axe_idx = i;
		if (axe_newbuf(sc, c, NULL) == ENOBUFS)
			return(ENOBUFS);
		if (c->axe_xfer == NULL) {
			c->axe_xfer = usbd_alloc_xfer(sc->axe_udev);
			if (c->axe_xfer == NULL)
				return(ENOBUFS);
		}
	}

	return(0);
}

static int
axe_tx_list_init(struct axe_softc *sc)
{
	struct axe_cdata	*cd;
	struct axe_chain	*c;
	int			i;

	cd = &sc->axe_cdata;
	for (i = 0; i < AXE_TX_LIST_CNT; i++) {
		c = &cd->axe_tx_chain[i];
		c->axe_sc = sc;
		c->axe_idx = i;
		c->axe_mbuf = NULL;
		if (c->axe_xfer == NULL) {
			c->axe_xfer = usbd_alloc_xfer(sc->axe_udev);
			if (c->axe_xfer == NULL)
				return(ENOBUFS);
		}
		c->axe_buf = kmalloc(AXE_BUFSZ, M_USBDEV, M_WAITOK);
	}

	return(0);
}

static void
axe_rxstart(struct ifnet *ifp)
{
	struct axe_softc *sc = ifp->if_softc;
	struct axe_chain *c;

	c = &sc->axe_cdata.axe_rx_chain[sc->axe_cdata.axe_rx_prod];

	if (axe_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		return;
	}

	/* Setup new transfer. */
	usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_RX],
	    c, mtod(c->axe_mbuf, char *), AXE_BUFSZ, USBD_SHORT_XFER_OK,
	    USBD_NO_TIMEOUT, axe_rxeof);
	usbd_transfer(c->axe_xfer);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
axe_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct axe_chain *c = priv;
	struct axe_softc *sc = c->axe_sc;
        struct ifnet *ifp = &sc->arpcom.ac_if;
        struct mbuf *m;
	int total_len = 0;

	lwkt_serialize_enter(ifp->if_serializer);

	if (!(ifp->if_flags & IFF_RUNNING)) {
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			lwkt_serialize_exit(ifp->if_serializer);
			return;
		}
		if (usbd_ratecheck(&sc->axe_rx_notice)) {
			if_printf(ifp, "usb error on rx: %s\n",
			    usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->axe_ep[AXE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	m = c->axe_mbuf;

	if (total_len < sizeof(struct ether_header)) {
		ifp->if_ierrors++;
		goto done;
	}

	ifp->if_ipackets++;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = total_len;

	/* Put the packet on the special USB input queue. */
	usb_ether_input(m);
	axe_rxstart(ifp);
	lwkt_serialize_exit(ifp->if_serializer);
	return;
done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_RX],
	    c, mtod(c->axe_mbuf, char *), AXE_BUFSZ, USBD_SHORT_XFER_OK,
	    USBD_NO_TIMEOUT, axe_rxeof);
	usbd_transfer(c->axe_xfer);
	lwkt_serialize_exit(ifp->if_serializer);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
axe_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct axe_chain *c = priv;
	struct axe_softc *sc = c->axe_sc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	usbd_status err;

	lwkt_serialize_enter(ifp->if_serializer);
	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			lwkt_serialize_exit(ifp->if_serializer);
			return;
		}
		if_printf(ifp, "usb error on tx: %s\n", usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->axe_ep[AXE_ENDPT_TX]);
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;
	usbd_get_xfer_status(c->axe_xfer, NULL, NULL, NULL, &err);

	if (c->axe_mbuf != NULL) {
		m_freem(c->axe_mbuf);
		c->axe_mbuf = NULL;
	}

	if (err)
		ifp->if_oerrors++;
	else
		ifp->if_opackets++;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
axe_tick(void *xsc)
{
	struct axe_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii;

	mii = GET_MII(sc);
	if (mii == NULL)
		return;

	lwkt_serialize_enter(ifp->if_serializer);
	mii_tick(mii);
	if (!sc->axe_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->axe_link++;
		if (!ifq_is_empty(&ifp->if_snd))
			if_devstart(ifp);
	}
	callout_reset(&sc->axe_stat_timer, hz, axe_tick, sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
axe_encap(struct axe_softc *sc, struct mbuf *m, int idx)
{
	struct axe_chain	*c;
	usbd_status		err;

	c = &sc->axe_cdata.axe_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->axe_buf);
	c->axe_mbuf = m;

	usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_TX],
	    c, c->axe_buf, m->m_pkthdr.len, USBD_FORCE_SHORT_XFER,
	    10000, axe_txeof);

	/* Transmit */
	err = usbd_transfer(c->axe_xfer);
	if (err != USBD_IN_PROGRESS) {
		axe_stop(sc);
		return(EIO);
	}

	sc->axe_cdata.axe_tx_cnt++;

	return(0);
}

static void
axe_start(struct ifnet *ifp)
{
	struct axe_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;

	if (!sc->axe_link) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if (ifp->if_flags & IFF_OACTIVE) {
		return;
	}

	m_head = ifq_dequeue(&ifp->if_snd, NULL);
	if (m_head == NULL)
		return;

	if (axe_encap(sc, m_head, 0)) {
		/* axe_encap() will free m_head, if we reach here */
		ifp->if_flags |= IFF_OACTIVE;
		return;
	}

	/*
	 * If there's a BPF listener, bounce a copy of this frame
	 * to him.
	 */
	BPF_MTAP(ifp, m_head);

	ifp->if_flags |= IFF_OACTIVE;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
axe_init(void *xsc)
{
	struct axe_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct axe_chain	*c;
	usbd_status		err;
	int i, rxmode;

	if (ifp->if_flags & IFF_RUNNING) {
		return;
	}

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */

	axe_reset(sc);

#ifdef notdef
	/* Set MAC address */
	axe_mac(sc, sc->arpcom.ac_enaddr, 1);
#endif

	/* Enable RX logic. */

	/* Init TX ring. */
	if (axe_tx_list_init(sc) == ENOBUFS) {
		if_printf(ifp, "tx list init failed\n");
		return;
	}

	/* Init RX ring. */
	if (axe_rx_list_init(sc) == ENOBUFS) {
		if_printf(ifp, "rx list init failed\n");
		return;
	}

	/* Set transmitter IPG values */
	axe_cmd(sc, AXE_CMD_WRITE_IPG0, 0, sc->axe_ipgs[0], NULL);
	axe_cmd(sc, AXE_CMD_WRITE_IPG1, 0, sc->axe_ipgs[1], NULL);
	axe_cmd(sc, AXE_CMD_WRITE_IPG2, 0, sc->axe_ipgs[2], NULL);

	/* Enable receiver, set RX mode */
	rxmode = AXE_RXCMD_UNICAST|AXE_RXCMD_MULTICAST|AXE_RXCMD_ENABLE;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		rxmode |= AXE_RXCMD_PROMISC;

	if (ifp->if_flags & IFF_BROADCAST)
		rxmode |= AXE_RXCMD_BROADCAST;

	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);

	/* Load the multicast filter. */
	axe_setmulti(sc);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->axe_iface, sc->axe_ed[AXE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->axe_ep[AXE_ENDPT_RX]);
	if (err) {
		if_printf(ifp, "open rx pipe failed: %s\n", usbd_errstr(err));
		return;
	}

	err = usbd_open_pipe(sc->axe_iface, sc->axe_ed[AXE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->axe_ep[AXE_ENDPT_TX]);
	if (err) {
		if_printf(ifp, "open tx pipe failed: %s\n", usbd_errstr(err));
		return;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		c = &sc->axe_cdata.axe_rx_chain[i];
		usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_RX],
		    c, mtod(c->axe_mbuf, char *), AXE_BUFSZ,
		    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, axe_rxeof);
		usbd_transfer(c->axe_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset(&sc->axe_stat_timer, hz, axe_tick, sc);
}

static int
axe_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct axe_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct mii_data		*mii;
	u_int16_t		rxmode;
	int error = 0;

	switch(command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->axe_if_flags & IFF_PROMISC)) {
				axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, &rxmode);
				rxmode |= AXE_RXCMD_PROMISC;
				axe_cmd(sc, AXE_CMD_RXCTL_WRITE,
					0, rxmode, NULL);
				axe_setmulti(sc);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->axe_if_flags & IFF_PROMISC) {
				axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, &rxmode);
				rxmode &= ~AXE_RXCMD_PROMISC;
				axe_cmd(sc, AXE_CMD_RXCTL_WRITE,
					0, rxmode, NULL);
				axe_setmulti(sc);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				axe_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				axe_stop(sc);
		}
		sc->axe_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		axe_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = GET_MII(sc);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

static void
axe_watchdog(struct ifnet *ifp)
{
	struct axe_softc *sc = ifp->if_softc;
	struct axe_chain *c;
	usbd_status stat;

	ifp->if_oerrors++;
	if_printf(ifp, "watchdog timeout\n");

	c = &sc->axe_cdata.axe_tx_chain[0];
	usbd_get_xfer_status(c->axe_xfer, NULL, NULL, NULL, &stat);
	axe_txeof(c->axe_xfer, c, stat);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
axe_stop(struct axe_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int i;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	callout_stop(&sc->axe_stat_timer);

	/* Stop transfers. */
	if (sc->axe_ep[AXE_ENDPT_RX] != NULL) {
		err = usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "abort rx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "close rx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_RX] = NULL;
	}

	if (sc->axe_ep[AXE_ENDPT_TX] != NULL) {
		err = usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "abort tx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "close tx pipe failed: %s\n",
			    usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_TX] = NULL;
	}

	if (sc->axe_ep[AXE_ENDPT_INTR] != NULL) {
		err = usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "abort intr pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "close intr pipe failed: %s\n",
			    usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_INTR] = NULL;
	}

	axe_reset(sc);

	/* Free RX resources. */
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		if (sc->axe_cdata.axe_rx_chain[i].axe_buf != NULL) {
			kfree(sc->axe_cdata.axe_rx_chain[i].axe_buf, M_USBDEV);
			sc->axe_cdata.axe_rx_chain[i].axe_buf = NULL;
		}
		if (sc->axe_cdata.axe_rx_chain[i].axe_mbuf != NULL) {
			m_freem(sc->axe_cdata.axe_rx_chain[i].axe_mbuf);
			sc->axe_cdata.axe_rx_chain[i].axe_mbuf = NULL;
		}
		if (sc->axe_cdata.axe_rx_chain[i].axe_xfer != NULL) {
			usbd_free_xfer(sc->axe_cdata.axe_rx_chain[i].axe_xfer);
			sc->axe_cdata.axe_rx_chain[i].axe_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < AXE_TX_LIST_CNT; i++) {
		if (sc->axe_cdata.axe_tx_chain[i].axe_buf != NULL) {
			kfree(sc->axe_cdata.axe_tx_chain[i].axe_buf, M_USBDEV);
			sc->axe_cdata.axe_tx_chain[i].axe_buf = NULL;
		}
		if (sc->axe_cdata.axe_tx_chain[i].axe_mbuf != NULL) {
			m_freem(sc->axe_cdata.axe_tx_chain[i].axe_mbuf);
			sc->axe_cdata.axe_tx_chain[i].axe_mbuf = NULL;
		}
		if (sc->axe_cdata.axe_tx_chain[i].axe_xfer != NULL) {
			usbd_free_xfer(sc->axe_cdata.axe_tx_chain[i].axe_xfer);
			sc->axe_cdata.axe_tx_chain[i].axe_xfer = NULL;
		}
	}

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
        sc->axe_link = 0;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
axe_shutdown(device_t dev)
{
	struct axe_softc	*sc;

	sc = device_get_softc(dev);

	axe_stop(sc);
}
