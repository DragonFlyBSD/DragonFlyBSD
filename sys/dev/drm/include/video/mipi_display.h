/*
 * Copyright (c) 2015 François Tigeot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __VIDEO_MIPI_DISPLAY_H__
#define __VIDEO_MIPI_DISPLAY_H__

#define MIPI_DSI_V_SYNC_START			0x01
#define MIPI_DSI_V_SYNC_END			0x11
#define MIPI_DSI_H_SYNC_START			0x21
#define MIPI_DSI_H_SYNC_END			0x31

#define MIPI_DSI_COLOR_MODE_OFF			0x02
#define MIPI_DSI_COLOR_MODE_ON			0x12
#define MIPI_DSI_SHUTDOWN_PERIPHERAL		0x22
#define MIPI_DSI_TURN_ON_PERIPHERAL		0x32

#define MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM	0x03
#define MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM	0x13
#define MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM	0x23

#define MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM	0x04
#define MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM	0x14
#define MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM	0x24

#define MIPI_DSI_DCS_SHORT_WRITE		0x05
#define MIPI_DSI_DCS_SHORT_WRITE_PARAM		0x15

#define MIPI_DSI_DCS_READ			0x06

#define MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE	0x37

#define MIPI_DSI_END_OF_TRANSMISSION		0x08

#define MIPI_DSI_NULL_PACKET			0x09
#define MIPI_DSI_BLANKING_PACKET		0x19

#define MIPI_DSI_GENERIC_LONG_WRITE		0x29
#define MIPI_DSI_DCS_LONG_WRITE			0x39

#define MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20	0x0c
#define MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24		0x1c
#define MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16		0x2c

#define MIPI_DSI_PACKED_PIXEL_STREAM_30		0x0d
#define MIPI_DSI_PACKED_PIXEL_STREAM_36		0x1d
#define MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12	0x3d

#define MIPI_DSI_PACKED_PIXEL_STREAM_16		0x0e
#define MIPI_DSI_PACKED_PIXEL_STREAM_18		0x1e
#define MIPI_DSI_PIXEL_STREAM_3BYTE_18		0x2e
#define MIPI_DSI_PACKED_PIXEL_STREAM_24		0x3e

#endif /* __VIDEO_MIPI_DISPLAY_H__ */
