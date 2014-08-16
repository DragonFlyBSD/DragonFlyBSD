/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*-
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_ringbuffer.h"
#include <linux/workqueue.h>

extern struct drm_i915_private *i915_mch_dev;

#define LP_RING(d) (&((struct drm_i915_private *)(d))->ring[RCS])

#define BEGIN_LP_RING(n) \
	intel_ring_begin(LP_RING(dev_priv), (n))

#define OUT_RING(x) \
	intel_ring_emit(LP_RING(dev_priv), x)

#define ADVANCE_LP_RING() \
	intel_ring_advance(LP_RING(dev_priv))

/**
 * Lock test for when it's just for synchronization of ring access.
 *
 * In that case, we don't need to do it when GEM is initialized as nobody else
 * has access to the ring.
 */
#define RING_LOCK_TEST_WITH_RETURN(dev, file) do {			\
	if (LP_RING(dev->dev_private)->obj == NULL)			\
		LOCK_TEST_WITH_RETURN(dev, file);			\
} while (0)

static inline u32
intel_read_legacy_status_page(struct drm_i915_private *dev_priv, int reg)
{
	if (I915_NEED_GFX_HWS(dev_priv->dev))
		return ioread32(dev_priv->dri1.gfx_hws_cpu_addr + reg);
	else
		return intel_read_status_page(LP_RING(dev_priv), reg);
}

#define READ_HWSP(dev_priv, reg) intel_read_legacy_status_page(dev_priv, reg)
#define READ_BREADCRUMB(dev_priv) READ_HWSP(dev_priv, I915_BREADCRUMB_INDEX)
#define I915_BREADCRUMB_INDEX		0x21

void i915_update_dri1_breadcrumb(struct drm_device *dev)
{
	/*
	 * The dri breadcrumb update races against the drm master disappearing.
	 * Instead of trying to fix this (this is by far not the only ums issue)
	 * just don't do the update in kms mode.
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	/* XXX: don't do it at all actually */
	return;
}

static void i915_write_hws_pga(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 addr;

	addr = dev_priv->status_page_dmah->busaddr;
	if (INTEL_INFO(dev)->gen >= 4)
		addr |= (dev_priv->status_page_dmah->busaddr >> 28) & 0xf0;
	I915_WRITE(HWS_PGA, addr);
}

/**
 * Sets up the hardware status page for devices that need a physical address
 * in the register.
 */
static int i915_init_phys_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	/*
	 * Program Hardware Status Page
	 * XXXKIB Keep 4GB limit for allocation for now.  This method
	 * of allocation is used on <= 965 hardware, that has several
	 * erratas regarding the use of physical memory > 4 GB.
	 */
	DRM_UNLOCK(dev);
	dev_priv->status_page_dmah =
		drm_pci_alloc(dev, PAGE_SIZE, PAGE_SIZE, 0xffffffff);
	DRM_LOCK(dev);
	if (!dev_priv->status_page_dmah) {
		DRM_ERROR("Can not allocate hardware status page\n");
		return -ENOMEM;
	}
	ring->status_page.page_addr = dev_priv->hw_status_page =
	    dev_priv->status_page_dmah->vaddr;
	dev_priv->dma_status_page = dev_priv->status_page_dmah->busaddr;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);

	i915_write_hws_pga(dev);
	DRM_DEBUG("Enabled hardware status page, phys %jx\n",
	    (uintmax_t)dev_priv->dma_status_page);
	return 0;
}

/**
 * Frees the hardware status page, whether it's a physical address or a virtual
 * address set up by the X Server.
 */
static void i915_free_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	if (dev_priv->status_page_dmah) {
		drm_pci_free(dev, dev_priv->status_page_dmah);
		dev_priv->status_page_dmah = NULL;
	}

	if (dev_priv->status_gfx_addr) {
		dev_priv->status_gfx_addr = 0;
		ring->status_page.gfx_addr = 0;
		drm_core_ioremapfree(&dev_priv->hws_map, dev);
	}

	/* Need to rewrite hardware status page */
	I915_WRITE(HWS_PGA, 0x1ffff000);
}

void i915_kernel_lost_context(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	/*
	 * We should never lose context on the ring with modesetting
	 * as we don't expose it to userspace
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ring->head = I915_READ_HEAD(ring) & HEAD_ADDR;
	ring->tail = I915_READ_TAIL(ring) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + I915_RING_FREE_SPACE);
	if (ring->space < 0)
		ring->space += ring->size;

#if 1
	KIB_NOTYET();
#else
	if (!dev->primary->master)
		return;
#endif

	if (ring->head == ring->tail && dev_priv->sarea_priv)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;


	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	DRM_LOCK(dev);
	for (i = 0; i < I915_NUM_RINGS; i++)
		intel_cleanup_ring_buffer(&dev_priv->ring[i]);
	DRM_UNLOCK(dev);

	/* Clear the HWS virtual address at teardown */
	if (I915_NEED_GFX_HWS(dev))
		i915_free_hws(dev);

	return 0;
}

static int i915_initialize(struct drm_device * dev, drm_i915_init_t * init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		i915_dma_cleanup(dev);
		return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_i915_sarea_t *)
	    ((u8 *) dev_priv->sarea->virtual + init->sarea_priv_offset);

	if (init->ring_size != 0) {
		if (LP_RING(dev_priv)->obj != NULL) {
			i915_dma_cleanup(dev);
			DRM_ERROR("Client tried to initialize ringbuffer in "
				  "GEM mode\n");
			return -EINVAL;
		}

		ret = intel_render_ring_init_dri(dev,
						 init->ring_start,
						 init->ring_size);
		if (ret) {
			i915_dma_cleanup(dev);
			return ret;
		}
	}

	dev_priv->cpp = init->cpp;
	dev_priv->back_offset = init->back_offset;
	dev_priv->front_offset = init->front_offset;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pf_current_page = 0;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->dri1.allow_batchbuffer = 1;

	return 0;
}

static int i915_dma_resume(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	DRM_DEBUG("\n");

	if (ring->virtual_start == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	/* Program Hardware Status Page */
	if (!ring->status_page.page_addr) {
		DRM_ERROR("Can not find hardware status page\n");
		return -EINVAL;
	}
	DRM_DEBUG("hw status page @ %p\n", ring->status_page.page_addr);
	if (ring->status_page.gfx_addr != 0)
		intel_ring_setup_status_page(ring);
	else
		i915_write_hws_pga(dev);

	DRM_DEBUG("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case I915_INIT_DMA:
		retcode = i915_initialize(dev, init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int do_validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
		break;
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

	return 0;
}

static int validate_cmd(int cmd)
{
	int ret = do_validate_cmd(cmd);

/*	printk("validate_cmd( %x ): %d\n", cmd, ret); */

	return ret;
}

static int i915_emit_cmds(struct drm_device *dev, int __user *buffer,
			  int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i, ret;

	if ((dwords+1) * sizeof(int) >= LP_RING(dev_priv)->size - 8)
		return -EINVAL;

	ret = BEGIN_LP_RING((dwords+1)&~1);
	if (ret)
		return ret;

	for (i = 0; i < dwords;) {
		int cmd, sz;

		if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i], sizeof(cmd)))
			return -EINVAL;

		if ((sz = validate_cmd(cmd)) == 0 || i + sz > dwords)
			return -EINVAL;

		OUT_RING(cmd);

		while (++i, --sz) {
			if (DRM_COPY_FROM_USER_UNCHECKED(&cmd, &buffer[i],
							 sizeof(cmd))) {
				return -EINVAL;
			}
			OUT_RING(cmd);
		}
	}

	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();

	return 0;
}

int
i915_emit_box(struct drm_device *dev,
	      struct drm_clip_rect *box,
	      int DR1, int DR4)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (box->y2 <= box->y1 || box->x2 <= box->x1 ||
	    box->y2 <= 0 || box->x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box->x1, box->y1, box->x2, box->y2);
		return -EINVAL;
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		ret = BEGIN_LP_RING(4);
		if (ret)
			return ret;

		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box->x1 & 0xffff) | (box->y1 << 16));
		OUT_RING(((box->x2 - 1) & 0xffff) | ((box->y2 - 1) << 16));
		OUT_RING(DR4);
	} else {
		ret = BEGIN_LP_RING(6);
		if (ret)
			return ret;

		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box->x1 & 0xffff) | (box->y1 << 16));
		OUT_RING(((box->x2 - 1) & 0xffff) | ((box->y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
	}
	ADVANCE_LP_RING();

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit. For now, do it in both places:
 */

static void i915_emit_breadcrumb(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	dev_priv->dri1.counter++;
	if (dev_priv->dri1.counter > 0x7FFFFFFFUL)
		dev_priv->dri1.counter = 0;
	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}
}

static int i915_dispatch_cmdbuffer(struct drm_device * dev,
				   drm_i915_cmdbuffer_t *cmd,
				   struct drm_clip_rect *cliprects,
				   void *cmdbuf)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, &cliprects[i],
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, cmdbuf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_batchbuffer(struct drm_device * dev,
				     drm_i915_batchbuffer_t * batch,
				     struct drm_clip_rect *cliprects)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int nbox = batch->num_cliprects;
	int i, count, ret;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;
	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, &cliprects[i],
					    batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (!IS_I830(dev) && !IS_845G(dev)) {
			ret = BEGIN_LP_RING(2);
			if (ret)
				return ret;

			if (INTEL_INFO(dev)->gen >= 4) {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6) | MI_BATCH_NON_SECURE_I965);
				OUT_RING(batch->start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			}
		} else {
			ret = BEGIN_LP_RING(4);
			if (ret)
				return ret;

			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
		}
		ADVANCE_LP_RING();
	}


	if (IS_G4X(dev) || IS_GEN5(dev)) {
		if (BEGIN_LP_RING(2) == 0) {
			OUT_RING(MI_FLUSH | MI_NO_WRITE_FLUSH | MI_INVALIDATE_ISP);
			OUT_RING(MI_NOOP);
			ADVANCE_LP_RING();
		}
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_flip(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->sarea_priv)
		return -EINVAL;

	DRM_DEBUG_DRIVER("%s: page=%d pfCurrentPage=%d\n",
			  __func__,
			 dev_priv->dri1.current_page,
			 dev_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	ret = BEGIN_LP_RING(10);
	if (ret)
		return ret;

	OUT_RING(MI_FLUSH | MI_READ_FLUSH);
	OUT_RING(0);

	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | ASYNC_FLIP);
	OUT_RING(0);
	if (dev_priv->dri1.current_page == 0) {
		OUT_RING(dev_priv->dri1.back_offset);
		dev_priv->dri1.current_page = 1;
	} else {
		OUT_RING(dev_priv->dri1.front_offset);
		dev_priv->dri1.current_page = 0;
	}
	OUT_RING(0);

	OUT_RING(MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);

	ADVANCE_LP_RING();

	dev_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter++;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}

	dev_priv->sarea_priv->pf_current_page = dev_priv->dri1.current_page;
	return 0;
}

static int i915_quiescent(struct drm_device *dev)
{
	i915_kernel_lost_context(dev);
	return intel_ring_idle(LP_RING(dev->dev_private));
}

static int
i915_flush_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	int ret;

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	ret = i915_quiescent(dev);
	DRM_UNLOCK(dev);

	return (ret);
}

static int i915_batchbuffer(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv;
	drm_i915_batchbuffer_t *batch = data;
	struct drm_clip_rect *cliprects;
	size_t cliplen;
	int ret;

	if (!dev_priv->dri1.allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return -EINVAL;
	}

	DRM_DEBUG("i915 batchbuffer, start %x used %d cliprects %d\n",
		  batch->start, batch->used, batch->num_cliprects);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	cliplen = batch->num_cliprects * sizeof(struct drm_clip_rect);
	if (batch->num_cliprects < 0)
		return -EFAULT;
	if (batch->num_cliprects != 0) {
		cliprects = kmalloc(batch->num_cliprects *
		    sizeof(struct drm_clip_rect), DRM_MEM_DMA,
		    M_WAITOK | M_ZERO);

		ret = -copyin(batch->cliprects, cliprects,
		    batch->num_cliprects * sizeof(struct drm_clip_rect));
		if (ret != 0) {
			ret = -EFAULT;
			goto fail_free;
		}
	} else
		cliprects = NULL;

	DRM_LOCK(dev);
	ret = i915_dispatch_batchbuffer(dev, batch, cliprects);
	DRM_UNLOCK(dev);

	sarea_priv = (drm_i915_sarea_t *)dev_priv->sarea_priv;
	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_free:
	drm_free(cliprects, DRM_MEM_DMA);
	return ret;
}

static int i915_cmdbuffer(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	drm_i915_sarea_t *sarea_priv;
	drm_i915_cmdbuffer_t *cmdbuf = data;
	struct drm_clip_rect *cliprects = NULL;
	void *batch_data;
	int ret;

	DRM_DEBUG("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
		  cmdbuf->buf, cmdbuf->sz, cmdbuf->num_cliprects);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (cmdbuf->num_cliprects < 0)
		return -EINVAL;

	batch_data = kmalloc(cmdbuf->sz, DRM_MEM_DMA, M_WAITOK);

	ret = -copyin(cmdbuf->buf, batch_data, cmdbuf->sz);
	if (ret != 0) {
		ret = -EFAULT;
		goto fail_batch_free;
	}

	if (cmdbuf->num_cliprects) {
		cliprects = kmalloc(cmdbuf->num_cliprects *
		    sizeof(struct drm_clip_rect), DRM_MEM_DMA,
		    M_WAITOK | M_ZERO);
		ret = -copyin(cmdbuf->cliprects, cliprects,
		    cmdbuf->num_cliprects * sizeof(struct drm_clip_rect));

		if (ret != 0) {
			ret = -EFAULT;
			goto fail_clip_free;
		}
	}

	DRM_LOCK(dev);
	ret = i915_dispatch_cmdbuffer(dev, cmdbuf, cliprects, batch_data);
	DRM_UNLOCK(dev);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		goto fail_clip_free;
	}

	sarea_priv = (drm_i915_sarea_t *)dev_priv->sarea_priv;
	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_clip_free:
	drm_free(cliprects, DRM_MEM_DMA);
fail_batch_free:
	drm_free(batch_data, DRM_MEM_DMA);
	return ret;
}

static int i915_emit_irq(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
#if 0
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
#endif

	i915_kernel_lost_context(dev);

	DRM_DEBUG_DRIVER("\n");

	dev_priv->dri1.counter++;
	if (dev_priv->dri1.counter > 0x7FFFFFFFUL)
		dev_priv->dri1.counter = 1;
	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(MI_USER_INTERRUPT);
		ADVANCE_LP_RING();
	}

	return dev_priv->dri1.counter;
}

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
#if 0
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
#endif
	int ret = 0;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	DRM_DEBUG_DRIVER("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

#if 0
	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
		return 0;
	}

	if (master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
#else
	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (dev_priv->sarea_priv) {
			dev_priv->sarea_priv->last_dispatch =
				READ_BREADCRUMB(dev_priv);
		}
		return 0;
	}

	if (dev_priv->sarea_priv)
		dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;
#endif

	if (ring->irq_get(ring)) {
		DRM_WAIT_ON(ret, ring->irq_queue, 3 * DRM_HZ,
			    READ_BREADCRUMB(dev_priv) >= irq_nr);
		ring->irq_put(ring);
	} else if (wait_for(READ_BREADCRUMB(dev_priv) >= irq_nr, 3000))
		ret = -EBUSY;

	if (ret == -EBUSY) {
		DRM_ERROR("EBUSY -- rec: %d emitted: %d\n",
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->dri1.counter);
	}

	return ret;
}

/* Needs the lock as it touches the ring.
 */
int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t *emit = data;
	int result;

	if (!dev_priv || !LP_RING(dev_priv)->virtual_start) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	result = i915_emit_irq(dev);
	DRM_UNLOCK(dev);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t *irqwait = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return i915_wait_irq(dev, irqwait->irq_seq);
}

static int i915_vblank_pipe_get(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	pipe->pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	return 0;
}

/**
 * Schedule buffer swap at given vertical blank.
 */
static int i915_vblank_swap(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	/* The delayed swap mechanism was fundamentally racy, and has been
	 * removed.  The model was that the client requested a delayed flip/swap
	 * from the kernel, then waited for vblank before continuing to perform
	 * rendering.  The problem was that the kernel might wake the client
	 * up before it dispatched the vblank swap (since the lock has to be
	 * held while touching the ringbuffer), in which case the client would
	 * clear and start the next frame before the swap occurred, and
	 * flicker would occur in addition to likely missing the vblank.
	 *
	 * In the absence of this ioctl, userland falls back to a correct path
	 * of waiting for a vblank, then dispatching the swap on its own.
	 * Context switching to userland and back is plenty fast enough for
	 * meeting the requirements of vblank swapping.
	 */
	return -EINVAL;
}

static int i915_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	int ret;

	DRM_DEBUG("%s\n", __func__);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	ret = i915_dispatch_flip(dev);
	DRM_UNLOCK(dev);

	return ret;
}

static int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq_enabled ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->dri1.allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pci_device;
		break;
	case I915_PARAM_HAS_GEM:
		value = 1;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs - dev_priv->fence_reg_start;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = dev_priv->overlay ? 1 : 0;
		break;
	case I915_PARAM_HAS_PAGEFLIPPING:
		value = 1;
		break;
	case I915_PARAM_HAS_EXECBUF2:
		/* depends on GEM */
		value = 1;
		break;
	case I915_PARAM_HAS_BSD:
		value = intel_ring_initialized(&dev_priv->ring[VCS]);
		break;
	case I915_PARAM_HAS_BLT:
		value = intel_ring_initialized(&dev_priv->ring[BCS]);
		break;
	case I915_PARAM_HAS_RELAXED_FENCING:
		value = 1;
		break;
	case I915_PARAM_HAS_COHERENT_RINGS:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		value = INTEL_INFO(dev)->gen >= 4;
		break;
	case I915_PARAM_HAS_RELAXED_DELTA:
		value = 1;
		break;
	case I915_PARAM_HAS_GEN7_SOL_RESET:
		value = 1;
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(dev);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = dev_priv->mm.aliasing_ppgtt ? 1 : 0;
		break;
	case I915_PARAM_HAS_PINNED_BATCHES:
		value = 1;
		break;
	default:
		DRM_DEBUG_DRIVER("Unknown parameter %d\n",
				 param->param);
		return -EINVAL;
	}

	if (DRM_COPY_TO_USER(param->value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
		return -EFAULT;
	}

	return 0;
}

static int i915_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t *param = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->dri1.allow_batchbuffer = param->value;
		break;
	case I915_SETPARAM_NUM_USED_FENCES:
		if (param->value > dev_priv->num_fence_regs ||
		    param->value < 0)
			return -EINVAL;
		/* Userspace can use first N regs */
		dev_priv->fence_reg_start = param->value;
		break;
	default:
		DRM_DEBUG("unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	return 0;
}

static int i915_set_status_page(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_hws_addr_t *hws = data;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	if (!I915_NEED_GFX_HWS(dev))
		return -EINVAL;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("set status page addr 0x%08x\n", (u32)hws->addr);
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		DRM_ERROR("tried to set status page when mode setting active\n");
		return 0;
	}

	ring->status_page.gfx_addr = dev_priv->status_gfx_addr =
	    hws->addr & (0x1ffff<<12);

	dev_priv->hws_map.offset = dev->agp->base + hws->addr;
	dev_priv->hws_map.size = 4*1024;
	dev_priv->hws_map.type = 0;
	dev_priv->hws_map.flags = 0;
	dev_priv->hws_map.mtrr = 0;

	drm_core_ioremap_wc(&dev_priv->hws_map, dev);
	if (dev_priv->hws_map.virtual == NULL) {
		i915_dma_cleanup(dev);
		ring->status_page.gfx_addr = dev_priv->status_gfx_addr = 0;
		DRM_ERROR("can not ioremap virtual address for"
				" G33 hw status page\n");
		return -ENOMEM;
	}
	ring->status_page.page_addr = dev_priv->hw_status_page =
	    dev_priv->hws_map.virtual;

	memset(dev_priv->hw_status_page, 0, PAGE_SIZE);
	I915_WRITE(HWS_PGA, dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws HWS_PGA with gfx mem 0x%x\n",
			dev_priv->status_gfx_addr);
	DRM_DEBUG("load hws at %p\n", dev_priv->hw_status_page);
	return 0;
}

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_parse_bios(dev);
	if (ret)
		DRM_INFO("failed to find VBIOS tables\n");

#if 0
	/* If we have > 1 VGA cards, then we need to arbitrate access
	 * to the common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(dev->pdev, dev, NULL, i915_vga_set_decode);
	if (ret && ret != -ENODEV)
		goto out;

	intel_register_dsm_handler();

	ret = vga_switcheroo_register_client(dev->pdev,
					     i915_switcheroo_set_state,
					     NULL,
					     i915_switcheroo_can_switch);
	if (ret)
		goto cleanup_vga_client;

	/* Initialise stolen first so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	ret = i915_gem_init_stolen(dev);
	if (ret)
		goto cleanup_vga_switcheroo;
#endif

	intel_modeset_init(dev);

	ret = i915_gem_init(dev);
	if (ret)
		goto cleanup_gem_stolen;

	intel_modeset_gem_init(dev);

	ret = drm_irq_install(dev);
	if (ret)
		goto cleanup_gem;

	/* Always safe in the mode setting case. */
	/* FIXME: do pre/post-mode set stuff in core KMS code */
	dev->vblank_disable_allowed = 1;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_irq;

	drm_kms_helper_poll_init(dev);

	/* We're off and running w/KMS */
	dev_priv->mm.suspended = 0;

	return 0;

cleanup_irq:
	drm_irq_uninstall(dev);
cleanup_gem:
	DRM_LOCK(dev);
	i915_gem_cleanup_ringbuffer(dev);
	DRM_UNLOCK(dev);
	i915_gem_cleanup_aliasing_ppgtt(dev);
cleanup_gem_stolen:
#if 0
	i915_gem_cleanup_stolen(dev);
cleanup_vga_switcheroo:
	vga_switcheroo_unregister_client(dev->pdev);
cleanup_vga_client:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
out:
#endif
	return ret;
}

static int i915_get_bridge_dev(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->bridge_dev = pci_find_dbsf(0, 0, 0, 0);
	if (!dev_priv->bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}
	return 0;
}

#define MCHBAR_I915 0x44
#define MCHBAR_I965 0x48
#define MCHBAR_SIZE (4*4096)

#define DEVEN_REG 0x54
#define   DEVEN_MCHBAR_EN (1 << 28)

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv;
	device_t vga;
	int reg;
	u32 temp_lo, temp_hi;
	u64 mchbar_addr, temp;

	dev_priv = dev->dev_private;
	reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	if (INTEL_INFO(dev)->gen >= 4)
		temp_hi = pci_read_config(dev_priv->bridge_dev, reg + 4, 4);
	else
		temp_hi = 0;
	temp_lo = pci_read_config(dev_priv->bridge_dev, reg, 4);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef XXX_CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;
#endif

	/* Get some space for it */
	vga = device_get_parent(dev->dev);
	dev_priv->mch_res_rid = 0x100;
	dev_priv->mch_res = BUS_ALLOC_RESOURCE(device_get_parent(vga),
	    dev->dev, SYS_RES_MEMORY, &dev_priv->mch_res_rid, 0, ~0UL,
	    MCHBAR_SIZE, RF_ACTIVE | RF_SHAREABLE, -1);
	if (dev_priv->mch_res == NULL) {
		DRM_ERROR("failed mchbar resource alloc\n");
		return (-ENOMEM);
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		temp = rman_get_start(dev_priv->mch_res);
		temp >>= 32;
		pci_write_config(dev_priv->bridge_dev, reg + 4, temp, 4);
	}
	pci_write_config(dev_priv->bridge_dev, reg,
	    rman_get_start(dev_priv->mch_res) & UINT32_MAX, 4);
	return (0);
}

static void
intel_setup_mchbar(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv;
	int mchbar_reg;
	u32 temp;
	bool enabled;

	dev_priv = dev->dev_private;
	mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		temp = pci_read_config(dev_priv->bridge_dev, DEVEN_REG, 4);
		enabled = (temp & DEVEN_MCHBAR_EN) != 0;
	} else {
		temp = pci_read_config(dev_priv->bridge_dev, mchbar_reg, 4);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled) {
		DRM_DEBUG("mchbar already enabled\n");
		return;
	}

	if (intel_alloc_mchbar_resource(dev))
		return;

	dev_priv->mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_write_config(dev_priv->bridge_dev, DEVEN_REG,
		    temp | DEVEN_MCHBAR_EN, 4);
	} else {
		temp = pci_read_config(dev_priv->bridge_dev, mchbar_reg, 4);
		pci_write_config(dev_priv->bridge_dev, mchbar_reg, temp | 1, 4);
	}
}

static void
intel_teardown_mchbar(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv;
	device_t vga;
	int mchbar_reg;
	u32 temp;

	dev_priv = dev->dev_private;
	mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
			temp = pci_read_config(dev_priv->bridge_dev,
			    DEVEN_REG, 4);
			temp &= ~DEVEN_MCHBAR_EN;
			pci_write_config(dev_priv->bridge_dev, DEVEN_REG,
			    temp, 4);
		} else {
			temp = pci_read_config(dev_priv->bridge_dev,
			    mchbar_reg, 4);
			temp &= ~1;
			pci_write_config(dev_priv->bridge_dev, mchbar_reg,
			    temp, 4);
		}
	}

	if (dev_priv->mch_res != NULL) {
		vga = device_get_parent(dev->dev);
		BUS_DEACTIVATE_RESOURCE(device_get_parent(vga), dev->dev,
		    SYS_RES_MEMORY, dev_priv->mch_res_rid, dev_priv->mch_res);
		BUS_RELEASE_RESOURCE(device_get_parent(vga), dev->dev,
		    SYS_RES_MEMORY, dev_priv->mch_res_rid, dev_priv->mch_res);
		dev_priv->mch_res = NULL;
	}
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long base, size;
	int mmio_bar, ret;

	ret = 0;

	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = kmalloc(sizeof(drm_i915_private_t), DRM_MEM_DRIVER,
	    M_ZERO | M_WAITOK);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = (void *)dev_priv;
	dev_priv->dev = dev;
	dev_priv->info = i915_get_device_id(dev->pci_device);

	if (i915_get_bridge_dev(dev)) {
		drm_free(dev_priv, DRM_MEM_DRIVER);
		return (-EIO);
	}
	dev_priv->mm.gtt = intel_gtt_get();

	/* Add register map (needed for suspend/resume) */
	mmio_bar = IS_GEN2(dev) ? 1 : 0;
	base = drm_get_resource_start(dev, mmio_bar);
	size = drm_get_resource_len(dev, mmio_bar);

	ret = drm_addmap(dev, base, size, _DRM_REGISTERS,
	    _DRM_KERNEL | _DRM_DRIVER, &dev_priv->mmio_map);

	/* The i915 workqueue is primarily used for batched retirement of
	 * requests (and thus managing bo) once the task has been completed
	 * by the GPU. i915_gem_retire_requests() is called directly when we
	 * need high-priority retirement, such as waiting for an explicit
	 * bo.
	 *
	 * It is also used for periodic low-priority events, such as
	 * idle-timers and recording error state.
	 *
	 * All tasks on the workqueue are expected to acquire the dev mutex
	 * so there is no point in running more than one instance of the
	 * workqueue at any time.  Use an ordered one.
	 */
	dev_priv->wq = alloc_ordered_workqueue("i915", 0);
	if (dev_priv->wq == NULL) {
		DRM_ERROR("Failed to create our workqueue.\n");
		ret = -ENOMEM;
		goto out_mtrrfree;
	}

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev);

	intel_irq_init(dev);
	intel_gt_init(dev);

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev);
	intel_setup_gmbus(dev);
	intel_opregion_setup(dev);

	intel_setup_bios(dev);

	i915_gem_load(dev);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, but we use them anyways to avoid
	 * stuck interrupts on some machines.
	 */

	lockinit(&dev_priv->irq_lock, "userirq", 0, LK_CANRECURSE);
	lockinit(&dev_priv->error_lock, "915err", 0, LK_CANRECURSE);
	spin_init(&dev_priv->rps.lock);
	spin_init(&dev_priv->dpio_lock);

	lockinit(&dev_priv->rps.hw_lock, "i915 rps.hw_lock", 0, LK_CANRECURSE);

	/* Init HWS */
	if (!I915_NEED_GFX_HWS(dev)) {
		ret = i915_init_phys_hws(dev);
		if (ret != 0) {
			drm_rmmap(dev, dev_priv->mmio_map);
			drm_free(dev_priv, DRM_MEM_DRIVER);
			return ret;
		}
	}

	if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev))
		dev_priv->num_pipe = 3;
	else if (IS_MOBILE(dev) || !IS_GEN2(dev))
		dev_priv->num_pipe = 2;
	else
		dev_priv->num_pipe = 1;

	ret = drm_vblank_init(dev, dev_priv->num_pipe);
	if (ret)
		goto out_gem_unload;

	/* Start out suspended */
	dev_priv->mm.suspended = 1;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = i915_load_modeset_init(dev);
		if (ret < 0) {
			DRM_ERROR("failed to init modeset\n");
			goto out_gem_unload;
		}
	}

	/* Must be done after probing outputs */
	intel_opregion_init(dev);

	setup_timer(&dev_priv->hangcheck_timer, i915_hangcheck_elapsed,
		    (unsigned long) dev);

	if (IS_GEN5(dev))
		intel_gpu_ips_init(dev_priv);

	return 0;

out_gem_unload:
	intel_teardown_gmbus(dev);
	intel_teardown_mchbar(dev);
	destroy_workqueue(dev_priv->wq);
out_mtrrfree:
	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	intel_gpu_ips_teardown();

	DRM_LOCK(dev);
	ret = i915_gpu_idle(dev);
	if (ret)
		DRM_ERROR("failed to idle hardware: %d\n", ret);
	i915_gem_retire_requests(dev);
	DRM_UNLOCK(dev);

	/* Cancel the retire work handler, which should be idle now. */
	cancel_delayed_work_sync(&dev_priv->mm.retire_work);

	i915_free_hws(dev);

	intel_teardown_mchbar(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		intel_fbdev_fini(dev);
		intel_modeset_cleanup(dev);
	}

	/* Free error state after interrupts are fully disabled. */
	del_timer_sync(&dev_priv->hangcheck_timer);
	cancel_work_sync(&dev_priv->error_work);
	i915_destroy_error_state(dev);

	intel_opregion_fini(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* Flush any outstanding unpin_work. */
		flush_workqueue(dev_priv->wq);

		DRM_LOCK(dev);
		i915_gem_free_all_phys_object(dev);
		i915_gem_cleanup_ringbuffer(dev);
		DRM_UNLOCK(dev);
		i915_gem_cleanup_aliasing_ppgtt(dev);
		drm_mm_takedown(&dev_priv->mm.stolen);

		intel_cleanup_overlay(dev);

		if (!I915_NEED_GFX_HWS(dev))
			i915_free_hws(dev);
	}

	i915_gem_unload(dev);

	bus_generic_detach(dev->dev);
	drm_rmmap(dev, dev_priv->mmio_map);
	intel_teardown_gmbus(dev);

	destroy_workqueue(dev_priv->wq);

	drm_free(dev->dev_private, DRM_MEM_DRIVER);

	return 0;
}

int
i915_driver_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv;

	i915_file_priv = kmalloc(sizeof(*i915_file_priv), DRM_MEM_FILES,
	    M_WAITOK | M_ZERO);

	spin_init(&i915_file_priv->mm.lock);
	INIT_LIST_HEAD(&i915_file_priv->mm.request_list);
	file_priv->driver_priv = i915_file_priv;

	return (0);
}

void
i915_driver_lastclose(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!dev_priv || drm_core_check_feature(dev, DRIVER_MODESET)) {
#if 1
		KIB_NOTYET();
#else
		drm_fb_helper_restore();
		vga_switcheroo_process_delayed_switch();
#endif
		return;
	}
	i915_gem_lastclose(dev);
	i915_dma_cleanup(dev);
}

void i915_driver_preclose(struct drm_device * dev, struct drm_file *file_priv)
{

	i915_gem_release(dev, file_priv);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_i915_file_private *i915_file_priv = file_priv->driver_priv;

	spin_uninit(&i915_file_priv->mm.lock);
	drm_free(i915_file_priv, DRM_MEM_FILES);
}

struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF(DRM_I915_INIT, i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_FLUSH, i915_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FLIP, i915_flip_bufs, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_BATCHBUFFER, i915_batchbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_EMIT, i915_irq_emit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_IRQ_WAIT, i915_irq_wait, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GETPARAM, i915_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_SETPARAM, i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_CMDBUFFER, i915_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_DESTROY_HEAP,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_SET_VBLANK_PIPE,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY ),
	DRM_IOCTL_DEF(DRM_I915_GET_VBLANK_PIPE,  i915_vblank_pipe_get, DRM_AUTH ),
	DRM_IOCTL_DEF(DRM_I915_VBLANK_SWAP, i915_vblank_swap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_HWS_ADDR, i915_set_status_page, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_INIT, i915_gem_init_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_EXECBUFFER2, i915_gem_execbuffer2, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_PIN, i915_gem_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_UNPIN, i915_gem_unpin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_I915_GEM_ENTERVT, i915_gem_entervt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_LEAVEVT, i915_gem_leavevt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_I915_GEM_CREATE, i915_gem_create_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_MMAP, i915_gem_mmap_ioctl, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_SET_TILING, i915_gem_set_tiling, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_GET_TILING, i915_gem_get_tiling, 0),
	DRM_IOCTL_DEF(DRM_I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_OVERLAY_ATTRS, intel_overlay_attrs, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF(DRM_I915_GET_SPRITE_COLORKEY, intel_sprite_get_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
};

struct drm_driver i915_driver_info = {
	.driver_features =   DRIVER_USE_AGP | DRIVER_REQUIRE_AGP |
	    DRIVER_USE_MTRR | DRIVER_HAVE_IRQ | DRIVER_LOCKLESS_IRQ |
	    DRIVER_GEM /*| DRIVER_MODESET*/,

	.buf_priv_size	= sizeof(drm_i915_private_t),
	.load		= i915_driver_load,
	.open		= i915_driver_open,
	.unload		= i915_driver_unload,
	.preclose	= i915_driver_preclose,
	.lastclose	= i915_driver_lastclose,
	.postclose	= i915_driver_postclose,
	.device_is_agp	= i915_driver_device_is_agp,
	.gem_init_object = i915_gem_init_object,
	.gem_free_object = i915_gem_free_object,
	.gem_pager_ops	= &i915_gem_pager_ops,
	.dumb_create	= i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_mmap_gtt,
	.dumb_destroy	= i915_gem_dumb_destroy,

	.ioctls		= i915_ioctls,
	.max_ioctl	= DRM_ARRAY_SIZE(i915_ioctls),

	.name		= DRIVER_NAME,
	.desc		= DRIVER_DESC,
	.date		= DRIVER_DATE,
	.major		= DRIVER_MAJOR,
	.minor		= DRIVER_MINOR,
	.patchlevel	= DRIVER_PATCHLEVEL,
};

/**
 * Determine if the device really is AGP or not.
 *
 * All Intel graphics chipsets are treated as AGP, even if they are really
 * built-in.
 *
 * \param dev   The device to be tested.
 *
 * \returns
 * A value of 1 is always retured to indictate every i9x5 is AGP.
 */
int i915_driver_device_is_agp(struct drm_device * dev)
{
	return 1;
}
