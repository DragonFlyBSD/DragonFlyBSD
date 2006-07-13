/*
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/ath/if_athioctl.h,v 1.10.2.3 2006/02/24 19:51:11 sam Exp $
 * $DragonFly: src/sys/dev/netif/ath/ath/if_athioctl.h,v 1.1 2006/07/13 09:15:22 sephe Exp $
 */

/*
 * Ioctl-related defintions for the Atheros Wireless LAN controller driver.
 */
#ifndef _DEV_ATH_ATHIOCTL_H
#define _DEV_ATH_ATHIOCTL_H

struct ath_stats {
	uint32_t	ast_watchdog;	/* device reset by watchdog */
	uint32_t	ast_hardware;	/* fatal hardware error interrupts */
	uint32_t	ast_bmiss;	/* beacon miss interrupts */
	uint32_t	ast_bmiss_phantom;/* beacon miss interrupts */
	uint32_t	ast_bstuck;	/* beacon stuck interrupts */
	uint32_t	ast_rxorn;	/* rx overrun interrupts */
	uint32_t	ast_rxeol;	/* rx eol interrupts */
	uint32_t	ast_txurn;	/* tx underrun interrupts */
	uint32_t	ast_mib;	/* mib interrupts */
	uint32_t	ast_intrcoal;	/* interrupts coalesced */
	uint32_t	ast_tx_packets;	/* packet sent on the interface */
	uint32_t	ast_tx_mgmt;	/* management frames transmitted */
	uint32_t	ast_tx_discard;	/* frames discarded prior to assoc */
	uint32_t	ast_tx_qstop;	/* output stopped 'cuz no buffer */
	uint32_t	ast_tx_encap;	/* tx encapsulation failed */
	uint32_t	ast_tx_nonode;	/* tx failed 'cuz no node */
	uint32_t	ast_tx_nombuf;	/* tx failed 'cuz no mbuf */
	uint32_t	ast_tx_nomcl;	/* tx failed 'cuz no cluster */
	uint32_t	ast_tx_linear;	/* tx linearized to cluster */
	uint32_t	ast_tx_nodata;	/* tx discarded empty frame */
	uint32_t	ast_tx_busdma;	/* tx failed for dma resrcs */
	uint32_t	ast_tx_xretries;/* tx failed 'cuz too many retries */
	uint32_t	ast_tx_fifoerr;	/* tx failed 'cuz FIFO underrun */
	uint32_t	ast_tx_filtered;/* tx failed 'cuz xmit filtered */
	uint32_t	ast_tx_shortretry;/* tx on-chip retries (short) */
	uint32_t	ast_tx_longretry;/* tx on-chip retries (long) */
	uint32_t	ast_tx_badrate;	/* tx failed 'cuz bogus xmit rate */
	uint32_t	ast_tx_noack;	/* tx frames with no ack marked */
	uint32_t	ast_tx_rts;	/* tx frames with rts enabled */
	uint32_t	ast_tx_cts;	/* tx frames with cts enabled */
	uint32_t	ast_tx_shortpre;/* tx frames with short preamble */
	uint32_t	ast_tx_altrate;	/* tx frames with alternate rate */
	uint32_t	ast_tx_protect;	/* tx frames with protection */
	uint32_t	ast_unused1;
	uint32_t	ast_unused2;
	uint32_t	ast_rx_nombuf;	/* rx setup failed 'cuz no mbuf */
	uint32_t	ast_rx_busdma;	/* rx setup failed for dma resrcs */
	uint32_t	ast_rx_orn;	/* rx failed 'cuz of desc overrun */
	uint32_t	ast_rx_crcerr;	/* rx failed 'cuz of bad CRC */
	uint32_t	ast_rx_fifoerr;	/* rx failed 'cuz of FIFO overrun */
	uint32_t	ast_rx_badcrypt;/* rx failed 'cuz decryption */
	uint32_t	ast_rx_badmic;	/* rx failed 'cuz MIC failure */
	uint32_t	ast_rx_phyerr;	/* rx failed 'cuz of PHY err */
	uint32_t	ast_rx_phy[32];	/* rx PHY error per-code counts */
	uint32_t	ast_rx_tooshort;/* rx discarded 'cuz frame too short */
	uint32_t	ast_rx_toobig;	/* rx discarded 'cuz frame too large */
	uint32_t	ast_rx_packets;	/* packet recv on the interface */
	uint32_t	ast_rx_mgt;	/* management frames received */
	uint32_t	ast_rx_ctl;	/* rx discarded 'cuz ctl frame */
	int8_t		ast_tx_rssi;	/* tx rssi of last ack */
	int8_t		ast_rx_rssi;	/* rx rssi from histogram */
	uint32_t	ast_be_xmit;	/* beacons transmitted */
	uint32_t	ast_be_nombuf;	/* beacon setup failed 'cuz no mbuf */
	uint32_t	ast_per_cal;	/* periodic calibration calls */
	uint32_t	ast_per_calfail;/* periodic calibration failed */
	uint32_t	ast_per_rfgain;	/* periodic calibration rfgain reset */
	uint32_t	ast_rate_calls;	/* rate control checks */
	uint32_t	ast_rate_raise;	/* rate control raised xmit rate */
	uint32_t	ast_rate_drop;	/* rate control dropped xmit rate */
	uint32_t	ast_ant_defswitch;/* rx/default antenna switches */
	uint32_t	ast_ant_txswitch;/* tx antenna switches */
	uint32_t	ast_ant_rx[8];	/* rx frames with antenna */
	uint32_t	ast_ant_tx[8];	/* tx frames with antenna */
	uint32_t	ast_pad[32];
};

#define	SIOCGATHSTATS	_IOWR('i', 137, struct ifreq)

struct ath_diag {
	char	ad_name[IFNAMSIZ];	/* if name, e.g. "ath0" */
	uint16_t ad_id;
#define	ATH_DIAG_DYN	0x8000		/* allocate buffer in caller */
#define	ATH_DIAG_IN	0x4000		/* copy in parameters */
#define	ATH_DIAG_OUT	0x0000		/* copy out results (always) */
#define	ATH_DIAG_ID	0x0fff
	uint16_t ad_in_size;		/* pack to fit, yech */
	caddr_t	ad_in_data;
	caddr_t	ad_out_data;
	u_int	ad_out_size;

};
#define	SIOCGATHDIAG	_IOWR('i', 138, struct ath_diag)

/*
 * Radio capture format.
 */
#define ATH_RX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_TSFT)		| \
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)	| \
	0)

struct ath_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsf;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antsignal;
	uint8_t		wr_antnoise;
	uint8_t		wr_antenna;
};

#define ATH_TX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_TSFT)		| \
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_TX_POWER)	| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	0)

struct ath_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint64_t	wt_tsf;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
	uint8_t		wt_txpower;
	uint8_t		wt_antenna;
};

#endif /* _DEV_ATH_ATHIOCTL_H */
