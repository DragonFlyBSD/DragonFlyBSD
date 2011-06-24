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
 * $FreeBSD: src/sys/dev/usb/if_aue.c,v 1.78 2003/12/17 14:23:07 sanpei Exp $
 */

/*
 * ADMtek AN986 Pegasus and AN8511 Pegasus II USB to ethernet driver.
 * Datasheet is available from http://www.admtek.com.tw.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Pegasus chip uses four USB "endpoints" to provide 10/100 ethernet
 * support: the control endpoint for reading/writing registers, burst
 * read endpoint for packet reception, burst write for packet transmission
 * and one for "interrupts." The chip uses the same RX filter scheme
 * as the other ADMtek ethernet parts: one perfect filter entry for the
 * the station address and a 64-bit multicast hash table. The chip supports
 * both MII and HomePNA attachments.
 *
 * Since the maximum data transfer speed of USB is supposed to be 12Mbps,
 * you're never really going to get 100Mbps speeds from this device. I
 * think the idea is to allow the device to connect to 10 or 100Mbps
 * networks, not necessarily to provide 100Mbps performance. Also, since
 * the controller uses an external PHY chip, it's possible that board
 * designers might simply choose a 10Mbps PHY.
 *
 * Registers are accessed using usbd_do_request(). Packet transfers are
 * done using usbd_transfer() and friends.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>

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

#include "../mii_layer/mii.h"
#include "../mii_layer/miivar.h"

#include "if_auereg.h"

MODULE_DEPEND(aue, usb, 1, 1, 1);
MODULE_DEPEND(aue, miibus, 1, 1, 1);

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

struct aue_type {
	struct usb_devno        aue_dev;
	u_int16_t               aue_flags;
#define LSYS  0x0001          /* use Linksys reset */
#define PNA   0x0002          /* has Home PNA */
#define PII   0x0004          /* Pegasus II chip */
};

static const struct aue_type aue_devs[] = {
 {{ USB_DEVICE(0x03f0, 0x811c) }, PII },  /* HP HN210E */
 {{ USB_DEVICE(0x0411, 0x0001) }, 0 },    /* Melco LUA-TX */
 {{ USB_DEVICE(0x0411, 0x0005) }, 0 },    /* Melco LUA-TX */
 {{ USB_DEVICE(0x0411, 0x0009) }, PII },  /* Melco LUA2-TX */
 {{ USB_DEVICE(0x045e, 0x007a) }, PII },  /* Microsoft MN110 */
 {{ USB_DEVICE(0x04bb, 0x0904) }, 0 },    /* I-O DATA USB ETTX */
 {{ USB_DEVICE(0x04bb, 0x0913) }, PII },  /* I-O DATA USB ETTX */
 {{ USB_DEVICE(0x0506, 0x4601) }, PII },  /* 3com HomeConnect 3C460B */
 {{ USB_DEVICE(0x050d, 0x0121) }, PII },  /* Belkin USB to LAN Converter */
 {{ USB_DEVICE(0x056e, 0x200c) }, 0 },    /* Elecom LD-USB/TX */
 {{ USB_DEVICE(0x056e, 0x4002) }, LSYS }, /* Elecom LD-USB/TX */
 {{ USB_DEVICE(0x056e, 0x4005) }, PII },  /* Elecom LD-USBL/TX */
 {{ USB_DEVICE(0x056e, 0x400b) }, 0 },    /* Elecom LD-USB/TX */
 {{ USB_DEVICE(0x056e, 0xabc1) }, LSYS }, /* Elecom LD-USB/TX */
 {{ USB_DEVICE(0x05cc, 0x3000) }, 0 },    /* Elsa Microlink USB2Ethernet */
 {{ USB_DEVICE(0x066b, 0x200c) }, LSYS|PII }, /* Linksys USB10TX */
 {{ USB_DEVICE(0x066b, 0x2202) }, LSYS }, /* Linksys USB10T */
 {{ USB_DEVICE(0x066b, 0x2203) }, LSYS }, /* Linksys USB100TX */
 {{ USB_DEVICE(0x066b, 0x2204) }, LSYS|PNA }, /* Linksys USB100H1 */
 {{ USB_DEVICE(0x066b, 0x2206) }, LSYS }, /* Linksys USB10TA */
 {{ USB_DEVICE(0x066b, 0x400b) }, LSYS|PII }, /* Linksys USB10TX */
 {{ USB_DEVICE(0x067c, 0x1001) }, PII },  /* Siemens SpeedStream USB */
 {{ USB_DEVICE(0x0707, 0x0200) }, 0 },    /* SMC 2202USB */
 {{ USB_DEVICE(0x0707, 0x0201) }, PII },  /* SMC 2206USB */
 {{ USB_DEVICE(0x07a6, 0x0986) }, PNA },  /* ADMtek AN986 */
 {{ USB_DEVICE(0x07a6, 0x8511) }, PII },  /* ADMtek AN8511 */
 {{ USB_DEVICE(0x07a6, 0x8513) }, PII },  /* ADMtek AN8513 */
 {{ USB_DEVICE(0x07aa, 0x0004) }, 0 },    /* Corega FEther USB-TX */
 {{ USB_DEVICE(0x07aa, 0x000d) }, PII },  /* Corega FEther USB-TXS */
 {{ USB_DEVICE(0x07b8, 0x110c) }, PNA|PII }, /* AboCom XX1 */
 {{ USB_DEVICE(0x07b8, 0x200c) }, PII },  /* AboCom XX2 */
 {{ USB_DEVICE(0x07b8, 0x4002) }, LSYS }, /* AboCom UFE1000 */
 {{ USB_DEVICE(0x07b8, 0x4003) }, 0 },    /* AboCom DSB650TX_PNA */
 {{ USB_DEVICE(0x07b8, 0x4004) }, PNA },  /* AboCom XX4 */
 {{ USB_DEVICE(0x07b8, 0x4007) }, PNA },  /* AboCom XX5 */
 {{ USB_DEVICE(0x07b8, 0x400b) }, PII },  /* AboCom XX6 */
 {{ USB_DEVICE(0x07b8, 0x400c) }, PII },  /* AboCom XX7 */
 {{ USB_DEVICE(0x07b8, 0x4102) }, PII },  /* AboCom XX8 */
 {{ USB_DEVICE(0x07b8, 0x4104) }, PNA },  /* AboCom XX9 */
 {{ USB_DEVICE(0x07b8, 0xabc1) }, 0 },    /* AboCom XX10 */
 {{ USB_DEVICE(0x083a, 0x1046) }, 0 },    /* Accton USB320-EC */
 {{ USB_DEVICE(0x083a, 0x5046) }, PII },  /* Accton SpeedStream 1001 */
 {{ USB_DEVICE(0x08d1, 0x0003) }, PII },  /* SmartBridges smartNIC 2 PnP */
 {{ USB_DEVICE(0x08dd, 0x0986) }, 0 },    /* Billionton USB100N */
 {{ USB_DEVICE(0x08dd, 0x0987) }, PNA },  /* Billionton USB100LP */
 {{ USB_DEVICE(0x08dd, 0x0988) }, 0 },    /* Billionton USB100EL */
 {{ USB_DEVICE(0x08dd, 0x8511) }, PII },  /* Billionton USBE100 */
 {{ USB_DEVICE(0x0951, 0x000a) }, 0 },    /* Kingston KNU101TX */
 {{ USB_DEVICE(0x0e66, 0x400c) }, PII },  /* Hawking UF100 */
 {{ USB_DEVICE(0x15e8, 0x9100) }, 0 },    /* SOHOware NUB100 */
 {{ USB_DEVICE(0x2001, 0x200c) }, LSYS|PII },/* D-Link DSB650TX4 */
 {{ USB_DEVICE(0x2001, 0x4001) }, LSYS }, /* D-Link DSB650TX1 */
 {{ USB_DEVICE(0x2001, 0x4002) }, LSYS }, /* D-Link DSB650TX */
 {{ USB_DEVICE(0x2001, 0x4003) }, PNA },  /* D-Link DSB650TX_PNA */
 {{ USB_DEVICE(0x2001, 0x400b) }, LSYS|PII }, /* D-Link DSB650TX3 */
 {{ USB_DEVICE(0x2001, 0x4102) }, LSYS|PII }, /* D-Link DSB650TX2 */
 {{ USB_DEVICE(0x2001, 0xabc1) }, LSYS }, /* D-Link DSB650 */
};
#define aue_lookup(v, p) ((const struct aue_type *)usb_lookup(aue_devs, v, p))

static int aue_match(device_t);
static int aue_attach(device_t);
static int aue_detach(device_t);

static void aue_reset_pegasus_II(struct aue_softc *sc);
static int aue_tx_list_init(struct aue_softc *);
static int aue_rx_list_init(struct aue_softc *);
static int aue_newbuf(struct aue_softc *, struct aue_chain *, struct mbuf *);
static int aue_encap(struct aue_softc *, struct mbuf *, int);
#ifdef AUE_INTR_PIPE
static void aue_intr(usbd_xfer_handle, usbd_private_handle, usbd_status);
#endif
static void aue_rxeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void aue_txeof(usbd_xfer_handle, usbd_private_handle, usbd_status);
static void aue_tick(void *);
static void aue_rxstart(struct ifnet *);
static void aue_start(struct ifnet *);
static int aue_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void aue_init(void *);
static void aue_stop(struct aue_softc *);
static void aue_watchdog(struct ifnet *);
static void aue_shutdown(device_t);
static int aue_ifmedia_upd(struct ifnet *);
static void aue_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void aue_eeprom_getword(struct aue_softc *, int, u_int16_t *);
static void aue_read_eeprom(struct aue_softc *, caddr_t, int, int, int);
static int aue_miibus_readreg(device_t, int, int);
static int aue_miibus_writereg(device_t, int, int, int);
static void aue_miibus_statchg(device_t);

static void aue_setmulti(struct aue_softc *);
static void aue_reset(struct aue_softc *);

static int aue_csr_read_1(struct aue_softc *, int);
static int aue_csr_write_1(struct aue_softc *, int, int);
static int aue_csr_read_2(struct aue_softc *, int);
static int aue_csr_write_2(struct aue_softc *, int, int);

static device_method_t aue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aue_match),
	DEVMETHOD(device_attach,	aue_attach),
	DEVMETHOD(device_detach,	aue_detach),
	DEVMETHOD(device_shutdown,	aue_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	aue_miibus_readreg),
	DEVMETHOD(miibus_writereg,	aue_miibus_writereg),
	DEVMETHOD(miibus_statchg,	aue_miibus_statchg),

	{ 0, 0 }
};

static driver_t aue_driver = {
	"aue",
	aue_methods,
	sizeof(struct aue_softc)
};

static devclass_t aue_devclass;

DECLARE_DUMMY_MODULE(if_aue);
DRIVER_MODULE(aue, uhub, aue_driver, aue_devclass, usbd_driver_load, NULL);
DRIVER_MODULE(miibus, aue, miibus_driver, miibus_devclass, NULL, NULL);

#define AUE_SETBIT(sc, reg, x)				\
	aue_csr_write_1(sc, reg, aue_csr_read_1(sc, reg) | (x))

#define AUE_CLRBIT(sc, reg, x)				\
	aue_csr_write_1(sc, reg, aue_csr_read_1(sc, reg) & ~(x))

static int
aue_csr_read_1(struct aue_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	u_int8_t		val = 0;

	if (sc->aue_dying)
		return(0);

	AUE_LOCK(sc);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AUE_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->aue_udev, &req, &val);

	AUE_UNLOCK(sc);

	if (err) {
		return (0);
	}

	return (val);
}

static int
aue_csr_read_2(struct aue_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	u_int16_t		val = 0;

	if (sc->aue_dying)
		return (0);

	AUE_LOCK(sc);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AUE_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->aue_udev, &req, &val);

	AUE_UNLOCK(sc);

	if (err) {
		return (0);
	}

	return (val);
}

static int
aue_csr_write_1(struct aue_softc *sc, int reg, int val)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (sc->aue_dying)
		return (0);

	AUE_LOCK(sc);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AUE_UR_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->aue_udev, &req, &val);

	AUE_UNLOCK(sc);

	if (err) {
		return (-1);
	}

	return (0);
}

static int
aue_csr_write_2(struct aue_softc *sc, int reg, int val)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (sc->aue_dying)
		return (0);

	AUE_LOCK(sc);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AUE_UR_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->aue_udev, &req, &val);

	AUE_UNLOCK(sc);

	if (err) {
		return (-1);
	}

	return (0);
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
aue_eeprom_getword(struct aue_softc *sc, int addr, u_int16_t *dest)
{
	int		i;
	u_int16_t	word = 0;

	aue_csr_write_1(sc, AUE_EE_REG, addr);
	aue_csr_write_1(sc, AUE_EE_CTL, AUE_EECTL_READ);

	for (i = 0; i < AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_EE_CTL) & AUE_EECTL_DONE)
			break;
	}

	if (i == AUE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "EEPROM read timed out\n");

	word = aue_csr_read_2(sc, AUE_EE_DATA);
	*dest = word;

	return;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
aue_read_eeprom(struct aue_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int			i;
	u_int16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		aue_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}

	return;
}

static int
aue_miibus_readreg(device_t dev, int phy, int reg)
{
	struct aue_softc	*sc = device_get_softc(dev);
	int			i;
	u_int16_t		val = 0;

	/*
	 * The Am79C901 HomePNA PHY actually contains
	 * two transceivers: a 1Mbps HomePNA PHY and a
	 * 10Mbps full/half duplex ethernet PHY with
	 * NWAY autoneg. However in the ADMtek adapter,
	 * only the 1Mbps PHY is actually connected to
	 * anything, so we ignore the 10Mbps one. It
	 * happens to be configured for MII address 3,
	 * so we filter that out.
	 */
	if (sc->aue_vendor == 0x07a6 && sc->aue_product == 0x0986) {
		if (phy == 3)
			return (0);
#ifdef notdef
		if (phy != 1)
			return (0);
#endif
	}

	aue_csr_write_1(sc, AUE_PHY_ADDR, phy);
	aue_csr_write_1(sc, AUE_PHY_CTL, reg | AUE_PHYCTL_READ);

	for (i = 0; i < AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_PHY_CTL) & AUE_PHYCTL_DONE)
			break;
	}

	if (i == AUE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "MII read timed out\n");

	val = aue_csr_read_2(sc, AUE_PHY_DATA);

	return (val);
}

static int
aue_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct aue_softc	*sc = device_get_softc(dev);
	int			i;

	if (phy == 3)
		return (0);

	aue_csr_write_2(sc, AUE_PHY_DATA, data);
	aue_csr_write_1(sc, AUE_PHY_ADDR, phy);
	aue_csr_write_1(sc, AUE_PHY_CTL, reg | AUE_PHYCTL_WRITE);

	for (i = 0; i < AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_PHY_CTL) & AUE_PHYCTL_DONE)
			break;
	}

	if (i == AUE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "MII read timed out\n");

	return(0);
}

static void
aue_miibus_statchg(device_t dev)
{
	struct aue_softc	*sc = device_get_softc(dev);
	struct mii_data		*mii = GET_MII(sc);

	AUE_CLRBIT(sc, AUE_CTL0, AUE_CTL0_RX_ENB | AUE_CTL0_TX_ENB);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
		AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_SPEEDSEL);
	} else {
		AUE_CLRBIT(sc, AUE_CTL1, AUE_CTL1_SPEEDSEL);
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_DUPLEX);
	else
		AUE_CLRBIT(sc, AUE_CTL1, AUE_CTL1_DUPLEX);

	AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_RX_ENB | AUE_CTL0_TX_ENB);

	/*
	 * Set the LED modes on the LinkSys adapter.
	 * This turns on the 'dual link LED' bin in the auxmode
	 * register of the Broadcom PHY.
	 */
	if (sc->aue_flags & LSYS) {
		u_int16_t auxmode;
		auxmode = aue_miibus_readreg(dev, 0, 0x1b);
		aue_miibus_writereg(dev, 0, 0x1b, auxmode | 0x04);
	}

	return;
}

#define	AUE_BITS	6

static void
aue_setmulti(struct aue_softc *sc)
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	u_int32_t		h = 0, i;

	ifp = &sc->arpcom.ac_if;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_ALLMULTI);
		return;
	}

	AUE_CLRBIT(sc, AUE_CTL0, AUE_CTL0_ALLMULTI);

	/* first, zot all the existing hash bits */
	for (i = 0; i < 8; i++)
		aue_csr_write_1(sc, AUE_MAR0 + i, 0);

	/* now program new ones */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
	{
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) & ((1 << AUE_BITS) - 1);
		AUE_SETBIT(sc, AUE_MAR + (h >> 3), 1 << (h & 0x7));
	}

	return;
}

static void
aue_reset_pegasus_II(struct aue_softc *sc)
{
	/* Magic constants taken from Linux driver. */
	aue_csr_write_1(sc, AUE_REG_1D, 0);
	aue_csr_write_1(sc, AUE_REG_7B, 2);
#if 0
	if ((sc->aue_flags & HAS_HOME_PNA) && mii_mode)
		aue_csr_write_1(sc, AUE_REG_81, 6);
	else
#endif
		aue_csr_write_1(sc, AUE_REG_81, 2);
}

static void
aue_reset(struct aue_softc *sc)
{
	int		i;

	AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_RESETMAC);

	for (i = 0; i < AUE_TIMEOUT; i++) {
		if (!(aue_csr_read_1(sc, AUE_CTL1) & AUE_CTL1_RESETMAC))
			break;
	}

	if (i == AUE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "reset failed\n");

	/*
	 * The PHY(s) attached to the Pegasus chip may be held
	 * in reset until we flip on the GPIO outputs. Make sure
	 * to set the GPIO pins high so that the PHY(s) will
	 * be enabled.
	 *
	 * Note: We force all of the GPIO pins low first, *then*
	 * enable the ones we want.
	 */
	aue_csr_write_1(sc, AUE_GPIO0, AUE_GPIO_OUT0|AUE_GPIO_SEL0);
	aue_csr_write_1(sc, AUE_GPIO0, AUE_GPIO_OUT0|AUE_GPIO_SEL0|AUE_GPIO_SEL1);

	if (sc->aue_flags & LSYS) {
		/* Grrr. LinkSys has to be different from everyone else. */
		aue_csr_write_1(sc, AUE_GPIO0,
		    AUE_GPIO_SEL0 | AUE_GPIO_SEL1);
		aue_csr_write_1(sc, AUE_GPIO0,
		    AUE_GPIO_SEL0 | AUE_GPIO_SEL1 | AUE_GPIO_OUT0);
	}

	if (sc->aue_flags & PII)
                aue_reset_pegasus_II(sc);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(10000);

	return;
}

/*
 * Probe for a Pegasus chip.
 */
static int
aue_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return (aue_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
aue_attach(device_t self)
{
	struct aue_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	u_char			eaddr[ETHER_ADDR_LEN];
	struct ifnet		*ifp;
	usbd_interface_handle	iface;
	usbd_status		err;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	sc->aue_udev = uaa->device;
	callout_init(&sc->aue_stat_timer);

	if (usbd_set_config_no(sc->aue_udev, AUE_CONFIG_NO, 0)) {
		device_printf(self, "setting config no %d failed\n",
			      AUE_CONFIG_NO);
		return ENXIO;
	}

	err = usbd_device2interface_handle(uaa->device, AUE_IFACE_IDX, &iface);
	if (err) {
		device_printf(self, "getting interface handle failed\n");
		return ENXIO;
	}

	sc->aue_iface = iface;
	sc->aue_flags = aue_lookup(uaa->vendor, uaa->product)->aue_flags;

	sc->aue_product = uaa->product;
	sc->aue_vendor = uaa->vendor;

	id = usbd_get_interface_descriptor(sc->aue_iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			device_printf(self, "couldn't get ep %d\n", i);
			return ENXIO;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->aue_ed[AUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->aue_ed[AUE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->aue_ed[AUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	AUE_LOCK(sc);

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(self), device_get_unit(self));

	/* Reset the adapter. */
	aue_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	aue_read_eeprom(sc, (caddr_t)&eaddr, 0, 3, 0);

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = aue_ioctl;
	ifp->if_start = aue_start;
	ifp->if_watchdog = aue_watchdog;
	ifp->if_init = aue_init;
	ifp->if_baudrate = 10000000;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * Do MII setup.
	 * NOTE: Doing this causes child devices to be attached to us,
	 * which we would normally disconnect at in the detach routine
	 * using device_delete_child(). However the USB code is set up
	 * such that when this driver is removed, all children devices
	 * are removed as well. In effect, the USB code ends up detaching
	 * all of our children for us, so we don't have to do is ourselves
	 * in aue_detach(). It's important to point this out since if
	 * we *do* try to detach the child devices ourselves, we will
	 * end up getting the children deleted twice, which will crash
	 * the system.
	 */
	if (mii_phy_probe(self, &sc->aue_miibus,
	    aue_ifmedia_upd, aue_ifmedia_sts)) {
		device_printf(self, "MII without any PHY!\n");
		AUE_UNLOCK(sc);
		return ENXIO;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);
	usb_register_netisr();
	sc->aue_dying = 0;

	AUE_UNLOCK(sc);
	return 0;
}

static int
aue_detach(device_t dev)
{
	struct aue_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	AUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;

	sc->aue_dying = 1;
	callout_stop(&sc->aue_stat_timer);
	ether_ifdetach(ifp);

	if (sc->aue_ep[AUE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_TX]);
	if (sc->aue_ep[AUE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_RX]);
#ifdef AUE_INTR_PIPE
	if (sc->aue_ep[AUE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_INTR]);
#endif

	AUE_UNLOCK(sc);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
aue_newbuf(struct aue_softc *sc, struct aue_chain *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		m_new = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (m_new == NULL) {
			if_printf(&sc->arpcom.ac_if,
			    "no memory for rx list -- packet dropped!\n");
			return (ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);
	c->aue_mbuf = m_new;

	return (0);
}

static int
aue_rx_list_init(struct aue_softc *sc)
{
	struct aue_cdata	*cd;
	struct aue_chain	*c;
	int			i;

	cd = &sc->aue_cdata;
	for (i = 0; i < AUE_RX_LIST_CNT; i++) {
		c = &cd->aue_rx_chain[i];
		c->aue_sc = sc;
		c->aue_idx = i;
		if (aue_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->aue_xfer == NULL) {
			c->aue_xfer = usbd_alloc_xfer(sc->aue_udev);
			if (c->aue_xfer == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

static int
aue_tx_list_init(struct aue_softc *sc)
{
	struct aue_cdata	*cd;
	struct aue_chain	*c;
	int			i;

	cd = &sc->aue_cdata;
	for (i = 0; i < AUE_TX_LIST_CNT; i++) {
		c = &cd->aue_tx_chain[i];
		c->aue_sc = sc;
		c->aue_idx = i;
		c->aue_mbuf = NULL;
		if (c->aue_xfer == NULL) {
			c->aue_xfer = usbd_alloc_xfer(sc->aue_udev);
			if (c->aue_xfer == NULL)
				return (ENOBUFS);
		}
		c->aue_buf = kmalloc(AUE_BUFSZ, M_USBDEV, M_WAITOK);
	}

	return (0);
}

#ifdef AUE_INTR_PIPE
static void
aue_intr(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct aue_softc	*sc = priv;
	struct ifnet		*ifp;
	struct aue_intrpkt	*p;

	AUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;

	if (!(ifp->if_flags & IFF_RUNNING)) {
		AUE_UNLOCK(sc);
		return;
	}

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			AUE_UNLOCK(sc);
			return;
		}
		if_printf(ifp, "usb error on intr: %s\n", usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->aue_ep[AUE_ENDPT_RX]);
		AUE_UNLOCK(sc);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, (void **)&p, NULL, NULL);

	if (p->aue_txstat0)
		ifp->if_oerrors++;

	if (p->aue_txstat0 & (AUE_TXSTAT0_LATECOLL & AUE_TXSTAT0_EXCESSCOLL))
		ifp->if_collisions++;

	AUE_UNLOCK(sc);
	return;
}
#endif

static void
aue_rxstart(struct ifnet *ifp)
{
	struct aue_softc	*sc;
	struct aue_chain	*c;

	sc = ifp->if_softc;
	AUE_LOCK(sc);
	c = &sc->aue_cdata.aue_rx_chain[sc->aue_cdata.aue_rx_prod];

	if (aue_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		AUE_UNLOCK(sc);
		return;
	}

	/* Setup new transfer. */
	usbd_setup_xfer(c->aue_xfer, sc->aue_ep[AUE_ENDPT_RX],
	    c, mtod(c->aue_mbuf, char *), AUE_BUFSZ, USBD_SHORT_XFER_OK,
	    USBD_NO_TIMEOUT, aue_rxeof);
	usbd_transfer(c->aue_xfer);

	AUE_UNLOCK(sc);
	return;
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
aue_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct aue_chain	*c = priv;
	struct aue_softc	*sc = c->aue_sc;
        struct mbuf		*m;
        struct ifnet		*ifp;
	int			total_len = 0;
	struct aue_rxpkt	r;

	if (sc->aue_dying)
		return;

	ifp = &sc->arpcom.ac_if;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->aue_rx_notice)) {
			if_printf(ifp, "usb error on rx: %s\n",
			    usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->aue_ep[AUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len <= 4 + ETHER_CRC_LEN) {
		ifp->if_ierrors++;
		goto done;
	}

	m = c->aue_mbuf;
	bcopy(mtod(m, char *) + total_len - 4, (char *)&r, sizeof(r));

	/* Turn off all the non-error bits in the rx status word. */
	r.aue_rxstat &= AUE_RXSTAT_MASK;

	if (r.aue_rxstat) {
		ifp->if_ierrors++;
		goto done;
	}

	/* No errors; receive the packet. */
	total_len -= (4 + ETHER_CRC_LEN);

	ifp->if_ipackets++;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = total_len;

	/* Put the packet on the special USB input queue. */
	usb_ether_input(m);
	aue_rxstart(ifp);
	return;
done:

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->aue_ep[AUE_ENDPT_RX],
	    c, mtod(c->aue_mbuf, char *), AUE_BUFSZ, USBD_SHORT_XFER_OK,
	    USBD_NO_TIMEOUT, aue_rxeof);
	usbd_transfer(xfer);
}

static void
aue_start_ipifunc(void *arg)
{
	struct ifnet *ifp = arg;
	struct lwkt_msg *lmsg = &ifp->if_start_nmsg[mycpuid].lmsg;

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg(ifnet_portfn(mycpuid), lmsg);
	crit_exit();
}

static void
aue_start_schedule(struct ifnet *ifp)
{
#ifdef SMP
        int cpu;

	cpu = ifp->if_start_cpuid(ifp);
	if (cpu != mycpuid)
		lwkt_send_ipiq(globaldata_find(cpu), aue_start_ipifunc, ifp);
	else
#endif
	aue_start_ipifunc(ifp);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
aue_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct aue_chain	*c = priv;
	struct aue_softc	*sc = c->aue_sc;
	struct ifnet		*ifp;
	usbd_status		err;

	ifp = &sc->arpcom.ac_if;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if_printf(ifp, "usb error on tx: %s\n", usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->aue_ep[AUE_ENDPT_TX]);
		return;
	}

	usbd_get_xfer_status(c->aue_xfer, NULL, NULL, NULL, &err);
	if (err)
		ifp->if_oerrors++;
	else
		ifp->if_opackets++;

	/* XXX should hold serializer */
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (!ifq_is_empty(&ifp->if_snd))
		aue_start_schedule(ifp);
}

static void
aue_tick(void *xsc)
{
	struct aue_softc	*sc = xsc;
	struct ifnet		*ifp;
	struct mii_data		*mii;

	if (sc == NULL)
		return;

	ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	mii = GET_MII(sc);
	if (mii == NULL) {
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	mii_tick(mii);
	if (!sc->aue_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->aue_link++;
		if (!ifq_is_empty(&ifp->if_snd))
			aue_start_schedule(ifp);
	}

	callout_reset(&sc->aue_stat_timer, hz, aue_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
aue_encap(struct aue_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct aue_chain	*c;
	usbd_status		err;

	c = &sc->aue_cdata.aue_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->aue_buf + 2);
	c->aue_mbuf = m;

	total_len = m->m_pkthdr.len + 2;

	/*
	 * The ADMtek documentation says that the packet length is
	 * supposed to be specified in the first two bytes of the
	 * transfer, however it actually seems to ignore this info
	 * and base the frame size on the bulk transfer length.
	 */
	c->aue_buf[0] = (u_int8_t)m->m_pkthdr.len;
	c->aue_buf[1] = (u_int8_t)(m->m_pkthdr.len >> 8);

	m_freem(c->aue_mbuf);
	c->aue_mbuf = NULL;
	m = NULL;

	usbd_setup_xfer(c->aue_xfer, sc->aue_ep[AUE_ENDPT_TX],
	    c, c->aue_buf, total_len, USBD_FORCE_SHORT_XFER,
	    10000, aue_txeof);

	/* Transmit */
	err = usbd_transfer(c->aue_xfer);
	if (err != USBD_IN_PROGRESS) {
		aue_stop(sc);
		return (EIO);
	}

	sc->aue_cdata.aue_tx_cnt++;

	return (0);
}

static void
aue_start(struct ifnet *ifp)
{
	struct aue_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	AUE_LOCK(sc);

	if (!sc->aue_link) {
		ifq_purge(&ifp->if_snd);
		AUE_UNLOCK(sc);
		return;
	}

	if ((ifp->if_flags & (IFF_OACTIVE | IFF_RUNNING)) != IFF_RUNNING) {
		AUE_UNLOCK(sc);
		return;
	}

	m_head = ifq_dequeue(&ifp->if_snd, NULL);
	if (m_head == NULL) {
		AUE_UNLOCK(sc);
		return;
	}

	if (aue_encap(sc, m_head, 0)) {
		/* aue_encap() will free m_head, if we reach here */
		ifp->if_flags |= IFF_OACTIVE;
		AUE_UNLOCK(sc);
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
	AUE_UNLOCK(sc);

	return;
}

static void
aue_init(void *xsc)
{
	struct aue_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = GET_MII(sc);
	struct aue_chain	*c;
	usbd_status		err;
	int			i;

	AUE_LOCK(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		AUE_UNLOCK(sc);
		return;
	}

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	aue_reset(sc);

	/* Set MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		aue_csr_write_1(sc, AUE_PAR0 + i, sc->arpcom.ac_enaddr[i]);

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		AUE_SETBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);
	else
		AUE_CLRBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);

	/* Init TX ring. */
	if (aue_tx_list_init(sc) == ENOBUFS) {
		if_printf(&sc->arpcom.ac_if, "tx list init failed\n");
		AUE_UNLOCK(sc);
		return;
	}

	/* Init RX ring. */
	if (aue_rx_list_init(sc) == ENOBUFS) {
		if_printf(&sc->arpcom.ac_if, "rx list init failed\n");
		AUE_UNLOCK(sc);
		return;
	}

#ifdef AUE_INTR_PIPE
	sc->aue_cdata.aue_ibuf = kmalloc(AUE_INTR_PKTLEN, M_USBDEV, M_WAITOK);
#endif

	/* Load the multicast filter. */
	aue_setmulti(sc);

	/* Enable RX and TX */
	aue_csr_write_1(sc, AUE_CTL0, AUE_CTL0_RXSTAT_APPEND | AUE_CTL0_RX_ENB);
	AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_TX_ENB);
	AUE_SETBIT(sc, AUE_CTL2, AUE_CTL2_EP3_CLR);

	mii_mediachg(mii);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->aue_iface, sc->aue_ed[AUE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->aue_ep[AUE_ENDPT_RX]);
	if (err) {
		if_printf(&sc->arpcom.ac_if, "open rx pipe failed: %s\n",
		    usbd_errstr(err));
		AUE_UNLOCK(sc);
		return;
	}
	err = usbd_open_pipe(sc->aue_iface, sc->aue_ed[AUE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->aue_ep[AUE_ENDPT_TX]);
	if (err) {
		if_printf(&sc->arpcom.ac_if, "open tx pipe failed: %s\n",
		    usbd_errstr(err));
		AUE_UNLOCK(sc);
		return;
	}

#ifdef AUE_INTR_PIPE
	err = usbd_open_pipe_intr(sc->aue_iface, sc->aue_ed[AUE_ENDPT_INTR],
	    USBD_SHORT_XFER_OK, &sc->aue_ep[AUE_ENDPT_INTR], sc,
	    sc->aue_cdata.aue_ibuf, AUE_INTR_PKTLEN, aue_intr,
	    AUE_INTR_INTERVAL);
	if (err) {
		if_printf(&sc->arpcom.ac_if, "open intr pipe failed: %s\n",
		    usbd_errstr(err));
		AUE_UNLOCK(sc);
		return;
	}
#endif

	/* Start up the receive pipe. */
	for (i = 0; i < AUE_RX_LIST_CNT; i++) {
		c = &sc->aue_cdata.aue_rx_chain[i];
		usbd_setup_xfer(c->aue_xfer, sc->aue_ep[AUE_ENDPT_RX],
		    c, mtod(c->aue_mbuf, char *), AUE_BUFSZ,
	    	USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, aue_rxeof);
		usbd_transfer(c->aue_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset(&sc->aue_stat_timer, hz, aue_tick, sc);

	AUE_UNLOCK(sc);

	return;
}

/*
 * Set media options.
 */
static int
aue_ifmedia_upd(struct ifnet *ifp)
{
	struct aue_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	sc->aue_link = 0;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
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
aue_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct aue_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

static int
aue_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct aue_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct mii_data		*mii;
	int			error = 0;

	AUE_LOCK(sc);

	switch(command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->aue_if_flags & IFF_PROMISC)) {
				AUE_SETBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->aue_if_flags & IFF_PROMISC) {
				AUE_CLRBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				aue_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				aue_stop(sc);
		}
		sc->aue_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		aue_setmulti(sc);
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

	AUE_UNLOCK(sc);

	return (error);
}

static void
aue_watchdog(struct ifnet *ifp)
{
	struct aue_softc	*sc = ifp->if_softc;
	struct aue_chain	*c;
	usbd_status		stat;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_oerrors++;
	if_printf(ifp, "watchdog timeout\n");

	c = &sc->aue_cdata.aue_tx_chain[0];
	usbd_get_xfer_status(c->aue_xfer, NULL, NULL, NULL, &stat);
	aue_txeof(c->aue_xfer, c, stat);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
aue_stop(struct aue_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	AUE_LOCK(sc);
	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	aue_csr_write_1(sc, AUE_CTL0, 0);
	aue_csr_write_1(sc, AUE_CTL1, 0);
	aue_reset(sc);
	callout_stop(&sc->aue_stat_timer);

	/* Stop transfers. */
	if (sc->aue_ep[AUE_ENDPT_RX] != NULL) {
		err = usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "abort rx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->aue_ep[AUE_ENDPT_RX]);
		if (err) {
			if_printf(ifp, "close rx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		sc->aue_ep[AUE_ENDPT_RX] = NULL;
	}

	if (sc->aue_ep[AUE_ENDPT_TX] != NULL) {
		err = usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "abort tx pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->aue_ep[AUE_ENDPT_TX]);
		if (err) {
			if_printf(ifp, "close tx pipe failed: %s\n",
			    usbd_errstr(err));
		}
		sc->aue_ep[AUE_ENDPT_TX] = NULL;
	}

#ifdef AUE_INTR_PIPE
	if (sc->aue_ep[AUE_ENDPT_INTR] != NULL) {
		err = usbd_abort_pipe(sc->aue_ep[AUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "abort intr pipe failed: %s\n",
		    	    usbd_errstr(err));
		}
		err = usbd_close_pipe(sc->aue_ep[AUE_ENDPT_INTR]);
		if (err) {
			if_printf(ifp, "close intr pipe failed: %s\n",
			    usbd_errstr(err));
		}
		sc->aue_ep[AUE_ENDPT_INTR] = NULL;
	}
#endif

	/* Free RX resources. */
	for (i = 0; i < AUE_RX_LIST_CNT; i++) {
		if (sc->aue_cdata.aue_rx_chain[i].aue_buf != NULL) {
			kfree(sc->aue_cdata.aue_rx_chain[i].aue_buf, M_USBDEV);
			sc->aue_cdata.aue_rx_chain[i].aue_buf = NULL;
		}
		if (sc->aue_cdata.aue_rx_chain[i].aue_mbuf != NULL) {
			m_freem(sc->aue_cdata.aue_rx_chain[i].aue_mbuf);
			sc->aue_cdata.aue_rx_chain[i].aue_mbuf = NULL;
		}
		if (sc->aue_cdata.aue_rx_chain[i].aue_xfer != NULL) {
			usbd_free_xfer(sc->aue_cdata.aue_rx_chain[i].aue_xfer);
			sc->aue_cdata.aue_rx_chain[i].aue_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < AUE_TX_LIST_CNT; i++) {
		if (sc->aue_cdata.aue_tx_chain[i].aue_buf != NULL) {
			kfree(sc->aue_cdata.aue_tx_chain[i].aue_buf, M_USBDEV);
			sc->aue_cdata.aue_tx_chain[i].aue_buf = NULL;
		}
		if (sc->aue_cdata.aue_tx_chain[i].aue_mbuf != NULL) {
			m_freem(sc->aue_cdata.aue_tx_chain[i].aue_mbuf);
			sc->aue_cdata.aue_tx_chain[i].aue_mbuf = NULL;
		}
		if (sc->aue_cdata.aue_tx_chain[i].aue_xfer != NULL) {
			usbd_free_xfer(sc->aue_cdata.aue_tx_chain[i].aue_xfer);
			sc->aue_cdata.aue_tx_chain[i].aue_xfer = NULL;
		}
	}

#ifdef AUE_INTR_PIPE
	if (sc->aue_cdata.aue_ibuf != NULL) {
		kfree(sc->aue_cdata.aue_ibuf, M_USBDEV);
		sc->aue_cdata.aue_ibuf = NULL;
	}
#endif

	sc->aue_link = 0;

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	AUE_UNLOCK(sc);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
aue_shutdown(device_t dev)
{
	struct aue_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	sc->aue_dying++;

	ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	aue_reset(sc);
	aue_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}
