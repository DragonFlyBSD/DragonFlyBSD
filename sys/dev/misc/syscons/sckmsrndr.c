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
#include "font_ext.h"

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

static uint32_t colormap24[16] = {
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

static uint16_t colormap565[16] = {
	0x0000,	/* BLACK */
	0x0015,	/* BLUE */
	0x0540,	/* GREEN */
	0x0555,	/* CYAN */
	0xA800,	/* RED */
	0xA815,	/* MAGENTA */
	0xAAA0,	/* BROWN */
	0xAD55,	/* WHITE */
	0x52AA,	/* HIGHLIGHT BLACK */
	0x52BF,	/* HIGHLIGHT BLUE */
	0x57EA,	/* HIGHLIGHT GREEN */
	0x57FF,	/* HIGHLIGHT CYAN */
	0xFAAA,	/* HIGHLIGHT RED */
	0xFCBF,	/* HIGHLIGHT MAGENTA */
	0xFFEA,	/* HIGHLIGHT BROWN */
	0xffff,	/* HIGHLIGHT WHITE */
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

static inline int
get_pixel_size(uint16_t depth)
{
	return depth / 8;
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

/* Bytes per glyph row / glyph for variable-width bitmap fonts (KMS). */
static inline int
sc_font_bpr(const scr_stat *scp)
{
	return ((scp->font_width + 7) >> 3);
}

static inline u_char *
sc_font_glyph(scr_stat *scp, int c)
{
	int bpr = sc_font_bpr(scp);
	const u_char *ext;
	int ew, eh;

	if (c >= SC_GLYPH_EXT_BASE) {
		ext = sc_ext_glyph_bits(c, &ew, &eh);
		if (ext != NULL &&
		    ew == scp->font_width && eh == scp->font_height)
			return ((u_char *)(uintptr_t)ext);
		/* Wrong metrics or missing: blank from base font. */
		c = 0x20;
	}
	if (c < 0)
		c = 0x20;
	if (scp->font == NULL)
		return (NULL);
	/* Base Spleen / VGA font plane is 0..255. */
	if (c > 255)
		c = 0x20;
	return (&scp->font[c * scp->font_height * bpr]);
}



static inline void
blit_blk32(scr_stat *scp, u_char *char_data, int sw, int sh,
	   vm_offset_t draw_pos, int dw, int dh,
	   int line_width, uint8_t fgidx, uint8_t bgidx, int how)
{
	vm_offset_t p;
	int pos;
	int x;		/* destination iterator (whole pixels) */
	int y;
	int sx, sx_inc;	/* source iterator (fractional) */
	int sy, sy_inc;
	uint8_t c;
	uint32_t fg = colormap24[fgidx];
	uint32_t bg = colormap24[bgidx];

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
	   int line_width, uint8_t fgidx, uint8_t bgidx, int how)
{
	vm_offset_t p;
	int pos;
	int x;		/* destination iterator (whole pixels) */
	int y;
	int sx, sx_inc;	/* source iterator (fractional) */
	int sy, sy_inc;
	uint8_t c;
	uint32_t fg = colormap24[fgidx];
	uint32_t bg = colormap24[bgidx];

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

static inline void
blit_blk16(scr_stat *scp, u_char *char_data, int sw, int sh,
	   vm_offset_t draw_pos, int dw, int dh,
	   int line_width, uint8_t fgidx, uint8_t bgidx, int how)
{
	vm_offset_t p;
	int pos;
	int x;		/* destination iterator (whole pixels) */
	int y;
	int sx, sx_inc;	/* source iterator (fractional) */
	int sy, sy_inc;
	uint8_t c;
	uint16_t fg = colormap565[fgidx];
	uint16_t bg = colormap565[bgidx];

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
				writew(p + x * 2, (c & (1 << pos) ? fg : bg));
				sx += sx_inc;
			}
			break;
		case BLIT_MASK:
			for (x = 0; x < dw; ++x) {
				if ((sx & 0x00070000) == 0)
					c = char_data[sx >> 19];
				pos = ~(sx >> 16) & 7;
				if (c & (1 << pos))
					writew(p + x * 2, fg);
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
blit_blk(scr_stat *scp, u_char *char_data, int sw, int sh,
	   vm_offset_t draw_pos, int dw, int dh,
	   int line_width, uint8_t fgidx, uint8_t bgidx, int how,
	   int pixel_size)
{
	if (pixel_size == 2) {
		blit_blk16(scp, char_data, sw, sh, draw_pos, dw, dh, line_width,
			   fgidx, bgidx, how);
	} else if (pixel_size == 3) {
		blit_blk24(scp, char_data, sw, sh, draw_pos, dw, dh, line_width,
			   fgidx, bgidx, how);
	} else {
		blit_blk32(scp, char_data, sw, sh, draw_pos, dw, dh, line_width,
			   fgidx, bgidx, how);
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

static void
fill_rect16(scr_stat *scp, vm_offset_t draw_pos, int width, int height,
	    int line_width, uint16_t fg)
{
	int i, j;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++)
			writew(draw_pos + j * 2, fg);
		draw_pos += line_width;
	}
}


/*
 * Draw an empty box (tofu) spanning ncells (1 or 2) starting at cell 'at'.
 * Border thickness scales with cell size so 8x16 and 16x32 both look clean.
 */
static void
kms_fill(scr_stat *scp, vm_offset_t pos, int width, int height,
    int line_width, int pixel_size, uint32_t color32, uint16_t color16)
{
	if (pixel_size == 2)
		fill_rect16(scp, pos, width, height, line_width, color16);
	else if (pixel_size == 3)
		fill_rect24(scp, pos, width, height, line_width, color32);
	else
		fill_rect32(scp, pos, width, height, line_width, color32);
}

static void
kms_draw_tofu_box(scr_stat *scp, int at, int ncells, int flip)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size;
	int col, row;
	int dw, dh, inset, t;
	int a;
	uint8_t fgidx, bgidx;
	uint32_t fg32, bg32;
	uint16_t fg16, bg16;
	vm_offset_t base, p;

	if (sc->fbi->vaddr == 0 || ncells < 1)
		return;
	if (at < 0 || at >= scp->xsize * scp->ysize)
		return;
	if (at % scp->xsize + ncells > scp->xsize)
		ncells = scp->xsize - (at % scp->xsize);
	if (ncells < 1)
		return;

	line_width = sc->fbi->stride;
	pixel_size = get_pixel_size(sc->fbi->depth);
	col = at % scp->xsize;
	row = at / scp->xsize;
	dw = scp->blk_width * ncells;
	dh = scp->blk_height;
	/* ~1/8 cell, at least 1px, at most 3px for fine Spleen cells */
	inset = scp->blk_width / 8;
	if (inset < 1)
		inset = 1;
	if (inset > 3)
		inset = 3;
	t = inset;	/* stroke thickness */

	a = sc_vtb_geta(&scp->vtb, at);
	if (flip) {
		fgidx = ((a & 0xf000) >> 4) >> 8;
		bgidx = (a & 0x0f00) >> 8;
	} else {
		fgidx = (a & 0x0f00) >> 8;
		bgidx = ((a & 0xf000) >> 4) >> 8;
	}
	fg32 = colormap24[fgidx & 0xf];
	bg32 = colormap24[bgidx & 0xf];
	fg16 = colormap565[fgidx & 0xf];
	bg16 = colormap565[bgidx & 0xf];

	base = sc->fbi->vaddr +
	    scp->blk_width * pixel_size * col +
	    scp->blk_height * line_width * row;

	/* background fill */
	kms_fill(scp, base, dw, dh, line_width, pixel_size, bg32, bg16);

	/* top */
	p = base + inset * line_width + inset * pixel_size;
	kms_fill(scp, p, dw - 2 * inset, t, line_width, pixel_size, fg32, fg16);
	/* bottom */
	p = base + (dh - inset - t) * line_width + inset * pixel_size;
	kms_fill(scp, p, dw - 2 * inset, t, line_width, pixel_size, fg32, fg16);
	/* left */
	p = base + inset * line_width + inset * pixel_size;
	kms_fill(scp, p, t, dh - 2 * inset, line_width, pixel_size, fg32, fg16);
	/* right */
	p = base + inset * line_width + (dw - inset - t) * pixel_size;
	kms_fill(scp, p, t, dh - 2 * inset, line_width, pixel_size, fg32, fg16);
}


/* KMS renderer */

static void
kms_draw_border(scr_stat *scp, int color)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size;
	int rightpixel, bottompixel;
	vm_offset_t draw_pos;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	pixel_size = get_pixel_size(sc->fbi->depth);
	rightpixel = sc->fbi->width - scp->xsize * scp->blk_width;
	bottompixel = sc->fbi->height - scp->ysize * scp->blk_height;

	draw_pos = sc->fbi->vaddr + scp->blk_width * pixel_size * scp->xsize;
	if (pixel_size == 2) {
		fill_rect16(scp, draw_pos, rightpixel,
		    scp->blk_height * scp->ysize, line_width,
		    colormap565[color]);
	} else if (pixel_size == 3) {
		fill_rect24(scp, draw_pos, rightpixel,
		    scp->blk_height * scp->ysize, line_width,
		    colormap24[color]);
	} else {
		fill_rect32(scp, draw_pos, rightpixel,
		    scp->blk_height * scp->ysize, line_width,
		    colormap24[color]);
	}

	draw_pos = sc->fbi->vaddr + scp->blk_height * scp->ysize * line_width;
	if (pixel_size == 2) {
		fill_rect16(scp, draw_pos, sc->fbi->width,
		    sc->fbi->height - scp->blk_height * scp->ysize, line_width,
		    colormap565[color]);
	} else if (pixel_size == 3) {
		fill_rect24(scp, draw_pos, sc->fbi->width,
		    sc->fbi->height - scp->blk_height * scp->ysize, line_width,
		    colormap24[color]);
	} else {
		fill_rect32(scp, draw_pos, sc->fbi->width,
		    sc->fbi->height - scp->blk_height * scp->ysize, line_width,
		    colormap24[color]);
	}
}

static void
kms_draw(scr_stat *scp, int from, int count, int flip)
{
	sc_softc_t *sc = scp->sc;
	u_char *char_data;
	int a, i;
	uint8_t fgidx, bgidx;
	vm_offset_t draw_pos, p;
	int line_width, pixel_size;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	pixel_size = get_pixel_size(sc->fbi->depth);

	draw_pos = sc->fbi->vaddr +
	    scp->blk_height * line_width * (from / scp->xsize);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	p = draw_pos + scp->blk_width * pixel_size * (from % scp->xsize);
	for (i = from; count-- > 0; i++) {
		int ncells = 1;

		/* Double-width CONT: tofu drawn with lead; emoji draws right half. */
		if (sc_utf8_enable && scp->uside != NULL &&
		    i < scp->xsize * scp->ysize &&
		    (scp->uside[i].flags & SC_UCELL_WIDE_CONT) &&
		    (scp->uside[i].flags & SC_UCELL_REPLACEMENT)) {
			if (i == from && i > 0 &&
			    (scp->uside[i - 1].flags & SC_UCELL_WIDE))
				kms_draw_tofu_box(scp, i - 1, 2, flip);
			p += scp->blk_width * pixel_size;
			if ((i % scp->xsize) == scp->xsize - 1) {
				draw_pos += scp->blk_height * line_width;
				p = draw_pos;
			}
			continue;
		}

		if (sc_utf8_enable && scp->uside != NULL &&
		    i < scp->xsize * scp->ysize &&
		    (scp->uside[i].flags & SC_UCELL_REPLACEMENT)) {
			int col0 = i % scp->xsize;

			ncells = (scp->uside[i].flags & SC_UCELL_WIDE) ? 2 : 1;
			kms_draw_tofu_box(scp, i, ncells, flip);
			if (ncells == 2 && count > 0 &&
			    i + 1 < scp->xsize * scp->ysize &&
			    (scp->uside[i + 1].flags & SC_UCELL_WIDE_CONT)) {
				i++;
				count--;
			}
			p += scp->blk_width * pixel_size * ncells;
			/* End of row after this cell or wide pair. */
			if (col0 + ncells >= scp->xsize) {
				draw_pos += scp->blk_height * line_width;
				p = draw_pos;
			}
			continue;
		}


		{
			int gch = sc_vtb_getc(&scp->vtb, i);
			/*
			 * Prefer uside only for true Unicode / wide / extension
			 * glyphs.  Pure ASCII follows vtb so libedit insert/delete
			 * and gen_print stay correct when uside is stale.
			 */
			if (sc_utf8_enable && scp->uside != NULL &&
			    i < scp->xsize * scp->ysize &&
			    !(scp->uside[i].flags & SC_UCELL_REPLACEMENT)) {
				sc_ucell_t *u = &scp->uside[i];
				if ((u->flags & (SC_UCELL_WIDE | SC_UCELL_WIDE_CONT)) ||
				    u->cp > 0x7f ||
				    (int)u->glyph >= SC_GLYPH_EXT_BASE)
					gch = (int)u->glyph;
			}
			char_data = sc_font_glyph(scp, gch);
			/*
			 * font is non-NULL on KMS; if missing, skip blit but
			 * still advance the draw cursor.
			 */
			if (char_data == NULL) {
				p += scp->blk_width * pixel_size;
				if ((i % scp->xsize) == scp->xsize - 1) {
					draw_pos += scp->blk_height * line_width;
					p = draw_pos;
				}
				continue;
			}
		}

		a = sc_vtb_geta(&scp->vtb, i);
		if (flip) {
			fgidx = ((a & 0xf000) >> 4) >> 8;
			bgidx = (a & 0x0f00) >> 8;
		} else {
			fgidx = (a & 0x0f00) >> 8;
			bgidx = ((a & 0xf000) >> 4) >> 8;
		}
		blit_blk(scp, char_data, scp->font_width, scp->font_height,
		    p, scp->blk_width, scp->blk_height, line_width,
		    fgidx, bgidx, BLIT_SET, pixel_size);

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
	uint8_t fgidx, bgidx;
	unsigned char *char_data;
	vm_offset_t draw_pos;

	if (sc->fbi->vaddr == 0)
		return;

	line_width = sc->fbi->stride;
	pixel_size = get_pixel_size(sc->fbi->depth);
	cursor_base = /* scp->font_height - */ scp->cursor_base;
	blk_base = scp->blk_height * cursor_base / scp->font_height;

	draw_pos = sc->fbi->vaddr +
	    scp->blk_width * pixel_size * (at % scp->xsize) +
	    scp->blk_height * line_width * (at / scp->xsize) +
	    blk_base * line_width;

	a = sc_vtb_geta(&scp->vtb, at);
	if (flip) {
		fgidx = ((on) ? (a & 0x0f00) : ((a & 0xf000) >> 4)) >> 8;
		bgidx = ((on) ? ((a & 0xf000) >> 4) : (a & 0x0f00)) >> 8;
	} else {
		fgidx = ((on) ? ((a & 0xf000) >> 4) : (a & 0x0f00)) >> 8;
		bgidx = ((on) ? (a & 0x0f00) : ((a & 0xf000) >> 4)) >> 8;
	}

	{
		int gch = sc_vtb_getc(&scp->vtb, at);
		if (sc_utf8_enable && scp->uside != NULL &&
		    at < scp->xsize * scp->ysize &&
		    (scp->uside[at].flags & SC_UCELL_REPLACEMENT)) {
			int ncells = 1;
			int lead = at;

			if (scp->uside[at].flags & SC_UCELL_WIDE_CONT) {
				lead = at - 1;
				ncells = 2;
			} else if (scp->uside[at].flags & SC_UCELL_WIDE) {
				ncells = 2;
			}
			kms_draw_tofu_box(scp, lead, ncells, flip);
			/* Cursor invert still needs a glyph; use space. */
			gch = (int)' ';
		} else if (sc_utf8_enable && scp->uside != NULL &&
		    at < scp->xsize * scp->ysize &&
		    !(scp->uside[at].flags & SC_UCELL_REPLACEMENT)) {
			sc_ucell_t *u = &scp->uside[at];
			if ((u->flags & (SC_UCELL_WIDE | SC_UCELL_WIDE_CONT)) ||
			    u->cp > 0x7f ||
			    (int)u->glyph >= SC_GLYPH_EXT_BASE)
				gch = (int)u->glyph;
		}
		char_data = sc_font_glyph(scp, gch);
	}
	char_data += cursor_base * sc_font_bpr(scp);

	blit_blk(scp, char_data,
		 scp->font_width, scp->font_height - cursor_base,
		 draw_pos, scp->blk_width, scp->blk_height - blk_base,
		 line_width, fgidx, bgidx, BLIT_SET, pixel_size);
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
	pixel_size = get_pixel_size(sc->fbi->depth);

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
	blit_blk(scp, (unsigned char *)mouse_and_mask, 16, 16,
		 draw_pos, blk_width, blk_height, line_width,
		 0, 0, BLIT_MASK, pixel_size);
	blit_blk(scp, (unsigned char *)mouse_or_mask, 16, 16,
		 draw_pos, blk_width, blk_height, line_width,
		 15, 0, BLIT_MASK, pixel_size);
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
