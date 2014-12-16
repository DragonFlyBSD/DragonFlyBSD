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
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_drv.h"

#include <linux/highmem.h>

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
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;

	scratch_pte = pte_encode(ppgtt->dev, ppgtt->scratch_page_dma_addr,
				 I915_CACHE_LLC);

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

int i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt;
	unsigned first_pd_entry_in_global_pt;
	int i;
	int ret = -ENOMEM;

	/* ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now.
	 */
	first_pd_entry_in_global_pt = 512 * 1024 - I915_PPGTT_PD_ENTRIES;

	ppgtt = kmalloc(sizeof(*ppgtt), M_DRM, M_WAITOK | M_ZERO);
	if (!ppgtt)
		return ret;

	ppgtt->dev = dev;
	ppgtt->num_pd_entries = I915_PPGTT_PD_ENTRIES;
	ppgtt->pt_pages = kmalloc(sizeof(vm_page_t) * ppgtt->num_pd_entries,
	    M_DRM, M_WAITOK | M_ZERO);
	if (!ppgtt->pt_pages)
		goto err_ppgtt;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		ppgtt->pt_pages[i] = vm_page_alloc(NULL, 0,
		    VM_ALLOC_NORMAL | VM_ALLOC_ZERO);
		if (!ppgtt->pt_pages[i])
			goto err_pt_alloc;
	}

	ppgtt->scratch_page_dma_addr = dev_priv->mm.gtt->scratch_page_dma;

	i915_ppgtt_clear_range(ppgtt, 0,
			       ppgtt->num_pd_entries*I915_PPGTT_PT_ENTRIES);

	ppgtt->pd_offset = (first_pd_entry_in_global_pt)*sizeof(gtt_pte_t);

	dev_priv->mm.aliasing_ppgtt = ppgtt;

	return 0;


err_pt_alloc:
	dev_priv->mm.aliasing_ppgtt = ppgtt;
	i915_gem_cleanup_aliasing_ppgtt(dev);
	return (-ENOMEM);
err_ppgtt:
	kfree(ppgtt, M_DRM);

	return ret;
}

void i915_gem_cleanup_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	vm_page_t m;
	int i;

	if (!ppgtt)
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
	drm_free(ppgtt->pt_pages, M_DRM);
	drm_free(ppgtt, M_DRM);
}

static void
i915_ppgtt_insert_pages(struct i915_hw_ppgtt *ppgtt, unsigned first_entry,
    unsigned num_entries, vm_page_t *pages, enum i915_cache_level cache_level)
{
	uint32_t *pt_vaddr;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;
	dma_addr_t page_addr;

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		pt_vaddr = kmap_atomic(ppgtt->pt_pages[act_pd]);

		for (i = first_pte; i < last_pte; i++) {
			page_addr = VM_PAGE_TO_PHYS(*pages);
			pt_vaddr[i] = pte_encode(ppgtt->dev, page_addr,
						 cache_level);

			pages++;
		}

		kunmap_atomic(pt_vaddr);

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

void i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
			    struct drm_i915_gem_object *obj,
			    enum i915_cache_level cache_level)
{
	i915_ppgtt_insert_pages(ppgtt, obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT, obj->pages, cache_level);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	i915_ppgtt_clear_range(ppgtt,
			       obj->gtt_space->start >> PAGE_SHIFT,
			       obj->base.size >> PAGE_SHIFT);
}

void i915_gem_init_ppgtt(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t pd_offset;
	struct intel_ring_buffer *ring;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	uint32_t pd_entry, first_pd_entry_in_global_pt;
	int i;

	if (!dev_priv->mm.aliasing_ppgtt)
		return;

	first_pd_entry_in_global_pt = 512 * 1024 - I915_PPGTT_PD_ENTRIES;
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		vm_paddr_t pt_addr;

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
		uint32_t ecochk, gab_ctl, ecobits;

		ecobits = I915_READ(GAC_ECO_BITS);
		I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_PPGTT_CACHE64B);

		gab_ctl = I915_READ(GAB_CTL);
		I915_WRITE(GAB_CTL, gab_ctl | GAB_CTL_CONT_AFTER_PAGEFAULT);

		ecochk = I915_READ(GAM_ECOCHK);
		I915_WRITE(GAM_ECOCHK, ecochk | ECOCHK_SNB_BIT |
				       ECOCHK_PPGTT_CACHE64B);
		I915_WRITE(GFX_MODE, _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	} else if (INTEL_INFO(dev)->gen >= 7) {
		I915_WRITE(GAM_ECOCHK, ECOCHK_PPGTT_CACHE64B);
		/* GFX_MODE is per-ring on gen7+ */
	}

	for_each_ring(ring, dev_priv, i) {
		if (INTEL_INFO(dev)->gen >= 7)
			I915_WRITE(RING_MODE_GEN7(ring),
				   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));

		I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
		I915_WRITE(RING_PP_DIR_BASE(ring), pd_offset);
	}
}

static bool do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->mm.gtt->do_idle_maps)) {
		dev_priv->mm.interruptible = false;
		if (i915_gpu_idle(dev_priv->dev)) {
			DRM_ERROR("Couldn't idle GPU\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	return ret;
}

static void undo_idling(struct drm_i915_private *dev_priv, bool interruptible)
{
	if (unlikely(dev_priv->mm.gtt->do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}


static void i915_ggtt_clear_range(struct drm_device *dev,
				 unsigned first_entry,
				 unsigned num_entries)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	gtt_pte_t scratch_pte;
	gtt_pte_t __iomem *gtt_base = dev_priv->mm.gtt->gtt + first_entry;
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;
	int i;

	if (INTEL_INFO(dev)->gen < 6) {
		intel_gtt_clear_range(first_entry, num_entries);
		return;
	}

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = pte_encode(dev, dev_priv->mm.gtt->scratch_page_dma, I915_CACHE_LLC);
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
	readl(gtt_base);
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* First fill our portion of the GTT with scratch pages */
	i915_ggtt_clear_range(dev, dev_priv->mm.gtt_start / PAGE_SIZE,
			      (dev_priv->mm.gtt_end - dev_priv->mm.gtt_start) / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list) {
		i915_gem_clflush_object(obj);
		i915_gem_gtt_bind_object(obj, obj->cache_level);
	}

	i915_gem_chipset_flush(dev);
}

#if 0
int i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj)
{
	if (obj->has_dma_mapping)
		return 0;

	if (!dma_map_sg(&obj->base.dev->pdev->dev,
			obj->pages->sgl, obj->pages->nents,
			PCI_DMA_BIDIRECTIONAL))
		return -ENOSPC;

	return 0;
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_bind_object(struct drm_i915_gem_object *obj,
				  enum i915_cache_level level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct sg_table *st = obj->pages;
	struct scatterlist *sg = st->sgl;
	const int first_entry = obj->gtt_space->start >> PAGE_SHIFT;
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;
	gtt_pte_t __iomem *gtt_entries = dev_priv->mm.gtt->gtt + first_entry;
	int unused, i = 0;
	unsigned int len, m = 0;
	dma_addr_t addr;

	for_each_sg(st->sgl, sg, st->nents, unused) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (m = 0; m < len; m++) {
			addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			iowrite32(pte_encode(dev, addr, level), &gtt_entries[i]);
			i++;
		}
	}

	BUG_ON(i > max_entries);
	BUG_ON(i != obj->base.size / PAGE_SIZE);

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1]) != pte_encode(dev, addr, level));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}
#endif

void i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj,
			      enum i915_cache_level cache_level)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
			AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;
	intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT, obj->pages, flags);

	obj->has_global_gtt_mapping = 1;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
	    obj->base.size >> PAGE_SHIFT);

	undo_idling(dev_priv, interruptible);
	obj->has_global_gtt_mapping = 0;
}

void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

#if 0
	if (!obj->has_dma_mapping)
		dma_unmap_sg(&dev->pdev->dev,
			     obj->pages->sgl, obj->pages->nents,
			     PCI_DMA_BIDIRECTIONAL);
#endif

	undo_idling(dev_priv, interruptible);
}

static void i915_gtt_color_adjust(struct drm_mm_node *node,
				  unsigned long color,
				  unsigned long *start,
				  unsigned long *end)
{
	if (node->color != color)
		*start += 4096;

	if (!list_empty(&node->node_list)) {
		node = list_entry(node->node_list.next,
				  struct drm_mm_node,
				  node_list);
		if (node->allocated && node->color != color)
			*end -= 4096;
	}
}

void i915_gem_init_global_gtt(struct drm_device *dev,
			      unsigned long start,
			      unsigned long mappable_end,
			      unsigned long end)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	unsigned long mappable;
	int error;

	mappable = min(end, mappable_end) - start;

	/* Substract the guard page ... */
	drm_mm_init(&dev_priv->mm.gtt_space, start, end - start);
	if (!HAS_LLC(dev))
		dev_priv->mm.gtt_space.color_adjust = i915_gtt_color_adjust;

	dev_priv->mm.gtt_start = start;
	dev_priv->mm.gtt_mappable_end = mappable_end;
	dev_priv->mm.gtt_end = end;
	dev_priv->mm.gtt_total = end - start;
	dev_priv->mm.mappable_gtt_total = mappable;

	/* ... but ensure that we clear the entire range. */
	intel_gtt_clear_range(start / PAGE_SIZE, (end-start) / PAGE_SIZE);
	device_printf(dev->dev,
	    "taking over the fictitious range 0x%lx-0x%lx\n",
	    dev->agp->base + start, dev->agp->base + start + mappable);
	error = -vm_phys_fictitious_reg_range(dev->agp->base + start,
	    dev->agp->base + start + mappable, VM_MEMATTR_WRITE_COMBINING);
}

int i915_gem_gtt_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* On modern platforms we need not worry ourself with the legacy
	 * hostbridge query stuff. Skip it entirely
	 */
	if (INTEL_INFO(dev)->gen < 6 || 1) {
		dev_priv->mm.gtt = intel_gtt_get();
		if (!dev_priv->mm.gtt) {
			DRM_ERROR("Failed to initialize GTT\n");
			return -ENODEV;
		}
		return 0;
	}

	dev_priv->mm.gtt = kmalloc(sizeof(*dev_priv->mm.gtt), M_DRM, M_WAITOK | M_ZERO);
	if (!dev_priv->mm.gtt)
		return -ENOMEM;

#ifdef CONFIG_INTEL_IOMMU
	dev_priv->mm.gtt->needs_dmar = 1;
#endif

	/* GMADR is the PCI aperture used by SW to access tiled GFX surfaces in a linear fashion. */
	DRM_INFO("Memory usable by graphics device = %dM\n", dev_priv->mm.gtt->gtt_total_entries >> 8);
	DRM_DEBUG_DRIVER("GMADR size = %dM\n", dev_priv->mm.gtt->gtt_mappable_entries >> 8);
	DRM_DEBUG_DRIVER("GTT stolen size = %dM\n", dev_priv->mm.gtt->stolen_size >> 20);

	return 0;
}
