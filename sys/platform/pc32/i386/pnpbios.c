/*-
 * Copyright (c) 2004 Joerg Sonnenberger <joerg@bec.de>
 *
 * Copyright (c) 1997 Michael Smith
 * Copyright (c) 1998 Jonathan Lemon
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

/*
 * Code for dealing with the PnP-BIOS in x86 PC systems.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <machine/pc/bios.h>

#include <bus/isa/pnpreg.h>
#include <bus/isa/isavar.h>

/*
 * PnP BIOS interface; enumerate devices only known to the system
 * BIOS and save information about them for later use.
 */

struct pnp_sysdev 
{
    uint16_t	size;
    uint8_t	handle;
    uint32_t	devid;
    uint8_t	type[3];
    uint16_t	attrib;
    /* device-specific data comes here */
    uint8_t	devdata[0];
} __attribute__((__packed__));

#define PNPATTR_NODISABLE	(1<<0)	/* can't be disabled */
#define PNPATTR_NOCONFIG	(1<<1)	/* can't be configured */
#define PNPATTR_OUTPUT		(1<<2)	/* can be primary output */
#define PNPATTR_INPUT		(1<<3)	/* can be primary input */
#define PNPATTR_BOOTABLE	(1<<4)	/* can be booted from */
#define PNPATTR_DOCK		(1<<5)	/* is a docking station */
#define PNPATTR_REMOVEABLE	(1<<6)	/* device is removeable */

#define PNPATTR_CONFIG(a)	(((a) >> 7) & 0x03)
#define PNPATTR_CONFIG_STATIC	0x00
#define PNPATTR_CONFIG_DYNAMIC	0x01
#define PNPATTR_CONFIG_DYNONLY	0x03

/* We have to cluster arguments within a 64k range for the bios16 call */
struct pnp_sysdevargs
{
    uint16_t	next;
    uint16_t	pad;
    struct pnp_sysdev node;
};

/*
 * This function is called after the bus has assigned resource
 * locations for a logical device.
 */
static void
pnpbios_set_config(void *arg, struct isa_config *config, int enable)
{
}

/*
 * Quiz the PnP BIOS, build a list of PNP IDs and resource data.
 */
static int
pnpbios_identify(driver_t *driver, device_t parent)
{
    struct PnPBIOS_table	*pt = PnPBIOStable;
    struct bios_args		args;
    struct pnp_sysdev		*pd;
    struct pnp_sysdevargs	*pda;
    uint16_t			ndevs, bigdev;
    int				error, currdev;
    uint8_t			*devnodebuf, tag;
    uint32_t			*devid, *compid;
    int				idx, left;
    device_t			dev;

    /*
     * Umm, we aren't going to rescan the PnP BIOS to look for new additions.
     */
    if (device_get_state(parent) == DS_ATTACHED)
	return (0);
        
    /* no PnP BIOS information */
    if (pt == NULL)
	return (ENXIO);

    /* ACPI already active */
    if (devclass_get_softc(devclass_find("ACPI"), 0) != NULL)
	return (ENXIO);
    
    bzero(&args, sizeof(args));
    args.seg.code16.base = BIOS_PADDRTOVADDR(pt->pmentrybase);
    args.seg.code16.limit = 0xffff;		/* XXX ? */
    args.seg.data.base = BIOS_PADDRTOVADDR(pt->pmdataseg);
    args.seg.data.limit = 0xffff;
    args.entry = pt->pmentryoffset;

    if ((error = bios16(&args, PNP_COUNT_DEVNODES, &ndevs, &bigdev)) || (args.r.eax & 0xff))
	kprintf("pnpbios: error %d/%x getting device count/size limit\n", error, args.r.eax);
    ndevs &= 0xff;				/* clear high byte garbage */
    if (bootverbose)
	kprintf("pnpbios: %d devices, largest %d bytes\n", ndevs, bigdev);

    devnodebuf = kmalloc(bigdev + (sizeof(struct pnp_sysdevargs) - sizeof(struct pnp_sysdev)), M_DEVBUF, M_INTWAIT);
    pda = (struct pnp_sysdevargs *)devnodebuf;
    pd = &pda->node;

    for (currdev = 0, left = ndevs; (currdev != 0xff) && (left > 0); left--) {
	bzero(pd, bigdev);
	pda->next = currdev;

	/* get current configuration */
	if ((error = bios16(&args, PNP_GET_DEVNODE, &pda->next, &pda->node, 1))) {
	    kprintf("pnpbios: error %d making BIOS16 call\n", error);
	    break;
	}
	if (bootverbose)
	    kprintf("pnp_get_devnode cd=%d nxt=%d size=%d handle=%d devid=%08x type=%02x%02x%02x, attrib=%04x\n", currdev, pda->next, pd->size, pd->handle, pd->devid, pd->type[0], pd->type[1], pd->type[2], pd->attrib);

	if ((error = (args.r.eax & 0xff))) {
	    if (bootverbose)
		kprintf("pnpbios: %s 0x%x fetching node %d\n", error & 0x80 ? "error" : "warning", error, currdev);
	    if (error & 0x80) 
		break;
	}
	currdev = pda->next;
	if (pd->size < sizeof(struct pnp_sysdev)) {
	    kprintf("pnpbios: bogus system node data, aborting scan\n");
	    break;
	}
	
	/*
	 * Ignore PICs so that we don't have to worry about the PICs
	 * claiming IRQs to prevent their use.  The PIC drivers
	 * already ensure that invalid IRQs are not used.
	 */
	if (!strcmp(pnp_eisaformat(pd->devid), "PNP0000"))	/* ISA PIC */
	    continue;
	if (!strcmp(pnp_eisaformat(pd->devid), "PNP0003"))	/* APIC */
	    continue;

	/* Add the device and parse its resources */
	dev = BUS_ADD_CHILD(parent, parent, ISA_ORDER_PNP, NULL, -1);
	isa_set_vendorid(dev, pd->devid);
	isa_set_logicalid(dev, pd->devid);

	/*
	 * It appears that some PnP BIOS doesn't allow us to re-enable
	 * the embedded system device once it is disabled.  We shall
	 * mark all system device nodes as "cannot be disabled", regardless
	 * of actual settings in the device attribute byte.  XXX
	 */
#if 0
	isa_set_configattr(dev, 
	    ((pd->attrib & PNPATTR_NODISABLE) ?  0 : ISACFGATTR_CANDISABLE) |
	    ((!(pd->attrib & PNPATTR_NOCONFIG) && 
		PNPATTR_CONFIG(pd->attrib) != PNPATTR_CONFIG_STATIC)
		? ISACFGATTR_DYNAMIC : 0));
#endif
	isa_set_configattr(dev, 
	    (!(pd->attrib & PNPATTR_NOCONFIG) && 
		PNPATTR_CONFIG(pd->attrib) != PNPATTR_CONFIG_STATIC)
		? ISACFGATTR_DYNAMIC : 0);

	ISA_SET_CONFIG_CALLBACK(parent, dev, pnpbios_set_config, 0);
	pnp_parse_resources(dev, &pd->devdata[0],
			    pd->size - sizeof(struct pnp_sysdev), 0);
	if (!device_get_desc(dev))
	    device_set_desc_copy(dev, pnp_eisaformat(pd->devid));

	/* Find device IDs */
	devid = &pd->devid;
	compid = NULL;

	/* look for a compatible device ID too */
	left = pd->size - sizeof(struct pnp_sysdev);
	idx = 0;
	while (idx < left) {
	    tag = pd->devdata[idx++];
	    if (PNP_RES_TYPE(tag) == 0) {
		/* Small resource */
		switch (PNP_SRES_NUM(tag)) {
		case PNP_TAG_COMPAT_DEVICE:
		    compid = (uint32_t *)(pd->devdata + idx);
		    if (bootverbose)
			kprintf("pnpbios: node %d compat ID 0x%08x\n", pd->handle, *compid);
		    /* FALLTHROUGH */
		case PNP_TAG_END:
		    idx = left;
		    break;
		default:
		    idx += PNP_SRES_LEN(tag);
		    break;
		}
	    } else
		/* Large resource, skip it */
		idx += *(uint16_t *)(pd->devdata + idx) + 2;
	}
	if (bootverbose) {
	    kprintf("pnpbios: handle %d device ID %s (%08x)", 
		   pd->handle, pnp_eisaformat(*devid), *devid);
	    if (compid != NULL)
		kprintf(" compat ID %s (%08x)",
		       pnp_eisaformat(*compid), *compid);
	    kprintf("\n");
	}
    }
    return (0);
}

static device_method_t pnpbios_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	pnpbios_identify),

	{ 0, 0 }
};

static driver_t pnpbios_driver = {
	"pnpbios",
	pnpbios_methods,
	1,			/* no softc */
};

static devclass_t pnpbios_devclass;

DRIVER_MODULE(pnpbios, isa, pnpbios_driver, pnpbios_devclass, NULL, NULL);
