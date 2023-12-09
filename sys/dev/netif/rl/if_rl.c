/*
 * Copyright (c) 1997, 1998
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
 * $FreeBSD: src/sys/pci/if_rl.c,v 1.38.2.16 2003/03/05 18:42:33 njl Exp $
 */

/*
 * RealTek 8129/8139 PCI NIC driver
 *
 * Supports several extremely cheap PCI 10/100 adapters based on
 * the RealTek chipset. Datasheets can be obtained from
 * www.realtek.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The RealTek 8139 PCI NIC redefines the meaning of 'low end.' This is
 * probably the worst PCI ethernet controller ever made, with the possible
 * exception of the FEAST chip made by SMC. The 8139 supports bus-master
 * DMA, but it has a terrible interface that nullifies any performance
 * gains that bus-master DMA usually offers.
 *
 * For transmission, the chip offers a series of four TX descriptor
 * registers. Each transmit frame must be in a contiguous buffer, aligned
 * on a longword (32-bit) boundary. This means we almost always have to
 * do mbuf copies in order to transmit a frame, except in the unlikely
 * case where a) the packet fits into a single mbuf, and b) the packet
 * is 32-bit aligned within the mbuf's data area. The presence of only
 * four descriptor registers means that we can never have more than four
 * packets queued for transmission at any one time.
 *
 * Reception is not much better. The driver has to allocate a single large
 * buffer area (up to 64K in size) into which the chip will DMA received
 * frames. Because we don't know where within this region received packets
 * will begin or end, we have no choice but to copy data from the buffer
 * area into mbufs in order to pass the packets up to the higher protocol
 * levels.
 *
 * It's impossible given this rotten design to really achieve decent
 * performance at 100Mbps, unless you happen to have a 400Mhz PII or
 * some equally overmuscled CPU to drive it.
 *
 * On the bright side, the 8139 does have a built-in PHY, although
 * rather than using an MDIO serial interface like most other NICs, the
 * PHY registers are directly accessible through the 8139's register
 * space. The 8139 supports autonegotiation, as well as a 64-bit multicast
 * filter.
 *
 * The 8129 chip is an older version of the 8139 that uses an external PHY
 * chip. The 8129 has a serial MDIO interface for accessing the MII where
 * the 8139 lets you directly access the on-board PHY registers. We need
 * to select which interface to use depending on the chip type.
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/serialize.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/interrupt.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>

#include <net/bpf.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Default to using PIO access for this driver. On SMP systems,
 * there appear to be problems with memory mapped mode: it looks like
 * doing too many memory mapped access back to back in rapid succession
 * can hang the bus. I'm inclined to blame this on crummy design/construction
 * on the part of RealTek. Memory mapped mode does appear to work on
 * uniprocessor systems though.
 */
#define RL_USEIOSPACE

#include <dev/netif/rl/if_rlreg.h>

/*
 * Various supported device vendors/types and their names.
 */
static struct rl_type {
	uint16_t	 rl_vid;
	uint16_t	 rl_did;
	const char	*rl_name;
} rl_devs[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8129,
		"RealTek 8129 10/100BaseTX" },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8139,
		"RealTek 8139 10/100BaseTX" },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8139B,
		"RealTek 8139 10/100BaseTX CardBus" },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_MPX5030,
		"Accton MPX 5030/5038 10/100BaseTX" },
	{ PCI_VENDOR_DELTA, PCI_PRODUCT_DELTA_8139,
		"Delta Electronics 8139 10/100BaseTX" },
	{ PCI_VENDOR_ADDTRON, PCI_PRODUCT_ADDTRON_8139,
		"Addtron Technology 8139 10/100BaseTX" },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE520TX_C1,
		"D-Link DFE-520TX C1 10/100BaseTX" },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE530TXPLUS,
		"D-Link DFE-530TX+ 10/100BaseTX" },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE690TXD,
		"D-Link DFE-690TX 10/100BaseTX" },
	{ PCI_VENDOR_NORTEL, PCI_PRODUCT_NORTEL_BAYSTACK_21,
		"Nortel Networks 10/100BaseTX" },
	{ PCI_VENDOR_PEPPERCON, PCI_PRODUCT_PEPPERCON_ROLF,
		"Peppercon AG ROL/F" },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CB_TXD,
		"Corega FEther CB-TXD" },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_2CB_TXD,
		"Corega FEtherII CB-TXD" },
	{ PCI_VENDOR_PLANEX, PCI_PRODUCT_PLANEX_FNW_3800_TX,
		"Planex FNW-3800-TX" },
	{ 0, 0, NULL }
};

static int	rl_probe(device_t);
static int	rl_attach(device_t);
static int	rl_detach(device_t);

static int	rl_encap(struct rl_softc *, struct mbuf * );

static void	rl_rxeof(struct rl_softc *);
static void	rl_txeof(struct rl_softc *);
static void	rl_intr(void *);
static void	rl_tick(void *);
static void	rl_start(struct ifnet *, struct ifaltq_subque *);
static int	rl_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	rl_init(void *);
static void	rl_stop	(struct rl_softc *);
static void	rl_watchdog(struct ifnet *);
static int	rl_suspend(device_t);
static int	rl_resume(device_t);
static void	rl_shutdown(device_t);
static int	rl_ifmedia_upd(struct ifnet *);
static void	rl_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void	rl_eeprom_putbyte(struct rl_softc *, int);
static void	rl_eeprom_getword(struct rl_softc *, int, uint16_t *);
static void	rl_read_eeprom(struct rl_softc *, caddr_t, int, int, int);
static void	rl_mii_sync(struct rl_softc *);
static void	rl_mii_send(struct rl_softc *, uint32_t, int);
static int	rl_mii_readreg(struct rl_softc *, struct rl_mii_frame *);
static int	rl_mii_writereg(struct rl_softc *, struct rl_mii_frame *);

static int	rl_miibus_readreg(device_t, int, int);
static int	rl_miibus_writereg(device_t, int, int, int);
static void	rl_miibus_statchg(device_t);

static void	rl_setmulti(struct rl_softc *);
static void	rl_reset(struct rl_softc *);
static void	rl_list_tx_init(struct rl_softc *);

#ifdef IFPOLL_ENABLE
static void	rl_npoll(struct ifnet *, struct ifpoll_info *);
static void	rl_npoll_compat(struct ifnet *, void *, int);
#endif

static int	rl_dma_alloc(struct rl_softc *);
static void	rl_dma_free(struct rl_softc *);

#ifdef RL_USEIOSPACE
#define	RL_RES			SYS_RES_IOPORT
#define	RL_RID			RL_PCI_LOIO
#else
#define	RL_RES			SYS_RES_MEMORY
#define	RL_RID			RL_PCI_LOMEM
#endif

static device_method_t rl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rl_probe),
	DEVMETHOD(device_attach,	rl_attach),
	DEVMETHOD(device_detach,	rl_detach),
	DEVMETHOD(device_suspend,	rl_suspend),
	DEVMETHOD(device_resume,	rl_resume),
	DEVMETHOD(device_shutdown,	rl_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	rl_miibus_readreg),
	DEVMETHOD(miibus_writereg,	rl_miibus_writereg),
	DEVMETHOD(miibus_statchg,	rl_miibus_statchg),

	DEVMETHOD_END
};

static DEFINE_CLASS_0(rl, rl_driver, rl_methods, sizeof(struct rl_softc));
static devclass_t rl_devclass;

DECLARE_DUMMY_MODULE(if_rl);
DRIVER_MODULE(if_rl, pci, rl_driver, rl_devclass, NULL, NULL);
DRIVER_MODULE(if_rl, cardbus, rl_driver, rl_devclass, NULL, NULL);
DRIVER_MODULE(miibus, rl, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_DEPEND(if_rl, miibus, 1, 1, 1);

#define EE_SET(x)					\
	CSR_WRITE_1(sc, RL_EECMD, CSR_READ_1(sc, RL_EECMD) | (x))

#define EE_CLR(x)					\
	CSR_WRITE_1(sc, RL_EECMD, CSR_READ_1(sc, RL_EECMD) & ~(x))

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
rl_eeprom_putbyte(struct rl_softc *sc, int addr)
{
	int d, i;

	d = addr | sc->rl_eecmd_read;

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i)
			EE_SET(RL_EE_DATAIN);
		else
			EE_CLR(RL_EE_DATAIN);
		DELAY(100);
		EE_SET(RL_EE_CLK);
		DELAY(150);
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
rl_eeprom_getword(struct rl_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/* Enter EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Send address of word we want to read.
	 */
	rl_eeprom_putbyte(sc, addr);

	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		EE_SET(RL_EE_CLK);
		DELAY(100);
		if (CSR_READ_1(sc, RL_EECMD) & RL_EE_DATAOUT)
			word |= i;
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}

	/* Turn off EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
rl_read_eeprom(struct rl_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int i;
	u_int16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		rl_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
}


/*
 * MII access routines are provided for the 8129, which
 * doesn't have a built-in PHY. For the 8139, we fake things
 * up by diverting rl_phy_readreg()/rl_phy_writereg() to the
 * direct access PHY registers.
 */
#define MII_SET(x)							\
	CSR_WRITE_1(sc, RL_MII, CSR_READ_1(sc, RL_MII) | x)

#define MII_CLR(x)							\
	CSR_WRITE_1(sc, RL_MII, CSR_READ_1(sc, RL_MII) & ~x)

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
rl_mii_sync(struct rl_softc *sc)
{
	int i;

	MII_SET(RL_MII_DIR|RL_MII_DATAOUT);

	for (i = 0; i < 32; i++) {
		MII_SET(RL_MII_CLK);
		DELAY(1);
		MII_CLR(RL_MII_CLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
rl_mii_send(struct rl_softc *sc, uint32_t bits, int cnt)
{
	int i;

	MII_CLR(RL_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i)
			MII_SET(RL_MII_DATAOUT);
		else
			MII_CLR(RL_MII_DATAOUT);
		DELAY(1);
		MII_CLR(RL_MII_CLK);
		DELAY(1);
		MII_SET(RL_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
static int
rl_mii_readreg(struct rl_softc *sc, struct rl_mii_frame *frame)	
{
	int ack, i;

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = RL_MII_STARTDELIM;
	frame->mii_opcode = RL_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_2(sc, RL_MII, 0);

	/*
 	 * Turn on data xmit.
	 */
	MII_SET(RL_MII_DIR);

	rl_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	rl_mii_send(sc, frame->mii_stdelim, 2);
	rl_mii_send(sc, frame->mii_opcode, 2);
	rl_mii_send(sc, frame->mii_phyaddr, 5);
	rl_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	MII_CLR((RL_MII_CLK|RL_MII_DATAOUT));
	DELAY(1);
	MII_SET(RL_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	MII_CLR(RL_MII_DIR);

	/* Check for ack */
	MII_CLR(RL_MII_CLK);
	DELAY(1);
	ack = CSR_READ_2(sc, RL_MII) & RL_MII_DATAIN;
	MII_SET(RL_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(RL_MII_CLK);
			DELAY(1);
			MII_SET(RL_MII_CLK);
			DELAY(1);
		}
	} else {
		for (i = 0x8000; i; i >>= 1) {
			MII_CLR(RL_MII_CLK);
			DELAY(1);
			if (!ack) {
				if (CSR_READ_2(sc, RL_MII) & RL_MII_DATAIN)
					frame->mii_data |= i;
				DELAY(1);
			}
			MII_SET(RL_MII_CLK);
			DELAY(1);
		}
	}

	MII_CLR(RL_MII_CLK);
	DELAY(1);
	MII_SET(RL_MII_CLK);
	DELAY(1);

	return(ack ? 1 : 0);
}

/*
 * Write to a PHY register through the MII.
 */
static int
rl_mii_writereg(struct rl_softc *sc, struct rl_mii_frame *frame)
{
	/*
	 * Set up frame for TX.
	 */
	frame->mii_stdelim = RL_MII_STARTDELIM;
	frame->mii_opcode = RL_MII_WRITEOP;
	frame->mii_turnaround = RL_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	MII_SET(RL_MII_DIR);

	rl_mii_sync(sc);

	rl_mii_send(sc, frame->mii_stdelim, 2);
	rl_mii_send(sc, frame->mii_opcode, 2);
	rl_mii_send(sc, frame->mii_phyaddr, 5);
	rl_mii_send(sc, frame->mii_regaddr, 5);
	rl_mii_send(sc, frame->mii_turnaround, 2);
	rl_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(RL_MII_CLK);
	DELAY(1);
	MII_CLR(RL_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(RL_MII_DIR);

	return(0);
}

static int
rl_miibus_readreg(device_t dev, int phy, int reg)
{
	struct rl_softc *sc;
	struct rl_mii_frame frame;
	uint16_t rval = 0;
	uint16_t rl8139_reg = 0;

	sc = device_get_softc(dev);

	if (sc->rl_type == RL_8139) {
		/* Pretend the internal PHY is only at address 0 */
		if (phy)
			return(0);
		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return(0);
			break;
		/*
		 * Allow the rlphy driver to read the media status
		 * register. If we have a link partner which does not
		 * support NWAY, this is the register which will tell
		 * us the results of parallel detection.
		 */
		case RL_MEDIASTAT:
			rval = CSR_READ_1(sc, RL_MEDIASTAT);
			return(rval);
		default:
			device_printf(dev, "bad phy register\n");
			return(0);
		}
		rval = CSR_READ_2(sc, rl8139_reg);
		return(rval);
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	rl_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

static int
rl_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct rl_softc *sc;
	struct rl_mii_frame frame;
	u_int16_t rl8139_reg = 0;

	sc = device_get_softc(dev);

	if (sc->rl_type == RL_8139) {
		/* Pretend the internal PHY is only at address 0 */
		if (phy)
			return(0);
		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return(0);
		default:
			device_printf(dev, "bad phy register\n");
			return(0);
		}
		CSR_WRITE_2(sc, rl8139_reg, data);
		return(0);
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	rl_mii_writereg(sc, &frame);

	return(0);
}

static void
rl_miibus_statchg(device_t dev)
{
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
rl_setmulti(struct rl_softc *sc)
{
	struct ifnet *ifp;
	int h = 0;
	uint32_t hashes[2] = { 0, 0 };
	struct ifmultiaddr *ifma;
	uint32_t rxfilt;
	int mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	rxfilt = CSR_READ_4(sc, RL_RXCFG);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= RL_RXCFG_RX_MULTI;
		CSR_WRITE_4(sc, RL_RXCFG, rxfilt);
		CSR_WRITE_4(sc, RL_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, RL_MAR4, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, RL_MAR0, 0);
	CSR_WRITE_4(sc, RL_MAR4, 0);

	/* now program new ones */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}

	if (mcnt)
		rxfilt |= RL_RXCFG_RX_MULTI;
	else
		rxfilt &= ~RL_RXCFG_RX_MULTI;

	CSR_WRITE_4(sc, RL_RXCFG, rxfilt);
	CSR_WRITE_4(sc, RL_MAR0, hashes[0]);
	CSR_WRITE_4(sc, RL_MAR4, hashes[1]);
}

static void
rl_reset(struct rl_softc *sc)
{
	int i;

	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_RESET);

	for (i = 0; i < RL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_1(sc, RL_COMMAND) & RL_CMD_RESET))
			break;
	}
	if (i == RL_TIMEOUT)
		device_printf(sc->rl_dev, "reset never completed!\n");
}

/*
 * Probe for a RealTek 8129/8139 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 *
 * Return with a value < 0 to give re(4) a change to attach.
 */
static int
rl_probe(device_t dev)
{
	struct rl_type *t;
	uint16_t product = pci_get_device(dev);
	uint16_t vendor = pci_get_vendor(dev);

	for (t = rl_devs; t->rl_name != NULL; t++) {
		if (vendor == t->rl_vid && product == t->rl_did) {
			device_set_desc(dev, t->rl_name);
			return(-100);
		}
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
rl_attach(device_t dev)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint16_t as[3];
	struct rl_softc *sc;
	struct ifnet *ifp;
	uint16_t rl_did = 0;
	int error = 0, rid, i;

	sc = device_get_softc(dev);
	sc->rl_dev = dev;

	/*
	 * Handle power management nonsense.
	 */

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t iobase, membase, irq;

		/* Save important PCI config data. */
		iobase = pci_read_config(dev, RL_PCI_LOIO, 4);
		membase = pci_read_config(dev, RL_PCI_LOMEM, 4);
		irq = pci_read_config(dev, RL_PCI_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
			      "-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, RL_PCI_LOIO, iobase, 4);
		pci_write_config(dev, RL_PCI_LOMEM, membase, 4);
		pci_write_config(dev, RL_PCI_INTLINE, irq, 4);
	}

	pci_enable_busmaster(dev);

	rid = RL_RID; 
	sc->rl_res = bus_alloc_resource_any(dev, RL_RES, &rid, RF_ACTIVE);

	if (sc->rl_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->rl_btag = rman_get_bustag(sc->rl_res);
	sc->rl_bhandle = rman_get_bushandle(sc->rl_res);

	rid = 0;
	sc->rl_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					    RF_SHAREABLE | RF_ACTIVE);

	if (sc->rl_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	callout_init(&sc->rl_stat_timer);

	/* Reset the adapter. */
	rl_reset(sc);

	sc->rl_eecmd_read = RL_EECMD_READ_6BIT;
	rl_read_eeprom(sc, (uint8_t *)&rl_did, 0, 1, 0);
	if (rl_did != 0x8129)
		sc->rl_eecmd_read = RL_EECMD_READ_8BIT;

	/*
	 * Get station address from the EEPROM.
	 */
	rl_read_eeprom(sc, (caddr_t)as, RL_EE_EADDR, 3, 0);
	for (i = 0; i < 3; i++) {
		eaddr[(i * 2) + 0] = as[i] & 0xff;
		eaddr[(i * 2) + 1] = as[i] >> 8;
	}

	/*
	 * Now read the exact device type from the EEPROM to find
	 * out if it's an 8129 or 8139.
	 */
	rl_read_eeprom(sc, (caddr_t)&rl_did, RL_EE_PCI_DID, 1, 0);

	if (rl_did == PCI_PRODUCT_REALTEK_RT8139 ||
	    rl_did == PCI_PRODUCT_ACCTON_MPX5030 ||
	    rl_did == PCI_PRODUCT_DELTA_8139 ||
	    rl_did == PCI_PRODUCT_ADDTRON_8139 ||
	    rl_did == PCI_PRODUCT_DLINK_DFE530TXPLUS ||
	    rl_did == PCI_PRODUCT_REALTEK_RT8139B ||
	    rl_did == PCI_PRODUCT_DLINK_DFE690TXD || 
	    rl_did == PCI_PRODUCT_COREGA_CB_TXD ||
	    rl_did == PCI_PRODUCT_COREGA_2CB_TXD ||
	    rl_did == PCI_PRODUCT_PLANEX_FNW_3800_TX) {
		sc->rl_type = RL_8139;
	} else if (rl_did == PCI_PRODUCT_REALTEK_RT8129) {
		sc->rl_type = RL_8129;
	} else {
		device_printf(dev, "unknown device ID: %x\n", rl_did);
		sc->rl_type = RL_8139;
		/*
		 * Read RL_IDR register to get ethernet address as accessing
		 * EEPROM may not extract correct address.
		 */
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			eaddr[i] = CSR_READ_1(sc, RL_IDR0 + i);
	}

	error = rl_dma_alloc(sc);
	if (error)
		goto fail;

	/* Do MII setup */
	if (mii_phy_probe(dev, &sc->rl_miibus, rl_ifmedia_upd,
			  rl_ifmedia_sts)) {
		device_printf(dev, "MII without any phy!\n");
		error = ENXIO;
		goto fail;
	}

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rl_ioctl;
	ifp->if_start = rl_start;
	ifp->if_watchdog = rl_watchdog;
	ifp->if_init = rl_init;
	ifp->if_baudrate = 10000000;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = rl_npoll;
#endif
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->rl_irq));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->rl_npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif

	error = bus_setup_intr(dev, sc->rl_irq, INTR_MPSAFE, rl_intr,
			       sc, &sc->rl_intrhand, ifp->if_serializer);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	return(0);

fail:
	rl_detach(dev);
	return(error);
}

static int
rl_detach(device_t dev)
{
	struct rl_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = &sc->arpcom.ac_if;

	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		rl_stop(sc);
		bus_teardown_intr(dev, sc->rl_irq, sc->rl_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->rl_miibus)
		device_delete_child(dev, sc->rl_miibus);
	bus_generic_detach(dev);

	if (sc->rl_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->rl_irq);
	if (sc->rl_res)
		bus_release_resource(dev, RL_RES, RL_RID, sc->rl_res);

	rl_dma_free(sc);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static void
rl_list_tx_init(struct rl_softc *sc)
{
	struct rl_chain_data *cd;
	int i;

	cd = &sc->rl_cdata;
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		cd->rl_tx_chain[i] = NULL;
		CSR_WRITE_4(sc, RL_TXADDR0 + (i * sizeof(uint32_t)),
			    0x0000000);
	}

	sc->rl_cdata.cur_tx = 0;
	sc->rl_cdata.last_tx = 0;
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 *
 * You know there's something wrong with a PCI bus-master chip design
 * when you have to use m_devget().
 *
 * The receive operation is badly documented in the datasheet, so I'll
 * attempt to document it here. The driver provides a buffer area and
 * places its base address in the RX buffer start address register.
 * The chip then begins copying frames into the RX buffer. Each frame
 * is preceded by a 32-bit RX status word which specifies the length
 * of the frame and certain other status bits. Each frame (starting with
 * the status word) is also 32-bit aligned. The frame length is in the
 * first 16 bits of the status word; the lower 15 bits correspond with
 * the 'rx status register' mentioned in the datasheet.
 *
 * Note: to make the Alpha happy, the frame payload needs to be aligned
 * on a 32-bit boundary. To achieve this, we cheat a bit by copying from
 * the ring buffer starting at an address two bytes before the actual
 * data location. We can then shave off the first two bytes using m_adj().
 * The reason we do this is because m_devget() doesn't let us specify an
 * offset into the mbuf storage space, so we have to artificially create
 * one. The ring is allocated in such a way that there are a few unused
 * bytes of space preceecing it so that it will be safe for us to do the
 * 2-byte backstep even if reading from the ring at offset 0.
 */
static void
rl_rxeof(struct rl_softc *sc)
{
        struct mbuf *m;
        struct ifnet *ifp;
	int total_len = 0;
	uint32_t rxstat;
	caddr_t rxbufpos;
	int wrap = 0, done = 0;
	uint16_t cur_rx = 0, max_bytes = 0, rx_bytes = 0;

	ifp = &sc->arpcom.ac_if;

	while((CSR_READ_1(sc, RL_COMMAND) & RL_CMD_EMPTY_RXBUF) == 0) {
		if (!done) {
			uint16_t limit;

			done = 1;

			cur_rx = (CSR_READ_2(sc, RL_CURRXADDR) + 16) %
			    RL_RXBUFLEN;

			/* Do not try to read past this point. */
			limit = CSR_READ_2(sc, RL_CURRXBUF) % RL_RXBUFLEN;
			if (limit < cur_rx)
				max_bytes = (RL_RXBUFLEN - cur_rx) + limit;
			else
				max_bytes = limit - cur_rx;
		}
#ifdef IFPOLL_ENABLE
		if (ifp->if_flags & IFF_NPOLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif /* IFPOLL_ENABLE */
		rxbufpos = sc->rl_cdata.rl_rx_buf + cur_rx;
		rxstat = le32toh(*(uint32_t *)rxbufpos);

		/*
		 * Here's a totally undocumented fact for you. When the
		 * RealTek chip is in the process of copying a packet into
		 * RAM for you, the length will be 0xfff0. If you spot a
		 * packet header with this value, you need to stop. The
		 * datasheet makes absolutely no mention of this and
		 * RealTek should be shot for this.
		 */
		if ((uint16_t)(rxstat >> 16) == RL_RXSTAT_UNFINISHED)
			break;
	
		if ((rxstat & RL_RXSTAT_RXOK) == 0) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			rl_init(sc);
			return;
		}

		/* No errors; receive the packet. */	
		total_len = rxstat >> 16;
		rx_bytes += total_len + 4;

		/*
		 * XXX The RealTek chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
	 	 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		/*
		 * Avoid trying to read more bytes than we know
		 * the chip has prepared for us.
		 */
		if (rx_bytes > max_bytes)
			break;

		rxbufpos = sc->rl_cdata.rl_rx_buf +
			((cur_rx + sizeof(uint32_t)) % RL_RXBUFLEN);

		if (rxbufpos == (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN))
			rxbufpos = sc->rl_cdata.rl_rx_buf;

		wrap = (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN) - rxbufpos;

		if (total_len > wrap) {
			/*
			 * Fool m_devget() into thinking we want to copy
			 * the whole buffer so we don't end up fragmenting
			 * the data.
			 */
			m = m_devget(rxbufpos - RL_ETHER_ALIGN,
				     wrap + RL_ETHER_ALIGN, 0, ifp);
			if (m == NULL) {
				IFNET_STAT_INC(ifp, ierrors, 1);
			} else {
				m_adj(m, RL_ETHER_ALIGN);
				m_copyback(m, wrap, total_len - wrap,
					sc->rl_cdata.rl_rx_buf);
			}
			cur_rx = (total_len - wrap + ETHER_CRC_LEN);
		} else {
			m = m_devget(rxbufpos - RL_ETHER_ALIGN,
				     total_len + RL_ETHER_ALIGN, 0, ifp);
			if (m == NULL) {
				IFNET_STAT_INC(ifp, ierrors, 1);
			} else
				m_adj(m, RL_ETHER_ALIGN);
			cur_rx += total_len + 4 + ETHER_CRC_LEN;
		}

		/*
		 * Round up to 32-bit boundary.
		 */
		cur_rx = (cur_rx + 3) & ~3;
		CSR_WRITE_2(sc, RL_CURRXADDR, cur_rx - 16);

		if (m == NULL)
			continue;

		IFNET_STAT_INC(ifp, ipackets, 1);

		ifp->if_input(ifp, m, NULL, -1);
	}
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
rl_txeof(struct rl_softc *sc)
{
	struct ifnet *ifp;
	uint32_t txstat;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been uploaded.
	 */
	do {
		if (RL_LAST_TXMBUF(sc) == NULL)
			break;
		txstat = CSR_READ_4(sc, RL_LAST_TXSTAT(sc));
		if ((txstat & (RL_TXSTAT_TX_OK | RL_TXSTAT_TX_UNDERRUN |
			       RL_TXSTAT_TXABRT)) == 0)
			break;

		IFNET_STAT_INC(ifp, collisions,
		    (txstat & RL_TXSTAT_COLLCNT) >> 24);

		bus_dmamap_unload(sc->rl_cdata.rl_tx_tag, RL_LAST_DMAMAP(sc));
		m_freem(RL_LAST_TXMBUF(sc));
		RL_LAST_TXMBUF(sc) = NULL;
		RL_INC(sc->rl_cdata.last_tx);

		if (txstat & RL_TXSTAT_TX_UNDERRUN) {
			sc->rl_txthresh += 32;
			if (sc->rl_txthresh > RL_TX_THRESH_MAX)
				sc->rl_txthresh = RL_TX_THRESH_MAX;
		}

		if (txstat & RL_TXSTAT_TX_OK) {
			IFNET_STAT_INC(ifp, opackets, 1);
		} else {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (txstat & (RL_TXSTAT_TXABRT | RL_TXSTAT_OUTOFWIN))
				CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
		}
		ifq_clr_oactive(&ifp->if_snd);
	} while (sc->rl_cdata.last_tx != sc->rl_cdata.cur_tx);

	if (RL_LAST_TXMBUF(sc) == NULL)
		ifp->if_timer = 0;
	else if (ifp->if_timer == 0)
		ifp->if_timer = 5;
}

static void
rl_tick(void *xsc)
{
	struct rl_softc *sc = xsc;
	struct mii_data *mii;

	lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);

	mii = device_get_softc(sc->rl_miibus);
	mii_tick(mii);

	callout_reset(&sc->rl_stat_timer, hz, rl_tick, sc);

	lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
}

#ifdef IFPOLL_ENABLE

static void
rl_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct rl_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	sc->rxcycles = count;
	rl_rxeof(sc);
	rl_txeof(sc);
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	if (sc->rl_npoll.ifpc_stcount-- == 0) {
		uint16_t status;

		sc->rl_npoll.ifpc_stcount = sc->rl_npoll.ifpc_stfrac;
 
		status = CSR_READ_2(sc, RL_ISR);
		if (status == 0xffff)
			return;
		if (status)
			CSR_WRITE_2(sc, RL_ISR, status);
		 
		/*
		 * XXX check behaviour on receiver stalls.
		 */

		if (status & RL_ISR_SYSTEM_ERR) {
			rl_reset(sc);
			rl_init(sc);
		}
	}
}

static void
rl_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct rl_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->rl_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = rl_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			/* disable interrupts */
			CSR_WRITE_2(sc, RL_IMR, 0x0000);
			sc->rl_npoll.ifpc_stcount = 0;
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING) {
			/* enable interrupts */
			CSR_WRITE_2(sc, RL_IMR, RL_INTRS);
		}
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->rl_irq));
	}
}

#endif /* IFPOLL_ENABLE */

static void
rl_intr(void *arg)
{
	struct rl_softc *sc;
	struct ifnet *ifp;
	uint16_t status;

	sc = arg;

	if (sc->suspended)
		return;

	ifp = &sc->arpcom.ac_if;

	for (;;) {
		status = CSR_READ_2(sc, RL_ISR);
		/* If the card has gone away, the read returns 0xffff. */
		if (status == 0xffff)
			break;

		if (status != 0)
			CSR_WRITE_2(sc, RL_ISR, status);

		if ((status & RL_INTRS) == 0)
			break;

		if (status & RL_ISR_RX_OK)
			rl_rxeof(sc);

		if (status & RL_ISR_RX_ERR)
			rl_rxeof(sc);

		if ((status & RL_ISR_TX_OK) || (status & RL_ISR_TX_ERR))
			rl_txeof(sc);

		if (status & RL_ISR_SYSTEM_ERR) {
			rl_reset(sc);
			rl_init(sc);
		}

	}

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
rl_encap(struct rl_softc *sc, struct mbuf *m_head)
{
	struct mbuf *m_new = NULL;
	bus_dma_segment_t seg;
	int nseg, error;

	/*
	 * The RealTek is brain damaged and wants longword-aligned
	 * TX buffers, plus we can only have one fragment buffer
	 * per packet.  We have to copy pretty much all the time.
	 */
	m_new = m_defrag(m_head, M_NOWAIT);
	if (m_new == NULL) {
		m_freem(m_head);
		return ENOBUFS;
	}
	m_head = m_new;

	/* Pad frames to at least 60 bytes. */
	if (m_head->m_pkthdr.len < RL_MIN_FRAMELEN) {
		error = m_devpad(m_head, RL_MIN_FRAMELEN);
		if (error) {
			m_freem(m_head);
			return error;
		}
	}

	/* Extract physical address. */
	error = bus_dmamap_load_mbuf_segment(sc->rl_cdata.rl_tx_tag,
			RL_CUR_DMAMAP(sc), m_head,
			&seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_head);
		return error;
	}

	/* Sync the loaded TX buffer. */
	bus_dmamap_sync(sc->rl_cdata.rl_tx_tag, RL_CUR_DMAMAP(sc),
			BUS_DMASYNC_PREWRITE);

	/* Transmit */
	CSR_WRITE_4(sc, RL_CUR_TXADDR(sc), seg.ds_addr);
	CSR_WRITE_4(sc, RL_CUR_TXSTAT(sc),
		    RL_TXTHRESH(sc->rl_txthresh) | seg.ds_len);

	RL_CUR_TXMBUF(sc) = m_head;
	return 0;
}

/*
 * Main transmit routine.
 */

static void
rl_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct rl_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	while (RL_CUR_TXMBUF(sc) == NULL) {
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (rl_encap(sc, m_head))
			continue;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, RL_CUR_TXMBUF(sc));

		RL_INC(sc->rl_cdata.cur_tx);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		ifp->if_timer = 5;
	}

	/*
	 * We broke out of the loop because all our TX slots are
	 * full. Mark the NIC as busy until it drains some of the
	 * packets from the queue.
	 */
	if (RL_CUR_TXMBUF(sc) != NULL)
		ifq_set_oactive(&ifp->if_snd);
}

static void
rl_init(void *xsc)
{
	struct rl_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	uint32_t rxcfg = 0;

	mii = device_get_softc(sc->rl_miibus);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	rl_stop(sc);

	/*
	 * Init our MAC address.  Even though the chipset documentation
	 * doesn't mention it, we need to enter "Config register write enable"
	 * mode to modify the ID registers.
	 */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);
	CSR_WRITE_STREAM_4(sc, RL_IDR0,
			   *(uint32_t *)(&sc->arpcom.ac_enaddr[0]));
	CSR_WRITE_STREAM_4(sc, RL_IDR4,
			   *(uint32_t *)(&sc->arpcom.ac_enaddr[4]));
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	/* Init the RX buffer pointer register. */
	CSR_WRITE_4(sc, RL_RXADDR, sc->rl_cdata.rl_rx_buf_paddr);

	/* Init TX descriptors. */
	rl_list_tx_init(sc);

	/*
	 * Enable transmit and receive.
	 */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	/*
	 * Set the initial TX and RX configuration.
	 */
	CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
	CSR_WRITE_4(sc, RL_RXCFG, RL_RXCFG_CONFIG);

	/* Set the individual bit to receive frames for this host only. */
	rxcfg = CSR_READ_4(sc, RL_RXCFG);
	rxcfg |= RL_RXCFG_RX_INDIV;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		rxcfg |= RL_RXCFG_RX_ALLPHYS;
		CSR_WRITE_4(sc, RL_RXCFG, rxcfg);
	} else {
		rxcfg &= ~RL_RXCFG_RX_ALLPHYS;
		CSR_WRITE_4(sc, RL_RXCFG, rxcfg);
	}

	/*
	 * Set capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		rxcfg |= RL_RXCFG_RX_BROAD;
		CSR_WRITE_4(sc, RL_RXCFG, rxcfg);
	} else {
		rxcfg &= ~RL_RXCFG_RX_BROAD;
		CSR_WRITE_4(sc, RL_RXCFG, rxcfg);
	}

	/*
	 * Program the multicast filter, if necessary.
	 */
	rl_setmulti(sc);

#ifdef IFPOLL_ENABLE
	/*
	 * Only enable interrupts if we are polling, keep them off otherwise.
	 */
	if (ifp->if_flags & IFF_NPOLLING) {
		CSR_WRITE_2(sc, RL_IMR, 0);
		sc->rl_npoll.ifpc_stcount = 0;
	} else
#endif /* IFPOLL_ENABLE */
	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, RL_IMR, RL_INTRS);

	/* Set initial TX threshold */
	sc->rl_txthresh = RL_TX_THRESH_INIT;

	/* Start RX/TX process. */
	CSR_WRITE_4(sc, RL_MISSEDPKT, 0);

	/* Enable receiver and transmitter. */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	mii_mediachg(mii);

	CSR_WRITE_1(sc, RL_CFG1, RL_CFG1_DRVLOAD|RL_CFG1_FULLDUPLEX);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->rl_stat_timer, hz, rl_tick, sc);
}

/*
 * Set media options.
 */
static int
rl_ifmedia_upd(struct ifnet *ifp)
{
	struct rl_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->rl_miibus);
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void
rl_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rl_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->rl_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
rl_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct rl_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data	*mii;
	int error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			rl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rl_stop(sc);
		}
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		rl_setmulti(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->rl_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return(error);
}

static void
rl_watchdog(struct ifnet *ifp)
{
	struct rl_softc *sc = ifp->if_softc;

	device_printf(sc->rl_dev, "watchdog timeout\n");

	IFNET_STAT_INC(ifp, oerrors, 1);

	rl_txeof(sc);
	rl_rxeof(sc);
	rl_init(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
rl_stop(struct rl_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ifp->if_timer = 0;

	callout_stop(&sc->rl_stat_timer);
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_WRITE_1(sc, RL_COMMAND, 0x00);
	CSR_WRITE_2(sc, RL_IMR, 0x0000);

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		if (sc->rl_cdata.rl_tx_chain[i] != NULL) {
			bus_dmamap_unload(sc->rl_cdata.rl_tx_tag,
					  sc->rl_cdata.rl_tx_dmamap[i]);
			m_freem(sc->rl_cdata.rl_tx_chain[i]);
			sc->rl_cdata.rl_tx_chain[i] = NULL;
			CSR_WRITE_4(sc, RL_TXADDR0 + (i * sizeof(uint32_t)),
				    0x0000000);
		}
	}
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
rl_shutdown(device_t dev)
{
	struct rl_softc *sc;

	sc = device_get_softc(dev);
	lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);
	rl_stop(sc);
	lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
rl_suspend(device_t dev)
{
	struct rl_softc	*sc = device_get_softc(dev);
	int i;

	lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);
	rl_stop(sc);

	for (i = 0; i < 5; i++)
		sc->saved_maps[i] = pci_read_config(dev, PCIR_BAR(i), 4);
	sc->saved_biosaddr = pci_read_config(dev, PCIR_BIOS, 4);
	sc->saved_intline = pci_read_config(dev, PCIR_INTLINE, 1);
	sc->saved_cachelnsz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	sc->saved_lattimer = pci_read_config(dev, PCIR_LATTIMER, 1);

	sc->suspended = 1;

	lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
rl_resume(device_t dev)
{
	struct rl_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int		i;

	lwkt_serialize_enter(ifp->if_serializer);

	/* better way to do this? */
	for (i = 0; i < 5; i++)
		pci_write_config(dev, PCIR_BAR(i), sc->saved_maps[i], 4);
	pci_write_config(dev, PCIR_BIOS, sc->saved_biosaddr, 4);
	pci_write_config(dev, PCIR_INTLINE, sc->saved_intline, 1);
	pci_write_config(dev, PCIR_CACHELNSZ, sc->saved_cachelnsz, 1);
	pci_write_config(dev, PCIR_LATTIMER, sc->saved_lattimer, 1);

	/* reenable busmastering */
	pci_enable_busmaster(dev);
	pci_enable_io(dev, RL_RES);

        /* reinitialize interface if necessary */
        if (ifp->if_flags & IFF_UP)
                rl_init(sc);

	sc->suspended = 0;
	lwkt_serialize_exit(ifp->if_serializer);
	return (0);
}

static int
rl_dma_alloc(struct rl_softc *sc)
{
	bus_dmamem_t dmem;
	int error, i;

	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
			0,			/* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			0,			/* flags */
			&sc->rl_parent_tag);
	if (error) {
		device_printf(sc->rl_dev, "can't create parent tag\n");
		return error;
	}

	/* Allocate a chunk of coherent memory for RX */
	error = bus_dmamem_coherent(sc->rl_parent_tag, 1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RL_RXBUFLEN + 1518, BUS_DMA_WAITOK, &dmem);
	if (error)
		return error;

	sc->rl_cdata.rl_rx_tag = dmem.dmem_tag;
	sc->rl_cdata.rl_rx_dmamap = dmem.dmem_map;
	sc->rl_cdata.rl_rx_buf_ptr = dmem.dmem_addr;

	/* NOTE: Apply same adjustment to vaddr and paddr */
	sc->rl_cdata.rl_rx_buf = sc->rl_cdata.rl_rx_buf_ptr + sizeof(uint64_t);
	sc->rl_cdata.rl_rx_buf_paddr = dmem.dmem_busaddr + sizeof(uint64_t);

	/*
	 * Allocate TX mbuf's DMA tag and maps
	 */
	error = bus_dma_tag_create(sc->rl_parent_tag,/* parent */
			RL_TXBUF_ALIGN, 0,	/* alignment, boundary */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MCLBYTES,		/* maxsize */
			1,			/* nsegments */
			MCLBYTES,		/* maxsegsize */
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK |
			BUS_DMA_ALIGNED,	/* flags */
			&sc->rl_cdata.rl_tx_tag);
	if (error) {
		device_printf(sc->rl_dev, "can't create TX mbuf tag\n");
		return error;
	}

	for (i = 0; i < RL_TX_LIST_CNT; ++i) {
		error = bus_dmamap_create(sc->rl_cdata.rl_tx_tag,
				BUS_DMA_WAITOK, &sc->rl_cdata.rl_tx_dmamap[i]);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(sc->rl_cdata.rl_tx_tag,
					sc->rl_cdata.rl_tx_dmamap[j]);
			}
			bus_dma_tag_destroy(sc->rl_cdata.rl_tx_tag);
			sc->rl_cdata.rl_tx_tag = NULL;

			device_printf(sc->rl_dev, "can't create TX mbuf map\n");
			return error;
		}
	}
	return 0;
}

static void
rl_dma_free(struct rl_softc *sc)
{
	if (sc->rl_cdata.rl_tx_tag != NULL) {
		int i;

		for (i = 0; i < RL_TX_LIST_CNT; ++i) {
			bus_dmamap_destroy(sc->rl_cdata.rl_tx_tag,
					   sc->rl_cdata.rl_tx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->rl_cdata.rl_tx_tag);
	}

	if (sc->rl_cdata.rl_rx_tag != NULL) {
		bus_dmamap_unload(sc->rl_cdata.rl_rx_tag,
				  sc->rl_cdata.rl_rx_dmamap);
		/* NOTE: Use rl_rx_buf_ptr here */
		bus_dmamem_free(sc->rl_cdata.rl_rx_tag,
				sc->rl_cdata.rl_rx_buf_ptr,
				sc->rl_cdata.rl_rx_dmamap);
		bus_dma_tag_destroy(sc->rl_cdata.rl_rx_tag);
	}

	if (sc->rl_parent_tag)
		bus_dma_tag_destroy(sc->rl_parent_tag);
}
