/*
 * Copyright (c) 2026 Leding Li <lileding@gmail.com>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/bitops.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drmP.h>
#include <drm/drm_auth.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_lease.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_plane.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

#ifdef __DragonFly__
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmsg.h>

#define DTYPE_DRM_LEASE		10
#endif

static struct fileops drm_lease_fileops;
static uint64_t drm_lease_idr_object;

static bool
drm_lease_master_has_objects_locked(struct drm_master *master)
{
	int id = 0;

	return idr_get_next(&master->leases, &id) != NULL;
}

static struct drm_master *
drm_lease_owner(struct drm_master *master)
{
	while (master != NULL && master->lessor != NULL)
		master = master->lessor;
	return master;
}

/*
 * drm_lease_find_lessee_locked()
 *
 * Ownership:
 *   Borrows master and the returned lessee.  The caller must not drop the
 *   returned pointer after idr_mutex is released.
 *
 * Lifetime:
 *   Valid only while dev->mode_config.idr_mutex protects the owner lessee_idr.
 *   Lessee ids are allocated from the top-level owner, even when the caller is
 *   itself a lessee.
 *
 * Threading:
 *   Requires dev->mode_config.idr_mutex.  No additional references are taken.
 */
static struct drm_master *
drm_lease_find_lessee_locked(struct drm_master *master, int lessee_id)
{
	struct drm_master *owner;

	owner = drm_lease_owner(master);
	if (owner == NULL)
		return NULL;
	return idr_find(&owner->lessee_idr, lessee_id);
}

static bool
_drm_lease_held_master(struct drm_master *master, int id)
{
	if (master == NULL || master->lessor == NULL)
		return true;
	return idr_find(&master->leases, id) != NULL;
}

bool
_drm_lease_held(struct drm_file *file_priv, int id)
{
	struct drm_master *master;
	bool held;

	if (file_priv == NULL)
		return true;
	master = drm_file_get_master(file_priv);
	if (master == NULL)
		return true;
	held = _drm_lease_held_master(master, id);
	drm_master_put(&master);
	return held;
}

bool
drm_lease_held(struct drm_file *file_priv, int id)
{
	struct drm_device *dev;
	struct drm_master *master;
	bool held;

	if (file_priv == NULL)
		return true;

	master = drm_file_get_master(file_priv);
	if (master == NULL || master->lessor == NULL) {
		if (master != NULL)
			drm_master_put(&master);
		return true;
	}

	dev = file_priv->minor->dev;
	mutex_lock(&dev->mode_config.idr_mutex);
	held = _drm_lease_held_master(master, id);
	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&master);

	return held;
}

uint32_t
drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs)
{
	struct drm_device *dev;
	struct drm_crtc *crtc;
	uint32_t visible = 0;
	uint32_t out_bit = 1;

	struct drm_master *master;

	if (file_priv == NULL)
		return crtcs;
	master = drm_file_get_master(file_priv);
	if (master == NULL || master->lessor == NULL) {
		if (master != NULL)
			drm_master_put(&master);
		return crtcs;
	}

	dev = file_priv->minor->dev;
	mutex_lock(&dev->mode_config.idr_mutex);
	drm_for_each_crtc(crtc, dev) {
		if (_drm_lease_held_master(master, crtc->base.id)) {
			if (crtcs & drm_crtc_mask(crtc))
				visible |= out_bit;
			out_bit <<= 1;
		}
	}
	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&master);

	return visible;
}

static void
drm_lease_remove_from_owner_locked(struct drm_master *lessee)
{
	struct drm_master *owner = lessee->lessor;

	if (owner == NULL)
		return;

	if (lessee->lessee_id != 0) {
		idr_remove(&owner->lessee_idr, lessee->lessee_id);
		lessee->lessee_id = 0;
	}
	if (!list_empty(&lessee->lessee_list))
		list_del_init(&lessee->lessee_list);
}

/*
 * drm_lease_clear_objects_locked()
 *
 * Ownership:
 *   Borrows the lessee master and removes only its leased KMS object ids.  The
 *   caller keeps ownership of the master, lessee id, lessor reference, and list
 *   links.
 *
 * Lifetime:
 *   Called while the master is still alive.  Revoke uses this to make the lease
 *   empty without destroying the fd-visible lessee identity; close/destroy
 *   performs the final owner-list removal.
 *
 * Threading:
 *   dev->mode_config.idr_mutex must be held by the caller.
 */
static void
drm_lease_clear_objects_locked(struct drm_master *master)
{
	idr_remove_all(&master->leases);
}

static void
drm_lease_revoke_locked(struct drm_master *master)
{
	struct drm_master *lessee;

	if (master->lessor != NULL) {
		drm_lease_clear_objects_locked(master);
		return;
	}

	list_for_each_entry(lessee, &master->lessees, lessee_list)
		drm_lease_clear_objects_locked(lessee);
}

void
drm_lease_revoke(struct drm_master *master)
{
	struct drm_device *dev;

	if (master == NULL)
		return;

	dev = master->dev;
	mutex_lock(&dev->mode_config.idr_mutex);
	drm_lease_revoke_locked(master);
	mutex_unlock(&dev->mode_config.idr_mutex);
}

void
drm_lease_destroy(struct drm_master *master)
{
	struct drm_master *lessor;
	struct drm_device *dev;

	if (master == NULL)
		return;

	dev = master->dev;
	mutex_lock(&dev->mode_config.idr_mutex);
	drm_lease_revoke_locked(master);
	drm_lease_remove_from_owner_locked(master);
	lessor = master->lessor;
	master->lessor = NULL;
	mutex_unlock(&dev->mode_config.idr_mutex);

	if (lessor != NULL)
		drm_master_put(&lessor);
}

#ifdef __DragonFly__
static int
drm_lease_read(struct file *fp, struct uio *uio, struct ucred *cred,
    int flags)
{
	struct dev_read_args args;

	memset(&args, 0, sizeof(args));
	args.a_fp = fp;
	args.a_uio = uio;
	args.a_ioflag = flags;

	return drm_read(&args);
}

static int
drm_lease_ioctl(struct file *fp, u_long cmd, caddr_t data, struct ucred *cred,
    struct sysmsg *msg)
{
	struct dev_ioctl_args args;

	memset(&args, 0, sizeof(args));
	args.a_fp = fp;
	args.a_cmd = cmd;
	args.a_data = data;
	args.a_fflag = fp->f_flag;
	args.a_cred = cred;
	args.a_sysmsg = msg;

	return drm_ioctl(&args);
}

static int
drm_lease_kqfilter(struct file *fp, struct knote *kn)
{
	struct dev_kqfilter_args args;

	memset(&args, 0, sizeof(args));
	args.a_fp = fp;
	args.a_kn = kn;

	if (drm_kqfilter(&args) != 0)
		return EINVAL;

	return args.a_result;
}

static int
drm_lease_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
	memset(sb, 0, sizeof(*sb));
	sb->st_mode = S_IFCHR;

	return 0;
}

static int
drm_lease_close(struct file *fp)
{
	struct drm_file *file_priv;

	if (fp->f_ops != &drm_lease_fileops)
		return EBADF;

	file_priv = fp->private_data;
	fp->private_data = NULL;
	if (file_priv == NULL)
		return 0;

	drm_file_close_counted(file_priv);
	return 0;
}

static int
drm_lease_seek(struct file *fp, off_t offset, int whence, off_t *res)
{
	return ESPIPE;
}

static struct fileops drm_lease_fileops = {
	.fo_read = drm_lease_read,
	.fo_write = badfo_readwrite,
	.fo_ioctl = drm_lease_ioctl,
	.fo_kqfilter = drm_lease_kqfilter,
	.fo_stat = drm_lease_stat,
	.fo_close = drm_lease_close,
	.fo_shutdown = nofo_shutdown,
	.fo_seek = drm_lease_seek,
};

static int
drm_lease_alloc_fd(struct drm_file *owner_file, struct drm_master *lessee,
    unsigned int flags, int *fdp, struct drm_file **lease_filep,
    struct file **fpp)
{
	struct drm_device *dev = owner_file->minor->dev;
	struct drm_minor *minor;
	struct drm_file *lease_file;
	struct file *fp;
	int error;
	int fd;

	if (flags & ~(O_CLOEXEC | O_NONBLOCK))
		return -EINVAL;

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	error = falloc(curthread->td_lwp, &fp, NULL);
	if (error != 0) {
		put_unused_fd(fd);
		return -error;
	}

	minor = drm_minor_acquire(owner_file->minor->index);
	if (IS_ERR(minor)) {
		fdrop(fp);
		put_unused_fd(fd);
		return PTR_ERR(minor);
	}

	lease_file = drm_file_alloc(minor);
	if (IS_ERR(lease_file)) {
		drm_minor_release(minor);
		fdrop(fp);
		put_unused_fd(fd);
		return PTR_ERR(lease_file);
	}

	/*
	 * Ownership: the file owns one master reference until close; the
	 * lessee keeps a separate lessor reference until master destruction.
	 * Lifetime: the caller installs fp only after the lease tree commit, so
	 * userspace cannot close a half-constructed lease fd.
	 * Threading: open_count and filelist follow the same drm_global_mutex /
	 * filelist_mutex order as the devfs open/close path.
	 */
	lease_file->master = drm_master_get(lessee);
	lease_file->is_master = 1;
	lease_file->authenticated = 1;
	lease_file->filp = fp;

	fp->f_type = DTYPE_DRM_LEASE;
	fp->f_flag = FREAD | FWRITE;
	if (flags & O_NONBLOCK)
		fp->f_flag |= FNONBLOCK;
	fp->f_ops = &drm_lease_fileops;
	fp->private_data = lease_file;

	mutex_lock(&drm_global_mutex);
	dev->open_count++;
	mutex_lock(&dev->filelist_mutex);
	list_add(&lease_file->lhead, &dev->filelist);
	mutex_unlock(&dev->filelist_mutex);
	mutex_unlock(&drm_global_mutex);

	*fdp = fd;
	*lease_filep = lease_file;
	*fpp = fp;

	return 0;
}
#else
static int
drm_lease_alloc_fd(struct drm_file *owner_file, struct drm_master *lessee,
    unsigned int flags, int *fdp, struct drm_file **lease_filep,
    struct file **fpp)
{
	return -EOPNOTSUPP;
}
#endif

static bool
drm_lease_object_busy_locked(struct drm_master *owner, uint32_t object_id)
{
	struct drm_master *lessee;

	list_for_each_entry(lessee, &owner->lessees, lessee_list) {
		if (idr_find(&lessee->leases, object_id) != NULL)
			return true;
	}

	return false;
}

static int
drm_lease_prepare_objects(struct drm_device *dev, struct drm_file *file_priv,
    struct drm_master *master, uint32_t *object_ids, uint32_t object_count,
    bool *has_crtc, bool *has_connector, bool *has_plane)
{
	struct drm_master *owner = drm_lease_owner(master);
	struct drm_mode_object *obj;
	uint32_t max_objects;
	uint32_t i;
	uint32_t j;
	int ret = 0;

	max_objects = dev->mode_config.num_crtc +
	    dev->mode_config.num_connector + dev->mode_config.num_total_plane;
	if (object_count > max_objects)
		return -EINVAL;

	for (i = 0; i < object_count; i++) {
		for (j = 0; j < i; j++) {
			if (object_ids[i] == object_ids[j])
				return -EEXIST;
		}

		mutex_lock(&dev->mode_config.idr_mutex);
		obj = __drm_mode_object_find(dev, NULL, object_ids[i],
		    DRM_MODE_OBJECT_ANY);
		if (obj == NULL) {
			ret = -ENOENT;
		} else if (!drm_mode_object_lease_required(obj->type)) {
			ret = -EINVAL;
		} else if (drm_lease_object_busy_locked(owner, object_ids[i])) {
			ret = -EBUSY;
		} else {
			switch (obj->type) {
			case DRM_MODE_OBJECT_CRTC:
				*has_crtc = true;
				break;
			case DRM_MODE_OBJECT_CONNECTOR:
				*has_connector = true;
				break;
			case DRM_MODE_OBJECT_PLANE:
				*has_plane = true;
				break;
			default:
				break;
			}
		}
		mutex_unlock(&dev->mode_config.idr_mutex);

		if (obj != NULL)
			drm_mode_object_put(obj);
		if (ret != 0)
			return ret;
	}

	if (object_count == 0)
		return 0;

	if (!*has_crtc || !*has_connector)
		return -EINVAL;
	if (file_priv->universal_planes && !*has_plane)
		return -EINVAL;

	return 0;
}

static int
drm_lease_insert_object(struct drm_device *dev, struct idr *leases,
    uint32_t object_id)
{
	struct drm_mode_object *obj;
	int ret;

	obj = __drm_mode_object_find(dev, NULL, object_id,
	    DRM_MODE_OBJECT_ANY);
	if (obj == NULL)
		return -ENOENT;

	ret = idr_alloc(leases, &drm_lease_idr_object, object_id,
	    object_id + 1, GFP_KERNEL);
	drm_mode_object_put(obj);
	return ret;
}

static int
drm_lease_populate_master(struct drm_device *dev, struct drm_file *file_priv,
    struct drm_master *lessee, uint32_t *object_ids, uint32_t object_count)
{
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	uint32_t i;
	int ret;

	for (i = 0; i < object_count; i++) {
		obj = __drm_mode_object_find(dev, NULL, object_ids[i],
		    DRM_MODE_OBJECT_ANY);
		if (obj == NULL)
			return -ENOENT;

		ret = idr_alloc(&lessee->leases, &drm_lease_idr_object,
		    object_ids[i], object_ids[i] + 1, GFP_KERNEL);
		if (ret >= 0 && !file_priv->universal_planes &&
		    obj->type == DRM_MODE_OBJECT_CRTC) {
			crtc = obj_to_crtc(obj);
			ret = drm_lease_insert_object(dev, &lessee->leases,
			    crtc->primary->base.id);
			if (ret >= 0 && crtc->cursor != NULL) {
				ret = drm_lease_insert_object(dev,
				    &lessee->leases, crtc->cursor->base.id);
			}
		}
		drm_mode_object_put(obj);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int
drm_mode_create_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_mode_create_lease *arg = data;
	struct drm_master *master;
	struct drm_master *owner;
	struct drm_master *lessee;
	struct drm_file *lease_file = NULL;
	struct file *lease_fp = NULL;
	void *entry;
	uint32_t *object_ids;
	bool has_connector = false;
	bool has_crtc = false;
	bool has_plane = false;
	int fd = -1;
	int id;
	int lessee_id;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;
	master = drm_file_get_master(file_priv);
	if (master == NULL)
		return -EACCES;
	if (master->lessor != NULL) {
		drm_master_put(&master);
		return -EINVAL;
	}
	if (arg->flags & ~(O_CLOEXEC | O_NONBLOCK)) {
		ret = -EINVAL;
		goto out_master_ref;
	}
	if (arg->object_count > dev->mode_config.num_crtc +
	    dev->mode_config.num_connector + dev->mode_config.num_total_plane) {
		ret = -EINVAL;
		goto out_master_ref;
	}

	object_ids = NULL;
	if (arg->object_count != 0) {
		object_ids = kmalloc_array(arg->object_count,
		    sizeof(*object_ids), GFP_KERNEL);
		if (object_ids == NULL) {
			ret = -ENOMEM;
			goto out_master_ref;
		}
		if (copy_from_user(object_ids, u64_to_user_ptr(arg->object_ids),
		    arg->object_count * sizeof(*object_ids))) {
			ret = -EFAULT;
			goto out_ids;
		}
	}

	ret = drm_lease_prepare_objects(dev, file_priv, master, object_ids,
	    arg->object_count, &has_crtc, &has_connector, &has_plane);
	if (ret != 0)
		goto out_ids;

	owner = drm_lease_owner(master);
	lessee = drm_master_create(dev);
	if (lessee == NULL) {
		ret = -ENOMEM;
		goto out_ids;
	}
	lessee->lessor = drm_master_get(owner);

	ret = drm_lease_populate_master(dev, file_priv, lessee, object_ids,
	    arg->object_count);
	if (ret != 0)
		goto out_master;

	ret = drm_lease_alloc_fd(file_priv, lessee, arg->flags, &fd,
	    &lease_file, &lease_fp);
	if (ret != 0)
		goto out_master;

	mutex_lock(&dev->mode_config.idr_mutex);
	idr_for_each_entry(&lessee->leases, entry, id) {
		if (drm_lease_object_busy_locked(owner, (uint32_t)id)) {
			mutex_unlock(&dev->mode_config.idr_mutex);
			ret = -EBUSY;
			goto out_file;
		}
	}

	lessee_id = idr_alloc(&owner->lessee_idr, lessee, 1, 0, GFP_KERNEL);
	if (lessee_id < 0) {
		mutex_unlock(&dev->mode_config.idr_mutex);
		ret = lessee_id;
		goto out_file;
	}
	lessee->lessee_id = lessee_id;
	list_add_tail(&lessee->lessee_list, &owner->lessees);
	mutex_unlock(&dev->mode_config.idr_mutex);

	arg->lessee_id = lessee_id;
	arg->fd = fd;
	fd_install(fd, lease_fp);
	drm_master_put(&lessee);
	kfree(object_ids);
	drm_master_put(&master);
	return 0;

out_file:
	lease_fp->private_data = NULL;
	drm_file_close_counted(lease_file);
	fdrop(lease_fp);
	put_unused_fd(fd);
out_master:
	drm_master_put(&lessee);
out_ids:
	kfree(object_ids);
out_master_ref:
	drm_master_put(&master);
	return ret;
}

int
drm_mode_list_lessees_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_mode_list_lessees *arg = data;
	struct drm_master *master;
	struct drm_master *owner;
	struct drm_master *lessee;
	uint32_t __user *lessee_ptr;
	uint32_t count = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;
	if (arg->pad)
		return -EINVAL;
	master = drm_file_get_master(file_priv);
	if (master == NULL)
		return -EACCES;

	owner = master;
	lessee_ptr = u64_to_user_ptr(arg->lessees_ptr);

	mutex_lock(&dev->mode_config.idr_mutex);
	list_for_each_entry(lessee, &owner->lessees, lessee_list) {
		if (!drm_lease_master_has_objects_locked(lessee))
			continue;
		if (count < arg->count_lessees &&
		    put_user((uint32_t)lessee->lessee_id, lessee_ptr + count)) {
			mutex_unlock(&dev->mode_config.idr_mutex);
			drm_master_put(&master);
			return -EFAULT;
		}
		count++;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	arg->count_lessees = count;
	drm_master_put(&master);
	return 0;
}

static int
drm_lease_count_owner_objects(struct drm_device *dev,
    struct drm_mode_get_lease *arg)
{
	struct drm_mode_object *obj;
	uint32_t __user *object_ptr = u64_to_user_ptr(arg->objects_ptr);
	uint32_t count = 0;
	int id;

	mutex_lock(&dev->mode_config.idr_mutex);
	idr_for_each_entry(&dev->mode_config.crtc_idr, obj, id) {
		if (count < arg->count_objects &&
		    put_user((uint32_t)id, object_ptr + count)) {
			mutex_unlock(&dev->mode_config.idr_mutex);
			return -EFAULT;
		}
		count++;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	arg->count_objects = count;
	return 0;
}

int
drm_mode_get_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_mode_get_lease *arg = data;
	struct drm_master *master;
	void *entry;
	uint32_t __user *object_ptr = u64_to_user_ptr(arg->objects_ptr);
	uint32_t count = 0;
	int id;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;
	if (arg->pad)
		return -EINVAL;
	master = drm_file_get_master(file_priv);
	if (master == NULL)
		return -EACCES;

	if (master->lessor == NULL) {
		int ret = drm_lease_count_owner_objects(dev, arg);

		drm_master_put(&master);
		return ret;
	}

	mutex_lock(&dev->mode_config.idr_mutex);
	idr_for_each_entry(&master->leases, entry, id) {
		if (count < arg->count_objects &&
		    put_user((uint32_t)id, object_ptr + count)) {
			mutex_unlock(&dev->mode_config.idr_mutex);
			drm_master_put(&master);
			return -EFAULT;
		}
		count++;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	arg->count_objects = count;
	drm_master_put(&master);
	return 0;
}

int
drm_mode_revoke_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_mode_revoke_lease *arg = data;
	struct drm_master *master;
	struct drm_master *owner;
	struct drm_master *lessee;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;
	master = drm_file_get_master(file_priv);
	if (master == NULL)
		return -EACCES;

	owner = master;

	mutex_lock(&dev->mode_config.idr_mutex);
	lessee = drm_lease_find_lessee_locked(owner, arg->lessee_id);
	if (lessee == NULL) {
		mutex_unlock(&dev->mode_config.idr_mutex);
		drm_master_put(&master);
		return -ENOENT;
	}
	if (lessee->lessor != owner) {
		mutex_unlock(&dev->mode_config.idr_mutex);
		drm_master_put(&master);
		return -EACCES;
	}
	drm_lease_revoke_locked(lessee);
	mutex_unlock(&dev->mode_config.idr_mutex);
	drm_master_put(&master);

	return 0;
}
