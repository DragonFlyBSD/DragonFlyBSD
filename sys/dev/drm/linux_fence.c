/*
 * Copyright (c) 2019-2020 Jonathan Gray <jsg@openbsd.org>
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

#include <linux/slab.h>
#include <linux/dma-fence.h>

void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
    spinlock_t *lock, u64 context, unsigned seqno)
{
	fence->ops = ops;
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0;
	fence->error = 0;
	kref_init(&fence->refcount);
	INIT_LIST_HEAD(&fence->cb_list);
}

void
dma_fence_release(struct kref *ref)
{
	struct dma_fence *fence = container_of(ref, struct dma_fence, refcount);

	if (fence->ops && fence->ops->release)
		fence->ops->release(fence);
	else
		kfree(fence);
}

long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, long timeout)
{
	if (timeout < 0)
		return -EINVAL;

	if (fence->ops->wait)
		return fence->ops->wait(fence, intr, timeout);
	else
		return dma_fence_default_wait(fence, intr, timeout);
}

static atomic64_t drm_fence_context_count = ATOMIC_INIT(1);

u64
dma_fence_context_alloc(unsigned num)
{
	return atomic64_add_return(num, &drm_fence_context_count) - num;
}

struct default_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void
dma_fence_default_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct default_wait_cb *wait =
		container_of(cb, struct default_wait_cb, base);

	wake_up_process(wait->task);
}

long
dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	long ret = timeout ? timeout : 1;
	unsigned long end;
	int err;
	struct default_wait_cb cb;
	bool was_set;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return ret;

	lockmgr(fence->lock, LK_EXCLUSIVE);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	    &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

	if (!was_set && fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			goto out;
		}
	}

	if (timeout == 0) {
		ret = 0;
		goto out;
	}

	cb.base.func = dma_fence_default_wait_cb;
	cb.task = current;
	list_add(&cb.base.node, &fence->cb_list);

	end = jiffies + timeout;
	for (ret = timeout; ret > 0; ret = MAX(0, end - jiffies)) {
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			break;
		}
		if (intr) {
			__set_current_state(TASK_INTERRUPTIBLE);
		}
		else {
			__set_current_state(TASK_UNINTERRUPTIBLE);
		}
		/* wake_up_process() directly uses task_struct pointers as sleep identifiers */
		err = lksleep(current, fence->lock, intr ? PCATCH : 0, "dmafence", ret);
		if (err == EINTR || err == ERESTART) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
	__set_current_state(TASK_RUNNING);
out:
	lockmgr(fence->lock, LK_RELEASE);
	return ret;
}

static bool
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count,
    uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

long
dma_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
    bool intr, long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	long ret = timeout;
	unsigned long end;
	int i, err;

	if (timeout == 0) {
		for (i = 0; i < count; i++) {
			if (dma_fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return 1;
			}
		}
		return 0;
	}

	cb = kcalloc(count, sizeof(struct default_wait_cb), GFP_KERNEL);
	if (cb == NULL)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct dma_fence *fence = fences[i];
		cb[i].task = current;
		if (dma_fence_add_callback(fence, &cb[i].base,
		    dma_fence_default_wait_cb)) {
			if (idx)
				*idx = i;
			goto cb_cleanup;
		}
	}

	end = jiffies + timeout;
	for (ret = timeout; ret > 0; ret = MAX(0, end - jiffies)) {
		if (dma_fence_test_signaled_any(fences, count, idx))
			break;
		err = tsleep(current, intr ? PCATCH : 0, "dfwat", ret);
		if (err == EINTR || err == ERESTART) {
			ret = -ERESTARTSYS;
			break;
		}
	}

cb_cleanup:
	while (i-- > 0)
		dma_fence_remove_callback(fences[i], &cb[i].base);
	kfree(cb);
	return ret;
}

int
dma_fence_signal_locked(struct dma_fence *fence)
{
	struct dma_fence_cb *cur, *tmp;
	struct list_head cb_list;

	if (fence == NULL)
		return -EINVAL;

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return -EINVAL;

	list_replace(&fence->cb_list, &cb_list);

	fence->timestamp = ktime_get();
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);

	list_for_each_entry_safe(cur, tmp, &cb_list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}

	return 0;
}

int
dma_fence_signal(struct dma_fence *fence)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	lockmgr(fence->lock, LK_EXCLUSIVE);
	r = dma_fence_signal_locked(fence);
	lockmgr(fence->lock, LK_RELEASE);

	return r;
}

void
dma_fence_enable_sw_signaling(struct dma_fence *fence)
{
	if (!test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags) &&
	    !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) &&
	    fence->ops->enable_signaling) {
		lockmgr(fence->lock, LK_EXCLUSIVE);
		if (!fence->ops->enable_signaling(fence))
			dma_fence_signal_locked(fence);
		lockmgr(fence->lock, LK_RELEASE);
	}
}

int
dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb,
    dma_fence_func_t func)
{
	int ret = 0;
	bool was_set;

	if (WARN_ON(!fence || !func))
		return -EINVAL;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		INIT_LIST_HEAD(&cb->node);
		return -ENOENT;
	}

	lockmgr(fence->lock, LK_EXCLUSIVE);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		ret = -ENOENT;
	else if (!was_set && fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			ret = -ENOENT;
		}
	}

	if (!ret) {
		cb->func = func;
		list_add_tail(&cb->node, &fence->cb_list);
	} else
		INIT_LIST_HEAD(&cb->node);
	lockmgr(fence->lock, LK_RELEASE);

	return ret;
}

bool
dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	bool ret;

	lockmgr(fence->lock, LK_EXCLUSIVE);

	ret = !list_empty(&cb->node);
	if (ret)
		list_del_init(&cb->node);

	lockmgr(fence->lock, LK_RELEASE);

	return ret;
}

void
dma_fence_free(struct dma_fence *fence)
{
	kfree(fence);
}
