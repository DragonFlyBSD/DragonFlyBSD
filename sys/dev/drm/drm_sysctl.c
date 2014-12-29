/*-
 * Copyright 2003 Eric Anholt
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
 * ERIC ANHOLT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * $FreeBSD: src/sys/dev/drm2/drm_sysctl.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

/** @file drm_sysctl.c
 * Implementation of various sysctls for controlling DRM behavior and reporting
 * debug information.
 */

#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <drm/drmP.h>

static int	   drm_name_info DRM_SYSCTL_HANDLER_ARGS;
static int	   drm_vm_info DRM_SYSCTL_HANDLER_ARGS;
static int	   drm_clients_info DRM_SYSCTL_HANDLER_ARGS;
static int	   drm_bufs_info DRM_SYSCTL_HANDLER_ARGS;
static int	   drm_vblank_info DRM_SYSCTL_HANDLER_ARGS;

struct drm_sysctl_list {
	const char *name;
	int	   (*f) DRM_SYSCTL_HANDLER_ARGS;
} drm_sysctl_list[] = {
	{"name",    drm_name_info},
	{"vm",	    drm_vm_info},
	{"clients", drm_clients_info},
	{"bufs",    drm_bufs_info},
	{"vblank",    drm_vblank_info},
};
#define DRM_SYSCTL_ENTRIES NELEM(drm_sysctl_list)

int drm_sysctl_init(struct drm_device *dev)
{
	struct drm_sysctl_info *info;
	struct sysctl_oid *oid;
	struct sysctl_oid *top, *drioid;
	int		  i;

	info = kmalloc(sizeof *info, M_DRM, M_WAITOK | M_ZERO);
	if ( !info )
		return 1;
	dev->sysctl = info;

	/* Add the sysctl node for DRI if it doesn't already exist */
	drioid = SYSCTL_ADD_NODE(&info->ctx, &sysctl__hw_children, OID_AUTO,
	    "dri", CTLFLAG_RW, NULL, "DRI Graphics");
	if (!drioid)
		return 1;

	/* Find the next free slot under hw.dri */
	i = 0;
	SLIST_FOREACH(oid, SYSCTL_CHILDREN(drioid), oid_link) {
		if (i <= oid->oid_arg2)
			i = oid->oid_arg2 + 1;
	}
	if (i>9)
		return 1;
	
	dev->sysctl_node_idx = i;
	/* Add the hw.dri.x for our device */
	info->name[0] = '0' + i;
	info->name[1] = 0;
	top = SYSCTL_ADD_NODE(&info->ctx, SYSCTL_CHILDREN(drioid),
	    OID_AUTO, info->name, CTLFLAG_RW, NULL, NULL);
	if (!top)
		return 1;
	
	for (i = 0; i < DRM_SYSCTL_ENTRIES; i++) {
		oid = SYSCTL_ADD_OID(&info->ctx,
			SYSCTL_CHILDREN(top),
			OID_AUTO,
			drm_sysctl_list[i].name,
			CTLTYPE_STRING | CTLFLAG_RD,
			dev,
			0,
			drm_sysctl_list[i].f,
			"A",
			NULL);
		if (!oid)
			return 1;
	}
	SYSCTL_ADD_INT(&info->ctx, SYSCTL_CHILDREN(drioid), OID_AUTO, "debug",
	    CTLFLAG_RW, &drm_debug, sizeof(drm_debug),
	    "Enable debugging output");
	SYSCTL_ADD_INT(&info->ctx, SYSCTL_CHILDREN(drioid), OID_AUTO, "notyet",
	    CTLFLAG_RW, &drm_notyet_flag, sizeof(drm_debug),
	    "Enable notyet reminders");

	if (dev->driver->sysctl_init != NULL)
		dev->driver->sysctl_init(dev, &info->ctx, top);

	SYSCTL_ADD_INT(&info->ctx, SYSCTL_CHILDREN(drioid), OID_AUTO,
	    "vblank_offdelay", CTLFLAG_RW, &drm_vblank_offdelay,
	    sizeof(drm_vblank_offdelay),
	    "");
	SYSCTL_ADD_INT(&info->ctx, SYSCTL_CHILDREN(drioid), OID_AUTO,
	    "timestamp_precision", CTLFLAG_RW, &drm_timestamp_precision,
	    sizeof(drm_timestamp_precision),
	    "");

	return (0);
}

int drm_sysctl_cleanup(struct drm_device *dev)
{
	int error;

	error = sysctl_ctx_free(&dev->sysctl->ctx);
	drm_free(dev->sysctl, M_DRM);
	dev->sysctl = NULL;
	if (dev->driver->sysctl_cleanup != NULL)
		dev->driver->sysctl_cleanup(dev);

	return (error);
}

#define DRM_SYSCTL_PRINT(fmt, arg...)				\
do {								\
	ksnprintf(buf, sizeof(buf), fmt, ##arg);			\
	retcode = SYSCTL_OUT(req, buf, strlen(buf));		\
	if (retcode)						\
		goto done;					\
} while (0)

static int drm_name_info DRM_SYSCTL_HANDLER_ARGS
{
	struct drm_device *dev = arg1;
	char buf[128];
	int retcode;
	int hasunique = 0;

	DRM_SYSCTL_PRINT("%s 0x%x", dev->driver->name, dev2udev(dev->devnode));
	
	DRM_LOCK(dev);
	if (dev->unique) {
		ksnprintf(buf, sizeof(buf), " %s", dev->unique);
		hasunique = 1;
	}
	DRM_UNLOCK(dev);
	
	if (hasunique)
		SYSCTL_OUT(req, buf, strlen(buf));

	SYSCTL_OUT(req, "", 1);

done:
	return retcode;
}

/**
 * Called when "/proc/dri/.../vm" is read.
 *
 * Prints information about all mappings in drm_device::maplist.
 */
static int drm_vm_info DRM_SYSCTL_HANDLER_ARGS
{
	char buf[128];
	int retcode;
	struct drm_device *dev = arg1;
	struct drm_local_map *map;
	struct drm_map_list *r_list;

	/* Hardcoded from _DRM_FRAME_BUFFER,
	   _DRM_REGISTERS, _DRM_SHM, _DRM_AGP, and
	   _DRM_SCATTER_GATHER and _DRM_CONSISTENT */
	const char *types[] = { "FB", "REG", "SHM", "AGP", "SG", "PCI" };
	const char *type;
	int i;

	DRM_LOCK(dev);
	DRM_SYSCTL_PRINT("\nslot offset	        size       "
	    "type flags address            handle mtrr\n");
	i = 0;
	list_for_each_entry(r_list, &dev->maplist, head) {
		map = r_list->map;
		if (!map)
			continue;
		if (map->type < 0 || map->type > 5)
			type = "??";
		else
			type = types[map->type];

		DRM_SYSCTL_PRINT("%4d 0x%016llx 0x%08lx %4.4s  0x%02x 0x%08lx ",
			   i,
			   (unsigned long long)map->offset,
			   map->size, type, map->flags,
			   (unsigned long) r_list->user_token);
		if (map->mtrr < 0)
			DRM_SYSCTL_PRINT("none\n");
		else
			DRM_SYSCTL_PRINT("%4d\n", map->mtrr);
		i++;

	}
	SYSCTL_OUT(req, "", 1);
	DRM_UNLOCK(dev);

done:
	return 0;
}

static int drm_bufs_info DRM_SYSCTL_HANDLER_ARGS
{
	struct drm_device	 *dev = arg1;
	drm_device_dma_t *dma = dev->dma;
	drm_device_dma_t tempdma;
	int *templists;
	int i;
	char buf[128];
	int retcode;

	/* We can't hold the locks around DRM_SYSCTL_PRINT, so make a temporary
	 * copy of the whole structure and the relevant data from buflist.
	 */
	DRM_LOCK(dev);
	if (dma == NULL) {
		DRM_UNLOCK(dev);
		return 0;
	}
	spin_lock(&dev->dma_lock);
	tempdma = *dma;
	templists = kmalloc(sizeof(int) * dma->buf_count, M_DRM,
			    M_WAITOK | M_NULLOK);
	for (i = 0; i < dma->buf_count; i++)
		templists[i] = dma->buflist[i]->list;
	dma = &tempdma;
	spin_unlock(&dev->dma_lock);
	DRM_UNLOCK(dev);

	DRM_SYSCTL_PRINT("\n o     size count  free	 segs pages    kB\n");
	for (i = 0; i <= DRM_MAX_ORDER; i++) {
		if (dma->bufs[i].buf_count)
			DRM_SYSCTL_PRINT("%2d %8d %5d %5d %5d %5d %5d\n",
				       i,
				       dma->bufs[i].buf_size,
				       dma->bufs[i].buf_count,
				       atomic_read(&dma->bufs[i]
						   .freelist.count),
				       dma->bufs[i].seg_count,
				       dma->bufs[i].seg_count
				       *(1 << dma->bufs[i].page_order),
				       (dma->bufs[i].seg_count
					* (1 << dma->bufs[i].page_order))
				       * (int)PAGE_SIZE / 1024);
	}
	DRM_SYSCTL_PRINT("\n");
	for (i = 0; i < dma->buf_count; i++) {
		if (i && !(i%32)) DRM_SYSCTL_PRINT("\n");
		DRM_SYSCTL_PRINT(" %d", templists[i]);
	}
	DRM_SYSCTL_PRINT("\n");

	SYSCTL_OUT(req, "", 1);
done:
	drm_free(templists, M_DRM);
	return retcode;
}

static int drm_clients_info DRM_SYSCTL_HANDLER_ARGS
{
	struct drm_device *dev = arg1;
	struct drm_file *priv, *tempprivs;
	char buf[128];
	int retcode;
	int privcount, i;

	DRM_LOCK(dev);

	privcount = 0;
	list_for_each_entry(priv, &dev->filelist, lhead)
		privcount++;

	tempprivs = kmalloc(sizeof(struct drm_file) * privcount, M_DRM,
			    M_WAITOK | M_NULLOK);
	if (tempprivs == NULL) {
		DRM_UNLOCK(dev);
		return ENOMEM;
	}
	i = 0;
	list_for_each_entry(priv, &dev->filelist, lhead)
		tempprivs[i++] = *priv;

	DRM_UNLOCK(dev);

	DRM_SYSCTL_PRINT(
	    "\na dev            pid   uid      magic     ioctls\n");
	for (i = 0; i < privcount; i++) {
		priv = &tempprivs[i];
		DRM_SYSCTL_PRINT("%c %-12s %5d %5d %10u %10lu\n",
			       priv->authenticated ? 'y' : 'n',
			       devtoname(priv->dev->devnode),
			       priv->pid,
			       priv->uid,
			       priv->magic,
			       priv->ioctl_count);
	}

	SYSCTL_OUT(req, "", 1);
done:
	drm_free(tempprivs, M_DRM);
	return retcode;
}

static int drm_vblank_info DRM_SYSCTL_HANDLER_ARGS
{
	struct drm_device *dev = arg1;
	char buf[128];
	int retcode;
	int i;

	DRM_SYSCTL_PRINT("\ncrtc ref count    last     enabled inmodeset\n");
	DRM_LOCK(dev);
	if (dev->_vblank_count == NULL)
		goto done;
	for (i = 0 ; i < dev->num_crtcs ; i++) {
		DRM_SYSCTL_PRINT("  %02d  %02d %08d %08d %02d      %02d\n",
		    i, dev->vblank_refcount[i].counter,
		    dev->_vblank_count[i].counter,
		    dev->last_vblank[i],
		    dev->vblank_enabled[i],
		    dev->vblank_inmodeset[i]);
	}
done:
	DRM_UNLOCK(dev);

	SYSCTL_OUT(req, "", -1);
	return retcode;
}
