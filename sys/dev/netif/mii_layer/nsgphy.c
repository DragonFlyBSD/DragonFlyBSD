/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 2001
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
 * $FreeBSD: src/sys/dev/mii/nsgphy.c,v 1.1.2.3 2002/11/08 21:53:49 semenu Exp $
 */

/*
 * Driver for the National Semiconductor DP83891 and DP83861
 * 10/100/1000 PHYs.
 * Datasheet available at: http://www.national.com/ds/DP/DP83861.pdf
 *
 * The DP83891 is the older NatSemi gigE PHY which isn't being sold
 * anymore. The DP83861 is its replacement, which is an 'enhanced'
 * firmware driven component. The major difference between the
 * two is that the 83891 can't generate interrupts, while the
 * 83861 can. (I think it wasn't originally designed to do this, but
 * it can now thanks to firmware updates.) The 83861 also allows
 * access to its internal RAM via indirect register access.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>

#include <machine/clock.h>

#include <net/if.h>
#include <net/if_media.h>

#include "mii.h"
#include "miivar.h"
#include "miidevs.h"

#include "nsgphyreg.h"

#include "miibus_if.h"

static int nsgphy_probe		(device_t);
static int nsgphy_attach	(device_t);

static device_method_t nsgphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		nsgphy_probe),
	DEVMETHOD(device_attach,	nsgphy_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	{ 0, 0 }
};

static const struct mii_phydesc nsgphys[] = {
	MII_PHYDESC(NATSEMI,	DP83891),
	MII_PHYDESC(NATSEMI,	DP83861),
	MII_PHYDESC_NULL
};

static devclass_t nsgphy_devclass;

static driver_t nsgphy_driver = {
	"nsgphy",
	nsgphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(nsgphy, miibus, nsgphy_driver, nsgphy_devclass, NULL, NULL);

static int	nsgphy_service(struct mii_softc *, struct mii_data *, int);
static void	nsgphy_status(struct mii_softc *);

static int
nsgphy_probe(device_t dev)
{
	struct mii_attach_args *ma = device_get_ivars(dev);
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, nsgphys);
	if (mpd != NULL) {
		device_set_desc(dev, mpd->mpd_name);
		return (0);
	}
	return(ENXIO);
}

static int
nsgphy_attach(device_t dev)
{
	struct mii_softc *sc;
	struct mii_attach_args *ma;
	struct mii_data *mii;

	sc = device_get_softc(dev);
	ma = device_get_ivars(dev);
	mii_softc_init(sc, ma);
	sc->mii_dev = device_get_parent(dev);
	mii = device_get_softc(sc->mii_dev);
	LIST_INSERT_HEAD(&mii->mii_phys, sc, mii_list);

	sc->mii_inst = mii->mii_instance;
	sc->mii_service = nsgphy_service;
	/*
	 * Only retry autonegotiation every 17 seconds.
	 * Actually, for gigE PHYs, we should wait longer, since
	 * 5 seconds is the mimimum time the documentation
	 * says to wait for a 1000mbps link to be established.
	 */
	sc->mii_anegticks = 17;
	sc->mii_pdata = mii;

	sc->mii_flags |= MIIF_NOISOLATE;
	mii->mii_instance++;

#define	ADD(m, c)	ifmedia_add(&mii->mii_media, (m), (c), NULL)

	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_NONE, 0, sc->mii_inst),
	    MII_MEDIA_NONE);
#if 0
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX, IFM_LOOP, sc->mii_inst),
	    MII_MEDIA_100_TX);
#endif

	mii_phy_reset(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
        if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);

	device_printf(dev, " ");
	if ((sc->mii_capabilities & BMSR_MEDIAMASK) ||
	    (sc->mii_extcapabilities & EXTSR_MEDIAMASK))
		mii_phy_add_media(sc);
	else
		kprintf("no media present");

	kprintf("\n");
#undef ADD

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return(0);
}

int
nsgphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst) {
			reg = PHY_READ(sc, MII_BMCR);
			PHY_WRITE(sc, MII_BMCR, reg | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_set_media(sc);
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);
		break;
	}

	/* Update the media status. */
	nsgphy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
nsgphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, physup, anlpar, gstat;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, NSGPHY_MII_BMSR);
	physup = PHY_READ(sc, NSGPHY_MII_PHYSUP);
	if (physup & NSGPHY_PHYSUP_LNKSTS)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, NSGPHY_MII_BMCR);

	if (bmcr & NSGPHY_BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & NSGPHY_BMCR_AUTOEN) {
		if ((bmsr & NSGPHY_BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
		anlpar = PHY_READ(sc, NSGPHY_MII_ANLPAR);
		gstat = PHY_READ(sc, NSGPHY_MII_1000STS);
		if (gstat & NSGPHY_1000STS_LPFD)
			mii->mii_media_active |= IFM_1000_T | IFM_FDX;
		else if (gstat & NSGPHY_1000STS_LPHD)
			mii->mii_media_active |= IFM_1000_T | IFM_HDX;
		else if (anlpar & NSGPHY_ANLPAR_100T4)
			mii->mii_media_active |= IFM_100_T4;
		else if (anlpar & NSGPHY_ANLPAR_100FDX)
			mii->mii_media_active |= IFM_100_TX|IFM_FDX;
		else if (anlpar & NSGPHY_ANLPAR_100HDX)
			mii->mii_media_active |= IFM_100_TX;
		else if (anlpar & NSGPHY_ANLPAR_10FDX)
			mii->mii_media_active |= IFM_10_T|IFM_FDX;
		else if (anlpar & NSGPHY_ANLPAR_10HDX)
			mii->mii_media_active |= IFM_10_T|IFM_HDX;
		else
			mii->mii_media_active |= IFM_NONE;
		return;
	}

	switch(bmcr & (NSGPHY_BMCR_SPD1|NSGPHY_BMCR_SPD0)) {
	case NSGPHY_S1000:
		mii->mii_media_active |= IFM_1000_T;
		break;
	case NSGPHY_S100:
		mii->mii_media_active |= IFM_100_TX;
		break;
	case NSGPHY_S10:
		mii->mii_media_active |= IFM_10_T;
		break;
	default:
		break;
	}

	if (bmcr & NSGPHY_BMCR_FDX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;
}
