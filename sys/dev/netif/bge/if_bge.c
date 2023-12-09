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

#include "opt_ifpoll.h"

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
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>
#include <dev/netif/mii_layer/brgphyreg.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/bge/if_bgereg.h>
#include <dev/netif/bge/if_bgevar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define BGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP)

#define	BGE_RESET_SHUTDOWN	0
#define	BGE_RESET_START		1
#define	BGE_RESET_SUSPEND	2

static const struct bge_type {
	uint16_t		bge_vid;
	uint16_t		bge_did;
	char			*bge_name;
} bge_devs[] = {
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
#define BGE_IS_5788(sc)			((sc)->bge_flags & BGE_FLAG_5788)

#define BGE_IS_CRIPPLED(sc)		\
	(BGE_IS_5788((sc)) || (sc)->bge_asicrev == BGE_ASICREV_BCM5700)

typedef int	(*bge_eaddr_fcn_t)(struct bge_softc *, uint8_t[]);

static int	bge_probe(device_t);
static int	bge_attach(device_t);
static int	bge_detach(device_t);
static void	bge_txeof(struct bge_softc *, uint16_t);
static void	bge_rxeof(struct bge_softc *, uint16_t, int);

static void	bge_tick(void *);
static void	bge_stats_update(struct bge_softc *);
static void	bge_stats_update_regs(struct bge_softc *);
static struct mbuf *
		bge_defrag_shortdma(struct mbuf *);
static int	bge_encap(struct bge_softc *, struct mbuf **,
		    uint32_t *, int *);
static void	bge_xmit(struct bge_softc *, uint32_t);
static int	bge_setup_tso(struct bge_softc *, struct mbuf **,
		    uint16_t *, uint16_t *);

#ifdef IFPOLL_ENABLE
static void	bge_npoll(struct ifnet *, struct ifpoll_info *);
static void	bge_npoll_compat(struct ifnet *, void *, int );
#endif
static void	bge_intr_crippled(void *);
static void	bge_intr_legacy(void *);
static void	bge_msi(void *);
static void	bge_msi_oneshot(void *);
static void	bge_intr(struct bge_softc *);
static void	bge_enable_intr(struct bge_softc *);
static void	bge_disable_intr(struct bge_softc *);
static void	bge_start(struct ifnet *, struct ifaltq_subque *);
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
static void	bge_enable_msi(struct bge_softc *sc);

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
static void	bge_stop_block(struct bge_softc *, bus_size_t, uint32_t);

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
static void	bge_autopoll_link_upd(struct bge_softc *, uint32_t);
static void	bge_link_poll(struct bge_softc *);

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
static int	bge_sysctl_rx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_rx_coal_ticks_int(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_tx_coal_ticks_int(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_rx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_tx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bge_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *,
		    int, int, uint32_t);

static void	bge_sig_post_reset(struct bge_softc *, int);
static void	bge_sig_legacy(struct bge_softc *, int);
static void	bge_sig_pre_reset(struct bge_softc *, int);
static void	bge_stop_fw(struct bge_softc *);
static void	bge_asf_driver_up(struct bge_softc *);

static void	bge_ape_lock_init(struct bge_softc *);
static void	bge_ape_read_fw_ver(struct bge_softc *);
static int	bge_ape_lock(struct bge_softc *, int);
static void	bge_ape_unlock(struct bge_softc *, int);
static void	bge_ape_send_event(struct bge_softc *, uint32_t);
static void	bge_ape_driver_state_change(struct bge_softc *, int);

/*
 * Set following tunable to 1 for some IBM blade servers with the DNLK
 * switch module. Auto negotiation is broken for those configurations.
 */
static int	bge_fake_autoneg = 0;
TUNABLE_INT("hw.bge.fake_autoneg", &bge_fake_autoneg);

static int	bge_msi_enable = 1;
TUNABLE_INT("hw.bge.msi.enable", &bge_msi_enable);

static int	bge_allow_asf = 1;
TUNABLE_INT("hw.bge.allow_asf", &bge_allow_asf);

#if !defined(KTR_IF_BGE)
#define KTR_IF_BGE	KTR_ALL
#endif
KTR_INFO_MASTER(if_bge);
KTR_INFO(KTR_IF_BGE, if_bge, intr, 0, "intr");
KTR_INFO(KTR_IF_BGE, if_bge, rx_pkt, 1, "rx_pkt");
KTR_INFO(KTR_IF_BGE, if_bge, tx_pkt, 2, "tx_pkt");
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

	DEVMETHOD_END
};

static DEFINE_CLASS_0(bge, bge_driver, bge_methods, sizeof(struct bge_softc));
static devclass_t bge_devclass;

DECLARE_DUMMY_MODULE(if_bge);
MODULE_DEPEND(if_bge, miibus, 1, 1, 1);
DRIVER_MODULE(if_bge, pci, bge_driver, bge_devclass, NULL, NULL);
DRIVER_MODULE(miibus, bge, miibus_driver, miibus_devclass, NULL, NULL);

static uint32_t
bge_readmem_ind(struct bge_softc *sc, uint32_t off)
{
	device_t dev = sc->bge_dev;
	uint32_t val;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return 0;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	val = pci_read_config(dev, BGE_PCI_MEMWIN_DATA, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
	return (val);
}

static void
bge_writemem_ind(struct bge_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bge_dev;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return;

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
	if (sc->bge_mbox_reorder)
		CSR_READ_4(sc, off);
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
	uint32_t val;
	int i;

	KASSERT(phy == sc->bge_phyno,
	    ("invalid phyno %d, should be %d", phy, sc->bge_phyno));

	if (bge_ape_lock(sc, sc->bge_phy_ape_lock) != 0)
		return 0;

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bge_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_READ | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg));

	/* Poll for the PHY register access to complete. */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		val = CSR_READ_4(sc, BGE_MI_COMM);
		if ((val & BGE_MICOMM_BUSY) == 0) {
			DELAY(5);
			val = CSR_READ_4(sc, BGE_MI_COMM);
			break;
		}
	}
	if (i == BGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "PHY read timed out "
		    "(phy %d, reg %d, val 0x%08x)\n", phy, reg, val);
		val = 0;
	}

	/* Restore the autopoll bit if necessary. */
	if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
		DELAY(80);
	}

	bge_ape_unlock(sc, sc->bge_phy_ape_lock);

	if (val & BGE_MICOMM_READFAIL)
		return 0;

	return (val & 0xFFFF);
}

static int
bge_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct bge_softc *sc = device_get_softc(dev);
	int i;

	KASSERT(phy == sc->bge_phyno,
	    ("invalid phyno %d, should be %d", phy, sc->bge_phyno));

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    (reg == BRGPHY_MII_1000CTL || reg == BRGPHY_MII_AUXCTL))
	       return 0;

	if (bge_ape_lock(sc, sc->bge_phy_ape_lock) != 0)
		return 0;

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bge_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_WRITE | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg) | val);

	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, BGE_MI_COMM) & BGE_MICOMM_BUSY)) {
			DELAY(5);
			CSR_READ_4(sc, BGE_MI_COMM); /* dummy read */
			break;
		}
	}
	if (i == BGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "PHY write timed out "
		    "(phy %d, reg %d, val %d)\n", phy, reg, val);
	}

	/* Restore the autopoll bit if necessary. */
	if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
		DELAY(80);
	}

	bge_ape_unlock(sc, sc->bge_phy_ape_lock);

	return 0;
}

static void
bge_miibus_statchg(device_t dev)
{
	struct bge_softc *sc;
	struct mii_data *mii;
	uint32_t mac_mode;

	sc = device_get_softc(dev);
	if ((sc->arpcom.ac_if.if_flags & IFF_RUNNING) == 0)
		return;

	mii = device_get_softc(sc->bge_miibus);

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->bge_link = 1;
			break;
		case IFM_1000_T:
		case IFM_1000_SX:
		case IFM_2500_SX:
			if (sc->bge_asicrev != BGE_ASICREV_BCM5906)
				sc->bge_link = 1;
			else
				sc->bge_link = 0;
			break;
		default:
			sc->bge_link = 0;
			break;
		}
	} else {
		sc->bge_link = 0;
	}
	if (sc->bge_link == 0)
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

	m_new = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
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
	MGETHDR(m_new, init ? M_WAITOK : M_NOWAIT, MT_DATA);
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
	}

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
	}

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
	uint32_t dma_rw_ctl, mode_ctl;
	uint16_t val;

	/* Set endian type before we access any non-PCI registers. */
	pci_write_config(sc->bge_dev, BGE_PCI_MISC_CTL,
	    BGE_INIT | sc->bge_pci_miscctl, 4);

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

	if (sc->bge_chiprev == BGE_CHIPREV_5704_BX) {
		/*
		 * Fix data corruption caused by non-qword write with WB.
		 * Fix master abort in PCI mode.
		 * Fix PCI latency timer.
		 */
		val = pci_read_config(sc->bge_dev, BGE_PCI_MSI_DATA + 2, 2);
		val |= (1 << 10) | (1 << 12) | (1 << 13);
		pci_write_config(sc->bge_dev, BGE_PCI_MSI_DATA + 2, val, 2);
	}

	/* Set up the PCI DMA control register. */
	dma_rw_ctl = BGE_PCI_READ_CMD | BGE_PCI_WRITE_CMD;
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		/* PCI-E bus */
		/* DMA read watermark not used on PCI-E */
		dma_rw_ctl |= (0x3 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
	} else if (sc->bge_flags & BGE_FLAG_PCIX) {
		/* PCI-X bus */
		if (sc->bge_asicrev == BGE_ASICREV_BCM5780) {
			dma_rw_ctl |= (0x4 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (0x2 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
			dma_rw_ctl |= BGE_PCIDMARWCTL_ONEDMA_ATONCE_GLOBAL;
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5714) {
			dma_rw_ctl |= (0x4 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (0x2 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
			dma_rw_ctl |= BGE_PCIDMARWCTL_ONEDMA_ATONCE_LOCAL;
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			uint32_t rd_wat = 0x7;
			uint32_t clkctl;

			clkctl = CSR_READ_4(sc, BGE_PCI_CLKCTL) & 0x1f;
			if ((sc->bge_flags & BGE_FLAG_MAXADDR_40BIT) &&
			    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
				dma_rw_ctl |=
				    BGE_PCIDMARWCTL_ONEDMA_ATONCE_LOCAL;
			} else if (clkctl == 0x6 || clkctl == 0x7) {
				dma_rw_ctl |=
				    BGE_PCIDMARWCTL_ONEDMA_ATONCE_GLOBAL;
			}
			if (sc->bge_asicrev == BGE_ASICREV_BCM5703)
				rd_wat = 0x4;

			dma_rw_ctl |= (rd_wat << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (3 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
			dma_rw_ctl |= BGE_PCIDMARWCTL_ASRT_ALL_BE;
		} else {
			dma_rw_ctl |= (0x3 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
			    (0x3 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
			dma_rw_ctl |= 0xf;
		}
	} else {
		/* Conventional PCI bus */
		dma_rw_ctl |= (0x7 << BGE_PCIDMARWCTL_RD_WAT_SHIFT) |
		    (0x7 << BGE_PCIDMARWCTL_WR_WAT_SHIFT);
		if (sc->bge_asicrev != BGE_ASICREV_BCM5705 &&
		    sc->bge_asicrev != BGE_ASICREV_BCM5750)
			dma_rw_ctl |= 0xf;
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
		dma_rw_ctl &= ~BGE_PCIDMARWCTL_MINDMA;
	} else if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5701) {
		dma_rw_ctl |= BGE_PCIDMARWCTL_USE_MRM |
		    BGE_PCIDMARWCTL_ASRT_ALL_BE;
	}
	pci_write_config(sc->bge_dev, BGE_PCI_DMA_RW_CTL, dma_rw_ctl, 4);

	/*
	 * Set up general mode register.
	 */
	mode_ctl = BGE_DMA_SWAP_OPTIONS|
	    BGE_MODECTL_MAC_ATTN_INTR|BGE_MODECTL_HOST_SEND_BDS|
	    BGE_MODECTL_TX_NO_PHDR_CSUM;

	/*
	 * BCM5701 B5 have a bug causing data corruption when using
	 * 64-bit DMA reads, which can be terminated early and then
	 * completed later as 32-bit accesses, in combination with
	 * certain bridges.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5701 &&
	    sc->bge_chipid == BGE_CHIPID_BCM5701_B5)
		mode_ctl |= BGE_MODECTL_FORCE_PCI32;

	/*
	 * Tell the firmware the driver is running
	 */
	if (sc->bge_asf_mode & ASF_STACKUP)
		mode_ctl |= BGE_MODECTL_STACKUP;

	CSR_WRITE_4(sc, BGE_MODE_CTL, mode_ctl);

	/*
	 * Disable memory write invalidate.  Apparently it is not supported
	 * properly by these devices.  Also ensure that INTx isn't disabled,
	 * as these chips need it even when using MSI.
	 */
	PCI_CLRBIT(sc->bge_dev, BGE_PCI_CMD,
	    (PCIM_CMD_MWRICEN | PCIM_CMD_INTxDIS), 4);

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
	int i, limit;

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
	 *   buffer up to the stack.  Many versions of the controller
	 *   support multiple RR rings.
	 *
	 * Send Ring
	 * - This ring is used for outgoing frames.  Many versions of
	 *   the controller support multiple send rings.
	 */

	/* Initialize the standard receive producer ring control block. */
	rcb = &sc->bge_ldata.bge_info.bge_std_rx_rcb;
	rcb->bge_hostaddr.bge_addr_lo =
	    BGE_ADDR_LO(sc->bge_ldata.bge_rx_std_ring_paddr);
	rcb->bge_hostaddr.bge_addr_hi =
	    BGE_ADDR_HI(sc->bge_ldata.bge_rx_std_ring_paddr);
	if (BGE_IS_5705_PLUS(sc)) {
		/*
		 * Bits 31-16: Programmable ring size (512, 256, 128, 64, 32)
		 * Bits 15-2 : Reserved (should be 0)
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring Enabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(512, 0);
	} else {
		/*
		 * Ring size is always XXX entries
		 * Bits 31-16: Maximum RX frame size
		 * Bits 15-2 : Reserved (should be 0)
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring Enabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(BGE_MAX_FRAMELEN, 0);
	}
	rcb->bge_nicaddr = BGE_STD_RX_RINGS;
	/* Write the standard receive producer ring control block. */
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);
	/* Reset the standard receive producer ring producer index. */
	bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, 0);

	/*
	 * Initialize the jumbo RX producer ring control
	 * block.  We set the 'ring disabled' bit in the
	 * flags field until we're actually ready to start
	 * using this ring (i.e. once we set the MTU
	 * high enough to require it).
	 */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		rcb = &sc->bge_ldata.bge_info.bge_jumbo_rx_rcb;
		/* Get the jumbo receive producer ring RCB parameters. */
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
		/* Program the jumbo receive producer ring RCB parameters. */
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_NICADDR, rcb->bge_nicaddr);
		/* Reset the jumbo receive producer ring producer index. */
		bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, 0);
	}

	/* Disable the mini receive producer ring RCB. */
	if (BGE_IS_5700_FAMILY(sc)) {
		rcb = &sc->bge_ldata.bge_info.bge_mini_rx_rcb;
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED);
		CSR_WRITE_4(sc, BGE_RX_MINI_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		/* Reset the mini receive producer ring producer index. */
		bge_writembx(sc, BGE_MBX_RX_MINI_PROD_LO, 0);
	}

	/* Choose de-pipeline mode for BCM5906 A0, A1 and A2. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    (sc->bge_chipid == BGE_CHIPID_BCM5906_A0 ||
	     sc->bge_chipid == BGE_CHIPID_BCM5906_A1 ||
	     sc->bge_chipid == BGE_CHIPID_BCM5906_A2)) {
		CSR_WRITE_4(sc, BGE_ISO_PKT_TX,
		    (CSR_READ_4(sc, BGE_ISO_PKT_TX) & ~3) | 2);
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
	if (BGE_IS_5705_PLUS(sc))
		val = 8;
	else
		val = BGE_STD_RX_RING_CNT / 8;
	CSR_WRITE_4(sc, BGE_RBDI_STD_REPL_THRESH, val);
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		CSR_WRITE_4(sc, BGE_RBDI_JUMBO_REPL_THRESH,
		    BGE_JUMBO_RX_RING_CNT/8);
	}

	/*
	 * Disable all send rings by setting the 'ring disabled' bit
	 * in the flags field of all the TX send ring control blocks,
	 * located in NIC memory.
	 */
	if (!BGE_IS_5705_PLUS(sc)) {
		/* 5700 to 5704 had 16 send rings. */
		limit = BGE_TX_RINGS_EXTSSRAM_MAX;
	} else {
		limit = 1;
	}
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED));
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/* Configure send ring RCB 0 (we use only the first ring) */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_tx_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr,
	    BGE_NIC_TXRING_ADDR(0, BGE_TX_RING_CNT));
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(BGE_TX_RING_CNT, 0));

	/*
	 * Disable all receive return rings by setting the
	 * 'ring diabled' bit in the flags field of all the receive
	 * return ring control blocks, located in NIC memory.
	 */
	if (!BGE_IS_5705_PLUS(sc))
		limit = BGE_RX_RINGS_MAX;
	else if (sc->bge_asicrev == BGE_ASICREV_BCM5755)
		limit = 4;
	else
		limit = 1;
	/* Disable all receive return rings. */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, 0);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, 0);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_FLAG_RING_DISABLED);
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		bge_writembx(sc, BGE_MBX_RX_CONS0_LO +
		    (i * (sizeof(uint64_t))), 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Set up receive return ring 0.  Note that the NIC address
	 * for RX return rings is 0x0.  The return rings live entirely
	 * within the host, so the nicaddr field in the RCB isn't used.
	 */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_rx_return_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(sc->bge_return_ring_cnt, 0));

	/* Set random backoff seed for TX */
	CSR_WRITE_4(sc, BGE_TX_RANDOM_BACKOFF,
	    (sc->arpcom.ac_enaddr[0] + sc->arpcom.ac_enaddr[1] +
	     sc->arpcom.ac_enaddr[2] + sc->arpcom.ac_enaddr[3] +
	     sc->arpcom.ac_enaddr[4] + sc->arpcom.ac_enaddr[5]) &
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
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, sc->bge_rx_coal_bds);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, sc->bge_tx_coal_bds);
	if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS_INT,
		    sc->bge_rx_coal_ticks_int);
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS_INT,
		    sc->bge_tx_coal_ticks_int);
	}
	/*
	 * NOTE:
	 * The datasheet (57XX-PG105-R) says BCM5705+ do not
	 * have following two registers; obviously it is wrong.
	 */
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT, sc->bge_rx_coal_bds_int);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT, sc->bge_tx_coal_bds_int);

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
	bzero(sc->bge_ldata.bge_status_block, BGE_STATUS_BLK_SZ);
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_HI,
	    BGE_ADDR_HI(sc->bge_ldata.bge_status_block_paddr));
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_LO,
	    BGE_ADDR_LO(sc->bge_ldata.bge_status_block_paddr));

	/*
	 * Set up status block partail update size.
	 *
	 * Because only single TX ring, RX produce ring and Rx return ring
	 * are used, ask device to update only minimum part of status block
	 * except for BCM5700 AX/BX, whose status block partial update size
	 * can't be configured.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_C0) {
		/* XXX Actually reserved on BCM5700 AX/BX */
		val = BGE_STATBLKSZ_FULL;
	} else {
		val = BGE_STATBLKSZ_32BYTE;
	}
#if 0
	/*
	 * Does not seem to have visible effect in both
	 * bulk data (1472B UDP datagram) and tiny data
	 * (18B UDP datagram) TX tests.
	 */
	if (!BGE_IS_CRIPPLED(sc))
		val |= BGE_HCCMODE_CLRTICK_TX;
#endif

	/* Turn on host coalescing state machine */
	CSR_WRITE_4(sc, BGE_HCC_MODE, val | BGE_HCCMODE_ENABLE);

	/* Turn on RX BD completion state machine and enable attentions */
	CSR_WRITE_4(sc, BGE_RBDC_MODE,
	    BGE_RBDCMODE_ENABLE|BGE_RBDCMODE_ATTN);

	/* Turn on RX list placement state machine */
	CSR_WRITE_4(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);

	/* Turn on RX list selector state machine. */
	if (!BGE_IS_5705_PLUS(sc))
		CSR_WRITE_4(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);

	val = BGE_MACMODE_TXDMA_ENB | BGE_MACMODE_RXDMA_ENB |
	    BGE_MACMODE_RX_STATS_CLEAR | BGE_MACMODE_TX_STATS_CLEAR |
	    BGE_MACMODE_RX_STATS_ENB | BGE_MACMODE_TX_STATS_ENB |
	    BGE_MACMODE_FRMHDR_DMA_ENB;

	if (sc->bge_flags & BGE_FLAG_TBI)
		val |= BGE_PORTMODE_TBI;
	else if (sc->bge_flags & BGE_FLAG_MII_SERDES)
		val |= BGE_PORTMODE_GMII;
	else
		val |= BGE_PORTMODE_MII;

	/* Allow APE to send/receive frames. */
	if (sc->bge_mfw_flags & BGE_MFW_ON_APE)
		val |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;

	/* Turn on DMA, clear stats */
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	/* Set misc. local control, enable interrupts on attentions */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

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
	if (BGE_IS_5755_PLUS(sc)) {
		/* Enable host coalescing bug fix. */
		val |= BGE_WDMAMODE_STATUS_TAG_FIX;
	}
	if (sc->bge_asicrev == BGE_ASICREV_BCM5785) {
		/* Request larger DMA burst size to get better performance. */
		val |= BGE_WDMAMODE_BURST_ALL_DATA;
	}
	CSR_WRITE_4(sc, BGE_WDMA_MODE, val);
	DELAY(40);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM57780) {
		/*
		 * Enable fix for read DMA FIFO overruns.
		 * The fix is to limit the number of RX BDs
		 * the hardware would fetch at a fime.
		 */
		val = CSR_READ_4(sc, BGE_RDMA_RSRVCTRL);
		CSR_WRITE_4(sc, BGE_RDMA_RSRVCTRL,
		    val| BGE_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
	}

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
	if (sc->bge_flags & BGE_FLAG_TSO)
		val |= BGE_RDMAMODE_TSO4_ENABLE;
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
	if (sc->bge_flags & BGE_FLAG_TSO)
		CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE |
		    BGE_SDIMODE_HW_LSO_PRE_DMA);
	else
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

	/*
	 * Enable attention when the link has changed state for
	 * devices that use auto polling.
	 */
	if (sc->bge_flags & BGE_FLAG_TBI) {
		CSR_WRITE_4(sc, BGE_MI_STS, BGE_MISTS_LINK);
 	} else {
		if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
			CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
			DELAY(80);
		}
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
	return(0);
}

static int
bge_attach(device_t dev)
{
	struct ifnet *ifp;
	struct bge_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint32_t hwcfg = 0, misccfg;
	int error = 0, rid, capmask;
	uint8_t ether_addr[ETHER_ADDR_LEN];
	uint16_t product, vendor;
	driver_intr_t *intr_func;
	uintptr_t mii_priv = 0;
	u_int intr_flags;
	int msi_enable;

	sc = device_get_softc(dev);
	sc->bge_dev = dev;
	callout_init_mp(&sc->bge_stat_timer);
	lwkt_serialize_init(&sc->bge_jslot_serializer);

	sc->bge_func_addr = pci_get_function(dev);
	product = pci_get_device(dev);
	vendor = pci_get_vendor(dev);

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
	if (BGE_ASICREV(sc->bge_chipid) == BGE_ASICREV_USE_PRODID_REG) {
		/* All chips, which use BGE_PCI_PRODID_ASICREV, have CPMU */
		sc->bge_flags |= BGE_FLAG_CPMU;
		sc->bge_chipid = pci_read_config(dev, BGE_PCI_PRODID_ASICREV, 4);
	}
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

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		sc->bge_flags |= BGE_FLAG_APE;

	misccfg = CSR_READ_4(sc, BGE_MISC_CFG) & BGE_MISCCFG_BOARD_ID_MASK;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	    (misccfg == BGE_MISCCFG_BOARD_ID_5788 ||
	     misccfg == BGE_MISCCFG_BOARD_ID_5788M))
		sc->bge_flags |= BGE_FLAG_5788;

	/* BCM5755 or higher and BCM5906 have short DMA bug. */
	if (BGE_IS_5755_PLUS(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5906)
		sc->bge_flags |= BGE_FLAG_SHORTDMA;

	/*
	 * Increase STD RX ring prod index by at most 8 for BCM5750,
	 * BCM5752 and BCM5755 to workaround hardware errata.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5750 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5752 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5755)
		sc->bge_rx_wreg = 8;

  	/*
	 * Check if this is a PCI-X or PCI Express device.
  	 */
	if (BGE_IS_5705_PLUS(sc)) {
		if (pci_is_pcie(dev)) {
			sc->bge_flags |= BGE_FLAG_PCIE;
			sc->bge_pciecap = pci_get_pciecap_ptr(sc->bge_dev);
			pcie_set_max_readrq(dev, PCIEM_DEVCTL_MAX_READRQ_4096);
		}
	} else {
		/*
		 * Check if the device is in PCI-X Mode.
		 * (This bit is not valid on PCI Express controllers.)
		 */
		if ((pci_read_config(sc->bge_dev, BGE_PCI_PCISTATE, 4) &
		    BGE_PCISTATE_PCI_BUSMODE) == 0) {
			sc->bge_flags |= BGE_FLAG_PCIX;
			sc->bge_pcixcap = pci_get_pcixcap_ptr(sc->bge_dev);
			sc->bge_mbox_reorder = device_getenv_int(sc->bge_dev,
			    "mbox_reorder", 0);
		}
 	}
	device_printf(dev, "CHIP ID 0x%08x; "
		      "ASIC REV 0x%02x; CHIP REV 0x%02x; %s\n",
		      sc->bge_chipid, sc->bge_asicrev, sc->bge_chiprev,
		      (sc->bge_flags & BGE_FLAG_PCIX) ? "PCI-X"
		      : ((sc->bge_flags & BGE_FLAG_PCIE) ?
			"PCI-E" : "PCI"));

	/*
	 * The 40bit DMA bug applies to the 5714/5715 controllers and is
	 * not actually a MAC controller bug but an issue with the embedded
	 * PCIe to PCI-X bridge in the device. Use 40bit DMA workaround.
	 */
	if ((sc->bge_flags & BGE_FLAG_PCIX) &&
	    (BGE_IS_5714_FAMILY(sc) || device_getenv_int(dev, "dma40b", 0)))
		sc->bge_flags |= BGE_FLAG_MAXADDR_40BIT;

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

	if (!BGE_IS_CRIPPLED(sc)) {
		if (device_getenv_int(dev, "status_tag", 1)) {
			sc->bge_flags |= BGE_FLAG_STATUS_TAG;
			sc->bge_pci_miscctl = BGE_PCIMISCCTL_TAGGED_STATUS;
			if (bootverbose)
				device_printf(dev, "enable status tag\n");
		}
	}

	if (BGE_IS_5755_PLUS(sc)) {
		/*
		 * BCM5754 and BCM5787 shares the same ASIC id so
		 * explicit device id check is required.
		 * Due to unknown reason TSO does not work on BCM5755M.
		 */
		if (product != PCI_PRODUCT_BROADCOM_BCM5754 &&
		    product != PCI_PRODUCT_BROADCOM_BCM5754M &&
		    product != PCI_PRODUCT_BROADCOM_BCM5755M)
			sc->bge_flags |= BGE_FLAG_TSO;
	}

	/*
	 * Set various PHY quirk flags.
	 */

	if ((sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	     sc->bge_asicrev == BGE_ASICREV_BCM5701) &&
	    pci_get_subvendor(dev) == PCI_VENDOR_DELL)
		mii_priv |= BRGPHY_FLAG_NO_3LED;

	capmask = MII_CAPMASK_DEFAULT;
	if ((sc->bge_asicrev == BGE_ASICREV_BCM5703 &&
	     (misccfg == 0x4000 || misccfg == 0x8000)) ||
	    (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	     vendor == PCI_VENDOR_BROADCOM &&
	     (product == PCI_PRODUCT_BROADCOM_BCM5901 ||
	      product == PCI_PRODUCT_BROADCOM_BCM5901A2 ||
	      product == PCI_PRODUCT_BROADCOM_BCM5705F)) ||
	    (vendor == PCI_VENDOR_BROADCOM &&
	     (product == PCI_PRODUCT_BROADCOM_BCM5751F ||
	      product == PCI_PRODUCT_BROADCOM_BCM5753F ||
	      product == PCI_PRODUCT_BROADCOM_BCM5787F)) ||
	    product == PCI_PRODUCT_BROADCOM_BCM57790 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		/* 10/100 only */
		capmask &= ~BMSR_EXTSTAT;
	}

	mii_priv |= BRGPHY_FLAG_WIRESPEED;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	     (sc->bge_chipid != BGE_CHIPID_BCM5705_A0 &&
	      sc->bge_chipid != BGE_CHIPID_BCM5705_A1)) ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mii_priv &= ~BRGPHY_FLAG_WIRESPEED;

	if (sc->bge_chipid == BGE_CHIPID_BCM5701_A0 ||
	    sc->bge_chipid == BGE_CHIPID_BCM5701_B0)
		mii_priv |= BRGPHY_FLAG_CRC_BUG;

	if (sc->bge_chiprev == BGE_CHIPREV_5703_AX ||
	    sc->bge_chiprev == BGE_CHIPREV_5704_AX)
		mii_priv |= BRGPHY_FLAG_ADC_BUG;

	if (sc->bge_chipid == BGE_CHIPID_BCM5704_A0)
		mii_priv |= BRGPHY_FLAG_5704_A0;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mii_priv |= BRGPHY_FLAG_5906;

	if (BGE_IS_5705_PLUS(sc) &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5906 &&
	    /* sc->bge_asicrev != BGE_ASICREV_BCM5717 && */
	    sc->bge_asicrev != BGE_ASICREV_BCM5785 &&
	    /* sc->bge_asicrev != BGE_ASICREV_BCM57765 && */
	    sc->bge_asicrev != BGE_ASICREV_BCM57780) {
		if (sc->bge_asicrev == BGE_ASICREV_BCM5755 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5787) {
			if (product != PCI_PRODUCT_BROADCOM_BCM5722 &&
			    product != PCI_PRODUCT_BROADCOM_BCM5756)
				mii_priv |= BRGPHY_FLAG_JITTER_BUG;
			if (product == PCI_PRODUCT_BROADCOM_BCM5755M)
				mii_priv |= BRGPHY_FLAG_ADJUST_TRIM;
		} else {
			mii_priv |= BRGPHY_FLAG_BER_BUG;
		}
	}

	/*
	 * Chips with APE need BAR2 access for APE registers/memory.
	 */
	if (sc->bge_flags & BGE_FLAG_APE) {
		uint32_t pcistate;

		rid = PCIR_BAR(2);
		sc->bge_res2 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (sc->bge_res2 == NULL) {
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

		bge_ape_lock_init(sc);
		bge_ape_read_fw_ver(sc);
	}

	/*
	 * Allocate interrupt
	 */
	msi_enable = bge_msi_enable;
	if ((sc->bge_flags & BGE_FLAG_STATUS_TAG) == 0) {
		/* If "tagged status" is disabled, don't enable MSI */
		msi_enable = 0;
	} else if (msi_enable) {
		msi_enable = 0; /* Disable by default */
		if (BGE_IS_575X_PLUS(sc)) {
			msi_enable = 1;
			/* XXX we filter all 5714 chips */
			if (sc->bge_asicrev == BGE_ASICREV_BCM5714 ||
			    (sc->bge_asicrev == BGE_ASICREV_BCM5750 &&
			     (sc->bge_chiprev == BGE_CHIPREV_5750_AX ||
			      sc->bge_chiprev == BGE_CHIPREV_5750_BX)))
				msi_enable = 0;
			else if (BGE_IS_5755_PLUS(sc) ||
			    sc->bge_asicrev == BGE_ASICREV_BCM5906)
				sc->bge_flags |= BGE_FLAG_ONESHOT_MSI;
		}
	}
	if (msi_enable) {
		if (pci_find_extcap(dev, PCIY_MSI, &sc->bge_msicap)) {
			device_printf(dev, "no MSI capability\n");
			msi_enable = 0;
		}
	}

	sc->bge_irq_type = pci_alloc_1intr(dev, msi_enable, &sc->bge_irq_rid,
	    &intr_flags);

	sc->bge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->bge_irq_rid,
	    intr_flags);
	if (sc->bge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	if (sc->bge_irq_type == PCI_INTR_TYPE_MSI)
		bge_enable_msi(sc);
	else
		sc->bge_flags &= ~BGE_FLAG_ONESHOT_MSI;

	/* Initialize if_name earlier, so if_printf could be used */
	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	sc->bge_asf_mode = 0;
	/* No ASF if APE present. */
	if ((sc->bge_flags & BGE_FLAG_APE) == 0) {
		if (bge_allow_asf && (bge_readmem_ind(sc, BGE_SRAM_DATA_SIG) ==
		    BGE_SRAM_DATA_SIG_MAGIC)) {
			if (bge_readmem_ind(sc, BGE_SRAM_DATA_CFG) &
			    BGE_HWCFG_ASF) {
				sc->bge_asf_mode |= ASF_ENABLE;
				sc->bge_asf_mode |= ASF_STACKUP;
				if (BGE_IS_575X_PLUS(sc))
					sc->bge_asf_mode |= ASF_NEW_HANDSHAKE;
			}
		}
	}

	/*
	 * Try to reset the chip.
	 */
	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_SHUTDOWN);
	bge_reset(sc);
	bge_sig_legacy(sc, BGE_RESET_SHUTDOWN);
	bge_sig_post_reset(sc, BGE_RESET_SHUTDOWN);

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
	sc->bge_rx_coal_ticks = BGE_RX_COAL_TICKS_DEF;
	sc->bge_tx_coal_ticks = BGE_TX_COAL_TICKS_DEF;
	sc->bge_rx_coal_bds = BGE_RX_COAL_BDS_DEF;
	sc->bge_tx_coal_bds = BGE_TX_COAL_BDS_DEF;
	if (sc->bge_flags & BGE_FLAG_STATUS_TAG) {
		sc->bge_rx_coal_ticks_int = BGE_RX_COAL_TICKS_DEF;
		sc->bge_tx_coal_ticks_int = BGE_TX_COAL_TICKS_DEF;
		sc->bge_rx_coal_bds_int = BGE_RX_COAL_BDS_DEF;
		sc->bge_tx_coal_bds_int = BGE_TX_COAL_BDS_DEF;
	} else {
		sc->bge_rx_coal_ticks_int = BGE_RX_COAL_TICKS_MIN;
		sc->bge_tx_coal_ticks_int = BGE_TX_COAL_TICKS_MIN;
		sc->bge_rx_coal_bds_int = BGE_RX_COAL_BDS_MIN;
		sc->bge_tx_coal_bds_int = BGE_TX_COAL_BDS_MIN;
	}
	sc->bge_tx_wreg = BGE_TX_WREG_NSEGS;

	/* Set up TX spare and reserved descriptor count */
	if (sc->bge_flags & BGE_FLAG_TSO) {
		sc->bge_txspare = BGE_NSEG_SPARE_TSO;
		sc->bge_txrsvd = BGE_NSEG_RSVD_TSO;
	} else {
		sc->bge_txspare = BGE_NSEG_SPARE;
		sc->bge_txrsvd = BGE_NSEG_RSVD;
	}

	/* Set up ifnet structure */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bge_ioctl;
	ifp->if_start = bge_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = bge_npoll;
#endif
	ifp->if_watchdog = bge_watchdog;
	ifp->if_init = bge_init;
	ifp->if_mtu = ETHERMTU;
	ifp->if_capabilities = IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	ifp->if_nmbclusters = BGE_STD_RX_RING_CNT;
	ifq_set_maxlen(&ifp->if_snd, BGE_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * 5700 B0 chips do not support checksumming correctly due
	 * to hardware bugs.
	 */
	if (sc->bge_chipid != BGE_CHIPID_BCM5700_B0) {
		ifp->if_capabilities |= IFCAP_HWCSUM;
		ifp->if_hwassist |= BGE_CSUM_FEATURES;
	}
	if (sc->bge_flags & BGE_FLAG_TSO) {
		ifp->if_capabilities |= IFCAP_TSO;
		ifp->if_hwassist |= CSUM_TSO;
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
	if (bge_readmem_ind(sc, BGE_SRAM_DATA_SIG) == BGE_SRAM_DATA_SIG_MAGIC) {
		hwcfg = bge_readmem_ind(sc, BGE_SRAM_DATA_CFG);
	} else {
		if (bge_read_eeprom(sc, (caddr_t)&hwcfg, BGE_EE_HWCFG_OFFSET,
				    sizeof(hwcfg))) {
			device_printf(dev, "failed to read EEPROM\n");
			error = ENXIO;
			goto fail;
		}
		hwcfg = ntohl(hwcfg);
	}

	/* The SysKonnect SK-9D41 is a 1000baseSX card. */
	if (pci_get_subvendor(dev) == PCI_PRODUCT_SCHNEIDERKOCH_SK_9D41 ||
	    (hwcfg & BGE_HWCFG_MEDIA) == BGE_MEDIA_FIBER) {
		if (BGE_IS_5714_FAMILY(sc))
			sc->bge_flags |= BGE_FLAG_MII_SERDES;
		else
			sc->bge_flags |= BGE_FLAG_TBI;
	}

	/* Setup MI MODE */
	if (sc->bge_flags & BGE_FLAG_CPMU)
		sc->bge_mi_mode = BGE_MIMODE_500KHZ_CONST;
	else
		sc->bge_mi_mode = BGE_MIMODE_BASE;
	if (BGE_IS_5700_FAMILY(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5705) {
		/* Enable auto polling for BCM570[0-5]. */
		sc->bge_mi_mode |= BGE_MIMODE_AUTOPOLL;
	}

	/* Setup link status update stuffs */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) {
		sc->bge_link_upd = bge_bcm5700_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_MI_INTERRUPT;
	} else if (sc->bge_flags & BGE_FLAG_TBI) {
		sc->bge_link_upd = bge_tbi_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_LINK_CHANGED;
	} else if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
		sc->bge_link_upd = bge_autopoll_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_LINK_CHANGED;
	} else {
		sc->bge_link_upd = bge_copper_link_upd;
		sc->bge_link_chg = BGE_MACSTAT_LINK_CHANGED;
	}

	/*
	 * Broadcom's own driver always assumes the internal
	 * PHY is at GMII address 1.  On some chips, the PHY responds
	 * to accesses at all addresses, which could cause us to
	 * bogusly attach the PHY 32 times at probe type.  Always
	 * restricting the lookup to address 1 is simpler than
	 * trying to figure out which chips revisions should be
	 * special-cased.
	 */
	sc->bge_phyno = 1;

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
		struct mii_probe_args mii_args;
		int tries;

		/*
		 * Do transceiver setup and tell the firmware the
		 * driver is down so we can try to get access the
		 * probe if ASF is running.  Retry a couple of times
		 * if we get a conflict with the ASF firmware accessing
		 * the PHY.
		 */
		tries = 0;
		BGE_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
again:
		bge_asf_driver_up(sc);

		mii_probe_args_init(&mii_args, bge_ifmedia_upd, bge_ifmedia_sts);
		mii_args.mii_probemask = 1 << sc->bge_phyno;
		mii_args.mii_capmask = capmask;
		mii_args.mii_privtag = MII_PRIVTAG_BRGPHY;
		mii_args.mii_priv = mii_priv;

		error = mii_probe(dev, &sc->bge_miibus, &mii_args);
		if (error) {
			if (tries++ < 4) {
				device_printf(sc->bge_dev, "Probe MII again\n");
				bge_miibus_writereg(sc->bge_dev,
				    sc->bge_phyno, MII_BMCR, BMCR_RESET);
				goto again;
			}
			device_printf(dev, "MII without any PHY!\n");
			goto fail;
		}

		/*
		 * Now tell the firmware we are going up after probing the PHY
		 */
		if (sc->bge_asf_mode & ASF_STACKUP)
			BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
	}

	ctx = device_get_sysctl_ctx(sc->bge_dev);
	tree = device_get_sysctl_tree(sc->bge_dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "rx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_rx_coal_ticks, "I",
			"Receive coalescing ticks (usec).");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_tx_coal_ticks, "I",
			"Transmit coalescing ticks (usec).");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "rx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_rx_coal_bds, "I",
			"Receive max coalesced BD count.");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bge_sysctl_tx_coal_bds, "I",
			"Transmit max coalesced BD count.");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_wreg", CTLFLAG_RW,
		       &sc->bge_tx_wreg, 0,
		       "# of segments before writing to hardware register");

	if (sc->bge_flags & BGE_FLAG_PCIE) {
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
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			       "force_defrag", CTLFLAG_RW,
			       &sc->bge_force_defrag, 0,
			       "Force defragment on TX path");
	}
	if (sc->bge_flags & BGE_FLAG_STATUS_TAG) {
		if (!BGE_IS_5705_PLUS(sc)) {
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			    "rx_coal_ticks_int", CTLTYPE_INT | CTLFLAG_RW,
			    sc, 0, bge_sysctl_rx_coal_ticks_int, "I",
			    "Receive coalescing ticks "
			    "during interrupt (usec).");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			    "tx_coal_ticks_int", CTLTYPE_INT | CTLFLAG_RW,
			    sc, 0, bge_sysctl_tx_coal_ticks_int, "I",
			    "Transmit coalescing ticks "
			    "during interrupt (usec).");
		}
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "rx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
		    sc, 0, bge_sysctl_rx_coal_bds_int, "I",
		    "Receive max coalesced BD count during interrupt.");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "tx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
		    sc, 0, bge_sysctl_tx_coal_bds_int, "I",
		    "Transmit max coalesced BD count during interrupt.");
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ether_addr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->bge_irq));

#ifdef IFPOLL_ENABLE
	/* Polling setup */
	ifpoll_compat_setup(&sc->bge_npoll, ctx, tree,
	    device_get_unit(dev), ifp->if_serializer);
#endif

	if (sc->bge_irq_type == PCI_INTR_TYPE_MSI) {
		if (sc->bge_flags & BGE_FLAG_ONESHOT_MSI) {
			intr_func = bge_msi_oneshot;
			if (bootverbose)
				device_printf(dev, "oneshot MSI\n");
		} else {
			intr_func = bge_msi;
		}
	} else if (sc->bge_flags & BGE_FLAG_STATUS_TAG) {
		intr_func = bge_intr_legacy;
	} else {
		intr_func = bge_intr_crippled;
	}
	error = bus_setup_intr(dev, sc->bge_irq, INTR_MPSAFE, intr_func, sc,
	    &sc->bge_intrhand, ifp->if_serializer);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

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
		bus_teardown_intr(dev, sc->bge_irq, sc->bge_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->bge_flags & BGE_FLAG_TBI)
		ifmedia_removeall(&sc->bge_ifmedia);
	if (sc->bge_miibus)
		device_delete_child(dev, sc->bge_miibus);
	bus_generic_detach(dev);

	if (sc->bge_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->bge_irq_rid,
		    sc->bge_irq);
	}
	if (sc->bge_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->bge_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    BGE_PCI_BAR0, sc->bge_res);
	}
	if (sc->bge_res2 != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    PCIR_BAR(2), sc->bge_res2);
	}

	bge_dma_free(sc);

	return 0;
}

static void
bge_reset(struct bge_softc *sc)
{
	device_t dev = sc->bge_dev;
	uint32_t cachesize, command, reset, mac_mode, mac_mode_mask;
	void (*write_op)(struct bge_softc *, uint32_t, uint32_t);
	int i, val = 0;

	mac_mode_mask = BGE_MACMODE_HALF_DUPLEX | BGE_MACMODE_PORTMODE;
	if (sc->bge_mfw_flags & BGE_MFW_ON_APE)
		mac_mode_mask |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;
	mac_mode = CSR_READ_4(sc, BGE_MAC_MODE) & mac_mode_mask;

	if (BGE_IS_575X_PLUS(sc) && !BGE_IS_5714_FAMILY(sc) &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5906) {
		if (sc->bge_flags & BGE_FLAG_PCIE)
			write_op = bge_writemem_direct;
		else
			write_op = bge_writemem_ind;
	} else {
		write_op = bge_writereg_ind;
	}

	if (sc->bge_asicrev != BGE_ASICREV_BCM5700 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5701) {
		CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_SET1);
		for (i = 0; i < 8000; i++) {
			if (CSR_READ_4(sc, BGE_NVRAM_SWARB) &
			    BGE_NVRAMSWARB_GNT1)
				break;
			DELAY(20);
		}
		if (i == 8000) {
			if (bootverbose) {
				if_printf(&sc->arpcom.ac_if,
				    "NVRAM lock timedout!\n");
			}
		}
	}
	/* Take APE lock when performing reset. */
	bge_ape_lock(sc, BGE_APE_LOCK_GRC);

	/* Save some important PCI state. */
	cachesize = pci_read_config(dev, BGE_PCI_CACHESZ, 4);
	command = pci_read_config(dev, BGE_PCI_CMD, 4);

	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW|
	    sc->bge_pci_miscctl, 4);

	/* Disable fastboot on controllers that support it. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5752 ||
	    BGE_IS_5755_PLUS(sc)) {
		if (bootverbose)
			if_printf(&sc->arpcom.ac_if, "Disabling fastboot\n");
		CSR_WRITE_4(sc, BGE_FASTBOOT_PC, 0x0);
	}

	/*
	 * Write the magic number to SRAM at offset 0xB50.
	 * When firmware finishes its initialization it will
	 * write ~BGE_SRAM_FW_MB_MAGIC to the same location.
	 */
	bge_writemem_ind(sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);

	reset = BGE_MISCCFG_RESET_CORE_CLOCKS|(65<<1);

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		/* Force PCI-E 1.0a mode */
		if (sc->bge_asicrev != BGE_ASICREV_BCM5785 &&
		    CSR_READ_4(sc, BGE_PCIE_PHY_TSTCTL) ==
		    (BGE_PCIE_PHY_TSTCTL_PSCRAM |
		     BGE_PCIE_PHY_TSTCTL_PCIE10)) {
			CSR_WRITE_4(sc, BGE_PCIE_PHY_TSTCTL,
			    BGE_PCIE_PHY_TSTCTL_PSCRAM);
		}
		if (sc->bge_chipid != BGE_CHIPID_BCM5750_A0) {
			/* Prevent PCIE link training during global reset */
			CSR_WRITE_4(sc, BGE_MISC_CFG, (1<<29));
			reset |= (1<<29);
		}
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		uint32_t status, ctrl;

		status = CSR_READ_4(sc, BGE_VCPU_STATUS);
		CSR_WRITE_4(sc, BGE_VCPU_STATUS,
		    status | BGE_VCPU_STATUS_DRV_RESET);
		ctrl = CSR_READ_4(sc, BGE_VCPU_EXT_CTRL);
		CSR_WRITE_4(sc, BGE_VCPU_EXT_CTRL,
		    ctrl & ~BGE_VCPU_EXT_CTRL_HALT_CPU);
	}

	/* 
	 * Set GPHY Power Down Override to leave GPHY
	 * powered up in D0 uninitialized.
	 */
	if (BGE_IS_5705_PLUS(sc) && (sc->bge_flags & BGE_FLAG_CPMU) == 0)
		reset |= BGE_MISCCFG_GPHY_PD_OVERRIDE;

	/* Issue global reset */
	write_op(sc, BGE_MISC_CFG, reset);

	if (sc->bge_flags & BGE_FLAG_PCIE)
		DELAY(100 * 1000);
	else
		DELAY(1000);

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		uint16_t devctl;

		if (sc->bge_chipid == BGE_CHIPID_BCM5750_A0) {
			uint32_t v;

			DELAY(500000); /* wait for link training to complete */
			v = pci_read_config(dev, 0xc4, 4);
			pci_write_config(dev, 0xc4, v | (1<<15), 4);
		}

		devctl = pci_read_config(dev,
		    sc->bge_pciecap + PCIER_DEVCTRL, 2);

		/* Disable no snoop and disable relaxed ordering. */
		devctl &= ~(PCIEM_DEVCTL_RELAX_ORDER | PCIEM_DEVCTL_NOSNOOP);

		/* Old PCI-E chips only support 128 bytes Max PayLoad Size. */
		if ((sc->bge_flags & BGE_FLAG_CPMU) == 0) {
			devctl &= ~PCIEM_DEVCTL_MAX_PAYLOAD_MASK;
			devctl |= PCIEM_DEVCTL_MAX_PAYLOAD_128;
		}

		pci_write_config(dev, sc->bge_pciecap + PCIER_DEVCTRL,
		    devctl, 2);

		/* Clear error status. */
		pci_write_config(dev, sc->bge_pciecap + PCIER_DEVSTS,
		    PCIEM_DEVSTS_CORR_ERR |
		    PCIEM_DEVSTS_NFATAL_ERR |
		    PCIEM_DEVSTS_FATAL_ERR |
		    PCIEM_DEVSTS_UNSUPP_REQ, 2);
	}

	/* Reset some of the PCI state that got zapped by reset */
	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
	    BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW|
	    sc->bge_pci_miscctl, 4);
	val = BGE_PCISTATE_ROM_ENABLE | BGE_PCISTATE_ROM_RETRY_ENABLE;
	if (sc->bge_chipid == BGE_CHIPID_BCM5704_A0 &&
	    (sc->bge_flags & BGE_FLAG_PCIX))
		val |= BGE_PCISTATE_RETRY_SAME_DMA;
	if (sc->bge_mfw_flags & BGE_MFW_ON_APE) {
		val |= BGE_PCISTATE_ALLOW_APE_CTLSPC_WR |
		    BGE_PCISTATE_ALLOW_APE_SHMEM_WR |
		    BGE_PCISTATE_ALLOW_APE_PSPACE_WR;
	}
	pci_write_config(dev, BGE_PCI_PCISTATE, val, 4);
	pci_write_config(dev, BGE_PCI_CACHESZ, cachesize, 4);
	pci_write_config(dev, BGE_PCI_CMD, command, 4);

	/*
	 * Disable PCI-X relaxed ordering to ensure status block update
	 * comes first then packet buffer DMA. Otherwise driver may
	 * read stale status block.
	 */
	if (sc->bge_flags & BGE_FLAG_PCIX) {
		uint16_t devctl;

		devctl = pci_read_config(dev,
		    sc->bge_pcixcap + PCIXR_COMMAND, 2);
		devctl &= ~PCIXM_COMMAND_ERO;
		if (sc->bge_asicrev == BGE_ASICREV_BCM5703) {
			devctl &= ~PCIXM_COMMAND_MAX_READ;
			devctl |= PCIXM_COMMAND_MAX_READ_2048;
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			devctl &= ~(PCIXM_COMMAND_MAX_SPLITS |
			    PCIXM_COMMAND_MAX_READ);
			devctl |= PCIXM_COMMAND_MAX_READ_2048;
		}
		pci_write_config(dev, sc->bge_pcixcap + PCIXR_COMMAND,
		    devctl, 2);
	}

	/*
	 * Enable memory arbiter and re-enable MSI if necessary.
	 */
	if (BGE_IS_5714_FAMILY(sc)) {
		uint32_t val;

		if (sc->bge_irq_type == PCI_INTR_TYPE_MSI) {
			/*
			 * Resetting BCM5714 family will clear MSI
			 * enable bit; restore it after resetting.
			 */
			PCI_SETBIT(sc->bge_dev, sc->bge_msicap + PCIR_MSI_CTRL,
			    PCIM_MSICTRL_MSI_ENABLE, 2);
			BGE_SETBIT(sc, BGE_MSI_MODE, BGE_MSIMODE_ENABLE);
		}
		val = CSR_READ_4(sc, BGE_MARB_MODE);
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE | val);
	} else {
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
	}

	/* Fix up byte swapping. */
	CSR_WRITE_4(sc, BGE_MODE_CTL, BGE_DMA_SWAP_OPTIONS |
	    BGE_MODECTL_BYTESWAP_DATA);

	val = CSR_READ_4(sc, BGE_MAC_MODE);
	val = (val & ~mac_mode_mask) | mac_mode;
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	bge_ape_unlock(sc, BGE_APE_LOCK_GRC);

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
		int delay_us = 10;

		if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
			delay_us = 100;

		/*
		 * Poll until we see the 1's complement of the magic number.
		 * This indicates that the firmware initialization
		 * is complete.
		 */
		for (i = 0; i < BGE_FIRMWARE_TIMEOUT; i++) {
			val = bge_readmem_ind(sc, BGE_SRAM_FW_MB);
			if (val == ~BGE_SRAM_FW_MB_MAGIC)
				break;
			DELAY(delay_us);
		}
		if (i == BGE_FIRMWARE_TIMEOUT) {
			if_printf(&sc->arpcom.ac_if, "firmware handshake "
				  "timed out, found 0x%08x\n", val);
		}
	}

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
	    sc->bge_chipid != BGE_CHIPID_BCM5750_A0 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5785) {
		uint32_t v;

		/* Enable Data FIFO protection. */
		v = CSR_READ_4(sc, BGE_PCIE_TLDLPL_PORT);
		CSR_WRITE_4(sc, BGE_PCIE_TLDLPL_PORT, v | (1 << 25));
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
bge_rxeof(struct bge_softc *sc, uint16_t rx_prod, int count)
{
	struct ifnet *ifp;
	int stdcnt = 0, jumbocnt = 0;

	ifp = &sc->arpcom.ac_if;

	while (sc->bge_rx_saved_considx != rx_prod && count != 0) {
		struct bge_rx_bd	*cur_rx;
		uint32_t		rxidx;
		struct mbuf		*m = NULL;
		uint16_t		vlan_tag = 0;
		int			have_tag = 0;

		--count;

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
				IFNET_STAT_INC(ifp, ierrors, 1);
				if_printf(ifp, "sw jumbo index(%d) "
				    "and hw jumbo index(%d) mismatch, drop!\n",
				    sc->bge_jumbo, rxidx);
				bge_setup_rxdesc_jumbo(sc, rxidx);
				continue;
			}

			m = sc->bge_cdata.bge_rx_jumbo_chain[rxidx].bge_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				bge_setup_rxdesc_jumbo(sc, sc->bge_jumbo);
				continue;
			}
			if (bge_newbuf_jumbo(sc, sc->bge_jumbo, 0)) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				bge_setup_rxdesc_jumbo(sc, sc->bge_jumbo);
				continue;
			}
		} else {
			int discard = 0;

			BGE_INC(sc->bge_std, BGE_STD_RX_RING_CNT);
			stdcnt++;

			if (rxidx != sc->bge_std) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				if_printf(ifp, "sw std index(%d) "
				    "and hw std index(%d) mismatch, drop!\n",
				    sc->bge_std, rxidx);
				bge_setup_rxdesc_std(sc, rxidx);
				discard = 1;
				goto refresh_rx;
			}

			m = sc->bge_cdata.bge_rx_std_chain[rxidx].bge_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				bge_setup_rxdesc_std(sc, sc->bge_std);
				discard = 1;
				goto refresh_rx;
			}
			if (bge_newbuf_std(sc, sc->bge_std, 0)) {
				IFNET_STAT_INC(ifp, ierrors, 1);
				bge_setup_rxdesc_std(sc, sc->bge_std);
				discard = 1;
			}
refresh_rx:
			if (sc->bge_rx_wreg > 0 && stdcnt >= sc->bge_rx_wreg) {
				bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO,
				    sc->bge_std);
				stdcnt = 0;
			}
			if (discard)
				continue;
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
#if !defined(__x86_64__)
		/*
		 * The x86 allows unaligned accesses, but for other
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
			    m->m_pkthdr.len >= BGE_MIN_FRAMELEN) {
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
		}
		ifp->if_input(ifp, m, NULL, -1);
	}

	bge_writembx(sc, BGE_MBX_RX_CONS0_LO, sc->bge_rx_saved_considx);
	if (stdcnt)
		bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, sc->bge_std);
	if (jumbocnt)
		bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, sc->bge_jumbo);
}

static void
bge_txeof(struct bge_softc *sc, uint16_t tx_cons)
{
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->bge_tx_saved_considx != tx_cons) {
		uint32_t idx = 0;

		idx = sc->bge_tx_saved_considx;
		if (sc->bge_cdata.bge_tx_chain[idx] != NULL) {
			IFNET_STAT_INC(ifp, opackets, 1);
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[idx]);
			m_freem(sc->bge_cdata.bge_tx_chain[idx]);
			sc->bge_cdata.bge_tx_chain[idx] = NULL;
		}
		sc->bge_txcnt--;
		BGE_INC(sc->bge_tx_saved_considx, BGE_TX_RING_CNT);
		logif(tx_pkt);
	}

	if ((BGE_TX_RING_CNT - sc->bge_txcnt) >=
	    (sc->bge_txrsvd + sc->bge_txspare))
		ifq_clr_oactive(&ifp->if_snd);

	if (sc->bge_txcnt == 0)
		ifp->if_timer = 0;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

#ifdef IFPOLL_ENABLE

static void
bge_npoll_compat(struct ifnet *ifp, void *arg __unused, int cycles)
{
	struct bge_softc *sc = ifp->if_softc;
	struct bge_status_block *sblk = sc->bge_ldata.bge_status_block;
	uint16_t rx_prod, tx_cons;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->bge_npoll.ifpc_stcount-- == 0) {
		sc->bge_npoll.ifpc_stcount = sc->bge_npoll.ifpc_stfrac;
		/*
		 * Process link state changes.
		 */
		bge_link_poll(sc);
	}

	if (sc->bge_flags & BGE_FLAG_STATUS_TAG) {
		sc->bge_status_tag = sblk->bge_status_tag;
		/*
		 * Use a load fence to ensure that status_tag
		 * is saved  before rx_prod and tx_cons.
		 */
		cpu_lfence();
	}

	rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
	if (sc->bge_rx_saved_considx != rx_prod)
		bge_rxeof(sc, rx_prod, cycles);

	tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
	if (sc->bge_tx_saved_considx != tx_cons)
		bge_txeof(sc, tx_cons);

	if (sc->bge_flags & BGE_FLAG_STATUS_TAG)
		bge_writembx(sc, BGE_MBX_IRQ0_LO, sc->bge_status_tag << 24);

	if (sc->bge_coal_chg)
		bge_coal_change(sc);
}

static void
bge_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct bge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->bge_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = bge_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING)
			bge_disable_intr(sc);
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			bge_enable_intr(sc);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->bge_irq));
	}
}

#endif	/* IFPOLL_ENABLE */

static void
bge_intr_crippled(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

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
	bge_link_poll(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		struct bge_status_block *sblk = sc->bge_ldata.bge_status_block;
		uint16_t rx_prod, tx_cons;

		rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
		if (sc->bge_rx_saved_considx != rx_prod)
			bge_rxeof(sc, rx_prod, -1);

		tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
		if (sc->bge_tx_saved_considx != tx_cons)
			bge_txeof(sc, tx_cons);
	}

	if (sc->bge_coal_chg)
		bge_coal_change(sc);
}

static void
bge_intr_legacy(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct bge_status_block *sblk = sc->bge_ldata.bge_status_block;

	if (sc->bge_status_tag == sblk->bge_status_tag) {
		uint32_t val;

		val = pci_read_config(sc->bge_dev, BGE_PCI_PCISTATE, 4);
		if (val & BGE_PCISTAT_INTR_NOTACT)
			return;
	}

	/*
	 * NOTE:
	 * Interrupt will have to be disabled if tagged status
	 * is used, else interrupt will always be asserted on
	 * certain chips (at least on BCM5750 AX/BX).
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	bge_intr(sc);
}

static void
bge_msi(void *xsc)
{
	struct bge_softc *sc = xsc;

	/* Disable interrupt first */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);
	bge_intr(sc);
}

static void
bge_msi_oneshot(void *xsc)
{
	bge_intr(xsc);
}

static void
bge_intr(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_status_block *sblk = sc->bge_ldata.bge_status_block;
	uint16_t rx_prod, tx_cons;
	uint32_t status;

	sc->bge_status_tag = sblk->bge_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved 
	 * before rx_prod, tx_cons and status.
	 */
	cpu_lfence();

	rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
	status = sblk->bge_status;

	if ((status & BGE_STATFLAG_LINKSTATE_CHANGED) || sc->bge_link_evt)
		bge_link_poll(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		if (sc->bge_rx_saved_considx != rx_prod)
			bge_rxeof(sc, rx_prod, -1);

		if (sc->bge_tx_saved_considx != tx_cons)
			bge_txeof(sc, tx_cons);
	}

	bge_writembx(sc, BGE_MBX_IRQ0_LO, sc->bge_status_tag << 24);

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
		if (BGE_IS_CRIPPLED(sc))
			BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
		else
			BGE_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
	} else if (!sc->bge_link) {
		mii_tick(device_get_softc(sc->bge_miibus));
	}

	bge_asf_driver_up(sc);

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

	IFNET_STAT_SET(ifp, collisions,
	   (stats.dot3StatsSingleCollisionFrames +
	   stats.dot3StatsMultipleCollisionFrames +
	   stats.dot3StatsExcessiveCollisions +
	   stats.dot3StatsLateCollisions));
}

static void
bge_stats_update(struct bge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	bus_size_t stats;

	stats = BGE_MEMWIN_START + BGE_STATS_BLOCK;

#define READ_STAT(sc, stats, stat)	\
	CSR_READ_4(sc, stats + offsetof(struct bge_stats, stat))

	IFNET_STAT_SET(ifp, collisions,
	   (READ_STAT(sc, stats,
		txstats.dot3StatsSingleCollisionFrames.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsMultipleCollisionFrames.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsExcessiveCollisions.bge_addr_lo) +
	    READ_STAT(sc, stats,
		txstats.dot3StatsLateCollisions.bge_addr_lo)));

#undef READ_STAT

#ifdef notdef
	IFNET_STAT_SET(ifp, collisions,
	   (sc->bge_rdata->bge_info.bge_stats.dot3StatsSingleCollisionFrames +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsMultipleCollisionFrames +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsExcessiveCollisions +
	   sc->bge_rdata->bge_info.bge_stats.dot3StatsLateCollisions));
#endif
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
bge_encap(struct bge_softc *sc, struct mbuf **m_head0, uint32_t *txidx,
    int *segs_used)
{
	struct bge_tx_bd *d = NULL, *last_d;
	uint16_t csum_flags = 0, mss = 0;
	bus_dma_segment_t segs[BGE_NSEG_NEW];
	bus_dmamap_t map;
	int error, maxsegs, nsegs, idx, i;
	struct mbuf *m_head = *m_head0, *m_new;

	if (m_head->m_pkthdr.csum_flags & CSUM_TSO) {
		error = bge_setup_tso(sc, m_head0, &mss, &csum_flags);
		if (error)
			return ENOBUFS;
		m_head = *m_head0;
	} else if (m_head->m_pkthdr.csum_flags & BGE_CSUM_FEATURES) {
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

	maxsegs = (BGE_TX_RING_CNT - sc->bge_txcnt) - sc->bge_txrsvd;
	KASSERT(maxsegs >= sc->bge_txspare,
		("not enough segments %d", maxsegs));

	if (maxsegs > BGE_NSEG_NEW)
		maxsegs = BGE_NSEG_NEW;

	/*
	 * Pad outbound frame to BGE_MIN_FRAMELEN for an unusual reason.
	 * The bge hardware will pad out Tx runts to BGE_MIN_FRAMELEN,
	 * but when such padded frames employ the bge IP/TCP checksum
	 * offload, the hardware checksum assist gives incorrect results
	 * (possibly from incorporating its own padding into the UDP/TCP
	 * checksum; who knows).  If we pad such runts with zeros, the
	 * onboard checksum comes out correct.
	 */
	if ((csum_flags & BGE_TXBDFLAG_TCP_UDP_CSUM) &&
	    m_head->m_pkthdr.len < BGE_MIN_FRAMELEN) {
		error = m_devpad(m_head, BGE_MIN_FRAMELEN);
		if (error)
			goto back;
	}

	if ((sc->bge_flags & BGE_FLAG_SHORTDMA) && m_head->m_next != NULL) {
		m_new = bge_defrag_shortdma(m_head);
		if (m_new == NULL) {
			error = ENOBUFS;
			goto back;
		}
		*m_head0 = m_head = m_new;
	}
	if ((m_head->m_pkthdr.csum_flags & CSUM_TSO) == 0 &&
	    sc->bge_force_defrag && (sc->bge_flags & BGE_FLAG_PCIE) &&
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

	error = bus_dmamap_load_mbuf_defrag(sc->bge_cdata.bge_tx_mtag, map,
			m_head0, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;
	*segs_used += nsegs;

	m_head = *m_head0;
	bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	for (i = 0; ; i++) {
		d = &sc->bge_ldata.bge_tx_ring[idx];

		d->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[i].ds_addr);
		d->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[i].ds_addr);
		d->bge_len = segs[i].ds_len;
		d->bge_flags = csum_flags;
		d->bge_mss = mss;

		if (i == nsegs - 1)
			break;
		BGE_INC(idx, BGE_TX_RING_CNT);
	}
	last_d = d;

	/* Set vlan tag to the first segment of the packet. */
	d = &sc->bge_ldata.bge_tx_ring[*txidx];
	if (m_head->m_flags & M_VLANTAG) {
		d->bge_flags |= BGE_TXBDFLAG_VLAN_TAG;
		d->bge_vlan_tag = m_head->m_pkthdr.ether_vlantag;
	} else {
		d->bge_vlan_tag = 0;
	}

	/* Mark the last segment as end of packet... */
	last_d->bge_flags |= BGE_TXBDFLAG_END;

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

static void
bge_xmit(struct bge_softc *sc, uint32_t prodidx)
{
	/* Transmit */
	bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);
	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void
bge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct bge_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	uint32_t prodidx;
	int nsegs = 0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	prodidx = sc->bge_tx_prodidx;

	while (sc->bge_cdata.bge_tx_chain[prodidx] == NULL) {
		m_head = ifq_dequeue(&ifp->if_snd);
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
			    m_head->m_pkthdr.csum_data + sc->bge_txrsvd) {
				ifq_set_oactive(&ifp->if_snd);
				ifq_prepend(&ifp->if_snd, m_head);
				break;
			}
		}

		/*
		 * Sanity check: avoid coming within bge_txrsvd
		 * descriptors of the end of the ring.  Also make
		 * sure there are bge_txspare descriptors for
		 * jumbo buffers' defragmentation.
		 */
		if ((BGE_TX_RING_CNT - sc->bge_txcnt) <
		    (sc->bge_txrsvd + sc->bge_txspare)) {
			ifq_set_oactive(&ifp->if_snd);
			ifq_prepend(&ifp->if_snd, m_head);
			break;
		}

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (bge_encap(sc, &m_head, &prodidx, &nsegs)) {
			ifq_set_oactive(&ifp->if_snd);
			IFNET_STAT_INC(ifp, oerrors, 1);
			break;
		}

		if (nsegs >= sc->bge_tx_wreg) {
			bge_xmit(sc, prodidx);
			nsegs = 0;
		}

		ETHER_BPF_MTAP(ifp, m_head);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		ifp->if_timer = 5;
	}

	if (nsegs > 0)
		bge_xmit(sc, prodidx);
	sc->bge_tx_prodidx = prodidx;
}

static void
bge_init(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t *m;
	uint32_t mode;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/* Cancel pending I/O and flush buffers. */
	bge_stop(sc);

	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_START);
	bge_reset(sc);
	bge_sig_legacy(sc, BGE_RESET_START);
	bge_sig_post_reset(sc, BGE_RESET_START);

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

	/* Enable TX MAC state machine lockup fix. */
	mode = CSR_READ_4(sc, BGE_TX_MODE);
	if (BGE_IS_5755_PLUS(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mode |= BGE_TXMODE_MBUF_LOCKUP_FIX;
	/* Turn on transmitter */
	CSR_WRITE_4(sc, BGE_TX_MODE, mode | BGE_TXMODE_ENABLE);
	DELAY(100);

	/* Turn on receiver */
	mode = CSR_READ_4(sc, BGE_RX_MODE);
	if (BGE_IS_5755_PLUS(sc))
		mode |= BGE_RXMODE_IPV6_ENABLE;
	CSR_WRITE_4(sc, BGE_RX_MODE, mode | BGE_RXMODE_ENABLE);
	DELAY(10);

	/*
	 * Set the number of good frames to receive after RX MBUF
	 * Low Watermark has been reached.  After the RX MAC receives
	 * this number of frames, it will drop subsequent incoming
	 * frames until the MBUF High Watermark is reached.
	 */
	CSR_WRITE_4(sc, BGE_MAX_RX_FRAME_LOWAT, 2);

	if (sc->bge_irq_type == PCI_INTR_TYPE_MSI) {
		if (bootverbose) {
			if_printf(ifp, "MSI_MODE: %#x\n",
			    CSR_READ_4(sc, BGE_MSI_MODE));
		}

		/*
		 * XXX
		 * Linux driver turns it on for all chips supporting MSI?!
		 */
		if (sc->bge_flags & BGE_FLAG_ONESHOT_MSI) {
			/*
			 * XXX
			 * According to 5722-PG101-R,
			 * BGE_PCIE_TRANSACT_ONESHOT_MSI applies only to
			 * BCM5906.
			 */
			BGE_SETBIT(sc, BGE_PCIE_TRANSACT,
			    BGE_PCIE_TRANSACT_ONESHOT_MSI);
		}
	}

	/* Tell firmware we're alive. */
	BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Enable host interrupts if polling(4) is not enabled. */
	PCI_SETBIT(sc->bge_dev, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_CLEAR_INTA, 4);
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		bge_disable_intr(sc);
	else
#endif
	bge_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	bge_ifmedia_upd(ifp);

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
			DELAY(40);
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

		/*
		 * Force an interrupt so that we will call bge_link_upd
		 * if needed and clear any pending link state attention.
		 * Without this we are not getting any further interrupts
		 * for link state changes and thus will not UP the link and
		 * not be able to send in bge_start.  The only way to get
		 * things working was to receive a packet and get an RX
		 * intr.
		 *
		 * bge_tick should help for fiber cards and we might not
		 * need to do this here if BGE_FLAG_TBI is set but as
		 * we poll for fiber anyway it should not harm.
		 */
		if (BGE_IS_CRIPPLED(sc))
			BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
		else
			BGE_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
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

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

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
			if (ifp->if_flags & IFF_RUNNING)
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
		} else if (ifp->if_flags & IFF_RUNNING) {
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
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= BGE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~BGE_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= IFCAP_TSO;
			if (ifp->if_capenable & IFCAP_TSO)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
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

	bge_init(sc);

	IFNET_STAT_INC(ifp, oerrors, 1);

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

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->bge_stat_timer);

	/* Disable host interrupts. */
	bge_disable_intr(sc);

	/*
	 * Tell firmware we're shutting down.
	 */
	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_SHUTDOWN);

	/*
	 * Disable all of the receiver blocks
	 */
	bge_stop_block(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);
	bge_stop_block(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);
	bge_stop_block(sc, BGE_RDBDI_MODE, BGE_RBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);
	bge_stop_block(sc, BGE_RBDC_MODE, BGE_RBDCMODE_ENABLE);

	/*
	 * Disable all of the transmit blocks
	 */
	bge_stop_block(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);
	bge_stop_block(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RDMA_MODE, BGE_RDMAMODE_ENABLE);
	bge_stop_block(sc, BGE_SDC_MODE, BGE_SDCMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_DMAC_MODE, BGE_DMACMODE_ENABLE);
	bge_stop_block(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/*
	 * Shut down all of the memory managers and related
	 * state machines.
	 */
	bge_stop_block(sc, BGE_HCC_MODE, BGE_HCCMODE_ENABLE);
	bge_stop_block(sc, BGE_WDMA_MODE, BGE_WDMAMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_MBCF_MODE, BGE_MBCFMODE_ENABLE);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);
	if (!BGE_IS_5705_PLUS(sc)) {
		BGE_CLRBIT(sc, BGE_BMAN_MODE, BGE_BMANMODE_ENABLE);
		BGE_CLRBIT(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
	}

	bge_reset(sc);
	bge_sig_legacy(sc, BGE_RESET_SHUTDOWN);
	bge_sig_post_reset(sc, BGE_RESET_SHUTDOWN);

	/*
	 * Keep the ASF firmware running if up.
	 */
	if (sc->bge_asf_mode & ASF_STACKUP)
		BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
	else
		BGE_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Free the RX lists. */
	bge_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	if (BGE_IS_JUMBO_CAPABLE(sc))
		bge_free_rx_ring_jumbo(sc);

	/* Free TX buffers. */
	bge_free_tx_ring(sc);

	sc->bge_status_tag = 0;
	sc->bge_link = 0;
	sc->bge_coal_chg = 0;

	sc->bge_tx_saved_considx = BGE_TXCONS_UNSET;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
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
	bus_addr_t lowaddr;
	bus_size_t txmaxsz;

	lowaddr = BUS_SPACE_MAXADDR;
	if (sc->bge_flags & BGE_FLAG_MAXADDR_40BIT)
		lowaddr = BGE_DMA_MAXADDR_40BIT;

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
				   lowaddr, BUS_SPACE_MAXADDR,
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
				   MCLBYTES, 1, MCLBYTES,
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
	if (sc->bge_flags & BGE_FLAG_TSO)
		txmaxsz = IP_MAXPACKET + sizeof(struct ether_vlan_header);
	else
		txmaxsz = BGE_JUMBO_FRAMELEN;
	error = bus_dma_tag_create(sc->bge_cdata.bge_parent_tag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   txmaxsz, BGE_NSEG_NEW, PAGE_SIZE,
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
	error = bge_dma_block_alloc(sc,
	    BGE_RX_RTN_RING_SZ(sc->bge_return_ring_cnt),
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
				DELAY(40);
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
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc->bge_miibus);

	mii_pollstat(mii);
	bge_miibus_statchg(sc->bge_dev);

	if (bootverbose) {
		if (sc->bge_link)
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
bge_autopoll_link_upd(struct bge_softc *sc, uint32_t status __unused)
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
	    BGE_RX_COAL_TICKS_MIN, BGE_RX_COAL_TICKS_MAX,
	    BGE_RX_COAL_TICKS_CHG);
}

static int
bge_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_tx_coal_ticks,
	    BGE_TX_COAL_TICKS_MIN, BGE_TX_COAL_TICKS_MAX,
	    BGE_TX_COAL_TICKS_CHG);
}

static int
bge_sysctl_rx_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_rx_coal_bds,
	    BGE_RX_COAL_BDS_MIN, BGE_RX_COAL_BDS_MAX,
	    BGE_RX_COAL_BDS_CHG);
}

static int
bge_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_tx_coal_bds,
	    BGE_TX_COAL_BDS_MIN, BGE_TX_COAL_BDS_MAX,
	    BGE_TX_COAL_BDS_CHG);
}

static int
bge_sysctl_rx_coal_ticks_int(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_rx_coal_ticks_int,
	    BGE_RX_COAL_TICKS_MIN, BGE_RX_COAL_TICKS_MAX,
	    BGE_RX_COAL_TICKS_INT_CHG);
}

static int
bge_sysctl_tx_coal_ticks_int(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_tx_coal_ticks_int,
	    BGE_TX_COAL_TICKS_MIN, BGE_TX_COAL_TICKS_MAX,
	    BGE_TX_COAL_TICKS_INT_CHG);
}

static int
bge_sysctl_rx_coal_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_rx_coal_bds_int,
	    BGE_RX_COAL_BDS_MIN, BGE_RX_COAL_BDS_MAX,
	    BGE_RX_COAL_BDS_INT_CHG);
}

static int
bge_sysctl_tx_coal_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc = arg1;

	return bge_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bge_tx_coal_bds_int,
	    BGE_TX_COAL_BDS_MIN, BGE_TX_COAL_BDS_MAX,
	    BGE_TX_COAL_BDS_INT_CHG);
}

static int
bge_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *coal,
    int coal_min, int coal_max, uint32_t coal_chg_mask)
{
	struct bge_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = *coal;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != NULL) {
		if (v < coal_min || v > coal_max) {
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

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->bge_coal_chg & BGE_RX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS,
			    sc->bge_rx_coal_ticks);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_RX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_ticks -> %u\n",
				  sc->bge_rx_coal_ticks);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS,
			    sc->bge_tx_coal_ticks);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_TX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_ticks -> %u\n",
				  sc->bge_tx_coal_ticks);
		}
	}

	if (sc->bge_coal_chg & BGE_RX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS,
			    sc->bge_rx_coal_bds);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_RX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_bds -> %u\n",
				  sc->bge_rx_coal_bds);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS,
			    sc->bge_tx_coal_bds);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_TX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "tx_max_coal_bds -> %u\n",
				  sc->bge_tx_coal_bds);
		}
	}

	if (sc->bge_coal_chg & BGE_RX_COAL_TICKS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS_INT,
		    sc->bge_rx_coal_ticks_int);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_RX_COAL_TICKS_INT);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_ticks_int -> %u\n",
			    sc->bge_rx_coal_ticks_int);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_COAL_TICKS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS_INT,
		    sc->bge_tx_coal_ticks_int);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_TX_COAL_TICKS_INT);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_ticks_int -> %u\n",
			    sc->bge_tx_coal_ticks_int);
		}
	}

	if (sc->bge_coal_chg & BGE_RX_COAL_BDS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT,
		    sc->bge_rx_coal_bds_int);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_bds_int -> %u\n",
			    sc->bge_rx_coal_bds_int);
		}
	}

	if (sc->bge_coal_chg & BGE_TX_COAL_BDS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT,
		    sc->bge_tx_coal_bds_int);
		DELAY(10);
		CSR_READ_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_bds_int -> %u\n",
			    sc->bge_tx_coal_bds_int);
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
	bge_writembx(sc, BGE_MBX_IRQ0_LO, sc->bge_status_tag << 24);
	if (sc->bge_flags & BGE_FLAG_ONESHOT_MSI) {
		/* XXX Linux driver */
		bge_writembx(sc, BGE_MBX_IRQ0_LO, sc->bge_status_tag << 24);
	}

	/*
	 * Unmask the interrupt when we stop polling.
	 */
	PCI_CLRBIT(sc->bge_dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_MASK_PCI_INTR, 4);

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
	PCI_SETBIT(sc->bge_dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_MASK_PCI_INTR, 4);

	/*
	 * Acknowledge possible asserted interrupt.
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	sc->bge_npoll.ifpc_stcount = 0;

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

/*
 * NOTE: 'm' is not freed upon failure
 */
static struct mbuf *
bge_defrag_shortdma(struct mbuf *m)
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
bge_stop_block(struct bge_softc *sc, bus_size_t reg, uint32_t bit)
{
	int i;

	BGE_CLRBIT(sc, reg, bit);
	for (i = 0; i < BGE_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, reg) & bit) == 0)
			return;
		DELAY(100);
	}
}

static void
bge_link_poll(struct bge_softc *sc)
{
	uint32_t status;

	status = CSR_READ_4(sc, BGE_MAC_STS);
	if ((status & sc->bge_link_chg) || sc->bge_link_evt) {
		sc->bge_link_evt = 0;
		sc->bge_link_upd(sc, status);
	}
}

static void
bge_enable_msi(struct bge_softc *sc)
{
	uint32_t msi_mode;

	msi_mode = CSR_READ_4(sc, BGE_MSI_MODE);
	msi_mode |= BGE_MSIMODE_ENABLE;
	if (sc->bge_flags & BGE_FLAG_ONESHOT_MSI) {
		/*
		 * According to all of the datasheets that are publicly
		 * available, bit 5 of the MSI_MODE is defined to be
		 * "MSI FIFO Underrun Attn" for BCM5755+ and BCM5906, on
		 * which "oneshot MSI" is enabled.  However, it is always
		 * safe to clear it here.
		 */
		msi_mode &= ~BGE_MSIMODE_ONESHOT_DISABLE;
	}
	CSR_WRITE_4(sc, BGE_MSI_MODE, msi_mode);
}

static int
bge_setup_tso(struct bge_softc *sc, struct mbuf **mp,
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
	mss |= (hlen << 11);

	*mss0 = mss;
	*flags0 = flags;

	return 0;
}

static void
bge_stop_fw(struct bge_softc *sc)
{
	int i;

	if (sc->bge_asf_mode) {
		bge_writemem_ind(sc, BGE_SRAM_FW_CMD_MB, BGE_FW_CMD_PAUSE);
		CSR_WRITE_4(sc, BGE_RX_CPU_EVENT,
		    CSR_READ_4(sc, BGE_RX_CPU_EVENT) | BGE_RX_CPU_DRV_EVENT);

		for (i = 0; i < 100; i++ ) {
			if (!(CSR_READ_4(sc, BGE_RX_CPU_EVENT) &
			    BGE_RX_CPU_DRV_EVENT))
				break;
			DELAY(10);
		}
	}
}

static void
bge_sig_pre_reset(struct bge_softc *sc, int type)
{
	/*
	 * Some chips don't like this so only do this if ASF is enabled
	 */
	if (sc->bge_asf_mode)
		bge_writemem_ind(sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);

	if (sc->bge_asf_mode & ASF_NEW_HANDSHAKE) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START);
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD);
			break;
		case BGE_RESET_SUSPEND:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_SUSPEND);
			break;
		}
	}

	if (type == BGE_RESET_START || type == BGE_RESET_SUSPEND)
		bge_ape_driver_state_change(sc, type);
}

static void
bge_sig_legacy(struct bge_softc *sc, int type)
{
	if (sc->bge_asf_mode) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START);
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD);
			break;
		}
	}
}

static void
bge_sig_post_reset(struct bge_softc *sc, int type)
{
	if (sc->bge_asf_mode & ASF_NEW_HANDSHAKE) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START_DONE);
			/* START DONE */
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD_DONE);
			break;
		}
	}
	if (type == BGE_RESET_SHUTDOWN)
		bge_ape_driver_state_change(sc, type);
}

static void
bge_asf_driver_up(struct bge_softc *sc)
{
	if (sc->bge_asf_mode & ASF_STACKUP) {
		/* Send ASF heartbeat aprox. every 2s */
		if (sc->bge_asf_count)
			sc->bge_asf_count --;
		else {
			sc->bge_asf_count = 2;
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_MB,
			    BGE_FW_CMD_DRV_ALIVE);
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_LEN_MB, 4);
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_DATA_MB,
			    BGE_FW_HB_TIMEOUT_SEC);
			CSR_WRITE_4(sc, BGE_RX_CPU_EVENT,
			    CSR_READ_4(sc, BGE_RX_CPU_EVENT) |
			    BGE_RX_CPU_DRV_EVENT);
		}
	}
}

/*
 * Clear all stale locks and select the lock for this driver instance.
 */
static void
bge_ape_lock_init(struct bge_softc *sc)
{
	uint32_t bit, regbase;
	int i;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		regbase = BGE_APE_LOCK_GRANT;
	else
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
			if (sc->bge_func_addr == 0)
				bit = BGE_APE_LOCK_GRANT_DRIVER0;
			else
				bit = (1 << sc->bge_func_addr);
		}
		APE_WRITE_4(sc, regbase + 4 * i, bit);
	}

	/* Select the PHY lock based on the device's function number. */
	switch (sc->bge_func_addr) {
	case 0:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY0;
		break;
	case 1:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY1;
		break;
	case 2:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY2;
		break;
	case 3:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY3;
		break;
	default:
		device_printf(sc->bge_dev,
		    "PHY lock not supported on this function\n");
	}
}

/*
 * Check for APE firmware, set flags, and print version info.
 */
static void
bge_ape_read_fw_ver(struct bge_softc *sc)
{
	const char *fwtype;
	uint32_t apedata, features;

	/* Check for a valid APE signature in shared memory. */
	apedata = APE_READ_4(sc, BGE_APE_SEG_SIG);
	if (apedata != BGE_APE_SEG_SIG_MAGIC) {
		device_printf(sc->bge_dev, "no APE signature\n");
		sc->bge_mfw_flags &= ~BGE_MFW_ON_APE;
		return;
	}

	/* Check if APE firmware is running. */
	apedata = APE_READ_4(sc, BGE_APE_FW_STATUS);
	if ((apedata & BGE_APE_FW_STATUS_READY) == 0) {
		device_printf(sc->bge_dev, "APE signature found "
		    "but FW status not ready! 0x%08x\n", apedata);
		return;
	}

	sc->bge_mfw_flags |= BGE_MFW_ON_APE;

	/* Fetch the APE firwmare type and version. */
	apedata = APE_READ_4(sc, BGE_APE_FW_VERSION);
	features = APE_READ_4(sc, BGE_APE_FW_FEATURES);
	if ((features & BGE_APE_FW_FEATURE_NCSI) != 0) {
		sc->bge_mfw_flags |= BGE_MFW_TYPE_NCSI;
		fwtype = "NCSI";
	} else if ((features & BGE_APE_FW_FEATURE_DASH) != 0) {
		sc->bge_mfw_flags |= BGE_MFW_TYPE_DASH;
		fwtype = "DASH";
	} else
		fwtype = "UNKN";

	/* Print the APE firmware version. */
	device_printf(sc->bge_dev, "APE FW version: %s v%d.%d.%d.%d\n",
	    fwtype,
	    (apedata & BGE_APE_FW_VERSION_MAJMSK) >> BGE_APE_FW_VERSION_MAJSFT,
	    (apedata & BGE_APE_FW_VERSION_MINMSK) >> BGE_APE_FW_VERSION_MINSFT,
	    (apedata & BGE_APE_FW_VERSION_REVMSK) >> BGE_APE_FW_VERSION_REVSFT,
	    (apedata & BGE_APE_FW_VERSION_BLDMSK));
}

static int
bge_ape_lock(struct bge_softc *sc, int locknum)
{
	uint32_t bit, gnt, req, status;
	int i, off;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return (0);

	/* Lock request/grant registers have different bases. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5761) {
		req = BGE_APE_LOCK_REQ;
		gnt = BGE_APE_LOCK_GRANT;
	} else {
		req = BGE_APE_PER_LOCK_REQ;
		gnt = BGE_APE_PER_LOCK_GRANT;
	}

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		/* Lock required when using GPIO. */
		if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
			return (0);
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_GRC:
		/* Lock required to reset the device. */
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_MEM:
		/* Lock required when accessing certain APE memory. */
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_PHY0:
	case BGE_APE_LOCK_PHY1:
	case BGE_APE_LOCK_PHY2:
	case BGE_APE_LOCK_PHY3:
		/* Lock required when accessing PHYs. */
		bit = BGE_APE_LOCK_REQ_DRIVER0;
		break;
	default:
		return (EINVAL);
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
		device_printf(sc->bge_dev, "APE lock %d request failed! "
		    "request = 0x%04x[0x%04x], status = 0x%04x[0x%04x]\n",
		    locknum, req + off, bit & 0xFFFF, gnt + off,
		    status & 0xFFFF);
		/* Revoke the lock request. */
		APE_WRITE_4(sc, gnt + off, bit);
		return (EBUSY);
	}

	return (0);
}

static void
bge_ape_unlock(struct bge_softc *sc, int locknum)
{
	uint32_t bit, gnt;
	int off;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		gnt = BGE_APE_LOCK_GRANT;
	else
		gnt = BGE_APE_PER_LOCK_GRANT;

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
			return;
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_GRC:
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_MEM:
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
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
bge_ape_send_event(struct bge_softc *sc, uint32_t event)
{
	uint32_t apedata;
	int i;

	/* NCSI does not support APE events. */
	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	/* Wait up to 1ms for APE to service previous event. */
	for (i = 10; i > 0; i--) {
		if (bge_ape_lock(sc, BGE_APE_LOCK_MEM) != 0)
			break;
		apedata = APE_READ_4(sc, BGE_APE_EVENT_STATUS);
		if ((apedata & BGE_APE_EVENT_STATUS_EVENT_PENDING) == 0) {
			APE_WRITE_4(sc, BGE_APE_EVENT_STATUS, event |
			    BGE_APE_EVENT_STATUS_EVENT_PENDING);
			bge_ape_unlock(sc, BGE_APE_LOCK_MEM);
			APE_WRITE_4(sc, BGE_APE_EVENT, BGE_APE_EVENT_1);
			break;
		}
		bge_ape_unlock(sc, BGE_APE_LOCK_MEM);
		DELAY(100);
	}
	if (i == 0)
		device_printf(sc->bge_dev, "APE event 0x%08x send timed out\n",
		    event);
}

static void
bge_ape_driver_state_change(struct bge_softc *sc, int kind)
{
	uint32_t apedata, event;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	switch (kind) {
	case BGE_RESET_START:
		/* If this is the first load, clear the load counter. */
		apedata = APE_READ_4(sc, BGE_APE_HOST_SEG_SIG);
		if (apedata != BGE_APE_HOST_SEG_SIG_MAGIC)
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, 0);
		else {
			apedata = APE_READ_4(sc, BGE_APE_HOST_INIT_COUNT);
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, ++apedata);
		}
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_SIG,
		    BGE_APE_HOST_SEG_SIG_MAGIC);
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_LEN,
		    BGE_APE_HOST_SEG_LEN_MAGIC);

		/* Add some version info if bge(4) supports it. */
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
	case BGE_RESET_SHUTDOWN:
		APE_WRITE_4(sc, BGE_APE_HOST_DRVR_STATE,
		    BGE_APE_HOST_DRVR_STATE_UNLOAD);
		event = BGE_APE_EVENT_STATUS_STATE_UNLOAD;
		break;
	case BGE_RESET_SUSPEND:
		event = BGE_APE_EVENT_STATUS_STATE_SUSPEND;
		break;
	default:
		return;
	}

	bge_ape_send_event(sc, event | BGE_APE_EVENT_STATUS_DRIVER_EVNT |
	    BGE_APE_EVENT_STATUS_STATE_CHNGE);
}
