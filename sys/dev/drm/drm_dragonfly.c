/*
 * Copyright (c) 2015 Imre Vadász <imre@vdsz.com>
 * Copyright (c) 2015 Rimvydas Jasinskas
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
 * Implement simplified version of kvasnrprintf() for drm needs using
 * M_DRM and kvsnprintf(). Since it is unclear what string size is
 * optimal thus use of an actual length.
 */
char *drm_vasprintf(int flags, const char *format, __va_list ap)
{
	char *str;
	size_t size;
	__va_list aq;

	__va_copy(aq, ap);
	size = kvsnprintf(NULL, 0, format, aq);
	__va_end(aq);

	str = kmalloc(size+1, M_DRM, flags);
	if (str == NULL)
		return NULL;

	kvsnprintf(str, size+1, format, ap);

	return str;
}

/* mimic ksnrprintf(), return pointer to char* and match drm api */
char *drm_asprintf(int flags, const char *format, ...)
{
	char *str;
	__va_list ap;

	__va_start(ap, format);
	str = drm_vasprintf(flags, format, ap);
	__va_end(ap);

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
	pdev->dev.bsddev = dev;
	pdev->vendor = pci_get_vendor(dev);
	pdev->device = pci_get_device(dev);
	pdev->subsystem_vendor = pci_get_subvendor(dev);
	pdev->subsystem_device = pci_get_subdevice(dev);

	pdev->revision = pci_get_revid(dev) & 0xff;
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

	DRM_LOCK_ASSERT(dev);

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
