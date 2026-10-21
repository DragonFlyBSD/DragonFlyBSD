/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP backend state boundary for the native NVIDIA GPU driver.
 */

#include "nvgsp_state.h"
#include "nvgsp_priv.h"

static MALLOC_DEFINE(M_NVGSP_STATE, "nvgsp_state", "nvgsp backend state");

#define NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE 128u
#define NV2080_INTR_CATEGORY_ENUM_COUNT	7u
#define NV2080_CTRL_CMD_CE_GET_FAULT_METHOD_BUFFER_SIZE 0x20802a08u
#define NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE 0x20800a5cu
#define NVGSP_CPU_INTR_LEAF_EN_SET(i)	(0x00b81200u + (i) * 4u)
#define NVGSP_CPU_INTR_TOP_EN_SET	0x00b81608u
#define NVGSP_ENGINE_IDX_DISP		2u
#define NVGSP_ENGINE_IDX_GSP		50u

struct nvgsp_intr_table_entry {
	uint16_t engine_idx;
	uint16_t pad;
	uint32_t pmc_intr_mask;
	uint32_t vector_stall;
	uint32_t vector_nonstall;
};

struct nvgsp_intr_table_params {
	uint32_t table_len;
	struct nvgsp_intr_table_entry table[
	    NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE];
	uint8_t subtree_map[NV2080_INTR_CATEGORY_ENUM_COUNT * 2];
};

struct nvgsp_fault_method_buffer_size_params {
	uint32_t size;
};

/* Return the borrowed GSP state stored on the physical GPU object. */
struct nvgsp_state *
nvgsp_state_get(struct nvgpu_device *gpu)
{
	if (gpu == NULL)
		return (NULL);
	return (nvgpu_device_get_gsp(gpu));
}

/* Return usable VRAM base parsed from static GSP info. */
uint64_t
nvgsp_state_get_fb_usable_base(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp;

	gsp = nvgsp_state_get(gpu);
	return (gsp != NULL ? gsp->fb_usable_base : 0);
}

/* Return usable VRAM bytes parsed from static GSP info. */
uint64_t
nvgsp_state_get_fb_usable_size(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp;

	gsp = nvgsp_state_get(gpu);
	return (gsp != NULL ? gsp->fb_usable_size : 0);
}

/* Allocate CPU-side GSP state.  The GPU owns the returned object until state_fini. */
int
nvgsp_state_init(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp;

	if (nvgpu_device_get_gsp(gpu) != NULL)
		return (0);

	gsp = kmalloc(sizeof(*gsp), M_NVGSP_STATE, M_WAITOK | M_ZERO);
	gsp->gpu = gpu;
	gsp->dev = nvgpu_device_get_newbus_dev(gpu);
	gsp->chip = nvgpu_device_get_chip(gpu);
	lwkt_token_init(&gsp->gsp_tok, "nvgsp");
	LIST_INIT(&gsp->gsp_pending);
	nvgpu_device_set_gsp(gpu, gsp);
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp state initialized for %s\n", gsp->chip->chip);
	return (0);
}

/* Release CPU-side GSP state after all firmware/runtime resources are gone. */
void
nvgsp_state_fini(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp;

	gsp = nvgsp_state_get(gpu);
	if (gsp == NULL)
		return;
	if (gsp->gsp != NULL || gsp->sec2 != NULL || gsp->gsp_shm.kva != NULL ||
	    gsp->wpr_meta.kva != NULL || gsp->vbios != NULL) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "gsp state still owns resources at state_fini; shutdown path incomplete\n");
	}
	nvgpu_device_set_gsp(gpu, NULL);
	kfree(gsp, M_NVGSP_STATE);
}

/* Query static GPU information from GSP. */
int
nvgsp_state_query_static_info(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	return (nvgsp_static_query_info(gsp));
}

/* Query the method-buffer size from GSP. */
int
nvgsp_state_query_mthdbuf_size(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_client client;
	struct nvgsp_object subdevice;
	struct nvgsp_fault_method_buffer_size_params *params;
	void *reply;
	int error;

	if (gsp == NULL || gsp->gsp_internal_client == 0 ||
	    gsp->gsp_internal_subdevice == 0)
		return (ENXIO);
	memset(&client, 0, sizeof(client));
	client.gsp = gsp;
	client.object.client = &client;
	client.object.handle = gsp->gsp_internal_client;
	memset(&subdevice, 0, sizeof(subdevice));
	subdevice.client = &client;
	subdevice.handle = gsp->gsp_internal_subdevice;

	lwkt_gettoken(&gsp->gsp_tok);
	params = nvgsp_rm_get_ctrl(&subdevice,
	    NV2080_CTRL_CMD_CE_GET_FAULT_METHOD_BUFFER_SIZE, sizeof(*params));
	if (params == NULL) {
		lwkt_reltoken(&gsp->gsp_tok);
		return (ENOMEM);
	}
	reply = params;
	error = nvgsp_rm_read_ctrl(&subdevice, &reply, sizeof(*params));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&subdevice, reply);
		if (error == 0)
			error = EIO;
	} else {
		params = reply;
		if (params->size == 0)
			error = EIO;
		else
			gsp->mthdbuf_size = params->size;
		nvgsp_rm_complete_ctrl(&subdevice, params);
	}
	lwkt_reltoken(&gsp->gsp_tok);
	if (error == 0)
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp method buffer size=%u\n",
		    gsp->mthdbuf_size);
	return (error);
}

/* Retrieve the interrupt table from GSP. */
int
nvgsp_state_get_intr_table(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_client client;
	struct nvgsp_object subdevice;
	struct nvgsp_intr_table_params *params;
	void *reply;
	uint32_t table_len;
	int error;

	if (gsp == NULL || gsp->gsp_internal_client == 0 ||
	    gsp->gsp_internal_subdevice == 0)
		return (ENXIO);
	memset(&client, 0, sizeof(client));
	client.gsp = gsp;
	client.object.client = &client;
	client.object.handle = gsp->gsp_internal_client;
	memset(&subdevice, 0, sizeof(subdevice));
	subdevice.client = &client;
	subdevice.handle = gsp->gsp_internal_subdevice;
	params = nvgsp_rm_get_ctrl(&subdevice,
	    NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE, sizeof(*params));
	if (params == NULL)
		return (ENOMEM);
	reply = params;
	error = nvgsp_rm_read_ctrl(&subdevice, &reply, sizeof(*params));
	if (error != 0 || reply == NULL)
		return (error != 0 ? error : EIO);
	params = reply;
	table_len = params->table_len;
	if (table_len > NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE)
		table_len = NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE;
	memset(gsp->gsp_nonstall_leaf_mask, 0, sizeof(gsp->gsp_nonstall_leaf_mask));
	memset(gsp->gsp_stall_leaf_mask, 0, sizeof(gsp->gsp_stall_leaf_mask));
	memset(gsp->gsp_engine_leaf_mask, 0, sizeof(gsp->gsp_engine_leaf_mask));
	memset(gsp->gsp_disp_leaf_mask, 0, sizeof(gsp->gsp_disp_leaf_mask));
	for (uint32_t i = 0; i < table_len; i++) {
		uint32_t vector = params->table[i].vector_nonstall;
		uint32_t stall = params->table[i].vector_stall;

		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gsp intr[%u] engine=%u pmc=0x%08x stall=%u nonstall=%u\n",
		    i, params->table[i].engine_idx,
		    params->table[i].pmc_intr_mask, stall, vector);

		if (vector != 0xffffffffu && vector / 32u < 8u)
			gsp->gsp_nonstall_leaf_mask[vector / 32u] |=
			    1u << (vector % 32u);
		if (stall == 0xffffffffu || stall / 32u >= 8u)
			continue;
		gsp->gsp_stall_leaf_mask[stall / 32u] |= 1u << (stall % 32u);
		if (params->table[i].engine_idx == NVGSP_ENGINE_IDX_GSP)
			gsp->gsp_engine_leaf_mask[stall / 32u] |=
			    1u << (stall % 32u);
		if (params->table[i].engine_idx == NVGSP_ENGINE_IDX_DISP)
			gsp->gsp_disp_leaf_mask[stall / 32u] |=
			    1u << (stall % 32u);
	}
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp interrupt table entries=%u\n",
	    params->table_len);
	for (uint32_t leaf = 0; leaf < 8; leaf++) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "gsp intr leaf=%u nonstall=0x%08x stall=0x%08x "
		    "gsp=0x%08x display=0x%08x\n", leaf,
		    gsp->gsp_nonstall_leaf_mask[leaf],
		    gsp->gsp_stall_leaf_mask[leaf],
		    gsp->gsp_engine_leaf_mask[leaf],
		    gsp->gsp_disp_leaf_mask[leaf]);
	}
	nvgsp_rm_complete_ctrl(&subdevice, reply);
	return (0);
}

int
nvgsp_state_enable_intr(struct nvgpu_device *gpu)
{
	static const uint32_t base_mask[8] = {
		0x00031c80u, 0, 0, 0, 0x0c000000u, 0, 0, 0,
	};
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return (ENXIO);
	for (uint32_t leaf = 0; leaf < 8; leaf++) {
		uint32_t mask = base_mask[leaf] |
		    gsp->gsp_nonstall_leaf_mask[leaf] |
		    gsp->gsp_stall_leaf_mask[leaf];

		if (mask != 0)
			nvgsp_wr32(gsp, NVGSP_CPU_INTR_LEAF_EN_SET(leaf), mask);
	}
	nvgsp_wr32(gsp, NVGSP_CPU_INTR_TOP_EN_SET, 0x0000000fu);
	nvgsp_wr32(gsp, gsp->chip->gsp_base + 0x004, 0x00000040u);
	return (0);
}

void
nvgsp_state_get_intr_masks(struct nvgpu_device *gpu, uint32_t leaf,
    struct nvgsp_intr_masks *masks)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	memset(masks, 0, sizeof(*masks));
	if (gsp == NULL || leaf >= 8)
		return;
	masks->nonstall = gsp->gsp_nonstall_leaf_mask[leaf];
	masks->stall = gsp->gsp_stall_leaf_mask[leaf];
	masks->engine = gsp->gsp_engine_leaf_mask[leaf];
	masks->display = gsp->gsp_disp_leaf_mask[leaf];
}

/* Enable doorbells before channel submission. */
int
nvgsp_state_enable_doorbell(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint32_t v;

	if (gsp == NULL)
		return (ENXIO);
	v = nvgsp_rd32(gsp, 0x00b65000);
	nvgsp_wr32(gsp, 0x00b65000, v | 0x80000000u);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "gsp doorbell enable 0xb65000 was 0x%08x now 0x%08x\n",
	    v, v | 0x80000000u);
	return (0);
}
