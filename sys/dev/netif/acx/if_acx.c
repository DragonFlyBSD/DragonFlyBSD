/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * 
 */

/*
 * Copyright (c) 2003-2004 wlan.kewl.org Project
 * All rights reserved.
 * 
 * $Id: LICENSE,v 1.1.1.1 2004/07/01 12:20:39 darron Exp $
 *  
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *    
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 
 *    This product includes software developed by the wlan.kewl.org Project.
 * 
 * 4. Neither the name of the wlan.kewl.org Project nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE wlan.kewl.org Project BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/firmware.h>
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

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/wlan_ratectl/amrr/ieee80211_amrr_param.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#define ACX_DEBUG

#include <dev/netif/acx/if_acxreg.h>
#include <dev/netif/acx/if_acxvar.h>
#include <dev/netif/acx/acxcmd.h>

static int	acx_probe(device_t);
static int	acx_attach(device_t);
static int	acx_detach(device_t);
static int	acx_shutdown(device_t);

static void	acx_init(void *);
static void	acx_start(struct ifnet *, struct ifaltq_subque *);
static int	acx_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	acx_watchdog(struct ifnet *);

static void	acx_intr(void *);
static void	acx_txeof(struct acx_softc *);
static void	acx_txerr(struct acx_softc *, uint8_t);
static void	acx_rxeof(struct acx_softc *);
static void	acx_disable_intr(struct acx_softc *);
static void	acx_enable_intr(struct acx_softc *);

static int	acx_reset(struct acx_softc *);
static int	acx_stop(struct acx_softc *);
static void	acx_init_info_reg(struct acx_softc *);
static int	acx_config(struct acx_softc *);
static int	acx_read_config(struct acx_softc *, struct acx_config *);
static int	acx_write_config(struct acx_softc *, struct acx_config *);
static int	acx_rx_config(struct acx_softc *, int);
static int	acx_set_crypt_keys(struct acx_softc *);
static void	acx_calibrate(void *);

static int	acx_dma_alloc(struct acx_softc *);
static void	acx_dma_free(struct acx_softc *);
static int	acx_init_tx_ring(struct acx_softc *);
static int	acx_init_rx_ring(struct acx_softc *);
static int	acx_newbuf(struct acx_softc *, struct acx_rxbuf *, int);
static int	acx_encap(struct acx_softc *, struct acx_txbuf *,
			  struct mbuf *, struct ieee80211_node *);

static int	acx_set_null_tmplt(struct acx_softc *);
static int	acx_set_probe_req_tmplt(struct acx_softc *, const char *, int);
static int	acx_set_probe_resp_tmplt(struct acx_softc *,
					 struct ieee80211_node *);
static int	acx_set_beacon_tmplt(struct acx_softc *,
				     struct ieee80211_node *);

static int	acx_read_eeprom(struct acx_softc *, uint32_t, uint8_t *);
static int	acx_read_phyreg(struct acx_softc *, uint32_t, uint8_t *);

static int	acx_alloc_firmware(struct acx_softc *);
static void	acx_free_firmware(struct acx_softc *);
static int	acx_setup_firmware(struct acx_softc *, struct fw_image *,
				   const uint8_t **, int *);
static int	acx_load_firmware(struct acx_softc *, uint32_t,
				  const uint8_t *, int);
static int	acx_load_radio_firmware(struct acx_softc *, const uint8_t *,
					uint32_t);
static int	acx_load_base_firmware(struct acx_softc *, const uint8_t *,
				       uint32_t);

static void	acx_next_scan(void *);
static int	acx_set_chan(struct acx_softc *, struct ieee80211_channel *);

static int	acx_media_change(struct ifnet *);
static int	acx_newstate(struct ieee80211com *, enum ieee80211_state, int);

static int	acx_sysctl_msdu_lifetime(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_free_firmware(SYSCTL_HANDLER_ARGS);

const struct ieee80211_rateset	acx_rates_11b =
	{ 4, { 2, 4, 11, 22 } };
const struct ieee80211_rateset	acx_rates_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };
const struct ieee80211_rateset	acx_rates_11b_pbcc =
	{ 5, { 2, 4, 11, 22, 44 } };
const struct ieee80211_rateset	acx_rates_11g_pbcc =
	{ 13, { 2, 4, 11, 22, 44, 12, 18, 24, 36, 48, 72, 96, 108 } };

int	acx_enable_pbcc = 1;
TUNABLE_INT("hw.acx.enable_pbcc", &acx_enable_pbcc);

static const struct acx_device {
	uint16_t	vid;
	uint16_t	did;
	void		(*set_param)(device_t);
	const char	*desc;
} acx_devices[] = {
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100A, acx100_set_param,
	  "Texas Instruments TNETW1100A Wireless Adapter" },
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100B, acx100_set_param,
	  "Texas Instruments TNETW1100B Wireless Adapter" },
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX111, acx111_set_param,
	  "Texas Instruments TNETW1130 Wireless Adapter" },
	{ 0, 0, NULL, NULL }
};

static device_method_t acx_methods[] = {
	DEVMETHOD(device_probe,		acx_probe),
	DEVMETHOD(device_attach,	acx_attach),
	DEVMETHOD(device_detach,	acx_detach),
	DEVMETHOD(device_shutdown,	acx_shutdown),
#if 0
	DEVMETHOD(device_suspend,	acx_suspend),
	DEVMETHOD(device_resume,	acx_resume),
#endif
	DEVMETHOD_END
};

static driver_t acx_driver = {
	"acx",
	acx_methods,
	sizeof(struct acx_softc)
};

static devclass_t acx_devclass;

DRIVER_MODULE(acx, pci, acx_driver, acx_devclass, NULL, NULL);
DRIVER_MODULE(acx, cardbus, acx_driver, acx_devclass, NULL, NULL);

MODULE_DEPEND(acx, wlan, 1, 1, 1);
MODULE_DEPEND(acx, wlan_ratectl_onoe, 1, 1, 1);
MODULE_DEPEND(acx, wlan_ratectl_amrr, 1, 1, 1);
MODULE_DEPEND(acx, pci, 1, 1, 1);
MODULE_DEPEND(acx, cardbus, 1, 1, 1);

static __inline int
acx_get_rssi(struct acx_softc *sc, uint8_t raw)
{
	int rssi;

	rssi = ((sc->chip_rssi_corr / 2) + (raw * 5)) / sc->chip_rssi_corr;
	return rssi > 100 ? 100 : rssi;
}

static int
acx_probe(device_t dev)
{
	const struct acx_device *a;
	uint16_t did, vid;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (a = acx_devices; a->desc != NULL; ++a) {
		if (vid == a->vid && did == a->did) {
			a->set_param(dev);
			device_set_desc(dev, a->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
acx_attach(device_t dev)
{
	struct acx_softc *sc;
	struct ifnet *ifp;
	struct ieee80211com *ic;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;
	int i, error;

	sc = device_get_softc(dev);
	ic = &sc->sc_ic;
	ifp = &ic->ic_if;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t mem1, mem2, irq;

		mem1 = pci_read_config(dev, sc->chip_mem1_rid, 4);
		mem2 = pci_read_config(dev, sc->chip_mem2_rid, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, sc->chip_mem1_rid, mem1, 4);
		pci_write_config(dev, sc->chip_mem2_rid, mem2, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif	/* !BURN_BRIDGE */

	/* Enable bus mastering */
	pci_enable_busmaster(dev); 

	/* Allocate IO memory 1 */
	sc->sc_mem1_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						 &sc->chip_mem1_rid,
						 RF_ACTIVE);
	if (sc->sc_mem1_res == NULL) {
		error = ENXIO;
		device_printf(dev, "can't allocate IO mem1\n");
		goto fail;
	}
	sc->sc_mem1_bt = rman_get_bustag(sc->sc_mem1_res);
	sc->sc_mem1_bh = rman_get_bushandle(sc->sc_mem1_res);

	/* Allocate IO memory 2 */
	sc->sc_mem2_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						 &sc->chip_mem2_rid,
						 RF_ACTIVE);
	if (sc->sc_mem2_res == NULL) {
		error = ENXIO;
		device_printf(dev, "can't allocate IO mem2\n");
		goto fail;
	}
	sc->sc_mem2_bt = rman_get_bustag(sc->sc_mem2_res);
	sc->sc_mem2_bh = rman_get_bushandle(sc->sc_mem2_res);

	/* Allocate irq */
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->sc_irq_rid,
						RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		error = ENXIO;
		device_printf(dev, "can't allocate intr\n");
		goto fail;
	}

	/* Initialize channel scanning timer */
	callout_init(&sc->sc_scan_timer);

	/* Initialize calibration timer */
	callout_init(&sc->sc_calibrate_timer);

	/* Allocate busdma stuffs */
	error = acx_dma_alloc(sc);
	if (error)
		goto fail;

	/* Reset Hardware */
	error = acx_reset(sc);
	if (error)
		goto fail;

	/* Disable interrupts before firmware is loaded */
	acx_disable_intr(sc);

	/* Get radio type and form factor */
#define EEINFO_RETRY_MAX	50
	for (i = 0; i < EEINFO_RETRY_MAX; ++i) {
		uint16_t ee_info;

		ee_info = CSR_READ_2(sc, ACXREG_EEPROM_INFO);
		if (ACX_EEINFO_HAS_RADIO_TYPE(ee_info)) {
			sc->sc_form_factor = ACX_EEINFO_FORM_FACTOR(ee_info);
			sc->sc_radio_type = ACX_EEINFO_RADIO_TYPE(ee_info);
			break;
		}
		DELAY(10000);
	}
	if (i == EEINFO_RETRY_MAX) {
		error = ENXIO;
		goto fail;
	}
#undef EEINFO_RETRY_MAX

	DPRINTF((&sc->sc_ic.ic_if, "radio type %02x\n", sc->sc_radio_type));

#ifdef DUMP_EEPROM
	for (i = 0; i < 0x40; ++i) {
		uint8_t val;

		error = acx_read_eeprom(sc, i, &val);
		if (i % 10 == 0)
			kprintf("\n");
		kprintf("%02x ", val);
	}
	kprintf("\n");
#endif	/* DUMP_EEPROM */

	/* Get EEPROM version */
	error = acx_read_eeprom(sc, ACX_EE_VERSION_OFS, &sc->sc_eeprom_ver);
	if (error)
		goto fail;
	DPRINTF((&sc->sc_ic.ic_if, "EEPROM version %u\n", sc->sc_eeprom_ver));

	/*
	 * Initialize device sysctl before ieee80211_ifattach()
	 */
	sc->sc_long_retry_limit = 4;
	sc->sc_msdu_lifetime = 4096;
	sc->sc_scan_dwell = 200;	/* 200 milliseconds */
	sc->sc_calib_intvl = 3 * 60;	/* 3 minutes */

	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			"msdu_lifetime", CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_msdu_lifetime, "I",
			"MSDU life time");
	SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		       "long_retry_limit", CTLFLAG_RW,
		       &sc->sc_long_retry_limit, 0, "Long retry limit");
	SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		       "scan_dwell", CTLFLAG_RW,
		       &sc->sc_scan_dwell, 0, "Scan channel dwell time (ms)");
	SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		       "calib_intvl", CTLFLAG_RW,
		       &sc->sc_calib_intvl, 0, "Calibration interval (second)");

	/*
	 * Nodes for firmware operation
	 */
	SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		       "combined_radio_fw", CTLFLAG_RW,
		       &sc->sc_firmware.combined_radio_fw, 0,
		       "Radio and base firmwares are combined");
	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "free_fw",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_free_firmware, "I",
			"Free firmware");

	/*
	 * Nodes for statistics
	 */
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "frag_error", CTLFLAG_RW, &sc->sc_stats.err_oth_frag,
			 0, "Fragment errors");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "tx_abort", CTLFLAG_RW, &sc->sc_stats.err_abort,
			 0, "TX abortions");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "tx_invalid", CTLFLAG_RW, &sc->sc_stats.err_param,
			 0, "Invalid TX param in TX descriptor");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "no_wepkey", CTLFLAG_RW, &sc->sc_stats.err_no_wepkey,
			 0, "No WEP key exists");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "msdu_timeout", CTLFLAG_RW,
			 &sc->sc_stats.err_msdu_timeout,
			 0, "MSDU timeouts");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "ex_txretry", CTLFLAG_RW, &sc->sc_stats.err_ex_retry,
			 0, "Excessive TX retries");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "buf_oflow", CTLFLAG_RW, &sc->sc_stats.err_buf_oflow,
			 0, "Buffer overflows");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "dma_error", CTLFLAG_RW, &sc->sc_stats.err_dma,
			 0, "DMA errors");
	SYSCTL_ADD_UQUAD(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
			 "unkn_error", CTLFLAG_RW, &sc->sc_stats.err_unkn,
			 0, "Unknown errors");

	ifp->if_softc = sc;
	ifp->if_init = acx_init;
	ifp->if_ioctl = acx_ioctl;
	ifp->if_start = acx_start;
	ifp->if_watchdog = acx_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
#ifdef notyet
	ifq_set_ready(&ifp->if_snd);
#endif

	/* Set channels */
	for (i = 1; i <= 14; ++i) {
		ic->ic_channels[i].ic_freq =
			ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags = sc->chip_chan_flags;
	}

	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	/*
	 * NOTE: Don't overwrite ic_caps set by chip specific code
	 */
	ic->ic_caps |= IEEE80211_C_WEP |	/* WEP */
		       IEEE80211_C_HOSTAP |	/* HostAP mode */
		       IEEE80211_C_MONITOR |	/* Monitor mode */
		       IEEE80211_C_IBSS |	/* IBSS modes */
		       IEEE80211_C_SHPREAMBLE;	/* Short preamble */
	if (acx_enable_pbcc)
		ic->ic_caps_ext = IEEE80211_CEXT_PBCC;	/* PBCC modulation */

	/* Get station id */
	for (i = 0; i < IEEE80211_ADDR_LEN; ++i) {
		error = acx_read_eeprom(sc, sc->chip_ee_eaddr_ofs - i,
					&ic->ic_myaddr[i]);
	}

	ieee80211_ifattach(ic);

	/* Enable software beacon missing */
	ic->ic_flags_ext |= IEEE80211_FEXT_SWBMISS;

	/* Override newstate */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = acx_newstate;

	ieee80211_media_init(ic, acx_media_change, ieee80211_media_status);

	/*
	 * Radio tap attaching
	 */
	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
		      sizeof(struct ieee80211_frame) + sizeof(sc->sc_tx_th),
		      &sc->sc_drvbpf);

	sc->sc_tx_th_len = roundup(sizeof(sc->sc_tx_th), sizeof(uint32_t));
	sc->sc_tx_th.wt_ihdr.it_len = htole16(sc->sc_tx_th_len);
	sc->sc_tx_th.wt_ihdr.it_present = htole32(ACX_TX_RADIOTAP_PRESENT);

	sc->sc_rx_th_len = roundup(sizeof(sc->sc_rx_th), sizeof(uint32_t));
	sc->sc_rx_th.wr_ihdr.it_len = htole16(sc->sc_rx_th_len);
	sc->sc_rx_th.wr_ihdr.it_present = htole32(ACX_RX_RADIOTAP_PRESENT);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, acx_intr, sc,
			       &sc->sc_irq_handle, ifp->if_serializer);
	if (error) {
		device_printf(dev, "can't set up interrupt\n");
		bpfdetach(ifp);
		ieee80211_ifdetach(ic);
		goto fail;
	}

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;
fail:
	acx_detach(dev);
	return error;
}

static int
acx_detach(device_t dev)
{
	struct acx_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ieee80211com *ic = &sc->sc_ic;
		struct ifnet *ifp = &ic->ic_if;

		lwkt_serialize_enter(ifp->if_serializer);

		acx_stop(sc);
		acx_free_firmware(sc);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);

		lwkt_serialize_exit(ifp->if_serializer);

		bpfdetach(ifp);
		ieee80211_ifdetach(ic);
	}

	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq_res);
	}
	if (sc->sc_mem1_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->chip_mem1_rid,
				     sc->sc_mem1_res);
	}
	if (sc->sc_mem2_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->chip_mem2_rid,
				     sc->sc_mem2_res);
	}

	acx_dma_free(sc);
	return 0;
}

static int
acx_shutdown(device_t dev)
{
	struct acx_softc *sc = device_get_softc(dev);

	lwkt_serialize_enter(sc->sc_ic.ic_if.if_serializer);
	acx_stop(sc);
	lwkt_serialize_exit(sc->sc_ic.ic_if.if_serializer);
	return 0;
}

static void
acx_init(void *arg)
{
	struct acx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct acx_firmware *fw = &sc->sc_firmware;
	int error;

	error = acx_stop(sc);
	if (error)
		return;

	error = acx_alloc_firmware(sc);
	if (error)
		return;

	error = acx_init_tx_ring(sc);
	if (error) {
		if_printf(ifp, "can't initialize TX ring\n");
		goto back;
	}

	error = acx_init_rx_ring(sc);
	if (error) {
		if_printf(ifp, "can't initialize RX ring\n");
		goto back;
	}

	error = acx_load_base_firmware(sc, fw->base_fw, fw->base_fw_len);
	if (error)
		goto back;

	/*
	 * Initialize command and information registers
	 * NOTE: This should be done after base firmware is loaded
	 */
	acx_init_cmd_reg(sc);
	acx_init_info_reg(sc);

	sc->sc_flags |= ACX_FLAG_FW_LOADED;

#if 0
	if (sc->chip_post_basefw != NULL) {
		error = sc->chip_post_basefw(sc);
		if (error)
			goto back;
	}
#endif

	if (fw->radio_fw != NULL) {
		error = acx_load_radio_firmware(sc, fw->radio_fw,
						fw->radio_fw_len);
		if (error)
			goto back;
	}

	error = sc->chip_init(sc);
	if (error)
		goto back;

	/* Get and set device various configuration */
	error = acx_config(sc);
	if (error)
		goto back;

	/* Setup crypto stuffs */
	if (sc->sc_ic.ic_flags & IEEE80211_F_PRIVACY) {
		error = acx_set_crypt_keys(sc);
		if (error)
			goto back;
		sc->sc_ic.ic_flags &= ~IEEE80211_F_DROPUNENC;
	}

	/* Turn on power led */
	CSR_CLRB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	acx_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
			ieee80211_new_state(&sc->sc_ic, IEEE80211_S_SCAN, -1);
	} else {
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	}
back:
	if (error)
		acx_stop(sc);
}

static void
acx_init_info_reg(struct acx_softc *sc)
{
	sc->sc_info = CSR_READ_4(sc, ACXREG_INFO_REG_OFFSET);
	sc->sc_info_param = sc->sc_info + ACX_INFO_REG_SIZE;
}

static int
acx_set_crypt_keys(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_conf_wep_txkey wep_txkey;
	int i, error, got_wk = 0;

	for (i = 0; i < IEEE80211_WEP_NKID; ++i) {
		struct ieee80211_key *wk = &ic->ic_nw_keys[i];

		if (wk->wk_keylen == 0)
			continue;

		if (sc->chip_hw_crypt) {
			error = sc->chip_set_wepkey(sc, wk, i);
			if (error)
				return error;
			got_wk = 1;
		} else if (wk->wk_flags & IEEE80211_KEY_XMIT) {
			wk->wk_flags |= IEEE80211_KEY_SWCRYPT;
		}
	}

	if (!got_wk || sc->chip_hw_crypt ||
	    ic->ic_def_txkey == IEEE80211_KEYIX_NONE)
		return 0;

	/* Set current WEP key index */
	wep_txkey.wep_txkey = ic->ic_def_txkey;
	if (acx_set_wep_txkey_conf(sc, &wep_txkey) != 0) {
		if_printf(&ic->ic_if, "set WEP txkey failed\n");
		return ENXIO;
	}
	return 0;
}

static void
acx_next_scan(void *arg)
{
	struct acx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ic);

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
acx_stop(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	int i, error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);

	sc->sc_firmware_ver = 0;
	sc->sc_hardware_id = 0;

	/* Reset hardware */
	error = acx_reset(sc);
	if (error)
		return error;

	/* Firmware no longer functions after hardware reset */
	sc->sc_flags &= ~ACX_FLAG_FW_LOADED;

	acx_disable_intr(sc);

	/* Stop backgroud scanning */
	callout_stop(&sc->sc_scan_timer);

	/* Turn off power led */
	CSR_SETB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	/* Free TX mbuf */
	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		struct acx_txbuf *buf;

		buf = &bd->tx_buf[i];

		if (buf->tb_mbuf != NULL) {
			bus_dmamap_unload(bd->mbuf_dma_tag,
					  buf->tb_mbuf_dmamap);
			m_freem(buf->tb_mbuf);
			buf->tb_mbuf = NULL;
		}

		if (buf->tb_node != NULL)
			ieee80211_free_node(buf->tb_node);
		buf->tb_node = NULL;
	}

	/* Clear TX host descriptors */
	bzero(rd->tx_ring, ACX_TX_RING_SIZE);

	/* Free RX mbuf */
	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (bd->rx_buf[i].rb_mbuf != NULL) {
			bus_dmamap_unload(bd->mbuf_dma_tag,
					  bd->rx_buf[i].rb_mbuf_dmamap);
			m_freem(bd->rx_buf[i].rb_mbuf);
			bd->rx_buf[i].rb_mbuf = NULL;
		}
	}

	/* Clear RX host descriptors */
	bzero(rd->rx_ring, ACX_RX_RING_SIZE);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return 0;
}

static int
acx_config(struct acx_softc *sc)
{
	struct acx_config conf;
	int error;

	error = acx_read_config(sc, &conf);
	if (error)
		return error;

	error = acx_write_config(sc, &conf);
	if (error)
		return error;

	error = acx_rx_config(sc, sc->sc_flags & ACX_FLAG_PROMISC);
	if (error)
		return error;

	if (acx_set_probe_req_tmplt(sc, "", 0) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set probe req template "
			  "(empty ssid)\n");
		return ENXIO;
	}

	/* XXX for PM?? */
	if (acx_set_null_tmplt(sc) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set null data template\n");
		return ENXIO;
	}
	return 0;
}

static int
acx_read_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx_conf_eaddr addr;
	struct acx_conf_regdom reg_dom;
	struct acx_conf_antenna ant;
	struct acx_conf_fwrev fw_rev;
	char ethstr[ETHER_ADDRSTRLEN + 1];
	uint32_t fw_rev_no;
	uint8_t sen;
	int i, error;

	/* Get station id */
	if (acx_get_eaddr_conf(sc, &addr) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't get station id\n");
		return ENXIO;
	}

	/*
	 * Get and print station id in case that EEPROM station id's
	 * offset is not correct
	 */
	for (i = 0; i < IEEE80211_ADDR_LEN; ++i)
		conf->eaddr[IEEE80211_ADDR_LEN - 1 - i] = addr.eaddr[i];
	if_printf(&sc->sc_ic.ic_if, "MAC address (from firmware): %s\n",
	    kether_ntoa(conf->eaddr, ethstr));

	/* Get region domain */
	if (acx_get_regdom_conf(sc, &reg_dom) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't get region domain\n");
		return ENXIO;
	}
	conf->regdom = reg_dom.regdom;
	DPRINTF((&sc->sc_ic.ic_if, "regdom %02x\n", reg_dom.regdom));

	/* Get antenna */
	if (acx_get_antenna_conf(sc, &ant) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't get antenna\n");
		return ENXIO;
	}
	conf->antenna = ant.antenna;
	DPRINTF((&sc->sc_ic.ic_if, "antenna %02x\n", ant.antenna));

	/* Get sensitivity XXX not used */
	if (sc->sc_radio_type == ACX_RADIO_TYPE_MAXIM ||
	    sc->sc_radio_type == ACX_RADIO_TYPE_RFMD ||
	    sc->sc_radio_type == ACX_RADIO_TYPE_RALINK) {
	    	error = acx_read_phyreg(sc, ACXRV_PHYREG_SENSITIVITY, &sen);
	    	if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't get sensitivity\n");
			return error;
		}
	} else {
		sen = 0;
	}
	DPRINTF((&sc->sc_ic.ic_if, "sensitivity %02x\n", sen));

	/* Get firmware revision */
	if (acx_get_fwrev_conf(sc, &fw_rev) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't get firmware revision\n");
		return ENXIO;
	}

	if (strncmp(fw_rev.fw_rev, "Rev ", 4) != 0) {
		if_printf(&sc->sc_ic.ic_if, "strange revision string -- %s\n",
			  fw_rev.fw_rev);
		fw_rev_no = 0x01090407;
	} else {
		char *s, *endp;

		/*
		 *  01234
		 * "Rev xx.xx.xx.xx"
		 *      ^ Start from here
		 */
		s = &fw_rev.fw_rev[4];
		fw_rev_no = 0;
		for (i = 0; i < 4; ++i) {
			uint8_t val;

			val = strtoul(s, &endp, 16);
			fw_rev_no |= val << ((3 - i) * 8);

			if (*endp == '\0')
				break;
			else
				s = ++endp;
		}
	}
	sc->sc_firmware_ver = fw_rev_no;
	sc->sc_hardware_id = le32toh(fw_rev.hw_id);
	DPRINTF((&sc->sc_ic.ic_if, "fw rev %08x, hw id %08x\n",
		 sc->sc_firmware_ver, sc->sc_hardware_id));

	if (sc->chip_read_config != NULL) {
		error = sc->chip_read_config(sc, conf);
		if (error)
			return error;
	}
	return 0;
}

static int
acx_write_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx_conf_nretry_short sretry;
	struct acx_conf_nretry_long lretry;
	struct acx_conf_msdu_lifetime msdu_lifetime;
	struct acx_conf_rate_fallback rate_fb;
	struct acx_conf_antenna ant;
	struct acx_conf_regdom reg_dom;
	int error;

	/* Set number of long/short retry */
	KKASSERT(sc->chip_short_retry_limit > 0);
	sretry.nretry = sc->chip_short_retry_limit;
	if (acx_set_nretry_short_conf(sc, &sretry) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set short retry limit\n");
		return ENXIO;
	}

	lretry.nretry = sc->sc_long_retry_limit;
	if (acx_set_nretry_long_conf(sc, &lretry) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set long retry limit\n");
		return ENXIO;
	}

	/* Set MSDU lifetime */
	msdu_lifetime.lifetime = htole32(sc->sc_msdu_lifetime);
	if (acx_set_msdu_lifetime_conf(sc, &msdu_lifetime) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set MSDU lifetime\n");
		return ENXIO;
	}

	/* Enable rate fallback */
	rate_fb.ratefb_enable = 1;
	if (acx_set_rate_fallback_conf(sc, &rate_fb) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't enable rate fallback\n");
		return ENXIO;
	}

	/* Set antenna */
	ant.antenna = conf->antenna;
	if (acx_set_antenna_conf(sc, &ant) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set antenna\n");
		return ENXIO;
	}

	/* Set region domain */
	reg_dom.regdom = conf->regdom;
	if (acx_set_regdom_conf(sc, &reg_dom) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set region domain\n");
		return ENXIO;
	}

	if (sc->chip_write_config != NULL) {
		error = sc->chip_write_config(sc, conf);
		if (error)
			return error;
	}

	return 0;
}

static int
acx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING)) {
				int promisc = -1;

				if ((ifp->if_flags & IFF_PROMISC) &&
				    (sc->sc_flags & ACX_FLAG_PROMISC) == 0)
					promisc = 1;
				else if ((ifp->if_flags & IFF_PROMISC) == 0 &&
					 (sc->sc_flags & ACX_FLAG_PROMISC))
					promisc = 0;

				/*
				 * Promisc mode is always enabled when
				 * operation mode is Monitor.
				 */
				if (ic->ic_opmode != IEEE80211_M_MONITOR &&
				    promisc >= 0)
					error = acx_rx_config(sc, promisc);
			} else {
				acx_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				acx_stop(sc);
		}

		if (ifp->if_flags & IFF_PROMISC)
			sc->sc_flags |= ACX_FLAG_PROMISC;
		else
			sc->sc_flags &= ~ACX_FLAG_PROMISC;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* TODO */
		break;
	default:
		error = ieee80211_ioctl(ic, cmd, data, cr);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING | IFF_UP)) ==
		    (IFF_RUNNING | IFF_UP))
			acx_init(sc);
		error = 0;
	}
	return error;
}

static void
acx_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_txbuf *buf;
	int trans, idx;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0) {
		ifq_purge(&ifp->if_snd);
		ieee80211_drain_mgtq(&ic->ic_mgtq);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(ifp->if_snd))
		return;

	/*
	 * NOTE:
	 * We can't start from a random position that TX descriptor
	 * is free, since hardware will be confused by that.
	 * We have to follow the order of the TX ring.
	 */
	idx = bd->tx_free_start;
	trans = 0;
	for (buf = &bd->tx_buf[idx]; buf->tb_mbuf == NULL;
	     buf = &bd->tx_buf[idx]) {
		struct ieee80211_frame *f;
		struct ieee80211_node *ni = NULL;
		struct mbuf *m;
		int mgmt_pkt = 0;

		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			IF_DEQUEUE(&ic->ic_mgtq, m);

			ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
			m->m_pkthdr.rcvif = NULL;

			mgmt_pkt = 1;

			/*
			 * Don't transmit probe response firmware will
			 * do it for us.
			 */
			f = mtod(m, struct ieee80211_frame *);
			if ((f->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
			    IEEE80211_FC0_TYPE_MGT &&
			    (f->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
				if (ni != NULL)
					ieee80211_free_node(ni);
				m_freem(m);
				continue;
			}
		} else if (!ifq_is_empty(&ifp->if_snd)) {
			struct ether_header *eh;

			if (ic->ic_state != IEEE80211_S_RUN) {
				ifq_purge(&ifp->if_snd);
				break;
			}

			m = ifq_dequeue(&ifp->if_snd);
			if (m == NULL)
				break;

			if (m->m_len < sizeof(struct ether_header)) {
				m = m_pullup(m, sizeof(struct ether_header));
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

			/* TODO power save */

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

		f = mtod(m, struct ieee80211_frame *);
		if ((f->i_fc[1] & IEEE80211_FC1_PROTECTED) && !sc->chip_hw_crypt) {
			KASSERT(ni != NULL, ("TX node is NULL (WEP)"));
			if (ieee80211_crypto_encap(ic, ni, m) == NULL) {
				ieee80211_free_node(ni);
				m_freem(m);
				IFNET_STAT_INC(ifp, oerrors, 1);
				continue;
			}
		}

		/*
		 * Since mgmt data are transmitted at fixed rate
		 * they will not be used to do rate control.
		 */
		if (mgmt_pkt && ni != NULL) {
			ieee80211_free_node(ni);
			ni = NULL;
		}

		if (acx_encap(sc, buf, m, ni) != 0) {
			/*
			 * NOTE: `m' will be freed in acx_encap()
			 * if we reach here.
			 */
			if (ni != NULL)
				ieee80211_free_node(ni);
			IFNET_STAT_INC(ifp, oerrors, 1);
			continue;
		}

		/*
		 * NOTE:
		 * 1) `m' should not be touched after acx_encap()
		 * 2) `node' will be used to do TX rate control during
		 *    acx_txeof(), so it is not freed here.  acx_txeof()
		 *    will free it for us
		 */

		trans = 1;
		bd->tx_used_count++;
		idx = (idx + 1) % ACX_TX_DESC_CNT;
	}
	bd->tx_free_start = idx;

	if (bd->tx_used_count == ACX_TX_DESC_CNT)
		ifq_set_oactive(&ifp->if_snd);

	if (trans && sc->sc_tx_timer == 0)
		sc->sc_tx_timer = 5;
	ifp->if_timer = 1;
}

static void
acx_watchdog(struct ifnet *ifp)
{
	struct acx_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0) {
			if_printf(ifp, "watchdog timeout\n");
			IFNET_STAT_INC(ifp, oerrors, 1);
			acx_txeof(ifp->if_softc);
		} else {
			ifp->if_timer = 1;
		}
	}
	ieee80211_watchdog(&sc->sc_ic);
}

static void
acx_intr(void *arg)
{
	struct acx_softc *sc = arg;
	uint16_t intr_status;

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0)
		return;

	intr_status = CSR_READ_2(sc, ACXREG_INTR_STATUS_CLR);
	if (intr_status == ACXRV_INTR_ALL) {
		/* not our interrupt */
		return;
	}

	intr_status &= sc->chip_intr_enable;
	if (intr_status == 0) {
		/* not interrupts we care about */
		return;
	}

	/* Acknowledge all interrupts */
	CSR_WRITE_2(sc, ACXREG_INTR_ACK, ACXRV_INTR_ALL);

	if (intr_status & ACXRV_INTR_TX_FINI)
		acx_txeof(sc);

	if (intr_status & ACXRV_INTR_RX_FINI)
		acx_rxeof(sc);
}

static void
acx_disable_intr(struct acx_softc *sc)
{
	CSR_WRITE_2(sc, ACXREG_INTR_MASK, sc->chip_intr_disable);
	CSR_WRITE_2(sc, ACXREG_EVENT_MASK, 0);
}

static void
acx_enable_intr(struct acx_softc *sc)
{
	/* Mask out interrupts that are not in the enable set */
	CSR_WRITE_2(sc, ACXREG_INTR_MASK, ~sc->chip_intr_enable);
	CSR_WRITE_2(sc, ACXREG_EVENT_MASK, ACXRV_EVENT_DISABLE);
}

static void
acx_txeof(struct acx_softc *sc)
{
	struct acx_buf_data *bd;
	struct acx_txbuf *buf;
	struct ifnet *ifp;
	int idx;

	ifp = &sc->sc_ic.ic_if;
	ASSERT_SERIALIZED(ifp->if_serializer);

	bd = &sc->sc_buf_data;
	idx = bd->tx_used_start;
	for (buf = &bd->tx_buf[idx]; buf->tb_mbuf != NULL;
	     buf = &bd->tx_buf[idx]) {
		uint8_t ctrl, error;
		int frame_len;

		ctrl = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_ctrl);
		if ((ctrl & (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE)) !=
		    (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE))
			break;

		bus_dmamap_unload(bd->mbuf_dma_tag, buf->tb_mbuf_dmamap);
		frame_len = buf->tb_mbuf->m_pkthdr.len;
		m_freem(buf->tb_mbuf);
		buf->tb_mbuf = NULL;

		error = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_error);
		if (error) {
			acx_txerr(sc, error);
			IFNET_STAT_INC(ifp, oerrors, 1);
		} else {
			IFNET_STAT_INC(ifp, opackets, 1);
		}

		if (buf->tb_node != NULL) {
			sc->chip_tx_complete(sc, buf, frame_len, error);
			ieee80211_free_node(buf->tb_node);
			buf->tb_node = NULL;
		}

		FW_TXDESC_SETFIELD_1(sc, buf, f_tx_ctrl, DESC_CTRL_HOSTOWN);

		bd->tx_used_count--;

		idx = (idx + 1) % ACX_TX_DESC_CNT;
	}
	bd->tx_used_start = idx;

	sc->sc_tx_timer = bd->tx_used_count == 0 ? 0 : 5;

	if (bd->tx_used_count != ACX_TX_DESC_CNT) {
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_start(ifp);
	}
}

static void
acx_txerr(struct acx_softc *sc, uint8_t err)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct acx_stats *stats = &sc->sc_stats;

	if (err == DESC_ERR_EXCESSIVE_RETRY) {
		/*
		 * This a common error (see comment below),
		 * so print it using DPRINTF()
		 */
		DPRINTF((ifp, "TX failed -- excessive retry\n"));
	} else {
		if_printf(ifp, "TX failed -- ");
	}

	/*
	 * Although `err' looks like bitmask, it never
	 * has multiple bits set.
	 */
	switch (err) {
#if 0
	case DESC_ERR_OTHER_FRAG:
		/* XXX what's this */
		kprintf("error in other fragment\n");
		stats->err_oth_frag++;
		break;
#endif
	case DESC_ERR_ABORT:
		kprintf("aborted\n");
		stats->err_abort++;
		break;
	case DESC_ERR_PARAM:
		kprintf("wrong parameters in descriptor\n");
		stats->err_param++;
		break;
	case DESC_ERR_NO_WEPKEY:
		kprintf("WEP key missing\n");
		stats->err_no_wepkey++;
		break;
	case DESC_ERR_MSDU_TIMEOUT:
		kprintf("MSDU life timeout\n");
		stats->err_msdu_timeout++;
		break;
	case DESC_ERR_EXCESSIVE_RETRY:
		/*
		 * Possible causes:
		 * 1) Distance is too long
		 * 2) Transmit failed (e.g. no MAC level ACK)
		 * 3) Chip overheated (this should be rare)
		 */
		stats->err_ex_retry++;
		break;
	case DESC_ERR_BUF_OVERFLOW:
		kprintf("buffer overflow\n");
		stats->err_buf_oflow++;
		break;
	case DESC_ERR_DMA:
		kprintf("DMA error\n");
		stats->err_dma++;
		break;
	default:
		kprintf("unknown error %d\n", err);
		stats->err_unkn++;
		break;
	}
}

static void
acx_rxeof(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct ifnet *ifp = &ic->ic_if;
	int idx, ready;

	ASSERT_SERIALIZED(ic->ic_if.if_serializer);

	bus_dmamap_sync(rd->rx_ring_dma_tag, rd->rx_ring_dmamap,
			BUS_DMASYNC_POSTREAD);

	/*
	 * Locate first "ready" rx buffer,
	 * start from last stopped position
	 */
	idx = bd->rx_scan_start;
	ready = 0;
	do {
		struct acx_rxbuf *buf;

		buf = &bd->rx_buf[idx];
		if ((buf->rb_desc->h_ctrl & htole16(DESC_CTRL_HOSTOWN)) &&
		    (buf->rb_desc->h_status & htole32(DESC_STATUS_FULL))) {
			ready = 1;
			break;
		}
		idx = (idx + 1) % ACX_RX_DESC_CNT;
	} while (idx != bd->rx_scan_start);

	if (!ready)
		return;

	/*
	 * NOTE: don't mess up `idx' here, it will
	 * be used in the following code
	 */

	do {
		struct acx_rxbuf_hdr *head;
		struct acx_rxbuf *buf;
		struct ieee80211_frame_min *wh;
		struct mbuf *m;
		uint32_t desc_status;
		uint16_t desc_ctrl;
		int len, error, rssi, is_priv;

		buf = &bd->rx_buf[idx];

		desc_ctrl = le16toh(buf->rb_desc->h_ctrl);
		desc_status = le32toh(buf->rb_desc->h_status);
		if (!(desc_ctrl & DESC_CTRL_HOSTOWN) ||
		    !(desc_status & DESC_STATUS_FULL))
			break;

		bus_dmamap_sync(bd->mbuf_dma_tag, buf->rb_mbuf_dmamap,
				BUS_DMASYNC_POSTREAD);

		m = buf->rb_mbuf;

		error = acx_newbuf(sc, buf, 0);
		if (error) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto next;
		}

		head = mtod(m, struct acx_rxbuf_hdr *);
		len = le16toh(head->rbh_len) & ACX_RXBUF_LEN_MASK;
		rssi = acx_get_rssi(sc, head->rbh_level);

		m_adj(m, sizeof(struct acx_rxbuf_hdr) + sc->chip_rxbuf_exhdr);
		m->m_len = m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = &ic->ic_if;

		wh = mtod(m, struct ieee80211_frame_min *);
		is_priv = (wh->i_fc[1] & IEEE80211_FC1_PROTECTED);

		if (sc->sc_drvbpf != NULL) {
			sc->sc_rx_th.wr_tsf = htole32(head->rbh_time);

			sc->sc_rx_th.wr_flags = 0;
			if (is_priv) {
				sc->sc_rx_th.wr_flags |=
					IEEE80211_RADIOTAP_F_WEP;
			}
			if (head->rbh_bbp_stat & ACX_RXBUF_STAT_SHPRE) {
				sc->sc_rx_th.wr_flags |=
					IEEE80211_RADIOTAP_F_SHORTPRE;
			}

			if (sc->chip_phymode == IEEE80211_MODE_11G) {
				sc->sc_rx_th.wr_rate =
				    ieee80211_plcp2rate(head->rbh_plcp,
				    head->rbh_bbp_stat & ACX_RXBUF_STAT_OFDM);
			} else {
				sc->sc_rx_th.wr_rate =
				    ieee80211_plcp2rate(head->rbh_plcp, 0);
			}

			sc->sc_rx_th.wr_antsignal = rssi;

			if (head->rbh_bbp_stat & ACX_RXBUF_STAT_ANT1)
				sc->sc_rx_th.wr_antenna = 1;
			else
				sc->sc_rx_th.wr_antenna = 0;

			bpf_ptap(sc->sc_drvbpf, m, &sc->sc_rx_th,
				 sc->sc_rx_th_len);
		}

		if (len >= sizeof(struct ieee80211_frame_min) &&
		    len < MCLBYTES) {
			struct ieee80211_node *ni;

			if (is_priv && sc->chip_hw_crypt) {
				/* Short circuit software WEP */
				wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;

				/* Do chip specific RX buffer processing */
				if (sc->chip_proc_wep_rxbuf != NULL) {
					sc->chip_proc_wep_rxbuf(sc, m, &len);
					wh = mtod(m,
					     struct ieee80211_frame_min *);
				}
			}
			m->m_len = m->m_pkthdr.len = len;

			ni = ieee80211_find_rxnode(ic, wh);
			ieee80211_input(ic, m, ni, rssi,
					le32toh(head->rbh_time));
			ieee80211_free_node(ni);

			IFNET_STAT_INC(ifp, ipackets, 1);
		} else {
			if (len < sizeof(struct ieee80211_frame_min)) {
				if (ic->ic_rawbpf != NULL &&
				    len >= sizeof(struct ieee80211_frame_ack))
					bpf_mtap(ic->ic_rawbpf, m);

				if (ic->ic_opmode != IEEE80211_M_MONITOR)
					ic->ic_stats.is_rx_tooshort++;
			}
			m_freem(m);
		}
next:
		buf->rb_desc->h_ctrl = htole16(desc_ctrl & ~DESC_CTRL_HOSTOWN);
		buf->rb_desc->h_status = 0;
		bus_dmamap_sync(rd->rx_ring_dma_tag, rd->rx_ring_dmamap,
				BUS_DMASYNC_PREWRITE);

		idx = (idx + 1) % ACX_RX_DESC_CNT;
	} while (idx != bd->rx_scan_start);

	/*
	 * Record the position so that next
	 * time we can start from it
	 */
	bd->rx_scan_start = idx;
}

static int
acx_reset(struct acx_softc *sc)
{
	uint16_t reg;

	/* Halt ECPU */
	CSR_SETB_2(sc, ACXREG_ECPU_CTRL, ACXRV_ECPU_HALT);

	/* Software reset */
	reg = CSR_READ_2(sc, ACXREG_SOFT_RESET);
	CSR_WRITE_2(sc, ACXREG_SOFT_RESET, reg | ACXRV_SOFT_RESET);
	DELAY(100);
	CSR_WRITE_2(sc, ACXREG_SOFT_RESET, reg);

	/* Initialize EEPROM */
	CSR_SETB_2(sc, ACXREG_EEPROM_INIT, ACXRV_EEPROM_INIT);
	DELAY(50000);

	/* Test whether ECPU is stopped */
	reg = CSR_READ_2(sc, ACXREG_ECPU_CTRL);
	if (!(reg & ACXRV_ECPU_HALT)) {
		if_printf(&sc->sc_ic.ic_if, "can't halt ECPU\n");
		return ENXIO;
	}
	return 0;
}

static int
acx_read_eeprom(struct acx_softc *sc, uint32_t offset, uint8_t *val)
{
	int i;

	CSR_WRITE_4(sc, ACXREG_EEPROM_CONF, 0);
	CSR_WRITE_4(sc, ACXREG_EEPROM_ADDR, offset);
	CSR_WRITE_4(sc, ACXREG_EEPROM_CTRL, ACXRV_EEPROM_READ);

#define EE_READ_RETRY_MAX	100
	for (i = 0; i < EE_READ_RETRY_MAX; ++i) {
		if (CSR_READ_2(sc, ACXREG_EEPROM_CTRL) == 0)
			break;
		DELAY(10000);
	}
	if (i == EE_READ_RETRY_MAX) {
		if_printf(&sc->sc_ic.ic_if, "can't read EEPROM offset %x "
			  "(timeout)\n", offset);
		return ETIMEDOUT;
	}
#undef EE_READ_RETRY_MAX

	*val = CSR_READ_1(sc, ACXREG_EEPROM_DATA);
	return 0;
}

static int
acx_read_phyreg(struct acx_softc *sc, uint32_t reg, uint8_t *val)
{
	int i;

	CSR_WRITE_4(sc, ACXREG_PHY_ADDR, reg);
	CSR_WRITE_4(sc, ACXREG_PHY_CTRL, ACXRV_PHY_READ);

#define PHY_READ_RETRY_MAX	100
	for (i = 0; i < PHY_READ_RETRY_MAX; ++i) {
		if (CSR_READ_4(sc, ACXREG_PHY_CTRL) == 0)
			break;
		DELAY(10000);
	}
	if (i == PHY_READ_RETRY_MAX) {
		if_printf(&sc->sc_ic.ic_if, "can't read phy reg %x (timeout)\n",
			  reg);
		return ETIMEDOUT;
	}
#undef PHY_READ_RETRY_MAX

	*val = CSR_READ_1(sc, ACXREG_PHY_DATA);
	return 0;
}

void
acx_write_phyreg(struct acx_softc *sc, uint32_t reg, uint8_t val)
{
	CSR_WRITE_4(sc, ACXREG_PHY_DATA, val);
	CSR_WRITE_4(sc, ACXREG_PHY_ADDR, reg);
	CSR_WRITE_4(sc, ACXREG_PHY_CTRL, ACXRV_PHY_WRITE);
}

static int
acx_alloc_firmware(struct acx_softc *sc)
{
	struct acx_firmware *fw = &sc->sc_firmware;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct fw_image *img;
	char filename[64];
	int error = 0;

	/*
	 * NB: serializer need to be released before loading firmware
	 *     image to avoid possible dead lock
	 */
	ASSERT_SERIALIZED(ifp->if_serializer);

	if (fw->base_fw_image == NULL) {
		if (fw->combined_radio_fw) {
			ksnprintf(filename, sizeof(filename),
				  ACX_BASE_RADIO_FW_PATH,
				  fw->fwdir, sc->sc_radio_type);
		} else {
			ksnprintf(filename, sizeof(filename),
				  ACX_BASE_FW_PATH, fw->fwdir);
		}

		lwkt_serialize_exit(ifp->if_serializer);
		img = firmware_image_load(filename, NULL);
		lwkt_serialize_enter(ifp->if_serializer);

		fw->base_fw_image = img;
		if (fw->base_fw_image == NULL) {
			if_printf(ifp, "load %s base fw failed\n", filename);
			error = EIO;
			goto back;
		}

		error = acx_setup_firmware(sc, fw->base_fw_image,
					   &fw->base_fw, &fw->base_fw_len);
		if (error)
			goto back;
	}

	if (!fw->combined_radio_fw && fw->radio_fw_image == NULL) {
		ksnprintf(filename, sizeof(filename), ACX_RADIO_FW_PATH,
			  fw->fwdir, sc->sc_radio_type);

		lwkt_serialize_exit(ifp->if_serializer);
		img = firmware_image_load(filename, NULL);
		lwkt_serialize_enter(ifp->if_serializer);

		fw->radio_fw_image = img;
		if (fw->radio_fw_image == NULL) {
			if_printf(ifp, "load %s radio fw failed\n", filename);
			error = EIO;
			goto back;
		}

		error = acx_setup_firmware(sc, fw->radio_fw_image,
					   &fw->radio_fw, &fw->radio_fw_len);
	}
back:
	if (error)
		acx_free_firmware(sc);
	return error;
}

static int
acx_setup_firmware(struct acx_softc *sc, struct fw_image *img,
		   const uint8_t **ptr, int *len)
{
	const struct acx_firmware_hdr *hdr;
	const uint8_t *p;
	uint32_t cksum;
	int i;

	*ptr = NULL;
	*len = 0;

	/*
	 * Make sure that the firmware image contains more than just a header
	 */
	if (img->fw_imglen <= sizeof(*hdr)) {
		if_printf(&sc->sc_ic.ic_if, "%s is invalid image, "
			  "size %zu (too small)\n",
			  img->fw_name, img->fw_imglen);
		return EINVAL;
	}
	hdr = (const struct acx_firmware_hdr *)img->fw_image;

	/*
	 * Verify length
	 */
	if (hdr->fwh_len != img->fw_imglen - sizeof(*hdr)) {
		if_printf(&sc->sc_ic.ic_if, "%s is invalid image, "
			  "size in hdr %u and image size %zu mismatches\n",
			  img->fw_name, hdr->fwh_len, img->fw_imglen);
		return EINVAL;
	}

	/*
	 * Verify cksum
	 */
	cksum = 0;
	for (i = 0, p = (const uint8_t *)&hdr->fwh_len;
	     i < img->fw_imglen - sizeof(hdr->fwh_cksum); ++i, ++p)
		cksum += *p;
	if (cksum != hdr->fwh_cksum) {
		if_printf(&sc->sc_ic.ic_if, "%s is invalid image, "
			  "checksum mismatch\n", img->fw_name);
		return EINVAL;
	}

	*ptr = ((const uint8_t *)img->fw_image + sizeof(*hdr));
	*len = img->fw_imglen - sizeof(*hdr);
	return 0;
}

static void
acx_free_firmware(struct acx_softc *sc)
{
	struct acx_firmware *fw = &sc->sc_firmware;

	if (fw->base_fw_image != NULL) {
		firmware_image_unload(fw->base_fw_image);
		fw->base_fw_image = NULL;
		fw->base_fw = NULL;
		fw->base_fw_len = 0;
	}
	if (fw->radio_fw_image != NULL) {
		firmware_image_unload(fw->radio_fw_image);
		fw->radio_fw_image = NULL;
		fw->radio_fw = NULL;
		fw->radio_fw_len = 0;
	}
}

static int
acx_load_base_firmware(struct acx_softc *sc, const uint8_t *base_fw,
		       uint32_t base_fw_len)
{
	int i, error;

	/* Load base firmware */
	error = acx_load_firmware(sc, 0, base_fw, base_fw_len);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't load base firmware\n");
		return error;
	}
	DPRINTF((&sc->sc_ic.ic_if, "base firmware loaded\n"));

	/* Start ECPU */
	CSR_WRITE_2(sc, ACXREG_ECPU_CTRL, ACXRV_ECPU_START);

	/* Wait for ECPU to be up */
	for (i = 0; i < 500; ++i) {
		uint16_t reg;

		reg = CSR_READ_2(sc, ACXREG_INTR_STATUS);
		if (reg & ACXRV_INTR_FCS_THRESH) {
			CSR_WRITE_2(sc, ACXREG_INTR_ACK, ACXRV_INTR_FCS_THRESH);
			return 0;
		}
		DELAY(10000);
	}

	if_printf(&sc->sc_ic.ic_if, "can't initialize ECPU (timeout)\n");
	return ENXIO;
}

static int
acx_load_radio_firmware(struct acx_softc *sc, const uint8_t *radio_fw,
			uint32_t radio_fw_len)
{
	struct acx_conf_mmap mem_map;
	uint32_t radio_fw_ofs;
	int error;

	/*
	 * Get the position, where base firmware is loaded, so that
	 * radio firmware can be loaded after it.
	 */
	if (acx_get_mmap_conf(sc, &mem_map) != 0)
		return ENXIO;
	radio_fw_ofs = le32toh(mem_map.code_end);

	/* Put ECPU into sleeping state, before loading radio firmware */
	if (acx_sleep(sc) != 0)
		return ENXIO;

	/* Load radio firmware */
	error = acx_load_firmware(sc, radio_fw_ofs, radio_fw, radio_fw_len);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't load radio firmware\n");
		return ENXIO;
	}
	DPRINTF((&sc->sc_ic.ic_if, "radio firmware loaded\n"));

	/* Wake up sleeping ECPU, after radio firmware is loaded */
	if (acx_wakeup(sc) != 0)
		return ENXIO;

	/* Initialize radio */
	if (acx_init_radio(sc, radio_fw_ofs, radio_fw_len) != 0)
		return ENXIO;

	/* Verify radio firmware's loading position */
	if (acx_get_mmap_conf(sc, &mem_map) != 0)
		return ENXIO;
	if (le32toh(mem_map.code_end) != radio_fw_ofs + radio_fw_len) {
		if_printf(&sc->sc_ic.ic_if, "loaded radio firmware position "
			  "mismatch\n");
		return ENXIO;
	}

	DPRINTF((&sc->sc_ic.ic_if, "radio firmware initialized\n"));
	return 0;
}

static int
acx_load_firmware(struct acx_softc *sc, uint32_t offset, const uint8_t *data,
		  int data_len)
{
	const uint32_t *fw;
	int i, fw_len;

	fw = (const uint32_t *)data;
	fw_len = data_len / sizeof(uint32_t);

	/*
	 * LOADFW_AUTO_INC only works with some older firmware:
	 * 1) acx100's firmware
	 * 2) acx111's firmware whose rev is 0x00010011
	 */

	/* Load firmware */
	CSR_WRITE_4(sc, ACXREG_FWMEM_START, ACXRV_FWMEM_START_OP);
#ifndef LOADFW_AUTO_INC
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, 0);
#else
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, ACXRV_FWMEM_ADDR_AUTOINC);
	CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset);
#endif

	for (i = 0; i < fw_len; ++i) {
#ifndef LOADFW_AUTO_INC
		CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset + (i * 4));
#endif
		CSR_WRITE_4(sc, ACXREG_FWMEM_DATA, be32toh(fw[i]));
	}

	/* Verify firmware */
	CSR_WRITE_4(sc, ACXREG_FWMEM_START, ACXRV_FWMEM_START_OP);
#ifndef LOADFW_AUTO_INC
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, 0);
#else
	CSR_WRITE_4(sc, ACXREG_FWMEM_CTRL, ACXRV_FWMEM_ADDR_AUTOINC);
	CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset);
#endif

	for (i = 0; i < fw_len; ++i) {
		uint32_t val;

#ifndef LOADFW_AUTO_INC
		CSR_WRITE_4(sc, ACXREG_FWMEM_ADDR, offset + (i * 4));
#endif
		val = CSR_READ_4(sc, ACXREG_FWMEM_DATA);
		if (be32toh(fw[i]) != val) {
			if_printf(&sc->sc_ic.ic_if, "fireware mismatch "
				  "fw %08x  loaded %08x\n", fw[i], val);
			return ENXIO;
		}
	}
	return 0;
}

static int
acx_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211_node *ni = NULL;
	struct ieee80211_channel *c = NULL;
	int error = 1, mode = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ieee80211_ratectl_newstate(ic, nstate);
	callout_stop(&sc->sc_scan_timer);
	callout_stop(&sc->sc_calibrate_timer);

	switch (nstate) {
	case IEEE80211_S_SCAN:
		acx_set_chan(sc, ic->ic_curchan);
		callout_reset(&sc->sc_scan_timer,
			      (hz * sc->sc_scan_dwell) / 1000,
			      acx_next_scan, sc);
		break;
	case IEEE80211_S_AUTH:
		if (ic->ic_opmode == IEEE80211_M_STA) {
			ni = ic->ic_bss;
			c = ni->ni_chan;
			mode = ACX_MODE_STA;
		}
		break;
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_IBSS ||
		    ic->ic_opmode == IEEE80211_M_HOSTAP) {
			ni = ic->ic_bss;
			c = ni->ni_chan;
			if (ic->ic_opmode == IEEE80211_M_IBSS)
				mode = ACX_MODE_ADHOC;
			else
				mode = ACX_MODE_AP;

			if (acx_set_beacon_tmplt(sc, ni) != 0) {
				if_printf(ifp, "set bescon template failed\n");
				goto back;
			}
			if (acx_set_probe_resp_tmplt(sc, ni) != 0) {
				if_printf(ifp, "set probe response template"
					  " failed\n");
				goto back;
			}
		} else if (ic->ic_opmode == IEEE80211_M_MONITOR) {
			ni = ic->ic_bss;
			c = ic->ic_curchan;
			mode = ACX_MODE_STA;
		}
		break;
	default:
		break;
	}

	if (ni != NULL) {
		KKASSERT(c != NULL);

		if (acx_set_chan(sc, c) != 0)
			goto back;

		if (acx_join_bss(sc, mode, ni, c) != 0) {
			if_printf(ifp, "join BSS failed\n");
			goto back;
		}
	}

	if (nstate == IEEE80211_S_RUN) {
		int interval = sc->sc_calib_intvl;

		if (sc->chip_calibrate != NULL) {
			error = sc->chip_calibrate(sc);
			if (error) {
				/*
				 * Restart calibration some time later
				 */
				interval = 10;
			}
			callout_reset(&sc->sc_calibrate_timer,
			      	      hz * interval, acx_calibrate, sc);
		}
	}
	error = 0;
back:
	if (error) {
		/* XXX */
		nstate = IEEE80211_S_INIT;
		arg = -1;
	}
	return sc->sc_newstate(ic, nstate, arg);
}

int
acx_init_tmplt_ordered(struct acx_softc *sc)
{
#define INIT_TMPLT(name)			\
do {						\
	if (acx_init_##name##_tmplt(sc) != 0)	\
		return 1;			\
} while (0)

	/*
	 * NOTE:
	 * Order of templates initialization:
	 * 1) Probe request
	 * 2) NULL data
	 * 3) Beacon
	 * 4) TIM
	 * 5) Probe response
	 * Above order is critical to get a correct memory map.
	 */
	INIT_TMPLT(probe_req);
	INIT_TMPLT(null_data);
	INIT_TMPLT(beacon);
	INIT_TMPLT(tim);
	INIT_TMPLT(probe_resp);

#undef INIT_TMPLT
	return 0;
}

static void
acx_ring_dma_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	*((uint32_t *)arg) = seg->ds_addr;
}

static int
acx_dma_alloc(struct acx_softc *sc)
{
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	int i, error;

	/* Allocate DMA stuffs for RX descriptors  */
	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   ACX_RX_RING_SIZE, 1, ACX_RX_RING_SIZE,
				   0, &rd->rx_ring_dma_tag);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't create rx ring dma tag\n");
		return error;
	}

	error = bus_dmamem_alloc(rd->rx_ring_dma_tag, (void **)&rd->rx_ring,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &rd->rx_ring_dmamap);
	if (error) {
		if_printf(&sc->sc_ic.ic_if,
			  "can't allocate rx ring dma memory\n");
		bus_dma_tag_destroy(rd->rx_ring_dma_tag);
		rd->rx_ring_dma_tag = NULL;
		return error;
	}

	error = bus_dmamap_load(rd->rx_ring_dma_tag, rd->rx_ring_dmamap,
				rd->rx_ring, ACX_RX_RING_SIZE,
				acx_ring_dma_addr, &rd->rx_ring_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't get rx ring dma address\n");
		bus_dmamem_free(rd->rx_ring_dma_tag, rd->rx_ring,
				rd->rx_ring_dmamap);
		bus_dma_tag_destroy(rd->rx_ring_dma_tag);
		rd->rx_ring_dma_tag = NULL;
		return error;
	}

	/* Allocate DMA stuffs for TX descriptors */
	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   ACX_TX_RING_SIZE, 1, ACX_TX_RING_SIZE,
				   0, &rd->tx_ring_dma_tag);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't create tx ring dma tag\n");
		return error;
	}

	error = bus_dmamem_alloc(rd->tx_ring_dma_tag, (void **)&rd->tx_ring,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &rd->tx_ring_dmamap);
	if (error) {
		if_printf(&sc->sc_ic.ic_if,
			  "can't allocate tx ring dma memory\n");
		bus_dma_tag_destroy(rd->tx_ring_dma_tag);
		rd->tx_ring_dma_tag = NULL;
		return error;
	}

	error = bus_dmamap_load(rd->tx_ring_dma_tag, rd->tx_ring_dmamap,
				rd->tx_ring, ACX_TX_RING_SIZE,
				acx_ring_dma_addr, &rd->tx_ring_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't get tx ring dma address\n");
		bus_dmamem_free(rd->tx_ring_dma_tag, rd->tx_ring,
				rd->tx_ring_dmamap);
		bus_dma_tag_destroy(rd->tx_ring_dma_tag);
		rd->tx_ring_dma_tag = NULL;
		return error;
	}

	/* Create DMA tag for RX/TX mbuf map */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MCLBYTES, 1, MCLBYTES,
				   0, &bd->mbuf_dma_tag);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't create mbuf dma tag\n");
		return error;
	}

	/* Create a spare RX DMA map */
	error = bus_dmamap_create(bd->mbuf_dma_tag, 0, &bd->mbuf_tmp_dmamap);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't create tmp mbuf dma map\n");
		bus_dma_tag_destroy(bd->mbuf_dma_tag);
		bd->mbuf_dma_tag = NULL;
		return error;
	}

	/* Create DMA map for RX mbufs */
	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		error = bus_dmamap_create(bd->mbuf_dma_tag, 0,
					  &bd->rx_buf[i].rb_mbuf_dmamap);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't create rx mbuf "
				  "dma map (%d)\n", i);
			return error;
		}
		bd->rx_buf[i].rb_desc = &rd->rx_ring[i];
	}

	/* Create DMA map for TX mbufs */
	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		error = bus_dmamap_create(bd->mbuf_dma_tag, 0,
					  &bd->tx_buf[i].tb_mbuf_dmamap);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't create tx mbuf "
				  "dma map (%d)\n", i);
			return error;
		}
		bd->tx_buf[i].tb_desc1 = &rd->tx_ring[i * 2];
		bd->tx_buf[i].tb_desc2 = &rd->tx_ring[(i * 2) + 1];
	}

	return 0;
}

static void
acx_dma_free(struct acx_softc *sc)
{
	struct acx_ring_data *rd = &sc->sc_ring_data;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	int i;

	if (rd->rx_ring_dma_tag != NULL) {
		bus_dmamap_unload(rd->rx_ring_dma_tag, rd->rx_ring_dmamap);
		bus_dmamem_free(rd->rx_ring_dma_tag, rd->rx_ring,
				rd->rx_ring_dmamap);
		bus_dma_tag_destroy(rd->rx_ring_dma_tag);
	}

	if (rd->tx_ring_dma_tag != NULL) {
		bus_dmamap_unload(rd->tx_ring_dma_tag, rd->tx_ring_dmamap);
		bus_dmamem_free(rd->tx_ring_dma_tag, rd->tx_ring,
				rd->tx_ring_dmamap);
		bus_dma_tag_destroy(rd->tx_ring_dma_tag);
	}

	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (bd->rx_buf[i].rb_desc != NULL) {
			if (bd->rx_buf[i].rb_mbuf != NULL) {
				bus_dmamap_unload(bd->mbuf_dma_tag,
						  bd->rx_buf[i].rb_mbuf_dmamap);
				m_freem(bd->rx_buf[i].rb_mbuf);
			}
			bus_dmamap_destroy(bd->mbuf_dma_tag,
					   bd->rx_buf[i].rb_mbuf_dmamap);
		}
	}

	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		if (bd->tx_buf[i].tb_desc1 != NULL) {
			if (bd->tx_buf[i].tb_mbuf != NULL) {
				bus_dmamap_unload(bd->mbuf_dma_tag,
						  bd->tx_buf[i].tb_mbuf_dmamap);
				m_freem(bd->tx_buf[i].tb_mbuf);
			}
			bus_dmamap_destroy(bd->mbuf_dma_tag,
					   bd->tx_buf[i].tb_mbuf_dmamap);
		}
	}

	if (bd->mbuf_dma_tag != NULL) {
		bus_dmamap_destroy(bd->mbuf_dma_tag, bd->mbuf_tmp_dmamap);
		bus_dma_tag_destroy(bd->mbuf_dma_tag);
	}
}

static int
acx_init_tx_ring(struct acx_softc *sc)
{
	struct acx_ring_data *rd;
	struct acx_buf_data *bd;
	uint32_t paddr;
	int i;

	rd = &sc->sc_ring_data;
	paddr = rd->tx_ring_paddr;
	for (i = 0; i < (ACX_TX_DESC_CNT * 2) - 1; ++i) {
		paddr += sizeof(struct acx_host_desc);

		rd->tx_ring[i].h_ctrl = htole16(DESC_CTRL_HOSTOWN);

		if (i == (ACX_TX_DESC_CNT * 2) - 1)
			rd->tx_ring[i].h_next_desc = htole32(rd->tx_ring_paddr);
		else
			rd->tx_ring[i].h_next_desc = htole32(paddr);
	}

	bus_dmamap_sync(rd->tx_ring_dma_tag, rd->tx_ring_dmamap,
			BUS_DMASYNC_PREWRITE);

	bd = &sc->sc_buf_data;
	bd->tx_free_start = 0;
	bd->tx_used_start = 0;
	bd->tx_used_count = 0;

	return 0;
}

static int
acx_init_rx_ring(struct acx_softc *sc)
{
	struct acx_ring_data *rd;
	struct acx_buf_data *bd;
	uint32_t paddr;
	int i;

	bd = &sc->sc_buf_data;
	rd = &sc->sc_ring_data;
	paddr = rd->rx_ring_paddr;

	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		int error;

		paddr += sizeof(struct acx_host_desc);

		error = acx_newbuf(sc, &bd->rx_buf[i], 1);
		if (error)
			return error;

		if (i == ACX_RX_DESC_CNT - 1)
			rd->rx_ring[i].h_next_desc = htole32(rd->rx_ring_paddr);
		else
			rd->rx_ring[i].h_next_desc = htole32(paddr);
	}

	bus_dmamap_sync(rd->rx_ring_dma_tag, rd->rx_ring_dmamap,
			BUS_DMASYNC_PREWRITE);

	bd->rx_scan_start = 0;
	return 0;
}

static void
acx_buf_dma_addr(void *arg, bus_dma_segment_t *seg, int nseg,
		 bus_size_t mapsz, int error)
{
	if (error)
		return;

	/* XXX */
	KASSERT(nseg == 1, ("too many RX dma segments"));
	*((uint32_t *)arg) = seg->ds_addr;
}

static int
acx_newbuf(struct acx_softc *sc, struct acx_rxbuf *rb, int wait)
{
	struct acx_buf_data *bd;
	struct mbuf *m;
	bus_dmamap_t map;
	uint32_t paddr;
	int error;

	bd = &sc->sc_buf_data;

	m = m_getcl(wait ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return ENOBUFS;

	m->m_len = m->m_pkthdr.len = MCLBYTES;

	error = bus_dmamap_load_mbuf(bd->mbuf_dma_tag, bd->mbuf_tmp_dmamap,
				     m, acx_buf_dma_addr, &paddr,
				     wait ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if_printf(&sc->sc_ic.ic_if, "can't map rx mbuf %d\n", error);
		return error;
	}

	/* Unload originally mapped mbuf */
	bus_dmamap_unload(bd->mbuf_dma_tag, rb->rb_mbuf_dmamap);

	/* Swap this dmamap with tmp dmamap */
	map = rb->rb_mbuf_dmamap;
	rb->rb_mbuf_dmamap = bd->mbuf_tmp_dmamap;
	bd->mbuf_tmp_dmamap = map;

	rb->rb_mbuf = m;
	rb->rb_desc->h_data_paddr = htole32(paddr);
	rb->rb_desc->h_data_len = htole16(m->m_len);

	bus_dmamap_sync(bd->mbuf_dma_tag, rb->rb_mbuf_dmamap,
			BUS_DMASYNC_PREREAD);
	return 0;
}

static int
acx_encap(struct acx_softc *sc, struct acx_txbuf *txbuf, struct mbuf *m,
	  struct ieee80211_node *ni)
{
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	uint32_t paddr;
	uint8_t ctrl, rate;
	int error;

	KASSERT(txbuf->tb_mbuf == NULL, ("free TX buf has mbuf installed"));

	if (m->m_pkthdr.len > MCLBYTES) {
		if_printf(&sc->sc_ic.ic_if, "mbuf too big\n");
		error = E2BIG;
		goto back;
	} else if (m->m_pkthdr.len < ACX_FRAME_HDRLEN) {
		if_printf(&sc->sc_ic.ic_if, "mbuf too small\n");
		error = EINVAL;
		goto back;
	}

	error = bus_dmamap_load_mbuf(bd->mbuf_dma_tag, txbuf->tb_mbuf_dmamap,
				     m, acx_buf_dma_addr, &paddr,
				     BUS_DMA_NOWAIT);
	if (error && error != EFBIG) {
		if_printf(&sc->sc_ic.ic_if, "can't map tx mbuf1 %d\n", error);
		goto back;
	}

	if (error) {	/* error == EFBIG */
		struct mbuf *m_new;

		m_new = m_defrag(m, MB_DONTWAIT);
		if (m_new == NULL) {
			if_printf(&sc->sc_ic.ic_if, "can't defrag tx mbuf\n");
			error = ENOBUFS;
			goto back;
		} else {
			m = m_new;
		}

		error = bus_dmamap_load_mbuf(bd->mbuf_dma_tag,
					     txbuf->tb_mbuf_dmamap, m,
					     acx_buf_dma_addr, &paddr,
					     BUS_DMA_NOWAIT);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't map tx mbuf2 %d\n",
				  error);
			goto back;
		}
	}

	error = 0;

	bus_dmamap_sync(bd->mbuf_dma_tag, txbuf->tb_mbuf_dmamap,
			BUS_DMASYNC_PREWRITE);

	txbuf->tb_mbuf = m;
	txbuf->tb_node = ni;

	/*
	 * TX buffers are accessed in following way:
	 * acx_fw_txdesc -> acx_host_desc -> buffer
	 *
	 * It is quite strange that acx also querys acx_host_desc next to
	 * the one we have assigned to acx_fw_txdesc even if first one's
	 * acx_host_desc.h_data_len == acx_fw_txdesc.f_tx_len
	 *
	 * So we allocate two acx_host_desc for one acx_fw_txdesc and
	 * assign the first acx_host_desc to acx_fw_txdesc
	 *
	 * For acx111
	 * host_desc1.h_data_len = buffer_len
	 * host_desc2.h_data_len = buffer_len - mac_header_len
	 *
	 * For acx100
	 * host_desc1.h_data_len = mac_header_len
	 * host_desc2.h_data_len = buffer_len - mac_header_len
	 */

	txbuf->tb_desc1->h_data_paddr = htole32(paddr);
	txbuf->tb_desc2->h_data_paddr = htole32(paddr + ACX_FRAME_HDRLEN);

	txbuf->tb_desc1->h_data_len =
		htole16(sc->chip_txdesc1_len ? sc->chip_txdesc1_len
					     : m->m_pkthdr.len);
	txbuf->tb_desc2->h_data_len =
		htole16(m->m_pkthdr.len - ACX_FRAME_HDRLEN);

	/*
	 * NOTE:
	 * We can't simply assign f_tx_ctrl, we will first read it back
	 * and change it bit by bit
	 */
	ctrl = FW_TXDESC_GETFIELD_1(sc, txbuf, f_tx_ctrl);
	ctrl |= sc->chip_fw_txdesc_ctrl; /* extra chip specific flags */
	ctrl &= ~(DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE);

	FW_TXDESC_SETFIELD_2(sc, txbuf, f_tx_len, m->m_pkthdr.len);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_error, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_data_nretry, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_nretry, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_ok, 0);
	rate = sc->chip_set_fw_txdesc_rate(sc, txbuf, ni, m->m_pkthdr.len);

	if (sc->sc_drvbpf != NULL) {
		struct ieee80211_frame_min *wh;

		wh = mtod(m, struct ieee80211_frame_min *);
		sc->sc_tx_th.wt_flags = 0;
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		sc->sc_tx_th.wt_rate = rate;

		bpf_ptap(sc->sc_drvbpf, m, &sc->sc_tx_th, sc->sc_tx_th_len);
	}

	txbuf->tb_desc1->h_ctrl = 0;
	txbuf->tb_desc2->h_ctrl = 0;
	bus_dmamap_sync(rd->tx_ring_dma_tag, rd->tx_ring_dmamap,
			BUS_DMASYNC_PREWRITE);

	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ctrl2, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ctrl, ctrl);

	/* Tell chip to inform us about TX completion */
	CSR_WRITE_2(sc, ACXREG_INTR_TRIG, ACXRV_TRIG_TX_FINI);
back:
	if (error)
		m_freem(m);
	return error;
}

static int
acx_set_null_tmplt(struct acx_softc *sc)
{
	struct acx_tmplt_null_data n;
	struct ieee80211_frame *f;

	bzero(&n, sizeof(n));

	f = &n.data;
	f->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA |
		     IEEE80211_FC0_SUBTYPE_NODATA;
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(f->i_addr2, IF_LLADDR(&sc->sc_ic.ic_if));
	IEEE80211_ADDR_COPY(f->i_addr3, etherbroadcastaddr);

	return _acx_set_null_data_tmplt(sc, &n, sizeof(n));
}

static int
acx_set_probe_req_tmplt(struct acx_softc *sc, const char *ssid, int ssid_len)
{
	struct acx_tmplt_probe_req req;
	struct ieee80211_frame *f;
	uint8_t *v;
	int vlen;

	bzero(&req, sizeof(req));

	f = &req.data.u_data.f;
	f->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
		     IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(f->i_addr2, IF_LLADDR(&sc->sc_ic.ic_if));
	IEEE80211_ADDR_COPY(f->i_addr3, etherbroadcastaddr);

	v = req.data.u_data.var;
	v = ieee80211_add_ssid(v, ssid, ssid_len);
	v = ieee80211_add_rates(v, &sc->sc_ic.ic_sup_rates[sc->chip_phymode]);
	v = ieee80211_add_xrates(v, &sc->sc_ic.ic_sup_rates[sc->chip_phymode]);
	vlen = v - req.data.u_data.var;

	return _acx_set_probe_req_tmplt(sc, &req,
					ACX_TMPLT_PROBE_REQ_SIZ(vlen));
}

static int
acx_set_probe_resp_tmplt(struct acx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_probe_resp resp;
	struct ieee80211_frame *f;
	struct mbuf *m;
	int len;

	m = ieee80211_probe_resp_alloc(ic, ni);
	if (m == NULL)
		return 1;
	DPRINTF((&ic->ic_if, "%s alloc probe resp size %d\n", __func__,
		 m->m_pkthdr.len));

	f = mtod(m, struct ieee80211_frame *);
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);

	bzero(&resp, sizeof(resp));
	m_copydata(m, 0, m->m_pkthdr.len, (caddr_t)&resp.data);
	len = m->m_pkthdr.len + sizeof(resp.size);
	m_freem(m);

	return _acx_set_probe_resp_tmplt(sc, &resp, len);
}

static int
acx_set_beacon_tmplt(struct acx_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_tmplt_beacon beacon;
	struct acx_tmplt_tim tim;
	struct ieee80211_beacon_offsets bo;
	struct mbuf *m;
	int beacon_tmplt_len = 0, tim_tmplt_len = 0;

	bzero(&bo, sizeof(bo));
	m = ieee80211_beacon_alloc(ic, ni, &bo);
	if (m == NULL)
		return 1;
	DPRINTF((&ic->ic_if, "%s alloc beacon size %d\n", __func__,
		 m->m_pkthdr.len));

	if (bo.bo_tim_len == 0) {
		beacon_tmplt_len = m->m_pkthdr.len;
	} else {
		beacon_tmplt_len = bo.bo_tim - mtod(m, uint8_t *);
		tim_tmplt_len = m->m_pkthdr.len - beacon_tmplt_len;
	}

	bzero(&beacon, sizeof(beacon));
	bzero(&tim, sizeof(tim));

	m_copydata(m, 0, beacon_tmplt_len, (caddr_t)&beacon.data);
	if (tim_tmplt_len != 0) {
		m_copydata(m, beacon_tmplt_len, tim_tmplt_len,
			   (caddr_t)&tim.data);
	}
	m_freem(m);

	beacon_tmplt_len += sizeof(beacon.size);
	if (_acx_set_beacon_tmplt(sc, &beacon, beacon_tmplt_len) != 0)
		return 1;

	if (tim_tmplt_len != 0) {
		tim_tmplt_len += sizeof(tim.size);
		if (_acx_set_tim_tmplt(sc, &tim, tim_tmplt_len) != 0)
			return 1;
	}
	return 0;
}

static int
acx_sysctl_msdu_lifetime(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_msdu_lifetime;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_flags & ACX_FLAG_FW_LOADED) {
		struct acx_conf_msdu_lifetime msdu_lifetime;

		msdu_lifetime.lifetime = htole32(v);
		if (acx_set_msdu_lifetime_conf(sc, &msdu_lifetime) != 0) {
			if_printf(&sc->sc_ic.ic_if,
				  "can't set MSDU lifetime\n");
			error = ENXIO;
			goto back;
		}
	}
	sc->sc_msdu_lifetime = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_sysctl_free_firmware(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v == 0)	/* Do nothing */
		goto back;

	acx_free_firmware(sc);
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		acx_init(ifp->if_softc);
	return 0;
}

static int
acx_rx_config(struct acx_softc *sc, int promisc)
{
	struct acx_conf_rxopt rx_opt;
	struct ieee80211com *ic = &sc->sc_ic;

	/*
	 * What we want to receive and how to receive
	 */

	/* Common for all operational modes */
	rx_opt.opt1 = RXOPT1_INCL_RXBUF_HDR;
	rx_opt.opt2 = RXOPT2_RECV_ASSOC_REQ |
		      RXOPT2_RECV_AUTH |
		      RXOPT2_RECV_BEACON |
		      RXOPT2_RECV_CF |
		      RXOPT2_RECV_CTRL |
		      RXOPT2_RECV_DATA |
		      RXOPT2_RECV_MGMT |
		      RXOPT2_RECV_PROBE_REQ |
		      RXOPT2_RECV_PROBE_RESP |
		      RXOPT2_RECV_OTHER;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		rx_opt.opt1 |= RXOPT1_PROMISC;
		rx_opt.opt2 |= RXOPT2_RECV_BROKEN | RXOPT2_RECV_ACK;
	} else {
		rx_opt.opt1 |= promisc ? RXOPT1_PROMISC : RXOPT1_FILT_FDEST;
	}

	if (acx_set_rxopt_conf(sc, &rx_opt) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't config RX\n");
		return ENXIO;
	}
	return 0;
}

static int
acx_set_chan(struct acx_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t flags;
	uint8_t chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (acx_enable_txchan(sc, chan) != 0) {
		if_printf(&ic->ic_if, "enable TX on channel %d failed\n", chan);
		return EIO;
	}
	if (acx_enable_rxchan(sc, chan) != 0) {
		if_printf(&ic->ic_if, "enable RX on channel %d failed\n", chan);
		return EIO;
	}

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
acx_calibrate(void *xsc)
{
	struct acx_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);
	if (sc->chip_calibrate != NULL &&
	    sc->sc_ic.ic_state == IEEE80211_S_RUN) {
		sc->chip_calibrate(sc);
		callout_reset(&sc->sc_calibrate_timer, hz * sc->sc_calib_intvl,
			      acx_calibrate, sc);
	}
	lwkt_serialize_exit(ifp->if_serializer);
}
