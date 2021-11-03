/*
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _LINUX_DMA_FENCE_ARRAY_H_
#define _LINUX_DMA_FENCE_ARRAY_H_

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

/**
 * struct dma_fence_array - fence to represent an array of fences
 * @base: fence base class
 * @lock: spinlock for fence handling
 * @num_fences: number of fences in the array
 * @num_pending: fences in the array still pending
 * @fences: array of the fences
 * @work: internal irq_work function
 */
struct dma_fence_array {
        struct dma_fence base;

        spinlock_t lock;
        unsigned num_fences;
        atomic_t num_pending;
        struct dma_fence **fences;

        struct irq_work work;
};

extern const struct dma_fence_ops dma_fence_array_ops;

/**
 * struct dma_fence_array_cb - callback helper for fence array
 * @cb: fence callback structure for signaling
 * @array: reference to the parent fence array object
 */
struct dma_fence_array_cb {
        struct dma_fence_cb cb;
        struct dma_fence_array *array;
};

/**
 * dma_fence_is_array - check if a fence is from the array subsclass
 * @fence: fence to test
 *
 * Return true if it is a dma_fence_array and false otherwise.
 */
static inline bool dma_fence_is_array(struct dma_fence *fence)
{
        return fence->ops == &dma_fence_array_ops;
}

/**
 * to_dma_fence_array - cast a fence to a dma_fence_array
 * @fence: fence to cast to a dma_fence_array
 *
 * Returns NULL if the fence is not a dma_fence_array,
 * or the dma_fence_array otherwise.
 */
static inline struct dma_fence_array *
to_dma_fence_array(struct dma_fence *fence)
{
        if (fence->ops != &dma_fence_array_ops)
                return NULL;

        return container_of(fence, struct dma_fence_array, base);
}

struct dma_fence_array *dma_fence_array_create(int num_fences,
                                               struct dma_fence **fences,
                                               u64 context, unsigned seqno,
                                               bool signal_on_any);

#endif	/* _LINUX_DMA_FENCE_ARRAY_H_ */
