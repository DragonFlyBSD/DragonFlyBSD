/*-
 * Copyright (c) 2008 Stanislav Sedov <stas@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Driver for Attansic Technology Corp. L2 FastEthernet adapter.
 *
 * This driver is heavily based on age(4) Attansic L1 driver by Pyun YongHyeon.
 *
 * $FreeBSD: src/sys/dev/ae/if_ae.c,v 1.1.2.3.2.1 2009/04/15 03:14:26 kensmith Exp $
 */

#include <sys/param.h>
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
#include <net/vlan/if_vlan_ether.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#include <dev/netif/mii_layer/miivar.h>

#include <dev/netif/ae/if_aereg.h>
#include <dev/netif/ae/if_aevar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Devices supported by this driver.
 */
static const struct ae_dev {
	uint16_t	ae_vendorid;
	uint16_t	ae_deviceid;
	const char	*ae_name;
} ae_devs[] = {
        { VENDORID_ATTANSIC, DEVICEID_ATTANSIC_L2,
            "Attansic Technology Corp, L2 Fast Ethernet" },
	/* Required last entry */
	{ 0, 0, NULL }
};


static int	ae_probe(device_t);
static int	ae_attach(device_t);
static int	ae_detach(device_t);
static int	ae_shutdown(device_t);
static int	ae_suspend(device_t);
static int	ae_resume(device_t);
static int	ae_miibus_readreg(device_t, int, int);
static int	ae_miibus_writereg(device_t, int, int, int);
static void	ae_miibus_statchg(device_t);

static int	ae_mediachange(struct ifnet *);
static void	ae_mediastatus(struct ifnet *, struct ifmediareq *);
static void	ae_init(void *);
static void	ae_start(struct ifnet *, struct ifaltq_subque *);
static int	ae_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	ae_watchdog(struct ifnet *);
static void	ae_stop(struct ae_softc *);
static void	ae_tick(void *);

static void	ae_intr(void *);
static void	ae_tx_intr(struct ae_softc *);
static void	ae_rx_intr(struct ae_softc *);
static int	ae_rxeof(struct ae_softc *, struct ae_rxd *);

static int	ae_encap(struct ae_softc *, struct mbuf **);
static void	ae_sysctl_node(struct ae_softc *);
static void	ae_phy_reset(struct ae_softc *);
static int	ae_reset(struct ae_softc *);
static void	ae_pcie_init(struct ae_softc *);
static void	ae_get_eaddr(struct ae_softc *);
static void	ae_dma_free(struct ae_softc *);
static int	ae_dma_alloc(struct ae_softc *);
static void	ae_mac_config(struct ae_softc *);
static void	ae_stop_rxmac(struct ae_softc *);
static void	ae_stop_txmac(struct ae_softc *);
static void	ae_rxfilter(struct ae_softc *);
static void	ae_rxvlan(struct ae_softc *);
static void	ae_update_stats_rx(uint16_t, struct ae_stats *);
static void	ae_update_stats_tx(uint16_t, struct ae_stats *);
static void	ae_powersave_disable(struct ae_softc *);
static void	ae_powersave_enable(struct ae_softc *);

static device_method_t ae_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		ae_probe),
	DEVMETHOD(device_attach,	ae_attach),
	DEVMETHOD(device_detach,	ae_detach),
	DEVMETHOD(device_shutdown,	ae_shutdown),
	DEVMETHOD(device_suspend,	ae_suspend),
	DEVMETHOD(device_resume,	ae_resume),
	
	/* Bus interface. */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	
	/* MII interface. */
	DEVMETHOD(miibus_readreg,	ae_miibus_readreg),
	DEVMETHOD(miibus_writereg,	ae_miibus_writereg),
	DEVMETHOD(miibus_statchg,	ae_miibus_statchg),
	{ NULL, NULL }
};

static driver_t ae_driver = {
	"ae",
	ae_methods,
	sizeof(struct ae_softc)
};

static devclass_t ae_devclass;
DECLARE_DUMMY_MODULE(if_ae);
MODULE_DEPEND(if_ae, miibus, 1, 1, 1);
DRIVER_MODULE(if_ae, pci, ae_driver, ae_devclass, NULL, NULL);
DRIVER_MODULE(miibus, ae, miibus_driver, miibus_devclass, NULL, NULL);

/* Register access macros. */
#define AE_WRITE_4(_sc, reg, val)	\
	bus_space_write_4((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg), (val))
#define AE_WRITE_2(_sc, reg, val)	\
	bus_space_write_2((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg), (val))
#define AE_WRITE_1(_sc, reg, val)	\
	bus_space_write_1((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg), (val))
#define AE_READ_4(_sc, reg)		\
	bus_space_read_4((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg))
#define AE_READ_2(_sc, reg)		\
	bus_space_read_2((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg))
#define AE_READ_1(_sc, reg)		\
	bus_space_read_1((_sc)->ae_mem_bt, (_sc)->ae_mem_bh, (reg))

#define AE_PHY_READ(sc, reg)		\
	ae_miibus_readreg(sc->ae_dev, 0, reg)
#define AE_PHY_WRITE(sc, reg, val)	\
	ae_miibus_writereg(sc->ae_dev, 0, reg, val)
#define AE_CHECK_EADDR_VALID(eaddr)	\
	((eaddr[0] == 0 && eaddr[1] == 0) || \
	 (eaddr[0] == 0xffffffff && eaddr[1] == 0xffff))
#define AE_RXD_VLAN(vtag) \
	(((vtag) >> 4) | (((vtag) & 0x07) << 13) | (((vtag) & 0x08) << 9))
#define AE_TXD_VLAN(vtag) \
	(((vtag) << 4) | (((vtag) >> 13) & 0x07) | (((vtag) >> 9) & 0x08))

/*
 * ae statistics.
 */
#define STATS_ENTRY(node, desc, field) \
	{ node, desc, offsetof(struct ae_stats, field) }
struct {
	const char	*node;
	const char	*desc;
	intptr_t	offset;
} ae_stats_tx[] = {
	STATS_ENTRY("bcast", "broadcast frames", tx_bcast),
	STATS_ENTRY("mcast", "multicast frames", tx_mcast),
	STATS_ENTRY("pause", "PAUSE frames", tx_pause),
	STATS_ENTRY("control", "control frames", tx_ctrl),
	STATS_ENTRY("defers", "deferrals occuried", tx_defer),
	STATS_ENTRY("exc_defers", "excessive deferrals occuried", tx_excdefer),
	STATS_ENTRY("singlecols", "single collisions occuried", tx_singlecol),
	STATS_ENTRY("multicols", "multiple collisions occuried", tx_multicol),
	STATS_ENTRY("latecols", "late collisions occuried", tx_latecol),
	STATS_ENTRY("aborts", "transmit aborts due collisions", tx_abortcol),
	STATS_ENTRY("underruns", "Tx FIFO underruns", tx_underrun)
}, ae_stats_rx[] = {
	STATS_ENTRY("bcast", "broadcast frames", rx_bcast),
	STATS_ENTRY("mcast", "multicast frames", rx_mcast),
	STATS_ENTRY("pause", "PAUSE frames", rx_pause),
	STATS_ENTRY("control", "control frames", rx_ctrl),
	STATS_ENTRY("crc_errors", "frames with CRC errors", rx_crcerr),
	STATS_ENTRY("code_errors", "frames with invalid opcode", rx_codeerr),
	STATS_ENTRY("runt", "runt frames", rx_runt),
	STATS_ENTRY("frag", "fragmented frames", rx_frag),
	STATS_ENTRY("align_errors", "frames with alignment errors", rx_align),
	STATS_ENTRY("truncated", "frames truncated due to Rx FIFO inderrun",
	    rx_trunc)
};
#define AE_STATS_RX_LEN NELEM(ae_stats_rx)
#define AE_STATS_TX_LEN NELEM(ae_stats_tx)

static void
ae_stop(struct ae_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	sc->ae_flags &= ~AE_FLAG_LINK;
	callout_stop(&sc->ae_tick_ch);

	/*
	 * Clear and disable interrupts.
	 */
	AE_WRITE_4(sc, AE_IMR_REG, 0);
	AE_WRITE_4(sc, AE_ISR_REG, 0xffffffff);

	/*
	 * Stop Rx/Tx MACs.
	 */
	ae_stop_txmac(sc);
	ae_stop_rxmac(sc);

	/*
	 * Stop DMA engines.
	 */
	AE_WRITE_1(sc, AE_DMAREAD_REG, ~AE_DMAREAD_EN);
	AE_WRITE_1(sc, AE_DMAWRITE_REG, ~AE_DMAWRITE_EN);

	/*
	 * Wait for everything to enter idle state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		if (AE_READ_4(sc, AE_IDLE_REG) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT)
		if_printf(ifp, "could not enter idle state in stop.\n");
}

static void
ae_stop_rxmac(struct ae_softc *sc)
{
	uint32_t val;
	int i;

	/*
	 * Stop Rx MAC engine.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	if ((val & AE_MAC_RX_EN) != 0) {
		val &= ~AE_MAC_RX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}

	/*
	 * Stop Rx DMA engine.
	 */
	if (AE_READ_1(sc, AE_DMAWRITE_REG) == AE_DMAWRITE_EN)
		AE_WRITE_1(sc, AE_DMAWRITE_REG, 0);

	/*
	 * Wait for IDLE state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i--) {
		val = AE_READ_4(sc, AE_IDLE_REG);
		if ((val & (AE_IDLE_RXMAC | AE_IDLE_DMAWRITE)) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "timed out while stopping Rx MAC.\n");
	}
}

static void
ae_stop_txmac(struct ae_softc *sc)
{
	uint32_t val;
	int i;

	/*
	 * Stop Tx MAC engine.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	if ((val & AE_MAC_TX_EN) != 0) {
		val &= ~AE_MAC_TX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}

	/*
	 * Stop Tx DMA engine.
	 */
	if (AE_READ_1(sc, AE_DMAREAD_REG) == AE_DMAREAD_EN)
		AE_WRITE_1(sc, AE_DMAREAD_REG, 0);

	/*
	 * Wait for IDLE state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i--) {
		val = AE_READ_4(sc, AE_IDLE_REG);
		if ((val & (AE_IDLE_TXMAC | AE_IDLE_DMAREAD)) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "timed out while stopping Tx MAC.\n");
	}
}

/*
 * Callback from MII layer when media changes.
 */
static void
ae_miibus_statchg(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	uint32_t val;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	mii = device_get_softc(sc->ae_miibus);
	sc->ae_flags &= ~AE_FLAG_LINK;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->ae_flags |= AE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Stop Rx/Tx MACs. */
	ae_stop_rxmac(sc);
	ae_stop_txmac(sc);

	/* Program MACs with resolved speed/duplex/flow-control. */
	if ((sc->ae_flags & AE_FLAG_LINK) != 0) {
		ae_mac_config(sc);

		/*
		 * Restart DMA engines.
		 */
		AE_WRITE_1(sc, AE_DMAREAD_REG, AE_DMAREAD_EN);
		AE_WRITE_1(sc, AE_DMAWRITE_REG, AE_DMAWRITE_EN);

		/*
		 * Enable Rx and Tx MACs.
		 */
		val = AE_READ_4(sc, AE_MAC_REG);
		val |= AE_MAC_TX_EN | AE_MAC_RX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}
}

static void
ae_sysctl_node(struct ae_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *root, *stats, *stats_rx, *stats_tx;
	struct ae_stats *ae_stats;
	unsigned int i;

	ae_stats = &sc->stats;

	ctx = device_get_sysctl_ctx(sc->ae_dev);
	root = device_get_sysctl_tree(sc->ae_dev);
	stats = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(root), OID_AUTO, "stats",
	    CTLFLAG_RD, NULL, "ae statistics");
	if (stats == NULL) {
		device_printf(sc->ae_dev, "can't add stats sysctl node\n");
		return;
	}

	/*
	 * Receiver statistcics.
	 */
	stats_rx = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(stats), OID_AUTO, "rx",
	    CTLFLAG_RD, NULL, "Rx MAC statistics");
	if (stats_rx != NULL) {
		for (i = 0; i < AE_STATS_RX_LEN; i++) {
			SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(stats_rx),
			    OID_AUTO, ae_stats_rx[i].node, CTLFLAG_RD,
			    (char *)ae_stats + ae_stats_rx[i].offset, 0,
			    ae_stats_rx[i].desc);
		}
	}

	/*
	 * Transmitter statistcics.
	 */
	stats_tx = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(stats), OID_AUTO, "tx",
	    CTLFLAG_RD, NULL, "Tx MAC statistics");
	if (stats_tx != NULL) {
		for (i = 0; i < AE_STATS_TX_LEN; i++) {
			SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(stats_tx),
			    OID_AUTO, ae_stats_tx[i].node, CTLFLAG_RD,
			    (char *)ae_stats + ae_stats_tx[i].offset, 0,
			    ae_stats_tx[i].desc);
		}
	}
}

static int
ae_miibus_readreg(device_t dev, int phy, int reg)
{
	struct ae_softc *sc = device_get_softc(dev);
	uint32_t val;
	int i;

	/*
	 * Locking is done in upper layers.
	 */
	if (phy != sc->ae_phyaddr)
		return (0);
	val = ((reg << AE_MDIO_REGADDR_SHIFT) & AE_MDIO_REGADDR_MASK) |
	    AE_MDIO_START | AE_MDIO_READ | AE_MDIO_SUP_PREAMBLE |
	    ((AE_MDIO_CLK_25_4 << AE_MDIO_CLK_SHIFT) & AE_MDIO_CLK_MASK);
	AE_WRITE_4(sc, AE_MDIO_REG, val);

	/*
	 * Wait for operation to complete.
	 */
	for (i = 0; i < AE_MDIO_TIMEOUT; i++) {
		DELAY(2);
		val = AE_READ_4(sc, AE_MDIO_REG);
		if ((val & (AE_MDIO_START | AE_MDIO_BUSY)) == 0)
			break;
	}
	if (i == AE_MDIO_TIMEOUT) {
		device_printf(sc->ae_dev, "phy read timeout: %d.\n", reg);
		return (0);
	}
	return ((val << AE_MDIO_DATA_SHIFT) & AE_MDIO_DATA_MASK);
}

static int
ae_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct ae_softc *sc = device_get_softc(dev);
	uint32_t aereg;
	int i;

	/*
	 * Locking is done in upper layers.
	 */
	if (phy != sc->ae_phyaddr)
		return (0);
	aereg = ((reg << AE_MDIO_REGADDR_SHIFT) & AE_MDIO_REGADDR_MASK) |
	    AE_MDIO_START | AE_MDIO_SUP_PREAMBLE |
	    ((AE_MDIO_CLK_25_4 << AE_MDIO_CLK_SHIFT) & AE_MDIO_CLK_MASK) |
	    ((val << AE_MDIO_DATA_SHIFT) & AE_MDIO_DATA_MASK);
	AE_WRITE_4(sc, AE_MDIO_REG, aereg);

	/*
	 * Wait for operation to complete.
	 */
	for (i = 0; i < AE_MDIO_TIMEOUT; i++) {
		DELAY(2);
		aereg = AE_READ_4(sc, AE_MDIO_REG);
		if ((aereg & (AE_MDIO_START | AE_MDIO_BUSY)) == 0)
			break;
	}
	if (i == AE_MDIO_TIMEOUT)
		device_printf(sc->ae_dev, "phy write timeout: %d.\n", reg);
	return (0);
}

static int
ae_probe(device_t dev)
{
	uint16_t vendor, devid;
	const struct ae_dev *sp;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	for (sp = ae_devs; sp->ae_name != NULL; sp++) {
		if (vendor == sp->ae_vendorid &&
		    devid == sp->ae_deviceid) {
			device_set_desc(dev, sp->ae_name);
			return (0);
		}
	}
	return (ENXIO);
}

static int
ae_dma_alloc(struct ae_softc *sc)
{
	bus_addr_t busaddr;
	int error;

	/*
	 * Create parent DMA tag.
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->dma_parent_tag);
	if (error) {
		device_printf(sc->ae_dev, "could not creare parent DMA tag.\n");
		return (error);
	}

	/*
	 * Create DMA stuffs for TxD.
	 */
	sc->txd_base = bus_dmamem_coherent_any(sc->dma_parent_tag, 4,
			AE_TXD_BUFSIZE_DEFAULT, BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&sc->dma_txd_tag, &sc->dma_txd_map,
			&sc->dma_txd_busaddr);
	if (sc->txd_base == NULL) {
		device_printf(sc->ae_dev, "could not creare TxD DMA stuffs.\n");
		return ENOMEM;
	}

	/*
	 * Create DMA stuffs for TxS.
	 */
	sc->txs_base = bus_dmamem_coherent_any(sc->dma_parent_tag, 4,
			AE_TXS_COUNT_DEFAULT * 4, BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&sc->dma_txs_tag, &sc->dma_txs_map,
			&sc->dma_txs_busaddr);
	if (sc->txs_base == NULL) {
		device_printf(sc->ae_dev, "could not creare TxS DMA stuffs.\n");
		return ENOMEM;
	}

	/*
	 * Create DMA stuffs for RxD.
	 */
	sc->rxd_base_dma = bus_dmamem_coherent_any(sc->dma_parent_tag, 128,
				AE_RXD_COUNT_DEFAULT * 1536 + 120,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&sc->dma_rxd_tag, &sc->dma_rxd_map,
				&busaddr);
	if (sc->rxd_base_dma == NULL) {
		device_printf(sc->ae_dev, "could not creare RxD DMA stuffs.\n");
		return ENOMEM;
	}
	sc->dma_rxd_busaddr = busaddr + 120;
	sc->rxd_base = (struct ae_rxd *)(sc->rxd_base_dma + 120);

	return (0);
}

static void
ae_mac_config(struct ae_softc *sc)
{
	struct mii_data *mii;
	uint32_t val;

	mii = device_get_softc(sc->ae_miibus);
	val = AE_READ_4(sc, AE_MAC_REG);
	val &= ~AE_MAC_FULL_DUPLEX;
	/* XXX disable AE_MAC_TX_FLOW_EN? */
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		val |= AE_MAC_FULL_DUPLEX;
	AE_WRITE_4(sc, AE_MAC_REG, val);
}

static int
ae_rxeof(struct ae_softc *sc, struct ae_rxd *rxd)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;
	unsigned int size;
	uint16_t flags;

	flags = le16toh(rxd->flags);
#ifdef AE_DEBUG
	if_printf(ifp, "Rx interrupt occuried.\n");
#endif
	size = le16toh(rxd->len) - ETHER_CRC_LEN;
	if (size < (ETHER_MIN_LEN - ETHER_CRC_LEN -
		    sizeof(struct ether_vlan_header))) {
		if_printf(ifp, "Runt frame received.");
		return (EIO);
	}

	m = m_devget(&rxd->data[0], size, ETHER_ALIGN, ifp);
	if (m == NULL)
		return (ENOBUFS);

	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) &&
	    (flags & AE_RXD_HAS_VLAN)) {
		m->m_pkthdr.ether_vlantag = AE_RXD_VLAN(le16toh(rxd->vlan));
		m->m_flags |= M_VLANTAG;
	}
	ifp->if_input(ifp, m, NULL, -1);

	return (0);
}

static void
ae_rx_intr(struct ae_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ae_rxd *rxd;
	uint16_t flags;
	int error;

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_rxd_tag, sc->dma_rxd_map,
			BUS_DMASYNC_POSTREAD);
	for (;;) {
		rxd = (struct ae_rxd *)(sc->rxd_base + sc->rxd_cur);

		flags = le16toh(rxd->flags);
		if ((flags & AE_RXD_UPDATE) == 0)
			break;
		rxd->flags = htole16(flags & ~AE_RXD_UPDATE);

		/* Update stats. */
		ae_update_stats_rx(flags, &sc->stats);

		/*
		 * Update position index.
		 */
		sc->rxd_cur = (sc->rxd_cur + 1) % AE_RXD_COUNT_DEFAULT;
		if ((flags & AE_RXD_SUCCESS) == 0) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}

		error = ae_rxeof(sc, rxd);
		if (error)
			IFNET_STAT_INC(ifp, ierrors, 1);
		else
			IFNET_STAT_INC(ifp, ipackets, 1);
	}

	/* Update Rx index. */
	AE_WRITE_2(sc, AE_MB_RXD_IDX_REG, sc->rxd_cur);
}

static void
ae_tx_intr(struct ae_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ae_txd *txd;
	struct ae_txs *txs;
	uint16_t flags;

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map, BUS_DMASYNC_POSTREAD);

	for (;;) {
		txs = sc->txs_base + sc->txs_ack;

		flags = le16toh(txs->flags);
		if ((flags & AE_TXS_UPDATE) == 0)
			break;
		txs->flags = htole16(flags & ~AE_TXS_UPDATE);

		/* Update stats. */
		ae_update_stats_tx(flags, &sc->stats);

		/*
		 * Update TxS position.
		 */
		sc->txs_ack = (sc->txs_ack + 1) % AE_TXS_COUNT_DEFAULT;
		sc->ae_flags |= AE_FLAG_TXAVAIL;
		txd = (struct ae_txd *)(sc->txd_base + sc->txd_ack);
		if (txs->len != txd->len) {
			device_printf(sc->ae_dev, "Size mismatch: "
				"TxS:%d TxD:%d\n",
				le16toh(txs->len), le16toh(txd->len));
		}

		/*
		 * Move txd ack and align on 4-byte boundary.
		 */
		sc->txd_ack = ((sc->txd_ack + le16toh(txd->len) + 4 + 3) & ~3) %
		    AE_TXD_BUFSIZE_DEFAULT;
		if ((flags & AE_TXS_SUCCESS) != 0)
			IFNET_STAT_INC(ifp, opackets, 1);
		else
			IFNET_STAT_INC(ifp, oerrors, 1);
		sc->tx_inproc--;
	}

	if (sc->tx_inproc < 0) {
		/* XXX assert? */
		if_printf(ifp, "Received stray Tx interrupt(s).\n");
		sc->tx_inproc = 0;
	}
	if (sc->tx_inproc == 0)
		ifp->if_timer = 0;	/* Unarm watchdog. */
	if (sc->ae_flags & AE_FLAG_TXAVAIL) {
		ifq_clr_oactive(&ifp->if_snd);
		if (!ifq_is_empty(&ifp->if_snd))
#ifdef foo
			ae_intr(sc);
#else
			if_devstart(ifp);
#endif
	}

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map, BUS_DMASYNC_PREWRITE);
}

static void
ae_intr(void *xsc)
{
	struct ae_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;

	ASSERT_SERIALIZED(ifp->if_serializer);

	val = AE_READ_4(sc, AE_ISR_REG);
	if (val == 0 || (val & AE_IMR_DEFAULT) == 0)
		return;

#ifdef foo
	AE_WRITE_4(sc, AE_ISR_REG, AE_ISR_DISABLE);
#endif

	/* Read interrupt status. */
	val = AE_READ_4(sc, AE_ISR_REG);

	/* Clear interrupts and disable them. */
	AE_WRITE_4(sc, AE_ISR_REG, val | AE_ISR_DISABLE);

	if (ifp->if_flags & IFF_RUNNING) {
		if (val & (AE_ISR_DMAR_TIMEOUT |
			   AE_ISR_DMAW_TIMEOUT |
			   AE_ISR_PHY_LINKDOWN)) {
			ae_init(sc);
		}
		if (val & AE_ISR_TX_EVENT)
			ae_tx_intr(sc);
		if (val & AE_ISR_RX_EVENT)
			ae_rx_intr(sc);
	}

	/* Re-enable interrupts. */
	AE_WRITE_4(sc, AE_ISR_REG, 0);
}

static void
ae_init(void *xsc)
{
	struct ae_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint32_t val;
	bus_addr_t addr;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->ae_miibus);
	ae_stop(sc);
	ae_reset(sc);
	ae_pcie_init(sc);
	ae_powersave_disable(sc);

	/*
	 * Clear and disable interrupts.
	 */
	AE_WRITE_4(sc, AE_ISR_REG, 0xffffffff);

	/*
	 * Set the MAC address.
	 */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	val = eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5];
	AE_WRITE_4(sc, AE_EADDR0_REG, val);
	val = eaddr[0] << 8 | eaddr[1];
	AE_WRITE_4(sc, AE_EADDR1_REG, val);

	/*
	 * Set ring buffers base addresses.
	 */
	addr = sc->dma_rxd_busaddr;
	AE_WRITE_4(sc, AE_DESC_ADDR_HI_REG, BUS_ADDR_HI(addr));
	AE_WRITE_4(sc, AE_RXD_ADDR_LO_REG, BUS_ADDR_LO(addr));
	addr = sc->dma_txd_busaddr;
	AE_WRITE_4(sc, AE_TXD_ADDR_LO_REG, BUS_ADDR_LO(addr));
	addr = sc->dma_txs_busaddr;
	AE_WRITE_4(sc, AE_TXS_ADDR_LO_REG, BUS_ADDR_LO(addr));

	/*
	 * Configure ring buffers sizes.
	 */
	AE_WRITE_2(sc, AE_RXD_COUNT_REG, AE_RXD_COUNT_DEFAULT);
	AE_WRITE_2(sc, AE_TXD_BUFSIZE_REG, AE_TXD_BUFSIZE_DEFAULT / 4);
	AE_WRITE_2(sc, AE_TXS_COUNT_REG, AE_TXS_COUNT_DEFAULT);

	/*
	 * Configure interframe gap parameters.
	 */
	val = ((AE_IFG_TXIPG_DEFAULT << AE_IFG_TXIPG_SHIFT) &
	    AE_IFG_TXIPG_MASK) |
	    ((AE_IFG_RXIPG_DEFAULT << AE_IFG_RXIPG_SHIFT) &
	    AE_IFG_RXIPG_MASK) |
	    ((AE_IFG_IPGR1_DEFAULT << AE_IFG_IPGR1_SHIFT) &
	    AE_IFG_IPGR1_MASK) |
	    ((AE_IFG_IPGR2_DEFAULT << AE_IFG_IPGR2_SHIFT) &
	    AE_IFG_IPGR2_MASK);
	AE_WRITE_4(sc, AE_IFG_REG, val);

	/*
	 * Configure half-duplex operation.
	 */
	val = ((AE_HDPX_LCOL_DEFAULT << AE_HDPX_LCOL_SHIFT) &
	    AE_HDPX_LCOL_MASK) |
	    ((AE_HDPX_RETRY_DEFAULT << AE_HDPX_RETRY_SHIFT) &
	    AE_HDPX_RETRY_MASK) |
	    ((AE_HDPX_ABEBT_DEFAULT << AE_HDPX_ABEBT_SHIFT) &
	    AE_HDPX_ABEBT_MASK) |
	    ((AE_HDPX_JAMIPG_DEFAULT << AE_HDPX_JAMIPG_SHIFT) &
	    AE_HDPX_JAMIPG_MASK) | AE_HDPX_EXC_EN;
	AE_WRITE_4(sc, AE_HDPX_REG, val);

	/*
	 * Configure interrupt moderate timer.
	 */
	AE_WRITE_2(sc, AE_IMT_REG, AE_IMT_DEFAULT);
	val = AE_READ_4(sc, AE_MASTER_REG);
	val |= AE_MASTER_IMT_EN;
	AE_WRITE_4(sc, AE_MASTER_REG, val);

	/*
	 * Configure interrupt clearing timer.
	 */
	AE_WRITE_2(sc, AE_ICT_REG, AE_ICT_DEFAULT);

	/*
	 * Configure MTU.
	 */
	val = ifp->if_mtu + ETHER_HDR_LEN + sizeof(struct ether_vlan_header) +
	    ETHER_CRC_LEN;
	AE_WRITE_2(sc, AE_MTU_REG, val);

	/*
	 * Configure cut-through threshold.
	 */
	AE_WRITE_4(sc, AE_CUT_THRESH_REG, AE_CUT_THRESH_DEFAULT);

	/*
	 * Configure flow control.
	 */
	AE_WRITE_2(sc, AE_FLOW_THRESH_HI_REG, (AE_RXD_COUNT_DEFAULT / 8) * 7);
	AE_WRITE_2(sc, AE_FLOW_THRESH_LO_REG, (AE_RXD_COUNT_MIN / 8) >
	    (AE_RXD_COUNT_DEFAULT / 12) ? (AE_RXD_COUNT_MIN / 8) :
	    (AE_RXD_COUNT_DEFAULT / 12));

	/*
	 * Init mailboxes.
	 */
	sc->txd_cur = sc->rxd_cur = 0;
	sc->txs_ack = sc->txd_ack = 0;
	sc->rxd_cur = 0;
	AE_WRITE_2(sc, AE_MB_TXD_IDX_REG, sc->txd_cur);
	AE_WRITE_2(sc, AE_MB_RXD_IDX_REG, sc->rxd_cur);
	sc->tx_inproc = 0;
	sc->ae_flags |= AE_FLAG_TXAVAIL; /* Free Tx's available. */

	/*
	 * Enable DMA.
	 */
	AE_WRITE_1(sc, AE_DMAREAD_REG, AE_DMAREAD_EN);
	AE_WRITE_1(sc, AE_DMAWRITE_REG, AE_DMAWRITE_EN);

	/*
	 * Check if everything is OK.
	 */
	val = AE_READ_4(sc, AE_ISR_REG);
	if ((val & AE_ISR_PHY_LINKDOWN) != 0) {
		device_printf(sc->ae_dev, "Initialization failed.\n");
		return;
	}

	/*
	 * Clear interrupt status.
	 */
	AE_WRITE_4(sc, AE_ISR_REG, 0x3fffffff);
	AE_WRITE_4(sc, AE_ISR_REG, 0x0);

	/*
	 * Enable interrupts.
	 */
	val = AE_READ_4(sc, AE_MASTER_REG);
	AE_WRITE_4(sc, AE_MASTER_REG, val | AE_MASTER_MANUAL_INT);
	AE_WRITE_4(sc, AE_IMR_REG, AE_IMR_DEFAULT);

	/*
	 * Disable WOL.
	 */
	AE_WRITE_4(sc, AE_WOL_REG, 0);

	/*
	 * Configure MAC.
	 */
	val = AE_MAC_TX_CRC_EN | AE_MAC_TX_AUTOPAD |
	    AE_MAC_FULL_DUPLEX | AE_MAC_CLK_PHY |
	    AE_MAC_TX_FLOW_EN | AE_MAC_RX_FLOW_EN |
	    ((AE_HALFBUF_DEFAULT << AE_HALFBUF_SHIFT) & AE_HALFBUF_MASK) |
	    ((AE_MAC_PREAMBLE_DEFAULT << AE_MAC_PREAMBLE_SHIFT) &
	    AE_MAC_PREAMBLE_MASK);
	AE_WRITE_4(sc, AE_MAC_REG, val);

	/*
	 * Configure Rx MAC.
	 */
	ae_rxfilter(sc);
	ae_rxvlan(sc);

	/*
	 * Enable Tx/Rx.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	AE_WRITE_4(sc, AE_MAC_REG, val | AE_MAC_TX_EN | AE_MAC_RX_EN);

	sc->ae_flags &= ~AE_FLAG_LINK;
	mii_mediachg(mii);	/* Switch to the current media. */

	callout_reset(&sc->ae_tick_ch, hz, ae_tick, sc);
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

static void
ae_watchdog(struct ifnet *ifp)
{
	struct ae_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->ae_flags & AE_FLAG_LINK) == 0)
		if_printf(ifp, "watchdog timeout (missed link).\n");
	else
		if_printf(ifp, "watchdog timeout - resetting.\n");
	IFNET_STAT_INC(ifp, oerrors, 1);

	ae_init(sc);
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
ae_tick(void *xsc)
{
	struct ae_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->ae_miibus);

	lwkt_serialize_enter(ifp->if_serializer);
	mii_tick(mii);
	callout_reset(&sc->ae_tick_ch, hz, ae_tick, sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
ae_rxvlan(struct ae_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;

	val = AE_READ_4(sc, AE_MAC_REG);
	val &= ~AE_MAC_RMVLAN_EN;
	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		val |= AE_MAC_RMVLAN_EN;
	AE_WRITE_4(sc, AE_MAC_REG, val);
}

static void
ae_rxfilter(struct ae_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	rxcfg = AE_READ_4(sc, AE_MAC_REG);
	rxcfg &= ~(AE_MAC_MCAST_EN | AE_MAC_BCAST_EN | AE_MAC_PROMISC_EN);
	rxcfg |= AE_MAC_BCAST_EN;
	if (ifp->if_flags & IFF_PROMISC)
		rxcfg |= AE_MAC_PROMISC_EN;
	if (ifp->if_flags & IFF_ALLMULTI)
		rxcfg |= AE_MAC_MCAST_EN;

	/*
	 * Wipe old settings.
	 */
	AE_WRITE_4(sc, AE_REG_MHT0, 0);
	AE_WRITE_4(sc, AE_REG_MHT1, 0);
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		AE_WRITE_4(sc, AE_REG_MHT0, 0xffffffff);
		AE_WRITE_4(sc, AE_REG_MHT1, 0xffffffff);
		AE_WRITE_4(sc, AE_MAC_REG, rxcfg);
		return;
	}

	/*
	 * Load multicast tables.
	 */
	bzero(mchash, sizeof(mchash));
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_le(LLADDR((struct sockaddr_dl *)
			ifma->ifma_addr), ETHER_ADDR_LEN);
		mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);
	}
	AE_WRITE_4(sc, AE_REG_MHT0, mchash[0]);
	AE_WRITE_4(sc, AE_REG_MHT1, mchash[1]);
	AE_WRITE_4(sc, AE_MAC_REG, rxcfg);
}

static unsigned int
ae_tx_avail_size(struct ae_softc *sc)
{
	unsigned int avail;

	if (sc->txd_cur >= sc->txd_ack)
		avail = AE_TXD_BUFSIZE_DEFAULT - (sc->txd_cur - sc->txd_ack);
	else
		avail = sc->txd_ack - sc->txd_cur;
	return (avail - 4);     /* 4-byte header. */
}

static int
ae_encap(struct ae_softc *sc, struct mbuf **m_head)
{
	struct mbuf *m0;
	struct ae_txd *hdr;
	unsigned int to_end;
	uint16_t len;

	M_ASSERTPKTHDR((*m_head));
	m0 = *m_head;
	len = m0->m_pkthdr.len;
	if ((sc->ae_flags & AE_FLAG_TXAVAIL) == 0 ||
	    ae_tx_avail_size(sc) < len) {
#ifdef AE_DEBUG
		if_printf(sc->ifp, "No free Tx available.\n");
#endif
		return ENOBUFS;
	}

	hdr = (struct ae_txd *)(sc->txd_base + sc->txd_cur);
	bzero(hdr, sizeof(*hdr));

	/* Header size. */
	sc->txd_cur = (sc->txd_cur + 4) % AE_TXD_BUFSIZE_DEFAULT;

	/* Space available to the end of the ring */
	to_end = AE_TXD_BUFSIZE_DEFAULT - sc->txd_cur;

	if (to_end >= len) {
		m_copydata(m0, 0, len, (caddr_t)(sc->txd_base + sc->txd_cur));
	} else {
		m_copydata(m0, 0, to_end, (caddr_t)(sc->txd_base +
		    sc->txd_cur));
		m_copydata(m0, to_end, len - to_end, (caddr_t)sc->txd_base);
	}

	/*
	 * Set TxD flags and parameters.
	 */
	if ((m0->m_flags & M_VLANTAG) != 0) {
		hdr->vlan = htole16(AE_TXD_VLAN(m0->m_pkthdr.ether_vlantag));
		hdr->len = htole16(len | AE_TXD_INSERT_VTAG);
	} else {
		hdr->len = htole16(len);
	}

	/*
	 * Set current TxD position and round up to a 4-byte boundary.
	 */
	sc->txd_cur = ((sc->txd_cur + len + 3) & ~3) % AE_TXD_BUFSIZE_DEFAULT;
	if (sc->txd_cur == sc->txd_ack)
		sc->ae_flags &= ~AE_FLAG_TXAVAIL;
#ifdef AE_DEBUG
	if_printf(sc->ifp, "New txd_cur = %d.\n", sc->txd_cur);
#endif

	/*
	 * Update TxS position and check if there are empty TxS available.
	 */
	sc->txs_base[sc->txs_cur].flags &= ~htole16(AE_TXS_UPDATE);
	sc->txs_cur = (sc->txs_cur + 1) % AE_TXS_COUNT_DEFAULT;
	if (sc->txs_cur == sc->txs_ack)
		sc->ae_flags &= ~AE_FLAG_TXAVAIL;

	/*
	 * Synchronize DMA memory.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map, BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
ae_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct ae_softc *sc = ifp->if_softc;
	int error, trans;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

#ifdef AE_DEBUG
	if_printf(ifp, "Start called.\n");
#endif
	if ((sc->ae_flags & AE_FLAG_LINK) == 0) {
		ifq_purge(&ifp->if_snd);
		return;
	}
	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	trans = 0;
	while (!ifq_is_empty(&ifp->if_snd)) {
		struct mbuf *m0;

		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;  /* Nothing to do. */

		error = ae_encap(sc, &m0);
		if (error != 0) {
			if (m0 != NULL) {
				ifq_prepend(&ifp->if_snd, m0);
				ifq_set_oactive(&ifp->if_snd);
#ifdef AE_DEBUG
				if_printf(ifp, "Setting OACTIVE.\n");
#endif
			}
			break;
		}
		trans = 1;
		sc->tx_inproc++;

		/* Bounce a copy of the frame to BPF. */
		ETHER_BPF_MTAP(ifp, m0);
		m_freem(m0);
	}
	if (trans) {	/* Something was dequeued. */
		AE_WRITE_2(sc, AE_MB_TXD_IDX_REG, sc->txd_cur / 4);
		ifp->if_timer = AE_TX_TIMEOUT; /* Load watchdog. */
#ifdef AE_DEBUG
		if_printf(ifp, "%d packets dequeued.\n", count);
		if_printf(ifp, "Tx pos now is %d.\n", sc->txd_cur);
#endif
	}
}

static int
ae_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
        struct ae_softc *sc = ifp->if_softc;
        struct ifreq *ifr;
        struct mii_data *mii;
        int error = 0, mask;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifr = (struct ifreq *)data;
	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if (((ifp->if_flags ^ sc->ae_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
					ae_rxfilter(sc);
			} else {
				ae_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ae_stop(sc);
		}
		sc->ae_if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			ae_rxfilter(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->ae_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_VLAN_HWTAGGING) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			ae_rxvlan(sc);
		}
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static int
ae_attach(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0;

	sc->ae_dev = dev;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	callout_init(&sc->ae_tick_ch);

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate memory mapped IO
	 */
	sc->ae_mem_rid = PCIR_BAR(0);
	sc->ae_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						&sc->ae_mem_rid, RF_ACTIVE);
	if (sc->ae_mem_res == NULL) {
		device_printf(dev, "can't allocate IO memory\n");
		return ENXIO;
	}
	sc->ae_mem_bt = rman_get_bustag(sc->ae_mem_res);
	sc->ae_mem_bh = rman_get_bushandle(sc->ae_mem_res);

	/*
	 * Allocate IRQ
	 */
	sc->ae_irq_rid = 0;
	sc->ae_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->ae_irq_rid,
						RF_SHAREABLE | RF_ACTIVE);
	if (sc->ae_irq_res == NULL) {
		device_printf(dev, "can't allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	/* Set PHY address. */
	sc->ae_phyaddr = AE_PHYADDR_DEFAULT;

	/* Create sysctl tree */
	ae_sysctl_node(sc);

	/* Reset PHY. */
	ae_phy_reset(sc);

	/*
	 * Reset the ethernet controller.
	 */
	ae_reset(sc);
	ae_pcie_init(sc);

	/*
	 * Get PCI and chip id/revision.
	 */
	sc->ae_rev = pci_get_revid(dev);
	sc->ae_chip_rev =
	(AE_READ_4(sc, AE_MASTER_REG) >> AE_MASTER_REVNUM_SHIFT) &
	AE_MASTER_REVNUM_MASK;
	if (bootverbose) {
		device_printf(dev, "PCI device revision : 0x%04x\n", sc->ae_rev);
		device_printf(dev, "Chip id/revision : 0x%04x\n",
		    sc->ae_chip_rev);
	}

	/*
	 * XXX
	 * Unintialized hardware returns an invalid chip id/revision
	 * as well as 0xFFFFFFFF for Tx/Rx fifo length. It seems that
	 * unplugged cable results in putting hardware into automatic
	 * power down mode which in turn returns invalld chip revision.
	 */
	if (sc->ae_chip_rev == 0xFFFF) {
		device_printf(dev,"invalid chip revision : 0x%04x -- "
		    "not initialized?\n", sc->ae_chip_rev);
		error = ENXIO;
		goto fail;
	}
#if 0
	/* Get DMA parameters from PCIe device control register. */
	pcie_ptr = pci_get_pciecap_ptr(dev);
	if (pcie_ptr) {
		uint16_t devctl;
		sc->ae_flags |= AE_FLAG_PCIE;
		devctl = pci_read_config(dev, pcie_ptr + PCIER_DEVCTRL, 2);
		/* Max read request size. */
		sc->ae_dma_rd_burst = ((devctl >> 12) & 0x07) <<
		    DMA_CFG_RD_BURST_SHIFT;
		/* Max payload size. */
		sc->ae_dma_wr_burst = ((devctl >> 5) & 0x07) <<
		    DMA_CFG_WR_BURST_SHIFT;
		if (bootverbose) {
			device_printf(dev, "Read request size : %d bytes.\n",
			    128 << ((devctl >> 12) & 0x07));
			device_printf(dev, "TLP payload size : %d bytes.\n",
			    128 << ((devctl >> 5) & 0x07));
		}
	} else {
		sc->ae_dma_rd_burst = DMA_CFG_RD_BURST_128;
		sc->ae_dma_wr_burst = DMA_CFG_WR_BURST_128;
	}
#endif

	/* Create DMA stuffs */
	error = ae_dma_alloc(sc);
	if (error)
		goto fail;

	/* Load station address. */
	ae_get_eaddr(sc);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ae_ioctl;
	ifp->if_start = ae_start;
	ifp->if_init = ae_init;
	ifp->if_watchdog = ae_watchdog;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN - 1);
	ifq_set_ready(&ifp->if_snd);
	ifp->if_capabilities = IFCAP_VLAN_MTU |
			       IFCAP_VLAN_HWTAGGING;
	ifp->if_hwassist = 0;
	ifp->if_capenable = ifp->if_capabilities;

	/* Set up MII bus. */
	error = mii_phy_probe(dev, &sc->ae_miibus,
			      ae_mediachange, ae_mediastatus);
	if (error) {
		device_printf(dev, "no PHY found!\n");
		goto fail;
	}
	ether_ifattach(ifp, sc->ae_eaddr, NULL);

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->ae_irq_res));

	error = bus_setup_intr(dev, sc->ae_irq_res, INTR_MPSAFE, ae_intr, sc,
			       &sc->ae_irq_handle, ifp->if_serializer);
	if (error) {
		device_printf(dev, "could not set up interrupt handler.\n");
		ether_ifdetach(ifp);
		goto fail;
	}
	return 0;
fail:
	ae_detach(dev);
	return (error);
}

static int
ae_detach(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		sc->ae_flags |= AE_FLAG_DETACH;
		ae_stop(sc);
		bus_teardown_intr(dev, sc->ae_irq_res, sc->ae_irq_handle);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->ae_miibus != NULL)
		device_delete_child(dev, sc->ae_miibus);
	bus_generic_detach(dev);

	if (sc->ae_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->ae_irq_rid,
				     sc->ae_irq_res);
	}
	if (sc->ae_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->ae_mem_rid,
				     sc->ae_mem_res);
	}
	ae_dma_free(sc);

	return (0);
}

static void
ae_dma_free(struct ae_softc *sc)
{
	if (sc->dma_txd_tag != NULL) {
		bus_dmamap_unload(sc->dma_txd_tag, sc->dma_txd_map);
		bus_dmamem_free(sc->dma_txd_tag, sc->txd_base,
		    sc->dma_txd_map);
		bus_dma_tag_destroy(sc->dma_txd_tag);
	}
	if (sc->dma_txs_tag != NULL) {
		bus_dmamap_unload(sc->dma_txs_tag, sc->dma_txs_map);
		bus_dmamem_free(sc->dma_txs_tag, sc->txs_base,
		    sc->dma_txs_map);
		bus_dma_tag_destroy(sc->dma_txs_tag);
	}
	if (sc->dma_rxd_tag != NULL) {
		bus_dmamap_unload(sc->dma_rxd_tag, sc->dma_rxd_map);
		bus_dmamem_free(sc->dma_rxd_tag,
		    sc->rxd_base_dma, sc->dma_rxd_map);
		bus_dma_tag_destroy(sc->dma_rxd_tag);
	}
	if (sc->dma_parent_tag != NULL)
		bus_dma_tag_destroy(sc->dma_parent_tag);
}

static void
ae_pcie_init(struct ae_softc *sc)
{
	AE_WRITE_4(sc, AE_PCIE_LTSSM_TESTMODE_REG,
		   AE_PCIE_LTSSM_TESTMODE_DEFAULT);
	AE_WRITE_4(sc, AE_PCIE_DLL_TX_CTRL_REG,
		   AE_PCIE_DLL_TX_CTRL_DEFAULT);
}

static void
ae_phy_reset(struct ae_softc *sc)
{
	AE_WRITE_4(sc, AE_PHY_ENABLE_REG, AE_PHY_ENABLE);
	DELAY(1000);    /* XXX: pause(9) ? */
}

static int
ae_reset(struct ae_softc *sc)
{
	int i;

	/*
	 * Issue a soft reset.
	 */
	AE_WRITE_4(sc, AE_MASTER_REG, AE_MASTER_SOFT_RESET);
	bus_space_barrier(sc->ae_mem_bt, sc->ae_mem_bh, AE_MASTER_REG, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * Wait for reset to complete.
	 */
	for (i = 0; i < AE_RESET_TIMEOUT; i++) {
		if ((AE_READ_4(sc, AE_MASTER_REG) & AE_MASTER_SOFT_RESET) == 0)
			break;
		DELAY(10);
	}
	if (i == AE_RESET_TIMEOUT) {
		device_printf(sc->ae_dev, "reset timeout.\n");
		return (ENXIO);
	}

	/*
	 * Wait for everything to enter idle state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		if (AE_READ_4(sc, AE_IDLE_REG) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT) {
		device_printf(sc->ae_dev, "could not enter idle state.\n");
		return (ENXIO);
	}
	return (0);
}

static int
ae_check_eeprom_present(struct ae_softc *sc, int *vpdc)
{
	int error;
	uint32_t val;

	/*
	 * Not sure why, but Linux does this.
	 */
	val = AE_READ_4(sc, AE_SPICTL_REG);
	if ((val & AE_SPICTL_VPD_EN) != 0) {
		val &= ~AE_SPICTL_VPD_EN;
		AE_WRITE_4(sc, AE_SPICTL_REG, val);
	}
	error = pci_find_extcap(sc->ae_dev, PCIY_VPD, vpdc);
	return (error);
}

static int
ae_vpd_read_word(struct ae_softc *sc, int reg, uint32_t *word)
{
	uint32_t val;
	int i;

	AE_WRITE_4(sc, AE_VPD_DATA_REG, 0);	/* Clear register value. */

	/*
	 * VPD registers start at offset 0x100. Read them.
	 */
	val = 0x100 + reg * 4;
	AE_WRITE_4(sc, AE_VPD_CAP_REG, (val << AE_VPD_CAP_ADDR_SHIFT) &
	    AE_VPD_CAP_ADDR_MASK);
	for (i = 0; i < AE_VPD_TIMEOUT; i++) {
		DELAY(2000);
		val = AE_READ_4(sc, AE_VPD_CAP_REG);
		if ((val & AE_VPD_CAP_DONE) != 0)
			break;
	}
	if (i == AE_VPD_TIMEOUT) {
		device_printf(sc->ae_dev, "timeout reading VPD register %d.\n",
		    reg);
		return (ETIMEDOUT);
	}
	*word = AE_READ_4(sc, AE_VPD_DATA_REG);
	return (0);
}

static int
ae_get_vpd_eaddr(struct ae_softc *sc, uint32_t *eaddr)
{
	uint32_t word, reg, val;
	int error;
	int found;
	int vpdc;
	int i;

	/*
	 * Check for EEPROM.
	 */
	error = ae_check_eeprom_present(sc, &vpdc);
	if (error != 0)
		return (error);

	/*
	 * Read the VPD configuration space.
	 * Each register is prefixed with signature,
	 * so we can check if it is valid.
	 */
	for (i = 0, found = 0; i < AE_VPD_NREGS; i++) {
		error = ae_vpd_read_word(sc, i, &word);
		if (error != 0)
			break;

		/*
		 * Check signature.
		 */
		if ((word & AE_VPD_SIG_MASK) != AE_VPD_SIG)
			break;
		reg = word >> AE_VPD_REG_SHIFT;
		i++;	/* Move to the next word. */
		if (reg != AE_EADDR0_REG && reg != AE_EADDR1_REG)
			continue;

		error = ae_vpd_read_word(sc, i, &val);
		if (error != 0)
			break;
		if (reg == AE_EADDR0_REG)
			eaddr[0] = val;
		else
			eaddr[1] = val;
		found++;
	}
	if (found < 2)
		return (ENOENT);

	eaddr[1] &= 0xffff;	/* Only last 2 bytes are used. */
	if (AE_CHECK_EADDR_VALID(eaddr) != 0) {
		if (bootverbose)
			device_printf(sc->ae_dev,
			    "VPD ethernet address registers are invalid.\n");
		return (EINVAL);
	}
	return (0);
}

static int
ae_get_reg_eaddr(struct ae_softc *sc, uint32_t *eaddr)
{
	/*
	 * BIOS is supposed to set this.
	 */
	eaddr[0] = AE_READ_4(sc, AE_EADDR0_REG);
	eaddr[1] = AE_READ_4(sc, AE_EADDR1_REG);
	eaddr[1] &= 0xffff;	/* Only last 2 bytes are used. */
	if (AE_CHECK_EADDR_VALID(eaddr) != 0) {
		if (bootverbose)
			device_printf(sc->ae_dev,
			    "Ethetnet address registers are invalid.\n");
		return (EINVAL);
	}
	return (0);
}

static void
ae_get_eaddr(struct ae_softc *sc)
{
	uint32_t eaddr[2] = {0, 0};
	int error;

	/*
	 *Check for EEPROM.
	 */
	error = ae_get_vpd_eaddr(sc, eaddr);
	if (error)
		error = ae_get_reg_eaddr(sc, eaddr);
	if (error) {
		if (bootverbose)
			device_printf(sc->ae_dev,
			    "Generating random ethernet address.\n");
		eaddr[0] = karc4random();
		/*
		 * Set OUI to ASUSTek COMPUTER INC.
		 */
		sc->ae_eaddr[0] = 0x02;	/* U/L bit set. */
		sc->ae_eaddr[1] = 0x1f;
		sc->ae_eaddr[2] = 0xc6;
		sc->ae_eaddr[3] = (eaddr[0] >> 16) & 0xff;
		sc->ae_eaddr[4] = (eaddr[0] >> 8) & 0xff;
		sc->ae_eaddr[5] = (eaddr[0] >> 0) & 0xff;
	} else {
		sc->ae_eaddr[0] = (eaddr[1] >> 8) & 0xff;
		sc->ae_eaddr[1] = (eaddr[1] >> 0) & 0xff;
		sc->ae_eaddr[2] = (eaddr[0] >> 24) & 0xff;
		sc->ae_eaddr[3] = (eaddr[0] >> 16) & 0xff;
		sc->ae_eaddr[4] = (eaddr[0] >> 8) & 0xff;
		sc->ae_eaddr[5] = (eaddr[0] >> 0) & 0xff;
	}
}

static int
ae_mediachange(struct ifnet *ifp)
{
	struct ae_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->ae_miibus);
	int error;

	ASSERT_SERIALIZED(ifp->if_serializer);
	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	error = mii_mediachg(mii);
	return (error);
}

static void
ae_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ae_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->ae_miibus);

	ASSERT_SERIALIZED(ifp->if_serializer);
	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

static void
ae_update_stats_tx(uint16_t flags, struct ae_stats *stats)
{
	if ((flags & AE_TXS_BCAST) != 0)
		stats->tx_bcast++;
	if ((flags & AE_TXS_MCAST) != 0)
		stats->tx_mcast++;
	if ((flags & AE_TXS_PAUSE) != 0)
		stats->tx_pause++;
	if ((flags & AE_TXS_CTRL) != 0)
		stats->tx_ctrl++;
	if ((flags & AE_TXS_DEFER) != 0)
		stats->tx_defer++;
	if ((flags & AE_TXS_EXCDEFER) != 0)
		stats->tx_excdefer++;
	if ((flags & AE_TXS_SINGLECOL) != 0)
		stats->tx_singlecol++;
	if ((flags & AE_TXS_MULTICOL) != 0)
		stats->tx_multicol++;
	if ((flags & AE_TXS_LATECOL) != 0)
		stats->tx_latecol++;
	if ((flags & AE_TXS_ABORTCOL) != 0)
		stats->tx_abortcol++;
	if ((flags & AE_TXS_UNDERRUN) != 0)
		stats->tx_underrun++;
}

static void
ae_update_stats_rx(uint16_t flags, struct ae_stats *stats)
{
	if ((flags & AE_RXD_BCAST) != 0)
		stats->rx_bcast++;
	if ((flags & AE_RXD_MCAST) != 0)
		stats->rx_mcast++;
	if ((flags & AE_RXD_PAUSE) != 0)
		stats->rx_pause++;
	if ((flags & AE_RXD_CTRL) != 0)
		stats->rx_ctrl++;
	if ((flags & AE_RXD_CRCERR) != 0)
		stats->rx_crcerr++;
	if ((flags & AE_RXD_CODEERR) != 0)
		stats->rx_codeerr++;
	if ((flags & AE_RXD_RUNT) != 0)
		stats->rx_runt++;
	if ((flags & AE_RXD_FRAG) != 0)
		stats->rx_frag++;
	if ((flags & AE_RXD_TRUNC) != 0)
		stats->rx_trunc++;
	if ((flags & AE_RXD_ALIGN) != 0)
		stats->rx_align++;
}

static int
ae_resume(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
#if 0
	AE_READ_4(sc, AE_WOL_REG);	/* Clear WOL status. */
#endif
	ae_phy_reset(sc);
	if ((ifp->if_flags & IFF_UP) != 0)
		ae_init(sc);
	lwkt_serialize_exit(ifp->if_serializer);
	return (0);
}

static int
ae_suspend(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	ae_stop(sc);
#if 0
	/* we don't use ae_pm_init because we don't want WOL */
	ae_pm_init(sc);
#endif
	lwkt_serialize_exit(ifp->if_serializer);
	return (0);
}

static int
ae_shutdown(device_t dev)
{
	struct ae_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ae_suspend(dev);

	lwkt_serialize_enter(ifp->if_serializer);
	ae_powersave_enable(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

static void
ae_powersave_disable(struct ae_softc *sc)
{
	uint32_t val;

	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 0);
	val = AE_PHY_READ(sc, AE_PHY_DBG_DATA);
	if (val & AE_PHY_DBG_POWERSAVE) {
		val &= ~AE_PHY_DBG_POWERSAVE;
		AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, val);
		DELAY(1000);
	}
}

static void
ae_powersave_enable(struct ae_softc *sc)
{
	uint32_t val;

	/*
	 * XXX magic numbers.
	 */
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 0);
	val = AE_PHY_READ(sc, AE_PHY_DBG_DATA);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, val | 0x1000);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 2);
	AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, 0x3000);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 3);
	AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, 0);
}
