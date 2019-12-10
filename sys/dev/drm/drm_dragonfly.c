/*
 * Copyright (c) 2015 Imre Vadász <imre@vdsz.com>
 * Copyright (c) 2015 Rimvydas Jasinskas
 * Copyright (c) 2018 François Tigeot <ftigeot@wolfpond.org>
 *
 * DRM Dragonfly-specific helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <sys/libkern.h>
#include <sys/ctype.h>
#include <drm/drmP.h>

/*
 * An implementation of fb_get_options()
 * This can be used to set the video mode used for the syscons fb console,
 * a la "video=..." in linux.
 */
int
fb_get_options(const char *connector_name, char **option)
{
	char buf[128], str[1024];

	/*
	 * Where on linux one would use the command line option
	 * video=LVDS-1:<video-mode>, the corresponding tunable is
	 * drm.video.LVDS-1=<video-mode>.
	 * e.g. drm.video.LVDS-1=1024x768 sets the LVDS-1 connector to
	 * a 1024x768 video mode in the syscons framebuffer console.
	 * See https://wiki.archlinux.org/index.php/Kernel_mode_setting
	 * for an explanation of the video mode command line option.
	 */
	memset(str, 0, sizeof(str));
	ksnprintf(buf, sizeof(buf), "drm.video.%s", connector_name);
	if (kgetenv_string(buf, str, sizeof(str)-1)) {
		kprintf("found kenv %s=%s\n", buf, str);
		*option = kstrdup(str, M_DRM);
		return (0);
	} else {
		kprintf("tunable %s is not set\n", buf);
		return (1);
	}
}

/*
 * Implement simplified version of kvasnprintf() for drm needs using
 * M_DRM and kvsnprintf(). Since it is unclear what string size is
 * optimal thus use of an actual length.
 */
char *kvasprintf(int flags, const char *format, va_list ap)
{
	char *str;
	size_t size;
	va_list aq;

	va_copy(aq, ap);
	size = kvsnprintf(NULL, 0, format, aq);
	va_end(aq);

	str = kmalloc(size+1, M_DRM, flags);
	if (str == NULL)
		return NULL;

	kvsnprintf(str, size+1, format, ap);

	return str;
}

/* mimic ksnprintf(), return pointer to char* and match drm api */
char *kasprintf(int flags, const char *format, ...)
{
	char *str;
	va_list ap;

	va_start(ap, format);
	str = kvasprintf(flags, format, ap);
	va_end(ap);

	return str;
}

/*
 * XXX pci glue logic helpers
 * Should be done in drm_pci_init(), pending drm update.
 * Assumes static runtime data.
 * Only for usage in *_driver_[un]load()
 */

static void drm_fill_pdev(device_t dev, struct pci_dev *pdev)
{
	int msi_enable = 1;
	u_int irq_flags;
	int slot, func;

	pdev->dev.bsddev = dev;
	pdev->devfn = PCI_DEVFN(pci_get_slot(dev), pci_get_function(dev));
	pdev->vendor = pci_get_vendor(dev);
	pdev->device = pci_get_device(dev);
	pdev->subsystem_vendor = pci_get_subvendor(dev);
	pdev->subsystem_device = pci_get_subdevice(dev);

	pdev->revision = pci_get_revid(dev) & 0xff;

	pdev->_irq_type = pci_alloc_1intr(dev, msi_enable,
	    &pdev->_irqrid, &irq_flags);

	pdev->_irqr = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &pdev->_irqrid, irq_flags);
	if (!pdev->_irqr)
		return;

	pdev->irq = (int)rman_get_start(pdev->_irqr);

	slot = pci_get_slot(dev);
	func = pci_get_function(dev);
	pdev->devfn = PCI_DEVFN(slot, func);
}

void drm_init_pdev(device_t dev, struct pci_dev **pdev)
{
	BUG_ON(*pdev != NULL);

	*pdev = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);
	drm_fill_pdev(dev, *pdev);

	(*pdev)->bus = kzalloc(sizeof(struct pci_bus), GFP_KERNEL);
	(*pdev)->bus->self = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);

	drm_fill_pdev(device_get_parent(dev), (*pdev)->bus->self);
	(*pdev)->bus->number = pci_get_bus(dev);
}

void drm_fini_pdev(struct pci_dev **pdev)
{
	kfree((*pdev)->bus->self);
	kfree((*pdev)->bus);

	kfree(*pdev);
}

void drm_print_pdev(struct pci_dev *pdev)
{
	if (pdev == NULL) {
		DRM_ERROR("pdev is null!\n");
		return;
	}

	DRM_INFO("pdev:  vendor=0x%04x  device=0x%04x rev=0x%02x\n",
		 pdev->vendor, pdev->device, pdev->revision);
	DRM_INFO("      svendor=0x%04x sdevice=0x%04x irq=%u\n",
		 pdev->subsystem_vendor, pdev->subsystem_device, pdev->irq);
}

/* Allocation of PCI memory resources (framebuffer, registers, etc.) for
 * drm_get_resource_*.  Note that they are not RF_ACTIVE, so there's no virtual
 * address for accessing them.  Cleaned up at unload.
 */
static int drm_alloc_resource(struct drm_device *dev, int resource)
{
	struct resource *res;
	int rid;

	KKASSERT(lockstatus(&dev->struct_mutex, curthread) != 0);

	if (resource >= DRM_MAX_PCI_RESOURCE) {
		DRM_ERROR("Resource %d too large\n", resource);
		return 1;
	}

	if (dev->pcir[resource] != NULL) {
		return 0;
	}

	DRM_UNLOCK(dev);
	rid = PCIR_BAR(resource);
	res = bus_alloc_resource_any(dev->dev->bsddev, SYS_RES_MEMORY, &rid,
	    RF_SHAREABLE);
	DRM_LOCK(dev);
	if (res == NULL) {
		DRM_ERROR("Couldn't find resource 0x%x\n", resource);
		return 1;
	}

	if (dev->pcir[resource] == NULL) {
		dev->pcirid[resource] = rid;
		dev->pcir[resource] = res;
	}

	return 0;
}

unsigned long drm_get_resource_start(struct drm_device *dev,
				     unsigned int resource)
{
	if (drm_alloc_resource(dev, resource) != 0)
		return 0;

	return rman_get_start(dev->pcir[resource]);
}

unsigned long drm_get_resource_len(struct drm_device *dev,
				   unsigned int resource)
{
	if (drm_alloc_resource(dev, resource) != 0)
		return 0;

	return rman_get_size(dev->pcir[resource]);
}

/* Former drm_release() in the legacy DragonFly BSD drm codebase */
int drm_device_detach(device_t kdev)
{
	struct drm_softc *softc = device_get_softc(kdev);
	struct drm_device *dev = softc->drm_driver_data;

	drm_sysctl_cleanup(dev);
	if (dev->devnode != NULL)
		destroy_dev(dev->devnode);

#ifdef __DragonFly__
	/* Clean up PCI resources allocated by drm_bufs.c.  We're not really
	 * worried about resource consumption while the DRM is inactive (between
	 * lastclose and firstopen or unload) because these aren't actually
	 * taking up KVA, just keeping the PCI resource allocated.
	 */
	for (int i = 0; i < DRM_MAX_PCI_RESOURCE; i++) {
		if (dev->pcir[i] == NULL)
			continue;
		bus_release_resource(dev->dev->bsddev, SYS_RES_MEMORY,
		    dev->pcirid[i], dev->pcir[i]);
		dev->pcir[i] = NULL;
	}

	if (dev->agp) {
		kfree(dev->agp);
		dev->agp = NULL;
	}

	if (dev->driver->unload != NULL) {
		DRM_LOCK(dev);
		dev->driver->unload(dev);
		DRM_UNLOCK(dev);
	}

	if (pci_disable_busmaster(dev->dev->bsddev))
		DRM_ERROR("Request to disable bus-master failed.\n");

	lockuninit(&dev->vbl_lock);
	lockuninit(&dev->event_lock);
	lockuninit(&dev->struct_mutex);
#endif

	return 0;
}
