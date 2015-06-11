/*-
 * (MPSAFE)
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>
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
 *
 * $FreeBSD: src/sys/dev/syscons/scvgarndr.c,v 1.5.2.3 2001/07/28 12:51:47 yokota Exp $
 */

#include "opt_syscons.h"
#include "opt_vga.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <machine/console.h>

#include <dev/video/fb/fbreg.h>
#include <dev/video/fb/vgareg.h>
#include "syscons.h"

#include <bus/isa/isareg.h>

static vr_draw_border_t		vga_txtborder;
static vr_draw_t		vga_txtdraw;
static vr_set_cursor_t		vga_txtcursor_shape;
static vr_draw_cursor_t		vga_txtcursor;
static vr_blink_cursor_t	vga_txtblink;
#ifndef SC_NO_CUTPASTE
static vr_draw_mouse_t		vga_txtmouse;
#else
#define	vga_txtmouse		(vr_draw_mouse_t *)vga_nop
#endif

#ifdef SC_PIXEL_MODE
static vr_draw_border_t		vga_pxlborder_direct;
static vr_draw_border_t		vga_pxlborder_packed;
static vr_draw_border_t		vga_pxlborder_planar;
static vr_draw_t		vga_vgadraw_direct;
static vr_draw_t		vga_vgadraw_packed;
static vr_draw_t		vga_vgadraw_planar;
static vr_set_cursor_t		vga_pxlcursor_shape;
static vr_draw_cursor_t		vga_pxlcursor_direct;
static vr_draw_cursor_t		vga_pxlcursor_packed;
static vr_draw_cursor_t		vga_pxlcursor_planar;
static vr_blink_cursor_t	vga_pxlblink_direct;
static vr_blink_cursor_t	vga_pxlblink_packed;
static vr_blink_cursor_t	vga_pxlblink_planar;
#ifndef SC_NO_CUTPASTE
static vr_draw_mouse_t		vga_pxlmouse_direct;
static vr_draw_mouse_t		vga_pxlmouse_packed;
static vr_draw_mouse_t		vga_pxlmouse_planar;
#else
#define	vga_pxlmouse_direct	(vr_draw_mouse_t *)vga_nop
#define	vga_pxlmouse_packed	(vr_draw_mouse_t *)vga_nop
#define	vga_pxlmouse_planar	(vr_draw_mouse_t *)vga_nop
#endif
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE
static vr_draw_border_t		vga_grborder;
#endif

static void			vga_nop(scr_stat *scp, ...);

static sc_rndr_sw_t txtrndrsw = {
	vga_txtborder,
	vga_txtdraw,	
	vga_txtcursor_shape,
	vga_txtcursor,
	vga_txtblink,
	vga_txtmouse,
};
RENDERER(vga, V_INFO_MM_TEXT, txtrndrsw, vga_set);

#ifdef SC_PIXEL_MODE
static sc_rndr_sw_t directrndrsw = {
	vga_pxlborder_direct,
	vga_vgadraw_direct,
	vga_pxlcursor_shape,
	vga_pxlcursor_direct,
	vga_pxlblink_direct,
	vga_pxlmouse_direct,
};
RENDERER(vga, V_INFO_MM_DIRECT, directrndrsw, vga_set);

static sc_rndr_sw_t packedrndrsw = {
	vga_pxlborder_packed,
	vga_vgadraw_packed,
	vga_pxlcursor_shape,
	vga_pxlcursor_packed,
	vga_pxlblink_packed,
	vga_pxlmouse_packed,
};
RENDERER(vga, V_INFO_MM_PACKED, packedrndrsw, vga_set);

static sc_rndr_sw_t planarrndrsw = {
	vga_pxlborder_planar,
	vga_vgadraw_planar,
	vga_pxlcursor_shape,
	vga_pxlcursor_planar,
	vga_pxlblink_planar,
	vga_pxlmouse_planar,
};
RENDERER(vga, V_INFO_MM_PLANAR, planarrndrsw, vga_set);
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE
static sc_rndr_sw_t grrndrsw = {
	vga_grborder,
	(vr_draw_t *)vga_nop,
	(vr_set_cursor_t *)vga_nop,
	(vr_draw_cursor_t *)vga_nop,
	(vr_blink_cursor_t *)vga_nop,
	(vr_draw_mouse_t *)vga_nop,
};
RENDERER(vga, V_INFO_MM_OTHER, grrndrsw, vga_set);
#endif /* SC_NO_MODE_CHANGE */

RENDERER_MODULE(vga, vga_set);

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
vga_nop(scr_stat *scp, ...)
{
}

/* text mode renderer */

static void
vga_txtborder(scr_stat *scp, int color)
{
	(*vidsw[scp->sc->adapter]->set_border)(scp->sc->adp, color);
}

static void
vga_txtdraw(scr_stat *scp, int from, int count,
	    int flip, void (*func_yield)(void))
{
	uint16_t *p;
	int c;
	int a;

	if (from + count > scp->xsize*scp->ysize)
		count = scp->xsize*scp->ysize - from;

	if (flip) {
		for (p = scp->scr.vtb_buffer + from; count-- > 0; ++from) {
			c = sc_vtb_getc(&scp->vtb, from);
			a = sc_vtb_geta(&scp->vtb, from);
			a = (a & 0x8800) | ((a & 0x7000) >> 4) 
				| ((a & 0x0700) << 4);
			p = sc_vtb_putchar(&scp->scr, p, c, a);
		}
	} else {
		sc_vtb_copy(&scp->vtb, from, &scp->scr, from, count);
	}
}

static void 
vga_txtcursor_shape(scr_stat *scp, int base, int height, int blink)
{
	if (base < 0 || base >= scp->font_height)
		return;

	/* the caller may set height <= 0 in order to disable the cursor */
#if 0
	scp->cursor_base = base;
	scp->cursor_height = height;
#endif
	(*vidsw[scp->sc->adapter]->set_hw_cursor_shape)(scp->sc->adp,
							base, height,
							scp->font_height,
							blink);

}

static void
draw_txtcharcursor(scr_stat *scp, int at, u_short c, u_short a, int flip)
{
	sc_softc_t *sc;

	sc = scp->sc;
	scp->cursor_saveunder_char = c;
	scp->cursor_saveunder_attr = a;

#ifndef SC_NO_FONT_LOADING
	if (sc->flags & SC_CHAR_CURSOR) {
		unsigned char *font;
		int h;
		int i;

		if (scp->font_height < 14) {
			font = sc->font_8;
			h = 8;
		} else if (scp->font_height >= 16) {
			font = sc->font_16;
			h = 16;
		} else {
			font = sc->font_14;
			h = 14;
		}
		if (scp->cursor_base >= h)
			return;
		if (flip)
			a = (a & 0x8800)
				| ((a & 0x7000) >> 4) | ((a & 0x0700) << 4);
		bcopy(font + c*h, font + sc->cursor_char*h, h);
		font = font + sc->cursor_char*h;
		for (i = imax(h - scp->cursor_base - scp->cursor_height, 0);
			i < h - scp->cursor_base; ++i) {
			font[i] ^= 0xff;
		}
		sc->font_loading_in_progress = TRUE;
		/* XXX */
		(*vidsw[sc->adapter]->load_font)(sc->adp, 0, h, font,
						 sc->cursor_char, 1);
		sc->font_loading_in_progress = FALSE;
		sc_vtb_putc(&scp->scr, at, sc->cursor_char, a);
	} else
#endif /* SC_NO_FONT_LOADING */
	{
		if ((a & 0x7000) == 0x7000) {
			a &= 0x8f00;
			if ((a & 0x0700) == 0)
				a |= 0x0700;
		} else {
			a |= 0x7000;
			if ((a & 0x0700) == 0x0700)
				a &= 0xf000;
		}
		if (flip)
			a = (a & 0x8800)
				| ((a & 0x7000) >> 4) | ((a & 0x0700) << 4);
		sc_vtb_putc(&scp->scr, at, c, a);
	}
}

static void
vga_txtcursor(scr_stat *scp, int at, int blink, int on, int flip)
{
	video_adapter_t *adp;
	int cursor_attr;

	if (scp->cursor_height <= 0)	/* the text cursor is disabled */
		return;

	adp = scp->sc->adp;
	if (blink) {
		scp->status |= VR_CURSOR_BLINK;
		if (on) {
			scp->status |= VR_CURSOR_ON;
			(*vidsw[adp->va_index]->set_hw_cursor)(adp,
							       at%scp->xsize,
							       at/scp->xsize); 
		} else {
			if (scp->status & VR_CURSOR_ON)
				(*vidsw[adp->va_index]->set_hw_cursor)(adp,
								       -1, -1);
			scp->status &= ~VR_CURSOR_ON;
		}
	} else {
		scp->status &= ~VR_CURSOR_BLINK;
		if (on) {
			scp->status |= VR_CURSOR_ON;
			draw_txtcharcursor(scp, at,
					   sc_vtb_getc(&scp->scr, at),
					   sc_vtb_geta(&scp->scr, at),
					   flip);
		} else {
			cursor_attr = scp->cursor_saveunder_attr;
			if (flip)
				cursor_attr = (cursor_attr & 0x8800)
					| ((cursor_attr & 0x7000) >> 4)
					| ((cursor_attr & 0x0700) << 4);
			if (scp->status & VR_CURSOR_ON)
				sc_vtb_putc(&scp->scr, at,
					    scp->cursor_saveunder_char,
					    cursor_attr);
			scp->status &= ~VR_CURSOR_ON;
		}
	}
}

static void
vga_txtblink(scr_stat *scp, int at, int flip)
{
}

int sc_txtmouse_no_retrace_wait;

#ifndef SC_NO_CUTPASTE

static void
draw_txtmouse(scr_stat *scp, int x, int y)
{
#ifndef SC_ALT_MOUSE_IMAGE
    if (ISMOUSEAVAIL(scp->sc->adp->va_flags)) {
	u_char font_buf[128];
	u_short cursor[32];
	u_char c;
	int pos;
	int xoffset, yoffset;
	int i;

	/* prepare mousepointer char's bitmaps */
	pos = (y / scp->font_height - scp->yoff) * scp->xsize +
	      x / scp->font_width - scp->xoff;
	bcopy(scp->font + sc_vtb_getc(&scp->scr, pos) * scp->font_height,
	      &font_buf[0], scp->font_height);
	bcopy(scp->font + sc_vtb_getc(&scp->scr, pos + 1) * scp->font_height,
	      &font_buf[32], scp->font_height);
	bcopy(scp->font 
		 + sc_vtb_getc(&scp->scr, pos + scp->xsize) * scp->font_height,
	      &font_buf[64], scp->font_height);
	bcopy(scp->font +
	      sc_vtb_getc(&scp->scr, pos + scp->xsize + 1) * scp->font_height,
	      &font_buf[96], scp->font_height);
	for (i = 0; i < scp->font_height; ++i) {
		cursor[i] = (font_buf[i]<<8) | font_buf[i+32];
		cursor[i + scp->font_height] = (font_buf[i+64]<<8) |
					       font_buf[i+96];
	}

	/* now and-or in the mousepointer image */
	xoffset = x % scp->font_width;
	yoffset = y % scp->font_height;
	for (i = 0; i < 16; ++i) {
		cursor[i + yoffset] =
	    		(cursor[i + yoffset] & ~(mouse_and_mask[i] >> xoffset))
	    		| (mouse_or_mask[i] >> xoffset);
	}
	for (i = 0; i < scp->font_height; ++i) {
		font_buf[i] = (cursor[i] & 0xff00) >> 8;
		font_buf[i + 32] = cursor[i] & 0xff;
		font_buf[i + 64] = (cursor[i + scp->font_height] & 0xff00) >> 8;
		font_buf[i + 96] = cursor[i + scp->font_height] & 0xff;
	}

#if 1
	/* wait for vertical retrace to avoid jitter on some videocards */
	while (!sc_txtmouse_no_retrace_wait &&
	    !(inb(CRTC + 6) & 0x08))
		/* idle */ ;
#endif
	c = scp->sc->mouse_char;
	(*vidsw[scp->sc->adapter]->load_font)(scp->sc->adp, 0, 32, font_buf,
					      c, 4); 

	sc_vtb_putc(&scp->scr, pos, c, sc_vtb_geta(&scp->scr, pos));
	/* FIXME: may be out of range! */
	sc_vtb_putc(&scp->scr, pos + scp->xsize, c + 2,
		    sc_vtb_geta(&scp->scr, pos + scp->xsize));
	if (x < (scp->xsize - 1)*8) {
		sc_vtb_putc(&scp->scr, pos + 1, c + 1,
			    sc_vtb_geta(&scp->scr, pos + 1));
		sc_vtb_putc(&scp->scr, pos + scp->xsize + 1, c + 3,
			    sc_vtb_geta(&scp->scr, pos + scp->xsize + 1));
	}
    } else
#endif /* SC_ALT_MOUSE_IMAGE */
    {
	/* Red, magenta and brown are mapped to green to to keep it readable */
	static const int col_conv[16] = {
		6, 6, 6, 6, 2, 2, 2, 6, 14, 14, 14, 14, 10, 10, 10, 14
	};
	int pos;
	int color;
	int a;

	pos = (y / scp->font_height - scp->yoff)*
	      scp->xsize + x / scp->font_width - scp->xoff;
	a = sc_vtb_geta(&scp->scr, pos);
	if (scp->sc->adp->va_flags & V_ADP_COLOR)
		color = (col_conv[(a & 0xf000) >> 12] << 12)
			| ((a & 0x0f00) | 0x0800);
	else
		color = ((a & 0xf000) >> 4) | ((a & 0x0f00) << 4);
	sc_vtb_putc(&scp->scr, pos, sc_vtb_getc(&scp->scr, pos), color);
    }

}

static void
remove_txtmouse(scr_stat *scp, int x, int y)
{
}

static void 
vga_txtmouse(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_txtmouse(scp, x, y);
	else
		remove_txtmouse(scp, x, y);
}

#endif /* SC_NO_CUTPASTE */

#ifdef SC_PIXEL_MODE

/* pixel (raster text) mode renderer */

static void
vga_pxlborder_direct(scr_stat *scp, int color)
{
	int i, x, y;
	int line_width, pixel_size;
	uint32_t u32 = 0;
	vm_offset_t draw_pos, draw_end, p;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	for (i = 0; i < 4 / pixel_size; ++i)
		u32 += scp->ega_palette[color] << (i * 8 * pixel_size);

	if (scp->yoff > 0) {
		draw_pos = scp->sc->adp->va_window;
		draw_end = draw_pos + line_width * scp->yoff * scp->font_height;

		for (p = draw_pos; p < draw_end; p += 4)
			writel(p, u32);
	}

	y = (scp->yoff + scp->ysize) * scp->font_height;

	if (scp->ypixel > y) {
		draw_pos = scp->sc->adp->va_window + line_width * y;
		draw_end = draw_pos + line_width * (scp->ypixel - y);

		for (p = draw_pos; p < draw_end; p += 4)
			writel(p, u32); 
	}

	y = scp->yoff * scp->font_height;
	x = scp->xpixel / scp->font_width - scp->xoff - scp->xsize;

	for (i = 0; i < scp->ysize * scp->font_height; ++i) {
		if (scp->xoff > 0) {
			draw_pos = scp->sc->adp->va_window +
			    line_width * (y + i);
			draw_end = draw_pos +
				   scp->xoff * scp->font_width * pixel_size;

			for (p = draw_pos; p < draw_end; p += 4)
				writel(p, u32);
		}

		if (x > 0) {
			draw_pos = scp->sc->adp->va_window +
			    line_width * (y + i) +
			    scp->xoff * 8 * pixel_size +
			    scp->xsize * 8 * pixel_size;
			draw_end = draw_pos + x * 8 * pixel_size;

			for (p = draw_pos; p < draw_end; p += 4)
				writel(p, u32);
		}
	}
}

static void
vga_pxlborder_packed(scr_stat *scp, int color)
{
	int i, x, y;
	int line_width;
	uint32_t u32;
	vm_offset_t draw_pos, draw_end, p;

	line_width = scp->sc->adp->va_line_width;
	u32 = (color << 24) + (color << 16) + (color << 8) + color;

	if (scp->yoff > 0) {
		draw_pos = scp->sc->adp->va_window;
		draw_end = draw_pos + line_width * scp->yoff * scp->font_height;

		for (p = draw_pos; p < draw_end; p += 4)
			writel(p, u32);
	}

	y = (scp->yoff + scp->ysize) * scp->font_height;

	if (scp->ypixel > y) {
		draw_pos = scp->sc->adp->va_window + line_width * y;
		draw_end = draw_pos + line_width * (scp->ypixel - y);

		for (p = draw_pos; p < draw_end; p += 4)
			writel(p, u32);
	}

	y = scp->yoff * scp->font_height;
	x = scp->xpixel / scp->font_width - scp->xoff - scp->xsize;

	for (i = 0; i < scp->ysize * scp->font_height; ++i) {
		if (scp->xoff > 0) {
			draw_pos = scp->sc->adp->va_window +
			    line_width * (y + i);
			draw_end = draw_pos + scp->xoff * scp->font_width;

			for (p = draw_pos; p < draw_end; p += 4)
				writel(p, u32);
		}

		if (x > 0) {
			draw_pos = scp->sc->adp->va_window +
			    line_width * (y + i) + scp->xoff * 8 +
			    scp->xsize * 8;
			draw_end = draw_pos + x * 8;

			for (p = draw_pos; p < draw_end; p += 4)
				writel(p, u32);
		}
	}
}

static void
vga_pxlborder_planar(scr_stat *scp, int color)
{
	vm_offset_t p;
	int line_width;
	int x;
	int y;
	int i;

	lwkt_gettoken(&tty_token);

	(*vidsw[scp->sc->adapter]->set_border)(scp->sc->adp, color);

	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
	outw(GDCIDX, (color << 8) | 0x00);	/* set/reset */
	line_width = scp->sc->adp->va_line_width;
	p = scp->sc->adp->va_window;
	if (scp->yoff > 0)
		bzero_io((void *)p, line_width*scp->yoff*scp->font_height);
	y = (scp->yoff + scp->ysize)*scp->font_height;
	if (scp->ypixel > y)
		bzero_io((void *)(p + line_width*y),
			 line_width*(scp->ypixel - y));
	y = scp->yoff*scp->font_height;
	x = scp->xpixel/scp->font_width - scp->xoff - scp->xsize;
	for (i = 0; i < scp->ysize*scp->font_height; ++i) {
		if (scp->xoff > 0)
			bzero_io((void *)(p + line_width*(y + i)), scp->xoff);
		if (x > 0)
			bzero_io((void *)(p + line_width*(y + i)
				     + scp->xoff + scp->xsize), x);
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
	lwkt_reltoken(&tty_token);
}

static void
vga_vgadraw_direct(scr_stat *scp, int from, int count,
		   int flip, void (*func_yield)(void))
{
	int line_width, pixel_size;
	int a, i, j, k, l, pos;
	uint32_t fg, bg, u32;
	unsigned char *char_data;
	vm_offset_t draw_pos, p;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	draw_pos = VIDEO_MEMORY_POS(scp, from, 8 * pixel_size);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	for (i = from; count-- > 0; ++i) {
		a = sc_vtb_geta(&scp->vtb, i);

		if (flip) {
			fg = scp->ega_palette[(((a & 0x7000) >> 4) |
			    (a & 0x0800)) >> 8];
			bg = scp->ega_palette[(((a & 0x8000) >> 4) |
			    (a & 0x0700)) >> 8];
		} else {
			fg = scp->ega_palette[(a & 0x0f00) >> 8];
			bg = scp->ega_palette[(a & 0xf000) >> 12];
		}

		p = draw_pos;
		char_data = &(scp->font[sc_vtb_getc(&scp->vtb, i) *
		    scp->font_height]);

		for (j = 0; j < scp->font_height; ++j, ++char_data) {
			pos = 7;

			for (k = 0; k < 2 * pixel_size; ++k) {
				u32 = 0;

				for (l = 0; l < 4 / pixel_size; ++l) {
					u32 += (*char_data & (1 << pos--) ?
					    fg : bg) << (l * 8 * pixel_size);
				}

				writel(p, u32);
				p += 4;
			}

			p += line_width - 8 * pixel_size;
		}

		draw_pos += 8 * pixel_size;

		if ((i % scp->xsize) == scp->xsize - 1)
			draw_pos += scp->xoff * 16 * pixel_size +
			     (scp->font_height - 1) * line_width;
	}
}

static void
vga_vgadraw_packed(scr_stat *scp, int from, int count,
		   int flip, void (*func_yield)(void))
{
	int line_width;
	int a, i, j;
	uint32_t fg, bg, u32;
	unsigned char *char_data;
	vm_offset_t draw_pos, p;

	line_width = scp->sc->adp->va_line_width;

	draw_pos = VIDEO_MEMORY_POS(scp, from, 8);

	if (from + count > scp->xsize * scp->ysize)
		count = scp->xsize * scp->ysize - from;

	for (i = from; count-- > 0; ++i) {
		a = sc_vtb_geta(&scp->vtb, i);

		if (flip) {
			fg = ((a & 0xf000) >> 4) >> 8;
			bg = (a & 0x0f00) >> 8;
		} else {
			fg = (a & 0x0f00) >> 8;
			bg = ((a & 0xf000) >> 4) >> 8;
		}

		p = draw_pos;
		char_data = &(scp->font[sc_vtb_getc(&scp->vtb, i) *
		    scp->font_height]);

		for (j = 0; j < scp->font_height; ++j, ++char_data) {
			u32 = ((*char_data & 1 ? fg : bg) << 24) +
			      ((*char_data & 2 ? fg : bg) << 16) +
			      ((*char_data & 4 ? fg : bg) << 8) +
			      (*char_data & 8 ? fg : bg);
			writel(p + 4, u32);

			u32 = ((*char_data & 16 ? fg : bg) << 24) +
			      ((*char_data & 32 ? fg : bg) << 16) +
			      ((*char_data & 64 ? fg : bg) << 8) +
			      (*char_data & 128 ? fg : bg);
			writel(p, u32);

			p += line_width;
		}

		draw_pos += scp->font_width;

		if ((i % scp->xsize) == scp->xsize - 1)
			draw_pos += scp->xoff * 16 +
			     (scp->font_height - 1) * line_width;
	}
}

static void
vga_vgadraw_planar(scr_stat *scp, int from, int count,
		   int flip, void (*func_yield)(void))
{
	vm_offset_t d;
	vm_offset_t e;
	u_char *f;
	u_short bg;
	u_short col1, col2;
	int line_width;
	int i, j;
	int a;
	u_char c;

	d = VIDEO_MEMORY_POS(scp, from, 1);

	line_width = scp->sc->adp->va_line_width;

	outw(GDCIDX, 0x0305);		/* read mode 0, write mode 3 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
	bg = -1;
	if (from + count > scp->xsize*scp->ysize)
		count = scp->xsize*scp->ysize - from;
	for (i = from; count-- > 0; ++i) {
		a = sc_vtb_geta(&scp->vtb, i);
		if (flip) {
			col1 = ((a & 0x7000) >> 4) | (a & 0x0800);
			col2 = ((a & 0x8000) >> 4) | (a & 0x0700);
		} else {
			col1 = (a & 0x0f00);
			col2 = (a & 0xf000) >> 4;
		}
		/* set background color in EGA/VGA latch */
		if (bg != col2) {
			bg = col2;
			outw(GDCIDX, 0x0005);	/* read mode 0, write mode 0 */
			outw(GDCIDX, bg | 0x00); /* set/reset */
			writeb(d, 0);
			c = readb(d);		/* set bg color in the latch */
			outw(GDCIDX, 0x0305);	/* read mode 0, write mode 3 */
		}
		/* foreground color */
		outw(GDCIDX, col1 | 0x00);	/* set/reset */
		e = d;
		f = &(scp->font[sc_vtb_getc(&scp->vtb, i)*scp->font_height]);
		for (j = 0; j < scp->font_height; ++j, ++f) {
	        	writeb(e, *f);
			e += line_width;
		}
		++d;
		if ((i % scp->xsize) == scp->xsize - 1)
			d += scp->xoff*2 
				 + (scp->font_height - 1)*line_width;
	}
	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
}

static void 
vga_pxlcursor_shape(scr_stat *scp, int base, int height, int blink)
{
	if (base < 0 || base >= scp->font_height)
		return;
	/* the caller may set height <= 0 in order to disable the cursor */
#if 0
	scp->cursor_base = base;
	scp->cursor_height = height;
#endif
}

static void 
draw_pxlcursor_direct(scr_stat *scp, int at, int on, int flip)
{
	int line_width, pixel_size, height;
	int a, i, j, k, pos;
	uint32_t fg, bg, u32;
	unsigned char *char_data;
	vm_offset_t draw_pos;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	draw_pos = VIDEO_MEMORY_POS(scp, at, scp->font_width * pixel_size) +
	    (scp->font_height - scp->cursor_base - 1) * line_width;

	a = sc_vtb_geta(&scp->vtb, at);

	if (flip) {
		fg = scp->ega_palette[((on) ? (a & 0x0f00) :
		    ((a & 0xf000) >> 4)) >> 8];
		bg = scp->ega_palette[((on) ? ((a & 0xf000) >> 4) :
		    (a & 0x0f00)) >> 8];
	} else {
		fg = scp->ega_palette[((on) ? ((a & 0xf000) >> 4) :
		    (a & 0x0f00)) >> 8];
		bg = scp->ega_palette[((on) ? (a & 0x0f00) :
		    ((a & 0xf000) >> 4)) >> 8];
	}

	char_data = &(scp->font[sc_vtb_getc(&scp->vtb, at) * scp->font_height +
	    scp->font_height - scp->cursor_base - 1]);

	height = imin(scp->cursor_height, scp->font_height);

	for (i = 0; i < height; ++i, --char_data) {
		pos = 7;

		for (j = 0; j < 2 * pixel_size; ++j) {
			u32 = 0;

			for (k = 0; k < 4 / pixel_size; ++k) {
				u32 += (*char_data & (1 << pos--) ?
				    fg : bg) << (k * 8 * pixel_size);
			}

			writel(draw_pos, u32);
			draw_pos += 4;
		}

		draw_pos -= line_width + 8 * pixel_size;
	}
}

static void
draw_pxlcursor_packed(scr_stat *scp, int at, int on, int flip)
{
	int line_width, height;
	int a, i;
	uint32_t fg, bg, u32;
	unsigned char *char_data;
	vm_offset_t draw_pos;

	line_width = scp->sc->adp->va_line_width;

	draw_pos = VIDEO_MEMORY_POS(scp, at, 8) +
	    (scp->font_height - scp->cursor_base - 1) * line_width;

	a = sc_vtb_geta(&scp->vtb, at);

	if (flip) {
		fg = ((on) ? (a & 0x0f00) : ((a & 0xf000) >> 4)) >> 8;
		bg = ((on) ? ((a & 0xf000) >> 4) : (a & 0x0f00)) >> 8;
	} else {
		fg = ((on) ? ((a & 0xf000) >> 4) : (a & 0x0f00)) >> 8;
		bg = ((on) ? (a & 0x0f00) : ((a & 0xf000) >> 4)) >> 8;
	}

	char_data = &(scp->font[sc_vtb_getc(&scp->vtb, at) * scp->font_height +
	    scp->font_height - scp->cursor_base - 1]);

	height = imin(scp->cursor_height, scp->font_height);

	for (i = 0; i < height; ++i, --char_data) {
		u32 = ((*char_data & 1 ? fg : bg) << 24) +
		      ((*char_data & 2 ? fg : bg) << 16) +
		      ((*char_data & 4 ? fg : bg) << 8) +
		      (*char_data & 8 ? fg : bg);
		writel(draw_pos + 4, u32);

		u32 = ((*char_data & 16 ? fg : bg) << 24) +
		      ((*char_data & 32 ? fg : bg) << 16) +
		      ((*char_data & 64 ? fg : bg) << 8) +
		      (*char_data & 128 ? fg : bg);
		writel(draw_pos, u32);

		draw_pos -= line_width;
	}
}

static void 
draw_pxlcursor_planar(scr_stat *scp, int at, int on, int flip)
{
	vm_offset_t d;
	u_char *f;
	int line_width;
	int height;
	int col;
	int a;
	int i;
	u_char c;

	line_width = scp->sc->adp->va_line_width;

	d = VIDEO_MEMORY_POS(scp, at, 1) +
	    (scp->font_height - scp->cursor_base - 1) * line_width;

	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
	outw(GDCIDX, 0x0f01);		/* set/reset enable */
	/* set background color in EGA/VGA latch */
	a = sc_vtb_geta(&scp->vtb, at);
	if (flip)
		col = (on) ? ((a & 0xf000) >> 4) : (a & 0x0f00);
	else
		col = (on) ? (a & 0x0f00) : ((a & 0xf000) >> 4);
	outw(GDCIDX, col | 0x00);	/* set/reset */
	outw(GDCIDX, 0xff08);		/* bit mask */
	writeb(d, 0);
	c = readb(d);			/* set bg color in the latch */
	/* foreground color */
	if (flip)
		col = (on) ? (a & 0x0f00) : ((a & 0xf000) >> 4);
	else
		col = (on) ? ((a & 0xf000) >> 4) : (a & 0x0f00);
	outw(GDCIDX, col | 0x00);	/* set/reset */
	f = &(scp->font[sc_vtb_getc(&scp->vtb, at)*scp->font_height
		+ scp->font_height - scp->cursor_base - 1]);
	height = imin(scp->cursor_height, scp->font_height);
	for (i = 0; i < height; ++i, --f) {
		outw(GDCIDX, (*f << 8) | 0x08);	/* bit mask */
	       	writeb(d, 0);
		d -= line_width;
	}
	outw(GDCIDX, 0x0000);		/* set/reset */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
	outw(GDCIDX, 0xff08);		/* bit mask */
}

static int pxlblinkrate = 0;

static void 
vga_pxlcursor_direct(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->cursor_height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_pxlcursor_direct(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_pxlcursor_direct(scp, at,
					      scp->status & VR_CURSOR_ON,
					      flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_pxlcursor_direct(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void 
vga_pxlcursor_packed(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->cursor_height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_pxlcursor_packed(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_pxlcursor_packed(scp, at,
					      scp->status & VR_CURSOR_ON,
					      flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_pxlcursor_packed(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void
vga_pxlcursor_planar(scr_stat *scp, int at, int blink, int on, int flip)
{
	if (scp->cursor_height <= 0)	/* the text cursor is disabled */
		return;

	if (on) {
		if (!blink) {
			scp->status |= VR_CURSOR_ON;
			draw_pxlcursor_planar(scp, at, on, flip);
		} else if (++pxlblinkrate & 4) {
			pxlblinkrate = 0;
			scp->status ^= VR_CURSOR_ON;
			draw_pxlcursor_planar(scp, at,
					      scp->status & VR_CURSOR_ON,
					      flip);
		}
	} else {
		if (scp->status & VR_CURSOR_ON)
			draw_pxlcursor_planar(scp, at, on, flip);
		scp->status &= ~VR_CURSOR_ON;
	}
	if (blink)
		scp->status |= VR_CURSOR_BLINK;
	else
		scp->status &= ~VR_CURSOR_BLINK;
}

static void
vga_pxlblink_direct(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_pxlcursor_direct(scp, at, scp->status & VR_CURSOR_ON, flip);
}

static void
vga_pxlblink_packed(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_pxlcursor_packed(scp, at, scp->status & VR_CURSOR_ON, flip);
}

static void
vga_pxlblink_planar(scr_stat *scp, int at, int flip)
{
	if (!(scp->status & VR_CURSOR_BLINK))
		return;
	if (!(++pxlblinkrate & 4))
		return;
	pxlblinkrate = 0;
	scp->status ^= VR_CURSOR_ON;
	draw_pxlcursor_planar(scp, at, scp->status & VR_CURSOR_ON, flip);
}

#ifndef SC_NO_CUTPASTE

static void 
draw_pxlmouse_direct(scr_stat *scp, int x, int y)
{
	int line_width, pixel_size;
	int xend, yend;
	int i, j;
	vm_offset_t draw_pos;

	line_width = scp->sc->adp->va_line_width;
	pixel_size = scp->sc->adp->va_info.vi_pixel_size;

	xend = imin(x + 8, 8 * (scp->xoff + scp->xsize));
	yend = imin(y + 16, scp->font_height * (scp->yoff + scp->ysize));

	draw_pos = scp->sc->adp->va_window + y * line_width + x * pixel_size;

	for (i = 0; i < (yend - y); i++) {
		for (j = (xend - x - 1); j >= 0; j--) {
			switch (scp->sc->adp->va_info.vi_depth) {
			case 32:
				if (mouse_or_mask[i] & 1 << (15 - j))
					writel(draw_pos + 4 * j,
					    scp->ega_palette[15]);
				else if (mouse_and_mask[i] & 1 << (15 - j))
					writel(draw_pos + 4 * j,
					    scp->ega_palette[0]);
				break;
			case 16:
				/* FALLTHROUGH */
			case 15:
				if (mouse_or_mask[i] & 1 << (15 - j))
					writew(draw_pos + 2 * j,
					    scp->ega_palette[15]);
				else if (mouse_and_mask[i] & 1 << (15 - j))
					writew(draw_pos + 2 * j,
					    scp->ega_palette[0]);
				break;
			}
		}

		draw_pos += line_width;
	}
}

static void
draw_pxlmouse_packed(scr_stat *scp, int x, int y)
{
	int line_width;
	int xend, yend;
	int i, j;
	vm_offset_t draw_pos;

	line_width = scp->sc->adp->va_line_width;

	xend = imin(scp->font_width * (scp->xoff + scp->xsize),
		    imin(x + 16, scp->xpixel));
	yend = imin(scp->font_height * (scp->yoff + scp->ysize),
		    imin(y + 16, scp->ypixel));

	draw_pos = scp->sc->adp->va_window + y * line_width + x;

	for (i = 0; i < (yend - y); i++) {
		for (j = (xend - x - 1); j >= 0; j--) {
			if (mouse_or_mask[i] & 1 << (15 - j))
				writeb(draw_pos + j, 15);
			else if (mouse_and_mask[i] & 1 << (15 - j))
				writeb(draw_pos + j, 0);
		}

		draw_pos += line_width;
	}
}

static void
draw_pxlmouse_planar(scr_stat *scp, int x, int y)
{
	vm_offset_t p;
	int line_width;
	int xoff;
	int ymax;
	u_short m;
	int i, j;

	line_width = scp->sc->adp->va_line_width;
	xoff = (x - scp->xoff*8)%8;
	ymax = imin(y + 16, scp->font_height * (scp->yoff + scp->ysize));

	outw(GDCIDX, 0x0805);		/* read mode 1, write mode 0 */
	outw(GDCIDX, 0x0001);		/* set/reset enable */
	outw(GDCIDX, 0x0002);		/* color compare */
	outw(GDCIDX, 0x0007);		/* color don't care */
	outw(GDCIDX, 0xff08);		/* bit mask */
	outw(GDCIDX, 0x0803);		/* data rotate/function select (and) */
	p = scp->sc->adp->va_window + line_width*y + x/8;
	if (x < 8 * (scp->xoff + scp->xsize) - 8) {
		for (i = y, j = 0; i < ymax; ++i, ++j) {
			m = ~(mouse_and_mask[j] >> xoff);
			*(u_char *)p &= m >> 8;
			*(u_char *)(p + 1) &= m;
			p += line_width;
		}
	} else {
		xoff += 8;
		for (i = y, j = 0; i < ymax; ++i, ++j) {
			m = ~(mouse_and_mask[j] >> xoff);
			*(u_char *)p &= m;
			p += line_width;
		}
	}
	outw(GDCIDX, 0x1003);		/* data rotate/function select (or) */
	p = scp->sc->adp->va_window + line_width*y + x/8;
	if (x < 8 * (scp->xoff + scp->xsize) - 8) {
		for (i = y, j = 0; i < ymax; ++i, ++j) {
			m = mouse_or_mask[j] >> xoff;
			*(u_char *)p &= m >> 8;
			*(u_char *)(p + 1) &= m;
			p += line_width;
		}
	} else {
		for (i = y, j = 0; i < ymax; ++i, ++j) {
			m = mouse_or_mask[j] >> xoff;
			*(u_char *)p &= m;
			p += line_width;
		}
	}
	outw(GDCIDX, 0x0005);		/* read mode 0, write mode 0 */
	outw(GDCIDX, 0x0003);		/* data rotate/function select */
}

static void
remove_pxlmouse(scr_stat *scp, int x, int y)
{
	int col, row;
	int pos;
	int i;

	/* erase the mouse cursor image */
	col = x / scp->font_width - scp->xoff;
	row = y / scp->font_height - scp->yoff;
	pos = row * scp->xsize + col;
	i = (col < scp->xsize - 1) ? 2 : 1;
	(*scp->rndr->draw)(scp, pos, i, FALSE, NULL);
	if (row < scp->ysize - 1)
		(*scp->rndr->draw)(scp, pos + scp->xsize, i, FALSE, NULL);
}

static void 
vga_pxlmouse_direct(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_pxlmouse_direct(scp, x, y);
	else
		remove_pxlmouse(scp, x, y);
}

static void
vga_pxlmouse_packed(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_pxlmouse_packed(scp, x, y);
	else
		remove_pxlmouse(scp, x, y);
}

static void 
vga_pxlmouse_planar(scr_stat *scp, int x, int y, int on)
{
	if (on)
		draw_pxlmouse_planar(scp, x, y);
	else
		remove_pxlmouse(scp, x, y);
}

#endif /* SC_NO_CUTPASTE */
#endif /* SC_PIXEL_MODE */

#ifndef SC_NO_MODE_CHANGE

/* graphics mode renderer */

static void
vga_grborder(scr_stat *scp, int color)
{
	lwkt_gettoken(&tty_token);
	(*vidsw[scp->sc->adapter]->set_border)(scp->sc->adp, color);
	lwkt_reltoken(&tty_token);
}

#endif
