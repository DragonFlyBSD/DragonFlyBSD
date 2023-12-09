/*
 * (MPSAFE)
 *
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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
 * $OpenBSD: sili.c,v 1.147 2009/02/16 21:19:07 miod Exp $
 */

#include "sili.h"

static int	sili_pci_attach(device_t);
static int	sili_pci_detach(device_t);

static const struct sili_device sili_devices[] = {
	{
		.ad_vendor = PCI_VENDOR_SII,
		.ad_product = PCI_PRODUCT_SII_3132,
		.ad_nports = 2,
		.ad_attach = sili_pci_attach,
		.ad_detach = sili_pci_detach,
		.name = "SiliconImage-3132-SATA"
	},
	{
		.ad_vendor = PCI_VENDOR_SII,
		.ad_product = 0x3124,
		.ad_nports = 4,
		.ad_attach = sili_pci_attach,
		.ad_detach = sili_pci_detach,
		.name = "Rosewill-3124-SATA"
	},
	{ 0, 0, 0, NULL, NULL, NULL }
};

/*
 * Don't enable MSI by default; it does not seem to
 * work at all on Silicon Image 3132.
 */
static int sili_msi_enable = 0;
TUNABLE_INT("hw.sili.msi.enable", &sili_msi_enable);

/*
 * Match during probe and attach.  The device does not yet have a softc.
 */
const struct sili_device *
sili_lookup_device(device_t dev)
{
	const struct sili_device *ad;
	u_int16_t vendor = pci_get_vendor(dev);
	u_int16_t product = pci_get_device(dev);
#if 0
	u_int8_t class = pci_get_class(dev);
	u_int8_t subclass = pci_get_subclass(dev);
	u_int8_t progif = pci_read_config(dev, PCIR_PROGIF, 1);
#endif

	for (ad = &sili_devices[0]; ad->ad_vendor; ++ad) {
		if (ad->ad_vendor == vendor && ad->ad_product == product)
			return (ad);
	}
	return (NULL);
#if 0
	/*
	 * Last ad is the default match if the PCI device matches SATA.
	 */
	if (class == PCIC_STORAGE && subclass == PCIS_STORAGE_SATA &&
	    progif == PCIP_STORAGE_SATA_SILI_1_0) {
		return (ad);
	}
	return (NULL);
#endif
}

static int
sili_pci_attach(device_t dev)
{
	struct sili_softc *sc = device_get_softc(dev);
	struct sili_port *ap;
	const char *gen;
	u_int32_t nports, reg;
	bus_addr_t addr;
	int i, error, msi_enable;
	u_int irq_flags;

	/*
	 * Map the SILI controller's IRQ, BAR(0) (global regs),
	 * and BAR(1) (port regs and lram).
	 */

	msi_enable = sili_msi_enable;
	if (!pci_is_pcie(dev)) {
		/*
		 * Don't enable MSI on PCI devices by default;
		 * well, this may cause less trouble.
		 */
		msi_enable = 0;
	}
	sc->sc_irq_type = pci_alloc_1intr(dev, msi_enable, &sc->sc_rid_irq,
	    &irq_flags);

	sc->sc_dev = dev;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_rid_irq,
	    irq_flags);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "unable to map interrupt\n");
		sili_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * When mapping the register window store the tag and handle
	 * separately so we can use the tag with per-port bus handle
	 * sub-spaces.
	 */
	sc->sc_rid_regs = PCIR_BAR(0);
	sc->sc_regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					     &sc->sc_rid_regs, RF_ACTIVE);
	if (sc->sc_regs == NULL) {
		device_printf(dev, "unable to map registers\n");
		sili_pci_detach(dev);
		return (ENXIO);
	}
	sc->sc_iot = rman_get_bustag(sc->sc_regs);
	sc->sc_ioh = rman_get_bushandle(sc->sc_regs);

	sc->sc_rid_pregs = PCIR_BAR(2);
	sc->sc_pregs = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					      &sc->sc_rid_pregs, RF_ACTIVE);
	if (sc->sc_pregs == NULL) {
		device_printf(dev, "unable to map port registers\n");
		sili_pci_detach(dev);
		return (ENXIO);
	}
	sc->sc_piot = rman_get_bustag(sc->sc_pregs);
	sc->sc_pioh = rman_get_bushandle(sc->sc_pregs);

	/*
	 * Initialize the chipset and then set the interrupt vector up
	 */
	error = sili_init(sc);
	if (error) {
		sili_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * We assume at least 4 commands.
	 */
	sc->sc_ncmds = SILI_MAX_CMDS;
	sc->sc_flags |= SILI_F_64BIT;
	sc->sc_flags |= SILI_F_NCQ;
	sc->sc_flags |= SILI_F_SSNTF;
	sc->sc_flags |= SILI_F_SPM;

	addr = (sc->sc_flags & SILI_F_64BIT) ?
		BUS_SPACE_MAXADDR : BUS_SPACE_MAXADDR_32BIT;

	/*
	 * DMA tags for allocation of DMA memory buffers, lists, and so
	 * forth.  These are typically per-port.
	 *
	 * The stuff is mostly built into the BAR mappings.  We only need
	 * tags for our external SGE list and data.
	 */
	error = 0;
	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			256,				/* alignment */
			65536,				/* boundary */
			addr,				/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			sizeof(struct sili_prb) * SILI_MAX_CMDS,
							/* [max]size */
			1,				/* maxsegs */
			sizeof(struct sili_prb) * SILI_MAX_CMDS,
							/* maxsegsz */
			0,				/* flags */
			&sc->sc_tag_prbs);		/* return tag */

	/*
	 * The data tag is used for later dmamaps and not immediately
	 * allocated.
	 */
	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			4,				/* alignment */
			0,				/* boundary */
			addr,				/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			4096 * 1024,			/* maxiosize */
			SILI_MAX_SGET,			/* maxsegs */
			65536,				/* maxsegsz */
			0,				/* flags */
			&sc->sc_tag_data);		/* return tag */

	if (error) {
		device_printf(dev, "unable to create dma tags\n");
		sili_pci_detach(dev);
		return (ENXIO);
	}

	if (sili_read(sc, SILI_REG_GCTL) & SILI_REG_GCTL_300CAP) {
		gen = "1 (1.5Gbps) and 2 (3Gbps)";
		sc->sc_flags |= SILI_F_300;
	} else {
		gen = "1 (1.5Gbps)";
	}

	nports = sc->sc_ad->ad_nports;
	KKASSERT(nports <= SILI_MAX_PORTS);

	device_printf(dev, "ports=%d tags=31, gen %s, cap=NCQ,FBSS,SPM\n",
	    nports, gen);

	/*
	 * Allocate per-port resources
	 *
	 * All ports are attached in parallel but the CAM scan-bus
	 * is held up until all ports are attached so we get a deterministic
	 * order.
	 */
	for (i = 0; error == 0 && i < nports; i++) {
		error = sili_port_alloc(sc, i);
	}

	/*
	 * Setup the interrupt vector and enable interrupts.  Note that
	 * since the irq may be shared we do not set it up until we are
	 * ready to go.
	 */
	if (error == 0) {
		error = bus_setup_intr(dev, sc->sc_irq, INTR_MPSAFE,
				       sili_intr, sc,
				       &sc->sc_irq_handle, NULL);
	}

	if (error) {
		device_printf(dev, "unable to install interrupt\n");
		sili_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * Interrupt subsystem is good to go now, enable all port interrupts
	 */
	crit_enter();
	reg = sili_read(sc, SILI_REG_GCTL);
	for (i = 0; i < nports; ++i)
		reg |= SILI_REG_GCTL_PORTEN(i);
	sili_write(sc, SILI_REG_GCTL, reg);
	sc->sc_flags |= SILI_F_INT_GOOD;
	crit_exit();
	sili_intr(sc);

	/*
	 * All ports are probing in parallel.  Wait for them to finish
	 * and then issue the cam attachment and bus scan serially so
	 * the 'da' assignments are deterministic.
	 */
	for (i = 0; i < nports; i++) {
		if ((ap = sc->sc_ports[i]) != NULL) {
			while (ap->ap_signal & AP_SIGF_INIT)
				tsleep(&ap->ap_signal, 0, "ahprb1", hz);
			sili_os_lock_port(ap);
			if (sili_cam_attach(ap) == 0) {
				sili_cam_changed(ap, NULL, -1);
				sili_os_unlock_port(ap);
				while ((ap->ap_flags & AP_F_SCAN_COMPLETED) == 0) {
					tsleep(&ap->ap_flags, 0, "ahprb2", hz);
				}
			} else {
				sili_os_unlock_port(ap);
			}
		}
	}

	return(0);
}

/*
 * Device unload / detachment
 */
static int
sili_pci_detach(device_t dev)
{
	struct sili_softc *sc = device_get_softc(dev);
	struct sili_port *ap;
	int	i;

	/*
	 * Disable the controller and de-register the interrupt, if any.
	 *
	 * XXX interlock last interrupt?
	 */
	sc->sc_flags &= ~SILI_F_INT_GOOD;
	if (sc->sc_regs)
		sili_write(sc, SILI_REG_GCTL, SILI_REG_GCTL_GRESET);

	if (sc->sc_irq_handle) {
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	/*
	 * Free port structures and DMA memory
	 */
	for (i = 0; i < SILI_MAX_PORTS; i++) {
		ap = sc->sc_ports[i];
		if (ap) {
			sili_cam_detach(ap);
			sili_port_free(sc, i);
		}
	}

	/*
	 * Clean up the bus space
	 */
	if (sc->sc_irq) {
		bus_release_resource(dev, SYS_RES_IRQ,
				     sc->sc_rid_irq, sc->sc_irq);
		sc->sc_irq = NULL;
	}
	if (sc->sc_irq_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);

	if (sc->sc_regs) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->sc_rid_regs, sc->sc_regs);
		sc->sc_regs = NULL;
	}
	if (sc->sc_pregs) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->sc_rid_pregs, sc->sc_pregs);
		sc->sc_regs = NULL;
	}

	if (sc->sc_tag_prbs) {
		bus_dma_tag_destroy(sc->sc_tag_prbs);
		sc->sc_tag_prbs = NULL;
	}
	if (sc->sc_tag_data) {
		bus_dma_tag_destroy(sc->sc_tag_data);
		sc->sc_tag_data = NULL;
	}

	return (0);
}
