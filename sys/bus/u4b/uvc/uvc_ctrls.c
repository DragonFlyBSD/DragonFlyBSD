/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
 *	Zhichao Li <Zhichao1.Li@Dell.com>
 *	Pillar Liang <Pillar.Liang@Dellteam.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * UVC spec:https://www.usb.org/sites/default/files/USB_Video_Class_1_5.zip
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <bus/u4b/usb.h>
#define USB_DEBUG_VAR uvc_debug
#include <bus/u4b/usb_debug.h>
#include <bus/u4b/usbdi.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include "uvc_drv.h"
#include "uvc_buf.h"
#include "uvc_v4l2.h"

#define UVC_LOCK(lkp)   lockmgr(lkp, LK_EXCLUSIVE)
#define UVC_UNLOCK(lkp) lockmgr(lkp, LK_RELEASE)

/*
 * DragonFly does not implement _SAFE macros because they are generally not
 * actually safe in a MP environment, and so it is bad programming practice
 * to use them.
 */
#define STAILQ_FOREACH_SAFE(scan, list, next, save)	\
	for (scan = STAILQ_FIRST(list); (save = scan ? STAILQ_NEXT(scan, next) : NULL), scan; scan = save) 	\

#define UVC_CTRL_DATA_CURRENT	0
#define UVC_CTRL_DATA_BACKUP	1
#define UVC_CTRL_DATA_MIN	2
#define UVC_CTRL_DATA_MAX	3
#define UVC_CTRL_DATA_RES	4
#define UVC_CTRL_DATA_DEF	5
#define UVC_CTRL_DATA_LAST	6

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

#define UVC_BITMASK	      0x7
#define UVC_BITSHIFT	      0x3
#define UVC_VALMASK	      0x1

static unsigned int
uvc_test_bit(const uint8_t *buf, int b)
{
	return (buf[b >> UVC_BITSHIFT] >> (b & UVC_BITMASK)) & UVC_VALMASK;
}


static int
uvc_ctrl_init_sub_info(struct uvc_ctrl_info *info,
    struct uvc_ctrl_sub_info *sub_info)
{
	int ret = 0;

	enum v4l2_ctrl_type type = V4L2_CTRL_TYPE_INTEGER;
	uint8_t bit_offset = 0;
	uint8_t bit_size = 0;
	uint8_t name[32] = { 0 };
	uint32_t superior_id = 0;
	uint32_t superior_manual = 0;
	uint32_t inferior_ids[2] = { 0 };

	uint32_t id = sub_info->v4l2_id;

	if (info->topo_type == UVC_TOPO_TYPE_PROCESSING_UNIT) {
		switch (id) {
		case V4L2_CID_BRIGHTNESS:
			ksprintf(name, "Brightness");
			bit_size = 16;
			break;
		case V4L2_CID_CONTRAST:
			ksprintf(name, "Contrast");
			bit_size = 16;
			break;
		case V4L2_CID_HUE:
			ksprintf(name, "Hue");
			bit_size = 16;
			superior_id = V4L2_CID_HUE_AUTO;
			break;
		case V4L2_CID_SATURATION:
			ksprintf(name, "Saturation");
			bit_size = 16;
			break;
		case V4L2_CID_SHARPNESS:
			ksprintf(name, "Sharpness");
			bit_size = 16;
			break;
		case V4L2_CID_GAMMA:
			ksprintf(name, "Gamma");
			bit_size = 16;
			break;
		case V4L2_CID_BACKLIGHT_COMPENSATION:
			ksprintf(name, "Backlight Compensation");
			bit_size = 16;
			break;
		case V4L2_CID_GAIN:
			ksprintf(name, "Gain");
			bit_size = 16;
			break;
		case V4L2_CID_POWER_LINE_FREQUENCY:
			ksprintf(name, "Power Line Frequency");
			type = V4L2_CTRL_TYPE_MENU;
			bit_size = 2;
			break;
		case V4L2_CID_HUE_AUTO:
			ksprintf(name, "Hue, Auto");
			type = V4L2_CTRL_TYPE_BOOLEAN;
			bit_size = 1;
			sub_info->inferior_v4l2_ids[0] = V4L2_CID_HUE;
			break;
		case V4L2_CID_AUTO_WHITE_BALANCE:
			if (info->selector ==
			    UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL) {
				ksprintf(name, "White Balance Temperature, Auto");
				type = V4L2_CTRL_TYPE_BOOLEAN;
				bit_size = 1;
				inferior_ids[0] =
				    V4L2_CID_WHITE_BALANCE_TEMPERATURE;
			} else if (info->selector ==
			    UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL) {
				ksprintf(name, "White Balance Component, Auto");
				type = V4L2_CTRL_TYPE_BOOLEAN;
				bit_size = 1;
				inferior_ids[0] = V4L2_CID_BLUE_BALANCE;
				inferior_ids[1] = V4L2_CID_RED_BALANCE;
			} else {
				ret = EINVAL;
			}
			break;
		case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
			ksprintf(name, "White Balance Temperature");
			bit_size = 16;
			superior_id = V4L2_CID_AUTO_WHITE_BALANCE;
			break;
		case V4L2_CID_BLUE_BALANCE:
			ksprintf(name, "White Balance Blue Component");
			bit_size = 16;
			superior_id = V4L2_CID_AUTO_WHITE_BALANCE;
			break;
		case V4L2_CID_RED_BALANCE:
			ksprintf(name, "White Balance Red Component");
			bit_offset = 16;
			bit_size = 16;
			superior_id = V4L2_CID_AUTO_WHITE_BALANCE;
			break;
		default:
			ret = EINVAL;
			goto err_done;
			break;
		}
	} else if (info->topo_type == UVC_TOPO_TYPE_CAMERA_TERMINAL) {
		switch (id) {
		case V4L2_CID_EXPOSURE_AUTO:
			ksprintf(name, "Exposure, Auto");
			type = V4L2_CTRL_TYPE_MENU;
			bit_size = 4;
			inferior_ids[0] = V4L2_CID_EXPOSURE_ABSOLUTE;
			break;
		case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
			ksprintf(name, "Exposure, Auto Priority");
			type = V4L2_CTRL_TYPE_BOOLEAN;
			bit_size = 1;
			break;
		case V4L2_CID_EXPOSURE_ABSOLUTE:
			ksprintf(name, "Exposure (Absolute)");
			bit_size = 32;
			superior_id = V4L2_CID_EXPOSURE_AUTO;
			superior_manual = V4L2_EXPOSURE_MANUAL;
			break;
		case V4L2_CID_FOCUS_ABSOLUTE:
			ksprintf(name, "Focus (absolute)");
			bit_size = 16;
			superior_id = V4L2_CID_FOCUS_AUTO;
			break;
		case V4L2_CID_FOCUS_AUTO:
			ksprintf(name, "Focus, Auto");
			type = V4L2_CTRL_TYPE_BOOLEAN;
			bit_size = 1;
			inferior_ids[0] = V4L2_CID_FOCUS_ABSOLUTE;
			break;
		case V4L2_CID_IRIS_ABSOLUTE:
			ksprintf(name, "Iris, Absolute");
			bit_size = 16;
			break;
		case V4L2_CID_IRIS_RELATIVE:
			ksprintf(name, "Iris, Relative");
			bit_size = 8;
			break;
		case V4L2_CID_ZOOM_ABSOLUTE:
			ksprintf(name, "Zoom, Absolute");
			bit_size = 16;
			break;
		case V4L2_CID_ZOOM_CONTINUOUS:
			ksprintf(name, "Zoom, Continuous");
			bit_size = 0; // should use get/set
			break;
		case V4L2_CID_PAN_ABSOLUTE:
			ksprintf(name, "Pan (Absolute)");
			bit_size = 32;
			break;
		case V4L2_CID_TILT_ABSOLUTE:
			ksprintf(name, "Tilt (Absolute)");
			bit_offset = 32;
			bit_size = 32;
			break;
		case V4L2_CID_PAN_SPEED:
			ksprintf(name, "Pan (Speed)");
			bit_size = 16;
			break;
		case V4L2_CID_TILT_SPEED:
			ksprintf(name, "Tilt (Speed)");
			bit_offset = 16;
			bit_size = 16;
			break;
		case V4L2_CID_PRIVACY:
			ksprintf(name, "Privacy");
			type = V4L2_CTRL_TYPE_BOOLEAN;
			bit_size = 1;
			break;
		default:
			ret = EINVAL;
			goto err_done;
			break;
		}
	}

	strcpy(sub_info->v4l2_name, name);
	sub_info->v4l2_type = type;

	sub_info->bit_offset = bit_offset;
	sub_info->bit_size = bit_size;

	sub_info->superior_v4l2_id = superior_id;
	sub_info->superior_manual = superior_manual;
	sub_info->inferior_v4l2_ids[0] = inferior_ids[0];
	sub_info->inferior_v4l2_ids[1] = inferior_ids[1];

	return 0;

err_done:
	kprintf("%s:%d Unknown topo 0x%x, selector 0x%x, v4l2 id 0x%x\n",
			 __func__, __LINE__, info->topo_type, info->selector, id);

	return ret;
}

static int
uvc_ctrl_get_info_ct(uint8_t bit_idx, struct uvc_ctrl_info *info)
{
	int ret = 0;

	memset(info, 0, sizeof(*info));

	info->topo_type = UVC_TOPO_TYPE_CAMERA_TERMINAL;
	info->index = bit_idx;

	// Table 3-6 Camera Terminal Descriptor, bitmap bmControls
	switch (bit_idx) {
	case 0: // Table 4-9
		info->selector = UVC_CT_SCANNING_MODE_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_RESTORE;
		break;
	case 1: // Table 4-10
		info->selector = UVC_CT_AE_MODE_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_GET_RES | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BITMASK;
		break;
	case 2: // Table 4-11
		info->selector = UVC_CT_AE_PRIORITY_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	case 3: // Table 4-12
		info->selector = UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL;
		info->byte_size = 4;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 4: // Table 4-13
		info->selector = UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_RESTORE;
		break;
	case 5: // Table 4-14
		info->selector = UVC_CT_FOCUS_ABSOLUTE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 6: // Table 4-15
		info->selector = UVC_CT_FOCUS_RELATIVE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_MIN |
		    UVC_CTRL_GET_MAX | UVC_CTRL_GET_RES | UVC_CTRL_GET_DEF |
		    UVC_CTRL_AUTO_UPDATE;
		break;
	case 7: // Table 4-18
		info->selector = UVC_CT_IRIS_ABSOLUTE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 8: // Table 4-19
		info->selector = UVC_CT_IRIS_RELATIVE_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 9: // Table 4-20
		info->selector = UVC_CT_ZOOM_ABSOLUTE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 10: // Table 4-21
		info->selector = UVC_CT_ZOOM_RELATIVE_CONTROL;
		info->byte_size = 3;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_MIN |
		    UVC_CTRL_GET_MAX | UVC_CTRL_GET_RES | UVC_CTRL_GET_DEF |
		    UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 11: // Table 4-22
		info->selector = UVC_CT_PANTILT_ABSOLUTE_CONTROL;
		info->byte_size = 8;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 12: // Table 4-23
		info->selector = UVC_CT_PANTILT_RELATIVE_CONTROL;
		info->byte_size = 4;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 13: // Table 4-24
		info->selector = UVC_CT_ROLL_ABSOLUTE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		break;
	case 14: // Table 4-25
		info->selector = UVC_CT_ROLL_RELATIVE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_MIN |
		    UVC_CTRL_GET_MAX | UVC_CTRL_GET_RES | UVC_CTRL_GET_DEF |
		    UVC_CTRL_AUTO_UPDATE;
		break;
	case 17: // Table 4-17
		info->selector = UVC_CT_FOCUS_AUTO_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	case 18: // Table 4-26
		info->selector = UVC_CT_PRIVACY_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	default: // Others to be implemented.
		ret = EINVAL;
		break;
	}

	if (ret != 0) {
		memset(info, 0, sizeof(*info));
	}
	return ret;
}

static int
uvc_ctrl_get_info_pu(uint8_t bit_idx, struct uvc_ctrl_info *info)
{
	int ret = 0;

	memset(info, 0, sizeof(*info));

	info->topo_type = UVC_TOPO_TYPE_PROCESSING_UNIT;
	info->index = bit_idx;

	// Table 3-8 Processing Unit Descriptor, bitmap bmControls
	switch (bit_idx) {
	case 0: // Table 4-31
		info->selector = UVC_PU_BRIGHTNESS_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 1: // Table 4-32
		info->selector = UVC_PU_CONTRAST_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 2: // Table 4-36
		info->selector = UVC_PU_HUE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;
		break;
	case 3: // Table 4-38
		info->selector = UVC_PU_SATURATION_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 4: // Table 4-39
		info->selector = UVC_PU_SHARPNESS_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 5: // Table 4-40
		info->selector = UVC_PU_GAMMA_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 6: // Table 4-41
		info->selector = UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 7: // Table 4-43
		info->selector = UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL;
		info->byte_size = 4;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE | UVC_CTRL_AUTO_UPDATE;
		info->data_type = UVC_CTRL_DATA_SIGNED;

		break;
	case 8: // Table 4-30
		info->selector = UVC_PU_BACKLIGHT_COMPENSATION_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 9: // Table 4-34
		info->selector = UVC_PU_GAIN_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_UNSIGNED;
		break;
	case 10: // Table 4-35
		info->selector = UVC_PU_POWER_LINE_FREQUENCY_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_ENUM;
		break;
	case 11: // Table 4-37
		info->selector = UVC_PU_HUE_AUTO_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	case 12: // Table 4-42
		info->selector = UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	case 13: // Table 4-44
		info->selector = UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_CUR |
		    UVC_CTRL_GET_DEF | UVC_CTRL_RESTORE;
		info->data_type = UVC_CTRL_DATA_BOOLEAN;
		break;
	case 14: // Table 4-45
		info->selector = UVC_PU_DIGITAL_MULTIPLIER_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		break;
	case 15: // Table 4-46
		info->selector = UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL;
		info->byte_size = 2;
		info->flags = UVC_CTRL_SET_CUR | UVC_CTRL_GET_RANGE |
		    UVC_CTRL_RESTORE;
		break;
	case 16: // Table 4-47
		info->selector = UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_GET_CUR;
		break;
	case 17: // Table 4-48
		info->selector = UVC_PU_ANALOG_LOCK_STATUS_CONTROL;
		info->byte_size = 1;
		info->flags = UVC_CTRL_GET_CUR;
		break;
	default:
		ret = EINVAL;
		break;
	}

	if (ret != 0) {
		memset(info, 0, sizeof(*info));
	}
	return ret;
}

static const struct uvc_ctrl_menu_item exposure_mode_menu[] = {
	{ 2, "Auto Mode" },
	{ 1, "Manual Mode" },
	{ 4, "Shutter Priority Mode" },
	{ 8, "Aperture Priority Mode" },
};

static const struct uvc_ctrl_menu_item power_line_freq_menu[] = {
	{ 0, "Disabled" },
	{ 1, "50 Hz" },
	{ 2, "60 Hz" },
};

static int
uvc_ctrl_init_menu(struct uvc_ctrl_info *info)
{
	const struct uvc_ctrl_menu_item *src_menu_data = NULL;
	struct uvc_ctrl_menu_item *dst_menu_data = NULL;
	uint32_t item_num = 0;
	uint32_t len = 0;

	if (info->topo_type == UVC_TOPO_TYPE_CAMERA_TERMINAL &&
	    info->selector == UVC_CT_AE_MODE_CONTROL) {
		src_menu_data = exposure_mode_menu;
		item_num = ARRAY_SIZE(exposure_mode_menu);

	} else if (info->topo_type == UVC_TOPO_TYPE_PROCESSING_UNIT &&
	    info->selector == UVC_PU_POWER_LINE_FREQUENCY_CONTROL) {
		src_menu_data = power_line_freq_menu;
		item_num = ARRAY_SIZE(power_line_freq_menu);
	} else {
		return 0;
	}

	len = item_num * sizeof(struct uvc_ctrl_menu_item);
	dst_menu_data = kmalloc(len, M_UVC, M_ZERO | M_WAITOK);

	if (dst_menu_data == NULL) {
		kprintf("%s:%d fail to alloc mem\n", __func__, __LINE__);
		return ENOMEM;
	}

	memcpy(dst_menu_data, src_menu_data, len);

	info->menus[0].item_num = item_num;
	info->menus[0].menu_data = dst_menu_data;

	return 0;
}

static void
uvc_ctrl_init_sub_info_num(struct uvc_ctrl_info *info)
{
	info->sub_info_num = 1;

	if (info->topo_type == UVC_TOPO_TYPE_CAMERA_TERMINAL) {
		switch (info->selector) {
		case UVC_CT_PANTILT_ABSOLUTE_CONTROL:
		case UVC_CT_PANTILT_RELATIVE_CONTROL:
			info->sub_info_num = 2;
			break;
		}
	} else if (info->topo_type == UVC_TOPO_TYPE_PROCESSING_UNIT) {
		switch (info->selector) {
		case UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL:
			info->sub_info_num = 2;
			break;
		}
	}
}

static int
uvc_ctrl_build_mapping_ct(struct uvc_ctrl_info *info)
{
	int ret = 0;

	switch (info->selector) {
	case UVC_CT_AE_MODE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_EXPOSURE_AUTO;
		break;
	case UVC_CT_AE_PRIORITY_CONTROL:
		info->sub_infos[0]->v4l2_id =
		    V4L2_CID_EXPOSURE_AUTO_PRIORITY;
		break;
	case UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_EXPOSURE_ABSOLUTE;
		break;
	case UVC_CT_FOCUS_ABSOLUTE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_FOCUS_ABSOLUTE;
		break;
	case UVC_CT_FOCUS_AUTO_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_FOCUS_AUTO;
		break;
	case UVC_CT_IRIS_ABSOLUTE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_IRIS_ABSOLUTE;
		break;
	case UVC_CT_IRIS_RELATIVE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_IRIS_RELATIVE;
		break;
	case UVC_CT_ZOOM_ABSOLUTE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_ZOOM_ABSOLUTE;
		break;
	case UVC_CT_ZOOM_RELATIVE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_ZOOM_CONTINUOUS;
		break;
	case UVC_CT_PANTILT_ABSOLUTE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_PAN_ABSOLUTE;
		info->sub_infos[1]->v4l2_id = V4L2_CID_TILT_ABSOLUTE;
		break;
	case UVC_CT_PANTILT_RELATIVE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_PAN_SPEED;
		info->sub_infos[1]->v4l2_id = V4L2_CID_TILT_SPEED;
		break;
	case UVC_CT_PRIVACY_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_PRIVACY;
		break;
	default:
		ret = EINVAL;
		break;
	}

	return ret;
}

static int
uvc_ctrl_build_mapping_pu(struct uvc_ctrl_info *info)
{
	int ret = 0;

	switch (info->selector) {
	case UVC_PU_BRIGHTNESS_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_BRIGHTNESS;
		break;
	case UVC_PU_CONTRAST_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_CONTRAST;
		break;
	case UVC_PU_HUE_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_HUE;
		break;
	case UVC_PU_SATURATION_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_SATURATION;
		break;
	case UVC_PU_SHARPNESS_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_SHARPNESS;
		break;
	case UVC_PU_GAMMA_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_GAMMA;
		break;
	case UVC_PU_BACKLIGHT_COMPENSATION_CONTROL:
		info->sub_infos[0]->v4l2_id =
		    V4L2_CID_BACKLIGHT_COMPENSATION;
		break;
	case UVC_PU_GAIN_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_GAIN;
		break;
	case UVC_PU_POWER_LINE_FREQUENCY_CONTROL:
		info->sub_infos[0]->v4l2_id =
		    V4L2_CID_POWER_LINE_FREQUENCY;
		break;
	case UVC_PU_HUE_AUTO_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_HUE_AUTO;
		break;
	case UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_AUTO_WHITE_BALANCE;
		break;
	case UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
		info->sub_infos[0]->v4l2_id =
		    V4L2_CID_WHITE_BALANCE_TEMPERATURE;
		break;
	case UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_AUTO_WHITE_BALANCE;
		break;
	case UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL:
		info->sub_infos[0]->v4l2_id = V4L2_CID_BLUE_BALANCE;
		info->sub_infos[1]->v4l2_id = V4L2_CID_RED_BALANCE;
		break;
	default:
		ret = EINVAL;
	}

	return ret;
}

static int
uvc_ctrl_build_mapping(struct uvc_ctrl_info *info)
{
	int ret = 0;
	uint8_t i = 0;

	enum uvc_topo_type topo_type = info->topo_type;

	uint8_t sub_info_num = info->sub_info_num;
	struct uvc_ctrl_sub_info *sub_info = NULL;

	if (sub_info_num == 0) {
		return 0;
	}

	for (i = 0; i < sub_info_num; i++) {
		sub_info = kmalloc(sizeof(*sub_info), M_UVC, M_ZERO | M_WAITOK);

		if (sub_info == NULL) {
			kprintf("%s:%d fail to alloc mem\n", __func__, __LINE__);
			ret = ENOMEM;
			goto err_done;
		}

		sub_info->uvc_info = info;
		info->sub_infos[i] = sub_info;
	}

	if (topo_type == UVC_TOPO_TYPE_CAMERA_TERMINAL) {
		ret = uvc_ctrl_build_mapping_ct(info);
	} else if (topo_type == UVC_TOPO_TYPE_PROCESSING_UNIT) {
		ret = uvc_ctrl_build_mapping_pu(info);
	} else {
		ret = EINVAL;
	}

	if (ret != 0) {
		goto err_done;
	}

	for (int i = 0; i < sub_info_num; i++) {
		uvc_ctrl_init_sub_info(info, info->sub_infos[i]);
	}

	return 0;

err_done:
	for (i = 0; i < MAX_MAPPING_NUM; i++) {
		if (info->sub_infos[i] != NULL) {
			kfree(info->sub_infos[i], M_UVC);
			info->sub_infos[i] = NULL;
		}
	}

	return ret;
}

static int
uvc_ctrl_initialize_control(struct uvc_control *ctrl)
{
	int ret = 0;

	uint8_t bit_idx = ctrl->index;
	uint16_t node_type = UVC_ENT_TYPE(ctrl->topo_node);

	struct uvc_ctrl_info tmp_info;

	switch (node_type) {
	case UVC_ITT_CAMERA:
		ret = uvc_ctrl_get_info_ct(bit_idx, &tmp_info);
		if (ret != 0) {
			return EINVAL;
		}
		break;
	case UDESCSUB_VC_PROCESSING_UNIT:
		ret = uvc_ctrl_get_info_pu(bit_idx, &tmp_info);
		if (ret != 0) {
			return EINVAL;
		}
		break;
	default:
		return 0;
	}

	ctrl->uvc_data = kmalloc(tmp_info.byte_size * UVC_CTRL_DATA_LAST + 1, M_UVC,
	    M_ZERO | M_WAITOK);

	if (ctrl->uvc_data == NULL) {
		kprintf("%s:%d fail to allocate memory\n", __func__, __LINE__);
		ret = ENOMEM;
		goto err;
	}

	uvc_ctrl_init_sub_info_num(&tmp_info);

	ret = uvc_ctrl_build_mapping(&tmp_info);
	if (0 != ret) {
		goto err;
	}

	ret = uvc_ctrl_init_menu(&tmp_info);
	if (0 != ret) {
		goto err;
	}

	ctrl->info = tmp_info;
	ctrl->initialized = 1;

	return 0;

err:
	if (ctrl->uvc_data != NULL) {
		kfree(ctrl->uvc_data, M_UVC);
		ctrl->uvc_data = NULL;
	}

	return ret;
}

void
uvc_ctrl_destroy_mappings(struct uvc_control *ctrl)
{
	uint8_t i = 0;

	if (!ctrl)
		return;

	for (i = 0; i < MAX_MAPPING_NUM; i++) {
		if (ctrl->info.sub_infos[i] != NULL) {
			DPRINTF("removing v4l2 mapping '%s' \n",
				ctrl->info.sub_infos[i]->v4l2_name);

			kfree(ctrl->info.sub_infos[i], M_UVC);
			ctrl->info.sub_infos[i] = NULL;
		}

		if (ctrl->info.menus[i].menu_data != NULL) {
			kfree(ctrl->info.menus[i].menu_data, M_UVC);
			ctrl->info.menus[i].menu_data = NULL;
		}
	}

	ctrl->info.sub_info_num = 0;

	return;
}

static unsigned int
uvc_ctrl_count_control(const uint8_t *bmCtrls, uint8_t bCtrlSize)
{
	int i = 0;
	unsigned int count = 0;
	const uint8_t *data = bmCtrls;

	if (data == NULL || bCtrlSize == 0)
		return 0;

	for (i = 0; i < bCtrlSize * 8; i++) {
		if ((data[i >> UVC_BITSHIFT] >>
		     (i & UVC_BITMASK)) & UVC_VALMASK)
			count++;
	}
	return count;
}

int
uvc_ctrl_init_dev(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrls)
{
	struct uvc_topo_node *topo_node, *tmp;
	struct uvc_control *ctrl = NULL;
	uint8_t bCtrlSize = 0;
	uint32_t nctrls = 0;
	uint8_t *bmCtrls = NULL;
	uint8_t i = 0;

	struct uvc_xu_node_info *node_info_xu = NULL;
	struct uvc_pu_node_info *node_info_pu = NULL;
	struct uvc_ct_node_info *node_info_ct = NULL;

	if (!ctrls) {
		return EINVAL;
	}

	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		if (UVC_ENT_TYPE(topo_node) == UDESCSUB_VC_EXTENSION_UNIT) {
			node_info_xu = (struct uvc_xu_node_info*)topo_node->node_info;

			bmCtrls = node_info_xu->bmControls;
			bCtrlSize = node_info_xu->bControlSize;
		} else if (UVC_ENT_TYPE(topo_node) == UDESCSUB_VC_PROCESSING_UNIT) {
			node_info_pu = (struct uvc_pu_node_info*)topo_node->node_info;

			bmCtrls = node_info_pu->bmControls;
			bCtrlSize = node_info_pu->bControlSize;
		} else if (UVC_ENT_TYPE(topo_node) == UVC_ITT_CAMERA) {
			node_info_ct = (struct uvc_ct_node_info*)topo_node->node_info;

			bmCtrls = node_info_ct->bmControls;
			bCtrlSize = node_info_ct->bControlSize;
		} else
			continue;

		nctrls = uvc_ctrl_count_control(bmCtrls, bCtrlSize);
		if (nctrls == 0)
			continue;
		topo_node->controls = kmalloc(nctrls * sizeof(*ctrl), M_UVC,
		    M_ZERO | M_WAITOK);
		if (!topo_node->controls)
			return ENOMEM;
		topo_node->controls_num = nctrls;

		ctrl = topo_node->controls;
		for (i = 0; i < bCtrlSize * 8; i++) {
			if (uvc_test_bit(bmCtrls, i) == 0)
				continue;

			ctrl->topo_node = topo_node;
			ctrl->index = i;

			uvc_ctrl_initialize_control(ctrl);

			ctrl++;
		}
	}

	return 0;
}

static int
uvc_query_v4l2_ctrl_sub(struct uvc_control *ctrl, struct v4l2_queryctrl *query)
{
	int i = 0;

	uint8_t sub_info_idx = ctrl->info.cached_sub_info_idx;

	struct uvc_ctrl_menu_item *menu_items =
	    ctrl->info.menus[sub_info_idx].menu_data;
	uint8_t menu_item_num = ctrl->info.menus[sub_info_idx].item_num;

	struct uvc_ctrl_sub_info *sub_info = ctrl->info.sub_infos[sub_info_idx];

	memset(query, 0, sizeof(*query));

	if (sub_info == NULL) {
		return EINVAL;
	}

	query->id = sub_info->v4l2_id;
	query->type = sub_info->v4l2_type;
	strlcpy(query->name, sub_info->v4l2_name, sizeof(query->name));

	query->flags = 0;

	switch (sub_info->v4l2_type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		query->minimum = 0;
		query->maximum = 1;
		query->step = 1;
		goto end;

	case V4L2_CTRL_TYPE_BUTTON:
		query->minimum = 0;
		query->maximum = 0;
		query->step = 0;
		goto end;

	case V4L2_CTRL_TYPE_MENU:
		query->minimum = 0;
		query->maximum = menu_item_num - 1;
		query->step = 1;

		for (i = 0; i < menu_item_num; i++) {
			if (menu_items[i].value == query->default_value) {
				query->default_value = i;
				break;
			}
		}
		goto end;

	default:
		break;
	}

	/*todo*/
	DPRINTF("WARNING __TO_BE_IMPLEMENT__ other par %s\n", __func__);
end:
	return 0;
}

static struct uvc_control *
uvc_search_control_sub(struct uvc_topo_node *topo_node, uint32_t v4l2_id)
{
	unsigned int i;
	unsigned int j;

	struct uvc_control *ctrl = NULL;

	if (topo_node == NULL) {
		return NULL;
	}

	for (i = 0; i < topo_node->controls_num; ++i) {
		ctrl = &topo_node->controls[i];

		if (!ctrl->initialized)
			continue;

		for (j = 0; j < MAX_MAPPING_NUM; j++) {
			if (ctrl->info.sub_infos[j] == NULL) {
				continue;
			}

			if (ctrl->info.sub_infos[j]->v4l2_id == v4l2_id) {
				ctrl->info.cached_sub_info_idx = j;
				return ctrl;
			}
		}
	}

	return NULL;
}

static struct uvc_control *
uvc_search_control(struct uvc_drv_ctrl *ctrls, uint32_t v4l2_id)
{
	struct uvc_control *ret_ctrl = NULL;
	struct uvc_topo_node *topo_node = NULL, *tmp = NULL;

	if (v4l2_id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		DPRINTF("Not support V4L2_CTRL_FLAG_NEXT_CTRL (0x%08x )\n",
		    v4l2_id);
		return NULL;
	}

	/* Mask the query flags. */
	v4l2_id &= V4L2_CTRL_ID_MASK;

	/* Find the control. */
	STAILQ_FOREACH_SAFE(topo_node, &ctrls->topo_nodes, link, tmp) {
		ret_ctrl = uvc_search_control_sub(topo_node, v4l2_id);
		if (ret_ctrl != NULL) {
			return ret_ctrl;
		}
	}

	DPRINTF("Control 0x%08x not found.\n", v4l2_id);
	return NULL;
}

int
uvc_query_v4l2_ctrl(struct uvc_drv_video *video, struct v4l2_queryctrl *query)
{
	struct uvc_control *ctrl;
	int ret = EINVAL;

	UVC_LOCK(&video->ctrl->mtx);

	ctrl = uvc_search_control(video->ctrl, query->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	ret = uvc_query_v4l2_ctrl_sub(ctrl, query);
done:
	UVC_UNLOCK(&video->ctrl->mtx);
	return ret;
}

int
uvc_query_v4l2_menu(struct uvc_drv_video *video,
		    struct v4l2_querymenu *qm)
{
	struct uvc_control *ctrl;
	uint8_t sub_info_idx = 0;
	int ret = 0;
	int id = qm->id;
	int index = qm->index;

	memset(qm, 0, sizeof(*qm));
	qm->id = id;
	qm->index = index;

	UVC_LOCK(&video->ctrl->mtx);

	ctrl = uvc_search_control(video->ctrl, qm->id);
	if (ctrl == NULL) {
		ret = EINVAL;
		goto done;
	}

	sub_info_idx = ctrl->info.cached_sub_info_idx;

	if (ctrl->info.menus[sub_info_idx].menu_data == NULL) {
		ret = EINVAL;
		goto done;
	}

	strlcpy(qm->name,
		ctrl->info.sub_infos[sub_info_idx]->v4l2_name,
		sizeof(qm->name));

done:
	UVC_UNLOCK(&video->ctrl->mtx);
	return ret;
}
