/*
 * Copyright (c) 2019-2020 François Tigeot <ftigeot@wolfpond.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/reservation.h>
#include <linux/sync_file.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/ktime.h>

#include <sys/sysctl.h>
#include <sys/proc.h>

struct fileops dmabuf_fileops;

SYSCTL_DECL(_hw_dri);

static uint64_t dmabuf_export_sync_file_count;
static uint64_t dmabuf_export_sync_file_us;
static uint64_t dmabuf_export_sync_file_read_count;
static uint64_t dmabuf_export_sync_file_write_count;
static uint64_t dmabuf_export_sync_file_empty_count;
static uint64_t dmabuf_export_sync_file_single_count;
static uint64_t dmabuf_export_sync_file_array_count;
static uint64_t dmabuf_export_sync_file_signaled_count;
static uint64_t dmabuf_export_sync_file_pending_count;
static uint64_t dmabuf_import_sync_file_count;
static uint64_t dmabuf_import_sync_file_us;
static uint64_t dmabuf_import_sync_file_read_count;
static uint64_t dmabuf_import_sync_file_write_count;
static uint64_t dmabuf_import_sync_file_signaled_count;
static uint64_t dmabuf_import_sync_file_pending_count;
static uint64_t dmabuf_fd_count;
static uint64_t dmabuf_fd_error_count;
static uint64_t dmabuf_fd_us;
static uint64_t dmabuf_export_count;
static uint64_t dmabuf_get_count;
static uint64_t dmabuf_get_error_count;
static uint64_t dmabuf_close_count;
static uint64_t dmabuf_attach_count;
static uint64_t dmabuf_attach_error_count;
static uint64_t dmabuf_detach_count;
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_count, 0, "dma-buf export sync_file count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_us, CTLFLAG_RD,
    &dmabuf_export_sync_file_us, 0, "dma-buf export sync_file time");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_read_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_read_count, 0, "dma-buf export sync_file read count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_write_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_write_count, 0, "dma-buf export sync_file write count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_empty_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_empty_count, 0, "dma-buf export sync_file empty reservation count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_single_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_single_count, 0, "dma-buf export sync_file single fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_array_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_array_count, 0, "dma-buf export sync_file fence array count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_signaled_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_signaled_count, 0, "dma-buf export sync_file signaled fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_sync_file_pending_count, CTLFLAG_RD,
    &dmabuf_export_sync_file_pending_count, 0, "dma-buf export sync_file pending fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_count, CTLFLAG_RD,
    &dmabuf_import_sync_file_count, 0, "dma-buf import sync_file count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_us, CTLFLAG_RD,
    &dmabuf_import_sync_file_us, 0, "dma-buf import sync_file time");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_read_count, CTLFLAG_RD,
    &dmabuf_import_sync_file_read_count, 0, "dma-buf import sync_file read count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_write_count, CTLFLAG_RD,
    &dmabuf_import_sync_file_write_count, 0, "dma-buf import sync_file write count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_signaled_count, CTLFLAG_RD,
    &dmabuf_import_sync_file_signaled_count, 0, "dma-buf import sync_file signaled fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_import_sync_file_pending_count, CTLFLAG_RD,
    &dmabuf_import_sync_file_pending_count, 0, "dma-buf import sync_file pending fence count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_fd_count, CTLFLAG_RD,
    &dmabuf_fd_count, 0, "dma-buf fd export count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_fd_error_count, CTLFLAG_RD,
    &dmabuf_fd_error_count, 0, "dma-buf fd export error count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_fd_us, CTLFLAG_RD,
    &dmabuf_fd_us, 0, "dma-buf fd export time");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_export_count, CTLFLAG_RD,
    &dmabuf_export_count, 0, "dma-buf export count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_get_count, CTLFLAG_RD,
    &dmabuf_get_count, 0, "dma-buf get count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_get_error_count, CTLFLAG_RD,
    &dmabuf_get_error_count, 0, "dma-buf get error count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_close_count, CTLFLAG_RD,
    &dmabuf_close_count, 0, "dma-buf close count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_attach_count, CTLFLAG_RD,
    &dmabuf_attach_count, 0, "dma-buf attach count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_attach_error_count, CTLFLAG_RD,
    &dmabuf_attach_error_count, 0, "dma-buf attach error count");
SYSCTL_UQUAD(_hw_dri, OID_AUTO, dmabuf_detach_count, CTLFLAG_RD,
    &dmabuf_detach_count, 0, "dma-buf detach count");
static uint64_t
dmabuf_now_us(void)
{
	return ((uint64_t)ktime_to_us(ktime_get()));
}

static void
dmabuf_probe_fence_state(struct dma_fence *fence, uint64_t *signaled,
    uint64_t *pending)
{
	/*
	 * Ownership:
	 *   Borrows fence.  The caller owns the reference and this helper
	 *   never takes, drops, or publishes one.
	 *
	 * Lifetime:
	 *   Must run while the caller's fence reference is valid.  It only
	 *   samples the dma_fence software signaled flag; it deliberately does
	 *   not call dma_fence_is_signaled(), because that may invoke the
	 *   driver's .signaled() callback and publish completion.
	 *
	 * Threading:
	 *   Lockless diagnostic read.  The counters are best-effort telemetry
	 *   and may race with a concurrent signal path.
	 */
	if (fence != NULL &&
	    test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		(*signaled)++;
	else
		(*pending)++;
}

struct dmabuf_stub_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static const char *
dmabuf_stub_fence_get_name(struct dma_fence *fence)
{
	return "dmabufstub";
}

static const struct dma_fence_ops dmabuf_stub_fence_ops = {
	.get_driver_name = dmabuf_stub_fence_get_name,
	.get_timeline_name = dmabuf_stub_fence_get_name,
};

static bool
dmabuf_sync_flags_valid(uint32_t flags)
{
	return (flags != 0 && (flags & ~DMA_BUF_SYNC_RW) == 0);
}

static struct dma_fence *
dmabuf_signaled_fence_create(void)
{
	struct dmabuf_stub_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	lockinit(&fence->lock, "dbsf", 0, 0);
	dma_fence_init(&fence->base, &dmabuf_stub_fence_ops,
	    &fence->lock, dma_fence_context_alloc(1), 0);
	dma_fence_signal(&fence->base);

	return (&fence->base);
}

static void
dmabuf_put_fences(struct dma_fence *excl, unsigned shared_count,
    struct dma_fence **shared)
{
	unsigned i;

	if (excl != NULL)
		dma_fence_put(excl);
	for (i = 0; i < shared_count; i++)
		dma_fence_put(shared[i]);
	kfree(shared);
}

static int
dmabuf_sync_file_install(struct dma_fence *fence, int *sync_fd)
{
	struct sync_file *sync_file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return (-fd);

	sync_file = sync_file_create(fence);
	if (sync_file == NULL) {
		put_unused_fd(fd);
		return (ENOMEM);
	}

	fd_install(fd, sync_file->file);
	*sync_fd = fd;
	return (0);
}

static int
dmabuf_export_sync_file(struct dma_buf *dmabuf,
    struct dma_buf_export_sync_file *args)
{
	struct dma_fence *excl = NULL;
	struct dma_fence **shared = NULL;
	struct dma_fence **fences;
	struct dma_fence *fence = NULL;
	struct dma_fence_array *array;
	unsigned shared_count = 0;
	unsigned count, i, out;
	int ret;

	if (!dmabuf_sync_flags_valid(args->flags))
		return (EINVAL);
	if (dmabuf->resv == NULL)
		return (EINVAL);

	if ((args->flags & DMA_BUF_SYNC_WRITE) == 0) {
		dmabuf_export_sync_file_read_count++;
		fence = reservation_object_get_excl_rcu(dmabuf->resv);
		if (fence == NULL) {
			dmabuf_export_sync_file_empty_count++;
			fence = dmabuf_signaled_fence_create();
		} else {
			dmabuf_export_sync_file_single_count++;
		}
		if (fence == NULL)
			return (ENOMEM);

		dmabuf_probe_fence_state(fence,
		    &dmabuf_export_sync_file_signaled_count,
		    &dmabuf_export_sync_file_pending_count);
		ret = dmabuf_sync_file_install(fence, &args->fd);
		dma_fence_put(fence);
		return (ret);
	}

	dmabuf_export_sync_file_write_count++;
	ret = reservation_object_get_fences_rcu(dmabuf->resv, &excl,
	    &shared_count, &shared);
	if (ret < 0)
		return (-ret);

	count = shared_count + (excl != NULL ? 1 : 0);
	if (count == 0) {
		dmabuf_export_sync_file_empty_count++;
		fence = dmabuf_signaled_fence_create();
		if (fence == NULL)
			return (ENOMEM);
		dmabuf_probe_fence_state(fence,
		    &dmabuf_export_sync_file_signaled_count,
		    &dmabuf_export_sync_file_pending_count);
		ret = dmabuf_sync_file_install(fence, &args->fd);
		dma_fence_put(fence);
		return (ret);
	}

	if (count == 1) {
		dmabuf_export_sync_file_single_count++;
		fence = excl != NULL ? excl : shared[0];
		dmabuf_probe_fence_state(fence,
		    &dmabuf_export_sync_file_signaled_count,
		    &dmabuf_export_sync_file_pending_count);
		ret = dmabuf_sync_file_install(fence, &args->fd);
		dmabuf_put_fences(excl, shared_count, shared);
		return (ret);
	}

	dmabuf_export_sync_file_array_count++;
	fences = kmalloc_array(count, sizeof(*fences), GFP_KERNEL);
	if (fences == NULL) {
		dmabuf_put_fences(excl, shared_count, shared);
		return (ENOMEM);
	}

	out = 0;
	if (excl != NULL) {
		dmabuf_probe_fence_state(excl,
		    &dmabuf_export_sync_file_signaled_count,
		    &dmabuf_export_sync_file_pending_count);
		fences[out++] = excl;
	}
	for (i = 0; i < shared_count; i++) {
		dmabuf_probe_fence_state(shared[i],
		    &dmabuf_export_sync_file_signaled_count,
		    &dmabuf_export_sync_file_pending_count);
		fences[out++] = shared[i];
	}
	kfree(shared);

	array = dma_fence_array_create(count, fences, dma_fence_context_alloc(1),
	    0, false);
	if (array == NULL) {
		for (i = 0; i < count; i++)
			dma_fence_put(fences[i]);
		kfree(fences);
		return (ENOMEM);
	}

	ret = dmabuf_sync_file_install(&array->base, &args->fd);
	dma_fence_put(&array->base);
	return (ret);
}

static int
dmabuf_import_sync_file(struct dma_buf *dmabuf,
    const struct dma_buf_import_sync_file *args)
{
	struct dma_fence *fence;
	int ret;

	if (!dmabuf_sync_flags_valid(args->flags))
		return (EINVAL);
	if (dmabuf->resv == NULL)
		return (EINVAL);

	fence = sync_file_get_fence(args->fd);
	if (fence == NULL)
		return (EINVAL);
	if ((args->flags & DMA_BUF_SYNC_WRITE) != 0)
		dmabuf_import_sync_file_write_count++;
	else
		dmabuf_import_sync_file_read_count++;
	dmabuf_probe_fence_state(fence, &dmabuf_import_sync_file_signaled_count,
	    &dmabuf_import_sync_file_pending_count);

	ret = reservation_object_lock(dmabuf->resv, NULL);
	if (ret < 0) {
		dma_fence_put(fence);
		return (-ret);
	}

	if ((args->flags & DMA_BUF_SYNC_WRITE) != 0) {
		reservation_object_add_excl_fence(dmabuf->resv, fence);
		ret = 0;
	} else {
		ret = reservation_object_reserve_shared(dmabuf->resv);
		if (ret == 0)
			reservation_object_add_shared_fence(dmabuf->resv, fence);
	}

	reservation_object_unlock(dmabuf->resv);
	dma_fence_put(fence);

	return (ret < 0 ? -ret : ret);
}

static int
dmabuf_stat(struct file *fp, struct stat *sb, struct ucred *cred)
{
STUB();
	KASSERT(fp->f_type == DTYPE_DMABUF, ("fp is not DMABUF"));
	struct dma_buf *dmabuf = fp->private_data;

	memset(sb, 0, sizeof(*sb));
	sb->st_size = dmabuf->size;
	sb->st_mode = S_IFIFO;	/* XXX */

	return (0);
}

static int
dmabuf_close(struct file *fp)
{
	struct dma_buf *dmabuf;
	if (fp->f_ops != &dmabuf_fileops) {
		kprintf("dmabuf_close(): file->f_ops != &dmabuf_fileops\n");
		return EINVAL;
	}
	dmabuf_close_count++;
	dmabuf = fp->private_data;
	dmabuf->ops->release(dmabuf);
	kfree(dmabuf);

	//kprintf("dmabuf_close(): success\n");
	return 0;
}

static int
dmabuf_ioctl(struct file *fp, u_long com, caddr_t data,
	    struct ucred *cred, struct sysmsg *msgv)
{
	struct dma_buf *dmabuf;

	if (fp->f_ops != &dmabuf_fileops)
		return (EBADF);
	dmabuf = fp->private_data;
	if (dmabuf == NULL)
		return (EBADF);

	switch (com) {
	case DMA_BUF_IOCTL_EXPORT_SYNC_FILE: {
		uint64_t start = dmabuf_now_us();
		struct dma_buf_export_sync_file *args =
		    (struct dma_buf_export_sync_file *)data;
		int ret;

		ret = dmabuf_export_sync_file(dmabuf, args);
		dmabuf_export_sync_file_count++;
		dmabuf_export_sync_file_us += dmabuf_now_us() - start;
		return (ret);
	}
	case DMA_BUF_IOCTL_IMPORT_SYNC_FILE: {
		uint64_t start = dmabuf_now_us();
		const struct dma_buf_import_sync_file *args =
		    (const struct dma_buf_import_sync_file *)data;
		int ret;

		ret = dmabuf_import_sync_file(dmabuf, args);
		dmabuf_import_sync_file_count++;
		dmabuf_import_sync_file_us += dmabuf_now_us() - start;
		return (ret);
	}
	default:
		return (ENOTTY);
	}
}

static int
dmabuf_seek(struct file *fp, off_t offset, int whence, off_t *res)
{
	KASSERT(fp->f_type == DTYPE_DMABUF, ("fp is not DMABUF"));
	struct dma_buf *dmabuf = fp->private_data;
	off_t newoff;

	if (offset != 0) {
		return EINVAL;
	}

	switch (whence) {
	case SEEK_SET:
		newoff = 0;
		break;
	case SEEK_END:
		newoff = dmabuf->size;
		break;
	default:
		return EINVAL;
	}
	spin_lock(&fp->f_spin);
	fp->f_offset = newoff;
	spin_unlock(&fp->f_spin);
	*res = newoff;
	return 0;
	
}

struct fileops dmabuf_fileops = {
	.fo_read	= badfo_readwrite,
	.fo_write	= badfo_readwrite,
	.fo_ioctl	= dmabuf_ioctl,
	.fo_kqfilter	= badfo_kqfilter,
	.fo_stat	= dmabuf_stat,
	.fo_close	= dmabuf_close,
	.fo_seek	= dmabuf_seek,
};

struct dma_buf *
dma_buf_export(const struct dma_buf_export_info *exp_info)
{
	struct dma_buf *dmabuf;
	struct file *fp;

	falloc(curthread->td_lwp, &fp, NULL);
	if (fp == NULL)
		return ERR_PTR(-ENFILE);

	dmabuf_export_count++;
	dmabuf = kmalloc(sizeof(struct dma_buf), M_DRM, M_WAITOK);
	fp->f_type = DTYPE_DMABUF;
	fp->f_flag = FREAD | FWRITE;
	fp->f_ops = &dmabuf_fileops;
	fp->private_data = dmabuf;
	dmabuf->priv = exp_info->priv;
	dmabuf->ops = exp_info->ops;
	dmabuf->size = exp_info->size;
	dmabuf->file = fp;
	dmabuf->resv = exp_info->resv;

	return dmabuf;
}

/*
 * dma_buf_attach()
 *
 * Ownership:
 *   The returned attachment is newly allocated and owned by the caller.  The
 *   caller's existing dma-buf reference remains owned by the caller; this
 *   helper does not acquire a file reference.
 *
 * Lifetime:
 *   The attachment is valid until dma_buf_detach().  Exporter attach hooks may
 *   store exporter-private state in attach->priv and must release it from
 *   their detach hook or from their own attach error path.
 *
 * Threading:
 *   May sleep in the exporter attach hook.  No global dma-buf lock is held
 *   across the callback in this DragonFly shim.
 */
struct dma_buf_attachment *
dma_buf_attach(struct dma_buf *dmabuf, struct device *dev)
{
	struct dma_buf_attachment *attach;
	int ret;

	dmabuf_attach_count++;
	if (dmabuf == NULL || dmabuf->ops == NULL) {
		dmabuf_attach_error_count++;
		return ERR_PTR(-EINVAL);
	}

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (attach == NULL) {
		dmabuf_attach_error_count++;
		return ERR_PTR(-ENOMEM);
	}
	attach->dmabuf = dmabuf;
	attach->dev = dev;
	attach->priv = NULL;

	if (dmabuf->ops->attach != NULL) {
		ret = dmabuf->ops->attach(dmabuf, attach);
		if (ret != 0) {
			if (ret > 0)
				ret = -ret;
			kfree(attach);
			dmabuf_attach_error_count++;
			return ERR_PTR(ret);
		}
	}

	return attach;
}

/*
 * dma_buf_detach()
 *
 * Ownership:
 *   Consumes the attachment allocation.  The caller still owns and must release
 *   any dma-buf reference it acquired for the import.
 *
 * Lifetime:
 *   attach->dmabuf must match dmabuf.  A mismatch is ignored after counting it
 *   as an attach/detach protocol error because detach paths can run during
 *   object teardown.
 *
 * Threading:
 *   May sleep in exporter detach hooks.  Callers must serialize against their
 *   own imported-object lifetime.
 */
void
dma_buf_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attach)
{
	if (dmabuf == NULL || attach == NULL || attach->dmabuf != dmabuf) {
		dmabuf_attach_error_count++;
		return;
	}
	if (dmabuf->ops != NULL && dmabuf->ops->detach != NULL)
		dmabuf->ops->detach(dmabuf, attach);
	kfree(attach);
	dmabuf_detach_count++;
}

int
dma_buf_fd(struct dma_buf *dmabuf, int flags)
{
	uint64_t start = dmabuf_now_us();
	int fd;
	int ret;

	dmabuf_fd_count++;

	if (dmabuf == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (dmabuf->file == NULL) {
		ret = -EINVAL;
		goto out;
	}

	fd = get_unused_fd_flags(flags);
	if (fd < 0) {
		ret = fd;
		goto out;
	}

	fd_install(fd, dmabuf->file);
	ret = fd;
out:
	if (ret < 0)
		dmabuf_fd_error_count++;
	dmabuf_fd_us += dmabuf_now_us() - start;
	return ret;
}

struct dma_buf *
dma_buf_get(int fd)
{
	struct file *fp;
	struct dma_buf *dmabuf;

	dmabuf_get_count++;
	if ((fp = holdfp(curthread, fd, -1)) == NULL) {
		dmabuf_get_error_count++;
		return ERR_PTR(-EBADF);
	}

	if (fp->f_ops != &dmabuf_fileops) {
		kprintf("dma_buf_get(): file->f_ops != &dmabuf_fileops\n");
		dropfp(curthread, fd, fp);
		dmabuf_get_error_count++;
		return ERR_PTR(-EBADF);
	}

	dmabuf = fp->private_data;
	/* Keep holdfp()'s reference; the caller releases it with dma_buf_put(). */

	return dmabuf;
}

struct sg_table *
dma_buf_map_attachment(struct dma_buf_attachment *attach,
				enum dma_data_direction direction)
{
	struct sg_table *sg_table;

	if (attach == NULL)
		return ERR_PTR(-EINVAL);

	if (attach->dmabuf == NULL || attach->dmabuf->ops == NULL ||
	    attach->dmabuf->ops->map_dma_buf == NULL)
		return ERR_PTR(-EINVAL);

	sg_table = attach->dmabuf->ops->map_dma_buf(attach, direction);
	if (sg_table == NULL)
		return ERR_PTR(-ENOMEM);

	return sg_table;
}

void dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
				struct sg_table *sg_table,
				enum dma_data_direction direction)
{
	if (attach == NULL || attach->dmabuf == NULL ||
	    attach->dmabuf->ops == NULL ||
	    attach->dmabuf->ops->unmap_dma_buf == NULL)
		return;
	attach->dmabuf->ops->unmap_dma_buf(attach, sg_table, direction);
}
