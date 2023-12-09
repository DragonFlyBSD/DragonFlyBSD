/*
 * Copyright (c) 2004
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
 * $FreeBSD: src/sys/dev/vge/if_vge.c,v 1.24 2006/02/14 12:44:56 glebius Exp $
 */

/*
 * VIA Networking Technologies VT612x PCI gigabit ethernet NIC driver.
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Networking Software Engineer
 * Wind River Systems
 */

/*
 * The VIA Networking VT6122 is a 32bit, 33/66Mhz PCI device that
 * combines a tri-speed ethernet MAC and PHY, with the following
 * features:
 *
 *	o Jumbo frame support up to 16K
 *	o Transmit and receive flow control
 *	o IPv4 checksum offload
 *	o VLAN tag insertion and stripping
 *	o TCP large send
 *	o 64-bit multicast hash table filter
 *	o 64 entry CAM filter
 *	o 16K RX FIFO and 48K TX FIFO memory
 *	o Interrupt moderation
 *
 * The VT6122 supports up to four transmit DMA queues. The descriptors
 * in the transmit ring can address up to 7 data fragments; frames which
 * span more than 7 data buffers must be coalesced, but in general the
 * BSD TCP/IP stack rarely generates frames more than 2 or 3 fragments
 * long. The receive descriptors address only a single buffer.
 *
 * There are two peculiar design issues with the VT6122. One is that
 * receive data buffers must be aligned on a 32-bit boundary. This is
 * not a problem where the VT6122 is used as a LOM device in x86-based
 * systems, but on architectures that generate unaligned access traps, we
 * have to do some copying.
 *
 * The other issue has to do with the way 64-bit addresses are handled.
 * The DMA descriptors only allow you to specify 48 bits of addressing
 * information. The remaining 16 bits are specified using one of the
 * I/O registers. If you only have a 32-bit system, then this isn't
 * an issue, but if you have a 64-bit system and more than 4GB of
 * memory, you must have to make sure your network data buffers reside
 * in the same 48-bit 'segment.'
 *
 * Special thanks to Ryan Fu at VIA Networking for providing documentation
 * and sample NICs for testing.
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/serialize.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/interrupt.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_poll.h>
#include <net/ifq_var.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <net/bpf.h>

#include <dev/netif/mii_layer/mii.h>
#include <dev/netif/mii_layer/miivar.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"

#include "miibus_if.h"

#include <dev/netif/vge/if_vgereg.h>
#include <dev/netif/vge/if_vgevar.h>

#define VGE_CSUM_FEATURES    (CSUM_IP | CSUM_TCP | CSUM_UDP)

/*
 * Various supported device vendors/types and their names.
 */
static const struct vge_type vge_devs[] = {
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT612X,
	  "VIA Networking Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static int vge_probe		(device_t);
static int vge_attach		(device_t);
static int vge_detach		(device_t);

static int vge_encap		(struct vge_softc *, struct mbuf *, int);

static void vge_dma_map_addr	(void *, bus_dma_segment_t *, int, int);
static void vge_dma_map_rx_desc	(void *, bus_dma_segment_t *, int,
				    bus_size_t, int);
static void vge_dma_map_tx_desc	(void *, bus_dma_segment_t *, int,
				    bus_size_t, int);
static int vge_dma_alloc	(device_t);
static void vge_dma_free	(struct vge_softc *);
static int vge_newbuf		(struct vge_softc *, int, struct mbuf *);
static int vge_rx_list_init	(struct vge_softc *);
static int vge_tx_list_init	(struct vge_softc *);
#ifdef VGE_FIXUP_RX
static __inline void vge_fixup_rx
				(struct mbuf *);
#endif
static void vge_rxeof		(struct vge_softc *, int);
static void vge_txeof		(struct vge_softc *);
static void vge_intr		(void *);
static void vge_tick		(struct vge_softc *);
static void vge_start		(struct ifnet *, struct ifaltq_subque *);
static int vge_ioctl		(struct ifnet *, u_long, caddr_t,
				 struct ucred *);
static void vge_init		(void *);
static void vge_stop		(struct vge_softc *);
static void vge_watchdog	(struct ifnet *);
static int vge_suspend		(device_t);
static int vge_resume		(device_t);
static void vge_shutdown	(device_t);
static int vge_ifmedia_upd	(struct ifnet *);
static void vge_ifmedia_sts	(struct ifnet *, struct ifmediareq *);

#ifdef VGE_EEPROM
static void vge_eeprom_getword	(struct vge_softc *, int, u_int16_t *);
#endif
static void vge_read_eeprom	(struct vge_softc *, uint8_t *, int, int, int);

static void vge_miipoll_start	(struct vge_softc *);
static void vge_miipoll_stop	(struct vge_softc *);
static int vge_miibus_readreg	(device_t, int, int);
static int vge_miibus_writereg	(device_t, int, int, int);
static void vge_miibus_statchg	(device_t);

static void vge_cam_clear	(struct vge_softc *);
static int vge_cam_set		(struct vge_softc *, uint8_t *);
static void vge_setmulti	(struct vge_softc *);
static void vge_reset		(struct vge_softc *);

#ifdef IFPOLL_ENABLE
static void	vge_npoll(struct ifnet *, struct ifpoll_info *);
static void	vge_npoll_compat(struct ifnet *, void *, int);
static void	vge_disable_intr(struct vge_softc *);
#endif
static void	vge_enable_intr(struct vge_softc *, uint32_t);

#define VGE_PCI_LOIO             0x10
#define VGE_PCI_LOMEM            0x14

static device_method_t vge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vge_probe),
	DEVMETHOD(device_attach,	vge_attach),
	DEVMETHOD(device_detach,	vge_detach),
	DEVMETHOD(device_suspend,	vge_suspend),
	DEVMETHOD(device_resume,	vge_resume),
	DEVMETHOD(device_shutdown,	vge_shutdown),

	/* bus interface */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	vge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	vge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	vge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t vge_driver = {
	"vge",
	vge_methods,
	sizeof(struct vge_softc)
};

static devclass_t vge_devclass;

DECLARE_DUMMY_MODULE(if_vge);
MODULE_DEPEND(if_vge, miibus, 1, 1, 1);
DRIVER_MODULE(if_vge, pci, vge_driver, vge_devclass, NULL, NULL);
DRIVER_MODULE(if_vge, cardbus, vge_driver, vge_devclass, NULL, NULL);
DRIVER_MODULE(miibus, vge, miibus_driver, miibus_devclass, NULL, NULL);

#ifdef VGE_EEPROM
/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
vge_eeprom_getword(struct vge_softc *sc, int addr, uint16_t dest)
{
	uint16_t word = 0;
	int i;

	/*
	 * Enter EEPROM embedded programming mode. In order to
	 * access the EEPROM at all, we first have to set the
	 * EELOAD bit in the CHIPCFG2 register.
	 */
	CSR_SETBIT_1(sc, VGE_CHIPCFG2, VGE_CHIPCFG2_EELOAD);
	CSR_SETBIT_1(sc, VGE_EECSR, VGE_EECSR_EMBP/*|VGE_EECSR_ECS*/);

	/* Select the address of the word we want to read */
	CSR_WRITE_1(sc, VGE_EEADDR, addr);

	/* Issue read command */
	CSR_SETBIT_1(sc, VGE_EECMD, VGE_EECMD_ERD);

	/* Wait for the done bit to be set. */
	for (i = 0; i < VGE_TIMEOUT; i++) {
		if (CSR_READ_1(sc, VGE_EECMD) & VGE_EECMD_EDONE)
			break;
	}
	if (i == VGE_TIMEOUT) {
		device_printf(sc->vge_dev, "EEPROM read timed out\n");
		*dest = 0;
		return;
	}

	/* Read the result */
	word = CSR_READ_2(sc, VGE_EERDDAT);

	/* Turn off EEPROM access mode. */
	CSR_CLRBIT_1(sc, VGE_EECSR, VGE_EECSR_EMBP/*|VGE_EECSR_ECS*/);
	CSR_CLRBIT_1(sc, VGE_CHIPCFG2, VGE_CHIPCFG2_EELOAD);

	*dest = word;
}
#endif

/*
 * Read a sequence of words from the EEPROM.
 */
static void
vge_read_eeprom(struct vge_softc *sc, uint8_t *dest, int off, int cnt, int swap)
{
	int i;
#ifdef VGE_EEPROM
	uint16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		vge_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
#else
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		dest[i] = CSR_READ_1(sc, VGE_PAR0 + i);
#endif
}

static void
vge_miipoll_stop(struct vge_softc *sc)
{
	int i;

	CSR_WRITE_1(sc, VGE_MIICMD, 0);

	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if (CSR_READ_1(sc, VGE_MIISTS) & VGE_MIISTS_IIDL)
			break;
	}
	if (i == VGE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "failed to idle MII autopoll\n");
}

static void
vge_miipoll_start(struct vge_softc *sc)
{
	int i;

	/* First, make sure we're idle. */
	CSR_WRITE_1(sc, VGE_MIICMD, 0);
	CSR_WRITE_1(sc, VGE_MIIADDR, VGE_MIIADDR_SWMPL);

	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if (CSR_READ_1(sc, VGE_MIISTS) & VGE_MIISTS_IIDL)
			break;
	}
	if (i == VGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "failed to idle MII autopoll\n");
		return;
	}

	/* Now enable auto poll mode. */
	CSR_WRITE_1(sc, VGE_MIICMD, VGE_MIICMD_MAUTO);

	/* And make sure it started. */
	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VGE_MIISTS) & VGE_MIISTS_IIDL) == 0)
			break;
	}
	if (i == VGE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "failed to start MII autopoll\n");
}

static int
vge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct vge_softc *sc;
	int i;
	uint16_t rval = 0;

	sc = device_get_softc(dev);

	if (phy != (CSR_READ_1(sc, VGE_MIICFG) & 0x1F))
		return(0);

	vge_miipoll_stop(sc);

	/* Specify the register we want to read. */
	CSR_WRITE_1(sc, VGE_MIIADDR, reg);

	/* Issue read command. */
	CSR_SETBIT_1(sc, VGE_MIICMD, VGE_MIICMD_RCMD);

	/* Wait for the read command bit to self-clear. */
	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VGE_MIICMD) & VGE_MIICMD_RCMD) == 0)
			break;
	}
	if (i == VGE_TIMEOUT)
		if_printf(&sc->arpcom.ac_if, "MII read timed out\n");
	else
		rval = CSR_READ_2(sc, VGE_MIIDATA);

	vge_miipoll_start(sc);

	return (rval);
}

static int
vge_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct vge_softc *sc;
	int i, rval = 0;

	sc = device_get_softc(dev);

	if (phy != (CSR_READ_1(sc, VGE_MIICFG) & 0x1F))
		return(0);

	vge_miipoll_stop(sc);

	/* Specify the register we want to write. */
	CSR_WRITE_1(sc, VGE_MIIADDR, reg);

	/* Specify the data we want to write. */
	CSR_WRITE_2(sc, VGE_MIIDATA, data);

	/* Issue write command. */
	CSR_SETBIT_1(sc, VGE_MIICMD, VGE_MIICMD_WCMD);

	/* Wait for the write command bit to self-clear. */
	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VGE_MIICMD) & VGE_MIICMD_WCMD) == 0)
			break;
	}
	if (i == VGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "MII write timed out\n");
		rval = EIO;
	}

	vge_miipoll_start(sc);

	return (rval);
}

static void
vge_cam_clear(struct vge_softc *sc)
{
	int i;

	/*
	 * Turn off all the mask bits. This tells the chip
	 * that none of the entries in the CAM filter are valid.
	 * desired entries will be enabled as we fill the filter in.
	 */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_CAMMASK);
	CSR_WRITE_1(sc, VGE_CAMADDR, VGE_CAMADDR_ENABLE);
	for (i = 0; i < 8; i++)
		CSR_WRITE_1(sc, VGE_CAM0 + i, 0);

	/* Clear the VLAN filter too. */
	CSR_WRITE_1(sc, VGE_CAMADDR, VGE_CAMADDR_ENABLE|VGE_CAMADDR_AVSEL|0);
	for (i = 0; i < 8; i++)
		CSR_WRITE_1(sc, VGE_CAM0 + i, 0);

	CSR_WRITE_1(sc, VGE_CAMADDR, 0);
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_MAR);

	sc->vge_camidx = 0;
}

static int
vge_cam_set(struct vge_softc *sc, uint8_t *addr)
{
	int i, error = 0;

	if (sc->vge_camidx == VGE_CAM_MAXADDRS)
		return(ENOSPC);

	/* Select the CAM data page. */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_CAMDATA);

	/* Set the filter entry we want to update and enable writing. */
	CSR_WRITE_1(sc, VGE_CAMADDR, VGE_CAMADDR_ENABLE|sc->vge_camidx);

	/* Write the address to the CAM registers */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VGE_CAM0 + i, addr[i]);

	/* Issue a write command. */
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_WRITE);

	/* Wake for it to clear. */
	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VGE_CAMCTL) & VGE_CAMCTL_WRITE) == 0)
			break;
	}
	if (i == VGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "setting CAM filter failed\n");
		error = EIO;
		goto fail;
	}

	/* Select the CAM mask page. */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_CAMMASK);

	/* Set the mask bit that enables this filter. */
	CSR_SETBIT_1(sc, VGE_CAM0 + (sc->vge_camidx/8),
	    1<<(sc->vge_camidx & 7));

	sc->vge_camidx++;

fail:
	/* Turn off access to CAM. */
	CSR_WRITE_1(sc, VGE_CAMADDR, 0);
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_MAR);

	return (error);
}

/*
 * Program the multicast filter. We use the 64-entry CAM filter
 * for perfect filtering. If there's more than 64 multicast addresses,
 * we use the hash filter insted.
 */
static void
vge_setmulti(struct vge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int error = 0;
	struct ifmultiaddr *ifma;
	uint32_t h, hashes[2] = { 0, 0 };

	/* First, zot all the multicast entries. */
	vge_cam_clear(sc);
	CSR_WRITE_4(sc, VGE_MAR0, 0);
	CSR_WRITE_4(sc, VGE_MAR1, 0);

	/*
	 * If the user wants allmulti or promisc mode, enable reception
	 * of all multicast frames.
	 */
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		CSR_WRITE_4(sc, VGE_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, VGE_MAR1, 0xFFFFFFFF);
		return;
	}

	/* Now program new ones */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		error = vge_cam_set(sc,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		if (error)
			break;
	}

	/* If there were too many addresses, use the hash filter. */
	if (error) {
		vge_cam_clear(sc);

		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
		}

		CSR_WRITE_4(sc, VGE_MAR0, hashes[0]);
		CSR_WRITE_4(sc, VGE_MAR1, hashes[1]);
	}
}

static void
vge_reset(struct vge_softc *sc)
{
	int i;

	CSR_WRITE_1(sc, VGE_CRS1, VGE_CR1_SOFTRESET);

	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(5);
		if ((CSR_READ_1(sc, VGE_CRS1) & VGE_CR1_SOFTRESET) == 0)
			break;
	}

	if (i == VGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "soft reset timed out");
		CSR_WRITE_1(sc, VGE_CRS3, VGE_CR3_STOP_FORCE);
		DELAY(2000);
	}

	DELAY(5000);

	CSR_SETBIT_1(sc, VGE_EECSR, VGE_EECSR_RELOAD);

	for (i = 0; i < VGE_TIMEOUT; i++) {
		DELAY(5);
		if ((CSR_READ_1(sc, VGE_EECSR) & VGE_EECSR_RELOAD) == 0)
			break;
	}
	if (i == VGE_TIMEOUT) {
		if_printf(&sc->arpcom.ac_if, "EEPROM reload timed out\n");
		return;
	}

	CSR_CLRBIT_1(sc, VGE_CHIPCFG0, VGE_CHIPCFG0_PACPI);
}

/*
 * Probe for a VIA gigabit chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
vge_probe(device_t dev)
{
	const struct vge_type *t;
	uint16_t did, vid;

	did = pci_get_device(dev);
	vid = pci_get_vendor(dev);
	for (t = vge_devs; t->vge_name != NULL; ++t) {
		if (vid == t->vge_vid && did == t->vge_did) {
			device_set_desc(dev, t->vge_name);
			return 0;
		}
	}
	return (ENXIO);
}

static void
vge_dma_map_rx_desc(void *arg, bus_dma_segment_t *segs, int nseg,
		    bus_size_t mapsize, int error)
{

	struct vge_dmaload_arg *ctx;
	struct vge_rx_desc *d = NULL;

	if (error)
		return;

	ctx = arg;

	/* Signal error to caller if there's too many segments */
	if (nseg > ctx->vge_maxsegs) {
		ctx->vge_maxsegs = 0;
		return;
	}

	/*
	 * Map the segment array into descriptors.
	 */
	d = &ctx->sc->vge_ldata.vge_rx_list[ctx->vge_idx];

	/* If this descriptor is still owned by the chip, bail. */
	if (le32toh(d->vge_sts) & VGE_RDSTS_OWN) {
		if_printf(&ctx->sc->arpcom.ac_if,
			  "tried to map busy descriptor\n");
		ctx->vge_maxsegs = 0;
		return;
	}

	d->vge_buflen = htole16(VGE_BUFLEN(segs[0].ds_len) | VGE_RXDESC_I);
	d->vge_addrlo = htole32(VGE_ADDR_LO(segs[0].ds_addr));
	d->vge_addrhi = htole16(VGE_ADDR_HI(segs[0].ds_addr) & 0xFFFF);
	d->vge_sts = 0;
	d->vge_ctl = 0;

	ctx->vge_maxsegs = 1;
}

static void
vge_dma_map_tx_desc(void *arg, bus_dma_segment_t *segs, int nseg,
		    bus_size_t mapsize, int error)
{
	struct vge_dmaload_arg *ctx;
	struct vge_tx_desc *d = NULL;
	struct vge_tx_frag *f;
	int i = 0;

	if (error)
		return;

	ctx = arg;

	/* Signal error to caller if there's too many segments */
	if (nseg > ctx->vge_maxsegs) {
		ctx->vge_maxsegs = 0;
		return;
	}

	/* Map the segment array into descriptors. */
	d = &ctx->sc->vge_ldata.vge_tx_list[ctx->vge_idx];

	/* If this descriptor is still owned by the chip, bail. */
	if (le32toh(d->vge_sts) & VGE_TDSTS_OWN) {
		ctx->vge_maxsegs = 0;
		return;
	}

	for (i = 0; i < nseg; i++) {
		f = &d->vge_frag[i];
		f->vge_buflen = htole16(VGE_BUFLEN(segs[i].ds_len));
		f->vge_addrlo = htole32(VGE_ADDR_LO(segs[i].ds_addr));
		f->vge_addrhi = htole16(VGE_ADDR_HI(segs[i].ds_addr) & 0xFFFF);
	}

	/* Argh. This chip does not autopad short frames */
	if (ctx->vge_m0->m_pkthdr.len < VGE_MIN_FRAMELEN) {
		f = &d->vge_frag[i];
		f->vge_buflen = htole16(VGE_BUFLEN(VGE_MIN_FRAMELEN -
		    ctx->vge_m0->m_pkthdr.len));
		f->vge_addrlo = htole32(VGE_ADDR_LO(segs[0].ds_addr));
		f->vge_addrhi = htole16(VGE_ADDR_HI(segs[0].ds_addr) & 0xFFFF);
		ctx->vge_m0->m_pkthdr.len = VGE_MIN_FRAMELEN;
		i++;
	}

	/*
	 * When telling the chip how many segments there are, we
	 * must use nsegs + 1 instead of just nsegs. Darned if I
	 * know why.
	 */
	i++;

	d->vge_sts = ctx->vge_m0->m_pkthdr.len << 16;
	d->vge_ctl = ctx->vge_flags|(i << 28)|VGE_TD_LS_NORM;

	if (ctx->vge_m0->m_pkthdr.len > ETHERMTU + ETHER_HDR_LEN)
		d->vge_ctl |= VGE_TDCTL_JUMBO;

	ctx->vge_maxsegs = nseg;
}

/*
 * Map a single buffer address.
 */

static void
vge_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	*((bus_addr_t *)arg) = segs->ds_addr;
}

static int
vge_dma_alloc(device_t dev)
{
	struct vge_softc *sc = device_get_softc(dev);
	int error, nseg, i, tx_pos = 0, rx_pos = 0;

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
#define VGE_NSEG_NEW 32
	error = bus_dma_tag_create(NULL,	/* parent */
			1, 0,			/* alignment, boundary */
			BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			MAXBSIZE, VGE_NSEG_NEW,	/* maxsize, nsegments */
			BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
			BUS_DMA_ALLOCNOW,	/* flags */
			&sc->vge_parent_tag);
	if (error) {
		device_printf(dev, "can't create parent dma tag\n");
		return error;
	}

	/*
	 * Allocate map for RX mbufs.
	 */
	nseg = 32;
	error = bus_dma_tag_create(sc->vge_parent_tag, ETHER_ALIGN, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   MCLBYTES * nseg, nseg, MCLBYTES,
				   BUS_DMA_ALLOCNOW, &sc->vge_ldata.vge_mtag);
	if (error) {
		device_printf(dev, "could not allocate mbuf dma tag\n");
		return error;
	}

	/*
	 * Allocate map for TX descriptor list.
	 */
	error = bus_dma_tag_create(sc->vge_parent_tag, VGE_RING_ALIGN, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   VGE_TX_LIST_SZ, 1, VGE_TX_LIST_SZ,
				   BUS_DMA_ALLOCNOW,
				   &sc->vge_ldata.vge_tx_list_tag);
	if (error) {
		device_printf(dev, "could not allocate tx list dma tag\n");
		return error;
	}

	/* Allocate DMA'able memory for the TX ring */
	error = bus_dmamem_alloc(sc->vge_ldata.vge_tx_list_tag,
				 (void **)&sc->vge_ldata.vge_tx_list,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &sc->vge_ldata.vge_tx_list_map);
	if (error) {
		device_printf(dev, "could not allocate tx list dma memory\n");
		return error;
	}

	/* Load the map for the TX ring. */
	error = bus_dmamap_load(sc->vge_ldata.vge_tx_list_tag,
				sc->vge_ldata.vge_tx_list_map,
				sc->vge_ldata.vge_tx_list, VGE_TX_LIST_SZ,
				vge_dma_map_addr,
				&sc->vge_ldata.vge_tx_list_addr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(dev, "could not load tx list\n");
		bus_dmamem_free(sc->vge_ldata.vge_tx_list_tag, 
				sc->vge_ldata.vge_tx_list,
				sc->vge_ldata.vge_tx_list_map);
		sc->vge_ldata.vge_tx_list = NULL;
		return error;
	}

	/* Create DMA maps for TX buffers */
	for (i = 0; i < VGE_TX_DESC_CNT; i++) {
		error = bus_dmamap_create(sc->vge_ldata.vge_mtag, 0,
					  &sc->vge_ldata.vge_tx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for TX\n");
			tx_pos = i;
			goto map_fail;
		}
	}
	tx_pos = VGE_TX_DESC_CNT;

	/*
	 * Allocate map for RX descriptor list.
	 */
	error = bus_dma_tag_create(sc->vge_parent_tag, VGE_RING_ALIGN, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   VGE_TX_LIST_SZ, 1, VGE_TX_LIST_SZ,
				   BUS_DMA_ALLOCNOW,
				   &sc->vge_ldata.vge_rx_list_tag);
	if (error) {
		device_printf(dev, "could not allocate rx list dma tag\n");
		return error;
	}

	/* Allocate DMA'able memory for the RX ring */
	error = bus_dmamem_alloc(sc->vge_ldata.vge_rx_list_tag,
				 (void **)&sc->vge_ldata.vge_rx_list,
				 BUS_DMA_WAITOK | BUS_DMA_ZERO,
				 &sc->vge_ldata.vge_rx_list_map);
	if (error) {
		device_printf(dev, "could not allocate rx list dma memory\n");
		return error;
	}

	/* Load the map for the RX ring. */
	error = bus_dmamap_load(sc->vge_ldata.vge_rx_list_tag,
				sc->vge_ldata.vge_rx_list_map,
				sc->vge_ldata.vge_rx_list, VGE_TX_LIST_SZ,
				vge_dma_map_addr,
				&sc->vge_ldata.vge_rx_list_addr,
				BUS_DMA_WAITOK);
	if (error) {
		device_printf(dev, "could not load rx list\n");
		bus_dmamem_free(sc->vge_ldata.vge_rx_list_tag,
				sc->vge_ldata.vge_rx_list,
				sc->vge_ldata.vge_rx_list_map);
		sc->vge_ldata.vge_rx_list = NULL;
		return error;
	}

	/* Create DMA maps for RX buffers */
	for (i = 0; i < VGE_RX_DESC_CNT; i++) {
		error = bus_dmamap_create(sc->vge_ldata.vge_mtag, 0,
					  &sc->vge_ldata.vge_rx_dmamap[i]);
		if (error) {
			device_printf(dev, "can't create DMA map for RX\n");
			rx_pos = i;
			goto map_fail;
		}
	}
	return (0);

map_fail:
	for (i = 0; i < tx_pos; ++i) {
		error = bus_dmamap_destroy(sc->vge_ldata.vge_mtag,
					   sc->vge_ldata.vge_tx_dmamap[i]);
	}
	for (i = 0; i < rx_pos; ++i) {
		error = bus_dmamap_destroy(sc->vge_ldata.vge_mtag,
					   sc->vge_ldata.vge_rx_dmamap[i]);
	}
	bus_dma_tag_destroy(sc->vge_ldata.vge_mtag);
	sc->vge_ldata.vge_mtag = NULL;

	return error;
}

static void
vge_dma_free(struct vge_softc *sc)
{
	/* Unload and free the RX DMA ring memory and map */
	if (sc->vge_ldata.vge_rx_list_tag) {
		bus_dmamap_unload(sc->vge_ldata.vge_rx_list_tag,
				  sc->vge_ldata.vge_rx_list_map);
		bus_dmamem_free(sc->vge_ldata.vge_rx_list_tag,
				sc->vge_ldata.vge_rx_list,
				sc->vge_ldata.vge_rx_list_map);
	}

	if (sc->vge_ldata.vge_rx_list_tag)
		bus_dma_tag_destroy(sc->vge_ldata.vge_rx_list_tag);

	/* Unload and free the TX DMA ring memory and map */
	if (sc->vge_ldata.vge_tx_list_tag) {
		bus_dmamap_unload(sc->vge_ldata.vge_tx_list_tag,
				  sc->vge_ldata.vge_tx_list_map);
		bus_dmamem_free(sc->vge_ldata.vge_tx_list_tag,
				sc->vge_ldata.vge_tx_list,
				sc->vge_ldata.vge_tx_list_map);
	}

	if (sc->vge_ldata.vge_tx_list_tag)
		bus_dma_tag_destroy(sc->vge_ldata.vge_tx_list_tag);

	/* Destroy all the RX and TX buffer maps */
	if (sc->vge_ldata.vge_mtag) {
		int i;

		for (i = 0; i < VGE_TX_DESC_CNT; i++) {
			bus_dmamap_destroy(sc->vge_ldata.vge_mtag,
					   sc->vge_ldata.vge_tx_dmamap[i]);
		}
		for (i = 0; i < VGE_RX_DESC_CNT; i++) {
			bus_dmamap_destroy(sc->vge_ldata.vge_mtag,
					   sc->vge_ldata.vge_rx_dmamap[i]);
		}
		bus_dma_tag_destroy(sc->vge_ldata.vge_mtag);
	}

	if (sc->vge_parent_tag)
		bus_dma_tag_destroy(sc->vge_parent_tag);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
vge_attach(device_t dev)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	struct vge_softc *sc;
	struct ifnet *ifp;
	int error = 0;

	sc = device_get_softc(dev);
	ifp = &sc->arpcom.ac_if;

	/* Initialize if_xname early, so if_printf() can be used */
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	sc->vge_res_rid = VGE_PCI_LOMEM;
	sc->vge_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					     &sc->vge_res_rid, RF_ACTIVE);
	if (sc->vge_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		return ENXIO;
	}

	sc->vge_btag = rman_get_bustag(sc->vge_res);
	sc->vge_bhandle = rman_get_bushandle(sc->vge_res);

	/* Allocate interrupt */
	sc->vge_irq_rid = 0;
	sc->vge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->vge_irq_rid,
					     RF_SHAREABLE | RF_ACTIVE);
	if (sc->vge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	vge_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	vge_read_eeprom(sc, eaddr, VGE_EE_EADDR, 3, 0);

	/* Allocate DMA related stuffs */
	error = vge_dma_alloc(dev);
	if (error)
		goto fail;

	/* Do MII setup */
	error = mii_phy_probe(dev, &sc->vge_miibus, vge_ifmedia_upd,
			      vge_ifmedia_sts);
	if (error) {
		device_printf(dev, "MII without any phy!\n");
		goto fail;
	}

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = vge_init;
	ifp->if_start = vge_start;
	ifp->if_watchdog = vge_watchdog;
	ifp->if_ioctl = vge_ioctl;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = vge_npoll;
#endif
	ifp->if_hwassist = VGE_CSUM_FEATURES;
	ifp->if_capabilities = IFCAP_VLAN_MTU |
			       IFCAP_HWCSUM |
			       IFCAP_VLAN_HWTAGGING;
	ifp->if_capenable = ifp->if_capabilities;
	ifq_set_maxlen(&ifp->if_snd, VGE_IFQ_MAXLEN);
	ifq_set_ready(&ifp->if_snd);

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr, NULL);

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->vge_irq));

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&sc->vge_npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->vge_irq, INTR_MPSAFE, vge_intr, sc,
			       &sc->vge_intrhand, ifp->if_serializer);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	return 0;
fail:
	vge_detach(dev);
	return error;
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
vge_detach(device_t dev)
{
	struct vge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		lwkt_serialize_enter(ifp->if_serializer);

		vge_stop(sc);
		bus_teardown_intr(dev, sc->vge_irq, sc->vge_intrhand);
		/*
		 * Force off the IFF_UP flag here, in case someone
		 * still had a BPF descriptor attached to this
		 * interface. If they do, ether_ifattach() will cause
		 * the BPF code to try and clear the promisc mode
		 * flag, which will bubble down to vge_ioctl(),
		 * which will try to call vge_init() again. This will
		 * turn the NIC back on and restart the MII ticker,
		 * which will panic the system when the kernel tries
		 * to invoke the vge_tick() function that isn't there
		 * anymore.
		 */
		ifp->if_flags &= ~IFF_UP;

		lwkt_serialize_exit(ifp->if_serializer);

		ether_ifdetach(ifp);
	}

	if (sc->vge_miibus)
		device_delete_child(dev, sc->vge_miibus);
	bus_generic_detach(dev);

	if (sc->vge_irq) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->vge_irq_rid,
				     sc->vge_irq);
	}

	if (sc->vge_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->vge_res_rid,
				     sc->vge_res);
	}

	vge_dma_free(sc);
	return (0);
}

static int
vge_newbuf(struct vge_softc *sc, int idx, struct mbuf *m)
{
	struct vge_dmaload_arg arg;
	struct mbuf *n = NULL;
	int i, error;

	if (m == NULL) {
		n = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (n == NULL)
			return (ENOBUFS);
		m = n;
	} else {
		m->m_data = m->m_ext.ext_buf;
	}


#ifdef VGE_FIXUP_RX
	/*
	 * This is part of an evil trick to deal with non-x86 platforms.
	 * The VIA chip requires RX buffers to be aligned on 32-bit
	 * boundaries, but that will hose non-x86 machines. To get around
	 * this, we leave some empty space at the start of each buffer
	 * and for non-x86 hosts, we copy the buffer back two bytes
	 * to achieve word alignment. This is slightly more efficient
	 * than allocating a new buffer, copying the contents, and
	 * discarding the old buffer.
	 */
	m->m_len = m->m_pkthdr.len = MCLBYTES - VGE_ETHER_ALIGN;
	m_adj(m, VGE_ETHER_ALIGN);
#else
	m->m_len = m->m_pkthdr.len = MCLBYTES;
#endif

	arg.sc = sc;
	arg.vge_idx = idx;
	arg.vge_maxsegs = 1;
	arg.vge_flags = 0;

	error = bus_dmamap_load_mbuf(sc->vge_ldata.vge_mtag,
				     sc->vge_ldata.vge_rx_dmamap[idx], m,
				     vge_dma_map_rx_desc, &arg, BUS_DMA_NOWAIT);
	if (error || arg.vge_maxsegs != 1) {
		if (n != NULL)
			m_freem(n);
		return (ENOMEM);
	}

	/*
	 * Note: the manual fails to document the fact that for
	 * proper opration, the driver needs to replentish the RX
	 * DMA ring 4 descriptors at a time (rather than one at a
	 * time, like most chips). We can allocate the new buffers
	 * but we should not set the OWN bits until we're ready
	 * to hand back 4 of them in one shot.
	 */

#define VGE_RXCHUNK 4
	sc->vge_rx_consumed++;
	if (sc->vge_rx_consumed == VGE_RXCHUNK) {
		for (i = idx; i != idx - sc->vge_rx_consumed; i--) {
			sc->vge_ldata.vge_rx_list[i].vge_sts |=
			    htole32(VGE_RDSTS_OWN);
		}
		sc->vge_rx_consumed = 0;
	}

	sc->vge_ldata.vge_rx_mbuf[idx] = m;

	bus_dmamap_sync(sc->vge_ldata.vge_mtag,
			sc->vge_ldata.vge_rx_dmamap[idx], BUS_DMASYNC_PREREAD);

	return (0);
}

static int
vge_tx_list_init(struct vge_softc *sc)
{
	bzero ((char *)sc->vge_ldata.vge_tx_list, VGE_TX_LIST_SZ);
	bzero ((char *)&sc->vge_ldata.vge_tx_mbuf,
	    (VGE_TX_DESC_CNT * sizeof(struct mbuf *)));

	bus_dmamap_sync(sc->vge_ldata.vge_tx_list_tag,
	    sc->vge_ldata.vge_tx_list_map, BUS_DMASYNC_PREWRITE);
	sc->vge_ldata.vge_tx_prodidx = 0;
	sc->vge_ldata.vge_tx_considx = 0;
	sc->vge_ldata.vge_tx_free = VGE_TX_DESC_CNT;

	return (0);
}

static int
vge_rx_list_init(struct vge_softc *sc)
{
	int i;

	bzero(sc->vge_ldata.vge_rx_list, VGE_RX_LIST_SZ);
	bzero(&sc->vge_ldata.vge_rx_mbuf,
	      VGE_RX_DESC_CNT * sizeof(struct mbuf *));

	sc->vge_rx_consumed = 0;

	for (i = 0; i < VGE_RX_DESC_CNT; i++) {
		if (vge_newbuf(sc, i, NULL) == ENOBUFS)
			return (ENOBUFS);
	}

	/* Flush the RX descriptors */
	bus_dmamap_sync(sc->vge_ldata.vge_rx_list_tag,
			sc->vge_ldata.vge_rx_list_map,
			BUS_DMASYNC_PREWRITE);

	sc->vge_ldata.vge_rx_prodidx = 0;
	sc->vge_rx_consumed = 0;
	sc->vge_head = sc->vge_tail = NULL;
	return (0);
}

#ifdef VGE_FIXUP_RX
static __inline void
vge_fixup_rx(struct mbuf *m)
{
	uint16_t *src, *dst;
	int i;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= ETHER_ALIGN;
}
#endif

/*
 * RX handler. We support the reception of jumbo frames that have
 * been fragmented across multiple 2K mbuf cluster buffers.
 */
static void
vge_rxeof(struct vge_softc *sc, int count)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf *m;
	int i, total_len, lim = 0;
	struct vge_rx_desc *cur_rx;
	uint32_t rxstat, rxctl;

	ASSERT_SERIALIZED(ifp->if_serializer);

	i = sc->vge_ldata.vge_rx_prodidx;

	/* Invalidate the descriptor memory */

	bus_dmamap_sync(sc->vge_ldata.vge_rx_list_tag,
			sc->vge_ldata.vge_rx_list_map, BUS_DMASYNC_POSTREAD);

	while (!VGE_OWN(&sc->vge_ldata.vge_rx_list[i])) {
#ifdef IFPOLL_ENABLE
		if (count >= 0 && count-- == 0)
			break;
#endif

		cur_rx = &sc->vge_ldata.vge_rx_list[i];
		m = sc->vge_ldata.vge_rx_mbuf[i];
		total_len = VGE_RXBYTES(cur_rx);
		rxstat = le32toh(cur_rx->vge_sts);
		rxctl = le32toh(cur_rx->vge_ctl);

		/* Invalidate the RX mbuf and unload its map */
		bus_dmamap_sync(sc->vge_ldata.vge_mtag,
				sc->vge_ldata.vge_rx_dmamap[i],
				BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->vge_ldata.vge_mtag,
				  sc->vge_ldata.vge_rx_dmamap[i]);

		/*
		 * If the 'start of frame' bit is set, this indicates
		 * either the first fragment in a multi-fragment receive,
		 * or an intermediate fragment. Either way, we want to
		 * accumulate the buffers.
		 */
		if (rxstat & VGE_RXPKT_SOF) {
			m->m_len = MCLBYTES - VGE_ETHER_ALIGN;
			if (sc->vge_head == NULL) {
				sc->vge_head = sc->vge_tail = m;
			} else {
				m->m_flags &= ~M_PKTHDR;
				sc->vge_tail->m_next = m;
				sc->vge_tail = m;
			}
			vge_newbuf(sc, i, NULL);
			VGE_RX_DESC_INC(i);
			continue;
		}

		/*
		 * Bad/error frames will have the RXOK bit cleared.
		 * However, there's one error case we want to allow:
		 * if a VLAN tagged frame arrives and the chip can't
		 * match it against the CAM filter, it considers this
		 * a 'VLAN CAM filter miss' and clears the 'RXOK' bit.
		 * We don't want to drop the frame though: our VLAN
		 * filtering is done in software.
		 */
		if (!(rxstat & VGE_RDSTS_RXOK) && !(rxstat & VGE_RDSTS_VIDM) &&
		    !(rxstat & VGE_RDSTS_CSUMERR)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			/*
			 * If this is part of a multi-fragment packet,
			 * discard all the pieces.
			 */
			if (sc->vge_head != NULL) {
				m_freem(sc->vge_head);
				sc->vge_head = sc->vge_tail = NULL;
			}
			vge_newbuf(sc, i, m);
			VGE_RX_DESC_INC(i);
			continue;
		}

		/*
		 * If allocating a replacement mbuf fails,
		 * reload the current one.
		 */
		if (vge_newbuf(sc, i, NULL)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			if (sc->vge_head != NULL) {
				m_freem(sc->vge_head);
				sc->vge_head = sc->vge_tail = NULL;
			}
			vge_newbuf(sc, i, m);
			VGE_RX_DESC_INC(i);
			continue;
		}

		VGE_RX_DESC_INC(i);

		if (sc->vge_head != NULL) {
			m->m_len = total_len % (MCLBYTES - VGE_ETHER_ALIGN);
			/*
			 * Special case: if there's 4 bytes or less
			 * in this buffer, the mbuf can be discarded:
			 * the last 4 bytes is the CRC, which we don't
			 * care about anyway.
			 */
			if (m->m_len <= ETHER_CRC_LEN) {
				sc->vge_tail->m_len -=
				    (ETHER_CRC_LEN - m->m_len);
				m_freem(m);
			} else {
				m->m_len -= ETHER_CRC_LEN;
				m->m_flags &= ~M_PKTHDR;
				sc->vge_tail->m_next = m;
			}
			m = sc->vge_head;
			sc->vge_head = sc->vge_tail = NULL;
			m->m_pkthdr.len = total_len - ETHER_CRC_LEN;
		} else {
			m->m_pkthdr.len = m->m_len =
			    (total_len - ETHER_CRC_LEN);
		}

#ifdef VGE_FIXUP_RX
		vge_fixup_rx(m);
#endif
		IFNET_STAT_INC(ifp, ipackets, 1);
		m->m_pkthdr.rcvif = ifp;

		/* Do RX checksumming if enabled */
		if (ifp->if_capenable & IFCAP_RXCSUM) {
			/* Check IP header checksum */
			if (rxctl & VGE_RDCTL_IPPKT)
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if (rxctl & VGE_RDCTL_IPCSUMOK)
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;

			/* Check TCP/UDP checksum */
			if (rxctl & (VGE_RDCTL_TCPPKT|VGE_RDCTL_UDPPKT) &&
			    rxctl & VGE_RDCTL_PROTOCSUMOK) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID|CSUM_PSEUDO_HDR|
				    CSUM_FRAG_NOT_CHECKED;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		if (rxstat & VGE_RDSTS_VTAG) {
			m->m_flags |= M_VLANTAG;
			m->m_pkthdr.ether_vlantag =
				ntohs((rxctl & VGE_RDCTL_VLANID));
		}
		ifp->if_input(ifp, m, NULL, -1);

		lim++;
		if (lim == VGE_RX_DESC_CNT)
			break;
	}

	/* Flush the RX DMA ring */
	bus_dmamap_sync(sc->vge_ldata.vge_rx_list_tag,
			sc->vge_ldata.vge_rx_list_map,
			BUS_DMASYNC_PREWRITE);

	sc->vge_ldata.vge_rx_prodidx = i;
	CSR_WRITE_2(sc, VGE_RXDESC_RESIDUECNT, lim);
}

static void
vge_txeof(struct vge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t txstat;
	int idx;

	idx = sc->vge_ldata.vge_tx_considx;

	/* Invalidate the TX descriptor list */

	bus_dmamap_sync(sc->vge_ldata.vge_tx_list_tag,
			sc->vge_ldata.vge_tx_list_map, BUS_DMASYNC_POSTREAD);

	while (idx != sc->vge_ldata.vge_tx_prodidx) {

		txstat = le32toh(sc->vge_ldata.vge_tx_list[idx].vge_sts);
		if (txstat & VGE_TDSTS_OWN)
			break;

		m_freem(sc->vge_ldata.vge_tx_mbuf[idx]);
		sc->vge_ldata.vge_tx_mbuf[idx] = NULL;
		bus_dmamap_unload(sc->vge_ldata.vge_mtag,
				  sc->vge_ldata.vge_tx_dmamap[idx]);
		if (txstat & (VGE_TDSTS_EXCESSCOLL|VGE_TDSTS_COLL))
			IFNET_STAT_INC(ifp, collisions, 1);
		if (txstat & VGE_TDSTS_TXERR)
			IFNET_STAT_INC(ifp, oerrors, 1);
		else
			IFNET_STAT_INC(ifp, opackets, 1);

		sc->vge_ldata.vge_tx_free++;
		VGE_TX_DESC_INC(idx);
	}

	/* No changes made to the TX ring, so no flush needed */
	if (idx != sc->vge_ldata.vge_tx_considx) {
		sc->vge_ldata.vge_tx_considx = idx;
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
	}

	/*
	 * If not all descriptors have been released reaped yet,
	 * reload the timer so that we will eventually get another
	 * interrupt that will cause us to re-enter this routine.
	 * This is done in case the transmitter has gone idle.
	 */
	if (sc->vge_ldata.vge_tx_free != VGE_TX_DESC_CNT)
		CSR_WRITE_1(sc, VGE_CRS1, VGE_CR1_TIMER0_ENABLE);
}

static void
vge_tick(struct vge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;

	mii = device_get_softc(sc->vge_miibus);

	mii_tick(mii);
	if (sc->vge_link) {
		if (!(mii->mii_media_status & IFM_ACTIVE))
			sc->vge_link = 0;
	} else {
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->vge_link = 1;
			if (!ifq_is_empty(&ifp->if_snd))
				if_devstart(ifp);
		}
	}
}

#ifdef IFPOLL_ENABLE

static void
vge_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct vge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	vge_rxeof(sc, count);
	vge_txeof(sc);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);

	/* XXX copy & paste from vge_intr */
	if (sc->vge_npoll.ifpc_stcount-- == 0) {
		uint32_t status;

		sc->vge_npoll.ifpc_stcount = sc->vge_npoll.ifpc_stfrac;

		status = CSR_READ_4(sc, VGE_ISR);
		if (status == 0xffffffff)
			return;

		if (status)
			CSR_WRITE_4(sc, VGE_ISR, status);

		if (status & (VGE_ISR_TXDMA_STALL |
			      VGE_ISR_RXDMA_STALL))
			vge_init(sc);

		if (status & (VGE_ISR_RXOFLOW | VGE_ISR_RXNODESC)) {
			IFNET_STAT_INC(ifp, ierrors, 1);
			CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_RUN);
			CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_WAK);
		}
	}
}

static void
vge_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct vge_softc *sc = ifp->if_softc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	if (info != NULL) {
		int cpuid = sc->vge_npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = vge_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING)
			vge_disable_intr(sc);
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING)
			vge_enable_intr(sc, 0xffffffff);
		ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->vge_irq));
	}
}

#endif	/* IFPOLL_ENABLE */

static void
vge_intr(void *arg)
{
	struct vge_softc *sc = arg;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	uint32_t status;

	if (sc->suspended || !(ifp->if_flags & IFF_UP))
		return;

	/* Disable interrupts */
	CSR_WRITE_1(sc, VGE_CRC3, VGE_CR3_INT_GMSK);

	for (;;) {
		status = CSR_READ_4(sc, VGE_ISR);
		/* If the card has gone away the read returns 0xffff. */
		if (status == 0xFFFFFFFF)
			break;

		if (status)
			CSR_WRITE_4(sc, VGE_ISR, status);

		if ((status & VGE_INTRS) == 0)
			break;

		if (status & (VGE_ISR_RXOK|VGE_ISR_RXOK_HIPRIO))
			vge_rxeof(sc, -1);

		if (status & (VGE_ISR_RXOFLOW|VGE_ISR_RXNODESC)) {
			vge_rxeof(sc, -1);
			IFNET_STAT_INC(ifp, ierrors, 1);
			CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_RUN);
			CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_WAK);
		}

		if (status & (VGE_ISR_TXOK0|VGE_ISR_TIMER0))
			vge_txeof(sc);

		if (status & (VGE_ISR_TXDMA_STALL|VGE_ISR_RXDMA_STALL))
			vge_init(sc);

		if (status & VGE_ISR_LINKSTS)
			vge_tick(sc);
	}

	/* Re-enable interrupts */
	CSR_WRITE_1(sc, VGE_CRS3, VGE_CR3_INT_GMSK);

	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
}

static int
vge_encap(struct vge_softc *sc, struct mbuf *m_head, int idx)
{
	struct vge_dmaload_arg arg;
	bus_dmamap_t map;
	int error;

	arg.vge_flags = 0;

	if (m_head->m_pkthdr.csum_flags & CSUM_IP)
		arg.vge_flags |= VGE_TDCTL_IPCSUM;
	if (m_head->m_pkthdr.csum_flags & CSUM_TCP)
		arg.vge_flags |= VGE_TDCTL_TCPCSUM;
	if (m_head->m_pkthdr.csum_flags & CSUM_UDP)
		arg.vge_flags |= VGE_TDCTL_UDPCSUM;

	arg.sc = sc;
	arg.vge_idx = idx;
	arg.vge_m0 = m_head;
	arg.vge_maxsegs = VGE_TX_FRAGS;

	map = sc->vge_ldata.vge_tx_dmamap[idx];
	error = bus_dmamap_load_mbuf(sc->vge_ldata.vge_mtag, map, m_head,
				     vge_dma_map_tx_desc, &arg, BUS_DMA_NOWAIT);
	if (error && error != EFBIG) {
		if_printf(&sc->arpcom.ac_if, "can't map mbuf (error %d)\n",
			  error);
		goto fail;
	}

	/* Too many segments to map, coalesce into a single mbuf */
	if (error || arg.vge_maxsegs == 0) {
		struct mbuf *m_new;

		m_new = m_defrag(m_head, M_NOWAIT);
		if (m_new == NULL) {
			error = ENOBUFS;
			goto fail;
		} else {
			m_head = m_new;
		}

		arg.sc = sc;
		arg.vge_m0 = m_head;
		arg.vge_idx = idx;
		arg.vge_maxsegs = 1;

		error = bus_dmamap_load_mbuf(sc->vge_ldata.vge_mtag, map,
					     m_head, vge_dma_map_tx_desc, &arg,
					     BUS_DMA_NOWAIT);
		if (error) {
			if_printf(&sc->arpcom.ac_if,
				  "can't map mbuf (error %d)\n", error);
			goto fail;
		}
	}

	sc->vge_ldata.vge_tx_mbuf[idx] = m_head;
	sc->vge_ldata.vge_tx_free--;

	/*
	 * Set up hardware VLAN tagging.
	 */
	if (m_head->m_flags & M_VLANTAG) {
		sc->vge_ldata.vge_tx_list[idx].vge_ctl |=
			htole32(htons(m_head->m_pkthdr.ether_vlantag) |
				VGE_TDCTL_VTAG);
	}

	sc->vge_ldata.vge_tx_list[idx].vge_sts |= htole32(VGE_TDSTS_OWN);
	return (0);

fail:
	m_freem(m_head);
	return error;
}

/*
 * Main transmit routine.
 */

static void
vge_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct vge_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	int idx, pidx = 0;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ASSERT_SERIALIZED(ifp->if_serializer);

	if (!sc->vge_link) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
		return;

	idx = sc->vge_ldata.vge_tx_prodidx;

	pidx = idx - 1;
	if (pidx < 0)
		pidx = VGE_TX_DESC_CNT - 1;

	while (sc->vge_ldata.vge_tx_mbuf[idx] == NULL) {
		if (sc->vge_ldata.vge_tx_free <= 2) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (vge_encap(sc, m_head, idx)) {
			/* If vge_encap() failed, it will free m_head for us */
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		sc->vge_ldata.vge_tx_list[pidx].vge_frag[0].vge_buflen |=
		    htole16(VGE_TXDESC_Q);

		pidx = idx;
		VGE_TX_DESC_INC(idx);

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (idx == sc->vge_ldata.vge_tx_prodidx)
		return;

	/* Flush the TX descriptors */
	bus_dmamap_sync(sc->vge_ldata.vge_tx_list_tag,
			sc->vge_ldata.vge_tx_list_map,
			BUS_DMASYNC_PREWRITE);

	/* Issue a transmit command. */
	CSR_WRITE_2(sc, VGE_TXQCSRS, VGE_TXQCSR_WAK0);

	sc->vge_ldata.vge_tx_prodidx = idx;

	/*
	 * Use the countdown timer for interrupt moderation.
	 * 'TX done' interrupts are disabled. Instead, we reset the
	 * countdown timer, which will begin counting until it hits
	 * the value in the SSTIMER register, and then trigger an
	 * interrupt. Each time we set the TIMER0_ENABLE bit, the
	 * the timer count is reloaded. Only when the transmitter
	 * is idle will the timer hit 0 and an interrupt fire.
	 */
	CSR_WRITE_1(sc, VGE_CRS1, VGE_CR1_TIMER0_ENABLE);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

static void
vge_init(void *xsc)
{
	struct vge_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	mii = device_get_softc(sc->vge_miibus);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	vge_stop(sc);
	vge_reset(sc);

	/*
	 * Initialize the RX and TX descriptors and mbufs.
	 */
	vge_rx_list_init(sc);
	vge_tx_list_init(sc);

	/* Set our station address */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VGE_PAR0 + i, IF_LLADDR(ifp)[i]);

	/*
	 * Set receive FIFO threshold. Also allow transmission and
	 * reception of VLAN tagged frames.
	 */
	CSR_CLRBIT_1(sc, VGE_RXCFG, VGE_RXCFG_FIFO_THR|VGE_RXCFG_VTAGOPT);
	CSR_SETBIT_1(sc, VGE_RXCFG, VGE_RXFIFOTHR_128BYTES|VGE_VTAG_OPT2);

	/* Set DMA burst length */
	CSR_CLRBIT_1(sc, VGE_DMACFG0, VGE_DMACFG0_BURSTLEN);
	CSR_SETBIT_1(sc, VGE_DMACFG0, VGE_DMABURST_128);

	CSR_SETBIT_1(sc, VGE_TXCFG, VGE_TXCFG_ARB_PRIO|VGE_TXCFG_NONBLK);

	/* Set collision backoff algorithm */
	CSR_CLRBIT_1(sc, VGE_CHIPCFG1, VGE_CHIPCFG1_CRANDOM|
	    VGE_CHIPCFG1_CAP|VGE_CHIPCFG1_MBA|VGE_CHIPCFG1_BAKOPT);
	CSR_SETBIT_1(sc, VGE_CHIPCFG1, VGE_CHIPCFG1_OFSET);

	/* Disable LPSEL field in priority resolution */
	CSR_SETBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_LPSEL_DIS);

	/*
	 * Load the addresses of the DMA queues into the chip.
	 * Note that we only use one transmit queue.
	 */
	CSR_WRITE_4(sc, VGE_TXDESC_ADDR_LO0,
	    VGE_ADDR_LO(sc->vge_ldata.vge_tx_list_addr));
	CSR_WRITE_2(sc, VGE_TXDESCNUM, VGE_TX_DESC_CNT - 1);

	CSR_WRITE_4(sc, VGE_RXDESC_ADDR_LO,
	    VGE_ADDR_LO(sc->vge_ldata.vge_rx_list_addr));
	CSR_WRITE_2(sc, VGE_RXDESCNUM, VGE_RX_DESC_CNT - 1);
	CSR_WRITE_2(sc, VGE_RXDESC_RESIDUECNT, VGE_RX_DESC_CNT);

	/* Enable and wake up the RX descriptor queue */
	CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_RUN);
	CSR_WRITE_1(sc, VGE_RXQCSRS, VGE_RXQCSR_WAK);

	/* Enable the TX descriptor queue */
	CSR_WRITE_2(sc, VGE_TXQCSRS, VGE_TXQCSR_RUN0);

	/* Set up the receive filter -- allow large frames for VLANs. */
	CSR_WRITE_1(sc, VGE_RXCTL, VGE_RXCTL_RX_UCAST|VGE_RXCTL_RX_GIANT);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		CSR_SETBIT_1(sc, VGE_RXCTL, VGE_RXCTL_RX_PROMISC);

	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		CSR_SETBIT_1(sc, VGE_RXCTL, VGE_RXCTL_RX_BCAST);

	/* Set multicast bit to capture multicast frames. */
	if (ifp->if_flags & IFF_MULTICAST)
		CSR_SETBIT_1(sc, VGE_RXCTL, VGE_RXCTL_RX_MCAST);

	/* Init the cam filter. */
	vge_cam_clear(sc);

	/* Init the multicast filter. */
	vge_setmulti(sc);

	/* Enable flow control */

	CSR_WRITE_1(sc, VGE_CRS2, 0x8B);

	/* Enable jumbo frame reception (if desired) */

	/* Start the MAC. */
	CSR_WRITE_1(sc, VGE_CRC0, VGE_CR0_STOP);
	CSR_WRITE_1(sc, VGE_CRS1, VGE_CR1_NOPOLL);
	CSR_WRITE_1(sc, VGE_CRS0,
	    VGE_CR0_TX_ENABLE|VGE_CR0_RX_ENABLE|VGE_CR0_START);

	/*
	 * Configure one-shot timer for microsecond
	 * resulution and load it for 500 usecs.
	 */
	CSR_SETBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_TIMER0_RES);
	CSR_WRITE_2(sc, VGE_SSTIMER, 400);

	/*
	 * Configure interrupt moderation for receive. Enable
	 * the holdoff counter and load it, and set the RX
	 * suppression count to the number of descriptors we
	 * want to allow before triggering an interrupt.
	 * The holdoff timer is in units of 20 usecs.
	 */

#ifdef notyet
	CSR_WRITE_1(sc, VGE_INTCTL1, VGE_INTCTL_TXINTSUP_DISABLE);
	/* Select the interrupt holdoff timer page. */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_INTHLDOFF);
	CSR_WRITE_1(sc, VGE_INTHOLDOFF, 10); /* ~200 usecs */

	/* Enable use of the holdoff timer. */
	CSR_WRITE_1(sc, VGE_CRS3, VGE_CR3_INT_HOLDOFF);
	CSR_WRITE_1(sc, VGE_INTCTL1, VGE_INTCTL_SC_RELOAD);

	/* Select the RX suppression threshold page. */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_RXSUPPTHR);
	CSR_WRITE_1(sc, VGE_RXSUPPTHR, 64); /* interrupt after 64 packets */

	/* Restore the page select bits. */
	CSR_CLRBIT_1(sc, VGE_CAMCTL, VGE_CAMCTL_PAGESEL);
	CSR_SETBIT_1(sc, VGE_CAMCTL, VGE_PAGESEL_MAR);
#endif

#ifdef IFPOLL_ENABLE
	/* Disable intr if polling(4) is enabled */
	if (ifp->if_flags & IFF_NPOLLING)
		vge_disable_intr(sc);
	else
#endif
	vge_enable_intr(sc, 0);

	mii_mediachg(mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	sc->vge_if_flags = 0;
	sc->vge_link = 0;
}

/*
 * Set media options.
 */
static int
vge_ifmedia_upd(struct ifnet *ifp)
{
	struct vge_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->vge_miibus);

	mii_mediachg(mii);

	return (0);
}

/*
 * Report current media status.
 */
static void
vge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vge_softc *sc = ifp->if_softc;
	struct mii_data *mii = device_get_softc(sc->vge_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static void
vge_miibus_statchg(device_t dev)
{
	struct vge_softc *sc;
	struct mii_data *mii;
	struct ifmedia_entry *ife;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->vge_miibus);
	ife = mii->mii_media.ifm_cur;

	/*
	 * If the user manually selects a media mode, we need to turn
	 * on the forced MAC mode bit in the DIAGCTL register. If the
	 * user happens to choose a full duplex mode, we also need to
	 * set the 'force full duplex' bit. This applies only to
	 * 10Mbps and 100Mbps speeds. In autoselect mode, forced MAC
	 * mode is disabled, and in 1000baseT mode, full duplex is
	 * always implied, so we turn on the forced mode bit but leave
	 * the FDX bit cleared.
	 */

	switch (IFM_SUBTYPE(ife->ifm_media)) {
	case IFM_AUTO:
		CSR_CLRBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_MACFORCE);
		CSR_CLRBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_FDXFORCE);
		break;
	case IFM_1000_T:
		CSR_SETBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_MACFORCE);
		CSR_CLRBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_FDXFORCE);
		break;
	case IFM_100_TX:
	case IFM_10_T:
		CSR_SETBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_MACFORCE);
		if ((ife->ifm_media & IFM_GMASK) == IFM_FDX)
			CSR_SETBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_FDXFORCE);
		else
			CSR_CLRBIT_1(sc, VGE_DIAGCTL, VGE_DIAGCTL_FDXFORCE);
		break;
	default:
		device_printf(dev, "unknown media type: %x\n",
			      IFM_SUBTYPE(ife->ifm_media));
		break;
	}
}

static int
vge_ioctl(struct ifnet *ifp, u_long command, caddr_t data, struct ucred *cr)
{
	struct vge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0;

	switch (command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu > VGE_JUMBO_MTU)
			error = EINVAL;
		ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) &&
			    (ifp->if_flags & IFF_PROMISC) &&
			    !(sc->vge_if_flags & IFF_PROMISC)) {
				CSR_SETBIT_1(sc, VGE_RXCTL,
				    VGE_RXCTL_RX_PROMISC);
				vge_setmulti(sc);
			} else if ((ifp->if_flags & IFF_RUNNING) &&
				   !(ifp->if_flags & IFF_PROMISC) &&
				   (sc->vge_if_flags & IFF_PROMISC)) {
				CSR_CLRBIT_1(sc, VGE_RXCTL,
					     VGE_RXCTL_RX_PROMISC);
				vge_setmulti(sc);
                        } else {
				vge_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vge_stop(sc);
		}
		sc->vge_if_flags = ifp->if_flags;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		vge_setmulti(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->vge_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
	    {
		uint32_t mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable |= ifr->ifr_reqcap & (IFCAP_HWCSUM);
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist = VGE_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
			if (ifp->if_flags & IFF_RUNNING)
				vge_init(sc);
		}
	    }
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static void
vge_watchdog(struct ifnet *ifp)
{
	struct vge_softc *sc = ifp->if_softc;

	if_printf(ifp, "watchdog timeout\n");
	IFNET_STAT_INC(ifp, oerrors, 1);

	vge_txeof(sc);
	vge_rxeof(sc, -1);

	vge_init(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
vge_stop(struct vge_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int i;

	ASSERT_SERIALIZED(ifp->if_serializer);

	ifp->if_timer = 0;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_WRITE_1(sc, VGE_CRC3, VGE_CR3_INT_GMSK);
	CSR_WRITE_1(sc, VGE_CRS0, VGE_CR0_STOP);
	CSR_WRITE_4(sc, VGE_ISR, 0xFFFFFFFF);
	CSR_WRITE_2(sc, VGE_TXQCSRC, 0xFFFF);
	CSR_WRITE_1(sc, VGE_RXQCSRC, 0xFF);
	CSR_WRITE_4(sc, VGE_RXDESC_ADDR_LO, 0);

	if (sc->vge_head != NULL) {
		m_freem(sc->vge_head);
		sc->vge_head = sc->vge_tail = NULL;
	}

	/* Free the TX list buffers. */
	for (i = 0; i < VGE_TX_DESC_CNT; i++) {
		if (sc->vge_ldata.vge_tx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->vge_ldata.vge_mtag,
					  sc->vge_ldata.vge_tx_dmamap[i]);
			m_freem(sc->vge_ldata.vge_tx_mbuf[i]);
			sc->vge_ldata.vge_tx_mbuf[i] = NULL;
		}
	}

	/* Free the RX list buffers. */
	for (i = 0; i < VGE_RX_DESC_CNT; i++) {
		if (sc->vge_ldata.vge_rx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->vge_ldata.vge_mtag,
					  sc->vge_ldata.vge_rx_dmamap[i]);
			m_freem(sc->vge_ldata.vge_rx_mbuf[i]);
			sc->vge_ldata.vge_rx_mbuf[i] = NULL;
		}
	}
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
vge_suspend(device_t dev)
{
	struct vge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	vge_stop(sc);
	sc->suspended = 1;
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
vge_resume(device_t dev)
{
	struct vge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* reenable busmastering */
	pci_enable_busmaster(dev);
	pci_enable_io(dev, SYS_RES_MEMORY);

	lwkt_serialize_enter(ifp->if_serializer);
	/* reinitialize interface if necessary */
	if (ifp->if_flags & IFF_UP)
		vge_init(sc);

	sc->suspended = 0;
	lwkt_serialize_exit(ifp->if_serializer);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static void
vge_shutdown(device_t dev)
{
	struct vge_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	vge_stop(sc);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
vge_enable_intr(struct vge_softc *sc, uint32_t isr)
{
	CSR_WRITE_4(sc, VGE_IMR, VGE_INTRS);
	CSR_WRITE_4(sc, VGE_ISR, isr);
	CSR_WRITE_1(sc, VGE_CRS3, VGE_CR3_INT_GMSK);
}

#ifdef IFPOLL_ENABLE

static void
vge_disable_intr(struct vge_softc *sc)
{
	CSR_WRITE_4(sc, VGE_IMR, 0);
	CSR_WRITE_1(sc, VGE_CRC3, VGE_CR3_INT_GMSK);
	sc->vge_npoll.ifpc_stcount = 0;
}

#endif	/* IFPOLL_ENABLE */
