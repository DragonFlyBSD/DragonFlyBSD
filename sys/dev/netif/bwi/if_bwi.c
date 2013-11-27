/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
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
 */

#include <sys/param.h>
#include <sys/bitops.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>

#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#include <dev/netif/bwi/if_bwireg.h>
#include <dev/netif/bwi/if_bwivar.h>
#include <dev/netif/bwi/bwimac.h>
#include <dev/netif/bwi/bwirf.h>

struct bwi_clock_freq {
	u_int		clkfreq_min;
	u_int		clkfreq_max;
};

struct bwi_myaddr_bssid {
	uint8_t		myaddr[IEEE80211_ADDR_LEN];
	uint8_t		bssid[IEEE80211_ADDR_LEN];
} __packed;

static int	bwi_probe(device_t);
static int	bwi_attach(device_t);
static int	bwi_detach(device_t);
static int	bwi_shutdown(device_t);

static void	bwi_init(void *);
static int	bwi_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bwi_start(struct ifnet *, struct ifaltq_subque *);
static void	bwi_watchdog(struct ifnet *);
static int	bwi_newstate(struct ieee80211com *, enum ieee80211_state, int);
static void	bwi_updateslot(struct ifnet *);
static int	bwi_media_change(struct ifnet *);
static void	*bwi_ratectl_attach(struct ieee80211com *, u_int);

static void	bwi_next_scan(void *);
static void	bwi_calibrate(void *);

static void	bwi_newstate_begin(struct bwi_softc *, enum ieee80211_state);
static void	bwi_init_statechg(struct bwi_softc *, int);
static int	bwi_stop(struct bwi_softc *, int);
static int	bwi_newbuf(struct bwi_softc *, int, int);
static int	bwi_encap(struct bwi_softc *, int, struct mbuf *,
			  struct ieee80211_node **, int);

static void	bwi_init_rxdesc_ring32(struct bwi_softc *, uint32_t,
				       bus_addr_t, int, int);
static void	bwi_reset_rx_ring32(struct bwi_softc *, uint32_t);

static int	bwi_init_tx_ring32(struct bwi_softc *, int);
static int	bwi_init_rx_ring32(struct bwi_softc *);
static int	bwi_init_txstats32(struct bwi_softc *);
static void	bwi_free_tx_ring32(struct bwi_softc *, int);
static void	bwi_free_rx_ring32(struct bwi_softc *);
static void	bwi_free_txstats32(struct bwi_softc *);
static void	bwi_setup_rx_desc32(struct bwi_softc *, int, bus_addr_t, int);
static void	bwi_setup_tx_desc32(struct bwi_softc *, struct bwi_ring_data *,
				    int, bus_addr_t, int);
static int	bwi_rxeof32(struct bwi_softc *);
static void	bwi_start_tx32(struct bwi_softc *, uint32_t, int);
static void	bwi_txeof_status32(struct bwi_softc *);

static int	bwi_init_tx_ring64(struct bwi_softc *, int);
static int	bwi_init_rx_ring64(struct bwi_softc *);
static int	bwi_init_txstats64(struct bwi_softc *);
static void	bwi_free_tx_ring64(struct bwi_softc *, int);
static void	bwi_free_rx_ring64(struct bwi_softc *);
static void	bwi_free_txstats64(struct bwi_softc *);
static void	bwi_setup_rx_desc64(struct bwi_softc *, int, bus_addr_t, int);
static void	bwi_setup_tx_desc64(struct bwi_softc *, struct bwi_ring_data *,
				    int, bus_addr_t, int);
static int	bwi_rxeof64(struct bwi_softc *);
static void	bwi_start_tx64(struct bwi_softc *, uint32_t, int);
static void	bwi_txeof_status64(struct bwi_softc *);

static void	bwi_intr(void *);
static int	bwi_rxeof(struct bwi_softc *, int);
static void	_bwi_txeof(struct bwi_softc *, uint16_t, int, int);
static void	bwi_txeof(struct bwi_softc *);
static void	bwi_txeof_status(struct bwi_softc *, int);
static void	bwi_enable_intrs(struct bwi_softc *, uint32_t);
static void	bwi_disable_intrs(struct bwi_softc *, uint32_t);
static int	bwi_calc_rssi(struct bwi_softc *, const struct bwi_rxbuf_hdr *);
static void	bwi_rx_radiotap(struct bwi_softc *, struct mbuf *,
				struct bwi_rxbuf_hdr *, const void *, int, int);

static int	bwi_dma_alloc(struct bwi_softc *);
static void	bwi_dma_free(struct bwi_softc *);
static int	bwi_dma_ring_alloc(struct bwi_softc *, bus_dma_tag_t,
				   struct bwi_ring_data *, bus_size_t,
				   uint32_t);
static int	bwi_dma_mbuf_create(struct bwi_softc *);
static void	bwi_dma_mbuf_destroy(struct bwi_softc *, int, int);
static int	bwi_dma_txstats_alloc(struct bwi_softc *, uint32_t, bus_size_t);
static void	bwi_dma_txstats_free(struct bwi_softc *);
static void	bwi_dma_ring_addr(void *, bus_dma_segment_t *, int, int);
static void	bwi_dma_buf_addr(void *, bus_dma_segment_t *, int,
				 bus_size_t, int);

static void	bwi_power_on(struct bwi_softc *, int);
static int	bwi_power_off(struct bwi_softc *, int);
static int	bwi_set_clock_mode(struct bwi_softc *, enum bwi_clock_mode);
static int	bwi_set_clock_delay(struct bwi_softc *);
static void	bwi_get_clock_freq(struct bwi_softc *, struct bwi_clock_freq *);
static int	bwi_get_pwron_delay(struct bwi_softc *sc);
static void	bwi_set_addr_filter(struct bwi_softc *, uint16_t,
				    const uint8_t *);
static void	bwi_set_bssid(struct bwi_softc *, const uint8_t *);
static int	bwi_set_chan(struct bwi_softc *, struct ieee80211_channel *);

static void	bwi_get_card_flags(struct bwi_softc *);
static void	bwi_get_eaddr(struct bwi_softc *, uint16_t, uint8_t *);

static int	bwi_bus_attach(struct bwi_softc *);
static int	bwi_bbp_attach(struct bwi_softc *);
static int	bwi_bbp_power_on(struct bwi_softc *, enum bwi_clock_mode);
static void	bwi_bbp_power_off(struct bwi_softc *);

static const char *bwi_regwin_name(const struct bwi_regwin *);
static uint32_t	bwi_regwin_disable_bits(struct bwi_softc *);
static void	bwi_regwin_info(struct bwi_softc *, uint16_t *, uint8_t *);
static int	bwi_regwin_select(struct bwi_softc *, int);

static void	bwi_led_attach(struct bwi_softc *);
static void	bwi_led_newstate(struct bwi_softc *, enum ieee80211_state);
static void	bwi_led_event(struct bwi_softc *, int);
static void	bwi_led_blink_start(struct bwi_softc *, int, int);
static void	bwi_led_blink_next(void *);
static void	bwi_led_blink_end(void *);

static const struct bwi_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} bwi_devices[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4301,
	  "Broadcom BCM4301 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4307,
	  "Broadcom BCM4307 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4311,
	  "Broadcom BCM4311 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4312,
	  "Broadcom BCM4312 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306_1,
	  "Broadcom BCM4306 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306_2,
	  "Broadcom BCM4306 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306_3,
	  "Broadcom BCM4306 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4309,
	  "Broadcom BCM4309 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4318,
	  "Broadcom BCM4318 802.11 Wireless Lan" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4319,
	  "Broadcom BCM4319 802.11 Wireless Lan" }
};

static device_method_t bwi_methods[] = {
	DEVMETHOD(device_probe,		bwi_probe),
	DEVMETHOD(device_attach,	bwi_attach),
	DEVMETHOD(device_detach,	bwi_detach),
	DEVMETHOD(device_shutdown,	bwi_shutdown),
#if 0
	DEVMETHOD(device_suspend,	bwi_suspend),
	DEVMETHOD(device_resume,	bwi_resume),
#endif
	DEVMETHOD_END
};

static driver_t bwi_driver = {
	"bwi",
	bwi_methods,
	sizeof(struct bwi_softc)
};

static devclass_t bwi_devclass;

DRIVER_MODULE(bwi, pci, bwi_driver, bwi_devclass, NULL, NULL);
DRIVER_MODULE(bwi, cardbus, bwi_driver, bwi_devclass, NULL, NULL);

MODULE_DEPEND(bwi, wlan, 1, 1, 1);
MODULE_DEPEND(bwi, wlan_ratectl_onoe, 1, 1, 1);
#if 0
MODULE_DEPEND(bwi, wlan_ratectl_amrr, 1, 1, 1);
#endif
MODULE_DEPEND(bwi, pci, 1, 1, 1);
MODULE_DEPEND(bwi, cardbus, 1, 1, 1);

static const struct {
	uint16_t	did_min;
	uint16_t	did_max;
	uint16_t	bbp_id;
} bwi_bbpid_map[] = {
	{ 0x4301, 0x4301, 0x4301 },
	{ 0x4305, 0x4307, 0x4307 },
	{ 0x4403, 0x4403, 0x4402 },
	{ 0x4610, 0x4615, 0x4610 },
	{ 0x4710, 0x4715, 0x4710 },
	{ 0x4720, 0x4725, 0x4309 }
};

static const struct {
	uint16_t	bbp_id;
	int		nregwin;
} bwi_regwin_count[] = {
	{ 0x4301, 5 },
	{ 0x4306, 6 },
	{ 0x4307, 5 },
	{ 0x4310, 8 },
	{ 0x4401, 3 },
	{ 0x4402, 3 },
	{ 0x4610, 9 },
	{ 0x4704, 9 },
	{ 0x4710, 9 },
	{ 0x5365, 7 }
};

#define CLKSRC(src) 				\
[BWI_CLKSRC_ ## src] = {			\
	.freq_min = BWI_CLKSRC_ ##src## _FMIN,	\
	.freq_max = BWI_CLKSRC_ ##src## _FMAX	\
}

static const struct {
	u_int	freq_min;
	u_int	freq_max;
} bwi_clkfreq[BWI_CLKSRC_MAX] = {
	CLKSRC(LP_OSC),
	CLKSRC(CS_OSC),
	CLKSRC(PCI)
};

#undef CLKSRC

#define VENDOR_LED_ACT(vendor)				\
{							\
	.vid = PCI_VENDOR_##vendor,			\
	.led_act = { BWI_VENDOR_LED_ACT_##vendor }	\
}

static const struct {
	uint16_t	vid;
	uint8_t		led_act[BWI_LED_MAX];
} bwi_vendor_led_act[] = {
	VENDOR_LED_ACT(COMPAQ),
	VENDOR_LED_ACT(LINKSYS)
};

static const uint8_t bwi_default_led_act[BWI_LED_MAX] =
	{ BWI_VENDOR_LED_ACT_DEFAULT };

#undef VENDOR_LED_ACT

static const struct {
	int	on_dur;
	int	off_dur;
} bwi_led_duration[109] = {
	[0]	= { 400, 100 },
	[2]	= { 150, 75 },
	[4]	= { 90, 45 },
	[11]	= { 66, 34 },
	[12]	= { 53, 26 },
	[18]	= { 42, 21 },
	[22]	= { 35, 17 },
	[24]	= { 32, 16 },
	[36]	= { 21, 10 },
	[48]	= { 16, 8 },
	[72]	= { 11, 5 },
	[96]	= { 9, 4 },
	[108]	= { 7, 3 }
};

#ifdef BWI_DEBUG
#ifdef BWI_DEBUG_VERBOSE
static uint32_t	bwi_debug = BWI_DBG_ATTACH | BWI_DBG_INIT | BWI_DBG_TXPOWER;
#else
static uint32_t	bwi_debug;
#endif
TUNABLE_INT("hw.bwi.debug", (int *)&bwi_debug);
#endif	/* BWI_DEBUG */

static const uint8_t bwi_zero_addr[IEEE80211_ADDR_LEN];

static const struct ieee80211_rateset bwi_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };
static const struct ieee80211_rateset bwi_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

uint16_t
bwi_read_sprom(struct bwi_softc *sc, uint16_t ofs)
{
	return CSR_READ_2(sc, ofs + BWI_SPROM_START);
}

static __inline void
bwi_setup_desc32(struct bwi_softc *sc, struct bwi_desc32 *desc_array,
		 int ndesc, int desc_idx, bus_addr_t paddr, int buf_len,
		 int tx)
{
	struct bwi_desc32 *desc = &desc_array[desc_idx];
	uint32_t ctrl, addr, addr_hi, addr_lo;

	addr_lo = __SHIFTOUT(paddr, BWI_DESC32_A_ADDR_MASK);
	addr_hi = __SHIFTOUT(paddr, BWI_DESC32_A_FUNC_MASK);

	addr = __SHIFTIN(addr_lo, BWI_DESC32_A_ADDR_MASK) |
	       __SHIFTIN(BWI_DESC32_A_FUNC_TXRX, BWI_DESC32_A_FUNC_MASK);

	ctrl = __SHIFTIN(buf_len, BWI_DESC32_C_BUFLEN_MASK) |
	       __SHIFTIN(addr_hi, BWI_DESC32_C_ADDRHI_MASK);
	if (desc_idx == ndesc - 1)
		ctrl |= BWI_DESC32_C_EOR;
	if (tx) {
		/* XXX */
		ctrl |= BWI_DESC32_C_FRAME_START |
			BWI_DESC32_C_FRAME_END |
			BWI_DESC32_C_INTR;
	}

	desc->addr = htole32(addr);
	desc->ctrl = htole32(ctrl);
}

/* XXX does not belong here */
uint8_t
bwi_rate2plcp(uint8_t rate)
{
	rate &= IEEE80211_RATE_VAL;

	switch (rate) {
	case 2:		return 0xa;
	case 4:		return 0x14;
	case 11:	return 0x37;
	case 22:	return 0x6e;
	case 44:	return 0xdc;

	case 12:	return 0xb;
	case 18:	return 0xf;
	case 24:	return 0xa;
	case 36:	return 0xe;
	case 48:	return 0x9;
	case 72:	return 0xd;
	case 96:	return 0x8;
	case 108:	return 0xc;

	default:
		panic("unsupported rate %u", rate);
	}
}

/* XXX does not belong here */
#define IEEE80211_OFDM_PLCP_RATE_MASK	__BITS(3, 0)
#define IEEE80211_OFDM_PLCP_LEN_MASK	__BITS(16, 5)

static __inline void
bwi_ofdm_plcp_header(uint32_t *plcp0, int pkt_len, uint8_t rate)
{
	uint32_t plcp;

	plcp = __SHIFTIN(bwi_rate2plcp(rate), IEEE80211_OFDM_PLCP_RATE_MASK) |
	       __SHIFTIN(pkt_len, IEEE80211_OFDM_PLCP_LEN_MASK);
	*plcp0 = htole32(plcp);
}

/* XXX does not belong here */
struct ieee80211_ds_plcp_hdr {
	uint8_t		i_signal;
	uint8_t		i_service;
	uint16_t	i_length;
	uint16_t	i_crc;
} __packed;

#define IEEE80211_DS_PLCP_SERVICE_LOCKED	0x04
#define IEEE80211_DS_PLCL_SERVICE_PBCC		0x08
#define IEEE80211_DS_PLCP_SERVICE_LENEXT5	0x20
#define IEEE80211_DS_PLCP_SERVICE_LENEXT6	0x40
#define IEEE80211_DS_PLCP_SERVICE_LENEXT7	0x80

static __inline void
bwi_ds_plcp_header(struct ieee80211_ds_plcp_hdr *plcp, int pkt_len,
		   uint8_t rate)
{
	int len, service, pkt_bitlen;

	pkt_bitlen = pkt_len * NBBY;
	len = howmany(pkt_bitlen * 2, rate);

	service = IEEE80211_DS_PLCP_SERVICE_LOCKED;
	if (rate == (11 * 2)) {
		int pkt_bitlen1;

		/*
		 * PLCP service field needs to be adjusted,
		 * if TX rate is 11Mbytes/s
		 */
		pkt_bitlen1 = len * 11;
		if (pkt_bitlen1 - pkt_bitlen >= NBBY)
			service |= IEEE80211_DS_PLCP_SERVICE_LENEXT7;
	}

	plcp->i_signal = bwi_rate2plcp(rate);
	plcp->i_service = service;
	plcp->i_length = htole16(len);
	/* NOTE: do NOT touch i_crc */
}

static __inline void
bwi_plcp_header(void *plcp, int pkt_len, uint8_t rate)
{
	enum ieee80211_modtype modtype;

	/*
	 * Assume caller has zeroed 'plcp'
	 */

	modtype = ieee80211_rate2modtype(rate);
	if (modtype == IEEE80211_MODTYPE_OFDM)
		bwi_ofdm_plcp_header(plcp, pkt_len, rate);
	else if (modtype == IEEE80211_MODTYPE_DS)
		bwi_ds_plcp_header(plcp, pkt_len, rate);
	else
		panic("unsupport modulation type %u", modtype);
}

static __inline uint8_t
bwi_ofdm_plcp2rate(const uint32_t *plcp0)
{
	uint32_t plcp;
	uint8_t plcp_rate;

	plcp = le32toh(*plcp0);
	plcp_rate = __SHIFTOUT(plcp, IEEE80211_OFDM_PLCP_RATE_MASK);
	return ieee80211_plcp2rate(plcp_rate, 1);
}

static __inline uint8_t
bwi_ds_plcp2rate(const struct ieee80211_ds_plcp_hdr *hdr)
{
	return ieee80211_plcp2rate(hdr->i_signal, 0);
}

static int
bwi_probe(device_t dev)
{
	const struct bwi_dev *b;
	uint16_t did, vid;

	did = pci_get_device(dev);
	vid = pci_get_vendor(dev);

	for (b = bwi_devices; b->desc != NULL; ++b) {
		if (b->did == did && b->vid == vid) {
			device_set_desc(dev, b->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
bwi_attach(device_t dev)
{
	struct bwi_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	struct bwi_phy *phy;
	char ethstr[ETHER_ADDRSTRLEN + 1];
	int i, error;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	sc->sc_dev = dev;

	/*
	 * Initialize sysctl variables
	 */
	sc->sc_fw_version = BWI_FW_VERSION3;
	sc->sc_dwell_time = 200;
	sc->sc_led_idle = (2350 * hz) / 1000;
	sc->sc_led_blink = 1;
	sc->sc_txpwr_calib = 1;
#ifdef BWI_DEBUG
	sc->sc_debug = bwi_debug;
#endif

	callout_init(&sc->sc_scan_ch);
	callout_init(&sc->sc_calib_ch);

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t irq, mem;

		/* XXX Save more PCIR */
		irq = pci_read_config(dev, PCIR_INTLINE, 4);
		mem = pci_read_config(dev, BWI_PCIR_BAR, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, PCIR_INTLINE, irq, 4);
		pci_write_config(dev, BWI_PCIR_BAR, mem, 4);
	}
#endif	/* !BURN_BRIDGE */

	pci_enable_busmaster(dev);

	/* Get more PCI information */
	sc->sc_pci_revid = pci_get_revid(dev);
	sc->sc_pci_subvid = pci_get_subvendor(dev);
	sc->sc_pci_subdid = pci_get_subdevice(dev);

	/*
	 * Allocate IO memory
	 */
	sc->sc_mem_rid = BWI_PCIR_BAR;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						&sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "can't allocate IO memory\n");
		return ENXIO;
	}
	sc->sc_mem_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_mem_bh = rman_get_bushandle(sc->sc_mem_res);

	/*
	 * Allocate IRQ
	 */
	sc->sc_irq_rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->sc_irq_rid,
						RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "can't allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Create sysctl tree
	 */
	sysctl_ctx_init(&sc->sc_sysctl_ctx);
	sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
					     SYSCTL_STATIC_CHILDREN(_hw),
					     OID_AUTO,
					     device_get_nameunit(dev),
					     CTLFLAG_RD, 0, "");
	if (sc->sc_sysctl_tree == NULL) {
		device_printf(dev, "can't add sysctl node\n");
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_UINT(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
			"dwell_time", CTLFLAG_RW, &sc->sc_dwell_time, 0,
			"Channel dwell time during scan (msec)");
	SYSCTL_ADD_UINT(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
			"fw_version", CTLFLAG_RD, &sc->sc_fw_version, 0,
			"Firmware version");
	SYSCTL_ADD_UINT(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
			"led_idle", CTLFLAG_RW, &sc->sc_led_idle, 0,
			"# ticks before LED enters idle state");
	SYSCTL_ADD_INT(&sc->sc_sysctl_ctx,
		       SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
		       "led_blink", CTLFLAG_RW, &sc->sc_led_blink, 0,
		       "Allow LED to blink");
	SYSCTL_ADD_INT(&sc->sc_sysctl_ctx,
		       SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
		       "txpwr_calib", CTLFLAG_RW, &sc->sc_txpwr_calib, 0,
		       "Enable software TX power calibration");
#ifdef BWI_DEBUG
	SYSCTL_ADD_UINT(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree), OID_AUTO,
			"debug", CTLFLAG_RW, &sc->sc_debug, 0, "Debug flags");
#endif

	bwi_power_on(sc, 1);

	error = bwi_bbp_attach(sc);
	if (error)
		goto fail;

	error = bwi_bbp_power_on(sc, BWI_CLOCK_MODE_FAST);
	if (error)
		goto fail;

	if (BWI_REGWIN_EXIST(&sc->sc_com_regwin)) {
		error = bwi_set_clock_delay(sc);
		if (error)
			goto fail;

		error = bwi_set_clock_mode(sc, BWI_CLOCK_MODE_FAST);
		if (error)
			goto fail;

		error = bwi_get_pwron_delay(sc);
		if (error)
			goto fail;
	}

	error = bwi_bus_attach(sc);
	if (error)
		goto fail;

	bwi_get_card_flags(sc);

	bwi_led_attach(sc);

	for (i = 0; i < sc->sc_nmac; ++i) {
		struct bwi_regwin *old;

		mac = &sc->sc_mac[i];
		error = bwi_regwin_switch(sc, &mac->mac_regwin, &old);
		if (error)
			goto fail;

		error = bwi_mac_lateattach(mac);
		if (error)
			goto fail;

		error = bwi_regwin_switch(sc, old, NULL);
		if (error)
			goto fail;
	}

	/*
	 * XXX First MAC is known to exist
	 * TODO2
	 */
	mac = &sc->sc_mac[0];
	phy = &mac->mac_phy;

	bwi_bbp_power_off(sc);

	error = bwi_dma_alloc(sc);
	if (error)
		goto fail;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = bwi_init;
	ifp->if_ioctl = bwi_ioctl;
	ifp->if_start = bwi_start;
	ifp->if_watchdog = bwi_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
#ifdef notyet
	ifq_set_ready(&ifp->if_snd);
#endif

	/* Get locale */
	sc->sc_locale = __SHIFTOUT(bwi_read_sprom(sc, BWI_SPROM_CARD_INFO),
				   BWI_SPROM_CARD_INFO_LOCALE);
	DPRINTF(sc, BWI_DBG_ATTACH, "locale: %d\n", sc->sc_locale);

	/*
	 * Setup ratesets, phytype, channels and get MAC address
	 */
	if (phy->phy_mode == IEEE80211_MODE_11B ||
	    phy->phy_mode == IEEE80211_MODE_11G) {
	    	uint16_t chan_flags;

		ic->ic_sup_rates[IEEE80211_MODE_11B] = bwi_rateset_11b;

		if (phy->phy_mode == IEEE80211_MODE_11B) {
			chan_flags = IEEE80211_CHAN_B;
			ic->ic_phytype = IEEE80211_T_DS;
		} else {
			chan_flags = IEEE80211_CHAN_CCK |
				     IEEE80211_CHAN_OFDM |
				     IEEE80211_CHAN_DYN |
				     IEEE80211_CHAN_2GHZ;
			ic->ic_phytype = IEEE80211_T_OFDM;
			ic->ic_sup_rates[IEEE80211_MODE_11G] =
				bwi_rateset_11g;
		}

		/* XXX depend on locale */
		for (i = 1; i <= 14; ++i) {
			ic->ic_channels[i].ic_freq =
				ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[i].ic_flags = chan_flags;
		}

		bwi_get_eaddr(sc, BWI_SPROM_11BG_EADDR, ic->ic_myaddr);
		if (IEEE80211_IS_MULTICAST(ic->ic_myaddr)) {
			bwi_get_eaddr(sc, BWI_SPROM_11A_EADDR, ic->ic_myaddr);
			if (IEEE80211_IS_MULTICAST(ic->ic_myaddr)) {
				device_printf(dev, "invalid MAC address: "
				    "%s\n", kether_ntoa(ic->ic_myaddr, ethstr));
			}
		}
	} else if (phy->phy_mode == IEEE80211_MODE_11A) {
		/* TODO:11A */
		error = ENXIO;
		goto fail;
	} else {
		panic("unknown phymode %d", phy->phy_mode);
	}

	ic->ic_caps = IEEE80211_C_SHSLOT |
		      IEEE80211_C_SHPREAMBLE |
		      IEEE80211_C_WPA |
		      IEEE80211_C_MONITOR;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_opmode = IEEE80211_M_STA;

	IEEE80211_ONOE_PARAM_SETUP(&sc->sc_onoe_param);
	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_ONOE;
	ic->ic_ratectl.rc_st_attach = bwi_ratectl_attach;

	ic->ic_updateslot = bwi_updateslot;

	ieee80211_ifattach(ic);

	ic->ic_headroom = sizeof(struct bwi_txbuf_hdr);
	ic->ic_flags_ext |= IEEE80211_FEXT_SWBMISS;

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = bwi_newstate;

	ieee80211_media_init(ic, bwi_media_change, ieee80211_media_status);

	/*
	 * Attach radio tap
	 */
	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
		      sizeof(struct ieee80211_frame) + sizeof(sc->sc_tx_th),
		      &sc->sc_drvbpf);

	sc->sc_tx_th_len = roundup(sizeof(sc->sc_tx_th), sizeof(uint32_t));
	sc->sc_tx_th.wt_ihdr.it_len = htole16(sc->sc_tx_th_len);
	sc->sc_tx_th.wt_ihdr.it_present = htole32(BWI_TX_RADIOTAP_PRESENT);

	sc->sc_rx_th_len = roundup(sizeof(sc->sc_rx_th), sizeof(uint32_t));
	sc->sc_rx_th.wr_ihdr.it_len = htole16(sc->sc_rx_th_len);
	sc->sc_rx_th.wr_ihdr.it_present = htole32(BWI_RX_RADIOTAP_PRESENT);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, bwi_intr, sc,
			       &sc->sc_irq_handle, ifp->if_serializer);
	if (error) {
		device_printf(dev, "can't setup intr\n");
		bpfdetach(ifp);
		ieee80211_ifdetach(ic);
		goto fail;
	}

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;
fail:
	bwi_detach(dev);
	return error;
}

static int
bwi_detach(device_t dev)
{
	struct bwi_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->sc_ic.ic_if;
		int i;

		lwkt_serialize_enter(ifp->if_serializer);
		bwi_stop(sc, 1);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);
		lwkt_serialize_exit(ifp->if_serializer);

		bpfdetach(ifp);
		ieee80211_ifdetach(&sc->sc_ic);

		for (i = 0; i < sc->sc_nmac; ++i)
			bwi_mac_detach(&sc->sc_mac[i]);
	}

	if (sc->sc_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sc_sysctl_ctx);

	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq_res);
	}

	if (sc->sc_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid,
				     sc->sc_mem_res);
	}

	bwi_dma_free(sc);

	return 0;
}

static int
bwi_shutdown(device_t dev)
{
	struct bwi_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);
	bwi_stop(sc, 1);
	lwkt_serialize_exit(ifp->if_serializer);
	return 0;
}

static void
bwi_power_on(struct bwi_softc *sc, int with_pll)
{
	uint32_t gpio_in, gpio_out, gpio_en;
	uint16_t status;

	gpio_in = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_IN, 4);
	if (gpio_in & BWI_PCIM_GPIO_PWR_ON)
		goto back;

	gpio_out = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, 4);
	gpio_en = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_ENABLE, 4);

	gpio_out |= BWI_PCIM_GPIO_PWR_ON;
	gpio_en |= BWI_PCIM_GPIO_PWR_ON;
	if (with_pll) {
		/* Turn off PLL first */
		gpio_out |= BWI_PCIM_GPIO_PLL_PWR_OFF;
		gpio_en |= BWI_PCIM_GPIO_PLL_PWR_OFF;
	}

	pci_write_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, gpio_out, 4);
	pci_write_config(sc->sc_dev, BWI_PCIR_GPIO_ENABLE, gpio_en, 4);
	DELAY(1000);

	if (with_pll) {
		/* Turn on PLL */
		gpio_out &= ~BWI_PCIM_GPIO_PLL_PWR_OFF;
		pci_write_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, gpio_out, 4);
		DELAY(5000);
	}

back:
	/* Clear "Signaled Target Abort" */
	status = pci_read_config(sc->sc_dev, PCIR_STATUS, 2);
	status &= ~PCIM_STATUS_STABORT;
	pci_write_config(sc->sc_dev, PCIR_STATUS, status, 2);
}

static int
bwi_power_off(struct bwi_softc *sc, int with_pll)
{
	uint32_t gpio_out, gpio_en;

	pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_IN, 4); /* dummy read */
	gpio_out = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, 4);
	gpio_en = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_ENABLE, 4);

	gpio_out &= ~BWI_PCIM_GPIO_PWR_ON;
	gpio_en |= BWI_PCIM_GPIO_PWR_ON;
	if (with_pll) {
		gpio_out |= BWI_PCIM_GPIO_PLL_PWR_OFF;
		gpio_en |= BWI_PCIM_GPIO_PLL_PWR_OFF;
	}

	pci_write_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, gpio_out, 4);
	pci_write_config(sc->sc_dev, BWI_PCIR_GPIO_ENABLE, gpio_en, 4);
	return 0;
}

int
bwi_regwin_switch(struct bwi_softc *sc, struct bwi_regwin *rw,
		  struct bwi_regwin **old_rw)
{
	int error;

	if (old_rw != NULL)
		*old_rw = NULL;

	if (!BWI_REGWIN_EXIST(rw))
		return EINVAL;

	if (sc->sc_cur_regwin != rw) {
		error = bwi_regwin_select(sc, rw->rw_id);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't select regwin %d\n",
				  rw->rw_id);
			return error;
		}
	}

	if (old_rw != NULL)
		*old_rw = sc->sc_cur_regwin;
	sc->sc_cur_regwin = rw;
	return 0;
}

static int
bwi_regwin_select(struct bwi_softc *sc, int id)
{
	uint32_t win = BWI_PCIM_REGWIN(id);
	int i;

#define RETRY_MAX	50
	for (i = 0; i < RETRY_MAX; ++i) {
		pci_write_config(sc->sc_dev, BWI_PCIR_SEL_REGWIN, win, 4);
		if (pci_read_config(sc->sc_dev, BWI_PCIR_SEL_REGWIN, 4) == win)
			return 0;
		DELAY(10);
	}
#undef RETRY_MAX

	return ENXIO;
}

static void
bwi_regwin_info(struct bwi_softc *sc, uint16_t *type, uint8_t *rev)
{
	uint32_t val;

	val = CSR_READ_4(sc, BWI_ID_HI);
	*type = BWI_ID_HI_REGWIN_TYPE(val);
	*rev = BWI_ID_HI_REGWIN_REV(val);

	DPRINTF(sc, BWI_DBG_ATTACH, "regwin: type 0x%03x, rev %d, "
		"vendor 0x%04x\n", *type, *rev,
		__SHIFTOUT(val, BWI_ID_HI_REGWIN_VENDOR_MASK));
}

static int
bwi_bbp_attach(struct bwi_softc *sc)
{
	uint16_t bbp_id, rw_type;
	uint8_t rw_rev;
	uint32_t info;
	int error, nregwin, i;

	/*
	 * Get 0th regwin information
	 * NOTE: 0th regwin should exist
	 */
	error = bwi_regwin_select(sc, 0);
	if (error) {
		device_printf(sc->sc_dev, "can't select regwin 0\n");
		return error;
	}
	bwi_regwin_info(sc, &rw_type, &rw_rev);

	/*
	 * Find out BBP id
	 */
	bbp_id = 0;
	info = 0;
	if (rw_type == BWI_REGWIN_T_COM) {
		info = CSR_READ_4(sc, BWI_INFO);
		bbp_id = __SHIFTOUT(info, BWI_INFO_BBPID_MASK);

		BWI_CREATE_REGWIN(&sc->sc_com_regwin, 0, rw_type, rw_rev);

		sc->sc_cap = CSR_READ_4(sc, BWI_CAPABILITY);
	} else {
		uint16_t did = pci_get_device(sc->sc_dev);
		uint8_t revid = pci_get_revid(sc->sc_dev);

		for (i = 0; i < NELEM(bwi_bbpid_map); ++i) {
			if (did >= bwi_bbpid_map[i].did_min &&
			    did <= bwi_bbpid_map[i].did_max) {
				bbp_id = bwi_bbpid_map[i].bbp_id;
				break;
			}
		}
		if (bbp_id == 0) {
			device_printf(sc->sc_dev, "no BBP id for device id "
				      "0x%04x\n", did);
			return ENXIO;
		}

		info = __SHIFTIN(revid, BWI_INFO_BBPREV_MASK) |
		       __SHIFTIN(0, BWI_INFO_BBPPKG_MASK);
	}

	/*
	 * Find out number of regwins
	 */
	nregwin = 0;
	if (rw_type == BWI_REGWIN_T_COM && rw_rev >= 4) {
		nregwin = __SHIFTOUT(info, BWI_INFO_NREGWIN_MASK);
	} else {
		for (i = 0; i < NELEM(bwi_regwin_count); ++i) {
			if (bwi_regwin_count[i].bbp_id == bbp_id) {
				nregwin = bwi_regwin_count[i].nregwin;
				break;
			}
		}
		if (nregwin == 0) {
			device_printf(sc->sc_dev, "no number of win for "
				      "BBP id 0x%04x\n", bbp_id);
			return ENXIO;
		}
	}

	/* Record BBP id/rev for later using */
	sc->sc_bbp_id = bbp_id;
	sc->sc_bbp_rev = __SHIFTOUT(info, BWI_INFO_BBPREV_MASK);
	sc->sc_bbp_pkg = __SHIFTOUT(info, BWI_INFO_BBPPKG_MASK);
	device_printf(sc->sc_dev, "BBP: id 0x%04x, rev 0x%x, pkg %d\n",
		      sc->sc_bbp_id, sc->sc_bbp_rev, sc->sc_bbp_pkg);

	DPRINTF(sc, BWI_DBG_ATTACH, "nregwin %d, cap 0x%08x\n",
		nregwin, sc->sc_cap);

	/*
	 * Create rest of the regwins
	 */

	/* Don't re-create common regwin, if it is already created */
	i = BWI_REGWIN_EXIST(&sc->sc_com_regwin) ? 1 : 0;

	for (; i < nregwin; ++i) {
		/*
		 * Get regwin information
		 */
		error = bwi_regwin_select(sc, i);
		if (error) {
			device_printf(sc->sc_dev,
				      "can't select regwin %d\n", i);
			return error;
		}
		bwi_regwin_info(sc, &rw_type, &rw_rev);

		/*
		 * Try attach:
		 * 1) Bus (PCI/PCIE) regwin
		 * 2) MAC regwin
		 * Ignore rest types of regwin
		 */
		if (rw_type == BWI_REGWIN_T_BUSPCI ||
		    rw_type == BWI_REGWIN_T_BUSPCIE) {
			if (BWI_REGWIN_EXIST(&sc->sc_bus_regwin)) {
				device_printf(sc->sc_dev,
					      "bus regwin already exists\n");
			} else {
				BWI_CREATE_REGWIN(&sc->sc_bus_regwin, i,
						  rw_type, rw_rev);
			}
		} else if (rw_type == BWI_REGWIN_T_MAC) {
			/* XXX ignore return value */
			bwi_mac_attach(sc, i, rw_rev);
		}
	}

	/* At least one MAC shold exist */
	if (!BWI_REGWIN_EXIST(&sc->sc_mac[0].mac_regwin)) {
		device_printf(sc->sc_dev, "no MAC was found\n");
		return ENXIO;
	}
	KKASSERT(sc->sc_nmac > 0);

	/* Bus regwin must exist */
	if (!BWI_REGWIN_EXIST(&sc->sc_bus_regwin)) {
		device_printf(sc->sc_dev, "no bus regwin was found\n");
		return ENXIO;
	}

	/* Start with first MAC */
	error = bwi_regwin_switch(sc, &sc->sc_mac[0].mac_regwin, NULL);
	if (error)
		return error;

	return 0;
}

int
bwi_bus_init(struct bwi_softc *sc, struct bwi_mac *mac)
{
	struct bwi_regwin *old, *bus;
	uint32_t val;
	int error;

	bus = &sc->sc_bus_regwin;
	KKASSERT(sc->sc_cur_regwin == &mac->mac_regwin);

	/*
	 * Tell bus to generate requested interrupts
	 */
	if (bus->rw_rev < 6 && bus->rw_type == BWI_REGWIN_T_BUSPCI) {
		/*
		 * NOTE: Read BWI_FLAGS from MAC regwin
		 */
		val = CSR_READ_4(sc, BWI_FLAGS);

		error = bwi_regwin_switch(sc, bus, &old);
		if (error)
			return error;

		CSR_SETBITS_4(sc, BWI_INTRVEC, (val & BWI_FLAGS_INTR_MASK));
	} else {
		uint32_t mac_mask;

		mac_mask = 1 << mac->mac_id;

		error = bwi_regwin_switch(sc, bus, &old);
		if (error)
			return error;

		val = pci_read_config(sc->sc_dev, BWI_PCIR_INTCTL, 4);
		val |= mac_mask << 8;
		pci_write_config(sc->sc_dev, BWI_PCIR_INTCTL, val, 4);
	}

	if (sc->sc_flags & BWI_F_BUS_INITED)
		goto back;

	if (bus->rw_type == BWI_REGWIN_T_BUSPCI) {
		/*
		 * Enable prefetch and burst
		 */
		CSR_SETBITS_4(sc, BWI_BUS_CONFIG,
			      BWI_BUS_CONFIG_PREFETCH | BWI_BUS_CONFIG_BURST);

		if (bus->rw_rev < 5) {
			struct bwi_regwin *com = &sc->sc_com_regwin;

			/*
			 * Configure timeouts for bus operation
			 */

			/*
			 * Set service timeout and request timeout
			 */
			CSR_SETBITS_4(sc, BWI_CONF_LO,
			__SHIFTIN(BWI_CONF_LO_SERVTO, BWI_CONF_LO_SERVTO_MASK) |
			__SHIFTIN(BWI_CONF_LO_REQTO, BWI_CONF_LO_REQTO_MASK));

			/*
			 * If there is common regwin, we switch to that regwin
			 * and switch back to bus regwin once we have done.
			 */
			if (BWI_REGWIN_EXIST(com)) {
				error = bwi_regwin_switch(sc, com, NULL);
				if (error)
					return error;
			}

			/* Let bus know what we have changed */
			CSR_WRITE_4(sc, BWI_BUS_ADDR, BWI_BUS_ADDR_MAGIC);
			CSR_READ_4(sc, BWI_BUS_ADDR); /* Flush */
			CSR_WRITE_4(sc, BWI_BUS_DATA, 0);
			CSR_READ_4(sc, BWI_BUS_DATA); /* Flush */

			if (BWI_REGWIN_EXIST(com)) {
				error = bwi_regwin_switch(sc, bus, NULL);
				if (error)
					return error;
			}
		} else if (bus->rw_rev >= 11) {
			/*
			 * Enable memory read multiple
			 */
			CSR_SETBITS_4(sc, BWI_BUS_CONFIG, BWI_BUS_CONFIG_MRM);
		}
	} else {
		/* TODO:PCIE */
	}

	sc->sc_flags |= BWI_F_BUS_INITED;
back:
	return bwi_regwin_switch(sc, old, NULL);
}

static void
bwi_get_card_flags(struct bwi_softc *sc)
{
	sc->sc_card_flags = bwi_read_sprom(sc, BWI_SPROM_CARD_FLAGS);
	if (sc->sc_card_flags == 0xffff)
		sc->sc_card_flags = 0;

	if (sc->sc_pci_subvid == PCI_VENDOR_APPLE &&
	    sc->sc_pci_subdid == 0x4e && /* XXX */
	    sc->sc_pci_revid > 0x40)
		sc->sc_card_flags |= BWI_CARD_F_PA_GPIO9;

	DPRINTF(sc, BWI_DBG_ATTACH, "card flags 0x%04x\n", sc->sc_card_flags);
}

static void
bwi_get_eaddr(struct bwi_softc *sc, uint16_t eaddr_ofs, uint8_t *eaddr)
{
	int i;

	for (i = 0; i < 3; ++i) {
		*((uint16_t *)eaddr + i) =
			htobe16(bwi_read_sprom(sc, eaddr_ofs + 2 * i));
	}
}

static void
bwi_get_clock_freq(struct bwi_softc *sc, struct bwi_clock_freq *freq)
{
	struct bwi_regwin *com;
	uint32_t val;
	u_int div;
	int src;

	bzero(freq, sizeof(*freq));
	com = &sc->sc_com_regwin;

	KKASSERT(BWI_REGWIN_EXIST(com));
	KKASSERT(sc->sc_cur_regwin == com);
	KKASSERT(sc->sc_cap & BWI_CAP_CLKMODE);

	/*
	 * Calculate clock frequency
	 */
	src = -1;
	div = 0;
	if (com->rw_rev < 6) {
		val = pci_read_config(sc->sc_dev, BWI_PCIR_GPIO_OUT, 4);
		if (val & BWI_PCIM_GPIO_OUT_CLKSRC) {
			src = BWI_CLKSRC_PCI;
			div = 64;
		} else {
			src = BWI_CLKSRC_CS_OSC;
			div = 32;
		}
	} else if (com->rw_rev < 10) {
		val = CSR_READ_4(sc, BWI_CLOCK_CTRL);

		src = __SHIFTOUT(val, BWI_CLOCK_CTRL_CLKSRC);
		if (src == BWI_CLKSRC_LP_OSC) {
			div = 1;
		} else {
			div = (__SHIFTOUT(val, BWI_CLOCK_CTRL_FDIV) + 1) << 2;

			/* Unknown source */
			if (src >= BWI_CLKSRC_MAX)
				src = BWI_CLKSRC_CS_OSC;
		}
	} else {
		val = CSR_READ_4(sc, BWI_CLOCK_INFO);

		src = BWI_CLKSRC_CS_OSC;
		div = (__SHIFTOUT(val, BWI_CLOCK_INFO_FDIV) + 1) << 2;
	}

	KKASSERT(src >= 0 && src < BWI_CLKSRC_MAX);
	KKASSERT(div != 0);

	DPRINTF(sc, BWI_DBG_ATTACH, "clksrc %s\n",
		src == BWI_CLKSRC_PCI ? "PCI" :
		(src == BWI_CLKSRC_LP_OSC ? "LP_OSC" : "CS_OSC"));

	freq->clkfreq_min = bwi_clkfreq[src].freq_min / div;
	freq->clkfreq_max = bwi_clkfreq[src].freq_max / div;

	DPRINTF(sc, BWI_DBG_ATTACH, "clkfreq min %u, max %u\n",
		freq->clkfreq_min, freq->clkfreq_max);
}

static int
bwi_set_clock_mode(struct bwi_softc *sc, enum bwi_clock_mode clk_mode)
{
	struct bwi_regwin *old, *com;
	uint32_t clk_ctrl, clk_src;
	int error, pwr_off = 0;

	com = &sc->sc_com_regwin;
	if (!BWI_REGWIN_EXIST(com))
		return 0;

	if (com->rw_rev >= 10 || com->rw_rev < 6)
		return 0;

	/*
	 * For common regwin whose rev is [6, 10), the chip
	 * must be capable to change clock mode.
	 */
	if ((sc->sc_cap & BWI_CAP_CLKMODE) == 0)
		return 0;

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return error;

	if (clk_mode == BWI_CLOCK_MODE_FAST)
		bwi_power_on(sc, 0);	/* Don't turn on PLL */

	clk_ctrl = CSR_READ_4(sc, BWI_CLOCK_CTRL);
	clk_src = __SHIFTOUT(clk_ctrl, BWI_CLOCK_CTRL_CLKSRC);

	switch (clk_mode) {
	case BWI_CLOCK_MODE_FAST:
		clk_ctrl &= ~BWI_CLOCK_CTRL_SLOW;
		clk_ctrl |= BWI_CLOCK_CTRL_IGNPLL;
		break;
	case BWI_CLOCK_MODE_SLOW:
		clk_ctrl |= BWI_CLOCK_CTRL_SLOW;
		break;
	case BWI_CLOCK_MODE_DYN:
		clk_ctrl &= ~(BWI_CLOCK_CTRL_SLOW |
			      BWI_CLOCK_CTRL_IGNPLL |
			      BWI_CLOCK_CTRL_NODYN);
		if (clk_src != BWI_CLKSRC_CS_OSC) {
			clk_ctrl |= BWI_CLOCK_CTRL_NODYN;
			pwr_off = 1;
		}
		break;
	}
	CSR_WRITE_4(sc, BWI_CLOCK_CTRL, clk_ctrl);

	if (pwr_off)
		bwi_power_off(sc, 0);	/* Leave PLL as it is */

	return bwi_regwin_switch(sc, old, NULL);
}

static int
bwi_set_clock_delay(struct bwi_softc *sc)
{
	struct bwi_regwin *old, *com;
	int error;

	com = &sc->sc_com_regwin;
	if (!BWI_REGWIN_EXIST(com))
		return 0;

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return error;

	if (sc->sc_bbp_id == BWI_BBPID_BCM4321) {
		if (sc->sc_bbp_rev == 0)
			CSR_WRITE_4(sc, BWI_CONTROL, BWI_CONTROL_MAGIC0);
		else if (sc->sc_bbp_rev == 1)
			CSR_WRITE_4(sc, BWI_CONTROL, BWI_CONTROL_MAGIC1);
	}

	if (sc->sc_cap & BWI_CAP_CLKMODE) {
		if (com->rw_rev >= 10) {
			CSR_FILT_SETBITS_4(sc, BWI_CLOCK_INFO, 0xffff, 0x40000);
		} else {
			struct bwi_clock_freq freq;

			bwi_get_clock_freq(sc, &freq);
			CSR_WRITE_4(sc, BWI_PLL_ON_DELAY,
				howmany(freq.clkfreq_max * 150, 1000000));
			CSR_WRITE_4(sc, BWI_FREQ_SEL_DELAY,
				howmany(freq.clkfreq_max * 15, 1000000));
		}
	}

	return bwi_regwin_switch(sc, old, NULL);
}

static void
bwi_init(void *xsc)
{
	bwi_init_statechg(xsc, 1);
}

static void
bwi_init_statechg(struct bwi_softc *sc, int statechg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	error = bwi_stop(sc, statechg);
	if (error) {
		if_printf(ifp, "can't stop\n");
		return;
	}

	bwi_bbp_power_on(sc, BWI_CLOCK_MODE_FAST);

	/* TODO: 2 MAC */

	mac = &sc->sc_mac[0];
	error = bwi_regwin_switch(sc, &mac->mac_regwin, NULL);
	if (error)
		goto back;

	error = bwi_mac_init(mac);
	if (error)
		goto back;

	bwi_bbp_power_on(sc, BWI_CLOCK_MODE_DYN);
	
	bcopy(IF_LLADDR(ifp), ic->ic_myaddr, sizeof(ic->ic_myaddr));

	bwi_set_bssid(sc, bwi_zero_addr);	/* Clear BSSID */
	bwi_set_addr_filter(sc, BWI_ADDR_FILTER_MYADDR, ic->ic_myaddr);

	bwi_mac_reset_hwkeys(mac);

	if ((mac->mac_flags & BWI_MAC_F_HAS_TXSTATS) == 0) {
		int i;

#define NRETRY	1000
		/*
		 * Drain any possible pending TX status
		 */
		for (i = 0; i < NRETRY; ++i) {
			if ((CSR_READ_4(sc, BWI_TXSTATUS0) &
			     BWI_TXSTATUS0_VALID) == 0)
				break;
			CSR_READ_4(sc, BWI_TXSTATUS1);
		}
		if (i == NRETRY)
			if_printf(ifp, "can't drain TX status\n");
#undef NRETRY
	}

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11G)
		bwi_mac_updateslot(mac, 1);

	/* Start MAC */
	error = bwi_mac_start(mac);
	if (error)
		goto back;

	/* Enable intrs */
	bwi_enable_intrs(sc, BWI_INIT_INTRS);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (statechg) {
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
				ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		} else {
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		}
	} else {
		ieee80211_new_state(ic, ic->ic_state, -1);
	}
back:
	if (error)
		bwi_stop(sc, 1);
	else
		ifp->if_start(ifp);
}

static int
bwi_ioctl(struct ifnet *ifp, u_long cmd, caddr_t req, struct ucred *cr)
{
	struct bwi_softc *sc = ifp->if_softc;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			struct bwi_mac *mac;
			int promisc = -1;

			KKASSERT(sc->sc_cur_regwin->rw_type ==
				 BWI_REGWIN_T_MAC);
			mac = (struct bwi_mac *)sc->sc_cur_regwin;

			if ((ifp->if_flags & IFF_PROMISC) &&
			    (sc->sc_flags & BWI_F_PROMISC) == 0) {
				promisc = 1;
				sc->sc_flags |= BWI_F_PROMISC;
			} else if ((ifp->if_flags & IFF_PROMISC) == 0 &&
				   (sc->sc_flags & BWI_F_PROMISC)) {
				promisc = 0;
				sc->sc_flags &= ~BWI_F_PROMISC;
			}

			if (promisc >= 0)
				bwi_mac_set_promisc(mac, promisc);
		}

		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				bwi_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				bwi_stop(sc, 1);
		}
		break;
	default:
		error = ieee80211_ioctl(&sc->sc_ic, cmd, req, cr);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			bwi_init(sc);
		error = 0;
	}
	return error;
}

static void
bwi_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct bwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[BWI_TX_DATA_RING];
	int trans, idx;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ifq_is_oactive(&ifp->if_snd) || (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	trans = 0;
	idx = tbd->tbd_idx;

	while (tbd->tbd_buf[idx].tb_mbuf == NULL) {
		struct ieee80211_frame *wh;
		struct ieee80211_node *ni;
		struct mbuf *m;
		int mgt_pkt = 0;

		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			IF_DEQUEUE(&ic->ic_mgtq, m);

			ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
			m->m_pkthdr.rcvif = NULL;

			mgt_pkt = 1;
		} else if (!ifq_is_empty(&ifp->if_snd)) {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN) {
				ifq_purge(&ifp->if_snd);
				break;
			}

			m = ifq_dequeue(&ifp->if_snd);
			if (m == NULL)
				break;

			if (m->m_len < sizeof(*eh)) {
				m = m_pullup(m, sizeof(*eh));
				if (m == NULL) {
					IFNET_STAT_INC(ifp, oerrors, 1);
					continue;
				}
			}
			eh = mtod(m, struct ether_header *);

			ni = ieee80211_find_txnode(ic, eh->ether_dhost);
			if (ni == NULL) {
				m_freem(m);
				IFNET_STAT_INC(ifp, oerrors, 1);
				continue;
			}

			/* TODO: PS */

			BPF_MTAP(ifp, m);

			m = ieee80211_encap(ic, m, ni);
			if (m == NULL) {
				ieee80211_free_node(ni);
				IFNET_STAT_INC(ifp, oerrors, 1);
				continue;
			}
		} else {
			break;
		}

		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m);

		wh = mtod(m, struct ieee80211_frame *);
		if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
			if (ieee80211_crypto_encap(ic, ni, m) == NULL) {
				ieee80211_free_node(ni);
				m_freem(m);
				IFNET_STAT_INC(ifp, oerrors, 1);
				continue;
			}
		}
		wh = NULL;	/* Catch any invalid use */

		if (bwi_encap(sc, idx, m, &ni, mgt_pkt) != 0) {
			/* 'm' is freed in bwi_encap() if we reach here */
			if (ni != NULL)
				ieee80211_free_node(ni);
			IFNET_STAT_INC(ifp, oerrors, 1);
			continue;
		}

		trans = 1;
		tbd->tbd_used++;
		idx = (idx + 1) % BWI_TX_NDESC;

		if (tbd->tbd_used + BWI_TX_NSPRDESC >= BWI_TX_NDESC) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
	}
	tbd->tbd_idx = idx;

	if (trans)
		sc->sc_tx_timer = 5;
	ifp->if_timer = 1;
}

static void
bwi_watchdog(struct ifnet *ifp)
{
	struct bwi_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_timer = 0;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0) {
			if_printf(ifp, "watchdog timeout\n");
			IFNET_STAT_INC(ifp, oerrors, 1);
			/* TODO */
		} else {
			ifp->if_timer = 1;
		}
	}
	ieee80211_watchdog(&sc->sc_ic);
}

static int
bwi_stop(struct bwi_softc *sc, int state_chg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct bwi_mac *mac;
	int i, error, pwr_off = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (state_chg)
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	else
		bwi_newstate_begin(sc, IEEE80211_S_INIT);

	if (ifp->if_flags & IFF_RUNNING) {
		KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		bwi_disable_intrs(sc, BWI_ALL_INTRS);
		CSR_READ_4(sc, BWI_MAC_INTR_MASK);
		bwi_mac_stop(mac);
	}

	for (i = 0; i < sc->sc_nmac; ++i) {
		struct bwi_regwin *old_rw;

		mac = &sc->sc_mac[i];
		if ((mac->mac_flags & BWI_MAC_F_INITED) == 0)
			continue;

		error = bwi_regwin_switch(sc, &mac->mac_regwin, &old_rw);
		if (error)
			continue;

		bwi_mac_shutdown(mac);
		pwr_off = 1;

		bwi_regwin_switch(sc, old_rw, NULL);
	}

	if (pwr_off)
		bwi_bbp_power_off(sc);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	return 0;
}

static void
bwi_intr(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct bwi_mac *mac;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t intr_status;
	uint32_t txrx_intr_status[BWI_TXRX_NRING];
	int i, txrx_error, tx = 0, rx_data = -1;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	/*
	 * Get interrupt status
	 */
	intr_status = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
	if (intr_status == 0xffffffff)	/* Not for us */
		return;

	DPRINTF(sc, BWI_DBG_INTR, "intr status 0x%08x\n", intr_status);

	intr_status &= CSR_READ_4(sc, BWI_MAC_INTR_MASK);
	if (intr_status == 0)		/* Nothing is interesting */
		return;

	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	txrx_error = 0;
	DPRINTF(sc, BWI_DBG_INTR, "%s\n", "TX/RX intr");
	for (i = 0; i < BWI_TXRX_NRING; ++i) {
		uint32_t mask;

		if (BWI_TXRX_IS_RX(i))
			mask = BWI_TXRX_RX_INTRS;
		else
			mask = BWI_TXRX_TX_INTRS;

		txrx_intr_status[i] =
		CSR_READ_4(sc, BWI_TXRX_INTR_STATUS(i)) & mask;

		_DPRINTF(sc, BWI_DBG_INTR, ", %d 0x%08x",
			 i, txrx_intr_status[i]);

		if (txrx_intr_status[i] & BWI_TXRX_INTR_ERROR) {
			if_printf(ifp, "intr fatal TX/RX (%d) error 0x%08x\n",
				  i, txrx_intr_status[i]);
			txrx_error = 1;
		}
	}
	_DPRINTF(sc, BWI_DBG_INTR, "%s\n", "");

	/*
	 * Acknowledge interrupt
	 */
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, intr_status);

	for (i = 0; i < BWI_TXRX_NRING; ++i)
		CSR_WRITE_4(sc, BWI_TXRX_INTR_STATUS(i), txrx_intr_status[i]);

	/* Disable all interrupts */
	bwi_disable_intrs(sc, BWI_ALL_INTRS);

	if (intr_status & BWI_INTR_PHY_TXERR) {
		if (mac->mac_flags & BWI_MAC_F_PHYE_RESET) {
			if_printf(ifp, "intr PHY TX error\n");
			/* XXX to netisr0? */
			bwi_init_statechg(sc, 0);
			return;
		}
	}

	if (txrx_error) {
		/* TODO: reset device */
	}

	if (intr_status & BWI_INTR_TBTT)
		bwi_mac_config_ps(mac);

	if (intr_status & BWI_INTR_EO_ATIM)
		if_printf(ifp, "EO_ATIM\n");

	if (intr_status & BWI_INTR_PMQ) {
		for (;;) {
			if ((CSR_READ_4(sc, BWI_MAC_PS_STATUS) & 0x8) == 0)
				break;
		}
		CSR_WRITE_2(sc, BWI_MAC_PS_STATUS, 0x2);
	}

	if (intr_status & BWI_INTR_NOISE)
		if_printf(ifp, "intr noise\n");

	if (txrx_intr_status[0] & BWI_TXRX_INTR_RX)
		rx_data = sc->sc_rxeof(sc);

	if (txrx_intr_status[3] & BWI_TXRX_INTR_RX) {
		sc->sc_txeof_status(sc);
		tx = 1;
	}

	if (intr_status & BWI_INTR_TX_DONE) {
		bwi_txeof(sc);
		tx = 1;
	}

	/* Re-enable interrupts */
	bwi_enable_intrs(sc, BWI_INIT_INTRS);

	if (sc->sc_blink_led != NULL && sc->sc_led_blink) {
		int evt = BWI_LED_EVENT_NONE;

		if (tx && rx_data > 0) {
			if (sc->sc_rx_rate > sc->sc_tx_rate)
				evt = BWI_LED_EVENT_RX;
			else
				evt = BWI_LED_EVENT_TX;
		} else if (tx) {
			evt = BWI_LED_EVENT_TX;
		} else if (rx_data > 0) {
			evt = BWI_LED_EVENT_RX;
		} else if (rx_data == 0) {
			evt = BWI_LED_EVENT_POLL;
		}

		if (evt != BWI_LED_EVENT_NONE)
			bwi_led_event(sc, evt);
	}
}

static void
bwi_newstate_begin(struct bwi_softc *sc, enum ieee80211_state nstate)
{
	callout_stop(&sc->sc_scan_ch);
	callout_stop(&sc->sc_calib_ch);

	ieee80211_ratectl_newstate(&sc->sc_ic, nstate);
	bwi_led_newstate(sc, nstate);

	if (nstate == IEEE80211_S_INIT)
		sc->sc_txpwrcb_type = BWI_TXPWR_INIT;
}

static int
bwi_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct bwi_softc *sc = ic->ic_if.if_softc;
	struct ifnet *ifp = &ic->ic_if;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	bwi_newstate_begin(sc, nstate);

	if (nstate == IEEE80211_S_INIT)
		goto back;

	error = bwi_set_chan(sc, ic->ic_curchan);
	if (error) {
		if_printf(ifp, "can't set channel to %u\n",
			  ieee80211_chan2ieee(ic, ic->ic_curchan));
		return error;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Nothing to do */
	} else if (nstate == IEEE80211_S_RUN) {
		struct bwi_mac *mac;

		bwi_set_bssid(sc, ic->ic_bss->ni_bssid);

		KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		/* Initial TX power calibration */
		bwi_mac_calibrate_txpower(mac, BWI_TXPWR_INIT);
#ifdef notyet
		sc->sc_txpwrcb_type = BWI_TXPWR_FORCE;
#else
		sc->sc_txpwrcb_type = BWI_TXPWR_CALIB;
#endif
	} else {
		bwi_set_bssid(sc, bwi_zero_addr);
	}

back:
	error = sc->sc_newstate(ic, nstate, arg);

	if (nstate == IEEE80211_S_SCAN) {
		callout_reset(&sc->sc_scan_ch,
			      (sc->sc_dwell_time * hz) / 1000,
			      bwi_next_scan, sc);
	} else if (nstate == IEEE80211_S_RUN) {
		callout_reset(&sc->sc_calib_ch, hz, bwi_calibrate, sc);
	}
	return error;
}

static int
bwi_media_change(struct ifnet *ifp)
{
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		bwi_init(ifp->if_softc);
	return 0;
}

static int
bwi_dma_alloc(struct bwi_softc *sc)
{
	int error, i, has_txstats;
	bus_addr_t lowaddr = 0;
	bus_size_t tx_ring_sz, rx_ring_sz, desc_sz = 0;
	uint32_t txrx_ctrl_step = 0;

	has_txstats = 0;
	for (i = 0; i < sc->sc_nmac; ++i) {
		if (sc->sc_mac[i].mac_flags & BWI_MAC_F_HAS_TXSTATS) {
			has_txstats = 1;
			break;
		}
	}

	switch (sc->sc_bus_space) {
	case BWI_BUS_SPACE_30BIT:
	case BWI_BUS_SPACE_32BIT:
		if (sc->sc_bus_space == BWI_BUS_SPACE_30BIT)
			lowaddr = BWI_BUS_SPACE_MAXADDR;
		else
			lowaddr = BUS_SPACE_MAXADDR_32BIT;
		desc_sz = sizeof(struct bwi_desc32);
		txrx_ctrl_step = 0x20;

		sc->sc_init_tx_ring = bwi_init_tx_ring32;
		sc->sc_free_tx_ring = bwi_free_tx_ring32;
		sc->sc_init_rx_ring = bwi_init_rx_ring32;
		sc->sc_free_rx_ring = bwi_free_rx_ring32;
		sc->sc_setup_rxdesc = bwi_setup_rx_desc32;
		sc->sc_setup_txdesc = bwi_setup_tx_desc32;
		sc->sc_rxeof = bwi_rxeof32;
		sc->sc_start_tx = bwi_start_tx32;
		if (has_txstats) {
			sc->sc_init_txstats = bwi_init_txstats32;
			sc->sc_free_txstats = bwi_free_txstats32;
			sc->sc_txeof_status = bwi_txeof_status32;
		}
		break;

	case BWI_BUS_SPACE_64BIT:
		lowaddr = BUS_SPACE_MAXADDR;	/* XXX */
		desc_sz = sizeof(struct bwi_desc64);
		txrx_ctrl_step = 0x40;

		sc->sc_init_tx_ring = bwi_init_tx_ring64;
		sc->sc_free_tx_ring = bwi_free_tx_ring64;
		sc->sc_init_rx_ring = bwi_init_rx_ring64;
		sc->sc_free_rx_ring = bwi_free_rx_ring64;
		sc->sc_setup_rxdesc = bwi_setup_rx_desc64;
		sc->sc_setup_txdesc = bwi_setup_tx_desc64;
		sc->sc_rxeof = bwi_rxeof64;
		sc->sc_start_tx = bwi_start_tx64;
		if (has_txstats) {
			sc->sc_init_txstats = bwi_init_txstats64;
			sc->sc_free_txstats = bwi_free_txstats64;
			sc->sc_txeof_status = bwi_txeof_status64;
		}
		break;
	}

	KKASSERT(lowaddr != 0);
	KKASSERT(desc_sz != 0);
	KKASSERT(txrx_ctrl_step != 0);

	tx_ring_sz = roundup(desc_sz * BWI_TX_NDESC, BWI_RING_ALIGN);
	rx_ring_sz = roundup(desc_sz * BWI_RX_NDESC, BWI_RING_ALIGN);

	/*
	 * Create top level DMA tag
	 */
	error = bus_dma_tag_create(NULL, BWI_ALIGN, 0,
				   lowaddr, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MAXBSIZE,
				   BUS_SPACE_UNRESTRICTED,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_parent_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create parent DMA tag\n");
		return error;
	}

#define TXRX_CTRL(idx)	(BWI_TXRX_CTRL_BASE + (idx) * txrx_ctrl_step)

	/*
	 * Create TX ring DMA stuffs
	 */
	error = bus_dma_tag_create(sc->sc_parent_dtag, BWI_RING_ALIGN, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   tx_ring_sz, 1, BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_txring_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create TX ring DMA tag\n");
		return error;
	}

	for (i = 0; i < BWI_TX_NRING; ++i) {
		error = bwi_dma_ring_alloc(sc, sc->sc_txring_dtag,
					   &sc->sc_tx_rdata[i], tx_ring_sz,
					   TXRX_CTRL(i));
		if (error) {
			device_printf(sc->sc_dev, "%dth TX ring "
				      "DMA alloc failed\n", i);
			return error;
		}
	}

	/*
	 * Create RX ring DMA stuffs
	 */
	error = bus_dma_tag_create(sc->sc_parent_dtag, BWI_RING_ALIGN, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   rx_ring_sz, 1, BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_rxring_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create RX ring DMA tag\n");
		return error;
	}

	error = bwi_dma_ring_alloc(sc, sc->sc_rxring_dtag, &sc->sc_rx_rdata,
				   rx_ring_sz, TXRX_CTRL(0));
	if (error) {
		device_printf(sc->sc_dev, "RX ring DMA alloc failed\n");
		return error;
	}

	if (has_txstats) {
		error = bwi_dma_txstats_alloc(sc, TXRX_CTRL(3), desc_sz);
		if (error) {
			device_printf(sc->sc_dev,
				      "TX stats DMA alloc failed\n");
			return error;
		}
	}

#undef TXRX_CTRL

	return bwi_dma_mbuf_create(sc);
}

static void
bwi_dma_free(struct bwi_softc *sc)
{
	if (sc->sc_txring_dtag != NULL) {
		int i;

		for (i = 0; i < BWI_TX_NRING; ++i) {
			struct bwi_ring_data *rd = &sc->sc_tx_rdata[i];

			if (rd->rdata_desc != NULL) {
				bus_dmamap_unload(sc->sc_txring_dtag,
						  rd->rdata_dmap);
				bus_dmamem_free(sc->sc_txring_dtag,
						rd->rdata_desc,
						rd->rdata_dmap);
			}
		}
		bus_dma_tag_destroy(sc->sc_txring_dtag);
	}

	if (sc->sc_rxring_dtag != NULL) {
		struct bwi_ring_data *rd = &sc->sc_rx_rdata;

		if (rd->rdata_desc != NULL) {
			bus_dmamap_unload(sc->sc_rxring_dtag, rd->rdata_dmap);
			bus_dmamem_free(sc->sc_rxring_dtag, rd->rdata_desc,
					rd->rdata_dmap);
		}
		bus_dma_tag_destroy(sc->sc_rxring_dtag);
	}

	bwi_dma_txstats_free(sc);
	bwi_dma_mbuf_destroy(sc, BWI_TX_NRING, 1);

	if (sc->sc_parent_dtag != NULL)
		bus_dma_tag_destroy(sc->sc_parent_dtag);
}

static int
bwi_dma_ring_alloc(struct bwi_softc *sc, bus_dma_tag_t dtag,
		   struct bwi_ring_data *rd, bus_size_t size,
		   uint32_t txrx_ctrl)
{
	int error;

	error = bus_dmamem_alloc(dtag, &rd->rdata_desc,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &rd->rdata_dmap);
	if (error) {
		device_printf(sc->sc_dev, "can't allocate DMA mem\n");
		return error;
	}

	error = bus_dmamap_load(dtag, rd->rdata_dmap, rd->rdata_desc, size,
				bwi_dma_ring_addr, &rd->rdata_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(sc->sc_dev, "can't load DMA mem\n");
		bus_dmamem_free(dtag, rd->rdata_desc, rd->rdata_dmap);
		rd->rdata_desc = NULL;
		return error;
	}

	rd->rdata_txrx_ctrl = txrx_ctrl;
	return 0;
}

static int
bwi_dma_txstats_alloc(struct bwi_softc *sc, uint32_t ctrl_base,
		      bus_size_t desc_sz)
{
	struct bwi_txstats_data *st;
	bus_size_t dma_size;
	int error;

	st = kmalloc(sizeof(*st), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_txstats = st;

	/*
	 * Create TX stats descriptor DMA stuffs
	 */
	dma_size = roundup(desc_sz * BWI_TXSTATS_NDESC, BWI_RING_ALIGN);

	error = bus_dma_tag_create(sc->sc_parent_dtag, BWI_RING_ALIGN, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   dma_size, 1, BUS_SPACE_MAXSIZE_32BIT,
				   0, &st->stats_ring_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create txstats ring "
			      "DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(st->stats_ring_dtag, &st->stats_ring,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &st->stats_ring_dmap);
	if (error) {
		device_printf(sc->sc_dev, "can't allocate txstats ring "
			      "DMA mem\n");
		bus_dma_tag_destroy(st->stats_ring_dtag);
		st->stats_ring_dtag = NULL;
		return error;
	}

	error = bus_dmamap_load(st->stats_ring_dtag, st->stats_ring_dmap,
				st->stats_ring, dma_size,
				bwi_dma_ring_addr, &st->stats_ring_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(sc->sc_dev, "can't load txstats ring DMA mem\n");
		bus_dmamem_free(st->stats_ring_dtag, st->stats_ring,
				st->stats_ring_dmap);
		bus_dma_tag_destroy(st->stats_ring_dtag);
		st->stats_ring_dtag = NULL;
		return error;
	}

	/*
	 * Create TX stats DMA stuffs
	 */
	dma_size = roundup(sizeof(struct bwi_txstats) * BWI_TXSTATS_NDESC,
			   BWI_ALIGN);

	error = bus_dma_tag_create(sc->sc_parent_dtag, BWI_ALIGN, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   dma_size, 1, BUS_SPACE_MAXSIZE_32BIT,
				   0, &st->stats_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create txstats DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(st->stats_dtag, (void **)&st->stats,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &st->stats_dmap);
	if (error) {
		device_printf(sc->sc_dev, "can't allocate txstats DMA mem\n");
		bus_dma_tag_destroy(st->stats_dtag);
		st->stats_dtag = NULL;
		return error;
	}

	error = bus_dmamap_load(st->stats_dtag, st->stats_dmap, st->stats,
				dma_size, bwi_dma_ring_addr, &st->stats_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(sc->sc_dev, "can't load txstats DMA mem\n");
		bus_dmamem_free(st->stats_dtag, st->stats, st->stats_dmap);
		bus_dma_tag_destroy(st->stats_dtag);
		st->stats_dtag = NULL;
		return error;
	}

	st->stats_ctrl_base = ctrl_base;
	return 0;
}

static void
bwi_dma_txstats_free(struct bwi_softc *sc)
{
	struct bwi_txstats_data *st;

	if (sc->sc_txstats == NULL)
		return;
	st = sc->sc_txstats;

	if (st->stats_ring_dtag != NULL) {
		bus_dmamap_unload(st->stats_ring_dtag, st->stats_ring_dmap);
		bus_dmamem_free(st->stats_ring_dtag, st->stats_ring,
				st->stats_ring_dmap);
		bus_dma_tag_destroy(st->stats_ring_dtag);
	}

	if (st->stats_dtag != NULL) {
		bus_dmamap_unload(st->stats_dtag, st->stats_dmap);
		bus_dmamem_free(st->stats_dtag, st->stats, st->stats_dmap);
		bus_dma_tag_destroy(st->stats_dtag);
	}

	kfree(st, M_DEVBUF);
}

static void
bwi_dma_ring_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	KASSERT(nseg == 1, ("too many segments"));
	*((bus_addr_t *)arg) = seg->ds_addr;
}

static int
bwi_dma_mbuf_create(struct bwi_softc *sc)
{
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	int i, j, k, ntx, error;

	/*
	 * Create TX/RX mbuf DMA tag
	 */
	error = bus_dma_tag_create(sc->sc_parent_dtag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL, MCLBYTES, 1,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_buf_dtag);
	if (error) {
		device_printf(sc->sc_dev, "can't create mbuf DMA tag\n");
		return error;
	}

	ntx = 0;

	/*
	 * Create TX mbuf DMA map
	 */
	for (i = 0; i < BWI_TX_NRING; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			error = bus_dmamap_create(sc->sc_buf_dtag, 0,
						  &tbd->tbd_buf[j].tb_dmap);
			if (error) {
				device_printf(sc->sc_dev, "can't create "
					      "%dth tbd, %dth DMA map\n", i, j);

				ntx = i;
				for (k = 0; k < j; ++k) {
					bus_dmamap_destroy(sc->sc_buf_dtag,
						tbd->tbd_buf[k].tb_dmap);
				}
				goto fail;
			}
		}
	}
	ntx = BWI_TX_NRING;

	/*
	 * Create RX mbuf DMA map and a spare DMA map
	 */
	error = bus_dmamap_create(sc->sc_buf_dtag, 0,
				  &rbd->rbd_tmp_dmap);
	if (error) {
		device_printf(sc->sc_dev,
			      "can't create spare RX buf DMA map\n");
		goto fail;
	}

	for (j = 0; j < BWI_RX_NDESC; ++j) {
		error = bus_dmamap_create(sc->sc_buf_dtag, 0,
					  &rbd->rbd_buf[j].rb_dmap);
		if (error) {
			device_printf(sc->sc_dev, "can't create %dth "
				      "RX buf DMA map\n", j);

			for (k = 0; k < j; ++k) {
				bus_dmamap_destroy(sc->sc_buf_dtag,
					rbd->rbd_buf[j].rb_dmap);
			}
			bus_dmamap_destroy(sc->sc_buf_dtag,
					   rbd->rbd_tmp_dmap);
			goto fail;
		}
	}

	return 0;
fail:
	bwi_dma_mbuf_destroy(sc, ntx, 0);
	return error;
}

static void
bwi_dma_mbuf_destroy(struct bwi_softc *sc, int ntx, int nrx)
{
	int i, j;

	if (sc->sc_buf_dtag == NULL)
		return;

	for (i = 0; i < ntx; ++i) {
		struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[i];

		for (j = 0; j < BWI_TX_NDESC; ++j) {
			struct bwi_txbuf *tb = &tbd->tbd_buf[j];

			if (tb->tb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_buf_dtag,
						  tb->tb_dmap);
				m_freem(tb->tb_mbuf);
			}
			if (tb->tb_ni != NULL)
				ieee80211_free_node(tb->tb_ni);
			bus_dmamap_destroy(sc->sc_buf_dtag, tb->tb_dmap);
		}
	}

	if (nrx) {
		struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;

		bus_dmamap_destroy(sc->sc_buf_dtag, rbd->rbd_tmp_dmap);
		for (j = 0; j < BWI_RX_NDESC; ++j) {
			struct bwi_rxbuf *rb = &rbd->rbd_buf[j];

			if (rb->rb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_buf_dtag,
						  rb->rb_dmap);
				m_freem(rb->rb_mbuf);
			}
			bus_dmamap_destroy(sc->sc_buf_dtag, rb->rb_dmap);
		}
	}

	bus_dma_tag_destroy(sc->sc_buf_dtag);
	sc->sc_buf_dtag = NULL;
}

static void
bwi_enable_intrs(struct bwi_softc *sc, uint32_t enable_intrs)
{
	CSR_SETBITS_4(sc, BWI_MAC_INTR_MASK, enable_intrs);
}

static void
bwi_disable_intrs(struct bwi_softc *sc, uint32_t disable_intrs)
{
	CSR_CLRBITS_4(sc, BWI_MAC_INTR_MASK, disable_intrs);
}

static int
bwi_init_tx_ring32(struct bwi_softc *sc, int ring_idx)
{
	struct bwi_ring_data *rd;
	struct bwi_txbuf_data *tbd;
	uint32_t val, addr_hi, addr_lo;

	KKASSERT(ring_idx < BWI_TX_NRING);
	rd = &sc->sc_tx_rdata[ring_idx];
	tbd = &sc->sc_tx_bdata[ring_idx];

	tbd->tbd_idx = 0;
	tbd->tbd_used = 0;

	bzero(rd->rdata_desc, sizeof(struct bwi_desc32) * BWI_TX_NDESC);
	bus_dmamap_sync(sc->sc_txring_dtag, rd->rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	addr_lo = __SHIFTOUT(rd->rdata_paddr, BWI_TXRX32_RINGINFO_ADDR_MASK);
	addr_hi = __SHIFTOUT(rd->rdata_paddr, BWI_TXRX32_RINGINFO_FUNC_MASK);

	val = __SHIFTIN(addr_lo, BWI_TXRX32_RINGINFO_ADDR_MASK) |
	      __SHIFTIN(BWI_TXRX32_RINGINFO_FUNC_TXRX,
	      		BWI_TXRX32_RINGINFO_FUNC_MASK);
	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_RINGINFO, val);

	val = __SHIFTIN(addr_hi, BWI_TXRX32_CTRL_ADDRHI_MASK) |
	      BWI_TXRX32_CTRL_ENABLE;
	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_CTRL, val);

	return 0;
}

static void
bwi_init_rxdesc_ring32(struct bwi_softc *sc, uint32_t ctrl_base,
		       bus_addr_t paddr, int hdr_size, int ndesc)
{
	uint32_t val, addr_hi, addr_lo;

	addr_lo = __SHIFTOUT(paddr, BWI_TXRX32_RINGINFO_ADDR_MASK);
	addr_hi = __SHIFTOUT(paddr, BWI_TXRX32_RINGINFO_FUNC_MASK);

	val = __SHIFTIN(addr_lo, BWI_TXRX32_RINGINFO_ADDR_MASK) |
	      __SHIFTIN(BWI_TXRX32_RINGINFO_FUNC_TXRX,
	      		BWI_TXRX32_RINGINFO_FUNC_MASK);
	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_RINGINFO, val);

	val = __SHIFTIN(hdr_size, BWI_RX32_CTRL_HDRSZ_MASK) |
	      __SHIFTIN(addr_hi, BWI_TXRX32_CTRL_ADDRHI_MASK) |
	      BWI_TXRX32_CTRL_ENABLE;
	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_CTRL, val);

	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_INDEX,
		    (ndesc - 1) * sizeof(struct bwi_desc32));
}

static int
bwi_init_rx_ring32(struct bwi_softc *sc)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	int i, error;

	sc->sc_rx_bdata.rbd_idx = 0;

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		error = bwi_newbuf(sc, i, 1);
		if (error) {
			if_printf(&sc->sc_ic.ic_if,
				  "can't allocate %dth RX buffer\n", i);
			return error;
		}
	}
	bus_dmamap_sync(sc->sc_rxring_dtag, rd->rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	bwi_init_rxdesc_ring32(sc, rd->rdata_txrx_ctrl, rd->rdata_paddr,
			       sizeof(struct bwi_rxbuf_hdr), BWI_RX_NDESC);
	return 0;
}

static int
bwi_init_txstats32(struct bwi_softc *sc)
{
	struct bwi_txstats_data *st = sc->sc_txstats;
	bus_addr_t stats_paddr;
	int i;

	bzero(st->stats, BWI_TXSTATS_NDESC * sizeof(struct bwi_txstats));
	bus_dmamap_sync(st->stats_dtag, st->stats_dmap, BUS_DMASYNC_PREWRITE);

	st->stats_idx = 0;

	stats_paddr = st->stats_paddr;
	for (i = 0; i < BWI_TXSTATS_NDESC; ++i) {
		bwi_setup_desc32(sc, st->stats_ring, BWI_TXSTATS_NDESC, i,
				 stats_paddr, sizeof(struct bwi_txstats), 0);
		stats_paddr += sizeof(struct bwi_txstats);
	}
	bus_dmamap_sync(st->stats_ring_dtag, st->stats_ring_dmap,
			BUS_DMASYNC_PREWRITE);

	bwi_init_rxdesc_ring32(sc, st->stats_ctrl_base,
			       st->stats_ring_paddr, 0, BWI_TXSTATS_NDESC);
	return 0;
}

static void
bwi_setup_rx_desc32(struct bwi_softc *sc, int buf_idx, bus_addr_t paddr,
		    int buf_len)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;

	KKASSERT(buf_idx < BWI_RX_NDESC);
	bwi_setup_desc32(sc, rd->rdata_desc, BWI_RX_NDESC, buf_idx,
			 paddr, buf_len, 0);
}

static void
bwi_setup_tx_desc32(struct bwi_softc *sc, struct bwi_ring_data *rd,
		    int buf_idx, bus_addr_t paddr, int buf_len)
{
	KKASSERT(buf_idx < BWI_TX_NDESC);
	bwi_setup_desc32(sc, rd->rdata_desc, BWI_TX_NDESC, buf_idx,
			 paddr, buf_len, 1);
}

static int
bwi_init_tx_ring64(struct bwi_softc *sc, int ring_idx)
{
	/* TODO:64 */
	return EOPNOTSUPP;
}

static int
bwi_init_rx_ring64(struct bwi_softc *sc)
{
	/* TODO:64 */
	return EOPNOTSUPP;
}

static int
bwi_init_txstats64(struct bwi_softc *sc)
{
	/* TODO:64 */
	return EOPNOTSUPP;
}

static void
bwi_setup_rx_desc64(struct bwi_softc *sc, int buf_idx, bus_addr_t paddr,
		    int buf_len)
{
	/* TODO:64 */
}

static void
bwi_setup_tx_desc64(struct bwi_softc *sc, struct bwi_ring_data *rd,
		    int buf_idx, bus_addr_t paddr, int buf_len)
{
	/* TODO:64 */
}

static void
bwi_dma_buf_addr(void *arg, bus_dma_segment_t *seg, int nseg,
		 bus_size_t mapsz __unused, int error)
{
        if (!error) {
		KASSERT(nseg == 1, ("too many segments(%d)", nseg));
		*((bus_addr_t *)arg) = seg->ds_addr;
	}
}

static int
bwi_newbuf(struct bwi_softc *sc, int buf_idx, int init)
{
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	struct bwi_rxbuf *rxbuf = &rbd->rbd_buf[buf_idx];
	struct bwi_rxbuf_hdr *hdr;
	bus_dmamap_t map;
	bus_addr_t paddr;
	struct mbuf *m;
	int error;

	KKASSERT(buf_idx < BWI_RX_NDESC);

	m = m_getcl(init ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		error = ENOBUFS;

		/*
		 * If the NIC is up and running, we need to:
		 * - Clear RX buffer's header.
		 * - Restore RX descriptor settings.
		 */
		if (init)
			return error;
		else
			goto back;
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * Try to load RX buf into temporary DMA map
	 */
	error = bus_dmamap_load_mbuf(sc->sc_buf_dtag, rbd->rbd_tmp_dmap, m,
				     bwi_dma_buf_addr, &paddr,
				     init ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);

		/*
		 * See the comment above
		 */
		if (init)
			return error;
		else
			goto back;
	}

	if (!init)
		bus_dmamap_unload(sc->sc_buf_dtag, rxbuf->rb_dmap);
	rxbuf->rb_mbuf = m;
	rxbuf->rb_paddr = paddr;

	/*
	 * Swap RX buf's DMA map with the loaded temporary one
	 */
	map = rxbuf->rb_dmap;
	rxbuf->rb_dmap = rbd->rbd_tmp_dmap;
	rbd->rbd_tmp_dmap = map;

back:
	/*
	 * Clear RX buf header
	 */
	hdr = mtod(rxbuf->rb_mbuf, struct bwi_rxbuf_hdr *);
	bzero(hdr, sizeof(*hdr));
	bus_dmamap_sync(sc->sc_buf_dtag, rxbuf->rb_dmap, BUS_DMASYNC_PREWRITE);

	/*
	 * Setup RX buf descriptor
	 */
	sc->sc_setup_rxdesc(sc, buf_idx, rxbuf->rb_paddr,
			    rxbuf->rb_mbuf->m_len - sizeof(*hdr));
	return error;
}

static void
bwi_set_addr_filter(struct bwi_softc *sc, uint16_t addr_ofs,
		    const uint8_t *addr)
{
	int i;

	CSR_WRITE_2(sc, BWI_ADDR_FILTER_CTRL,
		    BWI_ADDR_FILTER_CTRL_SET | addr_ofs);

	for (i = 0; i < (IEEE80211_ADDR_LEN / 2); ++i) {
		uint16_t addr_val;

		addr_val = (uint16_t)addr[i * 2] |
			   (((uint16_t)addr[(i * 2) + 1]) << 8);
		CSR_WRITE_2(sc, BWI_ADDR_FILTER_DATA, addr_val);
	}
}

static int
bwi_set_chan(struct bwi_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
#ifdef INVARIANTS
	struct ifnet *ifp = &ic->ic_if;
#endif
	struct bwi_mac *mac;
	uint16_t flags;
	u_int chan;

	ASSERT_SERIALIZED(ifp->if_serializer);

	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	chan = ieee80211_chan2ieee(ic, c);

	bwi_rf_set_chan(mac, chan, 0);

	/*
	 * Setup radio tap channel freq and flags
	 */
	if (IEEE80211_IS_CHAN_G(c))
		flags = IEEE80211_CHAN_G;
	else
		flags = IEEE80211_CHAN_B;

	sc->sc_tx_th.wt_chan_freq = sc->sc_rx_th.wr_chan_freq =
		htole16(c->ic_freq);
	sc->sc_tx_th.wt_chan_flags = sc->sc_rx_th.wr_chan_flags =
		htole16(flags);

	return 0;
}

static void
bwi_next_scan(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ic);

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
bwi_rxeof(struct bwi_softc *sc, int end_idx)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int idx, rx_data = 0;

	idx = rbd->rbd_idx;
	while (idx != end_idx) {
		struct bwi_rxbuf *rb = &rbd->rbd_buf[idx];
		struct bwi_rxbuf_hdr *hdr;
		struct ieee80211_frame_min *wh;
		struct ieee80211_node *ni;
		struct mbuf *m;
		const void *plcp;
		uint16_t flags2;
		int buflen, wh_ofs, hdr_extra, rssi, type, rate;

		m = rb->rb_mbuf;
		bus_dmamap_sync(sc->sc_buf_dtag, rb->rb_dmap,
				BUS_DMASYNC_POSTREAD);

		if (bwi_newbuf(sc, idx, 0)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto next;
		}

		hdr = mtod(m, struct bwi_rxbuf_hdr *);
		flags2 = le16toh(hdr->rxh_flags2);

		hdr_extra = 0;
		if (flags2 & BWI_RXH_F2_TYPE2FRAME)
			hdr_extra = 2;
		wh_ofs = hdr_extra + 6;	/* XXX magic number */

		buflen = le16toh(hdr->rxh_buflen);
		if (buflen < BWI_FRAME_MIN_LEN(wh_ofs)) {
			if_printf(ifp, "short frame %d, hdr_extra %d\n",
				  buflen, hdr_extra);
			IFNET_STAT_INC(ifp, ierrors, 1);
			m_freem(m);
			goto next;
		}

		plcp = ((const uint8_t *)(hdr + 1) + hdr_extra);
		rssi = bwi_calc_rssi(sc, hdr);

		m->m_pkthdr.rcvif = ifp;
		m->m_len = m->m_pkthdr.len = buflen + sizeof(*hdr);
		m_adj(m, sizeof(*hdr) + wh_ofs);

		if (htole16(hdr->rxh_flags1) & BWI_RXH_F1_OFDM)
			rate = bwi_ofdm_plcp2rate(plcp);
		else
			rate = bwi_ds_plcp2rate(plcp);

		/* RX radio tap */
		if (sc->sc_drvbpf != NULL)
			bwi_rx_radiotap(sc, m, hdr, plcp, rate, rssi);

		m_adj(m, -IEEE80211_CRC_LEN);

		wh = mtod(m, struct ieee80211_frame_min *);
		ni = ieee80211_find_rxnode(ic, wh);

		type = ieee80211_input(ic, m, ni, rssi - BWI_NOISE_FLOOR,
				       le16toh(hdr->rxh_tsf));
		ieee80211_free_node(ni);

		if (type == IEEE80211_FC0_TYPE_DATA) {
			rx_data = 1;
			sc->sc_rx_rate = rate;
		}
next:
		idx = (idx + 1) % BWI_RX_NDESC;
	}

	rbd->rbd_idx = idx;
	bus_dmamap_sync(sc->sc_rxring_dtag, rd->rdata_dmap,
			BUS_DMASYNC_PREWRITE);
	return rx_data;
}

static int
bwi_rxeof32(struct bwi_softc *sc)
{
	uint32_t val, rx_ctrl;
	int end_idx, rx_data;

	rx_ctrl = sc->sc_rx_rdata.rdata_txrx_ctrl;

	val = CSR_READ_4(sc, rx_ctrl + BWI_RX32_STATUS);
	end_idx = __SHIFTOUT(val, BWI_RX32_STATUS_INDEX_MASK) /
		  sizeof(struct bwi_desc32);

	rx_data = bwi_rxeof(sc, end_idx);

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_INDEX,
		    end_idx * sizeof(struct bwi_desc32));

	return rx_data;
}

static int
bwi_rxeof64(struct bwi_softc *sc)
{
	/* TODO:64 */
	return 0;
}

static void
bwi_reset_rx_ring32(struct bwi_softc *sc, uint32_t rx_ctrl)
{
	int i;

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_CTRL, 0);

#define NRETRY 10

	for (i = 0; i < NRETRY; ++i) {
		uint32_t status;

		status = CSR_READ_4(sc, rx_ctrl + BWI_RX32_STATUS);
		if (__SHIFTOUT(status, BWI_RX32_STATUS_STATE_MASK) ==
		    BWI_RX32_STATUS_STATE_DISABLED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY)
		if_printf(&sc->sc_ic.ic_if, "reset rx ring timedout\n");

#undef NRETRY

	CSR_WRITE_4(sc, rx_ctrl + BWI_RX32_RINGINFO, 0);
}

static void
bwi_free_txstats32(struct bwi_softc *sc)
{
	bwi_reset_rx_ring32(sc, sc->sc_txstats->stats_ctrl_base);
}

static void
bwi_free_rx_ring32(struct bwi_softc *sc)
{
	struct bwi_ring_data *rd = &sc->sc_rx_rdata;
	struct bwi_rxbuf_data *rbd = &sc->sc_rx_bdata;
	int i;

	bwi_reset_rx_ring32(sc, rd->rdata_txrx_ctrl);

	for (i = 0; i < BWI_RX_NDESC; ++i) {
		struct bwi_rxbuf *rb = &rbd->rbd_buf[i];

		if (rb->rb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_buf_dtag, rb->rb_dmap);
			m_freem(rb->rb_mbuf);
			rb->rb_mbuf = NULL;
		}
	}
}

static void
bwi_free_tx_ring32(struct bwi_softc *sc, int ring_idx)
{
	struct bwi_ring_data *rd;
	struct bwi_txbuf_data *tbd;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t state, val;
	int i;

	KKASSERT(ring_idx < BWI_TX_NRING);
	rd = &sc->sc_tx_rdata[ring_idx];
	tbd = &sc->sc_tx_bdata[ring_idx];

#define NRETRY 10

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_STATUS);
		state = __SHIFTOUT(val, BWI_TX32_STATUS_STATE_MASK);
		if (state == BWI_TX32_STATUS_STATE_DISABLED ||
		    state == BWI_TX32_STATUS_STATE_IDLE ||
		    state == BWI_TX32_STATUS_STATE_STOPPED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY) {
		if_printf(ifp, "wait for TX ring(%d) stable timed out\n",
			  ring_idx);
	}

	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_CTRL, 0);
	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_STATUS);
		state = __SHIFTOUT(val, BWI_TX32_STATUS_STATE_MASK);
		if (state == BWI_TX32_STATUS_STATE_DISABLED)
			break;

		DELAY(1000);
	}
	if (i == NRETRY)
		if_printf(ifp, "reset TX ring (%d) timed out\n", ring_idx);

#undef NRETRY

	DELAY(1000);

	CSR_WRITE_4(sc, rd->rdata_txrx_ctrl + BWI_TX32_RINGINFO, 0);

	for (i = 0; i < BWI_TX_NDESC; ++i) {
		struct bwi_txbuf *tb = &tbd->tbd_buf[i];

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_buf_dtag, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}
		if (tb->tb_ni != NULL) {
			ieee80211_free_node(tb->tb_ni);
			tb->tb_ni = NULL;
		}
	}
}

static void
bwi_free_txstats64(struct bwi_softc *sc)
{
	/* TODO:64 */
}

static void
bwi_free_rx_ring64(struct bwi_softc *sc)
{
	/* TODO:64 */
}

static void
bwi_free_tx_ring64(struct bwi_softc *sc, int ring_idx)
{
	/* TODO:64 */
}

static int
bwi_encap(struct bwi_softc *sc, int idx, struct mbuf *m,
	  struct ieee80211_node **ni0, int mgt_pkt)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = *ni0;
	struct bwi_ring_data *rd = &sc->sc_tx_rdata[BWI_TX_DATA_RING];
	struct bwi_txbuf_data *tbd = &sc->sc_tx_bdata[BWI_TX_DATA_RING];
	struct bwi_txbuf *tb = &tbd->tbd_buf[idx];
	struct bwi_mac *mac;
	struct bwi_txbuf_hdr *hdr;
	struct ieee80211_frame *wh;
	uint8_t rate, rate_fb;
	uint32_t mac_ctrl;
	uint16_t phy_ctrl;
	bus_addr_t paddr;
	int pkt_len, error, mcast_pkt = 0;
#if 0
	const uint8_t *p;
	int i;
#endif

	KKASSERT(ni != NULL);
	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	wh = mtod(m, struct ieee80211_frame *);

	/* Get 802.11 frame len before prepending TX header */
	pkt_len = m->m_pkthdr.len + IEEE80211_CRC_LEN;

	/*
	 * Find TX rate
	 */
	bzero(tb->tb_rateidx, sizeof(tb->tb_rateidx));
	if (!mgt_pkt) {
		if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE) {
			int idx;

			rate = IEEE80211_RS_RATE(&ni->ni_rates,
					ic->ic_fixed_rate);

			if (ic->ic_fixed_rate >= 1)
				idx = ic->ic_fixed_rate - 1;
			else
				idx = 0;
			rate_fb = IEEE80211_RS_RATE(&ni->ni_rates, idx);
		} else {
			tb->tb_rateidx_cnt = ieee80211_ratectl_findrate(ni,
				m->m_pkthdr.len, tb->tb_rateidx, BWI_NTXRATE);

			rate = IEEE80211_RS_RATE(&ni->ni_rates,
						 tb->tb_rateidx[0]);
			if (tb->tb_rateidx_cnt == BWI_NTXRATE) {
				rate_fb = IEEE80211_RS_RATE(&ni->ni_rates,
							    tb->tb_rateidx[1]);
			} else {
				rate_fb = rate;
			}
			tb->tb_buflen = m->m_pkthdr.len;
		}
	} else {
		/* Fixed at 1Mbits/s for mgt frames */
		rate = rate_fb = (1 * 2);
	}

	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		rate = rate_fb = ic->ic_mcast_rate;
		mcast_pkt = 1;
	}

	if (rate == 0 || rate_fb == 0) {
		/* XXX this should not happen */
		if_printf(&ic->ic_if, "invalid rate %u or fallback rate %u",
			  rate, rate_fb);
		rate = rate_fb = (1 * 2); /* Force 1Mbits/s */
	}
	sc->sc_tx_rate = rate;

	/*
	 * TX radio tap
	 */
	if (sc->sc_drvbpf != NULL) {
		sc->sc_tx_th.wt_flags = 0;
		if (wh->i_fc[1] & IEEE80211_FC1_WEP)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		if (ieee80211_rate2modtype(rate) == IEEE80211_MODTYPE_DS &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
		    rate != (1 * 2)) {
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		}
		sc->sc_tx_th.wt_rate = rate;

		bpf_ptap(sc->sc_drvbpf, m, &sc->sc_tx_th, sc->sc_tx_th_len);
	}

	/*
	 * Setup the embedded TX header
	 */
	M_PREPEND(m, sizeof(*hdr), MB_DONTWAIT);
	if (m == NULL) {
		if_printf(&ic->ic_if, "prepend TX header failed\n");
		return ENOBUFS;
	}
	hdr = mtod(m, struct bwi_txbuf_hdr *);

	bzero(hdr, sizeof(*hdr));

	bcopy(wh->i_fc, hdr->txh_fc, sizeof(hdr->txh_fc));
	bcopy(wh->i_addr1, hdr->txh_addr1, sizeof(hdr->txh_addr1));

	if (!mcast_pkt) {
		uint16_t dur;
		uint8_t ack_rate;

		ack_rate = ieee80211_ack_rate(ni, rate_fb);
		dur = ieee80211_txtime(ni,
		sizeof(struct ieee80211_frame_ack) + IEEE80211_CRC_LEN,
		ack_rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);

		hdr->txh_fb_duration = htole16(dur);
	}

	hdr->txh_id = __SHIFTIN(BWI_TX_DATA_RING, BWI_TXH_ID_RING_MASK) |
		      __SHIFTIN(idx, BWI_TXH_ID_IDX_MASK);

	bwi_plcp_header(hdr->txh_plcp, pkt_len, rate);
	bwi_plcp_header(hdr->txh_fb_plcp, pkt_len, rate_fb);

	phy_ctrl = __SHIFTIN(mac->mac_rf.rf_ant_mode,
			     BWI_TXH_PHY_C_ANTMODE_MASK);
	if (ieee80211_rate2modtype(rate) == IEEE80211_MODTYPE_OFDM)
		phy_ctrl |= BWI_TXH_PHY_C_OFDM;
	else if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) && rate != (2 * 1))
		phy_ctrl |= BWI_TXH_PHY_C_SHPREAMBLE;

	mac_ctrl = BWI_TXH_MAC_C_HWSEQ | BWI_TXH_MAC_C_FIRST_FRAG;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1))
		mac_ctrl |= BWI_TXH_MAC_C_ACK;
	if (ieee80211_rate2modtype(rate_fb) == IEEE80211_MODTYPE_OFDM)
		mac_ctrl |= BWI_TXH_MAC_C_FB_OFDM;

	hdr->txh_mac_ctrl = htole32(mac_ctrl);
	hdr->txh_phy_ctrl = htole16(phy_ctrl);

	/* Catch any further usage */
	hdr = NULL;
	wh = NULL;

	/* DMA load */
	error = bus_dmamap_load_mbuf(sc->sc_buf_dtag, tb->tb_dmap, m,
				     bwi_dma_buf_addr, &paddr, BUS_DMA_NOWAIT);
	if (error && error != EFBIG) {
		if_printf(&ic->ic_if, "can't load TX buffer (1) %d\n", error);
		goto back;
	}

	if (error) {	/* error == EFBIG */
		struct mbuf *m_new;

		m_new = m_defrag(m, MB_DONTWAIT);
		if (m_new == NULL) {
			if_printf(&ic->ic_if, "can't defrag TX buffer\n");
			error = ENOBUFS;
			goto back;
		} else {
			m = m_new;
		}

		error = bus_dmamap_load_mbuf(sc->sc_buf_dtag, tb->tb_dmap, m,
					     bwi_dma_buf_addr, &paddr,
					     BUS_DMA_NOWAIT);
		if (error) {
			if_printf(&ic->ic_if, "can't load TX buffer (2) %d\n",
				  error);
			goto back;
		}
	}
	error = 0;

	bus_dmamap_sync(sc->sc_buf_dtag, tb->tb_dmap, BUS_DMASYNC_PREWRITE);

	if (mgt_pkt || mcast_pkt) {
		/* Don't involve mcast/mgt packets into TX rate control */
		ieee80211_free_node(ni);
		*ni0 = ni = NULL;
	}
	tb->tb_mbuf = m;
	tb->tb_ni = ni;

#if 0
	p = mtod(m, const uint8_t *);
	for (i = 0; i < m->m_pkthdr.len; ++i) {
		if (i != 0 && i % 8 == 0)
			kprintf("\n");
		kprintf("%02x ", p[i]);
	}
	kprintf("\n");
#endif

	DPRINTF(sc, BWI_DBG_TX, "idx %d, pkt_len %d, buflen %d\n",
		idx, pkt_len, m->m_pkthdr.len);

	/* Setup TX descriptor */
	sc->sc_setup_txdesc(sc, rd, idx, paddr, m->m_pkthdr.len);
	bus_dmamap_sync(sc->sc_txring_dtag, rd->rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	/* Kick start */
	sc->sc_start_tx(sc, rd->rdata_txrx_ctrl, idx);

back:
	if (error)
		m_freem(m);
	return error;
}

static void
bwi_start_tx32(struct bwi_softc *sc, uint32_t tx_ctrl, int idx)
{
	idx = (idx + 1) % BWI_TX_NDESC;
	CSR_WRITE_4(sc, tx_ctrl + BWI_TX32_INDEX,
		    idx * sizeof(struct bwi_desc32));
}

static void
bwi_start_tx64(struct bwi_softc *sc, uint32_t tx_ctrl, int idx)
{
	/* TODO:64 */
}

static void
bwi_txeof_status32(struct bwi_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t val, ctrl_base;
	int end_idx;

	ctrl_base = sc->sc_txstats->stats_ctrl_base;

	val = CSR_READ_4(sc, ctrl_base + BWI_RX32_STATUS);
	end_idx = __SHIFTOUT(val, BWI_RX32_STATUS_INDEX_MASK) /
		  sizeof(struct bwi_desc32);

	bwi_txeof_status(sc, end_idx);

	CSR_WRITE_4(sc, ctrl_base + BWI_RX32_INDEX,
		    end_idx * sizeof(struct bwi_desc32));

	if (!ifq_is_oactive(&ifp->if_snd))
		ifp->if_start(ifp);
}

static void
bwi_txeof_status64(struct bwi_softc *sc)
{
	/* TODO:64 */
}

static void
_bwi_txeof(struct bwi_softc *sc, uint16_t tx_id, int acked, int data_txcnt)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct bwi_txbuf_data *tbd;
	struct bwi_txbuf *tb;
	int ring_idx, buf_idx;

	if (tx_id == 0) {
		if_printf(ifp, "zero tx id\n");
		return;
	}

	ring_idx = __SHIFTOUT(tx_id, BWI_TXH_ID_RING_MASK);
	buf_idx = __SHIFTOUT(tx_id, BWI_TXH_ID_IDX_MASK);

	KKASSERT(ring_idx == BWI_TX_DATA_RING);
	KKASSERT(buf_idx < BWI_TX_NDESC);

	tbd = &sc->sc_tx_bdata[ring_idx];
	KKASSERT(tbd->tbd_used > 0);
	tbd->tbd_used--;

	tb = &tbd->tbd_buf[buf_idx];

	DPRINTF(sc, BWI_DBG_TXEOF, "txeof idx %d, "
		"acked %d, data_txcnt %d, ni %p\n",
		buf_idx, acked, data_txcnt, tb->tb_ni);

	bus_dmamap_unload(sc->sc_buf_dtag, tb->tb_dmap);
	m_freem(tb->tb_mbuf);
	tb->tb_mbuf = NULL;

	if (tb->tb_ni != NULL) {
		struct ieee80211_ratectl_res res[BWI_NTXRATE];
		int res_len, retry;

		if (data_txcnt <= BWI_SHRETRY_FB || tb->tb_rateidx_cnt == 1) {
			res_len = 1;
			res[0].rc_res_rateidx = tb->tb_rateidx[0];
			res[0].rc_res_tries = data_txcnt;
		} else {
			res_len = BWI_NTXRATE;
			res[0].rc_res_rateidx = tb->tb_rateidx[0];
			res[0].rc_res_tries = BWI_SHRETRY_FB;
			res[1].rc_res_rateidx = tb->tb_rateidx[1];
			res[1].rc_res_tries = data_txcnt - BWI_SHRETRY_FB;
		}

		if (acked) {
			IFNET_STAT_INC(ifp, opackets, 1);
			retry = data_txcnt > 0 ? data_txcnt - 1 : 0;
		} else {
			IFNET_STAT_INC(ifp, oerrors, 1);
			retry = data_txcnt;
		}

		ieee80211_ratectl_tx_complete(tb->tb_ni, tb->tb_buflen,
			res, res_len, retry, 0, !acked);

		ieee80211_free_node(tb->tb_ni);
		tb->tb_ni = NULL;
	} else {
		/* XXX mgt packet error */
		IFNET_STAT_INC(ifp, opackets, 1);
	}

	if (tbd->tbd_used == 0)
		sc->sc_tx_timer = 0;

	ifq_clr_oactive(&ifp->if_snd);
}

static void
bwi_txeof_status(struct bwi_softc *sc, int end_idx)
{
	struct bwi_txstats_data *st = sc->sc_txstats;
	int idx;

	bus_dmamap_sync(st->stats_dtag, st->stats_dmap, BUS_DMASYNC_POSTREAD);

	idx = st->stats_idx;
	while (idx != end_idx) {
		const struct bwi_txstats *stats = &st->stats[idx];

		if ((stats->txs_flags & BWI_TXS_F_PENDING) == 0) {
			int data_txcnt;

			data_txcnt = __SHIFTOUT(stats->txs_txcnt,
						BWI_TXS_TXCNT_DATA);
			_bwi_txeof(sc, le16toh(stats->txs_id),
				   stats->txs_flags & BWI_TXS_F_ACKED,
				   data_txcnt);
		}
		idx = (idx + 1) % BWI_TXSTATS_NDESC;
	}
	st->stats_idx = idx;
}

static void
bwi_txeof(struct bwi_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	for (;;) {
		uint32_t tx_status0, tx_status1;
		uint16_t tx_id;
		int data_txcnt;

		tx_status0 = CSR_READ_4(sc, BWI_TXSTATUS0);
		if ((tx_status0 & BWI_TXSTATUS0_VALID) == 0)
			break;
		tx_status1 = CSR_READ_4(sc, BWI_TXSTATUS1);

		tx_id = __SHIFTOUT(tx_status0, BWI_TXSTATUS0_TXID_MASK);
		data_txcnt = __SHIFTOUT(tx_status0,
				BWI_TXSTATUS0_DATA_TXCNT_MASK);

		if (tx_status0 & (BWI_TXSTATUS0_AMPDU | BWI_TXSTATUS0_PENDING))
			continue;

		_bwi_txeof(sc, tx_id, tx_status0 & BWI_TXSTATUS0_ACKED,
			   data_txcnt);
	}

	if (!ifq_is_oactive(&ifp->if_snd))
		ifp->if_start(ifp);
}

static int
bwi_bbp_power_on(struct bwi_softc *sc, enum bwi_clock_mode clk_mode)
{
	bwi_power_on(sc, 1);
	return bwi_set_clock_mode(sc, clk_mode);
}

static void
bwi_bbp_power_off(struct bwi_softc *sc)
{
	bwi_set_clock_mode(sc, BWI_CLOCK_MODE_SLOW);
	bwi_power_off(sc, 1);
}

static int
bwi_get_pwron_delay(struct bwi_softc *sc)
{
	struct bwi_regwin *com, *old;
	struct bwi_clock_freq freq;
	uint32_t val;
	int error;

	com = &sc->sc_com_regwin;
	KKASSERT(BWI_REGWIN_EXIST(com));

	if ((sc->sc_cap & BWI_CAP_CLKMODE) == 0)
		return 0;

	error = bwi_regwin_switch(sc, com, &old);
	if (error)
		return error;

	bwi_get_clock_freq(sc, &freq);

	val = CSR_READ_4(sc, BWI_PLL_ON_DELAY);
	sc->sc_pwron_delay = howmany((val + 2) * 1000000, freq.clkfreq_min);
	DPRINTF(sc, BWI_DBG_ATTACH, "power on delay %u\n", sc->sc_pwron_delay);

	return bwi_regwin_switch(sc, old, NULL);
}

static int
bwi_bus_attach(struct bwi_softc *sc)
{
	struct bwi_regwin *bus, *old;
	int error;

	bus = &sc->sc_bus_regwin;

	error = bwi_regwin_switch(sc, bus, &old);
	if (error)
		return error;

	if (!bwi_regwin_is_enabled(sc, bus))
		bwi_regwin_enable(sc, bus, 0);

	/* Disable interripts */
	CSR_WRITE_4(sc, BWI_INTRVEC, 0);

	return bwi_regwin_switch(sc, old, NULL);
}

static const char *
bwi_regwin_name(const struct bwi_regwin *rw)
{
	switch (rw->rw_type) {
	case BWI_REGWIN_T_COM:
		return "COM";
	case BWI_REGWIN_T_BUSPCI:
		return "PCI";
	case BWI_REGWIN_T_MAC:
		return "MAC";
	case BWI_REGWIN_T_BUSPCIE:
		return "PCIE";
	}
	panic("unknown regwin type 0x%04x", rw->rw_type);
	return NULL;
}

static uint32_t
bwi_regwin_disable_bits(struct bwi_softc *sc)
{
	uint32_t busrev;

	/* XXX cache this */
	busrev = __SHIFTOUT(CSR_READ_4(sc, BWI_ID_LO), BWI_ID_LO_BUSREV_MASK);
	DPRINTF(sc, BWI_DBG_ATTACH | BWI_DBG_INIT | BWI_DBG_MISC,
		"bus rev %u\n", busrev);

	if (busrev == BWI_BUSREV_0)
		return BWI_STATE_LO_DISABLE1;
	else if (busrev == BWI_BUSREV_1)
		return BWI_STATE_LO_DISABLE2;
	else
		return (BWI_STATE_LO_DISABLE1 | BWI_STATE_LO_DISABLE2);
}

int
bwi_regwin_is_enabled(struct bwi_softc *sc, struct bwi_regwin *rw)
{
	uint32_t val, disable_bits;

	disable_bits = bwi_regwin_disable_bits(sc);
	val = CSR_READ_4(sc, BWI_STATE_LO);

	if ((val & (BWI_STATE_LO_CLOCK |
		    BWI_STATE_LO_RESET |
		    disable_bits)) == BWI_STATE_LO_CLOCK) {
		DPRINTF(sc, BWI_DBG_ATTACH | BWI_DBG_INIT, "%s is enabled\n",
			bwi_regwin_name(rw));
		return 1;
	} else {
		DPRINTF(sc, BWI_DBG_ATTACH | BWI_DBG_INIT, "%s is disabled\n",
			bwi_regwin_name(rw));
		return 0;
	}
}

void
bwi_regwin_disable(struct bwi_softc *sc, struct bwi_regwin *rw, uint32_t flags)
{
	uint32_t state_lo, disable_bits;
	int i;

	state_lo = CSR_READ_4(sc, BWI_STATE_LO);

	/*
	 * If current regwin is in 'reset' state, it was already disabled.
	 */
	if (state_lo & BWI_STATE_LO_RESET) {
		DPRINTF(sc, BWI_DBG_ATTACH | BWI_DBG_INIT,
			"%s was already disabled\n", bwi_regwin_name(rw));
		return;
	}

	disable_bits = bwi_regwin_disable_bits(sc);

	/*
	 * Disable normal clock
	 */
	state_lo = BWI_STATE_LO_CLOCK | disable_bits;
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/*
	 * Wait until normal clock is disabled
	 */
#define NRETRY	1000
	for (i = 0; i < NRETRY; ++i) {
		state_lo = CSR_READ_4(sc, BWI_STATE_LO);
		if (state_lo & disable_bits)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		device_printf(sc->sc_dev, "%s disable clock timeout\n",
			      bwi_regwin_name(rw));
	}

	for (i = 0; i < NRETRY; ++i) {
		uint32_t state_hi;

		state_hi = CSR_READ_4(sc, BWI_STATE_HI);
		if ((state_hi & BWI_STATE_HI_BUSY) == 0)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		device_printf(sc->sc_dev, "%s wait BUSY unset timeout\n",
			      bwi_regwin_name(rw));
	}
#undef NRETRY

	/*
	 * Reset and disable regwin with gated clock
	 */
	state_lo = BWI_STATE_LO_RESET | disable_bits |
		   BWI_STATE_LO_CLOCK | BWI_STATE_LO_GATED_CLOCK |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	/* Reset and disable regwin */
	state_lo = BWI_STATE_LO_RESET | disable_bits |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);
}

void
bwi_regwin_enable(struct bwi_softc *sc, struct bwi_regwin *rw, uint32_t flags)
{
	uint32_t state_lo, state_hi, imstate;

	bwi_regwin_disable(sc, rw, flags);

	/* Reset regwin with gated clock */
	state_lo = BWI_STATE_LO_RESET |
		   BWI_STATE_LO_CLOCK |
		   BWI_STATE_LO_GATED_CLOCK |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	state_hi = CSR_READ_4(sc, BWI_STATE_HI);
	if (state_hi & BWI_STATE_HI_SERROR)
		CSR_WRITE_4(sc, BWI_STATE_HI, 0);

	imstate = CSR_READ_4(sc, BWI_IMSTATE);
	if (imstate & (BWI_IMSTATE_INBAND_ERR | BWI_IMSTATE_TIMEOUT)) {
		imstate &= ~(BWI_IMSTATE_INBAND_ERR | BWI_IMSTATE_TIMEOUT);
		CSR_WRITE_4(sc, BWI_IMSTATE, imstate);
	}

	/* Enable regwin with gated clock */
	state_lo = BWI_STATE_LO_CLOCK |
		   BWI_STATE_LO_GATED_CLOCK |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);

	/* Enable regwin with normal clock */
	state_lo = BWI_STATE_LO_CLOCK |
		   __SHIFTIN(flags, BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1);
}

static void
bwi_set_bssid(struct bwi_softc *sc, const uint8_t *bssid)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_mac *mac;
	struct bwi_myaddr_bssid buf;
	const uint8_t *p;
	uint32_t val;
	int n, i;

	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	bwi_set_addr_filter(sc, BWI_ADDR_FILTER_BSSID, bssid);

	bcopy(ic->ic_myaddr, buf.myaddr, sizeof(buf.myaddr));
	bcopy(bssid, buf.bssid, sizeof(buf.bssid));

	n = sizeof(buf) / sizeof(val);
	p = (const uint8_t *)&buf;
	for (i = 0; i < n; ++i) {
		int j;

		val = 0;
		for (j = 0; j < sizeof(val); ++j)
			val |= ((uint32_t)(*p++)) << (j * 8);

		TMPLT_WRITE_4(mac, 0x20 + (i * sizeof(val)), val);
	}
}

static void
bwi_updateslot(struct ifnet *ifp)
{
	struct bwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct bwi_mac *mac;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	ASSERT_SERIALIZED(ifp->if_serializer);

	DPRINTF(sc, BWI_DBG_80211, "%s\n", __func__);

	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	bwi_mac_updateslot(mac, (ic->ic_flags & IEEE80211_F_SHSLOT));
}

static void
bwi_calibrate(void *xsc)
{
	struct bwi_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ic->ic_state == IEEE80211_S_RUN) {
		struct bwi_mac *mac;

		KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
		mac = (struct bwi_mac *)sc->sc_cur_regwin;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			bwi_mac_calibrate_txpower(mac, sc->sc_txpwrcb_type);
			sc->sc_txpwrcb_type = BWI_TXPWR_CALIB;
		}

		/* XXX 15 seconds */
		callout_reset(&sc->sc_calib_ch, hz * 15, bwi_calibrate, sc);
	}

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
bwi_calc_rssi(struct bwi_softc *sc, const struct bwi_rxbuf_hdr *hdr)
{
	struct bwi_mac *mac;

	KKASSERT(sc->sc_cur_regwin->rw_type == BWI_REGWIN_T_MAC);
	mac = (struct bwi_mac *)sc->sc_cur_regwin;

	return bwi_rf_calc_rssi(mac, hdr);
}

static void
bwi_rx_radiotap(struct bwi_softc *sc, struct mbuf *m,
		struct bwi_rxbuf_hdr *hdr, const void *plcp,
		int rate, int rssi)
{
	const struct ieee80211_frame_min *wh;

	KKASSERT(sc->sc_drvbpf != NULL);

	sc->sc_rx_th.wr_flags = IEEE80211_RADIOTAP_F_FCS;
	if (htole16(hdr->rxh_flags1) & BWI_RXH_F1_SHPREAMBLE)
		sc->sc_rx_th.wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

	wh = mtod(m, const struct ieee80211_frame_min *);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP)
		sc->sc_rx_th.wr_flags |= IEEE80211_RADIOTAP_F_WEP;

	sc->sc_rx_th.wr_tsf = hdr->rxh_tsf; /* No endian convertion */
	sc->sc_rx_th.wr_rate = rate;
	sc->sc_rx_th.wr_antsignal = rssi;
	sc->sc_rx_th.wr_antnoise = BWI_NOISE_FLOOR;

	bpf_ptap(sc->sc_drvbpf, m, &sc->sc_rx_th, sc->sc_rx_th_len);
}

static void
bwi_led_attach(struct bwi_softc *sc)
{
	const uint8_t *led_act = NULL;
	uint16_t gpio, val[BWI_LED_MAX];
	int i;

	for (i = 0; i < NELEM(bwi_vendor_led_act); ++i) {
		if (sc->sc_pci_subvid == bwi_vendor_led_act[i].vid) {
			led_act = bwi_vendor_led_act[i].led_act;
			break;
		}
	}
	if (led_act == NULL)
		led_act = bwi_default_led_act;

	gpio = bwi_read_sprom(sc, BWI_SPROM_GPIO01);
	val[0] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_0);
	val[1] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_1);

	gpio = bwi_read_sprom(sc, BWI_SPROM_GPIO23);
	val[2] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_2);
	val[3] = __SHIFTOUT(gpio, BWI_SPROM_GPIO_3);

	for (i = 0; i < BWI_LED_MAX; ++i) {
		struct bwi_led *led = &sc->sc_leds[i];

		if (val[i] == 0xff) {
			led->l_act = led_act[i];
		} else {
			if (val[i] & BWI_LED_ACT_LOW)
				led->l_flags |= BWI_LED_F_ACTLOW;
			led->l_act = __SHIFTOUT(val[i], BWI_LED_ACT_MASK);
		}
		led->l_mask = (1 << i);

		if (led->l_act == BWI_LED_ACT_BLINK_SLOW ||
		    led->l_act == BWI_LED_ACT_BLINK_POLL ||
		    led->l_act == BWI_LED_ACT_BLINK) {
			led->l_flags |= BWI_LED_F_BLINK;
			if (led->l_act == BWI_LED_ACT_BLINK_POLL)
				led->l_flags |= BWI_LED_F_POLLABLE;
			else if (led->l_act == BWI_LED_ACT_BLINK_SLOW)
				led->l_flags |= BWI_LED_F_SLOW;

			if (sc->sc_blink_led == NULL) {
				sc->sc_blink_led = led;
				if (led->l_flags & BWI_LED_F_SLOW)
					BWI_LED_SLOWDOWN(sc->sc_led_idle);
			}
		}

		DPRINTF(sc, BWI_DBG_LED | BWI_DBG_ATTACH,
			"%dth led, act %d, lowact %d\n", i,
			led->l_act, led->l_flags & BWI_LED_F_ACTLOW);
	}
	callout_init(&sc->sc_led_blink_ch);
}

static __inline uint16_t
bwi_led_onoff(const struct bwi_led *led, uint16_t val, int on)
{
	if (led->l_flags & BWI_LED_F_ACTLOW)
		on = !on;
	if (on)
		val |= led->l_mask;
	else
		val &= ~led->l_mask;
	return val;
}

static void
bwi_led_newstate(struct bwi_softc *sc, enum ieee80211_state nstate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
	int i;

	if (nstate == IEEE80211_S_INIT) {
		callout_stop(&sc->sc_led_blink_ch);
		sc->sc_led_blinking = 0;
	}

	if ((ic->ic_if.if_flags & IFF_RUNNING) == 0)
		return;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	for (i = 0; i < BWI_LED_MAX; ++i) {
		struct bwi_led *led = &sc->sc_leds[i];
		int on;

		if (led->l_act == BWI_LED_ACT_UNKN ||
		    led->l_act == BWI_LED_ACT_NULL)
			continue;

		if ((led->l_flags & BWI_LED_F_BLINK) &&
		    nstate != IEEE80211_S_INIT)
			continue;

		switch (led->l_act) {
		case BWI_LED_ACT_ON:		/* Always on */
			on = 1;
			break;
		case BWI_LED_ACT_OFF:		/* Always off */
		case BWI_LED_ACT_5GHZ:		/* TODO: 11A */
			on = 0;
			break;
		default:
			on = 1;
			switch (nstate) {
			case IEEE80211_S_INIT:
				on = 0;
				break;
			case IEEE80211_S_RUN:
				if (led->l_act == BWI_LED_ACT_11G &&
				    ic->ic_curmode != IEEE80211_MODE_11G)
					on = 0;
				break;
			default:
				if (led->l_act == BWI_LED_ACT_ASSOC)
					on = 0;
				break;
			}
			break;
		}

		val = bwi_led_onoff(led, val, on);
	}
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);
}

static void
bwi_led_event(struct bwi_softc *sc, int event)
{
	struct bwi_led *led = sc->sc_blink_led;
	int rate;

	if (event == BWI_LED_EVENT_POLL) {
		if ((led->l_flags & BWI_LED_F_POLLABLE) == 0)
			return;
		if (ticks - sc->sc_led_ticks < sc->sc_led_idle)
			return;
	}

	sc->sc_led_ticks = ticks;
	if (sc->sc_led_blinking)
		return;

	switch (event) {
	case BWI_LED_EVENT_RX:
		rate = sc->sc_rx_rate;
		break;
	case BWI_LED_EVENT_TX:
		rate = sc->sc_tx_rate;
		break;
	case BWI_LED_EVENT_POLL:
		rate = 0;
		break;
	default:
		panic("unknown LED event %d", event);
		break;
	}
	bwi_led_blink_start(sc, bwi_led_duration[rate].on_dur,
			    bwi_led_duration[rate].off_dur);
}

static void
bwi_led_blink_start(struct bwi_softc *sc, int on_dur, int off_dur)
{
	struct bwi_led *led = sc->sc_blink_led;
	uint16_t val;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	val = bwi_led_onoff(led, val, 1);
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);

	if (led->l_flags & BWI_LED_F_SLOW) {
		BWI_LED_SLOWDOWN(on_dur);
		BWI_LED_SLOWDOWN(off_dur);
	}

	sc->sc_led_blinking = 1;
	sc->sc_led_blink_offdur = off_dur;

	callout_reset(&sc->sc_led_blink_ch, on_dur, bwi_led_blink_next, sc);
}

static void
bwi_led_blink_next(void *xsc)
{
	struct bwi_softc *sc = xsc;
	uint16_t val;

	val = CSR_READ_2(sc, BWI_MAC_GPIO_CTRL);
	val = bwi_led_onoff(sc->sc_blink_led, val, 0);
	CSR_WRITE_2(sc, BWI_MAC_GPIO_CTRL, val);

	callout_reset(&sc->sc_led_blink_ch, sc->sc_led_blink_offdur,
		      bwi_led_blink_end, sc);
}

static void
bwi_led_blink_end(void *xsc)
{
	struct bwi_softc *sc = xsc;

	sc->sc_led_blinking = 0;
}

static void *
bwi_ratectl_attach(struct ieee80211com *ic, u_int rc)
{
	struct bwi_softc *sc = ic->ic_if.if_softc;

	switch (rc) {
	case IEEE80211_RATECTL_ONOE:
		return &sc->sc_onoe_param;
	case IEEE80211_RATECTL_NONE:
		/* This could only happen during detaching */
		return NULL;
	default:
		panic("unknown rate control algo %u", rc);
		return NULL;
	}
}
