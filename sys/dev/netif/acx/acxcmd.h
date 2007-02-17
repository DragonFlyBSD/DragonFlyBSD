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
 * $DragonFly: src/sys/dev/netif/acx/acxcmd.h,v 1.6 2007/02/17 07:05:53 sephe Exp $
 */

#ifndef _ACXCMD_H
#define _ACXCMD_H

#include "_acxcmd.h"

void	acx_init_cmd_reg(struct acx_softc *);

int	acx_enable_txchan(struct acx_softc *, uint8_t);
int	acx_enable_rxchan(struct acx_softc *, uint8_t);
int	acx_init_radio(struct acx_softc *, uint32_t, uint32_t);
int	acx_join_bss(struct acx_softc *, uint8_t,
		     struct ieee80211_node *, struct ieee80211_channel *);

/*
 * Possible values for the second parameter of acx_join_bss()
 */
#define ACX_MODE_ADHOC	0
#define ACX_MODE_UNUSED	1
#define ACX_MODE_STA	2
#define ACX_MODE_AP	3

/*
 * Do not use following functions directly
 */
int	acx_get_conf(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_set_conf(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_set_tmplt(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_exec_command(struct acx_softc *, uint16_t, void *, uint16_t,
			 void *, uint16_t);


/*
 * NOTE:
 * Following structs' fields are little endian
 */

struct acx_conf {
	uint16_t	conf_id;	/* see ACXCONF_ (_acxcmd.h) */
	uint16_t	conf_data_len;
} __packed;

struct acx_conf_mmap {
	struct acx_conf	confcom;
	uint32_t	code_start;
	uint32_t	code_end;
	uint32_t	wep_cache_start;
	uint32_t	wep_cache_end;
	uint32_t	pkt_tmplt_start;
	uint32_t	pkt_tmplt_end;
	uint32_t	fw_desc_start;
	uint32_t	fw_desc_end;
	uint32_t	memblk_start;
	uint32_t	memblk_end;
} __packed;

struct acx_conf_wepopt {
	struct acx_conf	confcom;
	uint16_t	nkey;
	uint8_t		opt;	/* see WEPOPT_ */
} __packed;

#define WEPOPT_HDWEP	0	/* hardware WEP */

struct acx_conf_eaddr {
	struct acx_conf	confcom;
	uint8_t		eaddr[IEEE80211_ADDR_LEN];
} __packed;

struct acx_conf_regdom {
	struct acx_conf	confcom;
	uint8_t		regdom;
	uint8_t		unknown;
} __packed;

struct acx_conf_antenna {
	struct acx_conf	confcom;
	uint8_t		antenna;
} __packed;

struct acx_conf_fwrev {
	struct acx_conf	confcom;
#define ACX_FWREV_LEN	20
	/*
	 * "Rev xx.xx.xx.xx"
	 * '\0' terminated
	 */
	char		fw_rev[ACX_FWREV_LEN];
	uint32_t	hw_id;
} __packed;

struct acx_conf_nretry_long {
	struct acx_conf	confcom;
	uint8_t		nretry;
} __packed;

struct acx_conf_nretry_short {
	struct acx_conf	confcom;
	uint8_t		nretry;
} __packed;

struct acx_conf_msdu_lifetime {
	struct acx_conf	confcom;
	uint32_t	lifetime;
} __packed;

struct acx_conf_rate_fallback {
	struct acx_conf	confcom;
	uint8_t		ratefb_enable;	/* 0/1 */
} __packed;

struct acx_conf_rxopt {
	struct acx_conf	confcom;
	uint16_t	opt1;	/* see RXOPT1_ */
	uint16_t	opt2;	/* see RXOPT2_ */
} __packed;

#define RXOPT1_INCL_RXBUF_HDR	0x2000	/* rxbuf with acx_rxbuf_hdr */
#define RXOPT1_RECV_SSID	0x0400	/* recv frame for joined SSID */
#define RXOPT1_FILT_BCAST	0x0200	/* filt broadcast pkt */
#define RXOPT1_RECV_MCAST1	0x0100	/* recv pkt for multicast addr1 */
#define RXOPT1_RECV_MCAST0	0x0080	/* recv pkt for multicast addr0 */
#define RXOPT1_FILT_ALLMULTI	0x0040	/* filt allmulti pkt */
#define RXOPT1_FILT_FSSID	0x0020	/* filt frame for foreign SSID */
#define RXOPT1_FILT_FDEST	0x0010	/* filt frame for foreign dest addr */
#define RXOPT1_PROMISC		0x0008	/* promisc mode */
#define RXOPT1_INCL_FCS		0x0004
#define RXOPT1_INCL_PHYHDR	0x0000	/* XXX 0x0002 */

#define RXOPT2_RECV_ASSOC_REQ	0x0800
#define RXOPT2_RECV_AUTH	0x0400
#define RXOPT2_RECV_BEACON	0x0200
#define RXOPT2_RECV_CF		0x0100
#define RXOPT2_RECV_CTRL	0x0080
#define RXOPT2_RECV_DATA	0x0040
#define RXOPT2_RECV_BROKEN	0x0020	/* broken frame */
#define RXOPT2_RECV_MGMT	0x0010
#define RXOPT2_RECV_PROBE_REQ	0x0008
#define RXOPT2_RECV_PROBE_RESP	0x0004
#define RXOPT2_RECV_ACK		0x0002	/* RTS/CTS/ACK */
#define RXOPT2_RECV_OTHER	0x0001

struct acx_conf_wep_txkey {
	struct acx_conf	confcom;
	uint8_t		wep_txkey;
} __packed;


struct acx_tmplt_null_data {
	uint16_t	size;
	struct ieee80211_frame data;
} __packed;

struct acx_tmplt_probe_req {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x44];
	}		data;
} __packed;

#define ACX_TMPLT_PROBE_REQ_SIZ(var_len)	\
	(sizeof(uint16_t) + sizeof(struct ieee80211_frame) + (var_len))

struct acx_tmplt_probe_resp {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		time_stamp[8];
			uint16_t	beacon_intvl;
			uint16_t	cap;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x100];
	}		data;
} __packed;

/* XXX same as acx_tmplt_probe_resp */
struct acx_tmplt_beacon {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		time_stamp[8];
			uint16_t	beacon_intvl;
			uint16_t	cap;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x100];
	}		data;
} __packed;

struct acx_tmplt_tim {
	uint16_t	size;
	union {
		struct ieee80211_tim_ie	u_tim;
		uint8_t			u_mem[0x100];
	}		data;
} __packed;

#define ACX_INIT_TMPLT_FUNC(name)			\
static __inline int					\
acx_init_##name##_tmplt(struct acx_softc *_sc)		\
{							\
	struct acx_tmplt_##name _tmplt;			\
							\
	bzero(&_tmplt, sizeof(_tmplt));			\
	return acx_set_tmplt(_sc, ACXCMD_TMPLT_##name,	\
			     &_tmplt, sizeof(_tmplt));	\
}							\
struct __hack

#define ACX_SET_TMPLT_FUNC(name)			\
static __inline int					\
_acx_set_##name##_tmplt(struct acx_softc *_sc,		\
		       struct acx_tmplt_##name *_tmplt,	\
		       uint16_t _tmplt_len)		\
{							\
	return acx_set_tmplt(_sc, ACXCMD_TMPLT_##name,	\
			     _tmplt, _tmplt_len);	\
}							\
struct __hack

#define _ACX_CONF_FUNC(sg, name, chip)			\
static __inline int					\
acx##chip##_##sg##_##name##_conf(struct acx_softc *_sc,	\
	struct acx##chip##_conf_##name *_conf)		\
{							\
	return acx_##sg##_conf(_sc, ACX_CONF_##name,	\
			       _conf, sizeof(*_conf));	\
}							\
struct __hack

#define ACX_NOARG_FUNC(name)				\
static __inline int					\
acx_##name(struct acx_softc *_sc)			\
{							\
	return acx_exec_command(_sc, ACXCMD_##name,	\
				NULL, 0, NULL, 0);	\
}							\
struct __hack


#define ACXCMD_TMPLT_tim	ACXCMD_TMPLT_TIM
#define ACXCMD_TMPLT_beacon	ACXCMD_TMPLT_BEACON
#define ACXCMD_TMPLT_probe_resp	ACXCMD_TMPLT_PROBE_RESP
#define ACXCMD_TMPLT_null_data	ACXCMD_TMPLT_NULL_DATA
#define ACXCMD_TMPLT_probe_req	ACXCMD_TMPLT_PROBE_REQ
ACX_INIT_TMPLT_FUNC(tim);
ACX_INIT_TMPLT_FUNC(null_data);
ACX_INIT_TMPLT_FUNC(beacon);
ACX_INIT_TMPLT_FUNC(probe_req);
ACX_INIT_TMPLT_FUNC(probe_resp);
ACX_SET_TMPLT_FUNC(tim);
ACX_SET_TMPLT_FUNC(null_data);
ACX_SET_TMPLT_FUNC(beacon);
ACX_SET_TMPLT_FUNC(probe_req);
ACX_SET_TMPLT_FUNC(probe_resp);

#define ACX_CONF_FUNC(sg, name)	_ACX_CONF_FUNC(sg, name,)
#define ACX_CONF_wepopt		ACX_CONF_WEPOPT
#define ACX_CONF_mmap		ACX_CONF_MMAP
#define ACX_CONF_eaddr		ACX_CONF_EADDR
#define ACX_CONF_regdom		ACX_CONF_REGDOM
#define ACX_CONF_antenna	ACX_CONF_ANTENNA
#define ACX_CONF_fwrev		ACX_CONF_FWREV
#define ACX_CONF_nretry_long	ACX_CONF_NRETRY_LONG
#define ACX_CONF_nretry_short	ACX_CONF_NRETRY_SHORT
#define ACX_CONF_msdu_lifetime	ACX_CONF_MSDU_LIFETIME
#define ACX_CONF_rate_fallback	ACX_CONF_RATE_FALLBACK
#define ACX_CONF_rxopt		ACX_CONF_RXOPT
#define ACX_CONF_wep_txkey	ACX_CONF_WEP_TXKEY
ACX_CONF_FUNC(get, mmap);
ACX_CONF_FUNC(set, mmap);
ACX_CONF_FUNC(set, wepopt);
ACX_CONF_FUNC(get, eaddr);
ACX_CONF_FUNC(get, regdom);
ACX_CONF_FUNC(set, regdom);
ACX_CONF_FUNC(get, antenna);
ACX_CONF_FUNC(set, antenna);
ACX_CONF_FUNC(get, fwrev);
ACX_CONF_FUNC(set, nretry_long);
ACX_CONF_FUNC(set, nretry_short);
ACX_CONF_FUNC(set, msdu_lifetime);
ACX_CONF_FUNC(set, rate_fallback);
ACX_CONF_FUNC(set, rxopt);
ACX_CONF_FUNC(set, wep_txkey);

#define ACXCMD_sleep		ACXCMD_SLEEP
#define ACXCMD_wakeup		ACXCMD_WAKEUP
ACX_NOARG_FUNC(sleep);
ACX_NOARG_FUNC(wakeup);

#endif	/* !_ACXCMD_H */
