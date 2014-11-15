/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
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
 * $FreeBSD: src/sys/dev/acpica/acpi_pcib_pci.c,v 1.17.8.1 2009/04/15 03:14:26 kensmith Exp $
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#include "acpi.h"
#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_pcibvar.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcib_private.h>
#include "pcib_if.h"

/* Hooks for the ACPICA debugging infrastructure. */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("PCI_PCI")

struct acpi_pcib_softc {
    struct pcib_softc	ap_pcibsc;
    ACPI_HANDLE		ap_handle;
    ACPI_BUFFER		ap_prt;		/* interrupt routing table */
};

struct acpi_pcib_lookup_info {
    UINT32		address;
    ACPI_HANDLE		handle;
};

static int		acpi_pcib_pci_probe(device_t bus);
static int		acpi_pcib_pci_attach(device_t bus);
static int		acpi_pcib_pci_resume(device_t bus);
static int		acpi_pcib_read_ivar(device_t dev, device_t child,
			    int which, uintptr_t *result);
static int		acpi_pcib_pci_route_interrupt(device_t pcib,
			    device_t dev, int pin);

static device_method_t acpi_pcib_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		acpi_pcib_pci_probe),
    DEVMETHOD(device_attach,		acpi_pcib_pci_attach),
    DEVMETHOD(device_resume,		acpi_pcib_pci_resume),

    /* Bus interface */
    DEVMETHOD(bus_read_ivar,		acpi_pcib_read_ivar),

    /* pcib interface */
    DEVMETHOD(pcib_route_interrupt,	acpi_pcib_pci_route_interrupt),

    DEVMETHOD_END
};

static devclass_t pcib_devclass;

DEFINE_CLASS_1(pcib, acpi_pcib_pci_driver, acpi_pcib_pci_methods,
    sizeof(struct acpi_pcib_softc), pcib_driver);
DRIVER_MODULE(acpi_pcib, pci, acpi_pcib_pci_driver, pcib_devclass, NULL, NULL);
MODULE_DEPEND(acpi_pcib, acpi, 1, 1, 1);

static int
acpi_pcib_pci_probe(device_t dev)
{
    int error;

    if (pci_get_class(dev) != PCIC_BRIDGE ||
	pci_get_subclass(dev) != PCIS_BRIDGE_PCI ||
	acpi_disabled("pci"))
	return (ENXIO);

    error = acpi_pcib_probe(dev);
    if (error)
	return (error);

    device_set_desc(dev, "ACPI PCI-PCI bridge");
    return (-100);
}

static int
acpi_pcib_pci_attach(device_t dev)
{
    struct acpi_pcib_softc *sc;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    pcib_attach_common(dev);
    sc = device_get_softc(dev);
    sc->ap_handle = acpi_get_handle(dev);
    return (acpi_pcib_attach(dev, &sc->ap_prt, sc->ap_pcibsc.secbus));
}

static int
acpi_pcib_pci_resume(device_t dev)
{
    return (acpi_pcib_resume(dev));
}

static int
acpi_pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
    struct acpi_pcib_softc *sc = device_get_softc(dev);

    switch (which) {
    case ACPI_IVAR_HANDLE:
	*result = (uintptr_t)sc->ap_handle;
	return (0);
    }
    return (pcib_read_ivar(dev, child, which, result));
}

static int
acpi_pcib_pci_route_interrupt(device_t pcib, device_t dev, int pin)
{
    struct acpi_pcib_softc *sc = device_get_softc(pcib);

    /*
     * If we don't have a _PRT, fall back to the swizzle method
     * for routing interrupts.
     */
    if (sc->ap_prt.Pointer == NULL) {
	device_printf(pcib, "No _PRT found, routing with pci\n");
	return (pcib_route_interrupt(pcib, dev, pin));
    } else {
	return (acpi_pcib_route_interrupt(pcib, dev, pin, &sc->ap_prt));
    }
}
