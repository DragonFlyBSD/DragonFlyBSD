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
 * $NetBSD: rtw.c,v 1.72 2006/03/28 00:48:10 dyoung Exp $
 */

/*
 * Copyright (c) 2004, 2005 David Young.  All rights reserved.
 *
 * Programmed for NetBSD by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

/*
 * Device driver for the Realtek RTL8180 802.11 MAC/BBP.
 */

#include <sys/param.h>
#include <sys/bitops.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/serialize.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#include <dev/netif/rtw/rtwreg.h>
#include <dev/netif/rtw/rtwvar.h>
#include <dev/netif/rtw/rtwphyio.h>
#include <dev/netif/rtw/rtwphy.h>
#include <dev/netif/rtw/smc93cx6var.h>
#include <dev/netif/rtw/sa2400reg.h>

/* XXX */
#define IEEE80211_DUR_DS_LONG_PREAMBLE	144
#define IEEE80211_DUR_DS_SHORT_PREAMBLE	72
#define IEEE80211_DUR_DS_SLOW_PLCPHDR	48
#define IEEE80211_DUR_DS_FAST_PLCPHDR	24
#define IEEE80211_DUR_DS_SLOW_ACK	112
#define IEEE80211_DUR_DS_SLOW_CTS	112
#define IEEE80211_DUR_DS_SIFS		10

struct rtw_txsegs {
	int			nseg;
	bus_dma_segment_t	segs[RTW_MAXPKTSEGS];
};

devclass_t	rtw_devclass;

static const struct ieee80211_rateset rtw_rates_11b = { 4, { 2, 4, 11, 22 } };

SYSCTL_NODE(_hw, OID_AUTO, rtw, CTLFLAG_RD, 0,
	    "Realtek RTL818x 802.11 controls");

/* [0, __SHIFTOUT(RTW_CONFIG4_RFTYPE_MASK, RTW_CONFIG4_RFTYPE_MASK)] */
static int	rtw_rfprog_fallback = 0;
SYSCTL_INT(_hw_rtw, OID_AUTO, rfprog_fallback, CTLFLAG_RW,
	   &rtw_rfprog_fallback, 0, "fallback RF programming method");

static int	rtw_host_rfio = 0;		/* 0/1 */
SYSCTL_INT(_hw_rtw, OID_AUTO, host_rfio, CTLFLAG_RW,
	   &rtw_host_rfio, 0, "enable host control of RF I/O");

#ifdef RTW_DEBUG
int		rtw_debug = 0;			/* [0, RTW_DEBUG_MAX] */
SYSCTL_INT(_hw_rtw, OID_AUTO, debug, CTLFLAG_RW, &rtw_debug, 0, "debug level");

static int	rtw_rxbufs_limit = RTW_RXQLEN;	/* [0, RTW_RXQLEN] */
SYSCTL_INT(_hw_rtw, OID_AUTO, rxbufs_limit, CTLFLAG_RW, &rtw_rxbufs_limit, 0,
	   "rx buffers limit");
#endif /* RTW_DEBUG */

#if 0
static int	rtw_xmtr_restart = 0;
SYSCTL_INT(_hw_rtw, OID_AUTO, xmtr_restart, CTLFLAG_RW, &rtw_xmtr_restart, 0,
	   "gratuitously reset xmtr on rcvr error");

static int	rtw_ring_reset = 0;
SYSCTL_INT(_hw_rtw, OID_AUTO, ring_reset, CTLFLAG_RW, &rtw_ring_reset, 0,
	   "reset ring pointers on rcvr error");
#endif

static int	rtw_do_chip_reset = 0;
SYSCTL_INT(_hw_rtw, OID_AUTO, chip_reset, CTLFLAG_RW, &rtw_do_chip_reset, 0,
	   "gratuitously reset chip on rcvr error");

int		rtw_dwelltime = 200;	/* milliseconds */

/* XXX */
static struct ieee80211_cipher rtw_cipher_wep;

static void	rtw_led_init(struct rtw_softc *);
static void	rtw_led_newstate(struct rtw_softc *, enum ieee80211_state);
static void	rtw_led_slowblink(void *);
static void	rtw_led_fastblink(void *);
static void	rtw_led_set(struct rtw_softc *);

static void	rtw_init(void *);
static void	rtw_start(struct ifnet *, struct ifaltq_subque *);
static int	rtw_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	rtw_watchdog(struct ifnet *);
static void	rtw_intr(void *);

static void	rtw_intr_rx(struct rtw_softc *, uint16_t);
static void	rtw_intr_tx(struct rtw_softc *, uint16_t);
static void	rtw_intr_beacon(struct rtw_softc *, uint16_t);
static void	rtw_intr_atim(struct rtw_softc *);
static void	rtw_intr_ioerror(struct rtw_softc *, uint16_t);
static void	rtw_intr_timeout(struct rtw_softc *);

static int	rtw_dequeue(struct ifnet *, struct rtw_txsoft_blk **,
			    struct rtw_txdesc_blk **, struct mbuf **,
			    struct ieee80211_node **);
static struct mbuf *rtw_load_txbuf(struct rtw_softc *, struct rtw_txsoft *,
				   struct rtw_txsegs *, int, struct mbuf *);

static void	rtw_idle(struct rtw_softc *);
static void	rtw_txring_fixup(struct rtw_softc *);
static void	rtw_rxring_fixup(struct rtw_softc *);
static int	rtw_txring_next(struct rtw_regs *, struct rtw_txdesc_blk *);
static void	rtw_reset_oactive(struct rtw_softc *);

static int	rtw_enable(struct rtw_softc *);
static void	rtw_disable(struct rtw_softc *);
static void	rtw_io_enable(struct rtw_softc *, uint8_t, int);
static int	rtw_pwrstate(struct rtw_softc *, enum rtw_pwrstate);
static void	rtw_set_access(struct rtw_softc *, enum rtw_access);

static void	rtw_continuous_tx_enable(struct rtw_softc *, int);
static void	rtw_txdac_enable(struct rtw_softc *, int);
static void	rtw_anaparm_enable(struct rtw_regs *, int);
static void	rtw_config0123_enable(struct rtw_regs *, int);

static void	rtw_transmit_config(struct rtw_regs *);
static void	rtw_set_rfprog(struct rtw_softc *);
static void	rtw_enable_interrupts(struct rtw_softc *);
static void	rtw_pktfilt_load(struct rtw_softc *);
static void	rtw_wep_setkeys(struct rtw_softc *);
static void	rtw_resume_ticks(struct rtw_softc *);
static void	rtw_set_nettype(struct rtw_softc *, enum ieee80211_opmode);

static int	rtw_reset(struct rtw_softc *);
static int	rtw_chip_reset(struct rtw_softc *);
static int	rtw_recall_eeprom(struct rtw_softc *);
static int	rtw_srom_read(struct rtw_softc *);
static int	rtw_srom_parse(struct rtw_softc *);
static struct rtw_rf *rtw_rf_attach(struct rtw_softc *, enum rtw_rfchipid, int);

static uint8_t	rtw_check_phydelay(struct rtw_regs *, uint32_t);
static void	rtw_identify_country(struct rtw_softc *);
static int	rtw_identify_sta(struct rtw_softc *);

static int	rtw_swring_setup(struct rtw_softc *);
static void	rtw_hwring_setup(struct rtw_softc *);

static int	rtw_desc_blk_alloc(struct rtw_softc *);
static void	rtw_desc_blk_free(struct rtw_softc *);
static int	rtw_soft_blk_alloc(struct rtw_softc *);
static void	rtw_soft_blk_free(struct rtw_softc *);

static void	rtw_txdesc_blk_init_all(struct rtw_softc *);
static void	rtw_txsoft_blk_init_all(struct rtw_softc *);
static void	rtw_rxdesc_blk_init_all(struct rtw_softc *);
static int	rtw_rxsoft_blk_init_all(struct rtw_softc *);

static void	rtw_txdesc_blk_reset_all(struct rtw_softc *);

static int	rtw_rxsoft_alloc(struct rtw_softc *, struct rtw_rxsoft *, int);
static void	rtw_rxdesc_init(struct rtw_softc *, int idx, int);

#ifdef RTW_DEBUG
static void	rtw_print_txdesc(struct rtw_softc *, const char *,
				 struct rtw_txsoft *, struct rtw_txdesc_blk *,
				 int);
#endif /* RTW_DEBUG */

static int	rtw_newstate(struct ieee80211com *, enum ieee80211_state, int);
static void	rtw_next_scan(void *);

static int	rtw_key_delete(struct ieee80211com *,
			       const struct ieee80211_key *);
static int	rtw_key_set(struct ieee80211com *,
			    const struct ieee80211_key *,
			    const u_int8_t[IEEE80211_ADDR_LEN]);
static void	rtw_key_update_end(struct ieee80211com *);
static void	rtw_key_update_begin(struct ieee80211com *);
static int	rtw_wep_decap(struct ieee80211_key *, struct mbuf *, int);

static int	rtw_compute_duration1(int, int, uint32_t, int,
				      struct rtw_duration *);
static int	rtw_compute_duration(const struct ieee80211_frame_min *,
				     const struct ieee80211_key *, int,
				     uint32_t, int, int,
				     struct rtw_duration *,
				     struct rtw_duration *, int *, int);

static int	rtw_get_rssi(struct rtw_softc *, uint8_t, uint8_t);
static int	rtw_maxim_getrssi(uint8_t, uint8_t);
static int	rtw_gct_getrssi(uint8_t, uint8_t);
static int	rtw_philips_getrssi(uint8_t, uint8_t);

static void	*rtw_ratectl_attach(struct ieee80211com *, u_int);

#ifdef RTW_DEBUG
static void
rtw_print_regs(struct rtw_regs *regs, const char *dvname, const char *where)
{
#define PRINTREG32(sc, reg)				\
	RTW_DPRINTF(RTW_DEBUG_REGDUMP,			\
	    ("%s: reg[ " #reg " / %03x ] = %08x\n",	\
	    dvname, reg, RTW_READ(regs, reg)))

#define PRINTREG16(sc, reg)				\
	RTW_DPRINTF(RTW_DEBUG_REGDUMP,			\
	    ("%s: reg[ " #reg " / %03x ] = %04x\n",	\
	    dvname, reg, RTW_READ16(regs, reg)))

#define PRINTREG8(sc, reg)				\
	RTW_DPRINTF(RTW_DEBUG_REGDUMP,			\
	    ("%s: reg[ " #reg " / %03x ] = %02x\n",	\
	    dvname, reg, RTW_READ8(regs, reg)))

	RTW_DPRINTF(RTW_DEBUG_REGDUMP, ("%s: %s\n", dvname, where));

	PRINTREG32(regs, RTW_IDR0);
	PRINTREG32(regs, RTW_IDR1);
	PRINTREG32(regs, RTW_MAR0);
	PRINTREG32(regs, RTW_MAR1);
	PRINTREG32(regs, RTW_TSFTRL);
	PRINTREG32(regs, RTW_TSFTRH);
	PRINTREG32(regs, RTW_TLPDA);
	PRINTREG32(regs, RTW_TNPDA);
	PRINTREG32(regs, RTW_THPDA);
	PRINTREG32(regs, RTW_TCR);
	PRINTREG32(regs, RTW_RCR);
	PRINTREG32(regs, RTW_TINT);
	PRINTREG32(regs, RTW_TBDA);
	PRINTREG32(regs, RTW_ANAPARM);
	PRINTREG32(regs, RTW_BB);
	PRINTREG32(regs, RTW_PHYCFG);
	PRINTREG32(regs, RTW_WAKEUP0L);
	PRINTREG32(regs, RTW_WAKEUP0H);
	PRINTREG32(regs, RTW_WAKEUP1L);
	PRINTREG32(regs, RTW_WAKEUP1H);
	PRINTREG32(regs, RTW_WAKEUP2LL);
	PRINTREG32(regs, RTW_WAKEUP2LH);
	PRINTREG32(regs, RTW_WAKEUP2HL);
	PRINTREG32(regs, RTW_WAKEUP2HH);
	PRINTREG32(regs, RTW_WAKEUP3LL);
	PRINTREG32(regs, RTW_WAKEUP3LH);
	PRINTREG32(regs, RTW_WAKEUP3HL);
	PRINTREG32(regs, RTW_WAKEUP3HH);
	PRINTREG32(regs, RTW_WAKEUP4LL);
	PRINTREG32(regs, RTW_WAKEUP4LH);
	PRINTREG32(regs, RTW_WAKEUP4HL);
	PRINTREG32(regs, RTW_WAKEUP4HH);
	PRINTREG32(regs, RTW_DK0);
	PRINTREG32(regs, RTW_DK1);
	PRINTREG32(regs, RTW_DK2);
	PRINTREG32(regs, RTW_DK3);
	PRINTREG32(regs, RTW_RETRYCTR);
	PRINTREG32(regs, RTW_RDSAR);
	PRINTREG32(regs, RTW_FER);
	PRINTREG32(regs, RTW_FEMR);
	PRINTREG32(regs, RTW_FPSR);
	PRINTREG32(regs, RTW_FFER);

	/* 16-bit registers */
	PRINTREG16(regs, RTW_BRSR);
	PRINTREG16(regs, RTW_IMR);
	PRINTREG16(regs, RTW_ISR);
	PRINTREG16(regs, RTW_BCNITV);
	PRINTREG16(regs, RTW_ATIMWND);
	PRINTREG16(regs, RTW_BINTRITV);
	PRINTREG16(regs, RTW_ATIMTRITV);
	PRINTREG16(regs, RTW_CRC16ERR);
	PRINTREG16(regs, RTW_CRC0);
	PRINTREG16(regs, RTW_CRC1);
	PRINTREG16(regs, RTW_CRC2);
	PRINTREG16(regs, RTW_CRC3);
	PRINTREG16(regs, RTW_CRC4);
	PRINTREG16(regs, RTW_CWR);

	/* 8-bit registers */
	PRINTREG8(regs, RTW_CR);
	PRINTREG8(regs, RTW_9346CR);
	PRINTREG8(regs, RTW_CONFIG0);
	PRINTREG8(regs, RTW_CONFIG1);
	PRINTREG8(regs, RTW_CONFIG2);
	PRINTREG8(regs, RTW_MSR);
	PRINTREG8(regs, RTW_CONFIG3);
	PRINTREG8(regs, RTW_CONFIG4);
	PRINTREG8(regs, RTW_TESTR);
	PRINTREG8(regs, RTW_PSR);
	PRINTREG8(regs, RTW_SCR);
	PRINTREG8(regs, RTW_PHYDELAY);
	PRINTREG8(regs, RTW_CRCOUNT);
	PRINTREG8(regs, RTW_PHYADDR);
	PRINTREG8(regs, RTW_PHYDATAW);
	PRINTREG8(regs, RTW_PHYDATAR);
	PRINTREG8(regs, RTW_CONFIG5);
	PRINTREG8(regs, RTW_TPPOLL);

	PRINTREG16(regs, RTW_BSSID16);
	PRINTREG32(regs, RTW_BSSID32);
#undef PRINTREG32
#undef PRINTREG16
#undef PRINTREG8
}
#endif /* RTW_DEBUG */

static void
rtw_continuous_tx_enable(struct rtw_softc *sc, int enable)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint32_t tcr;

	tcr = RTW_READ(regs, RTW_TCR);
	tcr &= ~RTW_TCR_LBK_MASK;
	if (enable)
		tcr |= RTW_TCR_LBK_CONT;
	else
		tcr |= RTW_TCR_LBK_NORMAL;
	RTW_WRITE(regs, RTW_TCR, tcr);
	RTW_SYNC(regs, RTW_TCR, RTW_TCR);
	rtw_set_access(sc, RTW_ACCESS_ANAPARM);
	rtw_txdac_enable(sc, !enable);
	rtw_set_access(sc, RTW_ACCESS_ANAPARM);/* XXX Voodoo from Linux. */
	rtw_set_access(sc, RTW_ACCESS_NONE);
}

#ifdef RTW_DEBUG
static const char *
rtw_access_string(enum rtw_access access)
{
	switch (access) {
	case RTW_ACCESS_NONE:
		return "none";
	case RTW_ACCESS_CONFIG:
		return "config";
	case RTW_ACCESS_ANAPARM:
		return "anaparm";
	default:
		return "unknown";
	}
}
#endif /* RTW_DEBUG */

static void
rtw_set_access1(struct rtw_regs *regs, enum rtw_access naccess)
{
	KKASSERT(naccess >= RTW_ACCESS_NONE && naccess <= RTW_ACCESS_ANAPARM);
	KKASSERT(regs->r_access >= RTW_ACCESS_NONE &&
		 regs->r_access <= RTW_ACCESS_ANAPARM);

	if (naccess == regs->r_access)
		return;

	switch (naccess) {
	case RTW_ACCESS_NONE:
		switch (regs->r_access) {
		case RTW_ACCESS_ANAPARM:
			rtw_anaparm_enable(regs, 0);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			rtw_config0123_enable(regs, 0);
			/*FALLTHROUGH*/
		case RTW_ACCESS_NONE:
			break;
		}
		break;
	case RTW_ACCESS_CONFIG:
		switch (regs->r_access) {
		case RTW_ACCESS_NONE:
			rtw_config0123_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			break;
		case RTW_ACCESS_ANAPARM:
			rtw_anaparm_enable(regs, 0);
			break;
		}
		break;
	case RTW_ACCESS_ANAPARM:
		switch (regs->r_access) {
		case RTW_ACCESS_NONE:
			rtw_config0123_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_CONFIG:
			rtw_anaparm_enable(regs, 1);
			/*FALLTHROUGH*/
		case RTW_ACCESS_ANAPARM:
			break;
		}
		break;
	}
}

static void
rtw_set_access(struct rtw_softc *sc, enum rtw_access access)
{
	struct rtw_regs *regs = &sc->sc_regs;

	rtw_set_access1(regs, access);
	RTW_DPRINTF(RTW_DEBUG_ACCESS,
	    ("%s: access %s -> %s\n", sc->sc_ic.ic_if.if_xname,
	    rtw_access_string(regs->r_access),
	    rtw_access_string(access)));
	regs->r_access = access;
}

/*
 * Enable registers, switch register banks.
 */
static void
rtw_config0123_enable(struct rtw_regs *regs, int enable)
{
	uint8_t ecr;

	ecr = RTW_READ8(regs, RTW_9346CR);
	ecr &= ~(RTW_9346CR_EEM_MASK | RTW_9346CR_EECS | RTW_9346CR_EESK);
	if (enable) {
		ecr |= RTW_9346CR_EEM_CONFIG;
	} else {
		RTW_WBW(regs, RTW_9346CR, MAX(RTW_CONFIG0, RTW_CONFIG3));
		ecr |= RTW_9346CR_EEM_NORMAL;
	}
	RTW_WRITE8(regs, RTW_9346CR, ecr);
	RTW_SYNC(regs, RTW_9346CR, RTW_9346CR);
}

/* requires rtw_config0123_enable(, 1) */
static void
rtw_anaparm_enable(struct rtw_regs *regs, int enable)
{
	uint8_t cfg3;

	cfg3 = RTW_READ8(regs, RTW_CONFIG3);
	cfg3 |= RTW_CONFIG3_CLKRUNEN;
	if (enable)
		cfg3 |= RTW_CONFIG3_PARMEN;
	else
		cfg3 &= ~RTW_CONFIG3_PARMEN;
	RTW_WRITE8(regs, RTW_CONFIG3, cfg3);
	RTW_SYNC(regs, RTW_CONFIG3, RTW_CONFIG3);
}

/* requires rtw_anaparm_enable(, 1) */
static void
rtw_txdac_enable(struct rtw_softc *sc, int enable)
{
	uint32_t anaparm;
	struct rtw_regs *regs = &sc->sc_regs;

	anaparm = RTW_READ(regs, RTW_ANAPARM);
	if (enable)
		anaparm &= ~RTW_ANAPARM_TXDACOFF;
	else
		anaparm |= RTW_ANAPARM_TXDACOFF;
	RTW_WRITE(regs, RTW_ANAPARM, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM, RTW_ANAPARM);
}

static int
rtw_chip_reset1(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t cr;
	int i;

	RTW_WRITE8(regs, RTW_CR, RTW_CR_RST);

	RTW_WBR(regs, RTW_CR, RTW_CR);

	for (i = 0; i < 1000; i++) {
		if ((cr = RTW_READ8(regs, RTW_CR) & RTW_CR_RST) == 0) {
			RTW_DPRINTF(RTW_DEBUG_RESET,
			    ("%s: reset in %dus\n",
			     sc->sc_ic.ic_if.if_xname, i));
			return 0;
		}
		RTW_RBR(regs, RTW_CR, RTW_CR);
		DELAY(10); /* 10us */
	}

	if_printf(&sc->sc_ic.ic_if, "reset failed\n");
	return ETIMEDOUT;
}

static int
rtw_chip_reset(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint32_t tcr;

	/* from Linux driver */
	tcr = RTW_TCR_CWMIN | RTW_TCR_MXDMA_2048 |
	      __SHIFTIN(7, RTW_TCR_SRL_MASK) | __SHIFTIN(7, RTW_TCR_LRL_MASK);

	RTW_WRITE(regs, RTW_TCR, tcr);

	RTW_WBW(regs, RTW_CR, RTW_TCR);

	return rtw_chip_reset1(sc);
}

static int
rtw_wep_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	struct ieee80211_key keycopy;
	const struct ieee80211_cipher *wep_cipher;

	RTW_DPRINTF(RTW_DEBUG_KEY, ("%s:\n", __func__));

	keycopy = *k;
	keycopy.wk_flags &= ~IEEE80211_KEY_SWCRYPT;

	wep_cipher = ieee80211_crypto_cipher(IEEE80211_CIPHER_WEP);
	KKASSERT(wep_cipher != NULL);

	return wep_cipher->ic_decap(&keycopy, m, hdrlen);
}

static int
rtw_key_delete(struct ieee80211com *ic, const struct ieee80211_key *k)
{
	struct rtw_softc *sc = ic->ic_ifp->if_softc;
	u_int keyix = k->wk_keyix;

	DPRINTF(sc, RTW_DEBUG_KEY, ("%s: delete key %u\n", __func__, keyix));

	if (keyix >= IEEE80211_WEP_NKID)
		return 0;
	if (k->wk_keylen != 0)
		sc->sc_flags &= ~RTW_F_DK_VALID;
	return 1;
}

static int
rtw_key_set(struct ieee80211com *ic, const struct ieee80211_key *k,
	    const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct rtw_softc *sc = ic->ic_ifp->if_softc;

	DPRINTF(sc, RTW_DEBUG_KEY, ("%s: set key %u\n", __func__, k->wk_keyix));

	if (k->wk_keyix >= IEEE80211_WEP_NKID)
		return 0;

	sc->sc_flags &= ~RTW_F_DK_VALID;
	return 1;
}

static void
rtw_key_update_begin(struct ieee80211com *ic)
{
#ifdef RTW_DEBUG
	struct ifnet *ifp = ic->ic_ifp;
	struct rtw_softc *sc = ifp->if_softc;
#endif

	DPRINTF(sc, RTW_DEBUG_KEY, ("%s:\n", __func__));
}

static void
rtw_key_update_end(struct ieee80211com *ic)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct rtw_softc *sc = ifp->if_softc;

	DPRINTF(sc, RTW_DEBUG_KEY, ("%s:\n", __func__));

	if ((sc->sc_flags & RTW_F_DK_VALID) != 0 ||
	    (sc->sc_flags & RTW_F_ENABLED) == 0 ||
	    (sc->sc_flags & RTW_F_INVALID) != 0)
		return;

	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 0);
	rtw_wep_setkeys(sc);
	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE,
		      (ifp->if_flags & IFF_RUNNING) != 0);
}

static __inline int
rtw_key_hwsupp(uint32_t flags, const struct ieee80211_key *k)
{
	if (k->wk_cipher->ic_cipher != IEEE80211_CIPHER_WEP)
		return 0;

	return ((flags & RTW_C_RXWEP_40) != 0 && k->wk_keylen == 5) ||
	       ((flags & RTW_C_RXWEP_104) != 0 && k->wk_keylen == 13);
}

static void
rtw_wep_setkeys(struct rtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_key *wk = ic->ic_nw_keys;
	const struct ieee80211_cipher *wep_cipher;
	struct rtw_regs *regs = &sc->sc_regs;
	union rtw_keys *rk = &sc->sc_keys;
	uint8_t psr, scr;
	int i, keylen;

	memset(rk->rk_keys, 0, sizeof(rk->rk_keys));

	wep_cipher = ieee80211_crypto_cipher(IEEE80211_CIPHER_WEP);
	KKASSERT(wep_cipher != NULL);

	/* Temporarily use software crypto for all keys. */
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (wk[i].wk_cipher == &rtw_cipher_wep)
			wk[i].wk_cipher = wep_cipher;
	}

	rtw_set_access(sc, RTW_ACCESS_CONFIG);

	psr = RTW_READ8(regs, RTW_PSR);
	scr = RTW_READ8(regs, RTW_SCR);
	scr &= ~(RTW_SCR_KM_MASK | RTW_SCR_TXSECON | RTW_SCR_RXSECON);

	if ((sc->sc_ic.ic_flags & IEEE80211_F_PRIVACY) == 0)
		goto out;

	for (keylen = i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (!rtw_key_hwsupp(sc->sc_flags, &wk[i]))
			continue;
		if (i == ic->ic_def_txkey) {
			keylen = wk[i].wk_keylen;
			break;
		}
		keylen = MAX(keylen, wk[i].wk_keylen);
	}

	if (keylen == 5)
		scr |= RTW_SCR_KM_WEP40 | RTW_SCR_RXSECON;
	else if (keylen == 13)
		scr |= RTW_SCR_KM_WEP104 | RTW_SCR_RXSECON;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (wk[i].wk_keylen != keylen ||
		    wk[i].wk_cipher->ic_cipher != IEEE80211_CIPHER_WEP)
			continue;
		/* h/w will decrypt, s/w still strips headers */
		wk[i].wk_cipher = &rtw_cipher_wep;
		memcpy(rk->rk_keys[i], wk[i].wk_key, wk[i].wk_keylen);
	}
out:
	RTW_WRITE8(regs, RTW_PSR, psr & ~RTW_PSR_PSEN);

	bus_space_write_region_4(regs->r_bt, regs->r_bh, RTW_DK0, rk->rk_words,
		NELEM(rk->rk_words));

	RTW_WBW(regs, RTW_DK0, RTW_PSR);
	RTW_WRITE8(regs, RTW_PSR, psr);
	RTW_WBW(regs, RTW_PSR, RTW_SCR);
	RTW_WRITE8(regs, RTW_SCR, scr);
	RTW_SYNC(regs, RTW_SCR, RTW_SCR);
	rtw_set_access(sc, RTW_ACCESS_NONE);
	sc->sc_flags |= RTW_F_DK_VALID;
}

static int
rtw_recall_eeprom(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	int i;
	uint8_t ecr;

	ecr = RTW_READ8(regs, RTW_9346CR);
	ecr = (ecr & ~RTW_9346CR_EEM_MASK) | RTW_9346CR_EEM_AUTOLOAD;
	RTW_WRITE8(regs, RTW_9346CR, ecr);

	RTW_WBR(regs, RTW_9346CR, RTW_9346CR);

	/* wait 25ms for completion */
	for (i = 0; i < 250; i++) {
		ecr = RTW_READ8(regs, RTW_9346CR);
		if ((ecr & RTW_9346CR_EEM_MASK) == RTW_9346CR_EEM_NORMAL) {
			RTW_DPRINTF(RTW_DEBUG_RESET,
			    ("%s: recall EEPROM in %dus\n",
			     sc->sc_ic.ic_if.if_xname, i * 100));
			return 0;
		}
		RTW_RBR(regs, RTW_9346CR, RTW_9346CR);
		DELAY(100);
	}
	if_printf(&sc->sc_ic.ic_if, "recall EEPROM failed\n");
	return ETIMEDOUT;
}

static int
rtw_reset(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t config1;
	int rc;

	sc->sc_flags &= ~RTW_F_DK_VALID;

	rc = rtw_chip_reset(sc);
	if (rc)
		return rc;

	rtw_recall_eeprom(sc);	/* ignore err */

	config1 = RTW_READ8(regs, RTW_CONFIG1);
	RTW_WRITE8(regs, RTW_CONFIG1, config1 & ~RTW_CONFIG1_PMEN);
	/* TBD turn off maximum power saving? */
	return 0;
}

static int
rtw_srom_parse(struct rtw_softc *sc)
{
	struct rtw_srom *sr = &sc->sc_srom;
	char scratch[sizeof("unknown 0xXX")];
	uint8_t mac[IEEE80211_ADDR_LEN];
	const char *rfname, *paname;
	uint16_t srom_version;
	char ethstr[ETHER_ADDRSTRLEN + 1];
	int i;

	sc->sc_flags &= ~(RTW_F_DIGPHY | RTW_F_DFLANTB | RTW_F_ANTDIV);
	sc->sc_rcr &= ~(RTW_RCR_ENCS1 | RTW_RCR_ENCS2);

	srom_version = RTW_SR_GET16(sr, RTW_SR_VERSION);
	if_printf(&sc->sc_ic.ic_if, "SROM version %d.%d",
		  srom_version >> 8, srom_version & 0xff);

	if (srom_version <= 0x0101) {
		kprintf(" is not understood, limping along with defaults\n");

		/* Default values */
		sc->sc_flags |= (RTW_F_DIGPHY | RTW_F_ANTDIV);
		sc->sc_csthr = RTW_SR_ENERGYDETTHR_DEFAULT;
		sc->sc_rcr |= RTW_RCR_ENCS1;
		sc->sc_rfchipid = RTW_RFCHIPID_PHILIPS;
		return 0;
	}
	kprintf("\n");

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		mac[i] = RTW_SR_GET(sr, RTW_SR_MAC + i);

	RTW_DPRINTF(RTW_DEBUG_ATTACH,
	    ("%s: EEPROM MAC %s\n", sc->sc_ic.ic_if.if_xname, kether_addr(mac, ethstr)));

	sc->sc_csthr = RTW_SR_GET(sr, RTW_SR_ENERGYDETTHR);

	if ((RTW_SR_GET(sr, RTW_SR_CONFIG2) & RTW_CONFIG2_ANT) != 0)
		sc->sc_flags |= RTW_F_ANTDIV;

	/*
	 * Note well: the sense of the RTW_SR_RFPARM_DIGPHY bit seems
	 * to be reversed.
	 */
	if ((RTW_SR_GET(sr, RTW_SR_RFPARM) & RTW_SR_RFPARM_DIGPHY) == 0)
		sc->sc_flags |= RTW_F_DIGPHY;
	if ((RTW_SR_GET(sr, RTW_SR_RFPARM) & RTW_SR_RFPARM_DFLANTB) != 0)
		sc->sc_flags |= RTW_F_DFLANTB;

	sc->sc_rcr |= __SHIFTIN(__SHIFTOUT(RTW_SR_GET(sr, RTW_SR_RFPARM),
				RTW_SR_RFPARM_CS_MASK), RTW_RCR_ENCS1);

	if ((RTW_SR_GET(sr, RTW_SR_CONFIG0) & RTW_CONFIG0_WEP104) != 0)
		sc->sc_flags |= RTW_C_RXWEP_104;

	sc->sc_flags |= RTW_C_RXWEP_40;	/* XXX */

	sc->sc_rfchipid = RTW_SR_GET(sr, RTW_SR_RFCHIPID);
	switch (sc->sc_rfchipid) {
	case RTW_RFCHIPID_GCT:		/* this combo seen in the wild */
		rfname = "GCT GRF5101";
		paname = "Winspring WS9901";
		break;
	case RTW_RFCHIPID_MAXIM:
		rfname = "Maxim MAX2820";	/* guess */
		paname = "Maxim MAX2422";	/* guess */
		break;
	case RTW_RFCHIPID_INTERSIL:
		rfname = "Intersil HFA3873";	/* guess */
		paname = "Intersil <unknown>";
		break;
	case RTW_RFCHIPID_PHILIPS:	/* this combo seen in the wild */
		rfname = "Philips SA2400A";
		paname = "Philips SA2411";
		break;
	case RTW_RFCHIPID_RFMD:
		/* this is the same front-end as an atw(4)! */
		rfname = "RFMD RF2948B, "	/* mentioned in Realtek docs */
			 "LNA: RFMD RF2494, "	/* mentioned in Realtek docs */
			 "SYN: Silicon Labs Si4126";	/* inferred from
			 				 * reference driver
							 */
		paname = "RFMD RF2189";		/* mentioned in Realtek docs */
		break;
	case RTW_RFCHIPID_RESERVED:
		rfname = paname = "reserved";
		break;
	default:
		ksnprintf(scratch, sizeof(scratch), "unknown 0x%02x",
			 sc->sc_rfchipid);
		rfname = paname = scratch;
	}
	if_printf(&sc->sc_ic.ic_if, "RF: %s, PA: %s\n", rfname, paname);

	switch (RTW_SR_GET(sr, RTW_SR_CONFIG0) & RTW_CONFIG0_GL_MASK) {
	case RTW_CONFIG0_GL_USA:
	case _RTW_CONFIG0_GL_USA:
		sc->sc_locale = RTW_LOCALE_USA;
		break;
	case RTW_CONFIG0_GL_EUROPE:
		sc->sc_locale = RTW_LOCALE_EUROPE;
		break;
	case RTW_CONFIG0_GL_JAPAN:
		sc->sc_locale = RTW_LOCALE_JAPAN;
		break;
	default:
		sc->sc_locale = RTW_LOCALE_UNKNOWN;
		break;
	}
	return 0;
}

static int
rtw_srom_read(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	struct rtw_srom *sr = &sc->sc_srom;
	struct seeprom_descriptor sd;
	uint8_t ecr;
	int rc;

	memset(&sd, 0, sizeof(sd));

	ecr = RTW_READ8(regs, RTW_9346CR);

	if ((sc->sc_flags & RTW_F_9356SROM) != 0) {
		RTW_DPRINTF(RTW_DEBUG_ATTACH,
			    ("%s: 93c56 SROM\n", sc->sc_ic.ic_if.if_xname));
		sr->sr_size = 256;
		sd.sd_chip = C56_66;
	} else {
		RTW_DPRINTF(RTW_DEBUG_ATTACH,
			    ("%s: 93c46 SROM\n", sc->sc_ic.ic_if.if_xname));
		sr->sr_size = 128;
		sd.sd_chip = C46;
	}

	ecr &= ~(RTW_9346CR_EEDI | RTW_9346CR_EEDO | RTW_9346CR_EESK |
	    RTW_9346CR_EEM_MASK | RTW_9346CR_EECS);
	ecr |= RTW_9346CR_EEM_PROGRAM;

	RTW_WRITE8(regs, RTW_9346CR, ecr);

	sr->sr_content = kmalloc(sr->sr_size, M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * RTL8180 has a single 8-bit register for controlling the
	 * 93cx6 SROM.  There is no "ready" bit. The RTL8180
	 * input/output sense is the reverse of read_seeprom's.
	 */
	sd.sd_tag = regs->r_bt;
	sd.sd_bsh = regs->r_bh;
	sd.sd_regsize = 1;
	sd.sd_control_offset = RTW_9346CR;
	sd.sd_status_offset = RTW_9346CR;
	sd.sd_dataout_offset = RTW_9346CR;
	sd.sd_CK = RTW_9346CR_EESK;
	sd.sd_CS = RTW_9346CR_EECS;
	sd.sd_DI = RTW_9346CR_EEDO;
	sd.sd_DO = RTW_9346CR_EEDI;
	/* make read_seeprom enter EEPROM read/write mode */
	sd.sd_MS = ecr;
	sd.sd_RDY = 0;

	/* TBD bus barriers */
	if (!read_seeprom(&sd, sr->sr_content, 0, sr->sr_size / 2)) {
		if_printf(&sc->sc_ic.ic_if, "could not read SROM\n");
		kfree(sr->sr_content, M_DEVBUF);
		sr->sr_content = NULL;
		return EIO;	/* XXX */
	}

	/* end EEPROM read/write mode */
	RTW_WRITE8(regs, RTW_9346CR,
		   (ecr & ~RTW_9346CR_EEM_MASK) | RTW_9346CR_EEM_NORMAL);
	RTW_WBRW(regs, RTW_9346CR, RTW_9346CR);

	rc = rtw_recall_eeprom(sc);
	if (rc)
		return rc;

#ifdef RTW_DEBUG
	{
		int i;
		RTW_DPRINTF(RTW_DEBUG_ATTACH,
		    ("\n%s: serial ROM:\n\t", sc->sc_ic.ic_if.if_xname));
		for (i = 0; i < sr->sr_size/2; i++) {
			if (((i % 8) == 0) && (i != 0))
				RTW_DPRINTF(RTW_DEBUG_ATTACH, ("\n\t"));
			RTW_DPRINTF(RTW_DEBUG_ATTACH,
			    (" %04x", sr->sr_content[i]));
		}
		RTW_DPRINTF(RTW_DEBUG_ATTACH, ("\n"));
	}
#endif /* RTW_DEBUG */
	return 0;
}

static void
rtw_set_rfprog(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	const char *method;
	uint8_t cfg4;

	cfg4 = RTW_READ8(regs, RTW_CONFIG4) & ~RTW_CONFIG4_RFTYPE_MASK;

	switch (sc->sc_rfchipid) {
	default:
		cfg4 |= __SHIFTIN(rtw_rfprog_fallback, RTW_CONFIG4_RFTYPE_MASK);
		method = "fallback";
		break;
	case RTW_RFCHIPID_INTERSIL:
		cfg4 |= RTW_CONFIG4_RFTYPE_INTERSIL;
		method = "Intersil";
		break;
	case RTW_RFCHIPID_PHILIPS:
		cfg4 |= RTW_CONFIG4_RFTYPE_PHILIPS;
		method = "Philips";
		break;
	case RTW_RFCHIPID_GCT:	/* XXX a guess */
	case RTW_RFCHIPID_RFMD:
		cfg4 |= RTW_CONFIG4_RFTYPE_RFMD;
		method = "RFMD";
		break;
	}

	RTW_WRITE8(regs, RTW_CONFIG4, cfg4);

	RTW_WBR(regs, RTW_CONFIG4, RTW_CONFIG4);

	RTW_DPRINTF(RTW_DEBUG_INIT,
		    ("%s: %s RF programming method, %#02x\n",
		     sc->sc_ic.ic_if.if_xname, method,
		     RTW_READ8(regs, RTW_CONFIG4)));
}

static __inline void
rtw_init_channels(struct rtw_softc *sc)
{
	const char *name = NULL;
	struct ieee80211_channel *chans = sc->sc_ic.ic_channels;
	int i;
#define ADD_CHANNEL(_chans, _chan) do {				\
	_chans[_chan].ic_flags = IEEE80211_CHAN_B;		\
	_chans[_chan].ic_freq =					\
	    ieee80211_ieee2mhz(_chan, _chans[_chan].ic_flags);	\
} while (0)

	switch (sc->sc_locale) {
	case RTW_LOCALE_USA:	/* 1-11 */
		name = "USA";
		for (i = 1; i <= 11; i++)
			ADD_CHANNEL(chans, i);
		break;
	case RTW_LOCALE_JAPAN:	/* 1-14 */
		name = "Japan";
		ADD_CHANNEL(chans, 14);
		for (i = 1; i <= 14; i++)
			ADD_CHANNEL(chans, i);
		break;
	case RTW_LOCALE_EUROPE:	/* 1-13 */
		name = "Europe";
		for (i = 1; i <= 13; i++)
			ADD_CHANNEL(chans, i);
		break;
	default:			/* 10-11 allowed by most countries */
		name = "<unknown>";
		for (i = 10; i <= 11; i++)
			ADD_CHANNEL(chans, i);
		break;
	}
	if_printf(&sc->sc_ic.ic_if, "Geographic Location %s\n", name);
#undef ADD_CHANNEL
}


static void
rtw_identify_country(struct rtw_softc *sc)
{
	uint8_t cfg0;

	cfg0 = RTW_READ8(&sc->sc_regs, RTW_CONFIG0);
	switch (cfg0 & RTW_CONFIG0_GL_MASK) {
	case RTW_CONFIG0_GL_USA:
	case _RTW_CONFIG0_GL_USA:
		sc->sc_locale = RTW_LOCALE_USA;
		break;
	case RTW_CONFIG0_GL_JAPAN:
		sc->sc_locale = RTW_LOCALE_JAPAN;
		break;
	case RTW_CONFIG0_GL_EUROPE:
		sc->sc_locale = RTW_LOCALE_EUROPE;
		break;
	default:
		sc->sc_locale = RTW_LOCALE_UNKNOWN;
		break;
	}
}

static int
rtw_identify_sta(struct rtw_softc *sc)
{
	static const uint8_t empty_macaddr[IEEE80211_ADDR_LEN] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t *addr = sc->sc_ic.ic_myaddr;
	uint32_t idr0, idr1;

	idr0 = RTW_READ(regs, RTW_IDR0);
	idr1 = RTW_READ(regs, RTW_IDR1);

	addr[0] = __SHIFTOUT(idr0, __BITS(0,  7));
	addr[1] = __SHIFTOUT(idr0, __BITS(8,  15));
	addr[2] = __SHIFTOUT(idr0, __BITS(16, 23));
	addr[3] = __SHIFTOUT(idr0, __BITS(24 ,31));

	addr[4] = __SHIFTOUT(idr1, __BITS(0,  7));
	addr[5] = __SHIFTOUT(idr1, __BITS(8, 15));

	if (IEEE80211_ADDR_EQ(addr, empty_macaddr)) {
		if_printf(&sc->sc_ic.ic_if, "could not get mac address\n");
		return ENXIO;
	}
	return 0;
}

static uint8_t
rtw_chan2txpower(struct rtw_srom *sr, struct ieee80211com *ic,
		 struct ieee80211_channel *chan)
{
	u_int idx = RTW_SR_TXPOWER1 + ieee80211_chan2ieee(ic, chan) - 1;

	KASSERT(idx >= RTW_SR_TXPOWER1 && idx <= RTW_SR_TXPOWER14,
		("%s: channel %d out of range", __func__,
		 idx - RTW_SR_TXPOWER1 + 1));
	return RTW_SR_GET(sr, idx);
}

static void
rtw_txdesc_blk_init_all(struct rtw_softc *sc)
{
	/* nfree: the number of free descriptors in each ring.
	 * The beacon ring is a special case: I do not let the
	 * driver use all of the descriptors on the beacon ring.
	 * The reasons are two-fold:
	 *
	 * (1) A BEACON descriptor's OWN bit is (apparently) not
	 * updated, so the driver cannot easily know if the descriptor
	 * belongs to it, or if it is racing the NIC.  If the NIC
	 * does not OWN every descriptor, then the driver can safely
	 * update the descriptors when RTW_TBDA points at tdb_next.
	 *
	 * (2) I hope that the NIC will process more than one BEACON
	 * descriptor in a single beacon interval, since that will
	 * enable multiple-BSS support.  Since the NIC does not
	 * clear the OWN bit, there is no natural place for it to
	 * stop processing BEACON desciptors.  Maybe it will *not*
	 * stop processing them!  I do not want to chance the NIC
	 * looping around and around a saturated beacon ring, so
	 * I will leave one descriptor unOWNed at all times.
	 */
	int nfree[RTW_NTXPRI] = {
		RTW_NTXDESCLO,
		RTW_NTXDESCMD,
		RTW_NTXDESCHI,
		RTW_NTXDESCBCN - 1
	};
	struct rtw_txdesc_blk *tdb;
	int pri;

	for (tdb = sc->sc_txdesc_blk, pri = 0; pri < RTW_NTXPRI; tdb++, pri++) {
		tdb->tdb_nfree = nfree[pri];
		tdb->tdb_next = 0;

		bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
				BUS_DMASYNC_PREWRITE);
	}
}

static void
rtw_txsoft_blk_init_all(struct rtw_softc *sc)
{
	struct rtw_txsoft_blk *tsb;
	int pri;

	for (tsb = sc->sc_txsoft_blk, pri = 0; pri < RTW_NTXPRI; tsb++, pri++) {
		int i;

		STAILQ_INIT(&tsb->tsb_dirtyq);
		STAILQ_INIT(&tsb->tsb_freeq);
		for (i = 0; i < tsb->tsb_ndesc; i++) {
			struct rtw_txsoft *ts;

			ts = &tsb->tsb_desc[i];
			ts->ts_mbuf = NULL;
			STAILQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
		}
		tsb->tsb_tx_timer = 0;
	}
}

static void
rtw_rxbuf_dma_map(void *arg, bus_dma_segment_t *seg, int nseg,
		  bus_size_t mapsize, int error)
{
	if (error)
		return;

	KASSERT(nseg == 1, ("too many rx mbuf seg"));

	*((bus_addr_t *)arg) = seg->ds_addr;
}

static int
rtw_rxsoft_alloc(struct rtw_softc *sc, struct rtw_rxsoft *rs, int waitok)
{
	bus_addr_t paddr;
	bus_dmamap_t map;
	struct mbuf *m;
	int rc;

	m = m_getcl(waitok ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return ENOBUFS;

	m->m_pkthdr.len = m->m_len = MCLBYTES;

	rc = bus_dmamap_load_mbuf(sc->sc_rxsoft_dmat, sc->sc_rxsoft_dmamap, m,
				  rtw_rxbuf_dma_map, &paddr,
				  waitok ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK);
	if (rc) {
		if_printf(&sc->sc_ic.ic_if, "can't load rx mbuf\n");
		m_freem(m);
		return rc;
	}

	if (rs->rs_mbuf != NULL)
		bus_dmamap_unload(sc->sc_rxsoft_dmat, rs->rs_dmamap);

	/* Swap DMA map */
	map = rs->rs_dmamap;
	rs->rs_dmamap = sc->sc_rxsoft_dmamap;
	sc->sc_rxsoft_dmamap = map;

	rs->rs_mbuf = m;
	rs->rs_phyaddr = paddr;

	bus_dmamap_sync(sc->sc_rxsoft_dmat, rs->rs_dmamap, BUS_DMASYNC_PREREAD);
	return 0;
}

static int
rtw_rxsoft_blk_init_all(struct rtw_softc *sc)
{
	int i, rc = 0;

	for (i = 0; i < RTW_RXQLEN; i++) {
		struct rtw_rxsoft *rs;

		rs = &sc->sc_rxsoft[i];
		/* we're in rtw_init, so there should be no mbufs allocated */
		KKASSERT(rs->rs_mbuf == NULL);
#ifdef RTW_DEBUG
		if (i == rtw_rxbufs_limit) {
			if_printf(&sc->sc_ic.ic_if,
				  "TEST hit %d-buffer limit\n", i);
			rc = ENOBUFS;
			break;
		}
#endif /* RTW_DEBUG */
		rc = rtw_rxsoft_alloc(sc, rs, 1);
		if (rc)
			break;
	}
	return rc;
}

static void
rtw_rxdesc_init(struct rtw_softc *sc, int idx, int kick)
{
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	struct rtw_rxdesc *rd = &rdb->rdb_desc[idx];
	struct rtw_rxsoft *rs = &sc->sc_rxsoft[idx];
	uint32_t ctl;

#ifdef RTW_DEBUG
	uint32_t octl, obuf;

	obuf = rd->rd_buf;
	octl = rd->rd_ctl;
#endif	/* RTW_DEBUG */

	rd->rd_buf = htole32(rs->rs_phyaddr);

	ctl = __SHIFTIN(rs->rs_mbuf->m_len, RTW_RXCTL_LENGTH_MASK) |
	      RTW_RXCTL_OWN | RTW_RXCTL_FS | RTW_RXCTL_LS;

	if (idx == rdb->rdb_ndesc - 1)
		ctl |= RTW_RXCTL_EOR;

	rd->rd_ctl = htole32(ctl);

	RTW_DPRINTF(kick ? (RTW_DEBUG_RECV_DESC | RTW_DEBUG_IO_KICK)
			 : RTW_DEBUG_RECV_DESC,
		    ("%s: rd %p buf %08x -> %08x ctl %08x -> %08x\n",
		     sc->sc_ic.ic_if.if_xname, rd, le32toh(obuf),
		     le32toh(rd->rd_buf), le32toh(octl), le32toh(rd->rd_ctl)));
}

static void
rtw_rxdesc_blk_init_all(struct rtw_softc *sc)
{
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	int i;

	for (i = 0; i < rdb->rdb_ndesc; i++)
		rtw_rxdesc_init(sc, i, 1);

	bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap, BUS_DMASYNC_PREWRITE);
}

static void
rtw_io_enable(struct rtw_softc *sc, uint8_t flags, int enable)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t cr;

	RTW_DPRINTF(RTW_DEBUG_IOSTATE,
		    ("%s: %s 0x%02x\n", sc->sc_ic.ic_if.if_xname,
		     enable ? "enable" : "disable", flags));

	cr = RTW_READ8(regs, RTW_CR);

	/* XXX reference source does not enable MULRW */
#if 0
	/* enable PCI Read/Write Multiple */
	cr |= RTW_CR_MULRW;
#endif

	RTW_RBW(regs, RTW_CR, RTW_CR);	/* XXX paranoia? */
	if (enable)
		cr |= flags;
	else
		cr &= ~flags;
	RTW_WRITE8(regs, RTW_CR, cr);
	RTW_SYNC(regs, RTW_CR, RTW_CR);
}

static void
rtw_intr_rx(struct rtw_softc *sc, uint16_t isr)
{
#define	IS_BEACON(__fc0)						\
    ((__fc0 & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==\
     (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON))

	/*
	 * convert rates:
	 * hardware -> net80211
	 */
	static const int ratetbl[4] = { 2, 4, 11, 22 };
	struct ifnet *ifp = &sc->sc_if;
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	int next, nproc = 0, sync = 0;

	KKASSERT(rdb->rdb_next < rdb->rdb_ndesc);

	bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap, BUS_DMASYNC_POSTREAD);

	for (next = rdb->rdb_next; ; next = (next + 1) % rdb->rdb_ndesc) {
		struct ieee80211_node *ni;
		struct ieee80211_frame_min *wh;
		struct rtw_rxdesc *rd;
		struct rtw_rxsoft *rs;
		struct mbuf *m;
		int hwrate, len, rate, rssi, sq, error;
		uint32_t hrssi, hstat, htsfth, htsftl;

		rd = &rdb->rdb_desc[next];
		rs = &sc->sc_rxsoft[next];

		hstat = le32toh(rd->rd_stat);
		hrssi = le32toh(rd->rd_rssi);
		htsfth = le32toh(rd->rd_tsfth);
		htsftl = le32toh(rd->rd_tsftl);

		RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
			    ("%s: rxdesc[%d] hstat %08x hrssi %08x "
			     "htsft %08x%08x\n", ifp->if_xname,
			     next, hstat, hrssi, htsfth, htsftl));

		++nproc;

		/* still belongs to NIC */
		if (hstat & RTW_RXSTAT_OWN) {
			if (nproc > 1)
				break;

			/* sometimes the NIC skips to the 0th descriptor */
			rd = &rdb->rdb_desc[0];
			if (rd->rd_stat & htole32(RTW_RXSTAT_OWN))
				break;
			RTW_DPRINTF(RTW_DEBUG_BUGS,
				    ("%s: NIC skipped from rxdesc[%u] "
				     "to rxdesc[0]\n", ifp->if_xname, next));
			next = rdb->rdb_ndesc - 1;
			continue;
		}

#ifdef RTW_DEBUG
#define PRINTSTAT(flag) do { \
	if ((hstat & flag) != 0) { \
		kprintf("%s" #flag, delim); \
		delim = ","; \
	} \
} while (0)
		if (rtw_debug & RTW_DEBUG_RECV_DESC) {
			const char *delim = "<";

			if_printf(ifp, "%s", "");
			if ((hstat & RTW_RXSTAT_DEBUG) != 0) {
				kprintf("status %08x", hstat);
				PRINTSTAT(RTW_RXSTAT_SPLCP);
				PRINTSTAT(RTW_RXSTAT_MAR);
				PRINTSTAT(RTW_RXSTAT_PAR);
				PRINTSTAT(RTW_RXSTAT_BAR);
				PRINTSTAT(RTW_RXSTAT_PWRMGT);
				PRINTSTAT(RTW_RXSTAT_CRC32);
				PRINTSTAT(RTW_RXSTAT_ICV);
				kprintf(">, ");
			}
		}
#endif /* RTW_DEBUG */

		if (hstat & RTW_RXSTAT_IOERROR) {
			if_printf(ifp, "DMA error/FIFO overflow %08x, "
				  "rx descriptor %d\n",
				  hstat & RTW_RXSTAT_IOERROR, next);
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto next;
		}

		len = __SHIFTOUT(hstat, RTW_RXSTAT_LENGTH_MASK);
		if (len < IEEE80211_MIN_LEN) {
			sc->sc_ic.ic_stats.is_rx_tooshort++;
			goto next;
		}

		/* CRC is included with the packet; trim it off. */
		len -= IEEE80211_CRC_LEN;

		hwrate = __SHIFTOUT(hstat, RTW_RXSTAT_RATE_MASK);
		if (hwrate >= NELEM(ratetbl)) {
			if_printf(ifp, "unknown rate #%d\n",
				  __SHIFTOUT(hstat, RTW_RXSTAT_RATE_MASK));
			IFNET_STAT_INC(ifp, ierrors, 1);
			goto next;
		}
		rate = ratetbl[hwrate];

#ifdef RTW_DEBUG
		RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
			    ("%s rate %d.%d Mb/s, time %08x%08x\n",
			     ifp->if_xname, (rate * 5) / 10,
			     (rate * 5) % 10, htsfth, htsftl));
#endif /* RTW_DEBUG */

		if ((hstat & RTW_RXSTAT_RES) &&
		    sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR)
			goto next;

		/* if bad flags, skip descriptor */
		if ((hstat & RTW_RXSTAT_ONESEG) != RTW_RXSTAT_ONESEG) {
			if_printf(ifp, "too many rx segments\n");
			goto next;
		}

		bus_dmamap_sync(sc->sc_rxsoft_dmat, rs->rs_dmamap,
				BUS_DMASYNC_POSTREAD);

		m = rs->rs_mbuf;

		/* if temporarily out of memory, re-use mbuf */
		error = rtw_rxsoft_alloc(sc, rs, 0);
		if (error) {
			if_printf(ifp, "%s: rtw_rxsoft_alloc(, %d) failed, "
			    "dropping packet\n", ifp->if_xname, next);
			goto next;
		}

		rssi = __SHIFTOUT(hrssi, RTW_RXRSSI_RSSI);
		sq = __SHIFTOUT(hrssi, RTW_RXRSSI_SQ);

		rssi = rtw_get_rssi(sc, rssi, sq);

		/*
		 * Note well: now we cannot recycle the rs_mbuf unless
		 * we restore its original length.
		 */
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = len;

		wh = mtod(m, struct ieee80211_frame_min *);

		if (!IS_BEACON(wh->i_fc[0]))
			sc->sc_led_state.ls_event |= RTW_LED_S_RX;

		/* TBD use _MAR, _BAR, _PAR flags as hints to _find_rxnode? */
		ni = ieee80211_find_rxnode(&sc->sc_ic, wh);

		sc->sc_tsfth = htsfth;

#ifdef RTW_DEBUG
		if ((ifp->if_flags & (IFF_DEBUG | IFF_LINK2)) ==
		    (IFF_DEBUG | IFF_LINK2)) {
			ieee80211_dump_pkt(mtod(m, uint8_t *), m->m_pkthdr.len,
					   rate, rssi);
		}
#endif /* RTW_DEBUG */

		if (sc->sc_radiobpf != NULL) {
			struct rtw_rx_radiotap_header *rr = &sc->sc_rxtap;

			rr->rr_tsft =
			    htole64(((uint64_t)htsfth << 32) | htsftl);

			if ((hstat & RTW_RXSTAT_SPLCP) != 0)
				rr->rr_flags = IEEE80211_RADIOTAP_F_SHORTPRE;

			rr->rr_flags = 0;
			rr->rr_rate = rate;
			rr->rr_antsignal = rssi;
			rr->rr_barker_lock = htole16(sq);

			bpf_ptap(sc->sc_radiobpf, m, rr, sizeof(sc->sc_rxtapu));
		}

		ieee80211_input(&sc->sc_ic, m, ni, rssi, htsftl);
		ieee80211_free_node(ni);
next:
		rtw_rxdesc_init(sc, next, 0);
		sync = 1;
	}

	if (sync) {
		bus_dmamap_sync(rdb->rdb_dmat, rdb->rdb_dmamap,
				BUS_DMASYNC_PREWRITE);
	}

	rdb->rdb_next = next;
	KKASSERT(rdb->rdb_next < rdb->rdb_ndesc);
#undef IS_BEACON
}

static __inline void
rtw_txsoft_release(bus_dma_tag_t dmat, struct rtw_txsoft *ts,
		   int data_retry, int rts_retry, int error, int ratectl)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	if (!ts->ts_ratectl)
		ratectl = 0;

	m = ts->ts_mbuf;
	ni = ts->ts_ni;
	KKASSERT(m != NULL);
	KKASSERT(ni != NULL);
	ts->ts_mbuf = NULL;
	ts->ts_ni = NULL;

	if (ratectl) {
		struct ieee80211_ratectl_res rc_res;

		rc_res.rc_res_rateidx = ts->ts_rateidx;
		rc_res.rc_res_tries = data_retry + 1;

		ieee80211_ratectl_tx_complete(ni, m->m_pkthdr.len,
					      &rc_res, 1,
					      data_retry, rts_retry,
					      error);
	}

	bus_dmamap_sync(dmat, ts->ts_dmamap, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(dmat, ts->ts_dmamap);
	m_freem(m);
	ieee80211_free_node(ni);
}

static __inline void
rtw_collect_txpkt(struct rtw_softc *sc, struct rtw_txdesc_blk *tdb,
		  struct rtw_txsoft *ts, int ndesc)
{
	uint32_t hstat;
	int data_retry, rts_retry, error;
	struct rtw_txdesc *tdn;
	const char *condstring;
	struct ifnet *ifp = &sc->sc_if;

	tdb->tdb_nfree += ndesc;

	tdn = &tdb->tdb_desc[ts->ts_last];

	hstat = le32toh(tdn->td_stat);
	rts_retry = __SHIFTOUT(hstat, RTW_TXSTAT_RTSRETRY_MASK);
	data_retry = __SHIFTOUT(hstat, RTW_TXSTAT_DRC_MASK);

	ifp->if_collisions += rts_retry + data_retry;

	if ((hstat & RTW_TXSTAT_TOK) != 0) {
		condstring = "ok";
		error = 0;
	} else {
		IFNET_STAT_INC(ifp, oerrors, 1);
		condstring = "error";
		error = 1;
	}

	rtw_txsoft_release(sc->sc_txsoft_dmat, ts, data_retry, rts_retry,
			   error, 1);

	DPRINTF(sc, RTW_DEBUG_XMIT_DESC,
		("%s: ts %p txdesc[%d, %d] %s tries rts %u data %u\n",
		 ifp->if_xname, ts, ts->ts_first, ts->ts_last,
		 condstring, rts_retry, data_retry));
}

static void
rtw_reset_oactive(struct rtw_softc *sc)
{
	int pri;
#ifdef RTW_DEBUG
	short oflags = sc->sc_if.if_flags;
#endif

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[pri];
		struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[pri];

		if (!STAILQ_EMPTY(&tsb->tsb_freeq) && tdb->tdb_nfree > 0)
			ifq_clr_oactive(&sc->sc_if.if_snd);
	}

#ifdef RTW_DEBUG
	if (oflags != sc->sc_if.if_flags) {
		DPRINTF(sc, RTW_DEBUG_OACTIVE,
			("%s: reset OACTIVE\n", sc->sc_ic.ic_if.if_xname));
	}
#endif
}

/* Collect transmitted packets. */
static __inline void
rtw_collect_txring(struct rtw_softc *sc, struct rtw_txsoft_blk *tsb,
		   struct rtw_txdesc_blk *tdb, int force)
{
	struct rtw_txsoft *ts;
	int ndesc;

	while ((ts = STAILQ_FIRST(&tsb->tsb_dirtyq)) != NULL) {
		ndesc = 1 + ts->ts_last - ts->ts_first;
		if (ts->ts_last < ts->ts_first)
			ndesc += tdb->tdb_ndesc;

		KKASSERT(ndesc > 0);

		bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
				BUS_DMASYNC_POSTREAD);

		if (force) {
			int i;

			for (i = ts->ts_first; ; i = RTW_NEXT_IDX(tdb, i)) {
				tdb->tdb_desc[i].td_stat &=
					~htole32(RTW_TXSTAT_OWN);
				if (i == ts->ts_last)
					break;
			}
			bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
					BUS_DMASYNC_PREWRITE);
		} else if ((tdb->tdb_desc[ts->ts_last].td_stat &
			    htole32(RTW_TXSTAT_OWN)) != 0) {
			break;
		}

		rtw_collect_txpkt(sc, tdb, ts, ndesc);
		STAILQ_REMOVE_HEAD(&tsb->tsb_dirtyq, ts_q);
		STAILQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
	}
	/* no more pending transmissions, cancel watchdog */ 
	if (ts == NULL)
		tsb->tsb_tx_timer = 0;
	rtw_reset_oactive(sc);
}

static void
rtw_intr_tx(struct rtw_softc *sc, uint16_t isr)
{
	int pri;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_collect_txring(sc, &sc->sc_txsoft_blk[pri],
				   &sc->sc_txdesc_blk[pri], 0);
	}
	if (isr) {
		rtw_start(&sc->sc_ic.ic_if,
		    ifq_get_subq_default(&sc->sc_ic.ic_if.if_snd));
	}
}

static __inline struct mbuf *
rtw_beacon_alloc(struct rtw_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_beacon_offsets	boff;
	struct mbuf *m;

	m = ieee80211_beacon_alloc(ic, ni, &boff);
	if (m != NULL) {
		RTW_DPRINTF(RTW_DEBUG_BEACON,
			    ("%s: m %p len %u\n", ic->ic_if.if_xname, m,
			     m->m_len));
	}
	return m;
}

static void
rtw_intr_beacon(struct rtw_softc *sc, uint16_t isr)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_regs *regs = &sc->sc_regs;
	struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[RTW_TXPRIBCN];
	struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[RTW_TXPRIBCN];

#ifdef RTW_DEBUG
	uint32_t tsfth, tsftl;

	tsfth = RTW_READ(regs, RTW_TSFTRH);
	tsftl = RTW_READ(regs, RTW_TSFTRL);
#endif

	if (isr & (RTW_INTR_TBDOK | RTW_INTR_TBDER)) {
#ifdef RTW_DEBUG
		int next = rtw_txring_next(regs, tdb);
#endif

		RTW_DPRINTF(RTW_DEBUG_BEACON,
			    ("%s: beacon ring %sprocessed, "
			     "isr = %#04x, next %d expected %d, %llu\n",
			     ic->ic_if.if_xname,
			     (next == tdb->tdb_next) ? "" : "un",
			     isr, next, tdb->tdb_next,
			     (uint64_t)tsfth << 32 | tsftl));

		if ((RTW_READ8(regs, RTW_TPPOLL) & RTW_TPPOLL_BQ) == 0){
			rtw_collect_txring(sc, tsb, tdb, 1);
			tdb->tdb_next = 0;
		}
	}
	/* Start beacon transmission. */

	if ((isr & RTW_INTR_BCNINT) && ic->ic_state == IEEE80211_S_RUN &&
	    STAILQ_EMPTY(&tsb->tsb_dirtyq)) {
		struct mbuf *m;

		RTW_DPRINTF(RTW_DEBUG_BEACON,
			    ("%s: beacon prep. time, isr = %#04x, %llu\n",
			     ic->ic_if.if_xname, isr,
			     (uint64_t)tsfth << 32 | tsftl));

		m = rtw_beacon_alloc(sc, ic->ic_bss);
		if (m == NULL) {
			if_printf(&ic->ic_if, "could not allocate beacon\n");
			return;
		}

		m->m_pkthdr.rcvif = (void *)ieee80211_ref_node(ic->ic_bss);

		IF_ENQUEUE(&sc->sc_beaconq, m);

		rtw_start(&ic->ic_if, ifq_get_subq_default(&ic->ic_if.if_snd));
	}
}

static void
rtw_intr_atim(struct rtw_softc *sc)
{
	/* TBD */
	return;
}

#ifdef RTW_DEBUG
static void
rtw_dump_rings(struct rtw_softc *sc)
{
	struct rtw_rxdesc_blk *rdb;
	int desc, pri;

	if ((rtw_debug & RTW_DEBUG_IO_KICK) == 0)
		return;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[pri];

		if_printf(&sc->sc_ic.ic_if, "txpri %d ndesc %d nfree %d\n",
			  pri, tdb->tdb_ndesc, tdb->tdb_nfree);
		for (desc = 0; desc < tdb->tdb_ndesc; desc++)
			rtw_print_txdesc(sc, ".", NULL, tdb, desc);
	}

	rdb = &sc->sc_rxdesc_blk;

	for (desc = 0; desc < RTW_RXQLEN; desc++) {
		struct rtw_rxdesc *rd = &rdb->rdb_desc[desc];

		if_printf(&sc->sc_ic.ic_if,
			  "%sctl %08x rsvd0/rssi %08x buf/tsftl %08x "
			  "rsvd1/tsfth %08x\n",
			  (desc >= rdb->rdb_ndesc) ? "UNUSED " : "",
			  le32toh(rd->rd_ctl), le32toh(rd->rd_rssi),
			  le32toh(rd->rd_buf), le32toh(rd->rd_tsfth));
	}
}
#endif /* RTW_DEBUG */

static void
rtw_hwring_setup(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	int pri;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[pri];

		RTW_WRITE(regs, tdb->tdb_basereg, tdb->tdb_base);
		RTW_DPRINTF(RTW_DEBUG_XMIT_DESC,
			    ("%s: reg[tdb->tdb_basereg] <- %u\n",
			     sc->sc_ic.ic_if.if_xname, tdb->tdb_base));
	}

	RTW_WRITE(regs, RTW_RDSAR, rdb->rdb_base);
	RTW_DPRINTF(RTW_DEBUG_RECV_DESC,
		    ("%s: reg[RDSAR] <- %u\n", sc->sc_ic.ic_if.if_xname,
		     rdb->rdb_base));

	RTW_SYNC(regs, RTW_TLPDA, RTW_RDSAR);
}

static int
rtw_swring_setup(struct rtw_softc *sc)
{
	int rc;

	rtw_txdesc_blk_init_all(sc);
	rtw_txsoft_blk_init_all(sc);

	rc = rtw_rxsoft_blk_init_all(sc);
	if (rc) {
		if_printf(&sc->sc_ic.ic_if, "could not allocate rx buffers\n");
		return rc;
	}

	rtw_rxdesc_blk_init_all(sc);
	sc->sc_rxdesc_blk.rdb_next = 0;
	return 0;
}

static int
rtw_txring_next(struct rtw_regs *regs, struct rtw_txdesc_blk *tdb)
{
	return (le32toh(RTW_READ(regs, tdb->tdb_basereg)) - tdb->tdb_base) /
		sizeof(struct rtw_txdesc);
}

static void
rtw_txring_fixup(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	int pri;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[pri];
		int next;

		next = rtw_txring_next(regs, tdb);
		if (tdb->tdb_next == next)
			continue;
		if_printf(&sc->sc_ic.ic_if,
			  "tx-ring %d expected next %d, read %d\n",
			  pri, tdb->tdb_next, next);
		tdb->tdb_next = MIN(next, tdb->tdb_ndesc - 1);
	}
}

static void
rtw_rxring_fixup(struct rtw_softc *sc)
{
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	uint32_t rdsar;
	int next;

	rdsar = le32toh(RTW_READ(&sc->sc_regs, RTW_RDSAR));
	next = (rdsar - rdb->rdb_base) / sizeof(struct rtw_rxdesc);

	if (rdb->rdb_next != next) {
		if_printf(&sc->sc_ic.ic_if,
			  "rx-ring expected next %d, read %d\n",
			  rdb->rdb_next, next);
		rdb->rdb_next = MIN(next, rdb->rdb_ndesc - 1);
	}
}

static void
rtw_txdesc_blk_reset_all(struct rtw_softc *sc)
{
	int pri;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		rtw_collect_txring(sc, &sc->sc_txsoft_blk[pri],
				   &sc->sc_txdesc_blk[pri], 1);
	}
}

static void
rtw_intr_ioerror(struct rtw_softc *sc, uint16_t isr)
{
	struct rtw_regs *regs = &sc->sc_regs;
	int xmtr = 0, rcvr = 0;
	uint8_t cr = 0;

	if (isr & RTW_INTR_TXFOVW) {
		if_printf(&sc->sc_ic.ic_if, "tx fifo underflow\n");
		rcvr = xmtr = 1;
		cr |= RTW_CR_TE | RTW_CR_RE;
	}

	if (isr & (RTW_INTR_RDU | RTW_INTR_RXFOVW)) {
		cr |= RTW_CR_RE;
		rcvr = 1;
	}

	RTW_DPRINTF(RTW_DEBUG_BUGS,
		    ("%s: restarting xmit/recv, isr %04x\n",
		     sc->sc_ic.ic_if.if_xname, isr));

#ifdef RTW_DEBUG
	rtw_dump_rings(sc);
#endif /* RTW_DEBUG */

	rtw_io_enable(sc, cr, 0);

	/* Collect rx'd packets.  Refresh rx buffers. */
	if (rcvr)
		rtw_intr_rx(sc, 0);

	/*
	 * Collect tx'd packets.
	 * XXX let's hope this stops the transmit timeouts.
	 */
	if (xmtr)
		rtw_txdesc_blk_reset_all(sc);

	RTW_WRITE16(regs, RTW_IMR, 0);
	RTW_SYNC(regs, RTW_IMR, RTW_IMR);

	if (rtw_do_chip_reset) {
		rtw_chip_reset1(sc);
		rtw_wep_setkeys(sc);
	}

	rtw_rxdesc_blk_init_all(sc);

#ifdef RTW_DEBUG
	rtw_dump_rings(sc);
#endif /* RTW_DEBUG */

	RTW_WRITE16(regs, RTW_IMR, sc->sc_inten);
	RTW_SYNC(regs, RTW_IMR, RTW_IMR);

	if (rcvr)
		rtw_rxring_fixup(sc);

	rtw_io_enable(sc, cr, 1);

	if (xmtr)
		rtw_txring_fixup(sc);
}

static __inline void
rtw_suspend_ticks(struct rtw_softc *sc)
{
	RTW_DPRINTF(RTW_DEBUG_TIMEOUT,
		    ("%s: suspending ticks\n", sc->sc_ic.ic_if.if_xname));
	sc->sc_do_tick = 0;
}

static void
rtw_resume_ticks(struct rtw_softc *sc)
{
	uint32_t tsftrl0, tsftrl1, next_tick;
	struct rtw_regs *regs = &sc->sc_regs;

	tsftrl0 = RTW_READ(regs, RTW_TSFTRL);

	tsftrl1 = RTW_READ(regs, RTW_TSFTRL);
	next_tick = tsftrl1 + 1000000;
	RTW_WRITE(regs, RTW_TINT, next_tick);

	sc->sc_do_tick = 1;

	RTW_DPRINTF(RTW_DEBUG_TIMEOUT,
		    ("%s: resume ticks delta %#08x now %#08x next %#08x\n",
		     sc->sc_ic.ic_if.if_xname, tsftrl1 - tsftrl0, tsftrl1,
		     next_tick));
}

static void
rtw_intr_timeout(struct rtw_softc *sc)
{
	RTW_DPRINTF(RTW_DEBUG_TIMEOUT,
		    ("%s: timeout\n", sc->sc_ic.ic_if.if_xname));
	if (sc->sc_do_tick)
		rtw_resume_ticks(sc);
}

static void
rtw_intr(void *arg)
{
	struct rtw_softc *sc = arg;
	struct rtw_regs *regs = &sc->sc_regs;
	struct ifnet *ifp = &sc->sc_if;
	int i;

	/*
	 * If the interface isn't running, the interrupt couldn't
	 * possibly have come from us.
	 */
	if ((sc->sc_flags & RTW_F_ENABLED) == 0 ||
	    (ifp->if_flags & IFF_RUNNING) == 0) {
		RTW_DPRINTF(RTW_DEBUG_INTR,
			    ("%s: stray interrupt\n", ifp->if_xname));
		return;
	}

	for (i = 0; i < 10; i++) {
		uint16_t isr;

		isr = RTW_READ16(regs, RTW_ISR);

		RTW_WRITE16(regs, RTW_ISR, isr);
		RTW_WBR(regs, RTW_ISR, RTW_ISR);

		if (sc->sc_intr_ack != NULL)
			sc->sc_intr_ack(regs);

		if (isr == 0)
			break;

#ifdef RTW_DEBUG
#define PRINTINTR(flag) do { \
	if ((isr & flag) != 0) { \
		kprintf("%s" #flag, delim); \
		delim = ","; \
	} \
} while (0)

		if ((rtw_debug & RTW_DEBUG_INTR) != 0 && isr != 0) {
			const char *delim = "<";

			if_printf(ifp, "reg[ISR] = %x", isr);

			PRINTINTR(RTW_INTR_TXFOVW);
			PRINTINTR(RTW_INTR_TIMEOUT);
			PRINTINTR(RTW_INTR_BCNINT);
			PRINTINTR(RTW_INTR_ATIMINT);
			PRINTINTR(RTW_INTR_TBDER);
			PRINTINTR(RTW_INTR_TBDOK);
			PRINTINTR(RTW_INTR_THPDER);
			PRINTINTR(RTW_INTR_THPDOK);
			PRINTINTR(RTW_INTR_TNPDER);
			PRINTINTR(RTW_INTR_TNPDOK);
			PRINTINTR(RTW_INTR_RXFOVW);
			PRINTINTR(RTW_INTR_RDU);
			PRINTINTR(RTW_INTR_TLPDER);
			PRINTINTR(RTW_INTR_TLPDOK);
			PRINTINTR(RTW_INTR_RER);
			PRINTINTR(RTW_INTR_ROK);

			kprintf(">\n");
		}
#undef PRINTINTR
#endif /* RTW_DEBUG */

		if (isr & RTW_INTR_RX)
			rtw_intr_rx(sc, isr & RTW_INTR_RX);
		if (isr & RTW_INTR_TX)
			rtw_intr_tx(sc, isr & RTW_INTR_TX);
		if (isr & RTW_INTR_BEACON)
			rtw_intr_beacon(sc, isr & RTW_INTR_BEACON);
		if (isr & RTW_INTR_ATIMINT)
			rtw_intr_atim(sc);
		if (isr & RTW_INTR_IOERROR)
			rtw_intr_ioerror(sc, isr & RTW_INTR_IOERROR);
		if (isr & RTW_INTR_TIMEOUT)
			rtw_intr_timeout(sc);
	}
}

/* Must be called at splnet. */
void
rtw_stop(struct rtw_softc *sc, int disable)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rtw_regs *regs = &sc->sc_regs;
	int i;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	rtw_suspend_ticks(sc);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	if ((sc->sc_flags & RTW_F_INVALID) == 0) {
		/* Disable interrupts. */
		RTW_WRITE16(regs, RTW_IMR, 0);

		RTW_WBW(regs, RTW_TPPOLL, RTW_IMR);

		/*
		 * Stop the transmit and receive processes. First stop DMA,
		 * then disable receiver and transmitter.
		 */
		RTW_WRITE8(regs, RTW_TPPOLL, RTW_TPPOLL_SALL);

		RTW_SYNC(regs, RTW_TPPOLL, RTW_IMR);

		rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 0);
	}

	/* Free pending TX mbufs */
	for (i = 0; i < RTW_NTXPRI; ++i) {
		struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[i];
		struct rtw_txsoft *ts;

		while ((ts = STAILQ_FIRST(&tsb->tsb_dirtyq)) != NULL) {
			rtw_txsoft_release(sc->sc_txsoft_dmat, ts, 0, 0, 0, 0);
			STAILQ_REMOVE_HEAD(&tsb->tsb_dirtyq, ts_q);
			STAILQ_INSERT_TAIL(&tsb->tsb_freeq, ts, ts_q);
		}
		tsb->tsb_tx_timer = 0;
	}

	/* Free pending RX mbufs */
	for (i = 0; i < RTW_RXQLEN; i++) {
		struct rtw_rxsoft *rs = &sc->sc_rxsoft[i];

		if (rs->rs_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_rxsoft_dmat, rs->rs_dmamap,
					BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_rxsoft_dmat, rs->rs_dmamap);
			m_freem(rs->rs_mbuf);
			rs->rs_mbuf = NULL;
		}
	}

	if (disable)
		rtw_disable(sc);

	/* Mark the interface as not running.  Cancel the watchdog timer. */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
}

#ifdef RTW_DEBUG
const char *
rtw_pwrstate_string(enum rtw_pwrstate power)
{
	switch (power) {
	case RTW_ON:
		return "on";
	case RTW_SLEEP:
		return "sleep";
	case RTW_OFF:
		return "off";
	default:
		return "unknown";
	}
}
#endif	/* RTW_DEBUG */

/*
 * XXX For Maxim, I am using the RFMD settings gleaned from the
 * reference driver, plus a magic Maxim "ON" value that comes from
 * the Realtek document "Windows PG for Rtl8180."
 */
static void
rtw_maxim_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
		   int before_rf, int digphy)
{
	uint32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_MAXIM_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM, RTW_ANAPARM);
}

/* XXX I am using the RFMD settings gleaned from the reference
 * driver.  They agree
 */
static void
rtw_rfmd_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
		  int before_rf, int digphy)
{
	uint32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_RFMD_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM, RTW_ANAPARM);
}

static void
rtw_philips_pwrstate(struct rtw_regs *regs, enum rtw_pwrstate power,
		     int before_rf, int digphy)
{
	uint32_t anaparm;

	anaparm = RTW_READ(regs, RTW_ANAPARM);
	anaparm &= ~(RTW_ANAPARM_RFPOW_MASK | RTW_ANAPARM_TXDACOFF);

	switch (power) {
	case RTW_OFF:
		if (before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_PHILIPS_OFF;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_SLEEP:
		if (!before_rf)
			return;
		anaparm |= RTW_ANAPARM_RFPOW_PHILIPS_SLEEP;
		anaparm |= RTW_ANAPARM_TXDACOFF;
		break;
	case RTW_ON:
		if (!before_rf)
			return;
		if (digphy) {
			anaparm |= RTW_ANAPARM_RFPOW_DIG_PHILIPS_ON;
			/* XXX guess */
			anaparm |= RTW_ANAPARM_TXDACOFF;
		} else
			anaparm |= RTW_ANAPARM_RFPOW_ANA_PHILIPS_ON;
		break;
	}
	RTW_DPRINTF(RTW_DEBUG_PWR,
	    ("%s: power state %s, %s RF, reg[ANAPARM] <- %08x\n",
	    __func__, rtw_pwrstate_string(power),
	    (before_rf) ? "before" : "after", anaparm));

	RTW_WRITE(regs, RTW_ANAPARM, anaparm);
	RTW_SYNC(regs, RTW_ANAPARM, RTW_ANAPARM);
}

static __inline void
rtw_pwrstate0(struct rtw_softc *sc, enum rtw_pwrstate power, int before_rf,
	      int digphy)
{
	rtw_set_access(sc, RTW_ACCESS_ANAPARM);
	sc->sc_pwrstate_cb(&sc->sc_regs, power, before_rf, digphy);
	rtw_set_access(sc, RTW_ACCESS_NONE);
}

static int
rtw_pwrstate(struct rtw_softc *sc, enum rtw_pwrstate power)
{
	int rc;

	RTW_DPRINTF(RTW_DEBUG_PWR,
		    ("%s: %s->%s\n", sc->sc_ic.ic_if.if_xname,
		    rtw_pwrstate_string(sc->sc_pwrstate),
		    rtw_pwrstate_string(power)));

	if (sc->sc_pwrstate == power)
		return 0;

	rtw_pwrstate0(sc, power, 1, sc->sc_flags & RTW_F_DIGPHY);
	rc = rtw_rf_pwrstate(sc->sc_rf, power);
	rtw_pwrstate0(sc, power, 0, sc->sc_flags & RTW_F_DIGPHY);

	switch (power) {
	case RTW_ON:
		/* TBD set LEDs */
		break;
	case RTW_SLEEP:
		/* TBD */
		break;
	case RTW_OFF:
		/* TBD */
		break;
	}
	if (rc == 0)
		sc->sc_pwrstate = power;
	else
		sc->sc_pwrstate = RTW_OFF;
	return rc;
}

static int
rtw_tune(struct rtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw_tx_radiotap_header *rt = &sc->sc_txtap;
	struct rtw_rx_radiotap_header *rr = &sc->sc_rxtap;
	u_int chan;
	int rc, antdiv, dflantb;

	antdiv = sc->sc_flags & RTW_F_ANTDIV;
	dflantb = sc->sc_flags & RTW_F_DFLANTB;

	chan = ieee80211_chan2ieee(ic, ic->ic_curchan);
	if (chan == IEEE80211_CHAN_ANY)
		panic("%s: chan == IEEE80211_CHAN_ANY", ic->ic_if.if_xname);

	rt->rt_chan_freq = htole16(ic->ic_curchan->ic_freq);
	rt->rt_chan_flags = htole16(ic->ic_curchan->ic_flags);

	rr->rr_chan_freq = htole16(ic->ic_curchan->ic_freq);
	rr->rr_chan_flags = htole16(ic->ic_curchan->ic_flags);

	if (chan == sc->sc_cur_chan) {
		RTW_DPRINTF(RTW_DEBUG_TUNE,
			    ("%s: already tuned chan #%d\n",
			     ic->ic_if.if_xname, chan));
		return 0;
	}

	rtw_suspend_ticks(sc);

	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 0);

	/* TBD wait for Tx to complete */

	KKASSERT((sc->sc_flags & RTW_F_ENABLED) != 0);

	rc = rtw_phy_init(&sc->sc_regs, sc->sc_rf,
			  rtw_chan2txpower(&sc->sc_srom, ic, ic->ic_curchan),
			  sc->sc_csthr, ic->ic_curchan->ic_freq, antdiv,
			  dflantb, RTW_ON);
	if (rc != 0) {
		/* XXX condition on powersaving */
		kprintf("%s: phy init failed\n", ic->ic_if.if_xname);
	}

	sc->sc_cur_chan = chan;

	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 1);

	rtw_resume_ticks(sc);

	return rc;
}

static void
rtw_disable(struct rtw_softc *sc)
{
	int rc;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	/* turn off PHY */
	if ((sc->sc_flags & RTW_F_INVALID) == 0 &&
	    (rc = rtw_pwrstate(sc, RTW_OFF)) != 0)
		if_printf(&sc->sc_ic.ic_if, "failed to turn off PHY\n");

	sc->sc_flags &= ~RTW_F_ENABLED;
}

static int
rtw_enable(struct rtw_softc *sc)
{
	if ((sc->sc_flags & RTW_F_ENABLED) == 0) {
		sc->sc_flags |= RTW_F_ENABLED;
		/*
		 * Power may have been removed, and WEP keys thus reset.
		 */
		sc->sc_flags &= ~RTW_F_DK_VALID;
	}
	return (0);
}

static void
rtw_transmit_config(struct rtw_regs *regs)
{
	uint32_t tcr;

	tcr = RTW_READ(regs, RTW_TCR);

	tcr |= RTW_TCR_CWMIN;
	tcr &= ~RTW_TCR_MXDMA_MASK;
	tcr |= RTW_TCR_MXDMA_256;
	tcr |= RTW_TCR_SAT;		/* send ACK as fast as possible */
	tcr &= ~RTW_TCR_LBK_MASK;
	tcr |= RTW_TCR_LBK_NORMAL;	/* normal operating mode */

	/* set short/long retry limits */
	tcr &= ~(RTW_TCR_SRL_MASK|RTW_TCR_LRL_MASK);
	tcr |= __SHIFTIN(4, RTW_TCR_SRL_MASK) | __SHIFTIN(4, RTW_TCR_LRL_MASK);

	tcr &= ~RTW_TCR_CRC;	/* NIC appends CRC32 */

	RTW_WRITE(regs, RTW_TCR, tcr);
	RTW_SYNC(regs, RTW_TCR, RTW_TCR);
}

static void
rtw_enable_interrupts(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;

	sc->sc_inten = RTW_INTR_RX|RTW_INTR_TX|RTW_INTR_BEACON|RTW_INTR_ATIMINT;
	sc->sc_inten |= RTW_INTR_IOERROR|RTW_INTR_TIMEOUT;

	RTW_WRITE16(regs, RTW_IMR, sc->sc_inten);
	RTW_WBW(regs, RTW_IMR, RTW_ISR);
	RTW_WRITE16(regs, RTW_ISR, 0xffff);
	RTW_SYNC(regs, RTW_IMR, RTW_ISR);

	/* XXX necessary? */
	if (sc->sc_intr_ack != NULL)
		sc->sc_intr_ack(regs);
}

static void
rtw_set_nettype(struct rtw_softc *sc, enum ieee80211_opmode opmode)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t msr;

	/* I'm guessing that MSR is protected as CONFIG[0123] are. */
	rtw_set_access(sc, RTW_ACCESS_CONFIG);

	msr = RTW_READ8(regs, RTW_MSR) & ~RTW_MSR_NETYPE_MASK;

	switch (opmode) {
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_IBSS:
		msr |= RTW_MSR_NETYPE_ADHOC_OK;
		break;
	case IEEE80211_M_HOSTAP:
		msr |= RTW_MSR_NETYPE_AP_OK;
		break;
	case IEEE80211_M_MONITOR:
		/* XXX */
		msr |= RTW_MSR_NETYPE_NOLINK;
		break;
	case IEEE80211_M_STA:
		msr |= RTW_MSR_NETYPE_INFRA_OK;
		break;
	}
	RTW_WRITE8(regs, RTW_MSR, msr);

	rtw_set_access(sc, RTW_ACCESS_NONE);
}

#define	rtw_calchash(addr) \
	(ether_crc32_be((addr), IEEE80211_ADDR_LEN) >> 26)

static void
rtw_pktfilt_load(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ifmultiaddr *ifma;
	uint32_t hashes[2] = { 0, 0 };
	int hash;

	/* XXX might be necessary to stop Rx/Tx engines while setting filters */

	sc->sc_rcr &= ~RTW_RCR_PKTFILTER_MASK;
	sc->sc_rcr &= ~(RTW_RCR_MXDMA_MASK | RTW_RCR_RXFTH_MASK);

	sc->sc_rcr |= RTW_RCR_PKTFILTER_DEFAULT;
	/* MAC auto-reset PHY (huh?) */
	sc->sc_rcr |= RTW_RCR_ENMARP;
	/* DMA whole Rx packets, only.  Set Tx DMA burst size to 1024 bytes. */
	sc->sc_rcr |= RTW_RCR_MXDMA_1024 | RTW_RCR_RXFTH_WHOLE;

	switch (ic->ic_opmode) {
	case IEEE80211_M_MONITOR:
		sc->sc_rcr |= RTW_RCR_MONITOR;
		break;
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_IBSS:
		/* receive broadcasts in our BSS */
		sc->sc_rcr |= RTW_RCR_ADD3;
		break;
	default:
		break;
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

	/* XXX accept all broadcast if scanning */
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		sc->sc_rcr |= RTW_RCR_AB;	/* accept all broadcast */

	if (ifp->if_flags & IFF_PROMISC) {
		sc->sc_rcr |= RTW_RCR_AB;	/* accept all broadcast */
allmulti:
		ifp->if_flags |= IFF_ALLMULTI;
		goto setit;
	}

	/*
	 * Program the 64-bit multicast hash filter.
	 */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		hash = rtw_calchash(
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		hashes[hash >> 5] |= (1 << (hash & 0x1f));
		sc->sc_rcr |= RTW_RCR_AM;
	}

	/* all bits set => hash is useless */
	if (~(hashes[0] & hashes[1]) == 0)
		goto allmulti;

setit:
	if (ifp->if_flags & IFF_ALLMULTI) {
		sc->sc_rcr |= RTW_RCR_AM;	/* accept all multicast */
		hashes[0] = hashes[1] = 0xffffffff;
	}

	RTW_WRITE(regs, RTW_MAR0, hashes[0]);
	RTW_WRITE(regs, RTW_MAR1, hashes[1]);
	RTW_WRITE(regs, RTW_RCR, sc->sc_rcr);
	RTW_SYNC(regs, RTW_MAR0, RTW_RCR); /* RTW_MAR0 < RTW_MAR1 < RTW_RCR */

	DPRINTF(sc, RTW_DEBUG_PKTFILT,
		("%s: RTW_MAR0 %08x RTW_MAR1 %08x RTW_RCR %08x\n",
		 ifp->if_xname, RTW_READ(regs, RTW_MAR0),
		 RTW_READ(regs, RTW_MAR1), RTW_READ(regs, RTW_RCR)));
}

/* Must be called at splnet. */
static void
rtw_init(void *xsc)
{
	struct rtw_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rtw_regs *regs = &sc->sc_regs;
	int rc = 0;

	rc = rtw_enable(sc);
	if (rc)
		goto out;

	/* Cancel pending I/O and reset. */
	rtw_stop(sc, 0);

	DPRINTF(sc, RTW_DEBUG_TUNE,
		("%s: channel %d freq %d flags 0x%04x\n", ifp->if_xname,
		ieee80211_chan2ieee(ic, ic->ic_curchan),
		ic->ic_curchan->ic_freq, ic->ic_curchan->ic_flags));

	rc = rtw_pwrstate(sc, RTW_OFF);
	if (rc)
		goto out;

	rc = rtw_swring_setup(sc);
	if (rc)
		goto out;

	rtw_transmit_config(regs);

	rtw_set_access(sc, RTW_ACCESS_CONFIG);

	RTW_WRITE8(regs, RTW_MSR, 0x0);	/* no link */
	RTW_WBW(regs, RTW_MSR, RTW_BRSR);

	/* long PLCP header, 1Mb/2Mb basic rate */
	RTW_WRITE16(regs, RTW_BRSR, RTW_BRSR_MBR8180_2MBPS);
	RTW_SYNC(regs, RTW_BRSR, RTW_BRSR);

	rtw_set_access(sc, RTW_ACCESS_ANAPARM);
	rtw_set_access(sc, RTW_ACCESS_NONE);

	/* XXX from reference sources */
	RTW_WRITE(regs, RTW_FEMR, 0xffff);
	RTW_SYNC(regs, RTW_FEMR, RTW_FEMR);

	rtw_set_rfprog(sc);

	RTW_WRITE8(regs, RTW_PHYDELAY, sc->sc_phydelay);
	/* from Linux driver */
	RTW_WRITE8(regs, RTW_CRCOUNT, RTW_CRCOUNT_MAGIC);

	RTW_SYNC(regs, RTW_PHYDELAY, RTW_CRCOUNT);

	rtw_enable_interrupts(sc);

	rtw_pktfilt_load(sc);

	rtw_hwring_setup(sc);

	rtw_wep_setkeys(sc);

	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 1);

	ifp->if_flags |= IFF_RUNNING;
	ic->ic_state = IEEE80211_S_INIT;

	RTW_WRITE16(regs, RTW_BSSID16, 0x0);
	RTW_WRITE(regs, RTW_BSSID32, 0x0);

	rtw_resume_ticks(sc);

	rtw_set_nettype(sc, IEEE80211_M_MONITOR);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

out:
	if (rc)
		if_printf(ifp, "interface not running\n");
}

static void
rtw_led_init(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t cfg0, cfg1;

	rtw_set_access(sc, RTW_ACCESS_CONFIG);

	cfg0 = RTW_READ8(regs, RTW_CONFIG0);
	cfg0 |= RTW_CONFIG0_LEDGPOEN;
	RTW_WRITE8(regs, RTW_CONFIG0, cfg0);

	cfg1 = RTW_READ8(regs, RTW_CONFIG1);
	RTW_DPRINTF(RTW_DEBUG_LED,
		    ("%s: read %02x from reg[CONFIG1]\n",
		     sc->sc_ic.ic_if.if_xname, cfg1));

	cfg1 &= ~RTW_CONFIG1_LEDS_MASK;
	cfg1 |= RTW_CONFIG1_LEDS_TX_RX;
	RTW_WRITE8(regs, RTW_CONFIG1, cfg1);

	rtw_set_access(sc, RTW_ACCESS_NONE);
}

/*
 * IEEE80211_S_INIT: 		LED1 off
 *
 * IEEE80211_S_AUTH,
 * IEEE80211_S_ASSOC,
 * IEEE80211_S_SCAN: 		LED1 blinks @ 1 Hz, blinks at 5Hz for tx/rx
 *
 * IEEE80211_S_RUN: 		LED1 on, blinks @ 5Hz for tx/rx
 */
static void
rtw_led_newstate(struct rtw_softc *sc, enum ieee80211_state nstate)
{
	struct rtw_led_state *ls = &sc->sc_led_state;

	switch (nstate) {
	case IEEE80211_S_INIT:
		rtw_led_init(sc);
		callout_stop(&ls->ls_slow_ch);
		callout_stop(&ls->ls_fast_ch);
		ls->ls_slowblink = 0;
		ls->ls_actblink = 0;
		ls->ls_default = 0;
		break;
	case IEEE80211_S_SCAN:
		callout_reset(&ls->ls_slow_ch, RTW_LED_SLOW_TICKS,
			      rtw_led_slowblink, sc);
		callout_reset(&ls->ls_fast_ch, RTW_LED_FAST_TICKS,
			      rtw_led_fastblink, sc);
		/*FALLTHROUGH*/
	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		ls->ls_default = RTW_LED1;
		ls->ls_actblink = RTW_LED1;
		ls->ls_slowblink = RTW_LED1;
		break;
	case IEEE80211_S_RUN:
		ls->ls_slowblink = 0;
		break;
	}
	rtw_led_set(sc);
}

static void
rtw_led_set(struct rtw_softc *sc)
{
	struct rtw_led_state *ls = &sc->sc_led_state;
	struct rtw_regs *regs = &sc->sc_regs;
	uint8_t led_condition, mask, newval, val;
	bus_size_t ofs;

	led_condition = ls->ls_default;

	if (ls->ls_state & RTW_LED_S_SLOW)
		led_condition ^= ls->ls_slowblink;
	if (ls->ls_state & (RTW_LED_S_RX|RTW_LED_S_TX))
		led_condition ^= ls->ls_actblink;

	RTW_DPRINTF(RTW_DEBUG_LED,
		    ("%s: LED condition %02x\n", sc->sc_ic.ic_if.if_xname,
		     led_condition));

	switch (sc->sc_hwverid) {
	default:
	case 'F':
		ofs = RTW_PSR;
		newval = mask = RTW_PSR_LEDGPO0 | RTW_PSR_LEDGPO1;
		if (led_condition & RTW_LED0)
			newval &= ~RTW_PSR_LEDGPO0;
		if (led_condition & RTW_LED1)
			newval &= ~RTW_PSR_LEDGPO1;
		break;
	case 'D':
		ofs = RTW_9346CR;
		mask = RTW_9346CR_EEM_MASK | RTW_9346CR_EEDI | RTW_9346CR_EECS;
		newval = RTW_9346CR_EEM_PROGRAM;
		if (led_condition & RTW_LED0)
			newval |= RTW_9346CR_EEDI;
		if (led_condition & RTW_LED1)
			newval |= RTW_9346CR_EECS;
		break;
	}
	val = RTW_READ8(regs, ofs);
	RTW_DPRINTF(RTW_DEBUG_LED,
		    ("%s: read %02x from reg[%02x]\n",
		     sc->sc_ic.ic_if.if_xname, val, ofs));
	val &= ~mask;
	val |= newval;
	RTW_WRITE8(regs, ofs, val);
	RTW_DPRINTF(RTW_DEBUG_LED,
		    ("%s: wrote %02x to reg[%02x]\n",
		     sc->sc_ic.ic_if.if_xname, val, ofs));
	RTW_SYNC(regs, ofs, ofs);
}

static void
rtw_led_fastblink(void *arg)
{
	struct rtw_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct rtw_led_state *ls = &sc->sc_led_state;
	int ostate;

	lwkt_serialize_enter(ifp->if_serializer);

	ostate = ls->ls_state;
	ls->ls_state ^= ls->ls_event;

	if ((ls->ls_event & RTW_LED_S_TX) == 0)
		ls->ls_state &= ~RTW_LED_S_TX;

	if ((ls->ls_event & RTW_LED_S_RX) == 0)
		ls->ls_state &= ~RTW_LED_S_RX;

	ls->ls_event = 0;

	if (ostate != ls->ls_state)
		rtw_led_set(sc);

	callout_reset(&ls->ls_fast_ch, RTW_LED_FAST_TICKS,
		      rtw_led_fastblink, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
rtw_led_slowblink(void *arg)
{
	struct rtw_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct rtw_led_state *ls = &sc->sc_led_state;

	lwkt_serialize_enter(ifp->if_serializer);

	ls->ls_state ^= RTW_LED_S_SLOW;
	rtw_led_set(sc);
	callout_reset(&ls->ls_slow_ch, RTW_LED_SLOW_TICKS,
		      rtw_led_slowblink, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
rtw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct rtw_softc *sc = ifp->if_softc;
	int rc = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				rtw_init(sc);
			RTW_PRINT_REGS(&sc->sc_regs, ifp->if_xname, __func__);
		} else if (sc->sc_flags & RTW_F_ENABLED) {
			RTW_PRINT_REGS(&sc->sc_regs, ifp->if_xname, __func__);
			rtw_stop(sc, 1);
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			rtw_pktfilt_load(sc);
		break;
	default:
		rc = ieee80211_ioctl(&sc->sc_ic, cmd, data, cr);
		if (rc == ENETRESET) {
			if (sc->sc_flags & RTW_F_ENABLED)
				rtw_init(sc);
			rc = 0;
		}
		break;
	}
	return rc;
}

/*
 * Select a transmit ring with at least one h/w and s/w descriptor free.
 * Return 0 on success, -1 on failure.
 */
static __inline int
rtw_txring_choose(struct rtw_softc *sc, struct rtw_txsoft_blk **tsbp,
		  struct rtw_txdesc_blk **tdbp, int pri)
{
	struct rtw_txsoft_blk *tsb;
	struct rtw_txdesc_blk *tdb;

	KKASSERT(pri >= 0 && pri < RTW_NTXPRI);

	tsb = &sc->sc_txsoft_blk[pri];
	tdb = &sc->sc_txdesc_blk[pri];

	if (STAILQ_EMPTY(&tsb->tsb_freeq) || tdb->tdb_nfree == 0) {
		if (tsb->tsb_tx_timer == 0)
			tsb->tsb_tx_timer = 5;
		*tsbp = NULL;
		*tdbp = NULL;
		return -1;
	}
	*tsbp = tsb;
	*tdbp = tdb;
	return 0;
}

static __inline struct mbuf *
rtw_80211_dequeue(struct rtw_softc *sc, struct ifqueue *ifq, int pri,
		  struct rtw_txsoft_blk **tsbp, struct rtw_txdesc_blk **tdbp,
		  struct ieee80211_node **nip)
{
	struct mbuf *m;
	struct ifnet *ifp = &sc->sc_if;

	if (IF_QEMPTY(ifq))
		return NULL;
	if (rtw_txring_choose(sc, tsbp, tdbp, pri) == -1) {
		DPRINTF(sc, RTW_DEBUG_XMIT_RSRC,
			("%s: no ring %d descriptor\n", ifp->if_xname, pri));
		ifq_set_oactive(&ifp->if_snd);
		ifp->if_timer = 1;
		return NULL;
	}
	IF_DEQUEUE(ifq, m);
	*nip = (struct ieee80211_node *)m->m_pkthdr.rcvif;
	m->m_pkthdr.rcvif = NULL;
	KKASSERT(*nip != NULL);
	return m;
}

/*
 * Point *mp at the next 802.11 frame to transmit.  Point *tsbp
 * at the driver's selection of transmit control block for the packet.
 */
static int
rtw_dequeue(struct ifnet *ifp, struct rtw_txsoft_blk **tsbp,
	    struct rtw_txdesc_blk **tdbp, struct mbuf **mp,
	    struct ieee80211_node **nip)
{
	struct rtw_softc *sc = ifp->if_softc;
	struct ether_header *eh;
	struct mbuf *m0;
	int pri;

	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: enter %s\n", ifp->if_xname, __func__));

	if (sc->sc_ic.ic_state == IEEE80211_S_RUN &&
	    (*mp = rtw_80211_dequeue(sc, &sc->sc_beaconq, RTW_TXPRIBCN, tsbp,
		                     tdbp, nip)) != NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT,
			("%s: dequeue beacon frame\n", ifp->if_xname));
		return 0;
	}

	if ((*mp = rtw_80211_dequeue(sc, &sc->sc_ic.ic_mgtq, RTW_TXPRIMD, tsbp,
		                     tdbp, nip)) != NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT,
			("%s: dequeue mgt frame\n", ifp->if_xname));
		return 0;
	}

	*mp = NULL;

	if (sc->sc_ic.ic_state != IEEE80211_S_RUN) {
		ifq_purge(&ifp->if_snd);
		DPRINTF(sc, RTW_DEBUG_XMIT,
			("%s: not running\n", ifp->if_xname));
		return 0;
	}

	m0 = ifq_dequeue(&ifp->if_snd);
	if (m0 == NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT,
			("%s: no frame ready\n", ifp->if_xname));
		return 0;
	}
	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: dequeue data frame\n", ifp->if_xname));

	pri = ((m0->m_flags & M_PWR_SAV) != 0) ? RTW_TXPRIHI : RTW_TXPRIMD;

	if (rtw_txring_choose(sc, tsbp, tdbp, pri) == -1) {
		DPRINTF(sc, RTW_DEBUG_XMIT_RSRC,
			("%s: no ring %d descriptor\n", ifp->if_xname, pri));
		ifq_set_oactive(&ifp->if_snd);
		ifq_prepend(&ifp->if_snd, m0);
		sc->sc_if.if_timer = 1;
		return 0;
	}

	BPF_MTAP(ifp, m0);

	eh = mtod(m0, struct ether_header *);
	*nip = ieee80211_find_txnode(&sc->sc_ic, eh->ether_dhost);
	if (*nip == NULL) {
		/* NB: ieee80211_find_txnode does stat+msg */
		m_freem(m0);
		return -1;
	}

	if ((m0 = ieee80211_encap(&sc->sc_ic, m0, *nip)) == NULL) {
		DPRINTF(sc, RTW_DEBUG_XMIT,
			("%s: encap error\n", ifp->if_xname));
		ieee80211_free_node(*nip);
		IFNET_STAT_INC(ifp, oerrors, 1);
		return -1;
	}

	IFNET_STAT_INC(ifp, opackets, 1);
	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: leave %s\n", ifp->if_xname, __func__));
	*mp = m0;
	return 0;
}

static __inline int
rtw_txsegs_too_short(struct rtw_txsegs *segs)
{
	int i;

	for (i = 0; i < segs->nseg; i++) {
		if (segs->segs[i].ds_len < 4)
			return 1;
	}
	return 0;
}

static __inline int
rtw_txsegs_too_long(struct rtw_txsegs *segs)
{
	int i;

	for (i = 0; i < segs->nseg; i++) {
		if (segs->segs[i].ds_len > RTW_TXLEN_LENGTH_MASK)
			return 1;
	}
	return 0;
}

static void
rtw_txbuf_dma_map(void *arg, bus_dma_segment_t *seg, int nseg,
		  bus_size_t mapsize, int error)
{
	struct rtw_txsegs *s = arg;

	if (error)
		return;

	KASSERT(nseg <= RTW_MAXPKTSEGS, ("too many tx mbuf seg"));

	s->nseg = nseg;
	bcopy(seg, s->segs, sizeof(*seg) * nseg);
}

static struct mbuf *
rtw_load_txbuf(struct rtw_softc *sc, struct rtw_txsoft *ts,
	       struct rtw_txsegs *segs, int ndesc_free, struct mbuf *m)
{
	int unload = 0, error;

	error = bus_dmamap_load_mbuf(sc->sc_txsoft_dmat, ts->ts_dmamap, m,
				     rtw_txbuf_dma_map, segs, BUS_DMA_NOWAIT);
	if (error && error != EFBIG) {
		if_printf(&sc->sc_ic.ic_if, "can't load tx mbuf1\n");
		goto back;
	}

	if (error || segs->nseg > ndesc_free || rtw_txsegs_too_short(segs)) {
		struct mbuf *m_new;

		if (error == 0)
			bus_dmamap_unload(sc->sc_txsoft_dmat, ts->ts_dmamap);

		m_new = m_defrag(m, MB_DONTWAIT);
		if (m_new == NULL) {
			if_printf(&sc->sc_ic.ic_if, "can't defrag tx mbuf\n");
			error = ENOBUFS;
			goto back;
		}
		m = m_new;

		error = bus_dmamap_load_mbuf(sc->sc_txsoft_dmat, ts->ts_dmamap,
					     m, rtw_txbuf_dma_map, segs,
					     BUS_DMA_NOWAIT);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "can't load tx mbuf2\n");
			goto back;
		}
		unload = 1;

		error = EFBIG;
		if (segs->nseg > ndesc_free) {
			if_printf(&sc->sc_ic.ic_if, "not enough free txdesc\n");
			goto back;
		}
		if (rtw_txsegs_too_short(segs)) {
			if_printf(&sc->sc_ic.ic_if, "segment too short\n");
			goto back;
		}
		error = 0;
	}

	if (rtw_txsegs_too_long(segs)) {
		if_printf(&sc->sc_ic.ic_if, "segment too long\n");
		unload = 1;
		error = EFBIG;
	}

back:
	if (error) {
		if (unload)
			bus_dmamap_unload(sc->sc_txsoft_dmat, ts->ts_dmamap);
		m_freem(m);
		m = NULL;
	} else {
		bus_dmamap_sync(sc->sc_txsoft_dmat, ts->ts_dmamap,
				BUS_DMASYNC_PREWRITE);
	}
	return m;
}

#ifdef RTW_DEBUG
static void
rtw_print_txdesc(struct rtw_softc *sc, const char *action,
		 struct rtw_txsoft *ts, struct rtw_txdesc_blk *tdb, int desc)
{
	struct rtw_txdesc *td = &tdb->tdb_desc[desc];

	DPRINTF(sc, RTW_DEBUG_XMIT_DESC,
		("%s: %p %s txdesc[%d] "
		 "next %#08x buf %#08x "
		 "ctl0 %#08x ctl1 %#08x len %#08x\n",
		 sc->sc_ic.ic_if.if_xname, ts, action,
		 desc, le32toh(td->td_buf), le32toh(td->td_next),
		 le32toh(td->td_ctl0), le32toh(td->td_ctl1),
		 le32toh(td->td_len)));
}
#endif /* RTW_DEBUG */

static void
rtw_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct rtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct rtw_txsoft *ts;
	struct mbuf *m0;
	uint32_t proto_ctl0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: enter %s\n", ifp->if_xname, __func__));

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		goto out;

	/* XXX do real rate control */
	proto_ctl0 = RTW_TXCTL0_RTSRATE_1MBPS;

	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		proto_ctl0 |= RTW_TXCTL0_SPLCP;

	for (;;) {
		struct rtw_txsegs segs;
		struct rtw_duration *d0;
		struct ieee80211_frame_min *wh;
		struct rtw_txsoft_blk *tsb;
		struct rtw_txdesc_blk *tdb;
		struct rtw_txdesc *td;
		struct ieee80211_key *k;
		uint32_t ctl0, ctl1;
		uint8_t tppoll;
		int desc, i, lastdesc, npkt, rate, rateidx, ratectl;

		if (rtw_dequeue(ifp, &tsb, &tdb, &m0, &ni) == -1)
			continue;
		if (m0 == NULL)
			break;

		wh = mtod(m0, struct ieee80211_frame_min *);

		if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0 &&
		    (k = ieee80211_crypto_encap(ic, ni, m0)) == NULL) {
			ieee80211_free_node(ni);
			m_freem(m0);
			break;
		} else {
			k = NULL;
		}

		ts = STAILQ_FIRST(&tsb->tsb_freeq);

		m0 = rtw_load_txbuf(sc, ts, &segs, tdb->tdb_nfree, m0);
		if (m0 == NULL || segs.nseg == 0) {
			DPRINTF(sc, RTW_DEBUG_XMIT,
				("%s: %s failed\n", ifp->if_xname, __func__));
			goto post_dequeue_err;
		}

		/*
		 * Note well: rtw_load_txbuf may have created a new chain,
		 * so we must find the header once more.
		 */
		wh = mtod(m0, struct ieee80211_frame_min *);

		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT) {
			rateidx = 0;
			rate = 2;	/* 1Mbit/s */
			ratectl = 0;
		} else {
			ieee80211_ratectl_findrate(ni, m0->m_pkthdr.len,
						   &rateidx, 1);
			rate = IEEE80211_RS_RATE(&ni->ni_rates, rateidx);
			ratectl =1;

			if (rate == 0) {
				if_printf(ifp, "incorrect rate\n");
				rateidx = 0;
				rate = 2;	/* 1Mbit/s */
				ratectl = 0;
			}
		}

#ifdef RTW_DEBUG
		if ((ifp->if_flags & (IFF_DEBUG | IFF_LINK2)) ==
		    (IFF_DEBUG | IFF_LINK2)) {
			ieee80211_dump_pkt(mtod(m0, uint8_t *),
					   (segs.nseg == 1) ? m0->m_pkthdr.len
					   		    : sizeof(wh),
					   rate, 0);
		}
#endif /* RTW_DEBUG */
		ctl0 = proto_ctl0 |
		       __SHIFTIN(m0->m_pkthdr.len, RTW_TXCTL0_TPKTSIZE_MASK);

		switch (rate) {
		default:
		case 2:
			ctl0 |= RTW_TXCTL0_RATE_1MBPS;
			break;
		case 4:
			ctl0 |= RTW_TXCTL0_RATE_2MBPS;
			break;
		case 11:
			ctl0 |= RTW_TXCTL0_RATE_5MBPS;
			break;
		case 22:
			ctl0 |= RTW_TXCTL0_RATE_11MBPS;
			break;
		}
		/* XXX >= ? Compare after fragmentation? */
		if (m0->m_pkthdr.len > ic->ic_rtsthreshold)
			ctl0 |= RTW_TXCTL0_RTSEN;

                /*
		 * XXX Sometimes writes a bogus keyid; h/w doesn't
                 * seem to care, since we don't activate h/w Tx
                 * encryption.
		 */
		if (k != NULL) {
			ctl0 |= __SHIFTIN(k->wk_keyix, RTW_TXCTL0_KEYID_MASK) &
				RTW_TXCTL0_KEYID_MASK;
		}

		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT) {
			ctl0 &= ~(RTW_TXCTL0_SPLCP | RTW_TXCTL0_RTSEN);
			if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_BEACON)
				ctl0 |= RTW_TXCTL0_BEACON;
		}

		if (rtw_compute_duration(wh, k, m0->m_pkthdr.len,
		    ic->ic_flags, ic->ic_fragthreshold,
		    rate, &ts->ts_d0, &ts->ts_dn, &npkt,
		    (ifp->if_flags & (IFF_DEBUG|IFF_LINK2)) ==
		    (IFF_DEBUG|IFF_LINK2)) == -1) {
			DPRINTF(sc, RTW_DEBUG_XMIT,
			    ("%s: fail compute duration\n", __func__));
			goto post_load_err;
		}

		d0 = &ts->ts_d0;

		*(uint16_t*)wh->i_dur = htole16(d0->d_data_dur);

		ctl1 = __SHIFTIN(d0->d_plcp_len, RTW_TXCTL1_LENGTH_MASK) |
		       __SHIFTIN(d0->d_rts_dur, RTW_TXCTL1_RTSDUR_MASK);

		if (d0->d_residue)
			ctl1 |= RTW_TXCTL1_LENGEXT;

		/* TBD fragmentation */

		ts->ts_first = tdb->tdb_next;
		KKASSERT(ts->ts_first < tdb->tdb_ndesc);

		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m0);

		if (sc->sc_radiobpf != NULL) {
			struct rtw_tx_radiotap_header *rt = &sc->sc_txtap;

			rt->rt_flags = 0;
			rt->rt_rate = rate;

			bpf_ptap(sc->sc_radiobpf, m0, rt,
				 sizeof(sc->sc_txtapu));
		}

		for (i = 0, lastdesc = desc = ts->ts_first; i < segs.nseg;
		     i++, desc = RTW_NEXT_IDX(tdb, desc)) {
			td = &tdb->tdb_desc[desc];
			td->td_ctl0 = htole32(ctl0);
			if (i != 0)
				td->td_ctl0 |= htole32(RTW_TXCTL0_OWN);
			td->td_ctl1 = htole32(ctl1);
			td->td_buf = htole32(segs.segs[i].ds_addr);
			td->td_len = htole32(segs.segs[i].ds_len);
			lastdesc = desc;
#ifdef RTW_DEBUG
			rtw_print_txdesc(sc, "load", ts, tdb, desc);
#endif /* RTW_DEBUG */
		}

		KKASSERT(desc < tdb->tdb_ndesc);

		ts->ts_ni = ni;
		KKASSERT(ni != NULL);
		ts->ts_mbuf = m0;
		ts->ts_rateidx = rateidx;
		ts->ts_ratectl = ratectl;
		ts->ts_last = lastdesc;
		tdb->tdb_desc[ts->ts_last].td_ctl0 |= htole32(RTW_TXCTL0_LS);
		tdb->tdb_desc[ts->ts_first].td_ctl0 |= htole32(RTW_TXCTL0_FS);

#ifdef RTW_DEBUG
		rtw_print_txdesc(sc, "FS on", ts, tdb, ts->ts_first);
		rtw_print_txdesc(sc, "LS on", ts, tdb, ts->ts_last);
#endif /* RTW_DEBUG */

		tdb->tdb_nfree -= segs.nseg;
		tdb->tdb_next = desc;

		tdb->tdb_desc[ts->ts_first].td_ctl0 |= htole32(RTW_TXCTL0_OWN);

#ifdef RTW_DEBUG
		rtw_print_txdesc(sc, "OWN on", ts, tdb, ts->ts_first);
#endif /* RTW_DEBUG */

		STAILQ_REMOVE_HEAD(&tsb->tsb_freeq, ts_q);
		STAILQ_INSERT_TAIL(&tsb->tsb_dirtyq, ts, ts_q);

		if (tsb != &sc->sc_txsoft_blk[RTW_TXPRIBCN])
			sc->sc_led_state.ls_event |= RTW_LED_S_TX;
		tsb->tsb_tx_timer = 5;
		ifp->if_timer = 1;
		tppoll = RTW_READ8(&sc->sc_regs, RTW_TPPOLL);
		tppoll &= ~RTW_TPPOLL_SALL;
		tppoll |= tsb->tsb_poll & RTW_TPPOLL_ALL;
		RTW_WRITE8(&sc->sc_regs, RTW_TPPOLL, tppoll);
		RTW_SYNC(&sc->sc_regs, RTW_TPPOLL, RTW_TPPOLL);

		bus_dmamap_sync(tdb->tdb_dmat, tdb->tdb_dmamap,
				BUS_DMASYNC_PREWRITE);
	}
out:
	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: leave %s\n", ifp->if_xname, __func__));
	return;

post_load_err:
	bus_dmamap_unload(sc->sc_txsoft_dmat, ts->ts_dmamap);
	m_freem(m0);
post_dequeue_err:
	ieee80211_free_node(ni);

	DPRINTF(sc, RTW_DEBUG_XMIT,
		("%s: leave %s\n", ifp->if_xname, __func__));
}

static void
rtw_idle(struct rtw_softc *sc)
{
	struct rtw_regs *regs = &sc->sc_regs;
	int active;

	/* request stop DMA; wait for packets to stop transmitting. */

	RTW_WRITE8(regs, RTW_TPPOLL, RTW_TPPOLL_SALL);
	RTW_WBR(regs, RTW_TPPOLL, RTW_TPPOLL);

	for (active = 0;
	     active < 300 &&
	     (RTW_READ8(regs, RTW_TPPOLL) & RTW_TPPOLL_ACTIVE) != 0;
	     active++)
		DELAY(10);
	if_printf(&sc->sc_ic.ic_if, "transmit DMA idle in %dus\n", active * 10);
}

static void
rtw_watchdog(struct ifnet *ifp)
{
	int pri, tx_timeouts = 0;
	struct rtw_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0)
		return;

	for (pri = 0; pri < RTW_NTXPRI; pri++) {
		struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[pri];

		if (tsb->tsb_tx_timer == 0)
			continue;
		else if (--tsb->tsb_tx_timer == 0) {
			if (STAILQ_EMPTY(&tsb->tsb_dirtyq))
				continue;
			if_printf(ifp, "transmit timeout, priority %d\n", pri);
			IFNET_STAT_INC(ifp, oerrors, 1);
			tx_timeouts++;
		} else {
			ifp->if_timer = 1;
		}
	}

	if (tx_timeouts > 0) {
		/*
		 * Stop Tx DMA, disable xmtr, flush Tx rings, enable xmtr,
		 * reset s/w tx-ring pointers, and start transmission.
		 *
		 * TBD Stop/restart just the broken rings?
		 */
		rtw_idle(sc);
		rtw_io_enable(sc, RTW_CR_TE, 0);
		rtw_txdesc_blk_reset_all(sc);
		rtw_io_enable(sc, RTW_CR_TE, 1);
		rtw_txring_fixup(sc);
		rtw_start(ifp, ifq_get_subq_default(&ifp->if_snd));
	}
	ieee80211_watchdog(&sc->sc_ic);
}

static void
rtw_next_scan(void *arg)
{
	struct ieee80211com *ic = arg;
	struct ifnet *ifp = &ic->ic_if;

	lwkt_serialize_enter(ifp->if_serializer);

	/* don't call rtw_start w/o network interrupts blocked */
	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ic);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
rtw_join_bss(struct rtw_softc *sc, uint8_t *bssid, uint16_t intval0)
{
	uint16_t bcnitv, bintritv, intval;
	int i;
	struct rtw_regs *regs = &sc->sc_regs;

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		RTW_WRITE8(regs, RTW_BSSID + i, bssid[i]);

	RTW_SYNC(regs, RTW_BSSID16, RTW_BSSID32);

	rtw_set_access(sc, RTW_ACCESS_CONFIG);

	intval = MIN(intval0, __SHIFTOUT_MASK(RTW_BCNITV_BCNITV_MASK));

	bcnitv = RTW_READ16(regs, RTW_BCNITV) & ~RTW_BCNITV_BCNITV_MASK;
	bcnitv |= __SHIFTIN(intval, RTW_BCNITV_BCNITV_MASK);
	RTW_WRITE16(regs, RTW_BCNITV, bcnitv);
	/* interrupt host 1ms before the TBTT */
	bintritv = RTW_READ16(regs, RTW_BINTRITV) & ~RTW_BINTRITV_BINTRITV;
	bintritv |= __SHIFTIN(1000, RTW_BINTRITV_BINTRITV);
	RTW_WRITE16(regs, RTW_BINTRITV, bintritv);
	/* magic from Linux */
	RTW_WRITE16(regs, RTW_ATIMWND, __SHIFTIN(1, RTW_ATIMWND_ATIMWND));
	RTW_WRITE16(regs, RTW_ATIMTRITV, __SHIFTIN(2, RTW_ATIMTRITV_ATIMTRITV));
	rtw_set_access(sc, RTW_ACCESS_NONE);

	rtw_io_enable(sc, RTW_CR_RE | RTW_CR_TE, 1);
}

/* Synchronize the hardware state with the software state. */
static int
rtw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct rtw_softc *sc = ifp->if_softc;
	enum ieee80211_state ostate;
	int error;

	ostate = ic->ic_state;

	ieee80211_ratectl_newstate(ic, nstate);
	rtw_led_newstate(sc, nstate);

	if (nstate == IEEE80211_S_INIT) {
		callout_stop(&sc->sc_scan_ch);
		sc->sc_cur_chan = IEEE80211_CHAN_ANY;
		return sc->sc_mtbl.mt_newstate(ic, nstate, arg);
	}

	if (ostate == IEEE80211_S_INIT && nstate != IEEE80211_S_INIT)
		rtw_pwrstate(sc, RTW_ON);

	error = rtw_tune(sc);
	if (error != 0)
		return error;

	switch (nstate) {
	case IEEE80211_S_INIT:
		panic("%s: unexpected state IEEE80211_S_INIT", __func__);
		break;
	case IEEE80211_S_SCAN:
		if (ostate != IEEE80211_S_SCAN) {
			memset(ic->ic_bss->ni_bssid, 0, IEEE80211_ADDR_LEN);
			rtw_set_nettype(sc, IEEE80211_M_MONITOR);
		}

		callout_reset(&sc->sc_scan_ch, rtw_dwelltime * hz / 1000,
			      rtw_next_scan, ic);

		break;
	case IEEE80211_S_RUN:
		switch (ic->ic_opmode) {
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_IBSS:
			rtw_set_nettype(sc, IEEE80211_M_MONITOR);
			/*FALLTHROUGH*/
		case IEEE80211_M_AHDEMO:
		case IEEE80211_M_STA:
			rtw_join_bss(sc, ic->ic_bss->ni_bssid,
				     ic->ic_bss->ni_intval);
			break;
		case IEEE80211_M_MONITOR:
			break;
		}
		rtw_set_nettype(sc, ic->ic_opmode);
		break;
	case IEEE80211_S_ASSOC:
	case IEEE80211_S_AUTH:
		break;
	}

	if (nstate != IEEE80211_S_SCAN)
		callout_stop(&sc->sc_scan_ch);

	return sc->sc_mtbl.mt_newstate(ic, nstate, arg);
}

/* Extend a 32-bit TSF timestamp to a 64-bit timestamp. */
static uint64_t
rtw_tsf_extend(struct rtw_regs *regs, uint32_t rstamp)
{
	uint32_t tsftl, tsfth;

	tsfth = RTW_READ(regs, RTW_TSFTRH);
	tsftl = RTW_READ(regs, RTW_TSFTRL);
	if (tsftl < rstamp)	/* Compensate for rollover. */
		tsfth--;
	return ((uint64_t)tsfth << 32) | rstamp;
}

static void
rtw_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
	      struct ieee80211_node *ni, int subtype, int rssi, uint32_t rstamp)
{
	struct ifnet *ifp = &ic->ic_if;
	struct rtw_softc *sc = ifp->if_softc;

	sc->sc_mtbl.mt_recv_mgmt(ic, m, ni, subtype, rssi, rstamp);

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
		if (ic->ic_opmode == IEEE80211_M_IBSS &&
		    ic->ic_state == IEEE80211_S_RUN) {
			uint64_t tsf = rtw_tsf_extend(&sc->sc_regs, rstamp);

			if (le64toh(ni->ni_tstamp.tsf) >= tsf)
				ieee80211_ibss_merge(ni);
		}
		break;
	default:
		break;
	}
}

#ifdef foo
static struct ieee80211_node *
rtw_node_alloc(struct ieee80211_node_table *nt)
{
	struct ifnet *ifp = nt->nt_ic->ic_ifp;
	struct rtw_softc *sc = (struct rtw_softc *)ifp->if_softc;
	struct ieee80211_node *ni = (*sc->sc_mtbl.mt_node_alloc)(nt);

	DPRINTF(sc, RTW_DEBUG_NODE,
	    ("%s: alloc node %p\n", sc->sc_dev.dv_xname, ni));
	return ni;
}

static void
rtw_node_free(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ifnet *ifp = ic->ic_ifp;
	struct rtw_softc *sc = (struct rtw_softc *)ifp->if_softc;
	char ethstr[ETHER_ADDRSTRLEN + 1];

	DPRINTF(sc, RTW_DEBUG_NODE,
	    ("%s: freeing node %p %s\n", sc->sc_dev.dv_xname, ni,
	    kether_ntoa(ni->ni_bssid, ethstr)));
	sc->sc_mtbl.mt_node_free(ni);
}
#endif

static int
rtw_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) ==
		    (IFF_RUNNING|IFF_UP))
			rtw_init(ifp);		/* XXX lose error */
		error = 0;
	}
	return error;
}

static void
rtw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct rtw_softc *sc = ifp->if_softc;

	if ((sc->sc_flags & RTW_F_ENABLED) == 0) {
		imr->ifm_active = IFM_IEEE80211 | IFM_NONE;
		imr->ifm_status = 0;
		return;
	}
	ieee80211_media_status(ifp, imr);
}

static __inline void
rtw_set80211methods(struct rtw_mtbl *mtbl, struct ieee80211com *ic)
{
	mtbl->mt_newstate = ic->ic_newstate;
	ic->ic_newstate = rtw_newstate;

	mtbl->mt_recv_mgmt = ic->ic_recv_mgmt;
	ic->ic_recv_mgmt = rtw_recv_mgmt;

#ifdef foo
	mtbl->mt_node_free = ic->ic_node_free;
	ic->ic_node_free = rtw_node_free;

	mtbl->mt_node_alloc = ic->ic_node_alloc;
	ic->ic_node_alloc = rtw_node_alloc;
#endif

	ic->ic_crypto.cs_key_delete = rtw_key_delete;
	ic->ic_crypto.cs_key_set = rtw_key_set;
	ic->ic_crypto.cs_key_update_begin = rtw_key_update_begin;
	ic->ic_crypto.cs_key_update_end = rtw_key_update_end;
}

static __inline void
rtw_init_radiotap(struct rtw_softc *sc)
{
	sc->sc_rxtap.rr_ihdr.it_len = htole16(sizeof(sc->sc_rxtapu));
	sc->sc_rxtap.rr_ihdr.it_present = htole32(RTW_RX_RADIOTAP_PRESENT);

	sc->sc_txtap.rt_ihdr.it_len = htole16(sizeof(sc->sc_txtapu));
	sc->sc_txtap.rt_ihdr.it_present = htole32(RTW_TX_RADIOTAP_PRESENT);
}

static struct rtw_rf *
rtw_rf_attach(struct rtw_softc *sc, enum rtw_rfchipid rfchipid, int digphy)
{
	rtw_rf_write_t rf_write;
	struct rtw_rf *rf;

	switch (rfchipid) {
	default:
		rf_write = rtw_rf_hostwrite;
		break;
	case RTW_RFCHIPID_INTERSIL:
	case RTW_RFCHIPID_PHILIPS:
	case RTW_RFCHIPID_GCT:	/* XXX a guess */
	case RTW_RFCHIPID_RFMD:
		rf_write = (rtw_host_rfio) ? rtw_rf_hostwrite : rtw_rf_macwrite;
		break;
	}

	switch (rfchipid) {
	case RTW_RFCHIPID_GCT:
		rf = rtw_grf5101_create(&sc->sc_regs, rf_write, 0);
		sc->sc_pwrstate_cb = rtw_maxim_pwrstate;
		sc->sc_getrssi = rtw_gct_getrssi;
		break;
	case RTW_RFCHIPID_MAXIM:
		rf = rtw_max2820_create(&sc->sc_regs, rf_write, 0);
		sc->sc_pwrstate_cb = rtw_maxim_pwrstate;
		sc->sc_getrssi = rtw_maxim_getrssi;
		break;
	case RTW_RFCHIPID_PHILIPS:
		rf = rtw_sa2400_create(&sc->sc_regs, rf_write, digphy);
		sc->sc_pwrstate_cb = rtw_philips_pwrstate;
		sc->sc_getrssi = rtw_philips_getrssi;
		break;
	case RTW_RFCHIPID_RFMD:
		/* XXX RFMD has no RF constructor */
		sc->sc_pwrstate_cb = rtw_rfmd_pwrstate;
		/*FALLTHROUGH*/
	default:
		return NULL;
	}
	rf->rf_continuous_tx_cb =
	    (rtw_continuous_tx_cb_t)rtw_continuous_tx_enable;
	rf->rf_continuous_tx_arg = sc;
	return rf;
}

/* Revision C and later use a different PHY delay setting than
 * revisions A and B.
 */
static uint8_t
rtw_check_phydelay(struct rtw_regs *regs, uint32_t old_rcr)
{
#define REVAB (RTW_RCR_MXDMA_UNLIMITED | RTW_RCR_AICV)
#define REVC (REVAB | RTW_RCR_RXFTH_WHOLE)

	uint8_t phydelay = __SHIFTIN(0x6, RTW_PHYDELAY_PHYDELAY);

	RTW_WRITE(regs, RTW_RCR, REVAB);
	RTW_WBW(regs, RTW_RCR, RTW_RCR);
	RTW_WRITE(regs, RTW_RCR, REVC);

	RTW_WBR(regs, RTW_RCR, RTW_RCR);
	if ((RTW_READ(regs, RTW_RCR) & REVC) == REVC)
		phydelay |= RTW_PHYDELAY_REVC_MAGIC;

	RTW_WRITE(regs, RTW_RCR, old_rcr);	/* restore RCR */
	RTW_SYNC(regs, RTW_RCR, RTW_RCR);

	return phydelay;
#undef REVC
#undef REVAB
}

int
rtw_attach(device_t dev)
{
	struct rtw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_cipher *wep_cipher;
	struct ifnet *ifp = &ic->ic_if;
	int rc;

	wep_cipher = ieee80211_crypto_cipher(IEEE80211_CIPHER_WEP);
	KKASSERT(wep_cipher != NULL);

	memcpy(&rtw_cipher_wep, wep_cipher, sizeof(rtw_cipher_wep));
	rtw_cipher_wep.ic_decap = rtw_wep_decap;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	switch (RTW_READ(&sc->sc_regs, RTW_TCR) & RTW_TCR_HWVERID_MASK) {
	case RTW_TCR_HWVERID_F:
		sc->sc_hwverid = 'F';
		break;
	case RTW_TCR_HWVERID_D:
		sc->sc_hwverid = 'D';
		break;
	default:
		sc->sc_hwverid = '?';
		break;
	}

	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->sc_irq_rid,
						RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "could not alloc irq res\n");
		return ENXIO;
	}

	/* Allocate h/w desc blocks */
	rc = rtw_desc_blk_alloc(sc);
	if (rc)
		goto err;

	/* Allocate s/w desc blocks */
	rc = rtw_soft_blk_alloc(sc);
	if (rc)
		goto err;

	/* Reset the chip to a known state. */
	rc = rtw_reset(sc);
	if (rc) {
		device_printf(dev, "could not reset\n");
		goto err;
	}

	sc->sc_rcr = RTW_READ(&sc->sc_regs, RTW_RCR);

	if ((sc->sc_rcr & RTW_RCR_9356SEL) != 0)
		sc->sc_flags |= RTW_F_9356SROM;

	rc = rtw_srom_read(sc);
	if (rc)
		goto err;

	rc = rtw_srom_parse(sc);
	if (rc) {
		device_printf(dev, "malformed serial ROM\n");
		goto err;
	}

	device_printf(dev, "%s PHY\n",
		      ((sc->sc_flags & RTW_F_DIGPHY) != 0) ? "digital"
		      					   : "analog");

	device_printf(dev, "CS threshold %u\n", sc->sc_csthr);

	sc->sc_rf = rtw_rf_attach(sc, sc->sc_rfchipid,
				  sc->sc_flags & RTW_F_DIGPHY);
	if (sc->sc_rf == NULL) {
		device_printf(dev, "could not attach RF\n");
		rc = ENXIO;
		goto err;
	}

	sc->sc_phydelay = rtw_check_phydelay(&sc->sc_regs, sc->sc_rcr);

	RTW_DPRINTF(RTW_DEBUG_ATTACH,
		    ("%s: PHY delay %d\n", ifp->if_xname, sc->sc_phydelay));

	if (sc->sc_locale == RTW_LOCALE_UNKNOWN)
		rtw_identify_country(sc);

	rtw_init_channels(sc);

	rc = rtw_identify_sta(sc);
	if (rc)
		goto err;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_init = rtw_init;
	ifp->if_ioctl = rtw_ioctl;
	ifp->if_start = rtw_start;
	ifp->if_watchdog = rtw_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
#ifdef notyet
	ifq_set_ready(&ifp->if_snd);
#endif

	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps = IEEE80211_C_PMGT |
		      IEEE80211_C_IBSS |
		      IEEE80211_C_HOSTAP |
		      IEEE80211_C_MONITOR;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = rtw_rates_11b;

	/* initialize led callout */
	callout_init(&sc->sc_led_state.ls_fast_ch);
	callout_init(&sc->sc_led_state.ls_slow_ch);

	IEEE80211_ONOE_PARAM_SETUP(&sc->sc_onoe_param);
	ic->ic_ratectl.rc_st_ratectl_cap = IEEE80211_RATECTL_CAP_ONOE;
	ic->ic_ratectl.rc_st_ratectl = IEEE80211_RATECTL_ONOE;
	ic->ic_ratectl.rc_st_attach = rtw_ratectl_attach;

	/*
	 * Call MI attach routines.
	 */
	ieee80211_ifattach(&sc->sc_ic);

	/* Override some ieee80211 methods */
	rtw_set80211methods(&sc->sc_mtbl, &sc->sc_ic);

	/*
	 * possibly we should fill in our own sc_send_prresp, since
	 * the RTL8180 is probably sending probe responses in ad hoc
	 * mode.
	 */

	/* complete initialization */
	ieee80211_media_init(&sc->sc_ic, rtw_media_change, rtw_media_status);
	callout_init(&sc->sc_scan_ch);

	rtw_init_radiotap(sc);

	bpfattach_dlt(ifp, DLT_IEEE802_11_RADIO,
		      sizeof(struct ieee80211_frame) + 64, &sc->sc_radiobpf);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

	rc = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, rtw_intr, sc,
			    &sc->sc_irq_handle, ifp->if_serializer);
	if (rc) {
		device_printf(dev, "can't set up interrupt\n");
		bpfdetach(ifp);
		ieee80211_ifdetach(ic);
		goto err;
	}

	device_printf(dev, "hardware version %c\n", sc->sc_hwverid);
	if (bootverbose)
		ieee80211_announce(ic);
	return 0;
err:
	rtw_detach(dev);
	return rc;
}

int
rtw_detach(device_t dev)
{
	struct rtw_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);

		rtw_stop(sc, 1);
		sc->sc_flags |= RTW_F_INVALID;

		callout_stop(&sc->sc_scan_ch);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);

		lwkt_serialize_exit(ifp->if_serializer);

		ieee80211_ifdetach(&sc->sc_ic);
	}

	if (sc->sc_rf != NULL)
		rtw_rf_destroy(sc->sc_rf);

	if (sc->sc_srom.sr_content != NULL)
		kfree(sc->sc_srom.sr_content, M_DEVBUF);

	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq_res);
	}

	rtw_soft_blk_free(sc);
	rtw_desc_blk_free(sc);
	return 0;
}

static void
rtw_desc_dma_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	if (error)
		return;

	KASSERT(nseg == 1, ("too many desc segments"));
	*((uint32_t *)arg) = seg->ds_addr;	/* XXX bus_addr_t */
}

static int
rtw_dma_alloc(struct rtw_softc *sc, bus_dma_tag_t *dmat, int len,
	      void **desc, uint32_t *phyaddr, bus_dmamap_t *dmamap)
{
	int error;

	error = bus_dma_tag_create(NULL, RTW_DESC_ALIGNMENT, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL, len, 1, len, 0, dmat);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not alloc desc DMA tag");
		return error;
	}

	error = bus_dmamem_alloc(*dmat, desc, BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 dmamap);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not alloc desc DMA mem");
		return error;
	}

	error = bus_dmamap_load(*dmat, *dmamap, *desc, len,
				rtw_desc_dma_addr, phyaddr, BUS_DMA_WAITOK);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not load desc DMA mem");
		bus_dmamem_free(*dmat, *desc, *dmamap);
		*desc = NULL;
		return error;
	}
	return 0;
}

static void
rtw_dma_free(struct rtw_softc *sc __unused, bus_dma_tag_t *dmat, void **desc,
	     bus_dmamap_t *dmamap)
{
	if (*desc != NULL) {
		bus_dmamap_unload(*dmat, *dmamap);
		bus_dmamem_free(*dmat, *desc, *dmamap);
		*desc = NULL;
	}

	if (*dmat != NULL) {
		bus_dma_tag_destroy(*dmat);
		*dmat = NULL;
	}
}

static void
rtw_txdesc_blk_free(struct rtw_softc *sc, int q_no)
{
	struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[q_no];

	rtw_dma_free(sc, &tdb->tdb_dmat, (void **)&tdb->tdb_desc,
		     &tdb->tdb_dmamap);
}

static int
rtw_txdesc_blk_alloc(struct rtw_softc *sc, int q_len, int q_no,
		     bus_size_t q_basereg)
{
	struct rtw_txdesc_blk *tdb = &sc->sc_txdesc_blk[q_no];
	int i, error;

	/*
	 * Setup TX h/w desc
	 */
	error = rtw_dma_alloc(sc, &tdb->tdb_dmat,
			      q_len * sizeof(*tdb->tdb_desc),
			      (void **)&tdb->tdb_desc, &tdb->tdb_base,
			      &tdb->tdb_dmamap);
	if (error) {
		kprintf("%dth tx\n", q_no);
		return error;
	}
	tdb->tdb_basereg = q_basereg;

	tdb->tdb_ndesc = q_len;
	for (i = 0; i < tdb->tdb_ndesc; ++i)
		tdb->tdb_desc[i].td_next = htole32(RTW_NEXT_DESC(tdb, i));

	return 0;
}

static void
rtw_rxdesc_blk_free(struct rtw_softc *sc)
{
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;

	rtw_dma_free(sc, &rdb->rdb_dmat, (void **)&rdb->rdb_desc,
		     &rdb->rdb_dmamap);
}

static int
rtw_rxdesc_blk_alloc(struct rtw_softc *sc, int q_len)
{
	struct rtw_rxdesc_blk *rdb = &sc->sc_rxdesc_blk;
	int error;

	/*
	 * Setup RX h/w desc
	 */
	error = rtw_dma_alloc(sc, &rdb->rdb_dmat,
			      q_len * sizeof(*rdb->rdb_desc),
			      (void **)&rdb->rdb_desc, &rdb->rdb_base,
			      &rdb->rdb_dmamap);
	if (error) {
		kprintf("rx\n");
	} else {
		rdb->rdb_ndesc = q_len;
	}

	return error;
}

static void
rtw_txsoft_blk_free(struct rtw_softc *sc, int n_sd, int q_no)
{
	struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[q_no];

	if (tsb->tsb_desc != NULL) {
		int i;

		for (i = 0; i < n_sd; ++i) {
			bus_dmamap_destroy(sc->sc_txsoft_dmat,
					   tsb->tsb_desc[i].ts_dmamap);
		}
		kfree(tsb->tsb_desc, M_DEVBUF);
		tsb->tsb_desc = NULL;
	}
}

static int
rtw_txsoft_blk_alloc(struct rtw_softc *sc, int q_len, int q_no, uint8_t q_poll)
{
	struct rtw_txsoft_blk *tsb = &sc->sc_txsoft_blk[q_no];
	int i, error;

	STAILQ_INIT(&tsb->tsb_dirtyq);
	STAILQ_INIT(&tsb->tsb_freeq);
	tsb->tsb_ndesc = q_len;
	tsb->tsb_desc = kmalloc(q_len * sizeof(*tsb->tsb_desc), M_DEVBUF,
			       M_WAITOK | M_ZERO);
	tsb->tsb_poll = q_poll;

	for (i = 0; i < tsb->tsb_ndesc; ++i) {
		error = bus_dmamap_create(sc->sc_txsoft_dmat, 0,
					  &tsb->tsb_desc[i].ts_dmamap);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "could not create DMA map "
				  "for soft tx desc\n");
			rtw_txsoft_blk_free(sc, i, q_no);
			return error;
		}
	}
	return 0;
}

static void
rtw_rxsoft_blk_free(struct rtw_softc *sc, int n_sd)
{
	if (sc->sc_rxsoft_free) {
		int i;

		for (i = 0; i < n_sd; ++i) {
			bus_dmamap_destroy(sc->sc_rxsoft_dmat,
					   sc->sc_rxsoft[i].rs_dmamap);
		}
		sc->sc_rxsoft_free = 0;
	}
}

static int
rtw_rxsoft_blk_alloc(struct rtw_softc *sc, int q_len)
{
	int i, error;

	sc->sc_rxsoft_free = 1;

	/*
	 * Setup RX s/w desc
	 */
	for (i = 0; i < q_len; ++i) {
		error = bus_dmamap_create(sc->sc_rxsoft_dmat, 0,
					  &sc->sc_rxsoft[i].rs_dmamap);
		if (error) {
			if_printf(&sc->sc_ic.ic_if, "could not create DMA map "
				  "for soft rx desc\n");
			rtw_rxsoft_blk_free(sc, i);
			return error;
		}
	}
	return 0;
}

#define TXQ_PARAM(q, poll, breg)			\
	[RTW_TXPRI ## q] = {				\
		.txq_len	= RTW_TXQLEN ## q,	\
		.txq_poll	= poll,			\
		.txq_basereg	= breg			\
	}
static const struct {
	int		txq_len;
	uint8_t		txq_poll;
	bus_size_t	txq_basereg;
} txq_params[RTW_NTXPRI] = {
	TXQ_PARAM(LO, RTW_TPPOLL_LPQ | RTW_TPPOLL_SLPQ, RTW_TLPDA),
	TXQ_PARAM(MD, RTW_TPPOLL_NPQ | RTW_TPPOLL_SNPQ, RTW_TNPDA),
	TXQ_PARAM(HI, RTW_TPPOLL_HPQ | RTW_TPPOLL_SHPQ, RTW_THPDA),
	TXQ_PARAM(BCN, RTW_TPPOLL_BQ | RTW_TPPOLL_SBQ, RTW_TBDA)
};
#undef TXQ_PARAM

static int
rtw_desc_blk_alloc(struct rtw_softc *sc)
{
	int i, error;

	/* Create h/w TX desc */
	for (i = 0; i < RTW_NTXPRI; ++i) {
		error = rtw_txdesc_blk_alloc(sc, txq_params[i].txq_len, i,
					     txq_params[i].txq_basereg);
		if (error)
			return error;
	}

	/* Create h/w RX desc */
	return rtw_rxdesc_blk_alloc(sc, RTW_RXQLEN);
}

static void
rtw_desc_blk_free(struct rtw_softc *sc)
{
	int i;

	for (i = 0; i < RTW_NTXPRI; ++i)
		rtw_txdesc_blk_free(sc, i);
	rtw_rxdesc_blk_free(sc);
}

static int
rtw_soft_blk_alloc(struct rtw_softc *sc)
{
	int i, error;

	/* Create DMA tag for TX mbuf */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MCLBYTES, RTW_MAXPKTSEGS, MCLBYTES,
				   0, &sc->sc_txsoft_dmat);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not alloc txsoft DMA tag\n");
		return error;
	}

	/* Create DMA tag for RX mbuf */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   MCLBYTES, 1, MCLBYTES,
				   0, &sc->sc_rxsoft_dmat);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not alloc rxsoft DMA tag\n");
		return error;
	}

	/* Create a spare DMA map for RX mbuf */
	error = bus_dmamap_create(sc->sc_rxsoft_dmat, 0, &sc->sc_rxsoft_dmamap);
	if (error) {
		if_printf(&sc->sc_ic.ic_if, "could not alloc spare rxsoft "
			  "DMA map\n");
		bus_dma_tag_destroy(sc->sc_rxsoft_dmat);
		sc->sc_rxsoft_dmat = NULL;
		return error;
	}

	/* Create s/w TX desc */
	for (i = 0; i < RTW_NTXPRI; ++i) {
		error = rtw_txsoft_blk_alloc(sc, txq_params[i].txq_len, i,
					     txq_params[i].txq_poll);
		if (error)
			return error;
	}

	/* Create s/w RX desc */
	return rtw_rxsoft_blk_alloc(sc, RTW_RXQLEN);
}

static void
rtw_soft_blk_free(struct rtw_softc *sc)
{
	int i;

	for (i = 0; i < RTW_NTXPRI; ++i)
		rtw_txsoft_blk_free(sc, txq_params[i].txq_len, i);

	rtw_rxsoft_blk_free(sc, RTW_RXQLEN);

	if (sc->sc_txsoft_dmat != NULL) {
		bus_dma_tag_destroy(sc->sc_txsoft_dmat);
		sc->sc_txsoft_dmat = NULL;
	}

	if (sc->sc_rxsoft_dmat != NULL) {
		bus_dmamap_destroy(sc->sc_rxsoft_dmat, sc->sc_rxsoft_dmamap);
		bus_dma_tag_destroy(sc->sc_rxsoft_dmat);
		sc->sc_rxsoft_dmat = NULL;
	}
}

/*
 * Arguments in:
 *
 * paylen:  payload length (no FCS, no WEP header)
 *
 * hdrlen:  header length
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * flags:   IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d:       802.11 Duration field for RTS,
 *          802.11 Duration field for data frame,
 *          PLCP Length for data frame,
 *          residual octets at end of data slot
 */
static int
rtw_compute_duration1(int len, int use_ack, uint32_t icflags, int rate,
		      struct rtw_duration *d)
{
	int pre, ctsrate;
	int ack, bitlen, data_dur, remainder;

	/*
	 * RTS reserves medium for SIFS | CTS | SIFS | (DATA) | SIFS | ACK
	 * DATA reserves medium for SIFS | ACK
	 *
	 * XXXMYC: no ACK on multicast/broadcast or control packets
	 */

	bitlen = len * 8;

	pre = IEEE80211_DUR_DS_SIFS;
	if (icflags & IEEE80211_F_SHPREAMBLE) {
		pre += IEEE80211_DUR_DS_SHORT_PREAMBLE +
		       IEEE80211_DUR_DS_FAST_PLCPHDR;
	} else {
		pre += IEEE80211_DUR_DS_LONG_PREAMBLE +
		       IEEE80211_DUR_DS_SLOW_PLCPHDR;
	}

	d->d_residue = 0;
	data_dur = (bitlen * 2) / rate;
	remainder = (bitlen * 2) % rate;
	if (remainder != 0) {
		d->d_residue = (rate - remainder) / 16;
		data_dur++;
	}

	switch (rate) {
	case 2:		/* 1 Mb/s */
	case 4:		/* 2 Mb/s */
		/* 1 - 2 Mb/s WLAN: send ACK/CTS at 1 Mb/s */
		ctsrate = 2;
		break;
	case 11:	/* 5.5 Mb/s */
	case 22:	/* 11  Mb/s */
	case 44:	/* 22  Mb/s */
		/* 5.5 - 11 Mb/s WLAN: send ACK/CTS at 2 Mb/s */
		ctsrate = 4;
		break;
	default:
		/* TBD */
		return -1;
	}

	d->d_plcp_len = data_dur;

	ack = (use_ack) ? pre + (IEEE80211_DUR_DS_SLOW_ACK * 2) / ctsrate : 0;

	d->d_rts_dur = pre + (IEEE80211_DUR_DS_SLOW_CTS * 2) / ctsrate +
		       pre + data_dur +
		       ack;

	d->d_data_dur = ack;
	return 0;
}

/*
 * Arguments in:
 *
 * wh:      802.11 header
 *
 * paylen:  payload length (no FCS, no WEP header)
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * fraglen: fragment length, set to maximum (or higher) for no
 *          fragmentation
 *
 * flags:   IEEE80211_F_PRIVACY (hardware adds WEP),
 *          IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d0: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of first/only fragment
 *
 * dn: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of last fragment
 *
 * rtw_compute_duration assumes crypto-encapsulation, if any,
 * has already taken place.
 */
static int
rtw_compute_duration(const struct ieee80211_frame_min *wh,
		     const struct ieee80211_key *wk, int len,
		     uint32_t icflags, int fraglen, int rate,
		     struct rtw_duration *d0, struct rtw_duration *dn,
		     int *npktp, int debug)
{
	int ack, rc;
	int cryptolen,	/* crypto overhead: header+trailer */
	    firstlen,	/* first fragment's payload + overhead length */
	    hdrlen,	/* header length w/o driver padding */
	    lastlen,	/* last fragment's payload length w/ overhead */
	    lastlen0,	/* last fragment's payload length w/o overhead */
	    npkt,	/* number of fragments */
	    overlen,	/* non-802.11 header overhead per fragment */
	    paylen;	/* payload length w/o overhead */

	hdrlen = ieee80211_anyhdrsize((const void *)wh);

        /* Account for padding required by the driver. */
	if (icflags & IEEE80211_F_DATAPAD)
		paylen = len - roundup(hdrlen, sizeof(u_int32_t));
	else
		paylen = len - hdrlen;

	overlen = IEEE80211_CRC_LEN;

	if (wk != NULL) {
		cryptolen = wk->wk_cipher->ic_header +
		            wk->wk_cipher->ic_trailer;
		paylen -= cryptolen;
		overlen += cryptolen;
	}

	npkt = paylen / fraglen;
	lastlen0 = paylen % fraglen;

	if (npkt == 0) {		/* no fragments */
		lastlen = paylen + overlen;
	} else if (lastlen0 != 0) {	/* a short "tail" fragment */
		lastlen = lastlen0 + overlen;
		npkt++;
	} else {			/* full-length "tail" fragment */
		lastlen = fraglen + overlen;
	}

	if (npktp != NULL)
		*npktp = npkt;

	if (npkt > 1)
		firstlen = fraglen + overlen;
	else
		firstlen = paylen + overlen;

	if (debug) {
		kprintf("%s: npkt %d firstlen %d lastlen0 %d lastlen %d "
		    "fraglen %d overlen %d len %d rate %d icflags %08x\n",
		    __func__, npkt, firstlen, lastlen0, lastlen, fraglen,
		    overlen, len, rate, icflags);
	}

	ack = (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	       (wh->i_fc[1] & IEEE80211_FC0_TYPE_MASK) !=
	       IEEE80211_FC0_TYPE_CTL);

	rc = rtw_compute_duration1(firstlen + hdrlen, ack, icflags, rate, d0);
	if (rc == -1)
		return rc;

	if (npkt <= 1) {
		*dn = *d0;
		return 0;
	}
	return rtw_compute_duration1(lastlen + hdrlen, ack, icflags, rate, dn);
}

static int
rtw_get_rssi(struct rtw_softc *sc, uint8_t raw, uint8_t sq)
{
	int rssi;

	rssi = sc->sc_getrssi(raw, sq);

	if (rssi == 0)
		rssi = 1;
	else if (rssi > 100)
		rssi = 100;

	if (rssi > (RTW_NOISE_FLOOR + RTW_RSSI_CORR))
		rssi -= (RTW_NOISE_FLOOR + RTW_RSSI_CORR);
	else
		rssi = 0;

	return rssi;
}

static int
rtw_maxim_getrssi(uint8_t raw, uint8_t sq __unused)
{
	int rssi = raw;

	rssi &= 0x7e;
	rssi >>= 1;
	rssi += 0x42;
	if (raw & 0x1)
		rssi += 0xa;
	rssi &= 0xff;

	return rssi;
}

static int
rtw_gct_getrssi(uint8_t raw, uint8_t sq __unused)
{
	int rssi = raw;

	rssi &= 0x7e;
	if ((raw & 0x1) == 0 || rssi > 0x3c)
		rssi = 100;
	else
		rssi = (100 * rssi) / 0x3c;
	rssi &= 0xff;

	return rssi;
}

static int
rtw_philips_getrssi(uint8_t raw, uint8_t sq)
{
	static const uint8_t sq_rssi_map[SA2400_SQ_RSSI_MAP_MAX] =
	{ SA2400_SQ_RSSI_MAP };

	if (sq < SA2400_SQ_RSSI_MAP_MAX - 1)	/* NB: -1 is intended */
		return sq_rssi_map[sq];

	if (sq == 0x80)
		return 1;
	else
		return 0x32;
}

static void *
rtw_ratectl_attach(struct ieee80211com *ic, u_int rc)
{
	struct rtw_softc *sc = ic->ic_if.if_softc;

	switch (rc) {
	case IEEE80211_RATECTL_ONOE:
		return &sc->sc_onoe_param;
	case IEEE80211_RATECTL_NONE:
		/* This could only happen during detaching */
		return NULL;
	default:
		panic("unknown rate control algo %u", rc);
		return NULL;
	}
}
