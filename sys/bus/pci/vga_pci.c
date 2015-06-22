/*-
 * Copyright (c) 2005 John Baldwin <jhb@FreeBSD.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/dev/pci/vga_pci.c,v 1.5.8.1 2009/04/15 03:14:26 kensmith Exp $
 */

/*
 * Simple driver for PCI VGA display devices.  Drivers such as agp(4) and
 * drm(4) should attach as children of this device.
 *
 * XXX: The vgapci name is a hack until we somehow merge the isa vga driver
 * in or rename it.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/pmap.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

struct vga_resource {
	struct resource	*vr_res;
	int	vr_refs;
};

struct vga_pci_softc {
	device_t	vga_msi_child;	/* Child driver using MSI. */
	struct vga_resource vga_bars[PCIR_MAX_BAR_0 + 1];
	struct vga_resource vga_bios;
};

SYSCTL_DECL(_hw_pci);

static struct vga_resource *lookup_res(struct vga_pci_softc *sc, int rid);
static struct resource *vga_pci_alloc_resource(device_t dev, device_t child,
    int type, int *rid, u_long start, u_long end, u_long count, u_int flags,
    int cpuid);
static int	vga_pci_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r);

int vga_pci_default_unit = -1;
TUNABLE_INT("hw.pci.default_vgapci_unit", &vga_pci_default_unit);
SYSCTL_INT(_hw_pci, OID_AUTO, default_vgapci_unit, CTLFLAG_RD,
    &vga_pci_default_unit, -1, "Default VGA-compatible display");

int
vga_pci_is_boot_display(device_t dev)
{
	int unit;
	device_t pcib;
	uint16_t config;

	/* Check that the given device is a video card */
	if ((pci_get_class(dev) != PCIC_DISPLAY &&
	    (pci_get_class(dev) != PCIC_OLD ||
	     pci_get_subclass(dev) != PCIS_OLD_VGA)))
		return (0);

	unit = device_get_unit(dev);

	if (vga_pci_default_unit >= 0) {
		/*
		 * The boot display device was determined by a previous
		 * call to this function, or the user forced it using
		 * the hw.pci.default_vgapci_unit tunable.
		 */
		return (vga_pci_default_unit == unit);
	}

	/*
	 * The primary video card used as a boot display must have the
	 * "I/O" and "Memory Address Space Decoding" bits set in its
	 * Command register.
	 *
	 * Furthermore, if the card is attached to a bridge, instead of
	 * the root PCI bus, the bridge must have the "VGA Enable" bit
	 * set in its Control register.
	 */

	pcib = device_get_parent(device_get_parent(dev));
	if (device_get_devclass(device_get_parent(pcib)) ==
	    devclass_find("pci")) {
		/*
		 * The parent bridge is a PCI-to-PCI bridge: check the
		 * value of the "VGA Enable" bit.
		 */
		config = pci_read_config(pcib, PCIR_BRIDGECTL_1, 2);
		if ((config & PCIB_BCR_VGA_ENABLE) == 0)
			return (0);
	}

	config = pci_read_config(dev, PCIR_COMMAND, 2);
	if ((config & (PCIM_CMD_PORTEN | PCIM_CMD_MEMEN)) == 0)
		return (0);

	/* This video card is the boot display: record its unit number. */
	vga_pci_default_unit = unit;
	device_set_flags(dev, 1);

	return (1);
}

void *
vga_pci_map_bios(device_t dev, size_t *size)
{
	int rid;
	struct resource *res;

	if (vga_pci_is_boot_display(dev)) {
		/*
		 * On x86, the System BIOS copy the default display
		 * device's Video BIOS at a fixed location in system
		 * memory (0xC0000, 128 kBytes long) at boot time.
		 *
		 * We use this copy for the default boot device, because
		 * the original ROM may not be valid after boot.
		 */

		*size = VGA_PCI_BIOS_SHADOW_SIZE;
		return (pmap_mapbios(VGA_PCI_BIOS_SHADOW_ADDR, *size));
	}

	rid = PCIR_BIOS;
	res = vga_pci_alloc_resource(dev, NULL, SYS_RES_MEMORY, &rid, 0ul,
	    ~0ul, 1, RF_ACTIVE, -1);
	if (res == NULL) {
		return (NULL);
	}

	*size = rman_get_size(res);
	return (rman_get_virtual(res));
}

void
vga_pci_unmap_bios(device_t dev, void *bios)
{
	struct vga_resource *vr;

	if (bios == NULL) {
		return;
	}

	if (vga_pci_is_boot_display(dev)) {
		/* We mapped the BIOS shadow copy located at 0xC0000. */
		pmap_unmapdev((vm_offset_t)bios, VGA_PCI_BIOS_SHADOW_SIZE);

		return;
	}

	/*
	 * Look up the PCIR_BIOS resource in our softc.  It should match
	 * the address we returned previously.
	 */
	vr = lookup_res(device_get_softc(dev), PCIR_BIOS);
	KASSERT(vr->vr_res != NULL, ("vga_pci_unmap_bios: bios not mapped"));
	KASSERT(rman_get_virtual(vr->vr_res) == bios,
	    ("vga_pci_unmap_bios: mismatch"));
	vga_pci_release_resource(dev, NULL, SYS_RES_MEMORY, PCIR_BIOS,
	    vr->vr_res);
}

static int
vga_pci_probe(device_t dev)
{

	switch (pci_get_class(dev)) {
	case PCIC_DISPLAY:
		break;
	case PCIC_OLD:
		if (pci_get_subclass(dev) != PCIS_OLD_VGA)
			return (ENXIO);
		break;
	default:
		return (ENXIO);
	}

	/* Probe default display. */
	vga_pci_is_boot_display(dev);

	device_set_desc(dev, "VGA-compatible display");
	return (BUS_PROBE_GENERIC);
}

static int
vga_pci_attach(device_t dev)
{

	bus_generic_probe(dev);

	/* Always create a drm child for now to make it easier on drm. */
	device_add_child(dev, "drm", -1);
	device_add_child(dev, "drmn", -1);
	bus_generic_attach(dev);

	if (vga_pci_is_boot_display(dev))
		device_printf(dev, "Boot video device\n");

	return (0);
}

static int
vga_pci_suspend(device_t dev)
{

	return (bus_generic_suspend(dev));
}

static int
vga_pci_resume(device_t dev)
{

	return (bus_generic_resume(dev));
}

/* Bus interface. */

static int
vga_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{

	return (BUS_READ_IVAR(device_get_parent(dev), dev, which, result));
}

static int
vga_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{

	return (EINVAL);
}

static int
vga_pci_setup_intr(device_t dev, device_t child, struct resource *irq,
    int flags, driver_intr_t *intr, void *arg, void **cookiep,
    lwkt_serialize_t serializer, const char *desc)
{

	return (BUS_SETUP_INTR(device_get_parent(dev), dev, irq, flags,
	    intr, arg, cookiep, serializer, desc));
}

static int
vga_pci_teardown_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie)
{

	return (BUS_TEARDOWN_INTR(device_get_parent(dev), dev, irq, cookie));
}

static struct vga_resource *
lookup_res(struct vga_pci_softc *sc, int rid)
{
	int bar;

	if (rid == PCIR_BIOS)
		return (&sc->vga_bios);
	bar = PCI_RID2BAR(rid);
	if (bar >= 0 && bar <= PCIR_MAX_BAR_0)
		return (&sc->vga_bars[bar]);
	return (NULL);
}

static struct resource *
vga_pci_alloc_resource(device_t dev, device_t child, int type, int *rid,
    u_long start, u_long end, u_long count, u_int flags, int cpuid __unused)
{
	struct vga_resource *vr;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		/*
		 * For BARs, we cache the resource so that we only allocate it
		 * from the PCI bus once.
		 */
		vr = lookup_res(device_get_softc(dev), *rid);
		if (vr == NULL)
			return (NULL);
		if (vr->vr_res == NULL)
			vr->vr_res = bus_alloc_resource(dev, type, rid, start,
			    end, count, flags);
		if (vr->vr_res != NULL)
			vr->vr_refs++;
		return (vr->vr_res);
	}
	return (bus_alloc_resource(dev, type, rid, start, end, count, flags));
}

static int
vga_pci_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	struct vga_resource *vr;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		/*
		 * Stop caching the resource when refs drops to 0
		 */
		vr = lookup_res(device_get_softc(dev), rid);
		if (vr && vr->vr_refs > 0) {
			if (--vr->vr_refs > 0)
				return(0);
			vr->vr_res = NULL;
			/* fall through */
		}
		/* fall through */
		break;
	}
	return (bus_release_resource(dev, type, rid, r));
}

/* PCI interface. */

static uint32_t
vga_pci_read_config(device_t dev, device_t child, int reg, int width)
{

	return (pci_read_config(dev, reg, width));
}

static void
vga_pci_write_config(device_t dev, device_t child, int reg,
    uint32_t val, int width)
{

	pci_write_config(dev, reg, val, width);
}

static int
vga_pci_enable_busmaster(device_t dev, device_t child)
{

	return (pci_enable_busmaster(dev));
}

static int
vga_pci_disable_busmaster(device_t dev, device_t child)
{

	return (pci_disable_busmaster(dev));
}

static int
vga_pci_enable_io(device_t dev, device_t child, int space)
{

	device_printf(dev, "child %s requested pci_enable_io\n",
	    device_get_nameunit(child));
	return (pci_enable_io(dev, space));
}

static int
vga_pci_disable_io(device_t dev, device_t child, int space)
{

	device_printf(dev, "child %s requested pci_disable_io\n",
	    device_get_nameunit(child));
	return (pci_disable_io(dev, space));
}

static int
vga_pci_get_vpd_ident(device_t dev, device_t child, const char **identptr)
{

	return (pci_get_vpd_ident(dev, identptr));
}

static int
vga_pci_get_vpd_readonly(device_t dev, device_t child, const char *kw,
    const char **vptr)
{

	return (pci_get_vpd_readonly(dev, kw, vptr));
}

static int
vga_pci_set_powerstate(device_t dev, device_t child, int state)
{

	device_printf(dev, "child %s requested pci_set_powerstate\n",
	    device_get_nameunit(child));
	return (pci_set_powerstate(dev, state));
}

static int
vga_pci_get_powerstate(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_get_powerstate\n",
	    device_get_nameunit(child));
	return (pci_get_powerstate(dev));
}

static int
vga_pci_assign_interrupt(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_assign_interrupt\n",
	    device_get_nameunit(child));
	return (PCI_ASSIGN_INTERRUPT(device_get_parent(dev), dev));
}

static int
vga_pci_find_extcap(device_t dev, device_t child, int capability,
    int *capreg)
{

	return (pci_find_extcap(dev, capability, capreg));
}

static int
vga_pci_alloc_msi(device_t dev, device_t child, int *rid, int count,
    int cpuid)
{
	struct vga_pci_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (sc->vga_msi_child != NULL)
		return (EBUSY);
	error = pci_alloc_msi(dev, rid, count, cpuid);
	if (error == 0)
		sc->vga_msi_child = child;
	return (error);
}

static int
vga_pci_release_msi(device_t dev, device_t child)
{
	struct vga_pci_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (sc->vga_msi_child != child)
		return (ENXIO);
	error = pci_release_msi(dev);
	if (error == 0)
		sc->vga_msi_child = NULL;
	return (error);
}

static int
vga_pci_msi_count(device_t dev, device_t child)
{

	return (pci_msi_count(dev));
}

static device_method_t vga_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vga_pci_probe),
	DEVMETHOD(device_attach,	vga_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	vga_pci_suspend),
	DEVMETHOD(device_resume,	vga_pci_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	vga_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	vga_pci_write_ivar),
	DEVMETHOD(bus_setup_intr,	vga_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	vga_pci_teardown_intr),

	DEVMETHOD(bus_alloc_resource,	vga_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	vga_pci_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),

	/* PCI interface */
	DEVMETHOD(pci_read_config,	vga_pci_read_config),
	DEVMETHOD(pci_write_config,	vga_pci_write_config),
	DEVMETHOD(pci_enable_busmaster,	vga_pci_enable_busmaster),
	DEVMETHOD(pci_disable_busmaster, vga_pci_disable_busmaster),
	DEVMETHOD(pci_enable_io,	vga_pci_enable_io),
	DEVMETHOD(pci_disable_io,	vga_pci_disable_io),
	DEVMETHOD(pci_get_vpd_ident,	vga_pci_get_vpd_ident),
	DEVMETHOD(pci_get_vpd_readonly,	vga_pci_get_vpd_readonly),
	DEVMETHOD(pci_get_powerstate,	vga_pci_get_powerstate),
	DEVMETHOD(pci_set_powerstate,	vga_pci_set_powerstate),
	DEVMETHOD(pci_assign_interrupt,	vga_pci_assign_interrupt),
	DEVMETHOD(pci_find_extcap,	vga_pci_find_extcap),
	DEVMETHOD(pci_alloc_msi,	vga_pci_alloc_msi),
	DEVMETHOD(pci_release_msi,	vga_pci_release_msi),
	DEVMETHOD(pci_msi_count,	vga_pci_msi_count),

	DEVMETHOD_END
};

static driver_t vga_pci_driver = {
	"vgapci",
	vga_pci_methods,
	sizeof(struct vga_pci_softc),
};

static devclass_t vga_devclass;

DRIVER_MODULE(vgapci, pci, vga_pci_driver, vga_devclass, NULL, NULL);
