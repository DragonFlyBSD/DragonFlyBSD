/*-
 * Copyright (c) 2006-2007 Broadcom Corporation
 *	David Christensen <davidch@broadcom.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Broadcom Corporation nor the name of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written consent.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/bce/if_bce.c,v 1.31 2007/05/16 23:34:11 davidch Exp $
 */

/*
 * The following controllers are supported by this driver:
 *   BCM5706C A2, A3
 *   BCM5706S A2, A3
 *   BCM5708C B1, B2
 *   BCM5708S B1, B2
 *   BCM5709C A1, B2, C0
 *   BCM5716  C0
 *
 * The following controllers are not supported by this driver:
 *   BCM5706C A0, A1
 *   BCM5706S A0, A1
 *   BCM5708C A0, B0
 *   BCM5708S A0, B0
 *   BCM5709C A0, B0, B1
 *   BCM5709S A0, A1, B0, B1, B2, C0
 *
 *
 * Note about MSI-X on 5709/5716:
 * - 9 MSI-X vectors are supported.
 * - MSI-X vectors, RX/TX rings and status blocks' association
 *   are fixed:
 *   o  The first RX ring and the first TX ring use the first
 *      status block.
 *   o  The first MSI-X vector is associated with the first
 *      status block.
 *   o  The second RX ring and the second TX ring use the second
 *      status block.
 *   o  The second MSI-X vector is associated with the second
 *      status block.
 *   ...
 *   and so on so forth.
 * - Status blocks must reside in physically contiguous memory
 *   and each status block consumes 128bytes.  In addition to
 *   this, the memory for the status blocks is aligned on 128bytes
 *   in this driver.  (see bce_dma_alloc() and HC_CONFIG)
 * - Each status block has its own coalesce parameters, which also
 *   serve as the related MSI-X vector's interrupt moderation
 *   parameters.  (see bce_coal_change())
 */

#include "opt_bce.h"
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

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "miibus_if.h"

#include <dev/netif/bce/if_bcereg.h>
#include <dev/netif/bce/if_bcefw.h>

#define BCE_MSI_CKINTVL		((10 * hz) / 1000)	/* 10ms */

#ifdef BCE_RSS_DEBUG
#define BCE_RSS_DPRINTF(sc, lvl, fmt, ...) \
do { \
	if (sc->rss_debug >= lvl) \
		if_printf(&sc->arpcom.ac_if, fmt, __VA_ARGS__); \
} while (0)
#else	/* !BCE_RSS_DEBUG */
#define BCE_RSS_DPRINTF(sc, lvl, fmt, ...)	((void)0)
#endif	/* BCE_RSS_DEBUG */

/****************************************************************************/
/* PCI Device ID Table                                                      */
/*                                                                          */
/* Used by bce_probe() to identify the devices supported by this driver.    */
/****************************************************************************/
#define BCE_DEVDESC_MAX		64

static struct bce_type bce_devs[] = {
	/* BCM5706C Controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706,  HP_VENDORID, 0x3101,
		"HP NC370T Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706,  HP_VENDORID, 0x3106,
		"HP NC370i Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706,  HP_VENDORID, 0x3070,
		"HP NC380T PCIe DP Multifunc Gig Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706,  HP_VENDORID, 0x1709,
		"HP NC371i Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706,  PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5706 1000Base-T" },

	/* BCM5706S controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706S, HP_VENDORID, 0x3102,
		"HP NC370F Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5706S, PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5706 1000Base-SX" },

	/* BCM5708C controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708,  HP_VENDORID, 0x7037,
		"HP NC373T PCIe Multifunction Gig Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708,  HP_VENDORID, 0x7038,
		"HP NC373i Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708,  HP_VENDORID, 0x7045,
		"HP NC374m PCIe Multifunction Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708,  PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5708 1000Base-T" },

	/* BCM5708S controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708S,  HP_VENDORID, 0x1706,
		"HP NC373m Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708S,  HP_VENDORID, 0x703b,
		"HP NC373i Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708S,  HP_VENDORID, 0x703d,
		"HP NC373F PCIe Multifunc Giga Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5708S,  PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5708S 1000Base-T" },

	/* BCM5709C controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709,  HP_VENDORID, 0x7055,
		"HP NC382i DP Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709,  HP_VENDORID, 0x7059,
		"HP NC382T PCIe DP Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709,  PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5709 1000Base-T" },

	/* BCM5709S controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709S,  HP_VENDORID, 0x171d,
		"HP NC382m DP 1GbE Multifunction BL-c Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709S,  HP_VENDORID, 0x7056,
		"HP NC382i DP Multifunction Gigabit Server Adapter" },
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5709S,  PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5709 1000Base-SX" },

	/* BCM5716 controllers and OEM boards. */
	{ BRCM_VENDORID, BRCM_DEVICEID_BCM5716,   PCI_ANY_ID,  PCI_ANY_ID,
		"Broadcom NetXtreme II BCM5716 1000Base-T" },

	{ 0, 0, 0, 0, NULL }
};

/****************************************************************************/
/* Supported Flash NVRAM device data.                                       */
/****************************************************************************/
static const struct flash_spec flash_table[] =
{
#define BUFFERED_FLAGS		(BCE_NV_BUFFERED | BCE_NV_TRANSLATE)
#define NONBUFFERED_FLAGS	(BCE_NV_WREN)

	/* Slow EEPROM */
	{0x00000000, 0x40830380, 0x009f0081, 0xa184a053, 0xaf000400,
	 BUFFERED_FLAGS, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - slow"},
	/* Expansion entry 0001 */
	{0x08000002, 0x4b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0001"},
	/* Saifun SA25F010 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x04000001, 0x47808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*2,
	 "Non-buffered flash (128kB)"},
	/* Saifun SA25F020 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x0c000003, 0x4f808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*4,
	 "Non-buffered flash (256kB)"},
	/* Expansion entry 0100 */
	{0x11000000, 0x53808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0100"},
	/* Entry 0101: ST M45PE10 (non-buffered flash, TetonII B0) */
	{0x19000002, 0x5b808201, 0x000500db, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*2,
	 "Entry 0101: ST M45PE10 (128kB non-bufferred)"},
	/* Entry 0110: ST M45PE20 (non-buffered flash)*/
	{0x15000001, 0x57808201, 0x000500db, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*4,
	 "Entry 0110: ST M45PE20 (256kB non-bufferred)"},
	/* Saifun SA25F005 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x1d000003, 0x5f808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE,
	 "Non-buffered flash (64kB)"},
	/* Fast EEPROM */
	{0x22000000, 0x62808380, 0x009f0081, 0xa184a053, 0xaf000400,
	 BUFFERED_FLAGS, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - fast"},
	/* Expansion entry 1001 */
	{0x2a000002, 0x6b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1001"},
	/* Expansion entry 1010 */
	{0x26000001, 0x67808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1010"},
	/* ATMEL AT45DB011B (buffered flash) */
	{0x2e000003, 0x6e808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE,
	 "Buffered flash (128kB)"},
	/* Expansion entry 1100 */
	{0x33000000, 0x73808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1100"},
	/* Expansion entry 1101 */
	{0x3b000002, 0x7b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1101"},
	/* Ateml Expansion entry 1110 */
	{0x37000001, 0x76808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1110 (Atmel)"},
	/* ATMEL AT45DB021B (buffered flash) */
	{0x3f000003, 0x7e808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE*2,
	 "Buffered flash (256kB)"},
};

/*
 * The BCM5709 controllers transparently handle the
 * differences between Atmel 264 byte pages and all
 * flash devices which use 256 byte pages, so no
 * logical-to-physical mapping is required in the
 * driver.
 */
static struct flash_spec flash_5709 = {
	.flags		= BCE_NV_BUFFERED,
	.page_bits	= BCM5709_FLASH_PAGE_BITS,
	.page_size	= BCM5709_FLASH_PAGE_SIZE,
	.addr_mask	= BCM5709_FLASH_BYTE_ADDR_MASK,
	.total_size	= BUFFERED_FLASH_TOTAL_SIZE * 2,
	.name		= "5709/5716 buffered flash (256kB)",
};

/****************************************************************************/
/* DragonFly device entry points.                                           */
/****************************************************************************/
static int	bce_probe(device_t);
static int	bce_attach(device_t);
static int	bce_detach(device_t);
static void	bce_shutdown(device_t);
static int	bce_miibus_read_reg(device_t, int, int);
static int	bce_miibus_write_reg(device_t, int, int, int);
static void	bce_miibus_statchg(device_t);

/****************************************************************************/
/* BCE Register/Memory Access Routines                                      */
/****************************************************************************/
static uint32_t	bce_reg_rd_ind(struct bce_softc *, uint32_t);
static void	bce_reg_wr_ind(struct bce_softc *, uint32_t, uint32_t);
static void	bce_shmem_wr(struct bce_softc *, uint32_t, uint32_t);
static uint32_t	bce_shmem_rd(struct bce_softc *, u32);
static void	bce_ctx_wr(struct bce_softc *, uint32_t, uint32_t, uint32_t);

/****************************************************************************/
/* BCE NVRAM Access Routines                                                */
/****************************************************************************/
static int	bce_acquire_nvram_lock(struct bce_softc *);
static int	bce_release_nvram_lock(struct bce_softc *);
static void	bce_enable_nvram_access(struct bce_softc *);
static void	bce_disable_nvram_access(struct bce_softc *);
static int	bce_nvram_read_dword(struct bce_softc *, uint32_t, uint8_t *,
		    uint32_t);
static int	bce_init_nvram(struct bce_softc *);
static int	bce_nvram_read(struct bce_softc *, uint32_t, uint8_t *, int);
static int	bce_nvram_test(struct bce_softc *);

/****************************************************************************/
/* BCE DMA Allocate/Free Routines                                           */
/****************************************************************************/
static int	bce_dma_alloc(struct bce_softc *);
static void	bce_dma_free(struct bce_softc *);
static void	bce_dma_map_addr(void *, bus_dma_segment_t *, int, int);

/****************************************************************************/
/* BCE Firmware Synchronization and Load                                    */
/****************************************************************************/
static int	bce_fw_sync(struct bce_softc *, uint32_t);
static void	bce_load_rv2p_fw(struct bce_softc *, uint32_t *,
		    uint32_t, uint32_t);
static void	bce_load_cpu_fw(struct bce_softc *, struct cpu_reg *,
		    struct fw_info *);
static void	bce_start_cpu(struct bce_softc *, struct cpu_reg *);
static void	bce_halt_cpu(struct bce_softc *, struct cpu_reg *);
static void	bce_start_rxp_cpu(struct bce_softc *);
static void	bce_init_rxp_cpu(struct bce_softc *);
static void	bce_init_txp_cpu(struct bce_softc *);
static void	bce_init_tpat_cpu(struct bce_softc *);
static void	bce_init_cp_cpu(struct bce_softc *);
static void	bce_init_com_cpu(struct bce_softc *);
static void	bce_init_cpus(struct bce_softc *);
static void	bce_setup_msix_table(struct bce_softc *);
static void	bce_init_rss(struct bce_softc *);

static void	bce_stop(struct bce_softc *);
static int	bce_reset(struct bce_softc *, uint32_t);
static int	bce_chipinit(struct bce_softc *);
static int	bce_blockinit(struct bce_softc *);
static void	bce_probe_pci_caps(struct bce_softc *);
static void	bce_print_adapter_info(struct bce_softc *);
static void	bce_get_media(struct bce_softc *);
static void	bce_mgmt_init(struct bce_softc *);
static int	bce_init_ctx(struct bce_softc *);
static void	bce_get_mac_addr(struct bce_softc *);
static void	bce_set_mac_addr(struct bce_softc *);
static void	bce_set_rx_mode(struct bce_softc *);
static void	bce_coal_change(struct bce_softc *);
static void	bce_npoll_coal_change(struct bce_softc *);
static void	bce_setup_serialize(struct bce_softc *);
static void	bce_serialize_skipmain(struct bce_softc *);
static void	bce_deserialize_skipmain(struct bce_softc *);
static void	bce_set_timer_cpuid(struct bce_softc *, boolean_t);
static int	bce_alloc_intr(struct bce_softc *);
static void	bce_free_intr(struct bce_softc *);
static void	bce_try_alloc_msix(struct bce_softc *);
static void	bce_free_msix(struct bce_softc *, boolean_t);
static void	bce_setup_ring_cnt(struct bce_softc *);
static int	bce_setup_intr(struct bce_softc *);
static void	bce_teardown_intr(struct bce_softc *);
static int	bce_setup_msix(struct bce_softc *);
static void	bce_teardown_msix(struct bce_softc *, int);

static int	bce_create_tx_ring(struct bce_tx_ring *);
static void	bce_destroy_tx_ring(struct bce_tx_ring *);
static void	bce_init_tx_context(struct bce_tx_ring *);
static int	bce_init_tx_chain(struct bce_tx_ring *);
static void	bce_free_tx_chain(struct bce_tx_ring *);
static void	bce_xmit(struct bce_tx_ring *);
static int	bce_encap(struct bce_tx_ring *, struct mbuf **, int *);
static int	bce_tso_setup(struct bce_tx_ring *, struct mbuf **,
		    uint16_t *, uint16_t *);

static int	bce_create_rx_ring(struct bce_rx_ring *);
static void	bce_destroy_rx_ring(struct bce_rx_ring *);
static void	bce_init_rx_context(struct bce_rx_ring *);
static int	bce_init_rx_chain(struct bce_rx_ring *);
static void	bce_free_rx_chain(struct bce_rx_ring *);
static int	bce_newbuf_std(struct bce_rx_ring *, uint16_t *, uint16_t,
		    uint32_t *, int);
static void	bce_setup_rxdesc_std(struct bce_rx_ring *, uint16_t,
		    uint32_t *);
static struct pktinfo *bce_rss_pktinfo(struct pktinfo *, uint32_t,
		    const struct l2_fhdr *);

static void	bce_start(struct ifnet *, struct ifaltq_subque *);
static int	bce_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bce_watchdog(struct ifaltq_subque *);
static int	bce_ifmedia_upd(struct ifnet *);
static void	bce_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	bce_init(void *);
#ifdef IFPOLL_ENABLE
static void	bce_npoll(struct ifnet *, struct ifpoll_info *);
static void	bce_npoll_rx(struct ifnet *, void *, int);
static void	bce_npoll_tx(struct ifnet *, void *, int);
static void	bce_npoll_status(struct ifnet *);
static void	bce_npoll_rx_pack(struct ifnet *, void *, int);
#endif
static void	bce_serialize(struct ifnet *, enum ifnet_serialize);
static void	bce_deserialize(struct ifnet *, enum ifnet_serialize);
static int	bce_tryserialize(struct ifnet *, enum ifnet_serialize);
#ifdef INVARIANTS
static void	bce_serialize_assert(struct ifnet *, enum ifnet_serialize,
		    boolean_t);
#endif

static void	bce_intr(struct bce_softc *);
static void	bce_intr_legacy(void *);
static void	bce_intr_msi(void *);
static void	bce_intr_msi_oneshot(void *);
static void	bce_intr_msix_rxtx(void *);
static void	bce_intr_msix_rx(void *);
static void	bce_tx_intr(struct bce_tx_ring *, uint16_t);
static void	bce_rx_intr(struct bce_rx_ring *, int, uint16_t);
static void	bce_phy_intr(struct bce_softc *);
static void	bce_disable_intr(struct bce_softc *);
static void	bce_enable_intr(struct bce_softc *);
static void	bce_reenable_intr(struct bce_rx_ring *);
static void	bce_check_msi(void *);

static void	bce_stats_update(struct bce_softc *);
static void	bce_tick(void *);
static void	bce_tick_serialized(struct bce_softc *);
static void	bce_pulse(void *);

static void	bce_add_sysctls(struct bce_softc *);
static int	bce_sysctl_tx_bds_int(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_tx_bds(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_tx_ticks_int(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_tx_ticks(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_rx_bds_int(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_rx_bds(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_rx_ticks_int(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_rx_ticks(SYSCTL_HANDLER_ARGS);
static int	bce_sysctl_coal_change(SYSCTL_HANDLER_ARGS,
		    uint32_t *, uint32_t);

/*
 * NOTE:
 * Don't set bce_tx_ticks_int/bce_tx_ticks to 1023.  Linux's bnx2
 * takes 1023 as the TX ticks limit.  However, using 1023 will
 * cause 5708(B2) to generate extra interrupts (~2000/s) even when
 * there is _no_ network activity on the NIC.
 */
static uint32_t	bce_tx_bds_int = 255;		/* bcm: 20 */
static uint32_t	bce_tx_bds = 255;		/* bcm: 20 */
static uint32_t	bce_tx_ticks_int = 1022;	/* bcm: 80 */
static uint32_t	bce_tx_ticks = 1022;		/* bcm: 80 */
static uint32_t	bce_rx_bds_int = 128;		/* bcm: 6 */
static uint32_t	bce_rx_bds = 0;			/* bcm: 6 */
static uint32_t	bce_rx_ticks_int = 150;		/* bcm: 18 */
static uint32_t	bce_rx_ticks = 150;		/* bcm: 18 */

static int	bce_tx_wreg = 8;

static int	bce_msi_enable = 1;
static int	bce_msix_enable = 1;

static int	bce_rx_pages = RX_PAGES_DEFAULT;
static int	bce_tx_pages = TX_PAGES_DEFAULT;

static int	bce_rx_rings = 0;	/* auto */
static int	bce_tx_rings = 0;	/* auto */

TUNABLE_INT("hw.bce.tx_bds_int", &bce_tx_bds_int);
TUNABLE_INT("hw.bce.tx_bds", &bce_tx_bds);
TUNABLE_INT("hw.bce.tx_ticks_int", &bce_tx_ticks_int);
TUNABLE_INT("hw.bce.tx_ticks", &bce_tx_ticks);
TUNABLE_INT("hw.bce.rx_bds_int", &bce_rx_bds_int);
TUNABLE_INT("hw.bce.rx_bds", &bce_rx_bds);
TUNABLE_INT("hw.bce.rx_ticks_int", &bce_rx_ticks_int);
TUNABLE_INT("hw.bce.rx_ticks", &bce_rx_ticks);
TUNABLE_INT("hw.bce.msi.enable", &bce_msi_enable);
TUNABLE_INT("hw.bce.msix.enable", &bce_msix_enable);
TUNABLE_INT("hw.bce.rx_pages", &bce_rx_pages);
TUNABLE_INT("hw.bce.tx_pages", &bce_tx_pages);
TUNABLE_INT("hw.bce.tx_wreg", &bce_tx_wreg);
TUNABLE_INT("hw.bce.tx_rings", &bce_tx_rings);
TUNABLE_INT("hw.bce.rx_rings", &bce_rx_rings);

/****************************************************************************/
/* DragonFly device dispatch table.                                         */
/****************************************************************************/
static device_method_t bce_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bce_probe),
	DEVMETHOD(device_attach,	bce_attach),
	DEVMETHOD(device_detach,	bce_detach),
	DEVMETHOD(device_shutdown,	bce_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bce_miibus_read_reg),
	DEVMETHOD(miibus_writereg,	bce_miibus_write_reg),
	DEVMETHOD(miibus_statchg,	bce_miibus_statchg),

	DEVMETHOD_END
};

static driver_t bce_driver = {
	"bce",
	bce_methods,
	sizeof(struct bce_softc)
};

static devclass_t bce_devclass;

DECLARE_DUMMY_MODULE(if_bce);
MODULE_DEPEND(bce, miibus, 1, 1, 1);
DRIVER_MODULE(if_bce, pci, bce_driver, bce_devclass, NULL, NULL);
DRIVER_MODULE(miibus, bce, miibus_driver, miibus_devclass, NULL, NULL);

/****************************************************************************/
/* Device probe function.                                                   */
/*                                                                          */
/* Compares the device to the driver's list of supported devices and        */
/* reports back to the OS whether this is the right driver for the device.  */
/*                                                                          */
/* Returns:                                                                 */
/*   BUS_PROBE_DEFAULT on success, positive value on failure.               */
/****************************************************************************/
static int
bce_probe(device_t dev)
{
	struct bce_type *t;
	uint16_t vid, did, svid, sdid;

	/* Get the data for the device to be probed. */
	vid  = pci_get_vendor(dev);
	did  = pci_get_device(dev);
	svid = pci_get_subvendor(dev);
	sdid = pci_get_subdevice(dev);

	/* Look through the list of known devices for a match. */
	for (t = bce_devs; t->bce_name != NULL; ++t) {
		if (vid == t->bce_vid && did == t->bce_did && 
		    (svid == t->bce_svid || t->bce_svid == PCI_ANY_ID) &&
		    (sdid == t->bce_sdid || t->bce_sdid == PCI_ANY_ID)) {
		    	uint32_t revid = pci_read_config(dev, PCIR_REVID, 4);
			char *descbuf;

			descbuf = kmalloc(BCE_DEVDESC_MAX, M_TEMP, M_WAITOK);

			/* Print out the device identity. */
			ksnprintf(descbuf, BCE_DEVDESC_MAX, "%s (%c%d)",
				  t->bce_name,
				  ((revid & 0xf0) >> 4) + 'A', revid & 0xf);

			device_set_desc_copy(dev, descbuf);
			kfree(descbuf, M_TEMP);
			return 0;
		}
	}
	return ENXIO;
}

/****************************************************************************/
/* PCI Capabilities Probe Function.                                         */
/*                                                                          */
/* Walks the PCI capabiites list for the device to find what features are   */
/* supported.                                                               */
/*                                                                          */
/* Returns:                                                                 */
/*   None.                                                                  */
/****************************************************************************/
static void
bce_print_adapter_info(struct bce_softc *sc)
{
	device_printf(sc->bce_dev, "ASIC (0x%08X); ", sc->bce_chipid);

	kprintf("Rev (%c%d); ", ((BCE_CHIP_ID(sc) & 0xf000) >> 12) + 'A',
		((BCE_CHIP_ID(sc) & 0x0ff0) >> 4));

	/* Bus info. */
	if (sc->bce_flags & BCE_PCIE_FLAG) {
		kprintf("Bus (PCIe x%d, ", sc->link_width);
		switch (sc->link_speed) {
		case 1:
			kprintf("2.5Gbps); ");
			break;
		case 2:
			kprintf("5Gbps); ");
			break;
		default:
			kprintf("Unknown link speed); ");
			break;
		}
	} else {
		kprintf("Bus (PCI%s, %s, %dMHz); ",
		    ((sc->bce_flags & BCE_PCIX_FLAG) ? "-X" : ""),
		    ((sc->bce_flags & BCE_PCI_32BIT_FLAG) ? "32-bit" : "64-bit"),
		    sc->bus_speed_mhz);
	}

	/* Firmware version and device features. */
	kprintf("B/C (%s)", sc->bce_bc_ver);

	if ((sc->bce_flags & BCE_MFW_ENABLE_FLAG) ||
	    (sc->bce_phy_flags & BCE_PHY_2_5G_CAPABLE_FLAG)) {
		kprintf("; Flags(");
		if (sc->bce_flags & BCE_MFW_ENABLE_FLAG)
			kprintf("MFW[%s]", sc->bce_mfw_ver);
		if (sc->bce_phy_flags & BCE_PHY_2_5G_CAPABLE_FLAG)
			kprintf(" 2.5G");
		kprintf(")");
	}
	kprintf("\n");
}

/****************************************************************************/
/* PCI Capabilities Probe Function.                                         */
/*                                                                          */
/* Walks the PCI capabiites list for the device to find what features are   */
/* supported.                                                               */
/*                                                                          */
/* Returns:                                                                 */
/*   None.                                                                  */
/****************************************************************************/
static void
bce_probe_pci_caps(struct bce_softc *sc)
{
	device_t dev = sc->bce_dev;
	uint8_t ptr;

	if (pci_is_pcix(dev))
		sc->bce_cap_flags |= BCE_PCIX_CAPABLE_FLAG;

	ptr = pci_get_pciecap_ptr(dev);
	if (ptr) {
		uint16_t link_status = pci_read_config(dev, ptr + 0x12, 2);

		sc->link_speed = link_status & 0xf;
		sc->link_width = (link_status >> 4) & 0x3f;
		sc->bce_cap_flags |= BCE_PCIE_CAPABLE_FLAG;
		sc->bce_flags |= BCE_PCIE_FLAG;
	}
}

/****************************************************************************/
/* Device attach function.                                                  */
/*                                                                          */
/* Allocates device resources, performs secondary chip identification,      */
/* resets and initializes the hardware, and initializes driver instance     */
/* variables.                                                               */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_attach(device_t dev)
{
	struct bce_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;
	int rid, rc = 0;
	int i, j;
	struct mii_probe_args mii_args;
	uintptr_t mii_priv = 0;

	sc->bce_dev = dev;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	lwkt_serialize_init(&sc->main_serialize);
	for (i = 0; i < BCE_MSIX_MAX; ++i) {
		struct bce_msix_data *msix = &sc->bce_msix[i];

		msix->msix_cpuid = -1;
		msix->msix_rid = -1;
	}

	pci_enable_busmaster(dev);

	bce_probe_pci_caps(sc);

	/* Allocate PCI memory resources. */
	rid = PCIR_BAR(0);
	sc->bce_res_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
						 RF_ACTIVE | PCI_RF_DENSE);
	if (sc->bce_res_mem == NULL) {
		device_printf(dev, "PCI memory allocation failed\n");
		return ENXIO;
	}
	sc->bce_btag = rman_get_bustag(sc->bce_res_mem);
	sc->bce_bhandle = rman_get_bushandle(sc->bce_res_mem);

	/*
	 * Configure byte swap and enable indirect register access.
	 * Rely on CPU to do target byte swapping on big endian systems.
	 * Access to registers outside of PCI configurtion space are not
	 * valid until this is done.
	 */
	pci_write_config(dev, BCE_PCICFG_MISC_CONFIG,
			 BCE_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
			 BCE_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP, 4);

	/* Save ASIC revsion info. */
	sc->bce_chipid =  REG_RD(sc, BCE_MISC_ID);

	/* Weed out any non-production controller revisions. */
	switch (BCE_CHIP_ID(sc)) {
	case BCE_CHIP_ID_5706_A0:
	case BCE_CHIP_ID_5706_A1:
	case BCE_CHIP_ID_5708_A0:
	case BCE_CHIP_ID_5708_B0:
	case BCE_CHIP_ID_5709_A0:
	case BCE_CHIP_ID_5709_B0:
	case BCE_CHIP_ID_5709_B1:
#ifdef foo
	/* 5709C B2 seems to work fine */
	case BCE_CHIP_ID_5709_B2:
#endif
		device_printf(dev, "Unsupported chip id 0x%08x!\n",
			      BCE_CHIP_ID(sc));
		rc = ENODEV;
		goto fail;
	}

	mii_priv |= BRGPHY_FLAG_WIRESPEED;
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709) {
		if (BCE_CHIP_REV(sc) == BCE_CHIP_REV_Ax ||
		    BCE_CHIP_REV(sc) == BCE_CHIP_REV_Bx)
			mii_priv |= BRGPHY_FLAG_NO_EARLYDAC;
	} else {
		mii_priv |= BRGPHY_FLAG_BER_BUG;
	}

	/*
	 * Find the base address for shared memory access.
	 * Newer versions of bootcode use a signature and offset
	 * while older versions use a fixed address.
	 */
	val = REG_RD_IND(sc, BCE_SHM_HDR_SIGNATURE);
	if ((val & BCE_SHM_HDR_SIGNATURE_SIG_MASK) ==
	    BCE_SHM_HDR_SIGNATURE_SIG) {
		/* Multi-port devices use different offsets in shared memory. */
		sc->bce_shmem_base = REG_RD_IND(sc,
		    BCE_SHM_HDR_ADDR_0 + (pci_get_function(sc->bce_dev) << 2));
	} else {
		sc->bce_shmem_base = HOST_VIEW_SHMEM_BASE;
	}

	/* Fetch the bootcode revision. */
	val = bce_shmem_rd(sc, BCE_DEV_INFO_BC_REV);
	for (i = 0, j = 0; i < 3; i++) {
		uint8_t num;
		int k, skip0;

		num = (uint8_t)(val >> (24 - (i * 8)));
		for (k = 100, skip0 = 1; k >= 1; num %= k, k /= 10) {
			if (num >= k || !skip0 || k == 1) {
				sc->bce_bc_ver[j++] = (num / k) + '0';
				skip0 = 0;
			}
		}
		if (i != 2)
			sc->bce_bc_ver[j++] = '.';
	}

	/* Check if any management firwmare is running. */
	val = bce_shmem_rd(sc, BCE_PORT_FEATURE);
	if (val & BCE_PORT_FEATURE_ASF_ENABLED) {
		sc->bce_flags |= BCE_MFW_ENABLE_FLAG;

		/* Allow time for firmware to enter the running state. */
		for (i = 0; i < 30; i++) {
			val = bce_shmem_rd(sc, BCE_BC_STATE_CONDITION);
			if (val & BCE_CONDITION_MFW_RUN_MASK)
				break;
			DELAY(10000);
		}
	}

	/* Check the current bootcode state. */
	val = bce_shmem_rd(sc, BCE_BC_STATE_CONDITION) &
	    BCE_CONDITION_MFW_RUN_MASK;
	if (val != BCE_CONDITION_MFW_RUN_UNKNOWN &&
	    val != BCE_CONDITION_MFW_RUN_NONE) {
		uint32_t addr = bce_shmem_rd(sc, BCE_MFW_VER_PTR);

		for (i = 0, j = 0; j < 3; j++) {
			val = bce_reg_rd_ind(sc, addr + j * 4);
			val = bswap32(val);
			memcpy(&sc->bce_mfw_ver[i], &val, 4);
			i += 4;
		}
	}

	/* Get PCI bus information (speed and type). */
	val = REG_RD(sc, BCE_PCICFG_MISC_STATUS);
	if (val & BCE_PCICFG_MISC_STATUS_PCIX_DET) {
		uint32_t clkreg;

		sc->bce_flags |= BCE_PCIX_FLAG;

		clkreg = REG_RD(sc, BCE_PCICFG_PCI_CLOCK_CONTROL_BITS) &
			 BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET;
		switch (clkreg) {
		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ:
			sc->bus_speed_mhz = 133;
			break;

		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ:
			sc->bus_speed_mhz = 100;
			break;

		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ:
		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ:
			sc->bus_speed_mhz = 66;
			break;

		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ:
		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ:
			sc->bus_speed_mhz = 50;
			break;

		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW:
		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ:
		case BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ:
			sc->bus_speed_mhz = 33;
			break;
		}
	} else {
		if (val & BCE_PCICFG_MISC_STATUS_M66EN)
			sc->bus_speed_mhz = 66;
		else
			sc->bus_speed_mhz = 33;
	}

	if (val & BCE_PCICFG_MISC_STATUS_32BIT_DET)
		sc->bce_flags |= BCE_PCI_32BIT_FLAG;

	/* Reset the controller. */
	rc = bce_reset(sc, BCE_DRV_MSG_CODE_RESET);
	if (rc != 0)
		goto fail;

	/* Initialize the controller. */
	rc = bce_chipinit(sc);
	if (rc != 0) {
		device_printf(dev, "Controller initialization failed!\n");
		goto fail;
	}

	/* Perform NVRAM test. */
	rc = bce_nvram_test(sc);
	if (rc != 0) {
		device_printf(dev, "NVRAM test failed!\n");
		goto fail;
	}

	/* Fetch the permanent Ethernet MAC address. */
	bce_get_mac_addr(sc);

	/*
	 * Trip points control how many BDs
	 * should be ready before generating an
	 * interrupt while ticks control how long
	 * a BD can sit in the chain before
	 * generating an interrupt.  Set the default 
	 * values for the RX and TX rings.
	 */

#ifdef BCE_DRBUG
	/* Force more frequent interrupts. */
	sc->bce_tx_quick_cons_trip_int = 1;
	sc->bce_tx_quick_cons_trip     = 1;
	sc->bce_tx_ticks_int           = 0;
	sc->bce_tx_ticks               = 0;

	sc->bce_rx_quick_cons_trip_int = 1;
	sc->bce_rx_quick_cons_trip     = 1;
	sc->bce_rx_ticks_int           = 0;
	sc->bce_rx_ticks               = 0;
#else
	sc->bce_tx_quick_cons_trip_int = bce_tx_bds_int;
	sc->bce_tx_quick_cons_trip     = bce_tx_bds;
	sc->bce_tx_ticks_int           = bce_tx_ticks_int;
	sc->bce_tx_ticks               = bce_tx_ticks;

	sc->bce_rx_quick_cons_trip_int = bce_rx_bds_int;
	sc->bce_rx_quick_cons_trip     = bce_rx_bds;
	sc->bce_rx_ticks_int           = bce_rx_ticks_int;
	sc->bce_rx_ticks               = bce_rx_ticks;
#endif

	/* Update statistics once every second. */
	sc->bce_stats_ticks = 1000000 & 0xffff00;

	/* Find the media type for the adapter. */
	bce_get_media(sc);

	/* Find out RX/TX ring count */
	bce_setup_ring_cnt(sc);

	/* Allocate DMA memory resources. */
	rc = bce_dma_alloc(sc);
	if (rc != 0) {
		device_printf(dev, "DMA resource allocation failed!\n");
		goto fail;
	}

	/* Allocate PCI IRQ resources. */
	rc = bce_alloc_intr(sc);
	if (rc != 0)
		goto fail;

	/* Setup serializer */
	bce_setup_serialize(sc);

	/* Initialize the ifnet interface. */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bce_ioctl;
	ifp->if_start = bce_start;
	ifp->if_init = bce_init;
	ifp->if_serialize = bce_serialize;
	ifp->if_deserialize = bce_deserialize;
	ifp->if_tryserialize = bce_tryserialize;
#ifdef INVARIANTS
	ifp->if_serialize_assert = bce_serialize_assert;
#endif
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = bce_npoll;
#endif

	ifp->if_mtu = ETHERMTU;
	ifp->if_hwassist = BCE_CSUM_FEATURES | CSUM_TSO;
	ifp->if_capabilities = BCE_IF_CAPABILITIES;
	if (sc->rx_ring_cnt > 1)
		ifp->if_capabilities |= IFCAP_RSS;
	ifp->if_capenable = ifp->if_capabilities;

	if (sc->bce_phy_flags & BCE_PHY_2_5G_CAPABLE_FLAG)
		ifp->if_baudrate = IF_Mbps(2500ULL);
	else
		ifp->if_baudrate = IF_Mbps(1000ULL);

	ifp->if_nmbclusters = sc->rx_ring_cnt * USABLE_RX_BD(&sc->rx_rings[0]);

	ifq_set_maxlen(&ifp->if_snd, USABLE_TX_BD(&sc->tx_rings[0]));
	ifq_set_ready(&ifp->if_snd);
	ifq_set_subq_cnt(&ifp->if_snd, sc->tx_ring_cnt);

	if (sc->tx_ring_cnt > 1) {
		ifp->if_mapsubq = ifq_mapsubq_modulo;
		ifq_set_subq_divisor(&ifp->if_snd, sc->tx_ring_cnt);
	}

	/*
	 * Look for our PHY.
	 */
	mii_probe_args_init(&mii_args, bce_ifmedia_upd, bce_ifmedia_sts);
	mii_args.mii_probemask = 1 << sc->bce_phy_addr;
	mii_args.mii_privtag = MII_PRIVTAG_BRGPHY;
	mii_args.mii_priv = mii_priv;

	rc = mii_probe(dev, &sc->bce_miibus, &mii_args);
	if (rc != 0) {
		device_printf(dev, "PHY probe failed!\n");
		goto fail;
	}

	/* Attach to the Ethernet interface list. */
	ether_ifattach(ifp, sc->eaddr, NULL);

	/* Setup TX rings and subqueues */
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(&ifp->if_snd, i);
		struct bce_tx_ring *txr = &sc->tx_rings[i];

		ifsq_set_cpuid(ifsq, sc->bce_msix[i].msix_cpuid);
		ifsq_set_priv(ifsq, txr);
		ifsq_set_hw_serialize(ifsq, &txr->tx_serialize);
		txr->ifsq = ifsq;

		ifsq_watchdog_init(&txr->tx_watchdog, ifsq, bce_watchdog, 0);
	}

	callout_init_mp(&sc->bce_tick_callout);
	callout_init_mp(&sc->bce_pulse_callout);
	callout_init_mp(&sc->bce_ckmsi_callout);

	rc = bce_setup_intr(sc);
	if (rc != 0) {
		device_printf(dev, "Failed to setup IRQ!\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	/* Set timer CPUID */
	bce_set_timer_cpuid(sc, FALSE);

	/* Add the supported sysctls to the kernel. */
	bce_add_sysctls(sc);

	/*
	 * The chip reset earlier notified the bootcode that
	 * a driver is present.  We now need to start our pulse
	 * routine so that the bootcode is reminded that we're
	 * still running.
	 */
	bce_pulse(sc);

	/* Get the firmware running so IPMI still works */
	bce_mgmt_init(sc);

	if (bootverbose)
		bce_print_adapter_info(sc);

	return 0;
fail:
	bce_detach(dev);
	return(rc);
}

/****************************************************************************/
/* Device detach function.                                                  */
/*                                                                          */
/* Stops the controller, resets the controller, and releases resources.     */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_detach(device_t dev)
{
	struct bce_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;
		uint32_t msg;

		ifnet_serialize_all(ifp);

		/* Stop and reset the controller. */
		callout_stop(&sc->bce_pulse_callout);
		bce_stop(sc);
		if (sc->bce_flags & BCE_NO_WOL_FLAG)
			msg = BCE_DRV_MSG_CODE_UNLOAD_LNK_DN;
		else
			msg = BCE_DRV_MSG_CODE_UNLOAD;
		bce_reset(sc, msg);

		bce_teardown_intr(sc);

		ifnet_deserialize_all(ifp);

		ether_ifdetach(ifp);
	}

	/* If we have a child device on the MII bus remove it too. */
	if (sc->bce_miibus)
		device_delete_child(dev, sc->bce_miibus);
	bus_generic_detach(dev);

	bce_free_intr(sc);

	if (sc->bce_res_mem != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(0),
				     sc->bce_res_mem);
	}

	bce_dma_free(sc);

	if (sc->serializes != NULL)
		kfree(sc->serializes, M_DEVBUF);

	if (sc->tx_rmap != NULL)
		if_ringmap_free(sc->tx_rmap);
	if (sc->rx_rmap != NULL)
		if_ringmap_free(sc->rx_rmap);

	return 0;
}

/****************************************************************************/
/* Device shutdown function.                                                */
/*                                                                          */
/* Stops and resets the controller.                                         */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing                                                                */
/****************************************************************************/
static void
bce_shutdown(device_t dev)
{
	struct bce_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t msg;

	ifnet_serialize_all(ifp);

	bce_stop(sc);
	if (sc->bce_flags & BCE_NO_WOL_FLAG)
		msg = BCE_DRV_MSG_CODE_UNLOAD_LNK_DN;
	else
		msg = BCE_DRV_MSG_CODE_UNLOAD;
	bce_reset(sc, msg);

	ifnet_deserialize_all(ifp);
}

/****************************************************************************/
/* Indirect register read.                                                  */
/*                                                                          */
/* Reads NetXtreme II registers using an index/data register pair in PCI    */
/* configuration space.  Using this mechanism avoids issues with posted     */
/* reads but is much slower than memory-mapped I/O.                         */
/*                                                                          */
/* Returns:                                                                 */
/*   The value of the register.                                             */
/****************************************************************************/
static uint32_t
bce_reg_rd_ind(struct bce_softc *sc, uint32_t offset)
{
	device_t dev = sc->bce_dev;

	pci_write_config(dev, BCE_PCICFG_REG_WINDOW_ADDRESS, offset, 4);
	return pci_read_config(dev, BCE_PCICFG_REG_WINDOW, 4);
}

/****************************************************************************/
/* Indirect register write.                                                 */
/*                                                                          */
/* Writes NetXtreme II registers using an index/data register pair in PCI   */
/* configuration space.  Using this mechanism avoids issues with posted     */
/* writes but is muchh slower than memory-mapped I/O.                       */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_reg_wr_ind(struct bce_softc *sc, uint32_t offset, uint32_t val)
{
	device_t dev = sc->bce_dev;

	pci_write_config(dev, BCE_PCICFG_REG_WINDOW_ADDRESS, offset, 4);
	pci_write_config(dev, BCE_PCICFG_REG_WINDOW, val, 4);
}

/****************************************************************************/
/* Shared memory write.                                                     */
/*                                                                          */
/* Writes NetXtreme II shared memory region.                                */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_shmem_wr(struct bce_softc *sc, uint32_t offset, uint32_t val)
{
	bce_reg_wr_ind(sc, sc->bce_shmem_base + offset, val);
}

/****************************************************************************/
/* Shared memory read.                                                      */
/*                                                                          */
/* Reads NetXtreme II shared memory region.                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   The 32 bit value read.                                                 */
/****************************************************************************/
static u32
bce_shmem_rd(struct bce_softc *sc, uint32_t offset)
{
	return bce_reg_rd_ind(sc, sc->bce_shmem_base + offset);
}

/****************************************************************************/
/* Context memory write.                                                    */
/*                                                                          */
/* The NetXtreme II controller uses context memory to track connection      */
/* information for L2 and higher network protocols.                         */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_ctx_wr(struct bce_softc *sc, uint32_t cid_addr, uint32_t ctx_offset,
    uint32_t ctx_val)
{
	uint32_t idx, offset = ctx_offset + cid_addr;
	uint32_t val, retry_cnt = 5;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		REG_WR(sc, BCE_CTX_CTX_DATA, ctx_val);
		REG_WR(sc, BCE_CTX_CTX_CTRL, (offset | BCE_CTX_CTX_CTRL_WRITE_REQ));

		for (idx = 0; idx < retry_cnt; idx++) {
			val = REG_RD(sc, BCE_CTX_CTX_CTRL);
			if ((val & BCE_CTX_CTX_CTRL_WRITE_REQ) == 0)
				break;
			DELAY(5);
		}

		if (val & BCE_CTX_CTX_CTRL_WRITE_REQ) {
			device_printf(sc->bce_dev,
			    "Unable to write CTX memory: "
			    "cid_addr = 0x%08X, offset = 0x%08X!\n",
			    cid_addr, ctx_offset);
		}
	} else {
		REG_WR(sc, BCE_CTX_DATA_ADR, offset);
		REG_WR(sc, BCE_CTX_DATA, ctx_val);
	}
}

/****************************************************************************/
/* PHY register read.                                                       */
/*                                                                          */
/* Implements register reads on the MII bus.                                */
/*                                                                          */
/* Returns:                                                                 */
/*   The value of the register.                                             */
/****************************************************************************/
static int
bce_miibus_read_reg(device_t dev, int phy, int reg)
{
	struct bce_softc *sc = device_get_softc(dev);
	uint32_t val;
	int i;

	/* Make sure we are accessing the correct PHY address. */
	KASSERT(phy == sc->bce_phy_addr,
	    ("invalid phyno %d, should be %d\n", phy, sc->bce_phy_addr));

	if (sc->bce_phy_flags & BCE_PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val = REG_RD(sc, BCE_EMAC_MDIO_MODE);
		val &= ~BCE_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(sc, BCE_EMAC_MDIO_MODE, val);
		REG_RD(sc, BCE_EMAC_MDIO_MODE);

		DELAY(40);
	}

	val = BCE_MIPHY(phy) | BCE_MIREG(reg) |
	      BCE_EMAC_MDIO_COMM_COMMAND_READ | BCE_EMAC_MDIO_COMM_DISEXT |
	      BCE_EMAC_MDIO_COMM_START_BUSY;
	REG_WR(sc, BCE_EMAC_MDIO_COMM, val);

	for (i = 0; i < BCE_PHY_TIMEOUT; i++) {
		DELAY(10);

		val = REG_RD(sc, BCE_EMAC_MDIO_COMM);
		if (!(val & BCE_EMAC_MDIO_COMM_START_BUSY)) {
			DELAY(5);

			val = REG_RD(sc, BCE_EMAC_MDIO_COMM);
			val &= BCE_EMAC_MDIO_COMM_DATA;
			break;
		}
	}

	if (val & BCE_EMAC_MDIO_COMM_START_BUSY) {
		if_printf(&sc->arpcom.ac_if,
			  "Error: PHY read timeout! phy = %d, reg = 0x%04X\n",
			  phy, reg);
		val = 0x0;
	} else {
		val = REG_RD(sc, BCE_EMAC_MDIO_COMM);
	}

	if (sc->bce_phy_flags & BCE_PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val = REG_RD(sc, BCE_EMAC_MDIO_MODE);
		val |= BCE_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(sc, BCE_EMAC_MDIO_MODE, val);
		REG_RD(sc, BCE_EMAC_MDIO_MODE);

		DELAY(40);
	}
	return (val & 0xffff);
}

/****************************************************************************/
/* PHY register write.                                                      */
/*                                                                          */
/* Implements register writes on the MII bus.                               */
/*                                                                          */
/* Returns:                                                                 */
/*   The value of the register.                                             */
/****************************************************************************/
static int
bce_miibus_write_reg(device_t dev, int phy, int reg, int val)
{
	struct bce_softc *sc = device_get_softc(dev);
	uint32_t val1;
	int i;

	/* Make sure we are accessing the correct PHY address. */
	KASSERT(phy == sc->bce_phy_addr,
	    ("invalid phyno %d, should be %d\n", phy, sc->bce_phy_addr));

	if (sc->bce_phy_flags & BCE_PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(sc, BCE_EMAC_MDIO_MODE);
		val1 &= ~BCE_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(sc, BCE_EMAC_MDIO_MODE, val1);
		REG_RD(sc, BCE_EMAC_MDIO_MODE);

		DELAY(40);
	}

	val1 = BCE_MIPHY(phy) | BCE_MIREG(reg) | val |
		BCE_EMAC_MDIO_COMM_COMMAND_WRITE |
		BCE_EMAC_MDIO_COMM_START_BUSY | BCE_EMAC_MDIO_COMM_DISEXT;
	REG_WR(sc, BCE_EMAC_MDIO_COMM, val1);

	for (i = 0; i < BCE_PHY_TIMEOUT; i++) {
		DELAY(10);

		val1 = REG_RD(sc, BCE_EMAC_MDIO_COMM);
		if (!(val1 & BCE_EMAC_MDIO_COMM_START_BUSY)) {
			DELAY(5);
			break;
		}
	}

	if (val1 & BCE_EMAC_MDIO_COMM_START_BUSY)
		if_printf(&sc->arpcom.ac_if, "PHY write timeout!\n");

	if (sc->bce_phy_flags & BCE_PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(sc, BCE_EMAC_MDIO_MODE);
		val1 |= BCE_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(sc, BCE_EMAC_MDIO_MODE, val1);
		REG_RD(sc, BCE_EMAC_MDIO_MODE);

		DELAY(40);
	}
	return 0;
}

/****************************************************************************/
/* MII bus status change.                                                   */
/*                                                                          */
/* Called by the MII bus driver when the PHY establishes link to set the    */
/* MAC interface registers.                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_miibus_statchg(device_t dev)
{
	struct bce_softc *sc = device_get_softc(dev);
	struct mii_data *mii = device_get_softc(sc->bce_miibus);

	BCE_CLRBIT(sc, BCE_EMAC_MODE, BCE_EMAC_MODE_PORT);

	/*
	 * Set MII or GMII interface based on the speed negotiated
	 * by the PHY.
	 */
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T || 
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX) {
		BCE_SETBIT(sc, BCE_EMAC_MODE, BCE_EMAC_MODE_PORT_GMII);
	} else {
		BCE_SETBIT(sc, BCE_EMAC_MODE, BCE_EMAC_MODE_PORT_MII);
	}

	/*
	 * Set half or full duplex based on the duplicity negotiated
	 * by the PHY.
	 */
	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		BCE_CLRBIT(sc, BCE_EMAC_MODE, BCE_EMAC_MODE_HALF_DUPLEX);
	} else {
		BCE_SETBIT(sc, BCE_EMAC_MODE, BCE_EMAC_MODE_HALF_DUPLEX);
	}
}

/****************************************************************************/
/* Acquire NVRAM lock.                                                      */
/*                                                                          */
/* Before the NVRAM can be accessed the caller must acquire an NVRAM lock.  */
/* Locks 0 and 2 are reserved, lock 1 is used by firmware and lock 2 is     */
/* for use by the driver.                                                   */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_acquire_nvram_lock(struct bce_softc *sc)
{
	uint32_t val;
	int j;

	/* Request access to the flash interface. */
	REG_WR(sc, BCE_NVM_SW_ARB, BCE_NVM_SW_ARB_ARB_REQ_SET2);
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(sc, BCE_NVM_SW_ARB);
		if (val & BCE_NVM_SW_ARB_ARB_ARB2)
			break;

		DELAY(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT) {
		return EBUSY;
	}
	return 0;
}

/****************************************************************************/
/* Release NVRAM lock.                                                      */
/*                                                                          */
/* When the caller is finished accessing NVRAM the lock must be released.   */
/* Locks 0 and 2 are reserved, lock 1 is used by firmware and lock 2 is     */
/* for use by the driver.                                                   */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_release_nvram_lock(struct bce_softc *sc)
{
	int j;
	uint32_t val;

	/*
	 * Relinquish nvram interface.
	 */
	REG_WR(sc, BCE_NVM_SW_ARB, BCE_NVM_SW_ARB_ARB_REQ_CLR2);

	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(sc, BCE_NVM_SW_ARB);
		if (!(val & BCE_NVM_SW_ARB_ARB_ARB2))
			break;

		DELAY(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT) {
		return EBUSY;
	}
	return 0;
}

/****************************************************************************/
/* Enable NVRAM access.                                                     */
/*                                                                          */
/* Before accessing NVRAM for read or write operations the caller must      */
/* enabled NVRAM access.                                                    */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_enable_nvram_access(struct bce_softc *sc)
{
	uint32_t val;

	val = REG_RD(sc, BCE_NVM_ACCESS_ENABLE);
	/* Enable both bits, even on read. */
	REG_WR(sc, BCE_NVM_ACCESS_ENABLE,
	       val | BCE_NVM_ACCESS_ENABLE_EN | BCE_NVM_ACCESS_ENABLE_WR_EN);
}

/****************************************************************************/
/* Disable NVRAM access.                                                    */
/*                                                                          */
/* When the caller is finished accessing NVRAM access must be disabled.     */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_disable_nvram_access(struct bce_softc *sc)
{
	uint32_t val;

	val = REG_RD(sc, BCE_NVM_ACCESS_ENABLE);

	/* Disable both bits, even after read. */
	REG_WR(sc, BCE_NVM_ACCESS_ENABLE,
	       val & ~(BCE_NVM_ACCESS_ENABLE_EN | BCE_NVM_ACCESS_ENABLE_WR_EN));
}

/****************************************************************************/
/* Read a dword (32 bits) from NVRAM.                                       */
/*                                                                          */
/* Read a 32 bit word from NVRAM.  The caller is assumed to have already    */
/* obtained the NVRAM lock and enabled the controller for NVRAM access.     */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success and the 32 bit value read, positive value on failure.     */
/****************************************************************************/
static int
bce_nvram_read_dword(struct bce_softc *sc, uint32_t offset, uint8_t *ret_val,
		     uint32_t cmd_flags)
{
	uint32_t cmd;
	int i, rc = 0;

	/* Build the command word. */
	cmd = BCE_NVM_COMMAND_DOIT | cmd_flags;

	/* Calculate the offset for buffered flash. */
	if (sc->bce_flash_info->flags & BCE_NV_TRANSLATE) {
		offset = ((offset / sc->bce_flash_info->page_size) <<
			  sc->bce_flash_info->page_bits) +
			 (offset % sc->bce_flash_info->page_size);
	}

	/*
	 * Clear the DONE bit separately, set the address to read,
	 * and issue the read.
	 */
	REG_WR(sc, BCE_NVM_COMMAND, BCE_NVM_COMMAND_DONE);
	REG_WR(sc, BCE_NVM_ADDR, offset & BCE_NVM_ADDR_NVM_ADDR_VALUE);
	REG_WR(sc, BCE_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (i = 0; i < NVRAM_TIMEOUT_COUNT; i++) {
		uint32_t val;

		DELAY(5);

		val = REG_RD(sc, BCE_NVM_COMMAND);
		if (val & BCE_NVM_COMMAND_DONE) {
			val = REG_RD(sc, BCE_NVM_READ);

			val = be32toh(val);
			memcpy(ret_val, &val, 4);
			break;
		}
	}

	/* Check for errors. */
	if (i >= NVRAM_TIMEOUT_COUNT) {
		if_printf(&sc->arpcom.ac_if,
			  "Timeout error reading NVRAM at offset 0x%08X!\n",
			  offset);
		rc = EBUSY;
	}
	return rc;
}

/****************************************************************************/
/* Initialize NVRAM access.                                                 */
/*                                                                          */
/* Identify the NVRAM device in use and prepare the NVRAM interface to      */
/* access that device.                                                      */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_init_nvram(struct bce_softc *sc)
{
	uint32_t val;
	int j, entry_count, rc = 0;
	const struct flash_spec *flash;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		sc->bce_flash_info = &flash_5709;
		goto bce_init_nvram_get_flash_size;
	}

	/* Determine the selected interface. */
	val = REG_RD(sc, BCE_NVM_CFG1);

	entry_count = sizeof(flash_table) / sizeof(struct flash_spec);

	/*
	 * Flash reconfiguration is required to support additional
	 * NVRAM devices not directly supported in hardware.
	 * Check if the flash interface was reconfigured
	 * by the bootcode.
	 */

	if (val & 0x40000000) {
		/* Flash interface reconfigured by bootcode. */
		for (j = 0, flash = flash_table; j < entry_count;
		     j++, flash++) {
			if ((val & FLASH_BACKUP_STRAP_MASK) ==
			    (flash->config1 & FLASH_BACKUP_STRAP_MASK)) {
				sc->bce_flash_info = flash;
				break;
			}
		}
	} else {
		/* Flash interface not yet reconfigured. */
		uint32_t mask;

		if (val & (1 << 23))
			mask = FLASH_BACKUP_STRAP_MASK;
		else
			mask = FLASH_STRAP_MASK;

		/* Look for the matching NVRAM device configuration data. */
		for (j = 0, flash = flash_table; j < entry_count;
		     j++, flash++) {
			/* Check if the device matches any of the known devices. */
			if ((val & mask) == (flash->strapping & mask)) {
				/* Found a device match. */
				sc->bce_flash_info = flash;

				/* Request access to the flash interface. */
				rc = bce_acquire_nvram_lock(sc);
				if (rc != 0)
					return rc;

				/* Reconfigure the flash interface. */
				bce_enable_nvram_access(sc);
				REG_WR(sc, BCE_NVM_CFG1, flash->config1);
				REG_WR(sc, BCE_NVM_CFG2, flash->config2);
				REG_WR(sc, BCE_NVM_CFG3, flash->config3);
				REG_WR(sc, BCE_NVM_WRITE1, flash->write1);
				bce_disable_nvram_access(sc);
				bce_release_nvram_lock(sc);
				break;
			}
		}
	}

	/* Check if a matching device was found. */
	if (j == entry_count) {
		sc->bce_flash_info = NULL;
		if_printf(&sc->arpcom.ac_if, "Unknown Flash NVRAM found!\n");
		return ENODEV;
	}

bce_init_nvram_get_flash_size:
	/* Write the flash config data to the shared memory interface. */
	val = bce_shmem_rd(sc, BCE_SHARED_HW_CFG_CONFIG2) &
	    BCE_SHARED_HW_CFG2_NVM_SIZE_MASK;
	if (val)
		sc->bce_flash_size = val;
	else
		sc->bce_flash_size = sc->bce_flash_info->total_size;

	return rc;
}

/****************************************************************************/
/* Read an arbitrary range of data from NVRAM.                              */
/*                                                                          */
/* Prepares the NVRAM interface for access and reads the requested data     */
/* into the supplied buffer.                                                */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success and the data read, positive value on failure.             */
/****************************************************************************/
static int
bce_nvram_read(struct bce_softc *sc, uint32_t offset, uint8_t *ret_buf,
	       int buf_size)
{
	uint32_t cmd_flags, offset32, len32, extra;
	int rc = 0;

	if (buf_size == 0)
		return 0;

	/* Request access to the flash interface. */
	rc = bce_acquire_nvram_lock(sc);
	if (rc != 0)
		return rc;

	/* Enable access to flash interface */
	bce_enable_nvram_access(sc);

	len32 = buf_size;
	offset32 = offset;
	extra = 0;

	cmd_flags = 0;

	/* XXX should we release nvram lock if read_dword() fails? */
	if (offset32 & 3) {
		uint8_t buf[4];
		uint32_t pre_len;

		offset32 &= ~3;
		pre_len = 4 - (offset & 3);

		if (pre_len >= len32) {
			pre_len = len32;
			cmd_flags = BCE_NVM_COMMAND_FIRST | BCE_NVM_COMMAND_LAST;
		} else {
			cmd_flags = BCE_NVM_COMMAND_FIRST;
		}

		rc = bce_nvram_read_dword(sc, offset32, buf, cmd_flags);
		if (rc)
			return rc;

		memcpy(ret_buf, buf + (offset & 3), pre_len);

		offset32 += 4;
		ret_buf += pre_len;
		len32 -= pre_len;
	}

	if (len32 & 3) {
		extra = 4 - (len32 & 3);
		len32 = (len32 + 4) & ~3;
	}

	if (len32 == 4) {
		uint8_t buf[4];

		if (cmd_flags)
			cmd_flags = BCE_NVM_COMMAND_LAST;
		else
			cmd_flags = BCE_NVM_COMMAND_FIRST |
				    BCE_NVM_COMMAND_LAST;

		rc = bce_nvram_read_dword(sc, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	} else if (len32 > 0) {
		uint8_t buf[4];

		/* Read the first word. */
		if (cmd_flags)
			cmd_flags = 0;
		else
			cmd_flags = BCE_NVM_COMMAND_FIRST;

		rc = bce_nvram_read_dword(sc, offset32, ret_buf, cmd_flags);

		/* Advance to the next dword. */
		offset32 += 4;
		ret_buf += 4;
		len32 -= 4;

		while (len32 > 4 && rc == 0) {
			rc = bce_nvram_read_dword(sc, offset32, ret_buf, 0);

			/* Advance to the next dword. */
			offset32 += 4;
			ret_buf += 4;
			len32 -= 4;
		}

		if (rc)
			goto bce_nvram_read_locked_exit;

		cmd_flags = BCE_NVM_COMMAND_LAST;
		rc = bce_nvram_read_dword(sc, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	}

bce_nvram_read_locked_exit:
	/* Disable access to flash interface and release the lock. */
	bce_disable_nvram_access(sc);
	bce_release_nvram_lock(sc);

	return rc;
}

/****************************************************************************/
/* Verifies that NVRAM is accessible and contains valid data.               */
/*                                                                          */
/* Reads the configuration data from NVRAM and verifies that the CRC is     */
/* correct.                                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   0 on success, positive value on failure.                               */
/****************************************************************************/
static int
bce_nvram_test(struct bce_softc *sc)
{
	uint32_t buf[BCE_NVRAM_SIZE / 4];
	uint32_t magic, csum;
	uint8_t *data = (uint8_t *)buf;
	int rc = 0;

	/*
	 * Check that the device NVRAM is valid by reading
	 * the magic value at offset 0.
	 */
	rc = bce_nvram_read(sc, 0, data, 4);
	if (rc != 0)
		return rc;

	magic = be32toh(buf[0]);
	if (magic != BCE_NVRAM_MAGIC) {
		if_printf(&sc->arpcom.ac_if,
			  "Invalid NVRAM magic value! Expected: 0x%08X, "
			  "Found: 0x%08X\n", BCE_NVRAM_MAGIC, magic);
		return ENODEV;
	}

	/*
	 * Verify that the device NVRAM includes valid
	 * configuration data.
	 */
	rc = bce_nvram_read(sc, 0x100, data, BCE_NVRAM_SIZE);
	if (rc != 0)
		return rc;

	csum = ether_crc32_le(data, 0x100);
	if (csum != BCE_CRC32_RESIDUAL) {
		if_printf(&sc->arpcom.ac_if,
			  "Invalid Manufacturing Information NVRAM CRC! "
			  "Expected: 0x%08X, Found: 0x%08X\n",
			  BCE_CRC32_RESIDUAL, csum);
		return ENODEV;
	}

	csum = ether_crc32_le(data + 0x100, 0x100);
	if (csum != BCE_CRC32_RESIDUAL) {
		if_printf(&sc->arpcom.ac_if,
			  "Invalid Feature Configuration Information "
			  "NVRAM CRC! Expected: 0x%08X, Found: 08%08X\n",
			  BCE_CRC32_RESIDUAL, csum);
		rc = ENODEV;
	}
	return rc;
}

/****************************************************************************/
/* Identifies the current media type of the controller and sets the PHY     */
/* address.                                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_get_media(struct bce_softc *sc)
{
	uint32_t val;

	sc->bce_phy_addr = 1;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
 		uint32_t val = REG_RD(sc, BCE_MISC_DUAL_MEDIA_CTRL);
		uint32_t bond_id = val & BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID;
		uint32_t strap;

		/*
		 * The BCM5709S is software configurable
		 * for Copper or SerDes operation.
		 */
		if (bond_id == BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID_C) {
			return;
		} else if (bond_id == BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID_S) {
			sc->bce_phy_flags |= BCE_PHY_SERDES_FLAG;
			return;
		}

		if (val & BCE_MISC_DUAL_MEDIA_CTRL_STRAP_OVERRIDE) {
			strap = (val & BCE_MISC_DUAL_MEDIA_CTRL_PHY_CTRL) >> 21;
		} else {
			strap =
			(val & BCE_MISC_DUAL_MEDIA_CTRL_PHY_CTRL_STRAP) >> 8;
		}

		if (pci_get_function(sc->bce_dev) == 0) {
			switch (strap) {
			case 0x4:
			case 0x5:
			case 0x6:
				sc->bce_phy_flags |= BCE_PHY_SERDES_FLAG;
				break;
			}
		} else {
			switch (strap) {
			case 0x1:
			case 0x2:
			case 0x4:
				sc->bce_phy_flags |= BCE_PHY_SERDES_FLAG;
				break;
			}
		}
	} else if (BCE_CHIP_BOND_ID(sc) & BCE_CHIP_BOND_ID_SERDES_BIT) {
		sc->bce_phy_flags |= BCE_PHY_SERDES_FLAG;
	}

	if (sc->bce_phy_flags & BCE_PHY_SERDES_FLAG) {
		sc->bce_flags |= BCE_NO_WOL_FLAG;
		if (BCE_CHIP_NUM(sc) != BCE_CHIP_NUM_5706) {
			sc->bce_phy_addr = 2;
			val = bce_shmem_rd(sc, BCE_SHARED_HW_CFG_CONFIG);
			if (val & BCE_SHARED_HW_CFG_PHY_2_5G)
				sc->bce_phy_flags |= BCE_PHY_2_5G_CAPABLE_FLAG;
		}
	} else if ((BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5706) ||
	    (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5708)) {
		sc->bce_phy_flags |= BCE_PHY_CRC_FIX_FLAG;
	}
}

static void
bce_destroy_tx_ring(struct bce_tx_ring *txr)
{
	int i;

	/* Destroy the TX buffer descriptor DMA stuffs. */
	if (txr->tx_bd_chain_tag != NULL) {
		for (i = 0; i < txr->tx_pages; i++) {
			if (txr->tx_bd_chain[i] != NULL) {
				bus_dmamap_unload(txr->tx_bd_chain_tag,
				    txr->tx_bd_chain_map[i]);
				bus_dmamem_free(txr->tx_bd_chain_tag,
				    txr->tx_bd_chain[i],
				    txr->tx_bd_chain_map[i]);
			}
		}
		bus_dma_tag_destroy(txr->tx_bd_chain_tag);
	}

	/* Destroy the TX mbuf DMA stuffs. */
	if (txr->tx_mbuf_tag != NULL) {
		for (i = 0; i < TOTAL_TX_BD(txr); i++) {
			/* Must have been unloaded in bce_stop() */
			KKASSERT(txr->tx_bufs[i].tx_mbuf_ptr == NULL);
			bus_dmamap_destroy(txr->tx_mbuf_tag,
			    txr->tx_bufs[i].tx_mbuf_map);
		}
		bus_dma_tag_destroy(txr->tx_mbuf_tag);
	}

	if (txr->tx_bd_chain_map != NULL)
		kfree(txr->tx_bd_chain_map, M_DEVBUF);
	if (txr->tx_bd_chain != NULL)
		kfree(txr->tx_bd_chain, M_DEVBUF);
	if (txr->tx_bd_chain_paddr != NULL)
		kfree(txr->tx_bd_chain_paddr, M_DEVBUF);

	if (txr->tx_bufs != NULL)
		kfree(txr->tx_bufs, M_DEVBUF);
}

static void
bce_destroy_rx_ring(struct bce_rx_ring *rxr)
{
	int i;

	/* Destroy the RX buffer descriptor DMA stuffs. */
	if (rxr->rx_bd_chain_tag != NULL) {
		for (i = 0; i < rxr->rx_pages; i++) {
			if (rxr->rx_bd_chain[i] != NULL) {
				bus_dmamap_unload(rxr->rx_bd_chain_tag,
				    rxr->rx_bd_chain_map[i]);
				bus_dmamem_free(rxr->rx_bd_chain_tag,
				    rxr->rx_bd_chain[i],
				    rxr->rx_bd_chain_map[i]);
			}
		}
		bus_dma_tag_destroy(rxr->rx_bd_chain_tag);
	}

	/* Destroy the RX mbuf DMA stuffs. */
	if (rxr->rx_mbuf_tag != NULL) {
		for (i = 0; i < TOTAL_RX_BD(rxr); i++) {
			/* Must have been unloaded in bce_stop() */
			KKASSERT(rxr->rx_bufs[i].rx_mbuf_ptr == NULL);
			bus_dmamap_destroy(rxr->rx_mbuf_tag,
			    rxr->rx_bufs[i].rx_mbuf_map);
		}
		bus_dmamap_destroy(rxr->rx_mbuf_tag, rxr->rx_mbuf_tmpmap);
		bus_dma_tag_destroy(rxr->rx_mbuf_tag);
	}

	if (rxr->rx_bd_chain_map != NULL)
		kfree(rxr->rx_bd_chain_map, M_DEVBUF);
	if (rxr->rx_bd_chain != NULL)
		kfree(rxr->rx_bd_chain, M_DEVBUF);
	if (rxr->rx_bd_chain_paddr != NULL)
		kfree(rxr->rx_bd_chain_paddr, M_DEVBUF);

	if (rxr->rx_bufs != NULL)
		kfree(rxr->rx_bufs, M_DEVBUF);
}

/****************************************************************************/
/* Free any DMA memory owned by the driver.                                 */
/*                                                                          */
/* Scans through each data structre that requires DMA memory and frees      */
/* the memory if allocated.                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_dma_free(struct bce_softc *sc)
{
	int i;

	/* Destroy the status block. */
	if (sc->status_tag != NULL) {
		if (sc->status_block != NULL) {
			bus_dmamap_unload(sc->status_tag, sc->status_map);
			bus_dmamem_free(sc->status_tag, sc->status_block,
					sc->status_map);
		}
		bus_dma_tag_destroy(sc->status_tag);
	}

	/* Destroy the statistics block. */
	if (sc->stats_tag != NULL) {
		if (sc->stats_block != NULL) {
			bus_dmamap_unload(sc->stats_tag, sc->stats_map);
			bus_dmamem_free(sc->stats_tag, sc->stats_block,
					sc->stats_map);
		}
		bus_dma_tag_destroy(sc->stats_tag);
	}

	/* Destroy the CTX DMA stuffs. */
	if (sc->ctx_tag != NULL) {
		for (i = 0; i < sc->ctx_pages; i++) {
			if (sc->ctx_block[i] != NULL) {
				bus_dmamap_unload(sc->ctx_tag, sc->ctx_map[i]);
				bus_dmamem_free(sc->ctx_tag, sc->ctx_block[i],
						sc->ctx_map[i]);
			}
		}
		bus_dma_tag_destroy(sc->ctx_tag);
	}

	/* Free TX rings */
	if (sc->tx_rings != NULL) {
		for (i = 0; i < sc->tx_ring_cnt; ++i)
			bce_destroy_tx_ring(&sc->tx_rings[i]);
		kfree(sc->tx_rings, M_DEVBUF);
	}

	/* Free RX rings */
	if (sc->rx_rings != NULL) {
		for (i = 0; i < sc->rx_ring_cnt; ++i)
			bce_destroy_rx_ring(&sc->rx_rings[i]);
		kfree(sc->rx_rings, M_DEVBUF);
	}

	/* Destroy the parent tag */
	if (sc->parent_tag != NULL)
		bus_dma_tag_destroy(sc->parent_tag);
}

/****************************************************************************/
/* Get DMA memory from the OS.                                              */
/*                                                                          */
/* Validates that the OS has provided DMA buffers in response to a          */
/* bus_dmamap_load() call and saves the physical address of those buffers.  */
/* When the callback is used the OS will return 0 for the mapping function  */
/* (bus_dmamap_load()) so we use the value of map_arg->maxsegs to pass any  */
/* failures back to the caller.                                             */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *busaddr = arg;

	/* Check for an error and signal the caller that an error occurred. */
	if (error)
		return;

	KASSERT(nseg == 1, ("only one segment is allowed"));
	*busaddr = segs->ds_addr;
}

static int
bce_create_tx_ring(struct bce_tx_ring *txr)
{
	int pages, rc, i;

	lwkt_serialize_init(&txr->tx_serialize);
	txr->tx_wreg = bce_tx_wreg;

	pages = device_getenv_int(txr->sc->bce_dev, "tx_pages", bce_tx_pages);
	if (pages <= 0 || pages > TX_PAGES_MAX || !powerof2(pages)) {
		device_printf(txr->sc->bce_dev, "invalid # of TX pages\n");
		pages = TX_PAGES_DEFAULT;
	}
	txr->tx_pages = pages;

	txr->tx_bd_chain_map = kmalloc(sizeof(bus_dmamap_t) * txr->tx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	txr->tx_bd_chain = kmalloc(sizeof(struct tx_bd *) * txr->tx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	txr->tx_bd_chain_paddr = kmalloc(sizeof(bus_addr_t) * txr->tx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	txr->tx_bufs = kmalloc(sizeof(struct bce_tx_buf) * TOTAL_TX_BD(txr),
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create a DMA tag for the TX buffer descriptor chain,
	 * allocate and clear the  memory, and fetch the
	 * physical address of the block.
	 */
	rc = bus_dma_tag_create(txr->sc->parent_tag, BCM_PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    BCE_TX_CHAIN_PAGE_SZ, 1, BCE_TX_CHAIN_PAGE_SZ,
	    0, &txr->tx_bd_chain_tag);
	if (rc != 0) {
		device_printf(txr->sc->bce_dev, "Could not allocate "
		    "TX descriptor chain DMA tag!\n");
		return rc;
	}

	for (i = 0; i < txr->tx_pages; i++) {
		bus_addr_t busaddr;

		rc = bus_dmamem_alloc(txr->tx_bd_chain_tag,
		    (void **)&txr->tx_bd_chain[i],
		    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
		    &txr->tx_bd_chain_map[i]);
		if (rc != 0) {
			device_printf(txr->sc->bce_dev,
			    "Could not allocate %dth TX descriptor "
			    "chain DMA memory!\n", i);
			return rc;
		}

		rc = bus_dmamap_load(txr->tx_bd_chain_tag,
		    txr->tx_bd_chain_map[i],
		    txr->tx_bd_chain[i],
		    BCE_TX_CHAIN_PAGE_SZ,
		    bce_dma_map_addr, &busaddr,
		    BUS_DMA_WAITOK);
		if (rc != 0) {
			if (rc == EINPROGRESS) {
				panic("%s coherent memory loading "
				    "is still in progress!",
				    txr->sc->arpcom.ac_if.if_xname);
			}
			device_printf(txr->sc->bce_dev, "Could not map %dth "
			    "TX descriptor chain DMA memory!\n", i);
			bus_dmamem_free(txr->tx_bd_chain_tag,
			    txr->tx_bd_chain[i],
			    txr->tx_bd_chain_map[i]);
			txr->tx_bd_chain[i] = NULL;
			return rc;
		}

		txr->tx_bd_chain_paddr[i] = busaddr;
	}

	/* Create a DMA tag for TX mbufs. */
	rc = bus_dma_tag_create(txr->sc->parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    IP_MAXPACKET + sizeof(struct ether_vlan_header),
	    BCE_MAX_SEGMENTS, PAGE_SIZE,
	    BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
	    &txr->tx_mbuf_tag);
	if (rc != 0) {
		device_printf(txr->sc->bce_dev,
		    "Could not allocate TX mbuf DMA tag!\n");
		return rc;
	}

	/* Create DMA maps for the TX mbufs clusters. */
	for (i = 0; i < TOTAL_TX_BD(txr); i++) {
		rc = bus_dmamap_create(txr->tx_mbuf_tag,
		    BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
		    &txr->tx_bufs[i].tx_mbuf_map);
		if (rc != 0) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(txr->tx_mbuf_tag,
				    txr->tx_bufs[j].tx_mbuf_map);
			}
			bus_dma_tag_destroy(txr->tx_mbuf_tag);
			txr->tx_mbuf_tag = NULL;

			device_printf(txr->sc->bce_dev, "Unable to create "
			    "%dth TX mbuf DMA map!\n", i);
			return rc;
		}
	}
	return 0;
}

static int
bce_create_rx_ring(struct bce_rx_ring *rxr)
{
	int pages, rc, i;

	lwkt_serialize_init(&rxr->rx_serialize);

	pages = device_getenv_int(rxr->sc->bce_dev, "rx_pages", bce_rx_pages);
	if (pages <= 0 || pages > RX_PAGES_MAX || !powerof2(pages)) {
		device_printf(rxr->sc->bce_dev, "invalid # of RX pages\n");
		pages = RX_PAGES_DEFAULT;
	}
	rxr->rx_pages = pages;

	rxr->rx_bd_chain_map = kmalloc(sizeof(bus_dmamap_t) * rxr->rx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	rxr->rx_bd_chain = kmalloc(sizeof(struct rx_bd *) * rxr->rx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	rxr->rx_bd_chain_paddr = kmalloc(sizeof(bus_addr_t) * rxr->rx_pages,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	rxr->rx_bufs = kmalloc(sizeof(struct bce_rx_buf) * TOTAL_RX_BD(rxr),
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);

	/*
	 * Create a DMA tag for the RX buffer descriptor chain,
	 * allocate and clear the  memory, and fetch the physical
	 * address of the blocks.
	 */
	rc = bus_dma_tag_create(rxr->sc->parent_tag, BCM_PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    BCE_RX_CHAIN_PAGE_SZ, 1, BCE_RX_CHAIN_PAGE_SZ,
	    0, &rxr->rx_bd_chain_tag);
	if (rc != 0) {
		device_printf(rxr->sc->bce_dev, "Could not allocate "
		    "RX descriptor chain DMA tag!\n");
		return rc;
	}

	for (i = 0; i < rxr->rx_pages; i++) {
		bus_addr_t busaddr;

		rc = bus_dmamem_alloc(rxr->rx_bd_chain_tag,
		    (void **)&rxr->rx_bd_chain[i],
		    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
		    &rxr->rx_bd_chain_map[i]);
		if (rc != 0) {
			device_printf(rxr->sc->bce_dev,
			    "Could not allocate %dth RX descriptor "
			    "chain DMA memory!\n", i);
			return rc;
		}

		rc = bus_dmamap_load(rxr->rx_bd_chain_tag,
		    rxr->rx_bd_chain_map[i],
		    rxr->rx_bd_chain[i],
		    BCE_RX_CHAIN_PAGE_SZ,
		    bce_dma_map_addr, &busaddr,
		    BUS_DMA_WAITOK);
		if (rc != 0) {
			if (rc == EINPROGRESS) {
				panic("%s coherent memory loading "
				    "is still in progress!",
				    rxr->sc->arpcom.ac_if.if_xname);
			}
			device_printf(rxr->sc->bce_dev,
			    "Could not map %dth RX descriptor "
			    "chain DMA memory!\n", i);
			bus_dmamem_free(rxr->rx_bd_chain_tag,
			    rxr->rx_bd_chain[i],
			    rxr->rx_bd_chain_map[i]);
			rxr->rx_bd_chain[i] = NULL;
			return rc;
		}

		rxr->rx_bd_chain_paddr[i] = busaddr;
	}

	/* Create a DMA tag for RX mbufs. */
	rc = bus_dma_tag_create(rxr->sc->parent_tag, BCE_DMA_RX_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    MCLBYTES, 1, MCLBYTES,
	    BUS_DMA_ALLOCNOW | BUS_DMA_ALIGNED | BUS_DMA_WAITOK,
	    &rxr->rx_mbuf_tag);
	if (rc != 0) {
		device_printf(rxr->sc->bce_dev,
		    "Could not allocate RX mbuf DMA tag!\n");
		return rc;
	}

	/* Create tmp DMA map for RX mbuf clusters. */
	rc = bus_dmamap_create(rxr->rx_mbuf_tag, BUS_DMA_WAITOK,
	    &rxr->rx_mbuf_tmpmap);
	if (rc != 0) {
		bus_dma_tag_destroy(rxr->rx_mbuf_tag);
		rxr->rx_mbuf_tag = NULL;

		device_printf(rxr->sc->bce_dev,
		    "Could not create RX mbuf tmp DMA map!\n");
		return rc;
	}

	/* Create DMA maps for the RX mbuf clusters. */
	for (i = 0; i < TOTAL_RX_BD(rxr); i++) {
		rc = bus_dmamap_create(rxr->rx_mbuf_tag, BUS_DMA_WAITOK,
		    &rxr->rx_bufs[i].rx_mbuf_map);
		if (rc != 0) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(rxr->rx_mbuf_tag,
				    rxr->rx_bufs[j].rx_mbuf_map);
			}
			bus_dma_tag_destroy(rxr->rx_mbuf_tag);
			rxr->rx_mbuf_tag = NULL;

			device_printf(rxr->sc->bce_dev, "Unable to create "
			    "%dth RX mbuf DMA map!\n", i);
			return rc;
		}
	}
	return 0;
}

/****************************************************************************/
/* Allocate any DMA memory needed by the driver.                            */
/*                                                                          */
/* Allocates DMA memory needed for the various global structures needed by  */
/* hardware.                                                                */
/*                                                                          */
/* Memory alignment requirements:                                           */
/* -----------------+----------+----------+----------+----------+           */
/*  Data Structure  |   5706   |   5708   |   5709   |   5716   |           */
/* -----------------+----------+----------+----------+----------+           */
/* Status Block     | 8 bytes  | 8 bytes  | 16 bytes | 16 bytes |           */
/* Statistics Block | 8 bytes  | 8 bytes  | 16 bytes | 16 bytes |           */
/* RX Buffers       | 16 bytes | 16 bytes | 16 bytes | 16 bytes |           */
/* PG Buffers       |   none   |   none   |   none   |   none   |           */
/* TX Buffers       |   none   |   none   |   none   |   none   |           */
/* Chain Pages(1)   |   4KiB   |   4KiB   |   4KiB   |   4KiB   |           */
/* Context Pages(1) |   N/A    |   N/A    |   4KiB   |   4KiB   |           */
/* -----------------+----------+----------+----------+----------+           */
/*                                                                          */
/* (1) Must align with CPU page size (BCM_PAGE_SZIE).                       */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_dma_alloc(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i, rc = 0;
	bus_addr_t busaddr, max_busaddr;
	bus_size_t status_align, stats_align, status_size;

	/*
	 * The embedded PCIe to PCI-X bridge (EPB) 
	 * in the 5708 cannot address memory above 
	 * 40 bits (E7_5708CB1_23043 & E6_5708SB1_23043). 
	 */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5708)
		max_busaddr = BCE_BUS_SPACE_MAXADDR;
	else
		max_busaddr = BUS_SPACE_MAXADDR;

	/*
	 * BCM5709 and BCM5716 uses host memory as cache for context memory.
	 */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		sc->ctx_pages = BCE_CTX_BLK_SZ / BCM_PAGE_SIZE;
		if (sc->ctx_pages == 0)
			sc->ctx_pages = 1;
		if (sc->ctx_pages > BCE_CTX_PAGES) {
			device_printf(sc->bce_dev, "excessive ctx pages %d\n",
			    sc->ctx_pages);
			return ENOMEM;
		}
		status_align = 16;
		stats_align = 16;
	} else {
		status_align = 8;
		stats_align = 8;
	}

	/*
	 * Each MSI-X vector needs a status block; each status block
	 * consumes 128bytes and is 128bytes aligned.
	 */
	if (sc->rx_ring_cnt > 1) {
		status_size = BCE_MSIX_MAX * BCE_STATUS_BLK_MSIX_ALIGN;
		status_align = BCE_STATUS_BLK_MSIX_ALIGN;
	} else {
		status_size = BCE_STATUS_BLK_SZ;
	}

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	rc = bus_dma_tag_create(NULL, 1, BCE_DMA_BOUNDARY,
				max_busaddr, BUS_SPACE_MAXADDR,
				BUS_SPACE_MAXSIZE_32BIT, 0,
				BUS_SPACE_MAXSIZE_32BIT,
				0, &sc->parent_tag);
	if (rc != 0) {
		if_printf(ifp, "Could not allocate parent DMA tag!\n");
		return rc;
	}

	/*
	 * Allocate status block.
	 */
	sc->status_block = bus_dmamem_coherent_any(sc->parent_tag,
				status_align, status_size,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&sc->status_tag, &sc->status_map,
				&sc->status_block_paddr);
	if (sc->status_block == NULL) {
		if_printf(ifp, "Could not allocate status block!\n");
		return ENOMEM;
	}

	/*
	 * Allocate statistics block.
	 */
	sc->stats_block = bus_dmamem_coherent_any(sc->parent_tag,
				stats_align, BCE_STATS_BLK_SZ,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&sc->stats_tag, &sc->stats_map,
				&sc->stats_block_paddr);
	if (sc->stats_block == NULL) {
		if_printf(ifp, "Could not allocate statistics block!\n");
		return ENOMEM;
	}

	/*
	 * Allocate context block, if needed
	 */
	if (sc->ctx_pages != 0) {
		rc = bus_dma_tag_create(sc->parent_tag, BCM_PAGE_SIZE, 0,
					BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
					BCM_PAGE_SIZE, 1, BCM_PAGE_SIZE,
					0, &sc->ctx_tag);
		if (rc != 0) {
			if_printf(ifp, "Could not allocate "
				  "context block DMA tag!\n");
			return rc;
		}

		for (i = 0; i < sc->ctx_pages; i++) {
			rc = bus_dmamem_alloc(sc->ctx_tag,
					      (void **)&sc->ctx_block[i],
					      BUS_DMA_WAITOK | BUS_DMA_ZERO |
					      BUS_DMA_COHERENT,
					      &sc->ctx_map[i]);
			if (rc != 0) {
				if_printf(ifp, "Could not allocate %dth context "
					  "DMA memory!\n", i);
				return rc;
			}

			rc = bus_dmamap_load(sc->ctx_tag, sc->ctx_map[i],
					     sc->ctx_block[i], BCM_PAGE_SIZE,
					     bce_dma_map_addr, &busaddr,
					     BUS_DMA_WAITOK);
			if (rc != 0) {
				if (rc == EINPROGRESS) {
					panic("%s coherent memory loading "
					      "is still in progress!", ifp->if_xname);
				}
				if_printf(ifp, "Could not map %dth context "
					  "DMA memory!\n", i);
				bus_dmamem_free(sc->ctx_tag, sc->ctx_block[i],
						sc->ctx_map[i]);
				sc->ctx_block[i] = NULL;
				return rc;
			}
			sc->ctx_paddr[i] = busaddr;
		}
	}

	sc->tx_rings = kmalloc(sizeof(struct bce_tx_ring) * sc->tx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		sc->tx_rings[i].sc = sc;
		if (i == 0) {
			sc->tx_rings[i].tx_cid = TX_CID;
			sc->tx_rings[i].tx_hw_cons =
			    &sc->status_block->status_tx_quick_consumer_index0;
		} else {
			struct status_block_msix *sblk =
			    (struct status_block_msix *)
			    (((uint8_t *)(sc->status_block)) +
			     (i * BCE_STATUS_BLK_MSIX_ALIGN));

			sc->tx_rings[i].tx_cid = TX_TSS_CID + i - 1;
			sc->tx_rings[i].tx_hw_cons =
			    &sblk->status_tx_quick_consumer_index;
		}

		rc = bce_create_tx_ring(&sc->tx_rings[i]);
		if (rc != 0) {
			device_printf(sc->bce_dev,
			    "can't create %dth tx ring\n", i);
			return rc;
		}
	}

	sc->rx_rings = kmalloc(sizeof(struct bce_rx_ring) * sc->rx_ring_cnt,
			       M_DEVBUF,
			       M_WAITOK | M_ZERO | M_CACHEALIGN);
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		sc->rx_rings[i].sc = sc;
		sc->rx_rings[i].idx = i;
		if (i == 0) {
			sc->rx_rings[i].rx_cid = RX_CID;
			sc->rx_rings[i].rx_hw_cons =
			    &sc->status_block->status_rx_quick_consumer_index0;
			sc->rx_rings[i].hw_status_idx =
			    &sc->status_block->status_idx;
		} else {
			struct status_block_msix *sblk =
			    (struct status_block_msix *)
			    (((uint8_t *)(sc->status_block)) +
			     (i * BCE_STATUS_BLK_MSIX_ALIGN));

			sc->rx_rings[i].rx_cid = RX_RSS_CID + i - 1;
			sc->rx_rings[i].rx_hw_cons =
			    &sblk->status_rx_quick_consumer_index;
			sc->rx_rings[i].hw_status_idx = &sblk->status_idx;
		}

		rc = bce_create_rx_ring(&sc->rx_rings[i]);
		if (rc != 0) {
			device_printf(sc->bce_dev,
			    "can't create %dth rx ring\n", i);
			return rc;
		}
	}

	return 0;
}

/****************************************************************************/
/* Firmware synchronization.                                                */
/*                                                                          */
/* Before performing certain events such as a chip reset, synchronize with  */
/* the firmware first.                                                      */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_fw_sync(struct bce_softc *sc, uint32_t msg_data)
{
	int i, rc = 0;
	uint32_t val;

	/* Don't waste any time if we've timed out before. */
	if (sc->bce_fw_timed_out)
		return EBUSY;

	/* Increment the message sequence number. */
	sc->bce_fw_wr_seq++;
	msg_data |= sc->bce_fw_wr_seq;

	/* Send the message to the bootcode driver mailbox. */
	bce_shmem_wr(sc, BCE_DRV_MB, msg_data);

	/* Wait for the bootcode to acknowledge the message. */
	for (i = 0; i < FW_ACK_TIME_OUT_MS; i++) {
		/* Check for a response in the bootcode firmware mailbox. */
		val = bce_shmem_rd(sc, BCE_FW_MB);
		if ((val & BCE_FW_MSG_ACK) == (msg_data & BCE_DRV_MSG_SEQ))
			break;
		DELAY(1000);
	}

	/* If we've timed out, tell the bootcode that we've stopped waiting. */
	if ((val & BCE_FW_MSG_ACK) != (msg_data & BCE_DRV_MSG_SEQ) &&
	    (msg_data & BCE_DRV_MSG_DATA) != BCE_DRV_MSG_DATA_WAIT0) {
		if_printf(&sc->arpcom.ac_if,
			  "Firmware synchronization timeout! "
			  "msg_data = 0x%08X\n", msg_data);

		msg_data &= ~BCE_DRV_MSG_CODE;
		msg_data |= BCE_DRV_MSG_CODE_FW_TIMEOUT;

		bce_shmem_wr(sc, BCE_DRV_MB, msg_data);

		sc->bce_fw_timed_out = 1;
		rc = EBUSY;
	}
	return rc;
}

/****************************************************************************/
/* Load Receive Virtual 2 Physical (RV2P) processor firmware.               */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_load_rv2p_fw(struct bce_softc *sc, uint32_t *rv2p_code,
		 uint32_t rv2p_code_len, uint32_t rv2p_proc)
{
	int i;
	uint32_t val;

	for (i = 0; i < rv2p_code_len; i += 8) {
		REG_WR(sc, BCE_RV2P_INSTR_HIGH, *rv2p_code);
		rv2p_code++;
		REG_WR(sc, BCE_RV2P_INSTR_LOW, *rv2p_code);
		rv2p_code++;

		if (rv2p_proc == RV2P_PROC1) {
			val = (i / 8) | BCE_RV2P_PROC1_ADDR_CMD_RDWR;
			REG_WR(sc, BCE_RV2P_PROC1_ADDR_CMD, val);
		} else {
			val = (i / 8) | BCE_RV2P_PROC2_ADDR_CMD_RDWR;
			REG_WR(sc, BCE_RV2P_PROC2_ADDR_CMD, val);
		}
	}

	/* Reset the processor, un-stall is done later. */
	if (rv2p_proc == RV2P_PROC1)
		REG_WR(sc, BCE_RV2P_COMMAND, BCE_RV2P_COMMAND_PROC1_RESET);
	else
		REG_WR(sc, BCE_RV2P_COMMAND, BCE_RV2P_COMMAND_PROC2_RESET);
}

/****************************************************************************/
/* Load RISC processor firmware.                                            */
/*                                                                          */
/* Loads firmware from the file if_bcefw.h into the scratchpad memory       */
/* associated with a particular processor.                                  */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_load_cpu_fw(struct bce_softc *sc, struct cpu_reg *cpu_reg,
		struct fw_info *fw)
{
	uint32_t offset;
	int j;

	bce_halt_cpu(sc, cpu_reg);

	/* Load the Text area. */
	offset = cpu_reg->spad_base + (fw->text_addr - cpu_reg->mips_view_base);
	if (fw->text) {
		for (j = 0; j < (fw->text_len / 4); j++, offset += 4)
			REG_WR_IND(sc, offset, fw->text[j]);
	}

	/* Load the Data area. */
	offset = cpu_reg->spad_base + (fw->data_addr - cpu_reg->mips_view_base);
	if (fw->data) {
		for (j = 0; j < (fw->data_len / 4); j++, offset += 4)
			REG_WR_IND(sc, offset, fw->data[j]);
	}

	/* Load the SBSS area. */
	offset = cpu_reg->spad_base + (fw->sbss_addr - cpu_reg->mips_view_base);
	if (fw->sbss) {
		for (j = 0; j < (fw->sbss_len / 4); j++, offset += 4)
			REG_WR_IND(sc, offset, fw->sbss[j]);
	}

	/* Load the BSS area. */
	offset = cpu_reg->spad_base + (fw->bss_addr - cpu_reg->mips_view_base);
	if (fw->bss) {
		for (j = 0; j < (fw->bss_len/4); j++, offset += 4)
			REG_WR_IND(sc, offset, fw->bss[j]);
	}

	/* Load the Read-Only area. */
	offset = cpu_reg->spad_base +
		(fw->rodata_addr - cpu_reg->mips_view_base);
	if (fw->rodata) {
		for (j = 0; j < (fw->rodata_len / 4); j++, offset += 4)
			REG_WR_IND(sc, offset, fw->rodata[j]);
	}

	/* Clear the pre-fetch instruction and set the FW start address. */
	REG_WR_IND(sc, cpu_reg->inst, 0);
	REG_WR_IND(sc, cpu_reg->pc, fw->start_addr);
}

/****************************************************************************/
/* Starts the RISC processor.                                               */
/*                                                                          */
/* Assumes the CPU starting address has already been set.                   */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_start_cpu(struct bce_softc *sc, struct cpu_reg *cpu_reg)
{
	uint32_t val;

	/* Start the CPU. */
	val = REG_RD_IND(sc, cpu_reg->mode);
	val &= ~cpu_reg->mode_value_halt;
	REG_WR_IND(sc, cpu_reg->state, cpu_reg->state_value_clear);
	REG_WR_IND(sc, cpu_reg->mode, val);
}

/****************************************************************************/
/* Halts the RISC processor.                                                */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_halt_cpu(struct bce_softc *sc, struct cpu_reg *cpu_reg)
{
	uint32_t val;

	/* Halt the CPU. */
	val = REG_RD_IND(sc, cpu_reg->mode);
	val |= cpu_reg->mode_value_halt;
	REG_WR_IND(sc, cpu_reg->mode, val);
	REG_WR_IND(sc, cpu_reg->state, cpu_reg->state_value_clear);
}

/****************************************************************************/
/* Start the RX CPU.                                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_start_rxp_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;

	cpu_reg.mode = BCE_RXP_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_RXP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_RXP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_RXP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_RXP_CPU_REG_FILE;
	cpu_reg.evmask = BCE_RXP_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_RXP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_RXP_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_RXP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_RXP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	bce_start_cpu(sc, &cpu_reg);
}

/****************************************************************************/
/* Initialize the RX CPU.                                                   */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_rxp_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	cpu_reg.mode = BCE_RXP_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_RXP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_RXP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_RXP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_RXP_CPU_REG_FILE;
	cpu_reg.evmask = BCE_RXP_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_RXP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_RXP_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_RXP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_RXP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
 		fw.ver_major = bce_RXP_b09FwReleaseMajor;
		fw.ver_minor = bce_RXP_b09FwReleaseMinor;
		fw.ver_fix = bce_RXP_b09FwReleaseFix;
		fw.start_addr = bce_RXP_b09FwStartAddr;

		fw.text_addr = bce_RXP_b09FwTextAddr;
		fw.text_len = bce_RXP_b09FwTextLen;
		fw.text_index = 0;
		fw.text = bce_RXP_b09FwText;

		fw.data_addr = bce_RXP_b09FwDataAddr;
		fw.data_len = bce_RXP_b09FwDataLen;
		fw.data_index = 0;
		fw.data = bce_RXP_b09FwData;

		fw.sbss_addr = bce_RXP_b09FwSbssAddr;
		fw.sbss_len = bce_RXP_b09FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_RXP_b09FwSbss;

		fw.bss_addr = bce_RXP_b09FwBssAddr;
		fw.bss_len = bce_RXP_b09FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_RXP_b09FwBss;

		fw.rodata_addr = bce_RXP_b09FwRodataAddr;
		fw.rodata_len = bce_RXP_b09FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_RXP_b09FwRodata;
	} else {
		fw.ver_major = bce_RXP_b06FwReleaseMajor;
		fw.ver_minor = bce_RXP_b06FwReleaseMinor;
		fw.ver_fix = bce_RXP_b06FwReleaseFix;
		fw.start_addr = bce_RXP_b06FwStartAddr;

		fw.text_addr = bce_RXP_b06FwTextAddr;
		fw.text_len = bce_RXP_b06FwTextLen;
		fw.text_index = 0;
		fw.text = bce_RXP_b06FwText;

		fw.data_addr = bce_RXP_b06FwDataAddr;
		fw.data_len = bce_RXP_b06FwDataLen;
		fw.data_index = 0;
		fw.data = bce_RXP_b06FwData;

		fw.sbss_addr = bce_RXP_b06FwSbssAddr;
		fw.sbss_len = bce_RXP_b06FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_RXP_b06FwSbss;

		fw.bss_addr = bce_RXP_b06FwBssAddr;
		fw.bss_len = bce_RXP_b06FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_RXP_b06FwBss;

		fw.rodata_addr = bce_RXP_b06FwRodataAddr;
		fw.rodata_len = bce_RXP_b06FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_RXP_b06FwRodata;
	}

	bce_load_cpu_fw(sc, &cpu_reg, &fw);
	/* Delay RXP start until initialization is complete. */
}

/****************************************************************************/
/* Initialize the TX CPU.                                                   */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_txp_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	cpu_reg.mode = BCE_TXP_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_TXP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_TXP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_TXP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_TXP_CPU_REG_FILE;
	cpu_reg.evmask = BCE_TXP_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_TXP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_TXP_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_TXP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_TXP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		fw.ver_major = bce_TXP_b09FwReleaseMajor;
		fw.ver_minor = bce_TXP_b09FwReleaseMinor;
		fw.ver_fix = bce_TXP_b09FwReleaseFix;
		fw.start_addr = bce_TXP_b09FwStartAddr;

		fw.text_addr = bce_TXP_b09FwTextAddr;
		fw.text_len = bce_TXP_b09FwTextLen;
		fw.text_index = 0;
		fw.text = bce_TXP_b09FwText;

		fw.data_addr = bce_TXP_b09FwDataAddr;
		fw.data_len = bce_TXP_b09FwDataLen;
		fw.data_index = 0;
		fw.data = bce_TXP_b09FwData;

		fw.sbss_addr = bce_TXP_b09FwSbssAddr;
		fw.sbss_len = bce_TXP_b09FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_TXP_b09FwSbss;

		fw.bss_addr = bce_TXP_b09FwBssAddr;
		fw.bss_len = bce_TXP_b09FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_TXP_b09FwBss;

		fw.rodata_addr = bce_TXP_b09FwRodataAddr;
		fw.rodata_len = bce_TXP_b09FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_TXP_b09FwRodata;
	} else {
		fw.ver_major = bce_TXP_b06FwReleaseMajor;
		fw.ver_minor = bce_TXP_b06FwReleaseMinor;
		fw.ver_fix = bce_TXP_b06FwReleaseFix;
		fw.start_addr = bce_TXP_b06FwStartAddr;

		fw.text_addr = bce_TXP_b06FwTextAddr;
		fw.text_len = bce_TXP_b06FwTextLen;
		fw.text_index = 0;
		fw.text = bce_TXP_b06FwText;

		fw.data_addr = bce_TXP_b06FwDataAddr;
		fw.data_len = bce_TXP_b06FwDataLen;
		fw.data_index = 0;
		fw.data = bce_TXP_b06FwData;

		fw.sbss_addr = bce_TXP_b06FwSbssAddr;
		fw.sbss_len = bce_TXP_b06FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_TXP_b06FwSbss;

		fw.bss_addr = bce_TXP_b06FwBssAddr;
		fw.bss_len = bce_TXP_b06FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_TXP_b06FwBss;

		fw.rodata_addr = bce_TXP_b06FwRodataAddr;
		fw.rodata_len = bce_TXP_b06FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_TXP_b06FwRodata;
	}

	bce_load_cpu_fw(sc, &cpu_reg, &fw);
	bce_start_cpu(sc, &cpu_reg);
}

/****************************************************************************/
/* Initialize the TPAT CPU.                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_tpat_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	cpu_reg.mode = BCE_TPAT_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_TPAT_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_TPAT_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_TPAT_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_TPAT_CPU_REG_FILE;
	cpu_reg.evmask = BCE_TPAT_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_TPAT_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_TPAT_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_TPAT_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_TPAT_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		fw.ver_major = bce_TPAT_b09FwReleaseMajor;
		fw.ver_minor = bce_TPAT_b09FwReleaseMinor;
		fw.ver_fix = bce_TPAT_b09FwReleaseFix;
		fw.start_addr = bce_TPAT_b09FwStartAddr;

		fw.text_addr = bce_TPAT_b09FwTextAddr;
		fw.text_len = bce_TPAT_b09FwTextLen;
		fw.text_index = 0;
		fw.text = bce_TPAT_b09FwText;

		fw.data_addr = bce_TPAT_b09FwDataAddr;
		fw.data_len = bce_TPAT_b09FwDataLen;
		fw.data_index = 0;
		fw.data = bce_TPAT_b09FwData;

		fw.sbss_addr = bce_TPAT_b09FwSbssAddr;
		fw.sbss_len = bce_TPAT_b09FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_TPAT_b09FwSbss;

		fw.bss_addr = bce_TPAT_b09FwBssAddr;
		fw.bss_len = bce_TPAT_b09FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_TPAT_b09FwBss;

		fw.rodata_addr = bce_TPAT_b09FwRodataAddr;
		fw.rodata_len = bce_TPAT_b09FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_TPAT_b09FwRodata;
	} else {
		fw.ver_major = bce_TPAT_b06FwReleaseMajor;
		fw.ver_minor = bce_TPAT_b06FwReleaseMinor;
		fw.ver_fix = bce_TPAT_b06FwReleaseFix;
		fw.start_addr = bce_TPAT_b06FwStartAddr;

		fw.text_addr = bce_TPAT_b06FwTextAddr;
		fw.text_len = bce_TPAT_b06FwTextLen;
		fw.text_index = 0;
		fw.text = bce_TPAT_b06FwText;

		fw.data_addr = bce_TPAT_b06FwDataAddr;
		fw.data_len = bce_TPAT_b06FwDataLen;
		fw.data_index = 0;
		fw.data = bce_TPAT_b06FwData;

		fw.sbss_addr = bce_TPAT_b06FwSbssAddr;
		fw.sbss_len = bce_TPAT_b06FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_TPAT_b06FwSbss;

		fw.bss_addr = bce_TPAT_b06FwBssAddr;
		fw.bss_len = bce_TPAT_b06FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_TPAT_b06FwBss;

		fw.rodata_addr = bce_TPAT_b06FwRodataAddr;
		fw.rodata_len = bce_TPAT_b06FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_TPAT_b06FwRodata;
	}

	bce_load_cpu_fw(sc, &cpu_reg, &fw);
	bce_start_cpu(sc, &cpu_reg);
}

/****************************************************************************/
/* Initialize the CP CPU.                                                   */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_cp_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	cpu_reg.mode = BCE_CP_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_CP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_CP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_CP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_CP_CPU_REG_FILE;
	cpu_reg.evmask = BCE_CP_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_CP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_CP_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_CP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_CP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		fw.ver_major = bce_CP_b09FwReleaseMajor;
		fw.ver_minor = bce_CP_b09FwReleaseMinor;
		fw.ver_fix = bce_CP_b09FwReleaseFix;
		fw.start_addr = bce_CP_b09FwStartAddr;

		fw.text_addr = bce_CP_b09FwTextAddr;
		fw.text_len = bce_CP_b09FwTextLen;
		fw.text_index = 0;
		fw.text = bce_CP_b09FwText;

		fw.data_addr = bce_CP_b09FwDataAddr;
		fw.data_len = bce_CP_b09FwDataLen;
		fw.data_index = 0;
		fw.data = bce_CP_b09FwData;

		fw.sbss_addr = bce_CP_b09FwSbssAddr;
		fw.sbss_len = bce_CP_b09FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_CP_b09FwSbss;

		fw.bss_addr = bce_CP_b09FwBssAddr;
		fw.bss_len = bce_CP_b09FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_CP_b09FwBss;

		fw.rodata_addr = bce_CP_b09FwRodataAddr;
		fw.rodata_len = bce_CP_b09FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_CP_b09FwRodata;
	} else {
		fw.ver_major = bce_CP_b06FwReleaseMajor;
		fw.ver_minor = bce_CP_b06FwReleaseMinor;
		fw.ver_fix = bce_CP_b06FwReleaseFix;
		fw.start_addr = bce_CP_b06FwStartAddr;

		fw.text_addr = bce_CP_b06FwTextAddr;
		fw.text_len = bce_CP_b06FwTextLen;
		fw.text_index = 0;
		fw.text = bce_CP_b06FwText;

		fw.data_addr = bce_CP_b06FwDataAddr;
		fw.data_len = bce_CP_b06FwDataLen;
		fw.data_index = 0;
		fw.data = bce_CP_b06FwData;

		fw.sbss_addr = bce_CP_b06FwSbssAddr;
		fw.sbss_len = bce_CP_b06FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_CP_b06FwSbss;

		fw.bss_addr = bce_CP_b06FwBssAddr;
		fw.bss_len = bce_CP_b06FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_CP_b06FwBss;

		fw.rodata_addr = bce_CP_b06FwRodataAddr;
		fw.rodata_len = bce_CP_b06FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_CP_b06FwRodata;
	}

	bce_load_cpu_fw(sc, &cpu_reg, &fw);
	bce_start_cpu(sc, &cpu_reg);
}

/****************************************************************************/
/* Initialize the COM CPU.                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_com_cpu(struct bce_softc *sc)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	cpu_reg.mode = BCE_COM_CPU_MODE;
	cpu_reg.mode_value_halt = BCE_COM_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BCE_COM_CPU_MODE_STEP_ENA;
	cpu_reg.state = BCE_COM_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BCE_COM_CPU_REG_FILE;
	cpu_reg.evmask = BCE_COM_CPU_EVENT_MASK;
	cpu_reg.pc = BCE_COM_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BCE_COM_CPU_INSTRUCTION;
	cpu_reg.bp = BCE_COM_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BCE_COM_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;

	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		fw.ver_major = bce_COM_b09FwReleaseMajor;
		fw.ver_minor = bce_COM_b09FwReleaseMinor;
		fw.ver_fix = bce_COM_b09FwReleaseFix;
		fw.start_addr = bce_COM_b09FwStartAddr;

		fw.text_addr = bce_COM_b09FwTextAddr;
		fw.text_len = bce_COM_b09FwTextLen;
		fw.text_index = 0;
		fw.text = bce_COM_b09FwText;

		fw.data_addr = bce_COM_b09FwDataAddr;
		fw.data_len = bce_COM_b09FwDataLen;
		fw.data_index = 0;
		fw.data = bce_COM_b09FwData;

		fw.sbss_addr = bce_COM_b09FwSbssAddr;
		fw.sbss_len = bce_COM_b09FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_COM_b09FwSbss;

		fw.bss_addr = bce_COM_b09FwBssAddr;
		fw.bss_len = bce_COM_b09FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_COM_b09FwBss;

		fw.rodata_addr = bce_COM_b09FwRodataAddr;
		fw.rodata_len = bce_COM_b09FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_COM_b09FwRodata;
	} else {
		fw.ver_major = bce_COM_b06FwReleaseMajor;
		fw.ver_minor = bce_COM_b06FwReleaseMinor;
		fw.ver_fix = bce_COM_b06FwReleaseFix;
		fw.start_addr = bce_COM_b06FwStartAddr;

		fw.text_addr = bce_COM_b06FwTextAddr;
		fw.text_len = bce_COM_b06FwTextLen;
		fw.text_index = 0;
		fw.text = bce_COM_b06FwText;

		fw.data_addr = bce_COM_b06FwDataAddr;
		fw.data_len = bce_COM_b06FwDataLen;
		fw.data_index = 0;
		fw.data = bce_COM_b06FwData;

		fw.sbss_addr = bce_COM_b06FwSbssAddr;
		fw.sbss_len = bce_COM_b06FwSbssLen;
		fw.sbss_index = 0;
		fw.sbss = bce_COM_b06FwSbss;

		fw.bss_addr = bce_COM_b06FwBssAddr;
		fw.bss_len = bce_COM_b06FwBssLen;
		fw.bss_index = 0;
		fw.bss = bce_COM_b06FwBss;

		fw.rodata_addr = bce_COM_b06FwRodataAddr;
		fw.rodata_len = bce_COM_b06FwRodataLen;
		fw.rodata_index = 0;
		fw.rodata = bce_COM_b06FwRodata;
	}

	bce_load_cpu_fw(sc, &cpu_reg, &fw);
	bce_start_cpu(sc, &cpu_reg);
}

/****************************************************************************/
/* Initialize the RV2P, RX, TX, TPAT, COM, and CP CPUs.                     */
/*                                                                          */
/* Loads the firmware for each CPU and starts the CPU.                      */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init_cpus(struct bce_softc *sc)
{
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		if (BCE_CHIP_REV(sc) == BCE_CHIP_REV_Ax) {
			bce_load_rv2p_fw(sc, bce_xi90_rv2p_proc1,
			    sizeof(bce_xi90_rv2p_proc1), RV2P_PROC1);
			bce_load_rv2p_fw(sc, bce_xi90_rv2p_proc2,
			    sizeof(bce_xi90_rv2p_proc2), RV2P_PROC2);
		} else {
			bce_load_rv2p_fw(sc, bce_xi_rv2p_proc1,
			    sizeof(bce_xi_rv2p_proc1), RV2P_PROC1);
			bce_load_rv2p_fw(sc, bce_xi_rv2p_proc2,
			    sizeof(bce_xi_rv2p_proc2), RV2P_PROC2);
		}
	} else {
		bce_load_rv2p_fw(sc, bce_rv2p_proc1,
		    sizeof(bce_rv2p_proc1), RV2P_PROC1);
		bce_load_rv2p_fw(sc, bce_rv2p_proc2,
		    sizeof(bce_rv2p_proc2), RV2P_PROC2);
	}

	bce_init_rxp_cpu(sc);
	bce_init_txp_cpu(sc);
	bce_init_tpat_cpu(sc);
	bce_init_com_cpu(sc);
	bce_init_cp_cpu(sc);
}

/****************************************************************************/
/* Initialize context memory.                                               */
/*                                                                          */
/* Clears the memory associated with each Context ID (CID).                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static int
bce_init_ctx(struct bce_softc *sc)
{
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		/* DRC: Replace this constant value with a #define. */
		int i, retry_cnt = 10;
		uint32_t val;

		/*
		 * BCM5709 context memory may be cached
		 * in host memory so prepare the host memory
		 * for access.
		 */
		val = BCE_CTX_COMMAND_ENABLED | BCE_CTX_COMMAND_MEM_INIT |
		    (1 << 12);
		val |= (BCM_PAGE_BITS - 8) << 16;
		REG_WR(sc, BCE_CTX_COMMAND, val);

		/* Wait for mem init command to complete. */
		for (i = 0; i < retry_cnt; i++) {
			val = REG_RD(sc, BCE_CTX_COMMAND);
			if (!(val & BCE_CTX_COMMAND_MEM_INIT))
				break;
			DELAY(2);
		}
		if (i == retry_cnt) {
			device_printf(sc->bce_dev,
			    "Context memory initialization failed!\n");
			return ETIMEDOUT;
		}

		for (i = 0; i < sc->ctx_pages; i++) {
			int j;

			/*
			 * Set the physical address of the context
			 * memory cache.
			 */
			REG_WR(sc, BCE_CTX_HOST_PAGE_TBL_DATA0,
			    BCE_ADDR_LO(sc->ctx_paddr[i] & 0xfffffff0) |
			    BCE_CTX_HOST_PAGE_TBL_DATA0_VALID);
			REG_WR(sc, BCE_CTX_HOST_PAGE_TBL_DATA1,
			    BCE_ADDR_HI(sc->ctx_paddr[i]));
			REG_WR(sc, BCE_CTX_HOST_PAGE_TBL_CTRL,
			    i | BCE_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ);

			/*
			 * Verify that the context memory write was successful.
			 */
			for (j = 0; j < retry_cnt; j++) {
				val = REG_RD(sc, BCE_CTX_HOST_PAGE_TBL_CTRL);
				if ((val &
				    BCE_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ) == 0)
					break;
				DELAY(5);
			}
			if (j == retry_cnt) {
				device_printf(sc->bce_dev,
				    "Failed to initialize context page!\n");
				return ETIMEDOUT;
			}
		}
	} else {
		uint32_t vcid_addr, offset;

		/*
		 * For the 5706/5708, context memory is local to
		 * the controller, so initialize the controller
		 * context memory.
		 */

		vcid_addr = GET_CID_ADDR(96);
		while (vcid_addr) {
			vcid_addr -= PHY_CTX_SIZE;

			REG_WR(sc, BCE_CTX_VIRT_ADDR, 0);
			REG_WR(sc, BCE_CTX_PAGE_TBL, vcid_addr);

			for (offset = 0; offset < PHY_CTX_SIZE; offset += 4)
				CTX_WR(sc, 0x00, offset, 0);

			REG_WR(sc, BCE_CTX_VIRT_ADDR, vcid_addr);
			REG_WR(sc, BCE_CTX_PAGE_TBL, vcid_addr);
		}
	}
	return 0;
}

/****************************************************************************/
/* Fetch the permanent MAC address of the controller.                       */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_get_mac_addr(struct bce_softc *sc)
{
	uint32_t mac_lo = 0, mac_hi = 0;

	/*
	 * The NetXtreme II bootcode populates various NIC
	 * power-on and runtime configuration items in a
	 * shared memory area.  The factory configured MAC
	 * address is available from both NVRAM and the
	 * shared memory area so we'll read the value from
	 * shared memory for speed.
	 */

	mac_hi = bce_shmem_rd(sc,  BCE_PORT_HW_CFG_MAC_UPPER);
	mac_lo = bce_shmem_rd(sc, BCE_PORT_HW_CFG_MAC_LOWER);

	if (mac_lo == 0 && mac_hi == 0) {
		if_printf(&sc->arpcom.ac_if, "Invalid Ethernet address!\n");
	} else {
		sc->eaddr[0] = (u_char)(mac_hi >> 8);
		sc->eaddr[1] = (u_char)(mac_hi >> 0);
		sc->eaddr[2] = (u_char)(mac_lo >> 24);
		sc->eaddr[3] = (u_char)(mac_lo >> 16);
		sc->eaddr[4] = (u_char)(mac_lo >> 8);
		sc->eaddr[5] = (u_char)(mac_lo >> 0);
	}
}

/****************************************************************************/
/* Program the MAC address.                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_set_mac_addr(struct bce_softc *sc)
{
	const uint8_t *mac_addr = sc->eaddr;
	uint32_t val;

	val = (mac_addr[0] << 8) | mac_addr[1];
	REG_WR(sc, BCE_EMAC_MAC_MATCH0, val);

	val = (mac_addr[2] << 24) |
	      (mac_addr[3] << 16) |
	      (mac_addr[4] << 8) |
	      mac_addr[5];
	REG_WR(sc, BCE_EMAC_MAC_MATCH1, val);
}

/****************************************************************************/
/* Stop the controller.                                                     */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_stop(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	callout_stop(&sc->bce_tick_callout);

	/* Disable the transmit/receive blocks. */
	REG_WR(sc, BCE_MISC_ENABLE_CLR_BITS, BCE_MISC_ENABLE_CLR_DEFAULT);
	REG_RD(sc, BCE_MISC_ENABLE_CLR_BITS);
	DELAY(20);

	bce_disable_intr(sc);

	ifp->if_flags &= ~IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		ifsq_clr_oactive(sc->tx_rings[i].ifsq);
		ifsq_watchdog_stop(&sc->tx_rings[i].tx_watchdog);
	}

	/* Free the RX lists. */
	for (i = 0; i < sc->rx_ring_cnt; ++i)
		bce_free_rx_chain(&sc->rx_rings[i]);

	/* Free TX buffers. */
	for (i = 0; i < sc->tx_ring_cnt; ++i)
		bce_free_tx_chain(&sc->tx_rings[i]);

	sc->bce_link = 0;
	sc->bce_coalchg_mask = 0;
}

static int
bce_reset(struct bce_softc *sc, uint32_t reset_code)
{
	uint32_t val;
	int i, rc = 0;

	/* Wait for pending PCI transactions to complete. */
	REG_WR(sc, BCE_MISC_ENABLE_CLR_BITS,
	       BCE_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE |
	       BCE_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE |
	       BCE_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE |
	       BCE_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE);
	val = REG_RD(sc, BCE_MISC_ENABLE_CLR_BITS);
	DELAY(5);

	/* Disable DMA */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		val = REG_RD(sc, BCE_MISC_NEW_CORE_CTL);
		val &= ~BCE_MISC_NEW_CORE_CTL_DMA_ENABLE;
		REG_WR(sc, BCE_MISC_NEW_CORE_CTL, val);
	}

	/* Assume bootcode is running. */
	sc->bce_fw_timed_out = 0;
	sc->bce_drv_cardiac_arrest = 0;

	/* Give the firmware a chance to prepare for the reset. */
	rc = bce_fw_sync(sc, BCE_DRV_MSG_DATA_WAIT0 | reset_code);
	if (rc) {
		if_printf(&sc->arpcom.ac_if,
			  "Firmware is not ready for reset\n");
		return rc;
	}

	/* Set a firmware reminder that this is a soft reset. */
	bce_shmem_wr(sc, BCE_DRV_RESET_SIGNATURE,
	    BCE_DRV_RESET_SIGNATURE_MAGIC);

	/* Dummy read to force the chip to complete all current transactions. */
	val = REG_RD(sc, BCE_MISC_ID);

	/* Chip reset. */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		REG_WR(sc, BCE_MISC_COMMAND, BCE_MISC_COMMAND_SW_RESET);
		REG_RD(sc, BCE_MISC_COMMAND);
		DELAY(5);

		val = BCE_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
		    BCE_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP;

		pci_write_config(sc->bce_dev, BCE_PCICFG_MISC_CONFIG, val, 4);
	} else {
		val = BCE_PCICFG_MISC_CONFIG_CORE_RST_REQ |
		    BCE_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
		    BCE_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP;
		REG_WR(sc, BCE_PCICFG_MISC_CONFIG, val);

		/* Allow up to 30us for reset to complete. */
		for (i = 0; i < 10; i++) {
			val = REG_RD(sc, BCE_PCICFG_MISC_CONFIG);
			if ((val & (BCE_PCICFG_MISC_CONFIG_CORE_RST_REQ |
			    BCE_PCICFG_MISC_CONFIG_CORE_RST_BSY)) == 0)
				break;
			DELAY(10);
		}

		/* Check that reset completed successfully. */
		if (val & (BCE_PCICFG_MISC_CONFIG_CORE_RST_REQ |
		    BCE_PCICFG_MISC_CONFIG_CORE_RST_BSY)) {
			if_printf(&sc->arpcom.ac_if, "Reset failed!\n");
			return EBUSY;
		}
	}

	/* Make sure byte swapping is properly configured. */
	val = REG_RD(sc, BCE_PCI_SWAP_DIAG0);
	if (val != 0x01020304) {
		if_printf(&sc->arpcom.ac_if, "Byte swap is incorrect!\n");
		return ENODEV;
	}

	/* Just completed a reset, assume that firmware is running again. */
	sc->bce_fw_timed_out = 0;
	sc->bce_drv_cardiac_arrest = 0;

	/* Wait for the firmware to finish its initialization. */
	rc = bce_fw_sync(sc, BCE_DRV_MSG_DATA_WAIT1 | reset_code);
	if (rc) {
		if_printf(&sc->arpcom.ac_if,
			  "Firmware did not complete initialization!\n");
	}

	if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX) {
		bce_setup_msix_table(sc);
		/* Prevent MSIX table reads and write from timing out */
		REG_WR(sc, BCE_MISC_ECO_HW_CTL,
		    BCE_MISC_ECO_HW_CTL_LARGE_GRC_TMOUT_EN);

	}
	return rc;
}

static int
bce_chipinit(struct bce_softc *sc)
{
	uint32_t val;
	int rc = 0;

	/* Make sure the interrupt is not active. */
	REG_WR(sc, BCE_PCICFG_INT_ACK_CMD, BCE_PCICFG_INT_ACK_CMD_MASK_INT);
	REG_RD(sc, BCE_PCICFG_INT_ACK_CMD);

	/*
	 * Initialize DMA byte/word swapping, configure the number of DMA
	 * channels and PCI clock compensation delay.
	 */
	val = BCE_DMA_CONFIG_DATA_BYTE_SWAP |
	      BCE_DMA_CONFIG_DATA_WORD_SWAP |
#if BYTE_ORDER == BIG_ENDIAN
	      BCE_DMA_CONFIG_CNTL_BYTE_SWAP |
#endif
	      BCE_DMA_CONFIG_CNTL_WORD_SWAP |
	      DMA_READ_CHANS << 12 |
	      DMA_WRITE_CHANS << 16;

	val |= (0x2 << 20) | BCE_DMA_CONFIG_CNTL_PCI_COMP_DLY;

	if ((sc->bce_flags & BCE_PCIX_FLAG) && sc->bus_speed_mhz == 133)
		val |= BCE_DMA_CONFIG_PCI_FAST_CLK_CMP;

	/*
	 * This setting resolves a problem observed on certain Intel PCI
	 * chipsets that cannot handle multiple outstanding DMA operations.
	 * See errata E9_5706A1_65.
	 */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5706 &&
	    BCE_CHIP_ID(sc) != BCE_CHIP_ID_5706_A0 &&
	    !(sc->bce_flags & BCE_PCIX_FLAG))
		val |= BCE_DMA_CONFIG_CNTL_PING_PONG_DMA;

	REG_WR(sc, BCE_DMA_CONFIG, val);

	/* Enable the RX_V2P and Context state machines before access. */
	REG_WR(sc, BCE_MISC_ENABLE_SET_BITS,
	       BCE_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE |
	       BCE_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE |
	       BCE_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE);

	/* Initialize context mapping and zero out the quick contexts. */
	rc = bce_init_ctx(sc);
	if (rc != 0)
		return rc;

	/* Initialize the on-boards CPUs */
	bce_init_cpus(sc);

	/* Enable management frames (NC-SI) to flow to the MCP. */
	if (sc->bce_flags & BCE_MFW_ENABLE_FLAG) {
		val = REG_RD(sc, BCE_RPM_MGMT_PKT_CTRL) |
		    BCE_RPM_MGMT_PKT_CTRL_MGMT_EN;
		REG_WR(sc, BCE_RPM_MGMT_PKT_CTRL, val);
	}

	/* Prepare NVRAM for access. */
	rc = bce_init_nvram(sc);
	if (rc != 0)
		return rc;

	/* Set the kernel bypass block size */
	val = REG_RD(sc, BCE_MQ_CONFIG);
	val &= ~BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE;
	val |= BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_256;

	/* Enable bins used on the 5709/5716. */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		val |= BCE_MQ_CONFIG_BIN_MQ_MODE;
		if (BCE_CHIP_ID(sc) == BCE_CHIP_ID_5709_A1)
			val |= BCE_MQ_CONFIG_HALT_DIS;
	}

	REG_WR(sc, BCE_MQ_CONFIG, val);

	val = 0x10000 + (MAX_CID_CNT * MB_KERNEL_CTX_SIZE);
	REG_WR(sc, BCE_MQ_KNL_BYP_WIND_START, val);
	REG_WR(sc, BCE_MQ_KNL_WIND_END, val);

	/* Set the page size and clear the RV2P processor stall bits. */
	val = (BCM_PAGE_BITS - 8) << 24;
	REG_WR(sc, BCE_RV2P_CONFIG, val);

	/* Configure page size. */
	val = REG_RD(sc, BCE_TBDR_CONFIG);
	val &= ~BCE_TBDR_CONFIG_PAGE_SIZE;
	val |= (BCM_PAGE_BITS - 8) << 24 | 0x40;
	REG_WR(sc, BCE_TBDR_CONFIG, val);

	/* Set the perfect match control register to default. */
	REG_WR_IND(sc, BCE_RXP_PM_CTRL, 0);

	return 0;
}

/****************************************************************************/
/* Initialize the controller in preparation to send/receive traffic.        */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_blockinit(struct bce_softc *sc)
{
	uint32_t reg, val;
	int i;

	/* Load the hardware default MAC address. */
	bce_set_mac_addr(sc);

	/* Set the Ethernet backoff seed value */
	val = sc->eaddr[0] + (sc->eaddr[1] << 8) + (sc->eaddr[2] << 16) +
	      sc->eaddr[3] + (sc->eaddr[4] << 8) + (sc->eaddr[5] << 16);
	REG_WR(sc, BCE_EMAC_BACKOFF_SEED, val);

	sc->rx_mode = BCE_EMAC_RX_MODE_SORT_MODE;

	/* Set up link change interrupt generation. */
	REG_WR(sc, BCE_EMAC_ATTENTION_ENA, BCE_EMAC_ATTENTION_ENA_LINK);

	/* Program the physical address of the status block. */
	REG_WR(sc, BCE_HC_STATUS_ADDR_L, BCE_ADDR_LO(sc->status_block_paddr));
	REG_WR(sc, BCE_HC_STATUS_ADDR_H, BCE_ADDR_HI(sc->status_block_paddr));

	/* Program the physical address of the statistics block. */
	REG_WR(sc, BCE_HC_STATISTICS_ADDR_L,
	       BCE_ADDR_LO(sc->stats_block_paddr));
	REG_WR(sc, BCE_HC_STATISTICS_ADDR_H,
	       BCE_ADDR_HI(sc->stats_block_paddr));

	/* Program various host coalescing parameters. */
	REG_WR(sc, BCE_HC_TX_QUICK_CONS_TRIP,
	       (sc->bce_tx_quick_cons_trip_int << 16) |
	       sc->bce_tx_quick_cons_trip);
	REG_WR(sc, BCE_HC_RX_QUICK_CONS_TRIP,
	       (sc->bce_rx_quick_cons_trip_int << 16) |
	       sc->bce_rx_quick_cons_trip);
	REG_WR(sc, BCE_HC_COMP_PROD_TRIP,
	       (sc->bce_comp_prod_trip_int << 16) | sc->bce_comp_prod_trip);
	REG_WR(sc, BCE_HC_TX_TICKS,
	       (sc->bce_tx_ticks_int << 16) | sc->bce_tx_ticks);
	REG_WR(sc, BCE_HC_RX_TICKS,
	       (sc->bce_rx_ticks_int << 16) | sc->bce_rx_ticks);
	REG_WR(sc, BCE_HC_COM_TICKS,
	       (sc->bce_com_ticks_int << 16) | sc->bce_com_ticks);
	REG_WR(sc, BCE_HC_CMD_TICKS,
	       (sc->bce_cmd_ticks_int << 16) | sc->bce_cmd_ticks);
	REG_WR(sc, BCE_HC_STATS_TICKS, (sc->bce_stats_ticks & 0xffff00));
	REG_WR(sc, BCE_HC_STAT_COLLECT_TICKS, 0xbb8);	/* 3ms */

	if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX)
		REG_WR(sc, BCE_HC_MSIX_BIT_VECTOR, BCE_HC_MSIX_BIT_VECTOR_VAL);

	val = BCE_HC_CONFIG_TX_TMR_MODE | BCE_HC_CONFIG_COLLECT_STATS;
	if ((sc->bce_flags & BCE_ONESHOT_MSI_FLAG) ||
	    sc->bce_irq_type == PCI_INTR_TYPE_MSIX) {
		if (bootverbose) {
			if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX) {
				if_printf(&sc->arpcom.ac_if,
				    "using MSI-X\n");
			} else {
				if_printf(&sc->arpcom.ac_if,
				    "using oneshot MSI\n");
			}
		}
		val |= BCE_HC_CONFIG_ONE_SHOT | BCE_HC_CONFIG_USE_INT_PARAM;
		if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX)
			val |= BCE_HC_CONFIG_SB_ADDR_INC_128B;
	}
	REG_WR(sc, BCE_HC_CONFIG, val);

	for (i = 1; i < sc->rx_ring_cnt; ++i) {
		uint32_t base;

		base = ((i - 1) * BCE_HC_SB_CONFIG_SIZE) + BCE_HC_SB_CONFIG_1;
		KKASSERT(base <= BCE_HC_SB_CONFIG_8);

		REG_WR(sc, base,
		    BCE_HC_SB_CONFIG_1_TX_TMR_MODE |
		    /* BCE_HC_SB_CONFIG_1_RX_TMR_MODE | */
		    BCE_HC_SB_CONFIG_1_ONE_SHOT);

		REG_WR(sc, base + BCE_HC_TX_QUICK_CONS_TRIP_OFF,
		    (sc->bce_tx_quick_cons_trip_int << 16) |
		    sc->bce_tx_quick_cons_trip);
		REG_WR(sc, base + BCE_HC_RX_QUICK_CONS_TRIP_OFF,
		    (sc->bce_rx_quick_cons_trip_int << 16) |
		    sc->bce_rx_quick_cons_trip);
		REG_WR(sc, base + BCE_HC_TX_TICKS_OFF,
		    (sc->bce_tx_ticks_int << 16) | sc->bce_tx_ticks);
		REG_WR(sc, base + BCE_HC_RX_TICKS_OFF,
		    (sc->bce_rx_ticks_int << 16) | sc->bce_rx_ticks);
	}

	/* Clear the internal statistics counters. */
	REG_WR(sc, BCE_HC_COMMAND, BCE_HC_COMMAND_CLR_STAT_NOW);

	/* Verify that bootcode is running. */
	reg = bce_shmem_rd(sc, BCE_DEV_INFO_SIGNATURE);

	if ((reg & BCE_DEV_INFO_SIGNATURE_MAGIC_MASK) !=
	    BCE_DEV_INFO_SIGNATURE_MAGIC) {
		if_printf(&sc->arpcom.ac_if,
			  "Bootcode not running! Found: 0x%08X, "
			  "Expected: 08%08X\n",
			  reg & BCE_DEV_INFO_SIGNATURE_MAGIC_MASK,
			  BCE_DEV_INFO_SIGNATURE_MAGIC);
		return ENODEV;
	}

	/* Enable DMA */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		val = REG_RD(sc, BCE_MISC_NEW_CORE_CTL);
		val |= BCE_MISC_NEW_CORE_CTL_DMA_ENABLE;
		REG_WR(sc, BCE_MISC_NEW_CORE_CTL, val);
	}

	/* Allow bootcode to apply any additional fixes before enabling MAC. */
	bce_fw_sync(sc, BCE_DRV_MSG_DATA_WAIT2 | BCE_DRV_MSG_CODE_RESET);

	/* Enable link state change interrupt generation. */
	REG_WR(sc, BCE_HC_ATTN_BITS_ENABLE, STATUS_ATTN_BITS_LINK_STATE);

	/* Enable the RXP. */
	bce_start_rxp_cpu(sc);

	/* Disable management frames (NC-SI) from flowing to the MCP. */
	if (sc->bce_flags & BCE_MFW_ENABLE_FLAG) {
		val = REG_RD(sc, BCE_RPM_MGMT_PKT_CTRL) &
		    ~BCE_RPM_MGMT_PKT_CTRL_MGMT_EN;
		REG_WR(sc, BCE_RPM_MGMT_PKT_CTRL, val);
	}

	/* Enable all remaining blocks in the MAC. */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		REG_WR(sc, BCE_MISC_ENABLE_SET_BITS,
		    BCE_MISC_ENABLE_DEFAULT_XI);
	} else {
		REG_WR(sc, BCE_MISC_ENABLE_SET_BITS, BCE_MISC_ENABLE_DEFAULT);
	}
	REG_RD(sc, BCE_MISC_ENABLE_SET_BITS);
	DELAY(20);

	/* Save the current host coalescing block settings. */
	sc->hc_command = REG_RD(sc, BCE_HC_COMMAND);

	return 0;
}

/****************************************************************************/
/* Encapsulate an mbuf cluster into the rx_bd chain.                        */
/*                                                                          */
/* The NetXtreme II can support Jumbo frames by using multiple rx_bd's.     */
/* This routine will map an mbuf cluster into 1 or more rx_bd's as          */
/* necessary.                                                               */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_newbuf_std(struct bce_rx_ring *rxr, uint16_t *prod, uint16_t chain_prod,
    uint32_t *prod_bseq, int init)
{
	struct bce_rx_buf *rx_buf;
	bus_dmamap_t map;
	bus_dma_segment_t seg;
	struct mbuf *m_new;
	int error, nseg;

	/* This is a new mbuf allocation. */
	m_new = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return ENOBUFS;

	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	/* Map the mbuf cluster into device memory. */
	error = bus_dmamap_load_mbuf_segment(rxr->rx_mbuf_tag,
	    rxr->rx_mbuf_tmpmap, m_new, &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_new);
		if (init) {
			if_printf(&rxr->sc->arpcom.ac_if,
			    "Error mapping mbuf into RX chain!\n");
		}
		return error;
	}

	rx_buf = &rxr->rx_bufs[chain_prod];
	if (rx_buf->rx_mbuf_ptr != NULL)
		bus_dmamap_unload(rxr->rx_mbuf_tag, rx_buf->rx_mbuf_map);

	map = rx_buf->rx_mbuf_map;
	rx_buf->rx_mbuf_map = rxr->rx_mbuf_tmpmap;
	rxr->rx_mbuf_tmpmap = map;

	/* Save the mbuf and update our counter. */
	rx_buf->rx_mbuf_ptr = m_new;
	rx_buf->rx_mbuf_paddr = seg.ds_addr;
	rxr->free_rx_bd--;

	bce_setup_rxdesc_std(rxr, chain_prod, prod_bseq);

	return 0;
}

static void
bce_setup_rxdesc_std(struct bce_rx_ring *rxr, uint16_t chain_prod,
    uint32_t *prod_bseq)
{
	const struct bce_rx_buf *rx_buf;
	struct rx_bd *rxbd;
	bus_addr_t paddr;
	int len;

	rx_buf = &rxr->rx_bufs[chain_prod];
	paddr = rx_buf->rx_mbuf_paddr;
	len = rx_buf->rx_mbuf_ptr->m_len;

	/* Setup the rx_bd for the first segment. */
	rxbd = &rxr->rx_bd_chain[RX_PAGE(chain_prod)][RX_IDX(chain_prod)];

	rxbd->rx_bd_haddr_lo = htole32(BCE_ADDR_LO(paddr));
	rxbd->rx_bd_haddr_hi = htole32(BCE_ADDR_HI(paddr));
	rxbd->rx_bd_len = htole32(len);
	rxbd->rx_bd_flags = htole32(RX_BD_FLAGS_START);
	*prod_bseq += len;

	rxbd->rx_bd_flags |= htole32(RX_BD_FLAGS_END);
}

/****************************************************************************/
/* Initialize the TX context memory.                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing                                                                */
/****************************************************************************/
static void
bce_init_tx_context(struct bce_tx_ring *txr)
{
	uint32_t val;

	/* Initialize the context ID for an L2 TX chain. */
	if (BCE_CHIP_NUM(txr->sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(txr->sc) == BCE_CHIP_NUM_5716) {
		/* Set the CID type to support an L2 connection. */
		val = BCE_L2CTX_TX_TYPE_TYPE_L2 | BCE_L2CTX_TX_TYPE_SIZE_L2;
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TYPE_XI, val);
		val = BCE_L2CTX_TX_CMD_TYPE_TYPE_L2 | (8 << 16);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_CMD_TYPE_XI, val);

		/* Point the hardware to the first page in the chain. */
		val = BCE_ADDR_HI(txr->tx_bd_chain_paddr[0]);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TBDR_BHADDR_HI_XI, val);
		val = BCE_ADDR_LO(txr->tx_bd_chain_paddr[0]);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TBDR_BHADDR_LO_XI, val);
	} else {
		/* Set the CID type to support an L2 connection. */
		val = BCE_L2CTX_TX_TYPE_TYPE_L2 | BCE_L2CTX_TX_TYPE_SIZE_L2;
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TYPE, val);
		val = BCE_L2CTX_TX_CMD_TYPE_TYPE_L2 | (8 << 16);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_CMD_TYPE, val);

		/* Point the hardware to the first page in the chain. */
		val = BCE_ADDR_HI(txr->tx_bd_chain_paddr[0]);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TBDR_BHADDR_HI, val);
		val = BCE_ADDR_LO(txr->tx_bd_chain_paddr[0]);
		CTX_WR(txr->sc, GET_CID_ADDR(txr->tx_cid),
		    BCE_L2CTX_TX_TBDR_BHADDR_LO, val);
	}
}

/****************************************************************************/
/* Allocate memory and initialize the TX data structures.                   */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_init_tx_chain(struct bce_tx_ring *txr)
{
	struct tx_bd *txbd;
	int i, rc = 0;

	/* Set the initial TX producer/consumer indices. */
	txr->tx_prod = 0;
	txr->tx_cons = 0;
	txr->tx_prod_bseq = 0;
	txr->used_tx_bd = 0;
	txr->max_tx_bd = USABLE_TX_BD(txr);

	/*
	 * The NetXtreme II supports a linked-list structre called
	 * a Buffer Descriptor Chain (or BD chain).  A BD chain
	 * consists of a series of 1 or more chain pages, each of which
	 * consists of a fixed number of BD entries.
	 * The last BD entry on each page is a pointer to the next page
	 * in the chain, and the last pointer in the BD chain
	 * points back to the beginning of the chain.
	 */

	/* Set the TX next pointer chain entries. */
	for (i = 0; i < txr->tx_pages; i++) {
		int j;

		txbd = &txr->tx_bd_chain[i][USABLE_TX_BD_PER_PAGE];

		/* Check if we've reached the last page. */
		if (i == (txr->tx_pages - 1))
			j = 0;
		else
			j = i + 1;

		txbd->tx_bd_haddr_hi =
		    htole32(BCE_ADDR_HI(txr->tx_bd_chain_paddr[j]));
		txbd->tx_bd_haddr_lo =
		    htole32(BCE_ADDR_LO(txr->tx_bd_chain_paddr[j]));
	}
	bce_init_tx_context(txr);

	return(rc);
}

/****************************************************************************/
/* Free memory and clear the TX data structures.                            */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_free_tx_chain(struct bce_tx_ring *txr)
{
	int i;

	/* Unmap, unload, and free any mbufs still in the TX mbuf chain. */
	for (i = 0; i < TOTAL_TX_BD(txr); i++) {
		struct bce_tx_buf *tx_buf = &txr->tx_bufs[i];

		if (tx_buf->tx_mbuf_ptr != NULL) {
			bus_dmamap_unload(txr->tx_mbuf_tag,
			    tx_buf->tx_mbuf_map);
			m_freem(tx_buf->tx_mbuf_ptr);
			tx_buf->tx_mbuf_ptr = NULL;
		}
	}

	/* Clear each TX chain page. */
	for (i = 0; i < txr->tx_pages; i++)
		bzero(txr->tx_bd_chain[i], BCE_TX_CHAIN_PAGE_SZ);
	txr->used_tx_bd = 0;
}

/****************************************************************************/
/* Initialize the RX context memory.                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing                                                                */
/****************************************************************************/
static void
bce_init_rx_context(struct bce_rx_ring *rxr)
{
	uint32_t val;

	/* Initialize the context ID for an L2 RX chain. */
	val = BCE_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE |
	    BCE_L2CTX_RX_CTX_TYPE_SIZE_L2 | (0x02 << 8);

	/*
	 * Set the level for generating pause frames
	 * when the number of available rx_bd's gets
	 * too low (the low watermark) and the level
	 * when pause frames can be stopped (the high
	 * watermark).
	 */
	if (BCE_CHIP_NUM(rxr->sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(rxr->sc) == BCE_CHIP_NUM_5716) {
		uint32_t lo_water, hi_water;

		lo_water = BCE_L2CTX_RX_LO_WATER_MARK_DEFAULT;
		hi_water = USABLE_RX_BD(rxr) / 4;

		lo_water /= BCE_L2CTX_RX_LO_WATER_MARK_SCALE;
		hi_water /= BCE_L2CTX_RX_HI_WATER_MARK_SCALE;

		if (hi_water > 0xf)
			hi_water = 0xf;
		else if (hi_water == 0)
			lo_water = 0;
		val |= lo_water |
		    (hi_water << BCE_L2CTX_RX_HI_WATER_MARK_SHIFT);
	}

 	CTX_WR(rxr->sc, GET_CID_ADDR(rxr->rx_cid),
	    BCE_L2CTX_RX_CTX_TYPE, val);

	/* Setup the MQ BIN mapping for l2_ctx_host_bseq. */
	if (BCE_CHIP_NUM(rxr->sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(rxr->sc) == BCE_CHIP_NUM_5716) {
		val = REG_RD(rxr->sc, BCE_MQ_MAP_L2_5);
		REG_WR(rxr->sc, BCE_MQ_MAP_L2_5, val | BCE_MQ_MAP_L2_5_ARM);
	}

	/* Point the hardware to the first page in the chain. */
	val = BCE_ADDR_HI(rxr->rx_bd_chain_paddr[0]);
	CTX_WR(rxr->sc, GET_CID_ADDR(rxr->rx_cid),
	    BCE_L2CTX_RX_NX_BDHADDR_HI, val);
	val = BCE_ADDR_LO(rxr->rx_bd_chain_paddr[0]);
	CTX_WR(rxr->sc, GET_CID_ADDR(rxr->rx_cid),
	    BCE_L2CTX_RX_NX_BDHADDR_LO, val);
}

/****************************************************************************/
/* Allocate memory and initialize the RX data structures.                   */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_init_rx_chain(struct bce_rx_ring *rxr)
{
	struct rx_bd *rxbd;
	int i, rc = 0;
	uint16_t prod, chain_prod;
	uint32_t prod_bseq;

	/* Initialize the RX producer and consumer indices. */
	rxr->rx_prod = 0;
	rxr->rx_cons = 0;
	rxr->rx_prod_bseq = 0;
	rxr->free_rx_bd = USABLE_RX_BD(rxr);
	rxr->max_rx_bd = USABLE_RX_BD(rxr);

	/* Clear cache status index */
	rxr->last_status_idx = 0;

	/* Initialize the RX next pointer chain entries. */
	for (i = 0; i < rxr->rx_pages; i++) {
		int j;

		rxbd = &rxr->rx_bd_chain[i][USABLE_RX_BD_PER_PAGE];

		/* Check if we've reached the last page. */
		if (i == (rxr->rx_pages - 1))
			j = 0;
		else
			j = i + 1;

		/* Setup the chain page pointers. */
		rxbd->rx_bd_haddr_hi =
		    htole32(BCE_ADDR_HI(rxr->rx_bd_chain_paddr[j]));
		rxbd->rx_bd_haddr_lo =
		    htole32(BCE_ADDR_LO(rxr->rx_bd_chain_paddr[j]));
	}

	/* Allocate mbuf clusters for the rx_bd chain. */
	prod = prod_bseq = 0;
	while (prod < TOTAL_RX_BD(rxr)) {
		chain_prod = RX_CHAIN_IDX(rxr, prod);
		if (bce_newbuf_std(rxr, &prod, chain_prod, &prod_bseq, 1)) {
			if_printf(&rxr->sc->arpcom.ac_if,
			    "Error filling RX chain: rx_bd[0x%04X]!\n",
			    chain_prod);
			rc = ENOBUFS;
			break;
		}
		prod = NEXT_RX_BD(prod);
	}

	/* Save the RX chain producer index. */
	rxr->rx_prod = prod;
	rxr->rx_prod_bseq = prod_bseq;

	/* Tell the chip about the waiting rx_bd's. */
	REG_WR16(rxr->sc, MB_GET_CID_ADDR(rxr->rx_cid) + BCE_L2MQ_RX_HOST_BDIDX,
	    rxr->rx_prod);
	REG_WR(rxr->sc, MB_GET_CID_ADDR(rxr->rx_cid) + BCE_L2MQ_RX_HOST_BSEQ,
	    rxr->rx_prod_bseq);

	bce_init_rx_context(rxr);

	return(rc);
}

/****************************************************************************/
/* Free memory and clear the RX data structures.                            */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_free_rx_chain(struct bce_rx_ring *rxr)
{
	int i;

	/* Free any mbufs still in the RX mbuf chain. */
	for (i = 0; i < TOTAL_RX_BD(rxr); i++) {
		struct bce_rx_buf *rx_buf = &rxr->rx_bufs[i];

		if (rx_buf->rx_mbuf_ptr != NULL) {
			bus_dmamap_unload(rxr->rx_mbuf_tag,
			    rx_buf->rx_mbuf_map);
			m_freem(rx_buf->rx_mbuf_ptr);
			rx_buf->rx_mbuf_ptr = NULL;
		}
	}

	/* Clear each RX chain page. */
	for (i = 0; i < rxr->rx_pages; i++)
		bzero(rxr->rx_bd_chain[i], BCE_RX_CHAIN_PAGE_SZ);
}

/****************************************************************************/
/* Set media options.                                                       */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_ifmedia_upd(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->bce_miibus);
	int error = 0;

	/*
	 * 'mii' will be NULL, when this function is called on following
	 * code path: bce_attach() -> bce_mgmt_init()
	 */
	if (mii != NULL) {
		/* Make sure the MII bus has been enumerated. */
		sc->bce_link = 0;
		if (mii->mii_instance) {
			struct mii_softc *miisc;

			LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
				mii_phy_reset(miisc);
		}
		error = mii_mediachg(mii);
	}
	return error;
}

/****************************************************************************/
/* Reports current media status.                                            */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bce_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->bce_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

/****************************************************************************/
/* Handles PHY generated interrupt events.                                  */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_phy_intr(struct bce_softc *sc)
{
	uint32_t new_link_state, old_link_state;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(&sc->main_serialize);

	new_link_state = sc->status_block->status_attn_bits &
			 STATUS_ATTN_BITS_LINK_STATE;
	old_link_state = sc->status_block->status_attn_bits_ack &
			 STATUS_ATTN_BITS_LINK_STATE;

	/* Handle any changes if the link state has changed. */
	if (new_link_state != old_link_state) {	/* XXX redundant? */
		/* Update the status_attn_bits_ack field in the status block. */
		if (new_link_state) {
			REG_WR(sc, BCE_PCICFG_STATUS_BIT_SET_CMD,
			       STATUS_ATTN_BITS_LINK_STATE);
			if (bootverbose)
				if_printf(ifp, "Link is now UP.\n");
		} else {
			REG_WR(sc, BCE_PCICFG_STATUS_BIT_CLEAR_CMD,
			       STATUS_ATTN_BITS_LINK_STATE);
			if (bootverbose)
				if_printf(ifp, "Link is now DOWN.\n");
		}

		/*
		 * Assume link is down and allow tick routine to
		 * update the state based on the actual media state.
		 */
		sc->bce_link = 0;
		callout_stop(&sc->bce_tick_callout);
		bce_tick_serialized(sc);
	}

	/* Acknowledge the link change interrupt. */
	REG_WR(sc, BCE_EMAC_STATUS, BCE_EMAC_STATUS_LINK_CHANGE);
}

/****************************************************************************/
/* Reads the receive consumer value from the status block (skipping over    */
/* chain page pointer if necessary).                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   hw_cons                                                                */
/****************************************************************************/
static __inline uint16_t
bce_get_hw_rx_cons(struct bce_rx_ring *rxr)
{
	uint16_t hw_cons = *rxr->rx_hw_cons;

	if ((hw_cons & USABLE_RX_BD_PER_PAGE) == USABLE_RX_BD_PER_PAGE)
		hw_cons++;
	return hw_cons;
}

/****************************************************************************/
/* Handles received frame interrupt events.                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_rx_intr(struct bce_rx_ring *rxr, int count, uint16_t hw_cons)
{
	struct ifnet *ifp = &rxr->sc->arpcom.ac_if;
	uint16_t sw_cons, sw_chain_cons, sw_prod, sw_chain_prod;
	uint32_t sw_prod_bseq;
	int cpuid = mycpuid;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	/* Get working copies of the driver's view of the RX indices. */
	sw_cons = rxr->rx_cons;
	sw_prod = rxr->rx_prod;
	sw_prod_bseq = rxr->rx_prod_bseq;

	/* Scan through the receive chain as long as there is work to do. */
	while (sw_cons != hw_cons) {
		struct pktinfo pi0, *pi = NULL;
		struct bce_rx_buf *rx_buf;
		struct mbuf *m = NULL;
		struct l2_fhdr *l2fhdr = NULL;
		unsigned int len;
		uint32_t status = 0;

#ifdef IFPOLL_ENABLE
		if (count >= 0 && count-- == 0)
			break;
#endif

		/*
		 * Convert the producer/consumer indices
		 * to an actual rx_bd index.
		 */
		sw_chain_cons = RX_CHAIN_IDX(rxr, sw_cons);
		sw_chain_prod = RX_CHAIN_IDX(rxr, sw_prod);
		rx_buf = &rxr->rx_bufs[sw_chain_cons];

		rxr->free_rx_bd++;

		/* The mbuf is stored with the last rx_bd entry of a packet. */
		if (rx_buf->rx_mbuf_ptr != NULL) {
			if (sw_chain_cons != sw_chain_prod) {
				if_printf(ifp, "RX cons(%d) != prod(%d), "
				    "drop!\n", sw_chain_cons, sw_chain_prod);
				IFNET_STAT_INC(ifp, ierrors, 1);

				bce_setup_rxdesc_std(rxr, sw_chain_cons,
				    &sw_prod_bseq);
				m = NULL;
				goto bce_rx_int_next_rx;
			}

			/* Unmap the mbuf from DMA space. */
			bus_dmamap_sync(rxr->rx_mbuf_tag, rx_buf->rx_mbuf_map,
			    BUS_DMASYNC_POSTREAD);

			/* Save the mbuf from the driver's chain. */
			m = rx_buf->rx_mbuf_ptr;

			/*
			 * Frames received on the NetXteme II are prepended 
			 * with an l2_fhdr structure which provides status
			 * information about the received frame (including
			 * VLAN tags and checksum info).  The frames are also
			 * automatically adjusted to align the IP header
			 * (i.e. two null bytes are inserted before the 
			 * Ethernet header).  As a result the data DMA'd by
			 * the controller into the mbuf is as follows:
			 *
			 * +---------+-----+---------------------+-----+
			 * | l2_fhdr | pad | packet data         | FCS |
			 * +---------+-----+---------------------+-----+
			 * 
			 * The l2_fhdr needs to be checked and skipped and the
			 * FCS needs to be stripped before sending the packet
			 * up the stack.
			 */
			l2fhdr = mtod(m, struct l2_fhdr *);

			len = l2fhdr->l2_fhdr_pkt_len;
			status = l2fhdr->l2_fhdr_status;

			len -= ETHER_CRC_LEN;

			/* Check the received frame for errors. */
			if (status & (L2_FHDR_ERRORS_BAD_CRC |
				      L2_FHDR_ERRORS_PHY_DECODE |
				      L2_FHDR_ERRORS_ALIGNMENT |
				      L2_FHDR_ERRORS_TOO_SHORT |
				      L2_FHDR_ERRORS_GIANT_FRAME)) {
				IFNET_STAT_INC(ifp, ierrors, 1);

				/* Reuse the mbuf for a new frame. */
				bce_setup_rxdesc_std(rxr, sw_chain_prod,
				    &sw_prod_bseq);
				m = NULL;
				goto bce_rx_int_next_rx;
			}

			/* 
			 * Get a new mbuf for the rx_bd.   If no new
			 * mbufs are available then reuse the current mbuf,
			 * log an ierror on the interface, and generate
			 * an error in the system log.
			 */
			if (bce_newbuf_std(rxr, &sw_prod, sw_chain_prod,
			    &sw_prod_bseq, 0)) {
				IFNET_STAT_INC(ifp, ierrors, 1);

				/* Try and reuse the exisitng mbuf. */
				bce_setup_rxdesc_std(rxr, sw_chain_prod,
				    &sw_prod_bseq);
				m = NULL;
				goto bce_rx_int_next_rx;
			}

			/*
			 * Skip over the l2_fhdr when passing
			 * the data up the stack.
			 */
			m_adj(m, sizeof(struct l2_fhdr) + ETHER_ALIGN);

			m->m_pkthdr.len = m->m_len = len;
			m->m_pkthdr.rcvif = ifp;

			/* Validate the checksum if offload enabled. */
			if (ifp->if_capenable & IFCAP_RXCSUM) {
				/* Check for an IP datagram. */
				if (status & L2_FHDR_STATUS_IP_DATAGRAM) {
					m->m_pkthdr.csum_flags |=
						CSUM_IP_CHECKED;

					/* Check if the IP checksum is valid. */
					if ((l2fhdr->l2_fhdr_ip_xsum ^
					     0xffff) == 0) {
						m->m_pkthdr.csum_flags |=
							CSUM_IP_VALID;
					}
				}

				/* Check for a valid TCP/UDP frame. */
				if (status & (L2_FHDR_STATUS_TCP_SEGMENT |
					      L2_FHDR_STATUS_UDP_DATAGRAM)) {

					/* Check for a good TCP/UDP checksum. */
					if ((status &
					     (L2_FHDR_ERRORS_TCP_XSUM |
					      L2_FHDR_ERRORS_UDP_XSUM)) == 0) {
						m->m_pkthdr.csum_data =
						l2fhdr->l2_fhdr_tcp_udp_xsum;
						m->m_pkthdr.csum_flags |=
							CSUM_DATA_VALID |
							CSUM_PSEUDO_HDR;
					}
				}
			}
			if (ifp->if_capenable & IFCAP_RSS) {
				pi = bce_rss_pktinfo(&pi0, status, l2fhdr);
				if (pi != NULL &&
				    (status & L2_FHDR_STATUS_RSS_HASH)) {
					m_sethash(m,
					    toeplitz_hash(l2fhdr->l2_fhdr_hash));
				}
			}

			IFNET_STAT_INC(ifp, ipackets, 1);
bce_rx_int_next_rx:
			sw_prod = NEXT_RX_BD(sw_prod);
		}

		sw_cons = NEXT_RX_BD(sw_cons);

		/* If we have a packet, pass it up the stack */
		if (m) {
			if (status & L2_FHDR_STATUS_L2_VLAN_TAG) {
				m->m_flags |= M_VLANTAG;
				m->m_pkthdr.ether_vlantag =
					l2fhdr->l2_fhdr_vlan_tag;
			}
			ifp->if_input(ifp, m, pi, cpuid);
#ifdef BCE_RSS_DEBUG
			rxr->rx_pkts++;
#endif
		}
	}

	rxr->rx_cons = sw_cons;
	rxr->rx_prod = sw_prod;
	rxr->rx_prod_bseq = sw_prod_bseq;

	REG_WR16(rxr->sc, MB_GET_CID_ADDR(rxr->rx_cid) + BCE_L2MQ_RX_HOST_BDIDX,
	    rxr->rx_prod);
	REG_WR(rxr->sc, MB_GET_CID_ADDR(rxr->rx_cid) + BCE_L2MQ_RX_HOST_BSEQ,
	    rxr->rx_prod_bseq);
}

/****************************************************************************/
/* Reads the transmit consumer value from the status block (skipping over   */
/* chain page pointer if necessary).                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   hw_cons                                                                */
/****************************************************************************/
static __inline uint16_t
bce_get_hw_tx_cons(struct bce_tx_ring *txr)
{
	uint16_t hw_cons = *txr->tx_hw_cons;

	if ((hw_cons & USABLE_TX_BD_PER_PAGE) == USABLE_TX_BD_PER_PAGE)
		hw_cons++;
	return hw_cons;
}

/****************************************************************************/
/* Handles transmit completion interrupt events.                            */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_tx_intr(struct bce_tx_ring *txr, uint16_t hw_tx_cons)
{
	struct ifnet *ifp = &txr->sc->arpcom.ac_if;
	uint16_t sw_tx_cons, sw_tx_chain_cons;

	ASSERT_SERIALIZED(&txr->tx_serialize);

	/* Get the hardware's view of the TX consumer index. */
	sw_tx_cons = txr->tx_cons;

	/* Cycle through any completed TX chain page entries. */
	while (sw_tx_cons != hw_tx_cons) {
		struct bce_tx_buf *tx_buf;

		sw_tx_chain_cons = TX_CHAIN_IDX(txr, sw_tx_cons);
		tx_buf = &txr->tx_bufs[sw_tx_chain_cons];

		/*
		 * Free the associated mbuf. Remember
		 * that only the last tx_bd of a packet
		 * has an mbuf pointer and DMA map.
		 */
		if (tx_buf->tx_mbuf_ptr != NULL) {
			/* Unmap the mbuf. */
			bus_dmamap_unload(txr->tx_mbuf_tag,
			    tx_buf->tx_mbuf_map);

			/* Free the mbuf. */
			m_freem(tx_buf->tx_mbuf_ptr);
			tx_buf->tx_mbuf_ptr = NULL;

			IFNET_STAT_INC(ifp, opackets, 1);
#ifdef BCE_TSS_DEBUG
			txr->tx_pkts++;
#endif
		}

		txr->used_tx_bd--;
		sw_tx_cons = NEXT_TX_BD(sw_tx_cons);
	}

	if (txr->used_tx_bd == 0) {
		/* Clear the TX timeout timer. */
		ifsq_watchdog_set_count(&txr->tx_watchdog, 0);
	}

	/* Clear the tx hardware queue full flag. */
	if (txr->max_tx_bd - txr->used_tx_bd >= BCE_TX_SPARE_SPACE)
		ifsq_clr_oactive(txr->ifsq);
	txr->tx_cons = sw_tx_cons;
}

/****************************************************************************/
/* Disables interrupt generation.                                           */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_disable_intr(struct bce_softc *sc)
{
	int i;

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		REG_WR(sc, BCE_PCICFG_INT_ACK_CMD,
		    (sc->rx_rings[i].idx << 24) |
		    BCE_PCICFG_INT_ACK_CMD_MASK_INT);
	}
	REG_RD(sc, BCE_PCICFG_INT_ACK_CMD);

	callout_stop(&sc->bce_ckmsi_callout);
	sc->bce_msi_maylose = FALSE;
	sc->bce_check_rx_cons = 0;
	sc->bce_check_tx_cons = 0;
	sc->bce_check_status_idx = 0xffff;

	for (i = 0; i < sc->rx_ring_cnt; ++i)
		lwkt_serialize_handler_disable(sc->bce_msix[i].msix_serialize);
}

/****************************************************************************/
/* Enables interrupt generation.                                            */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_enable_intr(struct bce_softc *sc)
{
	int i;

	for (i = 0; i < sc->rx_ring_cnt; ++i)
		lwkt_serialize_handler_enable(sc->bce_msix[i].msix_serialize);

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct bce_rx_ring *rxr = &sc->rx_rings[i];

		REG_WR(sc, BCE_PCICFG_INT_ACK_CMD, (rxr->idx << 24) |
		       BCE_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       BCE_PCICFG_INT_ACK_CMD_MASK_INT |
		       rxr->last_status_idx);
		REG_WR(sc, BCE_PCICFG_INT_ACK_CMD, (rxr->idx << 24) |
		       BCE_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       rxr->last_status_idx);
	}
	REG_WR(sc, BCE_HC_COMMAND, sc->hc_command | BCE_HC_COMMAND_COAL_NOW);

	if (sc->bce_flags & BCE_CHECK_MSI_FLAG) {
		sc->bce_msi_maylose = FALSE;
		sc->bce_check_rx_cons = 0;
		sc->bce_check_tx_cons = 0;
		sc->bce_check_status_idx = 0xffff;

		if (bootverbose)
			if_printf(&sc->arpcom.ac_if, "check msi\n");

		callout_reset_bycpu(&sc->bce_ckmsi_callout, BCE_MSI_CKINTVL,
		    bce_check_msi, sc, sc->bce_msix[0].msix_cpuid);
	}
}

/****************************************************************************/
/* Reenables interrupt generation during interrupt handling.                */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_reenable_intr(struct bce_rx_ring *rxr)
{
	REG_WR(rxr->sc, BCE_PCICFG_INT_ACK_CMD, (rxr->idx << 24) |
	       BCE_PCICFG_INT_ACK_CMD_INDEX_VALID | rxr->last_status_idx);
}

/****************************************************************************/
/* Handles controller initialization.                                       */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_init(void *xsc)
{
	struct bce_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t ether_mtu;
	int error, i;
	boolean_t polling;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* Check if the driver is still running and bail out if it is. */
	if (ifp->if_flags & IFF_RUNNING)
		return;

	bce_stop(sc);

	error = bce_reset(sc, BCE_DRV_MSG_CODE_RESET);
	if (error) {
		if_printf(ifp, "Controller reset failed!\n");
		goto back;
	}

	error = bce_chipinit(sc);
	if (error) {
		if_printf(ifp, "Controller initialization failed!\n");
		goto back;
	}

	error = bce_blockinit(sc);
	if (error) {
		if_printf(ifp, "Block initialization failed!\n");
		goto back;
	}

	/* Load our MAC address. */
	bcopy(IF_LLADDR(ifp), sc->eaddr, ETHER_ADDR_LEN);
	bce_set_mac_addr(sc);

	/* Calculate and program the Ethernet MTU size. */
	ether_mtu = ETHER_HDR_LEN + EVL_ENCAPLEN + ifp->if_mtu + ETHER_CRC_LEN;

	/* 
	 * Program the mtu, enabling jumbo frame 
	 * support if necessary.  Also set the mbuf
	 * allocation count for RX frames.
	 */
	if (ether_mtu > ETHER_MAX_LEN + EVL_ENCAPLEN) {
#ifdef notyet
		REG_WR(sc, BCE_EMAC_RX_MTU_SIZE,
		       min(ether_mtu, BCE_MAX_JUMBO_ETHER_MTU) |
		       BCE_EMAC_RX_MTU_SIZE_JUMBO_ENA);
#else
		panic("jumbo buffer is not supported yet");
#endif
	} else {
		REG_WR(sc, BCE_EMAC_RX_MTU_SIZE, ether_mtu);
	}

	/* Program appropriate promiscuous/multicast filtering. */
	bce_set_rx_mode(sc);

	/*
	 * Init RX buffer descriptor chain.
	 */
	REG_WR(sc, BCE_RLUP_RSS_CONFIG, 0);
	bce_reg_wr_ind(sc, BCE_RXP_SCRATCH_RSS_TBL_SZ, 0);

	for (i = 0; i < sc->rx_ring_cnt; ++i)
		bce_init_rx_chain(&sc->rx_rings[i]);	/* XXX return value */

	if (sc->rx_ring_cnt > 1)
		bce_init_rss(sc);

	/*
	 * Init TX buffer descriptor chain.
	 */
	REG_WR(sc, BCE_TSCH_TSS_CFG, 0);

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		bce_init_tx_chain(&sc->tx_rings[i]);

	if (sc->tx_ring_cnt > 1) {
		REG_WR(sc, BCE_TSCH_TSS_CFG,
		    ((sc->tx_ring_cnt - 1) << 24) | (TX_TSS_CID << 7));
	}

	polling = FALSE;
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		polling = TRUE;
#endif

	if (polling) {
		/* Disable interrupts if we are polling. */
		bce_disable_intr(sc);

		/* Change coalesce parameters */
		bce_npoll_coal_change(sc);
	} else {
		/* Enable host interrupts. */
		bce_enable_intr(sc);
	}
	bce_set_timer_cpuid(sc, polling);

	bce_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		ifsq_clr_oactive(sc->tx_rings[i].ifsq);
		ifsq_watchdog_start(&sc->tx_rings[i].tx_watchdog);
	}

	callout_reset_bycpu(&sc->bce_tick_callout, hz, bce_tick, sc,
	    sc->bce_timer_cpuid);
back:
	if (error)
		bce_stop(sc);
}

/****************************************************************************/
/* Initialize the controller just enough so that any management firmware    */
/* running on the device will continue to operate corectly.                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_mgmt_init(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* Bail out if management firmware is not running. */
	if (!(sc->bce_flags & BCE_MFW_ENABLE_FLAG))
		return;

	/* Enable all critical blocks in the MAC. */
	if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
	    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
		REG_WR(sc, BCE_MISC_ENABLE_SET_BITS,
		    BCE_MISC_ENABLE_DEFAULT_XI);
	} else {
		REG_WR(sc, BCE_MISC_ENABLE_SET_BITS, BCE_MISC_ENABLE_DEFAULT);
	}
	REG_RD(sc, BCE_MISC_ENABLE_SET_BITS);
	DELAY(20);

	bce_ifmedia_upd(ifp);
}

/****************************************************************************/
/* Encapsultes an mbuf cluster into the tx_bd chain structure and makes the */
/* memory visible to the controller.                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_encap(struct bce_tx_ring *txr, struct mbuf **m_head, int *nsegs_used)
{
	bus_dma_segment_t segs[BCE_MAX_SEGMENTS];
	bus_dmamap_t map, tmp_map;
	struct mbuf *m0 = *m_head;
	struct tx_bd *txbd = NULL;
	uint16_t vlan_tag = 0, flags = 0, mss = 0;
	uint16_t chain_prod, chain_prod_start, prod;
	uint32_t prod_bseq;
	int i, error, maxsegs, nsegs;

	/* Transfer any checksum offload flags to the bd. */
	if (m0->m_pkthdr.csum_flags & CSUM_TSO) {
		error = bce_tso_setup(txr, m_head, &flags, &mss);
		if (error)
			return ENOBUFS;
		m0 = *m_head;
	} else if (m0->m_pkthdr.csum_flags & BCE_CSUM_FEATURES) {
		if (m0->m_pkthdr.csum_flags & CSUM_IP)
			flags |= TX_BD_FLAGS_IP_CKSUM;
		if (m0->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
			flags |= TX_BD_FLAGS_TCP_UDP_CKSUM;
	}

	/* Transfer any VLAN tags to the bd. */
	if (m0->m_flags & M_VLANTAG) {
		flags |= TX_BD_FLAGS_VLAN_TAG;
		vlan_tag = m0->m_pkthdr.ether_vlantag;
	}

	prod = txr->tx_prod;
	chain_prod_start = chain_prod = TX_CHAIN_IDX(txr, prod);

	/* Map the mbuf into DMAable memory. */
	map = txr->tx_bufs[chain_prod_start].tx_mbuf_map;

	maxsegs = txr->max_tx_bd - txr->used_tx_bd;
	KASSERT(maxsegs >= BCE_TX_SPARE_SPACE,
		("not enough segments %d", maxsegs));
	if (maxsegs > BCE_MAX_SEGMENTS)
		maxsegs = BCE_MAX_SEGMENTS;

	/* Map the mbuf into our DMA address space. */
	error = bus_dmamap_load_mbuf_defrag(txr->tx_mbuf_tag, map, m_head,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;
	bus_dmamap_sync(txr->tx_mbuf_tag, map, BUS_DMASYNC_PREWRITE);

	*nsegs_used += nsegs;

	/* Reset m0 */
	m0 = *m_head;

	/* prod points to an empty tx_bd at this point. */
	prod_bseq  = txr->tx_prod_bseq;

	/*
	 * Cycle through each mbuf segment that makes up
	 * the outgoing frame, gathering the mapping info
	 * for that segment and creating a tx_bd to for
	 * the mbuf.
	 */
	for (i = 0; i < nsegs; i++) {
		chain_prod = TX_CHAIN_IDX(txr, prod);
		txbd =
		&txr->tx_bd_chain[TX_PAGE(chain_prod)][TX_IDX(chain_prod)];

		txbd->tx_bd_haddr_lo = htole32(BCE_ADDR_LO(segs[i].ds_addr));
		txbd->tx_bd_haddr_hi = htole32(BCE_ADDR_HI(segs[i].ds_addr));
		txbd->tx_bd_mss_nbytes = htole32(mss << 16) |
		    htole16(segs[i].ds_len);
		txbd->tx_bd_vlan_tag = htole16(vlan_tag);
		txbd->tx_bd_flags = htole16(flags);

		prod_bseq += segs[i].ds_len;
		if (i == 0)
			txbd->tx_bd_flags |= htole16(TX_BD_FLAGS_START);
		prod = NEXT_TX_BD(prod);
	}

	/* Set the END flag on the last TX buffer descriptor. */
	txbd->tx_bd_flags |= htole16(TX_BD_FLAGS_END);

	/*
	 * Ensure that the mbuf pointer for this transmission
	 * is placed at the array index of the last
	 * descriptor in this chain.  This is done
	 * because a single map is used for all 
	 * segments of the mbuf and we don't want to
	 * unload the map before all of the segments
	 * have been freed.
	 */
	txr->tx_bufs[chain_prod].tx_mbuf_ptr = m0;

	tmp_map = txr->tx_bufs[chain_prod].tx_mbuf_map;
	txr->tx_bufs[chain_prod].tx_mbuf_map = map;
	txr->tx_bufs[chain_prod_start].tx_mbuf_map = tmp_map;

	txr->used_tx_bd += nsegs;

	/* prod points to the next free tx_bd at this point. */
	txr->tx_prod = prod;
	txr->tx_prod_bseq = prod_bseq;
back:
	if (error) {
		m_freem(*m_head);
		*m_head = NULL;
	}
	return error;
}

static void
bce_xmit(struct bce_tx_ring *txr)
{
	/* Start the transmit. */
	REG_WR16(txr->sc, MB_GET_CID_ADDR(txr->tx_cid) + BCE_L2CTX_TX_HOST_BIDX,
	    txr->tx_prod);
	REG_WR(txr->sc, MB_GET_CID_ADDR(txr->tx_cid) + BCE_L2CTX_TX_HOST_BSEQ,
	    txr->tx_prod_bseq);
}

/****************************************************************************/
/* Main transmit routine when called from another routine with a lock.      */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct bce_softc *sc = ifp->if_softc;
	struct bce_tx_ring *txr = ifsq_get_priv(ifsq);
	int count = 0;

	KKASSERT(txr->ifsq == ifsq);
	ASSERT_SERIALIZED(&txr->tx_serialize);

	/* If there's no link or the transmit queue is empty then just exit. */
	if (!sc->bce_link) {
		ifsq_purge(ifsq);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifsq_is_oactive(ifsq))
		return;

	for (;;) {
		struct mbuf *m_head;

		/*
		 * We keep BCE_TX_SPARE_SPACE entries, so bce_encap() is
		 * unlikely to fail.
		 */
		if (txr->max_tx_bd - txr->used_tx_bd < BCE_TX_SPARE_SPACE) {
			ifsq_set_oactive(ifsq);
			break;
		}

		/* Check for any frames to send. */
		m_head = ifsq_dequeue(ifsq);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, place the mbuf back at the
		 * head of the queue and set the OACTIVE flag
		 * to wait for the NIC to drain the chain.
		 */
		if (bce_encap(txr, &m_head, &count)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (txr->used_tx_bd == 0) {
				continue;
			} else {
				ifsq_set_oactive(ifsq);
				break;
			}
		}

		if (count >= txr->tx_wreg) {
			bce_xmit(txr);
			count = 0;
		}

		/* Send a copy of the frame to any BPF listeners. */
		ETHER_BPF_MTAP(ifp, m_head);

		/* Set the tx timeout. */
		ifsq_watchdog_set_count(&txr->tx_watchdog, BCE_TX_TIMEOUT);
	}
	if (count > 0)
		bce_xmit(txr);
}

/****************************************************************************/
/* Handles any IOCTL calls from the operating system.                       */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static int
bce_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct bce_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int mask, error = 0;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch(command) {
	case SIOCSIFMTU:
		/* Check that the MTU setting is supported. */
		if (ifr->ifr_mtu < BCE_MIN_MTU ||
#ifdef notyet
		    ifr->ifr_mtu > BCE_MAX_JUMBO_MTU
#else
		    ifr->ifr_mtu > ETHERMTU
#endif
		   ) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		ifp->if_flags &= ~IFF_RUNNING;	/* Force reinitialize */
		bce_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				mask = ifp->if_flags ^ sc->bce_if_flags;

				if (mask & (IFF_PROMISC | IFF_ALLMULTI))
					bce_set_rx_mode(sc);
			} else {
				bce_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			bce_stop(sc);

			/* If MFW is running, restart the controller a bit. */
			if (sc->bce_flags & BCE_MFW_ENABLE_FLAG) {
				bce_reset(sc, BCE_DRV_MSG_CODE_RESET);
				bce_chipinit(sc);
				bce_mgmt_init(sc);
			}
		}
		sc->bce_if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_flags & IFF_RUNNING)
			bce_set_rx_mode(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->bce_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable ^= (mask & IFCAP_HWCSUM);
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist |= BCE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~BCE_CSUM_FEATURES;
		}
		if (mask & IFCAP_TSO) {
			ifp->if_capenable ^= IFCAP_TSO;
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

/****************************************************************************/
/* Transmit timeout handler.                                                */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_watchdog(struct ifaltq_subque *ifsq)
{
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct bce_softc *sc = ifp->if_softc;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/*
	 * If we are in this routine because of pause frames, then
	 * don't reset the hardware.
	 */
	if (REG_RD(sc, BCE_EMAC_TX_STATUS) & BCE_EMAC_TX_STATUS_XOFFED)	
		return;

	if_printf(ifp, "Watchdog timeout occurred, resetting!\n");

	ifp->if_flags &= ~IFF_RUNNING;	/* Force reinitialize */
	bce_init(sc);

	IFNET_STAT_INC(ifp, oerrors, 1);

	for (i = 0; i < sc->tx_ring_cnt; ++i)
		ifsq_devstart_sched(sc->tx_rings[i].ifsq);
}

#ifdef IFPOLL_ENABLE

static void
bce_npoll_status(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	struct status_block *sblk = sc->status_block;
	uint32_t status_attn_bits;

	ASSERT_SERIALIZED(&sc->main_serialize);

	status_attn_bits = sblk->status_attn_bits;

	/* Was it a link change interrupt? */
	if ((status_attn_bits & STATUS_ATTN_BITS_LINK_STATE) !=
	    (sblk->status_attn_bits_ack & STATUS_ATTN_BITS_LINK_STATE)) {
		bce_phy_intr(sc);

		/*
		 * Clear any transient status updates during link state change.
		 */
		REG_WR(sc, BCE_HC_COMMAND,
		    sc->hc_command | BCE_HC_COMMAND_COAL_NOW_WO_INT);
		REG_RD(sc, BCE_HC_COMMAND);
	}

	/*
	 * If any other attention is asserted then the chip is toast.
	 */
	if ((status_attn_bits & ~STATUS_ATTN_BITS_LINK_STATE) !=
	     (sblk->status_attn_bits_ack & ~STATUS_ATTN_BITS_LINK_STATE)) {
		if_printf(ifp, "Fatal attention detected: 0x%08X\n",
		    sblk->status_attn_bits);
		bce_serialize_skipmain(sc);
		bce_init(sc);
		bce_deserialize_skipmain(sc);
	}
}

static void
bce_npoll_rx(struct ifnet *ifp, void *arg, int count)
{
	struct bce_rx_ring *rxr = arg;
	uint16_t hw_rx_cons;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	/*
	 * Save the status block index value for use when enabling
	 * the interrupt.
	 */
	rxr->last_status_idx = *rxr->hw_status_idx;

	/* Make sure status index is extracted before RX/TX cons */
	cpu_lfence();

	hw_rx_cons = bce_get_hw_rx_cons(rxr);

	/* Check for any completed RX frames. */
	if (hw_rx_cons != rxr->rx_cons)
		bce_rx_intr(rxr, count, hw_rx_cons);
}

static void
bce_npoll_rx_pack(struct ifnet *ifp, void *arg, int count)
{
	struct bce_rx_ring *rxr = arg;

	KASSERT(rxr->idx == 0, ("not the first RX ring, but %d", rxr->idx));
	bce_npoll_rx(ifp, rxr, count);

	KASSERT(rxr->sc->rx_ring_cnt != rxr->sc->rx_ring_cnt2,
	    ("RX ring count %d, count2 %d", rxr->sc->rx_ring_cnt,
	     rxr->sc->rx_ring_cnt2));

	/* Last ring carries packets whose masked hash is 0 */
	rxr = &rxr->sc->rx_rings[rxr->sc->rx_ring_cnt - 1];

	lwkt_serialize_enter(&rxr->rx_serialize);
	bce_npoll_rx(ifp, rxr, count);
	lwkt_serialize_exit(&rxr->rx_serialize);
}

static void
bce_npoll_tx(struct ifnet *ifp, void *arg, int count __unused)
{
	struct bce_tx_ring *txr = arg;
	uint16_t hw_tx_cons;

	ASSERT_SERIALIZED(&txr->tx_serialize);

	hw_tx_cons = bce_get_hw_tx_cons(txr);

	/* Check for any completed TX frames. */
	if (hw_tx_cons != txr->tx_cons) {
		bce_tx_intr(txr, hw_tx_cons);
		if (!ifsq_is_empty(txr->ifsq))
			ifsq_devstart(txr->ifsq);
	}
}

static void
bce_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct bce_softc *sc = ifp->if_softc;
	int i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	if (info != NULL) {
		int cpu;

		info->ifpi_status.status_func = bce_npoll_status;
		info->ifpi_status.serializer = &sc->main_serialize;

		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			struct bce_tx_ring *txr = &sc->tx_rings[i];

			cpu = if_ringmap_cpumap(sc->tx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			info->ifpi_tx[cpu].poll_func = bce_npoll_tx;
			info->ifpi_tx[cpu].arg = txr;
			info->ifpi_tx[cpu].serializer = &txr->tx_serialize;
			ifsq_set_cpuid(txr->ifsq, cpu);
		}

		for (i = 0; i < sc->rx_ring_cnt2; ++i) {
			struct bce_rx_ring *rxr = &sc->rx_rings[i];

			cpu = if_ringmap_cpumap(sc->rx_rmap, i);
			KKASSERT(cpu < netisr_ncpus);
			if (i == 0 && sc->rx_ring_cnt2 != sc->rx_ring_cnt) {
				/*
				 * If RSS is enabled, the packets whose
				 * masked hash are 0 are queued to the
				 * last RX ring; piggyback the last RX
				 * ring's processing in the first RX
				 * polling handler. (see also: comment
				 * in bce_setup_ring_cnt())
				 */
				if (bootverbose) {
					if_printf(ifp, "npoll pack last "
					    "RX ring on cpu%d\n", cpu);
				}
				info->ifpi_rx[cpu].poll_func =
				    bce_npoll_rx_pack;
			} else {
				info->ifpi_rx[cpu].poll_func = bce_npoll_rx;
			}
			info->ifpi_rx[cpu].arg = rxr;
			info->ifpi_rx[cpu].serializer = &rxr->rx_serialize;
		}

		if (ifp->if_flags & IFF_RUNNING) {
			bce_set_timer_cpuid(sc, TRUE);
			bce_disable_intr(sc);
			bce_npoll_coal_change(sc);
		}
	} else {
		for (i = 0; i < sc->tx_ring_cnt; ++i) {
			ifsq_set_cpuid(sc->tx_rings[i].ifsq,
			    sc->bce_msix[i].msix_cpuid);
		}

		if (ifp->if_flags & IFF_RUNNING) {
			bce_set_timer_cpuid(sc, FALSE);
			bce_enable_intr(sc);

			sc->bce_coalchg_mask |= BCE_COALMASK_TX_BDS_INT |
			    BCE_COALMASK_RX_BDS_INT;
			bce_coal_change(sc);
		}
	}
}

#endif	/* IFPOLL_ENABLE */

/*
 * Interrupt handler.
 */
/****************************************************************************/
/* Main interrupt entry point.  Verifies that the controller generated the  */
/* interrupt and then calls a separate routine for handle the various       */
/* interrupt causes (PHY, TX, RX).                                          */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static void
bce_intr(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct status_block *sblk;
	uint16_t hw_rx_cons, hw_tx_cons;
	uint32_t status_attn_bits;
	struct bce_tx_ring *txr = &sc->tx_rings[0];
	struct bce_rx_ring *rxr = &sc->rx_rings[0];

	ASSERT_SERIALIZED(&sc->main_serialize);

	sblk = sc->status_block;

	/*
	 * Save the status block index value for use during
	 * the next interrupt.
	 */
	rxr->last_status_idx = *rxr->hw_status_idx;

	/* Make sure status index is extracted before RX/TX cons */
	cpu_lfence();

	/* Check if the hardware has finished any work. */
	hw_rx_cons = bce_get_hw_rx_cons(rxr);
	hw_tx_cons = bce_get_hw_tx_cons(txr);

	status_attn_bits = sblk->status_attn_bits;

	/* Was it a link change interrupt? */
	if ((status_attn_bits & STATUS_ATTN_BITS_LINK_STATE) !=
	    (sblk->status_attn_bits_ack & STATUS_ATTN_BITS_LINK_STATE)) {
		bce_phy_intr(sc);

		/*
		 * Clear any transient status updates during link state
		 * change.
		 */
		REG_WR(sc, BCE_HC_COMMAND,
		    sc->hc_command | BCE_HC_COMMAND_COAL_NOW_WO_INT);
		REG_RD(sc, BCE_HC_COMMAND);
	}

	/*
	 * If any other attention is asserted then
	 * the chip is toast.
	 */
	if ((status_attn_bits & ~STATUS_ATTN_BITS_LINK_STATE) !=
	    (sblk->status_attn_bits_ack & ~STATUS_ATTN_BITS_LINK_STATE)) {
		if_printf(ifp, "Fatal attention detected: 0x%08X\n",
			  sblk->status_attn_bits);
		bce_serialize_skipmain(sc);
		bce_init(sc);
		bce_deserialize_skipmain(sc);
		return;
	}

	/* Check for any completed RX frames. */
	lwkt_serialize_enter(&rxr->rx_serialize);
	if (hw_rx_cons != rxr->rx_cons)
		bce_rx_intr(rxr, -1, hw_rx_cons);
	lwkt_serialize_exit(&rxr->rx_serialize);

	/* Check for any completed TX frames. */
	lwkt_serialize_enter(&txr->tx_serialize);
	if (hw_tx_cons != txr->tx_cons) {
		bce_tx_intr(txr, hw_tx_cons);
		if (!ifsq_is_empty(txr->ifsq))
			ifsq_devstart(txr->ifsq);
	}
	lwkt_serialize_exit(&txr->tx_serialize);
}

static void
bce_intr_legacy(void *xsc)
{
	struct bce_softc *sc = xsc;
	struct bce_rx_ring *rxr = &sc->rx_rings[0];
	struct status_block *sblk;

	sblk = sc->status_block;

	/*
	 * If the hardware status block index matches the last value
	 * read by the driver and we haven't asserted our interrupt
	 * then there's nothing to do.
	 */
	if (sblk->status_idx == rxr->last_status_idx &&
	    (REG_RD(sc, BCE_PCICFG_MISC_STATUS) &
	     BCE_PCICFG_MISC_STATUS_INTA_VALUE))
		return;

	/* Ack the interrupt and stop others from occuring. */
	REG_WR(sc, BCE_PCICFG_INT_ACK_CMD,
	       BCE_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
	       BCE_PCICFG_INT_ACK_CMD_MASK_INT);

	/*
	 * Read back to deassert IRQ immediately to avoid too
	 * many spurious interrupts.
	 */
	REG_RD(sc, BCE_PCICFG_INT_ACK_CMD);

	bce_intr(sc);

	/* Re-enable interrupts. */
	REG_WR(sc, BCE_PCICFG_INT_ACK_CMD,
	       BCE_PCICFG_INT_ACK_CMD_INDEX_VALID |
	       BCE_PCICFG_INT_ACK_CMD_MASK_INT | rxr->last_status_idx);
	bce_reenable_intr(rxr);
}

static void
bce_intr_msi(void *xsc)
{
	struct bce_softc *sc = xsc;

	/* Ack the interrupt and stop others from occuring. */
	REG_WR(sc, BCE_PCICFG_INT_ACK_CMD,
	       BCE_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
	       BCE_PCICFG_INT_ACK_CMD_MASK_INT);

	bce_intr(sc);

	/* Re-enable interrupts */
	bce_reenable_intr(&sc->rx_rings[0]);
}

static void
bce_intr_msi_oneshot(void *xsc)
{
	struct bce_softc *sc = xsc;

	bce_intr(sc);

	/* Re-enable interrupts */
	bce_reenable_intr(&sc->rx_rings[0]);
}

static void
bce_intr_msix_rxtx(void *xrxr)
{
	struct bce_rx_ring *rxr = xrxr;
	struct bce_tx_ring *txr;
	uint16_t hw_rx_cons, hw_tx_cons;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	KKASSERT(rxr->idx < rxr->sc->tx_ring_cnt);
	txr = &rxr->sc->tx_rings[rxr->idx];

	/*
	 * Save the status block index value for use during
	 * the next interrupt.
	 */
	rxr->last_status_idx = *rxr->hw_status_idx;

	/* Make sure status index is extracted before RX/TX cons */
	cpu_lfence();

	/* Check if the hardware has finished any work. */
	hw_rx_cons = bce_get_hw_rx_cons(rxr);
	if (hw_rx_cons != rxr->rx_cons)
		bce_rx_intr(rxr, -1, hw_rx_cons);

	/* Check for any completed TX frames. */
	hw_tx_cons = bce_get_hw_tx_cons(txr);
	lwkt_serialize_enter(&txr->tx_serialize);
	if (hw_tx_cons != txr->tx_cons) {
		bce_tx_intr(txr, hw_tx_cons);
		if (!ifsq_is_empty(txr->ifsq))
			ifsq_devstart(txr->ifsq);
	}
	lwkt_serialize_exit(&txr->tx_serialize);

	/* Re-enable interrupts */
	bce_reenable_intr(rxr);
}

static void
bce_intr_msix_rx(void *xrxr)
{
	struct bce_rx_ring *rxr = xrxr;
	uint16_t hw_rx_cons;

	ASSERT_SERIALIZED(&rxr->rx_serialize);

	/*
	 * Save the status block index value for use during
	 * the next interrupt.
	 */
	rxr->last_status_idx = *rxr->hw_status_idx;

	/* Make sure status index is extracted before RX cons */
	cpu_lfence();

	/* Check if the hardware has finished any work. */
	hw_rx_cons = bce_get_hw_rx_cons(rxr);
	if (hw_rx_cons != rxr->rx_cons)
		bce_rx_intr(rxr, -1, hw_rx_cons);

	/* Re-enable interrupts */
	bce_reenable_intr(rxr);
}

/****************************************************************************/
/* Programs the various packet receive modes (broadcast and multicast).     */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_set_rx_mode(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t hashes[NUM_MC_HASH_REGISTERS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	uint32_t rx_mode, sort_mode;
	int h, i;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/* Initialize receive mode default settings. */
	rx_mode = sc->rx_mode &
		  ~(BCE_EMAC_RX_MODE_PROMISCUOUS |
		    BCE_EMAC_RX_MODE_KEEP_VLAN_TAG);
	sort_mode = 1 | BCE_RPM_SORT_USER0_BC_EN;

	/*
	 * ASF/IPMI/UMP firmware requires that VLAN tag stripping
	 * be enbled.
	 */
	if (!(BCE_IF_CAPABILITIES & IFCAP_VLAN_HWTAGGING) &&
	    !(sc->bce_flags & BCE_MFW_ENABLE_FLAG))
		rx_mode |= BCE_EMAC_RX_MODE_KEEP_VLAN_TAG;

	/*
	 * Check for promiscuous, all multicast, or selected
	 * multicast address filtering.
	 */
	if (ifp->if_flags & IFF_PROMISC) {
		/* Enable promiscuous mode. */
		rx_mode |= BCE_EMAC_RX_MODE_PROMISCUOUS;
		sort_mode |= BCE_RPM_SORT_USER0_PROM_EN;
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		/* Enable all multicast addresses. */
		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(sc, BCE_EMAC_MULTICAST_HASH0 + (i * 4),
			       0xffffffff);
		}
		sort_mode |= BCE_RPM_SORT_USER0_MC_EN;
	} else {
		/* Accept one or more multicast(s). */
		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = ether_crc32_le(
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			    ETHER_ADDR_LEN) & 0xFF;
			hashes[(h & 0xE0) >> 5] |= 1 << (h & 0x1F);
		}

		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(sc, BCE_EMAC_MULTICAST_HASH0 + (i * 4),
			       hashes[i]);
		}
		sort_mode |= BCE_RPM_SORT_USER0_MC_HSH_EN;
	}

	/* Only make changes if the recive mode has actually changed. */
	if (rx_mode != sc->rx_mode) {
		sc->rx_mode = rx_mode;
		REG_WR(sc, BCE_EMAC_RX_MODE, rx_mode);
	}

	/* Disable and clear the exisitng sort before enabling a new sort. */
	REG_WR(sc, BCE_RPM_SORT_USER0, 0x0);
	REG_WR(sc, BCE_RPM_SORT_USER0, sort_mode);
	REG_WR(sc, BCE_RPM_SORT_USER0, sort_mode | BCE_RPM_SORT_USER0_ENA);
}

/****************************************************************************/
/* Called periodically to updates statistics from the controllers           */
/* statistics block.                                                        */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_stats_update(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct statistics_block *stats = sc->stats_block;

	ASSERT_SERIALIZED(&sc->main_serialize);

	/* 
	 * Certain controllers don't report carrier sense errors correctly.
	 * See errata E11_5708CA0_1165.
	 */
	if (!(BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5706) &&
	    !(BCE_CHIP_ID(sc) == BCE_CHIP_ID_5708_A0)) {
		IFNET_STAT_INC(ifp, oerrors,
			(u_long)stats->stat_Dot3StatsCarrierSenseErrors);
	}

	/*
	 * Update the sysctl statistics from the hardware statistics.
	 */
	sc->stat_IfHCInOctets =
		((uint64_t)stats->stat_IfHCInOctets_hi << 32) +
		 (uint64_t)stats->stat_IfHCInOctets_lo;

	sc->stat_IfHCInBadOctets =
		((uint64_t)stats->stat_IfHCInBadOctets_hi << 32) +
		 (uint64_t)stats->stat_IfHCInBadOctets_lo;

	sc->stat_IfHCOutOctets =
		((uint64_t)stats->stat_IfHCOutOctets_hi << 32) +
		 (uint64_t)stats->stat_IfHCOutOctets_lo;

	sc->stat_IfHCOutBadOctets =
		((uint64_t)stats->stat_IfHCOutBadOctets_hi << 32) +
		 (uint64_t)stats->stat_IfHCOutBadOctets_lo;

	sc->stat_IfHCInUcastPkts =
		((uint64_t)stats->stat_IfHCInUcastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCInUcastPkts_lo;

	sc->stat_IfHCInMulticastPkts =
		((uint64_t)stats->stat_IfHCInMulticastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCInMulticastPkts_lo;

	sc->stat_IfHCInBroadcastPkts =
		((uint64_t)stats->stat_IfHCInBroadcastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCInBroadcastPkts_lo;

	sc->stat_IfHCOutUcastPkts =
		((uint64_t)stats->stat_IfHCOutUcastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCOutUcastPkts_lo;

	sc->stat_IfHCOutMulticastPkts =
		((uint64_t)stats->stat_IfHCOutMulticastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCOutMulticastPkts_lo;

	sc->stat_IfHCOutBroadcastPkts =
		((uint64_t)stats->stat_IfHCOutBroadcastPkts_hi << 32) +
		 (uint64_t)stats->stat_IfHCOutBroadcastPkts_lo;

	sc->stat_emac_tx_stat_dot3statsinternalmactransmiterrors =
		stats->stat_emac_tx_stat_dot3statsinternalmactransmiterrors;

	sc->stat_Dot3StatsCarrierSenseErrors =
		stats->stat_Dot3StatsCarrierSenseErrors;

	sc->stat_Dot3StatsFCSErrors =
		stats->stat_Dot3StatsFCSErrors;

	sc->stat_Dot3StatsAlignmentErrors =
		stats->stat_Dot3StatsAlignmentErrors;

	sc->stat_Dot3StatsSingleCollisionFrames =
		stats->stat_Dot3StatsSingleCollisionFrames;

	sc->stat_Dot3StatsMultipleCollisionFrames =
		stats->stat_Dot3StatsMultipleCollisionFrames;

	sc->stat_Dot3StatsDeferredTransmissions =
		stats->stat_Dot3StatsDeferredTransmissions;

	sc->stat_Dot3StatsExcessiveCollisions =
		stats->stat_Dot3StatsExcessiveCollisions;

	sc->stat_Dot3StatsLateCollisions =
		stats->stat_Dot3StatsLateCollisions;

	sc->stat_EtherStatsCollisions =
		stats->stat_EtherStatsCollisions;

	sc->stat_EtherStatsFragments =
		stats->stat_EtherStatsFragments;

	sc->stat_EtherStatsJabbers =
		stats->stat_EtherStatsJabbers;

	sc->stat_EtherStatsUndersizePkts =
		stats->stat_EtherStatsUndersizePkts;

	sc->stat_EtherStatsOverrsizePkts =
		stats->stat_EtherStatsOverrsizePkts;

	sc->stat_EtherStatsPktsRx64Octets =
		stats->stat_EtherStatsPktsRx64Octets;

	sc->stat_EtherStatsPktsRx65Octetsto127Octets =
		stats->stat_EtherStatsPktsRx65Octetsto127Octets;

	sc->stat_EtherStatsPktsRx128Octetsto255Octets =
		stats->stat_EtherStatsPktsRx128Octetsto255Octets;

	sc->stat_EtherStatsPktsRx256Octetsto511Octets =
		stats->stat_EtherStatsPktsRx256Octetsto511Octets;

	sc->stat_EtherStatsPktsRx512Octetsto1023Octets =
		stats->stat_EtherStatsPktsRx512Octetsto1023Octets;

	sc->stat_EtherStatsPktsRx1024Octetsto1522Octets =
		stats->stat_EtherStatsPktsRx1024Octetsto1522Octets;

	sc->stat_EtherStatsPktsRx1523Octetsto9022Octets =
		stats->stat_EtherStatsPktsRx1523Octetsto9022Octets;

	sc->stat_EtherStatsPktsTx64Octets =
		stats->stat_EtherStatsPktsTx64Octets;

	sc->stat_EtherStatsPktsTx65Octetsto127Octets =
		stats->stat_EtherStatsPktsTx65Octetsto127Octets;

	sc->stat_EtherStatsPktsTx128Octetsto255Octets =
		stats->stat_EtherStatsPktsTx128Octetsto255Octets;

	sc->stat_EtherStatsPktsTx256Octetsto511Octets =
		stats->stat_EtherStatsPktsTx256Octetsto511Octets;

	sc->stat_EtherStatsPktsTx512Octetsto1023Octets =
		stats->stat_EtherStatsPktsTx512Octetsto1023Octets;

	sc->stat_EtherStatsPktsTx1024Octetsto1522Octets =
		stats->stat_EtherStatsPktsTx1024Octetsto1522Octets;

	sc->stat_EtherStatsPktsTx1523Octetsto9022Octets =
		stats->stat_EtherStatsPktsTx1523Octetsto9022Octets;

	sc->stat_XonPauseFramesReceived =
		stats->stat_XonPauseFramesReceived;

	sc->stat_XoffPauseFramesReceived =
		stats->stat_XoffPauseFramesReceived;

	sc->stat_OutXonSent =
		stats->stat_OutXonSent;

	sc->stat_OutXoffSent =
		stats->stat_OutXoffSent;

	sc->stat_FlowControlDone =
		stats->stat_FlowControlDone;

	sc->stat_MacControlFramesReceived =
		stats->stat_MacControlFramesReceived;

	sc->stat_XoffStateEntered =
		stats->stat_XoffStateEntered;

	sc->stat_IfInFramesL2FilterDiscards =
		stats->stat_IfInFramesL2FilterDiscards;

	sc->stat_IfInRuleCheckerDiscards =
		stats->stat_IfInRuleCheckerDiscards;

	sc->stat_IfInFTQDiscards =
		stats->stat_IfInFTQDiscards;

	sc->stat_IfInMBUFDiscards =
		stats->stat_IfInMBUFDiscards;

	sc->stat_IfInRuleCheckerP4Hit =
		stats->stat_IfInRuleCheckerP4Hit;

	sc->stat_CatchupInRuleCheckerDiscards =
		stats->stat_CatchupInRuleCheckerDiscards;

	sc->stat_CatchupInFTQDiscards =
		stats->stat_CatchupInFTQDiscards;

	sc->stat_CatchupInMBUFDiscards =
		stats->stat_CatchupInMBUFDiscards;

	sc->stat_CatchupInRuleCheckerP4Hit =
		stats->stat_CatchupInRuleCheckerP4Hit;

	sc->com_no_buffers = REG_RD_IND(sc, 0x120084);

	/*
	 * Update the interface statistics from the
	 * hardware statistics.
	 */
	IFNET_STAT_SET(ifp, collisions, (u_long)sc->stat_EtherStatsCollisions);

	IFNET_STAT_SET(ifp, ierrors, (u_long)sc->stat_EtherStatsUndersizePkts +
	    (u_long)sc->stat_EtherStatsOverrsizePkts +
	    (u_long)sc->stat_IfInMBUFDiscards +
	    (u_long)sc->stat_Dot3StatsAlignmentErrors +
	    (u_long)sc->stat_Dot3StatsFCSErrors +
	    (u_long)sc->stat_IfInRuleCheckerDiscards +
	    (u_long)sc->stat_IfInFTQDiscards +
	    (u_long)sc->com_no_buffers);

	IFNET_STAT_SET(ifp, oerrors,
	    (u_long)sc->stat_emac_tx_stat_dot3statsinternalmactransmiterrors +
	    (u_long)sc->stat_Dot3StatsExcessiveCollisions +
	    (u_long)sc->stat_Dot3StatsLateCollisions);
}

/****************************************************************************/
/* Periodic function to notify the bootcode that the driver is still        */
/* present.                                                                 */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_pulse(void *xsc)
{
	struct bce_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t msg;

	lwkt_serialize_enter(&sc->main_serialize);

	/* Tell the firmware that the driver is still running. */
	msg = (uint32_t)++sc->bce_fw_drv_pulse_wr_seq;
	bce_shmem_wr(sc, BCE_DRV_PULSE_MB, msg);

	/* Update the bootcode condition. */
	sc->bc_state = bce_shmem_rd(sc, BCE_BC_STATE_CONDITION);

	/* Report whether the bootcode still knows the driver is running. */
	if (!sc->bce_drv_cardiac_arrest) {
		if (!(sc->bc_state & BCE_CONDITION_DRV_PRESENT)) {
			sc->bce_drv_cardiac_arrest = 1;
			if_printf(ifp, "Bootcode lost the driver pulse! "
			    "(bc_state = 0x%08X)\n", sc->bc_state);
		}
	} else {
 		/*
 		 * Not supported by all bootcode versions.
 		 * (v5.0.11+ and v5.2.1+)  Older bootcode
 		 * will require the driver to reset the
 		 * controller to clear this condition.
		 */
		if (sc->bc_state & BCE_CONDITION_DRV_PRESENT) {
			sc->bce_drv_cardiac_arrest = 0;
			if_printf(ifp, "Bootcode found the driver pulse! "
			    "(bc_state = 0x%08X)\n", sc->bc_state);
		}
	}

	/* Schedule the next pulse. */
	callout_reset_bycpu(&sc->bce_pulse_callout, hz, bce_pulse, sc,
	    sc->bce_timer_cpuid);

	lwkt_serialize_exit(&sc->main_serialize);
}

/****************************************************************************/
/* Periodic function to check whether MSI is lost                           */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_check_msi(void *xsc)
{
	struct bce_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct status_block *sblk = sc->status_block;
	struct bce_tx_ring *txr = &sc->tx_rings[0];
	struct bce_rx_ring *rxr = &sc->rx_rings[0];

	lwkt_serialize_enter(&sc->main_serialize);

	KKASSERT(mycpuid == sc->bce_msix[0].msix_cpuid);

	if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) != IFF_RUNNING) {
		lwkt_serialize_exit(&sc->main_serialize);
		return;
	}

	if (bce_get_hw_rx_cons(rxr) != rxr->rx_cons ||
	    bce_get_hw_tx_cons(txr) != txr->tx_cons ||
	    (sblk->status_attn_bits & STATUS_ATTN_BITS_LINK_STATE) !=
	    (sblk->status_attn_bits_ack & STATUS_ATTN_BITS_LINK_STATE)) {
		if (sc->bce_check_rx_cons == rxr->rx_cons &&
		    sc->bce_check_tx_cons == txr->tx_cons &&
		    sc->bce_check_status_idx == rxr->last_status_idx) {
			uint32_t msi_ctrl;

			if (!sc->bce_msi_maylose) {
				sc->bce_msi_maylose = TRUE;
				goto done;
			}

			msi_ctrl = REG_RD(sc, BCE_PCICFG_MSI_CONTROL);
			if (msi_ctrl & BCE_PCICFG_MSI_CONTROL_ENABLE) {
				if (bootverbose)
					if_printf(ifp, "lost MSI\n");

				REG_WR(sc, BCE_PCICFG_MSI_CONTROL,
				    msi_ctrl & ~BCE_PCICFG_MSI_CONTROL_ENABLE);
				REG_WR(sc, BCE_PCICFG_MSI_CONTROL, msi_ctrl);

				bce_intr_msi(sc);
			} else if (bootverbose) {
				if_printf(ifp, "MSI may be lost\n");
			}
		}
	}
	sc->bce_msi_maylose = FALSE;
	sc->bce_check_rx_cons = rxr->rx_cons;
	sc->bce_check_tx_cons = txr->tx_cons;
	sc->bce_check_status_idx = rxr->last_status_idx;

done:
	callout_reset(&sc->bce_ckmsi_callout, BCE_MSI_CKINTVL,
	    bce_check_msi, sc);
	lwkt_serialize_exit(&sc->main_serialize);
}

/****************************************************************************/
/* Periodic function to perform maintenance tasks.                          */
/*                                                                          */
/* Returns:                                                                 */
/*   Nothing.                                                               */
/****************************************************************************/
static void
bce_tick_serialized(struct bce_softc *sc)
{
	struct mii_data *mii;

	ASSERT_SERIALIZED(&sc->main_serialize);

	/* Update the statistics from the hardware statistics block. */
	bce_stats_update(sc);

	/* Schedule the next tick. */
	callout_reset_bycpu(&sc->bce_tick_callout, hz, bce_tick, sc,
	    sc->bce_timer_cpuid);

	/* If link is up already up then we're done. */
	if (sc->bce_link)
		return;

	mii = device_get_softc(sc->bce_miibus);
	mii_tick(mii);

	/* Check if the link has come up. */
	if ((mii->mii_media_status & IFM_ACTIVE) &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		int i;

		sc->bce_link++;
		/* Now that link is up, handle any outstanding TX traffic. */
		for (i = 0; i < sc->tx_ring_cnt; ++i)
			ifsq_devstart_sched(sc->tx_rings[i].ifsq);
	}
}

static void
bce_tick(void *xsc)
{
	struct bce_softc *sc = xsc;

	lwkt_serialize_enter(&sc->main_serialize);
	bce_tick_serialized(sc);
	lwkt_serialize_exit(&sc->main_serialize);
}

/****************************************************************************/
/* Adds any sysctl parameters for tuning or debugging purposes.             */
/*                                                                          */
/* Returns:                                                                 */
/*   0 for success, positive value for failure.                             */
/****************************************************************************/
static void
bce_add_sysctls(struct bce_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
#if defined(BCE_TSS_DEBUG) || defined(BCE_RSS_DEBUG)
	char node[32];
	int i;
#endif

	ctx = device_get_sysctl_ctx(sc->bce_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->bce_dev));

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_bds_int",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_tx_bds_int, "I",
			"Send max coalesced BD count during interrupt");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_tx_bds, "I",
			"Send max coalesced BD count");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_ticks_int",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_tx_ticks_int, "I",
			"Send coalescing ticks during interrupt");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_tx_ticks, "I",
			"Send coalescing ticks");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_bds_int",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_rx_bds_int, "I",
			"Receive max coalesced BD count during interrupt");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_rx_bds, "I",
			"Receive max coalesced BD count");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_ticks_int",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_rx_ticks_int, "I",
			"Receive coalescing ticks during interrupt");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bce_sysctl_rx_ticks, "I",
			"Receive coalescing ticks");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_rings",
		CTLFLAG_RD, &sc->rx_ring_cnt, 0, "# of RX rings");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rx_pages",
		CTLFLAG_RD, &sc->rx_rings[0].rx_pages, 0, "# of RX pages");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_rings",
		CTLFLAG_RD, &sc->tx_ring_cnt, 0, "# of TX rings");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_pages",
		CTLFLAG_RD, &sc->tx_rings[0].tx_pages, 0, "# of TX pages");

	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_wreg",
	    	CTLFLAG_RW, &sc->tx_rings[0].tx_wreg, 0,
		"# segments before write to hardware registers");

	if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX) {
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD, sc->tx_rmap, 0,
		    if_ringmap_cpumap_sysctl, "I", "TX ring CPU map");
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD, sc->rx_rmap, 0,
		    if_ringmap_cpumap_sysctl, "I", "RX ring CPU map");
	} else {
#ifdef IFPOLL_ENABLE
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_poll_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD, sc->tx_rmap, 0,
		    if_ringmap_cpumap_sysctl, "I", "TX poll CPU map");
		SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_poll_cpumap",
		    CTLTYPE_OPAQUE | CTLFLAG_RD, sc->rx_rmap, 0,
		    if_ringmap_cpumap_sysctl, "I", "RX poll CPU map");
#endif
	}

#ifdef BCE_RSS_DEBUG
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "rss_debug",
	    CTLFLAG_RW, &sc->rss_debug, 0, "RSS debug level");
	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "rx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, node,
		    CTLFLAG_RW, &sc->rx_rings[i].rx_pkts,
		    "RXed packets");
	}
#endif

#ifdef BCE_TSS_DEBUG
	for (i = 0; i < sc->tx_ring_cnt; ++i) {
		ksnprintf(node, sizeof(node), "tx%d_pkt", i);
		SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, node,
		    CTLFLAG_RW, &sc->tx_rings[i].tx_pkts,
		    "TXed packets");
	}
#endif

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCInOctets",
		CTLFLAG_RD, &sc->stat_IfHCInOctets,
		"Bytes received");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCInBadOctets",
		CTLFLAG_RD, &sc->stat_IfHCInBadOctets,
		"Bad bytes received");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCOutOctets",
		CTLFLAG_RD, &sc->stat_IfHCOutOctets,
		"Bytes sent");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCOutBadOctets",
		CTLFLAG_RD, &sc->stat_IfHCOutBadOctets,
		"Bad bytes sent");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCInUcastPkts",
		CTLFLAG_RD, &sc->stat_IfHCInUcastPkts,
		"Unicast packets received");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCInMulticastPkts",
		CTLFLAG_RD, &sc->stat_IfHCInMulticastPkts,
		"Multicast packets received");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCInBroadcastPkts",
		CTLFLAG_RD, &sc->stat_IfHCInBroadcastPkts,
		"Broadcast packets received");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCOutUcastPkts",
		CTLFLAG_RD, &sc->stat_IfHCOutUcastPkts,
		"Unicast packets sent");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCOutMulticastPkts",
		CTLFLAG_RD, &sc->stat_IfHCOutMulticastPkts,
		"Multicast packets sent");

	SYSCTL_ADD_ULONG(ctx, children, OID_AUTO, 
		"stat_IfHCOutBroadcastPkts",
		CTLFLAG_RD, &sc->stat_IfHCOutBroadcastPkts,
		"Broadcast packets sent");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_emac_tx_stat_dot3statsinternalmactransmiterrors",
		CTLFLAG_RD, &sc->stat_emac_tx_stat_dot3statsinternalmactransmiterrors,
		0, "Internal MAC transmit errors");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsCarrierSenseErrors",
		CTLFLAG_RD, &sc->stat_Dot3StatsCarrierSenseErrors,
		0, "Carrier sense errors");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsFCSErrors",
		CTLFLAG_RD, &sc->stat_Dot3StatsFCSErrors,
		0, "Frame check sequence errors");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsAlignmentErrors",
		CTLFLAG_RD, &sc->stat_Dot3StatsAlignmentErrors,
		0, "Alignment errors");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsSingleCollisionFrames",
		CTLFLAG_RD, &sc->stat_Dot3StatsSingleCollisionFrames,
		0, "Single Collision Frames");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsMultipleCollisionFrames",
		CTLFLAG_RD, &sc->stat_Dot3StatsMultipleCollisionFrames,
		0, "Multiple Collision Frames");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsDeferredTransmissions",
		CTLFLAG_RD, &sc->stat_Dot3StatsDeferredTransmissions,
		0, "Deferred Transmissions");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsExcessiveCollisions",
		CTLFLAG_RD, &sc->stat_Dot3StatsExcessiveCollisions,
		0, "Excessive Collisions");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_Dot3StatsLateCollisions",
		CTLFLAG_RD, &sc->stat_Dot3StatsLateCollisions,
		0, "Late Collisions");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsCollisions",
		CTLFLAG_RD, &sc->stat_EtherStatsCollisions,
		0, "Collisions");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsFragments",
		CTLFLAG_RD, &sc->stat_EtherStatsFragments,
		0, "Fragments");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsJabbers",
		CTLFLAG_RD, &sc->stat_EtherStatsJabbers,
		0, "Jabbers");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsUndersizePkts",
		CTLFLAG_RD, &sc->stat_EtherStatsUndersizePkts,
		0, "Undersize packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsOverrsizePkts",
		CTLFLAG_RD, &sc->stat_EtherStatsOverrsizePkts,
		0, "stat_EtherStatsOverrsizePkts");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx64Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx64Octets,
		0, "Bytes received in 64 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx65Octetsto127Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx65Octetsto127Octets,
		0, "Bytes received in 65 to 127 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx128Octetsto255Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx128Octetsto255Octets,
		0, "Bytes received in 128 to 255 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx256Octetsto511Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx256Octetsto511Octets,
		0, "Bytes received in 256 to 511 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx512Octetsto1023Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx512Octetsto1023Octets,
		0, "Bytes received in 512 to 1023 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx1024Octetsto1522Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx1024Octetsto1522Octets,
		0, "Bytes received in 1024 t0 1522 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsRx1523Octetsto9022Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsRx1523Octetsto9022Octets,
		0, "Bytes received in 1523 to 9022 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx64Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx64Octets,
		0, "Bytes sent in 64 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx65Octetsto127Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx65Octetsto127Octets,
		0, "Bytes sent in 65 to 127 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx128Octetsto255Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx128Octetsto255Octets,
		0, "Bytes sent in 128 to 255 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx256Octetsto511Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx256Octetsto511Octets,
		0, "Bytes sent in 256 to 511 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx512Octetsto1023Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx512Octetsto1023Octets,
		0, "Bytes sent in 512 to 1023 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx1024Octetsto1522Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx1024Octetsto1522Octets,
		0, "Bytes sent in 1024 to 1522 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_EtherStatsPktsTx1523Octetsto9022Octets",
		CTLFLAG_RD, &sc->stat_EtherStatsPktsTx1523Octetsto9022Octets,
		0, "Bytes sent in 1523 to 9022 byte packets");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_XonPauseFramesReceived",
		CTLFLAG_RD, &sc->stat_XonPauseFramesReceived,
		0, "XON pause frames receved");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_XoffPauseFramesReceived",
		CTLFLAG_RD, &sc->stat_XoffPauseFramesReceived,
		0, "XOFF pause frames received");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_OutXonSent",
		CTLFLAG_RD, &sc->stat_OutXonSent,
		0, "XON pause frames sent");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_OutXoffSent",
		CTLFLAG_RD, &sc->stat_OutXoffSent,
		0, "XOFF pause frames sent");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_FlowControlDone",
		CTLFLAG_RD, &sc->stat_FlowControlDone,
		0, "Flow control done");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_MacControlFramesReceived",
		CTLFLAG_RD, &sc->stat_MacControlFramesReceived,
		0, "MAC control frames received");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_XoffStateEntered",
		CTLFLAG_RD, &sc->stat_XoffStateEntered,
		0, "XOFF state entered");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_IfInFramesL2FilterDiscards",
		CTLFLAG_RD, &sc->stat_IfInFramesL2FilterDiscards,
		0, "Received L2 packets discarded");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_IfInRuleCheckerDiscards",
		CTLFLAG_RD, &sc->stat_IfInRuleCheckerDiscards,
		0, "Received packets discarded by rule");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_IfInFTQDiscards",
		CTLFLAG_RD, &sc->stat_IfInFTQDiscards,
		0, "Received packet FTQ discards");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_IfInMBUFDiscards",
		CTLFLAG_RD, &sc->stat_IfInMBUFDiscards,
		0, "Received packets discarded due to lack of controller buffer memory");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_IfInRuleCheckerP4Hit",
		CTLFLAG_RD, &sc->stat_IfInRuleCheckerP4Hit,
		0, "Received packets rule checker hits");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_CatchupInRuleCheckerDiscards",
		CTLFLAG_RD, &sc->stat_CatchupInRuleCheckerDiscards,
		0, "Received packets discarded in Catchup path");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_CatchupInFTQDiscards",
		CTLFLAG_RD, &sc->stat_CatchupInFTQDiscards,
		0, "Received packets discarded in FTQ in Catchup path");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_CatchupInMBUFDiscards",
		CTLFLAG_RD, &sc->stat_CatchupInMBUFDiscards,
		0, "Received packets discarded in controller buffer memory in Catchup path");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"stat_CatchupInRuleCheckerP4Hit",
		CTLFLAG_RD, &sc->stat_CatchupInRuleCheckerP4Hit,
		0, "Received packets rule checker hits in Catchup path");

	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, 
		"com_no_buffers",
		CTLFLAG_RD, &sc->com_no_buffers,
		0, "Valid packets received but no RX buffers available");
}

static int
bce_sysctl_tx_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_tx_quick_cons_trip_int,
			BCE_COALMASK_TX_BDS_INT);
}

static int
bce_sysctl_tx_bds(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_tx_quick_cons_trip,
			BCE_COALMASK_TX_BDS);
}

static int
bce_sysctl_tx_ticks_int(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_tx_ticks_int,
			BCE_COALMASK_TX_TICKS_INT);
}

static int
bce_sysctl_tx_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_tx_ticks,
			BCE_COALMASK_TX_TICKS);
}

static int
bce_sysctl_rx_bds_int(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_rx_quick_cons_trip_int,
			BCE_COALMASK_RX_BDS_INT);
}

static int
bce_sysctl_rx_bds(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_rx_quick_cons_trip,
			BCE_COALMASK_RX_BDS);
}

static int
bce_sysctl_rx_ticks_int(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_rx_ticks_int,
			BCE_COALMASK_RX_TICKS_INT);
}

static int
bce_sysctl_rx_ticks(SYSCTL_HANDLER_ARGS)
{
	struct bce_softc *sc = arg1;

	return bce_sysctl_coal_change(oidp, arg1, arg2, req,
			&sc->bce_rx_ticks,
			BCE_COALMASK_RX_TICKS);
}

static int
bce_sysctl_coal_change(SYSCTL_HANDLER_ARGS, uint32_t *coal,
    uint32_t coalchg_mask)
{
	struct bce_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0, v;

	ifnet_serialize_all(ifp);

	v = *coal;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != NULL) {
		if (v < 0) {
			error = EINVAL;
		} else {
			*coal = v;
			sc->bce_coalchg_mask |= coalchg_mask;

			/* Commit changes */
			bce_coal_change(sc);
		}
	}

	ifnet_deserialize_all(ifp);
	return error;
}

static void
bce_coal_change(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(&sc->main_serialize);

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		sc->bce_coalchg_mask = 0;
		return;
	}

	if (sc->bce_coalchg_mask &
	    (BCE_COALMASK_TX_BDS | BCE_COALMASK_TX_BDS_INT)) {
		REG_WR(sc, BCE_HC_TX_QUICK_CONS_TRIP,
		       (sc->bce_tx_quick_cons_trip_int << 16) |
		       sc->bce_tx_quick_cons_trip);
		for (i = 1; i < sc->rx_ring_cnt; ++i) {
			uint32_t base;

			base = ((i - 1) * BCE_HC_SB_CONFIG_SIZE) +
			    BCE_HC_SB_CONFIG_1;
			REG_WR(sc, base + BCE_HC_TX_QUICK_CONS_TRIP_OFF,
			    (sc->bce_tx_quick_cons_trip_int << 16) |
			    sc->bce_tx_quick_cons_trip);
		}
		if (bootverbose) {
			if_printf(ifp, "tx_bds %u, tx_bds_int %u\n",
				  sc->bce_tx_quick_cons_trip,
				  sc->bce_tx_quick_cons_trip_int);
		}
	}

	if (sc->bce_coalchg_mask &
	    (BCE_COALMASK_TX_TICKS | BCE_COALMASK_TX_TICKS_INT)) {
		REG_WR(sc, BCE_HC_TX_TICKS,
		       (sc->bce_tx_ticks_int << 16) | sc->bce_tx_ticks);
		for (i = 1; i < sc->rx_ring_cnt; ++i) {
			uint32_t base;

			base = ((i - 1) * BCE_HC_SB_CONFIG_SIZE) +
			    BCE_HC_SB_CONFIG_1;
			REG_WR(sc, base + BCE_HC_TX_TICKS_OFF,
			    (sc->bce_tx_ticks_int << 16) | sc->bce_tx_ticks);
		}
		if (bootverbose) {
			if_printf(ifp, "tx_ticks %u, tx_ticks_int %u\n",
				  sc->bce_tx_ticks, sc->bce_tx_ticks_int);
		}
	}

	if (sc->bce_coalchg_mask &
	    (BCE_COALMASK_RX_BDS | BCE_COALMASK_RX_BDS_INT)) {
		REG_WR(sc, BCE_HC_RX_QUICK_CONS_TRIP,
		       (sc->bce_rx_quick_cons_trip_int << 16) |
		       sc->bce_rx_quick_cons_trip);
		for (i = 1; i < sc->rx_ring_cnt; ++i) {
			uint32_t base;

			base = ((i - 1) * BCE_HC_SB_CONFIG_SIZE) +
			    BCE_HC_SB_CONFIG_1;
			REG_WR(sc, base + BCE_HC_RX_QUICK_CONS_TRIP_OFF,
			    (sc->bce_rx_quick_cons_trip_int << 16) |
			    sc->bce_rx_quick_cons_trip);
		}
		if (bootverbose) {
			if_printf(ifp, "rx_bds %u, rx_bds_int %u\n",
				  sc->bce_rx_quick_cons_trip,
				  sc->bce_rx_quick_cons_trip_int);
		}
	}

	if (sc->bce_coalchg_mask &
	    (BCE_COALMASK_RX_TICKS | BCE_COALMASK_RX_TICKS_INT)) {
		REG_WR(sc, BCE_HC_RX_TICKS,
		       (sc->bce_rx_ticks_int << 16) | sc->bce_rx_ticks);
		for (i = 1; i < sc->rx_ring_cnt; ++i) {
			uint32_t base;

			base = ((i - 1) * BCE_HC_SB_CONFIG_SIZE) +
			    BCE_HC_SB_CONFIG_1;
			REG_WR(sc, base + BCE_HC_RX_TICKS_OFF,
			    (sc->bce_rx_ticks_int << 16) | sc->bce_rx_ticks);
		}
		if (bootverbose) {
			if_printf(ifp, "rx_ticks %u, rx_ticks_int %u\n",
				  sc->bce_rx_ticks, sc->bce_rx_ticks_int);
		}
	}

	sc->bce_coalchg_mask = 0;
}

static int
bce_tso_setup(struct bce_tx_ring *txr, struct mbuf **mp,
    uint16_t *flags0, uint16_t *mss0)
{
	struct mbuf *m;
	uint16_t flags;
	int thoff, iphlen, hoff;

	m = *mp;
	KASSERT(M_WRITABLE(m), ("TSO mbuf not writable"));

	hoff = m->m_pkthdr.csum_lhlen;
	iphlen = m->m_pkthdr.csum_iphlen;
	thoff = m->m_pkthdr.csum_thlen;

	KASSERT(hoff >= sizeof(struct ether_header),
	    ("invalid ether header len %d", hoff));
	KASSERT(iphlen >= sizeof(struct ip),
	    ("invalid ip header len %d", iphlen));
	KASSERT(thoff >= sizeof(struct tcphdr),
	    ("invalid tcp header len %d", thoff));

	if (__predict_false(m->m_len < hoff + iphlen + thoff)) {
		m = m_pullup(m, hoff + iphlen + thoff);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		*mp = m;
	}

	/* Set the LSO flag in the TX BD */
	flags = TX_BD_FLAGS_SW_LSO;

	/* Set the length of IP + TCP options (in 32 bit words) */
	flags |= (((iphlen + thoff -
	    sizeof(struct ip) - sizeof(struct tcphdr)) >> 2) << 8);

	*mss0 = htole16(m->m_pkthdr.tso_segsz);
	*flags0 = flags;

	return 0;
}

static void
bce_setup_serialize(struct bce_softc *sc)
{
	int i, j;

	/*
	 * Allocate serializer array
	 */

	/* Main + TX + RX */
	sc->serialize_cnt = 1 + sc->tx_ring_cnt + sc->rx_ring_cnt;

	sc->serializes =
	    kmalloc(sc->serialize_cnt * sizeof(struct lwkt_serialize *),
	        M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Setup serializers
	 *
	 * NOTE: Order is critical
	 */

	i = 0;

	KKASSERT(i < sc->serialize_cnt);
	sc->serializes[i++] = &sc->main_serialize;

	for (j = 0; j < sc->rx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->rx_rings[j].rx_serialize;
	}

	for (j = 0; j < sc->tx_ring_cnt; ++j) {
		KKASSERT(i < sc->serialize_cnt);
		sc->serializes[i++] = &sc->tx_rings[j].tx_serialize;
	}

	KKASSERT(i == sc->serialize_cnt);
}

static void
bce_serialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bce_softc *sc = ifp->if_softc;

	ifnet_serialize_array_enter(sc->serializes, sc->serialize_cnt, slz);
}

static void
bce_deserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bce_softc *sc = ifp->if_softc;

	ifnet_serialize_array_exit(sc->serializes, sc->serialize_cnt, slz);
}

static int
bce_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz)
{
	struct bce_softc *sc = ifp->if_softc;

	return ifnet_serialize_array_try(sc->serializes, sc->serialize_cnt,
	    slz);
}

#ifdef INVARIANTS

static void
bce_serialize_assert(struct ifnet *ifp, enum ifnet_serialize slz,
    boolean_t serialized)
{
	struct bce_softc *sc = ifp->if_softc;

	ifnet_serialize_array_assert(sc->serializes, sc->serialize_cnt,
	    slz, serialized);
}

#endif	/* INVARIANTS */

static void
bce_serialize_skipmain(struct bce_softc *sc)
{
	lwkt_serialize_array_enter(sc->serializes, sc->serialize_cnt, 1);
}

static void
bce_deserialize_skipmain(struct bce_softc *sc)
{
	lwkt_serialize_array_exit(sc->serializes, sc->serialize_cnt, 1);
}

static void
bce_set_timer_cpuid(struct bce_softc *sc, boolean_t polling)
{
	if (polling)
		sc->bce_timer_cpuid = 0; /* XXX */
	else
		sc->bce_timer_cpuid = sc->bce_msix[0].msix_cpuid;
}

static int
bce_alloc_intr(struct bce_softc *sc)
{
	u_int irq_flags;

	bce_try_alloc_msix(sc);
	if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX)
		return 0;

	sc->bce_irq_type = pci_alloc_1intr(sc->bce_dev, bce_msi_enable,
	    &sc->bce_irq_rid, &irq_flags);

	sc->bce_res_irq = bus_alloc_resource_any(sc->bce_dev, SYS_RES_IRQ,
	    &sc->bce_irq_rid, irq_flags);
	if (sc->bce_res_irq == NULL) {
		device_printf(sc->bce_dev, "PCI map interrupt failed\n");
		return ENXIO;
	}
	sc->bce_msix[0].msix_cpuid = rman_get_cpuid(sc->bce_res_irq);
	sc->bce_msix[0].msix_serialize = &sc->main_serialize;

	return 0;
}

static void
bce_try_alloc_msix(struct bce_softc *sc)
{
	struct bce_msix_data *msix;
	int i, error;
	boolean_t setup = FALSE;

	if (sc->rx_ring_cnt == 1)
		return;

	msix = &sc->bce_msix[0];
	msix->msix_serialize = &sc->main_serialize;
	msix->msix_func = bce_intr_msi_oneshot;
	msix->msix_arg = sc;
	msix->msix_cpuid = if_ringmap_cpumap(sc->rx_rmap, 0);
	KKASSERT(msix->msix_cpuid < netisr_ncpus);
	ksnprintf(msix->msix_desc, sizeof(msix->msix_desc), "%s combo",
	    device_get_nameunit(sc->bce_dev));

	for (i = 1; i < sc->rx_ring_cnt; ++i) {
		struct bce_rx_ring *rxr = &sc->rx_rings[i];

		msix = &sc->bce_msix[i];

		msix->msix_serialize = &rxr->rx_serialize;
		msix->msix_arg = rxr;
		msix->msix_cpuid = if_ringmap_cpumap(sc->rx_rmap,
		    i % sc->rx_ring_cnt2);
		KKASSERT(msix->msix_cpuid < netisr_ncpus);

		if (i < sc->tx_ring_cnt) {
			msix->msix_func = bce_intr_msix_rxtx;
			ksnprintf(msix->msix_desc, sizeof(msix->msix_desc),
			    "%s rxtx%d", device_get_nameunit(sc->bce_dev), i);
		} else {
			msix->msix_func = bce_intr_msix_rx;
			ksnprintf(msix->msix_desc, sizeof(msix->msix_desc),
			    "%s rx%d", device_get_nameunit(sc->bce_dev), i);
		}
	}

	/*
	 * Setup MSI-X table
	 */
	bce_setup_msix_table(sc);
	REG_WR(sc, BCE_PCI_MSIX_CONTROL, BCE_MSIX_MAX - 1);
	REG_WR(sc, BCE_PCI_MSIX_TBL_OFF_BIR, BCE_PCI_GRC_WINDOW2_BASE);
	REG_WR(sc, BCE_PCI_MSIX_PBA_OFF_BIT, BCE_PCI_GRC_WINDOW3_BASE);
	/* Flush */
	REG_RD(sc, BCE_PCI_MSIX_CONTROL);

	error = pci_setup_msix(sc->bce_dev);
	if (error) {
		device_printf(sc->bce_dev, "Setup MSI-X failed\n");
		goto back;
	}
	setup = TRUE;

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		msix = &sc->bce_msix[i];

		error = pci_alloc_msix_vector(sc->bce_dev, i, &msix->msix_rid,
		    msix->msix_cpuid);
		if (error) {
			device_printf(sc->bce_dev,
			    "Unable to allocate MSI-X %d on cpu%d\n",
			    i, msix->msix_cpuid);
			goto back;
		}

		msix->msix_res = bus_alloc_resource_any(sc->bce_dev,
		    SYS_RES_IRQ, &msix->msix_rid, RF_ACTIVE);
		if (msix->msix_res == NULL) {
			device_printf(sc->bce_dev,
			    "Unable to allocate MSI-X %d resource\n", i);
			error = ENOMEM;
			goto back;
		}
	}

	pci_enable_msix(sc->bce_dev);
	sc->bce_irq_type = PCI_INTR_TYPE_MSIX;
back:
	if (error)
		bce_free_msix(sc, setup);
}

static void
bce_setup_ring_cnt(struct bce_softc *sc)
{
	int msix_enable, msix_cnt, msix_ring;
	int ring_max, ring_cnt;

	sc->rx_rmap = if_ringmap_alloc(sc->bce_dev, 1, 1);

	if (BCE_CHIP_NUM(sc) != BCE_CHIP_NUM_5709 &&
	    BCE_CHIP_NUM(sc) != BCE_CHIP_NUM_5716)
		goto skip_rx;

	msix_enable = device_getenv_int(sc->bce_dev, "msix.enable",
	    bce_msix_enable);
	if (!msix_enable)
		goto skip_rx;

	if (netisr_ncpus == 1)
		goto skip_rx;

	/*
	 * One extra RX ring will be needed (see below), so make sure
	 * that there are enough MSI-X vectors.
	 */
	msix_cnt = pci_msix_count(sc->bce_dev);
	if (msix_cnt <= 2)
		goto skip_rx;
	msix_ring = msix_cnt - 1;

	/*
	 * Setup RX ring count
	 */
	ring_max = BCE_RX_RING_MAX;
	if (ring_max > msix_ring)
		ring_max = msix_ring;
	ring_cnt = device_getenv_int(sc->bce_dev, "rx_rings", bce_rx_rings);

	if_ringmap_free(sc->rx_rmap);
	sc->rx_rmap = if_ringmap_alloc(sc->bce_dev, ring_cnt, ring_max);

skip_rx:
	sc->rx_ring_cnt2 = if_ringmap_count(sc->rx_rmap);

	/*
	 * Setup TX ring count
	 *
	 * NOTE:
	 * TX ring count must be less than the effective RSS RX ring
	 * count, since we use RX ring software data struct to save
	 * status index and various other MSI-X related stuffs.
	 */
	ring_max = BCE_TX_RING_MAX;
	if (ring_max > sc->rx_ring_cnt2)
		ring_max = sc->rx_ring_cnt2;
	ring_cnt = device_getenv_int(sc->bce_dev, "tx_rings", bce_tx_rings);

	sc->tx_rmap = if_ringmap_alloc(sc->bce_dev, ring_cnt, ring_max);
	if_ringmap_align(sc->bce_dev, sc->rx_rmap, sc->tx_rmap);

	sc->tx_ring_cnt = if_ringmap_count(sc->tx_rmap);

	if (sc->rx_ring_cnt2 == 1) {
		/*
		 * Don't use MSI-X, if the effective RX ring count is 1.
		 * Since if the effective RX ring count is 1, the TX ring
		 * count will be 1.  This RX ring and the TX ring must be
		 * bundled into one MSI-X vector, so the hot path will be
		 * exact same as using MSI.  Besides, the first RX ring
		 * must be fully populated, which only accepts packets whose
		 * RSS hash can't calculated, e.g. ARP packets; waste of
		 * resource at least.
		 */
		sc->rx_ring_cnt = 1;
	} else {
		/*
		 * One extra RX ring is allocated, since the first RX ring
		 * could not be used for RSS hashed packets whose masked
		 * hash is 0.  The first RX ring is only used for packets
		 * whose RSS hash could not be calculated, e.g. ARP packets.
		 * This extra RX ring will be used for packets whose masked
		 * hash is 0.  The effective RX ring count involved in RSS
		 * is still sc->rx_ring_cnt2.
		 */
		sc->rx_ring_cnt = sc->rx_ring_cnt2 + 1;
	}
}

static void
bce_free_msix(struct bce_softc *sc, boolean_t setup)
{
	int i;

	KKASSERT(sc->rx_ring_cnt > 1);

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct bce_msix_data *msix = &sc->bce_msix[i];

		if (msix->msix_res != NULL) {
			bus_release_resource(sc->bce_dev, SYS_RES_IRQ,
			    msix->msix_rid, msix->msix_res);
		}
		if (msix->msix_rid >= 0)
			pci_release_msix_vector(sc->bce_dev, msix->msix_rid);
	}
	if (setup)
		pci_teardown_msix(sc->bce_dev);
}

static void
bce_free_intr(struct bce_softc *sc)
{
	if (sc->bce_irq_type != PCI_INTR_TYPE_MSIX) {
		if (sc->bce_res_irq != NULL) {
			bus_release_resource(sc->bce_dev, SYS_RES_IRQ,
			    sc->bce_irq_rid, sc->bce_res_irq);
		}
		if (sc->bce_irq_type == PCI_INTR_TYPE_MSI)
			pci_release_msi(sc->bce_dev);
	} else {
		bce_free_msix(sc, TRUE);
	}
}

static void
bce_setup_msix_table(struct bce_softc *sc)
{
	REG_WR(sc, BCE_PCI_GRC_WINDOW_ADDR, BCE_PCI_GRC_WINDOW_ADDR_SEP_WIN);
	REG_WR(sc, BCE_PCI_GRC_WINDOW2_ADDR, BCE_MSIX_TABLE_ADDR);
	REG_WR(sc, BCE_PCI_GRC_WINDOW3_ADDR, BCE_MSIX_PBA_ADDR);
}

static int
bce_setup_intr(struct bce_softc *sc)
{
	void (*irq_handle)(void *);
	int error;

	if (sc->bce_irq_type == PCI_INTR_TYPE_MSIX)
		return bce_setup_msix(sc);

	if (sc->bce_irq_type == PCI_INTR_TYPE_LEGACY) {
		irq_handle = bce_intr_legacy;
	} else if (sc->bce_irq_type == PCI_INTR_TYPE_MSI) {
		if (BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5709 ||
		    BCE_CHIP_NUM(sc) == BCE_CHIP_NUM_5716) {
			irq_handle = bce_intr_msi_oneshot;
			sc->bce_flags |= BCE_ONESHOT_MSI_FLAG;
		} else {
			irq_handle = bce_intr_msi;
			sc->bce_flags |= BCE_CHECK_MSI_FLAG;
		}
	} else {
		panic("%s: unsupported intr type %d",
		    device_get_nameunit(sc->bce_dev), sc->bce_irq_type);
	}

	error = bus_setup_intr(sc->bce_dev, sc->bce_res_irq, INTR_MPSAFE,
	    irq_handle, sc, &sc->bce_intrhand, &sc->main_serialize);
	if (error != 0) {
		device_printf(sc->bce_dev, "Failed to setup IRQ!\n");
		return error;
	}

	return 0;
}

static void
bce_teardown_intr(struct bce_softc *sc)
{
	if (sc->bce_irq_type != PCI_INTR_TYPE_MSIX)
		bus_teardown_intr(sc->bce_dev, sc->bce_res_irq, sc->bce_intrhand);
	else
		bce_teardown_msix(sc, sc->rx_ring_cnt);
}

static int
bce_setup_msix(struct bce_softc *sc)
{
	int i;

	for (i = 0; i < sc->rx_ring_cnt; ++i) {
		struct bce_msix_data *msix = &sc->bce_msix[i];
		int error;

		error = bus_setup_intr_descr(sc->bce_dev, msix->msix_res,
		    INTR_MPSAFE, msix->msix_func, msix->msix_arg,
		    &msix->msix_handle, msix->msix_serialize, msix->msix_desc);
		if (error) {
			device_printf(sc->bce_dev, "could not set up %s "
			    "interrupt handler.\n", msix->msix_desc);
			bce_teardown_msix(sc, i);
			return error;
		}
	}
	return 0;
}

static void
bce_teardown_msix(struct bce_softc *sc, int msix_cnt)
{
	int i;

	for (i = 0; i < msix_cnt; ++i) {
		struct bce_msix_data *msix = &sc->bce_msix[i];

		bus_teardown_intr(sc->bce_dev, msix->msix_res,
		    msix->msix_handle);
	}
}

static void
bce_init_rss(struct bce_softc *sc)
{
	uint8_t key[BCE_RLUP_RSS_KEY_CNT * BCE_RLUP_RSS_KEY_SIZE];
	uint32_t tbl = 0;
	int i;

	KKASSERT(sc->rx_ring_cnt > 2);

	/*
	 * Configure RSS keys
	 */
	toeplitz_get_key(key, sizeof(key));
	for (i = 0; i < BCE_RLUP_RSS_KEY_CNT; ++i) {
		uint32_t rss_key;

		rss_key = BCE_RLUP_RSS_KEYVAL(key, i);
		BCE_RSS_DPRINTF(sc, 1, "rss_key%d 0x%08x\n", i, rss_key);

		REG_WR(sc, BCE_RLUP_RSS_KEY(i), rss_key);
	}

	/*
	 * Configure the redirect table
	 *
	 * NOTE:
	 * - The "queue ID" in redirect table is the software RX ring's
	 *   index _minus_ one.
	 * - The last RX ring, whose "queue ID" is (sc->rx_ring_cnt - 2)
	 *   will be used for packets whose masked hash is 0.
	 *   (see also: comment in bce_setup_ring_cnt())
	 */
	if_ringmap_rdrtable(sc->rx_rmap, sc->rdr_table,
	    BCE_RXP_SCRATCH_RSS_TBL_MAX_ENTRIES);
	for (i = 0; i < BCE_RXP_SCRATCH_RSS_TBL_MAX_ENTRIES; i++) {
		int shift = (i % 8) << 2, qid;

		qid = sc->rdr_table[i];
		KKASSERT(qid >= 0 && qid < sc->rx_ring_cnt2);
		if (qid > 0)
			--qid;
		else
			qid = sc->rx_ring_cnt - 2;
		KKASSERT(qid < (sc->rx_ring_cnt - 1));

		tbl |= qid << shift;
		if (i % 8 == 7) {
			BCE_RSS_DPRINTF(sc, 1, "tbl 0x%08x\n", tbl);
			REG_WR(sc, BCE_RLUP_RSS_DATA, tbl);
			REG_WR(sc, BCE_RLUP_RSS_COMMAND, (i >> 3) |
			    BCE_RLUP_RSS_COMMAND_RSS_WRITE_MASK |
			    BCE_RLUP_RSS_COMMAND_WRITE |
			    BCE_RLUP_RSS_COMMAND_HASH_MASK);
			tbl = 0;
		}
	}
	REG_WR(sc, BCE_RLUP_RSS_CONFIG,
	    BCE_RLUP_RSS_CONFIG_IPV4_RSS_TYPE_ALL_XI);
}

static void
bce_npoll_coal_change(struct bce_softc *sc)
{
	uint32_t old_rx_cons, old_tx_cons;

	old_rx_cons = sc->bce_rx_quick_cons_trip_int;
	old_tx_cons = sc->bce_tx_quick_cons_trip_int;
	sc->bce_rx_quick_cons_trip_int = 1;
	sc->bce_tx_quick_cons_trip_int = 1;

	sc->bce_coalchg_mask |= BCE_COALMASK_TX_BDS_INT |
	    BCE_COALMASK_RX_BDS_INT;
	bce_coal_change(sc);

	sc->bce_rx_quick_cons_trip_int = old_rx_cons;
	sc->bce_tx_quick_cons_trip_int = old_tx_cons;
}

static struct pktinfo *
bce_rss_pktinfo(struct pktinfo *pi, uint32_t status,
    const struct l2_fhdr *l2fhdr)
{
	/* Check for an IP datagram. */
	if ((status & L2_FHDR_STATUS_IP_DATAGRAM) == 0)
		return NULL;

	/* Check if the IP checksum is valid. */
	if (l2fhdr->l2_fhdr_ip_xsum != 0xffff)
		return NULL;

	/* Check for a valid TCP/UDP frame. */
	if (status & L2_FHDR_STATUS_TCP_SEGMENT) {
		if (status & L2_FHDR_ERRORS_TCP_XSUM)
			return NULL;
		if (l2fhdr->l2_fhdr_tcp_udp_xsum != 0xffff)
			return NULL;
		pi->pi_l3proto = IPPROTO_TCP;
	} else if (status & L2_FHDR_STATUS_UDP_DATAGRAM) {
		if (status & L2_FHDR_ERRORS_UDP_XSUM)
			return NULL;
		if (l2fhdr->l2_fhdr_tcp_udp_xsum != 0xffff)
			return NULL;
		pi->pi_l3proto = IPPROTO_UDP;
	} else {
		return NULL;
	}
	pi->pi_netisr = NETISR_IP;
	pi->pi_flags = 0;

	return pi;
}
