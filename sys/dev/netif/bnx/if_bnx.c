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
#include "opt_polling.h"

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
#include <dev/netif/bnx/if_bnxvar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define BNX_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

#define BNX_INTR_CKINTVL	((10 * hz) / 1000)	/* 10ms */

static const struct bnx_type {
	uint16_t		bnx_vid;
	uint16_t		bnx_did;
	char			*bnx_name;
} bnx_devs[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5717,
		"Broadcom BCM5717 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5718,
		"Broadcom BCM5718 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5719,
		"Broadcom BCM5719 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM5720_ALT,
		"Broadcom BCM5720 Gigabit Ethernet" },

	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57761,
		"Broadcom BCM57761 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57762,
		"Broadcom BCM57762 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57765,
		"Broadcom BCM57765 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57766,
		"Broadcom BCM57766 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57781,
		"Broadcom BCM57781 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57782,
		"Broadcom BCM57782 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57785,
		"Broadcom BCM57785 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57786,
		"Broadcom BCM57786 Gigabit Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57791,
		"Broadcom BCM57791 Fast Ethernet" },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM57795,
		"Broadcom BCM57795 Fast Ethernet" },

	{ 0, 0, NULL }
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

#ifdef DEVICE_POLLING
static void	bnx_poll(struct ifnet *ifp, enum poll_cmd cmd, int count);
#endif
static void	bnx_intr_legacy(void *);
static void	bnx_msi(void *);
static void	bnx_msi_oneshot(void *);
static void	bnx_intr(struct bnx_softc *);
static void	bnx_enable_intr(struct bnx_softc *);
static void	bnx_disable_intr(struct bnx_softc *);
static void	bnx_txeof(struct bnx_softc *, uint16_t);
static void	bnx_rxeof(struct bnx_softc *, uint16_t);

static void	bnx_start(struct ifnet *);
static int	bnx_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	bnx_init(void *);
static void	bnx_stop(struct bnx_softc *);
static void	bnx_watchdog(struct ifnet *);
static int	bnx_ifmedia_upd(struct ifnet *);
static void	bnx_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	bnx_tick(void *);

static int	bnx_alloc_jumbo_mem(struct bnx_softc *);
static void	bnx_free_jumbo_mem(struct bnx_softc *);
static struct bnx_jslot
		*bnx_jalloc(struct bnx_softc *);
static void	bnx_jfree(void *);
static void	bnx_jref(void *);
static int	bnx_newbuf_std(struct bnx_softc *, int, int);
static int	bnx_newbuf_jumbo(struct bnx_softc *, int, int);
static void	bnx_setup_rxdesc_std(struct bnx_softc *, int);
static void	bnx_setup_rxdesc_jumbo(struct bnx_softc *, int);
static int	bnx_init_rx_ring_std(struct bnx_softc *);
static void	bnx_free_rx_ring_std(struct bnx_softc *);
static int	bnx_init_rx_ring_jumbo(struct bnx_softc *);
static void	bnx_free_rx_ring_jumbo(struct bnx_softc *);
static void	bnx_free_tx_ring(struct bnx_softc *);
static int	bnx_init_tx_ring(struct bnx_softc *);
static int	bnx_dma_alloc(struct bnx_softc *);
static void	bnx_dma_free(struct bnx_softc *);
static int	bnx_dma_block_alloc(struct bnx_softc *, bus_size_t,
		    bus_dma_tag_t *, bus_dmamap_t *, void **, bus_addr_t *);
static void	bnx_dma_block_free(bus_dma_tag_t, bus_dmamap_t, void *);
static struct mbuf *
		bnx_defrag_shortdma(struct mbuf *);
static int	bnx_encap(struct bnx_softc *, struct mbuf **, uint32_t *);
static int	bnx_setup_tso(struct bnx_softc *, struct mbuf **,
		    uint16_t *, uint16_t *);

static void	bnx_reset(struct bnx_softc *);
static int	bnx_chipinit(struct bnx_softc *);
static int	bnx_blockinit(struct bnx_softc *);
static void	bnx_stop_block(struct bnx_softc *, bus_size_t, uint32_t);
static void	bnx_enable_msi(struct bnx_softc *sc);
static void	bnx_setmulti(struct bnx_softc *);
static void	bnx_setpromisc(struct bnx_softc *);
static void	bnx_stats_update_regs(struct bnx_softc *);
static uint32_t	bnx_dma_swap_options(struct bnx_softc *);

static uint32_t	bnx_readmem_ind(struct bnx_softc *, uint32_t);
static void	bnx_writemem_ind(struct bnx_softc *, uint32_t, uint32_t);
#ifdef notdef
static uint32_t	bnx_readreg_ind(struct bnx_softc *, uint32_t);
#endif
static void	bnx_writereg_ind(struct bnx_softc *, uint32_t, uint32_t);
static void	bnx_writemem_direct(struct bnx_softc *, uint32_t, uint32_t);
static void	bnx_writembx(struct bnx_softc *, int, int);
static uint8_t	bnx_nvram_getbyte(struct bnx_softc *, int, uint8_t *);
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
static int	bnx_sysctl_rx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_ticks(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_rx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_tx_coal_bds_int(SYSCTL_HANDLER_ARGS);
static int	bnx_sysctl_coal_chg(SYSCTL_HANDLER_ARGS, uint32_t *,
		    int, int, uint32_t);

static int	bnx_msi_enable = 1;
TUNABLE_INT("hw.bnx.msi.enable", &bnx_msi_enable);

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

	{ 0, 0 }
};

static DEFINE_CLASS_0(bnx, bnx_driver, bnx_methods, sizeof(struct bnx_softc));
static devclass_t bnx_devclass;

DECLARE_DUMMY_MODULE(if_bnx);
DRIVER_MODULE(if_bnx, pci, bnx_driver, bnx_devclass, NULL, NULL);
DRIVER_MODULE(miibus, bnx, miibus_driver, miibus_devclass, NULL, NULL);

static uint32_t
bnx_readmem_ind(struct bnx_softc *sc, uint32_t off)
{
	device_t dev = sc->bnx_dev;
	uint32_t val;

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return 0;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	val = pci_read_config(dev, BGE_PCI_MEMWIN_DATA, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
	return (val);
}

static void
bnx_writemem_ind(struct bnx_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bnx_dev;

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_DATA, val, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
}

#ifdef notdef
static uint32_t
bnx_readreg_ind(struct bnx_softc *sc, uin32_t off)
{
	device_t dev = sc->bnx_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	return(pci_read_config(dev, BGE_PCI_REG_DATA, 4));
}
#endif

static void
bnx_writereg_ind(struct bnx_softc *sc, uint32_t off, uint32_t val)
{
	device_t dev = sc->bnx_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_REG_DATA, val, 4);
}

static void
bnx_writemem_direct(struct bnx_softc *sc, uint32_t off, uint32_t val)
{
	CSR_WRITE_4(sc, off, val);
}

static void
bnx_writembx(struct bnx_softc *sc, int off, int val)
{
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906)
		off += BGE_LPMBX_IRQ0_HI - BGE_MBX_IRQ0_HI;

	CSR_WRITE_4(sc, off, val);
}

static uint8_t
bnx_nvram_getbyte(struct bnx_softc *sc, int addr, uint8_t *dest)
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
	for (i = 0; i < BNX_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_NVRAM_CMD) & BGE_NVRAMCMD_DONE) {
			DELAY(10);
			break;
		}
	}

	if (i == BNX_TIMEOUT * 10) {
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
bnx_read_nvram(struct bnx_softc *sc, caddr_t dest, int off, int cnt)
{
	int err = 0, i;
	uint8_t byte = 0;

	if (sc->bnx_asicrev != BGE_ASICREV_BCM5906)
		return (1);

	for (i = 0; i < cnt; i++) {
		err = bnx_nvram_getbyte(sc, off + i, &byte);
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

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906 &&
	    (reg == BRGPHY_MII_1000CTL || reg == BRGPHY_MII_AUXCTL))
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

	return 0;
}

static void
bnx_miibus_statchg(device_t dev)
{
	struct bnx_softc *sc;
	struct mii_data *mii;

	sc = device_get_softc(dev);
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
			if (sc->bnx_asicrev != BGE_ASICREV_BCM5906)
				sc->bnx_link = 1;
			else
				sc->bnx_link = 0;
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

	BNX_CLRBIT(sc, BGE_MAC_MODE, BGE_MACMODE_PORTMODE);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX) {
		BNX_SETBIT(sc, BGE_MAC_MODE, BGE_PORTMODE_GMII);
	} else {
		BNX_SETBIT(sc, BGE_MAC_MODE, BGE_PORTMODE_MII);
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		BNX_CLRBIT(sc, BGE_MAC_MODE, BGE_MACMODE_HALF_DUPLEX);
	} else {
		BNX_SETBIT(sc, BGE_MAC_MODE, BGE_MACMODE_HALF_DUPLEX);
	}
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
bnx_newbuf_std(struct bnx_softc *sc, int i, int init)
{
	struct mbuf *m_new = NULL;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	int error, nsegs;

	m_new = m_getcl(init ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return ENOBUFS;
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	m_adj(m_new, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_segment(sc->bnx_cdata.bnx_rx_mtag,
			sc->bnx_cdata.bnx_rx_tmpmap, m_new,
			&seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_new);
		return error;
	}

	if (!init) {
		bus_dmamap_sync(sc->bnx_cdata.bnx_rx_mtag,
				sc->bnx_cdata.bnx_rx_std_dmamap[i],
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->bnx_cdata.bnx_rx_mtag,
			sc->bnx_cdata.bnx_rx_std_dmamap[i]);
	}

	map = sc->bnx_cdata.bnx_rx_tmpmap;
	sc->bnx_cdata.bnx_rx_tmpmap = sc->bnx_cdata.bnx_rx_std_dmamap[i];
	sc->bnx_cdata.bnx_rx_std_dmamap[i] = map;

	sc->bnx_cdata.bnx_rx_std_chain[i].bnx_mbuf = m_new;
	sc->bnx_cdata.bnx_rx_std_chain[i].bnx_paddr = seg.ds_addr;

	bnx_setup_rxdesc_std(sc, i);
	return 0;
}

static void
bnx_setup_rxdesc_std(struct bnx_softc *sc, int i)
{
	struct bnx_rxchain *rc;
	struct bge_rx_bd *r;

	rc = &sc->bnx_cdata.bnx_rx_std_chain[i];
	r = &sc->bnx_ldata.bnx_rx_std_ring[i];

	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(rc->bnx_paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(rc->bnx_paddr);
	r->bge_len = rc->bnx_mbuf->m_len;
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
	MGETHDR(m_new, init ? MB_WAIT : MB_DONTWAIT, MT_DATA);
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
	sc->bnx_cdata.bnx_rx_jumbo_chain[i].bnx_mbuf = m_new;
	sc->bnx_cdata.bnx_rx_jumbo_chain[i].bnx_paddr = paddr;

	/* Set up the descriptor. */
	bnx_setup_rxdesc_jumbo(sc, i);
	return 0;
}

static void
bnx_setup_rxdesc_jumbo(struct bnx_softc *sc, int i)
{
	struct bge_rx_bd *r;
	struct bnx_rxchain *rc;

	r = &sc->bnx_ldata.bnx_rx_jumbo_ring[i];
	rc = &sc->bnx_cdata.bnx_rx_jumbo_chain[i];

	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(rc->bnx_paddr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(rc->bnx_paddr);
	r->bge_len = rc->bnx_mbuf->m_len;
	r->bge_idx = i;
	r->bge_flags = BGE_RXBDFLAG_END|BGE_RXBDFLAG_JUMBO_RING;
}

static int
bnx_init_rx_ring_std(struct bnx_softc *sc)
{
	int i, error;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		error = bnx_newbuf_std(sc, i, 1);
		if (error)
			return error;
	};

	sc->bnx_std = BGE_STD_RX_RING_CNT - 1;
	bnx_writembx(sc, BGE_MBX_RX_STD_PROD_LO, sc->bnx_std);

	return(0);
}

static void
bnx_free_rx_ring_std(struct bnx_softc *sc)
{
	int i;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		struct bnx_rxchain *rc = &sc->bnx_cdata.bnx_rx_std_chain[i];

		if (rc->bnx_mbuf != NULL) {
			bus_dmamap_unload(sc->bnx_cdata.bnx_rx_mtag,
					  sc->bnx_cdata.bnx_rx_std_dmamap[i]);
			m_freem(rc->bnx_mbuf);
			rc->bnx_mbuf = NULL;
		}
		bzero(&sc->bnx_ldata.bnx_rx_std_ring[i],
		    sizeof(struct bge_rx_bd));
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
	};

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
		struct bnx_rxchain *rc = &sc->bnx_cdata.bnx_rx_jumbo_chain[i];

		if (rc->bnx_mbuf != NULL) {
			m_freem(rc->bnx_mbuf);
			rc->bnx_mbuf = NULL;
		}
		bzero(&sc->bnx_ldata.bnx_rx_jumbo_ring[i],
		    sizeof(struct bge_rx_bd));
	}
}

static void
bnx_free_tx_ring(struct bnx_softc *sc)
{
	int i;

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		if (sc->bnx_cdata.bnx_tx_chain[i] != NULL) {
			bus_dmamap_unload(sc->bnx_cdata.bnx_tx_mtag,
					  sc->bnx_cdata.bnx_tx_dmamap[i]);
			m_freem(sc->bnx_cdata.bnx_tx_chain[i]);
			sc->bnx_cdata.bnx_tx_chain[i] = NULL;
		}
		bzero(&sc->bnx_ldata.bnx_tx_ring[i],
		    sizeof(struct bge_tx_bd));
	}
}

static int
bnx_init_tx_ring(struct bnx_softc *sc)
{
	sc->bnx_txcnt = 0;
	sc->bnx_tx_saved_considx = 0;
	sc->bnx_tx_prodidx = 0;

	/* Initialize transmit producer index for host-memory send ring. */
	bnx_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, sc->bnx_tx_prodidx);
	bnx_writembx(sc, BGE_MBX_TX_NIC_PROD0_LO, 0);

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

	/* Clear the MAC control register */
	CSR_WRITE_4(sc, BGE_MAC_MODE, 0);

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
	mode_ctl = bnx_dma_swap_options(sc) | BGE_MODECTL_MAC_ATTN_INTR |
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

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906) {
		DELAY(40);	/* XXX */

		/* Put PHY into ready state */
		BNX_CLRBIT(sc, BGE_MISC_CFG, BGE_MISCCFG_EPHY_IDDQ);
		CSR_READ_4(sc, BGE_MISC_CFG); /* Flush */
		DELAY(40);
	}

	return(0);
}

static int
bnx_blockinit(struct bnx_softc *sc)
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
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5906) {
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
	 *   buffer up to the stack.  Many versions of the controller
	 *   support multiple RR rings.
	 *
	 * Send Ring
	 * - This ring is used for outgoing frames.  Many versions of
	 *   the controller support multiple send rings.
	 */

	/* Initialize the standard receive producer ring control block. */
	rcb = &sc->bnx_ldata.bnx_info.bnx_std_rx_rcb;
	rcb->bge_hostaddr.bge_addr_lo =
	    BGE_ADDR_LO(sc->bnx_ldata.bnx_rx_std_ring_paddr);
	rcb->bge_hostaddr.bge_addr_hi =
	    BGE_ADDR_HI(sc->bnx_ldata.bnx_rx_std_ring_paddr);
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

	/* Choose de-pipeline mode for BCM5906 A0, A1 and A2. */
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906 &&
	    (sc->bnx_chipid == BGE_CHIPID_BCM5906_A0 ||
	     sc->bnx_chipid == BGE_CHIPID_BCM5906_A1 ||
	     sc->bnx_chipid == BGE_CHIPID_BCM5906_A2)) {
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
	else if (BNX_IS_57765_FAMILY(sc))
		limit = 2;
	else
		limit = 1;
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED));
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/* Configure send ring RCB 0 (we use only the first ring) */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bnx_ldata.bnx_tx_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	if (BNX_IS_5717_PLUS(sc)) {
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, BGE_SEND_RING_5717);
	} else {
		RCB_WRITE_4(sc, vrcb, bge_nicaddr,
		    BGE_NIC_TXRING_ADDR(0, BGE_TX_RING_CNT));
	}
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(BGE_TX_RING_CNT, 0));

	/*
	 * Disable all receive return rings by setting the
	 * 'ring disabled' bit in the flags field of all the receive
	 * return ring control blocks, located in NIC memory.
	 */
	if (BNX_IS_5717_PLUS(sc)) {
		/* Should be 17, use 16 until we get an SRAM map. */
		limit = 16;
	} else if (BNX_IS_57765_FAMILY(sc)) {
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
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		bnx_writembx(sc, BGE_MBX_RX_CONS0_LO +
		    (i * (sizeof(uint64_t))), 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Set up receive return ring 0.  Note that the NIC address
	 * for RX return rings is 0x0.  The return rings live entirely
	 * within the host, so the nicaddr field in the RCB isn't used.
	 */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bnx_ldata.bnx_rx_return_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(sc->bnx_return_ring_cnt, 0));

	/* Set random backoff seed for TX */
	CSR_WRITE_4(sc, BGE_TX_RANDOM_BACKOFF,
	    sc->arpcom.ac_enaddr[0] + sc->arpcom.ac_enaddr[1] +
	    sc->arpcom.ac_enaddr[2] + sc->arpcom.ac_enaddr[3] +
	    sc->arpcom.ac_enaddr[4] + sc->arpcom.ac_enaddr[5] +
	    BGE_TX_BACKOFF_SEED_MASK);

	/* Set inter-packet gap */
	val = 0x2620;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
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
	CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS, sc->bnx_rx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS, sc->bnx_tx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, sc->bnx_rx_coal_bds);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, sc->bnx_tx_coal_bds);
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT, sc->bnx_rx_coal_bds_int);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT, sc->bnx_tx_coal_bds_int);

	/* Set up address of status block */
	bzero(sc->bnx_ldata.bnx_status_block, BGE_STATUS_BLK_SZ);
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_HI,
	    BGE_ADDR_HI(sc->bnx_ldata.bnx_status_block_paddr));
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_LO,
	    BGE_ADDR_LO(sc->bnx_ldata.bnx_status_block_paddr));

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

	/* Turn on DMA, clear stats */
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);

	/* Set misc. local control, enable interrupts on attentions */
	CSR_WRITE_4(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

#ifdef notdef
	/* Assert GPIO pins for PHY reset */
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUT0|
	    BGE_MLC_MISCIO_OUT1|BGE_MLC_MISCIO_OUT2);
	BNX_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUTEN0|
	    BGE_MLC_MISCIO_OUTEN1|BGE_MLC_MISCIO_OUTEN2);
#endif

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
		uint32_t dmactl;

		dmactl = CSR_READ_4(sc, BGE_RDMA_RSRVCTRL);
		/*
		 * Adjust tx margin to prevent TX data corruption and
		 * fix internal FIFO overflow.
		 */
		if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 ||
		    sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
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
		CSR_WRITE_4(sc, BGE_RDMA_RSRVCTRL,
		    dmactl | BGE_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
	}

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719) {
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_4K |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		/*
		 * Allow 4KB burst length reads for non-LSO frames.
		 * Enable 512B burst length reads for buffer descriptors.
		 */
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
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
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		val |= CSR_READ_4(sc, BGE_RDMA_MODE) &
		    BGE_RDMAMODE_H2BNC_VLAN_DET;
		/*
		 * Allow multiple outstanding read requests from
		 * non-LSO read DMA engine.
		 */
		val &= ~BGE_RDMAMODE_MULT_DMA_RD_DIS;
	}
	if (sc->bnx_flags & BNX_FLAG_TSO)
		val |= BGE_RDMAMODE_TSO4_ENABLE;
	val |= BGE_RDMAMODE_FIFO_LONG_BURST;
	CSR_WRITE_4(sc, BGE_RDMA_MODE, val);
	DELAY(40);

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
	uint32_t hwcfg = 0, misccfg;
	int error = 0, rid, capmask;
	uint8_t ether_addr[ETHER_ADDR_LEN];
	uint16_t product, vendor;
	driver_intr_t *intr_func;
	uintptr_t mii_priv = 0;
	u_int intr_flags;
#ifdef BNX_TSO_DEBUG
	char desc[32];
	int i;
#endif

	sc = device_get_softc(dev);
	sc->bnx_dev = dev;
	callout_init_mp(&sc->bnx_stat_timer);
	callout_init_mp(&sc->bnx_intr_timer);
	lwkt_serialize_init(&sc->bnx_jslot_serializer);

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
		case PCI_PRODUCT_BROADCOM_BCM5718:
		case PCI_PRODUCT_BROADCOM_BCM5719:
		case PCI_PRODUCT_BROADCOM_BCM5720_ALT:
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
	sc->bnx_asicrev = BGE_ASICREV(sc->bnx_chipid);
	sc->bnx_chiprev = BGE_CHIPREV(sc->bnx_chipid);

	switch (sc->bnx_asicrev) {
	case BGE_ASICREV_BCM5717:
	case BGE_ASICREV_BCM5719:
	case BGE_ASICREV_BCM5720:
		sc->bnx_flags |= BNX_FLAG_5717_PLUS | BNX_FLAG_57765_PLUS;
		break;

	case BGE_ASICREV_BCM57765:
	case BGE_ASICREV_BCM57766:
		sc->bnx_flags |= BNX_FLAG_57765_FAMILY | BNX_FLAG_57765_PLUS;
		break;
	}
	sc->bnx_flags |= BNX_FLAG_SHORTDMA;

	sc->bnx_flags |= BNX_FLAG_TSO;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5719 &&
	    sc->bnx_chipid == BGE_CHIPID_BCM5719_A0)
		sc->bnx_flags &= ~BNX_FLAG_TSO;

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5717 ||
	    BNX_IS_57765_FAMILY(sc)) {
		/*
		 * All BCM57785 and BCM5718 families chips have a bug that
		 * under certain situation interrupt will not be enabled
		 * even if status tag is written to BGE_MBX_IRQ0_LO mailbox.
		 *
		 * While BCM5719 and BCM5720 have a hardware workaround
		 * which could fix the above bug.
		 * See the comment near BGE_PCIDMARWCTL_TAGGED_STATUS_WA in
		 * bnx_chipinit().
		 *
		 * For the rest of the chips in these two families, we will
		 * have to poll the status block at high rate (10ms currently)
		 * to check whether the interrupt is hosed or not.
		 * See bnx_intr_check() for details.
		 */
		sc->bnx_flags |= BNX_FLAG_STATUSTAG_BUG;
	}

	misccfg = CSR_READ_4(sc, BGE_MISC_CFG) & BGE_MISCCFG_BOARD_ID_MASK;

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

	/*
	 * Allocate interrupt
	 */
	sc->bnx_irq_type = pci_alloc_1intr(dev, bnx_msi_enable, &sc->bnx_irq_rid,
	    &intr_flags);

	sc->bnx_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->bnx_irq_rid,
	    intr_flags);
	if (sc->bnx_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	if (sc->bnx_irq_type == PCI_INTR_TYPE_MSI) {
		sc->bnx_flags |= BNX_FLAG_ONESHOT_MSI;
		bnx_enable_msi(sc);
	}

	/* Initialize if_name earlier, so if_printf could be used */
	ifp = &sc->arpcom.ac_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* Try to reset the chip. */
	bnx_reset(sc);

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

	if (BNX_IS_57765_PLUS(sc)) {
		sc->bnx_return_ring_cnt = BGE_RETURN_RING_CNT;
	} else {
		/* 5705/5750 limits RX return ring to 512 entries. */
		sc->bnx_return_ring_cnt = BGE_RETURN_RING_CNT_5705;
	}

	error = bnx_dma_alloc(sc);
	if (error)
		goto fail;

	/* Set default tuneable values. */
	sc->bnx_rx_coal_ticks = BNX_RX_COAL_TICKS_DEF;
	sc->bnx_tx_coal_ticks = BNX_TX_COAL_TICKS_DEF;
	sc->bnx_rx_coal_bds = BNX_RX_COAL_BDS_DEF;
	sc->bnx_tx_coal_bds = BNX_TX_COAL_BDS_DEF;
	sc->bnx_rx_coal_bds_int = BNX_RX_COAL_BDS_DEF;
	sc->bnx_tx_coal_bds_int = BNX_TX_COAL_BDS_DEF;

	/* Set up ifnet structure */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bnx_ioctl;
	ifp->if_start = bnx_start;
#ifdef DEVICE_POLLING
	ifp->if_poll = bnx_poll;
#endif
	ifp->if_watchdog = bnx_watchdog;
	ifp->if_init = bnx_init;
	ifp->if_mtu = ETHERMTU;
	ifp->if_capabilities = IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU;
	ifq_set_maxlen(&ifp->if_snd, BGE_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities |= IFCAP_HWCSUM;
	ifp->if_hwassist = BNX_CSUM_FEATURES;
	if (sc->bnx_flags & BNX_FLAG_TSO) {
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
	if (bnx_readmem_ind(sc, BGE_SOFTWARE_GENCOMM_SIG) == BGE_MAGIC_NUMBER) {
		hwcfg = bnx_readmem_ind(sc, BGE_SOFTWARE_GENCOMM_NICCFG);
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
	 * BCM5704  |   1   |   X   |   1   |   X   |
	 * BCM5717  |   1   |   8   |   2   |   9   |
	 * BCM5719  |   1   |   8   |   2   |   9   |
	 * BCM5720  |   1   |   8   |   2   |   9   |
	 *
	 * Other addresses may respond but they are not
	 * IEEE compliant PHYs and should be ignored.
	 */
	if (BNX_IS_5717_PLUS(sc)) {
		int f;

		f = pci_get_function(dev);
		if (sc->bnx_chipid == BGE_CHIPID_BCM5717_A0) {
			if (CSR_READ_4(sc, BGE_SGDIG_STS) &
			    BGE_SGDIGSTS_IS_SERDES)
				sc->bnx_phyno = f + 8;
			else
				sc->bnx_phyno = f + 1;
		} else {
			if (CSR_READ_4(sc, BGE_CPMU_PHY_STRAP) &
			    BGE_CPMU_PHY_STRAP_IS_SERDES)
				sc->bnx_phyno = f + 8;
			else
				sc->bnx_phyno = f + 1;
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

	/*
	 * Create sysctl nodes.
	 */
	sysctl_ctx_init(&sc->bnx_sysctl_ctx);
	sc->bnx_sysctl_tree = SYSCTL_ADD_NODE(&sc->bnx_sysctl_ctx,
					      SYSCTL_STATIC_CHILDREN(_hw),
					      OID_AUTO,
					      device_get_nameunit(dev),
					      CTLFLAG_RD, 0, "");
	if (sc->bnx_sysctl_tree == NULL) {
		device_printf(dev, "can't add sysctl node\n");
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bnx_sysctl_tree),
			OID_AUTO, "rx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_rx_coal_ticks, "I",
			"Receive coalescing ticks (usec).");
	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bnx_sysctl_tree),
			OID_AUTO, "tx_coal_ticks",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_tx_coal_ticks, "I",
			"Transmit coalescing ticks (usec).");
	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bnx_sysctl_tree),
			OID_AUTO, "rx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_rx_coal_bds, "I",
			"Receive max coalesced BD count.");
	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
			SYSCTL_CHILDREN(sc->bnx_sysctl_tree),
			OID_AUTO, "tx_coal_bds",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, bnx_sysctl_tx_coal_bds, "I",
			"Transmit max coalesced BD count.");
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
	SYSCTL_ADD_INT(&sc->bnx_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->bnx_sysctl_tree), OID_AUTO,
	    "force_defrag", CTLFLAG_RW, &sc->bnx_force_defrag, 0,
	    "Force defragment on TX path");

	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->bnx_sysctl_tree), OID_AUTO,
	    "rx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_rx_coal_bds_int, "I",
	    "Receive max coalesced BD count during interrupt.");
	SYSCTL_ADD_PROC(&sc->bnx_sysctl_ctx,
	    SYSCTL_CHILDREN(sc->bnx_sysctl_tree), OID_AUTO,
	    "tx_coal_bds_int", CTLTYPE_INT | CTLFLAG_RW,
	    sc, 0, bnx_sysctl_tx_coal_bds_int, "I",
	    "Transmit max coalesced BD count during interrupt.");

#ifdef BNX_TSO_DEBUG
	for (i = 0; i < BNX_TSO_NSTATS; ++i) {
		ksnprintf(desc, sizeof(desc), "tso%d", i + 1);
		SYSCTL_ADD_ULONG(&sc->bnx_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->bnx_sysctl_tree), OID_AUTO,
		    desc, CTLFLAG_RW, &sc->bnx_tsosegs[i], "");
	}
#endif

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, ether_addr, NULL);

	if (sc->bnx_irq_type == PCI_INTR_TYPE_MSI) {
		if (sc->bnx_flags & BNX_FLAG_ONESHOT_MSI) {
			intr_func = bnx_msi_oneshot;
			if (bootverbose)
				device_printf(dev, "oneshot MSI\n");
		} else {
			intr_func = bnx_msi;
		}
	} else {
		intr_func = bnx_intr_legacy;
	}
	error = bus_setup_intr(dev, sc->bnx_irq, INTR_MPSAFE, intr_func, sc,
	    &sc->bnx_intrhand, ifp->if_serializer);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

	ifp->if_cpuid = rman_get_cpuid(sc->bnx_irq);
	KKASSERT(ifp->if_cpuid >= 0 && ifp->if_cpuid < ncpus);

	sc->bnx_stat_cpuid = ifp->if_cpuid;
	sc->bnx_intr_cpuid = ifp->if_cpuid;

	return(0);
fail:
	bnx_detach(dev);
	return(error);
}

static int
bnx_detach(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);

	if (device_is_attached(dev)) {
		struct ifnet *ifp = &sc->arpcom.ac_if;

		lwkt_serialize_enter(ifp->if_serializer);
		bnx_stop(sc);
		bnx_reset(sc);
		bus_teardown_intr(dev, sc->bnx_irq, sc->bnx_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->bnx_flags & BNX_FLAG_TBI)
		ifmedia_removeall(&sc->bnx_ifmedia);
	if (sc->bnx_miibus)
		device_delete_child(dev, sc->bnx_miibus);
	bus_generic_detach(dev);

	if (sc->bnx_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->bnx_irq_rid,
		    sc->bnx_irq);
	}
	if (sc->bnx_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->bnx_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    BGE_PCI_BAR0, sc->bnx_res);
	}

	if (sc->bnx_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->bnx_sysctl_ctx);

	bnx_dma_free(sc);

	return 0;
}

static void
bnx_reset(struct bnx_softc *sc)
{
	device_t dev;
	uint32_t cachesize, command, pcistate, reset;
	void (*write_op)(struct bnx_softc *, uint32_t, uint32_t);
	int i, val = 0;
	uint16_t devctl;

	dev = sc->bnx_dev;

	if (sc->bnx_asicrev != BGE_ASICREV_BCM5906)
		write_op = bnx_writemem_direct;
	else
		write_op = bnx_writereg_ind;

	/* Save some important PCI state. */
	cachesize = pci_read_config(dev, BGE_PCI_CACHESZ, 4);
	command = pci_read_config(dev, BGE_PCI_CMD, 4);
	pcistate = pci_read_config(dev, BGE_PCI_PCISTATE, 4);

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
	 * write ~BGE_MAGIC_NUMBER to the same location.
	 */
	bnx_writemem_ind(sc, BGE_SOFTWARE_GENCOMM, BGE_MAGIC_NUMBER);

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
	 * Set GPHY Power Down Override to leave GPHY
	 * powered up in D0 uninitialized.
	 */
	if ((sc->bnx_flags & BNX_FLAG_CPMU) == 0)
		reset |= BGE_MISCCFG_GPHY_PD_OVERRIDE;

	/* Issue global reset */
	write_op(sc, BGE_MISC_CFG, reset);

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906) {
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
	pci_write_config(dev, BGE_PCI_CACHESZ, cachesize, 4);
	pci_write_config(dev, BGE_PCI_CMD, command, 4);
	write_op(sc, BGE_MISC_CFG, (65 << 1));

	/* Enable memory arbiter */
	CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5906) {
		for (i = 0; i < BNX_TIMEOUT; i++) {
			val = CSR_READ_4(sc, BGE_VCPU_STATUS);
			if (val & BGE_VCPU_STATUS_INIT_DONE)
				break;
			DELAY(100);
		}
		if (i == BNX_TIMEOUT) {
			if_printf(&sc->arpcom.ac_if, "reset timed out\n");
			return;
		}
	} else {
		/*
		 * Poll until we see the 1's complement of the magic number.
		 * This indicates that the firmware initialization
		 * is complete.
		 */
		for (i = 0; i < BNX_FIRMWARE_TIMEOUT; i++) {
			val = bnx_readmem_ind(sc, BGE_SOFTWARE_GENCOMM);
			if (val == ~BGE_MAGIC_NUMBER)
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
	}

	/*
	 * XXX Wait for the value of the PCISTATE register to
	 * return to its original pre-reset state. This is a
	 * fairly good indicator of reset completion. If we don't
	 * wait for the reset to fully complete, trying to read
	 * from the device's non-PCI registers may yield garbage
	 * results.
	 */
	for (i = 0; i < BNX_TIMEOUT; i++) {
		if (pci_read_config(dev, BGE_PCI_PCISTATE, 4) == pcistate)
			break;
		DELAY(10);
	}

	/* Fix up byte swapping */
	CSR_WRITE_4(sc, BGE_MODE_CTL, bnx_dma_swap_options(sc));

	CSR_WRITE_4(sc, BGE_MAC_MODE, 0);

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

	/* XXX: Broadcom Linux driver. */
	if (!BNX_IS_57765_PLUS(sc)) {
		uint32_t v;

		/* Enable Data FIFO protection. */
		v = CSR_READ_4(sc, BGE_PCIE_TLDLPL_PORT);
		CSR_WRITE_4(sc, BGE_PCIE_TLDLPL_PORT, v | (1 << 25));
	}

	DELAY(10000);

	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		BNX_CLRBIT(sc, BGE_CPMU_CLCK_ORIDE,
		    CPMU_CLCK_ORIDE_MAC_ORIDE_EN);
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
bnx_rxeof(struct bnx_softc *sc, uint16_t rx_prod)
{
	struct ifnet *ifp;
	int stdcnt = 0, jumbocnt = 0;

	ifp = &sc->arpcom.ac_if;

	while (sc->bnx_rx_saved_considx != rx_prod) {
		struct bge_rx_bd	*cur_rx;
		uint32_t		rxidx;
		struct mbuf		*m = NULL;
		uint16_t		vlan_tag = 0;
		int			have_tag = 0;

		cur_rx =
	    &sc->bnx_ldata.bnx_rx_return_ring[sc->bnx_rx_saved_considx];

		rxidx = cur_rx->bge_idx;
		BNX_INC(sc->bnx_rx_saved_considx, sc->bnx_return_ring_cnt);

		if (cur_rx->bge_flags & BGE_RXBDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->bge_vlan_tag;
		}

		if (cur_rx->bge_flags & BGE_RXBDFLAG_JUMBO_RING) {
			BNX_INC(sc->bnx_jumbo, BGE_JUMBO_RX_RING_CNT);
			jumbocnt++;

			if (rxidx != sc->bnx_jumbo) {
				ifp->if_ierrors++;
				if_printf(ifp, "sw jumbo index(%d) "
				    "and hw jumbo index(%d) mismatch, drop!\n",
				    sc->bnx_jumbo, rxidx);
				bnx_setup_rxdesc_jumbo(sc, rxidx);
				continue;
			}

			m = sc->bnx_cdata.bnx_rx_jumbo_chain[rxidx].bnx_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				ifp->if_ierrors++;
				bnx_setup_rxdesc_jumbo(sc, sc->bnx_jumbo);
				continue;
			}
			if (bnx_newbuf_jumbo(sc, sc->bnx_jumbo, 0)) {
				ifp->if_ierrors++;
				bnx_setup_rxdesc_jumbo(sc, sc->bnx_jumbo);
				continue;
			}
		} else {
			BNX_INC(sc->bnx_std, BGE_STD_RX_RING_CNT);
			stdcnt++;

			if (rxidx != sc->bnx_std) {
				ifp->if_ierrors++;
				if_printf(ifp, "sw std index(%d) "
				    "and hw std index(%d) mismatch, drop!\n",
				    sc->bnx_std, rxidx);
				bnx_setup_rxdesc_std(sc, rxidx);
				continue;
			}

			m = sc->bnx_cdata.bnx_rx_std_chain[rxidx].bnx_mbuf;
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				ifp->if_ierrors++;
				bnx_setup_rxdesc_std(sc, sc->bnx_std);
				continue;
			}
			if (bnx_newbuf_std(sc, sc->bnx_std, 0)) {
				ifp->if_ierrors++;
				bnx_setup_rxdesc_std(sc, sc->bnx_std);
				continue;
			}
		}

		ifp->if_ipackets++;
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

		/*
		 * If we received a packet with a vlan tag, pass it
		 * to vlan_input() instead of ether_input().
		 */
		if (have_tag) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag = vlan_tag;
			have_tag = vlan_tag = 0;
		}
		ifp->if_input(ifp, m);
	}

	bnx_writembx(sc, BGE_MBX_RX_CONS0_LO, sc->bnx_rx_saved_considx);
	if (stdcnt)
		bnx_writembx(sc, BGE_MBX_RX_STD_PROD_LO, sc->bnx_std);
	if (jumbocnt)
		bnx_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, sc->bnx_jumbo);
}

static void
bnx_txeof(struct bnx_softc *sc, uint16_t tx_cons)
{
	struct bge_tx_bd *cur_tx = NULL;
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->bnx_tx_saved_considx != tx_cons) {
		uint32_t idx = 0;

		idx = sc->bnx_tx_saved_considx;
		cur_tx = &sc->bnx_ldata.bnx_tx_ring[idx];
		if (cur_tx->bge_flags & BGE_TXBDFLAG_END)
			ifp->if_opackets++;
		if (sc->bnx_cdata.bnx_tx_chain[idx] != NULL) {
			bus_dmamap_unload(sc->bnx_cdata.bnx_tx_mtag,
			    sc->bnx_cdata.bnx_tx_dmamap[idx]);
			m_freem(sc->bnx_cdata.bnx_tx_chain[idx]);
			sc->bnx_cdata.bnx_tx_chain[idx] = NULL;
		}
		sc->bnx_txcnt--;
		BNX_INC(sc->bnx_tx_saved_considx, BGE_TX_RING_CNT);
	}

	if (cur_tx != NULL &&
	    (BGE_TX_RING_CNT - sc->bnx_txcnt) >=
	    (BNX_NSEG_RSVD + BNX_NSEG_SPARE))
		ifp->if_flags &= ~IFF_OACTIVE;

	if (sc->bnx_txcnt == 0)
		ifp->if_timer = 0;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

#ifdef DEVICE_POLLING

static void
bnx_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct bnx_softc *sc = ifp->if_softc;
	struct bge_status_block *sblk = sc->bnx_ldata.bnx_status_block;
	uint16_t rx_prod, tx_cons;

	switch(cmd) {
	case POLL_REGISTER:
		bnx_disable_intr(sc);
		break;
	case POLL_DEREGISTER:
		bnx_enable_intr(sc);
		break;
	case POLL_AND_CHECK_STATUS:
		/*
		 * Process link state changes.
		 */
		bnx_link_poll(sc);
		/* Fall through */
	case POLL_ONLY:
		sc->bnx_status_tag = sblk->bge_status_tag;
		/*
		 * Use a load fence to ensure that status_tag
		 * is saved  before rx_prod and tx_cons.
		 */
		cpu_lfence();

		rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
		tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
		if (ifp->if_flags & IFF_RUNNING) {
			rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
			if (sc->bnx_rx_saved_considx != rx_prod)
				bnx_rxeof(sc, rx_prod);

			tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
			if (sc->bnx_tx_saved_considx != tx_cons)
				bnx_txeof(sc, tx_cons);
		}
		break;
	}
}

#endif

static void
bnx_intr_legacy(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct bge_status_block *sblk = sc->bnx_ldata.bnx_status_block;

	if (sc->bnx_status_tag == sblk->bge_status_tag) {
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
	struct bnx_softc *sc = xsc;

	/* Disable interrupt first */
	bnx_writembx(sc, BGE_MBX_IRQ0_LO, 1);
	bnx_intr(sc);
}

static void
bnx_msi_oneshot(void *xsc)
{
	bnx_intr(xsc);
}

static void
bnx_intr(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_status_block *sblk = sc->bnx_ldata.bnx_status_block;
	uint16_t rx_prod, tx_cons;
	uint32_t status;

	sc->bnx_status_tag = sblk->bge_status_tag;
	/*
	 * Use a load fence to ensure that status_tag is saved 
	 * before rx_prod, tx_cons and status.
	 */
	cpu_lfence();

	rx_prod = sblk->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sblk->bge_idx[0].bge_tx_cons_idx;
	status = sblk->bge_status;

	if ((status & BGE_STATFLAG_LINKSTATE_CHANGED) || sc->bnx_link_evt)
		bnx_link_poll(sc);

	if (ifp->if_flags & IFF_RUNNING) {
		if (sc->bnx_rx_saved_considx != rx_prod)
			bnx_rxeof(sc, rx_prod);

		if (sc->bnx_tx_saved_considx != tx_cons)
			bnx_txeof(sc, tx_cons);
	}

	bnx_writembx(sc, BGE_MBX_IRQ0_LO, sc->bnx_status_tag << 24);

	if (sc->bnx_coal_chg)
		bnx_coal_change(sc);
}

static void
bnx_tick(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	KKASSERT(mycpuid == sc->bnx_stat_cpuid);

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

	callout_reset(&sc->bnx_stat_timer, hz, bnx_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
bnx_stats_update_regs(struct bnx_softc *sc)
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

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
bnx_encap(struct bnx_softc *sc, struct mbuf **m_head0, uint32_t *txidx)
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

		error = bnx_setup_tso(sc, m_head0, &mss, &csum_flags);
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
		sc->bnx_tsosegs[tso_nsegs]++;
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
	map = sc->bnx_cdata.bnx_tx_dmamap[idx];

	maxsegs = (BGE_TX_RING_CNT - sc->bnx_txcnt) - BNX_NSEG_RSVD;
	KASSERT(maxsegs >= BNX_NSEG_SPARE,
		("not enough segments %d", maxsegs));

	if (maxsegs > BNX_NSEG_NEW)
		maxsegs = BNX_NSEG_NEW;

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
	    m_head->m_pkthdr.len < BNX_MIN_FRAMELEN) {
		error = m_devpad(m_head, BNX_MIN_FRAMELEN);
		if (error)
			goto back;
	}

	if ((sc->bnx_flags & BNX_FLAG_SHORTDMA) && m_head->m_next != NULL) {
		m_new = bnx_defrag_shortdma(m_head);
		if (m_new == NULL) {
			error = ENOBUFS;
			goto back;
		}
		*m_head0 = m_head = m_new;
	}
	if ((m_head->m_pkthdr.csum_flags & CSUM_TSO) == 0 &&
	    sc->bnx_force_defrag && m_head->m_next != NULL) {
		/*
		 * Forcefully defragment mbuf chain to overcome hardware
		 * limitation which only support a single outstanding
		 * DMA read operation.  If it fails, keep moving on using
		 * the original mbuf chain.
		 */
		m_new = m_defrag(m_head, MB_DONTWAIT);
		if (m_new != NULL)
			*m_head0 = m_head = m_new;
	}

	error = bus_dmamap_load_mbuf_defrag(sc->bnx_cdata.bnx_tx_mtag, map,
			m_head0, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;

	m_head = *m_head0;
	bus_dmamap_sync(sc->bnx_cdata.bnx_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	for (i = 0; ; i++) {
		d = &sc->bnx_ldata.bnx_tx_ring[idx];

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
	sc->bnx_cdata.bnx_tx_dmamap[*txidx] = sc->bnx_cdata.bnx_tx_dmamap[idx];
	sc->bnx_cdata.bnx_tx_dmamap[idx] = map;
	sc->bnx_cdata.bnx_tx_chain[idx] = m_head;
	sc->bnx_txcnt += nsegs;

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
bnx_start(struct ifnet *ifp)
{
	struct bnx_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	uint32_t prodidx;
	int need_trans;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	prodidx = sc->bnx_tx_prodidx;

	need_trans = 0;
	while (sc->bnx_cdata.bnx_tx_chain[prodidx] == NULL) {
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
			if ((BGE_TX_RING_CNT - sc->bnx_txcnt) <
			    m_head->m_pkthdr.csum_data + BNX_NSEG_RSVD) {
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
		if ((BGE_TX_RING_CNT - sc->bnx_txcnt) <
		    (BNX_NSEG_RSVD + BNX_NSEG_SPARE)) {
			ifp->if_flags |= IFF_OACTIVE;
			ifq_prepend(&ifp->if_snd, m_head);
			break;
		}

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (bnx_encap(sc, &m_head, &prodidx)) {
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
	bnx_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);

	sc->bnx_tx_prodidx = prodidx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
bnx_init(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t *m;
	uint32_t mode;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/* Cancel pending I/O and flush buffers. */
	bnx_stop(sc);
	bnx_reset(sc);
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
	if (bnx_init_rx_ring_std(sc)) {
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
	sc->bnx_rx_saved_considx = 0;

	/* Init TX ring. */
	bnx_init_tx_ring(sc);

	/* Enable TX MAC state machine lockup fix. */
	mode = CSR_READ_4(sc, BGE_TX_MODE);
	mode |= BGE_TXMODE_MBUF_LOCKUP_FIX;
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		mode &= ~(BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
		mode |= CSR_READ_4(sc, BGE_TX_MODE) &
		    (BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
	}
	/* Turn on transmitter */
	CSR_WRITE_4(sc, BGE_TX_MODE, mode | BGE_TXMODE_ENABLE);

	/* Turn on receiver */
	BNX_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);

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

	if (sc->bnx_irq_type == PCI_INTR_TYPE_MSI) {
		if (bootverbose) {
			if_printf(ifp, "MSI_MODE: %#x\n",
			    CSR_READ_4(sc, BGE_MSI_MODE));
		}
	}

	/* Tell firmware we're alive. */
	BNX_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Enable host interrupts if polling(4) is not enabled. */
	PCI_SETBIT(sc->bnx_dev, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_CLEAR_INTA, 4);
#ifdef DEVICE_POLLING
	if (ifp->if_flags & IFF_POLLING)
		bnx_disable_intr(sc);
	else
#endif
	bnx_enable_intr(sc);

	bnx_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset_bycpu(&sc->bnx_stat_timer, hz, bnx_tick, sc,
	    sc->bnx_stat_cpuid);
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

	ASSERT_SERIALIZED(ifp->if_serializer);

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
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return error;
}

static void
bnx_watchdog(struct ifnet *ifp)
{
	struct bnx_softc *sc = ifp->if_softc;

	if_printf(ifp, "watchdog timeout -- resetting\n");

	bnx_init(sc);

	ifp->if_oerrors++;

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
bnx_stop(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ASSERT_SERIALIZED(ifp->if_serializer);

	callout_stop(&sc->bnx_stat_timer);

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

	/* Disable host interrupts. */
	bnx_disable_intr(sc);

	/*
	 * Tell firmware we're shutting down.
	 */
	BNX_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Free the RX lists. */
	bnx_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	if (BNX_IS_JUMBO_CAPABLE(sc))
		bnx_free_rx_ring_jumbo(sc);

	/* Free TX buffers. */
	bnx_free_tx_ring(sc);

	sc->bnx_status_tag = 0;
	sc->bnx_link = 0;
	sc->bnx_coal_chg = 0;

	sc->bnx_tx_saved_considx = BNX_TXCONS_UNSET;

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
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

	lwkt_serialize_enter(ifp->if_serializer);
	bnx_stop(sc);
	bnx_reset(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
bnx_suspend(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	bnx_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);

	return 0;
}

static int
bnx_resume(device_t dev)
{
	struct bnx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ifp->if_flags & IFF_UP) {
		bnx_init(sc);

		if (!ifq_is_empty(&ifp->if_snd))
			if_devstart(ifp);
	}

	lwkt_serialize_exit(ifp->if_serializer);

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
	int i;

	/* Destroy RX mbuf DMA stuffs. */
	if (sc->bnx_cdata.bnx_rx_mtag != NULL) {
		for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
			bus_dmamap_destroy(sc->bnx_cdata.bnx_rx_mtag,
			    sc->bnx_cdata.bnx_rx_std_dmamap[i]);
		}
		bus_dmamap_destroy(sc->bnx_cdata.bnx_rx_mtag,
				   sc->bnx_cdata.bnx_rx_tmpmap);
		bus_dma_tag_destroy(sc->bnx_cdata.bnx_rx_mtag);
	}

	/* Destroy TX mbuf DMA stuffs. */
	if (sc->bnx_cdata.bnx_tx_mtag != NULL) {
		for (i = 0; i < BGE_TX_RING_CNT; i++) {
			bus_dmamap_destroy(sc->bnx_cdata.bnx_tx_mtag,
			    sc->bnx_cdata.bnx_tx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->bnx_cdata.bnx_tx_mtag);
	}

	/* Destroy standard RX ring */
	bnx_dma_block_free(sc->bnx_cdata.bnx_rx_std_ring_tag,
			   sc->bnx_cdata.bnx_rx_std_ring_map,
			   sc->bnx_ldata.bnx_rx_std_ring);

	if (BNX_IS_JUMBO_CAPABLE(sc))
		bnx_free_jumbo_mem(sc);

	/* Destroy RX return ring */
	bnx_dma_block_free(sc->bnx_cdata.bnx_rx_return_ring_tag,
			   sc->bnx_cdata.bnx_rx_return_ring_map,
			   sc->bnx_ldata.bnx_rx_return_ring);

	/* Destroy TX ring */
	bnx_dma_block_free(sc->bnx_cdata.bnx_tx_ring_tag,
			   sc->bnx_cdata.bnx_tx_ring_map,
			   sc->bnx_ldata.bnx_tx_ring);

	/* Destroy status block */
	bnx_dma_block_free(sc->bnx_cdata.bnx_status_tag,
			   sc->bnx_cdata.bnx_status_map,
			   sc->bnx_ldata.bnx_status_block);

	/* Destroy the parent tag */
	if (sc->bnx_cdata.bnx_parent_tag != NULL)
		bus_dma_tag_destroy(sc->bnx_cdata.bnx_parent_tag);
}

static int
bnx_dma_alloc(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	bus_size_t txmaxsz;
	int i, error;

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
				   NULL, NULL,
				   BUS_SPACE_MAXSIZE_32BIT, 0,
				   BUS_SPACE_MAXSIZE_32BIT,
				   0, &sc->bnx_cdata.bnx_parent_tag);
	if (error) {
		if_printf(ifp, "could not allocate parent dma tag\n");
		return error;
	}

	/*
	 * Create DMA tag and maps for RX mbufs.
	 */
	error = bus_dma_tag_create(sc->bnx_cdata.bnx_parent_tag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL, MCLBYTES, 1, MCLBYTES,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,
				   &sc->bnx_cdata.bnx_rx_mtag);
	if (error) {
		if_printf(ifp, "could not allocate RX mbuf dma tag\n");
		return error;
	}

	error = bus_dmamap_create(sc->bnx_cdata.bnx_rx_mtag,
				  BUS_DMA_WAITOK, &sc->bnx_cdata.bnx_rx_tmpmap);
	if (error) {
		bus_dma_tag_destroy(sc->bnx_cdata.bnx_rx_mtag);
		sc->bnx_cdata.bnx_rx_mtag = NULL;
		return error;
	}

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bnx_cdata.bnx_rx_mtag,
					  BUS_DMA_WAITOK,
					  &sc->bnx_cdata.bnx_rx_std_dmamap[i]);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->bnx_cdata.bnx_rx_mtag,
					sc->bnx_cdata.bnx_rx_std_dmamap[j]);
			}
			bus_dma_tag_destroy(sc->bnx_cdata.bnx_rx_mtag);
			sc->bnx_cdata.bnx_rx_mtag = NULL;

			if_printf(ifp, "could not create DMA map for RX\n");
			return error;
		}
	}

	/*
	 * Create DMA tag and maps for TX mbufs.
	 */
	if (sc->bnx_flags & BNX_FLAG_TSO)
		txmaxsz = IP_MAXPACKET + sizeof(struct ether_vlan_header);
	else
		txmaxsz = BNX_JUMBO_FRAMELEN;
	error = bus_dma_tag_create(sc->bnx_cdata.bnx_parent_tag, 1, 0,
				   BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   txmaxsz, BNX_NSEG_NEW, PAGE_SIZE,
				   BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK |
				   BUS_DMA_ONEBPAGE,
				   &sc->bnx_cdata.bnx_tx_mtag);
	if (error) {
		if_printf(ifp, "could not allocate TX mbuf dma tag\n");
		return error;
	}

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bnx_cdata.bnx_tx_mtag,
					  BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
					  &sc->bnx_cdata.bnx_tx_dmamap[i]);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->bnx_cdata.bnx_tx_mtag,
					sc->bnx_cdata.bnx_tx_dmamap[j]);
			}
			bus_dma_tag_destroy(sc->bnx_cdata.bnx_tx_mtag);
			sc->bnx_cdata.bnx_tx_mtag = NULL;

			if_printf(ifp, "could not create DMA map for TX\n");
			return error;
		}
	}

	/*
	 * Create DMA stuffs for standard RX ring.
	 */
	error = bnx_dma_block_alloc(sc, BGE_STD_RX_RING_SZ,
				    &sc->bnx_cdata.bnx_rx_std_ring_tag,
				    &sc->bnx_cdata.bnx_rx_std_ring_map,
				    (void *)&sc->bnx_ldata.bnx_rx_std_ring,
				    &sc->bnx_ldata.bnx_rx_std_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create std RX ring\n");
		return error;
	}

	/*
	 * Create jumbo buffer pool.
	 */
	if (BNX_IS_JUMBO_CAPABLE(sc)) {
		error = bnx_alloc_jumbo_mem(sc);
		if (error) {
			if_printf(ifp, "could not create jumbo buffer pool\n");
			return error;
		}
	}

	/*
	 * Create DMA stuffs for RX return ring.
	 */
	error = bnx_dma_block_alloc(sc,
	    BGE_RX_RTN_RING_SZ(sc->bnx_return_ring_cnt),
	    &sc->bnx_cdata.bnx_rx_return_ring_tag,
	    &sc->bnx_cdata.bnx_rx_return_ring_map,
	    (void *)&sc->bnx_ldata.bnx_rx_return_ring,
	    &sc->bnx_ldata.bnx_rx_return_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create RX ret ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for TX ring.
	 */
	error = bnx_dma_block_alloc(sc, BGE_TX_RING_SZ,
				    &sc->bnx_cdata.bnx_tx_ring_tag,
				    &sc->bnx_cdata.bnx_tx_ring_map,
				    (void *)&sc->bnx_ldata.bnx_tx_ring,
				    &sc->bnx_ldata.bnx_tx_ring_paddr);
	if (error) {
		if_printf(ifp, "could not create TX ring\n");
		return error;
	}

	/*
	 * Create DMA stuffs for status block.
	 */
	error = bnx_dma_block_alloc(sc, BGE_STATUS_BLK_SZ,
				    &sc->bnx_cdata.bnx_status_tag,
				    &sc->bnx_cdata.bnx_status_map,
				    (void *)&sc->bnx_ldata.bnx_status_block,
				    &sc->bnx_ldata.bnx_status_block_paddr);
	if (error) {
		if_printf(ifp, "could not create status block\n");
		return error;
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
bnx_sysctl_tx_coal_bds(SYSCTL_HANDLER_ARGS)
{
	struct bnx_softc *sc = arg1;

	return bnx_sysctl_coal_chg(oidp, arg1, arg2, req,
	    &sc->bnx_tx_coal_bds,
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

	lwkt_serialize_enter(ifp->if_serializer);

	v = *coal;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != NULL) {
		if (v < coal_min || v > coal_max) {
			error = EINVAL;
		} else {
			*coal = v;
			sc->bnx_coal_chg |= coal_chg_mask;
		}
	}

	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
bnx_coal_change(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t val;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->bnx_coal_chg & BNX_RX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS,
			    sc->bnx_rx_coal_ticks);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_RX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_ticks -> %u\n",
				  sc->bnx_rx_coal_ticks);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_TICKS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS,
			    sc->bnx_tx_coal_ticks);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_TX_COAL_TICKS);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_ticks -> %u\n",
				  sc->bnx_tx_coal_ticks);
		}
	}

	if (sc->bnx_coal_chg & BNX_RX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS,
			    sc->bnx_rx_coal_bds);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_RX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_bds -> %u\n",
				  sc->bnx_rx_coal_bds);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_BDS_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS,
			    sc->bnx_tx_coal_bds);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_TX_MAX_COAL_BDS);

		if (bootverbose) {
			if_printf(ifp, "tx_max_coal_bds -> %u\n",
				  sc->bnx_tx_coal_bds);
		}
	}

	if (sc->bnx_coal_chg & BNX_RX_COAL_BDS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT,
		    sc->bnx_rx_coal_bds_int);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT);

		if (bootverbose) {
			if_printf(ifp, "rx_coal_bds_int -> %u\n",
			    sc->bnx_rx_coal_bds_int);
		}
	}

	if (sc->bnx_coal_chg & BNX_TX_COAL_BDS_INT_CHG) {
		CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT,
		    sc->bnx_tx_coal_bds_int);
		DELAY(10);
		val = CSR_READ_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT);

		if (bootverbose) {
			if_printf(ifp, "tx_coal_bds_int -> %u\n",
			    sc->bnx_tx_coal_bds_int);
		}
	}

	sc->bnx_coal_chg = 0;
}

static void
bnx_intr_check(void *xsc)
{
	struct bnx_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct bge_status_block *sblk = sc->bnx_ldata.bnx_status_block;

	lwkt_serialize_enter(ifp->if_serializer);

	KKASSERT(mycpuid == sc->bnx_intr_cpuid);

	if ((ifp->if_flags & (IFF_RUNNING | IFF_POLLING)) != IFF_RUNNING) {
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	if (sblk->bge_idx[0].bge_rx_prod_idx != sc->bnx_rx_saved_considx ||
	    sblk->bge_idx[0].bge_tx_cons_idx != sc->bnx_tx_saved_considx) {
		if (sc->bnx_rx_check_considx == sc->bnx_rx_saved_considx &&
		    sc->bnx_tx_check_considx == sc->bnx_tx_saved_considx) {
			if (!sc->bnx_intr_maylose) {
				sc->bnx_intr_maylose = TRUE;
				goto done;
			}
			if (bootverbose)
				if_printf(ifp, "lost interrupt\n");
			bnx_msi(sc);
		}
	}
	sc->bnx_intr_maylose = FALSE;
	sc->bnx_rx_check_considx = sc->bnx_rx_saved_considx;
	sc->bnx_tx_check_considx = sc->bnx_tx_saved_considx;

done:
	callout_reset(&sc->bnx_intr_timer, BNX_INTR_CKINTVL,
	    bnx_intr_check, sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
bnx_enable_intr(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_handler_enable(ifp->if_serializer);

	/*
	 * Enable interrupt.
	 */
	bnx_writembx(sc, BGE_MBX_IRQ0_LO, sc->bnx_status_tag << 24);
	if (sc->bnx_flags & BNX_FLAG_ONESHOT_MSI) {
		/* XXX Linux driver */
		bnx_writembx(sc, BGE_MBX_IRQ0_LO, sc->bnx_status_tag << 24);
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
		sc->bnx_intr_maylose = FALSE;
		sc->bnx_rx_check_considx = 0;
		sc->bnx_tx_check_considx = 0;

		if (bootverbose)
			if_printf(ifp, "status tag bug workaround\n");

		/* 10ms check interval */
		callout_reset_bycpu(&sc->bnx_intr_timer, BNX_INTR_CKINTVL,
		    bnx_intr_check, sc, sc->bnx_intr_cpuid);
	}
}

static void
bnx_disable_intr(struct bnx_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/*
	 * Mask the interrupt when we start polling.
	 */
	PCI_SETBIT(sc->bnx_dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_MASK_PCI_INTR, 4);

	/*
	 * Acknowledge possible asserted interrupt.
	 */
	bnx_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	callout_stop(&sc->bnx_intr_timer);
	sc->bnx_intr_maylose = FALSE;
	sc->bnx_rx_check_considx = 0;
	sc->bnx_tx_check_considx = 0;

	lwkt_serialize_handler_disable(ifp->if_serializer);
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
	} else if (sc->bnx_asicrev == BGE_ASICREV_BCM5906) {
		mac_offset = BGE_EE_MAC_OFFSET_5906;
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
struct mbuf *
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
		n = m_defrag(m, MB_DONTWAIT);
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
bnx_enable_msi(struct bnx_softc *sc)
{
	uint32_t msi_mode;

	msi_mode = CSR_READ_4(sc, BGE_MSI_MODE);
	msi_mode |= BGE_MSIMODE_ENABLE;
	if (sc->bnx_flags & BNX_FLAG_ONESHOT_MSI) {
		/*
		 * NOTE:
		 * 5718-PG105-R says that "one shot" mode
		 * does not work if MSI is used, however,
		 * it obviously works.
		 */
		msi_mode &= ~BGE_MSIMODE_ONESHOT_DISABLE;
	}
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
	if (sc->bnx_asicrev == BGE_ASICREV_BCM5720) {
		dma_options |= BGE_MODECTL_BYTESWAP_B2HRX_DATA |
		    BGE_MODECTL_WORDSWAP_B2HRX_DATA | BGE_MODECTL_B2HRX_ENABLE |
		    BGE_MODECTL_HTX2B_ENABLE;
	}
	return dma_options;
}

static int
bnx_setup_tso(struct bnx_softc *sc, struct mbuf **mp,
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
