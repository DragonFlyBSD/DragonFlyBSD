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

#ifndef _AMDGPU_TRACE_H_
#define _AMDGPU_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#define trace_amdgpu_vm_bo_update(mapping)

static inline bool
trace_amdgpu_vm_bo_mapping_enabled(void) { return false; }
#define trace_amdgpu_vm_bo_mapping(mapping)
#define trace_amdgpu_vm_bo_unmap(bo_va, mapping)
#define trace_amdgpu_vm_copy_ptes(pe, src, count)
#define trace_amdgpu_vm_flush(addr, ring_idx, vm_id)
#define trace_amdgpu_vm_grab_id(vm, ring_idx, job)
#define trace_amdgpu_vm_set_page(pe, addr, count, incr, flags)
#define trace_amdgpu_vm_set_ptes(pe, addr, count, incr, flags)

#define trace_amdgpu_bo_list_set(list, obj)
static inline void
trace_amdgpu_ttm_bo_move(struct amdgpu_bo *bo, u8 new_memtype, u8 old_memtype)
{
}

#define trace_amdgpu_sched_run_job(job)

#define trace_amdgpu_mm_rreg(device, reg, ret)
#define trace_amdgpu_mm_wreg(device, reg, v)

#define trace_amdgpu_cs_bo_status(entries, size)

#define trace_amdgpu_bo_create(bo)
#define trace_amdgpu_bo_move(bo, new_mem_type, old_mem_type)

#endif	/* _AMDGPU_TRACE_H_ */
