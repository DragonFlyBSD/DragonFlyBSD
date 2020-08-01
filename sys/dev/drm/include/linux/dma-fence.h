/*
 * Copyright (c) 2019 Jonathan Gray <jsg@openbsd.org>
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _LINUX_DMA_FENCE_H_
#define _LINUX_DMA_FENCE_H_

#include <linux/err.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>

#define DMA_FENCE_TRACE(fence, fmt, args...) do {} while(0)

struct dma_fence_cb;

struct dma_fence {
	struct kref refcount;
	struct lock *lock;
	const struct dma_fence_ops *ops;
	struct rcu_head rcu;
	struct list_head cb_list;
	u64 context;
	unsigned seqno;
	unsigned long flags;
	ktime_t timestamp;
	int error;
};

enum dma_fence_flag_bits {
	DMA_FENCE_FLAG_SIGNALED_BIT,
	DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	DMA_FENCE_FLAG_USER_BITS, /* must always be last member */
};

typedef void (*dma_fence_func_t)(struct dma_fence *fence,
				 struct dma_fence_cb *cb);

struct dma_fence_cb {
	struct list_head node;
	dma_fence_func_t func;
};

struct dma_fence_ops {
	const char * (*get_driver_name)(struct dma_fence *fence);
	const char * (*get_timeline_name)(struct dma_fence *fence);
	bool (*enable_signaling)(struct dma_fence *fence);
	bool (*signaled)(struct dma_fence *fence);
	signed long (*wait)(struct dma_fence *fence,
			    bool intr, signed long timeout);
	void (*release)(struct dma_fence *fence);

	int (*fill_driver_data)(struct dma_fence *fence, void *data, int size);
	void (*fence_value_str)(struct dma_fence *fence, char *str, int size);
	void (*timeline_value_str)(struct dma_fence *fence,
				   char *str, int size);
};

void dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
		    spinlock_t *lock, u64 context, unsigned seqno);

void dma_fence_release(struct kref *kref);

static inline struct dma_fence *
dma_fence_get(struct dma_fence *fence)
{
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

static inline struct dma_fence *
dma_fence_get_rcu(struct dma_fence *fence)
{
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

static inline void
dma_fence_put(struct dma_fence *fence)
{
	if (fence)
		kref_put(&fence->refcount, dma_fence_release);
}

int dma_fence_signal(struct dma_fence *fence);
int dma_fence_signal_locked(struct dma_fence *fence);

static inline bool
dma_fence_is_signaled(struct dma_fence *fence)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return true;

	if (fence->ops->signaled && fence->ops->signaled(fence)) {
		dma_fence_signal(fence);
		return true;
	}

	return false;
}

void dma_fence_enable_sw_signaling(struct dma_fence *fence);

signed long dma_fence_default_wait(struct dma_fence *fence,
				   bool intr, signed long timeout);
signed long dma_fence_wait_timeout(struct dma_fence *,
				   bool intr, signed long timeout);

static inline signed long
dma_fence_wait(struct dma_fence *fence, bool intr)
{
	signed long ret;

	ret = dma_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);

	if (ret < 0)
		return ret;

	return 0;
}

int dma_fence_add_callback(struct dma_fence *fence,
			   struct dma_fence_cb *cb,
			   dma_fence_func_t func);

bool dma_fence_remove_callback(struct dma_fence *fence,
			       struct dma_fence_cb *cb);

u64 dma_fence_context_alloc(unsigned num);

static inline bool
dma_fence_is_later(struct dma_fence *a, struct dma_fence *b)
{
	return (a->seqno > b->seqno);
}

static inline void
dma_fence_set_error(struct dma_fence *fence, int error)
{
	fence->error = error;
}

static inline struct dma_fence *
dma_fence_get_rcu_safe(struct dma_fence **dfp)
{
	struct dma_fence *fence;
	if (dfp == NULL)
		return NULL;
	fence = *dfp;
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

void dma_fence_free(struct dma_fence *fence);

#endif	/* _LINUX_DMA_FENCE_H_ */
