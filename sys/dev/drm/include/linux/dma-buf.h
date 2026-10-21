/*
 * Copyright (c) 2018-2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef LINUX_DMA_BUF_H
#define LINUX_DMA_BUF_H

#include <sys/ioccom.h>

#include <linux/err.h>
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/dma-fence.h>
#include <linux/wait.h>

#include <linux/slab.h>

#define DMA_BUF_SYNC_READ	(1 << 0)
#define DMA_BUF_SYNC_WRITE	(2 << 0)
#define DMA_BUF_SYNC_RW		(DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)

struct dma_buf_export_sync_file {
	uint32_t flags;
	int32_t fd;
};

struct dma_buf_import_sync_file {
	uint32_t flags;
	int32_t fd;
};

#define DMA_BUF_BASE			'b'
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE	\
	_IOWR(DMA_BUF_BASE, 2, struct dma_buf_export_sync_file)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE	\
	_IOW(DMA_BUF_BASE, 3, struct dma_buf_import_sync_file)

struct dma_buf;
struct dma_buf_attachment;

struct dma_buf_ops {
	struct sg_table * (*map_dma_buf)(struct dma_buf_attachment *,
						enum dma_data_direction);
	void (*unmap_dma_buf)(struct dma_buf_attachment *,
						struct sg_table *,
						enum dma_data_direction);
	void (*release)(struct dma_buf *);
	void *(*map)(struct dma_buf *, unsigned long);
	void *(*map_atomic)(struct dma_buf *, unsigned long);
	void (*unmap)(struct dma_buf *, unsigned long, void *);
	void (*unmap_atomic)(struct dma_buf *, unsigned long, void *);
	int (*mmap)(struct dma_buf *, struct vm_area_struct *vma);
	void *(*vmap)(struct dma_buf *);
	void (*vunmap)(struct dma_buf *, void *vaddr);
	int (*begin_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int (*end_cpu_access)(struct dma_buf *, enum dma_data_direction);
	int (*attach)(struct dma_buf *, struct dma_buf_attachment *);
	void (*detach)(struct dma_buf *, struct dma_buf_attachment *);
};

struct dma_buf {
	struct reservation_object *resv;
	void *priv;
	const struct dma_buf_ops *ops;
	size_t size;
	struct file *file;
};

struct dma_buf_attachment {
	struct dma_buf *dmabuf;
	struct device *dev;
	void *priv;
};

struct dma_buf_export_info {
	const struct dma_buf_ops *ops;
	size_t size;
	int flags;
	void *priv;
	struct reservation_object *resv;
};

struct dma_buf *dma_buf_export(const struct dma_buf_export_info *exp_info);

#define DEFINE_DMA_BUF_EXPORT_INFO(name)	\
	struct dma_buf_export_info name = {	\
	}

struct sg_table * dma_buf_map_attachment(struct dma_buf_attachment *,
						enum dma_data_direction);
void dma_buf_unmap_attachment(struct dma_buf_attachment *,
				struct sg_table *, enum dma_data_direction);

/*
 * dma_buf_attach()
 *
 * Ownership:
 *   The caller keeps ownership of the dma-buf reference it already holds.
 *   The returned attachment is owned by the caller and must be released with
 *   dma_buf_detach().
 *
 * Lifetime:
 *   dmabuf and dev must remain valid until dma_buf_detach() returns.  The
 *   attachment does not take an extra dma-buf file reference; this matches the
 *   Linux dma-buf contract where the importer owns the reference separately.
 *
 * Threading:
 *   Exporter-specific attach hooks may sleep.  Callers must not hold locks
 *   that the exporter's attach path can re-enter.
 */
struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf,
    struct device *dev);

static inline void
get_dma_buf(struct dma_buf *dmabuf)
{
	fhold(dmabuf->file);
}

static inline void
dma_buf_put(struct dma_buf *dmabuf)
{
	//kprintf("dma_buf_put: dmabuf=%p, dmabuf->file=%p\n", dmabuf, dmabuf ? dmabuf->file : NULL);
	if (dmabuf == NULL)
		return;

	if (dmabuf->file == NULL)
		return;
	fdrop(dmabuf->file);
}

int dma_buf_fd(struct dma_buf *dmabuf, int flags);

struct dma_buf *dma_buf_get(int fd);

/*
 * dma_buf_detach()
 *
 * Ownership:
 *   Consumes and frees the attachment returned by dma_buf_attach().  It does
 *   not drop the caller's dma-buf reference; the importer must call
 *   dma_buf_put() for any reference it owns.
 *
 * Lifetime:
 *   dmabuf must be the same object used for dma_buf_attach().
 *
 * Threading:
 *   Exporter-specific detach hooks may sleep and must not be called from IRQ.
 */
void dma_buf_detach(struct dma_buf *dmabuf,
    struct dma_buf_attachment *dmabuf_attach);

#endif /* LINUX_DMA_BUF_H */
