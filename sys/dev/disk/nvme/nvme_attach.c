/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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
 */

#include "nvme.h"

static int	nvme_pci_attach(device_t);
static int	nvme_pci_detach(device_t);

static const nvme_device_t nvme_devices[] = {
	/* Vendor-specific table goes here (see ahci for example) */
	{ 0, 0, nvme_pci_attach, nvme_pci_detach, "NVME-PCIe" }
};

static int	nvme_msix_enable = 1;
TUNABLE_INT("hw.nvme.msix.enable", &nvme_msix_enable);
static int	nvme_msi_enable = 0;
TUNABLE_INT("hw.nvme.msi.enable", &nvme_msi_enable);

TAILQ_HEAD(, nvme_softc) nvme_sc_list = TAILQ_HEAD_INITIALIZER(nvme_sc_list);
struct lock nvme_master_lock = LOCK_INITIALIZER("nvmstr", 0, 0);

static int last_global_cpu;

/*
 * Match during probe and attach.  The device does not yet have a softc.
 */
const nvme_device_t *
nvme_lookup_device(device_t dev)
{
	const nvme_device_t *ad;
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t product = pci_get_device(dev);
	uint8_t class = pci_get_class(dev);
	uint8_t subclass = pci_get_subclass(dev);
	uint8_t progif = pci_read_config(dev, PCIR_PROGIF, 1);
	int is_nvme;

	/*
	 * Generally speaking if the pci device does not identify as
	 * AHCI we skip it.
	 */
	if (class == PCIC_STORAGE && subclass == PCIS_STORAGE_NVM &&
	    progif == PCIP_STORAGE_NVM_ENTERPRISE_NVMHCI_1_0) {
		is_nvme = 1;
	} else {
		is_nvme = 0;
	}

	for (ad = &nvme_devices[0]; ad->vendor; ++ad) {
		if (ad->vendor == vendor && ad->product == product)
			return (ad);
	}

	/*
	 * Last ad is the default match if the PCI device matches SATA.
	 */
	if (is_nvme == 0)
		ad = NULL;
	return (ad);
}

/*
 * Attach functions.  They all eventually fall through to nvme_pci_attach().
 */
static int
nvme_pci_attach(device_t dev)
{
	nvme_softc_t *sc = device_get_softc(dev);
	uint32_t reg;
	int error;
	int msi_enable;
	int msix_enable;

#if 0
	if (pci_read_config(dev, PCIR_COMMAND, 2) & 0x0400) {
		device_printf(dev, "BIOS disabled PCI interrupt, "
				   "re-enabling\n");
		pci_write_config(dev, PCIR_COMMAND,
			pci_read_config(dev, PCIR_COMMAND, 2) & ~0x0400, 2);
	}
#endif

	sc->dev = dev;

	/*
	 * Map the register window
	 */
	sc->rid_regs = PCIR_BAR(0);
	sc->regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					  &sc->rid_regs, RF_ACTIVE);
	if (sc->regs == NULL) {
		device_printf(dev, "unable to map registers\n");
		nvme_pci_detach(dev);
		return (ENXIO);
	}
	sc->iot = rman_get_bustag(sc->regs);
	sc->ioh = rman_get_bushandle(sc->regs);

	/*
	 * NVMe allows the MSI-X table to be mapped to BAR 4/5.
	 * Always try to map BAR4, but it's ok if it fails.  Must
	 * be done prior to allocating our interrupts.
	 */
	sc->rid_bar4 = PCIR_BAR(4);
	sc->bar4 = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					  &sc->rid_bar4, RF_ACTIVE);

	/*
	 * Map the interrupt or initial interrupt which will be used for
	 * the admin queue.  NVME chipsets can potentially support a huge
	 * number of MSIX vectors but we really only need enough for
	 * available cpus, plus 1.
	 */
	msi_enable = device_getenv_int(dev, "msi.enable", nvme_msi_enable);
	msix_enable = device_getenv_int(dev, "msix.enable", nvme_msix_enable);

	error = 0;
	if (msix_enable) {
		int i;
		int cpu;

		sc->nirqs = pci_msix_count(dev);
		sc->irq_type = PCI_INTR_TYPE_MSIX;
		if (sc->nirqs > ncpus + 1)		/* max we need */
			sc->nirqs = ncpus + 1;

		error = pci_setup_msix(dev);
		cpu = (last_global_cpu + 0) % ncpus;	/* GCC warn */
		for (i = 0; error == 0 && i < sc->nirqs; ++i) {
			cpu = (last_global_cpu + i) % ncpus;
			error = pci_alloc_msix_vector(dev, i,
						      &sc->rid_irq[i], cpu);
			if (error)
				break;
			sc->irq[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
							    &sc->rid_irq[i],
							    RF_ACTIVE);
			/*
			 * We want this to overwrite queue 0's cpu vector
			 * when the cpu's rotate through later on.
			 */
			if (sc->cputovect[cpu] == 0)
				sc->cputovect[cpu] = i;
		}

		/*
		 * If we did not iterate enough cpus (that is, there weren't
		 * enough irqs for all available cpus) we still need to
		 * finish or sc->cputovect[] mapping.
		 */
		while (error == 0) {
			cpu = (cpu + 1) % ncpus;
			i = (i + 1) % sc->nirqs;
			if (i == 0)
				i = 1;
			if (sc->cputovect[cpu] != 0)
				break;
			sc->cputovect[cpu] = i;
		}

		if (error) {
			while (--i >= 0) {
				bus_release_resource(dev, SYS_RES_IRQ,
						     sc->rid_irq[i],
						     sc->irq[i]);
				pci_release_msix_vector(dev, sc->rid_irq[i]);
				sc->irq[i] = NULL;
			}
			/* leave error intact to fall through to normal */
		} else {
			last_global_cpu = (last_global_cpu + sc->nirqs) % ncpus;
			pci_enable_msix(dev);
		}
	}

	/*
	 * If we have to use a normal interrupt we fake the cputovect[] in
	 * order to try to map at least (ncpus) submission queues.  The admin
	 * code will limit the number of completion queues to something
	 * reasonable when nirqs is 1 since the single interrupt polls all
	 * completion queues.
	 *
	 * NOTE: We do NOT want to map a single completion queue (#0), because
	 *	 then an I/O submission and/or completion queue will overlap
	 *	 the admin submission or completion queue, and that can cause
	 *	 havoc when admin commands are submitted that don't return
	 *	 for long periods of time.
	 *
	 * NOTE: Chipsets supporting MSI-X *MIGHT* *NOT* properly support
	 *	 a normal pin-based level interrupt.  For example, the BPX
	 *	 NVMe SSD just leaves the level interrupt stuck on.  Do not
	 *	 disable MSI-X unless you have no choice.
	 */
	if (msix_enable == 0 || error) {
		uint32_t irq_flags;
		int i;

		error = 0;
		sc->nirqs = 1;
		sc->irq_type = pci_alloc_1intr(dev, msi_enable,
					       &sc->rid_irq[0], &irq_flags);
		sc->irq[0] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						 &sc->rid_irq[0], irq_flags);

		for (i = 0; i < ncpus; ++i)
			sc->cputovect[i] = i + 1;
	}
	if (sc->irq[0] == NULL) {
		device_printf(dev, "unable to map interrupt\n");
		nvme_pci_detach(dev);
		return (ENXIO);
	} else {
		const char *type;
		switch(sc->irq_type) {
		case PCI_INTR_TYPE_MSI:
			type = "MSI";
			break;
		case PCI_INTR_TYPE_MSIX:
			type = "MSIX";
			break;
		default:
			type = "normal-int";
			break;
		}
		device_printf(dev, "mapped %d %s IRQs\n", sc->nirqs, type);
	}

	/*
	 * Make sure the chip is disabled, which will reset all controller
	 * registers except for the admin queue registers.  Device should
	 * already be disabled so this is usually instantanious.  Use a
	 * fixed 5-second timeout in case it is not.  I'd like my other
	 * reads to occur after the device has been disabled.
	 */
	sc->entimo = hz * 5;
	error = nvme_enable(sc, 0);
	if (error) {
		nvme_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * Get capabillities and version and report
	 */
	sc->vers = nvme_read(sc, NVME_REG_VERS);
	sc->cap = nvme_read8(sc, NVME_REG_CAP);
	sc->maxqe = NVME_CAP_MQES_GET(sc->cap);
	sc->dstrd4 = NVME_CAP_DSTRD_GET(sc->cap);

	device_printf(dev, "NVME Version %u.%u maxqe=%u caps=%016jx\n",
		      NVME_VERS_MAJOR_GET(sc->vers),
		      NVME_VERS_MINOR_GET(sc->vers),
		      sc->maxqe, sc->cap);

	/*
	 * Enable timeout, 500ms increments.  Convert to ticks.
	 */
	sc->entimo = NVME_CAP_TIMEOUT_GET(sc->cap) * hz / 2; /* in ticks */
	++sc->entimo;		/* fudge */

	/*
	 * Validate maxqe.  To cap the amount of memory we reserve for
	 * PRPs we limit maxqe to 256.  Also make sure it is a power of
	 * two.
	 */
	if (sc->maxqe < 2) {
		device_printf(dev,
			      "Attach failed, max queue entries (%d) "
			      "below minimum (2)\n", sc->maxqe);
		nvme_pci_detach(dev);
		return (ENXIO);
	}
	if (sc->maxqe > 256)
		sc->maxqe = 256;
	for (reg = 2; reg <= sc->maxqe; reg <<= 1)
		;
	sc->maxqe = reg >> 1;

	/*
	 * DMA tags
	 *
	 * PRP	- Worst case PRPs needed per queue is MAXPHYS / PAGE_SIZE
	 *	  (typically 64), multiplied by maxqe (typ 256).  Roughly
	 *	  ~128KB per queue.  Align for cache performance.  We actually
	 *	  need one more PRP per queue entry worst-case to handle
	 *	  buffer overlap, but we have an extra one in the command
	 *	  structure so we don't have to calculate that out.
	 *
	 *	  Remember that we intend to allocate potentially many queues,
	 *	  so we don't want to bloat this too much.  A queue depth of
	 *	  256 is plenty.
	 *
	 * CMD - Storage for the submit queue.  maxqe * 64	(~16KB)
	 *
	 * RES - Storage for the completion queue.  maxqe * 16	(~4KB)
	 *
	 * ADM - Storage for admin command DMA data.  Maximum admin command
	 *	 DMA data is 4KB so reserve maxqe * 4KB (~1MB).  There is only
	 *	 one admin queue.
	 *
	 * NOTE: There are no boundary requirements for NVMe, but I specify a
	 *	 4MB boundary anyway because this reduces mass-bit flipping
	 *	 of address bits inside the controller when incrementing
	 *	 DMA addresses.  Why not?  Can't hurt.
	 */
	sc->prp_bytes = sizeof(uint64_t) * (MAXPHYS / PAGE_SIZE) * sc->maxqe;
	sc->cmd_bytes = sizeof(nvme_subq_item_t) * sc->maxqe;
	sc->res_bytes = sizeof(nvme_comq_item_t) * sc->maxqe;
	sc->adm_bytes = NVME_MAX_ADMIN_BUFFER * sc->maxqe;

	error = 0;

	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			PAGE_SIZE,			/* alignment */
			4 * 1024 * 1024,		/* boundary */
			BUS_SPACE_MAXADDR,		/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			sc->prp_bytes,			/* [max]size */
			1,				/* maxsegs */
			sc->prp_bytes,			/* maxsegsz */
			0,				/* flags */
			&sc->prps_tag);			/* return tag */

	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			PAGE_SIZE,			/* alignment */
			4 * 1024 * 1024,		/* boundary */
			BUS_SPACE_MAXADDR,		/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			sc->cmd_bytes,			/* [max]size */
			1,				/* maxsegs */
			sc->cmd_bytes,			/* maxsegsz */
			0,				/* flags */
			&sc->sque_tag);			/* return tag */

	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			PAGE_SIZE,			/* alignment */
			4 * 1024 * 1024,		/* boundary */
			BUS_SPACE_MAXADDR,		/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			sc->res_bytes,			/* [max]size */
			1,				/* maxsegs */
			sc->res_bytes,			/* maxsegsz */
			0,				/* flags */
			&sc->cque_tag);			/* return tag */

	error += bus_dma_tag_create(
			NULL,				/* parent tag */
			PAGE_SIZE,			/* alignment */
			4 * 1024 * 1024,		/* boundary */
			BUS_SPACE_MAXADDR,		/* loaddr? */
			BUS_SPACE_MAXADDR,		/* hiaddr */
			sc->adm_bytes,			/* [max]size */
			1,				/* maxsegs */
			sc->adm_bytes,			/* maxsegsz */
			0,				/* flags */
			&sc->adm_tag);			/* return tag */

	if (error) {
		device_printf(dev, "unable to create dma tags\n");
		nvme_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * Setup the admin queues (qid 0).
	 */
	error = nvme_alloc_subqueue(sc, 0);
	if (error) {
		device_printf(dev, "unable to allocate admin subqueue\n");
		nvme_pci_detach(dev);
		return (ENXIO);
	}
	error = nvme_alloc_comqueue(sc, 0);
	if (error) {
		device_printf(dev, "unable to allocate admin comqueue\n");
		nvme_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * Initialize the admin queue registers
	 */
	reg = NVME_ATTR_COM_SET(sc->maxqe) | NVME_ATTR_SUB_SET(sc->maxqe);
	nvme_write(sc, NVME_REG_ADM_ATTR, reg);
	nvme_write8(sc, NVME_REG_ADM_SUBADR, (uint64_t)sc->subqueues[0].psubq);
	nvme_write8(sc, NVME_REG_ADM_COMADR, (uint64_t)sc->comqueues[0].pcomq);

	/*
	 * qemu appears to require this, real hardware does not appear
	 * to require this.
	 */
	pci_enable_busmaster(dev);

	/*
	 * Other configuration registers
	 */
	reg = NVME_CONFIG_IOSUB_ES_SET(6) |		/* 64 byte sub entry */
	      NVME_CONFIG_IOCOM_ES_SET(4) |		/* 16 byte com entry */
	      NVME_CONFIG_MEMPG_SET(PAGE_SHIFT) |	/* 4K pages */
	      NVME_CONFIG_CSS_NVM;			/* NVME command set */
	nvme_write(sc, NVME_REG_CONFIG, reg);

	reg = nvme_read(sc, NVME_REG_MEMSIZE);

	/*
	 * Enable the chip for operation
	 */
	error = nvme_enable(sc, 1);
	if (error) {
		nvme_enable(sc, 0);
		nvme_pci_detach(dev);
		return (ENXIO);
	}

	/*
	 * Start the admin thread.  This will also setup the admin queue
	 * interrupt.
	 */
	error = nvme_start_admin_thread(sc);
	if (error) {
		nvme_pci_detach(dev);
		return (ENXIO);
	}
	lockmgr(&nvme_master_lock, LK_EXCLUSIVE);
	sc->flags |= NVME_SC_ATTACHED;
	TAILQ_INSERT_TAIL(&nvme_sc_list, sc, entry);
	lockmgr(&nvme_master_lock, LK_RELEASE);

	return(0);
}

/*
 * Device unload / detachment
 */
static int
nvme_pci_detach(device_t dev)
{
	nvme_softc_t *sc = device_get_softc(dev);
	int i;

	/*
	 * Stop the admin thread
	 */
	nvme_stop_admin_thread(sc);

	/*
	 * Issue a normal shutdown and wait for completion
	 */
	nvme_issue_shutdown(sc, 0);

	/*
	 * Disable the chip
	 */
	nvme_enable(sc, 0);

	/*
	 * Free admin memory
	 */
	nvme_free_subqueue(sc, 0);
	nvme_free_comqueue(sc, 0);

	/*
	 * Release related resources.
	 */
	for (i = 0; i < sc->nirqs; ++i) {
		if (sc->irq[i]) {
			bus_release_resource(dev, SYS_RES_IRQ,
					     sc->rid_irq[i], sc->irq[i]);
			sc->irq[i] = NULL;
			if (sc->irq_type == PCI_INTR_TYPE_MSIX)
				pci_release_msix_vector(dev, sc->rid_irq[i]);
		}
	}
	switch(sc->irq_type) {
	case PCI_INTR_TYPE_MSI:
		pci_release_msi(dev);
		break;
	case PCI_INTR_TYPE_MSIX:
		pci_teardown_msix(dev);
		break;
	default:
		break;
	}

	/*
	 * Release remaining chipset resources
	 */
	if (sc->regs) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->rid_regs, sc->regs);
		sc->regs = NULL;
	}
	if (sc->bar4) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->rid_bar4, sc->bar4);
		sc->bar4 = NULL;
	}

	/*
	 * Cleanup the DMA tags
	 */
	if (sc->prps_tag) {
		bus_dma_tag_destroy(sc->prps_tag);
		sc->prps_tag = NULL;
	}
	if (sc->sque_tag) {
		bus_dma_tag_destroy(sc->sque_tag);
		sc->sque_tag = NULL;
	}
	if (sc->cque_tag) {
		bus_dma_tag_destroy(sc->cque_tag);
		sc->cque_tag = NULL;
	}
	if (sc->adm_tag) {
		bus_dma_tag_destroy(sc->adm_tag);
		sc->adm_tag = NULL;
	}

	if (sc->flags & NVME_SC_ATTACHED) {
		lockmgr(&nvme_master_lock, LK_EXCLUSIVE);
		sc->flags &= ~NVME_SC_ATTACHED;
		TAILQ_REMOVE(&nvme_sc_list, sc, entry);
		lockmgr(&nvme_master_lock, LK_RELEASE);
	}

	return (0);
}
