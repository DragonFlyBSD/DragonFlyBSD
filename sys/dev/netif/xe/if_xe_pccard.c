/*
 * Copyright (c) 2002 Takeshi Shibagaki
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
 * xe pccard interface driver
 *
 * $FreeBSD: src/sys/dev/xe/if_xe_pccard.c,v 1.11 2003/10/14 22:51:35 rsm Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
 
#include <net/ethernet.h>
#include <net/if.h> 
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_mib.h>

#include <bus/pccard/pccardvar.h>

#include "card_if.h"
#include "pccarddevs.h"

#include "if_xereg.h"
#include "if_xevar.h"

#define XE_DEBUG

#ifdef XE_DEBUG
#define DEVPRINTF(level, arg)	if (xe_debug >= (level)) device_printf arg
#else
#define DEVPRINTF(level, arg)
#endif

static const struct pccard_product xe_pccard_products[] = {
	PCMCIA_CARD(COMPAQ, CPQ550, 0),
	PCMCIA_CARD(COMPAQ2, CPQ_10_100, 0),
	PCMCIA_CARD(INTEL, EEPRO100, 0),
	PCMCIA_CARD(RACORE, ACCTON_EN2226, 0),
	PCMCIA_CARD(XIRCOM, CE, 0),
	PCMCIA_CARD(XIRCOM, CE2, 0),
	PCMCIA_CARD(XIRCOM, CE3, 0),
	PCMCIA_CARD(XIRCOM, CEM, 0),
	PCMCIA_CARD(XIRCOM, CEM28, 0),
	PCMCIA_CARD(XIRCOM, CEM33, 0),
	PCMCIA_CARD(XIRCOM, CEM56, 0),
	PCMCIA_CARD(XIRCOM, REM56, 0),
	PCMCIA_CARD(XIRCOM, CNW_801, 0),
	PCMCIA_CARD(XIRCOM, CNW_802, 0),
        { NULL }
};

struct xe_vendor {
	uint32_t	 vendor_id;
	const char	*vendor_desc;
} xe_vendors[] = {
	{ PCMCIA_VENDOR_XIRCOM,		"Xircom" },
	{ PCMCIA_VENDOR_COMPAQ,		"Compaq" },
	{ PCMCIA_VENDOR_COMPAQ2,	"Compaq" },
	{ PCMCIA_VENDOR_INTEL,		"Intel" },
	{ 0,				"Unknown" }
};

#define XE_CARD_TYPE_FLAGS_NO		0x0
#define XE_CARD_TYPE_FLAGS_CE2		0x1
#define XE_CARD_TYPE_FLAGS_MOHAWK	0x2
#define XE_CARD_TYPE_FLAGS_DINGO	0x4

#define XE_PROD_UMASK		0x11000f
#define XE_PROD_ETHER_UMASK	0x010000
#define XE_PROD_MODEM_UMASK	0x100000
#define XE_PROD_SINGLE_ID1	0x010001
#define XE_PROD_SINGLE_ID2	0x010002
#define XE_PROD_SINGLE_ID3	0x010003
#define XE_PROD_MULTI_ID1	0x110001
#define XE_PROD_MULTI_ID2	0x110002
#define XE_PROD_MULTI_ID3	0x110003
#define XE_PROD_MULTI_ID4	0x110004
#define XE_PROD_MULTI_ID5	0x110005
#define XE_PROD_MULTI_ID6	0x110006 
#define XE_PROD_MULTI_ID7	0x110007  

struct xe_card_type {
	uint32_t	 prod_type;
	const char	*card_type_desc;
	uint32_t	 flags;
} xe_card_types[] = {
	{ XE_PROD_MULTI_ID1,	"CEM",		XE_CARD_TYPE_FLAGS_NO },
	{ XE_PROD_MULTI_ID2,	"CEM2",		XE_CARD_TYPE_FLAGS_CE2 },
	{ XE_PROD_MULTI_ID3,	"CEM3",		XE_CARD_TYPE_FLAGS_CE2 },
	{ XE_PROD_MULTI_ID4,	"CEM33",	XE_CARD_TYPE_FLAGS_CE2 },
	{ XE_PROD_MULTI_ID5,	"CEM56M",	XE_CARD_TYPE_FLAGS_MOHAWK },
	{ XE_PROD_MULTI_ID6,	"CEM56",	XE_CARD_TYPE_FLAGS_MOHAWK |
						XE_CARD_TYPE_FLAGS_DINGO },
	{ XE_PROD_MULTI_ID7,	"CEM56",	XE_CARD_TYPE_FLAGS_MOHAWK |
						XE_CARD_TYPE_FLAGS_DINGO },
	{ XE_PROD_SINGLE_ID1,	"CE",		XE_CARD_TYPE_FLAGS_NO },
	{ XE_PROD_SINGLE_ID2,	"CE2",		XE_CARD_TYPE_FLAGS_CE2 },
	{ XE_PROD_SINGLE_ID3,	"CE3",		XE_CARD_TYPE_FLAGS_MOHAWK },
	{ 0, NULL, -1 }
};

static struct xe_vendor		*xe_vendor_lookup	(uint32_t);
static struct xe_card_type	*xe_card_type_lookup	(uint32_t);

static int	xe_cemfix	(device_t);
static int	xe_pccard_probe	(device_t);
static int	xe_pccard_match	(device_t);
static int	xe_pccard_attach(device_t);

static device_method_t xe_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pccard_compat_probe),
	DEVMETHOD(device_attach,	pccard_compat_attach),
	DEVMETHOD(device_detach,	xe_detach),

	/* Card interface */
	DEVMETHOD(card_compat_match,	xe_pccard_match),
	DEVMETHOD(card_compat_probe,	xe_pccard_probe),
	DEVMETHOD(card_compat_attach,	xe_pccard_attach),

        DEVMETHOD_END
};

static driver_t xe_pccard_driver = {
	"xe",
	xe_pccard_methods,
	sizeof(struct xe_softc),
};

devclass_t xe_devclass;
DRIVER_MODULE(xe, pccard, xe_pccard_driver, xe_devclass, NULL, NULL);

/*
 * Fixing for CEM2, CEM3 and CEM56/REM56 cards.  These need some magic to
 * enable the Ethernet function, which isn't mentioned anywhere in the CIS.
 * Despite the register names, most of this isn't Dingo-specific.
 */
static int
xe_cemfix(device_t dev)
{
	struct xe_softc *sc = device_get_softc(dev);
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	struct resource *r;
	int rid;
	int ioport;

	device_printf(dev, "CEM I/O port 0x%0lx, size 0x%0lx\n",
	    bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid),
	    bus_get_resource_count(dev, SYS_RES_IOPORT, sc->port_rid));

	rid = 0;
	r = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid, 0,
			       ~0, 4 << 10, RF_ACTIVE);
	if (r == NULL) {
		device_printf(dev, "Can't map in attribute memory\n");
		return -1;
	}

	bsh = rman_get_bushandle(r);
	bst = rman_get_bustag(r);

	CARD_SET_RES_FLAGS(device_get_parent(dev), dev, SYS_RES_MEMORY, rid,
			   PCCARD_A_MEM_ATTR);

	bus_space_write_1(bst, bsh, DINGO_ECOR, DINGO_ECOR_IRQ_LEVEL |
						DINGO_ECOR_INT_ENABLE |
						DINGO_ECOR_IOB_ENABLE |
						DINGO_ECOR_ETH_ENABLE);
	ioport = bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid);
	bus_space_write_1(bst, bsh, DINGO_EBAR0, ioport & 0xff);
	bus_space_write_1(bst, bsh, DINGO_EBAR1, (ioport >> 8) & 0xff);

	if (sc->dingo) {
		bus_space_write_1(bst, bsh, DINGO_DCOR0, DINGO_DCOR0_SF_INT);
		bus_space_write_1(bst, bsh, DINGO_DCOR1,
				  DINGO_DCOR1_INT_LEVEL | DINGO_DCOR1_EEDIO);
		bus_space_write_1(bst, bsh, DINGO_DCOR2, 0x00);
		bus_space_write_1(bst, bsh, DINGO_DCOR3, 0x00);
		bus_space_write_1(bst, bsh, DINGO_DCOR4, 0x00);
	}

	bus_release_resource(dev, SYS_RES_MEMORY, rid, r);

	/* success! */
	return 0;
}

static struct xe_vendor *
xe_vendor_lookup(uint32_t vendor_id)
{
	struct xe_vendor *v;

	for (v = xe_vendors; v->vendor_id != 0; ++v) {
		if(v->vendor_id == vendor_id)
			break;
	}
	return v;
}
      
static struct xe_card_type *
xe_card_type_lookup(uint32_t prod)
{
	struct xe_card_type *ct;

	for (ct = xe_card_types; ct->card_type_desc != NULL; ++ct) {
		if(ct->prod_type == (prod & XE_PROD_UMASK))
			return ct;
	}
	return NULL;
}

/*
 * PCMCIA probe routine.
 * Identify the device.  Called from the bus driver when the card is
 * inserted or otherwise powers up.
 */
static int
xe_pccard_probe(device_t dev)
{
	struct xe_softc *scp = device_get_softc(dev);
	uint32_t vendor, product, prod;
	uint16_t prodext;
	const uint8_t *ether_addr;
	const char *cis3_str=NULL;
	struct xe_vendor *vendor_itm;
	struct xe_card_type *card_itm;
	int i;

#ifdef XE_DEBUG
	const char *vendor_str = NULL;
	const char *product_str = NULL;
	const char *cis4_str = NULL;

	vendor = pccard_get_vendor(dev);
	product = pccard_get_product(dev);
	prodext = pccard_get_prodext(dev);
	vendor_str = pccard_get_vendor_str(dev);
	product_str = pccard_get_product_str(dev);
	cis3_str = pccard_get_cis3_str(dev);
	cis4_str = pccard_get_cis4_str(dev);

	DEVPRINTF(1, (dev, "pccard_probe\n"));
	DEVPRINTF(1, (dev, "vendor = 0x%04x\n", vendor));
	DEVPRINTF(1, (dev, "product = 0x%04x\n", product));
	DEVPRINTF(1, (dev, "prodext = 0x%02x\n", prodext));
	DEVPRINTF(1, (dev, "vendor_str = %s\n",
		      vendor_str == NULL ? "NULL" : vendor_str));
	DEVPRINTF(1, (dev, "product_str = %s\n",
		      product_str == NULL ? "NULL" : product_str));
	DEVPRINTF(1, (dev, "cis3_str = %s\n",
		      cis3_str == NULL ? "NULL" : cis3_str));
	DEVPRINTF(1, (dev, "cis4_str = %s\n",
		      cis4_str == NULL ? "NULL" : cis4_str));
#endif

	/*
	 * PCCARD_CISTPL_MANFID = 0x20
	 */
	vendor = pccard_get_vendor(dev);
	vendor_itm = xe_vendor_lookup(vendor);
	/*
	 * We always have some vendor here, although
	 * vendor description may be "Unknown".
	 */
	scp->vendor = vendor_itm->vendor_desc;

	product = pccard_get_product(dev);
	prodext = pccard_get_prodext(dev);

	/*
	 * prod(new) =  rev, media, prod(old)
	 * prod(new) =  (don't care), (care 0x10 bit), (care 0x0f bit)
	 */
	prod = (product << 8) | prodext;
	card_itm = xe_card_type_lookup(prod);
	if (card_itm == NULL)
		return ENODEV;

	scp->card_type = card_itm->card_type_desc;
	if (card_itm->prod_type & XE_PROD_MODEM_UMASK)
		scp->modem = 1;

	for (i = 1; i != XE_CARD_TYPE_FLAGS_DINGO; i = i << 1) {
		switch(i & card_itm->flags) {
		case XE_CARD_TYPE_FLAGS_CE2:
			scp->ce2 = 1;
			break;
		case XE_CARD_TYPE_FLAGS_MOHAWK:
			scp->mohawk = 1;
			break;
		case XE_CARD_TYPE_FLAGS_DINGO:
			scp->dingo = 1;
			break;
		}
	}

	/*
	 * PCCARD_CISTPL_VERS_1 = 0x15
	 *
	 * Check for certain strange CE2's that look like CE's:
	 * match 3rd version string against "CE2"
	 */
	cis3_str = pccard_get_cis3_str(dev);
	if (strcmp(scp->card_type, "CE") == 0)
		if (cis3_str != NULL && strcmp(cis3_str, "PS-CE2-10") == 0)
			scp->card_type = "CE2";

	/*
	 * PCCARD_CISTPL_FUNCE = 0x22
	 */
	ether_addr = pccard_get_ether(dev);
	bcopy(ether_addr, scp->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Reject unsupported cards */
	if (strcmp(scp->card_type, "CE") == 0 ||
	    strcmp(scp->card_type, "CEM") == 0) {
		device_printf(dev, "Sorry, your %s card is not supported :(\n",
			      scp->card_type);
		return ENODEV;
	}

	/* Success */
	return 0;
}

static int
xe_pccard_attach(device_t dev)
{
	struct xe_softc *scp = device_get_softc(dev);
	int err;

	if ((err = xe_activate(dev)) != 0)
		return err;
         
	/* Hack RealPorts into submission */
	if (scp->modem && xe_cemfix(dev) < 0) {
		device_printf(dev, "Unable to fix your %s combo card\n",
			      scp->card_type);
		xe_deactivate(dev);
		return ENODEV;
	}
	return xe_attach(dev);
}

static int
xe_pccard_match(device_t dev)
{
	const struct pccard_product *pp;

	if ((pp = pccard_product_lookup(dev, xe_pccard_products,
	     sizeof(xe_pccard_products[0]), NULL)) != NULL) {
		if (pp->pp_name != NULL)
			device_set_desc(dev, pp->pp_name);
		return 0;
	}
	return EIO;
}
