/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
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

#ifndef _DRM_P_H_
#define _DRM_P_H_

#include <linux/agp_backend.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/ratelimit.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/dma-fence.h>
#include <linux/module.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <linux/uaccess.h>

#include <uapi/drm/drm.h>
#include <uapi/drm/drm_mode.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_global.h>
#include <drm/drm_hashtab.h>
#include <drm/drm_mm.h>
#include <drm/drm_os_linux.h>
#include <drm/drm_sarea.h>
#include <drm/drm_drv.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>
#include <drm/drm_pci.h>
#include <drm/drm_file.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_sysfs.h>
#include <drm/drm_vblank.h>
#include <drm/drm_irq.h>
#include <drm/drm_device.h>

#ifdef __DragonFly__
#include <sys/conf.h>
#include <sys/sysctl.h>

#include <vm/vm_extern.h>
#include <vm/vm_pager.h>
#endif

struct module;

struct device_node;
struct videomode;
struct reservation_object;
struct dma_buf_attachment;

struct pci_dev;
struct pci_controller;

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

#define DRM_DEV_MODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)
#define DRM_DEV_UID	UID_ROOT
#define DRM_DEV_GID	GID_VIDEO

#define DRM_CURPROC		curthread
#define DRM_STRUCTPROC		struct thread
#define DRM_LOCK(dev)		lockmgr(&(dev)->struct_mutex, LK_EXCLUSIVE)
#define DRM_UNLOCK(dev)		lockmgr(&(dev)->struct_mutex, LK_RELEASE)

#define DRM_SYSCTL_HANDLER_ARGS	(SYSCTL_HANDLER_ARGS)

#define drm_get_device_from_kdev(_kdev) (_kdev->si_drv1)

int vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end,
    vm_memattr_t memattr);
void vm_phys_fictitious_unreg_range(vm_paddr_t start, vm_paddr_t end);
vm_page_t vm_phys_fictitious_to_vm_page(vm_paddr_t pa);

/* Flags and return codes for get_vblank_timestamp() driver function. */
#define DRM_CALLED_FROM_VBLIRQ 1

/* get_scanout_position() return flags */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_IN_VBLANK    (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)

#ifdef __DragonFly__
struct drm_sysctl_info {
	struct sysctl_ctx_list ctx;
	char   name[2];
};

/* Length for the array of resource pointers for drm_get_resource_*. */
#define DRM_MAX_PCI_RESOURCE	6
#endif

#define DRM_SWITCH_POWER_ON 0
#define DRM_SWITCH_POWER_OFF 1
#define DRM_SWITCH_POWER_CHANGING 2
#define DRM_SWITCH_POWER_DYNAMIC_OFF 3

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

#ifdef __DragonFly__
int    drm_create_cdevs(device_t kdev);

d_kqfilter_t drm_kqfilter;
d_mmap_t drm_mmap;
d_mmap_single_t drm_mmap_single;

int drm_device_detach(device_t kdev);

void drm_cdevpriv_dtor(void *cd);

int drm_add_busid_modesetting(struct drm_device *dev,
    struct sysctl_ctx_list *ctx, struct sysctl_oid *top);

/* XXX glue logic, should be done in drm_pci_init(), pending drm update */
void drm_init_pdev(device_t dev, struct pci_dev **pdev);
void drm_fini_pdev(struct pci_dev **pdev);
void drm_print_pdev(struct pci_dev *pdev);
#endif

/* returns true if currently okay to sleep */
static inline bool drm_can_sleep(void)
{
	if (in_atomic() || in_dbg_master())
		return false;
	return true;
}

#ifdef __DragonFly__
struct drm_softc {
	void *drm_driver_data;
};

/* sysctl support (drm_sysctl.h) */
extern int drm_sysctl_init(struct drm_device *dev);
extern int drm_sysctl_cleanup(struct drm_device *dev);
unsigned long drm_get_resource_start(struct drm_device *dev,
				     unsigned int resource);
unsigned long drm_get_resource_len(struct drm_device *dev,
				   unsigned int resource);

int ttm_bo_mmap_single(struct file *fp, struct drm_device *dev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **obj_res, int nprot);

int drm_gem_mmap_single(struct drm_device *dev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **obj_res, int nprot);

static __inline__ int drm_pci_device_is_agp(struct drm_device *dev)
{
	return (pci_find_extcap(dev->pdev->dev.bsddev, PCIY_AGP, NULL) == 0);
}
#endif

#endif
