/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP channel backend boundary for the native NVIDIA GPU driver.
 */

#include "nvgsp_channel.h"
#include "nvgsp_priv.h"
#include "nvgsp_rm.h"
#include "nvgsp_vmm.h"
#include "nvgsp_vram.h"

#include <machine/atomic.h>
#include <sys/kernel.h>
#include <sys/ktr.h>

static MALLOC_DEFINE(M_NVGSP_CHANNEL, "nvgsp_channel", "nvgsp channel state");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, gsp_golden_stage, 18,
    "gsp golden stage=%u error=%d", uint32_t stage, int error);

struct nvgsp_channel_object {
	struct nvgsp_object object;
};

static uint32_t
nvgsp_channel_count_bits64(uint64_t value)
{
	uint32_t count = 0;

	while (value != 0) {
		value &= value - 1;
		count++;
	}
	return (count);
}

#define TURING_CHANNEL_GPFIFO_A		0x0000c46fu
#define TURING_A			0x0000c597u
#define TURING_DMA_COPY_A		0x0000c5b5u
#define TURING_COMPUTE_A		0x0000c5c0u
#define NVGSP_CHANNEL_CHID_COUNT	2048u
#define FERMI_TWOD_A			0x0000902du
#define KEPLER_INLINE_TO_MEMORY_B	0x0000a140u
#define NVGSP_RM_CHANNEL		0xf1f00000u
#define NVGSP_RM_CE_OBJECT		0xc5b50000u
#define NVGSP_RM_THREED_OBJECT		0x97000000u
#define NV_CHANNEL_INST_SIZE		0x1000u
#define NV_CHANNEL_GOLDEN_SIZE		0x12000u
#define NV_CHANNEL_USERD_SIZE		0x200u
#define NV_CHANNEL_RAMFC_SIZE		0x200u
#define NV_CHANNEL_GPFIFO_ENTRIES	512u
#define NV_MEMORY_DESC_ADDRSPACE_SYSMEM_NONCOH 1u
#define NV_MEMORY_DESC_ADDRSPACE_VIDMEM 2u
#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED (1u << 21)
#define NV_KERNELCHANNEL_INTERNALFLAGS_PRIV_USER (0u << 0)
#define NV_KERNELCHANNEL_INTERNALFLAGS_PRIV_ADMIN (1u << 0)
#define NV_KERNELCHANNEL_INTERNALFLAGS_ERRNOT_NONE (1u << 2)
#define NV_KERNELCHANNEL_INTERNALFLAGS_ECCNOT_NONE (1u << 4)
#define NV2080_ENGINE_TYPE_COPY0	9u
#define NV2080_ENGINE_TYPE_COPY1	10u
#define NV2080_ENGINE_TYPE_COPY2	11u
#define NV2080_ENGINE_TYPE_GRAPHICS	1u
#define NV_USERD_SLOT_SIZE		0x200u
#define NV_USERD_GP_GET			0x88u
#define NV_USERD_GP_PUT			0x8cu
#define NV_USERMODE_DOORBELL		0x00bb0090u
#define NVGSP_SUBMIT_GVA_STRIDE	0x10000ULL
#define NVGSP_BAR2_ZERO_GVA		0x3000ULL
#define NVGSP_ALIGN_UP(v, a)		(((v) + (a) - 1) & ~((a) - 1))
#define NVGSP_CHANNEL_POST_PUSH_DWORDS	13u
#define NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW 0x40u
#define NVC06F_GP_ENTRY1_LEVEL_SUBROUTINE (1u << 9)
#define NVC06F_GP_ENTRY1_LENGTH_SHIFT	10u
#define NVC06F_GP_ENTRY1_NO_PREFETCH	(1u << 31)
#define NVC36F_DMA_SEC_OP_INC_METHOD	1u
#define NVC36F_MEM_OP_A_OFFSET		0x28u
#define NVC36F_SEM_ADDR_LO_OFFSET	0x5cu
#define NVC36F_SEM_EXECUTE_OFFSET	0x6cu
#define NVC36F_NON_STALL_INTERRUPT_OFFSET 0x20u
#define NVC36F_PUSH_HDR_MEM_OP_A_TO_D \
	((NVC36F_DMA_SEC_OP_INC_METHOD << 29) | (4u << 16) | \
	 (NVC36F_MEM_OP_A_OFFSET >> 2))
#define NVC36F_PUSH_HDR_SEM_ADDR_TRIPLET \
	((NVC36F_DMA_SEC_OP_INC_METHOD << 29) | (3u << 16) | \
	 (NVC36F_SEM_ADDR_LO_OFFSET >> 2))
#define NVC36F_PUSH_HDR_SEM_EXECUTE \
	((NVC36F_DMA_SEC_OP_INC_METHOD << 29) | (1u << 16) | \
	 (NVC36F_SEM_EXECUTE_OFFSET >> 2))
#define NVC36F_PUSH_HDR_NON_STALL_INTERRUPT \
	((NVC36F_DMA_SEC_OP_INC_METHOD << 29) | (1u << 16) | \
	 (NVC36F_NON_STALL_INTERRUPT_OFFSET >> 2))
#define NVC36F_SEM_EXECUTE_RELEASE	1u
#define NVC36F_SEM_EXECUTE_RELEASE_WFI	(1u << 20)
#define NVC36F_MEM_OP_C_MEMBAR_SYS_MEMBAR 0u
#define NVC36F_MEM_OP_D_OPERATION_MEMBAR (0x5u << 27)

#define NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_CONTEXT_BUFFERS_INFO 0x20800a32u
#define NV2080_CTRL_INTERNAL_GR_MAX_ENGINES 8u
#define NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID_COUNT 0x1au
#define NV0080_ENGINE_ID_GRAPHICS 0x00u
#define NV0080_ENGINE_ID_GRAPHICS_PATCH 0x10u
#define NV0080_ENGINE_ID_GRAPHICS_BUNDLE_CB 0x11u
#define NV0080_ENGINE_ID_GRAPHICS_PAGEPOOL_GLOBAL 0x12u
#define NV0080_ENGINE_ID_GRAPHICS_ATTRIBUTE_CB 0x13u
#define NV0080_ENGINE_ID_GRAPHICS_RTV_CB_GLOBAL 0x14u
#define NV0080_ENGINE_ID_GRAPHICS_FECS_EVENT 0x17u
#define NV0080_ENGINE_ID_GRAPHICS_PRIV_ACCESS_MAP 0x18u
#define NV2080_CTXBUF_ID_MAIN 0u
#define NV2080_CTXBUF_ID_PATCH 2u
#define NV2080_CTXBUF_ID_BUFFER_BUNDLE_CB 3u
#define NV2080_CTXBUF_ID_PAGEPOOL 4u
#define NV2080_CTXBUF_ID_ATTRIBUTE_CB 5u
#define NV2080_CTXBUF_ID_RTV_CB_GLOBAL 6u
#define NV2080_CTXBUF_ID_FECS_EVENT 9u
#define NV2080_CTXBUF_ID_PRIV_ACCESS_MAP 10u
#define NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP 11u
#define NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES 16u
#define NV2080_CTRL_CMD_GPU_PROMOTE_CTX 0x2080012bu
#define NV2080_CTRL_CMD_GR_GET_ZCULL_INFO 0x20801206u
#define NVGSP_GR_CTXBUF_TARGET_INST 0u
#define NVGSP_GR_CTXBUF_TARGET_INST_SR_LOST 1u

struct nvgsp_memory_desc_params {
	uint64_t base;
	uint64_t size;
	uint32_t addressSpace;
	uint32_t cacheAttrib;
};

struct nvgsp_channel_alloc_params {
	uint32_t hObjectError;
	uint32_t hObjectBuffer;
	uint64_t gpFifoOffset;
	uint32_t gpFifoEntries;
	uint32_t flags;
	uint32_t hContextShare;
	uint32_t hVASpace;
	uint32_t hUserdMemory[8];
	uint64_t userdOffset[8];
	uint32_t engineType;
	uint32_t cid;
	uint32_t subDeviceId;
	uint32_t hObjectEccError;
	struct nvgsp_memory_desc_params instanceMem;
	struct nvgsp_memory_desc_params userdMem;
	struct nvgsp_memory_desc_params ramfcMem;
	struct nvgsp_memory_desc_params mthdbufMem;
	uint32_t hPhysChannelGroup;
	uint32_t internalFlags;
	struct nvgsp_memory_desc_params errorNotifierMem;
	struct nvgsp_memory_desc_params eccErrorNotifierMem;
	uint32_t ProcessID;
	uint32_t SubProcessID;
	uint32_t encryptIv[3];
	uint32_t decryptIv[3];
	uint32_t hmacNonce[8];
	uint32_t tpcConfigID;
};

struct nvgsp_gr_engine_ctxbuf_info {
	uint32_t size;
	uint32_t alignment;
};

struct nvgsp_gr_context_buffers_info {
	struct nvgsp_gr_engine_ctxbuf_info
	    engine[NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID_COUNT];
};

struct nvgsp_gr_get_context_buffers_info_params {
	struct nvgsp_gr_context_buffers_info
	    engineContextBuffersInfo[NV2080_CTRL_INTERNAL_GR_MAX_ENGINES];
};

struct nvgsp_gr_promote_ctx_buffer_entry {
	uint64_t gpuPhysAddr;
	uint64_t gpuVirtAddr;
	uint64_t size;
	uint32_t physAttr;
	uint16_t bufferId;
	uint8_t bInitialize;
	uint8_t bNonmapped;
};

struct nvgsp_gr_promote_ctx_params {
	uint32_t engineType;
	uint32_t hClient;
	uint32_t ChID;
	uint32_t hChanClient;
	uint32_t hObject;
	uint32_t hVirtMemory;
	uint64_t virtAddress;
	uint64_t size;
	uint32_t entryCount;
	uint8_t pad[4];
	struct nvgsp_gr_promote_ctx_buffer_entry
	    promoteEntry[NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES];
};

struct nvgsp_gr_zcull_info_params {
	uint32_t widthAlignPixels;
	uint32_t heightAlignPixels;
	uint32_t pixelSquaresByAliquots;
	uint32_t aliquotTotal;
	uint32_t zcullRegionByteMultiplier;
	uint32_t zcullRegionHeaderSize;
	uint32_t zcullSubregionHeaderSize;
	uint32_t subregionCount;
	uint32_t subregionWidthAlignPixels;
	uint32_t subregionHeightAlignPixels;
};

struct nvgsp_channel_submission {
	struct nvgsp_channel *chan;
	volatile uint32_t *sema;
	uint32_t payload;
	uint32_t put;
	uint32_t gpf_required;
	uint32_t post_slot;
};

static uint64_t
nvgsp_channel_calc_order_base_2_u64(uint64_t value)
{
	uint64_t order = 0;
	uint64_t size = 1;

	while (size < value) {
		size <<= 1;
		order++;
	}
	return (order);
}

int
nvgsp_channel_prepare_submit(struct nvgsp_channel *chan,
    const struct nvgsp_channel_push *pushes, uint32_t push_count,
    struct nvgsp_channel_submission **out)
{
	struct nvgsp_channel_submission *submission;
	struct nvgsp_state *gsp;
	uint32_t *gpf, *post;
	uint32_t get, put, next, required, post_slot, post_offset;
	uint64_t post_gva, sema_gva, slot_bar1;

	if (chan == NULL || chan->vmm == NULL || out == NULL ||
	    (push_count != 0 && pushes == NULL) ||
	    push_count > NV_CHANNEL_GPFIFO_ENTRIES - 2)
		return (EINVAL);
	*out = NULL;
	submission = kmalloc(sizeof(*submission), M_NVGSP_CHANNEL,
	    M_NOWAIT | M_ZERO);
	if (submission == NULL)
		return (ENOMEM);
	gsp = chan->vmm->gsp;
	if (gsp == NULL || chan->submit_gpf.kva == NULL ||
	    chan->submit_push.kva == NULL || chan->submit_sema.kva == NULL) {
		_kfree(submission, M_NVGSP_CHANNEL);
		return (ENXIO);
	}

	lwkt_gettoken(&gsp->gsp_tok);
	if (chan->faulted) {
		int error = chan->fault_error != 0 ? chan->fault_error : EIO;

		lwkt_reltoken(&gsp->gsp_tok);
		_kfree(submission, M_NVGSP_CHANNEL);
		return (error);
	}
	for (uint32_t i = 0; i < NVGSP_CHANNEL_POST_RING_SLOTS; i++) {
		uint64_t bit = 1ULL << i;
		volatile uint32_t *sema;
		uint32_t value;

		if ((chan->submit_post_slots_busy & bit) == 0)
			continue;
		sema = (volatile uint32_t *)chan->submit_sema.kva + i;
		cpu_lfence();
		value = *sema;
		if ((int32_t)(value - chan->submit_post_payload[i]) >= 0)
			chan->submit_post_slots_busy &= ~bit;
	}
	post_slot = NVGSP_CHANNEL_POST_RING_SLOTS;
	for (uint32_t i = 0; i < NVGSP_CHANNEL_POST_RING_SLOTS; i++) {
		uint32_t candidate = (chan->submit_post_slot + i) %
		    NVGSP_CHANNEL_POST_RING_SLOTS;
		uint64_t bit = 1ULL << candidate;

		if ((chan->submit_post_slots_busy & bit) != 0)
			continue;
		chan->submit_post_slots_busy |= bit;
		chan->submit_post_slot = (candidate + 1) %
		    NVGSP_CHANNEL_POST_RING_SLOTS;
		post_slot = candidate;
		break;
	}
	if (post_slot == NVGSP_CHANNEL_POST_RING_SLOTS) {
		lwkt_reltoken(&gsp->gsp_tok);
		_kfree(submission, M_NVGSP_CHANNEL);
		return (EAGAIN);
	}

	put = chan->gpf_put & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	next = put;
	required = 0;
	for (uint32_t i = 0; i < push_count; i++) {
		if ((next & (NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1)) ==
		    NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1) {
			required++;
			next = (next + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
		}
		required++;
		next = (next + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	}
	if ((next & (NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1)) ==
	    NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1)
		required++;
	required++;
	if (required >= NV_CHANNEL_GPFIFO_ENTRIES) {
		chan->submit_post_slots_busy &= ~(1ULL << post_slot);
		lwkt_reltoken(&gsp->gsp_tok);
		_kfree(submission, M_NVGSP_CHANNEL);
		return (EINVAL);
	}
	slot_bar1 = chan->userd_bar1_gva +
	    (uint64_t)((uint32_t)chan->chid % 8u) * NV_USERD_SLOT_SIZE;
	if (chan->gpf_free < required) {
		get = nvgsp_bar_rd32_bar1(gsp, slot_bar1 + NV_USERD_GP_GET) &
		    (NV_CHANNEL_GPFIFO_ENTRIES - 1);
		chan->gpf_free = (get - put - 1) &
		    (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	}
	if (chan->gpf_free < required) {
		chan->submit_post_slots_busy &= ~(1ULL << post_slot);
		lwkt_reltoken(&gsp->gsp_tok);
		_kfree(submission, M_NVGSP_CHANNEL);
		return (EAGAIN);
	}

	chan->submit_payload++;
	if (chan->submit_payload == 0)
		chan->submit_payload++;
	post_offset = post_slot * NVGSP_CHANNEL_POST_PUSH_DWORDS;
	post = (uint32_t *)chan->submit_push.kva + post_offset;
	submission->chan = chan;
	submission->sema = (volatile uint32_t *)chan->submit_sema.kva + post_slot;
	submission->payload = chan->submit_payload;
	submission->post_slot = post_slot;
	chan->submit_post_payload[post_slot] = submission->payload;
	submission->gpf_required = required;
	post_gva = chan->submit_gva_push + (uint64_t)post_offset * 4;
	sema_gva = chan->submit_gva_sema + (uint64_t)post_slot * 4;
	gpf = chan->submit_gpf.kva;

	for (uint32_t i = 0; i < push_count; i++) {
		uint32_t entry0 = (uint32_t)pushes[i].va;
		uint32_t entry1 = (uint32_t)((pushes[i].va >> 32) & 0xffu) |
		    NVC06F_GP_ENTRY1_LEVEL_SUBROUTINE |
		    ((pushes[i].va_len / 4) << NVC06F_GP_ENTRY1_LENGTH_SHIFT);

		if ((pushes[i].flags & NVGSP_CHANNEL_PUSH_NO_PREFETCH) != 0)
			entry1 |= NVC06F_GP_ENTRY1_NO_PREFETCH;
		if ((put & (NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1)) ==
		    NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1) {
			gpf[put * 2] = 0;
			gpf[put * 2 + 1] = 0;
			put = (put + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
		}
		gpf[put * 2] = entry0;
		gpf[put * 2 + 1] = entry1;
		put = (put + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	}

	post[0] = NVC36F_PUSH_HDR_SEM_ADDR_TRIPLET;
	post[1] = (uint32_t)sema_gva;
	post[2] = (uint32_t)((sema_gva >> 32) & 0xffu);
	post[3] = submission->payload;
	post[4] = NVC36F_PUSH_HDR_SEM_EXECUTE;
	post[5] = NVC36F_SEM_EXECUTE_RELEASE |
	    NVC36F_SEM_EXECUTE_RELEASE_WFI;
	post[6] = NVC36F_PUSH_HDR_MEM_OP_A_TO_D;
	post[7] = 0;
	post[8] = 0;
	post[9] = NVC36F_MEM_OP_C_MEMBAR_SYS_MEMBAR;
	post[10] = NVC36F_MEM_OP_D_OPERATION_MEMBAR;
	post[11] = NVC36F_PUSH_HDR_NON_STALL_INTERRUPT;
	post[12] = 0;
	if ((put & (NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1)) ==
	    NVGSP_CHANNEL_GPFIFO_FETCH_WINDOW - 1) {
		gpf[put * 2] = 0;
		gpf[put * 2 + 1] = 0;
		put = (put + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	}
	gpf[put * 2] = (uint32_t)post_gva;
	gpf[put * 2 + 1] = (uint32_t)((post_gva >> 32) & 0xffu) |
	    (NVGSP_CHANNEL_POST_PUSH_DWORDS << NVC06F_GP_ENTRY1_LENGTH_SHIFT);
	submission->put = (put + 1) & (NV_CHANNEL_GPFIFO_ENTRIES - 1);
	*out = submission;
	return (0);
}

void
nvgsp_channel_commit_submit(struct nvgsp_channel_submission *submission)
{
	struct nvgsp_channel *chan = submission->chan;
	struct nvgsp_state *gsp = chan->vmm->gsp;
	uint64_t slot_bar1 = chan->userd_bar1_gva +
	    (uint64_t)((uint32_t)chan->chid % 8u) * NV_USERD_SLOT_SIZE;

	nvgsp_bar_wr32_bar1(gsp, slot_bar1 + NV_USERD_GP_PUT, submission->put);
	cpu_sfence();
	(void)nvgsp_bar_rd32_bar1(gsp, slot_bar1);
	nvgsp_wr32(gsp, NV_USERMODE_DOORBELL, chan->gsp_token);
	chan->gpf_put = submission->put;
	chan->gpf_free -= submission->gpf_required;
	lwkt_reltoken(&gsp->gsp_tok);
	_kfree(submission, M_NVGSP_CHANNEL);
}

void
nvgsp_channel_abort_submit(struct nvgsp_channel_submission *submission)
{
	struct nvgsp_channel *chan;
	struct nvgsp_state *gsp;

	if (submission == NULL)
		return;
	chan = submission->chan;
	gsp = chan->vmm->gsp;
	chan->submit_post_slots_busy &= ~(1ULL << submission->post_slot);
	lwkt_reltoken(&gsp->gsp_tok);
	_kfree(submission, M_NVGSP_CHANNEL);
}

void
nvgsp_channel_describe_submit(struct nvgsp_channel_submission *submission,
    struct nvgsp_channel_completion *completion)
{
	if (submission == NULL || completion == NULL)
		return;
	completion->sema = submission->sema;
	completion->payload = submission->payload;
	completion->chid = (uint32_t)submission->chan->chid;
}

void
nvgsp_channel_mark_fault(struct nvgpu_device *gpu, uint32_t chid, int error)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan;

	if (gsp == NULL || chid >= NVGSP_CHANNEL_CHID_COUNT)
		return;
	lwkt_gettoken(&gsp->gsp_tok);
	lwkt_gettoken(&gsp->chid_tok);
	chan = gsp->chid_channel[chid];
	if (chan != NULL) {
		chan->faulted = 1;
		chan->fault_error = error != 0 ? error : EIO;
	}
	lwkt_reltoken(&gsp->chid_tok);
	lwkt_reltoken(&gsp->gsp_tok);
}

int
nvgsp_channel_query_capacity(struct nvgpu_device *gpu, uint32_t *total,
    uint32_t *reserved)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint32_t used = 0;
	uint32_t i;

	if (gsp == NULL || total == NULL || reserved == NULL)
		return (EINVAL);
	lwkt_gettoken(&gsp->chid_tok);
	for (i = 0; i < nitems(gsp->chid_used); i++)
		used += nvgsp_channel_count_bits64(gsp->chid_used[i]);
	lwkt_reltoken(&gsp->chid_tok);
	*total = NVGSP_CHANNEL_CHID_COUNT;
	*reserved = used;
	return (0);
}

int
nvgsp_channel_alloc_chid(struct nvgsp_state *gsp)
{
	int chid = -1;
	int w, b;

	lwkt_gettoken(&gsp->chid_tok);
	for (w = 0; w < 32 && chid < 0; w++) {
		uint64_t bits = gsp->chid_used[w];

		if (bits == ~0ULL)
			continue;
		for (b = 0; b < 64; b++) {
			if ((bits & (1ULL << b)) == 0) {
				gsp->chid_used[w] |= (1ULL << b);
				chid = w * 64 + b;
				break;
			}
		}
	}
	lwkt_reltoken(&gsp->chid_tok);
	return (chid);
}

void
nvgsp_channel_free_chid(struct nvgsp_state *gsp, int chid)
{
	if (gsp == NULL || chid < 0 || chid >= NVGSP_CHANNEL_CHID_COUNT)
		return;
	lwkt_gettoken(&gsp->chid_tok);
	gsp->chid_used[chid >> 6] &= ~(1ULL << (chid & 63));
	if (gsp->chid_channel[chid] != NULL)
		gsp->chid_channel[chid] = NULL;
	lwkt_reltoken(&gsp->chid_tok);
}

static int
nvgsp_channel_map_gr_ctxbuf(uint32_t engine_id, uint32_t *buffer_id, uint8_t *global,
    uint8_t *init, uint8_t *ro, uint8_t *nonmapped)
{
	*global = 1;
	*init = 0;
	*ro = 0;
	*nonmapped = 0;

	switch (engine_id) {
	case NV0080_ENGINE_ID_GRAPHICS:
		*buffer_id = NV2080_CTXBUF_ID_MAIN;
		*global = 0;
		*init = 1;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_PATCH:
		*buffer_id = NV2080_CTXBUF_ID_PATCH;
		*global = 0;
		*init = 1;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_BUNDLE_CB:
		*buffer_id = NV2080_CTXBUF_ID_BUFFER_BUNDLE_CB;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_PAGEPOOL_GLOBAL:
		*buffer_id = NV2080_CTXBUF_ID_PAGEPOOL;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_ATTRIBUTE_CB:
		*buffer_id = NV2080_CTXBUF_ID_ATTRIBUTE_CB;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_RTV_CB_GLOBAL:
		*buffer_id = NV2080_CTXBUF_ID_RTV_CB_GLOBAL;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_FECS_EVENT:
		*buffer_id = NV2080_CTXBUF_ID_FECS_EVENT;
		*init = 1;
		return (0);
	case NV0080_ENGINE_ID_GRAPHICS_PRIV_ACCESS_MAP:
		*buffer_id = NV2080_CTXBUF_ID_PRIV_ACCESS_MAP;
		*init = 1;
		*ro = 1;
		*nonmapped = 1;
		return (0);
	default:
		return (ENOENT);
	}
}

static struct nvgsp_gr_ctxbuf *
nvgsp_channel_get_gr_global_ctxbuf(struct nvgsp_state *gsp, uint32_t buffer_id)
{
	for (uint32_t i = 0; i < gsp->gr_ctxbuf_global_nr; i++) {
		if (gsp->gr_ctxbuf_global[i].buffer_id == buffer_id)
			return (&gsp->gr_ctxbuf_global[i]);
	}
	return (NULL);
}

static int
nvgsp_channel_save_gr_global_ctxbuf(struct nvgsp_state *gsp,
    const struct nvgsp_gr_ctxbuf *buf)
{
	if (gsp->gr_ctxbuf_global_nr >= NVGSP_GR_MAX_CTXBUFS)
		return (ENOSPC);
	gsp->gr_ctxbuf_global[gsp->gr_ctxbuf_global_nr++] = *buf;
	return (0);
}

static void
nvgsp_channel_submit_dmamem_free(struct nvgsp_state *gsp,
    struct nvgsp_channel *chan)
{
	nvgsp_dma_free_dmamem(gsp, &chan->submit_sema);
	nvgsp_dma_free_dmamem(gsp, &chan->submit_gpf);
	nvgsp_dma_free_dmamem(gsp, &chan->submit_push);
}

static int
nvgsp_channel_submit_dmamem_alloc(struct nvgsp_state *gsp,
    struct nvgsp_channel *chan)
{
	int error;

	error = nvgsp_dma_alloc_dmamem(gsp, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, &chan->submit_push);
	if (error != 0)
		return (error);
	error = nvgsp_dma_alloc_dmamem(gsp, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, &chan->submit_gpf);
	if (error != 0)
		goto fail;
	error = nvgsp_dma_alloc_dmamem(gsp, NVGSP_GMMU_PT_PAGE_SIZE,
	    NVGSP_GMMU_PT_PAGE_SIZE, &chan->submit_sema);
	if (error != 0)
		goto fail;
	return (0);

fail:
	nvgsp_channel_submit_dmamem_free(gsp, chan);
	return (error);
}

static int
nvgsp_channel_zero_vram(struct nvgsp_state *gsp, uint64_t paddr, uint64_t size)
{
	uint64_t off;
	int error;

	for (off = 0; off < size; off += NVGSP_GMMU_PT_PAGE_SIZE) {
		error = nvgsp_bar_map_bar2_vram(gsp, NVGSP_BAR2_ZERO_GVA, paddr + off);
		if (error != 0)
			return (error);
		nvgsp_bar_flush_bar2(gsp);
		for (uint32_t i = 0; i < NVGSP_GMMU_PT_PAGE_SIZE; i += 4)
			nvgsp_bar_wr32_bar2(gsp, NVGSP_BAR2_ZERO_GVA + i, 0);
		nvgsp_bar_flush_bar2(gsp);
	}
	return (0);
}

static void
nvgsp_channel_zero_inst(struct nvgsp_state *gsp, struct nvgsp_channel *chan)
{
	for (uint32_t off = 0; off < NV_CHANNEL_INST_SIZE; off += 4)
		nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + off, 0);
	nvgsp_bar_flush_bar1(gsp);
}

/* Join the channel instance block to its TU102 VMM and subcontext 0. */
static void
nvgsp_channel_write_inst_vmm(struct nvgsp_state *gsp, struct nvgsp_channel *chan,
    const struct nvgsp_vmm *vmm)
{
	uint64_t pdb = vmm->pt[0].page.vram_paddr;
	uint64_t limit = (uint64_t)NVGSP_GMMU_PD3_ENTRIES <<
	    NVGSP_GMMU_PD3_SHIFT;
	uint64_t mask = 1;
	uint32_t pdb_lo = (uint32_t)((pdb >> 12) << 12) | (1u << 10) | (1u << 11);
	uint32_t pdb_hi = (uint32_t)(pdb >> 32);

	nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + 0x200, pdb_lo);
	nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + 0x204, pdb_hi);
	nvgsp_bar_wr64_bar1(gsp, chan->inst_bar1_gva + 0x208, limit - 1);
	nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + 0x21c, 0);

	for (uint32_t i = 0; i < 64; i++) {
		if ((mask & (1ULL << i)) != 0) {
			nvgsp_bar_wr32_bar1(gsp,
			    chan->inst_bar1_gva + 0x2a4 + i * 0x10, pdb_hi);
			nvgsp_bar_wr32_bar1(gsp,
			    chan->inst_bar1_gva + 0x2a0 + i * 0x10, pdb_lo);
		} else {
			nvgsp_bar_wr32_bar1(gsp,
			    chan->inst_bar1_gva + 0x2a4 + i * 0x10, 1);
			nvgsp_bar_wr32_bar1(gsp,
			    chan->inst_bar1_gva + 0x2a0 + i * 0x10, 1);
		}
		nvgsp_bar_wr32_bar1(gsp,
		    chan->inst_bar1_gva + 0x2a8 + i * 0x10, 0);
	}
	nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + 0x298,
	    (uint32_t)mask);
	nvgsp_bar_wr32_bar1(gsp, chan->inst_bar1_gva + 0x29c,
	    (uint32_t)(mask >> 32));
	nvgsp_bar_flush_bar1(gsp);
}

static void
nvgsp_channel_clear_userd(struct nvgsp_state *gsp,
    const struct nvgsp_channel *chan)
{
	static const uint32_t clear_offsets[] = {
		0x040, 0x044, 0x048, 0x04c, 0x050,
		0x058, 0x05c, 0x060, NV_USERD_GP_GET, NV_USERD_GP_PUT,
	};
	uint64_t slot_gva;

	slot_gva = chan->userd_bar1_gva +
	    (uint64_t)((uint32_t)chan->chid % 8u) * NV_USERD_SLOT_SIZE;
	for (uint32_t i = 0; i < nitems(clear_offsets); i++)
		nvgsp_bar_wr32_bar1(gsp, slot_gva + clear_offsets[i], 0);
	nvgsp_bar_flush_bar1(gsp);
}

static int
nvgsp_channel_map_submit_pages(struct nvgsp_vmm *vmm, struct nvgsp_channel *chan)
{
	int error;

	error = nvgsp_vmm_map_sysmem_noflush(vmm, chan->submit_gva_push,
	    chan->submit_push.paddr, NVGSP_GMMU_PT_PAGE_SIZE);
	if (error == 0)
		error = nvgsp_vmm_map_sysmem_noflush(vmm, chan->submit_gva_gpf,
		    chan->submit_gpf.paddr, NVGSP_GMMU_PT_PAGE_SIZE);
	if (error == 0)
		error = nvgsp_vmm_map_sysmem_noflush(vmm, chan->submit_gva_sema,
		    chan->submit_sema.paddr, NVGSP_GMMU_PT_PAGE_SIZE);
	if (error == 0)
		nvgsp_vmm_flush(vmm);
	else
		(void)nvgsp_vmm_unmap(vmm, chan->submit_gva_push, 0x3000);
	return (error);
}

static int
nvgsp_channel_alloc_rm_common(struct nvgsp_vmm *vmm, struct nvgsp_channel *chan,
    uint32_t handle, uint32_t engine_type, uint8_t priv, uint64_t inst_addr,
    uint64_t userd_addr, uint64_t mthdbuf_addr, uint32_t mthdbuf_size,
    uint64_t gpfifo_offset, uint32_t gpfifo_length)
{
	struct nvgsp_channel_alloc_params *args;
	uint32_t userd_page, userd_index;

	if (vmm == NULL || chan == NULL || vmm->device.object.client == NULL)
		return (ENXIO);

	args = nvgsp_rm_get_alloc(&vmm->device.object, handle,
	    TURING_CHANNEL_GPFIFO_A, sizeof(*args), &chan->object);
	if (args == NULL)
		return (ENOMEM);

	userd_page = (uint32_t)chan->chid / 8u;
	userd_index = (uint32_t)chan->chid % 8u;
	args->gpFifoOffset = gpfifo_offset;
	args->gpFifoEntries = gpfifo_length / 8;
	args->flags = ((userd_index & 7u) << 8) |
	    ((userd_page & 0x1ffu) << 12) |
	    NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED;
	if (priv)
		args->flags |= (1u << 5);
	args->hVASpace = vmm->vaspace.handle;
	args->engineType = engine_type;
	args->instanceMem.base = inst_addr;
	args->instanceMem.size = NV_CHANNEL_INST_SIZE;
	args->instanceMem.addressSpace = NV_MEMORY_DESC_ADDRSPACE_VIDMEM;
	args->instanceMem.cacheAttrib = 1;
	args->userdMem.base = userd_addr;
	args->userdMem.size = NV_CHANNEL_USERD_SIZE;
	args->userdMem.addressSpace = NV_MEMORY_DESC_ADDRSPACE_VIDMEM;
	args->userdMem.cacheAttrib = 1;
	args->ramfcMem.base = inst_addr;
	args->ramfcMem.size = NV_CHANNEL_RAMFC_SIZE;
	args->ramfcMem.addressSpace = NV_MEMORY_DESC_ADDRSPACE_VIDMEM;
	args->ramfcMem.cacheAttrib = 1;
	args->mthdbufMem.base = mthdbuf_addr;
	args->mthdbufMem.size = mthdbuf_size;
	args->mthdbufMem.addressSpace = NV_MEMORY_DESC_ADDRSPACE_SYSMEM_NONCOH;
	args->mthdbufMem.cacheAttrib = 0;
	args->internalFlags = NV_KERNELCHANNEL_INTERNALFLAGS_ERRNOT_NONE |
	    NV_KERNELCHANNEL_INTERNALFLAGS_ECCNOT_NONE;
	if (priv)
		args->internalFlags |= NV_KERNELCHANNEL_INTERNALFLAGS_PRIV_ADMIN;
	else
		args->internalFlags |= NV_KERNELCHANNEL_INTERNALFLAGS_PRIV_USER;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "channel alloc handle=0x%x class=0x%x chid=%d engine=%u flags=0x%x internal=0x%x hVASpace=0x%x inst=0x%llx userd=0x%llx mthdbuf=0x%llx/0x%x gpfifo=0x%llx/0x%x\n",
	    handle, TURING_CHANNEL_GPFIFO_A, chan->chid, engine_type,
	    args->flags, args->internalFlags, args->hVASpace,
	    (unsigned long long)inst_addr, (unsigned long long)userd_addr,
	    (unsigned long long)mthdbuf_addr, mthdbuf_size,
	    (unsigned long long)gpfifo_offset, gpfifo_length);
	return (nvgsp_rm_write_alloc(&chan->object, args));
}

static int
nvgsp_channel_bind_engine(struct nvgsp_channel *chan, uint32_t engine_type)
{
	struct { uint32_t engineType; } *bind;

	bind = nvgsp_rm_get_ctrl(&chan->object, 0xa06f0104u, sizeof(*bind));
	if (bind == NULL)
		return (ENOMEM);
	bind->engineType = engine_type;
	return (nvgsp_rm_write_ctrl(&chan->object, bind));
}

static int
nvgsp_channel_schedule(struct nvgsp_channel *chan)
{
	struct { uint8_t bEnable; uint8_t bSkipSubmit; } *sched;

	sched = nvgsp_rm_get_ctrl(&chan->object, 0xa06f0103u, sizeof(*sched));
	if (sched == NULL)
		return (ENOMEM);
	sched->bEnable = 1;
	sched->bSkipSubmit = 0;
	return (nvgsp_rm_write_ctrl(&chan->object, sched));
}

static int
nvgsp_channel_alloc_ce_object(struct nvgsp_channel *chan, uint32_t engine_type)
{
	struct {
		uint32_t version;
		uint32_t engineType;
	} *args;
	int error;

	if (engine_type < NV2080_ENGINE_TYPE_COPY0 || engine_type > NV2080_ENGINE_TYPE_COPY2)
		return (0);
	args = nvgsp_rm_get_alloc(&chan->object, NVGSP_RM_CE_OBJECT,
	    TURING_DMA_COPY_A, sizeof(*args), &chan->ce_obj);
	if (args == NULL)
		return (ENOMEM);
	args->version = 1;
	args->engineType = engine_type;
	error = nvgsp_rm_write_alloc(&chan->ce_obj, args);
	if (error != 0)
		memset(&chan->ce_obj, 0, sizeof(chan->ce_obj));
	return (error);
}

static int
nvgsp_channel_get_work_submit_token(struct nvgsp_channel *chan)
{
	struct { uint32_t workSubmitToken; } *token;
	void *reply;
	int error;

	token = nvgsp_rm_get_ctrl(&chan->object, 0xc36f0108u, sizeof(*token));
	if (token == NULL)
		return (ENOMEM);
	token->workSubmitToken = 0;
	reply = token;
	error = nvgsp_rm_read_ctrl(&chan->object, &reply, sizeof(*token));
	if (error == 0 && reply != NULL) {
		token = reply;
		chan->gsp_token = token->workSubmitToken;
		nvgsp_rm_complete_ctrl(&chan->object, token);
	}
	return (error);
}

static int
nvgsp_channel_alloc_graphics_object(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan = gsp != NULL ? gsp->golden_channel : NULL;
	struct nvgsp_object threed;
	void *args;
	int error;

	if (chan == NULL)
		return (ENXIO);
	memset(&threed, 0, sizeof(threed));
	args = nvgsp_rm_get_alloc(&chan->object, NVGSP_RM_THREED_OBJECT,
	    nvgpu_device_get_chip(gpu)->class_3d, 0, &threed);
	if (args == NULL)
		return (ENOMEM);
	error = nvgsp_rm_write_alloc(&threed, args);
	if (error == 0)
		nvgsp_rm_free(&threed);
	return (error);
}

static void
nvgsp_channel_free_gr_ctxbufs(struct nvgsp_state *gsp, struct nvgsp_channel *chan)
{
	if (gsp == NULL || chan == NULL)
		return;

	for (uint32_t i = 0; i < chan->gr_ctxbuf_nr; i++) {
		struct nvgsp_gr_ctxbuf *buf = &chan->gr_ctxbuf[i];

		if (!buf->nonmapped && buf->gva != 0 && buf->size != 0)
			(void)nvgsp_vmm_unmap(chan->vmm, buf->gva, buf->size);
		nvgsp_vram_free_kind(gsp, buf->paddr,
		    NVGSP_VRAM_GR_CTXBUF_GLOBAL, chan);
		nvgsp_vram_free_kind(gsp, buf->paddr,
		    NVGSP_VRAM_GR_CTXBUF_CHANNEL, chan);
	}
	chan->gr_ctxbuf_nr = 0;
}

static int
nvgsp_channel_promote_gr_context(struct nvgsp_channel *chan, int golden)
{
	struct nvgsp_state *gsp;
	struct nvgsp_client tmp_client;
	struct nvgsp_object tmp_subdev;
	struct nvgsp_gr_get_context_buffers_info_params *info;
	struct nvgsp_gr_promote_ctx_params *ctrl;
	void *reply;
	uint64_t next_gva = 0x1000;
	int error;
	bool ctx_map_dirty = false;
	bool ctx_map_flushed = false;

	if (chan == NULL || chan->vmm == NULL || chan->vmm->gsp == NULL)
		return (ENXIO);
	gsp = chan->vmm->gsp;
	if (chan->gr_ctx_promoted)
		return (0);

	memset(&tmp_client, 0, sizeof(tmp_client));
	tmp_client.gsp = gsp;
	tmp_client.object.client = &tmp_client;
	tmp_client.object.handle = gsp->gsp_internal_client;
	tmp_subdev.client = &tmp_client;
	tmp_subdev.parent = NULL;
	tmp_subdev.handle = gsp->gsp_internal_subdevice;

	info = nvgsp_rm_get_ctrl(&tmp_subdev,
	    NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_CONTEXT_BUFFERS_INFO,
	    sizeof(*info));
	if (info == NULL)
		return (ENOMEM);
	reply = info;
	error = nvgsp_rm_read_ctrl(&tmp_subdev, &reply, sizeof(*info));
	if (error != 0 || reply == NULL)
		return (error != 0 ? error : EIO);
	info = reply;

	{
		struct nvgsp_gr_zcull_info_params *zcull;

		zcull = nvgsp_rm_get_ctrl(&tmp_subdev,
		    NV2080_CTRL_CMD_GR_GET_ZCULL_INFO, sizeof(*zcull));
		if (zcull == NULL) {
			nvgsp_rm_complete_ctrl(&tmp_subdev, info);
			return (ENOMEM);
		}
		reply = zcull;
		error = nvgsp_rm_read_ctrl(&tmp_subdev, &reply, sizeof(*zcull));
		if (error != 0 || reply == NULL) {
			nvgsp_rm_complete_ctrl(&tmp_subdev, info);
			return (error != 0 ? error : EIO);
		}
		zcull = reply;
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gr zcull widthAlign=%u heightAlign=%u subregions=%u\n",
		    zcull->widthAlignPixels, zcull->heightAlignPixels,
		    zcull->subregionCount);
		nvgsp_rm_complete_ctrl(&tmp_subdev, zcull);
	}

	ctrl = nvgsp_rm_get_ctrl(&chan->vmm->device.subdevice,
	    NV2080_CTRL_CMD_GPU_PROMOTE_CTX, sizeof(*ctrl));
	if (ctrl == NULL) {
		nvgsp_rm_complete_ctrl(&tmp_subdev, info);
		return (ENOMEM);
	}
	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->engineType = NV2080_ENGINE_TYPE_GRAPHICS;
	ctrl->hChanClient = chan->vmm->client.object.handle;
	ctrl->hObject = chan->object.handle;
	if (golden)
		gsp->gr_ctxbuf_global_nr = 0;

	for (uint32_t i = 0;
	    i < NV2080_CTRL_INTERNAL_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID_COUNT;
	    i++) {
		struct nvgsp_gr_engine_ctxbuf_info *bi =
		    &info->engineContextBuffersInfo[0].engine[i];
		struct nvgsp_gr_promote_ctx_buffer_entry *entry;
		struct nvgsp_gr_ctxbuf *buf;
		uint32_t buffer_id;
		uint64_t size, alloc_size, entry_size, mem_align, gva_align;
		uint32_t page_shift, gva_align_shift;
		uint8_t global, init, ro, nonmapped, target, alloc;
		uint8_t entry_nonmapped;

		if (bi->size == 0)
			continue;
		if (nvgsp_channel_map_gr_ctxbuf(i, &buffer_id, &global, &init, &ro,
		    &nonmapped) != 0)
			continue;
		if (ctrl->entryCount >= NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES ||
		    chan->gr_ctxbuf_nr >= NVGSP_GR_MAX_CTXBUFS) {
			error = ENOSPC;
			goto out_done;
		}

		size = bi->size;
		if (buffer_id == NV2080_CTXBUF_ID_MAIN)
			size = NVGSP_ALIGN_UP(size, 0x1000) + 64 * 0x1000;
		entry_size = size;

		if (size >= (1ULL << 21))
			page_shift = 21;
		else if (size >= (1ULL << 16))
			page_shift = 16;
		else
			page_shift = 12;
		if (buffer_id == NV2080_CTXBUF_ID_ATTRIBUTE_CB)
			gva_align_shift = nvgsp_channel_calc_order_base_2_u64(size);
		else
			gva_align_shift = page_shift;

		mem_align = 1ULL << page_shift;
		gva_align = 1ULL << gva_align_shift;
		alloc_size = NVGSP_ALIGN_UP(size, mem_align);
		next_gva = NVGSP_ALIGN_UP(next_gva, gva_align);
		target = init ? NVGSP_GR_CTXBUF_TARGET_INST :
		    NVGSP_GR_CTXBUF_TARGET_INST_SR_LOST;
		alloc = golden || !global;
		entry_nonmapped = nonmapped && alloc;

		if (!alloc &&
		    buffer_id == NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP)
			continue;

		buf = &chan->gr_ctxbuf[chan->gr_ctxbuf_nr];
		if (alloc) {
			buf->paddr = nvgsp_vram_alloc_kind(gsp, alloc_size,
			    mem_align, golden && global ?
			    NVGSP_VRAM_GR_CTXBUF_GLOBAL :
			    NVGSP_VRAM_GR_CTXBUF_CHANNEL, chan);
			if (buf->paddr == 0) {
				error = ENOMEM;
				goto out_done;
			}
		} else {
			struct nvgsp_gr_ctxbuf *global_buf;

			global_buf = nvgsp_channel_get_gr_global_ctxbuf(gsp, buffer_id);
			if (global_buf == NULL) {
				nvgpu_log(NVGPU_LOG_INFO,
				    "missing global ctxbuf id=%u\n", buffer_id);
				error = ENOENT;
				goto out_done;
			}
			buf->paddr = global_buf->paddr;
			alloc_size = global_buf->size;
		}
		buf->size = alloc_size;
		buf->gva = next_gva;
		buf->buffer_id = buffer_id;
		buf->target = target;
		buf->init = init;
		buf->ro = ro;
		buf->nonmapped = entry_nonmapped;
		chan->gr_ctxbuf_nr++;

		if (init && alloc) {
			error = nvgsp_channel_zero_vram(gsp, buf->paddr,
			    buf->size);
			if (error != 0)
				goto out_done;
		}
		if (!entry_nonmapped) {
			error = nvgsp_vmm_map_vram_flags_noflush(chan->vmm,
			    buf->gva, buf->paddr, buf->size, 1, ro, 0);
			if (error != 0)
				goto out_done;
			ctx_map_dirty = true;
		}
		if (golden && global) {
			error = nvgsp_channel_save_gr_global_ctxbuf(gsp, buf);
			if (error != 0)
				goto out_done;
		}

		entry = &ctrl->promoteEntry[ctrl->entryCount++];
		entry->gpuVirtAddr = entry_nonmapped ? 0 : buf->gva;
		entry->bufferId = (uint16_t)buffer_id;
		entry->bInitialize = init && alloc;
		entry->bNonmapped = entry_nonmapped;
		if (entry->bInitialize) {
			entry->gpuPhysAddr = buf->paddr;
			entry->size = entry_size;
			entry->physAttr = 4;
		}
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gr promote ctxbuf id=%u eng=%u entry=0x%llx alloc=0x%llx pa=0x%llx va=0x%llx global=%u init=%u ro=%u target=%u nm=%u\n",
		    buffer_id, i, (unsigned long long)entry->size,
		    (unsigned long long)buf->size,
		    (unsigned long long)entry->gpuPhysAddr,
		    (unsigned long long)entry->gpuVirtAddr, global, init, ro,
		    target, entry_nonmapped);
		next_gva += buf->size;

		if (buffer_id == NV2080_CTXBUF_ID_PRIV_ACCESS_MAP) {
			if (ctrl->entryCount >=
			    NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES ||
			    chan->gr_ctxbuf_nr >= NVGSP_GR_MAX_CTXBUFS) {
				error = ENOSPC;
				goto out_done;
			}

			next_gva = NVGSP_ALIGN_UP(next_gva, gva_align);
			buf = &chan->gr_ctxbuf[chan->gr_ctxbuf_nr];
			if (alloc) {
				buf->paddr = nvgsp_vram_alloc_kind(gsp,
				    alloc_size, mem_align, golden && global ?
				    NVGSP_VRAM_GR_CTXBUF_GLOBAL :
				    NVGSP_VRAM_GR_CTXBUF_CHANNEL, chan);
				if (buf->paddr == 0) {
					error = ENOMEM;
					goto out_done;
				}
			} else {
				struct nvgsp_gr_ctxbuf *global_buf;

				global_buf = nvgsp_channel_get_gr_global_ctxbuf(gsp,
				    NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP);
				if (global_buf == NULL) {
					nvgpu_log(NVGPU_LOG_INFO,
					    "missing global ctxbuf id=%u\n",
					    NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP);
					error = ENOENT;
					goto out_done;
				}
				buf->paddr = global_buf->paddr;
				alloc_size = global_buf->size;
			}
			buf->size = alloc_size;
			buf->gva = next_gva;
			buf->buffer_id =
			    NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP;
			buf->target = target;
			buf->init = init;
			buf->ro = ro;
			buf->nonmapped = 0;
			chan->gr_ctxbuf_nr++;

			if (init && alloc) {
				error = nvgsp_channel_zero_vram(gsp, buf->paddr,
				    buf->size);
				if (error != 0)
					goto out_done;
			}
			error = nvgsp_vmm_map_vram_flags_noflush(chan->vmm,
			    buf->gva, buf->paddr, buf->size, 1, ro, 0);
			if (error != 0)
				goto out_done;
			ctx_map_dirty = true;
			if (golden && global) {
				error = nvgsp_channel_save_gr_global_ctxbuf(gsp, buf);
				if (error != 0)
					goto out_done;
			}

			entry = &ctrl->promoteEntry[ctrl->entryCount++];
			entry->gpuVirtAddr = buf->gva;
			entry->bufferId =
			    NV2080_CTXBUF_ID_UNRESTRICTED_PRIV_ACCESS_MAP;
			entry->bInitialize = init && alloc;
			entry->bNonmapped = 0;
			if (entry->bInitialize) {
				entry->gpuPhysAddr = buf->paddr;
				entry->size = entry_size;
				entry->physAttr = 4;
			}
			nvgpu_log(NVGPU_LOG_DEBUG,
			    "gr promote ctxbuf id=%u eng=%u entry=0x%llx alloc=0x%llx pa=0x%llx va=0x%llx global=%u init=%u ro=%u target=%u nm=%u\n",
			    entry->bufferId, i, (unsigned long long)entry->size,
			    (unsigned long long)buf->size,
			    (unsigned long long)entry->gpuPhysAddr,
			    (unsigned long long)entry->gpuVirtAddr, global, init,
			    ro, target, 0);
			next_gva += buf->size;
		}
	}

	if (ctx_map_dirty) {
		nvgsp_vmm_flush(chan->vmm);
		ctx_map_flushed = true;
	}

	uint32_t entry_count = ctrl->entryCount;
	error = nvgsp_rm_write_ctrl(&chan->vmm->device.subdevice, ctrl);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "GPU_PROMOTE_CTX chan=0x%x chid=%d golden=%u entries=%u err=%d\n",
	    chan->object.handle, chan->chid, golden, entry_count, error);
	if (error == 0)
		chan->gr_ctx_promoted = 1;

out_done:
	if (error != 0 && ctx_map_dirty && !ctx_map_flushed)
		nvgsp_vmm_flush(chan->vmm);
	nvgsp_rm_complete_ctrl(&tmp_subdev, info);
	return (error);
}

int
nvgsp_channel_create_bootstrap(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan;
	struct nvgsp_vmm *vmm;
	uint32_t mthdbuf_size, userd_page;
	int error;

	if (gsp == NULL || gsp->kernel_vmm == NULL)
		return (ENXIO);
	vmm = gsp->kernel_vmm;
	chan = kmalloc(sizeof(*chan), M_NVGSP_CHANNEL, M_WAITOK | M_ZERO);
	chan->vmm = vmm;
	chan->chid = -1;
	chan->engine_type = NV2080_ENGINE_TYPE_COPY2;
	chan->gpf_free = NV_CHANNEL_GPFIFO_ENTRIES - 1;
	chan->submit_gva_push = NVGSP_VMM_CLIENT_BASE +
	    (uint64_t)vmm->submit_gva_slot * NVGSP_SUBMIT_GVA_STRIDE;
	chan->submit_gva_gpf = chan->submit_gva_push + 0x1000;
	chan->submit_gva_sema = chan->submit_gva_push + 0x2000;
	vmm->submit_gva_slot++;
	gsp->bootstrap_channel = chan;

	chan->chid = nvgsp_channel_alloc_chid(gsp);
	if (chan->chid < 0) {
		error = ENOMEM;
		goto fail;
	}
	lwkt_gettoken(&gsp->chid_tok);
	gsp->chid_channel[chan->chid] = chan;
	lwkt_reltoken(&gsp->chid_tok);

	userd_page = (uint32_t)chan->chid / 8u;
	chan->inst_vram = nvgsp_vram_alloc_kind(gsp, NV_CHANNEL_INST_SIZE, 0x1000,
	    NVGSP_VRAM_CHANNEL_INST, chan);
	chan->userd_vram = nvgsp_vram_alloc_kind(gsp,
	    (uint64_t)(userd_page + 1) * 0x1000, 0x1000,
	    NVGSP_VRAM_CHANNEL_USERD, chan);
	if (chan->inst_vram == 0 || chan->userd_vram == 0) {
		error = ENOMEM;
		goto fail;
	}

	error = nvgsp_bar_map_bar1_existing(gsp, chan->inst_vram, &chan->inst_bar1_gva);
	if (error != 0)
		goto fail;
	nvgsp_bar_invalidate_bar1(gsp);
	nvgsp_channel_zero_inst(gsp, chan);
	nvgsp_channel_write_inst_vmm(gsp, chan, vmm);

	mthdbuf_size = gsp->mthdbuf_size != 0 ? gsp->mthdbuf_size : 0x4000u;
	error = nvgsp_dma_alloc_dmamem(gsp, mthdbuf_size, 0x1000,
	    &chan->mthdbuf);
	if (error != 0)
		goto fail;

	error = nvgsp_channel_submit_dmamem_alloc(gsp, chan);
	if (error != 0)
		goto fail;
	error = nvgsp_channel_map_submit_pages(vmm, chan);
	if (error != 0)
		goto fail;

	error = nvgsp_bar_map_bar1_existing(gsp,
	    chan->userd_vram + (uint64_t)userd_page * 0x1000,
	    &chan->userd_bar1_gva);
	if (error != 0)
		goto fail;
	nvgsp_bar_invalidate_bar1(gsp);
	nvgsp_channel_clear_userd(gsp, chan);

	error = nvgsp_channel_alloc_rm_common(vmm, chan,
	    NVGSP_RM_CHANNEL | (uint32_t)chan->chid, NV2080_ENGINE_TYPE_COPY2, 1,
	    chan->inst_vram,
	    chan->userd_vram + (uint64_t)chan->chid * NV_USERD_SLOT_SIZE,
	    chan->mthdbuf.paddr, mthdbuf_size, chan->submit_gva_gpf,
	    NV_CHANNEL_GPFIFO_ENTRIES * 8);
	if (error != 0)
		goto fail;
	error = nvgsp_channel_bind_engine(chan, NV2080_ENGINE_TYPE_COPY2);
	if (error != 0)
		goto fail;
	error = nvgsp_channel_schedule(chan);
	if (error != 0)
		goto fail;
	error = nvgsp_channel_alloc_ce_object(chan, NV2080_ENGINE_TYPE_COPY2);
	if (error != 0)
		goto fail;
	error = nvgsp_channel_get_work_submit_token(chan);
	if (error != 0)
		goto fail;

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "bootstrap channel ready chid=%d token=0x%x inst=0x%llx userd=0x%llx gpf=0x%llx\n",
	    chan->chid, chan->gsp_token, (unsigned long long)chan->inst_vram,
	    (unsigned long long)chan->userd_vram, (unsigned long long)chan->submit_gva_gpf);
	return (0);

fail:
	nvgsp_channel_destroy_bootstrap(gpu);
	return (error);
}

void
nvgsp_channel_destroy_bootstrap(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan = gsp != NULL ? gsp->bootstrap_channel : NULL;

	if (chan == NULL)
		return;
	if (chan->ce_obj.handle != 0)
		nvgsp_rm_free(&chan->ce_obj);
	if (chan->object.handle != 0)
		nvgsp_rm_free(&chan->object);
	if (chan->userd_bar1_gva != 0)
		nvgsp_bar_unmap_bar1_existing(gsp, chan->userd_bar1_gva);
	if (chan->inst_bar1_gva != 0)
		nvgsp_bar_unmap_bar1_existing(gsp, chan->inst_bar1_gva);
	if (chan->submit_gva_push != 0 && chan->vmm != NULL)
		(void)nvgsp_vmm_unmap(chan->vmm, chan->submit_gva_push, 0x3000);
	nvgsp_dma_free_dmamem(gsp, &chan->mthdbuf);
	nvgsp_channel_submit_dmamem_free(gsp, chan);
	nvgsp_vram_free_kind(gsp, chan->inst_vram, NVGSP_VRAM_CHANNEL_INST, chan);
	nvgsp_vram_free_kind(gsp, chan->userd_vram, NVGSP_VRAM_CHANNEL_USERD, chan);
	nvgsp_channel_free_chid(gsp, chan->chid);
	kfree(chan, M_NVGSP_CHANNEL);
	gsp->bootstrap_channel = NULL;
}

/* Create a user submission channel on an existing per-process VMM. */
int
nvgsp_channel_create_user(struct nvgsp_vmm *vmm, uint32_t engine_type,
    struct nvgsp_channel **out)
{
	struct nvgsp_state *gsp;
	struct nvgsp_channel *chan;
	uint32_t mthdbuf_size, userd_page;
	int error;

	if (vmm == NULL || out == NULL || vmm->gsp == NULL)
		return (EINVAL);
	gsp = vmm->gsp;
	if (engine_type == NV2080_ENGINE_TYPE_GRAPHICS) {
		error = nvgsp_channel_create_golden(gsp->gpu);
		if (error != 0)
			return (error);
	}
	chan = kmalloc(sizeof(*chan), M_NVGSP_CHANNEL, M_WAITOK | M_ZERO);
	chan->vmm = vmm;
	chan->chid = -1;
	chan->engine_type = engine_type;
	chan->gpf_free = NV_CHANNEL_GPFIFO_ENTRIES - 1;
	chan->submit_gva_push = NVGSP_VMM_CLIENT_BASE +
	    (uint64_t)vmm->submit_gva_slot * NVGSP_SUBMIT_GVA_STRIDE;
	chan->submit_gva_gpf = chan->submit_gva_push + 0x1000;
	chan->submit_gva_sema = chan->submit_gva_push + 0x2000;
	vmm->submit_gva_slot++;

	lwkt_gettoken(&gsp->gsp_tok);
	chan->chid = nvgsp_channel_alloc_chid(gsp);
	if (chan->chid < 0) {
		error = ENOMEM;
		goto fail_locked;
	}
	lwkt_gettoken(&gsp->chid_tok);
	gsp->chid_channel[chan->chid] = chan;
	lwkt_reltoken(&gsp->chid_tok);

	userd_page = (uint32_t)chan->chid / 8u;
	chan->inst_vram = nvgsp_vram_alloc_kind(gsp, NV_CHANNEL_INST_SIZE,
	    0x1000, NVGSP_VRAM_CHANNEL_INST, chan);
	chan->userd_vram = nvgsp_vram_alloc_kind(gsp,
	    (uint64_t)(userd_page + 1) * 0x1000, 0x1000,
	    NVGSP_VRAM_CHANNEL_USERD, chan);
	if (chan->inst_vram == 0 || chan->userd_vram == 0) {
		error = ENOMEM;
		goto fail_locked;
	}

	error = nvgsp_bar_map_bar1_existing(gsp, chan->inst_vram,
	    &chan->inst_bar1_gva);
	if (error != 0)
		goto fail_locked;
	nvgsp_bar_invalidate_bar1(gsp);
	nvgsp_channel_zero_inst(gsp, chan);
	nvgsp_channel_write_inst_vmm(gsp, chan, vmm);

	mthdbuf_size = gsp->mthdbuf_size != 0 ? gsp->mthdbuf_size : 0x4000u;
	error = nvgsp_dma_alloc_dmamem(gsp, mthdbuf_size, 0x1000,
	    &chan->mthdbuf);
	if (error != 0)
		goto fail_locked;

	error = nvgsp_channel_submit_dmamem_alloc(gsp, chan);
	if (error != 0)
		goto fail_locked;
	error = nvgsp_channel_map_submit_pages(vmm, chan);
	if (error != 0)
		goto fail_locked;

	error = nvgsp_bar_map_bar1_existing(gsp,
	    chan->userd_vram + (uint64_t)userd_page * 0x1000,
	    &chan->userd_bar1_gva);
	if (error != 0)
		goto fail_locked;
	nvgsp_bar_invalidate_bar1(gsp);
	nvgsp_channel_clear_userd(gsp, chan);

	error = nvgsp_channel_alloc_rm_common(vmm, chan,
	    NVGSP_RM_CHANNEL | (uint32_t)chan->chid, engine_type, 1,
	    chan->inst_vram,
	    chan->userd_vram + (uint64_t)chan->chid * NV_USERD_SLOT_SIZE,
	    chan->mthdbuf.paddr, mthdbuf_size, chan->submit_gva_gpf,
	    NV_CHANNEL_GPFIFO_ENTRIES * 8);
	if (error != 0)
		goto fail_locked;
	error = nvgsp_channel_bind_engine(chan, engine_type);
	if (error != 0)
		goto fail_locked;
	error = nvgsp_channel_schedule(chan);
	if (error != 0)
		goto fail_locked;
	error = nvgsp_channel_alloc_ce_object(chan, engine_type);
	if (error != 0)
		goto fail_locked;
	error = nvgsp_channel_get_work_submit_token(chan);
	if (error != 0)
		goto fail_locked;
	lwkt_reltoken(&gsp->gsp_tok);

	*out = chan;
	return (0);

fail_locked:
	lwkt_reltoken(&gsp->gsp_tok);
	nvgsp_channel_destroy_user(chan);
	return (error);
}

/* Destroy a user submission channel after scheduler work has drained. */
void
nvgsp_channel_destroy_user(struct nvgsp_channel *chan)
{
	struct nvgsp_state *gsp;

	if (chan == NULL || chan->vmm == NULL)
		return;
	for (uint32_t i = 0; i < NVGSP_CHANNEL_POST_RING_SLOTS; i++) {
		uint64_t bit = 1ULL << i;
		volatile uint32_t *sema;
		uint32_t value;

		if ((chan->submit_post_slots_busy & bit) == 0)
			continue;
		sema = (volatile uint32_t *)chan->submit_sema.kva + i;
		cpu_lfence();
		value = *sema;
		if ((int32_t)(value - chan->submit_post_payload[i]) >= 0)
			chan->submit_post_slots_busy &= ~bit;
	}
	KASSERT(chan->submit_post_slots_busy == 0,
	    ("destroying channel with active submit slots"));
	gsp = chan->vmm->gsp;
	if (gsp != NULL)
		lwkt_gettoken(&gsp->gsp_tok);
	if (chan->ce_obj.handle != 0)
		nvgsp_rm_free(&chan->ce_obj);
	if (chan->object.handle != 0)
		nvgsp_rm_free(&chan->object);
	if (chan->userd_bar1_gva != 0)
		nvgsp_bar_unmap_bar1_existing(gsp, chan->userd_bar1_gva);
	if (chan->inst_bar1_gva != 0)
		nvgsp_bar_unmap_bar1_existing(gsp, chan->inst_bar1_gva);
	if (chan->submit_gva_push != 0 && chan->vmm != NULL)
		(void)nvgsp_vmm_unmap(chan->vmm, chan->submit_gva_push, 0x3000);
	nvgsp_dma_free_dmamem(gsp, &chan->mthdbuf);
	if (gsp != NULL) {
		nvgsp_channel_submit_dmamem_free(gsp, chan);
		nvgsp_vram_free_kind(gsp, chan->inst_vram,
		    NVGSP_VRAM_CHANNEL_INST, chan);
		nvgsp_vram_free_kind(gsp, chan->userd_vram,
		    NVGSP_VRAM_CHANNEL_USERD, chan);
		nvgsp_channel_free_chid(gsp, chan->chid);
		lwkt_reltoken(&gsp->gsp_tok);
	}
	kfree(chan, M_NVGSP_CHANNEL);
}

/* Promote GR context buffers for a user channel before GR-class object allocation. */
int
nvgsp_channel_promote_graphics_context(struct nvgsp_channel *chan)
{
	struct nvgsp_state *gsp;
	int error;

	if (chan == NULL || chan->vmm == NULL || chan->vmm->gsp == NULL)
		return (ENXIO);
	gsp = chan->vmm->gsp;
	lwkt_gettoken(&gsp->gsp_tok);
	error = nvgsp_channel_promote_gr_context(chan, 0);
	lwkt_reltoken(&gsp->gsp_tok);
	return (error);
}

/* Allocate an RM engine object under a user channel. */
int
nvgsp_channel_alloc_object(struct nvgsp_channel *chan, uint32_t handle,
    uint32_t oclass, struct nvgsp_channel_object **out)
{
	struct nvgsp_channel_object *obj;
	struct nvgsp_state *gsp;
	void *args;
	int error;

	if (chan == NULL || chan->vmm == NULL || chan->vmm->gsp == NULL ||
	    out == NULL)
		return (EINVAL);
	gsp = chan->vmm->gsp;
	obj = kmalloc(sizeof(*obj), M_NVGSP_CHANNEL, M_WAITOK | M_ZERO);

	lwkt_gettoken(&gsp->gsp_tok);
	switch (oclass) {
	case TURING_DMA_COPY_A: {
		struct {
			uint32_t version;
			uint32_t engineType;
		} *copy;

		copy = nvgsp_rm_get_alloc(&chan->object, handle, oclass,
		    sizeof(*copy), &obj->object);
		if (copy == NULL) {
			error = ENOMEM;
			break;
		}
		copy->version = 1;
		copy->engineType = NV2080_ENGINE_TYPE_COPY0;
		error = nvgsp_rm_write_alloc(&obj->object, copy);
		break;
	}
	case TURING_A:
	case TURING_COMPUTE_A:
	case FERMI_TWOD_A:
		args = nvgsp_rm_get_alloc(&chan->object, handle, oclass, 0,
		    &obj->object);
		if (args == NULL) {
			error = ENOMEM;
			break;
		}
		error = nvgsp_rm_write_alloc(&obj->object, args);
		break;
	case KEPLER_INLINE_TO_MEMORY_B:
		args = nvgsp_rm_get_alloc(&chan->object, handle, oclass, 0,
		    &obj->object);
		if (args == NULL) {
			error = ENOMEM;
			break;
		}
		error = nvgsp_rm_write_alloc(&obj->object, args);
		break;
	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(&gsp->gsp_tok);

	if (error != 0) {
		kfree(obj, M_NVGSP_CHANNEL);
		return (error);
	}
	*out = obj;
	return (0);
}

/* Free an RM engine object allocated by nvgsp_channel_alloc_object(). */
void
nvgsp_channel_free_object(struct nvgsp_channel_object *obj)
{
	struct nvgsp_state *gsp;

	if (obj == NULL)
		return;
	gsp = obj->object.client != NULL ? obj->object.client->gsp : NULL;
	if (gsp != NULL)
		lwkt_gettoken(&gsp->gsp_tok);
	(void)nvgsp_rm_free(&obj->object);
	if (gsp != NULL)
		lwkt_reltoken(&gsp->gsp_tok);
	kfree(obj, M_NVGSP_CHANNEL);
}

int
nvgsp_channel_create_golden(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan;
	uint32_t mthdbuf_size;
	int error;

	KTR_LOG(nvgpu_gsp_golden_stage, 0u, 0);
	if (gsp == NULL)
		return (ENXIO);
	lwkt_gettoken(&gsp->gsp_tok);
	if (gsp->golden_channel != NULL) {
		lwkt_reltoken(&gsp->gsp_tok);
		return (0);
	}
	error = nvgsp_vmm_create_golden(gpu);
	if (error != 0)
		goto done;
	KTR_LOG(nvgpu_gsp_golden_stage, 1u, 0);

	chan = kmalloc(sizeof(*chan), M_NVGSP_CHANNEL, M_WAITOK | M_ZERO);
	chan->vmm = gsp->golden_vmm;
	chan->chid = 1;
	chan->engine_type = NV2080_ENGINE_TYPE_GRAPHICS;
	gsp->golden_channel = chan;

	chan->inst_vram = nvgsp_vram_alloc_kind(gsp, NV_CHANNEL_GOLDEN_SIZE,
	    0x1000, NVGSP_VRAM_CHANNEL_GOLDEN, chan);
	if (chan->inst_vram == 0) {
		error = ENOMEM;
		goto fail;
	}
	chan->userd_vram = chan->inst_vram + 0x1000;
	chan->mthdbuf.paddr = chan->inst_vram + 0x2000;
	mthdbuf_size = gsp->mthdbuf_size != 0 ? gsp->mthdbuf_size : 0x4000u;
	chan->mthdbuf.size = mthdbuf_size;

	error = nvgsp_channel_zero_vram(gsp, chan->inst_vram, NV_CHANNEL_GOLDEN_SIZE);
	if (error != 0)
		goto fail;
	KTR_LOG(nvgpu_gsp_golden_stage, 2u, 0);
	error = nvgsp_channel_alloc_rm_common(gsp->golden_vmm, chan,
	    NVGSP_RM_CHANNEL, NV2080_ENGINE_TYPE_GRAPHICS, 1, chan->inst_vram,
	    chan->userd_vram, chan->mthdbuf.paddr, mthdbuf_size, 0, 0x1000);
	if (error != 0)
		goto fail;
	KTR_LOG(nvgpu_gsp_golden_stage, 3u, 0);
	error = nvgsp_channel_promote_gr_context(chan, 1);
	if (error != 0)
		goto fail;
	KTR_LOG(nvgpu_gsp_golden_stage, 4u, 0);
	error = nvgsp_channel_alloc_graphics_object(gpu);
	if (error != 0)
		goto fail;
	KTR_LOG(nvgpu_gsp_golden_stage, 5u, 0);
	(void)nvgsp_rm_free_graphics_object(gpu);
	KTR_LOG(nvgpu_gsp_golden_stage, 6u, 0);
	goto done;

fail:
	KTR_LOG(nvgpu_gsp_golden_stage, 7u, error);
	nvgsp_channel_destroy_golden(gpu);
done:
	lwkt_reltoken(&gsp->gsp_tok);
	return (error);
}

void
nvgsp_channel_destroy_golden(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_channel *chan = gsp != NULL ? gsp->golden_channel : NULL;

	if (chan != NULL) {
		if (chan->object.handle != 0)
			nvgsp_rm_free(&chan->object);
		nvgsp_channel_free_gr_ctxbufs(gsp, chan);
		nvgsp_vram_free_kind(gsp, chan->inst_vram,
		    NVGSP_VRAM_CHANNEL_GOLDEN, chan);
		kfree(chan, M_NVGSP_CHANNEL);
		gsp->golden_channel = NULL;
	}
	if (gsp != NULL)
		gsp->gr_ctxbuf_global_nr = 0;
	nvgsp_vmm_destroy_golden(gpu);
}
