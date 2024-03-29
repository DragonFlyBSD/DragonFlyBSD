/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
#include <sys/bitops.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_var.h>

#include <dev/netif/mii_layer/miivar.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#include <dev/netif/et/if_etreg.h>
#include <dev/netif/et/if_etvar.h>

#include "miibus_if.h"

static int	et_probe(device_t);
static int	et_attach(device_t);
static int	et_detach(device_t);
static int	et_shutdown(device_t);

static int	et_miibus_readreg(device_t, int, int);
static int	et_miibus_writereg(device_t, int, int, int);
static void	et_miibus_statchg(device_t);

static void	et_init(void *);
static int	et_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	et_start(struct ifnet *, struct ifaltq_subque *);
static void	et_watchdog(struct ifnet *);
static int	et_ifmedia_upd(struct ifnet *);
static void	et_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int	et_sysctl_rx_intr_npkts(SYSCTL_HANDLER_ARGS);
static int	et_sysctl_rx_intr_delay(SYSCTL_HANDLER_ARGS);

static void	et_intr(void *);
static void	et_enable_intrs(struct et_softc *, uint32_t);
static void	et_disable_intrs(struct et_softc *);
static void	et_rxeof(struct et_softc *);
static void	et_txeof(struct et_softc *, int);

static int	et_dma_alloc(device_t);
static void	et_dma_free(device_t);
static void	et_dma_mem_destroy(bus_dma_tag_t, void *, bus_dmamap_t);
static int	et_dma_mbuf_create(device_t);
static void	et_dma_mbuf_destroy(device_t, int, const int[]);
static int	et_jumbo_mem_alloc(device_t);
static void	et_jumbo_mem_free(device_t);
static int	et_init_tx_ring(struct et_softc *);
static int	et_init_rx_ring(struct et_softc *);
static void	et_free_tx_ring(struct et_softc *);
static void	et_free_rx_ring(struct et_softc *);
static int	et_encap(struct et_softc *, struct mbuf **);
static struct et_jslot *
		et_jalloc(struct et_jumbo_data *);
static void	et_jfree(void *);
static void	et_jref(void *);
static int	et_newbuf(struct et_rxbuf_data *, int, int, int);
static int	et_newbuf_cluster(struct et_rxbuf_data *, int, int);
static int	et_newbuf_hdr(struct et_rxbuf_data *, int, int);
static int	et_newbuf_jumbo(struct et_rxbuf_data *, int, int);

static void	et_stop(struct et_softc *);
static int	et_chip_init(struct et_softc *);
static void	et_chip_attach(struct et_softc *);
static void	et_init_mac(struct et_softc *);
static void	et_init_rxmac(struct et_softc *);
static void	et_init_txmac(struct et_softc *);
static int	et_init_rxdma(struct et_softc *);
static int	et_init_txdma(struct et_softc *);
static int	et_start_rxdma(struct et_softc *);
static int	et_start_txdma(struct et_softc *);
static int	et_stop_rxdma(struct et_softc *);
static int	et_stop_txdma(struct et_softc *);
static int	et_enable_txrx(struct et_softc *, int);
static void	et_reset(struct et_softc *);
static int	et_bus_config(device_t);
static void	et_get_eaddr(device_t, uint8_t[]);
static void	et_setmulti(struct et_softc *);
static void	et_tick(void *);
static void	et_setmedia(struct et_softc *);
static void	et_setup_rxdesc(struct et_rxbuf_data *, int, bus_addr_t);

static const struct et_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} et_devices[] = {
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310,
	  "Agere ET1310 Gigabit Ethernet" },
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310_FAST,
	  "Agere ET1310 Fast Ethernet" },
	{ 0, 0, NULL }
};

static device_method_t et_methods[] = {
	DEVMETHOD(device_probe,		et_probe),
	DEVMETHOD(device_attach,	et_attach),
	DEVMETHOD(device_detach,	et_detach),
	DEVMETHOD(device_shutdown,	et_shutdown),
#if 0
	DEVMETHOD(device_suspend,	et_suspend),
	DEVMETHOD(device_resume,	et_resume),
#endif

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	DEVMETHOD(miibus_readreg,	et_miibus_readreg),
	DEVMETHOD(miibus_writereg,	et_miibus_writereg),
	DEVMETHOD(miibus_statchg,	et_miibus_statchg),

	DEVMETHOD_END
};

static driver_t et_driver = {
	"et",
	et_methods,
	sizeof(struct et_softc)
};

static devclass_t et_devclass;

DECLARE_DUMMY_MODULE(if_et);
MODULE_DEPEND(if_et, miibus, 1, 1, 1);
DRIVER_MODULE(if_et, pci, et_driver, et_devclass, NULL, NULL);
DRIVER_MODULE(miibus, et, miibus_driver, miibus_devclass, NULL, NULL);

static int	et_rx_intr_npkts = 129;
static int	et_rx_intr_delay = 25;		/* x4 usec */
static int	et_tx_intr_nsegs = 256;
static uint32_t	et_timer = 1000 * 1000 * 1000;	/* nanosec */

static int	et_msi_enable = 1;

TUNABLE_INT("hw.et.timer", &et_timer);
TUNABLE_INT("hw.et.rx_intr_npkts", &et_rx_intr_npkts);
TUNABLE_INT("hw.et.rx_intr_delay", &et_rx_intr_delay);
TUNABLE_INT("hw.et.tx_intr_nsegs", &et_tx_intr_nsegs);
TUNABLE_INT("hw.et.msi.enable", &et_msi_enable);

struct et_bsize {
	int		bufsize;
	int		jumbo;
	et_newbuf_t	newbuf;
};

static const struct et_bsize	et_bufsize_std[ET_RX_NRING] = {
	{ .bufsize = ET_RXDMA_CTRL_RING0_128,	.jumbo = 0,
	  .newbuf = et_newbuf_hdr },
	{ .bufsize = ET_RXDMA_CTRL_RING1_2048,	.jumbo = 0,
	  .newbuf = et_newbuf_cluster },
};

static const struct et_bsize	et_bufsize_jumbo[ET_RX_NRING] = {
	{ .bufsize = ET_RXDMA_CTRL_RING0_128,	.jumbo = 0,
	  .newbuf = et_newbuf_hdr },
	{ .bufsize = ET_RXDMA_CTRL_RING1_16384,	.jumbo = 1,
	  .newbuf = et_newbuf_jumbo },
};

static int
et_probe(device_t dev)
{
	const struct et_dev *d;
	uint16_t did, vid;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = et_devices; d->desc != NULL; ++d) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
et_attach(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int error;
	u_int irq_flags;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	callout_init(&sc->sc_tick);

	/*
	 * Initialize tunables
	 */
	sc->sc_rx_intr_npkts = et_rx_intr_npkts;
	sc->sc_rx_intr_delay = et_rx_intr_delay;
	sc->sc_tx_intr_nsegs = et_tx_intr_nsegs;
	sc->sc_timer = et_timer;

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t irq, mem;

		irq = pci_read_config(dev, PCIR_INTLINE, 4);
		mem = pci_read_config(dev, ET_PCIR_BAR, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, PCIR_INTLINE, irq, 4);
		pci_write_config(dev, ET_PCIR_BAR, mem, 4);
	}
#endif	/* !BURN_BRIDGE */

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate IO memory
	 */
	sc->sc_mem_rid = ET_PCIR_BAR;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						&sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "can't allocate IO memory\n");
		return ENXIO;
	}
	sc->sc_mem_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_mem_bh = rman_get_bushandle(sc->sc_mem_res);

	/*
	 * Allocate IRQ
	 */
	sc->sc_irq_type = pci_alloc_1intr(dev, et_msi_enable,
	    &sc->sc_irq_rid, &irq_flags);
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irq_rid, irq_flags);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "can't allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Create sysctl tree
	 */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
			OID_AUTO, "rx_intr_npkts", CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, et_sysctl_rx_intr_npkts, "I",
			"RX IM, # packets per RX interrupt");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
			OID_AUTO, "rx_intr_delay", CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, et_sysctl_rx_intr_delay, "I",
			"RX IM, RX interrupt delay (x10 usec)");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "tx_intr_nsegs", CTLFLAG_RW, &sc->sc_tx_intr_nsegs, 0,
		       "TX IM, # segments per TX interrupt");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"timer", CTLFLAG_RW, &sc->sc_timer, 0,
			"TX timer");

	error = et_bus_config(dev);
	if (error)
		goto fail;

	et_get_eaddr(dev, eaddr);

	CSR_WRITE_4(sc, ET_PM,
		    ET_PM_SYSCLK_GATE | ET_PM_TXCLK_GATE | ET_PM_RXCLK_GATE);

	et_reset(sc);

	et_disable_intrs(sc);

	error = et_dma_alloc(dev);
	if (error)
		goto fail;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = et_init;
	ifp->if_ioctl = et_ioctl;
	ifp->if_start = et_start;
	ifp->if_watchdog = et_watchdog;
	ifp->if_mtu = ETHERMTU;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_nmbclusters = ET_RX_NDESC;
	ifq_set_maxlen(&ifp->if_snd, ET_TX_NDESC);
	ifq_set_ready(&ifp->if_snd);

	et_chip_attach(sc);

	error = mii_phy_probe(dev, &sc->sc_miibus,
			      et_ifmedia_upd, et_ifmedia_sts);
	if (error) {
		device_printf(dev, "can't probe any PHY\n");
		goto fail;
	}

	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sc_irq_res));

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE, et_intr, sc,
			       &sc->sc_irq_handle, ifp->if_serializer);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "can't setup intr\n");
		goto fail;
	}

	/* Increase non-cluster mbuf limit; used by tiny RX ring */
	mb_inclimit(ET_RX_NDESC);

	return 0;
fail:
	et_detach(dev);
	return error;
}

static int
et_detach(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		et_stop(sc);
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);

		/* Decrease non-cluster mbuf limit increased by us */
		mb_inclimit(-ET_RX_NDESC);
	}

	if (sc->sc_miibus != NULL)
		device_delete_child(dev, sc->sc_miibus);
	bus_generic_detach(dev);

	if (sc->sc_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
				     sc->sc_irq_res);
	}
	if (sc->sc_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->sc_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid,
				     sc->sc_mem_res);
	}

	et_dma_free(dev);

	return 0;
}

static int
et_shutdown(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	et_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
	return 0;
}

static int
et_miibus_readreg(device_t dev, int phy, int reg)
{
	struct et_softc *sc = device_get_softc(dev);
	uint32_t val;
	int i, ret;

	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = __SHIFTIN(phy, ET_MII_ADDR_PHY) |
	      __SHIFTIN(reg, ET_MII_ADDR_REG);
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start reading */
	CSR_WRITE_4(sc, ET_MII_CMD, ET_MII_CMD_READ);

#define NRETRY	50

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & (ET_MII_IND_BUSY | ET_MII_IND_INVALID)) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		if_printf(&sc->arpcom.ac_if,
			  "read phy %d, reg %d timed out\n", phy, reg);
		ret = 0;
		goto back;
	}

#undef NRETRY

	val = CSR_READ_4(sc, ET_MII_STAT);
	ret = __SHIFTOUT(val, ET_MII_STAT_VALUE);

back:
	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
	return ret;
}

static int
et_miibus_writereg(device_t dev, int phy, int reg, int val0)
{
	struct et_softc *sc = device_get_softc(dev);
	uint32_t val;
	int i;

	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = __SHIFTIN(phy, ET_MII_ADDR_PHY) |
	      __SHIFTIN(reg, ET_MII_ADDR_REG);
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start writing */
	CSR_WRITE_4(sc, ET_MII_CTRL, __SHIFTIN(val0, ET_MII_CTRL_VALUE));

#define NRETRY 100

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & ET_MII_IND_BUSY) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		if_printf(&sc->arpcom.ac_if,
			  "write phy %d, reg %d timed out\n", phy, reg);
		et_miibus_readreg(dev, phy, reg);
	}

#undef NRETRY

	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
	return 0;
}

static void
et_miibus_statchg(device_t dev)
{
	et_setmedia(device_get_softc(dev));
}

static int
et_ifmedia_upd(struct ifnet *ifp)
{
	struct et_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return 0;
}

static void
et_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct et_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static void
et_stop(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->sc_tick);

	et_stop_rxdma(sc);
	et_stop_txdma(sc);

	et_disable_intrs(sc);

	et_free_tx_ring(sc);
	et_free_rx_ring(sc);

	et_reset(sc);

	sc->sc_tx = 0;
	sc->sc_tx_intr = 0;
	sc->sc_flags &= ~ET_FLAG_TXRX_ENABLED;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

static int
et_bus_config(device_t dev)
{
	uint32_t val, max_plsz;
	uint16_t ack_latency, replay_timer;

	/*
	 * Test whether EEPROM is valid
	 * NOTE: Read twice to get the correct value
	 */
	pci_read_config(dev, ET_PCIR_EEPROM_STATUS, 1);
	val = pci_read_config(dev, ET_PCIR_EEPROM_STATUS, 1);
	if (val & ET_PCIM_EEPROM_STATUS_ERROR) {
		device_printf(dev, "EEPROM status error 0x%02x\n", val);
		return ENXIO;
	}

	/* TODO: LED */

	/*
	 * Configure ACK latency and replay timer according to
	 * max playload size
	 */
	val = pci_read_config(dev, ET_PCIR_DEVICE_CAPS, 4);
	max_plsz = val & ET_PCIM_DEVICE_CAPS_MAX_PLSZ;

	switch (max_plsz) {
	case ET_PCIV_DEVICE_CAPS_PLSZ_128:
		ack_latency = ET_PCIV_ACK_LATENCY_128;
		replay_timer = ET_PCIV_REPLAY_TIMER_128;
		break;

	case ET_PCIV_DEVICE_CAPS_PLSZ_256:
		ack_latency = ET_PCIV_ACK_LATENCY_256;
		replay_timer = ET_PCIV_REPLAY_TIMER_256;
		break;

	default:
		ack_latency = pci_read_config(dev, ET_PCIR_ACK_LATENCY, 2);
		replay_timer = pci_read_config(dev, ET_PCIR_REPLAY_TIMER, 2);
		device_printf(dev, "ack latency %u, replay timer %u\n",
			      ack_latency, replay_timer);
		break;
	}
	if (ack_latency != 0) {
		pci_write_config(dev, ET_PCIR_ACK_LATENCY, ack_latency, 2);
		pci_write_config(dev, ET_PCIR_REPLAY_TIMER, replay_timer, 2);
	}

	/*
	 * Set L0s and L1 latency timer to 2us
	 */
	val = ET_PCIV_L0S_LATENCY(2) | ET_PCIV_L1_LATENCY(2);
	pci_write_config(dev, ET_PCIR_L0S_L1_LATENCY, val, 1);

	/*
	 * Set max read request size to 2048 bytes
	 */
	val = pci_read_config(dev, ET_PCIR_DEVICE_CTRL, 2);
	val &= ~ET_PCIM_DEVICE_CTRL_MAX_RRSZ;
	val |= ET_PCIV_DEVICE_CTRL_RRSZ_2K;
	pci_write_config(dev, ET_PCIR_DEVICE_CTRL, val, 2);

	return 0;
}

static void
et_get_eaddr(device_t dev, uint8_t eaddr[])
{
	uint32_t val;
	int i;

	val = pci_read_config(dev, ET_PCIR_MAC_ADDR0, 4);
	for (i = 0; i < 4; ++i)
		eaddr[i] = (val >> (8 * i)) & 0xff;

	val = pci_read_config(dev, ET_PCIR_MAC_ADDR1, 2);
	for (; i < ETHER_ADDR_LEN; ++i)
		eaddr[i] = (val >> (8 * (i - 4))) & 0xff;
}

static void
et_reset(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	CSR_WRITE_4(sc, ET_SWRST,
		    ET_SWRST_TXDMA | ET_SWRST_RXDMA |
		    ET_SWRST_TXMAC | ET_SWRST_RXMAC |
		    ET_SWRST_MAC | ET_SWRST_MAC_STAT | ET_SWRST_MMC);

	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC);
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
}

static void
et_disable_intrs(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_INTR_MASK, 0xffffffff);
}

static void
et_enable_intrs(struct et_softc *sc, uint32_t intrs)
{
	CSR_WRITE_4(sc, ET_INTR_MASK, ~intrs);
}

static int
et_dma_alloc(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	int i, error;

	/*
	 * Create top level DMA tag
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR,
				   BUS_SPACE_MAXADDR,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->sc_dtag);
	if (error) {
		device_printf(dev, "can't create DMA tag\n");
		return error;
	}

	/*
	 * Create TX ring DMA stuffs
	 */
	tx_ring->tr_desc = bus_dmamem_coherent_any(sc->sc_dtag,
				ET_ALIGN, ET_TX_RING_SIZE,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&tx_ring->tr_dtag, &tx_ring->tr_dmap,
				&tx_ring->tr_paddr);
	if (tx_ring->tr_desc == NULL) {
		device_printf(dev, "can't create TX ring DMA stuffs\n");
		return ENOMEM;
	}

	/*
	 * Create TX status DMA stuffs
	 */
	txsd->txsd_status = bus_dmamem_coherent_any(sc->sc_dtag,
				ET_ALIGN, sizeof(uint32_t),
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&txsd->txsd_dtag, &txsd->txsd_dmap,
				&txsd->txsd_paddr);
	if (txsd->txsd_status == NULL) {
		device_printf(dev, "can't create TX status DMA stuffs\n");
		return ENOMEM;
	}

	/*
	 * Create DMA stuffs for RX rings
	 */
	for (i = 0; i < ET_RX_NRING; ++i) {
		static const uint32_t rx_ring_posreg[ET_RX_NRING] =
		{ ET_RX_RING0_POS, ET_RX_RING1_POS };

		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[i];

		rx_ring->rr_desc = bus_dmamem_coherent_any(sc->sc_dtag,
					ET_ALIGN, ET_RX_RING_SIZE,
					BUS_DMA_WAITOK | BUS_DMA_ZERO,
					&rx_ring->rr_dtag, &rx_ring->rr_dmap,
					&rx_ring->rr_paddr);
		if (rx_ring->rr_desc == NULL) {
			device_printf(dev, "can't create DMA stuffs for "
				      "the %d RX ring\n", i);
			return ENOMEM;
		}
		rx_ring->rr_posreg = rx_ring_posreg[i];
	}

	/*
	 * Create RX stat ring DMA stuffs
	 */
	rxst_ring->rsr_stat = bus_dmamem_coherent_any(sc->sc_dtag,
				ET_ALIGN, ET_RXSTAT_RING_SIZE,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&rxst_ring->rsr_dtag, &rxst_ring->rsr_dmap,
				&rxst_ring->rsr_paddr);
	if (rxst_ring->rsr_stat == NULL) {
		device_printf(dev, "can't create RX stat ring DMA stuffs\n");
		return ENOMEM;
	}

	/*
	 * Create RX status DMA stuffs
	 */
	rxsd->rxsd_status = bus_dmamem_coherent_any(sc->sc_dtag,
				ET_ALIGN, sizeof(struct et_rxstatus),
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&rxsd->rxsd_dtag, &rxsd->rxsd_dmap,
				&rxsd->rxsd_paddr);
	if (rxsd->rxsd_status == NULL) {
		device_printf(dev, "can't create RX status DMA stuffs\n");
		return ENOMEM;
	}

	/*
	 * Create mbuf DMA stuffs
	 */
	error = et_dma_mbuf_create(dev);
	if (error)
		return error;

	/*
	 * Create jumbo buffer DMA stuffs
	 * NOTE: Allow it to fail
	 */
	if (et_jumbo_mem_alloc(dev) == 0)
		sc->sc_flags |= ET_FLAG_JUMBO;

	return 0;
}

static void
et_dma_free(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	int i, rx_done[ET_RX_NRING];

	/*
	 * Destroy TX ring DMA stuffs
	 */
	et_dma_mem_destroy(tx_ring->tr_dtag, tx_ring->tr_desc,
			   tx_ring->tr_dmap);

	/*
	 * Destroy TX status DMA stuffs
	 */
	et_dma_mem_destroy(txsd->txsd_dtag, txsd->txsd_status,
			   txsd->txsd_dmap);

	/*
	 * Destroy DMA stuffs for RX rings
	 */
	for (i = 0; i < ET_RX_NRING; ++i) {
		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[i];

		et_dma_mem_destroy(rx_ring->rr_dtag, rx_ring->rr_desc,
				   rx_ring->rr_dmap);
	}

	/*
	 * Destroy RX stat ring DMA stuffs
	 */
	et_dma_mem_destroy(rxst_ring->rsr_dtag, rxst_ring->rsr_stat,
			   rxst_ring->rsr_dmap);

	/*
	 * Destroy RX status DMA stuffs
	 */
	et_dma_mem_destroy(rxsd->rxsd_dtag, rxsd->rxsd_status,
			   rxsd->rxsd_dmap);

	/*
	 * Destroy mbuf DMA stuffs
	 */
	for (i = 0; i < ET_RX_NRING; ++i)
		rx_done[i] = ET_RX_NDESC;
	et_dma_mbuf_destroy(dev, ET_TX_NDESC, rx_done);

	/*
	 * Destroy jumbo buffer DMA stuffs
	 */
	if (sc->sc_flags & ET_FLAG_JUMBO)
		et_jumbo_mem_free(dev);

	/*
	 * Destroy top level DMA tag
	 */
	if (sc->sc_dtag != NULL)
		bus_dma_tag_destroy(sc->sc_dtag);
}

static int
et_dma_mbuf_create(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int i, error, rx_done[ET_RX_NRING];

	/*
	 * Create RX mbuf DMA tag
	 */
	error = bus_dma_tag_create(sc->sc_dtag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   MCLBYTES, 1, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,
				   &sc->sc_rxbuf_dtag);
	if (error) {
		device_printf(dev, "can't create RX mbuf DMA tag\n");
		return error;
	}

	/*
	 * Create spare DMA map for RX mbufs
	 */
	error = bus_dmamap_create(sc->sc_rxbuf_dtag, BUS_DMA_WAITOK,
				  &sc->sc_rxbuf_tmp_dmap);
	if (error) {
		device_printf(dev, "can't create spare mbuf DMA map\n");
		bus_dma_tag_destroy(sc->sc_rxbuf_dtag);
		sc->sc_rxbuf_dtag = NULL;
		return error;
	}

	/*
	 * Create DMA maps for RX mbufs
	 */
	bzero(rx_done, sizeof(rx_done));
	for (i = 0; i < ET_RX_NRING; ++i) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[i];
		int j;

		for (j = 0; j < ET_RX_NDESC; ++j) {
			error = bus_dmamap_create(sc->sc_rxbuf_dtag,
						  BUS_DMA_WAITOK,
						  &rbd->rbd_buf[j].rb_dmap);
			if (error) {
				device_printf(dev, "can't create %d RX mbuf "
					      "for %d RX ring\n", j, i);
				rx_done[i] = j;
				et_dma_mbuf_destroy(dev, 0, rx_done);
				return error;
			}
		}
		rx_done[i] = ET_RX_NDESC;

		rbd->rbd_softc = sc;
		rbd->rbd_ring = &sc->sc_rx_ring[i];
	}

	/*
	 * Create TX mbuf DMA tag
	 */
	error = bus_dma_tag_create(sc->sc_dtag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   ET_JUMBO_FRAMELEN, ET_NSEG_MAX, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK |
				   BUS_DMA_ONEBPAGE,
				   &sc->sc_txbuf_dtag);
	if (error) {
		device_printf(dev, "can't create TX mbuf DMA tag\n");
		return error;
	}

	/*
	 * Create DMA maps for TX mbufs
	 */
	for (i = 0; i < ET_TX_NDESC; ++i) {
		error = bus_dmamap_create(sc->sc_txbuf_dtag,
					  BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
					  &tbd->tbd_buf[i].tb_dmap);
		if (error) {
			device_printf(dev, "can't create %d TX mbuf "
				      "DMA map\n", i);
			et_dma_mbuf_destroy(dev, i, rx_done);
			return error;
		}
	}

	return 0;
}

static void
et_dma_mbuf_destroy(device_t dev, int tx_done, const int rx_done[])
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int i;

	/*
	 * Destroy DMA tag and maps for RX mbufs
	 */
	if (sc->sc_rxbuf_dtag) {
		for (i = 0; i < ET_RX_NRING; ++i) {
			struct et_rxbuf_data *rbd = &sc->sc_rx_data[i];
			int j;

			for (j = 0; j < rx_done[i]; ++j) {
				struct et_rxbuf *rb = &rbd->rbd_buf[j];

				KASSERT(rb->rb_mbuf == NULL,
					("RX mbuf in %d RX ring is "
					 "not freed yet", i));
				bus_dmamap_destroy(sc->sc_rxbuf_dtag,
						   rb->rb_dmap);
			}
		}
		bus_dmamap_destroy(sc->sc_rxbuf_dtag, sc->sc_rxbuf_tmp_dmap);
		bus_dma_tag_destroy(sc->sc_rxbuf_dtag);
		sc->sc_rxbuf_dtag = NULL;
	}

	/*
	 * Destroy DMA tag and maps for TX mbufs
	 */
	if (sc->sc_txbuf_dtag) {
		for (i = 0; i < tx_done; ++i) {
			struct et_txbuf *tb = &tbd->tbd_buf[i];

			KASSERT(tb->tb_mbuf == NULL,
				("TX mbuf is not freed yet"));
			bus_dmamap_destroy(sc->sc_txbuf_dtag, tb->tb_dmap);
		}
		bus_dma_tag_destroy(sc->sc_txbuf_dtag);
		sc->sc_txbuf_dtag = NULL;
	}
}

static void
et_dma_mem_destroy(bus_dma_tag_t dtag, void *addr, bus_dmamap_t dmap)
{
	if (dtag != NULL) {
		bus_dmamap_unload(dtag, dmap);
		bus_dmamem_free(dtag, addr, dmap);
		bus_dma_tag_destroy(dtag);
	}
}

static void
et_chip_attach(struct et_softc *sc)
{
	uint32_t val;

	/*
	 * Perform minimal initialization
	 */

	/* Disable loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup half duplex mode
	 */
	val = __SHIFTIN(10, ET_MAC_HDX_ALT_BEB_TRUNC) |
	      __SHIFTIN(15, ET_MAC_HDX_REXMIT_MAX) |
	      __SHIFTIN(55, ET_MAC_HDX_COLLWIN) |
	      ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);
}

static void
et_intr(void *xsc)
{
	struct et_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t intrs;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	et_disable_intrs(sc);

	intrs = CSR_READ_4(sc, ET_INTR_STATUS);
	intrs &= ET_INTRS;
	if (intrs == 0)	/* Not interested */
		goto back;

	if (intrs & ET_INTR_RXEOF)
		et_rxeof(sc);
	if (intrs & (ET_INTR_TXEOF | ET_INTR_TIMER))
		et_txeof(sc, 1);
	if (intrs & ET_INTR_TIMER)
		CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);
back:
	et_enable_intrs(sc, ET_INTRS);
}

static void
et_init(void *xsc)
{
	struct et_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	const struct et_bsize *arr;
	int error, i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	et_stop(sc);

	arr = ET_FRAMELEN(ifp->if_mtu) < MCLBYTES ?
	      et_bufsize_std : et_bufsize_jumbo;
	for (i = 0; i < ET_RX_NRING; ++i) {
		sc->sc_rx_data[i].rbd_bufsize = arr[i].bufsize;
		sc->sc_rx_data[i].rbd_newbuf = arr[i].newbuf;
		sc->sc_rx_data[i].rbd_jumbo = arr[i].jumbo;
	}

	error = et_init_tx_ring(sc);
	if (error)
		goto back;

	error = et_init_rx_ring(sc);
	if (error)
		goto back;

	error = et_chip_init(sc);
	if (error)
		goto back;

	error = et_enable_txrx(sc, 1);
	if (error)
		goto back;

	et_enable_intrs(sc, ET_INTRS);

	callout_reset(&sc->sc_tick, hz, et_tick, sc);

	CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
back:
	if (error)
		et_stop(sc);
}

static int
et_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct et_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, max_framelen;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc->sc_if_flags) &
				    (IFF_ALLMULTI | IFF_PROMISC))
					et_setmulti(sc);
			} else {
				et_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				et_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			et_setmulti(sc);
		break;

	case SIOCSIFMTU:
		if (sc->sc_flags & ET_FLAG_JUMBO)
			max_framelen = ET_JUMBO_FRAMELEN;
		else
			max_framelen = MCLBYTES - 1;

		if (ET_FRAMELEN(ifr->ifr_mtu) > max_framelen) {
			error = EOPNOTSUPP;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		if (ifp->if_flags & IFF_RUNNING)
			et_init(sc);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return error;
}

static void
et_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct et_softc *sc = ifp->if_softc;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int trans, oactive;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	oactive = 0;
	trans = 0;
	for (;;) {
		struct mbuf *m;
		int error;

		if ((tbd->tbd_used + ET_NSEG_SPARE) > ET_TX_NDESC) {
			if (oactive) {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}

			et_txeof(sc, 0);
			oactive = 1;
			continue;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		error = et_encap(sc, &m);
		if (error) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			KKASSERT(m == NULL);

			if (error == EFBIG) {
				/*
				 * Excessive fragmented packets
				 */
				if (oactive) {
					ifq_set_oactive(&ifp->if_snd);
					break;
				}
				et_txeof(sc, 0);
				oactive = 1;
			}
			continue;
		} else {
			oactive = 0;
		}
		trans = 1;

		BPF_MTAP(ifp, m);
	}

	if (trans)
		ifp->if_timer = 5;
}

static void
et_watchdog(struct ifnet *ifp)
{
	ASSERT_SERIALIZED(ifp->if_serializer);

	if_printf(ifp, "watchdog timed out\n");

	ifp->if_init(ifp->if_softc);
	if_devstart(ifp);
}

static int
et_stop_rxdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_RXDMA_CTRL,
		    ET_RXDMA_CTRL_HALT | ET_RXDMA_CTRL_RING1_ENABLE);

	DELAY(5);
	if ((CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) == 0) {
		if_printf(&sc->arpcom.ac_if, "can't stop RX DMA engine\n");
		return ETIMEDOUT;
	}
	return 0;
}

static int
et_stop_txdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_TXDMA_CTRL,
		    ET_TXDMA_CTRL_HALT | ET_TXDMA_CTRL_SINGLE_EPKT);
	return 0;
}

static void
et_free_tx_ring(struct et_softc *sc)
{
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	int i;

	for (i = 0; i < ET_TX_NDESC; ++i) {
		struct et_txbuf *tb = &tbd->tbd_buf[i];

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_txbuf_dtag, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}
	}
	bzero(tx_ring->tr_desc, ET_TX_RING_SIZE);
}

static void
et_free_rx_ring(struct et_softc *sc)
{
	int n;

	for (n = 0; n < ET_RX_NRING; ++n) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[n];
		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[n];
		int i;

		for (i = 0; i < ET_RX_NDESC; ++i) {
			struct et_rxbuf *rb = &rbd->rbd_buf[i];

			if (rb->rb_mbuf != NULL) {
				if (!rbd->rbd_jumbo) {
					bus_dmamap_unload(sc->sc_rxbuf_dtag,
							  rb->rb_dmap);
				}
				m_freem(rb->rb_mbuf);
				rb->rb_mbuf = NULL;
			}
		}
		bzero(rx_ring->rr_desc, ET_RX_RING_SIZE);
	}
}

static void
et_setmulti(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t hash[4] = { 0, 0, 0, 0 };
	uint32_t rxmac_ctrl, pktfilt;
	struct ifmultiaddr *ifma;
	int i, count;

	pktfilt = CSR_READ_4(sc, ET_PKTFILT);
	rxmac_ctrl = CSR_READ_4(sc, ET_RXMAC_CTRL);

	pktfilt &= ~(ET_PKTFILT_BCAST | ET_PKTFILT_MCAST | ET_PKTFILT_UCAST);
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		rxmac_ctrl |= ET_RXMAC_CTRL_NO_PKTFILT;
		goto back;
	}

	count = 0;
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		uint32_t *hp, h;

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
				   ifma->ifma_addr), ETHER_ADDR_LEN);
		h = (h & 0x3f800000) >> 23;

		hp = &hash[0];
		if (h >= 32 && h < 64) {
			h -= 32;
			hp = &hash[1];
		} else if (h >= 64 && h < 96) {
			h -= 64;
			hp = &hash[2];
		} else if (h >= 96) {
			h -= 96;
			hp = &hash[3];
		}
		*hp |= (1 << h);

		++count;
	}

	for (i = 0; i < 4; ++i)
		CSR_WRITE_4(sc, ET_MULTI_HASH + (i * 4), hash[i]);

	if (count > 0)
		pktfilt |= ET_PKTFILT_MCAST;
	rxmac_ctrl &= ~ET_RXMAC_CTRL_NO_PKTFILT;
back:
	CSR_WRITE_4(sc, ET_PKTFILT, pktfilt);
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, rxmac_ctrl);
}

static int
et_chip_init(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t rxq_end;
	int error, frame_len, rxmem_size;

	/*
	 * Split 16Kbytes internal memory between TX and RX
	 * according to frame length.
	 */
	frame_len = ET_FRAMELEN(ifp->if_mtu);
	if (frame_len < 2048) {
		rxmem_size = ET_MEM_RXSIZE_DEFAULT;
	} else if (frame_len <= ET_RXMAC_CUT_THRU_FRMLEN) {
		rxmem_size = ET_MEM_SIZE / 2;
	} else {
		rxmem_size = ET_MEM_SIZE -
		roundup(frame_len + ET_MEM_TXSIZE_EX, ET_MEM_UNIT);
	}
	rxq_end = ET_QUEUE_ADDR(rxmem_size);

	CSR_WRITE_4(sc, ET_RXQUEUE_START, ET_QUEUE_ADDR_START);
	CSR_WRITE_4(sc, ET_RXQUEUE_END, rxq_end);
	CSR_WRITE_4(sc, ET_TXQUEUE_START, rxq_end + 1);
	CSR_WRITE_4(sc, ET_TXQUEUE_END, ET_QUEUE_ADDR_END);

	/* No loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Clear MSI configure */
	CSR_WRITE_4(sc, ET_MSI_CFG, 0);

	/* Disable timer */
	CSR_WRITE_4(sc, ET_TIMER, 0);

	/* Initialize MAC */
	et_init_mac(sc);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);

	/* Initialize RX MAC */
	et_init_rxmac(sc);

	/* Initialize TX MAC */
	et_init_txmac(sc);

	/* Initialize RX DMA engine */
	error = et_init_rxdma(sc);
	if (error)
		return error;

	/* Initialize TX DMA engine */
	error = et_init_txdma(sc);
	if (error)
		return error;

	return 0;
}

static int
et_init_tx_ring(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;

	bzero(tx_ring->tr_desc, ET_TX_RING_SIZE);

	tbd->tbd_start_index = 0;
	tbd->tbd_start_wrap = 0;
	tbd->tbd_used = 0;

	bzero(txsd->txsd_status, sizeof(uint32_t));

	return 0;
}

static int
et_init_rx_ring(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	int n;

	for (n = 0; n < ET_RX_NRING; ++n) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[n];
		int i, error;

		for (i = 0; i < ET_RX_NDESC; ++i) {
			error = rbd->rbd_newbuf(rbd, i, 1);
			if (error) {
				if_printf(&sc->arpcom.ac_if, "%d ring %d buf, "
					  "newbuf failed: %d\n", n, i, error);
				return error;
			}
		}
	}

	bzero(rxsd->rxsd_status, sizeof(struct et_rxstatus));
	bzero(rxst_ring->rsr_stat, ET_RXSTAT_RING_SIZE);

	return 0;
}

static int
et_init_rxdma(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxdesc_ring *rx_ring;
	int error;

	error = et_stop_rxdma(sc);
	if (error) {
		if_printf(&sc->arpcom.ac_if, "can't init RX DMA engine\n");
		return error;
	}

	/*
	 * Install RX status
	 */
	CSR_WRITE_4(sc, ET_RX_STATUS_HI, ET_ADDR_HI(rxsd->rxsd_paddr));
	CSR_WRITE_4(sc, ET_RX_STATUS_LO, ET_ADDR_LO(rxsd->rxsd_paddr));

	/*
	 * Install RX stat ring
	 */
	CSR_WRITE_4(sc, ET_RXSTAT_HI, ET_ADDR_HI(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_LO, ET_ADDR_LO(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_CNT, ET_RX_NSTAT - 1);
	CSR_WRITE_4(sc, ET_RXSTAT_POS, 0);
	CSR_WRITE_4(sc, ET_RXSTAT_MINCNT, ((ET_RX_NSTAT * 15) / 100) - 1);

	/* Match ET_RXSTAT_POS */
	rxst_ring->rsr_index = 0;
	rxst_ring->rsr_wrap = 0;

	/*
	 * Install the 2nd RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[1];
	CSR_WRITE_4(sc, ET_RX_RING1_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING1_POS, ET_RX_RING1_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING1_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING1_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * Install the 1st RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[0];
	CSR_WRITE_4(sc, ET_RX_RING0_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING0_POS, ET_RX_RING0_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING0_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING0_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * RX intr moderation
	 */
	CSR_WRITE_4(sc, ET_RX_INTR_NPKTS, sc->sc_rx_intr_npkts);
	CSR_WRITE_4(sc, ET_RX_INTR_DELAY, sc->sc_rx_intr_delay);

	return 0;
}

static int
et_init_txdma(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	int error;

	error = et_stop_txdma(sc);
	if (error) {
		if_printf(&sc->arpcom.ac_if, "can't init TX DMA engine\n");
		return error;
	}

	/*
	 * Install TX descriptor ring
	 */
	CSR_WRITE_4(sc, ET_TX_RING_HI, ET_ADDR_HI(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_LO, ET_ADDR_LO(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_CNT, ET_TX_NDESC - 1);

	/*
	 * Install TX status
	 */
	CSR_WRITE_4(sc, ET_TX_STATUS_HI, ET_ADDR_HI(txsd->txsd_paddr));
	CSR_WRITE_4(sc, ET_TX_STATUS_LO, ET_ADDR_LO(txsd->txsd_paddr));

	CSR_WRITE_4(sc, ET_TX_READY_POS, 0);

	/* Match ET_TX_READY_POS */
	tx_ring->tr_ready_index = 0;
	tx_ring->tr_ready_wrap = 0;

	return 0;
}

static void
et_init_mac(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	const uint8_t *eaddr = IF_LLADDR(ifp);
	uint32_t val;

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup inter packet gap
	 */
	val = __SHIFTIN(56, ET_IPG_NONB2B_1) |
	      __SHIFTIN(88, ET_IPG_NONB2B_2) |
	      __SHIFTIN(80, ET_IPG_MINIFG) |
	      __SHIFTIN(96, ET_IPG_B2B);
	CSR_WRITE_4(sc, ET_IPG, val);

	/*
	 * Setup half duplex mode
	 */
	val = __SHIFTIN(10, ET_MAC_HDX_ALT_BEB_TRUNC) |
	      __SHIFTIN(15, ET_MAC_HDX_REXMIT_MAX) |
	      __SHIFTIN(55, ET_MAC_HDX_COLLWIN) |
	      ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/*
	 * Set MAC address
	 */
	val = eaddr[2] | (eaddr[3] << 8) | (eaddr[4] << 16) | (eaddr[5] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR1, val);
	val = (eaddr[0] << 16) | (eaddr[1] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR2, val);

	/* Set max frame length */
	CSR_WRITE_4(sc, ET_MAX_FRMLEN, ET_FRAMELEN(ifp->if_mtu));

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
}

static void
et_init_rxmac(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	const uint8_t *eaddr = IF_LLADDR(ifp);
	uint32_t val;
	int i;

	/* Disable RX MAC and WOL */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, ET_RXMAC_CTRL_WOL_DISABLE);

	/*
	 * Clear all WOL related registers
	 */
	for (i = 0; i < 3; ++i)
		CSR_WRITE_4(sc, ET_WOL_CRC + (i * 4), 0);
	for (i = 0; i < 20; ++i)
		CSR_WRITE_4(sc, ET_WOL_MASK + (i * 4), 0);

	/*
	 * Set WOL source address.  XXX is this necessary?
	 */
	val = (eaddr[2] << 24) | (eaddr[3] << 16) | (eaddr[4] << 8) | eaddr[5];
	CSR_WRITE_4(sc, ET_WOL_SA_LO, val);
	val = (eaddr[0] << 8) | eaddr[1];
	CSR_WRITE_4(sc, ET_WOL_SA_HI, val);

	/* Clear packet filters */
	CSR_WRITE_4(sc, ET_PKTFILT, 0);

	/* No ucast filtering */
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR1, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR2, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR3, 0);

	if (ET_FRAMELEN(ifp->if_mtu) > ET_RXMAC_CUT_THRU_FRMLEN) {
		/*
		 * In order to transmit jumbo packets greater than
		 * ET_RXMAC_CUT_THRU_FRMLEN bytes, the FIFO between
		 * RX MAC and RX DMA needs to be reduced in size to
		 * (ET_MEM_SIZE - ET_MEM_TXSIZE_EX - framelen).  In
		 * order to implement this, we must use "cut through"
		 * mode in the RX MAC, which chops packets down into
		 * segments.  In this case we selected 256 bytes,
		 * since this is the size of the PCI-Express TLP's
		 * that the ET1310 uses.
		 */
		val = __SHIFTIN(ET_RXMAC_SEGSZ(256), ET_RXMAC_MC_SEGSZ_MAX) |
		      ET_RXMAC_MC_SEGSZ_ENABLE;
	} else {
		val = 0;
	}
	CSR_WRITE_4(sc, ET_RXMAC_MC_SEGSZ, val);

	CSR_WRITE_4(sc, ET_RXMAC_MC_WATERMARK, 0);

	/* Initialize RX MAC management register */
	CSR_WRITE_4(sc, ET_RXMAC_MGT, 0);

	CSR_WRITE_4(sc, ET_RXMAC_SPACE_AVL, 0);

	CSR_WRITE_4(sc, ET_RXMAC_MGT,
		    ET_RXMAC_MGT_PASS_ECRC |
		    ET_RXMAC_MGT_PASS_ELEN |
		    ET_RXMAC_MGT_PASS_ETRUNC |
		    ET_RXMAC_MGT_CHECK_PKT);

	/*
	 * Configure runt filtering (may not work on certain chip generation)
	 */
	val = __SHIFTIN(ETHER_MIN_LEN, ET_PKTFILT_MINLEN) | ET_PKTFILT_FRAG;
	CSR_WRITE_4(sc, ET_PKTFILT, val);

	/* Enable RX MAC but leave WOL disabled */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL,
		    ET_RXMAC_CTRL_WOL_DISABLE | ET_RXMAC_CTRL_ENABLE);

	/*
	 * Setup multicast hash and allmulti/promisc mode
	 */
	et_setmulti(sc);
}

static void
et_init_txmac(struct et_softc *sc)
{
	/* Disable TX MAC and FC(?) */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL, ET_TXMAC_CTRL_FC_DISABLE);

	/* No flow control yet */
	CSR_WRITE_4(sc, ET_TXMAC_FLOWCTRL, 0);

	/* Enable TX MAC but leave FC(?) diabled */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL,
		    ET_TXMAC_CTRL_ENABLE | ET_TXMAC_CTRL_FC_DISABLE);
}

static int
et_start_rxdma(struct et_softc *sc)
{
	uint32_t val = 0;

	val |= __SHIFTIN(sc->sc_rx_data[0].rbd_bufsize,
			 ET_RXDMA_CTRL_RING0_SIZE) |
	       ET_RXDMA_CTRL_RING0_ENABLE;
	val |= __SHIFTIN(sc->sc_rx_data[1].rbd_bufsize,
			 ET_RXDMA_CTRL_RING1_SIZE) |
	       ET_RXDMA_CTRL_RING1_ENABLE;

	CSR_WRITE_4(sc, ET_RXDMA_CTRL, val);

	DELAY(5);

	if (CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) {
		if_printf(&sc->arpcom.ac_if, "can't start RX DMA engine\n");
		return ETIMEDOUT;
	}
	return 0;
}

static int
et_start_txdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_TXDMA_CTRL, ET_TXDMA_CTRL_SINGLE_EPKT);
	return 0;
}

static int
et_enable_txrx(struct et_softc *sc, int media_upd)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;
	int i, error;

	val = CSR_READ_4(sc, ET_MAC_CFG1);
	val |= ET_MAC_CFG1_TXEN | ET_MAC_CFG1_RXEN;
	val &= ~(ET_MAC_CFG1_TXFLOW | ET_MAC_CFG1_RXFLOW |
		 ET_MAC_CFG1_LOOPBACK);
	CSR_WRITE_4(sc, ET_MAC_CFG1, val);

	if (media_upd)
		et_ifmedia_upd(ifp);
	else
		et_setmedia(sc);

#define NRETRY	100

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MAC_CFG1);
		if ((val & (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN)) ==
		    (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN))
			break;

		DELAY(10);
	}
	if (i == NRETRY) {
		if_printf(ifp, "can't enable RX/TX\n");
		return 0;
	}
	sc->sc_flags |= ET_FLAG_TXRX_ENABLED;

#undef NRETRY

	/*
	 * Start TX/RX DMA engine
	 */
	error = et_start_rxdma(sc);
	if (error)
		return error;

	error = et_start_txdma(sc);
	if (error)
		return error;

	return 0;
}

static void
et_rxeof(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	uint32_t rxs_stat_ring;
	int rxst_wrap, rxst_index;

	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0)
		return;

	rxs_stat_ring = rxsd->rxsd_status->rxs_stat_ring;
	rxst_wrap = (rxs_stat_ring & ET_RXS_STATRING_WRAP) ? 1 : 0;
	rxst_index = __SHIFTOUT(rxs_stat_ring, ET_RXS_STATRING_INDEX);

	while (rxst_index != rxst_ring->rsr_index ||
	       rxst_wrap != rxst_ring->rsr_wrap) {
		struct et_rxbuf_data *rbd;
		struct et_rxdesc_ring *rx_ring;
		struct et_rxstat *st;
		struct mbuf *m;
		int buflen, buf_idx, ring_idx;
		uint32_t rxstat_pos, rxring_pos;

		KKASSERT(rxst_ring->rsr_index < ET_RX_NSTAT);
		st = &rxst_ring->rsr_stat[rxst_ring->rsr_index];

		buflen = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_LEN);
		buf_idx = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_BUFIDX);
		ring_idx = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_RINGIDX);

		if (++rxst_ring->rsr_index == ET_RX_NSTAT) {
			rxst_ring->rsr_index = 0;
			rxst_ring->rsr_wrap ^= 1;
		}
		rxstat_pos = __SHIFTIN(rxst_ring->rsr_index,
				       ET_RXSTAT_POS_INDEX);
		if (rxst_ring->rsr_wrap)
			rxstat_pos |= ET_RXSTAT_POS_WRAP;
		CSR_WRITE_4(sc, ET_RXSTAT_POS, rxstat_pos);

		if (ring_idx >= ET_RX_NRING) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			if_printf(ifp, "invalid ring index %d\n", ring_idx);
			continue;
		}
		if (buf_idx >= ET_RX_NDESC) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			if_printf(ifp, "invalid buf index %d\n", buf_idx);
			continue;
		}

		rbd = &sc->sc_rx_data[ring_idx];
		m = rbd->rbd_buf[buf_idx].rb_mbuf;

		if (rbd->rbd_newbuf(rbd, buf_idx, 0) == 0) {
			if (buflen < ETHER_CRC_LEN) {
				m_freem(m);
				IFNET_STAT_INC(ifp, ierrors, 1);
			} else {
				m->m_pkthdr.len = m->m_len = buflen;
				m->m_pkthdr.rcvif = ifp;

				m_adj(m, -ETHER_CRC_LEN);

				IFNET_STAT_INC(ifp, ipackets, 1);
				ifp->if_input(ifp, m, NULL, -1);
			}
		} else {
			IFNET_STAT_INC(ifp, ierrors, 1);
		}
		m = NULL;	/* Catch invalid reference */

		rx_ring = &sc->sc_rx_ring[ring_idx];

		if (buf_idx != rx_ring->rr_index) {
			if_printf(ifp, "WARNING!! ring %d, "
				  "buf_idx %d, rr_idx %d\n",
				  ring_idx, buf_idx, rx_ring->rr_index);
		}

		KKASSERT(rx_ring->rr_index < ET_RX_NDESC);
		if (++rx_ring->rr_index == ET_RX_NDESC) {
			rx_ring->rr_index = 0;
			rx_ring->rr_wrap ^= 1;
		}
		rxring_pos = __SHIFTIN(rx_ring->rr_index, ET_RX_RING_POS_INDEX);
		if (rx_ring->rr_wrap)
			rxring_pos |= ET_RX_RING_POS_WRAP;
		CSR_WRITE_4(sc, rx_ring->rr_posreg, rxring_pos);
	}
}

static int
et_encap(struct et_softc *sc, struct mbuf **m0)
{
	bus_dma_segment_t segs[ET_NSEG_MAX];
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	struct et_txdesc *td;
	bus_dmamap_t map;
	int error, maxsegs, nsegs, first_idx, last_idx, i;
	uint32_t tx_ready_pos, last_td_ctrl2;

	maxsegs = ET_TX_NDESC - tbd->tbd_used;
	if (maxsegs > ET_NSEG_MAX)
		maxsegs = ET_NSEG_MAX;
	KASSERT(maxsegs >= ET_NSEG_SPARE,
		("not enough spare TX desc (%d)", maxsegs));

	KKASSERT(tx_ring->tr_ready_index < ET_TX_NDESC);
	first_idx = tx_ring->tr_ready_index;
	map = tbd->tbd_buf[first_idx].tb_dmap;

	error = bus_dmamap_load_mbuf_defrag(sc->sc_txbuf_dtag, map, m0,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;
	bus_dmamap_sync(sc->sc_txbuf_dtag, map, BUS_DMASYNC_PREWRITE);

	last_td_ctrl2 = ET_TDCTRL2_LAST_FRAG;
	sc->sc_tx += nsegs;
	if (sc->sc_tx / sc->sc_tx_intr_nsegs != sc->sc_tx_intr) {
		sc->sc_tx_intr = sc->sc_tx / sc->sc_tx_intr_nsegs;
		last_td_ctrl2 |= ET_TDCTRL2_INTR;
	}

	last_idx = -1;
	for (i = 0; i < nsegs; ++i) {
		int idx;

		idx = (first_idx + i) % ET_TX_NDESC;
		td = &tx_ring->tr_desc[idx];
		td->td_addr_hi = ET_ADDR_HI(segs[i].ds_addr);
		td->td_addr_lo = ET_ADDR_LO(segs[i].ds_addr);
		td->td_ctrl1 = __SHIFTIN(segs[i].ds_len, ET_TDCTRL1_LEN);

		if (i == nsegs - 1) {	/* Last frag */
			td->td_ctrl2 = last_td_ctrl2;
			last_idx = idx;
		}

		KKASSERT(tx_ring->tr_ready_index < ET_TX_NDESC);
		if (++tx_ring->tr_ready_index == ET_TX_NDESC) {
			tx_ring->tr_ready_index = 0;
			tx_ring->tr_ready_wrap ^= 1;
		}
	}
	td = &tx_ring->tr_desc[first_idx];
	td->td_ctrl2 |= ET_TDCTRL2_FIRST_FRAG;	/* First frag */

	KKASSERT(last_idx >= 0);
	tbd->tbd_buf[first_idx].tb_dmap = tbd->tbd_buf[last_idx].tb_dmap;
	tbd->tbd_buf[last_idx].tb_dmap = map;
	tbd->tbd_buf[last_idx].tb_mbuf = *m0;

	tbd->tbd_used += nsegs;
	KKASSERT(tbd->tbd_used <= ET_TX_NDESC);

	tx_ready_pos = __SHIFTIN(tx_ring->tr_ready_index,
		       ET_TX_READY_POS_INDEX);
	if (tx_ring->tr_ready_wrap)
		tx_ready_pos |= ET_TX_READY_POS_WRAP;
	CSR_WRITE_4(sc, ET_TX_READY_POS, tx_ready_pos);

	error = 0;
back:
	if (error) {
		m_freem(*m0);
		*m0 = NULL;
	}
	return error;
}

static void
et_txeof(struct et_softc *sc, int start)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	uint32_t tx_done;
	int end, wrap;

	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0)
		return;

	if (tbd->tbd_used == 0)
		return;

	tx_done = CSR_READ_4(sc, ET_TX_DONE_POS);
	end = __SHIFTOUT(tx_done, ET_TX_DONE_POS_INDEX);
	wrap = (tx_done & ET_TX_DONE_POS_WRAP) ? 1 : 0;

	while (tbd->tbd_start_index != end || tbd->tbd_start_wrap != wrap) {
		struct et_txbuf *tb;

		KKASSERT(tbd->tbd_start_index < ET_TX_NDESC);
		tb = &tbd->tbd_buf[tbd->tbd_start_index];

		bzero(&tx_ring->tr_desc[tbd->tbd_start_index],
		      sizeof(struct et_txdesc));

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_txbuf_dtag, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
			IFNET_STAT_INC(ifp, opackets, 1);
		}

		if (++tbd->tbd_start_index == ET_TX_NDESC) {
			tbd->tbd_start_index = 0;
			tbd->tbd_start_wrap ^= 1;
		}

		KKASSERT(tbd->tbd_used > 0);
		tbd->tbd_used--;
	}

	if (tbd->tbd_used == 0)
		ifp->if_timer = 0;
	if (tbd->tbd_used + ET_NSEG_SPARE <= ET_TX_NDESC)
		ifq_clr_oactive(&ifp->if_snd);

	if (start)
		if_devstart(ifp);
}

static void
et_tick(void *xsc)
{
	struct et_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->sc_miibus);

	lwkt_serialize_enter(ifp->if_serializer);

	mii_tick(mii);
	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0 &&
	    (mii->mii_media_status & IFM_ACTIVE) &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		if_printf(ifp, "Link up, enable TX/RX\n");
		if (et_enable_txrx(sc, 0) == 0)
			if_devstart(ifp);
	}
	callout_reset(&sc->sc_tick, hz, et_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static int
et_newbuf_cluster(struct et_rxbuf_data *rbd, int buf_idx, int init)
{
	return et_newbuf(rbd, buf_idx, init, MCLBYTES);
}

static int
et_newbuf_hdr(struct et_rxbuf_data *rbd, int buf_idx, int init)
{
	return et_newbuf(rbd, buf_idx, init, MHLEN);
}

static int
et_newbuf(struct et_rxbuf_data *rbd, int buf_idx, int init, int len0)
{
	struct et_softc *sc = rbd->rbd_softc;
	struct et_rxbuf *rb;
	struct mbuf *m;
	bus_dma_segment_t seg;
	bus_dmamap_t dmap;
	int error, len, nseg;

	KASSERT(!rbd->rbd_jumbo, ("calling %s with jumbo ring", __func__));

	KKASSERT(buf_idx < ET_RX_NDESC);
	rb = &rbd->rbd_buf[buf_idx];

	m = m_getl(len0, init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR, &len);
	if (m == NULL) {
		error = ENOBUFS;

		if (init) {
			if_printf(&sc->arpcom.ac_if,
				  "m_getl failed, size %d\n", len0);
			return error;
		} else {
			goto back;
		}
	}
	m->m_len = m->m_pkthdr.len = len;

	/*
	 * Try load RX mbuf into temporary DMA tag
	 */
	error = bus_dmamap_load_mbuf_segment(sc->sc_rxbuf_dtag,
			sc->sc_rxbuf_tmp_dmap, m, &seg, 1, &nseg,
			BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init) {
			if_printf(&sc->arpcom.ac_if, "can't load RX mbuf\n");
			return error;
		} else {
			goto back;
		}
	}

	if (!init) {
		bus_dmamap_sync(sc->sc_rxbuf_dtag, rb->rb_dmap,
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rxbuf_dtag, rb->rb_dmap);
	}
	rb->rb_mbuf = m;
	rb->rb_paddr = seg.ds_addr;

	/*
	 * Swap RX buf's DMA map with the loaded temporary one
	 */
	dmap = rb->rb_dmap;
	rb->rb_dmap = sc->sc_rxbuf_tmp_dmap;
	sc->sc_rxbuf_tmp_dmap = dmap;

	error = 0;
back:
	et_setup_rxdesc(rbd, buf_idx, rb->rb_paddr);
	return error;
}

static int
et_sysctl_rx_intr_npkts(SYSCTL_HANDLER_ARGS)
{
	struct et_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_rx_intr_npkts;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_rx_intr_npkts != v) {
		if (ifp->if_flags & IFF_RUNNING)
			CSR_WRITE_4(sc, ET_RX_INTR_NPKTS, v);
		sc->sc_rx_intr_npkts = v;
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
et_sysctl_rx_intr_delay(SYSCTL_HANDLER_ARGS)
{
	struct et_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->sc_rx_intr_delay;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_rx_intr_delay != v) {
		if (ifp->if_flags & IFF_RUNNING)
			CSR_WRITE_4(sc, ET_RX_INTR_DELAY, v);
		sc->sc_rx_intr_delay = v;
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
et_setmedia(struct et_softc *sc)
{
	struct mii_data *mii = device_get_softc(sc->sc_miibus);
	uint32_t cfg2, ctrl;

	cfg2 = CSR_READ_4(sc, ET_MAC_CFG2);
	cfg2 &= ~(ET_MAC_CFG2_MODE_MII | ET_MAC_CFG2_MODE_GMII |
		  ET_MAC_CFG2_FDX | ET_MAC_CFG2_BIGFRM);
	cfg2 |= ET_MAC_CFG2_LENCHK | ET_MAC_CFG2_CRC | ET_MAC_CFG2_PADCRC |
		__SHIFTIN(7, ET_MAC_CFG2_PREAMBLE_LEN);

	ctrl = CSR_READ_4(sc, ET_MAC_CTRL);
	ctrl &= ~(ET_MAC_CTRL_GHDX | ET_MAC_CTRL_MODE_MII);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		cfg2 |= ET_MAC_CFG2_MODE_GMII;
	} else {
		cfg2 |= ET_MAC_CFG2_MODE_MII;
		ctrl |= ET_MAC_CTRL_MODE_MII;
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		cfg2 |= ET_MAC_CFG2_FDX;
	else
		ctrl |= ET_MAC_CTRL_GHDX;

	CSR_WRITE_4(sc, ET_MAC_CTRL, ctrl);
	CSR_WRITE_4(sc, ET_MAC_CFG2, cfg2);
}

static int
et_jumbo_mem_alloc(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_jumbo_data *jd = &sc->sc_jumbo_data;
	bus_addr_t paddr;
	uint8_t *buf;
	int i;

	jd->jd_buf = bus_dmamem_coherent_any(sc->sc_dtag,
			ET_JUMBO_ALIGN, ET_JUMBO_MEM_SIZE, BUS_DMA_WAITOK,
			&jd->jd_dtag, &jd->jd_dmap, &paddr);
	if (jd->jd_buf == NULL) {
		device_printf(dev, "can't create jumbo DMA stuffs\n");
		return ENOMEM;
	}

	jd->jd_slots = kmalloc(sizeof(*jd->jd_slots) * ET_JSLOTS, M_DEVBUF,
			       M_WAITOK | M_ZERO);
	lwkt_serialize_init(&jd->jd_serializer);
	SLIST_INIT(&jd->jd_free_slots);

	buf = jd->jd_buf;
	for (i = 0; i < ET_JSLOTS; ++i) {
		struct et_jslot *jslot = &jd->jd_slots[i];

		jslot->jslot_data = jd;
		jslot->jslot_buf = buf;
		jslot->jslot_paddr = paddr;
		jslot->jslot_inuse = 0;
		jslot->jslot_index = i;
		SLIST_INSERT_HEAD(&jd->jd_free_slots, jslot, jslot_link);

		buf += ET_JLEN;
		paddr += ET_JLEN;
	}
	return 0;
}

static void
et_jumbo_mem_free(device_t dev)
{
	struct et_softc *sc = device_get_softc(dev);
	struct et_jumbo_data *jd = &sc->sc_jumbo_data;

	KKASSERT(sc->sc_flags & ET_FLAG_JUMBO);

	kfree(jd->jd_slots, M_DEVBUF);
	et_dma_mem_destroy(jd->jd_dtag, jd->jd_buf, jd->jd_dmap);
}

static struct et_jslot *
et_jalloc(struct et_jumbo_data *jd)
{
	struct et_jslot *jslot;

	lwkt_serialize_enter(&jd->jd_serializer);

	jslot = SLIST_FIRST(&jd->jd_free_slots);
	if (jslot) {
		SLIST_REMOVE_HEAD(&jd->jd_free_slots, jslot_link);
		jslot->jslot_inuse = 1;
	}

	lwkt_serialize_exit(&jd->jd_serializer);
	return jslot;
}

static void
et_jfree(void *xjslot)
{
	struct et_jslot *jslot = xjslot;
	struct et_jumbo_data *jd = jslot->jslot_data;

	if (&jd->jd_slots[jslot->jslot_index] != jslot) {
		panic("%s wrong jslot!?", __func__);
	} else if (jslot->jslot_inuse == 0) {
		panic("%s jslot already freed", __func__);
	} else {
		lwkt_serialize_enter(&jd->jd_serializer);

		atomic_subtract_int(&jslot->jslot_inuse, 1);
		if (jslot->jslot_inuse == 0) {
			SLIST_INSERT_HEAD(&jd->jd_free_slots, jslot,
					  jslot_link);
		}

		lwkt_serialize_exit(&jd->jd_serializer);
	}
}

static void
et_jref(void *xjslot)
{
	struct et_jslot *jslot = xjslot;
	struct et_jumbo_data *jd = jslot->jslot_data;

	if (&jd->jd_slots[jslot->jslot_index] != jslot)
		panic("%s wrong jslot!?", __func__);
	else if (jslot->jslot_inuse == 0)
		panic("%s jslot already freed", __func__);
	else
		atomic_add_int(&jslot->jslot_inuse, 1);
}

static int
et_newbuf_jumbo(struct et_rxbuf_data *rbd, int buf_idx, int init)
{
	struct et_softc *sc = rbd->rbd_softc;
	struct et_rxbuf *rb;
	struct mbuf *m;
	struct et_jslot *jslot;
	int error;

	KASSERT(rbd->rbd_jumbo, ("calling %s with non-jumbo ring", __func__));

	KKASSERT(buf_idx < ET_RX_NDESC);
	rb = &rbd->rbd_buf[buf_idx];

	error = ENOBUFS;

	MGETHDR(m, init ? M_WAITOK : M_NOWAIT, MT_DATA);
	if (m == NULL) {
		if (init) {
			if_printf(&sc->arpcom.ac_if, "MGETHDR failed\n");
			return error;
		} else {
			goto back;
		}
	}

	jslot = et_jalloc(&sc->sc_jumbo_data);
	if (jslot == NULL) {
		m_freem(m);

		if (init) {
			if_printf(&sc->arpcom.ac_if,
				  "jslot allocation failed\n");
			return error;
		} else {
			goto back;
		}
	}

	m->m_ext.ext_arg = jslot;
	m->m_ext.ext_buf = jslot->jslot_buf;
	m->m_ext.ext_free = et_jfree;
	m->m_ext.ext_ref = et_jref;
	m->m_ext.ext_size = ET_JUMBO_FRAMELEN;
	m->m_flags |= M_EXT;
	m->m_data = m->m_ext.ext_buf;
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	rb->rb_mbuf = m;
	rb->rb_paddr = jslot->jslot_paddr;

	error = 0;
back:
	et_setup_rxdesc(rbd, buf_idx, rb->rb_paddr);
	return error;
}

static void
et_setup_rxdesc(struct et_rxbuf_data *rbd, int buf_idx, bus_addr_t paddr)
{
	struct et_rxdesc_ring *rx_ring = rbd->rbd_ring;
	struct et_rxdesc *desc;

	KKASSERT(buf_idx < ET_RX_NDESC);
	desc = &rx_ring->rr_desc[buf_idx];

	desc->rd_addr_hi = ET_ADDR_HI(paddr);
	desc->rd_addr_lo = ET_ADDR_LO(paddr);
	desc->rd_ctrl = __SHIFTIN(buf_idx, ET_RDCTRL_BUFIDX);
}
