/*
 * Copyright (c) 2003 by Quinton Dolan <q@onthenet.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS `AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 *
 * $Id: if_nvreg.h,v 1.3 2003/11/08 13:03:01 q Exp $
 * $DragonFly: src/sys/dev/netif/nv/Attic/if_nvreg.h,v 1.5 2004/11/05 17:13:44 dillon Exp $
 */
 
#ifndef _IF_NVREG_H_
#define _IF_NVREG_H_

#include "basetype.h"
#include "os.h"
#include "adapter.h"

#ifndef NVIDIA_VENDORID
#define NVIDIA_VENDORID 0x10DE
#endif

#define NFORCE_MCPNET1_DEVICEID 0x01C3
#define NFORCE_MCPNET2_DEVICEID 0x0066
#define NFORCE_MCPNET3_DEVICEID 0x00D6
#define NFORCE_MCPNET4_DEVICEID 0x0086
#define NFORCE_MCPNET5_DEVICEID 0x008C
#define NFORCE_MCPNET6_DEVICEID 0x00E6
#define NFORCE_MCPNET7_DEVICEID 0x00DF

#define NV_RID		0x10

#define TX_RING_SIZE 64
#define RX_RING_SIZE 64

#define NV_MAX_FRAGS 63

#define NV_DEBUG_INIT		0x0001
#define NV_DEBUG_RUNNING	0x0002
#define NV_DEBUG_DEINIT 	0x0004
#define NV_DEBUG_IOCTL		0x0008
#define NV_DEBUG_INTERRUPT  	0x0010
#define NV_DEBUG_API		0x0020
#define NV_DEBUG_LOCK		0x0040
#define NV_DEBUG_BROKEN		0x0080
#define NV_DEBUG_MII		0x0100
#define NV_DEBUG_ALL		0xFFFF

#define NV_DEBUG		0x0000

#if NV_DEBUG
#define DEBUGOUT(level, fmt, args...) if (NV_DEBUG & level) \
    printf(fmt, ## args)
#else
#define DEBUGOUT(level, fmt, args...)
#endif

typedef unsigned long	ulong;

struct nv_map_buffer {
	struct mbuf *mbuf;		/* mbuf receiving packet */
	bus_dmamap_t map;		/* DMA map */	
};

struct nv_dma_info {
	bus_dma_tag_t tag;
	struct nv_map_buffer buf;
	u_int16_t buflength;
	caddr_t vaddr;			/* Virtual memory address */
	bus_addr_t paddr;		/* DMA physical address */
};

struct nv_rx_desc {
	struct nv_rx_desc *next;
	struct nv_map_buffer buf;
	u_int16_t buflength;
	caddr_t vaddr;
	bus_addr_t paddr;
};

struct nv_tx_desc {
	/* Don't add anything above this structure */
	TX_INFO_ADAP TxInfoAdap;
	struct nv_tx_desc *next;
	struct nv_map_buffer buf;
	u_int16_t buflength;
	u_int32_t numfrags;
	bus_dma_segment_t frags[NV_MAX_FRAGS + 1];
};

struct nv_softc {
	struct arpcom arpcom;         /* interface info */
	struct resource *res;
	struct resource *irq;

	ADAPTER_API *hwapi;
	OS_API osapi;
		
	device_t miibus;
	device_t dev;
	struct callout	nv_stat_timer;

	void *sc_ih;
	bus_space_tag_t sc_st;
	bus_space_handle_t sc_sh;
	bus_dma_tag_t mtag;
	bus_dma_tag_t rtag;
	bus_dmamap_t rmap;
	bus_dma_tag_t ttag;
	bus_dmamap_t tmap;
	
	struct nv_rx_desc *rx_desc;
	struct nv_tx_desc *tx_desc;
	bus_addr_t rx_addr;
	bus_addr_t tx_addr;
	u_int16_t rx_ring_full;
	u_int16_t tx_ring_full;
	u_int32_t cur_rx;
	u_int32_t cur_tx;
	u_int32_t pending_rxs;
	u_int32_t pending_txs;
	
	u_int32_t flags;
	u_int32_t miicfg;
	int spl;
		
	/* Stuff for dealing with the NVIDIA OS API */
	struct callout ostimer;
	PTIMER_FUNC ostimer_func;
	void *ostimer_params;
	unsigned int linkup;
	ulong tx_errors;
	ulong phyaddr;
	unsigned char original_mac_addr[6];
};

struct nv_type {
	u_int16_t		vid_id;
	u_int16_t		dev_id;
	char			*name;
};

#define sc_if arpcom.ac_if
#define sc_macaddr arpcom.ac_enaddr

#define NV_LOCK(_sc)		int s = splimp()
#define NV_UNLOCK(_sc)		splx(s)
#define NV_OSLOCK(_sc)		(int)(_sc)->spl = splimp()
#define NV_OSUNLOCK(_sc)	splx((int)(_sc)->spl)

extern int ADAPTER_ReadPhy (PVOID pContext, ULONG ulPhyAddr, ULONG ulReg, ULONG *pulVal);
extern int ADAPTER_WritePhy (PVOID pContext, ULONG ulPhyAddr, ULONG ulReg, ULONG ulVal);
extern int ADAPTER_Init (PVOID pContext, USHORT usForcedSpeed, UCHAR ucForceDpx, UCHAR ucForceMode, UINT *puiLinkState);
#endif
