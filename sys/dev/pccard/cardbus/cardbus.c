/*-
 * Copyright (c) 2003 M. Warner Losh.  All Rights Reserved.
 * Copyright (c) 2000,2001 Jonathan Chen.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 * $FreeBSD: src/sys/dev/cardbus/cardbus.c,v 1.28 2002/11/27 17:30:41 imp Exp $
 * $FreeBSD @153896,@153900,@159532,@166104 merged
 * $FreeBSD @169620,@169633 merged
 */

/*
 * Cardbus Bus Driver
 *
 * much of the bus code was stolen directly from sys/pci/pci.c
 *   (Copyright (c) 1997, Stefan Esser <se@freebsd.org>)
 *
 * Written by Jonathan Chen <jon@freebsd.org>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/pciio.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pci_private.h>

#include <dev/pccard/cardbus/cardbusreg.h>
#include <dev/pccard/cardbus/cardbusvar.h>
#include <dev/pccard/cardbus/cardbus_cis.h>
#include <bus/pccard/pccard_cis.h>
#include <bus/pccard/pccardvar.h>

#include "power_if.h"
#include "pcib_if.h"

/* sysctl vars */
SYSCTL_NODE(_hw, OID_AUTO, cardbus, CTLFLAG_RD, 0, "CardBus parameters");

int    cardbus_debug = 0;
TUNABLE_INT("hw.cardbus.debug", &cardbus_debug);
SYSCTL_INT(_hw_cardbus, OID_AUTO, debug, CTLFLAG_RW,
    &cardbus_debug, 0,
  "CardBus debug");

int    cardbus_cis_debug = 0;
TUNABLE_INT("hw.cardbus.cis_debug", &cardbus_cis_debug);
SYSCTL_INT(_hw_cardbus, OID_AUTO, cis_debug, CTLFLAG_RW,
    &cardbus_cis_debug, 0,
  "CardBus CIS debug");

#define	DPRINTF(a) if (cardbus_debug) kprintf a
#define	DEVPRINTF(x) if (cardbus_debug) device_printf x


static int	cardbus_attach(device_t cbdev);
static int	cardbus_attach_card(device_t cbdev);
static int	cardbus_detach(device_t cbdev);
static int	cardbus_detach_card(device_t cbdev);
static void	cardbus_device_setup_regs(pcicfgregs *cfg);
static void	cardbus_driver_added(device_t cbdev, driver_t *driver);
static int	cardbus_probe(device_t cbdev);
static int	cardbus_read_ivar(device_t cbdev, device_t child, int which,
		    uintptr_t *result);
static void	cardbus_release_all_resources(device_t cbdev,
		    struct cardbus_devinfo *dinfo);
static int	cardbus_write_ivar(device_t cbdev, device_t child, int which,
		    uintptr_t value);

/************************************************************************/
/* Probe/Attach								*/
/************************************************************************/

static int
cardbus_probe(device_t cbdev)
{
	device_set_desc(cbdev, "CardBus bus");
	return 0;
}

static int
cardbus_attach(device_t cbdev)
{
	return 0;
}

static int
cardbus_detach(device_t cbdev)
{
	cardbus_detach_card(cbdev);
	return 0;
}

static int
cardbus_suspend(device_t self)
{
	cardbus_detach_card(self);
	return (0);
}

static int
cardbus_resume(device_t self)
{
	return (0);
}

/************************************************************************/
/* Attach/Detach card							*/
/************************************************************************/

static void
cardbus_device_setup_regs(pcicfgregs *cfg)
{
	device_t dev = cfg->dev;
	int i;

	/*
	 * Some cards power up with garbage in their BARs.  This
	 * code clears all that junk out.
	 */
	for (i = 0; i < PCIR_MAX_BAR_0; i++)
		pci_write_config(dev, PCIR_BAR(i), 0, 4);

	cfg->intline =
	    pci_get_irq(device_get_parent(device_get_parent(dev)));
	pci_write_config(dev, PCIR_INTLINE, cfg->intline, 1);
	pci_write_config(dev, PCIR_CACHELNSZ, 0x08, 1);
	pci_write_config(dev, PCIR_LATTIMER, 0xa8, 1);
	pci_write_config(dev, PCIR_MINGNT, 0x14, 1);
	pci_write_config(dev, PCIR_MAXLAT, 0x14, 1);
}

static int
cardbus_attach_card(device_t cbdev)
{
	device_t brdev = device_get_parent(cbdev);
	device_t child;
	int cardattached = 0;
	int bus, slot, func, domain;

	cardbus_detach_card(cbdev); /* detach existing cards */
	POWER_ENABLE_SOCKET(brdev, cbdev);
	bus = pcib_get_bus(cbdev);
	domain = pcib_get_domain(cbdev);

	/* For each function, set it up and try to attach a driver to it */
	for (slot = 0; slot <= CARDBUS_SLOTMAX; slot++) {
		int cardbusfunchigh = 0;

		for (func = 0; func <= cardbusfunchigh; func++) {
			struct cardbus_devinfo *dinfo;

			dinfo = (struct cardbus_devinfo *)
			    pci_read_device(brdev, domain, bus, slot, func,
				sizeof(struct cardbus_devinfo));
			if (dinfo == NULL)
				continue;
			if (dinfo->pci.cfg.mfdev)
				cardbusfunchigh = CARDBUS_FUNCMAX;

			child = device_add_child(cbdev, NULL, -1);
			if (child == NULL) {
				DEVPRINTF((cbdev, "Cannot add child!\n"));
				pci_freecfg((struct pci_devinfo *)dinfo);
				continue;
			}
			dinfo->pci.cfg.dev = child;
			resource_list_init(&dinfo->pci.resources);
			device_set_ivars(child, dinfo);
			if (cardbus_do_cis(cbdev, child) != 0) {
				DEVPRINTF((cbdev,
					   "Warning: Bogus CIS ignored\n"));
			}
			pci_cfg_save(dinfo->pci.cfg.dev, &dinfo->pci, 0);
			pci_cfg_restore(dinfo->pci.cfg.dev, &dinfo->pci);
			cardbus_device_setup_regs(&dinfo->pci.cfg);
			pci_add_resources(brdev, cbdev, child, 1,
					  dinfo->mprefetchable);
			pci_print_verbose(&dinfo->pci);
			if (device_probe_and_attach(child) != 0)
				pci_cfg_save(dinfo->pci.cfg.dev, &dinfo->pci, 1);
			else
				cardattached++;
		}
	}

	if (cardattached > 0)
		return (0);
	POWER_DISABLE_SOCKET(brdev, cbdev);
	return (ENOENT);
}

static int
cardbus_detach_card(device_t cbdev)
{
	int numdevs;
	device_t *devlist;
	int tmp;
	int err = 0;

	if (device_get_children(cbdev, &devlist, &numdevs) != 0)
		return (ENOENT);

	if (numdevs == 0) {
		kfree(devlist, M_TEMP);
		return (ENOENT);
	}

	for (tmp = 0; tmp < numdevs; tmp++) {
		struct cardbus_devinfo *dinfo = device_get_ivars(devlist[tmp]);
		int status = device_get_state(devlist[tmp]);

		if (dinfo->pci.cfg.dev != devlist[tmp])
			device_printf(cbdev, "devinfo dev mismatch\n");
		if (status == DS_ATTACHED || status == DS_BUSY)
			device_detach(devlist[tmp]);
		cardbus_release_all_resources(cbdev, dinfo);
		device_delete_child(cbdev, devlist[tmp]);
		pci_freecfg((struct pci_devinfo *)dinfo);
	}
	POWER_DISABLE_SOCKET(device_get_parent(cbdev), cbdev);
	kfree(devlist, M_TEMP);
	return (err);
}

static void
cardbus_driver_added(device_t cbdev, driver_t *driver)
{
	int numdevs;
	device_t *devlist;
	device_t dev;
	int i;
	struct cardbus_devinfo *dinfo;

	DEVICE_IDENTIFY(driver, cbdev);
	if (device_get_children(cbdev, &devlist, &numdevs) != 0)
		return;

	/*
	 * If there are no drivers attached, but there are children,
	 * then power the card up.
	 */
	for (i = 0; i < numdevs; i++) {
		dev = devlist[i];
		if (device_get_state(dev) != DS_NOTPRESENT)
		    break;
	}
	if (i > 0 && i == numdevs)
		POWER_ENABLE_SOCKET(device_get_parent(cbdev), cbdev);
	for (i = 0; i < numdevs; i++) {
		dev = devlist[i];
		if (device_get_state(dev) != DS_NOTPRESENT)
			continue;
		dinfo = device_get_ivars(dev);
		pci_print_verbose(&dinfo->pci);
		if (bootverbose)
			kprintf("pci%d:%d:%d:%d: reprobing on driver added\n",
			    dinfo->pci.cfg.domain, dinfo->pci.cfg.bus,
			    dinfo->pci.cfg.slot, dinfo->pci.cfg.func);
		pci_cfg_restore(dinfo->pci.cfg.dev, &dinfo->pci);
		if (device_probe_and_attach(dev) != 0)
			pci_cfg_save(dev, &dinfo->pci, 1);
	}
	kfree(devlist, M_TEMP);
}

static void
cardbus_release_all_resources(device_t cbdev, struct cardbus_devinfo *dinfo)
{
	struct resource_list_entry *rle;

	/* Free all allocated resources */
	SLIST_FOREACH(rle, &dinfo->pci.resources, link) {
		if (rle->res) {
			BUS_RELEASE_RESOURCE(device_get_parent(cbdev),
			    cbdev, rle->type, rle->rid, rle->res);
			rle->res = NULL;
			/*
			 * zero out config so the card won't acknowledge
			 * access to the space anymore
			 */
			pci_write_config(dinfo->pci.cfg.dev, rle->rid, 0, 4);
		}
	}
	resource_list_free(&dinfo->pci.resources);
}

/************************************************************************/
/* Other Bus Methods							*/
/************************************************************************/

static int
cardbus_read_ivar(device_t cbdev, device_t child, int which, uintptr_t *result)
{
	struct cardbus_devinfo *dinfo;
	pcicfgregs *cfg;

	dinfo = device_get_ivars(child);
	cfg = &dinfo->pci.cfg;

	switch (which) {
	case PCI_IVAR_ETHADDR:
		/*
		 * The generic accessor doesn't deal with failure, so
		 * we set the return value, then return an error.
		 */
		if (dinfo->fepresent & (1 << PCCARD_TPLFE_TYPE_LAN_NID)) {
			*((uint8_t **) result) = dinfo->funce.lan.nid;
			break;
		}
		*((uint8_t **) result) = NULL;
		return (EINVAL);
	default:
		return (pci_read_ivar(cbdev, child, which, result));
	}
	return 0;
}

static int
cardbus_write_ivar(device_t cbdev, device_t child, int which, uintptr_t value)
{
	return(pci_write_ivar(cbdev, child, which, value));
}

static device_method_t cardbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cardbus_probe),
	DEVMETHOD(device_attach,	cardbus_attach),
	DEVMETHOD(device_detach,	cardbus_detach),
	DEVMETHOD(device_suspend,	cardbus_suspend),
	DEVMETHOD(device_resume,	cardbus_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	cardbus_read_ivar),
	DEVMETHOD(bus_write_ivar,	cardbus_write_ivar),
	DEVMETHOD(bus_driver_added,	cardbus_driver_added),

	/* Card Interface */
	DEVMETHOD(card_attach_card,	cardbus_attach_card),
	DEVMETHOD(card_detach_card,	cardbus_detach_card),

	{0,0}
};

DEFINE_CLASS_1(cardbus, cardbus_driver, cardbus_methods, 0, pci_driver);

static devclass_t cardbus_devclass;

DRIVER_MODULE(cardbus, cbb, cardbus_driver, cardbus_devclass, NULL, NULL);
MODULE_VERSION(cardbus, 1);
