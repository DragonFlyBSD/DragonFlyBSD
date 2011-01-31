/*
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/nexus.c,v 1.26.2.10 2003/02/22 13:16:45 imp Exp $
 * $DragonFly: src/sys/bus/isa/pnpeat.c,v 1.5 2008/08/02 01:14:39 dillon Exp $
 */

/*
 * This code implements a `root nexus' for Intel Architecture
 * machines.  The function of the root nexus is to serve as an
 * attachment point for both processors and buses, and to manage
 * resources which are common to all of them.  In particular,
 * this code implements the core resource managers for interrupt
 * requests, DMA requests (which rightfully should be a part of the
 * ISA code but it's easier to do it here for now), I/O port addresses,
 * and I/O memory address space.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <bus/isa/isavar.h>
#include <bus/isa/isa.h>

/*
 * Placeholder which claims PnP 'devices' which describe system
 * resources.
 */
static struct isa_pnp_id sysresource_ids[] = {
	{ 0x010cd041 /* PNP0c01 */, "System Memory" },
	{ 0x020cd041 /* PNP0c02 */, "System Resource" },
	{ 0 }
};

static int
sysresource_probe(device_t dev)
{
	int	result;

	if ((result = ISA_PNP_PROBE(device_get_parent(dev), dev, sysresource_ids)) >= 0) {
		device_quiet(dev);
	}
	return (result);
}

static int
sysresource_attach(device_t dev)
{
	return (0);
}

static device_method_t sysresource_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sysresource_probe),
	DEVMETHOD(device_attach,	sysresource_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	{ 0, 0 }
};

static driver_t sysresource_driver = {
	"sysresource",
	sysresource_methods,
	1,		/* no softc */
};

static devclass_t sysresource_devclass;

DRIVER_MODULE(sysresource, isa, sysresource_driver, sysresource_devclass, 0, 0);
