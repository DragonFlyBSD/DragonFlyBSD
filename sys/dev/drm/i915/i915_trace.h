/*
 *
 * Copyright (c) 2014-2020 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/types.h>

#include <drm/drmP.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_ringbuffer.h"

#define trace_dma_fence_enable_signal(fence)

#define trace_g4x_wm(a,b)

#define trace_i915_flip_complete(a,b)
#define trace_i915_flip_request(a,b)

#define trace_i915_gem_evict(a,b,c,d)
#define trace_i915_gem_evict_everything(a)

#define trace_i915_gem_object_clflush(obj)
#define trace_i915_gem_object_create(obj)
#define trace_i915_gem_object_destroy(obj)
#define trace_i915_gem_object_fault(a,b,c,d)
#define trace_i915_gem_object_pread(obj, offset, size)
#define trace_i915_gem_object_pwrite(obj, offset, size)

#define trace_i915_gem_request_add(req)
#define trace_i915_gem_request_complete(ring)
#define trace_i915_gem_request_execute(request)
#define trace_i915_gem_request_in(request, n)
#define trace_i915_gem_request_notify(ring)
#define trace_i915_gem_request_out(request)
#define trace_i915_gem_request_queue(request, flags)
#define trace_i915_gem_request_retire(req)
#define trace_i915_gem_request_submit(request)
#define trace_i915_gem_request_wait_begin(req, flags)
#define trace_i915_gem_request_wait_end(req)

#define trace_i915_request_queue(a, b)
#define trace_i915_request_submit(a)
#define trace_i915_request_execute(a)
#define trace_i915_request_retire(a)
#define trace_i915_request_add(a)
#define trace_i915_request_in(a, b)
#define trace_i915_request_out(req)
#define trace_i915_request_wait_begin(a, b)
#define trace_i915_request_wait_end(a)

#define trace_i915_gem_ring_dispatch(a,b)
#define trace_i915_gem_ring_flush(a,b,c)
#define trace_i915_gem_ring_sync_to(to, from)

#define trace_i915_gem_shrink(dev_priv, target, flags)

#define trace_i915_reg_rw(a,b,c,d,trace)

#define trace_intel_cpu_fifo_underrun(a,b)
#define trace_intel_disable_plane(plane, crtc)
#define trace_intel_engine_notify(engine, waiters)
#define trace_intel_gpu_freq_change(a)
#define trace_intel_memory_cxsr(a,b,c)
#define trace_intel_pch_fifo_underrun(a,b)
#define trace_intel_update_plane(plane, crtc)

#define trace_i915_vma_bind(vma, map_and_fenceable)
#define trace_i915_vma_unbind(vma)

#define trace_i915_gem_evict_vm(vm)
#define trace_i915_gem_evict_node(vm, target, flags)

#define trace_i915_pipe_update_start(crtc)
#define trace_i915_pipe_update_vblank_evaded(crtc)
#define trace_i915_pipe_update_end(crtc, end_vbl_count, scanline_end)

#define trace_i915_context_create(ctx)
#define trace_i915_context_free(ctx)

#define trace_switch_mm(ring, to)

#define trace_i915_ppgtt_create(base)
#define trace_i915_ppgtt_release(base)

#define trace_i915_page_table_entry_alloc(a,b,c,d)
#define trace_i915_page_table_entry_map(a,b,c,d,e,f)

#define trace_i915_va_alloc(a)

#define trace_i915_page_directory_entry_alloc(vm, pdpe, start, shift)
#define trace_i915_page_directory_pointer_entry_alloc(vm, pdpe, start, shift)

#define trace_vlv_wm(a,b)
#define trace_vlv_fifo_size(a,b,c,d)

#endif /* _I915_TRACE_H_ */
