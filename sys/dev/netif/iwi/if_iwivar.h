/*
 * Copyright (c) 2004, 2005
 *      Damien Bergamini <damien.bergamini@free.fr>.
 * Copyright (c) 2004, 2005
 *	Andrew Atrens <atrens@nortelnetworks.com>.
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
 * $DragonFly: src/sys/dev/netif/iwi/if_iwivar.h,v 1.1 2005/03/06 05:02:02 dillon Exp $
 */

struct iwi_firmware {
	void	*boot;
	int	boot_size;
	void	*ucode;
	int	ucode_size;
	void	*main;
	int	main_size;
};

struct iwi_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	u_int8_t	wr_flags;
	u_int8_t	wr_rate;
	u_int16_t	wr_chan_freq;
	u_int16_t	wr_chan_flags;
	u_int8_t	wr_antsignal;
	u_int8_t	wr_antnoise;
	u_int8_t	wr_antenna;
};

#define IWI_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DB_ANTNOISE) |			\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct iwi_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	u_int8_t	wt_flags;
	u_int16_t	wt_chan_freq;
	u_int16_t	wt_chan_flags;
};

#define IWI_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct iwi_softc {
	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	device_t		sc_dev;


	struct iwi_firmware	fw;
	u_int32_t		flags;
#define IWI_FLAG_FW_CACHED	(1 << 0)
#define IWI_FLAG_FW_IBSS	(1 << 1)
#define IWI_FLAG_FW_INITED	(1 << 2)
#define IWI_FLAG_SCANNING       (1 << 3)
#define IWI_FLAG_SCAN_COMPLETE  (1 << 4)
#define IWI_FLAG_SCAN_ABORT     (1 << 5)
#define IWI_FLAG_ASSOCIATED     (1 << 6)
#define IWI_FLAG_RF_DISABLED    (1 << 7)
#define IWI_FLAG_RESET          (1 << 8)
#define IWI_FLAG_EXIT           (1 << 9)

	struct iwi_tx_desc	*tx_desc;
	bus_dma_tag_t		iwi_parent_tag;
	bus_dma_tag_t		tx_ring_dmat;
	bus_dmamap_t		tx_ring_map;
	bus_addr_t		tx_ring_pa;
	bus_dma_tag_t		tx_buf_dmat;

	struct iwi_tx_buf {
		bus_dmamap_t		map;
		struct mbuf		*m;
		struct ieee80211_node	*ni;
	} tx_buf[IWI_TX_RING_SIZE];

	int			tx_cur;
	int			tx_old;
	int			tx_queued;

	struct iwi_cmd_desc	*cmd_desc;
	bus_dma_tag_t		cmd_ring_dmat;
	bus_dmamap_t		cmd_ring_map;
	bus_addr_t		cmd_ring_pa;
	int			cmd_cur;

	bus_dma_tag_t		rx_buf_dmat;

	struct iwi_rx_buf {
		bus_dmamap_t	map;
		bus_addr_t	physaddr;
		struct mbuf	*m;
	} rx_buf[IWI_RX_RING_SIZE];

	int			rx_cur;

	struct resource		*irq;
	struct resource		*mem;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	void 			*sc_ih;

	int			authmode;

	int			sc_tx_timer;

#if NBPFILTER > 0
	struct bpf_if		*sc_drvbpf;

	union {
		struct iwi_rx_radiotap_header th;
		u_int8_t	pad[64];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct iwi_tx_radiotap_header th;
		u_int8_t	pad[64];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
#endif
	int 			num_stations;
	u_int8_t                stations[IWI_FW_MAX_STATIONS][ETHER_ADDR_LEN];

	struct lwkt_token       sc_lock;
	struct lwkt_token       sc_intrlock;

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;

	int			debug_level;

	int			enable_bg_autodetect;
	int			enable_bt_coexist;
	int			enable_cts_to_self;
	int			antenna_diversity; /* 1 = A, 3 = B, 0 = A + B */
	int			enable_neg_best_first;
	int			disable_unicast_decryption;
	int			disable_multicast_decryption;

	struct thread		*event_thread;

	struct iwi_associate	assoc;

	int			scan_counter;

};

#define SIOCSLOADFW	 _IOW('i', 137, struct ifreq)
#define SIOCSLOADIBSSFW	 _IOW('i', 138, struct ifreq)
#define SIOCSKILLFW	 _IOW('i', 139, struct ifreq)

#define IWI_LOCK_INIT(tok)     lwkt_token_init(tok)
#define IWI_LOCK_DESTROY(tok)  lwkt_token_uninit(tok)

#define IWI_LOCK_INFO          struct lwkt_tokref tokinfo
#define IWI_INTRLOCK_INFO      struct lwkt_tokref intrtokinfo
#define IWI_INTRLOCK(_sc)      lwkt_gettoken(&intrtokinfo,(&(_sc)->sc_intrlock))
#define IWI_INTRUNLOCK(SC)     lwkt_reltoken(&intrtokinfo)
#define IWI_LOCK(_sc)          lwkt_gettoken(&tokinfo,&((_sc)->sc_lock))
#define IWI_UNLOCK(SC)         lwkt_reltoken(&tokinfo)

/*
 * Holding a token is not enough for iwi_tx_start() the DMA send
 * routine. Revert back to the old ipl mechanism for now.
 */

#define IWI_IPLLOCK_INFO       int saved_ipl_level
#define IWI_IPLLOCK(_sc)       saved_ipl_level =  splimp()
#define IWI_IPLUNLOCK(_sc)     splx(saved_ipl_level)

/* tsleepable events */
#define IWI_FW_WAKE_MONITOR(sc)      (sc + 1)
#define IWI_FW_INITIALIZED(sc)       (sc + 2)
#define IWI_FW_CMD_ACKED(sc)         (sc + 3)
#define IWI_FW_SCAN_COMPLETED(sc)    (sc + 4)
#define IWI_FW_DEASSOCIATED(sc)      (sc + 5)
#define IWI_FW_MON_EXIT(sc)          (sc + 6)

