/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $NetBSD: if_rtw_pci.c,v 1.4 2005/12/04 17:44:02 christos Exp $
 */

/*
 * Copyright (c) 1998, 1999, 2000, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center; Charles M. Hannum; and David Young.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI bus front-end for the Realtek RTL8180 802.11 MAC/BBP chip.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include "pcidevs.h"

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_onoe_param.h>

#include <dev/netif/rtw/rtwreg.h>
#include <dev/netif/rtw/sa2400reg.h>
#include <dev/netif/rtw/rtwvar.h>

/*
 * PCI configuration space registers
 */
#define	RTW_PCI_IOBA		0x10	/* i/o mapped base */
#define	RTW_PCI_MMBA		0x14	/* memory mapped base */

static const struct rtw_pci_reg {
	int	reg_type;
	int	reg_rid;
} rtw_pci_regs[] = {
	/* Prefer IO memory over IO port */
	{ SYS_RES_MEMORY, RTW_PCI_MMBA },
	{ SYS_RES_IOPORT, RTW_PCI_IOBA }
};

static const struct rtw_pci_product {
	uint16_t	app_vendor;	/* PCI vendor ID */
	uint16_t	app_product;	/* PCI product ID */
	const char	*app_product_name;
} rtw_pci_products[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8180,
	  "Realtek RTL8180 802.11 MAC/BBP" },
	{ PCI_VENDOR_BELKIN, PCI_PRODUCT_BELKIN_F5D6001,
	  "Belkin F5D6001" },

	{ 0, 0, NULL }
};

static int	rtw_pci_probe(device_t);
static int	rtw_pci_attach(device_t);
static int	rtw_pci_detach(device_t);
static int	rtw_pci_shutdown(device_t);

static device_method_t rtw_pci_methods[] = {
	DEVMETHOD(device_probe,		rtw_pci_probe),
	DEVMETHOD(device_attach,	rtw_pci_attach),
	DEVMETHOD(device_detach,	rtw_pci_detach),
	DEVMETHOD(device_shutdown,	rtw_pci_shutdown),
#if 0
	DEVMETHOD(device_suspend,	rtw_pci_suspend),
	DEVMETHOD(device_resume,	rtw_pci_resume),
#endif
	DEVMETHOD_END
};

static driver_t rtw_pci_driver = {
	"rtw",
	rtw_pci_methods,
	sizeof(struct rtw_softc)
};

DRIVER_MODULE(rtw, pci, rtw_pci_driver, rtw_devclass, NULL, NULL);
DRIVER_MODULE(rtw, cardbus, rtw_pci_driver, rtw_devclass, NULL, NULL);

MODULE_DEPEND(rtw, wlan, 1, 1, 1);
MODULE_DEPEND(rtw, wlan_ratectl_onoe, 1, 1, 1);
MODULE_DEPEND(rtw, pci, 1, 1, 1);
MODULE_DEPEND(rtw, cardbus, 1, 1, 1);

static int
rtw_pci_probe(device_t dev)
{
	const struct rtw_pci_product *app;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (app = rtw_pci_products; app->app_product_name != NULL; app++) {
		if (vid == app->app_vendor && did == app->app_product) {
			device_set_desc(dev, app->app_product_name);
			return 0;
		}
	}
	return ENXIO;
}

static int
rtw_pci_attach(device_t dev)
{
	struct rtw_softc *sc = device_get_softc(dev);
	struct rtw_regs *regs = &sc->sc_regs;
	int i, error;

	/*
	 * No power management hooks.
	 * XXX Maybe we should add some!
	 */
	sc->sc_flags |= RTW_F_ENABLED;

	sc->sc_rev = pci_get_revid(dev);

#ifndef BURN_BRIDGES
	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		uint32_t mem, port, irq;
						
		mem = pci_read_config(dev, RTW_PCI_MMBA, 4);
		port = pci_read_config(dev, RTW_PCI_IOBA, 4);
		irq = pci_read_config(dev, PCIR_INTLINE, 4);
		
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));

		pci_set_powerstate(dev, PCI_POWERSTATE_D0);

		pci_write_config(dev, RTW_PCI_MMBA, mem, 4);
		pci_write_config(dev, RTW_PCI_IOBA, port, 4);
		pci_write_config(dev, PCIR_INTLINE, irq, 4);
	}
#endif	/* !BURN_BRIDGES */

	/* Enable PCI bus master */
	pci_enable_busmaster(dev);

	/* Allocate IO memory/port */
	for (i = 0; i < NELEM(rtw_pci_regs); ++i) {
		regs->r_rid = rtw_pci_regs[i].reg_rid;
		regs->r_type = rtw_pci_regs[i].reg_type;
		regs->r_res = bus_alloc_resource_any(dev, regs->r_type,
						     &regs->r_rid, RF_ACTIVE);
		if (regs->r_res != NULL)
			break;
	}
	if (regs->r_res == NULL) {
		device_printf(dev, "can't allocate IO mem/port\n");
		return ENXIO;
	}
	regs->r_bh = rman_get_bushandle(regs->r_res);
	regs->r_bt = rman_get_bustag(regs->r_res);

	error = rtw_attach(dev);
	if (error)
		rtw_pci_detach(dev);
	return error;
}

static int
rtw_pci_detach(device_t dev)
{
	struct rtw_softc *sc = device_get_softc(dev);
	struct rtw_regs *regs = &sc->sc_regs;

	if (device_is_attached(dev))
		rtw_detach(dev);

	if (regs->r_res != NULL) {
		bus_release_resource(dev, regs->r_type, regs->r_rid,
				     regs->r_res);
	}
	return 0;
}

static int
rtw_pci_shutdown(device_t dev)
{
	struct rtw_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	lwkt_serialize_enter(ifp->if_serializer);
	rtw_stop(sc, 1);
	lwkt_serialize_exit(ifp->if_serializer);
	return 0;
}
