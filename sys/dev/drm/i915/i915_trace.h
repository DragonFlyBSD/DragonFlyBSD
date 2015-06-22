/*
 *
 * Copyright (c) 2014-2015 François Tigeot
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _I915_TRACE_H_
#define _I915_TRACE_H_

#include "i915_drv.h"


#define trace_i915_flip_complete(a,b)
#define trace_i915_flip_request(a,b)

#define trace_i915_gem_evict(a,b,c,d)
#define trace_i915_gem_evict_everything(a)

static inline void
trace_i915_gem_object_change_domain(struct drm_i915_gem_object *obj, u32 read, u32 write)
{
}

#define trace_i915_gem_object_clflush(obj)
#define trace_i915_gem_object_create(obj)
#define trace_i915_gem_object_destroy(obj)
#define trace_i915_gem_object_fault(a,b,c,d)
#define trace_i915_gem_object_pread(obj, offset, size)
#define trace_i915_gem_object_pwrite(obj, offset, size)

#define trace_i915_gem_request_add(ring, seqno)
#define trace_i915_gem_request_complete(ring)
#define trace_i915_gem_request_retire(ring, seqno)
#define trace_i915_gem_request_wait_begin(ring, seqno)
#define trace_i915_gem_request_wait_end(ring, seqno)

#define trace_i915_ring_wait_begin(a)
#define trace_i915_ring_wait_end(a)
#define trace_i915_gem_ring_dispatch(a,b,c)
#define trace_i915_gem_ring_flush(a,b,c)
#define trace_i915_gem_ring_sync_to(from, to, seqno)

#define trace_i915_reg_rw(a,b,c,d,trace)

#define trace_intel_gpu_freq_change(a)

#define trace_i915_vma_bind(vma, map_and_fenceable)
#define trace_i915_vma_unbind(vma)

#define trace_i915_gem_evict_vm(vm)

#endif /* _I915_TRACE_H_ */
