/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Display policy boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGPU_DISPLAY_H_
#define _NVGPU_DISPLAY_H_

#include <sys/types.h>

#define NVGPU_DISPLAY_EDID_SIZE 1024u
#define NVGPU_DISPLAY_ELD_SIZE 96u
#define NVGPU_DISPLAY_COLOR_LUT_SIZE 1024u

struct nvgpu_device;
struct nvgpu_display;
struct nvgpu_display_prepared_output;

enum nvgpu_display_protocol {
	NVGPU_DISPLAY_PROTOCOL_NONE = 0,
	NVGPU_DISPLAY_PROTOCOL_TMDS,
	NVGPU_DISPLAY_PROTOCOL_DP,
};

enum nvgpu_display_connector_type {
	NVGPU_DISPLAY_CONNECTOR_VGA = 0x00,
	NVGPU_DISPLAY_CONNECTOR_DVI_A = 0x01,
	NVGPU_DISPLAY_CONNECTOR_POD_VGA = 0x02,
	NVGPU_DISPLAY_CONNECTOR_DVI_I_TV_1 = 0x20,
	NVGPU_DISPLAY_CONNECTOR_DVI_I_TV_0 = 0x21,
	NVGPU_DISPLAY_CONNECTOR_DVI_I_TV_2 = 0x22,
	NVGPU_DISPLAY_CONNECTOR_DVI_I = 0x30,
	NVGPU_DISPLAY_CONNECTOR_DVI_D = 0x31,
	NVGPU_DISPLAY_CONNECTOR_DVI_ADC = 0x32,
	NVGPU_DISPLAY_CONNECTOR_DMS59_0 = 0x38,
	NVGPU_DISPLAY_CONNECTOR_DMS59_1 = 0x39,
	NVGPU_DISPLAY_CONNECTOR_LVDS = 0x40,
	NVGPU_DISPLAY_CONNECTOR_LVDS_SPWG = 0x41,
	NVGPU_DISPLAY_CONNECTOR_LVDS_REM = 0x42,
	NVGPU_DISPLAY_CONNECTOR_LVDS_SPWG_REM = 0x43,
	NVGPU_DISPLAY_CONNECTOR_TMDS = 0x45,
	NVGPU_DISPLAY_CONNECTOR_DP = 0x46,
	NVGPU_DISPLAY_CONNECTOR_EDP = 0x47,
	NVGPU_DISPLAY_CONNECTOR_MINI_DP = 0x48,
	NVGPU_DISPLAY_CONNECTOR_DOCK_VGA_0 = 0x50,
	NVGPU_DISPLAY_CONNECTOR_DOCK_VGA_1 = 0x51,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DVI_I_0 = 0x52,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DVI_I_1 = 0x53,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DVI_D_0 = 0x54,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DVI_D_1 = 0x55,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DP_0 = 0x56,
	NVGPU_DISPLAY_CONNECTOR_DOCK_DP_1 = 0x57,
	NVGPU_DISPLAY_CONNECTOR_DOCK_MINI_DP_0 = 0x58,
	NVGPU_DISPLAY_CONNECTOR_DOCK_MINI_DP_1 = 0x59,
	NVGPU_DISPLAY_CONNECTOR_HDMI_A_0 = 0x60,
	NVGPU_DISPLAY_CONNECTOR_HDMI_A_1 = 0x61,
	NVGPU_DISPLAY_CONNECTOR_HDMI_C = 0x63,
	NVGPU_DISPLAY_CONNECTOR_DMS59_DP_0 = 0x64,
	NVGPU_DISPLAY_CONNECTOR_DMS59_DP_1 = 0x65,
	NVGPU_DISPLAY_CONNECTOR_WFD = 0x70,
	NVGPU_DISPLAY_CONNECTOR_USB_C = 0x71,
};

enum nvgpu_display_format {
	NVGPU_DISPLAY_FORMAT_XRGB8888 = 0,
	NVGPU_DISPLAY_FORMAT_ARGB8888,
	NVGPU_DISPLAY_FORMAT_XBGR8888,
	NVGPU_DISPLAY_FORMAT_ABGR8888,
	NVGPU_DISPLAY_FORMAT_RGB565,
	NVGPU_DISPLAY_FORMAT_ARGB1555,
	NVGPU_DISPLAY_FORMAT_ARGB2101010,
	NVGPU_DISPLAY_FORMAT_ABGR2101010,
};

enum nvgpu_display_layout {
	NVGPU_DISPLAY_LAYOUT_PITCH = 0,
	NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR,
};

enum nvgpu_display_dither_mode {
	NVGPU_DISPLAY_DITHER_MODE_OFF = 0,
	NVGPU_DISPLAY_DITHER_MODE_ON = 1,
	NVGPU_DISPLAY_DITHER_MODE_DYNAMIC_2X2 = 513,
	NVGPU_DISPLAY_DITHER_MODE_STATIC_2X2 = 769,
	NVGPU_DISPLAY_DITHER_MODE_TEMPORAL = 1025,
	NVGPU_DISPLAY_DITHER_MODE_AUTO = 1026,
};

enum nvgpu_display_dither_depth {
	NVGPU_DISPLAY_DITHER_DEPTH_6_BPC = 0,
	NVGPU_DISPLAY_DITHER_DEPTH_8_BPC = 16,
	NVGPU_DISPLAY_DITHER_DEPTH_AUTO = 17,
};

enum nvgpu_display_underscan_mode {
	NVGPU_DISPLAY_UNDERSCAN_OFF = 0,
	NVGPU_DISPLAY_UNDERSCAN_ON = 1,
	NVGPU_DISPLAY_UNDERSCAN_AUTO = 2,
};

struct nvgpu_display_mode {
	bool interlace;
	uint32_t clock_khz;
	uint16_t hdisplay;
	uint16_t hsync_start;
	uint16_t hsync_end;
	uint16_t hblank_end;
	uint16_t htotal;
	uint32_t vdisplay;
	uint16_t vsync_start;
	uint16_t vsync_end;
	uint16_t vblank_end;
	uint16_t vtotal;
	bool negative_hsync;
	bool negative_vsync;
};

struct nvgpu_display_output_info {
	uint32_t display_id;
	uint32_t possible_heads;
	uint32_t connector_type;
	uint32_t connector_location;
	uint32_t output_location;
	uint32_t max_link_rate;
	enum nvgpu_display_protocol protocol;
	bool mst_capable;
	bool dp_interlace_capable;
};

struct nvgpu_display_output_config {
	uint8_t bpc;
	bool audio_enabled;
	bool hdmi_scdc_supported;
	bool hdmi_scrambling_supported;
	bool hdmi_low_rate_scrambling_supported;
	uint8_t eld_size;
	uint8_t eld[NVGPU_DISPLAY_ELD_SIZE];
};

struct nvgpu_display_head_config {
	struct nvgpu_display_mode mode;
	uint16_t input_width;
	uint16_t input_height;
	uint16_t output_width;
	uint16_t output_height;
	uint8_t bpc;
	bool dither_enabled;
	uint8_t dither_bits;
	uint8_t dither_mode;
};

struct nvgpu_display_scanout {
	uint64_t paddr;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	enum nvgpu_display_format format;
	enum nvgpu_display_layout layout;
	uint8_t kind;
	uint8_t block_height;
	uint32_t source_x;
	uint32_t source_y;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t output_width;
	uint32_t output_height;
};

struct nvgpu_display_cursor {
	uint64_t paddr;
	uint32_t width;
	uint32_t height;
	int32_t x;
	int32_t y;
};

struct nvgpu_display_color_lut_entry {
	uint16_t red;
	uint16_t green;
	uint16_t blue;
};

struct nvgpu_display_color_config {
	const struct nvgpu_display_color_lut_entry *degamma;
	uint32_t degamma_count;
	const struct nvgpu_display_color_lut_entry *gamma;
	uint32_t gamma_count;
	bool ctm_enabled;
	uint64_t ctm[9];
};

struct nvgpu_display_console {
	uint64_t paddr;
	uint64_t size;
	uint64_t bar1_gva;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
};

struct nvgpu_display_event_ops {
	void (*vblank)(void *arg, uint32_t head);
	void (*hotplug)(void *arg, uint32_t plug_mask, uint32_t unplug_mask);
	void (*dp_irq)(void *arg, uint32_t display_id);
};

/* Initialize or release device-owned display policy around KMS registration. */
int nvgpu_display_init(struct nvgpu_device *gpu);
void nvgpu_display_fini(struct nvgpu_device *gpu);

/* Query topology cached by display initialization; returned data is copied out. */
uint32_t nvgpu_display_get_head_count(struct nvgpu_device *gpu);
uint32_t nvgpu_display_get_output_mask(struct nvgpu_device *gpu);
int nvgpu_display_get_output(struct nvgpu_device *gpu, uint32_t display_id,
	struct nvgpu_display_output_info *info);
int nvgpu_display_detect_output(struct nvgpu_device *gpu,
	uint32_t display_id);
int nvgpu_display_read_edid(struct nvgpu_device *gpu, uint32_t display_id,
	uint8_t *data, uint32_t *size);

/*
 * Prepare an output route without publishing a modeset.
 *
 * On success prepared receives an owned route.  enable consumes it on every
 * path; abort consumes it when the caller rolls back before enable.
 */
int nvgpu_display_prepare_output(struct nvgpu_device *gpu, uint32_t head,
	uint32_t display_id, const struct nvgpu_display_mode *mode,
	const struct nvgpu_display_output_config *config,
	struct nvgpu_display_prepared_output **prepared);
void nvgpu_display_abort_output(struct nvgpu_device *gpu,
	struct nvgpu_display_prepared_output *prepared);

/*
 * Enable a prepared route and initial scanout.
 *
 * prepared is consumed on every return path.  Configuration and scanout data
 * are borrowed for the call; KMS keeps the underlying BO pinned separately.
 */
int nvgpu_display_enable(struct nvgpu_device *gpu, uint32_t head,
	uint32_t window, const struct nvgpu_display_head_config *head_config,
	const struct nvgpu_display_scanout *scanout,
	struct nvgpu_display_prepared_output *prepared);
int nvgpu_display_disable(struct nvgpu_device *gpu, uint32_t head,
	uint32_t window);

/*
 * KMS owns the display token from begin through end.  While active, update
 * calls only emit state and record changed windows; flush submits all EVO
 * channels together and wait observes only the recorded window notifiers.
 */
int nvgpu_display_atomic_begin(struct nvgpu_device *gpu);
int nvgpu_display_atomic_flush(struct nvgpu_device *gpu);
int nvgpu_display_atomic_wait(struct nvgpu_device *gpu);
void nvgpu_display_atomic_end(struct nvgpu_device *gpu);

/*
 * Emit a primary-plane update.  Within an atomic section, flush and notifier
 * waiting are deferred to the KMS tail; otherwise this submits immediately.
 * Scanout storage is borrowed for the call.
 */
int nvgpu_display_update_primary(struct nvgpu_device *gpu, uint32_t head,
	uint32_t window, const struct nvgpu_display_scanout *scanout);
int nvgpu_display_update_head(struct nvgpu_device *gpu, uint32_t head,
	const struct nvgpu_display_head_config *config, bool update_view,
	bool update_dither);
int nvgpu_display_update_color(struct nvgpu_device *gpu, uint32_t head,
	uint32_t window, const struct nvgpu_display_color_config *config);
/* Check and retrain the active DP route for display_id in process context. */
int nvgpu_display_recover_dp_link(struct nvgpu_device *gpu,
	uint32_t display_id);
int nvgpu_display_disable_primary(struct nvgpu_device *gpu,
	uint32_t window);

/* Program, move, or disable the hardware cursor for one active head. */
int nvgpu_display_update_cursor(struct nvgpu_device *gpu, uint32_t head,
	const struct nvgpu_display_cursor *cursor, bool legacy_update);
void nvgpu_display_move_cursor(struct nvgpu_device *gpu, uint32_t head,
	int32_t x, int32_t y);
int nvgpu_display_disable_cursor(struct nvgpu_device *gpu, uint32_t head,
	bool legacy_update);
/* Ensure a persistent pitch-linear console scanout and return borrowed scalar metadata. */
int nvgpu_display_prepare_console(struct nvgpu_device *gpu, uint32_t width,
	uint32_t height, struct nvgpu_display_console *console);

/* Release persistent console scanout storage after KMS has stopped using it. */
void nvgpu_display_release_console(struct nvgpu_device *gpu);

/*
 * Install borrowed KMS callbacks and their argument until replaced or cleared.
 * Vblank handling runs in the interrupt worker's process context.
 */
void nvgpu_display_set_event_ops(struct nvgpu_device *gpu,
	const struct nvgpu_display_event_ops *ops, void *arg);
void nvgpu_display_enable_vblank(struct nvgpu_device *gpu, uint32_t head);
void nvgpu_display_disable_vblank(struct nvgpu_device *gpu, uint32_t head);
void nvgpu_display_handle_vblank(struct nvgpu_device *gpu, uint32_t head);

#endif /* _NVGPU_DISPLAY_H_ */
