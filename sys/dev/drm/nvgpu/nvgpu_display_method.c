/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Native Turing display method emitters derived from nouveau dispnv50.
 */

#include "nvgpu_display_method.h"
#include "nvgsp_disp.h"

#include "nvhw/drf.h"
#include "nvhw/class/clc37a.h"
#include "nvhw/class/clc37b.h"
#include "nvhw/class/clc37d.h"
#include "nvhw/class/clc37e.h"
#include "nvhw/class/clc57d.h"
#include "nvhw/class/clc57e.h"

#include <sys/errno.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>

#define NVGPU_DISPLAY_PUSH_POLL_COUNT 2000u
#define NVGPU_DISPLAY_PUSH_POLL_US 1000u
#define NVGPU_DISPLAY_SCALE_ONE 1024u

void
nvgpu_display_push_init(struct nvgpu_display_push *push,
    struct nvgsp_display_channel *channel)
{
	uint32_t dwords;

	bzero(push, sizeof(*push));
	push->channel = channel;
	push->ring = nvgsp_disp_channel_get_push(channel, &dwords);
	if (push->ring != NULL && dwords > 1)
		push->max = dwords - 1;
}

int
nvgpu_display_push_kick(struct nvgpu_display_push *push)
{
	if (push == NULL || push->ring == NULL || push->channel == NULL)
		return (ENODEV);
	if (push->cur == push->put)
		return (0);
	cpu_sfence();
	nvgsp_disp_channel_write_user(push->channel, 0, push->cur << 2);
	(void)nvgsp_disp_channel_read_user(push->channel, 0);
	push->put = push->cur;
	return (0);
}

int
nvgpu_display_push_reserve(struct nvgpu_display_push *push, uint32_t dwords)
{
	uint32_t free_dwords;
	uint32_t get;
	uint32_t poll;
	int error;

	if (push == NULL || push->ring == NULL || push->channel == NULL)
		return (ENODEV);
	if (dwords > push->max)
		return (EINVAL);
	if (push->cur + dwords >= push->max) {
		get = nvgsp_disp_channel_read_user(push->channel, 4) >> 2;
		if (get == 0) {
			if (push->put == 0 && push->cur != 0) {
				error = nvgpu_display_push_kick(push);
				if (error != 0)
					return (error);
			}
			for (poll = 0; poll < NVGPU_DISPLAY_PUSH_POLL_COUNT; poll++) {
				get = nvgsp_disp_channel_read_user(push->channel, 4) >> 2;
				if (get != 0)
					break;
				DELAY(NVGPU_DISPLAY_PUSH_POLL_US);
			}
			if (get == 0)
				return (ETIMEDOUT);
		}
		push->ring[push->cur] = NVDEF(NVC37B, DMA, OPCODE, JUMP);
		cpu_sfence();
		nvgsp_disp_channel_write_user(push->channel, 0, 0);
		(void)nvgsp_disp_channel_read_user(push->channel, 0);
		push->cur = 0;
		push->put = 0;
	}

	get = nvgsp_disp_channel_read_user(push->channel, 4) >> 2;
	if (get >= push->max)
		get = 0;
	if (get > push->cur)
		free_dwords = get - push->cur;
	else
		free_dwords = push->max - push->cur + get;
	free_dwords = free_dwords > 5 ? free_dwords - 5 : 0;
	for (poll = 0; free_dwords < dwords &&
	    poll < NVGPU_DISPLAY_PUSH_POLL_COUNT; poll++) {
		DELAY(NVGPU_DISPLAY_PUSH_POLL_US);
		get = nvgsp_disp_channel_read_user(push->channel, 4) >> 2;
		if (get >= push->max)
			get = 0;
		if (get > push->cur)
			free_dwords = get - push->cur;
		else
			free_dwords = push->max - push->cur + get;
		free_dwords = free_dwords > 5 ? free_dwords - 5 : 0;
	}
	if (free_dwords < dwords)
		return (ETIMEDOUT);
	return (0);
}

int
nvgpu_display_push_method(struct nvgpu_display_push *push, uint32_t method,
    const uint32_t *data, uint32_t count)
{
	uint32_t header;
	uint32_t index;
	int error;

	if ((method & ~DRF_SMASK(NVC37B_DMA_METHOD_OFFSET)) != 0 ||
	    (count & ~DRF_MASK(NVC37B_DMA_METHOD_COUNT)) != 0)
		return (EINVAL);
	error = nvgpu_display_push_reserve(push, count + 1);
	if (error != 0)
		return (error);
	header = NVDEF(NVC37B, DMA, OPCODE, METHOD) |
	    NVVAL(NVC37B, DMA, METHOD_COUNT, count) |
	    NVVAL(NVC37B, DMA, METHOD_OFFSET, method >> 2);
	push->ring[push->cur++] = header;
	for (index = 0; index < count; index++)
		push->ring[push->cur++] = data[index];
	return (0);
}

int
nvgpu_display_method_init_core(struct nvgpu_display_push *push,
    uint32_t sync_handle, uint32_t windows)
{
	uint32_t values[2];
	uint32_t window;
	int error;

	error = nvgpu_display_push_method(push, NVC57D_SET_CONTEXT_DMA_NOTIFIER,
	    &sync_handle, 1);
	if (error != 0)
		return (error);
	for (window = 0; window < windows; window++) {
		values[0] =
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_FORMAT_USAGE_BOUNDS,
			RGB_PACKED1BPP, TRUE) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_FORMAT_USAGE_BOUNDS,
			RGB_PACKED2BPP, TRUE) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_FORMAT_USAGE_BOUNDS,
			RGB_PACKED4BPP, TRUE) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_FORMAT_USAGE_BOUNDS,
			RGB_PACKED8BPP, TRUE);
		values[1] = 0;
		error = nvgpu_display_push_method(push,
		    NVC57D_WINDOW_SET_WINDOW_FORMAT_USAGE_BOUNDS(window), values, 2);
		if (error != 0)
			return (error);
		values[0] =
		    NVVAL(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			MAX_PIXELS_FETCHED_PER_LINE, 0x7fff) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS, ILUT_ALLOWED,
			TRUE) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			INPUT_SCALER_TAPS, TAPS_2) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			UPSCALING_ALLOWED, FALSE);
		error = nvgpu_display_push_method(push,
		    NVC57D_WINDOW_SET_WINDOW_USAGE_BOUNDS(window), values, 1);
		if (error != 0)
			return (error);
	}
	return (nvgpu_display_push_kick(push));
}

int
nvgpu_display_method_assign_windows(struct nvgpu_display_push *push,
    uint32_t windows)
{
	uint32_t value;
	uint32_t window;
	int error;

	for (window = 0; window < windows; window++) {
		value = NVDEF(NVC37D, WINDOW_SET_CONTROL, OWNER,
		    HEAD(window >> 1));
		error = nvgpu_display_push_method(push,
		    NVC37D_WINDOW_SET_CONTROL(window), &value, 1);
		if (error != 0)
			return (error);
	}
	return (0);
}

int
nvgpu_display_method_emit_core_update(struct nvgpu_display_push *push,
    const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT], bool notifier,
    uint32_t notifier_offset)
{
	uint32_t values[2];
	int error;

	if (notifier) {
		values[0] = NVDEF(NVC37D, SET_NOTIFIER_CONTROL, MODE, WRITE) |
		    NVVAL(NVC37D, SET_NOTIFIER_CONTROL, OFFSET,
			notifier_offset >> 4) |
		    NVDEF(NVC37D, SET_NOTIFIER_CONTROL, NOTIFY, ENABLE);
		error = nvgpu_display_push_method(push,
		    NVC37D_SET_NOTIFIER_CONTROL, values, 1);
		if (error != 0)
			return (error);
	}
	values[0] = interlock[NVGPU_DISPLAY_INTERLOCK_CURSOR];
	values[1] = interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW];
	error = nvgpu_display_push_method(push, NVC37D_SET_INTERLOCK_FLAGS,
	    values, 2);
	if (error != 0)
		return (error);
	values[0] = 1 |
	    NVDEF(NVC37D, UPDATE, SPECIAL_HANDLING, NONE) |
	    NVDEF(NVC37D, UPDATE, INHIBIT_INTERRUPTS, FALSE);
	error = nvgpu_display_push_method(push, NVC37D_UPDATE, values, 1);
	if (error != 0)
		return (error);
	if (notifier) {
		values[0] = NVDEF(NVC37D, SET_NOTIFIER_CONTROL, NOTIFY, DISABLE);
		error = nvgpu_display_push_method(push,
		    NVC37D_SET_NOTIFIER_CONTROL, values, 1);
		if (error != 0)
			return (error);
	}
	return (0);
}

int
nvgpu_display_method_update_core(struct nvgpu_display_push *push,
    const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT], bool notifier,
    uint32_t notifier_offset)
{
	int error;

	error = nvgpu_display_method_emit_core_update(push, interlock, notifier,
	    notifier_offset);
	if (error != 0)
		return (error);
	return (nvgpu_display_push_kick(push));
}

int
nvgpu_display_method_set_head_display_id(struct nvgpu_display_push *push,
    uint32_t head, uint32_t display_id)
{
	return (nvgpu_display_push_method(push, 0x2020 + head * 0x400,
	    &display_id, 1));
}

int
nvgpu_display_method_set_head_mode(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	const struct nvgpu_display_mode *mode = &state->mode;
	uint32_t values[4];
	uint32_t horizontal_active;
	uint32_t horizontal_sync_end;
	uint32_t horizontal_blank_end;
	uint32_t horizontal_blank_start;
	uint32_t vertical_active;
	uint32_t vertical_sync_end;
	uint32_t vertical_blank_end;
	uint32_t vertical_blank_start;
	uint32_t vertical_blank2_end;
	uint32_t vertical_blank2_start;
	uint32_t value;
	int error;

	horizontal_active = mode->htotal;
	horizontal_sync_end = mode->hsync_end - mode->hsync_start - 1u;
	horizontal_blank_end = mode->hblank_end - mode->hsync_start - 1u;
	horizontal_blank_start = horizontal_blank_end + mode->hdisplay;
	vertical_active = mode->vtotal;
	vertical_sync_end = mode->vsync_end - mode->vsync_start - 1u;
	vertical_blank_end = mode->vblank_end - mode->vsync_start - 1u;
	vertical_blank_start = vertical_blank_end + mode->vdisplay;
	if (mode->interlace) {
		vertical_blank2_end = vertical_active + vertical_blank_end;
		vertical_blank2_start = vertical_blank2_end + mode->vdisplay;
		vertical_active = vertical_active * 2u + 1u;
	} else {
		vertical_blank2_end = 0;
		vertical_blank2_start = 1;
	}
	values[0] = NVVAL(NVC57D, HEAD_SET_RASTER_SIZE, WIDTH,
	    horizontal_active) |
	    NVVAL(NVC57D, HEAD_SET_RASTER_SIZE, HEIGHT, vertical_active);
	values[1] = NVVAL(NVC57D, HEAD_SET_RASTER_SYNC_END, X,
	    horizontal_sync_end) |
	    NVVAL(NVC57D, HEAD_SET_RASTER_SYNC_END, Y,
		vertical_sync_end);
	values[2] = NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_END, X,
	    horizontal_blank_end) |
	    NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_END, Y,
		vertical_blank_end);
	values[3] = NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_START, X,
	    horizontal_blank_start) |
	    NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_START, Y,
		vertical_blank_start);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_RASTER_SIZE(head), values, 4);
	if (error != 0)
		return (error);
	value = vertical_blank2_end << 16 | vertical_blank2_start;
	error = nvgpu_display_push_method(push, 0x2074 + head * 0x400,
	    &value, 1);
	if (error != 0)
		return (error);
	value = mode->interlace;
	error = nvgpu_display_push_method(push, 0x2008 + head * 0x400,
	    &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY, HERTZ,
	    mode->clock_khz * 1000);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_PIXEL_CLOCK_FREQUENCY(head), &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX, HERTZ,
	    mode->clock_khz * 1000);
	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX(head), &value, 1));
}

int
nvgpu_display_method_set_head_view(struct nvgpu_display_push *push,
    uint32_t head, uint32_t windows,
    const struct nvgpu_display_head_state *state)
{
	uint64_t factor;
	uint32_t horizontal_factor = NVGPU_DISPLAY_SCALE_ONE;
	uint32_t vertical_factor = NVGPU_DISPLAY_SCALE_ONE;
	uint32_t max_pixels;
	uint32_t first;
	uint32_t end;
	uint32_t value;
	uint32_t window;
	int error;

	if (state->view.input_width > state->view.output_width &&
	    state->view.output_width != 0) {
		factor = ((uint64_t)state->view.input_width *
		    NVGPU_DISPLAY_SCALE_ONE + state->view.output_width - 1) /
		    state->view.output_width;
		horizontal_factor = MIN(factor, 0xffffu);
	}
	if (state->view.input_height > state->view.output_height &&
	    state->view.output_height != 0) {
		factor = ((uint64_t)state->view.input_height *
		    NVGPU_DISPLAY_SCALE_ONE + state->view.output_height - 1) /
		    state->view.output_height;
		vertical_factor = MIN(factor, 0xffffu);
	}
	max_pixels = (((uint32_t)state->view.input_width + 14u) *
	    NVGPU_DISPLAY_SCALE_ONE + NVGPU_DISPLAY_SCALE_ONE - 1) >> 10;
	max_pixels = MIN(max_pixels + 8u, 0x7fffu);
	value = NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_SCALER,
	    VERTICAL_TAPS, TAPS_2) |
	    NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_SCALER,
		HORIZONTAL_TAPS, TAPS_2);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_CONTROL_OUTPUT_SCALER(head), &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_VIEWPORT_SIZE_IN, WIDTH,
	    state->view.input_width) |
	    NVVAL(NVC57D, HEAD_SET_VIEWPORT_SIZE_IN, HEIGHT,
		state->view.input_height);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_VIEWPORT_SIZE_IN(head), &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_VIEWPORT_POINT_OUT_ADJUST, X, 0) |
	    NVVAL(NVC57D, HEAD_SET_VIEWPORT_POINT_OUT_ADJUST, Y, 0);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_VIEWPORT_POINT_OUT_ADJUST(head), &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_VIEWPORT_SIZE_OUT, WIDTH,
	    state->view.output_width) |
	    NVVAL(NVC57D, HEAD_SET_VIEWPORT_SIZE_OUT, HEIGHT,
		state->view.output_height);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_VIEWPORT_SIZE_OUT(head), &value, 1);
	if (error != 0)
		return (error);
	value = NVVAL(NVC57D, HEAD_SET_MAX_OUTPUT_SCALE_FACTOR, HORIZONTAL,
	    horizontal_factor) |
	    NVVAL(NVC57D, HEAD_SET_MAX_OUTPUT_SCALE_FACTOR, VERTICAL,
		vertical_factor);
	error = nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_MAX_OUTPUT_SCALE_FACTOR(head), &value, 1);
	if (error != 0)
		return (error);
	first = head * 2;
	end = MIN(first + 2, windows);
	for (window = first; window < end; window++) {
		value = NVVAL(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
		    MAX_PIXELS_FETCHED_PER_LINE, max_pixels) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			ILUT_ALLOWED, TRUE) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			INPUT_SCALER_TAPS, TAPS_2) |
		    NVDEF(NVC57D, WINDOW_SET_WINDOW_USAGE_BOUNDS,
			UPSCALING_ALLOWED, FALSE);
		error = nvgpu_display_push_method(push,
		    NVC57D_WINDOW_SET_WINDOW_USAGE_BOUNDS(window), &value, 1);
		if (error != 0)
			return (error);
	}
	value = NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, CURSOR,
	    USAGE_W256_H256) |
	    NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, OLUT_ALLOWED, TRUE) |
	    NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, OUTPUT_SCALER_TAPS,
		TAPS_2) |
	    NVVAL(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, UPSCALING_ALLOWED,
		state->view.output_height > state->view.input_height);
	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_HEAD_USAGE_BOUNDS(head), &value, 1));
}

int
nvgpu_display_method_set_head_output(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	uint32_t depth;
	uint32_t value;

	switch (state->output.depth) {
	case 6:
		depth = 5;
		break;
	case 5:
		depth = 4;
		break;
	case 2:
		depth = 1;
		break;
	case 0:
		depth = 4;
		break;
	default:
		depth = state->output.depth;
		break;
	}
	value = NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, CRC_MODE,
	    state->output.crc_raster) |
	    NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, HSYNC_POLARITY,
		state->output.negative_hsync) |
	    NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, VSYNC_POLARITY,
		state->output.negative_vsync) |
	    NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, PIXEL_DEPTH,
		depth) |
	    NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE,
		COLOR_SPACE_OVERRIDE, DISABLE) |
	    NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE,
		EXT_PACKET_WIN, NONE);
	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_CONTROL_OUTPUT_RESOURCE(head), &value, 1));
}

int
nvgpu_display_method_set_head_dither(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	uint32_t value;

	value = NVVAL(NVC37D, HEAD_SET_DITHER_CONTROL, ENABLE,
	    state->dither.enable) |
	    NVVAL(NVC37D, HEAD_SET_DITHER_CONTROL, BITS,
		state->dither.bits) |
	    NVDEF(NVC37D, HEAD_SET_DITHER_CONTROL, OFFSET_ENABLE, DISABLE) |
	    NVVAL(NVC37D, HEAD_SET_DITHER_CONTROL, MODE,
		state->dither.mode) |
	    NVVAL(NVC37D, HEAD_SET_DITHER_CONTROL, PHASE, 0);
	return (nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_DITHER_CONTROL(head), &value, 1));
}

int
nvgpu_display_method_set_head_procamp(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	uint32_t value;

	(void)state;
	value = NVDEF(NVC57D, HEAD_SET_PROCAMP, COLOR_SPACE, RGB) |
	    NVDEF(NVC57D, HEAD_SET_PROCAMP, CHROMA_LPF, DISABLE) |
	    NVDEF(NVC57D, HEAD_SET_PROCAMP, DYNAMIC_RANGE, VESA);
	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_PROCAMP(head), &value, 1));
}

int
nvgpu_display_method_set_head_output_lut(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	uint32_t values[4];

	values[0] = NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, INTERPOLATE,
	    state->output_lut.output_mode) |
	    NVDEF(NVC57D, HEAD_SET_OLUT_CONTROL, MIRROR, DISABLE) |
	    NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, MODE,
		state->output_lut.mode) |
	    NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, SIZE,
		state->output_lut.size);
	values[1] = 0xffffffff;
	values[2] = state->output_lut.handle;
	values[3] = state->output_lut.offset >> 8;
	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_OLUT_CONTROL(head), values, 4));
}

int
nvgpu_display_method_clear_head_output_lut(struct nvgpu_display_push *push,
    uint32_t head)
{
	uint32_t value = 0;

	return (nvgpu_display_push_method(push,
	    NVC57D_HEAD_SET_CONTEXT_DMA_OLUT(head), &value, 1));
}

int
nvgpu_display_method_set_head_cursor(struct nvgpu_display_push *push,
    uint32_t head, const struct nvgpu_display_head_state *state)
{
	uint32_t values[2];
	int error;

	values[0] = NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR, ENABLE, ENABLE) |
	    NVVAL(NVC37D, HEAD_SET_CONTROL_CURSOR, FORMAT,
		state->cursor.format) |
	    NVVAL(NVC37D, HEAD_SET_CONTROL_CURSOR, SIZE,
		state->cursor.layout) |
	    NVVAL(NVC37D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_X, 0) |
	    NVVAL(NVC37D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_Y, 0) |
	    NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR, DE_GAMMA, NONE);
	values[1] = NVVAL(NVC37D, HEAD_SET_CONTROL_CURSOR_COMPOSITION, K1,
	    0xff) |
	    NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR_COMPOSITION,
		CURSOR_COLOR_FACTOR_SELECT, K1) |
	    NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR_COMPOSITION,
		VIEWPORT_COLOR_FACTOR_SELECT, NEG_K1_TIMES_SRC) |
	    NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR_COMPOSITION, MODE, BLEND);
	error = nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_CONTROL_CURSOR(head), values, 2);
	if (error != 0)
		return (error);
	values[0] = state->cursor.handle;
	error = nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_CONTEXT_DMA_CURSOR(head, 0), values, 1);
	if (error != 0)
		return (error);
	values[0] = state->cursor.offset >> 8;
	return (nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_OFFSET_CURSOR(head, 0), values, 1));
}

int
nvgpu_display_method_clear_head_cursor(struct nvgpu_display_push *push,
    uint32_t head)
{
	uint32_t value;
	int error;

	value = NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR, ENABLE, DISABLE) |
	    NVDEF(NVC37D, HEAD_SET_CONTROL_CURSOR, FORMAT, A8R8G8B8);
	error = nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_CONTROL_CURSOR(head), &value, 1);
	if (error != 0)
		return (error);
	value = 0;
	return (nvgpu_display_push_method(push,
	    NVC37D_HEAD_SET_CONTEXT_DMA_CURSOR(head, 0), &value, 1));
}

uint32_t
nvgpu_display_method_head_window_mask(uint32_t head, uint32_t windows)
{
	uint32_t first = head * 2;
	uint32_t end = MIN(first + 2, windows);
	uint32_t mask = 0;
	uint32_t window;

	for (window = first; window < end; window++)
		mask |= 1u << window;
	return (mask);
}

int
nvgpu_display_method_set_window_image(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	uint32_t values[4];
	int error;

	values[0] = NVVAL(NVC57E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL,
	    state->image.interval) |
	    NVVAL(NVC57E, SET_PRESENT_CONTROL, BEGIN_MODE,
		state->image.mode) |
	    NVDEF(NVC57E, SET_PRESENT_CONTROL, TIMESTAMP_MODE, DISABLE);
	error = nvgpu_display_push_method(push, NVC57E_SET_PRESENT_CONTROL,
	    values, 1);
	if (error != 0)
		return (error);
	values[0] = NVVAL(NVC57E, SET_SIZE, WIDTH, state->image.width) |
	    NVVAL(NVC57E, SET_SIZE, HEIGHT, state->image.height);
	values[1] = NVVAL(NVC57E, SET_STORAGE, BLOCK_HEIGHT,
	    state->image.block_height) |
	    NVVAL(NVC57E, SET_STORAGE, MEMORY_LAYOUT, state->image.layout);
	values[2] = NVVAL(NVC57E, SET_PARAMS, FORMAT, state->image.format) |
	    NVDEF(NVC57E, SET_PARAMS, CLAMP_BEFORE_BLEND, DISABLE) |
	    NVDEF(NVC57E, SET_PARAMS, SWAP_UV, DISABLE) |
	    NVDEF(NVC57E, SET_PARAMS, FMT_ROUNDING_MODE, ROUND_TO_NEAREST);
	values[3] = NVVAL(NVC57E, SET_PLANAR_STORAGE, PITCH,
	    state->image.block) |
	    NVVAL(NVC57E, SET_PLANAR_STORAGE, PITCH,
		state->image.pitch >> 6);
	error = nvgpu_display_push_method(push, NVC57E_SET_SIZE, values, 4);
	if (error != 0)
		return (error);
	values[0] = state->image.handle;
	error = nvgpu_display_push_method(push, NVC57E_SET_CONTEXT_DMA_ISO(0),
	    values, 1);
	if (error != 0)
		return (error);
	values[0] = state->image.offset >> 8;
	error = nvgpu_display_push_method(push, NVC57E_SET_OFFSET(0), values, 1);
	if (error != 0)
		return (error);
	values[0] = NVVAL(NVC57E, SET_POINT_IN, X, state->image.source_x) |
	    NVVAL(NVC57E, SET_POINT_IN, Y, state->image.source_y);
	error = nvgpu_display_push_method(push, NVC57E_SET_POINT_IN(0), values,
	    1);
	if (error != 0)
		return (error);
	values[0] = NVVAL(NVC57E, SET_SIZE_IN, WIDTH,
	    state->image.source_width) |
	    NVVAL(NVC57E, SET_SIZE_IN, HEIGHT, state->image.source_height);
	error = nvgpu_display_push_method(push, NVC57E_SET_SIZE_IN, values, 1);
	if (error != 0)
		return (error);
	values[0] = NVVAL(NVC57E, SET_SIZE_OUT, WIDTH,
	    state->image.output_width) |
	    NVVAL(NVC57E, SET_SIZE_OUT, HEIGHT, state->image.output_height);
	return (nvgpu_display_push_method(push, NVC57E_SET_SIZE_OUT, values, 1));
}

int
nvgpu_display_method_clear_window_image(struct nvgpu_display_push *push)
{
	uint32_t value;
	int error;

	value = NVVAL(NVC37E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, 0) |
	    NVDEF(NVC37E, SET_PRESENT_CONTROL, BEGIN_MODE, NON_TEARING);
	error = nvgpu_display_push_method(push, NVC37E_SET_PRESENT_CONTROL,
	    &value, 1);
	if (error != 0)
		return (error);
	value = 0;
	return (nvgpu_display_push_method(push, NVC37E_SET_CONTEXT_DMA_ISO(0),
	    &value, 1));
}

int
nvgpu_display_method_set_window_blend(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	uint32_t values[7];

	values[0] = NVDEF(NVC37E, SET_COMPOSITION_CONTROL, COLOR_KEY_SELECT,
	    DISABLE) |
	    NVVAL(NVC37E, SET_COMPOSITION_CONTROL, DEPTH, state->blend.depth);
	values[1] = NVVAL(NVC37E, SET_COMPOSITION_CONSTANT_ALPHA, K1,
	    state->blend.k1) |
	    NVVAL(NVC37E, SET_COMPOSITION_CONSTANT_ALPHA, K2, 0);
	values[2] = NVVAL(NVC37E, SET_COMPOSITION_FACTOR_SELECT,
	    SRC_COLOR_FACTOR_MATCH_SELECT, state->blend.source_color) |
	    NVVAL(NVC37E, SET_COMPOSITION_FACTOR_SELECT,
		SRC_COLOR_FACTOR_NO_MATCH_SELECT, state->blend.source_color) |
	    NVVAL(NVC37E, SET_COMPOSITION_FACTOR_SELECT,
		DST_COLOR_FACTOR_MATCH_SELECT, state->blend.destination_color) |
	    NVVAL(NVC37E, SET_COMPOSITION_FACTOR_SELECT,
		DST_COLOR_FACTOR_NO_MATCH_SELECT, state->blend.destination_color);
	values[3] = NVVAL(NVC37E, SET_KEY_ALPHA, MIN, 0) |
	    NVVAL(NVC37E, SET_KEY_ALPHA, MAX, 0xffff);
	values[4] = NVVAL(NVC37E, SET_KEY_RED_CR, MIN, 0) |
	    NVVAL(NVC37E, SET_KEY_RED_CR, MAX, 0xffff);
	values[5] = NVVAL(NVC37E, SET_KEY_GREEN_Y, MIN, 0) |
	    NVVAL(NVC37E, SET_KEY_GREEN_Y, MAX, 0xffff);
	values[6] = NVVAL(NVC37E, SET_KEY_BLUE_CB, MIN, 0) |
	    NVVAL(NVC37E, SET_KEY_BLUE_CB, MAX, 0xffff);
	return (nvgpu_display_push_method(push,
	    NVC37E_SET_COMPOSITION_CONTROL, values, 7));
}

int
nvgpu_display_method_set_window_csc(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	return (nvgpu_display_push_method(push, NVC57E_SET_FMT_COEFFICIENT_C00,
	    state->csc.matrix, 12));
}

int
nvgpu_display_method_clear_window_csc(struct nvgpu_display_push *push)
{
	const uint32_t identity[12] = {
		0x00010000, 0, 0, 0,
		0, 0x00010000, 0, 0,
		0, 0, 0x00010000, 0,
	};

	return (nvgpu_display_push_method(push, NVC57E_SET_FMT_COEFFICIENT_C00,
	    identity, 12));
}

int
nvgpu_display_method_set_window_input_lut(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	uint32_t values[3];

	values[0] = NVVAL(NVC57E, SET_ILUT_CONTROL, SIZE,
	    state->input_lut.size) |
	    NVVAL(NVC57E, SET_ILUT_CONTROL, MODE, state->input_lut.mode) |
	    NVVAL(NVC57E, SET_ILUT_CONTROL, INTERPOLATE,
		state->input_lut.output_mode);
	values[1] = state->input_lut.handle;
	values[2] = state->input_lut.offset >> 8;
	return (nvgpu_display_push_method(push, NVC57E_SET_ILUT_CONTROL,
	    values, 3));
}

int
nvgpu_display_method_clear_window_input_lut(
    struct nvgpu_display_push *push)
{
	uint32_t value = 0;

	return (nvgpu_display_push_method(push, NVC57E_SET_CONTEXT_DMA_ILUT,
	    &value, 1));
}

int
nvgpu_display_method_set_window_notifier(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	uint32_t values[2];

	values[0] = state->notifier.handle;
	values[1] = NVVAL(NVC37E, SET_NOTIFIER_CONTROL, MODE,
	    state->notifier.mode) |
	    NVVAL(NVC37E, SET_NOTIFIER_CONTROL, OFFSET,
		state->notifier.offset >> 4);
	return (nvgpu_display_push_method(push, NVC37E_SET_CONTEXT_DMA_NOTIFIER,
	    values, 2));
}

int
nvgpu_display_method_clear_window_notifier(struct nvgpu_display_push *push)
{
	uint32_t value = 0;

	return (nvgpu_display_push_method(push, NVC37E_SET_CONTEXT_DMA_NOTIFIER,
	    &value, 1));
}

int
nvgpu_display_method_set_window_semaphore(struct nvgpu_display_push *push,
    const struct nvgpu_display_window_state *state)
{
	uint32_t values[4];

	values[0] = state->semaphore.offset;
	values[1] = state->semaphore.acquire;
	values[2] = state->semaphore.release;
	values[3] = state->semaphore.handle;
	return (nvgpu_display_push_method(push, NVC37E_SET_SEMAPHORE_CONTROL,
	    values, 4));
}

int
nvgpu_display_method_clear_window_semaphore(struct nvgpu_display_push *push)
{
	uint32_t value = 0;

	return (nvgpu_display_push_method(push,
	    NVC37E_SET_CONTEXT_DMA_SEMAPHORE, &value, 1));
}

int
nvgpu_display_method_emit_window_update(struct nvgpu_display_push *push,
    uint32_t window,
    const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT])
{
	uint32_t values[2];
	uint32_t value;
	int error;

	values[0] = interlock[NVGPU_DISPLAY_INTERLOCK_CURSOR] << 1 |
	    interlock[NVGPU_DISPLAY_INTERLOCK_CORE];
	values[1] = interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW];
	error = nvgpu_display_push_method(push, NVC37E_SET_INTERLOCK_FLAGS,
	    values, 2);
	if (error != 0)
		return (error);
	value = 1 | NVVAL(NVC37E, UPDATE, INTERLOCK_WITH_WIN_IMM,
	    !!(interlock[NVGPU_DISPLAY_INTERLOCK_IMMEDIATE] & (1u << window)));
	error = nvgpu_display_push_method(push, NVC37E_UPDATE, &value, 1);
	if (error != 0)
		return (error);
	return (0);
}

int
nvgpu_display_method_update_window(struct nvgpu_display_push *push,
    uint32_t window,
    const uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT])
{
	int error;

	error = nvgpu_display_method_emit_window_update(push, window, interlock);
	if (error != 0)
		return (error);
	return (nvgpu_display_push_kick(push));
}

void
nvgpu_display_method_set_cursor_point(struct nvgsp_display_channel *channel,
    int32_t x, int32_t y)
{
	uint32_t value;

	value = NVVAL(NVC37A, SET_CURSOR_HOT_SPOT_POINT_OUT, X, x) |
	    NVVAL(NVC37A, SET_CURSOR_HOT_SPOT_POINT_OUT, Y, y);
	nvgsp_disp_channel_write_user(channel,
	    NVC37A_SET_CURSOR_HOT_SPOT_POINT_OUT(0), value);
}

void
nvgpu_display_method_update_cursor(struct nvgsp_display_channel *channel)
{
	nvgsp_disp_channel_write_user(channel, NVC37A_UPDATE, 1);
}

int
nvgpu_display_method_set_sor_control(struct nvgpu_display_push *push,
    uint32_t sor, uint32_t control)
{
	return (nvgpu_display_push_method(push, NVC37D_SOR_SET_CONTROL(sor),
	    &control, 1));
}

uint8_t
nvgpu_display_method_get_format(enum nvgpu_display_format format)
{
	switch (format) {
	case NVGPU_DISPLAY_FORMAT_XRGB8888:
		return (NVC57E_SET_PARAMS_FORMAT_X8R8G8B8);
	case NVGPU_DISPLAY_FORMAT_ARGB8888:
		return (NVC57E_SET_PARAMS_FORMAT_A8R8G8B8);
	case NVGPU_DISPLAY_FORMAT_XBGR8888:
		return (NVC57E_SET_PARAMS_FORMAT_X8B8G8R8);
	case NVGPU_DISPLAY_FORMAT_ABGR8888:
		return (NVC57E_SET_PARAMS_FORMAT_A8B8G8R8);
	case NVGPU_DISPLAY_FORMAT_RGB565:
		return (NVC57E_SET_PARAMS_FORMAT_R5G6B5);
	case NVGPU_DISPLAY_FORMAT_ARGB1555:
		return (NVC57E_SET_PARAMS_FORMAT_A1R5G5B5);
	case NVGPU_DISPLAY_FORMAT_ARGB2101010:
		return (NVC57E_SET_PARAMS_FORMAT_A2R10G10B10);
	case NVGPU_DISPLAY_FORMAT_ABGR2101010:
		return (NVC57E_SET_PARAMS_FORMAT_A2B10G10R10);
	default:
		return (0);
	}
}

int
nvgpu_display_method_route_sor(struct nvgpu_display_push *push, uint32_t sor,
    enum nvgpu_display_protocol protocol, uint32_t link, uint32_t head)
{
	uint32_t value;

	switch (protocol) {
	case NVGPU_DISPLAY_PROTOCOL_TMDS:
		value = link & 1 ?
		    NVC37D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_A :
		    NVC37D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_B;
		break;
	case NVGPU_DISPLAY_PROTOCOL_DP:
		value = link & 1 ? NVC37D_SOR_SET_CONTROL_PROTOCOL_DP_A :
		    NVC37D_SOR_SET_CONTROL_PROTOCOL_DP_B;
		break;
	default:
		return (EINVAL);
	}
	value = NVVAL(NVC37D, SOR_SET_CONTROL, PROTOCOL, value) | (1u << head);
	return (nvgpu_display_method_set_sor_control(push, sor, value));
}
