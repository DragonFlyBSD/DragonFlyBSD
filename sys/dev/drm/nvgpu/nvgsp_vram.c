/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP VRAM allocation boundary for the native NVIDIA GPU driver.
 */

#include "nvgsp_vram.h"
#include "nvgsp_bar.h"
#include "nvgsp_priv.h"

static MALLOC_DEFINE(M_NVGSP_VRAM, "nvgsp_vram", "nvgsp VRAM allocation metadata");

static struct nvgsp_vram_alloc *
nvgsp_vram_record_alloc(uint64_t size, uint64_t align,
    enum nvgsp_vram_kind kind, void *owner)
{
	struct nvgsp_vram_alloc *alloc;

	alloc = kmalloc(sizeof(*alloc), M_NVGSP_VRAM, M_WAITOK | M_ZERO);
	alloc->size = size;
	alloc->align = align;
	alloc->kind = kind;
	alloc->owner = owner;
	return (alloc);
}

static struct nvgsp_vram_alloc *
nvgsp_vram_alloc_with_kind(struct nvgsp_state *gsp, uint64_t size,
    uint64_t align, enum nvgsp_vram_kind kind, void *owner)
{
	struct nvgsp_vram_alloc *alloc;
	int error;

	if (gsp == NULL || gsp->vram_bump_limit == 0)
		return (NULL);
	if (align == 0)
		align = NVGSP_PAGE_SIZE;
	size = NVGSP_ALIGN_UP(size, align);
	alloc = nvgsp_vram_record_alloc(size, align, kind, owner);

	lockmgr(&gsp->vram_lock, LK_EXCLUSIVE);
	error = drm_mm_insert_node_in_range(&gsp->vram_mm, &alloc->node, size,
	    align, 0, gsp->vram_bump_base, gsp->vram_bump_limit,
	    DRM_MM_INSERT_HIGH);
	if (error != 0) {
		lockmgr(&gsp->vram_lock, LK_RELEASE);
		kfree(alloc, M_NVGSP_VRAM);
		return (NULL);
	}
	alloc->paddr = alloc->node.start;
	TAILQ_INSERT_TAIL(&gsp->vram_allocs, alloc, link);
	gsp->vram_alloc_bytes[kind] += alloc->size;
	gsp->vram_alloc_count[kind]++;
	lockmgr(&gsp->vram_lock, LK_RELEASE);
	return (alloc);
}

static void
nvgsp_vram_remove_alloc(struct nvgsp_state *gsp, struct nvgsp_vram_alloc *alloc)
{
	if (gsp == NULL || alloc == NULL)
		return;
	lockmgr(&gsp->vram_lock, LK_EXCLUSIVE);
	TAILQ_REMOVE(&gsp->vram_allocs, alloc, link);
	gsp->vram_alloc_bytes[alloc->kind] -= alloc->size;
	gsp->vram_alloc_count[alloc->kind]--;
	if (drm_mm_node_allocated(&alloc->node))
		drm_mm_remove_node(&alloc->node);
	lockmgr(&gsp->vram_lock, LK_RELEASE);
}

static void
nvgsp_vram_free_alloc(struct nvgsp_state *gsp, struct nvgsp_vram_alloc *alloc)
{
	if (gsp == NULL || alloc == NULL)
		return;
	nvgsp_vram_remove_alloc(gsp, alloc);
	if (alloc->bar1_gva != 0) {
		nvgsp_bar_unmap_bar1_existing_range(gsp, alloc->bar1_gva,
		    alloc->bar1_size);
		alloc->bar1_gva = 0;
	}
	if (alloc->bar1_page_gva != NULL) {
		nvgsp_bar_unmap_bar1_existing_scatter(gsp, alloc->bar1_page_gva,
		    alloc->bar1_page_count);
		kfree(alloc->bar1_page_gva, M_NVGSP_VRAM);
		alloc->bar1_page_gva = NULL;
	}
	kfree(alloc, M_NVGSP_VRAM);
}

int
nvgsp_vram_init(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint64_t base, size;

	if (gsp == NULL)
		return (ENXIO);
	base = gsp->fb_usable_base;
	size = gsp->fb_usable_size;
	if (size == 0)
		return (ENXIO);
	if (size > (64ULL << 20))
		size -= (64ULL << 20);

	gsp->vram_bump_base = base;
	gsp->vram_bump_limit = base + size;
	drm_mm_init(&gsp->vram_mm, base, size);
	lockinit(&gsp->vram_lock, "nvgspvram", 0, 0);
	TAILQ_INIT(&gsp->vram_allocs);
	memset(gsp->vram_alloc_bytes, 0, sizeof(gsp->vram_alloc_bytes));
	memset(gsp->vram_alloc_count, 0, sizeof(gsp->vram_alloc_count));
	lwkt_token_init(&gsp->chid_tok, "nvgsp-chid");
	memset(gsp->chid_used, 0, sizeof(gsp->chid_used));
	gsp->chid_used[0] = 0x3ULL;
	nvgpu_log(NVGPU_LOG_DEBUG, "vram window 0x%llx..0x%llx\n",
	    (unsigned long long)gsp->vram_bump_base,
	    (unsigned long long)gsp->vram_bump_limit);
	return (0);
}

void
nvgsp_vram_fini(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_vram_alloc *alloc;

	if (gsp == NULL || gsp->vram_bump_limit == 0)
		return;
	lockmgr(&gsp->vram_lock, LK_EXCLUSIVE);
	while ((alloc = TAILQ_FIRST(&gsp->vram_allocs)) != NULL) {
		TAILQ_REMOVE(&gsp->vram_allocs, alloc, link);
		if (drm_mm_node_allocated(&alloc->node))
			drm_mm_remove_node(&alloc->node);
		if (alloc->bar1_page_gva != NULL)
			kfree(alloc->bar1_page_gva, M_NVGSP_VRAM);
		kfree(alloc, M_NVGSP_VRAM);
	}
	lockmgr(&gsp->vram_lock, LK_RELEASE);
	drm_mm_takedown(&gsp->vram_mm);
	lockuninit(&gsp->vram_lock);
	gsp->vram_bump_base = 0;
	gsp->vram_bump_limit = 0;
}

struct nvgsp_vram_alloc *
nvgsp_vram_alloc_gem(struct nvgpu_device *gpu, uint64_t size, uint64_t align,
    void *owner)
{
	return (nvgsp_vram_alloc_with_kind(nvgsp_state_get(gpu), size, align,
	    NVGSP_VRAM_GEM, owner));
}

void
nvgsp_vram_free_gem(struct nvgpu_device *gpu, struct nvgsp_vram_alloc *alloc,
    void *owner __unused)
{
	nvgsp_vram_free_alloc(nvgsp_state_get(gpu), alloc);
}

struct nvgsp_vram_alloc *
nvgsp_vram_alloc_display(struct nvgpu_device *gpu, uint64_t size,
    uint64_t align, void *owner)
{
	return (nvgsp_vram_alloc_with_kind(nvgsp_state_get(gpu), size, align,
	    NVGSP_VRAM_DISPLAY_DATA, owner));
}

void
nvgsp_vram_free_display(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc)
{
	nvgsp_vram_free_alloc(nvgsp_state_get(gpu), alloc);
}

struct drm_mm_node *
nvgsp_vram_alloc_get_node(struct nvgsp_vram_alloc *alloc)
{
	return (alloc != NULL ? &alloc->node : NULL);
}

struct nvgsp_vram_alloc *
nvgsp_vram_alloc_from_node(struct drm_mm_node *node)
{
	return (node != NULL ? container_of(node, struct nvgsp_vram_alloc, node) : NULL);
}

void *
nvgsp_vram_alloc_get_owner(struct nvgsp_vram_alloc *alloc)
{
	return (alloc != NULL ? alloc->owner : NULL);
}

uint64_t
nvgsp_vram_alloc_get_paddr(const struct nvgsp_vram_alloc *alloc)
{
	return (alloc != NULL ? alloc->paddr : 0);
}

uint64_t
nvgsp_vram_alloc_get_size(const struct nvgsp_vram_alloc *alloc)
{
	return (alloc != NULL ? alloc->size : 0);
}

int
nvgsp_vram_alloc_map_bar1_scatter(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t size, uint32_t pages)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint64_t *gvas;
	int error;

	if (gsp == NULL || alloc == NULL || pages == 0)
		return (EINVAL);
	if (alloc->bar1_page_gva != NULL) {
		if (alloc->bar1_page_count != pages || alloc->bar1_size != size)
			return (EINVAL);
		return (0);
	}
	gvas = kmalloc((size_t)pages * sizeof(*gvas), M_NVGSP_VRAM,
	    M_WAITOK | M_ZERO);
	error = nvgsp_bar_map_bar1_existing_scatter(gsp, alloc->paddr, size,
	    gvas, pages);
	if (error != 0) {
		kfree(gvas, M_NVGSP_VRAM);
		return (error == ENOSPC ? EAGAIN : error);
	}
	alloc->bar1_page_gva = gvas;
	alloc->bar1_page_count = pages;
	alloc->bar1_size = size;
	return (0);
}

void
nvgsp_vram_alloc_unmap_bar1_scatter(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL || alloc == NULL || alloc->bar1_page_gva == NULL)
		return;
	nvgsp_bar_unmap_bar1_existing_scatter(gsp, alloc->bar1_page_gva,
	    alloc->bar1_page_count);
	kfree(alloc->bar1_page_gva, M_NVGSP_VRAM);
	alloc->bar1_page_gva = NULL;
	alloc->bar1_page_count = 0;
	alloc->bar1_size = 0;
}

int
nvgsp_vram_alloc_map_bar1_range(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint64_t gva;
	int error;

	if (gsp == NULL || alloc == NULL || size == 0 || size > alloc->size)
		return (EINVAL);
	if (alloc->bar1_page_gva != NULL)
		return (EBUSY);
	if (alloc->bar1_gva != 0)
		return (alloc->bar1_size == size ? 0 : EINVAL);
	error = nvgsp_bar_map_bar1_existing_range(gsp, alloc->paddr, size, &gva);
	if (error != 0)
		return (error == ENOSPC ? EAGAIN : error);
	alloc->bar1_gva = gva;
	alloc->bar1_size = size;
	return (0);
}

void
nvgsp_vram_alloc_unmap_bar1_range(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL || alloc == NULL || alloc->bar1_gva == 0)
		return;
	nvgsp_bar_unmap_bar1_existing_range(gsp, alloc->bar1_gva,
	    alloc->bar1_size);
	alloc->bar1_gva = 0;
	alloc->bar1_size = 0;
}

uint64_t
nvgsp_vram_alloc_get_bar1_range(const struct nvgsp_vram_alloc *alloc)
{
	return (alloc != NULL ? alloc->bar1_gva : 0);
}

uint64_t
nvgsp_vram_alloc_bar1_gva(struct nvgsp_vram_alloc *alloc,
    unsigned long page_offset)
{
	if (alloc == NULL || alloc->bar1_page_gva == NULL ||
	    page_offset >= alloc->bar1_page_count)
		return (0);
	return (alloc->bar1_page_gva[page_offset]);
}

uint32_t
nvgsp_vram_alloc_read32(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t offset)
{
	uint64_t gva;

	if (alloc == NULL || offset + sizeof(uint32_t) > alloc->bar1_size)
		return (0xffffffffu);
	gva = nvgsp_vram_alloc_bar1_gva(alloc, offset / PAGE_SIZE);
	if (gva == 0)
		return (0xffffffffu);
	return (nvgsp_bar_rd32_bar1(nvgsp_state_get(gpu),
	    gva + offset % PAGE_SIZE));
}

void
nvgsp_vram_alloc_write32(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t offset, uint32_t value)
{
	uint64_t gva;

	if (alloc == NULL || offset + sizeof(uint32_t) > alloc->bar1_size)
		return;
	gva = nvgsp_vram_alloc_bar1_gva(alloc, offset / PAGE_SIZE);
	if (gva != 0)
		nvgsp_bar_wr32_bar1(nvgsp_state_get(gpu),
		    gva + offset % PAGE_SIZE, value);
}

uint64_t
nvgsp_vram_alloc_kind(struct nvgsp_state *gsp, uint64_t size, uint64_t align,
    enum nvgsp_vram_kind kind, void *owner)
{
	struct nvgsp_vram_alloc *alloc;

	alloc = nvgsp_vram_alloc_with_kind(gsp, size, align, kind, owner);
	return (alloc != NULL ? alloc->paddr : 0);
}

void
nvgsp_vram_free_kind(struct nvgsp_state *gsp, uint64_t paddr,
    enum nvgsp_vram_kind kind, void *owner)
{
	struct nvgsp_vram_alloc *alloc;

	if (gsp == NULL || paddr == 0)
		return;
	lockmgr(&gsp->vram_lock, LK_EXCLUSIVE);
	TAILQ_FOREACH(alloc, &gsp->vram_allocs, link) {
		if (alloc->paddr == paddr && alloc->kind == kind && alloc->owner == owner)
			break;
	}
	lockmgr(&gsp->vram_lock, LK_RELEASE);
	if (alloc != NULL)
		nvgsp_vram_free_alloc(gsp, alloc);
}

uint32_t
nvgsp_vram_free_owner(struct nvgsp_state *gsp, void *owner)
{
	struct nvgsp_vram_alloc *alloc;
	uint32_t count = 0;

	if (gsp == NULL || owner == NULL)
		return (0);
	for (;;) {
		alloc = NULL;
		lockmgr(&gsp->vram_lock, LK_EXCLUSIVE);
		TAILQ_FOREACH(alloc, &gsp->vram_allocs, link) {
			if (alloc->owner == owner)
				break;
		}
		lockmgr(&gsp->vram_lock, LK_RELEASE);
		if (alloc == NULL)
			break;
		nvgsp_vram_free_alloc(gsp, alloc);
		count++;
	}
	return (count);
}
