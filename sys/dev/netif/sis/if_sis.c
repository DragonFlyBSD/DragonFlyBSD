/*
 * Copyright (c) 1997, 1998, 1999
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
 * $FreeBSD: src/sys/pci/if_sis.c,v 1.13.4.24 2003/03/05 18:42:33 njl Exp $
 */

/*
 * SiS 900/SiS 7016 fast ethernet PCI NIC driver. Datasheets are
 * available from http://www.sis.com.tw.
 *
 * This driver also supports the NatSemi DP83815. Datasheets are
 * available from http://www.national.com.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The SiS 900 is a fairly simple chip. It uses bus master DMA with
 * simple TX and RX descriptors of 3 longwords in size. The receiver
 * has a single perfect filter entry for the station address and a
 * 128-bit multicast hash table. The SiS 900 has a built-in MII-based
 * transceiver while the 7016 requires an external transceiver chip.
 * Both chips offer the standard bit-bang MII interface as well as
 * an enchanced PHY interface which simplifies accessing MII registers.
 *
 * The only downside to this chipset is that RX descriptors must be
 * longword aligned.
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
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
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>

#include <net/bpf.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#define SIS_USEIOSPACE

#include "if_sisreg.h"

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */
static struct sis_type sis_devs[] = {
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_900, "SiS 900 10/100BaseTX" },
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_7016, "SiS 7016 10/100BaseTX" },
	{ PCI_VENDOR_NS, PCI_PRODUCT_NS_DP83815, "NatSemi DP8381[56] 10/100BaseTX" },
	{ 0, 0, NULL }
};

static int	sis_probe(device_t);
static int	sis_attach(device_t);
static int	sis_detach(device_t);

static int	sis_newbuf(struct sis_softc *, int, int);
static void	sis_setup_rxdesc(struct sis_softc *, int);
static int	sis_encap(struct sis_softc *, struct mbuf **, uint32_t *);
static void	sis_rxeof(struct sis_softc *);
static void	sis_rxeoc(struct sis_softc *);
static void	sis_txeof(struct sis_softc *);
static void	sis_intr(void *);
static void	sis_tick(void *);
static void	sis_start(struct ifnet *, struct ifaltq_subque *);
static int	sis_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	sis_init(void *);
static void	sis_stop(struct sis_softc *);
static void	sis_watchdog(struct ifnet *);
static void	sis_shutdown(device_t);
static int	sis_ifmedia_upd(struct ifnet *);
static void	sis_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static uint16_t	sis_reverse(uint16_t);
static void	sis_delay(struct sis_softc *);
static void	sis_eeprom_idle(struct sis_softc *);
static void	sis_eeprom_putbyte(struct sis_softc *, int);
static void	sis_eeprom_getword(struct sis_softc *, int, uint16_t *);
static void	sis_read_eeprom(struct sis_softc *, caddr_t, int, int, int);
#ifdef __x86_64__
static void	sis_read_cmos(struct sis_softc *, device_t, caddr_t, int, int);
static void	sis_read_mac(struct sis_softc *, device_t, caddr_t);
static device_t	sis_find_bridge(device_t);
#endif

static void	sis_mii_sync(struct sis_softc *);
static void	sis_mii_send(struct sis_softc *, uint32_t, int);
static int	sis_mii_readreg(struct sis_softc *, struct sis_mii_frame *);
static int	sis_mii_writereg(struct sis_softc *, struct sis_mii_frame *);
static int	sis_miibus_readreg(device_t, int, int);
static int	sis_miibus_writereg(device_t, int, int, int);
static void	sis_miibus_statchg(device_t);

static void	sis_setmulti_sis(struct sis_softc *);
static void	sis_setmulti_ns(struct sis_softc *);
static uint32_t	sis_mchash(struct sis_softc *, const uint8_t *);
static void	sis_reset(struct sis_softc *);
static int	sis_list_rx_init(struct sis_softc *);
static int	sis_list_tx_init(struct sis_softc *);

static int	sis_dma_alloc(device_t dev);
static void	sis_dma_free(device_t dev);
#ifdef IFPOLL_ENABLE
static void	sis_npoll(struct ifnet *, struct ifpoll_info *);
static void	sis_npoll_compat(struct ifnet *, void *, int);
#endif
#ifdef SIS_USEIOSPACE
#define SIS_RES			SYS_RES_IOPORT
#define SIS_RID			SIS_PCI_LOIO
#else
#define SIS_RES			SYS_RES_MEMORY
#define SIS_RID			SIS_PCI_LOMEM
#endif

static device_method_t sis_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sis_probe),
	DEVMETHOD(device_attach,	sis_attach),
	DEVMETHOD(device_detach,	sis_detach),
	DEVMETHOD(device_shutdown,	sis_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sis_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sis_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sis_miibus_statchg),

	DEVMETHOD_END
};

static driver_t sis_driver = {
	"sis",
	sis_methods,
	sizeof(struct sis_softc)
};

static devclass_t sis_devclass;

DECLARE_DUMMY_MODULE(if_sis);
DRIVER_MODULE(if_sis, pci, sis_driver, sis_devclass, NULL, NULL);
DRIVER_MODULE(miibus, sis, miibus_driver, miibus_devclass, NULL, NULL);

#define SIS_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))

#define SIS_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) & ~x)

/*
 * Routine to reverse the bits in a word. Stolen almost
 * verbatim from /usr/games/fortune.
 */
static uint16_t
sis_reverse(uint16_t n)
{
	n = ((n >>  1) & 0x5555) | ((n <<  1) & 0xaaaa);
	n = ((n >>  2) & 0x3333) | ((n <<  2) & 0xcccc);
	n = ((n >>  4) & 0x0f0f) | ((n <<  4) & 0xf0f0);
	n = ((n >>  8) & 0x00ff) | ((n <<  8) & 0xff00);

	return(n);
}

static void
sis_delay(struct sis_softc *sc)
{
	int idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, SIS_CSR);
}

static void
sis_eeprom_idle(struct sis_softc *sc)
{
	int i;

	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CLK);
	sis_delay(sc);

	for (i = 0; i < 25; i++) {
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CSEL);
	sis_delay(sc);
	CSR_WRITE_4(sc, SIS_EECTL, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
sis_eeprom_putbyte(struct sis_softc *sc, int addr)
{
	int d, i;

	d = addr | SIS_EECMD_READ;

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i)
			SIO_SET(SIS_EECTL_DIN);
		else
			SIO_CLR(SIS_EECTL_DIN);
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
sis_eeprom_getword(struct sis_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/* Force EEPROM to idle state. */
	sis_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	sis_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		if (CSR_READ_4(sc, SIS_EECTL) & SIS_EECTL_DOUT)
			word |= i;
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	sis_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
sis_read_eeprom(struct sis_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int i;
	uint16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		sis_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
}

#ifdef __x86_64__
static device_t
sis_find_bridge(device_t dev)
{
	devclass_t pci_devclass;
	device_t *pci_devices;
	int pci_count = 0;
	device_t *pci_children;
	int pci_childcount = 0;
	device_t *busp, *childp;
	device_t child = NULL;
	int i, j;

	if ((pci_devclass = devclass_find("pci")) == NULL)
		return(NULL);

	devclass_get_devices(pci_devclass, &pci_devices, &pci_count);

	for (i = 0, busp = pci_devices; i < pci_count; i++, busp++) {
		pci_childcount = 0;
		device_get_children(*busp, &pci_children, &pci_childcount);
		for (j = 0, childp = pci_children; j < pci_childcount;
		     j++, childp++) {
			if (pci_get_vendor(*childp) == PCI_VENDOR_SIS &&
			    pci_get_device(*childp) == 0x0008) {
				child = *childp;
				goto done;
			}
		}
	}

done:
	kfree(pci_devices, M_TEMP);
	kfree(pci_children, M_TEMP);
	return(child);
}

static void
sis_read_cmos(struct sis_softc *sc, device_t dev, caddr_t dest, int off,
	      int cnt)
{
	device_t bridge;
	uint8_t reg;
	int i;
	bus_space_tag_t	btag;

	bridge = sis_find_bridge(dev);
	if (bridge == NULL)
		return;
	reg = pci_read_config(bridge, 0x48, 1);
	pci_write_config(bridge, 0x48, reg|0x40, 1);

	/* XXX */
	btag = X86_64_BUS_SPACE_IO;

	for (i = 0; i < cnt; i++) {
		bus_space_write_1(btag, 0x0, 0x70, i + off);
		*(dest + i) = bus_space_read_1(btag, 0x0, 0x71);
	}

	pci_write_config(bridge, 0x48, reg & ~0x40, 1);
}

static void
sis_read_mac(struct sis_softc *sc, device_t dev, caddr_t dest)
{
	uint32_t filtsave, csrsave;

	filtsave = CSR_READ_4(sc, SIS_RXFILT_CTL);
	csrsave = CSR_READ_4(sc, SIS_CSR);

	CSR_WRITE_4(sc, SIS_CSR, SIS_CSR_RELOAD | filtsave);
	CSR_WRITE_4(sc, SIS_CSR, 0);
		
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filtsave & ~SIS_RXFILTCTL_ENABLE);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
	((uint16_t *)dest)[0] = CSR_READ_2(sc, SIS_RXFILT_DATA);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL,SIS_FILTADDR_PAR1);
	((uint16_t *)dest)[1] = CSR_READ_2(sc, SIS_RXFILT_DATA);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
	((uint16_t *)dest)[2] = CSR_READ_2(sc, SIS_RXFILT_DATA);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filtsave);
	CSR_WRITE_4(sc, SIS_CSR, csrsave);
}
#endif

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
sis_mii_sync(struct sis_softc *sc)
{
	int i;

	SIO_SET(SIS_MII_DIR|SIS_MII_DATA);

	for (i = 0; i < 32; i++) {
		SIO_SET(SIS_MII_CLK);
		DELAY(1);
		SIO_CLR(SIS_MII_CLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
sis_mii_send(struct sis_softc *sc, uint32_t bits, int cnt)
{
	int i;

	SIO_CLR(SIS_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i)
			SIO_SET(SIS_MII_DATA);
		else
			SIO_CLR(SIS_MII_DATA);
		DELAY(1);
		SIO_CLR(SIS_MII_CLK);
		DELAY(1);
		SIO_SET(SIS_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
static int
sis_mii_readreg(struct sis_softc *sc, struct sis_mii_frame *frame)
{
	int i, ack;

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = SIS_MII_STARTDELIM;
	frame->mii_opcode = SIS_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	/*
 	 * Turn on data xmit.
	 */
	SIO_SET(SIS_MII_DIR);

	sis_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	sis_mii_send(sc, frame->mii_stdelim, 2);
	sis_mii_send(sc, frame->mii_opcode, 2);
	sis_mii_send(sc, frame->mii_phyaddr, 5);
	sis_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	SIO_CLR((SIS_MII_CLK|SIS_MII_DATA));
	DELAY(1);
	SIO_SET(SIS_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(SIS_MII_DIR);

	/* Check for ack */
	SIO_CLR(SIS_MII_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, SIS_EECTL) & SIS_MII_DATA;
	SIO_SET(SIS_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(SIS_MII_CLK);
			DELAY(1);
			SIO_SET(SIS_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(SIS_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, SIS_EECTL) & SIS_MII_DATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(SIS_MII_CLK);
		DELAY(1);
	}

fail:

	SIO_CLR(SIS_MII_CLK);
	DELAY(1);
	SIO_SET(SIS_MII_CLK);
	DELAY(1);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
static int
sis_mii_writereg(struct sis_softc *sc, struct sis_mii_frame *frame)
{
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = SIS_MII_STARTDELIM;
	frame->mii_opcode = SIS_MII_WRITEOP;
	frame->mii_turnaround = SIS_MII_TURNAROUND;

	/*
	 * Turn on data output.
	 */
	SIO_SET(SIS_MII_DIR);

	sis_mii_sync(sc);

	sis_mii_send(sc, frame->mii_stdelim, 2);
	sis_mii_send(sc, frame->mii_opcode, 2);
	sis_mii_send(sc, frame->mii_phyaddr, 5);
	sis_mii_send(sc, frame->mii_regaddr, 5);
	sis_mii_send(sc, frame->mii_turnaround, 2);
	sis_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	SIO_SET(SIS_MII_CLK);
	DELAY(1);
	SIO_CLR(SIS_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	SIO_CLR(SIS_MII_DIR);

	return(0);
}

static int
sis_miibus_readreg(device_t dev, int phy, int reg)
{
	struct sis_softc *sc;
	struct sis_mii_frame frame;

	sc = device_get_softc(dev);

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return(0);
		/*
		 * The NatSemi chip can take a while after
		 * a reset to come ready, during which the BMSR
		 * returns a value of 0. This is *never* supposed
		 * to happen: some of the BMSR bits are meant to
		 * be hardwired in the on position, and this can
		 * confuse the miibus code a bit during the probe
		 * and attach phase. So we make an effort to check
		 * for this condition and wait for it to clear.
		 */
		if (!CSR_READ_4(sc, NS_BMSR))
			DELAY(1000);
		return CSR_READ_4(sc, NS_BMCR + (reg * 4));
	}
	/*
	 * Chipsets < SIS_635 seem not to be able to read/write
	 * through mdio. Use the enhanced PHY access register
	 * again for them.
	 */
	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i, val = 0;

		if (phy != 0)
			return(0);

		CSR_WRITE_4(sc, SIS_PHYCTL,
		    (phy << 11) | (reg << 6) | SIS_PHYOP_READ);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT) {
			device_printf(dev, "PHY failed to come ready\n");
			return(0);
		}

		val = (CSR_READ_4(sc, SIS_PHYCTL) >> 16) & 0xFFFF;

		if (val == 0xFFFF)
			return(0);

		return(val);
	} else {
		bzero((char *)&frame, sizeof(frame));

		frame.mii_phyaddr = phy;
		frame.mii_regaddr = reg;
		sis_mii_readreg(sc, &frame);

		return(frame.mii_data);
	}
}

static int
sis_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct sis_softc *sc;
	struct sis_mii_frame frame;

	sc = device_get_softc(dev);

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return(0);
		CSR_WRITE_4(sc, NS_BMCR + (reg * 4), data);
		return(0);
	}

	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i;

		if (phy != 0)
			return(0);

		CSR_WRITE_4(sc, SIS_PHYCTL, (data << 16) | (phy << 11) |
		    (reg << 6) | SIS_PHYOP_WRITE);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT)
			device_printf(dev, "PHY failed to come ready\n");
	} else {
		bzero((char *)&frame, sizeof(frame));

		frame.mii_phyaddr = phy;
		frame.mii_regaddr = reg;
		frame.mii_data = data;
		sis_mii_writereg(sc, &frame);
	}
	return(0);
}

static void
sis_miibus_statchg(device_t dev)
{
	struct sis_softc *sc;

	sc = device_get_softc(dev);
	sis_init(sc);
}

static uint32_t
sis_mchash(struct sis_softc *sc, const uint8_t *addr)
{
	uint32_t crc, carry; 
	int i, j;
	uint8_t c;

	/* Compute CRC for the address value. */
	crc = 0xFFFFFFFF; /* initial value */

	for (i = 0; i < 6; i++) {
		c = *(addr + i);
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x80000000) ? 1 : 0) ^ (c & 0x01);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ 0x04c11db6) | carry;
		}
	}

	/*
	 * return the filter bit position
	 *
	 * The NatSemi chip has a 512-bit filter, which is
	 * different than the SiS, so we special-case it.
	 */
	if (sc->sis_type == SIS_TYPE_83815)
		return (crc >> 23);
	else if (sc->sis_rev >= SIS_REV_635 || sc->sis_rev == SIS_REV_900B)
		return (crc >> 24);
	else
		return (crc >> 25);
}

static void
sis_setmulti_ns(struct sis_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t h = 0, i, filtsave;
	int bit, index;

	ifp = &sc->arpcom.ac_if;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		SIS_CLRBIT(sc, SIS_RXFILT_CTL, NS_RXFILTCTL_MCHASH);
		SIS_SETBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_ALLMULTI);
		return;
	}

	/*
	 * We have to explicitly enable the multicast hash table
	 * on the NatSemi chip if we want to use it, which we do.
	 */
	SIS_SETBIT(sc, SIS_RXFILT_CTL, NS_RXFILTCTL_MCHASH);
	SIS_CLRBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_ALLMULTI);

	filtsave = CSR_READ_4(sc, SIS_RXFILT_CTL);

	/* first, zot all the existing hash bits */
	for (i = 0; i < 32; i++) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO + (i*2));
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, 0);
	}

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = sis_mchash(sc,
			       LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		index = h >> 3;
		bit = h & 0x1F;
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO + index);
		if (bit > 0xF)
			bit -= 0x10;
		SIS_SETBIT(sc, SIS_RXFILT_DATA, (1 << bit));
	}

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filtsave);
}

static void
sis_setmulti_sis(struct sis_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t h, i, n, ctl;
	uint16_t hashes[16];

	ifp = &sc->arpcom.ac_if;

	/* hash table size */
	if (sc->sis_rev >= SIS_REV_635 || sc->sis_rev == SIS_REV_900B)
		n = 16;
	else
		n = 8;

	ctl = CSR_READ_4(sc, SIS_RXFILT_CTL) & SIS_RXFILTCTL_ENABLE;

	if (ifp->if_flags & IFF_BROADCAST)
		ctl |= SIS_RXFILTCTL_BROAD;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		ctl |= SIS_RXFILTCTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			ctl |= SIS_RXFILTCTL_BROAD|SIS_RXFILTCTL_ALLPHYS;
		for (i = 0; i < n; i++)
			hashes[i] = ~0;
	} else {
		for (i = 0; i < n; i++)
			hashes[i] = 0;
		i = 0;
		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = sis_mchash(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			hashes[h >> 4] |= 1 << (h & 0xf);
			i++;
		}
		if (i > n) {
			ctl |= SIS_RXFILTCTL_ALLMULTI;
			for (i = 0; i < n; i++)
				hashes[i] = ~0;
		}
	}

	for (i = 0; i < n; i++) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, (4 + i) << 16);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, hashes[i]);
	}

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, ctl);
}

static void
sis_reset(struct sis_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RESET);

	for (i = 0; i < SIS_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, SIS_CSR) & SIS_CSR_RESET))
			break;
	}

	if (i == SIS_TIMEOUT)
		if_printf(ifp, "reset never completed\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/*
	 * If this is a NetSemi chip, make sure to clear
	 * PME mode.
	 */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, NS_CLKRUN, NS_CLKRUN_PMESTS);
		CSR_WRITE_4(sc, NS_CLKRUN, 0);
	}
}

/*
 * Probe for an SiS chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
sis_probe(device_t dev)
{
	struct sis_type *t;

	t = sis_devs;

	while(t->sis_name != NULL) {
		if ((pci_get_vendor(dev) == t->sis_vid) &&
		    (pci_get_device(dev) == t->sis_did)) {
			device_set_desc(dev, t->sis_name);
			return(0);
		}
		t++;
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
sis_attach(device_t dev)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint32_t command;
	struct sis_softc *sc;
	struct ifnet *ifp;
	int error, rid, waittime;

	error = waittime = 0;
	sc = device_get_softc(dev);

	if (pci_get_device(dev) == PCI_PRODUCT_SIS_900)
		sc->sis_type = SIS_TYPE_900;
	if (pci_get_device(dev) == PCI_PRODUCT_SIS_7016)
		sc->sis_type = SIS_TYPE_7016;
	if (pci_get_vendor(dev) == PCI_VENDOR_NS)
		sc->sis_type = SIS_TYPE_83815;

	sc->sis_rev = pci_read_config(dev, PCIR_REVID, 1);

	/*
	 * Handle power management nonsense.
	 */

	command = pci_read_config(dev, SIS_PCI_CAPID, 4) & 0x000000FF;
	if (command == 0x01) {

		command = pci_read_config(dev, SIS_PCI_PWRMGMTCTRL, 4);
		if (command & SIS_PSTATE_MASK) {
			uint32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_read_config(dev, SIS_PCI_LOIO, 4);
			membase = pci_read_config(dev, SIS_PCI_LOMEM, 4);
			irq = pci_read_config(dev, SIS_PCI_INTLINE, 4);

			/* Reset the power state. */
			device_printf(dev, "chip is in D%d power mode "
			    "-- setting to D0\n", command & SIS_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_write_config(dev, SIS_PCI_PWRMGMTCTRL, command, 4);

			/* Restore PCI config data. */
			pci_write_config(dev, SIS_PCI_LOIO, iobase, 4);
			pci_write_config(dev, SIS_PCI_LOMEM, membase, 4);
			pci_write_config(dev, SIS_PCI_INTLINE, irq, 4);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_read_config(dev, PCIR_COMMAND, 4);
	command |= (PCIM_CMD_PORTEN|PCIM_CMD_MEMEN|PCIM_CMD_BUSMASTEREN);
	pci_write_config(dev, PCIR_COMMAND, command, 4);
	command = pci_read_config(dev, PCIR_COMMAND, 4);

#ifdef SIS_USEIOSPACE
	if (!(command & PCIM_CMD_PORTEN)) {
		device_printf(dev, "failed to enable I/O ports!\n");
		error = ENXIO;
		goto fail;
	}
#else
	if (!(command & PCIM_CMD_MEMEN)) {
		device_printf(dev, "failed to enable memory mapping!\n");
		error = ENXIO;
		goto fail;
	}
#endif

	rid = SIS_RID;
	sc->sis_res = bus_alloc_resource_any(dev, SIS_RES, &rid, RF_ACTIVE);

	if (sc->sis_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->sis_btag = rman_get_bustag(sc->sis_res);
	sc->sis_bhandle = rman_get_bushandle(sc->sis_res);

	/* Allocate interrupt */
	rid = 0;
	sc->sis_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->sis_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	sis_reset(sc);

	if (sc->sis_type == SIS_TYPE_900 &&
            (sc->sis_rev == SIS_REV_635 ||
             sc->sis_rev == SIS_REV_900B)) {
		SIO_SET(SIS_CFG_RND_CNT);
		SIO_SET(SIS_CFG_PERR_DETECT);
	}

	/*
	 * Get station address from the EEPROM.
	 */
	switch (pci_get_vendor(dev)) {
	case PCI_VENDOR_NS:
		/*
		 * Reading the MAC address out of the EEPROM on
		 * the NatSemi chip takes a bit more work than
		 * you'd expect. The address spans 4 16-bit words,
		 * with the first word containing only a single bit.
		 * You have to shift everything over one bit to
		 * get it aligned properly. Also, the bits are
		 * stored backwards (the LSB is really the MSB,
		 * and so on) so you have to reverse them in order
		 * to get the MAC address into the form we want.
		 * Why? Who the hell knows.
		 */
		{
			uint16_t		tmp[4];

			sis_read_eeprom(sc, (caddr_t)&tmp,
			    NS_EE_NODEADDR, 4, 0);

			/* Shift everything over one bit. */
			tmp[3] = tmp[3] >> 1;
			tmp[3] |= tmp[2] << 15;
			tmp[2] = tmp[2] >> 1;
			tmp[2] |= tmp[1] << 15;
			tmp[1] = tmp[1] >> 1;
			tmp[1] |= tmp[0] << 15;

			/* Now reverse all the bits. */
			tmp[3] = sis_reverse(tmp[3]);
			tmp[2] = sis_reverse(tmp[2]);
			tmp[1] = sis_reverse(tmp[1]);

			bcopy((char *)&tmp[1], eaddr, ETHER_ADDR_LEN);
		}
		break;
	case PCI_VENDOR_SIS:
	default:
#ifdef __x86_64__
		/*
		 * If this is a SiS 630E chipset with an embedded
		 * SiS 900 controller, we have to read the MAC address
		 * from the APC CMOS RAM. Our method for doing this
		 * is very ugly since we have to reach out and grab
		 * ahold of hardware for which we cannot properly
		 * allocate resources. This code is only compiled on
		 * the x86_64 architecture since the SiS 630E chipset
		 * is for x86 motherboards only. Note that there are
		 * a lot of magic numbers in this hack. These are
		 * taken from SiS's Linux driver. I'd like to replace
		 * them with proper symbolic definitions, but that
		 * requires some datasheets that I don't have access
		 * to at the moment.
		 */
		if (sc->sis_rev == SIS_REV_630S ||
		    sc->sis_rev == SIS_REV_630E ||
		    sc->sis_rev == SIS_REV_630EA1)
			sis_read_cmos(sc, dev, (caddr_t)&eaddr, 0x9, 6);

		else if (sc->sis_rev == SIS_REV_635 ||
			 sc->sis_rev == SIS_REV_630ET)
			sis_read_mac(sc, dev, (caddr_t)&eaddr);
		else if (sc->sis_rev == SIS_REV_96x) {
			/*
			 * Allow to read EEPROM from LAN. It is shared
			 * between a 1394 controller and the NIC and each
			 * time we access it, we need to set SIS_EECMD_REQ.
			 */
			SIO_SET(SIS_EECMD_REQ);
			for (waittime = 0; waittime < SIS_TIMEOUT;
			    waittime++) {
				/* Force EEPROM to idle state. */
				sis_eeprom_idle(sc);
				if (CSR_READ_4(sc, SIS_EECTL) & SIS_EECMD_GNT) {
					sis_read_eeprom(sc, (caddr_t)&eaddr,
					    SIS_EE_NODEADDR, 3, 0);
					break;
				}
				DELAY(1);
			}
			/*
			 * Set SIS_EECTL_CLK to high, so a other master
			 * can operate on the i2c bus.
			 */
			SIO_SET(SIS_EECTL_CLK);
			/* Refuse EEPROM access by LAN */
			SIO_SET(SIS_EECMD_DONE);
		} else
#endif
			sis_read_eeprom(sc, (caddr_t)&eaddr,
			    SIS_EE_NODEADDR, 3, 0);
		break;
	}

	callout_init(&sc->sis_timer);

	error = sis_dma_alloc(dev);
	if (error)
		goto fail;

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sis_ioctl;
	ifp->if_start = sis_start;
	ifp->if_watchdog = sis_watchdog;
	ifp->if_init = sis_init;
	ifp->if_baudrate = 10000000;
	ifq_set_maxlen(&ifp->if_snd, SIS_TX_LIST_CNT - 1);
	ifq_set_ready(&ifp->if_snd);
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = sis_npoll;
#endif
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Do MII setup.
	 */
	if (mii_phy_probe(dev, &sc->sis_miibus,
	    sis_ifmedia_upd, sis_ifmedia_sts)) {
		device_printf(dev, "MII without any PHY!\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->sis_npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif
	
	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sis_irq));

	error = bus_setup_intr(dev, sc->sis_irq, INTR_MPSAFE,
			       sis_intr, sc, 
			       &sc->sis_intrhand, 
			       ifp->if_serializer);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		sis_detach(dev);

	return(error);
}

/*
 * Shutdown hardware and free up resources. It is called in both the error case
 * and the normal detach case so it needs to be careful about only freeing
 * resources that have actually been allocated.
 */
static int
sis_detach(device_t dev)
{
	struct sis_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;


	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		sis_reset(sc);
		sis_stop(sc);
		bus_teardown_intr(dev, sc->sis_irq, sc->sis_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}
	if (sc->sis_miibus)
		device_delete_child(dev, sc->sis_miibus);
	bus_generic_detach(dev);

	if (sc->sis_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sis_irq);
	if (sc->sis_res)
		bus_release_resource(dev, SIS_RES, SIS_RID, sc->sis_res);

	sis_dma_free(dev);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
sis_list_tx_init(struct sis_softc *sc)
{
	struct sis_list_data *ld = &sc->sis_ldata;
	struct sis_chain_data *cd = &sc->sis_cdata;
	int i, nexti;

	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		bus_addr_t paddr;

		/*
		 * Link the TX desc together
		 */
		nexti = (i == (SIS_TX_LIST_CNT - 1)) ? 0 : i+1;
		paddr = ld->sis_tx_paddr + (nexti * sizeof(struct sis_desc));
		ld->sis_tx_list[i].sis_next = paddr;
	}
	cd->sis_tx_prod = cd->sis_tx_cons = cd->sis_tx_cnt = 0;

	return 0;
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
sis_list_rx_init(struct sis_softc *sc)
{
	struct sis_list_data *ld = &sc->sis_ldata;
	struct sis_chain_data *cd = &sc->sis_cdata;
	int i, error;

	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		bus_addr_t paddr;
		int nexti;

		error = sis_newbuf(sc, i, 1);
		if (error)
			return error;

		/*
		 * Link the RX desc together
		 */
		nexti = (i == (SIS_RX_LIST_CNT - 1)) ? 0 : i+1;
		paddr = ld->sis_rx_paddr + (nexti * sizeof(struct sis_desc));
		ld->sis_rx_list[i].sis_next = paddr;
	}
	cd->sis_rx_prod = 0;

	return 0;
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
sis_newbuf(struct sis_softc *sc, int idx, int init)
{
	struct sis_chain_data *cd = &sc->sis_cdata;
	struct sis_rx_data *rd = &cd->sis_rx_data[idx];
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct mbuf *m;
	int nseg, error;

	m = m_getcl(init ? M_WAITOK : M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		if (init)
			if_printf(&sc->arpcom.ac_if, "can't alloc RX mbuf\n");
		return ENOBUFS;
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/* Try loading the mbuf into tmp DMA map */
	error = bus_dmamap_load_mbuf_segment(cd->sis_rxbuf_tag,
			cd->sis_rx_tmpmap, m, &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init)
			if_printf(&sc->arpcom.ac_if, "can't load RX mbuf\n");
		return error;
	}

	/* Unload the currently loaded mbuf */
	if (rd->sis_mbuf != NULL) {
		bus_dmamap_sync(cd->sis_rxbuf_tag, rd->sis_map,
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(cd->sis_rxbuf_tag, rd->sis_map);
	}

	/* Swap DMA maps */
	map = cd->sis_rx_tmpmap;
	cd->sis_rx_tmpmap = rd->sis_map;
	rd->sis_map = map;

	/* Save necessary information */
	rd->sis_mbuf = m;
	rd->sis_paddr = seg.ds_addr;

	sis_setup_rxdesc(sc, idx);
	return 0;
}

static void
sis_setup_rxdesc(struct sis_softc *sc, int idx)
{
	struct sis_desc *c = &sc->sis_ldata.sis_rx_list[idx];

	/* Setup the RX desc */
	c->sis_ctl = SIS_RXLEN;
	c->sis_ptr = sc->sis_cdata.sis_rx_data[idx].sis_paddr;
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
sis_rxeof(struct sis_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i, total_len = 0;
	uint32_t rxstat;

	i = sc->sis_cdata.sis_rx_prod;
	while (SIS_OWNDESC(&sc->sis_ldata.sis_rx_list[i])) {
		struct sis_desc	*cur_rx;
		struct sis_rx_data *rd;
		struct mbuf *m;
		int idx = i;

#ifdef IFPOLL_ENABLE
		if (ifp->if_flags & IFF_NPOLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif /* IFPOLL_ENABLE */

		cur_rx = &sc->sis_ldata.sis_rx_list[idx];
		rd = &sc->sis_cdata.sis_rx_data[idx];

		rxstat = cur_rx->sis_rxstat;
		total_len = SIS_RXBYTES(cur_rx);

		m = rd->sis_mbuf;

		SIS_INC(i, SIS_RX_LIST_CNT);

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (!(rxstat & SIS_CMDSTS_PKT_OK)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			if (rxstat & SIS_RXSTAT_COLL)
				IFNET_STAT_INC(ifp, collisions, 1);
			sis_setup_rxdesc(sc, idx);
			continue;
		}

		/* No errors; receive the packet. */
		if (sis_newbuf(sc, idx, 0) == 0) {
			m->m_pkthdr.len = m->m_len = total_len;
			m->m_pkthdr.rcvif = ifp;
		} else {
			IFNET_STAT_INC(ifp, ierrors, 1);
			sis_setup_rxdesc(sc, idx);
			continue;
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		ifp->if_input(ifp, m, NULL, -1);
	}
	sc->sis_cdata.sis_rx_prod = i;
}

static void
sis_rxeoc(struct sis_softc *sc)
{
	sis_rxeof(sc);
	sis_init(sc);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
sis_txeof(struct sis_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct sis_chain_data *cd = &sc->sis_cdata;
	uint32_t idx;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (idx = sc->sis_cdata.sis_tx_cons; sc->sis_cdata.sis_tx_cnt > 0;
	     sc->sis_cdata.sis_tx_cnt--, SIS_INC(idx, SIS_TX_LIST_CNT) ) {
		struct sis_desc *cur_tx;
		struct sis_tx_data *td;

		cur_tx = &sc->sis_ldata.sis_tx_list[idx];
		td = &cd->sis_tx_data[idx];

		if (SIS_OWNDESC(cur_tx))
			break;

		if (cur_tx->sis_ctl & SIS_CMDSTS_MORE)
			continue;

		if (!(cur_tx->sis_ctl & SIS_CMDSTS_PKT_OK)) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (cur_tx->sis_txstat & SIS_TXSTAT_EXCESSCOLLS)
				IFNET_STAT_INC(ifp, collisions, 1);
			if (cur_tx->sis_txstat & SIS_TXSTAT_OUTOFWINCOLL)
				IFNET_STAT_INC(ifp, collisions, 1);
		}

		IFNET_STAT_INC(ifp, collisions,
		    (cur_tx->sis_txstat & SIS_TXSTAT_COLLCNT) >> 16);

		IFNET_STAT_INC(ifp, opackets, 1);
		if (td->sis_mbuf != NULL) {
			bus_dmamap_unload(cd->sis_txbuf_tag, td->sis_map);
			m_freem(td->sis_mbuf);
			td->sis_mbuf = NULL;
		}
	}

	if (idx != sc->sis_cdata.sis_tx_cons) {
		/* we freed up some buffers */
		sc->sis_cdata.sis_tx_cons = idx;
	}

	if (cd->sis_tx_cnt == 0)
		ifp->if_timer = 0;
	if (!SIS_IS_OACTIVE(sc))
		ifq_clr_oactive(&ifp->if_snd);
}

static void
sis_tick(void *xsc)
{
	struct sis_softc *sc = xsc;
	struct mii_data *mii;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	mii = device_get_softc(sc->sis_miibus);
	mii_tick(mii);

	if (!sc->sis_link) {
		mii_pollstat(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->sis_link++;
		if (!ifq_is_empty(&ifp->if_snd))
			if_devstart(ifp);
	}

	callout_reset(&sc->sis_timer, hz, sis_tick, sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

#ifdef IFPOLL_ENABLE

static void
sis_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct sis_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/*
	 * On the sis, reading the status register also clears it.
	 * So before returning to intr mode we must make sure that all
	 * possible pending sources of interrupts have been served.
	 * In practice this means run to completion the *eof routines,
	 * and then call the interrupt routine
	 */
	sc->rxcycles = count;
	sis_rxeof(sc);
	sis_txeof(sc);
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	if (sc->sis_npoll.ifpc_stcount-- == 0) {
		uint32_t status;

		sc->sis_npoll.ifpc_stcount = sc->sis_npoll.ifpc_stfrac;

		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, SIS_ISR);

		if (status & (SIS_ISR_RX_ERR|SIS_ISR_RX_OFLOW))
			sis_rxeoc(sc);

		if (status & (SIS_ISR_RX_IDLE))
			SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

		if (status & SIS_ISR_SYSERR) {
			sis_reset(sc);
			sis_init(sc);
		}
	}
}

static void
sis_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct sis_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->sis_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = sis_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			/* disable interrupts */
			CSR_WRITE_4(sc, SIS_IER, 0);
			sc->sis_npoll.ifpc_stcount = 0;
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING) {
			/* enable interrupts */
			CSR_WRITE_4(sc, SIS_IER, 1);
		}
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->sis_irq));
	}
}

#endif /* IFPOLL_ENABLE */

static void
sis_intr(void *arg)
{
	struct sis_softc *sc;
	struct ifnet *ifp;
	uint32_t status;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Supress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		sis_stop(sc);
		return;
	}

	/* Disable interrupts. */
	CSR_WRITE_4(sc, SIS_IER, 0);

	for (;;) {
		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, SIS_ISR);

		if ((status & SIS_INTRS) == 0)
			break;

		if (status &
		    (SIS_ISR_TX_DESC_OK | SIS_ISR_TX_ERR | SIS_ISR_TX_OK |
		     SIS_ISR_TX_IDLE) )
			sis_txeof(sc);

		if (status &
		    (SIS_ISR_RX_DESC_OK | SIS_ISR_RX_OK | SIS_ISR_RX_IDLE))
			sis_rxeof(sc);

		if (status & (SIS_ISR_RX_ERR | SIS_ISR_RX_OFLOW))
			sis_rxeoc(sc);

		if (status & (SIS_ISR_RX_IDLE))
			SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

		if (status & SIS_ISR_SYSERR) {
			sis_reset(sc);
			sis_init(sc);
		}
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, SIS_IER, 1);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
sis_encap(struct sis_softc *sc, struct mbuf **m_head, uint32_t *txidx)
{
	struct sis_chain_data *cd = &sc->sis_cdata;
	struct sis_list_data *ld = &sc->sis_ldata;
	bus_dma_segment_t segs[SIS_NSEGS];
	bus_dmamap_t map;
	int frag, cur, maxsegs, nsegs, error, i;

	maxsegs = SIS_TX_LIST_CNT - SIS_NSEGS_RESERVED - cd->sis_tx_cnt;
	KASSERT(maxsegs >= 1, ("not enough TX descs"));
	if (maxsegs > SIS_NSEGS)
		maxsegs = SIS_NSEGS;

	map = cd->sis_tx_data[*txidx].sis_map;
	error = bus_dmamap_load_mbuf_defrag(cd->sis_txbuf_tag, map, m_head,
			segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(*m_head);
		*m_head = NULL;
		return error;
	}
	bus_dmamap_sync(cd->sis_txbuf_tag, map, BUS_DMASYNC_PREWRITE);

	cur = frag = *txidx;
	for (i = 0; i < nsegs; ++i) {
		struct sis_desc *f = &ld->sis_tx_list[frag];

		f->sis_ctl = SIS_CMDSTS_MORE | segs[i].ds_len;
		f->sis_ptr = segs[i].ds_addr;
		if (i != 0)
			f->sis_ctl |= SIS_CMDSTS_OWN;

		cur = frag;
		SIS_INC(frag, SIS_TX_LIST_CNT);
	}
	ld->sis_tx_list[cur].sis_ctl &= ~SIS_CMDSTS_MORE;
	ld->sis_tx_list[*txidx].sis_ctl |= SIS_CMDSTS_OWN;

	/* Swap DMA map */
	cd->sis_tx_data[*txidx].sis_map = cd->sis_tx_data[cur].sis_map;
	cd->sis_tx_data[cur].sis_map = map;

	cd->sis_tx_data[cur].sis_mbuf = *m_head;

	cd->sis_tx_cnt += nsegs;
	*txidx = frag;

	return 0;
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

static void
sis_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct sis_softc *sc = ifp->if_softc;
	int need_trans, error;
	uint32_t idx;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

	if (!sc->sis_link) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	idx = sc->sis_cdata.sis_tx_prod;
	need_trans = 0;

	while (sc->sis_cdata.sis_tx_data[idx].sis_mbuf == NULL) {
		struct mbuf *m_head;

		/*
		 * If there's no way we can send any packets, return now.
		 */
		if (SIS_IS_OACTIVE(sc)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		error = sis_encap(sc, &m_head, &idx);
		if (error) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (sc->sis_cdata.sis_tx_cnt == 0) {
				continue;
			} else {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}
		}
		need_trans = 1;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);
	}

	if (!need_trans)
		return;

	/* Transmit */
	sc->sis_cdata.sis_tx_prod = idx;
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_ENABLE);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
sis_init(void *xsc)
{
	struct sis_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	sis_stop(sc);

	mii = device_get_softc(sc->sis_miibus);

	/* Set MAC address */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[0]);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[1]);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[2]);
	} else {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[0]);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[1]);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    ((uint16_t *)sc->arpcom.ac_enaddr)[2]);
	}

	/* Init circular RX list. */
	if (sis_list_rx_init(sc)) {
		if_printf(ifp, "initialization failed: "
			  "no memory for rx buffers\n");
		sis_stop(sc);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	sis_list_tx_init(sc);

	/*
	 * For the NatSemi chip, we have to explicitly enable the
	 * reception of ARP frames, as well as turn on the 'perfect
	 * match' filter where we store the station address, otherwise
	 * we won't receive unicasts meant for this host.
	 */
	if (sc->sis_type == SIS_TYPE_83815) {
		SIS_SETBIT(sc, SIS_RXFILT_CTL, NS_RXFILTCTL_ARP);
		SIS_SETBIT(sc, SIS_RXFILT_CTL, NS_RXFILTCTL_PERFECT);
	}

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		SIS_SETBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_ALLPHYS);
	else
		SIS_CLRBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_ALLPHYS);

	/*
	 * Set the capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST)
		SIS_SETBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_BROAD);
	else
		SIS_CLRBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_BROAD);

	/*
	 * Load the multicast filter.
	 */
	if (sc->sis_type == SIS_TYPE_83815)
		sis_setmulti_ns(sc);
	else
		sis_setmulti_sis(sc);

	/* Turn the receive filter on */
	SIS_SETBIT(sc, SIS_RXFILT_CTL, SIS_RXFILTCTL_ENABLE);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, sc->sis_ldata.sis_rx_paddr);
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, sc->sis_ldata.sis_tx_paddr);

	/* SIS_CFG_EDB_MASTER_EN indicates the EDB bus is used instead of
	 * the PCI bus. When this bit is set, the Max DMA Burst Size
	 * for TX/RX DMA should be no larger than 16 double words.
	 */
	if (CSR_READ_4(sc, SIS_CFG) & SIS_CFG_EDB_MASTER_EN)
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG64);
	else
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG256);

	/* Accept Long Packets for VLAN support */
	SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_JABBER);

	/* Set TX configuration */
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_10_T)
		CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_10);
	else
		CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_100);

	/* Set full/half duplex mode. */
	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		SIS_SETBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT|SIS_TXCFG_IGN_CARR));
		SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	} else {
		SIS_CLRBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT|SIS_TXCFG_IGN_CARR));
		SIS_CLRBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	}

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, SIS_IMR, SIS_INTRS);
#ifdef IFPOLL_ENABLE
	/*
	 * ... only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (ifp->if_flags & IFF_NPOLLING) {
		CSR_WRITE_4(sc, SIS_IER, 0);
		sc->sis_npoll.ifpc_stcount = 0;
	} else
#endif /* IFPOLL_ENABLE */
	CSR_WRITE_4(sc, SIS_IER, 1);

	/* Enable receiver and transmitter. */
	SIS_CLRBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE|SIS_CSR_RX_DISABLE);
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

#ifdef notdef
	mii_mediachg(mii);
#endif

	/*
	 * Page 75 of the DP83815 manual recommends the
	 * following register settings "for optimum
	 * performance." Note however that at least three
	 * of the registers are listed as "reserved" in
	 * the register map, so who knows what they do.
	 */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0x0001);
		CSR_WRITE_4(sc, NS_PHY_CR, 0x189C);
		CSR_WRITE_4(sc, NS_PHY_TDATA, 0x0000);
		CSR_WRITE_4(sc, NS_PHY_DSPCFG, 0x5040);
		CSR_WRITE_4(sc, NS_PHY_SDCFG, 0x008C);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->sis_timer, hz, sis_tick, sc);
}

/*
 * Set media options.
 */
static int
sis_ifmedia_upd(struct ifnet *ifp)
{
	struct sis_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;

	mii = device_get_softc(sc->sis_miibus);
	sc->sis_link = 0;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void
sis_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sis_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;

	mii = device_get_softc(sc->sis_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
sis_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct sis_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0;

	switch(command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			sis_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sis_stop(sc);
		}
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (sc->sis_type == SIS_TYPE_83815)
			sis_setmulti_ns(sc);
		else
			sis_setmulti_sis(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->sis_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

static void
sis_watchdog(struct ifnet *ifp)
{
	struct sis_softc *sc;

	sc = ifp->if_softc;

	IFNET_STAT_INC(ifp, oerrors, 1);
	if_printf(ifp, "watchdog timeout\n");

	sis_stop(sc);
	sis_reset(sc);
	sis_init(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
sis_stop(struct sis_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct sis_list_data *ld = &sc->sis_ldata;
	struct sis_chain_data *cd = &sc->sis_cdata;
	int i;

	callout_stop(&sc->sis_timer);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	CSR_WRITE_4(sc, SIS_IER, 0);
	CSR_WRITE_4(sc, SIS_IMR, 0);
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE|SIS_CSR_RX_DISABLE);
	DELAY(1000);
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, 0);
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, 0);

	sc->sis_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		struct sis_rx_data *rd = &cd->sis_rx_data[i];

		if (rd->sis_mbuf != NULL) {
			bus_dmamap_unload(cd->sis_rxbuf_tag, rd->sis_map);
			m_freem(rd->sis_mbuf);
			rd->sis_mbuf = NULL;
		}
	}
	bzero(ld->sis_rx_list, SIS_RX_LIST_SZ);

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		struct sis_tx_data *td = &cd->sis_tx_data[i];

		if (td->sis_mbuf != NULL) {
			bus_dmamap_unload(cd->sis_txbuf_tag, td->sis_map);
			m_freem(td->sis_mbuf);
			td->sis_mbuf = NULL;
		}
	}
	bzero(ld->sis_tx_list, SIS_TX_LIST_SZ);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
sis_shutdown(device_t dev)
{
	struct sis_softc	*sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = &sc->arpcom.ac_if;
	lwkt_serialize_enter(ifp->if_serializer);
	sis_reset(sc);
	sis_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
sis_dma_alloc(device_t dev)
{
	struct sis_softc *sc = device_get_softc(dev);
	struct sis_chain_data *cd = &sc->sis_cdata;
	struct sis_list_data *ld = &sc->sis_ldata;
	int i, error;

	/* Create top level DMA tag */
	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
			0,			/* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			0,			/* flags */
			&sc->sis_parent_tag);
	if (error) {
		device_printf(dev, "could not create parent DMA tag\n");
		return error;
	}

	/* Allocate RX ring */
	ld->sis_rx_list = bus_dmamem_coherent_any(sc->sis_parent_tag,
				SIS_RING_ALIGN, SIS_RX_LIST_SZ,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&ld->sis_rx_tag, &ld->sis_rx_dmamap,
				&ld->sis_rx_paddr);
	if (ld->sis_rx_list == NULL) {
		device_printf(dev, "could not allocate RX ring\n");
		return ENOMEM;
	}

	/* Allocate TX ring */
	ld->sis_tx_list = bus_dmamem_coherent_any(sc->sis_parent_tag,
				SIS_RING_ALIGN, SIS_TX_LIST_SZ,
				BUS_DMA_WAITOK | BUS_DMA_ZERO,
				&ld->sis_tx_tag, &ld->sis_tx_dmamap,
				&ld->sis_tx_paddr);
	if (ld->sis_tx_list == NULL) {
		device_printf(dev, "could not allocate TX ring\n");
		return ENOMEM;
	}

	/* Create DMA tag for TX mbuf */
	error = bus_dma_tag_create(sc->sis_parent_tag,/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MCLBYTES,		/* maxsize */
			SIS_NSEGS,		/* nsegments */
			MCLBYTES,		/* maxsegsize */
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK,/* flags */
			&cd->sis_txbuf_tag);
	if (error) {
		device_printf(dev, "could not create TX buf DMA tag\n");
		return error;
	}

	/* Create DMA maps for TX mbufs */
	for (i = 0; i < SIS_TX_LIST_CNT; ++i) {
		error = bus_dmamap_create(cd->sis_txbuf_tag, BUS_DMA_WAITOK,
					  &cd->sis_tx_data[i].sis_map);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(cd->sis_txbuf_tag,
					cd->sis_tx_data[j].sis_map);
			}
			bus_dma_tag_destroy(cd->sis_txbuf_tag);
			cd->sis_txbuf_tag = NULL;

			device_printf(dev, "could not create %dth "
				      "TX buf DMA map\n", i);
			return error;
		}
	}

	/* Create DMA tag for RX mbuf */
	error = bus_dma_tag_create(sc->sis_parent_tag,/* parent */
			SIS_RXBUF_ALIGN, 0,	/* alignment, boundary */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MCLBYTES,		/* maxsize */
			1,			/* nsegments */
			MCLBYTES,		/* maxsegsize */
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK |
			BUS_DMA_ALIGNED,	/* flags */
			&cd->sis_rxbuf_tag);
	if (error) {
		device_printf(dev, "could not create RX buf DMA tag\n");
		return error;
	}

	/* Create tmp DMA map for loading RX mbuf */
	error = bus_dmamap_create(cd->sis_rxbuf_tag, BUS_DMA_WAITOK,
				  &cd->sis_rx_tmpmap);
	if (error) {
		device_printf(dev, "could not create RX buf tmp DMA map\n");
		bus_dma_tag_destroy(cd->sis_rxbuf_tag);
		cd->sis_rxbuf_tag = NULL;
		return error;
	}

	/* Create DMA maps for RX mbufs */
	for (i = 0; i < SIS_RX_LIST_CNT; ++i) {
		error = bus_dmamap_create(cd->sis_rxbuf_tag, BUS_DMA_WAITOK,
					  &cd->sis_rx_data[i].sis_map);
		if (error) {
			int j;

			for (j = 0; j < i; ++j) {
				bus_dmamap_destroy(cd->sis_rxbuf_tag,
					cd->sis_rx_data[j].sis_map);
			}
			bus_dmamap_destroy(cd->sis_rxbuf_tag,
					   cd->sis_rx_tmpmap);
			bus_dma_tag_destroy(cd->sis_rxbuf_tag);
			cd->sis_rxbuf_tag = NULL;

			device_printf(dev, "could not create %dth "
				      "RX buf DMA map\n", i);
			return error;
		}
	}
	return 0;
}

static void
sis_dma_free(device_t dev)
{
	struct sis_softc *sc = device_get_softc(dev);
	struct sis_list_data *ld = &sc->sis_ldata;
	struct sis_chain_data *cd = &sc->sis_cdata;
	int i;

	/* Free TX ring */
	if (ld->sis_tx_list != NULL) {
		bus_dmamap_unload(ld->sis_tx_tag, ld->sis_tx_dmamap);
		bus_dmamem_free(ld->sis_tx_tag, ld->sis_tx_list,
				ld->sis_tx_dmamap);
		bus_dma_tag_destroy(ld->sis_tx_tag);
	}

	/* Free RX ring */
	if (ld->sis_rx_list != NULL) {
		bus_dmamap_unload(ld->sis_rx_tag, ld->sis_rx_dmamap);
		bus_dmamem_free(ld->sis_rx_tag, ld->sis_rx_list,
				ld->sis_rx_dmamap);
		bus_dma_tag_destroy(ld->sis_rx_tag);
	}

	/* Destroy DMA stuffs for TX mbufs */
	if (cd->sis_txbuf_tag != NULL) {
		for (i = 0; i < SIS_TX_LIST_CNT; ++i) {
			KKASSERT(cd->sis_tx_data[i].sis_mbuf == NULL);
			bus_dmamap_destroy(cd->sis_txbuf_tag,
					   cd->sis_tx_data[i].sis_map);
		}
		bus_dma_tag_destroy(cd->sis_txbuf_tag);
	}

	/* Destroy DMA stuffs for RX mbufs */
	if (cd->sis_rxbuf_tag != NULL) {
		for (i = 0; i < SIS_RX_LIST_CNT; ++i) {
			KKASSERT(cd->sis_rx_data[i].sis_mbuf == NULL);
			bus_dmamap_destroy(cd->sis_rxbuf_tag,
					   cd->sis_rx_data[i].sis_map);
		}
		bus_dmamap_destroy(cd->sis_rxbuf_tag, cd->sis_rx_tmpmap);
		bus_dma_tag_destroy(cd->sis_rxbuf_tag);
	}
}
