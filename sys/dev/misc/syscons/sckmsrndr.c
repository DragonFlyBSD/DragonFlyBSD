/*-
 * Copyright (c) 2014 Imre Vadász <imre@vdsz.com>
 * All rights reserved.
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
#include <sys/thread2.h>

#include <machine/console.h>

#include <dev/drm/include/linux/fb.h>

#include "syscons.h"

#include <bus/isa/isareg.h>

static vr_draw_t		kms_draw;
static vr_draw_cursor_t		kms_cursor;
static vr_blink_cursor_t	kms_blink;
static vr_draw_mouse_t		kms_mouse;

static void			kms_nop(scr_stat *scp, ...);

static sc_rndr_sw_t kmsrndrsw = {
	(vr_draw_border_t *)kms_nop,
	kms_draw,
	(vr_set_cursor_t *)kms_nop,
	kms_cursor,
	kms_blink,
#ifndef SC_NO_CUTPASTE
	kms_mouse,
#else
	(vr_draw_mouse_t *)kms_nop;
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

/* KMS renderer */

static void
kms_draw(scr_stat *scp, int from, int count, int flip)
{
	sc_softc_t *sc = scp->sc;
	u_char *char_data;
	int a, i, j;
	uint32_t fg, bg;
	vm_offset_t draw_pos, p;
	int pos, line_width, pixel_size;

	line_width = sc->fbi->stride;
	pixel_size = 4;

	draw_pos = sc->fbi->vaddr +
	    8 * pixel_size * (from % scp->xsize) +
	    scp->font_size * line_width * (from / scp->xsize);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	for (i = from; count-- > 0; i++) {
		p = draw_pos;
		char_data = &(scp->font[sc_vtb_getc(&scp->vtb, i) *
		    scp->font_size]);

		a = sc_vtb_geta(&scp->vtb, i);
		if (flip) {
			fg = colormap[((a & 0xf000) >> 4) >> 8];
			bg = colormap[(a & 0x0f00) >> 8];
		} else {
			fg = colormap[(a & 0x0f00) >> 8];
			bg = colormap[((a & 0xf000) >> 4) >> 8];
		}

		for (j = 0; j < scp->font_size; j++, char_data++) {
			for (pos = 7; pos >= 0; pos--, p += pixel_size)
				writel(p, *char_data & (1 << pos) ? fg : bg);
			p += line_width - 8 * pixel_size;
		}
		draw_pos += 8 * pixel_size;
		if ((i % scp->xsize) == scp->xsize - 1) {
			draw_pos += (scp->font_size - 1) * line_width +
			    scp->xpad * pixel_size;
		}
	}
}

static void
draw_kmscursor(scr_stat *scp, int at, int on, int flip)
{
	sc_softc_t *sc = scp->sc;
	int line_width, pixel_size, height;
	int a, i, pos;
	uint32_t fg, bg;
	unsigned char *char_data;
	vm_offset_t draw_pos;

	line_width = sc->fbi->stride;
	pixel_size = 4;

	draw_pos = sc->fbi->vaddr +
	    8 * pixel_size * (at % scp->xsize) +
	    scp->font_size * line_width * (at / scp->xsize) +
	    (scp->font_size - scp->cursor_base - 1) * line_width;

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

	char_data = &(scp->font[sc_vtb_getc(&scp->vtb, at) * scp->font_size +
	    scp->font_size - scp->cursor_base - 1]);
	height = imin(scp->cursor_height, scp->font_size);

	for (i = 0; i < height; i++, char_data--) {
		for (pos = 7; pos >= 0; pos--, draw_pos += pixel_size)
			writel(draw_pos, *char_data & (1 << pos) ? fg : bg);
		draw_pos -= line_width + 8 * pixel_size;
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
	int xend, yend;
	int i, j;
	vm_offset_t draw_pos;

	line_width = sc->fbi->stride;
	pixel_size = 4;

	xend = imin(x + 8, 8 * (scp->xoff + scp->xsize));
	yend = imin(y + 16, scp->font_size * (scp->yoff + scp->ysize));

	draw_pos = sc->fbi->vaddr + y * line_width + x * pixel_size;

	for (i = 0; i < (yend - y); i++) {
		for (j = (xend - x - 1); j >= 0; j--) {
			if (mouse_or_mask[i] & 1 << (15 - j))
				writel(draw_pos + pixel_size * j, colormap[15]);
			else if (mouse_and_mask[i] & 1 << (15 - j))
				writel(draw_pos + pixel_size * j, colormap[0]);
		}

		draw_pos += line_width;
	}
}

static void
remove_kmsmouse(scr_stat *scp, int x, int y)
{
	int col, row;
	int pos;
	int i;

	/* erase the mouse cursor image */
	col = x/8 - scp->xoff;
	row = y/scp->font_size - scp->yoff;
	pos = row*scp->xsize + col;
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
