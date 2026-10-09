/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP display backend boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_DISP_H_
#define _NVGSP_DISP_H_

#include <sys/types.h>

#define NVGSP_DISPLAY_MAX_OUTPUTS 32u

struct nvgpu_device;
struct nvgsp_object;
struct nvgsp_display_channel;

struct nvgsp_display_event_ops {
	void (*hotplug)(void *arg, uint32_t plug_mask, uint32_t unplug_mask);
	void (*dp_irq)(void *arg, uint32_t display_id);
};

enum nvgsp_display_protocol {
	NVGSP_DISPLAY_PROTOCOL_NONE = 0,
	NVGSP_DISPLAY_PROTOCOL_TMDS,
	NVGSP_DISPLAY_PROTOCOL_DP,
};

struct nvgsp_display_output {
	uint32_t display_id;
	uint32_t heads;
	uint32_t connector_index;
	uint32_t connector_type;
	uint32_t connector_location;
	uint32_t output_location;
	/* UINT32_MAX until DFP_ASSIGN_SOR selects a route for a modeset. */
	uint32_t sor_index;
	uint32_t or_mask;
	uint32_t link;
	uint32_t max_link_rate;
	enum nvgsp_display_protocol protocol;
	bool mst_capable;
	bool increased_watermark;
	bool dp_interlace_capable;
};

struct nvgsp_display_route {
	uint32_t display_id;
	uint32_t sor_index;
	uint32_t link;
	enum nvgsp_display_protocol protocol;
	bool audio;
	bool acquired;
};

struct nvgsp_display_dp_stream {
	uint32_t head;
	uint32_t lane_count;
	uint32_t link_bandwidth;
	uint32_t watermark;
	uint32_t horizontal_blank_symbols;
	uint32_t vertical_blank_symbols;
	bool enhanced_framing;
	bool mst;
};

struct nvgsp_display_hdmi_sink {
	bool scdc_supported;
	bool scrambling_supported;
	bool low_rate_scrambling_supported;
};

/* Initialize display backend before DRM/KMS registration. */
int nvgsp_disp_init(struct nvgpu_device *gpu);
/* Release display backend after DRM/KMS users have stopped. */
void nvgsp_disp_fini(struct nvgpu_device *gpu);
/* Callbacks and arg are borrowed until replaced or display teardown. */
void nvgsp_disp_set_event_ops(struct nvgpu_device *gpu,
	const struct nvgsp_display_event_ops *ops, void *arg);

/* Query immutable display capabilities cached during backend initialization. */
uint32_t nvgsp_disp_get_supported_mask(struct nvgpu_device *gpu);
uint32_t nvgsp_disp_get_head_count(struct nvgpu_device *gpu);
int nvgsp_disp_get_vram_range(struct nvgpu_device *gpu, uint64_t *base,
    uint64_t *size);

/* Query, detect, and communicate with one output identified by display_id. */
int nvgsp_disp_get_output(struct nvgpu_device *gpu, uint32_t display_id,
    struct nvgsp_display_output *output);
int nvgsp_disp_detect(struct nvgpu_device *gpu, uint32_t display_id);
int nvgsp_disp_read_edid(struct nvgpu_device *gpu, uint32_t display_id,
    uint8_t *data, uint32_t *size);
int nvgsp_disp_transfer_aux(struct nvgpu_device *gpu, uint32_t display_id,
    uint8_t request, uint32_t address, uint8_t *data, uint8_t *size);

/*
 * Acquire an output route for one modeset and release it during rollback or
 * disable.  route is caller-owned state; acquire initializes it and release
 * consumes its acquired RM state without freeing the structure.
 */
int nvgsp_disp_acquire_output(struct nvgpu_device *gpu, uint32_t display_id,
	bool audio, struct nvgsp_display_route *route);
void nvgsp_disp_release_output(struct nvgpu_device *gpu,
	struct nvgsp_display_route *route);
/* Program protocol, link, audio, and ELD state for a live acquired route. */
int nvgsp_disp_enable_hdmi(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route,
	const struct nvgsp_display_hdmi_sink *sink);
void nvgsp_disp_disable_hdmi(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route);
int nvgsp_disp_configure_dp_stream(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route,
	const struct nvgsp_display_dp_stream *stream);
int nvgsp_disp_train_dp(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route, uint32_t lane_count,
	uint32_t link_bandwidth, bool mst);
int nvgsp_disp_set_audio(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route, uint32_t head, bool enable);
int nvgsp_disp_set_eld(struct nvgpu_device *gpu,
	const struct nvgsp_display_route *route, uint32_t head,
	const uint8_t *data, uint32_t size);
void nvgsp_disp_enable_vblank(struct nvgpu_device *gpu, uint32_t head);
void nvgsp_disp_disable_vblank(struct nvgpu_device *gpu, uint32_t head);

/*
 * Create a display command channel and return one owned channel object.
 * destroy consumes it after command submission and notifier users have
 * stopped.  Creation and destruction may sleep in GSP RPC.
 */
int nvgsp_disp_create_dma_channel(struct nvgpu_device *gpu, uint32_t oclass,
    uint32_t instance, struct nvgsp_display_channel **channel);
int nvgsp_disp_create_pio_channel(struct nvgpu_device *gpu, uint32_t oclass,
    uint32_t instance, struct nvgsp_display_channel **channel);
void nvgsp_disp_destroy_channel(struct nvgsp_display_channel *channel);
/* Return the channel-owned coherent push ring; valid until channel destroy. */
uint32_t *nvgsp_disp_channel_get_push(struct nvgsp_display_channel *channel,
    uint32_t *dwords);

/* Access channel-owned USER registers while the channel remains live. */
uint32_t nvgsp_disp_channel_read_user(struct nvgsp_display_channel *channel,
    uint32_t offset);
void nvgsp_disp_channel_write_user(struct nvgsp_display_channel *channel,
    uint32_t offset, uint32_t value);

/* Bind or unbind one display context; cookie is initialized only on success. */
int nvgsp_disp_channel_bind_context(struct nvgsp_display_channel *channel,
    uint32_t handle, uint64_t start, uint64_t limit, uint32_t flags,
    int *cookie);
void nvgsp_disp_channel_unbind_context(struct nvgsp_display_channel *channel,
    int cookie);

#endif /* _NVGSP_DISP_H_ */
