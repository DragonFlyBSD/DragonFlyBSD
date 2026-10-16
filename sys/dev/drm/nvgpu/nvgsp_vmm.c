/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * NVIDIA GSP VMM and userspace GMMU page-table backend.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>

#include "nvgpu_bo.h"
#include "nvgpu_debug.h"
#include "nvgsp_priv.h"
#include "nvgsp_rm.h"
#include "nvgsp_vmm.h"

#include <linux/ktime.h>

static MALLOC_DEFINE(M_NVGSP_VMM, "nvgsp_vmm", "nvgsp GMMU page tables");

static uint64_t
nvgsp_vmm_profile_now_us(struct nvgsp_state *sc)
{
	if (sc == NULL)
		return (0);
	return ((uint64_t)ktime_to_us(ktime_get()));
}

static void
nvgsp_vmm_profile_add_us(struct nvgsp_state *sc, uint64_t *total,
    uint64_t start_us)
{
	uint64_t end_us;

	if (sc == NULL || start_us == 0)
		return;

	end_us = nvgsp_vmm_profile_now_us(sc);
	if (end_us >= start_us)
		*total += end_us - start_us;
}

/* The fixed split — both endpoints are documented in
 * Linux nouveau rm/r535/nvrm/vmm.h:
 *   SPLIT_VAS_SERVER_RM_MANAGED_VA_START  0x100000000  (4 GiB)
 *   SPLIT_VAS_SERVER_RM_MANAGED_VA_SIZE       0x20000000 (512 MiB)
 */
/* gp100 PDE encoding:
 *   bits [2:1] aperture (1=VRAM, 2=SYS_COH, 3=SYS_NCOH)
 *   bit  [3]   VOL (set for SYS_COH)
 *   bits [39:4] (paddr >> 4)
 *   Non-zero aperture = PDE valid.
 */
#define NVGSP_PDE_APERTURE_SYS_COH	(2ULL << 1)


/* NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES — params from
 * Linux nouveau rm/r535/nvrm/vmm.h. Up to GMMU_FMT_MAX_LEVELS=6
 * level entries. We use 3 (PD3, PD2, PD1). */
#define NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES	0x90f10106U
#define FERMI_VASPACE_A		0x000090f1u
#define TURING_USERMODE_A	0x0000c461u
#define NVGSP_RM_VASPACE	0x90f10000u
#define NVGSP_RM_USERMODE	0xc4610000u
#define NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY	0x00801813u
#define NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY	0x00801814u
#define NVGSP_VASPACE_FLAG_IS_EXTERNALLY_OWNED	(1u << 3)
#define NVGSP_DMA_SET_PDB_FLAGS_APERTURE_VIDMEM	0u
/* aperture values for NV90F1_CTRL_..._PDES levels[i].aperture
 * (RPC enum, NOT the PDE-encoding bits). r535/nvrm/vmm.h: same as
 * GMMU_APERTURE enum values without the shift. */
#define NV_COPY_PDE_APERTURE_INVALID	0
#define NV_COPY_PDE_APERTURE_VIDMEM	1
#define NV_COPY_PDE_APERTURE_SYS_COH	2
#define NV_COPY_PDE_APERTURE_SYS_NCOH	3

struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_LEVEL {
	uint64_t physAddress;
	uint64_t size;		/* bytes occupied at this level */
	uint32_t aperture;
	uint32_t pageShift;
};
struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS {
	uint32_t hSubDevice;	/* 0 -> use subDeviceId */
	uint32_t subDeviceId;
	uint64_t pageSize;
	uint64_t virtAddrLo;
	uint64_t virtAddrHi;
	uint32_t numLevelsToCopy;
	uint8_t  _pad[4];
	struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_LEVEL levels[6];
};

struct nvgsp_vaspace_params {
	uint32_t index;
	int32_t flags;
	uint64_t vaSize;
	uint64_t vaStartInternal;
	uint64_t vaLimitInternal;
	uint32_t bigPageSize;
	uint8_t pad[4];
	uint64_t vaBase;
};

struct nvgsp_dma_set_page_directory_params {
	uint64_t phys_address;
	uint32_t num_entries;
	uint32_t flags;
	uint32_t h_vaspace;
	uint32_t chid;
	uint32_t subdevice_id;
	uint32_t pasid;
};

struct nvgsp_dma_unset_page_directory_params {
	uint32_t h_vaspace;
	uint32_t subdevice_id;
};

static uint32_t
nvgsp_vmm_get_child_handle(const struct nvgsp_client *client, uint32_t base)
{
	return (base | (client->object.handle & 0x00000fffu));
}

static int
nvgsp_vmm_pt_alloc(struct nvgsp_state *sc, struct nvgsp_vmm_pt *pt)
{
	return (nvgsp_bar_alloc_bar1_page_kind(sc, &pt->page,
	    NVGSP_VRAM_VMM_PT, pt));
}

static void
nvgsp_vmm_pt_free(struct nvgsp_state *sc, struct nvgsp_vmm_pt *pt)
{
	nvgsp_bar_free_bar1_page(sc, &pt->page);
}

static void
nvgsp_vmm_zero_bar1_page(struct nvgsp_state *sc, const struct nvgsp_bar1_page *p)
{
	nvgsp_bar_set_bar1_region64(sc, p->bar1_gva, 0,
	    NVGSP_GMMU_PT_PAGE_SIZE / sizeof(uint64_t));
	nvgsp_bar_flush_bar1(sc);
}

static void
nvgsp_vmm_sparse_bar1_page(struct nvgsp_state *sc,
    const struct nvgsp_bar1_page *p, uint64_t pte)
{
	nvgsp_bar_set_bar1_region64(sc, p->bar1_gva, pte,
	    NVGSP_GMMU_PT_PAGE_SIZE / sizeof(uint64_t));
	nvgsp_bar_flush_bar1(sc);
}

/*
 * nvgsp_vmm_sparse_region_overlaps_range()
 *
 * Ownership:
 *   Borrows a sparse-region record and caller-owned scalar range.  It does
 *   not take sparse ownership, mutate the list, or inspect page tables.
 *
 * Lifetime:
 *   The returned value is valid only while the caller's sparse-list walk keeps
 *   the region record alive.  Corrupt or overflowing ranges are reported as
 *   overlapping so direct valid/invalid writers fail closed instead of
 *   treating broken sparse metadata as absence.
 *
 * Threading:
 *   Caller owns the required VMM serialization.  This helper is pure scalar
 *   arithmetic and performs no allocation, BAR1 access, or GPU wait.
 */
static int
nvgsp_vmm_sparse_region_overlaps_range(
    const struct nvgsp_vmm_sparse_region *region, uint64_t va,
    uint64_t size)
{
	uint64_t end, region_end;

	if (size == 0)
		return (0);
	if (region == NULL || va > UINT64_MAX - size ||
	    region->size == 0 ||
	    region->addr > UINT64_MAX - region->size)
		return (1);
	end = va + size;
	region_end = region->addr + region->size;
	return (region->addr < end && region_end > va);
}

static int
nvgsp_vmm_range_has_sparse_region(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size)
{
	struct nvgsp_vmm_sparse_region *region;

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		if (nvgsp_vmm_sparse_region_overlaps_range(region, va,
		    size))
			return (1);
	}
	return (0);
}

/*
 * nvgsp_vmm_has_sparse_region()
 *
 * Ownership:
 *   Borrows the VMM and caller-owned scalar range.  It does not acquire,
 *   release, insert, remove, or retain sparse-region ownership.
 *
 * Lifetime:
 *   The result is a read-only snapshot for the caller's current serialized
 *   VM_BIND prepare decision.  Corrupt sparse metadata is reported as present
 *   through nvgsp_vmm_sparse_region_overlaps_range(), so callers fail
 *   closed instead of skipping required sparse cleanup.
 *
 * Threading:
 *   Acquires vmm->tok for the sparse-list walk.  This helper is prepare-stage
 *   read-only code; it performs no allocation, BAR1 write, flush, or GPU wait.
 */
int
nvgsp_vmm_has_sparse_region(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	int has_sparse;

	lwkt_gettoken(&vmm->tok);
	has_sparse = nvgsp_vmm_range_has_sparse_region(vmm, va, size);
	lwkt_reltoken(&vmm->tok);
	return (has_sparse);
}

/*
 * nvgsp_vmm_sparse_state_run_locked()
 *
 * Ownership:
 *   Borrows vmm's sparse-region list and the caller-owned VA cursor.  It does
 *   not acquire, release, insert, or remove sparse-region ownership.
 *
 * Lifetime:
 *   The result is valid while the caller keeps vmm->tok and VM remap
 *   serialization held.  *run_size is bounded by [va, limit) and stops at the
 *   next sparse-state transition, so callers can batch one same-state PTE run.
 *   Corrupt sparse records create a fail-closed transition: callers either see
 *   sparse state or an invalid run boundary before mutating PTEs.
 *
 * Threading:
 *   Callers hold vmm->tok.  The helper only scans linked sparse records and
 *   does not write PTEs, allocate memory, or wait on GPU work.
 */
static int
nvgsp_vmm_sparse_state_run_locked(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t limit, uint64_t *run_size)
{
	struct nvgsp_vmm_sparse_region *region;
	uint64_t boundary = limit;
	int sparse = 0;

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		uint64_t region_end;

		if (region->size == 0 ||
		    region->addr > UINT64_MAX - region->size) {
			if (region->addr >= limit)
				continue;
			if (region->addr <= va) {
				sparse = 1;
				boundary = limit;
			} else if (region->addr < boundary) {
				boundary = region->addr;
			}
			continue;
		}
		region_end = region->addr + region->size;

		if (region_end <= va || region->addr >= limit)
			continue;
		if (region->addr <= va && region_end > va) {
			sparse = 1;
			if (region_end < boundary)
				boundary = region_end;
		} else if (region->addr > va && region->addr < boundary) {
			boundary = region->addr;
		}
	}
	if (boundary <= va || boundary > limit)
		boundary = limit;
	*run_size = boundary - va;
	return (sparse);
}

static int nvgsp_vmm_sparse_region_page_size(uint8_t page_shift,
    uint64_t *page_size);
static int nvgsp_vmm_sparse_region_insert_preflight_locked(
    struct nvgsp_vmm *vmm, const struct nvgsp_vmm_sparse_region *region);
static void nvgsp_vmm_sparse_regions_merge_locked(
    struct nvgsp_vmm *vmm, struct nvgsp_vmm_sparse_region *region);

static void
nvgsp_vmm_pd0_write_slot(struct nvgsp_state *sc,
    const struct nvgsp_vmm_pd0 *pd0, uint32_t pd0_idx,
    uint64_t big_pde, uint64_t small_pde)
{
	nvgsp_bar_wr64_bar1(sc, pd0->page.bar1_gva + (pd0_idx * 2 + 0) * 8,
	    big_pde);
	nvgsp_bar_wr64_bar1(sc, pd0->page.bar1_gva + (pd0_idx * 2 + 1) * 8,
	    small_pde);
}

/*
 * nvgsp_vmm_pd0_mark_slot_state()
 *
 * Ownership:
 *   Borrows pd0 and updates only the software state for one PD0 slot.  The
 *   helper does not own or release the child PT or mapped BO backing.
 *
 * Lifetime:
 *   The recorded state must describe the hardware PD0 slot written by the
 *   caller in the same commit section.  The pd0 object must remain linked
 *   until all non-empty slot states are cleared.
 *
 * Threading:
 *   Callers hold vmm->tok.  This is the single accounting point for PD0 child
 *   table, 2 MiB valid leaf, and 2 MiB sparse leaf ownership.
 */
static void
nvgsp_vmm_pd0_mark_slot_state(struct nvgsp_vmm_pd0 *pd0,
    uint32_t pd0_idx, enum nvgsp_vmm_pd0_slot_state new_state)
{
	enum nvgsp_vmm_pd0_slot_state old_state;

	old_state = pd0->slot_state[pd0_idx];
	if (old_state == new_state)
		return;

	switch (old_state) {
	case NVGSP_VMM_PD0_SLOT_CHILD:
		if (pd0->refcount > 0)
			pd0->refcount--;
		break;
	case NVGSP_VMM_PD0_SLOT_VALID_2M:
		if (pd0->valid_2m_count > 0)
			pd0->valid_2m_count--;
		break;
	case NVGSP_VMM_PD0_SLOT_SPARSE_2M:
		if (pd0->sparse_2m_count > 0)
			pd0->sparse_2m_count--;
		break;
	default:
		break;
	}

	pd0->slot_state[pd0_idx] = (uint8_t)new_state;

	switch (new_state) {
	case NVGSP_VMM_PD0_SLOT_CHILD:
		pd0->refcount++;
		break;
	case NVGSP_VMM_PD0_SLOT_VALID_2M:
		pd0->valid_2m_count++;
		break;
	case NVGSP_VMM_PD0_SLOT_SPARSE_2M:
		pd0->sparse_2m_count++;
		break;
	default:
		break;
	}
}

static int
nvgsp_vmm_pd0_has_live_slot(const struct nvgsp_vmm_pd0 *pd0)
{
	return (pd0->refcount != 0 || pd0->valid_2m_count != 0 ||
	    pd0->sparse_2m_count != 0);
}

static void
nvgsp_vmm_pd0_write_child_slot(struct nvgsp_state *sc,
    struct nvgsp_vmm_pd0 *pd0, uint32_t pd0_idx, uint64_t big_pde,
    uint64_t small_pde)
{
	nvgsp_vmm_pd0_mark_slot_state(pd0, pd0_idx,
	    NVGSP_VMM_PD0_SLOT_CHILD);
	nvgsp_vmm_pd0_write_slot(sc, pd0, pd0_idx, big_pde, small_pde);
}

static void
nvgsp_vmm_pd0_write_empty_slot(struct nvgsp_state *sc,
    struct nvgsp_vmm_pd0 *pd0, uint32_t pd0_idx)
{
	nvgsp_vmm_pd0_mark_slot_state(pd0, pd0_idx,
	    NVGSP_VMM_PD0_SLOT_EMPTY);
	nvgsp_vmm_pd0_write_slot(sc, pd0, pd0_idx, 0, 0);
}

static void
nvgsp_vmm_pd0_write_valid_2m_slot(struct nvgsp_state *sc,
    struct nvgsp_vmm_pd0 *pd0, uint32_t pd0_idx, uint64_t pte)
{
	nvgsp_vmm_pd0_mark_slot_state(pd0, pd0_idx,
	    NVGSP_VMM_PD0_SLOT_VALID_2M);
	nvgsp_vmm_pd0_write_slot(sc, pd0, pd0_idx, pte, 0);
}

static void
nvgsp_vmm_pd0_write_sparse_2m_slot(struct nvgsp_state *sc,
    struct nvgsp_vmm_pd0 *pd0, uint32_t pd0_idx, uint64_t pde)
{
	nvgsp_vmm_pd0_mark_slot_state(pd0, pd0_idx,
	    NVGSP_VMM_PD0_SLOT_SPARSE_2M);
	nvgsp_vmm_pd0_write_slot(sc, pd0, pd0_idx, pde, 0);
}

static struct nvgsp_bar1_page *
nvgsp_vmm_pd1_base_page(struct nvgsp_vmm *vmm, uint32_t pd2_idx)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pd1 *pd1;
	int err;

	if (pd2_idx == 0)
		return (&vmm->pt[2].page);

	LIST_FOREACH(pd1, &vmm->user_pd1_pages, link) {
		if (pd1->pd2_idx == pd2_idx)
			return (&pd1->page);
	}

	pd1 = kmalloc(sizeof(*pd1), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	pd1->pd2_idx = pd2_idx;
	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pd1->page,
	    NVGSP_VRAM_VMM_PT, pd1);
	if (err != 0) {
		_kfree(pd1, M_NVGSP_VMM);
		return (NULL);
	}

	nvgsp_vmm_zero_bar1_page(sc, &pd1->page);
	nvgsp_bar_wr64_bar1(sc, vmm->pt[1].page.bar1_gva + pd2_idx * 8,
	    nvgsp_pde_to_vram(pd1->page.vram_paddr));
	nvgsp_bar_flush_bar1(sc);
	LIST_INSERT_HEAD(&vmm->user_pd1_pages, pd1, link);
	return (&pd1->page);
}

static void
nvgsp_vmm_invalidate(struct nvgsp_vmm *vmm)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint64_t pdb = vmm->pt[0].page.vram_paddr;
	uint32_t trig_rb = 0xffffffffu;

	/* Match nouveau tu102_vmm_flush(): target this VMM root PDB and
	 * invalidate all pages. BAR1/BAR2 have their own ALL_PDB path. */
	nvgsp_wr32(sc, 0xb830a0, (uint32_t)(pdb >> 8));
	nvgsp_wr32(sc, 0xb830a4, 0x00000000u);
	nvgsp_wr32(sc, 0xb830b0, 0x80000000u | 0x00000001u);
	for (int spin = 0; spin < 200000; spin++) {
		trig_rb = nvgsp_rd32(sc, 0xb830b0);
		if ((trig_rb & 0x80000000u) == 0)
			break;
		DELAY(10);
	}
	if ((trig_rb & 0x80000000u) != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gsp_vmm: invalidate timeout pdb=0x%llx trig=0x%08x\n",
		    (unsigned long long)pdb, trig_rb);
	}
}

void
nvgsp_vmm_begin_update(struct nvgsp_vmm *vmm)
{
	lwkt_gettoken(&vmm->gsp->gsp_tok);
}

void
nvgsp_vmm_end_update(struct nvgsp_vmm *vmm)
{
	lwkt_reltoken(&vmm->gsp->gsp_tok);
}

static uint32_t
nvgsp_vmm_pd0_hash(uint32_t pd2_idx, uint32_t pd1_idx)
{
	uint32_t hash;

	hash = pd2_idx * 2654435761U;
	hash ^= pd1_idx * 2246822519U;
	return (hash & (NVGSP_VMM_PD0_HASH_SIZE - 1));
}

static uint32_t
nvgsp_vmm_user_pt_hash(uint32_t pd2_idx, uint32_t pd1_idx,
    uint32_t pd0_idx)
{
	uint32_t hash;

	hash = pd2_idx * 2654435761U;
	hash ^= pd1_idx * 2246822519U;
	hash ^= pd0_idx * 3266489917U;
	return (hash & (NVGSP_VMM_USER_PT_HASH_SIZE - 1));
}

/*
 * nvgsp_vmm_*_lookup helpers
 *
 * Ownership:
 *   The lookup buckets borrow PD0/PT objects owned by user_pd0_pages and
 *   user_pt_pages.  They do not own memory and must be unlinked before the
 *   corresponding owner list node is freed.
 *
 * Lifetime:
 *   Objects are inserted only after their page-table storage and PDEs have
 *   been initialized.  A successful lookup therefore means the hardware PDE
 *   chain already exists and must not be rewritten just to reuse the PT.
 *
 * Threading:
 *   Callers hold vmm->tok.  The buckets are only an index for the same
 *   per-VMM page table state protected by that token.
 */
static struct nvgsp_vmm_pd0 *
nvgsp_vmm_pd0_find(struct nvgsp_vmm *vmm, uint32_t pd2_idx,
    uint32_t pd1_idx)
{
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t bucket;

	bucket = nvgsp_vmm_pd0_hash(pd2_idx, pd1_idx);
	LIST_FOREACH(pd0, &vmm->user_pd0_lookup[bucket], lookup_link) {
		if (pd0->pd2_idx == pd2_idx && pd0->pd1_idx == pd1_idx)
			return (pd0);
	}
	return (NULL);
}

static void
nvgsp_vmm_pd0_lookup_insert(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_pd0 *pd0)
{
	uint32_t bucket;

	bucket = nvgsp_vmm_pd0_hash(pd0->pd2_idx, pd0->pd1_idx);
	LIST_INSERT_HEAD(&vmm->user_pd0_lookup[bucket], pd0, lookup_link);
}

static int
nvgsp_vmm_pd0_get(struct nvgsp_vmm *vmm, uint32_t pd2_idx,
    uint32_t pd1_idx,
    struct nvgsp_vmm_pd0 **ppd0)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_bar1_page *pd1_page;
	struct nvgsp_vmm_pd0 *pd0;
	int err;

	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 != NULL) {
		*ppd0 = pd0;
		return (0);
	}

	pd1_page = nvgsp_vmm_pd1_base_page(vmm, pd2_idx);
	if (pd1_page == NULL)
		return (ENOMEM);

	pd0 = kmalloc(sizeof(*pd0), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	pd0->pd1_page = pd1_page;
	pd0->pd2_idx = pd2_idx;
	pd0->pd1_idx = pd1_idx;
	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pd0->page,
	    NVGSP_VRAM_VMM_PT, pd0);
	if (err != 0) {
		_kfree(pd0, M_NVGSP_VMM);
		return (err);
	}

	nvgsp_vmm_zero_bar1_page(sc, &pd0->page);
	nvgsp_bar_wr64_bar1(sc, pd1_page->bar1_gva + pd1_idx * 8,
	    nvgsp_pde_to_vram(pd0->page.vram_paddr));
	nvgsp_bar_flush_bar1(sc);
	LIST_INSERT_HEAD(&vmm->user_pd0_pages, pd0, link);
	nvgsp_vmm_pd0_lookup_insert(vmm, pd0);
	*ppd0 = pd0;
	return (0);
}

static struct nvgsp_vmm_user_pt *
nvgsp_vmm_user_pt_find(struct nvgsp_vmm *vmm, uint32_t pd2_idx,
    uint32_t pd1_idx, uint32_t pd0_idx)
{
	struct nvgsp_vmm_user_pt *pt;
	uint32_t bucket;

	bucket = nvgsp_vmm_user_pt_hash(pd2_idx, pd1_idx, pd0_idx);
	LIST_FOREACH(pt, &vmm->user_pt_lookup[bucket], lookup_link) {
		if (pt->pd2_idx == pd2_idx && pt->pd1_idx == pd1_idx &&
		    pt->pd0_idx == pd0_idx)
			return (pt);
	}
	return (NULL);
}

static void
nvgsp_vmm_user_pt_lookup_insert(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt)
{
	uint32_t bucket;

	bucket = nvgsp_vmm_user_pt_hash(pt->pd2_idx, pt->pd1_idx,
	    pt->pd0_idx);
	LIST_INSERT_HEAD(&vmm->user_pt_lookup[bucket], pt, lookup_link);
}

/*
 * nvgsp_vmm_inject_user_pt_alloc_failure()
 *
 * Ownership:
 *   Borrows vmm and sc.  The function only consumes the debug one-shot counter
 *   and records the VA that would have needed a new user PT.
 *
 * Lifetime:
 *   Called after lookup proves the user PT is absent, but before allocating or
 *   publishing any parent/leaf page-table storage.  A non-zero return therefore
 *   leaves the VM's page-table object lifetime graph unchanged.
 *
 * Threading:
 *   The caller holds vmm->tok.  The debug counter is per-device and intended for
 *   single-probe fault injection; normal paths leave it at zero.
 */
static int
nvgsp_vmm_inject_user_pt_alloc_failure(struct nvgsp_vmm *vmm,
    uint64_t va)
{
	if (vmm->pt_alloc_fail_after <= 0)
		return (0);
	if (vmm->pt_alloc_fail_after > 1) {
		vmm->pt_alloc_fail_after--;
		return (0);
	}

	vmm->pt_alloc_fail_after = 0;
	vmm->pt_alloc_fail_count++;
	vmm->pt_alloc_fail_last_va = va;
	return (ENOMEM);
}

static int
nvgsp_vmm_user_pt_get(struct nvgsp_vmm *vmm, uint64_t va,
    struct nvgsp_vmm_user_pt **ppt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *pt;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;
	int err;

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	pt = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx, pd0_idx);
	if (pt != NULL) {
		*ppt = pt;
		return (0);
	}

	err = nvgsp_vmm_inject_user_pt_alloc_failure(vmm, va);
	if (err != 0)
		return (err);

	err = nvgsp_vmm_pd0_get(vmm, pd2_idx, pd1_idx, &pd0);
	if (err != 0)
		return (err);
	if (pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_CHILD)
		return (EIO);
	if (pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_EMPTY)
		return (EBUSY);

	pt = kmalloc(sizeof(*pt), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	pt->pd0 = pd0;
	pt->pd2_idx = pd2_idx;
	pt->pd1_idx = pd1_idx;
	pt->pd0_idx = pd0_idx;
	pt->valid_pte_count = 0;
	pt->valid_lpte_count = 0;
	pt->sparse_pte_count = 0;

	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->lpt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;
	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->spt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;

	nvgsp_vmm_zero_bar1_page(sc, &pt->lpt);
	nvgsp_vmm_zero_bar1_page(sc, &pt->spt);

	/* PD1 owns one shared PD0 page. Each PD0 entry gets its own dual
	 * BIG/SMALL PDE pair, so mappings in the same 512 MiB range do not
	 * overwrite earlier PD0 entries. */
	nvgsp_vmm_pd0_write_child_slot(sc, pd0, pd0_idx,
	    nvgsp_pde_to_vram(pt->lpt.vram_paddr),
	    nvgsp_pde_to_vram(pt->spt.vram_paddr));
	nvgsp_bar_flush_bar1(sc);

	LIST_INSERT_HEAD(&vmm->user_pt_pages, pt, link);
	nvgsp_vmm_user_pt_lookup_insert(vmm, pt);
	*ppt = pt;
	return (0);

fail:
	if (pt->spt.vram_paddr != 0)
		nvgsp_bar_free_bar1_page(sc, &pt->spt);
	if (pt->lpt.vram_paddr != 0)
		nvgsp_bar_free_bar1_page(sc, &pt->lpt);
	_kfree(pt, M_NVGSP_VMM);
	return (err);
}

static uint32_t
nvgsp_vmm_spt_idx(uint64_t va)
{
	return ((uint32_t)((va >> NVGSP_GMMU_SPT_SHIFT) &
	    (NVGSP_GMMU_SPT_ENTRIES - 1)));
}

static uint32_t
nvgsp_vmm_lpt_idx(uint64_t va)
{
	return ((uint32_t)((va >> NVGSP_GMMU_LPT_SHIFT) &
	    (NVGSP_GMMU_LPT_ENTRIES - 1)));
}

static uint32_t
nvgsp_vmm_page_shift_bucket(uint8_t page_shift)
{
	switch (page_shift) {
	case NVGSP_GMMU_SPT_SHIFT:
		return (NVGSP_VMM_PAGE_SHIFT_4K);
	case NVGSP_GMMU_LPT_SHIFT:
		return (NVGSP_VMM_PAGE_SHIFT_64K);
	case NVGSP_GMMU_PD0_SHIFT:
		return (NVGSP_VMM_PAGE_SHIFT_2M);
	default:
		return (NVGSP_VMM_PAGE_SHIFT_4K);
	}
}

static int
nvgsp_vmm_spt_mask_test(const uint64_t *mask, uint32_t spt_idx)
{
	return ((mask[spt_idx >> 6] & (1ULL << (spt_idx & 63))) != 0);
}

static void
nvgsp_vmm_spt_mask_set(uint64_t *mask, uint32_t spt_idx)
{
	mask[spt_idx >> 6] |= 1ULL << (spt_idx & 63);
}

static void
nvgsp_vmm_spt_mask_clear(uint64_t *mask, uint32_t spt_idx)
{
	mask[spt_idx >> 6] &= ~(1ULL << (spt_idx & 63));
}

/*
 * nvgsp_vmm_pt_check_promote_64k()
 *
 * Ownership:
 *   Borrows one linked userspace PT and checks only software leaf-state.  It
 *   does not write BAR1, allocate page tables, or mutate refcounts.
 *
 * Lifetime:
 *   The result is valid while vmm->tok and VM remap serialization keep the PT
 *   linked and unchanged.  A successful result means the requested LPT slots
 *   are empty and every covered SPT leaf is a valid non-sparse mapping.
 *
 * Threading:
 *   Caller holds vmm->tok.  This is the read-only preflight half of
 *   4 KiB -> 64 KiB promotion; the paired commit may then clear the lower
 *   SPT leaves and install LPT leaves without discovering a semantic conflict.
 */
static int
nvgsp_vmm_pt_check_promote_64k(const struct nvgsp_vmm_user_pt *pt,
    uint32_t lpt_idx, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		uint32_t cur_lpt = lpt_idx + i;
		uint32_t bit = 1U << cur_lpt;
		uint32_t spt_start = cur_lpt * NVGSP_GMMU_LPT_SPTE_COUNT;

		if ((pt->valid_lpt_mask & bit) != 0 ||
		    (pt->sparse_lpt_mask & bit) != 0)
			return (EBUSY);
		for (uint32_t j = 0; j < NVGSP_GMMU_LPT_SPTE_COUNT; j++) {
			uint32_t spt_idx = spt_start + j;

			if (!nvgsp_vmm_spt_mask_test(pt->valid_spt_mask,
			    spt_idx) ||
			    nvgsp_vmm_spt_mask_test(pt->sparse_spt_mask,
			    spt_idx))
				return (EBUSY);
		}
	}
	return (0);
}

/*
 * nvgsp_vmm_spt_mark_state()
 *
 * Ownership:
 *   Borrows pt and updates only its software SPT leaf-state.  It does not
 *   write BAR1, allocate page-table pages, or acquire mapping ownership.
 *
 * Lifetime:
 *   The state describes the hardware PTE write that the caller performs in
 *   the same no-fail commit section.  The PT must remain linked until both the
 *   software state and hardware PTE write complete.
 *
 * Threading:
 *   Callers hold vmm->tok.  This helper is the single ref/unref authority for
 *   SPT valid/sparse counts, including bulk paths that do not read old PTEs.
 */
static void
nvgsp_vmm_spt_mark_state(struct nvgsp_vmm_user_pt *pt,
    uint32_t spt_idx, int new_valid, int new_sparse)
{
	int old_valid, old_sparse;

	if (new_valid)
		new_sparse = 0;
	old_valid = nvgsp_vmm_spt_mask_test(pt->valid_spt_mask, spt_idx);
	old_sparse = nvgsp_vmm_spt_mask_test(pt->sparse_spt_mask, spt_idx);

	if (new_valid) {
		nvgsp_vmm_spt_mask_set(pt->valid_spt_mask, spt_idx);
		nvgsp_vmm_spt_mask_clear(pt->sparse_spt_mask, spt_idx);
	} else {
		nvgsp_vmm_spt_mask_clear(pt->valid_spt_mask, spt_idx);
		if (new_sparse)
			nvgsp_vmm_spt_mask_set(pt->sparse_spt_mask,
			    spt_idx);
		else
			nvgsp_vmm_spt_mask_clear(pt->sparse_spt_mask,
			    spt_idx);
	}

	if (!old_valid && new_valid) {
		pt->valid_pte_count++;
	} else if (old_valid && !new_valid && pt->valid_pte_count > 0) {
		pt->valid_pte_count--;
	}
	if (!old_sparse && new_sparse) {
		pt->sparse_pte_count++;
	} else if (old_sparse && !new_sparse &&
	    pt->sparse_pte_count > 0) {
		pt->sparse_pte_count--;
	}
}

static void
nvgsp_vmm_spt_mark_range_state(struct nvgsp_vmm_user_pt *pt,
    uint32_t spt_idx, uint32_t count, int new_valid, int new_sparse)
{
	uint32_t i;

	for (i = 0; i < count; i++)
		nvgsp_vmm_spt_mark_state(pt, spt_idx + i, new_valid,
		    new_sparse);
}

static int
nvgsp_vmm_lpt_mask_test(const struct nvgsp_vmm_user_pt *pt,
    uint32_t lpt_idx)
{
	return ((pt->valid_lpt_mask & (1U << lpt_idx)) != 0);
}

/*
 * nvgsp_vmm_lpt_mark_state()
 *
 * Ownership:
 *   Borrows pt and updates only its software LPT leaf-state.  Hardware LPT
 *   writes are performed by the caller in the same commit section.
 *
 * Lifetime:
 *   The PT must remain linked until the paired software state and hardware
 *   BAR1 write complete.  The caller owns the final VMM flush boundary.
 *
 * Threading:
 *   Callers hold vmm->tok.  This helper is the single ref/unref authority for
 *   LPT valid/sparse counts.  Valid and sparse are mutually exclusive; invalid
 *   clears both.
 */
static void
nvgsp_vmm_lpt_mark_state(struct nvgsp_vmm_user_pt *pt,
    uint32_t lpt_idx, int new_valid, int new_sparse)
{
	int old_valid, old_sparse;
	uint32_t bit;

	if (new_valid)
		new_sparse = 0;
	old_valid = nvgsp_vmm_lpt_mask_test(pt, lpt_idx);
	old_sparse = (pt->sparse_lpt_mask & (1U << lpt_idx)) != 0;
	bit = 1U << lpt_idx;
	if (new_valid) {
		pt->valid_lpt_mask |= bit;
		pt->sparse_lpt_mask &= ~bit;
	} else {
		pt->valid_lpt_mask &= ~bit;
		if (new_sparse)
			pt->sparse_lpt_mask |= bit;
		else
			pt->sparse_lpt_mask &= ~bit;
	}

	if (!old_valid && new_valid) {
		pt->valid_lpte_count++;
	} else if (old_valid && !new_valid &&
	    pt->valid_lpte_count > 0) {
		pt->valid_lpte_count--;
	}
	if (!old_sparse && new_sparse) {
		pt->sparse_lpte_count++;
	} else if (old_sparse && !new_sparse &&
	    pt->sparse_lpte_count > 0) {
		pt->sparse_lpte_count--;
	}
}

static void
nvgsp_vmm_lpt_mark_range_state(struct nvgsp_vmm_user_pt *pt,
    uint32_t lpt_idx, uint32_t count, int new_valid, int new_sparse)
{
	uint32_t i;

	for (i = 0; i < count; i++)
		nvgsp_vmm_lpt_mark_state(pt, lpt_idx + i, new_valid,
		    new_sparse);
}

static void
nvgsp_vmm_mark_spt_from_pte(struct nvgsp_vmm_user_pt *pt,
    uint32_t spt_idx, uint64_t pte)
{
	uint64_t sparse_pte;

	sparse_pte = nvgsp_pte_to_sparse();
	nvgsp_vmm_spt_mark_state(pt, spt_idx,
	    pte != 0 && pte != sparse_pte, pte == sparse_pte);
}

static void
nvgsp_vmm_mark_spt_range_from_pte(struct nvgsp_vmm_user_pt *pt,
    uint32_t spt_idx, uint32_t count, uint64_t pte)
{
	uint64_t sparse_pte;

	sparse_pte = nvgsp_pte_to_sparse();
	nvgsp_vmm_spt_mark_range_state(pt, spt_idx, count,
	    pte != 0 && pte != sparse_pte, pte == sparse_pte);
}

enum nvgsp_vmm_clear_purpose {
	NVGSP_VMM_CLEAR_FINAL_INVALID = 0,
	NVGSP_VMM_CLEAR_CONFLICT,
};

static void nvgsp_vmm_note_bulk_write(struct nvgsp_vmm *vmm,
    uint8_t page_shift, uint64_t count);
static void nvgsp_vmm_note_bulk_clear(struct nvgsp_vmm *vmm,
    uint8_t page_shift, uint64_t count);
static void nvgsp_vmm_note_skip_clear(struct nvgsp_vmm *vmm,
    uint8_t page_shift, uint64_t count);
static void nvgsp_vmm_note_conflict_clear(struct nvgsp_vmm *vmm,
    uint8_t page_shift, uint64_t count);
static void nvgsp_vmm_note_final_clear(struct nvgsp_vmm *vmm,
    uint8_t page_shift, uint64_t count);
static void nvgsp_vmm_note_clear_by_purpose(struct nvgsp_vmm *vmm,
    enum nvgsp_vmm_clear_purpose purpose, uint8_t page_shift,
    uint64_t count);

/*
 * nvgsp_vmm_clear_lpt_range_purpose()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  It does not allocate or free
 *   page-table pages and does not change the VM_BIND tracker.
 *
 * Lifetime:
 *   The PT must remain linked in vmm while the LPT entries are cleared.
 *   Callers must keep any higher-level binding state consistent with the
 *   cleared big-page coverage before publishing the final VMM flush.
 *
 * Threading:
 *   Callers hold vmm->tok.  The clear is a synchronous BAR1 write; the caller
 *   owns the final flush/TLB invalidate boundary.
 */
static void
nvgsp_vmm_clear_lpt_range_purpose(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t lpt_idx, uint32_t count,
    enum nvgsp_vmm_clear_purpose purpose)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t cleared, end, cur;

	if (count == 0)
		return;
	end = lpt_idx + count;
	cleared = 0;
	cur = lpt_idx;
	while (cur < end) {
		uint32_t start, run;

		while (cur < end &&
		    (((pt->valid_lpt_mask | pt->sparse_lpt_mask) &
		    (1U << cur)) == 0))
			cur++;
		start = cur;
		while (cur < end &&
		    (((pt->valid_lpt_mask | pt->sparse_lpt_mask) &
		    (1U << cur)) != 0))
			cur++;
		run = cur - start;
		if (run == 0)
			continue;

		/* Software leaf-state is the fact source; skip empty LPT slots. */
		nvgsp_vmm_lpt_mark_range_state(pt, start, run, 0, 0);
		nvgsp_bar_set_bar1_region64(sc,
		    pt->lpt.bar1_gva + (uint64_t)start * 8, 0, run);
		vmm->stats.pte_fast_clear_count += run;
		vmm->stats.pte_fast_invalid_clear_count += run;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_LPT_SHIFT, run);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] +=
		    run;
		nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
		    NVGSP_GMMU_LPT_SHIFT, run);
		cleared += run;
	}
	if (count > cleared)
		nvgsp_vmm_note_skip_clear(vmm, NVGSP_GMMU_LPT_SHIFT,
		    count - cleared);
}

static void
nvgsp_vmm_clear_lpt_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t lpt_idx, uint32_t count)
{
	nvgsp_vmm_clear_lpt_range_purpose(vmm, pt, lpt_idx, count,
	    NVGSP_VMM_CLEAR_CONFLICT);
}

static void
nvgsp_vmm_clear_lpt_for_spt_range_purpose(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint32_t count,
    enum nvgsp_vmm_clear_purpose purpose)
{
	uint32_t lpt_start, lpt_end;

	if (count == 0)
		return;
	lpt_start = spt_idx / NVGSP_GMMU_LPT_SPTE_COUNT;
	lpt_end = (spt_idx + count + NVGSP_GMMU_LPT_SPTE_COUNT - 1) /
	    NVGSP_GMMU_LPT_SPTE_COUNT;
	nvgsp_vmm_clear_lpt_range_purpose(vmm, pt, lpt_start,
	    lpt_end - lpt_start, purpose);
}

static void
nvgsp_vmm_clear_lpt_for_spt_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint32_t count)
{
	nvgsp_vmm_clear_lpt_for_spt_range_purpose(vmm, pt, spt_idx,
	    count, NVGSP_VMM_CLEAR_CONFLICT);
}

static int
nvgsp_vmm_write_pt_pte(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint64_t va, uint64_t pte)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t spt_idx;

	spt_idx = nvgsp_vmm_spt_idx(va);
	nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, 1);
	nvgsp_vmm_mark_spt_from_pte(pt, spt_idx, pte);
	nvgsp_bar_wr64_bar1(sc, pt->spt.bar1_gva + spt_idx * 8, pte);
	vmm->stats.pte_read_modify_write_count++;
	if (pte != 0 && pte != nvgsp_pte_to_sparse()) {
		vmm->stats.pte_leaf_write_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)]++;
	} else {
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)]++;
	}
	return (0);
}

static int
nvgsp_vmm_write_pte(struct nvgsp_vmm *vmm, uint64_t va, uint64_t pte)
{
	struct nvgsp_vmm_user_pt *pt;
	int err;

	err = nvgsp_vmm_user_pt_get(vmm, va, &pt);
	if (err != 0)
		return (err);
	return (nvgsp_vmm_write_pt_pte(vmm, pt, va, pte));
}

/*
 * nvgsp_vmm_write_new_valid_pt_pte()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT. It does not allocate page-table
 *   pages or retain caller-owned mapping state.
 *
 * Lifetime:
 *   The PT must remain linked in vmm while this helper writes the PTE. Callers
 *   hold vmm->tok and pass an spt_idx within that PT.
 *
 * Threading:
 *   Writes one valid leaf PTE synchronously through BAR1. The caller owns the
 *   final flush/TLB invalidate boundary.
 */
static void
nvgsp_vmm_write_new_valid_pt_pte_raw(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint64_t pte)
{
	struct nvgsp_state *sc = vmm->gsp;

	nvgsp_vmm_spt_mark_state(pt, spt_idx, 1, 0);
	nvgsp_bar_wr64_bar1(sc, pt->spt.bar1_gva + spt_idx * 8, pte);
	vmm->stats.pte_fast_write_count++;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)]++;
}

static void
nvgsp_vmm_write_new_valid_pt_pte(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint64_t pte)
{
	nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, 1);
	nvgsp_vmm_write_new_valid_pt_pte_raw(vmm, pt, spt_idx, pte);
}

/*
 * nvgsp_vmm_write_new_valid_pt_pte_linear()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT. It does not allocate page-table
 *   pages or retain caller-owned mapping state.
 *
 * Lifetime:
 *   The PT must remain linked in vmm while this helper writes the PTE range.
 *   The written PTEs become visible to the GPU only after the caller performs
 *   the enclosing VMM flush/TLB invalidate.
 *
 * Threading:
 *   Writes valid leaf PTEs synchronously through BAR1. Callers hold vmm->tok
 *   and own the final flush/TLB invalidate boundary.
 */
static void
nvgsp_vmm_write_new_valid_pt_pte_linear(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint64_t first_pte,
    uint64_t pte_step, uint32_t count)
{
	struct nvgsp_state *sc = vmm->gsp;

	if (count == 0)
		return;

	nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, count);
	nvgsp_vmm_spt_mark_range_state(pt, spt_idx, count, 1, 0);
	nvgsp_bar_write_bar1_linear_region64(sc,
	    pt->spt.bar1_gva + (uint64_t)spt_idx * 8, first_pte, pte_step,
	    count);
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)] += count;
}

/*
 * nvgsp_vmm_write_new_valid_pt_pte_paddrs()
 *
 * Ownership:
 *   Borrows vmm, an already-owned user PT, and a caller-owned DMA paddr
 *   snapshot.  It does not allocate page-table storage or retain the paddr
 *   array after returning.
 *
 * Lifetime:
 *   paddrs must cover count 4 KiB leaves starting at spt_idx.  The PT remains
 *   owned by vmm and the written PTEs become visible only after the enclosing
 *   VM_BIND flush/TLB invalidate.
 *
 * Threading:
 *   Callers hold vmm->tok and own the surrounding VM_BIND serialization.  This
 *   helper performs synchronous BAR1 writes and batches software leaf-state
 *   updates for one prepared SPT chunk.
 */
static void
nvgsp_vmm_write_new_valid_pt_pte_paddrs(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx,
    const vm_paddr_t *paddrs, uint32_t count, uint64_t kind_bits)
{
	struct nvgsp_state *sc = vmm->gsp;
	const uint64_t pte_step = NVGSP_GMMU_PT_PAGE_SIZE >> NVGSP_PT_ADDR_SHIFT;
	uint32_t run_start;

	if (count == 0)
		return;

	nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, count);
	nvgsp_vmm_spt_mark_range_state(pt, spt_idx, count, 1, 0);
	for (run_start = 0; run_start < count;) {
		uint32_t run = 1;
		uint64_t first_paddr = (uint64_t)paddrs[run_start];
		uint64_t first_pte = nvgsp_pte_to_sysmem(first_paddr) |
		    kind_bits;

		while (run_start + run < count &&
		    (uint64_t)paddrs[run_start + run] ==
		    first_paddr + (uint64_t)run * NVGSP_GMMU_PT_PAGE_SIZE) {
			run++;
		}
		nvgsp_bar_write_bar1_linear_region64(sc,
		    pt->spt.bar1_gva +
		    (uint64_t)(spt_idx + run_start) * 8,
		    first_pte, pte_step, run);
		run_start += run;
	}
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)] += count;
}

static int
nvgsp_vmm_write_new_valid_pte(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t pte)
{
	struct nvgsp_vmm_user_pt *pt;
	uint32_t spt_idx;
	int err;

	err = nvgsp_vmm_user_pt_get(vmm, va, &pt);
	if (err != 0)
		return (err);

	spt_idx = nvgsp_vmm_spt_idx(va);
	nvgsp_vmm_write_new_valid_pt_pte(vmm, pt, spt_idx, pte);
	return (0);
}

static uint32_t
nvgsp_vmm_spt_chunk_count(uint64_t va, uint64_t size)
{
	uint64_t remaining_pages = size / NVGSP_GMMU_PT_PAGE_SIZE;
	uint32_t spt_idx = nvgsp_vmm_spt_idx(va);

	return ((uint32_t)MIN(remaining_pages,
	    NVGSP_GMMU_SPT_ENTRIES - spt_idx));
}

static uint32_t
nvgsp_vmm_lpt_chunk_count(uint64_t va, uint64_t size)
{
	uint64_t remaining_pages = size / NVGSP_GMMU_LPT_PAGE_SIZE;
	uint32_t lpt_idx = nvgsp_vmm_lpt_idx(va);

	return ((uint32_t)MIN(remaining_pages,
	    NVGSP_GMMU_LPT_ENTRIES - lpt_idx));
}

static uint64_t
nvgsp_vmm_user_pt_chunk_size(uint64_t va, uint64_t size)
{
	uint64_t pt_offset;
	uint64_t pt_remaining;

	pt_offset = va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1);
	pt_remaining = (1ULL << NVGSP_GMMU_PD0_SHIFT) - pt_offset;
	return (MIN(size, pt_remaining));
}

static void
nvgsp_vmm_note_bulk_write(struct nvgsp_vmm *vmm, uint8_t page_shift,
    uint64_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t bucket;

	if (count == 0)
		return;
	bucket = nvgsp_vmm_page_shift_bucket(page_shift);
	vmm->stats.pte_bulk_write_count++;
	vmm->stats.pte_bulk_write_pages += count;
	vmm->stats.pte_write_batch_count[bucket]++;
}

static void
nvgsp_vmm_note_bulk_clear(struct nvgsp_vmm *vmm, uint8_t page_shift,
    uint64_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t bucket;

	if (count == 0)
		return;
	bucket = nvgsp_vmm_page_shift_bucket(page_shift);
	vmm->stats.pte_bulk_clear_count++;
	vmm->stats.pte_bulk_clear_pages += count;
	vmm->stats.pte_clear_batch_count[bucket]++;
}

static void
nvgsp_vmm_note_skip_clear(struct nvgsp_vmm *vmm, uint8_t page_shift,
    uint64_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t bucket;

	if (count == 0)
		return;
	bucket = nvgsp_vmm_page_shift_bucket(page_shift);
	vmm->stats.pte_skip_clear_count[bucket]++;
	vmm->stats.pte_skip_clear_pages[bucket] += count;
}

static void
nvgsp_vmm_note_conflict_clear(struct nvgsp_vmm *vmm, uint8_t page_shift,
    uint64_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t bucket;

	if (count == 0)
		return;
	bucket = nvgsp_vmm_page_shift_bucket(page_shift);
	vmm->stats.pte_conflict_clear_count[bucket]++;
	vmm->stats.pte_conflict_clear_pages[bucket] += count;
}

static void
nvgsp_vmm_note_final_clear(struct nvgsp_vmm *vmm, uint8_t page_shift,
    uint64_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t bucket;

	if (count == 0)
		return;
	bucket = nvgsp_vmm_page_shift_bucket(page_shift);
	vmm->stats.pte_final_clear_count[bucket]++;
	vmm->stats.pte_final_clear_pages[bucket] += count;
}

static void
nvgsp_vmm_note_clear_by_purpose(struct nvgsp_vmm *vmm,
    enum nvgsp_vmm_clear_purpose purpose, uint8_t page_shift,
    uint64_t count)
{
	if (purpose == NVGSP_VMM_CLEAR_CONFLICT)
		nvgsp_vmm_note_conflict_clear(vmm, page_shift, count);
	else
		nvgsp_vmm_note_final_clear(vmm, page_shift, count);
}

/*
 * nvgsp_vmm_ensure_pt_range()
 *
 * Ownership:
 *   Borrows vmm and ensures the host-owned page-table pages for the requested
 *   VA range exist.  It does not acquire ownership of caller data and does not
 *   install leaf PTEs.
 *
 * Lifetime:
 *   Allocated PT pages become part of vmm and live until the VMM destructor or
 *   later page-table reclamation.  The caller retains ownership of the range.
 *
 * Threading:
 *   Acquires vmm->tok internally.  Callers may already hold higher-level DRM or
 *   GSP serialization tokens.  This helper only prepares page-table storage; it
 *   does not publish a mapping and does not perform a final TLB invalidate.
 */
int
nvgsp_vmm_ensure_pt_range(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_vmm_user_pt *pt;
	uint64_t off;
	uint64_t chunk;
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += chunk) {
		err = nvgsp_vmm_user_pt_get(vmm, va + off, &pt);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
		chunk = nvgsp_vmm_user_pt_chunk_size(va + off, size - off);
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_ensure_pd0_range()
 *
 * Ownership:
 *   Borrows vmm and ensures the parent PD0 pages for the requested 2 MiB
 *   prepared-writer range exist.  It does not create LPT/SPT child tables and
 *   does not install PD0 leaf state.
 *
 * Lifetime:
 *   Allocated PD0 pages become owned by vmm.  The target PD0 slots remain
 *   empty until a prepared writer installs valid/sparse state in commit.
 *
 * Threading:
 *   Acquires vmm->tok internally.  This is a prepare-stage helper for direct
 *   2 MiB valid MAP and large sparse paths; it may allocate PD0 storage, so
 *   callers must not use it from the no-fail VM_BIND commit section.
 */
int
nvgsp_vmm_ensure_pd0_range(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_vmm_pd0 *pd0;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	uint64_t off;
	int err;

	if ((va | size) & (page_size - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += page_size) {
		uint64_t cur = va + off;
		uint32_t pd2_idx, pd1_idx;

		pd2_idx = (uint32_t)((cur >> NVGSP_GMMU_PD2_SHIFT) &
		    (NVGSP_GMMU_PD2_ENTRIES - 1));
		pd1_idx = (uint32_t)((cur >> NVGSP_GMMU_PD1_SHIFT) &
		    (NVGSP_GMMU_PD1_ENTRIES - 1));
		err = nvgsp_vmm_pd0_get(vmm, pd2_idx, pd1_idx, &pd0);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

static struct nvgsp_vmm_user_pt *
nvgsp_vmm_user_pt_find_va(struct nvgsp_vmm *vmm, uint64_t va);

/*
 * nvgsp_vmm_check_prepared_pt_range()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It does not allocate page
 *   tables, write BAR1, retain pointers, or change leaf-state.
 *
 * Lifetime:
 *   The check proves that a later VM_BIND prepared writer can direct-lookup
 *   every PT or PD0 page it will touch for this range and page size.  The
 *   caller must keep VM remap serialization until the checked writer runs,
 *   otherwise another remap could reclaim the page-table storage.
 *
 * Threading:
 *   Acquires vmm->tok for the lookup walk.  This belongs to VM_BIND prepare /
 *   pre-commit validation and mirrors the prepared writer's lookup granularity.
 *   For 2 MiB PD0 sparse this validates parent PD0 slots, not child PTs.
 */
static int
nvgsp_vmm_check_prepared_pt_range_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t page_shift)
{
	uint64_t off;

	if (size == 0)
		return (0);
	if (page_shift == NVGSP_GMMU_SPT_SHIFT) {
		if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
			return (EINVAL);
	} else if (page_shift == NVGSP_GMMU_LPT_SHIFT) {
		if ((va | size) & (NVGSP_GMMU_LPT_PAGE_SIZE - 1))
			return (EINVAL);
	} else if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
		if ((va | size) & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
			return (EINVAL);
	} else {
		return (EINVAL);
	}

	for (off = 0; off < size;) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint32_t count, pd0_idx;

		if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
			uint32_t pd2_idx, pd1_idx;

			pd2_idx = (uint32_t)((cur >> NVGSP_GMMU_PD2_SHIFT) &
			    (NVGSP_GMMU_PD2_ENTRIES - 1));
			pd1_idx = (uint32_t)((cur >> NVGSP_GMMU_PD1_SHIFT) &
			    (NVGSP_GMMU_PD1_ENTRIES - 1));
			pd0_idx = (uint32_t)((cur >> NVGSP_GMMU_PD0_SHIFT) &
			    (NVGSP_GMMU_PD0_ENTRIES - 1));
			pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
			if (pd0 == NULL)
				return (ENOENT);
			if (pd0->slot_state[pd0_idx] ==
			    NVGSP_VMM_PD0_SLOT_CHILD)
				return (EBUSY);
			off += 1ULL << NVGSP_GMMU_PD0_SHIFT;
			continue;
		}

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL)
			return (ENOENT);
		if (page_shift == NVGSP_GMMU_LPT_SHIFT) {
			count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
			off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
		} else {
			count = nvgsp_vmm_spt_chunk_count(cur, size - off);
			off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
		}
	}
	return (0);
}

int
nvgsp_vmm_check_prepared_pt_range(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, uint8_t page_shift)
{
	int err;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va, size,
	    page_shift);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

static struct nvgsp_vmm_user_pt *
nvgsp_vmm_user_pt_find_va(struct nvgsp_vmm *vmm, uint64_t va)
{
	uint32_t pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	uint32_t pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	uint32_t pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	return (nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx));
}

/*
 * nvgsp_vmm_clear_live_spt_range()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  It rewrites only SPT leaves
 *   whose software state is currently valid or sparse; empty leaves are left
 *   untouched and reported to the caller by the returned count.
 *
 * Lifetime:
 *   The PT must remain linked in vmm while the helper updates software
 *   leaf-state and matching BAR1 PTE storage.  The caller owns the final
 *   flush/TLB invalidate boundary.
 *
 * Threading:
 *   Callers hold vmm->tok.  The helper performs no allocation, BO lookup,
 *   mapping-tree edit, or GPU fence wait.
 */
static uint32_t
nvgsp_vmm_clear_live_spt_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint32_t count)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t cleared, cur, end;

	if (count == 0)
		return (0);
	cleared = 0;
	cur = spt_idx;
	end = spt_idx + count;
	while (cur < end) {
		uint32_t start, run;

		while (cur < end &&
		    !nvgsp_vmm_spt_mask_test(pt->valid_spt_mask, cur) &&
		    !nvgsp_vmm_spt_mask_test(pt->sparse_spt_mask, cur))
			cur++;
		start = cur;
		while (cur < end &&
		    (nvgsp_vmm_spt_mask_test(pt->valid_spt_mask, cur) ||
		    nvgsp_vmm_spt_mask_test(pt->sparse_spt_mask, cur)))
			cur++;
		run = cur - start;
		if (run == 0)
			continue;

		nvgsp_vmm_spt_mark_range_state(pt, start, run, 0, 0);
		nvgsp_bar_set_bar1_region64(sc,
		    pt->spt.bar1_gva + (uint64_t)start * 8, 0, run);
		vmm->stats.pte_fast_clear_count += run;
		vmm->stats.pte_fast_invalid_clear_count += run;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_SPT_SHIFT, run);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)] +=
		    run;
		cleared += run;
	}
	return (cleared);
}

/*
 * nvgsp_vmm_write_old_valid_pte_range()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  It does not allocate or free
 *   page-table pages and does not change sparse-region ownership.
 *
 * Lifetime:
 *   The PT must remain linked in vmm for the duration of the call.  Callers
 *   hold vmm->tok while walking the range.
 *
 * Threading:
 *   Writes a contiguous run of leaf PTEs synchronously through BAR1.  The
 *   caller is responsible for the final VMM flush/TLB invalidate boundary.
 */
static void
nvgsp_vmm_write_old_valid_pte_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t spt_idx, uint32_t count,
    uint64_t pte, enum nvgsp_vmm_clear_purpose purpose)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint32_t cleared;

	if (count == 0)
		return;
	if (pte == 0) {
		nvgsp_vmm_clear_lpt_for_spt_range_purpose(vmm, pt, spt_idx,
		    count, purpose);
		cleared = nvgsp_vmm_clear_live_spt_range(vmm, pt, spt_idx,
		    count);
		nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
		    NVGSP_GMMU_SPT_SHIFT, cleared);
		if (count > cleared)
			nvgsp_vmm_note_skip_clear(vmm, NVGSP_GMMU_SPT_SHIFT,
			    count - cleared);
		return;
	}

	nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, count);
	nvgsp_vmm_mark_spt_range_from_pte(pt, spt_idx, count, pte);
	nvgsp_bar_set_bar1_region64(sc, pt->spt.bar1_gva + spt_idx * 8,
	    pte, count);
	vmm->stats.pte_fast_clear_count += count;
	if (pte == nvgsp_pte_to_sparse())
		vmm->stats.pte_fast_sparse_clear_count += count;
	nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_SPT_SHIFT, count);
	vmm->stats.pte_leaf_clear_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)] += count;
}

/*
 * nvgsp_vmm_clear_spt_for_lpt_range()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  The helper only updates the
 *   small-page PTE storage covered by the requested big-page range.
 *
 * Lifetime:
 *   The caller must ensure any live VM_BIND records covering the same range
 *   either match the new big-page mapping or have already been materialized
 *   into 4 KiB mappings before this clear is published.
 *
 * Threading:
 *   Callers hold vmm->tok and own the final flush/TLB invalidate boundary.
 */
static void
nvgsp_vmm_clear_spt_for_lpt_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t lpt_idx, uint32_t count)
{
	uint32_t cleared, spt_count, spt_idx;

	if (count == 0)
		return;
	spt_idx = lpt_idx * NVGSP_GMMU_LPT_SPTE_COUNT;
	spt_count = count * NVGSP_GMMU_LPT_SPTE_COUNT;
	cleared = nvgsp_vmm_clear_live_spt_range(vmm, pt, spt_idx,
	    spt_count);
	nvgsp_vmm_note_conflict_clear(vmm, NVGSP_GMMU_SPT_SHIFT, cleared);
	if (spt_count > cleared)
		nvgsp_vmm_note_skip_clear(vmm, NVGSP_GMMU_SPT_SHIFT,
		    spt_count - cleared);
}

/*
 * nvgsp_vmm_clear_spt_for_lpt_invalid_range()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  It clears only live SPT leaves
 *   covered by a final invalid LPT target; it does not consume mapping or
 *   sparse-region ownership.
 *
 * Lifetime:
 *   The PT remains linked for the caller's paired LPT invalid write.  The
 *   target range becomes semantically empty only after both SPT and LPT writes
 *   are visible and the caller performs the final invalidate.
 *
 * Threading:
 *   Callers hold vmm->tok.  The explicit purpose marks whether this clear is
 *   final invalid state or conflict resolution for a following target leaf.
 */
static void
nvgsp_vmm_clear_spt_for_lpt_invalid_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t lpt_idx, uint32_t count,
    enum nvgsp_vmm_clear_purpose purpose)
{
	uint32_t cleared, spt_count, spt_idx;

	if (count == 0)
		return;
	spt_idx = lpt_idx * NVGSP_GMMU_LPT_SPTE_COUNT;
	spt_count = count * NVGSP_GMMU_LPT_SPTE_COUNT;
	cleared = nvgsp_vmm_clear_live_spt_range(vmm, pt, spt_idx,
	    spt_count);
	nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
	    NVGSP_GMMU_SPT_SHIFT, cleared);
	if (spt_count > cleared)
		nvgsp_vmm_note_skip_clear(vmm, NVGSP_GMMU_SPT_SHIFT,
		    spt_count - cleared);
}

/*
 * nvgsp_vmm_write_new_valid_lpt_pte_linear()
 *
 * Ownership:
 *   Borrows vmm and an already-owned user PT.  It writes the big-page leaf PT
 *   only; caller-owned BO/binding state is not retained.
 *
 * Lifetime:
 *   The PT must remain linked until the write completes.  The mapping becomes
 *   visible to the GPU only after the caller's final VMM flush/TLB invalidate.
 *
 * Threading:
 *   Callers hold vmm->tok.  This is synchronous BAR1 MMIO and does not wait on
 *   GPU execution fences.
 */
static void
nvgsp_vmm_write_new_valid_lpt_pte_linear(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint32_t lpt_idx, uint64_t first_pte,
    uint64_t pte_step, uint32_t count)
{
	struct nvgsp_state *sc = vmm->gsp;

	if (count == 0)
		return;

	nvgsp_vmm_clear_spt_for_lpt_range(vmm, pt, lpt_idx, count);
	nvgsp_vmm_lpt_mark_range_state(pt, lpt_idx, count, 1, 0);
	nvgsp_bar_write_bar1_linear_region64(sc,
	    pt->lpt.bar1_gva + (uint64_t)lpt_idx * 8, first_pte, pte_step,
	    count);
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] += count;
}

static struct nvgsp_vmm_pd0 *
nvgsp_vmm_pd0_find_va(struct nvgsp_vmm *vmm, uint64_t va,
    uint32_t *pd0_idxp)
{
	uint32_t pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	uint32_t pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	uint32_t pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	if (pd0_idxp != NULL)
		*pd0_idxp = pd0_idx;
	return (nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx));
}

static int
nvgsp_vmm_pt_has_sparse_region(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_vmm_sparse_region *region;
	uint64_t pt_start, pt_end;

	pt_start = ((uint64_t)pt->pd2_idx << NVGSP_GMMU_PD2_SHIFT) |
	    ((uint64_t)pt->pd1_idx << NVGSP_GMMU_PD1_SHIFT) |
	    ((uint64_t)pt->pd0_idx << NVGSP_GMMU_PD0_SHIFT);
	pt_end = pt_start + (1ULL << NVGSP_GMMU_PD0_SHIFT);

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		if (nvgsp_vmm_sparse_region_overlaps_range(region,
		    pt_start, pt_end - pt_start))
			return (1);
	}
	return (0);
}

/*
 * nvgsp_vmm_release_pd0_if_empty()
 *
 * Ownership:
 *   Borrows pd0 from vmm.  If every PD0 slot is empty, the helper consumes the
 *   pd0 object and releases its BAR1-backed page-table page.
 *
 * Lifetime:
 *   pd0 is invalid after this helper frees it.  Callers must not dereference
 *   pd0 after calling when nvgsp_vmm_pd0_has_live_slot() was false.
 *
 * Threading:
 *   Callers hold vmm->tok.  Parent PD1 writes are synchronous BAR1 MMIO; the
 *   enclosing commit owns the final flush/TLB invalidate.
 */
static void
nvgsp_vmm_release_pd0_if_empty(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_pd0 *pd0)
{
	struct nvgsp_state *sc = vmm->gsp;

	if (nvgsp_vmm_pd0_has_live_slot(pd0))
		return;

	nvgsp_bar_wr64_bar1(sc, pd0->pd1_page->bar1_gva + pd0->pd1_idx * 8,
	    0);
	LIST_REMOVE(pd0, lookup_link);
	LIST_REMOVE(pd0, link);
	nvgsp_bar_free_bar1_page(sc, &pd0->page);
	_kfree(pd0, M_NVGSP_VMM);
	vmm->stats.pd0_empty_free_count++;
}

/*
 * nvgsp_vmm_free_pt()
 *
 * Ownership:
 *   Consumes pt from vmm's live PT lists and releases its BAR1-backed LPT/SPT
 *   pages.  The caller must have already decided that the parent PDE may be
 *   cleared or replaced with empty_pde.
 *
 * Lifetime:
 *   pt is invalid after return.  Its parent pd0 may also be consumed if this
 *   was the last child PT and empty_pde is invalid.
 *
 * Threading:
 *   Callers hold vmm->tok.  This helper performs synchronous BAR1 writes but
 *   does not flush or invalidate; the enclosing VM_BIND commit owns that
 *   boundary.
 */
static void
nvgsp_vmm_free_pt(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint64_t empty_pde)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pd0 *pd0 = pt->pd0;

	if (empty_pde != 0)
		nvgsp_vmm_pd0_write_sparse_2m_slot(sc, pd0, pt->pd0_idx,
		    empty_pde);
	else
		nvgsp_vmm_pd0_write_empty_slot(sc, pd0, pt->pd0_idx);
	LIST_REMOVE(pt, lookup_link);
	LIST_REMOVE(pt, link);
	nvgsp_bar_free_bar1_page(sc, &pt->spt);
	nvgsp_bar_free_bar1_page(sc, &pt->lpt);
	_kfree(pt, M_NVGSP_VMM);
	vmm->stats.pt_empty_free_count++;

	nvgsp_vmm_release_pd0_if_empty(vmm, pd0);
}

/*
 * nvgsp_vmm_free_pt_preserve_pd0()
 *
 * Ownership:
 *   Consumes a child PT from vmm's live PT lists, but deliberately keeps the
 *   parent PD0 page owned by the VMM for a following prepared PD0 writer.
 *
 * Lifetime:
 *   pt is invalid after return.  The parent PD0 slot is empty and the PD0 page
 *   remains linked until the enclosing remap commit installs the next PD0
 *   state or a later reclaim sees it empty.
 *
 * Threading:
 *   Callers hold vmm->tok in a no-fail VM_BIND commit section.  This helper
 *   performs deterministic BAR1 writes and page-table frees only.
 */
static void
nvgsp_vmm_free_pt_preserve_pd0(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pd0 *pd0 = pt->pd0;

	nvgsp_vmm_pd0_write_empty_slot(sc, pd0, pt->pd0_idx);
	LIST_REMOVE(pt, lookup_link);
	LIST_REMOVE(pt, link);
	nvgsp_bar_free_bar1_page(sc, &pt->spt);
	nvgsp_bar_free_bar1_page(sc, &pt->lpt);
	_kfree(pt, M_NVGSP_VMM);
	vmm->stats.pt_empty_free_count++;
}

/*
 * nvgsp_vmm_free_pt_to_2m()
 *
 * Ownership:
 *   Consumes a child PT and replaces its parent PD0 slot with one 2 MiB valid
 *   leaf.  The caller owns the BO/mapping state that proves the replacement is
 *   semantically identical to the 64 KiB leaves being removed.
 *
 * Lifetime:
 *   pt is invalid after return.  The parent pd0 remains live because the same
 *   slot is now a 2 MiB leaf.
 *
 * Threading:
 *   Callers hold vmm->tok.  This helper performs deterministic BAR1 writes
 *   and page-table page frees; the caller owns the final invalidate.
 */
static void
nvgsp_vmm_free_pt_to_2m(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint64_t pte)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pd0 *pd0 = pt->pd0;

	nvgsp_vmm_pd0_write_valid_2m_slot(sc, pd0, pt->pd0_idx, pte);
	LIST_REMOVE(pt, lookup_link);
	LIST_REMOVE(pt, link);
	nvgsp_bar_free_bar1_page(sc, &pt->spt);
	nvgsp_bar_free_bar1_page(sc, &pt->lpt);
	_kfree(pt, M_NVGSP_VMM);
	vmm->stats.pt_empty_free_count++;
}

static void
nvgsp_vmm_reclaim_empty_pt(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt, uint64_t empty_pde)
{
	if (pt->valid_pte_count != 0 || pt->valid_lpte_count != 0)
		return;
	if (pt->sparse_pte_count != 0 || pt->sparse_lpte_count != 0)
		return;
	if (nvgsp_vmm_pt_has_sparse_region(vmm, pt))
		return;

	nvgsp_vmm_free_pt(vmm, pt, empty_pde);
}

static uint64_t
nvgsp_vmm_pt_live_pages(const struct nvgsp_vmm_user_pt *pt)
{
	return ((uint64_t)pt->valid_pte_count + pt->sparse_pte_count +
	    ((uint64_t)pt->valid_lpte_count + pt->sparse_lpte_count) *
	    NVGSP_GMMU_LPT_SPTE_COUNT);
}

static void
nvgsp_vmm_note_pt_skip_clear(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_user_pt *pt,
    enum nvgsp_vmm_clear_purpose purpose)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint64_t live_pages;

	live_pages = nvgsp_vmm_pt_live_pages(pt);
	vmm->stats.pt_skip_clear_count++;
	vmm->stats.pt_skip_clear_pages += NVGSP_GMMU_SPT_ENTRIES;
	if (purpose == NVGSP_VMM_CLEAR_CONFLICT) {
		vmm->stats.pt_conflict_clear_count++;
		vmm->stats.pt_conflict_clear_pages += live_pages;
	} else {
		vmm->stats.pt_final_clear_count++;
		vmm->stats.pt_final_clear_pages += live_pages;
	}
}

/*
 * nvgsp_vmm_skip_clear_full_pt()
 *
 * Ownership:
 *   Consumes pt only when the caller is unmapping the full 2 MiB PT window.
 *   No leaf PTE ownership remains after the parent PDE is cleared.
 *
 * Lifetime:
 *   pt is invalid on success.  On failure no state changes are made and the
 *   caller must fall back to ordinary leaf clear.
 *
 * Threading:
 *   Callers hold vmm->tok and have already proven the VM_BIND target covers
 *   the whole PT window.  This helper does not flush; the caller owns the
 *   final invalidate.
 */
static bool
nvgsp_vmm_skip_clear_full_pt(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt,
    enum nvgsp_vmm_clear_purpose purpose)
{
	if (pt->sparse_pte_count != 0 || pt->sparse_lpte_count != 0)
		return (false);
	if (nvgsp_vmm_pt_has_sparse_region(vmm, pt))
		return (false);

	nvgsp_vmm_note_pt_skip_clear(vmm, pt, purpose);
	nvgsp_vmm_free_pt(vmm, pt, 0);
	return (true);
}

static void
nvgsp_vmm_unmap_existing_pte(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t pte, uint64_t empty_pde)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pd0 *pd0;
	struct nvgsp_vmm_user_pt *pt;
	uint32_t pd0_idx;

	pt = nvgsp_vmm_user_pt_find_va(vmm, va);
	if (pt == NULL) {
		if (empty_pde == 0) {
			pd0 = nvgsp_vmm_pd0_find_va(vmm, va, &pd0_idx);
			if (pd0 != NULL &&
			    (pd0->slot_state[pd0_idx] ==
			    NVGSP_VMM_PD0_SLOT_EMPTY ||
			    (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1)) == 0)) {
				nvgsp_vmm_pd0_write_empty_slot(sc, pd0,
				    pd0_idx);
				nvgsp_vmm_release_pd0_if_empty(vmm, pd0);
			}
		}
		return;
	}
	(void)nvgsp_vmm_write_pt_pte(vmm, pt, va, pte);
	nvgsp_vmm_reclaim_empty_pt(vmm, pt, empty_pde);
}

/*
 * nvgsp_vmm_write_sparse_prepared()
 *
 * Ownership:
 *   Borrows vmm and consumes no sparse-region ownership.  The caller owns the
 *   region record and is responsible for linking or unlinking it around this
 *   writer.
 *
 * Lifetime:
 *   All PTs covering [va, va + size) must have been created by prepare before
 *   commit.  This helper only borrows those PTs by direct lookup and never
 *   calls the legacy lazy allocation path.
 *
 * Threading:
 *   Callers hold vmm->tok and are already in the VM_BIND commit section.  BAR1
 *   writes complete synchronously here; the caller owns the final VMM flush/TLB
 *   invalidate boundary.
 */
static int
nvgsp_vmm_write_sparse_spt_prepared(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *pt;
	uint64_t cur, chunk, off, sparse_pte;
	uint32_t spt_idx, count;
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	sparse_pte = nvgsp_pte_to_sparse();
	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va, size,
	    NVGSP_GMMU_SPT_SHIFT);
	if (err != 0)
		return (err);
	for (off = 0; off < size; off += chunk) {
		cur = va + off;
		chunk = nvgsp_vmm_user_pt_chunk_size(cur, size - off);
		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL)
			return (ENOENT);

		spt_idx = nvgsp_vmm_spt_idx(cur);
		count = (uint32_t)(chunk / NVGSP_GMMU_PT_PAGE_SIZE);
		nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx, count);
		nvgsp_vmm_mark_spt_range_from_pte(pt, spt_idx, count,
		    sparse_pte);
		nvgsp_bar_set_bar1_region64(sc,
		    pt->spt.bar1_gva + (uint64_t)spt_idx * 8, sparse_pte,
		    count);
		vmm->stats.pte_fast_clear_count += count;
		vmm->stats.pte_fast_sparse_clear_count += count;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_SPT_SHIFT,
		    count);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_SPT_SHIFT)] +=
		    count;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_sparse_lpt_prepared()
 *
 * Ownership:
 *   Borrows vmm and consumes no sparse-region ownership.  It only rewrites the
 *   prepared child LPT/SPT storage that the caller already owns through the
 *   enclosing sparse-region commit.
 *
 * Lifetime:
 *   All PTs covering [va, va + size) must remain linked until the paired LPT
 *   sparse writes and SPT clears are published by the caller's final flush.
 *
 * Threading:
 *   Callers hold vmm->tok.  This is the large-page sparse backend path; the
 *   current DRM VM_BIND planner still calls the 4 KiB sparse path until the
 *   large-sparse enable gate is deliberately opened.
 */
static int
nvgsp_vmm_write_sparse_lpt_prepared(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *pt;
	uint64_t cur, off, sparse_pte;
	uint32_t lpt_idx, count;
	int err;

	if ((va | size) & (NVGSP_GMMU_LPT_PAGE_SIZE - 1))
		return (EINVAL);

	sparse_pte = nvgsp_pte_to_sparse();
	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va, size,
	    NVGSP_GMMU_LPT_SHIFT);
	if (err != 0)
		return (err);
	for (off = 0; off < size;) {
		cur = va + off;
		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL)
			return (ENOENT);

		lpt_idx = nvgsp_vmm_lpt_idx(cur);
		count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
		nvgsp_vmm_clear_spt_for_lpt_range(vmm, pt, lpt_idx,
		    count);
		nvgsp_vmm_lpt_mark_range_state(pt, lpt_idx, count, 0,
		    1);
		nvgsp_bar_set_bar1_region64(sc,
		    pt->lpt.bar1_gva + (uint64_t)lpt_idx * 8, sparse_pte,
		    count);
		vmm->stats.pte_fast_clear_count += count;
		vmm->stats.pte_fast_sparse_clear_count += count;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_LPT_SHIFT,
		    count);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] +=
		    count;
		off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_sparse_pd0_prepared()
 *
 * Ownership:
 *   Borrows vmm and consumes no sparse-region ownership.  It writes only PD0
 *   slots whose parent PD0 pages were prepared earlier by
 *   ensure_pd0_range().
 *
 * Lifetime:
 *   The target slots must not contain child PT ownership from the prepared
 *   range check through the PD0 sparse writes.  Existing same-size valid or
 *   sparse PD0 leaves are overwritten directly in the same remap commit.
 *
 * Threading:
 *   Callers hold vmm->tok and are in the no-fail commit section.  This helper
 *   does not allocate PD0 pages, create child LPT/SPT tables, or call any
 *   legacy lazy page-table helper.
 */
static int
nvgsp_vmm_write_sparse_pd0_prepared(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint64_t off, sparse_pde;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	uint64_t count = 0;
	int err;

	if ((va | size) & (page_size - 1))
		return (EINVAL);

	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va, size,
	    NVGSP_GMMU_PD0_SHIFT);
	if (err != 0)
		return (err);

	sparse_pde = nvgsp_pde_to_sparse();
	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_pd0 *pd0;
		uint32_t pd0_idx;

		pd0 = nvgsp_vmm_pd0_find_va(vmm, va + off, &pd0_idx);
		if (pd0 == NULL)
			return (ENOENT);
		if (pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_CHILD)
			return (EBUSY);
		nvgsp_vmm_pd0_write_sparse_2m_slot(sc, pd0, pd0_idx,
		    sparse_pde);
		count++;
	}
	vmm->stats.pte_fast_clear_count += count;
	vmm->stats.pte_fast_sparse_clear_count += count;
	nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_PD0_SHIFT, count);
	vmm->stats.pte_leaf_clear_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] += count;
	return (0);
}

static int
nvgsp_vmm_write_sparse_prepared(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, uint8_t page_shift)
{
	if (page_shift == NVGSP_GMMU_SPT_SHIFT)
		return (nvgsp_vmm_write_sparse_spt_prepared(vmm, va,
		    size));
	if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		return (nvgsp_vmm_write_sparse_lpt_prepared(vmm, va,
		    size));
	if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		return (nvgsp_vmm_write_sparse_pd0_prepared(vmm, va,
		    size));
	return (EINVAL);
}

void
nvgsp_vmm_read_pte(struct nvgsp_vmm *vmm, uint64_t va,
    struct nvgsp_vmm_pte_info *info)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *pt;
	uint32_t pd2_idx, pd1_idx, pd0_idx, lpt_idx, spt_idx;

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));
	lpt_idx = nvgsp_vmm_lpt_idx(va);
	spt_idx = nvgsp_vmm_spt_idx(va);

	bzero(info, sizeof(*info));
	info->va = va;
	info->pd2_idx = pd2_idx;
	info->pd1_idx = pd1_idx;
	info->pd0_idx = pd0_idx;
	info->lpt_idx = lpt_idx;
	info->spt_idx = spt_idx;

	lwkt_gettoken(&vmm->tok);
	pt = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx, pd0_idx);
	if (pt == NULL) {
		lwkt_reltoken(&vmm->tok);
		return;
	}

	info->has_pt = 1;
	info->lpte = (uint64_t)nvgsp_bar_rd32_bar1(sc,
	    pt->lpt.bar1_gva + lpt_idx * 8);
	info->lpte |= (uint64_t)nvgsp_bar_rd32_bar1(sc,
	    pt->lpt.bar1_gva + lpt_idx * 8 + 4) << 32;
	info->pte = (uint64_t)nvgsp_bar_rd32_bar1(sc,
	    pt->spt.bar1_gva + spt_idx * 8);
	info->pte |= (uint64_t)nvgsp_bar_rd32_bar1(sc,
	    pt->spt.bar1_gva + spt_idx * 8 + 4) << 32;
	lwkt_reltoken(&vmm->tok);
}

void
nvgsp_vmm_debug_dump_pte(struct nvgsp_vmm *vmm, uint64_t va)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_pte_info info;

	nvgsp_vmm_read_pte(vmm, va, &info);
	if (!info.has_pt) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gsp_vmm: pte va=0x%016jx pd2=%u pd1=%u pd0=%u lpt=%u spt=%u missing-pt sparse=0x%016jx\n",
		    (uintmax_t)info.va, info.pd2_idx, info.pd1_idx,
		    info.pd0_idx, info.lpt_idx, info.spt_idx,
		    (uintmax_t)vmm->sparse_page.paddr);
		return;
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp_vmm: pte va=0x%016jx pd2=%u pd1=%u pd0=%u lpt=%u lpte=0x%016jx spt=%u pte=0x%016jx sparse=0x%016jx sparse_pte=0x%016jx\n",
	    (uintmax_t)info.va, info.pd2_idx, info.pd1_idx, info.pd0_idx,
	    info.lpt_idx, (uintmax_t)info.lpte, info.spt_idx,
	    (uintmax_t)info.pte,
	    (uintmax_t)vmm->sparse_page.paddr,
	    (uintmax_t)nvgsp_pte_to_sparse());
}

static uint64_t
nvgsp_vmm_dirty_set_page_count(const struct nvgsp_vmm_dirty_set *dirty)
{
	uint64_t pages = 0;

	if (dirty == NULL)
		return (0);
	if (dirty->page_count != 0)
		return (dirty->page_count);
	for (uint32_t i = 0; i < dirty->range_count; i++) {
		const struct nvgsp_vmm_dirty_range *range =
		    &dirty->ranges[i];

		if (range->end <= range->start)
			continue;
		pages += (range->end - range->start) / NVGSP_GMMU_PT_PAGE_SIZE;
	}
	return (pages);
}

/*
 * nvgsp_vmm_flush_common()
 *
 * Ownership:
 *   Borrows vmm and an optional dirty set.  The backend consumes only scalar
 *   range facts and never retains dirty-set storage or remap ownership.
 *
 * Lifetime:
 *   PTE/PDE writes issued before this call become visible to GMMU after the
 *   BAR1 flush and hardware invalidate complete.  Dirty range storage only has
 *   to live through this call.
 *
 * Threading:
 *   Takes vmm->tok while flushing backend writes and issuing the TU102
 *   invalidate.  The caller owns higher-level VM_BIND/job ordering and must not
 *   publish fences or release retired BO refs before this call returns.
 */
static void
nvgsp_vmm_flush_common(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_dirty_set *dirty)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint64_t profile_start = nvgsp_vmm_profile_now_us(sc);
	uint32_t dirty_ranges = dirty != NULL ? dirty->range_count : 0;

	lwkt_gettoken(&vmm->tok);
	nvgsp_bar_flush_bar1(sc);
	vmm->stats.pte_backend_flush_count++;
	if (dirty_ranges != 0) {
		vmm->stats.dirty_flush_count++;
		vmm->stats.dirty_flush_range_count += dirty_ranges;
		vmm->stats.dirty_flush_pages +=
		    nvgsp_vmm_dirty_set_page_count(dirty);
		if (dirty->overflow)
			vmm->stats.dirty_flush_overflow_count++;
		/*
		 * TU102/GSP exposes the same PAGE_ALL invalidate shape as
		 * Linux nouveau tu102_vmm_flush().  The dirty set is still the
		 * backend boundary; this counter marks the current whole-PDB
		 * fallback until a narrower hardware/RM command is proven.
		 */
		vmm->stats.dirty_flush_all_fallback_count++;
	}
	nvgsp_vmm_invalidate(vmm);
	vmm->stats.flush_count++;
	nvgsp_vmm_profile_add_us(sc, &vmm->stats.flush_us, profile_start);
	lwkt_reltoken(&vmm->tok);
}

void
nvgsp_vmm_flush(struct nvgsp_vmm *vmm)
{
	nvgsp_vmm_flush_common(vmm, NULL);
}

/*
 * nvgsp_vmm_flush_dirty()
 *
 * Ownership:
 *   Borrows a VMM and a caller-owned dirty set.  It does not retain dirty
 *   ranges after the call and does not own any VM_BIND plan objects.
 *
 * Lifetime:
 *   Completes the visibility boundary for all PTE/PDE writes described by the
 *   dirty set before returning.  Callers may signal fences or release retired
 *   BO refs only after this function returns.
 *
 * Threading:
 *   May be called while the caller holds the outer remap serialization.  The
 *   helper takes vmm->tok internally for backend write visibility and hardware
 *   invalidate, matching nvgsp_vmm_flush().
 */
void
nvgsp_vmm_flush_dirty(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_dirty_set *dirty)
{
	nvgsp_vmm_flush_common(vmm, dirty);
}

int
nvgsp_vmm_map_sysmem_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    vm_paddr_t paddr, uint64_t size)
{
	uint64_t off;
	int err;

	if ((va | paddr | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += NVGSP_GMMU_PT_PAGE_SIZE) {
		err = nvgsp_vmm_write_pte(vmm, va + off,
		    nvgsp_pte_to_sysmem((uint64_t)paddr + off));
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

int
nvgsp_vmm_map_sysmem(struct nvgsp_vmm *vmm, uint64_t va,
    vm_paddr_t paddr, uint64_t size)
{
	int err = nvgsp_vmm_map_sysmem_noflush(vmm, va, paddr, size);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}

int
nvgsp_vmm_map_sysmem_kva_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    void *kva, uint64_t size, uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t off;
	int err;

	if (((uintptr_t)kva | va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += NVGSP_GMMU_PT_PAGE_SIZE) {
		vm_paddr_t paddr = vtophys((uint8_t *)kva + off);

		err = nvgsp_vmm_write_pte(vmm, va + off,
		    nvgsp_pte_to_sysmem((uint64_t)paddr) | kind_bits);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_map_sysmem_bo_worker()
 *
 * Ownership:
 *   Borrows vmm and bo.  It does not take GEM/BO references and does not own
 *   the VM binding; callers must keep the BO populated and pinned.
 *
 * Lifetime:
 *   In prepared_only mode the target PTs must already be linked in vmm by
 *   nvgsp_vmm_ensure_pt_range().  The written PTEs become visible only
 *   after the caller performs the enclosing VMM flush/TLB invalidate.
 *
 * Threading:
 *   Acquires vmm->tok while writing PTEs.  prepared_only never allocates page
 *   tables; the legacy mode may allocate through user_pt_get() for internal
 *   non-VM_BIND callers.
 */
static int
nvgsp_vmm_map_sysmem_bo_worker(struct nvgsp_vmm *vmm, uint64_t va,
    const struct nvgpu_bo *bo, uint64_t bo_offset, uint64_t size, uint8_t kind,
    int prepared_only)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t off;
	int err;

	if ((va | bo_offset | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint32_t spt_idx, count, i;

		if (prepared_only) {
			pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
			if (pt == NULL) {
				lwkt_reltoken(&vmm->tok);
				return (ENOENT);
			}
		} else {
			err = nvgsp_vmm_user_pt_get(vmm, cur, &pt);
			if (err != 0) {
				lwkt_reltoken(&vmm->tok);
				return (err);
			}
		}
		spt_idx = (uint32_t)((cur >> NVGSP_GMMU_SPT_SHIFT) &
		    (NVGSP_GMMU_SPT_ENTRIES - 1));
		count = nvgsp_vmm_spt_chunk_count(cur, size - off);
		nvgsp_vmm_clear_lpt_for_spt_range(vmm, pt, spt_idx,
		    count);
		for (i = 0; i < count; i++) {
			uint64_t page_off = off + (uint64_t)i * NVGSP_GMMU_PT_PAGE_SIZE;
			vm_paddr_t paddr;

			err = nvgpu_bo_get_paddr_at(bo, bo_offset + page_off, &paddr);
			if (err != 0) {
				lwkt_reltoken(&vmm->tok);
				return (err);
			}
				nvgsp_vmm_write_new_valid_pt_pte_raw(vmm, pt,
				    spt_idx + i, nvgsp_pte_to_sysmem((uint64_t)paddr) |
				    kind_bits);
			}
			nvgsp_vmm_note_bulk_write(vmm,
			    NVGSP_GMMU_SPT_SHIFT, count);
			off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
		}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

int
nvgsp_vmm_map_sysmem_bo_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    const struct nvgpu_bo *bo, uint64_t bo_offset, uint64_t size, uint8_t kind)
{
	return (nvgsp_vmm_map_sysmem_bo_worker(vmm, va, bo, bo_offset,
	    size, kind, 0));
}

int
nvgsp_vmm_map_sysmem_bo_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, const struct nvgpu_bo *bo, uint64_t bo_offset, uint64_t size,
    uint8_t kind)
{
	return (nvgsp_vmm_map_sysmem_bo_worker(vmm, va, bo, bo_offset,
	    size, kind, 1));
}

static int
nvgsp_vmm_check_sysmem_paddr_snapshot(const vm_paddr_t *paddrs,
    uint32_t page_count, uint8_t page_shift)
{
	uint64_t page_size;
	uint64_t first;

	if (paddrs == NULL || page_count == 0)
		return (EINVAL);
	if (page_shift != NVGSP_GMMU_SPT_SHIFT &&
	    page_shift != NVGSP_GMMU_LPT_SHIFT &&
	    page_shift != NVGSP_GMMU_PD0_SHIFT)
		return (EINVAL);

	page_size = 1ULL << page_shift;
	first = (uint64_t)paddrs[0];
	if ((first & (page_size - 1)) != 0)
		return (EINVAL);
	if (page_count > 1 &&
	    first > UINT64_MAX -
	    (uint64_t)(page_count - 1) * NVGSP_GMMU_PT_PAGE_SIZE)
		return (EINVAL);
	for (uint32_t i = 0; i < page_count; i++) {
		uint64_t expected = first +
		    (uint64_t)i * NVGSP_GMMU_PT_PAGE_SIZE;

		if ((uint64_t)paddrs[i] != expected)
			return (EINVAL);
	}
	return (0);
}

/*
 * nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush()
 *
 * Ownership:
 *   Borrows vmm and a caller-owned physical-page snapshot.  It does not retain
 *   the array and does not own the backing BO; VM_BIND prepare keeps that
 *   backing pinned for the enclosing operation.
 *
 * Lifetime:
 *   paddrs must contain one 4 KiB page address for every page in the target VA
 *   range and remain valid until this function returns.  The written PTEs are
 *   published only after the caller performs the enclosing VMM flush/invalidate.
 *
 * Threading:
 *   Acquires vmm->tok while updating leaf-state and BAR1 PTEs.  This is a
 *   prepared-only writer: it direct-lookups existing PTs and never allocates
 *   page tables or queries BO backing.
 */
int
nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush(
    struct nvgsp_vmm *vmm, uint64_t va, const vm_paddr_t *paddrs,
    uint32_t page_count, uint8_t kind, uint8_t page_shift)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t off;
	uint32_t page_index;
	int err;

	if (paddrs == NULL || page_count == 0 ||
	    (va & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return (EINVAL);
	if (page_shift == NVGSP_GMMU_LPT_SHIFT ||
	    page_shift == NVGSP_GMMU_PD0_SHIFT) {
		uint64_t page_size = 1ULL << page_shift;
		uint32_t pages_per_leaf =
		    (uint32_t)(page_size / NVGSP_GMMU_PT_PAGE_SIZE);

		if ((va & (page_size - 1)) != 0 ||
		    (page_count % pages_per_leaf) != 0)
			return (EINVAL);
		err = nvgsp_vmm_check_sysmem_paddr_snapshot(paddrs,
		    page_count, page_shift);
		if (err != 0)
			return (err);
	} else if (page_shift != NVGSP_GMMU_SPT_SHIFT) {
		return (EINVAL);
	}

	lwkt_gettoken(&vmm->tok);
	if (page_count > UINT64_MAX / NVGSP_GMMU_PT_PAGE_SIZE) {
		lwkt_reltoken(&vmm->tok);
		return (EINVAL);
	}
	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va,
	    (uint64_t)page_count * NVGSP_GMMU_PT_PAGE_SIZE,
	    page_shift);
	if (err != 0) {
		lwkt_reltoken(&vmm->tok);
		return (err);
	}
	if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
		uint32_t pages_per_leaf =
		    (uint32_t)(NVGSP_GMMU_PD0_PAGE_SIZE /
		    NVGSP_GMMU_PT_PAGE_SIZE);
		uint32_t pd0_count = 0;

		for (page_index = 0, off = 0; page_index < page_count;
		    page_index += pages_per_leaf,
		    off += NVGSP_GMMU_PD0_PAGE_SIZE) {
			struct nvgsp_vmm_pd0 *pd0;
			uint64_t cur = va + off;
			uint64_t pte = nvgsp_pte_to_sysmem(
			    (uint64_t)paddrs[page_index]) | kind_bits;
			uint32_t pd0_idx;

			pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
			if (pd0 == NULL) {
				lwkt_reltoken(&vmm->tok);
				return (EIO);
			}
			if (pd0->slot_state[pd0_idx] ==
			    NVGSP_VMM_PD0_SLOT_CHILD) {
				lwkt_reltoken(&vmm->tok);
				return (EBUSY);
			}
			nvgsp_vmm_pd0_write_valid_2m_slot(vmm->gsp, pd0,
			    pd0_idx, pte);
			pd0_count++;
		}
		vmm->stats.pte_fast_write_count += pd0_count;
		vmm->stats.pte_leaf_write_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] +=
		    pd0_count;
		nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_PD0_SHIFT,
		    pd0_count);
		lwkt_reltoken(&vmm->tok);
		return (0);
	}
	if (page_shift == NVGSP_GMMU_LPT_SHIFT) {
		uint64_t pte_step = NVGSP_GMMU_LPT_PAGE_SIZE >>
		    NVGSP_PT_ADDR_SHIFT;

		for (page_index = 0, off = 0; page_index < page_count;) {
			struct nvgsp_vmm_user_pt *pt;
			uint64_t cur = va + off;
			uint64_t first_pte;
			uint32_t lpt_idx, count;

			pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
			if (pt == NULL) {
				lwkt_reltoken(&vmm->tok);
				return (ENOENT);
			}

			lpt_idx = nvgsp_vmm_lpt_idx(cur);
			count = nvgsp_vmm_lpt_chunk_count(cur,
			    (uint64_t)(page_count - page_index) *
			    NVGSP_GMMU_PT_PAGE_SIZE);
			first_pte = nvgsp_pte_to_sysmem(
			    (uint64_t)paddrs[page_index]) | kind_bits;
			nvgsp_vmm_write_new_valid_lpt_pte_linear(vmm, pt,
			    lpt_idx, first_pte, pte_step, count);
			nvgsp_vmm_note_bulk_write(vmm,
			    NVGSP_GMMU_LPT_SHIFT, count);
			page_index += count * NVGSP_GMMU_LPT_SPTE_COUNT;
			off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
		}
		lwkt_reltoken(&vmm->tok);
		return (0);
	}

	for (page_index = 0, off = 0; page_index < page_count;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint32_t spt_idx, count;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (ENOENT);
		}

		spt_idx = (uint32_t)((cur >> NVGSP_GMMU_SPT_SHIFT) &
		    (NVGSP_GMMU_SPT_ENTRIES - 1));
		count = nvgsp_vmm_spt_chunk_count(cur,
		    (uint64_t)(page_count - page_index) *
		    NVGSP_GMMU_PT_PAGE_SIZE);
		nvgsp_vmm_write_new_valid_pt_pte_paddrs(vmm, pt,
		    spt_idx, paddrs + page_index, count, kind_bits);
		nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_SPT_SHIFT,
		    count);
		page_index += count;
		off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

int
nvgsp_vmm_map_sysmem_paddrs_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, const vm_paddr_t *paddrs, uint32_t page_count, uint8_t kind)
{
	return (nvgsp_vmm_map_sysmem_paddrs_page_prepared_noflush(vmm, va,
	    paddrs, page_count, kind, NVGSP_GMMU_SPT_SHIFT));
}

int
nvgsp_vmm_map_sysmem_kva(struct nvgsp_vmm *vmm, uint64_t va,
    void *kva, uint64_t size, uint8_t kind)
{
	int err = nvgsp_vmm_map_sysmem_kva_noflush(vmm, va, kva, size, kind);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}

static int
nvgsp_vmm_map_vram_flags_spt_worker(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind, int prepared_only)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t pte_step = NVGSP_GMMU_PT_PAGE_SIZE >> NVGSP_PT_ADDR_SHIFT;
	uint64_t off;
	int err;

	if ((va | paddr | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	if (prepared_only) {
		err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va,
		    size, NVGSP_GMMU_SPT_SHIFT);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	for (off = 0; off < size;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint64_t first_pte;
		uint32_t spt_idx, count;

		if (prepared_only) {
			pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
			if (pt == NULL) {
				lwkt_reltoken(&vmm->tok);
				return (ENOENT);
			}
		} else {
			err = nvgsp_vmm_user_pt_get(vmm, cur, &pt);
			if (err != 0) {
				lwkt_reltoken(&vmm->tok);
				return (err);
			}
		}
			spt_idx = (uint32_t)((cur >> NVGSP_GMMU_SPT_SHIFT) &
			    (NVGSP_GMMU_SPT_ENTRIES - 1));
			count = nvgsp_vmm_spt_chunk_count(cur, size - off);
			first_pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;
			nvgsp_vmm_write_new_valid_pt_pte_linear(vmm, pt, spt_idx,
			    first_pte, pte_step, count);
			nvgsp_vmm_note_bulk_write(vmm,
			    NVGSP_GMMU_SPT_SHIFT, count);
		off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

static int
nvgsp_vmm_map_vram_flags_lpt_worker(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind, int prepared_only)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t pte_step = NVGSP_GMMU_LPT_PAGE_SIZE >> NVGSP_PT_ADDR_SHIFT;
	uint64_t off;
	int err;

	if ((va | paddr | size) & (NVGSP_GMMU_LPT_PAGE_SIZE - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	if (prepared_only) {
		err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va,
		    size, NVGSP_GMMU_LPT_SHIFT);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	for (off = 0; off < size;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint64_t first_pte;
		uint32_t lpt_idx, count;

		if (prepared_only) {
			pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
			if (pt == NULL) {
				lwkt_reltoken(&vmm->tok);
				return (ENOENT);
			}
		} else {
			err = nvgsp_vmm_user_pt_get(vmm, cur, &pt);
			if (err != 0) {
				lwkt_reltoken(&vmm->tok);
				return (err);
			}
		}
			lpt_idx = nvgsp_vmm_lpt_idx(cur);
			count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
			first_pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;
			nvgsp_vmm_write_new_valid_lpt_pte_linear(vmm, pt, lpt_idx,
			    first_pte, pte_step, count);
			nvgsp_vmm_note_bulk_write(vmm,
			    NVGSP_GMMU_LPT_SHIFT, count);
		off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_map_vram_flags_*_worker()
 *
 * Ownership:
 *   Borrows vmm and caller-provided physical VRAM metadata.  It does not take
 *   BO/GEM references; VM_BIND callers keep the live binding pinned.
 *
 * Lifetime:
 *   prepared_only requires the LPT/SPT storage to exist before entry.  The
 *   caller owns the final VMM flush/TLB invalidate that publishes the PTEs.
 *
 * Threading:
 *   Writers hold vmm->tok while updating leaf-state and BAR1 PTEs.  The
 *   prepared path never allocates PTs; the legacy path may allocate for
 *   internal RM/GSP users that still rely on the older helper contract.
 */
static int
nvgsp_vmm_map_vram_flags_spt_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags_spt_worker(vmm, va, paddr,
	    size, priv, ro, kind, 0));
}

static int
nvgsp_vmm_map_vram_flags_lpt_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags_lpt_worker(vmm, va, paddr,
	    size, priv, ro, kind, 0));
}

static int
nvgsp_vmm_map_vram_flags_spt_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags_spt_worker(vmm, va, paddr,
	    size, priv, ro, kind, 1));
}

static int
nvgsp_vmm_map_vram_flags_lpt_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags_lpt_worker(vmm, va, paddr,
	    size, priv, ro, kind, 1));
}

/*
 * nvgsp_vmm_map_vram_flags_pd0_noflush()
 *
 * Ownership:
 *   Borrows vmm and caller-owned VRAM physical range metadata.  It does not
 *   acquire BO, GEM, or VM binding ownership.
 *
 * Lifetime:
 *   The target PD0 slots must not contain lower LPT/SPT child tables.  Existing
 *   same-size valid or sparse PD0 leaves are overwritten directly.  The caller
 *   owns any required remap materialization before calling and must publish the
 *   final VMM flush after this helper returns.
 *
 * Threading:
 *   Acquires vmm->tok internally.  The first pass allocates all needed PD0
 *   pages and verifies empty target slots; the second pass is deterministic
 *   BAR1 MMIO and cannot allocate.
 */
static int
nvgsp_vmm_map_vram_flags_pd0_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	uint64_t off;
	uint32_t count = 0;
	int err;

	if (size == 0)
		return (0);
	if ((va | paddr | size) & (page_size - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_pd0 *pd0;
		uint64_t cur = va + off;
		uint32_t pd2_idx, pd1_idx, pd0_idx;

		pd2_idx = (uint32_t)((cur >> NVGSP_GMMU_PD2_SHIFT) &
		    (NVGSP_GMMU_PD2_ENTRIES - 1));
		pd1_idx = (uint32_t)((cur >> NVGSP_GMMU_PD1_SHIFT) &
		    (NVGSP_GMMU_PD1_ENTRIES - 1));
		pd0_idx = (uint32_t)((cur >> NVGSP_GMMU_PD0_SHIFT) &
		    (NVGSP_GMMU_PD0_ENTRIES - 1));
		err = nvgsp_vmm_pd0_get(vmm, pd2_idx, pd1_idx, &pd0);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
		if (pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_CHILD) {
			lwkt_reltoken(&vmm->tok);
			return (EBUSY);
		}
	}

	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_pd0 *pd0;
		uint64_t cur = va + off;
		uint64_t pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;
		uint32_t pd0_idx;

		pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
		if (pd0 == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (EIO);
		}
		nvgsp_vmm_pd0_write_valid_2m_slot(vmm->gsp, pd0,
		    pd0_idx, pte);
		count++;
	}
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] += count;
	nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_PD0_SHIFT, count);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_map_vram_flags_pd0_prepared_noflush()
 *
 * Ownership:
 *   Borrows vmm and caller-owned VRAM physical range metadata.  It does not
 *   acquire BO, GEM, VM binding, or page-table ownership.
 *
 * Lifetime:
 *   The parent PD0 pages and non-child target slots must already have been
 *   prepared and kept stable by VM_BIND serialization.  Existing same-size PD0
 *   leaves are overwritten directly, and the new 2 MiB leaves become visible
 *   only after the caller performs the enclosing VMM flush/TLB invalidate.
 *
 * Threading:
 *   Acquires vmm->tok while validating prepared PD0 slots and writing BAR1
 *   PD0 leaf entries.  It never allocates PD0 pages, creates child LPT/SPT
 *   tables, queries user objects, or changes state outside the requested
 *   range.
 */
static int
nvgsp_vmm_map_vram_flags_pd0_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t off;
	uint32_t count = 0;
	int err;

	if (size == 0)
		return (0);
	if ((va | paddr | size) & (NVGSP_GMMU_PD0_PAGE_SIZE - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_check_prepared_pt_range_locked(vmm, va, size,
	    NVGSP_GMMU_PD0_SHIFT);
	if (err != 0) {
		lwkt_reltoken(&vmm->tok);
		return (err);
	}

	for (off = 0; off < size; off += NVGSP_GMMU_PD0_PAGE_SIZE) {
		struct nvgsp_vmm_pd0 *pd0;
		uint64_t cur = va + off;
		uint64_t pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;
		uint32_t pd0_idx;

		pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
		if (pd0 == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (EIO);
		}
		if (pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_CHILD) {
			lwkt_reltoken(&vmm->tok);
			return (EBUSY);
		}
		nvgsp_vmm_pd0_write_valid_2m_slot(vmm->gsp, pd0,
		    pd0_idx, pte);
		count++;
	}
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] += count;
	nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_PD0_SHIFT, count);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_promote_vram_64k_noflush()
 *
 * Ownership:
 *   Borrows vmm and caller-owned VRAM physical range metadata.  It does not
 *   acquire BO, GEM, or VM binding ownership, and it does not allocate or free
 *   page-table pages.
 *
 * Lifetime:
 *   The covered SPT pages must already exist and describe the same VA range
 *   in the caller's live mapping tracker.  This helper rewrites the hardware
 *   representation from 4 KiB SPT leaves to 64 KiB LPT leaves; the caller must
 *   update the live mapping page_shift before publishing the final VMM flush.
 *
 * Threading:
 *   Acquires vmm->tok internally.  The first pass verifies every covered PT is
 *   already present and that each target LPT slot is backed by complete valid
 *   SPT leaves with no sparse/LPT conflict, so the second pass is
 *   deterministic and cannot allocate.  The caller owns the surrounding
 *   VM_BIND serialization and final flush/TLB-invalidate boundary.
 */
int
nvgsp_vmm_promote_vram_64k_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t pte_step = NVGSP_GMMU_LPT_PAGE_SIZE >> NVGSP_PT_ADDR_SHIFT;
	uint64_t off;
	int err;

	if (size == 0)
		return (0);
	if ((va | paddr | size) & (NVGSP_GMMU_LPT_PAGE_SIZE - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint32_t count;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (ENOENT);
		}
		count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
		err = nvgsp_vmm_pt_check_promote_64k(pt,
		    nvgsp_vmm_lpt_idx(cur), count);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
		off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
	}

	for (off = 0; off < size;) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint64_t first_pte;
		uint32_t lpt_idx, count;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (EIO);
		}
		lpt_idx = nvgsp_vmm_lpt_idx(cur);
		count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
		first_pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;
		nvgsp_vmm_write_new_valid_lpt_pte_linear(vmm, pt,
		    lpt_idx, first_pte, pte_step, count);
		nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_LPT_SHIFT,
		    count);
		off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_promote_vram_2m_noflush()
 *
 * Ownership:
 *   Borrows vmm and caller-owned VRAM physical range metadata.  It does not
 *   acquire BO, GEM, or live mapping ownership.
 *
 * Lifetime:
 *   Each covered 2 MiB window must already be represented by a complete set
 *   of 64 KiB LPT leaves with identical mapping semantics in the caller's live
 *   mapping tree.  This helper only rewrites the GMMU representation; the
 *   caller must update live mapping page_shift before publishing the final
 *   VMM flush.
 *
 * Threading:
 *   Acquires vmm->tok internally.  The first pass verifies the whole range,
 *   and the second pass performs deterministic BAR1 writes and child-table
 *   frees.  No allocation or user object lookup occurs after verification.
 */
int
nvgsp_vmm_promote_vram_2m_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t flags = NVGSP_PTE_APERTURE_VRAM | NVGSP_PTE_VALID | kind_bits;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	uint64_t off;
	uint32_t count = 0;

	if (size == 0)
		return (0);
	if ((va | paddr | size) & (page_size - 1))
		return (EINVAL);
	if (priv)
		flags |= NVGSP_PTE_PRIV;
	if (ro)
		flags |= NVGSP_PTE_RO;

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_user_pt *pt;

		pt = nvgsp_vmm_user_pt_find_va(vmm, va + off);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (ENOENT);
		}
		if (pt->valid_lpte_count != NVGSP_GMMU_LPT_ENTRIES ||
		    pt->valid_lpt_mask != UINT32_MAX ||
		    pt->sparse_lpt_mask != 0 ||
		    pt->sparse_lpte_count != 0 ||
		    pt->valid_pte_count != 0 ||
		    pt->sparse_pte_count != 0 ||
		    nvgsp_vmm_pt_has_sparse_region(vmm, pt)) {
			lwkt_reltoken(&vmm->tok);
			return (EBUSY);
		}
	}

	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t pte = ((paddr + off) >> NVGSP_PT_ADDR_SHIFT) | flags;

		pt = nvgsp_vmm_user_pt_find_va(vmm, va + off);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (EIO);
		}
		nvgsp_vmm_free_pt_to_2m(vmm, pt, pte);
		count++;
	}
	vmm->stats.pte_leaf_clear_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] +=
	    (uint64_t)count * NVGSP_GMMU_LPT_ENTRIES;
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] += count;
	nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_LPT_SHIFT,
	    (uint64_t)count * NVGSP_GMMU_LPT_ENTRIES);
	nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_PD0_SHIFT, count);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_promote_sysmem_2m_noflush()
 *
 * Ownership:
 *   Borrows vmm and a caller-owned sysmem/GART paddr snapshot.  It does not
 *   retain the paddr array, acquire BO/GEM ownership, or allocate page tables.
 *
 * Lifetime:
 *   Each covered 2 MiB window must already be represented by a complete child
 *   PT whose 64 KiB LPT leaves match the caller's live mapping semantics.
 *   The caller must keep the paddr snapshot stable until this helper returns
 *   and update the live mapping tree before publishing the final flush.
 *
 * Threading:
 *   Acquires vmm->tok internally.  The first pass validates the entire range
 *   and paddr snapshot; the second pass performs deterministic BAR1 writes and
 *   child-table frees.  No allocation, BO lookup, or backing query occurs
 *   after validation starts.
 */
int
nvgsp_vmm_promote_sysmem_2m_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, const vm_paddr_t *paddrs, uint32_t page_count, uint8_t kind)
{
	uint64_t kind_bits = (uint64_t)kind << NVGSP_PTE_KIND_SHIFT;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	uint64_t size;
	uint32_t pages_per_leaf =
	    (uint32_t)(NVGSP_GMMU_PD0_PAGE_SIZE / NVGSP_GMMU_PT_PAGE_SIZE);
	uint32_t page_index;
	uint32_t count = 0;
	int err;

	if (paddrs == NULL || page_count == 0 ||
	    page_count > UINT64_MAX / NVGSP_GMMU_PT_PAGE_SIZE ||
	    (page_count % pages_per_leaf) != 0)
		return (EINVAL);
	size = (uint64_t)page_count * NVGSP_GMMU_PT_PAGE_SIZE;
	if ((va | size) & (page_size - 1))
		return (EINVAL);
	err = nvgsp_vmm_check_sysmem_paddr_snapshot(paddrs, page_count,
	    NVGSP_GMMU_PD0_SHIFT);
	if (err != 0)
		return (err);

	lwkt_gettoken(&vmm->tok);
	for (page_index = 0; page_index < page_count;
	    page_index += pages_per_leaf) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va +
		    (uint64_t)page_index * NVGSP_GMMU_PT_PAGE_SIZE;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (ENOENT);
		}
		if (pt->valid_lpte_count != NVGSP_GMMU_LPT_ENTRIES ||
		    pt->valid_lpt_mask != UINT32_MAX ||
		    pt->sparse_lpt_mask != 0 ||
		    pt->sparse_lpte_count != 0 ||
		    pt->valid_pte_count != 0 ||
		    pt->sparse_pte_count != 0 ||
		    nvgsp_vmm_pt_has_sparse_region(vmm, pt)) {
			lwkt_reltoken(&vmm->tok);
			return (EBUSY);
		}
	}

	for (page_index = 0; page_index < page_count;
	    page_index += pages_per_leaf) {
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va +
		    (uint64_t)page_index * NVGSP_GMMU_PT_PAGE_SIZE;
		uint64_t pte = nvgsp_pte_to_sysmem(
		    (uint64_t)paddrs[page_index]) | kind_bits;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			lwkt_reltoken(&vmm->tok);
			return (EIO);
		}
		nvgsp_vmm_free_pt_to_2m(vmm, pt, pte);
		count++;
	}
	vmm->stats.pte_leaf_clear_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] +=
	    (uint64_t)count * NVGSP_GMMU_LPT_ENTRIES;
	vmm->stats.pte_fast_write_count += count;
	vmm->stats.pte_leaf_write_count[
	    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)] += count;
	nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_LPT_SHIFT,
	    (uint64_t)count * NVGSP_GMMU_LPT_ENTRIES);
	nvgsp_vmm_note_bulk_write(vmm, NVGSP_GMMU_PD0_SHIFT, count);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_prepare_split_vram_2m()
 *
 * Ownership:
 *   Allocates an unlinked child LPT/SPT pair for one valid 2 MiB PD0 leaf and
 *   transfers that prepared page-table ownership to *ppt.  If the PD0 slot is
 *   already a child table, *ppt is NULL and the caller owns nothing.
 *
 * Lifetime:
 *   The prepared PT is private until commit_split_vram_2m_noflush() links it
 *   into vmm.  A failed or abandoned prepare must be released with
 *   abort_split_vram_2m().  The old 2 MiB PD0 leaf remains untouched.  The new
 *   child LPT/SPT pages stay empty: split only prepares child-table ownership,
 *   while keep ranges and final target ranges are written by their own remap
 *   writers in the commit section.
 *
 * Threading:
 *   May sleep and may allocate BAR1-backed page-table pages, so callers must
 *   run this in VM_BIND prepare before the no-fail commit section.  It borrows
 *   vmm->tok only long enough to validate current PD0 ownership.
 */
int
nvgsp_vmm_prepare_split_vram_2m(struct nvgsp_vmm *vmm, uint64_t va,
    struct nvgsp_vmm_user_pt **ppt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_user_pt *pt;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;
	int err;

	*ppt = NULL;
	if (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	lwkt_gettoken(&vmm->tok);
	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (existing != NULL) {
		lwkt_reltoken(&vmm->tok);
		return (0);
	}
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_VALID_2M) {
		lwkt_reltoken(&vmm->tok);
		return (ENOENT);
	}
	lwkt_reltoken(&vmm->tok);

	pt = kmalloc(sizeof(*pt), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	pt->pd0 = pd0;
	pt->pd2_idx = pd2_idx;
	pt->pd1_idx = pd1_idx;
	pt->pd0_idx = pd0_idx;

	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->lpt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;
	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->spt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;

	nvgsp_vmm_zero_bar1_page(sc, &pt->lpt);
	nvgsp_vmm_zero_bar1_page(sc, &pt->spt);
	*ppt = pt;
	return (0);

fail:
	nvgsp_vmm_abort_split_vram_2m(vmm, pt);
	return (err);
}

/*
 * nvgsp_vmm_check_split_vram_2m()
 *
 * Ownership:
 *   Borrows vmm and the prepared split PT.  It does not consume pt, link it
 *   into the VMM tree, write PD0, allocate page tables, or free ownership.
 *
 * Lifetime:
 *   The result is valid while VM remap serialization keeps the PD0 slot and
 *   any existing child table stable.  A NULL pt means prepare observed an
 *   already-linked child table, and this helper verifies the same condition
 *   still holds.
 *
 * Threading:
 *   Acquires vmm->tok for a read-only state check.  This is the preflight half
 *   of commit_split_vram_2m_noflush(); callers use it to validate every split
 *   in a larger materialize plan before the first PD0 slot is rewritten.
 */
int
nvgsp_vmm_check_split_vram_2m(struct nvgsp_vmm *vmm, uint64_t va,
    struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;
	int err = 0;

	if (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	lwkt_gettoken(&vmm->tok);
	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (existing != NULL) {
		err = (pt == NULL ? 0 : EEXIST);
		goto out;
	}
	if (pt == NULL || pt->pd2_idx != pd2_idx || pt->pd1_idx != pd1_idx ||
	    pt->pd0_idx != pd0_idx) {
		err = EINVAL;
		goto out;
	}
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL || pd0 != pt->pd0 ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_VALID_2M)
		err = EIO;
out:
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_commit_split_vram_2m_noflush()
 *
 * Ownership:
 *   Consumes pt on success and links it into vmm as the child LPT/SPT pair for
 *   va's PD0 slot.  If pt is NULL, the slot must already be a child table and
 *   no ownership is consumed.
 *
 * Lifetime:
 *   On success, lower-page writers may use the child table immediately.  The
 *   caller owns the final GMMU invalidate; this helper only flushes BAR1 so
 *   the freshly zeroed child tables and rewritten PD0 slot are visible before
 *   later lower-leaf writes.
 *
 * Threading:
 *   Runs in the VM_BIND no-fail commit section and only acquires vmm->tok.  It
 *   does not allocate, pin, lookup GEM handles, or sleep on user-owned state.
 */
int
nvgsp_vmm_commit_split_vram_2m_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;

	if (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	lwkt_gettoken(&vmm->tok);
	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (existing != NULL) {
		lwkt_reltoken(&vmm->tok);
		return (pt == NULL ? 0 : EEXIST);
	}
	if (pt == NULL || pt->pd2_idx != pd2_idx || pt->pd1_idx != pd1_idx ||
	    pt->pd0_idx != pd0_idx) {
		lwkt_reltoken(&vmm->tok);
		return (EINVAL);
	}
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL || pd0 != pt->pd0 ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_VALID_2M) {
		lwkt_reltoken(&vmm->tok);
		return (EIO);
	}

	nvgsp_vmm_pd0_write_child_slot(sc, pd0, pd0_idx,
	    nvgsp_pde_to_vram(pt->lpt.vram_paddr),
	    nvgsp_pde_to_vram(pt->spt.vram_paddr));
	nvgsp_bar_flush_bar1(sc);
	LIST_INSERT_HEAD(&vmm->user_pt_pages, pt, link);
	nvgsp_vmm_user_pt_lookup_insert(vmm, pt);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_abort_split_vram_2m()
 *
 * Ownership:
 *   Consumes an unlinked PT returned by prepare_split_vram_2m().  Passing NULL
 *   is allowed and consumes nothing.
 *
 * Lifetime:
 *   Only call this for PTs that were not successfully committed.  Linked PTs
 *   are owned by vmm and must be reclaimed by the normal page-table teardown.
 *
 * Threading:
 *   Does not acquire vmm->tok because the PT is not visible in vmm lists or
 *   lookup tables.  It only frees private BAR1-backed pages.
 */
void
nvgsp_vmm_abort_split_vram_2m(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_state *sc = vmm->gsp;

	if (pt == NULL)
		return;
	if (pt->spt.vram_paddr != 0)
		nvgsp_bar_free_bar1_page(sc, &pt->spt);
	if (pt->lpt.vram_paddr != 0)
		nvgsp_bar_free_bar1_page(sc, &pt->lpt);
	_kfree(pt, M_NVGSP_VMM);
}

int
nvgsp_vmm_map_vram_flags_page_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind, uint8_t page_shift)
{
	if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		return (nvgsp_vmm_map_vram_flags_pd0_noflush(vmm, va,
		    paddr, size, priv, ro, kind));
	if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		return (nvgsp_vmm_map_vram_flags_lpt_noflush(vmm, va,
		    paddr, size, priv, ro, kind));
	if (page_shift != NVGSP_GMMU_SPT_SHIFT)
		return (EINVAL);
	return (nvgsp_vmm_map_vram_flags_spt_noflush(vmm, va, paddr,
	    size, priv, ro, kind));
}

int
nvgsp_vmm_map_vram_flags_page_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro,
    uint8_t kind, uint8_t page_shift)
{
	if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		return (nvgsp_vmm_map_vram_flags_pd0_prepared_noflush(vmm,
		    va, paddr, size, priv, ro, kind));
	if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		return (nvgsp_vmm_map_vram_flags_lpt_prepared_noflush(vmm,
		    va, paddr, size, priv, ro, kind));
	if (page_shift != NVGSP_GMMU_SPT_SHIFT)
		return (EINVAL);
	return (nvgsp_vmm_map_vram_flags_spt_prepared_noflush(vmm, va,
	    paddr, size, priv, ro, kind));
}

int
nvgsp_vmm_map_vram_flags_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro, uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags_page_noflush(vmm, va, paddr,
	    size, priv, ro, kind, NVGSP_GMMU_SPT_SHIFT));
}

int
nvgsp_vmm_map_vram_flags(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro, uint8_t kind)
{
	int err = nvgsp_vmm_map_vram_flags_noflush(vmm, va, paddr, size,
	    priv, ro, kind);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}

int
nvgsp_vmm_map_vram(struct nvgsp_vmm *vmm, uint64_t va,
	uint64_t paddr, uint64_t size, uint8_t kind)
{
	return (nvgsp_vmm_map_vram_flags(vmm, va, paddr, size, 0, 0, kind));
}

int
nvgsp_vmm_unmap_noflush(struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	uint64_t off;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	for (off = 0; off < size; off += NVGSP_GMMU_PT_PAGE_SIZE) {
		uint64_t pte = nvgsp_vmm_range_has_sparse_region(vmm,
		    va + off, NVGSP_GMMU_PT_PAGE_SIZE) ?
		    nvgsp_pte_to_sparse() : 0;

		nvgsp_vmm_unmap_existing_pte(vmm, va + off, pte, 0);
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_check_unmap_valid_range_locked()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It does not write PTEs,
 *   allocate page-table storage, release PTs, or mutate sparse-region state.
 *
 * Lifetime:
 *   The result is valid while the caller keeps VM remap serialization and
 *   vmm->tok held.  It proves that a following valid-clear writer will not
 *   discover an unhandled parent/child ownership conflict after clearing
 *   earlier chunks.
 *
 * Threading:
 *   Callers hold vmm->tok.  preserve_target_pts is used by remap-to-sparse.
 *   For SPT/LPT targets, a live 2 MiB parent must already be materialized so
 *   the child storage survives for the sparse writer.  For a PD0 sparse
 *   target, a full child PT may be consumed only when no linked sparse region
 *   still depends on it; the parent PD0 storage is preserved for the following
 *   prepared PD0 sparse writer.
 */
static int
nvgsp_vmm_check_unmap_valid_range_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int preserve_target_pts,
    uint8_t preserve_page_shift)
{
	uint64_t off;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	for (off = 0; off < size;) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint64_t remaining_pages;
		uint32_t pd0_idx, spt_idx, count;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		spt_idx = (uint32_t)((cur >> NVGSP_GMMU_SPT_SHIFT) &
		    (NVGSP_GMMU_SPT_ENTRIES - 1));
		remaining_pages = (size - off) / NVGSP_GMMU_PT_PAGE_SIZE;
		count = (uint32_t)MIN(remaining_pages,
		    NVGSP_GMMU_SPT_ENTRIES - spt_idx);
		if (pt != NULL) {
			if (preserve_target_pts &&
			    preserve_page_shift == NVGSP_GMMU_PD0_SHIFT) {
				if (spt_idx != 0 ||
				    count != NVGSP_GMMU_SPT_ENTRIES)
					return (EBUSY);
				if (nvgsp_vmm_pt_has_sparse_region(vmm, pt))
					return (EBUSY);
			}
			off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
			continue;
		}

		pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
		if (pd0 != NULL && pd0->slot_state[pd0_idx] !=
		    NVGSP_VMM_PD0_SLOT_EMPTY) {
			if (preserve_target_pts &&
			    preserve_page_shift != NVGSP_GMMU_PD0_SHIFT)
				return (EBUSY);
			if (spt_idx != 0 || count != NVGSP_GMMU_SPT_ENTRIES)
				return (EBUSY);
		}
		off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_invalid_spt_prepared_locked()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It consumes no BO mapping or
 *   sparse-region ownership; callers must unlink mapping/region state as part
 *   of the enclosing remap plan.
 *
 * Lifetime:
 *   The target range is rewritten with the hardware invalid encoding at 4 KiB
 *   granularity.  The explicit purpose decides whether that write is semantic
 *   final invalid state or only conflict resolution for a following target
 *   writer.  If preserve_target_pts is false, empty child PTs may be reclaimed
 *   before the caller's final flush/invalidate; otherwise target PTs remain
 *   linked for a following prepared writer.
 *
 * Threading:
 *   Callers hold vmm->tok and are in a prepared commit section.  This helper
 *   does not allocate page tables, pin BOs, lookup handles, or wait on fences.
 */
static int
nvgsp_vmm_write_invalid_spt_prepared_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int preserve_target_pts,
    enum nvgsp_vmm_clear_purpose purpose)
{
	uint64_t off;
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	err = nvgsp_vmm_check_unmap_valid_range_locked(vmm, va, size,
	    preserve_target_pts, NVGSP_GMMU_SPT_SHIFT);
	if (err != 0)
		return (err);

	for (off = 0; off < size;) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint64_t remaining_pages;
		uint32_t pd0_idx, spt_idx, count;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		spt_idx = (uint32_t)((cur >> NVGSP_GMMU_SPT_SHIFT) &
		    (NVGSP_GMMU_SPT_ENTRIES - 1));
		remaining_pages = (size - off) / NVGSP_GMMU_PT_PAGE_SIZE;
		count = (uint32_t)MIN(remaining_pages,
		    NVGSP_GMMU_SPT_ENTRIES - spt_idx);
		if (pt != NULL) {
			if (preserve_target_pts || spt_idx != 0 ||
			    count != NVGSP_GMMU_SPT_ENTRIES ||
			    !nvgsp_vmm_skip_clear_full_pt(vmm, pt,
			    purpose)) {
				nvgsp_vmm_write_old_valid_pte_range(vmm,
				    pt, spt_idx, count, 0, purpose);
				if (!preserve_target_pts)
					nvgsp_vmm_reclaim_empty_pt(vmm,
					    pt, 0);
			}
			off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
			continue;
		}

		pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
		if (pd0 != NULL &&
		    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_EMPTY) {
			if (preserve_target_pts || spt_idx != 0 ||
			    count != NVGSP_GMMU_SPT_ENTRIES)
				return (EBUSY);
			nvgsp_vmm_pd0_write_empty_slot(vmm->gsp, pd0,
			    pd0_idx);
			vmm->stats.pte_fast_clear_count++;
			vmm->stats.pte_fast_invalid_clear_count++;
			nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_PD0_SHIFT,
			    1);
			nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
			    NVGSP_GMMU_PD0_SHIFT, 1);
			vmm->stats.pte_leaf_clear_count[
			    nvgsp_vmm_page_shift_bucket(
			    NVGSP_GMMU_PD0_SHIFT)]++;
			nvgsp_vmm_release_pd0_if_empty(vmm, pd0);
		}
		off += (uint64_t)count * NVGSP_GMMU_PT_PAGE_SIZE;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_invalid_preserve_sparse_spt_prepared_locked()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  Existing sparse regions remain
 *   owned by vmm; this helper only projects their target state into SPT PTEs
 *   while clearing valid mappings from the same range.
 *
 * Lifetime:
 *   The target range becomes invalid except where an already-linked sparse
 *   region covers it, in which case the corresponding 4 KiB leaf is written as
 *   sparse.  Empty PTs may be reclaimed only when no sparse state or sparse
 *   region still pins the PT window.
 *
 * Threading:
 *   Callers hold vmm->tok and are in a prepared commit section.  Live 2 MiB
 *   parent leaves must have been materialized before entry; encountering one
 *   is treated as a prepare/planner error and returned before mutation.
 */
static int
nvgsp_vmm_write_invalid_preserve_sparse_spt_prepared_locked(
    struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	struct nvgsp_vmm_user_pt *pt;
	uint64_t off, cur, chunk, limit, run_size;
	uint64_t sparse_pte = nvgsp_pte_to_sparse();
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	err = nvgsp_vmm_check_unmap_valid_range_locked(vmm, va, size, 1,
	    NVGSP_GMMU_SPT_SHIFT);
	if (err != 0)
		return (err);

	for (off = 0; off < size;) {
		cur = va + off;
		chunk = nvgsp_vmm_user_pt_chunk_size(cur, size - off);
		limit = cur + chunk;
		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		while (cur < limit) {
			int sparse;

			sparse = nvgsp_vmm_sparse_state_run_locked(vmm,
			    cur, limit, &run_size);
			if ((run_size & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0 ||
			    run_size == 0)
				return (EIO);
			if (pt == NULL && sparse)
				return (ENOENT);
			cur += run_size;
		}
		off += chunk;
	}

	for (off = 0; off < size;) {
		cur = va + off;
		chunk = nvgsp_vmm_user_pt_chunk_size(cur, size - off);
		limit = cur + chunk;
		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		while (cur < limit) {
			uint32_t count, spt_idx;
			uint64_t pte;
			int sparse;

			sparse = nvgsp_vmm_sparse_state_run_locked(vmm,
			    cur, limit, &run_size);
			if ((run_size & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0 ||
			    run_size == 0)
				return (EIO);
			if (pt == NULL) {
				if (sparse)
					return (ENOENT);
				cur += run_size;
				continue;
			}

			pte = sparse ? sparse_pte : 0;
			spt_idx = nvgsp_vmm_spt_idx(cur);
			count = (uint32_t)(run_size / NVGSP_GMMU_PT_PAGE_SIZE);
			nvgsp_vmm_write_old_valid_pte_range(vmm, pt,
			    spt_idx, count, pte,
			    NVGSP_VMM_CLEAR_FINAL_INVALID);
			cur += run_size;
		}
		if (pt != NULL)
			nvgsp_vmm_reclaim_empty_pt(vmm, pt, 0);
		off += chunk;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_invalid_lpt_prepared_locked()
 *
 * Ownership:
 *   Borrows vmm and rewrites only the target LPT/SPT page-table storage.  The
 *   enclosing remap plan owns mapping, BO, and sparse-region lifetime.
 *
 * Lifetime:
 *   The 64 KiB target leaves are written with the hardware invalid encoding.
 *   The explicit purpose decides whether that write is final invalid state or
 *   conflict resolution before a following target writer.  Empty child PTs may
 *   be reclaimed unless preserve_target_pts asks the backend to keep them for
 *   a following writer.
 *
 * Threading:
 *   Callers hold vmm->tok.  The helper is prepared-only and performs no lazy
 *   allocation or external waits.
 */
static int
nvgsp_vmm_write_invalid_lpt_prepared_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int preserve_target_pts,
    enum nvgsp_vmm_clear_purpose purpose)
{
	struct nvgsp_state *sc = vmm->gsp;
	uint64_t off;

	if ((va | size) & (NVGSP_GMMU_LPT_PAGE_SIZE - 1))
		return (EINVAL);

	for (off = 0; off < size;) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint64_t cur = va + off;
		uint32_t count, lpt_idx, pd0_idx;

		pt = nvgsp_vmm_user_pt_find_va(vmm, cur);
		if (pt == NULL) {
			pd0 = nvgsp_vmm_pd0_find_va(vmm, cur, &pd0_idx);
			if (pd0 != NULL && pd0->slot_state[pd0_idx] !=
			    NVGSP_VMM_PD0_SLOT_EMPTY)
				return (EBUSY);
			off += NVGSP_GMMU_LPT_PAGE_SIZE;
			continue;
		}

		lpt_idx = nvgsp_vmm_lpt_idx(cur);
		count = nvgsp_vmm_lpt_chunk_count(cur, size - off);
		nvgsp_vmm_clear_spt_for_lpt_invalid_range(vmm, pt,
		    lpt_idx, count, purpose);
		nvgsp_vmm_lpt_mark_range_state(pt, lpt_idx, count, 0,
		    0);
		nvgsp_bar_set_bar1_region64(sc,
		    pt->lpt.bar1_gva + (uint64_t)lpt_idx * 8, 0, count);
		vmm->stats.pte_fast_clear_count += count;
		vmm->stats.pte_fast_invalid_clear_count += count;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_LPT_SHIFT,
		    count);
		nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
		    NVGSP_GMMU_LPT_SHIFT, count);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_LPT_SHIFT)] +=
		    count;
		if (!preserve_target_pts)
			nvgsp_vmm_reclaim_empty_pt(vmm, pt, 0);
		off += (uint64_t)count * NVGSP_GMMU_LPT_PAGE_SIZE;
	}
	return (0);
}

/*
 * nvgsp_vmm_write_invalid_pd0_prepared_locked()
 *
 * Ownership:
 *   Borrows vmm and rewrites only PD0 page-table ownership for full 2 MiB
 *   windows.  The caller owns mapping and sparse-region publication.
 *
 * Lifetime:
 *   A final-invalid call clears a PD0 valid/sparse leaf to empty.  A conflict
 *   call clears only child PT ownership that would block a following PD0 target
 *   writer; same-size PD0 leaf replacement is direct overwrite and therefore
 *   records a skip instead of writing empty first.  If preserve_target_pd0 is
 *   true, the PD0 page itself remains linked so the immediately following
 *   prepared PD0 writer can install new leaf state without allocating in
 *   commit.
 *
 * Threading:
 *   Callers hold vmm->tok.  This backend op performs deterministic BAR1 writes
 *   and page-table frees only; it does not allocate or wait on GPU work.
 */
static int
nvgsp_vmm_write_invalid_pd0_prepared_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int preserve_target_pd0,
    enum nvgsp_vmm_clear_purpose purpose)
{
	uint64_t off;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;

	if ((va | size) & (page_size - 1))
		return (EINVAL);

	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint32_t pd0_idx;

		pt = nvgsp_vmm_user_pt_find_va(vmm, va + off);
		if (pt != NULL) {
			if (nvgsp_vmm_pt_has_sparse_region(vmm, pt))
				return (EBUSY);
			nvgsp_vmm_note_pt_skip_clear(vmm, pt, purpose);
			if (preserve_target_pd0)
				nvgsp_vmm_free_pt_preserve_pd0(vmm, pt);
			else
				nvgsp_vmm_free_pt(vmm, pt, 0);
			continue;
		}

		pd0 = nvgsp_vmm_pd0_find_va(vmm, va + off, &pd0_idx);
		if (pd0 == NULL ||
		    pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_EMPTY)
			continue;
		if (pd0->slot_state[pd0_idx] == NVGSP_VMM_PD0_SLOT_CHILD)
			return (EIO);
		if (purpose == NVGSP_VMM_CLEAR_CONFLICT) {
			nvgsp_vmm_note_skip_clear(vmm, NVGSP_GMMU_PD0_SHIFT,
			    1);
			continue;
		}
		nvgsp_vmm_pd0_write_empty_slot(vmm->gsp, pd0, pd0_idx);
		vmm->stats.pte_fast_clear_count++;
		vmm->stats.pte_fast_invalid_clear_count++;
		nvgsp_vmm_note_bulk_clear(vmm, NVGSP_GMMU_PD0_SHIFT, 1);
		nvgsp_vmm_note_clear_by_purpose(vmm, purpose,
		    NVGSP_GMMU_PD0_SHIFT, 1);
		vmm->stats.pte_leaf_clear_count[
		    nvgsp_vmm_page_shift_bucket(NVGSP_GMMU_PD0_SHIFT)]++;
		if (!preserve_target_pd0)
			nvgsp_vmm_release_pd0_if_empty(vmm, pd0);
	}
	return (0);
}

static int
nvgsp_vmm_write_invalid_prepared_locked_purpose(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t page_shift, int preserve_target_pts,
    enum nvgsp_vmm_clear_purpose purpose)
{
	if (page_shift == NVGSP_GMMU_SPT_SHIFT)
		return (nvgsp_vmm_write_invalid_spt_prepared_locked(vmm,
		    va, size, preserve_target_pts, purpose));
	if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		return (nvgsp_vmm_write_invalid_lpt_prepared_locked(vmm,
		    va, size, preserve_target_pts, purpose));
	if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
		return (nvgsp_vmm_write_invalid_pd0_prepared_locked(vmm,
		    va, size, preserve_target_pts, purpose));
	}
	return (EINVAL);
}

static int
nvgsp_vmm_write_invalid_prepared_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t page_shift, int preserve_target_pts)
{
	return (nvgsp_vmm_write_invalid_prepared_locked_purpose(vmm, va,
	    size, page_shift, preserve_target_pts,
	    NVGSP_VMM_CLEAR_FINAL_INVALID));
}

/*
 * nvgsp_vmm_check_clear_pd0_target_range_locked()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned 2 MiB-aligned VA range.  It does not
 *   allocate page tables, write BAR1, mutate sparse regions, or retain
 *   pointers.
 *
 * Lifetime:
 *   The check is valid only while VM remap serialization is held until the
 *   matching clear_pd0_target writer runs.  A successful result means a direct
 *   2 MiB MAP target can clear any old valid 2 MiB leaf or whole child PT in
 *   the range without preserving lower-table storage.  When allow_sparse is
 *   true, sparse ownership is treated as already covered by a prepared sparse
 *   clear plan; other invariant failures are still reported.
 *
 * Threading:
 *   Caller holds vmm->tok for the prepared-state walk.  This is a read-only
 *   prepare/preflight helper for direct 2 MiB MAP.
 */
static int
nvgsp_vmm_check_clear_pd0_target_range_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int allow_sparse)
{
	uint64_t off;
	uint64_t page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;

	if ((va | size) & (page_size - 1))
		return (EINVAL);

	if (!allow_sparse && nvgsp_vmm_range_has_sparse_region(vmm, va,
	    size))
		return (EBUSY);

	for (off = 0; off < size; off += page_size) {
		struct nvgsp_vmm_pd0 *pd0;
		struct nvgsp_vmm_user_pt *pt;
		uint32_t pd0_idx;

		pt = nvgsp_vmm_user_pt_find_va(vmm, va + off);
		if (pt != NULL) {
			if (!allow_sparse && (pt->sparse_pte_count != 0 ||
			    pt->sparse_lpte_count != 0 ||
			    nvgsp_vmm_pt_has_sparse_region(vmm, pt)))
				return (EBUSY);
			continue;
		}

		pd0 = nvgsp_vmm_pd0_find_va(vmm, va + off, &pd0_idx);
		if (pd0 != NULL && pd0->slot_state[pd0_idx] ==
		    NVGSP_VMM_PD0_SLOT_CHILD)
			return (EIO);
	}
	return (0);
}

/*
 * nvgsp_vmm_check_clear_pd0_target_range()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned 2 MiB-aligned VA range.  It does not
 *   allocate page tables, write BAR1, mutate sparse regions, or retain
 *   pointers.
 *
 * Lifetime:
 *   The check is valid only while VM remap serialization is held until the
 *   matching clear_pd0_target writer runs.  Sparse ownership is rejected here;
 *   callers that already own a prepared sparse-clear plan must use
 *   nvgsp_vmm_check_clear_pd0_target_range_allow_sparse().
 *
 * Threading:
 *   Acquires vmm->tok for a read-only prepared-state walk.
 */
int
nvgsp_vmm_check_clear_pd0_target_range(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size)
{
	int err;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_check_clear_pd0_target_range_locked(vmm, va, size,
	    0);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_check_clear_pd0_target_range_allow_sparse()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned direct 2 MiB MAP target.  It consumes no
 *   sparse-unmap plan; the caller owns that prepared plan and must commit it
 *   before clear_pd0_target_noflush().
 *
 * Lifetime:
 *   The successful result is valid only while VM remap serialization keeps the
 *   prepared sparse-clear plan and live VMM state paired.  It permits old sparse
 *   leaf state to exist during prepare, but still rejects malformed child-table
 *   ownership that sparse clear cannot repair.
 *
 * Threading:
 *   Acquires vmm->tok for a read-only prepared-state walk.  No allocation,
 *   PTE/PDE write, sparse-list mutation, or GPU wait occurs.
 */
int
nvgsp_vmm_check_clear_pd0_target_range_allow_sparse(
    struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	int err;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_check_clear_pd0_target_range_locked(vmm, va, size,
	    1);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_clear_pd0_target_noflush()
 *
 * Ownership:
 *   Borrows vmm and consumes no external object ownership.  It clears only the
 *   caller's 2 MiB-aligned target windows so a following prepared PD0 writer
 *   can install valid_2m leaves in the same VM_BIND commit.
 *
 * Lifetime:
 *   Any freed child PTs are removed from vmm ownership immediately.  The PD0
 *   parent page is retained so the following prepared PD0 writer can install
 *   the direct 2 MiB target without allocating in commit.  Hardware visibility
 *   of the clear still belongs to the caller's final flush/TLB invalidate
 *   boundary.
 *
 * Threading:
 *   Acquires vmm->tok and performs no allocation, GEM lookup, BO pin, user
 *   copy, or GPU wait.  Sparse-region replacement is deliberately rejected:
 *   direct 2 MiB MAP must first have an explicit sparse cleanup plan.
 */
int
nvgsp_vmm_clear_pd0_target_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size)
{
	int err;

	if ((va | size) & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	if (nvgsp_vmm_range_has_sparse_region(vmm, va, size)) {
		lwkt_reltoken(&vmm->tok);
		return (EBUSY);
	}
	err = nvgsp_vmm_write_invalid_prepared_locked_purpose(vmm, va,
	    size, NVGSP_GMMU_PD0_SHIFT, 1,
	    NVGSP_VMM_CLEAR_CONFLICT);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_check_unmap_valid_range()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It never mutates hardware
 *   PTEs, page-table ownership, or sparse-region records.
 *
 * Lifetime:
 *   The caller must keep VM remap serialization until the corresponding
 *   unmap_valid writer runs.  A successful check proves the page-shift-aware
 *   writer will not encounter a partial parent leaf, an unpreservable child
 *   table, or sparse-region ownership that would fail after earlier clear
 *   chunks have already committed.
 *
 * Threading:
 *   Acquires vmm->tok for the direct lookup/state walk.  This is a prepare /
 *   pre-commit gate for DRM remove_range().
 */
int
nvgsp_vmm_check_unmap_valid_range_page(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, int preserve_target_pts,
    uint8_t preserve_page_shift)
{
	int err;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_check_unmap_valid_range_locked(vmm, va, size,
	    preserve_target_pts, preserve_page_shift);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

int
nvgsp_vmm_check_unmap_valid_range(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, int preserve_target_pts)
{
	return (nvgsp_vmm_check_unmap_valid_range_page(vmm, va, size,
	    preserve_target_pts, NVGSP_GMMU_SPT_SHIFT));
}

int
nvgsp_vmm_unmap_valid_page_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, uint8_t page_shift)
{
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	if (!nvgsp_vmm_range_has_sparse_region(vmm, va, size)) {
		err = nvgsp_vmm_write_invalid_prepared_locked(vmm, va,
		    size, page_shift, 0);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	} else {
		err = nvgsp_vmm_write_invalid_preserve_sparse_spt_prepared_locked(
		    vmm, va, size);
		if (err != 0) {
			lwkt_reltoken(&vmm->tok);
			return (err);
		}
	}
	lwkt_reltoken(&vmm->tok);
	return (0);
}

int
nvgsp_vmm_unmap_valid_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	return (nvgsp_vmm_unmap_valid_page_noflush(vmm, va, size,
	    NVGSP_GMMU_SPT_SHIFT));
}

/*
 * nvgsp_vmm_unmap_valid_preserve_page_noflush()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It consumes no sparse-region
 *   ownership and does not allocate or free page-table pages.
 *
 * Lifetime:
 *   This final-invalid helper keeps the target storage selected by
 *   preserve_page_shift linked for a caller that must retain prepared
 *   page-table storage after clearing valid leaves.  Overwrite-to-valid or
 *   overwrite-to-sparse callers must use the conflict-preserve wrapper so
 *   counters and trace do not report a semantic unmap.
 *
 * Threading:
 *   Acquires vmm->tok while writing PTEs/PDEs.  The helper is prepared-only:
 *   all required target storage must already exist before it is called.
 */
int
nvgsp_vmm_unmap_valid_preserve_page_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t preserve_page_shift)
{
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_write_invalid_prepared_locked(vmm, va, size,
	    preserve_page_shift, 1);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_clear_conflict_preserve_page_noflush()
 *
 * Ownership:
 *   Borrows vmm and the caller-owned VA range.  It clears only live page-table
 *   state that conflicts with a final valid or sparse target leaf; it consumes
 *   no BO, mapping, or sparse-region ownership.
 *
 * Lifetime:
 *   Target PT/PD0 storage remains linked for the following prepared writer in
 *   the same remap commit.  The clear is not a semantic unmap: visibility is
 *   paired with the caller's final install and TLB invalidate.
 *
 * Threading:
 *   Acquires vmm->tok while writing PTEs/PDEs.  No allocation, user lookup, or
 *   GPU fence wait occurs in this prepared commit helper.
 */
int
nvgsp_vmm_clear_conflict_preserve_page_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t preserve_page_shift)
{
	int err;

	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_write_invalid_prepared_locked_purpose(vmm, va,
	    size, preserve_page_shift, 1, NVGSP_VMM_CLEAR_CONFLICT);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

int
nvgsp_vmm_unmap_valid_preserve_pt_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size)
{
	return (nvgsp_vmm_unmap_valid_preserve_page_noflush(vmm, va,
	    size, NVGSP_GMMU_SPT_SHIFT));
}

int
nvgsp_vmm_unmap(struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	int err = nvgsp_vmm_unmap_noflush(vmm, va, size);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}

/*
 * nvgsp_vmm_alloc_sparse_region()
 *
 * Ownership:
 *   Allocates an unlinked sparse-region record and transfers it to *pregion.
 *   It does not allocate or validate any page-table storage.  The default
 *   helper creates a 4 KiB sparse region; page-aware callers use
 *   alloc_sparse_region_page() to keep the target page size with the region.
 *
 * Lifetime:
 *   The returned region is private until commit_sparse_noflush() links it into
 *   vmm.  A failed or abandoned prepare must release it with
 *   abort_sparse_region().
 *
 * Threading:
 *   May sleep while allocating the region record.  Callers that need prepared PTs
 *   must ensure them separately before entering the no-fail commit section.
 */
int
nvgsp_vmm_alloc_sparse_region_page(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, uint8_t page_shift,
    struct nvgsp_vmm_sparse_region **pregion)
{
	struct nvgsp_vmm_sparse_region *region;
	uint64_t page_size;

	(void)vmm;
	*pregion = NULL;
	if (page_shift == NVGSP_GMMU_SPT_SHIFT)
		page_size = NVGSP_GMMU_PT_PAGE_SIZE;
	else if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		page_size = NVGSP_GMMU_LPT_PAGE_SIZE;
	else if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	else
		return (EINVAL);
	if ((va | size) & (page_size - 1))
		return (EINVAL);

	region = kmalloc(sizeof(*region), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	region->addr = va;
	region->size = size;
	region->page_shift = page_shift;
	*pregion = region;
	return (0);
}

int
nvgsp_vmm_alloc_sparse_region(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, struct nvgsp_vmm_sparse_region **pregion)
{
	return (nvgsp_vmm_alloc_sparse_region_page(vmm, va, size,
	    NVGSP_GMMU_SPT_SHIFT, pregion));
}

/*
 * nvgsp_vmm_prepare_sparse_region()
 *
 * Ownership:
 *   Allocates an unlinked sparse-region record and transfers it to *pregion.
 *   It also ensures all target page-table storage for the sparse range exists
 *   before commit.  The default helper prepares 4 KiB sparse storage; 2 MiB
 *   callers prepare only PD0 parent pages and do not allocate child LPT/SPT
 *   tables.
 *
 * Lifetime:
 *   The returned region is private until commit_sparse_noflush() links it into
 *   vmm.  A failed or abandoned prepare must be released with
 *   abort_sparse_region().
 *
 * Threading:
 *   May sleep and allocate, so VM_BIND callers run it in prepare before the
 *   no-fail commit section.  It does not publish sparse PTEs.
 */
int
nvgsp_vmm_prepare_sparse_region_page(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, uint8_t page_shift,
    struct nvgsp_vmm_sparse_region **pregion)
{
	uint64_t page_size;
	int err;

	*pregion = NULL;
	if (page_shift == NVGSP_GMMU_SPT_SHIFT)
		page_size = NVGSP_GMMU_PT_PAGE_SIZE;
	else if (page_shift == NVGSP_GMMU_LPT_SHIFT)
		page_size = NVGSP_GMMU_LPT_PAGE_SIZE;
	else if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
	else
		return (EINVAL);
	if ((va | size) & (page_size - 1))
		return (EINVAL);

	if (page_shift == NVGSP_GMMU_PD0_SHIFT)
		err = nvgsp_vmm_ensure_pd0_range(vmm, va, size);
	else
		err = nvgsp_vmm_ensure_pt_range(vmm, va, size);
	if (err != 0)
		return (err);

	return (nvgsp_vmm_alloc_sparse_region_page(vmm, va, size,
	    page_shift, pregion));
}

int
nvgsp_vmm_prepare_sparse_region(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, struct nvgsp_vmm_sparse_region **pregion)
{
	return (nvgsp_vmm_prepare_sparse_region_page(vmm, va, size,
	    NVGSP_GMMU_SPT_SHIFT, pregion));
}

/*
 * nvgsp_vmm_commit_sparse_noflush()
 *
 * Ownership:
 *   Consumes region on success and links it into vmm.  On failure the region
 *   remains unlinked and caller-owned so abort_sparse_region() can release it.
 *
 * Lifetime:
 *   Sparse PTEs become visible to the GPU only after the caller performs the
 *   enclosing VMM flush/TLB invalidate.  region->page_shift selects the
 *   backend sparse writer; existing DRM VM_BIND callers still pass 4 KiB
 *   regions until the large-sparse gate is explicitly enabled.
 *
 * Threading:
 *   Runs in the VM_BIND commit section.  It does not allocate page tables or
 *   sparse records; prepare_sparse_region() already did that work.
 */
int
nvgsp_vmm_commit_sparse_noflush(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_region *region)
{
	int err;

	if (region == NULL)
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_sparse_region_insert_preflight_locked(vmm,
	    region);
	if (err != 0) {
		lwkt_reltoken(&vmm->tok);
		return (err);
	}
	LIST_INSERT_HEAD(&vmm->sparse_regions, region, link);
	err = nvgsp_vmm_write_sparse_prepared(vmm, region->addr,
	    region->size, region->page_shift);
	if (err != 0) {
		LIST_REMOVE(region, link);
		lwkt_reltoken(&vmm->tok);
		return (err);
	}
	nvgsp_vmm_sparse_regions_merge_locked(vmm, region);
	lwkt_reltoken(&vmm->tok);
	return (0);
}

/*
 * nvgsp_vmm_abort_sparse_region()
 *
 * Ownership:
 *   Consumes an unlinked sparse region returned by prepare_sparse_region().
 *   Passing NULL is allowed.
 *
 * Lifetime:
 *   Only call this for regions that commit_sparse_noflush() did not consume.
 *   Linked sparse regions are owned by vmm and removed by unmap_sparse.
 *
 * Threading:
 *   No vmm token is required because the region is not linked in vmm.
 */
void
nvgsp_vmm_abort_sparse_region(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_region *region)
{
	(void)vmm;

	if (region != NULL)
		_kfree(region, M_NVGSP_VMM);
}

int
nvgsp_vmm_map_sparse_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_vmm_sparse_region *region;
	int err;

	err = nvgsp_vmm_prepare_sparse_region(vmm, va, size, &region);
	if (err != 0)
		return (err);
	err = nvgsp_vmm_commit_sparse_noflush(vmm, region);
	if (err != 0) {
		nvgsp_vmm_abort_sparse_region(vmm, region);
		return (err);
	}
	return (0);
}

int
nvgsp_vmm_map_sparse(struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	int err = nvgsp_vmm_map_sparse_noflush(vmm, va, size);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}

int
nvgsp_vmm_unmap_sparse_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	return (nvgsp_vmm_unmap_sparse_range_noflush(vmm, va, size));
}

/*
 * nvgsp_vmm_sparse_region_page_size()
 *
 * Ownership:
 *   Borrows a sparse-region page_shift by value and returns the semantic leaf
 *   size used by the sparse backend writer.
 *
 * Lifetime:
 *   The computed size is a scalar and has no lifetime dependency on the
 *   region object.
 *
 * Threading:
 *   Pure helper.  It does not read or mutate VMM state.
 */
static int
nvgsp_vmm_sparse_region_page_size(uint8_t page_shift, uint64_t *page_size)
{
	if (page_shift == NVGSP_GMMU_SPT_SHIFT) {
		*page_size = NVGSP_GMMU_PT_PAGE_SIZE;
		return (0);
	}
	if (page_shift == NVGSP_GMMU_LPT_SHIFT) {
		*page_size = NVGSP_GMMU_LPT_PAGE_SIZE;
		return (0);
	}
	if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
		*page_size = 1ULL << NVGSP_GMMU_PD0_SHIFT;
		return (0);
	}
	return (EINVAL);
}

static int
nvgsp_vmm_sparse_region_end(
    const struct nvgsp_vmm_sparse_region *region, uint64_t *pend)
{
	uint64_t page_size;
	int err;

	if (region == NULL || region->size == 0 ||
	    region->addr > UINT64_MAX - region->size)
		return (EINVAL);
	err = nvgsp_vmm_sparse_region_page_size(region->page_shift,
	    &page_size);
	if (err != 0)
		return (err);
	if (((region->addr | region->size) & (page_size - 1)) != 0)
		return (EINVAL);
	*pend = region->addr + region->size;
	return (0);
}

/*
 * nvgsp_vmm_sparse_region_insert_preflight_locked()
 *
 * Ownership:
 *   Borrows a caller-owned, unlinked sparse region and the VMM sparse-region
 *   list.  It does not take ownership of the new region and does not mutate
 *   the live list.
 *
 * Lifetime:
 *   The check is valid while VM remap serialization and vmm->tok remain held
 *   until commit links the region.  Adjacent regions are allowed because the
 *   later metadata-only normalize pass may merge them.
 *
 * Threading:
 *   Caller holds vmm->tok.  This is a read-only preflight; it performs no
 *   allocation, BAR1 writes, sparse-list mutation, or GPU waits.
 */
static int
nvgsp_vmm_sparse_region_insert_preflight_locked(
    struct nvgsp_vmm *vmm, const struct nvgsp_vmm_sparse_region *region)
{
	const struct nvgsp_vmm_sparse_region *old;
	uint64_t end;
	int err;

	err = nvgsp_vmm_sparse_region_end(region, &end);
	if (err != 0)
		return (err);
	LIST_FOREACH(old, &vmm->sparse_regions, link) {
		uint64_t old_end;

		err = nvgsp_vmm_sparse_region_end(old, &old_end);
		if (err != 0)
			return (err);
		if (old_end <= region->addr || old->addr >= end)
			continue;
		return (EBUSY);
	}
	return (0);
}

static int
nvgsp_vmm_sparse_region_writer_preflight_locked(
    struct nvgsp_vmm *vmm, const struct nvgsp_vmm_sparse_region *region)
{
	uint64_t end;
	int err;

	err = nvgsp_vmm_sparse_region_end(region, &end);
	if (err != 0)
		return (err);
	return (nvgsp_vmm_check_prepared_pt_range_locked(vmm,
	    region->addr, end - region->addr, region->page_shift));
}

/*
 * nvgsp_vmm_check_sparse_regions_commit()
 *
 * Ownership:
 *   Borrows a caller-owned array of prepared, unlinked sparse regions.  It
 *   does not consume region ownership, link records, mutate sparse metadata,
 *   or write PTE/PDE contents.
 *
 * Lifetime:
 *   The result is valid while VM remap serialization is held until the caller
 *   commits the paired sparse clear and sparse insert operations.  replace_*,
 *   when non-empty, names the VM_BIND range whose old sparse regions are
 *   already covered by the caller's prepared sparse-clear plan, so live sparse
 *   overlap inside that range is allowed during this preflight.
 *
 * Threading:
 *   Acquires vmm->tok and performs a read-only validation pass.  It checks
 *   live sparse overlap, overlap among new regions, and prepared backend writer
 *   targets before the first sparse region is linked or sparse PTE is written.
 */
int
nvgsp_vmm_check_sparse_regions_commit(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_region **regions, uint32_t region_count,
    uint64_t replace_addr, uint64_t replace_size)
{
	uint64_t replace_end = 0;
	int has_replace = replace_size != 0;
	int err = 0;

	if (region_count == 0)
		return (0);
	if (regions == NULL)
		return (EINVAL);
	if (has_replace) {
		if (replace_addr > UINT64_MAX - replace_size)
			return (EINVAL);
		replace_end = replace_addr + replace_size;
	}

	lwkt_gettoken(&vmm->tok);
	for (uint32_t i = 0; i < region_count; i++) {
		const struct nvgsp_vmm_sparse_region *region = regions[i];
		const struct nvgsp_vmm_sparse_region *old;
		uint64_t end;

		err = nvgsp_vmm_sparse_region_end(region, &end);
		if (err != 0)
			break;
		if (has_replace &&
		    (region->addr < replace_addr || end > replace_end)) {
			err = EINVAL;
			break;
		}
		err = nvgsp_vmm_sparse_region_writer_preflight_locked(vmm,
		    region);
		if (err != 0)
			break;

		LIST_FOREACH(old, &vmm->sparse_regions, link) {
			uint64_t old_end;

			err = nvgsp_vmm_sparse_region_end(old, &old_end);
			if (err != 0)
				break;
			if (old_end <= region->addr || old->addr >= end)
				continue;
			if (has_replace)
				continue;
			err = EBUSY;
			break;
		}
		if (err != 0)
			break;

		for (uint32_t j = 0; j < i; j++) {
			const struct nvgsp_vmm_sparse_region *other =
			    regions[j];
			uint64_t other_end;

			err = nvgsp_vmm_sparse_region_end(other,
			    &other_end);
			if (err != 0)
				break;
			if (other_end <= region->addr || other->addr >= end)
				continue;
			err = EBUSY;
			break;
		}
		if (err != 0)
			break;
	}
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_sparse_regions_merge_locked()
 *
 * Ownership:
 *   Borrows one already-linked sparse region and may consume adjacent linked
 *   sparse-region records with the same page_shift.  The kept region remains
 *   owned by vmm; merged neighbor records are unlinked and freed.
 *
 * Lifetime:
 *   This is metadata-only normalize.  Hardware sparse PTE/PDE contents already
 *   describe the same sparse state on both sides of the boundary, so merging
 *   records does not require additional PTE writes or invalidate work.
 *
 * Threading:
 *   Caller holds vmm->tok after a no-fail sparse commit.  The helper performs
 *   no allocation, user lookup, BO work, BAR1 writes, or GPU waits.
 */
static void
nvgsp_vmm_sparse_regions_merge_locked(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_region *region)
{
	struct nvgsp_vmm_sparse_region *other, *other_next;
	uint64_t region_end;
	int err;

restart:
	err = nvgsp_vmm_sparse_region_end(region, &region_end);
	if (err != 0)
		return;
	LIST_FOREACH_MUTABLE(other, &vmm->sparse_regions, link,
	    other_next) {
		uint64_t other_end;

		if (other == region || other->page_shift != region->page_shift)
			continue;
		err = nvgsp_vmm_sparse_region_end(other, &other_end);
		if (err != 0)
			continue;
		if (region_end == other->addr) {
			region->size = other_end - region->addr;
			LIST_REMOVE(other, link);
			_kfree(other, M_NVGSP_VMM);
			goto restart;
		}
		if (other_end == region->addr) {
			region->addr = other->addr;
			region->size = region_end - other->addr;
			LIST_REMOVE(other, link);
			_kfree(other, M_NVGSP_VMM);
			goto restart;
		}
	}
}

struct nvgsp_vmm_sparse_unmap_entry {
	struct nvgsp_vmm_sparse_region *old_region;
	uint64_t cut_addr;
	uint64_t cut_size;
	uint8_t page_shift;
	LIST_HEAD(, nvgsp_vmm_sparse_unmap_keep) keep_regions;
	LIST_HEAD(, nvgsp_vmm_sparse_unmap_clear) clear_ranges;
	LIST_HEAD(, nvgsp_vmm_sparse_unmap_split) split_pts;
};

struct nvgsp_vmm_sparse_unmap_keep {
	LIST_ENTRY(nvgsp_vmm_sparse_unmap_keep) link;
	struct nvgsp_vmm_sparse_region *region;
	int write_pte;
};

struct nvgsp_vmm_sparse_unmap_clear {
	LIST_ENTRY(nvgsp_vmm_sparse_unmap_clear) link;
	uint64_t addr;
	uint64_t size;
	uint8_t page_shift;
	int preserve_target_pts;
	int write_pte;
};

struct nvgsp_vmm_sparse_unmap_split {
	LIST_ENTRY(nvgsp_vmm_sparse_unmap_split) link;
	uint64_t va;
	struct nvgsp_vmm_user_pt *pt;
};

struct nvgsp_vmm_sparse_unmap_plan {
	struct nvgsp_vmm_sparse_unmap_entry *entries;
	uint32_t entry_count;
	uint64_t addr;
	uint64_t size;
	int wrote_hw;
};

/*
 * nvgsp_vmm_sparse_unmap_plan_wrote_hw()
 *
 * Ownership:
 *   Borrows a sparse-unmap plan after commit.  The caller keeps ownership of the
 *   plan and must still release it with fini_unmap_sparse_range().
 *
 * Lifetime:
 *   The returned value is copied from the successful commit path and remains
 *   valid until the plan is released.  It reports whether the plan wrote any
 *   PTE/PDE storage itself; a following target writer may still write the same
 *   remap range and own its own dirty accounting.
 *
 * Threading:
 *   Does not take vmm->tok and does not mutate state.  The caller must serialize
 *   against the plan commit/fini lifetime.
 */
int
nvgsp_vmm_sparse_unmap_plan_wrote_hw(
    const struct nvgsp_vmm_sparse_unmap_plan *plan)
{
	return (plan != NULL && plan->wrote_hw != 0);
}

static int
nvgsp_vmm_sparse_unmap_entries_have_hw_writes(
    const struct nvgsp_vmm_sparse_unmap_entry *entries, uint32_t entry_count)
{
	for (uint32_t i = 0; i < entry_count; i++) {
		const struct nvgsp_vmm_sparse_unmap_clear *clear;
		const struct nvgsp_vmm_sparse_unmap_keep *keep;

		if (!LIST_EMPTY(&entries[i].split_pts))
			return (1);
		LIST_FOREACH(clear, &entries[i].clear_ranges, link) {
			if (clear->write_pte)
				return (1);
		}
		LIST_FOREACH(keep, &entries[i].keep_regions, link) {
			if (keep->write_pte)
				return (1);
		}
	}
	return (0);
}

static uint64_t
nvgsp_vmm_sparse_unmap_round_down(uint64_t value, uint64_t align)
{
	return (value & ~(align - 1));
}

static uint64_t
nvgsp_vmm_sparse_unmap_next_boundary(uint64_t addr, uint64_t align)
{
	uint64_t mask = align - 1;

	return ((addr + mask) & ~mask);
}

/*
 * nvgsp_vmm_sparse_unmap_add_split()
 *
 * Ownership:
 *   Allocates a caller-owned split-plan node for one 2 MiB PD0 sparse leaf.
 *   The node later owns an unlinked child PT after prepare succeeds.
 *
 * Lifetime:
 *   The split node lives inside the unmap entry until commit consumes its PT or
 *   fini aborts it.  Duplicate windows share one split node.
 *
 * Threading:
 *   Runs in prepare/build context before the no-fail commit section.  It does
 *   not mutate the live VMM page tables.
 */
static int
nvgsp_vmm_sparse_unmap_add_split(
    struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t va)
{
	struct nvgsp_vmm_sparse_unmap_split *split;
	uint64_t base = nvgsp_vmm_sparse_unmap_round_down(va,
	    1ULL << NVGSP_GMMU_PD0_SHIFT);

	LIST_FOREACH(split, &entry->split_pts, link) {
		if (split->va == base)
			return (0);
	}
	split = kmalloc(sizeof(*split), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	if (split == NULL)
		return (ENOMEM);
	split->va = base;
	split->pt = NULL;
	LIST_INSERT_HEAD(&entry->split_pts, split, link);
	return (0);
}

static int
nvgsp_vmm_sparse_unmap_add_splits_for_range(
    struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t va, uint64_t size)
{
	uint64_t cur, end;
	int err;

	if (entry->page_shift != NVGSP_GMMU_PD0_SHIFT)
		return (0);
	end = va + size;
	for (cur = nvgsp_vmm_sparse_unmap_round_down(va,
	    1ULL << NVGSP_GMMU_PD0_SHIFT); cur < end;
	    cur += 1ULL << NVGSP_GMMU_PD0_SHIFT) {
		err = nvgsp_vmm_sparse_unmap_add_split(entry, cur);
		if (err != 0)
			return (err);
	}
	return (0);
}

static int
nvgsp_vmm_sparse_unmap_append_keep(
    struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t addr,
    uint64_t size, uint8_t page_shift)
{
	struct nvgsp_vmm_sparse_unmap_keep *keep;
	int err;

	keep = kmalloc(sizeof(*keep), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	if (keep == NULL)
		return (ENOMEM);
	keep->write_pte = page_shift != entry->page_shift;
	err = nvgsp_vmm_alloc_sparse_region_page(NULL, addr, size,
	    page_shift, &keep->region);
	if (err != 0) {
		_kfree(keep, M_NVGSP_VMM);
		return (err);
	}
	if (keep->write_pte) {
		err = nvgsp_vmm_sparse_unmap_add_splits_for_range(entry,
		    addr, size);
		if (err != 0) {
			nvgsp_vmm_abort_sparse_region(NULL, keep->region);
			_kfree(keep, M_NVGSP_VMM);
			return (err);
		}
	}
	LIST_INSERT_HEAD(&entry->keep_regions, keep, link);
	return (0);
}

static int
nvgsp_vmm_sparse_unmap_append_clear(
    struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t addr,
    uint64_t size, uint8_t page_shift, int preserve_target_pts,
    int prepare_target_storage, int write_pte)
{
	struct nvgsp_vmm_sparse_unmap_clear *clear;
	int err;

	clear = kmalloc(sizeof(*clear), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	if (clear == NULL)
		return (ENOMEM);
	clear->addr = addr;
	clear->size = size;
	clear->page_shift = page_shift;
	clear->write_pte = write_pte;
	clear->preserve_target_pts =
	    page_shift < entry->page_shift &&
	    (preserve_target_pts || prepare_target_storage || write_pte);
	if (clear->preserve_target_pts) {
		err = nvgsp_vmm_sparse_unmap_add_splits_for_range(entry,
		    addr, size);
		if (err != 0) {
			_kfree(clear, M_NVGSP_VMM);
			return (err);
		}
	}
	LIST_INSERT_HEAD(&entry->clear_ranges, clear, link);
	return (0);
}

/*
 * nvgsp_vmm_sparse_unmap_append_segments()
 *
 * Ownership:
 *   Allocates caller-owned keep or clear plan nodes for [addr, addr+size).
 *   Keep nodes own unlinked sparse-region records; clear nodes own only scalar
 *   range data.  No live VMM state is mutated.
 *
 * Lifetime:
 *   The generated segments use the largest page size allowed by the old sparse
 *   leaf, max_page_shift, and the current alignment.  Segments below an old
 *   PD0 sparse leaf add a split plan so commit has child PT storage before
 *   lower sparse/invalid writes.
 *
 * Threading:
 *   Runs in prepare/build context and may sleep for allocation.  Commit later
 *   consumes the prepared segment lists without allocating.
 */
static int
nvgsp_vmm_sparse_unmap_append_segments(
    struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t addr,
    uint64_t size, int keep, uint8_t max_page_shift, int preserve_target_pts,
    int prepare_target_storage, int write_target_pte)
{
	uint64_t cur = addr;
	uint64_t end = addr + size;
	int err;

	if (max_page_shift > entry->page_shift)
		max_page_shift = entry->page_shift;
	while (cur < end) {
		uint64_t remaining = end - cur;
		uint64_t chunk, next;
		uint8_t shift;

		if (max_page_shift >= NVGSP_GMMU_PD0_SHIFT &&
		    (cur & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1)) == 0 &&
		    remaining >= (1ULL << NVGSP_GMMU_PD0_SHIFT)) {
			shift = NVGSP_GMMU_PD0_SHIFT;
			chunk = nvgsp_vmm_sparse_unmap_round_down(
			    remaining, 1ULL << NVGSP_GMMU_PD0_SHIFT);
		} else if (max_page_shift >= NVGSP_GMMU_LPT_SHIFT &&
		    (cur & (NVGSP_GMMU_LPT_PAGE_SIZE - 1)) == 0 &&
		    remaining >= NVGSP_GMMU_LPT_PAGE_SIZE) {
			shift = NVGSP_GMMU_LPT_SHIFT;
			chunk = nvgsp_vmm_sparse_unmap_round_down(
			    remaining, NVGSP_GMMU_LPT_PAGE_SIZE);
			if (entry->page_shift >= NVGSP_GMMU_PD0_SHIFT) {
				next = nvgsp_vmm_sparse_unmap_next_boundary(
				    cur, 1ULL << NVGSP_GMMU_PD0_SHIFT);
				if (next > cur && chunk > next - cur)
					chunk = next - cur;
			}
		} else {
			shift = NVGSP_GMMU_SPT_SHIFT;
			next = nvgsp_vmm_sparse_unmap_next_boundary(cur,
			    NVGSP_GMMU_LPT_PAGE_SIZE);
			chunk = remaining;
			if (next > cur && chunk > next - cur)
				chunk = next - cur;
			chunk = nvgsp_vmm_sparse_unmap_round_down(chunk,
			    NVGSP_GMMU_PT_PAGE_SIZE);
			if (chunk == 0)
				chunk = NVGSP_GMMU_PT_PAGE_SIZE;
		}
		if (cur > UINT64_MAX - chunk || cur + chunk > end)
			return (EINVAL);
		err = keep ?
		    nvgsp_vmm_sparse_unmap_append_keep(entry, cur, chunk,
		    shift) :
		    nvgsp_vmm_sparse_unmap_append_clear(entry, cur, chunk,
		    shift, preserve_target_pts, prepare_target_storage,
		    write_target_pte);
		if (err != 0)
			return (err);
		cur += chunk;
	}
	return (0);
}

/*
 * nvgsp_vmm_prepare_split_sparse_2m()
 *
 * Ownership:
 *   Allocates an unlinked child PT for one existing PD0 sparse leaf.  The
 *   caller owns *ppt until commit_split_sparse_2m_locked() consumes it or fini
 *   aborts it.
 *
 * Lifetime:
 *   The old PD0 sparse leaf remains installed throughout prepare.  The child
 *   PT is private and prepared-empty; split only prepares child-table
 *   ownership.  Keep ranges and final target ranges are written by their own
 *   remap writers in commit, so prepare failure leaves the live page table
 *   unchanged.
 *
 * Threading:
 *   May allocate and therefore runs before the no-fail commit section.  It
 *   only borrows vmm->tok while checking that the target PD0 slot is still a
 *   sparse 2 MiB leaf.
 */
static int
nvgsp_vmm_prepare_split_sparse_2m(struct nvgsp_vmm *vmm, uint64_t va,
    struct nvgsp_vmm_user_pt **ppt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_user_pt *pt;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;
	int err;

	*ppt = NULL;
	if (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	lwkt_gettoken(&vmm->tok);
	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (existing != NULL) {
		lwkt_reltoken(&vmm->tok);
		return (0);
	}
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_SPARSE_2M) {
		lwkt_reltoken(&vmm->tok);
		return (ENOENT);
	}
	lwkt_reltoken(&vmm->tok);

	pt = kmalloc(sizeof(*pt), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	if (pt == NULL)
		return (ENOMEM);
	pt->pd0 = pd0;
	pt->pd2_idx = pd2_idx;
	pt->pd1_idx = pd1_idx;
	pt->pd0_idx = pd0_idx;

	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->lpt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;
	err = nvgsp_bar_alloc_bar1_page_kind(sc, &pt->spt,
	    NVGSP_VRAM_VMM_PT, pt);
	if (err != 0)
		goto fail;

	nvgsp_vmm_zero_bar1_page(sc, &pt->lpt);
	nvgsp_vmm_zero_bar1_page(sc, &pt->spt);
	*ppt = pt;
	return (0);

fail:
	nvgsp_vmm_abort_split_vram_2m(vmm, pt);
	return (err);
}

/*
 * nvgsp_vmm_commit_split_sparse_2m_locked()
 *
 * Ownership:
 *   Consumes pt on success and links it as the child table for va's old PD0
 *   sparse leaf.  If pt is NULL, the slot must already be materialized.
 *
 * Lifetime:
 *   The old PD0 sparse leaf stops being the hardware owner of this 2 MiB
 *   window.  Lower sparse/invalid writers in the same commit can then project
 *   the kept sparse regions and cut invalid leaves into the child table.  The
 *   split itself does not prefill lower sparse leaves and does not charge lower
 *   LPT/SPT clear counters.
 *
 * Threading:
 *   Caller holds vmm->tok and is in the no-fail commit section.  This helper
 *   only writes the PD0 child slot, links the prepared PT, and flushes BAR1 so
 *   following lower-leaf writes can use the child tables immediately.
 */
static int
nvgsp_vmm_commit_split_sparse_2m_locked(struct nvgsp_vmm *vmm,
    uint64_t va, struct nvgsp_vmm_user_pt *pt)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;

	if (va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);

	pd2_idx = (uint32_t)((va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (existing != NULL)
		return (pt == NULL ? 0 : EEXIST);
	if (pt == NULL || pt->pd2_idx != pd2_idx || pt->pd1_idx != pd1_idx ||
	    pt->pd0_idx != pd0_idx)
		return (EINVAL);
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL || pd0 != pt->pd0 ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_SPARSE_2M)
		return (EIO);

	nvgsp_vmm_pd0_write_child_slot(sc, pd0, pd0_idx,
	    nvgsp_pde_to_vram(pt->lpt.vram_paddr),
	    nvgsp_pde_to_vram(pt->spt.vram_paddr));
	nvgsp_bar_flush_bar1(sc);
	LIST_INSERT_HEAD(&vmm->user_pt_pages, pt, link);
	nvgsp_vmm_user_pt_lookup_insert(vmm, pt);
	return (0);
}

/*
 * nvgsp_vmm_sparse_unmap_count_locked()
 *
 * Ownership:
 *   Borrows sparse-region records and validates which records overlap the
 *   target range.  It does not allocate or mutate the VMM.
 *
 * Lifetime:
 *   *pcount is a scalar count used to size the caller-owned prepare plan.
 *
 * Threading:
 *   Caller holds vmm->tok.  The caller must also hold the higher-level VM
 *   remap serialization so the sparse-region list stays stable between count,
 *   build, and commit.
 */
static int
nvgsp_vmm_sparse_unmap_count_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t end, uint32_t *pcount)
{
	struct nvgsp_vmm_sparse_region *region;
	uint32_t count = 0;

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		uint64_t region_end, cut_start, cut_end, page_size;
		int err;

		if (region->size == 0 ||
		    region->addr > UINT64_MAX - region->size)
			return (EINVAL);
		region_end = region->addr + region->size;
		if (region_end <= va || region->addr >= end)
			continue;

		err = nvgsp_vmm_sparse_region_page_size(
		    region->page_shift, &page_size);
		if (err != 0)
			return (err);
		if (((region->addr | region->size) & (page_size - 1)) != 0)
			return (EINVAL);

		cut_start = region->addr > va ? region->addr : va;
		cut_end = region_end < end ? region_end : end;
		if (cut_start >= cut_end)
			continue;
		if (count == UINT32_MAX)
			return (ENOMEM);
		count++;
	}
	if (count == 0)
		return (ENOENT);
	*pcount = count;
	return (0);
}

/*
 * nvgsp_vmm_sparse_unmap_build_locked()
 *
 * Ownership:
 *   Fills caller-owned entries with borrowed old-region pointers and scalar
 *   cut ranges.  Keep/clear/split lists are initialized but not populated; the
 *   later prepare step owns all allocations.
 *
 * Lifetime:
 *   Borrowed old-region pointers remain valid until commit because the caller
 *   holds VM remap serialization.  Keep-region ownership is attached by the
 *   later allocation step.
 *
 * Threading:
 *   Caller holds vmm->tok.
 */
static int
nvgsp_vmm_sparse_unmap_build_locked(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t end, struct nvgsp_vmm_sparse_unmap_entry *entries,
    uint32_t entry_count)
{
	struct nvgsp_vmm_sparse_region *region;
	uint32_t idx = 0;

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		struct nvgsp_vmm_sparse_unmap_entry *entry;
		uint64_t region_end, cut_start, cut_end, page_size;
		int err;

		region_end = region->addr + region->size;
		if (region_end <= va || region->addr >= end)
			continue;
		if (idx >= entry_count)
			return (EIO);

		err = nvgsp_vmm_sparse_region_page_size(
		    region->page_shift, &page_size);
		if (err != 0)
			return (err);
		cut_start = region->addr > va ? region->addr : va;
		cut_end = region_end < end ? region_end : end;
		if (cut_start >= cut_end)
			return (EIO);

		entry = &entries[idx++];
		entry->old_region = region;
		entry->cut_addr = cut_start;
		entry->cut_size = cut_end - cut_start;
		entry->page_shift = region->page_shift;
		LIST_INIT(&entry->keep_regions);
		LIST_INIT(&entry->clear_ranges);
		LIST_INIT(&entry->split_pts);
	}
	return (idx == entry_count ? 0 : EIO);
}

/*
 * nvgsp_vmm_sparse_unmap_entries_fini()
 *
 * Ownership:
 *   Releases any prepared keep regions, clear nodes, and split PTs that were
 *   not consumed by a successful commit.  Old linked sparse regions are never
 *   freed here.
 *
 * Lifetime:
 *   Called on abort or after commit; consumed region/PT pointers are NULL.
 *
 * Threading:
 *   No VMM token is required because only unlinked prepared regions are freed.
 */
static void
nvgsp_vmm_sparse_unmap_entries_fini(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_unmap_entry *entries, uint32_t entry_count)
{
	for (uint32_t i = 0; i < entry_count; i++) {
		struct nvgsp_vmm_sparse_unmap_keep *keep;
		struct nvgsp_vmm_sparse_unmap_clear *clear;
		struct nvgsp_vmm_sparse_unmap_split *split;

		while ((keep = LIST_FIRST(&entries[i].keep_regions)) != NULL) {
			LIST_REMOVE(keep, link);
			nvgsp_vmm_abort_sparse_region(vmm, keep->region);
			_kfree(keep, M_NVGSP_VMM);
		}
		while ((clear = LIST_FIRST(&entries[i].clear_ranges)) != NULL) {
			LIST_REMOVE(clear, link);
			_kfree(clear, M_NVGSP_VMM);
		}
		while ((split = LIST_FIRST(&entries[i].split_pts)) != NULL) {
			LIST_REMOVE(split, link);
			nvgsp_vmm_abort_split_vram_2m(vmm, split->pt);
			_kfree(split, M_NVGSP_VMM);
		}
	}
}

/*
 * nvgsp_vmm_sparse_unmap_entries_prepare()
 *
 * Ownership:
 *   Allocates unlinked keep-region records, clear nodes, and any child PTs
 *   needed to materialize old PD0 sparse leaves into lower sparse leaves.
 *   Ownership stays in entries until commit links/consumes them or fini aborts
 *   them.
 *
 * Lifetime:
 *   Prepared keep records mirror old sparse leaves outside the cut range.  If
 *   their page_shift is smaller than the old region's page_shift, commit will
 *   write matching lower sparse PTEs before publishing the new region record.
 *   clear_page_shift caps the cut range so a following lower-page valid or
 *   sparse writer can consume prepared child PT storage in the same remap plan.
 *
 * Threading:
 *   May sleep while allocating.  It deliberately runs before commit mutates the
 *   sparse-region list or writes invalid PTEs.
 */
static int
nvgsp_vmm_sparse_unmap_entries_prepare(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_unmap_entry *entries, uint32_t entry_count,
    uint8_t clear_page_shift, int preserve_target_pts,
    int prepare_target_storage, int write_target_pte)
{
	int err;

	for (uint32_t i = 0; i < entry_count; i++) {
		struct nvgsp_vmm_sparse_unmap_entry *entry = &entries[i];
		struct nvgsp_vmm_sparse_unmap_split *split;
		uint64_t old_end, head_size, tail_addr, tail_size;

		old_end = entry->old_region->addr + entry->old_region->size;
		head_size = entry->cut_addr - entry->old_region->addr;
		tail_addr = entry->cut_addr + entry->cut_size;
		tail_size = old_end - tail_addr;
		if (head_size != 0) {
			err = nvgsp_vmm_sparse_unmap_append_segments(entry,
			    entry->old_region->addr, head_size, 1,
			    entry->page_shift, 0, 0, 1);
			if (err != 0)
				return (err);
		}
		err = nvgsp_vmm_sparse_unmap_append_segments(entry,
		    entry->cut_addr, entry->cut_size, 0, clear_page_shift,
		    preserve_target_pts, prepare_target_storage, write_target_pte);
		if (err != 0)
			return (err);
		if (tail_size != 0) {
			err = nvgsp_vmm_sparse_unmap_append_segments(entry,
			    tail_addr, tail_size, 1, entry->page_shift, 0, 0, 1);
			if (err != 0)
				return (err);
		}
		LIST_FOREACH(split, &entry->split_pts, link) {
			err = nvgsp_vmm_prepare_split_sparse_2m(vmm,
			    split->va, &split->pt);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

static int
nvgsp_vmm_sparse_unmap_old_linked_locked(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_sparse_unmap_entry *entry)
{
	struct nvgsp_vmm_sparse_region *region;
	uint64_t cut_end;

	LIST_FOREACH(region, &vmm->sparse_regions, link) {
		if (region == entry->old_region) {
			uint64_t page_size, region_end;
			int err;

			if (region->size == 0 ||
			    region->addr > UINT64_MAX - region->size ||
			    entry->cut_size == 0 ||
			    entry->cut_addr > UINT64_MAX - entry->cut_size)
				return (EIO);
			err = nvgsp_vmm_sparse_region_page_size(
			    region->page_shift, &page_size);
			if (err != 0)
				return (err);
			region_end = region->addr + region->size;
			cut_end = entry->cut_addr + entry->cut_size;
			if (region->page_shift != entry->page_shift ||
			    ((region->addr | region->size) &
			    (page_size - 1)) != 0 ||
			    entry->cut_addr < region->addr ||
			    cut_end > region_end)
				return (EIO);
			return (0);
		}
	}
	return (ENOENT);
}

static const struct nvgsp_vmm_sparse_unmap_split *
nvgsp_vmm_sparse_unmap_find_split(
    const struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t va)
{
	const struct nvgsp_vmm_sparse_unmap_split *split;
	uint64_t base = nvgsp_vmm_sparse_unmap_round_down(va,
	    1ULL << NVGSP_GMMU_PD0_SHIFT);

	LIST_FOREACH(split, &entry->split_pts, link) {
		if (split->va == base)
			return (split);
	}
	return (NULL);
}

static int
nvgsp_vmm_sparse_unmap_split_preflight_locked(
    struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_sparse_unmap_split *split)
{
	struct nvgsp_vmm_user_pt *existing;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx, pd1_idx, pd0_idx;

	if (split->va & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
		return (EINVAL);
	pd2_idx = (uint32_t)((split->va >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((split->va >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((split->va >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	existing = nvgsp_vmm_user_pt_find(vmm, pd2_idx, pd1_idx,
	    pd0_idx);
	if (split->pt == NULL)
		return (existing != NULL ? 0 : ENOENT);
	if (existing != NULL)
		return (EEXIST);
	if (split->pt->pd2_idx != pd2_idx ||
	    split->pt->pd1_idx != pd1_idx ||
	    split->pt->pd0_idx != pd0_idx)
		return (EINVAL);
	pd0 = nvgsp_vmm_pd0_find(vmm, pd2_idx, pd1_idx);
	if (pd0 == NULL || pd0 != split->pt->pd0 ||
	    pd0->slot_state[pd0_idx] != NVGSP_VMM_PD0_SLOT_SPARSE_2M)
		return (EIO);
	return (0);
}

static int
nvgsp_vmm_sparse_unmap_check_lower_prepared_locked(
    struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_sparse_unmap_entry *entry, uint64_t va,
    uint64_t size, uint8_t page_shift)
{
	uint64_t cur, end;

	if (entry->page_shift == NVGSP_GMMU_PD0_SHIFT &&
	    page_shift < NVGSP_GMMU_PD0_SHIFT) {
		end = va + size;
		for (cur = nvgsp_vmm_sparse_unmap_round_down(va,
		    1ULL << NVGSP_GMMU_PD0_SHIFT); cur < end;
		    cur += 1ULL << NVGSP_GMMU_PD0_SHIFT) {
			if (nvgsp_vmm_sparse_unmap_find_split(entry,
			    cur) == NULL)
				return (ENOENT);
		}
		return (0);
	}
	if (page_shift == NVGSP_GMMU_PD0_SHIFT) {
		if ((va | size) & ((1ULL << NVGSP_GMMU_PD0_SHIFT) - 1))
			return (EINVAL);
		return (0);
	}
	return (nvgsp_vmm_check_prepared_pt_range_locked(vmm, va,
	    size, page_shift));
}

/*
 * nvgsp_vmm_sparse_unmap_entries_preflight_locked()
 *
 * Ownership:
 *   Borrows a fully prepared sparse-unmap plan and validates every live
 *   sparse-region pointer, planned PD0 split, and lower-table writer target
 *   before commit mutates the sparse list or page tables.
 *
 * Lifetime:
 *   The check is valid while VM remap serialization is held until commit.
 *   It intentionally mirrors commit's plan consumption so an invariant break
 *   is reported before the first live region is removed.
 *
 * Threading:
 *   Caller holds vmm->tok.  This helper is read-only and performs no
 *   allocation, BAR1 writes, sparse-list mutation, or GPU waits.
 */
static int
nvgsp_vmm_sparse_unmap_entries_preflight_locked(
    struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_sparse_unmap_entry *entries,
    uint32_t entry_count)
{
	for (uint32_t i = 0; i < entry_count; i++) {
		const struct nvgsp_vmm_sparse_unmap_entry *entry =
		    &entries[i];
		const struct nvgsp_vmm_sparse_unmap_clear *clear;
		const struct nvgsp_vmm_sparse_unmap_keep *keep;
		const struct nvgsp_vmm_sparse_unmap_split *split;
		int err;

		if (entry->old_region == NULL)
			return (EIO);
		err = nvgsp_vmm_sparse_unmap_old_linked_locked(vmm,
		    entry);
		if (err != 0)
			return (err);
		LIST_FOREACH(split, &entry->split_pts, link) {
			err = nvgsp_vmm_sparse_unmap_split_preflight_locked(
			    vmm, split);
			if (err != 0)
				return (err);
		}
		LIST_FOREACH(clear, &entry->clear_ranges, link) {
			err =
			    nvgsp_vmm_sparse_unmap_check_lower_prepared_locked(
			    vmm, entry, clear->addr, clear->size,
			    clear->page_shift);
			if (err != 0)
				return (err);
		}
		LIST_FOREACH(keep, &entry->keep_regions, link) {
			if (!keep->write_pte)
				continue;
			err =
			    nvgsp_vmm_sparse_unmap_check_lower_prepared_locked(
			    vmm, entry, keep->region->addr,
			    keep->region->size, keep->region->page_shift);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

/*
 * nvgsp_vmm_check_unmap_sparse_range_prepared()
 *
 * Ownership:
 *   Borrows vmm, a caller-owned sparse-unmap plan, and the scalar target
 *   range.  It does not consume the plan, mutate sparse-region ownership, or
 *   write PTE/PDE state.
 *
 * Lifetime:
 *   The result is valid only while VM remap serialization keeps the live
 *   sparse-region tree paired with the prepared plan.  A successful result
 *   proves that committing the plan will clear the requested target range and
 *   provide the lower-table storage needed by a following prepared writer.
 *
 * Threading:
 *   Acquires vmm->tok for a read-only plan/live-state validation.  No memory
 *   allocation, BAR1 write, sparse-list mutation, BO lookup, or GPU wait occurs.
 */
int
nvgsp_vmm_check_unmap_sparse_range_prepared(struct nvgsp_vmm *vmm,
    const struct nvgsp_vmm_sparse_unmap_plan *plan, uint64_t va,
    uint64_t size, uint8_t page_shift)
{
	uint64_t end, plan_end, page_size, cur;
	int err;

	if (plan == NULL)
		return (ENOENT);
	if (size == 0 || va > UINT64_MAX - size)
		return (EINVAL);
	if (plan->size == 0 || plan->addr > UINT64_MAX - plan->size)
		return (EINVAL);
	err = nvgsp_vmm_sparse_region_page_size(page_shift, &page_size);
	if (err != 0)
		return (err);
	if (((va | size) & (page_size - 1)) != 0)
		return (EINVAL);

	end = va + size;
	plan_end = plan->addr + plan->size;
	if (va < plan->addr || end > plan_end)
		return (ENOENT);

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_sparse_unmap_entries_preflight_locked(vmm,
	    plan->entries, plan->entry_count);
	if (err != 0)
		goto out;

	cur = va;
	while (cur < end) {
		const struct nvgsp_vmm_sparse_unmap_clear *clear;
		const struct nvgsp_vmm_sparse_unmap_entry *entry;
		uint64_t next = cur;

		for (uint32_t i = 0; i < plan->entry_count; i++) {
			entry = &plan->entries[i];
			LIST_FOREACH(clear, &entry->clear_ranges, link) {
				uint64_t clear_end;

				if (clear->addr > UINT64_MAX - clear->size) {
					err = EINVAL;
					goto out;
				}
				clear_end = clear->addr + clear->size;
				if (clear->addr > cur || clear_end <= cur)
					continue;
				if (clear->page_shift > page_shift)
					continue;
				if (clear_end > next)
					next = clear_end;
			}
		}
		if (next == cur) {
			err = ENOENT;
			goto out;
		}
		cur = next < end ? next : end;
	}

out:
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_sparse_unmap_entries_commit_locked()
 *
 * Ownership:
 *   Consumes old sparse-region records covered by the target range, prepared
 *   split PTs, clear ranges, and prepared keep regions.  On success old
 *   records are freed and keep regions are linked into vmm.
 *
 * Lifetime:
 *   Invalid PTE/PDE writes become visible only after the caller's final VMM
 *   flush.  The sparse-region tree is kept semantically aligned with those
 *   writes throughout the no-fail commit.
 *
 * Threading:
 *   Caller holds vmm->tok.  The function performs no allocation, user lookup,
 *   BO pinning, or GPU waits.
 */
static int
nvgsp_vmm_sparse_unmap_entries_commit_locked(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_unmap_entry *entries, uint32_t entry_count,
    enum nvgsp_vmm_clear_purpose purpose)
{
	int err;

	err = nvgsp_vmm_sparse_unmap_entries_preflight_locked(vmm,
	    entries, entry_count);
	if (err != 0)
		return (err);

	for (uint32_t i = 0; i < entry_count; i++) {
		struct nvgsp_vmm_sparse_unmap_entry *entry = &entries[i];
		struct nvgsp_vmm_sparse_region *old = entry->old_region;
		struct nvgsp_vmm_sparse_unmap_clear *clear;
		struct nvgsp_vmm_sparse_unmap_keep *keep;
		struct nvgsp_vmm_sparse_unmap_split *split;

		LIST_FOREACH(split, &entry->split_pts, link) {
			err = nvgsp_vmm_commit_split_sparse_2m_locked(vmm,
			    split->va, split->pt);
			if (err != 0)
				return (err);
			split->pt = NULL;
		}
		LIST_FOREACH(clear, &entry->clear_ranges, link) {
			if (!clear->write_pte)
				continue;
			err =
			    nvgsp_vmm_write_invalid_prepared_locked_purpose(
			    vmm, clear->addr, clear->size, clear->page_shift,
			    clear->preserve_target_pts, purpose);
			if (err != 0)
				return (err);
		}
		LIST_FOREACH(keep, &entry->keep_regions, link) {
			if (!keep->write_pte)
				continue;
			err = nvgsp_vmm_write_sparse_prepared(vmm,
			    keep->region->addr, keep->region->size,
			    keep->region->page_shift);
			if (err != 0)
				return (err);
		}

		LIST_REMOVE(old, link);
		LIST_FOREACH(keep, &entry->keep_regions, link) {
			LIST_INSERT_HEAD(&vmm->sparse_regions,
			    keep->region, link);
			nvgsp_vmm_sparse_regions_merge_locked(vmm,
			    keep->region);
			keep->region = NULL;
		}
		_kfree(old, M_NVGSP_VMM);
		entry->old_region = NULL;
	}
	return (0);
}

/*
 * nvgsp_vmm_prepare_unmap_sparse_range_page()
 *
 * Ownership:
 *   Allocates a caller-owned sparse-unmap plan for every linked sparse-region
 *   record overlapped by [va, va+size).  The plan owns prepared keep-region
 *   records and any child PTs needed to materialize old PD0 sparse leaves.  It
 *   does not mutate live sparse-region ownership or hardware PTEs.
 *
 * Lifetime:
 *   The caller must keep VM remap serialization until commit or fini because
 *   the plan borrows old linked sparse-region pointers.  A range with no
 *   sparse regions returns success with *pplan == NULL.  clear_page_shift caps
 *   the page size used for the cut range; this lets a later lower-page target
 *   writer rely on this plan for child PT storage even when the old sparse
 *   region is fully covered.  preserve_target_pts keeps lower target storage
 *   for a following prepared valid/sparse writer instead of reclaiming it as
 *   part of the sparse clear.
 *
 * Threading:
 *   May allocate and may briefly take vmm->tok for count/build checks.  Use it
 *   before any no-fail VM_BIND commit step that writes PTEs or splices mapping
 *   state.
 */
static int
nvgsp_vmm_prepare_unmap_sparse_range_page_ex(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t clear_page_shift,
    int preserve_target_pts, int prepare_target_storage, int write_target_pte,
    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	struct nvgsp_vmm_sparse_unmap_plan *plan;
	struct nvgsp_vmm_sparse_unmap_entry *entries;
	uint64_t end;
	uint64_t page_size;
	uint32_t entry_count = 0;
	int err;

	*pplan = NULL;
	err = nvgsp_vmm_sparse_region_page_size(clear_page_shift,
	    &page_size);
	if (err != 0)
		return (err);
	if ((va | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1))
		return (EINVAL);
	if (size == 0 || va > UINT64_MAX - size)
		return (EINVAL);
	end = va + size;

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_sparse_unmap_count_locked(vmm, va, end,
	    &entry_count);
	lwkt_reltoken(&vmm->tok);
	if (err == ENOENT)
		return (0);
	if (err != 0)
		return (err);

	if (entry_count > SIZE_MAX / sizeof(*entries))
		return (ENOMEM);
	plan = kmalloc(sizeof(*plan), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	if (plan == NULL)
		return (ENOMEM);
	entries = kmalloc(entry_count * sizeof(*entries), M_NVGSP_VMM,
	    M_WAITOK | M_ZERO);
	if (entries == NULL) {
		_kfree(plan, M_NVGSP_VMM);
		return (ENOMEM);
	}
	for (uint32_t i = 0; i < entry_count; i++) {
		LIST_INIT(&entries[i].keep_regions);
		LIST_INIT(&entries[i].clear_ranges);
		LIST_INIT(&entries[i].split_pts);
	}

	lwkt_gettoken(&vmm->tok);
	err = nvgsp_vmm_sparse_unmap_build_locked(vmm, va, end, entries,
	    entry_count);
	lwkt_reltoken(&vmm->tok);
	if (err != 0)
		goto out;

	err = nvgsp_vmm_sparse_unmap_entries_prepare(vmm, entries,
	    entry_count, clear_page_shift, preserve_target_pts,
	    prepare_target_storage, write_target_pte);
	if (err != 0)
		goto out;

	plan->entries = entries;
	plan->entry_count = entry_count;
	plan->addr = va;
	plan->size = size;
	*pplan = plan;
	return (0);

out:
	nvgsp_vmm_sparse_unmap_entries_fini(vmm, entries, entry_count);
	_kfree(entries, M_NVGSP_VMM);
	_kfree(plan, M_NVGSP_VMM);
	return (err);
}

int
nvgsp_vmm_prepare_unmap_sparse_range_page(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t clear_page_shift,
    int preserve_target_pts,
    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	return (nvgsp_vmm_prepare_unmap_sparse_range_page_ex(vmm, va, size,
	    clear_page_shift, preserve_target_pts, preserve_target_pts, 1,
	    pplan));
}

/*
 * nvgsp_vmm_prepare_overwrite_sparse_range_page()
 *
 * Ownership:
 *   Prepares removal of sparse-region metadata that will be covered by a final
 *   valid/sparse target writer.  The returned plan owns sparse keep records and
 *   any child PT storage needed by that following writer.
 *
 * Lifetime:
 *   Commit removes old sparse metadata and may split a PD0 sparse parent to
 *   prepared-empty child storage, but it does not write invalid PTEs for the
 *   target-middle W range.  The final target writer owns those PTE/PDE writes.
 *
 * Threading:
 *   May allocate and must run before the serialized no-fail VM_BIND commit
 *   section.  It performs no GEM lookup, BO pinning, or GPU wait.
 */
int
nvgsp_vmm_prepare_overwrite_sparse_range_page(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size, uint8_t clear_page_shift,
    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	return (nvgsp_vmm_prepare_unmap_sparse_range_page_ex(vmm, va, size,
	    clear_page_shift, 1, 1, 0, pplan));
}

/*
 * nvgsp_vmm_prepare_metadata_sparse_range()
 *
 * Ownership:
 *   Prepares removal of sparse-region records only.  The plan owns replacement
 *   sparse keep records but does not prepare target PTE writes or child storage.
 *
 * Lifetime:
 *   This mode is for exact valid-map no-op cleanup where hardware already holds
 *   the final valid PTEs and only stale sparse metadata must be retired.  Commit
 *   therefore must not dirty page tables or require a VMM invalidate.
 *
 * Threading:
 *   May allocate sparse keep records during prepare; commit is deterministic and
 *   performs no allocation, GEM lookup, BO pinning, or GPU wait.
 */
int
nvgsp_vmm_prepare_metadata_sparse_range(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size,
    struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	return (nvgsp_vmm_prepare_unmap_sparse_range_page_ex(vmm, va, size,
	    NVGSP_GMMU_PD0_SHIFT, 0, 0, 0, pplan));
}

int
nvgsp_vmm_prepare_unmap_sparse_range(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size, struct nvgsp_vmm_sparse_unmap_plan **pplan)
{
	return (nvgsp_vmm_prepare_unmap_sparse_range_page(vmm, va, size,
	    NVGSP_GMMU_PD0_SHIFT, 0, pplan));
}

/*
 * nvgsp_vmm_commit_unmap_sparse_range_noflush()
 *
 * Ownership:
 *   Consumes the live sparse-region records described by plan and publishes
 *   prepared keep-regions.  The caller still owns plan storage and must call
 *   fini_unmap_sparse_range() after commit returns.
 *
 * Lifetime:
 *   Hardware visibility of invalid/sparse writes belongs to the caller's final
 *   VMM flush/TLB invalidate.  Passing NULL is a no-op prepared by a sparse
 *   range that had no linked regions.
 *
 * Threading:
 *   Takes vmm->tok and performs no allocation, GEM lookup, BO pinning, or GPU
 *   waits.  This is the no-fail commit half for DRM VM_BIND sparse replace.
 */
int
nvgsp_vmm_commit_unmap_sparse_range_noflush(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_unmap_plan *plan)
{
	int err;

	if (plan == NULL)
		return (0);

	lwkt_gettoken(&vmm->tok);
	plan->wrote_hw = 0;
	err = nvgsp_vmm_sparse_unmap_entries_commit_locked(vmm,
	    plan->entries, plan->entry_count,
	    NVGSP_VMM_CLEAR_FINAL_INVALID);
	if (err == 0)
		plan->wrote_hw = nvgsp_vmm_sparse_unmap_entries_have_hw_writes(
		    plan->entries, plan->entry_count);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_commit_unmap_sparse_range_conflict_noflush()
 *
 * Ownership:
 *   Consumes the same prepared sparse-unmap plan shape as the final-unmap
 *   wrapper, but classifies its PTE/PDE invalid writes as conflict resolution
 *   for a following valid/sparse target writer.
 *
 * Lifetime:
 *   The sparse clear and following target install are published by the
 *   caller's enclosing remap flush/invalidate.  The plan storage remains owned
 *   by the caller and must still be released with fini_unmap_sparse_range().
 *
 * Threading:
 *   Takes vmm->tok and performs no allocation or GPU waits.
 */
int
nvgsp_vmm_commit_unmap_sparse_range_conflict_noflush(
    struct nvgsp_vmm *vmm, struct nvgsp_vmm_sparse_unmap_plan *plan)
{
	int err;

	if (plan == NULL)
		return (0);

	lwkt_gettoken(&vmm->tok);
	plan->wrote_hw = 0;
	err = nvgsp_vmm_sparse_unmap_entries_commit_locked(vmm,
	    plan->entries, plan->entry_count, NVGSP_VMM_CLEAR_CONFLICT);
	if (err == 0)
		plan->wrote_hw = nvgsp_vmm_sparse_unmap_entries_have_hw_writes(
		    plan->entries, plan->entry_count);
	lwkt_reltoken(&vmm->tok);
	return (err);
}

/*
 * nvgsp_vmm_fini_unmap_sparse_range()
 *
 * Ownership:
 *   Releases a sparse-unmap plan created by prepare_unmap_sparse_range().
 *   Unconsumed keep-regions and split PTs are aborted; already committed
 *   entries have NULL ownership fields and are skipped.
 *
 * Lifetime:
 *   Safe after either prepare failure cleanup by the caller, successful commit,
 *   or abort before commit.  Passing NULL is allowed.
 *
 * Threading:
 *   Does not require vmm->tok because only unlinked prepared objects remain in
 *   the plan.  It never mutates live sparse-region lists.
 */
void
nvgsp_vmm_fini_unmap_sparse_range(struct nvgsp_vmm *vmm,
    struct nvgsp_vmm_sparse_unmap_plan *plan)
{
	if (plan == NULL)
		return;
	nvgsp_vmm_sparse_unmap_entries_fini(vmm, plan->entries,
	    plan->entry_count);
	_kfree(plan->entries, M_NVGSP_VMM);
	_kfree(plan, M_NVGSP_VMM);
}

/*
 * nvgsp_vmm_unmap_sparse_range_noflush()
 *
 * Ownership:
 *   Compatibility wrapper that prepares, commits, and releases one sparse
 *   range in a single call.  New VM_BIND paths should call the split
 *   prepare/commit API so allocation failures happen before other PTE writes.
 *
 * Lifetime:
 *   Returns ENOENT when no sparse region overlaps the range, preserving the
 *   historical helper contract for callers that distinguish no-op clears.
 *
 * Threading:
 *   May allocate during prepare and takes vmm->tok during commit.  It performs
 *   no final VMM flush; callers own the enclosing invalidate boundary.
 */
int
nvgsp_vmm_unmap_sparse_range_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t size)
{
	struct nvgsp_vmm_sparse_unmap_plan *plan;
	int err;

	err = nvgsp_vmm_prepare_unmap_sparse_range(vmm, va, size, &plan);
	if (err != 0)
		return (err);
	if (plan == NULL)
		return (ENOENT);
	err = nvgsp_vmm_commit_unmap_sparse_range_noflush(vmm, plan);
	nvgsp_vmm_fini_unmap_sparse_range(vmm, plan);
	return (err);
}

int
nvgsp_vmm_unmap_sparse(struct nvgsp_vmm *vmm, uint64_t va, uint64_t size)
{
	int err = nvgsp_vmm_unmap_sparse_range_noflush(vmm, va, size);

	if (err == 0)
		nvgsp_vmm_flush(vmm);
	return (err);
}


void
nvgsp_vmm_snapshot(struct nvgsp_vmm *vmm, uint32_t *pd0_count,
    uint32_t *pt_count, uint64_t *valid_pte_count,
    uint32_t *sparse_region_count)
{
	struct nvgsp_vmm_pd0 *pd0;
	struct nvgsp_vmm_user_pt *pt;
	struct nvgsp_vmm_sparse_region *region;

	*pd0_count = 0;
	*pt_count = 0;
	*valid_pte_count = 0;
	*sparse_region_count = 0;
	if (vmm == NULL)
		return;

	lwkt_gettoken(&vmm->tok);
	LIST_FOREACH(pd0, &vmm->user_pd0_pages, link)
		(*pd0_count)++;
	LIST_FOREACH(pt, &vmm->user_pt_pages, link) {
		(*pt_count)++;
		*valid_pte_count += pt->valid_pte_count +
		    (uint64_t)pt->valid_lpte_count *
		    NVGSP_GMMU_LPT_SPTE_COUNT;
	}
	LIST_FOREACH(region, &vmm->sparse_regions, link)
		(*sparse_region_count)++;
	lwkt_reltoken(&vmm->tok);
}

static int
nvgsp_vmm_copy_pdes(struct nvgsp_vmm *vmm)
{
	struct nvgsp_state *sc = vmm->gsp;
	struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS *ctrl;
	void *p;
	int err;

	ctrl = nvgsp_rm_get_ctrl(&vmm->vaspace,
	    NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES,
	    sizeof(*ctrl));
	if (ctrl == NULL)
		return (ENOMEM);

	ctrl->hSubDevice = 0;
	ctrl->subDeviceId = 0;
	ctrl->pageSize = 1ULL << 29;	/* 512 MiB — the leaf for PD1 */
	ctrl->virtAddrLo = vmm->rm_va_base;
	ctrl->virtAddrHi = vmm->rm_va_base + vmm->rm_va_size - 1;
	ctrl->numLevelsToCopy = 3;

	/* PD3 — root. Holds 4 entries of 8 bytes each (2-bit index). */
	ctrl->levels[0].physAddress = (uint64_t)vmm->pt[0].page.vram_paddr;
	ctrl->levels[0].size        = (1ULL << 2) * 8;	/* 32 bytes used */
	ctrl->levels[0].aperture    = NV_COPY_PDE_APERTURE_VIDMEM;
	ctrl->levels[0].pageShift   = 47;

	/* PD2 — 512 entries × 8 bytes = 4 KiB. */
	ctrl->levels[1].physAddress = (uint64_t)vmm->pt[1].page.vram_paddr;
	ctrl->levels[1].size        = (1ULL << 9) * 8;	/* 4096 */
	ctrl->levels[1].aperture    = NV_COPY_PDE_APERTURE_VIDMEM;
	ctrl->levels[1].pageShift   = 38;

	/* PD1 — 512 entries × 8 bytes = 4 KiB. */
	ctrl->levels[2].physAddress = (uint64_t)vmm->pt[2].page.vram_paddr;
	ctrl->levels[2].size        = (1ULL << 9) * 8;	/* 4096 */
	ctrl->levels[2].aperture    = NV_COPY_PDE_APERTURE_VIDMEM;
	ctrl->levels[2].pageShift   = 29;

	p = ctrl;
	err = nvgsp_rm_read_ctrl(&vmm->vaspace, &p, 0);
	if (err != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gsp_rm: VASPACE_COPY_SERVER_RESERVED_PDES failed err=%d\n",
		    err);
		return (err);
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp_rm: COPY_SERVER_RESERVED_PDES ok (va=0x%llx+0x%llx, "
	    "PD3=0x%llx PD2=0x%llx PD1=0x%llx)\n",
	    (unsigned long long)vmm->rm_va_base,
	    (unsigned long long)vmm->rm_va_size,
	    (unsigned long long)vmm->pt[0].page.vram_paddr,
	    (unsigned long long)vmm->pt[1].page.vram_paddr,
	    (unsigned long long)vmm->pt[2].page.vram_paddr);
	return (0);
}

int
nvgsp_vmm_ctor(struct nvgsp_state *sc, uint32_t client_handle,
    struct nvgsp_vmm *vmm, bool external)
{
	int err, i;

	memset(vmm, 0, sizeof(*vmm));
	vmm->gsp = sc;
	vmm->rm_va_base = NVGSP_VMM_RM_BASE;
	vmm->rm_va_size = NVGSP_VMM_RM_SIZE;
	lwkt_token_init(&vmm->tok, "nvgsp-vmm");
	LIST_INIT(&vmm->user_pd1_pages);
	LIST_INIT(&vmm->user_pd0_pages);
	LIST_INIT(&vmm->user_pt_pages);
	LIST_INIT(&vmm->sparse_regions);
	for (i = 0; i < NVGSP_VMM_PD0_HASH_SIZE; i++)
		LIST_INIT(&vmm->user_pd0_lookup[i]);
	for (i = 0; i < NVGSP_VMM_USER_PT_HASH_SIZE; i++)
		LIST_INIT(&vmm->user_pt_lookup[i]);

	err = nvgsp_dma_alloc_dmamem(sc, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, &vmm->sparse_page);
	if (err != 0)
		return (err);

	/* 1) Client + device + subdevice. */
	err = nvgsp_rm_construct_client(sc, client_handle, &vmm->client);
	if (err != 0)
		goto fail_sparse_page;
	err = nvgsp_rm_construct_device(&vmm->client, &vmm->device);
	if (err != 0)
		goto fail_client;

	/* 2) Three root VRAM PT pages: PD3, PD2, PD1. */
	for (i = 0; i < 3; i++) {
		err = nvgsp_vmm_pt_alloc(sc, &vmm->pt[i]);
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "gsp_rm: PT page %d alloc failed\n", i);
			goto fail_pt;
		}
	}

	/* 3) Link PD3[0] to PD2 and PD2[0] to PD1 through BAR1.
	 * The root pages live in VRAM, so the GMMU PDE aperture is VIDMEM.
	 * RM owns this chain for kernel/golden VMMs; user VMMs publish it as
	 * their externally-owned PDB. */
	nvgsp_bar_wr64_bar1(sc, vmm->pt[0].page.bar1_gva + 0,
	    nvgsp_pde_to_vram(vmm->pt[1].page.vram_paddr));
	nvgsp_bar_wr64_bar1(sc, vmm->pt[1].page.bar1_gva + 0,
	    nvgsp_pde_to_vram(vmm->pt[2].page.vram_paddr));

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp_rm: PT chain (VRAM) PD3=0x%llx@bar1=0x%llx PD2=0x%llx@bar1=0x%llx PD1=0x%llx@bar1=0x%llx\n",
	    (unsigned long long)vmm->pt[0].page.vram_paddr,
	    (unsigned long long)vmm->pt[0].page.bar1_gva,
	    (unsigned long long)vmm->pt[1].page.vram_paddr,
	    (unsigned long long)vmm->pt[1].page.bar1_gva,
	    (unsigned long long)vmm->pt[2].page.vram_paddr,
	    (unsigned long long)vmm->pt[2].page.bar1_gva);

	/* 4) Allocate an RM VAS with either RM or driver-owned page tables. */
	{
		struct nvgsp_vaspace_params *args;

		args = nvgsp_rm_get_alloc(&vmm->device.object,
		    nvgsp_vmm_get_child_handle(&vmm->client,
		    NVGSP_RM_VASPACE), FERMI_VASPACE_A, sizeof(*args),
		    &vmm->vaspace);
		if (args == NULL) {
			err = ENOMEM;
			goto fail_pt;
		}
		args->index = 0;
		args->flags = external ? NVGSP_VASPACE_FLAG_IS_EXTERNALLY_OWNED : 0;
		err = nvgsp_rm_write_alloc(&vmm->vaspace, args);
		if (err != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "gsp_rm: FERMI_VASPACE_A alloc failed err=%d\n",
			    err);
			goto fail_pt;
		}
	}

	/* 5) Tell RM whether it owns the PDB or must use the driver's root. */
	if (external) {
		struct nvgsp_dma_set_page_directory_params *ctrl;

		ctrl = nvgsp_rm_get_ctrl(&vmm->device.object,
		    NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY, sizeof(*ctrl));
		if (ctrl == NULL) {
			err = ENOMEM;
			goto fail_vaspace;
		}
		memset(ctrl, 0, sizeof(*ctrl));
		ctrl->phys_address = vmm->pt[0].page.vram_paddr;
		ctrl->num_entries = NVGSP_GMMU_PD3_ENTRIES;
		ctrl->flags = NVGSP_DMA_SET_PDB_FLAGS_APERTURE_VIDMEM;
		ctrl->h_vaspace = vmm->vaspace.handle;
		err = nvgsp_rm_write_ctrl(&vmm->device.object, ctrl);
		if (err != 0)
			goto fail_vaspace;
		vmm->external_page_directory = 1;
	} else {
		err = nvgsp_vmm_copy_pdes(vmm);
		if (err != 0)
			goto fail_vaspace;
	}

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp_rm: VMM ready (client=0x%x device=0x%x vaspace=0x%x)\n",
	    vmm->client.object.handle, vmm->device.object.handle,
	    vmm->vaspace.handle);

	/* Allocate TURING_USERMODE_A at device level (nouveau allocates this
	 * once at drm init). GSP may gate doorbell delivery on its presence. */
	{
		void *up = nvgsp_rm_get_alloc(&vmm->device.subdevice,
		    nvgsp_vmm_get_child_handle(&vmm->client, NVGSP_RM_USERMODE),
		    TURING_USERMODE_A, 0, &vmm->usermode);
		if (up != NULL) {
			int uerr = nvgsp_rm_write_alloc(&vmm->usermode, up);
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "gsp_rm: TURING_USERMODE_A handle=0x%x err=%d (device-level)\n",
			    vmm->usermode.handle, uerr);
			/* If GSP did not publish the USERMODE class id, seed it before channels use doorbells. */
			if (nvgsp_rd32(sc, 0xbb0000) == 0)
				nvgsp_wr32(sc, 0xbb0000, 0xc461u);
		}
	}

	return (0);

fail_vaspace:
	if (vmm->external_page_directory) {
		struct nvgsp_dma_unset_page_directory_params *ctrl;

		ctrl = nvgsp_rm_get_ctrl(&vmm->device.object,
		    NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY, sizeof(*ctrl));
		if (ctrl != NULL) {
			memset(ctrl, 0, sizeof(*ctrl));
			ctrl->h_vaspace = vmm->vaspace.handle;
			(void)nvgsp_rm_write_ctrl(&vmm->device.object, ctrl);
		}
		vmm->external_page_directory = 0;
	}
	nvgsp_rm_free(&vmm->vaspace);
fail_pt:
	for (i = 0; i < 3; i++)
		nvgsp_vmm_pt_free(vmm->gsp, &vmm->pt[i]);
	nvgsp_rm_destroy_device(&vmm->device);
fail_client:
	nvgsp_rm_destroy_client(&vmm->client);
fail_sparse_page:
	nvgsp_dma_free_dmamem(sc, &vmm->sparse_page);
	return (err);
}

int
nvgsp_vmm_set_kernel_va_range(struct nvgsp_vmm *vmm, uint64_t addr,
    uint64_t size)
{
	if (vmm == NULL || addr == 0 || size == 0 ||
	    addr > UINT64_MAX - size ||
	    ((addr | size) & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	if (vmm->kernel_va_ready) {
		if (vmm->kernel_va_addr == addr && vmm->kernel_va_size == size) {
			lwkt_reltoken(&vmm->tok);
			return (0);
		}
		lwkt_reltoken(&vmm->tok);
		return (EBUSY);
	}
	drm_mm_init(&vmm->kernel_va_mm, addr, size);
	vmm->kernel_va_addr = addr;
	vmm->kernel_va_size = size;
	vmm->kernel_va_ready = 1;
	lwkt_reltoken(&vmm->tok);
	return (0);
}

int
nvgsp_vmm_alloc_kernel_va(struct nvgsp_vmm *vmm, uint64_t size,
    uint64_t alignment, struct drm_mm_node *node)
{
	int error;

	if (vmm == NULL || node == NULL || size == 0 ||
	    (size & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0 ||
	    alignment == 0 ||
	    (alignment & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return (EINVAL);

	lwkt_gettoken(&vmm->tok);
	if (!vmm->kernel_va_ready) {
		lwkt_reltoken(&vmm->tok);
		return (ENXIO);
	}
	error = drm_mm_insert_node_in_range(&vmm->kernel_va_mm, node, size,
	    alignment, 0, vmm->kernel_va_addr,
	    vmm->kernel_va_addr + vmm->kernel_va_size, DRM_MM_INSERT_LOW);
	lwkt_reltoken(&vmm->tok);
	return (error < 0 ? -error : error);
}

void
nvgsp_vmm_free_kernel_va(struct nvgsp_vmm *vmm, struct drm_mm_node *node)
{
	if (vmm == NULL || node == NULL || !drm_mm_node_allocated(node))
		return;
	lwkt_gettoken(&vmm->tok);
	if (drm_mm_node_allocated(node))
		drm_mm_remove_node(node);
	lwkt_reltoken(&vmm->tok);
}

void
nvgsp_vmm_dtor(struct nvgsp_vmm *vmm)
{
	struct nvgsp_vmm_user_pt *pt;
	struct nvgsp_vmm_pd0 *pd0;
	struct nvgsp_vmm_pd1 *pd1;
	struct nvgsp_vmm_sparse_region *region;
	int i;

	if (vmm->external_page_directory) {
		struct nvgsp_dma_unset_page_directory_params *ctrl;

		ctrl = nvgsp_rm_get_ctrl(&vmm->device.object,
		    NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY, sizeof(*ctrl));
		if (ctrl != NULL) {
			memset(ctrl, 0, sizeof(*ctrl));
			ctrl->h_vaspace = vmm->vaspace.handle;
			(void)nvgsp_rm_write_ctrl(&vmm->device.object, ctrl);
		}
		vmm->external_page_directory = 0;
	}
	while ((region = LIST_FIRST(&vmm->sparse_regions)) != NULL) {
		LIST_REMOVE(region, link);
		_kfree(region, M_NVGSP_VMM);
	}
	while ((pt = LIST_FIRST(&vmm->user_pt_pages)) != NULL) {
		LIST_REMOVE(pt, lookup_link);
		LIST_REMOVE(pt, link);
		nvgsp_bar_free_bar1_page(vmm->gsp, &pt->spt);
		nvgsp_bar_free_bar1_page(vmm->gsp, &pt->lpt);
		_kfree(pt, M_NVGSP_VMM);
	}
	while ((pd0 = LIST_FIRST(&vmm->user_pd0_pages)) != NULL) {
		LIST_REMOVE(pd0, lookup_link);
		LIST_REMOVE(pd0, link);
		nvgsp_bar_free_bar1_page(vmm->gsp, &pd0->page);
		_kfree(pd0, M_NVGSP_VMM);
	}
	while ((pd1 = LIST_FIRST(&vmm->user_pd1_pages)) != NULL) {
		LIST_REMOVE(pd1, link);
		nvgsp_bar_free_bar1_page(vmm->gsp, &pd1->page);
		_kfree(pd1, M_NVGSP_VMM);
	}
	if (vmm->kernel_va_ready) {
		KASSERT(drm_mm_clean(&vmm->kernel_va_mm),
		    ("destroying VMM with live kernel VA nodes"));
		drm_mm_takedown(&vmm->kernel_va_mm);
		vmm->kernel_va_ready = 0;
	}
	if (vmm->usermode.handle != 0)
		nvgsp_rm_free(&vmm->usermode);
	if (vmm->vaspace.handle != 0)
		nvgsp_rm_free(&vmm->vaspace);
	for (i = 0; i < 3; i++)
		nvgsp_vmm_pt_free(vmm->gsp, &vmm->pt[i]);
	nvgsp_dma_free_dmamem(vmm->gsp, &vmm->sparse_page);
	nvgsp_rm_destroy_device(&vmm->device);
	nvgsp_rm_destroy_client(&vmm->client);
}

int
nvgsp_vmm_init_kernel(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	int error;

	if (gsp == NULL)
		return (ENXIO);
	if (gsp->kernel_vmm != NULL)
		return (0);
	gsp->kernel_vmm = kmalloc(sizeof(*gsp->kernel_vmm), M_NVGSP_VMM,
	    M_WAITOK | M_ZERO);
	error = nvgsp_vmm_ctor(gsp, 0xc1d00001u, gsp->kernel_vmm, false);
	if (error != 0) {
		_kfree(gsp->kernel_vmm, M_NVGSP_VMM);
		gsp->kernel_vmm = NULL;
	}
	return (error);
}

void
nvgsp_vmm_fini_kernel(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL || gsp->kernel_vmm == NULL)
		return;
	nvgsp_vmm_dtor(gsp->kernel_vmm);
	_kfree(gsp->kernel_vmm, M_NVGSP_VMM);
	gsp->kernel_vmm = NULL;
}

int
nvgsp_vmm_create_golden(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	int error;

	if (gsp == NULL)
		return (ENXIO);
	if (gsp->golden_vmm != NULL)
		return (0);
	gsp->golden_vmm = kmalloc(sizeof(*gsp->golden_vmm), M_NVGSP_VMM,
	    M_WAITOK | M_ZERO);
	error = nvgsp_vmm_ctor(gsp, 0xc1d00002u, gsp->golden_vmm, false);
	if (error != 0) {
		_kfree(gsp->golden_vmm, M_NVGSP_VMM);
		gsp->golden_vmm = NULL;
	}
	return (error);
}

void
nvgsp_vmm_destroy_golden(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL || gsp->golden_vmm == NULL)
		return;
	nvgsp_vmm_dtor(gsp->golden_vmm);
	_kfree(gsp->golden_vmm, M_NVGSP_VMM);
	gsp->golden_vmm = NULL;
}

int
nvgsp_vmm_create_user(struct nvgpu_device *gpu, uint32_t client_handle,
    uint64_t kernel_va_addr, uint64_t kernel_va_size,
    struct nvgsp_vmm **out)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_vmm *vmm;
	bool constructed;
	int error;

	if (gsp == NULL || out == NULL)
		return (EINVAL);
	vmm = kmalloc(sizeof(*vmm), M_NVGSP_VMM, M_WAITOK | M_ZERO);
	constructed = false;
	lwkt_gettoken(&gsp->gsp_tok);
	error = nvgsp_vmm_ctor(gsp, client_handle, vmm, true);
	if (error == 0) {
		constructed = true;
		if (kernel_va_addr != 0 || kernel_va_size != 0)
			error = nvgsp_vmm_set_kernel_va_range(vmm, kernel_va_addr,
			    kernel_va_size);
	}
	if (error != 0 && constructed)
		nvgsp_vmm_dtor(vmm);
	lwkt_reltoken(&gsp->gsp_tok);
	if (error != 0) {
		_kfree(vmm, M_NVGSP_VMM);
		return (error);
	}
	*out = vmm;
	return (0);
}

void
nvgsp_vmm_destroy_user(struct nvgsp_vmm *vmm)
{
	struct nvgsp_state *gsp;

	if (vmm == NULL)
		return;
	gsp = vmm->gsp;
	if (gsp != NULL)
		lwkt_gettoken(&gsp->gsp_tok);
	nvgsp_vmm_dtor(vmm);
	if (gsp != NULL)
		lwkt_reltoken(&gsp->gsp_tok);
	_kfree(vmm, M_NVGSP_VMM);
}

int
nvgsp_vmm_map_submit_pages(struct nvgpu_device *gpu)
{
	(void)gpu;
	return (0);
}
