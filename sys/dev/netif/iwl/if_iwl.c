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
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/msgport2.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/netmsg2.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "if_iwlreg.h"
#include "if_iwlvar.h"
#include "iwl2100var.h"

struct iwl_devinfo {
	const char	*desc;
	int		bar;
	int		(*attach)(device_t);
	void		(*detach)(device_t);
	int		(*shutdown)(device_t);
};

struct iwl_softc {
	union {
		struct iwlcom common;
		struct iwl2100_softc sc2100;
	} u;
	const struct iwl_devinfo *sc_info;
};

static int	iwl_probe(device_t);
static int	iwl_attach(device_t);
static int	iwl_detach(device_t);
static int	iwl_shutdown(device_t);

static void	iwl_dma_ring_addr(void *, bus_dma_segment_t *, int, int);
static void	iwl_service_loop(void *);
static int	iwl_put_port(struct lwkt_port *, struct lwkt_msg *);
static void	iwl_destroy_thread_dispatch(struct netmsg *);

static const struct iwl_devinfo iwl2100_devinfo = {
	.desc =		IWL2100_DESC,
	.bar =		IWL2100_PCIR_BAR,
	.attach =	iwl2100_attach,
	.detach =	iwl2100_detach,
	.shutdown =	iwl2100_shutdown
};

static const struct iwl_dev {
	uint16_t	vid;
	uint16_t	did;
	const struct iwl_devinfo *info;
} iwl_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_PRO_WL_2100, &iwl2100_devinfo },
	{ 0, 0, NULL }
};

static device_method_t iwl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		iwl_probe),
	DEVMETHOD(device_attach,	iwl_attach),
	DEVMETHOD(device_detach,	iwl_detach),
	DEVMETHOD(device_shutdown,	iwl_shutdown),

	DEVMETHOD_END
};

static driver_t iwl_driver = {
	"iwl",
	iwl_methods,
	sizeof(struct iwl_softc)
};

static devclass_t iwl_devclass;

DRIVER_MODULE(iwl, pci, iwl_driver, iwl_devclass, NULL, NULL);

MODULE_DEPEND(iwl, wlan, 1, 1, 1);
MODULE_DEPEND(iwl, pci, 1, 1, 1);

const struct ieee80211_rateset iwl_rateset_11b = { 4, { 2, 4, 11, 22 } };

static int
iwl_probe(device_t dev)
{
	const struct iwl_dev *d;
	uint16_t did, vid;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = iwl_devices; d->info != NULL; ++d) {
		if (d->did == did && d->vid == vid) {
			struct iwl_softc *sc = device_get_softc(dev);

			device_set_desc(dev, d->info->desc);
			sc->sc_info = d->info;
			return 0;
		}
	}
	return ENXIO;
}

static int
iwl_attach(device_t dev)
{
	struct iwl_softc *sc = device_get_softc(dev);
	struct iwlcom *iwl = &sc->u.common;
	struct ifnet *ifp = &iwl->iwl_ic.ic_if;
	int error, bar;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	bar = sc->sc_info->bar;

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t irq, mem;

		irq = pci_read_config(dev, PCIR_INTLINE, 4);
		mem = pci_read_config(dev, bar, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, PCIR_INTLINE, irq, 4);
		pci_write_config(dev, bar, mem, 4);
	}
#endif	/* !BURN_BRIDGES */

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate IO memory
	 */
	iwl->iwl_mem_rid = bar;
	iwl->iwl_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						  &iwl->iwl_mem_rid, RF_ACTIVE);
	if (iwl->iwl_mem_res == NULL) {
		device_printf(dev, "can't allocate IO memory\n");
		return ENXIO;
	}
	iwl->iwl_mem_bt = rman_get_bustag(iwl->iwl_mem_res);
	iwl->iwl_mem_bh = rman_get_bushandle(iwl->iwl_mem_res);

	/*
	 * Allocate IRQ
	 */
	iwl->iwl_irq_rid = 0;
	iwl->iwl_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						  &iwl->iwl_irq_rid,
						  RF_SHAREABLE | RF_ACTIVE);
	if (iwl->iwl_irq_res == NULL) {
		device_printf(dev, "can't allocate irq\n");
		error = ENXIO;
		goto back;
	}

	/*
	 * Device specific attach
	 */
	error = sc->sc_info->attach(dev);
back:
	if (error)
		iwl_detach(dev);
	return error;
}

static int
iwl_detach(device_t dev)
{
	struct iwl_softc *sc = device_get_softc(dev);
	struct iwlcom *iwl = &sc->u.common;

	sc->sc_info->detach(dev);

	if (iwl->iwl_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, iwl->iwl_irq_rid,
				     iwl->iwl_irq_res);
	}

	if (iwl->iwl_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, iwl->iwl_mem_rid,
				     iwl->iwl_mem_res);
	}
	return 0;
}

static int
iwl_shutdown(device_t dev)
{
	struct iwl_softc *sc = device_get_softc(dev);

	return sc->sc_info->shutdown(dev);
}

void
iwl_ind_write_4(struct iwlcom *iwl, uint32_t addr, uint32_t data)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	IWL_WRITE_4(iwl, IWL_IND_DATA, data);
}

void
iwl_ind_write_2(struct iwlcom *iwl, uint32_t addr, uint16_t data)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	IWL_WRITE_2(iwl, IWL_IND_DATA, data);
}

void
iwl_ind_write_1(struct iwlcom *iwl, uint32_t addr, uint8_t data)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	IWL_WRITE_1(iwl, IWL_IND_DATA, data);
}

uint32_t
iwl_ind_read_4(struct iwlcom *iwl, uint32_t addr)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	return IWL_READ_4(iwl, IWL_IND_DATA);
}

uint16_t
iwl_ind_read_2(struct iwlcom *iwl, uint32_t addr)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	return IWL_READ_2(iwl, IWL_IND_DATA);
}

uint8_t
iwl_ind_read_1(struct iwlcom *iwl, uint32_t addr)
{
	IWL_WRITE_4(iwl, IWL_IND_ADDR, addr);
	return IWL_READ_1(iwl, IWL_IND_DATA);
}

#define EEPROM_WRITE(iwl, data) \
do { \
	iwl_ind_write_4((iwl), IWL_EEPROM_IND_CSR, (data)); \
	DELAY(1); \
} while (0)

#define EEPROM_SET_BIT(iwl) \
do { \
	EEPROM_WRITE((iwl), IWL_EEBIT_CS | IWL_EEBIT_DI); \
	EEPROM_WRITE((iwl), IWL_EEBIT_CS | IWL_EEBIT_DI | IWL_EEBIT_SK); \
} while (0)

#define EEPROM_CLR_BIT(iwl) \
do { \
	EEPROM_WRITE((iwl), IWL_EEBIT_CS); \
	EEPROM_WRITE((iwl), IWL_EEBIT_CS | IWL_EEBIT_SK); \
} while (0)

uint16_t
iwl_read_eeprom(struct iwlcom *iwl, uint8_t ofs)
{
	uint16_t ret;
	int i;

	/* Chip select */
	EEPROM_WRITE(iwl, 0);
	EEPROM_WRITE(iwl, IWL_EEBIT_CS);
	EEPROM_WRITE(iwl, IWL_EEBIT_CS | IWL_EEBIT_SK);
	EEPROM_WRITE(iwl, IWL_EEBIT_CS);

	/* Send READ opcode (0x2) */
	EEPROM_SET_BIT(iwl);
	EEPROM_SET_BIT(iwl); /* READ opcode */
	EEPROM_CLR_BIT(iwl); /* READ opcode */

	/* Send offset */
	for (i = NBBY - 1; i >= 0; --i) {
		if (ofs & (1 << i))
			EEPROM_SET_BIT(iwl);
		else
			EEPROM_CLR_BIT(iwl);
	}

	/* Kick start */
	EEPROM_WRITE(iwl, IWL_EEBIT_CS);

	/* Read data */
	ret = 0;
	for (i = 0; i < (sizeof(ret) * NBBY); ++i) {
		EEPROM_WRITE(iwl, IWL_EEBIT_CS | IWL_EEBIT_SK);
		EEPROM_WRITE(iwl, IWL_EEBIT_CS);

		ret <<= 1;
		if (iwl_ind_read_4(iwl, IWL_EEPROM_IND_CSR) & IWL_EEBIT_DO)
			ret |= 1;
	}

	/* Stop */
	EEPROM_WRITE(iwl, 0);

	/* Chip de-select */
	EEPROM_WRITE(iwl, IWL_EEBIT_CS);
	EEPROM_WRITE(iwl, 0);
	EEPROM_WRITE(iwl, IWL_EEBIT_SK);

	return le16toh(ret);
}

static void
iwl_dma_ring_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	KASSERT(nseg == 1, ("too many segments"));
	*((bus_addr_t *)arg) = seg->ds_addr;
}

int
iwl_dma_mem_create(device_t dev, bus_dma_tag_t parent, bus_size_t size,
		   bus_dma_tag_t *dtag, void **addr, bus_addr_t *paddr,
		   bus_dmamap_t *dmap)
{
	int error;

	error = bus_dma_tag_create(parent, IWL_ALIGN, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   size, 1, BUS_SPACE_MAXSIZE_32BIT,
				   0, dtag);
	if (error) {
		device_printf(dev, "can't create DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(*dtag, addr, BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 dmap);
	if (error) {
		device_printf(dev, "can't allocate DMA mem\n");
		bus_dma_tag_destroy(*dtag);
		*dtag = NULL;
		return error;
	}

	error = bus_dmamap_load(*dtag, *dmap, *addr, size,
				iwl_dma_ring_addr, paddr, BUS_DMA_WAITOK);
	if (error) {
		device_printf(dev, "can't load DMA mem\n");
		bus_dmamem_free(*dtag, *addr, *dmap);
		bus_dma_tag_destroy(*dtag);
		*dtag = NULL;
		return error;
	}
	return 0;
}

void
iwl_dma_mem_destroy(bus_dma_tag_t dtag, void *addr, bus_dmamap_t dmap)
{
	if (dtag != NULL) {
		bus_dmamap_unload(dtag, dmap);
		bus_dmamem_free(dtag, addr, dmap);
		bus_dma_tag_destroy(dtag);
	}
}

void
iwl_dma_buf_addr(void *xctx, bus_dma_segment_t *segs, int nsegs,
		 bus_size_t mapsz __unused, int error)
{
	struct iwl_dmamap_ctx *ctx = xctx;
	int i;

	if (error)
		return;

	if (nsegs > ctx->nsegs) {
		ctx->nsegs = 0;
		return;
	}

	ctx->nsegs = nsegs;
	for (i = 0; i < nsegs; ++i)
		ctx->segs[i] = segs[i];
}

static int
iwl_put_port(struct lwkt_port *port, struct lwkt_msg *lmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)lmsg;
	struct iwlcom *iwl = msg->iwlm_softc;

	ASSERT_SERIALIZED(port->mpu_serialize);

	if ((lmsg->ms_flags & MSGF_SYNC) && curthread == &iwl->iwl_thread) {
		msg->iwlm_nmsg.nm_dispatch(&msg->iwlm_nmsg);
		if ((lmsg->ms_flags & MSGF_DONE) == 0) {
			panic("%s: self-referential deadlock on "
			      "iwl thread port\n", __func__);
		}
		return EASYNC;
	} else {
		return iwl->iwl_fwd_port(port, lmsg);
	}
}

static void
iwl_service_loop(void *arg)
{
	struct iwlcom *iwl = arg;
	struct ifnet *ifp = &iwl->iwl_ic.ic_if;
	struct netmsg *nmsg;

	get_mplock();
	lwkt_serialize_enter(ifp->if_serializer);
	while ((nmsg = lwkt_waitport(&iwl->iwl_thread_port, 0))) {
		nmsg->nm_dispatch(nmsg);
		if (iwl->iwl_end)
			break;
	}
	lwkt_serialize_exit(ifp->if_serializer);
	rel_mplock();
}

void
iwl_create_thread(struct iwlcom *iwl, int unit)
{
	struct ifnet *ifp = &iwl->iwl_ic.ic_if;

	lwkt_initport_serialize(&iwl->iwl_reply_port, ifp->if_serializer);
	lwkt_initport_serialize(&iwl->iwl_thread_port, ifp->if_serializer);

	/* NB: avoid self-reference domsg */
	iwl->iwl_fwd_port = iwl->iwl_thread_port.mp_putport;
	iwl->iwl_thread_port.mp_putport = iwl_put_port;

	lwkt_create(iwl_service_loop, iwl, NULL, &iwl->iwl_thread,
		    0, unit % ncpus, "iwl%d", unit);
}

static void
iwl_destroy_thread_dispatch(struct netmsg *nmsg)
{
	struct iwlmsg *msg = (struct iwlmsg *)nmsg;
	struct iwlcom *iwl = msg->iwlm_softc;

	ASSERT_SERIALIZED(iwl->iwl_ic.ic_if.if_serializer);

	iwl->iwl_end = 1;
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

void
iwl_destroy_thread(struct iwlcom *iwl)
{
	struct iwlmsg msg;

	ASSERT_SERIALIZED(iwl->iwl_ic.ic_if.if_serializer);

	iwlmsg_init(&msg, &iwl->iwl_reply_port,
		    iwl_destroy_thread_dispatch, iwl);
	lwkt_domsg(&iwl->iwl_thread_port, &msg.iwlm_nmsg.nm_lmsg, 0);
}

void
iwlmsg_init(struct iwlmsg *msg, struct lwkt_port *rport, netisr_fn_t dispatch,
	    void *sc)
{
	netmsg_init(&msg->iwlm_nmsg, NULL, rport, 0, dispatch);
	msg->iwlm_softc = sc;
}

void
iwlmsg_send(struct iwlmsg *msg, struct lwkt_port *port)
{
	struct lwkt_msg *lmsg;

	lmsg = &msg->iwlm_nmsg.nm_lmsg;
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg(port, lmsg);
}
