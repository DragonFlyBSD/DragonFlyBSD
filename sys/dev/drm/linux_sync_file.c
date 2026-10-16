/*
 * Copyright (c) 2026
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sync_file.h>

#define DTYPE_SYNC_FILE		9

SYSCTL_DECL(_hw_dri);

static uint64_t sync_file_create_count;
static uint64_t sync_file_get_fence_count;
static uint64_t sync_file_kqfilter_count;
static uint64_t sync_file_callback_add_count;
static uint64_t sync_file_callback_fire_count;
static uint64_t sync_file_callback_remove_count;

SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_create_count, CTLFLAG_RD,
    &sync_file_create_count, 0, "sync_file create count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_get_fence_count, CTLFLAG_RD,
    &sync_file_get_fence_count, 0, "sync_file get_fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_kqfilter_count, CTLFLAG_RD,
    &sync_file_kqfilter_count, 0, "sync_file kqfilter count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_callback_add_count, CTLFLAG_RD,
    &sync_file_callback_add_count, 0, "sync_file callback add count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_callback_fire_count, CTLFLAG_RD,
    &sync_file_callback_fire_count, 0, "sync_file callback fire count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, sync_file_callback_remove_count, CTLFLAG_RD,
    &sync_file_callback_remove_count, 0, "sync_file callback remove count");

struct dragonfly_sync_file {
	struct sync_file base;
	struct dma_fence_cb cb;
	struct klist kq;
	bool callback_added;
};

static struct fileops sync_file_fileops;

static void
sync_file_fence_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct dragonfly_sync_file *sync_file;

	sync_file = container_of(cb, struct dragonfly_sync_file, cb);
	sync_file_callback_fire_count++;
	KNOTE(&sync_file->kq, 0);
}

static void
sync_file_enable_callback(struct dragonfly_sync_file *dfly_sync_file)
{
	struct sync_file *sync_file = &dfly_sync_file->base;

	if (dfly_sync_file->callback_added)
		return;

	if (dma_fence_add_callback(sync_file->fence, &dfly_sync_file->cb,
	    sync_file_fence_cb) == 0) {
		dfly_sync_file->callback_added = true;
		sync_file_callback_add_count++;
	} else {
		KNOTE(&dfly_sync_file->kq, 0);
	}
}

static int
sync_file_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
	memset(sb, 0, sizeof(*sb));
	sb->st_mode = S_IFIFO;

	return 0;
}

static int
sync_file_close(struct file *fp)
{
	struct dragonfly_sync_file *dfly_sync_file;
	struct sync_file *sync_file;

	if (fp->f_ops != &sync_file_fileops)
		return EINVAL;

	sync_file = fp->private_data;
	fp->private_data = NULL;
	if (sync_file == NULL)
		return 0;
	dfly_sync_file = container_of(sync_file, struct dragonfly_sync_file,
	    base);

	if (dfly_sync_file->callback_added &&
	    dma_fence_remove_callback(sync_file->fence, &dfly_sync_file->cb))
		sync_file_callback_remove_count++;
	dma_fence_put(sync_file->fence);
	kfree(dfly_sync_file);

	return 0;
}

static void
sync_file_filter_detach(struct knote *kn)
{
	struct dragonfly_sync_file *sync_file;

	sync_file = (struct dragonfly_sync_file *)kn->kn_hook;
	knote_remove(&sync_file->kq, kn);
}

static int
sync_file_filter_read(struct knote *kn, long hint)
{
	struct dragonfly_sync_file *dfly_sync_file;
	struct sync_file *sync_file;

	dfly_sync_file = (struct dragonfly_sync_file *)kn->kn_hook;
	sync_file = &dfly_sync_file->base;
	if (dma_fence_is_signaled(sync_file->fence)) {
		kn->kn_data = 1;
		return 1;
	}

	kn->kn_data = 0;
	return 0;
}

static struct filterops sync_file_rfiltops = {
	FILTEROP_ISFD | FILTEROP_MPSAFE,
	NULL,
	sync_file_filter_detach,
	sync_file_filter_read
};

static int
sync_file_kqfilter(struct file *fp, struct knote *kn)
{
	struct dragonfly_sync_file *dfly_sync_file;
	struct sync_file *sync_file = fp->private_data;

	if (kn->kn_filter != EVFILT_READ)
		return EOPNOTSUPP;
	if (sync_file == NULL)
		return EINVAL;

	sync_file_kqfilter_count++;
	dfly_sync_file = container_of(sync_file, struct dragonfly_sync_file,
	    base);
	kn->kn_fop = &sync_file_rfiltops;
	kn->kn_hook = (caddr_t)dfly_sync_file;
	knote_insert(&dfly_sync_file->kq, kn);
	if (dma_fence_is_signaled(sync_file->fence)) {
		KNOTE(&dfly_sync_file->kq, 0);
	} else {
		sync_file_enable_callback(dfly_sync_file);
	}

	return 0;
}

static struct fileops sync_file_fileops = {
	.fo_read = badfo_readwrite,
	.fo_write = badfo_readwrite,
	.fo_ioctl = badfo_ioctl,
	.fo_kqfilter = sync_file_kqfilter,
	.fo_stat = sync_file_stat,
	.fo_close = sync_file_close,
	.fo_shutdown = nofo_shutdown,
	.fo_seek = badfo_seek
};

struct sync_file *
sync_file_create(struct dma_fence *fence)
{
	struct dragonfly_sync_file *dfly_sync_file;
	struct sync_file *sync_file;
	struct file *fp;
	int error;

	if (fence == NULL)
		return NULL;

	sync_file_create_count++;
	error = falloc(curthread->td_lwp, &fp, NULL);
	if (error)
		return NULL;

	dfly_sync_file = kzalloc(sizeof(*dfly_sync_file), GFP_KERNEL);
	if (dfly_sync_file == NULL) {
		fdrop(fp);
		return NULL;
	}

	sync_file = &dfly_sync_file->base;
	sync_file->file = fp;
	sync_file->fence = dma_fence_get(fence);
	INIT_LIST_HEAD(&dfly_sync_file->cb.node);
	SLIST_INIT(&dfly_sync_file->kq);

	fp->f_type = DTYPE_SYNC_FILE;
	fp->f_ops = &sync_file_fileops;
	fp->private_data = sync_file;

	return sync_file;
}
EXPORT_SYMBOL(sync_file_create);

struct dma_fence *
sync_file_get_fence(int fd)
{
	struct dma_fence *fence;
	struct sync_file *sync_file;
	struct file *fp;

	sync_file_get_fence_count++;
	fp = holdfp(curthread, fd, -1);
	if (fp == NULL)
		return NULL;

	if (fp->f_ops != &sync_file_fileops) {
		dropfp(curthread, fd, fp);
		return NULL;
	}

	sync_file = fp->private_data;
	if (sync_file == NULL)
		fence = NULL;
	else
		fence = dma_fence_get(sync_file->fence);

	dropfp(curthread, fd, fp);
	return fence;
}
EXPORT_SYMBOL(sync_file_get_fence);
