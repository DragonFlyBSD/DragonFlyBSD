/**
 * \file drm_ioctl.c
 * IOCTL processing for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Jan  8 09:01:26 1999 by faith@valinux.com
 *
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
 *
 * $FreeBSD: src/sys/dev/drm2/drm_ioctl.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

#include <drm/drmP.h>
#include <drm/drm_core.h>

#include <linux/export.h>

/**
 * Get the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from drm_device::unique into user space.
 */
int drm_getunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;

	if (u->unique_len >= dev->unique_len) {
		if (DRM_COPY_TO_USER(u->unique, dev->unique, dev->unique_len))
			return EFAULT;
	}
	u->unique_len = dev->unique_len;

	return 0;
}

/**
 * Set the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from userspace into drm_device::unique, and verifies that
 * it matches the device this DRM is attached to (EINVAL otherwise).  Deprecated
 * in interface version 1.1 and will return EBUSY when setversion has requested
 * version 1.1 or greater.
 */
int drm_setunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;
	int domain, bus, slot, func, ret;
	char *busid;

	/* Check and copy in the submitted Bus ID */
	if (!u->unique_len || u->unique_len > 1024)
		return EINVAL;

	busid = kmalloc(u->unique_len + 1, M_DRM, M_WAITOK);
	if (busid == NULL)
		return ENOMEM;

	if (DRM_COPY_FROM_USER(busid, u->unique, u->unique_len)) {
		drm_free(busid, M_DRM);
		return EFAULT;
	}
	busid[u->unique_len] = '\0';

	/* Return error if the busid submitted doesn't match the device's actual
	 * busid.
	 */
	ret = ksscanf(busid, "PCI:%d:%d:%d", &bus, &slot, &func);
	if (ret != 3) {
		drm_free(busid, M_DRM);
		return EINVAL;
	}
	domain = bus >> 8;
	bus &= 0xff;
	
	if ((domain != dev->pci_domain) ||
	    (bus != dev->pci_bus) ||
	    (slot != dev->pci_slot) ||
	    (func != dev->pci_func)) {
		drm_free(busid, M_DRM);
		return EINVAL;
	}

	/* Actually set the device's busid now. */
	DRM_LOCK(dev);
	if (dev->unique_len || dev->unique) {
		DRM_UNLOCK(dev);
		return EBUSY;
	}

	dev->unique_len = u->unique_len;
	dev->unique = busid;
	DRM_UNLOCK(dev);

	return 0;
}

static int drm_set_busid(struct drm_device *dev, struct drm_file *file_priv)
{

	DRM_LOCK(dev);

	dev->unique_len = 20;
	dev->unique = kmalloc(dev->unique_len + 1, M_DRM, M_WAITOK | M_NULLOK);
	if (dev->unique == NULL) {
		DRM_UNLOCK(dev);
		return ENOMEM;
	}

	ksnprintf(dev->unique, dev->unique_len, "pci:%04x:%02x:%02x.%1x",
	    dev->pci_domain, dev->pci_bus, dev->pci_slot, dev->pci_func);

	DRM_UNLOCK(dev);

	return 0;
}

/**
 * Get a mapping information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_map structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the mapping with the specified offset and copies its information
 * into userspace
 */
int drm_getmap(struct drm_device *dev, void *data,
	       struct drm_file *file_priv)
{
	struct drm_map *map = data;
	struct drm_map_list *r_list = NULL;
	struct list_head *list;
	int idx;
	int i;

	idx = map->offset;
	if (idx < 0) {
		return EINVAL;
	}

	i = 0;
	DRM_LOCK(dev);
	list_for_each(list, &dev->maplist) {
		if (i == idx) {
			r_list = list_entry(list, struct drm_map_list, head);
			break;
		}
		i++;
	}
	if (!r_list || !r_list->map) {
		DRM_UNLOCK(dev);
		return -EINVAL;
	}

	map->offset = r_list->map->offset;
	map->size = r_list->map->size;
	map->type = r_list->map->type;
	map->flags = r_list->map->flags;
	map->handle = r_list->map->handle;
	map->mtrr   = r_list->map->mtrr;
	DRM_UNLOCK(dev);

	return 0;
}

/**
 * Get client information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_client structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the client with the specified index and copies its information
 * into userspace
 */
int drm_getclient(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_client *client = data;
	struct drm_file *pt;
	int idx;
	int i = 0;

	idx = client->idx;
	DRM_LOCK(dev);
	list_for_each_entry(pt, &dev->filelist, lhead) {
		if (i++ >= idx) {
			client->auth  = pt->authenticated;
			client->pid   = pt->pid;
			client->uid   = pt->uid;
			client->magic = pt->magic;
			client->iocs  = pt->ioctl_count;
			DRM_UNLOCK(dev);

			return 0;
		}
	}
	DRM_UNLOCK(dev);

	return EINVAL;
}

/**
 * Get statistics information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_stats structure.
 *
 * \return zero on success or a negative number on failure.
 */
int drm_getstats(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_stats *stats = data;
	int          i;

	memset(stats, 0, sizeof(struct drm_stats));
	
	DRM_LOCK(dev);

	for (i = 0; i < dev->counters; i++) {
		if (dev->types[i] == _DRM_STAT_LOCK)
			stats->data[i].value =
			    (dev->lock.hw_lock ? dev->lock.hw_lock->lock : 0);
		else 
			stats->data[i].value = atomic_read(&dev->counts[i]);
		stats->data[i].type = dev->types[i];
	}
	
	stats->count = dev->counters;

	DRM_UNLOCK(dev);

	return 0;
}

/**
 * Get device/driver capabilities
 */
int drm_getcap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_get_cap *req = data;

	req->value = 0;
	switch (req->capability) {
	case DRM_CAP_DUMB_BUFFER:
		if (dev->driver->dumb_create)
			req->value = 1;
		break;
	case DRM_CAP_VBLANK_HIGH_CRTC:
		req->value = 1;
		break;
	case DRM_CAP_DUMB_PREFERRED_DEPTH:
		req->value = dev->mode_config.preferred_depth;
		break;
	case DRM_CAP_DUMB_PREFER_SHADOW:
		req->value = dev->mode_config.prefer_shadow;
		break;
	case DRM_CAP_TIMESTAMP_MONOTONIC:
		req->value = drm_timestamp_monotonic;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

/**
 * Setversion ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Sets the requested interface version
 */
int drm_setversion(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_version *sv = data;
	struct drm_set_version ver;
	int if_version, retcode = 0;

	/* Save the incoming data, and set the response before continuing
	 * any further.
	 */
	ver = *sv;
	sv->drm_di_major = DRM_IF_MAJOR;
	sv->drm_di_minor = DRM_IF_MINOR;
	sv->drm_dd_major = dev->driver->major;
	sv->drm_dd_minor = dev->driver->minor;

	if (ver.drm_di_major != -1) {
		if (ver.drm_di_major != DRM_IF_MAJOR ||
		    ver.drm_di_minor < 0 || ver.drm_di_minor > DRM_IF_MINOR) {
			return EINVAL;
		}
		if_version = DRM_IF_VERSION(ver.drm_di_major,
		    ver.drm_dd_minor);
		dev->if_version = DRM_MAX(if_version, dev->if_version);
		if (ver.drm_di_minor >= 1) {
			/*
			 * Version 1.1 includes tying of DRM to specific device
			 * Version 1.4 has proper PCI domain support
			 */
			retcode = drm_set_busid(dev, file_priv);
			if (retcode)
				return retcode;
		}
	}

	if (ver.drm_dd_major != -1) {
		if (ver.drm_dd_major != dev->driver->major ||
		    ver.drm_dd_minor < 0 ||
		    ver.drm_dd_minor > dev->driver->minor)
		{
			return EINVAL;
		}
	}

	return 0;
}

/** No-op ioctl. */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv)
{
	DRM_DEBUG("\n");
	return 0;
}
EXPORT_SYMBOL(drm_noop);
