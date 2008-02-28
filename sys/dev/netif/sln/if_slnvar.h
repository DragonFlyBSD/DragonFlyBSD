/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/dev/netif/sln/if_slnvar.h,v 1.1 2008/02/28 18:39:20 swildner Exp $
 */

#ifndef  _IF_SLVAR_H_
#define  _IF_SLVAR_H_

struct sln_buf_data {
	uint32_t		dirty_rx;
	caddr_t			sln_rx_buf;
	struct mbuf		*sln_tx_buf[SL_TXD_CNT];
	uint32_t		dirty_tx;
	uint32_t		cur_tx;
};

struct sln_softc {
	struct arpcom		arpcom;		/* interface info */
	struct ifmedia		ifmedia;	/* media info */
	bus_space_handle_t	sln_bushandle;	/* bus space handle */
	bus_space_tag_t		sln_bustag;	/* bus space tag */
	struct resource		*sln_res;
	struct resource		*sln_irq;
	void			*sln_intrhand;
	uint8_t			sln_type;
	uint8_t			sln_stats_no_timeout;
	uint16_t		tx_early_ctrl;
	uint16_t		rx_early_ctrl:1;
	struct sln_buf_data	sln_bufdata;	/* Tx buffer descriptor */
	struct callout		sln_state;
	uint32_t		rxcfg;
	uint32_t		txcfg;
	int			suspended;
	int			connect;
	int			media_duplex;
	int			media_speed;
	int			txenablepad;
};

#endif	/* !_IF_SLVAR_H_ */
