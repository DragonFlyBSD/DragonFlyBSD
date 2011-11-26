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

/*
 * Broadcom BCM570x family gigabit ethernet driver for FreeBSD.
 * 
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Engineer, Wind River Systems
 */

/*
 * The Broadcom BCM5700 is based on technology originally developed by
 * Alteon Networks as part of the Tigon I and Tigon II gigabit ethernet
 * MAC chips. The BCM5700, sometimes refered to as the Tigon III, has
 * two on-board MIPS R4000 CPUs and can have as much as 16MB of external
 * SSRAM. The BCM5700 supports TCP, UDP and IP checksum offload, jumbo
 * frames, highly configurable RX filtering, and 16 RX and TX queues
 * (which, along with RX filter rules, can be used for QOS applications).
 * Other features, such as TCP segmentation, may be available as part
 * of value-added firmware updates. Unlike the Tigon I and Tigon II,
 * firmware images can be stored in hardware and need not be compiled
 * into the driver.
 *
 * The BCM5700 supports the PCI v2.2 and PCI-X v1.0 standards, and will
 * function in a 32-bit/64-bit 33/66Mhz bus, or a 64-bit/133Mhz bus.
 * 
 * The BCM5701 is a single-chip solution incorporating both the BCM5700
 * MAC and a BCM5401 10/100/1000 PHY. Unlike the BCM5700, the BCM5701
 * does not support external SSRAM.
 *
 * Broadcom also produces a variation of the BCM5700 under the "Altima"
 * brand name, which is functionally similar but lacks PCI-X support.
 *
 * Without external SSRAM, you can only have at most 4 TX rings,
 * and the use of the mini RX ring is disabled. This seems to imply
 * that these features are simply not available on the BCM5701. As a
 * result, this driver does not implement any support for the mini RX
 * ring.
 */

#include "opt_polling.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/interrupt.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>
#include <dev/netif/mii_layer/brgphyreg.h>

#include <bus/pci/pcidevs.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/bge/if_bgereg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define BGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)
#define BGE_MIN_FRAME		60

static const struct bge_type bge_devs[] = {
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C996,
		"3COM 3C996 Gigabit Ethernet" },

	{ PCI_VENDOR_ALTEON, PCI_PRODUCT_ALTEON_BCM5700,
		"Alteon BCM5700 Gigabit Ethernet" },
	{ PCI_VENDOR_ALTEON, PCI_PRODUCT_ALTEON_BCM5701,
		"Alteon BCM5701 Gigabit Ethernet" },

	{ PCI_VENDOR_ALTIMA, PCI_PRODUCT_ALTIMA_AC1000,
		"Altima AC1000 Gigabit Ethernet" },
	{ PCI_VENDOR_ALTIMA, PCI_PRODUCT_ALTIMA_AC1001,
		"Altima AC1002 Gigabit Ethernet" },
	{ PCI_VENDOR_ALTIMA, PCI_PRODUCT_ALTIMA_AC9100,
		"Altima AC9100 Gigabit Ethernet" },

	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_BCM5701,
		"Apple BCM5701 Gigabit Ethernet" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5700,
		"Broadcom BCM5700 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5701,
		"Broadcom BCM5701 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5702,
		"Broadcom BCM5702 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5702X,
		"Broadcom BCM5702X Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5702_ALT,
		"Broadcom BCM5702 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5703,
		"Broadcom BCM5703 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5703X,
		"Broadcom BCM5703X Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5703A3,
		"Broadcom BCM5703 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5704C,
		"Broadcom BCM5704C Dual Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5704S,
		"Broadcom BCM5704S Dual Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5704S_ALT,
		"Broadcom BCM5704S Dual Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5705,
		"Broadcom BCM5705 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5705F,
		"Broadcom BCM5705F Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5705K,
		"Broadcom BCM5705K Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5705M,
		"Broadcom BCM5705M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5705M_ALT,
		"Broadcom BCM5705M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5714,
		"Broadcom BCM5714C Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5714S,
		"Broadcom BCM5714S Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5715,
		"Broadcom BCM5715 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5715S,
		"Broadcom BCM5715S Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5720,
		"Broadcom BCM5720 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5721,
		"Broadcom BCM5721 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5722,
		"Broadcom BCM5722 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5723,
		"Broadcom BCM5723 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5750,
		"Broadcom BCM5750 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5750M,
		"Broadcom BCM5750M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5751,
		"Broadcom BCM5751 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5751F,
		"Broadcom BCM5751F Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5751M,
		"Broadcom BCM5751M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5752,
		"Broadcom BCM5752 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5752M,
		"Broadcom BCM5752M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5753,
		"Broadcom BCM5753 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5753F,
		"Broadcom BCM5753F Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5753M,
		"Broadcom BCM5753M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5754,
		"Broadcom BCM5754 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5754M,
		"Broadcom BCM5754M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5755,
		"Broadcom BCM5755 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5755M,
		"Broadcom BCM5755M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5756,
		"Broadcom BCM5756 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5761,
		"Broadcom BCM5761 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5761E,
		"Broadcom BCM5761E Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5761S,
		"Broadcom BCM5761S Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5761SE,
		"Broadcom BCM5761SE Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5764,
		"Broadcom BCM5764 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5780,
		"Broadcom BCM5780 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5780S,
		"Broadcom BCM5780S Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5781,
		"Broadcom BCM5781 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5782,
		"Broadcom BCM5782 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5784,
		"Broadcom BCM5784 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5785F,
		"Broadcom BCM5785F Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5785G,
		"Broadcom BCM5785G Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5786,
		"Broadcom BCM5786 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5787,
		"Broadcom BCM5787 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5787F,
		"Broadcom BCM5787F Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5787M,
		"Broadcom BCM5787M Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5788,
		"Broadcom BCM5788 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5789,
		"Broadcom BCM5789 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5901,
		"Broadcom BCM5901 Fast Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5901A2,
		"Broadcom BCM5901A2 Fast Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5903M,
		"Broadcom BCM5903M Fast Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5906,
		"Broadcom BCM5906 Fast Ethernet"},
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5906M,
		"Broadcom BCM5906M Fast Ethernet"},
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57760,
		"Broadcom BCM57760 Gigabit Ethernet"},
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57780,
		"Broadcom BCM57780 Gigabit Ethernet"},
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57788,
		"Broadcom BCM57788 Gigabit Ethernet"},
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57790,
		"Broadcom BCM57790 Gigabit Ethernet"},
	{ PCI_VENDOR_SCHNEIDERKOCH, PCI_PRODUCT_SCHNEIDERKOCH_SK_9DX1,
		"SysKonnect Gigabit Ethernet" },

	{ 0, 0, NULL }
};

#define BGE_IS_JUMBO_CAPABLE(sc)	((sc)->bge_flags & BGE_FLAG_JUMBO)
#define BGE_IS_5700_FAMILY(sc)		((sc)->bge_flags & BGE_FLAG_5700_FAMILY)
#define BGE_IS_5705_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_5705_PLUS)
#define BGE_IS_5714_FAMILY(sc)		((sc)->bge_flags & BGE_FLAG_5714_FAMILY)
#define BGE_IS_575X_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_575X_PLUS)
#define BGE_IS_5755_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_5755_PLUS)

typedef int	(*bge_eaddr_fcn_t)(struct bge_softc *, uint8_t[]);

static int	bge_probe(device_t);
static int	bge_attach(device_t);
static int	bge_detach(device_t);
static void	bge_txeof(struct bge_softc *);
static void	bge_rxeof(struct bge_softc *);

static void	bge_tick(void *);
static void	bge_stats_update(struct bge_softc *);
static void	bge_stats_update_regs(struct bge_softc *);
static int	bge_encap(struct bge_softc *, struct mbuf **, uint32_t *);

#ifdef DEVICE_POLLING
static void	bge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count);
#endif
static void	bge_intr(void *);
static void	bge_enable_intr(struct bge_softc *);
static void	bge_disable_intr(struct bge_softc *);
static void	bge_start(struct ifnet *);
static int	bge_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bge_init(void *);
static void	bge_stop(struct bge_softc *);
static void	bge_watchdog(struct ifnet *);
static void	bge_shutdown(device_t);
static int	bge_suspend(device_t);
static int	bge_resume(device_t);
static int	bge_ifmedia_upd(struct ifnet *);
static void	bge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static uint8_t	bge_nvram_getbyte(struct bge_softc *, int, uint8_t *);
static int	bge_read_nvram(struct bge_softc *, caddr_t, int, int);

static uint8_t	bge_eeprom_getbyte(struct bge_softc *, uint32_t, uint8_t *);
static int	bge_read_eeprom(struct bge_softc *, caddr_t, uint32_t, size_t);

static void	bge_setmulti(struct bge_softc *);
static void	bge_setpromisc(struct bge_softc *);

static int	bge_alloc_jumbo_mem(struct bge_softc *);
static void	bge_free_jumbo_mem(struct bge_softc *);
static struct bge_jslot
		*bge_jalloc(struct bge_softc *);
static void	bge_jfree(void *);
static void	bge_jref(void *);
static int	bge_newbuf_std(struct bge_softc *, int, int);
static int	bge_newbuf_jumbo(struct bge_softc *, int, int);
static void	bge_setup_rxdesc_std(struct bge_softc *, int);
static void	bge_setup_rxdesc_jumbo(struct bge_softc *, int);
static int	bge_init_rx_ring_std(struct bge_softc *);
static void	bge_free_rx_ring_std(struct bge_softc *);
static int	bge_init_rx_ring_jumbo(struct bge_softc *);
static void	bge_free_rx_ring_jumbo(struct bge_softc *);
static void	bge_free_tx_ring(struct bge_softc *);
static int	bge_init_tx_ring(struct bge_softc *);

static int	bge_chipinit(struct bge_softc *);
static int	bge_blockinit(struct bge_softc *);

static uint32_t	bge_readmem_ind(struct bge_softc *, uint32_t);
static void	bge_writemem_ind(struct bge_softc *, uint32_t, uint32_t);
#ifdef notdef
static uint32_t	bge_readreg_ind(struct bge_softc *, uint32_t);
#endif
static void	bge_writereg_ind(struct bge_softc *, uint32_t, uint32_t);
static void	bge_writemem_direct(struct bge_softc *, uint32_t, uint32_t);
static void	bge_writembx(struct bge_softc *, int, int);

static int	bge_miibus_readreg(device_t, int, int);
static int	bge_miibus_writereg(device_t, int, int, int);
static void	bge_miibus_statchg(device_t);
static void	bge_bcm5700_link_upd(struct bge_softc *, uint32_t);
static void	bge_tbi_link_upd(struct bge_softc *, uint32_t);
static void	bge_copper_link_upd(struct bge_softc *, uint32_t);

static void	bge_reset(struct bge_softc *);

static int	bge_dma_alloc(struct bge_softc *);
static void	bge_dma_free(struct bge_softc *);
static int	bge_dma_block_alloc(struct bge_softc *, bus_size_t,
				    bus_dma_tag_t *, bus_dmamap_t *,
				    void **, bus_addr_t *);
static void	bge_dma_block_free(bus_dma_tag_t, bus_dmamap_t, void *);

static int	bge_get_eaddr_mem(struct bge_softc *, uint8_t[]);
static int	bge_get_eaddr_nvram(struct bge_softc *, uint8_t[]);
static int	bge_get_eaddr_eeprom(struct bge_softc *, uint8_t[]);
static int	bge_get_eaddr(struct bge_softc *, uint8_t[]);

static void	bge_coal_change(struct bge_softc *);
static int	bge_sysctl_rx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_rx_max_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_tx_max_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *, uint32_t);

/*
 * Set following tunable to 1 for some IBM blade servers with the DNLK
 * switch module. Auto negotiation is broken for those configurations.
 */
static int	bge_fake_autoneg = 0;
TUNABLE_INT("hw.bge.fake_autoneg", &bge_fake_autoneg);

/* Interrupt moderation control variables. */
static int	bge_rx_coal_ticks = 100;	/* usec */
static int	bge_tx_coal_ticks = 1023;	/* usec */
static int	bge_rx_max_coal_bds = 80;
static int	bge_tx_max_coal_bds = 128;

TUNABLE_INT("hw.bge.rx_coal_ticks", &bge_rx_coal_ticks);
TUNABLE_INT("hw.bge.tx_coal_ticks", &bge_tx_coal_ticks);
TUNABLE_INT("hw.bge.rx_max_coal_bds", &bge_rx_max_coal_bds);
TUNABLE_INT("hw.bge.tx_max_coal_bds", &bge_tx_max_coal_bds);

#if !defined(KTR_IF_BGE)
#define KTR_IF_BGE	KTR_ALL
#endif
KTR_INFO_MASTER(if_bge);
KTR_INFO(KTR_IF_BGE, if_bge, intr, 0, "intr", 0);
KTR_INFO(KTR_IF_BGE, if_bge, rx_pkt, 1, "rx_pkt", 0);
KTR_INFO(KTR_IF_BGE, if_bge, tx_pkt, 2, "tx_pkt", 0);
#define logif(name)	KTR_LOG(if_bge_ ## name)

static device_method_t bge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bge_probe),
	DEVMETHOD(device_attach,	bge_attach),
	DEVMETHOD(device_detach,	bge_detach),
	DEVMETHOD(device_shutdown,	bge_shutdown),
	DEVMETHOD(device_suspend,	bge_suspend),
	DEVMETHOD(device_resume,	bge_resume),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	bge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	bge_miibus_statchg),

	{ 0, 0 }
};

static DEFINE_CLASS_0(bge, bge_driver, bge_methods, sizeof(struct bge_softc));
static devclass_t bge_devclass;

DECLARE_DUMMY_MODULE(if_bge);
DRIVER_MODULE(if_bge, pci, bge_driver, bge_devclass, NULL, NULL);
DRIVER_MODULE(miibus, bge, miibus_driver, miibus_devclass, NULL, NULL);

static uint32_t
bge_readmem_ind(struct bge_softc *sc, uint32_t off)
{
	device_t dev = sc->bge_dev;
	uint32_t val;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	val = pci_read_config(dev, BGE_PCI_MEMWIN_DATA, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
	return (val);
}

static void
bge_writemem_ind(struct bge_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_DATA, val, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
}

#ifdef notdef
static uint32_t
bge_readreg_ind(struct bge_softc *sc, uin32_t off)
{
	device_t dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	return(pci_read_config(dev, BGE_PCI_REG_DATA, 4));
}
#endif

static void
bge_writereg_ind(struct bge_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_REG_DATA, val, 4);
}

static void
bge_writemem_direct(struct bge_softc *sc, uint32_t off, uint32_t val)
{
	CSR_WRITE_4(sc, off, val);
}

static void
bge_writembx(struct bge_softc *sc, int off, int val)
{
	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		off += BGE_LPMBX_IRQ0_HI - BGE_MBX_IRQ0_HI;

	CSR_WRITE_4(sc, off, val);
}

static uint8_t
bge_nvram_getbyte(struct bge_softc *sc, int addr, uint8_t *dest)
{
	uint32_t access, byte = 0;
	int i;

	/* Lock. */
	CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_SET1);
	for (i = 0; i < 8000; i++) {
		if (CSR_READ_4(sc, BGE_NVRAM_SWARB) & BGE_NVRAMSWARB_GNT1)
			break;
		DELAY(20);
	}
	if (i == 8000)
		return (1);

	/* Enable access. */
	access = CSR_READ_4(sc, BGE_NVRAM_ACCESS);
	CSR_WRITE_4(sc, BGE_NVRAM_ACCESS, access | BGE_NVRAMACC_ENABLE);

	CSR_WRITE_4(sc, BGE_NVRAM_ADDR, addr & 0xfffffffc);
	CSR_WRITE_4(sc, BGE_NVRAM_CMD, BGE_NVRAM_READCMD);
	for (i = 0; i < BGE_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_NVRAM_CMD) & BGE_NVRAMCMD_DONE) {
			DELAY(10);
			break;
		}
	}

	if (i == BGE_TIMEOUT * 10) {
		if_printf(&sc->arpcom.ac_if, "nvram read timed out\n");
		return (1);
	}

	/* Get result. */
	byte = CSR_READ_4(sc, BGE_NVRAM_RDDATA);

	*dest = (bswap32(byte) >> ((addr % 4) * 8)) & 0xFF;

	/* Disable access. */
	CSR_WRITE_4(sc, BGE_NVRAM_ACCESS, access);

	/* Unlock. */
	CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_CLR1);
	CSR_READ_4(sc, BGE_NVRAM_SWARB);

	return (0);
}

/*
 * Read a sequence of bytes from NVRAM.
 */
static int
bge_read_nvram(struct bge_softc *sc, caddr_t dest, int off, int cnt)
{
	int err = 0, i;
	uint8_t byte = 0;

	if (sc->bge_asicrev != BGE_ASICREV_BCM5906)
		return (1);

	for (i = 0; i < cnt; i++) {
		err = bge_nvram_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return (err ? 1 : 0);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.' The
 * BCM570x supports both the traditional bitbang interface and an
 * auto access interface for reading the EEPROM. We use the auto
 * access method.
 */
static uint8_t
bge_eeprom_getbyte(struct bge_softc *sc, uint32_t addr, uint8_t *dest)
{
	int i;
	uint32_t byte = 0;

	/*
	 * Enable use of auto EEPROM access so we can avoid
	 * having to use the bitbang method.
	 */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_AUTO_EEPROM);

	/* Reset the EEPROM, load the clock period. */
	CSR_WRITE_4(sc, BGE_EE_ADDR,
	    BGE_EEADDR_RESET|BGE_EEHALFCLK(BGE_HALFCLK_384SCL));
	DELAY(20);

	/* Issue the read EEPROM command. */
	CSR_WRITE_4(sc, BGE_EE_ADDR, BGE_EE_READCMD | addr);

	/* Wait for completion */
	for(i = 0; i < BGE_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_EE_ADDR) & BGE_EEADDR_DONE)
			break;
	}

	if (i == BGE_TIMEOUT) {
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
bge_read_eeprom(struct bge_softc *sc, caddr_t dest, uint32_t off, size_t len)
{
	size_t i;
	int err;
	uint8_t byte;

	for (byte = 0, err = 0, i = 0; i < len; i++) {
		err = bge_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return(err ? 1 : 0);
}

static int
bge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct bge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val, autopoll;
	int i;

	/*
	 * Broadcom's own driver always assumes the internal
	 * PHY is at GMII address 1. On some chips, the PHY responds
	 * to accesses at all addresses, which could cause us to
	 * bogusly attach the PHY 32 times at probe type. Always
	 * restricting the lookup to address 1 is simpler than
	 * trying to figure out which chips revisions should be
	 * special-cased.
	 */
	if (phy != 1)
		return(0);

	/* Reading with autopolling on may trigger PCI errors */
	autopoll = CSR_READ_4(sc, BGE_MI_MODE);
	if (autopoll & BGE_MIMODE_AUTOPOLL) {
		BGE_CLRBIT(sc, BGE_MI_MODE, BGE_MIMODE_AUTOPOLL);
		DELAY(40);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_READ|BGE_MICOMM_BUSY|
	    BGE_MIPHY(phy)|BGE_MIREG(reg));

	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		val = CSR_READ_4(sc, BGE_MI_COMM);
		if (!(val & BGE_MICOMM_BUSY))
			break;
	}

	if (i == BGE_TIMEOUT) {
		if_printf(ifp, "PHY read timed out "
			  "(phy %d, reg %d, val 0x%08x)\n", phy, reg, val);
		val = 0;
		goto done;
	}

	DELAY(5);
	val = CSR_READ_4(sc, BGE_MI_COMM);

done:
	if (autopoll & BGE_MIMODE_AUTOPOLL) {
		BGE_SETBIT(sc, BGE_MI_MODE, BGE_MIMODE_AUTOPOLL);
		DELAY(40);
	}

	if (val & BGE_MICOMM_READFAIL)
		return(0);

	return(val & 0xFFFF);
}

static int
bge_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct bge_softc *sc = device_get_softc(dev);
	uint32_t autopoll;
	int i;

	/*
	 * See the related comment in bge_miibus_readreg()
	 */
	if (phy != 1)
		return(0);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    (reg == BRGPHY_MII_1000CTL || reg == BRGPHY_MII_AUXCTL))
	       return(0);

	/* Reading with autopolling on may trigger PCI errors */
	autopoll = CSR_READ_4(sc, BGE_MI_MODE);
	if (autopoll & BGE_MIMODE_AUTOPOLL) {
		BGE_CLRBIT(sc, BGE_MI_MODE, BGE_MIMODE_AUTOPOLL);
		DELAY(40);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_WRITE|BGE_MICOMM_BUSY|
	    BGE_MIPHY(phy)|BGE_MIREG(reg)|val);

	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, BGE_MI_COMM) & BGE_MICOMM_BUSY)) {
			DELAY(5);
			CSR_READ_4(sc, BGE_MI_COMM); /* dummy read */
			break;
		}
	}

	if (autopoll & BGE_MIMODE_AUTOPOLL) {
		BGE_SETBIT(sc, BGE_MI_MODE, BGE_MIMODE_AUTOPOLL);
		DELAY(40);
	}

	if (i == BGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "PHY write timed out "
			  "(phy %d, reg %d, val %d)\n", phy, reg, val);
		return(0);
	}

	return(0);
}

static void
bge_miibus_statchg(device_t dev)
{
	struct bge_softc *sc;
	struct mii_data *mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->bge_miibus);

	BGE_CLRBIT(sc, BGE_MAC_MODE, BGE_MACMODE_PORTMODE);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		BGE_SETBIT(sc, BGE_MAC_MODE, BGE_PORTMODE_GMII);
	} else {
		BGE_SETBIT(sc, BGE_MAC_MODE, BGE_PORTMODE_MII);
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		BGE_CLRBIT(sc, BGE_MAC_MODE, BGE_MACMODE_HALF_DUPLEX);
	} else {
		BGE_SETBIT(sc, BGE_MAC_MODE, BGE_MACMODE_HALF_DUPLEX);
	}
}

/*
 * Memory management for jumbo frames.
 */
static int
bge_alloc_jumbo_mem(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_jslot *entry;
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
	error = bge_dma_block_alloc(sc, BGE_JUMBO_RX_RING_SZ,
				    &sc->bge_cdata.bge_rx_jumbo_ring_tag,
				    &sc->bge_cdata.bge_rx_jumbo_ring_map,
				    (void *)&sc->bge_ldata.bge_rx_jumbo_ring,
				    &sc->bge_ldata.bge_rx_jumbo_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create jumbo RX ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for jumbo buffer block.
	 */
	error = bge_dma_block_alloc(sc, BGE_JMEM,
				    &sc->bge_cdata.bge_jumbo_tag,
				    &sc->bge_cdata.bge_jumbo_map,
				    (void **)&sc->bge_ldata.bge_jumbo_buf,
				    &paddr);
	if (error) {
		if_printf(ifp, "could not create jumbo buffer\n");
		return error;
	}

	SLIST_INIT(&sc->bge_jfree_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array. Note that we play an evil trick here by using
	 * the first few bytes in the buffer to hold the the address
	 * of the softc structure for this interface. This is because
	 * bge_jfree() needs it, but it is called by the mbuf management
	 * code which will not pass it to us explicitly.
	 */
	for (i = 0, ptr = sc->bge_ldata.bge_jumbo_buf; i < BGE_JSLOTS; i++) {
		entry = &sc->bge_cdata.bge_jslots[i];
		entry->bge_sc = sc;
		entry->bge_buf = ptr;
		entry->bge_paddr = paddr;
		entry->bge_inuse = 0;
		entry->bge_slot = i;
		SLIST_INSERT_HEAD(&sc->bge_jfree_listhead, entry, jslot_link);

		ptr += BGE_JLEN;
		paddr += BGE_JLEN;
	}
	return 0;
}

static void
bge_free_jumbo_mem(struct bge_softc *sc)
{
	/* Destroy jumbo RX ring. */
	bge_dma_block_free(sc->bge_cdata.bge_rx_jumbo_ring_tag,
			   sc->bge_cdata.bge_rx_jumbo_ring_map,
			   sc->bge_ldata.bge_rx_jumbo_ring);

	/* Destroy jumbo buffer block. */
	bge_dma_block_free(sc->bge_cdata.bge_jumbo_tag,
			   sc->bge_cdata.bge_jumbo_map,
			   sc->bge_ldata.bge_jumbo_buf);
}

/*
 * Allocate a jumbo buffer.
 */
static struct bge_jslot *
bge_jalloc(struct bge_softc *sc)
{
	struct bge_jslot *entry;

	lwkt_serialize_enter(&sc->bge_jslot_serializer);
	entry = SLIST_FIRST(&sc->bge_jfree_listhead);
	if (entry) {
		SLIST_REMOVE_HEAD(&sc->bge_jfree_listhead, jslot_link);
		entry->bge_inuse = 1;
	} else {
		if_printf(&sc->arpcom.ac_if, "no free jumbo buffers\n");
	}
	lwkt_serialize_exit(&sc->bge_jslot_serializer);
	return(entry);
}

/*
 * Adjust usage count on a jumbo buffer.
 */
static void
bge_jref(void *arg)
{
	struct bge_jslot *entry = (struct bge_jslot *)arg;
	struct bge_softc *sc = entry->bge_sc;

	if (sc == NULL)
		panic("bge_jref: can't find softc pointer!");

	if (&sc->bge_cdata.bge_jslots[entry->bge_slot] != entry) {
		panic("bge_jref: asked to reference buffer "
		    "that we don't manage!");
	} else if (entry->bge_inuse == 0) {
		panic("bge_jref: buffer already free!");
	} else {
		atomic_add_int(&entry->bge_inuse, 1);
	}
}

/*
 * Release a jumbo buffer.
 */
static void
bge_jfree(void *arg)
{
	struct bge_jslot *entry = (struct bge_jslot *)arg;
	struct bge_softc *sc = entry->bge_sc;

	if (sc == NULL)
		panic("bge_jfree: can't find softc pointer!");

	if (&sc->bge_cdata.bge_jslots[entry->bge_slot] != entry) {
		panic("bge_jfree: asked to free buffer that we don't manage!");
	} else if (entry->bge_inuse == 0) {
		panic("bge_jfree: buffer already free!");
	} else {
		/*
		 * Possible MP race to 0, use the serializer.  The atomic insn
		 * is still needed for races against bge_jref().
		 */
		lwkt_serialize_enter(&sc->bge_jslot_serializer);
		atomic_subtract_int(&entry->bge_inuse, 1);
		if (entry->bge_inuse == 0) {
			SLIST_INSERT_HEAD(&sc->bge_jfree_listhead, 
					  entry, jslot_link);
		}
		lwkt_serialize_exit(&sc->bge_jslot_serializer);
	}
}


/*
 * Intialize a standard receive ring descriptor.
 */
static int
bge_newbuf_std(struct bge_softc *sc, int i, int init)
{
	struct mbuf *m_new = NULL;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	int error, nsegs;

	m_new = m_getcl(init ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return ENOBUFS;
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	if ((sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) == 0)
		m_adj(m_new, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(sc->bge_cdata.bge_rx_mtag,
			sc->bge_cdata.bge_rx_tmpmap, m_new,
			&seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_new);
		return error;
	}

	if (!init) {
		bus_dmamap_sync(sc->bge_cdata.bge_rx_mtag,
				sc->bge_cdata.bge_rx_std_dmamap[i],
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->bge_cdata.bge_rx_mtag,
			sc->bge_cdata.bge_rx_std_dmamap[i]);
	}

	map = sc->bge_cdata.bge_rx_tmpmap;
	sc->bge_cdata.bge_rx_tmpmap = sc->bge_cdata.bge_rx_std_dmamap[i];
	sc->bge_cdata.bge_rx_std_dmamap[i] = map;

	sc->bge_cdata.bge_rx_std_chain[i].bge_mbuf = m_new;
	sc->bge_cdata.bge_rx_std_chain[i].bge_paddr = seg.ds_addr;

	bge_setup_rxdesc_std(sc, i);
	return 0;
}

static void
bge_setup_rxdesc_std(struct bge_softc *sc, int i)
{
	struct bge_rxchain *rc;
	struct bge_rx_bd *r;

	rc = &sc->bge_cdata.bge_rx_std_chain[i];
	r = &sc->bge_ldata.bge_rx_std_ring[i];

	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(rc->bge_paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(rc->bge_paddr);
	r->bge_len = rc->bge_mbuf->m_len;
	r->bge_idx = i;
	r->bge_flags = BGE_RXBDFLAG_END;
}

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int
bge_newbuf_jumbo(struct bge_softc *sc, int i, int init)
{
	struct mbuf *m_new = NULL;
	struct bge_jslot *buf;
	bus_addr_t paddr;

	/* Allocate the mbuf. */
	MGETHDR(m_new, init ? MB_WAIT : MB_DONTWAIT, MT_DATA);
	if (m_new == NULL)
		return ENOBUFS;

	/* Allocate the jumbo buffer */
	buf = bge_jalloc(sc);
	if (buf == NULL) {
		m_freem(m_new);
		return ENOBUFS;
	}

	/* Attach the buffer to the mbuf. */
	m_new->m_ext.ext_arg = buf;
	m_new->m_ext.ext_buf = buf->bge_buf;
	m_new->m_ext.ext_free = bge_jfree;
	m_new->m_ext.ext_ref = bge_jref;
	m_new->m_ext.ext_size = BGE_JUMBO_FRAMELEN;

	m_new->m_flags |= M_EXT;

	m_new->m_data = m_new->m_ext.ext_buf;
	m_new->m_len = m_new->m_pkthdr.len = m_new->m_ext.ext_size;

	paddr = buf->bge_paddr;
	if ((sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) == 0) {
		m_adj(m_new, ETHER_ALIGN);
		paddr += ETHER_ALIGN;
	}

	/* Save necessary information */
	sc->bge_cdata.bge_rx_jumbo_chain[i].bge_mbuf = m_new;
	sc->bge_cdata.bge_rx_jumbo_chain[i].bge_paddr = paddr;

	/* Set up the descriptor. */
	bge_setup_rxdesc_jumbo(sc, i);
	return 0;
}

static void
bge_setup_rxdesc_jumbo(struct bge_softc *sc, int i)
{
	struct bge_rx_bd *r;
	struct bge_rxchain *rc;

	r = &sc->bge_ldata.bge_rx_jumbo_ring[i];
	rc = &sc->bge_cdata.bge_rx_jumbo_chain[i];

	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(rc->bge_paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(rc->bge_paddr);
	r->bge_len = rc->bge_mbuf->m_len;
	r->bge_idx = i;
	r->bge_flags = BGE_RXBDFLAG_END|BGE_RXBDFLAG_JUMBO_RING;
}

static int
bge_init_rx_ring_std(struct bge_softc *sc)
{
	int i, error;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		error = bge_newbuf_std(sc, i, 1);
		if (error)
			return error;
	};

	sc->bge_std = BGE_STD_RX_RING_CNT - 1;
	bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, sc->bge_std);

	return(0);
}

static void
bge_free_rx_ring_std(struct bge_softc *sc)
{
	int i;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		struct bge_rxchain *rc = &sc->bge_cdata.bge_rx_std_chain[i];

		if (rc->bge_mbuf != NULL) {
			bus_dmamap_unload(sc->bge_cdata.bge_rx_mtag,
					  sc->bge_cdata.bge_rx_std_dmamap[i]);
			m_freem(rc->bge_mbuf);
			rc->bge_mbuf = NULL;
		}
		bzero(&sc->bge_ldata.bge_rx_std_ring[i],
		    sizeof(struct bge_rx_bd));
	}
}

static int
bge_init_rx_ring_jumbo(struct bge_softc *sc)
{
	struct bge_rcb *rcb;
	int i, error;

	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		error = bge_newbuf_jumbo(sc, i, 1);
		if (error)
			return error;
	};

	sc->bge_jumbo = BGE_JUMBO_RX_RING_CNT - 1;

	rcb = &sc->bge_ldata.bge_info.bge_jumbo_rx_rcb;
	rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(0, 0);
	CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);

	bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, sc->bge_jumbo);

	return(0);
}

static void
bge_free_rx_ring_jumbo(struct bge_softc *sc)
{
	int i;

	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		struct bge_rxchain *rc = &sc->bge_cdata.bge_rx_jumbo_chain[i];

		if (rc->bge_mbuf != NULL) {
			m_freem(rc->bge_mbuf);
			rc->bge_mbuf = NULL;
		}
		bzero(&sc->bge_ldata.bge_rx_jumbo_ring[i],
		    sizeof(struct bge_rx_bd));
	}
}

static void
bge_free_tx_ring(struct bge_softc *sc)
{
	int i;

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_tx_chain[i] != NULL) {
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag,
					  sc->bge_cdata.bge_tx_dmamap[i]);
			m_freem(sc->bge_cdata.bge_tx_chain[i]);
			sc->bge_cdata.bge_tx_chain[i] = NULL;
		}
		bzero(&sc->bge_ldata.bge_tx_ring[i],
		    sizeof(struct bge_tx_bd));
	}
}

static int
bge_init_tx_ring(struct bge_softc *sc)
{
	sc->bge_txcnt = 0;
	sc->bge_tx_saved_considx = 0;
	sc->bge_tx_prodidx = 0;

	/* Initialize transmit producer index for host-memory send ring. */
	bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, sc->bge_tx_prodidx);

	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, sc->bge_tx_prodidx);

	bge_writembx(sc, BGE_MBX_TX_NIC_PROD0_LO, 0);
	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_NIC_PROD0_LO, 0);

	return(0);
}

static void
bge_setmulti(struct bge_softc *sc)
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
bge_chipinit(struct bge_softc *sc)
{
	int i;
	uint32_t dma_rw_ctl;

	/* Set endian type before we access any non-PCI registers. */
	pci_write_config(sc->bge_dev, BGE_PCI_MISC_CTL, BGE_INIT, 4);

	/* Clear the MAC control register */
	CSR_WRITE_4(sc, BGE_MAC_MODE, 0);

	/*
	 * Clear the MAC statistics block in the NIC's
	 * internal memory.
	 */
	for (i = BGE_STATS_BLOCK;
	    i < BGE_STATS_BLOCK_END + 1; i += sizeof(uint32_t))
		BGE_MEMWIN_WRITE(sc, i, 0);

	for (i = BGE_STATUS_BLOCK;
	    i < BGE_STATUS_BLOCK_END + 1; i += sizeof(uint32_t))
		BGE_MEMWIN_WRITE(sc, i, 0);

	/* Set up the PCI DMA control register. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		/* PCI Express */
		dma_rw_ctl = BGE_PCI_READ_CMD|BGE_PCI_WRITE_CMD |
		    (0xf << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
		    (0x2 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
	} else if (sc->bge_flags & BGE_FLAG_PCIX) {
		/* PCI-X bus */
		if (BGE_IS_5714_FAMILY(sc)) {
			dma_rw_ctl = BGE_PCI_READ_CMD|BGE_PCI_WRITE_CMD;
			dma_rw_ctl &= ~BGE_PCIDMARWCTL_ONEDMA_ATONCE; /* XXX */
			/* XXX magic values, Broadcom-supplied Linux driver */
			if (sc->bge_asicrev == BGE_ASICREV_BCM5780) {
				dma_rw_ctl |= (1 << 20) | (1 << 18) | 
				    BGE_PCIDMARWCTL_ONEDMA_ATONCE;
			} else {
				dma_rw_ctl |= (1 << 20) | (1 << 18) | (1 << 15);
			}
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			/*
			 * The 5704 uses a different encoding of read/write
			 * watermarks.
			 */
			dma_rw_ctl = BGE_PCI_READ_CMD|BGE_PCI_WRITE_CMD |
			    (0x7 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (0x3 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
		} else {
			dma_rw_ctl = BGE_PCI_READ_CMD|BGE_PCI_WRITE_CMD |
			    (0x3 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (0x3 << BGE_PCIDMARWCTL_WR_WAT_SHIFT) |
			    (0x0F);
		}

		/*
		 * 5703 and 5704 need ONEDMA_AT_ONCE as a workaround
		 * for hardware bugs.
		 */
		if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			uint32_t tmp;

			tmp = CSR_READ_4(sc, BGE_PCI_CLKCTL) & 0x1f;
			if (tmp == 0x6 || tmp == 0x7)
				dma_rw_ctl |= BGE_PCIDMARWCTL_ONEDMA_ATONCE;
		}
	} else {
		/* Conventional PCI bus */
		dma_rw_ctl = BGE_PCI_READ_CMD|BGE_PCI_WRITE_CMD |
		    (0x7 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
		    (0x7 << BGE_PCIDMARWCTL_WR_WAT_SHIFT) |
		    (0x0F);
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5704 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5705)
		dma_rw_ctl &= ~BGE_PCIDMARWCTL_MINDMA;
	pci_write_config(sc->bge_dev, BGE_PCI_DMA_RW_CTL, dma_rw_ctl, 4);

	/*
	 * Set up general mode register.
	 */
	CSR_WRITE_4(sc, BGE_MODE_CTL, BGE_DMA_SWAP_OPTIONS|
	    BGE_MODECTL_MAC_ATTN_INTR|BGE_MODECTL_HOST_SEND_BDS|
	    BGE_MODECTL_TX_NO_PHDR_CSUM);

	/*
	 * Disable memory write invalidate.  Apparently it is not supported
	 * properly by these devices.
	 */
	PCI_CLRBIT(sc->bge_dev, BGE_PCI_CMD, PCIM_CMD_MWIEN, 4);

	/* Set the timer prescaler (always 66Mhz) */
	CSR_WRITE_4(sc, BGE_MISC_CFG, 65 << 1/*BGE_32BITTIME_66MHZ*/);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		DELAY(40);	/* XXX */

		/* Put PHY into ready state */
		BGE_CLRBIT(sc, BGE_MISC_CFG, BGE_MISCCFG_EPHY_IDDQ);
		CSR_READ_4(sc, BGE_MISC_CFG); /* Flush */
		DELAY(40);
	}

	return(0);
}

static int
bge_blockinit(struct bge_softc *sc)
{
	struct bge_rcb *rcb;
	bus_size_t vrcb;
	bge_hostaddr taddr;
	uint32_t val;
	int i;

	/*
	 * Initialize the memory window pointer register so that
	 * we can access the first 32K of internal NIC RAM. This will
	 * allow us to set up the TX send ring RCBs and the RX return
	 * ring RCBs, plus other things which live in NIC memory.
	 */
	CSR_WRITE_4(sc, BGE_PCI_MEMWIN_BASEADDR, 0);

	/* Note: the BCM5704 has a smaller mbuf space than other chips. */

	if (!BGE_IS_5705_PLUS(sc)) {
		/* Configure mbuf memory pool */
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_BASEADDR, BGE_BUFFPOOL_1);
		if (sc->bge_asicrev == BGE_ASICREV_BCM5704)
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_LEN, 0x10000);
		else
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_LEN, 0x18000);

		/* Configure DMA resource pool */
		CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_BASEADDR,
		    BGE_DMA_DESCRIPTORS);
		CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_LEN, 0x2000);
	}

	/* Configure mbuf pool watermarks */
	if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x50);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x20);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
	} else if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x04);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x10);
	} else {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x10);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
	}

	/* Configure DMA resource watermarks */
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_LOWAT, 5);
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_HIWAT, 10);

	/* Enable buffer manager */
	if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_BMAN_MODE,
		    BGE_BMANMODE_ENABLE|BGE_BMANMODE_LOMBUF_ATTN);

		/* Poll for buffer manager start indication */
		for (i = 0; i < BGE_TIMEOUT; i++) {
			if (CSR_READ_4(sc, BGE_BMAN_MODE) & BGE_BMANMODE_ENABLE)
				break;
			DELAY(10);
		}

		if (i == BGE_TIMEOUT) {
			if_printf(&sc->arpcom.ac_if,
				  "buffer manager failed to start\n");
			return(ENXIO);
		}
	}

	/* Enable flow-through queues */
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);

	/* Wait until queue initialization is complete */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		if (CSR_READ_4(sc, BGE_FTQ_RESET) == 0)
			break;
		DELAY(10);
	}

	if (i == BGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "flow-through queue init failed\n");
		return(ENXIO);
	}

	/* Initialize the standard RX ring control block */
	rcb = &sc->bge_ldata.bge_info.bge_std_rx_rcb;
	rcb->bge_hostaddr.bge_addr_lo =
	    BGE_ADDR_LO(sc->bge_ldata.bge_rx_std_ring_paddr);
	rcb->bge_hostaddr.bge_addr_hi =
	    BGE_ADDR_HI(sc->bge_ldata.bge_rx_std_ring_paddr);
	if (BGE_IS_5705_PLUS(sc))
		rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(512, 0);
	else
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(BGE_MAX_FRAMELEN, 0);
	rcb->bge_nicaddr = BGE_STD_RX_RINGS;
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);

	/*
	 * Initialize the jumbo RX ring control block
	 * We set the 'ring disabled' bit in the flags
	 * field until we're actually ready to start
	 * using this ring (i.e. once we set the MTU
	 * high enough to require it).
	 */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		rcb = &sc->bge_ldata.bge_info.bge_jumbo_rx_rcb;

		rcb->bge_hostaddr.bge_addr_lo =
		    BGE_ADDR_LO(sc->bge_ldata.bge_rx_jumbo_ring_paddr);
		rcb->bge_hostaddr.bge_addr_hi =
		    BGE_ADDR_HI(sc->bge_ldata.bge_rx_jumbo_ring_paddr);
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(BGE_MAX_FRAMELEN,
		    BGE_RCB_FLAG_RING_DISABLED);
		rcb->bge_nicaddr = BGE_JUMBO_RX_RINGS;
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_HI,
		    rcb->bge_hostaddr.bge_addr_hi);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_LO,
		    rcb->bge_hostaddr.bge_addr_lo);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_NICADDR, rcb->bge_nicaddr);

		/* Set up dummy disabled mini ring RCB */
		rcb = &sc->bge_ldata.bge_info.bge_mini_rx_rcb;
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED);
		CSR_WRITE_4(sc, BGE_RX_MINI_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
	}

	/*
	 * Set the BD ring replentish thresholds. The recommended
	 * values are 1/8th the number of descriptors allocated to
	 * each ring.
	 */
	if (BGE_IS_5705_PLUS(sc))
		val = 8;
	else
		val = BGE_STD_RX_RING_CNT / 8;
	CSR_WRITE_4(sc, BGE_RBDI_STD_REPL_THRESH, val);
	CSR_WRITE_4(sc, BGE_RBDI_JUMBO_REPL_THRESH, BGE_JUMBO_RX_RING_CNT/8);

	/*
	 * Disable all unused send rings by setting the 'ring disabled'
	 * bit in the flags field of all the TX send ring control blocks.
	 * These are located in NIC memory.
	 */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < BGE_TX_RINGS_EXTSSRAM_MAX; i++) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED));
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/* Configure TX RCB 0 (we use only the first ring) */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_tx_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr,
	    BGE_NIC_TXRING_ADDR(0, BGE_TX_RING_CNT));
	if (!BGE_IS_5705_PLUS(sc)) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(BGE_TX_RING_CNT, 0));
	}

	/* Disable all unused RX return rings */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	for (i = 0; i < BGE_RX_RINGS_MAX; i++) {
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, 0);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, 0);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(sc->bge_return_ring_cnt,
		    BGE_RCB_FLAG_RING_DISABLED));
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		bge_writembx(sc, BGE_MBX_RX_CONS0_LO +
		    (i * (sizeof(uint64_t))), 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/* Initialize RX ring indexes */
	bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, 0);
	bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, 0);
	bge_writembx(sc, BGE_MBX_RX_MINI_PROD_LO, 0);

	/*
	 * Set up RX return ring 0
	 * Note that the NIC address for RX return rings is 0x00000000.
	 * The return rings live entirely within the host, so the
	 * nicaddr field in the RCB isn't used.
	 */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_rx_return_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0x00000000);
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(sc->bge_return_ring_cnt, 0));

	/* Set random backoff seed for TX */
	CSR_WRITE_4(sc, BGE_TX_RANDOM_BACKOFF,
	    sc->arpcom.ac_enaddr[0] + sc->arpcom.ac_enaddr[1] +
	    sc->arpcom.ac_enaddr[2] + sc->arpcom.ac_enaddr[3] +
	    sc->arpcom.ac_enaddr[4] + sc->arpcom.ac_enaddr[5] +
	    BGE_TX_BACKOFF_SEED_MASK);

	/* Set inter-packet gap */
	CSR_WRITE_4(sc, BGE_TX_LENGTHS, 0x2620);

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
	for (i = 0; i < BGE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, BGE_HCC_MODE) & BGE_HCCMODE_ENABLE))
			break;
		DELAY(10);
	}

	if (i == BGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if,
			  "host coalescing engine failed to idle\n");
		return(ENXIO);
	}

	/* Set up host coalescing defaults */
	CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS, sc->bge_rx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS, sc->bge_tx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, sc->bge_rx_max_coal_bds);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, sc->bge_tx_max_coal_bds);
	if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS_INT, 0);
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS_INT, 0);
	}
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT, 1);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT, 1);

	/* Set up address of statistics block */
	if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_HCC_STATS_ADDR_HI,
		    BGE_ADDR_HI(sc->bge_ldata.bge_stats_paddr));
		CSR_WRITE_4(sc, BGE_HCC_STATS_ADDR_LO,
		    BGE_ADDR_LO(sc->bge_ldata.bge_stats_paddr));

		CSR_WRITE_4(sc, BGE_HCC_STATS_BASEADDR, BGE_STATS_BLOCK);
		CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_BASEADDR, BGE_STATUS_BLOCK);
		CSR_WRITE_4(sc, BGE_HCC_STATS_TICKS, sc->bge_stat_ticks);
	}

	/* Set up address of status block */
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_HI,
	    BGE_ADDR_HI(sc->bge_ldata.bge_status_block_paddr));
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_LO,
	    BGE_ADDR_LO(sc->bge_ldata.bge_status_block_paddr));
	sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx = 0;
	sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx = 0;

	/* Turn on host coalescing state machine */
	CSR_WRITE_4(sc, BGE_HCC_MODE, BGE_HCCMODE_ENABLE);

	/* Turn on RX BD completion state machine and enable attentions */
	CSR_WRITE_4(sc, BGE_RBDC_MODE,
	    BGE_RBDCMODE_ENABLE|BGE_RBDCMODE_ATTN);

	/* Turn on RX list placement state machine */
	CSR_WRITE_4(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);

	/* Turn on RX list selector state machine. */
	if (!BGE_IS_5705_PLUS(sc))
		CSR_WRITE_4(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);

	/* Turn on DMA, clear stats */
	CSR_WRITE_4(sc, BGE_MAC_MODE, BGE_MACMODE_TXDMA_ENB|
	    BGE_MACMODE_RXDMA_ENB|BGE_MACMODE_RX_STATS_CLEAR|
	    BGE_MACMODE_TX_STATS_CLEAR|BGE_MACMODE_RX_STATS_ENB|
	    BGE_MACMODE_TX_STATS_ENB|BGE_MACMODE_FRMHDR_DMA_ENB|
	    ((sc->bge_flags & BGE_FLAG_TBI) ?
	     BGE_PORTMODE_TBI : BGE_PORTMODE_MII));

	/* Set misc. local control, enable interrupts on attentions */
	CSR_WRITE_4(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

#ifdef notdef
	/* Assert GPIO pins for PHY reset */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUT0|
	    BGE_MLC_MISCIO_OUT1|BGE_MLC_MISCIO_OUT2);
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUTEN0|
	    BGE_MLC_MISCIO_OUTEN1|BGE_MLC_MISCIO_OUTEN2);
#endif

	/* Turn on DMA completion state machine */
	if (!BGE_IS_5705_PLUS(sc))
		CSR_WRITE_4(sc, BGE_DMAC_MODE, BGE_DMACMODE_ENABLE);

	/* Turn on write DMA state machine */
	val = BGE_WDMAMODE_ENABLE|BGE_WDMAMODE_ALL_ATTNS;
	if (BGE_IS_5755_PLUS(sc))
		val |= (1 << 29);	/* Enable host coalescing bug fix. */
	CSR_WRITE_4(sc, BGE_WDMA_MODE, val);
	DELAY(40);

	/* Turn on read DMA state machine */
	val = BGE_RDMAMODE_ENABLE | BGE_RDMAMODE_ALL_ATTNS;
        if (sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
            sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
            sc->bge_asicrev == BGE_ASICREV_BCM57780)
		val |= BGE_RDMAMODE_BD_SBD_CRPT_ATTN |
                  BGE_RDMAMODE_MBUF_RBD_CRPT_ATTN |
                  BGE_RDMAMODE_MBUF_SBD_CRPT_ATTN;
	if (sc->bge_flags & BGE_FLAG_PCIE)
		val |= BGE_RDMAMODE_FIFO_LONG_BURST;
	CSR_WRITE_4(sc, BGE_RDMA_MODE, val);
	DELAY(40);

	/* Turn on RX data completion state machine */
	CSR_WRITE_4(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);

	/* Turn on RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);

	/* Turn on RX data and RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RDBDI_MODE, BGE_RDBDIMODE_ENABLE);

	/* Turn on Mbuf cluster free state machine */
	if (!BGE_IS_5705_PLUS(sc))
		CSR_WRITE_4(sc, BGE_MBCF_MODE, BGE_MBCFMODE_ENABLE);

	/* Turn on send BD completion state machine */
	CSR_WRITE_4(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/* Turn on send data completion state machine */
	val = BGE_SDCMODE_ENABLE;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		val |= BGE_SDCMODE_CDELAY; 
	CSR_WRITE_4(sc, BGE_SDC_MODE, val);

	/* Turn on send data initiator state machine */
	CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);

	/* Turn on send BD initiator state machine */
	CSR_WRITE_4(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);

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

	/* Enable PHY auto polling (for MII/GMII only) */
	if (sc->bge_flags & BGE_FLAG_TBI) {
		CSR_WRITE_4(sc, BGE_MI_STS, BGE_MISTS_LINK);
 	} else {
		BGE_SETBIT(sc, BGE_MI_MODE, BGE_MIMODE_AUTOPOLL|10<<16);
		if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
		    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) {
			CSR_WRITE_4(sc, BGE_MAC_EVT_ENB,
			    BGE_EVTENB_MI_INTERRUPT);
		}
	}

	/*
	 * Clear any pending link state attention.
	 * Otherwise some link state change events may be lost until attention
	 * is cleared by bge_intr() -> bge_softc.bge_link_upd() sequence.
	 * It's not necessary on newer BCM chips - perhaps enabling link
	 * state change attentions implies clearing pending attention.
	 */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED|
	    BGE_MACSTAT_CFG_CHANGED|BGE_MACSTAT_MI_COMPLETE|
	    BGE_MACSTAT_LINK_CHANGED);

	/* Enable link state change attentions. */
	BGE_SETBIT(sc, BGE_MAC_EVT_ENB, BGE_EVTENB_LINK_CHANGED);

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
bge_probe(device_t dev)
{
	const struct bge_type *t;
	uint16_t product, vendor;

	product = pci_get_device(dev);
	vendor = pci_get_vendor(dev);

	for (t = bge_devs; t->bge_name != NULL; t++) {
		if (vendor == t->bge_vid && product == t->bge_did)
			break;
	}
	if (t->bge_name == NULL)
		return(ENXIO);

	device_set_desc(dev, t->bge_name);
	if (pci_get_subvendor(dev) == PCI_VENDOR_DELL) {
		struct bge_softc *sc = device_get_softc(dev);
		sc->bge_flags |= BGE_FLAG_NO_3LED;
	}
	return(0);
}

static int
bge_attach(device_t dev)
{
	struct ifnet *ifp;
	struct bge_softc *sc;
	uint32_t hwcfg = 0;
	int error = 0, rid;
	uint8_t ether_addr[ETHER_ADDR_LEN];

	sc = device_get_softc(dev);
	sc->bge_dev = dev;
	callout_init(&sc->bge_stat_timer);
	lwkt_serialize_init(&sc->bge_jslot_serializer);

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
	sc->bge_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);

	if (sc->bge_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		return ENXIO;
	}

	sc->bge_btag = rman_get_bustag(sc->bge_res);
	sc->bge_bhandle = rman_get_bushandle(sc->bge_res);

	/* Save various chip information */
	sc->bge_chipid =
	    pci_read_config(dev, BGE_PCI_MISC_CTL, 4) >>
	    BGE_PCIMISCCTL_ASICREV_SHIFT;
        if (BGE_ASICREV(sc->bge_chipid) == BGE_ASICREV_USE_PRODID_REG)
		sc->bge_chipid = pci_read_config(dev, BGE_PCI_PRODID_ASICREV, 4);
	sc->bge_asicrev = BGE_ASICREV(sc->bge_chipid);
	sc->bge_chiprev = BGE_CHIPREV(sc->bge_chipid);

	/* Save chipset family. */
	switch (sc->bge_asicrev) {
	case BGE_ASICREV_BCM5755:
	case BGE_ASICREV_BCM5761:
	case BGE_ASICREV_BCM5784:
	case BGE_ASICREV_BCM5785:
	case BGE_ASICREV_BCM5787:
	case BGE_ASICREV_BCM57780:
	    sc->bge_flags |= BGE_FLAG_5755_PLUS | BGE_FLAG_575X_PLUS |
		BGE_FLAG_5705_PLUS;
	    break;

	case BGE_ASICREV_BCM5700:
	case BGE_ASICREV_BCM5701:
	case BGE_ASICREV_BCM5703:
	case BGE_ASICREV_BCM5704:
		sc->bge_flags |= BGE_FLAG_5700_FAMILY | BGE_FLAG_JUMBO;
		break;

	case BGE_ASICREV_BCM5714_A0:
	case BGE_ASICREV_BCM5780:
	case BGE_ASICREV_BCM5714:
		sc->bge_flags |= BGE_FLAG_5714_FAMILY;
		/* Fall through */

	case BGE_ASICREV_BCM5750:
	case BGE_ASICREV_BCM5752:
	case BGE_ASICREV_BCM5906:
		sc->bge_flags |= BGE_FLAG_575X_PLUS;
		/* Fall through */

	case BGE_ASICREV_BCM5705:
		sc->bge_flags |= BGE_FLAG_5705_PLUS;
		break;
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		sc->bge_flags |= BGE_FLAG_NO_EEPROM;

	/*
	 * Set various quirk flags.
	 */

	sc->bge_flags |= BGE_FLAG_ETH_WIRESPEED;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	     (sc->bge_chipid != BGE_CHIPID_BCM5705_A0 &&
	      sc->bge_chipid != BGE_CHIPID_BCM5705_A1)) ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5906)
		sc->bge_flags &= ~BGE_FLAG_ETH_WIRESPEED;

	if (sc->bge_chipid == BGE_CHIPID_BCM5701_A0 ||
	    sc->bge_chipid == BGE_CHIPID_BCM5701_B0)
		sc->bge_flags |= BGE_FLAG_CRC_BUG;

	if (sc->bge_chiprev == BGE_CHIPREV_5703_AX ||
	    sc->bge_chiprev == BGE_CHIPREV_5704_AX)
		sc->bge_flags |= BGE_FLAG_ADC_BUG;

	if (sc->bge_chipid == BGE_CHIPID_BCM5704_A0)
		sc->bge_flags |= BGE_FLAG_5704_A0_BUG;

	if (BGE_IS_5705_PLUS(sc) &&
		!(sc->bge_flags & BGE_FLAG_ADJUST_TRIM)) {
		if (sc->bge_asicrev == BGE_ASICREV_BCM5755 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5787) {
		    	if (sc->bge_chipid != BGE_CHIPID_BCM5722_A0)
			    sc->bge_flags |= BGE_FLAG_JITTER_BUG;
		} else if (sc->bge_asicrev != BGE_ASICREV_BCM5906) {
			sc->bge_flags |= BGE_FLAG_BER_BUG;
		}
	}

	/* Allocate interrupt */
	rid = 0;

	sc->bge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->bge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

  	/*
	 * Check if this is a PCI-X or PCI Express device.
  	 */
	if (BGE_IS_5705_PLUS(sc)) {
		if (pci_is_pcie(dev)) {
			sc->bge_flags |= BGE_FLAG_PCIE;
			pcie_set_max_readrq(dev, PCIEM_DEVCTL_MAX_READRQ_4096);
		}
	} else {
		/*
		 * Check if the device is in PCI-X Mode.
		 * (This bit is not valid on PCI Express controllers.)
		 */
		if ((pci_read_config(sc->bge_dev, BGE_PCI_PCISTATE, 4) &
		    BGE_PCISTATE_PCI_BUSMODE) == 0)
			sc->bge_flags |= BGE_FLAG_PCIX;
 	}

	device_printf(dev, "CHIP ID 0x%08x; "
		      "ASIC REV 0x%02x; CHIP REV 0x%02x; %s\n",
		      sc->bge_chipid, sc->bge_asicrev, sc->bge_chiprev,
		      (sc->bge_flags & BGE_FLAG_PCIX) ? "PCI-X"
		      : ((sc->bge_flags & BGE_FLAG_PCIE) ?
			"PCI-E" : "PCI"));

	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* Try to reset the chip. */
	bge_reset(sc);

	if (bge_chipinit(sc)) {
		device_printf(dev, "chip initialization failed\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Get station address
	 */
	error = bge_get_eaddr(sc, ether_addr);
	if (error) {
		device_printf(dev, "failed to read station address\n");
		goto fail;
	}

	/* 5705/5750 limits RX return ring to 512 entries. */
	if (BGE_IS_5705_PLUS(sc))
		sc->bge_return_ring_cnt = BGE_RETURN_RING_CNT_5705;
	else
		sc->bge_return_ring_cnt = BGE_RETURN_RING_CNT;

	error = bge_dma_alloc(sc);
	if (error)
		goto fail;

	/* Set default tuneable values. */
	sc->bge_stat_ticks = BGE_TICKS_PER_SEC;
	sc->bge_rx_coal_ticks = bge_rx_coal_ticks;
	sc->bge_tx_coal_ticks = bge_tx_coal_ticks;
	sc->bge_rx_max_coal_bds = bge_rx_max_coal_bds;
	sc->bge_tx_max_coal_bds = bge_tx_max_coal_bds;

	/* Set up ifnet structure */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bge_ioctl;
	ifp->if_start = bge_start;
#ifdef DEVICE_POLLING
	ifp->if_poll = bge_poll;
#endif
	ifp->if_watchdog = bge_watchdog;
	ifp->if_init = bge_init;
	ifp->if_mtu = ETHERMTU;
	ifp->if_capabilities = IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	ifq_set_maxlen(&ifp->if_snd, BGE_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * 5700 B0 chips do not support checksumming correctly due
	 * to hardware bugs.
	 */
	if (sc->bge_chipid != BGE_CHIPID_BCM5700_B0) {
		ifp->if_capabilities |= IFCAP_HWCSUM;
		ifp->if_hwassist = BGE_CSUM_FEATURES;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Figure out what sort of media we have by checking the
	 * hardware config word in the first 32k of NIC internal memory,
	 * or fall back to examining the EEPROM if necessary.
	 * Note: on some BCM5700 cards, this value appears to be unset.
	 * If that's the case, we have to rely on identifying the NIC
	 * by its PCI subsystem ID, as we do below for the SysKonnect
	 * SK-9D41.
	 */
	if (bge_readmem_ind(sc, BGE_SOFTWARE_GENCOMM_SIG) == BGE_MAGIC_NUMBER)
		hwcfg = bge_readmem_ind(sc, BGE_SOFTWARE_GENCOMM_NICCFG);
	else {
		if (bge_read_eeprom(sc, (caddr_t)&hwcfg, BGE_EE_HWCFG_OFFSET,
				    sizeof(hwcfg))) {
			device_printf(dev, "failed to read EEPROM\n");
			error = ENXIO;
			goto fail;
		}
		hwcfg = ntohl(hwcfg);
	}

	if ((hwcfg & BGE_HWCFG_MEDIA) == BGE_MEDIA_FIBER)
		sc->bge_flags |= BGE_FLAG_TBI;

	/* The SysKonnect SK-9D41 is a 1000baseSX card. */
	if (pci_get_subvendor(dev) == PCI_PRODUCT_SCHNEIDERKOCH_SK_9D41)
		sc->bge_flags |= BGE_FLAG_TBI;

	if (sc->bge_flags & BGE_FLAG_TBI) {
		ifmedia_init(&sc->bge_ifmedia, IFM_IMASK,
		    bge_ifmedia_upd, bge_ifmedia_sts);
		ifmedia_add(&sc->bge_ifmedia, IFM_ETHER|IFM_1000_SX, 0, NULL);
		ifmedia_add(&sc->bge_ifmedia,
		    IFM_ETHER|IFM_1000_SX|IFM_FDX, 0, NULL);
		ifmedia_add(&sc->bge_ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
		ifmedia_set(&sc->bge_ifmedia, IFM_ETHER|IFM_AUTO);
		sc->bge_ifmedia.ifm_media = sc->bge_ifmedia.ifm_cur->ifm_media;
	} else {
		/*
		 * Do transceiver setup.
		 */
		if (mii_phy_probe(dev, &sc->bge_miibus,
		    bge_ifmedia_upd, bge_ifmedia_sts)) {
			device_printf(dev, "MII without any PHY!\n");
			error = ENXIO;
			goto fail;
		}
	}

	/*
	 * When using the BCM5701 in PCI-X mode, data corruption has
	 * been observed in the first few bytes of some received packets.
	 * Aligning the packet buffer in memory eliminates the corruption.
	 * Unfortunately, this misaligns the packet payloads.  On platforms
	 * which do not support unaligned accesses, we will realign the
	 * payloads by copying the received packets.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5701 &&
	    (sc->bge_flags & BGE_FLAG_PCIX))
		sc->bge_flags |= BGE_FLAG_RX_ALIGNBUG;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) {
		sc->bge_link_upd = bge_bcm5700_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_MI_INTERRUPT;
	} else if (sc->bge_flags & BGE_FLAG_TBI) {
		sc->bge_link_upd = bge_tbi_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_LINK_CHANGED;
	} else {
		sc->bge_link_upd = bge_copper_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_LINK_CHANGED;
	}

	/*
	 * Create sysctl nodes.
	 */
	sysctl_ctx_init(&sc->bge_sysctl_ctx);
	sc->bge_sysctl_tree = SYSCTL_ADD_NODE(&sc->bge_sysctl_ctx,
					      SYSCTL_STATIC_CHILDREN(_hw),
					      OID_AUTO,
					      device_get_nameunit(dev),
					      CTLFLAG_RD, 0, "");
	if (sc->bge_sysctl_tree == NULL) {
		device_printf(dev, "can't add sysctl node\n");
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_PROC(&sc->bge_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bge_sysctl_tree),
			OID_AUTO, "rx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_rx_coal_ticks, "I",
			"Receive coalescing ticks (usec).");
	SYSCTL_ADD_PROC(&sc->bge_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bge_sysctl_tree),
			OID_AUTO, "tx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_tx_coal_ticks, "I",
			"Transmit coalescing ticks (usec).");
	SYSCTL_ADD_PROC(&sc->bge_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bge_sysctl_tree),
			OID_AUTO, "rx_max_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_rx_max_coal_bds, "I",
			"Receive max coalesced BD count.");
	SYSCTL_ADD_PROC(&sc->bge_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bge_sysctl_tree),
			OID_AUTO, "tx_max_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_tx_max_coal_bds, "I",
			"Transmit max coalesced BD count.");

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ether_addr, NULL);

	error = bus_setup_intr(dev, sc->bge_irq, INTR_MPSAFE,
			       bge_intr, sc, &sc->bge_intrhand, 
			       ifp->if_serializer);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

	ifp->if_cpuid = ithread_cpuid(rman_get_start(sc->bge_irq));
	KKASSERT(ifp->if_cpuid >= 0 && ifp->if_cpuid < ncpus);

	return(0);
fail:
	bge_detach(dev);
	return(error);
}

static int
bge_detach(device_t dev)
{
	struct bge_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		bge_stop(sc);
		bge_reset(sc);
		bus_teardown_intr(dev, sc->bge_irq, sc->bge_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->bge_flags & BGE_FLAG_TBI)
		ifmedia_removeall(&sc->bge_ifmedia);
	if (sc->bge_miibus)
		device_delete_child(dev, sc->bge_miibus);
	bus_generic_detach(dev);

        if (sc->bge_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->bge_irq);

        if (sc->bge_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    BGE_PCI_BAR0, sc->bge_res);

	if (sc->bge_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->bge_sysctl_ctx);

	bge_dma_free(sc);

	return 0;
}

static void
bge_reset(struct bge_softc *sc)
{
	device_t dev;
	uint32_t cachesize, command, pcistate, reset;
	void (*write_op)(struct bge_softc *, uint32_t, uint32_t);
	int i, val = 0;

	dev = sc->bge_dev;

	if (BGE_IS_575X_PLUS(sc) && !BGE_IS_5714_FAMILY(sc) &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5906) {
		if (sc->bge_flags & BGE_FLAG_PCIE)
			write_op = bge_writemem_direct;
		else
			write_op = bge_writemem_ind;
	} else {
		write_op = bge_writereg_ind;
	}

	/* Save some important PCI state. */
	cachesize = pci_read_config(dev, BGE_PCI_CACHESZ, 4);
	command = pci_read_config(dev, BGE_PCI_CMD, 4);
	pcistate = pci_read_config(dev, BGE_PCI_PCISTATE, 4);

	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW, 4);

	/* Disable fastboot on controllers that support it. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5752 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5755 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5787) {
		if (bootverbose)
			if_printf(&sc->arpcom.ac_if, "Disabling fastboot\n");
		CSR_WRITE_4(sc, BGE_FASTBOOT_PC, 0x0);
	}

	/*
	 * Write the magic number to SRAM at offset 0xB50.
	 * When firmware finishes its initialization it will
	 * write ~BGE_MAGIC_NUMBER to the same location.
	 */
	bge_writemem_ind(sc, BGE_SOFTWARE_GENCOMM, BGE_MAGIC_NUMBER);

	reset = BGE_MISCCFG_RESET_CORE_CLOCKS|(65<<1);

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		if (CSR_READ_4(sc, 0x7e2c) == 0x60)	/* PCIE 1.0 */
			CSR_WRITE_4(sc, 0x7e2c, 0x20);
		if (sc->bge_chipid != BGE_CHIPID_BCM5750_A0) {
			/* Prevent PCIE link training during global reset */
			CSR_WRITE_4(sc, BGE_MISC_CFG, (1<<29));
			reset |= (1<<29);
		}
	}

	/* 
	 * Set GPHY Power Down Override to leave GPHY
	 * powered up in D0 uninitialized.
	 */
	if (BGE_IS_5705_PLUS(sc))
		reset |= 0x04000000;

	/* Issue global reset */
	write_op(sc, BGE_MISC_CFG, reset);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		uint32_t status, ctrl;

		status = CSR_READ_4(sc, BGE_VCPU_STATUS);
		CSR_WRITE_4(sc, BGE_VCPU_STATUS,
		    status | BGE_VCPU_STATUS_DRV_RESET);
		ctrl = CSR_READ_4(sc, BGE_VCPU_EXT_CTRL);
		CSR_WRITE_4(sc, BGE_VCPU_EXT_CTRL,
		    ctrl & ~BGE_VCPU_EXT_CTRL_HALT_CPU);
	}

	DELAY(1000);

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		if (sc->bge_chipid == BGE_CHIPID_BCM5750_A0) {
			uint32_t v;

			DELAY(500000); /* wait for link training to complete */
			v = pci_read_config(dev, 0xc4, 4);
			pci_write_config(dev, 0xc4, v | (1<<15), 4);
		}
		/*
		 * Set PCIE max payload size to 128 bytes and
		 * clear error status.
		 */
		pci_write_config(dev, 0xd8, 0xf5000, 4);
	}

	/* Reset some of the PCI state that got zapped by reset */
	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW, 4);
	pci_write_config(dev, BGE_PCI_CACHESZ, cachesize, 4);
	pci_write_config(dev, BGE_PCI_CMD, command, 4);
	write_op(sc, BGE_MISC_CFG, (65 << 1));

	/* Enable memory arbiter. */
	if (BGE_IS_5714_FAMILY(sc)) {
		uint32_t val;

		val = CSR_READ_4(sc, BGE_MARB_MODE);
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE | val);
	} else {
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		for (i = 0; i < BGE_TIMEOUT; i++) {
			val = CSR_READ_4(sc, BGE_VCPU_STATUS);
			if (val & BGE_VCPU_STATUS_INIT_DONE)
				break;
			DELAY(100);
		}
		if (i == BGE_TIMEOUT) {
			if_printf(&sc->arpcom.ac_if, "reset timed out\n");
			return;
		}
	} else {
		/*
		 * Poll until we see the 1's complement of the magic number.
		 * This indicates that the firmware initialization
		 * is complete.
		 */
		for (i = 0; i < BGE_FIRMWARE_TIMEOUT; i++) {
			val = bge_readmem_ind(sc, BGE_SOFTWARE_GENCOMM);
			if (val == ~BGE_MAGIC_NUMBER)
				break;
			DELAY(10);
		}
		if (i == BGE_FIRMWARE_TIMEOUT) {
			if_printf(&sc->arpcom.ac_if, "firmware handshake "
				  "timed out, found 0x%08x\n", val);
			return;
		}
	}

	/*
	 * XXX Wait for the value of the PCISTATE register to
	 * return to its original pre-reset state. This is a
	 * fairly good indicator of reset completion. If we don't
	 * wait for the reset to fully complete, trying to read
	 * from the device's non-PCI registers may yield garbage
	 * results.
	 */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		if (pci_read_config(dev, BGE_PCI_PCISTATE, 4) == pcistate)
			break;
		DELAY(10);
	}

	if (sc->bge_flags & BGE_FLAG_PCIE) {
		reset = bge_readmem_ind(sc, 0x7c00);
		bge_writemem_ind(sc, 0x7c00, reset | (1 << 25));
	}

	/* Fix up byte swapping */
	CSR_WRITE_4(sc, BGE_MODE_CTL, BGE_DMA_SWAP_OPTIONS |
	    BGE_MODECTL_BYTESWAP_DATA);

	CSR_WRITE_4(sc, BGE_MAC_MODE, 0);

	/*
	 * The 5704 in TBI mode apparently needs some special
	 * adjustment to insure the SERDES drive level is set
	 * to 1.2V.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5704 &&
	    (sc->bge_flags & BGE_FLAG_TBI)) {
		uint32_t serdescfg;

		serdescfg = CSR_READ_4(sc, BGE_SERDES_CFG);
		serdescfg = (serdescfg & ~0xFFF) | 0x880;
		CSR_WRITE_4(sc, BGE_SERDES_CFG, serdescfg);
	}

	/* XXX: Broadcom Linux driver. */
	if ((sc->bge_flags & BGE_FLAG_PCIE) &&
	    sc->bge_chipid != BGE_CHIPID_BCM5750_A0) {
		uint32_t v;

		v = CSR_READ_4(sc, 0x7c00);
		CSR_WRITE_4(sc, 0x7c00, v | (1<<25));
	}

	DELAY(10000);
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
bge_rxeof(struct bge_softc *sc)
{
	struct ifnet *ifp;
	int stdcnt = 0, jumbocnt = 0;
	struct mbuf_chain chain[MAXCPU];

	if (sc->bge_rx_saved_considx ==
	    sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx)
		return;

	ether_input_chain_init(chain);

	ifp = &sc->arpcom.ac_if;

	while (sc->bge_rx_saved_considx !=
	       sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx) {
		struct bge_rx_bd	*cur_rx;
		uint32_t		rxidx;
		struct mbuf		*m = NULL;
		uint16_t		vlan_tag = 0;
		int			have_tag = 0;

		cur_rx =
	    &sc->bge_ldata.bge_rx_return_ring[sc->bge_rx_saved_considx];

		rxidx = cur_rx->bge_idx;
		BGE_INC(sc->bge_rx_saved_considx, sc->bge_return_ring_cnt);
		logif(rx_pkt);

		if (cur_rx->bge_flags & BGE_RXBDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->bge_vlan_tag;
		}

		if (cur_rx->bge_flags & BGE_RXBDFLAG_JUMBO_RING) {
			BGE_INC(sc->bge_jumbo, BGE_JUMBO_RX_RING_CNT);
			jumbocnt++;

			if (rxidx != sc->bge_jumbo) {
				ifp->if_ierrors++;
				if_printf(ifp, "sw jumbo index(%d) "
				    "and hw jumbo index(%d) mismatch, drop!\n",
				    sc->bge_jumbo, rxidx);
				bge_setup_rxdesc_jumbo(sc, rxidx);
				continue;
			}

			m = sc->bge_cdata.bge_rx_jumbo_chain[rxidx].bge_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				ifp->if_ierrors++;
				bge_setup_rxdesc_jumbo(sc, sc->bge_jumbo);
				continue;
			}
			if (bge_newbuf_jumbo(sc, sc->bge_jumbo, 0)) {
				ifp->if_ierrors++;
				bge_setup_rxdesc_jumbo(sc, sc->bge_jumbo);
				continue;
			}
		} else {
			BGE_INC(sc->bge_std, BGE_STD_RX_RING_CNT);
			stdcnt++;

			if (rxidx != sc->bge_std) {
				ifp->if_ierrors++;
				if_printf(ifp, "sw std index(%d) "
				    "and hw std index(%d) mismatch, drop!\n",
				    sc->bge_std, rxidx);
				bge_setup_rxdesc_std(sc, rxidx);
				continue;
			}

			m = sc->bge_cdata.bge_rx_std_chain[rxidx].bge_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				ifp->if_ierrors++;
				bge_setup_rxdesc_std(sc, sc->bge_std);
				continue;
			}
			if (bge_newbuf_std(sc, sc->bge_std, 0)) {
				ifp->if_ierrors++;
				bge_setup_rxdesc_std(sc, sc->bge_std);
				continue;
			}
		}

		ifp->if_ipackets++;
#ifndef __i386__
		/*
		 * The i386 allows unaligned accesses, but for other
		 * platforms we must make sure the payload is aligned.
		 */
		if (sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) {
			bcopy(m->m_data, m->m_data + ETHER_ALIGN,
			    cur_rx->bge_len);
			m->m_data += ETHER_ALIGN;
		}
#endif
		m->m_pkthdr.len = m->m_len = cur_rx->bge_len - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;

		if (ifp->if_capenable & IFCAP_RXCSUM) {
			if (cur_rx->bge_flags & BGE_RXBDFLAG_IP_CSUM) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((cur_rx->bge_ip_csum ^ 0xffff) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}
			if ((cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_CSUM) &&
			    m->m_pkthdr.len >= BGE_MIN_FRAME) {
				m->m_pkthdr.csum_data =
					cur_rx->bge_tcp_udp_csum;
				m->m_pkthdr.csum_flags |=
					CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			}
		}

		/*
		 * If we received a packet with a vlan tag, pass it
		 * to vlan_input() instead of ether_input().
		 */
		if (have_tag) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag = vlan_tag;
			have_tag = vlan_tag = 0;
		}
		ether_input_chain(ifp, m, NULL, chain);
	}

	ether_input_dispatch(chain);

	bge_writembx(sc, BGE_MBX_RX_CONS0_LO, sc->bge_rx_saved_considx);
	if (stdcnt)
		bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, sc->bge_std);
	if (jumbocnt)
		bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, sc->bge_jumbo);
}

static void
bge_txeof(struct bge_softc *sc)
{
	struct bge_tx_bd *cur_tx = NULL;
	struct ifnet *ifp;

	if (sc->bge_tx_saved_considx ==
	    sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx)
		return;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->bge_tx_saved_considx !=
	       sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx) {
		uint32_t idx = 0;

		idx = sc->bge_tx_saved_considx;
		cur_tx = &sc->bge_ldata.bge_tx_ring[idx];
		if (cur_tx->bge_flags & BGE_TXBDFLAG_END)
			ifp->if_opackets++;
		if (sc->bge_cdata.bge_tx_chain[idx] != NULL) {
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[idx]);
			m_freem(sc->bge_cdata.bge_tx_chain[idx]);
			sc->bge_cdata.bge_tx_chain[idx] = NULL;
		}
		sc->bge_txcnt--;
		BGE_INC(sc->bge_tx_saved_considx, BGE_TX_RING_CNT);
		logif(tx_pkt);
	}

	if (cur_tx != NULL &&
	    (BGE_TX_RING_CNT - sc->bge_txcnt) >=
	    (BGE_NSEG_RSVD + BGE_NSEG_SPARE))
		ifp->if_flags &= ~IFF_OACTIVE;

	if (sc->bge_txcnt == 0)
		ifp->if_timer = 0;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

#ifdef DEVICE_POLLING

static void
bge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct bge_softc *sc = ifp->if_softc;
 	uint32_t status;

	switch(cmd) {
	case POLL_REGISTER:
		bge_disable_intr(sc);
		break;
	case POLL_DEREGISTER:
		bge_enable_intr(sc);
		break;
	case POLL_AND_CHECK_STATUS:
		/*
		 * Process link state changes.
		 */
		status = CSR_READ_4(sc, BGE_MAC_STS);
		if ((status & sc->bge_link_chg) || sc->bge_link_evt) {
			sc->bge_link_evt = 0;
			sc->bge_link_upd(sc, status);
		}
		/* fall through */
	case POLL_ONLY:
		if (ifp->if_flags & IFF_RUNNING) {
			bge_rxeof(sc);
			bge_txeof(sc);
		}
		break;
	}
}

#endif

static void
bge_intr(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t status;

	logif(intr);

 	/*
	 * Ack the interrupt by writing something to BGE_MBX_IRQ0_LO.  Don't
	 * disable interrupts by writing nonzero like we used to, since with
	 * our current organization this just gives complications and
	 * pessimizations for re-enabling interrupts.  We used to have races
	 * instead of the necessary complications.  Disabling interrupts
	 * would just reduce the chance of a status update while we are
	 * running (by switching to the interrupt-mode coalescence
	 * parameters), but this chance is already very low so it is more
	 * efficient to get another interrupt than prevent it.
	 *
	 * We do the ack first to ensure another interrupt if there is a
	 * status update after the ack.  We don't check for the status
	 * changing later because it is more efficient to get another
	 * interrupt than prevent it, not quite as above (not checking is
	 * a smaller optimization than not toggling the interrupt enable,
	 * since checking doesn't involve PCI accesses and toggling require
	 * the status check).  So toggling would probably be a pessimization
	 * even with MSI.  It would only be needed for using a task queue.
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 0);

	/*
	 * Process link state changes.
	 */
	status = CSR_READ_4(sc, BGE_MAC_STS);
	if ((status & sc->bge_link_chg) || sc->bge_link_evt) {
		sc->bge_link_evt = 0;
		sc->bge_link_upd(sc, status);
	}

	if (ifp->if_flags & IFF_RUNNING) {
		/* Check RX return ring producer/consumer */
		bge_rxeof(sc);

		/* Check TX ring producer/consumer */
		bge_txeof(sc);
	}

	if (sc->bge_coal_chg)
		bge_coal_change(sc);
}

static void
bge_tick(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (BGE_IS_5705_PLUS(sc))
		bge_stats_update_regs(sc);
	else
		bge_stats_update(sc);

	if (sc->bge_flags & BGE_FLAG_TBI) {
		/*
		 * Since in TBI mode auto-polling can't be used we should poll
		 * link status manually. Here we register pending link event
		 * and trigger interrupt.
		 */
		sc->bge_link_evt++;
		BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
	} else if (!sc->bge_link) {
		mii_tick(device_get_softc(sc->bge_miibus));
	}

	callout_reset(&sc->bge_stat_timer, hz, bge_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
bge_stats_update_regs(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_mac_stats_regs stats;
	uint32_t *s;
	int i;

	s = (uint32_t *)&stats;
	for (i = 0; i < sizeof(struct bge_mac_stats_regs); i += 4) {
		*s = CSR_READ_4(sc, BGE_RX_STATS + i);
		s++;
	}

	ifp->if_collisions +=
	   (stats.dot3StatsSingleCollisionFrames +
	   stats.dot3StatsMultipleCollisionFrames +
	   stats.dot3StatsExcessiveCollisions +
	   stats.dot3StatsLateCollisions) -
	   ifp->if_collisions;
}

static void
bge_stats_update(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	bus_size_t stats;

	stats = BGE_MEMWIN_START + BGE_STATS_BLOCK;

#define READ_STAT(sc, stats, stat)	\
	CSR_READ_4(sc, stats + offsetof(struct bge_stats, stat))

	ifp->if_collisions +=
	   (READ_STAT(sc, stats,
		txstats.dot3StatsSingleCollisionFrames.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsMultipleCollisionFrames.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsExcessiveCollisions.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsLateCollisions.bge_addr_lo)) -
	   ifp->if_collisions;

#undef READ_STAT

#ifdef notdef
	ifp->if_collisions +=
	   (sc->bge_rdata->bge_info.bge_stats.dot3StatsSingleCollisionFrames +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsMultipleCollisionFrames +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsExcessiveCollisions +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsLateCollisions) -
	   ifp->if_collisions;
#endif
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
bge_encap(struct bge_softc *sc, struct mbuf **m_head0, uint32_t *txidx)
{
	struct bge_tx_bd *d = NULL;
	uint16_t csum_flags = 0;
	bus_dma_segment_t segs[BGE_NSEG_NEW];
	bus_dmamap_t map;
	int error, maxsegs, nsegs, idx, i;
	struct mbuf *m_head = *m_head0;

	if (m_head->m_pkthdr.csum_flags) {
		if (m_head->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= BGE_TXBDFLAG_IP_CSUM;
		if (m_head->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
			csum_flags |= BGE_TXBDFLAG_TCP_UDP_CSUM;
		if (m_head->m_flags & M_LASTFRAG)
			csum_flags |= BGE_TXBDFLAG_IP_FRAG_END;
		else if (m_head->m_flags & M_FRAG)
			csum_flags |= BGE_TXBDFLAG_IP_FRAG;
	}

	idx = *txidx;
	map = sc->bge_cdata.bge_tx_dmamap[idx];

	maxsegs = (BGE_TX_RING_CNT - sc->bge_txcnt) - BGE_NSEG_RSVD;
	KASSERT(maxsegs >= BGE_NSEG_SPARE,
		("not enough segments %d\n", maxsegs));

	if (maxsegs > BGE_NSEG_NEW)
		maxsegs = BGE_NSEG_NEW;

	/*
	 * Pad outbound frame to BGE_MIN_FRAME for an unusual reason.
	 * The bge hardware will pad out Tx runts to BGE_MIN_FRAME,
	 * but when such padded frames employ the bge IP/TCP checksum
	 * offload, the hardware checksum assist gives incorrect results
	 * (possibly from incorporating its own padding into the UDP/TCP
	 * checksum; who knows).  If we pad such runts with zeros, the
	 * onboard checksum comes out correct.
	 */
	if ((csum_flags & BGE_TXBDFLAG_TCP_UDP_CSUM) &&
	    m_head->m_pkthdr.len < BGE_MIN_FRAME) {
		error = m_devpad(m_head, BGE_MIN_FRAME);
		if (error)
			goto back;
	}

	error = bus_dmamap_load_mbuf_defrag(sc->bge_cdata.bge_tx_mtag, map,
			m_head0, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;

	m_head = *m_head0;
	bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	for (i = 0; ; i++) {
		d = &sc->bge_ldata.bge_tx_ring[idx];

		d->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[i].ds_addr);
		d->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[i].ds_addr);
		d->bge_len = segs[i].ds_len;
		d->bge_flags = csum_flags;

		if (i == nsegs - 1)
			break;
		BGE_INC(idx, BGE_TX_RING_CNT);
	}
	/* Mark the last segment as end of packet... */
	d->bge_flags |= BGE_TXBDFLAG_END;

	/* Set vlan tag to the first segment of the packet. */
	d = &sc->bge_ldata.bge_tx_ring[*txidx];
	if (m_head->m_flags & M_VLANTAG) {
		d->bge_flags |= BGE_TXBDFLAG_VLAN_TAG;
		d->bge_vlan_tag = m_head->m_pkthdr.ether_vlantag;
	} else {
		d->bge_vlan_tag = 0;
	}

	/*
	 * Insure that the map for this transmission is placed at
	 * the array index of the last descriptor in this chain.
	 */
	sc->bge_cdata.bge_tx_dmamap[*txidx] = sc->bge_cdata.bge_tx_dmamap[idx];
	sc->bge_cdata.bge_tx_dmamap[idx] = map;
	sc->bge_cdata.bge_tx_chain[idx] = m_head;
	sc->bge_txcnt += nsegs;

	BGE_INC(idx, BGE_TX_RING_CNT);
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
bge_start(struct ifnet *ifp)
{
	struct bge_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	uint32_t prodidx;
	int need_trans;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	prodidx = sc->bge_tx_prodidx;

	need_trans = 0;
	while (sc->bge_cdata.bge_tx_chain[prodidx] == NULL) {
		m_head = ifq_dequeue(&ifp->if_snd, NULL);
		if (m_head == NULL)
			break;

		/*
		 * XXX
		 * The code inside the if() block is never reached since we
		 * must mark CSUM_IP_FRAGS in our if_hwassist to start getting
		 * requests to checksum TCP/UDP in a fragmented packet.
		 * 
		 * XXX
		 * safety overkill.  If this is a fragmented packet chain
		 * with delayed TCP/UDP checksums, then only encapsulate
		 * it if we have enough descriptors to handle the entire
		 * chain at once.
		 * (paranoia -- may not actually be needed)
		 */
		if ((m_head->m_flags & M_FIRSTFRAG) &&
		    (m_head->m_pkthdr.csum_flags & CSUM_DELAY_DATA)) {
			if ((BGE_TX_RING_CNT - sc->bge_txcnt) <
			    m_head->m_pkthdr.csum_data + BGE_NSEG_RSVD) {
				ifp->if_flags |= IFF_OACTIVE;
				ifq_prepend(&ifp->if_snd, m_head);
				break;
			}
		}

		/*
		 * Sanity check: avoid coming within BGE_NSEG_RSVD
		 * descriptors of the end of the ring.  Also make
		 * sure there are BGE_NSEG_SPARE descriptors for
		 * jumbo buffers' defragmentation.
		 */
		if ((BGE_TX_RING_CNT - sc->bge_txcnt) <
		    (BGE_NSEG_RSVD + BGE_NSEG_SPARE)) {
			ifp->if_flags |= IFF_OACTIVE;
			ifq_prepend(&ifp->if_snd, m_head);
			break;
		}

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (bge_encap(sc, &m_head, &prodidx)) {
			ifp->if_flags |= IFF_OACTIVE;
			ifp->if_oerrors++;
			break;
		}
		need_trans = 1;

		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (!need_trans)
		return;

	/* Transmit */
	bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);
	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);

	sc->bge_tx_prodidx = prodidx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
bge_init(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t *m;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING)
		return;

	/* Cancel pending I/O and flush buffers. */
	bge_stop(sc);
	bge_reset(sc);
	bge_chipinit(sc);

	/*
	 * Init the various state machines, ring
	 * control blocks and firmware.
	 */
	if (bge_blockinit(sc)) {
		if_printf(ifp, "initialization failure\n");
		bge_stop(sc);
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
	bge_setpromisc(sc);

	/* Program multicast filter. */
	bge_setmulti(sc);

	/* Init RX ring. */
	if (bge_init_rx_ring_std(sc)) {
		if_printf(ifp, "RX ring initialization failed\n");
		bge_stop(sc);
		return;
	}

	/*
	 * Workaround for a bug in 5705 ASIC rev A0. Poll the NIC's
	 * memory to insure that the chip has in fact read the first
	 * entry of the ring.
	 */
	if (sc->bge_chipid == BGE_CHIPID_BCM5705_A0) {
		uint32_t		v, i;
		for (i = 0; i < 10; i++) {
			DELAY(20);
			v = bge_readmem_ind(sc, BGE_STD_RX_RINGS + 8);
			if (v == (MCLBYTES - ETHER_ALIGN))
				break;
		}
		if (i == 10)
			if_printf(ifp, "5705 A0 chip failed to load RX ring\n");
	}

	/* Init jumbo RX ring. */
	if (ifp->if_mtu > (ETHERMTU + ETHER_HDR_LEN + ETHER_CRC_LEN)) {
		if (bge_init_rx_ring_jumbo(sc)) {
			if_printf(ifp, "Jumbo RX ring initialization failed\n");
			bge_stop(sc);
			return;
		}
	}

	/* Init our RX return ring index */
	sc->bge_rx_saved_considx = 0;

	/* Init TX ring. */
	bge_init_tx_ring(sc);

	/* Turn on transmitter */
	BGE_SETBIT(sc, BGE_TX_MODE, BGE_TXMODE_ENABLE);

	/* Turn on receiver */
	BGE_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);

	/* Tell firmware we're alive. */
	BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Enable host interrupts if polling(4) is not enabled. */
	BGE_SETBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_CLEAR_INTA);
#ifdef DEVICE_POLLING
	if (ifp->if_flags & IFF_POLLING)
		bge_disable_intr(sc);
	else
#endif
	bge_enable_intr(sc);

	bge_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset(&sc->bge_stat_timer, hz, bge_tick, sc);
}

/*
 * Set media options.
 */
static int
bge_ifmedia_upd(struct ifnet *ifp)
{
	struct bge_softc *sc = ifp->if_softc;

	/* If this is a 1000baseX NIC, enable the TBI port. */
	if (sc->bge_flags & BGE_FLAG_TBI) {
		struct ifmedia *ifm = &sc->bge_ifmedia;

		if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
			return(EINVAL);

		switch(IFM_SUBTYPE(ifm->ifm_media)) {
		case IFM_AUTO:
			/*
			 * The BCM5704 ASIC appears to have a special
			 * mechanism for programming the autoneg
			 * advertisement registers in TBI mode.
			 */
			if (!bge_fake_autoneg &&
			    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
				uint32_t sgdig;

				CSR_WRITE_4(sc, BGE_TX_TBI_AUTONEG, 0);
				sgdig = CSR_READ_4(sc, BGE_SGDIG_CFG);
				sgdig |= BGE_SGDIGCFG_AUTO |
					 BGE_SGDIGCFG_PAUSE_CAP |
					 BGE_SGDIGCFG_ASYM_PAUSE;
				CSR_WRITE_4(sc, BGE_SGDIG_CFG,
					    sgdig | BGE_SGDIGCFG_SEND);
				DELAY(5);
				CSR_WRITE_4(sc, BGE_SGDIG_CFG, sgdig);
			}
			break;
		case IFM_1000_SX:
			if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
				BGE_CLRBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			} else {
				BGE_SETBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			}
			break;
		default:
			return(EINVAL);
		}
	} else {
		struct mii_data *mii = device_get_softc(sc->bge_miibus);

		sc->bge_link_evt++;
		sc->bge_link = 0;
		if (mii->mii_instance) {
			struct mii_softc *miisc;

			LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
				mii_phy_reset(miisc);
		}
		mii_mediachg(mii);
	}
	return(0);
}

/*
 * Report current media status.
 */
static void
bge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bge_softc *sc = ifp->if_softc;

	if (sc->bge_flags & BGE_FLAG_TBI) {
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
		struct mii_data *mii = device_get_softc(sc->bge_miibus);

		mii_pollstat(mii);
		ifmr->ifm_active = mii->mii_media_active;
		ifmr->ifm_status = mii->mii_media_status;
	}
}

static int
bge_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct bge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int mask, error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (command) {
	case SIOCSIFMTU:
		if ((!BGE_IS_JUMBO_CAPABLE(sc) && ifr->ifr_mtu > ETHERMTU) ||
		    (BGE_IS_JUMBO_CAPABLE(sc) &&
		     ifr->ifr_mtu > BGE_JUMBO_MTU)) {
			error = EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			ifp->if_flags &= ~IFF_RUNNING;
			bge_init(sc);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				mask = ifp->if_flags ^ sc->bge_if_flags;

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
					bge_setpromisc(sc);
				if (mask & IFF_ALLMULTI)
					bge_setmulti(sc);
			} else {
				bge_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				bge_stop(sc);
		}
		sc->bge_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			bge_setmulti(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->bge_flags & BGE_FLAG_TBI) {
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->bge_ifmedia, command);
		} else {
			struct mii_data *mii;

			mii = device_get_softc(sc->bge_miibus);
			error = ifmedia_ioctl(ifp, ifr,
					      &mii->mii_media, command);
		}
		break;
        case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable ^= (mask & IFCAP_HWCSUM);
			if (IFCAP_HWCSUM & ifp->if_capenable)
				ifp->if_hwassist = BGE_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return error;
}

static void
bge_watchdog(struct ifnet *ifp)
{
	struct bge_softc *sc = ifp->if_softc;

	if_printf(ifp, "watchdog timeout -- resetting\n");

	ifp->if_flags &= ~IFF_RUNNING;
	bge_init(sc);

	ifp->if_oerrors++;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
bge_stop(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmedia_entry *ifm;
	struct mii_data *mii = NULL;
	int mtmp, itmp;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->bge_flags & BGE_FLAG_TBI) == 0)
		mii = device_get_softc(sc->bge_miibus);

	callout_stop(&sc->bge_stat_timer);

	/*
	 * Disable all of the receiver blocks
	 */
	BGE_CLRBIT(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);
	if (!BGE_IS_5705_PLUS(sc))
		BGE_CLRBIT(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RDBDI_MODE, BGE_RBDIMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RBDC_MODE, BGE_RBDCMODE_ENABLE);

	/*
	 * Disable all of the transmit blocks
	 */
	BGE_CLRBIT(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_RDMA_MODE, BGE_RDMAMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_SDC_MODE, BGE_SDCMODE_ENABLE);
	if (!BGE_IS_5705_PLUS(sc))
		BGE_CLRBIT(sc, BGE_DMAC_MODE, BGE_DMACMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/*
	 * Shut down all of the memory managers and related
	 * state machines.
	 */
	BGE_CLRBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_ENABLE);
	BGE_CLRBIT(sc, BGE_WDMA_MODE, BGE_WDMAMODE_ENABLE);
	if (!BGE_IS_5705_PLUS(sc))
		BGE_CLRBIT(sc, BGE_MBCF_MODE, BGE_MBCFMODE_ENABLE);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);
	if (!BGE_IS_5705_PLUS(sc)) {
		BGE_CLRBIT(sc, BGE_BMAN_MODE, BGE_BMANMODE_ENABLE);
		BGE_CLRBIT(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
	}

	/* Disable host interrupts. */
	bge_disable_intr(sc);

	/*
	 * Tell firmware we're shutting down.
	 */
	BGE_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Free the RX lists. */
	bge_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	if (BGE_IS_JUMBO_CAPABLE(sc))
		bge_free_rx_ring_jumbo(sc);

	/* Free TX buffers. */
	bge_free_tx_ring(sc);

	/*
	 * Isolate/power down the PHY, but leave the media selection
	 * unchanged so that things will be put back to normal when
	 * we bring the interface back up.
	 *
	 * 'mii' may be NULL in the following cases:
	 * - The device uses TBI.
	 * - bge_stop() is called by bge_detach().
	 */
	if (mii != NULL) {
		itmp = ifp->if_flags;
		ifp->if_flags |= IFF_UP;
		ifm = mii->mii_media.ifm_cur;
		mtmp = ifm->ifm_media;
		ifm->ifm_media = IFM_ETHER|IFM_NONE;
		mii_mediachg(mii);
		ifm->ifm_media = mtmp;
		ifp->if_flags = itmp;
	}

	sc->bge_link = 0;
	sc->bge_coal_chg = 0;

	sc->bge_tx_saved_considx = BGE_TXCONS_UNSET;

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
bge_shutdown(device_t dev)
{
	struct bge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	bge_stop(sc);
	bge_reset(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
bge_suspend(device_t dev)
{
	struct bge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	bge_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static int
bge_resume(device_t dev)
{
	struct bge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ifp->if_flags & IFF_UP) {
		bge_init(sc);

		if (!ifq_is_empty(&ifp->if_snd))
			if_devstart(ifp);
	}

	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static void
bge_setpromisc(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (ifp->if_flags & IFF_PROMISC)
		BGE_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
	else
		BGE_CLRBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
}

static void
bge_dma_free(struct bge_softc *sc)
{
	int i;

	/* Destroy RX mbuf DMA stuffs. */
	if (sc->bge_cdata.bge_rx_mtag != NULL) {
		for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
			bus_dmamap_destroy(sc->bge_cdata.bge_rx_mtag,
			    sc->bge_cdata.bge_rx_std_dmamap[i]);
		}
		bus_dmamap_destroy(sc->bge_cdata.bge_rx_mtag,
				   sc->bge_cdata.bge_rx_tmpmap);
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_mtag);
	}

	/* Destroy TX mbuf DMA stuffs. */
	if (sc->bge_cdata.bge_tx_mtag != NULL) {
		for (i = 0; i < BGE_TX_RING_CNT; i++) {
			bus_dmamap_destroy(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->bge_cdata.bge_tx_mtag);
	}

	/* Destroy standard RX ring */
	bge_dma_block_free(sc->bge_cdata.bge_rx_std_ring_tag,
			   sc->bge_cdata.bge_rx_std_ring_map,
			   sc->bge_ldata.bge_rx_std_ring);

	if (BGE_IS_JUMBO_CAPABLE(sc))
		bge_free_jumbo_mem(sc);

	/* Destroy RX return ring */
	bge_dma_block_free(sc->bge_cdata.bge_rx_return_ring_tag,
			   sc->bge_cdata.bge_rx_return_ring_map,
			   sc->bge_ldata.bge_rx_return_ring);

	/* Destroy TX ring */
	bge_dma_block_free(sc->bge_cdata.bge_tx_ring_tag,
			   sc->bge_cdata.bge_tx_ring_map,
			   sc->bge_ldata.bge_tx_ring);

	/* Destroy status block */
	bge_dma_block_free(sc->bge_cdata.bge_status_tag,
			   sc->bge_cdata.bge_status_map,
			   sc->bge_ldata.bge_status_block);

	/* Destroy statistics block */
	bge_dma_block_free(sc->bge_cdata.bge_stats_tag,
			   sc->bge_cdata.bge_stats_map,
			   sc->bge_ldata.bge_stats);

	/* Destroy the parent tag */
	if (sc->bge_cdata.bge_parent_tag != NULL)
		bus_dma_tag_destroy(sc->bge_cdata.bge_parent_tag);
}

static int
bge_dma_alloc(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i, error;

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   BUS_SPACE_MAXSIZE_32BIT, 0,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->bge_cdata.bge_parent_tag);
	if (error) {
		if_printf(ifp, "could not allocate parent dma tag\n");
		return error;
	}

	/*
	 * Create DMA tag and maps for RX mbufs.
	 */
	error = bus_dma_tag_create(sc->bge_cdata.bge_parent_tag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL, MCLBYTES, 1, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,
				   &sc->bge_cdata.bge_rx_mtag);
	if (error) {
		if_printf(ifp, "could not allocate RX mbuf dma tag\n");
		return error;
	}

	error = bus_dmamap_create(sc->bge_cdata.bge_rx_mtag,
				  BUS_DMA_WAITOK, &sc->bge_cdata.bge_rx_tmpmap);
	if (error) {
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_mtag);
		sc->bge_cdata.bge_rx_mtag = NULL;
		return error;
	}

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bge_cdata.bge_rx_mtag,
					  BUS_DMA_WAITOK,
					  &sc->bge_cdata.bge_rx_std_dmamap[i]);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->bge_cdata.bge_rx_mtag,
					sc->bge_cdata.bge_rx_std_dmamap[j]);
			}
			bus_dma_tag_destroy(sc->bge_cdata.bge_rx_mtag);
			sc->bge_cdata.bge_rx_mtag = NULL;

			if_printf(ifp, "could not create DMA map for RX\n");
			return error;
		}
	}

	/*
	 * Create DMA tag and maps for TX mbufs.
	 */
	error = bus_dma_tag_create(sc->bge_cdata.bge_parent_tag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   BGE_JUMBO_FRAMELEN, BGE_NSEG_NEW, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK |
				   BUS_DMA_ONEBPAGE,
				   &sc->bge_cdata.bge_tx_mtag);
	if (error) {
		if_printf(ifp, "could not allocate TX mbuf dma tag\n");
		return error;
	}

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bge_cdata.bge_tx_mtag,
					  BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
					  &sc->bge_cdata.bge_tx_dmamap[i]);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->bge_cdata.bge_tx_mtag,
					sc->bge_cdata.bge_tx_dmamap[j]);
			}
			bus_dma_tag_destroy(sc->bge_cdata.bge_tx_mtag);
			sc->bge_cdata.bge_tx_mtag = NULL;

			if_printf(ifp, "could not create DMA map for TX\n");
			return error;
		}
	}

	/*
	 * Create DMA stuffs for standard RX ring.
	 */
	error = bge_dma_block_alloc(sc, BGE_STD_RX_RING_SZ,
				    &sc->bge_cdata.bge_rx_std_ring_tag,
				    &sc->bge_cdata.bge_rx_std_ring_map,
				    (void *)&sc->bge_ldata.bge_rx_std_ring,
				    &sc->bge_ldata.bge_rx_std_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create std RX ring\n");
		return error;
	}

	/*
	 * Create jumbo buffer pool.
	 */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		error = bge_alloc_jumbo_mem(sc);
		if (error) {
			if_printf(ifp, "could not create jumbo buffer pool\n");
			return error;
		}
	}

	/*
	 * Create DMA stuffs for RX return ring.
	 */
	error = bge_dma_block_alloc(sc, BGE_RX_RTN_RING_SZ(sc),
				    &sc->bge_cdata.bge_rx_return_ring_tag,
				    &sc->bge_cdata.bge_rx_return_ring_map,
				    (void *)&sc->bge_ldata.bge_rx_return_ring,
				    &sc->bge_ldata.bge_rx_return_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create RX ret ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for TX ring.
	 */
	error = bge_dma_block_alloc(sc, BGE_TX_RING_SZ,
				    &sc->bge_cdata.bge_tx_ring_tag,
				    &sc->bge_cdata.bge_tx_ring_map,
				    (void *)&sc->bge_ldata.bge_tx_ring,
				    &sc->bge_ldata.bge_tx_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create TX ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for status block.
	 */
	error = bge_dma_block_alloc(sc, BGE_STATUS_BLK_SZ,
				    &sc->bge_cdata.bge_status_tag,
				    &sc->bge_cdata.bge_status_map,
				    (void *)&sc->bge_ldata.bge_status_block,
				    &sc->bge_ldata.bge_status_block_paddr);
	if (error) {
		if_printf(ifp, "could not create status block\n");
		return error;
	}

	/*
	 * Create DMA stuffs for statistics block.
	 */
	error = bge_dma_block_alloc(sc, BGE_STATS_SZ,
				    &sc->bge_cdata.bge_stats_tag,
				    &sc->bge_cdata.bge_stats_map,
				    (void *)&sc->bge_ldata.bge_stats,
				    &sc->bge_ldata.bge_stats_paddr);
	if (error) {
		if_printf(ifp, "could not create stats block\n");
		return error;
	}
	return 0;
}

static int
bge_dma_block_alloc(struct bge_softc *sc, bus_size_t size, bus_dma_tag_t *tag,
		    bus_dmamap_t *map, void **addr, bus_addr_t *paddr)
{
	bus_dmamem_t dmem;
	int error;

	error = bus_dmamem_coherent(sc->bge_cdata.bge_parent_tag, PAGE_SIZE, 0,
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
bge_dma_block_free(bus_dma_tag_t tag, bus_dmamap_t map, void *addr)
{
	if (tag != NULL) {
		bus_dmamap_unload(tag, map);
		bus_dmamem_free(tag, addr, map);
		bus_dma_tag_destroy(tag);
	}
}

/*
 * Grrr. The link status word in the status block does
 * not work correctly on the BCM5700 rev AX and BX chips,
 * according to all available information. Hence, we have
 * to enable MII interrupts in order to properly obtain
 * async link changes. Unfortunately, this also means that
 * we have to read the MAC status register to detect link
 * changes, thereby adding an additional register access to
 * the interrupt handler.
 *
 * XXX: perhaps link state detection procedure used for
 * BGE_CHIPID_BCM5700_B2 can be used for others BCM5700 revisions.
 */
static void
bge_bcm5700_link_upd(struct bge_softc *sc, uint32_t status __unused)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->bge_miibus);

	mii_pollstat(mii);

	if (!sc->bge_link &&
	    (mii->mii_media_status & IFM_ACTIVE) &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->bge_link++;
		if (bootverbose)
			if_printf(ifp, "link UP\n");
	} else if (sc->bge_link &&
	    (!(mii->mii_media_status & IFM_ACTIVE) ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)) {
		sc->bge_link = 0;
		if (bootverbose)
			if_printf(ifp, "link DOWN\n");
	}

	/* Clear the interrupt. */
	CSR_WRITE_4(sc, BGE_MAC_EVT_ENB, BGE_EVTENB_MI_INTERRUPT);
	bge_miibus_readreg(sc->bge_dev, 1, BRGPHY_MII_ISR);
	bge_miibus_writereg(sc->bge_dev, 1, BRGPHY_MII_IMR, BRGPHY_INTRS);
}

static void
bge_tbi_link_upd(struct bge_softc *sc, uint32_t status)
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
		if (!sc->bge_link) {
			sc->bge_link++;
			if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
				BGE_CLRBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_TBI_SEND_CFGS);
			}
			CSR_WRITE_4(sc, BGE_MAC_STS, 0xFFFFFFFF);

			if (bootverbose)
				if_printf(ifp, "link UP\n");

			ifp->if_link_state = LINK_STATE_UP;
			if_link_state_change(ifp);
		}
	} else if ((status & PCS_ENCODE_ERR) != PCS_ENCODE_ERR) {
		if (sc->bge_link) {
			sc->bge_link = 0;

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
bge_copper_link_upd(struct bge_softc *sc, uint32_t status __unused)
{
	/*
	 * Check that the AUTOPOLL bit is set before
	 * processing the event as a real link change.
	 * Turning AUTOPOLL on and off in the MII read/write
	 * functions will often trigger a link status
	 * interrupt for no reason.
	 */
	if (CSR_READ_4(sc, BGE_MI_MODE) & BGE_MIMODE_AUTOPOLL) {
		struct ifnet *ifp = &sc->arpcom.ac_if;
		struct mii_data *mii = device_get_softc(sc->bge_miibus);

		mii_pollstat(mii);

		if (!sc->bge_link &&
		    (mii->mii_media_status & IFM_ACTIVE) &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->bge_link++;
			if (bootverbose)
				if_printf(ifp, "link UP\n");
		} else if (sc->bge_link &&
		    (!(mii->mii_media_status & IFM_ACTIVE) ||
		    IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)) {
			sc->bge_link = 0;
			if (bootverbose)
				if_printf(ifp, "link DOWN\n");
		}
	}

	/* Clear the attention. */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
}

static int
bge_sysctl_rx_coal_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
				   &sc->bge_rx_coal_ticks,
				   BGE_RX_COAL_TICKS_CHG);
}

static int
bge_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
				   &sc->bge_tx_coal_ticks,
				   BGE_TX_COAL_TICKS_CHG);
}

static int
bge_sysctl_rx_max_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
				   &sc->bge_rx_max_coal_bds,
				   BGE_RX_MAX_COAL_BDS_CHG);
}

static int
bge_sysctl_tx_max_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
				   &sc->bge_tx_max_coal_bds,
				   BGE_TX_MAX_COAL_BDS_CHG);
}

static int
bge_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *coal,
		    uint32_t coal_chg_mask)
{
	struct bge_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = *coal;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != NULL) {
		if (v < 0) {
			error = EINVAL;
		} else {
			*coal = v;
			sc->bge_coal_chg |= coal_chg_mask;
		}
	}

	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
bge_coal_change(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->bge_coal_chg & BGE_RX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS,
			    sc->bge_rx_coal_ticks);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_RX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_ticks -> %u\n",
				  sc->bge_rx_coal_ticks);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS,
			    sc->bge_tx_coal_ticks);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_TX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_ticks -> %u\n",
				  sc->bge_tx_coal_ticks);
		}
	}

	if (sc->bge_coal_chg & BGE_RX_MAX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS,
			    sc->bge_rx_max_coal_bds);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_RX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "rx_max_coal_bds -> %u\n",
				  sc->bge_rx_max_coal_bds);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_MAX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS,
			    sc->bge_tx_max_coal_bds);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_TX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "tx_max_coal_bds -> %u\n",
				  sc->bge_tx_max_coal_bds);
		}
	}

	sc->bge_coal_chg = 0;
}

static void
bge_enable_intr(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_handler_enable(ifp->if_serializer);

	/*
	 * Enable interrupt.
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 0);

	/*
	 * Unmask the interrupt when we stop polling.
	 */
	BGE_CLRBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);

	/*
	 * Trigger another interrupt, since above writing
	 * to interrupt mailbox0 may acknowledge pending
	 * interrupt.
	 */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
}

static void
bge_disable_intr(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/*
	 * Mask the interrupt when we start polling.
	 */
	BGE_SETBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);

	/*
	 * Acknowledge possible asserted interrupt.
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	lwkt_serialize_handler_disable(ifp->if_serializer);
}

static int
bge_get_eaddr_mem(struct bge_softc *sc, uint8_t ether_addr[])
{
	uint32_t mac_addr;
	int ret = 1;

	mac_addr = bge_readmem_ind(sc, 0x0c14);
	if ((mac_addr >> 16) == 0x484b) {
		ether_addr[0] = (uint8_t)(mac_addr >> 8);
		ether_addr[1] = (uint8_t)mac_addr;
		mac_addr = bge_readmem_ind(sc, 0x0c18);
		ether_addr[2] = (uint8_t)(mac_addr >> 24);
		ether_addr[3] = (uint8_t)(mac_addr >> 16);
		ether_addr[4] = (uint8_t)(mac_addr >> 8);
		ether_addr[5] = (uint8_t)mac_addr;
		ret = 0;
	}
	return ret;
}

static int
bge_get_eaddr_nvram(struct bge_softc *sc, uint8_t ether_addr[])
{
	int mac_offset = BGE_EE_MAC_OFFSET;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mac_offset = BGE_EE_MAC_OFFSET_5906;

	return bge_read_nvram(sc, ether_addr, mac_offset + 2, ETHER_ADDR_LEN);
}

static int
bge_get_eaddr_eeprom(struct bge_softc *sc, uint8_t ether_addr[])
{
	if (sc->bge_flags & BGE_FLAG_NO_EEPROM)
		return 1;

	return bge_read_eeprom(sc, ether_addr, BGE_EE_MAC_OFFSET + 2,
			       ETHER_ADDR_LEN);
}

static int
bge_get_eaddr(struct bge_softc *sc, uint8_t eaddr[])
{
	static const bge_eaddr_fcn_t bge_eaddr_funcs[] = {
		/* NOTE: Order is critical */
		bge_get_eaddr_mem,
		bge_get_eaddr_nvram,
		bge_get_eaddr_eeprom,
		NULL
	};
	const bge_eaddr_fcn_t *func;

	for (func = bge_eaddr_funcs; *func != NULL; ++func) {
		if ((*func)(sc, eaddr) == 0)
			break;
	}
	return (*func == NULL ? ENXIO : 0);
}
