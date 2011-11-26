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
 * $DragonFly: src/sys/dev/netif/acx/acxcmd.c,v 1.11 2008/06/01 03:58:38 sephe Exp $
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/wlan_ratectl/amrr/ieee80211_amrr_param.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#define ACX_DEBUG

#include <dev/netif/acx/if_acxreg.h>
#include <dev/netif/acx/if_acxvar.h>
#include <dev/netif/acx/acxcmd.h>

#define CMDPRM_WRITE_REGION_1(sc, r, rlen)		\
	bus_space_write_region_1((sc)->sc_mem2_bt,	\
				 (sc)->sc_mem2_bh,	\
				 (sc)->sc_cmd_param,	\
				 (const uint8_t *)(r), (rlen))

#define CMDPRM_READ_REGION_1(sc, r, rlen)				\
	bus_space_read_region_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
				(sc)->sc_cmd_param, (uint8_t *)(r), (rlen))

/*
 * This will clear previous command's
 * execution status too
 */
#define CMD_WRITE_4(sc, val)					\
	bus_space_write_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
			  (sc)->sc_cmd, (val))
#define CMD_READ_4(sc)		\
	bus_space_read_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, (sc)->sc_cmd)

/*
 * acx command register layerout:
 * upper 16bits are command execution status
 * lower 16bits are command to be executed
 */
#define ACX_CMD_STATUS_SHIFT	16
#define ACX_CMD_STATUS_OK	1

struct radio_init {
	uint32_t	radio_ofs;	/* radio firmware offset */
	uint32_t	radio_len;	/* radio firmware length */
} __packed;

struct bss_join_hdr {
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint16_t	beacon_intvl;
	uint8_t		chip_spec[3];
	uint8_t		ndata_txrate;	/* see ACX_NDATA_TXRATE_ */
	uint8_t		ndata_txopt;	/* see ACX_NDATA_TXOPT_ */
	uint8_t		mode;		/* see ACX_MODE_ */
	uint8_t		channel;
	uint8_t		esslen;
	char		essid[1];
} __packed;

/*
 * non-data frame tx rate
 */
#define ACX_NDATA_TXRATE_1		10	/* 1Mbits/s */
#define ACX_NDATA_TXRATE_2		20	/* 2Mbits/s */

/*
 * non-data frame tx options
 */
#define ACX_NDATA_TXOPT_PBCC		0x40
#define ACX_NDATA_TXOPT_OFDM		0x20
#define ACX_NDATA_TXOPT_SHORT_PREAMBLE	0x10

#define BSS_JOIN_BUFLEN		\
	(sizeof(struct bss_join_hdr) + IEEE80211_NWID_LEN)
#define BSS_JOIN_PARAM_SIZE(bj)	\
	(sizeof(struct bss_join_hdr) + (bj)->esslen)

void
acx_init_cmd_reg(struct acx_softc *sc)
{
	sc->sc_cmd = CSR_READ_4(sc, ACXREG_CMD_REG_OFFSET);
	sc->sc_cmd_param = sc->sc_cmd + ACX_CMD_REG_SIZE;

	/* Clear command & status */
	CMD_WRITE_4(sc, 0);
}

int
acx_join_bss(struct acx_softc *sc, uint8_t mode, struct ieee80211_node *ni,
	     struct ieee80211_channel *c)
{
	uint8_t bj_buf[BSS_JOIN_BUFLEN];
	struct ieee80211com *ic = &sc->sc_ic;
	struct bss_join_hdr *bj;
	int i;

	bzero(bj_buf, sizeof(bj_buf));
	bj = (struct bss_join_hdr *)bj_buf;

	for (i = 0; i < IEEE80211_ADDR_LEN; ++i)
		bj->bssid[i] = ni->ni_bssid[IEEE80211_ADDR_LEN - i - 1];

	bj->beacon_intvl = htole16(ni->ni_intval);

	sc->chip_set_bss_join_param(sc, bj->chip_spec, ic->ic_dtim_period);

	bj->ndata_txrate = ACX_NDATA_TXRATE_1;
	bj->ndata_txopt = 0;
	bj->mode = mode;
	bj->channel = ieee80211_chan2ieee(ic, c);
	bj->esslen = ni->ni_esslen;
	bcopy(ni->ni_essid, bj->essid, ni->ni_esslen);

	DPRINTF((&ic->ic_if, "join BSS/IBSS on channel %d\n", bj->channel));
	return acx_exec_command(sc, ACXCMD_JOIN_BSS,
				bj, BSS_JOIN_PARAM_SIZE(bj), NULL, 0);
}

int
acx_enable_txchan(struct acx_softc *sc, uint8_t chan)
{
	return acx_exec_command(sc, ACXCMD_ENABLE_TXCHAN, &chan, sizeof(chan),
				NULL, 0);
}

int
acx_enable_rxchan(struct acx_softc *sc, uint8_t chan)
{
	return acx_exec_command(sc, ACXCMD_ENABLE_RXCHAN, &chan, sizeof(chan),
				NULL, 0);
}

int
acx_get_conf(struct acx_softc *sc, uint16_t conf_id, void *conf,
	     uint16_t conf_len)
{
	struct acx_conf *confcom;

	if (conf_len < sizeof(*confcom)) {
		if_printf(&sc->sc_ic.ic_if, "%s configure data is too short\n",
			  __func__);
		return 1;
	}

	confcom = conf;
	confcom->conf_id = htole16(conf_id);
	confcom->conf_data_len = htole16(conf_len - sizeof(*confcom));

	return acx_exec_command(sc, ACXCMD_GET_CONF, confcom, sizeof(*confcom),
				conf, conf_len);
}

int
acx_set_conf(struct acx_softc *sc, uint16_t conf_id, void *conf,
	     uint16_t conf_len)
{
	struct acx_conf *confcom;

	if (conf_len < sizeof(*confcom)) {
		if_printf(&sc->sc_ic.ic_if, "%s configure data is too short\n",
			  __func__);
		return 1;
	}

	confcom = conf;
	confcom->conf_id = htole16(conf_id);
	confcom->conf_data_len = htole16(conf_len - sizeof(*confcom));

	return acx_exec_command(sc, ACXCMD_SET_CONF, conf, conf_len, NULL, 0);
}

int
acx_set_tmplt(struct acx_softc *sc, uint16_t cmd, void *tmplt,
	      uint16_t tmplt_len)
{
	uint16_t *size;

	if (tmplt_len < sizeof(*size)) {
		if_printf(&sc->sc_ic.ic_if, "%s template is too short\n",
			  __func__);
		return 1;
	}

	size = tmplt;
	*size = htole16(tmplt_len - sizeof(*size));

	return acx_exec_command(sc, cmd, tmplt, tmplt_len, NULL, 0);
}

int
acx_init_radio(struct acx_softc *sc, uint32_t radio_ofs, uint32_t radio_len)
{
	struct radio_init r;

	r.radio_ofs = htole32(radio_ofs);
	r.radio_len = htole32(radio_len);
	return acx_exec_command(sc, ACXCMD_INIT_RADIO, &r, sizeof(r), NULL, 0);
}

int
acx_exec_command(struct acx_softc *sc, uint16_t cmd, void *param,
		 uint16_t param_len, void *result, uint16_t result_len)
{
	uint16_t status;
	int i, ret;

	ASSERT_SERIALIZED(sc->sc_ic.ic_if.if_serializer);

	if ((sc->sc_flags & ACX_FLAG_FW_LOADED) == 0) {
		if_printf(&sc->sc_ic.ic_if, "cmd 0x%04x failed (base firmware "
			  "not loaded)", cmd);
		return 1;
	}

	ret = 0;

	if (param != NULL && param_len != 0) {
		/* Set command param */
		CMDPRM_WRITE_REGION_1(sc, param, param_len);
	}

	/* Set command */
	CMD_WRITE_4(sc, cmd);

	/* Exec command */
	CSR_WRITE_2(sc, ACXREG_INTR_TRIG, ACXRV_TRIG_CMD_FINI);
	DELAY(50);	/* XXX maybe 100 */

	/* Wait for command to complete */
	if (cmd == ACXCMD_INIT_RADIO) {
		/* XXX radio initialization is extremely long */
		tsleep(&cmd, 0, "rdinit", (150 * hz) / 1000);	/* 150ms */
	}

#define CMDWAIT_RETRY_MAX	1000
	for (i = 0; i < CMDWAIT_RETRY_MAX; ++i) {
		uint16_t reg;

		reg = CSR_READ_2(sc, ACXREG_INTR_STATUS);
		if (reg & ACXRV_INTR_CMD_FINI) {
			CSR_WRITE_2(sc, ACXREG_INTR_ACK, ACXRV_INTR_CMD_FINI);
			break;
		}
		DELAY(50);
	}
	if (i == CMDWAIT_RETRY_MAX) {
		if_printf(&sc->sc_ic.ic_if, "cmd %04x failed (timeout)\n", cmd);
		ret = 1;
		goto back;
	}
#undef CMDWAIT_RETRY_MAX

	/* Get command exec status */
	status = (CMD_READ_4(sc) >> ACX_CMD_STATUS_SHIFT);
	if (status != ACX_CMD_STATUS_OK) {
		if_printf(&sc->sc_ic.ic_if, "cmd %04x failed\n", cmd);
		ret = 1;
		goto back;
	}

	if (result != NULL && result_len != 0) {
		/* Get command result */
		CMDPRM_READ_REGION_1(sc, result, result_len);
	}

back:
	CMD_WRITE_4(sc, 0);
	return ret;
}
