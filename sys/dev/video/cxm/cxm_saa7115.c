/*
 * Copyright (c) 2003, 2004, 2005
 *	John Wehle <john@feith.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Video decoder routines for the Conexant MPEG-2 Codec driver.
 *
 * Ideally these routines should be implemented as a separate
 * driver which has a generic video decoder interface so that
 * it's not necessary for each multimedia driver to re-invent
 * the wheel.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/clock.h>

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>
#include <bus/iicbus/iicbus.h>

#include "iicbb_if.h"


static const struct cxm_saa7115_command
saa7115_init = {
	19,
	{
		/* Full auto mode for CVBS */
		{ 0x01, 1, { 0x08 } },
		{ 0x03, 18, { 0x20, 0x90, 0x90, 0xeb, 0xe0, 0xb0, 0x40, 0x80,
			      0x44, 0x40, 0x00, 0x03, 0x2a, 0x06, 0x00, 0x9d,
			      0x80, 0x01 } },
		{ 0x17, 7, { 0x99, 0x40, 0x80, 0x77, 0x42, 0xa9, 0x01 } },

		/*
		 * VBI data slicer
		 *
		 * NTSC raw VBI data on lines 10 through 21
		 * PAL raw VBI data on lines 6 through 22
		 *
		 * Actually lines 21 and 22 are set by the
		 * NTSC and PAL specific configurations.
		 */
		{ 0x40, 20, { 0x40, 0x00, 0x00, 0x00, 0x00, 0xdd, 0xdd, 0xdd,
			      0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
			      0xdd, 0xdd, 0xdd, 0xdd } },
		{ 0x56, 4, { 0x00, 0x00, 0x00, 0x47 } },
		{ 0x5c, 3, { 0x00, 0x1f, 0x35 } },

		/* I-port and X-port configuration */
		{ 0x80, 2, { 0x00, 0x01 } },
		{ 0x83, 5, { 0x00, 0x20, 0x21, 0xc5, 0x01 } },

		/* Scaler input configuration and output format settings */
		{ 0xc0, 4, { 0x00, 0x08, 0x00, 0x80 } },

		/* VBI scaler configuration */
		{ 0x90, 4, { 0x80, 0x48, 0x00, 0x84 } },
		{ 0xa0, 3, { 0x01, 0x00, 0x00 } },
		{ 0xa4, 3, { 0x80, 0x40, 0x40 } },
		{ 0xa8, 3, { 0x00, 0x02, 0x00 } },
		{ 0xac, 3, { 0x00, 0x01, 0x00 } },
		{ 0xb0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },
		{ 0xb8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/* Audio Master Clock to Audio Serial Clock ratio */
		{ 0x38, 3, { 0x03, 0x10, 0x00 } },

		/* PLL2 target clock 27 MHz (using a 32.11 MHz crystal) */
		{ 0xf1, 4, { 0x05, 0xd0, 0x35, 0x00 } },

		/* Pulse generator */
		{ 0xf6, 10, { 0x61, 0x0e, 0x60, 0x0e, 0x60, 0x0e, 0x00,
			      0x00, 0x00, 0x88 } }
	}
};

static const struct cxm_saa7115_command
saa7115_mute = {
	1,
	{
		/* Disable I-port */
		{ 0x87, 1, { 0x00 } },
	}
};

static const struct cxm_saa7115_command
saa7115_unmute = {
	1,
	{
		/* Enable I-port */
		{ 0x87, 1, { 0x01 } },
	}
};

static const struct cxm_saa7115_command
saa7115_select_fm = {
	1,
	{
		/* Enable audio clock */
		{ 0x88, 1, { 0x33 } }
	}
};

static const struct cxm_saa7115_command
saa7115_select_line_in_composite = {
	3,
	{
		/* Amp plus anti-alias filter, CVBS from AI11 */
		{ 0x02, 1, { 0xc0 } },
		/* Adaptive luminance comb filter */
		{ 0x09, 1, { 0x40 } },

		/* Enable AD1, audio clock, scaler, decoder */
		{ 0x88, 1, { 0x70 } }
	}
};

static const struct cxm_saa7115_command
saa7115_select_line_in_svideo = {
	3,
	{
		/* Amp plus anti-alias filter, Y / C from AI11 / AI21 */
		{ 0x02, 1, { 0xc8 } },
		/* Bypass chrominance trap / comb filter */
		{ 0x09, 1, { 0x80 } },

		/* Enable AD1 & 2, audio clock, scaler, decoder */
		{ 0x88, 1, { 0xf0 } }
	}
};

static const struct cxm_saa7115_command
saa7115_select_tuner = {
	3,
	{
		/* Amp plus anti-alias filter, CVBS (auto gain) from AI23 */
		{ 0x02, 1, { 0xc4 } },
		/* Adaptive luminance comb filter */
		{ 0x09, 1, { 0x40 } },

		/* Enable AD2, audio clock, scaler, decoder */
		{ 0x88, 1, { 0xb0 } }
	}
};

static const struct cxm_saa7115_command
saa7115_audio_clock_44100_ntsc = {
	2,
	{
		/* Audio clock 44.1 kHz NTSC (using a 32.11 MHz crystal) */
		{ 0x30, 3, { 0xbc, 0xdf, 0x02 } },
		{ 0x34, 3, { 0xf2, 0x00, 0x2d } }
	}
};

static const struct cxm_saa7115_command
saa7115_audio_clock_44100_pal = {
	2,
	{
		/* Audio clock 44.1 kHz PAL (using a 32.11 MHz crystal) */
		{ 0x30, 3, { 0x00, 0x72, 0x03 } },
		{ 0x34, 3, { 0xf2, 0x00, 0x2d } }
	}
};

static const struct cxm_saa7115_command
saa7115_audio_clock_48000_ntsc = {
	2,
	{
		/* Audio clock 48 kHz NTSC (using a 32.11 MHz crystal) */
		{ 0x30, 3, { 0xcd, 0x20, 0x03 } },
		{ 0x34, 3, { 0xce, 0xfb, 0x30 } }
	}
};

static const struct cxm_saa7115_command
saa7115_audio_clock_48000_pal = {
	2,
	{
		/* Audio clock 48 kHz PAL (using a 32.11 MHz crystal) */
		{ 0x30, 3, { 0x00, 0xc0, 0x03 } },
		{ 0x34, 3, { 0xce, 0xfb, 0x30 } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_vcd_ntsc_double_lines = {
	13,
	{
		/*
		 * Input window = 720 x 240, output window = 352 x 240 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 5 and active video starting at line 23 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * NTSC active video should actually start at line 22, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x12, 0x00, 0xf2, 0x00,
			      0x60, 0x01, 0xf0, 0x00 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x02, 0x02, 0xaa } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x18, 0x04, 0x00 } },
		{ 0xdc, 3, { 0x0c, 0x02, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 12, output window = 1440 x 12.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x05, 0x00, 0x0c, 0x00,
			      0xa0, 0x05, 0x0c, 0x00 } },

		/* Inverted VGATE start at line 23, stop after line 263 */
		{ 0x15, 2, { 0x02, 0x12 } },

		/* VBI data slicer 525 lines, line 21 is closed caption */
		{ 0x54, 2, { 0x4d, 0x00 } },
		{ 0x5a, 2, { 0x06, 0x83 } },

		/* PLL2 525 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xad } },

		/* Pulse generator 525 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xad } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_vcd_pal_double_lines = {
	13,
	{
		/*
		 * Input window = 720 x 288, output window = 352 x 288 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 2 and active video starting at line 25 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * PAL active video should actually start at line 24, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x17, 0x00, 0x22, 0x01,
			      0x60, 0x01, 0x20, 0x01 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x02, 0x02, 0xaa } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x18, 0x04, 0x00 } },
		{ 0xdc, 3, { 0x0c, 0x02, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 17, output window = 1440 x 17.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x04, 0x00, 0x11, 0x00,
			      0xa0, 0x05, 0x11, 0x00 } },

		/* Inverted VGATE start at line 25, stop after line 313 */
		{ 0x15, 2, { 0x37, 0x17 } },

		/* VBI data slicer 625 lines, line 22 is closed caption */
		{ 0x54, 2, { 0xdd, 0x4d } },
		{ 0x5a, 2, { 0x03, 0x03 } },

		/* PLL2 625 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xb0 } },

		/* Pulse generator 625 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xb0 } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_svcd_ntsc = {
	13,
	{
		/*
		 * Input window = 720 x 240, output window = 480 x 240 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 5 and active video starting at line 23 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * NTSC active video should actually start at line 22, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x12, 0x00, 0xf2, 0x00,
			      0xe0, 0x01, 0xf0, 0x00 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x01, 0x00, 0x00 } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x00, 0x06, 0x00 } },
		{ 0xdc, 3, { 0x00, 0x03, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 12, output window = 1440 x 12.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x05, 0x00, 0x0c, 0x00,
			      0xa0, 0x05, 0x0c, 0x00 } },

		/* Inverted VGATE start at line 23, stop after line 263 */
		{ 0x15, 2, { 0x02, 0x12 } },

		/* VBI data slicer 525 lines, line 21 is closed caption */
		{ 0x54, 2, { 0x4d, 0x00 } },
		{ 0x5a, 2, { 0x06, 0x83 } },

		/* PLL2 525 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xad } },

		/* Pulse generator 525 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xad } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_svcd_pal = {
	13,
	{
		/*
		 * Input window = 720 x 288, output window = 480 x 288 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 2 and active video starting at line 25 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * PAL active video should actually start at line 24, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x17, 0x00, 0x22, 0x01,
			      0xe0, 0x01, 0x20, 0x01 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x01, 0x00, 0x00 } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x00, 0x06, 0x00 } },
		{ 0xdc, 3, { 0x00, 0x03, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 17, output window = 1440 x 17.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x04, 0x00, 0x11, 0x00,
			      0xa0, 0x05, 0x11, 0x00 } },

		/* Inverted VGATE start at line 25, stop after line 313 */
		{ 0x15, 2, { 0x37, 0x17 } },

		/* VBI data slicer 625 lines, line 22 is closed caption */
		{ 0x54, 2, { 0xdd, 0x4d } },
		{ 0x5a, 2, { 0x03, 0x03 } },

		/* PLL2 625 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xb0 } },

		/* Pulse generator 625 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xb0 } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_dvd_ntsc = {
	13,
	{
		/*
		 * Input window = 720 x 240, output window = 720 x 240 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 5 and active video starting at line 23 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * NTSC active video should actually start at line 22, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x12, 0x00, 0xf2, 0x00,
			      0xd0, 0x02, 0xf0, 0x00 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x01, 0x00, 0x00 } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x00, 0x04, 0x00 } },
		{ 0xdc, 3, { 0x00, 0x02, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 12, output window = 1440 x 12.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x05, 0x00, 0x0c, 0x00,
			      0xa0, 0x05, 0x0c, 0x00 } },

		/* Inverted VGATE start at line 23, stop after line 263 */
		{ 0x15, 2, { 0x02, 0x12 } },

		/* VBI data slicer 525 lines, line 21 is closed caption */
		{ 0x54, 2, { 0x4d, 0x00 } },
		{ 0x5a, 2, { 0x06, 0x83 } },

		/* PLL2 525 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xad } },

		/* Pulse generator 525 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xad } }
	}
};

static const struct cxm_saa7115_command
saa7115_scaler_dvd_pal = {
	13,
	{
		/*
		 * Input window = 720 x 288, output window = 720 x 288 with
		 * YS extended by 2 as per section 17.4 of the data sheet
		 * and YO accounting for scaler processing triggering at
		 * line 2 and active video starting at line 25 (see section
		 * 8.2 table 8 and section 8.3.1.1 table 11 of the data sheet).
		 * PAL active video should actually start at line 24, however
		 * not all channels / programs do.
		 */
		{ 0xc4, 12, { 0x02, 0x00, 0xd0, 0x02, 0x17, 0x00, 0x22, 0x01,
			      0xd0, 0x02, 0x20, 0x01 } },

		/* Prefiltering and prescaling */
		{ 0xd0, 3, { 0x01, 0x00, 0x00 } },

		/* Brightness, contrast, and saturation */
		{ 0xd4, 3, { 0x80, 0x40, 0x40 } },

		/* Horizontal phase scaling */
		{ 0xd8, 3, { 0x00, 0x04, 0x00 } },
		{ 0xdc, 3, { 0x00, 0x02, 0x00 } },

		/* Vertical scaling */
		{ 0xe0, 5, { 0x00, 0x04, 0x00, 0x04, 0x00 } },

		/* Vertical phase offsets */
		{ 0xe8, 8, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }},

		/*
		 * VBI input window = 720 x 17, output window = 1440 x 17.
		 */
		{ 0x94, 12, { 0x02, 0x00, 0xd0, 0x02, 0x04, 0x00, 0x11, 0x00,
			      0xa0, 0x05, 0x11, 0x00 } },

		/* Inverted VGATE start at line 25, stop after line 313 */
		{ 0x15, 2, { 0x37, 0x17 } },

		/* VBI data slicer 625 lines, line 22 is closed caption */
		{ 0x54, 2, { 0xdd, 0x4d } },
		{ 0x5a, 2, { 0x03, 0x03 } },

		/* PLL2 625 lines, 27 Mhz target clock */
		{ 0xf0, 1, { 0xb0 } },

		/* Pulse generator 625 lines, 27 Mhz target clock */
		{ 0xf5, 1, { 0xb0 } }
	}
};


static const struct cxm_saa7115_audio_clock
saa7115_audio_clock[] = {
	{ 44100, 30, &saa7115_audio_clock_44100_ntsc },
	{ 44100, 25, &saa7115_audio_clock_44100_pal },
	{ 48000, 30, &saa7115_audio_clock_48000_ntsc },
	{ 48000, 25, &saa7115_audio_clock_48000_pal }
};

static const struct cxm_saa7115_scaling
saa7115_scalings[] = {
	{ 352, 480, 30, &saa7115_scaler_vcd_ntsc_double_lines },
	{ 352, 576, 25, &saa7115_scaler_vcd_pal_double_lines },
	{ 480, 480, 30, &saa7115_scaler_svcd_ntsc },
	{ 480, 576, 25, &saa7115_scaler_svcd_pal },
	{ 720, 480, 30, &saa7115_scaler_dvd_ntsc },
	{ 720, 576, 25, &saa7115_scaler_dvd_pal }
};


/* Reset the SAA7115 chip */
static int
cxm_saa7115_reset(device_t iicbus, int i2c_addr)
{
	unsigned char msg[2];
	int sent;

	/* put into reset mode */
	msg[0] = 0x88;
	msg[1] = 0x0b;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	iicbus_stop(iicbus);

	/* put back to operational mode */
	msg[0] = 0x88;
	msg[1] = 0x2b;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	iicbus_stop(iicbus);

	return 0;

fail:
	iicbus_stop(iicbus);
	return -1;
}


/* Read from the SAA7115 registers */
static int
cxm_saa7115_read(device_t iicbus, int i2c_addr,
		  unsigned char addr, char *buf, int len)
{
	unsigned char msg[1];
	int received;
	int sent;

	msg[0] = addr;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	if (iicbus_repeated_start(iicbus, i2c_addr + 1, CXM_I2C_TIMEOUT) != 0)
		goto fail;

	if (iicbus_read(iicbus, buf, len, &received, IIC_LAST_READ, 0) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return received;

fail:
	iicbus_stop(iicbus);
	return -1;
}


/* Write to the SAA7115 registers */
static int
cxm_saa7115_write(device_t iicbus, int i2c_addr,
		   unsigned char addr, const char *buf, int len)
{
	unsigned char msg[1];
	int sent;

	msg[0] = addr;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	if (iicbus_write(iicbus, buf, len, &sent, CXM_I2C_TIMEOUT) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return sent;

fail:
	iicbus_stop(iicbus);
	return -1;
}


int
cxm_saa7115_init(struct cxm_softc *sc)
{
	char name[5];
	unsigned char id[1];
	unsigned char rev;
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_saa7115_setting *settings;

	if (cxm_saa7115_reset (sc->iicbus, CXM_I2C_SAA7115) < 0)
		return -1;

	name[4] = '\0';
	for (i = 0; i < 4; i++) {
		id[0] = 2 + i;

		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x00,
				      id, sizeof(id)) != sizeof(id))
			return -1;

		if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x00,
				     id, sizeof(id)) != sizeof(id))
			return -1;

		name[i] = '0' + (id[0] & 0x0f);
		rev = id[0] >> 4;
	}

	/*
	 * SAA 7115 is the only video decoder currently supported.
	 */

	nsettings = 0;
	settings = NULL;

	if (strcmp(name, "7115") == 0) {
		nsettings = saa7115_init.nsettings;
		settings = saa7115_init.settings;
	} else {
		device_printf(sc->dev, "unknown video decoder SAA%s\n", name);
		return -1;
	}

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	if (cxm_saa7115_select_source(sc, cxm_tuner_source) < 0)
		return -1;

	device_printf(sc->dev, "SAA%s rev %u video decoder\n",
	    name, (unsigned int)rev);

	return 0;
}


int
cxm_saa7115_mute(struct cxm_softc *sc)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_saa7115_setting *settings;

	nsettings = saa7115_mute.nsettings;
	settings = saa7115_mute.settings;

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	return 0;
}


int
cxm_saa7115_unmute(struct cxm_softc *sc)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_saa7115_setting *settings;

	nsettings = saa7115_unmute.nsettings;
	settings = saa7115_unmute.settings;

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	return 0;
}


int
cxm_saa7115_select_source(struct cxm_softc *sc, enum cxm_source source)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_saa7115_setting *settings;

	switch (source) {
	case cxm_fm_source:
		nsettings = saa7115_select_fm.nsettings;
		settings = saa7115_select_fm.settings;
		break;

	case cxm_line_in_source_composite:
		nsettings = saa7115_select_line_in_composite.nsettings;
		settings = saa7115_select_line_in_composite.settings;
		break;

	case cxm_line_in_source_svideo:
		nsettings = saa7115_select_line_in_svideo.nsettings;
		settings = saa7115_select_line_in_svideo.settings;
		break;

	case cxm_tuner_source:
		nsettings = saa7115_select_tuner.nsettings;
		settings = saa7115_select_tuner.settings;
		break;

	default:
		return -1;
	}

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	return 0;
}


int
cxm_saa7115_configure(struct cxm_softc *sc,
		       unsigned int width, unsigned int height,
		       unsigned int fps, unsigned int audio_sample_rate)
{
	unsigned char power[1];
	unsigned char task[1];
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_saa7115_setting *settings;

	for (i = 0; NUM_ELEMENTS(saa7115_scalings); i++)
		if (saa7115_scalings[i].width == width
		    && saa7115_scalings[i].height == height
		    && saa7115_scalings[i].fps == fps)
			break;

	if (i >= NUM_ELEMENTS(saa7115_scalings))
		return -1;

	nsettings = saa7115_scalings[i].scaling->nsettings;
	settings = saa7115_scalings[i].scaling->settings;

	/*
	 * Reset the scaler.
	 */

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x88,
			     power, sizeof(power)) != sizeof(power))
		return -1;

	power[0] &= ~0x20;

	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x88,
			      power, sizeof(power)) != sizeof(power))
		return -1;

	/*
	 * Configure the scaler.
	 */

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	/*
	 * Enable task register set A and B.
	 */

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x80,
			     task, sizeof(task)) != sizeof(task))
		return -1;

	task[0] |= 0x30;

	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x80,
			      task, sizeof(task)) != sizeof(task))
		return -1;

	/*
	 * Enable the scaler.
	 */

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x88,
			     power, sizeof(power)) != sizeof(power))
		return -1;

	power[0] |= 0x20;

	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x88,
			      power, sizeof(power)) != sizeof(power))
		return -1;

	/*
	 * Configure the audio clock.
	 */

	for (i = 0; NUM_ELEMENTS(saa7115_audio_clock); i++)
		if (saa7115_audio_clock[i].sample_rate == audio_sample_rate
		    && saa7115_audio_clock[i].fps == fps)
			break;

	if (i >= NUM_ELEMENTS(saa7115_audio_clock))
		return -1;

	nsettings = saa7115_audio_clock[i].clock->nsettings;
	settings = saa7115_audio_clock[i].clock->settings;

	for (i = 0; i < nsettings; i++)
		if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115,
				      settings[i].addr,
				      settings[i].values, settings[i].nvalues)
		    != settings[i].nvalues)
			return -1;

	return 0;
}


enum cxm_source_format
cxm_saa7115_detected_format(struct cxm_softc *sc)
{
	unsigned char status[2];
	enum cxm_source_format source_format;

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x1e,
			     status, sizeof(status)) != sizeof(status))
		return cxm_unknown_source_format;

	if (!(status[1] & 0x01)) {
		device_printf(sc->dev, "video decoder isn't locked\n");
		return cxm_unknown_source_format;
	}

	source_format = cxm_unknown_source_format;

	if (!(status[1] & 0x20)) {
		switch (status[0] & 0x03) {
		case 0:
			source_format = cxm_bw_50hz_source_format;
			break;

		case 1:
			source_format = cxm_ntsc_50hz_source_format;
			break;

		case 2:
			source_format = cxm_pal_50hz_source_format;
			break;

		case 3:
			source_format = cxm_secam_50hz_source_format;
			break;

		default:
			break;
		}
	} else {
		switch (status[0] & 0x03) {
		case 0:
			source_format = cxm_bw_60hz_source_format;
			break;

		case 1:
			source_format = cxm_ntsc_60hz_source_format;
			break;

		case 2:
			source_format = cxm_pal_60hz_source_format;
			break;

		default:
			break;
		}
	}

	return source_format;
}


int
cxm_saa7115_detected_fps(struct cxm_softc *sc)
{
	unsigned char status[1];

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x1f,
			     status, sizeof(status)) != sizeof(status))
		return -1;

	if (!(status[0] & 0x01)) {
		device_printf(sc->dev, "video decoder isn't locked\n");
		return -1;
	}

	return (status[0] & 0x20) ? 30 : 25;
}


int
cxm_saa7115_get_brightness(struct cxm_softc *sc)
{
	unsigned char brightness;

	/*
	 * Brightness is treated as an unsigned value by the decoder.
	 * 0 = dark, 128 = ITU level, 255 = bright
	 */
	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x0a,
			     &brightness, sizeof(brightness))
	    != sizeof(brightness))
		return -1;

	return brightness;
}


int
cxm_saa7115_set_brightness(struct cxm_softc *sc, unsigned char brightness)
{

	/*
	 * Brightness is treated as an unsigned value by the decoder.
	 * 0 = dark, 128 = ITU level, 255 = bright
	 */
	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x0a,
			      &brightness, sizeof(brightness))
	    != sizeof(brightness))
		return -1;

	return 0;
}


int
cxm_saa7115_get_chroma_saturation(struct cxm_softc *sc)
{
	unsigned char chroma_saturation;

	/*
	 * Chroma saturation is treated as a signed value by the decoder.
	 * -128 = -2.0 (inverse chrominance), -64 = 1.0 (inverse chrominance),
	 * 0 = 0 (color off), 64 = 1.0 (ITU level), 127 = 1.984 (maximum)
	 */
	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x0c,
			     &chroma_saturation, sizeof(chroma_saturation))
	    != sizeof(chroma_saturation))
		return -1;

	return chroma_saturation;
}


int
cxm_saa7115_set_chroma_saturation(struct cxm_softc *sc,
				   unsigned char chroma_saturation)
{

	/*
	 * Chroma saturation is treated as a signed value by the decoder.
	 * -128 = -2.0 (inverse chrominance), -64 = 1.0 (inverse chrominance),
	 * 0 = 0 (color off), 64 = 1.0 (ITU level), 127 = 1.984 (maximum)
	 */
	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x0c,
			      &chroma_saturation, sizeof(chroma_saturation))
	    != sizeof(chroma_saturation))
		return -1;

	return 0;
}


int
cxm_saa7115_get_contrast(struct cxm_softc *sc)
{
	unsigned char contrast;

	/*
	 * Contrast is treated as a signed value by the decoder.
	 * -128 = -2.0 (inverse luminance), -64 = 1.0 (inverse luminance),
	 * 0 = 0 (luminance off), 64 = 1.0, 68 = 1.063 (ITU level),
	 * 127 = 1.984 (maximum)
	 */
	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x0b,
			     &contrast, sizeof(contrast)) != sizeof(contrast))
		return -1;

	return contrast;
}


int
cxm_saa7115_set_contrast(struct cxm_softc *sc, unsigned char contrast)
{

	/*
	 * Contrast is treated as a signed value by the decoder.
	 * -128 = -2.0 (inverse luminance), -64 = 1.0 (inverse luminance),
	 * 0 = 0 (luminance off), 64 = 1.0, 68 = 1.063 (ITU level),
	 * 127 = 1.984 (maximum)
	 */
	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x0b,
			      &contrast, sizeof(contrast)) != sizeof(contrast))
		return -1;

	return 0;
}


int
cxm_saa7115_get_hue(struct cxm_softc *sc)
{
	unsigned char hue;

	/*
	 * Hue is treated as a signed value by the decoder.
	 * -128 = -180.0, 0 = 0.0, 127 = +178.6
	 */
	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x0d,
			     &hue, sizeof(hue))
	    != sizeof(hue))
		return -1;

	return hue;
}


int
cxm_saa7115_set_hue(struct cxm_softc *sc, unsigned char hue)
{

	/*
	 * Hue is treated as a signed value by the decoder.
	 * -128 = -180.0, 0 = 0.0, 127 = +178.6
	 */
	if (cxm_saa7115_write(sc->iicbus, CXM_I2C_SAA7115, 0x0d,
			      &hue, sizeof(hue))
	    != sizeof(hue))
		return -1;

	return 0;
}


int
cxm_saa7115_is_locked(struct cxm_softc *sc)
{
	unsigned char status[1];

	if (cxm_saa7115_read(sc->iicbus, CXM_I2C_SAA7115, 0x1f,
			     status, sizeof(status)) != sizeof(status))
		return -1;

	return (status[0] & 0x01) ? 1 : 0;
}


int
cxm_saa7115_wait_for_lock(struct cxm_softc *sc)
{
	unsigned int i;

	/*
	 * Section 2.7 of the data sheet states:
	 *
	 *   Ultra-fast frame lock (almost 1 field)
	 *
	 * so hopefully 500 ms is enough (the lock
	 * sometimes takes a long time to occur ...
	 * possibly due to the time it takes to
	 * autodetect the format).
	 */

	for (i = 0; i < 10; i++) {

		/*
		 * The input may have just changed (prior to
		 * cxm_saa7115_wait_for_lock) so start with
		 * the delay to give the video decoder a
		 * chance to update its status.
		 */

		tsleep(&sc->iicbus, 0, "video", hz / 20);

		switch (cxm_saa7115_is_locked(sc)) {
		case 1:
			return 1;

		case 0:
			break;

		default:
			return -1;
		}
	}

	device_printf(sc->dev, "video decoder failed to lock\n");

	return 0;
}
