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

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/dma-fence.h>
#include <linux/wait.h>

#include <linux/slab.h>

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
	int (*attach)(struct dma_buf *, struct device *, struct dma_buf_attachment *);
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

static inline struct dma_buf_attachment *
dma_buf_attach(struct dma_buf *dmabuf, struct device *dev)
{
	/* XXX: this function is a stub */
	struct dma_buf_attachment *attach;

	attach = kmalloc(sizeof(struct dma_buf_attachment), M_DRM, M_WAITOK | M_ZERO);

	return attach;
}

static inline void
get_dma_buf(struct dma_buf *dmabuf)
{
	fhold(dmabuf->file);
}

static inline void
dma_buf_put(struct dma_buf *dmabuf)
{
	if (dmabuf == NULL)
		return;

	if (dmabuf->file == NULL)
		return;

	fdrop(dmabuf->file);
}

int dma_buf_fd(struct dma_buf *dmabuf, int flags);

struct dma_buf *dma_buf_get(int fd);

static inline void
dma_buf_detach(struct dma_buf *dmabuf,
	       struct dma_buf_attachment *dmabuf_attach)
{
	kprintf("dma_buf_detach: Not implemented\n");
}

#endif /* LINUX_DMA_BUF_H */
