/* $FreeBSD: src/sys/dev/mii/e1000phy.c,v 1.18 2006/12/11 11:09:48 yongari Exp $ */
/*	$OpenBSD: eephy.c,v 1.26 2006/06/08 00:27:12 brad Exp $	*/
/*
 * Principal Author: Parag Patel
 * Copyright (c) 2001
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Additonal Copyright (c) 2001 by Traakan Software under same licence.
 * Secondary Author: Matthew Jacob
 */

/*
 * driver for the Marvell 88E1000 series external 1000/100/10-BT PHY.
 */

/*
 * Support added for the Marvell 88E1011 (Alaska) 1000/100/10baseT and
 * 1000baseSX PHY.
 * Nathan Binkert <nate@openbsd.org>
 * Jung-uk Kim <jkim@niksun.com>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include "mii.h"
#include "miivar.h"
#include "miidevs.h"

#include "e1000phyreg.h"

#include "miibus_if.h"

static int	e1000phy_probe(device_t);
static int	e1000phy_attach(device_t);
static int	e1000phy_service(struct mii_softc *, struct mii_data *, int);
static void	e1000phy_status(struct mii_softc *);
static void	e1000phy_mii_phy_auto(struct mii_softc *);
static void	e1000phy_reset(struct mii_softc *);

static device_method_t e1000phy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		e1000phy_probe),
	DEVMETHOD(device_attach,	e1000phy_attach),
	DEVMETHOD(device_detach,	ukphy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	{ 0, 0 }
};

static const struct mii_phydesc e1000phys[] = {
	MII_PHYDESC(xxMARVELL,	E1011),
	MII_PHYDESC(xxMARVELL,	E1000_3),
	MII_PHYDESC(xxMARVELL,	E1000_5),
	MII_PHYDESC(xxMARVELL,	E1111),
	MII_PHYDESC(MARVELL,	E1000),
	MII_PHYDESC(MARVELL,	E1011),
	MII_PHYDESC(MARVELL,	E1000_3),
	MII_PHYDESC(MARVELL,	E1000S),
	MII_PHYDESC(MARVELL,	E1000_5),
	MII_PHYDESC(MARVELL,	E1101),
	MII_PHYDESC(MARVELL,	E3082),
	MII_PHYDESC(MARVELL,	E1112),
	MII_PHYDESC(MARVELL,	E1149),
	MII_PHYDESC(MARVELL,	E1111),
	MII_PHYDESC(MARVELL,	E1116),
	MII_PHYDESC(MARVELL,	E1116R),
	MII_PHYDESC(MARVELL,	E1118),
	MII_PHYDESC(MARVELL,	E3016),
	MII_PHYDESC(MARVELL,	PHYG65G),
	MII_PHYDESC(MARVELL,	E1149R),
	MII_PHYDESC_NULL
};

static devclass_t e1000phy_devclass;

static driver_t e1000phy_driver = {
	"e1000phy",
	e1000phy_methods,
	sizeof(struct mii_softc)
};
DRIVER_MODULE(e1000phy, miibus, e1000phy_driver, e1000phy_devclass, NULL, NULL);

static int
e1000phy_probe(device_t	dev)
{
	struct mii_attach_args *ma = device_get_ivars(dev);
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, e1000phys);
	if (mpd != NULL) {
		device_set_desc(dev, mpd->mpd_name);
		return 0;
	}
	return (ENXIO);
}

static int
e1000phy_attach(device_t dev)
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
	sc->mii_service = e1000phy_service;
	sc->mii_reset = e1000phy_reset;
	sc->mii_anegticks = MII_ANEGTICKS_GIGE;
	sc->mii_pdata = mii;

	sc->mii_flags |= MIIF_NOISOLATE;

	switch (sc->mii_model) {
	case MII_MODEL_MARVELL_E1011:
	case MII_MODEL_MARVELL_E1112:
		if (PHY_READ(sc, E1000_ESSR) & E1000_ESSR_FIBER_LINK)
			sc->mii_flags |= MIIF_HAVEFIBER;
		break;
	case MII_MODEL_MARVELL_E1149:
	case MII_MODEL_MARVELL_E1149R:
		/*
		 * Some 88E1149 PHY's page select is initialized to
		 * point to other bank instead of copper/fiber bank
		 * which in turn resulted in wrong registers were
		 * accessed during PHY operation. It is believed that
		 * page 0 should be used for copper PHY so reinitialize
		 * E1000_EADR to select default copper PHY. If parent
		 * device know the type of PHY(either copper or fiber),
		 * that information should be used to select default
		 * type of PHY.
		 */
		PHY_WRITE(sc, E1000_EADR, 0);
		break;
	}

	mii->mii_instance++;

	e1000phy_reset(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);

	if ((sc->mii_extcapabilities & (EXTSR_1000TFDX | EXTSR_1000THDX)) == 0)
		sc->mii_anegticks = MII_ANEGTICKS;

	device_printf(dev, " ");
	if ((sc->mii_capabilities & BMSR_MEDIAMASK) == 0 &&
	    (sc->mii_extcapabilities & EXTSR_MEDIAMASK) == 0)
		kprintf("no media present");
	else
		mii_phy_add_media(sc);
	kprintf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return 0;
}

static void
e1000phy_reset(struct mii_softc *sc)
{
	uint16_t reg, page;

	reg = PHY_READ(sc, E1000_SCR);
	if ((sc->mii_flags & MIIF_HAVEFIBER) != 0) {
		reg &= ~E1000_SCR_AUTO_X_MODE;
		PHY_WRITE(sc, E1000_SCR, reg);
		if (sc->mii_model == MII_MODEL_MARVELL_E1112) {
			/* Select 1000BASE-X only mode. */
			page = PHY_READ(sc, E1000_EADR);
			PHY_WRITE(sc, E1000_EADR, 2);
			reg = PHY_READ(sc, E1000_SCR);
			reg &= ~E1000_SCR_MODE_MASK;
			reg |= E1000_SCR_MODE_1000BX;
			PHY_WRITE(sc, E1000_SCR, reg);
			/* TODO */
			PHY_WRITE(sc, E1000_EADR, page);
		}
	} else {
		switch (sc->mii_model) {
		case MII_MODEL_MARVELL_E1111:
		case MII_MODEL_MARVELL_E1112:
		case MII_MODEL_MARVELL_E1116:
		case MII_MODEL_MARVELL_E1118:
		case MII_MODEL_MARVELL_E1149:
		case MII_MODEL_MARVELL_E1149R:
		case MII_MODEL_MARVELL_PHYG65G:
			/* Disable energy detect mode. */
			reg &= ~E1000_SCR_EN_DETECT_MASK;
			reg |= E1000_SCR_AUTO_X_MODE;
			if (sc->mii_model == MII_MODEL_MARVELL_E1116)
				reg &= ~E1000_SCR_POWER_DOWN;
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		case MII_MODEL_MARVELL_E3082:
			reg |= (E1000_SCR_AUTO_X_MODE >> 1);
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		case MII_MODEL_MARVELL_E3016:
			reg |= E1000_SCR_AUTO_MDIX;
			reg &= ~(E1000_SCR_EN_DETECT |
			    E1000_SCR_SCRAMBLER_DISABLE);
			reg |= E1000_SCR_LPNP;
			/* XXX Enable class A driver for Yukon FE+ A0. */
			PHY_WRITE(sc, 0x1C, PHY_READ(sc, 0x1C) | 0x0001);
			break;
		default:
			reg &= ~E1000_SCR_AUTO_X_MODE;
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		}
		if (sc->mii_model != MII_MODEL_MARVELL_E3016) {
			/* Auto correction for reversed cable polarity. */
			reg &= ~E1000_SCR_POLARITY_REVERSAL;
		}
		PHY_WRITE(sc, E1000_SCR, reg);

		if (sc->mii_model == MII_MODEL_MARVELL_E1116 ||
		    sc->mii_model == MII_MODEL_MARVELL_E1149 ||
		    sc->mii_model == MII_MODEL_MARVELL_E1149R) {
			PHY_WRITE(sc, E1000_EADR, 2);
			reg = PHY_READ(sc, E1000_SCR);
			reg |= E1000_SCR_RGMII_POWER_UP;
			PHY_WRITE(sc, E1000_SCR, reg);
			PHY_WRITE(sc, E1000_EADR, 0);
		}
	}

	switch (MII_MODEL(sc->mii_model)) {
	case MII_MODEL_MARVELL_E3082:
	case MII_MODEL_MARVELL_E1112:
	case MII_MODEL_MARVELL_E1118:
		break;
	case MII_MODEL_MARVELL_E1116:
		page = PHY_READ(sc, E1000_EADR);
		/* Select page 3, LED control register. */
		PHY_WRITE(sc, E1000_EADR, 3);
		PHY_WRITE(sc, E1000_SCR,
		    E1000_SCR_LED_LOS(1) |	/* Link/Act */
		    E1000_SCR_LED_INIT(8) |	/* 10Mbps */
		    E1000_SCR_LED_STAT1(7) |	/* 100Mbps */
		    E1000_SCR_LED_STAT0(7));	/* 1000Mbps */
		/* Set blink rate. */
		PHY_WRITE(sc, E1000_IER, E1000_PULSE_DUR(E1000_PULSE_170MS) |
		    E1000_BLINK_RATE(E1000_BLINK_84MS));
		PHY_WRITE(sc, E1000_EADR, page);
		break;
	case MII_MODEL_MARVELL_E3016:
		/* LED2 -> ACT, LED1 -> LINK, LED0 -> SPEED. */
		PHY_WRITE(sc, 0x16, 0x0B << 8 | 0x05 << 4 | 0x04);
		/* Integrated register calibration workaround. */
		PHY_WRITE(sc, 0x1D, 17);
		PHY_WRITE(sc, 0x1E, 0x3F60);
 		break;
	default:
		/* Force TX_CLK to 25MHz clock. */
		reg = PHY_READ(sc, E1000_ESCR);
		reg |= E1000_ESCR_TX_CLK_25;
		PHY_WRITE(sc, E1000_ESCR, reg);
		break;
	}

	/* Reset the PHY so all changes take effect. */
	reg = PHY_READ(sc, E1000_CR);
	reg |= E1000_CR_RESET;
	PHY_WRITE(sc, E1000_CR, reg);
}

static int
e1000phy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t speed, gig;
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
			reg = PHY_READ(sc, E1000_CR);
			PHY_WRITE(sc, E1000_CR, reg | E1000_CR_ISOLATE);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		if (IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO) {
			e1000phy_mii_phy_auto(sc);
			break;
		}

		speed = 0;
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_1000_T:
			if ((sc->mii_extcapabilities &
			    (EXTSR_1000TFDX | EXTSR_1000THDX)) == 0)
				return EINVAL;
			speed = E1000_CR_SPEED_1000;
			break;
		case IFM_1000_SX:
			if ((sc->mii_extcapabilities &
			    (EXTSR_1000XFDX | EXTSR_1000XHDX)) == 0)
				return EINVAL;
			speed = E1000_CR_SPEED_1000;
			break;
		case IFM_100_TX:
			speed = E1000_CR_SPEED_100;
			break;
		case IFM_10_T:
			speed = E1000_CR_SPEED_10;
			break;
		case IFM_NONE:
			reg = PHY_READ(sc, E1000_CR);
			PHY_WRITE(sc, E1000_CR,
			    reg | E1000_CR_ISOLATE | E1000_CR_POWER_DOWN);
			goto done;
		default:
			return (EINVAL);
		}

		if (((ife->ifm_media & IFM_GMASK) & IFM_FDX) != 0) {
			speed |= E1000_CR_FULL_DUPLEX;
			gig = E1000_1GCR_1000T_FD;
		} else {
			gig = E1000_1GCR_1000T;
		}

		reg = PHY_READ(sc, E1000_CR);
		reg &= ~E1000_CR_AUTO_NEG_ENABLE;
		PHY_WRITE(sc, E1000_CR, reg | E1000_CR_RESET);

		/*
		 * When setting the link manually, one side must
		 * be the master and the other the slave. However
		 * ifmedia doesn't give us a good way to specify
		 * this, so we fake it by using one of the LINK
		 * flags. If LINK0 is set, we program the PHY to
		 * be a master, otherwise it's a slave.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T ||
		    IFM_SUBTYPE(ife->ifm_media) == IFM_1000_SX) {
			if (mii->mii_ifp->if_flags & IFF_LINK0) {
				PHY_WRITE(sc, E1000_1GCR, gig |
				    E1000_1GCR_MS_ENABLE | E1000_1GCR_MS_VALUE);
			} else {
				PHY_WRITE(sc, E1000_1GCR, gig |
				    E1000_1GCR_MS_ENABLE);
			}
		} else {
			if ((sc->mii_extcapabilities &
			    (EXTSR_1000TFDX | EXTSR_1000THDX)) != 0)
				PHY_WRITE(sc, E1000_1GCR, 0);
		}
		PHY_WRITE(sc, E1000_AR, E1000_AR_SELECTOR_FIELD);
		PHY_WRITE(sc, E1000_CR, speed | E1000_CR_RESET);
done:
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		/*
		 * Is the interface even up?
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			break;

		/*
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 */
		reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Only retry autonegotiation every mii_anegticks seconds.
		 */
		if (++sc->mii_ticks <= sc->mii_anegticks)
			return (0);
		sc->mii_ticks = 0;

		e1000phy_reset(sc);
		e1000phy_mii_phy_auto(sc);
		break;
	}

	/* Update the media status. */
	e1000phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
e1000phy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, gsr, ssr, ar, lpar;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, E1000_SR) | PHY_READ(sc, E1000_SR);
	bmcr = PHY_READ(sc, E1000_CR);
	ssr = PHY_READ(sc, E1000_SSR);

	if (bmsr & E1000_SR_LINK_STATUS)
		mii->mii_media_status |= IFM_ACTIVE;

	if (bmcr & E1000_CR_LOOPBACK)
		mii->mii_media_active |= IFM_LOOP;

	if ((bmcr & E1000_CR_AUTO_NEG_ENABLE) != 0 &&
	    (ssr & E1000_SSR_SPD_DPLX_RESOLVED) == 0) {
		/* Erg, still trying, I guess... */
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if ((sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		switch (ssr & E1000_SSR_SPEED) {
		case E1000_SSR_1000MBS:
			mii->mii_media_active |= IFM_1000_T;
			break;
		case E1000_SSR_100MBS:
			mii->mii_media_active |= IFM_100_TX;
			break;
		case E1000_SSR_10MBS:
			mii->mii_media_active |= IFM_10_T;
			break;
		default:
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	} else {
		/*
		 * Some fiber PHY(88E1112) does not seem to set resolved
		 * speed so always assume we've got IFM_1000_SX.
		 */
		mii->mii_media_active |= IFM_1000_SX;
	}

	if (ssr & E1000_SSR_DUPLEX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;

	if ((sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		ar = PHY_READ(sc, E1000_AR);
		lpar = PHY_READ(sc, E1000_LPAR);
		/* FLAG0==rx-flow-control FLAG1==tx-flow-control */
		if ((ar & E1000_AR_PAUSE) && (lpar & E1000_LPAR_PAUSE)) {
			mii->mii_media_active |= IFM_FLAG0 | IFM_FLAG1;
		} else if (!(ar & E1000_AR_PAUSE) && (ar & E1000_AR_ASM_DIR) &&
		    (lpar & E1000_LPAR_PAUSE) && (lpar & E1000_LPAR_ASM_DIR)) {
			mii->mii_media_active |= IFM_FLAG1;
		} else if ((ar & E1000_AR_PAUSE) && (ar & E1000_AR_ASM_DIR) &&
		    !(lpar & E1000_LPAR_PAUSE) && (lpar & E1000_LPAR_ASM_DIR)) {
			mii->mii_media_active |= IFM_FLAG0;
		}
	}

	/* FLAG2 : local PHY resolved to MASTER */
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX) {
		PHY_READ(sc, E1000_1GSR);
		gsr = PHY_READ(sc, E1000_1GSR);
		if ((gsr & E1000_1GSR_MS_CONFIG_RES) != 0)
			mii->mii_media_active |= IFM_FLAG2;
	}
}

static void
e1000phy_mii_phy_auto(struct mii_softc *sc)
{
	if ((sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		uint16_t reg;

		reg = PHY_READ(sc, E1000_AR);
		reg |= E1000_AR_10T | E1000_AR_10T_FD |
 		    E1000_AR_100TX | E1000_AR_100TX_FD |
		    E1000_AR_PAUSE | E1000_AR_ASM_DIR;
		PHY_WRITE(sc, E1000_AR, reg | E1000_AR_SELECTOR_FIELD);
	} else {
		PHY_WRITE(sc, E1000_AR, E1000_FA_1000X_FD | E1000_FA_1000X |
		    E1000_FA_SYM_PAUSE | E1000_FA_ASYM_PAUSE);
	}
	if ((sc->mii_extcapabilities &
	    (EXTSR_1000TFDX | EXTSR_1000THDX)) != 0) {
		PHY_WRITE(sc, E1000_1GCR,
		    E1000_1GCR_1000T_FD | E1000_1GCR_1000T);
	}
	PHY_WRITE(sc, E1000_CR,
	    E1000_CR_AUTO_NEG_ENABLE | E1000_CR_RESTART_AUTO_NEG);
}
