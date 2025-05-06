/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Dell Inc.
 *
 *	Alvin Chen <weike_chen@dell.com, vico.chern@qq.com>
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

#ifndef _DEV_USB_UVC_V4L2_H
#define _DEV_USB_UVC_V4L2_H

#define UVC_V4L2_DEVICE_NAME		"video"

#ifndef LINUX_MAJAR
#define LINUX_MAJAR 81
#endif
#ifndef LINUX_MINOR
#define LINUX_MINOR 0
#endif

enum uvc_v4l2_work_mode {
	UVC_V4L2_MODE_READ	=	0x0,
	UVC_V4L2_MODE_MMAP,
	UVC_V4L2_MODE_MAX,
};

enum uvc_v4l2_work_pri {
	UVC_V4L2_PRI_PASSIVE	=	0x0,
	UVC_V4L2_PRI_ACTIVE,
	UVC_V4L2_PRI_MAX,
};

struct uvc_v4l2_cdev_priv {
	uint64_t	work_mode;
	uint64_t	work_pri;
	uint64_t	num;
	struct uvc_drv_video	*v;
};

struct uvc_v4l2 {
	struct cdev *cdev;
};

struct uvc_xu_control_mapping {
	uint32_t id;
	uint8_t name[32];
	uint8_t entity[16];
	uint8_t selector;
	uint8_t size;
	uint8_t offset;
	uint32_t v4l2_type;
	uint32_t data_type;
	char *menu_info;
	uint32_t menu_count;
	uint32_t reserved[4];
};

struct uvc_xu_control_query {
	uint8_t unit;
	uint8_t selector;
	uint8_t query;
	uint16_t size;
	uint8_t *data;
};

#define UVCIOC_CTRL_MAP	_IOWR('u', 0x20, struct uvc_xu_control_mapping)
#define UVCIOC_CTRL_QUERY	_IOWR('u', 0x21, struct uvc_xu_control_query)

struct uvc_drv_video;
extern int uvc_v4l2_reg(struct uvc_drv_video *v);
extern void uvc_v4l2_unreg(struct uvc_drv_video *v);
extern void uvc_simple_frac(uint32_t *numerator, uint32_t *denominator,
	uint32_t n_terms, uint32_t threshold);

#endif /* end _DEV_USB_UVC_V4L2_H */
