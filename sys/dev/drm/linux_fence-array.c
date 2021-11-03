/*
 * Copyright (c) 2019 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dma-fence-array.h>

#define PENDING_ERROR 1


static const char *dma_fence_array_get_driver_name(struct dma_fence *fence)
{
	return "dma_fence_array";
}

static const char *dma_fence_array_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void dma_fence_array_set_pending_error(struct dma_fence_array *array,
					      int error)
{
	/*
	 * Propagate the first error reported by any of our fences, but only
	 * before we ourselves are signaled.
	 */
	if (error)
		cmpxchg(&array->base.error, PENDING_ERROR, error);
}

static void dma_fence_array_clear_pending_error(struct dma_fence_array *array)
{
	/* Clear the error flag if not actually set. */
	cmpxchg(&array->base.error, PENDING_ERROR, 0);
}

static void irq_dma_fence_array_work(struct irq_work *wrk)
{
	struct dma_fence_array *array = container_of(wrk, typeof(*array), work);

	dma_fence_array_clear_pending_error(array);

	dma_fence_signal(&array->base);
	dma_fence_put(&array->base);
}

static void dma_fence_array_cb_func(struct dma_fence *f,
				    struct dma_fence_cb *cb)
{
	struct dma_fence_array_cb *array_cb =
		container_of(cb, struct dma_fence_array_cb, cb);
	struct dma_fence_array *array = array_cb->array;

	dma_fence_array_set_pending_error(array, f->error);

	if (atomic_dec_and_test(&array->num_pending))
		irq_work_queue(&array->work);
	else
		dma_fence_put(&array->base);
}

static bool dma_fence_array_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	struct dma_fence_array_cb *cb = (void *)(&array[1]);
	unsigned i;

	for (i = 0; i < array->num_fences; ++i) {
		cb[i].array = array;
		/*
		 * As we may report that the fence is signaled before all
		 * callbacks are complete, we need to take an additional
		 * reference count on the array so that we do not free it too
		 * early. The core fence handling will only hold the reference
		 * until we signal the array as complete (but that is now
		 * insufficient).
		 */
		dma_fence_get(&array->base);
		if (dma_fence_add_callback(array->fences[i], &cb[i].cb,
					   dma_fence_array_cb_func)) {
			int error = array->fences[i]->error;

			dma_fence_array_set_pending_error(array, error);
			dma_fence_put(&array->base);
			if (atomic_dec_and_test(&array->num_pending)) {
				dma_fence_array_clear_pending_error(array);
				return false;
			}
		}
	}

	return true;
}

static bool dma_fence_array_signaled(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);

	return atomic_read(&array->num_pending) <= 0;
}

static void dma_fence_array_release(struct dma_fence *fence)
{
	struct dma_fence_array *array = to_dma_fence_array(fence);
	unsigned i;

	for (i = 0; i < array->num_fences; ++i)
		dma_fence_put(array->fences[i]);

	kfree(array->fences);
	dma_fence_free(fence);
}

const struct dma_fence_ops dma_fence_array_ops = {
	.get_driver_name = dma_fence_array_get_driver_name,
	.get_timeline_name = dma_fence_array_get_timeline_name,
	.enable_signaling = dma_fence_array_enable_signaling,
	.signaled = dma_fence_array_signaled,
	.wait = dma_fence_default_wait,
	.release = dma_fence_array_release,
};
EXPORT_SYMBOL(dma_fence_array_ops);

/**
 * dma_fence_array_create - Create a custom fence array
 * @num_fences:		[in]	number of fences to add in the array
 * @fences:		[in]	array containing the fences
 * @context:		[in]	fence context to use
 * @seqno:		[in]	sequence number to use
 * @signal_on_any:	[in]	signal on any fence in the array
 *
 * Allocate a dma_fence_array object and initialize the base fence with
 * dma_fence_init().
 * In case of error it returns NULL.
 *
 * The caller should allocate the fences array with num_fences size
 * and fill it with the fences it wants to add to the object. Ownership of this
 * array is taken and dma_fence_put() is used on each fence on release.
 *
 * If @signal_on_any is true the fence array signals if any fence in the array
 * signals, otherwise it signals when all fences in the array signal.
 */
struct dma_fence_array *dma_fence_array_create(int num_fences,
					       struct dma_fence **fences,
					       u64 context, unsigned seqno,
					       bool signal_on_any)
{
	struct dma_fence_array *array;
	size_t size = sizeof(*array);

	/* Allocate the callback structures behind the array. */
	size += num_fences * sizeof(struct dma_fence_array_cb);
	array = kzalloc(size, GFP_KERNEL);
	if (!array)
		return NULL;

	lockinit(&array->lock, "ldmbfal", 0, 0);
	dma_fence_init(&array->base, &dma_fence_array_ops, &array->lock,
		       context, seqno);
	init_irq_work(&array->work, irq_dma_fence_array_work);

	array->num_fences = num_fences;
	atomic_set(&array->num_pending, signal_on_any ? 1 : num_fences);
	array->fences = fences;

	array->base.error = PENDING_ERROR;

	return array;
}
EXPORT_SYMBOL(dma_fence_array_create);
