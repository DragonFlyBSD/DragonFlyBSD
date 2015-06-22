/*-
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 */

/* $FreeBSD: head/sys/dev/usb/controller/xhci_pci.c 276717 2015-01-05 20:22:18Z hselasky $ */

#include <sys/stdint.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <bus/u4b/usb.h>
#include <bus/u4b/usbdi.h>

#include <bus/u4b/usb_core.h>
#include <bus/u4b/usb_busdma.h>
#include <bus/u4b/usb_process.h>
#include <bus/u4b/usb_util.h>

#include <bus/u4b/usb_controller.h>
#include <bus/u4b/usb_bus.h>
#include <bus/u4b/usb_pci.h>
#include <bus/u4b/controller/xhci.h>
#include <bus/u4b/controller/xhcireg.h>
#include "usb_if.h"

static device_probe_t xhci_pci_probe;
static device_attach_t xhci_pci_attach;
static device_detach_t xhci_pci_detach;
static usb_take_controller_t xhci_pci_take_controller;

static device_method_t xhci_device_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe, xhci_pci_probe),
	DEVMETHOD(device_attach, xhci_pci_attach),
	DEVMETHOD(device_detach, xhci_pci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	DEVMETHOD(usb_take_controller, xhci_pci_take_controller),

	DEVMETHOD_END
};

static driver_t xhci_driver = {
	.name = "xhci",
	.methods = xhci_device_methods,
	.size = sizeof(struct xhci_softc),
};

static devclass_t xhci_devclass;

DRIVER_MODULE(xhci, pci, xhci_driver, xhci_devclass, NULL, NULL);
MODULE_DEPEND(xhci, usb, 1, 1, 1);


static const char *
xhci_pci_match(device_t self)
{
	uint32_t device_id = pci_get_devid(self);

	switch (device_id) {
	case 0x01941033:
		return ("NEC uPD720200 USB 3.0 controller");

	case 0x10421b21:
		return ("ASMedia ASM1042 USB 3.0 controller");

	case 0x0f358086:
		return ("Intel Intel BayTrail USB 3.0 controller");
	case 0x9c318086:
	case 0x1e318086:
		return ("Intel Panther Point USB 3.0 controller");
	case 0x8c318086:
		return ("Intel Lynx Point USB 3.0 controller");
	case 0x8cb18086:
		return ("Intel Wildcat Point USB 3.0 controller");
	case 0x9cb18086:
		return ("Intel Wildcat Point-LB USB 3.0 controller");
	default:
		break;
	}

	if ((pci_get_class(self) == PCIC_SERIALBUS)
	    && (pci_get_subclass(self) == PCIS_SERIALBUS_USB)
	    && (pci_get_progif(self) == PCIP_SERIALBUS_USB_XHCI)) {
		return ("XHCI (generic) USB 3.0 controller");
	}
	return (NULL);			/* dunno */
}

static int
xhci_pci_probe(device_t self)
{
	const char *desc = xhci_pci_match(self);

	if (desc) {
		device_set_desc(self, desc);
		return (0);
	} else {
		return (ENXIO);
	}
}

static int xhci_use_msi = 1;
TUNABLE_INT("hw.usb.xhci.msi", &xhci_use_msi);

static void
xhci_interrupt_poll(void *_sc)
{
	struct xhci_softc *sc = _sc;
	USB_BUS_UNLOCK(&sc->sc_bus);
	xhci_interrupt(sc);
	USB_BUS_LOCK(&sc->sc_bus);
	usb_callout_reset(&sc->sc_callout, 1, (void *)&xhci_interrupt_poll, sc);
}

static int
xhci_pci_port_route(device_t self, uint32_t set, uint32_t clear)
{
	uint32_t temp;
	uint32_t usb3_mask;
	uint32_t usb2_mask;

	temp = pci_read_config(self, PCI_XHCI_INTEL_USB3_PSSEN, 4) |
	    pci_read_config(self, PCI_XHCI_INTEL_XUSB2PR, 4);

	temp |= set;
	temp &= ~clear;

	/* Don't set bits which the hardware doesn't support */
	usb3_mask = pci_read_config(self, PCI_XHCI_INTEL_USB3PRM, 4);
	usb2_mask = pci_read_config(self, PCI_XHCI_INTEL_USB2PRM, 4);

	pci_write_config(self, PCI_XHCI_INTEL_USB3_PSSEN, temp & usb3_mask, 4);
	pci_write_config(self, PCI_XHCI_INTEL_XUSB2PR, temp & usb2_mask, 4);

	device_printf(self, "Port routing mask set to 0x%08x\n", temp);

	return (0);
}

static int
xhci_pci_attach(device_t self)
{
	struct xhci_softc *sc = device_get_softc(self);
	int count, err, rid;
	uint8_t usedma32;

	rid = PCI_XHCI_CBMEM;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(self, "Could not map memory\n");
		return (ENOMEM);
	}
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	/* check for USB 3.0 controllers which don't support 64-bit DMA */
	switch (pci_get_devid(self)) {
	case 0x01941033:	/* NEC uPD720200 USB 3.0 controller */
		usedma32 = 1;
		break;
	default:
		usedma32 = 0;
		break;
	}

	if (xhci_init(sc, self, usedma32)) {
		device_printf(self, "Could not initialize softc\n");
		bus_release_resource(self, SYS_RES_MEMORY, PCI_XHCI_CBMEM,
		    sc->sc_io_res);
		return (ENXIO);
	}

	pci_enable_busmaster(self);

	usb_callout_init_mtx(&sc->sc_callout, &sc->sc_bus.bus_lock, 0);

	rid = 0;
	if (xhci_use_msi) {
		count = pci_msi_count(self);
		if (count >= 1) {
			count = 1;
			if (pci_alloc_msi(self, &rid, 1, count) == 0) {
				if (bootverbose)
					device_printf(self, "MSI enabled\n");
				sc->sc_irq_rid = 1;
			}
		}
	}
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ,
	    &sc->sc_irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		pci_release_msi(self);
		device_printf(self, "Could not allocate IRQ\n");
		/* goto error; FALLTHROUGH - use polling */
	}
	sc->sc_bus.bdev = device_add_child(self, "usbus", -1);
	if (sc->sc_bus.bdev == NULL) {
		device_printf(self, "Could not add USB device\n");
		goto error;
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);

	ksprintf(sc->sc_vendor, "0x%04x", pci_get_vendor(self));

	if (sc->sc_irq_res != NULL) {
		err = bus_setup_intr(self, sc->sc_irq_res, INTR_MPSAFE,
		    (driver_intr_t *)xhci_interrupt, sc, &sc->sc_intr_hdl, NULL);
		if (err != 0) {
			bus_release_resource(self, SYS_RES_IRQ,
			    rman_get_rid(sc->sc_irq_res), sc->sc_irq_res);
			sc->sc_irq_res = NULL;
			pci_release_msi(self);
			device_printf(self, "Could not setup IRQ, err=%d\n", err);
			sc->sc_intr_hdl = NULL;
		}
	}
	if (sc->sc_irq_res == NULL || sc->sc_intr_hdl == NULL) {
		if (xhci_use_polling() != 0) {
			device_printf(self, "Interrupt polling at %dHz\n", hz);
			USB_BUS_LOCK(&sc->sc_bus);
			xhci_interrupt_poll(sc);
			USB_BUS_UNLOCK(&sc->sc_bus);
		} else
			goto error;
	}

	/* On Intel chipsets reroute ports from EHCI to XHCI controller. */
	switch (pci_get_devid(self)) {
	case 0x0f358086:	/* BayTrail */
	case 0x9c318086:	/* Panther Point */
	case 0x1e318086:	/* Panther Point */
	case 0x8c318086:	/* Lynx Point */
	case 0x8cb18086:	/* Wildcat Point */
	case 0x9cb18086:	/* Wildcat Point-LP */
		sc->sc_port_route = &xhci_pci_port_route;
		sc->sc_imod_default = XHCI_IMOD_DEFAULT_LP;
		break;
	default:
		break;
	}

	xhci_pci_take_controller(self);

	err = xhci_halt_controller(sc);

	if (err == 0)
		err = xhci_start_controller(sc);

	if (err == 0)
		err = device_probe_and_attach(sc->sc_bus.bdev);

	if (err) {
		device_printf(self, "XHCI halt/start/probe failed err=%d\n", err);
		goto error;
	}
	return (0);

error:
	xhci_pci_detach(self);
	return (ENXIO);
}

static int
xhci_pci_detach(device_t self)
{
	struct xhci_softc *sc = device_get_softc(self);
	device_t bdev;

	if (sc->sc_bus.bdev != NULL) {
		bdev = sc->sc_bus.bdev;
		device_detach(bdev);
		device_delete_child(self, bdev);
	}
	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	usb_callout_drain(&sc->sc_callout);
	xhci_halt_controller(sc);

	pci_disable_busmaster(self);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);
		sc->sc_intr_hdl = NULL;
	}
	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ,
		    rman_get_rid(sc->sc_irq_res), sc->sc_irq_res);
		sc->sc_irq_res = NULL;
		pci_release_msi(self);
	}
	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, PCI_XHCI_CBMEM,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}

	xhci_uninit(sc);

	return (0);
}

static int
xhci_pci_take_controller(device_t self)
{
	struct xhci_softc *sc = device_get_softc(self);
	uint32_t cparams;
	uint32_t eecp;
	uint32_t eec;
	uint16_t to;
	uint8_t bios_sem;

	cparams = XREAD4(sc, capa, XHCI_HCSPARAMS0);

	eec = -1;

	/* Synchronise with the BIOS if it owns the controller. */
	for (eecp = XHCI_HCS0_XECP(cparams) << 2; eecp != 0 && XHCI_XECP_NEXT(eec);
	    eecp += XHCI_XECP_NEXT(eec) << 2) {
		eec = XREAD4(sc, capa, eecp);

		if (XHCI_XECP_ID(eec) != XHCI_ID_USB_LEGACY)
			continue;
		bios_sem = XREAD1(sc, capa, eecp +
		    XHCI_XECP_BIOS_SEM);
		if (bios_sem == 0)
			continue;
		device_printf(sc->sc_bus.bdev, "waiting for BIOS "
		    "to give up control\n");
		XWRITE1(sc, capa, eecp +
		    XHCI_XECP_OS_SEM, 1);
		to = 500;
		while (1) {
			bios_sem = XREAD1(sc, capa, eecp +
			    XHCI_XECP_BIOS_SEM);
			if (bios_sem == 0)
				break;

			if (--to == 0) {
				device_printf(sc->sc_bus.bdev,
				    "timed out waiting for BIOS\n");
				break;
			}
			usb_pause_mtx(NULL, hz / 100);	/* wait 10ms */
		}
	}
	return (0);
}
