/*
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <drm/drmP.h>
#include "drm_internal.h"
#include "drm_legacy.h"

/**********************************************************************/
/** \name PCI memory */
/*@{*/

static void
drm_pci_busdma_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	drm_dma_handle_t *dmah = arg;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("drm_pci_busdma_callback: bad dma segment count"));
	dmah->busaddr = segs[0].ds_addr;
}

/**
 * \brief Allocate a PCI consistent memory block, for DMA.
 */
drm_dma_handle_t *drm_pci_alloc(struct drm_device * dev, size_t size, size_t align)
{
	drm_dma_handle_t *dmah;
	int ret;

	/* pci_alloc_consistent only guarantees alignment to the smallest
	 * PAGE_SIZE order which is greater than or equal to the requested size.
	 * Return NULL here for now to make sure nobody tries for larger alignment
	 */
	if (align > size)
		return NULL;

	/* Need power-of-two alignment, so fail the allocation if it isn't. */
	if ((align & (align - 1)) != 0) {
		DRM_ERROR("drm_pci_alloc with non-power-of-two alignment %d\n",
		    (int)align);
		return NULL;
	}

	dmah = kmalloc(sizeof(drm_dma_handle_t), M_DRM, M_WAITOK | M_NULLOK);
	if (!dmah)
		return NULL;

	dmah->size = size;

	ret = bus_dma_tag_create(NULL, align, 0, /* tag, align, boundary */
	    ~0, BUS_SPACE_MAXADDR, /* lowaddr, highaddr */
	    NULL, NULL, /* filtfunc, filtfuncargs */
	    size, 1, size, /* maxsize, nsegs, maxsegsize */
	    0,		/* flags */
	    &dmah->tag);
	if (ret != 0) {
		kfree(dmah);
		return NULL;
	}

	ret = bus_dmamem_alloc(dmah->tag, &dmah->vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_NOCACHE, &dmah->map);
	if (ret != 0) {
		bus_dma_tag_destroy(dmah->tag);
		kfree(dmah);
		return NULL;
	}

	ret = bus_dmamap_load(dmah->tag, dmah->map, dmah->vaddr, size,
	    drm_pci_busdma_callback, dmah, BUS_DMA_NOWAIT);
	if (ret != 0) {
		bus_dmamem_free(dmah->tag, dmah->vaddr, dmah->map);
		bus_dma_tag_destroy(dmah->tag);
		kfree(dmah);
		return NULL;
	}

	memset(dmah->vaddr, 0, size);

	return dmah;
}

/*
 * Free a PCI consistent memory block without freeing its descriptor.
 *
 * This function is for internal use in the Linux-specific DRM core code.
 */
void __drm_legacy_pci_free(struct drm_device * dev, drm_dma_handle_t * dmah)
{
	if (dmah == NULL)
		return;

	bus_dmamem_free(dmah->tag, dmah->vaddr, dmah->map);
	bus_dma_tag_destroy(dmah->tag);
}

/**
 * drm_pci_free - Free a PCI consistent memory block
 * @dev: DRM device
 * @dmah: handle to memory block
 */
void drm_pci_free(struct drm_device * dev, drm_dma_handle_t * dmah)
{
	__drm_legacy_pci_free(dev, dmah);
	kfree(dmah);
}

int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *mask)
{
	device_t root;
	int pos;
	u32 lnkcap = 0, lnkcap2 = 0;

	*mask = 0;

	root = device_get_parent(dev->dev->bsddev);

	/* we've been informed via and serverworks don't make the cut */
	if (pci_get_vendor(root) == PCI_VENDOR_ID_VIA ||
	    pci_get_vendor(root) == PCI_VENDOR_ID_SERVERWORKS)
		return -EINVAL;

	pos = 0;
	pci_find_extcap(root, PCIY_EXPRESS, &pos);
	if (!pos)
		return -EINVAL;

	lnkcap = pci_read_config(root, pos + PCIER_LINKCAP, 4);
	lnkcap2 = pci_read_config(root, pos + PCIER_LINK_CAP2, 4);

	lnkcap &= PCIEM_LNKCAP_SPEED_MASK;
	lnkcap2 &= 0xfe;

#define	PCI_EXP_LNKCAP_SLS_2_5GB	PCIEM_LNKCAP_SPEED_2_5
#define	PCI_EXP_LNKCAP_SLS_5_0GB	PCIEM_LNKCAP_SPEED_5
#define	PCI_EXP_LNKCAP2_SLS_2_5GB 0x02	/* Supported Link Speed 2.5GT/s */
#define	PCI_EXP_LNKCAP2_SLS_5_0GB 0x04	/* Supported Link Speed 5.0GT/s */
#define	PCI_EXP_LNKCAP2_SLS_8_0GB 0x08	/* Supported Link Speed 8.0GT/s */

	if (lnkcap2) {	/* PCIe r3.0-compliant */
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_2_5GB)
			*mask |= DRM_PCIE_SPEED_25;
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_5_0GB)
			*mask |= DRM_PCIE_SPEED_50;
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_8_0GB)
			*mask |= DRM_PCIE_SPEED_80;
	} else {	/* pre-r3.0 */
		if (lnkcap & PCI_EXP_LNKCAP_SLS_2_5GB)
			*mask |= DRM_PCIE_SPEED_25;
		if (lnkcap & PCI_EXP_LNKCAP_SLS_5_0GB)
			*mask |= DRM_PCIE_SPEED_50;
	}

	DRM_INFO("probing gen 2 caps for device %x:%x = %x/%x\n", pci_get_vendor(root), pci_get_device(root), lnkcap, lnkcap2);
	return 0;
}
EXPORT_SYMBOL(drm_pcie_get_speed_cap_mask);

#if 0
int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw)
{
	struct pci_dev *root;
	u32 lnkcap;

	*mlw = 0;
	if (!dev->pdev)
		return -EINVAL;

	root = dev->pdev->bus->self;

	pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);

	*mlw = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	DRM_INFO("probing mlw for device %x:%x = %x\n", root->vendor, root->device, lnkcap);
	return 0;
}
EXPORT_SYMBOL(drm_pcie_get_max_link_width);
#endif

/**
 * Get interrupt from bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_irq_busid structure.
 * \return zero on success or a negative number on failure.
 *
 * Finds the PCI device with the specified bus id and gets its IRQ number.
 * This IOCTL is deprecated, and will now return EINVAL for any busid not equal
 * to that of the device that this DRM instance attached to.
 */
int drm_irq_by_busid(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_irq_busid *irq = data;

	if ((irq->busnum >> 8) != dev->pci_domain ||
	    (irq->busnum & 0xff) != dev->pci_bus ||
	    irq->devnum != dev->pci_slot ||
	    irq->funcnum != dev->pci_func)
		return -EINVAL;

	irq->irq = dev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n",
	    irq->busnum, irq->devnum, irq->funcnum, irq->irq);

	return 0;
}
