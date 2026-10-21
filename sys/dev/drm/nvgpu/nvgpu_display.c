/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Display policy and EVO channel owner for the native NVIDIA GPU driver.
 */

#include "nvgpu_display.h"
#include "nvgpu_chip.h"
#include "nvgpu_device.h"
#include "nvgpu_debug.h"
#include "nvgpu_display_method.h"
#include "nvgsp_disp.h"
#include "nvgsp_vram.h"

#include <linux/math64.h>
#include <machine/cpufunc.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/systimer.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/time.h>

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, display_window_stage, 28,
    "display window stage win=%u ntfy=0x%x atomic=%u cur=%u put=%u get=%u",
    uint32_t window, uint32_t notifier, uint32_t atomic, uint32_t cur,
    uint32_t put, uint32_t get);
KTR_INFO(KTR_NVGPU, nvgpu, display_window_kick, 29,
    "display window kick win=%u ntfy=0x%x cur=%u put=%u get=%u",
    uint32_t window, uint32_t notifier, uint32_t cur, uint32_t put,
    uint32_t get);
KTR_INFO(KTR_NVGPU, nvgpu, display_window_armed, 30,
    "display window armed win=%u ntfy=0x%x polls=%u status=0x%x cur=%u put=%u get=%u",
    uint32_t window, uint32_t notifier, uint32_t polls, uint32_t status,
    uint32_t cur, uint32_t put, uint32_t get);

#define NVGPU_DISPLAY_MAX_WINDOWS 8u
#define NVGPU_DISPLAY_MAX_CURSORS 4u
#define NVGPU_DISPLAY_HANDLE_SYNC 0xf0000000u
#define NVGPU_DISPLAY_HANDLE_VRAM 0xf0000001u
#define NVGPU_DISPLAY_HANDLE_WINDOW(kind) (0xfb000000u | (kind))
#define NVGPU_DISPLAY_KIND_PITCH 0u
#define NVGPU_DISPLAY_KIND_BLOCK_LINEAR 0x06u
#define NVGPU_DISPLAY_DMA_VRAM 0x00000001u
#define NVGPU_DISPLAY_DMA_RW 0x00000004u
#define NVGPU_DISPLAY_DMA_LARGE_PAGE 0x00000040u
#define NVGPU_DISPLAY_DMA_BLOCK_LINEAR 0x00100000u
#define NVGPU_DISPLAY_LUT_ENTRIES 1024u
#define NVGPU_DISPLAY_LUT_PREFIX_ENTRIES 4u
#define NVGPU_DISPLAY_LUT_TOTAL_ENTRIES 1029u
#define NVGPU_DISPLAY_LUT_SIZE round_page(NVGPU_DISPLAY_LUT_TOTAL_ENTRIES * 8u)
#define NVGPU_DISPLAY_INPUT_LUT_OFFSET(window) \
	((uint64_t)(window) * NVGPU_DISPLAY_LUT_SIZE)
#define NVGPU_DISPLAY_OUTPUT_LUT_OFFSET(head) \
	((uint64_t)(head) * NVGPU_DISPLAY_LUT_SIZE)
#define NVGPU_DISPLAY_CORE_NOTIFIER 0x00u
#define NVGPU_DISPLAY_WINDOW_NOTIFIER(window) (0x40u * (1u + (window)) + 0x20u)
#define NVGPU_DISPLAY_NOTIFIER_STATUS_MASK 0xc0000000u
#define NVGPU_DISPLAY_NOTIFIER_NOT_BEGUN 0x00000000u
#define NVGPU_DISPLAY_NOTIFIER_BEGUN 0x40000000u
#define NVGPU_DISPLAY_NOTIFIER_FINISHED 0x80000000u
#define NVGPU_DISPLAY_NOTIFIER_POLLS 2000u
#define NVGPU_DISPLAY_NOTIFIER_DELAY_US 1000u
#define NVGPU_DISPLAY_WINDOW_NOTIFIER_POLLS 2000000u
#define NVGPU_DISPLAY_WINDOW_NOTIFIER_DELAY_US 1u
#define NVGPU_DISPLAY_ATOMIC_NOTIFIER_SLEEP_US 2u
#define NVGPU_DISPLAY_ATOMIC_NOTIFIER_TIMEOUT_SEC 2u
#define NVGPU_DISPLAY_DP_AUX_NATIVE_READ 0x09u
#define NVGPU_DISPLAY_DP_AUX_REPLY_ACK 0x00u
#define NVGPU_DISPLAY_DP_MAX_LINK_RATE 0x01u
#define NVGPU_DISPLAY_DP_MAX_LANE_COUNT 0x02u
#define NVGPU_DISPLAY_DP_DOWNSTREAM_PRESENT 0x05u
#define NVGPU_DISPLAY_DP_DOWNSTREAM_PORT 0x80u
#define NVGPU_DISPLAY_DP_DOWNSTREAM_ATTACHED 0x01u
#define NVGPU_DISPLAY_DP_DOWNSTREAM_DETAILED 0x10u
#define NVGPU_DISPLAY_DP_PORT_TYPE_MASK 0x07u
#define NVGPU_DISPLAY_DP_PORT_TYPE_VGA 0x01u
#define NVGPU_DISPLAY_DP_PORT_TYPE_DVI 0x02u
#define NVGPU_DISPLAY_DP_PORT_TYPE_HDMI 0x03u
#define NVGPU_DISPLAY_DP_PORT_TYPE_DUAL_MODE 0x05u
#define NVGPU_DISPLAY_DP_LANE_COUNT_MASK 0x1fu
#define NVGPU_DISPLAY_DP_ENHANCED_FRAME 0x80u
#define NVGPU_DISPLAY_DP_TU_SIZE 64u
#define NVGPU_DISPLAY_DP_PRECISION 100000u
#define NVGPU_DISPLAY_DP_WM_ADJUST 2u
#define NVGPU_DISPLAY_DP_WM_LIMIT 20u
#define NVGPU_DISPLAY_DP_WM_ADJUST_INC 8u
#define NVGPU_DISPLAY_DP_WM_LIMIT_INC 22u
#define NVGPU_DISPLAY_METHOD_LAYOUT_BLOCK_LINEAR 0u
#define NVGPU_DISPLAY_METHOD_LAYOUT_PITCH 1u
#define NVGPU_DISPLAY_METHOD_PRESENT_NON_TEARING 0u
#define NVGPU_DISPLAY_METHOD_LUT_DIRECT10 2u
#define NVGPU_DISPLAY_METHOD_LUT_DIRECT8 1u
#define NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_DISABLE 0u
#define NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_ENABLE 1u
#define NVGPU_DISPLAY_METHOD_BLEND_K1 2u
#define NVGPU_DISPLAY_METHOD_BLEND_NEG_K1 4u
#define NVGPU_DISPLAY_METHOD_CURSOR_A8R8G8B8 0xcfu
#define NVGPU_DISPLAY_METHOD_CURSOR_32 0u
#define NVGPU_DISPLAY_METHOD_CURSOR_64 1u
#define NVGPU_DISPLAY_METHOD_CURSOR_128 2u
#define NVGPU_DISPLAY_METHOD_CURSOR_256 3u

struct nvgpu_display_channel {
	struct nvgsp_display_channel *backend;
	struct nvgpu_display_push push;
	int sync_cookie;
	int vram_cookie;
	int pitch_cookie;
	int block_cookie;
};

struct nvgpu_display_head {
	struct nvgsp_display_route route;
	uint32_t display_id;
	bool active;
	bool cursor_active;
	uint8_t dp_lane_count;
	uint8_t dp_link_bandwidth;
	bool dp_mst;
};

struct nvgpu_display_window {
	uint32_t next_notifier_offset;
	bool active;
	bool color_active;
};

struct nvgpu_display_atomic {
	uint32_t window_mask;
	uint32_t window_notifier_mask;
	uint32_t window_notifier[NVGPU_DISPLAY_MAX_WINDOWS];
	uint32_t audio_enable_mask;
	uint8_t audio_eld_size[NVGPU_DISPLAY_MAX_CURSORS];
	uint8_t audio_eld[NVGPU_DISPLAY_MAX_CURSORS][NVGPU_DISPLAY_ELD_SIZE];
	uint32_t cursor_mask;
	int32_t cursor_x[NVGPU_DISPLAY_MAX_CURSORS];
	int32_t cursor_y[NVGPU_DISPLAY_MAX_CURSORS];
	bool active;
	bool core_changed;
};

struct nvgpu_display_prepared_output {
	struct nvgsp_display_route route;
	struct nvgsp_display_dp_stream dp_stream;
	struct nvgsp_display_hdmi_sink hdmi_sink;
	struct nvgpu_display_output_config config;
	uint32_t head;
	bool valid;
};

struct nvgpu_display {
	struct nvgpu_device *gpu;
	struct nvgsp_vram_alloc *sync;
	struct nvgsp_vram_alloc *input_lut;
	struct nvgsp_vram_alloc *output_lut;
	struct nvgsp_vram_alloc *console;
	uint64_t console_bar1_gva;
	uint64_t console_size;
	uint32_t console_width;
	uint32_t console_height;
	uint32_t console_pitch;
	struct nvgpu_display_channel core;
	struct nvgpu_display_channel windows[NVGPU_DISPLAY_MAX_WINDOWS];
	struct nvgpu_display_channel cursors[NVGPU_DISPLAY_MAX_CURSORS];
	struct nvgpu_display_head heads[NVGPU_DISPLAY_MAX_CURSORS];
	struct nvgpu_display_window window_state[NVGPU_DISPLAY_MAX_WINDOWS];
	struct nvgpu_display_atomic atomic;
	struct lwkt_token token;
	struct spinlock event_lock;
	struct nvgpu_display_event_ops event_ops;
	void *event_arg;
	uint32_t window_count;
	uint32_t cursor_count;
	uint32_t head_count;
	bool core_initialized;
	bool assign_windows;
};

int
nvgpu_display_atomic_begin(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display == NULL)
		return (ENODEV);
	lwkt_gettoken(&display->token);
	if (display->atomic.active) {
		lwkt_reltoken(&display->token);
		return (EBUSY);
	}
	bzero(&display->atomic, sizeof(display->atomic));
	display->atomic.active = true;
	return (0);
}

int
nvgpu_display_atomic_flush(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t window;
	uint32_t head;
	uint32_t poll;
	int error;

	if (display == NULL || !display->atomic.active)
		return (EINVAL);
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] =
	    display->atomic.core_changed ? 1 : 0;
	interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW] = display->atomic.window_mask;
	for (head = 0; head < display->cursor_count; head++) {
		if ((display->atomic.cursor_mask & (1u << head)) == 0)
			continue;
		nvgpu_display_method_set_cursor_point(display->cursors[head].backend,
		    display->atomic.cursor_x[head], display->atomic.cursor_y[head]);
		nvgpu_display_method_update_cursor(display->cursors[head].backend);
	}
	for (window = 0; window < display->window_count; window++) {
		if ((display->atomic.window_mask & (1u << window)) == 0)
			continue;
		error = nvgpu_display_method_emit_window_update(
		    &display->windows[window].push, window, interlock);
		if (error != 0)
			return (error);
	}
	if (display->atomic.core_changed) {
		for (poll = 0; poll < 4; poll++)
			nvgsp_vram_alloc_write32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER + poll * 4u, 0);
		error = nvgpu_display_method_emit_core_update(&display->core.push,
		    interlock, true, NVGPU_DISPLAY_CORE_NOTIFIER);
		if (error != 0)
			return (error);
	}
	for (window = 0; window < display->window_count; window++) {
		if ((display->atomic.window_mask & (1u << window)) == 0)
			continue;
		error = nvgpu_display_push_kick(&display->windows[window].push);
		if (error != 0)
			return (error);
#ifdef KTR
		if (__predict_false(ktr_nvgpu_enable &
		    ktr_nvgpu_display_window_kick_mask)) {
			uint32_t get = nvgsp_disp_channel_read_user(
			    display->windows[window].backend, 4) >> 2;
			KTR_LOG(nvgpu_display_window_kick, window,
			    display->atomic.window_notifier[window],
			    display->windows[window].push.cur,
			    display->windows[window].push.put, get);
		}
#endif
	}
	if (display->atomic.core_changed) {
		error = nvgpu_display_push_kick(&display->core.push);
		if (error != 0)
			return (error);
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display atomic flush windows=0x%x core=%d cursors=0x%x\n",
	    display->atomic.window_mask, display->atomic.core_changed,
	    display->atomic.cursor_mask);
	return (0);
}

static void
nvgpu_display_wake_notifier_wait(systimer_t timer,
    int in_ipi __unused, struct intrframe *frame __unused)
{
	wakeup(timer->data);
}

int
nvgpu_display_atomic_wait(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head *current;
	struct systimer timer;
	struct timespec deadline;
	struct timespec now;
	uint32_t head;
	uint32_t window;
	uint32_t poll;
	uint32_t status;
	uint32_t wait_token;
	int error;

	if (display == NULL || !display->atomic.active)
		return (EINVAL);
	if (display->atomic.core_changed) {
		for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
			status = nvgsp_vram_alloc_read32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER);
			if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
			    NVGPU_DISPLAY_NOTIFIER_FINISHED)
				break;
			DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
		}
		if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS) {
			nvgpu_log(NVGPU_LOG_INFO,
			    "display atomic core notifier timeout value=0x%08x\n",
			    status);
			return (ETIMEDOUT);
		}
	}
	for (head = 0; head < display->head_count; head++) {
		if ((display->atomic.audio_enable_mask & (1u << head)) == 0)
			continue;
		current = &display->heads[head];
		error = nvgsp_disp_set_audio(gpu, &current->route, head, true);
		if (error == 0)
			error = nvgsp_disp_set_eld(gpu, &current->route, head,
			    display->atomic.audio_eld[head],
			    display->atomic.audio_eld_size[head]);
		if (error != 0)
			nvgpu_log(NVGPU_LOG_INFO,
			    "display audio enable failed head=%u error=%d\n",
			    head, error);
	}
	for (window = 0; window < display->window_count; window++) {
		if ((display->atomic.window_notifier_mask & (1u << window)) == 0)
			continue;
		getnanouptime(&deadline);
		deadline.tv_sec += NVGPU_DISPLAY_ATOMIC_NOTIFIER_TIMEOUT_SEC;
		for (poll = 0;; poll++) {
			status = nvgsp_vram_alloc_read32(gpu, display->sync,
			    display->atomic.window_notifier[window]);
			if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
			    NVGPU_DISPLAY_NOTIFIER_BEGUN)
				break;
			getnanouptime(&now);
			if (timespeccmp(&now, &deadline, >=)) {
				nvgpu_log(NVGPU_LOG_INFO,
				    "display atomic notifier timeout window=%u value=0x%08x\n",
				    window, status);
				return (ETIMEDOUT);
			}
			tsleep_interlock(&wait_token, 0);
			systimer_init_oneshot(&timer,
			    nvgpu_display_wake_notifier_wait, &wait_token,
			    NVGPU_DISPLAY_ATOMIC_NOTIFIER_SLEEP_US);
			(void)tsleep(&wait_token, PINTERLOCKED, "nvwndwait", 0);
			systimer_del(&timer);
		}
#ifdef KTR
		if (__predict_false(ktr_nvgpu_enable &
		    ktr_nvgpu_display_window_armed_mask)) {
			uint32_t get = nvgsp_disp_channel_read_user(
			    display->windows[window].backend, 4) >> 2;
			KTR_LOG(nvgpu_display_window_armed, window,
			    display->atomic.window_notifier[window], poll, status,
			    display->windows[window].push.cur,
			    display->windows[window].push.put, get);
		}
#endif
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display atomic notifier begun window=%u value=0x%08x polls=%u\n",
		    window, status, poll);
	}
	return (0);
}

void
nvgpu_display_atomic_end(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	uint32_t window;

	if (display == NULL)
		return;
	display->core.push.cur = display->core.push.put;
	for (window = 0; window < display->window_count; window++)
		display->windows[window].push.cur =
		    display->windows[window].push.put;
	bzero(&display->atomic, sizeof(display->atomic));
	lwkt_reltoken(&display->token);
}

static void
nvgpu_display_receive_hotplug(void *arg, uint32_t plug_mask,
    uint32_t unplug_mask)
{
	struct nvgpu_display *display = arg;
	void (*callback)(void *, uint32_t, uint32_t);
	void *callback_arg;

	spin_lock(&display->event_lock);
	callback = display->event_ops.hotplug;
	callback_arg = display->event_arg;
	spin_unlock(&display->event_lock);
	if (callback != NULL)
		callback(callback_arg, plug_mask, unplug_mask);
}

static void
nvgpu_display_receive_dp_irq(void *arg, uint32_t display_id)
{
	struct nvgpu_display *display = arg;
	void (*callback)(void *, uint32_t);
	void *callback_arg;

	spin_lock(&display->event_lock);
	callback = display->event_ops.dp_irq;
	callback_arg = display->event_arg;
	spin_unlock(&display->event_lock);
	if (callback != NULL)
		callback(callback_arg, display_id);
}

static const struct nvgsp_display_event_ops nvgpu_display_backend_event_ops = {
	.hotplug = nvgpu_display_receive_hotplug,
	.dp_irq = nvgpu_display_receive_dp_irq,
};

/* Create the display channels without changing any active scanout state. */
int
nvgpu_display_init(struct nvgpu_device *gpu)
{
	const struct nvgpu_chip_config *chip = nvgpu_device_get_chip(gpu);
	struct nvgpu_display *display;
	uint64_t sync_paddr;
	uint64_t vram_base;
	uint64_t vram_size;
	uint64_t vram_limit;
	uint64_t input_lut_size;
	uint64_t output_lut_size;
	uint32_t flags;
	uint32_t index;
	uint32_t lut;
	int error;

	if (gpu == NULL || chip == NULL)
		return (EINVAL);
	if (nvgpu_device_get_display(gpu) != NULL)
		return (0);
	display = kmalloc(sizeof(*display), M_DEVBUF, M_WAITOK | M_ZERO);
	display->gpu = gpu;
	display->head_count = MIN(nvgsp_disp_get_head_count(gpu),
	    NVGPU_DISPLAY_MAX_CURSORS);
	display->window_count = MIN(chip->display_windows,
	    NVGPU_DISPLAY_MAX_WINDOWS);
	display->cursor_count = MIN(chip->display_cursors,
	    NVGPU_DISPLAY_MAX_CURSORS);
	lwkt_token_init(&display->token, "nvgpud");
	spin_init(&display->event_lock, "nvgpud event");
	display->assign_windows = true;
	nvgpu_device_set_display(gpu, display);

	display->sync = nvgsp_vram_alloc_display(gpu, PAGE_SIZE, PAGE_SIZE,
	    display);
	if (display->sync == NULL) {
		error = ENOMEM;
		goto fail;
	}
	error = nvgsp_vram_alloc_map_bar1_scatter(gpu, display->sync, PAGE_SIZE, 1);
	if (error != 0)
		goto fail;
	for (index = 0; index < PAGE_SIZE / sizeof(uint32_t); index++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    index * sizeof(uint32_t), 0);
	input_lut_size = (uint64_t)NVGPU_DISPLAY_LUT_SIZE * display->window_count;
	output_lut_size = (uint64_t)NVGPU_DISPLAY_LUT_SIZE * display->head_count;
	display->input_lut = nvgsp_vram_alloc_display(gpu, input_lut_size,
	    PAGE_SIZE, display);
	if (display->input_lut == NULL) {
		error = ENOMEM;
		goto fail;
	}
	error = nvgsp_vram_alloc_map_bar1_scatter(gpu, display->input_lut,
	    input_lut_size, atop(input_lut_size));
	if (error != 0)
		goto fail;
	display->output_lut = nvgsp_vram_alloc_display(gpu, output_lut_size,
	    PAGE_SIZE, display);
	if (display->output_lut == NULL) {
		error = ENOMEM;
		goto fail;
	}
	error = nvgsp_vram_alloc_map_bar1_scatter(gpu, display->output_lut,
	    output_lut_size, atop(output_lut_size));
	if (error != 0)
		goto fail;
	for (lut = 0; lut < MAX(display->window_count, display->head_count); lut++) {
		uint64_t input_base = NVGPU_DISPLAY_INPUT_LUT_OFFSET(lut);
		uint64_t output_base = NVGPU_DISPLAY_OUTPUT_LUT_OFFSET(lut);

		for (index = 0; index < NVGPU_DISPLAY_LUT_PREFIX_ENTRIES; index++) {
			if (lut < display->window_count) {
				nvgsp_vram_alloc_write32(gpu, display->input_lut,
				    input_base + index * 8u, 0);
				nvgsp_vram_alloc_write32(gpu, display->input_lut,
				    input_base + index * 8u + 4, 0);
			}
			if (lut < display->head_count) {
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_base + index * 8u, 0);
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_base + index * 8u + 4, 0);
			}
		}
		for (index = 0; index < NVGPU_DISPLAY_LUT_ENTRIES; index++) {
			uint16_t identity = (uint16_t)((index << 16) >> 10);
			uint16_t fixed = identity;
			uint16_t input_value = 0;
			uint64_t offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES + index) * 8u;
			int exponent = 0;
			int mantissa = 0;

			if (fixed != 0) {
				while (--exponent != 0 && (fixed & 0x8000u) == 0)
					fixed <<= 1;
				mantissa = ((fixed << 1) & 0xffc0u) >> 6;
				exponent += 15;
				input_value = (uint16_t)((exponent << 10) | mantissa);
			}
			if (lut < display->window_count) {
				nvgsp_vram_alloc_write32(gpu, display->input_lut,
				    input_base + offset, (uint32_t)input_value |
				    ((uint32_t)input_value << 16));
				nvgsp_vram_alloc_write32(gpu, display->input_lut,
				    input_base + offset + 4, input_value);
			}
			if (lut < display->head_count) {
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_base + offset, (uint32_t)identity |
				    ((uint32_t)identity << 16));
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_base + offset + 4, identity);
			}
		}
		index = NVGPU_DISPLAY_LUT_TOTAL_ENTRIES - 1u;
		if (lut < display->window_count) {
			nvgsp_vram_alloc_write32(gpu, display->input_lut,
			    input_base + index * 8u, nvgsp_vram_alloc_read32(gpu,
			    display->input_lut, input_base + (index - 1u) * 8u));
			nvgsp_vram_alloc_write32(gpu, display->input_lut,
			    input_base + index * 8u + 4, nvgsp_vram_alloc_read32(gpu,
			    display->input_lut, input_base + (index - 1u) * 8u + 4));
			(void)nvgsp_vram_alloc_read32(gpu, display->input_lut,
			    input_base + index * 8u);
		}
		if (lut < display->head_count) {
			nvgsp_vram_alloc_write32(gpu, display->output_lut,
			    output_base + index * 8u, nvgsp_vram_alloc_read32(gpu,
			    display->output_lut, output_base + (index - 1u) * 8u));
			nvgsp_vram_alloc_write32(gpu, display->output_lut,
			    output_base + index * 8u + 4, nvgsp_vram_alloc_read32(gpu,
			    display->output_lut, output_base + (index - 1u) * 8u + 4));
			(void)nvgsp_vram_alloc_read32(gpu, display->output_lut,
			    output_base + index * 8u);
		}
	}
	sync_paddr = nvgsp_vram_alloc_get_paddr(display->sync);
	error = nvgsp_disp_get_vram_range(gpu, &vram_base, &vram_size);
	if (error != 0 || vram_base + vram_size <= vram_base) {
		error = error != 0 ? error : EOVERFLOW;
		goto fail;
	}
	vram_limit = vram_base + vram_size - 1;
	flags = NVGPU_DISPLAY_DMA_VRAM | NVGPU_DISPLAY_DMA_RW |
	    NVGPU_DISPLAY_DMA_LARGE_PAGE;

	error = nvgsp_disp_create_dma_channel(gpu, chip->class_display_core, 0,
	    &display->core.backend);
	if (error != 0)
		goto fail;
	nvgpu_display_push_init(&display->core.push, display->core.backend);
	if (display->core.push.ring == NULL) {
		error = ENXIO;
		goto fail;
	}
	error = nvgsp_disp_channel_bind_context(display->core.backend,
	    NVGPU_DISPLAY_HANDLE_SYNC, sync_paddr, sync_paddr + PAGE_SIZE - 1,
	    flags, &display->core.sync_cookie);
	if (error != 0)
		goto fail;
	error = nvgsp_disp_channel_bind_context(display->core.backend,
	    NVGPU_DISPLAY_HANDLE_VRAM, 0, vram_limit, flags,
	    &display->core.vram_cookie);
	if (error != 0)
		goto fail;

	for (index = 0; index < display->window_count; index++) {
		display->window_state[index].next_notifier_offset =
		    NVGPU_DISPLAY_WINDOW_NOTIFIER(index);
		error = nvgsp_disp_create_dma_channel(gpu,
		    chip->class_display_window, index,
		    &display->windows[index].backend);
		if (error != 0)
			goto fail;
		nvgpu_display_push_init(&display->windows[index].push,
		    display->windows[index].backend);
		if (display->windows[index].push.ring == NULL) {
			error = ENXIO;
			goto fail;
		}
		error = nvgsp_disp_channel_bind_context(
		    display->windows[index].backend, NVGPU_DISPLAY_HANDLE_SYNC,
		    sync_paddr, sync_paddr + PAGE_SIZE - 1, flags,
		    &display->windows[index].sync_cookie);
		if (error != 0)
			goto fail;
		error = nvgsp_disp_channel_bind_context(
		    display->windows[index].backend, NVGPU_DISPLAY_HANDLE_VRAM,
		    0, vram_limit, flags, &display->windows[index].vram_cookie);
		if (error != 0)
			goto fail;
		error = nvgsp_disp_channel_bind_context(
		    display->windows[index].backend,
		    NVGPU_DISPLAY_HANDLE_WINDOW(NVGPU_DISPLAY_KIND_PITCH), 0,
		    vram_limit, flags, &display->windows[index].pitch_cookie);
		if (error != 0)
			goto fail;
		error = nvgsp_disp_channel_bind_context(
		    display->windows[index].backend,
		    NVGPU_DISPLAY_HANDLE_WINDOW(NVGPU_DISPLAY_KIND_BLOCK_LINEAR), 0,
		    vram_limit, flags | NVGPU_DISPLAY_DMA_BLOCK_LINEAR,
		    &display->windows[index].block_cookie);
		if (error != 0)
			goto fail;
	}

	for (index = 0; index < display->cursor_count; index++) {
		error = nvgsp_disp_create_pio_channel(gpu,
		    chip->class_display_cursor, index,
		    &display->cursors[index].backend);
		if (error != 0)
			goto fail;
	}
	nvgsp_disp_set_event_ops(gpu, &nvgpu_display_backend_event_ops, display);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display channels ready core=1 heads=%u windows=%u cursors=%u sync=0x%jx\n",
	    display->head_count, display->window_count, display->cursor_count,
	    (uintmax_t)sync_paddr);
	return (0);

fail:
	nvgpu_display_fini(gpu);
	return (error);
}

/* Release channels before the GSP display root and TTM disappear. */
void
nvgpu_display_fini(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	uint32_t index;

	if (display == NULL)
		return;
	nvgsp_disp_set_event_ops(gpu, NULL, NULL);
	nvgpu_device_set_display(gpu, NULL);
	spin_lock(&display->event_lock);
	bzero(&display->event_ops, sizeof(display->event_ops));
	display->event_arg = NULL;
	spin_unlock(&display->event_lock);
	for (index = display->cursor_count; index > 0; index--)
		nvgsp_disp_destroy_channel(display->cursors[index - 1].backend);
	if (display->console != NULL) {
		nvgsp_vram_alloc_unmap_bar1_range(gpu, display->console);
		nvgsp_vram_free_display(gpu, display->console);
		display->console = NULL;
	}
	for (index = display->window_count; index > 0; index--) {
		struct nvgpu_display_channel *channel = &display->windows[index - 1];

		nvgsp_disp_channel_unbind_context(channel->backend,
		    channel->block_cookie);
		nvgsp_disp_channel_unbind_context(channel->backend,
		    channel->pitch_cookie);
		nvgsp_disp_channel_unbind_context(channel->backend,
		    channel->vram_cookie);
		nvgsp_disp_channel_unbind_context(channel->backend,
		    channel->sync_cookie);
		nvgsp_disp_destroy_channel(channel->backend);
	}
	nvgsp_disp_channel_unbind_context(display->core.backend,
	    display->core.vram_cookie);
	nvgsp_disp_channel_unbind_context(display->core.backend,
	    display->core.sync_cookie);
	nvgsp_disp_destroy_channel(display->core.backend);
	if (display->sync != NULL) {
		nvgsp_vram_alloc_unmap_bar1_scatter(gpu, display->sync);
		nvgsp_vram_free_display(gpu, display->sync);
	}
	if (display->output_lut != NULL) {
		nvgsp_vram_alloc_unmap_bar1_scatter(gpu, display->output_lut);
		nvgsp_vram_free_display(gpu, display->output_lut);
	}
	if (display->input_lut != NULL) {
		nvgsp_vram_alloc_unmap_bar1_scatter(gpu, display->input_lut);
		nvgsp_vram_free_display(gpu, display->input_lut);
	}
	spin_uninit(&display->event_lock);
	lwkt_token_uninit(&display->token);
	kfree(display, M_DEVBUF);
}

uint32_t
nvgpu_display_get_head_count(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	return (display != NULL ? display->head_count : 0);
}

uint32_t
nvgpu_display_get_output_mask(struct nvgpu_device *gpu)
{
	return (nvgsp_disp_get_supported_mask(gpu));
}

int
nvgpu_display_get_output(struct nvgpu_device *gpu, uint32_t display_id,
    struct nvgpu_display_output_info *info)
{
	struct nvgsp_display_output output;
	int error;

	if (info == NULL)
		return (EINVAL);
	error = nvgsp_disp_get_output(gpu, display_id, &output);
	if (error != 0)
		return (error);
	bzero(info, sizeof(*info));
	info->display_id = output.display_id;
	info->possible_heads = output.heads;
	info->connector_type = output.connector_type;
	info->connector_location = output.connector_location;
	info->output_location = output.output_location;
	info->max_link_rate = output.max_link_rate;
	info->mst_capable = output.mst_capable;
	info->dp_interlace_capable = output.dp_interlace_capable;
	if (output.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS)
		info->protocol = NVGPU_DISPLAY_PROTOCOL_TMDS;
	else if (output.protocol == NVGSP_DISPLAY_PROTOCOL_DP)
		info->protocol = NVGPU_DISPLAY_PROTOCOL_DP;
	return (0);
}

int
nvgpu_display_detect_output(struct nvgpu_device *gpu, uint32_t display_id)
{
	return (nvgsp_disp_detect(gpu, display_id));
}

int
nvgpu_display_read_edid(struct nvgpu_device *gpu, uint32_t display_id,
    uint8_t *data, uint32_t *size)
{
	return (nvgsp_disp_read_edid(gpu, display_id, data, size));
}

int
nvgpu_display_prepare_output(struct nvgpu_device *gpu, uint32_t head,
    uint32_t display_id, const struct nvgpu_display_mode *mode,
    const struct nvgpu_display_output_config *config,
    struct nvgpu_display_prepared_output **prepared)
{
	static const uint8_t rates[] = { 0x06, 0x0a, 0x14, 0x1e };
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_prepared_output *next;
	struct nvgsp_display_output output;
	uint8_t dpcd[16];
	uint8_t port_cap[4];
	uint8_t bpc;
	uint8_t done;
	uint8_t lanes;
	uint32_t max_rate;
	uint32_t min_rate;
	uint32_t clock_khz;
	int error;

	if (display == NULL || mode == NULL || prepared == NULL ||
	    head >= display->head_count || display_id == 0)
		return (EINVAL);
	*prepared = NULL;
	error = nvgsp_disp_get_output(gpu, display_id, &output);
	if (error != 0)
		return (error);
	if ((output.heads & (1u << head)) == 0)
		return (EINVAL);
	if (mode->interlace && !output.dp_interlace_capable &&
	    output.protocol == NVGSP_DISPLAY_PROTOCOL_DP)
		return (ENOTSUP);
	next = kmalloc(sizeof(*next), M_DEVBUF, M_WAITOK | M_ZERO);
	next->head = head;
	next->valid = true;
	if (config != NULL)
		next->config = *config;
	if (next->config.bpc == 0)
		next->config.bpc = 8;
	error = nvgsp_disp_acquire_output(gpu, display_id,
	    next->config.audio_enabled, &next->route);
	if (error != 0) {
		kfree(next, M_DEVBUF);
		return (error);
	}

	if (output.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS) {
		next->hdmi_sink.scdc_supported =
		    next->config.hdmi_scdc_supported;
		next->hdmi_sink.scrambling_supported =
		    next->config.hdmi_scrambling_supported;
		next->hdmi_sink.low_rate_scrambling_supported =
		    next->config.hdmi_low_rate_scrambling_supported;
		*prepared = next;
		return (0);
	}
	if (output.protocol != NVGSP_DISPLAY_PROTOCOL_DP) {
		nvgsp_disp_release_output(gpu, &next->route);
		kfree(next, M_DEVBUF);
		return (ENOTSUP);
	}

	bzero(dpcd, sizeof(dpcd));
	for (done = 0; done < sizeof(dpcd);) {
		uint8_t want = MIN((uint8_t)(sizeof(dpcd) - done), (uint8_t)16);
		uint8_t reply_size = want;

		error = nvgsp_disp_transfer_aux(gpu, display_id,
		    NVGPU_DISPLAY_DP_AUX_NATIVE_READ, done, dpcd + done,
		    &reply_size);
		if (error != NVGPU_DISPLAY_DP_AUX_REPLY_ACK)
			goto fail;
		if (reply_size == 0) {
			error = EIO;
			goto fail;
		}
		done += MIN(reply_size, want);
	}
	clock_khz = mode->clock_khz;
	if ((dpcd[NVGPU_DISPLAY_DP_DOWNSTREAM_PRESENT] &
	    (NVGPU_DISPLAY_DP_DOWNSTREAM_ATTACHED |
	    NVGPU_DISPLAY_DP_DOWNSTREAM_DETAILED)) ==
	    (NVGPU_DISPLAY_DP_DOWNSTREAM_ATTACHED |
	    NVGPU_DISPLAY_DP_DOWNSTREAM_DETAILED)) {
		uint32_t downstream_max_clock = 0;
		uint8_t reply_size = sizeof(port_cap);

		bzero(port_cap, sizeof(port_cap));
		error = nvgsp_disp_transfer_aux(gpu, display_id,
		    NVGPU_DISPLAY_DP_AUX_NATIVE_READ,
		    NVGPU_DISPLAY_DP_DOWNSTREAM_PORT, port_cap, &reply_size);
		if (error != NVGPU_DISPLAY_DP_AUX_REPLY_ACK ||
		    reply_size != sizeof(port_cap)) {
			error = error != NVGPU_DISPLAY_DP_AUX_REPLY_ACK ? error : EIO;
			goto fail;
		}
		switch (port_cap[0] & NVGPU_DISPLAY_DP_PORT_TYPE_MASK) {
		case NVGPU_DISPLAY_DP_PORT_TYPE_VGA:
			downstream_max_clock = port_cap[1] * 8000u;
			break;
		case NVGPU_DISPLAY_DP_PORT_TYPE_DVI:
		case NVGPU_DISPLAY_DP_PORT_TYPE_HDMI:
		case NVGPU_DISPLAY_DP_PORT_TYPE_DUAL_MODE:
			downstream_max_clock = port_cap[1] * 2500u;
			break;
		default:
			break;
		}
		if (downstream_max_clock != 0 && clock_khz > downstream_max_clock) {
			error = ERANGE;
			goto fail;
		}
	}
	bpc = next->config.bpc;
	max_rate = output.max_link_rate;
	if (max_rate == 0 || max_rate > dpcd[NVGPU_DISPLAY_DP_MAX_LINK_RATE])
		max_rate = dpcd[NVGPU_DISPLAY_DP_MAX_LINK_RATE];
	lanes = dpcd[NVGPU_DISPLAY_DP_MAX_LANE_COUNT] &
	    NVGPU_DISPLAY_DP_LANE_COUNT_MASK;
	if (lanes >= 4)
		lanes = 4;
	else if (lanes >= 2)
		lanes = 2;
	else if (lanes != 0)
		lanes = 1;
	if (clock_khz == 0 || lanes == 0) {
		error = EINVAL;
		goto fail;
	}
	if ((uint64_t)clock_khz * bpc * 3u > UINT32_MAX * 8ULL) {
		error = ERANGE;
		goto fail;
	}
	min_rate = div_u64((uint64_t)clock_khz * bpc * 3u + 7u, 8u);
	error = ERANGE;
	for (; lanes != 0; lanes >>= 1) {
		uint32_t rate_index;

		for (rate_index = 0; rate_index < nitems(rates); rate_index++) {
			uint8_t link_bw = rates[rate_index];
			uint32_t link_rate;
			uint32_t adjust;
			uint32_t minimum;
			uint32_t active = mode->hdisplay;
			uint32_t total = mode->htotal;
			uint32_t depth = bpc * 3u;
			uint32_t hblank;
			uint32_t symbols_per_line;
			uint32_t blanking_bits;
			uint32_t steering_bits = 0;
			uint32_t min_hblank;
			uint32_t remain;
			uint64_t payload;
			uint64_t variance;
			uint64_t base;
			int64_t hsym;
			int64_t vsym;

			switch (link_bw) {
			case 0x06: link_rate = 162000; break;
			case 0x0a: link_rate = 270000; break;
			case 0x14: link_rate = 540000; break;
			case 0x1e: link_rate = 810000; break;
			default: continue;
			}
			if (link_bw > max_rate || link_rate * lanes < min_rate ||
			    total <= active || active <= 60 ||
			    (uint64_t)clock_khz * depth >=
			    (uint64_t)8u * link_rate * lanes)
				continue;
			hblank = total - active;
			payload = div_u64((uint64_t)clock_khz * depth *
			    NVGPU_DISPLAY_DP_PRECISION, 8u * link_rate * lanes);
			if (payload >= NVGPU_DISPLAY_DP_PRECISION)
				continue;
			variance = div_u64(payload * NVGPU_DISPLAY_DP_TU_SIZE *
			    (NVGPU_DISPLAY_DP_PRECISION - payload),
			    NVGPU_DISPLAY_DP_PRECISION);
			base = div_u64(2u * div_u64((uint64_t)depth *
			    NVGPU_DISPLAY_DP_PRECISION, 8u * lanes) + variance,
			    NVGPU_DISPLAY_DP_PRECISION);
			adjust = output.increased_watermark ?
			    NVGPU_DISPLAY_DP_WM_ADJUST_INC :
			    NVGPU_DISPLAY_DP_WM_ADJUST;
			minimum = output.increased_watermark ?
			    NVGPU_DISPLAY_DP_WM_LIMIT_INC :
			    NVGPU_DISPLAY_DP_WM_LIMIT;
			next->dp_stream.watermark = adjust + (uint32_t)base;
			symbols_per_line = div_u64((uint64_t)active * depth,
			    8u * lanes);
			if (next->dp_stream.watermark > 39u ||
			    next->dp_stream.watermark > symbols_per_line)
				continue;
			if (next->dp_stream.watermark < minimum)
				next->dp_stream.watermark = minimum;
			blanking_bits = 3u * 8u * lanes;
			if ((dpcd[NVGPU_DISPLAY_DP_MAX_LANE_COUNT] &
			    NVGPU_DISPLAY_DP_ENHANCED_FRAME) != 0)
				blanking_bits += 3u * 8u * lanes;
			blanking_bits += 3u * 8u * 4u;
			remain = active % lanes;
			if (remain != 0)
				steering_bits = (lanes - remain) * depth;
			blanking_bits += steering_bits;
			min_hblank = div_u64((uint64_t)blanking_bits *
			    NVGPU_DISPLAY_DP_PRECISION, 8u * lanes);
			min_hblank = div_u64((uint64_t)min_hblank * clock_khz,
			    link_rate);
			min_hblank = div_u64(min_hblank,
			    NVGPU_DISPLAY_DP_PRECISION) + 12u;
			if (min_hblank > hblank)
				continue;
			hsym = (int64_t)div_u64((uint64_t)(hblank - min_hblank) *
			    link_rate, clock_khz) - 4;
			hsym -= lanes == 1 ? 9 : lanes == 2 ? 6 : 3;
			vsym = (int64_t)div_u64((uint64_t)(active - 40u) *
			    link_rate, clock_khz) - 1;
			vsym -= lanes == 1 ? 39 : lanes == 2 ? 21 : 12;
			next->dp_stream.head = head;
			next->dp_stream.lane_count = lanes;
			next->dp_stream.link_bandwidth = link_bw;
			next->dp_stream.horizontal_blank_symbols =
			    hsym < 0 ? 0 : (uint32_t)hsym;
			next->dp_stream.vertical_blank_symbols =
			    vsym < 0 ? 0 : (uint32_t)vsym;
			next->dp_stream.enhanced_framing =
			    (dpcd[NVGPU_DISPLAY_DP_MAX_LANE_COUNT] &
			    NVGPU_DISPLAY_DP_ENHANCED_FRAME) != 0;
			next->dp_stream.mst = false;
			error = 0;
			goto prepared_dp;
		}
	}
fail:
	nvgsp_disp_release_output(gpu, &next->route);
	kfree(next, M_DEVBUF);
	return (error != 0 ? error : EIO);

prepared_dp:
	*prepared = next;
	return (0);
}

void
nvgpu_display_abort_output(struct nvgpu_device *gpu,
    struct nvgpu_display_prepared_output *prepared)
{
	if (prepared == NULL)
		return;
	if (prepared->valid)
		nvgsp_disp_release_output(gpu, &prepared->route);
	kfree(prepared, M_DEVBUF);
}

int
nvgpu_display_enable(struct nvgpu_device *gpu, uint32_t head, uint32_t window,
    const struct nvgpu_display_head_config *head_config,
    const struct nvgpu_display_scanout *scanout,
    struct nvgpu_display_prepared_output *prepared)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head_state head_state;
	struct nvgpu_display_window_state window_state;
	struct nvgpu_display_head *current;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t notifier_offset;
	uint32_t poll;
	uint32_t status;
	uint8_t format;
	bool sanitize_window;
	bool update_submitted = false;
	int error = 0;

	if (display == NULL || head_config == NULL || scanout == NULL ||
	    prepared == NULL || !prepared->valid || prepared->head != head ||
	    head >= display->head_count || window >= display->window_count ||
	    scanout->paddr == 0 || (scanout->paddr & 0xffu) != 0 ||
	    scanout->width == 0 || scanout->height == 0 ||
	    scanout->source_width == 0 || scanout->source_height == 0 ||
	    scanout->output_width == 0 || scanout->output_height == 0 ||
	    scanout->pitch == 0 || (scanout->pitch & 0x3fu) != 0) {
		nvgpu_display_abort_output(gpu, prepared);
		return (EINVAL);
	}
	format = nvgpu_display_method_get_format(scanout->format);
	if (format == 0) {
		nvgpu_display_abort_output(gpu, prepared);
		return (EINVAL);
	}
	if (scanout->layout == NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR &&
	    (scanout->kind != NVGPU_DISPLAY_KIND_BLOCK_LINEAR ||
	    scanout->block_height > 5)) {
		nvgpu_display_abort_output(gpu, prepared);
		return (EINVAL);
	}
	if (scanout->layout == NVGPU_DISPLAY_LAYOUT_PITCH &&
	    scanout->kind != NVGPU_DISPLAY_KIND_PITCH) {
		nvgpu_display_abort_output(gpu, prepared);
		return (EINVAL);
	}
	lwkt_gettoken(&display->token);
	current = &display->heads[head];
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display enable start head=%u win=%u active=%d route=%d "
	    "core=%d assign=%d paddr=0x%llx size=0x%llx pitch=%u "
	    "layout=%u kind=0x%x block_height=%u\n",
	    head, window, current->active, current->route.acquired,
	    display->core_initialized, display->assign_windows,
	    (unsigned long long)scanout->paddr,
	    (unsigned long long)scanout->size, scanout->pitch,
	    scanout->layout, scanout->kind, scanout->block_height);
	if (current->active || current->route.acquired) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display enable busy head=%u win=%u active=%d route=%d\n",
		    head, window, current->active, current->route.acquired);
		error = EBUSY;
		goto out;
	}
	if (!display->core_initialized) {
		error = nvgpu_display_method_init_core(&display->core.push,
		    NVGPU_DISPLAY_HANDLE_SYNC, display->window_count);
		if (error != 0)
			goto out;
		display->core_initialized = true;
	}
	sanitize_window = display->assign_windows;
	if (display->assign_windows) {
		interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
		error = nvgpu_display_method_assign_windows(&display->core.push,
		    display->window_count);
		if (error == 0)
			error = nvgpu_display_method_update_core(&display->core.push,
			    interlock, false, 0);
		if (error != 0)
			goto out;
		display->assign_windows = false;
		bzero(interlock, sizeof(interlock));
	}

	if (prepared->route.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS) {
		error = nvgsp_disp_enable_hdmi(gpu, &prepared->route,
		    &prepared->hdmi_sink);
	} else if (prepared->route.protocol == NVGSP_DISPLAY_PROTOCOL_DP) {
		error = nvgsp_disp_train_dp(gpu, &prepared->route,
		    prepared->dp_stream.lane_count,
		    prepared->dp_stream.link_bandwidth, false);
		if (error == 0)
			error = nvgsp_disp_configure_dp_stream(gpu,
			    &prepared->route, &prepared->dp_stream);
	} else {
		error = ENOTSUP;
	}
	if (error != 0)
		goto out;
	current->route = prepared->route;
	bzero(&prepared->route, sizeof(prepared->route));
	current->display_id = current->route.display_id;
	current->dp_lane_count = prepared->dp_stream.lane_count;
	current->dp_link_bandwidth = prepared->dp_stream.link_bandwidth;
	current->dp_mst = prepared->dp_stream.mst;
	bzero(&head_state, sizeof(head_state));
	head_state.mode = head_config->mode;
	head_state.view.input_width = head_config->input_width;
	head_state.view.input_height = head_config->input_height;
	head_state.view.output_width = head_config->output_width;
	head_state.view.output_height = head_config->output_height;
	/* Legacy head encoding: zero selects 24bpp RGB; bpc is separate. */
	head_state.output.depth = 0;
	head_state.output.negative_hsync = head_config->mode.negative_hsync;
	head_state.output.negative_vsync = head_config->mode.negative_vsync;
	head_state.dither.enable = head_config->dither_enabled;
	head_state.dither.bits = head_config->dither_bits == 8 ? 1 : 0;
	head_state.dither.mode = head_config->dither_mode;
	head_state.output_lut.handle = NVGPU_DISPLAY_HANDLE_VRAM;
	head_state.output_lut.offset =
	    nvgsp_vram_alloc_get_paddr(display->output_lut) +
	    NVGPU_DISPLAY_OUTPUT_LUT_OFFSET(head);
	head_state.output_lut.mode = NVGPU_DISPLAY_METHOD_LUT_DIRECT10;
	head_state.output_lut.size = NVGPU_DISPLAY_LUT_TOTAL_ENTRIES;
	head_state.output_lut.output_mode =
	    NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_ENABLE;

	error = nvgpu_display_method_set_head_display_id(&display->core.push,
	    head, current->display_id);
	if (error == 0)
		error = nvgpu_display_method_set_head_view(&display->core.push,
		    head, display->window_count, &head_state);
	if (error == 0)
		error = nvgpu_display_method_set_head_mode(&display->core.push,
		    head, &head_state);
	if (error == 0)
		error = nvgpu_display_method_set_head_dither(&display->core.push,
		    head, &head_state);
	if (error == 0)
		error = nvgpu_display_method_set_head_procamp(&display->core.push,
		    head, &head_state);
	if (error == 0)
		error = nvgpu_display_method_set_head_output(&display->core.push,
		    head, &head_state);
	if (error == 0)
		error = nvgpu_display_method_route_sor(&display->core.push,
		    current->route.sor_index,
		    current->route.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS ?
		    NVGPU_DISPLAY_PROTOCOL_TMDS : NVGPU_DISPLAY_PROTOCOL_DP,
		    current->route.link, head);
	if (error == 0)
		error = nvgpu_display_method_set_head_output_lut(
		    &display->core.push, head, &head_state);
	if (error != 0)
		goto out;

	bzero(&window_state, sizeof(window_state));
	window_state.image.interval = 1;
	window_state.image.mode = NVGPU_DISPLAY_METHOD_PRESENT_NON_TEARING;
	window_state.image.width = scanout->width;
	window_state.image.height = scanout->height;
	window_state.image.block_height = scanout->block_height;
	window_state.image.layout = scanout->layout ==
	    NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR ?
	    NVGPU_DISPLAY_METHOD_LAYOUT_BLOCK_LINEAR :
	    NVGPU_DISPLAY_METHOD_LAYOUT_PITCH;
	window_state.image.format = format;
	window_state.image.block = scanout->layout ==
	    NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR ? scanout->pitch >> 6 : 0;
	window_state.image.pitch = scanout->layout ==
	    NVGPU_DISPLAY_LAYOUT_PITCH ? scanout->pitch : 0;
	window_state.image.handle = NVGPU_DISPLAY_HANDLE_WINDOW(scanout->kind);
	window_state.image.offset = scanout->paddr;
	window_state.image.source_x = scanout->source_x;
	window_state.image.source_y = scanout->source_y;
	window_state.image.source_width = scanout->source_width;
	window_state.image.source_height = scanout->source_height;
	window_state.image.output_width = scanout->output_width;
	window_state.image.output_height = scanout->output_height;
	window_state.input_lut.handle = NVGPU_DISPLAY_HANDLE_VRAM;
	window_state.input_lut.offset =
	    nvgsp_vram_alloc_get_paddr(display->input_lut) +
	    NVGPU_DISPLAY_INPUT_LUT_OFFSET(window);
	window_state.input_lut.size = NVGPU_DISPLAY_LUT_TOTAL_ENTRIES;
	window_state.input_lut.mode = NVGPU_DISPLAY_METHOD_LUT_DIRECT10;
	window_state.input_lut.output_mode =
	    NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_DISABLE;
	window_state.blend.depth = 255;
	window_state.blend.k1 = 255;
	window_state.blend.source_color = NVGPU_DISPLAY_METHOD_BLEND_K1;
	window_state.blend.destination_color = NVGPU_DISPLAY_METHOD_BLEND_NEG_K1;
	notifier_offset = display->window_state[window].next_notifier_offset;
	display->window_state[window].next_notifier_offset ^= 0x10u;
	window_state.notifier.handle = NVGPU_DISPLAY_HANDLE_SYNC;
	window_state.notifier.mode = 0;
	window_state.notifier.offset = notifier_offset;
	for (poll = 0; poll < 4; poll++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    notifier_offset + poll * 4u, 0);

	if (sanitize_window)
		error = nvgpu_display_method_clear_window_notifier(
		    &display->windows[window].push);
	if (error == 0 && sanitize_window)
		error = nvgpu_display_method_clear_window_semaphore(
		    &display->windows[window].push);
	if (error == 0 && sanitize_window)
		error = nvgpu_display_method_clear_window_input_lut(
		    &display->windows[window].push);
	if (error == 0 && sanitize_window)
		error = nvgpu_display_method_clear_window_csc(
		    &display->windows[window].push);
	if (error == 0)
		error = nvgpu_display_method_set_window_notifier(
		    &display->windows[window].push, &window_state);
	if (error == 0)
		error = nvgpu_display_method_set_window_image(
		    &display->windows[window].push, &window_state);
	if (error == 0)
		error = nvgpu_display_method_set_window_input_lut(
		    &display->windows[window].push, &window_state);
	if (error == 0)
		error = nvgpu_display_method_clear_window_csc(
		    &display->windows[window].push);
	if (error == 0)
		error = nvgpu_display_method_set_window_blend(
		    &display->windows[window].push, &window_state);
	if (error != 0)
		goto out;

	if (display->atomic.active) {
		display->atomic.window_mask |= 1u << window;
		display->atomic.window_notifier_mask |= 1u << window;
		display->atomic.window_notifier[window] = notifier_offset;
		display->atomic.core_changed = true;
		update_submitted = true;
		current->active = true;
		display->window_state[window].active = true;
		display->window_state[window].color_active = true;
		if (prepared->config.audio_enabled && prepared->config.eld_size != 0) {
			display->atomic.audio_enable_mask |= 1u << head;
			display->atomic.audio_eld_size[head] =
			    prepared->config.eld_size;
			memcpy(display->atomic.audio_eld[head], prepared->config.eld,
			    prepared->config.eld_size);
		}
		goto out;
	}
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
	interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW] = 1u << window;
	error = nvgpu_display_method_update_window(
	    &display->windows[window].push, window, interlock);
	if (error != 0)
		goto out;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display enable window update submitted head=%u win=%u notifier=0x%x\n",
	    head, window, notifier_offset);
	update_submitted = true;
	current->active = true;
	display->window_state[window].active = true;
	display->window_state[window].color_active = true;
	for (poll = 0; poll < 4; poll++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    NVGPU_DISPLAY_CORE_NOTIFIER + poll * 4u, 0);
	error = nvgpu_display_method_update_core(&display->core.push, interlock,
	    true, NVGPU_DISPLAY_CORE_NOTIFIER);
	if (error != 0)
		goto out;
	for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
		status = nvgsp_vram_alloc_read32(gpu, display->sync,
		    NVGPU_DISPLAY_CORE_NOTIFIER);
		if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
		    NVGPU_DISPLAY_NOTIFIER_FINISHED)
			break;
		DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
	}
	if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "display core notifier timeout head=%u value=0x%08x "
		    "put=0x%08x get=0x%08x ctrl=0x%08x stat=0x%08x\n",
		    head, status,
		    nvgsp_disp_channel_read_user(display->core.backend, 0),
		    nvgsp_disp_channel_read_user(display->core.backend, 4),
		    nvgpu_device_rd32(gpu, 0x6104e0),
		    nvgpu_device_rd32(gpu, 0x610630));
		error = ETIMEDOUT;
		goto out;
	}
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display enable core notifier done head=%u value=0x%08x polls=%u\n",
	    head, status, poll);
	if (prepared->config.audio_enabled && prepared->config.eld_size != 0) {
		int audio_error;

		audio_error = nvgsp_disp_set_audio(gpu, &current->route, head, true);
		if (audio_error == 0)
			audio_error = nvgsp_disp_set_eld(gpu, &current->route, head,
			    prepared->config.eld, prepared->config.eld_size);
		if (audio_error != 0)
			nvgpu_log(NVGPU_LOG_INFO,
			    "display audio enable failed head=%u error=%d\n",
			    head, audio_error);
	}
	for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
		status = nvgsp_vram_alloc_read32(gpu, display->sync,
		    notifier_offset);
		if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
		    NVGPU_DISPLAY_NOTIFIER_BEGUN)
			break;
		DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
	}
	if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "display window notifier timeout head=%u window=%u value=0x%08x "
		    "put=0x%08x get=0x%08x ctrl=0x%08x stat=0x%08x\n",
		    head, window, status,
		    nvgsp_disp_channel_read_user(display->windows[window].backend, 0),
		    nvgsp_disp_channel_read_user(display->windows[window].backend, 4),
		    nvgpu_device_rd32(gpu, 0x6104e0 + (1u + window) * 4u),
		    nvgpu_device_rd32(gpu, 0x610664 + window * 4u));
		error = ETIMEDOUT;
	} else {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display enable window notifier begun head=%u win=%u "
		    "value=0x%08x polls=%u\n",
		    head, window, status, poll);
	}

out:
	if (error != 0)
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display enable exit error head=%u win=%u error=%d "
		    "submitted=%d active=%d route=%d\n",
		    head, window, error, update_submitted, current->active,
		    current->route.acquired);
	if (error != 0 && !update_submitted && current->route.acquired) {
		if (current->route.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS)
			nvgsp_disp_disable_hdmi(gpu, &current->route);
		if (current->route.audio)
			(void)nvgsp_disp_set_audio(gpu, &current->route, head, false);
		nvgsp_disp_release_output(gpu, &current->route);
		current->display_id = 0;
	}
	lwkt_reltoken(&display->token);
	nvgpu_display_abort_output(gpu, prepared);
	return (error);
}

int
nvgpu_display_update_primary(struct nvgpu_device *gpu, uint32_t head,
    uint32_t window, const struct nvgpu_display_scanout *scanout)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_window_state state;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t notifier_offset;
	uint32_t poll;
	uint32_t status;
	uint8_t format;
	int error;

	if (display == NULL || scanout == NULL || head >= display->head_count ||
	    window >= display->window_count || scanout->paddr == 0 ||
	    (scanout->paddr & 0xffu) != 0 || scanout->width == 0 ||
	    scanout->height == 0 || scanout->pitch == 0 ||
	    (scanout->pitch & 0x3fu) != 0 || scanout->source_width == 0 ||
	    scanout->source_height == 0 || scanout->output_width == 0 ||
	    scanout->output_height == 0)
		return (EINVAL);
	format = nvgpu_display_method_get_format(scanout->format);
	if (format == 0)
		return (EINVAL);
	if ((scanout->layout == NVGPU_DISPLAY_LAYOUT_PITCH &&
	    scanout->kind != NVGPU_DISPLAY_KIND_PITCH) ||
	    (scanout->layout == NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR &&
	    (scanout->kind != NVGPU_DISPLAY_KIND_BLOCK_LINEAR ||
	    scanout->block_height > 5)))
		return (EINVAL);

	lwkt_gettoken(&display->token);
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display primary start head=%u win=%u active=%d core=%d "
	    "paddr=0x%llx size=0x%llx pitch=%u layout=%u "
	    "kind=0x%x block_height=%u\n",
	    head, window, display->heads[head].active,
	    display->core_initialized,
	    (unsigned long long)scanout->paddr,
	    (unsigned long long)scanout->size, scanout->pitch,
	    scanout->layout, scanout->kind, scanout->block_height);
	if (!display->heads[head].active || !display->core_initialized) {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display primary rejected head=%u win=%u active=%d core=%d\n",
		    head, window, display->heads[head].active,
		    display->core_initialized);
		error = ENODEV;
		goto out;
	}
	bzero(&state, sizeof(state));
	state.image.interval = 1;
	state.image.mode = NVGPU_DISPLAY_METHOD_PRESENT_NON_TEARING;
	state.image.width = scanout->width;
	state.image.height = scanout->height;
	state.image.block_height = scanout->block_height;
	state.image.layout = scanout->layout ==
	    NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR ?
	    NVGPU_DISPLAY_METHOD_LAYOUT_BLOCK_LINEAR :
	    NVGPU_DISPLAY_METHOD_LAYOUT_PITCH;
	state.image.format = format;
	state.image.block = scanout->layout == NVGPU_DISPLAY_LAYOUT_BLOCK_LINEAR ?
	    scanout->pitch >> 6 : 0;
	state.image.pitch = scanout->layout == NVGPU_DISPLAY_LAYOUT_PITCH ?
	    scanout->pitch : 0;
	state.image.handle = NVGPU_DISPLAY_HANDLE_WINDOW(scanout->kind);
	state.image.offset = scanout->paddr;
	state.image.source_x = scanout->source_x;
	state.image.source_y = scanout->source_y;
	state.image.source_width = scanout->source_width;
	state.image.source_height = scanout->source_height;
	state.image.output_width = scanout->output_width;
	state.image.output_height = scanout->output_height;
	state.input_lut.handle = NVGPU_DISPLAY_HANDLE_VRAM;
	state.input_lut.offset = nvgsp_vram_alloc_get_paddr(display->input_lut) +
	    NVGPU_DISPLAY_INPUT_LUT_OFFSET(window);
	state.input_lut.size = NVGPU_DISPLAY_LUT_TOTAL_ENTRIES;
	state.input_lut.mode = NVGPU_DISPLAY_METHOD_LUT_DIRECT10;
	state.input_lut.output_mode =
	    NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_DISABLE;
	state.blend.depth = 255;
	state.blend.k1 = 255;
	state.blend.source_color = NVGPU_DISPLAY_METHOD_BLEND_K1;
	state.blend.destination_color = NVGPU_DISPLAY_METHOD_BLEND_NEG_K1;
	notifier_offset = display->window_state[window].next_notifier_offset;
	display->window_state[window].next_notifier_offset ^= 0x10u;
	state.notifier.handle = NVGPU_DISPLAY_HANDLE_SYNC;
	state.notifier.mode = 0;
	state.notifier.offset = notifier_offset;
	for (poll = 0; poll < 4; poll++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    notifier_offset + poll * 4u, 0);
	error = nvgpu_display_method_set_window_notifier(
	    &display->windows[window].push, &state);
	if (error == 0)
		error = nvgpu_display_method_set_window_image(
		    &display->windows[window].push, &state);
	if (error == 0 && !display->window_state[window].color_active)
		error = nvgpu_display_method_set_window_input_lut(
		    &display->windows[window].push, &state);
	if (error == 0 && !display->window_state[window].color_active)
		error = nvgpu_display_method_clear_window_csc(
		    &display->windows[window].push);
	if (error == 0 && !display->window_state[window].color_active)
		error = nvgpu_display_method_set_window_blend(
		    &display->windows[window].push, &state);
	if (error != 0)
		goto out;

	if (display->atomic.active) {
		display->atomic.window_mask |= 1u << window;
		display->atomic.window_notifier_mask |= 1u << window;
		display->atomic.window_notifier[window] = notifier_offset;
		display->window_state[window].active = true;
		display->window_state[window].color_active = true;
#ifdef KTR
		if (__predict_false(ktr_nvgpu_enable &
		    ktr_nvgpu_display_window_stage_mask)) {
			uint32_t get = nvgsp_disp_channel_read_user(
			    display->windows[window].backend, 4) >> 2;

			KTR_LOG(nvgpu_display_window_stage, window, notifier_offset,
			    1u, display->windows[window].push.cur,
			    display->windows[window].push.put, get);
		}
#endif
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display primary staged head=%u win=%u notifier=0x%x\n",
		    head, window, notifier_offset);
		goto out;
	}
	interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW] = 1u << window;
	error = nvgpu_display_method_update_window(
	    &display->windows[window].push, window, interlock);
	if (error != 0)
		goto out;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display primary window update submitted head=%u win=%u "
	    "notifier=0x%x\n", head, window, notifier_offset);
	display->window_state[window].active = true;
	display->window_state[window].color_active = true;
	for (poll = 0; poll < NVGPU_DISPLAY_WINDOW_NOTIFIER_POLLS; poll++) {
		status = nvgsp_vram_alloc_read32(gpu, display->sync,
		    notifier_offset);
		if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
		    NVGPU_DISPLAY_NOTIFIER_BEGUN)
			break;
		DELAY(NVGPU_DISPLAY_WINDOW_NOTIFIER_DELAY_US);
	}
	if (poll == NVGPU_DISPLAY_WINDOW_NOTIFIER_POLLS) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "display primary notifier timeout head=%u win=%u value=0x%08x\n",
		    head, window, status);
		error = ETIMEDOUT;
	} else {
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display primary notifier begun head=%u win=%u "
		    "value=0x%08x polls=%u\n",
		    head, window, status, poll);
	}

out:
	if (error != 0)
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "display primary exit error head=%u win=%u error=%d\n",
		    head, window, error);
	lwkt_reltoken(&display->token);
	return (error);
}

/* Publish a connector-only HEAD update without disturbing the active window. */
int
nvgpu_display_update_head(struct nvgpu_device *gpu, uint32_t head,
    const struct nvgpu_display_head_config *config, bool update_view,
    bool update_dither)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head_state state;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	int error = 0;

	if (display == NULL || config == NULL || head >= display->head_count)
		return (EINVAL);
	if (!update_view && !update_dither)
		return (0);

	lwkt_gettoken(&display->token);
	if (!display->heads[head].active || !display->core_initialized) {
		error = ENODEV;
		goto out;
	}
	bzero(&state, sizeof(state));
	state.view.input_width = config->input_width;
	state.view.input_height = config->input_height;
	state.view.output_width = config->output_width;
	state.view.output_height = config->output_height;
	state.dither.enable = config->dither_enabled;
	state.dither.bits = config->dither_bits == 8 ? 1 : 0;
	state.dither.mode = config->dither_mode;
	nvgpu_log(NVGPU_LOG_DEBUG,
	    "display head update head=%u view=%d dither=%d "
	    "input=%ux%u output=%ux%u\n",
	    head, update_view, update_dither, state.view.input_width,
	    state.view.input_height, state.view.output_width,
	    state.view.output_height);
	if (update_view)
		error = nvgpu_display_method_set_head_view(&display->core.push,
		    head, display->window_count, &state);
	if (error == 0 && update_dither)
		error = nvgpu_display_method_set_head_dither(&display->core.push,
		    head, &state);
	if (error == 0 && display->atomic.active) {
		display->atomic.core_changed = true;
		goto out;
	}
	if (error == 0) {
		interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, false, 0);
	}
out:
	lwkt_reltoken(&display->token);
	return (error);
}

/* Program atomic CRTC color state after the active image is established. */
int
nvgpu_display_update_color(struct nvgpu_device *gpu, uint32_t head,
    uint32_t window, const struct nvgpu_display_color_config *config)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head_state head_state;
	struct nvgpu_display_window_state window_state;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t count;
	uint32_t index;
	uint64_t input_lut_base;
	uint64_t output_lut_base;
	uint64_t offset;
	uint32_t notifier_offset;
	uint32_t poll;
	uint32_t status;
	bool window_color;
	int error = 0;

	if (display == NULL || config == NULL || head >= display->head_count ||
	    window >= display->window_count ||
	    (config->degamma_count != 0 && config->degamma_count != 256 &&
	    config->degamma_count != NVGPU_DISPLAY_COLOR_LUT_SIZE) ||
	    (config->gamma_count != 0 && config->gamma_count != 256 &&
	    config->gamma_count != NVGPU_DISPLAY_COLOR_LUT_SIZE))
		return (EINVAL);
	window_color = config->degamma_count != 0 || config->ctm_enabled;
	input_lut_base = NVGPU_DISPLAY_INPUT_LUT_OFFSET(window);
	output_lut_base = NVGPU_DISPLAY_OUTPUT_LUT_OFFSET(head);
	lwkt_gettoken(&display->token);
	if (!display->heads[head].active || !display->core_initialized ||
	    !display->window_state[window].active) {
		error = ENODEV;
		goto out;
	}

	bzero(&head_state, sizeof(head_state));
	count = config->gamma_count;
	if (count == 256) {
		for (index = 0; index < 256; index++) {
			uint32_t step;
			int32_t red = config->gamma[index].red;
			int32_t green = config->gamma[index].green;
			int32_t blue = config->gamma[index].blue;
			int32_t red_inc = 0;
			int32_t green_inc = 0;
			int32_t blue_inc = 0;

			if (index + 1 < 256) {
				red_inc = ((int32_t)config->gamma[index + 1].red - red) / 4;
				green_inc = ((int32_t)config->gamma[index + 1].green - green) / 4;
				blue_inc = ((int32_t)config->gamma[index + 1].blue - blue) / 4;
			}
			for (step = 0; step < 4; step++) {
				offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES + index * 4u +
				    step) * 8u;
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_lut_base + offset,
				    (uint16_t)(red + red_inc * step) |
				    ((uint32_t)(uint16_t)(green + green_inc * step) << 16));
				nvgsp_vram_alloc_write32(gpu, display->output_lut,
				    output_lut_base + offset + 4,
				    (uint16_t)(blue + blue_inc * step));
			}
		}
	} else {
		count = count != 0 ? count : NVGPU_DISPLAY_COLOR_LUT_SIZE;
		for (index = 0; index < count; index++) {
			uint16_t red;
			uint16_t green;
			uint16_t blue;

			if (config->gamma_count != 0) {
				red = config->gamma[index].red;
				green = config->gamma[index].green;
				blue = config->gamma[index].blue;
			} else {
				red = green = blue = (uint16_t)((index << 16) >> 10);
			}
			offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES + index) * 8u;
			nvgsp_vram_alloc_write32(gpu, display->output_lut,
			    output_lut_base + offset,
			    red | ((uint32_t)green << 16));
			nvgsp_vram_alloc_write32(gpu, display->output_lut,
			    output_lut_base + offset + 4, blue);
		}
	}
	offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES +
	    NVGPU_DISPLAY_COLOR_LUT_SIZE) * 8u;
	nvgsp_vram_alloc_write32(gpu, display->output_lut,
	    output_lut_base + offset, nvgsp_vram_alloc_read32(gpu,
	    display->output_lut, output_lut_base + offset - 8u));
	nvgsp_vram_alloc_write32(gpu, display->output_lut,
	    output_lut_base + offset + 4, nvgsp_vram_alloc_read32(gpu,
	    display->output_lut, output_lut_base + offset - 4u));
	head_state.output_lut.handle = NVGPU_DISPLAY_HANDLE_VRAM;
	head_state.output_lut.offset =
	    nvgsp_vram_alloc_get_paddr(display->output_lut) + output_lut_base;
	head_state.output_lut.mode = NVGPU_DISPLAY_METHOD_LUT_DIRECT10;
	head_state.output_lut.size = NVGPU_DISPLAY_LUT_TOTAL_ENTRIES;
	head_state.output_lut.output_mode =
	    NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_ENABLE;
	error = nvgpu_display_method_set_head_output_lut(&display->core.push,
	    head, &head_state);
	if (error != 0)
		goto out;
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;

	if (window_color) {
		bzero(&window_state, sizeof(window_state));
		if (display->atomic.active) {
			notifier_offset = display->window_state[window].next_notifier_offset;
			display->window_state[window].next_notifier_offset ^= 0x10u;
			window_state.notifier.handle = NVGPU_DISPLAY_HANDLE_SYNC;
			window_state.notifier.offset = notifier_offset;
			for (index = 0; index < 4; index++)
				nvgsp_vram_alloc_write32(gpu, display->sync,
				    notifier_offset + index * 4u, 0);
			error = nvgpu_display_method_set_window_notifier(
			    &display->windows[window].push, &window_state);
		}
		count = config->degamma_count != 0 ? config->degamma_count :
		    NVGPU_DISPLAY_COLOR_LUT_SIZE;
		for (index = 0; index < count; index++) {
			uint16_t red;
			uint16_t green;
			uint16_t blue;
			uint16_t values[3];
			int exponent;
			int component;

			if (config->degamma_count != 0) {
				red = config->degamma[index].red;
				green = config->degamma[index].green;
				blue = config->degamma[index].blue;
			} else {
				red = green = blue = (uint16_t)((index << 16) >> 10);
			}
			values[0] = red;
			values[1] = green;
			values[2] = blue;
			for (component = 0; component < 3; component++) {
				uint16_t fixed = values[component];
				uint16_t mantissa = 0;

				exponent = 0;
				if (fixed != 0) {
					while (--exponent != 0 && (fixed & 0x8000u) == 0)
						fixed <<= 1;
					mantissa = ((fixed << 1) & 0xffc0u) >> 6;
					exponent += 15;
				}
				values[component] = (uint16_t)((exponent << 10) | mantissa);
			}
			offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES + index) * 8u;
			nvgsp_vram_alloc_write32(gpu, display->input_lut,
			    input_lut_base + offset,
			    values[0] | ((uint32_t)values[1] << 16));
			nvgsp_vram_alloc_write32(gpu, display->input_lut,
			    input_lut_base + offset + 4,
			    values[2]);
		}
		offset = (NVGPU_DISPLAY_LUT_PREFIX_ENTRIES + count) * 8u;
		nvgsp_vram_alloc_write32(gpu, display->input_lut,
		    input_lut_base + offset, nvgsp_vram_alloc_read32(gpu,
		    display->input_lut, input_lut_base + offset - 8u));
		nvgsp_vram_alloc_write32(gpu, display->input_lut,
		    input_lut_base + offset + 4, nvgsp_vram_alloc_read32(gpu,
		    display->input_lut, input_lut_base + offset - 4u));
		window_state.input_lut.handle = NVGPU_DISPLAY_HANDLE_VRAM;
		window_state.input_lut.offset =
		    nvgsp_vram_alloc_get_paddr(display->input_lut) + input_lut_base;
		window_state.input_lut.size = NVGPU_DISPLAY_LUT_PREFIX_ENTRIES +
		    count + 1u;
		window_state.input_lut.mode = count == 256 ?
		    NVGPU_DISPLAY_METHOD_LUT_DIRECT8 :
		    NVGPU_DISPLAY_METHOD_LUT_DIRECT10;
		window_state.input_lut.output_mode =
		    NVGPU_DISPLAY_METHOD_LUT_INTERPOLATE_DISABLE;
		error = nvgpu_display_method_set_window_input_lut(
		    &display->windows[window].push, &window_state);
		if (error == 0 && config->ctm_enabled) {
			uint32_t column;
			uint32_t row;

			for (row = 0; row < 3; row++) {
				for (column = 0; column < 4; column++) {
					uint64_t value;
					uint32_t integer;
					uint32_t fraction;
					uint32_t coefficient;
					bool negative;

					if (column == 3) {
						window_state.csc.matrix[row * 4 + column] = 0;
						continue;
					}
					value = config->ctm[row * 3 + column];
					negative = (value & (1ULL << 63)) != 0;
					integer = (value >> 32) & 0x7fffffffu;
					fraction = value;
					if (integer >= 4)
						coefficient = (1u << 18) - (negative ? 0u : 1u);
					else {
						coefficient = (integer << 16) | (fraction >> 16);
						if (negative)
							coefficient = -((int32_t)coefficient);
						coefficient &= 0x7ffffu;
					}
					window_state.csc.matrix[row * 4 + column] = coefficient;
				}
			}
			error = nvgpu_display_method_set_window_csc(
			    &display->windows[window].push, &window_state);
		} else if (error == 0) {
			error = nvgpu_display_method_clear_window_csc(
			    &display->windows[window].push);
		}
			if (error == 0 && !display->atomic.active) {
				interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW] = 1u << window;
				error = nvgpu_display_method_update_window(
				    &display->windows[window].push, window, interlock);
		}
	}
	if (error != 0)
		goto out;
	if (display->atomic.active) {
		display->atomic.core_changed = true;
		if (window_color) {
			display->atomic.window_mask |= 1u << window;
			display->atomic.window_notifier_mask |= 1u << window;
			display->atomic.window_notifier[window] = notifier_offset;
		}
		goto out;
	}
	if (!window_color) {
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, false, 0);
		goto out;
	}
	for (index = 0; index < 4; index++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    NVGPU_DISPLAY_CORE_NOTIFIER + index * 4u, 0);
	error = nvgpu_display_method_update_core(&display->core.push, interlock,
	    true, NVGPU_DISPLAY_CORE_NOTIFIER);
	if (error == 0) {
		for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
			status = nvgsp_vram_alloc_read32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER);
			if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
			    NVGPU_DISPLAY_NOTIFIER_FINISHED)
				break;
			DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
		}
		if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS)
			error = ETIMEDOUT;
	}
out:
	lwkt_reltoken(&display->token);
	return (error);
}

int
nvgpu_display_disable_primary(struct nvgpu_device *gpu, uint32_t window)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_window_state state;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t notifier_offset;
	uint32_t index;
	int error = 0;

	if (display == NULL || window >= display->window_count)
		return (EINVAL);
	lwkt_gettoken(&display->token);
	if (!display->window_state[window].active &&
	    !display->window_state[window].color_active)
		goto out;
	bzero(&state, sizeof(state));
	notifier_offset = display->window_state[window].next_notifier_offset;
	display->window_state[window].next_notifier_offset ^= 0x10u;
	state.notifier.handle = NVGPU_DISPLAY_HANDLE_SYNC;
	state.notifier.offset = notifier_offset;
	for (index = 0; index < 4; index++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    notifier_offset + index * 4u, 0);
	error = nvgpu_display_method_set_window_notifier(
	    &display->windows[window].push, &state);
	if (error == 0)
		error = nvgpu_display_method_clear_window_semaphore(
		    &display->windows[window].push);
	if (error == 0)
		error = nvgpu_display_method_clear_window_input_lut(
		    &display->windows[window].push);
	if (error == 0)
		error = nvgpu_display_method_clear_window_csc(
		    &display->windows[window].push);
	if (error == 0)
		error = nvgpu_display_method_clear_window_image(
		    &display->windows[window].push);
	if (error == 0 && display->atomic.active) {
		display->atomic.window_mask |= 1u << window;
		display->atomic.window_notifier_mask |= 1u << window;
		display->atomic.window_notifier[window] = notifier_offset;
		display->window_state[window].active = false;
		display->window_state[window].color_active = false;
		goto out;
	}
	if (error == 0) {
		interlock[NVGPU_DISPLAY_INTERLOCK_WINDOW] = 1u << window;
		error = nvgpu_display_method_update_window(
		    &display->windows[window].push, window, interlock);
	}
	if (error == 0) {
		display->window_state[window].active = false;
		display->window_state[window].color_active = false;
	}
out:
	lwkt_reltoken(&display->token);
	return (error);
}

int
nvgpu_display_disable(struct nvgpu_device *gpu, uint32_t head,
    uint32_t window)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head *current;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t index;
	uint32_t poll;
	uint32_t status;
	int first_error;
	int error;

	if (display == NULL || head >= display->head_count ||
	    window >= display->window_count)
		return (EINVAL);
	first_error = nvgpu_display_disable_primary(gpu, window);
	lwkt_gettoken(&display->token);
	current = &display->heads[head];
	if (!current->active && !current->route.acquired)
		goto out;
	if (current->cursor_active) {
		error = nvgpu_display_method_clear_head_cursor(
		    &display->core.push, head);
		if (error != 0 && first_error == 0)
			first_error = error;
		current->cursor_active = false;
	}
	error = nvgpu_display_method_clear_head_output_lut(&display->core.push,
	    head);
	if (error != 0 && first_error == 0)
		first_error = error;
	error = nvgpu_display_method_set_head_display_id(&display->core.push,
	    head, 0);
	if (error != 0 && first_error == 0)
		first_error = error;
	if (current->route.acquired) {
		error = nvgpu_display_method_set_sor_control(&display->core.push,
		    current->route.sor_index, 0);
		if (error != 0 && first_error == 0)
			first_error = error;
	}
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
	for (index = 0; index < 4; index++)
		nvgsp_vram_alloc_write32(gpu, display->sync,
		    NVGPU_DISPLAY_CORE_NOTIFIER + index * 4u, 0);
	error = nvgpu_display_method_update_core(&display->core.push, interlock,
	    true, NVGPU_DISPLAY_CORE_NOTIFIER);
	if (error != 0 && first_error == 0)
		first_error = error;
	if (error == 0) {
		for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
			status = nvgsp_vram_alloc_read32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER);
			if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
			    NVGPU_DISPLAY_NOTIFIER_FINISHED)
				break;
			DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
		}
		if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS && first_error == 0)
			first_error = ETIMEDOUT;
	}
	if (current->route.acquired) {
		if (current->route.audio) {
			(void)nvgsp_disp_set_eld(gpu, &current->route, head, NULL, 0);
			(void)nvgsp_disp_set_audio(gpu, &current->route, head, false);
		}
		if (current->route.protocol == NVGSP_DISPLAY_PROTOCOL_TMDS)
			nvgsp_disp_disable_hdmi(gpu, &current->route);
		nvgsp_disp_release_output(gpu, &current->route);
	}
	current->active = false;
	current->display_id = 0;
	current->dp_lane_count = 0;
	current->dp_link_bandwidth = 0;
	current->dp_mst = false;
	display->assign_windows = true;
out:
	lwkt_reltoken(&display->token);
	return (first_error);
}

int
nvgpu_display_update_cursor(struct nvgpu_device *gpu, uint32_t head,
    const struct nvgpu_display_cursor *cursor, bool legacy_update)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head_state state;
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t index;
	uint32_t poll;
	uint32_t status;
	int error;

	if (display == NULL || cursor == NULL || head >= display->head_count ||
	    head >= display->cursor_count || cursor->paddr == 0 ||
	    (cursor->paddr & 0xffu) != 0 || cursor->width != cursor->height ||
	    (cursor->width != 32 && cursor->width != 64 && cursor->width != 128 &&
	    cursor->width != 256))
		return (EINVAL);
	lwkt_gettoken(&display->token);
	if (!display->heads[head].active) {
		error = ENODEV;
		goto out;
	}
	bzero(&state, sizeof(state));
	state.cursor.handle = NVGPU_DISPLAY_HANDLE_VRAM;
	state.cursor.offset = cursor->paddr;
	state.cursor.format = NVGPU_DISPLAY_METHOD_CURSOR_A8R8G8B8;
	state.cursor.layout = cursor->width == 32 ? NVGPU_DISPLAY_METHOD_CURSOR_32 :
	    cursor->width == 64 ? NVGPU_DISPLAY_METHOD_CURSOR_64 :
	    cursor->width == 128 ? NVGPU_DISPLAY_METHOD_CURSOR_128 :
	    NVGPU_DISPLAY_METHOD_CURSOR_256;
	error = nvgpu_display_method_set_head_cursor(&display->core.push, head,
	    &state);
	if (error != 0)
		goto out;
	if (display->atomic.active) {
		display->atomic.core_changed = true;
		display->atomic.cursor_mask |= 1u << head;
		display->atomic.cursor_x[head] = cursor->x;
		display->atomic.cursor_y[head] = cursor->y;
		display->heads[head].cursor_active = true;
		goto out;
	}
	nvgpu_display_method_set_cursor_point(display->cursors[head].backend,
	    cursor->x, cursor->y);
	nvgpu_display_method_update_cursor(display->cursors[head].backend);
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
	if (legacy_update) {
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, false, 0);
	} else {
		for (index = 0; index < 4; index++)
			nvgsp_vram_alloc_write32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER + index * 4u, 0);
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, true, NVGPU_DISPLAY_CORE_NOTIFIER);
		if (error == 0) {
			for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
				status = nvgsp_vram_alloc_read32(gpu, display->sync,
				    NVGPU_DISPLAY_CORE_NOTIFIER);
				if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
				    NVGPU_DISPLAY_NOTIFIER_FINISHED)
					break;
				DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
			}
			if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS)
				error = ETIMEDOUT;
		}
	}
	if (error == 0)
		display->heads[head].cursor_active = true;
out:
	lwkt_reltoken(&display->token);
	return (error);
}

int
nvgpu_display_recover_dp_link(struct nvgpu_device *gpu, uint32_t display_id)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_head *head_state = NULL;
	uint8_t status[6];
	uint8_t size;
	uint32_t head;
	uint32_t lane;
	bool link_ok;
	int error;

	if (display == NULL || display_id == 0)
		return (EINVAL);
	lwkt_gettoken(&display->token);
	for (head = 0; head < display->head_count; head++) {
		if (display->heads[head].active &&
		    display->heads[head].display_id == display_id &&
		    display->heads[head].route.protocol == NVGSP_DISPLAY_PROTOCOL_DP) {
			head_state = &display->heads[head];
			break;
		}
	}
	if (head_state == NULL || head_state->dp_lane_count == 0 ||
	    head_state->dp_link_bandwidth == 0) {
		error = ENOENT;
		goto out;
	}
	size = sizeof(status);
	error = nvgsp_disp_transfer_aux(gpu, display_id,
	    NVGPU_DISPLAY_DP_AUX_NATIVE_READ, 0x202, status, &size);
	if (error != NVGPU_DISPLAY_DP_AUX_REPLY_ACK || size < 3) {
		error = EIO;
		goto out;
	}
	link_ok = (status[2] & 0x01u) != 0;
	for (lane = 0; lane < head_state->dp_lane_count; lane++) {
		uint8_t lane_status = status[lane >> 1];

		lane_status >>= (lane & 1u) * 4u;
		if ((lane_status & 0x07u) != 0x07u)
			link_ok = false;
	}
	if (link_ok) {
		error = 0;
		goto out;
	}
	error = nvgsp_disp_train_dp(gpu, &head_state->route,
	    head_state->dp_lane_count, head_state->dp_link_bandwidth,
	    head_state->dp_mst);
	if (error != 0)
		goto out;
	size = sizeof(status);
	error = nvgsp_disp_transfer_aux(gpu, display_id,
	    NVGPU_DISPLAY_DP_AUX_NATIVE_READ, 0x202, status, &size);
	if (error != NVGPU_DISPLAY_DP_AUX_REPLY_ACK || size < 3) {
		error = EIO;
		goto out;
	}
	link_ok = (status[2] & 0x01u) != 0;
	for (lane = 0; lane < head_state->dp_lane_count; lane++) {
		uint8_t lane_status = status[lane >> 1];

		lane_status >>= (lane & 1u) * 4u;
		if ((lane_status & 0x07u) != 0x07u)
			link_ok = false;
	}
	error = link_ok ? 0 : EIO;
out:
	lwkt_reltoken(&display->token);
	return (error);
}

void
nvgpu_display_move_cursor(struct nvgpu_device *gpu, uint32_t head,
    int32_t x, int32_t y)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display == NULL || head >= display->cursor_count)
		return;
	lwkt_gettoken(&display->token);
	if (display->heads[head].cursor_active) {
		nvgpu_display_method_set_cursor_point(
		    display->cursors[head].backend, x, y);
		nvgpu_display_method_update_cursor(display->cursors[head].backend);
	}
	lwkt_reltoken(&display->token);
}

int
nvgpu_display_disable_cursor(struct nvgpu_device *gpu, uint32_t head,
    bool legacy_update)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	uint32_t interlock[NVGPU_DISPLAY_INTERLOCK_COUNT] = {};
	uint32_t index;
	uint32_t poll;
	uint32_t status;
	int error = 0;

	if (display == NULL || head >= display->head_count)
		return (EINVAL);
	lwkt_gettoken(&display->token);
	if (!display->heads[head].cursor_active)
		goto out;
	error = nvgpu_display_method_clear_head_cursor(&display->core.push, head);
	if (error != 0)
		goto out;
	if (display->atomic.active) {
		display->atomic.core_changed = true;
		display->heads[head].cursor_active = false;
		goto out;
	}
	interlock[NVGPU_DISPLAY_INTERLOCK_CORE] = 1;
	if (legacy_update) {
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, false, 0);
	} else {
		for (index = 0; index < 4; index++)
			nvgsp_vram_alloc_write32(gpu, display->sync,
			    NVGPU_DISPLAY_CORE_NOTIFIER + index * 4u, 0);
		error = nvgpu_display_method_update_core(&display->core.push,
		    interlock, true, NVGPU_DISPLAY_CORE_NOTIFIER);
		if (error == 0) {
			for (poll = 0; poll < NVGPU_DISPLAY_NOTIFIER_POLLS; poll++) {
				status = nvgsp_vram_alloc_read32(gpu, display->sync,
				    NVGPU_DISPLAY_CORE_NOTIFIER);
				if ((status & NVGPU_DISPLAY_NOTIFIER_STATUS_MASK) ==
				    NVGPU_DISPLAY_NOTIFIER_FINISHED)
					break;
				DELAY(NVGPU_DISPLAY_NOTIFIER_DELAY_US);
			}
			if (poll == NVGPU_DISPLAY_NOTIFIER_POLLS)
				error = ETIMEDOUT;
		}
	}
	if (error == 0)
		display->heads[head].cursor_active = false;
out:
	lwkt_reltoken(&display->token);
	return (error);
}

int
nvgpu_display_prepare_console(struct nvgpu_device *gpu, uint32_t width,
    uint32_t height, struct nvgpu_display_console *console)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgsp_vram_alloc *alloc;
	uint64_t size;
	uint32_t pitch;
	int error;

	if (display == NULL || console == NULL || width == 0 || height == 0)
		return (EINVAL);
	pitch = roundup2(width * 4u, 256u);
	size = round_page((uint64_t)pitch * height);
	if (size == 0)
		return (EOVERFLOW);

	lwkt_gettoken(&display->token);
	if (display->console != NULL && (display->console_width != width ||
	    display->console_height != height || display->console_pitch != pitch)) {
		nvgsp_vram_alloc_unmap_bar1_range(gpu, display->console);
		nvgsp_vram_free_display(gpu, display->console);
		display->console = NULL;
	}
	if (display->console == NULL) {
		alloc = nvgsp_vram_alloc_display(gpu, size, PAGE_SIZE, display);
		if (alloc == NULL) {
			error = ENOMEM;
			goto out;
		}
		error = nvgsp_vram_alloc_map_bar1_range(gpu, alloc, size);
		if (error != 0) {
			nvgsp_vram_free_display(gpu, alloc);
			goto out;
		}
		display->console = alloc;
		display->console_bar1_gva =
		    nvgsp_vram_alloc_get_bar1_range(alloc);
		display->console_size = size;
		display->console_width = width;
		display->console_height = height;
		display->console_pitch = pitch;
	}
	bzero(console, sizeof(*console));
	console->paddr = nvgsp_vram_alloc_get_paddr(display->console);
	console->size = display->console_size;
	console->bar1_gva = display->console_bar1_gva;
	console->width = display->console_width;
	console->height = display->console_height;
	console->pitch = display->console_pitch;
	error = 0;
out:
	lwkt_reltoken(&display->token);
	return (error);
}

void
nvgpu_display_release_console(struct nvgpu_device *gpu)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display == NULL)
		return;
	lwkt_gettoken(&display->token);
	if (display->console != NULL) {
		nvgsp_vram_alloc_unmap_bar1_range(gpu, display->console);
		nvgsp_vram_free_display(gpu, display->console);
		display->console = NULL;
	}
	display->console_bar1_gva = 0;
	display->console_size = 0;
	display->console_width = 0;
	display->console_height = 0;
	display->console_pitch = 0;
	lwkt_reltoken(&display->token);
}

void
nvgpu_display_set_event_ops(struct nvgpu_device *gpu,
    const struct nvgpu_display_event_ops *ops, void *arg)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display == NULL)
		return;
	lwkt_gettoken(&display->token);
	spin_lock(&display->event_lock);
	if (ops != NULL)
		display->event_ops = *ops;
	else
		bzero(&display->event_ops, sizeof(display->event_ops));
	display->event_arg = ops != NULL ? arg : NULL;
	spin_unlock(&display->event_lock);
	lwkt_reltoken(&display->token);
}

void
nvgpu_display_enable_vblank(struct nvgpu_device *gpu, uint32_t head)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display != NULL && head < display->head_count)
		nvgsp_disp_enable_vblank(gpu, head);
}

void
nvgpu_display_disable_vblank(struct nvgpu_device *gpu, uint32_t head)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);

	if (display != NULL && head < display->head_count)
		nvgsp_disp_disable_vblank(gpu, head);
}

/* Deliver physical vblank without contending with display method submission. */
void
nvgpu_display_handle_vblank(struct nvgpu_device *gpu, uint32_t head)
{
	struct nvgpu_display *display = nvgpu_device_get_display(gpu);
	struct nvgpu_display_event_ops ops;
	void *arg;

	if (display == NULL || head >= display->head_count)
		return;
	/*
	 * KMS teardown stops and joins display dispatch before it clears these
	 * callbacks.  The event lock only protects this borrowed snapshot.
	 */
	spin_lock(&display->event_lock);
	ops = display->event_ops;
	arg = display->event_arg;
	spin_unlock(&display->event_lock);
	if (ops.vblank != NULL)
		ops.vblank(arg, head);
}
