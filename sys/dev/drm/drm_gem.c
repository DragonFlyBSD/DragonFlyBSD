/*-
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/dev/drm2/drm_gem.c 247835 2013-03-05 09:49:34Z kib $"
 */

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <drm/drmP.h>

/*
 * We make up offsets for buffer objects so we can recognize them at
 * mmap time.
 */

/* pgoff in mmap is an unsigned long, so we need to make sure that
 * the faked up offset will fit
 */

#if ULONG_MAX == UINT64_MAX
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFUL >> PAGE_SHIFT) * 16)
#else
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFUL >> PAGE_SHIFT) * 16)
#endif

/**
 * Initialize the GEM device fields
 */

int
drm_gem_init(struct drm_device *dev)
{
	struct drm_gem_mm *mm;

	spin_init(&dev->object_name_lock);
	idr_init(&dev->object_name_idr);

	mm = kmalloc(sizeof(*mm), DRM_MEM_DRIVER, M_WAITOK);
	if (!mm) {
		DRM_ERROR("out of memory\n");
		return -ENOMEM;
	}

	dev->mm_private = mm;

	if (drm_ht_create(&mm->offset_hash, 12)) {
		drm_free(mm, DRM_MEM_DRIVER);
		return -ENOMEM;
	}

	mm->idxunr = new_unrhdr(0, DRM_GEM_MAX_IDX, NULL);
	return 0;
}

void
drm_gem_destroy(struct drm_device *dev)
{
	struct drm_gem_mm *mm = dev->mm_private;

	drm_ht_remove(&mm->offset_hash);
	delete_unrhdr(mm->idxunr);
	drm_free(mm, DRM_MEM_DRIVER);
	dev->mm_private = NULL;
}

/**
 * Initialize an already allocated GEM object of the specified size with
 * shmfs backing store.
 */
int drm_gem_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size)
{

	KASSERT((size & (PAGE_SIZE - 1)) == 0,
	    ("Bad size %ju", (uintmax_t)size));

	obj->dev = dev;
	obj->vm_obj = default_pager_alloc(NULL, size,
	    VM_PROT_READ | VM_PROT_WRITE, 0);

	kref_init(&obj->refcount);
	atomic_set(&obj->handle_count, 0);
	obj->size = size;

	return (0);
}

/**
 * Initialize an already allocated GEM object of the specified size with
 * no GEM provided backing store. Instead the caller is responsible for
 * backing the object and handling it.
 */
int drm_gem_private_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size)
{

	KASSERT((size & (PAGE_SIZE - 1)) == 0,
	    ("Bad size %ju", (uintmax_t)size));

	obj->dev = dev;
	obj->vm_obj = NULL;

	kref_init(&obj->refcount);
	atomic_set(&obj->handle_count, 0);
	obj->size = size;

	return (0);
}


struct drm_gem_object *
drm_gem_object_alloc(struct drm_device *dev, size_t size)
{
	struct drm_gem_object *obj;

	obj = kmalloc(sizeof(*obj), DRM_MEM_DRIVER, M_WAITOK | M_ZERO);
	if (drm_gem_object_init(dev, obj, size) != 0)
		goto free;

	if (dev->driver->gem_init_object != NULL &&
	    dev->driver->gem_init_object(obj) != 0)
		goto dealloc;
	return (obj);
dealloc:
	vm_object_deallocate(obj->vm_obj);
free:
	drm_free(obj, DRM_MEM_DRIVER);
	return (NULL);
}

/**
 * Called after the last reference to the object has been lost.
 * Must be called holding struct_ mutex
 *
 * Frees the object
 */
void
drm_gem_object_free(struct kref *kref)
{
	struct drm_gem_object *obj = (struct drm_gem_object *) kref;
	struct drm_device *dev = obj->dev;

	DRM_LOCK_ASSERT(dev);
	if (dev->driver->gem_free_object != NULL)
		dev->driver->gem_free_object(obj);
}

static void drm_gem_object_ref_bug(struct kref *list_kref)
{
	panic("BUG");
}

/**
 * Called after the last handle to the object has been closed
 *
 * Removes any name for the object. Note that this must be
 * called before drm_gem_object_free or we'll be touching
 * freed memory
 */
void drm_gem_object_handle_free(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	/* Remove any name for this object */
	spin_lock(&dev->object_name_lock);
	if (obj->name) {
		idr_remove(&dev->object_name_idr, obj->name);
		obj->name = 0;
		spin_unlock(&dev->object_name_lock);
		/*
		 * The object name held a reference to this object, drop
		 * that now.
		*
		* This cannot be the last reference, since the handle holds one too.
		 */
		kref_put(&obj->refcount, drm_gem_object_ref_bug);
	} else
		spin_unlock(&dev->object_name_lock);

}

/**
 * Removes the mapping from handle to filp for this object.
 */
int
drm_gem_handle_delete(struct drm_file *filp, u32 handle)
{
	struct drm_device *dev;
	struct drm_gem_object *obj;

	/* This is gross. The idr system doesn't let us try a delete and
	 * return an error code.  It just spews if you fail at deleting.
	 * So, we have to grab a lock around finding the object and then
	 * doing the delete on it and dropping the refcount, or the user
	 * could race us to double-decrement the refcount and cause a
	 * use-after-free later.  Given the frequency of our handle lookups,
	 * we may want to use ida for number allocation and a hash table
	 * for the pointers, anyway.
	 */
	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&filp->object_idr, handle);
	if (obj == NULL) {
		spin_unlock(&filp->table_lock);
		return -EINVAL;
	}
	dev = obj->dev;

	/* Release reference and decrement refcount. */
	idr_remove(&filp->object_idr, handle);
	spin_unlock(&filp->table_lock);

	if (dev->driver->gem_close_object)
		dev->driver->gem_close_object(obj, filp);
	drm_gem_object_handle_unreference_unlocked(obj);

	return 0;
}

/**
 * Create a handle for this object. This adds a handle reference
 * to the object, which includes a regular reference count. Callers
 * will likely want to dereference the object afterwards.
 */
int
drm_gem_handle_create(struct drm_file *file_priv,
		       struct drm_gem_object *obj,
		       u32 *handlep)
{
	struct drm_device *dev = obj->dev;
	int ret;

	/*
	 * Get the user-visible handle using idr.
	 */
again:
	/* ensure there is space available to allocate a handle */
	if (idr_pre_get(&file_priv->object_idr, GFP_KERNEL) == 0)
		return -ENOMEM;

	/* do the allocation under our spinlock */
	spin_lock(&file_priv->table_lock);
	ret = idr_get_new_above(&file_priv->object_idr, obj, 1, (int *)handlep);
	spin_unlock(&file_priv->table_lock);
	if (ret == -EAGAIN)
		goto again;
	else if (ret)
		return ret;

	drm_gem_object_handle_reference(obj);

	if (dev->driver->gem_open_object) {
		ret = dev->driver->gem_open_object(obj, file_priv);
		if (ret) {
			drm_gem_handle_delete(file_priv, *handlep);
			return ret;
		}
	}

	return 0;
}

/** Returns a reference to the object named by the handle. */
struct drm_gem_object *
drm_gem_object_lookup(struct drm_device *dev, struct drm_file *filp,
		      u32 handle)
{
	struct drm_gem_object *obj;

	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&filp->object_idr, handle);
	if (obj == NULL) {
		spin_unlock(&filp->table_lock);
		return NULL;
	}

	drm_gem_object_reference(obj);

	spin_unlock(&filp->table_lock);

	return obj;
}

int
drm_gem_close_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_gem_close *args;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return (ENODEV);
	args = data;

	return (drm_gem_handle_delete(file_priv, args->handle));
}

/**
 * Create a global name for an object, returning the name.
 *
 * Note that the name does not hold a reference; when the object
 * is freed, the name goes away.
 */
int
drm_gem_flink_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_gem_flink *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return -ENODEV;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return -ENOENT;

again:
	if (idr_pre_get(&dev->object_name_idr, GFP_KERNEL) == 0) {
		ret = -ENOMEM;
		goto err;
	}

	spin_lock(&dev->object_name_lock);
	if (!obj->name) {
		ret = idr_get_new_above(&dev->object_name_idr, obj, 1,
					&obj->name);
		args->name = (uint64_t) obj->name;
		spin_unlock(&dev->object_name_lock);

		if (ret == -EAGAIN)
			goto again;
		else if (ret)
			goto err;

		/* Allocate a reference for the name table.  */
		drm_gem_object_reference(obj);
	} else {
		args->name = (uint64_t) obj->name;
		spin_unlock(&dev->object_name_lock);
		ret = 0;
	}

err:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

/**
 * Open an object using the global name, returning a handle and the size.
 *
 * This handle (of course) holds a reference to the object, so the object
 * will not go away until the handle is deleted.
 */
int
drm_gem_open_ioctl(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	struct drm_gem_open *args = data;
	struct drm_gem_object *obj;
	int ret;
	u32 handle;

#if 0
	if (!drm_core_check_feature(dev, DRIVER_GEM))
#endif
	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	spin_lock(&dev->object_name_lock);
	obj = idr_find(&dev->object_name_idr, (int) args->name);
	if (obj)
		drm_gem_object_reference(obj);
	spin_unlock(&dev->object_name_lock);
	if (!obj)
		return -ENOENT;

	ret = drm_gem_handle_create(file_priv, obj, &handle);
	drm_gem_object_unreference_unlocked(obj);
	if (ret)
		return ret;

	args->handle = handle;
	args->size = obj->size;

	return 0;
}

/**
 * Called at device open time, sets up the structure for handling refcounting
 * of mm objects.
 */
void
drm_gem_open(struct drm_device *dev, struct drm_file *file_private)
{
	idr_init(&file_private->object_idr);
	spin_init(&file_private->table_lock);
}

/**
 * Called at device close to release the file's
 * handle references on objects.
 */
static int
drm_gem_object_release_handle(int id, void *ptr, void *data)
{
	struct drm_file *file_priv = data;
	struct drm_gem_object *obj = ptr;
	struct drm_device *dev = obj->dev;

	if (dev->driver->gem_close_object)
		dev->driver->gem_close_object(obj, file_priv);

	drm_gem_object_handle_unreference_unlocked(obj);

	return 0;
}

void
drm_gem_object_release(struct drm_gem_object *obj)
{

	/*
	 * obj->vm_obj can be NULL for private gem objects.
	 */
	vm_object_deallocate(obj->vm_obj);
}

/**
 * Called at close time when the filp is going away.
 *
 * Releases any remaining references on objects by this filp.
 */
void
drm_gem_release(struct drm_device *dev, struct drm_file *file_private)
{
	idr_for_each(&file_private->object_idr,
		     &drm_gem_object_release_handle, file_private);

	idr_remove_all(&file_private->object_idr);
	idr_destroy(&file_private->object_idr);
}

static struct drm_gem_object *
drm_gem_object_from_offset(struct drm_device *dev, vm_ooffset_t offset)
{
	struct drm_gem_object *obj;
	struct drm_gem_mm *mm;
	struct drm_hash_item *map_list;

	if ((offset & DRM_GEM_MAPPING_MASK) != DRM_GEM_MAPPING_KEY)
		return (NULL);
	offset &= ~DRM_GEM_MAPPING_KEY;
	mm = dev->mm_private;
	if (drm_ht_find_item(&mm->offset_hash, DRM_GEM_MAPPING_IDX(offset),
	    &map_list) != 0) {
	DRM_DEBUG("drm_gem_object_from_offset: offset 0x%jx obj not found\n",
		    (uintmax_t)offset);
		return (NULL);
	}
	obj = container_of(map_list, struct drm_gem_object, map_list);
	return (obj);
}

int
drm_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev;
	struct drm_gem_mm *mm;
	int ret;

	if (obj->on_map)
		return (0);
	dev = obj->dev;
	mm = dev->mm_private;
	ret = 0;

	obj->map_list.key = alloc_unr(mm->idxunr);
	ret = drm_ht_insert_item(&mm->offset_hash, &obj->map_list);
	if (ret != 0) {
		DRM_ERROR("failed to add to map hash\n");
		free_unr(mm->idxunr, obj->map_list.key);
		return (ret);
	}
	obj->on_map = true;
	return (0);
}

void
drm_gem_free_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_hash_item *list;
	struct drm_gem_mm *mm;

	if (!obj->on_map)
		return;
	mm = obj->dev->mm_private;
	list = &obj->map_list;

	drm_ht_remove_item(&mm->offset_hash, list);
	free_unr(mm->idxunr, list->key);
	obj->on_map = false;
}

int
drm_gem_mmap_single(struct drm_device *dev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **obj_res, int nprot)
{
	struct drm_gem_object *gem_obj;
	struct vm_object *vm_obj;

	DRM_LOCK(dev);
	gem_obj = drm_gem_object_from_offset(dev, *offset);
	if (gem_obj == NULL) {
		DRM_UNLOCK(dev);
		return (ENODEV);
	}
	drm_gem_object_reference(gem_obj);
	DRM_UNLOCK(dev);
	vm_obj = cdev_pager_allocate(gem_obj, OBJT_MGTDEVICE,
	    dev->driver->gem_pager_ops, size, nprot,
	    DRM_GEM_MAPPING_MAPOFF(*offset), curthread->td_ucred);
	if (vm_obj == NULL) {
		drm_gem_object_unreference_unlocked(gem_obj);
		return (EINVAL);
	}
	*offset = DRM_GEM_MAPPING_MAPOFF(*offset);
	*obj_res = vm_obj;
	return (0);
}

void
drm_gem_pager_dtr(void *handle)
{
	struct drm_gem_object *obj;
	struct drm_device *dev;

	obj = handle;
	dev = obj->dev;

	DRM_LOCK(dev);
	drm_gem_free_mmap_offset(obj);
	drm_gem_object_unreference(obj);
	DRM_UNLOCK(dev);
}
