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

#define MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM	0x03
#define MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM	0x13
#define MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM	0x23

#define MIPI_DSI_DCS_SHORT_WRITE_PARAM		0x15
#define MIPI_DSI_GENERIC_LONG_WRITE		0x29
#define MIPI_DSI_DCS_LONG_WRITE			0x39
#define MIPI_DSI_DCS_SHORT_WRITE		0x05		

#define MIPI_DSI_DCS_READ			0x06

#define MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM	0x04
#define MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM	0x14
#define MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM	0x24

#endif /* __VIDEO_MIPI_DISPLAY_H__ */
