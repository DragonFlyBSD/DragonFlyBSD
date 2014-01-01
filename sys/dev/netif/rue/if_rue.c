/*-
 * Copyright (c) 2001-2003, Shunsuke Akiyama <akiyama@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/usb/if_rue.c,v 1.14 2004/06/09 14:34:03 naddy Exp $
 */

/*
 * RealTek RTL8150 USB to fast ethernet controller driver.
 * Datasheet is available from
 * ftp://ftp.realtek.com.tw/lancard/data_sheet/8150/.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>

#include <net/bpf.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usbdivar.h>
#include <bus/usb/usb_ethersubr.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include <dev/netif/rue/if_ruereg.h>

#include "miibus_if.h"

#ifdef RUE_DEBUG
SYSCTL_NODE(_hw, OID_AUTO, rue, CTLFLAG_RW, 0, "USB rue");

static int	rue_debug = 0;
SYSCTL_INT(_hw_rue, OID_AUTO, debug, CTLFLAG_RW, &rue_debug, 0,
	   "rue debug level");

/* XXX DPRINTF/DPRINTFN can be used only after rue_attach() */
#define DPRINTFN(n, x)	do { if (rue_debug > (n)) if_printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

/*
 * Various supported device vendors/products.
 */

static const struct usb_devno rue_devs[] = {
	{ USB_DEVICE(0x0411, 0x0012) }, /* Melco LUA-KTX*/
	{ USB_DEVICE(0x0bda, 0x8150) }, /* Realtek USBKR100 (GREEN HOUSE) */
};

static int rue_match(device_t);
static int rue_attach(device_t);
static int rue_detach(device_t);

static int rue_tx_list_init(struct rue_softc *);
static int rue_rx_list_init(struct rue_softc *);
static int rue_newbuf(struct rue_softc *, struct rue_chain *, struct mbuf *);
static int rue_encap(struct rue_softc *, struct mbuf *, int);
#ifdef RUE_INTR_PIPE
static void rue_intr(usbd_xfer_handle, usbd_private_handle, usbd_status);
#endif
static void rue_rxeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void rue_txeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void rue_tick(void *);
static void rue_rxstart(struct ifnet *);
static void rue_start(struct ifnet *, struct ifaltq_subque *);
static int rue_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void rue_init(void *);
static void rue_stop(struct rue_softc *);
static void rue_watchdog(struct ifnet *);
static void rue_shutdown(device_t);
static int rue_ifmedia_upd(struct ifnet *);
static void rue_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int rue_miibus_readreg(device_t, int, int);
static int rue_miibus_writereg(device_t, int, int, int);
static void rue_miibus_statchg(device_t);

static void rue_setmulti(struct rue_softc *);
static void rue_reset(struct rue_softc *);

static int rue_read_mem(struct rue_softc *, u_int16_t, void *, u_int16_t);
static int rue_write_mem(struct rue_softc *, u_int16_t, void *, u_int16_t);
static int rue_csr_read_1(struct rue_softc *, int);
static int rue_csr_write_1(struct rue_softc *, int, u_int8_t);
static int rue_csr_read_2(struct rue_softc *, int);
static int rue_csr_write_2(struct rue_softc *, int, u_int16_t);
static int rue_csr_write_4(struct rue_softc *, int, u_int32_t);

static device_method_t rue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, rue_match),
	DEVMETHOD(device_attach, rue_attach),
	DEVMETHOD(device_detach, rue_detach),
	DEVMETHOD(device_shutdown, rue_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg, rue_miibus_readreg),
	DEVMETHOD(miibus_writereg, rue_miibus_writereg),
	DEVMETHOD(miibus_statchg, rue_miibus_statchg),

	DEVMETHOD_END
};

static driver_t rue_driver = {
	"rue",
	rue_methods,
	sizeof(struct rue_softc)
};

static devclass_t rue_devclass;

DRIVER_MODULE(rue, uhub, rue_driver, rue_devclass, usbd_driver_load, NULL);
DRIVER_MODULE(miibus, rue, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_DEPEND(rue, usb, 1, 1, 1);
MODULE_DEPEND(rue, miibus, 1, 1, 1);

#define RUE_SETBIT(sc, reg, x) \
	rue_csr_write_1(sc, reg, rue_csr_read_1(sc, reg) | (x))

#define RUE_CLRBIT(sc, reg, x) \
	rue_csr_write_1(sc, reg, rue_csr_read_1(sc, reg) & ~(x))

#define RUE_SETBIT_2(sc, reg, x) \
	rue_csr_write_2(sc, reg, rue_csr_read_2(sc, reg) | (x))

#define RUE_CLRBIT_2(sc, reg, x) \
	rue_csr_write_2(sc, reg, rue_csr_read_2(sc, reg) & ~(x))

static int
rue_read_mem(struct rue_softc *sc, u_int16_t addr, void *buf, u_int16_t len)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (sc->rue_dying)
		return (0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	err = usbd_do_request(sc->rue_udev, &req, buf);

	if (err) {
		if_printf(&sc->arpcom.ac_if, "control pipe read failed: %s\n",
			  usbd_errstr(err));
		return (-1);
	}

	return (0);
}

static int
rue_write_mem(struct rue_softc *sc, u_int16_t addr, void *buf, u_int16_t len)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (sc->rue_dying)
		return (0);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	err = usbd_do_request(sc->rue_udev, &req, buf);

	if (err) {
		if_printf(&sc->arpcom.ac_if, "control pipe write failed: %s\n",
			  usbd_errstr(err));
		return (-1);
	}

	return (0);
}

static int
rue_csr_read_1(struct rue_softc *sc, int reg)
{
	int		err;
	u_int8_t	val = 0;

	err = rue_read_mem(sc, reg, &val, 1);

	if (err)
		return (0);

	return (val);
}

static int
rue_csr_read_2(struct rue_softc *sc, int reg)
{
	int		err;
	u_int16_t	val = 0;
	uWord		w;

	USETW(w, val);
	err = rue_read_mem(sc, reg, &w, 2);
	val = UGETW(w);

	if (err)
		return (0);

	return (val);
}

static int
rue_csr_write_1(struct rue_softc *sc, int reg, u_int8_t val)
{
	int	err;

	err = rue_write_mem(sc, reg, &val, 1);

	if (err)
		return (-1);

	return (0);
}

static int
rue_csr_write_2(struct rue_softc *sc, int reg, u_int16_t val)
{
	int	err;
	uWord	w;

	USETW(w, val);
	err = rue_write_mem(sc, reg, &w, 2);

	if (err)
		return (-1);

	return (0);
}

static int
rue_csr_write_4(struct rue_softc *sc, int reg, u_int32_t val)
{
	int	err;
	uDWord	dw;

	USETDW(dw, val);
	err = rue_write_mem(sc, reg, &dw, 4);

	if (err)
		return (-1);

	return (0);
}

static int
rue_miibus_readreg(device_t dev, int phy, int reg)
{
	struct rue_softc	*sc = device_get_softc(dev);
	int			rval;
	int			ruereg;

	if (phy != 0)		/* RTL8150 supports PHY == 0, only */
		return (0);

	switch (reg) {
	case MII_BMCR:
		ruereg = RUE_BMCR;
		break;
	case MII_BMSR:
		ruereg = RUE_BMSR;
		break;
	case MII_ANAR:
		ruereg = RUE_ANAR;
		break;
	case MII_ANER:
		ruereg = RUE_AER;
		break;
	case MII_ANLPAR:
		ruereg = RUE_ANLP;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		return (0);
		break;
	default:
		if (RUE_REG_MIN <= reg && reg <= RUE_REG_MAX) {
			rval = rue_csr_read_1(sc, reg);
			return (rval);
		}
		if_printf(&sc->arpcom.ac_if, "bad phy register\n");
		return (0);
	}

	if (sc->arpcom.ac_if.if_serializer &&
	    IS_SERIALIZED(sc->arpcom.ac_if.if_serializer)) {
		RUE_UNLOCK(sc);
		rval = rue_csr_read_2(sc, ruereg);
		RUE_LOCK(sc);
	} else {
		rval = rue_csr_read_2(sc, ruereg);
	}

	return (rval);
}

static int
rue_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct rue_softc	*sc = device_get_softc(dev);
	int			ruereg;

	if (phy != 0)		/* RTL8150 supports PHY == 0, only */
		return (0);

	switch (reg) {
	case MII_BMCR:
		ruereg = RUE_BMCR;
		break;
	case MII_BMSR:
		ruereg = RUE_BMSR;
		break;
	case MII_ANAR:
		ruereg = RUE_ANAR;
		break;
	case MII_ANER:
		ruereg = RUE_AER;
		break;
	case MII_ANLPAR:
		ruereg = RUE_ANLP;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		return (0);
		break;
	default:
		if (RUE_REG_MIN <= reg && reg <= RUE_REG_MAX) {
			rue_csr_write_1(sc, reg, data);
			return (0);
		}
		if_printf(&sc->arpcom.ac_if, "bad phy register\n");
		return (0);
	}
	if (sc->arpcom.ac_if.if_serializer &&
	    IS_SERIALIZED(sc->arpcom.ac_if.if_serializer)) {
		RUE_UNLOCK(sc);
		rue_csr_write_2(sc, ruereg, data);
		RUE_LOCK(sc);
	} else {
		rue_csr_write_2(sc, ruereg, data);
	}

	return (0);
}

static void
rue_miibus_statchg(device_t dev)
{
}

/*
 * Program the 64-bit multicast hash filter.
 */

static void
rue_setmulti(struct rue_softc *sc)
{
	struct ifnet		*ifp;
	int			h = 0;
	u_int32_t		hashes[2] = { 0, 0 };
	struct ifmultiaddr	*ifma;
	u_int32_t		rxcfg;
	int			mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	rxcfg = rue_csr_read_2(sc, RUE_RCR);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxcfg |= (RUE_RCR_AAM | RUE_RCR_AAP);
		rxcfg &= ~RUE_RCR_AM;
		rue_csr_write_2(sc, RUE_RCR, rxcfg);
		rue_csr_write_4(sc, RUE_MAR0, 0xFFFFFFFF);
		rue_csr_write_4(sc, RUE_MAR4, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	rue_csr_write_4(sc, RUE_MAR0, 0);
	rue_csr_write_4(sc, RUE_MAR4, 0);

	/* now program new ones */
	TAILQ_FOREACH (ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}

	if (mcnt)
		rxcfg |= RUE_RCR_AM;
	else
		rxcfg &= ~RUE_RCR_AM;

	rxcfg &= ~(RUE_RCR_AAM | RUE_RCR_AAP);

	rue_csr_write_2(sc, RUE_RCR, rxcfg);
	rue_csr_write_4(sc, RUE_MAR0, hashes[0]);
	rue_csr_write_4(sc, RUE_MAR4, hashes[1]);
}

static void
rue_reset(struct rue_softc *sc)
{
	int	i;

	rue_csr_write_1(sc, RUE_CR, RUE_CR_SOFT_RST);

	for (i = 0; i < RUE_TIMEOUT; i++) {
		DELAY(500);
		if (!(rue_csr_read_1(sc, RUE_CR) & RUE_CR_SOFT_RST))
			break;
	}
	if (i == RUE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "reset never completed!\n");

	DELAY(10000);
}

/*
 * Probe for a RTL8150 chip.
 */

static int
rue_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	return (usb_lookup(rue_devs, uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */

static int
rue_attach(device_t self)
{
	struct rue_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	uint8_t				eaddr[ETHER_ADDR_LEN];
	struct ifnet			*ifp;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int				i;

	sc->rue_udev = uaa->device;

	if (usbd_set_config_no(sc->rue_udev, RUE_CONFIG_NO, 0)) {
		device_printf(self, "setting config no %d failed\n",
			      RUE_CONFIG_NO);
		return ENXIO;
	}

	if (usbd_device2interface_handle(uaa->device, RUE_IFACE_IDX,
					 &sc->rue_iface)) {
		device_printf(self, "getting interface handle failed\n");
		return ENXIO;
	}

	id = usbd_get_interface_descriptor(sc->rue_iface);

	/* Find endpoints */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->rue_iface, i);
		if (ed == NULL) {
			device_printf(self, "couldn't get ep %d\n", i);
			return ENXIO;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->rue_ed[RUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->rue_ed[RUE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->rue_ed[RUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(self), device_get_unit(self));

	/* Reset the adapter */
	rue_reset(sc);

	/* Get station address from the EEPROM */
	if (rue_read_mem(sc, RUE_EEPROM_IDR0, eaddr, ETHER_ADDR_LEN)) {
		device_printf(self, "couldn't get station address\n");
		return ENXIO;
	}

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rue_ioctl;
	ifp->if_start = rue_start;
	ifp->if_watchdog = rue_watchdog;
	ifp->if_init = rue_init;
	ifp->if_baudrate = 10000000;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* MII setup */
	if (mii_phy_probe(self, &sc->rue_miibus,
			  rue_ifmedia_upd, rue_ifmedia_sts)) {
		device_printf(self, "MII without any PHY!\n");
		return ENXIO;
	}

	/* Call MI attach routine */
	ether_ifattach(ifp, eaddr, NULL);

	callout_init(&sc->rue_stat_ch);
	sc->rue_dying = 0;

	usb_register_netisr();

	return 0;
}

static int
rue_detach(device_t dev)
{
	struct rue_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	RUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;

	sc->rue_dying = 1;
	callout_stop(&sc->rue_stat_ch);

	ether_ifdetach(ifp);

	if (sc->rue_ep[RUE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_TX]);
	if (sc->rue_ep[RUE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_RX]);
#ifdef RUE_INTR_PIPE
	if (sc->rue_ep[RUE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_INTR]);
#endif

	RUE_UNLOCK(sc);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */

static int
rue_newbuf(struct rue_softc *sc, struct rue_chain *c, struct mbuf *m)
{
	struct mbuf	*m_new = NULL;

	if (m == NULL) {
		m_new = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (m_new == NULL) {
			if_printf(&sc->arpcom.ac_if, "no memory for rx list "
				  "-- packet dropped!\n");
			return (ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);
	c->rue_mbuf = m_new;

	return (0);
}

static int
rue_rx_list_init(struct rue_softc *sc)
{
	struct rue_cdata	*cd;
	struct rue_chain	*c;
	int			i;

	cd = &sc->rue_cdata;
	for (i = 0; i < RUE_RX_LIST_CNT; i++) {
		c = &cd->rue_rx_chain[i];
		c->rue_sc = sc;
		c->rue_idx = i;
		if (rue_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->rue_xfer == NULL) {
			c->rue_xfer = usbd_alloc_xfer(sc->rue_udev);
			if (c->rue_xfer == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

static int
rue_tx_list_init(struct rue_softc *sc)
{
	struct rue_cdata	*cd;
	struct rue_chain	*c;
	int			i;

	cd = &sc->rue_cdata;
	for (i = 0; i < RUE_TX_LIST_CNT; i++) {
		c = &cd->rue_tx_chain[i];
		c->rue_sc = sc;
		c->rue_idx = i;
		c->rue_mbuf = NULL;
		if (c->rue_xfer == NULL) {
			c->rue_xfer = usbd_alloc_xfer(sc->rue_udev);
			if (c->rue_xfer == NULL)
				return (ENOBUFS);
		}
		c->rue_buf = kmalloc(RUE_BUFSZ, M_USBDEV, M_WAITOK);
	}

	return (0);
}

#ifdef RUE_INTR_PIPE
static void
rue_intr(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rue_softc	*sc = priv;
	struct ifnet		*ifp;
	struct rue_intrpkt	*p;

	RUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;

	if (!(ifp->if_flags & IFF_RUNNING)) {
		RUE_UNLOCK(sc);
		return;
	}

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			RUE_UNLOCK(sc);
			return;
		}
		if_printf(ifp, "usb error on intr: %s\n", usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->rue_ep[RUE_ENDPT_INTR]);
		RUE_UNLOCK(sc);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, (void **)&p, NULL, NULL);

	IFNET_STAT_INC(ifp, ierrors, p->rue_rxlost_cnt);
	IFNET_STAT_INC(ifp, ierrors, p->rue_crcerr_cnt);
	IFNET_STAT_INC(ifp, collisions, p->rue_col_cnt);

	RUE_UNLOCK(sc);
}
#endif

static void
rue_rxstart(struct ifnet *ifp)
{
	struct rue_softc	*sc;
	struct rue_chain	*c;

	sc = ifp->if_softc;
	c = &sc->rue_cdata.rue_rx_chain[sc->rue_cdata.rue_rx_prod];

	if (rue_newbuf(sc, c, NULL) == ENOBUFS) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		return;
	}

	/* Setup new transfer. */
	usbd_setup_xfer(c->rue_xfer, sc->rue_ep[RUE_ENDPT_RX],
		c, mtod(c->rue_mbuf, char *), RUE_BUFSZ, USBD_SHORT_XFER_OK,
		USBD_NO_TIMEOUT, rue_rxeof);
	usbd_transfer(c->rue_xfer);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */

static void
rue_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rue_chain	*c = priv;
	struct rue_softc	*sc = c->rue_sc;
	struct mbuf		*m;
	struct ifnet		*ifp;
	int			total_len = 0;
	struct rue_rxpkt	r;

	if (sc->rue_dying)
		return;
	RUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;

	if (!(ifp->if_flags & IFF_RUNNING)) {
		RUE_UNLOCK(sc);
		return;
	}

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			RUE_UNLOCK(sc);
			return;
		}
		if (usbd_ratecheck(&sc->rue_rx_notice)) {
			if_printf(ifp, "usb error on rx: %s\n",
				  usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->rue_ep[RUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len <= ETHER_CRC_LEN) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		goto done;
	}

	m = c->rue_mbuf;
	bcopy(mtod(m, char *) + total_len - 4, (char *)&r, sizeof (r));

	/* Check recieve packet was valid or not */
	if ((r.rue_rxstat & RUE_RXSTAT_VALID) == 0) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		goto done;
	}

	/* No errors; receive the packet. */
	total_len -= ETHER_CRC_LEN;

	IFNET_STAT_INC(ifp, ipackets, 1);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = total_len;

	/* Put the packet on the special USB input queue. */
	usb_ether_input(m);
	rue_rxstart(ifp);
	RUE_UNLOCK(sc);
	return;

    done:
	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->rue_ep[RUE_ENDPT_RX],
			c, mtod(c->rue_mbuf, char *), RUE_BUFSZ,
			USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rue_rxeof);
	usbd_transfer(xfer);
	RUE_UNLOCK(sc);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
rue_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rue_chain	*c = priv;
	struct rue_softc	*sc = c->rue_sc;
	struct ifnet		*ifp;
	usbd_status		err;

	RUE_LOCK(sc);

	ifp = &sc->arpcom.ac_if;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			RUE_UNLOCK(sc);
			return;
		}
		if_printf(ifp, "usb error on tx: %s\n", usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->rue_ep[RUE_ENDPT_TX]);
		RUE_UNLOCK(sc);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	usbd_get_xfer_status(c->rue_xfer, NULL, NULL, NULL, &err);

	if (c->rue_mbuf != NULL) {
		m_freem(c->rue_mbuf);
		c->rue_mbuf = NULL;
	}

	if (err)
		IFNET_STAT_INC(ifp, oerrors, 1);
	else
		IFNET_STAT_INC(ifp, opackets, 1);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	RUE_UNLOCK(sc);
}

static void
rue_tick(void *xsc)
{
	struct rue_softc	*sc = xsc;
	struct ifnet		*ifp;
	struct mii_data		*mii;

	if (sc == NULL)
		return;

	RUE_LOCK(sc);

	ifp = &sc->arpcom.ac_if;
	mii = GET_MII(sc);
	if (mii == NULL) {
		RUE_UNLOCK(sc);
		return;
	}

	/*
	 * USB mii functions make usb calls, we must unlock to avoid
	 * deadlocking on the usb bus if the request queue is full.
	 */
	RUE_UNLOCK(sc);
	mii_tick(mii);
	RUE_LOCK(sc);

	if (!(sc->rue_link && mii->mii_media_status & IFM_ACTIVE) &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->rue_link++;
		if (!ifq_is_empty(&ifp->if_snd))
			if_devstart(ifp);
	}

	callout_reset(&sc->rue_stat_ch, hz, rue_tick, sc);

	RUE_UNLOCK(sc);
}

static int
rue_encap(struct rue_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct rue_chain	*c;
	usbd_status		err;

	c = &sc->rue_cdata.rue_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->rue_buf);
	c->rue_mbuf = m;

	total_len = m->m_pkthdr.len;

	/*
	 * This is an undocumented behavior.
	 * RTL8150 chip doesn't send frame length smaller than
	 * RUE_MIN_FRAMELEN (60) byte packet.
	 */
	if (total_len < RUE_MIN_FRAMELEN)
		total_len = RUE_MIN_FRAMELEN;

	usbd_setup_xfer(c->rue_xfer, sc->rue_ep[RUE_ENDPT_TX],
			c, c->rue_buf, total_len, USBD_FORCE_SHORT_XFER,
			10000, rue_txeof);

	/* Transmit */
	err = usbd_transfer(c->rue_xfer);
	if (err != USBD_IN_PROGRESS) {
		rue_stop(sc);
		return (EIO);
	}

	sc->rue_cdata.rue_tx_cnt++;

	return (0);
}

static void
rue_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct rue_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if (!sc->rue_link) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if (ifq_is_oactive(&ifp->if_snd)) {
		return;
	}

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL) {
		return;
	}

	if (rue_encap(sc, m_head, 0)) {
		/* rue_encap() will free m_head, if we reach here */
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	/*
	 * If there's a BPF listener, bounce a copy of this frame
	 * to him.
	 */
	BPF_MTAP(ifp, m_head);

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
rue_init(void *xsc)
{
	struct rue_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = GET_MII(sc);
	struct rue_chain	*c;
	usbd_status		err;
	int			i;
	int			rxcfg;

	if (ifp->if_flags & IFF_RUNNING) {
		return;
	}

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	rue_reset(sc);

	/* Set MAC address */
	rue_write_mem(sc, RUE_IDR0, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Init TX ring. */
	if (rue_tx_list_init(sc) == ENOBUFS) {
		if_printf(ifp, "tx list init failed\n");
		return;
	}

	/* Init RX ring. */
	if (rue_rx_list_init(sc) == ENOBUFS) {
		if_printf(ifp, "rx list init failed\n");
		return;
	}

#ifdef RUE_INTR_PIPE
	sc->rue_cdata.rue_ibuf = kmalloc(RUE_INTR_PKTLEN, M_USBDEV, M_WAITOK);
#endif

	/*
	 * Set the initial TX and RX configuration.
	 */
	rue_csr_write_1(sc, RUE_TCR, RUE_TCR_CONFIG);

	rxcfg = RUE_RCR_CONFIG;

	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		rxcfg |= RUE_RCR_AB;
	else
		rxcfg &= ~RUE_RCR_AB;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		rxcfg |= RUE_RCR_AAP;
	else
		rxcfg &= ~RUE_RCR_AAP;

	rue_csr_write_2(sc, RUE_RCR, rxcfg);

	/* Load the multicast filter. */
	rue_setmulti(sc);

	/* Enable RX and TX */
	rue_csr_write_1(sc, RUE_CR, (RUE_CR_TE | RUE_CR_RE | RUE_CR_EP3CLREN));

	mii_mediachg(mii);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->rue_iface, sc->rue_ed[RUE_ENDPT_RX],
			     USBD_EXCLUSIVE_USE, &sc->rue_ep[RUE_ENDPT_RX]);
	if (err) {
		if_printf(ifp, "open rx pipe failed: %s\n", usbd_errstr(err));
		return;
	}
	err = usbd_open_pipe(sc->rue_iface, sc->rue_ed[RUE_ENDPT_TX],
			     USBD_EXCLUSIVE_USE, &sc->rue_ep[RUE_ENDPT_TX]);
	if (err) {
		if_printf(ifp, "open tx pipe failed: %s\n", usbd_errstr(err));
		return;
	}

#ifdef RUE_INTR_PIPE
	err = usbd_open_pipe_intr(sc->rue_iface, sc->rue_ed[RUE_ENDPT_INTR],
				  USBD_SHORT_XFER_OK,
				  &sc->rue_ep[RUE_ENDPT_INTR], sc,
				  sc->rue_cdata.rue_ibuf, RUE_INTR_PKTLEN,
				  rue_intr, RUE_INTR_INTERVAL);
	if (err) {
		if_printf(ifp, "open intr pipe failed: %s\n", usbd_errstr(err));
		return;
	}
#endif

	/* Start up the receive pipe. */
	for (i = 0; i < RUE_RX_LIST_CNT; i++) {
		c = &sc->rue_cdata.rue_rx_chain[i];
		usbd_setup_xfer(c->rue_xfer, sc->rue_ep[RUE_ENDPT_RX],
				c, mtod(c->rue_mbuf, char *), RUE_BUFSZ,
				USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rue_rxeof);
		usbd_transfer(c->rue_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->rue_stat_ch, hz, rue_tick, sc);
}

/*
 * Set media options.
 */

static int
rue_ifmedia_upd(struct ifnet *ifp)
{
	struct rue_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	sc->rue_link = 0;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH (miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return (0);
}

/*
 * Report current media status.
 */

static void
rue_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rue_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
rue_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct rue_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct mii_data		*mii;
	int			error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->rue_if_flags & IFF_PROMISC)) {
				RUE_SETBIT_2(sc, RUE_RCR,
					     (RUE_RCR_AAM | RUE_RCR_AAP));
				rue_setmulti(sc);
			} else if (ifp->if_flags & IFF_RUNNING &&
				   !(ifp->if_flags & IFF_PROMISC) &&
				   sc->rue_if_flags & IFF_PROMISC) {
				RUE_CLRBIT_2(sc, RUE_RCR,
					     (RUE_RCR_AAM | RUE_RCR_AAP));
				rue_setmulti(sc);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				rue_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rue_stop(sc);
		}
		sc->rue_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		rue_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		/*
		 * mii calls unfortunately issue usb commands which can
		 * deadlock against usb completions.
		 */
		mii = GET_MII(sc);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
rue_watchdog(struct ifnet *ifp)
{
	struct rue_softc	*sc = ifp->if_softc;
	struct rue_chain	*c;
	usbd_status		stat;

	IFNET_STAT_INC(ifp, oerrors, 1);
	if_printf(ifp, "watchdog timeout\n");

	c = &sc->rue_cdata.rue_tx_chain[0];
	usbd_get_xfer_status(c->rue_xfer, NULL, NULL, NULL, &stat);

	RUE_UNLOCK(sc);
	rue_txeof(c->rue_xfer, c, stat);
	RUE_LOCK(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */

static void
rue_stop(struct rue_softc *sc)
{
	usbd_status	err;
	struct ifnet	*ifp;
	int		i;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	rue_csr_write_1(sc, RUE_CR, 0x00);
	rue_reset(sc);

	callout_stop(&sc->rue_stat_ch);

	/* Stop transfers. */
	RUE_UNLOCK(sc);
	if (sc->rue_ep[RUE_ENDPT_RX] != NULL) {
		err = usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "abort rx pipe failed: %s\n",
				  usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->rue_ep[RUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "close rx pipe failed: %s\n",
				  usbd_errstr(err));
		}
		sc->rue_ep[RUE_ENDPT_RX] = NULL;
	}

	if (sc->rue_ep[RUE_ENDPT_TX] != NULL) {
		err = usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "abort tx pipe failed: %s\n",
				  usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->rue_ep[RUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "close tx pipe failed: %s\n",
				  usbd_errstr(err));
		}
		sc->rue_ep[RUE_ENDPT_TX] = NULL;
	}

#ifdef RUE_INTR_PIPE
	if (sc->rue_ep[RUE_ENDPT_INTR] != NULL) {
		err = usbd_abort_pipe(sc->rue_ep[RUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "abort intr pipe failed: %s\n",
				  usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->rue_ep[RUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "close intr pipe failed: %s\n",
				  usbd_errstr(err));
		}
		sc->rue_ep[RUE_ENDPT_INTR] = NULL;
	}
#endif
	RUE_LOCK(sc);

	/* Free RX resources. */
	for (i = 0; i < RUE_RX_LIST_CNT; i++) {
		if (sc->rue_cdata.rue_rx_chain[i].rue_buf != NULL) {
			kfree(sc->rue_cdata.rue_rx_chain[i].rue_buf, M_USBDEV);
			sc->rue_cdata.rue_rx_chain[i].rue_buf = NULL;
		}
		if (sc->rue_cdata.rue_rx_chain[i].rue_mbuf != NULL) {
			m_freem(sc->rue_cdata.rue_rx_chain[i].rue_mbuf);
			sc->rue_cdata.rue_rx_chain[i].rue_mbuf = NULL;
		}
		if (sc->rue_cdata.rue_rx_chain[i].rue_xfer != NULL) {
			usbd_free_xfer(sc->rue_cdata.rue_rx_chain[i].rue_xfer);
			sc->rue_cdata.rue_rx_chain[i].rue_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < RUE_TX_LIST_CNT; i++) {
		if (sc->rue_cdata.rue_tx_chain[i].rue_buf != NULL) {
			kfree(sc->rue_cdata.rue_tx_chain[i].rue_buf, M_USBDEV);
			sc->rue_cdata.rue_tx_chain[i].rue_buf = NULL;
		}
		if (sc->rue_cdata.rue_tx_chain[i].rue_mbuf != NULL) {
			m_freem(sc->rue_cdata.rue_tx_chain[i].rue_mbuf);
			sc->rue_cdata.rue_tx_chain[i].rue_mbuf = NULL;
		}
		if (sc->rue_cdata.rue_tx_chain[i].rue_xfer != NULL) {
			usbd_free_xfer(sc->rue_cdata.rue_tx_chain[i].rue_xfer);
			sc->rue_cdata.rue_tx_chain[i].rue_xfer = NULL;
		}
	}

#ifdef RUE_INTR_PIPE
	if (sc->rue_cdata.rue_ibuf != NULL) {
		kfree(sc->rue_cdata.rue_ibuf, M_USBDEV);
		sc->rue_cdata.rue_ibuf = NULL;
	}
#endif

	sc->rue_link = 0;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */

static void
rue_shutdown(device_t dev)
{
	struct rue_softc	*sc;

	sc = device_get_softc(dev);

	sc->rue_dying++;
	RUE_LOCK(sc);
	rue_reset(sc);
	rue_stop(sc);
	RUE_UNLOCK(sc);
}
