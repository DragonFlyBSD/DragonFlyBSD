/*
 * Copyright (c) 2005
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
 *
 * $FreeBSD: src/sys/dev/ral/rt2661var.h,v 1.1 2006/03/05 20:36:56 damien Exp $
 * $DragonFly: src/sys/dev/netif/ral/rt2661var.h,v 1.9 2008/01/16 12:31:25 sephe Exp $
 */

#define RT2661_NCHAN_MAX	38
#define RT2661_KEY_MAX		64

struct rt2661_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsf;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antsignal;
} __packed;

#define RT2661_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_TSFT) |				\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL))

struct rt2661_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RT2661_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct rt2661_tx_ratectl {
	struct ieee80211_node	*ni;
	int			len;
	int			rateidx;
	STAILQ_ENTRY(rt2661_tx_ratectl)	link;
};

struct rt2661_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

struct rt2661_tx_ring {
	bus_dma_tag_t		desc_dmat;
	bus_dma_tag_t		data_dmat;
	bus_dmamap_t		desc_map;
	bus_addr_t		physaddr;
	struct rt2661_tx_desc	*desc;
	struct rt2661_data	*data;
	int			count;
	int			queued;
	int			cur;
	int			next;
};

struct rt2661_rx_ring {
	bus_dma_tag_t		desc_dmat;
	bus_dma_tag_t		data_dmat;
	bus_dmamap_t		desc_map;
	bus_addr_t		physaddr;
	struct rt2661_rx_desc	*desc;
	struct rt2661_data	*data;
	int			count;
	int			cur;
	int			next;
};

struct rt2661_softc {
	struct ieee80211com		sc_ic;
	bus_space_tag_t			sc_st;
	bus_space_handle_t		sc_sh;
	device_t			sc_dev;

	int				sc_irq_rid;
	struct resource			*sc_irq;
	void				*sc_ih;

	int				(*sc_newstate)
					(struct ieee80211com *,
					 enum ieee80211_state, int);

	int				(*sc_key_alloc)
					(struct ieee80211com *,
					 const struct ieee80211_key *,
					 ieee80211_keyix *, ieee80211_keyix *);

	int				(*sc_key_delete)
					(struct ieee80211com *,
					 const struct ieee80211_key *);

	int				(*sc_key_set)
					(struct ieee80211com *,
					 const struct ieee80211_key *,
					 const uint8_t[IEEE80211_ADDR_LEN]);

	struct callout			scan_ch;

	int				sc_tx_timer;
	int				sc_sifs;

	struct ieee80211_channel	*sc_curchan;

	uint8_t				rf_rev;

	uint8_t				rfprog;
	uint8_t				rffreq;

	struct rt2661_tx_ring		txq[4];
	struct rt2661_tx_ring		mgtq;
	struct rt2661_rx_ring		rxq;

	uint32_t			rf_regs[4];
	int8_t				txpow[RT2661_NCHAN_MAX];

	struct {
		uint8_t	reg;
		uint8_t	val;
	}				bbp_prom[16];

	int				hw_radio;
	int				rx_ant;
	int				tx_ant;
	int				nb_ant;

	int				ext_2ghz_lna;
	int				rssi_2ghz_corr[2];
	int				avg_rssi[2];

	int				ext_5ghz_lna;
	int				rssi_5ghz_corr;

	uint8_t				bbp18;
	uint8_t				bbp21;
	uint8_t				bbp22;
	uint8_t				bbp16;
	uint8_t				bbp17;
	uint8_t				bbp64;
	uint16_t			mcu_led;

	STAILQ_HEAD(, rt2661_tx_ratectl) tx_ratectl;

	int				dwelltime;

	struct bpf_if			*sc_drvbpf;

	union {
		struct rt2661_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct rt2661_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;

	struct sysctl_ctx_list		sysctl_ctx;
	struct sysctl_oid		*sysctl_tree;

	struct ieee80211_onoe_param	sc_onoe_param;
	struct ieee80211_sample_param	sc_sample_param;

	uint32_t			sc_keymap[2];
};

#define RT2661_KEY_ASSERT(keyix) \
	KASSERT((keyix) < RT2661_KEY_MAX, ("invalid keyix %d\n", (keyix)))

#define RT2661_KEY_SET(sc, keyix) \
do { \
	RT2661_KEY_ASSERT((keyix)); \
	(sc)->sc_keymap[(keyix) / 32] |= (1 << ((keyix) % 32)); \
} while (0)

#define RT2661_KEY_CLR(sc, keyix) \
do { \
	RT2661_KEY_ASSERT((keyix)); \
	(sc)->sc_keymap[(keyix) / 32] &= ~(1 << ((keyix) % 32)); \
} while (0)

#define RT2661_KEY_ISSET(sc, keyix) \
	((sc)->sc_keymap[(keyix) / 32] & (1 << ((keyix) % 32)))

#define RT2661_RESET_AVG_RSSI(sc) \
do { \
	(sc)->avg_rssi[0] = -1; \
	(sc)->avg_rssi[1] = -1; \
} while (0)

int	rt2661_attach(device_t, int);
int	rt2661_detach(void *);
void	rt2661_shutdown(void *);
void	rt2661_suspend(void *);
void	rt2661_resume(void *);
