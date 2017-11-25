/*-
 * Copyright (c) 1998 - 2006 Søren Schmidt <sos@FreeBSD.org>
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
 * $FreeBSD: src/sys/dev/ata/ata-chipset.c,v 1.196 2007/04/08 19:18:51 sos Exp $
 */

#include "opt_ata.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/bus_dma.h>
#include <sys/bus_resource.h>
#include <sys/callout.h>
#include <sys/endian.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/nata.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/machintr.h>

#include <machine/bus_dma.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include "ata-all.h"
#include "ata-pci.h"
#include "ata_if.h"

/* ATA_NO_SMTH helper */
#define ATA_IDENT_DUMMY(name) int ata_ ## name ## _ident	\
				(device_t x __unused){return 1;}

/* local prototypes */
/* ata-chipset.c */
static int ata_generic_chipinit(device_t dev);
static void ata_generic_setmode(device_t dev, int mode);

#if !defined(ATA_NO_AHCI)
/* used by ata-{ahci,acerlabs,ati,intel,jmicron,via}.c */
static int ata_ahci_chipinit(device_t dev);

/* used by ata-ahci.c and ata-jmicron.c */
static int ata_ahci_allocate(device_t dev);
static void ata_ahci_dmainit(device_t dev);
static void ata_ahci_reset(device_t dev);
#endif

#if !defined(ATA_NO_MARVELL)
/* ata-adaptec.c depends on ata-marwell.c */
static int ata_marvell_edma_chipinit(device_t dev);
#endif

#if !defined(ATA_NO_SILICONIMAGE)
/* ata-ati.c depends on ata-siliconimage.c */
/* used by ata-ati.c and ata-siliconimage.c */
static int ata_sii_chipinit(device_t dev);
#endif


/*
 * ahci capable chipset support functions (needed for some vendor chipsets)
 */
#if !defined(ATA_NO_AHCI)
#include "chipsets/ata-ahci.c"
#else
ATA_IDENT_DUMMY(ahci)
#endif

/*
 * various vendor specific chipset support functions
 */
#if !defined(ATA_NO_ACARD)
#include "chipsets/ata-acard.c"
#else
ATA_IDENT_DUMMY(acard)
#endif

#if !defined(ATA_NO_ACERLABS) && !defined(ATA_NO_AHCI)
#include "chipsets/ata-acerlabs.c"
#else
ATA_IDENT_DUMMY(ali)
#endif

#if !defined(ATA_NO_AMD)
#include "chipsets/ata-amd.c"
#else
ATA_IDENT_DUMMY(amd)
#endif

#if !defined(ATA_NO_AHCI) && !defined(ATA_NO_SILICONIMAGE)
#include "chipsets/ata-ati.c"
#else
ATA_IDENT_DUMMY(ati)
#endif

#if !defined(ATA_NO_CYPRESS)
#include "chipsets/ata-cypress.c"
#else
ATA_IDENT_DUMMY(cypress)
#endif

#if !defined(ATA_NO_CYRIX)
#include "chipsets/ata-cyrix.c"
#else
ATA_IDENT_DUMMY(cyrix)
#endif

#if !defined(ATA_NO_HIGHPOINT)
#include "chipsets/ata-highpoint.c"
#else
ATA_IDENT_DUMMY(highpoint)
#endif

#if !defined(ATA_NO_INTEL) && !defined(ATA_NO_AHCI)
#include "chipsets/ata-intel.c"
#else
ATA_IDENT_DUMMY(intel)
#endif

#if !defined(ATA_NO_ITE)
#include "chipsets/ata-ite.c"
#else
ATA_IDENT_DUMMY(ite)
#endif

#if !defined(ATA_NO_JMICRON) && !defined(ATA_NO_AHCI)
#include "chipsets/ata-jmicron.c"
#else
ATA_IDENT_DUMMY(jmicron)
#endif

#if !defined(ATA_NO_MARVELL)
#include "chipsets/ata-adaptec.c"
#include "chipsets/ata-marvell.c"
#else
ATA_IDENT_DUMMY(adaptec)
ATA_IDENT_DUMMY(marvell)
#endif

#if !defined(ATA_NO_NATIONAL)
#include "chipsets/ata-national.c"
#else
ATA_IDENT_DUMMY(national)
#endif

#if !defined(ATA_NO_NETCELL)
#include "chipsets/ata-netcell.c"
#else
ATA_IDENT_DUMMY(netcell)
#endif

#if !defined(ATA_NO_NVIDIA)
#include "chipsets/ata-nvidia.c"
#else
ATA_IDENT_DUMMY(nvidia)
#endif

#if !defined(ATA_NO_PROMISE)
#include "chipsets/ata-promise.c"
#else
ATA_IDENT_DUMMY(promise)
#endif

#if !defined(ATA_NO_SERVERWORKS)
#include "chipsets/ata-serverworks.c"
#else
ATA_IDENT_DUMMY(serverworks)
#endif

#if !defined(ATA_NO_SILICONIMAGE)
#include "chipsets/ata-siliconimage.c"
#else
ATA_IDENT_DUMMY(sii)
#endif

#if !defined(ATA_NO_SIS)
#include "chipsets/ata-sis.c"
#else
ATA_IDENT_DUMMY(sis)
#endif

#if !defined(ATA_NO_VIA) && !defined(ATA_NO_AHCI)
#include "chipsets/ata-via.c"
#else
ATA_IDENT_DUMMY(via)
#endif

/*
 * various vendor specific chipset support functions based on generic ATA
 */

#include "chipsets/ata-cenatek.c"
#include "chipsets/ata-micron.c"

/*
 * generic ATA support functions
 */
int
ata_generic_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    char buffer[64];

    ksnprintf(buffer, sizeof(buffer),
	      "%s ATA controller", ata_pcivendor2str(dev));
    device_set_desc_copy(dev, buffer);
    ctlr->chipinit = ata_generic_chipinit;
    return 0;
}

static int
ata_generic_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;
    ctlr->setmode = ata_generic_setmode;
    return 0;
}

static void
ata_generic_setmode(device_t dev, int mode)
{
    struct ata_device *atadev = device_get_softc(dev);

    mode = ata_limit_mode(dev, mode, ATA_UDMA2);
    mode = ata_check_80pin(dev, mode);
    if (!ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode))
	atadev->mode = mode;
}
