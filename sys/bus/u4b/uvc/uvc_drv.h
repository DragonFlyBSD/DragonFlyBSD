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

#ifndef _DEV_USB_UVC_DRV_H
#define _DEV_USB_UVC_DRV_H

#include "uvc_buf.h"

/* Vendor */
#define USB_VENDOR_ID_LOGITECH			0x046D
/* Driver */
#define UVC_DRIVER_NAME				"uvcvideo"
/* V4l2 Version */
#define	V4L_VERSION(a, b, c)			(((a) << 16) + ((b) << 8) + (c))
#define	UVC_DEVICE_NAME				"video"
/* UVC Transfer */
#define	UVC_N_TRANSFER				2
#define	UVC_N_BULKTRANSFER			3
#define UVC_N_ISOFRAMES				0x8

#define	UVCINTR_N_TRANSFER			1

/* Format flags */
#define UVC_FMT_FLAG_COMPRESSED			0x00000001
#define UVC_FMT_FLAG_STREAM			0x00000002
#define UDESCSUB_VC_HEADER			0x1
#define UDESCSUB_VC_INPUT_TERMINAL		0x2
#define UDESCSUB_VC_OUTPUT_TERMINAL		0x3
#define UDESCSUB_VC_SELECTOR_UNIT		0x4
#define UDESCSUB_VC_PROCESSING_UNIT		0x5
#define UDESCSUB_VC_EXTENSION_UNIT		0x6
/* VideoStreaming class specific interface descriptor */
#define UDESCSUB_VS_UNDEFINED			0x00
#define UDESCSUB_VS_INPUT_HEADER		0x01
#define UDESCSUB_VS_OUTPUT_HEADER		0x02
#define UDESCSUB_VS_STILL_IMAGE_FRAME		0x03
#define UDESCSUB_VS_FORMAT_UNCOMPRESSED		0x04
#define UDESCSUB_VS_FRAME_UNCOMPRESSED		0x05
#define UDESCSUB_VS_FORMAT_MJPEG		0x06
#define UDESCSUB_VS_FRAME_MJPEG			0x07
#define UDESCSUB_VS_FORMAT_MPEG2TS		0x0a
#define UDESCSUB_VS_FORMAT_DV			0x0c
#define UDESCSUB_VS_COLORFORMAT			0x0d
#define UDESCSUB_VS_FORMAT_FRAME_BASED		0x10
#define UDESCSUB_VS_FRAME_FRAME_BASED		0x11
/* VideoStreaming interface controls */
#define VS_CONTROL_UNDEFINED			0x00
#define VS_PROBE_CONTROL			0x01
#define VS_COMMIT_CONTROL			0x02
#define VS_STILL_PROBE_CONTROL			0x03
#define VS_STILL_COMMIT_CONTROL			0x04
#define VS_STILL_IMAGE_TRIGGER_CONTROL		0x05
#define VS_STREAM_ERROR_CODE_CONTROL		0x06
#define VS_GENERATE_KEY_FRAME_CONTROL		0x07
#define VS_UPDATE_FRAME_SEGMENT_CONTROL		0x08
#define VS_SYNC_DELAY_CONTROL			0x09
/* Selector Unit controls */
#define SU_CONTROL_UNDEFINED			0x00
#define SU_INPUT_SELECT_CONTROL			0x01
/* Input Terminal types */
#define UVC_ITT_VENDOR_SPECIFIC			0x0200
#define UVC_ITT_CAMERA				0x0201
#define UVC_ITT_MEDIA_TRANSPORT_INPUT		0x0202
/* Request codes */
#define RC_UNDEFINED				0x00
#define SET_CUR					0x01
#define GET_CUR					0x81
#define GET_MIN					0x82
#define GET_MAX					0x83
#define GET_RES					0x84
#define GET_LEN					0x85
#define GET_INFO				0x86
#define GET_DEF					0x87

/* A.9.1. VideoControl Interface Control Selectors */
#define UVC_VC_CONTROL_UNDEFINED                        0x00
#define UVC_VC_VIDEO_POWER_MODE_CONTROL                 0x01
#define UVC_VC_REQUEST_ERROR_CODE_CONTROL               0x02

/* A.9.2. Terminal Control Selectors */
#define UVC_TE_CONTROL_UNDEFINED                        0x00

/* A.9.3. Selector Unit Control Selectors */
#define UVC_SU_CONTROL_UNDEFINED                        0x00
#define UVC_SU_INPUT_SELECT_CONTROL                     0x01

/* A.9.4. Camera Terminal Control Selectors */
#define UVC_CT_CONTROL_UNDEFINED                        0x00
#define UVC_CT_SCANNING_MODE_CONTROL                    0x01
#define UVC_CT_AE_MODE_CONTROL                          0x02
#define UVC_CT_AE_PRIORITY_CONTROL                      0x03
#define UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL           0x04
#define UVC_CT_EXPOSURE_TIME_RELATIVE_CONTROL           0x05
#define UVC_CT_FOCUS_ABSOLUTE_CONTROL                   0x06
#define UVC_CT_FOCUS_RELATIVE_CONTROL                   0x07
#define UVC_CT_FOCUS_AUTO_CONTROL                       0x08
#define UVC_CT_IRIS_ABSOLUTE_CONTROL                    0x09
#define UVC_CT_IRIS_RELATIVE_CONTROL                    0x0a
#define UVC_CT_ZOOM_ABSOLUTE_CONTROL                    0x0b
#define UVC_CT_ZOOM_RELATIVE_CONTROL                    0x0c
#define UVC_CT_PANTILT_ABSOLUTE_CONTROL                 0x0d
#define UVC_CT_PANTILT_RELATIVE_CONTROL                 0x0e
#define UVC_CT_ROLL_ABSOLUTE_CONTROL                    0x0f
#define UVC_CT_ROLL_RELATIVE_CONTROL                    0x10
#define UVC_CT_PRIVACY_CONTROL                          0x11

/* A.9.5. Processing Unit Control Selectors */
#define UVC_PU_CONTROL_UNDEFINED                        0x00
#define UVC_PU_BACKLIGHT_COMPENSATION_CONTROL           0x01
#define UVC_PU_BRIGHTNESS_CONTROL                       0x02
#define UVC_PU_CONTRAST_CONTROL                         0x03
#define UVC_PU_GAIN_CONTROL                             0x04
#define UVC_PU_POWER_LINE_FREQUENCY_CONTROL             0x05
#define UVC_PU_HUE_CONTROL                              0x06
#define UVC_PU_SATURATION_CONTROL                       0x07
#define UVC_PU_SHARPNESS_CONTROL                        0x08
#define UVC_PU_GAMMA_CONTROL                            0x09
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL        0x0a
#define UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL   0x0b
#define UVC_PU_WHITE_BALANCE_COMPONENT_CONTROL          0x0c
#define UVC_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL     0x0d
#define UVC_PU_DIGITAL_MULTIPLIER_CONTROL               0x0e
#define UVC_PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL         0x0f
#define UVC_PU_HUE_AUTO_CONTROL                         0x10
#define UVC_PU_ANALOG_VIDEO_STANDARD_CONTROL            0x11
#define UVC_PU_ANALOG_LOCK_STATUS_CONTROL               0x12

/* --------------------------------------------------------------------------
 * UVC constants
 */

#define UVC_TERM_INPUT                  0x0000
#define UVC_TERM_OUTPUT                 0x8000
#define UVC_TERM_DIRECTION(term)        ((term)->type & 0x8000)

#define UVC_ENT_TYPE(ent)		((ent)->node_type & 0x7fff)
#define UVC_ENT_IS_UNIT(ent)		(((ent)->node_type & 0xff00) == 0)
#define UVC_ENT_IS_TERM(ent)		(((ent)->node_type & 0xff00) != 0)
#define UVC_ENT_IS_ITERM(ent) \
	(UVC_ENT_IS_TERM(ent) && \
	((ent)->node_type & 0x8000) == UVC_TERM_INPUT)
#define UVC_ENT_IS_OTERM(ent) \
	(UVC_ENT_IS_TERM(ent) && \
	((ent)->node_type & 0x8000) == UVC_TERM_OUTPUT)

// 2.3 Video Function Topology
enum uvc_topo_type {
	UVC_TOPO_TYPE_UNKNOWN,
	UVC_TOPO_TYPE_INPUT_TERMINAL,
	UVC_TOPO_TYPE_OUTPUT_TERMINAL,
	UVC_TOPO_TYPE_SELECTOR_UNIT,
	UVC_TOPO_TYPE_PROCESSING_UNIT,
	UVC_TOPO_TYPE_ENCODING_UNIT,
	UVC_TOPO_TYPE_EXTENSION_UNIT,
	UVC_TOPO_TYPE_CAMERA_TERMINAL,
	UVC_TOPO_TYPE_MEDIA_TRANSPORT_TERMINAL
};

/* ------------------------------------------------------------------------
 * GUIDs
 */
#define UVC_GUID_LOGITECH_DEV_INFO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1e}
#define UVC_GUID_LOGITECH_USER_HW \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x1f}
#define UVC_GUID_LOGITECH_VIDEO \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x50}
#define UVC_GUID_LOGITECH_MOTOR \
	{0x82, 0x06, 0x61, 0x63, 0x70, 0x50, 0xab, 0x49, \
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x56}

#define UVC_GUID_FORMAT_MJPEG \
	{ 'M',  'J',  'P',  'G', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YUY2 \
	{ 'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_NV12 \
	{ 'N',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_YV12 \
	{ 'Y',  'V',  '1',  '2', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_I420 \
	{ 'I',  '4',  '2',  '0', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_UYVY \
	{ 'U',  'Y',  'V',  'Y', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_Y800 \
	{ 'Y',  '8',  '0',  '0', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_BY8 \
	{ 'B',  'Y',  '8',  ' ', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define UVC_GUID_FORMAT_H264 \
	{ 'H',  '2',  '6',  '4', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}


struct uvc_softc;
struct uvc_v4l2;
struct uvc_buf_queue;

/* Video Descriptor */
struct uvc_vc_header_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uWord	wRevision;
	uWord	wTotalLen;
	uDWord	dClockFreq;
	uByte	bIfaceNums;
	uByte   bIfaceList[0];		/*  Interface Index Lists   */
} __packed;

struct uvc_vc_input_terminal_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalID;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bITerminal;
} __packed;

struct uvc_vc_output_terminal_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bTerminalID;
	uWord wTerminalType;
	uByte bAssocTerminal;
	uByte bSourceID;
	uByte bITerminal;
} __packed;

struct uvc_vc_selector_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte bNrInPins;
	uByte baSourceID[0];
	uByte iSelector;
} __packed;

struct uvc_vc_processing_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte bSourceID;
	uWord wMaxMultiplier;
	uByte bControlSize;
	uByte bmControls[2];
	uByte iProcessing;
	uByte bmVideoStandards;
} __packed;

struct uvc_vc_extension_unit_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubType;
	uByte bUnitID;
	uByte guidExtensionCode[16];
	uByte bNumControls;
	uByte bNrInPins;
	uByte baSourceID[0];
	uByte bControlSize;
	uByte bmControls[0];
	uByte iExtension;
} __packed;

struct uvc_vs_in_header_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatNum;
	uWord wTotalLen;
	uByte bEp;
	uByte bInfo;
	uByte bTerminalLink;
	uByte bStillCaptureMethod;
	uByte bTriggerSupport;
	uByte bTriggerUsage;
	uByte bControlSize;
	uByte bControls[0];         /* Controls */
} __packed;

struct uvc_vs_color_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bColorPris;
	uByte bTranChars;
	uByte bMatrix;
};

struct uvc_vs_uncompressed_format_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatIndex;
	uByte bFrameNum;
	uByte bGuidFmt[16];
	uByte bBpp;
	uByte bDefaultFrameIndex;
	uByte bAspectRadiox;
	uByte bAspectRadioy;
	uByte bInterlaceFlags;
	uByte bCopyProtect;
} __packed;

struct uvc_vs_frame_based_format_desc {
	uByte bLength;
	uByte bDescriptorType;
	uByte bDescriptorSubtype;
	uByte bFormatIndex;
	uByte bFrameNum;
	uByte bGuidFmt[16];
	uByte bBitsPerpixel;
	uByte bDefaultFrameIndex;
	uByte bAspectRadiox;
	uByte bAspectRadioy;
	uByte bInterlaceFlags;
	uByte bCopyProtect;
	uByte bVariableSize;
} __packed;

struct uvc_vs_frame_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFrameIndex;
	uByte	bCapabilities;
	uWord	wWidth;
	uWord	wHeight;
	uDWord	dMinBitRate;
	uDWord	dMaxBitRate;
	uDWord	dMaxFrameBufferSize;
	uDWord	dDefaultFrameInterval;
	uByte	bFrameIntervalType;
	uDWord	dFrameInterval[0];
} __packed;

struct uvc_vs_frame_based_desc {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFrameIndex;
	uByte	bCapabilities;
	uWord	wWidth;
	uWord	wHeight;
	uDWord	dMinBitRate;
	uDWord	dMaxBitRate;
	uDWord	dDefaultFrameInterval;
	uByte	bFrameIntervalType;
	uDWord	dwBytesPerLine;
	uDWord	dFrameInterval[0];
} __packed;

struct uvc_data_request {
	uWord wHint;
	uByte bFormatIndex;
	uByte bFrameIndex;
	uDWord dwFrameInterval;
	uWord wKeyFrameRate;
	uWord wPFrameRate;
	uWord wCompQuality;
	uWord wCompWindowSize;
	uWord wDelay;
	uDWord dwMaxFrameSize;
	uDWord dwMaxPayloadSize;
	uDWord dwClockFrequency;
	uByte bFramingInfo;
	uByte bPreferedVersion;
	uByte bMinVersion;
	uByte bMaxVersion;
	uByte __dummy[14];
} __packed;

typedef char assert_uvc_data_request_is_48_bytes[(!!(sizeof(struct uvc_data_request)==48))*2-1];

struct uvc_data_payload_header {
	uint8_t	len;
#define	UVC_PL_HEADER_BIT_FID		(1)
#define	UVC_PL_HEADER_BIT_EOF		(1 << 1)
#define	UVC_PL_HEADER_BIT_PTS		(1 << 2)
#define	UVC_PL_HEADER_BIT_SCR		(1 << 3)
#define	UVC_PL_HEADER_BIT_RES		(1 << 4)
#define	UVC_PL_HEADER_BIT_STI		(1 << 5)
#define	UVC_PL_HEADER_BIT_ERR		(1 << 6)
#define	UVC_PL_HEADER_BIT_EOH		(1 << 7)
	uint8_t	header;
#if 0
	uint32_t pts;
	uint8_t scr[6];
#endif
} __attribute__ ((packed));

/*
 *  * Dynamic controls
 */

/* Data types for UVC control data */
#define UVC_CTRL_DATA_RAW          0
#define UVC_CTRL_DATA_SIGNED       1
#define UVC_CTRL_DATA_UNSIGNED     2
#define UVC_CTRL_DATA_BOOLEAN      3
#define UVC_CTRL_DATA_ENUM         4
#define UVC_CTRL_DATA_BITMASK      5

/* Control flags */
#define UVC_CTRL_SET_CUR           0x0001
#define UVC_CTRL_GET_CUR           0x0002
#define UVC_CTRL_GET_MIN           0x0004
#define UVC_CTRL_GET_MAX           0x0008
#define UVC_CTRL_GET_RES           0x0010
#define UVC_CTRL_GET_DEF           0x0020
#define UVC_CTRL_RESTORE           0x0040
#define UVC_CTRL_AUTO_UPDATE       0x0080
#define UVC_CTRL_ASYNCHRONOUS	0x0100

#define UVC_CTRL_GET_RANGE \
	(UVC_CTRL_GET_CUR | UVC_CTRL_GET_MIN | \
	UVC_CTRL_GET_MAX | UVC_CTRL_GET_RES | \
	UVC_CTRL_GET_DEF)

struct uvc_ctrl_menu_item {
	uint32_t value;
	char name[32];
};

struct uvc_ctrl_menu {
	uint32_t item_num;
	struct uvc_ctrl_menu_item* menu_data;
};

struct uvc_ctrl_info {
	uint8_t index;       /* Bit index in bmControls */

	enum uvc_topo_type topo_type;
	uint8_t selector;

	uint16_t byte_size;
	uint32_t flags;
	uint32_t data_type;

#define MAX_MAPPING_NUM 5
	uint8_t sub_info_num;
	struct uvc_ctrl_sub_info* sub_infos[MAX_MAPPING_NUM];
	struct uvc_ctrl_menu menus[MAX_MAPPING_NUM];

	uint8_t cached_sub_info_idx;
};

struct uvc_ctrl_sub_info {
	struct uvc_ctrl_info* uvc_info;

	uint32_t v4l2_id;
	uint8_t v4l2_name[32];
	enum v4l2_ctrl_type v4l2_type;

	uint8_t bit_offset;
	uint8_t bit_size;

	/*
		superior & inferior is used to determine flag 'inactive';
		E.g. Setting the V4L2_CID_HUE when V4L2_CID_HUE_AUTO is selected doesn't do anything.
		Only when the V4L2_CID_HUE_AUTO is turned off can you set the gain control.
		In this case, V4L2_CID_HUE_AUTO is superior of V4L2_CID_HUE.
	*/
	uint32_t superior_v4l2_id;
	uint32_t superior_manual;
	uint32_t inferior_v4l2_ids[2];

	int32_t (*get)(struct uvc_ctrl_sub_info *this, uint8_t query,
		       const uint8_t *data);
	void (*set)(struct uvc_ctrl_sub_info *this, int32_t value,
		    uint8_t *data);
};


struct uvc_control {
	struct uvc_topo_node *topo_node;
	struct uvc_ctrl_info info;
	uint8_t index; /*
			*  Used to match the uvc_control entry
			*  with a uvc_ctrl_info
			*/
	uint8_t dirty:1,
		loaded:1,
		modified:1,
		cached:1,
		initialized:1;

	uint8_t *uvc_data;
	/* File handle that last changed the control. */
	struct uvc_drv_video *video;
};
struct uvc_ct_node_info {
	uint16_t wObjectiveFocalLengthMin;
	uint16_t wObjectiveFocalLengthMax;
	uint16_t wOcularFocalLength;
	uint8_t bControlSize;
	uint8_t *bmControls;
}; // Camera Terminal

struct uvc_media_node_info {
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t bTransportModeSize;
	uint8_t *bmTransportModes;
}; // MEDIA Transport Terminal

struct uvc_pu_node_info {
	uint16_t wMaxMultiplier;
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t bmVideoStandards;
}; // Processing Unit

struct uvc_xu_node_info {
	uint8_t guidExtensionCode[16];
	uint8_t bNumControls;
	uint8_t bControlSize;
	uint8_t *bmControls;
	uint8_t *bmControlsType;
}; // Extension Unit

struct uvc_topo_node {
	STAILQ_ENTRY(uvc_topo_node) link;

	char node_name[64];

	uint16_t node_type;

	uint32_t flags;

	uint8_t node_id;
	// e.g.:
	// If Video Function Topology like this:
	//  o---o
	//  | 2 |
	//  o---o
	//    \___ o---o
	//     ___ | 5 |
	//    /    o---o
	//  o---o
	//  | 3 |
	//  o---o
	//
	// node 5 has two source ids,
	// one is node 2, another is node 3.
	uint16_t src_ids_num;
	uint8_t *src_ids;

	// mask of controls
	uint16_t controls_mask_len;
	uint8_t *controls_mask;

	uint32_t controls_num;
	struct uvc_control *controls;

	// data pointed by node_info is specified by topo_type
	void *node_info;
};

struct uvc_data_interval {
	uint32_t val;
};

struct uvc_data_frame {
/* begin interval for this frame */
	struct uvc_data_interval *interval;
	/* interval nums, desc string */
	uint8_t interval_type;

/* attribute */
	uint8_t index;
	uint8_t cap;
	uint8_t nouse;
	uint32_t min_bit_rate;
	uint32_t max_bit_rate;
	uint32_t default_interval;
	uint32_t max_buffer_size;

/* important */
	uint16_t width;
	uint16_t height;
};

struct uvc_data_format {
/* begin frame for this format */
	struct uvc_data_frame *frm;
	uint64_t nfrm;
	/* for COMPRESSED or not */
	uint64_t flags;

/* attributes */
	uint32_t fcc;
	uint8_t index;
	uint8_t bpp;
	uint8_t colorspace;
	uint8_t unuse;
	char name[64];
};

struct uvc_drv_data {
/* interface */
	struct usb_interface	*iface;
	/* for device configure iface_num + unit */
	uint8_t	iface_num;
	/* for usb framework */
	uint8_t	iface_index;
	/*for distinguishing bulk and iso uvc*/
	uint8_t num_altsetting;

	uint16_t maxpsize;

/* transfer */
	//struct usb_xfer	*xfer[UVC_N_TRANSFER];
	struct usb_xfer	*xfer[UVC_N_BULKTRANSFER];

/* data format */
	uint8_t nfmt;
	uint8_t nfrm;
	uint8_t nitv;
	uint8_t unuse[3];
	struct uvc_data_format *fmt;
};

struct uvc_drv_ctrl {
/* interface */
	struct usb_interface	*iface;
	/* for device configure iface_num + unit */
	uint8_t	iface_num;
	/* for usb framework */
	uint8_t	iface_index;
	struct lock	mtx;

/* desc infomation */
	STAILQ_HEAD(, uvc_topo_node) topo_nodes;
	uint8_t	sid;
	uint8_t	h264id;
	uint8_t unuse;
	uint16_t	revision;
	uint16_t	clock_freq;
};

struct uvc_drv_video {
	uint64_t		unit;
	uint64_t		users;
	uint64_t		pri;
	uint64_t		htsf;

	struct uvc_softc	*sc;
	struct uvc_drv_ctrl	*ctrl;
	struct uvc_drv_data	*data;
	struct usb_xfer		*intr_xfer[UVCINTR_N_TRANSFER];

	struct lock mtx;
	struct uvc_data_request	req;
	struct uvc_data_format *cur_fmt;
	struct uvc_data_frame *cur_frm;
	uint64_t enable;
	uint64_t bad_frame;
	enum v4l2_buf_type type;
	uint8_t last_fid;

	struct {
		uint8_t header[256];
		uint8_t last_fid;
		uint32_t header_size;
		uint32_t skip_payload;
		uint32_t payload_size;
		uint32_t max_payload_size;
	} bulk;

	struct uvc_v4l2 *v4l2;
	struct uvc_buf_queue bq;
};

struct uvc_drv_format_desc {
	char *name;
	uint8_t guid[16];
	uint32_t fcc;
};

struct uvc_softc {
	char	name[64];
	device_t		dev;
	struct usb_device	*udev;
	struct usb_interface	*iface;

	struct uvc_drv_ctrl	*ctrl;
	/* TODO: not only one */
	struct uvc_drv_data	*data;
	struct uvc_drv_video	*video;
	enum v4l2_buf_type	type;
	//struct uvc_buf_queue	*bq;
#define UVC_DROP_UNCOMPRESS_FORMAT 0x1
#define UVC_QUIRK_PROBE_MINMAX	   0x2
#define UVC_BIGGER_TRANSFER_BUF	   0x4
#define UVC_COMMIT_IN_ADVANCE	   0x8
#define UVC_DISABLE_HUB_U1U2	   0x16
#define UVC_NO_EOF		   (1<<6)
	uint8_t			quirks;
};

extern int uvc_debug;
struct uvc_xu_control_query;

MALLOC_DECLARE(M_UVC);

int uvc_drv_check_video_context(struct uvc_drv_video *v, unsigned char *data,
	uint32_t len);
int uvc_drv_get_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_format *f);
int uvc_drv_enum_v4l2_fmt(struct uvc_drv_video *v, struct v4l2_fmtdesc *vfmt);
int uvc_drv_enum_v4l2_framesizes(struct uvc_drv_video *v,
	struct v4l2_frmsizeenum *fs);
int uvc_drv_enum_v4l2_frameintervals(struct uvc_drv_video *v,
	struct v4l2_frmivalenum *itv);
int uvc_drv_try_v4l2_fmt(struct uvc_drv_video *video, struct v4l2_format *vfmt,
	struct uvc_data_request *req, struct uvc_data_format **rfmt,
	struct uvc_data_frame **rfrm);
int uvc_drv_set_video(struct uvc_drv_video *video,
	struct uvc_data_request *req, struct uvc_data_format *fmt,
	struct uvc_data_frame *frm);
int uvc_drv_xu_ctrl_query(struct uvc_drv_video *v,
	struct uvc_xu_control_query *q);
int uvc_drv_start_video(struct uvc_drv_video *video);
int uvc_drv_stop_video(struct uvc_drv_video *video, int close);
int uvc_drv_set_streampar(struct uvc_drv_video *v, struct v4l2_streamparm *a);
int uvc_drv_get_selection(struct uvc_drv_video *v, struct v4l2_selection *sel);
int uvc_drv_get_pixelaspect(void);
//uvc controls
int uvc_ctrl_init_dev(struct uvc_softc *sc, struct uvc_drv_ctrl *ctrls);
void uvc_ctrl_destroy_mappings(struct uvc_control *ctrl);
int uvc_query_v4l2_ctrl(struct uvc_drv_video *video,
			struct v4l2_queryctrl *v4l2_ctrl);
int uvc_query_v4l2_menu(struct uvc_drv_video *video,
			struct v4l2_querymenu *qm);
#endif /* end _DEV_USB_UVC_DRV_H */
