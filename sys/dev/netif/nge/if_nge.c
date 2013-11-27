/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2000, 2001
 *	Bill Paul <wpaul@bsdi.com>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/nge/if_nge.c,v 1.13.2.13 2003/02/05 22:03:57 mbr Exp $
 */

/*
 * National Semiconductor DP83820/DP83821 gigabit ethernet driver
 * for FreeBSD. Datasheets are available from:
 *
 * http://www.national.com/ds/DP/DP83820.pdf
 * http://www.national.com/ds/DP/DP83821.pdf
 *
 * These chips are used on several low cost gigabit ethernet NICs
 * sold by D-Link, Addtron, SMC and Asante. Both parts are
 * virtually the same, except the 83820 is a 64-bit/32-bit part,
 * while the 83821 is 32-bit only.
 *
 * Many cards also use National gigE transceivers, such as the
 * DP83891, DP83861 and DP83862 gigPHYTER parts. The DP83861 datasheet
 * contains a full register description that applies to all of these
 * components:
 *
 * http://www.national.com/ds/DP/DP83861.pdf
 *
 * Written by Bill Paul <wpaul@bsdi.com>
 * BSDi Open Source Solutions
 */

/*
 * The NatSemi DP83820 and 83821 controllers are enhanced versions
 * of the NatSemi MacPHYTER 10/100 devices. They support 10, 100
 * and 1000Mbps speeds with 1000baseX (ten bit interface), MII and GMII
 * ports. Other features include 8K TX FIFO and 32K RX FIFO, TCP/IP
 * hardware checksum offload (IPv4 only), VLAN tagging and filtering,
 * priority TX and RX queues, a 2048 bit multicast hash filter, 4 RX pattern
 * matching buffers, one perfect address filter buffer and interrupt
 * moderation. The 83820 supports both 64-bit and 32-bit addressing
 * and data transfers: the 64-bit support can be toggled on or off
 * via software. This affects the size of certain fields in the DMA
 * descriptors.
 *
 * There are two bugs/misfeatures in the 83820/83821 that I have
 * discovered so far:
 *
 * - Receive buffers must be aligned on 64-bit boundaries, which means
 *   you must resort to copying data in order to fix up the payload
 *   alignment.
 *
 * - In order to transmit jumbo frames larger than 8170 bytes, you have
 *   to turn off transmit checksum offloading, because the chip can't
 *   compute the checksum on an outgoing frame unless it fits entirely
 *   within the TX FIFO, which is only 8192 bytes in size. If you have
 *   TX checksum offload enabled and you transmit attempt to transmit a
 *   frame larger than 8170 bytes, the transmitter will wedge.
 *
 * To work around the latter problem, TX checksum offload is disabled
 * if the user selects an MTU larger than 8152 (8170 - 18).
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/serialize.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#define NGE_USEIOSPACE

#include "if_ngereg.h"


/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define NGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

/*
 * Various supported device vendors/types and their names.
 */
static struct nge_type nge_devs[] = {
	{ PCI_VENDOR_NS, PCI_PRODUCT_NS_DP83820,
	    "National Semiconductor Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static int	nge_probe(device_t);
static int	nge_attach(device_t);
static int	nge_detach(device_t);

static int	nge_alloc_jumbo_mem(struct nge_softc *);
static struct nge_jslot
		*nge_jalloc(struct nge_softc *);
static void	nge_jfree(void *);
static void	nge_jref(void *);

static int	nge_newbuf(struct nge_softc *, struct nge_desc *,
			   struct mbuf *);
static int	nge_encap(struct nge_softc *, struct mbuf *, uint32_t *);
static void	nge_rxeof(struct nge_softc *);
static void	nge_txeof(struct nge_softc *);
static void	nge_intr(void *);
static void	nge_tick(void *);
static void	nge_start(struct ifnet *, struct ifaltq_subque *);
static int	nge_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	nge_init(void *);
static void	nge_stop(struct nge_softc *);
static void	nge_watchdog(struct ifnet *);
static void	nge_shutdown(device_t);
static int	nge_ifmedia_upd(struct ifnet *);
static void	nge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void	nge_delay(struct nge_softc *);
static void	nge_eeprom_idle(struct nge_softc *);
static void	nge_eeprom_putbyte(struct nge_softc *, int);
static void	nge_eeprom_getword(struct nge_softc *, int, uint16_t *);
static void	nge_read_eeprom(struct nge_softc *, void *, int, int);

static void	nge_mii_sync(struct nge_softc *);
static void	nge_mii_send(struct nge_softc *, uint32_t, int);
static int	nge_mii_readreg(struct nge_softc *, struct nge_mii_frame *);
static int	nge_mii_writereg(struct nge_softc *, struct nge_mii_frame *);

static int	nge_miibus_readreg(device_t, int, int);
static int	nge_miibus_writereg(device_t, int, int, int);
static void	nge_miibus_statchg(device_t);

static void	nge_setmulti(struct nge_softc *);
static void	nge_reset(struct nge_softc *);
static int	nge_list_rx_init(struct nge_softc *);
static int	nge_list_tx_init(struct nge_softc *);
#ifdef IFPOLL_ENABLE
static void	nge_npoll(struct ifnet *, struct ifpoll_info *);
static void	nge_npoll_compat(struct ifnet *, void *, int);
#endif

#ifdef NGE_USEIOSPACE
#define NGE_RES			SYS_RES_IOPORT
#define NGE_RID			NGE_PCI_LOIO
#else
#define NGE_RES			SYS_RES_MEMORY
#define NGE_RID			NGE_PCI_LOMEM
#endif

static device_method_t nge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nge_probe),
	DEVMETHOD(device_attach,	nge_attach),
	DEVMETHOD(device_detach,	nge_detach),
	DEVMETHOD(device_shutdown,	nge_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	nge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	nge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	nge_miibus_statchg),

	DEVMETHOD_END
};

static DEFINE_CLASS_0(nge, nge_driver, nge_methods, sizeof(struct nge_softc));
static devclass_t nge_devclass;

DECLARE_DUMMY_MODULE(if_nge);
MODULE_DEPEND(if_nge, miibus, 1, 1, 1);
DRIVER_MODULE(if_nge, pci, nge_driver, nge_devclass, NULL, NULL);
DRIVER_MODULE(miibus, nge, miibus_driver, miibus_devclass, NULL, NULL);

#define NGE_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))

#define NGE_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, NGE_MEAR, CSR_READ_4(sc, NGE_MEAR) | (x))

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, NGE_MEAR, CSR_READ_4(sc, NGE_MEAR) & ~(x))

static void
nge_delay(struct nge_softc *sc)
{
	int idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, NGE_CSR);
}

static void
nge_eeprom_idle(struct nge_softc *sc)
{
	int i;

	SIO_SET(NGE_MEAR_EE_CSEL);
	nge_delay(sc);
	SIO_SET(NGE_MEAR_EE_CLK);
	nge_delay(sc);

	for (i = 0; i < 25; i++) {
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}

	SIO_CLR(NGE_MEAR_EE_CLK);
	nge_delay(sc);
	SIO_CLR(NGE_MEAR_EE_CSEL);
	nge_delay(sc);
	CSR_WRITE_4(sc, NGE_MEAR, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
nge_eeprom_putbyte(struct nge_softc *sc, int addr)
{
	int d, i;

	d = addr | NGE_EECMD_READ;

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i)
			SIO_SET(NGE_MEAR_EE_DIN);
		else
			SIO_CLR(NGE_MEAR_EE_DIN);
		nge_delay(sc);
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
nge_eeprom_getword(struct nge_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/* Force EEPROM to idle state. */
	nge_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	nge_delay(sc);
	SIO_CLR(NGE_MEAR_EE_CLK);
	nge_delay(sc);
	SIO_SET(NGE_MEAR_EE_CSEL);
	nge_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	nge_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		if (CSR_READ_4(sc, NGE_MEAR) & NGE_MEAR_EE_DOUT)
			word |= i;
		nge_delay(sc);
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	nge_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
nge_read_eeprom(struct nge_softc *sc, void *dest, int off, int cnt)
{
	int i;
	uint16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		nge_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)((uint8_t *)dest + (i * 2));
		*ptr = word;
	}
}

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
nge_mii_sync(struct nge_softc *sc)
{
	int i;

	SIO_SET(NGE_MEAR_MII_DIR | NGE_MEAR_MII_DATA);

	for (i = 0; i < 32; i++) {
		SIO_SET(NGE_MEAR_MII_CLK);
		DELAY(1);
		SIO_CLR(NGE_MEAR_MII_CLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
nge_mii_send(struct nge_softc *sc, uint32_t bits, int cnt)
{
	int i;

	SIO_CLR(NGE_MEAR_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i)
			SIO_SET(NGE_MEAR_MII_DATA);
                else
			SIO_CLR(NGE_MEAR_MII_DATA);
		DELAY(1);
		SIO_CLR(NGE_MEAR_MII_CLK);
		DELAY(1);
		SIO_SET(NGE_MEAR_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
static int
nge_mii_readreg(struct nge_softc *sc, struct nge_mii_frame *frame)
{
	int ack, i;

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = NGE_MII_STARTDELIM;
	frame->mii_opcode = NGE_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;

	CSR_WRITE_4(sc, NGE_MEAR, 0);

	/*
 	 * Turn on data xmit.
	 */
	SIO_SET(NGE_MEAR_MII_DIR);

	nge_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	nge_mii_send(sc, frame->mii_stdelim, 2);
	nge_mii_send(sc, frame->mii_opcode, 2);
	nge_mii_send(sc, frame->mii_phyaddr, 5);
	nge_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	SIO_CLR((NGE_MEAR_MII_CLK | NGE_MEAR_MII_DATA));
	DELAY(1);
	SIO_SET(NGE_MEAR_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(NGE_MEAR_MII_DIR);
	/* Check for ack */
	SIO_CLR(NGE_MEAR_MII_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, NGE_MEAR) & NGE_MEAR_MII_DATA;
	SIO_SET(NGE_MEAR_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(NGE_MEAR_MII_CLK);
			DELAY(1);
			SIO_SET(NGE_MEAR_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(NGE_MEAR_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, NGE_MEAR) & NGE_MEAR_MII_DATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(NGE_MEAR_MII_CLK);
		DELAY(1);
	}

fail:
	SIO_CLR(NGE_MEAR_MII_CLK);
	DELAY(1);
	SIO_SET(NGE_MEAR_MII_CLK);
	DELAY(1);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
static int
nge_mii_writereg(struct nge_softc *sc, struct nge_mii_frame *frame)
{
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = NGE_MII_STARTDELIM;
	frame->mii_opcode = NGE_MII_WRITEOP;
	frame->mii_turnaround = NGE_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	SIO_SET(NGE_MEAR_MII_DIR);

	nge_mii_sync(sc);

	nge_mii_send(sc, frame->mii_stdelim, 2);
	nge_mii_send(sc, frame->mii_opcode, 2);
	nge_mii_send(sc, frame->mii_phyaddr, 5);
	nge_mii_send(sc, frame->mii_regaddr, 5);
	nge_mii_send(sc, frame->mii_turnaround, 2);
	nge_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	SIO_SET(NGE_MEAR_MII_CLK);
	DELAY(1);
	SIO_CLR(NGE_MEAR_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	SIO_CLR(NGE_MEAR_MII_DIR);

	return(0);
}

static int
nge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct nge_softc *sc = device_get_softc(dev);
	struct nge_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	nge_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

static int
nge_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct nge_softc *sc = device_get_softc(dev);
	struct nge_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;
	nge_mii_writereg(sc, &frame);

	return(0);
}

static void
nge_miibus_statchg(device_t dev)
{
	struct nge_softc *sc = device_get_softc(dev);
	struct mii_data *mii;
	int status;	

	if (sc->nge_tbi) {
		if (IFM_SUBTYPE(sc->nge_ifmedia.ifm_cur->ifm_media)
		    == IFM_AUTO) {
			status = CSR_READ_4(sc, NGE_TBI_ANLPAR);
			if (status == 0 || status & NGE_TBIANAR_FDX) {
				NGE_SETBIT(sc, NGE_TX_CFG,
				    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
				NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
			} else {
				NGE_CLRBIT(sc, NGE_TX_CFG,
				    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
				NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
			}
		} else if ((sc->nge_ifmedia.ifm_cur->ifm_media & IFM_GMASK) 
			!= IFM_FDX) {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		} else {
			NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		}
	} else {
		mii = device_get_softc(sc->nge_miibus);

		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		        NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		} else {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		}

		/* If we have a 1000Mbps link, set the mode_1000 bit. */
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
		    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX) {
			NGE_SETBIT(sc, NGE_CFG, NGE_CFG_MODE_1000);
		} else {
			NGE_CLRBIT(sc, NGE_CFG, NGE_CFG_MODE_1000);
		}
	}
}

static void
nge_setmulti(struct nge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmultiaddr *ifma;
	uint32_t filtsave, h = 0, i;
	int bit, index;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		NGE_CLRBIT(sc, NGE_RXFILT_CTL,
		    NGE_RXFILTCTL_MCHASH | NGE_RXFILTCTL_UCHASH);
		NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_ALLMULTI);
		return;
	}

	/*
	 * We have to explicitly enable the multicast hash table
	 * on the NatSemi chip if we want to use it, which we do.
	 * We also have to tell it that we don't want to use the
	 * hash table for matching unicast addresses.
	 */
	NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_MCHASH);
	NGE_CLRBIT(sc, NGE_RXFILT_CTL,
	    NGE_RXFILTCTL_ALLMULTI | NGE_RXFILTCTL_UCHASH);

	filtsave = CSR_READ_4(sc, NGE_RXFILT_CTL);

	/* first, zot all the existing hash bits */
	for (i = 0; i < NGE_MCAST_FILTER_LEN; i += 2) {
		CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_MCAST_LO + i);
		CSR_WRITE_4(sc, NGE_RXFILT_DATA, 0);
	}

	/*
	 * From the 11 bits returned by the crc routine, the top 7
	 * bits represent the 16-bit word in the mcast hash table
	 * that needs to be updated, and the lower 4 bits represent
	 * which bit within that byte needs to be set.
	 */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 21;
		index = (h >> 4) & 0x7F;
		bit = h & 0xF;
		CSR_WRITE_4(sc, NGE_RXFILT_CTL,
		    NGE_FILTADDR_MCAST_LO + (index * 2));
		NGE_SETBIT(sc, NGE_RXFILT_DATA, (1 << bit));
	}

	CSR_WRITE_4(sc, NGE_RXFILT_CTL, filtsave);
}

static void
nge_reset(struct nge_softc *sc)
{
	int i;

	NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RESET);

	for (i = 0; i < NGE_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, NGE_CSR) & NGE_CSR_RESET) == 0)
			break;
	}

	if (i == NGE_TIMEOUT)
		kprintf("nge%d: reset never completed\n", sc->nge_unit);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/*
	 * If this is a NetSemi chip, make sure to clear
	 * PME mode.
	 */
	CSR_WRITE_4(sc, NGE_CLKRUN, NGE_CLKRUN_PMESTS);
	CSR_WRITE_4(sc, NGE_CLKRUN, 0);
}

/*
 * Probe for an NatSemi chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
nge_probe(device_t dev)
{
	struct nge_type	*t;
	uint16_t vendor, product;

	vendor = pci_get_vendor(dev);
	product = pci_get_device(dev);

	for (t = nge_devs; t->nge_name != NULL; t++) {
		if (vendor == t->nge_vid && product == t->nge_did) {
			device_set_desc(dev, t->nge_name);
			return(0);
		}
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
nge_attach(device_t dev)
{
	struct nge_softc *sc;
	struct ifnet *ifp;
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint32_t		command;
	int error = 0, rid, unit;
	const char		*sep = "";

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	callout_init(&sc->nge_stat_timer);
	lwkt_serialize_init(&sc->nge_jslot_serializer);

	/*
	 * Handle power management nonsense.
	 */
	command = pci_read_config(dev, NGE_PCI_CAPID, 4) & 0x000000FF;
	if (command == 0x01) {
		command = pci_read_config(dev, NGE_PCI_PWRMGMTCTRL, 4);
		if (command & NGE_PSTATE_MASK) {
			uint32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_read_config(dev, NGE_PCI_LOIO, 4);
			membase = pci_read_config(dev, NGE_PCI_LOMEM, 4);
			irq = pci_read_config(dev, NGE_PCI_INTLINE, 4);

			/* Reset the power state. */
			kprintf("nge%d: chip is in D%d power mode "
			"-- setting to D0\n", unit, command & NGE_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_write_config(dev, NGE_PCI_PWRMGMTCTRL, command, 4);

			/* Restore PCI config data. */
			pci_write_config(dev, NGE_PCI_LOIO, iobase, 4);
			pci_write_config(dev, NGE_PCI_LOMEM, membase, 4);
			pci_write_config(dev, NGE_PCI_INTLINE, irq, 4);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_read_config(dev, PCIR_COMMAND, 4);
	command |= (PCIM_CMD_PORTEN|PCIM_CMD_MEMEN|PCIM_CMD_BUSMASTEREN);
	pci_write_config(dev, PCIR_COMMAND, command, 4);
	command = pci_read_config(dev, PCIR_COMMAND, 4);

#ifdef NGE_USEIOSPACE
	if (!(command & PCIM_CMD_PORTEN)) {
		kprintf("nge%d: failed to enable I/O ports!\n", unit);
		error = ENXIO;
		return(error);
	}
#else
	if (!(command & PCIM_CMD_MEMEN)) {
		kprintf("nge%d: failed to enable memory mapping!\n", unit);
		error = ENXIO;
		return(error);
	}
#endif

	rid = NGE_RID;
	sc->nge_res = bus_alloc_resource_any(dev, NGE_RES, &rid, RF_ACTIVE);

	if (sc->nge_res == NULL) {
		kprintf("nge%d: couldn't map ports/memory\n", unit);
		error = ENXIO;
		return(error);
	}

	sc->nge_btag = rman_get_bustag(sc->nge_res);
	sc->nge_bhandle = rman_get_bushandle(sc->nge_res);

	/* Allocate interrupt */
	rid = 0;
	sc->nge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->nge_irq == NULL) {
		kprintf("nge%d: couldn't map interrupt\n", unit);
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	nge_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	nge_read_eeprom(sc, &eaddr[4], NGE_EE_NODEADDR, 1);
	nge_read_eeprom(sc, &eaddr[2], NGE_EE_NODEADDR + 1, 1);
	nge_read_eeprom(sc, &eaddr[0], NGE_EE_NODEADDR + 2, 1);

	sc->nge_unit = unit;

	sc->nge_ldata = contigmalloc(sizeof(struct nge_list_data), M_DEVBUF,
	    M_WAITOK | M_ZERO, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->nge_ldata == NULL) {
		kprintf("nge%d: no memory for list buffers!\n", unit);
		error = ENXIO;
		goto fail;
	}

	/* Try to allocate memory for jumbo buffers. */
	if (nge_alloc_jumbo_mem(sc)) {
		kprintf("nge%d: jumbo buffer allocation failed\n",
                    sc->nge_unit);
		error = ENXIO;
		goto fail;
	}

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, "nge", unit);
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nge_ioctl;
	ifp->if_start = nge_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = nge_npoll;
#endif
	ifp->if_watchdog = nge_watchdog;
	ifp->if_init = nge_init;
	ifp->if_baudrate = 1000000000;
	ifq_set_maxlen(&ifp->if_snd, NGE_TX_LIST_CNT - 1);
	ifq_set_ready(&ifp->if_snd);
	ifp->if_hwassist = NGE_CSUM_FEATURES;
	ifp->if_capabilities = IFCAP_HWCSUM | IFCAP_VLAN_HWTAGGING;
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Do MII setup.
	 */
	if (mii_phy_probe(dev, &sc->nge_miibus,
			  nge_ifmedia_upd, nge_ifmedia_sts)) {
		if (CSR_READ_4(sc, NGE_CFG) & NGE_CFG_TBI_EN) {
			sc->nge_tbi = 1;
			device_printf(dev, "Using TBI\n");
			
			sc->nge_miibus = dev;

			ifmedia_init(&sc->nge_ifmedia, 0, nge_ifmedia_upd, 
				nge_ifmedia_sts);
#define	ADD(m, c)	ifmedia_add(&sc->nge_ifmedia, (m), (c), NULL)
#define PRINT(s)	kprintf("%s%s", sep, s); sep = ", "
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_NONE, 0, 0), 0);
			device_printf(dev, " ");
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, 0, 0), 0);
			PRINT("1000baseSX");
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, IFM_FDX, 0),0);
			PRINT("1000baseSX-FDX");
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, 0), 0);
			PRINT("auto");
	    
			kprintf("\n");
#undef ADD
#undef PRINT
			ifmedia_set(&sc->nge_ifmedia, 
				IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, 0));
	    
			CSR_WRITE_4(sc, NGE_GPIO, CSR_READ_4(sc, NGE_GPIO)
				| NGE_GPIO_GP4_OUT 
				| NGE_GPIO_GP1_OUTENB | NGE_GPIO_GP2_OUTENB 
				| NGE_GPIO_GP3_OUTENB
				| NGE_GPIO_GP3_IN | NGE_GPIO_GP4_IN);
	    
		} else {
			kprintf("nge%d: MII without any PHY!\n", sc->nge_unit);
			error = ENXIO;
			goto fail;
		}
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->nge_irq));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->nge_npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif

	error = bus_setup_intr(dev, sc->nge_irq, INTR_MPSAFE,
			       nge_intr, sc, &sc->nge_intrhand, 
			       ifp->if_serializer);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

	return(0);
fail:
	nge_detach(dev);
	return(error);
}

static int
nge_detach(device_t dev)
{
	struct nge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		nge_reset(sc);
		nge_stop(sc);
		bus_teardown_intr(dev, sc->nge_irq, sc->nge_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->nge_miibus)
		device_delete_child(dev, sc->nge_miibus);
	bus_generic_detach(dev);

	if (sc->nge_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->nge_irq);
	if (sc->nge_res)
		bus_release_resource(dev, NGE_RES, NGE_RID, sc->nge_res);
	if (sc->nge_ldata) {
		contigfree(sc->nge_ldata, sizeof(struct nge_list_data),
			   M_DEVBUF);
	}
	if (sc->nge_cdata.nge_jumbo_buf)
		contigfree(sc->nge_cdata.nge_jumbo_buf, NGE_JMEM, M_DEVBUF);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
nge_list_tx_init(struct nge_softc *sc)
{
	struct nge_list_data *ld;
	struct nge_ring_data *cd;
	int i;

	cd = &sc->nge_cdata;
	ld = sc->nge_ldata;

	for (i = 0; i < NGE_TX_LIST_CNT; i++) {
		if (i == (NGE_TX_LIST_CNT - 1)) {
			ld->nge_tx_list[i].nge_nextdesc =
			    &ld->nge_tx_list[0];
			ld->nge_tx_list[i].nge_next =
			    vtophys(&ld->nge_tx_list[0]);
		} else {
			ld->nge_tx_list[i].nge_nextdesc =
			    &ld->nge_tx_list[i + 1];
			ld->nge_tx_list[i].nge_next =
			    vtophys(&ld->nge_tx_list[i + 1]);
		}
		ld->nge_tx_list[i].nge_mbuf = NULL;
		ld->nge_tx_list[i].nge_ptr = 0;
		ld->nge_tx_list[i].nge_ctl = 0;
	}

	cd->nge_tx_prod = cd->nge_tx_cons = cd->nge_tx_cnt = 0;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
nge_list_rx_init(struct nge_softc *sc)
{
	struct nge_list_data *ld;
	struct nge_ring_data *cd;
	int i;

	ld = sc->nge_ldata;
	cd = &sc->nge_cdata;

	for (i = 0; i < NGE_RX_LIST_CNT; i++) {
		if (nge_newbuf(sc, &ld->nge_rx_list[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (NGE_RX_LIST_CNT - 1)) {
			ld->nge_rx_list[i].nge_nextdesc =
			    &ld->nge_rx_list[0];
			ld->nge_rx_list[i].nge_next =
			    vtophys(&ld->nge_rx_list[0]);
		} else {
			ld->nge_rx_list[i].nge_nextdesc =
			    &ld->nge_rx_list[i + 1];
			ld->nge_rx_list[i].nge_next =
			    vtophys(&ld->nge_rx_list[i + 1]);
		}
	}

	cd->nge_rx_prod = 0;

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
nge_newbuf(struct nge_softc *sc, struct nge_desc *c, struct mbuf *m)
{
	struct mbuf *m_new = NULL;
	struct nge_jslot *buf;

	if (m == NULL) {
		MGETHDR(m_new, MB_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			kprintf("nge%d: no memory for rx list "
			    "-- packet dropped!\n", sc->nge_unit);
			return(ENOBUFS);
		}

		/* Allocate the jumbo buffer */
		buf = nge_jalloc(sc);
		if (buf == NULL) {
#ifdef NGE_VERBOSE
			kprintf("nge%d: jumbo allocation failed "
			    "-- packet dropped!\n", sc->nge_unit);
#endif
			m_freem(m_new);
			return(ENOBUFS);
		}
		/* Attach the buffer to the mbuf */
		m_new->m_ext.ext_arg = buf;
		m_new->m_ext.ext_buf = buf->nge_buf;
		m_new->m_ext.ext_free = nge_jfree;
		m_new->m_ext.ext_ref = nge_jref;
		m_new->m_ext.ext_size = NGE_JUMBO_FRAMELEN;

		m_new->m_data = m_new->m_ext.ext_buf;
		m_new->m_flags |= M_EXT;
		m_new->m_len = m_new->m_pkthdr.len = m_new->m_ext.ext_size;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = NGE_JLEN;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, sizeof(uint64_t));

	c->nge_mbuf = m_new;
	c->nge_ptr = vtophys(mtod(m_new, caddr_t));
	c->nge_ctl = m_new->m_len;
	c->nge_extsts = 0;

	return(0);
}

static int
nge_alloc_jumbo_mem(struct nge_softc *sc)
{
	caddr_t ptr;
	int i;
	struct nge_jslot *entry;

	/* Grab a big chunk o' storage. */
	sc->nge_cdata.nge_jumbo_buf = contigmalloc(NGE_JMEM, M_DEVBUF,
	    M_WAITOK, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->nge_cdata.nge_jumbo_buf == NULL) {
		kprintf("nge%d: no memory for jumbo buffers!\n", sc->nge_unit);
		return(ENOBUFS);
	}

	SLIST_INIT(&sc->nge_jfree_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array.
	 */
	ptr = sc->nge_cdata.nge_jumbo_buf;
	for (i = 0; i < NGE_JSLOTS; i++) {
		entry = &sc->nge_cdata.nge_jslots[i];
		entry->nge_sc = sc;
		entry->nge_buf = ptr;
		entry->nge_inuse = 0;
		entry->nge_slot = i;
		SLIST_INSERT_HEAD(&sc->nge_jfree_listhead, entry, jslot_link);
		ptr += NGE_JLEN;
	}

	return(0);
}


/*
 * Allocate a jumbo buffer.
 */
static struct nge_jslot *
nge_jalloc(struct nge_softc *sc)
{
	struct nge_jslot *entry;

	lwkt_serialize_enter(&sc->nge_jslot_serializer);
	entry = SLIST_FIRST(&sc->nge_jfree_listhead);
	if (entry) {
		SLIST_REMOVE_HEAD(&sc->nge_jfree_listhead, jslot_link);
		entry->nge_inuse = 1;
	} else {
#ifdef NGE_VERBOSE
		kprintf("nge%d: no free jumbo buffers\n", sc->nge_unit);
#endif
	}
	lwkt_serialize_exit(&sc->nge_jslot_serializer);
	return(entry);
}

/*
 * Adjust usage count on a jumbo buffer. In general this doesn't
 * get used much because our jumbo buffers don't get passed around
 * a lot, but it's implemented for correctness.
 */
static void
nge_jref(void *arg)
{
	struct nge_jslot *entry = (struct nge_jslot *)arg;
	struct nge_softc *sc = entry->nge_sc;

	if (sc == NULL)
		panic("nge_jref: can't find softc pointer!");

	if (&sc->nge_cdata.nge_jslots[entry->nge_slot] != entry)
		panic("nge_jref: asked to reference buffer "
		    "that we don't manage!");
	else if (entry->nge_inuse == 0)
		panic("nge_jref: buffer already free!");
	else
		atomic_add_int(&entry->nge_inuse, 1);
}

/*
 * Release a jumbo buffer.
 */
static void
nge_jfree(void *arg)
{
	struct nge_jslot *entry = (struct nge_jslot *)arg;
	struct nge_softc *sc = entry->nge_sc;

	if (sc == NULL)
		panic("nge_jref: can't find softc pointer!");

	if (&sc->nge_cdata.nge_jslots[entry->nge_slot] != entry) {
		panic("nge_jref: asked to reference buffer "
		    "that we don't manage!");
	} else if (entry->nge_inuse == 0) {
		panic("nge_jref: buffer already free!");
	} else {
		lwkt_serialize_enter(&sc->nge_jslot_serializer);
		atomic_subtract_int(&entry->nge_inuse, 1);
		if (entry->nge_inuse == 0) {
			SLIST_INSERT_HEAD(&sc->nge_jfree_listhead, 
					  entry, jslot_link);
		}
		lwkt_serialize_exit(&sc->nge_jslot_serializer);
	}
}
/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
nge_rxeof(struct nge_softc *sc)
{
        struct mbuf *m;
        struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nge_desc *cur_rx;
	int i, total_len = 0;
	uint32_t rxstat;

	i = sc->nge_cdata.nge_rx_prod;

	while(NGE_OWNDESC(&sc->nge_ldata->nge_rx_list[i])) {
		struct mbuf *m0 = NULL;
		uint32_t extsts;

#ifdef IFPOLL_ENABLE
		if (ifp->if_flags & IFF_NPOLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif /* IFPOLL_ENABLE */

		cur_rx = &sc->nge_ldata->nge_rx_list[i];
		rxstat = cur_rx->nge_rxstat;
		extsts = cur_rx->nge_extsts;
		m = cur_rx->nge_mbuf;
		cur_rx->nge_mbuf = NULL;
		total_len = NGE_RXBYTES(cur_rx);
		NGE_INC(i, NGE_RX_LIST_CNT);
		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if ((rxstat & NGE_CMDSTS_PKT_OK) == 0) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			nge_newbuf(sc, cur_rx, m);
			continue;
		}

		/*
		 * Ok. NatSemi really screwed up here. This is the
		 * only gigE chip I know of with alignment constraints
		 * on receive buffers. RX buffers must be 64-bit aligned.
		 */
#ifdef __i386__
		/*
		 * By popular demand, ignore the alignment problems
		 * on the Intel x86 platform. The performance hit
		 * incurred due to unaligned accesses is much smaller
		 * than the hit produced by forcing buffer copies all
		 * the time, especially with jumbo frames. We still
		 * need to fix up the alignment everywhere else though.
		 */
		if (nge_newbuf(sc, cur_rx, NULL) == ENOBUFS) {
#endif
			m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
			    total_len + ETHER_ALIGN, 0, ifp, NULL);
			nge_newbuf(sc, cur_rx, m);
			if (m0 == NULL) {
				kprintf("nge%d: no receive buffers "
				    "available -- packet dropped!\n",
				    sc->nge_unit);
				IFNET_STAT_INC(ifp, ierrors, 1);
				continue;
			}
			m_adj(m0, ETHER_ALIGN);
			m = m0;
#ifdef __i386__
		} else {
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = total_len;
		}
#endif

		IFNET_STAT_INC(ifp, ipackets, 1);

		/* Do IP checksum checking. */
		if (extsts & NGE_RXEXTSTS_IPPKT)
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
		if (!(extsts & NGE_RXEXTSTS_IPCSUMERR))
			m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		if ((extsts & NGE_RXEXTSTS_TCPPKT &&
		    (extsts & NGE_RXEXTSTS_TCPCSUMERR) == 0) ||
		    (extsts & NGE_RXEXTSTS_UDPPKT &&
		    (extsts & NGE_RXEXTSTS_UDPCSUMERR) == 0)) {
			m->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID|CSUM_PSEUDO_HDR|
			    CSUM_FRAG_NOT_CHECKED;
			m->m_pkthdr.csum_data = 0xffff;
		}

		/*
		 * If we received a packet with a vlan tag, pass it
		 * to vlan_input() instead of ether_input().
		 */
		if (extsts & NGE_RXEXTSTS_VLANPKT) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag =
				(extsts & NGE_RXEXTSTS_VTCI);
		}
		ifp->if_input(ifp, m);
	}

	sc->nge_cdata.nge_rx_prod = i;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
nge_txeof(struct nge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct nge_desc *cur_tx = NULL;
	uint32_t idx;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->nge_cdata.nge_tx_cons;
	while (idx != sc->nge_cdata.nge_tx_prod) {
		cur_tx = &sc->nge_ldata->nge_tx_list[idx];

		if (NGE_OWNDESC(cur_tx))
			break;

		if (cur_tx->nge_ctl & NGE_CMDSTS_MORE) {
			sc->nge_cdata.nge_tx_cnt--;
			NGE_INC(idx, NGE_TX_LIST_CNT);
			continue;
		}

		if (!(cur_tx->nge_ctl & NGE_CMDSTS_PKT_OK)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (cur_tx->nge_txstat & NGE_TXSTAT_EXCESSCOLLS)
				IFNET_STAT_INC(ifp, collisions, 1);
			if (cur_tx->nge_txstat & NGE_TXSTAT_OUTOFWINCOLL)
				IFNET_STAT_INC(ifp, collisions, 1);
		}

		IFNET_STAT_INC(ifp, collisions,
		    (cur_tx->nge_txstat & NGE_TXSTAT_COLLCNT) >> 16);

		IFNET_STAT_INC(ifp, opackets, 1);
		if (cur_tx->nge_mbuf != NULL) {
			m_freem(cur_tx->nge_mbuf);
			cur_tx->nge_mbuf = NULL;
		}

		sc->nge_cdata.nge_tx_cnt--;
		NGE_INC(idx, NGE_TX_LIST_CNT);
		ifp->if_timer = 0;
	}

	sc->nge_cdata.nge_tx_cons = idx;

	if (cur_tx != NULL)
		ifq_clr_oactive(&ifp->if_snd);
}

static void
nge_tick(void *xsc)
{
	struct nge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	lwkt_serialize_enter(ifp->if_serializer);

	if (sc->nge_tbi) {
		if (sc->nge_link == 0) {
			if (CSR_READ_4(sc, NGE_TBI_BMSR) 
			    & NGE_TBIBMSR_ANEG_DONE) {
				kprintf("nge%d: gigabit link up\n",
				    sc->nge_unit);
				nge_miibus_statchg(sc->nge_miibus);
				sc->nge_link++;
				if (!ifq_is_empty(&ifp->if_snd))
					if_devstart(ifp);
			}
		}
	} else {
		mii = device_get_softc(sc->nge_miibus);
		mii_tick(mii);

		if (sc->nge_link == 0) {
			if (mii->mii_media_status & IFM_ACTIVE &&
			    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
				sc->nge_link++;
				if (IFM_SUBTYPE(mii->mii_media_active) 
				    == IFM_1000_T)
					kprintf("nge%d: gigabit link up\n",
					    sc->nge_unit);
				if (!ifq_is_empty(&ifp->if_snd))
					if_devstart(ifp);
			}
		}
	}
	callout_reset(&sc->nge_stat_timer, hz, nge_tick, sc);

	lwkt_serialize_exit(ifp->if_serializer);
}

#ifdef IFPOLL_ENABLE

static void
nge_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct nge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * On the nge, reading the status register also clears it.
	 * So before returning to intr mode we must make sure that all
	 * possible pending sources of interrupts have been served.
	 * In practice this means run to completion the *eof routines,
	 * and then call the interrupt routine
	 */
	sc->rxcycles = count;
	nge_rxeof(sc);
	nge_txeof(sc);
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	if (sc->nge_npoll.ifpc_stcount-- == 0) {
		uint32_t status;

		sc->nge_npoll.ifpc_stcount = sc->nge_npoll.ifpc_stfrac;

		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, NGE_ISR);

		if (status & (NGE_ISR_RX_ERR|NGE_ISR_RX_OFLOW))
			nge_rxeof(sc);

		if (status & (NGE_ISR_RX_IDLE))
			NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);

		if (status & NGE_ISR_SYSERR) {
			nge_reset(sc);
			nge_init(sc);
		}
	}
}

static void
nge_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct nge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->nge_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = nge_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			/* disable interrupts */
			CSR_WRITE_4(sc, NGE_IER, 0);
			sc->nge_npoll.ifpc_stcount = 0;
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING) {
			/* enable interrupts */
			CSR_WRITE_4(sc, NGE_IER, 1);
		}
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->nge_irq));
	}
}

#endif /* IFPOLL_ENABLE */

static void
nge_intr(void *arg)
{
	struct nge_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t status;

	/* Supress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		nge_stop(sc);
		return;
	}

	/* Disable interrupts. */
	CSR_WRITE_4(sc, NGE_IER, 0);

	/* Data LED on for TBI mode */
	if(sc->nge_tbi)
		 CSR_WRITE_4(sc, NGE_GPIO, CSR_READ_4(sc, NGE_GPIO)
			     | NGE_GPIO_GP3_OUT);

	for (;;) {
		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, NGE_ISR);

		if ((status & NGE_INTRS) == 0)
			break;

		if ((status & NGE_ISR_TX_DESC_OK) ||
		    (status & NGE_ISR_TX_ERR) ||
		    (status & NGE_ISR_TX_OK) ||
		    (status & NGE_ISR_TX_IDLE))
			nge_txeof(sc);

		if ((status & NGE_ISR_RX_DESC_OK) ||
		    (status & NGE_ISR_RX_ERR) ||
		    (status & NGE_ISR_RX_OFLOW) ||
		    (status & NGE_ISR_RX_FIFO_OFLOW) ||
		    (status & NGE_ISR_RX_IDLE) ||
		    (status & NGE_ISR_RX_OK))
			nge_rxeof(sc);

		if ((status & NGE_ISR_RX_IDLE))
			NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);

		if (status & NGE_ISR_SYSERR) {
			nge_reset(sc);
			ifp->if_flags &= ~IFF_RUNNING;
			nge_init(sc);
		}

#ifdef notyet
		/* mii_tick should only be called once per second */
		if (status & NGE_ISR_PHY_INTR) {
			sc->nge_link = 0;
			nge_tick_serialized(sc);
		}
#endif
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, NGE_IER, 1);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	/* Data LED off for TBI mode */

	if(sc->nge_tbi)
		CSR_WRITE_4(sc, NGE_GPIO, CSR_READ_4(sc, NGE_GPIO)
			    & ~NGE_GPIO_GP3_OUT);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
nge_encap(struct nge_softc *sc, struct mbuf *m_head, uint32_t *txidx)
{
	struct nge_desc *f = NULL;
	struct mbuf *m;
	int frag, cur, cnt = 0;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	cur = frag = *txidx;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if ((NGE_TX_LIST_CNT -
			    (sc->nge_cdata.nge_tx_cnt + cnt)) < 2)
				break;
			f = &sc->nge_ldata->nge_tx_list[frag];
			f->nge_ctl = NGE_CMDSTS_MORE | m->m_len;
			f->nge_ptr = vtophys(mtod(m, vm_offset_t));
			if (cnt != 0)
				f->nge_ctl |= NGE_CMDSTS_OWN;
			cur = frag;
			NGE_INC(frag, NGE_TX_LIST_CNT);
			cnt++;
		}
	}
	/* Caller should make sure that 'm_head' is not excessive fragmented */
	KASSERT(m == NULL, ("too many fragments"));

	sc->nge_ldata->nge_tx_list[*txidx].nge_extsts = 0;
	if (m_head->m_pkthdr.csum_flags) {
		if (m_head->m_pkthdr.csum_flags & CSUM_IP)
			sc->nge_ldata->nge_tx_list[*txidx].nge_extsts |=
			    NGE_TXEXTSTS_IPCSUM;
		if (m_head->m_pkthdr.csum_flags & CSUM_TCP)
			sc->nge_ldata->nge_tx_list[*txidx].nge_extsts |=
			    NGE_TXEXTSTS_TCPCSUM;
		if (m_head->m_pkthdr.csum_flags & CSUM_UDP)
			sc->nge_ldata->nge_tx_list[*txidx].nge_extsts |=
			    NGE_TXEXTSTS_UDPCSUM;
	}

	if (m_head->m_flags & M_VLANTAG) {
		sc->nge_ldata->nge_tx_list[cur].nge_extsts |=
			(NGE_TXEXTSTS_VLANPKT|m_head->m_pkthdr.ether_vlantag);
	}

	sc->nge_ldata->nge_tx_list[cur].nge_mbuf = m_head;
	sc->nge_ldata->nge_tx_list[cur].nge_ctl &= ~NGE_CMDSTS_MORE;
	sc->nge_ldata->nge_tx_list[*txidx].nge_ctl |= NGE_CMDSTS_OWN;
	sc->nge_cdata.nge_tx_cnt += cnt;
	*txidx = frag;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

static void
nge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct nge_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL, *m_defragged;
	uint32_t idx;
	int need_trans;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if (!sc->nge_link) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	idx = sc->nge_cdata.nge_tx_prod;

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	need_trans = 0;
	while (sc->nge_ldata->nge_tx_list[idx].nge_mbuf == NULL) {
		struct mbuf *m;
		int cnt;

		m_defragged = NULL;
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

again:
		cnt = 0;
		for (m = m_head; m != NULL; m = m->m_next)
			++cnt;
		if ((NGE_TX_LIST_CNT -
		    (sc->nge_cdata.nge_tx_cnt + cnt)) < 2) {
			if (m_defragged != NULL) {
				/*
				 * Even after defragmentation, there
				 * are still too many fragments, so
				 * drop this packet.
				 */
				m_freem(m_head);
				ifq_set_oactive(&ifp->if_snd);
				break;
			}

			m_defragged = m_defrag(m_head, MB_DONTWAIT);
			if (m_defragged == NULL) {
				m_freem(m_head);
				continue;
			}
			m_head = m_defragged;

			/* Recount # of fragments */
			goto again;
		}

		nge_encap(sc, m_head, &idx);
		need_trans = 1;

		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (!need_trans)
		return;

	/* Transmit */
	sc->nge_cdata.nge_tx_prod = idx;
	NGE_SETBIT(sc, NGE_CSR, NGE_CSR_TX_ENABLE);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
nge_init(void *xsc)
{
	struct nge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	if (ifp->if_flags & IFF_RUNNING) {
		return;
	}

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	nge_stop(sc);
	callout_reset(&sc->nge_stat_timer, hz, nge_tick, sc);

	if (sc->nge_tbi)
		mii = NULL;
	else
		mii = device_get_softc(sc->nge_miibus);

	/* Set MAC address */
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR0);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA,
	    ((uint16_t *)sc->arpcom.ac_enaddr)[0]);
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR1);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA,
	    ((uint16_t *)sc->arpcom.ac_enaddr)[1]);
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR2);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA,
	    ((uint16_t *)sc->arpcom.ac_enaddr)[2]);

	/* Init circular RX list. */
	if (nge_list_rx_init(sc) == ENOBUFS) {
		kprintf("nge%d: initialization failed: no "
			"memory for rx buffers\n", sc->nge_unit);
		nge_stop(sc);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	nge_list_tx_init(sc);

	/*
	 * For the NatSemi chip, we have to explicitly enable the
	 * reception of ARP frames, as well as turn on the 'perfect
	 * match' filter where we store the station address, otherwise
	 * we won't receive unicasts meant for this host.
	 */
	NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_ARP);
	NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_PERFECT);

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_ALLPHYS);
	else
		NGE_CLRBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_ALLPHYS);

	/*
	 * Set the capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST)
		NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_BROAD);
	else
		NGE_CLRBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_BROAD);

	/*
	 * Load the multicast filter.
	 */
	nge_setmulti(sc);

	/* Turn the receive filter on */
	NGE_SETBIT(sc, NGE_RXFILT_CTL, NGE_RXFILTCTL_ENABLE);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, NGE_RX_LISTPTR,
	    vtophys(&sc->nge_ldata->nge_rx_list[0]));
	CSR_WRITE_4(sc, NGE_TX_LISTPTR,
	    vtophys(&sc->nge_ldata->nge_tx_list[0]));

	/* Set RX configuration */
	CSR_WRITE_4(sc, NGE_RX_CFG, NGE_RXCFG);
	/*
	 * Enable hardware checksum validation for all IPv4
	 * packets, do not reject packets with bad checksums.
	 */
	CSR_WRITE_4(sc, NGE_VLAN_IP_RXCTL, NGE_VIPRXCTL_IPCSUM_ENB);

	/*
	 * Tell the chip to detect and strip VLAN tag info from
	 * received frames. The tag will be provided in the extsts
	 * field in the RX descriptors.
	 */
	NGE_SETBIT(sc, NGE_VLAN_IP_RXCTL,
	    NGE_VIPRXCTL_TAG_DETECT_ENB|NGE_VIPRXCTL_TAG_STRIP_ENB);

	/* Set TX configuration */
	CSR_WRITE_4(sc, NGE_TX_CFG, NGE_TXCFG);

	/*
	 * Enable TX IPv4 checksumming on a per-packet basis.
	 */
	CSR_WRITE_4(sc, NGE_VLAN_IP_TXCTL, NGE_VIPTXCTL_CSUM_PER_PKT);

	/*
	 * Tell the chip to insert VLAN tags on a per-packet basis as
	 * dictated by the code in the frame encapsulation routine.
	 */
	NGE_SETBIT(sc, NGE_VLAN_IP_TXCTL, NGE_VIPTXCTL_TAG_PER_PKT);

	/* Set full/half duplex mode. */
	if (sc->nge_tbi) {
		if ((sc->nge_ifmedia.ifm_cur->ifm_media & IFM_GMASK) 
		    == IFM_FDX) {
			NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		} else {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		}
	} else {
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
			NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		} else {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
		}
	}

	/*
	 * Enable the delivery of PHY interrupts based on
	 * link/speed/duplex status changes. Also enable the
	 * extsts field in the DMA descriptors (needed for
	 * TCP/IP checksum offload on transmit).
	 */
	NGE_SETBIT(sc, NGE_CFG, NGE_CFG_PHYINTR_SPD |
	    NGE_CFG_PHYINTR_LNK | NGE_CFG_PHYINTR_DUP | NGE_CFG_EXTSTS_ENB);

	/*
	 * Configure interrupt holdoff (moderation). We can
	 * have the chip delay interrupt delivery for a certain
	 * period. Units are in 100us, and the max setting
	 * is 25500us (0xFF x 100us). Default is a 100us holdoff.
	 */
	CSR_WRITE_4(sc, NGE_IHR, 0x01);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, NGE_IMR, NGE_INTRS);
#ifdef IFPOLL_ENABLE
	/*
	 * ... only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (ifp->if_flags & IFF_NPOLLING) {
		CSR_WRITE_4(sc, NGE_IER, 0);
		sc->nge_npoll.ifpc_stcount = 0;
	} else
#endif /* IFPOLL_ENABLE */
	CSR_WRITE_4(sc, NGE_IER, 1);

	/* Enable receiver and transmitter. */
	NGE_CLRBIT(sc, NGE_CSR, NGE_CSR_TX_DISABLE | NGE_CSR_RX_DISABLE);
	NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);

	nge_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

/*
 * Set media options.
 */
static int
nge_ifmedia_upd(struct ifnet *ifp)
{
	struct nge_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	if (sc->nge_tbi) {
		if (IFM_SUBTYPE(sc->nge_ifmedia.ifm_cur->ifm_media) 
		     == IFM_AUTO) {
			CSR_WRITE_4(sc, NGE_TBI_ANAR, 
				CSR_READ_4(sc, NGE_TBI_ANAR)
					| NGE_TBIANAR_HDX | NGE_TBIANAR_FDX
					| NGE_TBIANAR_PS1 | NGE_TBIANAR_PS2);
			CSR_WRITE_4(sc, NGE_TBI_BMCR, NGE_TBIBMCR_ENABLE_ANEG
				| NGE_TBIBMCR_RESTART_ANEG);
			CSR_WRITE_4(sc, NGE_TBI_BMCR, NGE_TBIBMCR_ENABLE_ANEG);
		} else if ((sc->nge_ifmedia.ifm_cur->ifm_media 
			    & IFM_GMASK) == IFM_FDX) {
			NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT|NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);

			CSR_WRITE_4(sc, NGE_TBI_ANAR, 0);
			CSR_WRITE_4(sc, NGE_TBI_BMCR, 0);
		} else {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT|NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);

			CSR_WRITE_4(sc, NGE_TBI_ANAR, 0);
			CSR_WRITE_4(sc, NGE_TBI_BMCR, 0);
		}
			
		CSR_WRITE_4(sc, NGE_GPIO, CSR_READ_4(sc, NGE_GPIO)
			    & ~NGE_GPIO_GP3_OUT);
	} else {
		mii = device_get_softc(sc->nge_miibus);
		sc->nge_link = 0;
		if (mii->mii_instance) {
			struct mii_softc	*miisc;
			for (miisc = LIST_FIRST(&mii->mii_phys); miisc != NULL;
			    miisc = LIST_NEXT(miisc, mii_list))
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
nge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nge_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	if (sc->nge_tbi) {
		ifmr->ifm_status = IFM_AVALID;
		ifmr->ifm_active = IFM_ETHER;

		if (CSR_READ_4(sc, NGE_TBI_BMSR) & NGE_TBIBMSR_ANEG_DONE)
			ifmr->ifm_status |= IFM_ACTIVE;
		if (CSR_READ_4(sc, NGE_TBI_BMCR) & NGE_TBIBMCR_LOOPBACK)
			ifmr->ifm_active |= IFM_LOOP;
		if (!(CSR_READ_4(sc, NGE_TBI_BMSR) & NGE_TBIBMSR_ANEG_DONE)) {
			ifmr->ifm_active |= IFM_NONE;
			ifmr->ifm_status = 0;
			return;
		} 
		ifmr->ifm_active |= IFM_1000_SX;
		if (IFM_SUBTYPE(sc->nge_ifmedia.ifm_cur->ifm_media)
		    == IFM_AUTO) {
			ifmr->ifm_active |= IFM_AUTO;
			if (CSR_READ_4(sc, NGE_TBI_ANLPAR)
			    & NGE_TBIANAR_FDX) {
				ifmr->ifm_active |= IFM_FDX;
			}else if (CSR_READ_4(sc, NGE_TBI_ANLPAR)
				  & NGE_TBIANAR_HDX) {
				ifmr->ifm_active |= IFM_HDX;
			}
		} else if ((sc->nge_ifmedia.ifm_cur->ifm_media & IFM_GMASK) 
			== IFM_FDX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
 
	} else {
		mii = device_get_softc(sc->nge_miibus);
		mii_pollstat(mii);
		ifmr->ifm_active = mii->mii_media_active;
		ifmr->ifm_status = mii->mii_media_status;
	}
}

static int
nge_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct nge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0;

	switch(command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > NGE_JUMBO_MTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
			/*
			 * Workaround: if the MTU is larger than
			 * 8152 (TX FIFO size minus 64 minus 18), turn off
			 * TX checksum offloading.
			 */
			if (ifr->ifr_mtu >= 8152)
				ifp->if_hwassist = 0;
			else
				ifp->if_hwassist = NGE_CSUM_FEATURES;
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->nge_if_flags & IFF_PROMISC)) {
				NGE_SETBIT(sc, NGE_RXFILT_CTL,
				    NGE_RXFILTCTL_ALLPHYS|
				    NGE_RXFILTCTL_ALLMULTI);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->nge_if_flags & IFF_PROMISC) {
				NGE_CLRBIT(sc, NGE_RXFILT_CTL,
				    NGE_RXFILTCTL_ALLPHYS);
				if (!(ifp->if_flags & IFF_ALLMULTI))
					NGE_CLRBIT(sc, NGE_RXFILT_CTL,
					    NGE_RXFILTCTL_ALLMULTI);
			} else {
				ifp->if_flags &= ~IFF_RUNNING;
				nge_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				nge_stop(sc);
		}
		sc->nge_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		nge_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->nge_tbi) {
			error = ifmedia_ioctl(ifp, ifr, &sc->nge_ifmedia, 
					      command);
		} else {
			mii = device_get_softc(sc->nge_miibus);
			error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, 
					      command);
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

static void
nge_watchdog(struct ifnet *ifp)
{
	struct nge_softc *sc = ifp->if_softc;

	IFNET_STAT_INC(ifp, oerrors, 1);
	kprintf("nge%d: watchdog timeout\n", sc->nge_unit);

	nge_stop(sc);
	nge_reset(sc);
	ifp->if_flags &= ~IFF_RUNNING;
	nge_init(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
nge_stop(struct nge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ifmedia_entry *ifm;
	struct mii_data *mii;
	int i, itmp, mtmp, dtmp;

	ifp->if_timer = 0;
	if (sc->nge_tbi)
		mii = NULL;
	else
		mii = device_get_softc(sc->nge_miibus);

	callout_stop(&sc->nge_stat_timer);
	CSR_WRITE_4(sc, NGE_IER, 0);
	CSR_WRITE_4(sc, NGE_IMR, 0);
	NGE_SETBIT(sc, NGE_CSR, NGE_CSR_TX_DISABLE|NGE_CSR_RX_DISABLE);
	DELAY(1000);
	CSR_WRITE_4(sc, NGE_TX_LISTPTR, 0);
	CSR_WRITE_4(sc, NGE_RX_LISTPTR, 0);

	/*
	 * Isolate/power down the PHY, but leave the media selection
	 * unchanged so that things will be put back to normal when
	 * we bring the interface back up.
	 */
	itmp = ifp->if_flags;
	ifp->if_flags |= IFF_UP;

	if (sc->nge_tbi)
		ifm = sc->nge_ifmedia.ifm_cur;
	else
		ifm = mii->mii_media.ifm_cur;

	mtmp = ifm->ifm_media;
	dtmp = ifm->ifm_data;
	ifm->ifm_media = IFM_ETHER|IFM_NONE;
	ifm->ifm_data = MII_MEDIA_NONE;

	if (!sc->nge_tbi)
		mii_mediachg(mii);
	ifm->ifm_media = mtmp;
	ifm->ifm_data = dtmp;
	ifp->if_flags = itmp;

	sc->nge_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < NGE_RX_LIST_CNT; i++) {
		if (sc->nge_ldata->nge_rx_list[i].nge_mbuf != NULL) {
			m_freem(sc->nge_ldata->nge_rx_list[i].nge_mbuf);
			sc->nge_ldata->nge_rx_list[i].nge_mbuf = NULL;
		}
	}
	bzero(&sc->nge_ldata->nge_rx_list,
		sizeof(sc->nge_ldata->nge_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < NGE_TX_LIST_CNT; i++) {
		if (sc->nge_ldata->nge_tx_list[i].nge_mbuf != NULL) {
			m_freem(sc->nge_ldata->nge_tx_list[i].nge_mbuf);
			sc->nge_ldata->nge_tx_list[i].nge_mbuf = NULL;
		}
	}

	bzero(&sc->nge_ldata->nge_tx_list,
		sizeof(sc->nge_ldata->nge_tx_list));

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
nge_shutdown(device_t dev)
{
	struct nge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	nge_reset(sc);
	nge_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

