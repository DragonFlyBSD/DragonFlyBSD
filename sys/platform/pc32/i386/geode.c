/*
 * Copyright (c) 2003 Markus Friedl <markus@openbsd.org>
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
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/systimer.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/wdog.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pcidevs.h"
#include <bus/pci/pcib_private.h>
#include <machine/pc/bios.h>

/*
 * Geode SC1100 Information Appliance On a Chip
 * http://www.national.com/ds.cgi/SC/SC1100.pdf
 */

/* Configuration Space Register Map */

#define SC1100_F5_SCRATCHPAD	0x64

#define	GCB_WDTO	0x0000	/* WATCHDOG Timeout */
#define	GCB_WDCNFG	0x0002	/* WATCHDOG Configuration */
#define	GCB_WDSTS	0x0004	/* WATCHDOG Status */
#define	GCB_TSC		0x0008	/* Cyclecounter at 27MHz */
#define	GCB_TSCNFG	0x000c	/* config for the above */
#define	GCB_IID		0x003c	/* IA On a Chip ID */
#define	GCB_REV		0x003d	/* Revision */
#define	GCB_CBA		0x003e	/* Configuration Base Address */

/* Watchdog */

#define	WD32KPD_ENABLE	0x0000
#define	WD32KPD_DISABLE	0x0100
#define	WDTYPE1_RESET	0x0030
#define	WDTYPE2_RESET	0x00c0
#define	WDPRES_DIV_512	0x0009
#define	WDPRES_DIV_8192	0x000d
#define	WDCNFG_MASK	0x00ff
#define	WDOVF_CLEAR	0x0001

/* cyclecounter */
#define	TSC_ENABLE	0x0200
#define GEODE_TIMER_FREQ	27000000

struct geode_softc {
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

static struct geode_softc geode_sc;
static sysclock_t geode_get_timecount(void);

static void
geode_cputimer_construct(struct cputimer *timer, sysclock_t oldclock)
{
	timer->base = 0;
	timer->base = oldclock - geode_get_timecount();
}

static struct cputimer geode_timer = {
	SLIST_ENTRY_INITIALIZER,
	"Geode",
	CPUTIMER_PRI_GEODE,
	CPUTIMER_GEODE,
	geode_get_timecount,
	cputimer_default_fromhz,
	cputimer_default_fromus,
	geode_cputimer_construct,
	cputimer_default_destruct,
	GEODE_TIMER_FREQ,
	0, 0, 0
};

static sysclock_t
geode_get_timecount(void)
{

	return (geode_timer.base +
		bus_space_read_4(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_TSC));
}

static int
geode_watchdog(void *unused, int period)
{
	if (period > 0x03ff)
		period = 0x03ff;

	bus_space_write_2(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDTO, period * 64);

	return period;
}

static struct watchdog	geode_wdog = {
	.name		=	"Geode SC1100",
	.wdog_fn	=	geode_watchdog,
	.arg		=	NULL,
	.period_max	=	0x03ff,
};

static int
geode_probe(device_t self)
{
	if (pci_get_vendor(self) == PCI_VENDOR_NS &&
		(pci_get_device(self) == PCI_PRODUCT_NS_SC1100_XBUS ||
		pci_get_device(self) == PCI_PRODUCT_NS_SCx200_XBUS)) {
		/* device_set_desc(self, ...) */
		return 0;
	}

	return ENXIO;
}
static int
geode_attach(device_t self)
{
	u_int32_t	reg;
	u_int16_t	cnfg, cba;
	u_int8_t	sts, rev, iid;

	/*
	 * The address of the CBA is written to this register
	 * by the bios, see p161 in data sheet.
	 */
	reg = pci_read_config(self, SC1100_F5_SCRATCHPAD, 4);
	if (reg == 0)
		return ENODEV;

	/* bus_space_map(sc->sc_iot, reg, 64, 0, &sc->sc_ioh)) */
	geode_sc.sc_iot = I386_BUS_SPACE_IO;
	geode_sc.sc_ioh = reg;

	cba = bus_space_read_2(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_CBA);
	if (cba != reg) {
		kprintf("Geode LX: cba mismatch: cba 0x%x != reg 0x%x\n", cba, reg);
		return ENODEV;
	}

	/* outl(cba + 0x0d, 2); ??? */
	sts = bus_space_read_1(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDSTS);
	cnfg = bus_space_read_2(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDCNFG);
	iid = bus_space_read_1(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_IID);
	rev = bus_space_read_1(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_REV);
#define WDSTSBITS "\20\x04WDRST\x03WDSMI\x02WDINT\x01WDOVF"
	kprintf("Geode LX: iid %d revision %d wdstatus %b\n", iid, rev, sts, WDSTSBITS);

	/* enable timer */
	bus_space_write_4(geode_sc.sc_iot, geode_sc.sc_iot, GCB_TSCNFG, TSC_ENABLE);
	cputimer_register(&geode_timer);
	cputimer_select(&geode_timer, 0);

	/* enable watchdog and configure */
	bus_space_write_2(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDTO, 0);
	sts |= WDOVF_CLEAR;
	bus_space_write_1(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDSTS, sts);
	cnfg &= ~WDCNFG_MASK;
	cnfg |= WDTYPE1_RESET | WDPRES_DIV_512;
	bus_space_write_2(geode_sc.sc_iot, geode_sc.sc_ioh, GCB_WDCNFG, cnfg);
	wdog_register(&geode_wdog);

	return 0;
}

static device_method_t geode_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		geode_probe),
	DEVMETHOD(device_attach,	geode_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static driver_t geode_driver = {
	"geode",
	geode_methods,
	0,
};

static devclass_t geode_devclass;

DRIVER_MODULE(geode, pci, geode_driver, geode_devclass, NULL, NULL);
