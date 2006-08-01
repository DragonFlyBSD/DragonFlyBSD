/*
 * Copyright (c) 2005, 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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
 *
 * $FreeBSD: src/sys/dev/ral/if_ral_pci.c,v 1.4 2006/03/05 23:27:51 silby Exp $
 * $DragonFly: src/sys/dev/netif/ral/if_ral_pci.c,v 1.2 2006/08/01 18:06:44 swildner Exp $
 */

/*
 * PCI/Cardbus front-end for the Ralink RT2560/RT2561/RT2561S/RT2661 driver.
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_radiotap.h>

#include <bus/pci/pcidevs.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/netif/ral/if_ralrate.h>
#include <dev/netif/ral/rt2560var.h>
#include <dev/netif/ral/rt2661var.h>

struct ral_opns {
	int	(*attach)(device_t, int);
	int	(*detach)(void *);
	void	(*shutdown)(void *);
	void	(*suspend)(void *);
	void	(*resume)(void *);
};

static const struct ral_opns ral_rt2560_opns = {
	.attach		= rt2560_attach,
	.detach		= rt2560_detach,
	.shutdown	= rt2560_shutdown,
	.suspend	= rt2560_suspend,
	.resume		= rt2560_resume,
};

static const struct ral_opns ral_rt2661_opns = {
	.attach		= rt2661_attach,
	.detach		= rt2661_detach,
	.shutdown	= rt2661_shutdown,
	.suspend	= rt2661_suspend,
	.resume		= rt2661_resume,
};

static const struct ral_pci_ident {
	uint16_t		vendor;
	uint16_t		device;
	const char		*name;
	const struct ral_opns	*opns;
} ral_pci_ids[] = {
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2560,
		"Ralink Technology RT2560", &ral_rt2560_opns },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2561S,
		"Ralink Technology RT2561S", &ral_rt2661_opns },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2561,
		"Ralink Technology RT2561", &ral_rt2661_opns },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2661,
		"Ralink Technology RT2661", &ral_rt2661_opns },
	{ 0, 0, NULL, NULL }
};

struct ral_pci_softc {
	/* XXX MUST be the first field */
	union {
		struct rt2560_softc sc_rt2560;
		struct rt2661_softc sc_rt2661;
	} u;

	const struct ral_opns	*sc_opns;
	int			mem_rid;
	struct resource		*mem;
};

static int ral_pci_probe(device_t);
static int ral_pci_attach(device_t);
static int ral_pci_detach(device_t);
static int ral_pci_shutdown(device_t);
static int ral_pci_suspend(device_t);
static int ral_pci_resume(device_t);

static device_method_t ral_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ral_pci_probe),
	DEVMETHOD(device_attach,	ral_pci_attach),
	DEVMETHOD(device_detach,	ral_pci_detach),
	DEVMETHOD(device_shutdown,	ral_pci_shutdown),
	DEVMETHOD(device_suspend,	ral_pci_suspend),
	DEVMETHOD(device_resume,	ral_pci_resume),

	{ 0, 0 }
};

static driver_t ral_pci_driver = {
	"ral",
	ral_pci_methods,
	sizeof (struct ral_pci_softc)
};

static devclass_t ral_devclass;

DRIVER_MODULE(ral, pci, ral_pci_driver, ral_devclass, 0, 0);
DRIVER_MODULE(ral, cardbus, ral_pci_driver, ral_devclass, 0, 0);

MODULE_DEPEND(ral, wlan, 1, 1, 1);
MODULE_DEPEND(ral, pci, 1, 1, 1);
MODULE_DEPEND(ral, cardbus, 1, 1, 1);

static int
ral_pci_probe(device_t dev)
{
	const struct ral_pci_ident *ident;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	for (ident = ral_pci_ids; ident->name != NULL; ident++) {
		if (vid == ident->vendor && did == ident->device) {
			struct ral_pci_softc *psc = device_get_softc(dev);

			psc->sc_opns = ident->opns;
			device_set_desc(dev, ident->name);
			return 0;
		}
	}
	return ENXIO;
}

/* Base Address Register */
#define RAL_PCI_BAR0	0x10

static int
ral_pci_attach(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);
	struct rt2560_softc *sc = &psc->u.sc_rt2560;
	int error;

	/* Assign `dev' earlier, so that we can do possible error clean up */
	sc->sc_dev = dev;

	if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
		device_printf(dev, "chip is in D%d power mode "
		    "-- setting to D0\n", pci_get_powerstate(dev));
		pci_set_powerstate(dev, PCI_POWERSTATE_D0);
	}

	/* enable bus-mastering */
	pci_enable_busmaster(dev);

	psc->mem_rid = RAL_PCI_BAR0;
	psc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &psc->mem_rid,
					  RF_ACTIVE);
	if (psc->mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return ENXIO;
	}
	sc->sc_st = rman_get_bustag(psc->mem);
	sc->sc_sh = rman_get_bushandle(psc->mem);

	error = psc->sc_opns->attach(dev, pci_get_device(dev));
	if (error != 0)
		ral_pci_detach(dev);

	return error;
}

static int
ral_pci_detach(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	if (device_is_attached(dev))
		psc->sc_opns->detach(psc);

	bus_generic_detach(dev);

	if (psc->mem != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, psc->mem_rid,
				     psc->mem);
	}
	return 0;
}

static int
ral_pci_shutdown(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	psc->sc_opns->shutdown(psc);
	return 0;
}

static int
ral_pci_suspend(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	psc->sc_opns->suspend(psc);
	return 0;
}

static int
ral_pci_resume(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	psc->sc_opns->resume(psc);
	return 0;
}
