/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/timepps.h>

#include "sioreg.h"
#include "sio_private.h"

#include <bus/pccard/pccard_cis.h>
#include <bus/pccard/pccardreg.h>
#include <bus/pccard/pccardvar.h>

#include "pccarddevs.h"

static	int	sio_pccard_attach(device_t dev);
static	int	sio_pccard_match(device_t self);
static	int	sio_pccard_detach(device_t dev);
static	int	sio_pccard_probe(device_t dev);

static device_method_t sio_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pccard_compat_probe),
	DEVMETHOD(device_attach,	pccard_compat_attach),
	DEVMETHOD(device_detach,	sio_pccard_detach),

	/* Card interface */
	DEVMETHOD(card_compat_match,	sio_pccard_match),
	DEVMETHOD(card_compat_probe,	sio_pccard_probe),
	DEVMETHOD(card_compat_attach,	sio_pccard_attach),

	DEVMETHOD_END
};

static driver_t sio_pccard_driver = {
	"sio",
	sio_pccard_methods,
	sizeof(struct com_s),
};

static int
sio_pccard_match(device_t dev)
{
	u_int32_t	fcn = PCCARD_FUNCTION_UNSPEC;

	fcn = pccard_get_function(dev);
	/*
	 * If a serial card, we are likely the right driver.  However,
	 * some serial cards are better servered by other drivers, so
	 * allow other drivers to claim it, if they want.
	 */
	if (fcn == PCCARD_FUNCTION_SERIAL)
		return (-100);

	return(ENXIO);
}

static int
sio_pccard_probe(device_t dev)
{
	/* Do not probe IRQ - pccard doesn't turn on the interrupt line */
	/* until bus_setup_intr */
	SET_FLAG(dev, COM_C_NOPROBE);

	return (sioprobe(dev, 0, 0UL));
}

static int
sio_pccard_attach(device_t dev)
{
	return (sioattach(dev, 0, 0UL));
}

/*
 *	sio_detach - unload the driver and clear the table.
 *	XXX TODO:
 *	This is usually called when the card is ejected, but
 *	can be caused by a modunload of a controller driver.
 *	The idea is to reset the driver's view of the device
 *	and ensure that any driver entry points such as
 *	read and write do not hang.
 */
static int
sio_pccard_detach(device_t dev)
{
	struct com_s	*com;

	com = (struct com_s *) device_get_softc(dev);
	if (com == NULL) {
		device_printf(dev, "NULL com in siounload\n");
		return (0);
	}
	com->gone = 1;
	if (com->irqres) {
		bus_teardown_intr(dev, com->irqres, com->cookie);
		bus_release_resource(dev, SYS_RES_IRQ, 0, com->irqres);
	}
	if (com->ioportres)
		bus_release_resource(dev, SYS_RES_IOPORT, 0, com->ioportres);
	if (com->tp && (com->tp->t_state & TS_ISOPEN)) {
		device_printf(dev, "still open, forcing close\n");
		ttyclose(com->tp);
	} else {
		if (com->ibuf != NULL)
			kfree(com->ibuf, M_DEVBUF);
	}
	device_printf(dev, "unloaded\n");
	return (0);
}

DRIVER_MODULE(sio, pccard, sio_pccard_driver, sio_devclass, NULL, NULL);
