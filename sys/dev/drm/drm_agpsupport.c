/**
 * \file drm_agpsupport.c
 * DRM support for AGP/GART backend
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <linux/module.h>
#include "drm_legacy.h"

#include <dev/agp/agpreg.h>
#include <bus/pci/pcireg.h>

/**
 * Get AGP information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a (output) drm_agp_info structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been initialized and acquired and fills in the
 * drm_agp_info structure with the information in drm_agp_head::agp_info.
 */
int drm_agp_info(struct drm_device *dev, struct drm_agp_info *info)
{
	struct agp_info *kern;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	kern = &dev->agp->agp_info;
	agp_get_info(dev->agp->agpdev, kern);
	info->agp_version_major = 1;
	info->agp_version_minor = 0;
	info->mode = kern->ai_mode;
	info->aperture_base = kern->ai_aperture_base;
	info->aperture_size = kern->ai_aperture_size;
	info->memory_allowed = kern->ai_memory_allowed;
	info->memory_used = kern->ai_memory_used;
	info->id_vendor = kern->ai_devid & 0xffff;
	info->id_device = kern->ai_devid >> 16;

	return 0;
}

EXPORT_SYMBOL(drm_agp_info);

int drm_agp_info_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_agp_info info;
	int err;

	err = drm_agp_info(dev, &info);
	if (err)
		return err;

	*(struct drm_agp_info *) data = info;
	return 0;
}

/**
 * Acquire the AGP device.
 *
 * \param dev DRM device that is to acquire AGP.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device hasn't been acquired before and calls
 * \c agp_backend_acquire.
 */
int drm_agp_acquire(struct drm_device * dev)
{
	int retcode;

	if (!dev->agp || dev->agp->acquired)
		return -EINVAL;

	retcode = -agp_acquire(dev->agp->agpdev);
	if (retcode)
		return retcode;

	dev->agp->acquired = 1;
	return 0;
}

EXPORT_SYMBOL(drm_agp_acquire);

/**
 * Acquire the AGP device (ioctl).
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device hasn't been acquired before and calls
 * \c agp_backend_acquire.
 */
int drm_agp_acquire_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	return drm_agp_acquire(dev);
}

/**
 * Release the AGP device.
 *
 * \param dev DRM device that is to release AGP.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been acquired and calls \c agp_backend_release.
 */
int drm_agp_release(struct drm_device * dev)
{
	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	agp_release(dev->agp->agpdev);
	dev->agp->acquired = 0;
	return 0;
}
EXPORT_SYMBOL(drm_agp_release);

int drm_agp_release_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	return drm_agp_release(dev);
}

/**
 * Enable the AGP bus.
 *
 * \param dev DRM device that has previously acquired AGP.
 * \param mode Requested AGP mode.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been acquired but not enabled, and calls
 * \c agp_enable.
 */
int drm_agp_enable(struct drm_device * dev, struct drm_agp_mode mode)
{
	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	dev->agp->mode = mode.mode;
	agp_enable(dev->agp->agpdev, mode.mode);
	dev->agp->enabled = 1;
	return 0;
}

EXPORT_SYMBOL(drm_agp_enable);

int drm_agp_enable_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_agp_mode mode;

	mode = *(struct drm_agp_mode *) data;

	return drm_agp_enable(dev, mode);
}

static void *drm_agp_allocate_memory(size_t pages, u32 type)
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev)
		return NULL;

	return agp_alloc_memory(agpdev, type, pages << AGP_PAGE_SHIFT);
}

/**
 * Allocate AGP memory.
 *
 * \param inode device inode.
 * \param file_priv file private pointer.
 * \param cmd command.
 * \param arg pointer to a drm_agp_buffer structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired, allocates the
 * memory via agp_allocate_memory() and creates a drm_agp_mem entry for it.
 */
int drm_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request)
{
	struct drm_agp_mem *entry;
	void	         *handle;
	unsigned long    pages;
	u_int32_t	 type;
	struct agp_memory_info info;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	entry = kmalloc(sizeof(*entry), M_DRM, M_WAITOK | M_NULLOK | M_ZERO);
	if (entry == NULL)
		return -ENOMEM;

	pages = (request->size + PAGE_SIZE - 1) / PAGE_SIZE;
	type = (u_int32_t) request->type;

	handle = drm_agp_allocate_memory(pages, type);
	if (handle == NULL) {
		kfree(entry);
		return -ENOMEM;
	}
	
	entry->handle    = handle;
	entry->bound     = 0;
	entry->pages     = pages;
	entry->prev      = NULL;
	entry->next      = dev->agp->memory;
	if (dev->agp->memory)
		dev->agp->memory->prev = entry;
	dev->agp->memory = entry;

	agp_memory_info(dev->agp->agpdev, entry->handle, &info);

	request->handle   = (unsigned long) entry->handle;
	request->physical = info.ami_physical;

	return 0;
}
EXPORT_SYMBOL(drm_agp_alloc);


int drm_agp_alloc_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_agp_buffer request;
	int retcode;

	request = *(struct drm_agp_buffer *) data;

	retcode = drm_agp_alloc(dev, &request);

	*(struct drm_agp_buffer *) data = request;

	return retcode;
}

/**
 * Search for the AGP memory entry associated with a handle.
 *
 * \param dev DRM device structure.
 * \param handle AGP memory handle.
 * \return pointer to the drm_agp_mem structure associated with \p handle.
 *
 * Walks through drm_agp_head::memory until finding a matching handle.
 */
static struct drm_agp_mem *drm_agp_lookup_entry(struct drm_device * dev,
					    void *handle)
{
	struct drm_agp_mem *entry;

	for (entry = dev->agp->memory; entry; entry = entry->next) {
		if (entry->handle == handle)
			return entry;
	}
	return NULL;
}

static int drm_agp_unbind_memory(void *handle)
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return -EINVAL;

	return -agp_unbind_memory(agpdev, handle);
}

/**
 * Unbind AGP memory from the GATT (ioctl).
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a drm_agp_binding structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and acquired, looks-up the AGP memory
 * entry and passes it to the unbind_agp() function.
 */
int drm_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request)
{
	struct drm_agp_mem *entry;
	int ret;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	entry = drm_agp_lookup_entry(dev, (void *)request->handle);
	if (entry == NULL || !entry->bound)
		return -EINVAL;
	ret = drm_agp_unbind_memory(entry->handle);
	if (ret == 0)
		entry->bound = 0;
	return ret;
}
EXPORT_SYMBOL(drm_agp_unbind);


int drm_agp_unbind_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_agp_binding request;

	request = *(struct drm_agp_binding *) data;

	return drm_agp_unbind(dev, &request);
}

static int drm_agp_bind_memory(void *handle, off_t start)
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return -EINVAL;

	return -agp_bind_memory(agpdev, handle, start * PAGE_SIZE);
}

/**
 * Bind AGP memory into the GATT (ioctl)
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a drm_agp_binding structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired and that no memory
 * is currently bound into the GATT. Looks-up the AGP memory entry and passes
 * it to bind_agp() function.
 */
int drm_agp_bind(struct drm_device *dev, struct drm_agp_binding *request)
{
	struct drm_agp_mem *entry;
	int retcode;
	int page;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	DRM_DEBUG("agp_bind, page_size=%x\n", (int)PAGE_SIZE);

	entry = drm_agp_lookup_entry(dev, (void *)request->handle);
	if (entry == NULL || entry->bound)
		return -EINVAL;

	page = (request->offset + PAGE_SIZE - 1) / PAGE_SIZE;

	retcode = drm_agp_bind_memory(entry->handle, page);
	if (retcode == 0)
		entry->bound = dev->agp->base + (page << PAGE_SHIFT);

	return retcode;
}
EXPORT_SYMBOL(drm_agp_bind);


int drm_agp_bind_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_agp_binding request;

	request = *(struct drm_agp_binding *) data;

	return drm_agp_bind(dev, &request);
}

static int drm_agp_free_memory(void *handle)
{
	device_t agpdev;

	agpdev = agp_find_device();
	if (!agpdev || !handle)
		return 0;

	agp_free_memory(agpdev, handle);
	return 1;
}

/**
 * Free AGP memory (ioctl).
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg pointer to a drm_agp_buffer structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired and looks up the
 * AGP memory entry. If the memory it's currently bound, unbind it via
 * unbind_agp(). Frees it via free_agp() as well as the entry itself
 * and unlinks from the doubly linked list it's inserted in.
 */
int drm_agp_free(struct drm_device *dev, struct drm_agp_buffer *request)
{
	struct drm_agp_mem *entry;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	entry = drm_agp_lookup_entry(dev, (void*)request->handle);
	if (entry == NULL)
		return -EINVAL;

	if (entry->prev)
		entry->prev->next = entry->next;
	else
		dev->agp->memory  = entry->next;
	if (entry->next)
		entry->next->prev = entry->prev;

	if (entry->bound)
		drm_agp_unbind_memory(entry->handle);

	drm_agp_free_memory(entry->handle);
	kfree(entry);
	return 0;
}
EXPORT_SYMBOL(drm_agp_free);



int drm_agp_free_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_agp_buffer request;

	request = *(struct drm_agp_buffer *) data;

	return drm_agp_free(dev, &request);
}

/**
 * Initialize the AGP resources.
 *
 * \return pointer to a drm_agp_head structure.
 *
 * Gets the drm_agp_t structure which is made available by the agpgart module
 * via the inter_module_* functions. Creates and initializes a drm_agp_head
 * structure.
 *
 * Note that final cleanup of the kmalloced structure is directly done in
 * drm_pci_agp_destroy.
 */
struct drm_agp_head *drm_agp_init(struct drm_device *dev)
{
	device_t agpdev;
	struct drm_agp_head *head = NULL;
	int      agp_available = 1;

	agpdev = agp_find_device();
	if (!agpdev)
		agp_available = 0;

	DRM_DEBUG("agp_available = %d\n", agp_available);

	if (agp_available) {
		head = kmalloc(sizeof(*head), M_DRM,
				M_WAITOK | M_NULLOK | M_ZERO);
		if (head == NULL)
			return NULL;
		head->agpdev = agpdev;
		agp_get_info(agpdev, &head->agp_info);
		head->base = head->agp_info.ai_aperture_base;
		head->memory = NULL;
		DRM_INFO("AGP at 0x%08lx %dMB\n",
			 (long)head->agp_info.ai_aperture_base,
			 (int)(head->agp_info.ai_aperture_size >> 20));
	}
	return head;
}

/**
 * drm_legacy_agp_clear - Clear AGP resource list
 * @dev: DRM device
 *
 * Iterate over all AGP resources and remove them. But keep the AGP head
 * intact so it can still be used. It is safe to call this if AGP is disabled or
 * was already removed.
 *
 * If DRIVER_MODESET is active, nothing is done to protect the modesetting
 * resources from getting destroyed. Drivers are responsible of cleaning them up
 * during device shutdown.
 */
void drm_legacy_agp_clear(struct drm_device *dev)
{
	struct drm_agp_mem *entry, *nexte;

	if (!dev->agp)
		return;
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	/* Remove AGP resources, but leave dev->agp intact until
	 * drm_unload is called.
	 */
	for (entry = dev->agp->memory; entry; entry = nexte) {
		nexte = entry->next;
		if (entry->bound)
			drm_agp_unbind_memory(entry->handle);
		drm_agp_free_memory(entry->handle);
		kfree(entry);
	}
	dev->agp->memory = NULL;

	if (dev->agp->acquired)
		drm_agp_release(dev);

	dev->agp->acquired = 0;
	dev->agp->enabled = 0;
}
