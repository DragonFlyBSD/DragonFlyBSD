/*
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/ath/if_ath_pci.c,v 1.12 2005/03/05 19:06:12 imp Exp $
 * $DragonFly: src/sys/dev/netif/ath/ath/if_ath_pci.c,v 1.1 2006/07/13 09:15:22 sephe Exp $
 */

/*
 * PCI/Cardbus front-end for the Atheros Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <netproto/802_11/ieee80211_var.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>

#include <dev/netif/ath/ath/if_athvar.h>
#include <contrib/dev/ath/ah.h>

static int	ath_pci_probe(device_t);
static int	ath_pci_attach(device_t);
static int	ath_pci_detach(device_t);
static int	ath_pci_shutdown(device_t);
static int	ath_pci_suspend(device_t);
static int	ath_pci_resume(device_t);

static void	ath_pci_setup(device_t);

/*
 * PCI glue.
 */

struct ath_pci_softc {
	struct ath_softc	sc_sc;
	struct resource		*sc_sr;		/* memory resource */
	int			sc_srid;
};

#define	BS_BAR	0x10
#define	PCIR_RETRY_TIMEOUT	0x41

static int
ath_pci_probe(device_t dev)
{
	const char* devname;

	devname = ath_hal_probe(pci_get_vendor(dev), pci_get_device(dev));
	if (devname != NULL) {
		device_set_desc(dev, devname);
		return 0;
	}
	return ENXIO;
}

static void
ath_pci_setup(device_t dev)
{
	/*
	 * Enable memory mapping and bus mastering.
	 */
	pci_enable_busmaster(dev);

	/*
	 * Disable retry timeout to keep PCI Tx retries from
	 * interfering with C3 CPU state.
	 */
	pci_write_config(dev, PCIR_RETRY_TIMEOUT, 0, 1);
}

static int
ath_pci_attach(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;
	int error;

	sc->sc_dev = dev;

	ath_pci_setup(dev);

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	psc->sc_srid = BS_BAR;
	psc->sc_sr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &psc->sc_srid,
					    RF_ACTIVE);
	if (psc->sc_sr == NULL) {
		device_printf(dev, "cannot map register space\n");
		return ENXIO;
	}
	sc->sc_st = rman_get_bustag(psc->sc_sr);
	sc->sc_sh = rman_get_bushandle(psc->sc_sr);

	/*
	 * Setup DMA descriptor area.
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
				   NULL, NULL, 0x3ffff/* maxsize XXX */,
				   ATH_MAX_SCATTER, BUS_SPACE_MAXADDR,
				   BUS_DMA_ALLOCNOW, &sc->sc_dmat);
	if (error) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto back;
	}

	error = ath_attach(pci_get_device(dev), sc);

back:
	if (error)
		ath_pci_detach(dev);
	return error;
}

static int
ath_pci_detach(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;

	/* XXX check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	if (device_is_attached(dev))
		ath_detach(sc);

	bus_generic_detach(dev);

	if (sc->sc_dmat != NULL)
		bus_dma_tag_destroy(sc->sc_dmat);

	if (psc->sc_sr != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, psc->sc_srid,
				     psc->sc_sr);
	}
	return 0;
}

static int
ath_pci_shutdown(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	ath_shutdown(&psc->sc_sc);
	return (0);
}

static int
ath_pci_suspend(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	ath_suspend(&psc->sc_sc);
	return (0);
}

static int
ath_pci_resume(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	ath_pci_setup(dev);
	ath_resume(&psc->sc_sc);
	return (0);
}

static device_method_t ath_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ath_pci_probe),
	DEVMETHOD(device_attach,	ath_pci_attach),
	DEVMETHOD(device_detach,	ath_pci_detach),
	DEVMETHOD(device_shutdown,	ath_pci_shutdown),
	DEVMETHOD(device_suspend,	ath_pci_suspend),
	DEVMETHOD(device_resume,	ath_pci_resume),

	{ 0, 0 }
};

static driver_t ath_pci_driver = {
	"ath",
	ath_pci_methods,
	sizeof (struct ath_pci_softc)
};

static	devclass_t ath_devclass;

DRIVER_MODULE(if_ath, pci, ath_pci_driver, ath_devclass, 0, 0);
DRIVER_MODULE(if_ath, cardbus, ath_pci_driver, ath_devclass, 0, 0);

MODULE_VERSION(if_ath, 1);

MODULE_DEPEND(if_ath, ath_hal, 1, 1, 1);	/* Atheros HAL */
MODULE_DEPEND(if_ath, ath_rate, 1, 1, 1);	/* rate control algorithm */
MODULE_DEPEND(if_ath, wlan, 1, 1, 1);		/* 802.11 media layer */
