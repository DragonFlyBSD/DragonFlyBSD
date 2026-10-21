/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP display backend for the native NVIDIA GPU driver.
 */

#include "nvgsp_disp.h"
#include "nvgsp_disp_abi.h"
#include "nvgsp_priv.h"

#include "nvhw/drf.h"

#define NVGSP_RM_DISPLAY_COMMON 0x00730000u
#define NVGSP_DISPLAY_CAPS_BASE 0x00640000u
#define NVGSP_DISPLAY_CAPS_SOR_OFFSET 0x00000144u
#define NVGSP_DISPLAY_CAPS_SOR_STRIDE 0x00000008u
#define NVGSP_DISPLAY_CAPS_SOR_DP_INTERLACE 0x04000000u
#define NVGSP_DISPLAY_RAMHT_SIZE 0x1000u
#define NVGSP_DISPLAY_RAMHT_SLOTS (NVGSP_DISPLAY_RAMHT_SIZE / 8u)
#define NVGSP_DISPLAY_PUSH_SIZE 0x1000u
#define NVGSP_DISPLAY_EVENT_CLASS 0x0000007eu
#define NVGSP_DISPLAY_EVENT_CLIENT_RM 0x04000000u
#define NVGSP_DISPLAY_EVENT_HPD_HANDLE 0x007e0000u
#define NVGSP_DISPLAY_EVENT_DP_IRQ_HANDLE 0x007e0001u
#define NVGSP_DISPLAY_EVENT_HPD_ID 1u
#define NVGSP_DISPLAY_EVENT_DP_IRQ_ID 7u
#define NVGSP_DISPLAY_EVENT_SET_NOTIFICATION 0x20800301u
#define NVGSP_DISPLAY_EVENT_ACTION_REPEAT 2u

struct nvgsp_display_event_alloc {
	uint32_t parent_client;
	uint32_t source_resource;
	uint32_t oclass;
	uint32_t notify_index;
	uint64_t data;
};

struct nvgsp_display_event_notification {
	uint32_t event;
	uint32_t action;
	uint32_t notify_state;
	uint32_t info32;
	uint16_t info16;
	uint8_t pad[2];
};

struct nvgsp_display_ramht_entry {
	int chid;
	uint32_t handle;
};

struct nvgsp_display_channel {
	struct nvgsp_display *display;
	struct nvgsp_object object;
	struct nvgsp_dmamem push;
	uint32_t oclass;
	uint32_t instance;
	uint32_t chid;
	uint32_t user_offset;
	uint32_t user_size;
};

struct nvgsp_display {
	struct nvgsp_state *gsp;
	struct nvgsp_client client;
	struct nvgsp_device device;
	struct nvgsp_object common;
	struct nvgsp_object root;
	struct nvgsp_object hotplug_event;
	struct nvgsp_object dp_irq_event;
	struct nvgsp_display_event_ops event_ops;
	void *event_arg;
	uint64_t inst_paddr;
	uint64_t inst_gva;
	uint32_t ctx_next;
	uint32_t supported_mask;
	uint32_t head_count;
	uint32_t head_mask;
	uint32_t window_mask;
	uint32_t output_count;
	uint32_t assigned_sors;
	struct nvgsp_display_output outputs[NVGSP_DISPLAY_MAX_OUTPUTS];
	struct nvgsp_display_ramht_entry ramht[NVGSP_DISPLAY_RAMHT_SLOTS];
};

static bool
nvgsp_disp_output_supports_interlace(struct nvgsp_display *display,
    uint32_t or_mask)
{
	uint32_t caps;
	uint32_t sor;
	bool found = false;

	if (or_mask == 0 || display->gsp->chip->display_sors >= 32 ||
	    (or_mask & ~((1u << display->gsp->chip->display_sors) - 1u)) != 0)
		return (false);
	for (sor = 0; sor < display->gsp->chip->display_sors; sor++) {
		if ((or_mask & (1u << sor)) == 0)
			continue;
		found = true;
		caps = nvgsp_rd32(display->gsp, NVGSP_DISPLAY_CAPS_BASE +
		    NVGSP_DISPLAY_CAPS_SOR_OFFSET + sor * NVGSP_DISPLAY_CAPS_SOR_STRIDE);
		if ((caps & NVGSP_DISPLAY_CAPS_SOR_DP_INTERLACE) == 0)
			return (false);
	}
	return (found);
}

/* Build the RM display object graph and cache immutable output metadata. */
int
nvgsp_disp_init(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display *display;
	struct nvgsp_client internal_client;
	struct nvgsp_object internal_subdevice;
	NV2080_CTRL_INTERNAL_DISPLAY_WRITE_INST_MEM_PARAMS *inst;
	NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS *static_info;
	NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS *supported;
	NV0073_CTRL_SPECIFIC_OR_GET_INFO_PARAMS *or_info;
	NV0073_CTRL_SPECIFIC_GET_CONNECTOR_DATA_PARAMS *connector;
	NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS *dp_caps;
	struct nvgsp_display_output *output;
	struct nvgsp_display_event_alloc *event_args;
	struct nvgsp_display_event_notification *event_ctrl;
	struct nvgsp_object *event_object;
	void *reply;
	void *args;
	uint32_t id;
	uint32_t event_index;
	int error;

	if (gsp == NULL || gsp->kernel_vmm == NULL || gsp->gsp_internal_client == 0 ||
	    gsp->gsp_internal_subdevice == 0)
		return (ENXIO);
	if (gsp->display != NULL)
		return (0);

	display = kmalloc(sizeof(*display), M_DEVBUF, M_WAITOK | M_ZERO);
	display->gsp = gsp;
	display->inst_paddr = nvgsp_vram_alloc_kind(gsp, 0x10000, 0x10000,
	    NVGSP_VRAM_DISPLAY_INST, display);
	if (display->inst_paddr == 0) {
		error = ENOMEM;
		goto fail_display;
	}
	error = nvgsp_bar_map_bar1_existing_range(gsp, display->inst_paddr,
	    0x10000, &display->inst_gva);
	if (error != 0)
		goto fail_inst;
	nvgsp_bar_set_bar1_region64(gsp, display->inst_gva, 0, 0x10000 / 8);
	nvgsp_bar_flush_bar1(gsp);
	display->ctx_next = NVGSP_DISPLAY_RAMHT_SIZE;
	for (id = 0; id < NVGSP_DISPLAY_RAMHT_SLOTS; id++)
		display->ramht[id].chid = -1;

	memset(&internal_client, 0, sizeof(internal_client));
	internal_client.gsp = gsp;
	internal_client.object.client = &internal_client;
	internal_client.object.handle = gsp->gsp_internal_client;
	memset(&internal_subdevice, 0, sizeof(internal_subdevice));
	internal_subdevice.client = &internal_client;
	internal_subdevice.handle = gsp->gsp_internal_subdevice;

	inst = nvgsp_rm_get_ctrl(&internal_subdevice,
	    NV2080_CTRL_CMD_INTERNAL_DISPLAY_WRITE_INST_MEM, sizeof(*inst));
	if (inst == NULL) {
		error = ENOMEM;
		goto fail_inst_map;
	}
	inst->instMemPhysAddr = display->inst_paddr;
	inst->instMemSize = 0x10000;
	inst->instMemAddrSpace = ADDR_FBMEM;
	inst->instMemCpuCacheAttr = NV_MEMORY_WRITECOMBINED;
	error = nvgsp_rm_write_ctrl(&internal_subdevice, inst);
	if (error != 0)
		goto fail_inst_map;

	error = nvgsp_rm_construct_client(gsp, 0xc1d00073u, &display->client);
	if (error != 0)
		goto fail_inst_map;
	error = nvgsp_rm_construct_device(&display->client, &display->device);
	if (error != 0)
		goto fail_client;

	args = nvgsp_rm_get_alloc(&display->device.object, NVGSP_RM_DISPLAY_COMMON,
	    NV04_DISPLAY_COMMON, 0, &display->common);
	if (args == NULL) {
		error = ENOMEM;
		goto fail_device;
	}
	error = nvgsp_rm_write_alloc(&display->common, args);
	if (error != 0)
		goto fail_device;

	static_info = nvgsp_rm_get_ctrl(&internal_subdevice,
	    NV2080_CTRL_CMD_INTERNAL_DISPLAY_GET_STATIC_INFO, sizeof(*static_info));
	if (static_info == NULL) {
		error = ENOMEM;
		goto fail_common;
	}
	reply = static_info;
	error = nvgsp_rm_read_ctrl(&internal_subdevice, &reply, sizeof(*static_info));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&internal_subdevice, reply);
		error = error != 0 ? error : EIO;
		goto fail_common;
	}
	static_info = reply;
	display->head_count = static_info->numHeads;
	display->window_mask = static_info->windowPresentMask;
	nvgsp_rm_complete_ctrl(&internal_subdevice, static_info);
	if (display->head_count == 0 || display->head_count > gsp->chip->display_heads ||
	    display->head_count >= 32) {
		error = EINVAL;
		goto fail_common;
	}
	display->head_mask = (1u << display->head_count) - 1u;

	supported = nvgsp_rm_get_ctrl(&display->common,
	    NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED, sizeof(*supported));
	if (supported == NULL) {
		error = ENOMEM;
		goto fail_common;
	}
	reply = supported;
	error = nvgsp_rm_read_ctrl(&display->common, &reply, sizeof(*supported));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&display->common, reply);
		error = error != 0 ? error : EIO;
		goto fail_common;
	}
	supported = reply;
	display->supported_mask = supported->displayMask;
	nvgsp_rm_complete_ctrl(&display->common, supported);

	for (id = 0; id < NVGSP_DISPLAY_MAX_OUTPUTS; id++) {
		if ((display->supported_mask & (1u << id)) == 0)
			continue;
		output = &display->outputs[id];
		output->display_id = 1u << id;
		output->heads = display->head_mask;

		or_info = nvgsp_rm_get_ctrl(&display->common,
		    NV0073_CTRL_CMD_SPECIFIC_OR_GET_INFO, sizeof(*or_info));
		if (or_info == NULL) {
			error = ENOMEM;
			goto fail_common;
		}
		or_info->subDeviceInstance = 0;
		or_info->displayId = output->display_id;
		reply = or_info;
		error = nvgsp_rm_read_ctrl(&display->common, &reply, sizeof(*or_info));
		if (error != 0 || reply == NULL) {
			if (reply != NULL)
				nvgsp_rm_complete_ctrl(&display->common, reply);
			error = error != 0 ? error : EIO;
			goto fail_common;
		}
		or_info = reply;
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display output id=0x%x type=%u protocol=%u sor=%u location=%u\n",
		    output->display_id, or_info->type, or_info->protocol, or_info->index,
		    or_info->location);
		if (or_info->type == NV0073_CTRL_SPECIFIC_OR_TYPE_NONE) {
			nvgsp_rm_complete_ctrl(&display->common, or_info);
			memset(output, 0, sizeof(*output));
			continue;
		}
		if (or_info->type != NV0073_CTRL_SPECIFIC_OR_TYPE_SOR) {
			nvgsp_rm_complete_ctrl(&display->common, or_info);
			error = ENOTSUP;
			goto fail_common;
		}
		output->sor_index = or_info->index;
		output->or_mask = (1u << gsp->chip->display_sors) - 1u;
		output->output_location = or_info->location;
		switch (or_info->protocol) {
		case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_SINGLE_TMDS_A:
			output->protocol = NVGSP_DISPLAY_PROTOCOL_TMDS;
			output->link = 1;
			break;
		case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_SINGLE_TMDS_B:
			output->protocol = NVGSP_DISPLAY_PROTOCOL_TMDS;
			output->link = 2;
			break;
		case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_DUAL_TMDS:
			output->protocol = NVGSP_DISPLAY_PROTOCOL_TMDS;
			output->link = 3;
			break;
		case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_DP_A:
			output->protocol = NVGSP_DISPLAY_PROTOCOL_DP;
			output->link = 1;
			break;
		case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_DP_B:
			output->protocol = NVGSP_DISPLAY_PROTOCOL_DP;
			output->link = 2;
			break;
		default:
			nvgsp_rm_complete_ctrl(&display->common, or_info);
			error = EINVAL;
			goto fail_common;
		}
		nvgsp_rm_complete_ctrl(&display->common, or_info);

		connector = nvgsp_rm_get_ctrl(&display->common,
		    NV0073_CTRL_CMD_SPECIFIC_GET_CONNECTOR_DATA, sizeof(*connector));
		if (connector == NULL) {
			error = ENOMEM;
			goto fail_common;
		}
		connector->subDeviceInstance = 0;
		connector->displayId = output->display_id;
		reply = connector;
		error = nvgsp_rm_read_ctrl(&display->common, &reply, sizeof(*connector));
		if (error != 0 || reply == NULL) {
			if (reply != NULL)
				nvgsp_rm_complete_ctrl(&display->common, reply);
			error = error != 0 ? error : EIO;
			goto fail_common;
		}
		connector = reply;
		if (connector->count == 0) {
			nvgsp_rm_complete_ctrl(&display->common, connector);
			error = ENXIO;
			goto fail_common;
		}
		output->connector_index = connector->data[0].index;
		output->connector_type = connector->data[0].type;
		output->connector_location = connector->data[0].location;
		nvgsp_rm_complete_ctrl(&display->common, connector);

		if (output->protocol == NVGSP_DISPLAY_PROTOCOL_DP) {
			dp_caps = nvgsp_rm_get_ctrl(&display->common,
			    NV0073_CTRL_CMD_DP_GET_CAPS, sizeof(*dp_caps));
			if (dp_caps == NULL) {
				error = ENOMEM;
				goto fail_common;
			}
			dp_caps->sorIndex = ~0u;
			reply = dp_caps;
			error = nvgsp_rm_read_ctrl(&display->common, &reply, sizeof(*dp_caps));
			if (error != 0 || reply == NULL) {
				if (reply != NULL)
					nvgsp_rm_complete_ctrl(&display->common, reply);
				error = error != 0 ? error : EIO;
				goto fail_common;
			}
			dp_caps = reply;
			switch (dp_caps->maxLinkRate & 0x7u) {
			case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_1_62:
				output->max_link_rate = 0x06;
				break;
			case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_2_70:
				output->max_link_rate = 0x0a;
				break;
			case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_5_40:
				output->max_link_rate = 0x14;
				break;
			case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_8_10:
				output->max_link_rate = 0x1e;
				break;
			default:
				output->max_link_rate = 0;
				break;
			}
			output->mst_capable = dp_caps->bIsMultistreamSupported;
			output->increased_watermark = dp_caps->bHasIncreasedWatermarkLimits;
			nvgsp_rm_complete_ctrl(&display->common, dp_caps);
			output->dp_interlace_capable =
			    nvgsp_disp_output_supports_interlace(display, output->or_mask);
		}
		display->output_count++;
	}
	for (event_index = 0; event_index < 2; event_index++) {
		uint32_t event_handle = event_index == 0 ?
		    NVGSP_DISPLAY_EVENT_HPD_HANDLE : NVGSP_DISPLAY_EVENT_DP_IRQ_HANDLE;
		uint32_t event_id = event_index == 0 ?
		    NVGSP_DISPLAY_EVENT_HPD_ID : NVGSP_DISPLAY_EVENT_DP_IRQ_ID;

		event_object = event_index == 0 ? &display->hotplug_event :
		    &display->dp_irq_event;
		event_args = nvgsp_rm_get_alloc(&display->device.subdevice,
		    event_handle, NVGSP_DISPLAY_EVENT_CLASS, sizeof(*event_args),
		    event_object);
		if (event_args == NULL) {
			error = ENOMEM;
			goto fail_events;
		}
		event_args->parent_client = display->client.object.handle;
		event_args->source_resource = 0;
		event_args->oclass = NVGSP_DISPLAY_EVENT_CLASS;
		event_args->notify_index = NVGSP_DISPLAY_EVENT_CLIENT_RM | event_id;
		event_args->data = 0;
		error = nvgsp_rm_write_alloc(event_object, event_args);
		if (error != 0) {
			memset(event_object, 0, sizeof(*event_object));
			goto fail_events;
		}
		event_ctrl = nvgsp_rm_get_ctrl(&display->device.subdevice,
		    NVGSP_DISPLAY_EVENT_SET_NOTIFICATION, sizeof(*event_ctrl));
		if (event_ctrl == NULL) {
			error = ENOMEM;
			goto fail_events;
		}
		event_ctrl->event = event_id;
		event_ctrl->action = NVGSP_DISPLAY_EVENT_ACTION_REPEAT;
		error = nvgsp_rm_write_ctrl(&display->device.subdevice, event_ctrl);
		if (error != 0)
			goto fail_events;
	}

	args = nvgsp_rm_get_alloc(&display->device.object,
	    gsp->chip->class_display_root << 16, gsp->chip->class_display_root, 0,
	    &display->root);
	if (args == NULL) {
		error = ENOMEM;
		goto fail_events;
	}
	error = nvgsp_rm_write_alloc(&display->root, args);
	if (error != 0) {
		memset(&display->root, 0, sizeof(display->root));
		goto fail_events;
	}

	gsp->display = display;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display ready heads=%u head_mask=0x%x windows=0x%x outputs=0x%x count=%u\n",
	    display->head_count, display->head_mask, display->window_mask,
	    display->supported_mask, display->output_count);
	return (0);

fail_events:
	(void)nvgsp_rm_free(&display->dp_irq_event);
	(void)nvgsp_rm_free(&display->hotplug_event);
fail_common:
	(void)nvgsp_rm_free(&display->common);
fail_device:
	(void)nvgsp_rm_destroy_device(&display->device);
fail_client:
	(void)nvgsp_rm_destroy_client(&display->client);
fail_inst_map:
	nvgsp_bar_unmap_bar1_existing_range(gsp, display->inst_gva, 0x10000);
fail_inst:
	nvgsp_vram_free_kind(gsp, display->inst_paddr, NVGSP_VRAM_DISPLAY_INST,
	    display);
fail_display:
	kfree(display, M_DEVBUF);
	return (error);
}

/* Tear down display objects while GSP RPC and VRAM allocators are alive. */
void
nvgsp_disp_fini(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display *display;

	if (gsp == NULL || gsp->display == NULL)
		return;
	display = gsp->display;
	gsp->display = NULL;
	(void)nvgsp_rm_free(&display->root);
	(void)nvgsp_rm_free(&display->dp_irq_event);
	(void)nvgsp_rm_free(&display->hotplug_event);
	(void)nvgsp_rm_free(&display->common);
	(void)nvgsp_rm_destroy_device(&display->device);
	(void)nvgsp_rm_destroy_client(&display->client);
	nvgsp_bar_unmap_bar1_existing_range(gsp, display->inst_gva, 0x10000);
	nvgsp_vram_free_kind(gsp, display->inst_paddr, NVGSP_VRAM_DISPLAY_INST,
	    display);
	kfree(display, M_DEVBUF);
}

void
nvgsp_disp_set_event_ops(struct nvgpu_device *gpu,
    const struct nvgsp_display_event_ops *ops, void *arg)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL)
		return;
	lwkt_gettoken(&gsp->gsp_tok);
	if (gsp->display != NULL) {
		if (ops != NULL)
			gsp->display->event_ops = *ops;
		else
			bzero(&gsp->display->event_ops,
			    sizeof(gsp->display->event_ops));
		gsp->display->event_arg = ops != NULL ? arg : NULL;
	}
	lwkt_reltoken(&gsp->gsp_tok);
}

/* Dispatch one validated POST_EVENT while the caller holds the GSP token. */
int
nvgsp_disp_dispatch_event(struct nvgpu_device *gpu, uint32_t client_handle,
    uint32_t event_handle, const void *data, uint32_t size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display *display;

	if (gsp == NULL || gsp->display == NULL || data == NULL)
		return (ENOENT);
	display = gsp->display;
	if (client_handle != display->client.object.handle)
		return (ENOENT);
	if (event_handle == display->hotplug_event.handle) {
		const Nv2080HotplugNotification *event = data;

		if (size < sizeof(*event))
			return (EINVAL);
		if (display->event_ops.hotplug != NULL)
			display->event_ops.hotplug(display->event_arg,
			    event->plugDisplayMask, event->unplugDisplayMask);
		return (0);
	}
	if (event_handle == display->dp_irq_event.handle) {
		const Nv2080DpIrqNotification *event = data;

		if (size < sizeof(*event))
			return (EINVAL);
		if (event->displayId != 0 && display->event_ops.dp_irq != NULL)
			display->event_ops.dp_irq(display->event_arg, event->displayId);
		return (0);
	}
	return (ENOENT);
}

uint32_t
nvgsp_disp_get_supported_mask(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	return (gsp != NULL && gsp->display != NULL ?
	    gsp->display->supported_mask : 0);
}

uint32_t
nvgsp_disp_get_head_count(struct nvgpu_device *gpu)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	return (gsp != NULL && gsp->display != NULL ? gsp->display->head_count : 0);
}

int
nvgsp_disp_get_vram_range(struct nvgpu_device *gpu, uint64_t *base,
    uint64_t *size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);

	if (gsp == NULL || base == NULL || size == NULL || gsp->fb_usable_size == 0)
		return (EINVAL);
	*base = gsp->fb_usable_base;
	*size = gsp->fb_usable_size;
	return (0);
}

int
nvgsp_disp_get_output(struct nvgpu_device *gpu, uint32_t display_id,
    struct nvgsp_display_output *output)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	int id;

	if (gsp == NULL || gsp->display == NULL)
		return (ENXIO);
	if (output == NULL || display_id == 0 || (display_id & (display_id - 1)) != 0)
		return (EINVAL);
	id = ffs(display_id) - 1;
	if (id < 0 || id >= NVGSP_DISPLAY_MAX_OUTPUTS ||
	    gsp->display->outputs[id].display_id == 0)
		return (ENOENT);
	*output = gsp->display->outputs[id];
	return (0);
}

int
nvgsp_disp_detect(struct nvgpu_device *gpu, uint32_t display_id)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS *state;
	void *reply;
	int error;

	if (gsp == NULL || gsp->display == NULL || display_id == 0 ||
	    (display_id & (display_id - 1)) != 0)
		return (EINVAL);
	state = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SYSTEM_GET_CONNECT_STATE, sizeof(*state));
	if (state == NULL)
		return (ENOMEM);
	state->subDeviceInstance = 0;
	state->displayMask = display_id;
	reply = state;
	error = nvgsp_rm_read_ctrl(&gsp->display->common, &reply, sizeof(*state));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&gsp->display->common, reply);
		return (error != 0 ? error : EIO);
	}
	state = reply;
	error = (state->displayMask & display_id) != 0;
	nvgsp_rm_complete_ctrl(&gsp->display->common, state);
	return (error);
}

int
nvgsp_disp_read_edid(struct nvgpu_device *gpu, uint32_t display_id,
    uint8_t *data, uint32_t *size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_SPECIFIC_GET_EDID_V2_PARAMS *edid;
	void *reply;
	int error;

	if (gsp == NULL || gsp->display == NULL || data == NULL || size == NULL ||
	    *size == 0 || display_id == 0 || (display_id & (display_id - 1)) != 0)
		return (EINVAL);
	edid = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SPECIFIC_GET_EDID_V2, sizeof(*edid));
	if (edid == NULL)
		return (ENOMEM);
	edid->subDeviceInstance = 0;
	edid->displayId = display_id;
	reply = edid;
	error = nvgsp_rm_read_ctrl(&gsp->display->common, &reply, sizeof(*edid));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&gsp->display->common, reply);
		return (error != 0 ? error : EIO);
	}
	edid = reply;
	if (edid->bufferSize > *size) {
		error = E2BIG;
	} else {
		memcpy(data, edid->edidBuffer, edid->bufferSize);
		*size = edid->bufferSize;
		error = 0;
	}
	nvgsp_rm_complete_ctrl(&gsp->display->common, edid);
	return (error);
}

int
nvgsp_disp_transfer_aux(struct nvgpu_device *gpu, uint32_t display_id,
    uint8_t request, uint32_t address, uint8_t *data, uint8_t *size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_DP_AUXCH_CTRL_PARAMS *aux;
	void *reply;
	uint8_t transfer_size;
	int error;

	if (gsp == NULL || gsp->display == NULL || data == NULL || size == NULL ||
	    display_id == 0 || (display_id & (display_id - 1)) != 0)
		return (EINVAL);
	transfer_size = *size;
	if (transfer_size > sizeof(aux->data))
		return (E2BIG);
	aux = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_DP_AUXCH_CTRL, sizeof(*aux));
	if (aux == NULL)
		return (ENOMEM);
	aux->subDeviceInstance = 0;
	aux->displayId = display_id;
	aux->bAddrOnly = transfer_size == 0;
	aux->cmd = request;
	aux->addr = address;
	aux->size = transfer_size != 0 ? transfer_size - 1 : 0;
	memcpy(aux->data, data, transfer_size);
	reply = aux;
	error = nvgsp_rm_read_ctrl(&gsp->display->common, &reply, sizeof(*aux));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&gsp->display->common, reply);
		return (error != 0 ? error : EIO);
	}
	aux = reply;
	memcpy(data, aux->data, transfer_size);
	*size = aux->size;
	error = aux->replyType;
	nvgsp_rm_complete_ctrl(&gsp->display->common, aux);
	return (error);
}

int
nvgsp_disp_acquire_output(struct nvgpu_device *gpu, uint32_t display_id,
    bool audio, struct nvgsp_display_route *route)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display *display;
	struct nvgsp_display_output output;
	NV0073_CTRL_DFP_ASSIGN_SOR_PARAMS *assign;
	void *reply;
	uint32_t sor;
	int error;

	if (gsp == NULL || gsp->display == NULL || route == NULL)
		return (EINVAL);
	display = gsp->display;
	error = nvgsp_disp_get_output(gpu, display_id, &output);
	if (error != 0)
		return (error);
	if (route->acquired)
		return (EBUSY);
	assign = nvgsp_rm_get_ctrl(&display->common,
	    NV0073_CTRL_CMD_DFP_ASSIGN_SOR, sizeof(*assign));
	if (assign == NULL)
		return (ENOMEM);
	assign->subDeviceInstance = 0;
	assign->displayId = display_id;
	assign->sorExcludeMask = display->assigned_sors;
	if (audio)
		assign->flags |= NVDEF(NV0073_CTRL, DFP_ASSIGN_SOR_FLAGS,
		    AUDIO, OPTIMAL);
	reply = assign;
	error = nvgsp_rm_read_ctrl(&display->common, &reply, sizeof(*assign));
	if (error != 0 || reply == NULL) {
		if (reply != NULL)
			nvgsp_rm_complete_ctrl(&display->common, reply);
		return (error != 0 ? error : EIO);
	}
	assign = reply;
	for (sor = 0; sor < NV0073_CTRL_CMD_DFP_ASSIGN_SOR_MAX_SORS; sor++) {
		if ((assign->sorAssignListWithTag[sor].displayMask & display_id) != 0)
			break;
	}
	nvgsp_rm_complete_ctrl(&display->common, assign);
	if (sor >= gsp->chip->display_sors ||
	    (display->assigned_sors & (1u << sor)) != 0)
		return (EINVAL);
	display->assigned_sors |= 1u << sor;
	display->outputs[ffs(display_id) - 1].sor_index = sor;
	memset(route, 0, sizeof(*route));
	route->display_id = display_id;
	route->sor_index = sor;
	route->link = output.link;
	route->protocol = output.protocol;
	route->audio = audio;
	route->acquired = true;
	return (0);
}

void
nvgsp_disp_release_output(struct nvgpu_device *gpu,
    struct nvgsp_display_route *route)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display *display;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    !route->acquired)
		return;
	display = gsp->display;
	if (route->protocol == NVGSP_DISPLAY_PROTOCOL_DP)
		(void)nvgsp_disp_train_dp(gpu, route, 0, 0x06, false);
	if (route->sor_index < gsp->chip->display_sors)
		display->assigned_sors &= ~(1u << route->sor_index);
	if (route->display_id != 0 &&
	    (route->display_id & (route->display_id - 1)) == 0)
		display->outputs[ffs(route->display_id) - 1].sor_index = UINT32_MAX;
	memset(route, 0, sizeof(*route));
}

int
nvgsp_disp_enable_hdmi(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route,
    const struct nvgsp_display_hdmi_sink *sink)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_SPECIFIC_SET_HDMI_SINK_CAPS_PARAMS *caps;
	NV0073_CTRL_SPECIFIC_SET_HDMI_ENABLE_PARAMS *enable;
	int error;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    sink == NULL || !route->acquired ||
	    route->protocol != NVGSP_DISPLAY_PROTOCOL_TMDS)
		return (EINVAL);
	caps = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SPECIFIC_SET_HDMI_SINK_CAPS, sizeof(*caps));
	if (caps == NULL)
		return (ENOMEM);
	caps->displayId = route->display_id;
	if (sink->scdc_supported)
		caps->caps |= NVDEF(NV0073_CTRL_CMD_SPECIFIC,
		    SET_HDMI_SINK_CAPS, SCDC_SUPPORTED, TRUE);
	if (sink->scrambling_supported)
		caps->caps |= NVDEF(NV0073_CTRL_CMD_SPECIFIC,
		    SET_HDMI_SINK_CAPS, GT_340MHZ_CLOCK_SUPPORTED, TRUE);
	if (sink->low_rate_scrambling_supported)
		caps->caps |= NVDEF(NV0073_CTRL_CMD_SPECIFIC,
		    SET_HDMI_SINK_CAPS, LTE_340MHZ_SCRAMBLING_SUPPORTED, TRUE);
	error = nvgsp_rm_write_ctrl(&gsp->display->common, caps);
	if (error != 0)
		return (error);
	enable = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SPECIFIC_SET_HDMI_ENABLE, sizeof(*enable));
	if (enable == NULL)
		return (ENOMEM);
	enable->displayId = route->display_id;
	enable->enable = true;
	return (nvgsp_rm_write_ctrl(&gsp->display->common, enable));
}

void
nvgsp_disp_disable_hdmi(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route)
{
	(void)gpu;
	(void)route;
	/* RM r535 only accepts SET_HDMI_ENABLE while enabling an output. */
}

int
nvgsp_disp_configure_dp_stream(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route,
    const struct nvgsp_display_dp_stream *stream)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_CMD_DP_CONFIG_STREAM_PARAMS *config;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    stream == NULL || !route->acquired ||
	    route->protocol != NVGSP_DISPLAY_PROTOCOL_DP)
		return (EINVAL);
	config = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_DP_CONFIG_STREAM, sizeof(*config));
	if (config == NULL)
		return (ENOMEM);
	config->subDeviceInstance = 0;
	config->head = stream->head;
	config->sorIndex = route->sor_index;
	config->dpLink = route->link == 2;
	config->bEnableOverride = true;
	config->bMST = stream->mst;
	config->hBlankSym = stream->horizontal_blank_symbols;
	config->vBlankSym = stream->vertical_blank_symbols;
	config->colorFormat = 0;
	config->bEnableTwoHeadOneOr = false;
	if (!stream->mst) {
		config->SST.bEnhancedFraming = stream->enhanced_framing;
		config->SST.tuSize = 64;
		config->SST.waterMark = stream->watermark;
	}
	return (nvgsp_rm_write_ctrl(&gsp->display->common, config));
}

int
nvgsp_disp_train_dp(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route, uint32_t lane_count,
    uint32_t link_bandwidth, bool mst)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_DP_CTRL_PARAMS *train;
	void *reply;
	uint32_t retries;
	int error;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    !route->acquired || route->protocol != NVGSP_DISPLAY_PROTOCOL_DP)
		return (EINVAL);
	for (retries = 0; retries < 3; retries++) {
		train = nvgsp_rm_get_ctrl(&gsp->display->common,
		    NV0073_CTRL_CMD_DP_CTRL, sizeof(*train));
		if (train == NULL)
			return (ENOMEM);
		train->subDeviceInstance = 0;
		train->displayId = route->display_id;
		train->cmd = NVDEF(NV0073_CTRL, DP_CMD, SET_LANE_COUNT, TRUE) |
		    NVDEF(NV0073_CTRL, DP_CMD, SET_LINK_BW, TRUE) |
		    NVDEF(NV0073_CTRL, DP_CMD, TRAIN_PHY_REPEATER, YES) |
		    NVDEF(NV0073_CTRL, DP_CMD, SET_ENHANCED_FRAMING, TRUE);
		if (mst)
			train->cmd |= NVDEF(NV0073_CTRL, DP_CMD, SET_FORMAT_MODE,
			    MULTI_STREAM);
		train->data = NVVAL(NV0073_CTRL, DP_DATA, SET_LANE_COUNT,
		    lane_count) |
		    NVVAL(NV0073_CTRL, DP_DATA, SET_LINK_BW, link_bandwidth) |
		    NVDEF(NV0073_CTRL, DP_DATA, TARGET, SINK);
		reply = train;
		error = nvgsp_rm_read_ctrl(&gsp->display->common, &reply,
		    sizeof(*train));
		if (reply == NULL)
			return (error != 0 ? error : EIO);
		train = reply;
		if (error == 0 && train->err != 0)
			error = EIO;
		if ((error == EAGAIN || error == EBUSY) &&
		    train->retryTimeMs != 0) {
			uint32_t delay_ms = train->retryTimeMs;

			nvgsp_rm_complete_ctrl(&gsp->display->common, train);
			DELAY(delay_ms * 1000);
			continue;
		}
		nvgsp_rm_complete_ctrl(&gsp->display->common, train);
		return (error);
	}
	return (ETIMEDOUT);
}

int
nvgsp_disp_set_eld(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route, uint32_t head,
    const uint8_t *data, uint32_t size)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_DFP_SET_ELD_AUDIO_CAP_PARAMS *eld;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    !route->acquired || size > NV0073_CTRL_DFP_ELD_AUDIO_CAPS_ELD_BUFFER ||
	    (size != 0 && data == NULL))
		return (EINVAL);
	eld = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_DFP_SET_ELD_AUDIO_CAPS, sizeof(*eld));
	if (eld == NULL)
		return (ENOMEM);
	eld->displayId = route->display_id;
	eld->numELDSize = size;
	if (size != 0) {
		memcpy(eld->bufferELD, data, size);
		eld->ctrl = NVDEF(NV0073_CTRL, DFP_ELD_AUDIO_CAPS_CTRL, PD, TRUE) |
		    NVDEF(NV0073_CTRL, DFP_ELD_AUDIO_CAPS_CTRL, ELDV, TRUE);
	}
	eld->deviceEntry = head;
	return (nvgsp_rm_write_ctrl(&gsp->display->common, eld));
}

int
nvgsp_disp_set_audio(struct nvgpu_device *gpu,
    const struct nvgsp_display_route *route, uint32_t head, bool enable)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	NV0073_CTRL_DFP_SET_AUDIO_ENABLE_PARAMS *audio;
	NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS *dp_mute;
	NV0073_CTRL_CMD_SPECIFIC_SET_HDMI_AUDIO_MUTESTREAM_PARAMS *hdmi_mute;
	NV0073_CTRL_SPECIFIC_SET_OD_PACKET_PARAMS *packet;
	uint32_t reg;
	int error;

	if (gsp == NULL || gsp->display == NULL || route == NULL ||
	    !route->acquired)
		return (EINVAL);
	if (route->protocol == NVGSP_DISPLAY_PROTOCOL_DP) {
		if (!enable) {
			dp_mute = nvgsp_rm_get_ctrl(&gsp->display->common,
			    NV0073_CTRL_CMD_DP_SET_AUDIO_MUTESTREAM,
			    sizeof(*dp_mute));
			if (dp_mute == NULL)
				return (ENOMEM);
			dp_mute->displayId = route->display_id;
			dp_mute->mute = true;
			error = nvgsp_rm_write_ctrl(&gsp->display->common, dp_mute);
			if (error != 0)
				return (error);
		}
		audio = nvgsp_rm_get_ctrl(&gsp->display->common,
		    NV0073_CTRL_CMD_DFP_SET_AUDIO_ENABLE, sizeof(*audio));
		if (audio == NULL)
			return (ENOMEM);
		audio->displayId = route->display_id;
		audio->enable = enable;
		error = nvgsp_rm_write_ctrl(&gsp->display->common, audio);
		if (error != 0 || !enable)
			return (error);
		dp_mute = nvgsp_rm_get_ctrl(&gsp->display->common,
		    NV0073_CTRL_CMD_DP_SET_AUDIO_MUTESTREAM, sizeof(*dp_mute));
		if (dp_mute == NULL)
			return (ENOMEM);
		dp_mute->displayId = route->display_id;
		dp_mute->mute = false;
		return (nvgsp_rm_write_ctrl(&gsp->display->common, dp_mute));
	}
	if (route->protocol != NVGSP_DISPLAY_PROTOCOL_TMDS)
		return (EINVAL);
	packet = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SPECIFIC_SET_OD_PACKET, sizeof(*packet));
	if (packet == NULL)
		return (ENOMEM);
	packet->displayId = route->display_id;
	packet->transmitControl =
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		ENABLE, YES) |
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		OTHER_FRAME, DISABLE) |
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		SINGLE_FRAME, DISABLE) |
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		ON_HBLANK, DISABLE) |
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		VIDEO_FMT, SW_CONTROLLED) |
	    NVDEF(NV0073_CTRL_SPECIFIC, SET_OD_PACKET_TRANSMIT_CONTROL,
		RESERVED_LEGACY_MODE, NO);
	packet->packetSize = 10;
	packet->aPacket[0] = 0x03;
	packet->aPacket[3] = enable ? 0x10 : 0x01;
	error = nvgsp_rm_write_ctrl(&gsp->display->common, packet);
	if (error != 0)
		return (error);
	hdmi_mute = nvgsp_rm_get_ctrl(&gsp->display->common,
	    NV0073_CTRL_CMD_SPECIFIC_SET_HDMI_AUDIO_MUTESTREAM,
	    sizeof(*hdmi_mute));
	if (hdmi_mute == NULL)
		return (ENOMEM);
	hdmi_mute->displayId = route->display_id;
	hdmi_mute->mute = !enable;
	error = nvgsp_rm_write_ctrl(&gsp->display->common, hdmi_mute);
	if (error != 0)
		return (error);
	reg = nvgsp_rd32(gsp, 0x6f00c0 + head * 0x400);
	nvgsp_wr32(gsp, 0x6f00c0 + head * 0x400, reg & ~1u);
	nvgsp_wr32(gsp, 0x6f00cc + head * 0x400,
	    enable ? 0x00000010 : 0x00000001);
	nvgsp_wr32(gsp, 0x6f00c0 + head * 0x400, reg | 1u);
	return (0);
}

void
nvgsp_disp_enable_vblank(struct nvgpu_device *gpu, uint32_t head)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint32_t value;

	if (gsp == NULL || gsp->display == NULL || head >= gsp->display->head_count)
		return;
	nvgsp_wr32(gsp, 0x611800 + head * 4, 0x00000002);
	value = nvgsp_rd32(gsp, 0x611d80 + head * 4);
	nvgsp_wr32(gsp, 0x611d80 + head * 4, value | 0x00000002);
}

void
nvgsp_disp_disable_vblank(struct nvgpu_device *gpu, uint32_t head)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	uint32_t value;

	if (gsp == NULL || gsp->display == NULL || head >= gsp->display->head_count)
		return;
	value = nvgsp_rd32(gsp, 0x611d80 + head * 4);
	nvgsp_wr32(gsp, 0x611d80 + head * 4, value & ~0x00000002);
}

int
nvgsp_disp_create_dma_channel(struct nvgpu_device *gpu, uint32_t oclass,
    uint32_t instance, struct nvgsp_display_channel **channel)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display_channel *created;
	struct nvgsp_client internal_client;
	struct nvgsp_object internal_subdevice;
	NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *push;
	NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS *args;
	int error;

	if (gsp == NULL || gsp->display == NULL || channel == NULL ||
	    (oclass & 0xffu) == 0x7au)
		return (EINVAL);
	*channel = NULL;
	created = kmalloc(sizeof(*created), M_DEVBUF, M_WAITOK | M_ZERO);
	created->display = gsp->display;
	created->oclass = oclass;
	created->instance = instance;
	switch (oclass & 0xffu) {
	case 0x7d:
		created->chid = 0;
		created->user_offset = 0x680000;
		created->user_size = 0x10000;
		break;
	case 0x7e:
		created->chid = 1 + instance;
		created->user_offset = 0x690000 + instance * 0x1000;
		created->user_size = 0x1000;
		break;
	case 0x7b:
		created->chid = 33 + instance;
		created->user_offset = 0x6b0000 + instance * 0x1000;
		created->user_size = 0x1000;
		break;
	default:
		error = EINVAL;
		goto fail_channel;
	}
	error = nvgsp_dma_alloc_dmamem(gsp, NVGSP_DISPLAY_PUSH_SIZE,
	    NVGSP_DISPLAY_PUSH_SIZE, &created->push);
	if (error != 0)
		goto fail_channel;

	memset(&internal_client, 0, sizeof(internal_client));
	internal_client.gsp = gsp;
	internal_client.object.client = &internal_client;
	internal_client.object.handle = gsp->gsp_internal_client;
	memset(&internal_subdevice, 0, sizeof(internal_subdevice));
	internal_subdevice.client = &internal_client;
	internal_subdevice.handle = gsp->gsp_internal_subdevice;
	push = nvgsp_rm_get_ctrl(&internal_subdevice,
	    NV2080_CTRL_CMD_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER, sizeof(*push));
	if (push == NULL) {
		error = ENOMEM;
		goto fail_push;
	}
	push->addressSpace = ADDR_SYSMEM;
	push->physicalAddr = created->push.paddr;
	push->limit = created->push.size - 1;
	push->cacheSnoop = 1;
	push->hclass = oclass;
	push->channelInstance = instance;
	push->valid = 1;
	push->pbTargetAperture = PHYS_PCI_COHERENT;
	push->subDeviceId = 1;
	error = nvgsp_rm_write_ctrl(&internal_subdevice, push);
	if (error != 0)
		goto fail_push;

	args = nvgsp_rm_get_alloc(&gsp->display->root,
	    (oclass << 16) | instance, oclass, sizeof(*args), &created->object);
	if (args == NULL) {
		error = ENOMEM;
		goto fail_push;
	}
	args->channelInstance = instance;
	args->offset = 0;
	args->subDeviceId = 1;
	error = nvgsp_rm_write_alloc(&created->object, args);
	if (error != 0) {
		memset(&created->object, 0, sizeof(created->object));
		goto fail_push;
	}
	*channel = created;
	return (0);

fail_push:
	nvgsp_dma_free_dmamem(gsp, &created->push);
fail_channel:
	kfree(created, M_DEVBUF);
	return (error);
}

int
nvgsp_disp_create_pio_channel(struct nvgpu_device *gpu, uint32_t oclass,
    uint32_t instance, struct nvgsp_display_channel **channel)
{
	struct nvgsp_state *gsp = nvgsp_state_get(gpu);
	struct nvgsp_display_channel *created;
	struct nvgsp_client internal_client;
	struct nvgsp_object internal_subdevice;
	NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *push;
	NV50VAIO_CHANNELPIO_ALLOCATION_PARAMETERS *args;
	int error;

	if (gsp == NULL || gsp->display == NULL || channel == NULL ||
	    (oclass & 0xffu) != 0x7au)
		return (EINVAL);
	*channel = NULL;
	created = kmalloc(sizeof(*created), M_DEVBUF, M_WAITOK | M_ZERO);
	created->display = gsp->display;
	created->oclass = oclass;
	created->instance = instance;
	created->chid = 73 + instance;
	created->user_offset = 0x6d8000 + instance * 0x1000;
	created->user_size = 0x1000;

	memset(&internal_client, 0, sizeof(internal_client));
	internal_client.gsp = gsp;
	internal_client.object.client = &internal_client;
	internal_client.object.handle = gsp->gsp_internal_client;
	memset(&internal_subdevice, 0, sizeof(internal_subdevice));
	internal_subdevice.client = &internal_client;
	internal_subdevice.handle = gsp->gsp_internal_subdevice;
	push = nvgsp_rm_get_ctrl(&internal_subdevice,
	    NV2080_CTRL_CMD_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER, sizeof(*push));
	if (push == NULL) {
		error = ENOMEM;
		goto fail_channel;
	}
	push->hclass = oclass;
	push->channelInstance = instance;
	push->valid = 0;
	push->subDeviceId = 1;
	error = nvgsp_rm_write_ctrl(&internal_subdevice, push);
	if (error != 0)
		goto fail_channel;

	args = nvgsp_rm_get_alloc(&gsp->display->root,
	    (oclass << 16) | instance, oclass, sizeof(*args), &created->object);
	if (args == NULL) {
		error = ENOMEM;
		goto fail_channel;
	}
	args->channelInstance = instance;
	error = nvgsp_rm_write_alloc(&created->object, args);
	if (error != 0) {
		memset(&created->object, 0, sizeof(created->object));
		goto fail_channel;
	}
	*channel = created;
	return (0);

fail_channel:
	kfree(created, M_DEVBUF);
	return (error);
}

void
nvgsp_disp_destroy_channel(struct nvgsp_display_channel *channel)
{
	if (channel == NULL)
		return;
	(void)nvgsp_rm_free(&channel->object);
	nvgsp_dma_free_dmamem(channel->display->gsp, &channel->push);
	kfree(channel, M_DEVBUF);
}

uint32_t *
nvgsp_disp_channel_get_push(struct nvgsp_display_channel *channel,
    uint32_t *dwords)
{
	if (channel == NULL || dwords == NULL || channel->push.kva == NULL)
		return (NULL);
	*dwords = channel->push.size / sizeof(uint32_t);
	return (channel->push.kva);
}

uint32_t
nvgsp_disp_channel_read_user(struct nvgsp_display_channel *channel,
    uint32_t offset)
{
	if (channel == NULL || offset + sizeof(uint32_t) > channel->user_size)
		return (0xffffffffu);
	return (nvgsp_rd32(channel->display->gsp, channel->user_offset + offset));
}

void
nvgsp_disp_channel_write_user(struct nvgsp_display_channel *channel,
    uint32_t offset, uint32_t value)
{
	if (channel == NULL || offset + sizeof(uint32_t) > channel->user_size)
		return;
	cpu_sfence();
	nvgsp_wr32(channel->display->gsp, channel->user_offset + offset, value);
	(void)nvgsp_rd32(channel->display->gsp, channel->user_offset + offset);
}

int
nvgsp_disp_channel_bind_context(struct nvgsp_display_channel *channel,
    uint32_t handle, uint64_t start, uint64_t limit, uint32_t flags,
    int *cookie)
{
	struct nvgsp_display *display;
	uint32_t context;
	uint32_t hash;
	uint32_t first;
	uint32_t value;
	uint32_t ctx;

	if (channel == NULL || cookie == NULL || limit < start)
		return (EINVAL);
	display = channel->display;
	for (hash = 0, value = handle; value != 0; value >>= 9)
		hash ^= value & (NVGSP_DISPLAY_RAMHT_SLOTS - 1);
	hash ^= channel->chid << 5;
	hash &= NVGSP_DISPLAY_RAMHT_SLOTS - 1;
	first = hash;
	do {
		if (display->ramht[hash].chid == (int)channel->chid &&
		    display->ramht[hash].handle == handle)
			return (EEXIST);
		if (display->ramht[hash].chid < 0)
			break;
		hash = (hash + 1) & (NVGSP_DISPLAY_RAMHT_SLOTS - 1);
	} while (hash != first);
	if (display->ramht[hash].chid >= 0)
		return (ENOSPC);
	ctx = (display->ctx_next + 15u) & ~15u;
	if (ctx + 24u > 0x10000u)
		return (ENOSPC);
	display->ctx_next = ctx + 24u;
	start >>= 8;
	limit >>= 8;
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x00, flags);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x04,
	    (uint32_t)start);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x08,
	    (uint32_t)(start >> 32));
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x0c,
	    (uint32_t)limit);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x10,
	    (uint32_t)(limit >> 32));
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + ctx + 0x14, 0);
	context = channel->chid << 25 |
	    (display->client.object.handle & 0x3fffu) | (ctx << 9);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + hash * 8, handle);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + hash * 8 + 4,
	    context);
	nvgsp_bar_flush_bar1(display->gsp);
	display->ramht[hash].chid = channel->chid;
	display->ramht[hash].handle = handle;
	*cookie = hash + 1;
	return (0);
}

void
nvgsp_disp_channel_unbind_context(struct nvgsp_display_channel *channel,
    int cookie)
{
	struct nvgsp_display *display;
	uint32_t slot;

	if (channel == NULL || cookie <= 0 || cookie > NVGSP_DISPLAY_RAMHT_SLOTS)
		return;
	display = channel->display;
	slot = cookie - 1;
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + slot * 8, 0);
	nvgsp_bar_wr32_bar1(display->gsp, display->inst_gva + slot * 8 + 4, 0);
	nvgsp_bar_flush_bar1(display->gsp);
	display->ramht[slot].chid = -1;
	display->ramht[slot].handle = 0;
}
