/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/dev/netif/mii_layer/Attic/nvphy.c,v 1.1 2005/10/24 16:45:19 dillon Exp $
 */

/*
 * Driver for NVidia PHYs
 *
 * Basically just use the generic MII driver (ukphy) but flag it as being
 * GiGE capable.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include "mii.h"
#include "miivar.h"
#include "miidevs.h"

#include "miibus_if.h"

#define ML_STATE_AUTO_SELF	1
#define ML_STATE_AUTO_OTHER	2

static int nvphy_probe		(device_t);

/*
 * Use a custom probe + the generic mii device functions.
 */
static device_method_t nvphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		nvphy_probe),
	DEVMETHOD(device_attach,	ukphy_attach),
	DEVMETHOD(device_detach,	ukphy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	{ 0, 0 }
};

static devclass_t nvphy_devclass;

static driver_t nvphy_driver = {
	"nvphy",
	nvphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(nvphy, miibus, nvphy_driver, nvphy_devclass, 0, 0);

static
int
nvphy_probe(device_t dev)
{
	struct mii_attach_args	*ma;
	device_t		parent;

	ma = device_get_ivars(dev);
	parent = device_get_parent(device_get_parent(dev));

	/*
	 * Marvell chipset, nvidia driver
	 */
	if (MII_OUI(ma->mii_id1, ma->mii_id2) != MII_OUI_MARVELL)
		return (ENXIO);
	if (strcmp(device_get_name(parent), "nv") != 0)
		return (ENXIO);

	/*
	 * MII device name + flag for generic GigE support.
	 */
	device_set_desc(dev, "NVidia media interface");
	ma->mii_flags |= MIIF_IS_1000X;

	return (0);
}

