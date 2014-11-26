/*-
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 * $FreeBSD: src/sys/dev/drm2/drm_context.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

/** @file drm_context.c
 * Implementation of the context management ioctls.
 */

#include <drm/drmP.h>

/* ================================================================
 * Context bitmap support
 */

void drm_ctxbitmap_free(struct drm_device *dev, int ctx_handle)
{
	if (ctx_handle < 0 || ctx_handle >= DRM_MAX_CTXBITMAP || 
	    dev->ctx_bitmap == NULL) {
		DRM_ERROR("Attempt to free invalid context handle: %d\n",
		   ctx_handle);
		return;
	}

	DRM_LOCK(dev);
	clear_bit(ctx_handle, dev->ctx_bitmap);
	dev->context_sareas[ctx_handle] = NULL;
	DRM_UNLOCK(dev);
	return;
}

int drm_ctxbitmap_next(struct drm_device *dev)
{
	int bit;

	if (dev->ctx_bitmap == NULL)
		return -1;

	DRM_LOCK(dev);
	bit = find_first_zero_bit(dev->ctx_bitmap, DRM_MAX_CTXBITMAP);
	if (bit >= DRM_MAX_CTXBITMAP) {
		DRM_UNLOCK(dev);
		return -1;
	}

	set_bit(bit, dev->ctx_bitmap);
	DRM_DEBUG("bit : %d\n", bit);
	if ((bit+1) > dev->max_context) {
		drm_local_map_t **ctx_sareas;
		int max_ctx = (bit+1);

		ctx_sareas = krealloc(dev->context_sareas,
		    max_ctx * sizeof(*dev->context_sareas),
		    M_DRM, M_WAITOK | M_NULLOK);
		if (ctx_sareas == NULL) {
			clear_bit(bit, dev->ctx_bitmap);
			DRM_DEBUG("failed to allocate bit : %d\n", bit);
			DRM_UNLOCK(dev);
			return -1;
		}
		dev->max_context = max_ctx;
		dev->context_sareas = ctx_sareas;
		dev->context_sareas[bit] = NULL;
	}
	DRM_UNLOCK(dev);
	return bit;
}

int drm_ctxbitmap_init(struct drm_device *dev)
{
	int i;
   	int temp;

	DRM_LOCK(dev);
	dev->ctx_bitmap = kmalloc(PAGE_SIZE, M_DRM,
				  M_WAITOK | M_NULLOK | M_ZERO);
	if (dev->ctx_bitmap == NULL) {
		DRM_UNLOCK(dev);
		return ENOMEM;
	}
	dev->context_sareas = NULL;
	dev->max_context = -1;
	DRM_UNLOCK(dev);

	for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
		temp = drm_ctxbitmap_next(dev);
		DRM_DEBUG("drm_ctxbitmap_init : %d\n", temp);
	}

	return 0;
}

void drm_ctxbitmap_cleanup(struct drm_device *dev)
{
	DRM_LOCK(dev);
	if (dev->context_sareas != NULL)
		drm_free(dev->context_sareas, M_DRM);
	drm_free(dev->ctx_bitmap, M_DRM);
	DRM_UNLOCK(dev);
}

/* ================================================================
 * Per Context SAREA Support
 */

int drm_getsareactx(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_ctx_priv_map *request = data;
	drm_local_map_t *map;

	DRM_LOCK(dev);
	if (dev->max_context < 0 ||
	    request->ctx_id >= (unsigned) dev->max_context) {
		DRM_UNLOCK(dev);
		return EINVAL;
	}

	map = dev->context_sareas[request->ctx_id];
	DRM_UNLOCK(dev);

	request->handle = (void *)map->handle;

	return 0;
}

/**
 * Set per-context SAREA.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx_priv_map structure.
 * \return zero on success or a negative number on failure.
 *
 * Searches the mapping specified in \p arg and update the entry in
 * drm_device::ctx_idr with it.
 */
int drm_setsareactx(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_ctx_priv_map *request = data;
	struct drm_local_map *map = NULL;
	struct drm_map_list *r_list = NULL;

	DRM_LOCK(dev);
	list_for_each_entry(r_list, &dev->maplist, head) {
		if (r_list->map
		    && r_list->map->handle == request->handle) {
			if (dev->max_context < 0)
				goto bad;
			if (request->ctx_id >= (unsigned) dev->max_context)
				goto bad;
			map = r_list->map;
			dev->context_sareas[request->ctx_id] = map;
			DRM_UNLOCK(dev);
			return 0;
		}
	}

bad:
	DRM_UNLOCK(dev);
	return EINVAL;
}

/*@}*/

/******************************************************************/
/** \name The actual DRM context handling routines */
/*@{*/

/**
 * Switch context.
 *
 * \param dev DRM device.
 * \param old old context handle.
 * \param new new context handle.
 * \return zero on success or a negative number on failure.
 *
 * Attempt to set drm_device::context_flag.
 */
static int drm_context_switch(struct drm_device * dev, int old, int new)
{
	if (test_and_set_bit(0, &dev->context_flag)) {
		DRM_ERROR("Reentering -- FIXME\n");
		return -EBUSY;
	}

	DRM_DEBUG("Context switch from %d to %d\n", old, new);

	if (new == dev->last_context) {
		clear_bit(0, &dev->context_flag);
		return 0;
	}

	return 0;
}

/**
 * Complete context switch.
 *
 * \param dev DRM device.
 * \param new new context handle.
 * \return zero on success or a negative number on failure.
 *
 * Updates drm_device::last_context and drm_device::last_switch. Verifies the
 * hardware lock is held, clears the drm_device::context_flag and wakes up
 * drm_device::context_wait.
 */
static int drm_context_switch_complete(struct drm_device *dev, int new)
{
	dev->last_context = new;  /* PRE/POST: This is the _only_ writer. */

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("Lock isn't held after context switch\n");
	}

	/* If a context switch is ever initiated
	   when the kernel holds the lock, release
	   that lock here. */
	clear_bit(0, &dev->context_flag);

	return 0;
}

int drm_resctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_ctx_res *res = data;
	struct drm_ctx ctx;
	int i;

	if (res->count >= DRM_RESERVED_CONTEXTS) {
		bzero(&ctx, sizeof(ctx));
		for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
			ctx.handle = i;
			if (DRM_COPY_TO_USER(&res->contexts[i],
			    &ctx, sizeof(ctx)))
				return EFAULT;
		}
	}
	res->count = DRM_RESERVED_CONTEXTS;

	return 0;
}

int drm_addctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_ctx *ctx = data;

	ctx->handle = drm_ctxbitmap_next(dev);
	if (ctx->handle == DRM_KERNEL_CONTEXT) {
		/* Skip kernel's context and get a new one. */
		ctx->handle = drm_ctxbitmap_next(dev);
	}
	DRM_DEBUG("%d\n", ctx->handle);
	if (ctx->handle == -1) {
		DRM_DEBUG("Not enough free contexts.\n");
		/* Should this return -EBUSY instead? */
		return ENOMEM;
	}

	if (dev->driver->context_ctor && ctx->handle != DRM_KERNEL_CONTEXT) {
		DRM_LOCK(dev);
		dev->driver->context_ctor(dev, ctx->handle);
		DRM_UNLOCK(dev);
	}

	return 0;
}

int drm_modctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	/* This does nothing */
	return 0;
}

int drm_getctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_ctx *ctx = data;

	/* This is 0, because we don't handle any context flags */
	ctx->flags = 0;

	return 0;
}

int drm_switchctx(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_ctx *ctx = data;

	DRM_DEBUG("%d\n", ctx->handle);
	return drm_context_switch(dev, dev->last_context, ctx->handle);
}

int drm_newctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_ctx *ctx = data;

	DRM_DEBUG("%d\n", ctx->handle);
	drm_context_switch_complete(dev, ctx->handle);

	return 0;
}

int drm_rmctx(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_ctx *ctx = data;

	DRM_DEBUG("%d\n", ctx->handle);
	if (ctx->handle != DRM_KERNEL_CONTEXT) {
		if (dev->driver->context_dtor) {
			DRM_LOCK(dev);
			dev->driver->context_dtor(dev, ctx->handle);
			DRM_UNLOCK(dev);
		}

		drm_ctxbitmap_free(dev, ctx->handle);
	}

	return 0;
}
