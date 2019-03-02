/*-
 * Copyright (c) 2014 Imre Vadász <imre@vdsz.com>
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>.
 *
 * Simple font scaling code by Sascha Wildner and Matthew Dillon
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/thread.h>

#include <machine/console.h>
#include <machine/framebuffer.h>

#include "syscons.h"

#include <bus/isa/isareg.h>

static vr_draw_border_t		kms_draw_border;
static vr_draw_t		kms_draw;
static vr_draw_cursor_t		kms_cursor;
static vr_blink_cursor_t	kms_blink;
#ifndef SC_NO_CUTPASTE
static vr_draw_mouse_t		kms_mouse;
#endif

static void			kms_nop(scr_stat *scp, ...);

static sc_rndr_sw_t kmsrndrsw = {
	kms_draw_border,
	kms_draw,
	(vr_set_cursor_t *)kms_nop,
	kms_cursor,
	kms_blink,
#ifndef SC_NO_CUTPASTE
	kms_mouse,
#else
	(vr_draw_mouse_t *)kms_nop,
#endif
};
RENDERER(kms, V_INFO_MM_TEXT, kmsrndrsw, kms_set);

#ifndef SC_NO_MODE_CHANGE
static sc_rndr_sw_t grrndrsw = {
	(vr_draw_border_t *)kms_nop,
	(vr_draw_t *)kms_nop,
	(vr_set_cursor_t *)kms_nop,
	(vr_draw_cursor_t *)kms_nop,
	(vr_blink_cursor_t *)kms_nop,
	(vr_draw_mouse_t *)kms_nop,
};
RENDERER(kms, V_INFO_MM_OTHER, grrndrsw, kms_set);
#endif /* SC_NO_MODE_CHANGE */

RENDERER_MODULE(kms, kms_set);

static uint32_t colormap[16] = {
	0x00000000,	/* BLACK */
	0x000000aa,	/* BLUE */
	0x0000aa00,	/* GREEN */
	0x0000aaaa,	/* CYAN */
	0x00aa0000,	/* RED */
	0x00aa00aa,	/* MAGENTA */
	0x00aa5500,	/* BROWN */
	0x00aaaaaa,	/* WHITE */
	0x00555555,	/* HIGHLIGHT BLACK */
	0x005555ff,	/* HIGHLIGHT BLUE */
	0x0055ff55,	/* HIGHLIGHT GREEN */
	0x0055ffff,	/* HIGHLIGHT CYAN */
	0x00ff5555,	/* HIGHLIGHT RED */
	0x00ff55ff,	/* HIGHLIGHT MAGENTA */
	0x00ffff55,	/* HIGHLIGHT BROWN */
	0x00ffffff,	/* HIGHLIGHT WHITE */
};

#ifndef SC_NO_CUTPASTE
static u_short mouse_and_mask[16] = {
	0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00, 0xff80,
	0xfe00, 0x1e00, 0x1f00, 0x0f00, 0x0f00, 0x0000, 0x0000, 0x0000
};
static u_short mouse_or_mask[16] = {
	0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x6800,
	0x0c00, 0x0c00, 0x0600, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000
};
#endif

static void
kms_nop(scr_stat *scp, ...)
{
}

/*
 * Scaled font rendering.  Simple blit blitter copy operation with bitmap
 * scaling.  Scales the bitmap char_data(sw x sh) to the output bitmap
 * draw_pos(dw x dh).
 *
 * This function does not do fractional scaling.
 *
 * SET	- Sets both the fg and bg pen
 *
 * MASK	- Sets only the fg pen based on the source mask and leaves
 *	  the background untouched.
 */
#define BLIT_SET	0
#define BLIT_MASK	1

static inline void
blit_blk32(scr_stat *scp, u_char *char_data, int sw, int sh,
	   vm_offset_t draw_pos, int dw, int dh,
	   int line_width, uint32_t fg, uint32_t bg, int how)
{
	vm_offset_t p;
	int pos;
	int x;		/* destination iterator (whole pixels) */
	int y;
	int sx, sx_inc;	/* source iterator (fractional) */
	int sy, sy_inc;
	uint8_t c;

	/*
	 * Calculate fractional iterator for source
	 */
	if (dw)
		sx_inc = (sw << 16) / dw;
	else
		sx_inc = 0;

	if (dh)
		sy_inc = (sh << 16) / dh;
	else
		sy_inc = 0;

	sy = 0;
	c = 0;

	/*
	 * For each pixel row in the target
	 */
	for (y = 0; y < dh; ++y) {
		sx = 0;
		p = draw_pos;

		/*
		 * Render all pixel columns in the target by calculating
		 * which bit in the source is applicable.
		 */
		switch(how) {
		case BLIT_SET:
			for (x = 0; x < dw; ++x) {
				if ((sx & 0x00070000) == 0)
					c = char_data[sx >> 19];
				pos = ~(sx >> 16) & 7;
				writel(p + x * 4, (c & (1 << pos) ? fg : bg));
				sx += sx_inc;
			}
			break;
		case BLIT_MASK:
			for (x = 0; x < dw; ++x) {
				if ((sx & 0x00070000) == 0)
					c = char_data[sx >> 19];
				pos = ~(sx >> 16) & 7;
				if (c & (1 << pos))
					writel(p + x * 4, fg);
				sx += sx_inc;
			}
			break;
		}
		draw_pos += line_width;
		sy += sy_inc;
		if (sy >= 0x10000) {
			char_data += (sy >> 16) * (sw >> 3);
			sy &= 0x0FFFF;
		}
	}
}

static inline void
blit_blk24(scr_stat *scp, u_char *char_data, int sw, int sh,
	   vm_offset_t draw_pos, int dw, int dh,
	   int line_width, uint32_t fg, uint32_t bg, int how)
{
	vm_offset_t p;
	int pos;
	int x;		/* destination iterator (whole pixels) */
	int y;
	int sx, sx_inc;	/* source iterator (fractional) */
	int sy, sy_inc;
	uint8_t c;

	/*
	 * Calculate fractional iterator for source
	 */
	if (dw)
		sx_inc = (sw << 16) / dw;
	else
		sx_inc = 0;

	if (dh)
		sy_inc = (sh << 16) / dh;
	else
		sy_inc = 0;

	sy = 0;
	c = 0;

	/*
	 * For each pixel row in the target
	 */
	for (y = 0; y < dh; ++y) {
		sx = 0;
		p = draw_pos;

		/*
		 * Render all pixel columns in the target by calculating
		 * which bit in the source is applicable.
		 */
		switch(how) {
		case BLIT_SET:
			for (x = 0; x < dw; ++x) {
				if ((sx & 0x00070000) == 0)
					c = char_data[sx >> 19];
				pos = ~(sx >> 16) & 7;
				uint32_t col = c & (1 << pos) ? fg : bg;
				writeb(p, col >> 16);
				writeb(p + 1, col >> 8);
				writeb(p + 2, col >> 0);
				p += 3;
				sx += sx_inc;
			}
			break;
		case BLIT_MASK:
			for (x = 0; x < dw; ++x) {
				if ((sx & 0x00070000) == 0)
					c = char_data[sx >> 19];
				pos = ~(sx >> 16) & 7;
				if (c & (1 << pos)) {
					writeb(p, fg >> 16);
					writeb(p + 1, fg >> 8);
					writeb(p + 2, fg >> 0);
				}
				p += 3;
				sx += sx_inc;
			}
			break;
		}
		draw_pos += line_width;
		sy += sy_inc;
		if (sy >= 0x10000) {
			char_data += (sy >> 16) * (sw >> 3);
			sy &= 0x0FFFF;
		}
	}
}

static void
fill_rect32(scr_stat *scp, vm_offset_t draw_pos, int width, int height,
	    int line_width, uint32_t fg)
{
	int i, j;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++)
			writel(draw_pos + j * 4, fg);
		draw_pos += line_width;
	}
}

static void
fill_rect24(scr_stat *scp, vm_offset_t draw_pos, int width, int height,
	    int line_width, uint32_t fg)
{
	int i, j, d;

	d = line_width - width * 3;
	KKASSERT(d >= 0);

	if ((draw_pos & 0x3) == 0 && (line_width & 0x3) == 0 &&
	    (width & 0x3) == 0) {
		uint32_t fga = fg | (fg << 24);
		uint32_t fgb = (fg << 16) | ((fg >> 8) & 0xffff);
		uint32_t fgc = (fg << 8) | ((fg >> 16) & 0xff);
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j += 4) {
				writel(draw_pos, fga);
				writel(draw_pos + 4, fgb);
				writel(draw_pos + 8, fgc);
				draw_pos += 12;
			}
			draw_pos += d;
		}
	} else {
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
				writeb(draw_pos, fg >> 16);
				writeb(draw_pos + 1, fg >> 8);
				writeb(draw_pos + 2, fg >> 0);
				draw_pos += 3;
			}
			draw_pos += line_width;
		}
	}
}

/* KMS renderer */

static void
kms_draw_border(scr_stat *scp, int color)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size;
	int rightpixel, bottompixel;
	uint32_t fg;
	vm_offset_t draw_pos;

	if (sc->fbi->vaddr == 0)
		return;

	fg = colormap[color];
	line_width = sc->fbi->stride;
	/* sc->fbi->depth may only be 24 or 32 */
	pixel_size = sc->fbi->depth == 24 ? 3 : 4;
	rightpixel = sc->fbi->width - scp->xsize * scp->blk_width;
	bottompixel = sc->fbi->height - scp->ysize * scp->blk_height;

	draw_pos = sc->fbi->vaddr + scp->blk_width * pixel_size * scp->xsize;
	if (pixel_size == 3) {
		fill_rect24(scp, draw_pos, rightpixel,
		    scp->blk_height * scp->ysize, line_width, fg);
	} else {
		fill_rect32(scp, draw_pos, rightpixel,
		    scp->blk_height * scp->ysize, line_width, fg);
	}

	draw_pos = sc->fbi->vaddr + scp->blk_height * scp->ysize * line_width;
	if (pixel_size == 3) {
		fill_rect24(scp, draw_pos, sc->fbi->width,
		    sc->fbi->height - scp->blk_height * scp->ysize, line_width,
		    fg);
	} else {
		fill_rect32(scp, draw_pos, sc->fbi->width,
		    sc->fbi->height - scp->blk_height * scp->ysize, line_width,
		    fg);
	}
}

static void
kms_draw(scr_stat *scp, int from, int count, int flip)
{
	sc_softc_t *sc = scp->sc;
	u_char *char_data;
	int a, i;
	uint32_t fg, bg;
	vm_offset_t draw_pos, p;
	int line_width, pixel_size;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	/* sc->fbi->depth may only be 24 or 32 */
	pixel_size = sc->fbi->depth == 24 ? 3 : 4;

	draw_pos = sc->fbi->vaddr +
	    scp->blk_height * line_width * (from / scp->xsize);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	p = draw_pos + scp->blk_width * pixel_size * (from % scp->xsize);
	for (i = from; count-- > 0; i++) {
		char_data = &(scp->font[sc_vtb_getc(&scp->vtb, i) *
					scp->font_height]);

		a = sc_vtb_geta(&scp->vtb, i);
		if (flip) {
			fg = colormap[((a & 0xf000) >> 4) >> 8];
			bg = colormap[(a & 0x0f00) >> 8];
		} else {
			fg = colormap[(a & 0x0f00) >> 8];
			bg = colormap[((a & 0xf000) >> 4) >> 8];
		}
		if (pixel_size == 3) {
			blit_blk24(scp, char_data, scp->font_width,
				   scp->font_height, p, scp->blk_width,
				   scp->blk_height, line_width, fg, bg,
				   BLIT_SET);
		} else {
			blit_blk32(scp, char_data, scp->font_width,
				   scp->font_height, p, scp->blk_width,
				   scp->blk_height, line_width, fg, bg,
				   BLIT_SET);
		}
		p += scp->blk_width * pixel_size;
		if ((i % scp->xsize) == scp->xsize - 1) {
			draw_pos += scp->blk_height * line_width;
			p = draw_pos;
		}
	}
}

static void
draw_kmscursor(scr_stat *scp, int at, int on, int flip)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size;
	int cursor_base;
	int blk_base;
	int a;
	uint32_t fg, bg;
	unsigned char *char_data;
	vm_offset_t draw_pos;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	/* sc->fbi->depth may only be 24 or 32 */
	pixel_size = sc->fbi->depth == 24 ? 3 : 4;
	cursor_base = /* scp->font_height - */ scp->cursor_base;
	blk_base = scp->blk_height * cursor_base / scp->font_height;

	draw_pos = sc->fbi->vaddr +
	    scp->blk_width * pixel_size * (at % scp->xsize) +
	    scp->blk_height * line_width * (at / scp->xsize) +
	    blk_base * line_width;

	a = sc_vtb_geta(&scp->vtb, at);
	if (flip) {
		fg = colormap[((on) ? (a & 0x0f00) :
		    ((a & 0xf000) >> 4)) >> 8];
		bg = colormap[((on) ? ((a & 0xf000) >> 4) :
		    (a & 0x0f00)) >> 8];
	} else {
		fg = colormap[((on) ? ((a & 0xf000) >> 4) :
		    (a & 0x0f00)) >> 8];
		bg = colormap[((on) ? (a & 0x0f00) :
		    ((a & 0xf000) >> 4)) >> 8];
	}

	char_data = &scp->font[sc_vtb_getc(&scp->vtb, at) * scp->font_height];
	char_data += cursor_base;

	if (pixel_size == 3) {
		blit_blk24(scp, char_data,
			   scp->font_width, scp->font_height - cursor_base,
			   draw_pos, scp->blk_width, scp->blk_height - blk_base,
			   line_width, fg, bg, BLIT_SET);
	} else {
		blit_blk32(scp, char_data,
			   scp->font_width, scp->font_height - cursor_base,
			   draw_pos, scp->blk_width, scp->blk_height - blk_base,
			   line_width, fg, bg, BLIT_SET);
	}
}

static int pxlblinkrate = 0;

static void
kms_cursor(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->cursor_height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_kmscursor(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_kmscursor(scp, at,
			    scp->status & VR_CURSOR_ON, flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_kmscursor(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void
kms_blink(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_kmscursor(scp, at, scp->status & VR_CURSOR_ON, flip);
}

#ifndef SC_NO_CUTPASTE

static void
draw_kmsmouse(scr_stat *scp, int x, int y)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size;
	int blk_width, blk_height;
	vm_offset_t draw_pos;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	/* sc->fbi->depth may only be 24 or 32 */
	pixel_size = sc->fbi->depth == 24 ? 3 : 4;

	if (x + scp->font_width < scp->font_width * scp->xsize)
		blk_width = scp->blk_width;
	else
		blk_width = scp->font_width * scp->xsize - x;

	if (y + scp->font_height < scp->font_height * scp->ysize)
		blk_height = scp->blk_height;
	else
		blk_height = scp->font_height * scp->ysize - y;

	draw_pos = sc->fbi->vaddr + y * scp->blk_height / scp->font_height *
		   line_width +
		   x * scp->blk_width / scp->font_width * pixel_size;
	if (pixel_size == 3) {
		blit_blk24(scp, (unsigned char *)mouse_and_mask, 16, 16,
			   draw_pos, blk_width, blk_height, line_width,
			   colormap[0], 0, BLIT_MASK);
		blit_blk24(scp, (unsigned char *)mouse_or_mask, 16, 16,
			   draw_pos, blk_width, blk_height, line_width,
			   colormap[15], 0, BLIT_MASK);
	} else {
		blit_blk32(scp, (unsigned char *)mouse_and_mask, 16, 16,
			   draw_pos, blk_width, blk_height, line_width,
			   colormap[0], 0, BLIT_MASK);
		blit_blk32(scp, (unsigned char *)mouse_or_mask, 16, 16,
			   draw_pos, blk_width, blk_height, line_width,
			   colormap[15], 0, BLIT_MASK);
	}
}

static void
remove_kmsmouse(scr_stat *scp, int x, int y)
{
	int col, row;
	int pos;
	int i;

	/* erase the mouse cursor image */
	col = x / scp->font_width - scp->xoff;
	row = y / scp->font_height - scp->yoff;
	pos = row * scp->xsize + col;
	i = (col < scp->xsize - 1) ? 2 : 1;
	(*scp->rndr->draw)(scp, pos, i, FALSE);
	if (row < scp->ysize - 1)
		(*scp->rndr->draw)(scp, pos + scp->xsize, i, FALSE);
}

static void
kms_mouse(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_kmsmouse(scp, x, y);
	else
		remove_kmsmouse(scp, x, y);
}

#endif /* SC_NO_CUTPASTE */
