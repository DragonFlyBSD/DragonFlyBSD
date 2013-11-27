/*-
 * Copyright (c) 2000 Mitsuru IWASAKI
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
 *	$FreeBSD: src/sys/dev/ex/if_ex_pccard.c,v 1.2.2.1 2001/03/05 05:33:20 imp Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/serialize.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h> 
#include <net/ifq_var.h>

#include "if_exreg.h"
#include "if_exvar.h"

#include <bus/pccard/pccardvar.h>

#include "pccarddevs.h"

static const struct pccard_product ex_pccard_products[] = {
	PCMCIA_CARD(OLICOM, OC2220, 0),
	{ NULL }
};

/* Bus Front End Functions */
static int	ex_pccard_match		(device_t);
static int	ex_pccard_probe		(device_t);
static int	ex_pccard_attach	(device_t);
static int	ex_pccard_detach	(device_t);

static device_method_t ex_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pccard_compat_probe),
	DEVMETHOD(device_attach,	pccard_compat_attach),
	DEVMETHOD(device_detach,	ex_pccard_detach),

	/* Card interface */
	DEVMETHOD(card_compat_match,	ex_pccard_match),
	DEVMETHOD(card_compat_probe,	ex_pccard_probe),
	DEVMETHOD(card_compat_attach,	ex_pccard_attach),

	DEVMETHOD_END
};

static driver_t ex_pccard_driver = {
	"ex",
	ex_pccard_methods,
	sizeof(struct ex_softc),
};

extern devclass_t ex_devclass;

DRIVER_MODULE(if_ex, pccard, ex_pccard_driver, ex_devclass, NULL, NULL);

static int
ex_pccard_match(device_t dev)
{
	const struct pccard_product *pp;

	if ((pp = pccard_product_lookup(dev, ex_pccard_products,
	    sizeof(ex_pccard_products[0]), NULL)) != NULL) {
		if (pp->pp_name != NULL)
			device_set_desc(dev, pp->pp_name);
		return 0;
	}
	return EIO;
}

static int
ex_pccard_probe(device_t dev)
{
	u_int		iobase;
	u_int		irq;

	iobase = bus_get_resource_start(dev, SYS_RES_IOPORT, 0);
	if (!iobase) {
		kprintf("ex: no iobase?\n");
		return(ENXIO);
	}

	if (bootverbose)
		kprintf("ex: ex_pccard_probe() found card at 0x%03x\n", iobase);

	irq = bus_get_resource_start(dev, SYS_RES_IRQ, 0);

	if (irq == 0) {
		kprintf("ex: invalid IRQ.\n");
		return(ENXIO);
	}

	return(0);
}

static int
ex_pccard_attach(device_t dev)
{
	struct ex_softc *	sc = device_get_softc(dev);
	struct ifnet *		ifp = &sc->arpcom.ac_if;
	int			error = 0;
	int			i;
	uint8_t			sum;
	const uint8_t	*	ether_addr;

	sc->dev = dev;
	sc->ioport_rid = 0;
	sc->irq_rid = 0;

	if ((error = ex_alloc_resources(dev)) != 0) {
		device_printf(dev, "ex_alloc_resources() failed!\n");
		goto bad;
	}

	/*
	 * Fill in several fields of the softc structure:
	 *	- I/O base address.
	 *	- Hardware Ethernet address.
	 *	- IRQ number.
	 */
	sc->iobase = rman_get_start(sc->ioport);
	sc->irq_no = rman_get_start(sc->irq);

	ether_addr = pccard_get_ether(dev);
	for (i = 0, sum = 0; i < ETHER_ADDR_LEN; i++)
		sum |= ether_addr[i];
	if (sum)
		bcopy(ether_addr, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	if ((error = ex_attach(dev)) != 0) {
		device_printf(dev, "ex_attach() failed!\n");
		goto bad;
	}

	ifq_set_cpuid(&ifp->if_snd, rman_get_cpuid(sc->irq));

	error = bus_setup_intr(dev, sc->irq, INTR_MPSAFE,
				ex_intr, (void *)sc, &sc->ih, 
				ifp->if_serializer);
	if (error) {
		device_printf(dev, "bus_setup_intr() failed!\n");
		goto bad;
	}

	return(0);
bad:
	ex_release_resources(dev);
	return (error);
}

static int
ex_pccard_detach(device_t dev)
{
	struct ex_softc		*sc = device_get_softc(dev);
	struct ifnet		*ifp = &sc->arpcom.ac_if;

	lwkt_serialize_enter(ifp->if_serializer);
	ex_stop(sc);
	ifp->if_flags &= ~IFF_RUNNING;
	lwkt_serialize_exit(ifp->if_serializer);

	ether_ifdetach(ifp);
	ex_release_resources(dev);
	return (0);
}
