/*-
 * Copyright (c) 1998,1999,2000,2001,2002 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/ata-card.c,v 1.4.2.1 2002/03/18 08:37:33 sos Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/stdarg.h>
#include "ata-all.h"

#include <bus/pccard/pccard_cis.h>
#include <bus/pccard/pccardreg.h>
#include <bus/pccard/pccardvar.h>
#include <bus/pccard/pccarddevs.h>

static const struct pccard_product ata_pccard_products[] = {
	PCMCIA_CARD(FREECOM, PCCARDIDE, 0),
	PCMCIA_CARD(EXP, EXPMULTIMEDIA, 0),
	PCMCIA_CARD(IODATA3, CBIDE2, 0),
	PCMCIA_CARD(OEM2, CDROM1, 0),
	PCMCIA_CARD(OEM2, IDE, 0),
	PCMCIA_CARD(PANASONIC, KXLC005, 0),
	PCMCIA_CARD(TEAC, IDECARDII, 0),
	{NULL}
};

static int
ata_pccard_match(device_t dev)
{
    const struct pccard_product *pp;
    u_int32_t fcn = PCCARD_FUNCTION_UNSPEC;

    fcn = pccard_get_function_number(dev);

    /* if it says its a disk we should register it */
    if (fcn == PCCARD_FUNCTION_DISK)
	return (0);

    /* match other devices here, primarily cdrom/dvd rom */
    if ((pp = pccard_product_lookup(dev, ata_pccard_products,
				    sizeof(ata_pccard_products[0]), NULL))) {
	if (pp->pp_name)
	    device_set_desc(dev, pp->pp_name);
	return (0);
    }
    return (ENXIO);
}

static int
ata_pccard_probe(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct resource *io;
    int rid, len, start, end;
    u_long tmp;

    /* allocate the io range to get start and length */
    rid = ATA_IOADDR_RID;
    len = bus_get_resource_count(dev, SYS_RES_IOPORT, rid);
    io = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid, 0, ~0,
			    ATA_IOSIZE, RF_ACTIVE);
    if (!io)
	return ENOMEM;

    /* reallocate the io address to only cover the io ports */
    start = rman_get_start(io);
    end = start + ATA_IOSIZE - 1;
    bus_release_resource(dev, SYS_RES_IOPORT, rid, io);
    io = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
			    start, end, ATA_IOSIZE, RF_ACTIVE);
    bus_release_resource(dev, SYS_RES_IOPORT, rid, io);

    /* 
     * if we got more than the default ATA_IOSIZE ports, this is likely
     * a pccard system where the altio ports are located at offset 14
     * otherwise its the normal altio offset
     */
    if (bus_get_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID, &tmp, &tmp)) {
	if (len > ATA_IOSIZE) {
	    bus_set_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID,
			     start + ATA_PCCARD_ALTOFFSET, ATA_ALTIOSIZE);
	}
	else {
	    bus_set_resource(dev, SYS_RES_IOPORT, ATA_ALTADDR_RID, 
			     start + ATA_ALTOFFSET, ATA_ALTIOSIZE);
	}
    }
    else
	return ENOMEM;

    ch->unit = 0;
    ch->flags |= (ATA_USE_16BIT | ATA_NO_SLAVE);
    return ata_probe(dev);
}

static device_method_t ata_pccard_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,	pccard_compat_probe),
    DEVMETHOD(device_attach,	pccard_compat_attach),
    DEVMETHOD(device_detach,	ata_detach),

    /* Card interface */
    DEVMETHOD(card_compat_match,	ata_pccard_match),
    DEVMETHOD(card_compat_probe,	ata_pccard_probe),
    DEVMETHOD(card_compat_attach,	ata_attach),

    { 0, 0 }
};

static driver_t ata_pccard_driver = {
    "ata",
    ata_pccard_methods,
    sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, pccard, ata_pccard_driver, ata_devclass, NULL, NULL);
