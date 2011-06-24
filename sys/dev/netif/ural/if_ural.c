/*	$FreeBSD: src/sys/dev/usb/if_ural.c,v 1.10.2.8 2006/07/08 07:48:43 maxim Exp $	*/

/*-
 * Copyright (c) 2005, 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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
 * Ralink Technology RT2500USB chipset driver
 * http://www.ralinktech.com/
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

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

#include <dev/netif/ural/if_uralreg.h>
#include <dev/netif/ural/if_uralvar.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (uraldebug > 0) kprintf x; } while (0)
#define DPRINTFN(n, x)	do { if (uraldebug >= (n)) kprintf x; } while (0)
int uraldebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, ural, CTLFLAG_RW, 0, "USB ural");
SYSCTL_INT(_hw_usb_ural, OID_AUTO, debug, CTLFLAG_RW, &uraldebug, 0,
    "ural debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

#define URAL_RSSI(rssi)					\
	((rssi) > (RAL_NOISE_FLOOR + RAL_RSSI_CORR) ?	\
	 ((rssi) - (RAL_NOISE_FLOOR + RAL_RSSI_CORR)) : 0)

/* various supported device vendors/products */
static const struct usb_devno ural_devs[] = {
	{ USB_DEVICE(0x0411, 0x005e) }, /* Melco WLI-U2-KG54-YB */
	{ USB_DEVICE(0x0411, 0x0066) }, /* Melco WLI-U2-KG54 */
	{ USB_DEVICE(0x0411, 0x0067) }, /* Melco WLI-U2-KG54-AI */
	{ USB_DEVICE(0x0411, 0x008b) }, /* Melco Nintendo Wi-Fi */
	{ USB_DEVICE(0x050d, 0x7050) }, /* Belkin Components F5D7050 */
	{ USB_DEVICE(0x06f8, 0xe000) }, /* Guillemot HWGUSB254 */
	{ USB_DEVICE(0x0769, 0x11f3) }, /* Surecom RT2570 */
	{ USB_DEVICE(0x0b05, 0x1706) }, /* Ralink (XXX) RT2500USB */
	{ USB_DEVICE(0x0b05, 0x1707) }, /* Asus WL167G */
	{ USB_DEVICE(0x0db0, 0x6861) }, /* MSI RT2570 */
	{ USB_DEVICE(0x0db0, 0x6865) }, /* MSI RT2570 */
	{ USB_DEVICE(0x0db0, 0x6869) }, /* MSI RT2570 */
	{ USB_DEVICE(0x0eb0, 0x9020) }, /* Nova Technology NV-902W */
	{ USB_DEVICE(0x0f88, 0x3012) }, /* VTech RT2570  */
	{ USB_DEVICE(0x1044, 0x8007) }, /* GIGABYTE GN-WBKG */
	{ USB_DEVICE(0x114b, 0x0110) }, /* Sphairon UB801R */
	{ USB_DEVICE(0x148f, 0x1706) }, /* Ralink RT2570 */
	{ USB_DEVICE(0x148f, 0x2570) }, /* Ralink RT2570 */
	{ USB_DEVICE(0x148f, 0x9020) }, /* Ralink RT2570 */
	{ USB_DEVICE(0x14b2, 0x3c02) }, /* Conceptronic C54RU */
	{ USB_DEVICE(0x1737, 0x000d) }, /* Linksys WUSB54G */
	{ USB_DEVICE(0x1737, 0x0011) }, /* Linksys WUSB54GP */
	{ USB_DEVICE(0x1737, 0x001a) }, /* Linksys HU200TS */
	{ USB_DEVICE(0x2001, 0x3c00) }, /* D-Link DWL-G122 */
	{ USB_DEVICE(0x5a57, 0x0260) }, /* Zinwell RT2570 */
};

static int		ural_alloc_tx_list(struct ural_softc *);
static void		ural_free_tx_list(struct ural_softc *);
static int		ural_alloc_rx_list(struct ural_softc *);
static void		ural_free_rx_list(struct ural_softc *);
static int		ural_media_change(struct ifnet *);
static void		ural_next_scan(void *);
static void		ural_task(void *);
static int		ural_newstate(struct ieee80211com *,
			    enum ieee80211_state, int);
static int		ural_rxrate(struct ural_rx_desc *);
static void		ural_txeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
static void		ural_rxeof(usbd_xfer_handle, usbd_private_handle,
			    usbd_status);
static uint8_t		ural_plcp_signal(int);
static void		ural_setup_tx_desc(struct ural_softc *,
			    struct ural_tx_desc *, uint32_t, int, int);
static int		ural_tx_bcn(struct ural_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		ural_tx_mgt(struct ural_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		ural_tx_data(struct ural_softc *, struct mbuf *,
			    struct ieee80211_node *);
static void		ural_start(struct ifnet *);
static void		ural_watchdog(struct ifnet *);
static int		ural_reset(struct ifnet *);
static int		ural_ioctl(struct ifnet *, u_long, caddr_t,
			    struct ucred *);
static void		ural_set_testmode(struct ural_softc *);
static void		ural_eeprom_read(struct ural_softc *, uint16_t, void *,
			    int);
static uint16_t		ural_read(struct ural_softc *, uint16_t);
static void		ural_read_multi(struct ural_softc *, uint16_t, void *,
			    int);
static void		ural_write(struct ural_softc *, uint16_t, uint16_t);
static void		ural_write_multi(struct ural_softc *, uint16_t, void *,
			    int) __unused;
static void		ural_bbp_write(struct ural_softc *, uint8_t, uint8_t);
static uint8_t		ural_bbp_read(struct ural_softc *, uint8_t);
static void		ural_rf_write(struct ural_softc *, uint8_t, uint32_t);
static void		ural_set_chan(struct ural_softc *,
			    struct ieee80211_channel *);
static void		ural_disable_rf_tune(struct ural_softc *);
static void		ural_enable_tsf_sync(struct ural_softc *);
static void		ural_update_slot(struct ifnet *);
static void		ural_set_txpreamble(struct ural_softc *);
static void		ural_set_basicrates(struct ural_softc *);
static void		ural_set_bssid(struct ural_softc *, uint8_t *);
static void		ural_set_macaddr(struct ural_softc *, uint8_t *);
static void		ural_update_promisc(struct ural_softc *);
static const char	*ural_get_rf(int);
static void		ural_read_eeprom(struct ural_softc *);
static int		ural_bbp_init(struct ural_softc *);
static void		ural_set_txantenna(struct ural_softc *, int);
static void		ural_set_rxantenna(struct ural_softc *, int);
static void		ural_init(void *);
static void		ural_stop(struct ural_softc *);
static void		ural_stats(struct ieee80211com *,
				   struct ieee80211_node *,
				   struct ieee80211_ratectl_stats *);
static void		ural_stats_update(usbd_xfer_handle,
					  usbd_private_handle, usbd_status);
static void		ural_stats_timeout(void *);
static void		*ural_ratectl_attach(struct ieee80211com *ic, u_int);

/*
 * Supported rates for 802.11a/b/g modes (in 500Kbps unit).
 */
static const struct ieee80211_rateset ural_rateset_11a =
	{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };

static const struct ieee80211_rateset ural_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

static const struct ieee80211_rateset ural_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

/*
 * Default values for MAC registers; values taken from the reference driver.
 */
static const struct {
	uint16_t	reg;
	uint16_t	val;
} ural_def_mac[] = {
	{ RAL_TXRX_CSR5,  0x8c8d },
	{ RAL_TXRX_CSR6,  0x8b8a },
	{ RAL_TXRX_CSR7,  0x8687 },
	{ RAL_TXRX_CSR8,  0x0085 },
	{ RAL_MAC_CSR13,  0x1111 },
	{ RAL_MAC_CSR14,  0x1e11 },
	{ RAL_TXRX_CSR21, 0xe78f },
	{ RAL_MAC_CSR9,   0xff1d },
	{ RAL_MAC_CSR11,  0x0002 },
	{ RAL_MAC_CSR22,  0x0053 },
	{ RAL_MAC_CSR15,  0x0000 },
	{ RAL_MAC_CSR8,   0x0780 },
	{ RAL_TXRX_CSR19, 0x0000 },
	{ RAL_TXRX_CSR18, 0x005a },
	{ RAL_PHY_CSR2,   0x0000 },
	{ RAL_TXRX_CSR0,  0x1ec0 },
	{ RAL_PHY_CSR4,   0x000f }
};

/*
 * Default values for BBP registers; values taken from the reference driver.
 */
static const struct {
	uint8_t	reg;
	uint8_t	val;
} ural_def_bbp[] = {
	{  3, 0x02 },
	{  4, 0x19 },
	{ 14, 0x1c },
	{ 15, 0x30 },
	{ 16, 0xac },
	{ 17, 0x48 },
	{ 18, 0x18 },
	{ 19, 0xff },
	{ 20, 0x1e },
	{ 21, 0x08 },
	{ 22, 0x08 },
	{ 23, 0x08 },
	{ 24, 0x80 },
	{ 25, 0x50 },
	{ 26, 0x08 },
	{ 27, 0x23 },
	{ 30, 0x10 },
	{ 31, 0x2b },
	{ 32, 0xb9 },
	{ 34, 0x12 },
	{ 35, 0x50 },
	{ 39, 0xc4 },
	{ 40, 0x02 },
	{ 41, 0x60 },
	{ 53, 0x10 },
	{ 54, 0x18 },
	{ 56, 0x08 },
	{ 57, 0x10 },
	{ 58, 0x08 },
	{ 61, 0x60 },
	{ 62, 0x10 },
	{ 75, 0xff }
};

/*
 * Default values for RF register R2 indexed by channel numbers.
 */
static const uint32_t ural_rf2522_r2[] = {
	0x307f6, 0x307fb, 0x30800, 0x30805, 0x3080a, 0x3080f, 0x30814,
	0x30819, 0x3081e, 0x30823, 0x30828, 0x3082d, 0x30832, 0x3083e
};

static const uint32_t ural_rf2523_r2[] = {
	0x00327, 0x00328, 0x00329, 0x0032a, 0x0032b, 0x0032c, 0x0032d,
	0x0032e, 0x0032f, 0x00340, 0x00341, 0x00342, 0x00343, 0x00346
};

static const uint32_t ural_rf2524_r2[] = {
	0x00327, 0x00328, 0x00329, 0x0032a, 0x0032b, 0x0032c, 0x0032d,
	0x0032e, 0x0032f, 0x00340, 0x00341, 0x00342, 0x00343, 0x00346
};

static const uint32_t ural_rf2525_r2[] = {
	0x20327, 0x20328, 0x20329, 0x2032a, 0x2032b, 0x2032c, 0x2032d,
	0x2032e, 0x2032f, 0x20340, 0x20341, 0x20342, 0x20343, 0x20346
};

static const uint32_t ural_rf2525_hi_r2[] = {
	0x2032f, 0x20340, 0x20341, 0x20342, 0x20343, 0x20344, 0x20345,
	0x20346, 0x20347, 0x20348, 0x20349, 0x2034a, 0x2034b, 0x2034e
};

static const uint32_t ural_rf2525e_r2[] = {
	0x2044d, 0x2044e, 0x2044f, 0x20460, 0x20461, 0x20462, 0x20463,
	0x20464, 0x20465, 0x20466, 0x20467, 0x20468, 0x20469, 0x2046b
};

static const uint32_t ural_rf2526_hi_r2[] = {
	0x0022a, 0x0022b, 0x0022b, 0x0022c, 0x0022c, 0x0022d, 0x0022d,
	0x0022e, 0x0022e, 0x0022f, 0x0022d, 0x00240, 0x00240, 0x00241
};

static const uint32_t ural_rf2526_r2[] = {
	0x00226, 0x00227, 0x00227, 0x00228, 0x00228, 0x00229, 0x00229,
	0x0022a, 0x0022a, 0x0022b, 0x0022b, 0x0022c, 0x0022c, 0x0022d
};

/*
 * For dual-band RF, RF registers R1 and R4 also depend on channel number;
 * values taken from the reference driver.
 */
static const struct {
	uint8_t		chan;
	uint32_t	r1;
	uint32_t	r2;
	uint32_t	r4;
} ural_rf5222[] = {
	{   1, 0x08808, 0x0044d, 0x00282 },
	{   2, 0x08808, 0x0044e, 0x00282 },
	{   3, 0x08808, 0x0044f, 0x00282 },
	{   4, 0x08808, 0x00460, 0x00282 },
	{   5, 0x08808, 0x00461, 0x00282 },
	{   6, 0x08808, 0x00462, 0x00282 },
	{   7, 0x08808, 0x00463, 0x00282 },
	{   8, 0x08808, 0x00464, 0x00282 },
	{   9, 0x08808, 0x00465, 0x00282 },
	{  10, 0x08808, 0x00466, 0x00282 },
	{  11, 0x08808, 0x00467, 0x00282 },
	{  12, 0x08808, 0x00468, 0x00282 },
	{  13, 0x08808, 0x00469, 0x00282 },
	{  14, 0x08808, 0x0046b, 0x00286 },

	{  36, 0x08804, 0x06225, 0x00287 },
	{  40, 0x08804, 0x06226, 0x00287 },
	{  44, 0x08804, 0x06227, 0x00287 },
	{  48, 0x08804, 0x06228, 0x00287 },
	{  52, 0x08804, 0x06229, 0x00287 },
	{  56, 0x08804, 0x0622a, 0x00287 },
	{  60, 0x08804, 0x0622b, 0x00287 },
	{  64, 0x08804, 0x0622c, 0x00287 },

	{ 100, 0x08804, 0x02200, 0x00283 },
	{ 104, 0x08804, 0x02201, 0x00283 },
	{ 108, 0x08804, 0x02202, 0x00283 },
	{ 112, 0x08804, 0x02203, 0x00283 },
	{ 116, 0x08804, 0x02204, 0x00283 },
	{ 120, 0x08804, 0x02205, 0x00283 },
	{ 124, 0x08804, 0x02206, 0x00283 },
	{ 128, 0x08804, 0x02207, 0x00283 },
	{ 132, 0x08804, 0x02208, 0x00283 },
	{ 136, 0x08804, 0x02209, 0x00283 },
	{ 140, 0x08804, 0x0220a, 0x00283 },

	{ 149, 0x08808, 0x02429, 0x00281 },
	{ 153, 0x08808, 0x0242b, 0x00281 },
	{ 157, 0x08808, 0x0242d, 0x00281 },
	{ 161, 0x08808, 0x0242f, 0x00281 }
};

static device_probe_t ural_match;
static device_attach_t ural_attach;
static device_detach_t ural_detach;

static devclass_t ural_devclass;

static kobj_method_t ural_methods[] = {
	DEVMETHOD(device_probe, ural_match),
	DEVMETHOD(device_attach, ural_attach),
	DEVMETHOD(device_detach, ural_detach),
	{0,0}
};

static driver_t ural_driver = {
	"ural",
	ural_methods,
	sizeof(struct ural_softc)
};

DRIVER_MODULE(ural, uhub, ural_driver, ural_devclass, usbd_driver_load, NULL);

MODULE_DEPEND(ural, usb, 1, 1, 1);
MODULE_DEPEND(ural, wlan, 1, 1, 1);
MODULE_DEPEND(ural, wlan_ratectl_onoe, 1, 1, 1);

static int
ural_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	return (usb_lookup(ural_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
ural_attach(device_t self)
{
	struct ural_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ifnet *ifp;
	struct ieee80211com *ic = &sc->sc_ic;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_tx_retries = 7;	/* TODO tunable/sysctl */

	sc->sc_dev = self;

	if (usbd_set_config_no(sc->sc_udev, RAL_CONFIG_NO, 0) != 0) {
		kprintf("%s: could not set configuration no\n",
		    device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, RAL_IFACE_INDEX,
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
			kprintf("%s: no endpoint descriptor for %d\n",
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

	usb_init_task(&sc->sc_task, ural_task, sc);
	callout_init(&sc->scan_ch);
	callout_init(&sc->stats_ch);

	/* retrieve RT2570 rev. no */
	sc->asic_rev = ural_read(sc, RAL_MAC_CSR0);

	/* retrieve MAC address and various other things from EEPROM */
	ural_read_eeprom(sc);

	kprintf("%s: MAC/BBP RT2570 (rev 0x%02x), RF %s\n",
	    device_get_nameunit(sc->sc_dev), sc->asic_rev, ural_get_rf(sc->rf_rev));

	ifp = &ic->ic_if;
	ifp->if_softc = sc;
	if_initname(ifp, "ural", device_get_unit(sc->sc_dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = ural_init;
	ifp->if_ioctl = ural_ioctl;
	ifp->if_start = ural_start;
	ifp->if_watchdog = ural_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	IEEE80211_ONOE_PARAM_SETUP(&sc->sc_onoe_param);
	sc->sc_onoe_param.onoe_raise = 20;
	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_ONOE;
	ic->ic_ratectl.rc_st_stats = ural_stats;
	ic->ic_ratectl.rc_st_attach = ural_ratectl_attach;

	ic->ic_phytype = IEEE80211_T_OFDM; /* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA; /* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_HOSTAP |	/* HostAp mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WPA;		/* 802.11i */

	if (sc->rf_rev == RAL_RF_5222) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] = ural_rateset_11a;

		/* set supported .11a channels */
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
		for (i = 149; i <= 161; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
	}

	/* set supported .11b and .11g rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ural_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ural_rateset_11g;

	/* set supported .11b and .11g channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	sc->sc_sifs = IEEE80211_DUR_SIFS;	/* Default SIFS */

	ieee80211_ifattach(ic);
	ic->ic_reset = ural_reset;
	/* enable s/w bmiss handling in sta mode */
	ic->ic_flags_ext |= IEEE80211_FEXT_SWBMISS;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ural_newstate;
	ieee80211_media_init(ic, ural_media_change, ieee80211_media_status);

	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64, &sc->sc_drvbpf);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RAL_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RAL_TX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;
}

static int
ural_detach(device_t self)
{
	struct ural_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
#ifdef INVARIANTS
	int i;
#endif

	crit_enter();

	callout_stop(&sc->scan_ch);
	callout_stop(&sc->stats_ch);

	lwkt_serialize_enter(ifp->if_serializer);
	ural_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	usb_rem_task(sc->sc_udev, &sc->sc_task);

	bpfdetach(ifp);
	ieee80211_ifdetach(ic);

	crit_exit();

	KKASSERT(sc->stats_xfer == NULL);
	KKASSERT(sc->sc_rx_pipeh == NULL);
	KKASSERT(sc->sc_tx_pipeh == NULL);

#ifdef INVARIANTS
	/*
	 * Make sure TX/RX list is empty
	 */
	for (i = 0; i < RAL_TX_LIST_COUNT; i++) {
		struct ural_tx_data *data = &sc->tx_data[i];

		KKASSERT(data->xfer == NULL);
		KKASSERT(data->ni == NULL);
		KKASSERT(data->m == NULL);
	}
	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

		KKASSERT(data->xfer == NULL);
		KKASSERT(data->m == NULL);
	}
#endif

	return 0;
}

static int
ural_alloc_tx_list(struct ural_softc *sc)
{
	int i;

	sc->tx_queued = 0;

	for (i = 0; i < RAL_TX_LIST_COUNT; i++) {
		struct ural_tx_data *data = &sc->tx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			kprintf("%s: could not allocate tx xfer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		data->buf = usbd_alloc_buffer(data->xfer,
		    RAL_TX_DESC_SIZE + MCLBYTES);
		if (data->buf == NULL) {
			kprintf("%s: could not allocate tx buffer\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}
	}
	return 0;
}

static void
ural_free_tx_list(struct ural_softc *sc)
{
	int i;

	for (i = 0; i < RAL_TX_LIST_COUNT; i++) {
		struct ural_tx_data *data = &sc->tx_data[i];

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
ural_alloc_rx_list(struct ural_softc *sc)
{
	int i;

	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

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

		data->m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (data->m == NULL) {
			kprintf("%s: could not allocate rx mbuf\n",
			    device_get_nameunit(sc->sc_dev));
			return ENOMEM;
		}

		data->buf = mtod(data->m, uint8_t *);
	}
	return 0;
}

static void
ural_free_rx_list(struct ural_softc *sc)
{
	int i;

	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

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
ural_media_change(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		ural_init(sc);

	return 0;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
static void
ural_next_scan(void *arg)
{
	struct ural_softc *sc = arg;
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
ural_task(void *xarg)
{
	struct ural_softc *sc = xarg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	enum ieee80211_state nstate;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int arg;

	if (sc->sc_stopped)
		return;

	crit_enter();

	nstate = sc->sc_state;
	arg = sc->sc_arg;

	KASSERT(nstate != IEEE80211_S_INIT,
		("->INIT state transition should not be defered\n"));
	ural_set_chan(sc, ic->ic_curchan);

	switch (sc->sc_state) {
	case IEEE80211_S_RUN:
		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			ural_update_slot(&ic->ic_if);
			ural_set_txpreamble(sc);
			ural_set_basicrates(sc);
			ural_set_bssid(sc, ni->ni_bssid);
		}

		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
		    	lwkt_serialize_enter(ifp->if_serializer);
			m = ieee80211_beacon_alloc(ic, ni, &sc->sc_bo);
			lwkt_serialize_exit(ifp->if_serializer);

			if (m == NULL) {
				kprintf("%s: could not allocate beacon\n",
				    device_get_nameunit(sc->sc_dev));
				crit_exit();
				return;
			}

			if (ural_tx_bcn(sc, m, ni) != 0) {
				kprintf("%s: could not send beacon\n",
				    device_get_nameunit(sc->sc_dev));
				crit_exit();
				return;
			}
		}

		/* make tx led blink on tx (controlled by ASIC) */
		ural_write(sc, RAL_MAC_CSR20, 1);

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			ural_enable_tsf_sync(sc);

		/* clear statistic registers (STA_CSR0 to STA_CSR10) */
		ural_read_multi(sc, RAL_STA_CSR0, sc->sta, sizeof(sc->sta));

		callout_reset(&sc->stats_ch, 4 * hz / 5,
			      ural_stats_timeout, sc);
		break;

	case IEEE80211_S_SCAN:
		callout_reset(&sc->scan_ch, hz / 5, ural_next_scan, sc);
		break;

	default:
		break;
	}

	lwkt_serialize_enter(ifp->if_serializer);
	ieee80211_ratectl_newstate(ic, sc->sc_state);
	sc->sc_newstate(ic, sc->sc_state, arg);
	lwkt_serialize_exit(ifp->if_serializer);

	crit_exit();
}

static int
ural_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ural_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

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
#define RAL_RATE_IS_OFDM(rate) ((rate) >= 12 && (rate) != 22)

#define RAL_ACK_SIZE	(sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN)

#define RAL_RXTX_TURNAROUND	5	/* us */

/*
 * This function is only used by the Rx radiotap code.
 */
static int
ural_rxrate(struct ural_rx_desc *desc)
{
	if (le32toh(desc->flags) & RAL_RX_OFDM) {
		/* reverse function of ural_plcp_signal */
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

static void
ural_txeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ural_tx_data *data = priv;
	struct ural_softc *sc = data->sc;
	struct ieee80211_node *ni;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

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
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipeh);

		ifp->if_oerrors++;
		crit_exit();
		return;
	}

	m_freem(data->m);
	data->m = NULL;
	ni = data->ni;
	data->ni = NULL;

	sc->tx_queued--;
	ifp->if_opackets++;

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
ural_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ural_rx_data *data = priv;
	struct ural_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ural_rx_desc *desc;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int len;

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

	if (len < RAL_RX_DESC_SIZE + IEEE80211_MIN_LEN) {
		DPRINTF(("%s: xfer too short %d\n", device_get_nameunit(sc->sc_dev),
		    len));
		ifp->if_ierrors++;
		goto skip;
	}

	/* rx descriptor is located at the end */
	desc = (struct ural_rx_desc *)(data->buf + len - RAL_RX_DESC_SIZE);

	if ((le32toh(desc->flags) & RAL_RX_PHY_ERROR) ||
	    (le32toh(desc->flags) & RAL_RX_CRC_ERROR)) {
		/*
		 * This should not happen since we did not request to receive
		 * those frames when we filled RAL_TXRX_CSR2.
		 */
		DPRINTFN(5, ("PHY or CRC error\n"));
		ifp->if_ierrors++;
		goto skip;
	}

	mnew = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (mnew == NULL) {
		ifp->if_ierrors++;
		goto skip;
	}

	m = data->m;
	data->m = NULL;
	data->buf = NULL;

	lwkt_serialize_enter(ifp->if_serializer);

	/* finalize mbuf */
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = (le32toh(desc->flags) >> 16) & 0xfff;

	if (sc->sc_drvbpf != NULL) {
		struct ural_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = IEEE80211_RADIOTAP_F_FCS; /* h/w leaves FCS */
		tap->wr_rate = ural_rxrate(desc);
		tap->wr_chan_freq = htole16(ic->ic_curchan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_curchan->ic_flags);
		tap->wr_antenna = sc->rx_ant;
		tap->wr_antsignal = URAL_RSSI(desc->rssi);

		bpf_ptap(sc->sc_drvbpf, m, tap, sc->sc_rxtap_len);
	}

	/* trim CRC here so WEP can find its own CRC at the end of packet. */
	m_adj(m, -IEEE80211_CRC_LEN);

	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, (struct ieee80211_frame_min *)wh);

	/* send the frame to the 802.11 layer */
	ieee80211_input(ic, m, ni, URAL_RSSI(desc->rssi), 0);

	/* node is no longer needed */
	ieee80211_free_node(ni);

	lwkt_serialize_exit(ifp->if_serializer);

	data->m = mnew;
	data->buf = mtod(data->m, uint8_t *);

	DPRINTFN(15, ("rx done\n"));

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->sc_rx_pipeh, data, data->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, ural_rxeof);
	usbd_transfer(xfer);

	crit_exit();
}

static uint8_t
ural_plcp_signal(int rate)
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
ural_setup_tx_desc(struct ural_softc *sc, struct ural_tx_desc *desc,
    uint32_t flags, int len, int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(RAL_TX_NEWSEQ);
	desc->flags |= htole32(len << 16);

	desc->wme = htole16(RAL_AIFSN(2) | RAL_LOGCWMIN(3) | RAL_LOGCWMAX(5));
	desc->wme |= htole16(RAL_IVOFFSET(sizeof (struct ieee80211_frame)));

	/* setup PLCP fields */
	desc->plcp_signal  = ural_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RAL_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RAL_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RAL_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}

	desc->iv = 0;
	desc->eiv = 0;
}

#define RAL_TX_TIMEOUT	5000

static int
ural_tx_bcn(struct ural_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ural_tx_desc *desc;
	usbd_xfer_handle xfer;
	uint8_t cmd = 0;
	usbd_status error;
	uint8_t *buf;
	int xferlen, rate;

	rate = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? 12 : 2;

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL)
		return ENOMEM;

	/* xfer length needs to be a multiple of two! */
	xferlen = (RAL_TX_DESC_SIZE + m0->m_pkthdr.len + 1) & ~1;

	buf = usbd_alloc_buffer(xfer, xferlen);
	if (buf == NULL) {
		usbd_free_xfer(xfer);
		return ENOMEM;
	}

	usbd_setup_xfer(xfer, sc->sc_tx_pipeh, NULL, &cmd, sizeof cmd,
	    USBD_FORCE_SHORT_XFER, RAL_TX_TIMEOUT, NULL);

	error = usbd_sync_transfer(xfer);
	if (error != 0) {
		usbd_free_xfer(xfer);
		return error;
	}

	desc = (struct ural_tx_desc *)buf;

	m_copydata(m0, 0, m0->m_pkthdr.len, buf + RAL_TX_DESC_SIZE);
	ural_setup_tx_desc(sc, desc, RAL_TX_IFS_NEWBACKOFF | RAL_TX_TIMESTAMP,
	    m0->m_pkthdr.len, rate);

	DPRINTFN(10, ("sending beacon frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len, rate, xferlen));

	usbd_setup_xfer(xfer, sc->sc_tx_pipeh, NULL, buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RAL_TX_TIMEOUT, NULL);

	error = usbd_sync_transfer(xfer);
	usbd_free_xfer(xfer);

	return error;
}

static int
ural_tx_mgt(struct ural_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ural_tx_desc *desc;
	struct ural_tx_data *data;
	struct ieee80211_frame *wh;
	uint32_t flags = 0;
	uint16_t dur;
	usbd_status error;
	int xferlen, rate;

	data = &sc->tx_data[0];
	desc = (struct ural_tx_desc *)data->buf;

	rate = IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan) ? 12 : 2;

	data->m = m0;
	data->ni = ni;

	wh = mtod(m0, struct ieee80211_frame *);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RAL_TX_ACK;

		dur = ieee80211_txtime(ni, RAL_ACK_SIZE, rate, ic->ic_flags) +
		      sc->sc_sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);

		/* tell hardware to add timestamp for probe responses */
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT &&
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= RAL_TX_TIMESTAMP;
	}

	if (sc->sc_drvbpf != NULL) {
		struct ural_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_curchan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_curchan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		bpf_ptap(sc->sc_drvbpf, m0, tap, sc->sc_txtap_len);
	}

	m_copydata(m0, 0, m0->m_pkthdr.len, data->buf + RAL_TX_DESC_SIZE);
	ural_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate);

	/* align end on a 2-bytes boundary */
	xferlen = (RAL_TX_DESC_SIZE + m0->m_pkthdr.len + 1) & ~1;

	/*
	 * No space left in the last URB to store the extra 2 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 2;

	DPRINTFN(10, ("sending mgt frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len, rate, xferlen));

	lwkt_serialize_exit(ifp->if_serializer);

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf,
	    xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RAL_TX_TIMEOUT,
	    ural_txeof);

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

static int
ural_tx_data(struct ural_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ural_tx_desc *desc;
	struct ural_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	uint32_t flags = 0;
	uint16_t dur;
	usbd_status error;
	int xferlen, rate, rate_idx;

	wh = mtod(m0, struct ieee80211_frame *);

	ieee80211_ratectl_findrate(ni, m0->m_pkthdr.len, &rate_idx, 1);
	rate = IEEE80211_RS_RATE(&ni->ni_rates, rate_idx);

	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = ieee80211_crypto_encap(ic, ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	data = &sc->tx_data[0];
	desc = (struct ural_tx_desc *)data->buf;

	data->m = m0;
	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RAL_TX_ACK;
		flags |= RAL_TX_RETRY(sc->sc_tx_retries);

		dur = ieee80211_txtime(ni, RAL_ACK_SIZE,
			ieee80211_ack_rate(ni, rate), ic->ic_flags) +
			sc->sc_sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

	if (sc->sc_drvbpf != NULL) {
		struct ural_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_curchan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_curchan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		bpf_ptap(sc->sc_drvbpf, m0, tap, sc->sc_txtap_len);
	}

	m_copydata(m0, 0, m0->m_pkthdr.len, data->buf + RAL_TX_DESC_SIZE);
	ural_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate);

	/* align end on a 2-bytes boundary */
	xferlen = (RAL_TX_DESC_SIZE + m0->m_pkthdr.len + 1) & ~1;

	/*
	 * No space left in the last URB to store the extra 2 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 2;

	DPRINTFN(10, ("sending data frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len, rate, xferlen));

	lwkt_serialize_exit(ifp->if_serializer);

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf,
	    xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RAL_TX_TIMEOUT,
	    ural_txeof);

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
ural_start(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_stopped) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	crit_enter();

	if ((ifp->if_flags & (IFF_OACTIVE | IFF_RUNNING)) != IFF_RUNNING) {
		crit_exit();
		return;
	}

	for (;;) {
		struct ieee80211_node *ni;
		struct mbuf *m0;

		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			if (sc->tx_queued >= RAL_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			IF_DEQUEUE(&ic->ic_mgtq, m0);

			ni = (struct ieee80211_node *)m0->m_pkthdr.rcvif;
			m0->m_pkthdr.rcvif = NULL;

			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0);

			if (ural_tx_mgt(sc, m0, ni) != 0) {
				ieee80211_free_node(ni);
				break;
			}
		} else {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN) {
				ifq_purge(&ifp->if_snd);
				break;
			}

			if (sc->tx_queued >= RAL_TX_LIST_COUNT) {
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}

			m0 = ifq_dequeue(&ifp->if_snd, NULL);
			if (m0 == NULL)
				break;

			if (m0->m_len < sizeof (struct ether_header)) {
				m0 = m_pullup(m0, sizeof (struct ether_header));
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

			if (ural_tx_data(sc, m0, ni) != 0) {
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
ural_watchdog(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			device_printf(sc->sc_dev, "device timeout\n");
			/*ural_init(sc); XXX needs a process context! */
			ifp->if_oerrors++;

			crit_exit();
			return;
		}
		ifp->if_timer = 1;
	}
	ieee80211_watchdog(ic);

	crit_exit();
}

/*
 * This function allows for fast channel switching in monitor mode (used by
 * net-mgmt/kismet). In IBSS mode, we must explicitly reset the interface to
 * generate a new beacon frame.
 */
static int
ural_reset(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		return ENETRESET;

	crit_enter();

	lwkt_serialize_exit(ifp->if_serializer);
	ural_set_chan(sc, ic->ic_curchan);
	lwkt_serialize_enter(ifp->if_serializer);

	crit_exit();

	return 0;
}

static int
ural_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				lwkt_serialize_exit(ifp->if_serializer);
				ural_update_promisc(sc);
				lwkt_serialize_enter(ifp->if_serializer);
			} else {
				ural_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ural_stop(sc);
		}
		break;

	default:
		error = ieee80211_ioctl(ic, cmd, data, cr);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING) &&
		    ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
			ural_init(sc);
		error = 0;
	}

	crit_exit();
	return error;
}

static void
ural_set_testmode(struct ural_softc *sc)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RAL_VENDOR_REQUEST;
	USETW(req.wValue, 4);
	USETW(req.wIndex, 1);
	USETW(req.wLength, 0);

	error = usbd_do_request(sc->sc_udev, &req, NULL);
	if (error != 0) {
		kprintf("%s: could not set test mode: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
ural_eeprom_read(struct ural_softc *sc, uint16_t addr, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_EEPROM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not read EEPROM: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static uint16_t
ural_read(struct ural_softc *sc, uint16_t reg)
{
	usb_device_request_t req;
	usbd_status error;
	uint16_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, sizeof (uint16_t));

	error = usbd_do_request(sc->sc_udev, &req, &val);
	if (error != 0) {
		kprintf("%s: could not read MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
		return 0;
	}

	return le16toh(val);
}

static void
ural_read_multi(struct ural_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not read MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
ural_write(struct ural_softc *sc, uint16_t reg, uint16_t val)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RAL_WRITE_MAC;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	error = usbd_do_request(sc->sc_udev, &req, NULL);
	if (error != 0) {
		kprintf("%s: could not write MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
ural_write_multi(struct ural_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RAL_WRITE_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		kprintf("%s: could not write MAC register: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(error));
	}
}

static void
ural_bbp_write(struct ural_softc *sc, uint8_t reg, uint8_t val)
{
	uint16_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR8) & RAL_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to BBP\n", device_get_nameunit(sc->sc_dev));
		return;
	}

	tmp = reg << 8 | val;
	ural_write(sc, RAL_PHY_CSR7, tmp);
}

static uint8_t
ural_bbp_read(struct ural_softc *sc, uint8_t reg)
{
	uint16_t val;
	int ntries;

	val = RAL_BBP_WRITE | reg << 8;
	ural_write(sc, RAL_PHY_CSR7, val);

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR8) & RAL_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not read BBP\n", device_get_nameunit(sc->sc_dev));
		return 0;
	}

	return ural_read(sc, RAL_PHY_CSR7) & 0xff;
}

static void
ural_rf_write(struct ural_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR10) & RAL_RF_LOBUSY))
			break;
	}
	if (ntries == 5) {
		kprintf("%s: could not write to RF\n", device_get_nameunit(sc->sc_dev));
		return;
	}

	tmp = RAL_RF_BUSY | RAL_RF_20BIT | (val & 0xfffff) << 2 | (reg & 0x3);
	ural_write(sc, RAL_PHY_CSR9,  tmp & 0xffff);
	ural_write(sc, RAL_PHY_CSR10, tmp >> 16);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 0x3, val & 0xfffff));
}

static void
ural_set_chan(struct ural_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t power, tmp;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		power = min(sc->txpow[chan - 1], 31);
	else
		power = 31;

	/* adjust txpower using ifconfig settings */
	power -= (100 - ic->ic_txpowlimit) / 8;

	DPRINTFN(2, ("setting channel to %u, txpower to %u\n", chan, power));

	switch (sc->rf_rev) {
	case RAL_RF_2522:
		ural_rf_write(sc, RAL_RF1, 0x00814);
		ural_rf_write(sc, RAL_RF2, ural_rf2522_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		break;

	case RAL_RF_2523:
		ural_rf_write(sc, RAL_RF1, 0x08804);
		ural_rf_write(sc, RAL_RF2, ural_rf2523_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x38044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2524:
		ural_rf_write(sc, RAL_RF1, 0x0c808);
		ural_rf_write(sc, RAL_RF2, ural_rf2524_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2525:
		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525_hi_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);

		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2525E:
		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525e_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00286 : 0x00282);
		break;

	case RAL_RF_2526:
		ural_rf_write(sc, RAL_RF2, ural_rf2526_hi_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		ural_rf_write(sc, RAL_RF1, 0x08804);

		ural_rf_write(sc, RAL_RF2, ural_rf2526_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		break;

	/* dual-band RF */
	case RAL_RF_5222:
		for (i = 0; ural_rf5222[i].chan != chan; i++)
			; /* EMPTY */

		ural_rf_write(sc, RAL_RF1, ural_rf5222[i].r1);
		ural_rf_write(sc, RAL_RF2, ural_rf5222[i].r2);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		ural_rf_write(sc, RAL_RF4, ural_rf5222[i].r4);
		break;
	}

	if (ic->ic_opmode != IEEE80211_M_MONITOR &&
	    ic->ic_state != IEEE80211_S_SCAN) {
		/* set Japan filter bit for channel 14 */
		tmp = ural_bbp_read(sc, 70);

		tmp &= ~RAL_JAPAN_FILTER;
		if (chan == 14)
			tmp |= RAL_JAPAN_FILTER;

		ural_bbp_write(sc, 70, tmp);

		/* clear CRC errors */
		ural_read(sc, RAL_STA_CSR0);

		DELAY(10000);
		ural_disable_rf_tune(sc);
	}

	sc->sc_sifs = IEEE80211_IS_CHAN_5GHZ(c) ? IEEE80211_DUR_OFDM_SIFS
						: IEEE80211_DUR_SIFS;
}

/*
 * Disable RF auto-tuning.
 */
static void
ural_disable_rf_tune(struct ural_softc *sc)
{
	uint32_t tmp;

	if (sc->rf_rev != RAL_RF_2523) {
		tmp = sc->rf_regs[RAL_RF1] & ~RAL_RF1_AUTOTUNE;
		ural_rf_write(sc, RAL_RF1, tmp);
	}

	tmp = sc->rf_regs[RAL_RF3] & ~RAL_RF3_AUTOTUNE;
	ural_rf_write(sc, RAL_RF3, tmp);

	DPRINTFN(2, ("disabling RF autotune\n"));
}

/*
 * Refer to IEEE Std 802.11-1999 pp. 123 for more information on TSF
 * synchronization.
 */
static void
ural_enable_tsf_sync(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t logcwmin, preload, tmp;

	/* first, disable TSF synchronization */
	ural_write(sc, RAL_TXRX_CSR19, 0);

	tmp = (16 * ic->ic_bss->ni_intval) << 4;
	ural_write(sc, RAL_TXRX_CSR18, tmp);

	logcwmin = (ic->ic_opmode == IEEE80211_M_IBSS) ? 2 : 0;
	preload = (ic->ic_opmode == IEEE80211_M_IBSS) ? 320 : 6;
	tmp = logcwmin << 12 | preload;
	ural_write(sc, RAL_TXRX_CSR20, tmp);

	/* finally, enable TSF synchronization */
	tmp = RAL_ENABLE_TSF | RAL_ENABLE_TBCN;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RAL_ENABLE_TSF_SYNC(1);
	else
		tmp |= RAL_ENABLE_TSF_SYNC(2) | RAL_ENABLE_BEACON_GENERATOR;
	ural_write(sc, RAL_TXRX_CSR19, tmp);

	DPRINTF(("enabling TSF synchronization\n"));
}

static void
ural_update_slot(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t slottime, sifs, eifs;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ? 9 : 20;

	/*
	 * These settings may sound a bit inconsistent but this is what the
	 * reference driver does.
	 */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		sifs = 16 - RAL_RXTX_TURNAROUND;
		eifs = 364;
	} else {
		sifs = 10 - RAL_RXTX_TURNAROUND;
		eifs = 64;
	}

	ural_write(sc, RAL_MAC_CSR10, slottime);
	ural_write(sc, RAL_MAC_CSR11, sifs);
	ural_write(sc, RAL_MAC_CSR12, eifs);
}

static void
ural_set_txpreamble(struct ural_softc *sc)
{
	uint16_t tmp;

	tmp = ural_read(sc, RAL_TXRX_CSR10);

	tmp &= ~RAL_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RAL_SHORT_PREAMBLE;

	ural_write(sc, RAL_TXRX_CSR10, tmp);
}

static void
ural_set_basicrates(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		ural_write(sc, RAL_TXRX_CSR11, 0x3);
	} else if (IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan)) {
		/* 11a basic rates: 6, 12, 24Mbps */
		ural_write(sc, RAL_TXRX_CSR11, 0x150);
	} else {
		/* 11g basic rates: 1, 2, 5.5, 11, 6, 12, 24Mbps */
		ural_write(sc, RAL_TXRX_CSR11, 0x15f);
	}
}

static void
ural_set_bssid(struct ural_softc *sc, uint8_t *bssid)
{
	uint16_t tmp;

	tmp = bssid[0] | bssid[1] << 8;
	ural_write(sc, RAL_MAC_CSR5, tmp);

	tmp = bssid[2] | bssid[3] << 8;
	ural_write(sc, RAL_MAC_CSR6, tmp);

	tmp = bssid[4] | bssid[5] << 8;
	ural_write(sc, RAL_MAC_CSR7, tmp);

	DPRINTF(("setting BSSID to %6D\n", bssid, ":"));
}

static void
ural_set_macaddr(struct ural_softc *sc, uint8_t *addr)
{
	uint16_t tmp;

	tmp = addr[0] | addr[1] << 8;
	ural_write(sc, RAL_MAC_CSR2, tmp);

	tmp = addr[2] | addr[3] << 8;
	ural_write(sc, RAL_MAC_CSR3, tmp);

	tmp = addr[4] | addr[5] << 8;
	ural_write(sc, RAL_MAC_CSR4, tmp);

	DPRINTF(("setting MAC address to %6D\n", addr, ":"));
}

static void
ural_update_promisc(struct ural_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t tmp;

	tmp = ural_read(sc, RAL_TXRX_CSR2);

	tmp &= ~RAL_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RAL_DROP_NOT_TO_ME;

	ural_write(sc, RAL_TXRX_CSR2, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

static const char *
ural_get_rf(int rev)
{
	switch (rev) {
	case RAL_RF_2522:	return "RT2522";
	case RAL_RF_2523:	return "RT2523";
	case RAL_RF_2524:	return "RT2524";
	case RAL_RF_2525:	return "RT2525";
	case RAL_RF_2525E:	return "RT2525e";
	case RAL_RF_2526:	return "RT2526";
	case RAL_RF_5222:	return "RT5222";
	default:		return "unknown";
	}
}

static void
ural_read_eeprom(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;

	ural_eeprom_read(sc, RAL_EEPROM_CONFIG0, &val, 2);
	val = le16toh(val);
	sc->rf_rev =   (val >> 11) & 0x7;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->led_mode = (val >> 6)  & 0x7;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	/* read MAC address */
	ural_eeprom_read(sc, RAL_EEPROM_ADDRESS, ic->ic_myaddr, 6);

	/* read default values for BBP registers */
	ural_eeprom_read(sc, RAL_EEPROM_BBP_BASE, sc->bbp_prom, 2 * 16);

	/* read Tx power for all b/g channels */
	ural_eeprom_read(sc, RAL_EEPROM_TXPOWER, sc->txpow, 14);
}

static int
ural_bbp_init(struct ural_softc *sc)
{
#define N(a)	(sizeof (a) / sizeof ((a)[0]))
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		if (ural_bbp_read(sc, RAL_BBP_VERSION) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for BBP\n");
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < N(ural_def_bbp); i++)
		ural_bbp_write(sc, ural_def_bbp[i].reg, ural_def_bbp[i].val);

#if 0
	/* initialize BBP registers to values stored in EEPROM */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0xff)
			continue;
		ural_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}
#endif

	return 0;
#undef N
}

static void
ural_set_txantenna(struct ural_softc *sc, int antenna)
{
	uint16_t tmp;
	uint8_t tx;

	tx = ural_bbp_read(sc, RAL_BBP_TX) & ~RAL_BBP_ANTMASK;
	if (antenna == 1)
		tx |= RAL_BBP_ANTA;
	else if (antenna == 2)
		tx |= RAL_BBP_ANTB;
	else
		tx |= RAL_BBP_DIVERSITY;

	/* need to force I/Q flip for RF 2525e, 2526 and 5222 */
	if (sc->rf_rev == RAL_RF_2525E || sc->rf_rev == RAL_RF_2526 ||
	    sc->rf_rev == RAL_RF_5222)
		tx |= RAL_BBP_FLIPIQ;

	ural_bbp_write(sc, RAL_BBP_TX, tx);

	/* update values in PHY_CSR5 and PHY_CSR6 */
	tmp = ural_read(sc, RAL_PHY_CSR5) & ~0x7;
	ural_write(sc, RAL_PHY_CSR5, tmp | (tx & 0x7));

	tmp = ural_read(sc, RAL_PHY_CSR6) & ~0x7;
	ural_write(sc, RAL_PHY_CSR6, tmp | (tx & 0x7));
}

static void
ural_set_rxantenna(struct ural_softc *sc, int antenna)
{
	uint8_t rx;

	rx = ural_bbp_read(sc, RAL_BBP_RX) & ~RAL_BBP_ANTMASK;
	if (antenna == 1)
		rx |= RAL_BBP_ANTA;
	else if (antenna == 2)
		rx |= RAL_BBP_ANTB;
	else
		rx |= RAL_BBP_DIVERSITY;

	/* need to force no I/Q flip for RF 2525e and 2526 */
	if (sc->rf_rev == RAL_RF_2525E || sc->rf_rev == RAL_RF_2526)
		rx &= ~RAL_BBP_FLIPIQ;

	ural_bbp_write(sc, RAL_BBP_RX, rx);
}

static void
ural_init(void *priv)
{
#define N(a)	(sizeof (a) / sizeof ((a)[0]))
	struct ural_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ural_rx_data *data;
	uint16_t tmp;
	usbd_status usb_err;
	int i, ntries, error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	lwkt_serialize_exit(ifp->if_serializer);
	ural_set_testmode(sc);
	ural_write(sc, 0x308, 0x00f0);	/* XXX magic */
	lwkt_serialize_enter(ifp->if_serializer);

	ural_stop(sc);
	sc->sc_stopped = 0;

	lwkt_serialize_exit(ifp->if_serializer);

	/* initialize MAC registers to default values */
	for (i = 0; i < N(ural_def_mac); i++)
		ural_write(sc, ural_def_mac[i].reg, ural_def_mac[i].val);

	/* wait for BBP and RF to wake up (this can take a long time!) */
	for (ntries = 0; ntries < 100; ntries++) {
		tmp = ural_read(sc, RAL_MAC_CSR17);
		if ((tmp & (RAL_BBP_AWAKE | RAL_RF_AWAKE)) ==
		    (RAL_BBP_AWAKE | RAL_RF_AWAKE))
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		kprintf("%s: timeout waiting for BBP/RF to wakeup\n",
		    device_get_nameunit(sc->sc_dev));
		error = ETIMEDOUT;
		goto fail;
	}

	/* we're ready! */
	ural_write(sc, RAL_MAC_CSR1, RAL_HOST_READY);

	/* set basic rate set (will be updated later) */
	ural_write(sc, RAL_TXRX_CSR11, 0x15f);

	error = ural_bbp_init(sc);
	if (error)
		goto fail;

	/* set default BSS channel */
	ural_set_chan(sc, ic->ic_curchan);

	/* clear statistic registers (STA_CSR0 to STA_CSR10) */
	ural_read_multi(sc, RAL_STA_CSR0, sc->sta, sizeof sc->sta);

	ural_set_txantenna(sc, sc->tx_ant);
	ural_set_rxantenna(sc, sc->rx_ant);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, IF_LLADDR(ifp));
	ural_set_macaddr(sc, ic->ic_myaddr);

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
	if (usb_err != 0) {
		kprintf("%s: could not open Tx pipe: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(usb_err));
		error = ENOMEM;
		goto fail;
	}

	usb_err = usbd_open_pipe(sc->sc_iface, sc->sc_rx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_rx_pipeh);
	if (usb_err != 0) {
		kprintf("%s: could not open Rx pipe: %s\n",
		    device_get_nameunit(sc->sc_dev), usbd_errstr(usb_err));
		error = ENOMEM;
		goto fail;
	}

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	error = ural_alloc_tx_list(sc);
	if (error) {
		kprintf("%s: could not allocate Tx list\n",
		    device_get_nameunit(sc->sc_dev));
		goto fail;
	}

	error = ural_alloc_rx_list(sc);
	if (error) {
		kprintf("%s: could not allocate Rx list\n",
		    device_get_nameunit(sc->sc_dev));
		goto fail;
	}

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->sc_rx_pipeh, data, data->buf,
		    MCLBYTES, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, ural_rxeof);
		usbd_transfer(data->xfer);
	}

	/* kick Rx */
	tmp = RAL_DROP_PHY | RAL_DROP_CRC;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RAL_DROP_CTL | RAL_DROP_BAD_VERSION;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
			tmp |= RAL_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RAL_DROP_NOT_TO_ME;
	}
	ural_write(sc, RAL_TXRX_CSR2, tmp);

	/* clear statistic registers (STA_CSR0 to STA_CSR10) */
	ural_read_multi(sc, RAL_STA_CSR0, sc->sta, sizeof(sc->sta));
fail:
	lwkt_serialize_enter(ifp->if_serializer);
	if (error) {
		ural_stop(sc);
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
ural_stop(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	crit_enter();

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	sc->sc_stopped = 1;

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;

	lwkt_serialize_exit(ifp->if_serializer);

	/* disable Rx */
	ural_write(sc, RAL_TXRX_CSR2, RAL_DISABLE_RX);

	/* reset ASIC and BBP (but won't reset MAC registers!) */
	ural_write(sc, RAL_MAC_CSR1, RAL_RESET_ASIC | RAL_RESET_BBP);
	ural_write(sc, RAL_MAC_CSR1, 0);

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

	ural_free_rx_list(sc);
	ural_free_tx_list(sc);

	crit_exit();
}

static void
ural_stats_timeout(void *arg)
{
	struct ural_softc *sc = (struct ural_softc *)arg;
	usb_device_request_t req;

	if (sc->sc_stopped)
		return;

	crit_enter();

	/*
	 * Asynchronously read statistic registers (cleared by read).
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, RAL_STA_CSR0);
	USETW(req.wLength, sizeof(sc->sta));

	usbd_setup_default_xfer(sc->stats_xfer, sc->sc_udev, sc,
				USBD_DEFAULT_TIMEOUT, &req,
				sc->sta, sizeof(sc->sta), 0,
				ural_stats_update);
	usbd_transfer(sc->stats_xfer);

	crit_exit();
}

static void
ural_stats_update(usbd_xfer_handle xfer, usbd_private_handle priv,
		  usbd_status status)
{
	struct ural_softc *sc = (struct ural_softc *)priv;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ieee80211_ratectl_stats *stats = &sc->sc_stats;

	if (status != USBD_NORMAL_COMPLETION) {
		device_printf(sc->sc_dev, "could not retrieve Tx statistics - "
		    "cancelling automatic rate control\n");
		return;
	}

	crit_enter();

	/* count TX retry-fail as Tx errors */
	ifp->if_oerrors += sc->sta[RAL_TX_PKT_FAIL];

	stats->stats_pkt_ok += sc->sta[RAL_TX_PKT_NO_RETRY] +
			       sc->sta[RAL_TX_PKT_ONE_RETRY] +
			       sc->sta[RAL_TX_PKT_MULTI_RETRY];

	stats->stats_pkt_err += sc->sta[RAL_TX_PKT_FAIL];

	stats->stats_pkt_noretry += sc->sta[RAL_TX_PKT_NO_RETRY];

	stats->stats_retries += sc->sta[RAL_TX_PKT_ONE_RETRY];
#if 1
	/*
	 * XXX Estimated average:
	 * Actual number of retries for each packet should belong to
	 * [2, sc->sc_tx_retries]
	 */
	stats->stats_retries += sc->sta[RAL_TX_PKT_MULTI_RETRY] *
				((2 + sc->sc_tx_retries) / 2);
#else
	stats->stats_retries += sc->sta[RAL_TX_PKT_MULTI_RETRY];
#endif
	stats->stats_retries += sc->sta[RAL_TX_PKT_FAIL] * sc->sc_tx_retries;

	callout_reset(&sc->stats_ch, 4 * hz / 5, ural_stats_timeout, sc);

	crit_exit();
}

static void
ural_stats(struct ieee80211com *ic, struct ieee80211_node *ni __unused,
	   struct ieee80211_ratectl_stats *stats)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ural_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	bcopy(&sc->sc_stats, stats, sizeof(*stats));
	bzero(&sc->sc_stats, sizeof(sc->sc_stats));
}

static void *
ural_ratectl_attach(struct ieee80211com *ic, u_int rc)
{
	struct ural_softc *sc = ic->ic_if.if_softc;

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
