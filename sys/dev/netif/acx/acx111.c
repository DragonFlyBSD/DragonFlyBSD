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
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
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

#include <bus/pci/pcireg.h>

#define ACX_DEBUG

#include <dev/netif/acx/if_acxreg.h>
#include <dev/netif/acx/if_acxvar.h>
#include <dev/netif/acx/acxcmd.h>

#define ACX111_CONF_MEM		0x0003
#define ACX111_CONF_MEMINFO	0x0005
#define ACX111_CONF_RT0_NRETRY	0x0006

#define ACX111_INTR_ENABLE	(ACXRV_INTR_TX_FINI | ACXRV_INTR_RX_FINI)
/*
 * XXX do we really care about fowlling interrupts?
 *
 * ACXRV_INTR_IV_ICV_FAILURE | ACXRV_INTR_INFO |
 * ACXRV_INTR_SCAN_FINI | ACXRV_INTR_FCS_THRESHOLD
 */

#define ACX111_INTR_DISABLE	(uint16_t)~(ACXRV_INTR_CMD_FINI)

#define ACX111_RATE_2		0x0001
#define ACX111_RATE_4		0x0002
#define ACX111_RATE_11		0x0004
#define ACX111_RATE_12		0x0008
#define ACX111_RATE_18		0x0010
#define ACX111_RATE_22		0x0020
#define ACX111_RATE_24		0x0040
#define ACX111_RATE_36		0x0080
#define ACX111_RATE_44		0x0100
#define ACX111_RATE_48		0x0200
#define ACX111_RATE_72		0x0400
#define ACX111_RATE_96		0x0800
#define ACX111_RATE_108		0x1000
#define ACX111_RATE(rate)	[rate] = ACX111_RATE_##rate

#define ACX111_RSSI_CORR	5
#define ACX111_TXPOWER		15
#define ACX111_GPIO_POWER_LED	0x0040
#define ACX111_EE_EADDR_OFS	0x21

#define ACX111_FW_TXDESC_SIZE	(sizeof(struct acx_fw_txdesc) + 4)

#if ACX111_TXPOWER <= 12
#define ACX111_TXPOWER_VAL	1
#else
#define ACX111_TXPOWER_VAL	2
#endif

#define ACX111_ONOE_RATEIDX_MAX		4
#define ACX111_AMRR_RATEIDX_MAX		4

/*
 * NOTE:
 * Following structs' fields are little endian
 */

struct acx111_bss_join {
	uint16_t	basic_rates;
	uint8_t		dtim_intvl;
} __packed;

struct acx111_calib {
	uint32_t	calib;		/* ACX111_CALIB_ */
	uint32_t	interval;	/* TU */
} __packed;

#define ACX111_CALIB_AUTO		0x80000000
#define ACX111_CALIB_DC			0x00000001
#define ACX111_CALIB_AFE_DC		0x00000002
#define ACX111_CALIB_TX_MISMATCH	0x00000004
#define ACX111_CALIB_TX_EQUAL		0x00000008

#define ACX111_FW_CALIB_INTVL		IEEE80211_MS_TO_TU(60000) /* 60sec */

struct acx111_conf_mem {
	struct acx_conf	confcom;

	uint16_t	sta_max;	/* max num of sta, ACX111_STA_MAX */
	uint16_t	memblk_size;	/* mem block size */
	uint8_t		rx_memblk_perc;	/* percent of RX mem block, unit: 5% */
	uint8_t		fw_rxring_num;	/* num of RX ring */
	uint8_t		fw_txring_num;	/* num of TX ring */
	uint8_t		opt;		/* see ACX111_MEMOPT_ */
	uint8_t		xfer_perc;	/* frag/xfer proportion, unit: 5% */
	uint16_t	reserved0;
	uint8_t		reserved1;

	uint8_t		fw_rxdesc_num;	/* num of fw rx desc */
	uint8_t		fw_rxring_reserved1;
	uint8_t		fw_rxring_type;	/* see ACX111_RXRING_TYPE_ */
	uint8_t		fw_rxring_prio;	/* see ACX111_RXRING_PRIO_ */

	uint32_t	h_rxring_paddr; /* host rx desc start phyaddr */

	uint8_t		fw_txdesc_num;	/* num of fw tx desc */
	uint8_t		fw_txring_reserved1;
	uint8_t		fw_txring_reserved2;
	uint8_t		fw_txring_attr;	/* see ACX111_TXRING_ATTR_ */
} __packed;

/*
 * ACX111 does support limited multi-rate retry, following rules apply to
 * at least firmware rev1.2.x.x:
 * 1) Rate field in firmware descriptor is a bitmask, which indicates
 *    set of rates to be used to send the packet.
 * 2) "acx111_conf_rt0_nretry" configures the number of retries for
 *    1st rate.
 * 3) Except for the last rate and 1st rate, rest of the rates in the
 *    rate set are tried only once.
 * 4) Last rate will be tried until "short retry limit" + "long retry limit"
 *    reaches.
 *
 * e.g.
 * a) 54Mbit/s, 48Mbit/s and 1Mbit/s are in the rate set.
 * b) Number of retries for the 1st rate (i.e. 54Mbit/s) is set to 3.
 * c) Short retry limit is set to 7
 *
 * For the above configuration:
 * A) 4 tries will be spent at 54Mbit/s.
 * B) 1 try will be spent at 48Mbit/s, if A) fails.
 * C) 3 tries will be spent at 1Mbit/s, if A) and B) fail.
 */
struct acx111_conf_rt0_nretry {
	struct acx_conf	confcom;
	uint8_t		rt0_nretry;	/* number of retry for 1st rate */
} __packed;

#define ACX111_STA_MAX			32
#define ACX111_RX_MEMBLK_PERCENT	10	/* 50% */
#define ACX111_XFER_PERCENT		15	/* 75% */
#define ACX111_RXRING_TYPE_DEFAULT	7
#define ACX111_RXRING_PRIO_DEFAULT	0
#define ACX111_TXRING_ATTR_DEFAULT	0
#define ACX111_MEMOPT_DEFAULT		0

struct acx111_conf_meminfo {
	struct acx_conf	confcom;
	uint32_t	tx_memblk_addr;	/* start addr of tx mem blocks */
	uint32_t	rx_memblk_addr;	/* start addr of rx mem blocks */
	uint32_t	fw_rxring_start; /* start phyaddr of fw rx ring */
	uint32_t	reserved0;
	uint32_t	fw_txring_start; /* start phyaddr of fw tx ring */
	uint8_t		fw_txring_attr;	/* XXX see ACX111_TXRING_ATTR_ */
	uint16_t	reserved1;
	uint8_t		reserved2;
} __packed;

struct acx111_conf_txpower {
	struct acx_conf	confcom;
	uint8_t		txpower;
} __packed;

struct acx111_conf_option {
	struct acx_conf	confcom;
	uint32_t	feature;
	uint32_t	dataflow;	/* see ACX111_DF_ */
} __packed;

#define ACX111_DF_NO_RXDECRYPT	0x00000080
#define ACX111_DF_NO_TXENCRYPT	0x00000001

struct acx111_wepkey {
	uint8_t		mac_addr[IEEE80211_ADDR_LEN];
	uint16_t	action;		/* see ACX111_WEPKEY_ACT_ */
	uint16_t	reserved;
	uint8_t		key_len;
	uint8_t		key_type;	/* see ACX111_WEPKEY_TYPE_ */
	uint8_t		index;		/* XXX ?? */
	uint8_t		key_idx;
	uint8_t		counter[6];
#define ACX111_WEPKEY_LEN	32
	uint8_t		key[ACX111_WEPKEY_LEN];
} __packed;

#define ACX111_WEPKEY_ACT_ADD		1
#define ACX111_WEPKEY_TYPE_DEFAULT	0

#define ACX111_CONF_FUNC(sg, name)	_ACX_CONF_FUNC(sg, name, 111)
#define ACX_CONF_mem			ACX111_CONF_MEM
#define ACX_CONF_meminfo		ACX111_CONF_MEMINFO
#define ACX_CONF_rt0_nretry		ACX111_CONF_RT0_NRETRY
#define ACX_CONF_txpower		ACX_CONF_TXPOWER
#define ACX_CONF_option			ACX_CONF_OPTION
ACX111_CONF_FUNC(set, mem);
ACX111_CONF_FUNC(get, meminfo);
ACX111_CONF_FUNC(set, txpower);
ACX111_CONF_FUNC(get, option);
ACX111_CONF_FUNC(set, option);
ACX111_CONF_FUNC(set, rt0_nretry);

static const uint16_t acx111_reg[ACXREG_MAX] = {
	ACXREG(SOFT_RESET,		0x0000),

	ACXREG(FWMEM_ADDR,		0x0014),
	ACXREG(FWMEM_DATA,		0x0018),
	ACXREG(FWMEM_CTRL,		0x001c),
	ACXREG(FWMEM_START,		0x0020),

	ACXREG(EVENT_MASK,		0x0034),

	ACXREG(INTR_TRIG,		0x00b4),
	ACXREG(INTR_MASK,		0x00d4),
	ACXREG(INTR_STATUS,		0x00f0),
	ACXREG(INTR_STATUS_CLR,		0x00e4),
	ACXREG(INTR_ACK,		0x00e8),

	ACXREG(HINTR_TRIG,		0x00ec),
	ACXREG(RADIO_ENABLE,		0x01d0),

	ACXREG(EEPROM_INIT,		0x0100),
	ACXREG(EEPROM_CTRL,		0x0338),
	ACXREG(EEPROM_ADDR,		0x033c),
	ACXREG(EEPROM_DATA,		0x0340),
	ACXREG(EEPROM_CONF,		0x0344),
	ACXREG(EEPROM_INFO,		0x0390),

	ACXREG(PHY_ADDR,		0x0350),
	ACXREG(PHY_DATA,		0x0354),
	ACXREG(PHY_CTRL,		0x0358),

	ACXREG(GPIO_OUT_ENABLE,		0x0374),
	ACXREG(GPIO_OUT,		0x037c),

	ACXREG(CMD_REG_OFFSET,		0x0388),
	ACXREG(INFO_REG_OFFSET,		0x038c),

	ACXREG(RESET_SENSE,		0x0104),
	ACXREG(ECPU_CTRL,		0x0108) 
};

static const uint16_t	acx111_rate_map[109] = {
	ACX111_RATE(2),
	ACX111_RATE(4),
	ACX111_RATE(11),
	ACX111_RATE(22),
	ACX111_RATE(12),
	ACX111_RATE(18),
	ACX111_RATE(24),
	ACX111_RATE(36),
	ACX111_RATE(44),
	ACX111_RATE(48),
	ACX111_RATE(72),
	ACX111_RATE(96),
	ACX111_RATE(108)
};

static const int
acx111_onoe_tries[IEEE80211_RATEIDX_MAX] = { 4, 1, 1, 3, 0 };

static const int
acx111_amrr_tries[IEEE80211_RATEIDX_MAX] = { 4, 1, 1, 3, 0 };

static int	acx111_init(struct acx_softc *);
static int	acx111_init_memory(struct acx_softc *);
static void	acx111_init_fw_txring(struct acx_softc *, uint32_t);

static int	acx111_write_config(struct acx_softc *, struct acx_config *);

static void	acx111_set_bss_join_param(struct acx_softc *, void *, int);
static int	acx111_calibrate(struct acx_softc *);

static void	*acx111_ratectl_attach(struct ieee80211com *, u_int);

static uint8_t	_acx111_set_fw_txdesc_rate(struct acx_softc *,
					   struct acx_txbuf *,
					   struct ieee80211_node *, int, int);
static uint8_t	acx111_set_fw_txdesc_rate_onoe(struct acx_softc *,
					       struct acx_txbuf *,
					       struct ieee80211_node *, int);
static uint8_t	acx111_set_fw_txdesc_rate_amrr(struct acx_softc *,
					       struct acx_txbuf *,
					       struct ieee80211_node *, int);

static void	_acx111_tx_complete(struct acx_softc *, struct acx_txbuf *,
				    int, int, const int[]);
static void	acx111_tx_complete_onoe(struct acx_softc *, struct acx_txbuf *,
					int, int);
static void	acx111_tx_complete_amrr(struct acx_softc *, struct acx_txbuf *,
					int, int);

#define ACX111_CHK_RATE(ifp, rate, rate_idx)	\
	acx111_check_rate(ifp, rate, rate_idx, __func__)

static __inline int
acx111_check_rate(struct ifnet *ifp, u_int rate, int rate_idx,
		  const char *fname)
{
	if (rate >= NELEM(acx111_rate_map)) {
		if_printf(ifp, "%s rate out of range %u (idx %d)\n",
			  fname, rate, rate_idx);
		return -1;
	}

	if (acx111_rate_map[rate] == 0) {
		if_printf(ifp, "%s invalid rate %u (idx %d)\n",
			  fname, rate, rate_idx);
		return -1;
	}
	return 0;
}

void
acx111_set_param(device_t dev)
{
	struct acx_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	struct acx_firmware *fw = &sc->sc_firmware;

	sc->chip_mem1_rid = PCIR_BAR(0);
	sc->chip_mem2_rid = PCIR_BAR(1);
	sc->chip_ioreg = acx111_reg;
	sc->chip_intr_enable = ACX111_INTR_ENABLE;
	sc->chip_intr_disable = ACX111_INTR_DISABLE;
	sc->chip_gpio_pled = ACX111_GPIO_POWER_LED;
	sc->chip_ee_eaddr_ofs = ACX111_EE_EADDR_OFS;
	sc->chip_rssi_corr = ACX111_RSSI_CORR;
	sc->chip_calibrate = acx111_calibrate;

	sc->chip_phymode = IEEE80211_MODE_11G;
	sc->chip_chan_flags = IEEE80211_CHAN_CCK |
			      IEEE80211_CHAN_OFDM |
			      IEEE80211_CHAN_DYN |
			      IEEE80211_CHAN_2GHZ;

	ic->ic_caps = IEEE80211_C_WPA | IEEE80211_C_SHSLOT;
	ic->ic_phytype = IEEE80211_T_OFDM;
	if (acx_enable_pbcc) {
		ic->ic_sup_rates[IEEE80211_MODE_11B] = acx_rates_11b_pbcc;
		ic->ic_sup_rates[IEEE80211_MODE_11G] = acx_rates_11g_pbcc;
	} else {
		ic->ic_sup_rates[IEEE80211_MODE_11B] = acx_rates_11b;
		ic->ic_sup_rates[IEEE80211_MODE_11G] = acx_rates_11g;
	}

	IEEE80211_ONOE_PARAM_SETUP(&sc->sc_onoe_param);
	IEEE80211_AMRR_PARAM_SETUP(&sc->sc_amrr_param);

	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE |
					   IEEE80211_RATECTL_CAP_AMRR;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_AMRR;
	ic->ic_ratectl.rc_st_attach = acx111_ratectl_attach;

	sc->chip_init = acx111_init;
	sc->chip_write_config = acx111_write_config;
	sc->chip_set_bss_join_param = acx111_set_bss_join_param;

	fw->combined_radio_fw = 1;
	fw->fwdir = "111";
}

static int
acx111_init(struct acx_softc *sc)
{
	/*
	 * NOTE:
	 * Order of initialization:
	 * 1) Templates
	 * 2) Hardware memory
	 * Above order is critical to get a correct memory map
	 */

	if (acx_init_tmplt_ordered(sc) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't initialize templates\n",
			  __func__);
		return ENXIO;
	}

	if (acx111_init_memory(sc) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't initialize hw memory\n",
			  __func__);
		return ENXIO;
	}
	return 0;
}

static int
acx111_init_memory(struct acx_softc *sc)
{
	struct acx111_conf_mem mem;
	struct acx111_conf_meminfo mem_info;

	/* Set memory configuration */
	bzero(&mem, sizeof(mem));

	mem.sta_max = htole16(ACX111_STA_MAX);
	mem.memblk_size = htole16(ACX_MEMBLOCK_SIZE);
	mem.rx_memblk_perc = ACX111_RX_MEMBLK_PERCENT;
	mem.opt = ACX111_MEMOPT_DEFAULT;
	mem.xfer_perc = ACX111_XFER_PERCENT;

	mem.fw_rxring_num = 1;
	mem.fw_rxring_type = ACX111_RXRING_TYPE_DEFAULT;
	mem.fw_rxring_prio = ACX111_RXRING_PRIO_DEFAULT;
	mem.fw_rxdesc_num = ACX_RX_DESC_CNT;
	mem.h_rxring_paddr = htole32(sc->sc_ring_data.rx_ring_paddr);

	mem.fw_txring_num = 1;
	mem.fw_txring_attr = ACX111_TXRING_ATTR_DEFAULT;
	mem.fw_txdesc_num = ACX_TX_DESC_CNT;

	if (acx111_set_mem_conf(sc, &mem) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't set mem\n");
		return 1;
	}

	/* Get memory configuration */
	if (acx111_get_meminfo_conf(sc, &mem_info) != 0) {
		if_printf(&sc->sc_ic.ic_if, "can't get meminfo\n");
		return 1;
	}

	/* Setup firmware TX descriptor ring */
	acx111_init_fw_txring(sc, le32toh(mem_info.fw_txring_start));

	/*
	 * There is no need to setup firmware RX descriptor ring,
	 * it is automaticly setup by hardware.
	 */

	return 0;
}

static void
acx111_init_fw_txring(struct acx_softc *sc, uint32_t fw_txdesc_start)
{
	struct acx_txbuf *tx_buf;
	uint32_t desc_paddr;
	int i;

	tx_buf = sc->sc_buf_data.tx_buf;
	desc_paddr = sc->sc_ring_data.tx_ring_paddr;

	for (i = 0; i < ACX_TX_DESC_CNT; ++i) {
		tx_buf[i].tb_fwdesc_ofs = fw_txdesc_start +
					  (i * ACX111_FW_TXDESC_SIZE);

		/*
		 * Except for the following fields, rest of the fields
		 * are setup by hardware.
		 */
		FW_TXDESC_SETFIELD_4(sc, &tx_buf[i], f_tx_host_desc,
				     desc_paddr);
		FW_TXDESC_SETFIELD_1(sc, &tx_buf[i], f_tx_ctrl,
				     DESC_CTRL_HOSTOWN);

		desc_paddr += (2 * sizeof(struct acx_host_desc));
	}
}

static int
acx111_write_config(struct acx_softc *sc, struct acx_config *conf)
{
	struct acx111_conf_txpower tx_power;
	struct acx111_conf_option opt;
	struct acx111_conf_rt0_nretry rt0_nretry;
	uint32_t dataflow;

	/* Set TX power */
	tx_power.txpower = ACX111_TXPOWER_VAL;
	if (acx111_set_txpower_conf(sc, &tx_power) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't set TX power\n",
			  __func__);
		return ENXIO;
	}

	/*
	 * Turn off hardware WEP
	 */
	if (acx111_get_option_conf(sc, &opt) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't get option\n", __func__);
		return ENXIO;
	}

	dataflow = le32toh(opt.dataflow) |
		   ACX111_DF_NO_TXENCRYPT |
		   ACX111_DF_NO_RXDECRYPT;
	opt.dataflow = htole32(dataflow);

	if (acx111_set_option_conf(sc, &opt) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't set option\n", __func__);
		return ENXIO;
	}

	/*
	 * Set number of retries for 0th rate
	 */
	rt0_nretry.rt0_nretry = sc->chip_rate_fallback;
	if (acx111_set_rt0_nretry_conf(sc, &rt0_nretry) != 0) {
		if_printf(&sc->sc_ic.ic_if, "%s can't set rate0 nretry\n",
			  __func__);
		return ENXIO;
	}
	return 0;
}

static uint8_t
_acx111_set_fw_txdesc_rate(struct acx_softc *sc, struct acx_txbuf *tx_buf,
			   struct ieee80211_node *ni, int data_len,
			   int rateidx_max)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint16_t rate;
	uint8_t ret = 0;

	KKASSERT(rateidx_max <= IEEE80211_RATEIDX_MAX);

	if (ni == NULL) {
		rate = ACX111_RATE_2;	/* 1Mbit/s */
		ret = 2;
		tx_buf->tb_rateidx_len = 1;
		tx_buf->tb_rateidx[0] = 0;
	} else {
		struct ieee80211_rateset *rs = &ni->ni_rates;
		int *rateidx = tx_buf->tb_rateidx;
		int i, n;

		n = ieee80211_ratectl_findrate(ni, data_len, rateidx,
					       rateidx_max);

		rate = 0;
		for (i = 0; i < n; ++i) {
			u_int map_idx = IEEE80211_RS_RATE(rs, rateidx[i]);

			if (ACX111_CHK_RATE(ifp, map_idx, rateidx[i]) < 0)
				continue;

			if (ret == 0)
				ret = map_idx;

			rate |= acx111_rate_map[map_idx];
		}
		if (rate == 0) {
			if_printf(ifp, "WARNING no rate, set to 1Mbit/s\n");
			rate = ACX111_RATE_2;
			ret = 2;
			tx_buf->tb_rateidx_len = 1;
			tx_buf->tb_rateidx[0] = 0;
		} else {
			tx_buf->tb_rateidx_len = n;
		}
	}
	FW_TXDESC_SETFIELD_2(sc, tx_buf, u.r2.rate111, rate);

	return ret;
}

static uint8_t
acx111_set_fw_txdesc_rate_onoe(struct acx_softc *sc, struct acx_txbuf *tx_buf,
			       struct ieee80211_node *ni, int data_len)
{
	return _acx111_set_fw_txdesc_rate(sc, tx_buf, ni, data_len,
					  ACX111_ONOE_RATEIDX_MAX);
}

static uint8_t
acx111_set_fw_txdesc_rate_amrr(struct acx_softc *sc, struct acx_txbuf *tx_buf,
			       struct ieee80211_node *ni, int data_len)
{
	return _acx111_set_fw_txdesc_rate(sc, tx_buf, ni, data_len,
					  ACX111_AMRR_RATEIDX_MAX);
}

static void
acx111_set_bss_join_param(struct acx_softc *sc, void *param, int dtim_intvl)
{
	struct acx111_bss_join *bj = param;
	const struct ieee80211_rateset *rs = &sc->sc_ic.ic_bss->ni_rates;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint16_t basic_rates = 0;
	int i;

	for (i = 0; i < rs->rs_nrates; ++i) {
		if (rs->rs_rates[i] & IEEE80211_RATE_BASIC) {
			u_int map_idx = IEEE80211_RS_RATE(rs, i);

			if (ACX111_CHK_RATE(ifp, map_idx, i) < 0)
				continue;

			basic_rates |= acx111_rate_map[map_idx];
		}
	}
	DPRINTF((ifp, "basic rates: 0x%04x\n", basic_rates));

	bj->basic_rates = htole16(basic_rates);
	bj->dtim_intvl = dtim_intvl;
}

static void *
acx111_ratectl_attach(struct ieee80211com *ic, u_int rc)
{
	struct ifnet *ifp = &ic->ic_if;
	struct acx_softc *sc = ifp->if_softc;
	const int *tries;
	void *ret;
	int i;

	switch (rc) {
	case IEEE80211_RATECTL_ONOE:
		tries = acx111_onoe_tries;
		sc->chip_set_fw_txdesc_rate = acx111_set_fw_txdesc_rate_onoe;
		sc->chip_tx_complete = acx111_tx_complete_onoe;
		ret = &sc->sc_onoe_param;
		break;

	case IEEE80211_RATECTL_AMRR:
		tries = acx111_amrr_tries;
		sc->chip_set_fw_txdesc_rate = acx111_set_fw_txdesc_rate_amrr;
		sc->chip_tx_complete = acx111_tx_complete_amrr;
		ret = &sc->sc_amrr_param;
		break;

	case IEEE80211_RATECTL_NONE:
		/* This could only happen during detaching */
		return NULL;

	default:
		panic("unknown rate control algo %u\n", rc);
		break;
	}

	sc->chip_rate_fallback = tries[0] - 1;

	sc->chip_short_retry_limit = 0;
	for (i = 0; i < IEEE80211_RATEIDX_MAX; ++i)
		sc->chip_short_retry_limit += tries[i];
	sc->chip_short_retry_limit--;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_UP)) ==
	    (IFF_RUNNING | IFF_UP)) {
	    	struct acx_conf_nretry_short sretry;
		struct acx111_conf_rt0_nretry rt0_nretry;

		/*
		 * Set number of short retries
		 */
		sretry.nretry = sc->chip_short_retry_limit;
		if (acx_set_nretry_short_conf(sc, &sretry) != 0) {
			if_printf(ifp, "%s can't set short retry limit\n",
				  __func__);
		}
		DPRINTF((ifp, "%s set sretry %d\n", __func__,
			 sc->chip_short_retry_limit));

		/*
		 * Set number of retries for 0th rate
		 */
		rt0_nretry.rt0_nretry = sc->chip_rate_fallback;
		if (acx111_set_rt0_nretry_conf(sc, &rt0_nretry) != 0) {
			if_printf(ifp, "%s can't set rate0 nretry\n",
				  __func__);
		}
		DPRINTF((ifp, "%s set rate 0 nretry %d\n", __func__,
			 sc->chip_rate_fallback));
	}
	return ret;
}

static void
_acx111_tx_complete(struct acx_softc *sc, struct acx_txbuf *tx_buf,
		    int frame_len, int is_fail, const int tries_arr[])
{
	struct ieee80211_ratectl_res rc_res[IEEE80211_RATEIDX_MAX];
	int rts_retries, data_retries, n, tries, prev_tries;

	KKASSERT(tx_buf->tb_rateidx_len <= IEEE80211_RATEIDX_MAX);

	rts_retries = FW_TXDESC_GETFIELD_1(sc, tx_buf, f_tx_rts_nretry);
	data_retries = FW_TXDESC_GETFIELD_1(sc, tx_buf, f_tx_data_nretry);

#if 0
	DPRINTF((&sc->sc_ic.ic_if, "d%d r%d rateidx_len %d\n",
		 data_retries, rts_retries, tx_buf->tb_rateidx_len));
#endif

	prev_tries = tries = 0;
	for (n = 0; n < tx_buf->tb_rateidx_len; ++n) {
		rc_res[n].rc_res_tries = tries_arr[n];
		rc_res[n].rc_res_rateidx = tx_buf->tb_rateidx[n];
		if (!is_fail) {
			if (data_retries + 1 <= tries)
				break;
			prev_tries = tries;
			tries += tries_arr[n];
		}
	}
	KKASSERT(n != 0);

	if (!is_fail && data_retries + 1 <= tries) {
		rc_res[n - 1].rc_res_tries = data_retries + 1 - prev_tries;
#if 0
		DPRINTF((&sc->sc_ic.ic_if, "n %d, last tries%d\n",
			 n, rc_res[n - 1].rc_res_tries));
#endif
	}
	ieee80211_ratectl_tx_complete(tx_buf->tb_node, frame_len, rc_res, n,
				      data_retries, rts_retries, is_fail);
}

static void
acx111_tx_complete_onoe(struct acx_softc *sc, struct acx_txbuf *tx_buf,
			int frame_len, int is_fail)
{
	_acx111_tx_complete(sc, tx_buf, frame_len, is_fail,
			    acx111_onoe_tries);
}

static void
acx111_tx_complete_amrr(struct acx_softc *sc, struct acx_txbuf *tx_buf,
			int frame_len, int is_fail)
{
	_acx111_tx_complete(sc, tx_buf, frame_len, is_fail,
			    acx111_amrr_tries);
}

static int
acx111_calibrate(struct acx_softc *sc)
{
	struct acx111_calib calib;

	calib.calib = htole32(ACX111_CALIB_AUTO |
			      ACX111_CALIB_DC |
			      ACX111_CALIB_AFE_DC |
			      ACX111_CALIB_TX_MISMATCH |
			      ACX111_CALIB_TX_EQUAL);
	calib.interval = htole32(ACX111_FW_CALIB_INTVL);

	return acx_exec_command(sc, ACXCMD_CALIBRATE, &calib, sizeof(calib),
				NULL, 0);
}
