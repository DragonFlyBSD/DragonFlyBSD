/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
 *
 * Libretto PCMCIA floppy support by David Horwitt (dhorwitt@ucsd.edu)
 * aided by the Linux floppy driver modifications from David Bateman
 * (dbateman@eng.uts.edu.au).
 *
 * Copyright (c) 1993, 1994 by
 *  jc@irbs.UUCP (John Capo)
 *  vak@zebub.msk.su (Serge Vakulenko)
 *  ache@astral.msk.su (Andrew A. Chernov)
 *
 * Copyright (c) 1993, 1994, 1995 by
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
 *  dufault@hda.com (Peter Dufault)
 *
 * Copyright (c) 2001 Joerg Wunsch,
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include "fdc.h"
#include "fdreg.h"

#include <bus/pccard/pccardvar.h>

#include "card_if.h"
#include "pccarddevs.h"

static const struct pccard_product fdc_products[] = {
  	PCMCIA_CARD(YEDATA, EXTERNAL_FDD, 0),
  	{ NULL }
};

static void
fdctl_wr_pcmcia(fdc_p fdc, u_int8_t v)
{
	bus_space_write_1(fdc->portt, fdc->porth, FDCTL+fdc->port_off, v);
}

static int fdc_pccard_match(device_t dev)
{
  	const struct pccard_product *pp;

	if ((pp = pccard_product_lookup(dev, fdc_products,
	    sizeof(fdc_products[0]), NULL)) != NULL) {
		device_set_desc(dev, pp->pp_name);
		return (0);
	}
	return (ENXIO);
}

static int
fdc_pccard_probe(device_t dev)
{
	int	error;
	struct	fdc_data *fdc;

	fdc = device_get_softc(dev);
	bzero(fdc, sizeof *fdc);
	fdc->fdc_dev = dev;
	fdc->fdctl_wr = fdctl_wr_pcmcia;

	fdc->flags |= FDC_ISPCMCIA | FDC_NODMA;

	/* Attempt to allocate our resources for the duration of the probe */
	error = fdc_alloc_resources(fdc);
	if (error)
		goto out;

	/* First - lets reset the floppy controller */
	fdout_wr(fdc, 0);
	DELAY(100);
	fdout_wr(fdc, FDO_FRST);

	/* see if it can handle a command */
	if (fd_cmd(fdc, 3, NE7CMD_SPECIFY, NE7_SPEC_1(3, 240), 
		   NE7_SPEC_2(2, 0), 0)) {
		error = ENXIO;
		goto out;
	}

	fdc->fdct = FDC_NE765;

out:
	fdc_release_resources(fdc);
	return (error);
}

static int
fdc_pccard_detach(device_t dev)
{
	struct	fdc_data *fdc;
	int	error;

	fdc = device_get_softc(dev);

	/* have our children detached first */
	if ((error = bus_generic_detach(dev)))
		return (error);

	if ((fdc->flags & FDC_ATTACHED) == 0) {
		device_printf(dev, "already unloaded\n");
		return (0);
	}
	fdc->flags &= ~FDC_ATTACHED;

	BUS_TEARDOWN_INTR(device_get_parent(dev), dev, fdc->res_irq,
			  fdc->fdc_intr);
	fdc_release_resources(fdc);
	device_printf(dev, "unload\n");
	return (0);
}

static device_method_t fdc_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pccard_compat_probe),
	DEVMETHOD(device_attach,	pccard_compat_attach),
	DEVMETHOD(device_detach,	fdc_pccard_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Card interface */
	DEVMETHOD(card_compat_match,	fdc_pccard_match),
	DEVMETHOD(card_compat_probe,	fdc_pccard_probe),
	DEVMETHOD(card_compat_attach,	fdc_attach),
	/* Device interface */

	/* Bus interface */
	DEVMETHOD(bus_print_child,	fdc_print_child),
	DEVMETHOD(bus_read_ivar,	fdc_read_ivar),
	/* Our children never use any other bus interface methods. */

	DEVMETHOD_END
};

static driver_t fdc_pccard_driver = {
	"fdc",
	fdc_pccard_methods,
	sizeof(struct fdc_data)
};

DRIVER_MODULE(fdc, pccard, fdc_pccard_driver, fdc_devclass, NULL, NULL);
