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
 */

#ifndef _IF_IWLVAR_H
#define _IF_IWLVAR_H

#define IWL_ALIGN	0x1000	/* XXX */

struct iwl_dmamap_ctx {
	int		nsegs;
	bus_dma_segment_t *segs;
};

struct iwlmsg {
	struct netmsg	iwlm_nmsg;
	void		*iwlm_softc;

	/* For newstate() */
	enum ieee80211_state iwlm_nstate;
	int		iwlm_arg;
};

struct iwlcom {
	struct ieee80211com	iwl_ic;
	int			iwl_end;

	int			iwl_mem_rid;
	struct resource		*iwl_mem_res;
	bus_space_tag_t		iwl_mem_bt;
	bus_space_handle_t	iwl_mem_bh;

	int			iwl_irq_rid;
	struct resource		*iwl_irq_res;
	void			*iwl_irq_handle;

	int			iwl_tx_timer;

	struct lwkt_port	iwl_reply_port;
	struct lwkt_port	iwl_thread_port;
	struct thread		iwl_thread;
	int			(*iwl_fwd_port)
				(struct lwkt_port *, struct lwkt_msg *);
};

#define sc_ic		iwlcom.iwl_ic
#define sc_irq_res	iwlcom.iwl_irq_res
#define sc_irq_handle	iwlcom.iwl_irq_handle
#define sc_thread	iwlcom.iwl_thread
#define sc_thread_port	iwlcom.iwl_thread_port
#define sc_reply_port	iwlcom.iwl_reply_port
#define sc_tx_timer	iwlcom.iwl_tx_timer
#define sc_sysctl_ctx	iwlcom.iwl_sysctl_ctx
#define sc_sysctl_tree	iwlcom.iwl_sysctl_tree

#define IWL_FW_PATH	"iwl/"

#define IWL_WRITE_4(iwl, reg, val)	\
	bus_space_write_4((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg), (val))
#define IWL_WRITE_2(iwl, reg, val)	\
	bus_space_write_2((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg), (val))
#define IWL_WRITE_1(iwl, reg, val)	\
	bus_space_write_1((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg), (val))
#define IWL_READ_4(iwl, reg)		\
	bus_space_read_4((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg))
#define IWL_READ_2(iwl, reg)		\
	bus_space_read_2((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg))
#define IWL_READ_1(iwl, reg)		\
	bus_space_read_1((iwl)->iwl_mem_bt, (iwl)->iwl_mem_bh, (reg))

#define IND_WRITE_4(sc, reg, val)	\
	iwl_ind_write_4(&(sc)->iwlcom, (reg), (val))
#define IND_WRITE_2(sc, reg, val)	\
	iwl_ind_write_2(&(sc)->iwlcom, (reg), (val))
#define IND_WRITE_1(sc, reg, val)	\
	iwl_ind_write_1(&(sc)->iwlcom, (reg), (val))
#define IND_READ_4(sc, reg)		\
	iwl_ind_read_4(&(sc)->iwlcom, (reg))
#define IND_READ_2(sc, reg)		\
	iwl_ind_read_2(&(sc)->iwlcom, (reg))
#define IND_READ_1(sc, reg)		\
	iwl_ind_read_1(&(sc)->iwlcom, (reg))

#define CSR_READ_1(sc, reg)		IWL_READ_1(&(sc)->iwlcom, (reg))

#define CSR_WRITE_4(sc, reg, val)	IWL_WRITE_4(&(sc)->iwlcom, (reg), (val))
#define CSR_READ_4(sc, reg)		IWL_READ_4(&(sc)->iwlcom, (reg))
#define CSR_SETBITS_4(sc, reg, bit)	\
	CSR_WRITE_4((sc), (reg), CSR_READ_4((sc), (reg)) | (bit))

extern const struct ieee80211_rateset iwl_rateset_11b;

void		iwl_ind_write_4(struct iwlcom *, uint32_t, uint32_t);
void		iwl_ind_write_2(struct iwlcom *, uint32_t, uint16_t);
void		iwl_ind_write_1(struct iwlcom *, uint32_t, uint8_t);
uint32_t	iwl_ind_read_4(struct iwlcom *, uint32_t);
uint16_t	iwl_ind_read_2(struct iwlcom *, uint32_t);
uint8_t		iwl_ind_read_1(struct iwlcom *, uint32_t);
uint16_t	iwl_read_eeprom(struct iwlcom *, uint8_t);
int		iwl_dma_mem_create(device_t, bus_dma_tag_t, bus_size_t,
				   bus_dma_tag_t *, void **, bus_addr_t *,
				   bus_dmamap_t *);
void		iwl_dma_mem_destroy(bus_dma_tag_t, void *, bus_dmamap_t);
void		iwl_dma_buf_addr(void *, bus_dma_segment_t *, int,
				 bus_size_t, int);
void		iwl_create_thread(struct iwlcom *, int);
void		iwl_destroy_thread(struct iwlcom *);

void		iwlmsg_send(struct iwlmsg *, struct lwkt_port *);
void		iwlmsg_init(struct iwlmsg *, struct lwkt_port *,
			    netisr_fn_t, void *);

#endif	/* !_IF_IWLVAR_H */
