/*
 * Copyright 2017 Red Hat
 * Parts ported from amdgpu (fence wait code).
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 *
 * Authors:
 *
 */

/**
 * DOC: Overview
 *
 * DRM synchronisation objects (syncobj, see struct &drm_syncobj) are
 * persistent objects that contain an optional fence. The fence can be updated
 * with a new fence, or be NULL.
 *
 * syncobj's can be waited upon, where it will wait for the underlying
 * fence.
 *
 * syncobj's can be export to fd's and back, these fd's are opaque and
 * have no other use case, except passing the syncobj between processes.
 *
 * Their primary use-case is to implement Vulkan fences and semaphores.
 *
 * syncobj have a kref reference count, but also have an optional file.
 * The file is only created once the syncobj is exported.
 * The file takes a reference on the kref.
 */

#include <drm/drmP.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/ktime.h>
#include <linux/sync_file.h>
#include <linux/sched/signal.h>
#include <linux/dma-fence-chain.h>

#include "drm_internal.h"
#include <drm/drm_syncobj.h>

SYSCTL_DECL(_hw_dri);

static uint64_t syncobj_query_count;
static uint64_t syncobj_query_handle_count;
static uint64_t syncobj_query_zero_count;
static uint64_t syncobj_query_lag_count;
static uint64_t syncobj_query_lag_max;
static uint64_t syncobj_query_head_point_max;
static uint64_t syncobj_query_return_point_max;
static uint64_t syncobj_wait_count;
static uint64_t syncobj_wait_handle_count;
static uint64_t syncobj_wait_timeout0_count;
static uint64_t syncobj_wait_wait_for_submit_count;
static uint64_t syncobj_wait_wait_all_count;
static uint64_t syncobj_wait_success_count;
static uint64_t syncobj_wait_etime_count;
static uint64_t syncobj_wait_error_count;
static uint64_t syncobj_wait_timeout0_success_count;
static uint64_t syncobj_wait_timeout0_etime_count;
static uint64_t syncobj_timeline_wait_count;
static uint64_t syncobj_timeline_wait_handle_count;
static uint64_t syncobj_timeline_wait_timeout0_count;
static uint64_t syncobj_timeline_wait_wait_available_count;
static uint64_t syncobj_timeline_wait_wait_for_submit_count;
static uint64_t syncobj_timeline_wait_wait_all_count;
static uint64_t syncobj_timeline_wait_success_count;
static uint64_t syncobj_timeline_wait_etime_count;
static uint64_t syncobj_timeline_wait_error_count;
static uint64_t syncobj_timeline_wait_timeout0_success_count;
static uint64_t syncobj_timeline_wait_timeout0_etime_count;
static uint64_t syncobj_timeline_wait_available_success_count;
static uint64_t syncobj_timeline_wait_available_etime_count;
static uint64_t syncobj_timeline_wait_timeout0_available_count;
static uint64_t syncobj_timeline_wait_timeout0_available_success_count;
static uint64_t syncobj_timeline_wait_timeout0_available_etime_count;
static uint64_t syncobj_timeline_wait_timeout0_complete_count;
static uint64_t syncobj_timeline_wait_timeout0_complete_success_count;
static uint64_t syncobj_timeline_wait_timeout0_complete_etime_count;
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_count, CTLFLAG_RD,
    &syncobj_query_count, 0, "syncobj query ioctl count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_handle_count, CTLFLAG_RD,
    &syncobj_query_handle_count, 0, "syncobj query handle count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_zero_count, CTLFLAG_RD,
    &syncobj_query_zero_count, 0, "syncobj query returned zero count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_lag_count, CTLFLAG_RD,
    &syncobj_query_lag_count, 0, "syncobj query returned below head count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_lag_max, CTLFLAG_RD,
    &syncobj_query_lag_max, 0, "syncobj query maximum head-return gap");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_head_point_max, CTLFLAG_RD,
    &syncobj_query_head_point_max, 0, "syncobj query maximum head point");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_query_return_point_max, CTLFLAG_RD,
    &syncobj_query_return_point_max, 0, "syncobj query maximum returned point");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_count, CTLFLAG_RD,
    &syncobj_wait_count, 0, "syncobj wait ioctl count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_handle_count, CTLFLAG_RD,
    &syncobj_wait_handle_count, 0, "syncobj wait handle count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_timeout0_count, CTLFLAG_RD,
    &syncobj_wait_timeout0_count, 0, "syncobj wait timeout=0 count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_wait_for_submit_count, CTLFLAG_RD,
    &syncobj_wait_wait_for_submit_count, 0, "syncobj wait WAIT_FOR_SUBMIT count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_wait_all_count, CTLFLAG_RD,
    &syncobj_wait_wait_all_count, 0, "syncobj wait WAIT_ALL count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_success_count, CTLFLAG_RD,
    &syncobj_wait_success_count, 0, "syncobj wait success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_etime_count, CTLFLAG_RD,
    &syncobj_wait_etime_count, 0, "syncobj wait ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_error_count, CTLFLAG_RD,
    &syncobj_wait_error_count, 0, "syncobj wait non-ETIME error count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_timeout0_success_count, CTLFLAG_RD,
    &syncobj_wait_timeout0_success_count, 0, "syncobj wait timeout=0 success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_wait_timeout0_etime_count, CTLFLAG_RD,
    &syncobj_wait_timeout0_etime_count, 0, "syncobj wait timeout=0 ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_count, CTLFLAG_RD,
    &syncobj_timeline_wait_count, 0, "syncobj timeline wait ioctl count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_handle_count, CTLFLAG_RD,
    &syncobj_timeline_wait_handle_count, 0, "syncobj timeline wait handle count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_count, 0, "syncobj timeline wait timeout=0 count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_wait_available_count, CTLFLAG_RD,
    &syncobj_timeline_wait_wait_available_count, 0, "syncobj timeline wait WAIT_AVAILABLE count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_wait_for_submit_count, CTLFLAG_RD,
    &syncobj_timeline_wait_wait_for_submit_count, 0, "syncobj timeline wait WAIT_FOR_SUBMIT count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_wait_all_count, CTLFLAG_RD,
    &syncobj_timeline_wait_wait_all_count, 0, "syncobj timeline wait WAIT_ALL count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_success_count, CTLFLAG_RD,
    &syncobj_timeline_wait_success_count, 0, "syncobj timeline wait success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_etime_count, CTLFLAG_RD,
    &syncobj_timeline_wait_etime_count, 0, "syncobj timeline wait ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_error_count, CTLFLAG_RD,
    &syncobj_timeline_wait_error_count, 0, "syncobj timeline wait non-ETIME error count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_success_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_success_count, 0, "syncobj timeline wait timeout=0 success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_etime_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_etime_count, 0, "syncobj timeline wait timeout=0 ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_available_success_count, CTLFLAG_RD,
    &syncobj_timeline_wait_available_success_count, 0, "syncobj timeline wait WAIT_AVAILABLE success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_available_etime_count, CTLFLAG_RD,
    &syncobj_timeline_wait_available_etime_count, 0, "syncobj timeline wait WAIT_AVAILABLE ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_available_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_available_count, 0, "syncobj timeline wait timeout=0 WAIT_AVAILABLE count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_available_success_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_available_success_count, 0, "syncobj timeline wait timeout=0 WAIT_AVAILABLE success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_available_etime_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_available_etime_count, 0, "syncobj timeline wait timeout=0 WAIT_AVAILABLE ETIME count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_complete_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_complete_count, 0, "syncobj timeline wait timeout=0 completion count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_complete_success_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_complete_success_count, 0, "syncobj timeline wait timeout=0 completion success count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, syncobj_timeline_wait_timeout0_complete_etime_count, CTLFLAG_RD,
    &syncobj_timeline_wait_timeout0_complete_etime_count, 0, "syncobj timeline wait timeout=0 completion ETIME count");
struct drm_syncobj_stub_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static const char *drm_syncobj_stub_fence_get_name(struct dma_fence *fence)
{
        return "syncobjstub";
}

static const struct dma_fence_ops drm_syncobj_stub_fence_ops = {
	.get_driver_name = drm_syncobj_stub_fence_get_name,
	.get_timeline_name = drm_syncobj_stub_fence_get_name,
};

static struct dma_fence *
drm_syncobj_stub_fence_create(void)
{
	struct drm_syncobj_stub_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	lockinit(&fence->lock, "dsofl", 0, 0);
	dma_fence_init(&fence->base, &drm_syncobj_stub_fence_ops,
		       &fence->lock, 0, 0);
	dma_fence_signal(&fence->base);

	return (&fence->base);
}


/**
 * drm_syncobj_find - lookup and reference a sync object.
 * @file_private: drm file private pointer
 * @handle: sync object handle to lookup.
 *
 * Returns a reference to the syncobj pointed to by handle or NULL. The
 * reference must be released by calling drm_syncobj_put().
 */
struct drm_syncobj *drm_syncobj_find(struct drm_file *file_private,
				     u32 handle)
{
	struct drm_syncobj *syncobj;

	lockmgr(&file_private->syncobj_table_lock, LK_SHARED);

	/* Check if we currently have a reference on the object */
	syncobj = idr_find(&file_private->syncobj_idr, handle);
	if (syncobj)
		drm_syncobj_get(syncobj);

	lockmgr(&file_private->syncobj_table_lock, LK_RELEASE);

	return syncobj;
}
EXPORT_SYMBOL(drm_syncobj_find);

static void drm_syncobj_add_callback_locked(struct drm_syncobj *syncobj,
					    struct drm_syncobj_cb *cb,
					    drm_syncobj_func_t func)
{
	cb->func = func;
	list_add_tail(&cb->node, &syncobj->cb_list);
}

#if 0 /* unused */
static
void drm_syncobj_add_callback(struct drm_syncobj *syncobj,
			      struct drm_syncobj_cb *cb,
			      drm_syncobj_func_t func)
{
	lockmgr(&syncobj->lock, LK_EXCLUSIVE);
	drm_syncobj_add_callback_locked(syncobj, cb, func);
	lockmgr(&syncobj->lock, LK_RELEASE);
}
#endif

static
void drm_syncobj_remove_callback(struct drm_syncobj *syncobj,
				 struct drm_syncobj_cb *cb)
{
	lockmgr(&syncobj->lock, LK_EXCLUSIVE);
	list_del_init(&cb->node);
	lockmgr(&syncobj->lock, LK_RELEASE);
}

/**
 * drm_syncobj_replace_fence - replace fence in a sync object.
 * @syncobj: Sync object to replace fence in
 * @point: timeline point
 * @fence: fence to install in sync file.
 *
 * This replaces the fence on a sync object.
 */
void drm_syncobj_replace_fence(struct drm_syncobj *syncobj,
			       u64 point,
			       struct dma_fence *fence)
{
	struct dma_fence *old_fence;
	struct drm_syncobj_cb *cur, *tmp;

	if (fence)
		dma_fence_get(fence);

	lockmgr(&syncobj->lock, LK_EXCLUSIVE);

	old_fence = rcu_dereference_protected(syncobj->fence,
					      lockdep_is_held(&syncobj->lock));
	rcu_assign_pointer(syncobj->fence, fence);

	if (fence != old_fence) {
		list_for_each_entry_safe(cur, tmp, &syncobj->cb_list, node) {
			list_del_init(&cur->node);
			cur->func(syncobj, cur);
		}
	}

	lockmgr(&syncobj->lock, LK_RELEASE);

	dma_fence_put(old_fence);
}
EXPORT_SYMBOL(drm_syncobj_replace_fence);

/**
 * drm_syncobj_add_point - install a fence as a timeline point
 * @syncobj: sync object holding the timeline
 * @chain: caller-allocated chain node (ownership passes here)
 * @fence: payload fence for @point (caller's reference is consumed)
 * @point: timeline point value
 *
 * The previous head becomes the new node's prefix, so each point keeps
 * its own fence and waiters for older points stay valid.  After the
 * install, fully signalled history below the first signalled node is
 * dropped to keep chains short.
 */
void drm_syncobj_add_point(struct drm_syncobj *syncobj,
			   struct dma_fence_chain *chain,
			   struct dma_fence *fence,
			   uint64_t point)
{
	struct dma_fence *prev;
	struct dma_fence *it;
	struct drm_syncobj_cb *cur, *tmp;

	lockmgr(&syncobj->lock, LK_EXCLUSIVE);

	prev = rcu_dereference_protected(syncobj->fence,
					 lockdep_is_held(&syncobj->lock));
	/* The slot's reference on the old head moves into chain->prev;
	 * the chain's initial kref becomes the slot's reference. */
	dma_fence_chain_init(chain, prev, fence, point);
	rcu_assign_pointer(syncobj->fence, &chain->base);

	list_for_each_entry_safe(cur, tmp, &syncobj->cb_list, node) {
		list_del_init(&cur->node);
		cur->func(syncobj, cur);
	}

	lockmgr(&syncobj->lock, LK_RELEASE);

	/* Walk once to match Linux's timeline-chain garbage collection. */
	it = dma_fence_chain_walk(dma_fence_get(&chain->base));
	while (it != NULL)
		it = dma_fence_chain_walk(it);
}
EXPORT_SYMBOL(drm_syncobj_add_point);

static int drm_syncobj_assign_null_handle(struct drm_syncobj *syncobj)
{
	struct dma_fence *fence;

	fence = drm_syncobj_stub_fence_create();
	if (fence == NULL)
		return -ENOMEM;

	drm_syncobj_replace_fence(syncobj, 0, fence);

	dma_fence_put(fence);

	return 0;
}

/**
 * drm_syncobj_find_fence - lookup and reference the fence in a sync object
 * @file_private: drm file private pointer
 * @handle: sync object handle to lookup.
 * @point: timeline point
 * @fence: out parameter for the fence
 *
 * This is just a convenience function that combines drm_syncobj_find() and
 * drm_syncobj_fence_get().
 *
 * Returns 0 on success or a negative error value on failure. On success @fence
 * contains a reference to the fence, which must be released by calling
 * dma_fence_put().
 */
int drm_syncobj_find_fence(struct drm_file *file_private,
			   u32 handle, u64 point,
			   struct dma_fence **fence)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	int ret = 0;

	if (!syncobj)
		return -ENOENT;

	*fence = drm_syncobj_fence_get(syncobj);
	if (!*fence) {
		ret = -EINVAL;
	} else if (point != 0) {
		ret = dma_fence_chain_find_seqno(fence, point);
		if (ret != 0) {
			dma_fence_put(*fence);
			*fence = NULL;
		} else if (*fence == NULL) {
			*fence = drm_syncobj_stub_fence_create();
			if (*fence == NULL)
				ret = -ENOMEM;
		}
	}
	drm_syncobj_put(syncobj);
	return ret;
}
EXPORT_SYMBOL(drm_syncobj_find_fence);

/**
 * drm_syncobj_free - free a sync object.
 * @kref: kref to free.
 *
 * Only to be called from kref_put in drm_syncobj_put.
 */
void drm_syncobj_free(struct kref *kref)
{
	struct drm_syncobj *syncobj = container_of(kref,
						   struct drm_syncobj,
						   refcount);
	drm_syncobj_replace_fence(syncobj, 0, NULL);
	kfree(syncobj);
}
EXPORT_SYMBOL(drm_syncobj_free);

/**
 * drm_syncobj_create - create a new syncobj
 * @out_syncobj: returned syncobj
 * @flags: DRM_SYNCOBJ_* flags
 * @fence: if non-NULL, the syncobj will represent this fence
 *
 * This is the first function to create a sync object. After creating, drivers
 * probably want to make it available to userspace, either through
 * drm_syncobj_get_handle() or drm_syncobj_get_fd().
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_create(struct drm_syncobj **out_syncobj, uint32_t flags,
		       struct dma_fence *fence)
{
	int ret;
	struct drm_syncobj *syncobj;

	syncobj = kzalloc(sizeof(struct drm_syncobj), GFP_KERNEL);
	if (!syncobj)
		return -ENOMEM;

	kref_init(&syncobj->refcount);
	INIT_LIST_HEAD(&syncobj->cb_list);
	lockinit(&syncobj->lock, "dsol", 0, 0);

	if (flags & DRM_SYNCOBJ_CREATE_SIGNALED) {
		ret = drm_syncobj_assign_null_handle(syncobj);
		if (ret < 0) {
			drm_syncobj_put(syncobj);
			return ret;
		}
	}

	if (fence) {
	  DRM_DEBUG("fence=%p\n", fence);
		drm_syncobj_replace_fence(syncobj, 0, fence);
	}

	DRM_DEBUG("syncobj=%p\n", syncobj);
	*out_syncobj = syncobj;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_create);

/**
 * drm_syncobj_get_handle - get a handle from a syncobj
 * @file_private: drm file private pointer
 * @syncobj: Sync object to export
 * @handle: out parameter with the new handle
 *
 * Exports a sync object created with drm_syncobj_create() as a handle on
 * @file_private to userspace.
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_get_handle(struct drm_file *file_private,
			   struct drm_syncobj *syncobj, u32 *handle)
{
	int ret;

	/* take a reference to put in the idr */
	drm_syncobj_get(syncobj);

	idr_preload(GFP_KERNEL);
	lockmgr(&file_private->syncobj_table_lock, LK_EXCLUSIVE);
	ret = idr_alloc(&file_private->syncobj_idr, syncobj, 1, 0, GFP_NOWAIT);
	lockmgr(&file_private->syncobj_table_lock, LK_RELEASE);

	idr_preload_end();

	if (ret < 0) {
		drm_syncobj_put(syncobj);
		return ret;
	}
	DRM_DEBUG("handle=%d\n", ret);
	*handle = ret;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_get_handle);

static int drm_syncobj_create_as_handle(struct drm_file *file_private,
					u32 *handle, uint32_t flags)
{
	int ret;
	struct drm_syncobj *syncobj;

	ret = drm_syncobj_create(&syncobj, flags, NULL);
	if (ret)
		return ret;

	ret = drm_syncobj_get_handle(file_private, syncobj, handle);
	drm_syncobj_put(syncobj);
	return ret;
}

static int drm_syncobj_destroy(struct drm_file *file_private,
			       u32 handle)
{
	struct drm_syncobj *syncobj;

	lockmgr(&file_private->syncobj_table_lock, LK_EXCLUSIVE);
	syncobj = idr_remove(&file_private->syncobj_idr, handle);
	lockmgr(&file_private->syncobj_table_lock, LK_RELEASE);

	if (!syncobj)
		return -EINVAL;

	drm_syncobj_put(syncobj);
	return 0;
}

#if 0
static int drm_syncobj_file_release(struct inode *inode, struct file *file)
{
	struct drm_syncobj *syncobj = file->private_data;

	drm_syncobj_put(syncobj);
	return 0;
}

static const struct file_operations drm_syncobj_file_fops = {
	.release = drm_syncobj_file_release,
};
#endif

/**
 * drm_syncobj_get_fd - get a file descriptor from a syncobj
 * @syncobj: Sync object to export
 * @p_fd: out parameter with the new file descriptor
 *
 * Exports a sync object created with drm_syncobj_create() as a file descriptor.
 *
 * Returns 0 on success or a negative error value on failure.
 */
int drm_syncobj_get_fd(struct drm_syncobj *syncobj, int *p_fd)
{
	struct file *file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

#if 0
	file = anon_inode_getfile("syncobj_file",
				  &drm_syncobj_file_fops,
				  syncobj, 0);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
 	}
#else
	return -ENOSYS;
#endif

	drm_syncobj_get(syncobj);
	fd_install(fd, file);

	*p_fd = fd;
	return 0;
}
EXPORT_SYMBOL(drm_syncobj_get_fd);

static int drm_syncobj_handle_to_fd(struct drm_file *file_private,
				    u32 handle, int *p_fd)
{
	struct drm_syncobj *syncobj = drm_syncobj_find(file_private, handle);
	int ret;

	if (!syncobj)
		return -EINVAL;

	ret = drm_syncobj_get_fd(syncobj, p_fd);
	drm_syncobj_put(syncobj);
	return ret;
}

static int drm_syncobj_fd_to_handle(struct drm_file *file_private,
				    int fd, u32 *handle)
{
	STUB();
	return -ENOSYS;
#if 0
	struct drm_syncobj *syncobj;
	struct file *file;
	int ret;

	file = fget(fd);
	if (!file)
		return -EINVAL;

	if (file->f_op != &drm_syncobj_file_fops) {
		fput(file);
		return -EINVAL;
	}

	/* take a reference to put in the idr */
	syncobj = file->private_data;
	drm_syncobj_get(syncobj);

	idr_preload(GFP_KERNEL);
	spin_lock(&file_private->syncobj_table_lock);
	ret = idr_alloc(&file_private->syncobj_idr, syncobj, 1, 0, GFP_NOWAIT);
	spin_unlock(&file_private->syncobj_table_lock);
	idr_preload_end();

	if (ret > 0) {
		*handle = ret;
		ret = 0;
	} else
		drm_syncobj_put(syncobj);

	fput(file);
	return ret;
#endif
}

static int drm_syncobj_import_sync_file_fence(struct drm_file *file_private,
					      int fd, int handle)
{
	struct dma_fence *fence;
	struct drm_syncobj *syncobj;
	int ret = 0;

	fence = sync_file_get_fence(fd);
	if (!fence)
		return -EINVAL;

	syncobj = drm_syncobj_find(file_private, handle);
	if (!syncobj) {
		dma_fence_put(fence);
		return -ENOENT;
	}

	drm_syncobj_replace_fence(syncobj, 0, fence);
	dma_fence_put(fence);
	drm_syncobj_put(syncobj);
	return ret;
}

static int drm_syncobj_export_sync_file(struct drm_file *file_private,
					int handle, int *p_fd)
{
	int ret;
	struct dma_fence *fence;
	struct sync_file *sync_file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	ret = drm_syncobj_find_fence(file_private, handle, 0, &fence);
	if (ret)
		goto err_put_fd;

	sync_file = sync_file_create(fence);
	dma_fence_put(fence);
	if (!sync_file) {
		ret = -EINVAL;
		goto err_put_fd;
	}

	fd_install(fd, sync_file->file);
	*p_fd = fd;
	return 0;

err_put_fd:
	put_unused_fd(fd);
	return ret;
}

/**
 * drm_syncobj_open - initalizes syncobj file-private structures at devnode open time
 * @file_private: drm file-private structure to set up
 *
 * Called at device open time, sets up the structure for handling refcounting
 * of sync objects.
 */
void
drm_syncobj_open(struct drm_file *file_private)
{
	idr_init(&file_private->syncobj_idr);
	lockinit(&file_private->syncobj_table_lock, "dsotl", 0, 0);
}

static int
drm_syncobj_release_handle(int id, void *ptr, void *data)
{
	struct drm_syncobj *syncobj = ptr;

	drm_syncobj_put(syncobj);
	return 0;
}

/**
 * drm_syncobj_release - release file-private sync object resources
 * @file_private: drm file-private structure to clean up
 *
 * Called at close time when the filp is going away.
 *
 * Releases any remaining references on objects by this filp.
 */
void
drm_syncobj_release(struct drm_file *file_private)
{
	idr_for_each(&file_private->syncobj_idr,
		     &drm_syncobj_release_handle, file_private);
	idr_destroy(&file_private->syncobj_idr);
}

int
drm_syncobj_create_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_private)
{
	struct drm_syncobj_create *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	/* no valid flags yet */
	if (args->flags & ~DRM_SYNCOBJ_CREATE_SIGNALED)
		return -EINVAL;

	return drm_syncobj_create_as_handle(file_private,
					    &args->handle, args->flags);
}

int
drm_syncobj_destroy_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_private)
{
	struct drm_syncobj_destroy *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	/* make sure padding is empty */
	if (args->pad)
		return -EINVAL;
	return drm_syncobj_destroy(file_private, args->handle);
}

int
drm_syncobj_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private)
{
	struct drm_syncobj_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->flags != 0 &&
	    args->flags != DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return -EINVAL;

	if (args->flags & DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return drm_syncobj_export_sync_file(file_private, args->handle,
						    &args->fd);

	return drm_syncobj_handle_to_fd(file_private, args->handle,
					&args->fd);
}

int
drm_syncobj_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private)
{
	struct drm_syncobj_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->flags != 0 &&
	    args->flags != DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return -EINVAL;

	if (args->flags & DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return drm_syncobj_import_sync_file_fence(file_private,
							  args->fd,
							  args->handle);

	return drm_syncobj_fd_to_handle(file_private, args->fd,
					&args->handle);
}

/* Timeline-only wait flag: wait for the point to materialize, not to
 * signal.  Not yet in this tree's uapi headers. */
#ifndef DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE (1 << 2)
#endif

struct syncobj_wait_entry {
	struct task_struct *task;
	struct dma_fence *fence;
	struct dma_fence_cb fence_cb;
	struct drm_syncobj_cb syncobj_cb;
	uint64_t point;
};

/*
 * Fetch a syncobj's current fence resolved to @point.  A collected,
 * already-signalled prefix is represented with a temporary signalled stub.
 * NULL means an empty syncobj, a not-yet-materialized point, or allocation
 * failure while creating the stub.
 */
static struct dma_fence *
drm_syncobj_point_get(struct drm_syncobj *syncobj, uint64_t point)
{
	struct dma_fence *fence = drm_syncobj_fence_get(syncobj);
	int ret;

	if (fence != NULL && point != 0) {
		ret = dma_fence_chain_find_seqno(&fence, point);
		if (ret != 0) {
			dma_fence_put(fence);
			fence = NULL;
		} else if (fence == NULL) {
			fence = drm_syncobj_stub_fence_create();
		}
	}
	return (fence);
}

static void syncobj_wait_syncobj_func(struct drm_syncobj *syncobj,
				      struct drm_syncobj_cb *cb);

static void
drm_syncobj_wait_add_callback(struct drm_syncobj *syncobj,
			      struct syncobj_wait_entry *wait)
{
	struct dma_fence *fence;

	if (wait->fence != NULL)
		return;

	lockmgr(&syncobj->lock, LK_EXCLUSIVE);
	fence = dma_fence_get(rcu_dereference_protected(syncobj->fence,
							lockdep_is_held(&syncobj->lock)));
	if (fence != NULL && wait->point != 0) {
		int ret = dma_fence_chain_find_seqno(&fence, wait->point);

		if (ret != 0) {
			dma_fence_put(fence);
			fence = NULL;
		} else if (fence == NULL) {
			fence = drm_syncobj_stub_fence_create();
		}
	}
	if (fence != NULL) {
		wait->fence = fence;
	} else {
		drm_syncobj_add_callback_locked(syncobj, &wait->syncobj_cb,
						syncobj_wait_syncobj_func);
	}
	lockmgr(&syncobj->lock, LK_RELEASE);
}

static void syncobj_wait_fence_func(struct dma_fence *fence,
				    struct dma_fence_cb *cb)
{
	struct syncobj_wait_entry *wait =
		container_of(cb, struct syncobj_wait_entry, fence_cb);
	DRM_DEBUG("wake_up\n");
	wake_up_process(wait->task);
}

static void syncobj_wait_syncobj_func(struct drm_syncobj *syncobj,
				      struct drm_syncobj_cb *cb)
{
	struct syncobj_wait_entry *wait =
		container_of(cb, struct syncobj_wait_entry, syncobj_cb);
	struct dma_fence *fence;

	if (wait->fence != NULL)
		return;

	/* This happens inside the syncobj lock */
	fence = dma_fence_get(rcu_dereference_protected(syncobj->fence,
							lockdep_is_held(&syncobj->lock)));
	if (fence != NULL && wait->point != 0) {
		int ret = dma_fence_chain_find_seqno(&fence, wait->point);

		if (ret != 0) {
			dma_fence_put(fence);
			fence = NULL;
		} else if (fence == NULL) {
			fence = drm_syncobj_stub_fence_create();
		}
	}
	if (fence == NULL)
		return;
	wait->fence = fence;
	DRM_DEBUG("wake_up\n");
	wake_up_process(wait->task);
}

static signed long drm_syncobj_array_wait_timeout(struct drm_syncobj **syncobjs,
						  const uint64_t *points,
						  uint32_t count,
						  uint32_t flags,
						  signed long timeout,
						  uint32_t *idx)
{
	struct syncobj_wait_entry *entries;
	struct syncobj_wait_entry stack_entry;
	struct dma_fence *fence;
	uint32_t signaled_count, i;

	if (count == 1) {
		memset(&stack_entry, 0, sizeof(stack_entry));
		entries = &stack_entry;
	} else {
		entries = kcalloc(count, sizeof(*entries), GFP_KERNEL);
		if (!entries)
			return -ENOMEM;
	}

	DRM_DEBUG("[in] timeout=%ld, flags=%u, entries=%p\n", timeout, flags, entries);
#if 0
	if (timeout == 0)
	  timeout = 2 * hz;
#endif

	/* Walk the list of sync objects and initialize entries.  We do
	 * this up-front so that we can properly return -EINVAL if there is
	 * a syncobj with a missing fence and then never have the chance of
	 * returning -EINVAL again.
	 */
	signaled_count = 0;
	for (i = 0; i < count; ++i) {
		entries[i].task = current;
		entries[i].point = points != NULL ? points[i] : 0;
		entries[i].fence = drm_syncobj_point_get(syncobjs[i],
							 entries[i].point);
		if (!entries[i].fence) {
			if (flags & (DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)) {
			  DRM_DEBUG("continue\n");
				continue;
			} else {
			  DRM_DEBUG("-EINVAL\n");
				timeout = -EINVAL;
				goto cleanup_entries;
			}
		}

		if ((flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) ||
		    dma_fence_is_signaled(entries[i].fence)) {
			if (signaled_count == 0 && idx)
				*idx = i;
			signaled_count++;
		}
	}

	if (signaled_count == count ||
	    (signaled_count > 0 &&
	     !(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)))
		goto cleanup_entries;

	/* There's a very annoying laxness in the dma_fence API here, in
	 * that backends are not required to automatically report when a
	 * fence is signaled prior to fence->ops->enable_signaling() being
	 * called.  So here if we fail to match signaled_count, we need to
	 * fallthough and try a 0 timeout wait!
	 */

	if (flags & (DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
	    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)) {
		for (i = 0; i < count; ++i)
			drm_syncobj_wait_add_callback(syncobjs[i], &entries[i]);
	}

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		signaled_count = 0;
		for (i = 0; i < count; ++i) {
			fence = entries[i].fence;
			if (!fence)
				continue;

			if ((flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) ||
			    dma_fence_is_signaled(fence) ||
			    (!entries[i].fence_cb.func &&
			     dma_fence_add_callback(fence,
						    &entries[i].fence_cb,
						    syncobj_wait_fence_func))) {
				/* The fence has been signaled */
				if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) {
					signaled_count++;
				} else {
					if (idx)
						*idx = i;
					goto done_waiting;
				}
			}
		}

		if (signaled_count == count)
			goto done_waiting;

		if (timeout == 0) {
			timeout = -ETIME;
			goto done_waiting;
		}

		if (signal_pending(current)) {
			timeout = -ERESTARTSYS;
			goto done_waiting;
		}

		DRM_DEBUG("[before] timeout=%ld\n", timeout);
		timeout = schedule_timeout(timeout);
		DRM_DEBUG("[after] timeout=%ld\n", timeout);
	} while (1);

done_waiting:
	__set_current_state(TASK_RUNNING);

cleanup_entries:
	for (i = 0; i < count; ++i) {
		if (entries[i].syncobj_cb.func)
			drm_syncobj_remove_callback(syncobjs[i],
						    &entries[i].syncobj_cb);
		if (entries[i].fence_cb.func)
			dma_fence_remove_callback(entries[i].fence,
						  &entries[i].fence_cb);
		dma_fence_put(entries[i].fence);
	}
	if (entries != &stack_entry)
		kfree(entries);

	return timeout;
}

/**
 * drm_timeout_abs_to_jiffies - calculate jiffies timeout from absolute value
 *
 * @timeout_nsec: timeout nsec component in ns, 0 for poll
 *
 * Calculate the timeout in jiffies from an absolute time in sec/nsec.
 */
static signed long drm_timeout_abs_to_jiffies(int64_t timeout_nsec)
{
	ktime_t abs_timeout, now;
	u64 timeout_ns, timeout_jiffies64;

	/* make 0 timeout means poll - absolute 0 doesn't seem valid */
	if (timeout_nsec == 0)
		return 0;

	abs_timeout = ns_to_ktime(timeout_nsec);
	now = ktime_get();

	if (!ktime_after(abs_timeout, now))
		return 0;

	timeout_ns = ktime_to_ns(ktime_sub(abs_timeout, now));

	timeout_jiffies64 = nsecs_to_jiffies64(timeout_ns);
	/*  clamp timeout to avoid infinite timeout */
	if (timeout_jiffies64 >= MAX_SCHEDULE_TIMEOUT - 1)
		return MAX_SCHEDULE_TIMEOUT - 1;

	return timeout_jiffies64 + 1;
}

static int drm_syncobj_array_wait(struct drm_device *dev,
				  struct drm_file *file_private,
				  struct drm_syncobj_wait *wait,
				  struct drm_syncobj **syncobjs)
{
	signed long timeout = drm_timeout_abs_to_jiffies(wait->timeout_nsec);
	uint32_t first = ~0;

	timeout = drm_syncobj_array_wait_timeout(syncobjs, NULL,
						 wait->count_handles,
						 wait->flags,
						 timeout, &first);
	if (timeout < 0)
		return timeout;

	wait->first_signaled = first;
	return 0;
}

static int drm_syncobj_array_find(struct drm_file *file_private,
				  void __user *user_handles,
				  uint32_t count_handles,
				  struct drm_syncobj ***syncobjs_out)
{
	uint32_t i, *handles;
	struct drm_syncobj **syncobjs;
	int ret;

	handles = kmalloc_array(count_handles, sizeof(*handles), GFP_KERNEL);
	if (handles == NULL)
		return -ENOMEM;

	if (copy_from_user(handles, user_handles,
			   sizeof(uint32_t) * count_handles)) {
		ret = -EFAULT;
		goto err_free_handles;
	}

	syncobjs = kmalloc_array(count_handles, sizeof(*syncobjs), GFP_KERNEL);
	if (syncobjs == NULL) {
		ret = -ENOMEM;
		goto err_free_handles;
	}

	for (i = 0; i < count_handles; i++) {
		syncobjs[i] = drm_syncobj_find(file_private, handles[i]);
		if (!syncobjs[i]) {
			ret = -ENOENT;
			goto err_put_syncobjs;
		}
	}

	kfree(handles);
	*syncobjs_out = syncobjs;
	return 0;

err_put_syncobjs:
	while (i-- > 0)
		drm_syncobj_put(syncobjs[i]);
	kfree(syncobjs);
err_free_handles:
	kfree(handles);

	return ret;
}

static void drm_syncobj_array_free(struct drm_syncobj **syncobjs,
				   uint32_t count)
{
	uint32_t i;
	for (i = 0; i < count; i++)
		drm_syncobj_put(syncobjs[i]);
	kfree(syncobjs);
}

#define DRM_SYNCOBJ_WAIT_FOR_SUBMIT_TIMEOUT (5 * hz)

static int
drm_syncobj_find_fence_for_transfer(struct drm_file *file_private,
				    u32 handle, u64 point, u32 flags,
				    struct dma_fence **fence)
{
	struct drm_syncobj *syncobj;
	uint64_t wait_point;
	signed long timeout;
	int ret;

	if (flags & ~DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT)
		return -EINVAL;

	ret = drm_syncobj_find_fence(file_private, handle, point, fence);
	if (ret == 0 || !(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
		return ret;

	syncobj = drm_syncobj_find(file_private, handle);
	if (syncobj == NULL)
		return -ENOENT;

	wait_point = point;
	/*
	 * A transfer needs a referenced source fence; it does not wait for that
	 * fence to signal.  When WAIT_FOR_SUBMIT is set and the source point has
	 * not materialized yet, wait only until the point becomes available, then
	 * resolve it below.  Plain SYNCOBJ_WAIT keeps its stronger "wait until
	 * signaled after submit" behavior in drm_syncobj_array_wait_timeout().
	 */
	timeout = drm_syncobj_array_wait_timeout(&syncobj, &wait_point, 1,
	    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
	    DRM_SYNCOBJ_WAIT_FOR_SUBMIT_TIMEOUT, NULL);
	drm_syncobj_put(syncobj);
	if (timeout < 0)
		return (int)timeout;

	return drm_syncobj_find_fence(file_private, handle, point, fence);
}

int
drm_syncobj_transfer_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_private)
{
	struct drm_syncobj_transfer *args = data;
	struct drm_syncobj *dst_syncobj;
	struct dma_fence *fence = NULL;
	struct dma_fence_chain *chain = NULL;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->pad)
		return -EINVAL;

	if (args->dst_point != 0) {
		chain = dma_fence_chain_alloc();
		if (chain == NULL)
			return -ENOMEM;
	}

	dst_syncobj = drm_syncobj_find(file_private, args->dst_handle);
	if (dst_syncobj == NULL) {
		dma_fence_chain_free(chain);
		return -ENOENT;
	}

	ret = drm_syncobj_find_fence_for_transfer(file_private,
	    args->src_handle, args->src_point, args->flags, &fence);
	if (ret < 0)
		goto out;

	if (args->dst_point != 0) {
		drm_syncobj_add_point(dst_syncobj, chain, fence,
		    args->dst_point);
		chain = NULL;
		fence = NULL;
	} else {
		drm_syncobj_replace_fence(dst_syncobj, 0, fence);
		dma_fence_put(fence);
		fence = NULL;
	}

out:
	dma_fence_put(fence);
	dma_fence_chain_free(chain);
	drm_syncobj_put(dst_syncobj);
	return ret;
}

/*
 * Ownership:
 *   On success, transfers one syncobj reference to *syncobj_out. On failure,
 *   no reference is transferred.
 *
 * Lifetime:
 *   The copied handle is a by-value snapshot from userspace. The returned
 *   syncobj reference remains valid until the caller releases it with
 *   drm_syncobj_put().
 *
 * Threading:
 *   Lookup uses drm_syncobj_find(), which pins the object while holding the
 *   per-file syncobj table lock. The caller may then wait without holding the
 *   table lock.
 */
static int
drm_syncobj_find_single_user(struct drm_file *file_private,
    void __user *user_handles, struct drm_syncobj **syncobj_out)
{
	uint32_t handle;

	if (copy_from_user(&handle, user_handles, sizeof(handle)))
		return (-EFAULT);
	*syncobj_out = drm_syncobj_find(file_private, handle);
	return (*syncobj_out != NULL ? 0 : -ENOENT);
}

int
drm_syncobj_wait_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_private)
{
	struct drm_syncobj_wait *args = data;
	struct drm_syncobj **syncobjs;
	struct drm_syncobj *single_syncobj = NULL;
	struct drm_syncobj *single_syncobjs[1];
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE))
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	syncobj_wait_count++;
	syncobj_wait_handle_count += args->count_handles;
	if (args->timeout_nsec == 0)
		syncobj_wait_timeout0_count++;
	if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT)
		syncobj_wait_wait_for_submit_count++;
	if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)
		syncobj_wait_wait_all_count++;

	if (args->count_handles == 1) {
		ret = drm_syncobj_find_single_user(file_private,
		    u64_to_user_ptr(args->handles), &single_syncobj);
		if (ret < 0)
			goto out;
		single_syncobjs[0] = single_syncobj;
		ret = drm_syncobj_array_wait(dev, file_private, args,
		    single_syncobjs);
		drm_syncobj_put(single_syncobj);
		goto out_record;
	}

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		goto out;

	ret = drm_syncobj_array_wait(dev, file_private,
				     args, syncobjs);
	drm_syncobj_array_free(syncobjs, args->count_handles);

out_record:
	if (ret == 0) {
		syncobj_wait_success_count++;
		if (args->timeout_nsec == 0)
			syncobj_wait_timeout0_success_count++;
	} else if (ret == -ETIME) {
		syncobj_wait_etime_count++;
		if (args->timeout_nsec == 0)
			syncobj_wait_timeout0_etime_count++;
	} else {
		syncobj_wait_error_count++;
	}

out:
	return ret;
}

int
drm_syncobj_timeline_wait_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_private)
{
	struct drm_syncobj_timeline_wait *args = data;
	struct drm_syncobj **syncobjs;
	struct drm_syncobj *single_syncobj = NULL;
	struct drm_syncobj *single_syncobjs[1];
	uint64_t *points;
	uint64_t single_point;
	uint32_t i, signaled = 0;
	uint32_t first = ~0u;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE |
			    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE))
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	syncobj_timeline_wait_count++;
	syncobj_timeline_wait_handle_count += args->count_handles;
	if (args->timeout_nsec == 0) {
		syncobj_timeline_wait_timeout0_count++;
		if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
			syncobj_timeline_wait_timeout0_available_count++;
		else
			syncobj_timeline_wait_timeout0_complete_count++;
	}
	if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
		syncobj_timeline_wait_wait_available_count++;
	if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT)
		syncobj_timeline_wait_wait_for_submit_count++;
	if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)
		syncobj_timeline_wait_wait_all_count++;

	if (args->count_handles == 1) {
		ret = drm_syncobj_find_single_user(file_private,
		    u64_to_user_ptr(args->handles), &single_syncobj);
		if (ret < 0)
			goto out;
		if (copy_from_user(&single_point, u64_to_user_ptr(args->points),
		    sizeof(single_point))) {
			ret = -EFAULT;
			goto out_single;
		}
		single_syncobjs[0] = single_syncobj;
		{
			signed long timeout =
			    drm_timeout_abs_to_jiffies(args->timeout_nsec);

			timeout = drm_syncobj_array_wait_timeout(single_syncobjs,
			    &single_point, 1, args->flags, timeout, &first);
			if (timeout < 0) {
				if (timeout == -ETIME) {
					syncobj_timeline_wait_etime_count++;
					if (args->timeout_nsec == 0) {
						syncobj_timeline_wait_timeout0_etime_count++;
						if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
							syncobj_timeline_wait_timeout0_available_etime_count++;
						else
							syncobj_timeline_wait_timeout0_complete_etime_count++;
					}
					if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
						syncobj_timeline_wait_available_etime_count++;
				} else {
					syncobj_timeline_wait_error_count++;
				}
				ret = timeout;
			} else {
				args->first_signaled = first;
				syncobj_timeline_wait_success_count++;
				if (args->timeout_nsec == 0) {
					syncobj_timeline_wait_timeout0_success_count++;
					if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
						syncobj_timeline_wait_timeout0_available_success_count++;
					else
						syncobj_timeline_wait_timeout0_complete_success_count++;
				}
				if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
					syncobj_timeline_wait_available_success_count++;
				ret = 0;
			}
		}
		goto out_single;
	}

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		goto out;

	points = kmalloc_array(args->count_handles, sizeof(*points), GFP_KERNEL);
	if (points == NULL) {
		ret = -ENOMEM;
		goto out_syncobjs;
	}

	if (copy_from_user(points, u64_to_user_ptr(args->points),
			   sizeof(*points) * args->count_handles)) {
		ret = -EFAULT;
		goto out_points;
	}

	{
		signed long timeout =
		    drm_timeout_abs_to_jiffies(args->timeout_nsec);

			timeout = drm_syncobj_array_wait_timeout(syncobjs, points,
								 args->count_handles,
								 args->flags,
								 timeout, &first);
			if (timeout < 0) {
				if (timeout == -ETIME) {
					syncobj_timeline_wait_etime_count++;
					if (args->timeout_nsec == 0) {
						syncobj_timeline_wait_timeout0_etime_count++;
						if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
							syncobj_timeline_wait_timeout0_available_etime_count++;
						else
							syncobj_timeline_wait_timeout0_complete_etime_count++;
					}
					if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
						syncobj_timeline_wait_available_etime_count++;
				} else {
					syncobj_timeline_wait_error_count++;
				}
				ret = timeout;
				goto out_points;
			}
			args->first_signaled = first;
			syncobj_timeline_wait_success_count++;
			if (args->timeout_nsec == 0) {
				syncobj_timeline_wait_timeout0_success_count++;
				if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
					syncobj_timeline_wait_timeout0_available_success_count++;
				else
					syncobj_timeline_wait_timeout0_complete_success_count++;
			}
			if (args->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE)
				syncobj_timeline_wait_available_success_count++;
			ret = 0;
		}

	(void)i;
	(void)signaled;
out_points:
	kfree(points);
out_syncobjs:
	drm_syncobj_array_free(syncobjs, args->count_handles);
out_single:
	if (single_syncobj != NULL)
		drm_syncobj_put(single_syncobj);
out:
	return ret;
}

int
drm_syncobj_reset_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_private)
{
	struct drm_syncobj_array *args = data;
	struct drm_syncobj **syncobjs;
	uint32_t i;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	for (i = 0; i < args->count_handles; i++)
		drm_syncobj_replace_fence(syncobjs[i], 0, NULL);

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return 0;
}

int
drm_syncobj_signal_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_private)
{
	struct drm_syncobj_array *args = data;
	struct drm_syncobj **syncobjs;
	uint32_t i;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		return -EOPNOTSUPP;

	if (args->pad != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	for (i = 0; i < args->count_handles; i++) {
		ret = drm_syncobj_assign_null_handle(syncobjs[i]);
		if (ret < 0)
			break;
	}

	drm_syncobj_array_free(syncobjs, args->count_handles);

	return ret;
}

int
drm_syncobj_query_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_private)
{
	struct drm_syncobj_timeline_array *args = data;
	struct drm_syncobj **syncobjs;
	uint64_t *points;
	uint32_t i;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags & ~DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	syncobj_query_count++;
	syncobj_query_handle_count += args->count_handles;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	points = kmalloc_array(args->count_handles, sizeof(*points), GFP_KERNEL);
	if (points == NULL) {
		ret = -ENOMEM;
		goto out_syncobjs;
	}

	for (i = 0; i < args->count_handles; i++) {
		struct dma_fence *fence = drm_syncobj_fence_get(syncobjs[i]);
		struct dma_fence_chain *chain = to_dma_fence_chain(fence);
		uint64_t head_point = 0;
		uint64_t gap;

		points[i] = 0;
		if (chain != NULL) {
			struct dma_fence *iter;
			struct dma_fence *last = dma_fence_get(fence);

			head_point = chain->point;
			if (syncobj_query_head_point_max < head_point)
				syncobj_query_head_point_max = head_point;

			if (args->flags &
			    DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED) {
				points[i] = head_point;
			} else {
				for (iter = dma_fence_get(fence); iter != NULL;
				    iter = dma_fence_chain_walk(iter)) {
					if (iter->context != fence->context) {
						dma_fence_put(iter);
						break;
					}
					dma_fence_put(last);
					last = dma_fence_get(iter);
				}

				if (dma_fence_is_signaled(last)) {
					points[i] = last->seqno;
				} else {
					struct dma_fence_chain *last_chain =
					    to_dma_fence_chain(last);

					if (last_chain != NULL)
						points[i] = last_chain->prev_seqno;
				}
			}
			dma_fence_put(last);
		} else if (fence != NULL && dma_fence_is_signaled(fence)) {
			points[i] = fence->seqno;
			head_point = fence->seqno;
			if (syncobj_query_head_point_max < head_point)
				syncobj_query_head_point_max = head_point;
		}
		if (points[i] == 0)
			syncobj_query_zero_count++;
		if (syncobj_query_return_point_max < points[i])
			syncobj_query_return_point_max = points[i];
		if (head_point > points[i]) {
			syncobj_query_lag_count++;
			gap = head_point - points[i];
			if (syncobj_query_lag_max < gap)
				syncobj_query_lag_max = gap;
		}
		dma_fence_put(fence);
	}

	if (copy_to_user(u64_to_user_ptr(args->points), points,
			 sizeof(*points) * args->count_handles))
		ret = -EFAULT;

	kfree(points);
out_syncobjs:
	drm_syncobj_array_free(syncobjs, args->count_handles);
	return ret;
}

int
drm_syncobj_timeline_signal_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_private)
{
	struct drm_syncobj_timeline_array *args = data;
	struct drm_syncobj **syncobjs;
	uint64_t *points;
	uint32_t i;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE))
		return -EOPNOTSUPP;

	if (args->flags != 0)
		return -EINVAL;

	if (args->count_handles == 0)
		return -EINVAL;

	ret = drm_syncobj_array_find(file_private,
				     u64_to_user_ptr(args->handles),
				     args->count_handles,
				     &syncobjs);
	if (ret < 0)
		return ret;

	points = kmalloc_array(args->count_handles, sizeof(*points), GFP_KERNEL);
	if (points == NULL) {
		ret = -ENOMEM;
		goto out_syncobjs;
	}

	if (copy_from_user(points, u64_to_user_ptr(args->points),
			   sizeof(*points) * args->count_handles)) {
		ret = -EFAULT;
		goto out_points;
	}

	for (i = 0; i < args->count_handles; i++) {
		if (points[i] == 0) {
			ret = drm_syncobj_assign_null_handle(syncobjs[i]);
		} else {
			struct drm_syncobj_stub_fence *stub;
			struct dma_fence_chain *chain;

			stub = kzalloc(sizeof(*stub), GFP_KERNEL);
			chain = dma_fence_chain_alloc();
			if (stub == NULL || chain == NULL) {
				kfree(stub);
				dma_fence_chain_free(chain);
				ret = -ENOMEM;
				break;
			}
			lockinit(&stub->lock, "dsofl", 0, 0);
			dma_fence_init(&stub->base,
			    &drm_syncobj_stub_fence_ops, &stub->lock, 0,
			    points[i]);
			dma_fence_signal(&stub->base);
			drm_syncobj_add_point(syncobjs[i], chain,
			    &stub->base, points[i]);
			ret = 0;
		}
		if (ret < 0)
			break;
	}

out_points:
	kfree(points);
out_syncobjs:
	drm_syncobj_array_free(syncobjs, args->count_handles);
	return ret;
}
