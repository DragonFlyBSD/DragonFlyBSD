/*	$OpenBSD: if_rum.c,v 1.40 2006/09/18 16:20:20 damien Exp $	*/
/*	$DragonFly: src/sys/dev/netif/rum/if_rum.c,v 1.4 2007/01/02 23:28:49 swildner Exp $	*/

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
#include <bus/usb/usbdevs.h>

#include "if_rumreg.h"
#include "if_rumvar.h"
#include "rum_ucode.h"

#ifdef USB_DEBUG
#define RUM_DEBUG
#endif

#ifdef RUM_DEBUG
#define DPRINTF(x)	do { if (rum_debug) logprintf x; } while (0)
#define DPRINTFN(n, x)	do { if (rum_debug >= (n)) logprintf x; } while (0)
int rum_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/* various supported device vendors/products */
static const struct usb_devno rum_devs[] = {
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_RT2573 },
	{ USB_VENDOR_ASUS,		USB_PRODUCT_ASUS_WL167G_2 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D7050A },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D9050V3 },
	{ USB_VENDOR_LINKSYS4,		USB_PRODUCT_LINKSYS4_WUSB54GC },
	{ USB_VENDOR_CONCEPTRONIC,	USB_PRODUCT_CONCEPTRONIC_C54RU2 },
	{ USB_VENDOR_DICKSMITH,		USB_PRODUCT_DICKSMITH_CWD854F },
	{ USB_VENDOR_DICKSMITH,		USB_PRODUCT_DICKSMITH_RT2573 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWLG122C1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_WUA1340 },
	{ USB_VENDOR_GIGABYTE,		USB_PRODUCT_GIGABYTE_GNWB01GS },
	{ USB_VENDOR_GIGASET,		USB_PRODUCT_GIGASET_RT2573 },
	{ USB_VENDOR_GOODWAY,		USB_PRODUCT_GOODWAY_RT2573 },
	{ USB_VENDOR_HUAWEI3COM,	USB_PRODUCT_HUAWEI3COM_RT2573 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_2 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_3 },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUSMM },
	{ USB_VENDOR_QCOM,		USB_PRODUCT_QCOM_RT2573 },
	{ USB_VENDOR_QCOM,		USB_PRODUCT_QCOM_RT2573_2 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2573 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2671 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL113R2 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL172 },
	{ USB_VENDOR_SURECOM,		USB_PRODUCT_SURECOM_RT2573 }
};

#ifdef notyet
Static void		rum_attachhook(void *);
#endif
Static int		rum_alloc_tx_list(struct rum_softc *);
Static void		rum_free_tx_list(struct rum_softc *);
Static int		rum_alloc_rx_list(struct rum_softc *);
Static void		rum_free_rx_list(struct rum_softc *);
Static int		rum_media_change(struct ifnet *);
Static void		rum_next_scan(void *);
Static void		rum_task(void *);
Static int		rum_newstate(struct ieee80211com *,
			    enum ieee80211_state, int);
Static void		rum_txeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
Static void		rum_rxeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
Static uint8_t		rum_rxrate(struct rum_rx_desc *);
Static int		rum_ack_rate(struct ieee80211com *, int);
Static uint16_t		rum_txtime(int, int, uint32_t);
Static uint8_t		rum_plcp_signal(int);
Static void		rum_setup_tx_desc(struct rum_softc *,
			    struct rum_tx_desc *, uint32_t, uint16_t, int,
			    int);
Static int		rum_tx_data(struct rum_softc *, struct mbuf *,
			    struct ieee80211_node *);
Static void		rum_start(struct ifnet *);
Static void		rum_watchdog(struct ifnet *);
Static int		rum_ioctl(struct ifnet *, u_long, caddr_t,
				  struct ucred *);
Static void		rum_eeprom_read(struct rum_softc *, uint16_t, void *,
			    int);
Static uint32_t		rum_read(struct rum_softc *, uint16_t);
Static void		rum_read_multi(struct rum_softc *, uint16_t, void *,
			    int);
Static void		rum_write(struct rum_softc *, uint16_t, uint32_t);
Static void		rum_write_multi(struct rum_softc *, uint16_t, void *,
			    size_t);
Static void		rum_bbp_write(struct rum_softc *, uint8_t, uint8_t);
Static uint8_t		rum_bbp_read(struct rum_softc *, uint8_t);
Static void		rum_rf_write(struct rum_softc *, uint8_t, uint32_t);
Static void		rum_select_antenna(struct rum_softc *);
Static void		rum_enable_mrr(struct rum_softc *);
Static void		rum_set_txpreamble(struct rum_softc *);
Static void		rum_set_basicrates(struct rum_softc *);
Static void		rum_select_band(struct rum_softc *,
			    struct ieee80211_channel *);
Static void		rum_set_chan(struct rum_softc *,
			    struct ieee80211_channel *);
Static void		rum_enable_tsf_sync(struct rum_softc *);
Static void		rum_update_slot(struct rum_softc *);
Static void		rum_set_bssid(struct rum_softc *, const uint8_t *);
Static void		rum_set_macaddr(struct rum_softc *, const uint8_t *);
Static void		rum_update_promisc(struct rum_softc *);
Static const char	*rum_get_rf(int);
Static void		rum_read_eeprom(struct rum_softc *);
Static int		rum_bbp_init(struct rum_softc *);
Static void		rum_init(void *);
Static void		rum_stop(struct rum_softc *);
Static int		rum_load_microcode(struct rum_softc *, const uint8_t *,
			    size_t);
Static int		rum_prepare_beacon(struct rum_softc *);

Static void		rum_stats_timeout(void *);
Static void		rum_stats_update(usbd_xfer_handle, usbd_private_handle,
					 usbd_status);
Static void		rum_stats(struct ieee80211com *,
				  struct ieee80211_node *,
				  struct ieee80211_ratectl_stats *);
Static void		rum_ratectl_change(struct ieee80211com *ic, u_int,
					   u_int);

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

USB_DECLARE_DRIVER(rum);
DRIVER_MODULE(rum, uhub, rum_driver, rum_devclass, usbd_driver_load, 0);

USB_MATCH(rum)
{
	USB_MATCH_START(rum, uaa);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (usb_lookup(rum_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

USB_ATTACH(rum)
{
	USB_ATTACH_START(rum, sc, uaa);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	char devinfo[1024];
	int i, ntries;
	uint32_t tmp;

	sc->sc_udev = uaa->device;

	usbd_devinfo(uaa->device, 0, devinfo);
	USB_ATTACH_SETUP;

	if (usbd_set_config_no(sc->sc_udev, RT2573_CONFIG_NO, 0) != 0) {
		kprintf("%s: could not set configuration no\n",
		    USBDEVNAME(sc->sc_dev));
		USB_ATTACH_ERROR_RETURN;
	}

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, RT2573_IFACE_INDEX,
	    &sc->sc_iface);
	if (error != 0) {
		kprintf("%s: could not get interface handle\n",
		    USBDEVNAME(sc->sc_dev));
		USB_ATTACH_ERROR_RETURN;
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
			    USBDEVNAME(sc->sc_dev), i);
			USB_ATTACH_ERROR_RETURN;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_rx_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_tx_no = ed->bEndpointAddress;
	}
	if (sc->sc_rx_no == -1 || sc->sc_tx_no == -1) {
		kprintf("%s: missing endpoint\n", USBDEVNAME(sc->sc_dev));
		USB_ATTACH_ERROR_RETURN;
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
		    USBDEVNAME(sc->sc_dev));
		USB_ATTACH_ERROR_RETURN;
	}

	/* retrieve MAC address and various other things from EEPROM */
	rum_read_eeprom(sc);

	kprintf("%s: MAC/BBP RT%04x (rev 0x%05x), RF %s, address %6D\n",
	    USBDEVNAME(sc->sc_dev), sc->macbbp_rev, tmp,
	    rum_get_rf(sc->rf_rev), ic->ic_myaddr, ":");

	error = rum_load_microcode(sc, rt2573, sizeof(rt2573));
	if (error != 0) {
		device_printf(self, "can't load microcode\n");
		USB_ATTACH_ERROR_RETURN;
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

	if_initname(ifp, device_get_name(self), device_get_unit(self));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = rum_init;
	ifp->if_ioctl = rum_ioctl;
	ifp->if_start = rum_start;
	ifp->if_watchdog = rum_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_ONOE;
	ic->ic_ratectl.rc_st_valid_stats =
		IEEE80211_RATECTL_STATS_PKT_NORETRY |
		IEEE80211_RATECTL_STATS_PKT_OK |
		IEEE80211_RATECTL_STATS_PKT_ERR |
		IEEE80211_RATECTL_STATS_RETRIES;
	ic->ic_ratectl.rc_st_stats = rum_stats;
	ic->ic_ratectl.rc_st_change = rum_ratectl_change;

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

	USB_ATTACH_SUCCESS_RETURN;
}

USB_DETACH(rum)
{
	USB_DETACH_START(rum, sc);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

#ifdef spl
	s = splusb();
#endif

	lwkt_serialize_enter(ifp->if_serializer);

	callout_stop(&sc->scan_ch);
	callout_stop(&sc->stats_ch);

	sc->sc_flags |= RUM_FLAG_SYNCTASK;
	rum_stop(sc);

	lwkt_serialize_exit(ifp->if_serializer);

	usb_rem_task(sc->sc_udev, &sc->sc_task);

	bpfdetach(ifp);
	ieee80211_ifdetach(&sc->sc_ic);	/* free all nodes */

	if (sc->stats_xfer != NULL) {
		usbd_free_xfer(sc->stats_xfer);
		sc->stats_xfer = NULL;
	}

	if (sc->sc_rx_pipeh != NULL) {
		usbd_abort_pipe(sc->sc_rx_pipeh);
		usbd_close_pipe(sc->sc_rx_pipeh);
	}

	if (sc->sc_tx_pipeh != NULL) {
		usbd_abort_pipe(sc->sc_tx_pipeh);
		usbd_close_pipe(sc->sc_tx_pipeh);
	}

	rum_free_rx_list(sc);
	rum_free_tx_list(sc);

#ifdef spl
	splx(s);
#endif
	return 0;
}

Static int
rum_alloc_tx_list(struct rum_softc *sc)
{
	struct rum_tx_data *data;
	int i, error;

	sc->tx_queued = 0;

	for (i = 0; i < RT2573_TX_LIST_COUNT; i++) {
		data = &sc->tx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			kprintf("%s: could not allocate tx xfer\n",
			    USBDEVNAME(sc->sc_dev));
			error = ENOMEM;
			goto fail;
		}

		data->buf = usbd_alloc_buffer(data->xfer,
		    RT2573_TX_DESC_SIZE + IEEE80211_MAX_LEN);
		if (data->buf == NULL) {
			kprintf("%s: could not allocate tx buffer\n",
			    USBDEVNAME(sc->sc_dev));
			error = ENOMEM;
			goto fail;
		}

		/* clean Tx descriptor */
		bzero(data->buf, RT2573_TX_DESC_SIZE);
	}

	return 0;

fail:	rum_free_tx_list(sc);
	return error;
}

Static void
rum_free_tx_list(struct rum_softc *sc)
{
	struct rum_tx_data *data;
	int i;

	for (i = 0; i < RT2573_TX_LIST_COUNT; i++) {
		data = &sc->tx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}

		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
	}
}

Static int
rum_alloc_rx_list(struct rum_softc *sc)
{
	struct rum_rx_data *data;
	int i, error;

	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			kprintf("%s: could not allocate rx xfer\n",
			    USBDEVNAME(sc->sc_dev));
			error = ENOMEM;
			goto fail;
		}

		if (usbd_alloc_buffer(data->xfer, MCLBYTES) == NULL) {
			kprintf("%s: could not allocate rx buffer\n",
			    USBDEVNAME(sc->sc_dev));
			error = ENOMEM;
			goto fail;
		}

		data->m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (data->m == NULL) {
			kprintf("%s: could not allocate rx mbuf\n",
			    USBDEVNAME(sc->sc_dev));
			error = ENOMEM;
			goto fail;
		}

		data->buf = mtod(data->m, uint8_t *);
		bzero(data->buf, sizeof(struct rum_rx_desc));
	}

	return 0;

fail:	rum_free_tx_list(sc);
	return error;
}

Static void
rum_free_rx_list(struct rum_softc *sc)
{
	struct rum_rx_data *data;
	int i;

	for (i = 0; i < RT2573_RX_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

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

Static int
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
Static void
rum_next_scan(void *arg)
{
	struct rum_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ic);

	lwkt_serialize_exit(ifp->if_serializer);
}

Static void
rum_task(void *arg)
{
	struct rum_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	uint32_t tmp;

	lwkt_serialize_enter(ifp->if_serializer);

	ieee80211_ratectl_newstate(ic, sc->sc_state);

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			tmp = rum_read(sc, RT2573_TXRX_CSR9);
			rum_write(sc, RT2573_TXRX_CSR9, tmp & ~0x00ffffff);
		}
		break;

	case IEEE80211_S_SCAN:
		rum_set_chan(sc, ic->ic_curchan);
		callout_reset(&sc->scan_ch, hz / 5, rum_next_scan, sc);
		break;

	case IEEE80211_S_AUTH:
		rum_set_chan(sc, ic->ic_curchan);
		break;

	case IEEE80211_S_ASSOC:
		rum_set_chan(sc, ic->ic_curchan);
		break;

	case IEEE80211_S_RUN:
		rum_set_chan(sc, ic->ic_curchan);

		ni = ic->ic_bss;

		lwkt_serialize_exit(ifp->if_serializer);

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

		lwkt_serialize_enter(ifp->if_serializer);

		callout_reset(&sc->stats_ch, 4 * hz / 5, rum_stats_timeout, sc);

		break;
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);

	lwkt_serialize_exit(ifp->if_serializer);
}

Static int
rum_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rum_softc *sc = ic->ic_if.if_softc;
	struct ifnet *ifp = &ic->ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->scan_ch);
	callout_stop(&sc->stats_ch);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;

	lwkt_serialize_exit(ifp->if_serializer);
	usb_rem_task(sc->sc_udev, &sc->sc_task);

	if (sc->sc_flags & RUM_FLAG_SYNCTASK) {
		usb_do_task(sc->sc_udev, &sc->sc_task, USB_TASKQ_DRIVER,
			    USBD_NO_TIMEOUT);
	} else {
		usb_add_task(sc->sc_udev, &sc->sc_task, USB_TASKQ_DRIVER);
	}
	lwkt_serialize_enter(ifp->if_serializer);

	return 0;
}

/* quickly determine if a given rate is CCK or OFDM */
#define RUM_RATE_IS_OFDM(rate)	((rate) >= 12 && (rate) != 22)

#define RUM_ACK_SIZE	14	/* 10 + 4(FCS) */
#define RUM_CTS_SIZE	14	/* 10 + 4(FCS) */

Static void
rum_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct rum_tx_data *data = priv;
	struct rum_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		kprintf("%s: could not transmit buffer: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tx_pipeh);

		ifp->if_oerrors++;
		return;
	}

#ifdef spl
	s = splnet();
#endif

	lwkt_serialize_enter(ifp->if_serializer);

	m_freem(data->m);
	data->m = NULL;
	ieee80211_free_node(data->ni);
	data->ni = NULL;

	bzero(data->buf, sizeof(struct rum_tx_data));
	sc->tx_queued--;
	ifp->if_opackets++;

	DPRINTFN(10, ("tx done\n"));

	sc->sc_tx_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;
	rum_start(ifp);

	lwkt_serialize_exit(ifp->if_serializer);

#ifdef spl
	splx(s);
#endif
}

Static void
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
	int len;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipeh);
		goto skip;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (len < RT2573_RX_DESC_SIZE + sizeof (struct ieee80211_frame_min)) {
		DPRINTF(("%s: xfer too short %d\n", USBDEVNAME(sc->sc_dev),
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
		    USBDEVNAME(sc->sc_dev));
		ifp->if_ierrors++;
		goto skip;
	}

	m = data->m;
	data->m = mnew;
	data->buf = mtod(data->m, uint8_t *);

	/* finalize mbuf */
	m->m_pkthdr.rcvif = ifp;
	m->m_data = (caddr_t)(desc + 1);
	m->m_pkthdr.len = m->m_len = (le32toh(desc->flags) >> 16) & 0xfff;

#ifdef spl
	s = splnet();
#endif

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->sc_drvbpf != NULL) {
		struct rum_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_rate = rum_rxrate(desc);
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_antenna = sc->rx_ant;
		tap->wr_antsignal = desc->rssi;

		bpf_ptap(sc->sc_drvbpf, m, tap, sc->sc_rxtap_len);
	}

	wh = mtod(m, struct ieee80211_frame_min *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* send the frame to the 802.11 layer */
	ieee80211_input(ic, m, ni, desc->rssi, 0);

	/* node is no longer needed */
	ieee80211_free_node(ni);

	/*
	 * In HostAP mode, ieee80211_input() will enqueue packets in if_snd
	 * without calling if_start().
	 */
	if (!ifq_is_empty(&ifp->if_snd) && !(ifp->if_flags & IFF_OACTIVE))
		rum_start(ifp);

#ifdef spl
	splx(s);
#endif

	lwkt_serialize_exit(ifp->if_serializer);

	DPRINTFN(15, ("rx done\n"));

skip:	/* setup a new transfer */
	bzero(data->buf, sizeof(struct rum_rx_desc));
	usbd_setup_xfer(xfer, sc->sc_rx_pipeh, data, data->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rum_rxeof);
	usbd_transfer(xfer);
}

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
Static uint8_t
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

/*
 * Return the expected ack rate for a frame transmitted at rate `rate'.
 * XXX: this should depend on the destination node basic rate set.
 */
Static int
rum_ack_rate(struct ieee80211com *ic, int rate)
{
	switch (rate) {
	/* CCK rates */
	case 2:
		return 2;
	case 4:
	case 11:
	case 22:
		return (ic->ic_curmode == IEEE80211_MODE_11B) ? 4 : rate;

	/* OFDM rates */
	case 12:
	case 18:
		return 12;
	case 24:
	case 36:
		return 24;
	case 48:
	case 72:
	case 96:
	case 108:
		return 48;
	}

	/* default to 1Mbps */
	return 2;
}

/*
 * Compute the duration (in us) needed to transmit `len' bytes at rate `rate'.
 * The function automatically determines the operating mode depending on the
 * given rate. `flags' indicates whether short preamble is in use or not.
 */
Static uint16_t
rum_txtime(int len, int rate, uint32_t flags)
{
	uint16_t txtime;

	if (RUM_RATE_IS_OFDM(rate)) {
		/* IEEE Std 802.11a-1999, pp. 37 */
		txtime = (8 + 4 * len + 3 + rate - 1) / rate;
		txtime = 16 + 4 + 4 * txtime + 6;
	} else {
		/* IEEE Std 802.11b-1999, pp. 28 */
		txtime = (16 * len + rate - 1) / rate;
		if (rate != 2 && (flags & IEEE80211_F_SHPREAMBLE))
			txtime +=  72 + 24;
		else
			txtime += 144 + 48;
	}
	return txtime;
}

Static uint8_t
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

Static void
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

Static int
rum_tx_data(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rum_tx_desc *desc;
	struct rum_tx_data *data;
	struct ieee80211_frame *wh;
	uint32_t flags = 0;
	uint16_t dur;
	usbd_status error;
	int xferlen, rate;

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
		rate = ni->ni_rates.rs_rates[0];
	} else {
		if (ic->ic_fixed_rate != -1) {
			rate = ic->ic_sup_rates[ic->ic_curmode].
			    rs_rates[ic->ic_fixed_rate];
		} else
			rate = ni->ni_rates.rs_rates[ni->ni_txrate];
	}
	rate &= IEEE80211_RATE_VAL;
	if (rate == 0)
		rate = 2;	/* fallback to 1Mbps; should not happen  */

	data = &sc->tx_data[0];
	desc = (struct rum_tx_desc *)data->buf;

	data->m = m0;
	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2573_TX_ACK;

		dur = rum_txtime(RUM_ACK_SIZE, rum_ack_rate(ic, rate),
		    ic->ic_flags) + sc->sifs;
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

	/* align end on a 4-bytes boundary */
	xferlen = (RT2573_TX_DESC_SIZE + m0->m_pkthdr.len + 3) & ~3;

	/*
	 * No space left in the last URB to store the extra 4 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 4;

	DPRINTFN(10, ("sending frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len + RT2573_TX_DESC_SIZE, rate, xferlen));

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RUM_TX_TIMEOUT, rum_txeof);

	error = usbd_transfer(data->xfer);
	if (error != USBD_NORMAL_COMPLETION && error != USBD_IN_PROGRESS) {
		m_freem(m0);
		return error;
	}

	sc->tx_queued++;

	return 0;
}

Static void
rum_start(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * net80211 may still try to send management frames even if the
	 * IFF_RUNNING flag is not set...
	 */
	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	for (;;) {
		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			if (sc->tx_queued >= RT2573_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			IF_DEQUEUE(&ic->ic_mgtq, m0);

			ni = (struct ieee80211_node *)m0->m_pkthdr.rcvif;
			m0->m_pkthdr.rcvif = NULL;

			BPF_MTAP(ifp, m0);

			if (rum_tx_data(sc, m0, ni) != 0)
				break;

		} else {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN)
				break;

			m0 = ifq_poll(&ifp->if_snd);
			if (m0 == NULL)
				break;
			if (sc->tx_queued >= RT2573_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			ifq_dequeue(&ifp->if_snd, m0);

			if (m0->m_len < sizeof (struct ether_header) &&
			    !(m0 = m_pullup(m0, sizeof (struct ether_header))))
			    	continue;

			eh = mtod(m0, struct ether_header *);
			ni = ieee80211_find_txnode(ic, eh->ether_dhost);
			if (ni == NULL) {
				m_freem(m0);
				continue;
			}

			BPF_MTAP(ifp, m0);

			m0 = ieee80211_encap(ic, m0, ni);
			if (m0 == NULL)
				continue;

			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0);

			if (rum_tx_data(sc, m0, ni) != 0) {
				if (ni != NULL)
					ieee80211_free_node(ni);
				ifp->if_oerrors++;
				break;
			}
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

Static void
rum_watchdog(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			kprintf("%s: device timeout\n", USBDEVNAME(sc->sc_dev));
			/*rum_init(sc); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(&sc->sc_ic);
}

Static int
rum_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

#ifdef spl
	s = splnet();
#endif

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				rum_update_promisc(sc);
			else
				rum_init(sc);
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
			 * This allows for fast channel switching in monitor mode
			 * (used by kismet). In IBSS mode, we must explicitly reset
			 * the interface to generate a new beacon frame.
			 */
			rum_set_chan(sc, ic->ic_ibss_chan);
		} else if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			rum_init(sc);
		}
		error = 0;
	}

#ifdef spl
	splx(s);
#endif

	return error;
}

Static void
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
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
	}
}

Static uint32_t
rum_read(struct rum_softc *sc, uint16_t reg)
{
	uint32_t val;

	rum_read_multi(sc, reg, &val, sizeof val);

	return le32toh(val);
}

Static void
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
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
	}
}

Static void
rum_write(struct rum_softc *sc, uint16_t reg, uint32_t val)
{
	uint32_t tmp = htole32(val);

	rum_write_multi(sc, reg, &tmp, sizeof tmp);
}

Static void
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
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
	}
}

Static void
rum_bbp_write(struct rum_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to BBP\n", USBDEVNAME(sc->sc_dev));
		return;
	}

	tmp = RT2573_BBP_BUSY | (reg & 0x7f) << 8 | val;
	rum_write(sc, RT2573_PHY_CSR3, tmp);
}

Static uint8_t
rum_bbp_read(struct rum_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not read BBP\n", USBDEVNAME(sc->sc_dev));
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

	kprintf("%s: could not read BBP\n", USBDEVNAME(sc->sc_dev));
	return 0;
}

Static void
rum_rf_write(struct rum_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR4) & RT2573_RF_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to RF\n", USBDEVNAME(sc->sc_dev));
		return;
	}

	tmp = RT2573_RF_BUSY | RT2573_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 3);
	rum_write(sc, RT2573_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 3, val & 0xfffff));
}

Static void
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
Static void
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

Static void
rum_set_txpreamble(struct rum_softc *sc)
{
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR4);

	tmp &= ~RT2573_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2573_SHORT_PREAMBLE;

	rum_write(sc, RT2573_TXRX_CSR4, tmp);
}

Static void
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
Static void
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

	/* 802.11a uses a 16 microseconds short interframe space */
	sc->sifs = IEEE80211_IS_CHAN_5GHZ(c) ? 16 : 10;
}

Static void
rum_set_chan(struct rum_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2573_BBPR94_DEFAULT;
	int8_t power;
	u_int i, chan;

	ASSERT_SERIALIZED(ifp->if_serializer);

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	lwkt_serialize_exit(ifp->if_serializer);

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

	bbp3 &= ~RT2573_SMART_MODE;
	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_2527)
		bbp3 |= RT2573_SMART_MODE;

	rum_bbp_write(sc, 3, bbp3);

	if (bbp94 != RT2573_BBPR94_DEFAULT)
		rum_bbp_write(sc, 94, bbp94);

	lwkt_serialize_enter(ifp->if_serializer);
}

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
Static void
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

Static void
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

Static void
rum_set_bssid(struct rum_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	rum_write(sc, RT2573_MAC_CSR4, tmp);

	tmp = bssid[4] | bssid[5] << 8 | RT2573_ONE_BSSID << 16;
	rum_write(sc, RT2573_MAC_CSR5, tmp);
}

Static void
rum_set_macaddr(struct rum_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	rum_write(sc, RT2573_MAC_CSR2, tmp);

	tmp = addr[4] | addr[5] << 8 | 0xff << 16;
	rum_write(sc, RT2573_MAC_CSR3, tmp);
}

Static void
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

Static const char *
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

Static void
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

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_5GHZ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

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

Static int
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
		    USBDEVNAME(sc->sc_dev));
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

Static void
rum_init(void *xsc)
{
#define N(a)	(sizeof (a) / sizeof ((a)[0]))
	struct rum_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rum_rx_data *data;
	uint32_t tmp;
	usbd_status error;
	int i, ntries;

	ASSERT_SERIALIZED(ifp->if_serializer);

	rum_stop(sc);

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
		    USBDEVNAME(sc->sc_dev));
		goto fail;
	}

	if ((error = rum_bbp_init(sc)) != 0)
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
		    USBDEVNAME(sc->sc_dev));
		goto fail;
	}

	/*
	 * Open Tx and Rx USB bulk pipes.
	 */
	error = usbd_open_pipe(sc->sc_iface, sc->sc_tx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_tx_pipeh);
	if (error != 0) {
		kprintf("%s: could not open Tx pipe: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, sc->sc_rx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_rx_pipeh);
	if (error != 0) {
		kprintf("%s: could not open Rx pipe: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		goto fail;
	}

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	error = rum_alloc_tx_list(sc);
	if (error != 0) {
		kprintf("%s: could not allocate Tx list\n",
		    USBDEVNAME(sc->sc_dev));
		goto fail;
	}

	error = rum_alloc_rx_list(sc);
	if (error != 0) {
		kprintf("%s: could not allocate Rx list\n",
		    USBDEVNAME(sc->sc_dev));
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

	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return;

fail:	rum_stop(sc);

#undef N
}

Static void
rum_stop(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t tmp;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	/* disable Rx */
	tmp = rum_read(sc, RT2573_TXRX_CSR0);
	rum_write(sc, RT2573_TXRX_CSR0, tmp | RT2573_DISABLE_RX);

	/* reset ASIC */
	rum_write(sc, RT2573_MAC_CSR1, 3);
	rum_write(sc, RT2573_MAC_CSR1, 0);

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

	rum_free_rx_list(sc);
	rum_free_tx_list(sc);
}

Static int
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
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
	}
	return error;
}

Static int
rum_prepare_beacon(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_beacon_offsets bo;
	struct rum_tx_desc desc;
	struct mbuf *m0;
	int rate;

	m0 = ieee80211_beacon_alloc(ic, ic->ic_bss, &bo);
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

Static void
rum_stats_timeout(void *arg)
{
	struct rum_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	usb_device_request_t req;

	lwkt_serialize_enter(ifp->if_serializer);

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

	lwkt_serialize_exit(ifp->if_serializer);
}

Static void
rum_stats_update(usbd_xfer_handle xfer, usbd_private_handle priv,
		 usbd_status status)
{
	struct rum_softc *sc = (struct rum_softc *)priv;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ieee80211_ratectl_stats *stats = &sc->sc_stats;

	if (status != USBD_NORMAL_COMPLETION) {
		kprintf("%s: could not retrieve Tx statistics - cancelling "
		    "automatic rate control\n", USBDEVNAME(sc->sc_dev));
		return;
	}

	lwkt_serialize_enter(ifp->if_serializer);

	/* count TX retry-fail as Tx errors */
	ifp->if_oerrors += RUM_TX_PKT_FAIL(sc);

	stats->stats_pkt_noretry += RUM_TX_PKT_NO_RETRY(sc);
	stats->stats_pkt_ok += RUM_TX_PKT_NO_RETRY(sc) +
			       RUM_TX_PKT_ONE_RETRY(sc) +
			       RUM_TX_PKT_MULTI_RETRY(sc);
	stats->stats_pkt_err += RUM_TX_PKT_FAIL(sc);

	stats->stats_short_retries += RUM_TX_PKT_ONE_RETRY(sc);
#if 1
	/*
	 * XXX Estimated average:
	 * Actual number of retries for each packet should belong to
	 * [2, RUM_TX_SHORT_RETRY_MAX]
	 */
	stats->stats_short_retries +=
		RUM_TX_PKT_MULTI_RETRY(sc) *
		((2 + RUM_TX_SHORT_RETRY_MAX) / 2);
#else
	stats->stats_short_retries += RUM_TX_PKT_MULTI_RETRY(sc);
#endif
	stats->stats_short_retries +=
		RUM_TX_PKT_FAIL(sc) * RUM_TX_SHORT_RETRY_MAX;

	callout_reset(&sc->stats_ch, 4 * hz / 5, rum_stats_timeout, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

Static void
rum_stats(struct ieee80211com *ic, struct ieee80211_node *ni __unused,
	  struct ieee80211_ratectl_stats *stats)
{
	struct ifnet *ifp = &ic->ic_if;
	struct rum_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	bcopy(&sc->sc_stats, stats, sizeof(*stats));
	bzero(&sc->sc_stats, sizeof(sc->sc_stats));
}

Static void
rum_ratectl_change(struct ieee80211com *ic, u_int orc __unused, u_int nrc)
{
	struct ieee80211_ratectl_state *st = &ic->ic_ratectl;
	struct ieee80211_onoe_param *oparam;

	if (st->rc_st_param != NULL) {
		kfree(st->rc_st_param, M_DEVBUF);
		st->rc_st_param = NULL;
	}

	switch (nrc) {
	case IEEE80211_RATECTL_ONOE:
		oparam = kmalloc(sizeof(*oparam), M_DEVBUF, M_INTWAIT);

		IEEE80211_ONOE_PARAM_SETUP(oparam);
		oparam->onoe_raise = 15;

		st->rc_st_param = oparam;
		break;
	case IEEE80211_RATECTL_NONE:
		/* This could only happen during detaching */
		break;
	default:
		panic("unknown rate control algo %u\n", nrc);
	}
}
