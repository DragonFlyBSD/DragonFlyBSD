/*
 * Copyright (c) 1997, 1998
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/re/if_rereg.h,v 1.14.2.1 2001/07/19 18:33:07 wpaul Exp $
 */

#ifndef _RE_DRAGONFLY_H_
#define _RE_DRAGONFLY_H_

#define RE_INTRS_TIMER	\
	((RE_INTRS & ~(RE_ISR_TX_OK | RE_ISR_RX_OK | RE_ISR_RX_OVERRUN |\
	  RE_ISR_PKT_UNDERRUN | RE_ISR_TDU)) | RE_ISR_PCS_TIMEOUT)

int		rtl_check_mac_version(struct re_softc *);
void		rtl_init_software_variable(struct re_softc *);
void		rtl_exit_oob(struct re_softc *);
void		rtl_hw_init(struct re_softc *);
void		rtl_reset(struct re_softc *);
void		rtl_get_hw_mac_address(struct re_softc *, u_int8_t *);
void		rtl_phy_power_up(struct re_softc *);
void		rtl_hw_phy_config(struct re_softc *);
void		rtl_clrwol(struct re_softc *);
int		rtl_ifmedia_upd(struct ifnet *);
void		rtl_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void		rtl_stop(struct re_softc *);
u_int8_t	rtl_link_ok(struct re_softc *);
void		rtl_link_on_patch(struct re_softc *);
void		rtl_set_eaddr(struct re_softc *);
void		rtl_hw_start(struct re_softc *);
void		rtl_set_rx_packet_filter(struct re_softc *);
void		rtl_hw_d3_para(struct re_softc *);
void		rtl_phy_power_down(struct re_softc *);

#endif	/* !_RE_DRAGONFLY_H_ */
