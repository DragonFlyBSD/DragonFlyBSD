/*-
 * Copyright (c) 2009 Yahoo! Inc.
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
 * $FreeBSD: src/sys/dev/mps/mps_pci.c,v 1.4 2012/01/26 18:17:21 ken Exp $
 */

/* PCI/PCI-X/PCIe bus interface for the LSI MPT2 controllers */

/* TODO Move headers to mpsvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pci_private.h>

#include <dev/raid/mps/mpi/mpi2_type.h>
#include <dev/raid/mps/mpi/mpi2.h>
#include <dev/raid/mps/mpi/mpi2_ioc.h>
#include <dev/raid/mps/mpi/mpi2_cnfg.h>
#include <dev/raid/mps/mpi/mpi2_tool.h>

#include <sys/queue.h>
#include <sys/kthread.h>
#include <dev/raid/mps/mps_ioctl.h>
#include <dev/raid/mps/mpsvar.h>

static int	mps_pci_probe(device_t);
static int	mps_pci_attach(device_t);
static int	mps_pci_detach(device_t);
static int	mps_pci_suspend(device_t);
static int	mps_pci_resume(device_t);
static void	mps_pci_free(struct mps_softc *);
#if 0 /* XXX swildner */
static int	mps_alloc_msix(struct mps_softc *sc, int msgs);
#endif

static device_method_t mps_methods[] = {
	DEVMETHOD(device_probe,		mps_pci_probe),
	DEVMETHOD(device_attach,	mps_pci_attach),
	DEVMETHOD(device_detach,	mps_pci_detach),
	DEVMETHOD(device_suspend,	mps_pci_suspend),
	DEVMETHOD(device_resume,	mps_pci_resume),

	DEVMETHOD_END
};

static driver_t mps_pci_driver = {
	"mps",
	mps_methods,
	sizeof(struct mps_softc)
};

static devclass_t	mps_devclass;
DRIVER_MODULE(mps, pci, mps_pci_driver, mps_devclass, NULL, NULL);
MODULE_VERSION(mps, 1);

struct mps_ident {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
	u_int		flags;
	const char	*desc;
} mps_identifiers[] = {
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
	    0xffff, 0xffff, 0, "LSI SAS2004" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2008,
	    0xffff, 0xffff, 0, "LSI SAS2008" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_1,
	    0xffff, 0xffff, 0, "LSI SAS2108" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_2,
	    0xffff, 0xffff, 0, "LSI SAS2108" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_3,
	    0xffff, 0xffff, 0, "LSI SAS2108" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_1,
	    0xffff, 0xffff, 0, "LSI SAS2116" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_2,
	    0xffff, 0xffff, 0, "LSI SAS2116" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_1,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_2,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_3,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_4,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_5,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_6,
	    0xffff, 0xffff, 0, "LSI SAS2208" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_1,
	    0xffff, 0xffff, 0, "LSI SAS2308" },
	// Add Customer specific vender/subdevice id before generic
	// (0xffff) vender/subdevice id.
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
	    0x8086, 0x3516, 0, "Intel(R) Integrated RAID Module RMS25JB080" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
	    0x8086, 0x3517, 0, "Intel(R) Integrated RAID Module RMS25JB040" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
	    0x8086, 0x3518, 0, "Intel(R) Integrated RAID Module RMS25KB080" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
	    0x8086, 0x3519, 0, "Intel(R) Integrated RAID Module RMS25KB040" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
	    0xffff, 0xffff, 0, "LSI SAS2308" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_3,
	    0xffff, 0xffff, 0, "LSI SAS2308" },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SSS6200,
	    0xffff, 0xffff, MPS_FLAGS_WD_AVAILABLE, "LSI SSS6200" },
	{ 0, 0, 0, 0, 0, NULL }
};

static struct mps_ident *
mps_find_ident(device_t dev)
{
	struct mps_ident *m;

	for (m = mps_identifiers; m->vendor != 0; m++) {
		if (m->vendor != pci_get_vendor(dev))
			continue;
		if (m->device != pci_get_device(dev))
			continue;
		if ((m->subvendor != 0xffff) &&
		    (m->subvendor != pci_get_subvendor(dev)))
			continue;
		if ((m->subdevice != 0xffff) &&
		    (m->subdevice != pci_get_subdevice(dev)))
			continue;
		return (m);
	}

	return (NULL);
}

static int
mps_pci_probe(device_t dev)
{
	struct mps_ident *id;

	if ((id = mps_find_ident(dev)) != NULL) {
		device_set_desc(dev, id->desc);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
mps_pci_attach(device_t dev)
{
	struct mps_softc *sc;
	struct mps_ident *m;
	uint16_t command;
	int error;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	sc->mps_dev = dev;
	m = mps_find_ident(dev);
	sc->mps_flags = m->flags;

	/* Twiddle basic PCI config bits for a sanity check */
	command = pci_read_config(dev, PCIR_COMMAND, 2);
	command |= PCIM_CMD_BUSMASTEREN;
	pci_write_config(dev, PCIR_COMMAND, command, 2);
	command = pci_read_config(dev, PCIR_COMMAND, 2);
	if ((command & PCIM_CMD_BUSMASTEREN) == 0) {
		device_printf(dev, "Cannot enable PCI busmaster\n");
		return (ENXIO);
	}
	if ((command & PCIM_CMD_MEMEN) == 0) {
		device_printf(dev, "PCI memory window not available\n");
		return (ENXIO);
	}

	/* Allocate the System Interface Register Set */
	sc->mps_regs_rid = PCIR_BAR(1);
	if ((sc->mps_regs_resource = bus_alloc_resource_any(dev,
	    SYS_RES_MEMORY, &sc->mps_regs_rid, RF_ACTIVE)) == NULL) {
		device_printf(dev, "Cannot allocate PCI registers\n");
		return (ENXIO);
	}
	sc->mps_btag = rman_get_bustag(sc->mps_regs_resource);
	sc->mps_bhandle = rman_get_bushandle(sc->mps_regs_resource);

	/* Allocate the parent DMA tag */
	if (bus_dma_tag_create(NULL,			/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
				BUS_SPACE_UNRESTRICTED,	/* nsegments */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
				0,			/* flags */
				&sc->mps_parent_dmat)) {
		device_printf(dev, "Cannot allocate parent DMA tag\n");
		mps_pci_free(sc);
		return (ENOMEM);
	}

	if ((error = mps_attach(sc)) != 0)
		mps_pci_free(sc);

	return (error);
}

int
mps_pci_setup_interrupts(struct mps_softc *sc)
{
	device_t dev;
	int error;
	u_int irq_flags;

	dev = sc->mps_dev;
#if 0 /* XXX swildner */
	if ((sc->disable_msix == 0) &&
	    ((msgs = pci_msix_count(dev)) >= MPS_MSI_COUNT))
		error = mps_alloc_msix(sc, MPS_MSI_COUNT);
#endif

	sc->mps_irq_rid[0] = 0;
	sc->mps_irq_type[0] = pci_alloc_1intr(dev, sc->enable_msi,
	    &sc->mps_irq_rid[0], &irq_flags);
	sc->mps_irq[0] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->mps_irq_rid[0],  irq_flags);
	if (sc->mps_irq[0] == NULL) {
		device_printf(dev, "Cannot allocate interrupt\n");
		return (ENXIO);
	}
	error = bus_setup_intr(dev, sc->mps_irq[0], INTR_MPSAFE, mps_intr, sc,
	    &sc->mps_intrhand[0], NULL);
	if (error)
		device_printf(dev, "Cannot setup interrupt\n");

	return (error);
}

static int
mps_pci_detach(device_t dev)
{
	struct mps_softc *sc;
	int error;

	sc = device_get_softc(dev);

	if ((error = mps_free(sc)) != 0)
		return (error);

	mps_pci_free(sc);
	return (0);
}

static void
mps_pci_free(struct mps_softc *sc)
{
	if (sc->mps_parent_dmat != NULL) {
		bus_dma_tag_destroy(sc->mps_parent_dmat);
	}

	bus_teardown_intr(sc->mps_dev, sc->mps_irq[0], sc->mps_intrhand[0]);
	bus_release_resource(sc->mps_dev, SYS_RES_IRQ, sc->mps_irq_rid[0],
	    sc->mps_irq[0]);
	if (sc->mps_irq_type[0] == PCI_INTR_TYPE_MSI)
		pci_release_msi(sc->mps_dev);

	if (sc->mps_regs_resource != NULL) {
		bus_release_resource(sc->mps_dev, SYS_RES_MEMORY,
		    sc->mps_regs_rid, sc->mps_regs_resource);
	}

	return;
}

static int
mps_pci_suspend(device_t dev)
{
	return (EINVAL);
}

static int
mps_pci_resume(device_t dev)
{
	return (EINVAL);
}

#if 0 /* XXX swildner */
static int
mps_alloc_msix(struct mps_softc *sc, int msgs)
{
	int error;

	error = pci_alloc_msix(sc->mps_dev, &msgs);
	return (error);
}
#endif

int
mps_pci_restore(struct mps_softc *sc)
{
	struct pci_devinfo *dinfo;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	dinfo = device_get_ivars(sc->mps_dev);
	if (dinfo == NULL) {
		mps_dprint(sc, MPS_FAULT, "%s: NULL dinfo\n", __func__);
		return (EINVAL);
	}

	pci_cfg_restore(sc->mps_dev, dinfo);
	return (0);
}
