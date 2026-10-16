/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Per-process GPU virtual address mappings and VM_BIND futures.
 */

#include "nvgpu_bo.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_fence.h"
#include "nvgpu_future.h"
#include "nvgpu_proc.h"
#include "nvgpu_vm.h"
#include "nvgsp_vmm.h"

#include <linux/dma-fence.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <machine/atomic.h>

static MALLOC_DEFINE(M_NVGPU_VM, "nvgpu_vm", "nvgpu process VM");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, vm_bind_spawn, 27,
    "vm bind spawn proc=%p vm=%p op_count=%u wait_count=%u", void *proc,
    void *vm, uint32_t op_count, uint32_t wait_count);
KTR_INFO(KTR_NVGPU, nvgpu, vm_bind_poll, 27,
    "vm bind poll future=%p phase=%u op_count=%u error=%d", void *future,
    u_int phase, uint32_t op_count, int error);
KTR_INFO(KTR_NVGPU, nvgpu, vm_bind_dirty, 27,
    "vm bind dirty future=%p dirty=%u", void *future, u_int dirty);

#define NVGPU_VM_PAGE_SHIFT_4K	12
#define NVGPU_VM_PAGE_SHIFT_64K	16
#define NVGPU_VM_PAGE_SHIFT_2M	21
#define NVGPU_VM_PAGE_SIZE_4K	(1ULL << NVGPU_VM_PAGE_SHIFT_4K)
#define NVGPU_VM_PAGE_SIZE_64K	(1ULL << NVGPU_VM_PAGE_SHIFT_64K)
#define NVGPU_VM_PAGE_SIZE_2M	(1ULL << NVGPU_VM_PAGE_SHIFT_2M)
#define NVGPU_VM_BIND_SIZE_BUCKET_COUNT	16

enum nvgpu_vm_trace_action {
	NVGPU_VM_TRACE_MAP = 1,
	NVGPU_VM_TRACE_UNMAP,
	NVGPU_VM_TRACE_MAP_NULL,
	NVGPU_VM_TRACE_MAP_SPARSE,
	NVGPU_VM_TRACE_UNMAP_SPARSE,
};

enum nvgpu_vm_bind_reject_reason {
	NVGPU_VM_BIND_REJECT_VA_ALIGN,
	NVGPU_VM_BIND_REJECT_BO_OFFSET_ALIGN,
	NVGPU_VM_BIND_REJECT_PADDR_ALIGN,
	NVGPU_VM_BIND_REJECT_RANGE_ALIGN,
	NVGPU_VM_BIND_REJECT_CAPABILITY_GATE,
};

enum nvgpu_vm_bind_page_bucket {
	NVGPU_VM_BIND_PAGE_SHIFT_4K,
	NVGPU_VM_BIND_PAGE_SHIFT_64K,
	NVGPU_VM_BIND_PAGE_SHIFT_2M,
};

enum nvgpu_vm_bind_domain_bucket {
	NVGPU_VM_BIND_DOMAIN_VRAM,
	NVGPU_VM_BIND_DOMAIN_HOST,
};

struct ttm_mem_reg;

struct nvgpu_vm_binding {
	LIST_ENTRY(nvgpu_vm_binding) link;
	LIST_ENTRY(nvgpu_vm_binding) bo_link;
	LIST_ENTRY(nvgpu_vm_binding) validate_link;
	RB_ENTRY(nvgpu_vm_binding) rb_link;
	uint64_t addr;
	uint64_t size;
	uint64_t bo_offset;
	struct nvgpu_vm *owner;
	struct nvgpu_bo *bo;
	uint8_t pte_kind;
	uint8_t page_shift;
	bool pte_installed;
	bool bo_pinned;
	bool bo_no_evict_pinned;
	bool bo_linked;
	bool validate_linked;
};

struct nvgpu_vm_bind_segment {
	uint64_t addr;
	uint64_t size;
	uint64_t bo_offset;
	uint64_t paddr;
	vm_paddr_t *sysmem_paddrs;
	uint32_t sysmem_page_count;
	uint8_t page_shift;
};

struct nvgpu_vm_bind_segment_plan {
	struct nvgpu_vm_bind_segment *segments;
	uint32_t count;
	uint32_t capacity;
};

#define NVGPU_VM_DIRTY_RANGE_INLINE	16

struct nvgpu_vm_dirty_range {
	uint64_t start;
	uint64_t end;
};

struct nvgpu_vm_dirty_set {
	struct nvgpu_vm_dirty_range ranges[NVGPU_VM_DIRTY_RANGE_INLINE];
	uint64_t first;
	uint64_t last;
	uint64_t range_count;
	uint64_t pages;
	uint32_t merged_count;
	bool dirty;
	bool overflow;
};

struct nvgpu_vm_bind_2m_split {
	uint64_t addr;
	struct nvgsp_vmm_user_pt *pt;
	bool committed;
};

struct nvgpu_vm_bind_2m_split_plan {
	struct nvgpu_vm_bind_2m_split *splits;
	uint32_t count;
	uint32_t capacity;
};
LIST_HEAD(nvgpu_vm_binding_list, nvgpu_vm_binding);

struct nvgpu_vm_materialize_entry {
	struct nvgpu_vm_binding *binding;
	struct nvgpu_vm_bind_segment_plan segments;
	struct nvgpu_vm_bind_2m_split_plan split_plan;
	struct nvgpu_vm_binding_list new_bindings;
	bool is_2m;
	bool committed;
};

struct nvgpu_vm_materialize_plan {
	struct nvgpu_vm_materialize_entry *entries;
	uint32_t count;
};

struct nvgpu_vm_remove_plan {
	struct nvgpu_vm_binding_list tail_bindings;
	struct nvgpu_vm_materialize_plan materialize_plan;
	uint64_t addr;
	uint64_t size;
	bool clear_empty_range;
	bool preserve_target_pts;
	uint8_t preserve_page_shift;
	bool materialize_full_cover;
	uint32_t clear_action;
};

struct nvgpu_vm_clear_plan {
	struct nvgpu_vm_remove_plan remove_plan;
	struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan;
	uint64_t addr;
	uint64_t size;
	uint32_t action;
};

struct nvgpu_vm_segment_remove_plan {
	struct nvgpu_vm_binding_list tail_bindings;
	struct nvgpu_vm_materialize_plan materialize_plan;
	const struct nvgpu_vm_bind_segment *segments;
	uint32_t segment_count;
	uint64_t addr;
	uint64_t size;
	uint32_t clear_action;
};

struct nvgpu_vm_valid_map_plan {
	struct nvgpu_vm_binding_list new_bindings;
	struct nvgpu_vm_binding_list replace_tails;
	struct nvgpu_vm_materialize_plan materialize_plan;
	struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan;
	bool committed;
};

struct nvgpu_vm_sparse_map_plan {
	struct nvgpu_vm_bind_segment_plan segment_plan;
	struct nvgpu_vm_segment_remove_plan target_clear_plan;
	struct nvgsp_vmm_sparse_region **sparse_regions;
	struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan;
	uint64_t addr;
	uint64_t size;
};
RB_HEAD(nvgpu_vm_binding_tree, nvgpu_vm_binding);

struct nvgpu_vm {
	struct nvgpu_device *gpu;
	struct nvgsp_vmm *backend;
	struct lwkt_token vm_token;
	struct nvgpu_vm_binding_list vm_bindings;
	struct nvgpu_vm_binding_list vm_validate_bindings;
	struct nvgpu_vm_binding_tree vm_binding_tree;
	uint32_t client_handle;
	size_t kernel_managed_addr;
	size_t kernel_managed_size;
	uint64_t vm_bindings_max_end;
};

static int
nvgpu_vm_binding_tree_cmp(struct nvgpu_vm_binding *a,
    struct nvgpu_vm_binding *b)
{
	if (a->addr < b->addr)
		return (-1);
	if (a->addr > b->addr)
		return (1);
	return (0);
}
RB_PROTOTYPE_STATIC(nvgpu_vm_binding_tree, nvgpu_vm_binding,
    rb_link, nvgpu_vm_binding_tree_cmp);
RB_GENERATE_STATIC(nvgpu_vm_binding_tree, nvgpu_vm_binding,
    rb_link, nvgpu_vm_binding_tree_cmp);

static void nvgpu_vm_binding_insert_sorted(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding);
static int nvgpu_vm_binding_pin(struct nvgpu_vm_binding *binding);
static void nvgpu_vm_binding_free(struct nvgpu_vm_binding *binding);
static void nvgpu_vm_binding_unlink_retire(
    struct nvgpu_vm_binding *binding,
    struct nvgpu_vm_binding_list *retired_bindings);
static void nvgpu_vm_binding_validate_clear(
    struct nvgpu_vm_binding *binding);
static void nvgpu_vm_binding_validate_mark(
    struct nvgpu_vm_binding *binding);
static void nvgpu_vm_bindings_free_prepared(
    struct nvgpu_vm_binding_list *bindings);
static void nvgpu_vm_bind_note_map_shape(struct nvgpu_device *gpu,
	    uint32_t flags, uint64_t size);
static void nvgpu_vm_bind_note_clear_shape(struct nvgpu_device *gpu,
	    const struct nvgpu_bo *bo, uint8_t pte_kind, uint8_t page_shift,
	    uint64_t size);
static int nvgpu_vm_bind_segment_check_writer_args(
    const struct nvgpu_vm_bind_segment *segment, const struct nvgpu_bo *bo);

/*
 * nvgpu_vm_range_end()
 *
 * Ownership:
 *   Borrows scalar VA range arguments and writes the exclusive end to the
 *   caller-owned output slot.
 *
 * Lifetime:
 *   The returned end is a pure value.  A false result means the range is empty
 *   or not representable as [addr, addr + size).
 *
 * Threading:
 *   Pure arithmetic helper; callers own any VM serialization needed for the
 *   bos whose ranges they are checking.
 */
static bool
nvgpu_vm_range_end(uint64_t addr, uint64_t size, uint64_t *end)
{
	if (size == 0 || addr > UINT64_MAX - size)
		return (false);
	*end = addr + size;
	return (true);
}

static bool
nvgpu_vm_ranges_overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs)
{
	uint64_t ae, be;

	if (as == 0 || bs == 0)
		return (false);
	if (!nvgpu_vm_range_end(a, as, &ae) ||
	    !nvgpu_vm_range_end(b, bs, &be))
		return (true);
	return (a < be && b < ae);
}

static bool
nvgpu_vm_gpu_va_fits_binding(const struct nvgpu_vm_binding *binding,
    uint64_t va, uint64_t size, uint64_t *binding_offset)
{
	uint64_t offset;

	if (va < binding->addr)
		return (false);
	offset = va - binding->addr;
	if (offset > binding->size || size > binding->size - offset)
		return (false);
	*binding_offset = offset;
	return (true);
}

static void
nvgpu_vm_binding_assert(const struct nvgpu_vm_binding *binding)
{
	KASSERT(binding->owner != NULL,
	    ("nvgpu vm: VM binding without owner"));
	KASSERT(binding->bo != NULL,
	    ("nvgpu vm: VM binding without GEM object"));
	KASSERT(binding->page_shift == NVGPU_VM_PAGE_SHIFT_4K ||
	    binding->page_shift == NVGPU_VM_PAGE_SHIFT_64K ||
	    binding->page_shift == NVGPU_VM_PAGE_SHIFT_2M,
	    ("nvgpu vm: VM binding with invalid page shift"));
}

static struct nvgpu_vm_binding *
nvgpu_vm_binding_alloc(struct nvgpu_vm *vm, uint64_t addr,
    uint64_t size, struct nvgpu_bo *bo, uint64_t bo_offset,
    uint8_t pte_kind, uint8_t page_shift)
{
	struct nvgpu_vm_binding *binding;

	binding = kmalloc(sizeof(*binding), M_NVGPU_VM, M_WAITOK | M_ZERO);
	if (binding == NULL)
		return (NULL);

	binding->addr = addr;
	binding->size = size;
	binding->bo_offset = bo_offset;
	binding->owner = vm;
	binding->bo = bo;
	binding->pte_kind = pte_kind;
	binding->page_shift = page_shift;
	binding->pte_installed = true;
	return (binding);
}

/*
 * nvgpu_vm_binding_tree_lower_bound()
 *
 * Ownership:
 *   Borrows vm and returns a borrowed live binding pointer.  It does not
 *   acquire or release GEM, BO, or VMM ownership.
 *
 * Lifetime:
 *   The returned binding is valid only while the caller keeps the VM token and
 *   does not unlink the binding from vm's live tracker.
 *
 * Threading:
 *   Requires vm->vm_token.  The rb-tree is a VM-local lookup index and has
 *   no independent locking.
 */
static struct nvgpu_vm_binding *
nvgpu_vm_binding_tree_lower_bound(struct nvgpu_vm *vm,
    uint64_t addr)
{
	struct nvgpu_vm_binding *binding;
	struct nvgpu_vm_binding *best = NULL;

	binding = RB_ROOT(&vm->vm_binding_tree);
	while (binding != NULL) {
		if (binding->addr < addr) {
			binding = RB_RIGHT(binding, rb_link);
		} else {
			best = binding;
			binding = RB_LEFT(binding, rb_link);
		}
	}
	return (best);
}

/*
 * nvgpu_vm_binding_first_overlap()
 *
 * Ownership:
 *   Borrows vm and returns a borrowed live binding pointer.  The caller
 *   keeps ownership of the query range.
 *
 * Lifetime:
 *   The pointer is stable only while the caller holds vm->vm_token and does
 *   not unlink that binding.  Use next_overlap before mutating the current
 *   binding in a destructive walk.
 *
 * Threading:
 *   Requires vm->vm_token.  This is the VA interval-manager lookup path for
 *   VM_BIND remap planning and replaces whole-list overlap scans.
 */
static struct nvgpu_vm_binding *
nvgpu_vm_binding_first_overlap(struct nvgpu_vm *vm,
    uint64_t addr, uint64_t size)
{
	struct nvgpu_vm_binding *binding, *prev;
	uint64_t end;

	if (size == 0 || addr >= vm->vm_bindings_max_end)
		return (NULL);
	if (!nvgpu_vm_range_end(addr, size, &end))
		end = UINT64_MAX;

	binding = nvgpu_vm_binding_tree_lower_bound(vm, addr);
	if (binding != NULL)
		prev = nvgpu_vm_binding_tree_RB_PREV(binding);
	else
		prev = nvgpu_vm_binding_tree_RB_MINMAX(
		    &vm->vm_binding_tree, 1);

	if (prev != NULL &&
	    nvgpu_vm_ranges_overlap(addr, size, prev->addr, prev->size))
		return (prev);

	while (binding != NULL && binding->addr < end) {
		if (nvgpu_vm_ranges_overlap(addr, size, binding->addr,
		    binding->size))
			return (binding);
		binding = nvgpu_vm_binding_tree_RB_NEXT(binding);
	}
	return (NULL);
}

static struct nvgpu_vm_binding *
nvgpu_vm_binding_next_overlap(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding, uint64_t addr, uint64_t size)
{
	uint64_t end;

	if (!nvgpu_vm_range_end(addr, size, &end))
		end = UINT64_MAX;
	binding = nvgpu_vm_binding_tree_RB_NEXT(binding);
	while (binding != NULL && binding->addr < end) {
		if (nvgpu_vm_ranges_overlap(addr, size, binding->addr,
		    binding->size))
			return (binding);
		binding = nvgpu_vm_binding_tree_RB_NEXT(binding);
	}
	return (NULL);
}

static void
nvgpu_vm_binding_tree_insert(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding)
{
	struct nvgpu_vm_binding *conflict;

	conflict = nvgpu_vm_binding_first_overlap(vm, binding->addr,
	    binding->size);
	KASSERT(conflict == NULL,
	    ("nvgpu vm: overlapping VM binding insert addr=0x%016jx size=0x%016jx old=0x%016jx+0x%016jx",
	    (uintmax_t)binding->addr, (uintmax_t)binding->size,
	    (uintmax_t)conflict->addr, (uintmax_t)conflict->size));
	conflict = nvgpu_vm_binding_tree_RB_INSERT(
	    &vm->vm_binding_tree, binding);
	KASSERT(conflict == NULL,
	    ("nvgpu vm: duplicate VM binding insert addr=0x%016jx",
	    (uintmax_t)binding->addr));
}

static void
nvgpu_vm_binding_tree_remove(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding)
{
	struct nvgpu_vm_binding *removed;

	removed = nvgpu_vm_binding_tree_RB_REMOVE(
	    &vm->vm_binding_tree, binding);
	KASSERT(removed == binding,
	    ("nvgpu vm: VM binding tree remove missed addr=0x%016jx",
	    (uintmax_t)binding->addr));
}

/*
 * nvgpu_vm_binding_bo_attach()
 *
 * Ownership:
 *   Borrows one live VM binding and links it into the owning BO's reverse GPUVA
 *   list.  The binding's existing GEM reference and VM_BIND pin keep the BO
 *   alive and immobile; this helper does not take another reference.
 *
 * Lifetime:
 *   The BO reverse link exists only while the binding is present in the live
 *   per-file VM tracker.  Retired bindings keep their GEM/pin ownership until
 *   the done fence is visible, but they are no longer live GPUVA mappings and
 *   must already be detached from this list.
 *
 * Threading:
 *   Called while vm->vm_token serializes the VM tracker.  The BO-local
 *   token serializes the reverse list across different drm_file VMs.
 */
static void
nvgpu_vm_binding_bo_attach(struct nvgpu_vm_binding *binding)
{
	struct nvgpu_bo *bo = binding->bo;

	KASSERT(!binding->bo_linked,
	    ("nvgpu vm: double BO reverse attach addr=0x%016jx",
	    (uintmax_t)binding->addr));
	lwkt_gettoken(&bo->vm_mapping_token);
	LIST_INSERT_HEAD(&bo->vm_mappings, binding, bo_link);
	bo->vm_mapping_count++;
	binding->bo_linked = true;
	lwkt_reltoken(&bo->vm_mapping_token);

}

/*
 * nvgpu_vm_binding_bo_detach()
 *
 * Ownership:
 *   Removes a live VM binding from the owning BO's reverse GPUVA list.  GEM
 *   reference and VM_BIND pin ownership stay with the binding and are released
 *   later by nvgpu_vm_binding_free().
 *
 * Lifetime:
 *   After detach, the binding may move to a retired list or be freed.  It must
 *   not be used for BO reverse lookup again unless it is reinserted as a live
 *   mapping.
 *
 * Threading:
 *   Called under vm->vm_token for the VM-side mutation and under the BO
 *   token for the BO-side list mutation.
 */
static void
nvgpu_vm_binding_bo_detach(struct nvgpu_vm_binding *binding)
{
	struct nvgpu_bo *bo = binding->bo;

	if (!binding->bo_linked)
		return;

	nvgpu_vm_binding_validate_clear(binding);
	lwkt_gettoken(&bo->vm_mapping_token);
	KASSERT(bo->vm_mapping_count > 0,
	    ("nvgpu vm: BO reverse mapping count underflow"));
	LIST_REMOVE(binding, bo_link);
	bo->vm_mapping_count--;
	binding->bo_linked = false;
	lwkt_reltoken(&bo->vm_mapping_token);

}

/*
 * nvgpu_vm_binding_validate_mark()
 *
 * Ownership:
 *   Borrows one live VM binding and links it into its drm_file validate list.
 *   The binding keeps owning its GEM reference and VM_BIND pin; this helper
 *   takes no additional TTM or GEM ownership.
 *
 * Lifetime:
 *   The mark exists only while the binding is live in the VM tracker.  It is
 *   cleared when TTM validates the BO back to preferred placement or when the
 *   mapping leaves the live tracker.
 *
 * Threading:
 *   Called while the owning vm->vm_token serializes VM state.  It never
 *   takes a TTM reservation lock and never sleeps.
 */
static void
nvgpu_vm_binding_validate_mark(struct nvgpu_vm_binding *binding)
{
	return;
}

/*
 * nvgpu_vm_binding_validate_clear()
 *
 * Ownership:
 *   Borrows one VM binding and removes any validate-list membership created
 *   by nvgpu_vm_binding_validate_mark().  It does not release the binding's
 *   GEM reference or VM_BIND pin.
 *
 * Lifetime:
 *   May be called repeatedly; only the first call after a mark mutates the
 *   list.  The binding must still be allocated.
 *
 * Threading:
 *   Called under vm->vm_token or from paths that still exclusively own a
 *   detached binding.  It does not sleep and does not touch TTM state.
 */
static void
nvgpu_vm_binding_validate_clear(struct nvgpu_vm_binding *binding)
{
	return;
}
/*
 * nvgpu_vm_binding_rekey_tail()
 *
 * Ownership:
 *   Borrows a live binding and mutates its VA key in place.  GEM/BO ownership
 *   remains with the binding.
 *
 * Lifetime:
 *   The binding stays on the sorted list.  The new address is the old tail
 *   after a split, so list order is preserved; only the rb-tree key changes.
 *
 * Threading:
 *   Requires vm->vm_token and must be used for every live binding addr
 *   mutation so the VA interval index stays isomorphic with the list.
 */
static void
nvgpu_vm_binding_rekey_tail(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding, uint64_t addr, uint64_t size,
    uint64_t bo_offset)
{
	nvgpu_vm_binding_tree_remove(vm, binding);
	binding->addr = addr;
	binding->size = size;
	binding->bo_offset = bo_offset;
	nvgpu_vm_binding_tree_insert(vm, binding);
}

/*
 * nvgpu_vm_bindings_match_map_range()
 *
 * Ownership:
 *   Borrows the live binding tree and the MAP operation's GEM object.  It
 *   consumes no GEM, BO, VMM, or binding ownership.
 *
 * Lifetime:
 *   The returned bool is valid only for the current serialized VM_BIND check.
 *   Borrowed binding pointers are not retained past the call.
 *
 * Threading:
 *   Requires vm->vm_token.  The helper walks the rb-tree interval index but
 *   does not write hardware PTEs or reservation state.
 */
static bool
nvgpu_vm_bindings_match_map_range(struct nvgpu_vm *vm,
    struct nvgpu_bo *bo,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint8_t pte_kind)
{
	struct nvgpu_vm_binding *binding;

	for (uint32_t i = 0; i < segment_count; i++) {
		uint64_t cur = segments[i].addr;
		uint64_t end = segments[i].addr + segments[i].size;

		binding = nvgpu_vm_binding_first_overlap(vm,
		    segments[i].addr, segments[i].size);
		while (cur < end) {
			uint64_t binding_end, covered_end, binding_bo_offset;
			uint64_t target_bo_offset;

			if (binding == NULL || binding->addr > cur)
				return (false);
			if (binding->bo != bo || binding->pte_kind != pte_kind)
				return (false);
			if (binding->page_shift < segments[i].page_shift)
				return (false);

			binding_end = binding->addr + binding->size;
			covered_end = binding_end < end ? binding_end : end;
			binding_bo_offset = binding->bo_offset + cur -
			    binding->addr;
			target_bo_offset = segments[i].bo_offset + cur -
			    segments[i].addr;
			if (binding_bo_offset != target_bo_offset)
				return (false);
			if (covered_end <= cur)
				return (false);

			cur = covered_end;
			if (cur < end)
				binding = nvgpu_vm_binding_next_overlap(vm,
				    binding, segments[i].addr, segments[i].size);
		}
	}
	return (true);
}

/*
 * nvgpu_vm_bindings_match_exact_map_op()
 *
 * Ownership:
 *   Borrows the live VM binding tree and the MAP operation's GEM object.  It
 *   consumes no GEM, BO, VMM, or binding ownership and never prepares segment
 *   snapshots.
 *
 * Lifetime:
 *   The result is valid only while the caller keeps VM_BIND serialization and
 *   does not mutate the live binding tree.  Covered bindings remain borrowed
 *   and are not retained.
 *
 * Threading:
 *   Requires vm->vm_token.  This is the prepare-stage fast exact-noop
 *   predicate used before BO pin/populate work; sparse-region absence must be
 *   checked separately so valid MAP over sparse reservation still runs the
 *   sparse-clear plan.
 */
static bool
nvgpu_vm_bindings_match_exact_map_op(struct nvgpu_vm *vm,
    struct nvgpu_bo *bo, uint64_t addr, uint64_t size,
    uint64_t bo_offset, uint8_t pte_kind)
{
	struct nvgpu_vm_binding *binding;
	uint64_t cur, end;

	if (!nvgpu_vm_range_end(addr, size, &end))
		return (false);
	cur = addr;
	binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	while (cur < end) {
		uint64_t binding_end, covered_end, binding_delta;
		uint64_t target_delta, binding_bo_offset, target_bo_offset;

		if (binding == NULL || binding->addr > cur)
			return (false);
		if (binding->bo != bo || binding->pte_kind != pte_kind)
			return (false);
		if (!binding->pte_installed || !binding->bo_pinned)
			return (false);
		if (binding->page_shift != NVGPU_VM_PAGE_SHIFT_4K &&
		    binding->page_shift != NVGPU_VM_PAGE_SHIFT_64K &&
		    binding->page_shift != NVGPU_VM_PAGE_SHIFT_2M)
			return (false);
		if (binding->addr > UINT64_MAX - binding->size)
			return (false);

		binding_end = binding->addr + binding->size;
		covered_end = binding_end < end ? binding_end : end;
		if (covered_end <= cur)
			return (false);
		binding_delta = cur - binding->addr;
		target_delta = cur - addr;
		if (binding->bo_offset > UINT64_MAX - binding_delta ||
		    bo_offset > UINT64_MAX - target_delta)
			return (false);
		binding_bo_offset = binding->bo_offset + binding_delta;
		target_bo_offset = bo_offset + target_delta;
		if (binding_bo_offset != target_bo_offset)
			return (false);

		cur = covered_end;
		if (cur < end)
			binding = nvgpu_vm_binding_next_overlap(vm,
			    binding, addr, size);
	}
	return (true);
}

/*
 * nvgpu_vm_bind_segment_plan_*()
 *
 * Ownership:
 *   The plan owns a dynamically sized segment array.  Segments own no GEM,
 *   BO, or VMM references; they are value data for the current MAP op.
 *
 * Lifetime:
 *   The plan must be initialized before use and finalized on every MAP path.
 *   Prepared live bindings copy the segment fields before the plan is freed.
 *
 * Threading:
 *   Runs in VM_BIND prepare while the caller holds normal VM_BIND
 *   serialization.  Allocation may sleep, but no PTEs are written here.
 */
static void
nvgpu_vm_bind_segment_plan_init(
    struct nvgpu_vm_bind_segment_plan *plan)
{
	plan->segments = NULL;
	plan->count = 0;
	plan->capacity = 0;
}

static void
nvgpu_vm_bind_segment_plan_fini(
    struct nvgpu_vm_bind_segment_plan *plan)
{
	for (uint32_t i = 0; i < plan->count; i++)
		if (plan->segments[i].sysmem_paddrs != NULL)
			_kfree(plan->segments[i].sysmem_paddrs, M_NVGPU_VM);
	if (plan->segments != NULL)
		_kfree(plan->segments, M_NVGPU_VM);
	plan->segments = NULL;
	plan->count = 0;
	plan->capacity = 0;
}

static int
nvgpu_vm_bind_segment_plan_reserve(
    struct nvgpu_vm_bind_segment_plan *plan, uint32_t required)
{
	struct nvgpu_vm_bind_segment *segments;
	uint32_t capacity;

	if (required <= plan->capacity)
		return (0);

	capacity = plan->capacity != 0 ? plan->capacity : 4;
	while (capacity < required) {
		if (capacity > UINT32_MAX / 2)
			return (ENOMEM);
		capacity *= 2;
	}

	segments = kmalloc((size_t)capacity * sizeof(*segments), M_NVGPU_VM,
	    M_WAITOK);
	if (segments == NULL)
		return (ENOMEM);
	if (plan->segments != NULL) {
		memcpy(segments, plan->segments,
		    plan->count * sizeof(*segments));
		_kfree(plan->segments, M_NVGPU_VM);
	}
	plan->segments = segments;
	plan->capacity = capacity;
	return (0);
}

static int
nvgpu_vm_bind_segment_add(struct nvgpu_vm_bind_segment_plan *plan,
    uint64_t addr, uint64_t size, uint64_t bo_offset, uint64_t paddr,
    uint8_t page_shift)
{
	int err;

	KASSERT(size != 0, ("nvgpu vm: empty VM_BIND MAP segment"));
	err = nvgpu_vm_bind_segment_plan_reserve(plan, plan->count + 1);
	if (err != 0)
		return (err);

	plan->segments[plan->count].addr = addr;
	plan->segments[plan->count].size = size;
	plan->segments[plan->count].bo_offset = bo_offset;
	plan->segments[plan->count].paddr = paddr;
	plan->segments[plan->count].sysmem_paddrs = NULL;
	plan->segments[plan->count].sysmem_page_count = 0;
	plan->segments[plan->count].page_shift = page_shift;
	plan->count++;
	return (0);
}

/*
 * nvgpu_vm_bind_segment_add_sysmem()
 *
 * Ownership:
 *   Allocates a temporary physical-page snapshot owned by the segment plan.
 *   The live VM binding does not retain this array; it only records VA, BO,
 *   bo_offset, kind, and page_shift after commit succeeds.
 *
 * Lifetime:
 *   The paddr array is valid until nvgpu_vm_bind_segment_plan_fini().  It is
 *   consumed by the prepared-only VMM writer during the same VM_BIND operation.
 *
 * Threading:
 *   Called in VM_BIND prepare while the BO is VM_BIND pinned and, for TTM TT
 *   memory, already populated.  It does not write PTEs and may fail before the
 *   no-fail commit section starts.
 */
static int
nvgpu_vm_bind_segment_add_sysmem(
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t size, uint64_t bo_offset, uint8_t page_shift)
{
	vm_paddr_t *paddrs;
	uint64_t page_size;
	uint64_t page_count64;
	uint32_t page_count;
	int err;

	KASSERT(size != 0, ("nvgpu vm: empty VM_BIND sysmem segment"));
	if (page_shift == NVGPU_VM_PAGE_SHIFT_4K)
		page_size = NVGPU_VM_PAGE_SIZE_4K;
	else if (page_shift == NVGPU_VM_PAGE_SHIFT_64K)
		page_size = NVGPU_VM_PAGE_SIZE_64K;
	else if (page_shift == NVGPU_VM_PAGE_SHIFT_2M)
		page_size = NVGPU_VM_PAGE_SIZE_2M;
	else
		return (EINVAL);
	if (((addr | size | bo_offset) & (page_size - 1)) != 0)
		return (EINVAL);
	page_count64 = size / NVGPU_VM_PAGE_SIZE_4K;
	if (page_count64 == 0 || page_count64 > UINT32_MAX)
		return (ENOMEM);
	page_count = (uint32_t)page_count64;

	paddrs = kmalloc((size_t)page_count * sizeof(*paddrs), M_NVGPU_VM,
	    M_WAITOK);
	if (paddrs == NULL)
		return (ENOMEM);

	for (uint32_t i = 0; i < page_count; i++) {
		err = nvgpu_bo_get_paddr_at(bo,
		    bo_offset + (uint64_t)i * NVGPU_VM_PAGE_SIZE_4K,
		    &paddrs[i]);
		if (err != 0) {
			_kfree(paddrs, M_NVGPU_VM);
			return (err);
		}
	}
	if (page_shift != NVGPU_VM_PAGE_SHIFT_4K) {
		uint32_t pages_per_leaf =
		    (uint32_t)(page_size / NVGPU_VM_PAGE_SIZE_4K);

		if ((page_count % pages_per_leaf) != 0 ||
		    ((uint64_t)paddrs[0] & (page_size - 1)) != 0) {
			_kfree(paddrs, M_NVGPU_VM);
			return (EINVAL);
		}
		for (uint32_t i = 1; i < page_count; i++) {
			if ((uint64_t)paddrs[i] !=
			    (uint64_t)paddrs[0] +
			    (uint64_t)i * NVGPU_VM_PAGE_SIZE_4K) {
				_kfree(paddrs, M_NVGPU_VM);
				return (EINVAL);
			}
		}
	}

	err = nvgpu_vm_bind_segment_add(plan, addr, size, bo_offset,
	    paddrs[0], page_shift);
	if (err != 0) {
		_kfree(paddrs, M_NVGPU_VM);
		return (err);
	}
	plan->segments[plan->count - 1].sysmem_paddrs = paddrs;
	plan->segments[plan->count - 1].sysmem_page_count = page_count;
	return (0);
}

/*
 * nvgpu_vm_bind_segment_add_sysmem_mem()
 *
 * Ownership:
 *   Allocates a temporary physical-page snapshot for a caller-supplied TTM
 *   placement.  It borrows bo and mem and never changes BO reservation,
 *   placement, GEM references, or live VM mapping ownership.
 *
 * Lifetime:
 *   mem must remain valid for the prepare call.  The copied paddr array lives
 *   in plan until nvgpu_vm_bind_segment_plan_fini().  This lets TTM move
 *   code prepare PTEs for new_mem before bo->tbo.mem is published.
 *
 * Threading:
 *   Pure prepare helper.  It does not sleep except for allocation, does not
 *   reserve the BO, and does not write page tables.
 */
static int
nvgpu_vm_bind_segment_add_sysmem_mem(
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    const struct ttm_mem_reg *mem, uint64_t addr, uint64_t size,
    uint64_t bo_offset, uint8_t page_shift)
{
	return (EOPNOTSUPP);
}

/*
 * nvgpu_vm_bind_2m_split_plan_*()
 *
 * Ownership:
 *   Owns prepared, unlinked child PTs returned by the VMM prepare helper.
 *   Once a split is committed, ownership transfers to vmm and the plan drops
 *   its pointer.
 *
 * Lifetime:
 *   The plan spans the prepare and commit halves of one 2 MiB materialize.
 *   Fini releases only PTs that were prepared but never committed.
 *
 * Threading:
 *   Used while vm->vm_token serializes the VM.  Prepare may allocate; commit
 *   only links already prepared PTs and performs deterministic VMM writes.
 */
static void
nvgpu_vm_bind_2m_split_plan_init(
    struct nvgpu_vm_bind_2m_split_plan *plan)
{
	plan->splits = NULL;
	plan->count = 0;
	plan->capacity = 0;
}

static void
nvgpu_vm_bind_2m_split_plan_fini(struct nvgsp_vmm *vmm,
    struct nvgpu_vm_bind_2m_split_plan *plan)
{
	for (uint32_t i = 0; i < plan->count; i++) {
		if (!plan->splits[i].committed)
			nvgsp_vmm_abort_split_vram_2m(vmm,
			    plan->splits[i].pt);
	}
	if (plan->splits != NULL)
		_kfree(plan->splits, M_NVGPU_VM);
	plan->splits = NULL;
	plan->count = 0;
	plan->capacity = 0;
}

static int
nvgpu_vm_bind_2m_split_plan_reserve(
    struct nvgpu_vm_bind_2m_split_plan *plan, uint32_t required)
{
	struct nvgpu_vm_bind_2m_split *splits;
	uint32_t capacity;

	if (required <= plan->capacity)
		return (0);

	capacity = plan->capacity != 0 ? plan->capacity : 4;
	while (capacity < required) {
		if (capacity > UINT32_MAX / 2)
			return (ENOMEM);
		capacity *= 2;
	}

	splits = kmalloc((size_t)capacity * sizeof(*splits), M_NVGPU_VM,
	    M_WAITOK);
	if (splits == NULL)
		return (ENOMEM);
	if (plan->splits != NULL) {
		memcpy(splits, plan->splits,
		    plan->count * sizeof(*splits));
		_kfree(plan->splits, M_NVGPU_VM);
	}
	plan->splits = splits;
	plan->capacity = capacity;
	return (0);
}

static int
nvgpu_vm_bind_2m_split_plan_prepare(struct nvgsp_vmm *vmm,
    struct nvgpu_vm_bind_2m_split_plan *plan, uint64_t addr)
{
	struct nvgsp_vmm_user_pt *pt;
	int err;

	for (uint32_t i = 0; i < plan->count; i++) {
		if (plan->splits[i].addr == addr)
			return (0);
	}

	err = nvgpu_vm_bind_2m_split_plan_reserve(plan, plan->count + 1);
	if (err != 0)
		return (err);

	err = nvgsp_vmm_prepare_split_vram_2m(vmm, addr, &pt);
	if (err != 0)
		return (err);

	plan->splits[plan->count].addr = addr;
	plan->splits[plan->count].pt = pt;
	plan->splits[plan->count].committed = false;
	plan->count++;
	return (0);
}

static int
nvgpu_vm_bind_2m_split_plan_commit(struct nvgsp_vmm *vmm,
    struct nvgpu_vm_bind_2m_split_plan *plan, uint64_t addr)
{
	int err;

	for (uint32_t i = 0; i < plan->count; i++) {
		if (plan->splits[i].addr != addr)
			continue;
		if (plan->splits[i].committed)
			return (0);
		err = nvgsp_vmm_commit_split_vram_2m_noflush(vmm, addr,
		    plan->splits[i].pt);
		if (err != 0)
			return (err);
		plan->splits[i].committed = true;
		plan->splits[i].pt = NULL;
		return (0);
	}
	return (ENOENT);
}

/*
 * nvgpu_vm_bind_2m_split_plan_preflight()
 *
 * Ownership:
 *   Borrows the caller-owned split plan and VMM.  It does not consume prepared
 *   PTs, link child tables, write PD0 slots, or mutate the split plan.
 *
 * Lifetime:
 *   A successful result proves every uncommitted split in the plan can be
 *   consumed by the later commit pass while VM_BIND serialization is held.
 *
 * Threading:
 *   Runs before materialize plan commit starts writing hardware state.  It may
 *   acquire vmm->tok through the VMM check helper, but performs only read-only
 *   validation.
 */
static int
nvgpu_vm_bind_2m_split_plan_preflight(struct nvgsp_vmm *vmm,
    const struct nvgpu_vm_bind_2m_split_plan *plan)
{
	int err;

	for (uint32_t i = 0; i < plan->count; i++) {
		if (plan->splits[i].committed)
			continue;
		err = nvgsp_vmm_check_split_vram_2m(vmm,
		    plan->splits[i].addr, plan->splits[i].pt);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_materialize_entry_*()
 *
 * Ownership:
 *   The entry owns all detached bindings, split PTs, and paddr snapshots
 *   prepared for one large live binding.  On commit, detached binding ownership
 *   moves into the live VM tracker and committed split PTs move into the VMM.
 *
 * Lifetime:
 *   Entries live inside one outer materialize plan.  Fini releases only
 *   resources that were prepared but not consumed by a successful commit.
 *
 * Threading:
 *   Prepare runs before the no-fail materialize commit and may allocate or pin.
 *   Commit runs while VM_BIND and GSP serialization are held and only consumes
 *   prepared resources.
 */
static void
nvgpu_vm_materialize_entry_init(
    struct nvgpu_vm_materialize_entry *entry)
{
	entry->binding = NULL;
	nvgpu_vm_bind_segment_plan_init(&entry->segments);
	nvgpu_vm_bind_2m_split_plan_init(&entry->split_plan);
	LIST_INIT(&entry->new_bindings);
	entry->is_2m = false;
	entry->committed = false;
}

static void
nvgpu_vm_materialize_entry_fini(struct nvgsp_vmm *vmm,
    struct nvgpu_vm_materialize_entry *entry)
{
	if (!entry->committed)
		nvgpu_vm_bindings_free_prepared(&entry->new_bindings);
	nvgpu_vm_bind_2m_split_plan_fini(vmm, &entry->split_plan);
	nvgpu_vm_bind_segment_plan_fini(&entry->segments);
	entry->binding = NULL;
}

static uint32_t
nvgpu_vm_bind_page_shift_bucket(uint8_t page_shift)
{
	switch (page_shift) {
	case NVGPU_VM_PAGE_SHIFT_4K:
		return (NVGPU_VM_BIND_PAGE_SHIFT_4K);
	case NVGPU_VM_PAGE_SHIFT_64K:
		return (NVGPU_VM_BIND_PAGE_SHIFT_64K);
	case NVGPU_VM_PAGE_SHIFT_2M:
		return (NVGPU_VM_BIND_PAGE_SHIFT_2M);
	default:
		return (NVGPU_VM_BIND_PAGE_SHIFT_4K);
	}
}

static uint32_t
nvgpu_vm_bind_domain_bucket(const struct nvgpu_bo *bo)
{
	if (bo != NULL && nvgpu_bo_is_vram(bo))
		return (NVGPU_VM_BIND_DOMAIN_VRAM);
	return (NVGPU_VM_BIND_DOMAIN_HOST);
}

static void
nvgpu_vm_bind_note_reject(struct nvgpu_device *gpu, uint32_t reason,
    uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_note_dirty_range()
 *
 * Ownership:
 *   Borrows gpu and the caller-owned dirty set, then records aggregate
 *   diagnostics for a PTE/PDE range already written by the caller.  It does not
 *   retain any VM, BO, or binding state.
 *
 * Lifetime:
 *   The range is caller-owned and used only for this commit's dirty set and
 *   aggregate counters.  The dirty set is cleared by the enclosing VM_BIND
 *   batch plan after the final flush boundary.
 *
 * Threading:
 *   Called from serialized VM_BIND mutation paths after a noflush VMM helper
 *   succeeds.  The counters are diagnostic only and are not part of the UAPI.
 */
static void
nvgpu_vm_dirty_set_init(struct nvgpu_vm_dirty_set *set)
{
	memset(set, 0, sizeof(*set));
}

static void
nvgpu_vm_dirty_set_remove_range(struct nvgpu_vm_dirty_set *set,
    uint32_t index)
{
	KASSERT(index < set->merged_count,
	    ("nvgpu vm: dirty range index %u count %u", index,
	    set->merged_count));
	for (uint32_t i = index + 1; i < set->merged_count; i++)
		set->ranges[i - 1] = set->ranges[i];
	set->merged_count--;
}

static void
nvgpu_vm_dirty_set_collapse(struct nvgpu_vm_dirty_set *set)
{
	if (!set->dirty)
		return;
	set->ranges[0].start = set->first;
	set->ranges[0].end = set->last;
	set->merged_count = 1;
	set->overflow = true;
}

static void
nvgpu_vm_dirty_set_insert(struct nvgpu_vm_dirty_set *set,
    uint64_t start, uint64_t end)
{
	uint32_t insert_at = 0;

	for (uint32_t i = 0; i < set->merged_count;) {
		struct nvgpu_vm_dirty_range *range = &set->ranges[i];

		if (end < range->start || start > range->end) {
			i++;
			continue;
		}
		if (range->start < start)
			start = range->start;
		if (range->end > end)
			end = range->end;
		nvgpu_vm_dirty_set_remove_range(set, i);
	}

	if (set->merged_count == NVGPU_VM_DIRTY_RANGE_INLINE) {
		nvgpu_vm_dirty_set_collapse(set);
		return;
	}
	while (insert_at < set->merged_count &&
	    set->ranges[insert_at].start < start)
		insert_at++;
	for (uint32_t i = set->merged_count; i > insert_at; i--)
		set->ranges[i] = set->ranges[i - 1];
	set->ranges[insert_at].start = start;
	set->ranges[insert_at].end = end;
	set->merged_count++;
}

static uint64_t
nvgpu_vm_dirty_set_merged_pages(const struct nvgpu_vm_dirty_set *set)
{
	uint64_t pages = 0;

	for (uint32_t i = 0; i < set->merged_count; i++) {
		if (set->ranges[i].end <= set->ranges[i].start)
			continue;
		pages += (set->ranges[i].end - set->ranges[i].start) /
		    NVGPU_VM_PAGE_SIZE_4K;
	}
	return (pages);
}

/*
 * nvgpu_vm_dirty_set_flush()
 *
 * Ownership:
 *   Borrows the DRM-local dirty set and converts its scalar ranges into the
 *   backend VMM dirty-set shape.  It does not transfer ownership of the VM_BIND
 *   batch plan or retain any range storage after return.
 *
 * Lifetime:
 *   The stack range array lives only for nvgsp_vmm_flush_dirty().  The flush
 *   completes the PTE/PDE visibility and invalidate boundary before returning.
 *
 * Threading:
 *   Called from VM_BIND/release commit paths before fence publish or retired
 *   BO ref release.  The VMM backend takes its own token for BAR1 visibility
 *   and hardware invalidate.
 */
static void
nvgpu_vm_dirty_set_flush(struct nvgsp_vmm *vmm,
    const struct nvgpu_vm_dirty_set *set)
{
	struct nvgsp_vmm_dirty_range ranges[NVGPU_VM_DIRTY_RANGE_INLINE];
	struct nvgsp_vmm_dirty_set dirty;
	uint32_t count;

	if (vmm == NULL || set == NULL || !set->dirty)
		return;
	count = set->merged_count;
	KASSERT(count <= NVGPU_VM_DIRTY_RANGE_INLINE,
	    ("nvgpu vm: dirty set range count %u", count));
	if (count == 0) {
		ranges[0].start = set->first;
		ranges[0].end = set->last;
		count = 1;
	} else {
		for (uint32_t i = 0; i < count; i++) {
			ranges[i].start = set->ranges[i].start;
			ranges[i].end = set->ranges[i].end;
		}
	}

	memset(&dirty, 0, sizeof(dirty));
	dirty.ranges = ranges;
	dirty.range_count = count;
	dirty.page_count = nvgpu_vm_dirty_set_merged_pages(set);
	dirty.overflow = set->overflow ? 1 : 0;
	nvgsp_vmm_flush_dirty(vmm, &dirty);
}

static void
nvgpu_vm_dirty_set_publish(struct nvgpu_device *gpu,
    const struct nvgpu_vm_dirty_set *set)
{
	return;
}

static void
nvgpu_vm_bind_note_dirty_range(struct nvgpu_device *gpu,
    struct nvgpu_vm_dirty_set *dirty_set, uint64_t addr, uint64_t size)
{
	uint64_t end;

	if (size == 0)
		return;
	if (!nvgpu_vm_range_end(addr, size, &end))
		end = UINT64_MAX;
	if (dirty_set != NULL) {
		if (!dirty_set->dirty) {
			dirty_set->first = addr;
			dirty_set->last = end;
			dirty_set->dirty = true;
		} else {
			if (addr < dirty_set->first)
				dirty_set->first = addr;
			if (end > dirty_set->last)
				dirty_set->last = end;
		}
		dirty_set->range_count++;
		dirty_set->pages += size / NVGPU_VM_PAGE_SIZE_4K;
		if (dirty_set->overflow) {
			dirty_set->ranges[0].start = dirty_set->first;
			dirty_set->ranges[0].end = dirty_set->last;
			dirty_set->merged_count = 1;
		} else {
			nvgpu_vm_dirty_set_insert(dirty_set, addr, end);
		}
	}
}

/*
 * nvgpu_vm_bind_note_materialize()
 *
 * Ownership:
 *   Borrows gpu and records one large-leaf materialize operation.  The caller
 *   keeps ownership of the binding and hardware page-table state.
 *
 * Lifetime:
 *   The byte range is copied into aggregate counters only.
 *
 * Threading:
 *   Called only after materialize-to-4K PTE writes succeed, while VM_BIND
 *   serialization is held.
 */
static void
nvgpu_vm_bind_note_materialize(struct nvgpu_device *gpu,
    struct nvgpu_vm_dirty_set *dirty_set, uint64_t addr, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_vram_run_has_aligned_middle()
 *
 * Ownership:
 *   Borrows caller-owned MAP coordinates.  It does not retain any BO, VMM, or
 *   binding state.
 *
 * Lifetime:
 *   All inputs are copied by value and used only during the current planner
 *   call.
 *
 * Threading:
 *   Pure planner helper.  The caller must already hold any serialization
 *   needed to make the physical run stable for this VM_BIND operation.
 */
static bool
nvgpu_vm_bind_vram_run_has_aligned_middle(uint64_t addr,
    uint64_t bo_offset, uint64_t paddr, uint64_t size, uint64_t page_size)
{
	uint64_t mask;
	uint64_t addr_mod;
	uint64_t prefix;
	uint64_t middle;

	if (page_size == 0)
		return (false);
	mask = page_size - 1;
	if ((page_size & mask) != 0)
		return (false);
	addr_mod = addr & mask;
	if ((bo_offset & mask) != addr_mod || (paddr & mask) != addr_mod)
		return (false);
	prefix = (page_size - addr_mod) & mask;
	if (prefix >= size)
		return (false);
	middle = (size - prefix) & ~mask;
	return (middle != 0);
}

/*
 * nvgpu_vm_bind_note_vram_2m_reject()
 *
 * Ownership:
 *   Borrows gpu and scalar MAP coordinates.  It records diagnostic counters only
 *   and retains no BO, VMM, or binding state.
 *
 * Lifetime:
 *   Inputs are snapshots from the current VM_BIND planner pass.  The selected
 *   reject reason is not UAPI and may not be used by userspace for control flow.
 *
 * Threading:
 *   Called while VM_BIND serialization protects the planner counters.  It does
 *   not sleep and performs no page-table or GEM operations.
 */
static void
nvgpu_vm_bind_note_vram_2m_reject(struct nvgpu_device *gpu, uint64_t addr,
    uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_build_vram_run_segments_64k()
 *
 * Ownership:
 *   Borrows bo and copies one physically contiguous VRAM run into MAP plan
 *   segments.  It does not acquire or release GEM, BO, VMM, or binding
 *   ownership.
 *
 * Lifetime:
 *   Added segments are owned by the caller's per-op plan until the MAP
 *   operation finishes or aborts.
 *
 * Threading:
 *   Pure planner helper.  The caller must keep the BO backing stable for the
 *   current VM_BIND operation and serialize access to gpu counters.
 */
static int
nvgpu_vm_bind_build_vram_run_segments_64k(struct nvgpu_device *gpu,
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_64K - 1;
	uint64_t addr_mod, bo_mod, paddr_mod;
	uint64_t prefix, middle, suffix;
	int err;

	if (nvgpu_bo_get_size(bo) < NVGPU_VM_PAGE_SIZE_64K) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add(plan, addr, size,
		    bo_offset, paddr, NVGPU_VM_PAGE_SHIFT_4K));
	}

	addr_mod = addr & mask;
	bo_mod = bo_offset & mask;
	paddr_mod = paddr & mask;
	if (addr_mod != bo_mod || addr_mod != paddr_mod) {
		uint32_t reason;

		if (addr_mod != bo_mod)
			reason = addr_mod != 0 ?
			    NVGPU_VM_BIND_REJECT_VA_ALIGN :
			    NVGPU_VM_BIND_REJECT_BO_OFFSET_ALIGN;
		else
			reason = NVGPU_VM_BIND_REJECT_PADDR_ALIGN;
		nvgpu_vm_bind_note_reject(gpu, reason, size);
		return (nvgpu_vm_bind_segment_add(plan, addr, size,
		    bo_offset, paddr, NVGPU_VM_PAGE_SHIFT_4K));
	}

	prefix = (NVGPU_VM_PAGE_SIZE_64K - addr_mod) & mask;
	if (prefix >= size) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add(plan, addr, size,
		    bo_offset, paddr, NVGPU_VM_PAGE_SHIFT_4K));
	}
	middle = (size - prefix) & ~mask;
	if (middle == 0) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add(plan, addr, size,
		    bo_offset, paddr, NVGPU_VM_PAGE_SHIFT_4K));
	}
	suffix = size - prefix - middle;

	if (prefix != 0) {
		err = nvgpu_vm_bind_segment_add(plan, addr, prefix,
		    bo_offset, paddr, NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	err = nvgpu_vm_bind_segment_add(plan, addr + prefix,
	    middle, bo_offset + prefix, paddr + prefix,
	    NVGPU_VM_PAGE_SHIFT_64K);
	if (err != 0)
		return (err);
	if (suffix != 0) {
		err = nvgpu_vm_bind_segment_add(plan,
		    addr + prefix + middle, suffix,
		    bo_offset + prefix + middle, paddr + prefix + middle,
		    NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_build_map_segments()
 *
 * Ownership:
 *   Borrows bo and copies MAP range values.  It does not acquire or release
 *   GEM, BO, VMM, or binding ownership.
 *
 * Lifetime:
 *   Returned segments are valid only for the current MAP operation.  Each
 *   segment is later materialized into an independent live binding with its
 *   own GEM reference and VM_BIND pin.
 *
 * Threading:
 *   Called while VM_BIND holds at least one BO pin, so bo->domain and
 *   bo->paddr are stable for this VM_BIND.  The helper is otherwise pure.
 */
static int
nvgpu_vm_bind_build_vram_run_segments(struct nvgpu_device *gpu,
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_2M - 1;
	uint64_t addr_mod;
	uint64_t prefix, middle, suffix;
	int err;

	if (nvgpu_bo_get_size(bo) >= NVGPU_VM_PAGE_SIZE_2M &&
	    nvgpu_vm_bind_vram_run_has_aligned_middle(addr, bo_offset,
	    paddr, size, NVGPU_VM_PAGE_SIZE_2M)) {
		if (1 == 0) {
			nvgpu_vm_bind_note_reject(gpu,
			    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE, size);
			return (nvgpu_vm_bind_build_vram_run_segments_64k(gpu,
			    plan, bo, addr, bo_offset, paddr, size));
		}

		addr_mod = addr & mask;
		prefix = (NVGPU_VM_PAGE_SIZE_2M - addr_mod) & mask;
		middle = (size - prefix) & ~mask;
		suffix = size - prefix - middle;

		if (prefix != 0) {
			err = nvgpu_vm_bind_build_vram_run_segments_64k(gpu,
			    plan, bo, addr, bo_offset, paddr, prefix);
			if (err != 0)
				return (err);
		}
		err = nvgpu_vm_bind_segment_add(plan, addr + prefix,
		    middle, bo_offset + prefix, paddr + prefix,
		    NVGPU_VM_PAGE_SHIFT_2M);
		if (err != 0)
			return (err);
		if (suffix != 0) {
			err = nvgpu_vm_bind_build_vram_run_segments_64k(gpu,
			    plan, bo, addr + prefix + middle,
			    bo_offset + prefix + middle,
			    paddr + prefix + middle, suffix);
			if (err != 0)
				return (err);
		}
		return (0);
	}

	if (nvgpu_bo_get_size(bo) >= NVGPU_VM_PAGE_SIZE_2M)
		nvgpu_vm_bind_note_vram_2m_reject(gpu, addr, bo_offset,
		    paddr, size);
	return (nvgpu_vm_bind_build_vram_run_segments_64k(gpu, plan, bo,
	    addr, bo_offset, paddr, size));
}

/*
 * nvgpu_vm_bind_build_sysmem_run_segments_64k()
 *
 * Ownership:
 *   Borrows bo and copies one physically contiguous HOST/GART run into MAP
 *   plan segments.  Each segment owns its own 4 KiB physical-page snapshot so
 *   commit never has to query BO backing.
 *
 * Lifetime:
 *   Added segments are owned by the caller's per-op plan until the MAP
 *   operation finishes or aborts.
 *
 * Threading:
 *   Pure planner helper except for diagnostic counters.  The caller must keep
 *   the BO backing pinned and populated for the current VM_BIND operation.
 */
static int
nvgpu_vm_bind_build_sysmem_run_segments_64k(struct nvgpu_device *gpu,
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_64K - 1;
	uint64_t addr_mod, bo_mod, paddr_mod;
	uint64_t prefix, middle, suffix;
	int err;

	if (nvgpu_bo_get_size(bo) < NVGPU_VM_PAGE_SIZE_64K) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
	}

	addr_mod = addr & mask;
	bo_mod = bo_offset & mask;
	paddr_mod = paddr & mask;
	if (addr_mod != bo_mod || addr_mod != paddr_mod) {
		uint32_t reason;

		if (addr_mod != bo_mod)
			reason = addr_mod != 0 ?
			    NVGPU_VM_BIND_REJECT_VA_ALIGN :
			    NVGPU_VM_BIND_REJECT_BO_OFFSET_ALIGN;
		else
			reason = NVGPU_VM_BIND_REJECT_PADDR_ALIGN;
		nvgpu_vm_bind_note_reject(gpu, reason, size);
		return (nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
	}

	prefix = (NVGPU_VM_PAGE_SIZE_64K - addr_mod) & mask;
	if (prefix >= size) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
	}
	middle = (size - prefix) & ~mask;
	if (middle == 0) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_RANGE_ALIGN, size);
		return (nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
	}
	suffix = size - prefix - middle;

	if (prefix != 0) {
		err = nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    prefix, bo_offset, NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	err = nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr + prefix,
	    middle, bo_offset + prefix, NVGPU_VM_PAGE_SHIFT_64K);
	if (err != 0)
		return (err);
	if (suffix != 0) {
		err = nvgpu_vm_bind_segment_add_sysmem(plan, bo,
		    addr + prefix + middle, suffix,
		    bo_offset + prefix + middle, NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_build_sysmem_run_segments_2m()
 *
 * Ownership:
 *   Borrows a pinned HOST/GART BO and appends caller-owned MAP segments.  Each
 *   appended sysmem segment owns its paddr snapshot through the segment plan.
 *
 * Lifetime:
 *   The helper only creates prepare-stage value data.  The snapshots are
 *   consumed by the prepared sysmem writer during this VM_BIND op and released
 *   by nvgpu_vm_bind_segment_plan_fini().
 *
 * Threading:
 *   Runs while VM_BIND serialization keeps the BO backing stable.  It does no
 *   PTE writes and may fail before the no-fail commit section begins.
 */
static int
nvgpu_vm_bind_build_sysmem_run_segments_2m(struct nvgpu_device *gpu,
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_2M - 1;
	uint64_t addr_mod;
	uint64_t prefix, middle, suffix;
	int err;

	addr_mod = addr & mask;
	prefix = (NVGPU_VM_PAGE_SIZE_2M - addr_mod) & mask;
	middle = (size - prefix) & ~mask;
	suffix = size - prefix - middle;

	if (prefix != 0) {
		err = nvgpu_vm_bind_build_sysmem_run_segments_64k(gpu,
		    plan, bo, addr, bo_offset, paddr, prefix);
		if (err != 0)
			return (err);
	}
	err = nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr + prefix,
	    middle, bo_offset + prefix, NVGPU_VM_PAGE_SHIFT_2M);
	if (err != 0)
		return (err);
	if (suffix != 0) {
		err = nvgpu_vm_bind_build_sysmem_run_segments_64k(gpu,
		    plan, bo, addr + prefix + middle,
		    bo_offset + prefix + middle, paddr + prefix + middle,
		    suffix);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_bind_build_sysmem_run_segments(struct nvgpu_device *gpu,
    struct nvgpu_vm_bind_segment_plan *plan, const struct nvgpu_bo *bo,
    uint64_t addr, uint64_t bo_offset, uint64_t paddr, uint64_t size)
{
	bool has_2m;
	bool has_64k;

	has_2m = nvgpu_vm_bind_vram_run_has_aligned_middle(addr, bo_offset,
	    paddr, size, NVGPU_VM_PAGE_SIZE_2M);
	has_64k = nvgpu_vm_bind_vram_run_has_aligned_middle(addr, bo_offset,
	    paddr, size, NVGPU_VM_PAGE_SIZE_64K);
	if (has_2m) {
		if (1 == 0 ||
		    1 == 0) {
			nvgpu_vm_bind_note_reject(gpu,
			    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE, size);
		}
	}
	if (1 == 0) {
		if (has_64k && !has_2m) {
			nvgpu_vm_bind_note_reject(gpu,
			    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE, size);
		}
		return (nvgpu_vm_bind_segment_add_sysmem(plan, bo, addr,
		    size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
	}
	if (has_2m && 1 != 0) {
		return (nvgpu_vm_bind_build_sysmem_run_segments_2m(gpu,
		    plan, bo, addr, bo_offset, paddr, size));
	}

	return (nvgpu_vm_bind_build_sysmem_run_segments_64k(gpu, plan, bo,
	    addr, bo_offset, paddr, size));
}

static int
nvgpu_vm_bind_build_map_segments(struct nvgpu_device *gpu,
    const struct nvgpu_bo *bo, uint64_t addr, uint64_t bo_offset, uint64_t size,
    struct nvgpu_vm_bind_segment_plan *plan)
{
	uint64_t off, run_size;
	uint64_t pending_addr = 0, pending_bo_offset = 0, pending_size = 0;
	vm_paddr_t paddr;
	uint32_t run_count = 0;
	int err;

	if (!nvgpu_bo_is_vram(bo)) {
		for (off = 0; off < size; off += run_size) {
			bool has_2m, has_64k, large_capable;

			err = nvgpu_bo_get_paddr_run_at(bo, bo_offset + off,
			    size - off, &paddr, &run_size);
			if (err != 0)
				return (err);
			if (run_size == 0)
				return (EIO);
			run_count++;
			has_2m = nvgpu_vm_bind_vram_run_has_aligned_middle(
			    addr + off, bo_offset + off, paddr, run_size,
			    NVGPU_VM_PAGE_SIZE_2M);
			has_64k = nvgpu_vm_bind_vram_run_has_aligned_middle(
			    addr + off, bo_offset + off, paddr, run_size,
			    NVGPU_VM_PAGE_SIZE_64K);
			if (1 == 0 &&
			    (has_2m || has_64k)) {
				nvgpu_vm_bind_note_reject(gpu,
				    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE,
				    run_size);
			}
			large_capable =
			    1 != 0 &&
			    (has_64k ||
			    (1 != 0 && has_2m));
			if (large_capable) {
				if (pending_size != 0) {
					err = nvgpu_vm_bind_segment_add_sysmem(
					    plan, bo, pending_addr,
					    pending_size, pending_bo_offset,
					    NVGPU_VM_PAGE_SHIFT_4K);
					if (err != 0)
						return (err);
					pending_size = 0;
				}
				err = nvgpu_vm_bind_build_sysmem_run_segments(
				    gpu, plan, bo, addr + off, bo_offset + off,
				    paddr, run_size);
				if (err != 0)
					return (err);
			} else {
				if (pending_size == 0) {
					pending_addr = addr + off;
					pending_bo_offset = bo_offset + off;
				}
				pending_size += run_size;
			}
		}
		if (pending_size != 0) {
			err = nvgpu_vm_bind_segment_add_sysmem(plan, bo,
			    pending_addr, pending_size, pending_bo_offset,
			    NVGPU_VM_PAGE_SHIFT_4K);
			if (err != 0)
				return (err);
		}
		return (0);
	}

	for (off = 0; off < size; off += run_size) {
		err = nvgpu_bo_get_paddr_run_at(bo, bo_offset + off, size - off,
		    &paddr, &run_size);
		if (err != 0)
			return (err);
		if (run_size == 0)
			return (EIO);
		run_count++;
		err = nvgpu_vm_bind_build_vram_run_segments(gpu, plan, bo,
		    addr + off, bo_offset + off, paddr, run_size);
		if (err != 0)
			return (err);
	}

	return (0);
}

/*
 * nvgpu_vm_bind_sparse_has_aligned_middle()
 *
 * Ownership:
 *   Borrows scalar VM_BIND sparse coordinates.  It does not retain VM, BO, or
 *   VMM state.
 *
 * Lifetime:
 *   The result is valid only for the current planner call.
 *
 * Threading:
 *   Pure helper.  The caller owns any VM_BIND serialization needed for
 *   diagnostic counter updates.
 */
static bool
nvgpu_vm_bind_sparse_has_aligned_middle(uint64_t addr, uint64_t size,
    uint64_t page_size)
{
	uint64_t mask, addr_mod, prefix, middle;

	if (page_size == 0)
		return (false);
	mask = page_size - 1;
	if ((page_size & mask) != 0)
		return (false);

	addr_mod = addr & mask;
	prefix = (page_size - addr_mod) & mask;
	if (prefix >= size)
		return (false);
	middle = (size - prefix) & ~mask;
	return (middle != 0);
}

/*
 * nvgpu_vm_bind_build_sparse_segments_64k()
 *
 * Ownership:
 *   Appends caller-owned value segments to plan.  No sparse-region object is
 *   allocated here; later prepare_sparse_region_page() consumes these values.
 *
 * Lifetime:
 *   The segments live only for the enclosing VM_BIND op and are released with
 *   the segment plan.
 *
 * Threading:
 *   Pure planner helper.  It does not inspect VMM state or write PTEs.
 */
static int
nvgpu_vm_bind_build_sparse_segments_64k(
    struct nvgpu_vm_bind_segment_plan *plan, uint64_t addr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_64K - 1;
	uint64_t prefix, middle, suffix;
	int err;

	prefix = (NVGPU_VM_PAGE_SIZE_64K - (addr & mask)) & mask;
	if (prefix >= size)
		return (nvgpu_vm_bind_segment_add(plan, addr, size, 0, 0,
		    NVGPU_VM_PAGE_SHIFT_4K));
	middle = (size - prefix) & ~mask;
	suffix = size - prefix - middle;

	if (prefix != 0) {
		err = nvgpu_vm_bind_segment_add(plan, addr, prefix, 0, 0,
		    NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	if (middle != 0) {
		err = nvgpu_vm_bind_segment_add(plan, addr + prefix,
		    middle, 0, 0, NVGPU_VM_PAGE_SHIFT_64K);
		if (err != 0)
			return (err);
	}
	if (suffix != 0) {
		err = nvgpu_vm_bind_segment_add(plan, addr + prefix +
		    middle, suffix, 0, 0, NVGPU_VM_PAGE_SHIFT_4K);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_build_sparse_segments_2m()
 *
 * Ownership:
 *   Appends sparse target segments only.  The caller owns the plan and later
 *   sparse-region allocation for each segment.
 *
 * Lifetime:
 *   Segment values survive until the enclosing VM_BIND op finishes preparing
 *   and committing sparse regions.
 *
 * Threading:
 *   Pure planner helper.  It encodes page-size intent but does not mutate
 *   mapping or GMMU state.
 */
static int
nvgpu_vm_bind_build_sparse_segments_2m(
    struct nvgpu_vm_bind_segment_plan *plan, uint64_t addr, uint64_t size)
{
	uint64_t mask = NVGPU_VM_PAGE_SIZE_2M - 1;
	uint64_t prefix, middle, suffix;
	int err;

	prefix = (NVGPU_VM_PAGE_SIZE_2M - (addr & mask)) & mask;
	if (prefix >= size)
		return (nvgpu_vm_bind_build_sparse_segments_64k(plan,
		    addr, size));
	middle = (size - prefix) & ~mask;
	suffix = size - prefix - middle;

	if (prefix != 0) {
		err = nvgpu_vm_bind_build_sparse_segments_64k(plan, addr,
		    prefix);
		if (err != 0)
			return (err);
	}
	if (middle != 0) {
		err = nvgpu_vm_bind_segment_add(plan, addr + prefix,
		    middle, 0, 0, NVGPU_VM_PAGE_SHIFT_2M);
		if (err != 0)
			return (err);
	}
	if (suffix != 0) {
		err = nvgpu_vm_bind_build_sparse_segments_64k(plan,
		    addr + prefix + middle, suffix);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_build_sparse_segments()
 *
 * Ownership:
 *   Converts one userspace MAP|SPARSE op into sparse target segments.  Segments
 *   are value data owned by the caller's segment plan; no sparse-region record
 *   is allocated here.
 *
 * Lifetime:
 *   The plan lives only for the current VM_BIND op.  Prepared sparse-region
 *   records later copy addr/size/page_shift from these segments.
 *
 * Threading:
 *   Pure planner helper except for diagnostic reject counters.  Large sparse
 *   is hidden behind explicit debug gates and defaults to the legacy 4 KiB
 *   path.
 */
static int
nvgpu_vm_bind_build_sparse_segments(struct nvgpu_device *gpu,
    uint64_t addr, uint64_t size, struct nvgpu_vm_bind_segment_plan *plan)
{
	bool has_2m = nvgpu_vm_bind_sparse_has_aligned_middle(addr, size,
	    NVGPU_VM_PAGE_SIZE_2M);
	bool has_64k = nvgpu_vm_bind_sparse_has_aligned_middle(addr, size,
	    NVGPU_VM_PAGE_SIZE_64K);

	if ((has_2m || has_64k) && 1 == 0) {
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE, size);
		return (nvgpu_vm_bind_segment_add(plan, addr, size, 0, 0,
		    NVGPU_VM_PAGE_SHIFT_4K));
	}

	if (has_2m) {
		if (1 != 0)
			return (nvgpu_vm_bind_build_sparse_segments_2m(
			    plan, addr, size));
		nvgpu_vm_bind_note_reject(gpu,
		    NVGPU_VM_BIND_REJECT_CAPABILITY_GATE, size);
	}

	if (has_64k || has_2m)
		return (nvgpu_vm_bind_build_sparse_segments_64k(plan,
		    addr, size));
	return (nvgpu_vm_bind_segment_add(plan, addr, size, 0, 0,
	    NVGPU_VM_PAGE_SHIFT_4K));
}

/*
 * nvgpu_vm_bind_prepare_segment_bindings()
 *
 * Ownership:
 *   Takes one GEM reference and one VM_BIND pin for every segment binding it
 *   creates.  On failure it releases all ownership already acquired.
 *
 * Lifetime:
 *   On success, new_bindings owns detached binding nodes.  The caller must
 *   either insert them into the live VM tracker after PTE install succeeds, or
 *   free them with nvgpu_vm_bindings_free_prepared().
 *
 * Threading:
 *   Runs in VM_BIND prepare while vm->vm_token is held.  It may sleep while
 *   pinning the BO, but it does not touch hardware PTEs.
 */
static int
nvgpu_vm_bind_prepare_segment_bindings(struct nvgpu_vm *vm,
    struct nvgpu_bo *bo,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint8_t pte_kind, struct nvgpu_vm_binding_list *new_bindings)
{
	struct nvgpu_vm_binding *binding;
	int err;

	LIST_INIT(new_bindings);
	for (uint32_t i = 0; i < segment_count; i++) {
		nvgpu_bo_addref(bo);
		binding = nvgpu_vm_binding_alloc(vm, segments[i].addr,
		    segments[i].size, bo, segments[i].bo_offset, pte_kind,
		    segments[i].page_shift);
		if (binding == NULL) {
			nvgpu_bo_release(bo);
			err = ENOMEM;
			goto fail;
		}
		err = nvgpu_vm_binding_pin(binding);
		if (err != 0) {
			nvgpu_vm_binding_free(binding);
			err = -err;
			goto fail;
		}
		LIST_INSERT_HEAD(new_bindings, binding, link);
	}
	return (0);

fail:
	nvgpu_vm_bindings_free_prepared(new_bindings);
	return (err);
}

static void
nvgpu_vm_bindings_insert_prepared(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding_list *new_bindings)
{
	struct nvgpu_vm_binding *binding;

	while ((binding = LIST_FIRST(new_bindings)) != NULL) {
		LIST_REMOVE(binding, link);
		nvgpu_vm_binding_insert_sorted(vm, binding);
	}
}

/*
 * nvgpu_vm_bind_map_segments_preflight()
 *
 * Ownership:
 *   Borrows the MAP segment plan, target BO, and file VMM.  It does not retain
 *   references, allocate bos, or write hardware PTEs.
 *
 * Lifetime:
 *   The caller keeps VM_BIND serialization held until the later prepared
 *   writers run.  A successful preflight proves that every segment has valid
 *   writer arguments and that all target PT storage is present before the
 *   first segment is written.
 *
 * Threading:
 *   May take vmm->tok through nvgsp_vmm_check_prepared_pt_range().  This is
 *   still a prepare/pre-commit gate; it must run before any PTE write in the
 *   MAP commit section.
 */
static int
nvgpu_vm_bind_map_segments_preflight_target(struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    bool target_vram)
{
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		const struct nvgpu_vm_bind_segment *segment = &segments[i];
		uint64_t page_size;

		if (segment->size == 0)
			return (EINVAL);
		if (!target_vram) {
			if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_4K)
				page_size = NVGPU_VM_PAGE_SIZE_4K;
			else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_64K)
				page_size = NVGPU_VM_PAGE_SIZE_64K;
			else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
				page_size = NVGPU_VM_PAGE_SIZE_2M;
			else
				return (EINVAL);
			if (segment->sysmem_paddrs == NULL ||
			    segment->sysmem_page_count !=
			    segment->size / NVGPU_VM_PAGE_SIZE_4K ||
			    ((segment->addr | segment->size |
			    segment->bo_offset) &
			    (page_size - 1)) != 0)
				return (EINVAL);
			if (segment->page_shift != NVGPU_VM_PAGE_SHIFT_4K) {
				uint32_t pages_per_leaf =
				    (uint32_t)(page_size /
				    NVGPU_VM_PAGE_SIZE_4K);

				if ((segment->paddr & (page_size - 1)) != 0 ||
				    (segment->sysmem_page_count %
				    pages_per_leaf) != 0)
					return (EINVAL);
				for (uint32_t j = 1;
				    j < segment->sysmem_page_count; j++) {
					if ((uint64_t)segment->sysmem_paddrs[j] !=
					    (uint64_t)segment->sysmem_paddrs[0] +
					    (uint64_t)j *
					    NVGPU_VM_PAGE_SIZE_4K)
						return (EINVAL);
				}
			}
		} else {
			if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_4K)
				page_size = NVGPU_VM_PAGE_SIZE_4K;
			else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_64K)
				page_size = NVGPU_VM_PAGE_SIZE_64K;
			else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
				page_size = NVGPU_VM_PAGE_SIZE_2M;
			else
				return (EINVAL);
			if ((segment->addr | segment->paddr |
			    segment->size) & (page_size - 1))
				return (EINVAL);
		}
		err = nvgsp_vmm_check_prepared_pt_range(vm->backend,
		    segment->addr, segment->size, segment->page_shift);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_bind_map_segments_preflight(struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    const struct nvgpu_bo *bo)
{
	return (nvgpu_vm_bind_map_segments_preflight_target(vm,
	    segments, segment_count,
	    nvgpu_bo_is_vram(bo)));
}

/*
 * nvgpu_vm_bind_map_segments_target_write_noflush()
 *
 * Ownership:
 *   Borrows prepared segment storage, vm, and dirty_set.  It does not own
 *   GEM refs, BO pins, or VM binding records, and it does not allocate page
 *   table storage.
 *
 * Lifetime:
 *   All preflight that can allocate or fail predictably must already be done
 *   by the caller.  This helper is the commit-side writer used after TTM
 *   rebind has invalidated old PTEs; it only consumes prepared mapping
 *   metadata and records the written dirty ranges.
 *
 * Threading:
 *   Called while the caller holds vm->vm_token and the enclosing GSP/PTE
 *   write serialization.  It does not flush or publish fences.
 */
static int
nvgpu_vm_bind_map_segments_target_write_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    bool target_vram, uint8_t pte_kind,
    struct nvgpu_vm_dirty_set *dirty_set)
{
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		if (target_vram) {
			err = nvgsp_vmm_map_vram_flags_page_prepared_noflush(
			    vm->backend, segments[i].addr, segments[i].paddr,
			    segments[i].size, 0, 0, pte_kind,
			    segments[i].page_shift);
		} else {
			err =
			    nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush(
			    vm->backend, segments[i].addr,
			    segments[i].sysmem_paddrs,
			    segments[i].sysmem_page_count, pte_kind,
			    segments[i].page_shift);
		}
		if (err != 0)
			return (err);
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
		    segments[i].addr, segments[i].size);
	}
	return (0);
}

static int
nvgpu_vm_bind_map_segments_target_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    bool target_vram, uint8_t pte_kind,
    struct nvgpu_vm_dirty_set *dirty_set)
{
	int err;

	err = nvgpu_vm_bind_map_segments_preflight_target(vm, segments,
	    segment_count, target_vram);
	if (err != 0)
		return (err);
	return (nvgpu_vm_bind_map_segments_target_write_noflush(gpu, vm,
	    segments, segment_count, target_vram, pte_kind, dirty_set));
}

static int
nvgpu_vm_bind_map_segments_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    const struct nvgpu_bo *bo, uint8_t pte_kind,
    struct nvgpu_vm_dirty_set *dirty_set)
{
	return (nvgpu_vm_bind_map_segments_target_noflush(gpu, vm,
	    segments, segment_count,
	    nvgpu_bo_is_vram(bo), pte_kind,
	    dirty_set));
}

static void
nvgpu_vm_bind_note_map_segments(struct nvgpu_device *gpu, uint32_t flags,
	    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
	    const struct nvgpu_bo *bo)
{
	return;
}

static void
nvgpu_vm_bind_debug_segments(struct nvgpu_device *gpu,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count)
{
	return;
}

static void
nvgpu_vm_bind_mark_bo_tiled(struct nvgpu_bo *bo, uint8_t pte_kind)
{
	if (pte_kind == 0)
		return;

	if (!bo->vm_bound_tiled)
		bo->vm_bound_kind = pte_kind;
	else if (bo->vm_bound_kind != pte_kind)
		bo->vm_bound_mixed_kind = true;
	bo->vm_bound_tiled = true;
}

static void
nvgpu_vm_binding_insert_sorted(struct nvgpu_vm *vm,
    struct nvgpu_vm_binding *binding)
{
	struct nvgpu_vm_binding *prev;
	uint64_t end = binding->addr + binding->size;

	nvgpu_vm_binding_tree_insert(vm, binding);
	if (vm->vm_bindings_max_end < end)
		vm->vm_bindings_max_end = end;

	prev = nvgpu_vm_binding_tree_RB_PREV(binding);
	if (prev != NULL)
		LIST_INSERT_AFTER(prev, binding, link);
	else
		LIST_INSERT_HEAD(&vm->vm_bindings, binding, link);

	nvgpu_vm_binding_bo_attach(binding);
}

static void
nvgpu_vm_bindings_recalc_max_end(struct nvgpu_vm *vm)
{
	struct nvgpu_vm_binding *binding;
	uint64_t max_end = 0;

	LIST_FOREACH(binding, &vm->vm_bindings, link) {
		uint64_t end = binding->addr + binding->size;

		if (max_end < end)
			max_end = end;
	}
	vm->vm_bindings_max_end = max_end;
}

static int
nvgpu_vm_binding_pin(struct nvgpu_vm_binding *binding)
{
	struct nvgpu_bo *bo;
	int err;

	nvgpu_vm_binding_assert(binding);
	if (binding->bo_pinned)
		return (0);

	bo = binding->bo;
	err = nvgpu_bo_vm_bind_pin(bo, &binding->bo_no_evict_pinned);
	if (err != 0)
		return (err);
	binding->bo_pinned = true;
	return (0);
}

static int
nvgpu_vm_binding_unpin(struct nvgpu_vm_binding *binding)
{
	struct nvgpu_bo *bo;
	int err;

	nvgpu_vm_binding_assert(binding);
	if (!binding->bo_pinned)
		return (0);

	bo = binding->bo;
	err = nvgpu_bo_vm_bind_unpin(bo, binding->bo_no_evict_pinned);
	if (err == 0) {
		binding->bo_pinned = false;
		binding->bo_no_evict_pinned = false;
	}
	return (err);
}

static void
nvgpu_vm_binding_free(struct nvgpu_vm_binding *binding)
{
	int err;

	nvgpu_vm_binding_assert(binding);
	KASSERT(!binding->bo_linked,
	    ("nvgpu vm: freeing live BO reverse mapping addr=0x%016jx",
	    (uintmax_t)binding->addr));
	KASSERT(!binding->validate_linked,
	    ("nvgpu vm: freeing validate-listed VM binding addr=0x%016jx",
	    (uintmax_t)binding->addr));
	err = nvgpu_vm_binding_unpin(binding);
	if (err != 0)
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "nvgpu vm: VM_BIND unpin failed bo=%p err=%d\n",
		    binding->bo, err);
	nvgpu_bo_release(binding->bo);
	_kfree(binding, M_NVGPU_VM);
}

static void
nvgpu_vm_binding_unlink_free(struct nvgpu_vm_binding *binding)
{
	nvgpu_vm_binding_bo_detach(binding);
	nvgpu_vm_binding_tree_remove(binding->owner, binding);
	LIST_REMOVE(binding, link);
	nvgpu_vm_binding_free(binding);
}

/*
 * nvgpu_vm_bindings_release()
 *
 * Ownership:
 *   Consumes every binding currently linked on bindings.  Each binding owns a
 *   GEM reference and may own a VM_BIND BO pin; both are released here.
 *
 * Lifetime:
 *   The list head remains owned by the caller and is empty on return.  The
 *   return value is the number of consumed bindings.
 *
 * Threading:
 *   The caller must own the detached list exclusively.  This helper may sleep
 *   through GEM/TTM destruction and reservation-object waits.
 */
static uint32_t
nvgpu_vm_bindings_release(
    struct nvgpu_vm_binding_list *bindings)
{
	struct nvgpu_vm_binding *binding;
	uint32_t count = 0;

	while ((binding = LIST_FIRST(bindings)) != NULL) {
		LIST_REMOVE(binding, link);
		nvgpu_vm_binding_free(binding);
		count++;
	}
	return (count);
}

/*
 * nvgpu_vm_bind_retire_work()
 *
 * Ownership:
 *   Consumes the retire record handed to system_unbound_wq by
 *   nvgpu_vm_bind_retire_schedule().
 *
 * Lifetime:
 *   Used after the VM_BIND job has updated PTEs but before its done fence is
 *   signaled.  The retired list is released after the done fence is visible.
 *
 * Threading:
 *   Requires the caller to hold the per-file vm_token that serializes the live
 *   binding tracker.  The retired list is detached from other threads.
 */
static void
nvgpu_vm_binding_unlink_retire(struct nvgpu_vm_binding *binding,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	nvgpu_vm_binding_bo_detach(binding);
	nvgpu_vm_binding_tree_remove(binding->owner, binding);
	LIST_REMOVE(binding, link);
	LIST_INSERT_HEAD(retired_bindings, binding, link);
}

/*
 * nvgpu_vm_bindings_can_merge()
 *
 * Ownership:
 *   Borrows two live binding records.  It does not acquire, transfer, or drop
 *   GEM references, BO pins, or VMM ownership.
 *
 * Lifetime:
 *   The result is valid only while both bindings stay linked in the same VM
 *   tracker and no caller mutates their VA or BO-offset ranges.
 *
 * Threading:
 *   Requires vm->vm_token through the caller.  This is a pure predicate for
 *   software mapping coalescing and does not inspect or write hardware PTEs.
 */
static bool
nvgpu_vm_bindings_can_merge(const struct nvgpu_vm_binding *left,
    const struct nvgpu_vm_binding *right)
{
	if (left == NULL || right == NULL)
		return (false);
	if (left->owner != right->owner)
		return (false);
	if (!left->pte_installed || !right->pte_installed)
		return (false);
	if (!left->bo_pinned || !right->bo_pinned)
		return (false);
	if (left->addr > UINT64_MAX - left->size ||
	    left->bo_offset > UINT64_MAX - left->size)
		return (false);
	if (right->addr > UINT64_MAX - right->size ||
	    right->bo_offset > UINT64_MAX - right->size)
		return (false);
	if (left->size > UINT64_MAX - right->size)
		return (false);
	if (left->addr + left->size != right->addr)
		return (false);
	if (left->bo_offset + left->size != right->bo_offset)
		return (false);
	if (left->bo != right->bo)
		return (false);
	if (left->pte_kind != right->pte_kind)
		return (false);
	if (left->page_shift != right->page_shift)
		return (false);
	return (true);
}

/*
 * nvgpu_vm_bindings_merge_neighbors()
 *
 * Ownership:
 *   Mutates vm's live mapping tracker and moves merged-away right-hand
 *   bindings to retired_bindings.  The surviving left binding keeps one GEM
 *   reference and one VM_BIND BO pin for the merged VA interval; retired
 *   bindings keep their original ownership until the VM_BIND done fence is
 *   visible.
 *
 * Lifetime:
 *   The helper never changes hardware PTEs.  It only coalesces adjacent
 *   software mappings whose PTE representation is already identical, so the
 *   mapping tree remains isomorphic with the existing page table leaves.
 *
 * Threading:
 *   Requires vm->vm_token.  Call after the enclosing remap has installed
 *   PTEs and spliced new mappings, before publishing the VM_BIND done fence.
 */
static void
nvgpu_vm_bindings_merge_neighbors(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_binding *binding, *prev, *right;
	uint64_t end = addr + size;
	uint64_t merged_pages = 0;
	uint64_t merged_count = 0;

	if (size == 0 || LIST_FIRST(&vm->vm_bindings) == NULL)
		return;
	if (addr > UINT64_MAX - size)
		return;

	binding = nvgpu_vm_binding_tree_lower_bound(vm, addr);
	if (binding != NULL) {
		prev = nvgpu_vm_binding_tree_RB_PREV(binding);
		if (prev != NULL)
			binding = prev;
	} else {
		binding = nvgpu_vm_binding_tree_RB_MINMAX(
		    &vm->vm_binding_tree, 1);
	}

	while (binding != NULL) {
		uint64_t right_size;

		if (binding->addr > end)
			break;
		right = nvgpu_vm_binding_tree_RB_NEXT(binding);
		if (right == NULL)
			break;
		if (!nvgpu_vm_bindings_can_merge(binding, right)) {
			binding = right;
			continue;
		}

		right_size = right->size;
		right->pte_installed = false;
		nvgpu_vm_binding_unlink_retire(right, retired_bindings);
		binding->size += right_size;
		merged_count++;
		merged_pages += right_size / NVGPU_VM_PAGE_SIZE_4K;
	}

	if (merged_count == 0)
		return;
	nvgpu_vm_bindings_recalc_max_end(vm);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND merged mappings count=%ju pages=%ju range=0x%016jx+0x%016jx\n",
	    (uintmax_t)merged_count, (uintmax_t)merged_pages,
	    (uintmax_t)addr, (uintmax_t)size);
}

static void
nvgpu_vm_bind_note_promote(struct nvgpu_device *gpu, uint8_t page_shift,
    uint64_t size)
{
	return;
}

static void
nvgpu_vm_bind_note_promote_split(struct nvgpu_device *gpu, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_binding_find_promote_64k()
 *
 * Ownership:
 *   Borrows one live binding and its BO.  It does not acquire or release GEM
 *   references, BO pins, or VMM ownership.
 *
 * Lifetime:
 *   The predicate is valid only while the caller holds vm->vm_token and the
 *   binding remains live.  Returned ranges are immediate values copied from
 *   the binding and BO.  The binding's existing VM_BIND pin keeps the BO
 *   physical run stable for the promotion decision.
 *
 * Threading:
 *   Pure VM_BIND normalize helper.  Hardware PTEs are not inspected here; the
 *   mapping tree is the software source of truth.
 */
static bool
nvgpu_vm_binding_find_promote_64k(
    const struct nvgpu_vm_binding *binding, bool allow_host_large,
    uint64_t *pmid_addr, uint64_t *pmid_size, uint64_t *pmid_bo_offset,
    uint64_t *ppaddr)
{
	const struct nvgpu_bo *bo;
	vm_paddr_t paddr;
	uint64_t mask = NVGPU_VM_PAGE_SIZE_64K - 1;
	uint64_t old_end, mid_start, mid_end, mid_size, mid_bo_offset;
	uint64_t run_size;
	int err;

	if (binding == NULL || !binding->pte_installed ||
	    !binding->bo_pinned)
		return (false);
	if (binding->page_shift != NVGPU_VM_PAGE_SHIFT_4K)
		return (false);
	if (binding->addr > UINT64_MAX - binding->size)
		return (false);
	if (binding->addr > UINT64_MAX - mask)
		return (false);
	old_end = binding->addr + binding->size;
	mid_start = (binding->addr + mask) & ~mask;
	mid_end = old_end & ~mask;
	if (mid_end <= mid_start)
		return (false);
	mid_size = mid_end - mid_start;
	if ((mid_start - binding->addr) > UINT64_MAX - binding->bo_offset)
		return (false);
	mid_bo_offset = binding->bo_offset + (mid_start - binding->addr);
	if (mid_bo_offset > nvgpu_bo_get_size(binding->bo) ||
	    mid_size > nvgpu_bo_get_size(binding->bo) - mid_bo_offset)
		return (false);
	bo = binding->bo;
	if (!nvgpu_bo_is_vram(bo) &&
	    !allow_host_large)
		return (false);
	err = nvgpu_bo_get_paddr_run_at(bo, mid_bo_offset, mid_size, &paddr,
	    &run_size);
	if (err != 0 || run_size < mid_size)
		return (false);
	if (paddr & mask)
		return (false);
	if ((mid_bo_offset | mid_size) & mask)
		return (false);
	*pmid_addr = mid_start;
	*pmid_size = mid_size;
	*pmid_bo_offset = mid_bo_offset;
	*ppaddr = paddr;
	return (true);
}

/*
 * nvgpu_vm_binding_prepare_clone()
 *
 * Ownership:
 *   Takes one GEM reference and one VM_BIND BO pin for the cloned live
 *   mapping.  The source binding is borrowed and keeps its own ownership.
 *
 * Lifetime:
 *   On success, *pbinding owns a detached binding node.  The caller must
 *   either insert it into vm's live VM tracker after hardware promotion
 *   succeeds, or free it with nvgpu_vm_binding_free().
 *
 * Threading:
 *   Called from VM_BIND normalize while vm->vm_token is held.  It may sleep
 *   while pinning the BO, so callers must run it before the promotion commit
 *   point that rewrites hardware PTEs.
 */
static int
nvgpu_vm_binding_prepare_clone(struct nvgpu_vm *vm,
    const struct nvgpu_vm_binding *source, uint64_t addr, uint64_t size,
    uint64_t bo_offset, uint8_t page_shift,
    struct nvgpu_vm_binding **pbinding)
{
	struct nvgpu_vm_binding *binding;
	int err;

	*pbinding = NULL;
	if (size == 0)
		return (0);
	nvgpu_bo_addref(source->bo);
	binding = nvgpu_vm_binding_alloc(vm, addr, size, source->bo,
	    bo_offset, source->pte_kind, page_shift);
	if (binding == NULL) {
		nvgpu_bo_release(source->bo);
		return (ENOMEM);
	}
	err = nvgpu_vm_binding_pin(binding);
	if (err != 0) {
		nvgpu_vm_binding_free(binding);
		return (err);
	}
	*pbinding = binding;
	return (0);
}

/*
 * nvgpu_vm_binding_promote_64k_one()
 *
 * Ownership:
 *   Mutates one live binding and may insert cloned middle/suffix bindings.
 *   Any clone takes its own GEM reference and VM_BIND pin before hardware PTEs
 *   are changed.  On VMM failure, all clones are freed and the source binding
 *   remains unchanged.
 *
 * Lifetime:
 *   The source binding stays live for the whole call.  After success, the
 *   software mapping ranges match the hardware shape: 4 KiB prefix/suffix
 *   remain SPT mappings and the promoted middle is an LPT mapping.
 *
 * Threading:
 *   Requires vm->vm_token and the caller's GSP/VMM serialization.  All
 *   fallible ownership work is complete before nvgsp_vmm_promote_* writes
 *   hardware PTEs; after that point, only deterministic tracker splices occur.
 */
static int
nvgpu_vm_binding_promote_64k_one(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_binding *binding,
    uint64_t mid_addr, uint64_t mid_size, uint64_t mid_bo_offset,
    uint64_t paddr)
{
	struct nvgpu_bo *bo = binding->bo;
	struct nvgpu_vm_bind_segment_plan sysmem_plan;
	struct nvgpu_vm_binding *middle = NULL, *suffix = NULL;
	uint64_t old_start = binding->addr;
	uint64_t old_end = binding->addr + binding->size;
	uint64_t mid_end = mid_addr + mid_size;
	uint64_t prefix_size = mid_addr - old_start;
	uint64_t suffix_size = old_end - mid_end;
	uint64_t suffix_bo_offset = mid_bo_offset + mid_size;
	bool is_vram = nvgpu_bo_is_vram(bo);
	int err;

	nvgpu_vm_bind_segment_plan_init(&sysmem_plan);
	if (!is_vram) {
		err = nvgpu_vm_bind_segment_add_sysmem(&sysmem_plan, bo,
		    mid_addr, mid_size, mid_bo_offset, NVGPU_VM_PAGE_SHIFT_64K);
		if (err != 0)
			goto fail;
		KASSERT(sysmem_plan.count == 1,
		    ("nvgpu vm: sysmem promote64 segment count %u",
		    sysmem_plan.count));
	}

	if (prefix_size != 0) {
		err = nvgpu_vm_binding_prepare_clone(vm, binding,
		    mid_addr, mid_size, mid_bo_offset, NVGPU_VM_PAGE_SHIFT_64K,
		    &middle);
		if (err != 0)
			goto fail;
	}
	if (suffix_size != 0) {
		err = nvgpu_vm_binding_prepare_clone(vm, binding,
		    mid_end, suffix_size, suffix_bo_offset,
		    NVGPU_VM_PAGE_SHIFT_4K, &suffix);
		if (err != 0)
			goto fail;
	}

	if (is_vram) {
		err = nvgsp_vmm_promote_vram_64k_noflush(vm->backend,
		    mid_addr, paddr, mid_size, 0, 0, binding->pte_kind);
	} else {
		struct nvgpu_vm_bind_segment *segment =
		    &sysmem_plan.segments[0];

		err =
		    nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush(
		    vm->backend, segment->addr, segment->sysmem_paddrs,
		    segment->sysmem_page_count, binding->pte_kind,
		    NVGPU_VM_PAGE_SHIFT_64K);
	}
	if (err != 0)
		goto fail;

	if (prefix_size != 0) {
		binding->size = prefix_size;
		nvgpu_vm_binding_insert_sorted(vm, middle);
	} else {
		binding->size = mid_size;
		binding->page_shift = NVGPU_VM_PAGE_SHIFT_64K;
	}
	if (suffix != NULL)
		nvgpu_vm_binding_insert_sorted(vm, suffix);
	nvgpu_vm_bind_note_promote(gpu, NVGPU_VM_PAGE_SHIFT_64K, mid_size);
	if (prefix_size != 0 || suffix_size != 0)
		nvgpu_vm_bind_note_promote_split(gpu, mid_size);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND promote64 addr=0x%016jx size=0x%016jx bo=%p split=%u\n",
	    (uintmax_t)mid_addr, (uintmax_t)mid_size, binding->bo,
	    prefix_size != 0 || suffix_size != 0);
	nvgpu_vm_bind_segment_plan_fini(&sysmem_plan);
	return (0);

fail:
	if (middle != NULL)
		nvgpu_vm_binding_free(middle);
	if (suffix != NULL)
		nvgpu_vm_binding_free(suffix);
	nvgpu_vm_bind_segment_plan_fini(&sysmem_plan);
	return (err);
}

/*
 * nvgpu_vm_bindings_promote_64k()
 *
 * Ownership:
 *   Mutates eligible live bindings in vm's VM tracker.  Whole-binding
 *   promotion keeps ownership on the same binding; split-promotion allocates
 *   cloned middle/suffix bindings with independent GEM references and BO pins
 *   before hardware PTEs are changed.
 *
 * Lifetime:
 *   On success for a middle range, the hardware representation has been
 *   rewritten from 4 KiB SPT leaves to 64 KiB LPT leaves and the live mapping
 *   tree is split/updated before the enclosing VM_BIND publishes its final
 *   flush/fence.
 *
 * Threading:
 *   Requires vm->vm_token and the caller's GSP/VMM serialization.  This is
 *   a best-effort normalize step; fallible preparation or unexpected VMM
 *   mismatch is counted and skipped without changing the VM_BIND UAPI result.
 */
static bool
nvgpu_vm_bindings_promote_64k(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size)
{
	struct nvgpu_vm_binding *binding, *next;
	uint64_t raw_end, start, end;
	bool promoted = false;

	if (size == 0 || addr > UINT64_MAX - size)
		return (false);
	raw_end = addr + size;
	start = addr & ~(NVGPU_VM_PAGE_SIZE_64K - 1);
	if (raw_end > UINT64_MAX - (NVGPU_VM_PAGE_SIZE_64K - 1))
		end = raw_end;
	else
		end = (raw_end + NVGPU_VM_PAGE_SIZE_64K - 1) &
		    ~(NVGPU_VM_PAGE_SIZE_64K - 1);
	if (end <= start)
		return (false);

	for (binding = nvgpu_vm_binding_first_overlap(vm, start,
	    end - start); binding != NULL; binding = next) {
		uint64_t mid_addr = 0, mid_size = 0, mid_bo_offset = 0;
		uint64_t paddr = 0;
		int err;

		next = nvgpu_vm_binding_next_overlap(vm, binding,
		    start, end - start);
		if (!nvgpu_vm_binding_find_promote_64k(binding,
		    1 != 0,
		    &mid_addr, &mid_size, &mid_bo_offset, &paddr))
			continue;

		err = nvgpu_vm_binding_promote_64k_one(gpu, vm,
		    binding, mid_addr, mid_size, mid_bo_offset, paddr);
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND promote64 skipped addr=0x%016jx size=0x%016jx err=%d\n",
			    (uintmax_t)binding->addr,
			    (uintmax_t)binding->size, err);
			continue;
		}
		promoted = true;
	}
	return (promoted);
}

/*
 * nvgpu_vm_binding_find_promote_2m()
 *
 * Ownership:
 *   Borrows one live 64 KiB binding and its BO.  It takes no GEM reference,
 *   BO pin, or VMM ownership; the binding's existing VM_BIND pin keeps the
 *   physical run stable while the caller holds VM_BIND serialization.
 *
 * Lifetime:
 *   Returned range values are immediate copies.  The binding pointer remains
 *   borrowed and must stay live until the caller either skips promotion or
 *   completes the corresponding tracker splice.
 *
 * Threading:
 *   Pure normalize predicate under vm->vm_token.  It uses the BO physical
 *   run helper so a 2 MiB promotion never crosses a discontiguous backing run.
 */
static bool
nvgpu_vm_binding_find_promote_2m(
    const struct nvgpu_vm_binding *binding, bool allow_host_large,
    uint64_t *pmid_addr, uint64_t *pmid_size, uint64_t *pmid_bo_offset,
    uint64_t *ppaddr)
{
	const struct nvgpu_bo *bo;
	vm_paddr_t paddr;
	uint64_t mask = (1ULL << NVGPU_VM_PAGE_SHIFT_2M) - 1;
	uint64_t old_end, mid_start, mid_end, mid_size, mid_bo_offset;
	uint64_t run_size;
	int err;

	if (binding == NULL || !binding->pte_installed ||
	    !binding->bo_pinned)
		return (false);
	if (binding->page_shift != NVGPU_VM_PAGE_SHIFT_64K)
		return (false);
	if (binding->addr > UINT64_MAX - binding->size)
		return (false);
	if (binding->addr > UINT64_MAX - mask)
		return (false);
	old_end = binding->addr + binding->size;
	mid_start = (binding->addr + mask) & ~mask;
	mid_end = old_end & ~mask;
	if (mid_end <= mid_start)
		return (false);
	mid_size = mid_end - mid_start;
	if ((mid_start - binding->addr) > UINT64_MAX - binding->bo_offset)
		return (false);
	mid_bo_offset = binding->bo_offset + (mid_start - binding->addr);
	if (mid_bo_offset > nvgpu_bo_get_size(binding->bo) ||
	    mid_size > nvgpu_bo_get_size(binding->bo) - mid_bo_offset)
		return (false);
	bo = binding->bo;
	if (!nvgpu_bo_is_vram(bo) &&
	    !allow_host_large)
		return (false);
	err = nvgpu_bo_get_paddr_run_at(bo, mid_bo_offset, mid_size, &paddr,
	    &run_size);
	if (err != 0 || run_size < mid_size)
		return (false);
	if (paddr & mask)
		return (false);
	if ((mid_bo_offset | mid_size) & mask)
		return (false);

	*pmid_addr = mid_start;
	*pmid_size = mid_size;
	*pmid_bo_offset = mid_bo_offset;
	*ppaddr = paddr;
	return (true);
}

/*
 * nvgpu_vm_binding_promote_2m_one()
 *
 * Ownership:
 *   Mutates one live 64 KiB binding and may insert cloned middle/suffix
 *   bindings.  Any clone takes its own GEM reference and VM_BIND pin before
 *   hardware page tables are changed.  On VMM failure, all clones are freed
 *   and the source binding remains unchanged.
 *
 * Lifetime:
 *   After success, software mapping shape and hardware page-table shape are
 *   isomorphic: LPT prefix/suffix remain 64 KiB mappings, and the promoted
 *   middle is a 2 MiB PD0 mapping.
 *
 * Threading:
 *   Requires vm->vm_token and VM_BIND serialization.  Fallible ownership
 *   work finishes before the VMM promote writer rewrites the child table into
 *   a PD0 leaf.  HOST/GART promotion snapshots paddr runs here and remains
 *   behind the host-large gate.
 */
static int
nvgpu_vm_binding_promote_2m_one(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_binding *binding,
    uint64_t mid_addr, uint64_t mid_size, uint64_t mid_bo_offset,
    uint64_t paddr)
{
	struct nvgpu_bo *bo = binding->bo;
	struct nvgpu_vm_bind_segment_plan sysmem_plan;
	struct nvgpu_vm_binding *middle = NULL, *suffix = NULL;
	uint64_t old_start = binding->addr;
	uint64_t old_end = binding->addr + binding->size;
	uint64_t mid_end = mid_addr + mid_size;
	uint64_t prefix_size = mid_addr - old_start;
	uint64_t suffix_size = old_end - mid_end;
	uint64_t suffix_bo_offset = mid_bo_offset + mid_size;
	bool is_vram = nvgpu_bo_is_vram(bo);
	int err;

	nvgpu_vm_bind_segment_plan_init(&sysmem_plan);
	if (!is_vram) {
		err = nvgpu_vm_bind_segment_add_sysmem(&sysmem_plan, bo,
		    mid_addr, mid_size, mid_bo_offset, NVGPU_VM_PAGE_SHIFT_2M);
		if (err != 0)
			goto fail;
		KASSERT(sysmem_plan.count == 1,
		    ("nvgpu vm: sysmem promote2m segment count %u",
		    sysmem_plan.count));
	}

	if (prefix_size != 0) {
		err = nvgpu_vm_binding_prepare_clone(vm, binding,
		    mid_addr, mid_size, mid_bo_offset, NVGPU_VM_PAGE_SHIFT_2M,
		    &middle);
		if (err != 0)
			goto fail;
	}
	if (suffix_size != 0) {
		err = nvgpu_vm_binding_prepare_clone(vm, binding,
		    mid_end, suffix_size, suffix_bo_offset,
		    NVGPU_VM_PAGE_SHIFT_64K, &suffix);
		if (err != 0)
			goto fail;
	}

	if (is_vram) {
		err = nvgsp_vmm_promote_vram_2m_noflush(vm->backend,
		    mid_addr, paddr, mid_size, 0, 0, binding->pte_kind);
	} else {
		struct nvgpu_vm_bind_segment *segment =
		    &sysmem_plan.segments[0];

		err = nvgsp_vmm_promote_sysmem_2m_noflush(vm->backend,
		    segment->addr, segment->sysmem_paddrs,
		    segment->sysmem_page_count, binding->pte_kind);
	}
	if (err != 0)
		goto fail;

	if (prefix_size != 0) {
		binding->size = prefix_size;
		nvgpu_vm_binding_insert_sorted(vm, middle);
	} else {
		binding->size = mid_size;
		binding->page_shift = NVGPU_VM_PAGE_SHIFT_2M;
	}
	if (suffix != NULL)
		nvgpu_vm_binding_insert_sorted(vm, suffix);
	nvgpu_vm_bind_note_promote(gpu, NVGPU_VM_PAGE_SHIFT_2M, mid_size);
	if (prefix_size != 0 || suffix_size != 0)
		nvgpu_vm_bind_note_promote_split(gpu, mid_size);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND promote2m addr=0x%016jx size=0x%016jx bo=%p split=%u\n",
	    (uintmax_t)mid_addr, (uintmax_t)mid_size, binding->bo,
	    prefix_size != 0 || suffix_size != 0);
	nvgpu_vm_bind_segment_plan_fini(&sysmem_plan);
	return (0);

fail:
	if (middle != NULL)
		nvgpu_vm_binding_free(middle);
	if (suffix != NULL)
		nvgpu_vm_binding_free(suffix);
	nvgpu_vm_bind_segment_plan_fini(&sysmem_plan);
	return (err);
}

/*
 * nvgpu_vm_bindings_promote_2m()
 *
 * Ownership:
 *   Mutates eligible live mappings when explicitly enabled.  It is part of
 *   the 2 MiB feature gate: the code is compiled and type-checked, but the
 *   caller keeps it out of the hot path until partial 2 MiB materialize and
 *   runtime validation are complete.
 *
 * Lifetime:
 *   Successful ranges are converted from 64 KiB LPT mappings to 2 MiB PD0
 *   mappings before the enclosing VM_BIND publishes its final flush/fence.
 *
 * Threading:
 *   Requires vm->vm_token and VM_BIND serialization.  Promotion is
 *   best-effort; failures increment diagnostics and preserve VM_BIND UAPI
 *   result semantics.
 */
static bool
nvgpu_vm_bindings_promote_2m(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size)
{
	struct nvgpu_vm_binding *binding, *next;
	uint64_t page_size = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	uint64_t raw_end, start, end;
	bool promoted = false;

	if (size == 0 || addr > UINT64_MAX - size)
		return (false);
	raw_end = addr + size;
	start = addr & ~(page_size - 1);
	if (raw_end > UINT64_MAX - (page_size - 1))
		end = raw_end;
	else
		end = (raw_end + page_size - 1) & ~(page_size - 1);
	if (end <= start)
		return (false);

	for (binding = nvgpu_vm_binding_first_overlap(vm, start,
	    end - start); binding != NULL; binding = next) {
		uint64_t mid_addr = 0, mid_size = 0, mid_bo_offset = 0;
		uint64_t paddr = 0;
		int err;

		next = nvgpu_vm_binding_next_overlap(vm, binding,
		    start, end - start);
		if (!nvgpu_vm_binding_find_promote_2m(binding,
		    1 != 0,
		    &mid_addr, &mid_size, &mid_bo_offset, &paddr))
			continue;

		err = nvgpu_vm_binding_promote_2m_one(gpu, vm,
		    binding, mid_addr, mid_size, mid_bo_offset, paddr);
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND promote2m skipped addr=0x%016jx size=0x%016jx err=%d\n",
			    (uintmax_t)binding->addr,
			    (uintmax_t)binding->size, err);
			continue;
		}
		promoted = true;
	}
	return (promoted);
}

/*
 * nvgpu_vm_bindings_normalize()
 *
 * Ownership:
 *   Borrows the VM_BIND remap range and mutates vm's live mapping tracker
 *   only when an already-installed valid mapping can be merged or promoted.
 *   Merged-away bindings are moved to retired_bindings and keep their BO/GEM
 *   ownership until the caller publishes the VM_BIND completion fence.
 *
 * Lifetime:
 *   Call after the current remap has made hardware PTE/PDE state and software
 *   mappings isomorphic.  The helper may rewrite eligible valid PTEs to a
 *   larger page class before the caller's final flush/invalidate boundary.
 *
 * Threading:
 *   Requires vm->vm_token and the caller's VMM/GSP serialization.  This is
 *   the common remap normalize tail for MAP, UNMAP, MAP_NULL, and MAP_SPARSE.
 */
static bool
nvgpu_vm_bindings_normalize(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	bool promoted = false;

	if (size == 0 || addr > UINT64_MAX - size)
		return (false);

	nvgpu_vm_bindings_merge_neighbors(gpu, vm, addr, size,
	    retired_bindings);
	if (nvgpu_vm_bindings_promote_64k(gpu, vm, addr, size)) {
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set, addr, size);
		promoted = true;
		nvgpu_vm_bindings_merge_neighbors(gpu, vm, addr, size,
		    retired_bindings);
	}
	if (1 != 0 &&
	    nvgpu_vm_bindings_promote_2m(gpu, vm, addr, size)) {
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set, addr, size);
		promoted = true;
		nvgpu_vm_bindings_merge_neighbors(gpu, vm, addr, size,
		    retired_bindings);
	}
	return (promoted);
}

/*
 * nvgpu_vm_binding_reclaim_noflush()
 *
 * Ownership:
 *   Moves one live VM binding out of the VM tree/list and BO reverse list into
 *   caller-owned release_bindings.  The binding keeps owning its GEM reference
 *   and VM_BIND pin until the caller releases that detached list.
 *
 * Lifetime:
 *   Used by file release after channels and queued jobs for the file have been
 *   closed.  A successful PTE clear is recorded in dirty_set for the caller's
 *   final flush.  If the clear reports an invariant error, the caller still
 *   destroys this file's VMM immediately afterwards, so the binding ownership
 *   must not be leaked.
 *
 * Threading:
 *   The caller holds the file VM token and then the GSP/VMM mutation token for
 *   live tree/list and PTE mutation.  This helper must not release GEM refs or
 *   VM_BIND pins because unpinning may reserve the TTM BO; release_bindings is
 *   consumed only after the caller drops VM/GSP tokens.  The helper does not
 *   flush; the caller publishes dirty PTE writes once after the release batch.
 */
static int
nvgpu_vm_binding_reclaim_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm_binding *binding,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *release_bindings)
{
	int err = 0;

	nvgpu_vm_binding_assert(binding);

	if (binding->pte_installed) {
		err = nvgsp_vmm_unmap_valid_page_noflush(binding->owner->backend,
		    binding->addr, binding->size, binding->page_shift);
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND unmap failed addr=0x%016jx size=0x%016jx bo=%p err=%d\n",
			    (uintmax_t)binding->addr,
			    (uintmax_t)binding->size, binding->bo, err);
		} else {
			binding->pte_installed = false;
			nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
			    binding->addr, binding->size);
		}
	}

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND reclaim addr=0x%016jx size=0x%016jx bo=%p err=%d\n",
	    (uintmax_t)binding->addr, (uintmax_t)binding->size,
	    binding->bo, err);
	nvgpu_vm_binding_unlink_retire(binding, release_bindings);
	return (err);
}

/*
 * nvgpu_vm_bind_note_empty_clear_skip()
 *
 * Ownership:
 *   Borrows gpu for counter updates only; it does not acquire references to the
 *   VMM, GEM bos, or VM binding records.
 *
 * Lifetime:
 *   The range is an ioctl-local value that remains owned by the caller.  The
 *   helper stores only aggregate diagnostic counters.
 *
 * Threading:
 *   Called from VM_BIND mutation paths while the caller holds the drm_file's
 *   vm_token.  Counters follow the driver's existing best-effort debug counter
 *   model and are not part of the ABI.
 */
static void
nvgpu_vm_bind_note_empty_clear_skip(struct nvgpu_device *gpu, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_note_replace_clear_skip()
 *
 * Ownership:
 *   Borrows gpu for diagnostic counter updates only.  It does not acquire or
 *   release VM binding, VMM, or GEM ownership.
 *
 * Lifetime:
 *   The byte range is caller-owned and used only to update aggregate counters.
 *
 * Threading:
 *   Called while the drm_file VM token serializes VM_BIND mutations.  Counters
 *   follow the driver's best-effort debug accounting and are not UAPI.
 */
static void
nvgpu_vm_bind_note_replace_clear_skip(struct nvgpu_device *gpu, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_size_bucket()
 *
 * Ownership:
 *   Borrows no bos and changes no state. The pages value is copied by
 *   value and is only used for debug-counter classification.
 *
 * Lifetime:
 *   The returned bucket is an immediate value. It carries no reference to VM,
 *   VMM, or GEM state.
 *
 * Threading:
 *   Pure helper; callers provide any required serialization for the counters
 *   they update with the returned bucket.
 */
static uint32_t
nvgpu_vm_bind_size_bucket(uint64_t pages)
{
	uint32_t bucket = 0;

	if (pages == 0)
		return (0);
	pages--;
	while (pages != 0 &&
	    bucket + 1 < NVGPU_VM_BIND_SIZE_BUCKET_COUNT) {
		pages >>= 1;
		bucket++;
	}
	return (bucket);
}

/*
 * nvgpu_vm_bind_note_map_shape()
 *
 * Ownership:
 *   Borrows gpu for debug counter updates only. It does not acquire or release
 *   VM_BIND, VMM, GEM, or BO ownership.
 *
 * Lifetime:
 *   The range and flags are copied into aggregate counters only. No pointer
 *   or user-provided state is retained.
 *
 * Threading:
 *   Called after a MAP PTE write succeeds while VM_BIND serialization is held.
 *   Counters are best-effort diagnostics and are not part of the ABI.
 */
static void
nvgpu_vm_bind_note_map_shape(struct nvgpu_device *gpu, uint32_t flags,
    uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_note_clear_shape()
 *
 * Ownership:
 *   Borrows gpu and the pte_kind copied from a live binding. It does not own
 *   or retain the binding, GEM object, or VMM range.
 *
 * Lifetime:
 *   The covered range is reduced to aggregate debug counters before return.
 *   No live-binding state escapes this call.
 *
 * Threading:
 *   Called immediately before clearing tracked valid PTEs while the drm_file
 *   VM token serializes live-binding mutations. Counters are diagnostics only.
 */
static void
nvgpu_vm_bind_note_clear_shape(struct nvgpu_device *gpu, const struct nvgpu_bo *bo,
    uint8_t pte_kind, uint8_t page_shift, uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_note_sparse_clear_shape()
 *
 * Ownership:
 *   Borrows gpu through the callback arg and receives one prepared sparse-clear
 *   range by value.  It does not own or retain any VMM sparse-unmap plan state.
 *
 * Lifetime:
 *   The scalar range values are consumed immediately into aggregate counters.
 *   No pointer from the VMM plan escapes the callback.
 *
 * Threading:
 *   Called after a sparse clear commit succeeds while VM_BIND serialization is
 *   still held.  Counters are diagnostics only and do not affect ABI or fence
 *   semantics.
 */
static void
nvgpu_vm_bind_note_sparse_clear_shape(void *arg, uint64_t addr __unused,
    uint64_t size, uint8_t page_shift)
{
	return;
}

/*
 * nvgpu_vm_bind_note_sparse_clear_shapes()
 *
 * Ownership:
 *   Borrows the committed sparse-unmap plan before fini consumes it.  The VMM
 *   plan remains owned by the caller.
 *
 * Lifetime:
 *   Must be called after successful sparse clear commit and before
 *   nvgsp_vmm_fini_unmap_sparse_range() releases the plan.  Only scalar
 *   page-shift counters survive the call.
 *
 * Threading:
 *   Runs inside the same VM_BIND commit serialization as the sparse clear.  It
 *   only updates debug counters and does not touch hardware PTEs.
 */
static void
nvgpu_vm_bind_note_sparse_clear_shapes(struct nvgpu_device *gpu,
    const struct nvgsp_vmm_sparse_unmap_plan *plan)
{
	return;
}

/*
 * nvgpu_vm_bind_note_op()
 *
 * Ownership:
 *   Borrows gpu for aggregate diagnostics.  No VM_BIND, BO, or VMM ownership is
 *   changed.
 *
 * Lifetime:
 *   The range is caller-owned and not retained.
 *
 * Threading:
 *   Called while a VM_BIND ioctl/job is applying under the per-file VM token.
 */
static void
nvgpu_vm_bind_note_op(struct nvgpu_device *gpu, uint32_t action,
    uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_note_clear()
 *
 * Ownership:
 *   Borrows gpu for aggregate diagnostics only.
 *
 * Lifetime:
 *   The range is caller-owned and not retained.
 *
 * Threading:
 *   Called immediately before remove_range writes invalid/sparse PTEs for a
 *   tracked overlap, while VM_BIND serialization is held.
 */
static void
nvgpu_vm_bind_note_clear(struct nvgpu_device *gpu, uint32_t action,
    uint64_t size)
{
	return;
}

/*
 * nvgpu_vm_bind_clear_is_conflict()
 *
 * Ownership:
 *   Consumes only the scalar VM trace action.  It does not inspect or retain
 *   VM, BO, sparse-plan, or mapping ownership.
 *
 * Lifetime:
 *   The returned boolean is valid for the caller's current remap operation:
 *   MAP and MAP_SPARSE clear old state only to make room for a final target
 *   leaf, while UNMAP, MAP_NULL, and UNMAP_SPARSE are semantic final invalid.
 *
 * Threading:
 *   Pure helper; no locks, waits, or side effects.
 */
static bool
nvgpu_vm_bind_clear_is_conflict(uint32_t action)
{
	return (action == NVGPU_VM_TRACE_MAP ||
	    action == NVGPU_VM_TRACE_MAP_SPARSE);
}

/*
 * nvgpu_vm_bind_segments_min_page_shift()
 *
 * Ownership:
 *   Borrows a caller-owned segment plan and returns a scalar page class.  It
 *   does not retain the plan or inspect BO/VMM state.
 *
 * Lifetime:
 *   The returned shift is valid only for the current remap plan.  It is used
 *   to cap sparse-clear page size so later prepared target writers can consume
 *   child PT storage produced by the same plan.
 *
 * Threading:
 *   Pure helper.  The caller owns VM_BIND serialization and segment lifetime.
 */
static uint8_t
nvgpu_vm_bind_segments_min_page_shift(
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count)
{
	uint8_t page_shift = NVGPU_VM_PAGE_SHIFT_2M;

	for (uint32_t i = 0; i < segment_count; i++) {
		if (segments[i].page_shift < page_shift)
			page_shift = segments[i].page_shift;
	}
	return (page_shift);
}

enum nvgpu_vm_sparse_clear_mode {
	NVGPU_VM_SPARSE_CLEAR_FINAL,
	NVGPU_VM_SPARSE_CLEAR_TARGET_OVERWRITE,
	NVGPU_VM_SPARSE_CLEAR_METADATA_ONLY,
};

/*
 * nvgpu_vm_bind_prepare_sparse_clear()
 *
 * Ownership:
 *   Prepares a caller-owned sparse clear/removal plan for the range.  FINAL
 *   plans own invalid PTE writes; TARGET_OVERWRITE plans own only sparse
 *   metadata removal plus child storage needed by the following target writer;
 *   METADATA_ONLY plans own stale sparse metadata removal only.
 *
 * Lifetime:
 *   The caller must keep VM remap serialization until commit or fini because
 *   the plan borrows old sparse-region pointers from the VMM.  A range with no
 *   sparse regions returns success with *pplan == NULL.  clear_page_shift caps
 *   the cut range page size when a later valid/sparse target writer needs
 *   lower child PT storage from the same plan.  METADATA_ONLY is used only
 *   after exact valid-map no-op proof, so it must not dirty hardware PTE/PDEs.
 *
 * Threading:
 *   Called before PTE/PDE mutation for the enclosing op.  It may allocate
 *   inside the VMM prepare path, but performs no GEM lookup, BO pinning, or
 *   fence waits.
 */
static int
nvgpu_vm_bind_prepare_sparse_clear(struct nvgpu_vm *vm,
	    uint64_t addr, uint64_t size, uint8_t clear_page_shift,
	    enum nvgpu_vm_sparse_clear_mode mode,
	    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	int err;

	switch (mode) {
	case NVGPU_VM_SPARSE_CLEAR_FINAL:
		err = nvgsp_vmm_prepare_unmap_sparse_range_page(vm->backend,
		    addr, size, clear_page_shift, 0, pplan);
		break;
	case NVGPU_VM_SPARSE_CLEAR_TARGET_OVERWRITE:
		err = nvgsp_vmm_prepare_overwrite_sparse_range_page(vm->backend,
		    addr, size, clear_page_shift, pplan);
		break;
	case NVGPU_VM_SPARSE_CLEAR_METADATA_ONLY:
		err = nvgsp_vmm_prepare_metadata_sparse_range(vm->backend,
		    addr, size, pplan);
		break;
	default:
		return (EINVAL);
	}
	if (err != 0)
		return (err);
	return (0);
}

/*
 * nvgpu_vm_bind_commit_sparse_clear_noflush()
 *
 * Ownership:
 *   Consumes the prepared sparse clear plan on success and releases plan
 *   storage before return.  Passing a NULL plan is a no-op.
 *
 * Lifetime:
 *   Sparse PTE/PDE clears become visible only after the enclosing VM_BIND
 *   flush/TLB invalidate.  On failure, remaining prepared bos are released
 *   before returning the negative errno.
 *
 * Threading:
 *   Called in the nofail VM_BIND commit section.  It does not allocate, lookup
 *   GEM handles, pin BOs, or wait on fences.
 */
static int
nvgpu_vm_bind_commit_sparse_clear_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    struct nvgsp_vmm_sparse_unmap_plan **pplan, uint32_t action,
    uint64_t addr, uint64_t size, struct nvgpu_vm_dirty_set *dirty_set)
{
	struct nvgsp_vmm_sparse_unmap_plan *plan = *pplan;
	bool wrote_hw;
	int err;

	if (plan == NULL)
		return (0);
	if (nvgpu_vm_bind_clear_is_conflict(action)) {
		err = nvgsp_vmm_commit_unmap_sparse_range_conflict_noflush(
		    vm->backend, plan);
	} else {
		err = nvgsp_vmm_commit_unmap_sparse_range_noflush(
		    vm->backend, plan);
	}
	if (err != 0) {
		nvgsp_vmm_fini_unmap_sparse_range(vm->backend, plan);
		*pplan = NULL;
		return (err);
	}
	wrote_hw = nvgsp_vmm_sparse_unmap_plan_wrote_hw(plan);
	if (wrote_hw)
		nvgpu_vm_bind_note_sparse_clear_shapes(gpu, plan);
	nvgsp_vmm_fini_unmap_sparse_range(vm->backend, plan);
	*pplan = NULL;
	if (wrote_hw) {
		nvgpu_vm_bind_note_clear(gpu, action, size);
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set, addr, size);
	}
	return (0);
}

static void
nvgpu_vm_bind_abort_sparse_clear(struct nvgpu_vm *vm,
    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	if (*pplan == NULL)
		return;
	nvgsp_vmm_fini_unmap_sparse_range(vm->backend, *pplan);
	*pplan = NULL;
}

/*
 * nvgpu_vm_materialize_entry_add_4k_keep_range()
 *
 * Ownership:
 *   Borrows one live large-page binding and appends a prepared SPT segment for
 *   a target-outside keep range.  The helper does not publish mapping ownership
 *   or write PTEs; the enclosing materialize entry owns the segment plan.
 *
 * Lifetime:
 *   Only keep ranges may be passed here.  Target-middle ranges that will become
 *   final valid, sparse, or invalid are deliberately skipped so split and final
 *   install/clear do not rewrite the same VA.
 *
 * Threading:
 *   Runs in VM_BIND prepare under vm->vm_token.  It may snapshot BO backing
 *   and allocate segment storage, but performs no BAR1 writes and waits on no
 *   GPU work.
 */
static int
nvgpu_vm_materialize_entry_add_4k_keep_range(
    struct nvgpu_vm_materialize_entry *entry, struct nvgpu_bo *bo,
    const struct nvgpu_vm_binding *binding, uint64_t keep_start,
    uint64_t keep_end)
{
	uint64_t bo_offset, keep_size;
	int err;

	if (keep_start >= keep_end)
		return (0);
	if (keep_start < binding->addr)
		return (EINVAL);
	keep_size = keep_end - keep_start;
	bo_offset = binding->bo_offset + (keep_start - binding->addr);
	if ((keep_start | keep_size | bo_offset) &
	    (NVGPU_VM_PAGE_SIZE_4K - 1))
		return (EINVAL);

	if (nvgpu_bo_is_vram(bo)) {
		vm_paddr_t paddr;
		uint64_t run_size;

		err = nvgpu_bo_get_paddr_run_at(bo, bo_offset, keep_size, &paddr,
		    &run_size);
		if (err != 0 || run_size < keep_size)
			return (err != 0 ? -err : -EIO);
		return (nvgpu_vm_bind_segment_add(&entry->segments,
		    keep_start, keep_size, bo_offset, paddr,
		    NVGPU_VM_PAGE_SHIFT_4K));
	}

	return (nvgpu_vm_bind_segment_add_sysmem(&entry->segments, bo,
	    keep_start, keep_size, bo_offset, NVGPU_VM_PAGE_SHIFT_4K));
}

static int
nvgpu_vm_materialize_entry_prepare_4k(struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_entry *entry,
    struct nvgpu_vm_binding *binding, uint64_t target_addr,
    uint64_t target_size)
{
	struct nvgpu_bo *bo = binding->bo;
	uint64_t old_start, old_end, target_end, cut_start, cut_end;
	int err;

	entry->binding = binding;
	entry->is_2m = false;
	if (binding->page_shift <= NVGPU_VM_PAGE_SHIFT_4K)
		return (0);
	if (binding->size == 0 || target_size == 0 ||
	    binding->addr > UINT64_MAX - binding->size ||
	    target_addr > UINT64_MAX - target_size)
		return (EINVAL);

	old_start = binding->addr;
	old_end = old_start + binding->size;
	target_end = target_addr + target_size;
	cut_start = old_start > target_addr ? old_start : target_addr;
	cut_end = old_end < target_end ? old_end : target_end;
	if (cut_start >= cut_end)
		return (nvgpu_vm_materialize_entry_add_4k_keep_range(entry,
		    bo, binding, old_start, old_end));

	err = nvgpu_vm_materialize_entry_add_4k_keep_range(entry, bo,
	    binding, old_start, cut_start);
	if (err != 0)
		return (err);
	err = nvgpu_vm_materialize_entry_add_4k_keep_range(entry, bo,
	    binding, cut_end, old_end);
	if (err != 0)
		return (err);
	(void)vm;
	return (0);
}

/*
 * nvgpu_vm_materialize_entry_add_vram_keep_range()
 *
 * Ownership:
 *   Borrows a live VRAM binding and appends prepared segment descriptors for
 *   the old BO range that must survive a remap.  The helper does not pin,
 *   retain, or publish mapping ownership; the enclosing materialize entry owns
 *   the segment plan.
 *
 * Lifetime:
 *   Generated segments describe only target-outside keep ranges.  Target-middle
 *   ranges that will be overwritten by valid/sparse or cleared by final invalid
 *   are deliberately skipped so split and install do not rewrite the same VA.
 *
 * Threading:
 *   Runs in VM_BIND prepare while vm->vm_token serializes the binding tree.
 *   It only snapshots VRAM physical runs and may not write PTEs/PDEs.
 */
static int
nvgpu_vm_materialize_entry_add_vram_keep_range(
    struct nvgpu_vm_materialize_entry *entry, struct nvgpu_bo *bo,
    const struct nvgpu_vm_binding *binding, uint64_t keep_start,
    uint64_t keep_end)
{
	uint64_t page_64k = NVGPU_VM_PAGE_SIZE_64K;
	uint64_t old_start = binding->addr;
	uint64_t cur;
	int err;

	for (cur = keep_start; cur < keep_end;) {
		uint64_t bo_offset = binding->bo_offset + (cur - old_start);
		uint64_t remaining = keep_end - cur;
		uint64_t chunk, next_64k;
		uint8_t page_shift;
		vm_paddr_t paddr;
		uint64_t run_size;

		if (((cur | bo_offset) & (page_64k - 1)) == 0 &&
		    remaining >= page_64k) {
			chunk = page_64k;
			page_shift = NVGPU_VM_PAGE_SHIFT_64K;
		} else {
			next_64k = (cur + page_64k) & ~(page_64k - 1);
			if (next_64k <= cur)
				next_64k = cur + page_64k;
			chunk = MIN(remaining, next_64k - cur);
			chunk &= ~(NVGPU_VM_PAGE_SIZE_4K - 1);
			if (chunk == 0)
				chunk = NVGPU_VM_PAGE_SIZE_4K;
			page_shift = NVGPU_VM_PAGE_SHIFT_4K;
		}

		err = nvgpu_bo_get_paddr_run_at(bo, bo_offset, chunk, &paddr,
		    &run_size);
		if (err != 0 || run_size < chunk)
			return (err != 0 ? -err : -EIO);
		err = nvgpu_vm_bind_segment_add(&entry->segments, cur,
		    chunk, bo_offset, paddr, page_shift);
		if (err != 0)
			return (err);
		cur += chunk;
	}
	return (0);
}

static int
nvgpu_vm_materialize_entry_prepare_2m(struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_entry *entry,
    struct nvgpu_vm_binding *binding, uint64_t target_addr,
    uint64_t target_size, bool materialize_full_cover)
{
	struct nvgpu_bo *bo = binding->bo;
	uint64_t page_2m = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	uint64_t page_64k = NVGPU_VM_PAGE_SIZE_64K;
	uint64_t old_start = binding->addr;
	uint64_t old_end;
	uint64_t target_end;
	uint64_t window;
	int err;

	entry->binding = binding;
	entry->is_2m = true;
	if (!nvgpu_bo_is_vram(bo))
		return (nvgpu_vm_materialize_entry_prepare_4k(vm,
		    entry, binding, target_addr, target_size));
	if (binding->page_shift != NVGPU_VM_PAGE_SHIFT_2M)
		return (nvgpu_vm_materialize_entry_prepare_4k(vm,
		    entry, binding, target_addr, target_size));
	if (binding->size == 0 || target_size == 0 ||
	    old_start > UINT64_MAX - binding->size ||
	    target_addr > UINT64_MAX - target_size)
		return (EINVAL);
	old_end = old_start + binding->size;
	target_end = target_addr + target_size;
	if ((old_start | binding->size | binding->bo_offset) &
	    (page_2m - 1))
		return (EINVAL);
	(void)materialize_full_cover;

	for (window = old_start; window < old_end; window += page_2m) {
		uint64_t window_end = window + page_2m;
		uint64_t cut_start = window > target_addr ? window :
		    target_addr;
		uint64_t cut_end = window_end < target_end ? window_end :
		    target_end;
		uint64_t window_bo_offset = binding->bo_offset +
		    (window - old_start);

		if (cut_start >= cut_end) {
			vm_paddr_t paddr;
			uint64_t run_size;

			err = nvgpu_bo_get_paddr_run_at(bo, window_bo_offset,
			    page_2m, &paddr, &run_size);
			if (err != 0 || run_size < page_2m)
				return (err != 0 ? -err : -EIO);
			err = nvgpu_vm_bind_segment_add(&entry->segments,
			    window, page_2m, window_bo_offset, paddr,
			    NVGPU_VM_PAGE_SHIFT_2M);
			if (err != 0)
				return (err);
			continue;
		}

		err = nvgpu_vm_bind_2m_split_plan_prepare(vm->backend,
		    &entry->split_plan, window);
		if (err != 0)
			return (err);
		if (window < cut_start) {
			err = nvgpu_vm_materialize_entry_add_vram_keep_range(
			    entry, bo, binding, window, cut_start);
			if (err != 0)
				return (err);
		}
		if (cut_end < window_end) {
			err = nvgpu_vm_materialize_entry_add_vram_keep_range(
			    entry, bo, binding, cut_end, window_end);
			if (err != 0)
				return (err);
		}
	}

	err = nvgpu_vm_bind_prepare_segment_bindings(vm, binding->bo,
	    entry->segments.segments, entry->segments.count,
	    binding->pte_kind, &entry->new_bindings);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < entry->segments.count; i++) {
		uint64_t split = entry->segments.segments[i].addr &
		    ~(page_2m - 1);

		if (entry->segments.segments[i].page_shift ==
		    NVGPU_VM_PAGE_SHIFT_2M)
			continue;
		err = nvgpu_vm_bind_2m_split_plan_prepare(vm->backend,
		    &entry->split_plan, split);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_materialize_entry_prepare(struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_entry *entry,
    struct nvgpu_vm_binding *binding, uint64_t target_addr,
    uint64_t target_size, bool materialize_full_cover)
{
	if (binding->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
		return (nvgpu_vm_materialize_entry_prepare_2m(vm,
		    entry, binding, target_addr, target_size,
		    materialize_full_cover));
	return (nvgpu_vm_materialize_entry_prepare_4k(vm, entry,
	    binding, target_addr, target_size));
}

/*
 * nvgpu_vm_materialize_entry_preflight()
 *
 * Ownership:
 *   Borrows one prepared materialize entry and the file VMM.  It does not
 *   consume detached bindings, prepared split PTs, paddr snapshots, or live
 *   mapping ownership.
 *
 * Lifetime:
 *   The result is valid while VM_BIND serialization keeps the prepared entry
 *   and live VMM page-table state stable.  A successful result means the
 *   later entry commit should not discover missing target PTs or invalid 2 MiB
 *   split state after another entry has already mutated hardware.
 *
 * Threading:
 *   Runs before materialize plan commit starts.  It may acquire vmm->tok via
 *   prepared-writer and split preflight helpers, but performs no PTE/PDE writes
 *   and no software mapping splice.
 */
static int
nvgpu_vm_materialize_entry_preflight(struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_entry *entry)
{
	struct nvgpu_vm_binding *binding = entry->binding;
	struct nvgpu_bo *bo = binding->bo;
	int err;

	if (!entry->is_2m) {
		return (nvgpu_vm_bind_map_segments_preflight(vm,
		    entry->segments.segments, entry->segments.count, bo));
	}

	err = nvgpu_vm_bind_2m_split_plan_preflight(vm->backend,
	    &entry->split_plan);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < entry->segments.count; i++) {
		struct nvgpu_vm_bind_segment *segment =
		    &entry->segments.segments[i];

		if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
			continue;
		err = nvgpu_vm_bind_segment_check_writer_args(segment, bo);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_materialize_entry_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_entry *entry,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_binding *binding = entry->binding;
	struct nvgpu_bo *bo = binding->bo;
	uint64_t page_2m = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	uint64_t materialized = 0;
	int err;

	if (!entry->is_2m) {
		err = nvgpu_vm_bind_map_segments_preflight(vm,
		    entry->segments.segments, entry->segments.count, bo);
		if (err != 0)
			return (err);
		for (uint32_t i = 0; i < entry->segments.count; i++) {
			struct nvgpu_vm_bind_segment *segment =
			    &entry->segments.segments[i];

			if (nvgpu_bo_is_vram(bo)) {
				err =
				    nvgsp_vmm_map_vram_flags_page_prepared_noflush(
				    vm->backend, segment->addr, segment->paddr,
				    segment->size, 0, 0, binding->pte_kind,
				    NVGPU_VM_PAGE_SHIFT_4K);
			} else {
				err =
				    nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush(
				    vm->backend, segment->addr,
				    segment->sysmem_paddrs,
				    segment->sysmem_page_count, binding->pte_kind,
				    segment->page_shift);
			}
			if (err != 0)
				return (err);
		}
		if (entry->segments.count != 0) {
			nvgpu_vm_bind_note_materialize(gpu, dirty_set,
			    binding->addr, binding->size);
			binding->page_shift = NVGPU_VM_PAGE_SHIFT_4K;
		}
		entry->committed = true;
		return (0);
	}

	for (uint32_t i = 0; i < entry->segments.count; i++) {
		struct nvgpu_vm_bind_segment *segment =
		    &entry->segments.segments[i];

		if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
			continue;
		err = nvgpu_vm_bind_segment_check_writer_args(segment, bo);
		if (err != 0)
			return (err);
	}
	for (uint32_t i = 0; i < entry->split_plan.count; i++) {
		uint64_t split = entry->split_plan.splits[i].addr;
		bool was_committed = entry->split_plan.splits[i].committed;

		err = nvgpu_vm_bind_2m_split_plan_commit(vm->backend,
		    &entry->split_plan, split);
		if (err != 0)
			return (err);
		if (!was_committed)
			materialized += page_2m;
	}
	for (uint32_t i = 0; i < entry->segments.count; i++) {
		struct nvgpu_vm_bind_segment *segment =
		    &entry->segments.segments[i];

		if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
			continue;
		err = nvgsp_vmm_map_vram_flags_page_prepared_noflush(
		    vm->backend, segment->addr, segment->paddr,
		    segment->size, 0, 0, binding->pte_kind,
		    segment->page_shift);
		if (err != 0)
			return (err);
	}

	nvgpu_vm_binding_unlink_retire(binding, retired_bindings);
	nvgpu_vm_bindings_insert_prepared(vm, &entry->new_bindings);
	nvgpu_vm_bindings_recalc_max_end(vm);
	if (materialized != 0)
		nvgpu_vm_bind_note_materialize(gpu, dirty_set,
		    binding->addr, materialized);
	entry->committed = true;
	return (0);
}

static void
nvgpu_vm_materialize_plan_init(struct nvgpu_vm_materialize_plan *plan)
{
	plan->entries = NULL;
	plan->count = 0;
}

static void
nvgpu_vm_materialize_plan_fini(struct nvgsp_vmm *vmm,
    struct nvgpu_vm_materialize_plan *plan)
{
	for (uint32_t i = 0; i < plan->count; i++)
		nvgpu_vm_materialize_entry_fini(vmm, &plan->entries[i]);
	if (plan->entries != NULL)
		_kfree(plan->entries, M_NVGPU_VM);
	plan->entries = NULL;
	plan->count = 0;
}

static int
nvgpu_vm_materialize_plan_alloc(
    struct nvgpu_vm_materialize_plan *plan, uint32_t count)
{
	if (count == 0)
		return (0);
	plan->entries = kmalloc((size_t)count * sizeof(*plan->entries),
	    M_NVGPU_VM, M_WAITOK);
	if (plan->entries == NULL)
		return (ENOMEM);
	plan->count = count;
	for (uint32_t i = 0; i < count; i++)
		nvgpu_vm_materialize_entry_init(&plan->entries[i]);
	return (0);
}

static bool
nvgpu_vm_binding_needs_range_materialize(
    const struct nvgpu_vm_binding *binding, uint64_t addr, uint64_t end,
    bool materialize_full_cover)
{
	uint64_t old_start, old_end, cut_start, cut_end;

	if (binding->page_shift <= NVGPU_VM_PAGE_SHIFT_4K)
		return (false);
	old_start = binding->addr;
	old_end = binding->addr + binding->size;
	cut_start = old_start > addr ? old_start : addr;
	cut_end = old_end < end ? old_end : end;
	if (cut_start >= cut_end)
		return (false);
	if (!materialize_full_cover && cut_start == old_start &&
	    cut_end == old_end)
		return (false);
	return (true);
}

static int
nvgpu_vm_materialize_plan_prepare_range(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_materialize_plan *plan,
    uint64_t addr, uint64_t size, bool materialize_full_cover)
{
	struct nvgpu_vm_binding *binding;
	uint64_t end = addr + size;
	uint32_t count = 0;
	int err;

	(void)gpu;
	nvgpu_vm_materialize_plan_init(plan);
	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = nvgpu_vm_binding_next_overlap(vm,
	    binding, addr, size)) {
		if (nvgpu_vm_binding_needs_range_materialize(binding, addr,
		    end, materialize_full_cover))
			count++;
	}
	err = nvgpu_vm_materialize_plan_alloc(plan, count);
	if (err != 0)
		return (err);
	if (count == 0)
		return (0);

	count = 0;
	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = nvgpu_vm_binding_next_overlap(vm,
	    binding, addr, size)) {
		if (!nvgpu_vm_binding_needs_range_materialize(binding, addr,
		    end, materialize_full_cover))
			continue;
		err = nvgpu_vm_materialize_entry_prepare(vm,
		    &plan->entries[count], binding, addr, size,
		    materialize_full_cover);
		if (err != 0) {
			nvgpu_vm_materialize_plan_fini(vm->backend, plan);
			return (err);
		}
		count++;
	}
	return (0);
}

static int
nvgpu_vm_materialize_plan_preflight(struct nvgpu_vm *vm,
    struct nvgpu_vm_materialize_plan *plan)
{
	int err;

	for (uint32_t i = 0; i < plan->count; i++) {
		err = nvgpu_vm_materialize_entry_preflight(vm,
		    &plan->entries[i]);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_materialize_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_materialize_plan *plan,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	int err;

	err = nvgpu_vm_materialize_plan_preflight(vm, plan);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < plan->count; i++) {
		err = nvgpu_vm_materialize_entry_commit(gpu, vm,
		    &plan->entries[i], dirty_set, retired_bindings);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgpu_vm_bindings_materialize_large_partials(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    bool materialize_full_cover, struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_materialize_plan plan;
	int err;

	if (addr >= vm->vm_bindings_max_end)
		return (0);
	err = nvgpu_vm_materialize_plan_prepare_range(gpu, vm, &plan,
	    addr, size, materialize_full_cover);
	if (err != 0)
		return (err);
	err = nvgpu_vm_materialize_plan_commit(gpu, vm, &plan,
	    dirty_set, retired_bindings);
	nvgpu_vm_materialize_plan_fini(vm->backend, &plan);
	return (err);
}

static bool
nvgpu_vm_bind_segments_overlap_lower(
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint64_t addr, uint64_t size, uint8_t old_page_shift)
{
	uint64_t end = addr + size;

	for (uint32_t i = 0; i < segment_count; i++) {
		uint64_t seg_start = segments[i].addr;
		uint64_t seg_end = segments[i].addr + segments[i].size;

		if (seg_start < end && addr < seg_end &&
		    segments[i].page_shift < old_page_shift)
			return (true);
	}
	return (false);
}

static bool
nvgpu_vm_binding_needs_map_materialize(
    struct nvgpu_vm *vm, const struct nvgpu_vm_bind_segment *segments,
    uint32_t segment_count, const struct nvgpu_vm_binding *binding,
    uint64_t addr, uint64_t end, bool *pmaterialize_full_cover)
{
	uint64_t old_start, old_end, cut_start, cut_end;
	bool full_cover, lower_target;

	(void)vm;
	if (binding->page_shift <= NVGPU_VM_PAGE_SHIFT_4K)
		return (false);
	old_start = binding->addr;
	old_end = binding->addr + binding->size;
	cut_start = old_start > addr ? old_start : addr;
	cut_end = old_end < end ? old_end : end;
	if (cut_start >= cut_end)
		return (false);
	full_cover = (cut_start == old_start && cut_end == old_end);
	lower_target = nvgpu_vm_bind_segments_overlap_lower(segments,
	    segment_count, cut_start, cut_end, binding->page_shift);
	if (pmaterialize_full_cover != NULL)
		*pmaterialize_full_cover = lower_target;
	if (full_cover && !lower_target)
		return (false);
	return (true);
}

static int
nvgpu_vm_materialize_plan_prepare_map_conflicts(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_materialize_plan *plan,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint64_t addr, uint64_t size)
{
	struct nvgpu_vm_binding *binding;
	uint64_t end = addr + size;
	uint32_t count = 0;
	int err;

	(void)gpu;
	nvgpu_vm_materialize_plan_init(plan);
	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = nvgpu_vm_binding_next_overlap(vm,
	    binding, addr, size)) {
		if (nvgpu_vm_binding_needs_map_materialize(vm, segments,
		    segment_count, binding, addr, end, NULL))
			count++;
	}
	err = nvgpu_vm_materialize_plan_alloc(plan, count);
	if (err != 0)
		return (err);
	if (count == 0)
		return (0);

	count = 0;
	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = nvgpu_vm_binding_next_overlap(vm,
	    binding, addr, size)) {
		bool materialize_full_cover = false;

		if (!nvgpu_vm_binding_needs_map_materialize(vm, segments,
		    segment_count, binding, addr, end,
		    &materialize_full_cover))
			continue;
		err = nvgpu_vm_materialize_entry_prepare(vm,
		    &plan->entries[count], binding, addr, size,
		    materialize_full_cover);
		if (err != 0) {
			nvgpu_vm_materialize_plan_fini(vm->backend, plan);
			return (err);
		}
		count++;
	}
	return (0);
}

/*
 * nvgpu_vm_bindings_ensure_target_pts_except_2m()
 *
 * Ownership:
 *   Borrows vm and the live binding tree.  It prepares lower LPT/SPT
 *   storage for a non-2M MAP segment; it does not acquire GEM ownership, write
 *   PTEs, or mutate mappings.
 *
 * Lifetime:
 *   Allocated PT pages become owned by the VMM.  Windows currently covered by
 *   a live 2 MiB leaf are skipped because the later materialize split plan
 *   supplies their child PTs before lower-page writers run.  Windows covered
 *   by a caller-owned sparse-clear plan are also skipped when that plan proves
 *   it will split the old sparse parent and provide child PT storage before
 *   the prepared writer runs.
 *
 * Threading:
 *   Requires vm->vm_token.  May allocate page-table pages and therefore
 *   belongs to VM_BIND prepare, before any large-leaf materialize commit.
 */
static int
nvgpu_vm_bindings_ensure_target_pts_except_2m(
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    uint8_t page_shift,
    const struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan)
{
	uint64_t page_2m = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	uint64_t end;
	uint64_t cur;

	if (size == 0)
		return (0);
	if (addr > UINT64_MAX - size)
		return (EINVAL);
	end = addr + size;

	for (cur = addr; cur < end;) {
		struct nvgpu_vm_binding *binding;
		uint64_t chunk_end = (cur & ~(page_2m - 1)) + page_2m;
		uint64_t chunk_size;
		bool has_2m = false;

		if (chunk_end < cur || chunk_end > end)
			chunk_end = end;
		chunk_size = chunk_end - cur;
		for (binding = nvgpu_vm_binding_first_overlap(vm, cur,
		    chunk_size); binding != NULL; binding =
		    nvgpu_vm_binding_next_overlap(vm, binding, cur,
		    chunk_size)) {
			if (binding->page_shift == NVGPU_VM_PAGE_SHIFT_2M) {
				has_2m = true;
				break;
			}
		}
		if (!has_2m) {
			int err;

			if (sparse_clear_plan != NULL) {
				err =
				    nvgsp_vmm_check_unmap_sparse_range_prepared(
				    vm->backend, sparse_clear_plan, cur,
				    chunk_size, page_shift);
				if (err == 0) {
					cur = chunk_end;
					continue;
				}
				if (err != ENOENT)
					return (err);
			}
			err = nvgsp_vmm_ensure_pt_range(vm->backend, cur,
			    chunk_size);

			if (err != 0)
				return (err);
		}
		cur = chunk_end;
	}
	return (0);
}

/*
 * nvgpu_vm_bindings_ensure_target_pd0_segments()
 *
 * Ownership:
 *   Borrows the MAP segment plan and prepares PD0 parent storage for direct
 *   2 MiB target segments.  It does not acquire GEM ownership, create child
 *   LPT/SPT tables, write PTEs, or mutate live bindings.
 *
 * Lifetime:
 *   Allocated PD0 pages become owned by the VMM.  The caller keeps VM_BIND
 *   serialization until the matching prepared writer consumes those slots.
 *
 * Threading:
 *   Requires vm->vm_token.  This is prepare-stage work and may allocate
 *   page-table pages; it must run before the no-fail MAP commit section.
 */
static int
nvgpu_vm_bindings_ensure_target_pd0_segments(
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count)
{
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		if (segments[i].page_shift != NVGPU_VM_PAGE_SHIFT_2M)
			continue;
		err = nvgsp_vmm_ensure_pd0_range(vm->backend,
		    segments[i].addr, segments[i].size);
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bindings_ensure_target_storage()
 *
 * Ownership:
 *   Borrows the MAP segment plan, vm, and the live binding tree.  It only
 *   prepares page-table storage selected by each segment's target page class;
 *   it does not acquire GEM ownership, pin BOs, write PTEs, or mutate mappings.
 *
 * Lifetime:
 *   PT pages allocated for 4 KiB/64 KiB target segments and PD0 pages allocated
 *   for direct 2 MiB target segments become owned by the VMM.  Existing live
 *   2 MiB leaves are skipped for lower-page target segments because the later
 *   materialize split plan supplies their child PTs.  A non-NULL
 *   sparse_clear_plan is borrowed only for this prepare call; if it proves the
 *   sparse clear will materialize the child PTs, this helper does not allocate
 *   duplicate live storage.  Direct 2 MiB target segments deliberately do not
 *   allocate lower LPT/SPT storage.
 *
 * Threading:
 *   Requires vm->vm_token.  This is prepare-stage work and may allocate
 *   page-table pages or take vmm->tok for read-only sparse-clear validation,
 *   so it must finish before the no-fail MAP commit section.
 */
static int
nvgpu_vm_bindings_ensure_target_storage(struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    const struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan)
{
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		if (segments[i].page_shift == NVGPU_VM_PAGE_SHIFT_2M) {
			err = nvgsp_vmm_ensure_pd0_range(vm->backend,
			    segments[i].addr, segments[i].size);
		} else {
			err = nvgpu_vm_bindings_ensure_target_pts_except_2m(
			    vm, segments[i].addr, segments[i].size,
			    segments[i].page_shift, sparse_clear_plan);
		}
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_preflight_clear_pd0_target_segments()
 *
 * Ownership:
 *   Borrows the direct-MAP segment plan and VMM.  It does not allocate, write
 *   PTEs/PDEs, pin BOs, or mutate the software mapping tree.
 *
 * Lifetime:
 *   The result is valid while VM_BIND serialization keeps the segment plan,
 *   live binding tree, sparse-region tree, and VMM page-table state stable.
 *   A successful result proves every direct 2 MiB target segment is either an
 *   already prepared empty PD0 slot or can be cleared by the following commit
 *   pass without discovering a later segment conflict.  If
 *   allow_prepared_sparse_clear is true, the caller already owns a sparse-clear
 *   plan covering this MAP op and will commit it before the PD0 clear writer.
 *
 * Threading:
 *   Called before the no-fail direct-2M MAP clear commit.  It may acquire the
 *   VMM token through the backend check helpers but performs only read-only
 *   validation.
 */
static int
nvgpu_vm_bind_preflight_clear_pd0_target_segments(
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    bool allow_prepared_sparse_clear)
{
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		if (segments[i].page_shift != NVGPU_VM_PAGE_SHIFT_2M)
			continue;

		err = nvgsp_vmm_check_prepared_pt_range(vm->backend,
		    segments[i].addr, segments[i].size,
		    NVGPU_VM_PAGE_SHIFT_2M);
		if (err == 0)
			continue;
		if (err != EBUSY)
			return (err);

		if (allow_prepared_sparse_clear) {
			err =
			    nvgsp_vmm_check_clear_pd0_target_range_allow_sparse(
			    vm->backend, segments[i].addr, segments[i].size);
		} else {
			err = nvgsp_vmm_check_clear_pd0_target_range(
			    vm->backend, segments[i].addr, segments[i].size);
		}
		if (err != 0)
			return (err);
	}
	return (0);
}

/*
 * nvgpu_vm_bind_clear_pd0_target_segments_noflush()
 *
 * Ownership:
 *   Borrows the MAP segment plan and clears only direct 2 MiB target windows
 *   that still have old PD0 or lower-table ownership.  It does not mutate the
 *   software mapping tree; commit_replace_range() retires old mappings after
 *   all new target PTEs are installed.
 *
 * Lifetime:
 *   This helper runs inside the no-fail MAP commit section after
 *   nvgpu_vm_bind_preflight_clear_pd0_target_segments() has validated the
 *   whole segment set.  Cleared hardware ownership is published by the
 *   caller's final VMM flush/TLB invalidate.
 *
 * Threading:
 *   Requires VM_BIND serialization and the surrounding GSP/VMM mutation
 *   boundary.  It performs no allocation or BO lookup; any sparse-region or
 *   partial-window conflict must have been rejected by the earlier preflight.
 */
static int
nvgpu_vm_bind_clear_pd0_target_segments_noflush(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    struct nvgpu_vm_dirty_set *dirty_set)
{
	int err;

	err = nvgpu_vm_bind_preflight_clear_pd0_target_segments(vm,
	    segments, segment_count, false);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < segment_count; i++) {
		if (segments[i].page_shift != NVGPU_VM_PAGE_SHIFT_2M)
			continue;

		err = nvgsp_vmm_check_prepared_pt_range(vm->backend,
		    segments[i].addr, segments[i].size,
		    NVGPU_VM_PAGE_SHIFT_2M);
		if (err == 0)
			continue;
		if (err != EBUSY)
			return (err);

		err = nvgsp_vmm_clear_pd0_target_noflush(vm->backend,
		    segments[i].addr, segments[i].size);
		if (err != 0)
			return (err);
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
		    segments[i].addr, segments[i].size);
	}
	return (0);
}

static bool
nvgpu_vm_binding_range_has_2m_overlap(struct nvgpu_vm *vm,
    uint64_t addr, uint64_t size)
{
	struct nvgpu_vm_binding *binding;

	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = nvgpu_vm_binding_next_overlap(vm,
	    binding, addr, size)) {
		if (binding->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
			return (true);
	}
	return (false);
}

static int
nvgpu_vm_bind_segment_check_writer_args(
    const struct nvgpu_vm_bind_segment *segment, const struct nvgpu_bo *bo)
{
	uint64_t page_size;

	if (segment->size == 0)
		return (EINVAL);
	if (!nvgpu_bo_is_vram(bo)) {
		if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_4K)
			page_size = NVGPU_VM_PAGE_SIZE_4K;
		else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_64K)
			page_size = NVGPU_VM_PAGE_SIZE_64K;
		else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
			page_size = NVGPU_VM_PAGE_SIZE_2M;
		else
			return (EINVAL);
		if (segment->sysmem_paddrs == NULL ||
		    segment->sysmem_page_count !=
		    segment->size / NVGPU_VM_PAGE_SIZE_4K ||
		    ((segment->addr | segment->size | segment->bo_offset) &
		    (page_size - 1)) != 0)
			return (EINVAL);
		if (segment->page_shift != NVGPU_VM_PAGE_SHIFT_4K) {
			uint32_t pages_per_leaf =
			    (uint32_t)(page_size / NVGPU_VM_PAGE_SIZE_4K);

			if ((segment->paddr & (page_size - 1)) != 0 ||
			    (segment->sysmem_page_count % pages_per_leaf) != 0)
				return (EINVAL);
			for (uint32_t i = 1;
			    i < segment->sysmem_page_count; i++) {
				if ((uint64_t)segment->sysmem_paddrs[i] !=
				    (uint64_t)segment->sysmem_paddrs[0] +
				    (uint64_t)i * NVGPU_VM_PAGE_SIZE_4K)
					return (EINVAL);
			}
		}
		return (0);
	}

	if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_4K)
		page_size = NVGPU_VM_PAGE_SIZE_4K;
	else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_64K)
		page_size = NVGPU_VM_PAGE_SIZE_64K;
	else if (segment->page_shift == NVGPU_VM_PAGE_SHIFT_2M)
		page_size = NVGPU_VM_PAGE_SIZE_2M;
	else
		return (EINVAL);
	if ((segment->addr | segment->paddr | segment->size) &
	    (page_size - 1))
		return (EINVAL);
	return (0);
}

/*
 * nvgpu_vm_bindings_check_prepared_target_pts()
 *
 * Ownership:
 *   Borrows the target MAP plan and the VM binding tree.  It only validates
 *   that later prepared writers have page-table storage to consume; it does
 *   not allocate, write PTEs, pin BOs, or mutate mappings.
 *
 * Lifetime:
 *   The result is valid while VM_BIND serialization is held.  Chunks currently
 *   covered by a live 2 MiB leaf are intentionally skipped because the
 *   subsequent MAP-specific materialize step prepares and commits their child
 *   PTs before the writer runs.  Chunks covered by sparse_clear_plan may still
 *   look missing or busy in the live VMM lookup; the plan is the ownership
 *   proof that commit will split/clear the sparse parent before the MAP writer.
 *
 * Threading:
 *   Requires vm->vm_token and may take vmm->tok for direct lookup checks.
 *   This is the prepare gate for prepared-only MAP writers.
 */
static int
nvgpu_vm_bindings_check_prepared_target_pts(struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    const struct nvgpu_bo *bo,
    const struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan)
{
	uint64_t page_2m = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	int err;

	for (uint32_t i = 0; i < segment_count; i++) {
		uint64_t end;
		uint64_t cur;

		err = nvgpu_vm_bind_segment_check_writer_args(&segments[i],
		    bo);
		if (err != 0)
			return (err);
		if (segments[i].addr > UINT64_MAX - segments[i].size)
			return (EINVAL);
		end = segments[i].addr + segments[i].size;
		for (cur = segments[i].addr; cur < end;) {
			uint64_t chunk_end = (cur & ~(page_2m - 1)) +
			    page_2m;
			uint64_t chunk_size;

			if (chunk_end < cur || chunk_end > end)
				chunk_end = end;
			chunk_size = chunk_end - cur;
			if (segments[i].page_shift == NVGPU_VM_PAGE_SHIFT_2M ||
			    !nvgpu_vm_binding_range_has_2m_overlap(vm,
			    cur, chunk_size)) {
				err = nvgsp_vmm_check_prepared_pt_range(
				    vm->backend, cur, chunk_size,
				    segments[i].page_shift);
				if ((err == EBUSY || err == ENOENT) &&
				    sparse_clear_plan != NULL) {
					err =
					    nvgsp_vmm_check_unmap_sparse_range_prepared(
					    vm->backend, sparse_clear_plan, cur,
					    chunk_size, segments[i].page_shift);
					if (err == 0) {
						cur = chunk_end;
						continue;
					}
				}
				if (err == EBUSY &&
				    segments[i].page_shift ==
				    NVGPU_VM_PAGE_SHIFT_2M) {
					err =
					    nvgsp_vmm_check_clear_pd0_target_range(
					    vm->backend, cur, chunk_size);
				}
				if (err != 0)
					return (err);
			}
			cur = chunk_end;
		}
	}
	return (0);
}

/*
 * nvgpu_vm_bindings_materialize_map_conflicts()
 *
 * Ownership:
 *   Borrows the target segment plan and mutates only conflicting live
 *   mappings.  Replacement old mappings are prepared before any hardware
 *   ownership split and retired through retired_bindings after success.
 *
 * Lifetime:
 *   This is the MAP-specific materialize path.  Unlike unmap/null/sparse,
 *   a full old 2 MiB leaf still must be split when the new MAP will install
 *   64 KiB or 4 KiB target leaves over it.
 *
 * Threading:
 *   Requires vm->vm_token and the surrounding VM_BIND/GSP serialization.
 *   All allocations are performed by the materialize helpers before their
 *   no-fail split commit starts.
 */
static int
nvgpu_vm_bindings_materialize_map_conflicts(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint64_t addr, uint64_t size, struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_materialize_plan plan;
	int err;

	if (addr >= vm->vm_bindings_max_end)
		return (0);
	err = nvgpu_vm_materialize_plan_prepare_map_conflicts(gpu, vm,
	    &plan, segments, segment_count, addr, size);
	if (err != 0)
		return (err);
	err = nvgpu_vm_materialize_plan_commit(gpu, vm, &plan,
	    dirty_set, retired_bindings);
	nvgpu_vm_materialize_plan_fini(vm->backend, &plan);
	return (err);
}

static void nvgpu_vm_bindings_free_prepared(
    struct nvgpu_vm_binding_list *bindings);

/*
 * nvgpu_vm_binding_prepare_remove_tail_for_segment()
 *
 * Ownership:
 *   Borrows source and appends at most one cloned tail binding to tail_bindings.
 *   The cloned tail owns its GEM reference and VM_BIND pin on success.
 *
 * Lifetime:
 *   The source binding is not mutated.  The prepared tail is detached until the
 *   caller's remove_range commit inserts it or the abort path frees it.
 *
 * Threading:
 *   Runs under vm->vm_token before hardware PTEs are changed.  It may sleep
 *   through allocation or BO pinning and therefore must not run in the no-fail
 *   commit section.
 */
static int
nvgpu_vm_binding_prepare_remove_tail_for_segment(
    struct nvgpu_vm *vm, const struct nvgpu_vm_binding *source,
    uint64_t segment_addr, uint64_t segment_size, uint64_t segment_bo_offset,
    uint8_t page_shift, uint64_t target_addr, uint64_t target_end,
    struct nvgpu_vm_binding_list *tail_bindings)
{
	struct nvgpu_vm_binding *tail;
	uint64_t segment_end, cut_start, cut_end;
	uint64_t head_size, tail_size, tail_bo_offset;
	int err;

	if (segment_size == 0 || segment_addr > UINT64_MAX - segment_size)
		return (EINVAL);
	segment_end = segment_addr + segment_size;
	cut_start = segment_addr > target_addr ? segment_addr : target_addr;
	cut_end = segment_end < target_end ? segment_end : target_end;
	if (cut_start >= cut_end)
		return (0);

	head_size = cut_start - segment_addr;
	tail_size = segment_end - cut_end;
	if (head_size == 0 || tail_size == 0)
		return (0);

	tail_bo_offset = segment_bo_offset + (cut_end - segment_addr);
	err = nvgpu_vm_binding_prepare_clone(vm, source, cut_end,
	    tail_size, tail_bo_offset, page_shift, &tail);
	if (err != 0)
		return (err);
	LIST_INSERT_HEAD(tail_bindings, tail, link);
	return (0);
}

/*
 * nvgpu_vm_binding_prepare_remove_tails_2m()
 *
 * Ownership:
 *   Borrows one live 2 MiB binding and prepares cloned tail bindings for the
 *   lower-level segments that materialize_large_partials() will expose.
 *
 * Lifetime:
 *   The source binding and hardware PD0 leaf remain unchanged here.  Prepared
 *   tails become valid only after the later materialize/remove commit consumes
 *   them.
 *
 * Threading:
 *   Runs in the prepare phase under vm->vm_token.  It mirrors the 2 MiB
 *   materialization geometry but performs no BAR1 writes and no tracker splice.
 */
static int
nvgpu_vm_binding_prepare_remove_tails_2m(struct nvgpu_vm *vm,
    const struct nvgpu_vm_binding *binding, uint64_t target_addr,
    uint64_t target_end, struct nvgpu_vm_binding_list *tail_bindings)
{
	uint64_t page_2m = 1ULL << NVGPU_VM_PAGE_SHIFT_2M;
	uint64_t page_64k = NVGPU_VM_PAGE_SIZE_64K;
	uint64_t old_start = binding->addr;
	uint64_t old_end;
	uint64_t window;
	int err;

	if (binding->size == 0 || old_start > UINT64_MAX - binding->size)
		return (EINVAL);
	if ((binding->addr | binding->size | binding->bo_offset) &
	    (page_2m - 1))
		return (EINVAL);

	old_end = binding->addr + binding->size;
	for (window = old_start; window < old_end; window += page_2m) {
		uint64_t window_end = window + page_2m;
		uint64_t cut_start = window > target_addr ? window :
		    target_addr;
		uint64_t cut_end = window_end < target_end ? window_end :
		    target_end;

		if (cut_start >= cut_end || (cut_start == window &&
		    cut_end == window_end))
			continue;

		for (uint64_t sub = window; sub < window_end;
		    sub += page_64k) {
			uint64_t sub_end = sub + page_64k;
			uint64_t sub_cut_start = sub > target_addr ? sub :
			    target_addr;
			uint64_t sub_cut_end = sub_end < target_end ? sub_end :
			    target_end;
			uint64_t sub_bo_offset = binding->bo_offset +
			    (sub - old_start);

			if (sub_cut_start >= sub_cut_end ||
			    (sub_cut_start == sub && sub_cut_end == sub_end))
				continue;

			err = nvgpu_vm_binding_prepare_remove_tail_for_segment(
			    vm, binding, sub, page_64k, sub_bo_offset,
			    NVGPU_VM_PAGE_SHIFT_4K, target_addr, target_end,
			    tail_bindings);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

/*
 * nvgpu_vm_bindings_prepare_remove_range()
 *
 * Ownership:
 *   Preallocates every tail binding that remove_range() may need after large
 *   partial mappings are materialized.  The returned tail_bindings list owns
 *   GEM references and VM_BIND pins until commit consumes it or abort frees it.
 *
 * Lifetime:
 *   Existing live bindings and hardware PTEs are not changed here.  The prepared
 *   tails are valid only while the caller keeps vm->vm_token and then either
 *   calls remove_range() commit logic or aborts the list.
 *
 * Threading:
 *   Runs before the VM_BIND no-fail commit section mutates page tables.  It may
 *   sleep while allocating binding nodes and pinning BOs.
 */
static int
nvgpu_vm_bindings_prepare_remove_range(struct nvgpu_vm *vm,
    uint64_t addr, uint64_t size,
    struct nvgpu_vm_binding_list *tail_bindings)
{
	struct nvgpu_vm_binding *binding;
	uint64_t end;
	int err;

	LIST_INIT(tail_bindings);
	if (size == 0 || addr > UINT64_MAX - size)
		return (EINVAL);
	end = addr + size;
	if (addr >= vm->vm_bindings_max_end)
		return (0);

	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL;
	    binding = nvgpu_vm_binding_next_overlap(vm, binding,
	    addr, size)) {
		uint64_t old_start, old_end, cut_start, cut_end;
		uint8_t tail_page_shift;

		old_start = binding->addr;
		if (binding->size == 0 ||
		    old_start > UINT64_MAX - binding->size) {
			err = EINVAL;
			goto fail;
		}
		old_end = binding->addr + binding->size;
		cut_start = old_start > addr ? old_start : addr;
		cut_end = old_end < end ? old_end : end;
		if (cut_start >= cut_end ||
		    (cut_start == old_start && cut_end == old_end))
			continue;

		if (binding->page_shift == NVGPU_VM_PAGE_SHIFT_2M) {
			err = nvgpu_vm_binding_prepare_remove_tails_2m(vm,
			    binding, addr, end, tail_bindings);
			if (err != 0)
				goto fail;
			continue;
		}

		tail_page_shift = binding->page_shift;
		if (binding->page_shift > NVGPU_VM_PAGE_SHIFT_4K)
			tail_page_shift = NVGPU_VM_PAGE_SHIFT_4K;
		err = nvgpu_vm_binding_prepare_remove_tail_for_segment(vm,
		    binding, old_start, binding->size, binding->bo_offset,
		    tail_page_shift, addr, end, tail_bindings);
		if (err != 0)
			goto fail;
	}
	return (0);

fail:
	nvgpu_vm_bindings_free_prepared(tail_bindings);
	return (err);
}

/*
 * nvgpu_vm_bindings_free_prepared()
 *
 * Ownership:
 *   Consumes every binding currently linked on bindings.  Each binding owns a
 *   GEM reference and may own a VM_BIND BO pin; both are released here.
 *
 * Lifetime:
 *   The list head remains owned by the caller and is empty on return.
 *
 * Threading:
 *   Called from VM_BIND prepare/abort paths while the caller owns the per-file
 *   VM token.  It does not touch hardware page tables.
 */
static void
nvgpu_vm_bindings_free_prepared(
    struct nvgpu_vm_binding_list *bindings)
{
	nvgpu_vm_bindings_release(bindings);
}

/*
 * nvgpu_vm_bindings_abort_replace_range()
 *
 * Ownership:
 *   Consumes prepared tail bindings that were not committed into the live
 *   tracker.  Each node owns its GEM reference and VM_BIND pin.
 *   Existing live bindings and hardware PTEs are untouched.
 *
 * Lifetime:
 *   The prepared list must not be used after this call except as an empty list.
 *
 * Threading:
 *   Requires the same VM_BIND serialization as prepare/commit.
 */
static void
nvgpu_vm_bindings_abort_replace_range(
    struct nvgpu_vm_binding_list *tail_bindings)
{
	nvgpu_vm_bindings_free_prepared(tail_bindings);
}

/*
 * nvgpu_vm_bindings_commit_replace_range()
 *
 * Ownership:
 *   Consumes prepared tail bindings and mutates vm's binding tracker to
 *   describe the final VA state after a successful MAP replacement.
 *
 * Lifetime:
 *   The caller must have already installed the new PTEs for [addr, addr+size).
 *   Old bindings covering that exact range are removed from the software
 *   tracker without first writing invalid PTEs, because the new valid PTEs have
 *   already overwritten them and the caller will flush once before ioctl
 *   return.  Fully covered old bindings move to retired_bindings; the caller
 *   must release them only after the VM_BIND done fence is signaled.
 *
 * Threading:
 *   Requires vm->vm_token.  This function does not acquire the VMM token and
 *   does not touch BAR1; it is a no-fail software state transition.
 */
static void
nvgpu_vm_bindings_commit_replace_range(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    struct nvgpu_vm_binding_list *tail_bindings,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_binding *binding, *next;
	uint64_t end = addr + size;
	bool changed = false;

	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = next) {
		uint64_t old_start, old_end, cut_start, cut_end;
		uint64_t head_size, tail_size;

		next = nvgpu_vm_binding_next_overlap(vm, binding,
		    addr, size);
		changed = true;
		old_start = binding->addr;
		old_end = binding->addr + binding->size;
		cut_start = old_start > addr ? old_start : addr;
		cut_end = old_end < end ? old_end : end;
		head_size = cut_start - old_start;
		tail_size = old_end - cut_end;
		nvgpu_vm_bind_note_replace_clear_skip(gpu, cut_end - cut_start);

		if (head_size != 0 && tail_size != 0) {
			binding->size = head_size;
		} else if (head_size != 0) {
			binding->size = head_size;
		} else if (tail_size != 0) {
			nvgpu_vm_binding_rekey_tail(vm, binding,
			    cut_end, tail_size,
			    binding->bo_offset + cut_end - old_start);
		} else {
			binding->pte_installed = false;
			nvgpu_vm_binding_unlink_retire(binding,
			    retired_bindings);
		}
	}

	while ((binding = LIST_FIRST(tail_bindings)) != NULL) {
		LIST_REMOVE(binding, link);
		nvgpu_vm_binding_insert_sorted(vm, binding);
	}
	if (changed)
		nvgpu_vm_bindings_recalc_max_end(vm);
}

/*
 * nvgpu_vm_valid_map_plan_*()
 *
 * Ownership:
 *   The plan owns every fallible object prepared for one non-noop valid MAP:
 *   sparse-clear ownership transferred from the caller, replacement tails, new
 *   target bindings, and the large-leaf materialize plan.  Commit consumes those
 *   bos into the VMM page tables and live mapping tree.
 *
 * Lifetime:
 *   The caller must keep the segment plan, BO, and VM remap serialization alive
 *   from prepare through commit/fini.  On abort, fini releases all unpublished
 *   bos.  On successful commit, old live mappings move to retired_bindings
 *   and remain caller-owned until the VM_BIND done fence is visible.
 *
 * Threading:
 *   Prepare may allocate, pin, populate TT pages, and inspect VMM state.  Commit
 *   runs under the VM_BIND/GSP mutation boundary and should only consume prepared
 *   state, write PTE/PDEs, and splice the software mapping tree.
 */
static void
nvgpu_vm_valid_map_plan_init(struct nvgpu_vm_valid_map_plan *plan)
{
	LIST_INIT(&plan->new_bindings);
	LIST_INIT(&plan->replace_tails);
	nvgpu_vm_materialize_plan_init(&plan->materialize_plan);
	plan->sparse_clear_plan = NULL;
	plan->committed = false;
}

static void
nvgpu_vm_valid_map_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_valid_map_plan *plan)
{
	if (!plan->committed) {
		nvgpu_vm_bindings_free_prepared(&plan->new_bindings);
		nvgpu_vm_bindings_abort_replace_range(&plan->replace_tails);
	}
	nvgpu_vm_bind_abort_sparse_clear(vm, &plan->sparse_clear_plan);
	nvgpu_vm_materialize_plan_fini(vm->backend,
	    &plan->materialize_plan);
	nvgpu_vm_valid_map_plan_init(plan);
}

static int
nvgpu_vm_valid_map_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_valid_map_plan *plan,
    struct nvgpu_bo *bo, const struct nvgpu_vm_bind_segment *segments,
    uint32_t segment_count, uint64_t addr, uint64_t size, uint8_t pte_kind,
    struct nvgsp_vmm_sparse_unmap_plan **psparse_clear_plan)
{
	int err;

	nvgpu_vm_valid_map_plan_init(plan);
	plan->sparse_clear_plan = *psparse_clear_plan;
	*psparse_clear_plan = NULL;

	err = nvgpu_vm_bindings_prepare_remove_range(vm, addr, size,
	    &plan->replace_tails);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_bind_prepare_segment_bindings(vm, bo, segments,
	    segment_count, pte_kind, &plan->new_bindings);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_bindings_ensure_target_storage(vm, segments,
	    segment_count, plan->sparse_clear_plan);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_bindings_check_prepared_target_pts(vm, segments,
	    segment_count, bo, plan->sparse_clear_plan);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_materialize_plan_prepare_map_conflicts(gpu, vm,
	    &plan->materialize_plan, segments, segment_count, addr, size);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_materialize_plan_preflight(vm,
	    &plan->materialize_plan);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_bind_preflight_clear_pd0_target_segments(vm,
	    segments, segment_count, plan->sparse_clear_plan != NULL);
	if (err != 0)
		goto fail;

	return (0);

fail:
	nvgpu_vm_valid_map_plan_fini(vm, plan);
	return (err);
}

static int
nvgpu_vm_valid_map_plan_commit_preflight(struct nvgpu_vm *vm,
    struct nvgpu_vm_valid_map_plan *plan,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    const struct nvgpu_bo *bo)
{
	int err;

	err = nvgpu_vm_materialize_plan_preflight(vm,
	    &plan->materialize_plan);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bindings_check_prepared_target_pts(vm, segments,
	    segment_count, bo, plan->sparse_clear_plan);
	if (err != 0)
		return (err);

	return (nvgpu_vm_bind_preflight_clear_pd0_target_segments(vm,
	    segments, segment_count, plan->sparse_clear_plan != NULL));
}

static int
nvgpu_vm_valid_map_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_valid_map_plan *plan,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    struct nvgpu_bo *bo, uint32_t op_flags, uint64_t addr, uint64_t size,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	int err;

	err = nvgpu_vm_valid_map_plan_commit_preflight(vm, plan,
	    segments, segment_count, bo);
	if (err != 0)
		return (err);

	err = nvgpu_vm_materialize_plan_commit(gpu, vm,
	    &plan->materialize_plan, dirty_set, retired_bindings);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bind_commit_sparse_clear_noflush(gpu, vm,
	    &plan->sparse_clear_plan, NVGPU_VM_TRACE_MAP, addr, size,
	    dirty_set);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bind_clear_pd0_target_segments_noflush(gpu, vm,
	    segments, segment_count, dirty_set);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bind_map_segments_noflush(gpu, vm, segments,
	    segment_count, bo, op_flags & 0xff, dirty_set);
	if (err != 0)
		return (err);

	nvgpu_vm_bind_note_map_segments(gpu, op_flags, segments,
	    segment_count, bo);
	nvgpu_vm_bindings_commit_replace_range(gpu, vm, addr, size,
	    &plan->replace_tails, retired_bindings);
	nvgpu_vm_bindings_insert_prepared(vm, &plan->new_bindings);
	plan->committed = true;
	return (0);
}

/*
 * nvgpu_vm_remove_plan_*()
 *
 * Ownership:
 *   The plan owns detached tail bindings and the large-leaf materialize plan
 *   prepared for one valid-range removal.  Commit consumes tail bindings into
 *   vm's live tracker and moves fully removed live bindings to the caller's
 *   retired list.
 *
 * Lifetime:
 *   The plan is valid only while the caller keeps VM remap serialization.  Fini
 *   releases any prepared ownership not consumed by a successful commit.  The
 *   caller keeps retired bindings alive until the VM_BIND done fence is visible.
 *
 * Threading:
 *   Prepare may allocate and pin, so it runs before the no-fail commit section.
 *   Commit runs with the same VM_BIND/GSP serialization as the PTE writers and
 *   does not flush or publish fences; the caller consumes dirty_set at the
 *   enclosing invalidate boundary.
 */
static void
nvgpu_vm_remove_plan_init(struct nvgpu_vm_remove_plan *plan)
{
	LIST_INIT(&plan->tail_bindings);
	nvgpu_vm_materialize_plan_init(&plan->materialize_plan);
	plan->addr = 0;
	plan->size = 0;
	plan->clear_empty_range = false;
	plan->preserve_target_pts = false;
	plan->preserve_page_shift = NVGPU_VM_PAGE_SHIFT_4K;
	plan->materialize_full_cover = false;
	plan->clear_action = NVGPU_VM_TRACE_UNMAP;
}

static void
nvgpu_vm_remove_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_remove_plan *plan)
{
	nvgpu_vm_bindings_free_prepared(&plan->tail_bindings);
	nvgpu_vm_materialize_plan_fini(vm->backend,
	    &plan->materialize_plan);
	nvgpu_vm_remove_plan_init(plan);
}

static int
nvgpu_vm_remove_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_remove_plan *plan,
    uint64_t addr, uint64_t size, bool clear_empty_range,
    bool preserve_target_pts, uint8_t preserve_page_shift,
    bool materialize_full_cover, uint32_t clear_action)
{
	int err;

	nvgpu_vm_remove_plan_init(plan);
	if (size == 0 || addr > UINT64_MAX - size)
		return (EINVAL);
	plan->addr = addr;
	plan->size = size;
	plan->clear_empty_range = clear_empty_range;
	plan->preserve_target_pts = preserve_target_pts;
	plan->preserve_page_shift = preserve_page_shift;
	plan->materialize_full_cover = materialize_full_cover;
	plan->clear_action = clear_action;

	if (addr >= vm->vm_bindings_max_end)
		return (0);

	err = nvgpu_vm_bindings_prepare_remove_range(vm, addr, size,
	    &plan->tail_bindings);
	if (err != 0)
		return (err);

	err = nvgpu_vm_materialize_plan_prepare_range(gpu, vm,
	    &plan->materialize_plan, addr, size, materialize_full_cover);
	if (err != 0) {
		nvgpu_vm_remove_plan_fini(vm, plan);
		return (err);
	}
	err = nvgpu_vm_materialize_plan_preflight(vm,
	    &plan->materialize_plan);
	if (err != 0) {
		nvgpu_vm_remove_plan_fini(vm, plan);
		return (err);
	}
	return (0);
}

static int
nvgpu_vm_remove_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_remove_plan *plan,
    uint32_t *punmapped, struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_binding *binding, *next;
	uint64_t addr = plan->addr;
	uint64_t size = plan->size;
	uint64_t end = addr + size;
	int err;

	*punmapped = 0;
	if (addr >= vm->vm_bindings_max_end) {
		if (plan->clear_empty_range)
			nvgpu_vm_bind_note_empty_clear_skip(gpu, size);
		return (0);
	}

	err = nvgpu_vm_materialize_plan_commit(gpu, vm,
	    &plan->materialize_plan, dirty_set, retired_bindings);
	if (err != 0)
		return (err);

	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL;
	    binding = nvgpu_vm_binding_next_overlap(vm, binding,
	    addr, size)) {
		(*punmapped)++;
	}

	if (*punmapped != 0) {
		for (binding = nvgpu_vm_binding_first_overlap(vm,
		    addr, size); binding != NULL;
		    binding = nvgpu_vm_binding_next_overlap(vm,
		    binding, addr, size)) {
			uint64_t old_start, old_end, cut_start, cut_end;
			uint8_t clear_page_shift;

			old_start = binding->addr;
			old_end = binding->addr + binding->size;
			cut_start = old_start > addr ? old_start : addr;
			cut_end = old_end < end ? old_end : end;
			clear_page_shift = plan->preserve_target_pts ?
			    plan->preserve_page_shift : binding->page_shift;
			err = nvgsp_vmm_check_unmap_valid_range_page(
			    vm->backend, cut_start, cut_end - cut_start,
			    plan->preserve_target_pts, clear_page_shift);
			if (err != 0)
				return (err);
		}
		for (binding = nvgpu_vm_binding_first_overlap(vm,
		    addr, size); binding != NULL;
		    binding = nvgpu_vm_binding_next_overlap(vm,
		    binding, addr, size)) {
			uint64_t old_start, old_end, cut_start, cut_end;
			uint8_t clear_page_shift;

			old_start = binding->addr;
			old_end = binding->addr + binding->size;
			cut_start = old_start > addr ? old_start : addr;
			cut_end = old_end < end ? old_end : end;
			clear_page_shift = plan->preserve_target_pts ?
			    plan->preserve_page_shift : binding->page_shift;
			nvgpu_vm_bind_note_clear_shape(gpu,
			    binding->bo, binding->pte_kind,
			    binding->page_shift, cut_end - cut_start);
			nvgpu_vm_bind_note_clear(gpu, plan->clear_action,
			    cut_end - cut_start);
			if (plan->preserve_target_pts) {
				if (nvgpu_vm_bind_clear_is_conflict(
				    plan->clear_action)) {
					err =
					    nvgsp_vmm_clear_conflict_preserve_page_noflush(
					    vm->backend, cut_start,
					    cut_end - cut_start,
					    clear_page_shift);
				} else {
					err =
					    nvgsp_vmm_unmap_valid_preserve_page_noflush(
					    vm->backend, cut_start,
					    cut_end - cut_start,
					    clear_page_shift);
				}
			} else {
				err = nvgsp_vmm_unmap_valid_page_noflush(
				    vm->backend, cut_start, cut_end - cut_start,
				    clear_page_shift);
			}
			if (err != 0)
				return (err);
			nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
			    cut_start, cut_end - cut_start);
		}
	} else if (plan->clear_empty_range) {
		nvgpu_vm_bind_note_empty_clear_skip(gpu, size);
	}

	for (binding = nvgpu_vm_binding_first_overlap(vm, addr, size);
	    binding != NULL; binding = next) {
		uint64_t old_start, old_end, cut_start, cut_end;
		uint64_t head_size, tail_size;

		next = nvgpu_vm_binding_next_overlap(vm, binding,
		    addr, size);
		old_start = binding->addr;
		old_end = binding->addr + binding->size;
		cut_start = old_start > addr ? old_start : addr;
		cut_end = old_end < end ? old_end : end;
		head_size = cut_start - old_start;
		tail_size = old_end - cut_end;

		if (head_size != 0 && tail_size != 0) {
			binding->size = head_size;
		} else if (head_size != 0) {
			binding->size = head_size;
		} else if (tail_size != 0) {
			nvgpu_vm_binding_rekey_tail(vm, binding,
			    cut_end, tail_size,
			    binding->bo_offset + cut_end - old_start);
		} else {
			binding->pte_installed = false;
			nvgpu_vm_binding_unlink_retire(binding,
			    retired_bindings);
		}
	}

	while ((binding = LIST_FIRST(&plan->tail_bindings)) != NULL) {
		LIST_REMOVE(binding, link);
		nvgpu_vm_binding_insert_sorted(vm, binding);
	}
	if (*punmapped != 0)
		nvgpu_vm_bindings_recalc_max_end(vm);
	return (0);
}

/*
 * nvgpu_vm_clear_plan_*()
 *
 * Ownership:
 *   The plan owns every prepared object needed by one clear-style op: the
 *   optional sparse clear plan plus the valid-range remove plan.  Commit moves
 *   fully removed valid bindings to retired_bindings and consumes sparse clear
 *   storage into the VMM sparse tree.
 *
 * Lifetime:
 *   The caller keeps VM_BIND serialization from prepare through commit/fini.
 *   On abort, fini releases unpublished sparse/removal ownership and leaves the
 *   live mapping tree unchanged.  On successful commit, old BO refs/pins remain
 *   owned by retired_bindings until the VM_BIND done fence is visible.
 *
 * Threading:
 *   Prepare may allocate sparse split records, clone tail mappings, and pin BOs.
 *   Commit runs inside the VM_BIND/GSP mutation boundary and does not flush; the
 *   caller publishes dirty PTE/PDE writes at the enclosing invalidate boundary.
 */
static void
nvgpu_vm_clear_plan_init(struct nvgpu_vm_clear_plan *plan)
{
	nvgpu_vm_remove_plan_init(&plan->remove_plan);
	plan->sparse_clear_plan = NULL;
	plan->addr = 0;
	plan->size = 0;
	plan->action = NVGPU_VM_TRACE_UNMAP;
}

static void
nvgpu_vm_clear_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_clear_plan *plan)
{
	nvgpu_vm_bind_abort_sparse_clear(vm, &plan->sparse_clear_plan);
	nvgpu_vm_remove_plan_fini(vm, &plan->remove_plan);
	nvgpu_vm_clear_plan_init(plan);
}

static int
nvgpu_vm_clear_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_clear_plan *plan,
    uint64_t addr, uint64_t size, bool clear_sparse, bool clear_empty_range,
    bool preserve_target_pts, uint8_t preserve_page_shift,
    bool materialize_full_cover, uint32_t action)
{
	int err;

	nvgpu_vm_clear_plan_init(plan);
	plan->addr = addr;
	plan->size = size;
	plan->action = action;

	if (clear_sparse) {
		err = nvgpu_vm_bind_prepare_sparse_clear(vm, addr, size,
		    NVGPU_VM_PAGE_SHIFT_2M, NVGPU_VM_SPARSE_CLEAR_FINAL,
		    &plan->sparse_clear_plan);
		if (err != 0)
			goto fail;
	}

	err = nvgpu_vm_remove_plan_prepare(gpu, vm, &plan->remove_plan,
	    addr, size, clear_empty_range, preserve_target_pts,
	    preserve_page_shift, materialize_full_cover, action);
	if (err != 0)
		goto fail;

	return (0);

fail:
	nvgpu_vm_clear_plan_fini(vm, plan);
	return (err);
}

static int
nvgpu_vm_clear_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_clear_plan *plan,
    uint32_t *punmapped, struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	int err;

	err = nvgpu_vm_remove_plan_commit(gpu, vm, &plan->remove_plan,
	    punmapped, dirty_set, retired_bindings);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bind_commit_sparse_clear_noflush(gpu, vm,
	    &plan->sparse_clear_plan, plan->action, plan->addr, plan->size,
	    dirty_set);
	if (err != 0)
		return (err);

	return (0);
}

/*
 * nvgpu_vm_segment_remove_plan_*()
 *
 * Ownership:
 *   The plan borrows a caller-owned, contiguous segment array and owns detached
 *   tail bindings plus a materialize plan for the whole covered range.  Commit
 *   clears valid PTE/PDE state per segment page_shift, then performs one mapping
 *   splice for the full range.
 *
 * Lifetime:
 *   The borrowed segments must outlive prepare, commit, and fini.  Prepared
 *   tail/materialize ownership is private to this plan until commit consumes it
 *   or fini aborts it.  Retired bindings remain caller-owned after commit.
 *
 * Threading:
 *   Prepare may allocate/pin and must run before the no-fail clear section.
 *   Commit requires VM_BIND/GSP serialization and does not flush; visibility is
 *   still published by the enclosing VM_BIND invalidate boundary.
 */
static void
nvgpu_vm_segment_remove_plan_init(
    struct nvgpu_vm_segment_remove_plan *plan)
{
	LIST_INIT(&plan->tail_bindings);
	nvgpu_vm_materialize_plan_init(&plan->materialize_plan);
	plan->segments = NULL;
	plan->segment_count = 0;
	plan->addr = 0;
	plan->size = 0;
	plan->clear_action = NVGPU_VM_TRACE_MAP_SPARSE;
}

static void
nvgpu_vm_segment_remove_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_segment_remove_plan *plan)
{
	nvgpu_vm_bindings_free_prepared(&plan->tail_bindings);
	nvgpu_vm_materialize_plan_fini(vm->backend,
	    &plan->materialize_plan);
	nvgpu_vm_segment_remove_plan_init(plan);
}

static int
nvgpu_vm_segment_remove_plan_check_coverage(
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint64_t addr, uint64_t size)
{
	uint64_t cur, end;

	if (segments == NULL || segment_count == 0 || size == 0 ||
	    addr > UINT64_MAX - size)
		return (EINVAL);
	cur = addr;
	end = addr + size;
	for (uint32_t i = 0; i < segment_count; i++) {
		uint64_t seg_end;

		if (segments[i].size == 0 ||
		    segments[i].addr != cur ||
		    segments[i].addr > UINT64_MAX - segments[i].size)
			return (EINVAL);
		seg_end = segments[i].addr + segments[i].size;
		if (seg_end > end)
			return (EINVAL);
		cur = seg_end;
	}
	if (cur != end)
		return (EINVAL);
	return (0);
}

static int
nvgpu_vm_segment_remove_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    struct nvgpu_vm_segment_remove_plan *plan,
    const struct nvgpu_vm_bind_segment *segments, uint32_t segment_count,
    uint64_t addr, uint64_t size, uint32_t clear_action)
{
	int err;

	nvgpu_vm_segment_remove_plan_init(plan);
	err = nvgpu_vm_segment_remove_plan_check_coverage(segments,
	    segment_count, addr, size);
	if (err != 0)
		return (err);
	plan->segments = segments;
	plan->segment_count = segment_count;
	plan->addr = addr;
	plan->size = size;
	plan->clear_action = clear_action;

	if (addr >= vm->vm_bindings_max_end)
		return (0);

	err = nvgpu_vm_bindings_prepare_remove_range(vm, addr, size,
	    &plan->tail_bindings);
	if (err != 0)
		return (err);

	err = nvgpu_vm_materialize_plan_prepare_map_conflicts(gpu, vm,
	    &plan->materialize_plan, segments, segment_count, addr, size);
	if (err != 0) {
		nvgpu_vm_segment_remove_plan_fini(vm, plan);
		return (err);
	}
	err = nvgpu_vm_materialize_plan_preflight(vm,
	    &plan->materialize_plan);
	if (err != 0) {
		nvgpu_vm_segment_remove_plan_fini(vm, plan);
		return (err);
	}
	return (0);
}

static int
nvgpu_vm_segment_remove_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm,
    struct nvgpu_vm_segment_remove_plan *plan, uint32_t *punmapped,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_binding *binding, *next;
	uint64_t end = plan->addr + plan->size;
	int err;

	*punmapped = 0;
	if (plan->addr >= vm->vm_bindings_max_end)
		return (0);

	err = nvgpu_vm_materialize_plan_commit(gpu, vm,
	    &plan->materialize_plan, dirty_set, retired_bindings);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < plan->segment_count; i++) {
		const struct nvgpu_vm_bind_segment *seg = &plan->segments[i];

		for (binding = nvgpu_vm_binding_first_overlap(vm,
		    seg->addr, seg->size); binding != NULL; binding =
		    nvgpu_vm_binding_next_overlap(vm, binding,
		    seg->addr, seg->size)) {
			(*punmapped)++;
		}
	}

	for (uint32_t i = 0; i < plan->segment_count; i++) {
		const struct nvgpu_vm_bind_segment *seg = &plan->segments[i];

		for (binding = nvgpu_vm_binding_first_overlap(vm,
		    seg->addr, seg->size); binding != NULL; binding =
		    nvgpu_vm_binding_next_overlap(vm, binding,
		    seg->addr, seg->size)) {
			uint64_t old_start, old_end, cut_start, cut_end;

			old_start = binding->addr;
			old_end = binding->addr + binding->size;
			cut_start = old_start > seg->addr ? old_start : seg->addr;
			cut_end = old_end < seg->addr + seg->size ? old_end :
			    seg->addr + seg->size;
			err = nvgsp_vmm_check_unmap_valid_range_page(
			    vm->backend, cut_start, cut_end - cut_start, true,
			    seg->page_shift);
			if (err != 0)
				return (err);
		}
	}

	for (uint32_t i = 0; i < plan->segment_count; i++) {
		const struct nvgpu_vm_bind_segment *seg = &plan->segments[i];

		for (binding = nvgpu_vm_binding_first_overlap(vm,
		    seg->addr, seg->size); binding != NULL; binding =
		    nvgpu_vm_binding_next_overlap(vm, binding,
		    seg->addr, seg->size)) {
			uint64_t old_start, old_end, cut_start, cut_end;

			old_start = binding->addr;
			old_end = binding->addr + binding->size;
			cut_start = old_start > seg->addr ? old_start : seg->addr;
			cut_end = old_end < seg->addr + seg->size ? old_end :
			    seg->addr + seg->size;
			nvgpu_vm_bind_note_clear_shape(gpu,
			    binding->bo, binding->pte_kind,
			    binding->page_shift, cut_end - cut_start);
			nvgpu_vm_bind_note_clear(gpu, plan->clear_action,
			    cut_end - cut_start);
			if (nvgpu_vm_bind_clear_is_conflict(
			    plan->clear_action)) {
				err =
				    nvgsp_vmm_clear_conflict_preserve_page_noflush(
				    vm->backend, cut_start, cut_end - cut_start,
				    seg->page_shift);
			} else {
				err =
				    nvgsp_vmm_unmap_valid_preserve_page_noflush(
				    vm->backend, cut_start, cut_end - cut_start,
				    seg->page_shift);
			}
			if (err != 0)
				return (err);
			nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
			    cut_start, cut_end - cut_start);
		}
	}

	for (binding = nvgpu_vm_binding_first_overlap(vm, plan->addr,
	    plan->size); binding != NULL; binding = next) {
		uint64_t old_start, old_end, cut_start, cut_end;
		uint64_t head_size, tail_size;

		next = nvgpu_vm_binding_next_overlap(vm, binding,
		    plan->addr, plan->size);
		old_start = binding->addr;
		old_end = binding->addr + binding->size;
		cut_start = old_start > plan->addr ? old_start : plan->addr;
		cut_end = old_end < end ? old_end : end;
		head_size = cut_start - old_start;
		tail_size = old_end - cut_end;

		if (head_size != 0 && tail_size != 0) {
			binding->size = head_size;
		} else if (head_size != 0) {
			binding->size = head_size;
		} else if (tail_size != 0) {
			nvgpu_vm_binding_rekey_tail(vm, binding,
			    cut_end, tail_size,
			    binding->bo_offset + cut_end - old_start);
		} else {
			binding->pte_installed = false;
			nvgpu_vm_binding_unlink_retire(binding,
			    retired_bindings);
		}
	}

	while ((binding = LIST_FIRST(&plan->tail_bindings)) != NULL) {
		LIST_REMOVE(binding, link);
		nvgpu_vm_binding_insert_sorted(vm, binding);
	}
	if (*punmapped != 0)
		nvgpu_vm_bindings_recalc_max_end(vm);
	return (0);
}

/*
 * nvgpu_vm_sparse_map_plan_*()
 *
 * Ownership:
 *   The plan owns every fallible object prepared for one MAP|SPARSE op: the
 *   sparse segment array, unlinked sparse regions, the sparse-clear plan for
 *   old sparse reservations, and the valid target-clear plan.  Commit consumes
 *   those bos into the VMM sparse tree, PTE/PDE state, and live mapping
 *   tree.
 *
 * Lifetime:
 *   The caller keeps VM_BIND serialization from prepare through commit/fini.
 *   On abort, fini releases every unpublished sparse region and prepared clear
 *   object.  On successful commit, valid bindings removed by the target clear
 *   stay in retired_bindings until the VM_BIND done fence is visible.
 *
 * Threading:
 *   Prepare may allocate sparse regions, split old sparse records, clone/pin
 *   valid tail bindings, and inspect VMM state.  Commit runs under the
 *   VM_BIND/GSP mutation boundary and does not flush; the enclosing VM_BIND
 *   invalidate publishes the dirty PTE/PDE writes.
 */
static void
nvgpu_vm_sparse_map_plan_init(struct nvgpu_vm_sparse_map_plan *plan)
{
	nvgpu_vm_bind_segment_plan_init(&plan->segment_plan);
	nvgpu_vm_segment_remove_plan_init(&plan->target_clear_plan);
	plan->sparse_regions = NULL;
	plan->sparse_clear_plan = NULL;
	plan->addr = 0;
	plan->size = 0;
}

static void
nvgpu_vm_sparse_map_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_sparse_map_plan *plan)
{
	if (plan->sparse_regions != NULL) {
		for (uint32_t i = 0; i < plan->segment_plan.count; i++) {
			if (plan->sparse_regions[i] != NULL)
				nvgsp_vmm_abort_sparse_region(vm->backend,
				    plan->sparse_regions[i]);
		}
		_kfree(plan->sparse_regions, M_NVGPU_VM);
	}
	nvgpu_vm_bind_abort_sparse_clear(vm, &plan->sparse_clear_plan);
	nvgpu_vm_segment_remove_plan_fini(vm, &plan->target_clear_plan);
	nvgpu_vm_bind_segment_plan_fini(&plan->segment_plan);
	nvgpu_vm_sparse_map_plan_init(plan);
}

static int
nvgpu_vm_sparse_map_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_sparse_map_plan *plan,
    uint64_t addr, uint64_t size)
{
	int err;

	nvgpu_vm_sparse_map_plan_init(plan);
	plan->addr = addr;
	plan->size = size;

	err = nvgpu_vm_bind_build_sparse_segments(gpu, addr, size,
	    &plan->segment_plan);
	if (err != 0)
		goto fail;

	plan->sparse_regions = kmalloc((size_t)plan->segment_plan.count *
	    sizeof(*plan->sparse_regions), M_NVGPU_VM, M_WAITOK);
	if (plan->sparse_regions == NULL) {
		err = ENOMEM;
		goto fail;
	}
	for (uint32_t i = 0; i < plan->segment_plan.count; i++)
		plan->sparse_regions[i] = NULL;
	for (uint32_t i = 0; i < plan->segment_plan.count; i++) {
		const struct nvgpu_vm_bind_segment *seg =
		    &plan->segment_plan.segments[i];

		err = nvgsp_vmm_prepare_sparse_region_page(vm->backend,
		    seg->addr, seg->size, seg->page_shift,
		    &plan->sparse_regions[i]);
		if (err != 0) {
			err = -err;
			goto fail;
		}
	}

		err = nvgpu_vm_bind_prepare_sparse_clear(vm, addr, size,
		    nvgpu_vm_bind_segments_min_page_shift(
		    plan->segment_plan.segments, plan->segment_plan.count),
		    NVGPU_VM_SPARSE_CLEAR_TARGET_OVERWRITE,
		    &plan->sparse_clear_plan);
	if (err != 0)
		goto fail;

	err = nvgpu_vm_segment_remove_plan_prepare(gpu, vm,
	    &plan->target_clear_plan, plan->segment_plan.segments,
	    plan->segment_plan.count, addr, size, NVGPU_VM_TRACE_MAP_SPARSE);
	if (err != 0)
		goto fail;

	err = nvgsp_vmm_check_sparse_regions_commit(vm->backend,
	    plan->sparse_regions, plan->segment_plan.count, addr, size);
	if (err != 0) {
		err = -err;
		goto fail;
	}

	return (0);

fail:
	nvgpu_vm_sparse_map_plan_fini(vm, plan);
	return (err);
}

static int
nvgpu_vm_sparse_map_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_sparse_map_plan *plan,
    uint32_t op_flags, uint32_t *punmapped,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	int err;

	err = nvgpu_vm_segment_remove_plan_commit(gpu, vm,
	    &plan->target_clear_plan, punmapped, dirty_set, retired_bindings);
	if (err != 0)
		return (err);

	err = nvgpu_vm_bind_commit_sparse_clear_noflush(gpu, vm,
	    &plan->sparse_clear_plan, NVGPU_VM_TRACE_MAP_SPARSE, plan->addr,
	    plan->size, dirty_set);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < plan->segment_plan.count; i++) {
		err = nvgsp_vmm_commit_sparse_noflush(vm->backend,
		    plan->sparse_regions[i]);
		if (err != 0) {
			nvgsp_vmm_abort_sparse_region(vm->backend,
			    plan->sparse_regions[i]);
			plan->sparse_regions[i] = NULL;
			return (err);
		}
		nvgpu_vm_bind_note_dirty_range(gpu, dirty_set,
		    plan->segment_plan.segments[i].addr,
		    plan->segment_plan.segments[i].size);
		plan->sparse_regions[i] = NULL;
	}

	nvgpu_vm_bind_note_map_segments(gpu, op_flags,
	    plan->segment_plan.segments, plan->segment_plan.count, NULL);
	return (0);
}

/*
 * nvgpu_vm_bindings_remove_range()
 *
 * Ownership:
 *   Compatibility wrapper for current VM_BIND callers.  It owns one temporary
 *   remove plan and releases any uncommitted prepared bos before return.
 *
 * Lifetime:
 *   The caller still owns retired_bindings until the VM_BIND done fence becomes
 *   visible.  Hardware writes are published by the caller's final VMM invalidate.
 *
 * Threading:
 *   Requires vm->vm_token and the caller's GSP/VMM mutation boundary.
 */
static int
nvgpu_vm_bindings_remove_range(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, uint64_t addr, uint64_t size,
    bool clear_empty_range, bool preserve_target_pts,
    uint8_t preserve_page_shift, bool materialize_full_cover,
    uint32_t clear_action, uint32_t *punmapped,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_remove_plan plan;
	int err;

	err = nvgpu_vm_remove_plan_prepare(gpu, vm, &plan, addr, size,
	    clear_empty_range, preserve_target_pts, preserve_page_shift,
	    materialize_full_cover, clear_action);
	if (err != 0)
		return (err);
	err = nvgpu_vm_remove_plan_commit(gpu, vm, &plan, punmapped,
	    dirty_set, retired_bindings);
	nvgpu_vm_remove_plan_fini(vm, &plan);
	return (err);
}

static int
nvgpu_vm_binding_add(struct nvgpu_vm *vm, uint64_t addr,
    uint64_t size, struct nvgpu_bo *bo, uint64_t bo_offset,
    bool bo_pinned)
{
	struct nvgpu_vm_binding *binding;
	int err;

	binding = nvgpu_vm_binding_alloc(vm, addr, size, bo, bo_offset,
	    0, NVGPU_VM_PAGE_SHIFT_4K);
	if (binding == NULL)
		return (ENOMEM);

	binding->bo_pinned = bo_pinned;
	err = nvgpu_vm_binding_pin(binding);
	if (err != 0) {
		_kfree(binding, M_NVGPU_VM);
		return (err);
	}
	nvgpu_vm_binding_insert_sorted(vm, binding);
	return (0);
}


static void
nvgpu_vm_bind_record_error(struct nvgpu_device *gpu, uint32_t op,
    uint32_t flags, uint32_t handle, uint64_t addr, uint64_t range,
    uint64_t bo_offset, int err)
{
	return;
};

enum nvgpu_vm_op_plan_kind {
	NVGPU_VM_OP_PLAN_NONE,
	NVGPU_VM_OP_PLAN_CLEAR,
	NVGPU_VM_OP_PLAN_MAP_NULL,
	NVGPU_VM_OP_PLAN_MAP_SPARSE,
	NVGPU_VM_OP_PLAN_MAP_VALID,
	NVGPU_VM_OP_PLAN_MAP_NOOP,
};

struct nvgpu_vm_valid_op_plan {
	struct nvgpu_vm_bind_segment_plan segment_plan;
	struct nvgpu_vm_valid_map_plan valid_map_plan;
	struct nvgsp_vmm_sparse_unmap_plan *sparse_clear_plan;
	struct nvgpu_bo *bo;
	bool bo_temp_pinned;
	bool bo_temp_no_evict_pinned;
	bool had_sparse_clear;
	bool fast_noop;
	uint8_t pte_kind;
};

struct nvgpu_vm_op_plan {
	enum nvgpu_vm_op_plan_kind kind;
	uint32_t action;
	uint32_t op_flags;
	uint32_t handle;
	uint64_t addr;
	uint64_t size;
	uint64_t bo_offset;
	union {
		struct nvgpu_vm_clear_plan clear_plan;
		struct nvgpu_vm_sparse_map_plan sparse_map_plan;
		struct nvgpu_vm_valid_op_plan valid_map_plan;
	} u;
};

struct nvgpu_vm_batch_plan {
	struct nvgpu_vm_bind_op *ops;
	struct nvgpu_vm_bind_op *failed_op;
	struct nvgpu_vm_op_plan current_op;
	struct nvgpu_vm_dirty_set dirty_set;
	uint32_t op_count;
	bool current_op_active;
};

/*
 * nvgpu_vm_op_plan_*()
 *
 * Ownership:
 *   The top-level op plan owns exactly one prepared VM_BIND operation.  It
 *   delegates storage ownership to the per-op clear, sparse-map, or valid-map
 *   plan and additionally owns transient GEM/BO/segment state for valid MAP.
 *
 * Lifetime:
 *   The caller keeps VM_BIND serialization from prepare through commit/fini.
 *   Fini is required on every path after prepare is attempted.  Published live
 *   bindings still move to retired_bindings and outlive the done fence, matching
 *   the existing nouveau-visible completion ordering.
 *
 * Threading:
 *   Prepare is the only phase that may allocate, lookup GEM handles, pin BOs, or
 *   populate TT pages.  Commit runs inside the VM_BIND/GSP mutation boundary and
 *   only consumes prepared state, writes PTE/PDEs, updates software mappings, and
 *   records the enclosing dirty set.
 */
static void
nvgpu_vm_op_plan_init(struct nvgpu_vm_op_plan *plan)
{
	memset(plan, 0, sizeof(*plan));
	plan->kind = NVGPU_VM_OP_PLAN_NONE;
}

static void
nvgpu_vm_valid_op_plan_init(struct nvgpu_vm_valid_op_plan *plan)
{
	nvgpu_vm_bind_segment_plan_init(&plan->segment_plan);
	nvgpu_vm_valid_map_plan_init(&plan->valid_map_plan);
	plan->sparse_clear_plan = NULL;
	plan->bo = NULL;
	plan->bo_temp_pinned = false;
	plan->bo_temp_no_evict_pinned = false;
	plan->had_sparse_clear = false;
	plan->fast_noop = false;
	plan->pte_kind = 0;
}

static void
nvgpu_vm_valid_op_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_valid_op_plan *plan)
{
	if (plan->bo_temp_pinned && plan->bo != NULL)
		(void)nvgpu_bo_vm_bind_unpin(plan->bo,
		    plan->bo_temp_no_evict_pinned);
	plan->bo_temp_pinned = false;
	plan->bo_temp_no_evict_pinned = false;
	nvgpu_vm_valid_map_plan_fini(vm, &plan->valid_map_plan);
	nvgpu_vm_bind_abort_sparse_clear(vm, &plan->sparse_clear_plan);
	nvgpu_vm_bind_segment_plan_fini(&plan->segment_plan);
	nvgpu_vm_valid_op_plan_init(plan);
}

static void
nvgpu_vm_op_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_op_plan *plan)
{
	switch (plan->kind) {
	case NVGPU_VM_OP_PLAN_CLEAR:
	case NVGPU_VM_OP_PLAN_MAP_NULL:
		nvgpu_vm_clear_plan_fini(vm, &plan->u.clear_plan);
		break;
	case NVGPU_VM_OP_PLAN_MAP_SPARSE:
		nvgpu_vm_sparse_map_plan_fini(vm,
		    &plan->u.sparse_map_plan);
		break;
	case NVGPU_VM_OP_PLAN_MAP_VALID:
	case NVGPU_VM_OP_PLAN_MAP_NOOP:
		nvgpu_vm_valid_op_plan_fini(vm,
		    &plan->u.valid_map_plan);
		break;
	case NVGPU_VM_OP_PLAN_NONE:
		break;
	}
	nvgpu_vm_op_plan_init(plan);
}

static struct nvgpu_bo *
nvgpu_vm_op_plan_trace_obj(struct nvgpu_vm_op_plan *plan)
{
	switch (plan->kind) {
	case NVGPU_VM_OP_PLAN_MAP_VALID:
	case NVGPU_VM_OP_PLAN_MAP_NOOP:
		return (plan->u.valid_map_plan.bo);
	default:
		return (NULL);
	}
}

static void
nvgpu_vm_op_plan_prepare_common(struct nvgpu_vm_op_plan *plan,
    enum nvgpu_vm_op_plan_kind kind, uint32_t action,
    const struct nvgpu_vm_bind_op *op)
{
	plan->kind = kind;
	plan->action = action;
	plan->op_flags = op->flags;
	plan->handle = op->handle;
	plan->addr = op->addr;
	plan->size = op->range;
	plan->bo_offset = op->bo_offset;
}

static int
nvgpu_vm_op_plan_prepare_clear(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    const struct nvgpu_vm_bind_op *op)
{
	uint32_t action =
	    (op->flags & NVGPU_VM_BIND_SPARSE) != 0 ?
	    NVGPU_VM_TRACE_UNMAP_SPARSE : NVGPU_VM_TRACE_UNMAP;
	bool clear_sparse = (op->flags & NVGPU_VM_BIND_SPARSE) != 0;

	nvgpu_vm_op_plan_prepare_common(plan, NVGPU_VM_OP_PLAN_CLEAR,
	    action, op);
	nvgpu_vm_clear_plan_init(&plan->u.clear_plan);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND unmap op=%u flags=0x%08x handle=%u addr=0x%016jx range=0x%016jx\n",
	    op->op, op->flags, op->handle, (uintmax_t)op->addr,
	    (uintmax_t)op->range);
	nvgpu_vm_bind_note_op(gpu, action, op->range);
	return (nvgpu_vm_clear_plan_prepare(gpu, vm, &plan->u.clear_plan,
	    op->addr, op->range, clear_sparse, !clear_sparse, false,
	    NVGPU_VM_PAGE_SHIFT_4K, false, action));
}

static int
nvgpu_vm_op_plan_prepare_map_null(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    const struct nvgpu_vm_bind_op *op)
{
	nvgpu_vm_op_plan_prepare_common(plan, NVGPU_VM_OP_PLAN_MAP_NULL,
	    NVGPU_VM_TRACE_MAP_NULL, op);
	nvgpu_vm_clear_plan_init(&plan->u.clear_plan);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND map-null flags=0x%08x addr=0x%016jx range=0x%016jx\n",
	    op->flags, (uintmax_t)op->addr, (uintmax_t)op->range);
	nvgpu_vm_bind_note_op(gpu, NVGPU_VM_TRACE_MAP_NULL, op->range);
	return (nvgpu_vm_clear_plan_prepare(gpu, vm, &plan->u.clear_plan,
	    op->addr, op->range, true, true, false, NVGPU_VM_PAGE_SHIFT_4K, false,
	    NVGPU_VM_TRACE_MAP_NULL));
}

static int
nvgpu_vm_op_plan_prepare_map_sparse(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    const struct nvgpu_vm_bind_op *op)
{
	nvgpu_vm_op_plan_prepare_common(plan, NVGPU_VM_OP_PLAN_MAP_SPARSE,
	    NVGPU_VM_TRACE_MAP_SPARSE, op);
	nvgpu_vm_sparse_map_plan_init(&plan->u.sparse_map_plan);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND map sparse flags=0x%08x handle=%u addr=0x%016jx range=0x%016jx\n",
	    op->flags, op->handle, (uintmax_t)op->addr,
	    (uintmax_t)op->range);
	nvgpu_vm_bind_note_op(gpu, NVGPU_VM_TRACE_MAP_SPARSE, op->range);
	return (nvgpu_vm_sparse_map_plan_prepare(gpu, vm,
	    &plan->u.sparse_map_plan, op->addr, op->range));
}

static int
nvgpu_vm_op_plan_prepare_valid_map(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    const struct nvgpu_vm_bind_op *op)
{
	struct nvgpu_vm_valid_op_plan *valid = &plan->u.valid_map_plan;
	struct nvgpu_bo *bo;
	int err;

	nvgpu_vm_op_plan_prepare_common(plan, NVGPU_VM_OP_PLAN_MAP_VALID,
	    NVGPU_VM_TRACE_MAP, op);
	nvgpu_vm_valid_op_plan_init(valid);
	valid->pte_kind = op->flags & 0xff;
	bo = op->bo;
	valid->bo = bo;
	if (bo == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "nvgpu vm: VM_BIND missing BO handle=%u\n", op->handle);
		return (ENOENT);
	}
	if (op->bo_offset > nvgpu_bo_get_size(bo) ||
	    op->range > nvgpu_bo_get_size(bo) - op->bo_offset) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "nvgpu vm: VM_BIND BO range invalid handle=%u bo_off=0x%016jx range=0x%016jx size=0x%016jx\n",
		    op->handle, (uintmax_t)op->bo_offset,
		    (uintmax_t)op->range, (uintmax_t)nvgpu_bo_get_size(bo));
		return (EINVAL);
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "nvgpu vm: VM_BIND map flags=0x%08x handle=%u bo=%p domain=0x%x addr=0x%016jx bo_off=0x%016jx range=0x%016jx paddr=0x%016jx\n",
	    op->flags, op->handle, bo, bo->domain, (uintmax_t)op->addr,
	    (uintmax_t)op->bo_offset, (uintmax_t)op->range,
	    (uintmax_t)bo->paddr);
	nvgpu_vm_bind_note_op(gpu, NVGPU_VM_TRACE_MAP, op->range);

	if (!nvgsp_vmm_has_sparse_region(vm->backend, op->addr,
	    op->range) &&
	    nvgpu_vm_bindings_match_exact_map_op(vm, bo, op->addr,
	    op->range, op->bo_offset, valid->pte_kind)) {
		plan->kind = NVGPU_VM_OP_PLAN_MAP_NOOP;
		valid->fast_noop = true;
		return (0);
	}

	err = nvgpu_bo_vm_bind_pin(bo, &valid->bo_temp_no_evict_pinned);
	if (err != 0)
		return (err);
	valid->bo_temp_pinned = true;

	if (!nvgpu_bo_is_vram(bo)) {
		err = nvgpu_bo_ensure_ttm_populated(bo);
		if (err != 0)
			return (err);
	}
	err = nvgpu_vm_bind_build_map_segments(gpu, bo, op->addr,
	    op->bo_offset, op->range, &valid->segment_plan);
	if (err != 0)
		return (err);

	if (nvgpu_vm_bindings_match_map_range(vm, bo,
	    valid->segment_plan.segments, valid->segment_plan.count,
	    valid->pte_kind)) {
		err = nvgpu_vm_bind_prepare_sparse_clear(vm, op->addr,
		    op->range, NVGPU_VM_PAGE_SHIFT_2M,
		    NVGPU_VM_SPARSE_CLEAR_METADATA_ONLY,
		    &valid->sparse_clear_plan);
		if (err != 0)
			return (err);
		plan->kind = NVGPU_VM_OP_PLAN_MAP_NOOP;
		valid->had_sparse_clear = valid->sparse_clear_plan != NULL;
		return (0);
	}

	err = nvgpu_vm_bind_prepare_sparse_clear(vm, op->addr,
	    op->range, nvgpu_vm_bind_segments_min_page_shift(
	    valid->segment_plan.segments, valid->segment_plan.count),
	    NVGPU_VM_SPARSE_CLEAR_TARGET_OVERWRITE,
	    &valid->sparse_clear_plan);
	if (err != 0)
		return (err);

	nvgpu_vm_bind_debug_segments(gpu, valid->segment_plan.segments,
	    valid->segment_plan.count);
	err = nvgpu_vm_valid_map_plan_prepare(gpu, vm,
	    &valid->valid_map_plan, bo, valid->segment_plan.segments,
	    valid->segment_plan.count, op->addr, op->range, valid->pte_kind,
	    &valid->sparse_clear_plan);
	if (err != 0)
		return (err);
	(void)nvgpu_bo_vm_bind_unpin(bo, valid->bo_temp_no_evict_pinned);
	valid->bo_temp_pinned = false;
	valid->bo_temp_no_evict_pinned = false;
	return (0);
}

static int
nvgpu_vm_op_plan_prepare(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    const struct nvgpu_vm_bind_op *op)
{
	nvgpu_vm_op_plan_init(plan);
	switch (op->op) {
	case NVGPU_VM_BIND_OP_UNMAP:
		return (nvgpu_vm_op_plan_prepare_clear(gpu, vm, plan, op));
	case NVGPU_VM_BIND_OP_MAP:
		if ((op->flags & NVGPU_VM_BIND_SPARSE) != 0)
			return (nvgpu_vm_op_plan_prepare_map_sparse(gpu, vm,
			    plan, op));
		if (op->handle == 0)
			return (nvgpu_vm_op_plan_prepare_map_null(gpu, vm,
			    plan, op));
		return (nvgpu_vm_op_plan_prepare_valid_map(gpu, vm, plan, op));
	default:
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "nvgpu vm: VM_BIND unknown op=%u flags=0x%08x\n",
		    op->op, op->flags);
		return (EINVAL);
	}
}

static int
nvgpu_vm_op_plan_commit_preflight(struct nvgpu_vm *vm,
    struct nvgpu_vm_op_plan *plan)
{
	struct nvgpu_vm_valid_op_plan *valid;

	switch (plan->kind) {
	case NVGPU_VM_OP_PLAN_MAP_VALID:
		valid = &plan->u.valid_map_plan;
		return (nvgpu_vm_valid_map_plan_commit_preflight(vm,
		    &valid->valid_map_plan, valid->segment_plan.segments,
		    valid->segment_plan.count, valid->bo));
	default:
		return (0);
	}
}

static void
nvgpu_vm_op_plan_commit(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_op_plan *plan,
    struct nvgpu_vm_dirty_set *dirty_set,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	struct nvgpu_vm_valid_op_plan *valid;
	uint32_t unmapped = 0;
	int err;

	switch (plan->kind) {
	case NVGPU_VM_OP_PLAN_CLEAR:
	case NVGPU_VM_OP_PLAN_MAP_NULL:
		err = nvgpu_vm_clear_plan_commit(gpu, vm,
		    &plan->u.clear_plan, &unmapped, dirty_set,
		    retired_bindings);
		KASSERT(err == 0, ("prepared VM clear commit failed: %d", err));
		(void)nvgpu_vm_bindings_normalize(gpu, vm, plan->addr,
		    plan->size, dirty_set, retired_bindings);
		return;
	case NVGPU_VM_OP_PLAN_MAP_SPARSE:
		err = nvgpu_vm_sparse_map_plan_commit(gpu, vm,
		    &plan->u.sparse_map_plan, plan->op_flags, &unmapped,
		    dirty_set, retired_bindings);
		KASSERT(err == 0,
		    ("prepared VM sparse-map commit failed: %d", err));
		(void)nvgpu_vm_bindings_normalize(gpu, vm, plan->addr,
		    plan->size, dirty_set, retired_bindings);
		return;
	case NVGPU_VM_OP_PLAN_MAP_NOOP:
		valid = &plan->u.valid_map_plan;
		err = nvgpu_vm_bind_commit_sparse_clear_noflush(gpu, vm,
		    &valid->sparse_clear_plan, NVGPU_VM_TRACE_MAP, plan->addr,
		    plan->size, dirty_set);
		KASSERT(err == 0,
		    ("prepared VM sparse-clear commit failed: %d", err));
		(void)nvgpu_vm_bindings_normalize(gpu, vm, plan->addr,
		    plan->size, dirty_set, retired_bindings);
		return;
	case NVGPU_VM_OP_PLAN_MAP_VALID:
		valid = &plan->u.valid_map_plan;
		err = nvgpu_vm_valid_map_plan_commit(gpu, vm,
		    &valid->valid_map_plan, valid->segment_plan.segments,
		    valid->segment_plan.count, valid->bo, plan->op_flags,
		    plan->addr, plan->size, dirty_set, retired_bindings);
		KASSERT(err == 0,
		    ("prepared VM valid-map commit failed: %d", err));
		nvgpu_vm_bind_mark_bo_tiled(valid->bo, valid->pte_kind);
		nvgpu_vm_bindings_normalize(gpu, vm, plan->addr,
		    plan->size, dirty_set, retired_bindings);
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "nvgpu vm: VM_BIND track addr=0x%016jx size=0x%016jx bo=%p\n",
		    (uintmax_t)plan->addr, (uintmax_t)plan->size, valid->bo);
		return;
	case NVGPU_VM_OP_PLAN_NONE:
		break;
	}
	KASSERT(false, ("VM op commit without a prepared plan"));
}

/*
 * nvgpu_vm_batch_plan_*()
 *
 * Ownership:
 *   The batch plan borrows the VM_BIND op array and the optional per-op GEM
 *   object array from the ioctl/job owner.  It owns at most one active op plan
 *   at a time.  Submitted live bindings and retired old bindings are still
 *   transferred through the op plan into the caller-provided retired list.
 *
 * Lifetime:
 *   The batch plan is a sequential cursor, not an all-or-nothing transaction.
 *   Each op is prepared, committed, traced, and finished before the next op is
 *   touched.  If a later op fails during prepare, already committed earlier ops
 *   remain visible exactly as nouveau VM_BIND ordering requires.
 *
 * Threading:
 *   The caller must hold the same VM_BIND serialization used by the per-op plan
 *   path.  apply() runs inside the VM/GSP mutation window and only delays the
 *   final GMMU invalidate through dirty_set; it does not wait on GPU fences.
 */
static void
nvgpu_vm_batch_plan_init(struct nvgpu_vm_batch_plan *plan,
    struct nvgpu_vm_bind_op *ops, uint32_t op_count)
{
	memset(plan, 0, sizeof(*plan));
	plan->ops = ops;
	plan->op_count = op_count;
	nvgpu_vm_dirty_set_init(&plan->dirty_set);
	nvgpu_vm_op_plan_init(&plan->current_op);
}

static void
nvgpu_vm_batch_plan_fini(struct nvgpu_vm *vm,
    struct nvgpu_vm_batch_plan *plan)
{
	if (plan->current_op_active) {
		nvgpu_vm_op_plan_fini(vm, &plan->current_op);
		plan->current_op_active = false;
	}
}

/*
 * nvgpu_vm_bind_inject_prepare_failure()
 *
 * Ownership:
 *   Borrows gpu and consumes the one-shot debug counter when it fires.
 *
 * Lifetime:
 *   The helper is called after an op has prepared all temporary state and
 *   before commit publishes any mapping/PTE changes.  On injection, the caller
 *   must still run op-plan fini so prepared refs, pins, sparse plans, and
 *   segment storage are released without touching live VM state.
 *
 * Threading:
 *   Called while the VM_BIND mutation path is serialized by vm_token/gsp_tok.
 *   The sysctl writer may update the integer concurrently; this is debug-only
 *   and one-shot, so races can only change whether the next prepared op is
 *   injected, never the VM_BIND ABI or normal default-off behavior.
 */
static bool
nvgpu_vm_bind_inject_prepare_failure(struct nvgpu_device *gpu,
    uint32_t op_index)
{
	return (false);
}

/*
 * nvgpu_vm_bind_inject_commit_pt_failure()
 *
 * Ownership:
 *   Borrows gpu and the already prepared op plan.  It consumes the public
 *   one-shot counter and records the VA used for diagnostics.
 *
 * Lifetime:
 *   Called after the read-only commit preflight has proven prepared storage is
 *   present, but before materialize, sparse clear, PTE write, or mapping splice
 *   publishes any new state.  A non-zero return therefore leaves the old VM
 *   state unchanged while still exercising the commit-error path.
 *
 * Threading:
 *   Called under the VM_BIND mutation serialization.  The sysctl writer may
 *   race with the integer counter, but normal operation leaves it zero.
 */
static bool
nvgpu_vm_bind_inject_commit_pt_failure(struct nvgpu_device *gpu,
    const struct nvgpu_vm_op_plan *plan, uint32_t op_index)
{
	return (false);
}

/*
 * nvgpu_vm_bind_inject_parent_child_failure()
 *
 * Ownership:
 *   Borrows gpu and the already prepared op plan.  It consumes the one-shot
 *   debug counter and records the VA whose commit unit was rejected.
 *
 * Lifetime:
 *   Called after commit preflight has walked the prepared page-table state and
 *   before any materialize, sparse clear, PTE/PDE writer, or mapping splice is
 *   allowed to publish.  A non-zero return therefore simulates detection of a
 *   parent/child ownership invariant failure without corrupting live VMM state.
 *
 * Threading:
 *   Called under the serialized VM_BIND mutation path.  Concurrent sysctl
 *   writes are debug-only; the default-off path is unaffected and keeps the
 *   nouveau VM_BIND ABI unchanged.
 */
static bool
nvgpu_vm_bind_inject_parent_child_failure(struct nvgpu_device *gpu,
    const struct nvgpu_vm_op_plan *plan, uint32_t op_index)
{
	return (false);
}

static int
nvgpu_vm_batch_plan_apply(struct nvgpu_device *gpu,
    struct nvgpu_vm *vm, struct nvgpu_vm_batch_plan *plan,
    struct nvgpu_vm_binding_list *retired_bindings)
{
	int err = 0;


	for (uint32_t i = 0; i < plan->op_count; i++) {
		struct nvgpu_vm_bind_op *op = &plan->ops[i];

		plan->failed_op = op;
		if (op->range == 0 || op->addr > UINT64_MAX - op->range ||
		    ((op->addr | op->bo_offset | op->range) &
		     (NVGPU_VM_PAGE_SIZE_4K - 1))) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND invalid idx=%u op=%u flags=0x%08x handle=%u addr=0x%016jx bo_off=0x%016jx range=0x%016jx\n",
			    i, op->op, op->flags, op->handle,
			    (uintmax_t)op->addr, (uintmax_t)op->bo_offset,
			    (uintmax_t)op->range);
			return (EINVAL);
		}
		if (vm->kernel_managed_size != 0 &&
		    nvgpu_vm_ranges_overlap(op->addr, op->range,
		    vm->kernel_managed_addr, vm->kernel_managed_size)) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND overlaps kernel-managed VA idx=%u addr=0x%016jx range=0x%016jx\n",
			    i, (uintmax_t)op->addr, (uintmax_t)op->range);
			return (EINVAL);
		}

		nvgpu_vm_op_plan_init(&plan->current_op);
		plan->current_op_active = true;
		err = nvgpu_vm_op_plan_prepare(gpu, vm,
		    &plan->current_op, op);
		if (err == 0 &&
		    nvgpu_vm_bind_inject_prepare_failure(gpu, i))
			err = EIO;
		if (err == 0) {
			err = nvgpu_vm_op_plan_commit_preflight(vm,
			    &plan->current_op);
			if (err == 0 &&
			    nvgpu_vm_bind_inject_commit_pt_failure(gpu,
			    &plan->current_op, i))
				err = ENOENT;
			if (err == 0 &&
			    nvgpu_vm_bind_inject_parent_child_failure(gpu,
			    &plan->current_op, i))
				err = EIO;
			if (err == 0) {
				nvgpu_vm_op_plan_commit(gpu, vm,
				    &plan->current_op, &plan->dirty_set,
				    retired_bindings);
			}
		}
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "nvgpu vm: VM_BIND op failed idx=%u op=%u flags=0x%08x err=%d\n",
			    i, op->op, op->flags, err);
			return (err);
		}
		nvgpu_vm_op_plan_fini(vm, &plan->current_op);
		plan->current_op_active = false;
	}
	plan->failed_op = NULL;
	return (0);
}

struct nvgpu_vm_bind_future {
	struct nvgpu_future base;
	struct nvgpu_proc *proc;
	struct nvgpu_vm *vm;
	struct nvgpu_vm_bind_op *ops;
	struct nvgpu_fence *done;
	struct nvgpu_vm_binding_list retired_bindings;
	uint32_t op_count;
};

static volatile u_int nvgpu_vm_next_client = 0xc1d10000u;

static struct nvgpu_future_result nvgpu_vm_bind_future_poll(
    struct nvgpu_future *future);

int
nvgpu_vm_init(struct nvgpu_device *device, struct nvgpu_vm **vmp,
    size_t addr, size_t size)
{
	struct nvgpu_vm *vm;
	int error;

	if (device == NULL || vmp == NULL || (addr == 0) != (size == 0) ||
	    (addr != 0 && (addr > SIZE_MAX - size ||
	    ((addr | size) & (NVGPU_VM_PAGE_SIZE_4K - 1)) != 0)))
		return (EINVAL);
	vm = *vmp;
	if (vm != NULL) {
		if (addr == 0)
			return (EINVAL);
		lwkt_gettoken(&vm->vm_token);
		if ((vm->kernel_managed_addr != 0 ||
		    vm->kernel_managed_size != 0) &&
		    (vm->kernel_managed_addr != addr ||
		    vm->kernel_managed_size != size) &&
		    nvgpu_vm_binding_first_overlap(vm, addr, size) != NULL) {
			lwkt_reltoken(&vm->vm_token);
			return (EBUSY);
		}
		error = 0;
		if (vm->backend != NULL)
			error = nvgsp_vmm_set_kernel_va_range(vm->backend,
			    addr, size);
		if (error == 0) {
			vm->kernel_managed_addr = addr;
			vm->kernel_managed_size = size;
		}
		lwkt_reltoken(&vm->vm_token);
		return (error);
	}
	vm = kmalloc(sizeof(*vm), M_NVGPU_VM, M_WAITOK | M_ZERO);
	vm->gpu = device;
	vm->client_handle = atomic_fetchadd_int(&nvgpu_vm_next_client, 1);
	vm->kernel_managed_addr = addr;
	vm->kernel_managed_size = size;
	lwkt_token_init(&vm->vm_token, "nvgvm");
	LIST_INIT(&vm->vm_bindings);
	LIST_INIT(&vm->vm_validate_bindings);
	RB_INIT(&vm->vm_binding_tree);
	*vmp = vm;
	return (0);
}

int
nvgpu_vm_ensure_backend(struct nvgpu_device *device, struct nvgpu_vm **vmp,
    struct nvgsp_vmm **vmm)
{
	struct nvgpu_vm *vm;
	int error;

	if (device == NULL || vmp == NULL)
		return (EINVAL);
	vm = *vmp;
	if (vm == NULL) {
		error = nvgpu_vm_init(device, vmp, 0, 0);
		if (error != 0)
			return (error);
		vm = *vmp;
	}
	lwkt_gettoken(&vm->vm_token);
	if (vm->backend == NULL)
		error = nvgsp_vmm_create_user(vm->gpu, vm->client_handle,
		    vm->kernel_managed_addr, vm->kernel_managed_size,
		    &vm->backend);
	else
		error = 0;
	if (error == 0 && vmm != NULL)
		*vmm = vm->backend;
	lwkt_reltoken(&vm->vm_token);
	return (error);
}

int
nvgpu_vm_remap(struct nvgpu_vm *vm, struct nvgpu_vm_remap_args *args)
{
	struct nvgpu_vm_bind_future *bind;
	struct nvgpu_vm_binding *binding;
	struct nvgpu_bo **resv_bos;
	struct nvgpu_fence *done;
	uint32_t resv_count;
	int error;

	if (args == NULL)
		return (EINVAL);
	done = args->done;
	if (args->proc == NULL || vm == NULL || vm->backend == NULL || done == NULL ||
	    (args->op_count != 0 && args->ops == NULL) ||
	    (args->wait_count != 0 && args->waits == NULL)) {
		return (EINVAL);
	}
	resv_bos = NULL;
	resv_count = 0;
	for (;;) {
		uint32_t binding_count;
		uint32_t capacity;
		uint32_t seen;
		bool retry;

		binding_count = 0;
		lwkt_gettoken(&vm->vm_token);
		LIST_FOREACH(binding, &vm->vm_bindings, link)
			binding_count++;
		lwkt_reltoken(&vm->vm_token);
		capacity = args->op_count + binding_count;
		if (capacity != 0)
			resv_bos = kmalloc((size_t)capacity * sizeof(*resv_bos),
			    M_NVGPU_VM, M_WAITOK | M_ZERO);
		for (uint32_t i = 0; i < args->op_count; i++) {
			bool found;

			if (args->ops[i].bo == NULL)
				continue;
			found = false;
			for (uint32_t j = 0; j < resv_count; j++) {
				if (resv_bos[j] == args->ops[i].bo) {
					found = true;
					break;
				}
			}
			if (!found) {
				nvgpu_bo_addref(args->ops[i].bo);
				resv_bos[resv_count++] = args->ops[i].bo;
			}
		}

		seen = 0;
		retry = false;
		lwkt_gettoken(&vm->vm_token);
		LIST_FOREACH(binding, &vm->vm_bindings, link) {
			bool affected;
			bool found;

			if (seen++ == binding_count) {
				retry = true;
				break;
			}
			affected = false;
			for (uint32_t i = 0; i < args->op_count; i++) {
				if (nvgpu_vm_ranges_overlap(args->ops[i].addr,
				    args->ops[i].range, binding->addr,
				    binding->size)) {
					affected = true;
					break;
				}
			}
			if (!affected)
				continue;
			found = false;
			for (uint32_t i = 0; i < resv_count; i++) {
				if (resv_bos[i] == binding->bo) {
					found = true;
					break;
				}
			}
			if (!found) {
				nvgpu_bo_addref(binding->bo);
				resv_bos[resv_count++] = binding->bo;
			}
		}
		lwkt_reltoken(&vm->vm_token);
		if (!retry)
			break;
		for (uint32_t i = 0; i < resv_count; i++)
			nvgpu_bo_release(resv_bos[i]);
		if (resv_bos != NULL)
			_kfree(resv_bos, M_NVGPU_VM);
		resv_bos = NULL;
		resv_count = 0;
	}
	for (uint32_t i = 0; i < resv_count; i++) {
		error = nvgpu_bo_add_bookkeeping_fence(resv_bos[i], done);
		if (error != 0)
			break;
	}
	for (uint32_t i = 0; i < resv_count; i++)
		nvgpu_bo_release(resv_bos[i]);
	if (resv_bos != NULL)
		_kfree(resv_bos, M_NVGPU_VM);
	if (error != 0) {
		(void)nvgpu_fence_signal(done, error);
		return (error);
	}
	bind = kmalloc(sizeof(*bind), M_NVGPU_VM, M_WAITOK | M_ZERO);
	bind->proc = args->proc;
	bind->vm = vm;
	bind->op_count = args->op_count;
	if (bind->op_count != 0) {
		bind->ops = kmalloc((size_t)bind->op_count * sizeof(*bind->ops),
		    M_NVGPU_VM, M_WAITOK);
		memcpy(bind->ops, args->ops,
		    (size_t)bind->op_count * sizeof(*bind->ops));
		for (uint32_t i = 0; i < bind->op_count; i++)
			nvgpu_bo_addref(bind->ops[i].bo);
	}
	KTR_LOG(nvgpu_vm_bind_spawn, args->proc, vm, args->op_count,
	    args->wait_count);
	bind->done = done;
	bind->base.poll = nvgpu_vm_bind_future_poll;
	nvgpu_proc_addref(args->proc);
	nvgpu_fence_addref(done);
	LIST_INIT(&bind->retired_bindings);

	error = nvgpu_future_spawn(&bind->base, args->waits, args->wait_count);
	if (error != 0) {
		(void)nvgpu_fence_signal(done, error);
		nvgpu_fence_release(done);
		for (uint32_t i = 0; i < bind->op_count; i++)
			nvgpu_bo_release(bind->ops[i].bo);
		if (bind->ops != NULL)
			_kfree(bind->ops, M_NVGPU_VM);
		nvgpu_proc_release(args->proc);
		_kfree(bind, M_NVGPU_VM);
	}
	return (error);
}

static struct nvgpu_future_result
nvgpu_vm_bind_future_poll(struct nvgpu_future *future)
{
	struct nvgpu_vm_bind_future *bind;
	struct nvgpu_vm_batch_plan plan;
	struct nvgpu_vm_bind_op *failed_op;
	int error;

	bind = (struct nvgpu_vm_bind_future *)future;
	KTR_LOG(nvgpu_vm_bind_poll, future, 0u, bind->op_count, 0);
	nvgpu_vm_batch_plan_init(&plan, bind->ops, bind->op_count);
	lwkt_gettoken(&bind->vm->vm_token);
	nvgsp_vmm_begin_update(bind->vm->backend);
	error = nvgpu_vm_batch_plan_apply(bind->vm->gpu, bind->vm, &plan,
	    &bind->retired_bindings);
	KTR_LOG(nvgpu_vm_bind_poll, future, 1u, bind->op_count, error);
	failed_op = plan.failed_op;
	nvgpu_vm_batch_plan_fini(bind->vm, &plan);
	nvgpu_vm_dirty_set_publish(bind->vm->gpu, &plan.dirty_set);
	KTR_LOG(nvgpu_vm_bind_dirty, future, plan.dirty_set.dirty ? 1u : 0u);
	if (plan.dirty_set.dirty) {
		KTR_LOG(nvgpu_vm_bind_poll, future, 2u, bind->op_count, error);
		nvgpu_vm_dirty_set_flush(bind->vm->backend, &plan.dirty_set);
		KTR_LOG(nvgpu_vm_bind_poll, future, 3u, bind->op_count, error);
	}
	nvgsp_vmm_end_update(bind->vm->backend);
	lwkt_reltoken(&bind->vm->vm_token);
	KTR_LOG(nvgpu_vm_bind_poll, future, 4u, bind->op_count, error);
	if (error != 0 && failed_op != NULL)
		nvgpu_vm_bind_record_error(bind->vm->gpu, failed_op->op,
		    failed_op->flags, failed_op->handle, failed_op->addr,
		    failed_op->range, failed_op->bo_offset, error);
	(void)nvgpu_fence_signal(bind->done, error);
	nvgpu_fence_release(bind->done);
	for (uint32_t i = 0; i < bind->op_count; i++)
		nvgpu_bo_release(bind->ops[i].bo);
	if (bind->ops != NULL)
		_kfree(bind->ops, M_NVGPU_VM);
	nvgpu_vm_bindings_release(&bind->retired_bindings);
	nvgpu_proc_release(bind->proc);
	_kfree(bind, M_NVGPU_VM);
	return (NVGPU_FUTURE_READY(error));
}

void
nvgpu_vm_destroy(struct nvgpu_vm *vm)
{
	struct nvgpu_vm_binding *binding;

	if (vm == NULL)
		return;
	while ((binding = LIST_FIRST(&vm->vm_bindings)) != NULL)
		nvgpu_vm_binding_unlink_free(binding);
	if (vm->backend != NULL)
		nvgsp_vmm_destroy_user(vm->backend);
	lwkt_token_uninit(&vm->vm_token);
	_kfree(vm, M_NVGPU_VM);
}
