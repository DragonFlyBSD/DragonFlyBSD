/*
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * $OpenBSD: if_sk.c,v 1.129 2006/10/16 12:30:08 tom Exp $
 * $FreeBSD: /c/ncvs/src/sys/pci/if_sk.c,v 1.20 2000/04/22 02:16:37 wpaul Exp $
 * $DragonFly: src/sys/dev/netif/sk/if_sk.c,v 1.49 2006/11/14 12:52:31 sephe Exp $
 */

/*
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SysKonnect SK-NET gigabit ethernet driver for FreeBSD. Supports
 * the SK-984x series adapters, both single port and dual port.
 * References:
 * 	The XaQti XMAC II datasheet,
 * http://www.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *	The SysKonnect GEnesis manual, http://www.syskonnect.com
 *
 * Note: XaQti has been acquired by Vitesse, and Vitesse does not have the
 * XMAC II datasheet online. I have put my copy at people.freebsd.org as a
 * convenience to others until Vitesse corrects this problem:
 *
 * http://people.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Department of Electrical Engineering
 * Columbia University, New York City
 */

/*
 * The SysKonnect gigabit ethernet adapters consist of two main
 * components: the SysKonnect GEnesis controller chip and the XaQti Corp.
 * XMAC II gigabit ethernet MAC. The XMAC provides all of the MAC
 * components and a PHY while the GEnesis controller provides a PCI
 * interface with DMA support. Each card may have between 512K and
 * 2MB of SRAM on board depending on the configuration.
 *
 * The SysKonnect GEnesis controller can have either one or two XMAC
 * chips connected to it, allowing single or dual port NIC configurations.
 * SysKonnect has the distinction of being the only vendor on the market
 * with a dual port gigabit ethernet NIC. The GEnesis provides dual FIFOs,
 * dual DMA queues, packet/MAC/transmit arbiters and direct access to the
 * XMAC registers. This driver takes advantage of these features to allow
 * both XMACs to operate as independent interfaces.
 */
 
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/in_cksum.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ifq_var.h>
#include <net/vlan/if_vlan_var.h>

#include <netinet/ip.h>
#include <netinet/udp.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>
#include <dev/netif/mii_layer/brgphyreg.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcidevs.h>

#include <dev/netif/sk/if_skreg.h>
#include <dev/netif/sk/yukonreg.h>
#include <dev/netif/sk/xmaciireg.h>
#include <dev/netif/sk/if_skvar.h>

#include "miibus_if.h"

#if 0
#define SK_DEBUG
#endif

#if 0
#define SK_RXCSUM
#endif

/* supported device vendors */
static const struct skc_type {
	uint16_t	skc_vid;
	uint16_t	skc_did;
	const char	*skc_name;
} skc_devs[] = {
	{ PCI_VENDOR_3COM,		PCI_PRODUCT_3COM_3C940,
	  "3Com 3C940" },
	{ PCI_VENDOR_3COM,		PCI_PRODUCT_3COM_3C940B,
	  "3Com 3C940B" },

	{ PCI_VENDOR_CNET,		PCI_PRODUCT_CNET_GIGACARD,
	  "CNet GigaCard" },

	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE530T_A1,
	  "D-Link DGE-530T A1" },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE530T_B1,
	  "D-Link DGE-530T B1" },

	{ PCI_VENDOR_LINKSYS,		PCI_PRODUCT_LINKSYS_EG1032,
	  "Linksys EG1032 v2" },
	{ PCI_VENDOR_LINKSYS,		PCI_PRODUCT_LINKSYS_EG1064,
	  "Linksys EG1064" },

	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON,
	  "Marvell Yukon 88E8001/8003/8010" },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_BELKIN,
	  "Belkin F5D5005" },

	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SKNET_GE,
	  "SysKonnect SK-NET" },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK9821v2,
	  "SysKonnect SK9821 v2" },

	{ 0, 0, NULL }
};

static int	skc_probe(device_t);
static int	skc_attach(device_t);
static int	skc_detach(device_t);
static void	skc_shutdown(device_t);
static int	sk_probe(device_t);
static int	sk_attach(device_t);
static int	sk_detach(device_t);
static void	sk_tick(void *);
static void	sk_yukon_tick(void *);
static void	sk_intr(void *);
static void	sk_intr_bcom(struct sk_if_softc *);
static void	sk_intr_xmac(struct sk_if_softc *);
static void	sk_intr_yukon(struct sk_if_softc *);
static void	sk_rxeof(struct sk_if_softc *);
static void	sk_txeof(struct sk_if_softc *);
static int	sk_encap(struct sk_if_softc *, struct mbuf *, uint32_t *);
static void	sk_start(struct ifnet *);
static int	sk_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	sk_init(void *);
static void	sk_init_xmac(struct sk_if_softc *);
static void	sk_init_yukon(struct sk_if_softc *);
static void	sk_stop(struct sk_if_softc *);
static void	sk_watchdog(struct ifnet *);
static int	sk_ifmedia_upd(struct ifnet *);
static void	sk_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	sk_reset(struct sk_softc *);
static int	sk_newbuf(struct sk_if_softc *, struct sk_chain *,
			  struct mbuf *, int);
static int	sk_jpool_alloc(device_t);
static void	sk_jpool_free(struct sk_if_softc *);
static struct sk_jpool_entry
		*sk_jalloc(struct sk_if_softc *);
static void	sk_jfree(void *);
static void	sk_jref(void *);
static int	sk_init_rx_ring(struct sk_if_softc *);
static int	sk_init_tx_ring(struct sk_if_softc *);

static int	sk_miibus_readreg(device_t, int, int);
static int	sk_miibus_writereg(device_t, int, int, int);
static void	sk_miibus_statchg(device_t);

static int	sk_xmac_miibus_readreg(struct sk_if_softc *, int, int);
static int	sk_xmac_miibus_writereg(struct sk_if_softc *, int, int, int);
static void	sk_xmac_miibus_statchg(struct sk_if_softc *);

static int	sk_marv_miibus_readreg(struct sk_if_softc *, int, int);
static int	sk_marv_miibus_writereg(struct sk_if_softc *, int, int, int);
static void	sk_marv_miibus_statchg(struct sk_if_softc *);

static void	sk_setfilt(struct sk_if_softc *, caddr_t, int);
static void	sk_setmulti(struct sk_if_softc *);
static void	sk_setpromisc(struct sk_if_softc *);

#ifdef SK_RXCSUM
static void	sk_rxcsum(struct ifnet *, struct mbuf *, const uint16_t,
			  const uint16_t);
#endif
static int	sk_dma_alloc(device_t);
static void	sk_dma_free(device_t);

static void	sk_buf_dma_addr(void *, bus_dma_segment_t *, int, bus_size_t,
				int);
static void	sk_dmamem_addr(void *, bus_dma_segment_t *, int, int);

#ifdef SK_DEBUG
#define DPRINTF(x)	if (skdebug) printf x
#define DPRINTFN(n,x)	if (skdebug >= (n)) printf x
static int	skdebug = 2;

static void	sk_dump_txdesc(struct sk_tx_desc *, int);
static void	sk_dump_mbuf(struct mbuf *);
static void	sk_dump_bytes(const char *, int);
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Note that we have newbus methods for both the GEnesis controller
 * itself and the XMAC(s). The XMACs are children of the GEnesis, and
 * the miibus code is a child of the XMACs. We need to do it this way
 * so that the miibus drivers can access the PHY registers on the
 * right PHY. It's not quite what I had in mind, but it's the only
 * design that achieves the desired effect.
 */
static device_method_t skc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		skc_probe),
	DEVMETHOD(device_attach,	skc_attach),
	DEVMETHOD(device_detach,	skc_detach),
	DEVMETHOD(device_shutdown,	skc_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	{ 0, 0 }
};

static DEFINE_CLASS_0(skc, skc_driver, skc_methods, sizeof(struct sk_softc));
static devclass_t skc_devclass;

static device_method_t sk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sk_probe),
	DEVMETHOD(device_attach,	sk_attach),
	DEVMETHOD(device_detach,	sk_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sk_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sk_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sk_miibus_statchg),

	{ 0, 0 }
};

static DEFINE_CLASS_0(sk, sk_driver, sk_methods, sizeof(struct sk_if_softc));
static devclass_t sk_devclass;

DECLARE_DUMMY_MODULE(if_sk);
DRIVER_MODULE(if_sk, pci, skc_driver, skc_devclass, 0, 0);
DRIVER_MODULE(if_sk, skc, sk_driver, sk_devclass, 0, 0);
DRIVER_MODULE(miibus, sk, miibus_driver, miibus_devclass, 0, 0);

static __inline uint32_t
sk_win_read_4(struct sk_softc *sc, uint32_t reg)
{
	return CSR_READ_4(sc, reg);
}

static __inline uint16_t
sk_win_read_2(struct sk_softc *sc, uint32_t reg)
{
	return CSR_READ_2(sc, reg);
}

static __inline uint8_t
sk_win_read_1(struct sk_softc *sc, uint32_t reg)
{
	return CSR_READ_1(sc, reg);
}

static __inline void
sk_win_write_4(struct sk_softc *sc, uint32_t reg, uint32_t x)
{
	CSR_WRITE_4(sc, reg, x);
}

static __inline void
sk_win_write_2(struct sk_softc *sc, uint32_t reg, uint16_t x)
{
	CSR_WRITE_2(sc, reg, x);
}

static __inline void
sk_win_write_1(struct sk_softc *sc, uint32_t reg, uint8_t x)
{
	CSR_WRITE_1(sc, reg, x);
}

static int
sk_miibus_readreg(device_t dev, int phy, int reg)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);

	if (SK_IS_GENESIS(sc_if->sk_softc))
		return sk_xmac_miibus_readreg(sc_if, phy, reg);
	else
		return sk_marv_miibus_readreg(sc_if, phy, reg);
}

static int
sk_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);

	if (SK_IS_GENESIS(sc_if->sk_softc))
		return sk_xmac_miibus_writereg(sc_if, phy, reg, val);
	else
		return sk_marv_miibus_writereg(sc_if, phy, reg, val);
}

static void
sk_miibus_statchg(device_t dev)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);

	if (SK_IS_GENESIS(sc_if->sk_softc))
		sk_xmac_miibus_statchg(sc_if);
	else
		sk_marv_miibus_statchg(sc_if);
}

static int
sk_xmac_miibus_readreg(struct sk_if_softc *sc_if, int phy, int reg)
{
	int i;

	DPRINTFN(9, ("sk_xmac_miibus_readreg\n"));

	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC && phy != 0)
		return(0);

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	SK_XM_READ_2(sc_if, XM_PHY_DATA);
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		for (i = 0; i < SK_TIMEOUT; i++) {
			DELAY(1);
			if (SK_XM_READ_2(sc_if, XM_MMUCMD) &
			    XM_MMUCMD_PHYDATARDY)
				break;
		}

		if (i == SK_TIMEOUT) {
			if_printf(&sc_if->arpcom.ac_if,
				  "phy failed to come ready\n");
			return(0);
		}
	}
	DELAY(1);
	return(SK_XM_READ_2(sc_if, XM_PHY_DATA));
}

static int
sk_xmac_miibus_writereg(struct sk_if_softc *sc_if, int phy, int reg, int val)
{
	int i;

	DPRINTFN(9, ("sk_xmac_miibus_writereg\n"));

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	for (i = 0; i < SK_TIMEOUT; i++) {
		if ((SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY) == 0)
			break;
	}

	if (i == SK_TIMEOUT) {
		if_printf(&sc_if->arpcom.ac_if, "phy failed to come ready\n");
		return(ETIMEDOUT);
	}

	SK_XM_WRITE_2(sc_if, XM_PHY_DATA, val);
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if ((SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY) == 0)
			break;
	}

	if (i == SK_TIMEOUT)
		if_printf(&sc_if->arpcom.ac_if, "phy write timed out\n");
	return(0);
}

static void
sk_xmac_miibus_statchg(struct sk_if_softc *sc_if)
{
	struct mii_data *mii;

	mii = device_get_softc(sc_if->sk_miibus);
	DPRINTFN(9, ("sk_xmac_miibus_statchg\n"));

	/*
	 * If this is a GMII PHY, manually set the XMAC's
	 * duplex mode accordingly.
	 */
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
			SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		else
			SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
	}
}

static int
sk_marv_miibus_readreg(struct sk_if_softc *sc_if, int phy, int reg)
{
	uint16_t val;
	int i;

	if (phy != 0 ||
	    (sc_if->sk_phytype != SK_PHYTYPE_MARV_COPPER &&
	     sc_if->sk_phytype != SK_PHYTYPE_MARV_FIBER)) {
		DPRINTFN(9, ("sk_marv_miibus_readreg (skip) phy=%d, reg=%#x\n",
			     phy, reg));
		return(0);
	}

        SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_READ);
        
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		val = SK_YU_READ_2(sc_if, YUKON_SMICR);
		if (val & YU_SMICR_READ_VALID)
			break;
	}

	if (i == SK_TIMEOUT) {
		if_printf(&sc_if->arpcom.ac_if, "phy failed to come ready\n");
		return(0);
	}
        
 	DPRINTFN(9, ("sk_marv_miibus_readreg: i=%d, timeout=%d\n", i,
		     SK_TIMEOUT));

	val = SK_YU_READ_2(sc_if, YUKON_SMIDR);

	DPRINTFN(9, ("sk_marv_miibus_readreg phy=%d, reg=%#x, val=%#x\n",
		     phy, reg, val));

	return(val);
}

static int
sk_marv_miibus_writereg(struct sk_if_softc *sc_if, int phy, int reg, int val)
{
	int i;

	DPRINTFN(9, ("sk_marv_miibus_writereg phy=%d reg=%#x val=%#x\n",
		     phy, reg, val));

	SK_YU_WRITE_2(sc_if, YUKON_SMIDR, val);
	SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_WRITE);

	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (SK_YU_READ_2(sc_if, YUKON_SMICR) & YU_SMICR_BUSY)
			break;
	}

	if (i == SK_TIMEOUT)
		if_printf(&sc_if->arpcom.ac_if, "phy write timed out\n");

	return(0);
}

static void
sk_marv_miibus_statchg(struct sk_if_softc *sc_if)
{
	DPRINTFN(9, ("sk_marv_miibus_statchg: gpcr=%x\n",
		     SK_YU_READ_2(sc_if, YUKON_GPCR)));
}

#define HASH_BITS	6
  
static uint32_t
sk_xmac_hash(caddr_t addr)
{
	uint32_t crc;

	crc = ether_crc32_le(addr, ETHER_ADDR_LEN);
	return (~crc & ((1 << HASH_BITS) - 1));
}

static uint32_t
sk_yukon_hash(caddr_t addr)
{
	uint32_t crc;

	crc = ether_crc32_be(addr, ETHER_ADDR_LEN);
	return (crc & ((1 << HASH_BITS) - 1));
}

static void
sk_setfilt(struct sk_if_softc *sc_if, caddr_t addr, int slot)
{
	int base;

	base = XM_RXFILT_ENTRY(slot);

	SK_XM_WRITE_2(sc_if, base, *(uint16_t *)(&addr[0]));
	SK_XM_WRITE_2(sc_if, base + 2, *(uint16_t *)(&addr[2]));
	SK_XM_WRITE_2(sc_if, base + 4, *(uint16_t *)(&addr[4]));
}

static void
sk_setmulti(struct sk_if_softc *sc_if)
{
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	uint32_t hashes[2] = { 0, 0 };
	int h = 0, i;
	struct ifmultiaddr *ifma;
	uint8_t dummy[] = { 0, 0, 0, 0, 0 ,0 };

	/* First, zot all the existing filters. */
	switch(sc->sk_type) {
	case SK_GENESIS:
		for (i = 1; i < XM_RXFILT_MAX; i++)
			sk_setfilt(sc_if, (caddr_t)&dummy, i);

		SK_XM_WRITE_4(sc_if, XM_MAR0, 0);
		SK_XM_WRITE_4(sc_if, XM_MAR2, 0);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		SK_YU_WRITE_2(sc_if, YUKON_MCAH1, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH2, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH3, 0);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH4, 0);
		break;
	}

	/* Now program new ones. */
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		i = 1;
		/* First find the tail of the list. */
		LIST_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_link.le_next == NULL)
				break;
		}
		/* Now traverse the list backwards. */
		for (; ifma != NULL && ifma != (void *)&ifp->if_multiaddrs;
			ifma = (struct ifmultiaddr *)ifma->ifma_link.le_prev) {
			caddr_t maddr;

			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;

			maddr = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);

			/*
			 * Program the first XM_RXFILT_MAX multicast groups
			 * into the perfect filter. For all others,
			 * use the hash table.
			 */
			if (SK_IS_GENESIS(sc) && i < XM_RXFILT_MAX) {
				sk_setfilt(sc_if, maddr, i);
				i++;
				continue;
			}

			switch(sc->sk_type) {
			case SK_GENESIS:
				h = sk_xmac_hash(maddr);
				break;
				
			case SK_YUKON:
			case SK_YUKON_LITE:
			case SK_YUKON_LP:
				h = sk_yukon_hash(maddr);
				break;
			}
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
		}
	}

	switch(sc->sk_type) {
	case SK_GENESIS:
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_USE_HASH|
			       XM_MODE_RX_USE_PERFECT);
		SK_XM_WRITE_4(sc_if, XM_MAR0, hashes[0]);
		SK_XM_WRITE_4(sc_if, XM_MAR2, hashes[1]);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		SK_YU_WRITE_2(sc_if, YUKON_MCAH1, hashes[0] & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH2, (hashes[0] >> 16) & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH3, hashes[1] & 0xffff);
		SK_YU_WRITE_2(sc_if, YUKON_MCAH4, (hashes[1] >> 16) & 0xffff);
		break;
	}
}

static void
sk_setpromisc(struct sk_if_softc *sc_if)
{
	struct sk_softc	*sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;

	switch(sc->sk_type) {
	case SK_GENESIS:
		if (ifp->if_flags & IFF_PROMISC)
			SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_PROMISC);
		else
			SK_XM_CLRBIT_4(sc_if, XM_MODE, XM_MODE_RX_PROMISC);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		if (ifp->if_flags & IFF_PROMISC) {
			SK_YU_CLRBIT_2(sc_if, YUKON_RCR,
			    YU_RCR_UFLEN | YU_RCR_MUFLEN);
		} else {
			SK_YU_SETBIT_2(sc_if, YUKON_RCR,
			    YU_RCR_UFLEN | YU_RCR_MUFLEN);
		}
		break;
	}
}

static int
sk_init_rx_ring(struct sk_if_softc *sc_if)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct sk_ring_data *rd = sc_if->sk_rdata;
	int i, nexti;

	bzero(rd->sk_rx_ring, sizeof(struct sk_rx_desc) * SK_RX_RING_CNT);

	for (i = 0; i < SK_RX_RING_CNT; i++) {
		cd->sk_rx_chain[i].sk_desc = &rd->sk_rx_ring[i];
		if (i == (SK_RX_RING_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		cd->sk_rx_chain[i].sk_next = &cd->sk_rx_chain[nexti];
		rd->sk_rx_ring[i].sk_next =
			htole32(SK_RX_RING_ADDR(sc_if, nexti));
		rd->sk_rx_ring[i].sk_csum1_start = htole16(ETHER_HDR_LEN);
		rd->sk_rx_ring[i].sk_csum2_start =
			htole16(ETHER_HDR_LEN + sizeof(struct ip));

		if (sk_newbuf(sc_if, &cd->sk_rx_chain[i], NULL, 1) == ENOBUFS) {
			if_printf(&sc_if->arpcom.ac_if,
				  "failed alloc of %dth mbuf\n", i);
			return (ENOBUFS);
		}
	}

	cd->sk_rx_prod = 0;
	cd->sk_rx_cons = 0;

	bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	return (0);
}

static int
sk_init_tx_ring(struct sk_if_softc *sc_if)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct sk_ring_data *rd = sc_if->sk_rdata;
	int i, nexti;

	bzero(rd->sk_tx_ring, sizeof(struct sk_tx_desc) * SK_TX_RING_CNT);

	for (i = 0; i < SK_TX_RING_CNT; i++) {
		cd->sk_tx_chain[i].sk_desc = &rd->sk_tx_ring[i];
		if (i == (SK_TX_RING_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		cd->sk_tx_chain[i].sk_next = &cd->sk_tx_chain[nexti];
		rd->sk_tx_ring[i].sk_next = htole32(SK_TX_RING_ADDR(sc_if, nexti));
	}

	sc_if->sk_cdata.sk_tx_prod = 0;
	sc_if->sk_cdata.sk_tx_cons = 0;
	sc_if->sk_cdata.sk_tx_cnt = 0;

	bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	return (0);
}

static int
sk_newbuf(struct sk_if_softc *sc_if, struct sk_chain *c, struct mbuf *m,
	  int wait)
{
	struct sk_jpool_entry *entry;
	struct mbuf *m_new = NULL;
	struct sk_rx_desc *r;

	if (m == NULL) {
		MGETHDR(m_new, wait ? MB_WAIT : MB_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return (ENOBUFS);
		
		/* Allocate the jumbo buffer */
		entry = sk_jalloc(sc_if);
		if (entry == NULL) {
			m_freem(m_new);
			DPRINTFN(1, ("%s jumbo allocation failed -- packet "
			    "dropped!\n", sc_if->arpcom.ac_if.if_xname));
			return (ENOBUFS);
		}

		m_new->m_ext.ext_arg = entry;
		m_new->m_ext.ext_buf = entry->buf;
		m_new->m_ext.ext_free = sk_jfree;
		m_new->m_ext.ext_ref = sk_jref;
		m_new->m_ext.ext_size = SK_JLEN;

		m_new->m_flags |= M_EXT;
	} else {
		/*
	 	 * We're re-using a previously allocated mbuf;
		 * be sure to re-init pointers and lengths to
		 * default values.
		 */
		KKASSERT(m->m_flags & M_EXT);
		entry = m->m_ext.ext_arg;
		m_new = m;
	}
	m_new->m_data = m_new->m_ext.ext_buf;
	m_new->m_len = m_new->m_pkthdr.len = m_new->m_ext.ext_size;

	/*
	 * Adjust alignment so packet payload begins on a
	 * longword boundary. Mandatory for Alpha, useful on
	 * x86 too.
	 */
	m_adj(m_new, ETHER_ALIGN);

	c->sk_mbuf = m_new;

	r = c->sk_desc;
	r->sk_data_lo = htole32(entry->paddr + ETHER_ALIGN);
	r->sk_ctl = htole32(SK_JLEN | SK_RXSTAT);

	return (0);
}

/*
 * Allocate a jumbo buffer.
 */
struct sk_jpool_entry *
sk_jalloc(struct sk_if_softc *sc_if)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct sk_jpool_entry *entry;

	lwkt_serialize_enter(&cd->sk_jpool_serializer);

	entry = SLIST_FIRST(&cd->sk_jpool_free_ent);
	if (entry != NULL) {
		SLIST_REMOVE_HEAD(&cd->sk_jpool_free_ent, entry_next);
		entry->inuse = 1;
	} else {
		if_printf(&sc_if->arpcom.ac_if,
			  "no free jumbo buffer\n");
	}

	lwkt_serialize_exit(&cd->sk_jpool_serializer);
	return entry;
}

/*
 * Release a jumbo buffer.
 */
void
sk_jfree(void *arg)
{
	struct sk_jpool_entry *entry = arg;
	struct sk_chain_data *cd = &entry->sc_if->sk_cdata;

	if (&cd->sk_jpool_ent[entry->slot] != entry)
		panic("%s: free wrong jumbo buffer\n", __func__);
	else if (entry->inuse == 0)
		panic("%s: jumbo buffer already freed\n", __func__);

	lwkt_serialize_enter(&cd->sk_jpool_serializer);

	atomic_subtract_int(&entry->inuse, 1);
	if (entry->inuse == 0)
		SLIST_INSERT_HEAD(&cd->sk_jpool_free_ent, entry, entry_next);

	lwkt_serialize_exit(&cd->sk_jpool_serializer);
}

static void
sk_jref(void *arg)
{
	struct sk_jpool_entry *entry = arg;
	struct sk_chain_data *cd = &entry->sc_if->sk_cdata;

	if (&cd->sk_jpool_ent[entry->slot] != entry)
		panic("%s: free wrong jumbo buffer\n", __func__);
	else if (entry->inuse == 0)
		panic("%s: jumbo buffer already freed\n", __func__);

	atomic_add_int(&entry->inuse, 1);
}

/*
 * Set media options.
 */
static int
sk_ifmedia_upd(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;
	struct mii_data *mii;

	mii = device_get_softc(sc_if->sk_miibus);
	sk_init(sc_if);
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void
sk_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sk_if_softc *sc_if;
	struct mii_data *mii;

	sc_if = ifp->if_softc;
	mii = device_get_softc(sc_if->sk_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
sk_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct sk_if_softc *sc_if = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch(command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > SK_JUMBO_MTU)
			error = EINVAL;
		else {
			ifp->if_mtu = ifr->ifr_mtu;
			ifp->if_flags &= ~IFF_RUNNING;
			sk_init(sc_if);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc_if->sk_if_flags)
				    & IFF_PROMISC) {
					sk_setpromisc(sc_if);
					sk_setmulti(sc_if);
				}
			} else
				sk_init(sc_if);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sk_stop(sc_if);
		}
		sc_if->sk_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		sk_setmulti(sc_if);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc_if->sk_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return(error);
}

/*
 * Probe for a SysKonnect GEnesis chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
skc_probe(device_t dev)
{
	const struct skc_type *t;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	/*
	 * Only attach to rev.2 of the Linksys EG1032 adapter.
	 * Rev.3 is supported by re(4).
	 */
	if (vid == PCI_VENDOR_LINKSYS &&
	    did == PCI_PRODUCT_LINKSYS_EG1032 &&
	    pci_get_subdevice(dev) != SUBDEVICEID_LINKSYS_EG1032_REV2)
		return ENXIO;

	for (t = skc_devs; t->skc_name != NULL; t++) {
		if (vid == t->skc_vid && did == t->skc_did) {
			device_set_desc(dev, t->skc_name);
			return 0;
		}
	}
	return ENXIO;
}

/*
 * Force the GEnesis into reset, then bring it out of reset.
 */
static void
sk_reset(struct sk_softc *sc)
{
	uint32_t imtimer_ticks;

	DPRINTFN(2, ("sk_reset\n"));

	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_RESET);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_RESET);
	if (SK_IS_YUKON(sc))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_SET);

	DELAY(1000);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_UNRESET);
	DELAY(2);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_UNRESET);
	if (SK_IS_YUKON(sc))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_CLEAR);

	DPRINTFN(2, ("sk_reset: sk_csr=%x\n", CSR_READ_2(sc, SK_CSR)));
	DPRINTFN(2, ("sk_reset: sk_link_ctrl=%x\n",
		     CSR_READ_2(sc, SK_LINK_CTRL)));

	if (SK_IS_GENESIS(sc)) {
		/* Configure packet arbiter */
		sk_win_write_2(sc, SK_PKTARB_CTL, SK_PKTARBCTL_UNRESET);
		sk_win_write_2(sc, SK_RXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_RXPA2_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA2_TINIT, SK_PKTARB_TIMEOUT);
	}

	/* Enable RAM interface */
	sk_win_write_4(sc, SK_RAMCTL, SK_RAMCTL_UNRESET);

	/*
	 * Configure interrupt moderation. The moderation timer
	 * defers interrupts specified in the interrupt moderation
	 * timer mask based on the timeout specified in the interrupt
	 * moderation timer init register. Each bit in the timer
	 * register represents one tick, so to specify a timeout in
	 * microseconds, we have to multiply by the correct number of
	 * ticks-per-microsecond.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		imtimer_ticks = SK_IMTIMER_TICKS_GENESIS;
		break;
	default:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON;
	}
	sk_win_write_4(sc, SK_IMTIMERINIT, SK_IM_USECS(100));
	sk_win_write_4(sc, SK_IMMR, SK_ISR_TX1_S_EOF|SK_ISR_TX2_S_EOF|
	    SK_ISR_RX1_EOF|SK_ISR_RX2_EOF);
	sk_win_write_1(sc, SK_IMTIMERCTL, SK_IMCTL_START);
}

static int
sk_probe(device_t dev)
{
	struct sk_softc *sc = device_get_softc(device_get_parent(dev));
	const char *revstr = "", *name = NULL;
	char devname[80];

	switch (sc->sk_type) {
	case SK_GENESIS:
		name = "SysKonnect GEnesis";
		break;
	case SK_YUKON:
		name = "Marvell Yukon";
		break;
	case SK_YUKON_LITE:
		name = "Marvell Yukon Lite";
		switch (sc->sk_rev) {
		case SK_YUKON_LITE_REV_A0:
			revstr = " rev.A0";
			break;
		case SK_YUKON_LITE_REV_A1:
			revstr = " rev.A1";
			break;
		case SK_YUKON_LITE_REV_A3:
			revstr = " rev.A3";
			break;
		}
		break;
	case SK_YUKON_LP:
		name = "Marvell Yukon LP";
		break;
	default:
		return ENXIO;
	}

	snprintf(devname, sizeof(devname), "%s%s (0x%x)",
		 name, revstr, sc->sk_rev);
	device_set_desc_copy(dev, devname);
	return 0;
}

/*
 * Each XMAC chip is attached as a separate logical IP interface.
 * Single port cards will have only one logical interface of course.
 */
static int
sk_attach(device_t dev)
{
	struct sk_softc *sc = device_get_softc(device_get_parent(dev));
	struct sk_if_softc *sc_if = device_get_softc(dev);
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	int i, error;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	sc_if->sk_port = *(int *)device_get_ivars(dev);
	KKASSERT(sc_if->sk_port == SK_PORT_A || sc_if->sk_port == SK_PORT_B);

	sc_if->sk_softc = sc;
	sc->sk_if[sc_if->sk_port] = sc_if;

	kfree(device_get_ivars(dev), M_DEVBUF);
	device_set_ivars(dev, NULL);

	if (sc_if->sk_port == SK_PORT_A)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR0;
	if (sc_if->sk_port == SK_PORT_B)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR1;

	DPRINTFN(2, ("begin sk_attach: port=%d\n", sc_if->sk_port));

	/*
	 * Get station address for this interface. Note that
	 * dual port cards actually come with three station
	 * addresses: one for each port, plus an extra. The
	 * extra one is used by the SysKonnect driver software
	 * as a 'virtual' station address for when both ports
	 * are operating in failover mode. Currently we don't
	 * use this extra address.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		/* XXX */
		sc_if->arpcom.ac_enaddr[i] =
		    sk_win_read_1(sc, SK_MAC0_0 + (sc_if->sk_port * 8) + i);
	}

	/*
	 * Set up RAM buffer addresses. The NIC will have a certain
	 * amount of SRAM on it, somewhere between 512K and 2MB. We
	 * need to divide this up a) between the transmitter and
 	 * receiver and b) between the two XMACs, if this is a
	 * dual port NIC. Our algorithm is to divide up the memory
	 * evenly so that everyone gets a fair share.
	 */
	if (sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC) {
		uint32_t chunk, val;

		chunk = sc->sk_ramsize / 2;
		val = sc->sk_rboff / sizeof(uint64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(uint64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(uint64_t));
		sc_if->sk_tx_ramend = val - 1;
	} else {
		uint32_t chunk, val;

		chunk = sc->sk_ramsize / 4;
		val = (sc->sk_rboff + (chunk * 2 * sc_if->sk_port)) /
		    sizeof(uint64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(uint64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(uint64_t));
		sc_if->sk_tx_ramend = val - 1;
	}

	DPRINTFN(2, ("sk_attach: rx_ramstart=%#x rx_ramend=%#x\n"
		     "           tx_ramstart=%#x tx_ramend=%#x\n",
		     sc_if->sk_rx_ramstart, sc_if->sk_rx_ramend,
		     sc_if->sk_tx_ramstart, sc_if->sk_tx_ramend));

	/* Read and save PHY type */
	sc_if->sk_phytype = sk_win_read_1(sc, SK_EPROM1) & 0xF;

	/* Set PHY address */
	if (SK_IS_GENESIS(sc)) {
		switch (sc_if->sk_phytype) {
		case SK_PHYTYPE_XMAC:
			sc_if->sk_phyaddr = SK_PHYADDR_XMAC;
			break;
		case SK_PHYTYPE_BCOM:
			sc_if->sk_phyaddr = SK_PHYADDR_BCOM;
			break;
		default:
			device_printf(dev, "unsupported PHY type: %d\n",
			    sc_if->sk_phytype);
			error = ENXIO;
			goto fail;
		}
	}

	if (SK_IS_YUKON(sc)) {
		if ((sc_if->sk_phytype < SK_PHYTYPE_MARV_COPPER &&
		    sc->sk_pmd != 'L' && sc->sk_pmd != 'S')) {
			/* not initialized, punt */
			sc_if->sk_phytype = SK_PHYTYPE_MARV_COPPER;
			sc->sk_coppertype = 1;
		}

		sc_if->sk_phyaddr = SK_PHYADDR_MARV;

		if (!(sc->sk_coppertype))
			sc_if->sk_phytype = SK_PHYTYPE_MARV_FIBER;
	}

	error = sk_dma_alloc(dev);
	if (error)
		goto fail;

	ifp->if_softc = sc_if;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sk_ioctl;
	ifp->if_start = sk_start;
	ifp->if_watchdog = sk_watchdog;
	ifp->if_init = sk_init;
	ifp->if_baudrate = 1000000000;
	ifq_set_maxlen(&ifp->if_snd, SK_TX_RING_CNT - 1);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Do miibus setup.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		break;
	default:
		device_printf(dev, "unknown device type %d\n", sc->sk_type);
		error = ENXIO;
		goto fail;
	}

 	DPRINTFN(2, ("sk_attach: 1\n"));

	error = mii_phy_probe(dev, &sc_if->sk_miibus,
			      sk_ifmedia_upd, sk_ifmedia_sts);
	if (error) {
		device_printf(dev, "no PHY found!\n");
		goto fail;
	}

	callout_init(&sc_if->sk_tick_timer);

	/*
	 * Call MI attach routines.
	 */
	ether_ifattach(ifp, sc_if->arpcom.ac_enaddr, &sc->sk_serializer);

	DPRINTFN(2, ("sk_attach: end\n"));
	return 0;
fail:
	sk_detach(dev);
	sc->sk_if[sc_if->sk_port] = NULL;
	return error;
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
skc_attach(device_t dev)
{
	struct sk_softc *sc = device_get_softc(dev);
	uint8_t skrs;
	int *port;
	int error;

	DPRINTFN(2, ("begin skc_attach\n"));

	lwkt_serialize_init(&sc->sk_serializer);

#ifndef BURN_BRIDGES
	/*
	 * Handle power management nonsense.
	 */
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t iobase, membase, irq;

		/* Save important PCI config data. */
		iobase = pci_read_config(dev, SK_PCI_LOIO, 4);
		membase = pci_read_config(dev, SK_PCI_LOMEM, 4);
		irq = pci_read_config(dev, SK_PCI_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
			      "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, SK_PCI_LOIO, iobase, 4);
		pci_write_config(dev, SK_PCI_LOMEM, membase, 4);
		pci_write_config(dev, SK_PCI_INTLINE, irq, 4);
	}
#endif	/* BURN_BRIDGES */

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	sc->sk_res_rid = SK_PCI_LOMEM;
	sc->sk_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					    &sc->sk_res_rid, RF_ACTIVE);
	if (sc->sk_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}
	sc->sk_btag = rman_get_bustag(sc->sk_res);
	sc->sk_bhandle = rman_get_bushandle(sc->sk_res);

	sc->sk_type = sk_win_read_1(sc, SK_CHIPVER);
	sc->sk_rev = (sk_win_read_1(sc, SK_CONFIG) >> 4);

	/* Bail out here if chip is not recognized */
	if (!SK_IS_GENESIS(sc) && !SK_IS_YUKON(sc)) {
		device_printf(dev, "unknown chip type: %d\n", sc->sk_type);
		error = ENXIO;
		goto fail;
	}

	DPRINTFN(2, ("skc_attach: allocate interrupt\n"));

	/* Allocate interrupt */
	sc->sk_irq_rid = 0;
	sc->sk_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sk_irq_rid,
					    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sk_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	sk_reset(sc);

	skrs = sk_win_read_1(sc, SK_EPROM0);
	if (SK_IS_GENESIS(sc)) {
		/* Read and save RAM size and RAMbuffer offset */
		switch(skrs) {
		case SK_RAMSIZE_512K_64:
			sc->sk_ramsize = 0x80000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_1024K_64:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_80000;
			break;
		case SK_RAMSIZE_1024K_128:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_2048K_128:
			sc->sk_ramsize = 0x200000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		default:
			device_printf(dev, "unknown ram size: %d\n", skrs);
			error = ENXIO;
			goto fail;
		}
	} else {
		if (skrs == 0x00)
			sc->sk_ramsize = 0x20000;
		else
			sc->sk_ramsize = skrs * (1<<12);
		sc->sk_rboff = SK_RBOFF_0;
	}

	DPRINTFN(2, ("skc_attach: ramsize=%d (%dk), rboff=%d\n",
		     sc->sk_ramsize, sc->sk_ramsize / 1024,
		     sc->sk_rboff));

	/* Read and save physical media type */
	sc->sk_pmd = sk_win_read_1(sc, SK_PMDTYPE);

	if (sc->sk_pmd == 'T' || sc->sk_pmd == '1')
		sc->sk_coppertype = 1;
	else
		sc->sk_coppertype = 0;

	/* Yukon Lite Rev A0 needs special test, from sk98lin driver */
	if (sc->sk_type == SK_YUKON || sc->sk_type == SK_YUKON_LP) {
		uint32_t flashaddr;
		uint8_t testbyte;

		flashaddr = sk_win_read_4(sc, SK_EP_ADDR);

		/* Test Flash-Address Register */
		sk_win_write_1(sc, SK_EP_ADDR+3, 0xff);
		testbyte = sk_win_read_1(sc, SK_EP_ADDR+3);

		if (testbyte != 0) {
			/* This is a Yukon Lite Rev A0 */
			sc->sk_type = SK_YUKON_LITE;
			sc->sk_rev = SK_YUKON_LITE_REV_A0;
			/* Restore Flash-Address Register */
			sk_win_write_4(sc, SK_EP_ADDR, flashaddr);
		}
	}

	sc->sk_devs[SK_PORT_A] = device_add_child(dev, "sk", -1);
	port = kmalloc(sizeof(*port), M_DEVBUF, M_WAITOK);
	*port = SK_PORT_A;
	device_set_ivars(sc->sk_devs[SK_PORT_A], port);

	if (!(sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC)) {
		sc->sk_devs[SK_PORT_B] = device_add_child(dev, "sk", -1);
		port = kmalloc(sizeof(*port), M_DEVBUF, M_WAITOK);
		*port = SK_PORT_B;
		device_set_ivars(sc->sk_devs[SK_PORT_B], port);
	}

	/* Turn on the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_ON);

	bus_generic_attach(dev);

	error = bus_setup_intr(dev, sc->sk_irq, INTR_NETSAFE, sk_intr, sc,
			       &sc->sk_intrhand, &sc->sk_serializer);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}
	return 0;
fail:
	skc_detach(dev);
	return error;
}

static int
sk_detach(device_t dev)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);
	struct ifnet *ifp = &sc_if->arpcom.ac_if;

	if (device_is_attached(dev))
		ether_ifdetach(ifp);

	bus_generic_detach(dev);
	if (sc_if->sk_miibus != NULL)
		device_delete_child(dev, sc_if->sk_miibus);

	sk_dma_free(dev);
	return 0;
}

static int
skc_detach(device_t dev)
{
	struct sk_softc *sc = device_get_softc(dev);
	int *port;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(&sc->sk_serializer);

		if (sc->sk_if[SK_PORT_A] != NULL)
			sk_stop(sc->sk_if[SK_PORT_A]);
		if (sc->sk_if[SK_PORT_B] != NULL)
			sk_stop(sc->sk_if[SK_PORT_B]);

		bus_teardown_intr(dev, sc->sk_irq, sc->sk_intrhand);

		lwkt_serialize_exit(&sc->sk_serializer);
	}

	bus_generic_detach(dev);
	if (sc->sk_devs[SK_PORT_A] != NULL) {
		port = device_get_ivars(sc->sk_devs[SK_PORT_A]);
		if (port != NULL) {
			kfree(port, M_DEVBUF);
			device_set_ivars(sc->sk_devs[SK_PORT_A], NULL);
		}
		device_delete_child(dev, sc->sk_devs[SK_PORT_A]);
	}
	if (sc->sk_devs[SK_PORT_B] != NULL) {
		port = device_get_ivars(sc->sk_devs[SK_PORT_B]);
		if (port != NULL) {
			kfree(port, M_DEVBUF);
			device_set_ivars(sc->sk_devs[SK_PORT_B], NULL);
		}
		device_delete_child(dev, sc->sk_devs[SK_PORT_B]);
	}

	if (sc->sk_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sk_irq_rid,
				     sc->sk_irq);
	}
	if (sc->sk_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sk_res_rid,
				     sc->sk_res);
	}

	return 0;
}

static int
sk_encap(struct sk_if_softc *sc_if, struct mbuf *m_head, uint32_t *txidx)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct sk_ring_data *rd = sc_if->sk_rdata;
	struct sk_tx_desc *f = NULL;
	uint32_t frag, cur, sk_ctl;
	struct sk_dma_ctx ctx;
	bus_dma_segment_t segs[SK_NTXSEG];
	bus_dmamap_t map;
	int i, error;

	DPRINTFN(2, ("sk_encap\n"));

	cur = frag = *txidx;

#ifdef SK_DEBUG
	if (skdebug >= 2)
		sk_dump_mbuf(m_head);
#endif

	map = cd->sk_tx_dmap[*txidx];

	/*
	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
	 * of fragments or hit the end of the mbuf chain.
	 */
	ctx.nsegs = SK_NTXSEG;
	ctx.segs = segs;
	error = bus_dmamap_load_mbuf(cd->sk_tx_dtag, map, m_head,
				     sk_buf_dma_addr, &ctx, BUS_DMA_NOWAIT);
	if (error) {
		if_printf(&sc_if->arpcom.ac_if, "could not map TX mbuf\n");
		return ENOBUFS;
	}

	if ((SK_TX_RING_CNT - (cd->sk_tx_cnt + ctx.nsegs)) < 2) {
		bus_dmamap_unload(cd->sk_tx_dtag, map);
		DPRINTFN(2, ("sk_encap: too few descriptors free\n"));
		return ENOBUFS;
	}

	DPRINTFN(2, ("sk_encap: nsegs=%d\n", ctx.nsegs));

	/* Sync the DMA map. */
	bus_dmamap_sync(cd->sk_tx_dtag, map, BUS_DMASYNC_PREWRITE);

	for (i = 0; i < ctx.nsegs; i++) {
		f = &rd->sk_tx_ring[frag];
		f->sk_data_lo = htole32(segs[i].ds_addr);
		sk_ctl = segs[i].ds_len | SK_OPCODE_DEFAULT;
		if (i == 0)
			sk_ctl |= SK_TXCTL_FIRSTFRAG;
		else
			sk_ctl |= SK_TXCTL_OWN;
		f->sk_ctl = htole32(sk_ctl);
		cur = frag;
		SK_INC(frag, SK_TX_RING_CNT);
	}

	cd->sk_tx_chain[cur].sk_mbuf = m_head;
	/* Switch DMA map */
	cd->sk_tx_dmap[*txidx] = cd->sk_tx_dmap[cur];
	cd->sk_tx_dmap[cur] = map;

	rd->sk_tx_ring[cur].sk_ctl |=
		htole32(SK_TXCTL_LASTFRAG|SK_TXCTL_EOF_INTR);
	rd->sk_tx_ring[*txidx].sk_ctl |= htole32(SK_TXCTL_OWN);

	/* Sync first descriptor to hand it off */
	bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
			BUS_DMASYNC_PREWRITE);

	sc_if->sk_cdata.sk_tx_cnt += ctx.nsegs;

#ifdef SK_DEBUG
	if (skdebug >= 2) {
		struct sk_tx_desc *desc;
		uint32_t idx;

		for (idx = *txidx; idx != frag; SK_INC(idx, SK_TX_RING_CNT)) {
			desc = &sc_if->sk_rdata->sk_tx_ring[idx];
			sk_dump_txdesc(desc, idx);
		}
	}
#endif

	*txidx = frag;

	DPRINTFN(2, ("sk_encap: completed successfully\n"));

	return (0);
}

static void
sk_start(struct ifnet *ifp)
{
        struct sk_if_softc *sc_if = ifp->if_softc;
        struct sk_softc *sc = sc_if->sk_softc;
        struct mbuf *m_head = NULL;
	uint32_t idx = sc_if->sk_cdata.sk_tx_prod;
	int pkts = 0;

	DPRINTFN(2, ("sk_start\n"));

	while (sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf == NULL) {
		m_head = ifq_poll(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (sk_encap(sc_if, m_head, &idx)) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		/* now we are committed to transmit the packet */
		ifq_dequeue(&ifp->if_snd, m_head);
		pkts++;

		BPF_MTAP(ifp, m_head);
	}
	if (pkts == 0)
		return;

	/* Transmit */
	if (idx != sc_if->sk_cdata.sk_tx_prod) {
		sc_if->sk_cdata.sk_tx_prod = idx;
		CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}
}

static void
sk_watchdog(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);
	/*
	 * Reclaim first as there is a possibility of losing Tx completion
	 * interrupts.
	 */
	sk_txeof(sc_if);
	if (sc_if->sk_cdata.sk_tx_cnt != 0) {
		if_printf(&sc_if->arpcom.ac_if, "watchdog timeout\n");
		ifp->if_oerrors++;
		ifp->if_flags &= ~IFF_RUNNING;
		sk_init(sc_if);
	}
}

static void
skc_shutdown(device_t dev)
{
	struct sk_softc *sc = device_get_softc(dev);

	DPRINTFN(2, ("sk_shutdown\n"));

	lwkt_serialize_enter(&sc->sk_serializer);

	/* Turn off the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_OFF);

	/*
	 * Reset the GEnesis controller. Doing this should also
	 * assert the resets on the attached XMAC(s).
	 */
	sk_reset(sc);

	lwkt_serialize_exit(&sc->sk_serializer);
}

static __inline int
sk_rxvalid(struct sk_softc *sc, uint32_t stat, uint32_t len)
{
	if (sc->sk_type == SK_GENESIS) {
		if ((stat & XM_RXSTAT_ERRFRAME) == XM_RXSTAT_ERRFRAME ||
		    XM_RXSTAT_BYTES(stat) != len)
			return (0);
	} else {
		if ((stat & (YU_RXSTAT_CRCERR | YU_RXSTAT_LONGERR |
		    YU_RXSTAT_MIIERR | YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC |
		    YU_RXSTAT_JABBER)) != 0 ||
		    (stat & YU_RXSTAT_RXOK) != YU_RXSTAT_RXOK ||
		    YU_RXSTAT_BYTES(stat) != len)
			return (0);
	}

	return (1);
}

static void
sk_rxeof(struct sk_if_softc *sc_if)
{
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct sk_ring_data *rd = sc_if->sk_rdata;
	int i, reap;

	DPRINTFN(2, ("sk_rxeof\n"));

	i = cd->sk_rx_prod;

	bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
			BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(cd->sk_jpool_dtag, cd->sk_jpool_dmap,
			BUS_DMASYNC_POSTREAD);

	reap = 0;
	for (;;) {
		struct sk_chain *cur_rx;
		struct sk_rx_desc *cur_desc;
		uint32_t rxstat, sk_ctl;
		uint16_t csum1, csum2;
		int cur, total_len;
		struct mbuf *m;

		cur = i;

		sk_ctl = le32toh(rd->sk_rx_ring[i].sk_ctl);
		if ((sk_ctl & SK_RXCTL_OWN) != 0) {
			/* Invalidate the descriptor -- it's not ready yet */
			cd->sk_rx_prod = i;
			break;
		}

		cur_rx = &cd->sk_rx_chain[cur];
		cur_desc = &rd->sk_rx_ring[cur];

		rxstat = le32toh(cur_desc->sk_xmac_rxstat);
		m = cur_rx->sk_mbuf;
		cur_rx->sk_mbuf = NULL;
		total_len = SK_RXBYTES(le32toh(cur_desc->sk_ctl));

		csum1 = le16toh(rd->sk_rx_ring[i].sk_csum1);
		csum2 = le16toh(rd->sk_rx_ring[i].sk_csum2);

		SK_INC(i, SK_RX_RING_CNT);

		if ((sk_ctl & (SK_RXCTL_STATUS_VALID | SK_RXCTL_FIRSTFRAG |
		    SK_RXCTL_LASTFRAG)) != (SK_RXCTL_STATUS_VALID |
		    SK_RXCTL_FIRSTFRAG | SK_RXCTL_LASTFRAG) ||
		    total_len < SK_MIN_FRAMELEN ||
		    total_len > SK_JUMBO_FRAMELEN ||
		    sk_rxvalid(sc, rxstat, total_len) == 0) {
			ifp->if_ierrors++;
			sk_newbuf(sc_if, cur_rx, m, 0);
			continue;
		}

		/*
		 * Try to allocate a new jumbo buffer. If that
		 * fails, copy the packet to mbufs and put the
		 * jumbo buffer back in the ring so it can be
		 * re-used. If allocating mbufs fails, then we
		 * have to drop the packet.
		 */
		if (sk_newbuf(sc_if, cur_rx, NULL, 0) == ENOBUFS) {
			struct mbuf *m0;

			m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
			    total_len + ETHER_ALIGN, 0, ifp, NULL);
			sk_newbuf(sc_if, cur_rx, m, 0);
			if (m0 == NULL) {
				ifp->if_ierrors++;
				continue;
			}
			m_adj(m0, ETHER_ALIGN);
			m = m0;
		} else {
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = total_len;
		}

#ifdef SK_RXCSUM
		sk_rxcsum(ifp, m, csum1, csum2);
#endif

		reap = 1;
		ifp->if_ipackets++;
		ifp->if_input(ifp, m);
	}

	if (reap) {
		bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
				BUS_DMASYNC_PREWRITE);
	}
}

#ifdef SK_RXCSUM
static void
sk_rxcsum(struct ifnet *ifp, struct mbuf *m,
	  const uint16_t csum1, const uint16_t csum2)
{
	struct ether_header *eh;
	struct ip *ip;
	uint8_t *pp;
	int hlen, len, plen;
	uint16_t iph_csum, ipo_csum, ipd_csum, csum;

	pp = mtod(m, uint8_t *);
	plen = m->m_pkthdr.len;
	if (plen < sizeof(*eh))
		return;
	eh = (struct ether_header *)pp;
	iph_csum = in_addword(csum1, (~csum2 & 0xffff));

	if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
		uint16_t *xp = (uint16_t *)pp;

		xp = (uint16_t *)pp;
		if (xp[1] != htons(ETHERTYPE_IP))
			return;
		iph_csum = in_addword(iph_csum, (~xp[0] & 0xffff));
		iph_csum = in_addword(iph_csum, (~xp[1] & 0xffff));
		xp = (uint16_t *)(pp + sizeof(struct ip));
		iph_csum = in_addword(iph_csum, xp[0]);
		iph_csum = in_addword(iph_csum, xp[1]);
		pp += EVL_ENCAPLEN;
	} else if (eh->ether_type != htons(ETHERTYPE_IP)) {
		return;
	}

	pp += sizeof(*eh);
	plen -= sizeof(*eh);

	ip = (struct ip *)pp;

	if (ip->ip_v != IPVERSION)
		return;

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip))
		return;
	if (hlen > ntohs(ip->ip_len))
		return;

	/* Don't deal with truncated or padded packets. */
	if (plen != ntohs(ip->ip_len))
		return;

	len = hlen - sizeof(struct ip);
	if (len > 0) {
		uint16_t *p;

		p = (uint16_t *)(ip + 1);
		ipo_csum = 0;
		for (ipo_csum = 0; len > 0; len -= sizeof(*p), p++)
			ipo_csum = in_addword(ipo_csum, *p);
		iph_csum = in_addword(iph_csum, ipo_csum);
		ipd_csum = in_addword(csum2, (~ipo_csum & 0xffff));
	} else {
		ipd_csum = csum2;
	}

	if (iph_csum != 0xffff)
		return;
	m->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID;

	if (ip->ip_off & htons(IP_MF | IP_OFFMASK))
		return;                 /* ip frag, we're done for now */

	pp += hlen;

	/* Only know checksum protocol for udp/tcp */
	if (ip->ip_p == IPPROTO_UDP) {
		struct udphdr *uh = (struct udphdr *)pp;

		if (uh->uh_sum == 0)    /* udp with no checksum */
			return;
	} else if (ip->ip_p != IPPROTO_TCP) {
		return;
	}

	csum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
	    htonl(ntohs(ip->ip_len) - hlen + ip->ip_p) + ipd_csum);
	if (csum == 0xffff) {
		m->m_pkthdr.csum_data = csum;
		m->m_pkthdr.csum_flags |= (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
	}
}
#endif

static void
sk_txeof(struct sk_if_softc *sc_if)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	uint32_t idx;
	int reap = 0;

	DPRINTFN(2, ("sk_txeof\n"));

	bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
			BUS_DMASYNC_POSTREAD);

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	idx = cd->sk_tx_cons;
	while (idx != cd->sk_tx_prod) {
		struct sk_tx_desc *cur_tx;
		uint32_t sk_ctl;

		cur_tx = &sc_if->sk_rdata->sk_tx_ring[idx];
		sk_ctl = le32toh(cur_tx->sk_ctl);
#ifdef SK_DEBUG
		if (skdebug >= 2)
			sk_dump_txdesc(cur_tx, idx);
#endif
		if (sk_ctl & SK_TXCTL_OWN)
			break;
		if (sk_ctl & SK_TXCTL_LASTFRAG)
			ifp->if_opackets++;
		if (cd->sk_tx_chain[idx].sk_mbuf != NULL) {
			bus_dmamap_unload(cd->sk_tx_dtag, cd->sk_tx_dmap[idx]);
			m_freem(cd->sk_tx_chain[idx].sk_mbuf);
			cd->sk_tx_chain[idx].sk_mbuf = NULL;
		}
		sc_if->sk_cdata.sk_tx_cnt--;
		reap = 1;
		SK_INC(idx, SK_TX_RING_CNT);
	}
	ifp->if_timer = sc_if->sk_cdata.sk_tx_cnt > 0 ? 5 : 0;

	if (sc_if->sk_cdata.sk_tx_cnt < SK_TX_RING_CNT - 2)
		ifp->if_flags &= ~IFF_OACTIVE;

	sc_if->sk_cdata.sk_tx_cons = idx;

	if (reap) {
		bus_dmamap_sync(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
				BUS_DMASYNC_PREWRITE);
	}
}

static void
sk_tick(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc_if->sk_miibus);
	int i;

	DPRINTFN(2, ("sk_tick\n"));

	lwkt_serialize_enter(ifp->if_serializer);

	if ((ifp->if_flags & IFF_UP) == 0) {
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		sk_intr_bcom(sc_if);
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	/*
	 * According to SysKonnect, the correct way to verify that
	 * the link has come back up is to poll bit 0 of the GPIO
	 * register three times. This pin has the signal from the
	 * link sync pin connected to it; if we read the same link
	 * state 3 times in a row, we know the link is up.
	 */
	for (i = 0; i < 3; i++) {
		if (SK_XM_READ_2(sc_if, XM_GPIO) & XM_GPIO_GP0_SET)
			break;
	}

	if (i != 3) {
		callout_reset(&sc_if->sk_tick_timer, hz, sk_tick, sc_if);
		lwkt_serialize_exit(ifp->if_serializer);
		return;
	}

	/* Turn the GP0 interrupt back on. */
	SK_XM_CLRBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
	SK_XM_READ_2(sc_if, XM_ISR);
	mii_tick(mii);
	callout_stop(&sc_if->sk_tick_timer);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
sk_yukon_tick(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;  
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc_if->sk_miibus);

	lwkt_serialize_enter(ifp->if_serializer);
	mii_tick(mii);
	callout_reset(&sc_if->sk_tick_timer, hz, sk_yukon_tick, sc_if);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
sk_intr_bcom(struct sk_if_softc *sc_if)
{
	struct mii_data *mii = device_get_softc(sc_if->sk_miibus);
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	int status;

	DPRINTFN(2, ("sk_intr_bcom\n"));

	SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	/*
	 * Read the PHY interrupt register to make sure
	 * we clear any pending interrupts.
	 */
	status = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, BRGPHY_MII_ISR);

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		sk_init_xmac(sc_if);
		return;
	}

	if (status & (BRGPHY_ISR_LNK_CHG|BRGPHY_ISR_AN_PR)) {
		int lstat;

		lstat = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_AUXSTS);

		if (!(lstat & BRGPHY_AUXSTS_LINK) && sc_if->sk_link) {
			mii_mediachg(mii);
			/* Turn off the link LED. */
			SK_IF_WRITE_1(sc_if, 0,
			    SK_LINKLED1_CTL, SK_LINKLED_OFF);
			sc_if->sk_link = 0;
		} else if (status & BRGPHY_ISR_LNK_CHG) {
			sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
			    BRGPHY_MII_IMR, 0xFF00);
			mii_tick(mii);
			sc_if->sk_link = 1;
			/* Turn on the link LED. */
			SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			    SK_LINKLED_ON|SK_LINKLED_LINKSYNC_OFF|
			    SK_LINKLED_BLINK_OFF);
		} else {
			mii_tick(mii);
			callout_reset(&sc_if->sk_tick_timer, hz,
				      sk_tick, sc_if);
		}
	}

	SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
}

static void
sk_intr_xmac(struct sk_if_softc *sc_if)
{
	uint16_t status;

	status = SK_XM_READ_2(sc_if, XM_ISR);
	DPRINTFN(2, ("sk_intr_xmac\n"));

	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC &&
	    (status & (XM_ISR_GP0_SET | XM_ISR_AUTONEG_DONE))) {
		if (status & XM_ISR_GP0_SET)
			SK_XM_SETBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);

		callout_reset(&sc_if->sk_tick_timer, hz,
			      sk_tick, sc_if);
	}

	if (status & XM_IMR_TX_UNDERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_TXFIFO);

	if (status & XM_IMR_RX_OVERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_RXFIFO);
}

static void
sk_intr_yukon(struct sk_if_softc *sc_if)
{
	uint8_t status;

	status = SK_IF_READ_1(sc_if, 0, SK_GMAC_ISR);
	/* RX overrun */
	if ((status & SK_GMAC_INT_RX_OVER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST,
		    SK_RFCTL_RX_FIFO_OVER);
	}
	/* TX underrun */
	if ((status & SK_GMAC_INT_TX_UNDER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST,
		    SK_TFCTL_TX_FIFO_UNDER);
	}

	DPRINTFN(2, ("sk_intr_yukon status=%#x\n", status));
}

static void
sk_intr(void *xsc)
{
	struct sk_softc *sc = xsc;
	struct sk_if_softc *sc_if0 = sc->sk_if[SK_PORT_A];
	struct sk_if_softc *sc_if1 = sc->sk_if[SK_PORT_B];
	struct ifnet *ifp0 = NULL, *ifp1 = NULL;
	uint32_t status;

	ASSERT_SERIALIZED(&sc->sk_serializer);

	status = CSR_READ_4(sc, SK_ISSR);
	if (status == 0 || status == 0xffffffff)
		return;

	if (sc_if0 != NULL)
		ifp0 = &sc_if0->arpcom.ac_if;
	if (sc_if1 != NULL)
		ifp1 = &sc_if1->arpcom.ac_if;

	for (; (status &= sc->sk_intrmask) != 0;) {
		/* Handle receive interrupts first. */
		if (sc_if0 && (status & SK_ISR_RX1_EOF)) {
			sk_rxeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR0,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}
		if (sc_if1 && (status & SK_ISR_RX2_EOF)) {
			sk_rxeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR1,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}

		/* Then transmit interrupts. */
		if (sc_if0 && (status & SK_ISR_TX1_S_EOF)) {
			sk_txeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR0,
			    SK_TXBMU_CLR_IRQ_EOF);
		}
		if (sc_if1 && (status & SK_ISR_TX2_S_EOF)) {
			sk_txeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR1,
			    SK_TXBMU_CLR_IRQ_EOF);
		}

		/* Then MAC interrupts. */
		if (sc_if0 && (status & SK_ISR_MAC1) &&
		    (ifp0->if_flags & IFF_RUNNING)) {
			if (SK_IS_GENESIS(sc))
				sk_intr_xmac(sc_if0);
			else
				sk_intr_yukon(sc_if0);
		}

		if (sc_if1 && (status & SK_ISR_MAC2) &&
		    (ifp1->if_flags & IFF_RUNNING)) {
			if (SK_IS_GENESIS(sc))
				sk_intr_xmac(sc_if1);
			else
				sk_intr_yukon(sc_if1);
		}

		if (status & SK_ISR_EXTERNAL_REG) {
			if (sc_if0 != NULL &&
			    sc_if0->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if0);

			if (sc_if1 != NULL &&
			    sc_if1->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if1);
		}
		status = CSR_READ_4(sc, SK_ISSR);
	}

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	if (ifp0 != NULL && !ifq_is_empty(&ifp0->if_snd))
		sk_start(ifp0);
	if (ifp1 != NULL && !ifq_is_empty(&ifp1->if_snd))
		sk_start(ifp1);
}

static void
sk_init_xmac(struct sk_if_softc	*sc_if)
{
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	static const struct sk_bcom_hack bhack[] = {
	{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 }, { 0x17, 0x0013 },
	{ 0x15, 0x0404 }, { 0x17, 0x8006 }, { 0x15, 0x0132 }, { 0x17, 0x8006 },
	{ 0x15, 0x0232 }, { 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
	{ 0, 0 } };

	DPRINTFN(2, ("sk_init_xmac\n"));

	/* Unreset the XMAC. */
	SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_UNRESET);
	DELAY(1000);

	/* Reset the XMAC's internal state. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);

	/* Save the XMAC II revision */
	sc_if->sk_xmac_rev = XM_XMAC_REV(SK_XM_READ_4(sc_if, XM_DEVID));

	/*
	 * Perform additional initialization for external PHYs,
	 * namely for the 1000baseTX cards that use the XMAC's
	 * GMII mode.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		int i = 0;
		uint32_t val;

		/* Take PHY out of reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A)
			val |= SK_GPIO_DIR0|SK_GPIO_DAT0;
		else
			val |= SK_GPIO_DIR2|SK_GPIO_DAT2;
		sk_win_write_4(sc, SK_GPIO, val);

		/* Enable GMII mode on the XMAC. */
		SK_XM_SETBIT_2(sc_if, XM_HWCFG, XM_HWCFG_GMIIMODE);

		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_BMCR, BRGPHY_BMCR_RESET);
		DELAY(10000);
		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_IMR, 0xFFF0);

		/*
		 * Early versions of the BCM5400 apparently have
		 * a bug that requires them to have their reserved
		 * registers initialized to some magic values. I don't
		 * know what the numbers do, I'm just the messenger.
		 */
		if (sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, 0x03)
		    == 0x6041) {
			while(bhack[i].reg) {
				sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
				    bhack[i].reg, bhack[i].val);
				i++;
			}
		}
	}

	/* Set station address */
	SK_XM_WRITE_2(sc_if, XM_PAR0,
	    *(uint16_t *)(&sc_if->arpcom.ac_enaddr[0]));
	SK_XM_WRITE_2(sc_if, XM_PAR1,
	    *(uint16_t *)(&sc_if->arpcom.ac_enaddr[2]));
	SK_XM_WRITE_2(sc_if, XM_PAR2,
	    *(uint16_t *)(&sc_if->arpcom.ac_enaddr[4]));
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_USE_STATION);

	if (ifp->if_flags & IFF_BROADCAST)
		SK_XM_CLRBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);
	else
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);

	/* We don't need the FCS appended to the packet. */
	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_STRIPFCS);

	/* We want short frames padded to 60 bytes. */
	SK_XM_SETBIT_2(sc_if, XM_TXCMD, XM_TXCMD_AUTOPAD);

	/*
	 * Enable the reception of all error frames. This is
	 * a necessary evil due to the design of the XMAC. The
	 * XMAC's receive FIFO is only 8K in size, however jumbo
	 * frames can be up to 9000 bytes in length. When bad
	 * frame filtering is enabled, the XMAC's RX FIFO operates
	 * in 'store and forward' mode. For this to work, the
	 * entire frame has to fit into the FIFO, but that means
	 * that jumbo frames larger than 8192 bytes will be
	 * truncated. Disabling all bad frame filtering causes
	 * the RX FIFO to operate in streaming mode, in which
	 * case the XMAC will start transfering frames out of the
	 * RX FIFO as soon as the FIFO threshold is reached.
	 */
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_BADFRAMES|
	    XM_MODE_RX_GIANTS|XM_MODE_RX_RUNTS|XM_MODE_RX_CRCERRS|
	    XM_MODE_RX_INRANGELEN);

	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);

	/*
	 * Bump up the transmit threshold. This helps hold off transmit
	 * underruns when we're blasting traffic from both ports at once.
	 */
	SK_XM_WRITE_2(sc_if, XM_TX_REQTHRESH, SK_XM_TX_FIFOTHRESH);

	/* Set promiscuous mode */
	sk_setpromisc(sc_if);

	/* Set multicast filter */
	sk_setmulti(sc_if);

	/* Clear and enable interrupts */
	SK_XM_READ_2(sc_if, XM_ISR);
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC)
		SK_XM_WRITE_2(sc_if, XM_IMR, XM_INTRS);
	else
		SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Configure MAC arbiter */
	switch(sc_if->sk_xmac_rev) {
	case XM_XMAC_REV_B2:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	case XM_XMAC_REV_C1:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	default:
		break;
	}
	sk_win_write_2(sc, SK_MACARB_CTL,
	    SK_MACARBCTL_UNRESET|SK_MACARBCTL_FASTOE_OFF);

	sc_if->sk_link = 1;
}

static void
sk_init_yukon(struct sk_if_softc *sc_if)
{
	uint32_t phy, v;
	uint16_t reg;
	struct sk_softc *sc;
	int i;

	sc = sc_if->sk_softc;

	DPRINTFN(2, ("sk_init_yukon: start: sk_csr=%#x\n",
		     CSR_READ_4(sc_if->sk_softc, SK_CSR)));

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, set PHY reset.
		 * Otherwise it will not correctly take chip out of
		 * powerdown (coma)
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9 | SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	DPRINTFN(6, ("sk_init_yukon: 1\n"));

	/* GMAC and GPHY Reset */
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_SET);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	DELAY(1000);

	DPRINTFN(6, ("sk_init_yukon: 2\n"));

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, clear PHY reset
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9;
		v &= ~SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	phy = SK_GPHY_INT_POL_HI | SK_GPHY_DIS_FC | SK_GPHY_DIS_SLEEP |
		SK_GPHY_ENA_XC | SK_GPHY_ANEG_ALL | SK_GPHY_ENA_PAUSE;

	if (sc->sk_coppertype)
		phy |= SK_GPHY_COPPER;
	else
		phy |= SK_GPHY_FIBER;

	DPRINTFN(3, ("sk_init_yukon: phy=%#x\n", phy));

	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_LOOP_OFF |
		      SK_GMAC_PAUSE_ON | SK_GMAC_RESET_CLEAR);

	DPRINTFN(3, ("sk_init_yukon: gmac_ctrl=%#x\n",
		     SK_IF_READ_4(sc_if, 0, SK_GMAC_CTRL)));

	DPRINTFN(6, ("sk_init_yukon: 3\n"));

	/* unused read of the interrupt source register */
	DPRINTFN(6, ("sk_init_yukon: 4\n"));
	SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	DPRINTFN(6, ("sk_init_yukon: 4a\n"));
	reg = SK_YU_READ_2(sc_if, YUKON_PAR);
	DPRINTFN(6, ("sk_init_yukon: YUKON_PAR=%#x\n", reg));

	/* MIB Counter Clear Mode set */
	reg |= YU_PAR_MIB_CLR;
	DPRINTFN(6, ("sk_init_yukon: YUKON_PAR=%#x\n", reg));
	DPRINTFN(6, ("sk_init_yukon: 4b\n"));
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* MIB Counter Clear Mode clear */
	DPRINTFN(6, ("sk_init_yukon: 5\n"));
	reg &= ~YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* receive control reg */
	DPRINTFN(6, ("sk_init_yukon: 7\n"));
	SK_YU_WRITE_2(sc_if, YUKON_RCR, YU_RCR_CRCR);

	/* transmit parameter register */
	DPRINTFN(6, ("sk_init_yukon: 8\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TPR, YU_TPR_JAM_LEN(0x3) |
		      YU_TPR_JAM_IPG(0xb) | YU_TPR_JAM2DATA_IPG(0x1a) );

	/* serial mode register */
	DPRINTFN(6, ("sk_init_yukon: 9\n"));
	SK_YU_WRITE_2(sc_if, YUKON_SMR, YU_SMR_DATA_BLIND(0x1c) |
		      YU_SMR_MFL_VLAN | YU_SMR_MFL_JUMBO |
		      YU_SMR_IPG_DATA(0x1e));

	DPRINTFN(6, ("sk_init_yukon: 10\n"));
	/* Setup Yukon's address */
	for (i = 0; i < 3; i++) {
		/* Write Source Address 1 (unicast filter) */
		SK_YU_WRITE_2(sc_if, YUKON_SAL1 + i * 4, 
			      sc_if->arpcom.ac_enaddr[i * 2] |
			      sc_if->arpcom.ac_enaddr[i * 2 + 1] << 8);
	}

	for (i = 0; i < 3; i++) {
		reg = sk_win_read_2(sc_if->sk_softc,
				    SK_MAC1_0 + i * 2 + sc_if->sk_port * 8);
		SK_YU_WRITE_2(sc_if, YUKON_SAL2 + i * 4, reg);
	}

	/* Set promiscuous mode */
	sk_setpromisc(sc_if);

	/* Set multicast filter */
	DPRINTFN(6, ("sk_init_yukon: 11\n"));
	sk_setmulti(sc_if);

	/* enable interrupt mask for counter overflows */
	DPRINTFN(6, ("sk_init_yukon: 12\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_RIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_TRIMR, 0);

	/* Configure RX MAC FIFO Flush Mask */
	v = YU_RXSTAT_FOFL | YU_RXSTAT_CRCERR | YU_RXSTAT_MIIERR |
	    YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC | YU_RXSTAT_RUNT |
	    YU_RXSTAT_JABBER;
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_MASK, v);

	/* Disable RX MAC FIFO Flush for YUKON-Lite Rev. A0 only */
	if (sc->sk_type == SK_YUKON_LITE && sc->sk_rev == SK_YUKON_LITE_REV_A0)
		v = SK_TFCTL_OPERATION_ON;
	else
		v = SK_TFCTL_OPERATION_ON | SK_RFCTL_FIFO_FLUSH_ON;
	/* Configure RX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_CTRL_TEST, v);

	/* Increase flush threshould to 64 bytes */
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_THRESHOLD,
	    SK_RFCTL_FIFO_THRESHOLD + 1);

	/* Configure TX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_OPERATION_ON);

	DPRINTFN(6, ("sk_init_yukon: end\n"));
}

/*
 * Note that to properly initialize any part of the GEnesis chip,
 * you first have to take it out of reset mode.
 */
static void
sk_init(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct mii_data *mii = device_get_softc(sc_if->sk_miibus);

	DPRINTFN(2, ("sk_init\n"));

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING)
		return;

	/* Cancel pending I/O and free all RX/TX buffers. */
	sk_stop(sc_if);

	if (SK_IS_GENESIS(sc)) {
		/* Configure LINK_SYNC LED */
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_ON);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			SK_LINKLED_LINKSYNC_ON);

		/* Configure RX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL,
			SK_RXLEDCTL_COUNTER_START);
		
		/* Configure TX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL,
			SK_TXLEDCTL_COUNTER_START);
	}

	/*
	 * Configure descriptor poll timer
	 *
	 * SK-NET GENESIS data sheet says that possibility of losing Start
	 * transmit command due to CPU/cache related interim storage problems
	 * under certain conditions. The document recommends a polling
	 * mechanism to send a Start transmit command to initiate transfer
	 * of ready descriptors regulary. To cope with this issue sk(4) now
	 * enables descriptor poll timer to initiate descriptor processing
	 * periodically as defined by SK_DPT_TIMER_MAX. However sk(4) still
	 * issue SK_TXBMU_TX_START to Tx BMU to get fast execution of Tx
	 * command instead of waiting for next descriptor polling time.
	 * The same rule may apply to Rx side too but it seems that is not
	 * needed at the moment.
	 * Since sk(4) uses descriptor polling as a last resort there is no
	 * need to set smaller polling time than maximum allowable one.
	 */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_INIT, SK_DPT_TIMER_MAX);

	/* Configure I2C registers */

	/* Configure XMAC(s) */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		break;
	}
	mii_mediachg(mii);

	if (SK_IS_GENESIS(sc)) {
		/* Configure MAC FIFOs */
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_ON);

		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_ON);
	}

	/* Configure transmit arbiter(s) */
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL,
	    SK_TXARCTL_ON | SK_TXARCTL_FSYNC_ON);

	/* Configure RAMbuffers */
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_START, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_WR_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_RD_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_END, sc_if->sk_rx_ramend);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_ON);

	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_STORENFWD_ON);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_START, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_WR_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_RD_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_END, sc_if->sk_tx_ramend);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_ON);

	/* Configure BMUs */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_LO,
	    SK_RX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_HI, 0);

	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_LO,
	    SK_TX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_HI, 0);

	/* Init descriptors */
	if (sk_init_rx_ring(sc_if) == ENOBUFS) {
		if_printf(ifp, "initialization failed: "
			  "no memory for rx buffers\n");
		sk_stop(sc_if);
		return;
	}

	if (sk_init_tx_ring(sc_if) == ENOBUFS) {
		if_printf(ifp, "initialization failed: "
			  "no memory for tx buffers\n");
		sk_stop(sc_if);
		return;
	}

	/* Configure interrupt handling */
	CSR_READ_4(sc, SK_ISSR);
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask |= SK_INTRS1;
	else
		sc->sk_intrmask |= SK_INTRS2;

	sc->sk_intrmask |= SK_ISR_EXTERNAL_REG;

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	/* Start BMUs. */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_START);

	if (SK_IS_GENESIS(sc)) {
		/* Enable XMACs TX and RX state machines */
		SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_IGNPAUSE);
		SK_XM_SETBIT_2(sc_if, XM_MMUCMD,
			       XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
	}

	if (SK_IS_YUKON(sc)) {
		uint16_t reg = SK_YU_READ_2(sc_if, YUKON_GPCR);
		reg |= YU_GPCR_TXEN | YU_GPCR_RXEN;
#if 0
		/* XXX disable 100Mbps and full duplex mode? */
		reg &= ~(YU_GPCR_SPEED | YU_GPCR_DPLX_DIS);
#endif
		SK_YU_WRITE_2(sc_if, YUKON_GPCR, reg);
	}

	/* Activate descriptor polling timer */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_START);
	/* Start transfer of Tx descriptors */
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (SK_IS_YUKON(sc))
		callout_reset(&sc_if->sk_tick_timer, hz, sk_yukon_tick, sc_if);
}

static void
sk_stop(struct sk_if_softc *sc_if)
{
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	uint32_t val;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	DPRINTFN(2, ("sk_stop\n"));

	callout_stop(&sc_if->sk_tick_timer);

	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);

	/* Stop Tx descriptor polling timer */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_STOP);

	/* Stop transfer of Tx descriptors */
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_STOP);
	for (i = 0; i < SK_TIMEOUT; i++) {
		val = CSR_READ_4(sc, sc_if->sk_tx_bmu);
		if (!(val & SK_TXBMU_TX_STOP))
			break;
		DELAY(1);
	}
	if (i == SK_TIMEOUT)
		if_printf(ifp, "cannot stop transfer of Tx descriptors\n");

	/* Stop transfer of Rx descriptors */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_STOP);
	for (i = 0; i < SK_TIMEOUT; i++) {
		val = SK_IF_READ_4(sc_if, 0, SK_RXQ1_BMU_CSR);
		if (!(val & SK_RXBMU_RX_STOP))
			break;
		DELAY(1);
	}
	if (i == SK_TIMEOUT)
		if_printf(ifp, "cannot stop transfer of Rx descriptors\n");

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		/* Put PHY back into reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A) {
			val |= SK_GPIO_DIR0;
			val &= ~SK_GPIO_DAT0;
		} else {
			val |= SK_GPIO_DIR2;
			val &= ~SK_GPIO_DAT2;
		}
		sk_win_write_4(sc, SK_GPIO, val);
	}

	/* Turn off various components of this interface. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);
	switch (sc->sk_type) {
	case SK_GENESIS:
		SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_RESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_RESET);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		SK_IF_WRITE_1(sc_if,0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_SET);
		SK_IF_WRITE_1(sc_if,0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_SET);
		break;
	}
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_RESET | SK_RBCTL_OFF);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST,
	    SK_RBCTL_RESET | SK_RBCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_LINKSYNC_OFF);

	/* Disable interrupts */
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask &= ~SK_INTRS1;
	else
		sc->sk_intrmask &= ~SK_INTRS2;
	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	SK_XM_READ_2(sc_if, XM_ISR);
	SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Free RX and TX mbufs still in the queues. */
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		if (cd->sk_rx_chain[i].sk_mbuf != NULL) {
			m_freem(cd->sk_rx_chain[i].sk_mbuf);
			cd->sk_rx_chain[i].sk_mbuf = NULL;
		}
	}
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		if (cd->sk_tx_chain[i].sk_mbuf != NULL) {
			bus_dmamap_unload(cd->sk_tx_dtag, cd->sk_tx_dmap[i]);
			m_freem(cd->sk_tx_chain[i].sk_mbuf);
			cd->sk_tx_chain[i].sk_mbuf = NULL;
		}
	}
}

#ifdef SK_DEBUG
static void
sk_dump_txdesc(struct sk_tx_desc *desc, int idx)
{
#define DESC_PRINT(X)					\
	if (X)					\
		printf("txdesc[%d]." #X "=%#x\n",	\
		       idx, X);

	DESC_PRINT(le32toh(desc->sk_ctl));
	DESC_PRINT(le32toh(desc->sk_next));
	DESC_PRINT(le32toh(desc->sk_data_lo));
	DESC_PRINT(le32toh(desc->sk_data_hi));
	DESC_PRINT(le32toh(desc->sk_xmac_txstat));
	DESC_PRINT(le16toh(desc->sk_rsvd0));
	DESC_PRINT(le16toh(desc->sk_csum_startval));
	DESC_PRINT(le16toh(desc->sk_csum_startpos));
	DESC_PRINT(le16toh(desc->sk_csum_writepos));
	DESC_PRINT(le16toh(desc->sk_rsvd1));
#undef PRINT
}

static void
sk_dump_bytes(const char *data, int len)
{
	int c, i, j;

	for (i = 0; i < len; i += 16) {
		printf("%08x  ", i);
		c = len - i;
		if (c > 16) c = 16;

		for (j = 0; j < c; j++) {
			printf("%02x ", data[i + j] & 0xff);
			if ((j & 0xf) == 7 && j > 0)
				printf(" ");
		}
		
		for (; j < 16; j++)
			printf("   ");
		printf("  ");

		for (j = 0; j < c; j++) {
			int ch = data[i + j] & 0xff;
			printf("%c", ' ' <= ch && ch <= '~' ? ch : ' ');
		}
		
		printf("\n");
		
		if (c < 16)
			break;
	}
}

static void
sk_dump_mbuf(struct mbuf *m)
{
	int count = m->m_pkthdr.len;

	printf("m=%p, m->m_pkthdr.len=%d\n", m, m->m_pkthdr.len);

	while (count > 0 && m) {
		printf("m=%p, m->m_data=%p, m->m_len=%d\n",
		       m, m->m_data, m->m_len);
		sk_dump_bytes(mtod(m, char *), m->m_len);

		count -= m->m_len;
		m = m->m_next;
	}
}
#endif

/*
 * Allocate jumbo buffer storage. The SysKonnect adapters support
 * "jumbograms" (9K frames), although SysKonnect doesn't currently
 * use them in their drivers. In order for us to use them, we need
 * large 9K receive buffers, however standard mbuf clusters are only
 * 2048 bytes in size. Consequently, we need to allocate and manage
 * our own jumbo buffer pool. Fortunately, this does not require an
 * excessive amount of additional code.
 */
static int
sk_jpool_alloc(device_t dev)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	bus_addr_t paddr;
	caddr_t buf;
	int error, i;

	lwkt_serialize_init(&cd->sk_jpool_serializer);

	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL, SK_JMEM, 1, SK_JMEM,
				   0, &cd->sk_jpool_dtag);
	if (error) {
		device_printf(dev, "can't create jpool DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(cd->sk_jpool_dtag, &cd->sk_jpool,
				 BUS_DMA_WAITOK, &cd->sk_jpool_dmap);
	if (error) {
		device_printf(dev, "can't alloc jpool DMA mem\n");
		bus_dma_tag_destroy(cd->sk_jpool_dtag);
		cd->sk_jpool_dtag = NULL;
		return error;
	}

	error = bus_dmamap_load(cd->sk_jpool_dtag, cd->sk_jpool_dmap,
				cd->sk_jpool, SK_JMEM,
				sk_dmamem_addr, &paddr, BUS_DMA_WAITOK);
	if (error) {
		device_printf(dev, "can't load DMA mem\n");
		bus_dmamem_free(cd->sk_jpool_dtag, cd->sk_jpool,
				cd->sk_jpool_dmap);
		bus_dma_tag_destroy(cd->sk_jpool_dtag);
		cd->sk_jpool_dtag = NULL;
		return error;
	}

	SLIST_INIT(&cd->sk_jpool_free_ent);
	buf = cd->sk_jpool;

	/*
	 * Now divide it up into SK_JLEN pieces.
	 */
	for (i = 0; i < SK_JSLOTS; i++) {
		struct sk_jpool_entry *entry = &cd->sk_jpool_ent[i];

		entry->sc_if = sc_if;
		entry->inuse = 0;
		entry->slot = i;
		entry->buf = buf;
		entry->paddr = paddr;

		SLIST_INSERT_HEAD(&cd->sk_jpool_free_ent, entry, entry_next);

		buf += SK_JLEN;
		paddr += SK_JLEN;
	}
	return 0;
}

static void
sk_jpool_free(struct sk_if_softc *sc_if)
{
	struct sk_chain_data *cd = &sc_if->sk_cdata;

	if (cd->sk_jpool_dtag != NULL) {
		bus_dmamap_unload(cd->sk_jpool_dtag, cd->sk_jpool_dmap);
		bus_dmamem_free(cd->sk_jpool_dtag, cd->sk_jpool,
				cd->sk_jpool_dmap);
		bus_dma_tag_destroy(cd->sk_jpool_dtag);
		cd->sk_jpool_dtag = NULL;
	}
}

static int
sk_dma_alloc(device_t dev)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);
	struct sk_chain_data *cd = &sc_if->sk_cdata;
	int i, j, error;

	/*
	 * Allocate the descriptor queues.
	 * TODO: split into RX/TX rings
	 */
	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   sizeof(struct sk_ring_data), 1,
				   sizeof(struct sk_ring_data), 0,
				   &sc_if->sk_rdata_dtag);
	if (error) {
		device_printf(dev, "can't create desc DMA tag\n");
		return error;
	}

	error = bus_dmamem_alloc(sc_if->sk_rdata_dtag,
				 (void **)&sc_if->sk_rdata,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &sc_if->sk_rdata_dmap);
	if (error) {
		device_printf(dev, "can't alloc desc DMA mem\n");
		bus_dma_tag_destroy(sc_if->sk_rdata_dtag);
		sc_if->sk_rdata_dtag = NULL;
		return error;
	}

	error = bus_dmamap_load(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap,
				sc_if->sk_rdata, sizeof(struct sk_ring_data),
				sk_dmamem_addr, &sc_if->sk_rdata_paddr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(dev, "can't load desc DMA mem\n");
		bus_dmamem_free(sc_if->sk_rdata_dtag, sc_if->sk_rdata,
				sc_if->sk_rdata_dmap);
		bus_dma_tag_destroy(sc_if->sk_rdata_dtag);
		sc_if->sk_rdata_dtag = NULL;
		return error;
	}

	/* Try to allocate memory for jumbo buffers. */
	error = sk_jpool_alloc(dev);
	if (error) {
		device_printf(dev, "jumbo buffer allocation failed\n");
		return error;
	}

	/* Create DMA tag for TX. */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL,
				   SK_JLEN, SK_NTXSEG, SK_JLEN,
				   0, &cd->sk_tx_dtag);
	if (error) {
		device_printf(dev, "can't create TX DMA tag\n");
		return error;
	}

	/* Create DMA maps for TX. */
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		error = bus_dmamap_create(cd->sk_tx_dtag, 0,
					  &cd->sk_tx_dmap[i]);
		if (error) {
			device_printf(dev, "can't create %dth TX DMA map\n", i);
			goto fail;
		}
	}
	return 0;
fail:
	for (j = 0; j < i; ++j)
		bus_dmamap_destroy(cd->sk_tx_dtag, cd->sk_tx_dmap[i]);
	bus_dma_tag_destroy(cd->sk_tx_dtag);
	cd->sk_tx_dtag = NULL;
	return error;
}

static void
sk_dma_free(device_t dev)
{
	struct sk_if_softc *sc_if = device_get_softc(dev);
	struct sk_chain_data *cd = &sc_if->sk_cdata;

	if (cd->sk_tx_dtag != NULL) {
		int i;

		for (i = 0; i < SK_TX_RING_CNT; ++i) {
			if (cd->sk_tx_chain[i].sk_mbuf != NULL) {
				bus_dmamap_unload(cd->sk_tx_dtag,
						  cd->sk_tx_dmap[i]);
				m_freem(cd->sk_tx_chain[i].sk_mbuf);
				cd->sk_tx_chain[i].sk_mbuf = NULL;
			}
			bus_dmamap_destroy(cd->sk_tx_dtag, cd->sk_tx_dmap[i]);
		}
		bus_dma_tag_destroy(cd->sk_tx_dtag);
		cd->sk_tx_dtag = NULL;
	}

	sk_jpool_free(sc_if);

	if (sc_if->sk_rdata_dtag != NULL) {
		bus_dmamap_unload(sc_if->sk_rdata_dtag, sc_if->sk_rdata_dmap);
		bus_dmamem_free(sc_if->sk_rdata_dtag, sc_if->sk_rdata,
				sc_if->sk_rdata_dmap);
		bus_dma_tag_destroy(sc_if->sk_rdata_dtag);
		sc_if->sk_rdata_dtag = NULL;
	}
}

static void
sk_buf_dma_addr(void *arg, bus_dma_segment_t *segs, int nsegs,
		bus_size_t mapsz __unused, int error)
{
	struct sk_dma_ctx *ctx = arg;
	int i;

	if (error)
		return;

	KASSERT(nsegs <= ctx->nsegs,
		("too many segments(%d), should be <= %d\n",
		 nsegs, ctx->nsegs));

	ctx->nsegs = nsegs;
	for (i = 0; i < nsegs; ++i)
		ctx->segs[i] = segs[i];
}

static void
sk_dmamem_addr(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	KASSERT(nseg == 1, ("too many segments %d", nseg));
	*((bus_addr_t *)arg) = seg->ds_addr;
}
