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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $Id: if_nv.c,v 1.9 2003/12/13 15:27:40 q Exp $
 * $DragonFly: src/sys/dev/netif/nv/Attic/if_nv.c,v 1.7 2004/11/05 17:13:44 dillon Exp $
 */

/*
 * NVIDIA nForce MCP Networking Adapter driver
 * 
 * This is a port of the NVIDIA MCP Linux ethernet driver distributed by NVIDIA
 * through their web site.
 * 
 * All mainstream nForce and nForce2 motherboards are supported. This module
 * is as stable, sometimes more stable, than the linux version. (This seems to 
 * be related to some issues with newer distributions using GCC 3.2, however 
 * this don't appear to effect FreeBSD 5.x).
 * 
 * In accordance with the NVIDIA distribution license it is necessary to link
 * this module against the nvlibnet.o binary object included in the Linux
 * driver source distribution. The binary component is not modified in any
 * way and is simply linked against a FreeBSD equivalent of the nvnet.c linux
 * kernel module "wrapper".
 * 
 * The Linux driver uses a common code API that is shared between Win32 and
 * Linux. This abstracts the low level driver functions and uses callbacks
 * and hooks to access the underlying hardware device. By using this same API
 * in a FreeBSD kernel module it is possible to support the hardware without
 * breaching the Linux source distributions licensing requirements, or
 * obtaining the hardware programming specifications.
 * 
 * Although not conventional, it works, and given the relatively small amount of
 * hardware centric code, it's hopefully no more buggy than its linux
 * counterpart.
 *
 * Written by Quinton Dolan <q@onthenet.com.au> 
 * Portions based on existing FreeBSD network drivers. 
 * NVIDIA API usage derived from distributed NVIDIA NVNET driver source files.
 * 
 * $Id: if_nv.c,v 1.9 2003/12/13 15:27:40 q Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/module.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <net/vlan/if_vlan_var.h>

#include <machine/bus_memio.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <vm/vm.h>		/* for vtophys */
#include <vm/pmap.h>		/* for vtophys */
#include <machine/clock.h>	/* for DELAY */
#include <sys/bus.h>
#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

MODULE_DEPEND(nv, pci, 1, 1, 1);
MODULE_DEPEND(nv, miibus, 1, 1, 1);

#include "if_nvreg.h"
#include "miibus_if.h"

static int      nv_probe(device_t);
static int      nv_attach(device_t);
static int      nv_detach(device_t);
static void     nv_init(void *);
static void     nv_stop(struct nv_softc *);
static void     nv_shutdown(device_t);
static int      nv_init_rings(struct nv_softc *);
static void     nv_free_rings(struct nv_softc *);

static void     nv_ifstart(struct ifnet *);
static int      nv_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void     nv_intr(void *);
static void     nv_tick(void *);
static void     nv_setmulti(struct nv_softc *);
static void     nv_watchdog(struct ifnet *);
static void     nv_update_stats(struct nv_softc *);

static int      nv_ifmedia_upd(struct ifnet *);
static void     nv_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int      nv_miibus_readreg(device_t, int, int);
static void     nv_miibus_writereg(device_t, int, int, int);

static void     nv_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static void     nv_dmamap_tx_cb(void *, bus_dma_segment_t *, int, bus_size_t, int);

static int      nv_osalloc(void *, MEMORY_BLOCK *);
static int      nv_osfree(void *, MEMORY_BLOCK *);
static int      nv_osallocex(void *, MEMORY_BLOCKEX *);
static int      nv_osfreeex(void *, MEMORY_BLOCKEX *);
static int      nv_osclear(void *, void *, int);
static int      nv_osdelay(void *, unsigned long);
static int      nv_osallocrxbuf(void *, MEMORY_BLOCK *, void **);
static int      nv_osfreerxbuf(void *, MEMORY_BLOCK *, void *);
static int      nv_ospackettx(void *, void *, unsigned long);
static int      nv_ospacketrx(void *, void *, unsigned long, unsigned char *, unsigned char);
static int      nv_oslinkchg(void *, int);
static int      nv_osalloctimer(void *, void **);
static int      nv_osfreetimer(void *, void *);
static int      nv_osinittimer(void *, void *, PTIMER_FUNC, void *);
static int      nv_ossettimer(void *, void *, unsigned long);
static int      nv_oscanceltimer(void *, void *);
static int      nv_ospreprocpkt(void *, void *, void **, unsigned char *, unsigned char);
static void    *nv_ospreprocpktnopq(void *, void *);
static int      nv_osindicatepkt(void *, void **, unsigned long);
static int      nv_oslockalloc(void *, int, void **);
static int      nv_oslockacquire(void *, int, void *);
static int      nv_oslockrelease(void *, int, void *);
static void    *nv_osreturnbufvirt(void *, void *);

static device_method_t nv_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, nv_probe),
	DEVMETHOD(device_attach, nv_attach),
	DEVMETHOD(device_detach, nv_detach),
	DEVMETHOD(device_shutdown, nv_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg, nv_miibus_readreg),
	DEVMETHOD(miibus_writereg, nv_miibus_writereg),

	{0, 0}
};

static driver_t nv_driver = {
	"nv",
	nv_methods,
	sizeof(struct nv_softc)
};

static devclass_t nv_devclass;

static int      nv_pollinterval = 0;
SYSCTL_INT(_hw, OID_AUTO, nv_pollinterval, CTLFLAG_RW,
	   &nv_pollinterval, 0, "delay between interface polls");

DRIVER_MODULE(nv, pci, nv_driver, nv_devclass, 0, 0);
DRIVER_MODULE(miibus, nv, miibus_driver, miibus_devclass, 0, 0);

static struct nv_type nv_devs[] = {
        {NVIDIA_VENDORID, NFORCE_MCPNET1_DEVICEID,
                "NVIDIA nForce MCP Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET2_DEVICEID,
                "NVIDIA nForce MCP2 Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET3_DEVICEID,
                "NVIDIA nForce MCP3 Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET4_DEVICEID,
                "NVIDIA nForce MCP4 Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET5_DEVICEID,
                "NVIDIA nForce MCP5 Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET6_DEVICEID,
                "NVIDIA nForce MCP6 Networking Adapter"},
        {NVIDIA_VENDORID, NFORCE_MCPNET7_DEVICEID,
                "NVIDIA nForce MCP7 Networking Adapter"},
        {0, 0, NULL}
};

/* DMA MEM map callback function to get data segment physical address */
static void
nv_dmamap_cb(void *arg, bus_dma_segment_t * segs, int nsegs, int error)
{
	if (error)
		return;

	KASSERT(nsegs == 1,
		("Too many DMA segments returned when mapping DMA memory"));
	*(bus_addr_t *)arg = segs->ds_addr;
}

/* DMA RX map callback function to get data segment physical address */
static void
nv_dmamap_rx_cb(void *arg, bus_dma_segment_t * segs, int nsegs, bus_size_t mapsize, int error)
{
	if (error)
		return;
	*(bus_addr_t *)arg = segs->ds_addr;
}

/*
 * DMA TX buffer callback function to allocate fragment data segment
 * addresses
 */
static void
nv_dmamap_tx_cb(void *arg, bus_dma_segment_t * segs, int nsegs, bus_size_t mapsize, int error)
{
	struct nv_tx_desc *info = arg;

	if (error)
		return;
	KASSERT(nsegs < NV_MAX_FRAGS,
		("Too many DMA segments returned when mapping mbuf"));
	info->numfrags = nsegs;
	bcopy(segs, info->frags, nsegs * sizeof(bus_dma_segment_t));
}

/* Probe for supported hardware ID's */
static int
nv_probe(device_t dev)
{
	struct nv_type *t = nv_devs;

	/* Check for matching PCI DEVICE ID's */
	while (t->name != NULL) {
		if ((pci_get_vendor(dev) == t->vid_id) &&
		    (pci_get_device(dev) == t->dev_id)) {
			device_set_desc(dev, t->name);
			return (0);
		}
		t++;
	}

	return (ENXIO);
}

/* Attach driver and initialise hardware for use */
static int
nv_attach(device_t dev)
{
	u_char          eaddr[ETHER_ADDR_LEN];
	struct nv_softc *sc;
	struct ifnet   *ifp;
	OS_API         *osapi;
	ADAPTER_OPEN_PARAMS OpenParams;
	int             error = 0, i, rid;

	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_attach - entry\n");

	sc = device_get_softc(dev);

	sc->dev = dev;
	callout_init(&sc->nv_stat_timer);

	/* Preinitialize data structures */
	bzero(&OpenParams, sizeof(ADAPTER_OPEN_PARAMS));

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/* Allocate memory mapped address space */
	rid = NV_RID;
	sc->res = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid,
				     0, ~0, 1, RF_ACTIVE);

	if (sc->res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}
	sc->sc_st = rman_get_bustag(sc->res);
	sc->sc_sh = rman_get_bushandle(sc->res);

	/* Allocate interrupt */
	rid = 0;
	sc->irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
				     RF_SHAREABLE | RF_ACTIVE);

	if (sc->irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}
	/* Allocate DMA tags */
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
		     BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES * NV_MAX_FRAGS,
				   NV_MAX_FRAGS, MCLBYTES, 0,
				   &sc->mtag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR, NULL, NULL,
				sizeof(struct nv_rx_desc) * RX_RING_SIZE, 1,
				sizeof(struct nv_rx_desc) * RX_RING_SIZE, 0,
				   &sc->rtag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}
	error = bus_dma_tag_create(NULL, 4, 0, BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR, NULL, NULL,
				sizeof(struct nv_tx_desc) * TX_RING_SIZE, 1,
				sizeof(struct nv_tx_desc) * TX_RING_SIZE, 0,
				   &sc->ttag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}

	error = bus_dmamap_create(sc->ttag, 0, &sc->tmap);
	if (error) {
		device_printf(dev, "couldn't create dma map\n");
		goto fail;
	}

	/* Allocate DMA safe memory and get the DMA addresses. */
	error = bus_dmamem_alloc(sc->ttag, (void **)&sc->tx_desc,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->tmap);
	if (error) {
		device_printf(dev, "couldn't allocate dma memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->ttag, sc->tmap, sc->tx_desc,
		     sizeof(struct nv_tx_desc) * TX_RING_SIZE, nv_dmamap_cb,
				&sc->tx_addr, 0);
	if (error) {
		device_printf(dev, "couldn't map dma memory\n");
		goto fail;
	}

	error = bus_dmamap_create(sc->rtag, 0, &sc->rmap);
	if (error) {
		device_printf(dev, "couldn't create dma map\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->rtag, (void **)&sc->rx_desc,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->rmap);
	if (error) {
		device_printf(dev, "couldn't allocate dma memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->rtag, sc->rmap, sc->rx_desc,
		     sizeof(struct nv_rx_desc) * RX_RING_SIZE, nv_dmamap_cb,
				&sc->rx_addr, 0);
	if (error) {
		device_printf(dev, "couldn't map dma memory\n");
		goto fail;
	}
	/* Initialize rings. */
	if (nv_init_rings(sc)) {
		device_printf(dev, "failed to init rings\n");
		error = ENXIO;
		goto fail;
	}
	/* Setup NVIDIA API callback routines */
	osapi = &sc->osapi;
	osapi->pOSCX = sc;
	osapi->pfnAllocMemory = nv_osalloc;
	osapi->pfnFreeMemory = nv_osfree;
	osapi->pfnAllocMemoryEx = nv_osallocex;
	osapi->pfnFreeMemoryEx = nv_osfreeex;
	osapi->pfnClearMemory = nv_osclear;
	osapi->pfnStallExecution = nv_osdelay;
	osapi->pfnAllocReceiveBuffer = nv_osallocrxbuf;
	osapi->pfnFreeReceiveBuffer = nv_osfreerxbuf;
	osapi->pfnPacketWasSent = nv_ospackettx;
	osapi->pfnPacketWasReceived = nv_ospacketrx;
	osapi->pfnLinkStateHasChanged = nv_oslinkchg;
	osapi->pfnAllocTimer = nv_osalloctimer;
	osapi->pfnFreeTimer = nv_osfreetimer;
	osapi->pfnInitializeTimer = nv_osinittimer;
	osapi->pfnSetTimer = nv_ossettimer;
	osapi->pfnCancelTimer = nv_oscanceltimer;
	osapi->pfnPreprocessPacket = nv_ospreprocpkt;
	osapi->pfnPreprocessPacketNopq = nv_ospreprocpktnopq;
	osapi->pfnIndicatePackets = nv_osindicatepkt;
	osapi->pfnLockAlloc = nv_oslockalloc;
	osapi->pfnLockAcquire = nv_oslockacquire;
	osapi->pfnLockRelease = nv_oslockrelease;
	osapi->pfnReturnBufferVirtual = nv_osreturnbufvirt;

	/* Set NVIDIA API startup parameters */
	OpenParams.MaxDpcLoop = 2;
	OpenParams.MaxRxPkt = RX_RING_SIZE;
	OpenParams.MaxTxPkt = TX_RING_SIZE;
	OpenParams.SentPacketStatusSuccess = 1;
	OpenParams.SentPacketStatusFailure = 0;
	OpenParams.MaxRxPktToAccumulate = 6;
	OpenParams.ulPollInterval = nv_pollinterval;
	OpenParams.SetForcedModeEveryNthRxPacket = 0;
	OpenParams.SetForcedModeEveryNthTxPacket = 0;
	OpenParams.RxForcedInterrupt = 0;
	OpenParams.TxForcedInterrupt = 0;
	OpenParams.pOSApi = osapi;
	OpenParams.pvHardwareBaseAddress = rman_get_virtual(sc->res);
	sc->linkup = 0;

	/* Open NVIDIA Hardware API */
	error = ADAPTER_Open(&OpenParams, (void **)&(sc->hwapi), &sc->phyaddr);
	if (error) {
		device_printf(dev, "failed to open NVIDIA Hardware API: 0x%x\n", error);
		goto fail;
	}
	/* MAC is loaded backwards into h/w reg */
	sc->hwapi->pfnGetNodeAddress(sc->hwapi->pADCX, sc->original_mac_addr);
	for (i = 0; i < 6; i++) {
		eaddr[i] = sc->original_mac_addr[5 - i];
	}
	sc->hwapi->pfnSetNodeAddress(sc->hwapi->pADCX, eaddr);
	bcopy(eaddr, (char *)&sc->sc_macaddr, ETHER_ADDR_LEN);

	/* Display ethernet address ,... */
	device_printf(dev, "Ethernet address %6D\n", sc->sc_macaddr, ":");

	DEBUGOUT(NV_DEBUG_INIT, "nv: do mii_phy_probe\n");

	/* Probe device for MII interface to PHY */
	if (mii_phy_probe(dev, &sc->miibus,
			  nv_ifmedia_upd, nv_ifmedia_sts)) {
		device_printf(dev, "MII without any phy!\n");
		error = ENXIO;
		goto fail;
	}
	/* Setup interface parameters */
	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nv_ioctl;
	ifp->if_start = nv_ifstart;
	ifp->if_watchdog = nv_watchdog;
	ifp->if_timer = 0;
	ifp->if_init = nv_init;
	ifp->if_mtu = ETHERMTU;
	ifp->if_baudrate = IF_Mbps(100);
	ifp->if_snd.ifq_maxlen = IFQ_MAXLEN;

	/* Attach to OS's managers. */
	ether_ifattach(ifp, sc->sc_macaddr);

	/* Activate our interrupt handler. - attach last to avoid lock */
	error = bus_setup_intr(sc->dev, sc->irq, INTR_TYPE_NET,
			       nv_intr, sc, &sc->sc_ih);
	if (error) {
		device_printf(sc->dev, "couldn't set up interrupt handler\n");
		goto fail;
	}
	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_attach - exit\n");

fail:
	if (error)
		nv_detach(dev);

	return (error);
}

/* Detach interface for module unload */
static int
nv_detach(device_t dev)
{
	struct nv_softc *sc = device_get_softc(dev);
	struct ifnet   *ifp;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: nv_detach - entry\n");

	ifp = &sc->arpcom.ac_if;

	if (device_is_attached(dev)) {
		nv_stop(sc);
		ether_ifdetach(ifp);
	}

	if (sc->miibus)
		device_delete_child(dev, sc->miibus);
	bus_generic_detach(dev);

	/* Reload unreversed address back into MAC in original state */
	if (sc->original_mac_addr)
		sc->hwapi->pfnSetNodeAddress(sc->hwapi->pADCX, sc->original_mac_addr);

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: do pfnClose\n");
	/* Detach from NVIDIA hardware API */
	if (sc->hwapi->pfnClose)
		sc->hwapi->pfnClose(sc->hwapi->pADCX);
	/* Release resources */
	if (sc->sc_ih)
		bus_teardown_intr(sc->dev, sc->irq, sc->sc_ih);
	if (sc->irq)
		bus_release_resource(sc->dev, SYS_RES_IRQ, 0, sc->irq);
	if (sc->res)
		bus_release_resource(sc->dev, SYS_RES_MEMORY, NV_RID, sc->res);

	nv_free_rings(sc);

	if (sc->tx_desc) {
		bus_dmamap_unload(sc->rtag, sc->rmap);
		bus_dmamem_free(sc->rtag, sc->rx_desc, sc->rmap);
		bus_dmamap_destroy(sc->rtag, sc->rmap);
	}
	if (sc->mtag)
		bus_dma_tag_destroy(sc->mtag);
	if (sc->ttag)
		bus_dma_tag_destroy(sc->ttag);
	if (sc->rtag)
		bus_dma_tag_destroy(sc->rtag);

	NV_UNLOCK(sc);

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: nv_detach - exit\n");

	return (0);
}

/* Initialise interface and start it "RUNNING" */
static void
nv_init(void *xsc)
{
	struct nv_softc *sc = xsc;
	struct ifnet   *ifp;
	int             error;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_init - entry (%d)\n", sc->linkup);

	ifp = &sc->sc_if;

	/* Do nothing if already running */
	if (ifp->if_flags & IFF_RUNNING)
		goto fail;

	nv_stop(sc);

	DEBUGOUT(NV_DEBUG_INIT, "nv: do pfnInit\n");
	/* Setup Hardware interface and allocate memory structures */
	error = sc->hwapi->pfnInit(sc->hwapi->pADCX, 0, 0, 0, &sc->linkup);
	if (error) {
		device_printf(sc->dev, "failed to start NVIDIA Hardware interface\n");
		goto fail;
	}
	/* Set the MAC address */
	sc->hwapi->pfnSetNodeAddress(sc->hwapi->pADCX, sc->sc_macaddr);
	sc->hwapi->pfnEnableInterrupts(sc->hwapi->pADCX);
	sc->hwapi->pfnStart(sc->hwapi->pADCX);

	/* Setup multicast filter */
	nv_setmulti(sc);
	nv_ifmedia_upd(ifp);

	/* Update interface parameters */
	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset(&sc->nv_stat_timer, hz, nv_tick, sc);

	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_init - exit\n");

fail:
	NV_UNLOCK(sc);

	return;
}

/* Stop interface activity ie. not "RUNNING" */
static void
nv_stop(struct nv_softc *sc)
{
	struct ifnet   *ifp;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_stop - entry\n");

	ifp = &sc->sc_if;
	ifp->if_timer = 0;

	/* Cancel tick timer */
	callout_stop(&sc->nv_stat_timer);

	/* Stop hardware activity */
	sc->hwapi->pfnDisableInterrupts(sc->hwapi->pADCX);
	sc->hwapi->pfnStop(sc->hwapi->pADCX, 0);

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: do pfnDeinit\n");
	/* Shutdown interface and deallocate memory buffers */
	if (sc->hwapi->pfnDeinit)
		sc->hwapi->pfnDeinit(sc->hwapi->pADCX, 0);

	sc->linkup = 0;
	sc->cur_rx = 0;

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_stop - exit\n");

	NV_UNLOCK(sc);

	return;
}

/* Shutdown interface for unload/reboot */
static void
nv_shutdown(device_t dev)
{
	struct nv_softc *sc;

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: nv_shutdown\n");

	sc = device_get_softc(dev);

	/* Stop hardware activity */
	nv_stop(sc);
}

/* Allocate TX ring buffers */
static int
nv_init_rings(struct nv_softc *sc)
{
	int             error, i;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_init_rings - entry\n");

	sc->cur_rx = sc->cur_tx = sc->pending_rxs = sc->pending_txs = 0;
	/* Initialise RX ring */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct nv_rx_desc *desc = sc->rx_desc + i;
		struct nv_map_buffer *buf = &desc->buf;

		buf->mbuf = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (buf->mbuf == NULL) {
			device_printf(sc->dev, "couldn't allocate mbuf\n");
			nv_free_rings(sc);
			error = ENOBUFS;
			goto fail;
		}
		buf->mbuf->m_len = buf->mbuf->m_pkthdr.len = MCLBYTES;
		m_adj(buf->mbuf, ETHER_ALIGN);

		error = bus_dmamap_create(sc->mtag, 0, &buf->map);
		if (error) {
			device_printf(sc->dev, "couldn't create dma map\n");
			nv_free_rings(sc);
			goto fail;
		}
		error = bus_dmamap_load_mbuf(sc->mtag, buf->map, buf->mbuf,
					  nv_dmamap_rx_cb, &desc->paddr, 0);
		if (error) {
			device_printf(sc->dev, "couldn't dma map mbuf\n");
			nv_free_rings(sc);
			goto fail;
		}
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREREAD);

		desc->buflength = buf->mbuf->m_len;
		desc->vaddr = mtod(buf->mbuf, PVOID);
	}
	bus_dmamap_sync(sc->rtag, sc->rmap,
			BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Initialize TX ring */
	for (i = 0; i < TX_RING_SIZE; i++) {
		struct nv_tx_desc *desc = sc->tx_desc + i;
		struct nv_map_buffer *buf = &desc->buf;

		buf->mbuf = NULL;

		error = bus_dmamap_create(sc->mtag, 0, &buf->map);
		if (error) {
			device_printf(sc->dev, "couldn't create dma map\n");
			nv_free_rings(sc);
			goto fail;
		}
	}
	bus_dmamap_sync(sc->ttag, sc->tmap,
			BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	DEBUGOUT(NV_DEBUG_INIT, "nv: nv_init_rings - exit\n");

fail:
	NV_UNLOCK(sc);

	return (error);
}

/* Free the TX ring buffers */
static void
nv_free_rings(struct nv_softc *sc)
{
	int             i;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: nv_free_rings - entry\n");

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct nv_rx_desc *desc = sc->rx_desc + i;
		struct nv_map_buffer *buf = &desc->buf;

		if (buf->mbuf) {
			bus_dmamap_unload(sc->mtag, buf->map);
			bus_dmamap_destroy(sc->mtag, buf->map);
			m_freem(buf->mbuf);
		}
		buf->mbuf = NULL;
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct nv_tx_desc *desc = sc->tx_desc + i;
		struct nv_map_buffer *buf = &desc->buf;

		if (buf->mbuf) {
			bus_dmamap_unload(sc->mtag, buf->map);
			bus_dmamap_destroy(sc->mtag, buf->map);
			m_freem(buf->mbuf);
		}
		buf->mbuf = NULL;
	}

	DEBUGOUT(NV_DEBUG_DEINIT, "nv: nv_free_rings - exit\n");

	NV_UNLOCK(sc);
}

/* Main loop for sending packets from OS to interface */
static void
nv_ifstart(struct ifnet *ifp)
{
	struct nv_softc *sc = ifp->if_softc;
	struct nv_map_buffer *buf;
	struct mbuf    *m0, *m;
	struct nv_tx_desc *desc;
	ADAPTER_WRITE_DATA txdata;
	int             error, i;

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_ifstart - entry\n");

	/* If link is down/busy or queue is empty do nothing */
	if (ifp->if_flags & IFF_OACTIVE || ifp->if_snd.ifq_head == NULL)
		return;

	/* Transmit queued packets until sent or TX ring is full */
	while (sc->pending_txs < TX_RING_SIZE) {
		desc = sc->tx_desc + sc->cur_tx;
		buf = &desc->buf;

		/* Get next packet to send. */
		IF_DEQUEUE(&ifp->if_snd, m0);

		/* If nothing to send, return. */
		if (m0 == NULL)
			return;

		/* Map MBUF for DMA access */
		error = bus_dmamap_load_mbuf(sc->mtag, buf->map, m0,
				     nv_dmamap_tx_cb, desc, BUS_DMA_NOWAIT);

		if (error && error != EFBIG) {
			m_freem(m0);
			sc->tx_errors++;
			continue;
		}
		/*
		 * Packet has too many fragments - defrag into new mbuf
		 * cluster
		 */
		if (error) {
			m = m_defrag(m0, MB_DONTWAIT);
			if (m == NULL) {
				m_freem(m0);
				sc->tx_errors++;
				continue;
			}
			m_freem(m0);
			m0 = m;

			error = bus_dmamap_load_mbuf(sc->mtag, buf->map, m,
				     nv_dmamap_tx_cb, desc, BUS_DMA_NOWAIT);
			if (error) {
				m_freem(m);
				sc->tx_errors++;
				continue;
			}
		}
		/* Do sync on DMA bounce buffer */
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREWRITE);

		buf->mbuf = m0;
		txdata.ulNumberOfElements = desc->numfrags;
		txdata.pvID = (PVOID)desc;

		/* Put fragments into API element list */
		txdata.ulTotalLength = buf->mbuf->m_len;
		for (i = 0; i < desc->numfrags; i++) {
			txdata.sElement[i].ulLength = (ulong)desc->frags[i].ds_len;
			txdata.sElement[i].pPhysical = (PVOID)desc->frags[i].ds_addr;
		}

		/* Send packet to Nvidia API for transmission */
		error = sc->hwapi->pfnWrite(sc->hwapi->pADCX, &txdata);

		switch (error) {
		case ADAPTERERR_NONE:
			/* Packet was queued in API TX queue successfully */
			sc->pending_txs++;
			sc->cur_tx = (sc->cur_tx + 1) % TX_RING_SIZE;
			break;

		case ADAPTERERR_TRANSMIT_QUEUE_FULL:
			/* The API TX queue is full - requeue the packet */
			device_printf(sc->dev, "nv_ifstart: transmit queue is full\n");
			ifp->if_flags |= IFF_OACTIVE;
			bus_dmamap_unload(sc->mtag, buf->map);
			IF_PREPEND(&ifp->if_snd, buf->mbuf);
			buf->mbuf = NULL;
			return;

		default:
			/* The API failed to queue/send the packet so dump it */
			device_printf(sc->dev, "nv_ifstart: transmit error\n");
			bus_dmamap_unload(sc->mtag, buf->map);
			m_freem(buf->mbuf);
			buf->mbuf = NULL;
			sc->tx_errors++;
			return;
		}
		/* Set watchdog timer. */
		ifp->if_timer = 8;

		/* Copy packet to BPF tap */
		BPF_MTAP(ifp, m0);
	}
	ifp->if_flags |= IFF_OACTIVE;

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_ifstart - exit\n");
}

/* Handle IOCTL events */
static int
nv_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct nv_softc *sc = ifp->if_softc;
	struct ifreq   *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int             error = 0;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_IOCTL, "nv: nv_ioctl - entry\n");

	switch (command) {
	case SIOCSIFMTU:
		/* Set MTU size */
		if (ifp->if_mtu == ifr->ifr_mtu)
			break;
		if (ifr->ifr_mtu + ifp->if_hdrlen <= MAX_PACKET_SIZE) {
			ifp->if_mtu = ifr->ifr_mtu;
			nv_stop(sc);
			nv_init(sc);
		} else
			error = EINVAL;
		break;

	case SIOCSIFFLAGS:
		/* Setup interface flags */
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0) {
				nv_init(sc);
				break;
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				nv_stop(sc);
				break;
			}
		}

		/* Handle IFF_PROMISC and IFF_ALLMULTI flags. */
		nv_setmulti(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* Setup multicast filter */
		if (ifp->if_flags & IFF_RUNNING) {
			nv_setmulti(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		/* Get/Set interface media parameters */
		mii = device_get_softc(sc->miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	default:
		/* Everything else we forward to generic ether ioctl */
		error = ether_ioctl(ifp, (int)command, data);
		break;
	}

	DEBUGOUT(NV_DEBUG_IOCTL, "nv: nv_ioctl - exit\n");

	NV_UNLOCK(sc);

	return (error);
}

/* Interrupt service routine */
static void
nv_intr(void *arg)
{
	struct nv_softc *sc = arg;
	struct ifnet   *ifp = &sc->sc_if;

	DEBUGOUT(NV_DEBUG_INTERRUPT, "nv: nv_intr - entry\n");

	if (!ifp->if_flags & IFF_UP) {
		nv_stop(sc);
		return;
	}
	/* Handle interrupt event */
	if (sc->hwapi->pfnQueryInterrupt(sc->hwapi->pADCX)) {
		sc->hwapi->pfnHandleInterrupt(sc->hwapi->pADCX);
		sc->hwapi->pfnEnableInterrupts(sc->hwapi->pADCX);
	}
	if (ifp->if_snd.ifq_head != NULL)
		nv_ifstart(ifp);

	/* If no pending packets we don't need a timeout */
	if (sc->pending_txs == 0)
		sc->sc_if.if_timer = 0;

	DEBUGOUT(NV_DEBUG_INTERRUPT, "nv: nv_intr - exit\n");

	return;
}

/* Setup multicast filters */
static void
nv_setmulti(struct nv_softc *sc)
{
	struct ifnet   *ifp;
	struct ifmultiaddr *ifma;
	PACKET_FILTER   hwfilter;
	int             i;
	u_int8_t        oraddr[6];
	u_int8_t        andaddr[6];

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_setmulti - entry\n");

	ifp = &sc->sc_if;

	/* Initialize filter */
	hwfilter.ulFilterFlags = 0;
	for (i = 0; i < 6; i++) {
		hwfilter.acMulticastAddress[i] = 0;
		hwfilter.acMulticastMask[i] = 0;
	}

	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		/* Accept all packets */
		hwfilter.ulFilterFlags |= ACCEPT_ALL_PACKETS;
		sc->hwapi->pfnSetPacketFilter(sc->hwapi->pADCX, &hwfilter);
		NV_UNLOCK(sc);
		return;
	}
	/* Setup multicast filter */
	LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		u_char         *addrp;

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		addrp = LLADDR((struct sockaddr_dl *) ifma->ifma_addr);
		for (i = 0; i < 6; i++) {
			u_int8_t        mcaddr = addrp[i];
			andaddr[i] &= mcaddr;
			oraddr[i] |= mcaddr;
		}
	}
	for (i = 0; i < 6; i++) {
		hwfilter.acMulticastAddress[i] = andaddr[i] & oraddr[i];
		hwfilter.acMulticastMask[i] = andaddr[i] | (~oraddr[i]);
	}

	/* Send filter to NVIDIA API */
	sc->hwapi->pfnSetPacketFilter(sc->hwapi->pADCX, &hwfilter);

	NV_UNLOCK(sc);

	DEBUGOUT(NV_DEBUG_RUNNING, "nv: nv_setmulti - exit\n");

	return;
}

/* Change the current media/mediaopts */
static int
nv_ifmedia_upd(struct ifnet *ifp)
{
	struct nv_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_ifmedia_upd\n");

	mii = device_get_softc(sc->miibus);

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		for (miisc = LIST_FIRST(&mii->mii_phys); miisc != NULL;
		     miisc = LIST_NEXT(miisc, mii_list)) {
			mii_phy_reset(miisc);
		}
	}
	mii_mediachg(mii);

	return (0);
}

/* Update current miibus PHY status of media */
static void
nv_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nv_softc *sc;
	struct mii_data *mii;

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_ifmedia_sts\n");

	sc = ifp->if_softc;
	mii = device_get_softc(sc->miibus);
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

/* miibus tick timer - maintain link status */
static void
nv_tick(void *xsc)
{
	struct nv_softc *sc = xsc;
	struct mii_data *mii;
	struct ifnet   *ifp;

	NV_LOCK(sc);

	ifp = &sc->sc_if;
	nv_update_stats(sc);

	mii = device_get_softc(sc->miibus);
	mii_tick(mii);

	if (mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		if (ifp->if_snd.ifq_head != NULL)
			nv_ifstart(ifp);
	}
	callout_reset(&sc->nv_stat_timer, hz, nv_tick, sc);

	NV_UNLOCK(sc);

	return;
}

/* Update ifnet data structure with collected interface stats from API */
static void
nv_update_stats(struct nv_softc *sc)
{
	struct ifnet   *ifp = &sc->sc_if;
	ADAPTER_STATS   stats;

	NV_LOCK(sc);

	if (sc->hwapi) {
		sc->hwapi->pfnGetStatistics(sc->hwapi->pADCX, &stats);

		ifp->if_ipackets = stats.ulSuccessfulReceptions;
		ifp->if_ierrors = stats.ulMissedFrames +
			stats.ulFailedReceptions +
			stats.ulCRCErrors +
			stats.ulFramingErrors +
			stats.ulOverFlowErrors;

		ifp->if_opackets = stats.ulSuccessfulTransmissions;
		ifp->if_oerrors = sc->tx_errors +
			stats.ulFailedTransmissions +
			stats.ulRetryErrors +
			stats.ulUnderflowErrors +
			stats.ulLossOfCarrierErrors +
			stats.ulLateCollisionErrors;

		ifp->if_collisions = stats.ulLateCollisionErrors;
	}
	NV_UNLOCK(sc);

	return;
}

/* miibus Read PHY register wrapper - calls Nvidia API entry point */
static int
nv_miibus_readreg(device_t dev, int phy, int reg)
{
	struct nv_softc *sc = device_get_softc(dev);
	ulong           data;

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_miibus_readreg - entry\n");

	ADAPTER_ReadPhy(sc->hwapi->pADCX, phy, reg, &data);

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_miibus_readreg - exit\n");

	return (data);
}

/* miibus Write PHY register wrapper - calls Nvidia API entry point */
static void
nv_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct nv_softc *sc = device_get_softc(dev);

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_miibus_writereg - entry\n");

	ADAPTER_WritePhy(sc->hwapi->pADCX, phy, reg, (ulong)data);

	DEBUGOUT(NV_DEBUG_MII, "nv: nv_miibus_writereg - exit\n");

	return;
}

/* Watchdog timer to prevent PHY lockups */
static void
nv_watchdog(struct ifnet *ifp)
{
	struct nv_softc *sc = ifp->if_softc;

	device_printf(sc->dev, "device timeout\n");

	sc->tx_errors++;

	nv_stop(sc);
	ifp->if_flags &= ~IFF_RUNNING;
	nv_init(sc);

	if (ifp->if_snd.ifq_head != NULL)
		nv_ifstart(ifp);

	return;
}

/* --- Start of NVOSAPI interface --- */

/* Allocate DMA enabled general use memory for API */
static int
nv_osalloc(void *ctx, MEMORY_BLOCK *mem)
{
	struct nv_softc *sc;
	bus_addr_t      mem_physical;

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osalloc - %d\n", mem->uiLength);

	sc = (struct nv_softc *)ctx;

	mem->pLogical = (PVOID)contigmalloc(mem->uiLength, M_DEVBUF,
				    M_NOWAIT | M_ZERO, 0, ~0, PAGE_SIZE, 0);

	if (!mem->pLogical) {
		device_printf(sc->dev, "memory allocation failed\n");
		return (0);
	}
	memset(mem->pLogical, 0, (ulong)mem->uiLength);
	mem_physical = vtophys(mem->pLogical);
	mem->pPhysical = (PVOID)mem_physical;

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osalloc 0x%x/0x%x - %d\n",
		 (u_int32_t) mem->pLogical,
		 (u_int32_t) mem->pPhysical, mem->uiLength);

	return (1);
}

/* Free allocated memory */
static int
nv_osfree(void *ctx, MEMORY_BLOCK *mem)
{
	DEBUGOUT(NV_DEBUG_API, "nv: nv_osfree - 0x%x - %d\n",
		 (u_int32_t) mem->pLogical, mem->uiLength);

	contigfree(mem->pLogical, PAGE_SIZE, M_DEVBUF);
	return (1);
}

/* Copied directly from nvnet.c */
static int
nv_osallocex(void *ctx, MEMORY_BLOCKEX *mem_block_ex)
{
	MEMORY_BLOCK    mem_block;

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osallocex\n");

	mem_block_ex->pLogical = NULL;
	mem_block_ex->uiLengthOrig = mem_block_ex->uiLength;

	if ((mem_block_ex->AllocFlags & ALLOC_MEMORY_ALIGNED) &&
	    (mem_block_ex->AlignmentSize > 1)) {
		DEBUGOUT(NV_DEBUG_API, "     aligning on %d\n",
			 mem_block_ex->AlignmentSize);
		mem_block_ex->uiLengthOrig += mem_block_ex->AlignmentSize;
	}
	mem_block.uiLength = mem_block_ex->uiLengthOrig;

	if (nv_osalloc(ctx, &mem_block) == 0) {
		return (0);
	}
	mem_block_ex->pLogicalOrig = mem_block.pLogical;
	mem_block_ex->pPhysicalOrigLow = (ULONG)mem_block.pPhysical;
	mem_block_ex->pPhysicalOrigHigh = 0;

	mem_block_ex->pPhysical = mem_block.pPhysical;
	mem_block_ex->pLogical = mem_block.pLogical;

	if (mem_block_ex->uiLength != mem_block_ex->uiLengthOrig) {
		unsigned int    offset;
		offset = mem_block_ex->pPhysicalOrigLow & (mem_block_ex->AlignmentSize - 1);

		if (offset) {
			mem_block_ex->pPhysical = (PVOID)((ULONG)mem_block_ex->pPhysical +
				      mem_block_ex->AlignmentSize - offset);
			mem_block_ex->pLogical = (PVOID)((ULONG)mem_block_ex->pLogical +
				      mem_block_ex->AlignmentSize - offset);
		}		/* if (offset) */
	}			/* if (mem_block_ex->uiLength !=
				 * mem_block_ex->uiLengthOrig) */
	return (1);
}

/* Copied directly from nvnet.c */
static int
nv_osfreeex(void *ctx, MEMORY_BLOCKEX *mem_block_ex)
{
	MEMORY_BLOCK    mem_block;

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osfreeex\n");

	mem_block.pLogical = mem_block_ex->pLogicalOrig;
	mem_block.pPhysical = (PVOID)mem_block_ex->pPhysicalOrigLow;
	mem_block.uiLength = mem_block_ex->uiLengthOrig;

	return (nv_osfree(ctx, &mem_block));
}

/* Clear memory region */
static int
nv_osclear(void *ctx, void *mem, int length)
{
	DEBUGOUT(NV_DEBUG_API, "nv: nv_osclear\n");
	memset(mem, 0, length);
	return (1);
}

/* Sleep for a tick */
static int
nv_osdelay(void *ctx, unsigned long usec)
{
	DELAY(usec);
	return (1);
}

/* Allocate memory for rx buffer */
static int
nv_osallocrxbuf(void *ctx, MEMORY_BLOCK *mem, void **id)
{
	struct nv_softc *sc = ctx;
	struct nv_rx_desc *desc;
	struct nv_map_buffer *buf;
	int             error;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osallocrxbuf\n");

	if (sc->pending_rxs == RX_RING_SIZE) {
		device_printf(sc->dev, "rx ring buffer is full\n");
		goto fail;
	}
	desc = sc->rx_desc + sc->cur_rx;
	buf = &desc->buf;

	if (buf->mbuf == NULL) {
		buf->mbuf = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (buf->mbuf == NULL) {
			device_printf(sc->dev, "failed to allocate memory\n");
			goto fail;
		}
		buf->mbuf->m_len = buf->mbuf->m_pkthdr.len = MCLBYTES;
		m_adj(buf->mbuf, ETHER_ALIGN);

		error = bus_dmamap_load_mbuf(sc->mtag, buf->map, buf->mbuf,
					  nv_dmamap_rx_cb, &desc->paddr, 0);
		if (error) {
			device_printf(sc->dev, "failed to dmamap mbuf\n");
			m_freem(buf->mbuf);
			buf->mbuf = NULL;
			goto fail;
		}
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREREAD);
		desc->buflength = buf->mbuf->m_len;
		desc->vaddr = mtod(buf->mbuf, PVOID);
	}
	sc->pending_rxs++;
	sc->cur_rx = (sc->cur_rx + 1) % RX_RING_SIZE;

	mem->pLogical = (void *)desc->vaddr;
	mem->pPhysical = (void *)desc->paddr;
	mem->uiLength = desc->buflength;
	*id = (void *)desc;

fail:
	NV_UNLOCK(sc);

	return (1);
}


/* Free the rx buffer */
static int
nv_osfreerxbuf(void *ctx, MEMORY_BLOCK *mem, void *id)
{
	struct nv_softc *sc = ctx;
	struct nv_rx_desc *desc;
	struct nv_map_buffer *buf;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_API, "nv: nv_osfreerxbuf\n");

	desc = (struct nv_rx_desc *) id;
	buf = &desc->buf;

	if (buf->mbuf) {
		bus_dmamap_unload(sc->mtag, buf->map);
		bus_dmamap_destroy(sc->mtag, buf->map);
		m_freem(buf->mbuf);
	}
	sc->pending_rxs--;
	buf->mbuf = NULL;

	NV_UNLOCK(sc);

	return (1);
}

/* This gets called by the Nvidia API after our TX packet has been sent */
static int
nv_ospackettx(void *ctx, void *id, unsigned long success)
{
	struct nv_softc *sc = ctx;
	struct nv_map_buffer *buf;
	struct nv_tx_desc *desc = (struct nv_tx_desc *) id;
	struct ifnet   *ifp;

	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_API, "nv: nv_ospackettx\n");

	ifp = &sc->sc_if;

	buf = &desc->buf;

	if (buf->mbuf == NULL)
		goto fail;

	bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->mtag, buf->map);
	m_freem(buf->mbuf);
	buf->mbuf = NULL;

	sc->pending_txs--;

	if (ifp->if_snd.ifq_head != NULL && sc->pending_txs < TX_RING_SIZE)
		nv_ifstart(ifp);

fail:
	NV_UNLOCK(sc);

	return (1);
}

/* This gets called by the Nvidia API when a new packet has been received */
/* XXX What is newbuf used for? XXX */
static int
nv_ospacketrx(void *ctx, void *data, unsigned long success,
	      unsigned char *newbuf, unsigned char priority)
{
	struct nv_softc *sc = ctx;
	struct ifnet   *ifp;
	struct nv_rx_desc *desc;
	struct nv_map_buffer *buf;
	ADAPTER_READ_DATA *readdata;
	NV_LOCK(sc);

	DEBUGOUT(NV_DEBUG_API, "nv: nv_ospacketrx\n");

	ifp = &sc->sc_if;

	readdata = (ADAPTER_READ_DATA *) data;
	desc = readdata->pvID;
	buf = &desc->buf;
	bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTREAD);

	if (success) {
		/* Sync DMA bounce buffer. */
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTREAD);

		/* First mbuf in packet holds the ethernet and packet headers */
		buf->mbuf->m_pkthdr.rcvif = ifp;
		buf->mbuf->m_pkthdr.len = buf->mbuf->m_len = readdata->ulTotalLength;

		bus_dmamap_unload(sc->mtag, buf->map);

		/* Give mbuf to OS. */
		(*ifp->if_input) (ifp, buf->mbuf);
		if (readdata->ulFilterMatch & ADREADFL_MULTICAST_MATCH)
			ifp->if_imcasts++;

		/* Blat the mbuf pointer, kernel will free the mbuf cluster */
		buf->mbuf = NULL;
	} else {
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->mtag, buf->map);
		m_freem(buf->mbuf);
		buf->mbuf = NULL;
	}

	sc->cur_rx = desc - sc->rx_desc;
	sc->pending_rxs--;

	NV_UNLOCK(sc);

	return (1);
}

/* This gets called by NVIDIA API when the PHY link state changes */
static int
nv_oslinkchg(void *ctx, int enabled)
{
	struct nv_softc *sc = (struct nv_softc *)ctx;
	struct ifnet   *ifp;

	DEBUGOUT(NV_DEBUG_API, "nv: nv_oslinkchg\n");

	ifp = &sc->sc_if;

	if (enabled)
		ifp->if_flags |= IFF_UP;
	else
		ifp->if_flags &= ~IFF_UP;


	return (1);
}


/* Setup a watchdog timer */
static int
nv_osalloctimer(void *ctx, void **timer)
{
	struct nv_softc *sc = (struct nv_softc *)ctx;

	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_osalloctimer\n");

	callout_init(&sc->ostimer);
	*timer = &sc->ostimer;

	return (1);
}

/* Free the timer */
static int
nv_osfreetimer(void *ctx, void *timer)
{
	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_osfreetimer\n");

	return (1);
}

/* Setup timer parameters */
static int
nv_osinittimer(void *ctx, void *timer, PTIMER_FUNC func, void *parameters)
{
	struct nv_softc *sc = (struct nv_softc *)ctx;

	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_osinittimer\n");

	sc->ostimer_func = func;
	sc->ostimer_params = parameters;

	return (1);
}

/* Set the timer to go off */
static int
nv_ossettimer(void *ctx, void *timer, unsigned long delay)
{
	struct nv_softc *sc = ctx;

	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_ossettimer\n");

	callout_reset(&sc->ostimer, delay, sc->ostimer_func,
		      sc->ostimer_params);

	return (1);
}

/* Cancel the timer */
static int
nv_oscanceltimer(void *ctx, void *timer)
{
	struct nv_softc *sc = ctx;

	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_oscanceltimer\n");

	callout_stop(&sc->ostimer);

	return (1);
}

static int
nv_ospreprocpkt(void *ctx, void *readdata, void **id, unsigned char *newbuffer,
		unsigned char priority)
{
	/* Not implemented */
	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_ospreprocpkt\n");

	return (1);
}

static void
               *
nv_ospreprocpktnopq(void *ctx, void *readdata)
{
	/* Not implemented */
	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_ospreprocpkt\n");

	return (NULL);
}

static int
nv_osindicatepkt(void *ctx, void **id, unsigned long pktno)
{
	/* Not implemented */
	DEBUGOUT(NV_DEBUG_BROKEN, "nv: nv_osindicatepkt\n");

	return (1);
}

/* Allocate mutex context (already done in nv_attach) */
static int
nv_oslockalloc(void *ctx, int type, void **pLock)
{
	struct nv_softc *sc = (struct nv_softc *)ctx;

	DEBUGOUT(NV_DEBUG_LOCK, "nv: nv_oslockalloc\n");

	*pLock = (void **)sc;

	return (1);
}

/* Obtain a spin lock */
static int
nv_oslockacquire(void *ctx, int type, void *lock)
{
	DEBUGOUT(NV_DEBUG_LOCK, "nv: nv_oslockacquire\n");

	NV_OSLOCK((struct nv_softc *)lock);

	return (1);
}

/* Release lock */
static int
nv_oslockrelease(void *ctx, int type, void *lock)
{
	DEBUGOUT(NV_DEBUG_LOCK, "nv: nv_oslockrelease\n");

	NV_OSUNLOCK((struct nv_softc *)lock);

	return (1);
}

/* I have no idea what this is for */
static void    *
nv_osreturnbufvirt(void *ctx, void *readdata)
{
	/* Not implemented */
	DEBUGOUT(NV_DEBUG_LOCK, "nv: nv_osreturnbufvirt\n");
	panic("nv: nv_osreturnbufvirtual not implemented\n");

	return (NULL);
}


/* --- End on NVOSAPI interface --- */
