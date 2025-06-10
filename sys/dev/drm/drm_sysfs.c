/*
 * Copyright 2015-2018 François Tigeot <ftigeot@wolfpond.org>
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/device.h>

#include <drm/drm_sysfs.h>
#include <drm/drmP.h>
#include "drm_internal.h"

int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (connector->kdev)
		return 0;

	/* Linux uses device_create_with_groups() here */
	connector->kdev = kzalloc(sizeof(struct device), GFP_KERNEL);
	connector->kdev->kobj.name = kasprintf(GFP_KERNEL, "card%d-%s",
					       dev->primary->index,
					       connector->name);
	DRM_DEBUG("adding \"%s\" to sysfs\n", connector->name);

	return 0;
}

void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	DRM_DEBUG("removing \"%s\" from sysfs\n", connector->name);

	if (connector->kdev)
		kfree(connector->kdev);
}

void drm_sysfs_hotplug_event(struct drm_device *dev)
{
}

int drm_class_device_register(struct device *dev)
{
	return 0;
}

/**
 * drm_class_device_unregister - unregister device with the DRM sysfs class
 * @dev: device to unregister
 *
 * Unregisters a &struct device from the DRM sysfs class. Essentially only used
 * by ttm to have a place for its global settings. Drivers should never use
 * this.
 */
void drm_class_device_unregister(struct device *dev)
{
}

extern struct dev_ops drm_cdevsw;

struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char dev_str[12];
	struct device *kdev;
	int r;
	struct cdev *devnode;

	if (minor->type == DRM_MINOR_PRIMARY)
		ksnprintf(dev_str, sizeof(dev_str), "card%d", minor->index);
	else if (minor->type == DRM_MINOR_RENDER)
		ksnprintf(dev_str, sizeof(dev_str), "renderD%d", minor->index);
	else
		return NULL;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	devnode = make_dev(&drm_cdevsw, minor->index,
		DRM_DEV_UID, DRM_DEV_GID, DRM_DEV_MODE, "dri/%s", dev_str);

	kdev->parent = minor->dev->dev;
	dev_set_drvdata(kdev, minor);

	r = dev_set_name(kdev, dev_str);
	if (r < 0)
		goto err_free;

	return kdev;

err_free:
	return ERR_PTR(r);
}

