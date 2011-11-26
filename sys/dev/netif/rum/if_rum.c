/*	$OpenBSD: if_rum.c,v 1.40 2006/09/18 16:20:20 damien Exp $	*/

/*-
 * Copyright (c) 2005, 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Niall O'Higgins <niallo@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Ralink Technology RT2501USB/RT2601USB chipset driver
 * http://www.ralinktech.com/
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>

#include "if_rumreg.h"
#include "if_rumvar.h"
#include "rum_ucode.h"

#ifdef USB_DEBUG
#define RUM_DEBUG
#endif

#ifdef RUM_DEBUG
#define DPRINTF(x)	do { if (rum_debug) kprintf x; } while (0)
#define DPRINTFN(n, x)	do { if (rum_debug >= (n)) kprintf x; } while (0)
int rum_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/* various supported device vendors/products */
static const struct usb_devno rum_devs[] = {
	{ USB_DEVICE(0x0411, 0x00d8) }, /* Melco WLI-U2-SG54HP */
	{ USB_DEVICE(0x0411, 0x00d9) }, /* Melco WLI-U2-G54HP */
	{ USB_DEVICE(0x050d, 0x705a) }, /* Belkin F5D7050A */
	{ USB_DEVICE(0x050d, 0x905b) }, /* Belkin F5D9050 ver3 */
	{ USB_DEVICE(0x0586, 0x3415) },	/* ZyXEL RT2573 */
	{ USB_DEVICE(0x06f8, 0xe010) }, /* Guillemot HWGUSB2-54-LB */
	{ USB_DEVICE(0x06f8, 0xe020) }, /* Guillemot HWGUSB2-54V2-AP */
	{ USB_DEVICE(0x0769, 0x31f3) }, /* Surecom RT2573 */
	{ USB_DEVICE(0x07b8, 0xb21b) }, /* AboCom HWU54DM */
	{ USB_DEVICE(0x07b8, 0xb21c) }, /* AboCom RT2573 */
	{ USB_DEVICE(0x07b8, 0xb21d) }, /* AboCom RT2573 */
	{ USB_DEVICE(0x07b8, 0xb21e) }, /* AboCom RT2573 */
	{ USB_DEVICE(0x07b8, 0xb21f) }, /* AboCom WUG2700 */
	{ USB_DEVICE(0x07d1, 0x3c03) }, /* D-Link DWL-G122 rev c1 */
	{ USB_DEVICE(0x07d1, 0x3c04) }, /* D-Link WUA-1340 */
	{ USB_DEVICE(0x0b05, 0x1723) }, /* Asus WL-167g */
	{ USB_DEVICE(0x0b05, 0x1724) }, /* Asus WL-167g */
	{ USB_DEVICE(0x0db0, 0x6874) }, /* MSI RT2573 */
	{ USB_DEVICE(0x0db0, 0x6877) }, /* MSI RT2573 */
	{ USB_DEVICE(0x0db0, 0xa861) }, /* MSI RT2573 */
	{ USB_DEVICE(0x0db0, 0xa874) }, /* MSI RT2573 */
	{ USB_DEVICE(0x0df6, 0x90ac) }, /* Sitecom WL-172 */
	{ USB_DEVICE(0x0df6, 0x9712) }, /* Sitecom WL-113 rev 2 */
	{ USB_DEVICE(0x0eb0, 0x9021) }, /* Nova Technology RT2573 */
	{ USB_DEVICE(0x1044, 0x8008) }, /* GIGABYTE GN-WB01GS */
	{ USB_DEVICE(0x1044, 0x800a) }, /* GIGABYTE GN-WI05GS */
	{ USB_DEVICE(0x1371, 0x9022) }, /* (really) C-Net RT2573 */
	{ USB_DEVICE(0x1371, 0x9032) }, /* (really) C-Net CWD854F */
	{ USB_DEVICE(0x13b1, 0x0020) }, /* Cisco-Linksys WUSB54GC */
	{ USB_DEVICE(0x13b1, 0x0023) },	/* Cisco-Linksys WUSB54GR */
	{ USB_DEVICE(0x1472, 0x0009) }, /* Huawei RT2573 */
	{ USB_DEVICE(0x148f, 0x2573) }, /* Ralink RT2573 */
	{ USB_DEVICE(0x148f, 0x2671) }, /* Ralink RT2671 */
	{ USB_DEVICE(0x148f, 0x9021) }, /* Ralink RT2573 */
	{ USB_DEVICE(0x14b2, 0x3c22) }, /* Conceptronic C54RU */
	{ USB_DEVICE(0x15a9, 0x0004) }, /* SparkLan RT2573 */
	{ USB_DEVICE(0x1631, 0xc019) }, /* Good Way Technology RT2573 */
	{ USB_DEVICE(0x1690, 0x0722) }, /* Gigaset RT2573 */
	{ USB_DEVICE(0x1737, 0x0020) }, /* Linksys WUSB54GC */
	{ USB_DEVICE(0x1737, 0x0023) }, /* Linksys WUSB54GR */
	{ USB_DEVICE(0x18c5, 0x0002) }, /* AMIT CG-WLUSB2GO */
	{ USB_DEVICE(0x18e8, 0x6196) }, /* Qcom RT2573 */
	{ USB_DEVICE(0x18e8, 0x6229) }, /* Qcom RT2573 */
	{ USB_DEVICE(0x18e8, 0x6238) },	/* Qcom RT2573 */
	{ USB_DEVICE(0x2019, 0xab01) }, /* Planex GW-US54HP */
	{ USB_DEVICE(0x2019, 0xab50) }, /* Planex GW-US54Mini2 */
	{ USB_DEVICE(0x2019, 0xed02) }, /* Planex GW-USMM */
};

static int		rum_alloc_tx_list(struct rum_softc *);
static void		rum_free_tx_list(struct rum_softc *);
static int		rum_alloc_rx_list(struct rum_softc *);
static void		rum_free_rx_list(struct rum_softc *);
static int		rum_media_change(struct ifnet *);
static void		rum_next_scan(void *);
static void		rum_task(void *);
static int		rum_newstate(struct ieee80211com *,
			    enum ieee80211_state, int);
static void		rum_txeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
static void		rum_rxeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
static uint8_t		rum_rxrate(struct rum_rx_desc *);
static uint8_t		rum_plcp_signal(int);
static void		rum_setup_tx_desc(struct rum_softc *,
			    struct rum_tx_desc *, uint32_t, uint16_t, int,
			    int);
static int		rum_tx_data(struct rum_softc *, struct mbuf *,
			    struct ieee80211_node *);
static void		rum_start(struct ifnet *);
static void		rum_watchdog(struct ifnet *);
static int		rum_ioctl(struct ifnet *, u_long, caddr_t,
				  struct ucred *);
static void		rum_eeprom_read(struct rum_softc *, uint16_t, void *,
			    int);
static uint32_t		rum_read(struct rum_softc *, uint16_t);
static void		rum_read_multi(struct rum_softc *, uint16_t, void *,
			    int);
static void		rum_write(struct rum_softc *, uint16_t, uint32_t);
static void		rum_write_multi(struct rum_softc *, uint16_t, void *,
			    size_t);
static void		rum_bbp_write(struct rum_softc *, uint8_t, uint8_t);
static uint8_t		rum_bbp_read(struct rum_softc *, uint8_t);
static void		rum_rf_write(struct rum_softc *, uint8_t, uint32_t);
static void		rum_select_antenna(struct rum_softc *);
static void		rum_enable_mrr(struct rum_softc *);
static void		rum_set_txpreamble(struct rum_softc *);
static void		rum_set_basicrates(struct rum_softc *);
static void		rum_select_band(struct rum_softc *,
			    struct ieee80211_channel *);
static void		rum_set_chan(struct rum_softc *,
			    struct ieee80211_channel *);
static void		rum_enable_tsf_sync(struct rum_softc *);
static void		rum_update_slot(struct rum_softc *);
static void		rum_set_bssid(struct rum_softc *, const uint8_t *);
static void		rum_set_macaddr(struct rum_softc *, const uint8_t *);
static void		rum_update_promisc(struct rum_softc *);
static const char	*rum_get_rf(int);
static void		rum_read_eeprom(struct rum_softc *);
static int		rum_bbp_init(struct rum_softc *);
static void		rum_init(void *);
static void		rum_stop(struct rum_softc *);
static int		rum_load_microcode(struct rum_softc *, const uint8_t *,
			    size_t);
static int		rum_prepare_beacon(struct rum_softc *);

static void		rum_stats_timeout(void *);
static void		rum_stats_update(usbd_xfer_handle, usbd_private_handle,
					 usbd_status);
static void		rum_stats(struct ieee80211com *,
				  struct ieee80211_node *,
				  struct ieee80211_ratectl_stats *);
static void		*rum_ratectl_attach(struct ieee80211com *, u_int);
static int		rum_get_rssi(struct rum_softc *, uint8_t);

/*
 * Supported rates for 802.11a/b/g modes (in 500Kbps unit).
 */
static const struct ieee80211_rateset rum_rateset_11a =
	{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };

static const struct ieee80211_rateset rum_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

static const struct ieee80211_rateset rum_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rum_def_mac[] = {
	RT2573_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rum_def_bbp[] = {
	RT2573_DEF_BBP
};

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
}  rum_rf5226[] = {
	RT2573_RF5226
}, rum_rf5225[] = {
	RT2573_RF5225
};

static device_probe_t rum_match;
static device_attach_t rum_attach;
static device_detach_t rum_detach;

static devclass_t rum_devclass;

static kobj_method_t rum_methods[] = {
	DEVMETHOD(device_probe, rum_match),
	DEVMETHOD(device_attach, rum_attach),
	DEVMETHOD(device_detach, rum_detach),
	{0,0}
};

static driver_t rum_driver = {
	"rum",
	rum_methods,
	sizeof(struct rum_softc)
};

DRIVER_MODULE(rum, uhub, rum_driver, rum_devclass, usbd_driver_load, NULL);

MODULE_DEPEND(rum, usb, 1, 1, 1);
MODULE_DEPEND(rum, wlan, 1, 1, 1);
MODULE_DEPEND(rum, wlan_ratectl_onoe, 1, 1, 1);

static int
rum_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (usb_lookup(rum_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
rum_attach(device_t self)
{
	struct rum_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i, ntries;
	uint32_t tmp;

	sc->sc_udev = uaa->device;
	sc->sc_dev = self;

	if (usbd_set_config_no(sc->sc_udev, RT2573_CONFIG_NO, 0) != 0) {
		kprintf("%s: could not set configuration no\n",
		    device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, RT2573_IFACE_INDEX,
	    &sc->sc_iface);
	if (error != 0) {
		kprintf("%s: could not get interface handle\n",
		    device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	/*
	 * Find endpoints.
	 */
	id = usbd_get_interface_descriptor(sc->sc_iface);

	sc->sc_rx_no = sc->sc_tx_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			kprintf("%s: no endpoint descriptor for iface %d\n",
			    device_get_nameunit(sc->sc_dev), i);
			return ENXIO;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_rx_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_tx_no = ed->bEndpointAddress;
	}
	if (sc->sc_rx_no == -1 || sc->sc_tx_no == -1) {
		kprintf("%s: missing endpoint\n", device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	usb_init_task(&sc->sc_task, rum_task, sc);

	callout_init(&sc->scan_ch);
	callout_init(&sc->stats_ch);

	/* retrieve RT2573 rev. no */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((tmp = rum_read(sc, RT2573_MAC_CSR0)) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		kprintf("%s: timeout waiting for chip to settle\n",
		    device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	/* retrieve MAC address and various other things from EEPROM */
	rum_read_eeprom(sc);

	kprintf("%s: MAC/BBP RT%04x (rev 0x%05x), RF %s, address %6D\n",
	    device_get_nameunit(sc->sc_dev), sc->macbbp_rev, tmp,
	    rum_get_rf(sc->rf_rev), ic->ic_myaddr, ":");

	error = rum_load_microcode(sc, rt2573, sizeof(rt2573));
	if (error != 0) {
		device_printf(self, "can't load microcode\n");
		return ENXIO;
	}

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_HOSTAP |	/* HostAp mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WPA;		/* WPA 1+2 */

	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_5226) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] = rum_rateset_11a;

		/* set supported .11a channels */
		for (i = 34; i <= 46; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
		for (i = 36; i <= 64; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
		for (i = 100; i <= 140; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
		for (i = 149; i <= 165; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
	}

	/* set supported .11b and .11g rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = rum_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = rum_rateset_11g;

	/* set supported .11b and .11g channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	sc->sc_sifs = IEEE80211_DUR_SIFS;	/* Default SIFS */

	if_initname(ifp, device_get_name(self), device_get_unit(self));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = rum_init;
	ifp->if_ioctl = rum_ioctl;
	ifp->if_start = rum_start;
	ifp->if_watchdog = rum_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	IEEE80211_ONOE_PARAM_SETUP(&sc->sc_onoe_param);
	sc->sc_onoe_param.onoe_raise = 15;
	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_ONOE;
	ic->ic_ratectl.rc_st_stats = rum_stats;
	ic->ic_ratectl.rc_st_attach = rum_ratectl_attach;

	ieee80211_ifattach(ic);

	/* Enable software beacon missing handling. */
	ic->ic_flags_ext |= IEEE80211_FEXT_SWBMISS;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rum_newstate;
	ieee80211_media_init(ic, rum_media_change, ieee80211_media_status);

	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN,
	    &sc->sc_drvbpf);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RT2573_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RT2573_TX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;
}

static int
rum_detach(device_t self)
{
	struct rum_softc *sc = device_get_softc(self);
	struct ifnet *ifp = &sc->sc_ic.ic_if;
#ifdef INVARIANTS
	int i;
#endif

	crit_enter();

	callout_stop(&sc->scan_ch);
	callout_stop(&sc->stats_ch);

	lwkt_serialize_enter(ifp->if_serializer);
	rum_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	usb_rem_task(sc->sc_udev, &sc->sc_task);

	bpfdetach(ifp);
	ieee80211_ifdetach(&sc->sc_ic);	/* free all nodes */

	crit_exit();

	KKASSERT(sc->stats_xfer == NULL);
	KKASSERT(sc->sc_rx_pipeh == NULL);
	KKASSERT(sc->sc_tx_pipeh == NULL);

#ifdef INVARIANTS
	/*
	 * Make sure TX/RX list is empty
	 */
	for (i = 0; i < RT2573_TX_LIST_COUNT; i++) {
		struct rum_tx_data *data = &sc->tx_data[i];

		KKASSERT(data->xfer == NULL);
		KKASSERT(data->ni == NULL);
		KKASSERT(data->m == NULL);
	}
	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

		KKASSERT(data->xfer == NULL);
		KKASSERT(data->m == NULL);
	}
#endif
	return 0;
}

static int
rum_alloc_tx_list(struct rum_softc *sc)
{
	int i;

	sc->tx_queued = 0;
	for (i = 0; i < RT2573_TX_LIST_COUNT; i++) {
		struct rum_tx_data *data = &sc->tx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			kprintf("%s: could not allocate tx xfer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		data->buf = usbd_alloc_buffer(data->xfer,
		    RT2573_TX_DESC_SIZE + IEEE80211_MAX_LEN);
		if (data->buf == NULL) {
			kprintf("%s: could not allocate tx buffer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		/* clean Tx descriptor */
		bzero(data->buf, RT2573_TX_DESC_SIZE);
	}
	return 0;
}

static void
rum_free_tx_list(struct rum_softc *sc)
{
	int i;

	for (i = 0; i < RT2573_TX_LIST_COUNT; i++) {
		struct rum_tx_data *data = &sc->tx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
	}
	sc->tx_queued = 0;
}

static int
rum_alloc_rx_list(struct rum_softc *sc)
{
	int i;

	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			kprintf("%s: could not allocate rx xfer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		if (usbd_alloc_buffer(data->xfer, MCLBYTES) == NULL) {
			kprintf("%s: could not allocate rx buffer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		data->m = m_getcl(MB_WAIT, MT_DATA, M_PKTHDR);

		data->buf = mtod(data->m, uint8_t *);
		bzero(data->buf, sizeof(struct rum_rx_desc));
	}
	return 0;
}

static void
rum_free_rx_list(struct rum_softc *sc)
{
	int i;

	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
	}
}

static int
rum_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		rum_init(ifp->if_softc);

	return 0;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
static void
rum_next_scan(void *arg)
{
	struct rum_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (sc->sc_stopped)
		return;

	crit_enter();

	if (ic->ic_state == IEEE80211_S_SCAN) {
		lwkt_serialize_enter(ifp->if_serializer);
		ieee80211_next_scan(ic);
		lwkt_serialize_exit(ifp->if_serializer);
	}

	crit_exit();
}

static void
rum_task(void *xarg)
{
	struct rum_softc *sc = xarg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	enum ieee80211_state nstate;
	struct ieee80211_node *ni;
	int arg;

	if (sc->sc_stopped)
		return;

	crit_enter();

	nstate = sc->sc_state;
	arg = sc->sc_arg;

	KASSERT(nstate != IEEE80211_S_INIT,
		("->INIT state transition should not be defered\n"));
	rum_set_chan(sc, ic->ic_curchan);

	switch (nstate) {
	case IEEE80211_S_RUN:
		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rum_update_slot(sc);
			rum_enable_mrr(sc);
			rum_set_txpreamble(sc);
			rum_set_basicrates(sc);
			rum_set_bssid(sc, ni->ni_bssid);
		}

		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS)
			rum_prepare_beacon(sc);

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			rum_enable_tsf_sync(sc);

		/* clear statistic registers (STA_CSR0 to STA_CSR5) */
		rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof(sc->sta));
		callout_reset(&sc->stats_ch, 4 * hz / 5, rum_stats_timeout, sc);
		break;

	case IEEE80211_S_SCAN:
		callout_reset(&sc->scan_ch, hz / 5, rum_next_scan, sc);
		break;

	default:
		break;
	}

	lwkt_serialize_enter(ifp->if_serializer);
	ieee80211_ratectl_newstate(ic, nstate);
	sc->sc_newstate(ic, nstate, arg);
	lwkt_serialize_exit(ifp->if_serializer);

	crit_exit();
}

static int
rum_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rum_softc *sc = ic->ic_if.if_softc;
	struct ifnet *ifp = &ic->ic_if;

	crit_enter();

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->scan_ch);
	callout_stop(&sc->stats_ch);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;

	lwkt_serialize_exit(ifp->if_serializer);
	usb_rem_task(sc->sc_udev, &sc->sc_task);

	if (nstate == IEEE80211_S_INIT) {
		lwkt_serialize_enter(ifp->if_serializer);
		ieee80211_ratectl_newstate(ic, nstate);
		sc->sc_newstate(ic, nstate, arg);
	} else {
		usb_add_task(sc->sc_udev, &sc->sc_task, USB_TASKQ_DRIVER);
		lwkt_serialize_enter(ifp->if_serializer);
	}

	crit_exit();
	return 0;
}

/* quickly determine if a given rate is CCK or OFDM */
#define RUM_RATE_IS_OFDM(rate)	((rate) >= 12 && (rate) != 22)

#define RUM_ACK_SIZE	(sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN)

static void
rum_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rum_tx_data *data = priv;
	struct rum_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;

	if (sc->sc_stopped)
		return;

	crit_enter();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			crit_exit();
			return;
		}

		kprintf("%s: could not transmit buffer: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tx_pipeh);

		ifp->if_oerrors++;
		crit_exit();
		return;
	}

	m_freem(data->m);
	data->m = NULL;
	ni = data->ni;
	data->ni = NULL;

	bzero(data->buf, sizeof(struct rum_tx_data));
	sc->tx_queued--;
	ifp->if_opackets++;	/* XXX may fail too */

	DPRINTFN(10, ("tx done\n"));

	sc->sc_tx_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;

	lwkt_serialize_enter(ifp->if_serializer);
	ieee80211_free_node(ni);
	ifp->if_start(ifp);
	lwkt_serialize_exit(ifp->if_serializer);

	crit_exit();
}

static void
rum_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rum_rx_data *data = priv;
	struct rum_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rum_rx_desc *desc;
	struct ieee80211_frame_min *wh;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int len, rssi;

	if (sc->sc_stopped)
		return;

	crit_enter();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			crit_exit();
			return;
		}

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipeh);
		goto skip;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (len < RT2573_RX_DESC_SIZE + sizeof(struct ieee80211_frame_min)) {
		DPRINTF(("%s: xfer too short %d\n", device_get_nameunit(sc->sc_dev),
		    len));
		ifp->if_ierrors++;
		goto skip;
	}

	desc = (struct rum_rx_desc *)data->buf;

	if (le32toh(desc->flags) & RT2573_RX_CRC_ERROR) {
		/*
		 * This should not happen since we did not request to receive
		 * those frames when we filled RT2573_TXRX_CSR0.
		 */
		DPRINTFN(5, ("CRC error\n"));
		ifp->if_ierrors++;
		goto skip;
	}

	mnew = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (mnew == NULL) {
		kprintf("%s: could not allocate rx mbuf\n",
		    device_get_nameunit(sc->sc_dev));
		ifp->if_ierrors++;
		goto skip;
	}

	m = data->m;
	data->m = NULL;
	data->buf = NULL;

	lwkt_serialize_enter(ifp->if_serializer);

	/* finalize mbuf */
	m->m_pkthdr.rcvif = ifp;
	m->m_data = (caddr_t)(desc + 1);
	m->m_pkthdr.len = m->m_len = (le32toh(desc->flags) >> 16) & 0xfff;

	rssi = rum_get_rssi(sc, desc->rssi);

	wh = mtod(m, struct ieee80211_frame_min *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* Error happened during RSSI conversion. */
	if (rssi < 0)
		rssi = ni->ni_rssi;

	if (sc->sc_drvbpf != NULL) {
		struct rum_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_rate = rum_rxrate(desc);
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_antenna = sc->rx_ant;
		tap->wr_antsignal = rssi;

		bpf_ptap(sc->sc_drvbpf, m, tap, sc->sc_rxtap_len);
	}

	/* send the frame to the 802.11 layer */
	ieee80211_input(ic, m, ni, rssi, 0);

	/* node is no longer needed */
	ieee80211_free_node(ni);

	if ((ifp->if_flags & IFF_OACTIVE) == 0)
		ifp->if_start(ifp);

	lwkt_serialize_exit(ifp->if_serializer);

	data->m = mnew;
	data->buf = mtod(data->m, uint8_t *);

	DPRINTFN(15, ("rx done\n"));

skip:	/* setup a new transfer */
	bzero(data->buf, sizeof(struct rum_rx_desc));
	usbd_setup_xfer(xfer, sc->sc_rx_pipeh, data, data->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rum_rxeof);
	usbd_transfer(xfer);

	crit_exit();
}

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
static uint8_t
rum_rxrate(struct rum_rx_desc *desc)
{
	if (le32toh(desc->flags) & RT2573_RX_OFDM) {
		/* reverse function of rum_plcp_signal */
		switch (desc->rate) {
		case 0xb:	return 12;
		case 0xf:	return 18;
		case 0xa:	return 24;
		case 0xe:	return 36;
		case 0x9:	return 48;
		case 0xd:	return 72;
		case 0x8:	return 96;
		case 0xc:	return 108;
		}
	} else {
		if (desc->rate == 10)
			return 2;
		if (desc->rate == 20)
			return 4;
		if (desc->rate == 55)
			return 11;
		if (desc->rate == 110)
			return 22;
	}
	return 2;	/* should not get there */
}

static uint8_t
rum_plcp_signal(int rate)
{
	switch (rate) {
	/* CCK rates (returned values are device-dependent) */
	case 2:		return 0x0;
	case 4:		return 0x1;
	case 11:	return 0x2;
	case 22:	return 0x3;

	/* OFDM rates (cf IEEE Std 802.11a-1999, pp. 14 Table 80) */
	case 12:	return 0xb;
	case 18:	return 0xf;
	case 24:	return 0xa;
	case 36:	return 0xe;
	case 48:	return 0x9;
	case 72:	return 0xd;
	case 96:	return 0x8;
	case 108:	return 0xc;

	/* unsupported rates (should not get there) */
	default:	return 0xff;
	}
}

static void
rum_setup_tx_desc(struct rum_softc *sc, struct rum_tx_desc *desc,
    uint32_t flags, uint16_t xflags, int len, int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(len << 16);

	desc->xflags = htole16(xflags);

	desc->wme = htole16(
	    RT2573_QID(0) |
	    RT2573_AIFSN(2) |
	    RT2573_LOGCWMIN(4) |
	    RT2573_LOGCWMAX(10));

	/* setup PLCP fields */
	desc->plcp_signal  = rum_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RUM_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RT2573_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RT2573_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}
	desc->flags |= htole32(RT2573_TX_VALID);
}

#define RUM_TX_TIMEOUT	5000

static int
rum_tx_data(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rum_tx_desc *desc;
	struct rum_tx_data *data;
	struct ieee80211_frame *wh;
	uint32_t flags = 0;
	uint16_t dur;
	usbd_status error;
	int xferlen, rate, rateidx;

	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		if (ieee80211_crypto_encap(ic, ni, m0) == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	/* pickup a rate */
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_MGT) {
		/* mgmt frames are sent at the lowest available bit-rate */
		rateidx = 0;
	} else {
		ieee80211_ratectl_findrate(ni, m0->m_pkthdr.len, &rateidx, 1);
	}
	rate = IEEE80211_RS_RATE(&ni->ni_rates, rateidx);

	data = &sc->tx_data[0];
	desc = (struct rum_tx_desc *)data->buf;

	data->m = m0;
	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2573_TX_ACK;

		dur = ieee80211_txtime(ni, RUM_ACK_SIZE,
			ieee80211_ack_rate(ni, rate), ic->ic_flags) +
			sc->sc_sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);

		/* tell hardware to set timestamp in probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RT2573_TX_TIMESTAMP;
	}

	if (sc->sc_drvbpf != NULL) {
		struct rum_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		bpf_ptap(sc->sc_drvbpf, m0, tap, sc->sc_txtap_len);
	}

	m_copydata(m0, 0, m0->m_pkthdr.len, data->buf + RT2573_TX_DESC_SIZE);
	rum_setup_tx_desc(sc, desc, flags, 0, m0->m_pkthdr.len, rate);

	/* Align end on a 4-bytes boundary */
	xferlen = roundup(RT2573_TX_DESC_SIZE + m0->m_pkthdr.len, 4);

	/*
	 * No space left in the last URB to store the extra 4 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 4;

	DPRINTFN(10, ("sending frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len + RT2573_TX_DESC_SIZE, rate, xferlen));

	lwkt_serialize_exit(ifp->if_serializer);

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RUM_TX_TIMEOUT, rum_txeof);

	error = usbd_transfer(data->xfer);
	if (error != USBD_NORMAL_COMPLETION && error != USBD_IN_PROGRESS) {
		m_freem(m0);
		data->m = NULL;
		data->ni = NULL;
	} else {
		sc->tx_queued++;
		error = 0;
	}

	lwkt_serialize_enter(ifp->if_serializer);
	return error;
}

static void
rum_start(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_stopped) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	crit_enter();

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING) {
		crit_exit();
		return;
	}

	for (;;) {
		struct ieee80211_node *ni;
		struct mbuf *m0;

		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			if (sc->tx_queued >= RT2573_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			IF_DEQUEUE(&ic->ic_mgtq, m0);

			ni = (struct ieee80211_node *)m0->m_pkthdr.rcvif;
			m0->m_pkthdr.rcvif = NULL;

			BPF_MTAP(ifp, m0);

			if (rum_tx_data(sc, m0, ni) != 0) {
				ieee80211_free_node(ni);
				break;
			}
		} else {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN) {
				ifq_purge(&ifp->if_snd);
				break;
			}

			if (sc->tx_queued >= RT2573_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}

			m0 = ifq_dequeue(&ifp->if_snd, NULL);
			if (m0 == NULL)
				break;

			if (m0->m_len < sizeof(struct ether_header)) {
				m0 = m_pullup(m0, sizeof(struct ether_header));
				if (m0 == NULL) {
					ifp->if_oerrors++;
					continue;
				}
			}
			eh = mtod(m0, struct ether_header *);

			ni = ieee80211_find_txnode(ic, eh->ether_dhost);
			if (ni == NULL) {
				m_freem(m0);
				continue;
			}

			BPF_MTAP(ifp, m0);

			m0 = ieee80211_encap(ic, m0, ni);
			if (m0 == NULL) {
				ieee80211_free_node(ni);
				continue;
			}

			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0);

			if (rum_tx_data(sc, m0, ni) != 0) {
				ieee80211_free_node(ni);
				ifp->if_oerrors++;
				break;
			}
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}

	crit_exit();
}

static void
rum_watchdog(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			kprintf("%s: device timeout\n", device_get_nameunit(sc->sc_dev));
			/*rum_init(sc); XXX needs a process context! */
			ifp->if_oerrors++;

			crit_exit();
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(&sc->sc_ic);

	crit_exit();
}

static int
rum_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				lwkt_serialize_exit(ifp->if_serializer);
				rum_update_promisc(sc);
				lwkt_serialize_enter(ifp->if_serializer);
			} else {
				rum_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rum_stop(sc);
		}
		break;
	default:
		error = ieee80211_ioctl(ic, cmd, data, cr);
		break;
	}

	if (error == ENETRESET) {
		struct ieee80211req *ireq = (struct ieee80211req *)data;

		if (cmd == SIOCS80211 &&
		    ireq->i_type == IEEE80211_IOC_CHANNEL &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			/*
			 * This allows for fast channel switching in monitor
			 * mode (used by kismet). In IBSS mode, we must
			 * explicitly reset the interface to generate a new
			 * beacon frame.
			 */
			lwkt_serialize_exit(ifp->if_serializer);
			rum_set_chan(sc, ic->ic_ibss_chan);
			lwkt_serialize_enter(ifp->if_serializer);
		} else if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			   (IFF_UP | IFF_RUNNING)) {
			rum_init(sc);
		}
		error = 0;
	}

	crit_exit();
	return error;
}

static void
rum_eeprom_read(struct rum_softc *sc, uint16_t addr, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_EEPROM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not read EEPROM: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static uint32_t
rum_read(struct rum_softc *sc, uint16_t reg)
{
	uint32_t val;

	rum_read_multi(sc, reg, &val, sizeof val);

	return le32toh(val);
}

static void
rum_read_multi(struct rum_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not multi read MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
rum_write(struct rum_softc *sc, uint16_t reg, uint32_t val)
{
	uint32_t tmp = htole32(val);

	rum_write_multi(sc, reg, &tmp, sizeof tmp);
}

static void
rum_write_multi(struct rum_softc *sc, uint16_t reg, void *buf, size_t len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_WRITE_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not multi write MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
rum_bbp_write(struct rum_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to BBP\n", device_get_nameunit(sc->sc_dev));
		return;
	}

	tmp = RT2573_BBP_BUSY | (reg & 0x7f) << 8 | val;
	rum_write(sc, RT2573_PHY_CSR3, tmp);
}

static uint8_t
rum_bbp_read(struct rum_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not read BBP\n", device_get_nameunit(sc->sc_dev));
		return 0;
	}

	val = RT2573_BBP_BUSY | RT2573_BBP_READ | reg << 8;
	rum_write(sc, RT2573_PHY_CSR3, val);

	for (ntries = 0; ntries < 100; ntries++) {
		val = rum_read(sc, RT2573_PHY_CSR3);
		if (!(val & RT2573_BBP_BUSY))
			return val & 0xff;
		DELAY(1);
	}

	kprintf("%s: could not read BBP\n", device_get_nameunit(sc->sc_dev));
	return 0;
}

static void
rum_rf_write(struct rum_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR4) & RT2573_RF_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to RF\n", device_get_nameunit(sc->sc_dev));
		return;
	}

	tmp = RT2573_RF_BUSY | RT2573_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 3);
	rum_write(sc, RT2573_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 3, val & 0xfffff));
}

static void
rum_select_antenna(struct rum_softc *sc)
{
	uint8_t bbp4, bbp77;
	uint32_t tmp;

	bbp4  = rum_bbp_read(sc, 4);
	bbp77 = rum_bbp_read(sc, 77);

	/* TBD */

	/* make sure Rx is disabled before switching antenna */
	tmp = rum_read(sc, RT2573_TXRX_CSR0);
	rum_write(sc, RT2573_TXRX_CSR0, tmp | RT2573_DISABLE_RX);

	rum_bbp_write(sc,  4, bbp4);
	rum_bbp_write(sc, 77, bbp77);

	rum_write(sc, RT2573_TXRX_CSR0, tmp);
}

/*
 * Enable multi-rate retries for frames sent at OFDM rates.
 * In 802.11b/g mode, allow fallback to CCK rates.
 */
static void
rum_enable_mrr(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR4);

	tmp &= ~RT2573_MRR_CCK_FALLBACK;
	if (!IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan))
		tmp |= RT2573_MRR_CCK_FALLBACK;
	tmp |= RT2573_MRR_ENABLED;

	rum_write(sc, RT2573_TXRX_CSR4, tmp);
}

static void
rum_set_txpreamble(struct rum_softc *sc)
{
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR4);

	tmp &= ~RT2573_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2573_SHORT_PREAMBLE;

	rum_write(sc, RT2573_TXRX_CSR4, tmp);
}

static void
rum_set_basicrates(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x3);
	} else if (IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan)) {
		/* 11a basic rates: 6, 12, 24Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x150);
	} else {
		/* 11g basic rates: 1, 2, 5.5, 11, 6, 12, 24Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x15f);
	}
}

/*
 * Reprogram MAC/BBP to switch to a new band.  Values taken from the reference
 * driver.
 */
static void
rum_select_band(struct rum_softc *sc, struct ieee80211_channel *c)
{
	uint8_t bbp17, bbp35, bbp96, bbp97, bbp98, bbp104;
	uint32_t tmp;

	/* update all BBP registers that depend on the band */
	bbp17 = 0x20; bbp96 = 0x48; bbp104 = 0x2c;
	bbp35 = 0x50; bbp97 = 0x48; bbp98  = 0x48;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		bbp17 += 0x08; bbp96 += 0x10; bbp104 += 0x0c;
		bbp35 += 0x10; bbp97 += 0x10; bbp98  += 0x10;
	}
	if ((IEEE80211_IS_CHAN_2GHZ(c) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(c) && sc->ext_5ghz_lna)) {
		bbp17 += 0x10; bbp96 += 0x10; bbp104 += 0x10;
	}

	sc->bbp17 = bbp17;
	rum_bbp_write(sc,  17, bbp17);
	rum_bbp_write(sc,  96, bbp96);
	rum_bbp_write(sc, 104, bbp104);

	if ((IEEE80211_IS_CHAN_2GHZ(c) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(c) && sc->ext_5ghz_lna)) {
		rum_bbp_write(sc, 75, 0x80);
		rum_bbp_write(sc, 86, 0x80);
		rum_bbp_write(sc, 88, 0x80);
	}

	rum_bbp_write(sc, 35, bbp35);
	rum_bbp_write(sc, 97, bbp97);
	rum_bbp_write(sc, 98, bbp98);

	tmp = rum_read(sc, RT2573_PHY_CSR0);
	tmp &= ~(RT2573_PA_PE_2GHZ | RT2573_PA_PE_5GHZ);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		tmp |= RT2573_PA_PE_2GHZ;
	else
		tmp |= RT2573_PA_PE_5GHZ;
	rum_write(sc, RT2573_PHY_CSR0, tmp);
}

static void
rum_set_chan(struct rum_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2573_BBPR94_DEFAULT;
	int8_t power;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	/* select the appropriate RF settings based on what EEPROM says */
	rfprog = (sc->rf_rev == RT2573_RF_5225 ||
		  sc->rf_rev == RT2573_RF_2527) ? rum_rf5225 : rum_rf5226;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++)
		;	/* EMPTY */

	power = sc->txpow[i];
	if (power < 0) {
		bbp94 += power;
		power = 0;
	} else if (power > 31) {
		bbp94 += power - 31;
		power = 31;
	}

	/*
	 * If we are switching from the 2GHz band to the 5GHz band or
	 * vice-versa, BBP registers need to be reprogrammed.
	 */
	if (c->ic_flags != sc->sc_curchan->ic_flags) {
		rum_select_band(sc, c);
		rum_select_antenna(sc);
	}
	sc->sc_curchan = c;

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7 | 1);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	DELAY(10);

	/* enable smart mode for MIMO-capable RFs */
	bbp3 = rum_bbp_read(sc, 3);

	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_2527)
		bbp3 &= ~RT2573_SMART_MODE;
	else
		bbp3 |= RT2573_SMART_MODE;

	rum_bbp_write(sc, 3, bbp3);

	if (bbp94 != RT2573_BBPR94_DEFAULT)
		rum_bbp_write(sc, 94, bbp94);

	sc->sc_sifs = IEEE80211_IS_CHAN_5GHZ(c) ? IEEE80211_DUR_OFDM_SIFS
						: IEEE80211_DUR_SIFS;
}

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
static void
rum_enable_tsf_sync(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	if (ic->ic_opmode != IEEE80211_M_STA) {
		/*
		 * Change default 16ms TBTT adjustment to 8ms.
		 * Must be done before enabling beacon generation.
		 */
		rum_write(sc, RT2573_TXRX_CSR10, 1 << 12 | 8);
	}

	tmp = rum_read(sc, RT2573_TXRX_CSR9) & 0xff000000;

	/* set beacon interval (in 1/16ms unit) */
	tmp |= ic->ic_bss->ni_intval * 16;

	tmp |= RT2573_TSF_TICKING | RT2573_ENABLE_TBTT;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RT2573_TSF_MODE(1);
	else
		tmp |= RT2573_TSF_MODE(2) | RT2573_GENERATE_BEACON;

	rum_write(sc, RT2573_TXRX_CSR9, tmp);
}

static void
rum_update_slot(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;
	uint32_t tmp;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ? 9 : 20;

	tmp = rum_read(sc, RT2573_MAC_CSR9);
	tmp = (tmp & ~0xff) | slottime;
	rum_write(sc, RT2573_MAC_CSR9, tmp);

	DPRINTF(("setting slot time to %uus\n", slottime));
}

static void
rum_set_bssid(struct rum_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	rum_write(sc, RT2573_MAC_CSR4, tmp);

	tmp = bssid[4] | bssid[5] << 8 | RT2573_ONE_BSSID << 16;
	rum_write(sc, RT2573_MAC_CSR5, tmp);
}

static void
rum_set_macaddr(struct rum_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	rum_write(sc, RT2573_MAC_CSR2, tmp);

	tmp = addr[4] | addr[5] << 8 | 0xff << 16;
	rum_write(sc, RT2573_MAC_CSR3, tmp);
}

static void
rum_update_promisc(struct rum_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR0);

	tmp &= ~RT2573_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RT2573_DROP_NOT_TO_ME;

	rum_write(sc, RT2573_TXRX_CSR0, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

static const char *
rum_get_rf(int rev)
{
	switch (rev) {
	case RT2573_RF_2527:	return "RT2527 (MIMO XR)";
	case RT2573_RF_2528:	return "RT2528";
	case RT2573_RF_5225:	return "RT5225 (MIMO XR)";
	case RT2573_RF_5226:	return "RT5226";
	default:		return "unknown";
	}
}

static void
rum_read_eeprom(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
#ifdef RUM_DEBUG
	int i;
#endif

	/* read MAC/BBP type */
	rum_eeprom_read(sc, RT2573_EEPROM_MACBBP, &val, 2);
	sc->macbbp_rev = le16toh(val);

	/* read MAC address */
	rum_eeprom_read(sc, RT2573_EEPROM_ADDRESS, ic->ic_myaddr, 6);

	rum_eeprom_read(sc, RT2573_EEPROM_ANTENNA, &val, 2);
	val = le16toh(val);
	sc->rf_rev =   (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	DPRINTF(("RF revision=%d\n", sc->rf_rev));

	rum_eeprom_read(sc, RT2573_EEPROM_CONFIG2, &val, 2);
	val = le16toh(val);
	sc->ext_5ghz_lna = (val >> 6) & 0x1;
	sc->ext_2ghz_lna = (val >> 4) & 0x1;

	DPRINTF(("External 2GHz LNA=%d\nExternal 5GHz LNA=%d\n",
	    sc->ext_2ghz_lna, sc->ext_5ghz_lna));

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_2GHZ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_2ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_2ghz_corr < -10 || sc->rssi_2ghz_corr > 10)
		sc->rssi_2ghz_corr = 0;

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_5GHZ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_5ghz_corr < -10 || sc->rssi_5ghz_corr > 10)
		sc->rssi_5ghz_corr = 0;

	if (sc->ext_2ghz_lna)
		sc->rssi_2ghz_corr -= 14;
	if (sc->ext_5ghz_lna)
		sc->rssi_5ghz_corr -= 14;

	DPRINTF(("RSSI 2GHz corr=%d\nRSSI 5GHz corr=%d\n",
	    sc->rssi_2ghz_corr, sc->rssi_5ghz_corr));

	rum_eeprom_read(sc, RT2573_EEPROM_FREQ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rffreq = val & 0xff;

	DPRINTF(("RF freq=%d\n", sc->rffreq));

	/* read Tx power for all a/b/g channels */
	rum_eeprom_read(sc, RT2573_EEPROM_TXPOWER, sc->txpow, 14);
	/* XXX default Tx power for 802.11a channels */
	memset(sc->txpow + 14, 24, sizeof (sc->txpow) - 14);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++)
		DPRINTF(("Channel=%d Tx power=%d\n", i + 1,  sc->txpow[i]));
#endif

	/* read default values for BBP registers */
	rum_eeprom_read(sc, RT2573_EEPROM_BBP_BASE, sc->bbp_prom, 2 * 16);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++) {
		if (sc->bbp_prom[i].reg == 0 || sc->bbp_prom[i].reg == 0xff)
			continue;
		DPRINTF(("BBP R%d=%02x\n", sc->bbp_prom[i].reg,
		    sc->bbp_prom[i].val));
	}
#endif
}

static int
rum_bbp_init(struct rum_softc *sc)
{
#define N(a)	(sizeof (a) / sizeof ((a)[0]))
	int i, ntries;
	uint8_t val;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		val = rum_bbp_read(sc, 0);
		if (val != 0 && val != 0xff)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		kprintf("%s: timeout waiting for BBP\n",
		    device_get_nameunit(sc->sc_dev));
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < N(rum_def_bbp); i++)
		rum_bbp_write(sc, rum_def_bbp[i].reg, rum_def_bbp[i].val);

	/* write vendor-specific BBP values (from EEPROM) */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0 || sc->bbp_prom[i].reg == 0xff)
			continue;
		rum_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}

	return 0;
#undef N
}

static void
rum_init(void *xsc)
{
#define N(a)	(sizeof(a) / sizeof((a)[0]))
	struct rum_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rum_rx_data *data;
	uint32_t tmp;
	usbd_status usb_err;
	int i, ntries, error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	rum_stop(sc);
	sc->sc_stopped = 0;

	lwkt_serialize_exit(ifp->if_serializer);

	/* initialize MAC registers to default values */
	for (i = 0; i < N(rum_def_mac); i++)
		rum_write(sc, rum_def_mac[i].reg, rum_def_mac[i].val);

	/* set host ready */
	rum_write(sc, RT2573_MAC_CSR1, 3);
	rum_write(sc, RT2573_MAC_CSR1, 0);

	/* wait for BBP/RF to wakeup */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (rum_read(sc, RT2573_MAC_CSR12) & 8)
			break;
		rum_write(sc, RT2573_MAC_CSR12, 4);	/* force wakeup */
		DELAY(1000);
	}
	if (ntries == 1000) {
		kprintf("%s: timeout waiting for BBP/RF to wakeup\n",
			device_get_nameunit(sc->sc_dev));
		error = ETIMEDOUT;
		goto fail;
	}

	error = rum_bbp_init(sc);
	if (error)
		goto fail;

	/* select default channel */
	sc->sc_curchan = ic->ic_curchan = ic->ic_ibss_chan;

	rum_select_band(sc, sc->sc_curchan);
	rum_select_antenna(sc);
	rum_set_chan(sc, sc->sc_curchan);

	/* clear STA registers */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof sc->sta);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, IF_LLADDR(ifp));
	rum_set_macaddr(sc, ic->ic_myaddr);

	/* initialize ASIC */
	rum_write(sc, RT2573_MAC_CSR1, 4);

	/*
	 * Allocate xfer for AMRR statistics requests.
	 */
	sc->stats_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->stats_xfer == NULL) {
		kprintf("%s: could not allocate AMRR xfer\n",
			device_get_nameunit(sc->sc_dev));
		error = ENOMEM;
		goto fail;
	}

	/*
	 * Open Tx and Rx USB bulk pipes.
	 */
	usb_err = usbd_open_pipe(sc->sc_iface, sc->sc_tx_no, USBD_EXCLUSIVE_USE,
				 &sc->sc_tx_pipeh);
	if (usb_err != USBD_NORMAL_COMPLETION) {
		kprintf("%s: could not open Tx pipe: %s\n",
			device_get_nameunit(sc->sc_dev), usbd_errstr(usb_err));
		error = EIO;
		goto fail;
	}

	usb_err = usbd_open_pipe(sc->sc_iface, sc->sc_rx_no, USBD_EXCLUSIVE_USE,
				 &sc->sc_rx_pipeh);
	if (usb_err != USBD_NORMAL_COMPLETION) {
		kprintf("%s: could not open Rx pipe: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(usb_err));
		error = EIO;
		goto fail;
	}

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	error = rum_alloc_tx_list(sc);
	if (error) {
		kprintf("%s: could not allocate Tx list\n",
			device_get_nameunit(sc->sc_dev));
		goto fail;
	}

	error = rum_alloc_rx_list(sc);
	if (error) {
		kprintf("%s: could not allocate Rx list\n",
			device_get_nameunit(sc->sc_dev));
		goto fail;
	}

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->sc_rx_pipeh, data, data->buf,
		    MCLBYTES, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rum_rxeof);
		usbd_transfer(data->xfer);
	}

	/* update Rx filter */
	tmp = rum_read(sc, RT2573_TXRX_CSR0) & 0xffff;

	tmp |= RT2573_DROP_PHY_ERROR | RT2573_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2573_DROP_CTL | RT2573_DROP_VER_ERROR |
		       RT2573_DROP_ACKCTS;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
			tmp |= RT2573_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RT2573_DROP_NOT_TO_ME;
	}
	rum_write(sc, RT2573_TXRX_CSR0, tmp);
fail:
	lwkt_serialize_enter(ifp->if_serializer);

	if (error) {
		rum_stop(sc);
	} else {
		ifp->if_flags &= ~IFF_OACTIVE;
		ifp->if_flags |= IFF_RUNNING;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
				ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		} else {
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		}
	}

	crit_exit();
#undef N
}

static void
rum_stop(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t tmp;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	sc->sc_stopped = 1;

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;

	lwkt_serialize_exit(ifp->if_serializer);

	/* disable Rx */
	tmp = rum_read(sc, RT2573_TXRX_CSR0);
	rum_write(sc, RT2573_TXRX_CSR0, tmp | RT2573_DISABLE_RX);

	/* reset ASIC */
	rum_write(sc, RT2573_MAC_CSR1, 3);
	rum_write(sc, RT2573_MAC_CSR1, 0);

	if (sc->stats_xfer != NULL) {
		usbd_free_xfer(sc->stats_xfer);
		sc->stats_xfer = NULL;
	}

	if (sc->sc_rx_pipeh != NULL) {
		usbd_abort_pipe(sc->sc_rx_pipeh);
		usbd_close_pipe(sc->sc_rx_pipeh);
		sc->sc_rx_pipeh = NULL;
	}

	if (sc->sc_tx_pipeh != NULL) {
		usbd_abort_pipe(sc->sc_tx_pipeh);
		usbd_close_pipe(sc->sc_tx_pipeh);
		sc->sc_tx_pipeh = NULL;
	}

	lwkt_serialize_enter(ifp->if_serializer);

	rum_free_rx_list(sc);
	rum_free_tx_list(sc);

	crit_exit();
}

static int
rum_load_microcode(struct rum_softc *sc, const uint8_t *ucode, size_t size)
{
	usb_device_request_t req;
	uint16_t reg = RT2573_MCU_CODE_BASE;
	usbd_status error;

	/* copy firmware image into NIC */
	for (; size >= 4; reg += 4, ucode += 4, size -= 4)
		rum_write(sc, reg, UGETDW(ucode));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_MCU_CNTL;
	USETW(req.wValue, RT2573_MCU_RUN);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	error = usbd_do_request(sc->sc_udev, &req, NULL);
	if (error != 0) {
		kprintf("%s: could not run firmware: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
	return error;
}

static int
rum_prepare_beacon(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_beacon_offsets bo;
	struct rum_tx_desc desc;
	struct mbuf *m0;
	int rate;

	lwkt_serialize_enter(ifp->if_serializer);
	m0 = ieee80211_beacon_alloc(ic, ic->ic_bss, &bo);
	lwkt_serialize_exit(ifp->if_serializer);

	if (m0 == NULL) {
		if_printf(&ic->ic_if, "could not allocate beacon frame\n");
		return ENOBUFS;
	}

	/* send beacons at the lowest available rate */
	rate = IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan) ? 12 : 2;

	rum_setup_tx_desc(sc, &desc, RT2573_TX_TIMESTAMP, RT2573_TX_HWSEQ,
	    m0->m_pkthdr.len, rate);

	/* copy the first 24 bytes of Tx descriptor into NIC memory */
	rum_write_multi(sc, RT2573_HW_BEACON_BASE0, (uint8_t *)&desc, 24);

	/* copy beacon header and payload into NIC memory */
	rum_write_multi(sc, RT2573_HW_BEACON_BASE0 + 24, mtod(m0, uint8_t *),
	    m0->m_pkthdr.len);

	m_freem(m0);

	return 0;
}

static void
rum_stats_timeout(void *arg)
{
	struct rum_softc *sc = arg;
	usb_device_request_t req;

	if (sc->sc_stopped)
		return;

	crit_enter();

	/*
	 * Asynchronously read statistic registers (cleared by read).
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, RT2573_STA_CSR0);
	USETW(req.wLength, sizeof(sc->sta));

	usbd_setup_default_xfer(sc->stats_xfer, sc->sc_udev, sc,
				USBD_DEFAULT_TIMEOUT, &req,
				sc->sta, sizeof(sc->sta), 0,
				rum_stats_update);
	usbd_transfer(sc->stats_xfer);

	crit_exit();
}

static void
rum_stats_update(usbd_xfer_handle xfer, usbd_private_handle priv,
		 usbd_status status)
{
	struct rum_softc *sc = (struct rum_softc *)priv;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ieee80211_ratectl_stats *stats = &sc->sc_stats;

	if (status != USBD_NORMAL_COMPLETION) {
		kprintf("%s: could not retrieve Tx statistics - cancelling "
		    "automatic rate control\n", device_get_nameunit(sc->sc_dev));
		return;
	}

	crit_enter();

	/* count TX retry-fail as Tx errors */
	ifp->if_oerrors += RUM_TX_PKT_FAIL(sc);

	stats->stats_pkt_noretry += RUM_TX_PKT_NO_RETRY(sc);
	stats->stats_pkt_ok += RUM_TX_PKT_NO_RETRY(sc) +
			       RUM_TX_PKT_ONE_RETRY(sc) +
			       RUM_TX_PKT_MULTI_RETRY(sc);
	stats->stats_pkt_err += RUM_TX_PKT_FAIL(sc);

	stats->stats_retries += RUM_TX_PKT_ONE_RETRY(sc);
#if 1
	/*
	 * XXX Estimated average:
	 * Actual number of retries for each packet should belong to
	 * [2, RUM_TX_SHORT_RETRY_MAX]
	 */
	stats->stats_retries += RUM_TX_PKT_MULTI_RETRY(sc) *
				((2 + RUM_TX_SHORT_RETRY_MAX) / 2);
#else
	stats->stats_retries += RUM_TX_PKT_MULTI_RETRY(sc);
#endif
	stats->stats_retries += RUM_TX_PKT_FAIL(sc) * RUM_TX_SHORT_RETRY_MAX;

	callout_reset(&sc->stats_ch, 4 * hz / 5, rum_stats_timeout, sc);

	crit_exit();
}

static void
rum_stats(struct ieee80211com *ic, struct ieee80211_node *ni __unused,
	  struct ieee80211_ratectl_stats *stats)
{
	struct ifnet *ifp = &ic->ic_if;
	struct rum_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	bcopy(&sc->sc_stats, stats, sizeof(*stats));
	bzero(&sc->sc_stats, sizeof(sc->sc_stats));
}

static void *
rum_ratectl_attach(struct ieee80211com *ic, u_int rc)
{
	struct rum_softc *sc = ic->ic_if.if_softc;

	switch (rc) {
	case IEEE80211_RATECTL_ONOE:
		return &sc->sc_onoe_param;
	case IEEE80211_RATECTL_NONE:
		/* This could only happen during detaching */
		return NULL;
	default:
		panic("unknown rate control algo %u\n", rc);
		return NULL;
	}
}

static int
rum_get_rssi(struct rum_softc *sc, uint8_t raw)
{
	int lna, agc, rssi;

	lna = (raw >> 5) & 0x3;
	agc = raw & 0x1f;

	if (lna == 0) {
		/*
		 * No RSSI mapping
		 *
		 * NB: Since RSSI is relative to noise floor, -1 is
		 *     adequate for caller to know error happened.
		 */
		return -1;
	}

	rssi = (2 * agc) - RT2573_NOISE_FLOOR;

	if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan)) {
		rssi += sc->rssi_2ghz_corr;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 74;
		else if (lna == 3)
			rssi -= 90;
	} else {
		rssi += sc->rssi_5ghz_corr;

		if (!sc->ext_5ghz_lna && lna != 1)
			rssi += 4;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 86;
		else if (lna == 3)
			rssi -= 100;
	}
	return rssi;
}
