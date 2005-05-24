/*
 * Copyright (c) 2004, 2005
 *      Damien Bergamini <damien.bergamini@free.fr>.
 * Copyright (c) 2004, 2005
 *      Andrew Atrens <atrens@nortelnetworks.com>.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $DragonFly: src/sys/dev/netif/iwi/if_iwi.c,v 1.3 2005/05/24 20:59:01 dillon Exp $
 */

#include "opt_inet.h"

#include <sys/cdefs.h>

/*-
 * Intel(R) PRO/Wireless 2200BG/2915ABG driver
 * http://www.intel.com/network/connectivity/products/wireless/prowireless_mobile.htm
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/ucred.h>
#include <sys/thread2.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/clock.h>
#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ifq_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#ifdef IPX
#include <netproto/ipx/ipx.h>
#include <netproto/ipx/ipx_if.h>
#endif

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_ioctl.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/if_wavelan_ieee.h>

#include "if_iwireg.h"
#include "if_iwivar.h"

#ifdef IWI_DEBUG
#define DPRINTF(x)	if (sc->debug_level > 0) printf x
#define DPRINTFN(n, x)	if (sc->debug_level >= (n)) printf x

#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

MODULE_DEPEND(iwi, pci,  1, 1, 1);
MODULE_DEPEND(iwi, wlan, 1, 1, 1);

struct iwi_dump_buffer {
	u_int32_t buf[128];
};

struct iwi_ident {
	u_int16_t	vendor;
	u_int16_t	device;
	const char	*name;
};

static const struct iwi_ident iwi_ident_table[] = {
	{ 0x8086, 0x4220, "Intel(R) PRO/Wireless 2200BG MiniPCI" },
	{ 0x8086, 0x4223, "Intel(R) PRO/Wireless 2915ABG MiniPCI" },
	{ 0x8086, 0x4224, "Intel(R) PRO/Wireless 2915ABG MiniPCI" },

	{ 0, 0, NULL }
};

static const struct ieee80211_rateset iwi_rateset_11a =
	{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };

static const struct ieee80211_rateset iwi_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

static const struct ieee80211_rateset iwi_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

static int		iwi_dma_alloc(struct iwi_softc *);
static void		iwi_release(struct iwi_softc *);
static int		iwi_media_change(struct ifnet *);
static void		iwi_media_status(struct ifnet *, struct ifmediareq *);
static u_int16_t	iwi_read_prom_word(struct iwi_softc *, u_int8_t);
static int		iwi_newstate(struct ieee80211com *,
			    enum ieee80211_state, int);
static void		iwi_fix_channel(struct iwi_softc *, struct mbuf *);
static void		iwi_frame_intr(struct iwi_softc *,
			    struct iwi_rx_buf *, int, struct iwi_frame *);
static void		iwi_notification_intr(struct iwi_softc *,
			    struct iwi_notif *);
static void		iwi_rx_intr(struct iwi_softc *);
static void		iwi_tx_intr(struct iwi_softc *);
static void		iwi_intr(void *);
static void		iwi_dma_map_buf(void *, bus_dma_segment_t *, int,
			    bus_size_t, int);
static void		iwi_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int		iwi_cmd(struct iwi_softc *, u_int8_t, void *, u_int8_t,
			    int);
static int		iwi_tx_start(struct ifnet *, struct mbuf *,
			    struct ieee80211_node *);
static void		iwi_start(struct ifnet *);
static void		iwi_watchdog(struct ifnet *);
static int		iwi_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *cr);
static void		iwi_stop_master(struct iwi_softc *);
static int		iwi_reset(struct iwi_softc *);
static int		iwi_load_ucode(struct iwi_softc *, void *, int);
static int		iwi_load_firmware(struct iwi_softc *, void *, int);
static int		iwi_cache_firmware(struct iwi_softc *, void *, int);
static void		iwi_free_firmware(struct iwi_softc *);
static int		iwi_config(struct iwi_softc *);
static int		iwi_scan(struct iwi_softc *);
static int		iwi_auth_and_assoc(struct iwi_softc *);
static void		iwi_init(void *);
static void		iwi_init_locked(void *);
static void		iwi_stop(void *);
static void		iwi_dump_fw_event_log(struct iwi_softc *sc);
static void		iwi_dump_fw_error_log(struct iwi_softc *sc);
static u_int8_t 	iwi_find_station(struct iwi_softc *sc, u_int8_t *mac);
static int8_t		iwi_cache_station(struct iwi_softc *sc, u_int8_t *mac);
static int		iwi_adapter_config(struct iwi_softc *sc, int is_a, int cmd_wait);

static int		iwi_sysctl_bt_coexist(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_bg_autodetect(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_cts_to_self(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_antenna_diversity(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_radio(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_dump_logs(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_neg_best_rates_first(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_disable_unicast_decryption(SYSCTL_HANDLER_ARGS);
static int		iwi_sysctl_disable_multicast_decryption(SYSCTL_HANDLER_ARGS);

static __inline u_int8_t MEM_READ_1(struct iwi_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IWI_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_1(sc, IWI_CSR_INDIRECT_DATA);
}

static __inline u_int32_t MEM_READ_4(struct iwi_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IWI_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_4(sc, IWI_CSR_INDIRECT_DATA);
}

static int iwi_probe(device_t);
static int iwi_attach(device_t);
static int iwi_detach(device_t);
static int iwi_shutdown(device_t);
static int iwi_suspend(device_t);
static int iwi_resume(device_t);

static device_method_t iwi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		iwi_probe),
	DEVMETHOD(device_attach,	iwi_attach),
	DEVMETHOD(device_detach,	iwi_detach),
	DEVMETHOD(device_shutdown,	iwi_shutdown),
	DEVMETHOD(device_suspend,	iwi_suspend),
	DEVMETHOD(device_resume,	iwi_resume),

	{ 0, 0 }
};

static driver_t iwi_driver = {
	"iwi",
	iwi_methods,
	sizeof (struct iwi_softc),
	0, /* baseclasses */
	0, /* refs */
	0  /* ops */
};

static devclass_t iwi_devclass;

DRIVER_MODULE(iwi, pci, iwi_driver, iwi_devclass, 0, 0);

static int
iwi_probe(device_t dev)
{
	const struct iwi_ident *ident;

	for (ident = iwi_ident_table; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return 0;
		}
	}
	return ENXIO;
}

static void
iwi_fw_monitor(void *arg)
{
	struct iwi_softc *sc = (struct iwi_softc *)arg;
	int error, boff;
	for ( ;; ) {
		error = tsleep(IWI_FW_WAKE_MONITOR(sc), 0, "iwifwm", 0 );
		if ( error == 0 ) {
			if ( sc->flags & IWI_FLAG_EXIT ) {
				sc->flags &= ~( IWI_FLAG_EXIT );
				break;
			} else if ( sc->flags & IWI_FLAG_RESET ) {
				device_printf(sc->sc_dev, "firmware reset\n");
				for ( boff = 1; sc->flags & IWI_FLAG_RESET ; boff++ ) {
					if ( sc->debug_level > 0 )
						iwi_dump_fw_error_log(sc);
					iwi_init_locked(sc);
					if ((sc->flags & IWI_FLAG_FW_INITED))
						sc->flags &= ~( IWI_FLAG_RESET );
					error = tsleep( IWI_FW_CMD_ACKED(sc), 0,
						       "iwirun", boff * hz );
				}
			}
		}
	}
	wakeup(IWI_FW_MON_EXIT(sc));
	kthread_exit();
}

static int
iwi_start_fw_monitor_thread( struct iwi_softc *sc )
{
       if (kthread_create(iwi_fw_monitor, sc, &sc->event_thread,
		"%s%d:fw-monitor", device_get_name(sc->sc_dev),
		device_get_unit(sc->sc_dev))) {
		device_printf (sc->sc_dev,
		   "unable to create firmware monitor thread.\n");
		return -1;
	}
	return 0;
}

/* Base Address Register */
#define IWI_PCI_BAR0	0x10

static int
iwi_attach(device_t dev)
{
	struct iwi_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	u_int16_t val;
	int error, rid, i;

	sc->sc_dev = dev;

	IWI_LOCK_INIT( &sc->sc_lock );
	IWI_LOCK_INIT( &sc->sc_intrlock );

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	}

	pci_write_config(dev, 0x41, 0, 1);

	/* enable bus-mastering */
	pci_enable_busmaster(dev);

	sc->num_stations = 0;

	/* map the register window */
	rid = IWI_PCI_BAR0;
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		goto fail;
	}

	sc->sc_st = rman_get_bustag(sc->mem);
	sc->sc_sh = rman_get_bushandle(sc->mem);

	rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    RF_SHAREABLE);
	if (sc->irq == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		goto fail;
	}

	if (iwi_reset(sc) != 0) {
		device_printf(dev, "could not reset adapter\n");
		goto fail;
	}

	if (iwi_start_fw_monitor_thread(sc) ) {
		device_printf(dev, "could not start f/w reset thread\n");
		goto fail;
	}

	if (iwi_dma_alloc(sc) != 0) {
		device_printf(dev, "could not allocate DMA resources\n");
		goto fail;
	}

	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode  = IEEE80211_M_STA;
	ic->ic_state   = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps = IEEE80211_C_IBSS | IEEE80211_C_PMGT | IEEE80211_C_WEP |
	    IEEE80211_C_TXPMGT | IEEE80211_C_SHPREAMBLE;

	/* read MAC address from EEPROM */
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 0);
	ic->ic_myaddr[0] = val >> 8;
	ic->ic_myaddr[1] = val & 0xff;
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 1);
	ic->ic_myaddr[2] = val >> 8;
	ic->ic_myaddr[3] = val & 0xff;
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 2);
	ic->ic_myaddr[4] = val >> 8;
	ic->ic_myaddr[5] = val & 0xff;

	if (pci_get_device(dev) != 0x4220) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] = iwi_rateset_11a;

		/* set supported .11a channels */
		for (i = 36; i <= 64; i += 4) {
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
	ic->ic_sup_rates[IEEE80211_MODE_11B] = iwi_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = iwi_rateset_11g;

	/* set supported .11b and .11g channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	/* default to authmode OPEN */
	sc->authmode = IEEE80211_AUTH_OPEN;

	/* IBSS channel undefined for now */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = iwi_init_locked;
	ifp->if_ioctl = iwi_ioctl;
	ifp->if_start = iwi_start;
	ifp->if_watchdog = iwi_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	ieee80211_ifattach(ifp);
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwi_newstate;
	ieee80211_media_init(ifp, iwi_media_change, iwi_media_status);

	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64, &sc->sc_drvbpf);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IWI_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IWI_TX_RADIOTAP_PRESENT);

	/*
	 * Hook our interrupt after all initialization is complete
	 */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
			       iwi_intr, sc, &sc->sc_ih, NULL);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt\n");
		goto fail;
	}

	/*
	 * Add sysctl knobs
	 * 
	 * use -1 to indicate 'default / not set'
	 */

	sc->enable_bg_autodetect         = -1;
	sc->enable_bt_coexist            = -1;
	sc->enable_cts_to_self           = -1;
	sc->antenna_diversity            = -1;
	sc->enable_neg_best_first        = -1;
	sc->disable_unicast_decryption   = -1;
	sc->disable_multicast_decryption = -1;

	sysctl_ctx_init(&sc->sysctl_ctx);
	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
				SYSCTL_STATIC_CHILDREN(_hw),
				OID_AUTO,
				device_get_nameunit(dev),
				CTLFLAG_RD,
				0, "");

	if (sc->sysctl_tree == NULL) {
		error = EIO;
		goto fail;
	}

	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
		       OID_AUTO, "debug", CTLFLAG_RW, &sc->debug_level, 0,
		      "Set driver debug level (0 = off)");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree),
			OID_AUTO, "cts_to_self", CTLTYPE_INT|CTLFLAG_RW,
			(void *)sc, 0, iwi_sysctl_cts_to_self, "I", 
			"Enable cts to self [0 = Off] [1 = On] [-1 = Auto]" );

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree),
			OID_AUTO, "antenna_diversity", CTLTYPE_INT|CTLFLAG_RW,
			(void *)sc, 0, iwi_sysctl_antenna_diversity,
			"I", "Set antenna diversity [0 = Both] "
			"[1 = Antenna A] [3 = Antenna B] [-1 = Auto]" );

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree),
			OID_AUTO, "bluetooth_coexist", CTLTYPE_INT|CTLFLAG_RW,
			(void *)sc, 0, iwi_sysctl_bt_coexist,
			"I", "Enable bluetooth coexistence heuristics "
			"[0 = Off] [1 = On] [-1 = Auto]" );

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree),
			OID_AUTO, "bg_autodetect", CTLTYPE_INT|CTLFLAG_RW,
			(void *)sc, 0, iwi_sysctl_bg_autodetect,
			"I", "Set b/g autodetect [0 = Off] [1 = On] [-1 = Auto]" );


	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "radio",
			CTLTYPE_INT | CTLFLAG_RD, sc, 0, iwi_sysctl_radio, "I",
			"Radio transmitter switch");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "stats",
			CTLTYPE_OPAQUE | CTLFLAG_RD, sc, 0, iwi_sysctl_stats, 
			"S,iwi_dump_buffer", "statistics");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree),
			OID_AUTO, "firmware_logs", CTLTYPE_INT|CTLFLAG_RW,
			(void *)sc, 0, iwi_sysctl_dump_logs, "I", "Dump firmware logs");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
			"neg_best_rates_first",
			CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			iwi_sysctl_neg_best_rates_first, "I",
			 "Negotiate highest rates first.");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
			"disable_unicast_decrypt",
			CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			iwi_sysctl_disable_unicast_decryption, "I",
			 "Disable unicast decryption.");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
			SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
			"disable_multicast_decrypt",
			CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			iwi_sysctl_disable_multicast_decryption, "I",
			 "Disable multicast decryption.");

	return 0;

fail:	iwi_detach(dev);
	return ENXIO;
}

static int
iwi_detach(device_t dev)
{
	struct iwi_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	IWI_LOCK_INFO;
	IWI_IPLLOCK_INFO;

	sc->flags |= IWI_FLAG_EXIT;
	wakeup(IWI_FW_WAKE_MONITOR(sc)); /* Stop firmware monitor. */

	(void) tsleep(IWI_FW_MON_EXIT(sc), 0, "iwiexi", 10 * hz );

	IWI_LOCK(sc);
	IWI_IPLLOCK(sc);

	iwi_stop(sc);
	iwi_free_firmware(sc);

	if ( sc->sysctl_tree ) {
		crit_enter();
		sysctl_ctx_free(&sc->sysctl_ctx);
		crit_exit();
		sc->sysctl_tree = 0;
	}

	IWI_IPLUNLOCK(sc);
	IWI_UNLOCK(sc);

	bpfdetach(ifp);

	ieee80211_ifdetach(ifp);

	iwi_release(sc);

	if (sc->irq != NULL) {
		bus_teardown_intr(dev, sc->irq, sc->sc_ih);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	}

	if (sc->mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, IWI_PCI_BAR0,
		    sc->mem);

	IWI_LOCK_DESTROY(&(sc->sc_lock));
	IWI_LOCK_DESTROY(&(sc->sc_intrlock));

	return 0;
}

static int
iwi_dma_alloc(struct iwi_softc *sc)
{
	int i, error;

	error = bus_dma_tag_create(NULL, /* parent */
				   1, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MAXBSIZE, 128,
				   BUS_SPACE_MAXSIZE_32BIT,
				   BUS_DMA_ALLOCNOW,
				   &sc->iwi_parent_tag );
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create parent tag\n");
		goto fail;
	}
	/*
	 * Allocate and map Tx ring
	 */
	error = bus_dma_tag_create(sc->iwi_parent_tag, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof (struct iwi_tx_desc) * IWI_TX_RING_SIZE, 1,
	    sizeof (struct iwi_tx_desc) * IWI_TX_RING_SIZE,
	    BUS_DMA_ALLOCNOW, &sc->tx_ring_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->tx_ring_dmat,(void **) &sc->tx_desc,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->tx_ring_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate tx ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->tx_ring_dmat, sc->tx_ring_map,
	    sc->tx_desc, sizeof (struct iwi_tx_desc) * IWI_TX_RING_SIZE,
	    iwi_dma_map_addr, &sc->tx_ring_pa, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load tx ring DMA map\n");
		goto fail;
	}

	/*
	 * Allocate and map command ring
	 */
	error = bus_dma_tag_create(sc->iwi_parent_tag, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof (struct iwi_cmd_desc) * IWI_CMD_RING_SIZE, 1,
	    sizeof (struct iwi_cmd_desc) * IWI_CMD_RING_SIZE, 
	    BUS_DMA_ALLOCNOW,
	    &sc->cmd_ring_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create command ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->cmd_ring_dmat, (void **)&sc->cmd_desc,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->cmd_ring_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate command ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->cmd_ring_dmat, sc->cmd_ring_map,
	    sc->cmd_desc, sizeof (struct iwi_cmd_desc) * IWI_CMD_RING_SIZE,
	    iwi_dma_map_addr, &sc->cmd_ring_pa, 0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not load command ring DMA map\n");
		goto fail;
	}

	/*
	 * Allocate Tx buffers DMA maps
	 */
	error = bus_dma_tag_create(sc->iwi_parent_tag, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, IWI_MAX_NSEG, MCLBYTES,
	    BUS_DMA_ALLOCNOW, &sc->tx_buf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx buf DMA tag\n");
		goto fail;
	}

	for (i = 0; i < IWI_TX_RING_SIZE; i++) {
		error = bus_dmamap_create(sc->tx_buf_dmat, 0,
		    &sc->tx_buf[i].map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create tx buf DMA map");
			goto fail;
		}
	}

	/*
	 * Allocate and map Rx buffers
	 */
	error = bus_dma_tag_create(sc->iwi_parent_tag, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1, MCLBYTES,
	    BUS_DMA_ALLOCNOW, &sc->rx_buf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx buf DMA tag\n");
		goto fail;
	}

	for (i = 0; i < IWI_RX_RING_SIZE; i++) {

		error = bus_dmamap_create(sc->rx_buf_dmat, 0,
		    &sc->rx_buf[i].map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create rx buf DMA map");
			goto fail;
		}

		sc->rx_buf[i].m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (sc->rx_buf[i].m == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate rx mbuf\n");
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(sc->rx_buf_dmat, sc->rx_buf[i].map,
		    mtod(sc->rx_buf[i].m, void *), MCLBYTES, iwi_dma_map_addr,
		    &sc->rx_buf[i].physaddr, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not load rx buf DMA map");
			goto fail;
		}
	}

	return 0;

fail:	iwi_release(sc);
	return error;
}

static void
iwi_release(struct iwi_softc *sc)
{
	int i;

	if (sc->tx_ring_dmat != NULL) {
		if (sc->tx_desc != NULL) {
			bus_dmamap_sync(sc->tx_ring_dmat, sc->tx_ring_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->tx_ring_dmat, sc->tx_ring_map);
			bus_dmamem_free(sc->tx_ring_dmat, sc->tx_desc,
			    sc->tx_ring_map);
		}
		bus_dma_tag_destroy(sc->tx_ring_dmat);
	}

	if (sc->cmd_ring_dmat != NULL) {
		if (sc->cmd_desc != NULL) {
			bus_dmamap_sync(sc->cmd_ring_dmat, sc->cmd_ring_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->cmd_ring_dmat, sc->cmd_ring_map);
			bus_dmamem_free(sc->cmd_ring_dmat, sc->cmd_desc,
			    sc->cmd_ring_map);
		}
		bus_dma_tag_destroy(sc->cmd_ring_dmat);
	}

	if (sc->tx_buf_dmat != NULL) {
		for (i = 0; i < IWI_TX_RING_SIZE; i++) {
			if (sc->tx_buf[i].m != NULL) {
				bus_dmamap_sync(sc->tx_buf_dmat,
				    sc->tx_buf[i].map, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->tx_buf_dmat,
				    sc->tx_buf[i].map);
				m_freem(sc->tx_buf[i].m);
			}
			bus_dmamap_destroy(sc->tx_buf_dmat, sc->tx_buf[i].map);
		}
		bus_dma_tag_destroy(sc->tx_buf_dmat);
	}

	if (sc->rx_buf_dmat != NULL) {
		for (i = 0; i < IWI_RX_RING_SIZE; i++) {
			if (sc->rx_buf[i].m != NULL) {
				bus_dmamap_sync(sc->rx_buf_dmat,
				    sc->rx_buf[i].map, BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->rx_buf_dmat,
				    sc->rx_buf[i].map);
				m_freem(sc->rx_buf[i].m);
			}
			bus_dmamap_destroy(sc->rx_buf_dmat, sc->rx_buf[i].map);
		}
		bus_dma_tag_destroy(sc->rx_buf_dmat);
	}
	if ( sc->iwi_parent_tag != NULL ) {
		bus_dma_tag_destroy(sc->iwi_parent_tag);
	}
}

static int
iwi_shutdown(device_t dev)
{
	struct iwi_softc *sc = device_get_softc(dev);
	IWI_LOCK_INFO;

	IWI_LOCK(sc);

	iwi_stop(sc);

	IWI_UNLOCK(sc);

	return 0;
}

static int
iwi_suspend(device_t dev)
{
	struct iwi_softc *sc = device_get_softc(dev);

	IWI_LOCK_INFO;

	IWI_LOCK(sc);

	iwi_stop(sc);

	IWI_UNLOCK(sc);

	return 0;
}

static int
iwi_resume(device_t dev)
{
	struct iwi_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	IWI_LOCK_INFO;

	IWI_LOCK(sc);

	pci_write_config(dev, 0x41, 0, 1);

	if (ifp->if_flags & IFF_UP) {
		ifp->if_init(ifp->if_softc);
		if (ifp->if_flags & IFF_RUNNING)
			ifp->if_start(ifp);
	}

	IWI_UNLOCK(sc);

	return 0;
}

static int
iwi_media_change(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;
	int error = 0;
	IWI_LOCK_INFO;

	IWI_LOCK(sc);

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET) {
		IWI_UNLOCK(sc);
		return error;
	}
	error = 0; /* clear ENETRESET */

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING)){
		iwi_init(sc);
		error = tsleep( IWI_FW_CMD_ACKED(sc), 0, "iwirun", hz );
	}


	IWI_UNLOCK(sc);

	return error;
}

static void
iwi_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
#define N(a)	(sizeof (a) / sizeof (a[0]))
	static const struct {
		u_int32_t	val;
		int		rate;
	} rates[] = {
		{ IWI_RATE_DS1,      2 },
		{ IWI_RATE_DS2,      4 },
		{ IWI_RATE_DS5,     11 },
		{ IWI_RATE_DS11,    22 },
		{ IWI_RATE_OFDM6,   12 },
		{ IWI_RATE_OFDM9,   18 },
		{ IWI_RATE_OFDM12,  24 },
		{ IWI_RATE_OFDM18,  36 },
		{ IWI_RATE_OFDM24,  48 },
		{ IWI_RATE_OFDM36,  72 },
		{ IWI_RATE_OFDM48,  96 },
		{ IWI_RATE_OFDM54, 108 },
	};
	u_int32_t val, i;
	int rate;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	/* read current transmission rate from adapter */
	val = CSR_READ_4(sc, IWI_CSR_CURRENT_TX_RATE);

	/* convert rate to 802.11 rate */
	for (i = 0; i < N(rates) && rates[i].val != val ; i++);
	rate = (i < N(rates)) ? rates[i].rate : 0;

	imr->ifm_active |= ieee80211_rate2media(ic, rate, ic->ic_curmode);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;

	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;

	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;

	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_HOSTAP:
		/* should not get there */
		break;
	}
#undef N
}

static int
iwi_disassociate( struct iwi_softc *sc  )
{
	sc->assoc.type = 2; /* DISASSOCIATE */
	return iwi_cmd(sc, IWI_CMD_ASSOCIATE, &sc->assoc, sizeof sc->assoc, 0);
}


static int
iwi_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg __unused)
{
	struct iwi_softc *sc = ic->ic_softc;

	switch (nstate) {
	case IEEE80211_S_SCAN:
		if ( sc->flags & IWI_FLAG_ASSOCIATED ) {
			sc->flags &= ~( IWI_FLAG_ASSOCIATED );
			iwi_disassociate(sc);
			(void) tsleep( IWI_FW_DEASSOCIATED(sc), 
					0, "iwisca", hz );
			
		}
		if ( !(sc->flags & IWI_FLAG_SCANNING) &&
		     !(sc->flags & IWI_FLAG_RF_DISABLED) ) {
			iwi_scan(sc);
		}
		break;

	case IEEE80211_S_AUTH:
		if ( sc->flags & IWI_FLAG_ASSOCIATED ) {
			sc->flags &= ~( IWI_FLAG_ASSOCIATED );
			iwi_disassociate(sc);
			(void) tsleep( IWI_FW_DEASSOCIATED(sc), 0,
				       "iwiaut", hz );
			
		}
		if ( iwi_auth_and_assoc(sc) != 0 )
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		break;

	case IEEE80211_S_RUN:
		if (sc->flags & IWI_FLAG_SCAN_COMPLETE) {
			sc->flags &= ~(IWI_FLAG_SCAN_COMPLETE);
			if (ic->ic_opmode == IEEE80211_M_IBSS ||
			    ic->ic_opmode == IEEE80211_M_MONITOR ) {
				/* 
				 * In IBSS mode, following an end_scan
				 * the ieee80211 stack state machine transitions
				 * straight to 'run' state. This is out of
				 * step with the firmware which requires
				 * an association first. Flip our state from
				 * RUN back to AUTH to allow us to tell the
				 * firmware to associate.
				 */
				ieee80211_new_state(ic, IEEE80211_S_AUTH, -1);
			}
		}
		break;

	case IEEE80211_S_ASSOC:
		break;
	case IEEE80211_S_INIT:
		sc->flags &= ~( IWI_FLAG_SCANNING | IWI_FLAG_ASSOCIATED );
		break;
	}

	ic->ic_state = nstate;
	return 0;
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM.
 * DON'T PLAY WITH THIS CODE UNLESS YOU KNOW *EXACTLY* WHAT YOU'RE DOING!
 */
static u_int16_t
iwi_read_prom_word(struct iwi_softc *sc, u_int8_t addr)
{
	u_int32_t tmp;
	u_int16_t val;
	int n;

	/* Clock C once before the first command */
	IWI_EEPROM_CTL(sc, 0);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);

	/* Write start bit (1) */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D | IWI_EEPROM_C);

	/* Write READ opcode (10) */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D | IWI_EEPROM_C);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);

	/* Write address A7-A0 */
	for (n = 7; n >= 0; n--) {
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S |
		    (((addr >> n) & 1) << IWI_EEPROM_SHIFT_D));
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S |
		    (((addr >> n) & 1) << IWI_EEPROM_SHIFT_D) | IWI_EEPROM_C);
	}

	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);

	/* Read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
		tmp = MEM_READ_4(sc, IWI_MEM_EEPROM_CTL);
		val |= ((tmp & IWI_EEPROM_Q) >> IWI_EEPROM_SHIFT_Q) << n;
	}

	IWI_EEPROM_CTL(sc, 0);

	/* Clear Chip Select and clock C */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, 0);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_C);

	return be16toh(val);
}

/*
 * XXX: Hack to set the current channel to the value advertised in beacons or
 * probe responses. Only used during AP detection.
 */
static void
iwi_fix_channel(struct iwi_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	u_int8_t subtype;
	u_int8_t *frm, *efrm;

	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_MGT)
		return;

	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
	    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP)
		return;

	/*
	 * Cache station entries from beacons and probes.
	 */
	if ( iwi_find_station(sc, wh->i_addr2) == 0xff )
		iwi_cache_station(sc, wh->i_addr2);

	frm = (u_int8_t *)(wh + 1);
	efrm = mtod(m, u_int8_t *) + m->m_len;

	frm += 12;	/* skip tstamp, bintval and capinfo fields */
#if 0
	{ /* XXX - debugging code */
		u_int8_t *ptr;
		u_int32_t cnt;
		printf("Frame -->");
		for ( ptr = frm, cnt = 0 ; ptr < efrm ; ptr++, cnt++ ) {
			if ( cnt % 8 == 0 )
				printf("\n");
			printf("0x%-2.2x ", *ptr);
		}
		printf("<-- End Frame\n");
	}
#endif

	while (frm < efrm) {
		if (*frm == IEEE80211_ELEMID_DSPARMS)
#if IEEE80211_CHAN_MAX < 255
		if (frm[2] <= IEEE80211_CHAN_MAX)
#endif
			ic->ic_bss->ni_chan = &ic->ic_channels[frm[2]];

		frm += frm[1] + 2; /* advance to the next tag */
	}
}

static void
iwi_frame_intr(struct iwi_softc *sc, struct iwi_rx_buf *buf, int i,
    struct iwi_frame *frame)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	int error;

	DPRINTFN(5, ("RX!DATA!%u!%u!%u\n", le16toh(frame->len), frame->chan,
	    frame->rssi_dbm));

	if (le16toh(frame->len) < sizeof (struct ieee80211_frame_min) ||
	    le16toh(frame->len) > MCLBYTES) {
		device_printf(sc->sc_dev, "bad frame length\n");
		return;
	}

	bus_dmamap_unload(sc->rx_buf_dmat, buf->map);

	/* Finalize mbuf */
	m = buf->m;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = sizeof (struct iwi_hdr) +
	    sizeof (struct iwi_frame) + le16toh(frame->len);

	m_adj(m, sizeof (struct iwi_hdr) + sizeof (struct iwi_frame));

	wh = mtod(m, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		/*
		 * Hardware decrypts the frame itself but leaves the WEP bit
		 * set in the 802.11 header and don't remove the iv and crc
		 * fields
		 */
		wh->i_fc[1] &= ~IEEE80211_FC1_WEP;
		bcopy(wh, (char *)wh + IEEE80211_WEP_IVLEN +
		    IEEE80211_WEP_KIDLEN, sizeof (struct ieee80211_frame));
		m_adj(m, IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN);
		m_adj(m, -IEEE80211_WEP_CRCLEN);
		wh = mtod(m, struct ieee80211_frame *);
	}

	if (sc->sc_drvbpf != NULL) {
		struct iwi_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_rate = frame->rate;
		tap->wr_chan_freq =
		    htole16(ic->ic_channels[frame->chan].ic_freq);
		tap->wr_chan_flags =
		    htole16(ic->ic_channels[frame->chan].ic_flags);
		tap->wr_antsignal = frame->signal;
		tap->wr_antnoise = frame->noise;
		tap->wr_antenna = frame->antenna;

		bpf_ptap(sc->sc_drvbpf, m, tap, sc->sc_rxtap_len);
	}

	if (ic->ic_state == IEEE80211_S_SCAN)
		iwi_fix_channel(sc, m);

	if (ic->ic_opmode != IEEE80211_M_STA) {
		ni = ieee80211_find_node(ic, wh->i_addr2);
		if (ni == NULL)
			ni = ieee80211_ref_node(ic->ic_bss);
	} else
		ni = ieee80211_ref_node(ic->ic_bss);

	/* Send the frame to the upper layer */
	ieee80211_input(ifp, m, ni, IWI_RSSIDBM2RAW(frame->rssi_dbm), 0);

	if (ni == ic->ic_bss)
		ieee80211_unref_node(&ni);
	else
		ieee80211_free_node(ic, ni);

	buf->m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (buf->m == NULL) {
		device_printf(sc->sc_dev, "could not allocate rx mbuf\n");
		return;
	}

	error = bus_dmamap_load(sc->rx_buf_dmat, buf->map, mtod(buf->m, void *),
	    MCLBYTES, iwi_dma_map_addr, &buf->physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load rx buf DMA map\n");
		m_freem(buf->m);
		buf->m = NULL;
		return;
	}

	CSR_WRITE_4(sc, IWI_CSR_RX_BASE + i * 4, buf->physaddr);
}

static void
iwi_notification_intr(struct iwi_softc *sc, struct iwi_notif *notif)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_notif_scan_channel *chan;
	struct iwi_notif_scan_complete *scan;
	struct iwi_notif_authentication *auth;
	struct iwi_notif_association *assoc;

	switch (notif->type) {
	case IWI_NOTIF_TYPE_SCAN_CHANNEL:
		chan = (struct iwi_notif_scan_channel *)(notif + 1);

		DPRINTFN(2, ("Scan channel (%u)\n", chan->nchan));
		break;

	case IWI_NOTIF_TYPE_SCAN_COMPLETE:
		scan = (struct iwi_notif_scan_complete *)(notif + 1);

		DPRINTFN(2, ("Scan completed (%u, %u)\n", scan->nchan,
		    scan->status));

		sc->flags &= ~(IWI_FLAG_SCANNING);
		sc->flags |= IWI_FLAG_SCAN_COMPLETE;

		if ( sc->flags & IWI_FLAG_SCAN_ABORT ) {
			sc->flags &= ~(IWI_FLAG_SCAN_ABORT);
			wakeup(IWI_FW_SCAN_COMPLETED(sc));
		} else {
			ieee80211_end_scan(ifp);
			wakeup(IWI_FW_SCAN_COMPLETED(sc));
		}
		break;

	case IWI_NOTIF_TYPE_AUTHENTICATION:
		auth = (struct iwi_notif_authentication *)(notif + 1);

		DPRINTFN(2, ("Authentication (%u)\n", auth->state));

		switch (auth->state) {
		case IWI_AUTHENTICATED:
			ieee80211_new_state(ic, IEEE80211_S_ASSOC, -1);
			break;

		case IWI_DEAUTHENTICATED:
			ieee80211_begin_scan(ifp);/* not necessary */
			break;

		default:
			device_printf(sc->sc_dev,
			    "unknown authentication state %u\n", auth->state);
		}
		break;

	case IWI_NOTIF_TYPE_ASSOCIATION:
		assoc = (struct iwi_notif_association *)(notif + 1);

		DPRINTFN(2, ("Association (%u, %u)\n", assoc->state,
		    assoc->status));

		switch (assoc->state) {
		case IWI_ASSOCIATED:
			sc->flags |= IWI_FLAG_ASSOCIATED;
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
			break;

		case IWI_DEASSOCIATED:
			sc->flags &= ~(IWI_FLAG_ASSOCIATED);
			wakeup(IWI_FW_DEASSOCIATED(sc));
			ieee80211_begin_scan(ifp);/* probably not necessary */
			break;

		default:
			device_printf(sc->sc_dev,
			    "unknown association state %u\n", assoc->state);
		}
		break;

	case IWI_NOTIF_TYPE_CALIBRATION:
		DPRINTFN(5, ("Notification calib (%u)\n", notif->type));
		break;
	case IWI_NOTIF_TYPE_BEACON:
		DPRINTFN(5, ("Notification beacon (%u)\n", notif->type));
		break;
	case IWI_NOTIF_TYPE_NOISE:
		DPRINTFN(5, ("Notification noise (%u)\n", notif->type));
		break;

	default:
		device_printf(sc->sc_dev, "unknown notification type %u\n",
		    notif->type);
	}
}

static void
iwi_rx_intr(struct iwi_softc *sc)
{
	struct iwi_rx_buf *buf;
	struct iwi_hdr *hdr;
	u_int32_t r, i;

	r = CSR_READ_4(sc, IWI_CSR_RX_READ_INDEX);

	for (i = (sc->rx_cur + 1) % IWI_RX_RING_SIZE; i != r;
	     i = (i + 1) % IWI_RX_RING_SIZE) {

		buf = &sc->rx_buf[i];

		bus_dmamap_sync(sc->rx_buf_dmat, buf->map,
		    BUS_DMASYNC_POSTREAD);

		hdr = mtod(buf->m, struct iwi_hdr *);

		switch (hdr->type) {
		case IWI_HDR_TYPE_FRAME:
			iwi_frame_intr(sc, buf, i,
			    (struct iwi_frame *)(hdr + 1));
			break;

		case IWI_HDR_TYPE_NOTIF:
			iwi_notification_intr(sc,
			    (struct iwi_notif *)(hdr + 1));
			break;

		default:
			device_printf(sc->sc_dev, "unknown hdr type %u\n",
			    hdr->type);
		}
	}

	/* Tell the firmware what we have processed */
	sc->rx_cur = (r == 0) ? IWI_RX_RING_SIZE - 1 : r - 1;
	CSR_WRITE_4(sc, IWI_CSR_RX_WRITE_INDEX, sc->rx_cur);
}

static void
iwi_tx_intr(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_tx_buf *buf;
	u_int32_t r, i;

	r = CSR_READ_4(sc, IWI_CSR_TX1_READ_INDEX);
#if  notyet
	bus_dmamap_sync(sc->tx_ring_dmat, sc->tx_ring_map, BUS_DMASYNC_POSTWRITE);
#endif

	for (i = (sc->tx_old + 1) % IWI_TX_RING_SIZE; i != r;
	     i = (i + 1) % IWI_TX_RING_SIZE) {

		buf = &sc->tx_buf[i];

		bus_dmamap_sync(sc->tx_buf_dmat, buf->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->tx_buf_dmat, buf->map);
		m_freem(buf->m);
		buf->m = NULL;
		if (buf->ni != ic->ic_bss)
			ieee80211_free_node(ic, buf->ni);
		buf->ni = NULL;

		sc->tx_queued--;

		/* kill watchdog timer */
		sc->sc_tx_timer = 0;
	}

	/* Remember what the firmware has processed */
	sc->tx_old = (r == 0) ? IWI_TX_RING_SIZE - 1 : r - 1;

	/* Call start() since some buffer descriptors have been released */
	ifp->if_flags &= ~IFF_OACTIVE;
	(*ifp->if_start)(ifp);
}

static void
iwi_intr(void *arg)
{
	struct iwi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	u_int32_t r;
	IWI_LOCK_INFO;
	IWI_IPLLOCK_INFO;

	IWI_IPLLOCK(sc);

	if ((r = CSR_READ_4(sc, IWI_CSR_INTR)) == 0 || r == 0xffffffff) {
		IWI_IPLUNLOCK(sc);
		return;
	}

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, 0);

	DPRINTFN(8, ("INTR!0x%08x\n", r));

	sc->flags &= ~(IWI_FLAG_RF_DISABLED);

	if ( r & IWI_INTR_FATAL_ERROR ) {
		if ( !(sc->flags & (IWI_FLAG_RESET | IWI_FLAG_EXIT))) {
			sc->flags |= IWI_FLAG_RESET;
			wakeup(IWI_FW_WAKE_MONITOR(sc));
		}
	}

	if (r & IWI_INTR_PARITY_ERROR) {
			device_printf(sc->sc_dev, "fatal error\n");
			sc->sc_ic.ic_if.if_flags &= ~IFF_UP;
			IWI_LOCK(sc);
			iwi_stop(sc);
			IWI_UNLOCK(sc);
	}

	if (r & IWI_INTR_FW_INITED) {
		if (!(r & (IWI_INTR_FATAL_ERROR | IWI_INTR_PARITY_ERROR)))
			wakeup(IWI_FW_INITIALIZED(sc));
	}

	if (r & IWI_INTR_RADIO_OFF) {
		DPRINTF(("radio transmitter off\n"));
		sc->sc_ic.ic_if.if_flags &= ~IFF_UP;
		IWI_LOCK(sc);
		iwi_stop(sc);
		IWI_UNLOCK(sc);
		sc->flags |= IWI_FLAG_RF_DISABLED;
	}

	if (r & IWI_INTR_RX_TRANSFER)
		iwi_rx_intr(sc);

	if (r & IWI_INTR_CMD_TRANSFER)
		wakeup(IWI_FW_CMD_ACKED(sc));

	if (r & IWI_INTR_TX1_TRANSFER)
		iwi_tx_intr(sc);

	if (r & ~(IWI_HANDLED_INTR_MASK))
		device_printf(sc->sc_dev, 
			      "unhandled interrupt(s) INTR!0x%08x\n",
			      r & ~(IWI_HANDLED_INTR_MASK));

	/* Acknowledge interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR, r);

	/* Re-enable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, IWI_INTR_MASK);

	IWI_IPLUNLOCK(sc);

	if ((ifp->if_flags & IFF_RUNNING) && !ifq_is_empty(&ifp->if_snd))
		iwi_start(ifp);
}

struct iwi_dma_mapping {
	bus_dma_segment_t segs[IWI_MAX_NSEG];
	int nseg;
	bus_size_t mapsize;
};

static void
iwi_dma_map_buf(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t mapsize, int error)
{
	struct iwi_dma_mapping *map = arg;

	if (error != 0)
		return;

	KASSERT(nseg <= IWI_MAX_NSEG, ("too many DMA segments %d", nseg));

	bcopy(segs, map->segs, nseg * sizeof (bus_dma_segment_t));
	map->nseg = nseg;
	map->mapsize = mapsize;
}

static void
iwi_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg __unused, int error)
{
	if (error != 0) {
		printf("iwi: fatal DMA mapping error !!!\n");
		return;
	}

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
iwi_cmd(struct iwi_softc *sc, u_int8_t type, void *data, u_int8_t len,
    int async)
{
	struct iwi_cmd_desc *desc;

	DPRINTFN(2, ("TX!CMD!%u!%u\n", type, len));

	desc = &sc->cmd_desc[sc->cmd_cur];
	desc->hdr.type = IWI_HDR_TYPE_COMMAND;
	desc->hdr.flags = IWI_HDR_FLAG_IRQ;
	desc->type = type;
	desc->len = len;
	bcopy(data, desc->data, len);

	bus_dmamap_sync(sc->cmd_ring_dmat, sc->cmd_ring_map,
	    BUS_DMASYNC_PREWRITE);

	sc->cmd_cur = (sc->cmd_cur + 1) % IWI_CMD_RING_SIZE;
	CSR_WRITE_4(sc, IWI_CSR_CMD_WRITE_INDEX, sc->cmd_cur);

	return async ? 0 : tsleep( IWI_FW_CMD_ACKED(sc), 0, "iwicmd", hz);
}

static int
iwi_tx_start(struct ifnet *ifp, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct iwi_tx_buf *buf;
	struct iwi_tx_desc *desc;
	struct iwi_dma_mapping map;
	struct mbuf *mnew;
	u_int32_t id = 0;
	int error, i;
	IWI_IPLLOCK_INFO; /* XXX still need old ipl locking mech. here */
	IWI_IPLLOCK(sc);

	if (sc->sc_drvbpf != NULL) {
		struct iwi_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		bpf_ptap(sc->sc_drvbpf, m0, tap, sc->sc_txtap_len);
	}

	buf = &sc->tx_buf[sc->tx_cur];
	desc = &sc->tx_desc[sc->tx_cur];

	wh = mtod(m0, struct ieee80211_frame *);

	if ( (id = iwi_find_station( sc, wh->i_addr1 ) ) == 0xff )
		id = iwi_cache_station( sc, wh->i_addr1 );

	bzero( desc, sizeof (struct iwi_tx_desc) );
	desc->station_number = id;

	/* trim IEEE802.11 header */
	m_adj(m0, sizeof (struct ieee80211_frame));

	error = bus_dmamap_load_mbuf(sc->tx_buf_dmat, buf->map, m0,
	    iwi_dma_map_buf, &map, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		IWI_IPLUNLOCK(sc);
		return error;
	}
	if (error != 0) {
		mnew = m_defrag(m0, MB_DONTWAIT);
		if (mnew == NULL) {
			device_printf(sc->sc_dev,
			    "could not defragment mbuf\n");
			m_freem(m0);
			IWI_IPLUNLOCK(sc);
			return ENOBUFS;
		}
		m0 = mnew;

		error = bus_dmamap_load_mbuf(sc->tx_buf_dmat, buf->map, m0,
		    iwi_dma_map_buf, &map, BUS_DMA_NOWAIT);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map mbuf (error %d)\n", error);
			m_freem(m0);
			IWI_IPLUNLOCK(sc);
			return error;
		}
	}

	buf->m = m0;
	buf->ni = ni;

	desc->hdr.type = IWI_HDR_TYPE_DATA;
	desc->hdr.flags = IWI_HDR_FLAG_IRQ;
	desc->cmd = IWI_DATA_CMD_TX;
	desc->len = htole16(m0->m_pkthdr.len);
	desc->flags = 0;
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1))
			desc->flags |= IWI_DATA_FLAG_NEED_ACK;
	} else if (!IEEE80211_IS_MULTICAST(wh->i_addr3))
		desc->flags |= IWI_DATA_FLAG_NEED_ACK;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		wh->i_fc[1] |= IEEE80211_FC1_WEP;
		desc->wep_txkey = ic->ic_wep_txkey;
	} else
		desc->flags |= IWI_DATA_FLAG_NO_WEP;

	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		desc->flags |= IWI_DATA_FLAG_SHPREAMBLE;

	bcopy(wh, &desc->wh, sizeof (struct ieee80211_frame));
	desc->nseg = htole32(map.nseg);
	for (i = 0; i < map.nseg; i++) {
		desc->seg_addr[i] = htole32(map.segs[i].ds_addr);
		desc->seg_len[i]  = htole32(map.segs[i].ds_len);
	}

	bus_dmamap_sync(sc->tx_buf_dmat, buf->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->tx_ring_dmat, sc->tx_ring_map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(5, ("TX!DATA!%u!%u\n", desc->len, desc->nseg));

	/* Inform firmware about this new packet */
	sc->tx_queued++;
	sc->tx_cur = (sc->tx_cur + 1) % IWI_TX_RING_SIZE;
	CSR_WRITE_4(sc, IWI_CSR_TX1_WRITE_INDEX, sc->tx_cur);

	IWI_IPLUNLOCK(sc);
	return 0;
}

static void
iwi_start(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m0;
	struct ieee80211_node *ni;

	if (ic->ic_state != IEEE80211_S_RUN) {
		return;
	}

	for (;;) {
		m0 = ifq_poll(&ifp->if_snd);
		if (m0 == NULL)
			break;

		if (sc->tx_queued >= IWI_TX_RING_SIZE - 4) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}
 
		m0 = ifq_dequeue(&ifp->if_snd);

#if NBPFILTER > 0
		BPF_MTAP(ifp, m0);
#endif

		m0 = ieee80211_encap(ifp, m0, &ni);
		if (m0 == NULL)
			continue;

		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m0);

		if (iwi_tx_start(ifp, m0, ni) != 0) {
			if (ni != NULL && ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);
			break;
		}

		/* start watchdog timer */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}

}

static void
iwi_watchdog(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			if_printf(ifp, "device timeout\n");
			wakeup(IWI_FW_WAKE_MONITOR(sc));
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}


static int
iwi_wi_ioctl_get(struct ifnet *ifp, caddr_t data)
{
	struct wi_req		wreq;
	struct ifreq		*ifr;
	struct iwi_softc	*sc;
	int			error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = copyin(ifr->ifr_data, &wreq, sizeof(wreq));
	if (error)
		return (error);

	switch (wreq.wi_type) {
	case WI_RID_READ_APS:
		ieee80211_begin_scan(ifp);
		(void) tsleep(IWI_FW_SCAN_COMPLETED(sc), 
			PPAUSE|PCATCH, "ssidscan", hz * 2);
		ieee80211_end_scan(ifp);
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}




static int
iwi_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ifreq *ifr;
	struct ieee80211req *ireq;
	struct ifaddr *ifa;
	int error = 0;
	IWI_LOCK_INFO;

	IWI_LOCK(sc);

	switch (cmd) {
       case SIOCSIFADDR:
		/*
		 * Handle this here instead of in net80211_ioctl.c
		 * so that we can lock (IWI_LOCK) the call to
		 * iwi_init().
		 */
		ifa = (struct ifaddr *) data;
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			if ((ifp->if_flags & IFF_UP) == 0) {
				ifp->if_flags |= IFF_UP;
				ifp->if_init(ifp->if_softc);
			}
			arp_ifinit(ifp, ifa);
			break;
#endif
#ifdef IPX
#warning "IPX support has not been tested"
		/*
		 * XXX - This code is probably wrong,
		 *       but has been copied many times.
		 */
		case AF_IPX: {
			struct ipx_addr *ina = &(IA_SIPX(ifa)->sipx_addr);
			struct arpcom *ac = (struct arpcom *)ifp;

			if (ipx_nullhost(*ina))
				ina->x_host = *(union ipx_host *) ac->ac_enaddr;
			else
				bcopy((caddr_t) ina->x_host.c_host,
				      (caddr_t) ac->ac_enaddr,
				      sizeof(ac->ac_enaddr));
			/* fall thru... */
		}
#endif
		default:
			if ((ifp->if_flags & IFF_UP) == 0) {
				ifp->if_flags |= IFF_UP;
				ifp->if_init(ifp->if_softc);
			}
			break;
		}
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				iwi_init(sc);
				error = tsleep(IWI_FW_CMD_ACKED(sc), 0,
						 "iwirun", hz);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				iwi_stop(sc);
			}
		}
		break;

	case SIOCSLOADFW:
	case SIOCSLOADIBSSFW:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ifr = (struct ifreq *)data;
		error = iwi_cache_firmware(sc, ifr->ifr_data,
				(cmd == SIOCSLOADIBSSFW) ? 1 : 0);
		break;

	case SIOCSKILLFW:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ifp->if_flags &= ~IFF_UP;
		iwi_stop(sc);
		iwi_free_firmware(sc);
		break;

	case SIOCG80211:
		ireq = (struct ieee80211req *)data;
		switch (ireq->i_type) {
		case IEEE80211_IOC_AUTHMODE:
			ireq->i_val = sc->authmode;
			break;

		default:
			error = ieee80211_ioctl(ifp, cmd, data, cr);
		}
		break;

	case SIOCS80211:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ireq = (struct ieee80211req *)data;
		switch (ireq->i_type) {
		case IEEE80211_IOC_AUTHMODE:
			sc->authmode = ireq->i_val;
			break;

		default:
			error = ieee80211_ioctl(ifp, cmd, data, cr);
		}
		break;
	case SIOCGIFGENERIC:
		if (sc->flags & IWI_FLAG_FW_INITED) {
			error = iwi_wi_ioctl_get(ifp, data);
			if (! error)
				error = ieee80211_ioctl(ifp, cmd, data, cr);
		} else
			error = ENOTTY;
		if (error != ENOTTY)
			break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data, cr);
	}

	if (error == ENETRESET) {
		error = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			iwi_init(sc);
			error = tsleep(IWI_FW_CMD_ACKED(sc), 0, "iwirun", hz);
		}
	}

	IWI_UNLOCK(sc);

	return error;
}

static int
iwi_abort_scan( struct iwi_softc *sc  )
{
	sc->flags |= IWI_FLAG_SCAN_ABORT;
	return iwi_cmd(sc, IWI_CMD_SCAN_ABORT, NULL, 0, 1);
}

static void
iwi_stop_master(struct iwi_softc *sc)
{
	int ntries;

	/*
	 * If the master is busy scanning, we will occasionally 
	 * timeout waiting for it (the master) to stop. Make the
	 * 'stopping' process more robust by ceasing all scans
	 * prior to asking for the stop.
	 */
	if ( ( sc->flags & IWI_FLAG_SCANNING ) && 
	     !( sc->flags & IWI_FLAG_RF_DISABLED ) ) {
		iwi_abort_scan(sc);
		if (( sc->flags & IWI_FLAG_SCAN_ABORT ) && 
		    !( sc->flags & IWI_FLAG_RF_DISABLED )) {
			(void) tsleep(IWI_FW_SCAN_COMPLETED(sc), 0,
					 "iwiabr", hz);
		}
	}
	/* Disable interrupts */

	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, 0);

	CSR_WRITE_4(sc, IWI_CSR_RST, IWI_RST_STOP_MASTER);
	for (ntries = 0; ntries < 5; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_RST) & IWI_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 5 && sc->debug_level > 0) 
		device_printf(sc->sc_dev, "timeout waiting for master\n");

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_PRINCETON_RESET);

	sc->flags &= ~IWI_FLAG_FW_INITED;
}

static int
iwi_reset(struct iwi_softc *sc)
{
	int i, ntries;

	iwi_stop_master(sc);

	/* Move adapter to D0 state */
	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_INIT);

	/* Initialize Phase-Locked Level  (PLL) */
	CSR_WRITE_4(sc, IWI_CSR_READ_INT, IWI_READ_INT_INIT_HOST);

	/* Wait for clock stabilization */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_CTL) & IWI_CTL_CLOCK_READY)
			break;
		DELAY(200);
	}
	if (ntries == 1000) {
		return EIO;
	}

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_SW_RESET);

	DELAY(10);

	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_INIT);


	/* Clear NIC memory */
	CSR_WRITE_4(sc, IWI_CSR_AUTOINC_ADDR, 0);

	for (i = 0; i < 0xc000; i++) {
		CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, 0);
	}

	sc->num_stations = 0;
	return 0;
}

static int
iwi_load_ucode(struct iwi_softc *sc, void *uc, int size)
{
	u_int16_t *w;
	int ntries, i;

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_STOP_MASTER);
	for (ntries = 0; ntries < 5; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_RST) & IWI_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 5) {
		device_printf(sc->sc_dev, "timeout waiting for master\n");
		return EIO;
	}

	MEM_WRITE_4(sc, 0x3000e0, 0x80000000);
	DELAY(5000);
	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) &
	    ~IWI_RST_PRINCETON_RESET);
	DELAY(5000);
	MEM_WRITE_4(sc, 0x3000e0, 0);
	DELAY(1000);
	MEM_WRITE_4(sc, 0x300004, 1);
	DELAY(1000);
	MEM_WRITE_4(sc, 0x300004, 0);
	DELAY(1000);
	MEM_WRITE_1(sc, 0x200000, 0x00);
	MEM_WRITE_1(sc, 0x200000, 0x40);
	DELAY(1000);

	/* Adapter is buggy, we must set the address for each word */
	for (w = uc; size > 0; w++, size -= 2)
		MEM_WRITE_2(sc, 0x200010, *w);

	MEM_WRITE_1(sc, 0x200000, 0x00);
	MEM_WRITE_1(sc, 0x200000, 0x80);

	/* Wait until we get a response in the uc queue */
	for (ntries = 0; ntries < 100; ntries++) {
		if (MEM_READ_1(sc, 0x200000) & 1)
			break;
		DELAY(100);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "timeout waiting for ucode to initialize\n");
		return EIO;
	}

	/* Empty the uc queue or the firmware will not initialize properly */
	for (i = 0; i < 7; i++)
		MEM_READ_4(sc, 0x200004);

	MEM_WRITE_1(sc, 0x200000, 0x00);

	return 0;
}

/* macro to handle unaligned little endian data in firmware image */
#define GETLE32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
static int
iwi_load_firmware(struct iwi_softc *sc, void *fw, int size)
{
	bus_dma_tag_t dmat;
	bus_dmamap_t map;
	bus_addr_t physaddr;
	void *virtaddr;
	u_char *p, *end;
	u_int32_t sentinel, ctl, src, dst, sum, len, mlen;
	int ntries, error = 0;

	sc->flags &= ~(IWI_FLAG_FW_INITED);

	/* Allocate DMA memory for storing firmware image */
	error = bus_dma_tag_create(sc->iwi_parent_tag, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, size, 1, size, BUS_DMA_ALLOCNOW, &dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create firmware DMA tag\n");
		goto fail1;
	}

	/*
	 * We cannot map fw directly because of some hardware constraints on
	 * the mapping address.
	 */
	error = bus_dmamem_alloc(dmat, &virtaddr, BUS_DMA_WAITOK, &map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate firmware DMA memory\n");
		goto fail2;
	}

	error = bus_dmamap_load(dmat, map, virtaddr, size, iwi_dma_map_addr,
	    &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load firmware DMA map\n");
		goto fail3;
	}

	/* Copy firmware image to DMA memory */
	bcopy(fw, virtaddr, size);

	/* Make sure the adapter will get up-to-date values */
	bus_dmamap_sync(dmat, map, BUS_DMASYNC_PREWRITE);

	/* Tell the adapter where the command blocks are stored */
	MEM_WRITE_4(sc, 0x3000a0, 0x27000);

	/*
	 * Store command blocks into adapter's internal memory using register
	 * indirections. The adapter will read the firmware image through DMA
	 * using information stored in command blocks.
	 */
	src = physaddr;
	p = virtaddr;
	end = p + size;
	CSR_WRITE_4(sc, IWI_CSR_AUTOINC_ADDR, 0x27000);

	while (p < end) {
		dst = GETLE32(p); p += 4; src += 4;
		len = GETLE32(p); p += 4; src += 4;
		p += len;

		while (len > 0) {
			mlen = min(len, IWI_CB_MAXDATALEN);

			ctl = IWI_CB_DEFAULT_CTL | mlen;
			sum = ctl ^ src ^ dst;

			/* Write a command block */
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, ctl);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, src);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, dst);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, sum);

			src += mlen;
			dst += mlen;
			len -= mlen;
		}
	}

	/* Write a fictive final command block (sentinel) */
	sentinel = CSR_READ_4(sc, IWI_CSR_AUTOINC_ADDR);
	CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, 0);

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) &
	    ~(IWI_RST_MASTER_DISABLED | IWI_RST_STOP_MASTER));

	/* Tell the adapter to start processing command blocks */
	MEM_WRITE_4(sc, 0x3000a4, 0x540100);

	/* Wait until the adapter has processed all command blocks */
	for (ntries = 0; ntries < 400; ntries++) {
		if (MEM_READ_4(sc, 0x3000d0) >= sentinel)
			break;
		DELAY(100);
	}
	if (ntries == 400) {
		device_printf(sc->sc_dev,
		    "timeout processing command blocks\n");
		error = EIO;
		goto fail4;
	}


	/* We're done with command blocks processing */
	MEM_WRITE_4(sc, 0x3000a4, 0x540c00);

	/* Allow interrupts so we know when the firmware is inited */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, IWI_INTR_MASK);

	/* Tell the adapter to initialize the firmware */
	CSR_WRITE_4(sc, IWI_CSR_RST, 0);
	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_ALLOW_STANDBY);

	/* Wait at most one second for firmware initialization to complete */
	if ((error = tsleep(IWI_FW_INITIALIZED(sc), 0, "iwiini", hz)) != 0) {
		device_printf(sc->sc_dev, "timeout waiting for firmware "
		    "initialization to complete\n");
		goto fail4;
	}

fail4:  bus_dmamap_sync(dmat, map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(dmat, map);
fail3:	bus_dmamem_free(dmat, virtaddr, map);
fail2:	bus_dma_tag_destroy(dmat);
fail1:
	return error;
}

/*
 * Store firmware into kernel memory so we can download it when we need to,
 * e.g when the adapter wakes up from suspend mode.
 */
static int
iwi_cache_firmware(struct iwi_softc *sc, void *data, int is_ibss)
{
	struct iwi_firmware *kfw = &sc->fw;
	struct iwi_firmware ufw;
	int error;

	iwi_free_firmware(sc);

	/*
	 * mutex(9): no mutexes should be held across functions which access
	 * memory in userspace, such as copyin(9) [...]
	 */

	if ((error = copyin(data, &ufw, sizeof ufw)) != 0)
		goto fail1;

	kfw->boot_size  = ufw.boot_size;
	kfw->ucode_size = ufw.ucode_size;
	kfw->main_size  = ufw.main_size;

	kfw->boot = malloc(kfw->boot_size, M_DEVBUF, M_WAITOK);
	if (kfw->boot == NULL) {
		error = ENOMEM;
		goto fail1;
	}

	kfw->ucode = malloc(kfw->ucode_size, M_DEVBUF, M_WAITOK);
	if (kfw->ucode == NULL) {
		error = ENOMEM;
		goto fail2;
	}

	kfw->main = malloc(kfw->main_size, M_DEVBUF, M_WAITOK);
	if (kfw->main == NULL) {
		error = ENOMEM;
		goto fail3;
	}

	if ((error = copyin(ufw.boot, kfw->boot, kfw->boot_size)) != 0)
		goto fail4;

	if ((error = copyin(ufw.ucode, kfw->ucode, kfw->ucode_size)) != 0)
		goto fail4;

	if ((error = copyin(ufw.main, kfw->main, kfw->main_size)) != 0)
		goto fail4;

	DPRINTF(("Firmware cached: boot %u, ucode %u, main %u\n",
	    kfw->boot_size, kfw->ucode_size, kfw->main_size));


	sc->flags |= IWI_FLAG_FW_CACHED;
	sc->flags |= is_ibss ? IWI_FLAG_FW_IBSS : 0;
	return 0;

fail4:	free(kfw->boot, M_DEVBUF);
fail3:	free(kfw->ucode, M_DEVBUF);
fail2:	free(kfw->main, M_DEVBUF);
fail1:

	return error;
}

static void
iwi_free_firmware(struct iwi_softc *sc)
{
	if (!(sc->flags & IWI_FLAG_FW_CACHED))
		return;

	free(sc->fw.boot, M_DEVBUF);
	free(sc->fw.ucode, M_DEVBUF);
	free(sc->fw.main, M_DEVBUF);

	sc->flags &= ~( IWI_FLAG_FW_CACHED | IWI_FLAG_FW_IBSS );
}

static int
iwi_adapter_config(struct iwi_softc *sc, int is_a, int cmd_wait)
{
	struct iwi_configuration config;

	bzero(&config, sizeof config);
	config.enable_multicast = 1;
	config.noise_reported = 1;

	config.bg_autodetect = 
		( !(is_a) &&
		  ( sc->enable_bg_autodetect != 0 ) ) ? 1 : 0;	/* default: on  */

	config.bluetooth_coexistence =
		( sc->enable_bt_coexist != 0 ) ? 1 : 0;		/* default: on  */

	config.enable_cts_to_self =
		( sc->enable_cts_to_self > 0 ) ? 1 : 0;		/* default: off */

	if (sc->antenna_diversity > 0 ) {			/* default: BOTH */
		switch( sc->antenna_diversity ) {
			case 1: case 3:
				config.antenna_diversity = sc->antenna_diversity;
		}
	}

	config.disable_unicast_decryption =
		( sc->disable_unicast_decryption != 0 ) ? 1 : 0; /* default: on */

	config.disable_multicast_decryption =
		( sc->disable_multicast_decryption != 0 ) ? 1 : 0;/* default: on */


	if ( sc->debug_level > 0 ) {
		printf("config.bluetooth_coexistence = %d\n",
			 config.bluetooth_coexistence );
		printf("config.bg_autodetect = %d\n",
			 config.bg_autodetect );
		printf("config.enable_cts_to_self = %d\n",
			 config.enable_cts_to_self );
		printf("config.antenna_diversity = %d\n",
			 config.antenna_diversity );
		printf("config.disable_unicast_decryption = %d\n",
			 config.disable_unicast_decryption );
		printf("config.disable_multicast_decryption = %d\n",
			 config.disable_multicast_decryption );
		printf("config.neg_best_rates_first = %d\n",
			 sc->enable_neg_best_first );
	}

	return iwi_cmd(sc, IWI_CMD_SET_CONFIGURATION, &config,
		 sizeof config, cmd_wait );
}

static int
iwi_config(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_rateset rs;
	struct iwi_txpower power;
	struct ieee80211_wepkey *k;
	struct iwi_wep_key wepkey;
	u_int32_t data;
	int error, i;

	IEEE80211_ADDR_COPY(ic->ic_myaddr, IF_LLADDR(ifp));
	DPRINTF(("Setting MAC address to %6D\n", ic->ic_myaddr, ":"));
	error = iwi_cmd(sc, IWI_CMD_SET_MAC_ADDRESS, ic->ic_myaddr,
	    IEEE80211_ADDR_LEN, 0);
	if (error != 0)
		return error;

	DPRINTF(("Configuring adapter\n"));
	if ((error = iwi_adapter_config(sc, 1, 0)) != 0)
		return error;

	data = htole32(IWI_POWER_MODE_CAM);
	DPRINTF(("Setting power mode to %u\n", le32toh(data)));
	error = iwi_cmd(sc, IWI_CMD_SET_POWER_MODE, &data, sizeof data, 0);
	if (error != 0)
		return error;

	data = htole32(ic->ic_rtsthreshold);
	DPRINTF(("Setting RTS threshold to %u\n", le32toh(data)));
	error = iwi_cmd(sc, IWI_CMD_SET_RTS_THRESHOLD, &data, sizeof data, 0);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		power.mode = IWI_MODE_11B;
		power.nchan = 11;
		for (i = 0; i < 11; i++) {
			power.chan[i].chan = i + 1;
			power.chan[i].power = IWI_TXPOWER_MAX;
		}
		DPRINTF(("Setting .11b channels tx power\n"));
		error = iwi_cmd(sc, IWI_CMD_SET_TX_POWER, &power, sizeof power,
		    0);
		if (error != 0)
			return error;

		power.mode = IWI_MODE_11G;
		DPRINTF(("Setting .11g channels tx power\n"));
		error = iwi_cmd(sc, IWI_CMD_SET_TX_POWER, &power, sizeof power,
		    0);
		if (error != 0)
			return error;
	}

	rs.mode = IWI_MODE_11G;
	rs.type = IWI_RATESET_TYPE_SUPPORTED;
	rs.nrates = ic->ic_sup_rates[IEEE80211_MODE_11G].rs_nrates;
	bcopy(ic->ic_sup_rates[IEEE80211_MODE_11G].rs_rates, rs.rates,
	    rs.nrates);
	DPRINTF(("Setting .11bg supported rates (%u)\n", rs.nrates));
	error = iwi_cmd(sc, IWI_CMD_SET_RATES, &rs, sizeof rs, 0);
	if (error != 0)
		return error;

	rs.mode = IWI_MODE_11A;
	rs.type = IWI_RATESET_TYPE_SUPPORTED;
	rs.nrates = ic->ic_sup_rates[IEEE80211_MODE_11A].rs_nrates;
	bcopy(ic->ic_sup_rates[IEEE80211_MODE_11A].rs_rates, rs.rates,
	    rs.nrates);
	DPRINTF(("Setting .11a supported rates (%u)\n", rs.nrates));
	error = iwi_cmd(sc, IWI_CMD_SET_RATES, &rs, sizeof rs, 0);
	if (error != 0)
		return error;

	data = htole32(arc4random());
	DPRINTF(("Setting initialization vector to %u\n", le32toh(data)));
	error = iwi_cmd(sc, IWI_CMD_SET_IV, &data, sizeof data, 0);
	if (error != 0)
		return error;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		k = ic->ic_nw_keys;
		for (i = 0; i < IEEE80211_WEP_NKID; i++, k++) {
			wepkey.cmd = IWI_WEP_KEY_CMD_SETKEY;
			wepkey.idx = i;
			wepkey.len = k->wk_len;
			bzero(wepkey.key, sizeof wepkey.key);
			bcopy(k->wk_key, wepkey.key, k->wk_len);
			DPRINTF(("Setting wep key index %u len %u\n",
			    wepkey.idx, wepkey.len));
			error = iwi_cmd(sc, IWI_CMD_SET_WEP_KEY, &wepkey,
			    sizeof wepkey, 0);
			if (error != 0)
				return error;
		}
	}

	/* Enable adapter */
	DPRINTF(("Enabling adapter\n"));
	return iwi_cmd(sc, IWI_CMD_ENABLE, NULL, 0, 0);
}

static int
iwi_scan(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwi_scan scan;
	u_int8_t *p;
	int i, count;
	int do_5ghz_scan = 0;

	sc->scan_counter++; /* track the number of scans started */

	sc->flags |= IWI_FLAG_SCANNING;

	bzero(&scan, sizeof scan);

	/*
	 * Alternate two broadcast scans with
	 * two broadcast/direct scans.
	 */
	if ( sc->scan_counter & 2 ) {
		scan.type = IWI_SCAN_TYPE_BROADCAST_AND_DIRECT;
		scan.intval = htole16(100);
	} else {
		scan.type = IWI_SCAN_TYPE_BROADCAST;
		scan.intval = htole16(40);
	}

	p = scan.channels;

	/*
	 * If we have .11a capable adapter, and
	 * 	- we are in .11a mode, or
	 *	- we are in auto mode and this is an odd numbered scan
	 * then do a 5GHz scan, otherwise do a 2GHz scan.
	 */
	if ( ic->ic_sup_rates[IEEE80211_MODE_11A].rs_nrates > 0 ) {
		if (( ic->ic_curmode == IEEE80211_MODE_11A ) ||
		    (( ic->ic_curmode == IEEE80211_MODE_AUTO ) &&
		     ( sc->scan_counter & 1)))
			do_5ghz_scan = 1;
	}
	count = 0;
	if ( do_5ghz_scan ) {
		DPRINTF(("Scanning 5GHz band\n"));
		for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
			if (IEEE80211_IS_CHAN_5GHZ(&ic->ic_channels[i]) &&
					isset(ic->ic_chan_active, i)) {
				*++p = i;
				count++;
			}
		}
		*(p - count) = IWI_CHAN_5GHZ | count;
	} else {
		DPRINTF(("Scanning 2GHz band\n"));
		for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
			if (IEEE80211_IS_CHAN_2GHZ(&ic->ic_channels[i]) &&
					isset(ic->ic_chan_active, i)) {
				*++p = i;
				count++;
			}
		}
		*(p - count) = IWI_CHAN_2GHZ | count;
	}
	return iwi_cmd(sc, IWI_CMD_SCAN, &scan, sizeof scan, 1);
}

static int
iwi_auth_and_assoc(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni = ic->ic_bss;
	struct iwi_rateset rs;
	u_int32_t data;
	int error, x;

	if ( ( sc->flags & IWI_FLAG_FW_IBSS ) &&
	     !( ni->ni_capinfo & IEEE80211_CAPINFO_IBSS ) ) {
		return -1; /* IBSS F/W requires network ibss capability */
	}

	DPRINTF(("Configuring adapter\n"));
	if ((error = iwi_adapter_config(sc, 
		IEEE80211_IS_CHAN_5GHZ(ni->ni_chan), 1)) != 0)
			return error;

#ifdef IWI_DEBUG
	if (sc->debug_level > 0) {
		printf("Setting ESSID to ");
		ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
		printf("\n");
	}
#endif
	error = iwi_cmd(sc, IWI_CMD_SET_ESSID, ni->ni_essid, ni->ni_esslen, 1);
	if (error != 0)
		return error;

	/* the rate set has already been "negotiated" */
	rs.mode = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? IWI_MODE_11A :
	    IWI_MODE_11G;
	rs.type = IWI_RATESET_TYPE_NEGOTIATED;
	rs.nrates = ni->ni_rates.rs_nrates;
	if ( sc->enable_neg_best_first != 1 ) {
		bcopy(ni->ni_rates.rs_rates, rs.rates, rs.nrates);
	} else {
		for ( x = 0 ; x < rs.nrates; x++ ) {
			/*
			 * Present the firmware with the most favourable
			 * of the negotiated rates first. 
			 */
			rs.rates[rs.nrates-x-1] = ni->ni_rates.rs_rates[x];
		}
	}

	if ( sc->debug_level > 0 ) {
		printf("Setting negotiated rates (%u) : ", rs.nrates);
		for ( x = 0 ; x < rs.nrates; x++ ) {
			printf("%d ", rs.rates[x]);
		}
		printf("\n");
	}

	error = iwi_cmd(sc, IWI_CMD_SET_RATES, &rs, sizeof rs, 1);
	if (error != 0)
		return error;

	data = htole32(ni->ni_rssi);
	DPRINTF(("Setting sensitivity to %d\n", (int8_t)ni->ni_rssi));
	error = iwi_cmd(sc, IWI_CMD_SET_SENSITIVITY, &data, sizeof data, 1);
	if (error != 0)
		return error;

	bzero(&sc->assoc, sizeof sc->assoc);
	sc->assoc.mode = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? IWI_MODE_11A :
	    IWI_MODE_11G;
	sc->assoc.chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	if (sc->authmode == IEEE80211_AUTH_SHARED)
		sc->assoc.auth = (ic->ic_wep_txkey << 4) | IWI_AUTH_SHARED;
	bcopy(ni->ni_tstamp, sc->assoc.tstamp, 8);
	sc->assoc.capinfo = htole16(ni->ni_capinfo);
	sc->assoc.lintval = htole16(ic->ic_lintval);
	sc->assoc.intval = htole16(ni->ni_intval);
	IEEE80211_ADDR_COPY(sc->assoc.bssid, ni->ni_bssid);
	if ( ic->ic_opmode == IEEE80211_M_IBSS )
		IEEE80211_ADDR_COPY(sc->assoc.dst, ifp->if_broadcastaddr);
	else
		IEEE80211_ADDR_COPY(sc->assoc.dst, ni->ni_bssid);

	DPRINTF(("Trying to associate to %6D channel %u auth %u\n",
	    sc->assoc.bssid, ":", sc->assoc.chan, sc->assoc.auth));
	return iwi_cmd(sc, IWI_CMD_ASSOCIATE, &sc->assoc, sizeof sc->assoc, 1);
}

static void
iwi_init(void *priv)
{
	struct iwi_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_firmware *fw = &sc->fw;
	int i;

	/* exit immediately if firmware has not been ioctl'd */
	if (!(sc->flags & IWI_FLAG_FW_CACHED)) {
		ifp->if_flags &= ~IFF_UP;
		return;
	}

	iwi_stop(sc);

	if (iwi_reset(sc) != 0) {
		device_printf(sc->sc_dev, "could not reset adapter\n");
		goto fail;
	}

	if (iwi_load_firmware(sc, fw->boot, fw->boot_size) != 0) {
		device_printf(sc->sc_dev, "could not load boot firmware\n");
		goto fail;
	}

	if (iwi_load_ucode(sc, fw->ucode, fw->ucode_size) != 0) {
		device_printf(sc->sc_dev, "could not load microcode\n");
		goto fail;
	}

	iwi_stop_master(sc);

	sc->tx_cur = 0;
	sc->tx_queued = 0;
	sc->tx_old = IWI_TX_RING_SIZE - 1;
	sc->cmd_cur = 0;
	sc->rx_cur = IWI_RX_RING_SIZE - 1;

	CSR_WRITE_4(sc, IWI_CSR_CMD_BASE, sc->cmd_ring_pa);
	CSR_WRITE_4(sc, IWI_CSR_CMD_SIZE, IWI_CMD_RING_SIZE);
	CSR_WRITE_4(sc, IWI_CSR_CMD_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_CMD_WRITE_INDEX, sc->cmd_cur);

	CSR_WRITE_4(sc, IWI_CSR_TX1_BASE, sc->tx_ring_pa);
	CSR_WRITE_4(sc, IWI_CSR_TX1_SIZE, IWI_TX_RING_SIZE);
	CSR_WRITE_4(sc, IWI_CSR_TX1_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_TX1_WRITE_INDEX, sc->tx_cur);

	CSR_WRITE_4(sc, IWI_CSR_TX2_BASE, sc->tx_ring_pa);
	CSR_WRITE_4(sc, IWI_CSR_TX2_SIZE, IWI_TX_RING_SIZE);
	CSR_WRITE_4(sc, IWI_CSR_TX2_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_TX2_WRITE_INDEX, 0);

	CSR_WRITE_4(sc, IWI_CSR_TX3_BASE, sc->tx_ring_pa);
	CSR_WRITE_4(sc, IWI_CSR_TX3_SIZE, IWI_TX_RING_SIZE);
	CSR_WRITE_4(sc, IWI_CSR_TX3_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_TX3_WRITE_INDEX, 0);

	CSR_WRITE_4(sc, IWI_CSR_TX4_BASE, sc->tx_ring_pa);
	CSR_WRITE_4(sc, IWI_CSR_TX4_SIZE, IWI_TX_RING_SIZE);
	CSR_WRITE_4(sc, IWI_CSR_TX4_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_TX4_WRITE_INDEX, 0);

	for (i = 0; i < IWI_RX_RING_SIZE; i++)
		CSR_WRITE_4(sc, IWI_CSR_RX_BASE + i * 4,
		    sc->rx_buf[i].physaddr);

	/*
	 * Kick Rx
	 */
	CSR_WRITE_4(sc, IWI_CSR_RX_WRITE_INDEX, sc->rx_cur);
	CSR_WRITE_4(sc, IWI_CSR_RX_READ_INDEX, 0);

	if (iwi_load_firmware(sc, fw->main, fw->main_size) != 0) {
		device_printf(sc->sc_dev, "could not load main firmware\n");
		goto fail;
	}

	/*
	 * Force the opmode based on what firmware is loaded. This
	 * stops folks from killing the firmware by asking it to
	 * do something it doesn't support.
	 */
	if ( ic->ic_opmode != IEEE80211_M_MONITOR ) {
		ic->ic_opmode = ( sc->flags & IWI_FLAG_FW_IBSS )
			? IEEE80211_M_IBSS : IEEE80211_M_STA;
	}

	sc->flags |= IWI_FLAG_FW_INITED;

	sc->flags &= ~( IWI_FLAG_SCANNING |
			IWI_FLAG_SCAN_COMPLETE |
			IWI_FLAG_SCAN_ABORT |
			IWI_FLAG_ASSOCIATED );

	if (iwi_config(sc) != 0) {
		device_printf(sc->sc_dev, "device configuration failed\n");
		goto fail;
	}

	if ( ic->ic_opmode != IEEE80211_M_MONITOR ) {
		ieee80211_begin_scan(ifp);
		ifp->if_flags &= ~IFF_OACTIVE;
		ifp->if_flags |= IFF_RUNNING;
	} else {
		ieee80211_begin_scan(ifp);
		ifp->if_flags &= ~IFF_OACTIVE;
		ifp->if_flags |= IFF_RUNNING;
	}

	return;

fail:	
	if ( !(sc->flags & IWI_FLAG_RESET) )
		ifp->if_flags &= ~IFF_UP;
	iwi_stop(sc);
}

static void
iwi_init_locked(void *priv) 
{
	struct iwi_softc *sc = priv;
	IWI_LOCK_INFO;
	IWI_LOCK(sc);
	iwi_init(sc);
	IWI_UNLOCK(sc);
}

static void
iwi_stop(void *priv)
{
	struct iwi_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_tx_buf *buf;
	int i;

	iwi_stop_master(sc);
	CSR_WRITE_4(sc, IWI_CSR_RST, IWI_RST_SW_RESET);

	/*
	 * Release Tx buffers
	 */
	for (i = 0; i < IWI_TX_RING_SIZE; i++) {
		buf = &sc->tx_buf[i];

		if (buf->m != NULL) {
			bus_dmamap_sync(sc->tx_buf_dmat, buf->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->tx_buf_dmat, buf->map);
			m_freem(buf->m);
			buf->m = NULL;

			if (buf->ni != NULL) {
				if (buf->ni != ic->ic_bss)
					ieee80211_free_node(ic, buf->ni);
				buf->ni = NULL;
			}
		}
	}

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

static int8_t
iwi_cache_station(struct iwi_softc *sc, u_int8_t *mac)
{
	int i, x, base, elemsize = sizeof(struct iwi_fw_station);
	for (i = 0; i < sc->num_stations; i++)
		if (!memcmp(sc->stations[i], mac, IEEE80211_ADDR_LEN))
			break;
	if (i == IWI_FW_MAX_STATIONS)
		return 0xff;
	memcpy(sc->stations[i], mac, IEEE80211_ADDR_LEN);
	for (x = 0, base = IWI_STATION_TABLE + (i * elemsize) ; 
	     x < IEEE80211_ADDR_LEN ; x++ ) {
		CSR_WRITE_1(sc, base + x, mac[x]);
	}
	if ( (i + 1) > sc->num_stations )
		sc->num_stations++;
	return i;
}

static u_int8_t
iwi_find_station(struct iwi_softc *sc, u_int8_t *mac)
{
	u_int8_t i;
	for (i = 0; i < sc->num_stations; i++)
		if (!memcmp(sc->stations[i], mac, IEEE80211_ADDR_LEN))
			return i;
	return 0xff;
}

static const char *
iwi_error_desc(u_int32_t val)
{
	switch (val) {
	case IWI_FW_ERROR_OK:
		return "OK";
	case IWI_FW_ERROR_FAIL:
		return "FAIL";
	case IWI_FW_ERROR_MEMORY_UNDERFLOW:
		return "MEMORY_UNDERFLOW";
	case IWI_FW_ERROR_MEMORY_OVERFLOW:
		return "MEMORY_OVERFLOW";
	case IWI_FW_ERROR_BAD_PARAM:
		return "BAD_PARAMETER";
	case IWI_FW_ERROR_BAD_CHECKSUM:
		return "BAD_CHECKSUM";
	case IWI_FW_ERROR_NMI_INTERRUPT:
		return "NMI_INTERRUPT";
	case IWI_FW_ERROR_BAD_DATABASE:
		return "BAD_DATABASE";
	case IWI_FW_ERROR_ALLOC_FAIL:
		return "ALLOC_FAIL";
	case IWI_FW_ERROR_DMA_UNDERRUN:
		return "DMA_UNDERRUN";
	case IWI_FW_ERROR_DMA_STATUS:
		return "DMA_STATUS";
	case IWI_FW_ERROR_DINOSTATUS_ERROR:
		return "DINOSTATUS_ERROR";
	case IWI_FW_ERROR_EEPROMSTATUS_ERROR:
		return "EEPROMSTATUS_ERROR";
	case IWI_FW_ERROR_SYSASSERT:
		return "SYSASSERT";
	case IWI_FW_ERROR_FATAL_ERROR:
		return "FATAL";
	default:
		return "UNKNOWN_ERROR";
	}
}

static void
iwi_dump_fw_event_log(struct iwi_softc *sc)
{
	u_int32_t ev, time, data, i, count, base;
	base = CSR_READ_4(sc, IWI_FW_EVENT_LOG);
	count = MEM_READ_4(sc, base);
	if ( count > 0 && (sc->flags & IWI_FLAG_FW_INITED) ) {
		printf("Reading %d event log entries from base address 0x%x.\n",
			count,  base);
		if (IWI_FW_EVENT_START_OFFSET <= count * IWI_FW_EVENT_ELEM_SIZE)
			device_printf(sc->sc_dev,"Start IWI Event Log Dump:\n");
		for (i = IWI_FW_EVENT_START_OFFSET;
				i <= count * IWI_FW_EVENT_ELEM_SIZE;
				i += IWI_FW_EVENT_ELEM_SIZE) {
			ev = MEM_READ_4(sc, base + i);
			time  = MEM_READ_4(sc, base + i + 1 * sizeof(u_int32_t));
			data  = MEM_READ_4(sc, base + i + 2 * sizeof(u_int32_t));
			printf("%d %8p %8.8d\n", time, (void *) data, ev);
		}
	} else {
		printf("There are no entries in the firmware event log.\n");
	}
}

static void
iwi_dump_fw_error_log(struct iwi_softc *sc)
{
	u_int32_t i = 0;
	int32_t count, base;
	base = CSR_READ_4(sc, IWI_FW_ERROR_LOG);
	count = MEM_READ_4(sc, base);
	if ( count > 0 && (sc->flags & IWI_FLAG_FW_INITED) ) {
		printf("Reading %d error log entries "
		    "from base address 0x%p.\n", count,  (void *)base);
		for ( i = IWI_FW_ERROR_START_OFFSET; 
			i <= count * IWI_FW_EVENT_ELEM_SIZE;
			i += IWI_FW_ERROR_ELEM_SIZE ) {
			u_int32_t elems;
			printf("%15.15s",
			    iwi_error_desc(MEM_READ_4(sc, base + i)));
			printf(" time(%8.8d)", MEM_READ_4(sc, base + i + 4));
			for ( elems = 2 ; elems < 7 ; elems++ ) {
				printf(" %8p", (void *)
				    MEM_READ_4(sc, base + i + (4 * elems)));
			}
			printf("\n");
		}
	} 
}

static int
iwi_sysctl_cts_to_self(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = (void *)arg1;
	int cts_to_self = sc->enable_cts_to_self;
	int error = sysctl_handle_int(oidp, &cts_to_self, 0, req);

	(void)arg2; /* silence WARNS == 6 */

	if ( !error && req->newptr && cts_to_self != sc->enable_cts_to_self ) {
		switch ( cts_to_self ) {
			case -1: case 0: case 1:
				sc->enable_cts_to_self = cts_to_self;
				error = iwi_adapter_config(sc, 0, 0);
			break;
		}
	}
	return error;
}


static int
iwi_sysctl_antenna_diversity(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = (void *)arg1;
	int antenna_diversity = sc->antenna_diversity;
	int error = sysctl_handle_int(oidp, &antenna_diversity, 0, req);

	(void)arg2; /* silence WARNS == 6 */

	if ( !error && req->newptr && antenna_diversity != sc->antenna_diversity ) {
		switch ( antenna_diversity ) {
			case 1: case 3: case 0: case -1:
				sc->antenna_diversity = antenna_diversity;
				error = iwi_adapter_config(sc, 0, 0);
			break;
		}
	}
	return error;
}

static int
iwi_sysctl_bg_autodetect(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = (void *)arg1;
	int bg_autodetect = sc->enable_bg_autodetect;
	int error = sysctl_handle_int(oidp, &bg_autodetect, 0, req);

	(void)arg2; /* silence WARNS == 6 */

	if ( !error && req->newptr && bg_autodetect != sc->enable_bg_autodetect ) {
		switch ( bg_autodetect ) {
			case 1: case 0: case -1:
				sc->enable_bg_autodetect = bg_autodetect;
				error = iwi_adapter_config(sc, 0, 0);
			break;
		}
	}
	return error;
}

static int
iwi_sysctl_bt_coexist(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = (void *)arg1;
	int bt_coexist = sc->enable_bt_coexist;
	int error = sysctl_handle_int(oidp, &bt_coexist, 0, req);

	(void)arg2; /* silence WARNS == 6 */

	if ( !error && req->newptr && bt_coexist != sc->enable_bt_coexist ) {
		switch ( bt_coexist ) {
			case 1: case 0: case -1:
				sc->enable_bt_coexist = bt_coexist;
				error = iwi_adapter_config(sc, 0, 0);
			break;
		}
	}
	return error;
}

static int
iwi_sysctl_dump_logs(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	int result = -1;
	int error = sysctl_handle_int(oidp, &result, 0, req);

	(void)arg2; /* silence WARNS == 6 */

	if (!error && req->newptr && result == 1) {
		iwi_dump_fw_event_log(sc);
		iwi_dump_fw_error_log(sc);
	}
	return error;
}

static int
iwi_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	u_int32_t size;
	struct iwi_dump_buffer dump;

	(void)arg2; /* silence WARNS == 6 */
	(void)oidp; /* silence WARNS == 6 */

	if (!(sc->flags & IWI_FLAG_FW_INITED)) {
		bzero(dump.buf, sizeof dump.buf);
		return SYSCTL_OUT(req, &dump, sizeof dump);
	}

	size = min(CSR_READ_4(sc, IWI_CSR_TABLE0_SIZE), 128 - 1);
	CSR_READ_REGION_4(sc, IWI_CSR_TABLE0_BASE, &dump.buf[1], size);

	return SYSCTL_OUT(req, &dump, sizeof dump);
}

static int
iwi_sysctl_radio(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	int val;

	(void)arg2; /* silence WARNS == 6 */
	(void)oidp; /* silence WARNS == 6 */

	val = (CSR_READ_4(sc, IWI_CSR_IO) & IWI_IO_RADIO_ENABLED) ? 1 : 0;
	return SYSCTL_OUT(req, &val, sizeof val);
}

static int
iwi_sysctl_neg_best_rates_first(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	int best_first = sc->enable_neg_best_first;
	int error = sysctl_handle_int(oidp, &best_first, 0, req);

	(void)arg2; /* silence WARNS == 6 */
	(void)oidp; /* silence WARNS == 6 */

	if ( !error && req->newptr && best_first != sc->enable_neg_best_first ) {
		switch ( best_first ) {
			case 1: case 0: case -1:
				sc->enable_neg_best_first = best_first;
			break;
		}
	}
	return error;
}

static int
iwi_sysctl_disable_unicast_decryption(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	int disable_uni = sc->disable_unicast_decryption;
	int error = sysctl_handle_int(oidp, &disable_uni, 0, req);

	(void)arg2; /* silence WARNS == 6 */
	(void)oidp; /* silence WARNS == 6 */

	if (!error && req->newptr && disable_uni != sc->disable_unicast_decryption) {
		switch ( disable_uni ) {
			case 1: case 0: case -1:
				sc->disable_unicast_decryption = disable_uni;
			break;
		}
	}
	return error;
}

static int
iwi_sysctl_disable_multicast_decryption(SYSCTL_HANDLER_ARGS)
{
	struct iwi_softc *sc = arg1;
	int disable_mul = sc->disable_multicast_decryption;
	int error = sysctl_handle_int(oidp, &disable_mul, 0, req);

	(void)arg2; /* silence WARNS == 6 */
	(void)oidp; /* silence WARNS == 6 */

	if (!error && req->newptr && disable_mul!=sc->disable_multicast_decryption){
		switch ( disable_mul ) {
			case 1: case 0: case -1:
				sc->disable_multicast_decryption = disable_mul;
			break;
		}
	}
	return error;
}


