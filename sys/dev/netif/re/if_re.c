/*
 * Copyright (c) 2004
 *	Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * Copyright (c) 1997, 1998-2003
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
 * $FreeBSD: src/sys/dev/re/if_re.c,v 1.25 2004/06/09 14:34:01 naddy Exp $
 */

/*
 * RealTek 8139C+/8169/8169S/8110S/8168/8111/8101E PCI NIC driver
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Networking Software Engineer
 * Wind River Systems
 */

/*
 * This driver is designed to support RealTek's next generation of
 * 10/100 and 10/100/1000 PCI ethernet controllers. There are currently
 * seven devices in this family: the RTL8139C+, the RTL8169, the RTL8169S,
 * RTL8110S, the RTL8168, the RTL8111 and the RTL8101E.
 *
 * The 8139C+ is a 10/100 ethernet chip. It is backwards compatible
 * with the older 8139 family, however it also supports a special
 * C+ mode of operation that provides several new performance enhancing
 * features. These include:
 *
 *	o Descriptor based DMA mechanism. Each descriptor represents
 *	  a single packet fragment. Data buffers may be aligned on
 *	  any byte boundary.
 *
 *	o 64-bit DMA
 *
 *	o TCP/IP checksum offload for both RX and TX
 *
 *	o High and normal priority transmit DMA rings
 *
 *	o VLAN tag insertion and extraction
 *
 *	o TCP large send (segmentation offload)
 *
 * Like the 8139, the 8139C+ also has a built-in 10/100 PHY. The C+
 * programming API is fairly straightforward. The RX filtering, EEPROM
 * access and PHY access is the same as it is on the older 8139 series
 * chips.
 *
 * The 8169 is a 64-bit 10/100/1000 gigabit ethernet MAC. It has almost the
 * same programming API and feature set as the 8139C+ with the following
 * differences and additions:
 *
 *	o 1000Mbps mode
 *
 *	o Jumbo frames
 *
 * 	o GMII and TBI ports/registers for interfacing with copper
 *	  or fiber PHYs
 *
 *      o RX and TX DMA rings can have up to 1024 descriptors
 *        (the 8139C+ allows a maximum of 64)
 *
 *	o Slight differences in register layout from the 8139C+
 *
 * The TX start and timer interrupt registers are at different locations
 * on the 8169 than they are on the 8139C+. Also, the status word in the
 * RX descriptor has a slightly different bit layout. The 8169 does not
 * have a built-in PHY. Most reference boards use a Marvell 88E1000 'Alaska'
 * copper gigE PHY.
 *
 * The 8169S/8110S 10/100/1000 devices have built-in copper gigE PHYs
 * (the 'S' stands for 'single-chip'). These devices have the same
 * programming API as the older 8169, but also have some vendor-specific
 * registers for the on-board PHY. The 8110S is a LAN-on-motherboard
 * part designed to be pin-compatible with the RealTek 8100 10/100 chip.
 * 
 * This driver takes advantage of the RX and TX checksum offload and
 * VLAN tag insertion/extraction features. It also implements TX
 * interrupt moderation using the timer interrupt registers, which
 * significantly reduces TX interrupt load. There is also support
 * for jumbo frames, however the 8169/8169S/8110S can not transmit
 * jumbo frames larger than 7440, so the max MTU possible with this
 * driver is 7422 bytes.
 */

#define _IP_VHL

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/in_cksum.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <netinet/ip.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include "pcidevs.h"
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#include <dev/netif/re/if_rereg.h>
#include <dev/netif/re/if_revar.h>

/*
 * Various supported device vendors/types and their names.
 */
static const struct re_type {
	uint16_t	re_vid;
	uint16_t	re_did;
	const char	*re_name;
} re_devs[] = {
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE528T,
	  "D-Link DGE-528(T) Gigabit Ethernet Adapter" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8139,
	  "RealTek 8139C+ 10/100BaseTX" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8101E,
	  "RealTek 810x PCIe 10/100baseTX" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8168,
	  "RealTek 8111/8168 PCIe Gigabit Ethernet" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169,
	  "RealTek 8110/8169 Gigabit Ethernet" },

	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169SC,
	  "RealTek 8169SC/8110SC Single-chip Gigabit Ethernet" },

	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CG_LAPCIGT,
	  "Corega CG-LAPCIGT Gigabit Ethernet" },

	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_EG1032,
	  "Linksys EG1032 Gigabit Ethernet" },

	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_997902,
	  "US Robotics 997902 Gigabit Ethernet" },

	{ PCI_VENDOR_TTTECH, PCI_PRODUCT_TTTECH_MC322,
	  "TTTech MC322 Gigabit Ethernet" },

	{ 0, 0, NULL }
};

static const struct re_hwrev re_hwrevs[] = {
	{ RE_HWREV_8139CPLUS,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_8139CP | RE_C_FASTE },

	{ RE_HWREV_8169,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_8169 },

	{ RE_HWREV_8110S,	RE_MTU_6K,
	  RE_C_HWCSUM | RE_C_8169 },

	{ RE_HWREV_8169S,	RE_MTU_6K,
	  RE_C_HWCSUM | RE_C_8169 },

	{ RE_HWREV_8169SB,	RE_MTU_6K,
	  RE_C_HWCSUM | RE_C_PHYPMGT | RE_C_8169 },

	{ RE_HWREV_8169SC,	RE_MTU_6K,
	  RE_C_HWCSUM | RE_C_PHYPMGT | RE_C_8169 },

	{ RE_HWREV_8168B1,	RE_MTU_6K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_PHYPMGT },

	{ RE_HWREV_8168B2,	RE_MTU_6K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_PHYPMGT | RE_C_AUTOPAD },

	{ RE_HWREV_8168C,	RE_MTU_6K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168CP,	RE_MTU_6K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168D,	RE_MTU_9K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168DP,	RE_MTU_9K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168E,	RE_MTU_9K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168F,	RE_MTU_9K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8111F,	RE_MTU_9K,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8411,	ETHERMTU,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168G,	ETHERMTU,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168EP,	ETHERMTU,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8168GU,	ETHERMTU,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8411B,	ETHERMTU,
	  RE_C_HWIM | RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT |
	  RE_C_AUTOPAD | RE_C_CONTIGRX | RE_C_STOP_RXTX },

	{ RE_HWREV_8100E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_FASTE },

	{ RE_HWREV_8101E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_FASTE },

	{ RE_HWREV_8102E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_AUTOPAD | RE_C_STOP_RXTX |
	  RE_C_FASTE },

	{ RE_HWREV_8102EL,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_AUTOPAD | RE_C_STOP_RXTX |
	  RE_C_FASTE },

	{ RE_HWREV_8105E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT | RE_C_AUTOPAD |
	  RE_C_STOP_RXTX | RE_C_FASTE },

	{ RE_HWREV_8401E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT | RE_C_AUTOPAD |
	  RE_C_STOP_RXTX | RE_C_FASTE },

	{ RE_HWREV_8402,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT | RE_C_AUTOPAD |
	  RE_C_STOP_RXTX | RE_C_FASTE },

	{ RE_HWREV_8106E,	ETHERMTU,
	  RE_C_HWCSUM | RE_C_MAC2 | RE_C_PHYPMGT | RE_C_AUTOPAD |
	  RE_C_STOP_RXTX | RE_C_FASTE },

	{ RE_HWREV_NULL, 0, 0 }
};

static int	re_probe(device_t);
static int	re_attach(device_t);
static int	re_detach(device_t);
static int	re_suspend(device_t);
static int	re_resume(device_t);
static void	re_shutdown(device_t);

static int	re_allocmem(device_t);
static void	re_freemem(device_t);
static void	re_freebufmem(struct re_softc *, int, int);
static int	re_encap(struct re_softc *, struct mbuf **, int *);
static int	re_newbuf_std(struct re_softc *, int, int);
static int	re_newbuf_jumbo(struct re_softc *, int, int);
static void	re_setup_rxdesc(struct re_softc *, int);
static int	re_rx_list_init(struct re_softc *);
static int	re_tx_list_init(struct re_softc *);
static int	re_rxeof(struct re_softc *);
static int	re_txeof(struct re_softc *);
static int	re_tx_collect(struct re_softc *);
static void	re_intr(void *);
static void	re_tick(void *);
static void	re_tick_serialized(void *);

static void	re_start(struct ifnet *, struct ifaltq_subque *);
static int	re_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void	re_init(void *);
static void	re_stop(struct re_softc *);
static void	re_watchdog(struct ifnet *);
static int	re_ifmedia_upd(struct ifnet *);
static void	re_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void	re_eeprom_putbyte(struct re_softc *, int);
static void	re_eeprom_getword(struct re_softc *, int, u_int16_t *);
static void	re_read_eeprom(struct re_softc *, caddr_t, int, int);
static void	re_get_eewidth(struct re_softc *);

static int	re_gmii_readreg(device_t, int, int);
static int	re_gmii_writereg(device_t, int, int, int);

static int	re_miibus_readreg(device_t, int, int);
static int	re_miibus_writereg(device_t, int, int, int);
static void	re_miibus_statchg(device_t);

static void	re_setmulti(struct re_softc *);
static void	re_reset(struct re_softc *, int);
static void	re_get_eaddr(struct re_softc *, uint8_t *);

static void	re_setup_hw_im(struct re_softc *);
static void	re_setup_sim_im(struct re_softc *);
static void	re_disable_hw_im(struct re_softc *);
static void	re_disable_sim_im(struct re_softc *);
static void	re_config_imtype(struct re_softc *, int);
static void	re_setup_intr(struct re_softc *, int, int);

static int	re_sysctl_hwtime(SYSCTL_HANDLER_ARGS, int *);
static int	re_sysctl_rxtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_txtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_simtime(SYSCTL_HANDLER_ARGS);
static int	re_sysctl_imtype(SYSCTL_HANDLER_ARGS);

static int	re_jpool_alloc(struct re_softc *);
static void	re_jpool_free(struct re_softc *);
static struct re_jbuf *re_jbuf_alloc(struct re_softc *);
static void	re_jbuf_free(void *);
static void	re_jbuf_ref(void *);

#ifdef RE_DIAG
static int	re_diag(struct re_softc *);
#endif

#ifdef IFPOLL_ENABLE
static void	re_npoll(struct ifnet *, struct ifpoll_info *);
static void	re_npoll_compat(struct ifnet *, void *, int);
#endif

static device_method_t re_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		re_probe),
	DEVMETHOD(device_attach,	re_attach),
	DEVMETHOD(device_detach,	re_detach),
	DEVMETHOD(device_suspend,	re_suspend),
	DEVMETHOD(device_resume,	re_resume),
	DEVMETHOD(device_shutdown,	re_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	re_miibus_readreg),
	DEVMETHOD(miibus_writereg,	re_miibus_writereg),
	DEVMETHOD(miibus_statchg,	re_miibus_statchg),

	DEVMETHOD_END
};

static driver_t re_driver = {
	"re",
	re_methods,
	sizeof(struct re_softc)
};

static devclass_t re_devclass;

DECLARE_DUMMY_MODULE(if_re);
MODULE_DEPEND(if_re, miibus, 1, 1, 1);
DRIVER_MODULE(if_re, pci, re_driver, re_devclass, NULL, NULL);
DRIVER_MODULE(if_re, cardbus, re_driver, re_devclass, NULL, NULL);
DRIVER_MODULE(miibus, re, miibus_driver, miibus_devclass, NULL, NULL);

static int	re_rx_desc_count = RE_RX_DESC_CNT_DEF;
static int	re_tx_desc_count = RE_TX_DESC_CNT_DEF;
static int	re_msi_enable = 0;

TUNABLE_INT("hw.re.rx_desc_count", &re_rx_desc_count);
TUNABLE_INT("hw.re.tx_desc_count", &re_tx_desc_count);
TUNABLE_INT("hw.re.msi.enable", &re_msi_enable);

#define EE_SET(x)	\
	CSR_WRITE_1(sc, RE_EECMD, CSR_READ_1(sc, RE_EECMD) | (x))

#define EE_CLR(x)	\
	CSR_WRITE_1(sc, RE_EECMD, CSR_READ_1(sc, RE_EECMD) & ~(x))

static __inline void
re_free_rxchain(struct re_softc *sc)
{
	if (sc->re_head != NULL) {
		m_freem(sc->re_head);
		sc->re_head = sc->re_tail = NULL;
	}
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
re_eeprom_putbyte(struct re_softc *sc, int addr)
{
	int d, i;

	d = addr | (RE_9346_READ << sc->re_eewidth);

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = 1 << (sc->re_eewidth + 3); i; i >>= 1) {
		if (d & i)
			EE_SET(RE_EE_DATAIN);
		else
			EE_CLR(RE_EE_DATAIN);
		DELAY(100);
		EE_SET(RE_EE_CLK);
		DELAY(150);
		EE_CLR(RE_EE_CLK);
		DELAY(100);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
re_eeprom_getword(struct re_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/*
	 * Send address of word we want to read.
	 */
	re_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i != 0; i >>= 1) {
		EE_SET(RE_EE_CLK);
		DELAY(100);
		if (CSR_READ_1(sc, RE_EECMD) & RE_EE_DATAOUT)
			word |= i;
		EE_CLR(RE_EE_CLK);
		DELAY(100);
	}

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
re_read_eeprom(struct re_softc *sc, caddr_t dest, int off, int cnt)
{
	int i;
	uint16_t word = 0, *ptr;

	CSR_SETBIT_1(sc, RE_EECMD, RE_EEMODE_PROGRAM);
	DELAY(100);

	for (i = 0; i < cnt; i++) {
		CSR_SETBIT_1(sc, RE_EECMD, RE_EE_SEL);
		re_eeprom_getword(sc, off + i, &word);
		CSR_CLRBIT_1(sc, RE_EECMD, RE_EE_SEL);
		ptr = (uint16_t *)(dest + (i * 2));
		*ptr = word;
	}

	CSR_CLRBIT_1(sc, RE_EECMD, RE_EEMODE_PROGRAM);
}

static void
re_get_eewidth(struct re_softc *sc)
{
	uint16_t re_did = 0;

	sc->re_eewidth = 6;
	re_read_eeprom(sc, (caddr_t)&re_did, 0, 1);
	if (re_did != 0x8129)
		sc->re_eewidth = 8;
}

static int
re_gmii_readreg(device_t dev, int phy, int reg)
{
	struct re_softc *sc = device_get_softc(dev);
	u_int32_t rval;
	int i;

	if (phy != 1)
		return(0);

	/* Let the rgephy driver read the GMEDIASTAT register */

	if (reg == RE_GMEDIASTAT)
		return(CSR_READ_1(sc, RE_GMEDIASTAT));

	CSR_WRITE_4(sc, RE_PHYAR, reg << 16);
	DELAY(1000);

	for (i = 0; i < RE_TIMEOUT; i++) {
		rval = CSR_READ_4(sc, RE_PHYAR);
		if (rval & RE_PHYAR_BUSY)
			break;
		DELAY(100);
	}

	if (i == RE_TIMEOUT) {
		device_printf(dev, "PHY read failed\n");
		return(0);
	}

	return(rval & RE_PHYAR_PHYDATA);
}

static int
re_gmii_writereg(device_t dev, int phy, int reg, int data)
{
	struct re_softc *sc = device_get_softc(dev);
	uint32_t rval;
	int i;

	CSR_WRITE_4(sc, RE_PHYAR,
		    (reg << 16) | (data & RE_PHYAR_PHYDATA) | RE_PHYAR_BUSY);
	DELAY(1000);

	for (i = 0; i < RE_TIMEOUT; i++) {
		rval = CSR_READ_4(sc, RE_PHYAR);
		if ((rval & RE_PHYAR_BUSY) == 0)
			break;
		DELAY(100);
	}

	if (i == RE_TIMEOUT)
		device_printf(dev, "PHY write failed\n");

	return(0);
}

static int
re_miibus_readreg(device_t dev, int phy, int reg)
{
	struct re_softc	*sc = device_get_softc(dev);
	uint16_t rval = 0;
	uint16_t re8139_reg = 0;

	if (!RE_IS_8139CP(sc)) {
		rval = re_gmii_readreg(dev, phy, reg);
		return(rval);
	}

	/* Pretend the internal PHY is only at address 0 */
	if (phy)
		return(0);

	switch(reg) {
	case MII_BMCR:
		re8139_reg = RE_BMCR;
		break;
	case MII_BMSR:
		re8139_reg = RE_BMSR;
		break;
	case MII_ANAR:
		re8139_reg = RE_ANAR;
		break;
	case MII_ANER:
		re8139_reg = RE_ANER;
		break;
	case MII_ANLPAR:
		re8139_reg = RE_LPAR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		return(0);
	/*
	 * Allow the rlphy driver to read the media status
	 * register. If we have a link partner which does not
	 * support NWAY, this is the register which will tell
	 * us the results of parallel detection.
	 */
	case RE_MEDIASTAT:
		return(CSR_READ_1(sc, RE_MEDIASTAT));
	default:
		device_printf(dev, "bad phy register\n");
		return(0);
	}
	rval = CSR_READ_2(sc, re8139_reg);
	if (re8139_reg == RE_BMCR) {
		/* 8139C+ has different bit layout. */
		rval &= ~(BMCR_LOOP | BMCR_ISO);
	}
	return(rval);
}

static int
re_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct re_softc *sc= device_get_softc(dev);
	u_int16_t re8139_reg = 0;

	if (!RE_IS_8139CP(sc))
		return(re_gmii_writereg(dev, phy, reg, data));

	/* Pretend the internal PHY is only at address 0 */
	if (phy)
		return(0);

	switch(reg) {
	case MII_BMCR:
		re8139_reg = RE_BMCR;
		/* 8139C+ has different bit layout. */
		data &= ~(BMCR_LOOP | BMCR_ISO);
		break;
	case MII_BMSR:
		re8139_reg = RE_BMSR;
		break;
	case MII_ANAR:
		re8139_reg = RE_ANAR;
		break;
	case MII_ANER:
		re8139_reg = RE_ANER;
		break;
	case MII_ANLPAR:
		re8139_reg = RE_LPAR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		return(0);
	default:
		device_printf(dev, "bad phy register\n");
		return(0);
	}
	CSR_WRITE_2(sc, re8139_reg, data);
	return(0);
}

static void
re_miibus_statchg(device_t dev)
{
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
re_setmulti(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int h = 0;
	uint32_t hashes[2] = { 0, 0 };
	struct ifmultiaddr *ifma;
	uint32_t rxfilt;
	int mcnt = 0;

	rxfilt = CSR_READ_4(sc, RE_RXCFG);

	/* Set the individual bit to receive frames for this host only. */
	rxfilt |= RE_RXCFG_RX_INDIV;
	/* Set capture broadcast bit to capture broadcast frames. */
	rxfilt |= RE_RXCFG_RX_BROAD;

	rxfilt &= ~(RE_RXCFG_RX_ALLPHYS | RE_RXCFG_RX_MULTI);
	if ((ifp->if_flags & IFF_ALLMULTI) || (ifp->if_flags & IFF_PROMISC)) {
		rxfilt |= RE_RXCFG_RX_MULTI;

		/* If we want promiscuous mode, set the allframes bit. */
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= RE_RXCFG_RX_ALLPHYS;

		CSR_WRITE_4(sc, RE_RXCFG, rxfilt);
		CSR_WRITE_4(sc, RE_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, RE_MAR4, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, RE_MAR0, 0);
	CSR_WRITE_4(sc, RE_MAR4, 0);

	/* now program new ones */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}

	if (mcnt)
		rxfilt |= RE_RXCFG_RX_MULTI;
	else
		rxfilt &= ~RE_RXCFG_RX_MULTI;

	CSR_WRITE_4(sc, RE_RXCFG, rxfilt);

	/*
	 * For some unfathomable reason, RealTek decided to reverse
	 * the order of the multicast hash registers in the PCI Express
	 * parts. This means we have to write the hash pattern in reverse
	 * order for those devices.
	 */
	if (sc->re_caps & RE_C_PCIE) {
		CSR_WRITE_4(sc, RE_MAR0, bswap32(hashes[1]));
		CSR_WRITE_4(sc, RE_MAR4, bswap32(hashes[0]));
	} else {
		CSR_WRITE_4(sc, RE_MAR0, hashes[0]);
		CSR_WRITE_4(sc, RE_MAR4, hashes[1]);
	}
}

static void
re_reset(struct re_softc *sc, int running)
{
	int i;

	if ((sc->re_caps & RE_C_STOP_RXTX) && running) {
		CSR_WRITE_1(sc, RE_COMMAND,
			    RE_CMD_STOPREQ | RE_CMD_TX_ENB | RE_CMD_RX_ENB);
		DELAY(100);
	}

	CSR_WRITE_1(sc, RE_COMMAND, RE_CMD_RESET);

	for (i = 0; i < RE_TIMEOUT; i++) {
		DELAY(10);
		if ((CSR_READ_1(sc, RE_COMMAND) & RE_CMD_RESET) == 0)
			break;
	}
	if (i == RE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "reset never completed!\n");
}

#ifdef RE_DIAG
/*
 * The following routine is designed to test for a defect on some
 * 32-bit 8169 cards. Some of these NICs have the REQ64# and ACK64#
 * lines connected to the bus, however for a 32-bit only card, they
 * should be pulled high. The result of this defect is that the
 * NIC will not work right if you plug it into a 64-bit slot: DMA
 * operations will be done with 64-bit transfers, which will fail
 * because the 64-bit data lines aren't connected.
 *
 * There's no way to work around this (short of talking a soldering
 * iron to the board), however we can detect it. The method we use
 * here is to put the NIC into digital loopback mode, set the receiver
 * to promiscuous mode, and then try to send a frame. We then compare
 * the frame data we sent to what was received. If the data matches,
 * then the NIC is working correctly, otherwise we know the user has
 * a defective NIC which has been mistakenly plugged into a 64-bit PCI
 * slot. In the latter case, there's no way the NIC can work correctly,
 * so we print out a message on the console and abort the device attach.
 */

static int
re_diag(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m0;
	struct ether_header *eh;
	struct re_desc *cur_rx;
	uint16_t status;
	int total_len, i, error = 0, phyaddr;
	uint8_t dst[ETHER_ADDR_LEN] = { 0x00, 'h', 'e', 'l', 'l', 'o' };
	uint8_t src[ETHER_ADDR_LEN] = { 0x00, 'w', 'o', 'r', 'l', 'd' };
	char ethstr[2][ETHER_ADDRSTRLEN + 1];

	/* Allocate a single mbuf */

	MGETHDR(m0, MB_DONTWAIT, MT_DATA);
	if (m0 == NULL)
		return(ENOBUFS);

	/*
	 * Initialize the NIC in test mode. This sets the chip up
	 * so that it can send and receive frames, but performs the
	 * following special functions:
	 * - Puts receiver in promiscuous mode
	 * - Enables digital loopback mode
	 * - Leaves interrupts turned off
	 */

	ifp->if_flags |= IFF_PROMISC;
	sc->re_flags |= RE_F_TESTMODE;
	re_init(sc);
	sc->re_flags |= RE_F_LINKED;
	if (!RE_IS_8139CP(sc))
		phyaddr = 1;
	else
		phyaddr = 0;

	re_miibus_writereg(sc->re_dev, phyaddr, MII_BMCR, BMCR_RESET);
	for (i = 0; i < RE_TIMEOUT; i++) {
		status = re_miibus_readreg(sc->re_dev, phyaddr, MII_BMCR);
		if (!(status & BMCR_RESET))
			break;
	}

	re_miibus_writereg(sc->re_dev, phyaddr, MII_BMCR, BMCR_LOOP);
	CSR_WRITE_2(sc, RE_ISR, RE_INTRS_DIAG);

	DELAY(100000);

	/* Put some data in the mbuf */

	eh = mtod(m0, struct ether_header *);
	bcopy (dst, eh->ether_dhost, ETHER_ADDR_LEN);
	bcopy (src, eh->ether_shost, ETHER_ADDR_LEN);
	eh->ether_type = htons(ETHERTYPE_IP);
	m0->m_pkthdr.len = m0->m_len = ETHER_MIN_LEN - ETHER_CRC_LEN;

	/*
	 * Queue the packet, start transmission.
	 * Note: ifq_handoff() ultimately calls re_start() for us.
	 */

	CSR_WRITE_2(sc, RE_ISR, 0xFFFF);
	error = ifq_handoff(ifp, m0, NULL);
	if (error) {
		m0 = NULL;
		goto done;
	}
	m0 = NULL;

	/* Wait for it to propagate through the chip */

	DELAY(100000);
	for (i = 0; i < RE_TIMEOUT; i++) {
		status = CSR_READ_2(sc, RE_ISR);
		CSR_WRITE_2(sc, RE_ISR, status);
		if ((status & (RE_ISR_TIMEOUT_EXPIRED|RE_ISR_RX_OK)) ==
		    (RE_ISR_TIMEOUT_EXPIRED|RE_ISR_RX_OK))
			break;
		DELAY(10);
	}

	if (i == RE_TIMEOUT) {
		if_printf(ifp, "diagnostic failed to receive packet "
			  "in loopback mode\n");
		error = EIO;
		goto done;
	}

	/*
	 * The packet should have been dumped into the first
	 * entry in the RX DMA ring. Grab it from there.
	 */

	bus_dmamap_sync(sc->re_ldata.re_rx_mtag, sc->re_ldata.re_rx_dmamap[0],
			BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->re_ldata.re_rx_mtag,
			  sc->re_ldata.re_rx_dmamap[0]);

	m0 = sc->re_ldata.re_rx_mbuf[0];
	sc->re_ldata.re_rx_mbuf[0] = NULL;
	eh = mtod(m0, struct ether_header *);

	cur_rx = &sc->re_ldata.re_rx_list[0];
	total_len = RE_RXBYTES(cur_rx);

	if (total_len != ETHER_MIN_LEN) {
		if_printf(ifp, "diagnostic failed, received short packet\n");
		error = EIO;
		goto done;
	}

	/* Test that the received packet data matches what we sent. */

	if (bcmp(eh->ether_dhost, dst, ETHER_ADDR_LEN) ||
	    bcmp(eh->ether_shost, &src, ETHER_ADDR_LEN) ||
	    be16toh(eh->ether_type) != ETHERTYPE_IP) {
		if_printf(ifp, "WARNING, DMA FAILURE!\n");
		if_printf(ifp, "expected TX data: %s/%s/0x%x\n",
		    kether_ntoa(dst, ethstr[0]), kether_ntoa(src, ethstr[1]), ETHERTYPE_IP);
		if_printf(ifp, "received RX data: %s/%s/0x%x\n",
		    kether_ntoa(eh->ether_dhost, ethstr[0]),
		    kether_ntoa(eh->ether_shost, ethstr[1]),
		    ntohs(eh->ether_type));
		if_printf(ifp, "You may have a defective 32-bit NIC plugged "
		    "into a 64-bit PCI slot.\n");
		if_printf(ifp, "Please re-install the NIC in a 32-bit slot "
		    "for proper operation.\n");
		if_printf(ifp, "Read the re(4) man page for more details.\n");
		error = EIO;
	}

done:
	/* Turn interface off, release resources */

	sc->re_flags &= ~(RE_F_LINKED | RE_F_TESTMODE);
	ifp->if_flags &= ~IFF_PROMISC;
	re_stop(sc);
	if (m0 != NULL)
		m_freem(m0);

	return (error);
}
#endif	/* RE_DIAG */

/*
 * Probe for a RealTek 8139C+/8169/8110 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
re_probe(device_t dev)
{
	const struct re_type *t;
	const struct re_hwrev *hw_rev;
	struct re_softc *sc;
	int rid;
	uint32_t hwrev, macmode, txcfg;
	uint16_t vendor, product;

	vendor = pci_get_vendor(dev);
	product = pci_get_device(dev);

	/*
	 * Only attach to rev.3 of the Linksys EG1032 adapter.
	 * Rev.2 is supported by sk(4).
	 */
	if (vendor == PCI_VENDOR_LINKSYS &&
	    product == PCI_PRODUCT_LINKSYS_EG1032 &&
	    pci_get_subdevice(dev) != PCI_SUBDEVICE_LINKSYS_EG1032_REV3)
		return ENXIO;

	if (vendor == PCI_VENDOR_REALTEK &&
	    product == PCI_PRODUCT_REALTEK_RT8139 &&
	    pci_get_revid(dev) != PCI_REVID_REALTEK_RT8139CP) {
		/* Poor 8139 */
		return ENXIO;
	}

	for (t = re_devs; t->re_name != NULL; t++) {
		if (product == t->re_did && vendor == t->re_vid)
			break;
	}

	/*
	 * Check if we found a RealTek device.
	 */
	if (t->re_name == NULL)
		return ENXIO;

	/*
	 * Temporarily map the I/O space so we can read the chip ID register.
	 */
	sc = kmalloc(sizeof(*sc), M_TEMP, M_WAITOK | M_ZERO);
	rid = RE_PCI_LOIO;
	sc->re_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
					    RF_ACTIVE);
	if (sc->re_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		kfree(sc, M_TEMP);
		return ENXIO;
	}

	sc->re_btag = rman_get_bustag(sc->re_res);
	sc->re_bhandle = rman_get_bushandle(sc->re_res);

	txcfg = CSR_READ_4(sc, RE_TXCFG);
	hwrev = txcfg & RE_TXCFG_HWREV;
	macmode = txcfg & RE_TXCFG_MACMODE;
	bus_release_resource(dev, SYS_RES_IOPORT, RE_PCI_LOIO, sc->re_res);
	kfree(sc, M_TEMP);

	/*
	 * and continue matching for the specific chip...
	 */
	for (hw_rev = re_hwrevs; hw_rev->re_hwrev != RE_HWREV_NULL; hw_rev++) {
		if (hw_rev->re_hwrev == hwrev) {
			sc = device_get_softc(dev);

			sc->re_hwrev = hw_rev->re_hwrev;
			sc->re_caps = hw_rev->re_caps;
			sc->re_maxmtu = hw_rev->re_maxmtu;

			/*
			 * Apply chip property fixup
			 */
			switch (sc->re_hwrev) {
			case RE_HWREV_8168GU:
				if (vendor == PCI_VENDOR_REALTEK &&
				    product == PCI_PRODUCT_REALTEK_RT8101E) {
					/* 8106EUS */
					sc->re_caps = RE_C_HWCSUM | RE_C_MAC2 |
					    RE_C_PHYPMGT | RE_C_AUTOPAD |
					    RE_C_STOP_RXTX | RE_C_FASTE;
					sc->re_maxmtu = ETHERMTU;
					device_printf(dev, "8106EUS fixup\n");
				} else {
					/* 8168GU */
					goto ee_eaddr1;
				}
				break;

			case RE_HWREV_8168E:
				if (vendor == PCI_VENDOR_REALTEK &&
				    product == PCI_PRODUCT_REALTEK_RT8101E) {
					/* 8105E */
					sc->re_caps = RE_C_HWCSUM | RE_C_MAC2 |
					    RE_C_PHYPMGT | RE_C_AUTOPAD |
					    RE_C_STOP_RXTX | RE_C_FASTE;
					sc->re_maxmtu = ETHERMTU;
					device_printf(dev, "8105E fixup\n");
					goto ee_eaddr0;
				}
				/* 8168E */
				break;

			case RE_HWREV_8101E:
			case RE_HWREV_8102E:
			case RE_HWREV_8102EL:
			case RE_HWREV_8401E:
			case RE_HWREV_8105E:
			case RE_HWREV_8106E:
ee_eaddr0:
				sc->re_caps |= RE_C_EE_EADDR;
				sc->re_ee_eaddr = RE_EE_EADDR0;
				break;

			case RE_HWREV_8168F:
			case RE_HWREV_8111F:
			case RE_HWREV_8168G:
				if (macmode == 0 ||
				    macmode == 0x100000) {
					sc->re_caps |= RE_C_EE_EADDR;
					sc->re_ee_eaddr = RE_EE_EADDR1;
				}
				break;

			case RE_HWREV_8411:
			case RE_HWREV_8168EP:
			case RE_HWREV_8411B:
ee_eaddr1:
				sc->re_caps |= RE_C_EE_EADDR;
				sc->re_ee_eaddr = RE_EE_EADDR1;
				break;
			}
			if (pci_is_pcie(dev))
				sc->re_caps |= RE_C_PCIE;

			device_set_desc(dev, t->re_name);
			return 0;
		}
	}
	device_printf(dev, "unknown hwrev 0x%08x, macmode 0x%08x\n",
	    hwrev, macmode);

	return ENXIO;
}

static int
re_allocmem(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	bus_dmamem_t dmem;
	int error, i;

	/*
	 * Allocate list data
	 */
	sc->re_ldata.re_tx_mbuf =
	kmalloc(sc->re_tx_desc_cnt * sizeof(struct mbuf *),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_mbuf =
	kmalloc(sc->re_rx_desc_cnt * sizeof(struct mbuf *),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_paddr =
	kmalloc(sc->re_rx_desc_cnt * sizeof(bus_addr_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_tx_dmamap =
	kmalloc(sc->re_tx_desc_cnt * sizeof(bus_dmamap_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	sc->re_ldata.re_rx_dmamap =
	kmalloc(sc->re_rx_desc_cnt * sizeof(bus_dmamap_t),
		M_DEVBUF, M_ZERO | M_WAITOK);

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR,	/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			NULL, NULL,		/* filter, filterarg */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
			0,			/* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			0,			/* flags */
			&sc->re_parent_tag);
	if (error) {
		device_printf(dev, "could not allocate parent dma tag\n");
		return error;
	}

	/* Allocate TX descriptor list. */
	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RING_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RE_TX_LIST_SZ(sc), BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&dmem);
	if (error) {
		device_printf(dev, "could not allocate TX ring\n");
		return error;
	}
	sc->re_ldata.re_tx_list_tag = dmem.dmem_tag;
	sc->re_ldata.re_tx_list_map = dmem.dmem_map;
	sc->re_ldata.re_tx_list = dmem.dmem_addr;
	sc->re_ldata.re_tx_list_addr = dmem.dmem_busaddr;

	/* Allocate RX descriptor list. */
	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RING_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			RE_RX_LIST_SZ(sc), BUS_DMA_WAITOK | BUS_DMA_ZERO,
			&dmem);
	if (error) {
		device_printf(dev, "could not allocate RX ring\n");
		return error;
	}
	sc->re_ldata.re_rx_list_tag = dmem.dmem_tag;
	sc->re_ldata.re_rx_list_map = dmem.dmem_map;
	sc->re_ldata.re_rx_list = dmem.dmem_addr;
	sc->re_ldata.re_rx_list_addr = dmem.dmem_busaddr;

	/* Allocate maps for TX mbufs. */
	error = bus_dma_tag_create(sc->re_parent_tag,
			1, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			NULL, NULL,
			RE_FRAMELEN_MAX, RE_MAXSEGS, MCLBYTES,
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
			&sc->re_ldata.re_tx_mtag);
	if (error) {
		device_printf(dev, "could not allocate TX buf dma tag\n");
		return(error);
	}

	/* Create DMA maps for TX buffers */
	for (i = 0; i < sc->re_tx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->re_ldata.re_tx_mtag,
				BUS_DMA_WAITOK | BUS_DMA_ONEBPAGE,
				&sc->re_ldata.re_tx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for TX buf\n");
			re_freebufmem(sc, i, 0);
			return(error);
		}
	}

	/* Allocate maps for RX mbufs. */
	error = bus_dma_tag_create(sc->re_parent_tag,
			RE_RXBUF_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			NULL, NULL,
			MCLBYTES, 1, MCLBYTES,
			BUS_DMA_ALLOCNOW | BUS_DMA_WAITOK | BUS_DMA_ALIGNED,
			&sc->re_ldata.re_rx_mtag);
	if (error) {
		device_printf(dev, "could not allocate RX buf dma tag\n");
		return(error);
	}

	/* Create spare DMA map for RX */
	error = bus_dmamap_create(sc->re_ldata.re_rx_mtag, BUS_DMA_WAITOK,
			&sc->re_ldata.re_rx_spare);
	if (error) {
		device_printf(dev, "can't create spare DMA map for RX\n");
		bus_dma_tag_destroy(sc->re_ldata.re_rx_mtag);
		sc->re_ldata.re_rx_mtag = NULL;
		return error;
	}

	/* Create DMA maps for RX buffers */
	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->re_ldata.re_rx_mtag,
				BUS_DMA_WAITOK, &sc->re_ldata.re_rx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for RX buf\n");
			re_freebufmem(sc, sc->re_tx_desc_cnt, i);
			return(error);
		}
	}

	/* Create jumbo buffer pool for RX if required */
	if (sc->re_caps & RE_C_CONTIGRX) {
		error = re_jpool_alloc(sc);
		if (error) {
			re_jpool_free(sc);
			/* Disable jumbo frame support */
			sc->re_maxmtu = ETHERMTU;
		}
	}
	return(0);
}

static void
re_freebufmem(struct re_softc *sc, int tx_cnt, int rx_cnt)
{
	int i;

	/* Destroy all the RX and TX buffer maps */
	if (sc->re_ldata.re_tx_mtag) {
		for (i = 0; i < tx_cnt; i++) {
			bus_dmamap_destroy(sc->re_ldata.re_tx_mtag,
					   sc->re_ldata.re_tx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->re_ldata.re_tx_mtag);
		sc->re_ldata.re_tx_mtag = NULL;
	}

	if (sc->re_ldata.re_rx_mtag) {
		for (i = 0; i < rx_cnt; i++) {
			bus_dmamap_destroy(sc->re_ldata.re_rx_mtag,
					   sc->re_ldata.re_rx_dmamap[i]);
		}
		bus_dmamap_destroy(sc->re_ldata.re_rx_mtag,
				   sc->re_ldata.re_rx_spare);
		bus_dma_tag_destroy(sc->re_ldata.re_rx_mtag);
		sc->re_ldata.re_rx_mtag = NULL;
	}
}

static void
re_freemem(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);

	/* Unload and free the RX DMA ring memory and map */
	if (sc->re_ldata.re_rx_list_tag) {
		bus_dmamap_unload(sc->re_ldata.re_rx_list_tag,
				  sc->re_ldata.re_rx_list_map);
		bus_dmamem_free(sc->re_ldata.re_rx_list_tag,
				sc->re_ldata.re_rx_list,
				sc->re_ldata.re_rx_list_map);
		bus_dma_tag_destroy(sc->re_ldata.re_rx_list_tag);
	}

	/* Unload and free the TX DMA ring memory and map */
	if (sc->re_ldata.re_tx_list_tag) {
		bus_dmamap_unload(sc->re_ldata.re_tx_list_tag,
				  sc->re_ldata.re_tx_list_map);
		bus_dmamem_free(sc->re_ldata.re_tx_list_tag,
				sc->re_ldata.re_tx_list,
				sc->re_ldata.re_tx_list_map);
		bus_dma_tag_destroy(sc->re_ldata.re_tx_list_tag);
	}

	/* Free RX/TX buf DMA stuffs */
	re_freebufmem(sc, sc->re_tx_desc_cnt, sc->re_rx_desc_cnt);

	/* Unload and free the stats buffer and map */
	if (sc->re_ldata.re_stag) {
		bus_dmamap_unload(sc->re_ldata.re_stag, sc->re_ldata.re_smap);
		bus_dmamem_free(sc->re_ldata.re_stag,
				sc->re_ldata.re_stats,
				sc->re_ldata.re_smap);
		bus_dma_tag_destroy(sc->re_ldata.re_stag);
	}

	if (sc->re_caps & RE_C_CONTIGRX)
		re_jpool_free(sc);

	if (sc->re_parent_tag)
		bus_dma_tag_destroy(sc->re_parent_tag);

	if (sc->re_ldata.re_tx_mbuf != NULL)
		kfree(sc->re_ldata.re_tx_mbuf, M_DEVBUF);
	if (sc->re_ldata.re_rx_mbuf != NULL)
		kfree(sc->re_ldata.re_rx_mbuf, M_DEVBUF);
	if (sc->re_ldata.re_rx_paddr != NULL)
		kfree(sc->re_ldata.re_rx_paddr, M_DEVBUF);
	if (sc->re_ldata.re_tx_dmamap != NULL)
		kfree(sc->re_ldata.re_tx_dmamap, M_DEVBUF);
	if (sc->re_ldata.re_rx_dmamap != NULL)
		kfree(sc->re_ldata.re_rx_dmamap, M_DEVBUF);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
re_attach(device_t dev)
{
	struct re_softc	*sc = device_get_softc(dev);
	struct ifnet *ifp;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int error = 0, qlen, msi_enable;
	u_int irq_flags;

	callout_init_mp(&sc->re_timer);
	sc->re_dev = dev;

	if (RE_IS_8139CP(sc)) {
		sc->re_rx_desc_cnt = RE_RX_DESC_CNT_8139CP;
		sc->re_tx_desc_cnt = RE_TX_DESC_CNT_8139CP;
	} else {
		sc->re_rx_desc_cnt = re_rx_desc_count;
		if (sc->re_rx_desc_cnt > RE_RX_DESC_CNT_MAX)
			sc->re_rx_desc_cnt = RE_RX_DESC_CNT_MAX;

		sc->re_tx_desc_cnt = re_tx_desc_count;
		if (sc->re_tx_desc_cnt > RE_TX_DESC_CNT_MAX)
			sc->re_tx_desc_cnt = RE_TX_DESC_CNT_MAX;
	}

	qlen = RE_IFQ_MAXLEN;
	if (sc->re_tx_desc_cnt > qlen)
		qlen = sc->re_tx_desc_cnt;

	sc->re_rxbuf_size = MCLBYTES;
	sc->re_newbuf = re_newbuf_std;

	sc->re_tx_time = 5;		/* 125us */
	sc->re_rx_time = 2;		/* 50us */
	if (sc->re_caps & RE_C_PCIE)
		sc->re_sim_time = 75;	/* 75us */
	else
		sc->re_sim_time = 125;	/* 125us */
	if (!RE_IS_8139CP(sc)) {
		/* simulated interrupt moderation */
		sc->re_imtype = RE_IMTYPE_SIM;
	} else {
		sc->re_imtype = RE_IMTYPE_NONE;
	}
	re_config_imtype(sc, sc->re_imtype);

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "rx_desc_count", CTLFLAG_RD, &sc->re_rx_desc_cnt,
		       0, "RX desc count");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "tx_desc_count", CTLFLAG_RD, &sc->re_tx_desc_cnt,
		       0, "TX desc count");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "sim_time",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, re_sysctl_simtime, "I",
			"Simulated interrupt moderation time (usec).");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "imtype",
			CTLTYPE_INT | CTLFLAG_RW,
			sc, 0, re_sysctl_imtype, "I",
			"Interrupt moderation type -- "
			"0:disable, 1:simulated, "
			"2:hardware(if supported)");
	if (sc->re_caps & RE_C_HWIM) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
				OID_AUTO, "hw_rxtime",
				CTLTYPE_INT | CTLFLAG_RW,
				sc, 0, re_sysctl_rxtime, "I",
				"Hardware interrupt moderation time "
				"(unit: 25usec).");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree),
				OID_AUTO, "hw_txtime",
				CTLTYPE_INT | CTLFLAG_RW,
				sc, 0, re_sysctl_txtime, "I",
				"Hardware interrupt moderation time "
				"(unit: 25usec).");
	}

#ifndef BURN_BRIDGES
	/*
	 * Handle power management nonsense.
	 */

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t membase, irq;

		/* Save important PCI config data. */
		membase = pci_read_config(dev, RE_PCI_LOMEM, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);

		/* Reset the power state. */
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		/* Restore PCI config data. */
		pci_write_config(dev, RE_PCI_LOMEM, membase, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif
	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	if (pci_is_pcie(dev)) {
		sc->re_res_rid = PCIR_BAR(2);
		sc->re_res_type = SYS_RES_MEMORY;
	} else {
		sc->re_res_rid = PCIR_BAR(0);
		sc->re_res_type = SYS_RES_IOPORT;
	}
	sc->re_res = bus_alloc_resource_any(dev, sc->re_res_type,
	    &sc->re_res_rid, RF_ACTIVE);
	if (sc->re_res == NULL) {
		device_printf(dev, "couldn't map IO\n");
		error = ENXIO;
		goto fail;
	}

	sc->re_btag = rman_get_bustag(sc->re_res);
	sc->re_bhandle = rman_get_bushandle(sc->re_res);

	/*
	 * Allocate interrupt
	 */
	if (pci_is_pcie(dev))
		msi_enable = re_msi_enable;
	else
		msi_enable = 0;
	sc->re_irq_type = pci_alloc_1intr(dev, msi_enable,
	    &sc->re_irq_rid, &irq_flags);

	sc->re_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->re_irq_rid,
					    irq_flags);
	if (sc->re_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	re_reset(sc, 0);

	if (RE_IS_8139CP(sc)) {
		sc->re_bus_speed = 33; /* XXX */
	} else if (sc->re_caps & RE_C_PCIE) {
		sc->re_bus_speed = 125;
	} else {
		uint8_t cfg2;

		cfg2 = CSR_READ_1(sc, RE_CFG2);
		switch (cfg2 & RE_CFG2_PCICLK_MASK) {
		case RE_CFG2_PCICLK_33MHZ:
			sc->re_bus_speed = 33;
			break;
		case RE_CFG2_PCICLK_66MHZ:
			sc->re_bus_speed = 66;
			break;
		default:
			device_printf(dev, "unknown bus speed, assume 33MHz\n");
			sc->re_bus_speed = 33;
			break;
		}
		if (cfg2 & RE_CFG2_PCI64)
			sc->re_caps |= RE_C_PCI64;
	}
	device_printf(dev, "Hardware rev. 0x%08x; PCI%s %dMHz\n",
		      sc->re_hwrev,
		      (sc->re_caps & RE_C_PCIE) ?
		      "-E" : ((sc->re_caps & RE_C_PCI64) ? "64" : "32"),
		      sc->re_bus_speed);

	/*
	 * NOTE:
	 * DO NOT try to adjust config1 and config5 which was spotted in
	 * Realtek's Linux drivers.  It will _permanently_ damage certain
	 * cards EEPROM, e.g. one of my 8168B (0x38000000) card ...
	 */

	re_get_eaddr(sc, eaddr);

	if (!RE_IS_8139CP(sc)) {
		/* Set RX length mask */
		sc->re_rxlenmask = RE_RDESC_STAT_GFRAGLEN;
		sc->re_txstart = RE_GTXSTART;
	} else {
		/* Set RX length mask */
		sc->re_rxlenmask = RE_RDESC_STAT_FRAGLEN;
		sc->re_txstart = RE_TXSTART;
	}

	/* Allocate DMA stuffs */
	error = re_allocmem(dev);
	if (error)
		goto fail;

	/*
	 * Apply some magic PCI settings from Realtek ...
	 */
	if (RE_IS_8169(sc)) {
		CSR_WRITE_1(sc, 0x82, 1);
		pci_write_config(dev, PCIR_CACHELNSZ, 0x8, 1);
	}
	pci_write_config(dev, PCIR_LATTIMER, 0x40, 1);

	if (sc->re_caps & RE_C_MAC2) {
		/*
		 * Following part is extracted from Realtek BSD driver v176.
		 * However, this does _not_ make much/any sense:
		 * 8168C's PCI Express device control is located at 0x78,
		 * so the reading from 0x79 (higher part of 0x78) and setting
		 * the 4~6bits intend to enlarge the "max read request size"
		 * (we will do it).  The content of the rest part of this
		 * register is not meaningful to other PCI registers, so
		 * writing the value to 0x54 could be completely wrong.
		 * 0x80 is the lower part of PCI Express device status, non-
		 * reserved bits are RW1C, writing 0 to them will not have
		 * any effect at all.
		 */
#ifdef foo
		uint8_t val;

		val = pci_read_config(dev, 0x79, 1);
		val = (val & ~0x70) | 0x50;
		pci_write_config(dev, 0x54, val, 1);
		pci_write_config(dev, 0x80, 0, 1);
#endif
	}

	/*
	 * Apply some PHY fixup from Realtek ...
	 */
	if (sc->re_hwrev == RE_HWREV_8110S) {
		CSR_WRITE_1(sc, 0x82, 1);
		re_miibus_writereg(dev, 1, 0xb, 0);
	}
	if (sc->re_caps & RE_C_PHYPMGT) {
		/* Power up PHY */
		re_miibus_writereg(dev, 1, 0x1f, 0);
		re_miibus_writereg(dev, 1, 0xe, 0);
	}

	/* Do MII setup */
	if (mii_phy_probe(dev, &sc->re_miibus,
	    re_ifmedia_upd, re_ifmedia_sts)) {
		device_printf(dev, "MII without any phy!\n");
		error = ENXIO;
		goto fail;
	}

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = re_ioctl;
	ifp->if_start = re_start;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = re_npoll;
#endif
	ifp->if_watchdog = re_watchdog;
	ifp->if_init = re_init;
	if (!RE_IS_8139CP(sc)) /* XXX */
		ifp->if_baudrate = 1000000000;
	else
		ifp->if_baudrate = 100000000;
	ifq_set_maxlen(&ifp->if_snd, qlen);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
	if (sc->re_caps & RE_C_HWCSUM)
		ifp->if_capabilities |= IFCAP_HWCSUM;

	ifp->if_capenable = ifp->if_capabilities;
	if (ifp->if_capabilities & IFCAP_HWCSUM) {
		/*
		 * RTL8168/8111C generates wrong IP checksummed frame if the
		 * packet has IP options so disable TX IP checksum offloading.
		 */ 
		if (sc->re_hwrev == RE_HWREV_8168CP ||
		    sc->re_hwrev == RE_HWREV_8168C)
			sc->re_hwassist = CSUM_TCP | CSUM_UDP;
		else
			sc->re_hwassist = CSUM_IP | CSUM_TCP | CSUM_UDP;
	}
	ifp->if_hwassist = sc->re_hwassist;

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->re_irq));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->re_npoll, ctx, (struct sysctl_oid *)tree,
	    device_get_unit(dev), ifp->if_serializer);
#endif

#ifdef RE_DIAG
	/*
	 * Perform hardware diagnostic on the original RTL8169.
	 * Some 32-bit cards were incorrectly wired and would
	 * malfunction if plugged into a 64-bit slot.
	 */
	if (sc->re_hwrev == RE_HWREV_8169) {
		lwkt_serialize_enter(ifp->if_serializer);
		error = re_diag(sc);
		lwkt_serialize_exit(ifp->if_serializer);

		if (error) {
			device_printf(dev, "hardware diagnostic failure\n");
			ether_ifdetach(ifp);
			goto fail;
		}
	}
#endif	/* RE_DIAG */

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->re_irq, INTR_MPSAFE, re_intr, sc,
			       &sc->re_intrhand, ifp->if_serializer);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		re_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
re_detach(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);
		re_stop(sc);
		bus_teardown_intr(dev, sc->re_irq, sc->re_intrhand);
		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}
	if (sc->re_miibus)
		device_delete_child(dev, sc->re_miibus);
	bus_generic_detach(dev);

	if (sc->re_irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->re_irq_rid,
				     sc->re_irq);

	if (sc->re_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->re_res) {
		bus_release_resource(dev, sc->re_res_type, sc->re_res_rid,
		    sc->re_res);
	}

	/* Free DMA stuffs */
	re_freemem(dev);

	return(0);
}

static void
re_setup_rxdesc(struct re_softc *sc, int idx)
{
	bus_addr_t paddr;
	uint32_t cmdstat;
	struct re_desc *d;

	paddr = sc->re_ldata.re_rx_paddr[idx];
	d = &sc->re_ldata.re_rx_list[idx];

	d->re_bufaddr_lo = htole32(RE_ADDR_LO(paddr));
	d->re_bufaddr_hi = htole32(RE_ADDR_HI(paddr));

	cmdstat = sc->re_rxbuf_size | RE_RDESC_CMD_OWN;
	if (idx == (sc->re_rx_desc_cnt - 1))
		cmdstat |= RE_RDESC_CMD_EOR;
	d->re_cmdstat = htole32(cmdstat);
}

static int
re_newbuf_std(struct re_softc *sc, int idx, int init)
{
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	struct mbuf *m;
	int error, nsegs;

	m = m_getcl(init ? MB_WAIT : MB_DONTWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		error = ENOBUFS;

		if (init) {
			if_printf(&sc->arpcom.ac_if, "m_getcl failed\n");
			return error;
		} else {
			goto back;
		}
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * NOTE:
	 * re(4) chips need address of the receive buffer to be 8-byte
	 * aligned, so don't call m_adj(m, ETHER_ALIGN) here.
	 */

	error = bus_dmamap_load_mbuf_segment(sc->re_ldata.re_rx_mtag,
			sc->re_ldata.re_rx_spare, m,
			&seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		if (init) {
			if_printf(&sc->arpcom.ac_if, "can't load RX mbuf\n");
			return error;
		} else {
			goto back;
		}
	}

	if (!init) {
		bus_dmamap_sync(sc->re_ldata.re_rx_mtag,
				sc->re_ldata.re_rx_dmamap[idx],
				BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->re_ldata.re_rx_mtag,
				  sc->re_ldata.re_rx_dmamap[idx]);
	}
	sc->re_ldata.re_rx_mbuf[idx] = m;
	sc->re_ldata.re_rx_paddr[idx] = seg.ds_addr;

	map = sc->re_ldata.re_rx_dmamap[idx];
	sc->re_ldata.re_rx_dmamap[idx] = sc->re_ldata.re_rx_spare;
	sc->re_ldata.re_rx_spare = map;
back:
	re_setup_rxdesc(sc, idx);
	return error;
}

static int
re_newbuf_jumbo(struct re_softc *sc, int idx, int init)
{
	struct mbuf *m;
	struct re_jbuf *jbuf;
	int error = 0;

	MGETHDR(m, init ? MB_WAIT : MB_DONTWAIT, MT_DATA);
	if (m == NULL) {
		error = ENOBUFS;
		if (init) {
			if_printf(&sc->arpcom.ac_if, "MGETHDR failed\n");
			return error;
		} else {
			goto back;
		}
	}

	jbuf = re_jbuf_alloc(sc);
	if (jbuf == NULL) {
		m_freem(m);

		error = ENOBUFS;
		if (init) {
			if_printf(&sc->arpcom.ac_if, "jpool is empty\n");
			return error;
		} else {
			goto back;
		}
	}

	m->m_ext.ext_arg = jbuf;
	m->m_ext.ext_buf = jbuf->re_buf;
	m->m_ext.ext_free = re_jbuf_free;
	m->m_ext.ext_ref = re_jbuf_ref;
	m->m_ext.ext_size = sc->re_rxbuf_size;

	m->m_data = m->m_ext.ext_buf;
	m->m_flags |= M_EXT;
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	/*
	 * NOTE:
	 * Some re(4) chips(e.g. RTL8101E) need address of the receive buffer
	 * to be 8-byte aligned, so don't call m_adj(m, ETHER_ALIGN) here.
	 */

	sc->re_ldata.re_rx_mbuf[idx] = m;
	sc->re_ldata.re_rx_paddr[idx] = jbuf->re_paddr;
back:
	re_setup_rxdesc(sc, idx);
	return error;
}

static int
re_tx_list_init(struct re_softc *sc)
{
	bzero(sc->re_ldata.re_tx_list, RE_TX_LIST_SZ(sc));

	sc->re_ldata.re_tx_prodidx = 0;
	sc->re_ldata.re_tx_considx = 0;
	sc->re_ldata.re_tx_free = sc->re_tx_desc_cnt;

	return(0);
}

static int
re_rx_list_init(struct re_softc *sc)
{
	int i, error;

	bzero(sc->re_ldata.re_rx_list, RE_RX_LIST_SZ(sc));

	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		error = sc->re_newbuf(sc, i, 1);
		if (error)
			return(error);
	}

	sc->re_ldata.re_rx_prodidx = 0;
	sc->re_head = sc->re_tail = NULL;

	return(0);
}

#define RE_IP4_PACKET	0x1
#define RE_TCP_PACKET	0x2
#define RE_UDP_PACKET	0x4

static __inline uint8_t
re_packet_type(struct re_softc *sc, uint32_t rxstat, uint32_t rxctrl)
{
	uint8_t packet_type = 0;

	if (sc->re_caps & RE_C_MAC2) {
		if (rxctrl & RE_RDESC_CTL_PROTOIP4)
			packet_type |= RE_IP4_PACKET;
	} else {
		if (rxstat & RE_RDESC_STAT_PROTOID)
			packet_type |= RE_IP4_PACKET;
	}
	if (RE_TCPPKT(rxstat))
		packet_type |= RE_TCP_PACKET;
	else if (RE_UDPPKT(rxstat))
		packet_type |= RE_UDP_PACKET;
	return packet_type;
}

/*
 * RX handler for C+ and 8169. For the gigE chips, we support
 * the reception of jumbo frames that have been fragmented
 * across multiple 2K mbuf cluster buffers.
 */
static int
re_rxeof(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;
	struct re_desc 	*cur_rx;
	uint32_t rxstat, rxctrl;
	int i, total_len, rx = 0;

	for (i = sc->re_ldata.re_rx_prodidx;
	     RE_OWN(&sc->re_ldata.re_rx_list[i]) == 0; RE_RXDESC_INC(sc, i)) {
		cur_rx = &sc->re_ldata.re_rx_list[i];
		m = sc->re_ldata.re_rx_mbuf[i];
		total_len = RE_RXBYTES(cur_rx);
		rxstat = le32toh(cur_rx->re_cmdstat);
		rxctrl = le32toh(cur_rx->re_control);

		rx = 1;

#ifdef INVARIANTS
		if (sc->re_flags & RE_F_USE_JPOOL)
			KKASSERT(rxstat & RE_RDESC_STAT_EOF);
#endif

		if ((rxstat & RE_RDESC_STAT_EOF) == 0) {
			if (sc->re_flags & RE_F_DROP_RXFRAG) {
				re_setup_rxdesc(sc, i);
				continue;
			}

			if (sc->re_newbuf(sc, i, 0)) {
				/* Drop upcoming fragments */
				sc->re_flags |= RE_F_DROP_RXFRAG;
				continue;
			}

			m->m_len = MCLBYTES;
			if (sc->re_head == NULL) {
				sc->re_head = sc->re_tail = m;
			} else {
				sc->re_tail->m_next = m;
				sc->re_tail = m;
			}
			continue;
		} else if (sc->re_flags & RE_F_DROP_RXFRAG) {
			/*
			 * Last fragment of a multi-fragment packet.
			 *
			 * Since error already happened, this fragment
			 * must be dropped as well as the fragment chain.
			 */
			re_setup_rxdesc(sc, i);
			re_free_rxchain(sc);
			sc->re_flags &= ~RE_F_DROP_RXFRAG;
			continue;
		}

		/*
		 * NOTE: for the 8139C+, the frame length field
		 * is always 12 bits in size, but for the gigE chips,
		 * it is 13 bits (since the max RX frame length is 16K).
		 * Unfortunately, all 32 bits in the status word
		 * were already used, so to make room for the extra
		 * length bit, RealTek took out the 'frame alignment
		 * error' bit and shifted the other status bits
		 * over one slot. The OWN, EOR, FS and LS bits are
		 * still in the same places. We have already extracted
		 * the frame length and checked the OWN bit, so rather
		 * than using an alternate bit mapping, we shift the
		 * status bits one space to the right so we can evaluate
		 * them using the 8169 status as though it was in the
		 * same format as that of the 8139C+.
		 */
		if (!RE_IS_8139CP(sc))
			rxstat >>= 1;

		if (rxstat & RE_RDESC_STAT_RXERRSUM) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			/*
			 * If this is part of a multi-fragment packet,
			 * discard all the pieces.
			 */
			re_free_rxchain(sc);
			re_setup_rxdesc(sc, i);
			continue;
		}

		/*
		 * If allocating a replacement mbuf fails,
		 * reload the current one.
		 */

		if (sc->re_newbuf(sc, i, 0)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}

		if (sc->re_head != NULL) {
			m->m_len = total_len % MCLBYTES;
			/* 
			 * Special case: if there's 4 bytes or less
			 * in this buffer, the mbuf can be discarded:
			 * the last 4 bytes is the CRC, which we don't
			 * care about anyway.
			 */
			if (m->m_len <= ETHER_CRC_LEN) {
				sc->re_tail->m_len -=
				    (ETHER_CRC_LEN - m->m_len);
				m_freem(m);
			} else {
				m->m_len -= ETHER_CRC_LEN;
				sc->re_tail->m_next = m;
			}
			m = sc->re_head;
			sc->re_head = sc->re_tail = NULL;
			m->m_pkthdr.len = total_len - ETHER_CRC_LEN;
		} else {
			m->m_pkthdr.len = m->m_len =
			    (total_len - ETHER_CRC_LEN);
		}

		IFNET_STAT_INC(ifp, ipackets, 1);
		m->m_pkthdr.rcvif = ifp;

		/* Do RX checksumming if enabled */

		if (ifp->if_capenable & IFCAP_RXCSUM) {
			uint8_t packet_type;

			packet_type = re_packet_type(sc, rxstat, rxctrl);

			/* Check IP header checksum */
			if (packet_type & RE_IP4_PACKET) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((rxstat & RE_RDESC_STAT_IPSUMBAD) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}

			/* Check TCP/UDP checksum */
			if (((packet_type & RE_TCP_PACKET) &&
			     (rxstat & RE_RDESC_STAT_TCPSUMBAD) == 0) ||
			    ((packet_type & RE_UDP_PACKET) &&
			     (rxstat & RE_RDESC_STAT_UDPSUMBAD) == 0)) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID|CSUM_PSEUDO_HDR|
				    CSUM_FRAG_NOT_CHECKED;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		if (rxctrl & RE_RDESC_CTL_HASTAG) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag =
				be16toh((rxctrl & RE_RDESC_CTL_TAGDATA));
		}
		ifp->if_input(ifp, m, NULL, -1);
	}

	sc->re_ldata.re_rx_prodidx = i;

	return rx;
}

#undef RE_IP4_PACKET
#undef RE_TCP_PACKET
#undef RE_UDP_PACKET

static int
re_tx_collect(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t txstat;
	int idx, tx = 0;

	for (idx = sc->re_ldata.re_tx_considx;
	     sc->re_ldata.re_tx_free < sc->re_tx_desc_cnt;
	     RE_TXDESC_INC(sc, idx)) {
		txstat = le32toh(sc->re_ldata.re_tx_list[idx].re_cmdstat);
		if (txstat & RE_TDESC_CMD_OWN)
			break;

		tx = 1;

		sc->re_ldata.re_tx_list[idx].re_bufaddr_lo = 0;

		/*
		 * We only stash mbufs in the last descriptor
		 * in a fragment chain, which also happens to
		 * be the only place where the TX status bits
		 * are valid.
		 */
		if (txstat & RE_TDESC_CMD_EOF) {
			bus_dmamap_unload(sc->re_ldata.re_tx_mtag,
			    sc->re_ldata.re_tx_dmamap[idx]);
			m_freem(sc->re_ldata.re_tx_mbuf[idx]);
			sc->re_ldata.re_tx_mbuf[idx] = NULL;
			if (txstat & (RE_TDESC_STAT_EXCESSCOL|
			    RE_TDESC_STAT_COLCNT))
				IFNET_STAT_INC(ifp, collisions, 1);
			if (txstat & RE_TDESC_STAT_TXERRSUM)
				IFNET_STAT_INC(ifp, oerrors, 1);
			else
				IFNET_STAT_INC(ifp, opackets, 1);
		}
		sc->re_ldata.re_tx_free++;
	}
	sc->re_ldata.re_tx_considx = idx;

	return tx;
}

static int
re_txeof(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int tx;

	tx = re_tx_collect(sc);

	/* There is enough free TX descs */
	if (sc->re_ldata.re_tx_free > RE_TXDESC_SPARE)
		ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Some chips will ignore a second TX request issued while an
	 * existing transmission is in progress. If the transmitter goes
	 * idle but there are still packets waiting to be sent, we need
	 * to restart the channel here to flush them out. This only seems
	 * to be required with the PCIe devices.
	 */
	if (sc->re_ldata.re_tx_free < sc->re_tx_desc_cnt)
		CSR_WRITE_1(sc, sc->re_txstart, RE_TXSTART_START);
	else
		ifp->if_timer = 0;

	return tx;
}

static void
re_tick(void *xsc)
{
	struct re_softc *sc = xsc;

	lwkt_serialize_enter(sc->arpcom.ac_if.if_serializer);
	re_tick_serialized(xsc);
	lwkt_serialize_exit(sc->arpcom.ac_if.if_serializer);
}

static void
re_tick_serialized(void *xsc)
{
	struct re_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->re_miibus);
	mii_tick(mii);
	if (sc->re_flags & RE_F_LINKED) {
		if (!(mii->mii_media_status & IFM_ACTIVE))
			sc->re_flags &= ~RE_F_LINKED;
	} else {
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->re_flags |= RE_F_LINKED;
			if (!ifq_is_empty(&ifp->if_snd))
				if_devstart(ifp);
		}
	}

	callout_reset(&sc->re_timer, hz, re_tick, sc);
}

#ifdef IFPOLL_ENABLE

static void
re_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (sc->re_npoll.ifpc_stcount-- == 0) {
		uint16_t       status;

		sc->re_npoll.ifpc_stcount = sc->re_npoll.ifpc_stfrac;

		status = CSR_READ_2(sc, RE_ISR);
		if (status == 0xffff)
			return;
		if (status)
			CSR_WRITE_2(sc, RE_ISR, status);

		/*
		 * XXX check behaviour on receiver stalls.
		 */

		if (status & RE_ISR_SYSTEM_ERR)
			re_init(sc);
	}

	sc->rxcycles = count;
	re_rxeof(sc);
	re_txeof(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static void
re_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->re_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = re_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING)
			re_setup_intr(sc, 0, RE_IMTYPE_NONE);
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			re_setup_intr(sc, 1, sc->re_imtype);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->re_irq));
	}
}
#endif /* IFPOLL_ENABLE */

static void
re_intr(void *arg)
{
	struct re_softc	*sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint16_t status;
	int rx, tx;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->re_flags & RE_F_SUSPENDED) ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	rx = tx = 0;

	status = CSR_READ_2(sc, RE_ISR);
	/* If the card has gone away the read returns 0xffff. */
	if (status == 0xffff)
		goto reload;
	if (status)
		CSR_WRITE_2(sc, RE_ISR, status);

	if ((status & sc->re_intrs) == 0)
		goto reload;

	if (status & (sc->re_rx_ack | RE_ISR_RX_ERR))
		rx |= re_rxeof(sc);

	if (status & (sc->re_tx_ack | RE_ISR_TX_ERR))
		tx |= re_txeof(sc);

	if (status & RE_ISR_SYSTEM_ERR)
		re_init(sc);

	if (status & RE_ISR_LINKCHG) {
		callout_stop(&sc->re_timer);
		re_tick_serialized(sc);
	}

reload:
	if (sc->re_imtype == RE_IMTYPE_SIM) {
		if ((sc->re_flags & RE_F_TIMER_INTR)) {
			if ((tx | rx) == 0) {
				/*
				 * Nothing needs to be processed, fallback
				 * to use TX/RX interrupts.
				 */
				re_setup_intr(sc, 1, RE_IMTYPE_NONE);

				/*
				 * Recollect, mainly to avoid the possible
				 * race introduced by changing interrupt
				 * masks.
				 */
				re_rxeof(sc);
				tx = re_txeof(sc);
			} else {
				CSR_WRITE_4(sc, RE_TIMERCNT, 1); /* reload */
			}
		} else if (tx | rx) {
			/*
			 * Assume that using simulated interrupt moderation
			 * (hardware timer based) could reduce the interript
			 * rate.
			 */
			re_setup_intr(sc, 1, RE_IMTYPE_SIM);
		}
	}

	if (tx && !ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static int
re_encap(struct re_softc *sc, struct mbuf **m_head, int *idx0)
{
	struct mbuf *m = *m_head;
	bus_dma_segment_t segs[RE_MAXSEGS];
	bus_dmamap_t map;
	int error, maxsegs, idx, i, nsegs;
	struct re_desc *d, *tx_ring;
	uint32_t cmd_csum, ctl_csum, vlantag;

	KASSERT(sc->re_ldata.re_tx_free > RE_TXDESC_SPARE,
		("not enough free TX desc"));

	map = sc->re_ldata.re_tx_dmamap[*idx0];

	/*
	 * Set up checksum offload. Note: checksum offload bits must
	 * appear in all descriptors of a multi-descriptor transmit
	 * attempt. (This is according to testing done with an 8169
	 * chip. I'm not sure if this is a requirement or a bug.)
	 */
	cmd_csum = ctl_csum = 0;
	if (m->m_pkthdr.csum_flags & CSUM_IP) {
		cmd_csum |= RE_TDESC_CMD_IPCSUM;
		ctl_csum |= RE_TDESC_CTL_IPCSUM;
	}
	if (m->m_pkthdr.csum_flags & CSUM_TCP) {
		cmd_csum |= RE_TDESC_CMD_TCPCSUM;
		ctl_csum |= RE_TDESC_CTL_TCPCSUM;
	}
	if (m->m_pkthdr.csum_flags & CSUM_UDP) {
		cmd_csum |= RE_TDESC_CMD_UDPCSUM;
		ctl_csum |= RE_TDESC_CTL_UDPCSUM;
	}

	/* For MAC2 chips, csum flags are set on re_control */
	if (sc->re_caps & RE_C_MAC2)
		cmd_csum = 0;
	else
		ctl_csum = 0;

	if ((sc->re_caps & RE_C_AUTOPAD) == 0) {
		/*
		 * With some of the RealTek chips, using the checksum offload
		 * support in conjunction with the autopadding feature results
		 * in the transmission of corrupt frames. For example, if we
		 * need to send a really small IP fragment that's less than 60
		 * bytes in size, and IP header checksumming is enabled, the
		 * resulting ethernet frame that appears on the wire will
		 * have garbled payload. To work around this, if TX checksum
		 * offload is enabled, we always manually pad short frames out
		 * to the minimum ethernet frame size.
		 *
		 * Note: this appears unnecessary for TCP, and doing it for TCP
		 * with PCIe adapters seems to result in bad checksums.
		 */
		if ((m->m_pkthdr.csum_flags &
		     (CSUM_DELAY_IP | CSUM_DELAY_DATA)) &&
		    (m->m_pkthdr.csum_flags & CSUM_TCP) == 0 &&
		    m->m_pkthdr.len < RE_MIN_FRAMELEN) {
			error = m_devpad(m, RE_MIN_FRAMELEN);
			if (error)
				goto back;
		}
	}

	vlantag = 0;
	if (m->m_flags & M_VLANTAG) {
		vlantag = htobe16(m->m_pkthdr.ether_vlantag) |
			  RE_TDESC_CTL_INSTAG;
	}

	maxsegs = sc->re_ldata.re_tx_free;
	if (maxsegs > RE_MAXSEGS)
		maxsegs = RE_MAXSEGS;

	error = bus_dmamap_load_mbuf_defrag(sc->re_ldata.re_tx_mtag, map,
			m_head, segs, maxsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error)
		goto back;

	m = *m_head;
	bus_dmamap_sync(sc->re_ldata.re_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	/*
	 * Map the segment array into descriptors.  We also keep track
	 * of the end of the ring and set the end-of-ring bits as needed,
	 * and we set the ownership bits in all except the very first
	 * descriptor, whose ownership bits will be turned on later.
	 */
	tx_ring = sc->re_ldata.re_tx_list;
	idx = *idx0;
	i = 0;
	for (;;) {
		uint32_t cmdstat;

		d = &tx_ring[idx];

		cmdstat = segs[i].ds_len;
		d->re_bufaddr_lo = htole32(RE_ADDR_LO(segs[i].ds_addr));
		d->re_bufaddr_hi = htole32(RE_ADDR_HI(segs[i].ds_addr));
		if (i == 0)
			cmdstat |= RE_TDESC_CMD_SOF;
		else
			cmdstat |= RE_TDESC_CMD_OWN;
		if (idx == (sc->re_tx_desc_cnt - 1))
			cmdstat |= RE_TDESC_CMD_EOR;
		d->re_cmdstat = htole32(cmdstat | cmd_csum);
		d->re_control = htole32(ctl_csum | vlantag);

		i++;
		if (i == nsegs)
			break;
		RE_TXDESC_INC(sc, idx);
	}
	d->re_cmdstat |= htole32(RE_TDESC_CMD_EOF);

	/* Transfer ownership of packet to the chip. */
	d->re_cmdstat |= htole32(RE_TDESC_CMD_OWN);
	if (*idx0 != idx)
		tx_ring[*idx0].re_cmdstat |= htole32(RE_TDESC_CMD_OWN);

	/*
	 * Insure that the map for this transmission
	 * is placed at the array index of the last descriptor
	 * in this chain.
	 */
	sc->re_ldata.re_tx_dmamap[*idx0] = sc->re_ldata.re_tx_dmamap[idx];
	sc->re_ldata.re_tx_dmamap[idx] = map;

	sc->re_ldata.re_tx_mbuf[idx] = m;
	sc->re_ldata.re_tx_free -= nsegs;

	RE_TXDESC_INC(sc, idx);
	*idx0 = idx;
back:
	if (error) {
		m_freem(*m_head);
		*m_head = NULL;
	}
	return error;
}

/*
 * Main transmit routine for C+ and gigE NICs.
 */

static void
re_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct re_softc	*sc = ifp->if_softc;
	struct mbuf *m_head;
	int idx, need_trans, oactive, error;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if ((sc->re_flags & RE_F_LINKED) == 0) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	idx = sc->re_ldata.re_tx_prodidx;

	need_trans = 0;
	oactive = 0;
	while (sc->re_ldata.re_tx_mbuf[idx] == NULL) {
		if (sc->re_ldata.re_tx_free <= RE_TXDESC_SPARE) {
			if (!oactive) {
				if (re_tx_collect(sc)) {
					oactive = 1;
					continue;
				}
			}
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		error = re_encap(sc, &m_head, &idx);
		if (error) {
			/* m_head is freed by re_encap(), if we reach here */
			IFNET_STAT_INC(ifp, oerrors, 1);

			if (error == EFBIG && !oactive) {
				if (re_tx_collect(sc)) {
					oactive = 1;
					continue;
				}
			}
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		oactive = 0;
		need_trans = 1;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	/*
	 * If sc->re_ldata.re_tx_mbuf[idx] is not NULL it is possible
	 * for OACTIVE to not be properly set when we also do not
	 * have sufficient free tx descriptors, leaving packet in
	 * ifp->if_snd.  This can cause if_start_dispatch() to loop
	 * infinitely so make sure OACTIVE is set properly.
	 */
	if (sc->re_ldata.re_tx_free <= RE_TXDESC_SPARE) {
		if (!ifq_is_oactive(&ifp->if_snd)) {
#if 0
			if_printf(ifp, "Debug: OACTIVE was not set when "
			    "re_tx_free was below minimum!\n");
#endif
			ifq_set_oactive(&ifp->if_snd);
		}
	}
	if (!need_trans)
		return;

	sc->re_ldata.re_tx_prodidx = idx;

	/*
	 * RealTek put the TX poll request register in a different
	 * location on the 8169 gigE chip. I don't know why.
	 */
	CSR_WRITE_1(sc, sc->re_txstart, RE_TXSTART_START);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
re_init(void *xsc)
{
	struct re_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	int error, framelen;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->re_miibus);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	re_stop(sc);

	if (sc->re_caps & RE_C_CONTIGRX) {
		if (ifp->if_mtu > ETHERMTU) {
			KKASSERT(sc->re_ldata.re_jbuf != NULL);
			sc->re_flags |= RE_F_USE_JPOOL;
			sc->re_rxbuf_size = RE_FRAMELEN_MAX;
			sc->re_newbuf = re_newbuf_jumbo;
		} else {
			sc->re_flags &= ~RE_F_USE_JPOOL;
			sc->re_rxbuf_size = MCLBYTES;
			sc->re_newbuf = re_newbuf_std;
		}
	}

	/*
	 * Adjust max read request size according to MTU; mainly to
	 * improve TX performance for common case (ETHERMTU) on GigE
	 * NICs.  However, this could _not_ be done on 10/100 only
	 * NICs; their DMA engines will malfunction using non-default
	 * max read request size.
	 */
	if ((sc->re_caps & (RE_C_PCIE | RE_C_FASTE)) == RE_C_PCIE) {
		if (ifp->if_mtu > ETHERMTU) {
			/*
			 * 512 seems to be the only value that works
			 * reliably with jumbo frame
			 */
			pcie_set_max_readrq(sc->re_dev,
				PCIEM_DEVCTL_MAX_READRQ_512);
		} else {
			pcie_set_max_readrq(sc->re_dev,
				PCIEM_DEVCTL_MAX_READRQ_4096);
		}
	}

	/*
	 * Enable C+ RX and TX mode, as well as VLAN stripping and
	 * RX checksum offload. We must configure the C+ register
	 * before all others.
	 */
	CSR_WRITE_2(sc, RE_CPLUS_CMD, RE_CPLUSCMD_RXENB | RE_CPLUSCMD_TXENB |
		    RE_CPLUSCMD_PCI_MRW |
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING ?
		     RE_CPLUSCMD_VLANSTRIP : 0) |
		    (ifp->if_capenable & IFCAP_RXCSUM ?
		     RE_CPLUSCMD_RXCSUM_ENB : 0));

	/*
	 * Init our MAC address.  Even though the chipset
	 * documentation doesn't mention it, we need to enter "Config
	 * register write enable" mode to modify the ID registers.
	 */
	CSR_WRITE_1(sc, RE_EECMD, RE_EEMODE_WRITECFG);
	CSR_WRITE_4(sc, RE_IDR0,
	    htole32(*(uint32_t *)(&sc->arpcom.ac_enaddr[0])));
	CSR_WRITE_2(sc, RE_IDR4,
	    htole16(*(uint16_t *)(&sc->arpcom.ac_enaddr[4])));
	CSR_WRITE_1(sc, RE_EECMD, RE_EEMODE_OFF);

	/*
	 * For C+ mode, initialize the RX descriptors and mbufs.
	 */
	error = re_rx_list_init(sc);
	if (error) {
		re_stop(sc);
		return;
	}
	error = re_tx_list_init(sc);
	if (error) {
		re_stop(sc);
		return;
	}

	/*
	 * Load the addresses of the RX and TX lists into the chip.
	 */
	CSR_WRITE_4(sc, RE_RXLIST_ADDR_HI,
	    RE_ADDR_HI(sc->re_ldata.re_rx_list_addr));
	CSR_WRITE_4(sc, RE_RXLIST_ADDR_LO,
	    RE_ADDR_LO(sc->re_ldata.re_rx_list_addr));

	CSR_WRITE_4(sc, RE_TXLIST_ADDR_HI,
	    RE_ADDR_HI(sc->re_ldata.re_tx_list_addr));
	CSR_WRITE_4(sc, RE_TXLIST_ADDR_LO,
	    RE_ADDR_LO(sc->re_ldata.re_tx_list_addr));

	/*
	 * Enable transmit and receive.
	 */
	CSR_WRITE_1(sc, RE_COMMAND, RE_CMD_TX_ENB|RE_CMD_RX_ENB);

	/*
	 * Set the initial TX and RX configuration.
	 */
	if (sc->re_flags & RE_F_TESTMODE) {
		if (!RE_IS_8139CP(sc))
			CSR_WRITE_4(sc, RE_TXCFG,
				    RE_TXCFG_CONFIG | RE_LOOPTEST_ON);
		else
			CSR_WRITE_4(sc, RE_TXCFG,
				    RE_TXCFG_CONFIG | RE_LOOPTEST_ON_CPLUS);
	} else
		CSR_WRITE_4(sc, RE_TXCFG, RE_TXCFG_CONFIG);

	framelen = RE_FRAMELEN(ifp->if_mtu);
	if (framelen < MCLBYTES)
		CSR_WRITE_1(sc, RE_EARLY_TX_THRESH, howmany(MCLBYTES, 128));
	else
		CSR_WRITE_1(sc, RE_EARLY_TX_THRESH, howmany(framelen, 128));

	CSR_WRITE_4(sc, RE_RXCFG, RE_RXCFG_CONFIG);

	/*
	 * Program the multicast filter, if necessary.
	 */
	re_setmulti(sc);

#ifdef IFPOLL_ENABLE
	/*
	 * Disable interrupts if we are polling.
	 */
	if (ifp->if_flags & IFF_NPOLLING)
		re_setup_intr(sc, 0, RE_IMTYPE_NONE);
	else	/* otherwise ... */
#endif /* IFPOLL_ENABLE */
	/*
	 * Enable interrupts.
	 */
	if (sc->re_flags & RE_F_TESTMODE)
		CSR_WRITE_2(sc, RE_IMR, 0);
	else
		re_setup_intr(sc, 1, sc->re_imtype);
	CSR_WRITE_2(sc, RE_ISR, sc->re_intrs);

	/* Start RX/TX process. */
	CSR_WRITE_4(sc, RE_MISSEDPKT, 0);

#ifdef notdef
	/* Enable receiver and transmitter. */
	CSR_WRITE_1(sc, RE_COMMAND, RE_CMD_TX_ENB|RE_CMD_RX_ENB);
#endif

	/*
	 * For 8169 gigE NICs, set the max allowed RX packet
	 * size so we can receive jumbo frames.
	 */
	if (!RE_IS_8139CP(sc)) {
		if (sc->re_caps & RE_C_CONTIGRX)
			CSR_WRITE_2(sc, RE_MAXRXPKTLEN, sc->re_rxbuf_size);
		else
			CSR_WRITE_2(sc, RE_MAXRXPKTLEN, 16383);
	}

	if (sc->re_flags & RE_F_TESTMODE)
		return;

	mii_mediachg(mii);

	CSR_WRITE_1(sc, RE_CFG1, RE_CFG1_DRVLOAD|RE_CFG1_FULLDUPLEX);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	callout_reset(&sc->re_timer, hz, re_tick, sc);
}

/*
 * Set media options.
 */
static int
re_ifmedia_upd(struct ifnet *ifp)
{
	struct re_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->re_miibus);
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void
re_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct re_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->re_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static int
re_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct re_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0, mask;

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch(command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > sc->re_maxmtu) {
			error = EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_flags & IFF_RUNNING)
				ifp->if_init(sc);
		}
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING) {
				if ((ifp->if_flags ^ sc->re_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					re_setmulti(sc);
			} else {
				re_init(sc);
			}
		} else if (ifp->if_flags & IFF_RUNNING) {
			re_stop(sc);
		}
		sc->re_if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		re_setmulti(sc);
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->re_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	case SIOCSIFCAP:
		mask = (ifr->ifr_reqcap ^ ifp->if_capenable) &
		       ifp->if_capabilities;
		ifp->if_capenable ^= mask;

		if (mask & IFCAP_HWCSUM) {
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist = sc->re_hwassist;
			else
				ifp->if_hwassist = 0;
		}
		if (mask && (ifp->if_flags & IFF_RUNNING))
			re_init(sc);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return(error);
}

static void
re_watchdog(struct ifnet *ifp)
{
	struct re_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if_printf(ifp, "watchdog timeout\n");

	IFNET_STAT_INC(ifp, oerrors, 1);

	re_txeof(sc);
	re_rxeof(sc);

	re_init(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
re_stop(struct re_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	/* Reset the adapter. */
	re_reset(sc, ifp->if_flags & IFF_RUNNING);

	ifp->if_timer = 0;
	callout_stop(&sc->re_timer);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	sc->re_flags &= ~(RE_F_TIMER_INTR | RE_F_DROP_RXFRAG | RE_F_LINKED);

	CSR_WRITE_1(sc, RE_COMMAND, 0x00);
	CSR_WRITE_2(sc, RE_IMR, 0x0000);
	CSR_WRITE_2(sc, RE_ISR, 0xFFFF);

	re_free_rxchain(sc);

	/* Free the TX list buffers. */
	for (i = 0; i < sc->re_tx_desc_cnt; i++) {
		if (sc->re_ldata.re_tx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->re_ldata.re_tx_mtag,
					  sc->re_ldata.re_tx_dmamap[i]);
			m_freem(sc->re_ldata.re_tx_mbuf[i]);
			sc->re_ldata.re_tx_mbuf[i] = NULL;
		}
	}

	/* Free the RX list buffers. */
	for (i = 0; i < sc->re_rx_desc_cnt; i++) {
		if (sc->re_ldata.re_rx_mbuf[i] != NULL) {
			if ((sc->re_flags & RE_F_USE_JPOOL) == 0) {
				bus_dmamap_unload(sc->re_ldata.re_rx_mtag,
						  sc->re_ldata.re_rx_dmamap[i]);
			}
			m_freem(sc->re_ldata.re_rx_mbuf[i]);
			sc->re_ldata.re_rx_mbuf[i] = NULL;
		}
	}
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
re_suspend(device_t dev)
{
#ifndef BURN_BRIDGES
	int i;
#endif
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);

	re_stop(sc);

#ifndef BURN_BRIDGES
	for (i = 0; i < 5; i++)
		sc->saved_maps[i] = pci_read_config(dev, PCIR_MAPS + i * 4, 4);
	sc->saved_biosaddr = pci_read_config(dev, PCIR_BIOS, 4);
	sc->saved_intline = pci_read_config(dev, PCIR_INTLINE, 1);
	sc->saved_cachelnsz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	sc->saved_lattimer = pci_read_config(dev, PCIR_LATTIMER, 1);
#endif

	sc->re_flags |= RE_F_SUSPENDED;

	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
re_resume(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;
#ifndef BURN_BRIDGES
	int i;
#endif

	lwkt_serialize_enter(ifp->if_serializer);

#ifndef BURN_BRIDGES
	/* better way to do this? */
	for (i = 0; i < 5; i++)
		pci_write_config(dev, PCIR_MAPS + i * 4, sc->saved_maps[i], 4);
	pci_write_config(dev, PCIR_BIOS, sc->saved_biosaddr, 4);
	pci_write_config(dev, PCIR_INTLINE, sc->saved_intline, 1);
	pci_write_config(dev, PCIR_CACHELNSZ, sc->saved_cachelnsz, 1);
	pci_write_config(dev, PCIR_LATTIMER, sc->saved_lattimer, 1);

	/* reenable busmastering */
	pci_enable_busmaster(dev);
	pci_enable_io(dev, SYS_RES_IOPORT);
#endif

	/* reinitialize interface if necessary */
	if (ifp->if_flags & IFF_UP)
		re_init(sc);

	sc->re_flags &= ~RE_F_SUSPENDED;

	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
re_shutdown(device_t dev)
{
	struct re_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	re_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
re_sysctl_rxtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;

	return re_sysctl_hwtime(oidp, arg1, arg2, req, &sc->re_rx_time);
}

static int
re_sysctl_txtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;

	return re_sysctl_hwtime(oidp, arg1, arg2, req, &sc->re_tx_time);
}

static int
re_sysctl_hwtime(SYSCTL_HANDLER_ARGS, int *hwtime)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = *hwtime;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (v != *hwtime) {
		*hwtime = v;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING && sc->re_imtype == RE_IMTYPE_HW)
			re_setup_hw_im(sc);
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
re_sysctl_simtime(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->re_sim_time;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (v != sc->re_sim_time) {
		sc->re_sim_time = v;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING && sc->re_imtype == RE_IMTYPE_SIM) {
#ifdef foo
			int reg;

			/*
			 * Following code causes various strange
			 * performance problems.  Hmm ...
			 */
			CSR_WRITE_2(sc, RE_IMR, 0);
			if (!RE_IS_8139CP(sc))
				reg = RE_TIMERINT_8169;
			else
				reg = RE_TIMERINT;
			CSR_WRITE_4(sc, reg, 0);
			CSR_READ_4(sc, reg); /* flush */

			CSR_WRITE_2(sc, RE_IMR, sc->re_intrs);
			re_setup_sim_im(sc);
#else
			re_setup_intr(sc, 0, RE_IMTYPE_NONE);
			DELAY(10);
			re_setup_intr(sc, 1, RE_IMTYPE_SIM);
#endif
		}
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static int
re_sysctl_imtype(SYSCTL_HANDLER_ARGS)
{
	struct re_softc *sc = arg1;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error, v;

	lwkt_serialize_enter(ifp->if_serializer);

	v = sc->re_imtype;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;

	if (v != RE_IMTYPE_HW && v != RE_IMTYPE_SIM && v != RE_IMTYPE_NONE) {
		error = EINVAL;
		goto back;
	}
	if (v == RE_IMTYPE_HW && (sc->re_caps & RE_C_HWIM) == 0) {
		/* Can't do hardware interrupt moderation */
		error = EOPNOTSUPP;
		goto back;
	}

	if (v != sc->re_imtype) {
		sc->re_imtype = v;
		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    IFF_RUNNING)
			re_setup_intr(sc, 1, sc->re_imtype);
	}
back:
	lwkt_serialize_exit(ifp->if_serializer);
	return error;
}

static void
re_setup_hw_im(struct re_softc *sc)
{
	KKASSERT(sc->re_caps & RE_C_HWIM);

	/*
	 * Interrupt moderation
	 *
	 * 0xABCD
	 * A - unknown (maybe TX related)
	 * B - TX timer (unit: 25us)
	 * C - unknown (maybe RX related)
	 * D - RX timer (unit: 25us)
	 *
	 *
	 * re(4)'s interrupt moderation is actually controlled by
	 * two variables, like most other NICs (bge, bce etc.)
	 * o  timer
	 * o  number of packets [P]
	 *
	 * The logic relationship between these two variables is
	 * similar to other NICs too:
	 * if (timer expire || packets > [P])
	 *     Interrupt is delivered
	 *
	 * Currently we only know how to set 'timer', but not
	 * 'number of packets', which should be ~30, as far as I
	 * tested (sink ~900Kpps, interrupt rate is 30KHz)
	 */
	CSR_WRITE_2(sc, RE_IM,
		    RE_IM_RXTIME(sc->re_rx_time) |
		    RE_IM_TXTIME(sc->re_tx_time) |
		    RE_IM_MAGIC);
}

static void
re_disable_hw_im(struct re_softc *sc)
{
	if (sc->re_caps & RE_C_HWIM)
		CSR_WRITE_2(sc, RE_IM, 0);
}

static void
re_setup_sim_im(struct re_softc *sc)
{
	if (!RE_IS_8139CP(sc)) {
		uint32_t ticks;

		/*
		 * Datasheet says tick decreases at bus speed,
		 * but it seems the clock runs a little bit
		 * faster, so we do some compensation here.
		 */
		ticks = (sc->re_sim_time * sc->re_bus_speed * 8) / 5;
		CSR_WRITE_4(sc, RE_TIMERINT_8169, ticks);
	} else {
		CSR_WRITE_4(sc, RE_TIMERINT, 0x400); /* XXX */
	}
	CSR_WRITE_4(sc, RE_TIMERCNT, 1); /* reload */
	sc->re_flags |= RE_F_TIMER_INTR;
}

static void
re_disable_sim_im(struct re_softc *sc)
{
	if (!RE_IS_8139CP(sc))
		CSR_WRITE_4(sc, RE_TIMERINT_8169, 0);
	else
		CSR_WRITE_4(sc, RE_TIMERINT, 0);
	sc->re_flags &= ~RE_F_TIMER_INTR;
}

static void
re_config_imtype(struct re_softc *sc, int imtype)
{
	switch (imtype) {
	case RE_IMTYPE_HW:
		KKASSERT(sc->re_caps & RE_C_HWIM);
		/* FALL THROUGH */
	case RE_IMTYPE_NONE:
		sc->re_intrs = RE_INTRS;
		sc->re_rx_ack = RE_ISR_RX_OK | RE_ISR_FIFO_OFLOW |
				RE_ISR_RX_OVERRUN;
		sc->re_tx_ack = RE_ISR_TX_OK;
		break;

	case RE_IMTYPE_SIM:
		sc->re_intrs = RE_INTRS_TIMER;
		sc->re_rx_ack = RE_ISR_TIMEOUT_EXPIRED;
		sc->re_tx_ack = RE_ISR_TIMEOUT_EXPIRED;
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->arpcom.ac_if.if_xname, imtype);
	}
}

static void
re_setup_intr(struct re_softc *sc, int enable_intrs, int imtype)
{
	re_config_imtype(sc, imtype);

	if (enable_intrs)
		CSR_WRITE_2(sc, RE_IMR, sc->re_intrs);
	else
		CSR_WRITE_2(sc, RE_IMR, 0); 

	sc->re_npoll.ifpc_stcount = 0;

	switch (imtype) {
	case RE_IMTYPE_NONE:
		re_disable_sim_im(sc);
		re_disable_hw_im(sc);
		break;

	case RE_IMTYPE_HW:
		KKASSERT(sc->re_caps & RE_C_HWIM);
		re_disable_sim_im(sc);
		re_setup_hw_im(sc);
		break;

	case RE_IMTYPE_SIM:
		re_disable_hw_im(sc);
		re_setup_sim_im(sc);
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->arpcom.ac_if.if_xname, imtype);
	}
}

static void
re_get_eaddr(struct re_softc *sc, uint8_t *eaddr)
{
	int i;

	if (sc->re_caps & RE_C_EE_EADDR) {
		uint16_t re_did;

		re_get_eewidth(sc);
		re_read_eeprom(sc, (caddr_t)&re_did, 0, 1);
		if (re_did == 0x8128) {
			uint16_t as[ETHER_ADDR_LEN / 2];

			/*
			 * Get station address from the EEPROM.
			 */
			re_read_eeprom(sc, (caddr_t)as, sc->re_ee_eaddr, 3);
			for (i = 0; i < ETHER_ADDR_LEN / 2; i++)
				as[i] = le16toh(as[i]);
			bcopy(as, eaddr, ETHER_ADDR_LEN);
			return;
		}
	}

	/*
	 * Get station address from IDRx.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		eaddr[i] = CSR_READ_1(sc, RE_IDR0 + i);
}

static int
re_jpool_alloc(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;
	struct re_jbuf *jbuf;
	bus_addr_t paddr;
	bus_size_t jpool_size;
	bus_dmamem_t dmem;
	caddr_t buf;
	int i, error;

	lwkt_serialize_init(&ldata->re_jbuf_serializer);

	ldata->re_jbuf = kmalloc(sizeof(struct re_jbuf) * RE_JBUF_COUNT(sc),
				 M_DEVBUF, M_WAITOK | M_ZERO);

	jpool_size = RE_JBUF_COUNT(sc) * RE_JBUF_SIZE;

	error = bus_dmamem_coherent(sc->re_parent_tag,
			RE_RXBUF_ALIGN, 0,
			BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
			jpool_size, BUS_DMA_WAITOK, &dmem);
	if (error) {
		device_printf(sc->re_dev, "could not allocate jumbo memory\n");
		return error;
	}
	ldata->re_jpool_tag = dmem.dmem_tag;
	ldata->re_jpool_map = dmem.dmem_map;
	ldata->re_jpool = dmem.dmem_addr;
	paddr = dmem.dmem_busaddr;

	/* ..and split it into 9KB chunks */
	SLIST_INIT(&ldata->re_jbuf_free);

	buf = ldata->re_jpool;
	for (i = 0; i < RE_JBUF_COUNT(sc); i++) {
		jbuf = &ldata->re_jbuf[i];

		jbuf->re_sc = sc;
		jbuf->re_inuse = 0;
		jbuf->re_slot = i;
		jbuf->re_buf = buf;
		jbuf->re_paddr = paddr;

		SLIST_INSERT_HEAD(&ldata->re_jbuf_free, jbuf, re_link);

		buf += RE_JBUF_SIZE;
		paddr += RE_JBUF_SIZE;
	}
	return 0;
}

static void
re_jpool_free(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;

	if (ldata->re_jpool_tag != NULL) {
		bus_dmamap_unload(ldata->re_jpool_tag, ldata->re_jpool_map);
		bus_dmamem_free(ldata->re_jpool_tag, ldata->re_jpool,
				ldata->re_jpool_map);
		bus_dma_tag_destroy(ldata->re_jpool_tag);
		ldata->re_jpool_tag = NULL;
	}

	if (ldata->re_jbuf != NULL) {
		kfree(ldata->re_jbuf, M_DEVBUF);
		ldata->re_jbuf = NULL;
	}
}

static struct re_jbuf *
re_jbuf_alloc(struct re_softc *sc)
{
	struct re_list_data *ldata = &sc->re_ldata;
	struct re_jbuf *jbuf;

	lwkt_serialize_enter(&ldata->re_jbuf_serializer);

	jbuf = SLIST_FIRST(&ldata->re_jbuf_free);
	if (jbuf != NULL) {
		SLIST_REMOVE_HEAD(&ldata->re_jbuf_free, re_link);
		jbuf->re_inuse = 1;
	}

	lwkt_serialize_exit(&ldata->re_jbuf_serializer);

	return jbuf;
}

static void
re_jbuf_free(void *arg)
{
	struct re_jbuf *jbuf = arg;
	struct re_softc *sc = jbuf->re_sc;
	struct re_list_data *ldata = &sc->re_ldata;

	if (&ldata->re_jbuf[jbuf->re_slot] != jbuf) {
		panic("%s: free wrong jumbo buffer",
		      sc->arpcom.ac_if.if_xname);
	} else if (jbuf->re_inuse == 0) {
		panic("%s: jumbo buffer already freed",
		      sc->arpcom.ac_if.if_xname);
	}

	lwkt_serialize_enter(&ldata->re_jbuf_serializer);
	atomic_subtract_int(&jbuf->re_inuse, 1);
	if (jbuf->re_inuse == 0)
		SLIST_INSERT_HEAD(&ldata->re_jbuf_free, jbuf, re_link);
	lwkt_serialize_exit(&ldata->re_jbuf_serializer);
}

static void
re_jbuf_ref(void *arg)
{
	struct re_jbuf *jbuf = arg;
	struct re_softc *sc = jbuf->re_sc;
	struct re_list_data *ldata = &sc->re_ldata;

	if (&ldata->re_jbuf[jbuf->re_slot] != jbuf) {
		panic("%s: ref wrong jumbo buffer",
		      sc->arpcom.ac_if.if_xname);
	} else if (jbuf->re_inuse == 0) {
		panic("%s: jumbo buffer already freed",
		      sc->arpcom.ac_if.if_xname);
	}
	atomic_add_int(&jbuf->re_inuse, 1);
}
