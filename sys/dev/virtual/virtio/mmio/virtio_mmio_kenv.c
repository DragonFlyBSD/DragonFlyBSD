/*-
 * Copyright (c) 2022 Colin Percival
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/machintr.h>

/* DragonFly: SYS_RES_* / bus accessors live in <sys/bus.h> + <sys/rman.h>. */
#include "virtio_mmio.h"

/*
 * Children added by the identify routine are tracked here so the MOD_UNLOAD
 * handler can delete them.  Otherwise the device_t's would linger on nexus
 * after a kldunload, and the next kldload would create duplicates.
 */
#define VTMMIO_KENV_MAXDEV	32
static device_t	vtmmio_kenv_children[VTMMIO_KENV_MAXDEV];
static int	vtmmio_kenv_nchildren;

/* Parse <size>@<baseaddr>:<irq>[:<unit>] and add a child. */
static void
vtmmio_parsearg(driver_t *driver, device_t parent, char *arg)
{
	device_t child;
	char *p;
	unsigned long sz;
	unsigned long baseaddr;
	unsigned long irq;
	unsigned long unit;

	/* <size> */
	sz = strtoul(arg, &p, 0);
	if ((sz == 0) || (sz == ULONG_MAX))
		goto bad;
	switch (*p) {
	case 'E': case 'e':
		sz <<= 10;
		/* FALLTHROUGH */
	case 'P': case 'p':
		sz <<= 10;
		/* FALLTHROUGH */
	case 'T': case 't':
		sz <<= 10;
		/* FALLTHROUGH */
	case 'G': case 'g':
		sz <<= 10;
		/* FALLTHROUGH */
	case 'M': case 'm':
		sz <<= 10;
		/* FALLTHROUGH */
	case 'K': case 'k':
		sz <<= 10;
		p++;
		break;
	}

	/* @<baseaddr> */
	if (*p++ != '@')
		goto bad;
	baseaddr = strtoul(p, &p, 0);
	if ((baseaddr == 0) || (baseaddr == ULONG_MAX))
		goto bad;

	/* :<irq> */
	if (*p++ != ':')
		goto bad;
	irq = strtoul(p, &p, 0);
	if ((irq == 0) || (irq == ULONG_MAX))
		goto bad;

	/* Optionally, :<unit> */
	if (*p) {
		if (*p++ != ':')
			goto bad;
		unit = strtoul(p, &p, 0);
		if ((unit == 0) || (unit == ULONG_MAX))
			goto bad;
	} else {
		unit = 0;
	}

	/* Should have reached the end of the string. */
	if (*p)
		goto bad;

	/* Create the child and assign its resources. */
	child = BUS_ADD_CHILD(parent, parent, 0, "virtio_mmio",
	    unit ? (int)unit : -1);
	if (child == NULL)
		return;
	device_set_driver(child, driver);
	bus_set_resource(child, SYS_RES_MEMORY, 0, baseaddr, sz, -1);
	/*
	 * The x86 nexus keeps a per-CPU IRQ rman and rejects a -1 cpuid, so the
	 * IRQ resource needs a real target CPU.  Route it to whichever CPU the
	 * interrupt ABI assigns this GSI to (as the ISA/PCI/ACPI paths do); the
	 * host VMM is expected to deliver the interrupt to that same CPU, per the
	 * IOAPIC RTE destination field.
	 */
	bus_set_resource(child, SYS_RES_IRQ, 0, irq, 1,
	    machintr_legacy_intr_cpuid(irq));

	if (vtmmio_kenv_nchildren < VTMMIO_KENV_MAXDEV)
		vtmmio_kenv_children[vtmmio_kenv_nchildren++] = child;

	return;

bad:
	kprintf("Error parsing hw.virtio.mmio.device parameter: %s\n", arg);
}

static void
vtmmio_kenv_identify(driver_t *driver, device_t parent)
{
	char name[32];
	char *val;
	size_t n;

	if ((val = kgetenv("hw.virtio.mmio.device")) == NULL)
		return;
	vtmmio_parsearg(driver, parent, val);
	kfreeenv(val);

	for (n = 1; n <= 9999; n++) {
		ksnprintf(name, sizeof(name), "hw.virtio.mmio.device_%zu", n);
		if ((val = kgetenv(name)) == NULL)
			return;
		vtmmio_parsearg(driver, parent, val);
		kfreeenv(val);
	}
}

static device_method_t vtmmio_kenv_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_identify,	vtmmio_kenv_identify),
	DEVMETHOD(device_probe,		vtmmio_probe),

	DEVMETHOD_END
};

DEFINE_CLASS_1(virtio_mmio, vtmmio_kenv_driver, vtmmio_kenv_methods,
    sizeof(struct vtmmio_softc), vtmmio_driver);

static devclass_t vtmmio_kenv_devclass;

/*
 * Delete the identify-created children on unload so the module can be unloaded
 * and reloaded cleanly.  By the time this chained handler runs,
 * devclass_delete_driver() has already detached the driver from them (see
 * driver_module_handler(), MOD_UNLOAD), so we only delete the device_t's.
 */
static int
vtmmio_kenv_modevent(module_t mod __unused, int type, void *arg __unused)
{
	int i;

	if (type == MOD_UNLOAD) {
		for (i = 0; i < vtmmio_kenv_nchildren; i++) {
			if (vtmmio_kenv_children[i] == NULL)
				continue;
			device_delete_child(
			    device_get_parent(vtmmio_kenv_children[i]),
			    vtmmio_kenv_children[i]);
			vtmmio_kenv_children[i] = NULL;
		}
		vtmmio_kenv_nchildren = 0;
	}
	return (0);
}

DRIVER_MODULE(virtio_mmio_kenv, nexus, vtmmio_kenv_driver, vtmmio_kenv_devclass,
    vtmmio_kenv_modevent, NULL);
