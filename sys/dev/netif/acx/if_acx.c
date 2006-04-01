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
 * $DragonFly: src/sys/dev/netif/acx/if_acx.c,v 1.1 2006/04/01 02:55:36 sephe Exp $
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
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>

#include <netproto/802_11/ieee80211_var.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcidevs.h>

#define ACX_DEBUG

#include "if_acxreg.h"
#include "if_acxvar.h"
#include "acxcmd.h"

#define ACX_ENABLE_TXCHAN(sc, chan)					\
do {									\
	if (acx_enable_txchan((sc), (chan)) != 0) {			\
		if_printf(&(sc)->sc_ic.ic_if,				\
			  "enable TX on channel %d failed\n", (chan));	\
	}								\
} while (0)

#define ACX_ENABLE_RXCHAN(sc, chan)					\
do {									\
	if (acx_enable_rxchan((sc), (chan)) != 0) {			\
		if_printf(&(sc)->sc_ic.ic_if,				\
			  "enable RX on channel %d failed\n", (chan));	\
	}								\
} while (0)

#define SIOCSLOADFW	_IOW('i', 137, struct ifreq)	/* load firmware */
#define SIOCGRADIO	_IOW('i', 138, struct ifreq)	/* get radio type */
#define SIOCGSTATS	_IOW('i', 139, struct ifreq)	/* get acx stats */
#define SIOCSKILLFW	_IOW('i', 140, struct ifreq)	/* free firmware */
#define SIOCGFWVER	_IOW('i', 141, struct ifreq)	/* get firmware ver */
#define SIOCGHWID	_IOW('i', 142, struct ifreq)	/* get hardware id */

static int	acx_probe(device_t);
static int	acx_attach(device_t);
static int	acx_detach(device_t);
static int	acx_shutdown(device_t);

static void	acx_init(void *);
static int	acx_stop(struct acx_softc *);
static void	acx_init_info_reg(struct acx_softc *);
static int	acx_config(struct acx_softc *);
static int	acx_read_config(struct acx_softc *, struct acx_config *);
static int	acx_write_config(struct acx_softc *, struct acx_config *);
static int	acx_set_wepkeys(struct acx_softc *);
static void	acx_begin_scan(struct acx_softc *);
static void	acx_next_scan(void *);

static void	acx_start(struct ifnet *);
static void	acx_watchdog(struct ifnet *);

static int	acx_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);

static void	acx_intr(void *);
static void	acx_disable_intr(struct acx_softc *);
static void	acx_enable_intr(struct acx_softc *);
static void	acx_txeof(struct acx_softc *);
static void	acx_txerr(struct acx_softc *, uint8_t);
static void	acx_rxeof(struct acx_softc *);

static int	acx_dma_alloc(struct acx_softc *);
static void	acx_dma_free(struct acx_softc *);
static int	acx_init_tx_ring(struct acx_softc *);
static int	acx_init_rx_ring(struct acx_softc *);
static int	acx_newbuf(struct acx_softc *, struct acx_rxbuf *, int);
static int	acx_encap(struct acx_softc *, struct acx_txbuf *,
			  struct mbuf *, struct acx_node *, int);

static int	acx_reset(struct acx_softc *);

static int	acx_set_null_tmplt(struct acx_softc *);
static int	acx_set_probe_req_tmplt(struct acx_softc *, const char *, int);
static int	acx_set_probe_resp_tmplt(struct acx_softc *, const char *, int,
					 int);
static int	acx_set_beacon_tmplt(struct acx_softc *, const char *, int,
				     int);

static int	acx_read_eeprom(struct acx_softc *, uint32_t, uint8_t *);
static int	acx_read_phyreg(struct acx_softc *, uint32_t, uint8_t *);

static int	acx_copyin_firmware(struct acx_softc *, struct ifreq *);
static void	acx_free_firmware(struct acx_softc *);
static int	acx_load_firmware(struct acx_softc *, uint32_t,
				  const uint8_t *, int);
static int	acx_load_radio_firmware(struct acx_softc *, const uint8_t *,
					uint32_t);
static int	acx_load_base_firmware(struct acx_softc *, const uint8_t *,
				       uint32_t);

static struct ieee80211_node *acx_node_alloc(struct ieee80211com *);
static void	acx_node_free(struct ieee80211com *, struct ieee80211_node *);
static void	acx_node_init(struct acx_softc *, struct acx_node *);
static void	acx_node_update(struct acx_softc *, struct acx_node *,
				uint8_t, uint8_t);
static int	acx_newstate(struct ieee80211com *, enum ieee80211_state, int);

/* XXX */
static void	acx_media_status(struct ifnet *, struct ifmediareq *);

static int	acx_sysctl_txrate_upd_intvl_min(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_txrate_upd_intvl_max(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_txrate_sample_thresh(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_long_retry_limit(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_short_retry_limit(SYSCTL_HANDLER_ARGS);
static int	acx_sysctl_msdu_lifetime(SYSCTL_HANDLER_ARGS);

const struct ieee80211_rateset	acx_rates_11b =
	{ 4, { 2, 4, 11, 22 } };
const struct ieee80211_rateset	acx_rates_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

static int	acx_chanscan_rate = 5;	/* 5/second */
int		acx_beacon_intvl = 100;	/* 100 TU */

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
	{ 0, 0 }
};

static driver_t acx_driver = {
	"acx",
	acx_methods,
	sizeof(struct acx_softc)
};

static devclass_t acx_devclass;

DRIVER_MODULE(acx, pci, acx_driver, acx_devclass, 0, 0);
DRIVER_MODULE(acx, cardbus, acx_driver, acx_devclass, 0, 0);

MODULE_DEPEND(acx, wlan, 1, 1, 1);
MODULE_DEPEND(acx, pci, 1, 1, 1);
MODULE_DEPEND(acx, cardbus, 1, 1, 1);

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

	/* Initilize channel scanning timer */
	callout_init(&sc->sc_chanscan_timer);

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
			printf("\n");
		printf("%02x ", val);
	}
	printf("\n");
#endif	/* DUMP_EEPROM */

	/* Get EEPROM version */
	error = acx_read_eeprom(sc, ACX_EE_VERSION_OFS, &sc->sc_eeprom_ver);
	if (error)
		goto fail;
	DPRINTF((&sc->sc_ic.ic_if, "EEPROM version %u\n", sc->sc_eeprom_ver));

	ifp->if_softc = sc;
	ifp->if_init = acx_init;
	ifp->if_ioctl = acx_ioctl;
	ifp->if_start = acx_start;
	ifp->if_watchdog = acx_watchdog;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/* Set channels */
	for (i = 1; i <= 14; ++i) {
		ic->ic_channels[i].ic_freq =
			ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags = sc->chip_chan_flags;
	}

	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	ic->ic_caps = IEEE80211_C_WEP |		/* WEP */
		      IEEE80211_C_IBSS |	/* IBSS modes */
		      IEEE80211_C_SHPREAMBLE;	/* Short preamble */

	/* Get station id */
	for (i = 0; i < IEEE80211_ADDR_LEN; ++i) {
		error = acx_read_eeprom(sc, sc->chip_ee_eaddr_ofs - i,
					&ic->ic_myaddr[i]);
	}

	ieee80211_ifattach(ifp);

	/* Override alloc/free */
	ic->ic_node_alloc = acx_node_alloc;
	ic->ic_node_free = acx_node_free;

	/* Override newstate */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = acx_newstate;

	ieee80211_media_init(ifp, ieee80211_media_change, acx_media_status);

	sc->sc_txrate_upd_intvl_min = 10;	/* 10 seconds */
	sc->sc_txrate_upd_intvl_max = 300;	/* 5 minutes */
	sc->sc_txrate_sample_thresh = 30;	/* 30 packets */
	sc->sc_long_retry_limit = 4;
	sc->sc_short_retry_limit = 7;
	sc->sc_msdu_lifetime = 4096;

	sysctl_ctx_init(&sc->sc_sysctl_ctx);
	sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
					     SYSCTL_STATIC_CHILDREN(_hw),
					     OID_AUTO,
					     device_get_nameunit(dev),
					     CTLFLAG_RD, 0, "");
	if (sc->sc_sysctl_tree == NULL) {
		device_printf(dev, "can't add sysctl node\n");
		error = ENXIO;
		goto fail1;
	}

	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "txrate_upd_intvl_min",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_txrate_upd_intvl_min, "I",
			"min seconds to wait before raising TX rate");
	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "txrate_upd_intvl_max",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_txrate_upd_intvl_max, "I",
			"max seconds to wait before raising TX rate");
	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "txrate_sample_threshold",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_txrate_sample_thresh, "I",
			"number of packets to be sampled "
			"before raising TX rate");

	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "long_retry_limit",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_long_retry_limit, "I",
			"max number of retries for RTS packets");
	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "short_retry_limit",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_short_retry_limit, "I",
			"max number of retries for non-RTS packets");

	SYSCTL_ADD_PROC(&sc->sc_sysctl_ctx,
			SYSCTL_CHILDREN(sc->sc_sysctl_tree),
			OID_AUTO, "msdu_lifetime",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, acx_sysctl_msdu_lifetime, "I",
			"MSDU life time");

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, acx_intr, sc,
			       &sc->sc_irq_handle, ifp->if_serializer);
	if (error) {
		device_printf(dev, "can't set up interrupt\n");
		goto fail1;
	}

	return 0;
fail1:
	ieee80211_ifdetach(ifp);
fail:
	acx_detach(dev);
	return error;
}

static int
acx_detach(device_t dev)
{
	struct acx_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->sc_ic.ic_if;

		lwkt_serialize_enter(ifp->if_serializer);

		acx_stop(sc);
		acx_free_firmware(sc);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);

		lwkt_serialize_exit(ifp->if_serializer);

		ieee80211_ifdetach(ifp);
	}

	if (sc->sc_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sc_sysctl_ctx);

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
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct acx_firmware *fw = &sc->sc_firmware;
	int error;

	error = acx_stop(sc);
	if (error)
		return;

	if (fw->base_fw == NULL) {
		error = EINVAL;
		if_printf(ifp, "base firmware is not loaded yet\n");
		return;
	}

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

	/* Setup WEP */
	if (sc->sc_ic.ic_flags & IEEE80211_WEP_ON) {
		error = acx_set_wepkeys(sc);
		if (error)
			goto back;
	}

	/* Turn on power led */
	CSR_CLRB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	acx_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	/* Begin background scanning */
	acx_begin_scan(sc);
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
acx_set_wepkeys(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_conf_wep_txkey wep_txkey;
	int i, error;

	for (i = 0; i < IEEE80211_WEP_NKID; ++i) {
		struct ieee80211_wepkey *wk = &ic->ic_nw_keys[i];

		if (wk->wk_len == 0)
			continue;

		error = sc->chip_set_wepkey(sc, wk, i);
		if (error)
			return error;
	}

	/* Set current WEP key index */
	wep_txkey.wep_txkey = ic->ic_wep_txkey;
	if (acx_set_wep_txkey_conf(sc, &wep_txkey) != 0) {
		if_printf(&ic->ic_if, "set WEP txkey failed\n");
		return ENXIO;
	}
	return 0;
}

static void
acx_begin_scan(struct acx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t chan;

	ieee80211_begin_scan(&ic->ic_if);

	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

	ACX_ENABLE_TXCHAN(sc, chan);
	ACX_ENABLE_RXCHAN(sc, chan);

	/* Start background scanning */
	callout_reset(&sc->sc_chanscan_timer, hz / acx_chanscan_rate,
		      acx_next_scan, sc);
}

static void
acx_next_scan(void *arg)
{
	struct acx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ic->ic_state == IEEE80211_S_SCAN) {
		uint8_t chan;

		ieee80211_next_scan(ifp);

		chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

		ACX_ENABLE_TXCHAN(sc, chan);
		ACX_ENABLE_RXCHAN(sc, chan);

		callout_reset(&sc->sc_chanscan_timer, hz / acx_chanscan_rate,
			      acx_next_scan, sc);
	}

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
	callout_stop(&sc->sc_chanscan_timer);

	/* Turn off power led */
	CSR_SETB_2(sc, ACXREG_GPIO_OUT, sc->chip_gpio_pled);

	/* Free TX mbuf */
	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		struct acx_txbuf *buf;
		struct ieee80211_node *ni;

		buf = &bd->tx_buf[i];

		if (buf->tb_mbuf != NULL) {
			bus_dmamap_unload(bd->mbuf_dma_tag,
					  buf->tb_mbuf_dmamap);
			m_free(buf->tb_mbuf);
			buf->tb_mbuf = NULL;
		}

		ni = (struct ieee80211_node *)buf->tb_node;
		if (ni != NULL && ni != ic->ic_bss)
			ieee80211_free_node(ic, ni);
		buf->tb_node = NULL;
	}

	/* Clear TX host descriptors */
	bzero(rd->tx_ring, ACX_TX_RING_SIZE);

	/* Free RX mbuf */
	for (i = 0; i < ACX_RX_DESC_CNT; ++i) {
		if (bd->rx_buf[i].rb_mbuf != NULL) {
			bus_dmamap_unload(bd->mbuf_dma_tag,
					  bd->rx_buf[i].rb_mbuf_dmamap);
			m_free(bd->rx_buf[i].rb_mbuf);
			bd->rx_buf[i].rb_mbuf = NULL;
		}
	}

	/* Clear RX host descriptors */
	bzero(rd->rx_ring, ACX_RX_RING_SIZE);

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);

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
	if_printf(&sc->sc_ic.ic_if, "MAC address (from firmware): %6D\n",
		  conf->eaddr, ":");

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
	struct acx_conf_rxopt rx_opt;
	int error;

	/* Set number of long/short retry */
	sretry.nretry = sc->sc_short_retry_limit;
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

	/* What we want to receive and how to receive */
	/* XXX may not belong here, acx_init() */
	rx_opt.opt1 = RXOPT1_FILT_FDEST | RXOPT1_INCL_RXBUF_HDR;
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
	if (acx_set_rxopt_conf(sc, &rx_opt) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set RX option\n");
		return ENXIO;
	}
	return 0;
}

static int
acx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ifreq *req;
	int error;

	error = 0;
	req = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSLOADFW:
		error = suser(curthread);
		if (error)
			break;

		error = acx_copyin_firmware(sc, req);
		break;
	case SIOCSKILLFW:
		error = suser(curthread);
		if (error)
			break;
		acx_free_firmware(sc);
		break;
	case SIOCGRADIO:
		error = copyout(&sc->sc_radio_type, req->ifr_data,
				sizeof(sc->sc_radio_type));
		break;
	case SIOCGFWVER:
		error = copyout(&sc->sc_firmware_ver, req->ifr_data,
				sizeof(sc->sc_firmware_ver));
		break;
	case SIOCGHWID:
		error = copyout(&sc->sc_hardware_id, req->ifr_data,
				sizeof(sc->sc_hardware_id));
		break;
	case SIOCGSTATS:
		error = copyout(&sc->sc_stats, req->ifr_data,
				sizeof(sc->sc_stats));
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				acx_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				acx_stop(sc);
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* TODO */
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data, cr);
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

static __inline struct mbuf *
acx_softwep(struct ieee80211com *ic, struct mbuf *m, struct acx_node *node)
{
	m = ieee80211_wep_crypt(&ic->ic_if, m, 1);
	if (m != NULL)
		return m;

	if (node != NULL && (struct ieee80211_node *)node != ic->ic_bss)
		ieee80211_free_node(ic, (struct ieee80211_node *)node);
	return NULL;
}

static void
acx_start(struct ifnet *ifp)
{
	struct acx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_txbuf *buf;
	int trans, idx;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0 ||
	    (ifp->if_flags & IFF_RUNNING) == 0 ||
	    (ifp->if_flags & IFF_OACTIVE))
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
		struct acx_node *node;
		struct mbuf *m;
		int rate;

		node = NULL;
		if (!IF_QEMPTY(&ic->ic_mgtq)) {
			struct ieee80211_node *ni;

			IF_DEQUEUE(&ic->ic_mgtq, m);

			ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
			m->m_pkthdr.rcvif = NULL;

			/*
			 * Since mgmt data are transmitted at fixed rate
			 * they will not be used to do rate control.
			 */
			if (ni && ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);

			rate = 4;	/* XXX 2Mb/s for mgmt packet */
		} else if (!ifq_is_empty(&ifp->if_snd)) {
			struct ieee80211_frame *f;

			/* XXX */
#if 0
			if (ic->ic_state != IEEE80211_S_RUN) {
				if_printf(ifp, "data packet dropped due to "
					  "not RUN.  Current state %d\n",
					  ic->ic_state);
				break;
			}
#endif

			m = ifq_dequeue(&ifp->if_snd, NULL);
			if (m == NULL)
				break;

			m = ieee80211_encap(ifp, m,
					    (struct ieee80211_node **)&node);
			if (m == NULL) {
				ifp->if_oerrors++;
				continue;
			}
			f = mtod(m, struct ieee80211_frame *);

			if (ic->ic_flags & IEEE80211_F_WEPON) {
				f->i_fc[1] |= IEEE80211_FC1_WEP;
				if (sc->sc_softwep) {
					m = acx_softwep(ic, m, node);
					if (m == NULL) {
						/*
						 * axc_softwep() will free
						 * `node' for us if it fails
						 */
						ifp->if_oerrors++;
						node = NULL;
						continue;
					}
				}
			}

			if (node->nd_txrate < 0) {
				acx_node_init(sc, node);
				if (ic->ic_opmode == IEEE80211_M_IBSS) {
					/* XXX
					 * Add extra reference here,
					 * so that some node (bss_dup)
					 * will not be freed just after
					 * they are allocated, which
					 * make TX rate control impossible
					 */
					ieee80211_ref_node(
						(struct ieee80211_node *)node);
				}
			}

			rate = node->nd_rates.rs_rates[node->nd_txrate];

			BPF_MTAP(ifp, m);
		} else {
			break;
		}

		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m);

		if (acx_encap(sc, buf, m, node, rate) != 0) {
			struct ieee80211_node *ni;

			ni = (struct ieee80211_node *)node;
			if (ni != NULL && ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);

			ifp->if_oerrors++;
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
		ifp->if_flags |= IFF_OACTIVE;

	if (trans && ifp->if_timer == 0)
		ifp->if_timer = 5;
}

static void
acx_watchdog(struct ifnet *ifp)
{
	if_printf(ifp, "watchdog timeout\n");
	acx_txeof(ifp->if_softc);
	/* TODO */
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

		ctrl = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_ctrl);
		if ((ctrl & (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE)) !=
		    (DESC_CTRL_HOSTOWN | DESC_CTRL_ACXDONE))
			break;

		bus_dmamap_unload(bd->mbuf_dma_tag, buf->tb_mbuf_dmamap);
		m_free(buf->tb_mbuf);
		buf->tb_mbuf = NULL;

		error = FW_TXDESC_GETFIELD_1(sc, buf, f_tx_error);
		if (error) {
			acx_txerr(sc, error);
			ifp->if_oerrors++;
		} else {
			ifp->if_opackets++;
		}

		if (buf->tb_node != NULL) {
			struct ieee80211com *ic;
			struct ieee80211_node *ni;

			ic = &sc->sc_ic;
			ni = (struct ieee80211_node *)buf->tb_node;

			acx_node_update(sc, buf->tb_node, buf->tb_rate, error);
			if (ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);
			buf->tb_node = NULL;
		}

		FW_TXDESC_SETFIELD_1(sc, buf, f_tx_ctrl, DESC_CTRL_HOSTOWN);

		bd->tx_used_count--;

		idx = (idx + 1) % ACX_TX_DESC_CNT;
	}
	bd->tx_used_start = idx;

	ifp->if_timer = bd->tx_used_count == 0 ? 0 : 5;

	if (bd->tx_used_count != ACX_TX_DESC_CNT) {
		ifp->if_flags &= ~IFF_OACTIVE;
		acx_start(ifp);
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
		printf("error in other fragment\n");
		stats->err_oth_frag++;
		break;
#endif
	case DESC_ERR_ABORT:
		printf("aborted\n");
		stats->err_abort++;
		break;
	case DESC_ERR_PARAM:
		printf("wrong paramters in descriptor\n");
		stats->err_param++;
		break;
	case DESC_ERR_NO_WEPKEY:
		printf("WEP key missing\n");
		stats->err_no_wepkey++;
		break;
	case DESC_ERR_MSDU_TIMEOUT:
		printf("MSDU life timeout\n");
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
		printf("buffer overflow\n");
		stats->err_buf_oflow++;
		break;
	case DESC_ERR_DMA:
		printf("DMA error\n");
		stats->err_dma++;
		break;
	default:
		printf("unknown error %d\n", err);
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
		struct mbuf *m;
		uint32_t desc_status;
		uint16_t desc_ctrl;
		int len, error;

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
			ifp->if_ierrors++;
			goto next;
		}

		head = mtod(m, struct acx_rxbuf_hdr *);

		len = le16toh(head->rbh_len) & ACX_RXBUF_LEN_MASK;
		if (len >= sizeof(struct ieee80211_frame_min) &&
		    len < MCLBYTES) {
			struct ieee80211_frame *f;
			struct ieee80211_node *ni;

			m_adj(m, sizeof(struct acx_rxbuf_hdr) +
				 sc->chip_rxbuf_exhdr);
			f = mtod(m, struct ieee80211_frame *);

			if (ic->ic_opmode == IEEE80211_M_STA) {
				ni = ieee80211_ref_node(ic->ic_bss);
			} else {
				ni = ieee80211_find_node(ic, f->i_addr2);
				if (ni == NULL)
					ni = ieee80211_ref_node(ic->ic_bss);
			}

			if (f->i_fc[1] & IEEE80211_FC1_WEP) {
				/* Short circuit software WEP */
				f->i_fc[1] &= ~IEEE80211_FC1_WEP;

				/* Do chip specific RX buffer processing */
				if (sc->chip_proc_wep_rxbuf != NULL)
					sc->chip_proc_wep_rxbuf(sc, m, &len);
			}

			m->m_len = m->m_pkthdr.len = len;
			m->m_pkthdr.rcvif = &ic->ic_if;

			ieee80211_input(&ic->ic_if, m, ni, head->rbh_level,
					le32toh(head->rbh_time));

			if (ni == ic->ic_bss)
				ieee80211_unref_node(&ni);
			else
				ieee80211_free_node(ic, ni);

			ifp->if_ipackets++;
		} else {
			m_free(m);
			ifp->if_ierrors++;
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
acx_copyin_firmware(struct acx_softc *sc, struct ifreq *req)
{
	struct acx_firmware ufw, *kfw;
	uint8_t *base_fw, *radio_fw;
	int error;

	kfw = &sc->sc_firmware;
	base_fw = NULL;
	radio_fw = NULL;

	error = copyin(req->ifr_data, &ufw, sizeof(ufw));
	if (error)
		return error;

	/*
	 * For combined base firmware, there is no radio firmware.
	 * But base firmware must exist.
	 */
	if (ufw.base_fw_len <= 0 || ufw.radio_fw_len < 0)
		return EINVAL;

	base_fw = malloc(ufw.base_fw_len, M_DEVBUF, M_INTWAIT);
	error = copyin(ufw.base_fw, base_fw, ufw.base_fw_len);
	if (error)
		goto fail;

	if (ufw.radio_fw_len > 0) {
		radio_fw = malloc(ufw.radio_fw_len, M_DEVBUF, M_INTWAIT);
		error = copyin(ufw.radio_fw, radio_fw, ufw.radio_fw_len);
		if (error)
			goto fail;
	}

	kfw->base_fw_len = ufw.base_fw_len;
	if (kfw->base_fw != NULL)
		free(kfw->base_fw, M_DEVBUF);
	kfw->base_fw = base_fw;

	kfw->radio_fw_len = ufw.radio_fw_len;
	if (kfw->radio_fw != NULL)
		free(kfw->radio_fw, M_DEVBUF);
	kfw->radio_fw = radio_fw;

	return 0;
fail:
	if (base_fw != NULL)
		free(base_fw, M_DEVBUF);
	if (radio_fw != NULL)
		free(radio_fw, M_DEVBUF);
	return error;
}

static void
acx_free_firmware(struct acx_softc *sc)
{
	struct acx_firmware *fw = &sc->sc_firmware;

	if (fw->base_fw != NULL) {
		free(fw->base_fw, M_DEVBUF);
		fw->base_fw = NULL;
		fw->base_fw_len = 0;
	}
	if (fw->radio_fw != NULL) {
		free(fw->radio_fw, M_DEVBUF);
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

MALLOC_DECLARE(ACX_NODE);
MALLOC_DEFINE(ACX_NODE, "acx_node", "acx(4) wrapper for ieee80211_node");

static struct ieee80211_node *
acx_node_alloc(struct ieee80211com *ic)
{
	struct acx_node *node;

	node = malloc(sizeof(struct acx_node), ACX_NODE, M_NOWAIT | M_ZERO);
	node->nd_txrate = -1;
	return (struct ieee80211_node *)node;
}

static void
acx_node_init(struct acx_softc *sc, struct acx_node *node)
{
	struct ieee80211_rateset *nd_rset, *ic_rset, *cp_rset;
	struct ieee80211com *ic;
	int i, j, c;

	ic = &sc->sc_ic;

	nd_rset = &node->nd_node.ni_rates;
	ic_rset = &ic->ic_sup_rates[sc->chip_phymode];
	cp_rset = &node->nd_rates;
	c = 0;

#define IEEERATE(rate)	((rate) & IEEE80211_RATE_VAL)
	for (i = 0; i < nd_rset->rs_nrates; ++i) {
		uint8_t nd_rate = IEEERATE(nd_rset->rs_rates[i]);

		for (j = 0; j < ic_rset->rs_nrates; ++j) {
			if (nd_rate == IEEERATE(ic_rset->rs_rates[j])) {
				cp_rset->rs_rates[c++] = nd_rate;
				if (node->nd_txrate < 0) {
					/* XXX slow start?? */
					node->nd_txrate = 0;
					node->nd_node.ni_txrate = i;
				}
				break;
			}
		}
	}
	KASSERT(node->nd_node.ni_txrate >= 0, ("no compat rates"));
	DPRINTF((&ic->ic_if, "node rate %d\n",
		 IEEERATE(nd_rset->rs_rates[node->nd_node.ni_txrate])));
#undef IEEERATE

	cp_rset->rs_nrates = c;

	node->nd_txrate_upd_intvl = sc->sc_txrate_upd_intvl_min;
	node->nd_txrate_upd_time = time_second;
	node->nd_txrate_sample = 0;
}

static void
acx_node_update(struct acx_softc *sc, struct acx_node *node, uint8_t rate,
		uint8_t error)
{
	struct ieee80211_rateset *nd_rset, *cp_rset;
	int i, time_diff;

	nd_rset = &node->nd_node.ni_rates;
	cp_rset = &node->nd_rates;

	time_diff = time_second - node->nd_txrate_upd_time;

	if (error == DESC_ERR_MSDU_TIMEOUT ||
	    error == DESC_ERR_EXCESSIVE_RETRY) {
		uint8_t cur_rate;

		/* Reset packet sample counter */
		node->nd_txrate_sample = 0;

		if (rate > cp_rset->rs_rates[node->nd_txrate]) {
			/*
			 * This rate has already caused toubles,
			 * so don't count it in here
			 */
			return;
		}

		/* Double TX rate updating interval */
		node->nd_txrate_upd_intvl *= 2;
		if (node->nd_txrate_upd_intvl <=
		    sc->sc_txrate_upd_intvl_min) {
			node->nd_txrate_upd_intvl =
				sc->sc_txrate_upd_intvl_min;
		} else if (node->nd_txrate_upd_intvl >
			   sc->sc_txrate_upd_intvl_max) {
			node->nd_txrate_upd_intvl =
				sc->sc_txrate_upd_intvl_max;
		}

		if (node->nd_txrate == 0)
			return;

		node->nd_txrate_upd_time += time_diff;

		/* TX rate down */
		node->nd_txrate--;
		cur_rate = cp_rset->rs_rates[node->nd_txrate + 1];
		while (cp_rset->rs_rates[node->nd_txrate] > cur_rate) {
			if (node->nd_txrate - 1 > 0)
				node->nd_txrate--;
			else
				break;
		}
		DPRINTF((&sc->sc_ic.ic_if, "rate down %6D %d -> %d\n",
			 node->nd_node.ni_macaddr, ":",
			 cp_rset->rs_rates[node->nd_txrate + 1],
			 cp_rset->rs_rates[node->nd_txrate]));
	} else if (node->nd_txrate + 1 < node->nd_rates.rs_nrates) {
		uint8_t cur_rate;

		node->nd_txrate_sample++;

		if (node->nd_txrate_sample <= sc->sc_txrate_sample_thresh ||
		    time_diff <= node->nd_txrate_upd_intvl)
			return;

		/* Reset packet sample counter */
		node->nd_txrate_sample = 0;

		/* Half TX rate updating interval */
		node->nd_txrate_upd_intvl /= 2;
		if (node->nd_txrate_upd_intvl <
		    sc->sc_txrate_upd_intvl_min) {
			node->nd_txrate_upd_intvl =
				sc->sc_txrate_upd_intvl_min;
		} else if (node->nd_txrate_upd_intvl >
			   sc->sc_txrate_upd_intvl_max) {
			node->nd_txrate_upd_intvl =
				sc->sc_txrate_upd_intvl_max;
		}

		node->nd_txrate_upd_time += time_diff;

		/* TX Rate up */
		node->nd_txrate++;
		cur_rate = cp_rset->rs_rates[node->nd_txrate - 1];
		while (cp_rset->rs_rates[node->nd_txrate] < cur_rate) {
			if (node->nd_txrate + 1 < cp_rset->rs_nrates)
				node->nd_txrate++;
			else
				break;
		}
		DPRINTF((&sc->sc_ic.ic_if, "rate up %6D %d -> %d\n",
			 node->nd_node.ni_macaddr, ":",
			 cur_rate, cp_rset->rs_rates[node->nd_txrate]));
	} else {
		return;
	}

#define IEEERATE(rate)	((rate) & IEEE80211_RATE_VAL)
	/* XXX Update ieee80211_node's TX rate index */
	for (i = 0; i < nd_rset->rs_nrates; ++i) {
		if (IEEERATE(nd_rset->rs_rates[i]) ==
		    cp_rset->rs_rates[node->nd_txrate]) {
			node->nd_node.ni_txrate = i;
			break;
		}
	}
#undef IEEERATE
}

static void
acx_node_free(struct ieee80211com *ic, struct ieee80211_node *n)
{
	free(n, ACX_NODE);
}

static int
acx_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct acx_softc *sc = ic->ic_if.if_softc;
	int error = 0;

	ASSERT_SERIALIZED(ic->ic_if.if_serializer);

	switch (nstate) {
	case IEEE80211_S_AUTH:
		if (ic->ic_opmode == IEEE80211_M_STA) {
			struct ieee80211_node *ni;
#ifdef ACX_DEBUG
			int i;
#endif

			ni = ic->ic_bss;

			if (acx_join_bss(sc, ACX_MODE_STA, ni) != 0) {
				if_printf(&ic->ic_if, "join BSS failed\n");
				error = 1;
				goto back;
			}

			DPRINTF((&ic->ic_if, "join BSS\n"));
			if (ic->ic_state == IEEE80211_S_ASSOC) {
				DPRINTF((&ic->ic_if,
					 "change from assoc to run\n"));
				ic->ic_state = IEEE80211_S_RUN;
			}

#ifdef ACX_DEBUG
			if_printf(&ic->ic_if, "AP rates: ");
			for (i = 0; i < ni->ni_rates.rs_nrates; ++i)
				printf("%d ", ni->ni_rates.rs_rates[i]);
			ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
			printf(" %6D\n", ni->ni_bssid, ":");
#endif
		}
		break;
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_IBSS) {
			struct ieee80211_node *ni;
			uint8_t chan;

			ni = ic->ic_bss;
			chan = ieee80211_chan2ieee(ic, ni->ni_chan);

			error = 1;

			if (acx_enable_txchan(sc, chan) != 0) {
				if_printf(&ic->ic_if,
					  "enable TX on channel %d failed\n",
					  chan);
				goto back;
			}

			if (acx_enable_rxchan(sc, chan) != 0) {
				if_printf(&ic->ic_if,
					  "enable RX on channel %d failed\n",
					  chan);
				goto back;
			}

			if (acx_set_beacon_tmplt(sc, ni->ni_essid,
						 ni->ni_esslen, chan) != 0) {
				if_printf(&ic->ic_if,
					  "set bescon template failed\n");
				goto back;
			}

			if (acx_set_probe_resp_tmplt(sc, ni->ni_essid,
						     ni->ni_esslen,
						     chan) != 0) {
				if_printf(&ic->ic_if, "set probe response "
					  "template failed\n");
				goto back;
			}

			if (acx_join_bss(sc, ACX_MODE_ADHOC, ni) != 0) {
				if_printf(&ic->ic_if, "join IBSS failed\n");
				goto back;
			}

			DPRINTF((&ic->ic_if, "join IBSS\n"));
			error = 0;
		}
		break;
	default:
		break;
	}

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

#undef CALL_SET_TMPLT
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
				m_free(bd->rx_buf[i].rb_mbuf);
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
				m_free(bd->tx_buf[i].tb_mbuf);
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
	KASSERT(nseg == 1, ("too many RX dma segments\n"));
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
		m_free(m);
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
	  struct acx_node *node, int rate)
{
	struct acx_buf_data *bd = &sc->sc_buf_data;
	struct acx_ring_data *rd = &sc->sc_ring_data;
	uint32_t paddr;
	uint8_t ctrl;
	int error;

	KASSERT(txbuf->tb_mbuf == NULL, ("free TX buf has mbuf installed\n"));
	error = 0;

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
	txbuf->tb_node = node;
	txbuf->tb_rate = rate;

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

	FW_TXDESC_SETFIELD_4(sc, txbuf, f_tx_len, m->m_pkthdr.len);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_error, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_ack_fail, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_fail, 0);
	FW_TXDESC_SETFIELD_1(sc, txbuf, f_tx_rts_ok, 0);
	sc->chip_set_fw_txdesc_rate(sc, txbuf, rate);

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
		m_free(m);
	return error;
}

/* XXX C&P of ieee80211_add_ssid() */
static uint8_t *
my_ieee80211_add_ssid(uint8_t *frm, const uint8_t *ssid, u_int len)
{
	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = len;
	memcpy(frm, ssid, len);
	return frm + len;
}

static int
acx_set_null_tmplt(struct acx_softc *sc)
{
	struct acx_tmplt_null_data n;
	struct ieee80211_frame *f;

	bzero(&n, sizeof(n));

	f = &n.data;
	f->i_fc[0] = IEEE80211_FC0_SUBTYPE_NODATA | IEEE80211_FC0_TYPE_DATA;
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
	f->i_fc[0] = IEEE80211_FC0_SUBTYPE_PROBE_REQ | IEEE80211_FC0_TYPE_MGT;
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(f->i_addr2, IF_LLADDR(&sc->sc_ic.ic_if));
	IEEE80211_ADDR_COPY(f->i_addr3, etherbroadcastaddr);

	v = req.data.u_data.var;
	v = my_ieee80211_add_ssid(v, ssid, ssid_len);
	v = ieee80211_add_rates(v, &sc->sc_ic.ic_sup_rates[sc->chip_phymode]);
	v = ieee80211_add_xrates(v, &sc->sc_ic.ic_sup_rates[sc->chip_phymode]);
	vlen = v - req.data.u_data.var;

	return _acx_set_probe_req_tmplt(sc, &req,
					ACX_TMPLT_PROBE_REQ_SIZ(vlen));
}

static int
acx_set_probe_resp_tmplt(struct acx_softc *sc, const char *ssid, int ssid_len,
			 int chan)
{
	struct acx_tmplt_probe_resp resp;
	struct ieee80211_frame *f;
	struct ieee80211com *ic;
	uint8_t *v;
	int vlen;

	ic = &sc->sc_ic;

	bzero(&resp, sizeof(resp));

	f = &resp.data.u_data.f;
	f->i_fc[0] = IEEE80211_FC0_SUBTYPE_PROBE_RESP | IEEE80211_FC0_TYPE_MGT;
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(f->i_addr2, IF_LLADDR(&ic->ic_if));
	IEEE80211_ADDR_COPY(f->i_addr3, IF_LLADDR(&ic->ic_if));

	resp.data.u_data.beacon_intvl = htole16(acx_beacon_intvl);
	resp.data.u_data.cap = htole16(IEEE80211_CAPINFO_IBSS);

	v = resp.data.u_data.var;
	v = my_ieee80211_add_ssid(v, ssid, ssid_len);
	v = ieee80211_add_rates(v, &ic->ic_sup_rates[sc->chip_phymode]);

	*v++ = IEEE80211_ELEMID_DSPARMS;
	*v++ = 1;
	*v++ = chan;

	/* This should after IBSS or TIM, but acx always keeps them last */
	v = ieee80211_add_xrates(v, &ic->ic_sup_rates[sc->chip_phymode]);

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		*v++ = IEEE80211_ELEMID_IBSSPARMS;
		*v++ = 2;
	}

	vlen = v - resp.data.u_data.var;

	return _acx_set_probe_resp_tmplt(sc, &resp,
					 ACX_TMPLT_PROBE_RESP_SIZ(vlen));
}

/* XXX C&P of acx_set_probe_resp_tmplt() */
static int
acx_set_beacon_tmplt(struct acx_softc *sc, const char *ssid, int ssid_len,
		     int chan)
{
	struct acx_tmplt_beacon beacon;
	struct ieee80211_frame *f;
	struct ieee80211com *ic;
	uint8_t *v;
	int vlen;

	ic = &sc->sc_ic;

	bzero(&beacon, sizeof(beacon));

	f = &beacon.data.u_data.f;
	f->i_fc[0] = IEEE80211_FC0_SUBTYPE_BEACON | IEEE80211_FC0_TYPE_MGT;
	IEEE80211_ADDR_COPY(f->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(f->i_addr2, IF_LLADDR(&ic->ic_if));
	IEEE80211_ADDR_COPY(f->i_addr3, IF_LLADDR(&ic->ic_if));

	beacon.data.u_data.beacon_intvl = htole16(acx_beacon_intvl);
	beacon.data.u_data.cap = htole16(IEEE80211_CAPINFO_IBSS);

	v = beacon.data.u_data.var;
	v = my_ieee80211_add_ssid(v, ssid, ssid_len);
	v = ieee80211_add_rates(v, &ic->ic_sup_rates[sc->chip_phymode]);

	*v++ = IEEE80211_ELEMID_DSPARMS;
	*v++ = 1;
	*v++ = chan;

	/* This should after IBSS or TIM, but acx always keeps them last */
	v = ieee80211_add_xrates(v, &ic->ic_sup_rates[sc->chip_phymode]);

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		*v++ = IEEE80211_ELEMID_IBSSPARMS;
		*v++ = 2;
	}

	vlen = v - beacon.data.u_data.var;

	return _acx_set_beacon_tmplt(sc, &beacon, ACX_TMPLT_BEACON_SIZ(vlen));
}

/*
 * XXX
 * C&P of ieee80211_media_status(), only
 * imr->ifm_status |= IFM_ACTIVE; is added
 */
static void
acx_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_node *ni = NULL;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;

	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	imr->ifm_active |= IFM_AUTO;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		ni = ic->ic_bss;
		/* calculate rate subtype */
		imr->ifm_active |= ieee80211_rate2media(ic,
			ni->ni_rates.rs_rates[ni->ni_txrate], ic->ic_curmode);
		break;
	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;
	case IEEE80211_M_AHDEMO:
		/* should not come here */
		break;
	case IEEE80211_M_HOSTAP:
		imr->ifm_active |= IFM_IEEE80211_HOSTAP;
		break;
	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;
	}
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A:
		imr->ifm_active |= IFM_IEEE80211_11A;
		break;
	case IEEE80211_MODE_11B:
		imr->ifm_active |= IFM_IEEE80211_11B;
		break;
	case IEEE80211_MODE_11G:
		imr->ifm_active |= IFM_IEEE80211_11G;
		break;
	case IEEE80211_MODE_FH:
		imr->ifm_active |= IFM_IEEE80211_FH;
		break;
	case IEEE80211_MODE_TURBO:
		imr->ifm_active |= IFM_IEEE80211_11A
				|  IFM_IEEE80211_TURBO;
		break;
	}
}

static int
acx_sysctl_txrate_upd_intvl_min(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_txrate_upd_intvl_min;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0 || v > sc->sc_txrate_upd_intvl_max) {
		error = EINVAL;
		goto back;
	}

	sc->sc_txrate_upd_intvl_min = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_sysctl_txrate_upd_intvl_max(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_txrate_upd_intvl_max;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0 || v < sc->sc_txrate_upd_intvl_min) {
		error = EINVAL;
		goto back;
	}

	sc->sc_txrate_upd_intvl_max = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_sysctl_txrate_sample_thresh(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_txrate_sample_thresh;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	sc->sc_txrate_sample_thresh = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_sysctl_long_retry_limit(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_long_retry_limit;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_flags & ACX_FLAG_FW_LOADED) {
		struct acx_conf_nretry_long lretry;

		lretry.nretry = v;
		if (acx_set_nretry_long_conf(sc, &lretry) != 0) {
			if_printf(ifp, "can't set long retry limit\n");
			error = ENXIO;
			goto back;
		}
	}
	sc->sc_long_retry_limit = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
acx_sysctl_short_retry_limit(SYSCTL_HANDLER_ARGS)
{
	struct acx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_short_retry_limit;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_flags & ACX_FLAG_FW_LOADED) {
		struct acx_conf_nretry_short sretry;

		sretry.nretry = v;
		if (acx_set_nretry_short_conf(sc, &sretry) != 0) {
			if_printf(ifp, "can't set short retry limit\n");
			error = ENXIO;
			goto back;
		}
	}
	sc->sc_short_retry_limit = v;
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
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
