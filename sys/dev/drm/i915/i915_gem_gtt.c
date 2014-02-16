/*
 * Copyright © 2010 Daniel Vetter
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * $FreeBSD: src/sys/dev/drm2/i915/i915_gem_gtt.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

#include <sys/sfbuf.h>

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_drv.h"

typedef uint32_t gtt_pte_t;

/* PPGTT stuff */
#define GEN6_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0xff0))

#define GEN6_PDE_VALID			(1 << 0)
/* gen6+ has bit 11-4 for physical addr bit 39-32 */
#define GEN6_PDE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)

#define GEN6_PTE_VALID			(1 << 0)
#define GEN6_PTE_UNCACHED		(1 << 1)
#define HSW_PTE_UNCACHED		(0)
#define GEN6_PTE_CACHE_LLC		(2 << 1)
#define GEN6_PTE_CACHE_LLC_MLC		(3 << 1)
#define GEN6_PTE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)

static inline gtt_pte_t pte_encode(struct drm_device *dev,
				   dma_addr_t addr,
				   enum i915_cache_level level)
{
	gtt_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_LLC_MLC:
		/* Haswell doesn't set L3 this way */
		if (IS_HASWELL(dev))
			pte |= GEN6_PTE_CACHE_LLC;
		else
			pte |= GEN6_PTE_CACHE_LLC_MLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		if (IS_HASWELL(dev))
			pte |= HSW_PTE_UNCACHED;
		else
			pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		BUG();
	}


	return pte;
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void i915_ppgtt_clear_range(struct i915_hw_ppgtt *ppgtt,
				   unsigned first_entry,
				   unsigned num_entries)
{
	gtt_pte_t *pt_vaddr;
	gtt_pte_t scratch_pte;
	struct sf_buf *sf;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;

	scratch_pte = GEN6_GTT_ADDR_ENCODE(ppgtt->scratch_page_dma_addr);
	scratch_pte |= GEN6_PTE_VALID | GEN6_PTE_CACHE_LLC;

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		sf = sf_buf_alloc(ppgtt->pt_pages[act_pd]);
		pt_vaddr = (uint32_t *)(uintptr_t)sf_buf_kva(sf);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		sf_buf_free(sf);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

int
i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv;
	struct i915_hw_ppgtt *ppgtt;
	u_int first_pd_entry_in_global_pt, i;

	dev_priv = dev->dev_private;

	/*
	 * ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now.
	 */
	first_pd_entry_in_global_pt = 512 * 1024 - I915_PPGTT_PD_ENTRIES;

	ppgtt = kmalloc(sizeof(*ppgtt), DRM_I915_GEM, M_WAITOK | M_ZERO);

	ppgtt->num_pd_entries = I915_PPGTT_PD_ENTRIES;
	ppgtt->pt_pages = kmalloc(sizeof(vm_page_t) * ppgtt->num_pd_entries,
	    DRM_I915_GEM, M_WAITOK | M_ZERO);

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		ppgtt->pt_pages[i] = vm_page_alloc(NULL, 0,
		    VM_ALLOC_NORMAL | VM_ALLOC_ZERO);
		if (ppgtt->pt_pages[i] == NULL) {
			dev_priv->mm.aliasing_ppgtt = ppgtt;
			i915_gem_cleanup_aliasing_ppgtt(dev);
			return (-ENOMEM);
		}
	}

	ppgtt->scratch_page_dma_addr = dev_priv->mm.gtt->scratch_page_dma;

	i915_ppgtt_clear_range(ppgtt, 0, ppgtt->num_pd_entries *
	    I915_PPGTT_PT_ENTRIES);
	ppgtt->pd_offset = (first_pd_entry_in_global_pt) * sizeof(uint32_t);
	dev_priv->mm.aliasing_ppgtt = ppgtt;
	return (0);
}

static void
i915_ppgtt_insert_pages(struct i915_hw_ppgtt *ppgtt, unsigned first_entry,
    unsigned num_entries, vm_page_t *pages, uint32_t pte_flags)
{
	uint32_t *pt_vaddr, pte;
	struct sf_buf *sf;
	unsigned act_pd, first_pte;
	unsigned last_pte, i;
	vm_paddr_t page_addr;

	act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	first_pte = first_entry % I915_PPGTT_PT_ENTRIES;

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		sf = sf_buf_alloc(ppgtt->pt_pages[act_pd]);
		pt_vaddr = (uint32_t *)(uintptr_t)sf_buf_kva(sf);

		for (i = first_pte; i < last_pte; i++) {
			page_addr = VM_PAGE_TO_PHYS(*pages);
			pte = GEN6_PTE_ADDR_ENCODE(page_addr);
			pt_vaddr[i] = pte | pte_flags;

			pages++;
		}

		sf_buf_free(sf);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

void
i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
    struct drm_i915_gem_object *obj, enum i915_cache_level cache_level)
{
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;
	uint32_t pte_flags;

	dev = obj->base.dev;
	dev_priv = dev->dev_private;
	pte_flags = GEN6_PTE_VALID;

	switch (cache_level) {
	case I915_CACHE_LLC_MLC:
		pte_flags |= GEN6_PTE_CACHE_LLC_MLC;
		break;
	case I915_CACHE_LLC:
		pte_flags |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		pte_flags |= GEN6_PTE_UNCACHED;
		break;
	default:
		panic("cache mode");
	}

	i915_ppgtt_insert_pages(ppgtt, obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT, obj->pages, pte_flags);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	i915_ppgtt_clear_range(ppgtt, obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT);
}

void
i915_gem_cleanup_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv;
	struct i915_hw_ppgtt *ppgtt;
	vm_page_t m;
	int i;

	dev_priv = dev->dev_private;
	ppgtt = dev_priv->mm.aliasing_ppgtt;
	if (ppgtt == NULL)
		return;
	dev_priv->mm.aliasing_ppgtt = NULL;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		m = ppgtt->pt_pages[i];
		if (m != NULL) {
			vm_page_busy_wait(m, FALSE, "i915gem");
			vm_page_unwire(m, 0);
			vm_page_free(m);
		}
	}
	drm_free(ppgtt->pt_pages, DRM_I915_GEM);
	drm_free(ppgtt, DRM_I915_GEM);
}


static unsigned int
cache_level_to_agp_type(struct drm_device *dev, enum i915_cache_level
    cache_level)
{

	switch (cache_level) {
	case I915_CACHE_LLC_MLC:
		if (INTEL_INFO(dev)->gen >= 6)
			return (AGP_USER_CACHED_MEMORY_LLC_MLC);
		/*
		 * Older chipsets do not have this extra level of CPU
		 * cacheing, so fallthrough and request the PTE simply
		 * as cached.
		 */
	case I915_CACHE_LLC:
		return (AGP_USER_CACHED_MEMORY);

	default:
	case I915_CACHE_NONE:
		return (AGP_USER_MEMORY);
	}
}

static bool
do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->mm.gtt->do_idle_maps)) {
		dev_priv->mm.interruptible = false;
		if (i915_gpu_idle(dev_priv->dev, false)) {
			DRM_ERROR("Couldn't idle GPU\n");
			/* Wait a bit, in hopes it avoids the hang */
			DELAY(10);
		}
	}

	return ret;
}

static void
undo_idling(struct drm_i915_private *dev_priv, bool interruptible)
{

	if (unlikely(dev_priv->mm.gtt->do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}

void
i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv;
	struct drm_i915_gem_object *obj;

	dev_priv = dev->dev_private;

	/* First fill our portion of the GTT with scratch pages */
	intel_gtt_clear_range(dev_priv->mm.gtt_start / PAGE_SIZE,
	    (dev_priv->mm.gtt_end - dev_priv->mm.gtt_start) / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		i915_gem_clflush_object(obj);
		i915_gem_gtt_rebind_object(obj, obj->cache_level);
	}

	intel_gtt_chipset_flush();
}

int
i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj)
{
	unsigned int agp_type;

	agp_type = cache_level_to_agp_type(obj->base.dev, obj->cache_level);
	intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT, obj->pages, agp_type);
	return (0);
}

void
i915_gem_gtt_rebind_object(struct drm_i915_gem_object *obj,
    enum i915_cache_level cache_level)
{
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;
	unsigned int agp_type;

	dev = obj->base.dev;
	dev_priv = dev->dev_private;
	agp_type = cache_level_to_agp_type(dev, cache_level);

	intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT, obj->pages, agp_type);
}

void
i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	dev = obj->base.dev;
	dev_priv = dev->dev_private;

	interruptible = do_idling(dev_priv);

	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT);

	undo_idling(dev_priv, interruptible);
}

#define GFX_MODE_ENABLE(bit) (((bit) << 16) | (bit))

void i915_gem_init_ppgtt(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t pd_offset;
	struct intel_ring_buffer *ring;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	uint32_t pd_entry;
	vm_paddr_t pt_addr;
	u_int first_pd_entry_in_global_pt, i;

	if (ppgtt == NULL)
		return;

	first_pd_entry_in_global_pt = 512 * 1024 - I915_PPGTT_PD_ENTRIES;
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		pt_addr = VM_PAGE_TO_PHYS(ppgtt->pt_pages[i]);
		pd_entry = GEN6_PDE_ADDR_ENCODE(pt_addr);
		pd_entry |= GEN6_PDE_VALID;
		intel_gtt_write(first_pd_entry_in_global_pt + i, pd_entry);
	}
	intel_gtt_read_pte(first_pd_entry_in_global_pt);

	pd_offset = ppgtt->pd_offset;
	pd_offset /= 64; /* in cachelines, */
	pd_offset <<= 16;

	if (INTEL_INFO(dev)->gen == 6) {
		uint32_t ecochk = I915_READ(GAM_ECOCHK);
		I915_WRITE(GAM_ECOCHK, ecochk | ECOCHK_SNB_BIT |
				       ECOCHK_PPGTT_CACHE64B);
		I915_WRITE(GFX_MODE, GFX_MODE_ENABLE(GFX_PPGTT_ENABLE));
	} else if (INTEL_INFO(dev)->gen >= 7) {
		I915_WRITE(GAM_ECOCHK, ECOCHK_PPGTT_CACHE64B);
		/* GFX_MODE is per-ring on gen7+ */
	}

	for (i = 0; i < I915_NUM_RINGS; i++) {
		ring = &dev_priv->rings[i];

		if (INTEL_INFO(dev)->gen >= 7)
			I915_WRITE(RING_MODE_GEN7(ring),
				   GFX_MODE_ENABLE(GFX_PPGTT_ENABLE));

		I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
		I915_WRITE(RING_PP_DIR_BASE(ring), pd_offset);
	}
}
