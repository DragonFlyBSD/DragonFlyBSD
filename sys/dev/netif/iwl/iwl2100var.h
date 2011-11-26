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
 * 
 * $DragonFly: src/sys/dev/netif/iwl/iwl2100var.h,v 1.2 2008/03/08 06:43:52 sephe Exp $
 */

#ifndef _IWL2100VAR_H
#define _IWL2100VAR_H

#define IWL2100_DEBUG

#define IWL2100_NSEG_MAX	6
#define IWL2100_TX_NDESC	256
#define IWL2100_RX_NDESC	256

#define IWL2100_SPARE_NDESC	6
#define IWL2100_TX_USED_MAX	(IWL2100_TX_NDESC -  IWL2100_SPARE_NDESC)

#define IWL2100_TXRING_SIZE	(IWL2100_TX_NDESC * sizeof(struct iwl2100_desc))
#define IWL2100_RXRING_SIZE	(IWL2100_RX_NDESC * sizeof(struct iwl2100_desc))
#define IWL2100_RXSTATUS_SIZE	\
	(IWL2100_RX_NDESC * sizeof(struct iwl2100_rx_status))

#ifndef IWL2100_DEBUG
#define DPRINTF(sc, flags, fmt, ...)	((void)0)
#else
#define DPRINTF(sc, flags, fmt, ...) \
do { \
	if ((sc)->sc_debug & (flags)) \
		if_printf(&(sc)->sc_ic.ic_if, fmt, __VA_ARGS__); \
} while (0)
#endif	/* !IWL2100_DEBUG */

struct iwl2100_desc {
	uint32_t	d_paddr;
	uint32_t	d_len;
	uint8_t		d_flags;	/* IWL2100_TXD_F_ */
	uint8_t		d_nfrag;
	uint8_t		d_reserved[6];
} __packed;

#define IWL2100_TXD_F_NOTLAST	0x1
#define IWL2100_TXD_F_CMD	0x2
#define IWL2100_TXD_F_INTR	0x8

struct iwl2100_tx_hdr {
	uint32_t	th_cmd;
	uint32_t	th_cmd1;
	uint8_t		th_host_enc;
	uint8_t		th_enc;
	uint8_t		th_keyidx;
	uint8_t		th_keysz;
	uint8_t		th_key[IEEE80211_KEYBUF_SIZE];
	uint8_t		th_reserved[10];
	uint8_t		th_src[IEEE80211_ADDR_LEN];
	uint8_t		th_dst[IEEE80211_ADDR_LEN];
	uint16_t	th_frag_size;
} __packed;

struct iwl2100_rx_status {
	uint32_t	r_len;
	uint16_t	r_status;	/* IWL2100_RXS_ */
	uint8_t		r_flags;
	uint8_t		r_rssi;
} __packed;

#define IWL2100_RXS_TYPE_MASK	0xf
#define IWL2100_RXS_TYPE_CMD	0
#define IWL2100_RXS_TYPE_STATUS	1
#define IWL2100_RXS_TYPE_DATA	2
#define IWL2100_RXS_TYPE_DATA1	3
#define IWL2100_RXS_TYPE_NOTE	4

#define IWL2100_STATUS_RUNNING	(1 << 2)
#define IWL2100_STATUS_BMISS	(1 << 3)
#define IWL2100_STATUS_SCANDONE	(1 << 5)
#define IWL2100_STATUS_SCANNING	(1 << 11)

struct iwl2100_note {
	uint32_t	nt_subtype;
	uint32_t	nt_size;
} __packed;

#define IWL2100_CMD_PARAMSZ	100

struct iwl2100_cmd {
	uint32_t	c_cmd;
	uint32_t	c_cmd1;
	uint32_t	c_seq;
	uint32_t	c_param_len;
	uint32_t	c_param[IWL2100_CMD_PARAMSZ];
	uint32_t	c_status;
	uint32_t	c_unused[17];
} __packed;

struct iwl2100_cmdparam_sec {
	uint32_t	sec_cipher_mask;	/* IWL2100_CIPHER_ */
	uint16_t	sec_ver;
	uint8_t		sec_authmode;		/* IWL2100_AUTH_ */
	uint8_t		sec_replay_counter;
	uint8_t		sec_unused;
} __packed;

#define IWL2100_CIPHER_NONE	(1 << 0)
#define IWL2100_CIPHER_WEP40	(1 << 1)
#define IWL2100_CIPHER_TKIP	(1 << 2)
#define IWL2100_CIPHER_CCMP	(1 << 4)
#define IWL2100_CIPHER_WEP104	(1 << 5)

#define IWL2100_AUTH_OPEN	0
#define IWL2100_AUTH_SHARED	1

#define IWL2100_KEYDATA_SIZE	13

struct iwl2100_cmdparam_wepkey {
	uint8_t		key_index;
	uint8_t		key_len;
	uint8_t		key_data[IWL2100_KEYDATA_SIZE];
} __packed;

/* 16: ie_fixed_mask + ie_fixed + ie_optlen */
#define IWL2100_OPTIE_MAX	((IWL2100_CMD_PARAMSZ * sizeof(uint32_t)) - 16)

struct iwl2100_cmdparam_ie {
	uint16_t	ie_fixed_mask;
	struct {
		uint16_t	cap_info;
		uint16_t	lintval;
		uint8_t		bssid[IEEE80211_ADDR_LEN];
	} ie_fixed;
	uint32_t	ie_optlen;
	uint8_t		ie_opt[IWL2100_OPTIE_MAX];
} __packed;

struct iwl2100_ucode_resp {
	uint8_t		cmd_id;
	uint8_t		seq_no;
	uint8_t		ucode_rev;
	uint8_t		eeprom_valid;
	uint16_t	valid_flags;
	uint8_t		addr[6];
	uint16_t	flags;
	uint16_t	pcb_rev;
	uint16_t	clk_settle_time; 
	uint16_t	pwr_settle_time;
	uint16_t	hop_settle_time;
	uint8_t 	date_time[5];
	uint8_t		ucode_valid;
} __packed;

struct iwl2100_fwdata_hdr {
	uint32_t	addr;
	uint16_t	len;
	uint8_t		data[1];
} __packed;

struct iwl2100_fwimg_hdr {
	uint16_t	version;
	uint16_t	mode;		/* IWL2100_FW_M_ */
	uint32_t	data_size;
	uint32_t	ucode_size;
} __packed;

#define IWL2100_FW_M_STA	0
#define IWL2100_FW_M_IBSS	1
#define IWL2100_FW_M_MONITOR	2

struct iwl2100_txbuf {
	struct mbuf	*tb_mbuf;
	bus_dmamap_t	tb_dmap;
	uint32_t	tb_flags;	/* IWL2100_TBF_ */
};

#define IWL2100_TBF_CMDBUF	0x1

struct iwl2100_rxbuf {
	struct mbuf	*rb_mbuf;
	bus_dmamap_t	rb_dmap;
	bus_addr_t	rb_paddr;
};

struct iwl2100_tx_ring {
	bus_dma_tag_t		tr_dtag;
	bus_dmamap_t		tr_dmap;
	bus_addr_t		tr_paddr;
	struct iwl2100_desc	*tr_desc;

	int			tr_used;
	int			tr_index;
	int			tr_coll;

	struct iwl2100_txbuf	tr_buf[IWL2100_TX_NDESC];
};

struct iwl2100_rx_ring {
	bus_dma_tag_t		rr_dtag;
	bus_dmamap_t		rr_dmap;
	bus_addr_t		rr_paddr;
	struct iwl2100_desc	*rr_desc;

	bus_dma_tag_t		rr_st_dtag;
	bus_dmamap_t		rr_st_dmap;
	bus_addr_t		rr_st_paddr;
	struct iwl2100_rx_status *rr_status;

	int			rr_index;

	bus_dmamap_t		rr_tmp_dmap;
	struct iwl2100_rxbuf	rr_buf[IWL2100_RX_NDESC];
};

struct fw_image;

struct iwl2100_firmware {
	struct fw_image	*fw_image;
	const uint8_t	*fw_data;
	int		fw_data_size;
	const uint8_t	*fw_ucode;
	int		fw_ucode_size;
};

#define IWL2100_TX_RADIOTAP_PRESENT 		\
	((1 << IEEE80211_RADIOTAP_FLAGS) |	\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct iwl2100_tx_radiotap_hdr {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
};

#define IWL2100_RX_RADIOTAP_PRESENT			\
	((1 << IEEE80211_RADIOTAP_FLAGS) |		\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |		\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |	\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE))

struct iwl2100_rx_radiotap_hdr {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_antsignal;
	int8_t		wr_antnoise;
};

struct iwl2100_softc {
	struct iwlcom		iwlcom;

	uint32_t		sc_ord1;
	uint32_t		sc_ord2;
	uint32_t		sc_caps;	/* IWL2100_C_ */
	uint32_t		sc_flags;	/* IWL2100_F_ */
	int			sc_state_age;

	uint16_t		sc_ibss_chans;
	uint16_t		sc_bss_chans;

	bus_dma_tag_t		sc_dtag;
	bus_dma_tag_t		sc_mbuf_dtag;
	struct iwl2100_tx_ring	sc_txring;
	struct iwl2100_rx_ring	sc_rxring;

	struct iwl2100_firmware	sc_fw_sta;
	struct iwl2100_firmware	sc_fw_ibss;
	struct iwl2100_firmware	sc_fw_monitor;

	struct mbuf		*sc_cmd;

	struct callout		sc_restart_bmiss;
	struct callout		sc_ibss;
	struct callout		sc_reinit;

	struct iwlmsg		sc_scanend_msg;
	struct iwlmsg		sc_assoc_msg;
	struct iwlmsg		sc_run_msg;
	struct iwlmsg		sc_restart_msg;
	struct iwlmsg		sc_bmiss_msg;
	struct iwlmsg		sc_reinit_msg;

	struct bpf_if		*sc_drvbpf;

	union {
		struct iwl2100_tx_radiotap_hdr u_tx_th;
		uint8_t		u_pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_u_tx_th;
	int			sc_tx_th_len;

	union {
		struct iwl2100_rx_radiotap_hdr u_rx_th;
		uint8_t		u_pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_u_rx_th;
	int			sc_rx_th_len;

	int			(*sc_newstate)
				(struct ieee80211com *,
				 enum ieee80211_state, int);

	/*
	 * Sysctl variables
	 */
	uint32_t		sc_debug;	/* IWL2100_DBG_ */
};

#define IWL2100_C_RFKILL	0x1

#define IWL2100_F_WAITCMD	0x1
#define IWL2100_F_INITED	0x2
#define IWL2100_F_IN_INTR	0x4	/* for sanity check */
#define IWL2100_F_SCANNING	0x8
#define IWL2100_F_RESTARTING	0x10
#define IWL2100_F_IFSTART	0x20	/* if_start could run */
#define IWL2100_F_ERROR		0x40
#define IWL2100_F_ZERO_CMD	0x80
#define IWL2100_F_DETACH	0x100	/* detaching */

#define IWL2100_DBG_IBSS	0x01
#define IWL2100_DBG_SCAN	0x02
#define IWL2100_DBG_STATUS	0x04
#define IWL2100_DBG_RESTART	0x08
#define IWL2100_DBG_NOTE	0x10
#define IWL2100_DBG_CMD		0x20

#define IWL2100_PCIR_BAR	PCIR_BAR(0)
#define IWL2100_DESC		"Intel PRO/Wireless LAN 2100"
#define IWL2100_FW_PATH		IWL_FW_PATH "2100/1.3/ipw2100-1.3%s.fw"

int	iwl2100_attach(device_t);
void	iwl2100_detach(device_t);
int	iwl2100_shutdown(device_t);

#endif	/* _IWL2100VAR_H */
