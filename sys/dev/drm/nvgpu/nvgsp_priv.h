/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Private GSP backend state shared only by nvgsp translation units.
 */

#ifndef _NVGSP_PRIV_H_
#define _NVGSP_PRIV_H_

#include "nvgpu_chip.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgsp_abi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/firmware.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <machine/atomic.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include <drm/drm_mm.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#define NVGSP_PAGE_SIZE 4096u
#define NVGSP_PAGE_SHIFT 12
#define NVGSP_PCI_VENDOR_NVIDIA 0x10de

#define NV_PMC_BOOT_0          0x00000000u
#define NV_PMC_PCI_CFG         0x00088000u
#define NV_PMC_ROM_SHADOW      (NV_PMC_PCI_CFG + 0x50u)
#define NV_PMC_ROM_SHADOW_EN   0x00000001u
#define NV_PROM                0x00300000u
#define NV_PROM_SIZE           0x00100000u
#define NV_PRAMIN              0x00700000u
#define NV_PRAMIN_SIZE         0x00100000u
#define NV_PBUS_PRAMIN         0x00001700u
#define NV_PDISP_VGA_CR        0x00625f04u
#define NV_PDISP_VGA_CR_TARGET_VRAM 0x00000001u
#define NV_PDISP_VGA_CR_TARGET_MASK 0x00000003u
#define NV_PDISP_VGA_CR_ENABLED 0x00000008u
#define NV_PDISP_GENERAL_CTL   0x00021c04u
#define NV_PDISP_GENERAL_CTL_DISABLED 0x00000001u
#define NVGSP_VBIOS_MAX_SIZE   0x100000u

#define NVGSP_FW_WPR_META_MAGIC    0xdc3aae21371a60b3ULL
#define NVGSP_FW_WPR_META_REVISION 1ULL
#define NVGSP_FW_WPR_META_SIZE     256u
#define NVGSP_NTFY_MAX             16u
#define NVGSP_RPC_TRACE_N          512u
#define NVGSP_FWSEC_CMD_FRTS       0x15u
#define NVGSP_FWSEC_CMD_SB         0x19u

struct nvgpu_bo;
struct nvgsp_falcon;
struct nvgsp_display;
struct firmware;

struct nvgsp_dmamem {
	void *kva;
	bus_addr_t paddr;
	bus_size_t size;
	bus_dma_tag_t tag;
	bus_dmamap_t map;
};

struct nvgsp_booter_info {
	const uint8_t *blob;
	uint32_t blob_size;
	uint32_t data_offset;
	uint32_t data_size;
	uint32_t nmem_offset;
	uint32_t nmem_size;
	uint32_t imem_offset;
	uint32_t imem_size;
	uint32_t dmem_offset;
	uint32_t dmem_size;
	uint32_t boot_addr;
	uint32_t sig_prod_offset;
	uint32_t sig_prod_size;
	uint32_t patch_loc;
	uint32_t patch_sig;
	uint32_t num_sig;
};

struct nvgsp_wpr_meta {
	uint64_t magic;
	uint64_t revision;
	uint64_t sysmemAddrOfRadix3Elf;
	uint64_t sizeOfRadix3Elf;
	uint64_t sysmemAddrOfBootloader;
	uint64_t sizeOfBootloader;
	uint64_t bootloaderCodeOffset;
	uint64_t bootloaderDataOffset;
	uint64_t bootloaderManifestOffset;
	uint64_t sysmemAddrOfSignature;
	uint64_t sizeOfSignature;
	uint64_t gspFwRsvdStart;
	uint64_t nonWprHeapOffset;
	uint64_t nonWprHeapSize;
	uint64_t gspFwWprStart;
	uint64_t gspFwHeapOffset;
	uint64_t gspFwHeapSize;
	uint64_t gspFwOffset;
	uint64_t bootBinOffset;
	uint64_t frtsOffset;
	uint64_t frtsSize;
	uint64_t gspFwWprEnd;
	uint64_t fbSize;
	uint64_t vgaWorkspaceOffset;
	uint64_t vgaWorkspaceSize;
	uint64_t bootCount;
	uint64_t partitionRpcAddr;
	uint16_t partitionRpcRequestOffset;
	uint16_t partitionRpcReplyOffset;
	uint32_t elfCodeOffset;
	uint32_t elfDataOffset;
	uint32_t elfCodeSize;
	uint32_t elfDataSize;
	uint32_t lsUcodeVersion;
	uint8_t gspFwHeapVfPartitionCount;
	uint8_t flags;
	uint8_t padding[2];
	uint32_t pmuReservedSize;
	uint64_t verified;
} __packed;

struct nvgsp_pending {
	LIST_ENTRY(nvgsp_pending) link;
	uint32_t fn;
	uint32_t seq;
	uint64_t tx_us;
	volatile u_int done;
	void *reply_buf;
	uint32_t reply_len;
};
LIST_HEAD(nvgsp_pending_list, nvgsp_pending);

enum nvgsp_rpc_dir {
	NVGSP_RPC_TX = 0,
	NVGSP_RPC_RX = 1,
	NVGSP_RPC_EVENT = 2,
	NVGSP_RPC_STALE = 3,
};

struct nvgsp_rpc_trace_ent {
	uint64_t time_us;
	uint32_t fn;
	uint32_t seq;
	uint32_t aux;
	uint32_t aux2;
	uint32_t latency_us;
	uint8_t dir;
};

typedef int (*nvgsp_msg_ntfy_func)(void *priv, uint32_t fn, void *repv,
    uint32_t repc);



#define NVGSP_ALIGN_UP(v, a) (((v) + (a) - 1) & ~((a) - 1))

enum nvgsp_vram_kind {
	NVGSP_VRAM_UNKNOWN = 0,
	NVGSP_VRAM_GEM,
	NVGSP_VRAM_BAR1_SPT,
	NVGSP_VRAM_BAR1_PAGE,
	NVGSP_VRAM_BAR2_ROOT,
	NVGSP_VRAM_BAR2_PT,
	NVGSP_VRAM_BAR2_FLUSH,
	NVGSP_VRAM_BAR2_TEST,
	NVGSP_VRAM_VMM_PT,
	NVGSP_VRAM_CHANNEL_GOLDEN,
	NVGSP_VRAM_CHANNEL_INST,
	NVGSP_VRAM_CHANNEL_USERD,
	NVGSP_VRAM_CHANNEL_SUBMIT_PT,
	NVGSP_VRAM_GR_CTXBUF_GLOBAL,
	NVGSP_VRAM_GR_CTXBUF_CHANNEL,
	NVGSP_VRAM_DISPLAY_INST,
	NVGSP_VRAM_DISPLAY_DATA,
	NVGSP_VRAM_KIND_COUNT
};

struct nvgsp_vram_alloc {
	TAILQ_ENTRY(nvgsp_vram_alloc) link;
	struct drm_mm_node node;
	uint64_t paddr;
	uint64_t size;
	uint64_t align;
	uint64_t bar1_gva;
	uint64_t bar1_size;
	uint64_t *bar1_page_gva;
	uint32_t bar1_page_count;
	enum nvgsp_vram_kind kind;
	void *owner;
};
TAILQ_HEAD(nvgsp_vram_alloc_list, nvgsp_vram_alloc);

#define NVGSP_PT_ADDR_SHIFT          4
#define NVGSP_PDE_APERTURE_VRAM      (1ULL << 1)
#define NVGSP_PDE_VOL                (1ULL << 3)
#define NVGSP_PTE_VALID              (1ULL << 0)
#define NVGSP_PTE_APERTURE_VRAM      0ULL
#define NVGSP_PTE_APERTURE_SYS_COH   (2ULL << 1)
#define NVGSP_PTE_VOL                (1ULL << 3)
#define NVGSP_PTE_PRIV               (1ULL << 5)
#define NVGSP_PTE_RO                 (1ULL << 6)
#define NVGSP_PTE_KIND_SHIFT         56

#define NVGSP_GMMU_PD3_SHIFT         47
#define NVGSP_GMMU_PD2_SHIFT         38
#define NVGSP_GMMU_PD1_SHIFT         29
#define NVGSP_GMMU_PD0_SHIFT         21
#define NVGSP_GMMU_LPT_SHIFT         16
#define NVGSP_GMMU_SPT_SHIFT         12
#define NVGSP_GMMU_PD3_ENTRIES       4
#define NVGSP_GMMU_PD2_ENTRIES       512
#define NVGSP_GMMU_PD1_ENTRIES       512
#define NVGSP_GMMU_PD0_ENTRIES       256
#define NVGSP_GMMU_LPT_ENTRIES       (1U << (NVGSP_GMMU_PD0_SHIFT - NVGSP_GMMU_LPT_SHIFT))
#define NVGSP_GMMU_SPT_ENTRIES       512
#define NVGSP_GMMU_LPT_SPTE_COUNT    (1U << (NVGSP_GMMU_LPT_SHIFT - NVGSP_GMMU_SPT_SHIFT))
#define NVGSP_GMMU_PT_PAGE_SIZE      0x1000u
#define NVGSP_GMMU_PD0_PAGE_SIZE     (1ULL << NVGSP_GMMU_PD0_SHIFT)
#define NVGSP_GMMU_LPT_PAGE_SIZE     (1ULL << NVGSP_GMMU_LPT_SHIFT)

#define NVGSP_VMM_RM_BASE            0x000100000000ULL
#define NVGSP_VMM_RM_SIZE            0x000020000000ULL
#define NVGSP_VMM_CLIENT_BASE        0x000400000000ULL
#define NVGSP_VMM_CLIENT_SIZE        0x001000000000ULL

#define NVGSP_BAR1_PD0_MANAGED_FIRST 96u
#define NVGSP_BAR1_PD0_MANAGED_LAST  127u
#define NVGSP_BAR1_PD0_MANAGED_COUNT \
	(NVGSP_BAR1_PD0_MANAGED_LAST - NVGSP_BAR1_PD0_MANAGED_FIRST + 1u)
#define NVGSP_BAR1_GVA_ALLOC_BASE \
	((uint64_t)NVGSP_BAR1_PD0_MANAGED_FIRST * (2ULL << 20))
#define NVGSP_BAR1_GVA_ALLOC_PAGES   (NVGSP_BAR1_PD0_MANAGED_COUNT * 512u)
#define NVGSP_BAR1_GVA_BITMAP_SIZE   (NVGSP_BAR1_GVA_ALLOC_PAGES / 8u)
#define NVGSP_BAR2_GVA_FLUSH         0x0ULL

struct nvgsp_bar1_page {
	uint64_t vram_paddr;
	uint64_t bar1_gva;
	enum nvgsp_vram_kind kind;
	void *owner;
};

struct nvgsp_bar1 {
	uint64_t pd3_paddr;
	uint64_t pd2_paddr;
	uint64_t pd1_paddr;
	uint64_t pd0_paddr;
	uint64_t spt_paddr[NVGSP_BAR1_PD0_MANAGED_COUNT];
	uint64_t next_gva;
	uint8_t gva_used[NVGSP_BAR1_GVA_BITMAP_SIZE];
	vm_paddr_t fictitious_start;
	vm_paddr_t fictitious_end;
	bool ready;
	bool fictitious_registered;
};

struct nvgsp_bar2_pt {
	LIST_ENTRY(nvgsp_bar2_pt) link;
	uint8_t level;
	uint16_t pd2_idx;
	uint16_t pd1_idx;
	uint16_t pd0_idx;
	uint64_t paddr;
};
LIST_HEAD(nvgsp_bar2_pt_list, nvgsp_bar2_pt);

struct nvgsp_bar2 {
	uint64_t pd3_paddr;
	uint64_t pd2_paddr;
	uint64_t pd1_paddr;
	uint64_t pd0_paddr;
	uint64_t spt_paddr;
	uint64_t aperture_size;
	uint64_t next_gva;
	uint64_t flush_vram_paddr;
	struct nvgsp_bar2_pt_list pt_pages;
	bool ready;
};

static __inline uint64_t
nvgsp_pde_to_vram(uint64_t paddr)
{
	return ((paddr >> NVGSP_PT_ADDR_SHIFT) | NVGSP_PDE_APERTURE_VRAM);
}

static __inline uint64_t
nvgsp_pde_to_sparse(void)
{
	return (NVGSP_PDE_VOL);
}

static __inline uint64_t
nvgsp_pte_to_sysmem(uint64_t paddr)
{
	return ((paddr >> NVGSP_PT_ADDR_SHIFT) | NVGSP_PTE_APERTURE_SYS_COH |
	    NVGSP_PTE_VOL | NVGSP_PTE_VALID);
}

static __inline uint64_t
nvgsp_pte_to_sparse(void)
{
	return (NVGSP_PTE_VOL);
}

static __inline uint64_t
nvgsp_pte_to_vram_flags(uint64_t paddr, uint8_t priv, uint8_t ro, uint8_t kind)
{
	uint64_t pte = (paddr >> NVGSP_PT_ADDR_SHIFT) | NVGSP_PTE_APERTURE_VRAM |
	    NVGSP_PTE_VALID | ((uint64_t)kind << NVGSP_PTE_KIND_SHIFT);

	if (priv)
		pte |= NVGSP_PTE_PRIV;
	if (ro)
		pte |= NVGSP_PTE_RO;
	return (pte);
}


struct nvgsp_object {
	struct nvgsp_client *client;
	struct nvgsp_object *parent;
	uint32_t handle;
};

struct nvgsp_client {
	struct nvgsp_object object;
	struct nvgsp_state *gsp;
};

struct nvgsp_device {
	struct nvgsp_object object;
	struct nvgsp_object subdevice;
};

struct nvgsp_vmm_pt {
	struct nvgsp_bar1_page page;
};

struct nvgsp_vmm_pd1 {
	LIST_ENTRY(nvgsp_vmm_pd1) link;
	uint32_t pd2_idx;
	struct nvgsp_bar1_page page;
};
LIST_HEAD(nvgsp_vmm_pd1_list, nvgsp_vmm_pd1);

enum nvgsp_vmm_pd0_slot_state {
	NVGSP_VMM_PD0_SLOT_EMPTY = 0,
	NVGSP_VMM_PD0_SLOT_CHILD,
	NVGSP_VMM_PD0_SLOT_VALID_2M,
	NVGSP_VMM_PD0_SLOT_SPARSE_2M,
};

struct nvgsp_vmm_pd0 {
	LIST_ENTRY(nvgsp_vmm_pd0) link;
	LIST_ENTRY(nvgsp_vmm_pd0) lookup_link;
	struct nvgsp_bar1_page *pd1_page;
	uint32_t pd2_idx;
	uint32_t pd1_idx;
	uint32_t refcount;
	uint32_t valid_2m_count;
	uint32_t sparse_2m_count;
	uint8_t slot_state[NVGSP_GMMU_PD0_ENTRIES];
	struct nvgsp_bar1_page page;
};
LIST_HEAD(nvgsp_vmm_pd0_list, nvgsp_vmm_pd0);
LIST_HEAD(nvgsp_vmm_pd0_lookup_list, nvgsp_vmm_pd0);

#define NVGSP_VMM_SPT_MASK_WORDS (NVGSP_GMMU_SPT_ENTRIES / 64)

struct nvgsp_vmm_user_pt {
	LIST_ENTRY(nvgsp_vmm_user_pt) link;
	LIST_ENTRY(nvgsp_vmm_user_pt) lookup_link;
	struct nvgsp_vmm_pd0 *pd0;
	uint32_t pd2_idx;
	uint32_t pd1_idx;
	uint32_t pd0_idx;
	uint32_t valid_pte_count;
	uint32_t valid_lpte_count;
	uint32_t sparse_pte_count;
	uint32_t sparse_lpte_count;
	uint64_t valid_spt_mask[NVGSP_VMM_SPT_MASK_WORDS];
	uint64_t sparse_spt_mask[NVGSP_VMM_SPT_MASK_WORDS];
	uint32_t valid_lpt_mask;
	uint32_t sparse_lpt_mask;
	struct nvgsp_bar1_page lpt;
	struct nvgsp_bar1_page spt;
};
LIST_HEAD(nvgsp_vmm_user_pt_list, nvgsp_vmm_user_pt);
LIST_HEAD(nvgsp_vmm_user_pt_lookup_list, nvgsp_vmm_user_pt);

#define NVGSP_VMM_PD0_HASH_BITS 8
#define NVGSP_VMM_PD0_HASH_SIZE (1U << NVGSP_VMM_PD0_HASH_BITS)
#define NVGSP_VMM_USER_PT_HASH_BITS 10
#define NVGSP_VMM_USER_PT_HASH_SIZE (1U << NVGSP_VMM_USER_PT_HASH_BITS)
#define NVGSP_VMM_PAGE_SHIFT_4K 0
#define NVGSP_VMM_PAGE_SHIFT_64K 1
#define NVGSP_VMM_PAGE_SHIFT_2M 2
#define NVGSP_VMM_PAGE_SHIFT_COUNT 3

struct nvgsp_vmm_sparse_region {
	LIST_ENTRY(nvgsp_vmm_sparse_region) link;
	uint64_t addr;
	uint64_t size;
	uint8_t page_shift;
};
LIST_HEAD(nvgsp_vmm_sparse_region_list, nvgsp_vmm_sparse_region);

struct nvgsp_vmm_stats {
	uint64_t pte_backend_flush_count;
	uint64_t dirty_flush_count;
	uint64_t dirty_flush_range_count;
	uint64_t dirty_flush_pages;
	uint64_t dirty_flush_overflow_count;
	uint64_t dirty_flush_all_fallback_count;
	uint64_t flush_count;
	uint64_t flush_us;
	uint64_t pd0_empty_free_count;
	uint64_t pt_empty_free_count;
	uint64_t pt_skip_clear_count;
	uint64_t pt_skip_clear_pages;
	uint64_t pt_conflict_clear_count;
	uint64_t pt_conflict_clear_pages;
	uint64_t pt_final_clear_count;
	uint64_t pt_final_clear_pages;
	uint64_t pte_bulk_write_count;
	uint64_t pte_bulk_write_pages;
	uint64_t pte_bulk_clear_count;
	uint64_t pte_bulk_clear_pages;
	uint64_t pte_skip_clear_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_skip_clear_pages[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_conflict_clear_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_conflict_clear_pages[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_final_clear_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_final_clear_pages[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_leaf_write_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_leaf_clear_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_write_batch_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_clear_batch_count[NVGSP_VMM_PAGE_SHIFT_COUNT];
	uint64_t pte_fast_write_count;
	uint64_t pte_fast_clear_count;
	uint64_t pte_fast_invalid_clear_count;
	uint64_t pte_fast_sparse_clear_count;
	uint64_t pte_read_modify_write_count;
};

struct nvgsp_vmm {
	struct nvgsp_state *gsp;
	struct lwkt_token tok;
	struct nvgsp_client client;
	struct nvgsp_device device;
	struct nvgsp_object vaspace;
	struct nvgsp_object usermode;
	uint8_t external_page_directory;
	struct nvgsp_vmm_pt pt[3];
	uint64_t rm_va_base;
	uint64_t rm_va_size;
	struct drm_mm kernel_va_mm;
	uint64_t kernel_va_addr;
	uint64_t kernel_va_size;
	uint8_t kernel_va_ready;
	struct nvgsp_vmm_pd1_list user_pd1_pages;
	struct nvgsp_vmm_pd0_list user_pd0_pages;
	struct nvgsp_vmm_user_pt_list user_pt_pages;
	struct nvgsp_vmm_pd0_lookup_list user_pd0_lookup[NVGSP_VMM_PD0_HASH_SIZE];
	struct nvgsp_vmm_user_pt_lookup_list user_pt_lookup[NVGSP_VMM_USER_PT_HASH_SIZE];
	struct nvgsp_vmm_sparse_region_list sparse_regions;
	struct nvgsp_dmamem sparse_page;
	struct nvgsp_vmm_stats stats;
	int pt_alloc_fail_after;
	uint64_t pt_alloc_fail_count;
	uint64_t pt_alloc_fail_last_va;
	uint32_t submit_gva_slot;
};

#define NVGSP_GR_MAX_CTXBUFS 16
#define NVGSP_CHANNEL_POST_RING_SLOTS 64u

struct nvgsp_gr_ctxbuf {
	void *kva;
	vm_paddr_t paddr;
	uint64_t size;
	uint64_t gva;
	uint32_t buffer_id;
	uint8_t target;
	uint8_t init;
	uint8_t ro;
	uint8_t nonmapped;
};

struct nvgsp_channel {
	struct nvgsp_object object;
	struct nvgsp_vmm *vmm;
	uint64_t inst_vram;
	uint64_t userd_vram;
	uint64_t userd_bar1_gva;
	uint64_t inst_bar1_gva;
	struct nvgsp_dmamem mthdbuf;
	int chid;
	uint32_t engine_type;
	uint32_t gsp_token;
	uint32_t gpf_put;
	uint32_t gpf_free;
	uint32_t submit_post_slot;
	uint32_t submit_payload;
	uint64_t submit_post_slots_busy;
	uint32_t submit_post_payload[NVGSP_CHANNEL_POST_RING_SLOTS];
	uint8_t faulted;
	uint8_t fault_dumped;
	int fault_error;
	struct nvgsp_object ce_obj;
	struct nvgsp_object usermode_obj;
	uint64_t submit_gva_push;
	uint64_t submit_gva_gpf;
	uint64_t submit_gva_sema;
	uint8_t gr_ctx_promoted;
	uint8_t gr_ctxbuf_nr;
	struct nvgsp_gr_ctxbuf gr_ctxbuf[NVGSP_GR_MAX_CTXBUFS];
	struct nvgsp_dmamem submit_push;
	struct nvgsp_dmamem submit_gpf;
	struct nvgsp_dmamem submit_sema;
};

struct nvgsp_state {
	struct nvgpu_device *gpu;
	device_t dev;
	const struct nvgpu_chip_config *chip;
	uint8_t *vbios;
	uint32_t vbios_size;
	const struct firmware *fw_booter_load;
	const struct firmware *fw_booter_unload;
	struct nvgsp_falcon *sec2;
	struct nvgsp_falcon *gsp;
	struct nvgsp_booter_info booter;
	struct nvgsp_dmamem booter_dma;
	struct nvgsp_dmamem wpr_meta;
	struct nvgsp_dmamem gsp_image;
	struct nvgsp_dmamem gsp_radix3;
	struct nvgsp_dmamem gsp_bl;
	struct nvgsp_dmamem gsp_sig;
	uint32_t gsp_sig_size;
	uint32_t gsp_fwimage_off;
	struct nvgsp_dmamem gsp_libos;
	struct nvgsp_dmamem gsp_loginit;
	struct nvgsp_dmamem gsp_logintr;
	struct nvgsp_dmamem gsp_logrm;
	struct nvgsp_dmamem gsp_shm;
	struct nvgsp_dmamem gsp_rmargs;
	uint32_t gsp_shm_ptes_nr;
	uint32_t gsp_shm_ptes_size;
	uint32_t gsp_shm_cmdq_off;
	uint32_t gsp_shm_msgq_off;
	uint32_t gsp_cmdq_seq;
	uint32_t gsp_internal_client;
	uint32_t gsp_internal_device;
	uint32_t gsp_internal_subdevice;
	struct nvgsp_bar1 bar1;
	struct nvgsp_bar2 bar2;
	uint64_t gsp_bar1_pdb;
	uint64_t gsp_bar2_pdb;
	uint64_t fb_usable_base;
	uint64_t fb_usable_size;
	bool gsp_running;
	struct {
		struct {
			uint32_t fn;
			nvgsp_msg_ntfy_func func;
			void *priv;
		} tab[NVGSP_NTFY_MAX];
		uint32_t cnt;
	} gsp_ntfy;
	uint32_t gsp_rpc_seq;
	uint32_t gsp_msgq_rptr;
	struct nvgsp_rpc_trace_ent gsp_rpc_trace[NVGSP_RPC_TRACE_N];
	uint32_t gsp_rpc_trace_head;
	int gsp_rpc_trace_on;
	struct lwkt_token gsp_tok;
	struct nvgsp_pending_list gsp_pending;
	uint64_t gsp_msgq_null_event_drop_bytes;
	uint64_t gsp_msgq_null_event_drop_count;
	uint32_t gsp_msgq_null_event_last_fn;
	uint32_t mthdbuf_size;
	uint32_t gsp_nonstall_leaf_mask[8];
	uint32_t gsp_stall_leaf_mask[8];
	uint32_t gsp_engine_leaf_mask[8];
	uint32_t gsp_disp_leaf_mask[8];
	uint64_t vram_bump_base;
	uint64_t vram_bump_limit;
	struct drm_mm vram_mm;
	struct lock vram_lock;
	struct nvgsp_vram_alloc_list vram_allocs;
	uint64_t vram_alloc_bytes[NVGSP_VRAM_KIND_COUNT];
	uint32_t vram_alloc_count[NVGSP_VRAM_KIND_COUNT];
	struct lwkt_token chid_tok;
	uint64_t chid_used[32];
	struct nvgsp_channel *chid_channel[2048];
	struct nvgsp_vmm *kernel_vmm;
	struct nvgsp_vmm *golden_vmm;
	struct nvgsp_channel *bootstrap_channel;
	struct nvgsp_channel *golden_channel;
	struct nvgsp_display *display;
	uint8_t gr_ctxbuf_global_nr;
	struct nvgsp_gr_ctxbuf gr_ctxbuf_global[NVGSP_GR_MAX_CTXBUFS];
};

static __inline uint32_t
nvgsp_rd32(struct nvgsp_state *gsp, uint32_t offset)
{
	return (nvgpu_device_rd32(gsp->gpu, offset));
}

static __inline void
nvgsp_wr32(struct nvgsp_state *gsp, uint32_t offset, uint32_t val)
{
	nvgpu_device_wr32(gsp->gpu, offset, val);
}

static __inline uint16_t
nvgsp_le16(const uint8_t *p)
{
	return ((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static __inline uint32_t
nvgsp_le32(const uint8_t *p)
{
	return ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static __inline uint32_t
nvgsp_le24(const uint8_t *p)
{
	return ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16));
}


/* Decode one GSP display event in process context and invoke borrowed callbacks. */
int nvgsp_disp_dispatch_event(struct nvgpu_device *gpu, uint32_t client_handle,
	uint32_t event_handle, const void *data, uint32_t size);

/* Wait for GSP init-done during boot; may sleep. */
int nvgsp_event_poll_init_done(struct nvgpu_device *gpu);

/* Enable doorbells before channel submission. */
int nvgsp_state_enable_doorbell(struct nvgpu_device *gpu);

/* Retrieve interrupt routing metadata before nvgpu_intr_init(). */
int nvgsp_state_get_intr_table(struct nvgpu_device *gpu);

/* Query method-buffer size before channel creation. */
int nvgsp_state_query_mthdbuf_size(struct nvgpu_device *gpu);

/* Query static GPU info from GSP during boot. */
int nvgsp_state_query_static_info(struct nvgpu_device *gpu);

struct nvgsp_state *nvgsp_state_get(struct nvgpu_device *gpu);
int nvgsp_dma_alloc_dmamem(struct nvgsp_state *gsp, bus_size_t size,
    bus_size_t alignment, struct nvgsp_dmamem *out);
void nvgsp_dma_free_dmamem(struct nvgsp_state *gsp, struct nvgsp_dmamem *mem);

int nvgsp_bios_init(struct nvgsp_state *gsp);
void nvgsp_bios_fini(struct nvgsp_state *gsp);
int nvgsp_fw_init(struct nvgsp_state *gsp);
void nvgsp_fw_fini(struct nvgsp_state *gsp);
int nvgsp_falcon_init_state(struct nvgsp_state *gsp);
void nvgsp_falcon_fini_state(struct nvgsp_state *gsp);
int nvgsp_fwsec_run_cmd(struct nvgsp_state *gsp, uint32_t init_cmd,
    uint64_t frts_addr, uint32_t frts_size);
int nvgsp_meta_init(struct nvgsp_state *gsp);
void nvgsp_meta_fini(struct nvgsp_state *gsp);
int nvgsp_boot_prepare_image(struct nvgsp_state *gsp);
void nvgsp_boot_release_image(struct nvgsp_state *gsp);
int nvgsp_libos_prepare(struct nvgsp_state *gsp);
void nvgsp_libos_release(struct nvgsp_state *gsp);
int nvgsp_booter_parse(struct nvgsp_state *gsp, const struct firmware *fw,
    struct nvgsp_booter_info *info);
int nvgsp_booter_run(struct nvgsp_state *gsp,
    const struct nvgsp_booter_info *bi, uint32_t mb0, uint32_t mb1);
void nvgsp_booter_release(struct nvgsp_state *gsp);
void nvgsp_shutdown_backend(struct nvgsp_state *gsp);
int nvgsp_static_query_info(struct nvgsp_state *gsp);

void *nvgsp_rm_get_alloc(struct nvgsp_object *parent, uint32_t handle,
    uint32_t oclass, uint32_t params_size, struct nvgsp_object *new_obj);
int nvgsp_rm_write_alloc(struct nvgsp_object *obj, void *params);
int nvgsp_rm_read_alloc(struct nvgsp_object *obj, void **params, uint32_t repc);
void nvgsp_rm_complete_alloc(struct nvgsp_object *obj, void *params);
void *nvgsp_rm_get_ctrl(struct nvgsp_object *obj, uint32_t cmd,
    uint32_t params_size);
int nvgsp_rm_read_ctrl(struct nvgsp_object *obj, void **params, uint32_t repc);
int nvgsp_rm_write_ctrl(struct nvgsp_object *obj, void *params);
void nvgsp_rm_complete_ctrl(struct nvgsp_object *obj, void *params);
int nvgsp_rm_free(struct nvgsp_object *obj);
int nvgsp_rm_construct_client(struct nvgsp_state *gsp, uint32_t handle,
    struct nvgsp_client *client);
int nvgsp_rm_destroy_client(struct nvgsp_client *client);
int nvgsp_rm_construct_device(struct nvgsp_client *client, struct nvgsp_device *device);
int nvgsp_rm_destroy_device(struct nvgsp_device *device);
int nvgsp_rm_construct_vaspace(struct nvgsp_device *device, struct nvgsp_object *vaspace);
uint64_t nvgsp_vram_alloc_kind(struct nvgsp_state *gsp, uint64_t size,
    uint64_t align, enum nvgsp_vram_kind kind, void *owner);
void nvgsp_vram_free_kind(struct nvgsp_state *gsp, uint64_t paddr,
    enum nvgsp_vram_kind kind, void *owner);
uint32_t nvgsp_vram_free_owner(struct nvgsp_state *gsp, void *owner);
int nvgsp_channel_alloc_chid(struct nvgsp_state *gsp);
void nvgsp_channel_free_chid(struct nvgsp_state *gsp, int chid);
void nvgsp_bar_flush_bar1(struct nvgsp_state *gsp);
void nvgsp_bar_invalidate_bar1(struct nvgsp_state *gsp);
void nvgsp_bar_wr32_bar1(struct nvgsp_state *gsp, uint64_t gva, uint32_t val);
uint32_t nvgsp_bar_rd32_bar1(struct nvgsp_state *gsp, uint64_t gva);
void nvgsp_bar_wr64_bar1(struct nvgsp_state *gsp, uint64_t gva, uint64_t val);
uint64_t nvgsp_bar_rd64_bar1(struct nvgsp_state *gsp, uint64_t gva);
void nvgsp_bar_set_bar1_region64(struct nvgsp_state *gsp, uint64_t gva,
    uint64_t val, uint32_t count);
void nvgsp_bar_write_bar1_linear_region64(struct nvgsp_state *gsp, uint64_t gva,
    uint64_t first, uint64_t step, uint32_t count);
int nvgsp_bar_alloc_bar1_page_kind(struct nvgsp_state *gsp,
    struct nvgsp_bar1_page *page, enum nvgsp_vram_kind kind, void *owner);
void nvgsp_bar_free_bar1_page(struct nvgsp_state *gsp, struct nvgsp_bar1_page *page);
int nvgsp_bar_map_bar1_existing(struct nvgsp_state *gsp, uint64_t paddr,
    uint64_t *pgva);
void nvgsp_bar_unmap_bar1_existing(struct nvgsp_state *gsp, uint64_t gva);
int nvgsp_bar_map_bar1_existing_range(struct nvgsp_state *gsp, uint64_t paddr,
    uint64_t size, uint64_t *pgva);
void nvgsp_bar_unmap_bar1_existing_range(struct nvgsp_state *gsp, uint64_t gva,
    uint64_t size);
int nvgsp_bar_map_bar1_existing_scatter(struct nvgsp_state *gsp,
    uint64_t paddr, uint64_t size, uint64_t *gvas, uint32_t count);
void nvgsp_bar_unmap_bar1_existing_scatter(struct nvgsp_state *gsp,
    uint64_t *gvas, uint32_t count);
int nvgsp_bar_map_bar2_vram(struct nvgsp_state *gsp, uint64_t gva, uint64_t paddr);
void nvgsp_bar_flush_bar2(struct nvgsp_state *gsp);
void nvgsp_bar_invalidate_bar2(struct nvgsp_state *gsp);
void nvgsp_bar_wr32_bar2(struct nvgsp_state *gsp, uint64_t gva, uint32_t val);
uint32_t nvgsp_bar_rd32_bar2(struct nvgsp_state *gsp, uint64_t gva);
int nvgsp_vmm_map_sysmem_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    vm_paddr_t paddr, uint64_t size);
int nvgsp_vmm_map_sysmem_bo_prepared_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, const struct nvgpu_bo *bo, uint64_t bo_offset,
    uint64_t size, uint8_t kind);
int nvgsp_vmm_map_vram_flags_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t paddr, uint64_t size, uint8_t priv, uint8_t ro, uint8_t kind);
int nvgsp_vmm_map_sysmem(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t paddr, uint64_t size);
int nvgsp_vmm_map_sysmem_kva_noflush(struct nvgsp_vmm *vmm, uint64_t va,
    void *kva, uint64_t size, uint8_t kind);
int nvgsp_vmm_alloc_kernel_va(struct nvgsp_vmm *vmm, uint64_t size,
    uint64_t alignment, struct drm_mm_node *node);
void nvgsp_vmm_free_kernel_va(struct nvgsp_vmm *vmm,
    struct drm_mm_node *node);
int nvgsp_vmm_map_vram(struct nvgsp_vmm *vmm, uint64_t va,
    uint64_t paddr, uint64_t size, uint8_t kind);
int nvgsp_vmm_unmap(struct nvgsp_vmm *vmm, uint64_t va, uint64_t size);
int nvgsp_vmm_unmap_sparse_range_noflush(struct nvgsp_vmm *vmm,
    uint64_t va, uint64_t size);
void nvgsp_vmm_flush(struct nvgsp_vmm *vmm);
int nvgsp_vmm_create_golden(struct nvgpu_device *gpu);
void nvgsp_vmm_destroy_golden(struct nvgpu_device *gpu);
int nvgsp_vmm_map_submit_pages(struct nvgpu_device *gpu);

void nvgsp_rpc_init_msg_ntfy(struct nvgsp_state *gsp);
int nvgsp_rpc_add_msg_ntfy(struct nvgsp_state *gsp, uint32_t fn,
    nvgsp_msg_ntfy_func handler, void *priv);
int nvgsp_rpc_dispatch_all_msgs(struct nvgsp_state *gsp);
int nvgsp_seq_handle_msg(void *priv, uint32_t fn, void *repv, uint32_t repc);
void *nvgsp_rpc_get(struct nvgsp_state *gsp, uint32_t fn, uint32_t argc);
void *nvgsp_rpc_push(struct nvgsp_state *gsp, void *params, int policy,
    uint32_t repc);
void nvgsp_rpc_complete(struct nvgsp_state *gsp, void *params);
int nvgsp_rpc_set_system_info(struct nvgsp_state *gsp);
int nvgsp_rpc_set_registry(struct nvgsp_state *gsp);
int nvgsp_rpc_get_unloading_guest_driver_state(struct nvgsp_state *gsp);

enum {
	NVGSP_RPC_REPLY_NOWAIT = 0,
	NVGSP_RPC_REPLY_NOSEQ = 1,
	NVGSP_RPC_REPLY_RECV = 2,
};

static __inline void *
nvgsp_rpc_rd(struct nvgsp_state *gsp, uint32_t fn, uint32_t argc)
{
	void *p = nvgsp_rpc_get(gsp, fn, argc);
	if (p == NULL)
		return (NULL);
	return (nvgsp_rpc_push(gsp, p, NVGSP_RPC_REPLY_RECV, argc));
}

static __inline int
nvgsp_rpc_wr(struct nvgsp_state *gsp, void *params, int policy)
{
	void *ret = nvgsp_rpc_push(gsp, params, policy, 0);
	return (ret == NULL ? EIO : 0);
}

#endif /* _NVGSP_PRIV_H_ */
