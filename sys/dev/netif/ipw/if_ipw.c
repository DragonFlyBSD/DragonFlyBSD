/*-
 * Copyright (c) 2004, 2005
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $Id: if_ipw.c,v 1.7.2.1 2005/01/13 20:01:03 damien Exp $
 * $DragonFly: src/sys/dev/netif/ipw/Attic/if_ipw.c,v 1.2 2005/03/06 18:30:36 dillon Exp $
 */

/*-
 * Intel(R) PRO/Wireless 2100 MiniPCI driver
 * http://www.intel.com/network/connectivity/products/wireless/prowireless_mobile.htm
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/ucred.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/clock.h>
#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ifq_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_ioctl.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/if_wavelan_ieee.h>

#include "if_ipwreg.h"
#include "if_ipwvar.h"

#ifdef IPW_DEBUG
#define DPRINTF(x)	if (ipw_debug > 0) printf x
#define DPRINTFN(n, x)	if (ipw_debug >= (n)) printf x
int ipw_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, ipw, CTLFLAG_RW, &ipw_debug, 0, "ipw debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

MODULE_DEPEND(ipw, pci,  1, 1, 1);
MODULE_DEPEND(ipw, wlan, 1, 1, 1);

struct ipw_ident {
	u_int16_t	vendor;
	u_int16_t	device;
	const char	*name;
};

static const struct ipw_ident ipw_ident_table[] = {
	{ 0x8086, 0x1043, "Intel(R) PRO/Wireless 2100 MiniPCI" },

	{ 0, 0, NULL }
};

static const struct ieee80211_rateset ipw_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

static int		ipw_dma_alloc(struct ipw_softc *);
static void		ipw_release(struct ipw_softc *);
static int		ipw_media_change(struct ifnet *);
static void		ipw_media_status(struct ifnet *, struct ifmediareq *);
static int		ipw_newstate(struct ieee80211com *,
			    enum ieee80211_state, int);
static u_int16_t	ipw_read_prom_word(struct ipw_softc *, u_int8_t);
static void		ipw_command_intr(struct ipw_softc *,
			    struct ipw_soft_buf *);
static void		ipw_newstate_intr(struct ipw_softc *,
			    struct ipw_soft_buf *);
static void		ipw_data_intr(struct ipw_softc *, struct ipw_status *,
			    struct ipw_soft_bd *, struct ipw_soft_buf *);
static void		ipw_notification_intr(struct ipw_softc *,
			    struct ipw_soft_buf *);
static void		ipw_rx_intr(struct ipw_softc *);
static void		ipw_release_sbd(struct ipw_softc *,
			    struct ipw_soft_bd *);
static void		ipw_tx_intr(struct ipw_softc *);
static void		ipw_intr(void *);
static void		ipw_dma_map_txbuf(void *, bus_dma_segment_t *, int,
			    bus_size_t, int);
static void		ipw_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int		ipw_cmd(struct ipw_softc *, u_int32_t, void *,
			    u_int32_t);
static int		ipw_tx_start(struct ifnet *, struct mbuf *,
			    struct ieee80211_node *);
static void		ipw_start(struct ifnet *);
static void		ipw_watchdog(struct ifnet *);
static int		ipw_ioctl(struct ifnet *, u_long, caddr_t,
				struct ucred *cr);
static void		ipw_stop_master(struct ipw_softc *);
static int		ipw_reset(struct ipw_softc *);
static int		ipw_load_ucode(struct ipw_softc *, u_char *, int);
static int		ipw_load_firmware(struct ipw_softc *, u_char *, int);
static int		ipw_cache_firmware(struct ipw_softc *, void *);
static void		ipw_free_firmware(struct ipw_softc *);
static int		ipw_config(struct ipw_softc *);
static void		ipw_init(void *);
static void		ipw_stop(void *);
static int		ipw_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int		ipw_sysctl_radio(SYSCTL_HANDLER_ARGS);
static u_int32_t	ipw_read_table1(struct ipw_softc *, u_int32_t);
static void		ipw_write_table1(struct ipw_softc *, u_int32_t,
			    u_int32_t);
static int		ipw_read_table2(struct ipw_softc *, u_int32_t, void *,
			    u_int32_t *);
static void		ipw_read_mem_1(struct ipw_softc *, bus_size_t,
			    u_int8_t *, bus_size_t);
static void		ipw_write_mem_1(struct ipw_softc *, bus_size_t,
			    u_int8_t *, bus_size_t);

static __inline u_int8_t MEM_READ_1(struct ipw_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_1(sc, IPW_CSR_INDIRECT_DATA);
}

static __inline u_int32_t MEM_READ_4(struct ipw_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_4(sc, IPW_CSR_INDIRECT_DATA);
}

static int ipw_probe(device_t);
static int ipw_attach(device_t);
static int ipw_detach(device_t);
static int ipw_shutdown(device_t);
static int ipw_suspend(device_t);
static int ipw_resume(device_t);

static device_method_t ipw_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ipw_probe),
	DEVMETHOD(device_attach,	ipw_attach),
	DEVMETHOD(device_detach,	ipw_detach),
	DEVMETHOD(device_shutdown,	ipw_shutdown),
	DEVMETHOD(device_suspend,	ipw_suspend),
	DEVMETHOD(device_resume,	ipw_resume),

	{ 0, 0 }
};

static driver_t ipw_driver = {
	"ipw",
	ipw_methods,
	sizeof (struct ipw_softc)
};

static devclass_t ipw_devclass;

DRIVER_MODULE(ipw, pci, ipw_driver, ipw_devclass, 0, 0);

static int
ipw_probe(device_t dev)
{
	const struct ipw_ident *ident;

	for (ident = ipw_ident_table; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return 0;
		}
	}
	return ENXIO;
}

/* Base Address Register */
#define IPW_PCI_BAR0	0x10

static int
ipw_attach(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct sysctl_oid *sysctl_tree;
	u_int16_t val;
	int error, rid, i;

	sc->sc_dev = dev;

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	}

	pci_write_config(dev, 0x41, 0, 1);

	/* enable bus-mastering */
	pci_enable_busmaster(dev);

	/* map the register window */
	rid = IPW_PCI_BAR0;
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		goto fail;
	}

	sc->sc_st = rman_get_bustag(sc->mem);
	sc->sc_sh = rman_get_bushandle(sc->mem);

	rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
            RF_SHAREABLE);
	if (sc->irq == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		goto fail;
	}

	if (ipw_reset(sc) != 0) {
		device_printf(dev, "could not reset adapter\n");
		goto fail;
	}

	sysctl_ctx_init(&sc->sysctl_ctx);
	sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_hw),
		OID_AUTO, 
		device_get_nameunit(dev),
		CTLFLAG_RD,
		0, "");

	if (ipw_dma_alloc(sc) != 0) {
		device_printf(dev, "could not allocate DMA resources\n");
		goto fail;
	}

	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
       ic->ic_caps = IEEE80211_C_SHPREAMBLE | IEEE80211_C_TXPMGT |
           IEEE80211_C_PMGT | IEEE80211_C_IBSS | IEEE80211_C_MONITOR |
           IEEE80211_C_WEP;

	/* read MAC address from EEPROM */
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 0);
	ic->ic_myaddr[0] = val >> 8;
	ic->ic_myaddr[1] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 1);
	ic->ic_myaddr[2] = val >> 8;
	ic->ic_myaddr[3] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 2);
	ic->ic_myaddr[4] = val >> 8;
	ic->ic_myaddr[5] = val & 0xff;

	/* set supported .11b rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ipw_rateset_11b;

	/* set supported .11b channels (read from EEPROM) */
	if ((val = ipw_read_prom_word(sc, IPW_EEPROM_CHANNEL_LIST)) == 0)
		val = 0x7ff; /* default to channels 1-11 */
	val <<= 1;
	for (i = 1; i < 16; i++) {
		if (val & (1 << i)) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_B);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_B;
		}
	}

        /* check support for radio transmitter switch in EEPROM */
        if (!(ipw_read_prom_word(sc, IPW_EEPROM_RADIO) & 8))
                sc->flags |= IPW_FLAG_HAS_RADIO_SWITCH;

	/* default to authmode OPEN */
	sc->authmode = IEEE80211_AUTH_OPEN;

	/* IBSS channel undefined for now */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = ipw_init;
	ifp->if_ioctl = ipw_ioctl;
	ifp->if_start = ipw_start;
	ifp->if_watchdog = ipw_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	ieee80211_ifattach(ifp);
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ipw_newstate;
	ieee80211_media_init(ifp, ipw_media_change, ipw_media_status);

#ifdef WI_RAWBPF
	bpfattach2(ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64, &sc->sc_drvbpf);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IPW_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IPW_TX_RADIOTAP_PRESENT);
#endif

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sysctl_tree), OID_AUTO, "radio",
	    CTLTYPE_INT | CTLFLAG_RD, sc, 0, ipw_sysctl_radio, "I",
	    "Radio transmitter switch");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sysctl_tree), OID_AUTO, "stats",
	    CTLTYPE_OPAQUE | CTLFLAG_RD, sc, 0, ipw_sysctl_stats, "S",
	    "Statistics");

	/*
	 * Hook our interrupt after all initialization is complete
	 */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    ipw_intr, sc, &sc->sc_ih);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt\n");
		goto fail;
	}

	return 0;

fail:	ipw_detach(dev);
	return ENXIO;
}

static int
ipw_detach(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	sc->sc_intrmask = splimp();

	ipw_stop(sc);
	ipw_free_firmware(sc);

	splx(sc->sc_intrmask);

#ifdef WI_RAWBPF
	bpfdetach(ifp);
#endif
	ieee80211_ifdetach(ifp);

	ipw_release(sc);

	if (sc->irq != NULL) {
		bus_teardown_intr(dev, sc->irq, sc->sc_ih);
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	}

	if (sc->mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, IPW_PCI_BAR0,
		    sc->mem);

	sysctl_ctx_free(&sc->sysctl_ctx);

	return 0;
}

static int
ipw_dma_alloc(struct ipw_softc *sc)
{
	struct ipw_soft_bd *sbd;
        struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	bus_addr_t physaddr;
	int error, i;

	/*
	 * Allocate and map tx ring
	 */
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_TBD_SZ, 1, IPW_TBD_SZ, 0,
	    &sc->tbd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->tbd_dmat, (void **)&sc->tbd_list,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->tbd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate tx ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->tbd_dmat, sc->tbd_map, sc->tbd_list,
	    IPW_TBD_SZ, ipw_dma_map_addr, &sc->tbd_phys, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map tx ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate and map rx ring
	 */
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_RBD_SZ, 1, IPW_RBD_SZ, 0,
	    &sc->rbd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->rbd_dmat, (void **)&sc->rbd_list,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->rbd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate rx ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->rbd_dmat, sc->rbd_map, sc->rbd_list,
	    IPW_RBD_SZ, ipw_dma_map_addr, &sc->rbd_phys, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map rx ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate and map status ring
	 */
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_STATUS_SZ, 1, IPW_STATUS_SZ, 0,
	    &sc->status_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create status ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->status_dmat, (void **)&sc->status_list,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->status_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate status ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->status_dmat, sc->status_map,
	    sc->status_list, IPW_STATUS_SZ, ipw_dma_map_addr, &sc->status_phys,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not map status ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate command DMA map
	 */
	error = bus_dma_tag_create(NULL, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, sizeof (struct ipw_cmd), 1,
	    sizeof (struct ipw_cmd), 0, &sc->cmd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create command DMA tag\n");
		goto fail;
	}

	error = bus_dmamap_create(sc->cmd_dmat, 0, &sc->cmd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create command DMA map\n");
		goto fail;
	}

	/*
	 * Allocate headers DMA maps
	 */
	error = bus_dma_tag_create(NULL, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, sizeof (struct ipw_hdr), 1,
	    sizeof (struct ipw_hdr), 0, &sc->hdr_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create header DMA tag\n");
		goto fail;
	}

	SLIST_INIT(&sc->free_shdr);
	for (i = 0; i < IPW_NDATA; i++) {
                shdr = &sc->shdr_list[i];
		error = bus_dmamap_create(sc->hdr_dmat, 0, &shdr->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create header DMA map\n");
			goto fail;
		}
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
	}

	/*
	 * Allocate tx buffers DMA maps
	 */
	error = bus_dma_tag_create(NULL, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, IPW_MAX_NSEG, MCLBYTES, 0,
	    &sc->txbuf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx DMA tag\n");
		goto fail;
	}

	SLIST_INIT(&sc->free_sbuf);
	for (i = 0; i < IPW_NDATA; i++) {
		sbuf = &sc->tx_sbuf_list[i];
		error = bus_dmamap_create(sc->txbuf_dmat, 0, &sbuf->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create tx DMA map\n");
			goto fail;
		}
		SLIST_INSERT_HEAD(&sc->free_sbuf, sbuf, next);
	}

	/*
	 * Initialize tx ring
	 */
	for (i = 0; i < IPW_NTBD; i++) {
		sbd = &sc->stbd_list[i];
		sbd->bd = &sc->tbd_list[i];
		sbd->type = IPW_SBD_TYPE_NOASSOC;
	}

	/*
	 * Pre-allocate rx buffers and DMA maps
	 */
	error = bus_dma_tag_create(NULL, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, IPW_NRBD, MCLBYTES, 0,
	    &sc->rxbuf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx DMA tag\n");
		goto fail;
	}

	for (i = 0; i < IPW_NRBD; i++) {
		sbd = &sc->srbd_list[i];
		sbuf = &sc->rx_sbuf_list[i];
		sbd->bd = &sc->rbd_list[i];

		sbuf->m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (sbuf->m == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate rx mbuf\n");
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_create(sc->rxbuf_dmat, 0, &sbuf->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create rx DMA map\n");
			goto fail;
		}

		error = bus_dmamap_load(sc->rxbuf_dmat, sbuf->map,
		    mtod(sbuf->m, void *), MCLBYTES, ipw_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map rx DMA memory\n");
			goto fail;
		}

		sbd->type = IPW_SBD_TYPE_DATA;
		sbd->priv = sbuf;
		sbd->bd->physaddr = htole32(physaddr);
		sbd->bd->len = htole32(MCLBYTES);
	}

	bus_dmamap_sync(sc->rbd_dmat, sc->rbd_map, BUS_DMASYNC_PREWRITE);

	return 0;

fail:	ipw_release(sc);
	return error;
}

static void
ipw_release(struct ipw_softc *sc)
{
	struct ipw_soft_buf *sbuf;
	int i;

	if (sc->tbd_dmat != NULL) {
		if (sc->stbd_list != NULL) {
			bus_dmamap_unload(sc->tbd_dmat, sc->tbd_map);
			bus_dmamem_free(sc->tbd_dmat, sc->tbd_list,
			    sc->tbd_map);
		}
		bus_dma_tag_destroy(sc->tbd_dmat);
	}

	if (sc->rbd_dmat != NULL) {
		if (sc->rbd_list != NULL) {
			bus_dmamap_unload(sc->rbd_dmat, sc->rbd_map);
			bus_dmamem_free(sc->rbd_dmat, sc->rbd_list,
			    sc->rbd_map);
		}
		bus_dma_tag_destroy(sc->rbd_dmat);
	}

	if (sc->status_dmat != NULL) {
		if (sc->status_list != NULL) {
			bus_dmamap_unload(sc->status_dmat, sc->status_map);
			bus_dmamem_free(sc->status_dmat, sc->status_list,
			    sc->status_map);
		}
		bus_dma_tag_destroy(sc->status_dmat);
	}

	for (i = 0; i < IPW_NTBD; i++)
		ipw_release_sbd(sc, &sc->stbd_list[i]);

	if (sc->cmd_dmat != NULL) {
		bus_dmamap_destroy(sc->cmd_dmat, sc->cmd_map);
		bus_dma_tag_destroy(sc->cmd_dmat);
	}

	if (sc->hdr_dmat != NULL) {
		for (i = 0; i < IPW_NDATA; i++)
			bus_dmamap_destroy(sc->hdr_dmat, sc->shdr_list[i].map);
		bus_dma_tag_destroy(sc->hdr_dmat);
	}

	if (sc->txbuf_dmat != NULL) {
		for (i = 0; i < IPW_NDATA; i++) {
			bus_dmamap_destroy(sc->txbuf_dmat,
			    sc->tx_sbuf_list[i].map);
		}
		bus_dma_tag_destroy(sc->txbuf_dmat);
	}

	if (sc->rxbuf_dmat != NULL) {
		for (i = 0; i < IPW_NRBD; i++) {
			sbuf = &sc->rx_sbuf_list[i];
			if (sbuf->m != NULL) {
				bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->rxbuf_dmat, sbuf->map);
				m_freem(sbuf->m);
			}
			bus_dmamap_destroy(sc->rxbuf_dmat, sbuf->map);
		}
		bus_dma_tag_destroy(sc->rxbuf_dmat);
	}
}

static int
ipw_shutdown(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);

	sc->sc_intrmask = splimp();

	ipw_stop(sc);

	sc->sc_intrmask = splimp();

	return 0;
}

static int
ipw_suspend(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);

	sc->sc_intrmask = splimp();

	ipw_stop(sc);

	splx(sc->sc_intrmask);

	return 0;
}

static int
ipw_resume(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	sc->sc_intrmask = splimp();

	pci_write_config(dev, 0x41, 0, 1);

	if (ifp->if_flags & IFF_UP) {
		ifp->if_init(ifp->if_softc);
		if (ifp->if_flags & IFF_RUNNING)
			ifp->if_start(ifp);
	}

	splx(sc->sc_intrmask);

	return 0;
}

static int
ipw_media_change(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;
	int error;

	sc->sc_intrmask = splimp();

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET) {
		splx(sc->sc_intrmask);
		return error;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		ipw_init(sc);

	splx(sc->sc_intrmask);

	return 0;
}

static void
ipw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
#define N(a)	(sizeof (a) / sizeof (a[0]))
	static const struct {
		u_int32_t	val;
		int		rate;
	} rates[] = {
		{ IPW_RATE_DS1,   2 },
		{ IPW_RATE_DS2,   4 },
		{ IPW_RATE_DS5,  11 },
		{ IPW_RATE_DS11, 22 },
	};
	u_int32_t val;
	int rate, i;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	/* read current transmission rate from adapter */
	val = ipw_read_table1(sc, IPW_INFO_CURRENT_TX_RATE) & 0xf;

	/* convert rate to 802.11 rate */
	for (i = 0; i < N(rates) && rates[i].val != val; i++);
	rate = (i < N(rates)) ? rates[i].rate : 0;

	imr->ifm_active |= IFM_IEEE80211_11B;
	imr->ifm_active |= ieee80211_rate2media(ic, rate, IEEE80211_MODE_11B);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;

	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_IBSS;
		break;

	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;

	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_HOSTAP:
		/* should not get there */
		break;
	}
#undef N
}

static int
ipw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ipw_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni = ic->ic_bss;
	u_int32_t len;
	u_int8_t val;

	switch (nstate) {
	case IEEE80211_S_RUN:
		len = IEEE80211_NWID_LEN;
		ipw_read_table2(sc, IPW_INFO_CURRENT_SSID, ni->ni_essid, &len);
		ni->ni_esslen = len;

		val = ipw_read_table1(sc, IPW_INFO_CURRENT_CHANNEL);
		ni->ni_chan = &ic->ic_channels[val];

		DELAY(100); /* firmware needs a short delay here */

		len = IEEE80211_ADDR_LEN;
		ipw_read_table2(sc, IPW_INFO_CURRENT_BSSID, ni->ni_bssid, &len);
		IEEE80211_ADDR_COPY(ni->ni_macaddr, ni->ni_bssid);
		break;

	case IEEE80211_S_INIT:
	case IEEE80211_S_SCAN:
	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		break;
	}

	ic->ic_state = nstate;
	return 0;
}

/*
 * Read 16 bits at address 'addr' from the Microwire EEPROM.
 * DON'T PLAY WITH THIS CODE UNLESS YOU KNOW *EXACTLY* WHAT YOU'RE DOING!
 */
static u_int16_t
ipw_read_prom_word(struct ipw_softc *sc, u_int8_t addr)
{
	u_int32_t tmp;
	u_int16_t val;
	int n;

	/* Clock C once before the first command */
	IPW_EEPROM_CTL(sc, 0);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);

	/* Write start bit (1) */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D | IPW_EEPROM_C);

	/* Write READ opcode (10) */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D | IPW_EEPROM_C);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);

	/* Write address A7-A0 */
	for (n = 7; n >= 0; n--) {
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S |
		    (((addr >> n) & 1) << IPW_EEPROM_SHIFT_D));
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S |
		    (((addr >> n) & 1) << IPW_EEPROM_SHIFT_D) | IPW_EEPROM_C);
	}

	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);

	/* Read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
		tmp = MEM_READ_4(sc, IPW_MEM_EEPROM_CTL);
		val |= ((tmp & IPW_EEPROM_Q) >> IPW_EEPROM_SHIFT_Q) << n;
	}

	IPW_EEPROM_CTL(sc, 0);

	/* Clear Chip Select and clock C */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, 0);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_C);

	return le16toh(val);
}

static void
ipw_scan_result(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	u_int32_t i, cnt, off;
	struct ipw_node ap;

	/* flush previously seen access points */
	ieee80211_free_allnodes(ic);

	cnt = ipw_read_table1(sc, IPW_INFO_APS_CNT);
	off = ipw_read_table1(sc, IPW_INFO_APS_BASE);

	DPRINTF(("Found %u APs\n", cnt));

	for (i = 0; i < cnt; i++) {
		ipw_read_mem_1(sc, off, (u_int8_t *)&ap, sizeof ap);
		off += sizeof ap;

#ifdef IPW_DEBUG
		if (ipw_debug >= 2) {
			u_char *p = (u_char *)&ap;
			int j;

			printf("AP%u\n", i);
			for (j = 0; j < sizeof ap; j++)
				printf("%02x", *p++);
			printf("\n");
		}
#endif

		ni = ieee80211_lookup_node(ic, ap.bssid,
		    &ic->ic_channels[ap.chan]);
		if (ni != NULL)
			continue;

		ni = ieee80211_alloc_node(ic, ap.bssid);
		if (ni == NULL)
			return;

		IEEE80211_ADDR_COPY(ni->ni_bssid, ap.bssid);
		ni->ni_rssi = ap.rssi;
		ni->ni_intval = le16toh(ap.intval);
		ni->ni_capinfo = le16toh(ap.capinfo);
		ni->ni_chan = &ic->ic_channels[ap.chan];
		ni->ni_esslen = ap.esslen;
		bcopy(ap.essid, ni->ni_essid, IEEE80211_NWID_LEN);
	}
}

static void
ipw_command_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	struct ipw_cmd *cmd;

	cmd = mtod(sbuf->m, struct ipw_cmd *);

	DPRINTFN(2, ("RX!CMD!%u!%u!%u!%u!%u\n",
	    le32toh(cmd->type), le32toh(cmd->subtype), le32toh(cmd->seq),
	    le32toh(cmd->len), le32toh(cmd->status)));

	wakeup(sc);
}

static void
ipw_newstate_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int32_t state;

	state = le32toh(*mtod(sbuf->m, u_int32_t *));

	DPRINTFN(2, ("RX!NEWSTATE!%u\n", state));

	switch (state) {
	case IPW_STATE_ASSOCIATED:
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		break;

	case IPW_STATE_SCANNING:
		/* don't leave run state on background scan */
		if (ic->ic_state != IEEE80211_S_RUN)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		break;

	case IPW_STATE_SCAN_COMPLETE:
		ipw_scan_result(sc);
		break;

	case IPW_STATE_ASSOCIATION_LOST:
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
		break;

	case IPW_STATE_RADIO_DISABLED:
		sc->sc_ic.ic_if.if_flags &= ~IFF_UP;
		ipw_stop(sc);
		break;
	}
}

static void
ipw_data_intr(struct ipw_softc *sc, struct ipw_status *status,
    struct ipw_soft_bd *sbd, struct ipw_soft_buf *sbuf)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	bus_addr_t physaddr;
	int error;

	DPRINTFN(5, ("RX!DATA!%u!%u\n", le32toh(status->len), status->rssi));

	if (le32toh(status->len) < sizeof (struct ieee80211_frame_min) ||
	    le32toh(status->len) > MCLBYTES) {
		device_printf(sc->sc_dev, "bad frame length\n");
		return;
	}

	bus_dmamap_unload(sc->rxbuf_dmat, sbuf->map);

	/* Finalize mbuf */
	m = sbuf->m;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = le32toh(status->len);

#ifdef WI_RAWBPF
	if (sc->sc_drvbpf != NULL) {
		struct ipw_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_antsignal = status->rssi;
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		bpf_mtap2(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m);
	}
#endif

	wh = mtod(m, struct ieee80211_frame *);

	if (ic->ic_opmode != IEEE80211_M_STA) {
		ni = ieee80211_find_node(ic, wh->i_addr2);
		if (ni == NULL)
			ni = ieee80211_ref_node(ic->ic_bss);
	} else
		ni = ieee80211_ref_node(ic->ic_bss);

	/* Send the frame to the upper layer */
	ieee80211_input(ifp, m, ni, status->rssi, 0);

	if (ni == ic->ic_bss)
		ieee80211_unref_node(&ni);
	else
		ieee80211_free_node(ic, ni);

	m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		device_printf(sc->sc_dev, "could not allocate rx mbuf\n");
		sbuf->m = NULL;
		return;
	}

	error = bus_dmamap_load(sc->rxbuf_dmat, sbuf->map, mtod(m, void *),
	    MCLBYTES, ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map rx DMA memory\n");
		m_freem(m);
		sbuf->m = NULL;
		return;
	}

	sbuf->m = m;
	sbd->bd->physaddr = htole32(physaddr);
}

static void
ipw_notification_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	DPRINTFN(2, ("RX!NOTIFICATION\n"));
}

static void
ipw_rx_intr(struct ipw_softc *sc)
{
	struct ipw_status *status;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_buf *sbuf;
	u_int32_t r, i;

	if (!(sc->flags & IPW_FLAG_FW_INITED))
		return;

	r = CSR_READ_4(sc, IPW_CSR_RX_READ_INDEX);

	bus_dmamap_sync(sc->status_dmat, sc->status_map, BUS_DMASYNC_POSTREAD);

	for (i = (sc->rxcur + 1) % IPW_NRBD; i != r; i = (i + 1) % IPW_NRBD) {

		status = &sc->status_list[i];
		sbd = &sc->srbd_list[i];
		sbuf = sbd->priv;

		bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map,
		    BUS_DMASYNC_POSTREAD);

		switch (le16toh(status->code) & 0xf) {
		case IPW_STATUS_CODE_COMMAND:
			ipw_command_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_NEWSTATE:
			ipw_newstate_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_DATA_802_3:
		case IPW_STATUS_CODE_DATA_802_11:
			ipw_data_intr(sc, status, sbd, sbuf);
			break;

		case IPW_STATUS_CODE_NOTIFICATION:
			ipw_notification_intr(sc, sbuf);
			break;

		default:
			device_printf(sc->sc_dev, "unknown status code %u\n",
			    le16toh(status->code));
		}

		/* firmware was killed, stop processing received frames */
		if (!(sc->flags & IPW_FLAG_FW_INITED))
			return;

		sbd->bd->flags = 0;
	}
	/* Some buffer descriptors may have changed */
	bus_dmamap_sync(sc->rbd_dmat, sc->rbd_map, BUS_DMASYNC_PREWRITE);

	/* Tell the firmware what we have processed */
	sc->rxcur = (r == 0) ? IPW_NRBD - 1 : r - 1;
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE_INDEX, sc->rxcur);
}

static void
ipw_release_sbd(struct ipw_softc *sc, struct ipw_soft_bd *sbd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;

	switch (sbd->type) {
	case IPW_SBD_TYPE_COMMAND:
		bus_dmamap_sync(sc->cmd_dmat, sc->cmd_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->cmd_dmat, sc->cmd_map);
		break;

	case IPW_SBD_TYPE_HEADER:
		shdr = sbd->priv;
		bus_dmamap_sync(sc->hdr_dmat, shdr->map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->hdr_dmat, shdr->map);
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
		break;

	case IPW_SBD_TYPE_DATA:
		sbuf = sbd->priv;
		bus_dmamap_sync(sc->txbuf_dmat, sbuf->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->txbuf_dmat, sbuf->map);
		SLIST_INSERT_HEAD(&sc->free_sbuf, sbuf, next);

		m_freem(sbuf->m);

		if (sbuf->ni != NULL && sbuf->ni != ic->ic_bss)
			ieee80211_free_node(ic, sbuf->ni);

		/* kill watchdog timer */
		sc->sc_tx_timer = 0;
		break;
	}
	sbd->type = IPW_SBD_TYPE_NOASSOC;
}

static void
ipw_tx_intr(struct ipw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	u_int32_t r, i;

	if (!(sc->flags & IPW_FLAG_FW_INITED))
		return;

	r = CSR_READ_4(sc, IPW_CSR_TX_READ_INDEX);

	for (i = (sc->txold + 1) % IPW_NTBD; i != r; i = (i + 1) % IPW_NTBD) {
		ipw_release_sbd(sc, &sc->stbd_list[i]);
		sc->txfree++;
	}

	/* Remember what the firmware has processed */
	sc->txold = (r == 0) ? IPW_NTBD - 1 : r - 1;

	/* Call start() since some buffer descriptors have been released */
	ifp->if_flags &= ~IFF_OACTIVE;
	(*ifp->if_start)(ifp);
}

static void
ipw_intr(void *arg)
{
	struct ipw_softc *sc = arg;
	u_int32_t r;

	sc->sc_intrmask = splimp();

	if ((r = CSR_READ_4(sc, IPW_CSR_INTR)) == 0 || r == 0xffffffff) {
		splx(sc->sc_intrmask);
		return;
	}

	/* Disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	DPRINTFN(8, ("INTR!0x%08x\n", r));

	if (r & (IPW_INTR_FATAL_ERROR | IPW_INTR_PARITY_ERROR)) {
		device_printf(sc->sc_dev, "fatal error\n");
		sc->sc_ic.ic_if.if_flags &= ~IFF_UP;
		ipw_stop(sc);
	}

	if (r & IPW_INTR_FW_INIT_DONE) {
		if (!(r & (IPW_INTR_FATAL_ERROR | IPW_INTR_PARITY_ERROR)))
			wakeup(sc);
	}

	if (r & IPW_INTR_RX_TRANSFER)
		ipw_rx_intr(sc);

	if (r & IPW_INTR_TX_TRANSFER)
		ipw_tx_intr(sc);

	/* Acknowledge interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR, r);

	/* Re-enable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);

	splx(sc->sc_intrmask);
}

static void
ipw_dma_map_txbuf(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t mapsize, int error)
{
	struct ipw_dma_mapping *map = arg;

	if (error != 0)
		return;

	KASSERT(nseg <= IPW_MAX_NSEG, ("too many DMA segments %d", nseg));

	bcopy(segs, map->segs, nseg * sizeof (bus_dma_segment_t));
	map->nseg = nseg;
	map->mapsize = mapsize;
}

static void
ipw_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error != 0)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
ipw_cmd(struct ipw_softc *sc, u_int32_t type, void *data, u_int32_t len)
{
	struct ipw_soft_bd *sbd;
	bus_addr_t physaddr;
	int error;

	sbd = &sc->stbd_list[sc->txcur];

	error = bus_dmamap_load(sc->cmd_dmat, sc->cmd_map, &sc->cmd,
	    sizeof (struct ipw_cmd), ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map command DMA memory\n");
		return error;
	}

	sc->cmd.type = htole32(type);
	sc->cmd.subtype = htole32(0);
	sc->cmd.len = htole32(len);
	sc->cmd.seq = htole32(0);
	if (data != NULL)
		bcopy(data, sc->cmd.data, len);

	sbd->type = IPW_SBD_TYPE_COMMAND;
	sbd->bd->physaddr = htole32(physaddr);
	sbd->bd->len = htole32(sizeof (struct ipw_cmd));
	sbd->bd->nfrag = 1;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_COMMAND |
			 IPW_BD_FLAG_TX_LAST_FRAGMENT;

	bus_dmamap_sync(sc->cmd_dmat, sc->cmd_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->tbd_dmat, sc->tbd_map, BUS_DMASYNC_PREWRITE);

	sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	sc->txfree--;
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, sc->txcur);

	DPRINTFN(2, ("TX!CMD!%u!%u!%u!%u\n", type, 0, 0, len));

	/* wait at most one second for command to complete */
	return tsleep(sc, 0, "ipwcmd", hz);
}

static int
ipw_tx_start(struct ifnet *ifp, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ipw_dma_mapping map;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	struct mbuf *mnew;
	bus_addr_t physaddr;
	int error, i;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		m0 = ieee80211_wep_crypt(ifp, m0, 1);
		if (m0 == NULL)
			return ENOBUFS;
	}

#ifdef WI_RAWBPF
	if (sc->sc_drvbpf != NULL) {
		struct ipw_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		bpf_mtap2(sc->sc_drvbpf, tap, sc->sc_txtap_len, m0);
	}
#endif

	wh = mtod(m0, struct ieee80211_frame *);

	shdr = SLIST_FIRST(&sc->free_shdr);
	sbuf = SLIST_FIRST(&sc->free_sbuf);
	KASSERT(shdr != NULL && sbuf != NULL, ("empty sw hdr/buf pool"));

	shdr->hdr.type = htole32(IPW_HDR_TYPE_SEND);
	shdr->hdr.subtype = htole32(0);
	shdr->hdr.encrypted = (wh->i_fc[1] & IEEE80211_FC1_WEP) ? 1 : 0;
	shdr->hdr.encrypt = 0;
	shdr->hdr.keyidx = 0;
	shdr->hdr.keysz = 0;
	shdr->hdr.fragmentsz = htole16(0);
	IEEE80211_ADDR_COPY(shdr->hdr.src_addr, wh->i_addr2);
	if (ic->ic_opmode == IEEE80211_M_STA)
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr3);
	else
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr1);

	/* trim IEEE802.11 header */
	m_adj(m0, sizeof (struct ieee80211_frame));

	error = bus_dmamap_load_mbuf(sc->txbuf_dmat, sbuf->map, m0,
	    ipw_dma_map_txbuf, &map, 0);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}
	if (error != 0) {
		mnew = m_defrag(m0, MB_DONTWAIT);
		if (mnew == NULL) {
			device_printf(sc->sc_dev,
			    "could not defragment mbuf\n");
			m_freem(m0);
			return ENOBUFS;
		}
		m0 = mnew;

		error = bus_dmamap_load_mbuf(sc->txbuf_dmat, sbuf->map, m0,
		    ipw_dma_map_txbuf, &map, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map mbuf (error %d)\n", error);
			m_freem(m0);
			return error;
		}
	}

	error = bus_dmamap_load(sc->hdr_dmat, shdr->map, &shdr->hdr,
	    sizeof (struct ipw_hdr), ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map header DMA memory\n");
		bus_dmamap_unload(sc->txbuf_dmat, sbuf->map);
		m_freem(m0);
		return error;
	}

	SLIST_REMOVE_HEAD(&sc->free_sbuf, next);
	SLIST_REMOVE_HEAD(&sc->free_shdr, next);

	sbd = &sc->stbd_list[sc->txcur];
	sbd->type = IPW_SBD_TYPE_HEADER;
	sbd->priv = shdr;
	sbd->bd->physaddr = htole32(physaddr);
	sbd->bd->len = htole32(sizeof (struct ipw_hdr));
	sbd->bd->nfrag = 1 + map.nseg;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3 |
			 IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;

	DPRINTFN(5, ("TX!HDR!%u!%u!%u!%u!%6D!%6D\n", shdr->hdr.type,
	    shdr->hdr.subtype, shdr->hdr.encrypted, shdr->hdr.encrypt,
	    shdr->hdr.src_addr, ":", shdr->hdr.dst_addr, ":"));
	sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	sc->txfree--;

	sbuf->m = m0;
	sbuf->ni = ni;

	for (i = 0; i < map.nseg; i++) {
		sbd = &sc->stbd_list[sc->txcur];

		sbd->bd->physaddr = htole32(map.segs[i].ds_addr);
		sbd->bd->len = htole32(map.segs[i].ds_len);
		sbd->bd->nfrag = 0; /* used only in first bd */
		sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3;
		if (i == map.nseg - 1) {
			sbd->type = IPW_SBD_TYPE_DATA;
			sbd->priv = sbuf;
			sbd->bd->flags |= IPW_BD_FLAG_TX_LAST_FRAGMENT;
		} else {
			sbd->type = IPW_SBD_TYPE_NOASSOC;
			sbd->bd->flags |= IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;
		}

		DPRINTFN(5, ("TX!FRAG!%d!%d\n", i, map.segs[i].ds_len));
		sc->txcur = (sc->txcur + 1) % IPW_NTBD;
		sc->txfree--;
	}

	bus_dmamap_sync(sc->hdr_dmat, shdr->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->txbuf_dmat, sbuf->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->tbd_dmat, sc->tbd_map, BUS_DMASYNC_PREWRITE);

	/* Inform firmware about this new packet */
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, sc->txcur);

	return 0;
}

static void
ipw_start(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m0;
	struct ieee80211_node *ni;

#if 0
	sc->sc_intrmask = splimp();
#endif
	if (ic->ic_state != IEEE80211_S_RUN) {
#if 0
	   splx(sc->sc_intrmask);
#endif	   
		return;
	}

	for (;;) {
		IF_DEQUEUE(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;
	if (sc->txfree < 1 + IPW_MAX_NSEG) {
		IF_PREPEND(&ifp->if_snd, m0);
		ifp->if_flags |= IFF_OACTIVE;
		break;
	}


#ifdef WI_RAWBPF
		BPF_MTAP(ifp, m0);
#endif

		m0 = ieee80211_encap(ifp, m0, &ni);
		if (m0 == NULL)
			continue;

#ifdef WI_RAWBPF
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m0);
#endif

		if (ipw_tx_start(ifp, m0, ni) != 0) {
			if (ni != NULL && ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);
			break;
		}

		/* start watchdog timer */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
#if 0
	splx(sc->sc_intrmask);
#endif
}

static void
ipw_watchdog(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			if_printf(ifp, "device timeout\n");
			ifp->if_flags &= ~IFF_UP;
			ipw_stop(sc);
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

static int
ipw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifreq *ifr;
	struct ieee80211req *ireq;
	int error = 0;

	sc->sc_intrmask = splimp();

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				ipw_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ipw_stop(sc);
		}
		break;

	case SIOCSLOADFW:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ifr = (struct ifreq *)data;
		error = ipw_cache_firmware(sc, ifr->ifr_data);
		break;

	case SIOCSKILLFW:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ifp->if_flags &= ~IFF_UP;
		ipw_stop(sc);
		ipw_free_firmware(sc);
		break;

	case SIOCG80211:
		ireq = (struct ieee80211req *)data;
		switch (ireq->i_type) {
		case IEEE80211_IOC_AUTHMODE:
			ireq->i_val = sc->authmode;
			break;

		case IEEE80211_IOC_TXPOWER:
			ireq->i_val = (CSR_READ_4(sc, IPW_CSR_IO) &
			    IPW_IO_RADIO_DISABLED) ? 0 : ic->ic_txpower;
			break;

		default:
			error = ieee80211_ioctl(ifp, cmd, data, cr);
		}
		break;

	case SIOCS80211:
		/* only super-user can do that! */
		if ((error = suser(curthread)) != 0)
			break;

		ireq = (struct ieee80211req *)data;
		switch (ireq->i_type) {
		case IEEE80211_IOC_AUTHMODE:
			sc->authmode = ireq->i_val;
			break;

		default:
			error = ieee80211_ioctl(ifp, cmd, data, cr);
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data, cr);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			ipw_init(sc);
		error = 0;
	}

	splx(sc->sc_intrmask);

	return error;
}

static void
ipw_stop_master(struct ipw_softc *sc)
{
	int ntries;

	/* Disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	CSR_WRITE_4(sc, IPW_CSR_RST, IPW_RST_STOP_MASTER);
	for (ntries = 0; ntries < 5; ntries++) {
		if (CSR_READ_4(sc, IPW_CSR_RST) & IPW_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 5)
		device_printf(sc->sc_dev, "timeout waiting for master\n");

	CSR_WRITE_4(sc, IPW_CSR_RST, CSR_READ_4(sc, IPW_CSR_RST) |
	    IPW_RST_PRINCETON_RESET);

	sc->flags &= ~IPW_FLAG_FW_INITED;
}

static int
ipw_reset(struct ipw_softc *sc)
{
	int ntries;

	ipw_stop_master(sc);

	/* Move adapter to D0 state */
	CSR_WRITE_4(sc, IPW_CSR_CTL, CSR_READ_4(sc, IPW_CSR_CTL) |
	    IPW_CTL_INIT);

	/* Wait for clock stabilization */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (CSR_READ_4(sc, IPW_CSR_CTL) & IPW_CTL_CLOCK_READY)
			break;
		DELAY(200);
	}
	if (ntries == 1000)
		return EIO;

	CSR_WRITE_4(sc, IPW_CSR_RST, CSR_READ_4(sc, IPW_CSR_RST) |
	    IPW_RST_SW_RESET);

	DELAY(10);

	CSR_WRITE_4(sc, IPW_CSR_CTL, CSR_READ_4(sc, IPW_CSR_CTL) |
	    IPW_CTL_INIT);

	return 0;
}

static int
ipw_load_ucode(struct ipw_softc *sc, u_char *uc, int size)
{
	int ntries;

	MEM_WRITE_4(sc, 0x3000e0, 0x80000000);
	CSR_WRITE_4(sc, IPW_CSR_RST, 0);

	MEM_WRITE_2(sc, 0x220000, 0x0703);
	MEM_WRITE_2(sc, 0x220000, 0x0707);

	MEM_WRITE_1(sc, 0x210014, 0x72);
	MEM_WRITE_1(sc, 0x210014, 0x72);

	MEM_WRITE_1(sc, 0x210000, 0x40);
	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x40);

	MEM_WRITE_MULTI_1(sc, 0x210010, uc, size);

	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x80);

	MEM_WRITE_2(sc, 0x220000, 0x0703);
	MEM_WRITE_2(sc, 0x220000, 0x0707);

	MEM_WRITE_1(sc, 0x210014, 0x72);
	MEM_WRITE_1(sc, 0x210014, 0x72);

	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x80);

	for (ntries = 0; ntries < 100; ntries++) {
		if (MEM_READ_1(sc, 0x210000) & 1)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "timeout waiting for ucode to initialize\n");
		return EIO;
	}

	MEM_WRITE_4(sc, 0x3000e0, 0);

	return 0;
}

/* set of macros to handle unaligned little endian data in firmware image */
#define GETLE32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
#define GETLE16(p) ((p)[0] | (p)[1] << 8)
static int
ipw_load_firmware(struct ipw_softc *sc, u_char *fw, int size)
{
	u_char *p, *end;
	u_int32_t dst;
	u_int16_t len;
	int error;

	p = fw;
	end = fw + size;
	while (p < end) {
		if (p + 6 > end)
			return EINVAL;

		dst = GETLE32(p); p += 4;
		len = GETLE16(p); p += 2;

		if (p + len > end)
			return EINVAL;

		ipw_write_mem_1(sc, dst, p, len);
		p += len;
	}

	CSR_WRITE_4(sc, IPW_CSR_IO, IPW_IO_GPIO1_ENABLE | IPW_IO_GPIO3_MASK |
	    IPW_IO_LED_OFF);

	/* Allow interrupts so we know when the firmware is inited */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);

	/* Tell the adapter to initialize the firmware */
	CSR_WRITE_4(sc, IPW_CSR_RST, 0);
	CSR_WRITE_4(sc, IPW_CSR_CTL, CSR_READ_4(sc, IPW_CSR_CTL) |
	    IPW_CTL_ALLOW_STANDBY);

	/* Wait at most one second for firmware initialization to complete */
	if ((error = tsleep(sc, 0, "ipwinit", hz)) != 0) {
		device_printf(sc->sc_dev, "timeout waiting for firmware "
		    "initialization to complete\n");
		return error;
	}

	CSR_WRITE_4(sc, IPW_CSR_IO, CSR_READ_4(sc, IPW_CSR_IO) |
	    IPW_IO_GPIO1_MASK | IPW_IO_GPIO3_MASK);

	return 0;
}

/*
 * Store firmware into kernel memory so we can download it when we need to,
 * e.g when the adapter wakes up from suspend mode.
 */
static int
ipw_cache_firmware(struct ipw_softc *sc, void *data)
{
	struct ipw_firmware *fw = &sc->fw;
	struct ipw_firmware_hdr hdr;
	u_char *p = data;
	int error;

	ipw_free_firmware(sc);

	/*
	 * mutex(9): no mutexes should be held across functions which access
	 * memory in userspace, such as copyin(9) [...]
	 */
	splx(sc->sc_intrmask);

	if ((error = copyin(data, &hdr, sizeof hdr)) != 0)
		goto fail1;

	fw->main_size  = le32toh(hdr.main_size);
	fw->ucode_size = le32toh(hdr.ucode_size);
	p += sizeof hdr;

	fw->main = malloc(fw->main_size, M_DEVBUF, M_WAITOK);
	if (fw->main == NULL) {
		error = ENOMEM;
		goto fail1;
	}

	fw->ucode = malloc(fw->ucode_size, M_DEVBUF, M_WAITOK);
	if (fw->ucode == NULL) {
		error = ENOMEM;
		goto fail2;
	}

	if ((error = copyin(p, fw->main, fw->main_size)) != 0)
		goto fail3;

	p += fw->main_size;
	if ((error = copyin(p, fw->ucode, fw->ucode_size)) != 0)
		goto fail3;

	DPRINTF(("Firmware cached: main %u, ucode %u\n", fw->main_size,
	    fw->ucode_size));

	sc->sc_intrmask = splimp();

	sc->flags |= IPW_FLAG_FW_CACHED;

	return 0;

fail3:	free(fw->ucode, M_DEVBUF);
fail2:	free(fw->main, M_DEVBUF);
fail1:	sc->sc_intrmask = splimp();

	return error;
}

static void
ipw_free_firmware(struct ipw_softc *sc)
{
	if (!(sc->flags & IPW_FLAG_FW_CACHED))
		return;

	free(sc->fw.main, M_DEVBUF);
	free(sc->fw.ucode, M_DEVBUF);

	sc->flags &= ~IPW_FLAG_FW_CACHED;
}

static int
ipw_config(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ipw_security security;
	struct ieee80211_wepkey *k;
	struct ipw_wep_key wepkey;
	struct ipw_scan_options options;
	struct ipw_configuration config;
	u_int32_t data;
	int error, i;

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
	case IEEE80211_M_HOSTAP:
		data = htole32(IPW_MODE_BSS);
		break;

	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		data = htole32(IPW_MODE_IBSS);
		break;

	case IEEE80211_M_MONITOR:
		data = htole32(IPW_MODE_MONITOR);
		break;
	}
	DPRINTF(("Setting mode to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_MODE, &data, sizeof data);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS ||
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		data = htole32(ieee80211_chan2ieee(ic, ic->ic_ibss_chan));
		DPRINTF(("Setting channel to %u\n", le32toh(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_CHANNEL, &data, sizeof data);
		if (error != 0)
			return error;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		DPRINTF(("Enabling adapter\n"));
		return ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
	}

	IEEE80211_ADDR_COPY(((struct arpcom *)ifp)->ac_enaddr, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(IF_LLADDR(ifp), ic->ic_myaddr);
	DPRINTF(("Setting MAC address to %6D\n", ic->ic_myaddr, ":"));
	error = ipw_cmd(sc, IPW_CMD_SET_MAC_ADDRESS, ic->ic_myaddr,
	    IEEE80211_ADDR_LEN);
	if (error != 0)
		return error;

	config.flags = htole32(IPW_CFG_BSS_MASK | IPW_CFG_IBSS_MASK |
	    IPW_CFG_PREAMBLE_AUTO | IPW_CFG_802_1x_ENABLE);
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		config.flags |= htole32(IPW_CFG_IBSS_AUTO_START);
	if (ifp->if_flags & IFF_PROMISC)
		config.flags |= htole32(IPW_CFG_PROMISCUOUS);
	config.bss_chan = htole32(0x3fff); /* channels 1-14 */
	config.ibss_chan = htole32(0x7ff); /* channels 1-11 */
	DPRINTF(("Setting configuration to 0x%x\n", le32toh(config.flags)));
	error = ipw_cmd(sc, IPW_CMD_SET_CONFIGURATION, &config, sizeof config);
	if (error != 0)
		return error;

	data = htole32(0x3); /* 1, 2 */
	DPRINTF(("Setting basic tx rates to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_BASIC_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(0xf); /* 1, 2, 5.5, 11 */
	DPRINTF(("Setting tx rates to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(IPW_POWER_MODE_CAM);
	DPRINTF(("Setting power mode to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_POWER_MODE, &data, sizeof data);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		data = htole32(32); /* default value */
		DPRINTF(("Setting tx power index to %u\n", le32toh(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_TX_POWER_INDEX, &data,
		    sizeof data);
		if (error != 0)
			return error;
	}

	data = htole32(ic->ic_rtsthreshold);
	DPRINTF(("Setting RTS threshold to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_RTS_THRESHOLD, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(ic->ic_fragthreshold);
	DPRINTF(("Setting frag threshold to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_FRAG_THRESHOLD, &data, sizeof data);
	if (error != 0)
		return error;

#ifdef IPW_DEBUG
	if (ipw_debug > 0) {
		printf("Setting ESSID to ");
		ieee80211_print_essid(ic->ic_des_essid, ic->ic_des_esslen);
		printf("\n");
	}
#endif
	error = ipw_cmd(sc, IPW_CMD_SET_ESSID, ic->ic_des_essid,
	    ic->ic_des_esslen);
	if (error != 0)
		return error;

	/* no mandatory BSSID */
	DPRINTF(("Setting mandatory BSSID to null\n"));
	error = ipw_cmd(sc, IPW_CMD_SET_MANDATORY_BSSID, NULL, 0);
	if (error != 0)
		return error;

	if (ic->ic_flags & IEEE80211_F_DESBSSID) {
		DPRINTF(("Setting desired BSSID to %6D\n", ic->ic_des_bssid,
		    ":"));
		error = ipw_cmd(sc, IPW_CMD_SET_DESIRED_BSSID,
		    ic->ic_des_bssid, IEEE80211_ADDR_LEN);
		if (error != 0)
			return error;
	}

	bzero(&security, sizeof security);
	security.authmode = (sc->authmode == IEEE80211_AUTH_SHARED) ?
	    IPW_AUTH_SHARED : IPW_AUTH_OPEN;
	security.ciphers = htole32(IPW_CIPHER_NONE);
	DPRINTF(("Setting authmode to %u\n", security.authmode));
	error = ipw_cmd(sc, IPW_CMD_SET_SECURITY_INFORMATION, &security,
	    sizeof security);
	if (error != 0)
		return error;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		k = ic->ic_nw_keys;
		for (i = 0; i < IEEE80211_WEP_NKID; i++, k++) {
			if (k->wk_len == 0)
				continue;

			wepkey.idx = i;
			wepkey.len = k->wk_len;
			bzero(wepkey.key, sizeof wepkey.key);
			bcopy(k->wk_key, wepkey.key, k->wk_len);
			DPRINTF(("Setting wep key index %u len %u\n",
			    wepkey.idx, wepkey.len));
			error = ipw_cmd(sc, IPW_CMD_SET_WEP_KEY, &wepkey,
			    sizeof wepkey);
			if (error != 0)
				return error;
		}

		data = htole32(ic->ic_wep_txkey);
		DPRINTF(("Setting wep tx key index to %u\n", le32toh(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_WEP_KEY_INDEX, &data,
		    sizeof data);
		if (error != 0)
			return error;
	}

	data = htole32((ic->ic_flags & IEEE80211_F_WEPON) ? IPW_WEPON : 0);
	DPRINTF(("Setting wep flags to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_WEP_FLAGS, &data, sizeof data);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS ||
	    ic->ic_opmode == IEEE80211_M_HOSTAP) {
		data = htole32(ic->ic_lintval);
		DPRINTF(("Setting beacon interval to %u\n", le32toh(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_BEACON_INTERVAL, &data,
		    sizeof data);
		if (error != 0)
			return error;
	}

	options.flags = htole32(0);
	options.channels = htole32(0x3fff); /* scan channels 1-14 */
	DPRINTF(("Setting scan options to 0x%x\n", le32toh(options.flags)));
	error = ipw_cmd(sc, IPW_CMD_SET_SCAN_OPTIONS, &options, sizeof options);
	if (error != 0)
		return error;

	/* finally, enable adapter (start scanning for an access point) */
	DPRINTF(("Enabling adapter\n"));
	return ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
}

static void
ipw_init(void *priv)
{
	struct ipw_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ipw_firmware *fw = &sc->fw;

	/* exit immediately if firmware has not been ioctl'd */
	if (!(sc->flags & IPW_FLAG_FW_CACHED)) {
		ifp->if_flags &= ~IFF_UP;
		return;
	}

	ipw_stop(sc);

	if (ipw_reset(sc) != 0) {
		device_printf(sc->sc_dev, "could not reset adapter\n");
		goto fail;
	}

	if (ipw_load_ucode(sc, fw->ucode, fw->ucode_size) != 0) {
		device_printf(sc->sc_dev, "could not load microcode\n");
		goto fail;
	}

	ipw_stop_master(sc);

	/*
	 * Setup tx, rx and status rings
	 */
	CSR_WRITE_4(sc, IPW_CSR_TX_BD_BASE, sc->tbd_phys);
	CSR_WRITE_4(sc, IPW_CSR_TX_BD_SIZE, IPW_NTBD);
	CSR_WRITE_4(sc, IPW_CSR_TX_READ_INDEX, 0);
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, 0);
	sc->txold = IPW_NTBD - 1; /* latest bd index ack'ed by firmware */
	sc->txcur = 0; /* bd index to write to */
	sc->txfree = IPW_NTBD - 2;

	CSR_WRITE_4(sc, IPW_CSR_RX_BD_BASE, sc->rbd_phys);
	CSR_WRITE_4(sc, IPW_CSR_RX_BD_SIZE, IPW_NRBD);
	CSR_WRITE_4(sc, IPW_CSR_RX_READ_INDEX, 0);
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE_INDEX, IPW_NRBD - 1);
	sc->rxcur = IPW_NRBD - 1; /* latest bd index I've read */

	CSR_WRITE_4(sc, IPW_CSR_RX_STATUS_BASE, sc->status_phys);

	if (ipw_load_firmware(sc, fw->main, fw->main_size) != 0) {
		device_printf(sc->sc_dev, "could not load firmware\n");
		goto fail;
	}

	sc->flags |= IPW_FLAG_FW_INITED;

	/* Retrieve information tables base addresses */
	sc->table1_base = CSR_READ_4(sc, IPW_CSR_TABLE1_BASE);
	sc->table2_base = CSR_READ_4(sc, IPW_CSR_TABLE2_BASE);

	ipw_write_table1(sc, IPW_INFO_LOCK, 0);

	if (ipw_config(sc) != 0) {
		device_printf(sc->sc_dev, "device configuration failed\n");
		goto fail;
	}

	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_flags |= IFF_RUNNING;

	return;

fail:	ifp->if_flags &= ~IFF_UP;
	ipw_stop(sc);
}

static void
ipw_stop(void *priv)
{
	struct ipw_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int i;

	ipw_stop_master(sc);
	CSR_WRITE_4(sc, IPW_CSR_RST, IPW_RST_SW_RESET);

	/*
	 * Release tx buffers
	 */
	for (i = 0; i < IPW_NTBD; i++)
		ipw_release_sbd(sc, &sc->stbd_list[i]);

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

static int
ipw_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct ipw_softc *sc = arg1;
	u_int32_t i, size, buf[256];

	if (!(sc->flags & IPW_FLAG_FW_INITED)) {
		bzero(buf, sizeof buf);
		return SYSCTL_OUT(req, buf, sizeof buf);
	}

	CSR_WRITE_4(sc, IPW_CSR_AUTOINC_ADDR, sc->table1_base);

	size = min(CSR_READ_4(sc, IPW_CSR_AUTOINC_DATA), 256);
	for (i = 1; i < size; i++)
		buf[i] = MEM_READ_4(sc, CSR_READ_4(sc, IPW_CSR_AUTOINC_DATA));

	return SYSCTL_OUT(req, buf, sizeof buf);
}

static int
ipw_sysctl_radio(SYSCTL_HANDLER_ARGS)
{
	struct ipw_softc *sc = arg1;
	int val;

	val = !((sc->flags & IPW_FLAG_HAS_RADIO_SWITCH) &&
		(CSR_READ_4(sc, IPW_CSR_IO) & IPW_IO_RADIO_DISABLED));

	return SYSCTL_OUT(req, &val, sizeof val);
}

static u_int32_t
ipw_read_table1(struct ipw_softc *sc, u_int32_t off)
{
	return MEM_READ_4(sc, MEM_READ_4(sc, sc->table1_base + off));
}

static void
ipw_write_table1(struct ipw_softc *sc, u_int32_t off, u_int32_t info)
{
	MEM_WRITE_4(sc, MEM_READ_4(sc, sc->table1_base + off), info);
}

static int
ipw_read_table2(struct ipw_softc *sc, u_int32_t off, void *buf, u_int32_t *len)
{
	u_int32_t addr, info;
	u_int16_t count, size;
	u_int32_t total;

	/* addr[4] + count[2] + size[2] */
	addr = MEM_READ_4(sc, sc->table2_base + off);
	info = MEM_READ_4(sc, sc->table2_base + off + 4);

	count = info >> 16;
	size = info & 0xffff;
	total = count * size;

	if (total > *len) {
		*len = total;
		return EINVAL;
	}

	*len = total;
	ipw_read_mem_1(sc, addr, buf, total);

	return 0;
}

static void
ipw_read_mem_1(struct ipw_softc *sc, bus_size_t offset, u_int8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		*datap = CSR_READ_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3));
	}
}

static void
ipw_write_mem_1(struct ipw_softc *sc, bus_size_t offset, u_int8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		CSR_WRITE_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3), *datap);
	}
}
