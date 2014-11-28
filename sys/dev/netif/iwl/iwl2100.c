/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/netmsg2.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "if_iwlvar.h"
#include "iwl2100reg.h"
#include "iwl2100var.h"

#define IWL2100_INIT_F_ENABLE	0x1
#define IWL2100_INIT_F_IBSSCHAN	0x2

#define sc_tx_th	sc_u_tx_th.u_tx_th
#define sc_rx_th	sc_u_rx_th.u_rx_th

static void	iwl2100_init(void *);
static int	iwl2100_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	iwl2100_start(struct ifnet *, struct ifaltq_subque *);
static void	iwl2100_watchdog(struct ifnet *);
static int	iwl2100_newstate(struct ieee80211com *, enum ieee80211_state, int);
static int	iwl2100_media_change(struct ifnet *);
static void	iwl2100_media_status(struct ifnet *, struct ifmediareq *);
static void	iwl2100_stop(struct iwl2100_softc *);
static void	iwl2100_restart(struct iwl2100_softc *);
static void	iwl2100_reinit(struct iwl2100_softc *);

static void	iwl2100_intr(void *);
static void	iwl2100_txeof(struct iwl2100_softc *);
static void	iwl2100_rxeof(struct iwl2100_softc *);
static void	iwl2100_rxeof_status(struct iwl2100_softc *, int);
static void	iwl2100_rxeof_note(struct iwl2100_softc *, int);
static void	iwl2100_rxeof_cmd(struct iwl2100_softc *, int);
static void	iwl2100_rxeof_data(struct iwl2100_softc *, int);

static void	iwl2100_init_dispatch(struct netmsg *);
static void	iwl2100_reinit_dispatch(struct netmsg *);
static void	iwl2100_stop_dispatch(struct netmsg *);
static void	iwl2100_newstate_dispatch(struct netmsg *);
static void	iwl2100_scanend_dispatch(struct netmsg *);
static void	iwl2100_restart_dispatch(struct netmsg *);
static void	iwl2100_bmiss_dispatch(struct netmsg *);

static void	iwl2100_stop_callouts(struct iwl2100_softc *);
static void	iwl2100_restart_bmiss(void *);
static void	iwl2100_ibss_bssid(void *);
static void	iwl2100_reinit_callout(void *);

static int	iwl2100_dma_alloc(device_t);
static void	iwl2100_dma_free(device_t);
static int	iwl2100_dma_mbuf_create(device_t);
static void	iwl2100_dma_mbuf_destroy(device_t, int, int);
static int	iwl2100_init_tx_ring(struct iwl2100_softc *);
static int	iwl2100_init_rx_ring(struct iwl2100_softc *);
static void	iwl2100_free_tx_ring(struct iwl2100_softc *);
static void	iwl2100_free_rx_ring(struct iwl2100_softc *);

static int	iwl2100_alloc_cmd(struct iwl2100_softc *);
static void	iwl2100_free_cmd(struct iwl2100_softc *);
static int	iwl2100_wait_cmd(struct iwl2100_softc *);

static void	iwl2100_rxdesc_setup(struct iwl2100_softc *, int);
static int	iwl2100_newbuf(struct iwl2100_softc *, int, int);
static int	iwl2100_encap(struct iwl2100_softc *, struct mbuf *);

static void	iwl2100_chan_change(struct iwl2100_softc *,
				    const struct ieee80211_channel *);

static int	iwl2100_alloc_firmware(struct iwl2100_softc *,
				       enum ieee80211_opmode);
static void	iwl2100_free_firmware(struct iwl2100_softc *);
static int	iwl2100_load_firmware(struct iwl2100_softc *,
				      enum ieee80211_opmode);
static int	iwl2100_load_fw_ucode(struct iwl2100_softc *,
				      const struct iwl2100_firmware *);
static int	iwl2100_load_fw_data(struct iwl2100_softc *,
				     const struct iwl2100_firmware *);
static int	iwl2100_init_firmware(struct iwl2100_softc *);

static int	iwl2100_read_ord2(struct iwl2100_softc *, uint32_t,
				  void *, int);
static uint32_t	iwl2100_read_ord1(struct iwl2100_softc *, uint32_t);
static void	iwl2100_write_ord1(struct iwl2100_softc *, uint32_t, uint32_t);

static int	iwl2100_reset(struct iwl2100_softc *);
static int	iwl2100_hw_reset(struct iwl2100_softc *);
static int	iwl2100_rfkilled(struct iwl2100_softc *);

static int	iwl2100_scan(struct iwl2100_softc *);
static int	iwl2100_auth(struct iwl2100_softc *);
static int	iwl2100_ibss(struct iwl2100_softc *);

static int	iwl2100_hw_init(struct iwl2100_softc *, const uint8_t *,
				const uint8_t *, uint8_t, uint32_t);
static void	iwl2100_hw_stop(struct iwl2100_softc *);
static int	iwl2100_config(struct iwl2100_softc *, const uint8_t *,
			       const uint8_t *, uint8_t, int);
static int	iwl2100_start_scan(struct iwl2100_softc *, uint32_t, uint32_t);

static int	iwl2100_config_op(struct iwl2100_softc *, uint32_t);
static int	iwl2100_set_addr(struct iwl2100_softc *, const uint8_t *);
static int	iwl2100_set_opmode(struct iwl2100_softc *,
				   enum ieee80211_opmode);
static int	iwl2100_set_80211(struct iwl2100_softc *);
static int	iwl2100_set_basicrates(struct iwl2100_softc *);
static int	iwl2100_set_txrates(struct iwl2100_softc *);
static int	iwl2100_set_powersave(struct iwl2100_softc *, int);
static int	iwl2100_set_rtsthreshold(struct iwl2100_softc *, uint16_t);
static int	iwl2100_set_bssid(struct iwl2100_softc *, const uint8_t *);
static int	iwl2100_set_essid(struct iwl2100_softc *, const uint8_t *, int);
static int	iwl2100_set_auth_ciphers(struct iwl2100_softc *,
					 enum ieee80211_authmode);
static int	iwl2100_set_wepkey(struct iwl2100_softc *,
				   const struct ieee80211_key *);
static int	iwl2100_set_weptxkey(struct iwl2100_softc *, ieee80211_keyix);
static int	iwl2100_set_privacy(struct iwl2100_softc *, int);
static int	iwl2100_set_chan(struct iwl2100_softc *,
				 const struct ieee80211_channel *);
static int	iwl2100_set_scanopt(struct iwl2100_softc *, uint32_t, uint32_t);
static int	iwl2100_set_scan(struct iwl2100_softc *);
static int	iwl2100_set_optie(struct iwl2100_softc *, void *, uint16_t);
static int	iwl2100_set_bintval(struct iwl2100_softc *, uint16_t);
static int	iwl2100_set_txpower(struct iwl2100_softc *, uint16_t);

static __inline int
iwl2100_config_done(struct iwl2100_softc *sc)
{
	return iwl2100_config_op(sc, IWL2100_CMD_CONF_DONE);
}

static __inline int
iwl2100_config_start(struct iwl2100_softc *sc)
{
	return iwl2100_config_op(sc, IWL2100_CMD_CONF_START);
}

static __inline void
iwl2100_restart_done(struct iwl2100_softc *sc)
{
	callout_stop(&sc->sc_restart_bmiss);
	sc->sc_flags &= ~IWL2100_F_RESTARTING;
}

int
iwl2100_attach(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint16_t val;
	int error, i;

	/*
	 * Linux voodoo:
	 * Clear the retry timeout PCI configuration register to keep
	 * PCI TX retries from interfering with C3 CPU state.
	 */
	pci_write_config(dev, IWL2100_PCIR_RETRY_TIMEOUT, 0, 1);

	/*
	 * Allocate DMA stuffs
	 */
	error = iwl2100_dma_alloc(dev);
	if (error)
		return error;

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWL2100_INTR_MASK, 0);

	/*
	 * SW reset before reading EEPROM
	 */
	error = iwl2100_reset(sc);
	if (error)
		return error;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = iwl2100_init;
	ifp->if_ioctl = iwl2100_ioctl;
	ifp->if_start = iwl2100_start;
	ifp->if_watchdog = iwl2100_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IWL2100_TX_USED_MAX);
#ifdef notyet
	ifq_set_ready(&ifp->if_snd);
#endif

#ifdef DUMP_EEPROM
	device_printf(dev, "eeprom\n");
	for (i = 0; i < 128; ++i) {
		if (i != 0 && i % 8 == 0)
			kprintf("\n");
		val = iwl_read_eeprom(&sc->iwlcom, i);
		kprintf("%04x ", val);
	}
	kprintf("\n");
#endif

	/* IBSS channel mask */
	sc->sc_ibss_chans = iwl_read_eeprom(&sc->iwlcom,
			    IWL2100_EEPROM_IBSS_CHANS) & IWL2100_CFG_CHANMASK;

	/* BSS channel mask */
	sc->sc_bss_chans = iwl_read_eeprom(&sc->iwlcom, IWL2100_EEPROM_CHANS);

	/*
	 * Set MAC address
	 */
	for (i = 0; i < ETHER_ADDR_LEN / 2; ++i) {
		val = iwl_read_eeprom(&sc->iwlcom, IWL2100_EEPROM_MAC + i);
		ic->ic_myaddr[i * 2] = val >> 8;
		ic->ic_myaddr[(i * 2) + 1] = val & 0xff;
	}

	/*
	 * Set supported channels
	 */
	for (i = 0; i < 14; ++i) {
		if (sc->sc_bss_chans & (1 << i)) {
			int chan = i + 1;

			ic->ic_channels[chan].ic_freq =
				ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_B;
		}
	}

	ic->ic_sup_rates[IEEE80211_MODE_11B] = iwl_rateset_11b;
	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_caps = IEEE80211_C_MONITOR |
		      IEEE80211_C_IBSS |
		      IEEE80211_C_SHPREAMBLE |
		      IEEE80211_C_WPA;
	ic->ic_caps_ext = IEEE80211_CEXT_AUTOSCAN;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_opmode = IEEE80211_M_STA;

	ieee80211_ifattach(ic);

	/*
	 * ieee80211_frame will be stripped on TX path, so only
	 * extra space needs to be reserved.
	 */
	ic->ic_headroom = sizeof(struct iwl2100_tx_hdr) -
			  sizeof(struct ieee80211_frame);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwl2100_newstate;

	ieee80211_media_init(ic, iwl2100_media_change, iwl2100_media_status);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE,
			       iwl2100_intr, sc, &sc->sc_irq_handle,
			       ifp->if_serializer);
	if (error) {
		device_printf(dev, "can't setup intr\n");
		ieee80211_ifdetach(ic);
		return ENXIO;
	}

	/*
	 * Attach radio tap
	 */
	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
		      sizeof(struct ieee80211_frame) + sizeof(sc->sc_tx_th),
		      &sc->sc_drvbpf);

	sc->sc_tx_th_len = roundup(sizeof(sc->sc_tx_th), sizeof(uint32_t));
	sc->sc_tx_th.wt_ihdr.it_len = htole16(sc->sc_tx_th_len);
	sc->sc_tx_th.wt_ihdr.it_present = htole32(IWL2100_TX_RADIOTAP_PRESENT);

	sc->sc_rx_th_len = roundup(sizeof(sc->sc_rx_th), sizeof(uint32_t));
	sc->sc_rx_th.wr_ihdr.it_len = htole16(sc->sc_rx_th_len);
	sc->sc_rx_th.wr_ihdr.it_present = htole32(IWL2100_RX_RADIOTAP_PRESENT);

	sc->sc_tx_th.wt_chan_flags = sc->sc_rx_th.wr_chan_flags =
		htole16(IEEE80211_CHAN_B);

	/*
	 * Create worker thread and initialize all necessary messages
	 */
	iwl_create_thread(&sc->iwlcom, device_get_unit(dev));

	iwlmsg_init(&sc->sc_scanend_msg, &netisr_adone_rport,
		    iwl2100_scanend_dispatch, sc);
	iwlmsg_init(&sc->sc_restart_msg, &netisr_adone_rport,
		    iwl2100_restart_dispatch, sc);
	iwlmsg_init(&sc->sc_bmiss_msg, &netisr_adone_rport,
		    iwl2100_bmiss_dispatch, sc);
	iwlmsg_init(&sc->sc_reinit_msg, &netisr_adone_rport,
		    iwl2100_reinit_dispatch, sc);

	iwlmsg_init(&sc->sc_assoc_msg, &netisr_adone_rport,
		    iwl2100_newstate_dispatch, sc);
	sc->sc_assoc_msg.iwlm_nstate = IEEE80211_S_ASSOC;
	sc->sc_assoc_msg.iwlm_arg = -1;

	iwlmsg_init(&sc->sc_run_msg, &netisr_adone_rport,
		    iwl2100_newstate_dispatch, sc);
	sc->sc_run_msg.iwlm_nstate = IEEE80211_S_RUN;
	sc->sc_run_msg.iwlm_arg = -1;

	/*
	 * Initialize callouts
	 */
	callout_init(&sc->sc_restart_bmiss);
	callout_init(&sc->sc_ibss);
	callout_init(&sc->sc_reinit);

	/* Add sysctl node */
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
			SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
			"debug", CTLFLAG_RW, &sc->sc_debug, 0, "debug flags");

	if (bootverbose)
		ieee80211_announce(ic);
	return 0;
}

void
iwl2100_detach(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->sc_ic.ic_if;

		lwkt_serialize_enter(ifp->if_serializer);

		sc->sc_flags |= IWL2100_F_DETACH;
		iwl2100_stop(sc);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);
		iwl_destroy_thread(&sc->iwlcom);

		lwkt_serialize_exit(ifp->if_serializer);

		iwl2100_free_firmware(sc);

		bpfdetach(ifp);
		ieee80211_ifdetach(&sc->sc_ic);
	}
	iwl2100_dma_free(dev);
}

int
iwl2100_shutdown(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);
	iwl2100_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static void
iwl2100_stop(struct iwl2100_softc *sc)
{
	struct iwlmsg msg;

	ASSERT_SERIALIZED(sc->sc_ic.ic_if.if_serializer);

	iwl2100_stop_callouts(sc);

	iwlmsg_init(&msg, &sc->sc_reply_port, iwl2100_stop_dispatch, sc);
	lwkt_domsg(&sc->sc_thread_port, &msg.iwlm_nmsg.nm_lmsg, 0);
}

static void
iwl2100_stop_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;

	ASSERT_SERIALIZED(sc->sc_ic.ic_if.if_serializer);

	ieee80211_new_state(&sc->sc_ic, IEEE80211_S_INIT, -1);
	iwl2100_hw_stop(sc);
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static void
iwl2100_hw_stop(struct iwl2100_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);
	KKASSERT(curthread == &sc->sc_thread);

	callout_stop(&sc->sc_reinit);

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWL2100_INTR_MASK, 0);

	/*
	 * HW and SW reset
	 */
	iwl2100_hw_reset(sc);
	iwl2100_reset(sc);

	/*
	 * Free TX/RX rings
	 */
	iwl2100_free_tx_ring(sc);
	iwl2100_free_rx_ring(sc);

	/* NOTE: MUST after iwl2100_free_tx_ring() */
	iwl2100_free_cmd(sc);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	sc->sc_tx_timer = 0;
	sc->sc_flags &= ~(IWL2100_F_WAITCMD |
			  IWL2100_F_INITED |
			  IWL2100_F_SCANNING |
			  IWL2100_F_RESTARTING |
			  IWL2100_F_IFSTART |
			  IWL2100_F_ERROR |
			  IWL2100_F_ZERO_CMD);
}

static int
iwl2100_reset(struct iwl2100_softc *sc)
{
	int i;

	/*
	 * Software reset
	 */
#define WAIT_MAX	1000

	CSR_WRITE_4(sc, IWL2100_RESET, IWL2100_RESET_SW);
	for (i = 0; i < WAIT_MAX; ++i) {
		DELAY(10);
		if (CSR_READ_4(sc, IWL2100_RESET) & IWL2100_RESET_DONE)
			break;
	}
	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if, "sw reset timed out\n");
		return ETIMEDOUT;
	}

#undef WAIT_MAX

	/*
	 * Move to D0 state, wait clock to become stable
	 */
#define WAIT_MAX	10000

	CSR_WRITE_4(sc, IWL2100_CTRL, IWL2100_CTRL_INITDONE);
	for (i = 0; i < WAIT_MAX; ++i) {
		DELAY(200);
		if (CSR_READ_4(sc, IWL2100_CTRL) & IWL2100_CTRL_CLKREADY)
			break;
	}
	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if, "can't stablize clock\n");
		return ETIMEDOUT;
	}

#undef WAIT_MAX

	/*
	 * Move to D0 standby
	 */
	CSR_SETBITS_4(sc, IWL2100_CTRL, IWL2100_CTRL_STANDBY);
	return 0;
}

static int
iwl2100_dma_alloc(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	int error;

	/*
	 * Create top level DMA tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MAXBSIZE,
				   BUS_SPACE_UNRESTRICTED,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_dtag);
	if (error) {
		device_printf(dev, "can't create DMA tag\n");
		return error;
	}

	/*
	 * Create DMA stuffs for TX desc ring
	 */
	error = iwl_dma_mem_create(dev, sc->sc_dtag, IWL2100_TXRING_SIZE,
				   &tr->tr_dtag, (void **)&tr->tr_desc,
				   &tr->tr_paddr, &tr->tr_dmap);
	if (error) {
		device_printf(dev, "can't create DMA memory for "
			      "TX desc ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for RX desc ring
	 */
	error = iwl_dma_mem_create(dev, sc->sc_dtag, IWL2100_RXRING_SIZE,
				   &rr->rr_dtag, (void **)&rr->rr_desc,
				   &rr->rr_paddr, &rr->rr_dmap);
	if (error) {
		device_printf(dev, "can't create DMA memory for "
			      "RX desc ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for RX status ring
	 */
	error = iwl_dma_mem_create(dev, sc->sc_dtag, IWL2100_RXSTATUS_SIZE,
				   &rr->rr_st_dtag, (void **)&rr->rr_status,
				   &rr->rr_st_paddr, &rr->rr_st_dmap);
	if (error) {
		device_printf(dev, "can't create DMA memory for "
			      "RX status ring\n");
		return error;
	}

	/*
	 * Create mbuf DMA stuffs
	 */
	error = iwl2100_dma_mbuf_create(dev);
	if (error)
		return error;

	return 0;
}

static void
iwl2100_dma_free(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;

	/* Free DMA stuffs for TX desc ring */
	iwl_dma_mem_destroy(tr->tr_dtag, tr->tr_desc, tr->tr_dmap);

	/* Free DMA stuffs for RX desc ring */
	iwl_dma_mem_destroy(rr->rr_dtag, rr->rr_desc, rr->rr_dmap);

	/* Free DMA stuffs for RX status ring */
	iwl_dma_mem_destroy(rr->rr_st_dtag, rr->rr_status, rr->rr_st_dmap);

	/* Free DMA stuffs for mbufs */
	iwl2100_dma_mbuf_destroy(dev, IWL2100_TX_NDESC, IWL2100_RX_NDESC);

	/* Free top level DMA tag */
	if (sc->sc_dtag != NULL)
		bus_dma_tag_destroy(sc->sc_dtag);
}

static int
iwl2100_dma_mbuf_create(device_t dev)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	int i, error;

	/*
	 * Create mbuf DMA tag
	 */
	error = bus_dma_tag_create(sc->sc_dtag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MCLBYTES, IWL2100_NSEG_MAX,
				   BUS_SPACE_MAXSIZE_32BIT,
				   BUS_DMA_ALLOCNOW, &sc->sc_mbuf_dtag);
	if (error) {
		device_printf(dev, "can't create mbuf DMA tag\n");
		return error;
	}

	/*
	 * Create spare DMA map for RX mbufs
	 */
	error = bus_dmamap_create(sc->sc_mbuf_dtag, 0, &rr->rr_tmp_dmap);
	if (error) {
		device_printf(dev, "can't create spare mbuf DMA map\n");
		bus_dma_tag_destroy(sc->sc_mbuf_dtag);
		sc->sc_mbuf_dtag = NULL;
		return error;
	}

	/*
	 * Create DMA maps for RX mbufs
	 */
	for (i = 0; i < IWL2100_RX_NDESC; ++i) {
		error = bus_dmamap_create(sc->sc_mbuf_dtag, 0,
					  &rr->rr_buf[i].rb_dmap);
		if (error) {
			device_printf(dev, "can't create %d RX mbuf "
				      "for RX ring\n", i);
			iwl2100_dma_mbuf_destroy(dev, 0, i);
			return error;
		}
	}

	/*
	 * Create DMA maps for TX mbufs
	 */
	for (i = 0; i < IWL2100_TX_NDESC; ++i) {
		error = bus_dmamap_create(sc->sc_mbuf_dtag, 0,
					  &tr->tr_buf[i].tb_dmap);
		if (error) {
			device_printf(dev, "can't create %d TX mbuf "
				      "DMA map\n", i);
			iwl2100_dma_mbuf_destroy(dev, i, IWL2100_RX_NDESC);
			return error;
		}
	}
	return 0;
}

static void
iwl2100_dma_mbuf_destroy(device_t dev, int tx_done, int rx_done)
{
	struct iwl2100_softc *sc = device_get_softc(dev);
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	int i;

	if (sc->sc_mbuf_dtag == NULL)
		return;

	/*
	 * Destroy DMA maps for RX mbufs
	 */
	for (i = 0; i < rx_done; ++i) {
		struct iwl2100_rxbuf *rb = &rr->rr_buf[i];

		KASSERT(rb->rb_mbuf == NULL, ("RX mbuf is not freed yet"));
		bus_dmamap_destroy(sc->sc_mbuf_dtag, rb->rb_dmap);
	}

	/*
	 * Destroy DMA maps for TX mbufs
	 */
	for (i = 0; i < tx_done; ++i) {
		struct iwl2100_txbuf *tb = &tr->tr_buf[i];

		KASSERT(tb->tb_mbuf == NULL, ("TX mbuf is not freed yet"));
		bus_dmamap_destroy(sc->sc_mbuf_dtag, tb->tb_dmap);
	}

	/*
	 * Destroy spare mbuf DMA map
	 */
	bus_dmamap_destroy(sc->sc_mbuf_dtag, rr->rr_tmp_dmap);

	/*
	 * Destroy mbuf DMA tag
	 */
	bus_dma_tag_destroy(sc->sc_mbuf_dtag);
	sc->sc_mbuf_dtag = NULL;
}

static void
iwl2100_init(void *xsc)
{
	struct iwl2100_softc *sc = xsc;
	struct iwlmsg msg;

	ASSERT_SERIALIZED(sc->sc_ic.ic_if.if_serializer);

	iwl2100_stop_callouts(sc);

	iwlmsg_init(&msg, &sc->sc_reply_port, iwl2100_init_dispatch, sc);
	lwkt_domsg(&sc->sc_thread_port, &msg.iwlm_nmsg.nm_lmsg, 0);
}

static void
iwl2100_init_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int error = 0, flags;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto back;

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		/*
		 * XXX
		 * Workaround for dummy firmware:
		 * Don't enable hardware too early, since
		 * once it is enabled, it will start scanning.
		 */
		flags = 0;
	} else {
		flags = IWL2100_INIT_F_ENABLE;
	}

	/* Always put the device into a known state */
	error = iwl2100_hw_init(sc, NULL,
		ic->ic_des_essid, ic->ic_des_esslen, flags);
	if (error)
		goto back;

	if (sc->sc_flags & IWL2100_F_ZERO_CMD) {
		if_printf(ifp, "zero cmd, reinit 1s later\n");
		iwl2100_hw_stop(sc);

		callout_reset(&sc->sc_reinit, hz, iwl2100_reinit_callout, sc);
		goto back;
	}

	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	} else {
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	}
back:
	if (error)
		iwl2100_stop(sc);
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static int
iwl2100_ioctl(struct ifnet *ifp, u_long cmd, caddr_t req, struct ucred *cr)
{
	struct iwl2100_softc *sc = ifp->if_softc;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		return 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				iwl2100_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				iwl2100_stop(sc);
			} else {
				/*
				 * Stop callouts explicitly, since
				 * if reinitialization is happening,
				 * IFF_RUNNING will not be turned on.
				 */
				iwl2100_stop_callouts(sc);
			}
		}
		break;
	default:
		error = ieee80211_ioctl(&sc->sc_ic, cmd, req, cr);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			iwl2100_init(sc);
		error = 0;
	}
	return error;
}

static void
iwl2100_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct iwl2100_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	int trans = 0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH) {
		ieee80211_drain_mgtq(&ic->ic_mgtq);
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	if ((sc->sc_flags & IWL2100_F_IFSTART) == 0) {
		ifq_purge(&ifp->if_snd);
		goto back;
	}

	while (tr->tr_used < IWL2100_TX_USED_MAX) {
		struct ieee80211_frame *wh;
		struct ieee80211_node *ni;
		struct ether_header *eh;
		struct mbuf *m;

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

		/*
		 * TX radio tap
		 */
		if (sc->sc_drvbpf != NULL) {
			if (wh->i_fc[1] & IEEE80211_FC1_WEP)
				sc->sc_tx_th.wt_flags = IEEE80211_RADIOTAP_F_WEP;
			else
				sc->sc_tx_th.wt_flags = 0;
			bpf_ptap(sc->sc_drvbpf, m, &sc->sc_tx_th,
				 sc->sc_tx_th_len);
		}
		wh = NULL;	/* Catch any invalid use */

		ieee80211_free_node(ni);

		if (iwl2100_encap(sc, m)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			continue;
		}

		IFNET_STAT_INC(ifp, opackets, 1);
		trans = 1;
	}

	if (tr->tr_used >= IWL2100_TX_USED_MAX)
		ifq_set_oactive(&ifp->if_snd);

	if (trans) {
		bus_dmamap_sync(tr->tr_dtag, tr->tr_dmap, BUS_DMASYNC_PREWRITE);
		CSR_WRITE_4(sc, IWL2100_TXQ_WRITE_IDX, tr->tr_index);
		sc->sc_tx_timer = 5;
	}
back:
	ieee80211_drain_mgtq(&ic->ic_mgtq);
	ifp->if_timer = 1;
}

static void
iwl2100_watchdog(struct ifnet *ifp)
{
	struct iwl2100_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		return;

	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0) {
			if_printf(ifp, "watchdog timeout!\n");
			IFNET_STAT_INC(ifp, oerrors, 1);
			iwl2100_restart(sc);
			return;
		} else {
			ifp->if_timer = 1;
		}
	}
	ieee80211_watchdog(&sc->sc_ic);
}

static int
iwl2100_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct iwl2100_softc *sc = ifp->if_softc;
	struct iwlmsg msg;

	ASSERT_SERIALIZED(ifp->if_serializer);

	iwlmsg_init(&msg, &sc->sc_reply_port, iwl2100_newstate_dispatch, sc);
	msg.iwlm_nstate = nstate;
	msg.iwlm_arg = arg;

	return lwkt_domsg(&sc->sc_thread_port, &msg.iwlm_nmsg.nm_lmsg, 0);
}

static void
iwl2100_newstate_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ieee80211com *ic = &sc->sc_ic;
#ifdef INVARIANTS
	struct ifnet *ifp = &ic->ic_if;
#endif
	enum ieee80211_state nstate, ostate;
	int arg = msg->iwlm_arg, error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	nstate = msg->iwlm_nstate;
	ostate = ic->ic_state;

	sc->sc_flags &= ~IWL2100_F_IFSTART;
	sc->sc_state_age++;

	iwl2100_chan_change(sc, ic->ic_curchan);

	callout_stop(&sc->sc_ibss);
	iwl2100_restart_done(sc);

	if (nstate == IEEE80211_S_INIT)
		goto back;

	if (sc->sc_flags & IWL2100_F_DETACH) {
		/*
		 * Except for INIT, we skip rest of the
		 * state changes during detaching
		 */
		goto reply;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		if (nstate == IEEE80211_S_AUTH)
			error = iwl2100_auth(sc);
		else if (nstate == IEEE80211_S_RUN)
			sc->sc_flags |= IWL2100_F_IFSTART;
	} else if (ic->ic_opmode == IEEE80211_M_IBSS) {
		if (nstate == IEEE80211_S_RUN) {
			DPRINTF(sc, IWL2100_DBG_IBSS, "%s",
				"start/join ibss\n");

			/*
			 * IWL2100_F_IFSTART can't be turned on
			 * until BSSID generated by the firmware
			 * is extracted.
			 *
			 * XXX only if we started the IBSS
			 */
			error = iwl2100_ibss(sc);
		}
	}
back:
	if (!error)
		error = sc->sc_newstate(ic, nstate, arg);

	if (!error) {
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			/*
			 * Don't use 'nstate' here, since for IBSS
			 * mode 802.11 layer may enter RUN state in
			 * a recursive manner, i.e. when we reach
			 * here, nstate != ic->ic_state
			 */
			if (ic->ic_state == IEEE80211_S_SCAN &&
			    ic->ic_state != ostate) {
				DPRINTF(sc, IWL2100_DBG_SCAN, "%s",
					"start scan\n");
				error = iwl2100_scan(sc);
			}
		}
	}
reply:
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static int
iwl2100_media_change(struct ifnet *ifp)
{
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		iwl2100_init(ifp->if_softc);
	return 0;
}

static void
iwl2100_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct iwl2100_softc *sc = ifp->if_softc;

	if (sc->sc_flags & IWL2100_F_IFSTART) {
		struct ieee80211_node *ni = sc->sc_ic.ic_bss;
		uint32_t txrate;
		int i, nrates = 4;

		txrate = iwl2100_read_ord1(sc, IWL2100_ORD1_TXRATE) & 0xf;
		if (ni->ni_rates.rs_nrates < 4)
			nrates = ni->ni_rates.rs_nrates;

		for (i = 0; i < nrates; ++i) {
			if ((1 << i) & txrate)
				ni->ni_txrate = i;
		}
	}
	ieee80211_media_status(ifp, imr);
}

static void
iwl2100_intr(void *xsc)
{
	struct iwl2100_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t intr_status;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->sc_flags & IWL2100_F_INITED) == 0)
		return;

	intr_status = CSR_READ_4(sc, IWL2100_INTR_STATUS);
	if (intr_status == 0xffffffff)	/* not for us */
		return;

	if ((intr_status & IWL2100_INTRS) == 0)	/* not interested */
		return;

	sc->sc_flags |= IWL2100_F_IN_INTR;

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWL2100_INTR_MASK, 0);

	if (intr_status & IWL2100_INTR_EFATAL) {
		uint32_t error_info;

		if_printf(ifp, "intr fatal error\n");
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_EFATAL);

		error_info = IND_READ_4(sc, IWL2100_IND_ERROR_INFO);
		IND_READ_4(sc, error_info & IWL2100_IND_ERRORADDR_MASK);

		callout_stop(&sc->sc_reinit);
		iwl2100_reinit(sc);

		/* Leave interrupts disabled */
		goto back;
	}

	if (intr_status & IWL2100_INTR_EPARITY) {
		if_printf(ifp, "intr parity error\n");
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_EPARITY);
	}

	if (intr_status & IWL2100_INTR_RX) {
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_RX);
		iwl2100_rxeof(sc);
		iwl2100_txeof(sc);
	}

	if (intr_status & IWL2100_INTR_TX) {
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_TX);
		iwl2100_txeof(sc);
	}

	if (intr_status & IWL2100_INTR_FW_INITED)
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_FW_INITED);
	if (intr_status & IWL2100_INTR_CMD_DONE)
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, IWL2100_INTR_CMD_DONE);

	/* Enable interrupts */
	CSR_WRITE_4(sc, IWL2100_INTR_MASK, IWL2100_INTRS);
back:
	sc->sc_flags &= ~IWL2100_F_IN_INTR;
}

static int
iwl2100_hw_reset(struct iwl2100_softc *sc)
{
	int i;

	/*
	 * Initialize GPIO:
	 * - Enable GPIO3
	 * - Make GPIO3 firmware writable
	 * - Enable GPIO1
	 * - Turn off LED
	 */
	CSR_WRITE_4(sc, IWL2100_GPIO,
		    IWL2100_GPIO_3_EN | IWL2100_GPIO_3_FWWR |
		    IWL2100_GPIO_1_EN | IWL2100_GPIO_LEDOFF);

	/*
	 * Stop master
	 */
#define WAIT_MAX	5

	CSR_WRITE_4(sc, IWL2100_RESET, IWL2100_RESET_STOP_MASTER);
	for (i = 0; i < WAIT_MAX; ++i) {
		DELAY(10);

		if (CSR_READ_4(sc, IWL2100_RESET) &
		    IWL2100_RESET_MASTER_STOPPED)
			break;
	}
	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if, "can't stop master\n");
		return ETIMEDOUT;
	}

#undef WAIT_MAX

	CSR_WRITE_4(sc, IWL2100_RESET, IWL2100_RESET_SW);
	return 0;
}

static int
iwl2100_alloc_firmware(struct iwl2100_softc *sc, enum ieee80211_opmode opmode)
{
	struct {
		const char *suffix;
		uint16_t mode;
		enum ieee80211_opmode opmode;
		struct iwl2100_firmware *fw;
	} fw_arr[] = {
		{ "", IWL2100_FW_M_STA, IEEE80211_M_STA,
		  &sc->sc_fw_sta },
		{ "-i",	IWL2100_FW_M_IBSS, IEEE80211_M_IBSS,
		  &sc->sc_fw_ibss },
		{ "-p",	IWL2100_FW_M_MONITOR, IEEE80211_M_MONITOR,
		  &sc->sc_fw_monitor },
		{ NULL, 0, 0, NULL }
	};
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	const struct iwl2100_fwimg_hdr *hdr;
	struct iwl2100_firmware *fw = NULL;
	struct fw_image *image;
	char filename[128];
	int i, error;

	for (i = 0; fw_arr[i].fw != NULL; ++i) {
		fw = fw_arr[i].fw;

		if (fw_arr[i].opmode == opmode) {
			if (fw->fw_image != NULL)
				return 0;
			else
				break;
		}
	}
	KASSERT(fw_arr[i].fw != NULL, ("unsupported opmode %u", opmode));

	ksnprintf(filename, sizeof(filename), IWL2100_FW_PATH,
		  fw_arr[i].suffix);

	/*
	 * Release the serializer to avoid possible dead lock
	 */
	lwkt_serialize_exit(ifp->if_serializer);
	image = firmware_image_load(filename, NULL);
	lwkt_serialize_enter(ifp->if_serializer);

	if (image == NULL)
		return ENOENT;
	fw->fw_image = image;

	/*
	 * Verify the image
	 */
	error = EINVAL;

	hdr = (const struct iwl2100_fwimg_hdr *)image->fw_image;
	if ((hdr->version & 0xff) != 1) {
		if_printf(ifp, "%s unsupported firmware version %d",
			  image->fw_name, hdr->version & 0xff);
		goto back;
	}

	if (hdr->mode != fw_arr[i].mode) {
		if_printf(ifp, "%s contains %d mode firmware, should be %d\n",
			  image->fw_name, hdr->mode, fw_arr[i].mode);
		goto back;
	}

	if (hdr->data_size + hdr->ucode_size + sizeof(*hdr) !=
	    image->fw_imglen) {
		if_printf(ifp,
			  "%s size mismatch, %zu/hdr %zu\n",
			  image->fw_name, fw->fw_image->fw_imglen,
			  hdr->data_size + hdr->ucode_size + sizeof(*hdr));
		goto back;
	}

	fw->fw_data = (const uint8_t *)(hdr + 1);
	fw->fw_data_size = hdr->data_size;
	fw->fw_ucode = fw->fw_data + fw->fw_data_size;
	fw->fw_ucode_size = hdr->ucode_size;
	error = 0;
back:
	if (error) {
		firmware_image_unload(fw->fw_image);
		bzero(fw, sizeof(*fw));
	}
	return error;
}

static void
iwl2100_free_firmware(struct iwl2100_softc *sc)
{
	struct iwl2100_firmware *fw_arr[] =
	{ &sc->sc_fw_sta, &sc->sc_fw_ibss, &sc->sc_fw_monitor, NULL };
	int i;

	for (i = 0; fw_arr[i] != NULL; ++i) {
		struct iwl2100_firmware *fw = fw_arr[i];

		if (fw->fw_image != NULL) {
			firmware_image_unload(fw->fw_image);
			bzero(fw, sizeof(*fw));
		}
	}
}

static int
iwl2100_load_firmware(struct iwl2100_softc *sc, enum ieee80211_opmode opmode)
{
	static const struct {
		uint32_t	addr;
		int		size;
	} share_mem[] = {
		{ IWL2100_SHMEM0, IWL2100_SHMEM0_SIZE },
		{ IWL2100_SHMEM1, IWL2100_SHMEM1_SIZE },
		{ IWL2100_SHMEM2, IWL2100_SHMEM2_SIZE },
		{ IWL2100_SHMEM3, IWL2100_SHMEM3_SIZE },
		{ IWL2100_SHMEM_INTR, IWL2100_SHMEM_INTR_SIZE },
		{ 0, 0 }
	};
	const struct iwl2100_firmware *fw = NULL;
	int i, error;

	/*
	 * Pick up the firmware image corresponding to
	 * the current operation mode
	 */
	switch (opmode) {
	case IEEE80211_M_STA:
		fw = &sc->sc_fw_sta;
		break;
	case IEEE80211_M_IBSS:
		fw = &sc->sc_fw_ibss;
		break;
	case IEEE80211_M_MONITOR:
		fw = &sc->sc_fw_monitor;
		break;
	default:
		panic("unsupported opmode %d", opmode);
		break;
	}
	KASSERT(fw->fw_image != NULL,
		("opmode %d firmware image is not allocated yet\n", opmode));

	/* Load ucode */
	error = iwl2100_load_fw_ucode(sc, fw);
	if (error)
		return error;

	/* SW reset */
	error = iwl2100_reset(sc);
	if (error)
		return error;

	/* Load data */
	error = iwl2100_load_fw_data(sc, fw);
	if (error)
		return error;

	/* Clear shared memory */
	for (i = 0; share_mem[i].size != 0; ++i) {
		uint32_t addr = share_mem[i].addr;
		int j;

		for (j = 0; j < share_mem[i].size; j += 4)
			IND_WRITE_4(sc, addr + j, 0);
	}

	return 0;
}

#define IND_WRITE_FLUSH_2(sc, reg, val) \
do { \
	IND_WRITE_2((sc), (reg), (val)); \
	CSR_READ_4((sc), 0); \
} while (0)

#define IND_WRITE_FLUSH_1(sc, reg, val) \
do { \
	IND_WRITE_1((sc), (reg), (val)); \
	CSR_READ_4((sc), 0); \
} while (0)

/* XXX need more comment */
static int
iwl2100_load_fw_ucode(struct iwl2100_softc *sc,
		      const struct iwl2100_firmware *fw)
{
	struct iwl2100_ucode_resp resp;
	const uint8_t *p;
	int i;

	/* Hold ARC */
	IND_WRITE_4(sc, IWL2100_IND_HALT, IWL2100_IND_HALT_HOLD);

	/* Allow ARC to run */
	CSR_WRITE_4(sc, IWL2100_RESET, 0);

	IND_WRITE_FLUSH_2(sc, IWL2100_IND_CTRL, 0x703);
	IND_WRITE_FLUSH_2(sc, IWL2100_IND_CTRL, 0x707);

	IND_WRITE_FLUSH_1(sc, 0x210014, 0x72);
	IND_WRITE_FLUSH_1(sc, 0x210014, 0x72);

	IND_WRITE_FLUSH_1(sc, 0x210000, 0x40);
	IND_WRITE_FLUSH_1(sc, 0x210000, 0);
	IND_WRITE_FLUSH_1(sc, 0x210000, 0x40);

	p = fw->fw_ucode;
	for (i = 0; i < fw->fw_ucode_size; ++i, ++p)
		IND_WRITE_1(sc, 0x210010, *p);

	IND_WRITE_FLUSH_1(sc, 0x210000, 0);
	IND_WRITE_FLUSH_1(sc, 0x210000, 0);
	IND_WRITE_FLUSH_1(sc, 0x210000, 0x80);


	IND_WRITE_FLUSH_2(sc, IWL2100_IND_CTRL, 0x703);
	IND_WRITE_FLUSH_2(sc, IWL2100_IND_CTRL, 0x707);

	IND_WRITE_FLUSH_1(sc, 0x210014, 0x72);
	IND_WRITE_FLUSH_1(sc, 0x210014, 0x72);

	IND_WRITE_FLUSH_1(sc, 0x210000, 0);
	IND_WRITE_1(sc, 0x210000, 0x80);

#define WAIT_MAX	10
	for (i = 0; i < WAIT_MAX; ++i) {
		DELAY(10);

		if (IND_READ_1(sc, 0x210000) & 0x1)
			break;
	}
	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if,
			  "wait ucode symbol init timed out\n");
		return ETIMEDOUT;
	}
#undef WAIT_MAX

#define WAIT_MAX	30
	for (i = 0; i < WAIT_MAX; ++i) {
		uint16_t *r = (uint16_t *)&resp;
		int j;

		for (j = 0; j < sizeof(resp) / 2; ++j, ++r)
			*r = IND_READ_2(sc, 0x210004);

		if (resp.cmd_id == 1 && resp.ucode_valid == 1)
			break;
		DELAY(10);
	}
	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if,
			  "wait ucode response timed out\n");
		return ETIMEDOUT;
	}
#undef WAIT_MAX

	/* Release ARC */
	IND_WRITE_4(sc, IWL2100_IND_HALT, 0);

	if (bootverbose) {
		if_printf(&sc->sc_ic.ic_if, "ucode rev.%d date %d.%d.20%02d "
			  "time %02d:%02d\n", resp.ucode_rev,
			  resp.date_time[0], resp.date_time[1],
			  resp.date_time[2], resp.date_time[3],
			  resp.date_time[4]);
	}

	return 0;
}

#undef IND_WRITE_FLUSH_1
#undef IND_WRITE_FLUSH_2

static int
iwl2100_load_fw_data(struct iwl2100_softc *sc,
		     const struct iwl2100_firmware *fw)
{
	const uint8_t *p = fw->fw_data;
	int w = 0;

	while (w < fw->fw_data_size) {
		const struct iwl2100_fwdata_hdr *h;
		int hlen, i;

		h = (const struct iwl2100_fwdata_hdr *)p;
		if (h->len > 32 || h->len == 0) {
			if_printf(&sc->sc_ic.ic_if,
				  "firmware image data corrupted\n");
			return EINVAL;
		}
		if ((h->addr & 0x3) || (h->len & 0x3)) {
			if_printf(&sc->sc_ic.ic_if,
				  "firmware image data with unaligned "
				  "address %#x or length %#x\n",
				  h->addr, h->len);
			return EOPNOTSUPP;
		}

		hlen = sizeof(*h) + h->len - 1;
		if (w + hlen > fw->fw_data_size) {
			if_printf(&sc->sc_ic.ic_if,
				  "firmware image data size mismatch\n");
			return EINVAL;
		}

		CSR_WRITE_4(sc, IWL2100_AUTOINC_ADDR, h->addr);
		for (i = 0; i < h->len; i += 4) {
			CSR_WRITE_4(sc, IWL2100_AUTOINC_DATA,
				    *(const uint32_t *)&h->data[i]);
		}

		p += hlen;
		w += hlen;
	}
	KKASSERT(w == fw->fw_data_size);

	return 0;
}

static void
iwl2100_free_tx_ring(struct iwl2100_softc *sc)
{
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	int i;

	for (i = 0; i < IWL2100_TX_NDESC; ++i) {
		struct iwl2100_txbuf *tb = &tr->tr_buf[i];

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_mbuf_dtag, tb->tb_dmap);
			if (tb->tb_flags & IWL2100_TBF_CMDBUF) {
				KKASSERT(tb->tb_mbuf == sc->sc_cmd);
				tb->tb_flags &= ~IWL2100_TBF_CMDBUF;
			} else {
				m_freem(tb->tb_mbuf);
			}
			tb->tb_mbuf = NULL;
		}
	}

	bzero(tr->tr_desc, IWL2100_TXRING_SIZE);
	bus_dmamap_sync(tr->tr_dtag, tr->tr_dmap, BUS_DMASYNC_PREWRITE);

	tr->tr_used = 0;
	tr->tr_index = 0;
	tr->tr_coll = 0;
}

static void
iwl2100_free_rx_ring(struct iwl2100_softc *sc)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	int i;

	for (i = 0; i < IWL2100_RX_NDESC; ++i) {
		struct iwl2100_rxbuf *rb = &rr->rr_buf[i];

		if (rb->rb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_mbuf_dtag, rb->rb_dmap);
			m_freem(rb->rb_mbuf);
			rb->rb_mbuf = NULL;
		}
	}

	bzero(rr->rr_desc, IWL2100_RXRING_SIZE);
	bus_dmamap_sync(rr->rr_dtag, rr->rr_dmap, BUS_DMASYNC_PREWRITE);

	bzero(rr->rr_status, IWL2100_RXSTATUS_SIZE);
	bus_dmamap_sync(rr->rr_st_dtag, rr->rr_st_dmap, BUS_DMASYNC_PREWRITE);

	rr->rr_index = 0;
}

static void
iwl2100_free_cmd(struct iwl2100_softc *sc)
{
	if (sc->sc_cmd != NULL) {
		m_freem(sc->sc_cmd);
		sc->sc_cmd = NULL;
	}
}

static int
iwl2100_init_tx_ring(struct iwl2100_softc *sc)
{
	struct iwl2100_tx_ring *tr = &sc->sc_txring;

	tr->tr_used = 0;
	tr->tr_index = 0;
	tr->tr_coll = 0;

	bzero(tr->tr_desc, IWL2100_TXRING_SIZE);
	bus_dmamap_sync(tr->tr_dtag, tr->tr_dmap, BUS_DMASYNC_PREWRITE);

	CSR_WRITE_4(sc, IWL2100_TXQ_ADDR, tr->tr_paddr);
	CSR_WRITE_4(sc, IWL2100_TXQ_SIZE, IWL2100_TX_NDESC);
	CSR_WRITE_4(sc, IWL2100_TXQ_READ_IDX, 0);
	CSR_WRITE_4(sc, IWL2100_TXQ_WRITE_IDX, tr->tr_index);

	return 0;
}

static int
iwl2100_init_rx_ring(struct iwl2100_softc *sc)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	int i, error;

	for (i = 0; i < IWL2100_RX_NDESC; ++i) {
		error = iwl2100_newbuf(sc, i, 1);
		if (error)
			return error;
	}
	bus_dmamap_sync(rr->rr_st_dtag, rr->rr_st_dmap, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(rr->rr_dtag, rr->rr_dmap, BUS_DMASYNC_PREWRITE);

	rr->rr_index = IWL2100_RX_NDESC - 1;

	CSR_WRITE_4(sc, IWL2100_RXQ_ADDR, rr->rr_paddr);
	CSR_WRITE_4(sc, IWL2100_RXQ_SIZE, IWL2100_RX_NDESC);
	CSR_WRITE_4(sc, IWL2100_RXQ_READ_IDX, 0);
	CSR_WRITE_4(sc, IWL2100_RXQ_WRITE_IDX, rr->rr_index);

	CSR_WRITE_4(sc, IWL2100_RX_STATUS_ADDR, rr->rr_st_paddr);

	return 0;
}

static int
iwl2100_alloc_cmd(struct iwl2100_softc *sc)
{
	KKASSERT(sc->sc_cmd == NULL);

	sc->sc_cmd = m_getcl(MB_WAIT, MT_DATA, M_PKTHDR);
	if (sc->sc_cmd == NULL)
		return ENOBUFS;
	return 0;
}

static int
iwl2100_newbuf(struct iwl2100_softc *sc, int buf_idx, int init)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rxbuf *rb;
	struct iwl_dmamap_ctx ctx;
	bus_dma_segment_t seg;
	bus_dmamap_t dmap;
	struct mbuf *m;
	int error;

	KKASSERT(buf_idx < IWL2100_RX_NDESC);
	rb = &rr->rr_buf[buf_idx];

	m = m_getcl(init ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		error = ENOBUFS;

		if (init) {
			if_printf(&sc->sc_ic.ic_if, "m_getcl failed\n");
			return error;
		} else {
			goto back;
		}
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * Try load RX mbuf into temporary DMA map
	 */
	ctx.nsegs = 1;
	ctx.segs = &seg;
	error = bus_dmamap_load_mbuf(sc->sc_mbuf_dtag, rr->rr_tmp_dmap, m,
				     iwl_dma_buf_addr, &ctx,
				     init ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error || ctx.nsegs == 0) {
		if (!error) {
			bus_dmamap_unload(sc->sc_mbuf_dtag, rr->rr_tmp_dmap);
			error = EFBIG;
			if_printf(&sc->sc_ic.ic_if, "too many segments?!\n");
		}
		m_freem(m);

		if (init) {
			if_printf(&sc->sc_ic.ic_if, "can't load RX mbuf\n");
			return error;
		} else {
			goto back;
		}
	}

	if (!init)
		bus_dmamap_unload(sc->sc_mbuf_dtag, rb->rb_dmap);
	rb->rb_mbuf = m;
	rb->rb_paddr = seg.ds_addr;

	/*
	 * Swap RX buf's DMA map with the loaded temporary one
	 */
	dmap = rb->rb_dmap;
	rb->rb_dmap = rr->rr_tmp_dmap;
	rr->rr_tmp_dmap = dmap;

	error = 0;
back:
	iwl2100_rxdesc_setup(sc, buf_idx);
	return error;
}

static void
iwl2100_rxdesc_setup(struct iwl2100_softc *sc, int buf_idx)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rxbuf *rb;
	struct iwl2100_desc *d;
	struct iwl2100_rx_status *st;

	KKASSERT(buf_idx < IWL2100_RX_NDESC);
	rb = &rr->rr_buf[buf_idx];

	st = &rr->rr_status[buf_idx];
	bzero(st, sizeof(*st));

	d = &rr->rr_desc[buf_idx];
	bzero(d, sizeof(*d));
	d->d_paddr = rb->rb_paddr;
	d->d_len = MCLBYTES;
}

static int
iwl2100_init_firmware(struct iwl2100_softc *sc)
{
#ifdef INVARIANTS
	struct ifnet *ifp = &sc->sc_ic.ic_if;
#endif
	uint32_t intr;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	CSR_WRITE_4(sc, IWL2100_GPIO,
		    IWL2100_GPIO_3_EN | IWL2100_GPIO_3_FWWR |
		    IWL2100_GPIO_1_EN | IWL2100_GPIO_LEDOFF);
	CSR_WRITE_4(sc, IWL2100_RESET, 0);

	/*
	 * Wait for firmware to be initialized
	 */
#define WAIT_MAX	5000

	for (i = 0; i < WAIT_MAX; ++i) {
		DELAY(8000);

		intr = CSR_READ_4(sc, IWL2100_INTR_STATUS);
		if (intr & IWL2100_INTR_FW_INITED) {
			CSR_WRITE_4(sc, IWL2100_INTR_STATUS,
				    IWL2100_INTR_FW_INITED);
			break;
		}
		if (intr & (IWL2100_INTR_EFATAL | IWL2100_INTR_EPARITY)) {
			CSR_WRITE_4(sc, IWL2100_INTR_STATUS,
				    IWL2100_INTR_EFATAL | IWL2100_INTR_EPARITY);
		}
	}

	intr = CSR_READ_4(sc, IWL2100_INTR_STATUS) & IWL2100_INTRS;
	if (intr & CSR_READ_4(sc, IWL2100_INTR_MASK))
		CSR_WRITE_4(sc, IWL2100_INTR_STATUS, intr);

	if (i == WAIT_MAX) {
		if_printf(&sc->sc_ic.ic_if,
			  "firmware initialization timed out\n");
		return ETIMEDOUT;
	}

#undef WAIT_MAX

	/* Enable GPIO1/3 and allow firmware to write to them */
	CSR_SETBITS_4(sc, IWL2100_GPIO,
		      IWL2100_GPIO_1_EN | IWL2100_GPIO_1_FWWR |
		      IWL2100_GPIO_3_EN | IWL2100_GPIO_3_FWWR);
	return 0;
}

static int
iwl2100_read_ord2(struct iwl2100_softc *sc, uint32_t ofs, void *buf0, int buflen)
{
	uint8_t *buf = buf0;
	uint32_t addr, info;
	int i, len, ret;

#define IND_ALIGN	4
#define IND_ALIGN_MASK	0x3

	addr = IND_READ_4(sc, sc->sc_ord2 + (ofs << 3));
	info = IND_READ_4(sc, sc->sc_ord2 + (ofs << 3) + sizeof(addr));

	len = info & 0xffff;
	i = info >> 16;

	if ((len * i) < buflen)
		buflen = len * i;
	ret = buflen;

	i = addr & IND_ALIGN_MASK;
	addr &= ~IND_ALIGN_MASK;
	if (i) {
		int lim, r;

		KKASSERT(i < IND_ALIGN);
		if (buflen + i < IND_ALIGN)
			lim = buflen + i;
		else
			lim = IND_ALIGN;
		r = lim - i;

		CSR_WRITE_4(sc, IWL2100_IND_ADDR, addr);
		for (; i < lim; ++i, ++buf)
			*buf = CSR_READ_1(sc, IWL2100_IND_DATA + i);

		KKASSERT(buflen >= r);
		buflen -= r;
		if (buflen == 0)
			goto back;

		addr += IND_ALIGN;
	}

	len = buflen & ~IND_ALIGN_MASK;
	buflen &= IND_ALIGN_MASK;

	if (len) {
		CSR_WRITE_4(sc, IWL2100_AUTOINC_ADDR, addr);
		for (i = 0; i < len; i += 4, addr += 4, buf += 4) {
			*((uint32_t *)buf) =
			CSR_READ_4(sc, IWL2100_AUTOINC_DATA);
		}
	}
	if (buflen) {
		CSR_WRITE_4(sc, IWL2100_IND_ADDR, addr);
		for (i = 0; i < buflen; ++i, ++buf)
			*buf = CSR_READ_1(sc, IWL2100_IND_DATA + i);
	}
back:
	return ret;

#undef IND_ALIGN
#undef IND_ALIGN_MASK
}

static uint32_t
iwl2100_read_ord1(struct iwl2100_softc *sc, uint32_t ofs)
{
	uint32_t addr;

	addr = IND_READ_4(sc, sc->sc_ord1 + (ofs << 2));
	return IND_READ_4(sc, addr);
}

static void
iwl2100_write_ord1(struct iwl2100_softc *sc, uint32_t ofs, uint32_t val)
{
	uint32_t addr;

	addr = IND_READ_4(sc, sc->sc_ord1 + (ofs << 2));
	IND_WRITE_4(sc, addr, val);
}

static int
iwl2100_rfkilled(struct iwl2100_softc *sc)
{
	int i;

	if ((sc->sc_caps & IWL2100_C_RFKILL) == 0)
		return 0;

#define TEST_MAX	5

	for (i = 0; i < TEST_MAX; ++i) {
		DELAY(40);

		if (CSR_READ_4(sc, IWL2100_GPIO) & IWL2100_GPIO_RFKILLED)
			break;
	}
	if (i != TEST_MAX) {
		if_printf(&sc->sc_ic.ic_if, "RF killed\n");
		return 1;
	}

#undef TEST_MAX

	return 0;
}

static int
iwl2100_set_addr(struct iwl2100_softc *sc, const uint8_t *eaddr)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_ADDR;
	cmd->c_param_len = IEEE80211_ADDR_LEN;
	IEEE80211_ADDR_COPY(cmd->c_param, eaddr);

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_opmode(struct iwl2100_softc *sc, enum ieee80211_opmode opmode)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_OPMODE;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	switch (opmode) {
	case IEEE80211_M_STA:
		cmd->c_param[0] = IWL2100_OPMODE_STA;
		break;
	case IEEE80211_M_IBSS:
		cmd->c_param[0] = IWL2100_OPMODE_IBSS;
		break;
	case IEEE80211_M_MONITOR:
		/* YYY ipw2100 leave this unset */
		cmd->c_param[0] = IWL2100_OPMODE_MONITOR;
		break;
	default:
		panic("unsupported opmode %d", opmode);
		break;
	}

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_80211(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&ic->ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_80211;
	cmd->c_param_len = sizeof(cmd->c_param[0]) * 3;
	cmd->c_param[0] = IWL2100_CFG_IBSS | IWL2100_CFG_STA |
			  IWL2100_CFG_8021X | IWL2100_CFG_AUTO_PREAMBLE;
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		cmd->c_param[0] |= IWL2100_CFG_IBSS_AUTO_START;
	else if (ic->ic_opmode == IEEE80211_M_MONITOR) /* YYY not ipw2100 */
		cmd->c_param[0] |= IWL2100_CFG_MONITOR;
	cmd->c_param[1] = IWL2100_CFG_CHANMASK; /* XXX sc->sc_bss_chans */
	cmd->c_param[2] = IWL2100_CFG_CHANMASK; /* YYY sc->sc_ibss_chans */

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_basicrates(struct iwl2100_softc *sc)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	/*
	 * This configuration does not seem to have any effects
	 * on probe-req and assoc-req frames.
	 */
	cmd->c_cmd = IWL2100_CMD_SET_BASICRATES;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = 0x3;	/* 1Mbps and 2Mbps.  XXX from caller */

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_txrates(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_cmd *cmd;
	uint32_t rate_mask;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&ic->ic_if, "there is command pending\n");
		return EEXIST;
	}

	/* Calculate TX rate mask.  XXX let caller do this */
	if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE)
		rate_mask = 1 << ic->ic_fixed_rate;
	else
		rate_mask = 0xf; /* all 11b rates */
	KKASSERT((rate_mask & ~0xf) == 0);

	/*
	 * Set TX rates
	 */
	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_TXRATES;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = rate_mask;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}

	/*
	 * Set MSDU TX rates
	 */
	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_MSDU_TXRATES;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = rate_mask;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_powersave(struct iwl2100_softc *sc, int on)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_POWERSAVE;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = on;	/* XXX power level? */

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_rtsthreshold(struct iwl2100_softc *sc, uint16_t rtsthreshold)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_RTSTHRESHOLD;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	if (rtsthreshold == IEEE80211_RTS_MAX) {
		/* Disable RTS threshold */
		cmd->c_param[0] = IWL2100_RTS_MAX;
	} else {
		if (rtsthreshold >= IWL2100_RTS_MAX)
			rtsthreshold = IWL2100_RTS_MAX - 1;
		cmd->c_param[0] = rtsthreshold;
	}

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_bssid(struct iwl2100_softc *sc, const uint8_t *bssid)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_BSSID;
	if (bssid != NULL) {
		cmd->c_param_len = IEEE80211_ADDR_LEN;
		IEEE80211_ADDR_COPY(cmd->c_param, bssid);
	}

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_essid(struct iwl2100_softc *sc, const uint8_t *essid, int essid_len)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_ESSID;
	if (essid != NULL) {
		KKASSERT(essid_len <= sizeof(cmd->c_param));
		cmd->c_param_len = essid_len;
		if (essid_len != 0)
			bcopy(essid, cmd->c_param, essid_len);
	}

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_auth_ciphers(struct iwl2100_softc *sc,
			 enum ieee80211_authmode authmode)
{
	struct iwl2100_cmdparam_sec *sec;
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_SECURITY;
	cmd->c_param_len = sizeof(*sec);
	sec = (struct iwl2100_cmdparam_sec *)cmd->c_param;

	sec->sec_cipher_mask = IWL2100_CIPHER_NONE |
			       IWL2100_CIPHER_WEP40 |
			       IWL2100_CIPHER_TKIP |
			       IWL2100_CIPHER_CCMP |
			       IWL2100_CIPHER_WEP104;
	if (authmode == IEEE80211_AUTH_SHARED)
		sec->sec_authmode = IWL2100_AUTH_SHARED;
	else
		sec->sec_authmode = IWL2100_AUTH_OPEN;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_wepkey(struct iwl2100_softc *sc, const struct ieee80211_key *k)
{
	struct iwl2100_cmdparam_wepkey *key;
	struct iwl2100_cmd *cmd;
	int error;

	if (k->wk_keylen > IWL2100_KEYDATA_SIZE)
		return E2BIG;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_WEPKEY;
	cmd->c_param_len = sizeof(*key);
	key = (struct iwl2100_cmdparam_wepkey *)cmd->c_param;
	key->key_index = k->wk_keyix;
	key->key_len = k->wk_keylen;
	bcopy(k->wk_key, key->key_data, key->key_len);

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_weptxkey(struct iwl2100_softc *sc, ieee80211_keyix txkey)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_WEPTXKEY;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = txkey;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_privacy(struct iwl2100_softc *sc, int on)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_PRIVACY;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = on ? IWL2100_PRIVACY_ENABLE : 0;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_wait_cmd(struct iwl2100_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct mbuf *m = sc->sc_cmd;
	struct iwl_dmamap_ctx ctx;
	bus_dma_segment_t seg;
	struct iwl2100_desc *d;
	struct iwl2100_txbuf *tb;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);

	KKASSERT(tr->tr_index < IWL2100_TX_NDESC);
	tb = &tr->tr_buf[tr->tr_index];

	ctx.nsegs = 1;
	ctx.segs = &seg;
	error = bus_dmamap_load_mbuf(sc->sc_mbuf_dtag, tb->tb_dmap, m,
				     iwl_dma_buf_addr, &ctx, BUS_DMA_WAITOK);
	if (error || ctx.nsegs == 0) {
		if (!error) {
			bus_dmamap_unload(sc->sc_mbuf_dtag, tb->tb_dmap);
			error = EFBIG;
			if_printf(ifp, "too many segments?!\n");
		}

		if_printf(ifp, "can't load RX mbuf\n");
		return error;
	}
	tb->tb_mbuf = sc->sc_cmd;
	tb->tb_flags |= IWL2100_TBF_CMDBUF;

	d = &tr->tr_desc[tr->tr_index];
	d->d_paddr = seg.ds_addr;
	d->d_len = sizeof(struct iwl2100_cmd);
	d->d_nfrag = 1;
	d->d_flags = IWL2100_TXD_F_INTR | IWL2100_TXD_F_CMD;

	KKASSERT(tr->tr_used < IWL2100_TX_NDESC);
	++tr->tr_used;
	tr->tr_index = (tr->tr_index + 1) % IWL2100_TX_NDESC;

	bus_dmamap_sync(tr->tr_dtag, tr->tr_dmap, BUS_DMASYNC_PREWRITE);

	CSR_WRITE_4(sc, IWL2100_TXQ_WRITE_IDX, tr->tr_index);

	if (sc->sc_flags & IWL2100_F_IN_INTR)
		panic("sleep in interrupt thread");

	sc->sc_flags |= IWL2100_F_WAITCMD;
	error = zsleep(sc, ifp->if_serializer, 0, "iwlcmd", 2 * hz);
	if (!error) {
		sc->sc_flags &= ~IWL2100_F_WAITCMD;
		if (sc->sc_flags & IWL2100_F_ERROR) {
			if_printf(ifp, "error happened when waiting "
				  "command to be done\n");
			error = EIO;
		}
	}
	return error;
}

static void
iwl2100_rxeof(struct iwl2100_softc *sc)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int hwidx, i;

	hwidx = CSR_READ_4(sc, IWL2100_RXQ_READ_IDX);
	CSR_READ_4(sc, IWL2100_RXQ_WRITE_IDX);

	if (hwidx >= IWL2100_RX_NDESC) {
		if_printf(ifp, "invalid hardware RX index %d\n", hwidx);
		return;
	}

	KKASSERT(rr->rr_index < IWL2100_RX_NDESC);
	i = (rr->rr_index + 1) % IWL2100_RX_NDESC;
	while (hwidx != i) {
		struct iwl2100_rx_status *st = &rr->rr_status[i];
		struct iwl2100_rxbuf *rb = &rr->rr_buf[i];
		int frame_type;

		bus_dmamap_sync(rr->rr_st_dtag, rr->rr_st_dmap,
				BUS_DMASYNC_POSTREAD);
		frame_type = st->r_status & IWL2100_RXS_TYPE_MASK;

		bus_dmamap_sync(sc->sc_mbuf_dtag, rb->rb_dmap,
				BUS_DMASYNC_POSTREAD);
		switch (frame_type) {
		case IWL2100_RXS_TYPE_CMD:
			iwl2100_rxeof_cmd(sc, i);
			break;

		case IWL2100_RXS_TYPE_STATUS:
			iwl2100_rxeof_status(sc, i);
			break;

		case IWL2100_RXS_TYPE_NOTE:
			iwl2100_rxeof_note(sc, i);
			break;

		case IWL2100_RXS_TYPE_DATA:
		case IWL2100_RXS_TYPE_DATA1:
			iwl2100_rxeof_data(sc, i);
			break;

		default:
			if_printf(ifp, "unknown frame type: %d\n", frame_type);
			iwl2100_rxdesc_setup(sc, i);
			break;
		}
		i = (i + 1) % IWL2100_RX_NDESC;
	}
	bus_dmamap_sync(rr->rr_st_dtag, rr->rr_st_dmap, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(rr->rr_dtag, rr->rr_dmap, BUS_DMASYNC_POSTREAD);

	if (i == 0)
		rr->rr_index = IWL2100_RX_NDESC - 1;
	else
		rr->rr_index = i - 1;
	CSR_WRITE_4(sc, IWL2100_RXQ_WRITE_IDX, rr->rr_index);
}

static void
iwl2100_txeof(struct iwl2100_softc *sc)
{
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int hwidx;

	hwidx = CSR_READ_4(sc, IWL2100_TXQ_READ_IDX);
	CSR_READ_4(sc, IWL2100_TXQ_WRITE_IDX);
	if (hwidx >= IWL2100_TX_NDESC) {
		if_printf(ifp, "invalid hardware TX index %d\n", hwidx);
		return;
	}

	KKASSERT(tr->tr_coll < IWL2100_TX_NDESC);
	while (tr->tr_used) {
		struct iwl2100_txbuf *tb;

		if (tr->tr_coll == hwidx)
			break;

		tb = &tr->tr_buf[tr->tr_coll];
		if (tb->tb_mbuf == NULL)
			goto next;

		bus_dmamap_unload(sc->sc_mbuf_dtag, tb->tb_dmap);
		if (tb->tb_flags & IWL2100_TBF_CMDBUF) {
			tb->tb_flags &= ~IWL2100_TBF_CMDBUF;
			KKASSERT(tb->tb_mbuf == sc->sc_cmd);
		} else {
			m_freem(tb->tb_mbuf);
		}
		tb->tb_mbuf = NULL;
next:
		tr->tr_coll = (tr->tr_coll + 1) % IWL2100_TX_NDESC;

		KKASSERT(tr->tr_used > 0);
		--tr->tr_used;
	}

	if (tr->tr_used < IWL2100_TX_USED_MAX) {
		if (tr->tr_used == 0) {
			KKASSERT(tr->tr_coll == tr->tr_index);
			sc->sc_tx_timer = 0;
		}

		ifq_clr_oactive(&ifp->if_snd);
		if_devstart(ifp);
	}
}

static int
iwl2100_config(struct iwl2100_softc *sc, const uint8_t *bssid,
	       const uint8_t *essid, uint8_t esslen, int ibss_chan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int error;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		error = iwl2100_set_chan(sc, ic->ic_curchan);
		if (error) {
			if_printf(ifp, "can't set mon channel\n");
			return error;
		}
	}

	IEEE80211_ADDR_COPY(ic->ic_myaddr, IF_LLADDR(ifp));
	error = iwl2100_set_addr(sc, ic->ic_myaddr);
	if (error) {
		if_printf(ifp, "can't set MAC address\n");
		return error;
	}

	error = iwl2100_set_opmode(sc, ic->ic_opmode);
	if (error) {
		if_printf(ifp, "can't set opmode\n");
		return error;
	}

	if (ibss_chan) {
		KKASSERT(ic->ic_opmode == IEEE80211_M_IBSS);
		error = iwl2100_set_chan(sc, ic->ic_curchan);
		if (error) {
			if_printf(ifp, "can't set ibss channel\n");
			return error;
		}
	}

	error = iwl2100_set_80211(sc);
	if (error) {
		if_printf(ifp, "can't set 802.11 config\n");
		return error;
	}

	error = iwl2100_set_basicrates(sc);
	if (error) {
		if_printf(ifp, "can't set basicrates\n");
		return error;
	}

	error = iwl2100_set_txrates(sc);
	if (error) {
		if_printf(ifp, "can't set TX rates\n");
		return error;
	}

	error = iwl2100_set_powersave(sc, ic->ic_flags & IEEE80211_F_PMGTON);
	if (error) {
		if_printf(ifp, "can't turn off powersave\n");
		return error;
	}

	error = iwl2100_set_rtsthreshold(sc, ic->ic_rtsthreshold);
	if (error) {
		if_printf(ifp, "can't set RTS threshold\n");
		return error;
	}

	error = iwl2100_set_bssid(sc, bssid);
	if (error) {
		if_printf(ifp, "can't set bssid\n");
		return error;
	}

	error = iwl2100_set_essid(sc, essid, esslen);
	if (error) {
		if_printf(ifp, "can't set essid\n");
		return error;
	}

	error = iwl2100_set_auth_ciphers(sc, ic->ic_bss->ni_authmode);
	if (error) {
		if_printf(ifp, "can't set authmode and ciphers\n");
		return error;
	}

	if (ic->ic_flags & IEEE80211_F_PRIVACY) {
		ieee80211_keyix txkey = IEEE80211_KEYIX_NONE;
		int i;

		for (i = 0; i < IEEE80211_WEP_NKID; ++i) {
			const struct ieee80211_key *k = &ic->ic_nw_keys[i];

			if (k->wk_keyix == IEEE80211_KEYIX_NONE)
				continue;

			error = iwl2100_set_wepkey(sc, k);
			if (error == E2BIG) {
				continue;
			} else if (error) {
				if_printf(ifp, "can't set wepkey\n");
				return error;
			}
			txkey = k->wk_keyix;
		}

		if (txkey != IEEE80211_KEYIX_NONE) {
			/*
			 * Found some valid WEP keys.
			 *
			 * If WEP TX key index from 802.11 layer is not
			 * set, then use the first valid WEP key as TX
			 * key.
			 */
			if (ic->ic_def_txkey != IEEE80211_KEYIX_NONE)
				txkey = ic->ic_def_txkey;

			error = iwl2100_set_weptxkey(sc, txkey);
			if (error) {
				if_printf(ifp, "can't set weptxkey\n");
				return error;
			}
		}
	}

	error = iwl2100_set_privacy(sc, ic->ic_flags & IEEE80211_F_PRIVACY);
	if (error) {
		if_printf(ifp, "can't set privacy\n");
		return error;
	}

	error = iwl2100_set_optie(sc, ic->ic_opt_ie, ic->ic_opt_ie_len);
	if (error) {
		if (error != E2BIG) {
			if_printf(ifp, "can't set opt ie\n");
			return error;
		}
	}

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		error = iwl2100_set_bintval(sc, ic->ic_bss->ni_intval);
		if (error) {
			if_printf(ifp, "can't set bintval\n");
			return error;
		}

		error = iwl2100_set_txpower(sc, 32 /* XXX */);
		if (error) {
			if_printf(ifp, "can't set txpwr\n");
			return error;
		}
	}
	return 0;
}

static int
iwl2100_config_op(struct iwl2100_softc *sc, uint32_t op)
{
	struct iwl2100_cmd *cmd;
	int error;

	KASSERT(op == IWL2100_CMD_CONF_DONE || op == IWL2100_CMD_CONF_START,
		("unknown config_op %u", op));

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));
	cmd->c_cmd = op;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s(%u) failed\n", __func__, op);
		return error;
	}

	iwl2100_read_ord1(sc, IWL2100_ORD1_CONF_START); /* dummy read */
	return 0;
}

static int
iwl2100_set_chan(struct iwl2100_softc *sc, const struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_cmd *cmd;
	u_int chan;
	int error;

	KKASSERT(ic->ic_opmode != IEEE80211_M_STA);

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == IEEE80211_CHAN_ANY) {
		if_printf(&ic->ic_if, "invalid channel!\n");
		return EINVAL;
	}

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&ic->ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_CHAN;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = chan;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_scanopt(struct iwl2100_softc *sc, uint32_t chans, uint32_t flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_cmd *cmd;
	int error;

	KKASSERT(ic->ic_opmode != IEEE80211_M_MONITOR);

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&ic->ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	/*
	 * NOTE:
	 * 1) IWL2100_SCANOPT_NOASSOC is ignored by firmware, but same
	 *    function could be achieved by clearing bssid.
	 * 2) Channel mask is ignored by firmware, if NIC is in STA opmode.
	 *
	 * We leave the correct configuration here just with the hope
	 * that one day firmware could do better.
	 */
	cmd->c_cmd = IWL2100_CMD_SET_SCANOPT;
	cmd->c_param_len = sizeof(cmd->c_param[0]) * 2;
	cmd->c_param[0] = flags | IWL2100_SCANOPT_MIXED;
	cmd->c_param[1] = chans;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_scan(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_cmd *cmd;
	int error;

	KKASSERT(ic->ic_opmode != IEEE80211_M_MONITOR);

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&ic->ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SCAN;
	cmd->c_param_len = sizeof(cmd->c_param[0]);

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&ic->ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_optie(struct iwl2100_softc *sc, void *optie, uint16_t optie_len)
{
	struct iwl2100_cmd *cmd;
	struct iwl2100_cmdparam_ie *ie;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	if (optie_len > IWL2100_OPTIE_MAX) {
		if_printf(&sc->sc_ic.ic_if, "optie too long\n");
		return E2BIG;
	}

	if (optie == NULL || optie_len == 0)
		return 0;

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_IE;
	cmd->c_param_len = sizeof(*ie);
	ie = (struct iwl2100_cmdparam_ie *)cmd->c_param;
	ie->ie_optlen = optie_len;
	bcopy(optie, ie->ie_opt, optie_len);

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_bintval(struct iwl2100_softc *sc, uint16_t bintval)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_BINTVAL;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = bintval;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static int
iwl2100_set_txpower(struct iwl2100_softc *sc, uint16_t txpower)
{
	struct iwl2100_cmd *cmd;
	int error;

	if (sc->sc_flags & IWL2100_F_WAITCMD) {
		if_printf(&sc->sc_ic.ic_if, "there is command pending\n");
		return EEXIST;
	}

	cmd = mtod(sc->sc_cmd, struct iwl2100_cmd *);
	bzero(cmd, sizeof(*cmd));

	cmd->c_cmd = IWL2100_CMD_SET_TXPOWER;
	cmd->c_param_len = sizeof(cmd->c_param[0]);
	cmd->c_param[0] = txpower;

	error = iwl2100_wait_cmd(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "%s failed\n", __func__);
		return error;
	}
	return 0;
}

static void
iwl2100_rxeof_status(struct iwl2100_softc *sc, int i)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rx_status *st = &rr->rr_status[i];
	struct iwl2100_rxbuf *rb = &rr->rr_buf[i];
	struct mbuf *m = rb->rb_mbuf;
	uint32_t status;

	if (st->r_len != sizeof(status)) {
		if_printf(ifp, "invalid status frame len %u\n", st->r_len);
		goto back;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		goto back;

	if ((ic->ic_flags & IEEE80211_F_SCAN) == 0)
		sc->sc_flags &= ~IWL2100_F_SCANNING;

	status = *mtod(m, uint32_t *);
	DPRINTF(sc, IWL2100_DBG_STATUS, "status 0x%08x\n", status);

	switch (status) {
	case IWL2100_STATUS_SCANDONE:
		if (ic->ic_flags & IEEE80211_F_SCAN) {
			/*
			 * To make sure that firmware has iterated all
			 * of the channels, we wait for the second scan
			 * done status change.
			 */
			if (sc->sc_flags & IWL2100_F_SCANNING) {
				iwlmsg_send(&sc->sc_scanend_msg,
					    &sc->sc_thread_port);
			} else {
				sc->sc_flags |= IWL2100_F_SCANNING;
			}
		}
		break;

	case IWL2100_STATUS_RUNNING:
		iwl2100_restart_done(sc);
		if (ic->ic_state == IEEE80211_S_ASSOC) {
			KKASSERT(ic->ic_opmode == IEEE80211_M_STA);
			iwlmsg_send(&sc->sc_run_msg, &sc->sc_thread_port);
		} else if (ic->ic_state == IEEE80211_S_RUN) {
			if (ic->ic_opmode == IEEE80211_M_STA) {
				DPRINTF(sc, IWL2100_DBG_RESTART, "%s",
					"restart done\n");
				sc->sc_flags |= IWL2100_F_IFSTART;
				if_devstart(ifp);
			} else {
				KKASSERT(ic->ic_opmode == IEEE80211_M_IBSS);
				callout_reset(&sc->sc_ibss, (100 * hz) / 1000,
					      iwl2100_ibss_bssid, sc);
			}
		}
		break;

	case IWL2100_STATUS_BMISS:
		if (ic->ic_opmode == IEEE80211_M_STA) {
			DPRINTF(sc, IWL2100_DBG_SCAN, "%s", "bmiss\n");
			iwlmsg_send(&sc->sc_bmiss_msg, &sc->sc_thread_port);
		}
		break;

	case IWL2100_STATUS_SCANNING:
		if (ic->ic_opmode == IEEE80211_M_STA &&
		    ic->ic_state == IEEE80211_S_RUN) {
			/* Firmware error happens */
			iwl2100_restart(sc);
		}
		break;
	}
back:
	iwl2100_rxdesc_setup(sc, i);
}

static void
iwl2100_rxeof_note(struct iwl2100_softc *sc, int i)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rx_status *st = &rr->rr_status[i];
	struct iwl2100_rxbuf *rb = &rr->rr_buf[i];
	struct mbuf *m = rb->rb_mbuf;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwl2100_note *note;

	if (st->r_len < sizeof(*note)) {
		if_printf(&ic->ic_if, "invalid note frame len %u\n", st->r_len);
		goto back;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		goto back;

	note = mtod(m, struct iwl2100_note *);
	DPRINTF(sc, IWL2100_DBG_NOTE, "note subtype %u, size %u\n",
		note->nt_subtype, note->nt_size);

	if (note->nt_subtype == 19 /* XXX */ &&
	    ic->ic_state == IEEE80211_S_AUTH) {
		KKASSERT(ic->ic_opmode == IEEE80211_M_STA);
		iwlmsg_send(&sc->sc_assoc_msg, &sc->sc_thread_port);
	}
back:
	iwl2100_rxdesc_setup(sc, i);
}

static void
iwl2100_rxeof_cmd(struct iwl2100_softc *sc, int i)
{
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rx_status *st = &rr->rr_status[i];
	struct iwl2100_rxbuf *rb = &rr->rr_buf[i];
	struct mbuf *m = rb->rb_mbuf;
	struct iwl2100_cmd *cmd;

	if (st->r_len != sizeof(*cmd)) {
		if_printf(&sc->sc_ic.ic_if,
			  "invalid cmd done frame len %u\n", st->r_len);
		goto back;
	}

	cmd = mtod(m, struct iwl2100_cmd *);
	DPRINTF(sc, IWL2100_DBG_CMD, "cmd %u\n", cmd->c_cmd);
	if (cmd->c_cmd == 0)
		sc->sc_flags |= IWL2100_F_ZERO_CMD;
	wakeup(sc);
back:
	iwl2100_rxdesc_setup(sc, i);
}

static void
iwl2100_rxeof_data(struct iwl2100_softc *sc, int i)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwl2100_rx_ring *rr = &sc->sc_rxring;
	struct iwl2100_rx_status *st = &rr->rr_status[i];
	struct iwl2100_rxbuf *rb = &rr->rr_buf[i];
	struct mbuf *m = rb->rb_mbuf;
	struct ieee80211_frame_min *wh;
	struct ieee80211_node *ni;
	int frame_len, rssi;
	const struct ieee80211_channel *c;

	/*
	 * Gather all necessary information from status ring _here_,
	 * since the following iwl2100_newbuf() will clear them out.
	 */
	rssi = st->r_rssi;
	frame_len = st->r_len;

	if (iwl2100_newbuf(sc, i, 0)) {
		IFNET_STAT_INC(ifp, ierrors, 1);
		return;
	}

	c = ic->ic_curchan;

	m->m_pkthdr.rcvif = ifp;
	m->m_len = m->m_pkthdr.len = frame_len;

	wh = mtod(m, struct ieee80211_frame_min *);
	ni = ieee80211_find_rxnode(ic, wh);

	/*
	 * RX radio tap
	 */
	if (sc->sc_drvbpf != NULL) {
		if (wh->i_fc[1] & IEEE80211_FC1_WEP)
			sc->sc_rx_th.wr_flags = IEEE80211_RADIOTAP_F_WEP;
		else
			sc->sc_rx_th.wr_flags = 0;

		sc->sc_rx_th.wr_antsignal = rssi + IWL2100_NOISE_FLOOR;
		sc->sc_rx_th.wr_antnoise = IWL2100_NOISE_FLOOR;

		bpf_ptap(sc->sc_drvbpf, m, &sc->sc_rx_th, sc->sc_rx_th_len);
	}

	ieee80211_input(ic, m, ni, rssi, 0);
	ieee80211_free_node(ni);

	if (c != ic->ic_curchan)	/* Happen during scanning */
		iwl2100_chan_change(sc, ic->ic_curchan);
}

static void
iwl2100_scanend_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto reply;

	if (ifp->if_flags & IFF_RUNNING) {
		ieee80211_end_scan(ic);
		sc->sc_flags &= ~IWL2100_F_SCANNING;
	}
reply:
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static int
iwl2100_hw_init(struct iwl2100_softc *sc, const uint8_t *bssid,
		const uint8_t *essid, uint8_t esslen, uint32_t flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t db_addr;
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);
	KKASSERT(curthread == &sc->sc_thread);

	iwl2100_hw_stop(sc);

	error = iwl2100_alloc_firmware(sc, ic->ic_opmode);
	if (error) {
		if_printf(ifp, "can't allocate firmware\n");
		goto back;
	}

	error = iwl2100_load_firmware(sc, ic->ic_opmode);
	if (error) {
		if_printf(ifp, "can't load firmware\n");
		goto back;
	}

	error = iwl2100_alloc_cmd(sc);
	if (error) {
		if_printf(ifp, "can't allocate cmd\n");
		goto back;
	}

	error = iwl2100_init_tx_ring(sc);
	if (error) {
		if_printf(ifp, "can't init TX ring\n");
		goto back;
	}

	error = iwl2100_init_rx_ring(sc);
	if (error) {
		if_printf(ifp, "can't init RX ring\n");
		goto back;
	}

	error = iwl2100_init_firmware(sc);
	if (error) {
		if_printf(ifp, "can't initialize firmware\n");
		goto back;
	}

	sc->sc_ord1 = CSR_READ_4(sc, IWL2100_ORD1_ADDR);
	sc->sc_ord2 = CSR_READ_4(sc, IWL2100_ORD2_ADDR);

	db_addr = iwl2100_read_ord1(sc, IWL2100_ORD1_DBADDR);
	if ((IND_READ_4(sc, db_addr + 0x20) >> 24) & 0x1)
		sc->sc_caps &= ~IWL2100_C_RFKILL;
	else
		sc->sc_caps |= IWL2100_C_RFKILL;

	/* Unlock firmware */
	iwl2100_write_ord1(sc, IWL2100_ORD1_FWLOCK, 0);

	if (iwl2100_rfkilled(sc)) {
		error = ENXIO;
		goto back;
	}

	/* Let interrupt handler run */
	sc->sc_flags |= IWL2100_F_INITED;

	/* Enable interrupts */
	CSR_WRITE_4(sc, IWL2100_INTR_MASK, IWL2100_INTRS);

	error = iwl2100_config(sc, bssid, essid, esslen,
			       flags & IWL2100_INIT_F_IBSSCHAN);
	if (error)
		goto back;

	if (flags & IWL2100_INIT_F_ENABLE) {
		error = iwl2100_config_done(sc);
		if (error) {
			if_printf(ifp, "can't complete config\n");
			goto back;
		}
	}

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;
back:
	if (error)
		iwl2100_stop(sc);
	return error;
}

static int
iwl2100_start_scan(struct iwl2100_softc *sc, uint32_t chans, uint32_t flags)
{
	int error;

	/*
	 * XXX
	 * Firmware always starts scanning once config is done
	 */
	error = iwl2100_set_scanopt(sc, chans, flags);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't set scan opt\n");
		return error;
	}

	error = iwl2100_set_scan(sc);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "can't set bcast scanning\n");
		return error;
	}
	return 0;
}

static int
iwl2100_scan(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t chans, flags;
	int error;

	KKASSERT(ic->ic_opmode != IEEE80211_M_MONITOR);

	error = iwl2100_hw_init(sc, NULL,
		ic->ic_des_essid, ic->ic_des_esslen, IWL2100_INIT_F_ENABLE);
	if (error)
		return error;

	if (ic->ic_opmode == IEEE80211_M_STA) {
		chans = sc->sc_bss_chans;
		flags = IWL2100_SCANOPT_NOASSOC;
	} else {
		/*
		 * Normally # of IBSS channels is less than BSS's
		 * but it seems IBSS mode works on all BSS channels
		 */
#if 0
		chans = sc->sc_ibss_chans;
#else
		chans = sc->sc_bss_chans;
#endif
		/*
		 * Don't set NOASSOC scan option, it seems that
		 * firmware will disable itself after scanning
		 * if this flag is set.  After all, we are in
		 * IBSS mode, which does not have concept of
		 * association.
		 */
		flags = 0;
	}

	/* See NOTE in iwl2100_set_scanopt() */
	error = iwl2100_start_scan(sc, chans, flags);
	if (error)
		return error;
	return 0;
}

static int
iwl2100_auth(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	u_int chan;
	int error;

	KKASSERT(ic->ic_opmode == IEEE80211_M_STA);

	chan = ieee80211_chan2ieee(ic, ic->ic_curchan);
	if (chan == IEEE80211_CHAN_ANY) {
		if_printf(&ic->ic_if, "invalid curchan\n");
		return EINVAL;
	}

	error = iwl2100_hw_init(sc, ni->ni_bssid,
		ni->ni_essid, ni->ni_esslen, IWL2100_INIT_F_ENABLE);
	if (error)
		return error;

	/* See NOTE in iwl2100_set_scanopt() */
	error = iwl2100_start_scan(sc, 1 << (chan - 1), 0);
	if (error)
		return error;
	return 0;
}

static int
iwl2100_ibss(struct iwl2100_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;

	return iwl2100_hw_init(sc, ni->ni_bssid,
		ni->ni_essid, ni->ni_esslen,
		IWL2100_INIT_F_ENABLE | IWL2100_INIT_F_IBSSCHAN);
}

static int
iwl2100_encap(struct iwl2100_softc *sc, struct mbuf *m)
{
	struct iwl2100_tx_ring *tr = &sc->sc_txring;
	struct iwl2100_tx_hdr *th;
	struct ieee80211_frame *wh;
	struct iwl_dmamap_ctx ctx;
	bus_dma_segment_t segs[IWL2100_NSEG_MAX];
	uint8_t src[IEEE80211_ADDR_LEN], dst[IEEE80211_ADDR_LEN];
	bus_dmamap_t dmap;
	int maxsegs, i, first_idx, last_idx, error, host_enc;

	/*
	 * Save necessary information and strip 802.11 header
	 */
	wh = mtod(m, struct ieee80211_frame *);
	IEEE80211_ADDR_COPY(src, wh->i_addr2);
	if (sc->sc_ic.ic_opmode == IEEE80211_M_STA)
		IEEE80211_ADDR_COPY(dst, wh->i_addr3);
	else
		IEEE80211_ADDR_COPY(dst, wh->i_addr1);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP)
		host_enc = 1;
	else
		host_enc = 0;
	m_adj(m, sizeof(*wh));

	/*
	 * Prepend and setup hardware TX header
	 */
	M_PREPEND(m, sizeof(*th), MB_DONTWAIT);
	if (m == NULL) {
		if_printf(&sc->sc_ic.ic_if, "prepend TX header failed\n");
		return ENOBUFS;
	}
	th = mtod(m, struct iwl2100_tx_hdr *);

	bzero(th, sizeof(*th));
	th->th_cmd = IWL2100_CMD_TX_DATA;
	th->th_host_enc = host_enc;
	IEEE80211_ADDR_COPY(th->th_src, src);
	IEEE80211_ADDR_COPY(th->th_dst, dst);

	/*
	 * Load mbuf into DMA map
	 */
	maxsegs = IWL2100_TX_USED_MAX - tr->tr_used;
	if (maxsegs > IWL2100_NSEG_MAX)
		maxsegs = IWL2100_NSEG_MAX;

	KKASSERT(tr->tr_index < IWL2100_TX_NDESC);
	first_idx = tr->tr_index;
	dmap = tr->tr_buf[first_idx].tb_dmap;

	ctx.nsegs = maxsegs;
	ctx.segs = segs;
	error = bus_dmamap_load_mbuf(sc->sc_mbuf_dtag, dmap, m,
				     iwl_dma_buf_addr, &ctx, BUS_DMA_NOWAIT);
	if (!error && ctx.nsegs == 0) {
		bus_dmamap_unload(sc->sc_mbuf_dtag, dmap);
		error = EFBIG;
	}
	if (error && error != EFBIG) {
		if_printf(&sc->sc_ic.ic_if, "can't load TX mbuf, error %d\n",
			  error);
		goto back;
	}
	if (error) {	/* error == EFBIG */
		struct mbuf *m_new;

		m_new = m_defrag(m, MB_DONTWAIT);
		if (m_new == NULL) {
			if_printf(&sc->sc_ic.ic_if, "can't defrag TX mbuf\n");
			error = ENOBUFS;
			goto back;
		} else {
			m = m_new;
		}

		ctx.nsegs = maxsegs;
		ctx.segs = segs;
		error = bus_dmamap_load_mbuf(sc->sc_mbuf_dtag, dmap, m,
					     iwl_dma_buf_addr, &ctx,
					     BUS_DMA_NOWAIT);
		if (error || ctx.nsegs == 0) {
			if (ctx.nsegs == 0) {
				bus_dmamap_unload(sc->sc_mbuf_dtag, dmap);
				error = EFBIG;
			}
			if_printf(&sc->sc_ic.ic_if,
				  "can't load defraged TX mbuf\n");
			goto back;
		}
	}
	bus_dmamap_sync(sc->sc_mbuf_dtag, dmap, BUS_DMASYNC_PREWRITE);

	/*
	 * Fill TX desc ring
	 */
	last_idx = -1;
	for (i = 0; i < ctx.nsegs; ++i) {
		struct iwl2100_desc *d = &tr->tr_desc[tr->tr_index];

		d->d_paddr = segs[i].ds_addr;
		d->d_len = segs[i].ds_len;
		if (i != 0)
			d->d_nfrag = 0;
		else
			d->d_nfrag = ctx.nsegs;

		if (i == ctx.nsegs - 1) {
			d->d_flags = IWL2100_TXD_F_INTR;
			last_idx = tr->tr_index;
		} else {
			d->d_flags = IWL2100_TXD_F_NOTLAST;
		}

		tr->tr_index = (tr->tr_index + 1) % IWL2100_TX_NDESC;
	}
	KKASSERT(last_idx >= 0);

	tr->tr_buf[first_idx].tb_dmap = tr->tr_buf[last_idx].tb_dmap;
	tr->tr_buf[last_idx].tb_dmap = dmap;
	tr->tr_buf[last_idx].tb_mbuf = m;

	tr->tr_used += ctx.nsegs;
	KKASSERT(tr->tr_used <= IWL2100_TX_USED_MAX);

	error = 0;
back:
	if (error)
		m_freem(m);
	return error;
}

static void
iwl2100_restart_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto reply;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto reply;

	if (msg->iwlm_arg != sc->sc_state_age) {
		/*
		 * Restarting was triggered in old 802.11 state
		 * Don't do anything, this is a staled restarting.
		 */
		goto reply;
	}

	if (ic->ic_state != IEEE80211_S_RUN) {
		if_printf(ifp, "restart happened when not in RUN state\n");
		goto reply;
	}

	/*
	 * iwl2100_auth() may release slizer, so stop all
	 * callouts to prevent them from misfiring.
	 */
	callout_stop(&sc->sc_restart_bmiss);
	callout_stop(&sc->sc_ibss);

	if (ic->ic_opmode == IEEE80211_M_STA) {
		error = iwl2100_auth(sc);
		if (error)
			goto reply;

		/*
		 * Start software beacon missing to handle missing
		 * firmware bmiss status change when we restarting
		 */
		callout_reset(&sc->sc_restart_bmiss, IEEE80211_TU_TO_TICKS(
			2 * ic->ic_bmissthreshold * ic->ic_bss->ni_intval),
			iwl2100_restart_bmiss, sc);
	} else if (ic->ic_opmode == IEEE80211_M_IBSS) {
		error = iwl2100_ibss(sc);
		if (error)
			goto reply;
	}

	/* Turn on restarting flag before reply this message */
	sc->sc_flags |= IWL2100_F_RESTARTING;
reply:
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static void
iwl2100_restart(struct iwl2100_softc *sc)
{
	if ((sc->sc_flags & (IWL2100_F_RESTARTING | IWL2100_F_DETACH)) == 0) {
		struct iwlmsg *msg = &sc->sc_restart_msg;
		struct lwkt_msg *lmsg = &msg->iwlm_nmsg.nm_lmsg;

		DPRINTF(sc, IWL2100_DBG_RESTART, "%s", "restart\n");
		if (lmsg->ms_flags & MSGF_DONE) {
			sc->sc_flags &= ~IWL2100_F_IFSTART;
			msg->iwlm_arg = sc->sc_state_age;
			lwkt_sendmsg(&sc->sc_thread_port, lmsg);
		}
	}
}

static void
iwl2100_bmiss_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto reply;

	if (ifp->if_flags & IFF_RUNNING) {
		/*
		 * Fake a ic_bmiss_count to make sure that
		 * ieee80211_beacon_miss() will do its job
		 */
		ic->ic_bmiss_count = ic->ic_bmiss_max;
		ieee80211_beacon_miss(ic);
	}
reply:
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static void
iwl2100_restart_bmiss(void *xsc)
{
	struct iwl2100_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto back;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto back;

	if (sc->sc_flags & IWL2100_F_RESTARTING) {
		DPRINTF(sc, IWL2100_DBG_SCAN | IWL2100_DBG_RESTART, "%s",
			"restart bmiss\n");
		iwlmsg_send(&sc->sc_bmiss_msg, &sc->sc_thread_port);
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
iwl2100_ibss_bssid(void *xsc)
{
	struct iwl2100_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	char ethstr[ETHER_ADDRSTRLEN + 1];

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->sc_flags & IWL2100_F_DETACH)
		goto back;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto back;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    ic->ic_opmode == IEEE80211_M_IBSS) {
		uint8_t bssid[IEEE80211_ADDR_LEN];
		int len;

		len = iwl2100_read_ord2(sc, IWL2100_ORD2_BSSID,
					bssid, sizeof(bssid));
		if (len < (int)sizeof(bssid)) {
			if_printf(ifp, "can't get IBSS bssid\n");
		} else {
			DPRINTF(sc, IWL2100_DBG_IBSS, "IBSS bssid: %s\n",
			    kether_ntoa(bssid, ethstr));
			IEEE80211_ADDR_COPY(ic->ic_bss->ni_bssid, bssid);

			sc->sc_flags |= IWL2100_F_IFSTART;
			if_devstart(ifp);
		}
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
iwl2100_reinit(struct iwl2100_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	callout_stop(&sc->sc_restart_bmiss);
	callout_stop(&sc->sc_ibss);

	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;

	sc->sc_flags &= ~IWL2100_F_INITED;
	sc->sc_tx_timer = 0;

	/* Mark error happened, and wake up the pending command */
	sc->sc_flags |= IWL2100_F_ERROR;
	wakeup(sc);

	if ((sc->sc_flags & IWL2100_F_DETACH) == 0) {
		/*
		 * Schedule complete initialization,
		 * i.e. blow away current state
		 */
		iwlmsg_send(&sc->sc_reinit_msg, &sc->sc_thread_port);
	}
}

static void
iwl2100_reinit_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwl2100_softc *sc = msg->iwlm_softc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * NOTE: Reply ASAP, so reinit msg could be used if error intr
	 * happened again during following iwl2100_init()
	 */
	lwkt_replymsg(&nmsg->nm_lmsg, 0);

	if (sc->sc_flags & IWL2100_F_DETACH)
		return;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		iwl2100_init(sc);
}

static void
iwl2100_reinit_callout(void *xsc)
{
	struct iwl2100_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);
	if ((sc->sc_flags & IWL2100_F_DETACH) == 0)
		iwl2100_reinit(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
iwl2100_chan_change(struct iwl2100_softc *sc, const struct ieee80211_channel *c)
{
	sc->sc_tx_th.wt_chan_freq = sc->sc_rx_th.wr_chan_freq =
		htole16(c->ic_freq);
}

static void
iwl2100_stop_callouts(struct iwl2100_softc *sc)
{
	callout_stop(&sc->sc_restart_bmiss);
	callout_stop(&sc->sc_ibss);
	callout_stop(&sc->sc_reinit);
}
