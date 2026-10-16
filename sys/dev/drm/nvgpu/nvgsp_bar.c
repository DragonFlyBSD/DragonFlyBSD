/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP BAR1/BAR2 page-table backend for the native NVIDIA driver.
 */

#include "nvgsp_bar.h"
#include "nvgsp_priv.h"
#include "nvgsp_rm.h"

#include <vm/vm.h>
#include <vm/pmap.h>

static MALLOC_DEFINE(M_NVGSP_BAR2_PT, "nvgsp_bar2_pt", "nvgsp BAR2 page tables");

#define NV_VGPU_MSG_FUNCTION_UPDATE_BAR_PDE 70
#define NV_RPC_UPDATE_PDE_BAR_2             1

#define BAR2_PT_PD1	1
#define BAR2_PT_PD0	2
#define BAR2_PT_SPT	3

struct rpc_update_bar_pde_v15_00_b2 {
	uint32_t barType;
	uint8_t  _pad[4];
	uint64_t entryValue;
	uint64_t entryLevelShift;
};

int vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end,
    vm_memattr_t memattr);
void vm_phys_fictitious_unreg_range(vm_paddr_t start, vm_paddr_t end);
void nvgsp_bar_unmap_bar1_existing_scatter(struct nvgsp_state *sc,
    uint64_t *gvas, uint32_t count);

static __inline void
nvgsp_bar2_pramin_set_base(struct nvgsp_state *sc, uint64_t paddr)
{
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, (uint32_t)(paddr >> 16));
}

static __inline void
nvgsp_bar2_pramin_wr32(struct nvgsp_state *sc, uint64_t paddr, uint32_t val)
{
	nvgsp_wr32(sc, NV_PRAMIN + (uint32_t)(paddr & 0xffffu), val);
}

static __inline void
nvgsp_bar2_pramin_wr64(struct nvgsp_state *sc, uint64_t paddr, uint64_t val)
{
	nvgsp_bar2_pramin_wr32(sc, paddr + 0, (uint32_t)(val & 0xffffffffu));
	nvgsp_bar2_pramin_wr32(sc, paddr + 4, (uint32_t)(val >> 32));
}

static void
nvgsp_bar_zero_bar2_vram_page_locked(struct nvgsp_state *sc, uint64_t paddr)
{
	uint32_t saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);

	nvgsp_bar2_pramin_set_base(sc, paddr & ~(uint64_t)0xffffu);
	for (uint32_t off = 0; off < NVGSP_GMMU_PT_PAGE_SIZE; off += 4)
		nvgsp_bar2_pramin_wr32(sc, paddr + off, 0);
	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
}

static struct nvgsp_bar2_pt *
nvgsp_bar_find_bar2_pt(struct nvgsp_bar2 *b2, uint8_t level,
    uint32_t pd2_idx, uint32_t pd1_idx, uint32_t pd0_idx)
{
	struct nvgsp_bar2_pt *pt;

	LIST_FOREACH(pt, &b2->pt_pages, link) {
		if (pt->level == level &&
		    pt->pd2_idx == pd2_idx &&
		    pt->pd1_idx == pd1_idx &&
		    pt->pd0_idx == pd0_idx)
			return (pt);
	}

	return (NULL);
}

static int
nvgsp_bar_alloc_bar2_pt(struct nvgsp_state *sc, uint8_t level,
    uint32_t pd2_idx, uint32_t pd1_idx, uint32_t pd0_idx,
    struct nvgsp_bar2_pt **ppt)
{
	struct nvgsp_bar2 *b2 = &sc->bar2;
	struct nvgsp_bar2_pt *pt;
	uint64_t paddr;

	pt = nvgsp_bar_find_bar2_pt(b2, level, pd2_idx, pd1_idx, pd0_idx);
	if (pt != NULL) {
		*ppt = pt;
		return (0);
	}

	paddr = nvgsp_vram_alloc_kind(sc, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, NVGSP_VRAM_BAR2_PT, b2);
	if (paddr == 0)
		return (ENOMEM);

	pt = kmalloc(sizeof(*pt), M_NVGSP_BAR2_PT, M_WAITOK | M_ZERO);
	pt->level = level;
	pt->pd2_idx = pd2_idx;
	pt->pd1_idx = pd1_idx;
	pt->pd0_idx = pd0_idx;
	pt->paddr = paddr;
	LIST_INSERT_HEAD(&b2->pt_pages, pt, link);

	lwkt_gettoken(&sc->gsp_tok);
	nvgsp_bar_zero_bar2_vram_page_locked(sc, paddr);
	lwkt_reltoken(&sc->gsp_tok);

	*ppt = pt;
	return (0);
}

static int
nvgsp_bar_get_bar2_spt(struct nvgsp_state *sc, uint64_t bar2_gva,
    struct nvgsp_bar2_pt **pspt)
{
	struct nvgsp_bar2 *b2 = &sc->bar2;
	struct nvgsp_bar2_pt *pd1_pt, *pd0_pt, *spt_pt;
	uint32_t pd2_idx, pd1_idx, pd0_idx;
	uint32_t saved;
	int err;

	pd2_idx = (uint32_t)((bar2_gva >> NVGSP_GMMU_PD2_SHIFT) &
	    (NVGSP_GMMU_PD2_ENTRIES - 1));
	pd1_idx = (uint32_t)((bar2_gva >> NVGSP_GMMU_PD1_SHIFT) &
	    (NVGSP_GMMU_PD1_ENTRIES - 1));
	pd0_idx = (uint32_t)((bar2_gva >> NVGSP_GMMU_PD0_SHIFT) &
	    (NVGSP_GMMU_PD0_ENTRIES - 1));

	err = nvgsp_bar_alloc_bar2_pt(sc, BAR2_PT_PD1, pd2_idx, 0, 0, &pd1_pt);
	if (err != 0)
		return (err);

	err = nvgsp_bar_alloc_bar2_pt(sc, BAR2_PT_PD0, pd2_idx, pd1_idx, 0,
	    &pd0_pt);
	if (err != 0)
		return (err);

	err = nvgsp_bar_alloc_bar2_pt(sc, BAR2_PT_SPT, pd2_idx, pd1_idx,
	    pd0_idx, &spt_pt);
	if (err != 0)
		return (err);

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);

	nvgsp_bar2_pramin_set_base(sc, b2->pd2_paddr & ~(uint64_t)0xffffu);
	nvgsp_bar2_pramin_wr64(sc, b2->pd2_paddr + pd2_idx * 8,
	    nvgsp_pde_to_vram(pd1_pt->paddr));

	nvgsp_bar2_pramin_set_base(sc, pd1_pt->paddr & ~(uint64_t)0xffffu);
	nvgsp_bar2_pramin_wr64(sc, pd1_pt->paddr + pd1_idx * 8,
	    nvgsp_pde_to_vram(pd0_pt->paddr));

	nvgsp_bar2_pramin_set_base(sc, pd0_pt->paddr & ~(uint64_t)0xffffu);
	nvgsp_bar2_pramin_wr64(sc, pd0_pt->paddr + pd0_idx * 16,
	    nvgsp_pde_to_vram(spt_pt->paddr));
	nvgsp_bar2_pramin_wr64(sc, pd0_pt->paddr + pd0_idx * 16 + 8, 0);

	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

	if (b2->pd1_paddr == 0)
		b2->pd1_paddr = pd1_pt->paddr;
	if (b2->pd0_paddr == 0)
		b2->pd0_paddr = pd0_pt->paddr;
	if (b2->spt_paddr == 0)
		b2->spt_paddr = spt_pt->paddr;

	*pspt = spt_pt;
	return (0);
}

static int
nvgsp_bar_bootstrap_bar2(struct nvgsp_state *sc)
{
	struct nvgsp_bar2 *b2 = &sc->bar2;
	uint64_t gva;
	int err;

	/*
	 * nouveau bootstraps BAR2 with the smallest supported page size
	 * (tu102_vmm.page[] walks down to shift 12 before nvgsp_vmm_boot()).
	 * Pre-create every PD0->SPT edge that covers the halved BAR2 window,
	 * instead of allocating the tree lazily after UPDATE_BAR_PDE.
	 */
	for (gva = 0; gva < b2->aperture_size; gva +=
	    (1ULL << NVGSP_GMMU_PD0_SHIFT)) {
		struct nvgsp_bar2_pt *spt_pt __unused;

		err = nvgsp_bar_get_bar2_spt(sc, gva, &spt_pt);
		if (err != 0)
			return (err);
	}

	return (0);
}

void
nvgsp_bar_invalidate_bar2(struct nvgsp_state *sc)
{
	uint32_t trig_rb = 0xffffffffu;

	/*
	 * Match nouveau TU102 BAR VMM flush.  Under GSP-RM, BAR2 updates are
	 * invalidated through the GSP-provided BAR2 PDB with PAGE_ALL,
	 * HUB_ONLY, and ALL_PDB set.
	 */
	nvgsp_wr32(sc, 0xb830a0, (uint32_t)(sc->gsp_bar2_pdb >> 8));
	nvgsp_wr32(sc, 0xb830a4, 0x00000000u);
	nvgsp_wr32(sc, 0xb830b0, 0x80000000u | 0x00000007u);
	for (int spin = 0; spin < 200; spin++) {
		trig_rb = nvgsp_rd32(sc, 0xb830b0);
		if (!(trig_rb & 0x80000000u))
			break;
		DELAY(10);
	}

#ifdef NVGSP_DEBUG_BAR2
	nvgpu_log(NVGPU_LOG_DEBUG, "bar2: TU102 invalidate PDB=0x%llx 0xb830b0=0x%x\n",
	    (unsigned long long)sc->gsp_bar2_pdb, trig_rb);
#endif
}

static int
nvgsp_bar_start_bar2(struct nvgsp_state *sc)
{
	struct nvgsp_bar2 *b2 = &sc->bar2;
	uint64_t pd2;
	uint64_t pd2_pde;
	struct rpc_update_bar_pde_v15_00_b2 *rpc;
	int err;
	uint32_t saved;

	if (nvgpu_device_get_bar(sc->gpu, 3) == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: PCIe BAR3 (BAR2) not mapped\n");
		return (ENXIO);
	}
	if (sc->gsp_bar2_pdb == 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: GSP did not publish bar2PdeBase\n");
		return (ENXIO);
	}

	pd2 = nvgsp_vram_alloc_kind(sc, 0x1000, 0x1000,
	    NVGSP_VRAM_BAR2_ROOT, b2);
	if (!pd2) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: VRAM alloc failed\n");
		return (ENOMEM);
	}

	b2->pd3_paddr = sc->gsp_bar2_pdb;
	b2->pd2_paddr = pd2;
	b2->pd1_paddr = 0;
	b2->pd0_paddr = 0;
	b2->spt_paddr = 0;
	b2->aperture_size = rman_get_size(nvgpu_device_get_bar(sc->gpu, 3)) >> 1;
	b2->next_gva  = NVGSP_GMMU_PT_PAGE_SIZE;
	LIST_INIT(&b2->pt_pages);

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_bar_zero_bar2_vram_page_locked(sc, pd2);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

	err = nvgsp_bar_bootstrap_bar2(sc);
	if (err != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: bootstrap failed err=%d\n", err);
		return (err);
	}

	pd2_pde = (pd2 >> NVGSP_PT_ADDR_SHIFT) | NVGSP_PDE_APERTURE_VRAM;

	/* Diag: read GSP\'s BAR2 PDB[0] via PRAMIN before/after RPC. */
	uint64_t pdb_paddr = sc->gsp_bar2_pdb;
	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_bar2_pramin_set_base(sc, pdb_paddr & ~(uint64_t)0xffffu);
	uint32_t pdb0_pre_lo = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 0) & 0xffffu));
	uint32_t pdb0_pre_hi = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 4) & 0xffffu));
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

	rpc = nvgsp_rpc_get(sc, NV_VGPU_MSG_FUNCTION_UPDATE_BAR_PDE,
	    sizeof(*rpc));
	if (rpc == NULL)
		return (ENOMEM);
	rpc->barType         = NV_RPC_UPDATE_PDE_BAR_2;
	rpc->entryValue      = pd2_pde;
	rpc->entryLevelShift = NVGSP_GMMU_PD3_SHIFT;
	err = nvgsp_rpc_wr(sc, rpc, NVGSP_RPC_REPLY_RECV);
	if (err != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: UPDATE_BAR_PDE BAR_2 failed err=%d\n", err);
		return (err);
	}

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_bar2_pramin_set_base(sc, pdb_paddr & ~(uint64_t)0xffffu);
	uint32_t pdb0_post_lo = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 0) & 0xffffu));
	uint32_t pdb0_post_hi = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 4) & 0xffffu));
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

#ifdef NVGSP_DEBUG_BAR2
	nvgpu_log(NVGPU_LOG_DEBUG, "bar2: GSP PDB[0] pre RPC = 0x%08x:%08x, post RPC = 0x%08x:%08x, expected pde = 0x%llx\n",
	    pdb0_pre_hi, pdb0_pre_lo, pdb0_post_hi, pdb0_post_lo,
	    (unsigned long long)pd2_pde);
#else
	(void)pdb0_pre_lo;
	(void)pdb0_pre_hi;
	(void)pdb0_post_lo;
	(void)pdb0_post_hi;
#endif

	nvgsp_bar_invalidate_bar2(sc);

	/* Read BAR2 inst reg (0xb80f48) -- the address walker uses as root. */
	uint32_t bar2_inst = nvgsp_rd32(sc, 0xb80f48);
	uint32_t bar1_inst = nvgsp_rd32(sc, 0xb80f40);
	uint64_t bar2_inst_paddr = ((uint64_t)(bar2_inst & 0x0fffffffu)) << 12;
	uint64_t bar1_inst_paddr = ((uint64_t)(bar1_inst & 0x0fffffffu)) << 12;
#ifdef NVGSP_DEBUG_BAR2
	nvgpu_log(NVGPU_LOG_DEBUG, "bar2: 0xb80f48=0x%08x (inst paddr=0x%llx), 0xb80f40=0x%08x (inst paddr=0x%llx)\n",
	    bar2_inst, (unsigned long long)bar2_inst_paddr,
	    bar1_inst, (unsigned long long)bar1_inst_paddr);
#endif

	/* Re-verify whether GSP populated BAR1 inst[0x200] PDB ptr.
	 * Earlier conclusion was "GSP doesn\'t init BAR1" -- recheck. */
	if (bar1_inst_paddr != 0) {
		lwkt_gettoken(&sc->gsp_tok);
		saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
		nvgsp_bar2_pramin_set_base(sc, bar1_inst_paddr & ~(uint64_t)0xffffu);
		uint32_t b1_lo = nvgsp_rd32(sc, NV_PRAMIN +
		    (uint32_t)((bar1_inst_paddr + 0x200) & 0xffffu));
		uint32_t b1_hi = nvgsp_rd32(sc, NV_PRAMIN +
		    (uint32_t)((bar1_inst_paddr + 0x204) & 0xffffu));
		nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
		lwkt_reltoken(&sc->gsp_tok);
#ifdef NVGSP_DEBUG_BAR2
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: BAR1_inst[0x200..0x208] (PDB ptr) = 0x%08x:%08x %s\n",
		    b1_hi, b1_lo,
		    (b1_lo == 0 && b1_hi == 0) ? "*** UNINITIALIZED ***" : "(populated)");
#else
		(void)b1_lo;
		(void)b1_hi;
#endif
	}

	/* Read PDB pointer from BAR2 inst block (offset 0x200) via PRAMIN. */
	if (bar2_inst_paddr != 0) {
		lwkt_gettoken(&sc->gsp_tok);
		saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
		nvgsp_bar2_pramin_set_base(sc, bar2_inst_paddr & ~(uint64_t)0xffffu);
		uint32_t inst_lo = nvgsp_rd32(sc, NV_PRAMIN +
		    (uint32_t)((bar2_inst_paddr + 0x200) & 0xffffu));
		uint32_t inst_hi = nvgsp_rd32(sc, NV_PRAMIN +
		    (uint32_t)((bar2_inst_paddr + 0x204) & 0xffffu));
		nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
		lwkt_reltoken(&sc->gsp_tok);
#ifdef NVGSP_DEBUG_BAR2
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: BAR2_inst[0x200..0x208] (PDB ptr) = 0x%08x:%08x\n",
		    inst_hi, inst_lo);
#else
		(void)inst_lo;
		(void)inst_hi;
#endif
	}

	b2->ready = true;  /* enable map_vram + flush */

	/* Nouveau-style flush setup: map a guaranteed-valid VRAM page at
	 * NVGSP_BAR2_GVA_FLUSH (=0); flush() reads from that GVA to force walker
	 * re-walk after PT updates (port of r535_bar_bar2_init lines 100-114
	 * + r535_bar_flush). */
	{
		uint64_t flush_vram = nvgsp_vram_alloc_kind(sc, 0x1000,
		    0x1000, NVGSP_VRAM_BAR2_FLUSH, b2);
		if (flush_vram != 0) {
			b2->flush_vram_paddr = flush_vram;
			(void)nvgsp_bar_map_bar2_vram(sc, NVGSP_BAR2_GVA_FLUSH, flush_vram);
			/* First flush: forces walker to walk our chain end-to-end. */
			nvgsp_bar_flush_bar2(sc);
		}
	}

	/* Warm BAR2 after the root update.  The write/read pair is kept as a
	 * functional GMMU/BAR2 visibility barrier for early RM RPC traffic. */
	{
		uint64_t warm_vram = nvgsp_vram_alloc_kind(sc, 0x1000, 0x1000,
		    NVGSP_VRAM_BAR2_TEST, b2);
		if (warm_vram != 0) {
			uint32_t rb;

			(void)nvgsp_bar_map_bar2_vram(sc, 0x2000, warm_vram);
			nvgsp_bar_flush_bar2(sc);
			nvgsp_bar_wr32_bar2(sc, 0x2000 + 0x10, 0xC0FFEE12u);
			nvgsp_bar_flush_bar2(sc);
			rb = nvgsp_bar_rd32_bar2(sc, 0x2000 + 0x10);
#ifdef NVGSP_DEBUG_BAR2
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "bar2: warmup readback=0x%08x target=0x%llx\n",
			    rb, (unsigned long long)warm_vram);
#else
			(void)rb;
#endif
			b2->next_gva = 0x3000;
		}
	}

#ifdef NVGSP_DEBUG_BAR2
	nvgpu_log(NVGPU_LOG_DEBUG, "bar2: PT chain PD2=0x%llx PD1=0x%llx PD0=0x%llx SPT=0x%llx; "
	    "GSP PDB=0x%llx; UPDATE_BAR_PDE pde=0x%llx; BAR2@%llx %lluMiB halve=%lluMiB\n",
	    (unsigned long long)pd2, (unsigned long long)b2->pd1_paddr,
	    (unsigned long long)b2->pd0_paddr, (unsigned long long)b2->spt_paddr,
	    (unsigned long long)b2->pd3_paddr,
	    (unsigned long long)pd2_pde,
	    (unsigned long long)rman_get_start(nvgpu_device_bar(sc->gpu, 3)),
	    (unsigned long long)rman_get_size(nvgpu_device_bar(sc->gpu, 3)) >> 20,
	    (unsigned long long)b2->aperture_size >> 20);
#endif
	b2->ready = true;
	return (0);
}

static void
nvgsp_bar_stop_bar2(struct nvgsp_state *sc)
{
	struct nvgsp_bar2_pt *pt;
	uint32_t freed;

	sc->bar2.ready = false;
	while ((pt = LIST_FIRST(&sc->bar2.pt_pages)) != NULL) {
		LIST_REMOVE(pt, link);
		kfree(pt, M_NVGSP_BAR2_PT);
	}
	/* Root/PT/flush/test pages live only in the VRAM allocator records
	 * charged to &sc->bar2; release them the same way as BAR1. */
	freed = nvgsp_vram_free_owner(sc, &sc->bar2);
	if (freed != 0)
		nvgpu_log(NVGPU_LOG_DEBUG, "bar2: fini released %u VRAM pages\n",
		    freed);
}

int
nvgsp_bar_map_bar2_vram(struct nvgsp_state *sc, uint64_t bar2_gva,
    uint64_t vram_paddr)
{
	struct nvgsp_bar2 *b2 = &sc->bar2;
	struct nvgsp_bar2_pt *spt_pt;
	uint32_t spt_idx;
	uint64_t pte;
	uint32_t saved;
	int err;

	if (!b2->ready)
		return (ENXIO);
	if ((bar2_gva & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) ||
	    (vram_paddr & (NVGSP_GMMU_PT_PAGE_SIZE - 1)))
		return (EINVAL);
	if (bar2_gva >= b2->aperture_size)
		return (ERANGE);

	err = nvgsp_bar_get_bar2_spt(sc, bar2_gva, &spt_pt);
	if (err != 0)
		return (err);

	spt_idx = (uint32_t)((bar2_gva >> NVGSP_GMMU_SPT_SHIFT)
	    & (NVGSP_GMMU_SPT_ENTRIES - 1));
	pte = ((uint64_t)vram_paddr >> NVGSP_PT_ADDR_SHIFT) | NVGSP_PTE_VALID;

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_bar2_pramin_set_base(sc, spt_pt->paddr & ~(uint64_t)0xffffu);
	nvgsp_bar2_pramin_wr64(sc, spt_pt->paddr + spt_idx * 8, pte);
	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

#ifdef NVGSP_DEBUG_BAR2
	nvgpu_log(NVGPU_LOG_DEBUG, "bar2: map BAR2_GVA=0x%llx -> VRAM=0x%llx (SPT[%u] page=0x%llx pte=0x%016llx)\n",
	    (unsigned long long)bar2_gva, (unsigned long long)vram_paddr,
	    spt_idx, (unsigned long long)spt_pt->paddr,
	    (unsigned long long)pte);
#endif
	return (0);
}

void
nvgsp_bar_wr32_bar2(struct nvgsp_state *sc, uint64_t bar2_gva, uint32_t val)
{
	bus_write_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)bar2_gva, val);
}

uint32_t
nvgsp_bar_rd32_bar2(struct nvgsp_state *sc, uint64_t bar2_gva)
{
	return bus_read_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)bar2_gva);
}


/* Nouveau r535_bar_flush equivalent: read from BAR2 GVA 0 via PCIe BAR3.
 * Walker translates the GVA through our PDB->PD2->PD1->PD0->SPT chain
 * and returns data from the flush_vram page. The read itself serves as
 * the "flush" -- it forces walker to walk the chain and refresh TLB. */
void
nvgsp_bar_flush_bar2(struct nvgsp_state *sc)
{
	if (!sc->bar2.ready)
		return;
	(void)bus_read_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)NVGSP_BAR2_GVA_FLUSH);
}

void
nvgsp_bar_wr64_bar2(struct nvgsp_state *sc, uint64_t bar2_gva, uint64_t val)
{
	bus_write_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)(bar2_gva + 0),
	    (uint32_t)(val & 0xffffffffu));
	bus_write_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)(bar2_gva + 4),
	    (uint32_t)(val >> 32));
}

uint64_t
nvgsp_bar_rd64_bar2(struct nvgsp_state *sc, uint64_t bar2_gva)
{
	uint32_t lo = bus_read_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)(bar2_gva + 0));
	uint32_t hi = bus_read_4(nvgpu_device_get_bar(sc->gpu, 3), (bus_size_t)(bar2_gva + 4));
	return ((uint64_t)hi << 32) | lo;
}


int
nvgsp_bar_rd64_pramin(struct nvgsp_state *sc, uint64_t paddr, uint64_t *out)
{
	uint32_t saved;
	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	nvgsp_bar2_pramin_set_base(sc, paddr & ~(uint64_t)0xffffu);
	uint32_t lo = nvgsp_rd32(sc, NV_PRAMIN + (uint32_t)((paddr + 0) & 0xffffu));
	uint32_t hi = nvgsp_rd32(sc, NV_PRAMIN + (uint32_t)((paddr + 4) & 0xffffu));
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);
	*out = ((uint64_t)hi << 32) | lo;
	return (0);
}

#define NV_VGPU_MSG_FUNCTION_UPDATE_BAR_PDE 70
#define NV_RPC_UPDATE_PDE_BAR_1             0

struct rpc_update_bar_pde_v15_00_b1 {
	uint32_t barType;
	uint8_t  _pad[4];
	uint64_t entryValue;
	uint64_t entryLevelShift;
};

static __inline void
nvgsp_bar1_pramin_set_base(struct nvgsp_state *sc, uint64_t paddr)
{
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, (uint32_t)(paddr >> 16));
}

static __inline void
nvgsp_bar1_pramin_wr32(struct nvgsp_state *sc, uint64_t paddr, uint32_t val)
{
	nvgsp_wr32(sc, NV_PRAMIN + (uint32_t)(paddr & 0xffffu), val);
}

static __inline void
nvgsp_bar1_pramin_wr64(struct nvgsp_state *sc, uint64_t paddr, uint64_t val)
{
	nvgsp_bar1_pramin_wr32(sc, paddr + 0, (uint32_t)(val & 0xffffffffu));
	nvgsp_bar1_pramin_wr32(sc, paddr + 4, (uint32_t)(val >> 32));
}

static __inline uint64_t
nvgsp_bar1_pramin_rd64(struct nvgsp_state *sc, uint64_t paddr)
{
	uint32_t lo, hi;

	lo = nvgsp_rd32(sc, NV_PRAMIN + (uint32_t)((paddr + 0) & 0xffffu));
	hi = nvgsp_rd32(sc, NV_PRAMIN + (uint32_t)((paddr + 4) & 0xffffu));
	return (((uint64_t)hi << 32) | lo);
}

static uint64_t
nvgsp_bar_get_bar1_limit(struct nvgsp_state *sc)
{
	return (nvgpu_device_get_bar(sc->gpu, 1) != NULL ? rman_get_size(nvgpu_device_get_bar(sc->gpu, 1)) : 0);
}

void
nvgsp_bar_invalidate_bar1(struct nvgsp_state *sc)
{
	uint32_t trig_rb = 0xffffffffu;

	/*
	 * Nouveau TU102 BAR VMM flush uses HUB MMU invalidate registers:
	 *   0xb830a0 = PDB >> 8
	 *   0xb830a4 = upper PDB
	 *   0xb830b0 = TRIGGER | PAGE_ALL | HUB_ONLY | ALL_PDB
	 */
	nvgsp_wr32(sc, 0xb830a0, (uint32_t)(sc->gsp_bar1_pdb >> 8));
	nvgsp_wr32(sc, 0xb830a4, 0x00000000u);
	nvgsp_wr32(sc, 0xb830b0, 0x80000000u | 0x00000007u);
	for (int spin = 0; spin < 200; spin++) {
		trig_rb = nvgsp_rd32(sc, 0xb830b0);
		if (!(trig_rb & 0x80000000u))
			break;
		DELAY(10);
	}

#ifdef NVGSP_DEBUG_BAR1
	nvgpu_log(NVGPU_LOG_DEBUG, "bar1: TU102 invalidate PDB=0x%llx 0xb830b0=0x%x\n",
	    (unsigned long long)sc->gsp_bar1_pdb, trig_rb);
#endif
}

static int
nvgsp_bar_start_bar1(struct nvgsp_state *sc)
{
	struct nvgsp_bar1 *b1 = &sc->bar1;
	uint64_t spt;
	uint64_t gsp_pd2, gsp_pd1, gsp_pd0;
	uint64_t spt_pde;
	uint64_t pd0_big, pd0_small;
	uint64_t pd0_big_pre, pd0_small_pre;
	uint64_t pd0_big_post, pd0_small_post;
	uint32_t saved;

	if (nvgpu_device_get_bar(sc->gpu, 1) == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar1: PCIe BAR1 not mapped\n");
		return (ENXIO);
	}
	if (sc->gsp_bar1_pdb == 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar1: GSP did not publish bar1PdeBase\n");
		return (ENXIO);
	}
	if (!sc->bar2.ready) {
		nvgpu_log(NVGPU_LOG_DEBUG, "bar1: BAR2 must be ready first (PRAMIN bootstrap)\n");
		return (ENXIO);
	}

	/* nouveau-style inheritance: don't allocate our own PD2/PD1/PD0,
	 * don't issue UPDATE_BAR_PDE BAR_1 RPC. GSP already built a BAR1 PT
	 * chain rooted at sc->gsp_bar1_pdb with mappings for GSP's own use
	 * (e.g. its internal channel inst blocks, runlist VRAM, etc). If we
	 * overwrite GSP's PD3[0], those GSP-internal BAR1 mappings break and
	 * GSP can't access its own state — silently page-faulting on inst
	 * reads, leaving PBDMA unable to schedule channels.
	 *
	 * Instead: walk GSP's chain (PD3[0]->PD2[0]->PD1[0]->PD0), allocate
	 * our own SPT pages, and mount them on a fixed high BAR1 PD0 range.
	 * This gives the driver a 64 MiB BAR1 window for host writes to VRAM
	 * page-table pages without disturbing GSP's low BAR1 mappings.
	 */
	uint64_t pdb_paddr = sc->gsp_bar1_pdb;
	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);

	/* Read GSP PD3[0] = first 8 bytes of GSP PD3 page. */
	nvgsp_bar1_pramin_set_base(sc, pdb_paddr & ~(uint64_t)0xffffu);
	uint32_t pd3_lo = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 0) & 0xffffu));
	uint32_t pd3_hi = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((pdb_paddr + 4) & 0xffffu));
	uint64_t pd3_0 = ((uint64_t)pd3_hi << 32) | pd3_lo;
	gsp_pd2 = (pd3_0 & ~(uint64_t)0xffull) << 4;

	/* Read GSP PD2[0]. */
	nvgsp_bar1_pramin_set_base(sc, gsp_pd2 & ~(uint64_t)0xffffu);
	uint32_t pd2_lo = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((gsp_pd2 + 0) & 0xffffu));
	uint32_t pd2_hi = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((gsp_pd2 + 4) & 0xffffu));
	uint64_t pd2_0 = ((uint64_t)pd2_hi << 32) | pd2_lo;
	gsp_pd1 = (pd2_0 & ~(uint64_t)0xffull) << 4;

	/* Read GSP PD1[0]. */
	nvgsp_bar1_pramin_set_base(sc, gsp_pd1 & ~(uint64_t)0xffffu);
	uint32_t pd1_lo = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((gsp_pd1 + 0) & 0xffffu));
	uint32_t pd1_hi = nvgsp_rd32(sc, NV_PRAMIN +
	    (uint32_t)((gsp_pd1 + 4) & 0xffffu));
	uint64_t pd1_0 = ((uint64_t)pd1_hi << 32) | pd1_lo;
	gsp_pd0 = (pd1_0 & ~(uint64_t)0xffull) << 4;

	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

#ifdef NVGSP_DEBUG_BAR1
	nvgpu_log(NVGPU_LOG_DEBUG, "bar1: walked GSP PT chain: PD3=0x%llx -> PD2=0x%llx -> PD1=0x%llx -> PD0=0x%llx\n",
	    (unsigned long long)pdb_paddr, (unsigned long long)gsp_pd2,
	    (unsigned long long)gsp_pd1, (unsigned long long)gsp_pd0);
#endif

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	for (uint32_t slot = NVGSP_BAR1_PD0_MANAGED_FIRST;
	    slot <= NVGSP_BAR1_PD0_MANAGED_LAST; slot++) {
		uint32_t idx = slot - NVGSP_BAR1_PD0_MANAGED_FIRST;

		/* Alloc OUR SPT.  Each SPT covers one 2 MiB BAR1 PD0 slot. */
		spt = nvgsp_vram_alloc_kind(sc, 0x1000, 0x1000,
		    NVGSP_VRAM_BAR1_SPT, &sc->bar1);
		if (spt == 0) {
			nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
			lwkt_reltoken(&sc->gsp_tok);
			nvgpu_log(NVGPU_LOG_DEBUG, "bar1: SPT alloc failed slot=%u\n", slot);
			return (ENOMEM);
		}

		/* Zero our SPT via PRAMIN. */
		nvgsp_bar1_pramin_set_base(sc, spt & ~(uint64_t)0xffffu);
		for (uint32_t off = 0; off < 0x1000; off += 4)
			nvgsp_bar1_pramin_wr32(sc, spt + off, 0);

		/* Write GSP PD0[slot] as a full dual PDE, like nouveau's
		 * gp100_vmm_pd0_pde() VMM_WO128() path.  We only install a
		 * 4 KiB SMALL SPT, so BIG is invalid and SMALL points at ours.
		 */
		spt_pde = (spt >> NVGSP_PT_ADDR_SHIFT) |
		    NVGSP_PDE_APERTURE_VRAM;
		pd0_big = gsp_pd0 + slot * 16 + 0;
		pd0_small = gsp_pd0 + slot * 16 + 8;
		nvgsp_bar1_pramin_set_base(sc, pd0_big & ~(uint64_t)0xffffu);
		pd0_big_pre = nvgsp_bar1_pramin_rd64(sc, pd0_big);
		pd0_small_pre = nvgsp_bar1_pramin_rd64(sc, pd0_small);
		nvgsp_bar1_pramin_wr64(sc, pd0_big, 0);
		nvgsp_bar1_pramin_wr64(sc, pd0_small, spt_pde);
		(void)nvgsp_rd32(sc, NV_PRAMIN);
		pd0_big_post = nvgsp_bar1_pramin_rd64(sc, pd0_big);
		pd0_small_post = nvgsp_bar1_pramin_rd64(sc, pd0_small);
		b1->spt_paddr[idx] = spt;

#ifdef NVGSP_DEBUG_BAR1
		nvgpu_log(NVGPU_LOG_DEBUG, "bar1: mounted OUR SPT 0x%llx at GSP PD0[%u] BIG 0x%llx->0x%llx SMALL 0x%llx->0x%llx\n",
		    (unsigned long long)spt, slot,
		    (unsigned long long)pd0_big_pre,
		    (unsigned long long)pd0_big_post,
		    (unsigned long long)pd0_small_pre,
		    (unsigned long long)pd0_small_post);
#endif
	}
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

	b1->pd3_paddr = sc->gsp_bar1_pdb;
	b1->pd2_paddr = gsp_pd2;   /* shared with GSP, informational */
	b1->pd1_paddr = gsp_pd1;
	b1->pd0_paddr = gsp_pd0;
	b1->next_gva  = NVGSP_BAR1_GVA_ALLOC_BASE;
	memset(b1->gva_used, 0, sizeof(b1->gva_used));
	b1->fictitious_start = rman_get_start(nvgpu_device_get_bar(sc->gpu, 1)) +
	    NVGSP_BAR1_GVA_ALLOC_BASE;
	b1->fictitious_end = b1->fictitious_start +
	    (vm_paddr_t)NVGSP_BAR1_GVA_ALLOC_PAGES * NVGSP_GMMU_PT_PAGE_SIZE;
	if (!b1->fictitious_registered) {
		int r;

		r = vm_phys_fictitious_reg_range(b1->fictitious_start,
		    b1->fictitious_end, VM_MEMATTR_WRITE_COMBINING);
		if (r != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "bar1: fictitious range 0x%llx-0x%llx failed err=%d\n",
			    (unsigned long long)b1->fictitious_start,
			    (unsigned long long)b1->fictitious_end, r);
			return (r);
		}
		b1->fictitious_registered = true;
	}

#ifdef NVGSP_DEBUG_BAR1
	nvgpu_log(NVGPU_LOG_DEBUG, "bar1: mounted OUR SPT window PD0[%u..%u]; GVA base 0x%llx\n",
	    NVGSP_BAR1_PD0_MANAGED_FIRST, NVGSP_BAR1_PD0_MANAGED_LAST,
	    (unsigned long long)b1->next_gva);
#endif

	nvgsp_bar_invalidate_bar1(sc);

#ifdef NVGSP_DEBUG_BAR1
	nvgpu_log(NVGPU_LOG_DEBUG, "bar1: inheriting GSP PT chain PD2=0x%llx PD1=0x%llx PD0=0x%llx; "
	    "our SPT window mounted on GSP PD0[%u..%u].SMALL; "
	    "BAR1@%llx %lluMiB\n",
	    (unsigned long long)gsp_pd2, (unsigned long long)gsp_pd1,
	    (unsigned long long)gsp_pd0, NVGSP_BAR1_PD0_MANAGED_FIRST,
	    NVGSP_BAR1_PD0_MANAGED_LAST,
	    (unsigned long long)rman_get_start(nvgpu_device_bar(sc->gpu, 1)),
	    (unsigned long long)rman_get_size(nvgpu_device_bar(sc->gpu, 1)) >> 20);
#endif

	sc->bar1.ready = true;

	/* SMOKE test removed: GVA=0 maps through GSP's own PT chain
	 * (our SPT window is mounted at high PD0 slots). Writing
	 * BAR1[0] would write to whatever GSP has at SPT[0] — likely a
	 * critical GSP-internal page. Don't go there. */

	return (0);
}

static void
nvgsp_bar_stop_bar1(struct nvgsp_state *sc)
{
	uint32_t freed;

	if (sc->bar1.fictitious_registered) {
		vm_phys_fictitious_unreg_range(sc->bar1.fictitious_start,
		    sc->bar1.fictitious_end);
		sc->bar1.fictitious_registered = false;
	}
	sc->bar1.ready = false;
	/* SPT/PT pages are only reachable through PTE content; the VRAM
	 * allocator records charged to &sc->bar1 are their free list. */
	freed = nvgsp_vram_free_owner(sc, &sc->bar1);
	if (freed != 0)
		nvgpu_log(NVGPU_LOG_DEBUG, "bar1: fini released %u PT pages\n",
		    freed);
}

static int
nvgsp_bar_map_bar1_vram_pte(struct nvgsp_state *sc, uint64_t bar1_gva,
    uint64_t vram_paddr)
{
	struct nvgsp_bar1 *b1 = &sc->bar1;
	uint32_t saved;
	uint32_t spt_idx;
	uint32_t pd0_idx;
	uint64_t spt;
	uint64_t pte;

	if (!b1->ready)
		return (ENXIO);
	if (bar1_gva >= nvgsp_bar_get_bar1_limit(sc))
		return (EINVAL);

	/* Our SPT is mounted at GSP PD0[127].SMALL, covering the final
	 * 2 MiB of BAR1.  Match nouveau's VMM iterator index split:
	 *   SPT index = (gva >> 12) & 0x1ff
	 *   PD0 index = (gva >> 21) & 0xff
	 * The old code used gva >> 12 directly, which wrote far beyond the
	 * 512-entry SPT for GVAs like 0xfe07000.
	 */
	pd0_idx = (uint32_t)((bar1_gva >> 21) & 0xffu);
	if (pd0_idx < NVGSP_BAR1_PD0_MANAGED_FIRST ||
	    pd0_idx > NVGSP_BAR1_PD0_MANAGED_LAST)
		return (EINVAL);

	spt_idx = (uint32_t)((bar1_gva >> 12) & 0x1ffu);  /* 4 KiB SMALL pages */
	pte = (vram_paddr >> 12) << 8;          /* PTE: paddr in [39:8] */
	pte |= 0x1;                              /* VALID = bit 0 */

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	spt = b1->spt_paddr[pd0_idx - NVGSP_BAR1_PD0_MANAGED_FIRST];
	nvgsp_bar1_pramin_set_base(sc, spt & ~(uint64_t)0xffffu);
	nvgsp_bar1_pramin_wr64(sc, spt + (uint64_t)spt_idx * 8, pte);
	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

#ifdef NVGSP_DEBUG_BAR1
	nvgpu_log(NVGPU_LOG_DEBUG, "bar1: map BAR1_GVA=0x%llx -> VRAM=0x%llx (SPT[%u]=0x%llx)\n",
	    (unsigned long long)bar1_gva, (unsigned long long)vram_paddr,
	    spt_idx, (unsigned long long)pte);
#endif
	return (0);
}

/* Map a 4 KiB VRAM page at a specific BAR1 GVA by writing the SPT entry. */
int
nvgsp_bar_map_bar1_vram(struct nvgsp_state *sc, uint64_t bar1_gva,
    uint64_t vram_paddr)
{
	int err;

	err = nvgsp_bar_map_bar1_vram_pte(sc, bar1_gva, vram_paddr);
	if (err == 0)
		nvgsp_bar_invalidate_bar1(sc);
	return (err);
}

static int
nvgsp_bar_clear_bar1_gva(struct nvgsp_state *sc, uint64_t bar1_gva)
{
	struct nvgsp_bar1 *b1 = &sc->bar1;
	uint32_t saved;
	uint32_t spt_idx;
	uint32_t pd0_idx;
	uint64_t spt;

	if (!b1->ready)
		return (ENXIO);
	if (bar1_gva >= nvgsp_bar_get_bar1_limit(sc))
		return (EINVAL);

	pd0_idx = (uint32_t)((bar1_gva >> 21) & 0xffu);
	if (pd0_idx < NVGSP_BAR1_PD0_MANAGED_FIRST ||
	    pd0_idx > NVGSP_BAR1_PD0_MANAGED_LAST)
		return (EINVAL);

	spt_idx = (uint32_t)((bar1_gva >> 12) & 0x1ffu);

	lwkt_gettoken(&sc->gsp_tok);
	saved = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
	spt = b1->spt_paddr[pd0_idx - NVGSP_BAR1_PD0_MANAGED_FIRST];
	nvgsp_bar1_pramin_set_base(sc, spt & ~(uint64_t)0xffffu);
	nvgsp_bar1_pramin_wr64(sc, spt + (uint64_t)spt_idx * 8, 0);
	(void)nvgsp_rd32(sc, NV_PRAMIN);
	nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved);
	lwkt_reltoken(&sc->gsp_tok);

	return (0);
}

/* Flush: read from BAR1 to force walker re-walk + L2 sync. */
void
nvgsp_bar_flush_bar1(struct nvgsp_state *sc)
{
	if (!sc->bar1.ready || nvgpu_device_get_bar(sc->gpu, 1) == NULL)
		return;
	(void)bus_read_4(nvgpu_device_get_bar(sc->gpu, 1), 0);
}

void
nvgsp_bar_wr32_bar1(struct nvgsp_state *sc, uint64_t gva, uint32_t val)
{
	if (!sc->bar1.ready)
		return;
	bus_write_4(nvgpu_device_get_bar(sc->gpu, 1), gva, val);
}

uint32_t
nvgsp_bar_rd32_bar1(struct nvgsp_state *sc, uint64_t gva)
{
	if (!sc->bar1.ready)
		return (0xdeadbeef);
	return (bus_read_4(nvgpu_device_get_bar(sc->gpu, 1), gva));
}

void
nvgsp_bar_wr64_bar1(struct nvgsp_state *sc, uint64_t gva, uint64_t val)
{
	if (!sc->bar1.ready)
		return;
	bus_write_8(nvgpu_device_get_bar(sc->gpu, 1), gva, val);
}

/*
 * nvgsp_bar1_set_region64()
 *
 * Ownership:
 *   Borrows sc and its BAR1 resource; it does not retain references or change
 *   BAR1 virtual address ownership.
 *
 * Lifetime:
 *   The caller must ensure [gva, gva + count * 8) is currently mapped in BAR1
 *   for the full duration of the call.
 *
 * Threading:
 *   Performs synchronous MMIO writes.  Callers provide higher-level
 *   serialization for the page table or object being updated.
 */
void
nvgsp_bar_set_bar1_region64(struct nvgsp_state *sc, uint64_t gva, uint64_t val,
    uint32_t count)
{
	uint32_t i;

	if (!sc->bar1.ready || count == 0)
		return;
	for (i = 0; i < count; i++)
		bus_write_8(nvgpu_device_get_bar(sc->gpu, 1), gva + (uint64_t)i * 8, val);
}

/*
 * nvgsp_bar1_write_linear_region64()
 *
 * Ownership:
 *   Borrows sc and its BAR1 resource.  The helper writes caller-owned BAR1
 *   virtual addresses only; it does not allocate or retain BAR1 mappings.
 *
 * Lifetime:
 *   The caller must guarantee that [gva, gva + count * 8) remains mapped in
 *   BAR1 until the function returns.  first and step are plain qword values;
 *   no pointer ownership is transferred.
 *
 * Threading:
 *   Performs synchronous MMIO writes in caller order.  The caller serializes
 *   access to the object represented by the BAR1 mapping and performs any
 *   required flush/TLB invalidate after the write batch.
 */
void
nvgsp_bar_write_bar1_linear_region64(struct nvgsp_state *sc, uint64_t gva,
    uint64_t first, uint64_t step, uint32_t count)
{
	uint32_t i;

	if (!sc->bar1.ready || count == 0)
		return;
	for (i = 0; i < count; i++)
		bus_write_8(nvgpu_device_get_bar(sc->gpu, 1), gva + (uint64_t)i * 8,
		    first + (uint64_t)i * step);
}

uint64_t
nvgsp_bar_rd64_bar1(struct nvgsp_state *sc, uint64_t gva)
{
	if (!sc->bar1.ready)
		return (0xdeadbeefdeadbeefULL);
	return (bus_read_8(nvgpu_device_get_bar(sc->gpu, 1), gva));
}

static bool
nvgsp_bar_is_bar1_gva_used(struct nvgsp_bar1 *b1, uint32_t idx)
{
	return ((b1->gva_used[idx / 8] & (1u << (idx % 8))) != 0);
}

static void
nvgsp_bar_set_bar1_gva(struct nvgsp_bar1 *b1, uint32_t idx,
    bool used)
{
	uint8_t bit = 1u << (idx % 8);

	if (used)
		b1->gva_used[idx / 8] |= bit;
	else
		b1->gva_used[idx / 8] &= ~bit;
}

static int
nvgsp_bar_alloc_bar1_gva(struct nvgsp_bar1 *b1, uint64_t *pgva)
{
	uint32_t start, idx;

	start = (uint32_t)((b1->next_gva - NVGSP_BAR1_GVA_ALLOC_BASE) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	if (start >= NVGSP_BAR1_GVA_ALLOC_PAGES)
		start = 0;

	for (uint32_t i = 0; i < NVGSP_BAR1_GVA_ALLOC_PAGES; i++) {
		idx = (start + i) % NVGSP_BAR1_GVA_ALLOC_PAGES;
		if (nvgsp_bar_is_bar1_gva_used(b1, idx))
			continue;

		nvgsp_bar_set_bar1_gva(b1, idx, true);
		*pgva = NVGSP_BAR1_GVA_ALLOC_BASE +
		    (uint64_t)idx * NVGSP_GMMU_PT_PAGE_SIZE;
		b1->next_gva = NVGSP_BAR1_GVA_ALLOC_BASE +
		    (uint64_t)((idx + 1) % NVGSP_BAR1_GVA_ALLOC_PAGES) *
		    NVGSP_GMMU_PT_PAGE_SIZE;
		return (0);
	}

	return (ENOSPC);
}

static int
nvgsp_bar_alloc_bar1_gva_range(struct nvgsp_bar1 *b1, uint32_t pages,
    uint64_t *pgva)
{
	uint32_t start;

	if (pages == 0 || pages > NVGSP_BAR1_GVA_ALLOC_PAGES)
		return (EINVAL);

	start = (uint32_t)((b1->next_gva - NVGSP_BAR1_GVA_ALLOC_BASE) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	if (start >= NVGSP_BAR1_GVA_ALLOC_PAGES)
		start = 0;

	for (uint32_t i = 0; i < NVGSP_BAR1_GVA_ALLOC_PAGES; i++) {
		uint32_t idx = (start + i) % NVGSP_BAR1_GVA_ALLOC_PAGES;
		bool used = false;

		if (idx + pages > NVGSP_BAR1_GVA_ALLOC_PAGES)
			continue;

		for (uint32_t page = 0; page < pages; page++) {
			if (nvgsp_bar_is_bar1_gva_used(b1, idx + page)) {
				used = true;
				break;
			}
		}
		if (used)
			continue;

		for (uint32_t page = 0; page < pages; page++)
			nvgsp_bar_set_bar1_gva(b1, idx + page, true);

		*pgva = NVGSP_BAR1_GVA_ALLOC_BASE +
		    (uint64_t)idx * NVGSP_GMMU_PT_PAGE_SIZE;
		b1->next_gva = NVGSP_BAR1_GVA_ALLOC_BASE +
		    (uint64_t)((idx + pages) % NVGSP_BAR1_GVA_ALLOC_PAGES) *
		    NVGSP_GMMU_PT_PAGE_SIZE;
		return (0);
	}

	return (ENOSPC);
}

static void
nvgsp_bar_free_bar1_gva(struct nvgsp_bar1 *b1, uint64_t gva)
{
	uint32_t idx;

	if (gva < NVGSP_BAR1_GVA_ALLOC_BASE ||
	    gva >= NVGSP_BAR1_GVA_ALLOC_BASE +
	    (uint64_t)NVGSP_BAR1_GVA_ALLOC_PAGES * NVGSP_GMMU_PT_PAGE_SIZE ||
	    (gva & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return;

	idx = (uint32_t)((gva - NVGSP_BAR1_GVA_ALLOC_BASE) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	nvgsp_bar_set_bar1_gva(b1, idx, false);
}

static void
nvgsp_bar_free_bar1_gva_range(struct nvgsp_bar1 *b1, uint64_t gva,
    uint32_t pages)
{
	uint32_t idx;

	if (pages == 0 ||
	    gva < NVGSP_BAR1_GVA_ALLOC_BASE ||
	    gva >= NVGSP_BAR1_GVA_ALLOC_BASE +
	    (uint64_t)NVGSP_BAR1_GVA_ALLOC_PAGES * NVGSP_GMMU_PT_PAGE_SIZE ||
	    (gva & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return;

	idx = (uint32_t)((gva - NVGSP_BAR1_GVA_ALLOC_BASE) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	if (idx + pages > NVGSP_BAR1_GVA_ALLOC_PAGES)
		return;

	for (uint32_t page = 0; page < pages; page++)
		nvgsp_bar_set_bar1_gva(b1, idx + page, false);
}


void
nvgsp_bar_count_bar1_gva(struct nvgsp_state *sc, uint32_t *used,
    uint32_t *total)
{
	struct nvgsp_bar1 *b1 = &sc->bar1;
	uint32_t count = 0;

	lwkt_gettoken(&sc->gsp_tok);
	for (uint32_t i = 0; i < NVGSP_BAR1_GVA_ALLOC_PAGES; i++) {
		if (nvgsp_bar_is_bar1_gva_used(b1, i))
			count++;
	}
	*used = count;
	*total = NVGSP_BAR1_GVA_ALLOC_PAGES;
	lwkt_reltoken(&sc->gsp_tok);
}

int
nvgsp_bar_alloc_bar1_page_kind(struct nvgsp_state *sc,
    struct nvgsp_bar1_page *page, enum nvgsp_vram_kind kind, void *owner)
{
	uint64_t paddr, gva;
	int err;

	if (!sc->bar1.ready)
		return (ENXIO);

	lwkt_gettoken(&sc->gsp_tok);
	err = nvgsp_bar_alloc_bar1_gva(&sc->bar1, &gva);
	if (err != 0)
		goto out;

	paddr = nvgsp_vram_alloc_kind(sc, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, kind, owner);
	if (paddr == 0) {
		nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
		err = ENOMEM;
		goto out;
	}

	err = nvgsp_bar_map_bar1_vram(sc, gva, paddr);
	if (err != 0) {
		nvgsp_vram_free_kind(sc, paddr, kind, owner);
		nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
		goto out;
	}
	nvgsp_bar_flush_bar1(sc);

	page->vram_paddr = paddr;
	page->bar1_gva   = gva;
	page->kind = kind;
	page->owner = owner;
out:
	lwkt_reltoken(&sc->gsp_tok);
	return (err);
}

int
nvgsp_bar_alloc_bar1_page(struct nvgsp_state *sc, struct nvgsp_bar1_page *page)
{
	return (nvgsp_bar_alloc_bar1_page_kind(sc, page, NVGSP_VRAM_BAR1_PAGE,
	    &sc->bar1));
}

void
nvgsp_bar_free_bar1_page(struct nvgsp_state *sc, struct nvgsp_bar1_page *page)
{
	lwkt_gettoken(&sc->gsp_tok);
	if (page->bar1_gva != 0) {
		if (nvgsp_bar_clear_bar1_gva(sc, page->bar1_gva) == 0)
			nvgsp_bar_invalidate_bar1(sc);
		nvgsp_bar_free_bar1_gva(&sc->bar1, page->bar1_gva);
	}
	if (page->vram_paddr != 0)
		nvgsp_vram_free_kind(sc, page->vram_paddr, page->kind,
		    page->owner);
	page->vram_paddr = 0;
	page->bar1_gva   = 0;
	page->kind = NVGSP_VRAM_UNKNOWN;
	page->owner = NULL;
	lwkt_reltoken(&sc->gsp_tok);
}

/*
 * Map an EXISTING 4 KiB VRAM page (already-allocated paddr) into BAR1 at the
 * next free GVA, returning the GVA for host reads/writes. Unlike
 * nvgsp_bar1_alloc_page, this does not allocate VRAM -- the caller owns the
 * paddr (e.g. the GSP-handed display instance RAM). paddr must be 4 KiB
 * aligned. Release the GVA with nvgsp_bar1_unmap_existing.
 */
int
nvgsp_bar_map_bar1_existing(struct nvgsp_state *sc, uint64_t paddr, uint64_t *pgva)
{
	uint64_t gva;
	int err;

	if (!sc->bar1.ready)
		return (ENXIO);

	lwkt_gettoken(&sc->gsp_tok);
	err = nvgsp_bar_alloc_bar1_gva(&sc->bar1, &gva);
	if (err != 0)
		goto out;

	err = nvgsp_bar_map_bar1_vram(sc, gva, paddr);
	if (err != 0) {
		nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
		goto out;
	}
	nvgsp_bar_flush_bar1(sc);

	*pgva = gva;
out:
	lwkt_reltoken(&sc->gsp_tok);
	return (err);
}

int
nvgsp_bar_map_bar1_existing_range(struct nvgsp_state *sc, uint64_t paddr,
    uint64_t size, uint64_t *pgva)
{
	uint64_t gva;
	uint32_t pages;
	int err;

	if (!sc->bar1.ready)
		return (ENXIO);
	if (pgva == NULL || size == 0 ||
	    (paddr & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return (EINVAL);

	pages = (uint32_t)((size + NVGSP_GMMU_PT_PAGE_SIZE - 1) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	if (pages == 0 || pages > NVGSP_BAR1_GVA_ALLOC_PAGES)
		return (EINVAL);

	lwkt_gettoken(&sc->gsp_tok);
	err = nvgsp_bar_alloc_bar1_gva_range(&sc->bar1, pages, &gva);
	if (err != 0)
		goto out;

	for (uint32_t page = 0; page < pages; page++) {
		err = nvgsp_bar_map_bar1_vram_pte(sc,
		    gva + (uint64_t)page * NVGSP_GMMU_PT_PAGE_SIZE,
		    paddr + (uint64_t)page * NVGSP_GMMU_PT_PAGE_SIZE);
		if (err != 0) {
			for (uint32_t clear = 0; clear < page; clear++) {
				(void)nvgsp_bar_clear_bar1_gva(sc,
				    gva + (uint64_t)clear *
				    NVGSP_GMMU_PT_PAGE_SIZE);
			}
			if (page != 0)
				nvgsp_bar_invalidate_bar1(sc);
			nvgsp_bar_free_bar1_gva_range(&sc->bar1, gva, pages);
			goto out;
		}
	}
	nvgsp_bar_invalidate_bar1(sc);
	nvgsp_bar_flush_bar1(sc);

	*pgva = gva;
out:
	lwkt_reltoken(&sc->gsp_tok);
	return (err);
}

void
nvgsp_bar_unmap_bar1_existing(struct nvgsp_state *sc, uint64_t gva)
{
	lwkt_gettoken(&sc->gsp_tok);
	if (gva != 0) {
		if (nvgsp_bar_clear_bar1_gva(sc, gva) == 0)
			nvgsp_bar_invalidate_bar1(sc);
		nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
	}
	lwkt_reltoken(&sc->gsp_tok);
}

void
nvgsp_bar_unmap_bar1_existing_range(struct nvgsp_state *sc, uint64_t gva,
    uint64_t size)
{
	uint32_t pages;
	bool cleared = false;

	if (gva == 0 || size == 0)
		return;

	lwkt_gettoken(&sc->gsp_tok);
	pages = (uint32_t)((size + NVGSP_GMMU_PT_PAGE_SIZE - 1) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	for (uint32_t page = 0; page < pages; page++) {
		if (nvgsp_bar_clear_bar1_gva(sc,
		    gva + (uint64_t)page * NVGSP_GMMU_PT_PAGE_SIZE) == 0)
			cleared = true;
	}
	if (cleared)
		nvgsp_bar_invalidate_bar1(sc);
	nvgsp_bar_free_bar1_gva_range(&sc->bar1, gva, pages);
	lwkt_reltoken(&sc->gsp_tok);
}

int
nvgsp_bar_map_vram_range(struct nvgpu_device *gpu, uint64_t paddr,
    uint64_t size, uint64_t *pgva)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	return (nvgsp_bar_map_bar1_existing_range(gsp, paddr, size, pgva));
}

void
nvgsp_bar_unmap_vram_range(struct nvgpu_device *gpu, uint64_t gva,
    uint64_t size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp != NULL)
		nvgsp_bar_unmap_bar1_existing_range(gsp, gva, size);
}

/*
 * nvgsp_bar1_map_existing_scatter()
 *
 * Ownership:
 *   Borrows the caller-owned VRAM range and caller-owned gvas array.  On
 *   success, each non-zero array entry owns one BAR1 GVA loan for the matching
 *   VRAM page.  The caller must release those loans with
 *   nvgsp_bar1_unmap_existing_scatter().
 *
 * Lifetime:
 *   The returned GVA entries remain valid until explicit unmap or BAR1 teardown.
 *   They do not own the underlying VRAM allocation; the caller must keep that
 *   allocation alive while any GVA entry can be used by a CPU PTE.
 *
 * Threading:
 *   Serializes the device-wide BAR1 allocator with gsp_tok.  This function may
 *   sleep through GSP/BAR1 helper paths and is not IRQ-safe.
 */
int
nvgsp_bar_map_bar1_existing_scatter(struct nvgsp_state *sc, uint64_t paddr,
    uint64_t size, uint64_t *gvas, uint32_t count)
{
	uint32_t pages;
	int err;

	if (!sc->bar1.ready)
		return (ENXIO);
	if (gvas == NULL || count == 0 || size == 0 ||
	    (paddr & (NVGSP_GMMU_PT_PAGE_SIZE - 1)) != 0)
		return (EINVAL);

	pages = (uint32_t)((size + NVGSP_GMMU_PT_PAGE_SIZE - 1) /
	    NVGSP_GMMU_PT_PAGE_SIZE);
	if (pages == 0 || pages > count)
		return (EINVAL);

	lwkt_gettoken(&sc->gsp_tok);
	for (uint32_t page = 0; page < pages; page++) {
		uint64_t gva;

		err = nvgsp_bar_alloc_bar1_gva(&sc->bar1, &gva);
		if (err != 0)
			goto fail;

		err = nvgsp_bar_map_bar1_vram_pte(sc, gva,
		    paddr + (uint64_t)page * NVGSP_GMMU_PT_PAGE_SIZE);
		if (err != 0) {
			nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
			goto fail;
		}
		gvas[page] = gva;
	}
	nvgsp_bar_invalidate_bar1(sc);
	nvgsp_bar_flush_bar1(sc);
	lwkt_reltoken(&sc->gsp_tok);
	return (0);

fail:
	nvgsp_bar_unmap_bar1_existing_scatter(sc, gvas, pages);
	lwkt_reltoken(&sc->gsp_tok);
	return (err);
}

/*
 * nvgsp_bar1_unmap_existing_scatter()
 *
 * Ownership:
 *   Consumes BAR1 GVA loans stored in gvas and clears each consumed entry to
 *   zero.  It does not release the caller-owned array or the VRAM allocation.
 *
 * Lifetime:
 *   After return, CPU mappings that still reference the old fictitious BAR1 PFNs
 *   are invalid and must already have been removed by the VM/TTM owner.
 *
 * Threading:
 *   Serializes the device-wide BAR1 allocator with gsp_tok and batches PTE
 *   invalidation for all released pages.
 */
void
nvgsp_bar_unmap_bar1_existing_scatter(struct nvgsp_state *sc, uint64_t *gvas,
    uint32_t count)
{
	bool cleared = false;

	if (gvas == NULL || count == 0)
		return;

	lwkt_gettoken(&sc->gsp_tok);
	for (uint32_t page = 0; page < count; page++) {
		uint64_t gva = gvas[page];

		if (gva == 0)
			continue;
		if (nvgsp_bar_clear_bar1_gva(sc, gva) == 0)
			cleared = true;
		nvgsp_bar_free_bar1_gva(&sc->bar1, gva);
		gvas[page] = 0;
	}
	if (cleared)
		nvgsp_bar_invalidate_bar1(sc);
	lwkt_reltoken(&sc->gsp_tok);
}

void
nvgsp_bar_dump_bar1_pt(struct nvgsp_state *sc __unused,
    uint64_t target_paddr __unused, uint32_t target_off __unused)
{
}

int
nvgsp_bar_init_bar2(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	return (nvgsp_bar_start_bar2(gsp));
}

void
nvgsp_bar_fini_bar2(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp != NULL)
		nvgsp_bar_stop_bar2(gsp);
}

int
nvgsp_bar_init_bar1(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	return (nvgsp_bar_start_bar1(gsp));
}

void
nvgsp_bar_fini_bar1(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp != NULL)
		nvgsp_bar_stop_bar1(gsp);
}
