/*
 * Created: Fri Jan  8 09:01:26 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Author Rickard E. (Rik) Faith <faith@valinux.com>
 * Author Gareth Hughes <gareth@valinux.com>
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

#include <sys/devfs.h>

#include <drm/drmP.h>
#include <drm/drm_core.h>
#include "drm_legacy.h"
#include "drm_internal.h"
#include "drm_crtc_internal.h"

#include <linux/export.h>

static int drm_version(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

/*
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
static int drm_getunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;

	if (u->unique_len >= dev->unique_len) {
		if (copy_to_user(u->unique, dev->unique, dev->unique_len))
			return -EFAULT;
	}
	u->unique_len = dev->unique_len;

	return 0;
}

/*
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
static int drm_setunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;
	int domain, bus, slot, func, ret;
	char *busid;

	/* Check and copy in the submitted Bus ID */
	if (!u->unique_len || u->unique_len > 1024)
		return -EINVAL;

	busid = kmalloc(u->unique_len + 1, M_DRM, M_WAITOK);
	if (busid == NULL)
		return -ENOMEM;

	if (copy_from_user(busid, u->unique, u->unique_len)) {
		kfree(busid);
		return -EFAULT;
	}
	busid[u->unique_len] = '\0';

	/* Return error if the busid submitted doesn't match the device's actual
	 * busid.
	 */
	ret = ksscanf(busid, "PCI:%d:%d:%d", &bus, &slot, &func);
	if (ret != 3) {
		kfree(busid);
		return -EINVAL;
	}
	domain = bus >> 8;
	bus &= 0xff;
	
	if ((domain != dev->pci_domain) ||
	    (bus != dev->pci_bus) ||
	    (slot != dev->pci_slot) ||
	    (func != dev->pci_func)) {
		kfree(busid);
		return -EINVAL;
	}

	/* Actually set the device's busid now. */
	DRM_LOCK(dev);
	if (dev->unique_len || dev->unique) {
		DRM_UNLOCK(dev);
		return -EBUSY;
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
		return -ENOMEM;
	}

	ksnprintf(dev->unique, dev->unique_len, "pci:%04x:%02x:%02x.%1x",
	    dev->pci_domain, dev->pci_bus, dev->pci_slot, dev->pci_func);

	DRM_UNLOCK(dev);

	return 0;
}

/*
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
static int drm_getclient(struct drm_device *dev, void *data,
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

	return -EINVAL;
}

/*
 * Get statistics information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_stats structure.
 *
 * \return zero on success or a negative number on failure.
 */
static int drm_getstats(struct drm_device *dev, void *data,
		 struct drm_file *file_priv)
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

/*
 * Get device/driver capabilities
 */
static int drm_getcap(struct drm_device *dev, void *data, struct drm_file *file_priv)
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
	case DRM_CAP_ASYNC_PAGE_FLIP:
		req->value = dev->mode_config.async_page_flip;
		break;
	case DRM_CAP_CURSOR_WIDTH:
		if (dev->mode_config.cursor_width)
			req->value = dev->mode_config.cursor_width;
		else
			req->value = 64;
		break;
	case DRM_CAP_CURSOR_HEIGHT:
		if (dev->mode_config.cursor_height)
			req->value = dev->mode_config.cursor_height;
		else
			req->value = 64;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Set device/driver capabilities
 */
static int
drm_setclientcap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_client_cap *req = data;

	switch (req->capability) {
	case DRM_CLIENT_CAP_STEREO_3D:
		if (req->value > 1)
			return -EINVAL;
		file_priv->stereo_allowed = req->value;
		break;
	case DRM_CLIENT_CAP_UNIVERSAL_PLANES:
		if (req->value > 1)
			return -EINVAL;
		file_priv->universal_planes = req->value;
		break;
	case DRM_CLIENT_CAP_ATOMIC:
		if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
			return -EINVAL;
		if (req->value > 1)
			return -EINVAL;
		file_priv->atomic = req->value;
		file_priv->universal_planes = req->value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
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
static int drm_setversion(struct drm_device *dev, void *data, struct drm_file *file_priv)
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
			return -EINVAL;
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
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * drm_noop - DRM no-op ioctl implemntation
 * @dev: DRM device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: DRM file for the ioctl call
 *
 * This no-op implementation for drm ioctls is useful for deprecated
 * functionality where we can't return a failure code because existing userspace
 * checks the result of the ioctl, but doesn't care about the action.
 *
 * Always returns successfully with 0.
 */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv)
{
	DRM_DEBUG("\n");
	return 0;
}
EXPORT_SYMBOL(drm_noop);

/**
 * drm_invalid_op - DRM invalid ioctl implemntation
 * @dev: DRM device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: DRM file for the ioctl call
 *
 * This no-op implementation for drm ioctls is useful for deprecated
 * functionality where we really don't want to allow userspace to call the ioctl
 * any more. This is the case for old ums interfaces for drivers that
 * transitioned to kms gradually and so kept the old legacy tables around. This
 * only applies to radeon and i915 kms drivers, other drivers shouldn't need to
 * use this function.
 *
 * Always fails with a return value of -EINVAL.
 */
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	return -EINVAL;
}
EXPORT_SYMBOL(drm_invalid_op);

/*
 * Copy and IOCTL return string to user space
 */
static int drm_copy_field(char __user *buf, size_t *buf_len, const char *value)
{
	int len;

	/* don't overflow userbuf */
	len = strlen(value);
	if (len > *buf_len)
		len = *buf_len;

	/* let userspace know exact length of driver value (which could be
	 * larger than the userspace-supplied buffer) */
	*buf_len = strlen(value);

	/* finally, try filling in the userbuf */
	if (len && buf)
		if (copy_to_user(buf, value, len))
			return -EFAULT;
	return 0;
}

/*
 * Get version information
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_version structure.
 * \return zero on success or negative number on failure.
 *
 * Fills in the version information in \p arg.
 */
static int drm_version(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_version *version = data;
	int err;

	version->version_major = dev->driver->major;
	version->version_minor = dev->driver->minor;
	version->version_patchlevel = dev->driver->patchlevel;
	err = drm_copy_field(version->name, &version->name_len,
			dev->driver->name);
	if (!err)
		err = drm_copy_field(version->date, &version->date_len,
				dev->driver->date);
	if (!err)
		err = drm_copy_field(version->desc, &version->desc_len,
				dev->driver->desc);

	return err;
}

/*
 * drm_ioctl_permit - Check ioctl permissions against caller
 *
 * @flags: ioctl permission flags.
 * @file_priv: Pointer to struct drm_file identifying the caller.
 *
 * Checks whether the caller is allowed to run an ioctl with the
 * indicated permissions. If so, returns zero. Otherwise returns an
 * error code suitable for ioctl return.
 */
#if 0
int drm_ioctl_permit(u32 flags, struct drm_file *file_priv)
{
	/* ROOT_ONLY is only for CAP_SYS_ADMIN */
	if (unlikely((flags & DRM_ROOT_ONLY) && !capable(CAP_SYS_ADMIN)))
		return -EACCES;

	/* AUTH is only for authenticated or render client */
	if (unlikely((flags & DRM_AUTH) && !drm_is_render_client(file_priv) &&
		     !file_priv->authenticated))
		return -EACCES;

	/* MASTER is only for master or control clients */
	if (unlikely((flags & DRM_MASTER) && !file_priv->is_master &&
		     !drm_is_control_client(file_priv)))
		return -EACCES;

	/* Control clients must be explicitly allowed */
	if (unlikely(!(flags & DRM_CONTROL_ALLOW) &&
		     drm_is_control_client(file_priv)))
		return -EACCES;

	/* Render clients must be explicitly allowed */
	if (unlikely(!(flags & DRM_RENDER_ALLOW) &&
		     drm_is_render_client(file_priv)))
		return -EACCES;

	return 0;
}
EXPORT_SYMBOL(drm_ioctl_permit);
#endif

#define DRM_IOCTL_DEF(ioctl, _func, _flags)	\
	[DRM_IOCTL_NR(ioctl)] = {		\
		.cmd = ioctl,			\
		.func = _func,			\
		.flags = _flags,		\
		.cmd_drv = 0,			\
		.name = #ioctl			\
	}

/* Ioctl table */
static const struct drm_ioctl_desc drm_ioctls[] = {
	DRM_IOCTL_DEF(DRM_IOCTL_VERSION, drm_version,
		      DRM_UNLOCKED|DRM_RENDER_ALLOW|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_UNIQUE, drm_getunique, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAGIC, drm_getmagic, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_IRQ_BUSID, drm_irq_by_busid, DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAP, drm_legacy_getmap_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CLIENT, drm_getclient, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_STATS, drm_getstats, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CAP, drm_getcap, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_CLIENT_CAP, drm_setclientcap, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_VERSION, drm_setversion, DRM_MASTER),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_UNIQUE, drm_setunique, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_BLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_UNBLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AUTH_MAGIC, drm_authmagic, DRM_AUTH|DRM_MASTER),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_MAP, drm_legacy_addmap_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_MAP, drm_legacy_rmmap_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_SAREA_CTX, drm_legacy_setsareactx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_SAREA_CTX, drm_legacy_getsareactx, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_MASTER, drm_setmaster_ioctl, DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_DROP_MASTER, drm_dropmaster_ioctl, DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_CTX, drm_legacy_addctx, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_CTX, drm_legacy_rmctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_MOD_CTX, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CTX, drm_legacy_getctx, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_SWITCH_CTX, drm_legacy_switchctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_NEW_CTX, drm_legacy_newctx, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RES_CTX, drm_legacy_resctx, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_LOCK, drm_legacy_lock, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_UNLOCK, drm_legacy_unlock, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_FINISH, drm_noop, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_BUFS, drm_legacy_addbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_MARK_BUFS, drm_legacy_markbufs, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_INFO_BUFS, drm_legacy_infobufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_MAP_BUFS, drm_legacy_mapbufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_FREE_BUFS, drm_legacy_freebufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_DMA, drm_legacy_dma_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_CONTROL, drm_control, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

#if IS_ENABLED(CONFIG_AGP)
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ACQUIRE, drm_agp_acquire_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_RELEASE, drm_agp_release_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ENABLE, drm_agp_enable_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_INFO, drm_agp_info_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_ALLOC, drm_agp_alloc_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_FREE, drm_agp_free_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_BIND, drm_agp_bind_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AGP_UNBIND, drm_agp_unbind_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
#endif

	DRM_IOCTL_DEF(DRM_IOCTL_SG_ALLOC, drm_legacy_sg_alloc, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_SG_FREE, drm_legacy_sg_free, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_WAIT_VBLANK, drm_wait_vblank, DRM_UNLOCKED),

	DRM_IOCTL_DEF(DRM_IOCTL_MODESET_CTL, drm_modeset_ctl, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_UPDATE_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_GEM_CLOSE, drm_gem_close_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_FLINK, drm_gem_flink_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_OPEN, drm_gem_open_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETRESOURCES, drm_mode_getresources, DRM_CONTROL_ALLOW|DRM_UNLOCKED),

#if 0
	DRM_IOCTL_DEF(DRM_IOCTL_PRIME_HANDLE_TO_FD, drm_prime_handle_to_fd_ioctl, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_PRIME_FD_TO_HANDLE, drm_prime_fd_to_handle_ioctl, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
#endif

	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANERESOURCES, drm_mode_getplane_res, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCRTC, drm_mode_getcrtc, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETCRTC, drm_mode_setcrtc, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANE, drm_mode_getplane, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPLANE, drm_mode_setplane, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CURSOR, drm_mode_cursor_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETGAMMA, drm_mode_gamma_get_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETGAMMA, drm_mode_gamma_set_ioctl, DRM_MASTER|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETENCODER, drm_mode_getencoder, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCONNECTOR, drm_mode_getconnector, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ATTACHMODE, drm_noop, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DETACHMODE, drm_noop, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPERTY, drm_mode_getproperty_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPROPERTY, drm_mode_connector_property_set_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPBLOB, drm_mode_getblob_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETFB, drm_mode_getfb, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB, drm_mode_addfb, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB2, drm_mode_addfb2, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_RMFB, drm_mode_rmfb, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_PAGE_FLIP, drm_mode_page_flip_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DIRTYFB, drm_mode_dirtyfb_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATE_DUMB, drm_mode_create_dumb_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_MAP_DUMB, drm_mode_mmap_dumb_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DESTROY_DUMB, drm_mode_destroy_dumb_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, drm_mode_obj_get_properties_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_OBJ_SETPROPERTY, drm_mode_obj_set_property_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CURSOR2, drm_mode_cursor2_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ATOMIC, drm_mode_atomic_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATEPROPBLOB, drm_mode_createblob_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DESTROYPROPBLOB, drm_mode_destroyblob_ioctl, DRM_CONTROL_ALLOW|DRM_UNLOCKED),
};

#define DRM_CORE_IOCTL_COUNT	ARRAY_SIZE( drm_ioctls )

/**
 * drm_ioctl - ioctl callback implementation for DRM drivers
 * @filp: file this ioctl is called on
 * @cmd: ioctl cmd number
 * @arg: user argument
 *
 * Looks up the ioctl function in the ::ioctls table, checking for root
 * previleges if so required, and dispatches to the respective function.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int drm_ioctl(struct dev_ioctl_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	struct drm_device *dev;
	const struct drm_ioctl_desc *ioctl = NULL;
	u_long cmd = ap->a_cmd;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int retcode = 0;
	caddr_t data = ap->a_data;
	int (*func)(struct drm_device *dev, void *data, struct drm_file *file_priv);
	struct drm_file *file_priv;
	bool is_driver_ioctl;

	dev = drm_get_device_from_kdev(kdev);

	retcode = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (retcode !=0) {
		DRM_ERROR("can't find authenticator\n");
		return EINVAL;
	}

	atomic_inc(&dev->counts[_DRM_STAT_IOCTLS]);

	if (drm_device_is_unplugged(dev))
		return ENODEV;

	is_driver_ioctl = nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END;

	if (IOCGROUP(cmd) != DRM_IOCTL_BASE) {
		DRM_DEBUG_FIOCTL("Bad ioctl group 0x%x\n", (int)IOCGROUP(cmd));
		return EINVAL;
	}

	ioctl = &drm_ioctls[nr];
	/* It's not a core DRM ioctl, try driver-specific. */
	if (ioctl->func == NULL && nr >= DRM_COMMAND_BASE) {
		/* The array entries begin at DRM_COMMAND_BASE ioctl nr */
		nr -= DRM_COMMAND_BASE;
		if (nr >= dev->driver->num_ioctls) {
			return EINVAL;
		}
		ioctl = &dev->driver->ioctls[nr];
		is_driver_ioctl = 1;
	}

	DRM_DEBUG_IOCTL("pid=%d, dev=0x%lx, auth=%d, %s\n",
		  DRM_CURRENTPID, (long)dev->dev,
		  file_priv->authenticated, ioctl->name);

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		DRM_DEBUG("no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	if (((ioctl->flags & DRM_ROOT_ONLY) && !capable(CAP_SYS_ADMIN)) ||
	    ((ioctl->flags & DRM_AUTH) && !file_priv->authenticated) ||
	    ((ioctl->flags & DRM_MASTER) && !file_priv->master))
		return EACCES;

	/* Enforce sane locking for kms driver ioctls. Core ioctls are
	 * too messy still. */
	if (is_driver_ioctl) {
		if ((ioctl->flags & DRM_UNLOCKED) == 0)
			mutex_lock(&drm_global_mutex);
		/* shared code returns -errno */
		retcode = -func(dev, data, file_priv);
		if (retcode == ERESTARTSYS)
			retcode = EINTR;
		if ((ioctl->flags & DRM_UNLOCKED) == 0)
			mutex_unlock(&drm_global_mutex);
	} else {
		retcode = -func(dev, data, file_priv);
		if (retcode == ERESTARTSYS)
			retcode = EINTR;
	}

      err_i1:
	if (!ioctl)
		DRM_DEBUG_FIOCTL("invalid ioctl: pid=%d, dev=0x%lx, auth=%d, cmd=0x%02lx, nr=0x%02x\n",
			  DRM_CURRENTPID,
			  (long)dev->dev,
			  file_priv->authenticated, cmd, nr);

	if (retcode)
		DRM_DEBUG_FIOCTL("ret = %d\n", retcode);
	return retcode;
}
EXPORT_SYMBOL(drm_ioctl);

/**
 * drm_ioctl_flags - Check for core ioctl and return ioctl permission flags
 * @nr: ioctl number
 * @flags: where to return the ioctl permission flags
 *
 * This ioctl is only used by the vmwgfx driver to augment the access checks
 * done by the drm core and insofar a pretty decent layering violation. This
 * shouldn't be used by any drivers.
 *
 * Returns:
 * True if the @nr corresponds to a DRM core ioctl numer, false otherwise.
 */
bool drm_ioctl_flags(unsigned int nr, unsigned int *flags)
{
	if ((nr >= DRM_COMMAND_END && nr < DRM_CORE_IOCTL_COUNT) ||
	    (nr < DRM_COMMAND_BASE)) {
		*flags = drm_ioctls[nr].flags;
		return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_ioctl_flags);
