/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2001
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/bge/if_bge.c,v 1.3.2.39 2005/07/03 03:41:18 silby Exp $
 */

#include "opt_bnx.h"
#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/if_ringmap.h>
#include <net/toeplitz.h>
#include <net/toeplitz2.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>
#include <dev/netif/mii_layer/brgphyreg.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/bge/if_bgereg.h>
#include <dev/netif/bnx/if_bnxvar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define BNX_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

#define	BNX_RESET_SHUTDOWN	0
#define	BNX_RESET_START		1
#define	BNX_RESET_SUSPEND	2

#define BNX_INTR_CKINTVL	((10 * hz) / 1000)	/* 10ms */

#ifdef BNX_RSS_DEBUG
#define BNX_RSS_DPRINTF(sc, lvl, fmt, ...) \
do { \
	if (sc->bnx_rss_debug >= lvl) \
		if_printf(&sc->arpcom.ac_if, fmt, __VA_ARGS__); \
} while (0)
#else	/* !BNX_RSS_DEBUG */
#define BNX_RSS_DPRINTF(sc, lvl, fmt, ...)	((void)0)
#endif	/* BNX_RSS_DEBUG */

static const struct bnx_type {
	uint16_t		bnx_vid;
	uint16_t		bnx_did;
	char			*bnx_name;
} bnx_devs[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5717,
		"Broadcom BCM5717 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5717C,
		"Broadcom BCM5717C Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5718,
		"Broadcom BCM5718 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5719,
		"Broadcom BCM5719 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5720_ALT,
		"Broadcom BCM5720 Gigabit Ethernet" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5725,
		"Broadcom BCM5725 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5727,
		"Broadcom BCM5727 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5762,
		"Broadcom BCM5762 Gigabit Ethernet" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57761,
		"Broadcom BCM57761 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57762,
		"Broadcom BCM57762 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57764,
		"Broadcom BCM57764 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57765,
		"Broadcom BCM57765 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57766,
		"Broadcom BCM57766 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57767,
		"Broadcom BCM57767 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57781,
		"Broadcom BCM57781 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57782,
		"Broadcom BCM57782 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57785,
		"Broadcom BCM57785 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57786,
		"Broadcom BCM57786 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57787,
		"Broadcom BCM57787 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57791,
		"Broadcom BCM57791 Fast Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57795,
		"Broadcom BCM57795 Fast Ethernet" },

	{ 0, 0, NULL }
};

static const int bnx_tx_mailbox[BNX_TX_RING_MAX] = {
	BGE_MBX_TX_HOST_PROD0_LO,
	BGE_MBX_TX_HOST_PROD0_HI,
	BGE_MBX_TX_HOST_PROD1_LO,
	BGE_MBX_TX_HOST_PROD1_HI
};

#define BNX_IS_JUMBO_CAPABLE(sc)	((sc)->bnx_flags & BNX_FLAG_JUMBO)
#define BNX_IS_5717_PLUS(sc)		((sc)->bnx_flags & BNX_FLAG_5717_PLUS)
#define BNX_IS_57765_PLUS(sc)		((sc)->bnx_flags & BNX_FLAG_57765_PLUS)
#define BNX_IS_57765_FAMILY(sc)	 \
	((sc)->bnx_flags & BNX_FLAG_57765_FAMILY)

typedef int	(*bnx_eaddr_fcn_t)(struct bnx_softc *, uint8_t[]);

static int	bnx_probe(device_t);
static int	bnx_attach(device_t);
static int	bnx_detach(device_t);
static void	bnx_shutdown(device_t);
static int	bnx_suspend(device_t);
static int	bnx_resume(device_t);
static int	bnx_miibus_readreg(device_t, int, int);
static int	bnx_miibus_writereg(device_t, int, int, int);
static void	bnx_miibus_statchg(device_t);

static int	bnx_handle_status(struct bnx_softc *);
#ifdef IFPOLL_ENABLE
static void	bnx_npoll(struct ifnet *, struct ifpoll_info *);
static void	bnx_npoll_rx(struct ifnet *, void *, int);
static void	bnx_npoll_tx(struct ifnet *, void *, int);
static void	bnx_npoll_tx_notag(struct ifnet *, void *, int);
static void	bnx_npoll_status(struct ifnet *);
static void	bnx_npoll_status_notag(struct ifnet *);
#endif
static void	bnx_intr_legacy(void *);
static void	bnx_msi(void *);
static void	bnx_intr(struct bnx_softc *);
static void	bnx_msix_status(void *);
static void	bnx_msix_tx_status(void *);
static void	bnx_msix_rx(void *);
static void	bnx_msix_rxtx(void *);
static void	bnx_enable_intr(struct bnx_softc *);
static void	bnx_disable_intr(struct bnx_softc *);
static void	bnx_txeof(struct bnx_tx_ring *, uint16_t);
static void	bnx_rxeof(struct bnx_rx_ret_ring *, uint16_t, int);
static int	bnx_alloc_intr(struct bnx_softc *);
static int	bnx_setup_intr(struct bnx_softc *);
static void	bnx_free_intr(struct bnx_softc *);
static void	bnx_teardown_intr(struct bnx_softc *, int);
static int	bnx_alloc_msix(struct bnx_softc *);
static void	bnx_free_msix(struct bnx_softc *, boolean_t);
static void	bnx_check_intr_rxtx(void *);
static void	bnx_check_intr_rx(void *);
static void	bnx_check_intr_tx(void *);
static void	bnx_rx_std_refill_ithread(void *);
static void	bnx_rx_std_refill(void *, void *);
static void	bnx_rx_std_refill_sched_ipi(void *);
static void	bnx_rx_std_refill_stop(void *);
static void	bnx_rx_std_refill_sched(struct bnx_rx_ret_ring *,
		    struct bnx_rx_std_ring *);

static void	bnx_start(struct ifnet *, struct ifaltq_subque *);
static int	bnx_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bnx_init(void *);
static void	bnx_stop(struct bnx_softc *);
static void	bnx_watchdog(struct ifaltq_subque *);
static int	bnx_ifmedia_upd(struct ifnet *);
static void	bnx_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	bnx_tick(void *);
static void	bnx_serialize(struct ifnet *, enum ifnet_serialize);
static void	bnx_deserialize(struct ifnet *, enum ifnet_serialize);
static int	bnx_tryserialize(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
static void	bnx_serialize_assert(struct ifnet *, enum ifnet_serialize,
		    boolean_t);
#endif
static void	bnx_serialize_skipmain(struct bnx_softc *);
static void	bnx_deserialize_skipmain(struct bnx_softc *sc);

static int	bnx_alloc_jumbo_mem(struct bnx_softc *);
static void	bnx_free_jumbo_mem(struct bnx_softc *);
static struct bnx_jslot
		*bnx_jalloc(struct bnx_softc *);
static void	bnx_jfree(void *);
static void	bnx_jref(void *);
static int	bnx_newbuf_std(struct bnx_rx_ret_ring *, int, int);
static int	bnx_newbuf_jumbo(struct bnx_softc *, int, int);
static void	bnx_setup_rxdesc_std(struct bnx_rx_std_ring *, int);
static void	bnx_setup_rxdesc_jumbo(struct bnx_softc *, int);
static int	bnx_init_rx_ring_std(struct bnx_rx_std_ring *);
static void	bnx_free_rx_ring_std(struct bnx_rx_std_ring *);
static int	bnx_init_rx_ring_jumbo(struct bnx_softc *);
static void	bnx_free_rx_ring_jumbo(struct bnx_softc *);
static void	bnx_free_tx_ring(struct bnx_tx_ring *);
static int	bnx_init_tx_ring(struct bnx_tx_ring *);
static int	bnx_create_tx_ring(struct bnx_tx_ring *);
static void	bnx_destroy_tx_ring(struct bnx_tx_ring *);
static int	bnx_create_rx_ret_ring(struct bnx_rx_ret_ring *);
static void	bnx_destroy_rx_ret_ring(struct bnx_rx_ret_ring *);
static int	bnx_dma_alloc(device_t);
static void	bnx_dma_free(struct bnx_softc *);
static int	bnx_dma_block_alloc(struct bnx_softc *, bus_size_t,
		    bus_dma_tag_t *, bus_dmamap_t *, void **, bus_addr_t *);
static void	bnx_dma_block_free(bus_dma_tag_t, bus_dmamap_t, void *);
static struct mbuf *
		bnx_defrag_shortdma(struct mbuf *);
static int	bnx_encap(struct bnx_tx_ring *, struct mbuf **,
		    uint32_t *, int *);
static int	bnx_setup_tso(struct bnx_tx_ring *, struct mbuf **,
		    uint16_t *, uint16_t *);
static void	bnx_setup_serialize(struct bnx_softc *);
static void	bnx_set_tick_cpuid(struct bnx_softc *, boolean_t);
static void	bnx_setup_ring_cnt(struct bnx_softc *);

static struct pktinfo *bnx_rss_info(struct pktinfo *,
		    const struct bge_rx_bd *);
static void	bnx_init_rss(struct bnx_softc *);
static void	bnx_reset(struct bnx_softc *);
static int	bnx_chipinit(struct bnx_softc *);
static int	bnx_blockinit(struct bnx_softc *);
static void	bnx_stop_block(struct bnx_softc *, bus_size_t, uint32_t);
static void	bnx_enable_msi(struct bnx_softc *, boolean_t);
static void	bnx_setmulti(struct bnx_softc *);
static void	bnx_setpromisc(struct bnx_softc *);
static void	bnx_stats_update_regs(struct bnx_softc *);
static uint32_t	bnx_dma_swap_options(struct bnx_softc *);

static uint32_t	bnx_readmem_ind(struct bnx_softc *, uint32_t);
static void	bnx_writemem_ind(struct bnx_softc *, uint32_t, uint32_t);
#ifdef notdef
static uint32_t	bnx_readreg_ind(struct bnx_softc *, uint32_t);
#endif
static void	bnx_writemem_direct(struct bnx_softc *, uint32_t, uint32_t);
static void	bnx_writembx(struct bnx_softc *, int, int);
static int	bnx_read_nvram(struct bnx_softc *, caddr_t, int, int);
static uint8_t	bnx_eeprom_getbyte(struct bnx_softc *, uint32_t, uint8_t *);
static int	bnx_read_eeprom(struct bnx_softc *, caddr_t, uint32_t, size_t);

static void	bnx_tbi_link_upd(struct bnx_softc *, uint32_t);
static void	bnx_copper_link_upd(struct bnx_softc *, uint32_t);
static void	bnx_autopoll_link_upd(struct bnx_softc *, uint32_t);
static void	bnx_link_poll(struct bnx_softc *);

static int	bnx_get_eaddr_mem(struct bnx_softc *, uint8_t[]);
static int	bnx_get_eaddr_nvram(struct bnx_softc *, uint8_t[]);
static int	bnx_get_eaddr_eeprom(struct bnx_softc *, uint8_t[]);
static int	bnx_get_eaddr(struct bnx_softc *, uint8_t[]);

static void	bnx_coal_change(struct bnx_softc *);
static int	bnx_sysctl_force_defrag(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_wreg(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_bds_poll(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_bds_poll(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *,
		    int, int, uint32_t);
static int	bnx_sysctl_std_refill(SYSCTL_HANDLER_ARGS);

static void	bnx_sig_post_reset(struct bnx_softc *, int);
static void	bnx_sig_pre_reset(struct bnx_softc *, int);
static void	bnx_ape_lock_init(struct bnx_softc *);
static void	bnx_ape_read_fw_ver(struct bnx_softc *);
static int	bnx_ape_lock(struct bnx_softc *, int);
static void	bnx_ape_unlock(struct bnx_softc *, int);
static void	bnx_ape_send_event(struct bnx_softc *, uint32_t);
static void	bnx_ape_driver_state_change(struct bnx_softc *, int);

static int	bnx_msi_enable = 1;
static int	bnx_msix_enable = 1;

static int	bnx_rx_rings = 0; /* auto */
static int	bnx_tx_rings = 0; /* auto */

TUNABLE_INT("hw.bnx.msi.enable", &bnx_msi_enable);
TUNABLE_INT("hw.bnx.msix.enable", &bnx_msix_enable);
TUNABLE_INT("hw.bnx.rx_rings", &bnx_rx_rings);
TUNABLE_INT("hw.bnx.tx_rings", &bnx_tx_rings);

static device_method_t bnx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bnx_probe),
	DEVMETHOD(device_attach,	bnx_attach),
	DEVMETHOD(device_detach,	bnx_detach),
	DEVMETHOD(device_shutdown,	bnx_shutdown),
	DEVMETHOD(device_suspend,	bnx_suspend),
	DEVMETHOD(device_resume,	bnx_resume),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bnx_miibus_readreg),
	DEVMETHOD(miibus_writereg,	bnx_miibus_writereg),
	DEVMETHOD(miibus_statchg,	bnx_miibus_statchg),

	DEVMETHOD_END
};

static DEFINE_CLASS_0(bnx, bnx_driver, bnx_methods, sizeof(struct bnx_softc));
static devclass_t bnx_devclass;

DECLARE_DUMMY_MODULE(if_bnx);
MODULE_DEPEND(if_bnx, miibus, 1, 1, 1);
DRIVER_MODULE(if_bnx, pci, bnx_driver, bnx_devclass, NULL, NULL);
DRIVER_MODULE(miibus, bnx, miibus_driver, miibus_devclass, NULL, NULL);

static uint32_t
bnx_readmem_ind(struct bnx_softc *sc, uint32_t off)
{
	device_t dev = sc->bnx_dev;
	uint32_t val;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	val = pci_read_config(dev, BGE_PCI_MEMWIN_DATA, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
	return (val);
}

static void
bnx_writemem_ind(struct bnx_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bnx_dev;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_DATA, val, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
}

static void
bnx_writemem_direct(struct bnx_softc *sc, uint32_t off, uint32_t val)
{
	CSR_WRITE_4(sc, off, val);
}

static void
bnx_writembx(struct bnx_softc *sc, int off, int val)
{
	CSR_WRITE_4(sc, off, val);
}

/*
 * Read a sequence of bytes from NVRAM.
 */
static int
bnx_read_nvram(struct bnx_softc *sc, caddr_t dest, int off, int cnt)
{
	return (1);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.' The
 * BCM570x supports both the traditional bitbang interface and an
 * auto access interface for reading the EEPROM. We use the auto
 * access method.
 */
static uint8_t
bnx_eeprom_getbyte(struct bnx_softc *sc, uint32_t addr, uint8_t *dest)
{
	int i;
	uint32_t byte = 0;

	/*
	 * Enable use of auto EEPROM access so we can avoid
	 * having to use the bitbang method.
	 */
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_AUTO_EEPROM);

	/* Reset the EEPROM, load the clock period. */
	CSR_WRITE_4(sc, BGE_EE_ADDR,
	    BGE_EEADDR_RESET|BGE_EEHALFCLK(BGE_HALFCLK_384SCL));
	DELAY(20);

	/* Issue the read EEPROM command. */
	CSR_WRITE_4(sc, BGE_EE_ADDR, BGE_EE_READCMD | addr);

	/* Wait for completion */
	for(i = 0; i < BNX_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_EE_ADDR) & BGE_EEADDR_DONE)
			break;
	}

	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "eeprom read timed out\n");
		return(1);
	}

	/* Get result. */
	byte = CSR_READ_4(sc, BGE_EE_DATA);

        *dest = (byte >> ((addr % 4) * 8)) & 0xFF;

	return(0);
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
static int
bnx_read_eeprom(struct bnx_softc *sc, caddr_t dest, uint32_t off, size_t len)
{
	size_t i;
	int err;
	uint8_t byte;

	for (byte = 0, err = 0, i = 0; i < len; i++) {
		err = bnx_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return(err ? 1 : 0);
}

static int
bnx_miibus_readreg(device_t dev, int phy, int reg)
{
	struct bnx_softc *sc = device_get_softc(dev);
	uint32_t val;
	int i;

	KASSERT(phy == sc->bnx_phyno,
	    ("invalid phyno %d, should be %d", phy, sc->bnx_phyno));

	if (bnx_ape_lock(sc, sc->bnx_phy_ape_lock) != 0)
		return 0;

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bnx_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_READ | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg));

	/* Poll for the PHY register access to complete. */
	for (i = 0; i < BNX_TIMEOUT; i++) {
		DELAY(10);
		val = CSR_READ_4(sc, BGE_MI_COMM);
		if ((val & BGE_MICOMM_BUSY) == 0) {
			DELAY(5);
			val = CSR_READ_4(sc, BGE_MI_COMM);
			break;
		}
	}
	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "PHY read timed out "
		    "(phy %d, reg %d, val 0x%08x)\n", phy, reg, val);
		val = 0;
	}

	/* Restore the autopoll bit if necessary. */
	if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bnx_mi_mode);
		DELAY(80);
	}

	bnx_ape_unlock(sc, sc->bnx_phy_ape_lock);

	if (val & BGE_MICOMM_READFAIL)
		return 0;

	return (val & 0xFFFF);
}

static int
bnx_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct bnx_softc *sc = device_get_softc(dev);
	int i;

	KASSERT(phy == sc->bnx_phyno,
	    ("invalid phyno %d, should be %d", phy, sc->bnx_phyno));

	if (bnx_ape_lock(sc, sc->bnx_phy_ape_lock) != 0)
		return 0;

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bnx_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_WRITE | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg) | val);

	for (i = 0; i < BNX_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, BGE_MI_COMM) & BGE_MICOMM_BUSY)) {
			DELAY(5);
			CSR_READ_4(sc, BGE_MI_COMM); /* dummy read */
			break;
		}
	}
	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "PHY write timed out "
		    "(phy %d, reg %d, val %d)\n", phy, reg, val);
	}

	/* Restore the autopoll bit if necessary. */
	if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bnx_mi_mode);
		DELAY(80);
	}

	bnx_ape_unlock(sc, sc->bnx_phy_ape_lock);

	return 0;
}

static void
bnx_miibus_statchg(device_t dev)
{
	struct bnx_softc *sc;
	struct mii_data *mii;
	uint32_t mac_mode;

	sc = device_get_softc(dev);
	if ((sc->arpcom.ac_if.if_flags & IFF_RUNNING) == 0)
		return;

	mii = device_get_softc(sc->bnx_miibus);

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->bnx_link = 1;
			break;
		case IFM_1000_T:
		case IFM_1000_SX:
		case IFM_2500_SX:
			sc->bnx_link = 1;
			break;
		default:
			sc->bnx_link = 0;
			break;
		}
	} else {
		sc->bnx_link = 0;
	}
	if (sc->bnx_link == 0)
		return;

	/*
	 * APE firmware touches these registers to keep the MAC
	 * connected to the outside world.  Try to keep the
	 * accesses atomic.
	 */

	mac_mode = CSR_READ_4(sc, BGE_MAC_MODE) &
	    ~(BGE_MACMODE_PORTMODE | BGE_MACMODE_HALF_DUPLEX);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX)
		mac_mode |= BGE_PORTMODE_GMII;
	else
		mac_mode |= BGE_PORTMODE_MII;

	if ((mii->mii_media_active & IFM_GMASK) != IFM_FDX)
		mac_mode |= BGE_MACMODE_HALF_DUPLEX;

	CSR_WRITE_4(sc, BGE_MAC_MODE, mac_mode);
	DELAY(40);
}

/*
 * Memory management for jumbo frames.
 */
static int
bnx_alloc_jumbo_mem(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bnx_jslot *entry;
	uint8_t *ptr;
	bus_addr_t paddr;
	int i, error;

	/*
	 * Create tag for jumbo mbufs.
	 * This is really a bit of a kludge. We allocate a special
	 * jumbo buffer pool which (thanks to the way our DMA
	 * memory allocation works) will consist of contiguous
	 * pages. This means that even though a jumbo buffer might
	 * be larger than a page size, we don't really need to
	 * map it into more than one DMA segment. However, the
	 * default mbuf tag will result in multi-segment mappings,
	 * so we have to create a special jumbo mbuf tag that
	 * lets us get away with mapping the jumbo buffers as
	 * a single segment. I think eventually the driver should
	 * be changed so that it uses ordinary mbufs and cluster
	 * buffers, i.e. jumbo frames can span multiple DMA
	 * descriptors. But that's a project for another day.
	 */

	/*
	 * Create DMA stuffs for jumbo RX ring.
	 */
	error = bnx_dma_block_alloc(sc, BGE_JUMBO_RX_RING_SZ,
				    &sc->bnx_cdata.bnx_rx_jumbo_ring_tag,
				    &sc->bnx_cdata.bnx_rx_jumbo_ring_map,
				    (void *)&sc->bnx_ldata.bnx_rx_jumbo_ring,
				    &sc->bnx_ldata.bnx_rx_jumbo_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create jumbo RX ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for jumbo buffer block.
	 */
	error = bnx_dma_block_alloc(sc, BNX_JMEM,
				    &sc->bnx_cdata.bnx_jumbo_tag,
				    &sc->bnx_cdata.bnx_jumbo_map,
				    (void **)&sc->bnx_ldata.bnx_jumbo_buf,
				    &paddr);
	if (error) {
		if_printf(ifp, "could not create jumbo buffer\n");
		return error;
	}

	SLIST_INIT(&sc->bnx_jfree_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array. Note that we play an evil trick here by using
	 * the first few bytes in the buffer to hold the the address
	 * of the softc structure for this interface. This is because
	 * bnx_jfree() needs it, but it is called by the mbuf management
	 * code which will not pass it to us explicitly.
	 */
	for (i = 0, ptr = sc->bnx_ldata.bnx_jumbo_buf; i < BNX_JSLOTS; i++) {
		entry = &sc->bnx_cdata.bnx_jslots[i];
		entry->bnx_sc = sc;
		entry->bnx_buf = ptr;
		entry->bnx_paddr = paddr;
		entry->bnx_inuse = 0;
		entry->bnx_slot = i;
		SLIST_INSERT_HEAD(&sc->bnx_jfree_listhead, entry, jslot_link);

		ptr += BNX_JLEN;
		paddr += BNX_JLEN;
	}
	return 0;
}

static void
bnx_free_jumbo_mem(struct bnx_softc *sc)
{
	/* Destroy jumbo RX ring. */
	bnx_dma_block_free(sc->bnx_cdata.bnx_rx_jumbo_ring_tag,
			   sc->bnx_cdata.bnx_rx_jumbo_ring_map,
			   sc->bnx_ldata.bnx_rx_jumbo_ring);

	/* Destroy jumbo buffer block. */
	bnx_dma_block_free(sc->bnx_cdata.bnx_jumbo_tag,
			   sc->bnx_cdata.bnx_jumbo_map,
			   sc->bnx_ldata.bnx_jumbo_buf);
}

/*
 * Allocate a jumbo buffer.
 */
static struct bnx_jslot *
bnx_jalloc(struct bnx_softc *sc)
{
	struct bnx_jslot *entry;

	lwkt_serialize_enter(&sc->bnx_jslot_serializer);
	entry = SLIST_FIRST(&sc->bnx_jfree_listhead);
	if (entry) {
		SLIST_REMOVE_HEAD(&sc->bnx_jfree_listhead, jslot_link);
		entry->bnx_inuse = 1;
	} else {
		if_printf(&sc->arpcom.ac_if, "no free jumbo buffers\n");
	}
	lwkt_serialize_exit(&sc->bnx_jslot_serializer);
	return(entry);
}

/*
 * Adjust usage count on a jumbo buffer.
 */
static void
bnx_jref(void *arg)
{
	struct bnx_jslot *entry = (struct bnx_jslot *)arg;
	struct bnx_softc *sc = entry->bnx_sc;

	if (sc == NULL)
		panic("bnx_jref: can't find softc pointer!");

	if (&sc->bnx_cdata.bnx_jslots[entry->bnx_slot] != entry) {
		panic("bnx_jref: asked to reference buffer "
		    "that we don't manage!");
	} else if (entry->bnx_inuse == 0) {
		panic("bnx_jref: buffer already free!");
	} else {
		atomic_add_int(&entry->bnx_inuse, 1);
	}
}

/*
 * Release a jumbo buffer.
 */
static void
bnx_jfree(void *arg)
{
	struct bnx_jslot *entry = (struct bnx_jslot *)arg;
	struct bnx_softc *sc = entry->bnx_sc;

	if (sc == NULL)
		panic("bnx_jfree: can't find softc pointer!");

	if (&sc->bnx_cdata.bnx_jslots[entry->bnx_slot] != entry) {
		panic("bnx_jfree: asked to free buffer that we don't manage!");
	} else if (entry->bnx_inuse == 0) {
		panic("bnx_jfree: buffer already free!");
	} else {
		/*
		 * Possible MP race to 0, use the serializer.  The atomic insn
		 * is still needed for races against bnx_jref().
		 */
		lwkt_serialize_enter(&sc->bnx_jslot_serializer);
		atomic_subtract_int(&entry->bnx_inuse, 1);
		if (entry->bnx_inuse == 0) {
			SLIST_INSERT_HEAD(&sc->bnx_jfree_listhead, 
					  entry, jslot_link);
		}
		lwkt_serialize_exit(&sc->bnx_jslot_serializer);
	}
}


/*
 * Intialize a standard receive ring descriptor.
 */
static int
bnx_newbuf_std(struct bnx_rx_ret_ring *ret, int i, int init)
{
	struct mbuf *m_new = NULL;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	int error, nsegs;
	struct bnx_rx_buf *rb;

	rb = &ret->bnx_std->bnx_rx_std_buf[i];
	KASSERT(!rb->bnx_rx_refilled, ("RX buf %dth has been refilled", i));

	m_new = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL) {
		error = ENOBUFS;
		goto back;
	}
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	m_adj(m_new, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(ret->bnx_rx_mtag,
	    ret->bnx_rx_tmpmap, m_new, &seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_new);
		goto back;
	}

	if (!init) {
		bus_dmamap_sync(ret->bnx_rx_mtag, rb->bnx_rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(ret->bnx_rx_mtag, rb->bnx_rx_dmamap);
	}

	map = ret->bnx_rx_tmpmap;
	ret->bnx_rx_tmpmap = rb->bnx_rx_dmamap;

	rb->bnx_rx_dmamap = map;
	rb->bnx_rx_mbuf = m_new;
	rb->bnx_rx_paddr = seg.ds_addr;
	rb->bnx_rx_len = m_new->m_len;
back:
	cpu_sfence();
	rb->bnx_rx_refilled = 1;
	return error;
}

static void
bnx_setup_rxdesc_std(struct bnx_rx_std_ring *std, int i)
{
	struct bnx_rx_buf *rb;
	struct bge_rx_bd *r;
	bus_addr_t paddr;
	int len;

	rb = &std->bnx_rx_std_buf[i];
	KASSERT(rb->bnx_rx_refilled, ("RX buf %dth is not refilled", i));

	paddr = rb->bnx_rx_paddr;
	len = rb->bnx_rx_len;

	cpu_mfence();

	rb->bnx_rx_refilled = 0;

	r = &std->bnx_rx_std_ring[i];
	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(paddr);
	r->bge_len = len;
	r->bge_idx = i;
	r->bge_flags = BGE_RXBDFLAG_END;
}

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int
bnx_newbuf_jumbo(struct bnx_softc *sc, int i, int init)
{
	struct mbuf *m_new = NULL;
	struct bnx_jslot *buf;
	bus_addr_t paddr;

	/* Allocate the mbuf. */
	MGETHDR(m_new, init ? M_WAITOK : M_NOWAIT, MT_DATA);
	if (m_new == NULL)
		return ENOBUFS;

	/* Allocate the jumbo buffer */
	buf = bnx_jalloc(sc);
	if (buf == NULL) {
		m_freem(m_new);
		return ENOBUFS;
	}

	/* Attach the buffer to the mbuf. */
	m_new->m_ext.ext_arg = buf;
	m_new->m_ext.ext_buf = buf->bnx_buf;
	m_new->m_ext.ext_free = bnx_jfree;
	m_new->m_ext.ext_ref = bnx_jref;
	m_new->m_ext.ext_size = BNX_JUMBO_FRAMELEN;

	m_new->m_flags |= M_EXT;

	m_new->m_data = m_new->m_ext.ext_buf;
	m_new->m_len = m_new->m_pkthdr.len = m_new->m_ext.ext_size;

	paddr = buf->bnx_paddr;
	m_adj(m_new, ETHER_ALIGN);
	paddr += ETHER_ALIGN;

	/* Save necessary information */
	sc->bnx_cdata.bnx_rx_jumbo_chain[i].bnx_rx_mbuf = m_new;
	sc->bnx_cdata.bnx_rx_jumbo_chain[i].bnx_rx_paddr = paddr;

	/* Set up the descriptor. */
	bnx_setup_rxdesc_jumbo(sc, i);
	return 0;
}

static void
bnx_setup_rxdesc_jumbo(struct bnx_softc *sc, int i)
{
	struct bge_rx_bd *r;
	struct bnx_rx_buf *rc;

	r = &sc->bnx_ldata.bnx_rx_jumbo_ring[i];
	rc = &sc->bnx_cdata.bnx_rx_jumbo_chain[i];

	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(rc->bnx_rx_paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(rc->bnx_rx_paddr);
	r->bge_len = rc->bnx_rx_mbuf->m_len;
	r->bge_idx = i;
	r->bge_flags = BGE_RXBDFLAG_END|BGE_RXBDFLAG_JUMBO_RING;
}

static int
bnx_init_rx_ring_std(struct bnx_rx_std_ring *std)
{
	int i, error;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		/* Use the first RX return ring's tmp RX mbuf DMA map */
		error = bnx_newbuf_std(&std->bnx_sc->bnx_rx_ret_ring[0], i, 1);
		if (error)
			return error;
		bnx_setup_rxdesc_std(std, i);
	}

	std->bnx_rx_std_used = 0;
	std->bnx_rx_std_refill = 0;
	std->bnx_rx_std_running = 0;
	cpu_sfence();
	lwkt_serialize_handler_enable(&std->bnx_rx_std_serialize);

	std->bnx_rx_std = BGE_STD_RX_RING_CNT - 1;
	bnx_writembx(std->bnx_sc, BGE_MBX_RX_STD_PROD_LO, std->bnx_rx_std);

	return(0);
}

static void
bnx_free_rx_ring_std(struct bnx_rx_std_ring *std)
{
	int i;

	lwkt_serialize_handler_disable(&std->bnx_rx_std_serialize);

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		struct bnx_rx_buf *rb = &std->bnx_rx_std_buf[i];

		rb->bnx_rx_refilled = 0;
		if (rb->bnx_rx_mbuf != NULL) {
			bus_dmamap_unload(std->bnx_rx_mtag, rb->bnx_rx_dmamap);
			m_freem(rb->bnx_rx_mbuf);
			rb->bnx_rx_mbuf = NULL;
		}
		bzero(&std->bnx_rx_std_ring[i], sizeof(struct bge_rx_bd));
	}
}

static int
bnx_init_rx_ring_jumbo(struct bnx_softc *sc)
{
	struct bge_rcb *rcb;
	int i, error;

	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		error = bnx_newbuf_jumbo(sc, i, 1);
		if (error)
			return error;
	}

	sc->bnx_jumbo = BGE_JUMBO_RX_RING_CNT - 1;

	rcb = &sc->bnx_ldata.bnx_info.bnx_jumbo_rx_rcb;
	rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(0, 0);
	CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);

	bnx_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, sc->bnx_jumbo);

	return(0);
}

static void
bnx_free_rx_ring_jumbo(struct bnx_softc *sc)
{
	int i;

	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		struct bnx_rx_buf *rc = &sc->bnx_cdata.bnx_rx_jumbo_chain[i];

		if (rc->bnx_rx_mbuf != NULL) {
			m_freem(rc->bnx_rx_mbuf);
			rc->bnx_rx_mbuf = NULL;
		}
		bzero(&sc->bnx_ldata.bnx_rx_jumbo_ring[i],
		    sizeof(struct bge_rx_bd));
	}
}

static void
bnx_free_tx_ring(struct bnx_tx_ring *txr)
{
	int i;

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		struct bnx_tx_buf *buf = &txr->bnx_tx_buf[i];

		if (buf->bnx_tx_mbuf != NULL) {
			bus_dmamap_unload(txr->bnx_tx_mtag,
			    buf->bnx_tx_dmamap);
			m_freem(buf->bnx_tx_mbuf);
			buf->bnx_tx_mbuf = NULL;
		}
		bzero(&txr->bnx_tx_ring[i], sizeof(struct bge_tx_bd));
	}
	txr->bnx_tx_saved_considx = BNX_TXCONS_UNSET;
}

static int
bnx_init_tx_ring(struct bnx_tx_ring *txr)
{
	txr->bnx_tx_cnt = 0;
	txr->bnx_tx_saved_considx = 0;
	txr->bnx_tx_prodidx = 0;

	/* Initialize transmit producer index for host-memory send ring. */
	bnx_writembx(txr->bnx_sc, txr->bnx_tx_mbx, txr->bnx_tx_prodidx);

	return(0);
}

static void
bnx_setmulti(struct bnx_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t hashes[4] = { 0, 0, 0, 0 };
	int h, i;

	ifp = &sc->arpcom.ac_if;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		for (i = 0; i < 4; i++)
			CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), 0xFFFFFFFF);
		return;
	}

	/* First, zot all the existing filters. */
	for (i = 0; i < 4; i++)
		CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), 0);

	/* Now program new ones. */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_le(
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    ETHER_ADDR_LEN) & 0x7f;
		hashes[(h & 0x60) >> 5] |= 1 << (h & 0x1F);
	}

	for (i = 0; i < 4; i++)
		CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), hashes[i]);
}

/*
 * Do endian, PCI and DMA initialization. Also check the on-board ROM
 * self-test results.
 */
static int
bnx_chipinit(struct bnx_softc *sc)
{
	uint32_t dma_rw_ctl, mode_ctl;
	int i;

	/* Set endian type before we access any non-PCI registers. */
	pci_write_config(sc->bnx_dev, BGE_PCI_MISC_CTL,
	    BGE_INIT | BGE_PCIMISCCTL_TAGGED_STATUS, 4);

	/*
	 * Clear the MAC statistics block in the NIC's
	 * internal memory.
	 */
	for (i = BGE_STATS_BLOCK;
	    i < BGE_STATS_BLOCK_END + 1; i += sizeof(uint32_t))
		BNX_MEMWIN_WRITE(sc, i, 0);

	for (i = BGE_STATUS_BLOCK;
	    i < BGE_STATUS_BLOCK_END + 1; i += sizeof(uint32_t))
		BNX_MEMWIN_WRITE(sc, i, 0);

	if (BNX_IS_57765_FAMILY(sc)) {
		uint32_t val;

		if (sc->bnx_chipid == BGE_CHIPID_BCM57765_A0) {
			mode_ctl = CSR_READ_4(sc, BGE_MODE_CTL);
			val = mode_ctl & ~BGE_MODECTL_PCIE_PORTS;

			/* Access the lower 1K of PL PCI-E block registers. */
			CSR_WRITE_4(sc, BGE_MODE_CTL,
			    val | BGE_MODECTL_PCIE_PL_SEL);

			val = CSR_READ_4(sc, BGE_PCIE_PL_LO_PHYCTL5);
			val |= BGE_PCIE_PL_LO_PHYCTL5_DIS_L2CLKREQ;
			CSR_WRITE_4(sc, BGE_PCIE_PL_LO_PHYCTL5, val);

			CSR_WRITE_4(sc, BGE_MODE_CTL, mode_ctl);
		}
		if (sc->bnx_chiprev != BGE_CHIPREV_57765_AX) {
			/* Fix transmit hangs */
			val = CSR_READ_4(sc, BGE_CPMU_PADRNG_CTL);
			val |= BGE_CPMU_PADRNG_CTL_RDIV2;
			CSR_WRITE_4(sc, BGE_CPMU_PADRNG_CTL, val);

			mode_ctl = CSR_READ_4(sc, BGE_MODE_CTL);
			val = mode_ctl & ~BGE_MODECTL_PCIE_PORTS;

			/* Access the lower 1K of DL PCI-E block registers. */
			CSR_WRITE_4(sc, BGE_MODE_CTL,
			    val | BGE_MODECTL_PCIE_DL_SEL);

			val = CSR_READ_4(sc, BGE_PCIE_DL_LO_FTSMAX);
			val &= ~BGE_PCIE_DL_LO_FTSMAX_MASK;
			val |= BGE_PCIE_DL_LO_FTSMAX_VAL;
			CSR_WRITE_4(sc, BGE_PCIE_DL_LO_FTSMAX, val);

			CSR_WRITE_4(sc, BGE_MODE_CTL, mode_ctl);
		}

		val = CSR_READ_4(sc, BGE_CPMU_LSPD_10MB_CLK);
		val &= ~BGE_CPMU_LSPD_10MB_MACCLK_MASK;
		val |= BGE_CPMU_LSPD_10MB_MACCLK_6_25;
		CSR_WRITE_4(sc, BGE_CPMU_LSPD_10MB_CLK, val);
	}

	/*
	 * Set up the PCI DMA control register.
	 */
	dma_rw_ctl = pci_read_config(sc->bnx_dev, BGE_PCI_DMA_RW_CTL, 4);
	/*
	 * Disable 32bytes cache alignment for DMA write to host memory
	 *
	 * NOTE:
	 * 64bytes cache alignment for DMA write to host memory is still
	 * enabled.
	 */
	dma_rw_ctl |= BGE_PCIDMARWCTL_DIS_CACHE_ALIGNMENT;
	if (sc->bnx_chipid == BGE_CHIPID_BCM57765_A0)
		dma_rw_ctl &= ~BGE_PCIDMARWCTL_CRDRDR_RDMA_MRRS_MSK;
	/*
	 * Enable HW workaround for controllers that misinterpret
	 * a status tag update and leave interrupts permanently
	 * disabled.
	 */
	if (sc->bnx_asicrev != BGE_ASICREV_BCM5717 &&
	    sc->bnx_asicrev != BGE_ASICREV_BCM5762 &&
	    !BNX_IS_57765_FAMILY(sc))
		dma_rw_ctl |= BGE_PCIDMARWCTL_TAGGED_STATUS_WA;
	if (bootverbose) {
		if_printf(&sc->arpcom.ac_if, "DMA read/write %#x\n",
		    dma_rw_ctl);
	}
	pci_write_config(sc->bnx_dev, BGE_PCI_DMA_RW_CTL, dma_rw_ctl, 4);

	/*
	 * Set up general mode register.
	 */
	mode_ctl = bnx_dma_swap_options(sc);
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		/* Retain Host-2-BMC settings written by APE firmware. */
		mode_ctl |= CSR_READ_4(sc, BGE_MODE_CTL) &
		    (BGE_MODECTL_BYTESWAP_B2HRX_DATA |
		    BGE_MODECTL_WORDSWAP_B2HRX_DATA |
		    BGE_MODECTL_B2HRX_ENABLE | BGE_MODECTL_HTX2B_ENABLE);
	}
	mode_ctl |= BGE_MODECTL_MAC_ATTN_INTR |
	    BGE_MODECTL_HOST_SEND_BDS | BGE_MODECTL_TX_NO_PHDR_CSUM;
	CSR_WRITE_4(sc, BGE_MODE_CTL, mode_ctl);

	/*
	 * Disable memory write invalidate.  Apparently it is not supported
	 * properly by these devices.  Also ensure that INTx isn't disabled,
	 * as these chips need it even when using MSI.
	 */
	PCI_CLRBIT(sc->bnx_dev, BGE_PCI_CMD,
	    (PCIM_CMD_MWRICEN | PCIM_CMD_INTxDIS), 4);

	/* Set the timer prescaler (always 66Mhz) */
	CSR_WRITE_4(sc, BGE_MISC_CFG, 65 << 1/*BGE_32BITTIME_66MHZ*/);

	return(0);
}

static int
bnx_blockinit(struct bnx_softc *sc)
{
	struct bnx_intr_data *intr;
	struct bge_rcb *rcb;
	bus_size_t vrcb;
	bge_hostaddr taddr;
	uint32_t val;
	int i, limit;

	/*
	 * Initialize the memory window pointer register so that
	 * we can access the first 32K of internal NIC RAM. This will
	 * allow us to set up the TX send ring RCBs and the RX return
	 * ring RCBs, plus other things which live in NIC memory.
	 */
	CSR_WRITE_4(sc, BGE_PCI_MEMWIN_BASEADDR, 0);

	/* Configure mbuf pool watermarks */
	if (BNX_IS_57765_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		if (sc->arpcom.ac_if.if_mtu > ETHERMTU) {
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x7e);
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0xea);
		} else {
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x2a);
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0xa0);
		}
	} else {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x10);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
	}

	/* Configure DMA resource watermarks */
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_LOWAT, 5);
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_HIWAT, 10);

	/* Enable buffer manager */
	val = BGE_BMANMODE_ENABLE | BGE_BMANMODE_LOMBUF_ATTN;
	/*
	 * Change the arbitration algorithm of TXMBUF read request to
	 * round-robin instead of priority based for BCM5719.  When
	 * TXFIFO is almost empty, RDMA will hold its request until
	 * TXFIFO is not almost empty.
	 */
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719)
		val |= BGE_BMANMODE_NO_TX_UNDERRUN;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bnx_chipid == BGE_CHIPID_BCM5719_A0 ||
	    sc->bnx_chipid == BGE_CHIPID_BCM5720_A0)
		val |= BGE_BMANMODE_LOMBUF_ATTN;
	CSR_WRITE_4(sc, BGE_BMAN_MODE, val);

	/* Poll for buffer manager start indication */
	for (i = 0; i < BNX_TIMEOUT; i++) {
		if (CSR_READ_4(sc, BGE_BMAN_MODE) & BGE_BMANMODE_ENABLE)
			break;
		DELAY(10);
	}

	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "buffer manager failed to start\n");
		return(ENXIO);
	}

	/* Enable flow-through queues */
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);

	/* Wait until queue initialization is complete */
	for (i = 0; i < BNX_TIMEOUT; i++) {
		if (CSR_READ_4(sc, BGE_FTQ_RESET) == 0)
			break;
		DELAY(10);
	}

	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "flow-through queue init failed\n");
		return(ENXIO);
	}

	/*
	 * Summary of rings supported by the controller:
	 *
	 * Standard Receive Producer Ring
	 * - This ring is used to feed receive buffers for "standard"
	 *   sized frames (typically 1536 bytes) to the controller.
	 *
	 * Jumbo Receive Producer Ring
	 * - This ring is used to feed receive buffers for jumbo sized
	 *   frames (i.e. anything bigger than the "standard" frames)
	 *   to the controller.
	 *
	 * Mini Receive Producer Ring
	 * - This ring is used to feed receive buffers for "mini"
	 *   sized frames to the controller.
	 * - This feature required external memory for the controller
	 *   but was never used in a production system.  Should always
	 *   be disabled.
	 *
	 * Receive Return Ring
	 * - After the controller has placed an incoming frame into a
	 *   receive buffer that buffer is moved into a receive return
	 *   ring.  The driver is then responsible to passing the
	 *   buffer up to the stack.  BCM5718/BCM57785 families support
	 *   multiple receive return rings.
	 *
	 * Send Ring
	 * - This ring is used for outgoing frames.  BCM5719/BCM5720
	 *   support multiple send rings.
	 */

	/* Initialize the standard receive producer ring control block. */
	rcb = &sc->bnx_ldata.bnx_info.bnx_std_rx_rcb;
	rcb->bge_hostaddr.bge_addr_lo =
	    BGE_ADDR_LO(sc->bnx_rx_std_ring.bnx_rx_std_ring_paddr);
	rcb->bge_hostaddr.bge_addr_hi =
	    BGE_ADDR_HI(sc->bnx_rx_std_ring.bnx_rx_std_ring_paddr);
	if (BNX_IS_57765_PLUS(sc)) {
		/*
		 * Bits 31-16: Programmable ring size (2048, 1024, 512, .., 32)
		 * Bits 15-2 : Maximum RX frame size
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring ENabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(512, BNX_MAX_FRAMELEN << 2);
	} else {
		/*
		 * Bits 31-16: Programmable ring size (512, 256, 128, 64, 32)
		 * Bits 15-2 : Reserved (should be 0)
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring Enabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(512, 0);
	}
	if (BNX_IS_5717_PLUS(sc))
		rcb->bge_nicaddr = BGE_STD_RX_RINGS_5717;
	else
		rcb->bge_nicaddr = BGE_STD_RX_RINGS;
	/* Write the standard receive producer ring control block. */
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
	if (!BNX_IS_5717_PLUS(sc))
		CSR_WRITE_4(sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);
	/* Reset the standard receive producer ring producer index. */
	bnx_writembx(sc, BGE_MBX_RX_STD_PROD_LO, 0);

	/*
	 * Initialize the jumbo RX producer ring control
	 * block.  We set the 'ring disabled' bit in the
	 * flags field until we're actually ready to start
	 * using this ring (i.e. once we set the MTU
	 * high enough to require it).
	 */
	if (BNX_IS_JUMBO_CAPABLE(sc)) {
		rcb = &sc->bnx_ldata.bnx_info.bnx_jumbo_rx_rcb;
		/* Get the jumbo receive producer ring RCB parameters. */
		rcb->bge_hostaddr.bge_addr_lo =
		    BGE_ADDR_LO(sc->bnx_ldata.bnx_rx_jumbo_ring_paddr);
		rcb->bge_hostaddr.bge_addr_hi =
		    BGE_ADDR_HI(sc->bnx_ldata.bnx_rx_jumbo_ring_paddr);
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(BNX_MAX_FRAMELEN,
		    BGE_RCB_FLAG_RING_DISABLED);
		if (BNX_IS_5717_PLUS(sc))
			rcb->bge_nicaddr = BGE_JUMBO_RX_RINGS_5717;
		else
			rcb->bge_nicaddr = BGE_JUMBO_RX_RINGS;
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_HI,
		    rcb->bge_hostaddr.bge_addr_hi);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_LO,
		    rcb->bge_hostaddr.bge_addr_lo);
		/* Program the jumbo receive producer ring RCB parameters. */
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_NICADDR, rcb->bge_nicaddr);
		/* Reset the jumbo receive producer ring producer index. */
		bnx_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, 0);
	}

	/*
	 * The BD ring replenish thresholds control how often the
	 * hardware fetches new BD's from the producer rings in host
	 * memory.  Setting the value too low on a busy system can
	 * starve the hardware and recue the throughpout.
	 *
	 * Set the BD ring replentish thresholds. The recommended
	 * values are 1/8th the number of descriptors allocated to
	 * each ring.
	 */
	val = 8;
	CSR_WRITE_4(sc, BGE_RBDI_STD_REPL_THRESH, val);
	if (BNX_IS_JUMBO_CAPABLE(sc)) {
		CSR_WRITE_4(sc, BGE_RBDI_JUMBO_REPL_THRESH,
		    BGE_JUMBO_RX_RING_CNT/8);
	}
	if (BNX_IS_57765_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_STD_REPLENISH_LWM, 32);
		CSR_WRITE_4(sc, BGE_JMB_REPLENISH_LWM, 16);
	}

	/*
	 * Disable all send rings by setting the 'ring disabled' bit
	 * in the flags field of all the TX send ring control blocks,
	 * located in NIC memory.
	 */
	if (BNX_IS_5717_PLUS(sc))
		limit = 4;
	else if (BNX_IS_57765_FAMILY(sc) ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762)
		limit = 2;
	else
		limit = 1;
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED));
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Configure send ring RCBs
	 */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];

		BGE_HOSTADDR(taddr, txr->bnx_tx_ring_paddr);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi,
		    taddr.bge_addr_hi);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo,
		    taddr.bge_addr_lo);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(BGE_TX_RING_CNT, 0));
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Disable all receive return rings by setting the
	 * 'ring disabled' bit in the flags field of all the receive
	 * return ring control blocks, located in NIC memory.
	 */
	if (BNX_IS_5717_PLUS(sc)) {
		/* Should be 17, use 16 until we get an SRAM map. */
		limit = 16;
	} else if (BNX_IS_57765_FAMILY(sc) ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		limit = 4;
	} else {
		limit = 1;
	}
	/* Disable all receive return rings. */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, 0);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, 0);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_FLAG_RING_DISABLED);
		bnx_writembx(sc, BGE_MBX_RX_CONS0_LO +
		    (i * (sizeof(uint64_t))), 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Set up receive return rings.
	 */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
		struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[i];

		BGE_HOSTADDR(taddr, ret->bnx_rx_ret_ring_paddr);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi,
		    taddr.bge_addr_hi);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo,
		    taddr.bge_addr_lo);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(BNX_RETURN_RING_CNT, 0));
		vrcb += sizeof(struct bge_rcb);
	}

	/* Set random backoff seed for TX */
	CSR_WRITE_4(sc, BGE_TX_RANDOM_BACKOFF,
	    (sc->arpcom.ac_enaddr[0] + sc->arpcom.ac_enaddr[1] +
	     sc->arpcom.ac_enaddr[2] + sc->arpcom.ac_enaddr[3] +
	     sc->arpcom.ac_enaddr[4] + sc->arpcom.ac_enaddr[5]) &
	    BGE_TX_BACKOFF_SEED_MASK);

	/* Set inter-packet gap */
	val = 0x2620;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		val |= CSR_READ_4(sc, BGE_TX_LENGTHS) &
		    (BGE_TXLEN_JMB_FRM_LEN_MSK | BGE_TXLEN_CNT_DN_VAL_MSK);
	}
	CSR_WRITE_4(sc, BGE_TX_LENGTHS, val);

	/*
	 * Specify which ring to use for packets that don't match
	 * any RX rules.
	 */
	CSR_WRITE_4(sc, BGE_RX_RULES_CFG, 0x08);

	/*
	 * Configure number of RX lists. One interrupt distribution
	 * list, sixteen active lists, one bad frames class.
	 */
	CSR_WRITE_4(sc, BGE_RXLP_CFG, 0x181);

	/* Inialize RX list placement stats mask. */
	CSR_WRITE_4(sc, BGE_RXLP_STATS_ENABLE_MASK, 0x007FFFFF);
	CSR_WRITE_4(sc, BGE_RXLP_STATS_CTL, 0x1);

	/* Disable host coalescing until we get it set up */
	CSR_WRITE_4(sc, BGE_HCC_MODE, 0x00000000);

	/* Poll to make sure it's shut down. */
	for (i = 0; i < BNX_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, BGE_HCC_MODE) & BGE_HCCMODE_ENABLE))
			break;
		DELAY(10);
	}

	if (i == BNX_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "host coalescing engine failed to idle\n");
		return(ENXIO);
	}

	/* Set up host coalescing defaults */
	sc->bnx_coal_chg = BNX_RX_COAL_TICKS_CHG |
	    BNX_TX_COAL_TICKS_CHG |
	    BNX_RX_COAL_BDS_CHG |
	    BNX_TX_COAL_BDS_CHG |
	    BNX_RX_COAL_BDS_INT_CHG |
	    BNX_TX_COAL_BDS_INT_CHG;
	bnx_coal_change(sc);

	/*
	 * Set up addresses of status blocks
	 */
	intr = &sc->bnx_intr_data[0];
	bzero(intr->bnx_status_block, BGE_STATUS_BLK_SZ);
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_HI,
	    BGE_ADDR_HI(intr->bnx_status_block_paddr));
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_LO,
	    BGE_ADDR_LO(intr->bnx_status_block_paddr));
	for (i = 1; i < sc->bnx_intr_cnt; ++i) {
		intr = &sc->bnx_intr_data[i];
		bzero(intr->bnx_status_block, BGE_STATUS_BLK_SZ);
		CSR_WRITE_4(sc, BGE_VEC1_STATUSBLK_ADDR_HI + ((i - 1) * 8),
		    BGE_ADDR_HI(intr->bnx_status_block_paddr));
		CSR_WRITE_4(sc, BGE_VEC1_STATUSBLK_ADDR_LO + ((i - 1) * 8),
		    BGE_ADDR_LO(intr->bnx_status_block_paddr));
	}

	/* Set up status block partail update size. */
	val = BGE_STATBLKSZ_32BYTE;
#if 0
	/*
	 * Does not seem to have visible effect in both
	 * bulk data (1472B UDP datagram) and tiny data
	 * (18B UDP datagram) TX tests.
	 */
	val |= BGE_HCCMODE_CLRTICK_TX;
#endif
	/* Turn on host coalescing state machine */
	CSR_WRITE_4(sc, BGE_HCC_MODE, val | BGE_HCCMODE_ENABLE);

	/* Turn on RX BD completion state machine and enable attentions */
	CSR_WRITE_4(sc, BGE_RBDC_MODE,
	    BGE_RBDCMODE_ENABLE|BGE_RBDCMODE_ATTN);

	/* Turn on RX list placement state machine */
	CSR_WRITE_4(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);

	val = BGE_MACMODE_TXDMA_ENB | BGE_MACMODE_RXDMA_ENB |
	    BGE_MACMODE_RX_STATS_CLEAR | BGE_MACMODE_TX_STATS_CLEAR |
	    BGE_MACMODE_RX_STATS_ENB | BGE_MACMODE_TX_STATS_ENB |
	    BGE_MACMODE_FRMHDR_DMA_ENB;

	if (sc->bnx_flags & BNX_FLAG_TBI)
		val |= BGE_PORTMODE_TBI;
	else if (sc->bnx_flags & BNX_FLAG_MII_SERDES)
		val |= BGE_PORTMODE_GMII;
	else
		val |= BGE_PORTMODE_MII;

	/* Allow APE to send/receive frames. */
	if (sc->bnx_mfw_flags & BNX_MFW_ON_APE)
		val |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;

	/* Turn on DMA, clear stats */
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	/* Set misc. local control, enable interrupts on attentions */
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

#ifdef notdef
	/* Assert GPIO pins for PHY reset */
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUT0|
	    BGE_MLC_MISCIO_OUT1|BGE_MLC_MISCIO_OUT2);
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUTEN0|
	    BGE_MLC_MISCIO_OUTEN1|BGE_MLC_MISCIO_OUTEN2);
#endif

	if (sc->bnx_intr_type == PCI_INTR_TYPE_MSIX)
		bnx_enable_msi(sc, TRUE);

	/* Turn on write DMA state machine */
	val = BGE_WDMAMODE_ENABLE|BGE_WDMAMODE_ALL_ATTNS;
	/* Enable host coalescing bug fix. */
	val |= BGE_WDMAMODE_STATUS_TAG_FIX;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5785) {
		/* Request larger DMA burst size to get better performance. */
		val |= BGE_WDMAMODE_BURST_ALL_DATA;
	}
	CSR_WRITE_4(sc, BGE_WDMA_MODE, val);
	DELAY(40);

	if (BNX_IS_57765_PLUS(sc)) {
		uint32_t dmactl, dmactl_reg;

		if (sc->bnx_asicrev == BGE_ASICREV_BCM5762)
			dmactl_reg = BGE_RDMA_RSRVCTRL2;
		else
			dmactl_reg = BGE_RDMA_RSRVCTRL;

		dmactl = CSR_READ_4(sc, dmactl_reg);
		/*
		 * Adjust tx margin to prevent TX data corruption and
		 * fix internal FIFO overflow.
		 */
		if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
		    sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
		    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
			dmactl &= ~(BGE_RDMA_RSRVCTRL_FIFO_LWM_MASK |
			    BGE_RDMA_RSRVCTRL_FIFO_HWM_MASK |
			    BGE_RDMA_RSRVCTRL_TXMRGN_MASK);
			dmactl |= BGE_RDMA_RSRVCTRL_FIFO_LWM_1_5K |
			    BGE_RDMA_RSRVCTRL_FIFO_HWM_1_5K |
			    BGE_RDMA_RSRVCTRL_TXMRGN_320B;
		}
		/*
		 * Enable fix for read DMA FIFO overruns.
		 * The fix is to limit the number of RX BDs
		 * the hardware would fetch at a fime.
		 */
		CSR_WRITE_4(sc, dmactl_reg,
		    dmactl | BGE_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
	}

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719) {
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_4K |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		uint32_t ctrl_reg;

		if (sc->bnx_asicrev == BGE_ASICREV_BCM5762)
			ctrl_reg = BGE_RDMA_LSO_CRPTEN_CTRL2;
		else
			ctrl_reg = BGE_RDMA_LSO_CRPTEN_CTRL;

		/*
		 * Allow 4KB burst length reads for non-LSO frames.
		 * Enable 512B burst length reads for buffer descriptors.
		 */
		CSR_WRITE_4(sc, ctrl_reg,
		    CSR_READ_4(sc, ctrl_reg) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_512 |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	}

	/* Turn on read DMA state machine */
	val = BGE_RDMAMODE_ENABLE | BGE_RDMAMODE_ALL_ATTNS;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717)
		val |= BGE_RDMAMODE_MULT_DMA_RD_DIS;
        if (sc->bnx_asicrev == BGE_ASICREV_BCM5784 ||
            sc->bnx_asicrev == BGE_ASICREV_BCM5785 ||
            sc->bnx_asicrev == BGE_ASICREV_BCM57780) {
		val |= BGE_RDMAMODE_BD_SBD_CRPT_ATTN |
		    BGE_RDMAMODE_MBUF_RBD_CRPT_ATTN |
		    BGE_RDMAMODE_MBUF_SBD_CRPT_ATTN;
	}
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		val |= CSR_READ_4(sc, BGE_RDMA_MODE) &
		    BGE_RDMAMODE_H2BNC_VLAN_DET;
		/*
		 * Allow multiple outstanding read requests from
		 * non-LSO read DMA engine.
		 */
		val &= ~BGE_RDMAMODE_MULT_DMA_RD_DIS;
	}
	if (sc->bnx_asicrev == BGE_ASICREV_BCM57766)
		val |= BGE_RDMAMODE_JMB_2K_MMRR;
	if (sc->bnx_flags & BNX_FLAG_TSO)
		val |= BGE_RDMAMODE_TSO4_ENABLE;
	val |= BGE_RDMAMODE_FIFO_LONG_BURST;
	CSR_WRITE_4(sc, BGE_RDMA_MODE, val);
	DELAY(40);

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
	    	uint32_t thresh;

		thresh = ETHERMTU_JUMBO;
		if (sc->bnx_chipid == BGE_CHIPID_BCM5719_A0)
			thresh = ETHERMTU;

		for (i = 0; i < BGE_RDMA_NCHAN; ++i) {
			if (CSR_READ_4(sc, BGE_RDMA_LENGTH + (i << 2)) > thresh)
				break;
		}
		if (i < BGE_RDMA_NCHAN) {
			if (bootverbose) {
				if_printf(&sc->arpcom.ac_if,
				    "enable RDMA WA\n");
			}
			if (sc->bnx_asicrev == BGE_ASICREV_BCM5719)
				sc->bnx_rdma_wa = BGE_RDMA_TX_LENGTH_WA_5719;
			else
				sc->bnx_rdma_wa = BGE_RDMA_TX_LENGTH_WA_5720;
			CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
			    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
			    sc->bnx_rdma_wa);
		} else {
			sc->bnx_rdma_wa = 0;
		}
	}

	/* Turn on RX data completion state machine */
	CSR_WRITE_4(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);

	/* Turn on RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);

	/* Turn on RX data and RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RDBDI_MODE, BGE_RDBDIMODE_ENABLE);

	/* Turn on send BD completion state machine */
	CSR_WRITE_4(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/* Turn on send data completion state machine */
	val = BGE_SDCMODE_ENABLE;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5761)
		val |= BGE_SDCMODE_CDELAY; 
	CSR_WRITE_4(sc, BGE_SDC_MODE, val);

	/* Turn on send data initiator state machine */
	if (sc->bnx_flags & BNX_FLAG_TSO) {
		CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE |
		    BGE_SDIMODE_HW_LSO_PRE_DMA);
	} else {
		CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
	}

	/* Turn on send BD initiator state machine */
	val = BGE_SBDIMODE_ENABLE;
	if (sc->bnx_tx_ringcnt > 1)
		val |= BGE_SBDIMODE_MULTI_TXR;
	CSR_WRITE_4(sc, BGE_SBDI_MODE, val);

	/* Turn on send BD selector state machine */
	CSR_WRITE_4(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);

	CSR_WRITE_4(sc, BGE_SDI_STATS_ENABLE_MASK, 0x007FFFFF);
	CSR_WRITE_4(sc, BGE_SDI_STATS_CTL,
	    BGE_SDISTATSCTL_ENABLE|BGE_SDISTATSCTL_FASTER);

	/* ack/clear link change events */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED|
	    BGE_MACSTAT_CFG_CHANGED|BGE_MACSTAT_MI_COMPLETE|
	    BGE_MACSTAT_LINK_CHANGED);
	CSR_WRITE_4(sc, BGE_MI_STS, 0);

	/*
	 * Enable attention when the link has changed state for
	 * devices that use auto polling.
	 */
	if (sc->bnx_flags & BNX_FLAG_TBI) {
		CSR_WRITE_4(sc, BGE_MI_STS, BGE_MISTS_LINK);
 	} else {
		if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
			CSR_WRITE_4(sc, BGE_MI_MODE, sc->bnx_mi_mode);
			DELAY(80);
		}
	}

	/*
	 * Clear any pending link state attention.
	 * Otherwise some link state change events may be lost until attention
	 * is cleared by bnx_intr() -> bnx_softc.bnx_link_upd() sequence.
	 * It's not necessary on newer BCM chips - perhaps enabling link
	 * state change attentions implies clearing pending attention.
	 */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED|
	    BGE_MACSTAT_CFG_CHANGED|BGE_MACSTAT_MI_COMPLETE|
	    BGE_MACSTAT_LINK_CHANGED);

	/* Enable link state change attentions. */
	BNX_SETBIT(sc, BGE_MAC_EVT_ENB, BGE_EVTENB_LINK_CHANGED);

	return(0);
}

/*
 * Probe for a Broadcom chip. Check the PCI vendor and device IDs
 * against our list and return its name if we find a match. Note
 * that since the Broadcom controller contains VPD support, we
 * can get the device name string from the controller itself instead
 * of the compiled-in string. This is a little slow, but it guarantees
 * we'll always announce the right product name.
 */
static int
bnx_probe(device_t dev)
{
	const struct bnx_type *t;
	uint16_t product, vendor;

	if (!pci_is_pcie(dev))
		return ENXIO;

	product = pci_get_device(dev);
	vendor = pci_get_vendor(dev);

	for (t = bnx_devs; t->bnx_name != NULL; t++) {
		if (vendor == t->bnx_vid && product == t->bnx_did)
			break;
	}
	if (t->bnx_name == NULL)
		return ENXIO;

	device_set_desc(dev, t->bnx_name);
	return 0;
}

static int
bnx_attach(device_t dev)
{
	struct ifnet *ifp;
	struct bnx_softc *sc;
	struct bnx_rx_std_ring *std;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;
	uint32_t hwcfg = 0;
	int error = 0, rid, capmask, i, std_cpuid, std_cpuid_def;
	uint8_t ether_addr[ETHER_ADDR_LEN];
	uint16_t product;
	uintptr_t mii_priv = 0;
#if defined(BNX_TSO_DEBUG) || defined(BNX_RSS_DEBUG) || defined(BNX_TSS_DEBUG)
	char desc[32];
#endif

	sc = device_get_softc(dev);
	sc->bnx_dev = dev;
	callout_init_mp(&sc->bnx_tick_timer);
	lwkt_serialize_init(&sc->bnx_jslot_serializer);
	lwkt_serialize_init(&sc->bnx_main_serialize);

	/* Always setup interrupt mailboxes */
	for (i = 0; i < BNX_INTR_MAX; ++i) {
		callout_init_mp(&sc->bnx_intr_data[i].bnx_intr_timer);
		sc->bnx_intr_data[i].bnx_sc = sc;
		sc->bnx_intr_data[i].bnx_intr_mbx = BGE_MBX_IRQ0_LO + (i * 8);
		sc->bnx_intr_data[i].bnx_intr_rid = -1;
		sc->bnx_intr_data[i].bnx_intr_cpuid = -1;
	}

	sc->bnx_func_addr = pci_get_function(dev);
	product = pci_get_device(dev);

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t irq, mem;

		irq = pci_read_config(dev, PCIR_INTLINE, 4);
		mem = pci_read_config(dev, BGE_PCI_BAR0, 4);

		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, PCIR_INTLINE, irq, 4);
		pci_write_config(dev, BGE_PCI_BAR0, mem, 4);
	}
#endif	/* !BURN_BRIDGE */

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	rid = BGE_PCI_BAR0;
	sc->bnx_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);

	if (sc->bnx_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		return ENXIO;
	}

	sc->bnx_btag = rman_get_bustag(sc->bnx_res);
	sc->bnx_bhandle = rman_get_bushandle(sc->bnx_res);

	/* Save various chip information */
	sc->bnx_chipid =
	    pci_read_config(dev, BGE_PCI_MISC_CTL, 4) >>
	    BGE_PCIMISCCTL_ASICREV_SHIFT;
	if (BGE_ASICREV(sc->bnx_chipid) == BGE_ASICREV_USE_PRODID_REG) {
		/* All chips having dedicated ASICREV register have CPMU */
		sc->bnx_flags |= BNX_FLAG_CPMU;

		switch (product) {
		case PCI_PRODUCT_BROADCOM_BCM5717:
		case PCI_PRODUCT_BROADCOM_BCM5717C:
		case PCI_PRODUCT_BROADCOM_BCM5718:
		case PCI_PRODUCT_BROADCOM_BCM5719:
		case PCI_PRODUCT_BROADCOM_BCM5720_ALT:
		case PCI_PRODUCT_BROADCOM_BCM5725:
		case PCI_PRODUCT_BROADCOM_BCM5727:
		case PCI_PRODUCT_BROADCOM_BCM5762:
		case PCI_PRODUCT_BROADCOM_BCM57764:
		case PCI_PRODUCT_BROADCOM_BCM57767:
		case PCI_PRODUCT_BROADCOM_BCM57787:
			sc->bnx_chipid = pci_read_config(dev,
			    BGE_PCI_GEN2_PRODID_ASICREV, 4);
			break;

		case PCI_PRODUCT_BROADCOM_BCM57761:
		case PCI_PRODUCT_BROADCOM_BCM57762:
		case PCI_PRODUCT_BROADCOM_BCM57765:
		case PCI_PRODUCT_BROADCOM_BCM57766:
		case PCI_PRODUCT_BROADCOM_BCM57781:
		case PCI_PRODUCT_BROADCOM_BCM57782:
		case PCI_PRODUCT_BROADCOM_BCM57785:
		case PCI_PRODUCT_BROADCOM_BCM57786:
		case PCI_PRODUCT_BROADCOM_BCM57791:
		case PCI_PRODUCT_BROADCOM_BCM57795:
			sc->bnx_chipid = pci_read_config(dev,
			    BGE_PCI_GEN15_PRODID_ASICREV, 4);
			break;

		default:
			sc->bnx_chipid = pci_read_config(dev,
			    BGE_PCI_PRODID_ASICREV, 4);
			break;
		}
	}
	if (sc->bnx_chipid == BGE_CHIPID_BCM5717_C0)
		sc->bnx_chipid = BGE_CHIPID_BCM5720_A0;

	sc->bnx_asicrev = BGE_ASICREV(sc->bnx_chipid);
	sc->bnx_chiprev = BGE_CHIPREV(sc->bnx_chipid);

	switch (sc->bnx_asicrev) {
	case BGE_ASICREV_BCM5717:
	case BGE_ASICREV_BCM5719:
	case BGE_ASICREV_BCM5720:
		sc->bnx_flags |= BNX_FLAG_5717_PLUS | BNX_FLAG_57765_PLUS;
		break;

	case BGE_ASICREV_BCM5762:
		sc->bnx_flags |= BNX_FLAG_57765_PLUS;
		break;

	case BGE_ASICREV_BCM57765:
	case BGE_ASICREV_BCM57766:
		sc->bnx_flags |= BNX_FLAG_57765_FAMILY | BNX_FLAG_57765_PLUS;
		break;
	}

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762)
		sc->bnx_flags |= BNX_FLAG_APE;

	sc->bnx_flags |= BNX_FLAG_TSO;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 &&
	    sc->bnx_chipid == BGE_CHIPID_BCM5719_A0)
		sc->bnx_flags &= ~BNX_FLAG_TSO;

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717 ||
	    BNX_IS_57765_FAMILY(sc)) {
		/*
		 * All BCM57785 and BCM5718 families chips have a bug that
		 * under certain situation interrupt will not be enabled
		 * even if status tag is written to interrupt mailbox.
		 *
		 * While BCM5719 and BCM5720 have a hardware workaround
		 * which could fix the above bug.
		 * See the comment near BGE_PCIDMARWCTL_TAGGED_STATUS_WA in
		 * bnx_chipinit().
		 *
		 * For the rest of the chips in these two families, we will
		 * have to poll the status block at high rate (10ms currently)
		 * to check whether the interrupt is hosed or not.
		 * See bnx_check_intr_*() for details.
		 */
		sc->bnx_flags |= BNX_FLAG_STATUSTAG_BUG;
	}

	sc->bnx_pciecap = pci_get_pciecap_ptr(sc->bnx_dev);
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5720)
		pcie_set_max_readrq(dev, PCIEM_DEVCTL_MAX_READRQ_2048);
	else
		pcie_set_max_readrq(dev, PCIEM_DEVCTL_MAX_READRQ_4096);
	device_printf(dev, "CHIP ID 0x%08x; "
		      "ASIC REV 0x%02x; CHIP REV 0x%02x\n",
		      sc->bnx_chipid, sc->bnx_asicrev, sc->bnx_chiprev);

	/*
	 * Set various PHY quirk flags.
	 */

	capmask = MII_CAPMASK_DEFAULT;
	if (product == PCI_PRODUCT_BROADCOM_BCM57791 ||
	    product == PCI_PRODUCT_BROADCOM_BCM57795) {
		/* 10/100 only */
		capmask &= ~BMSR_EXTSTAT;
	}

	mii_priv |= BRGPHY_FLAG_WIRESPEED;
	if (sc->bnx_chipid == BGE_CHIPID_BCM5762_A0)
		mii_priv |= BRGPHY_FLAG_5762_A0;

	/*
	 * Chips with APE need BAR2 access for APE registers/memory.
	 */
	if (sc->bnx_flags & BNX_FLAG_APE) {
		uint32_t pcistate;

		rid = PCIR_BAR(2);
		sc->bnx_res2 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (sc->bnx_res2 == NULL) {
			device_printf(dev, "couldn't map BAR2 memory\n");
			error = ENXIO;
			goto fail;
		}

		/* Enable APE register/memory access by host driver. */
		pcistate = pci_read_config(dev, BGE_PCI_PCISTATE, 4);
		pcistate |= BGE_PCISTATE_ALLOW_APE_CTLSPC_WR |
		    BGE_PCISTATE_ALLOW_APE_SHMEM_WR |
		    BGE_PCISTATE_ALLOW_APE_PSPACE_WR;
		pci_write_config(dev, BGE_PCI_PCISTATE, pcistate, 4);

		bnx_ape_lock_init(sc);
		bnx_ape_read_fw_ver(sc);
	}

	/* Initialize if_name earlier, so if_printf could be used */
	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/*
	 * Try to reset the chip.
	 */
	bnx_sig_pre_reset(sc, BNX_RESET_SHUTDOWN);
	bnx_reset(sc);
	bnx_sig_post_reset(sc, BNX_RESET_SHUTDOWN);

	if (bnx_chipinit(sc)) {
		device_printf(dev, "chip initialization failed\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Get station address
	 */
	error = bnx_get_eaddr(sc, ether_addr);
	if (error) {
		device_printf(dev, "failed to read station address\n");
		goto fail;
	}

	/* Setup RX/TX and interrupt count */
	bnx_setup_ring_cnt(sc);

	if ((sc->bnx_rx_retcnt == 1 && sc->bnx_tx_ringcnt == 1) ||
	    (sc->bnx_rx_retcnt > 1 && sc->bnx_tx_ringcnt > 1)) {
	    	/*
		 * The RX ring and the corresponding TX ring processing
		 * should be on the same CPU, since they share the same
		 * status block.
		 */
		sc->bnx_flags |= BNX_FLAG_RXTX_BUNDLE;
		if (bootverbose)
			device_printf(dev, "RX/TX bundle\n");
		if (sc->bnx_tx_ringcnt > 1) {
			/*
			 * Multiple TX rings do not share status block
			 * with link status, so link status will have
			 * to save its own status_tag.
			 */
			sc->bnx_flags |= BNX_FLAG_STATUS_HASTAG;
			if (bootverbose)
				device_printf(dev, "status needs tag\n");
		}
	} else {
		KKASSERT(sc->bnx_rx_retcnt > 1 && sc->bnx_tx_ringcnt == 1);
		if (bootverbose)
			device_printf(dev, "RX/TX not bundled\n");
	}

	error = bnx_dma_alloc(dev);
	if (error)
		goto fail;

	/*
	 * Allocate interrupt
	 */
	error = bnx_alloc_intr(sc);
	if (error)
		goto fail;

	/* Setup serializers */
	bnx_setup_serialize(sc);

	/* Set default tuneable values. */
	sc->bnx_rx_coal_ticks = BNX_RX_COAL_TICKS_DEF;
	sc->bnx_tx_coal_ticks = BNX_TX_COAL_TICKS_DEF;
	sc->bnx_rx_coal_bds = BNX_RX_COAL_BDS_DEF;
	sc->bnx_rx_coal_bds_poll = sc->bnx_rx_ret_ring[0].bnx_rx_cntmax;
	sc->bnx_tx_coal_bds = BNX_TX_COAL_BDS_DEF;
	sc->bnx_tx_coal_bds_poll = BNX_TX_COAL_BDS_POLL_DEF;
	sc->bnx_rx_coal_bds_int = BNX_RX_COAL_BDS_INT_DEF;
	sc->bnx_tx_coal_bds_int = BNX_TX_COAL_BDS_INT_DEF;

	/* Set up ifnet structure */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bnx_ioctl;
	ifp->if_start = bnx_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = bnx_npoll;
#endif
	ifp->if_init = bnx_init;
	ifp->if_serialize = bnx_serialize;
	ifp->if_deserialize = bnx_deserialize;
	ifp->if_tryserialize = bnx_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = bnx_serialize_assert;
#endif
	ifp->if_mtu = ETHERMTU;
	ifp->if_capabilities = IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;

	ifp->if_capabilities |= IFCAP_HWCSUM;
	ifp->if_hwassist = BNX_CSUM_FEATURES;
	if (sc->bnx_flags & BNX_FLAG_TSO) {
		ifp->if_capabilities |= IFCAP_TSO;
		ifp->if_hwassist |= CSUM_TSO;
	}
	if (BNX_RSS_ENABLED(sc))
		ifp->if_capabilities |= IFCAP_RSS;
	ifp->if_capenable = ifp->if_capabilities;

	ifp->if_nmbclusters = BGE_STD_RX_RING_CNT;

	ifq_set_maxlen(&ifp->if_snd, BGE_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->bnx_tx_ringcnt);

	if (sc->bnx_tx_ringcnt > 1) {
		ifp->if_mapsubq = ifq_mapsubq_modulo;
		ifq_set_subq_divisor(&ifp->if_snd, sc->bnx_tx_ringcnt);
	}

	/*
	 * Figure out what sort of media we have by checking the
	 * hardware config word in the first 32k of NIC internal memory,
	 * or fall back to examining the EEPROM if necessary.
	 * Note: on some BCM5700 cards, this value appears to be unset.
	 * If that's the case, we have to rely on identifying the NIC
	 * by its PCI subsystem ID, as we do below for the SysKonnect
	 * SK-9D41.
	 */
	if (bnx_readmem_ind(sc, BGE_SRAM_DATA_SIG) == BGE_SRAM_DATA_SIG_MAGIC) {
		hwcfg = bnx_readmem_ind(sc, BGE_SRAM_DATA_CFG);
	} else {
		if (bnx_read_eeprom(sc, (caddr_t)&hwcfg, BGE_EE_HWCFG_OFFSET,
				    sizeof(hwcfg))) {
			device_printf(dev, "failed to read EEPROM\n");
			error = ENXIO;
			goto fail;
		}
		hwcfg = ntohl(hwcfg);
	}

	/* The SysKonnect SK-9D41 is a 1000baseSX card. */
	if (pci_get_subvendor(dev) == PCI_PRODUCT_SCHNEIDERKOCH_SK_9D41 ||
	    (hwcfg & BGE_HWCFG_MEDIA) == BGE_MEDIA_FIBER)
		sc->bnx_flags |= BNX_FLAG_TBI;

	/* Setup MI MODE */
	if (sc->bnx_flags & BNX_FLAG_CPMU)
		sc->bnx_mi_mode = BGE_MIMODE_500KHZ_CONST;
	else
		sc->bnx_mi_mode = BGE_MIMODE_BASE;

	/* Setup link status update stuffs */
	if (sc->bnx_flags & BNX_FLAG_TBI) {
		sc->bnx_link_upd = bnx_tbi_link_upd;
		sc->bnx_link_chg = BGE_MACSTAT_LINK_CHANGED;
	} else if (sc->bnx_mi_mode & BGE_MIMODE_AUTOPOLL) {
		sc->bnx_link_upd = bnx_autopoll_link_upd;
		sc->bnx_link_chg = BGE_MACSTAT_LINK_CHANGED;
	} else {
		sc->bnx_link_upd = bnx_copper_link_upd;
		sc->bnx_link_chg = BGE_MACSTAT_LINK_CHANGED;
	}

	/* Set default PHY address */
	sc->bnx_phyno = 1;

	/*
	 * PHY address mapping for various devices.
	 *
	 *          | F0 Cu | F0 Sr | F1 Cu | F1 Sr |
	 * ---------+-------+-------+-------+-------+
	 * BCM57XX  |   1   |   X   |   X   |   X   |
	 * BCM5717  |   1   |   8   |   2   |   9   |
	 * BCM5719  |   1   |   8   |   2   |   9   |
	 * BCM5720  |   1   |   8   |   2   |   9   |
	 *
	 *          | F2 Cu | F2 Sr | F3 Cu | F3 Sr |
	 * ---------+-------+-------+-------+-------+
	 * BCM57XX  |   X   |   X   |   X   |   X   |
	 * BCM5717  |   X   |   X   |   X   |   X   |
	 * BCM5719  |   3   |   10  |   4   |   11  |
	 * BCM5720  |   X   |   X   |   X   |   X   |
	 *
	 * Other addresses may respond but they are not
	 * IEEE compliant PHYs and should be ignored.
	 */
	if (BNX_IS_5717_PLUS(sc)) {
		if (sc->bnx_chipid == BGE_CHIPID_BCM5717_A0) {
			if (CSR_READ_4(sc, BGE_SGDIG_STS) &
			    BGE_SGDIGSTS_IS_SERDES)
				sc->bnx_phyno = sc->bnx_func_addr + 8;
			else
				sc->bnx_phyno = sc->bnx_func_addr + 1;
		} else {
			if (CSR_READ_4(sc, BGE_CPMU_PHY_STRAP) &
			    BGE_CPMU_PHY_STRAP_IS_SERDES)
				sc->bnx_phyno = sc->bnx_func_addr + 8;
			else
				sc->bnx_phyno = sc->bnx_func_addr + 1;
		}
	}

	if (sc->bnx_flags & BNX_FLAG_TBI) {
		ifmedia_init(&sc->bnx_ifmedia, IFM_IMASK,
		    bnx_ifmedia_upd, bnx_ifmedia_sts);
		ifmedia_add(&sc->bnx_ifmedia, IFM_ETHER|IFM_1000_SX, 0, NULL);
		ifmedia_add(&sc->bnx_ifmedia,
		    IFM_ETHER|IFM_1000_SX|IFM_FDX, 0, NULL);
		ifmedia_add(&sc->bnx_ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
		ifmedia_set(&sc->bnx_ifmedia, IFM_ETHER|IFM_AUTO);
		sc->bnx_ifmedia.ifm_media = sc->bnx_ifmedia.ifm_cur->ifm_media;
	} else {
		struct mii_probe_args mii_args;

		mii_probe_args_init(&mii_args, bnx_ifmedia_upd, bnx_ifmedia_sts);
		mii_args.mii_probemask = 1 << sc->bnx_phyno;
		mii_args.mii_capmask = capmask;
		mii_args.mii_privtag = MII_PRIVTAG_BRGPHY;
		mii_args.mii_priv = mii_priv;

		error = mii_probe(dev, &sc->bnx_miibus, &mii_args);
		if (error) {
			device_printf(dev, "MII without any PHY!\n");
			goto fail;
		}
	}

	ctx = device_get_sysctl_ctx(sc->bnx_dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->bnx_dev));

	SYSCTL_ADD_INT(ctx, tree, OID_AUTO,
	    "rx_rings", CTLFLAG_RD, &sc->bnx_rx_retcnt, 0, "# of RX rings");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO,
	    "tx_rings", CTLFLAG_RD, &sc->bnx_tx_ringcnt, 0, "# of TX rings");

	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_rx_coal_ticks, "I",
			"Receive coalescing ticks (usec).");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "tx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_tx_coal_ticks, "I",
			"Transmit coalescing ticks (usec).");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_rx_coal_bds, "I",
			"Receive max coalesced BD count.");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rx_coal_bds_poll",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_rx_coal_bds_poll, "I",
			"Receive max coalesced BD count in polling.");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "tx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_tx_coal_bds, "I",
			"Transmit max coalesced BD count.");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "tx_coal_bds_poll",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_tx_coal_bds_poll, "I",
			"Transmit max coalesced BD count in polling.");
	/*
	 * A common design characteristic for many Broadcom
	 * client controllers is that they only support a
	 * single outstanding DMA read operation on the PCIe
	 * bus. This means that it will take twice as long to
	 * fetch a TX frame that is split into header and
	 * payload buffers as it does to fetch a single,
	 * contiguous TX frame (2 reads vs. 1 read). For these
	 * controllers, coalescing buffers to reduce the number
	 * of memory reads is effective way to get maximum
	 * performance(about 940Mbps).  Without collapsing TX
	 * buffers the maximum TCP bulk transfer performance
	 * is about 850Mbps. However forcing coalescing mbufs
	 * consumes a lot of CPU cycles, so leave it off by
	 * default.
	 */
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO,
	    "force_defrag", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_force_defrag, "I",
	    "Force defragment on TX path");

	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO,
	    "tx_wreg", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_tx_wreg, "I",
	    "# of segments before writing to hardware register");

	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO,
	    "std_refill", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_std_refill, "I",
	    "# of packets received before scheduling standard refilling");

	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO,
	    "rx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_rx_coal_bds_int, "I",
	    "Receive max coalesced BD count during interrupt.");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO,
	    "tx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_tx_coal_bds_int, "I",
	    "Transmit max coalesced BD count during interrupt.");

	if (sc->bnx_intr_type == PCI_INTR_TYPE_MSIX) {
		SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "tx_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->bnx_tx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
		    "TX ring CPU map");
		SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rx_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->bnx_rx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
		    "RX ring CPU map");
	} else {
#ifdef IFPOLL_ENABLE
		SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "tx_poll_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->bnx_tx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
		    "TX poll CPU map");
		SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "rx_poll_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD,
		    sc->bnx_rx_rmap, 0, if_ringmap_cpumap_sysctl, "I",
		    "RX poll CPU map");
#endif
	}

#ifdef BNX_RSS_DEBUG
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO,
	    "std_refill_mask", CTLFLAG_RD,
	    &sc->bnx_rx_std_ring.bnx_rx_std_refill, 0, "");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO,
	    "std_used", CTLFLAG_RD,
	    &sc->bnx_rx_std_ring.bnx_rx_std_used, 0, "");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO,
	    "rss_debug", CTLFLAG_RW, &sc->bnx_rss_debug, 0, "");
	for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
		ksnprintf(desc, sizeof(desc), "rx_pkt%d", i);
		SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
		    desc, CTLFLAG_RW, &sc->bnx_rx_ret_ring[i].bnx_rx_pkt, "");

		ksnprintf(desc, sizeof(desc), "rx_force_sched%d", i);
		SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
		    desc, CTLFLAG_RW,
		    &sc->bnx_rx_ret_ring[i].bnx_rx_force_sched, "");
	}
#endif
#ifdef BNX_TSS_DEBUG
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		ksnprintf(desc, sizeof(desc), "tx_pkt%d", i);
		SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
		    desc, CTLFLAG_RW, &sc->bnx_tx_ring[i].bnx_tx_pkt, "");
	}
#endif

	SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
	    "norxbds", CTLFLAG_RW, &sc->bnx_norxbds, "");

	SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
	    "errors", CTLFLAG_RW, &sc->bnx_errors, "");

#ifdef BNX_TSO_DEBUG
	for (i = 0; i < BNX_TSO_NSTATS; ++i) {
		ksnprintf(desc, sizeof(desc), "tso%d", i + 1);
		SYSCTL_ADD_ULONG(ctx, tree, OID_AUTO,
		    desc, CTLFLAG_RW, &sc->bnx_tsosegs[i], "");
	}
#endif

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ether_addr, NULL);

	/* Setup TX rings and subqueues */
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];

		ifsq_set_cpuid(ifsq, txr->bnx_tx_cpuid);
		ifsq_set_hw_serialize(ifsq, &txr->bnx_tx_serialize);
		ifsq_set_priv(ifsq, txr);
		txr->bnx_ifsq = ifsq;

		ifsq_watchdog_init(&txr->bnx_tx_watchdog, ifsq,
				   bnx_watchdog, 0);

		if (bootverbose) {
			device_printf(dev, "txr %d -> cpu%d\n", i,
			    txr->bnx_tx_cpuid);
		}
	}

	error = bnx_setup_intr(sc);
	if (error) {
		ether_ifdetach(ifp);
		goto fail;
	}
	bnx_set_tick_cpuid(sc, FALSE);

	/*
	 * Create RX standard ring refilling thread
	 */
	std_cpuid_def = if_ringmap_cpumap(sc->bnx_rx_rmap, 0);
	std_cpuid = device_getenv_int(dev, "std.cpuid", std_cpuid_def);
	if (std_cpuid < 0 || std_cpuid >= ncpus) {
		device_printf(dev, "invalid std.cpuid %d, use %d\n",
		    std_cpuid, std_cpuid_def);
		std_cpuid = std_cpuid_def;
	}

	std = &sc->bnx_rx_std_ring;
	lwkt_create(bnx_rx_std_refill_ithread, std, &std->bnx_rx_std_ithread,
	    NULL, TDF_NOSTART | TDF_INTTHREAD, std_cpuid,
	    "%s std", device_get_nameunit(dev));
	lwkt_setpri(std->bnx_rx_std_ithread, TDPRI_INT_MED);
	std->bnx_rx_std_ithread->td_preemptable = lwkt_preempt;

	return(0);
fail:
	bnx_detach(dev);
	return(error);
}

static int
bnx_detach(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct bnx_rx_std_ring *std = &sc->bnx_rx_std_ring;

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		ifnet_serialize_all(ifp);
		bnx_stop(sc);
		bnx_teardown_intr(sc, sc->bnx_intr_cnt);
		ifnet_deserialize_all(ifp);

		ether_ifdetach(ifp);
	}

	if (std->bnx_rx_std_ithread != NULL) {
		tsleep_interlock(std, 0);

		if (std->bnx_rx_std_ithread->td_gd == mycpu) {
			bnx_rx_std_refill_stop(std);
		} else {
			lwkt_send_ipiq(std->bnx_rx_std_ithread->td_gd,
			    bnx_rx_std_refill_stop, std);
		}

		tsleep(std, PINTERLOCKED, "bnx_detach", 0);
		if (bootverbose)
			device_printf(dev, "RX std ithread exited\n");

		lwkt_synchronize_ipiqs("bnx_detach_ipiq");
	}

	if (sc->bnx_flags & BNX_FLAG_TBI)
		ifmedia_removeall(&sc->bnx_ifmedia);
	if (sc->bnx_miibus)
		device_delete_child(dev, sc->bnx_miibus);
	bus_generic_detach(dev);

	bnx_free_intr(sc);

	if (sc->bnx_msix_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bnx_msix_mem_rid,
		    sc->bnx_msix_mem_res);
	}
	if (sc->bnx_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    BGE_PCI_BAR0, sc->bnx_res);
	}
	if (sc->bnx_res2 != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    PCIR_BAR(2), sc->bnx_res2);
	}

	bnx_dma_free(sc);

	if (sc->bnx_serialize != NULL)
		kfree(sc->bnx_serialize, M_DEVBUF);

	if (sc->bnx_rx_rmap != NULL)
		if_ringmap_free(sc->bnx_rx_rmap);
	if (sc->bnx_tx_rmap != NULL)
		if_ringmap_free(sc->bnx_tx_rmap);

	return 0;
}

static void
bnx_reset(struct bnx_softc *sc)
{
	device_t dev = sc->bnx_dev;
	uint32_t cachesize, command, reset, mac_mode, mac_mode_mask;
	void (*write_op)(struct bnx_softc *, uint32_t, uint32_t);
	int i, val = 0;
	uint16_t devctl;

	mac_mode_mask = BGE_MACMODE_HALF_DUPLEX | BGE_MACMODE_PORTMODE;
	if (sc->bnx_mfw_flags & BNX_MFW_ON_APE)
		mac_mode_mask |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;
	mac_mode = CSR_READ_4(sc, BGE_MAC_MODE) & mac_mode_mask;

	write_op = bnx_writemem_direct;

	CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_SET1);
	for (i = 0; i < 8000; i++) {
		if (CSR_READ_4(sc, BGE_NVRAM_SWARB) & BGE_NVRAMSWARB_GNT1)
			break;
		DELAY(20);
	}
	if (i == 8000)
		if_printf(&sc->arpcom.ac_if, "NVRAM lock timedout!\n");

	/* Take APE lock when performing reset. */
	bnx_ape_lock(sc, BGE_APE_LOCK_GRC);

	/* Save some important PCI state. */
	cachesize = pci_read_config(dev, BGE_PCI_CACHESZ, 4);
	command = pci_read_config(dev, BGE_PCI_CMD, 4);

	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW|
	    BGE_PCIMISCCTL_TAGGED_STATUS, 4);

	/* Disable fastboot on controllers that support it. */
	if (bootverbose)
		if_printf(&sc->arpcom.ac_if, "Disabling fastboot\n");
	CSR_WRITE_4(sc, BGE_FASTBOOT_PC, 0x0);

	/*
	 * Write the magic number to SRAM at offset 0xB50.
	 * When firmware finishes its initialization it will
	 * write ~BGE_SRAM_FW_MB_MAGIC to the same location.
	 */
	bnx_writemem_ind(sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);

	reset = BGE_MISCCFG_RESET_CORE_CLOCKS|(65<<1);

	/* XXX: Broadcom Linux driver. */
	/* Force PCI-E 1.0a mode */
	if (!BNX_IS_57765_PLUS(sc) &&
	    CSR_READ_4(sc, BGE_PCIE_PHY_TSTCTL) ==
	    (BGE_PCIE_PHY_TSTCTL_PSCRAM |
	     BGE_PCIE_PHY_TSTCTL_PCIE10)) {
		CSR_WRITE_4(sc, BGE_PCIE_PHY_TSTCTL,
		    BGE_PCIE_PHY_TSTCTL_PSCRAM);
	}
	if (sc->bnx_chipid != BGE_CHIPID_BCM5750_A0) {
		/* Prevent PCIE link training during global reset */
		CSR_WRITE_4(sc, BGE_MISC_CFG, (1<<29));
		reset |= (1<<29);
	}

	/*
	 * Set the clock to the highest frequency to avoid timeout.
	 */
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717) {
		BNX_SETBIT(sc, BGE_CPMU_CLCK_ORIDE_ENABLE,
		    BGE_CPMU_MAC_ORIDE_ENABLE);
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev ==  BGE_ASICREV_BCM5720) {
		BNX_SETBIT(sc, BGE_CPMU_CLCK_ORIDE,
		    CPMU_CLCK_ORIDE_MAC_ORIDE_EN);
	}

	/* 
	 * Set GPHY Power Down Override to leave GPHY
	 * powered up in D0 uninitialized.
	 */
	if ((sc->bnx_flags & BNX_FLAG_CPMU) == 0)
		reset |= BGE_MISCCFG_GPHY_PD_OVERRIDE;

	/* Issue global reset */
	write_op(sc, BGE_MISC_CFG, reset);

	DELAY(100 * 1000);

	/* XXX: Broadcom Linux driver. */
	if (sc->bnx_chipid == BGE_CHIPID_BCM5750_A0) {
		uint32_t v;

		DELAY(500000); /* wait for link training to complete */
		v = pci_read_config(dev, 0xc4, 4);
		pci_write_config(dev, 0xc4, v | (1<<15), 4);
	}

	devctl = pci_read_config(dev, sc->bnx_pciecap + PCIER_DEVCTRL, 2);

	/* Disable no snoop and disable relaxed ordering. */
	devctl &= ~(PCIEM_DEVCTL_RELAX_ORDER | PCIEM_DEVCTL_NOSNOOP);

	/* Old PCI-E chips only support 128 bytes Max PayLoad Size. */
	if ((sc->bnx_flags & BNX_FLAG_CPMU) == 0) {
		devctl &= ~PCIEM_DEVCTL_MAX_PAYLOAD_MASK;
		devctl |= PCIEM_DEVCTL_MAX_PAYLOAD_128;
	}

	pci_write_config(dev, sc->bnx_pciecap + PCIER_DEVCTRL,
	    devctl, 2);

	/* Clear error status. */
	pci_write_config(dev, sc->bnx_pciecap + PCIER_DEVSTS,
	    PCIEM_DEVSTS_CORR_ERR |
	    PCIEM_DEVSTS_NFATAL_ERR |
	    PCIEM_DEVSTS_FATAL_ERR |
	    PCIEM_DEVSTS_UNSUPP_REQ, 2);

	/* Reset some of the PCI state that got zapped by reset */
	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW|
	    BGE_PCIMISCCTL_TAGGED_STATUS, 4);
	val = BGE_PCISTATE_ROM_ENABLE | BGE_PCISTATE_ROM_RETRY_ENABLE;
	if (sc->bnx_mfw_flags & BNX_MFW_ON_APE) {
		val |= BGE_PCISTATE_ALLOW_APE_CTLSPC_WR |
		    BGE_PCISTATE_ALLOW_APE_SHMEM_WR |
		    BGE_PCISTATE_ALLOW_APE_PSPACE_WR;
	}
	pci_write_config(dev, BGE_PCI_PCISTATE, val, 4);
	pci_write_config(dev, BGE_PCI_CACHESZ, cachesize, 4);
	pci_write_config(dev, BGE_PCI_CMD, command, 4);

	/* Enable memory arbiter */
	CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);

	/* Fix up byte swapping */
	CSR_WRITE_4(sc, BGE_MODE_CTL, bnx_dma_swap_options(sc));

	val = CSR_READ_4(sc, BGE_MAC_MODE);
	val = (val & ~mac_mode_mask) | mac_mode;
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	bnx_ape_unlock(sc, BGE_APE_LOCK_GRC);

	/*
	 * Poll until we see the 1's complement of the magic number.
	 * This indicates that the firmware initialization is complete.
	 */
	for (i = 0; i < BNX_FIRMWARE_TIMEOUT; i++) {
		val = bnx_readmem_ind(sc, BGE_SRAM_FW_MB);
		if (val == ~BGE_SRAM_FW_MB_MAGIC)
			break;
		DELAY(10);
	}
	if (i == BNX_FIRMWARE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "firmware handshake "
			  "timed out, found 0x%08x\n", val);
	}

	/* BCM57765 A0 needs additional time before accessing. */
	if (sc->bnx_chipid == BGE_CHIPID_BCM57765_A0)
		DELAY(10 * 1000);

	/*
	 * The 5704 in TBI mode apparently needs some special
	 * adjustment to insure the SERDES drive level is set
	 * to 1.2V.
	 */
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5704 &&
	    (sc->bnx_flags & BNX_FLAG_TBI)) {
		uint32_t serdescfg;

		serdescfg = CSR_READ_4(sc, BGE_SERDES_CFG);
		serdescfg = (serdescfg & ~0xFFF) | 0x880;
		CSR_WRITE_4(sc, BGE_SERDES_CFG, serdescfg);
	}

	CSR_WRITE_4(sc, BGE_MI_MODE,
	    sc->bnx_mi_mode & ~BGE_MIMODE_AUTOPOLL);
	DELAY(80);

	/* XXX: Broadcom Linux driver. */
	if (!BNX_IS_57765_PLUS(sc)) {
		uint32_t v;

		/* Enable Data FIFO protection. */
		v = CSR_READ_4(sc, BGE_PCIE_TLDLPL_PORT);
		CSR_WRITE_4(sc, BGE_PCIE_TLDLPL_PORT, v | (1 << 25));
	}

	DELAY(10000);

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717) {
		BNX_CLRBIT(sc, BGE_CPMU_CLCK_ORIDE_ENABLE,
		    BGE_CPMU_MAC_ORIDE_ENABLE);
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev ==  BGE_ASICREV_BCM5720) {
		BNX_CLRBIT(sc, BGE_CPMU_CLCK_ORIDE,
		    CPMU_CLCK_ORIDE_MAC_ORIDE_EN);
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		/*
		 * Increase the core clock speed to fix TX timeout for
		 * 5762 on 100Mbps link.
		 */
		BNX_SETBIT(sc, BGE_CPMU_CLCK_ORIDE_ENABLE,
		    BGE_CPMU_MAC_ORIDE_ENABLE);
	}
}

/*
 * Frame reception handling. This is called if there's a frame
 * on the receive return list.
 *
 * Note: we have to be able to handle two possibilities here:
 * 1) the frame is from the jumbo recieve ring
 * 2) the frame is from the standard receive ring
 */

static void
bnx_rxeof(struct bnx_rx_ret_ring *ret, uint16_t rx_prod, int count)
{
	struct bnx_softc *sc = ret->bnx_sc;
	struct bnx_rx_std_ring *std = ret->bnx_std;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int std_used = 0, cpuid = mycpuid;

	while (ret->bnx_rx_saved_considx != rx_prod && count != 0) {
		struct pktinfo pi0, *pi = NULL;
		struct bge_rx_bd *cur_rx;
		struct bnx_rx_buf *rb;
		uint32_t rxidx;
		struct mbuf *m = NULL;
		uint16_t vlan_tag = 0;
		int have_tag = 0;

		--count;

		cur_rx = &ret->bnx_rx_ret_ring[ret->bnx_rx_saved_considx];

		rxidx = cur_rx->bge_idx;
		KKASSERT(rxidx < BGE_STD_RX_RING_CNT);

		BNX_INC(ret->bnx_rx_saved_considx, BNX_RETURN_RING_CNT);
#ifdef BNX_RSS_DEBUG
		ret->bnx_rx_pkt++;
#endif

		if (cur_rx->bge_flags & BGE_RXBDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->bge_vlan_tag;
		}

		if (ret->bnx_rx_cnt >= ret->bnx_rx_cntmax) {
			atomic_add_int(&std->bnx_rx_std_used, std_used);
			std_used = 0;

			bnx_rx_std_refill_sched(ret, std);
		}
		ret->bnx_rx_cnt++;
		++std_used;

		rb = &std->bnx_rx_std_buf[rxidx];
		m = rb->bnx_rx_mbuf;
		if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			cpu_sfence();
			rb->bnx_rx_refilled = 1;
			continue;
		}
		if (bnx_newbuf_std(ret, rxidx, 0)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		m->m_pkthdr.len = m->m_len = cur_rx->bge_len - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;

		if ((ifp->if_capenable & IFCAP_RXCSUM) &&
		    (cur_rx->bge_flags & BGE_RXBDFLAG_IPV6) == 0) {
			if (cur_rx->bge_flags & BGE_RXBDFLAG_IP_CSUM) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((cur_rx->bge_error_flag &
				    BGE_RXERRFLAG_IP_CSUM_NOK) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}
			if (cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_CSUM) {
				m->m_pkthdr.csum_data =
				    cur_rx->bge_tcp_udp_csum;
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
				    CSUM_PSEUDO_HDR;
			}
		}
		if (ifp->if_capenable & IFCAP_RSS) {
			pi = bnx_rss_info(&pi0, cur_rx);
			if (pi != NULL &&
			    (cur_rx->bge_flags & BGE_RXBDFLAG_RSS_HASH))
				m_sethash(m, toeplitz_hash(cur_rx->bge_hash));
		}

		/*
		 * If we received a packet with a vlan tag, pass it
		 * to vlan_input() instead of ether_input().
		 */
		if (have_tag) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag = vlan_tag;
		}
		ifp->if_input(ifp, m, pi, cpuid);
	}
	bnx_writembx(sc, ret->bnx_rx_mbx, ret->bnx_rx_saved_considx);

	if (std_used > 0) {
		int cur_std_used;

		cur_std_used = atomic_fetchadd_int(&std->bnx_rx_std_used,
		    std_used);
		if (cur_std_used + std_used >= (BGE_STD_RX_RING_CNT / 2)) {
#ifdef BNX_RSS_DEBUG
			ret->bnx_rx_force_sched++;
#endif
			bnx_rx_std_refill_sched(ret, std);
		}
	}
}

static void
bnx_txeof(struct bnx_tx_ring *txr, uint16_t tx_cons)
{
	struct ifnet *ifp = &txr->bnx_sc->arpcom.ac_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (txr->bnx_tx_saved_considx != tx_cons) {
		struct bnx_tx_buf *buf;
		uint32_t idx = 0;

		idx = txr->bnx_tx_saved_considx;
		buf = &txr->bnx_tx_buf[idx];
		if (buf->bnx_tx_mbuf != NULL) {
			IFNET_STAT_INC(ifp, opackets, 1);
#ifdef BNX_TSS_DEBUG
			txr->bnx_tx_pkt++;
#endif
			bus_dmamap_unload(txr->bnx_tx_mtag,
			    buf->bnx_tx_dmamap);
			m_freem(buf->bnx_tx_mbuf);
			buf->bnx_tx_mbuf = NULL;
		}
		txr->bnx_tx_cnt--;
		BNX_INC(txr->bnx_tx_saved_considx, BGE_TX_RING_CNT);
	}

	if ((BGE_TX_RING_CNT - txr->bnx_tx_cnt) >=
	    (BNX_NSEG_RSVD + BNX_NSEG_SPARE))
		ifsq_clr_oactive(txr->bnx_ifsq);

	if (txr->bnx_tx_cnt == 0)
		ifsq_watchdog_set_count(&txr->bnx_tx_watchdog, 0);

	if (!ifsq_is_empty(txr->bnx_ifsq))
		ifsq_devstart(txr->bnx_ifsq);
}

static int
bnx_handle_status(struct bnx_softc *sc)
{
	uint32_t status;
	int handle = 0;

	status = *sc->bnx_hw_status;

	if (status & BGE_STATFLAG_ERROR) {
		uint32_t val;
		int reset = 0;

		sc->bnx_errors++;

		val = CSR_READ_4(sc, BGE_FLOW_ATTN);
		if (val & ~BGE_FLOWATTN_MB_LOWAT) {
			if_printf(&sc->arpcom.ac_if,
			    "flow attn 0x%08x\n", val);
			reset = 1;
		}

		val = CSR_READ_4(sc, BGE_MSI_STATUS);
		if (val & ~BGE_MSISTAT_MSI_PCI_REQ) {
			if_printf(&sc->arpcom.ac_if,
			    "msi status 0x%08x\n", val);
			reset = 1;
		}

		val = CSR_READ_4(sc, BGE_RDMA_STATUS);
		if (val) {
			if_printf(&sc->arpcom.ac_if,
			    "rmda status 0x%08x\n", val);
			reset = 1;
		}

		val = CSR_READ_4(sc, BGE_WDMA_STATUS);
		if (val) {
			if_printf(&sc->arpcom.ac_if,
			    "wdma status 0x%08x\n", val);
			reset = 1;
		}

		if (reset) {
			bnx_serialize_skipmain(sc);
			bnx_init(sc);
			bnx_deserialize_skipmain(sc);
		}
		handle = 1;
	}

	if ((status & BGE_STATFLAG_LINKSTATE_CHANGED) || sc->bnx_link_evt) {
		if (bootverbose) {
			if_printf(&sc->arpcom.ac_if, "link change, "
			    "link_evt %d\n", sc->bnx_link_evt);
		}
		bnx_link_poll(sc);
		handle = 1;
	}

	return handle;
}

#ifdef IFPOLL_ENABLE

static void
bnx_npoll_rx(struct ifnet *ifp __unused, void *xret, int cycle)
{
	struct bnx_rx_ret_ring *ret = xret;
	uint16_t rx_prod;

	ASSERT_SERIALIZED(&ret->bnx_rx_ret_serialize);

	ret->bnx_saved_status_tag = *ret->bnx_hw_status_tag;
	cpu_lfence();

	rx_prod = *ret->bnx_rx_considx;
	if (ret->bnx_rx_saved_considx != rx_prod)
		bnx_rxeof(ret, rx_prod, cycle);
}

static void
bnx_npoll_tx_notag(struct ifnet *ifp __unused, void *xtxr, int cycle __unused)
{
	struct bnx_tx_ring *txr = xtxr;
	uint16_t tx_cons;

	ASSERT_SERIALIZED(&txr->bnx_tx_serialize);

	tx_cons = *txr->bnx_tx_considx;
	if (txr->bnx_tx_saved_considx != tx_cons)
		bnx_txeof(txr, tx_cons);
}

static void
bnx_npoll_tx(struct ifnet *ifp, void *xtxr, int cycle)
{
	struct bnx_tx_ring *txr = xtxr;

	ASSERT_SERIALIZED(&txr->bnx_tx_serialize);

	txr->bnx_saved_status_tag = *txr->bnx_hw_status_tag;
	cpu_lfence();
	bnx_npoll_tx_notag(ifp, txr, cycle);
}

static void
bnx_npoll_status_notag(struct ifnet *ifp)
{
	struct bnx_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(&sc->bnx_main_serialize);

	if (bnx_handle_status(sc)) {
		/*
		 * Status changes are handled; force the chip to
		 * update the status block to reflect whether there
		 * are more status changes or not, else staled status
		 * changes are always seen.
		 */
		BNX_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
	}
}

static void
bnx_npoll_status(struct ifnet *ifp)
{
	struct bnx_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(&sc->bnx_main_serialize);

	sc->bnx_saved_status_tag = *sc->bnx_hw_status_tag;
	cpu_lfence();
	bnx_npoll_status_notag(ifp);
}

static void
bnx_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct bnx_softc *sc = ifp->if_softc;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (info != NULL) {
		if (sc->bnx_flags & BNX_FLAG_STATUS_HASTAG)
			info->ifpi_status.status_func = bnx_npoll_status;
		else
			info->ifpi_status.status_func = bnx_npoll_status_notag;
		info->ifpi_status.serializer = &sc->bnx_main_serialize;

		for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
			struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];
			int cpu = if_ringmap_cpumap(sc->bnx_tx_rmap, i);

			KKASSERT(cpu < netisr_ncpus);
			if (sc->bnx_flags & BNX_FLAG_RXTX_BUNDLE) {
				info->ifpi_tx[cpu].poll_func =
				    bnx_npoll_tx_notag;
			} else {
				info->ifpi_tx[cpu].poll_func = bnx_npoll_tx;
			}
			info->ifpi_tx[cpu].arg = txr;
			info->ifpi_tx[cpu].serializer = &txr->bnx_tx_serialize;
			ifsq_set_cpuid(txr->bnx_ifsq, cpu);
		}

		for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
			struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[i];
			int cpu = if_ringmap_cpumap(sc->bnx_rx_rmap, i);

			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_rx[cpu].poll_func = bnx_npoll_rx;
			info->ifpi_rx[cpu].arg = ret;
			info->ifpi_rx[cpu].serializer =
			    &ret->bnx_rx_ret_serialize;
		}

		if (ifp->if_flags & IFF_RUNNING) {
			bnx_disable_intr(sc);
			bnx_set_tick_cpuid(sc, TRUE);

			sc->bnx_coal_chg = BNX_TX_COAL_BDS_CHG |
			    BNX_RX_COAL_BDS_CHG;
			bnx_coal_change(sc);
		}
	} else {
		for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
			ifsq_set_cpuid(sc->bnx_tx_ring[i].bnx_ifsq,
			    sc->bnx_tx_ring[i].bnx_tx_cpuid);
		}
		if (ifp->if_flags & IFF_RUNNING) {
			sc->bnx_coal_chg = BNX_TX_COAL_BDS_CHG |
			    BNX_RX_COAL_BDS_CHG;
			bnx_coal_change(sc);

			bnx_enable_intr(sc);
			bnx_set_tick_cpuid(sc, FALSE);
		}
	}
}

#endif	/* IFPOLL_ENABLE */

static void
bnx_intr_legacy(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[0];

	if (ret->bnx_saved_status_tag == *ret->bnx_hw_status_tag) {
		uint32_t val;

		val = pci_read_config(sc->bnx_dev, BGE_PCI_PCISTATE, 4);
		if (val & BGE_PCISTAT_INTR_NOTACT)
			return;
	}

	/*
	 * NOTE:
	 * Interrupt will have to be disabled if tagged status
	 * is used, else interrupt will always be asserted on
	 * certain chips (at least on BCM5750 AX/BX).
	 */
	bnx_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	bnx_intr(sc);
}

static void
bnx_msi(void *xsc)
{
	bnx_intr(xsc);
}

static void
bnx_intr(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[0];

	ASSERT_SERIALIZED(&sc->bnx_main_serialize);

	ret->bnx_saved_status_tag = *ret->bnx_hw_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved 
	 * before rx_prod, tx_cons and status.
	 */
	cpu_lfence();

	bnx_handle_status(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[0];
		uint16_t rx_prod, tx_cons;

		lwkt_serialize_enter(&ret->bnx_rx_ret_serialize);
		rx_prod = *ret->bnx_rx_considx;
		if (ret->bnx_rx_saved_considx != rx_prod)
			bnx_rxeof(ret, rx_prod, -1);
		lwkt_serialize_exit(&ret->bnx_rx_ret_serialize);

		lwkt_serialize_enter(&txr->bnx_tx_serialize);
		tx_cons = *txr->bnx_tx_considx;
		if (txr->bnx_tx_saved_considx != tx_cons)
			bnx_txeof(txr, tx_cons);
		lwkt_serialize_exit(&txr->bnx_tx_serialize);
	}

	bnx_writembx(sc, BGE_MBX_IRQ0_LO, ret->bnx_saved_status_tag << 24);
}

static void
bnx_msix_tx_status(void *xtxr)
{
	struct bnx_tx_ring *txr = xtxr;
	struct bnx_softc *sc = txr->bnx_sc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(&sc->bnx_main_serialize);

	txr->bnx_saved_status_tag = *txr->bnx_hw_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved 
	 * before tx_cons and status.
	 */
	cpu_lfence();

	bnx_handle_status(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		uint16_t tx_cons;

		lwkt_serialize_enter(&txr->bnx_tx_serialize);
		tx_cons = *txr->bnx_tx_considx;
		if (txr->bnx_tx_saved_considx != tx_cons)
			bnx_txeof(txr, tx_cons);
		lwkt_serialize_exit(&txr->bnx_tx_serialize);
	}

	bnx_writembx(sc, BGE_MBX_IRQ0_LO, txr->bnx_saved_status_tag << 24);
}

static void
bnx_msix_rx(void *xret)
{
	struct bnx_rx_ret_ring *ret = xret;
	uint16_t rx_prod;

	ASSERT_SERIALIZED(&ret->bnx_rx_ret_serialize);

	ret->bnx_saved_status_tag = *ret->bnx_hw_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved
	 * before rx_prod.
	 */
	cpu_lfence();

	rx_prod = *ret->bnx_rx_considx;
	if (ret->bnx_rx_saved_considx != rx_prod)
		bnx_rxeof(ret, rx_prod, -1);

	bnx_writembx(ret->bnx_sc, ret->bnx_msix_mbx,
	    ret->bnx_saved_status_tag << 24);
}

static void
bnx_msix_rxtx(void *xret)
{
	struct bnx_rx_ret_ring *ret = xret;
	struct bnx_tx_ring *txr = ret->bnx_txr;
	uint16_t rx_prod, tx_cons;

	ASSERT_SERIALIZED(&ret->bnx_rx_ret_serialize);

	ret->bnx_saved_status_tag = *ret->bnx_hw_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved
	 * before rx_prod and tx_cons.
	 */
	cpu_lfence();

	rx_prod = *ret->bnx_rx_considx;
	if (ret->bnx_rx_saved_considx != rx_prod)
		bnx_rxeof(ret, rx_prod, -1);

	lwkt_serialize_enter(&txr->bnx_tx_serialize);
	tx_cons = *txr->bnx_tx_considx;
	if (txr->bnx_tx_saved_considx != tx_cons)
		bnx_txeof(txr, tx_cons);
	lwkt_serialize_exit(&txr->bnx_tx_serialize);

	bnx_writembx(ret->bnx_sc, ret->bnx_msix_mbx,
	    ret->bnx_saved_status_tag << 24);
}

static void
bnx_msix_status(void *xsc)
{
	struct bnx_softc *sc = xsc;

	ASSERT_SERIALIZED(&sc->bnx_main_serialize);

	sc->bnx_saved_status_tag = *sc->bnx_hw_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved
	 * before status.
	 */
	cpu_lfence();

	bnx_handle_status(sc);

	bnx_writembx(sc, BGE_MBX_IRQ0_LO, sc->bnx_saved_status_tag << 24);
}

static void
bnx_tick(void *xsc)
{
	struct bnx_softc *sc = xsc;

	lwkt_serialize_enter(&sc->bnx_main_serialize);

	bnx_stats_update_regs(sc);

	if (sc->bnx_flags & BNX_FLAG_TBI) {
		/*
		 * Since in TBI mode auto-polling can't be used we should poll
		 * link status manually. Here we register pending link event
		 * and trigger interrupt.
		 */
		sc->bnx_link_evt++;
		BNX_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
	} else if (!sc->bnx_link) {
		mii_tick(device_get_softc(sc->bnx_miibus));
	}

	callout_reset_bycpu(&sc->bnx_tick_timer, hz, bnx_tick, sc,
	    sc->bnx_tick_cpuid);

	lwkt_serialize_exit(&sc->bnx_main_serialize);
}

static void
bnx_stats_update_regs(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_mac_stats_regs stats;
	uint32_t *s, val;
	int i;

	s = (uint32_t *)&stats;
	for (i = 0; i < sizeof(struct bge_mac_stats_regs); i += 4) {
		*s = CSR_READ_4(sc, BGE_RX_STATS + i);
		s++;
	}

	IFNET_STAT_SET(ifp, collisions,
	   (stats.dot3StatsSingleCollisionFrames +
	   stats.dot3StatsMultipleCollisionFrames +
	   stats.dot3StatsExcessiveCollisions +
	   stats.dot3StatsLateCollisions));

	val = CSR_READ_4(sc, BGE_RXLP_LOCSTAT_OUT_OF_BDS);
	sc->bnx_norxbds += val;

	if (sc->bnx_rdma_wa != 0) {
		if (stats.ifHCOutUcastPkts + stats.ifHCOutMulticastPkts +
		    stats.ifHCOutBroadcastPkts > BGE_RDMA_NCHAN) {
			CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
			    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) &
			    ~sc->bnx_rdma_wa);
			sc->bnx_rdma_wa = 0;
			if (bootverbose)
				if_printf(ifp, "disable RDMA WA\n");
		}
	}
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
bnx_encap(struct bnx_tx_ring *txr, struct mbuf **m_head0, uint32_t *txidx,
    int *segs_used)
{
	struct bge_tx_bd *d = NULL;
	uint16_t csum_flags = 0, vlan_tag = 0, mss = 0;
	bus_dma_segment_t segs[BNX_NSEG_NEW];
	bus_dmamap_t map;
	int error, maxsegs, nsegs, idx, i;
	struct mbuf *m_head = *m_head0, *m_new;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
#ifdef BNX_TSO_DEBUG
		int tso_nsegs;
#endif

		error = bnx_setup_tso(txr, m_head0, &mss, &csum_flags);
		if (error)
			return error;
		m_head = *m_head0;

#ifdef BNX_TSO_DEBUG
		tso_nsegs = (m_head->m_pkthdr.len /
		    m_head->m_pkthdr.tso_segsz) - 1;
		if (tso_nsegs > (BNX_TSO_NSTATS - 1))
			tso_nsegs = BNX_TSO_NSTATS - 1;
		else if (tso_nsegs < 0)
			tso_nsegs = 0;
		txr->bnx_sc->bnx_tsosegs[tso_nsegs]++;
#endif
	} else if (m_head->m_pkthdr.csum_flags & BNX_CSUM_FEATURES) {
		if (m_head->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= BGE_TXBDFLAG_IP_CSUM;
		if (m_head->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
			csum_flags |= BGE_TXBDFLAG_TCP_UDP_CSUM;
		if (m_head->m_flags & M_LASTFRAG)
			csum_flags |= BGE_TXBDFLAG_IP_FRAG_END;
		else if (m_head->m_flags & M_FRAG)
			csum_flags |= BGE_TXBDFLAG_IP_FRAG;
	}
	if (m_head->m_flags & M_VLANTAG) {
		csum_flags |= BGE_TXBDFLAG_VLAN_TAG;
		vlan_tag = m_head->m_pkthdr.ether_vlantag;
	}

	idx = *txidx;
	map = txr->bnx_tx_buf[idx].bnx_tx_dmamap;

	maxsegs = (BGE_TX_RING_CNT - txr->bnx_tx_cnt) - BNX_NSEG_RSVD;
	KASSERT(maxsegs >= BNX_NSEG_SPARE,
		("not enough segments %d", maxsegs));

	if (maxsegs > BNX_NSEG_NEW)
		maxsegs = BNX_NSEG_NEW;

	/*
	 * Pad outbound frame to BNX_MIN_FRAMELEN for an unusual reason.
	 * The bge hardware will pad out Tx runts to BNX_MIN_FRAMELEN,
	 * but when such padded frames employ the bge IP/TCP checksum
	 * offload, the hardware checksum assist gives incorrect results
	 * (possibly from incorporating its own padding into the UDP/TCP
	 * checksum; who knows).  If we pad such runts with zeros, the
	 * onboard checksum comes out correct.
	 */
	if ((csum_flags & BGE_TXBDFLAG_TCP_UDP_CSUM) &&
	    m_head->m_pkthdr.len < BNX_MIN_FRAMELEN) {
		error = m_devpad(m_head, BNX_MIN_FRAMELEN);
		if (error)
			goto back;
	}

	if ((txr->bnx_tx_flags & BNX_TX_FLAG_SHORTDMA) &&
	    m_head->m_next != NULL) {
		m_new = bnx_defrag_shortdma(m_head);
		if (m_new == NULL) {
			error = ENOBUFS;
			goto back;
		}
		*m_head0 = m_head = m_new;
	}
	if ((m_head->m_pkthdr.csum_flags & CSUM_TSO) == 0 &&
	    (txr->bnx_tx_flags & BNX_TX_FLAG_FORCE_DEFRAG) &&
	    m_head->m_next != NULL) {
		/*
		 * Forcefully defragment mbuf chain to overcome hardware
		 * limitation which only support a single outstanding
		 * DMA read operation.  If it fails, keep moving on using
		 * the original mbuf chain.
		 */
		m_new = m_defrag(m_head, M_NOWAIT);
		if (m_new != NULL)
			*m_head0 = m_head = m_new;
	}

	error = bus_dmamap_load_mbuf_defrag(txr->bnx_tx_mtag, map,
	    m_head0, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;
	*segs_used += nsegs;

	m_head = *m_head0;
	bus_dmamap_sync(txr->bnx_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	for (i = 0; ; i++) {
		d = &txr->bnx_tx_ring[idx];

		d->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[i].ds_addr);
		d->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[i].ds_addr);
		d->bge_len = segs[i].ds_len;
		d->bge_flags = csum_flags;
		d->bge_vlan_tag = vlan_tag;
		d->bge_mss = mss;

		if (i == nsegs - 1)
			break;
		BNX_INC(idx, BGE_TX_RING_CNT);
	}
	/* Mark the last segment as end of packet... */
	d->bge_flags |= BGE_TXBDFLAG_END;

	/*
	 * Insure that the map for this transmission is placed at
	 * the array index of the last descriptor in this chain.
	 */
	txr->bnx_tx_buf[*txidx].bnx_tx_dmamap = txr->bnx_tx_buf[idx].bnx_tx_dmamap;
	txr->bnx_tx_buf[idx].bnx_tx_dmamap = map;
	txr->bnx_tx_buf[idx].bnx_tx_mbuf = m_head;
	txr->bnx_tx_cnt += nsegs;

	BNX_INC(idx, BGE_TX_RING_CNT);
	*txidx = idx;
back:
	if (error) {
		m_freem(*m_head0);
		*m_head0 = NULL;
	}
	return error;
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void
bnx_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct bnx_tx_ring *txr = ifsq_get_priv(ifsq);
	struct mbuf *m_head = NULL;
	uint32_t prodidx;
	int nsegs = 0;

	KKASSERT(txr->bnx_ifsq == ifsq);
	ASSERT_SERIALIZED(&txr->bnx_tx_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	prodidx = txr->bnx_tx_prodidx;

	while (txr->bnx_tx_buf[prodidx].bnx_tx_mbuf == NULL) {
		/*
		 * Sanity check: avoid coming within BGE_NSEG_RSVD
		 * descriptors of the end of the ring.  Also make
		 * sure there are BGE_NSEG_SPARE descriptors for
		 * jumbo buffers' or TSO segments' defragmentation.
		 */
		if ((BGE_TX_RING_CNT - txr->bnx_tx_cnt) <
		    (BNX_NSEG_RSVD + BNX_NSEG_SPARE)) {
			ifsq_set_oactive(ifsq);
			break;
		}

		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (bnx_encap(txr, &m_head, &prodidx, &nsegs)) {
			ifsq_set_oactive(ifsq);
			IFNET_STAT_INC(ifp, oerrors, 1);
			break;
		}

		if (nsegs >= txr->bnx_tx_wreg) {
			/* Transmit */
			bnx_writembx(txr->bnx_sc, txr->bnx_tx_mbx, prodidx);
			nsegs = 0;
		}

		ETHER_BPF_MTAP(ifp, m_head);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		ifsq_watchdog_set_count(&txr->bnx_tx_watchdog, 5);
	}

	if (nsegs > 0) {
		/* Transmit */
		bnx_writembx(txr->bnx_sc, txr->bnx_tx_mbx, prodidx);
	}
	txr->bnx_tx_prodidx = prodidx;
}

static void
bnx_init(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t *m;
	uint32_t mode;
	int i;
	boolean_t polling;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* Cancel pending I/O and flush buffers. */
	bnx_stop(sc);

	bnx_sig_pre_reset(sc, BNX_RESET_START);
	bnx_reset(sc);
	bnx_sig_post_reset(sc, BNX_RESET_START);

	bnx_chipinit(sc);

	/*
	 * Init the various state machines, ring
	 * control blocks and firmware.
	 */
	if (bnx_blockinit(sc)) {
		if_printf(ifp, "initialization failure\n");
		bnx_stop(sc);
		return;
	}

	/* Specify MTU. */
	CSR_WRITE_4(sc, BGE_RX_MTU, ifp->if_mtu +
	    ETHER_HDR_LEN + ETHER_CRC_LEN + EVL_ENCAPLEN);

	/* Load our MAC address. */
	m = (uint16_t *)&sc->arpcom.ac_enaddr[0];
	CSR_WRITE_4(sc, BGE_MAC_ADDR1_LO, htons(m[0]));
	CSR_WRITE_4(sc, BGE_MAC_ADDR1_HI, (htons(m[1]) << 16) | htons(m[2]));

	/* Enable or disable promiscuous mode as needed. */
	bnx_setpromisc(sc);

	/* Program multicast filter. */
	bnx_setmulti(sc);

	/* Init RX ring. */
	if (bnx_init_rx_ring_std(&sc->bnx_rx_std_ring)) {
		if_printf(ifp, "RX ring initialization failed\n");
		bnx_stop(sc);
		return;
	}

	/* Init jumbo RX ring. */
	if (ifp->if_mtu > (ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN)) {
		if (bnx_init_rx_ring_jumbo(sc)) {
			if_printf(ifp, "Jumbo RX ring initialization failed\n");
			bnx_stop(sc);
			return;
		}
	}

	/* Init our RX return ring index */
	for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
		struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[i];

		ret->bnx_rx_saved_considx = 0;
		ret->bnx_rx_cnt = 0;
	}

	/* Init TX ring. */
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
		bnx_init_tx_ring(&sc->bnx_tx_ring[i]);

	/* Enable TX MAC state machine lockup fix. */
	mode = CSR_READ_4(sc, BGE_TX_MODE);
	mode |= BGE_TXMODE_MBUF_LOCKUP_FIX;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5762) {
		mode &= ~(BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
		mode |= CSR_READ_4(sc, BGE_TX_MODE) &
		    (BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
	}
	/* Turn on transmitter */
	CSR_WRITE_4(sc, BGE_TX_MODE, mode | BGE_TXMODE_ENABLE);
	DELAY(100);

	/* Initialize RSS */
	mode = BGE_RXMODE_ENABLE | BGE_RXMODE_IPV6_ENABLE;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5762)
		mode |= BGE_RXMODE_IPV4_FRAG_FIX;
	if (BNX_RSS_ENABLED(sc)) {
		bnx_init_rss(sc);
		mode |= BGE_RXMODE_RSS_ENABLE |
		    BGE_RXMODE_RSS_HASH_MASK_BITS |
		    BGE_RXMODE_RSS_IPV4_HASH |
		    BGE_RXMODE_RSS_TCP_IPV4_HASH;
	}
	/* Turn on receiver */
	BNX_SETBIT(sc, BGE_RX_MODE, mode);
	DELAY(10);

	/*
	 * Set the number of good frames to receive after RX MBUF
	 * Low Watermark has been reached.  After the RX MAC receives
	 * this number of frames, it will drop subsequent incoming
	 * frames until the MBUF High Watermark is reached.
	 */
	if (BNX_IS_57765_FAMILY(sc))
		CSR_WRITE_4(sc, BGE_MAX_RX_FRAME_LOWAT, 1);
	else
		CSR_WRITE_4(sc, BGE_MAX_RX_FRAME_LOWAT, 2);

	if (sc->bnx_intr_type == PCI_INTR_TYPE_MSI ||
	    sc->bnx_intr_type == PCI_INTR_TYPE_MSIX) {
		if (bootverbose) {
			if_printf(ifp, "MSI_MODE: %#x\n",
			    CSR_READ_4(sc, BGE_MSI_MODE));
		}
	}

	/* Tell firmware we're alive. */
	BNX_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Enable host interrupts if polling(4) is not enabled. */
	PCI_SETBIT(sc->bnx_dev, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_CLEAR_INTA, 4);

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif
	if (polling)
		bnx_disable_intr(sc);
	else
		bnx_enable_intr(sc);
	bnx_set_tick_cpuid(sc, polling);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];

		ifsq_clr_oactive(txr->bnx_ifsq);
		ifsq_watchdog_start(&txr->bnx_tx_watchdog);
	}

	bnx_ifmedia_upd(ifp);

	callout_reset_bycpu(&sc->bnx_tick_timer, hz, bnx_tick, sc,
	    sc->bnx_tick_cpuid);
}

/*
 * Set media options.
 */
static int
bnx_ifmedia_upd(struct ifnet *ifp)
{
	struct bnx_softc *sc = ifp->if_softc;

	/* If this is a 1000baseX NIC, enable the TBI port. */
	if (sc->bnx_flags & BNX_FLAG_TBI) {
		struct ifmedia *ifm = &sc->bnx_ifmedia;

		if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
			return(EINVAL);

		switch(IFM_SUBTYPE(ifm->ifm_media)) {
		case IFM_AUTO:
			break;

		case IFM_1000_SX:
			if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
				BNX_CLRBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			} else {
				BNX_SETBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			}
			DELAY(40);
			break;
		default:
			return(EINVAL);
		}
	} else {
		struct mii_data *mii = device_get_softc(sc->bnx_miibus);

		sc->bnx_link_evt++;
		sc->bnx_link = 0;
		if (mii->mii_instance) {
			struct mii_softc *miisc;

			LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
				mii_phy_reset(miisc);
		}
		mii_mediachg(mii);

		/*
		 * Force an interrupt so that we will call bnx_link_upd
		 * if needed and clear any pending link state attention.
		 * Without this we are not getting any further interrupts
		 * for link state changes and thus will not UP the link and
		 * not be able to send in bnx_start.  The only way to get
		 * things working was to receive a packet and get an RX
		 * intr.
		 *
		 * bnx_tick should help for fiber cards and we might not
		 * need to do this here if BNX_FLAG_TBI is set but as
		 * we poll for fiber anyway it should not harm.
		 */
		BNX_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
	}
	return(0);
}

/*
 * Report current media status.
 */
static void
bnx_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bnx_softc *sc = ifp->if_softc;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->bnx_flags & BNX_FLAG_TBI) {
		ifmr->ifm_status = IFM_AVALID;
		ifmr->ifm_active = IFM_ETHER;
		if (CSR_READ_4(sc, BGE_MAC_STS) &
		    BGE_MACSTAT_TBI_PCS_SYNCHED) {
			ifmr->ifm_status |= IFM_ACTIVE;
		} else {
			ifmr->ifm_active |= IFM_NONE;
			return;
		}

		ifmr->ifm_active |= IFM_1000_SX;
		if (CSR_READ_4(sc, BGE_MAC_MODE) & BGE_MACMODE_HALF_DUPLEX)
			ifmr->ifm_active |= IFM_HDX;	
		else
			ifmr->ifm_active |= IFM_FDX;
	} else {
		struct mii_data *mii = device_get_softc(sc->bnx_miibus);

		mii_pollstat(mii);
		ifmr->ifm_active = mii->mii_media_active;
		ifmr->ifm_status = mii->mii_media_status;
	}
}

static int
bnx_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct bnx_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int mask, error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (command) {
	case SIOCSIFMTU:
		if ((!BNX_IS_JUMBO_CAPABLE(sc) && ifr->ifr_mtu > ETHERMTU) ||
		    (BNX_IS_JUMBO_CAPABLE(sc) &&
		     ifr->ifr_mtu > BNX_JUMBO_MTU)) {
			error = EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_flags & IFF_RUNNING)
				bnx_init(sc);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				mask = ifp->if_flags ^ sc->bnx_if_flags;

				/*
				 * If only the state of the PROMISC flag
				 * changed, then just use the 'set promisc
				 * mode' command instead of reinitializing
				 * the entire NIC. Doing a full re-init
				 * means reloading the firmware and waiting
				 * for it to start up, which may take a
				 * second or two.  Similarly for ALLMULTI.
				 */
				if (mask & IFF_PROMISC)
					bnx_setpromisc(sc);
				if (mask & IFF_ALLMULTI)
					bnx_setmulti(sc);
			} else {
				bnx_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			bnx_stop(sc);
		}
		sc->bnx_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			bnx_setmulti(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->bnx_flags & BNX_FLAG_TBI) {
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->bnx_ifmedia, command);
		} else {
			struct mii_data *mii;

			mii = device_get_softc(sc->bnx_miibus);
			error = ifmedia_ioctl(ifp, ifr,
					      &mii->mii_media, command);
		}
		break;
        case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable ^= (mask & IFCAP_HWCSUM);
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= BNX_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~BNX_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= (mask & IFCAP_TSO);
			if (ifp->if_capenable & IFCAP_TSO)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if (mask & IFCAP_RSS)
			ifp->if_capenable ^= IFCAP_RSS;
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return error;
}

static void
bnx_watchdog(struct ifaltq_subque *ifsq)
{
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct bnx_softc *sc = ifp->if_softc;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if_printf(ifp, "watchdog timeout -- resetting\n");

	bnx_init(sc);

	IFNET_STAT_INC(ifp, oerrors, 1);

	for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
		ifsq_devstart_sched(sc->bnx_tx_ring[i].bnx_ifsq);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
bnx_stop(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	callout_stop(&sc->bnx_tick_timer);

	/* Disable host interrupts. */
	bnx_disable_intr(sc);

	/*
	 * Tell firmware we're shutting down.
	 */
	bnx_sig_pre_reset(sc, BNX_RESET_SHUTDOWN);

	/*
	 * Disable all of the receiver blocks
	 */
	bnx_stop_block(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);
	bnx_stop_block(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);
	bnx_stop_block(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);
	bnx_stop_block(sc, BGE_RDBDI_MODE, BGE_RBDIMODE_ENABLE);
	bnx_stop_block(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);
	bnx_stop_block(sc, BGE_RBDC_MODE, BGE_RBDCMODE_ENABLE);

	/*
	 * Disable all of the transmit blocks
	 */
	bnx_stop_block(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);
	bnx_stop_block(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);
	bnx_stop_block(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
	bnx_stop_block(sc, BGE_RDMA_MODE, BGE_RDMAMODE_ENABLE);
	bnx_stop_block(sc, BGE_SDC_MODE, BGE_SDCMODE_ENABLE);
	bnx_stop_block(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/*
	 * Shut down all of the memory managers and related
	 * state machines.
	 */
	bnx_stop_block(sc, BGE_HCC_MODE, BGE_HCCMODE_ENABLE);
	bnx_stop_block(sc, BGE_WDMA_MODE, BGE_WDMAMODE_ENABLE);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);

	bnx_reset(sc);
	bnx_sig_post_reset(sc, BNX_RESET_SHUTDOWN);

	/*
	 * Tell firmware we're shutting down.
	 */
	BNX_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Free the RX lists. */
	bnx_free_rx_ring_std(&sc->bnx_rx_std_ring);

	/* Free jumbo RX list. */
	if (BNX_IS_JUMBO_CAPABLE(sc))
		bnx_free_rx_ring_jumbo(sc);

	/* Free TX buffers. */
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];

		txr->bnx_saved_status_tag = 0;
		bnx_free_tx_ring(txr);
	}

	/* Clear saved status tag */
	for (i = 0; i < sc->bnx_rx_retcnt; ++i)
		sc->bnx_rx_ret_ring[i].bnx_saved_status_tag = 0;

	sc->bnx_link = 0;
	sc->bnx_coal_chg = 0;

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];

		ifsq_clr_oactive(txr->bnx_ifsq);
		ifsq_watchdog_stop(&txr->bnx_tx_watchdog);
	}
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
bnx_shutdown(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);
	bnx_stop(sc);
	ifnet_deserialize_all(ifp);
}

static int
bnx_suspend(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);
	bnx_stop(sc);
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
bnx_resume(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifnet_serialize_all(ifp);

	if (ifp->if_flags & IFF_UP) {
		int i;

		bnx_init(sc);
		for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
			ifsq_devstart_sched(sc->bnx_tx_ring[i].bnx_ifsq);
	}

	ifnet_deserialize_all(ifp);

	return 0;
}

static void
bnx_setpromisc(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (ifp->if_flags & IFF_PROMISC)
		BNX_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
	else
		BNX_CLRBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
}

static void
bnx_dma_free(struct bnx_softc *sc)
{
	struct bnx_rx_std_ring *std = &sc->bnx_rx_std_ring;
	int i;

	/* Destroy RX return rings */
	if (sc->bnx_rx_ret_ring != NULL) {
		for (i = 0; i < sc->bnx_rx_retcnt; ++i)
			bnx_destroy_rx_ret_ring(&sc->bnx_rx_ret_ring[i]);
		kfree(sc->bnx_rx_ret_ring, M_DEVBUF);
	}

	/* Destroy RX mbuf DMA stuffs. */
	if (std->bnx_rx_mtag != NULL) {
		for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
			KKASSERT(std->bnx_rx_std_buf[i].bnx_rx_mbuf == NULL);
			bus_dmamap_destroy(std->bnx_rx_mtag,
			    std->bnx_rx_std_buf[i].bnx_rx_dmamap);
		}
		bus_dma_tag_destroy(std->bnx_rx_mtag);
	}

	/* Destroy standard RX ring */
	bnx_dma_block_free(std->bnx_rx_std_ring_tag,
	    std->bnx_rx_std_ring_map, std->bnx_rx_std_ring);

	/* Destroy TX rings */
	if (sc->bnx_tx_ring != NULL) {
		for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
			bnx_destroy_tx_ring(&sc->bnx_tx_ring[i]);
		kfree(sc->bnx_tx_ring, M_DEVBUF);
	}

	if (BNX_IS_JUMBO_CAPABLE(sc))
		bnx_free_jumbo_mem(sc);

	/* Destroy status blocks */
	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		bnx_dma_block_free(intr->bnx_status_tag,
		    intr->bnx_status_map, intr->bnx_status_block);
	}

	/* Destroy the parent tag */
	if (sc->bnx_cdata.bnx_parent_tag != NULL)
		bus_dma_tag_destroy(sc->bnx_cdata.bnx_parent_tag);
}

static int
bnx_dma_alloc(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct bnx_rx_std_ring *std = &sc->bnx_rx_std_ring;
	int i, error, mbx;

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 *
	 * All of the NetExtreme/NetLink controllers have 4GB boundary
	 * DMA bug.
	 * Whenever an address crosses a multiple of the 4GB boundary
	 * (including 4GB, 8Gb, 12Gb, etc.) and makes the transition
	 * from 0xX_FFFF_FFFF to 0x(X+1)_0000_0000 an internal DMA
	 * state machine will lockup and cause the device to hang.
	 */
	error = bus_dma_tag_create(NULL, 1, BGE_DMA_BOUNDARY_4G,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT,
	    0, &sc->bnx_cdata.bnx_parent_tag);
	if (error) {
		device_printf(dev, "could not create parent DMA tag\n");
		return error;
	}

	/*
	 * Create DMA stuffs for status blocks.
	 */
	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		error = bnx_dma_block_alloc(sc,
		    __VM_CACHELINE_ALIGN(BGE_STATUS_BLK_SZ),
		    &intr->bnx_status_tag, &intr->bnx_status_map,
		    (void *)&intr->bnx_status_block,
		    &intr->bnx_status_block_paddr);
		if (error) {
			device_printf(dev,
			    "could not create %dth status block\n", i);
			return error;
		}
	}
	sc->bnx_hw_status = &sc->bnx_intr_data[0].bnx_status_block->bge_status;
	if (sc->bnx_flags & BNX_FLAG_STATUS_HASTAG) {
		sc->bnx_hw_status_tag =
		    &sc->bnx_intr_data[0].bnx_status_block->bge_status_tag;
	}

	/*
	 * Create DMA tag and maps for RX mbufs.
	 */
	std->bnx_sc = sc;
	lwkt_serialize_init(&std->bnx_rx_std_serialize);
	error = bus_dma_tag_create(sc->bnx_cdata.bnx_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    MCLBYTES, 1, MCLBYTES,
	    BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK, &std->bnx_rx_mtag);
	if (error) {
		device_printf(dev, "could not create RX mbuf DMA tag\n");
		return error;
	}

	for (i = 0; i < BGE_STD_RX_RING_CNT; ++i) {
		error = bus_dmamap_create(std->bnx_rx_mtag, BUS_DMA_WAITOK,
		    &std->bnx_rx_std_buf[i].bnx_rx_dmamap);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(std->bnx_rx_mtag,
				    std->bnx_rx_std_buf[j].bnx_rx_dmamap);
			}
			bus_dma_tag_destroy(std->bnx_rx_mtag);
			std->bnx_rx_mtag = NULL;

			device_printf(dev,
			    "could not create %dth RX mbuf DMA map\n", i);
			return error;
		}
	}

	/*
	 * Create DMA stuffs for standard RX ring.
	 */
	error = bnx_dma_block_alloc(sc, BGE_STD_RX_RING_SZ,
	    &std->bnx_rx_std_ring_tag,
	    &std->bnx_rx_std_ring_map,
	    (void *)&std->bnx_rx_std_ring,
	    &std->bnx_rx_std_ring_paddr);
	if (error) {
		device_printf(dev, "could not create std RX ring\n");
		return error;
	}

	/*
	 * Create RX return rings
	 */
	mbx = BGE_MBX_RX_CONS0_LO;
	sc->bnx_rx_ret_ring =
		kmalloc(sizeof(struct bnx_rx_ret_ring) * sc->bnx_rx_retcnt,
			M_DEVBUF,
			M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
		struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[i];
		struct bnx_intr_data *intr;

		ret->bnx_sc = sc;
		ret->bnx_std = std;
		ret->bnx_rx_mbx = mbx;
		ret->bnx_rx_cntmax = (BGE_STD_RX_RING_CNT / 4) /
		    sc->bnx_rx_retcnt;
		ret->bnx_rx_mask = 1 << i;

		if (!BNX_RSS_ENABLED(sc)) {
			intr = &sc->bnx_intr_data[0];
		} else {
			KKASSERT(i + 1 < sc->bnx_intr_cnt);
			intr = &sc->bnx_intr_data[i + 1];
		}

		if (i == 0) {
			ret->bnx_rx_considx =
			    &intr->bnx_status_block->bge_idx[0].bge_rx_prod_idx;
		} else if (i == 1) {
			ret->bnx_rx_considx =
			    &intr->bnx_status_block->bge_rx_jumbo_cons_idx;
		} else if (i == 2) {
			ret->bnx_rx_considx =
			    &intr->bnx_status_block->bge_rsvd1;
		} else if (i == 3) {
			ret->bnx_rx_considx =
			    &intr->bnx_status_block->bge_rx_mini_cons_idx;
		} else {
			panic("unknown RX return ring %d\n", i);
		}
		ret->bnx_hw_status_tag =
		    &intr->bnx_status_block->bge_status_tag;

		error = bnx_create_rx_ret_ring(ret);
		if (error) {
			device_printf(dev,
			    "could not create %dth RX ret ring\n", i);
			return error;
		}
		mbx += 8;
	}

	/*
	 * Create TX rings
	 */
	sc->bnx_tx_ring =
		kmalloc(sizeof(struct bnx_tx_ring) * sc->bnx_tx_ringcnt,
			M_DEVBUF,
			M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		struct bnx_tx_ring *txr = &sc->bnx_tx_ring[i];
		struct bnx_intr_data *intr;

		txr->bnx_sc = sc;
		txr->bnx_tx_mbx = bnx_tx_mailbox[i];

		if (sc->bnx_tx_ringcnt == 1) {
			intr = &sc->bnx_intr_data[0];
		} else {
			KKASSERT(i + 1 < sc->bnx_intr_cnt);
			intr = &sc->bnx_intr_data[i + 1];
		}

		if ((sc->bnx_flags & BNX_FLAG_RXTX_BUNDLE) == 0) {
			txr->bnx_hw_status_tag =
			    &intr->bnx_status_block->bge_status_tag;
		}
		txr->bnx_tx_considx =
		    &intr->bnx_status_block->bge_idx[0].bge_tx_cons_idx;

		error = bnx_create_tx_ring(txr);
		if (error) {
			device_printf(dev,
			    "could not create %dth TX ring\n", i);
			return error;
		}
	}

	/*
	 * Create jumbo buffer pool.
	 */
	if (BNX_IS_JUMBO_CAPABLE(sc)) {
		error = bnx_alloc_jumbo_mem(sc);
		if (error) {
			device_printf(dev,
			    "could not create jumbo buffer pool\n");
			return error;
		}
	}

	return 0;
}

static int
bnx_dma_block_alloc(struct bnx_softc *sc, bus_size_t size, bus_dma_tag_t *tag,
		    bus_dmamap_t *map, void **addr, bus_addr_t *paddr)
{
	bus_dmamem_t dmem;
	int error;

	error = bus_dmamem_coherent(sc->bnx_cdata.bnx_parent_tag, PAGE_SIZE, 0,
				    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				    size, BUS_DMA_WAITOK | BUS_DMA_ZERO, &dmem);
	if (error)
		return error;

	*tag = dmem.dmem_tag;
	*map = dmem.dmem_map;
	*addr = dmem.dmem_addr;
	*paddr = dmem.dmem_busaddr;

	return 0;
}

static void
bnx_dma_block_free(bus_dma_tag_t tag, bus_dmamap_t map, void *addr)
{
	if (tag != NULL) {
		bus_dmamap_unload(tag, map);
		bus_dmamem_free(tag, addr, map);
		bus_dma_tag_destroy(tag);
	}
}

static void
bnx_tbi_link_upd(struct bnx_softc *sc, uint32_t status)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

#define PCS_ENCODE_ERR	(BGE_MACSTAT_PORT_DECODE_ERROR|BGE_MACSTAT_MI_COMPLETE)

	/*
	 * Sometimes PCS encoding errors are detected in
	 * TBI mode (on fiber NICs), and for some reason
	 * the chip will signal them as link changes.
	 * If we get a link change event, but the 'PCS
	 * encoding error' bit in the MAC status register
	 * is set, don't bother doing a link check.
	 * This avoids spurious "gigabit link up" messages
	 * that sometimes appear on fiber NICs during
	 * periods of heavy traffic.
	 */
	if (status & BGE_MACSTAT_TBI_PCS_SYNCHED) {
		if (!sc->bnx_link) {
			sc->bnx_link++;
			if (sc->bnx_asicrev == BGE_ASICREV_BCM5704) {
				BNX_CLRBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_TBI_SEND_CFGS);
				DELAY(40);
			}
			CSR_WRITE_4(sc, BGE_MAC_STS, 0xFFFFFFFF);

			if (bootverbose)
				if_printf(ifp, "link UP\n");

			ifp->if_link_state = LINK_STATE_UP;
			if_link_state_change(ifp);
		}
	} else if ((status & PCS_ENCODE_ERR) != PCS_ENCODE_ERR) {
		if (sc->bnx_link) {
			sc->bnx_link = 0;

			if (bootverbose)
				if_printf(ifp, "link DOWN\n");

			ifp->if_link_state = LINK_STATE_DOWN;
			if_link_state_change(ifp);
		}
	}

#undef PCS_ENCODE_ERR

	/* Clear the attention. */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
}

static void
bnx_copper_link_upd(struct bnx_softc *sc, uint32_t status __unused)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->bnx_miibus);

	mii_pollstat(mii);
	bnx_miibus_statchg(sc->bnx_dev);

	if (bootverbose) {
		if (sc->bnx_link)
			if_printf(ifp, "link UP\n");
		else
			if_printf(ifp, "link DOWN\n");
	}

	/* Clear the attention. */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
}

static void
bnx_autopoll_link_upd(struct bnx_softc *sc, uint32_t status __unused)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->bnx_miibus);

	mii_pollstat(mii);

	if (!sc->bnx_link &&
	    (mii->mii_media_status & IFM_ACTIVE) &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->bnx_link++;
		if (bootverbose)
			if_printf(ifp, "link UP\n");
	} else if (sc->bnx_link &&
	    (!(mii->mii_media_status & IFM_ACTIVE) ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)) {
		sc->bnx_link = 0;
		if (bootverbose)
			if_printf(ifp, "link DOWN\n");
	}

	/* Clear the attention. */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
}

static int
bnx_sysctl_rx_coal_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_rx_coal_ticks,
	    BNX_RX_COAL_TICKS_MIN, BNX_RX_COAL_TICKS_MAX,
	    BNX_RX_COAL_TICKS_CHG);
}

static int
bnx_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_tx_coal_ticks,
	    BNX_TX_COAL_TICKS_MIN, BNX_TX_COAL_TICKS_MAX,
	    BNX_TX_COAL_TICKS_CHG);
}

static int
bnx_sysctl_rx_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_rx_coal_bds,
	    BNX_RX_COAL_BDS_MIN, BNX_RX_COAL_BDS_MAX,
	    BNX_RX_COAL_BDS_CHG);
}

static int
bnx_sysctl_rx_coal_bds_poll(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_rx_coal_bds_poll,
	    BNX_RX_COAL_BDS_MIN, BNX_RX_COAL_BDS_MAX,
	    BNX_RX_COAL_BDS_CHG);
}

static int
bnx_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_tx_coal_bds,
	    BNX_TX_COAL_BDS_MIN, BNX_TX_COAL_BDS_MAX,
	    BNX_TX_COAL_BDS_CHG);
}

static int
bnx_sysctl_tx_coal_bds_poll(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_tx_coal_bds_poll,
	    BNX_TX_COAL_BDS_MIN, BNX_TX_COAL_BDS_MAX,
	    BNX_TX_COAL_BDS_CHG);
}

static int
bnx_sysctl_rx_coal_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_rx_coal_bds_int,
	    BNX_RX_COAL_BDS_MIN, BNX_RX_COAL_BDS_MAX,
	    BNX_RX_COAL_BDS_INT_CHG);
}

static int
bnx_sysctl_tx_coal_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_tx_coal_bds_int,
	    BNX_TX_COAL_BDS_MIN, BNX_TX_COAL_BDS_MAX,
	    BNX_TX_COAL_BDS_INT_CHG);
}

static int
bnx_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *coal,
    int coal_min, int coal_max, uint32_t coal_chg_mask)
{
	struct bnx_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	ifnet_serialize_all(ifp);

	v = *coal;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != NULL) {
		if (v < coal_min || v > coal_max) {
			error = EINVAL;
		} else {
			*coal = v;
			sc->bnx_coal_chg |= coal_chg_mask;

			/* Commit changes */
			bnx_coal_change(sc);
		}
	}

	ifnet_deserialize_all(ifp);
	return error;
}

static void
bnx_coal_change(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (sc->bnx_coal_chg & BNX_RX_COAL_TICKS_CHG) {
		if (sc->bnx_rx_retcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS,
			    sc->bnx_rx_coal_ticks);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS, 0);
			for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_RX_COAL_TICKS +
				    (i * BGE_VEC_COALSET_SIZE),
				    sc->bnx_rx_coal_ticks);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_RX_COAL_TICKS +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "rx_coal_ticks -> %u\n",
			    sc->bnx_rx_coal_ticks);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_TICKS_CHG) {
		if (sc->bnx_tx_ringcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS,
			    sc->bnx_tx_coal_ticks);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS, 0);
			for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_TX_COAL_TICKS +
				    (i * BGE_VEC_COALSET_SIZE),
				    sc->bnx_tx_coal_ticks);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_TX_COAL_TICKS +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "tx_coal_ticks -> %u\n",
			    sc->bnx_tx_coal_ticks);
		}
	}

	if (sc->bnx_coal_chg & BNX_RX_COAL_BDS_CHG) {
		uint32_t rx_coal_bds;

		if (ifp->if_flags & IFF_NPOLLING)
			rx_coal_bds = sc->bnx_rx_coal_bds_poll;
		else
			rx_coal_bds = sc->bnx_rx_coal_bds;

		if (sc->bnx_rx_retcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, rx_coal_bds);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, 0);
			for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_RX_MAX_COAL_BDS +
				    (i * BGE_VEC_COALSET_SIZE), rx_coal_bds);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_RX_MAX_COAL_BDS +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "%srx_coal_bds -> %u\n",
			    (ifp->if_flags & IFF_NPOLLING) ? "polling " : "",
			    rx_coal_bds);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_BDS_CHG) {
		uint32_t tx_coal_bds;

		if (ifp->if_flags & IFF_NPOLLING)
			tx_coal_bds = sc->bnx_tx_coal_bds_poll;
		else
			tx_coal_bds = sc->bnx_tx_coal_bds;

		if (sc->bnx_tx_ringcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, tx_coal_bds);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, 0);
			for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_TX_MAX_COAL_BDS +
				    (i * BGE_VEC_COALSET_SIZE), tx_coal_bds);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_TX_MAX_COAL_BDS +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "%stx_coal_bds -> %u\n",
			    (ifp->if_flags & IFF_NPOLLING) ? "polling " : "",
			    tx_coal_bds);
		}
	}

	if (sc->bnx_coal_chg & BNX_RX_COAL_BDS_INT_CHG) {
		if (sc->bnx_rx_retcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT,
			    sc->bnx_rx_coal_bds_int);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT, 0);
			for (i = 0; i < sc->bnx_rx_retcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_RX_MAX_COAL_BDS_INT +
				    (i * BGE_VEC_COALSET_SIZE),
				    sc->bnx_rx_coal_bds_int);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_RX_MAX_COAL_BDS_INT +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "rx_coal_bds_int -> %u\n",
			    sc->bnx_rx_coal_bds_int);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_BDS_INT_CHG) {
		if (sc->bnx_tx_ringcnt == 1) {
			CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT,
			    sc->bnx_tx_coal_bds_int);
			i = 0;
		} else {
			CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT, 0);
			for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
				CSR_WRITE_4(sc, BGE_VEC1_TX_MAX_COAL_BDS_INT +
				    (i * BGE_VEC_COALSET_SIZE),
				    sc->bnx_tx_coal_bds_int);
			}
		}
		for (; i < BNX_INTR_MAX - 1; ++i) {
			CSR_WRITE_4(sc, BGE_VEC1_TX_MAX_COAL_BDS_INT +
			    (i * BGE_VEC_COALSET_SIZE), 0);
		}
		if (bootverbose) {
			if_printf(ifp, "tx_coal_bds_int -> %u\n",
			    sc->bnx_tx_coal_bds_int);
		}
	}

	sc->bnx_coal_chg = 0;
}

static void
bnx_check_intr_rxtx(void *xintr)
{
	struct bnx_intr_data *intr = xintr;
	struct bnx_rx_ret_ring *ret;
	struct bnx_tx_ring *txr;
	struct ifnet *ifp;

	lwkt_serialize_enter(intr->bnx_intr_serialize);

	KKASSERT(mycpuid == intr->bnx_intr_cpuid);

	ifp = &intr->bnx_sc->arpcom.ac_if;
	if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) != IFF_RUNNING) {
		lwkt_serialize_exit(intr->bnx_intr_serialize);
		return;
	}

	txr = intr->bnx_txr;
	ret = intr->bnx_ret;

	if (*ret->bnx_rx_considx != ret->bnx_rx_saved_considx ||
	    *txr->bnx_tx_considx != txr->bnx_tx_saved_considx) {
		if (intr->bnx_rx_check_considx == ret->bnx_rx_saved_considx &&
		    intr->bnx_tx_check_considx == txr->bnx_tx_saved_considx) {
			if (!intr->bnx_intr_maylose) {
				intr->bnx_intr_maylose = TRUE;
				goto done;
			}
			if (bootverbose)
				if_printf(ifp, "lost interrupt\n");
			intr->bnx_intr_func(intr->bnx_intr_arg);
		}
	}
	intr->bnx_intr_maylose = FALSE;
	intr->bnx_rx_check_considx = ret->bnx_rx_saved_considx;
	intr->bnx_tx_check_considx = txr->bnx_tx_saved_considx;

done:
	callout_reset(&intr->bnx_intr_timer, BNX_INTR_CKINTVL,
	    intr->bnx_intr_check, intr);
	lwkt_serialize_exit(intr->bnx_intr_serialize);
}

static void
bnx_check_intr_tx(void *xintr)
{
	struct bnx_intr_data *intr = xintr;
	struct bnx_tx_ring *txr;
	struct ifnet *ifp;

	lwkt_serialize_enter(intr->bnx_intr_serialize);

	KKASSERT(mycpuid == intr->bnx_intr_cpuid);

	ifp = &intr->bnx_sc->arpcom.ac_if;
	if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) != IFF_RUNNING) {
		lwkt_serialize_exit(intr->bnx_intr_serialize);
		return;
	}

	txr = intr->bnx_txr;

	if (*txr->bnx_tx_considx != txr->bnx_tx_saved_considx) {
		if (intr->bnx_tx_check_considx == txr->bnx_tx_saved_considx) {
			if (!intr->bnx_intr_maylose) {
				intr->bnx_intr_maylose = TRUE;
				goto done;
			}
			if (bootverbose)
				if_printf(ifp, "lost interrupt\n");
			intr->bnx_intr_func(intr->bnx_intr_arg);
		}
	}
	intr->bnx_intr_maylose = FALSE;
	intr->bnx_tx_check_considx = txr->bnx_tx_saved_considx;

done:
	callout_reset(&intr->bnx_intr_timer, BNX_INTR_CKINTVL,
	    intr->bnx_intr_check, intr);
	lwkt_serialize_exit(intr->bnx_intr_serialize);
}

static void
bnx_check_intr_rx(void *xintr)
{
	struct bnx_intr_data *intr = xintr;
	struct bnx_rx_ret_ring *ret;
	struct ifnet *ifp;

	lwkt_serialize_enter(intr->bnx_intr_serialize);

	KKASSERT(mycpuid == intr->bnx_intr_cpuid);

	ifp = &intr->bnx_sc->arpcom.ac_if;
	if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) != IFF_RUNNING) {
		lwkt_serialize_exit(intr->bnx_intr_serialize);
		return;
	}

	ret = intr->bnx_ret;

	if (*ret->bnx_rx_considx != ret->bnx_rx_saved_considx) {
		if (intr->bnx_rx_check_considx == ret->bnx_rx_saved_considx) {
			if (!intr->bnx_intr_maylose) {
				intr->bnx_intr_maylose = TRUE;
				goto done;
			}
			if (bootverbose)
				if_printf(ifp, "lost interrupt\n");
			intr->bnx_intr_func(intr->bnx_intr_arg);
		}
	}
	intr->bnx_intr_maylose = FALSE;
	intr->bnx_rx_check_considx = ret->bnx_rx_saved_considx;

done:
	callout_reset(&intr->bnx_intr_timer, BNX_INTR_CKINTVL,
	    intr->bnx_intr_check, intr);
	lwkt_serialize_exit(intr->bnx_intr_serialize);
}

static void
bnx_enable_intr(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		lwkt_serialize_handler_enable(
		    sc->bnx_intr_data[i].bnx_intr_serialize);
	}

	/*
	 * Enable interrupt.
	 */
	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		bnx_writembx(sc, intr->bnx_intr_mbx,
		    (*intr->bnx_saved_status_tag) << 24);
		/* XXX Linux driver */
		bnx_writembx(sc, intr->bnx_intr_mbx,
		    (*intr->bnx_saved_status_tag) << 24);
	}

	/*
	 * Unmask the interrupt when we stop polling.
	 */
	PCI_CLRBIT(sc->bnx_dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_MASK_PCI_INTR, 4);

	/*
	 * Trigger another interrupt, since above writing
	 * to interrupt mailbox0 may acknowledge pending
	 * interrupt.
	 */
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);

	if (sc->bnx_flags & BNX_FLAG_STATUSTAG_BUG) {
		if (bootverbose)
			if_printf(ifp, "status tag bug workaround\n");

		for (i = 0; i < sc->bnx_intr_cnt; ++i) {
			struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

			if (intr->bnx_intr_check == NULL)
				continue;
			intr->bnx_intr_maylose = FALSE;
			intr->bnx_rx_check_considx = 0;
			intr->bnx_tx_check_considx = 0;
			callout_reset_bycpu(&intr->bnx_intr_timer,
			    BNX_INTR_CKINTVL, intr->bnx_intr_check, intr,
			    intr->bnx_intr_cpuid);
		}
	}
}

static void
bnx_disable_intr(struct bnx_softc *sc)
{
	int i;

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		callout_stop(&intr->bnx_intr_timer);
		intr->bnx_intr_maylose = FALSE;
		intr->bnx_rx_check_considx = 0;
		intr->bnx_tx_check_considx = 0;
	}

	/*
	 * Mask the interrupt when we start polling.
	 */
	PCI_SETBIT(sc->bnx_dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_MASK_PCI_INTR, 4);

	/*
	 * Acknowledge possible asserted interrupt.
	 */
	for (i = 0; i < BNX_INTR_MAX; ++i)
		bnx_writembx(sc, sc->bnx_intr_data[i].bnx_intr_mbx, 1);

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		lwkt_serialize_handler_disable(
		    sc->bnx_intr_data[i].bnx_intr_serialize);
	}
}

static int
bnx_get_eaddr_mem(struct bnx_softc *sc, uint8_t ether_addr[])
{
	uint32_t mac_addr;
	int ret = 1;

	mac_addr = bnx_readmem_ind(sc, 0x0c14);
	if ((mac_addr >> 16) == 0x484b) {
		ether_addr[0] = (uint8_t)(mac_addr >> 8);
		ether_addr[1] = (uint8_t)mac_addr;
		mac_addr = bnx_readmem_ind(sc, 0x0c18);
		ether_addr[2] = (uint8_t)(mac_addr >> 24);
		ether_addr[3] = (uint8_t)(mac_addr >> 16);
		ether_addr[4] = (uint8_t)(mac_addr >> 8);
		ether_addr[5] = (uint8_t)mac_addr;
		ret = 0;
	}
	return ret;
}

static int
bnx_get_eaddr_nvram(struct bnx_softc *sc, uint8_t ether_addr[])
{
	int mac_offset = BGE_EE_MAC_OFFSET;

	if (BNX_IS_5717_PLUS(sc)) {
		int f;

		f = pci_get_function(sc->bnx_dev);
		if (f & 1)
			mac_offset = BGE_EE_MAC_OFFSET_5717;
		if (f > 1)
			mac_offset += BGE_EE_MAC_OFFSET_5717_OFF;
	}

	return bnx_read_nvram(sc, ether_addr, mac_offset + 2, ETHER_ADDR_LEN);
}

static int
bnx_get_eaddr_eeprom(struct bnx_softc *sc, uint8_t ether_addr[])
{
	if (sc->bnx_flags & BNX_FLAG_NO_EEPROM)
		return 1;

	return bnx_read_eeprom(sc, ether_addr, BGE_EE_MAC_OFFSET + 2,
			       ETHER_ADDR_LEN);
}

static int
bnx_get_eaddr(struct bnx_softc *sc, uint8_t eaddr[])
{
	static const bnx_eaddr_fcn_t bnx_eaddr_funcs[] = {
		/* NOTE: Order is critical */
		bnx_get_eaddr_mem,
		bnx_get_eaddr_nvram,
		bnx_get_eaddr_eeprom,
		NULL
	};
	const bnx_eaddr_fcn_t *func;

	for (func = bnx_eaddr_funcs; *func != NULL; ++func) {
		if ((*func)(sc, eaddr) == 0)
			break;
	}
	return (*func == NULL ? ENXIO : 0);
}

/*
 * NOTE: 'm' is not freed upon failure
 */
static struct mbuf *
bnx_defrag_shortdma(struct mbuf *m)
{
	struct mbuf *n;
	int found;

	/*
	 * If device receive two back-to-back send BDs with less than
	 * or equal to 8 total bytes then the device may hang.  The two
	 * back-to-back send BDs must in the same frame for this failure
	 * to occur.  Scan mbuf chains and see whether two back-to-back
	 * send BDs are there.  If this is the case, allocate new mbuf
	 * and copy the frame to workaround the silicon bug.
	 */
	for (n = m, found = 0; n != NULL; n = n->m_next) {
		if (n->m_len < 8) {
			found++;
			if (found > 1)
				break;
			continue;
		}
		found = 0;
	}

	if (found > 1)
		n = m_defrag(m, M_NOWAIT);
	else
		n = m;
	return n;
}

static void
bnx_stop_block(struct bnx_softc *sc, bus_size_t reg, uint32_t bit)
{
	int i;

	BNX_CLRBIT(sc, reg, bit);
	for (i = 0; i < BNX_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, reg) & bit) == 0)
			return;
		DELAY(100);
	}
}

static void
bnx_link_poll(struct bnx_softc *sc)
{
	uint32_t status;

	status = CSR_READ_4(sc, BGE_MAC_STS);
	if ((status & sc->bnx_link_chg) || sc->bnx_link_evt) {
		sc->bnx_link_evt = 0;
		sc->bnx_link_upd(sc, status);
	}
}

static void
bnx_enable_msi(struct bnx_softc *sc, boolean_t is_msix)
{
	uint32_t msi_mode;

	msi_mode = CSR_READ_4(sc, BGE_MSI_MODE);
	msi_mode |= BGE_MSIMODE_ENABLE;
	/*
	 * NOTE:
	 * 5718-PG105-R says that "one shot" mode does not work
	 * if MSI is used, however, it obviously works.
	 */
	msi_mode &= ~BGE_MSIMODE_ONESHOT_DISABLE;
	if (is_msix)
		msi_mode |= BGE_MSIMODE_MSIX_MULTIMODE;
	else
		msi_mode &= ~BGE_MSIMODE_MSIX_MULTIMODE;
	CSR_WRITE_4(sc, BGE_MSI_MODE, msi_mode);
}

static uint32_t
bnx_dma_swap_options(struct bnx_softc *sc)
{
	uint32_t dma_options;

	dma_options = BGE_MODECTL_WORDSWAP_NONFRAME |
	    BGE_MODECTL_BYTESWAP_DATA | BGE_MODECTL_WORDSWAP_DATA;
#if BYTE_ORDER == BIG_ENDIAN
	dma_options |= BGE_MODECTL_BYTESWAP_NONFRAME;
#endif
	return dma_options;
}

static int
bnx_setup_tso(struct bnx_tx_ring *txr, struct mbuf **mp,
    uint16_t *mss0, uint16_t *flags0)
{
	struct mbuf *m;
	struct ip *ip;
	struct tcphdr *th;
	int thoff, iphlen, hoff, hlen;
	uint16_t flags, mss;

	m = *mp;
	KASSERT(M_WRITABLE(m), ("TSO mbuf not writable"));

	hoff = m->m_pkthdr.csum_lhlen;
	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;

	KASSERT(hoff > 0, ("invalid ether header len"));
	KASSERT(iphlen > 0, ("invalid ip header len"));
	KASSERT(thoff > 0, ("invalid tcp header len"));

	if (__predict_false(m->m_len < hoff + iphlen + thoff)) {
		m = m_pullup(m, hoff + iphlen + thoff);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		*mp = m;
	}
	ip = mtodoff(m, struct ip *, hoff);
	th = mtodoff(m, struct tcphdr *, hoff + iphlen);

	mss = m->m_pkthdr.tso_segsz;
	flags = BGE_TXBDFLAG_CPU_PRE_DMA | BGE_TXBDFLAG_CPU_POST_DMA;

	ip->ip_len = htons(mss + iphlen + thoff);
	th->th_sum = 0;

	hlen = (iphlen + thoff) >> 2;
	mss |= ((hlen & 0x3) << 14);
	flags |= ((hlen & 0xf8) << 7) | ((hlen & 0x4) << 2);

	*mss0 = mss;
	*flags0 = flags;

	return 0;
}

static int
bnx_create_tx_ring(struct bnx_tx_ring *txr)
{
	bus_size_t txmaxsz, txmaxsegsz;
	int i, error;

	lwkt_serialize_init(&txr->bnx_tx_serialize);

	/*
	 * Create DMA tag and maps for TX mbufs.
	 */
	if (txr->bnx_sc->bnx_flags & BNX_FLAG_TSO)
		txmaxsz = IP_MAXPACKET + sizeof(struct ether_vlan_header);
	else
		txmaxsz = BNX_JUMBO_FRAMELEN;
	if (txr->bnx_sc->bnx_asicrev == BGE_ASICREV_BCM57766)
		txmaxsegsz = MCLBYTES;
	else
		txmaxsegsz = PAGE_SIZE;
	error = bus_dma_tag_create(txr->bnx_sc->bnx_cdata.bnx_parent_tag,
	    1, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    txmaxsz, BNX_NSEG_NEW, txmaxsegsz,
	    BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
	    &txr->bnx_tx_mtag);
	if (error) {
		device_printf(txr->bnx_sc->bnx_dev,
		    "could not create TX mbuf DMA tag\n");
		return error;
	}

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		error = bus_dmamap_create(txr->bnx_tx_mtag,
		    BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
		    &txr->bnx_tx_buf[i].bnx_tx_dmamap);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(txr->bnx_tx_mtag,
				    txr->bnx_tx_buf[j].bnx_tx_dmamap);
			}
			bus_dma_tag_destroy(txr->bnx_tx_mtag);
			txr->bnx_tx_mtag = NULL;

			device_printf(txr->bnx_sc->bnx_dev,
			    "could not create TX mbuf DMA map\n");
			return error;
		}
	}

	/*
	 * Create DMA stuffs for TX ring.
	 */
	error = bnx_dma_block_alloc(txr->bnx_sc, BGE_TX_RING_SZ,
	    &txr->bnx_tx_ring_tag,
	    &txr->bnx_tx_ring_map,
	    (void *)&txr->bnx_tx_ring,
	    &txr->bnx_tx_ring_paddr);
	if (error) {
		device_printf(txr->bnx_sc->bnx_dev,
		    "could not create TX ring\n");
		return error;
	}

	txr->bnx_tx_flags |= BNX_TX_FLAG_SHORTDMA;
	txr->bnx_tx_wreg = BNX_TX_WREG_NSEGS;

	return 0;
}

static void
bnx_destroy_tx_ring(struct bnx_tx_ring *txr)
{
	/* Destroy TX mbuf DMA stuffs. */
	if (txr->bnx_tx_mtag != NULL) {
		int i;

		for (i = 0; i < BGE_TX_RING_CNT; i++) {
			KKASSERT(txr->bnx_tx_buf[i].bnx_tx_mbuf == NULL);
			bus_dmamap_destroy(txr->bnx_tx_mtag,
			    txr->bnx_tx_buf[i].bnx_tx_dmamap);
		}
		bus_dma_tag_destroy(txr->bnx_tx_mtag);
	}

	/* Destroy TX ring */
	bnx_dma_block_free(txr->bnx_tx_ring_tag,
	    txr->bnx_tx_ring_map, txr->bnx_tx_ring);
}

static int
bnx_sysctl_force_defrag(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bnx_tx_ring *txr = &sc->bnx_tx_ring[0];
	int error, defrag, i;

	if (txr->bnx_tx_flags & BNX_TX_FLAG_FORCE_DEFRAG)
		defrag = 1;
	else
		defrag = 0;

	error = sysctl_handle_int(oidp, &defrag, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i) {
		txr = &sc->bnx_tx_ring[i];
		if (defrag)
			txr->bnx_tx_flags |= BNX_TX_FLAG_FORCE_DEFRAG;
		else
			txr->bnx_tx_flags &= ~BNX_TX_FLAG_FORCE_DEFRAG;
	}
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
bnx_sysctl_tx_wreg(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bnx_tx_ring *txr = &sc->bnx_tx_ring[0];
	int error, tx_wreg, i;

	tx_wreg = txr->bnx_tx_wreg;
	error = sysctl_handle_int(oidp, &tx_wreg, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);
	for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
		sc->bnx_tx_ring[i].bnx_tx_wreg = tx_wreg;
	ifnet_deserialize_all(ifp);

	return 0;
}

static int
bnx_create_rx_ret_ring(struct bnx_rx_ret_ring *ret)
{
	int error;

	lwkt_serialize_init(&ret->bnx_rx_ret_serialize);

	/*
	 * Create DMA stuffs for RX return ring.
	 */
	error = bnx_dma_block_alloc(ret->bnx_sc,
	    BGE_RX_RTN_RING_SZ(BNX_RETURN_RING_CNT),
	    &ret->bnx_rx_ret_ring_tag,
	    &ret->bnx_rx_ret_ring_map,
	    (void *)&ret->bnx_rx_ret_ring,
	    &ret->bnx_rx_ret_ring_paddr);
	if (error) {
		device_printf(ret->bnx_sc->bnx_dev,
		    "could not create RX ret ring\n");
		return error;
	}

	/* Shadow standard ring's RX mbuf DMA tag */
	ret->bnx_rx_mtag = ret->bnx_std->bnx_rx_mtag;

	/*
	 * Create tmp DMA map for RX mbufs.
	 */
	error = bus_dmamap_create(ret->bnx_rx_mtag, BUS_DMA_WAITOK,
	    &ret->bnx_rx_tmpmap);
	if (error) {
		device_printf(ret->bnx_sc->bnx_dev,
		    "could not create tmp RX mbuf DMA map\n");
		ret->bnx_rx_mtag = NULL;
		return error;
	}
	return 0;
}

static void
bnx_destroy_rx_ret_ring(struct bnx_rx_ret_ring *ret)
{
	/* Destroy tmp RX mbuf DMA map */
	if (ret->bnx_rx_mtag != NULL)
		bus_dmamap_destroy(ret->bnx_rx_mtag, ret->bnx_rx_tmpmap);

	/* Destroy RX return ring */
	bnx_dma_block_free(ret->bnx_rx_ret_ring_tag,
	    ret->bnx_rx_ret_ring_map, ret->bnx_rx_ret_ring);
}

static int
bnx_alloc_intr(struct bnx_softc *sc)
{
	struct bnx_intr_data *intr;
	u_int intr_flags;
	int error;

	if (sc->bnx_intr_cnt > 1) {
		error = bnx_alloc_msix(sc);
		if (error)
			return error;
		KKASSERT(sc->bnx_intr_type == PCI_INTR_TYPE_MSIX);
		return 0;
	}

	KKASSERT(sc->bnx_intr_cnt == 1);

	intr = &sc->bnx_intr_data[0];
	intr->bnx_ret = &sc->bnx_rx_ret_ring[0];
	intr->bnx_txr = &sc->bnx_tx_ring[0];
	intr->bnx_intr_serialize = &sc->bnx_main_serialize;
	intr->bnx_intr_check = bnx_check_intr_rxtx;
	intr->bnx_saved_status_tag = &intr->bnx_ret->bnx_saved_status_tag;

	sc->bnx_intr_type = pci_alloc_1intr(sc->bnx_dev, bnx_msi_enable,
	    &intr->bnx_intr_rid, &intr_flags);

	intr->bnx_intr_res = bus_alloc_resource_any(sc->bnx_dev, SYS_RES_IRQ,
	    &intr->bnx_intr_rid, intr_flags);
	if (intr->bnx_intr_res == NULL) {
		device_printf(sc->bnx_dev, "could not alloc interrupt\n");
		return ENXIO;
	}

	if (sc->bnx_intr_type == PCI_INTR_TYPE_MSI) {
		bnx_enable_msi(sc, FALSE);
		intr->bnx_intr_func = bnx_msi;
		if (bootverbose)
			device_printf(sc->bnx_dev, "oneshot MSI\n");
	} else {
		intr->bnx_intr_func = bnx_intr_legacy;
	}
	intr->bnx_intr_arg = sc;
	intr->bnx_intr_cpuid = rman_get_cpuid(intr->bnx_intr_res);

	intr->bnx_txr->bnx_tx_cpuid = intr->bnx_intr_cpuid;

	return 0;
}

static int
bnx_setup_intr(struct bnx_softc *sc)
{
	int error, i;

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		error = bus_setup_intr_descr(sc->bnx_dev, intr->bnx_intr_res,
		    INTR_MPSAFE, intr->bnx_intr_func, intr->bnx_intr_arg,
		    &intr->bnx_intr_hand, intr->bnx_intr_serialize,
		    intr->bnx_intr_desc);
		if (error) {
			device_printf(sc->bnx_dev,
			    "could not set up %dth intr\n", i);
			bnx_teardown_intr(sc, i);
			return error;
		}
	}
	return 0;
}

static void
bnx_teardown_intr(struct bnx_softc *sc, int cnt)
{
	int i;

	for (i = 0; i < cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		bus_teardown_intr(sc->bnx_dev, intr->bnx_intr_res,
		    intr->bnx_intr_hand);
	}
}

static void
bnx_free_intr(struct bnx_softc *sc)
{
	if (sc->bnx_intr_type != PCI_INTR_TYPE_MSIX) {
		struct bnx_intr_data *intr;

		KKASSERT(sc->bnx_intr_cnt <= 1);
		intr = &sc->bnx_intr_data[0];

		if (intr->bnx_intr_res != NULL) {
			bus_release_resource(sc->bnx_dev, SYS_RES_IRQ,
			    intr->bnx_intr_rid, intr->bnx_intr_res);
		}
		if (sc->bnx_intr_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->bnx_dev);
	} else {
		bnx_free_msix(sc, TRUE);
	}
}

static void
bnx_setup_serialize(struct bnx_softc *sc)
{
	int i, j;

	/*
	 * Allocate serializer array
	 */

	/* Main + RX STD + TX + RX RET */
	sc->bnx_serialize_cnt = 1 + 1 + sc->bnx_tx_ringcnt + sc->bnx_rx_retcnt;

	sc->bnx_serialize =
	    kmalloc(sc->bnx_serialize_cnt * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializers
	 *
	 * NOTE: Order is critical
	 */

	i = 0;

	KKASSERT(i < sc->bnx_serialize_cnt);
	sc->bnx_serialize[i++] = &sc->bnx_main_serialize;

	KKASSERT(i < sc->bnx_serialize_cnt);
	sc->bnx_serialize[i++] = &sc->bnx_rx_std_ring.bnx_rx_std_serialize;

	for (j = 0; j < sc->bnx_rx_retcnt; ++j) {
		KKASSERT(i < sc->bnx_serialize_cnt);
		sc->bnx_serialize[i++] =
		    &sc->bnx_rx_ret_ring[j].bnx_rx_ret_serialize;
	}

	for (j = 0; j < sc->bnx_tx_ringcnt; ++j) {
		KKASSERT(i < sc->bnx_serialize_cnt);
		sc->bnx_serialize[i++] =
		    &sc->bnx_tx_ring[j].bnx_tx_serialize;
	}

	KKASSERT(i == sc->bnx_serialize_cnt);
}

static void
bnx_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bnx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->bnx_serialize,
	    sc->bnx_serialize_cnt, slz);
}

static void
bnx_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bnx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->bnx_serialize,
	    sc->bnx_serialize_cnt, slz);
}

static int
bnx_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bnx_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->bnx_serialize,
	    sc->bnx_serialize_cnt, slz);
}

#ifdef INVARIANTS

static void
bnx_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct bnx_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->bnx_serialize, sc->bnx_serialize_cnt,
	    slz, serialized);
}

#endif	/* INVARIANTS */

static void
bnx_set_tick_cpuid(struct bnx_softc *sc, boolean_t polling)
{
	if (polling)
		sc->bnx_tick_cpuid = 0; /* XXX */
	else
		sc->bnx_tick_cpuid = sc->bnx_intr_data[0].bnx_intr_cpuid;
}

static void
bnx_rx_std_refill_ithread(void *xstd)
{
	struct bnx_rx_std_ring *std = xstd;
	struct globaldata *gd = mycpu;

	crit_enter_gd(gd);

	while (!std->bnx_rx_std_stop) {
		if (std->bnx_rx_std_refill) {
			lwkt_serialize_handler_call(
			    &std->bnx_rx_std_serialize,
			    bnx_rx_std_refill, std, NULL);
		}

		crit_exit_gd(gd);
		crit_enter_gd(gd);

		atomic_poll_release_int(&std->bnx_rx_std_running);
		cpu_mfence();

		if (!std->bnx_rx_std_refill && !std->bnx_rx_std_stop) {
			lwkt_deschedule_self(gd->gd_curthread);
			lwkt_switch();
		}
	}

	crit_exit_gd(gd);

	wakeup(std);

	lwkt_exit();
}

static void
bnx_rx_std_refill(void *xstd, void *frame __unused)
{
	struct bnx_rx_std_ring *std = xstd;
	int cnt, refill_mask;

again:
	cnt = 0;

	cpu_lfence();
	refill_mask = std->bnx_rx_std_refill;
	atomic_clear_int(&std->bnx_rx_std_refill, refill_mask);

	while (refill_mask) {
		uint16_t check_idx = std->bnx_rx_std;
		int ret_idx;

		ret_idx = bsfl(refill_mask);
		for (;;) {
			struct bnx_rx_buf *rb;
			int refilled;

			BNX_INC(check_idx, BGE_STD_RX_RING_CNT);
			rb = &std->bnx_rx_std_buf[check_idx];
			refilled = rb->bnx_rx_refilled;
			cpu_lfence();
			if (refilled) {
				bnx_setup_rxdesc_std(std, check_idx);
				std->bnx_rx_std = check_idx;
				++cnt;
				if (cnt >= 8) {
					atomic_subtract_int(
					    &std->bnx_rx_std_used, cnt);
					bnx_writembx(std->bnx_sc,
					    BGE_MBX_RX_STD_PROD_LO,
					    std->bnx_rx_std);
					cnt = 0;
				}
			} else {
				break;
			}
		}
		refill_mask &= ~(1 << ret_idx);
	}

	if (cnt) {
		atomic_subtract_int(&std->bnx_rx_std_used, cnt);
		bnx_writembx(std->bnx_sc, BGE_MBX_RX_STD_PROD_LO,
		    std->bnx_rx_std);
	}

	if (std->bnx_rx_std_refill)
		goto again;

	atomic_poll_release_int(&std->bnx_rx_std_running);
	cpu_mfence();

	if (std->bnx_rx_std_refill)
		goto again;
}

static int
bnx_sysctl_std_refill(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = (void *)arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bnx_rx_ret_ring *ret = &sc->bnx_rx_ret_ring[0];
	int error, cntmax, i;

	cntmax = ret->bnx_rx_cntmax;
	error = sysctl_handle_int(oidp, &cntmax, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ifnet_serialize_all(ifp);

	if ((cntmax * sc->bnx_rx_retcnt) >= BGE_STD_RX_RING_CNT / 2) {
		error = EINVAL;
		goto back;
	}

	for (i = 0; i < sc->bnx_tx_ringcnt; ++i)
		sc->bnx_rx_ret_ring[i].bnx_rx_cntmax = cntmax;
	error = 0;

back:
	ifnet_deserialize_all(ifp);

	return error;
}

static void
bnx_init_rss(struct bnx_softc *sc)
{
	uint8_t key[BGE_RSS_KEYREG_CNT * BGE_RSS_KEYREG_SIZE];
	int i, j, r;

	KKASSERT(BNX_RSS_ENABLED(sc));

	/*
	 * Configure RSS redirect table.
	 */
	if_ringmap_rdrtable(sc->bnx_rx_rmap, sc->bnx_rdr_table,
	    BNX_RDRTABLE_SIZE);
	r = 0;
	for (j = 0; j < BGE_RSS_INDIR_TBL_CNT; ++j) {
		uint32_t tbl = 0;

		for (i = 0; i < BGE_RSS_INDIR_TBLENT_CNT; ++i) {
			uint32_t q;

			q = sc->bnx_rdr_table[r];
			tbl |= q << (BGE_RSS_INDIR_TBLENT_SHIFT *
			    (BGE_RSS_INDIR_TBLENT_CNT - i - 1));
			++r;
		}

		BNX_RSS_DPRINTF(sc, 1, "tbl%d %08x\n", j, tbl);
		CSR_WRITE_4(sc, BGE_RSS_INDIR_TBL(j), tbl);
	}

	toeplitz_get_key(key, sizeof(key));
	for (i = 0; i < BGE_RSS_KEYREG_CNT; ++i) {
		uint32_t keyreg;

		keyreg = BGE_RSS_KEYREG_VAL(key, i);

		BNX_RSS_DPRINTF(sc, 1, "key%d %08x\n", i, keyreg);
		CSR_WRITE_4(sc, BGE_RSS_KEYREG(i), keyreg);
	}
}

static void
bnx_setup_ring_cnt(struct bnx_softc *sc)
{
	int msix_enable, msix_cnt, msix_ring, ring_max, ring_cnt;

	/* One RX ring. */
	sc->bnx_rx_rmap = if_ringmap_alloc(sc->bnx_dev, 1, 1);

	if (netisr_ncpus == 1)
		goto skip_rx;

	msix_enable = device_getenv_int(sc->bnx_dev, "msix.enable",
	    bnx_msix_enable);
	if (!msix_enable)
		goto skip_rx;

	/*
	 * One MSI-X vector is dedicated to status or single TX queue,
	 * so make sure that there are enough MSI-X vectors.
	 */
	msix_cnt = pci_msix_count(sc->bnx_dev);
	if (msix_cnt <= 1)
		goto skip_rx;
	if (bootverbose)
		device_printf(sc->bnx_dev, "MSI-X count %d\n", msix_cnt);
	msix_ring = msix_cnt - 1;

	/*
	 * Setup RX ring count
	 */
	ring_max = BNX_RX_RING_MAX;
	if (ring_max > msix_ring)
		ring_max = msix_ring;
	ring_cnt = device_getenv_int(sc->bnx_dev, "rx_rings", bnx_rx_rings);

	if_ringmap_free(sc->bnx_rx_rmap);
	sc->bnx_rx_rmap = if_ringmap_alloc(sc->bnx_dev, ring_cnt, ring_max);

skip_rx:
	sc->bnx_rx_retcnt = if_ringmap_count(sc->bnx_rx_rmap);

	/*
	 * Setup TX ring count
	 *
	 * Currently only BCM5719 and BCM5720 support multiple TX rings
	 * and the TX ring count must be less than the RX ring count.
	 */
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		ring_max = BNX_TX_RING_MAX;
		if (ring_max > sc->bnx_rx_retcnt)
			ring_max = sc->bnx_rx_retcnt;
		ring_cnt = device_getenv_int(sc->bnx_dev, "tx_rings",
		    bnx_tx_rings);
	} else {
		ring_max = 1;
		ring_cnt = 1;
	}
	sc->bnx_tx_rmap = if_ringmap_alloc(sc->bnx_dev, ring_cnt, ring_max);
	if_ringmap_align(sc->bnx_dev, sc->bnx_rx_rmap, sc->bnx_tx_rmap);

	sc->bnx_tx_ringcnt = if_ringmap_count(sc->bnx_tx_rmap);
	KASSERT(sc->bnx_tx_ringcnt <= sc->bnx_rx_retcnt,
	    ("invalid TX ring count %d and RX ring count %d",
	     sc->bnx_tx_ringcnt, sc->bnx_rx_retcnt));

	/*
	 * Setup interrupt count.
	 */
	if (sc->bnx_rx_retcnt == 1) {
		sc->bnx_intr_cnt = 1;
	} else {
		/*
		 * We need one extra MSI-X vector for link status or
		 * TX ring (if only one TX ring is enabled).
		 */
		sc->bnx_intr_cnt = sc->bnx_rx_retcnt + 1;
	}
	KKASSERT(sc->bnx_intr_cnt <= BNX_INTR_MAX);

	if (bootverbose) {
		device_printf(sc->bnx_dev, "intr count %d, "
		    "RX ring %d, TX ring %d\n", sc->bnx_intr_cnt,
		    sc->bnx_rx_retcnt, sc->bnx_tx_ringcnt);
	}
}

static int
bnx_alloc_msix(struct bnx_softc *sc)
{
	struct bnx_intr_data *intr;
	boolean_t setup = FALSE;
	int error, i;

	KKASSERT(sc->bnx_intr_cnt > 1);
	KKASSERT(sc->bnx_intr_cnt == sc->bnx_rx_retcnt + 1);

	if (sc->bnx_flags & BNX_FLAG_RXTX_BUNDLE) {
		/*
		 * Link status
		 */
		intr = &sc->bnx_intr_data[0];

		intr->bnx_intr_serialize = &sc->bnx_main_serialize;
		intr->bnx_saved_status_tag = &sc->bnx_saved_status_tag;

		intr->bnx_intr_func = bnx_msix_status;
		intr->bnx_intr_arg = sc;
		intr->bnx_intr_cpuid = 0; /* XXX */

		ksnprintf(intr->bnx_intr_desc0, sizeof(intr->bnx_intr_desc0),
		    "%s sts", device_get_nameunit(sc->bnx_dev));
		intr->bnx_intr_desc = intr->bnx_intr_desc0;

		/*
		 * RX/TX rings
		 */
		for (i = 1; i < sc->bnx_intr_cnt; ++i) {
			int idx = i - 1;

			intr = &sc->bnx_intr_data[i];

			KKASSERT(idx < sc->bnx_rx_retcnt);
			intr->bnx_ret = &sc->bnx_rx_ret_ring[idx];
			if (idx < sc->bnx_tx_ringcnt) {
				intr->bnx_txr = &sc->bnx_tx_ring[idx];
				intr->bnx_ret->bnx_txr = intr->bnx_txr;
			}

			intr->bnx_intr_serialize =
			    &intr->bnx_ret->bnx_rx_ret_serialize;
			intr->bnx_saved_status_tag =
			    &intr->bnx_ret->bnx_saved_status_tag;

			intr->bnx_intr_arg = intr->bnx_ret;
			intr->bnx_intr_cpuid =
			    if_ringmap_cpumap(sc->bnx_rx_rmap, idx);
			KKASSERT(intr->bnx_intr_cpuid < netisr_ncpus);

			if (intr->bnx_txr == NULL) {
				intr->bnx_intr_check = bnx_check_intr_rx;
				intr->bnx_intr_func = bnx_msix_rx;
				ksnprintf(intr->bnx_intr_desc0,
				    sizeof(intr->bnx_intr_desc0), "%s rx%d",
				    device_get_nameunit(sc->bnx_dev), idx);
			} else {
#ifdef INVARIANTS
				int tx_cpuid;
#endif

				intr->bnx_intr_check = bnx_check_intr_rxtx;
				intr->bnx_intr_func = bnx_msix_rxtx;
				ksnprintf(intr->bnx_intr_desc0,
				    sizeof(intr->bnx_intr_desc0), "%s rxtx%d",
				    device_get_nameunit(sc->bnx_dev), idx);

#ifdef INVARIANTS
				tx_cpuid = if_ringmap_cpumap(sc->bnx_tx_rmap,
				    idx);
				KASSERT(intr->bnx_intr_cpuid == tx_cpuid,
				    ("RX intr cpu%d, TX intr cpu%d, mismatch",
				     intr->bnx_intr_cpuid, tx_cpuid));
#endif
				intr->bnx_txr->bnx_tx_cpuid =
				    intr->bnx_intr_cpuid;
			}
			intr->bnx_intr_desc = intr->bnx_intr_desc0;

			intr->bnx_ret->bnx_msix_mbx = intr->bnx_intr_mbx;
		}
	} else {
		/*
		 * TX ring0 and link status
		 */
		intr = &sc->bnx_intr_data[0];

		intr->bnx_txr = &sc->bnx_tx_ring[0];
		intr->bnx_intr_serialize = &sc->bnx_main_serialize;
		intr->bnx_intr_check = bnx_check_intr_tx;
		intr->bnx_saved_status_tag =
		    &intr->bnx_txr->bnx_saved_status_tag;

		intr->bnx_intr_func = bnx_msix_tx_status;
		intr->bnx_intr_arg = intr->bnx_txr;
		intr->bnx_intr_cpuid = if_ringmap_cpumap(sc->bnx_tx_rmap, 0);
		KKASSERT(intr->bnx_intr_cpuid < netisr_ncpus);

		ksnprintf(intr->bnx_intr_desc0, sizeof(intr->bnx_intr_desc0),
		    "%s ststx", device_get_nameunit(sc->bnx_dev));
		intr->bnx_intr_desc = intr->bnx_intr_desc0;

		intr->bnx_txr->bnx_tx_cpuid = intr->bnx_intr_cpuid;

		/*
		 * RX rings
		 */
		for (i = 1; i < sc->bnx_intr_cnt; ++i) {
			int idx = i - 1;

			intr = &sc->bnx_intr_data[i];

			KKASSERT(idx < sc->bnx_rx_retcnt);
			intr->bnx_ret = &sc->bnx_rx_ret_ring[idx];
			intr->bnx_intr_serialize =
			    &intr->bnx_ret->bnx_rx_ret_serialize;
			intr->bnx_intr_check = bnx_check_intr_rx;
			intr->bnx_saved_status_tag =
			    &intr->bnx_ret->bnx_saved_status_tag;

			intr->bnx_intr_func = bnx_msix_rx;
			intr->bnx_intr_arg = intr->bnx_ret;
			intr->bnx_intr_cpuid =
			    if_ringmap_cpumap(sc->bnx_rx_rmap, idx);
			KKASSERT(intr->bnx_intr_cpuid < netisr_ncpus);

			ksnprintf(intr->bnx_intr_desc0,
			    sizeof(intr->bnx_intr_desc0), "%s rx%d",
			    device_get_nameunit(sc->bnx_dev), idx);
			intr->bnx_intr_desc = intr->bnx_intr_desc0;

			intr->bnx_ret->bnx_msix_mbx = intr->bnx_intr_mbx;
		}
	}

	if (BNX_IS_5717_PLUS(sc)) {
		sc->bnx_msix_mem_rid = PCIR_BAR(4);
	} else {
		if (sc->bnx_res2 == NULL)
			sc->bnx_msix_mem_rid = PCIR_BAR(2);
	}
	if (sc->bnx_msix_mem_rid != 0) {
		sc->bnx_msix_mem_res = bus_alloc_resource_any(sc->bnx_dev,
		    SYS_RES_MEMORY, &sc->bnx_msix_mem_rid, RF_ACTIVE);
		if (sc->bnx_msix_mem_res == NULL) {
			device_printf(sc->bnx_dev,
			    "could not alloc MSI-X table\n");
			return ENXIO;
		}
	}

	bnx_enable_msi(sc, TRUE);

	error = pci_setup_msix(sc->bnx_dev);
	if (error) {
		device_printf(sc->bnx_dev, "could not setup MSI-X\n");
		goto back;
	}
	setup = TRUE;

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		intr = &sc->bnx_intr_data[i];

		error = pci_alloc_msix_vector(sc->bnx_dev, i,
		    &intr->bnx_intr_rid, intr->bnx_intr_cpuid);
		if (error) {
			device_printf(sc->bnx_dev,
			    "could not alloc MSI-X %d on cpu%d\n",
			    i, intr->bnx_intr_cpuid);
			goto back;
		}

		intr->bnx_intr_res = bus_alloc_resource_any(sc->bnx_dev,
		    SYS_RES_IRQ, &intr->bnx_intr_rid, RF_ACTIVE);
		if (intr->bnx_intr_res == NULL) {
			device_printf(sc->bnx_dev,
			    "could not alloc MSI-X %d resource\n", i);
			error = ENXIO;
			goto back;
		}
	}

	pci_enable_msix(sc->bnx_dev);
	sc->bnx_intr_type = PCI_INTR_TYPE_MSIX;
back:
	if (error)
		bnx_free_msix(sc, setup);
	return error;
}

static void
bnx_free_msix(struct bnx_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->bnx_intr_cnt > 1);

	for (i = 0; i < sc->bnx_intr_cnt; ++i) {
		struct bnx_intr_data *intr = &sc->bnx_intr_data[i];

		if (intr->bnx_intr_res != NULL) {
			bus_release_resource(sc->bnx_dev, SYS_RES_IRQ,
			    intr->bnx_intr_rid, intr->bnx_intr_res);
		}
		if (intr->bnx_intr_rid >= 0) {
			pci_release_msix_vector(sc->bnx_dev,
			    intr->bnx_intr_rid);
		}
	}
	if (setup)
		pci_teardown_msix(sc->bnx_dev);
}

static void
bnx_rx_std_refill_sched_ipi(void *xret)
{
	struct bnx_rx_ret_ring *ret = xret;
	struct bnx_rx_std_ring *std = ret->bnx_std;
	struct globaldata *gd = mycpu;

	crit_enter_gd(gd);

	atomic_set_int(&std->bnx_rx_std_refill, ret->bnx_rx_mask);
	cpu_sfence();

	KKASSERT(std->bnx_rx_std_ithread->td_gd == gd);
	lwkt_schedule(std->bnx_rx_std_ithread);

	crit_exit_gd(gd);
}

static void
bnx_rx_std_refill_stop(void *xstd)
{
	struct bnx_rx_std_ring *std = xstd;
	struct globaldata *gd = mycpu;

	crit_enter_gd(gd);

	std->bnx_rx_std_stop = 1;
	cpu_sfence();

	KKASSERT(std->bnx_rx_std_ithread->td_gd == gd);
	lwkt_schedule(std->bnx_rx_std_ithread);

	crit_exit_gd(gd);
}

static void
bnx_serialize_skipmain(struct bnx_softc *sc)
{
	lwkt_serialize_array_enter(sc->bnx_serialize,
	    sc->bnx_serialize_cnt, 1);
}

static void
bnx_deserialize_skipmain(struct bnx_softc *sc)
{
	lwkt_serialize_array_exit(sc->bnx_serialize,
	    sc->bnx_serialize_cnt, 1);
}

static void
bnx_rx_std_refill_sched(struct bnx_rx_ret_ring *ret,
    struct bnx_rx_std_ring *std)
{
	struct globaldata *gd = mycpu;

	ret->bnx_rx_cnt = 0;
	cpu_sfence();

	crit_enter_gd(gd);

	atomic_set_int(&std->bnx_rx_std_refill, ret->bnx_rx_mask);
	cpu_sfence();
	if (atomic_poll_acquire_int(&std->bnx_rx_std_running)) {
		if (std->bnx_rx_std_ithread->td_gd == gd) {
			lwkt_schedule(std->bnx_rx_std_ithread);
		} else {
			lwkt_send_ipiq(std->bnx_rx_std_ithread->td_gd,
			    bnx_rx_std_refill_sched_ipi, ret);
		}
	}

	crit_exit_gd(gd);
}

static struct pktinfo *
bnx_rss_info(struct pktinfo *pi, const struct bge_rx_bd *cur_rx)
{
	/* Don't pick up IPv6 packet */
	if (cur_rx->bge_flags & BGE_RXBDFLAG_IPV6)
		return NULL;

	/* Don't pick up IP packet w/o IP checksum */
	if ((cur_rx->bge_flags & BGE_RXBDFLAG_IP_CSUM) == 0 ||
	    (cur_rx->bge_error_flag & BGE_RXERRFLAG_IP_CSUM_NOK))
		return NULL;

	/* Don't pick up IP packet w/o TCP/UDP checksum */
	if ((cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_CSUM) == 0)
		return NULL;

	/* May be IP fragment */
	if (cur_rx->bge_tcp_udp_csum != 0xffff)
		return NULL;

	if (cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_IS_TCP)
		pi->pi_l3proto = IPPROTO_TCP;
	else
		pi->pi_l3proto = IPPROTO_UDP;
	pi->pi_netisr = NETISR_IP;
	pi->pi_flags = 0;

	return pi;
}

static void
bnx_sig_pre_reset(struct bnx_softc *sc, int type)
{
	if (type == BNX_RESET_START || type == BNX_RESET_SUSPEND)
		bnx_ape_driver_state_change(sc, type);
}

static void
bnx_sig_post_reset(struct bnx_softc *sc, int type)
{
	if (type == BNX_RESET_SHUTDOWN)
		bnx_ape_driver_state_change(sc, type);
}

/*
 * Clear all stale locks and select the lock for this driver instance.
 */
static void
bnx_ape_lock_init(struct bnx_softc *sc)
{
	uint32_t bit, regbase;
	int i;

	regbase = BGE_APE_PER_LOCK_GRANT;

	/* Clear any stale locks. */
	for (i = BGE_APE_LOCK_PHY0; i <= BGE_APE_LOCK_GPIO; i++) {
		switch (i) {
		case BGE_APE_LOCK_PHY0:
		case BGE_APE_LOCK_PHY1:
		case BGE_APE_LOCK_PHY2:
		case BGE_APE_LOCK_PHY3:
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
			break;

		default:
			if (sc->bnx_func_addr == 0)
				bit = BGE_APE_LOCK_GRANT_DRIVER0;
			else
				bit = 1 << sc->bnx_func_addr;
			break;
		}
		APE_WRITE_4(sc, regbase + 4 * i, bit);
	}

	/* Select the PHY lock based on the device's function number. */
	switch (sc->bnx_func_addr) {
	case 0:
		sc->bnx_phy_ape_lock = BGE_APE_LOCK_PHY0;
		break;

	case 1:
		sc->bnx_phy_ape_lock = BGE_APE_LOCK_PHY1;
		break;

	case 2:
		sc->bnx_phy_ape_lock = BGE_APE_LOCK_PHY2;
		break;

	case 3:
		sc->bnx_phy_ape_lock = BGE_APE_LOCK_PHY3;
		break;

	default:
		device_printf(sc->bnx_dev,
		    "PHY lock not supported on this function\n");
		break;
	}
}

/*
 * Check for APE firmware, set flags, and print version info.
 */
static void
bnx_ape_read_fw_ver(struct bnx_softc *sc)
{
	const char *fwtype;
	uint32_t apedata, features;

	/* Check for a valid APE signature in shared memory. */
	apedata = APE_READ_4(sc, BGE_APE_SEG_SIG);
	if (apedata != BGE_APE_SEG_SIG_MAGIC) {
		device_printf(sc->bnx_dev, "no APE signature\n");
		sc->bnx_mfw_flags &= ~BNX_MFW_ON_APE;
		return;
	}

	/* Check if APE firmware is running. */
	apedata = APE_READ_4(sc, BGE_APE_FW_STATUS);
	if ((apedata & BGE_APE_FW_STATUS_READY) == 0) {
		device_printf(sc->bnx_dev, "APE signature found "
		    "but FW status not ready! 0x%08x\n", apedata);
		return;
	}

	sc->bnx_mfw_flags |= BNX_MFW_ON_APE;

	/* Fetch the APE firwmare type and version. */
	apedata = APE_READ_4(sc, BGE_APE_FW_VERSION);
	features = APE_READ_4(sc, BGE_APE_FW_FEATURES);
	if (features & BGE_APE_FW_FEATURE_NCSI) {
		sc->bnx_mfw_flags |= BNX_MFW_TYPE_NCSI;
		fwtype = "NCSI";
	} else if (features & BGE_APE_FW_FEATURE_DASH) {
		sc->bnx_mfw_flags |= BNX_MFW_TYPE_DASH;
		fwtype = "DASH";
	} else {
		fwtype = "UNKN";
	}

	/* Print the APE firmware version. */
	device_printf(sc->bnx_dev, "APE FW version: %s v%d.%d.%d.%d\n",
	    fwtype,
	    (apedata & BGE_APE_FW_VERSION_MAJMSK) >> BGE_APE_FW_VERSION_MAJSFT,
	    (apedata & BGE_APE_FW_VERSION_MINMSK) >> BGE_APE_FW_VERSION_MINSFT,
	    (apedata & BGE_APE_FW_VERSION_REVMSK) >> BGE_APE_FW_VERSION_REVSFT,
	    (apedata & BGE_APE_FW_VERSION_BLDMSK));
}

static int
bnx_ape_lock(struct bnx_softc *sc, int locknum)
{
	uint32_t bit, gnt, req, status;
	int i, off;

	if ((sc->bnx_mfw_flags & BNX_MFW_ON_APE) == 0)
		return 0;

	/* Lock request/grant registers have different bases. */
	req = BGE_APE_PER_LOCK_REQ;
	gnt = BGE_APE_PER_LOCK_GRANT;

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		/* Lock required when using GPIO. */
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_GRC:
		/* Lock required to reset the device. */
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_MEM:
		/* Lock required when accessing certain APE memory. */
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_PHY0:
	case BGE_APE_LOCK_PHY1:
	case BGE_APE_LOCK_PHY2:
	case BGE_APE_LOCK_PHY3:
		/* Lock required when accessing PHYs. */
		bit = BGE_APE_LOCK_REQ_DRIVER0;
		break;

	default:
		return EINVAL;
	}

	/* Request a lock. */
	APE_WRITE_4(sc, req + off, bit);

	/* Wait up to 1 second to acquire lock. */
	for (i = 0; i < 20000; i++) {
		status = APE_READ_4(sc, gnt + off);
		if (status == bit)
			break;
		DELAY(50);
	}

	/* Handle any errors. */
	if (status != bit) {
		if_printf(&sc->arpcom.ac_if, "APE lock %d request failed! "
		    "request = 0x%04x[0x%04x], status = 0x%04x[0x%04x]\n",
		    locknum, req + off, bit & 0xFFFF, gnt + off,
		    status & 0xFFFF);
		/* Revoke the lock request. */
		APE_WRITE_4(sc, gnt + off, bit);
		return EBUSY;
	}

	return 0;
}

static void
bnx_ape_unlock(struct bnx_softc *sc, int locknum)
{
	uint32_t bit, gnt;
	int off;

	if ((sc->bnx_mfw_flags & BNX_MFW_ON_APE) == 0)
		return;

	gnt = BGE_APE_PER_LOCK_GRANT;

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_GRC:
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_MEM:
		if (sc->bnx_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = 1 << sc->bnx_func_addr;
		break;

	case BGE_APE_LOCK_PHY0:
	case BGE_APE_LOCK_PHY1:
	case BGE_APE_LOCK_PHY2:
	case BGE_APE_LOCK_PHY3:
		bit = BGE_APE_LOCK_GRANT_DRIVER0;
		break;

	default:
		return;
	}

	APE_WRITE_4(sc, gnt + off, bit);
}

/*
 * Send an event to the APE firmware.
 */
static void
bnx_ape_send_event(struct bnx_softc *sc, uint32_t event)
{
	uint32_t apedata;
	int i;

	/* NCSI does not support APE events. */
	if ((sc->bnx_mfw_flags & BNX_MFW_ON_APE) == 0)
		return;

	/* Wait up to 1ms for APE to service previous event. */
	for (i = 10; i > 0; i--) {
		if (bnx_ape_lock(sc, BGE_APE_LOCK_MEM) != 0)
			break;
		apedata = APE_READ_4(sc, BGE_APE_EVENT_STATUS);
		if ((apedata & BGE_APE_EVENT_STATUS_EVENT_PENDING) == 0) {
			APE_WRITE_4(sc, BGE_APE_EVENT_STATUS, event |
			    BGE_APE_EVENT_STATUS_EVENT_PENDING);
			bnx_ape_unlock(sc, BGE_APE_LOCK_MEM);
			APE_WRITE_4(sc, BGE_APE_EVENT, BGE_APE_EVENT_1);
			break;
		}
		bnx_ape_unlock(sc, BGE_APE_LOCK_MEM);
		DELAY(100);
	}
	if (i == 0) {
		if_printf(&sc->arpcom.ac_if,
		    "APE event 0x%08x send timed out\n", event);
	}
}

static void
bnx_ape_driver_state_change(struct bnx_softc *sc, int kind)
{
	uint32_t apedata, event;

	if ((sc->bnx_mfw_flags & BNX_MFW_ON_APE) == 0)
		return;

	switch (kind) {
	case BNX_RESET_START:
		/* If this is the first load, clear the load counter. */
		apedata = APE_READ_4(sc, BGE_APE_HOST_SEG_SIG);
		if (apedata != BGE_APE_HOST_SEG_SIG_MAGIC) {
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, 0);
		} else {
			apedata = APE_READ_4(sc, BGE_APE_HOST_INIT_COUNT);
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, ++apedata);
		}
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_SIG,
		    BGE_APE_HOST_SEG_SIG_MAGIC);
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_LEN,
		    BGE_APE_HOST_SEG_LEN_MAGIC);

		/* Add some version info if bnx(4) supports it. */
		APE_WRITE_4(sc, BGE_APE_HOST_DRIVER_ID,
		    BGE_APE_HOST_DRIVER_ID_MAGIC(1, 0));
		APE_WRITE_4(sc, BGE_APE_HOST_BEHAVIOR,
		    BGE_APE_HOST_BEHAV_NO_PHYLOCK);
		APE_WRITE_4(sc, BGE_APE_HOST_HEARTBEAT_INT_MS,
		    BGE_APE_HOST_HEARTBEAT_INT_DISABLE);
		APE_WRITE_4(sc, BGE_APE_HOST_DRVR_STATE,
		    BGE_APE_HOST_DRVR_STATE_START);
		event = BGE_APE_EVENT_STATUS_STATE_START;
		break;

	case BNX_RESET_SHUTDOWN:
		APE_WRITE_4(sc, BGE_APE_HOST_DRVR_STATE,
		    BGE_APE_HOST_DRVR_STATE_UNLOAD);
		event = BGE_APE_EVENT_STATUS_STATE_UNLOAD;
		break;

	case BNX_RESET_SUSPEND:
		event = BGE_APE_EVENT_STATUS_STATE_SUSPEND;
		break;

	default:
		return;
	}

	bnx_ape_send_event(sc, event | BGE_APE_EVENT_STATUS_DRIVER_EVNT |
	    BGE_APE_EVENT_STATUS_STATE_CHNGE);
}
