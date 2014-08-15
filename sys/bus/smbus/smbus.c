/*-
 * Copyright (c) 1998, 2001 Nicolas Souchu
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
 * $FreeBSD: src/sys/dev/smbus/smbus.c,v 1.18.10.4 2006/09/26 18:44:56 jhb Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h> 

#include "smbconf.h"
#include "smbus.h"
#include "smbus_if.h"
#include "bus_if.h"

/*
 * Autoconfiguration and support routines for System Management bus
 */

/*
 * Device methods
 */
static int smbus_probe(device_t);
static int smbus_attach(device_t);
static int smbus_detach(device_t);

static device_t smbus_add_child(device_t bus, device_t parent, int order,
				const char *name, int unit);
static void smbus_probe_device(device_t dev, u_char addr);

static device_method_t smbus_methods[] = {
        /* device interface */
        DEVMETHOD(device_probe,         smbus_probe),
        DEVMETHOD(device_attach,        smbus_attach),
        DEVMETHOD(device_detach,        smbus_detach),

        /* bus interface */
	DEVMETHOD(bus_add_child,	smbus_add_child),
	DEVMETHOD(bus_driver_added,     bus_generic_driver_added),
        DEVMETHOD(bus_print_child,	bus_generic_print_child),

        DEVMETHOD_END
};

driver_t smbus_driver = {
        "smbus",
        smbus_methods,
        sizeof(struct smbus_softc),
};

devclass_t smbus_devclass;

/*
 * At 'probe' time, we add all the devices which we know about to the
 * bus.  The generic attach routine will probe and attach them if they
 * are alive.
 */
static int
smbus_probe(device_t dev)
{
	device_set_desc(dev, "System Management Bus");

	return (0);
}

static int
smbus_attach(device_t dev)
{
	unsigned char addr;

	device_add_child(dev, NULL, device_get_unit(dev));
	for (addr = 16; addr < 112; ++addr) {
		smbus_probe_device(dev, addr);
	}

	/*bus_generic_probe(dev);*/
	bus_generic_attach(dev);

	return (0);
}

static int
smbus_detach(device_t dev)
{
	int error;

	error = bus_generic_detach(dev);
	if (error)
		return (error);

	return (0);
}

void
smbus_generic_intr(device_t dev, u_char devaddr, char low, char high)
{
}

static void
smbus_probe_device(device_t dev, u_char addr)
{
	int error;
	u_char cmd;
	u_char buf[2];

	cmd = 0x01;

	error = smbus_trans(dev, addr, cmd,
			    SMB_TRANS_NOCNT | SMB_TRANS_NOREPORT,
			    NULL, 0, buf, 1, NULL);
	if (error == 0) {
		device_printf(dev, "Probed address 0x%02x\n", addr);
		/* device_add_child / specific */
		device_add_child(dev, NULL,
				 (device_get_unit(dev) << 11) | 0x0400 | addr);
	}
}

static device_t
smbus_add_child(device_t bus, device_t parent, int order,
		const char *name, int unit)
{
	device_t child;

	kprintf("smbus_add_child unit %d\n", unit);
	child = device_add_child_ordered(parent, order, name, unit);
	device_probe_and_attach(child);

	return child;
}


MODULE_VERSION(smbus, SMBUS_MODVER);
