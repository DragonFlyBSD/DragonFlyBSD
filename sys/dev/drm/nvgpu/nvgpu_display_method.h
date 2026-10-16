/*-
 * SPDX-License-Identifier: MIT
 *
 * Native Turing display method interface.
 */

#ifndef _NVGPU_DISPLAY_METHOD_H_
#define _NVGPU_DISPLAY_METHOD_H_

#include "nvgpu_display.h"

#include <sys/types.h>

struct nvgsp_display_channel;

#define NVGPU_DISPLAY_INTERLOCK_COUNT 6u
#define NVGPU_DISPLAY_INTERLOCK_CORE 0u
#define NVGPU_DISPLAY_INTERLOCK_CURSOR 1u
#define NVGPU_DISPLAY_INTERLOCK_WINDOW 4u
#define NVGPU_DISPLAY_INTERLOCK_IMMEDIATE 5u

struct nvgpu_display_push {
	struct nvgsp_display_channel *channel;
	uint32_t *ring;
	uint32_t cur;
	uint32_t put;
	uint32_t max;
};

struct nvgpu_display_head_state {
	struct {
		uint16_t input_width;
		uint16_t input_height;
		uint16_t output_width;
		uint16_t output_height;
	} view;
	struct nvgpu_display_mode mode;
	struct {
		uint32_t handle;
		uint64_t offset;
		uint8_t mode;
		uint16_t size;
		uint8_t range;
		uint8_t output_mode;
	} output_lut;
	struct {
		uint32_t handle;
		uint64_t offset;
		uint8_t format;
		uint8_t layout;
	} cursor;
	struct {
		uint8_t depth;
		uint8_t crc_raster;
		uint8_t negative_hsync;
		uint8_t negative_vsync;
	} output;
	struct {
		bool enable;
		uint8_t bits;
		uint8_t mode;
	} dither;
	struct {
		uint16_t cosine;
		uint16_t sine;
	} saturation;
};

struct nvgpu_display_window_state {
	struct {
		uint32_t interval;
		uint32_t mode;
		uint16_t width;
		uint16_t height;
		uint8_t block_height;
		uint8_t layout;
		uint8_t format;
		uint32_t block;
		uint32_t pitch;
		uint32_t handle;
		uint64_t offset;
		uint32_t source_x;
		uint32_t source_y;
		uint32_t source_width;
		uint32_t source_height;
		uint32_t output_width;
		uint32_t output_height;
	} image;
	struct {
		uint32_t matrix[12];
		bool valid;
	} csc;
	struct {
		uint32_t handle;
		uint64_t offset;
		uint16_t size;
		uint8_t mode;
		uint8_t output_mode;
	} input_lut;
	struct {
		uint8_t depth;
		uint16_t k1;
		uint8_t source_color;
		uint8_t destination_color;
	} blend;
	struct {
		uint32_t handle;
		uint32_t mode;
		uint64_t offset;
	} notifier;
	struct {
		uint32_t handle;
		uint32_t offset;
		uint32_t acquire;
		uint32_t release;
	} semaphore;
};

void nvgpu_display_push_init(struct nvgpu_display_push *push,
	struct nvgsp_display_channel *channel);
int nvgpu_display_push_reserve(struct nvgpu_display_push *push,
	uint32_t dwords);
int nvgpu_display_push_method(struct nvgpu_display_push *push,
	uint32_t method, const uint32_t *data, uint32_t count);
int nvgpu_display_push_kick(struct nvgpu_display_push *push);

int nvgpu_display_method_init_core(struct nvgpu_display_push *push,
	uint32_t sync_handle, uint32_t windows);
int nvgpu_display_method_assign_windows(struct nvgpu_display_push *push,
	uint32_t windows);
int nvgpu_display_method_emit_core_update(struct nvgpu_display_push *push,
	const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT], bool notifier,
	uint32_t notifier_offset);
int nvgpu_display_method_update_core(struct nvgpu_display_push *push,
	const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT], bool notifier,
	uint32_t notifier_offset);

int nvgpu_display_method_set_head_display_id(struct nvgpu_display_push *push,
	uint32_t head, uint32_t display_id);
int nvgpu_display_method_set_head_mode(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_set_head_view(struct nvgpu_display_push *push,
	uint32_t head, uint32_t windows,
	const struct nvgpu_display_head_state *state);
int nvgpu_display_method_set_head_output(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_set_head_dither(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_set_head_procamp(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_set_head_output_lut(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_clear_head_output_lut(
	struct nvgpu_display_push *push, uint32_t head);
int nvgpu_display_method_set_head_cursor(struct nvgpu_display_push *push,
	uint32_t head, const struct nvgpu_display_head_state *state);
int nvgpu_display_method_clear_head_cursor(struct nvgpu_display_push *push,
	uint32_t head);
uint32_t nvgpu_display_method_head_window_mask(uint32_t head,
	uint32_t windows);

int nvgpu_display_method_set_window_image(struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_clear_window_image(struct nvgpu_display_push *push);
int nvgpu_display_method_set_window_blend(struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_set_window_csc(struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_clear_window_csc(struct nvgpu_display_push *push);
int nvgpu_display_method_set_window_input_lut(
	struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_clear_window_input_lut(
	struct nvgpu_display_push *push);
int nvgpu_display_method_set_window_notifier(struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_clear_window_notifier(
	struct nvgpu_display_push *push);
int nvgpu_display_method_set_window_semaphore(
	struct nvgpu_display_push *push,
	const struct nvgpu_display_window_state *state);
int nvgpu_display_method_clear_window_semaphore(
	struct nvgpu_display_push *push);
int nvgpu_display_method_emit_window_update(struct nvgpu_display_push *push,
	uint32_t window,
	const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT]);
int nvgpu_display_method_update_window(struct nvgpu_display_push *push,
	uint32_t window,
	const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT]);

void nvgpu_display_method_set_cursor_point(
	struct nvgsp_display_channel *channel, int32_t x, int32_t y);
void nvgpu_display_method_update_cursor(
	struct nvgsp_display_channel *channel);
int nvgpu_display_method_set_sor_control(struct nvgpu_display_push *push,
	uint32_t sor, uint32_t control);
uint8_t nvgpu_display_method_get_format(enum nvgpu_display_format format);
int nvgpu_display_method_route_sor(struct nvgpu_display_push *push,
	uint32_t sor, enum nvgpu_display_protocol protocol, uint32_t link,
	uint32_t head);

#endif /* _NVGPU_DISPLAY_METHOD_H_ */
