/*-
 * (MPSAFE)
 *
 * Copyright (c) 1998 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/syscons/scvidctl.c,v 1.19.2.2 2000/05/05 09:16:08 nyan Exp $
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/thread2.h>

#include <machine/console.h>

#include <dev/drm/include/linux/fb.h>
#include <dev/video/fb/fbreg.h>
#include "syscons.h"

SET_DECLARE(scrndr_set, const sc_renderer_t);

static int desired_cols = 0;
TUNABLE_INT("kern.kms_columns", &desired_cols);

int
sc_set_text_mode(scr_stat *scp, struct tty *tp, int mode, int xsize, int ysize,
		 int fontsize)
{
    video_info_t info;
    u_char *font;
    int prev_ysize;
    int new_ysize;
    int error;

    lwkt_gettoken(&tty_token);
    if ((*vidsw[scp->sc->adapter]->get_info)(scp->sc->adp, mode, &info)) {
	lwkt_reltoken(&tty_token);
	return ENODEV;
    }
    lwkt_reltoken(&tty_token);

    /* adjust argument values */
    if (fontsize <= 0)
	fontsize = info.vi_cheight;
    if (fontsize < 14) {
	fontsize = 8;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_8))
	    return EINVAL;
	font = scp->sc->font_8;
#else
	font = NULL;
#endif
    } else if (fontsize >= 16) {
	fontsize = 16;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_16))
	    return EINVAL;
	font = scp->sc->font_16;
#else
	font = NULL;
#endif
    } else {
	fontsize = 14;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_14))
	    return EINVAL;
	font = scp->sc->font_14;
#else
	font = NULL;
#endif
    }
    if ((xsize <= 0) || (xsize > info.vi_width))
	xsize = info.vi_width;
    if ((ysize <= 0) || (ysize > info.vi_height))
	ysize = info.vi_height;

    /* stop screen saver, etc */
    crit_enter();
    if ((error = sc_clean_up(scp))) {
	crit_exit();
	return error;
    }

    if (scp->sc->fbi != NULL &&
	sc_render_match(scp, "kms", V_INFO_MM_TEXT) == NULL) {
	crit_exit();
	return ENODEV;
    }
    if (scp->sc->fbi == NULL &&
	sc_render_match(scp, scp->sc->adp->va_name, V_INFO_MM_TEXT) == NULL) {
	crit_exit();
	return ENODEV;
    }

    /* set up scp */
    new_ysize = 0;
#ifndef SC_NO_HISTORY
    if (scp->history != NULL) {
	sc_hist_save(scp);
	new_ysize = sc_vtb_rows(scp->history); 
    }
#endif
    prev_ysize = scp->ysize;
    /*
     * This is a kludge to fend off scrn_update() while we
     * muck around with scp. XXX
     */
    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
    scp->status &= ~(GRAPHICS_MODE | PIXEL_MODE | MOUSE_VISIBLE);
    scp->mode = mode;
    scp->model = V_INFO_MM_TEXT;
    scp->xsize = xsize;
    scp->ysize = ysize;
    scp->xoff = 0;
    scp->yoff = 0;
    scp->xpixel = scp->xsize*8;
    scp->ypixel = scp->ysize*fontsize;
    scp->font = font;
    scp->font_height = fontsize;
    scp->font_width = 8;

    /* allocate buffers */
    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, new_ysize, prev_ysize, FALSE);
#endif
    crit_exit();

    if (scp == scp->sc->cur_scp)
	set_mode(scp);
    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    DPRINTF(5, ("ws_*size (%d,%d), size (%d,%d)\n",
	tp->t_winsize.ws_col, tp->t_winsize.ws_row, scp->xsize, scp->ysize));
    if (tp->t_winsize.ws_col != scp->xsize
	|| tp->t_winsize.ws_row != scp->ysize) {
	tp->t_winsize.ws_col = scp->xsize;
	tp->t_winsize.ws_row = scp->ysize;
	pgsignal(tp->t_pgrp, SIGWINCH, 1);
    }

    return 0;
}

int
sc_set_graphics_mode(scr_stat *scp, struct tty *tp, int mode)
{
#ifdef SC_NO_MODE_CHANGE
    return ENODEV;
#else
    video_info_t info;
    int error;

    lwkt_gettoken(&tty_token);
    if ((*vidsw[scp->sc->adapter]->get_info)(scp->sc->adp, mode, &info)) {
        lwkt_reltoken(&tty_token);
	return ENODEV;
    }
    lwkt_reltoken(&tty_token);

    /* stop screen saver, etc */
    crit_enter();
    if ((error = sc_clean_up(scp))) {
	crit_exit();
	return error;
    }

    if (scp->sc->fbi != NULL &&
	sc_render_match(scp, "kms", V_INFO_MM_OTHER) == NULL) {
	crit_exit();
	return ENODEV;
    }
    if (scp->sc->fbi == NULL &&
	sc_render_match(scp, scp->sc->adp->va_name, V_INFO_MM_OTHER) == NULL) {
	crit_exit();
	return ENODEV;
    }

    /* set up scp */
    scp->status |= (UNKNOWN_MODE | GRAPHICS_MODE | MOUSE_HIDDEN);
    scp->status &= ~(PIXEL_MODE | MOUSE_VISIBLE);
    scp->mode = mode;
    scp->model = V_INFO_MM_OTHER;
    /*
     * Don't change xsize and ysize; preserve the previous vty
     * and history buffers.
     */
    scp->xoff = 0;
    scp->yoff = 0;
    scp->xpixel = info.vi_width;
    scp->ypixel = info.vi_height;
    scp->font = NULL;
    scp->font_height = 0;
    scp->font_width = 0;
#ifndef SC_NO_SYSMOUSE
    /* move the mouse cursor at the center of the screen */
    sc_mouse_move(scp, scp->xpixel / 2, scp->ypixel / 2);
#endif
    sc_init_emulator(scp, NULL);
    crit_exit();

    if (scp == scp->sc->cur_scp)
	set_mode(scp);
    /* clear_graphics();*/
    refresh_ega_palette(scp);
    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    if (tp->t_winsize.ws_xpixel != scp->xpixel
	|| tp->t_winsize.ws_ypixel != scp->ypixel) {
	tp->t_winsize.ws_xpixel = scp->xpixel;
	tp->t_winsize.ws_ypixel = scp->ypixel;
	pgsignal(tp->t_pgrp, SIGWINCH, 1);
    }

    return 0;
#endif /* SC_NO_MODE_CHANGE */
}

int
sc_set_pixel_mode(scr_stat *scp, struct tty *tp, int xsize, int ysize, 
		  int fontsize)
{
#ifndef SC_PIXEL_MODE
    return ENODEV;
#else
    video_info_t info;
    u_char *font;
    int prev_ysize;
    int new_ysize;
    int error;

    lwkt_gettoken(&tty_token);
    if ((*vidsw[scp->sc->adapter]->get_info)(scp->sc->adp, scp->mode, &info)) {
        lwkt_reltoken(&tty_token);
	return ENODEV;		/* this shouldn't happen */
    }
    lwkt_reltoken(&tty_token);

    /* adjust argument values */
    if (fontsize <= 0)
	fontsize = info.vi_cheight;
    if (fontsize < 14) {
	fontsize = 8;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_8))
	    return EINVAL;
	font = scp->sc->font_8;
#else
	font = NULL;
#endif
    } else if (fontsize >= 16) {
	fontsize = 16;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_16))
	    return EINVAL;
	font = scp->sc->font_16;
#else
	font = NULL;
#endif
    } else {
	fontsize = 14;
#ifndef SC_NO_FONT_LOADING
	if (!(scp->sc->fonts_loaded & FONT_14))
	    return EINVAL;
	font = scp->sc->font_14;
#else
	font = NULL;
#endif
    }
    if (xsize <= 0)
	xsize = info.vi_width/8;
    if (ysize <= 0)
	ysize = info.vi_height/fontsize;

    if ((info.vi_width < xsize*8) || (info.vi_height < ysize*fontsize))
	return EINVAL;

    /*
     * We currently support the following graphic modes:
     *
     * - 4 bpp planar modes whose memory size does not exceed 64K
     * - 8 bbp packed pixel modes
     * - 15, 16, 24 and 32 bpp direct modes with linear frame buffer
     */

    if (info.vi_mem_model == V_INFO_MM_PLANAR) {
	if (info.vi_planes != 4)
	    return ENODEV;

	/*
	 * A memory size >64K requires bank switching to access the entire
	 * screen. XXX
	 */

	if (info.vi_width * info.vi_height / 8 > info.vi_window_size)
	    return ENODEV;
    } else if (info.vi_mem_model == V_INFO_MM_PACKED) {
	if (info.vi_depth != 8)
	    return ENODEV;
    } else if (info.vi_mem_model == V_INFO_MM_DIRECT) {
	if (!(info.vi_flags & V_INFO_LINEAR) &&
	    (info.vi_depth != 15) && (info.vi_depth != 16) &&
	    (info.vi_depth != 24) && (info.vi_depth != 32))
	    return ENODEV;
    } else
	return ENODEV;

    /* stop screen saver, etc */
    crit_enter();
    if ((error = sc_clean_up(scp))) {
	crit_exit();
	return error;
    }

    if (sc_render_match(scp, scp->sc->adp->va_name, info.vi_mem_model) == NULL) {
	crit_exit();
	return ENODEV;
    }

#if 0
    if (scp->tsw)
	(*scp->tsw->te_term)(scp, scp->ts);
    scp->tsw = NULL;
    scp->ts = NULL;
#endif

    /* set up scp */
    new_ysize = 0;
#ifndef SC_NO_HISTORY
    if (scp->history != NULL) {
	sc_hist_save(scp);
	new_ysize = sc_vtb_rows(scp->history);
    }
#endif
    prev_ysize = scp->ysize;
    scp->status |= (UNKNOWN_MODE | PIXEL_MODE | MOUSE_HIDDEN);
    scp->status &= ~(GRAPHICS_MODE | MOUSE_VISIBLE);
    scp->model = info.vi_mem_model;
    scp->xsize = xsize;
    scp->ysize = ysize;
    scp->xoff = (scp->xpixel/8 - xsize)/2;
    scp->yoff = (scp->ypixel/fontsize - ysize)/2;
    scp->font = font;
    scp->font_height = fontsize;
    scp->font_width = 8;

    /* allocate buffers */
    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, new_ysize, prev_ysize, FALSE);
#endif
    crit_exit();

    if (scp == scp->sc->cur_scp) {
	sc_set_border(scp, scp->border);
	sc_set_cursor_image(scp);
    }

    scp->status &= ~UNKNOWN_MODE;

    if (tp == NULL)
	return 0;
    if (tp->t_winsize.ws_col != scp->xsize
	|| tp->t_winsize.ws_row != scp->ysize) {
	tp->t_winsize.ws_col = scp->xsize;
	tp->t_winsize.ws_row = scp->ysize;
	pgsignal(tp->t_pgrp, SIGWINCH, 1);
    }

    return 0;
#endif /* SC_PIXEL_MODE */
}

#define fb_ioctl(a, c, d)		\
	(((a) == NULL) ? ENODEV : 	\
			 (*vidsw[(a)->va_index]->ioctl)((a), (c), (caddr_t)(d)))

int
sc_vid_ioctl(struct tty *tp, u_long cmd, caddr_t data, int flag)
{
    scr_stat *scp;
    video_adapter_t *adp;
#ifndef SC_NO_MODE_CHANGE
    video_info_t info;
#endif
    int error, ret;

	KKASSERT(tp->t_dev);

    scp = SC_STAT(tp->t_dev);
    if (scp == NULL)		/* tp == SC_MOUSE */
		return ENOIOCTL;
    adp = scp->sc->adp;
    if (adp == NULL)		/* shouldn't happen??? */
		return ENODEV;

    lwkt_gettoken(&tty_token);
    switch (cmd) {

    case CONS_CURRENTADP:	/* get current adapter index */
    case FBIO_ADAPTER:
	ret = fb_ioctl(adp, FBIO_ADAPTER, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case CONS_CURRENT:  	/* get current adapter type */
    case FBIO_ADPTYPE:
	ret = fb_ioctl(adp, FBIO_ADPTYPE, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case CONS_ADPINFO:		/* adapter information */
    case FBIO_ADPINFO:
	if (((video_adapter_info_t *)data)->va_index >= 0) {
	    adp = vid_get_adapter(((video_adapter_info_t *)data)->va_index);
	    if (adp == NULL) {
		lwkt_reltoken(&tty_token);
		return ENODEV;
	    }
	}
	ret = fb_ioctl(adp, FBIO_ADPINFO, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case CONS_GET:      	/* get current video mode */
    case FBIO_GETMODE:
	*(int *)data = scp->mode;
	lwkt_reltoken(&tty_token);
	return 0;

#ifndef SC_NO_MODE_CHANGE
    case CONS_SET:
    case FBIO_SETMODE:		/* set video mode */
	if (!(adp->va_flags & V_ADP_MODECHANGE)) {
	    lwkt_reltoken(&tty_token);
 	    return ENODEV;
	}
	info.vi_mode = *(int *)data;
	error = fb_ioctl(adp, FBIO_MODEINFO, &info);
	if (error) {
	    lwkt_reltoken(&tty_token);
	    return error;
	}
	if (info.vi_flags & V_INFO_GRAPHICS) {
	    lwkt_reltoken(&tty_token);
	    return sc_set_graphics_mode(scp, tp, *(int *)data);
	} else {
	    lwkt_reltoken(&tty_token);
	    return sc_set_text_mode(scp, tp, *(int *)data, 0, 0, 0);
	}
#endif /* SC_NO_MODE_CHANGE */

    case CONS_MODEINFO:		/* get mode information */
    case FBIO_MODEINFO:
	ret = fb_ioctl(adp, FBIO_MODEINFO, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case CONS_FINDMODE:		/* find a matching video mode */
    case FBIO_FINDMODE:
	ret = fb_ioctl(adp, FBIO_FINDMODE, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case CONS_SETWINORG:	/* set frame buffer window origin */
    case FBIO_SETWINORG:
	if (scp != scp->sc->cur_scp) {
	    lwkt_reltoken(&tty_token);
	    return ENODEV;	/* XXX */
	}
	ret = fb_ioctl(adp, FBIO_SETWINORG, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case FBIO_GETWINORG:	/* get frame buffer window origin */
	if (scp != scp->sc->cur_scp) {
	    lwkt_reltoken(&tty_token);
	    return ENODEV;	/* XXX */
	}
	ret = fb_ioctl(adp, FBIO_GETWINORG, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case FBIO_GETDISPSTART:
    case FBIO_SETDISPSTART:
    case FBIO_GETLINEWIDTH:
    case FBIO_SETLINEWIDTH:
	if (scp != scp->sc->cur_scp) {
	    lwkt_reltoken(&tty_token);
	    return ENODEV;	/* XXX */
	}
	ret = fb_ioctl(adp, cmd, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case FBIO_GETPALETTE:
    case FBIO_SETPALETTE:
    case FBIOPUTCMAP:
    case FBIOGETCMAP:
    case FBIOGTYPE:
    case FBIOGATTR:
    case FBIOSVIDEO:
    case FBIOGVIDEO:
    case FBIOSCURSOR:
    case FBIOGCURSOR:
    case FBIOSCURPOS:
    case FBIOGCURPOS:
    case FBIOGCURMAX:
	if (scp != scp->sc->cur_scp) {
	    lwkt_reltoken(&tty_token);
	    return ENODEV;	/* XXX */
	}
	ret = fb_ioctl(adp, cmd, data);
	lwkt_reltoken(&tty_token);
	return ret;

    case KDSETMODE:     	/* set current mode of this (virtual) console */
	switch (*(int *)data) {
	case KD_TEXT:   	/* switch to TEXT (known) mode */
	    /*
	     * If scp->mode is of graphics modes, we don't know which
	     * text mode to switch back to...
	     */
	    if (scp->status & GRAPHICS_MODE) {
	        lwkt_reltoken(&tty_token);
		return EINVAL;
	    }
	    /* restore fonts & palette ! */
#if 0
#ifndef SC_NO_FONT_LOADING
	    if (ISFONTAVAIL(adp->va_flags) 
		&& !(scp->status & (GRAPHICS_MODE | PIXEL_MODE)))
		/*
		 * FONT KLUDGE
		 * Don't load fonts for now... XXX
		 */
		if (scp->sc->fonts_loaded & FONT_8)
		    sc_load_font(scp, 0, 8, scp->sc->font_8, 0, 256);
		if (scp->sc->fonts_loaded & FONT_14)
		    sc_load_font(scp, 0, 14, scp->sc->font_14, 0, 256);
		if (scp->sc->fonts_loaded & FONT_16)
		    sc_load_font(scp, 0, 16, scp->sc->font_16, 0, 256);
	    }
#endif /* SC_NO_FONT_LOADING */
#endif

#ifndef SC_NO_PALETTE_LOADING
	    load_palette(adp, scp->sc->palette);
#endif

	    /* move hardware cursor out of the way */
	    (*vidsw[adp->va_index]->set_hw_cursor)(adp, -1, -1);
	    /* FALL THROUGH */

	case KD_TEXT1:  	/* switch to TEXT (known) mode */
	    /*
	     * If scp->mode is of graphics modes, we don't know which
	     * text/pixel mode to switch back to...
	     */
	    if (scp->status & GRAPHICS_MODE) {
	        lwkt_reltoken(&tty_token);
		return EINVAL;
	    }
	    crit_enter();
	    if ((error = sc_clean_up(scp))) {
		crit_exit();
		lwkt_reltoken(&tty_token);
		return error;
	    }
	    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
	    crit_exit();
	    /* no restore fonts & palette */
	    if (scp == scp->sc->cur_scp)
		set_mode(scp);
	    sc_clear_screen(scp);
	    scp->status &= ~UNKNOWN_MODE;
	    lwkt_reltoken(&tty_token);
	    return 0;

#ifdef SC_PIXEL_MODE
	case KD_PIXEL:		/* pixel (raster) display */
	    if (!(scp->status & (GRAPHICS_MODE | PIXEL_MODE))) {
	        lwkt_reltoken(&tty_token);
		return EINVAL;
            }
	    if (scp->status & GRAPHICS_MODE) {
	        lwkt_reltoken(&tty_token);
		return sc_set_pixel_mode(scp, tp, scp->xsize, scp->ysize, 
					 scp->font_height);
	    }
	    crit_enter();
	    if ((error = sc_clean_up(scp))) {
		crit_exit();
		lwkt_reltoken(&tty_token);
		return error;
	    }
	    scp->status |= (UNKNOWN_MODE | PIXEL_MODE | MOUSE_HIDDEN);
	    crit_exit();
	    if (scp == scp->sc->cur_scp) {
		set_mode(scp);
#ifndef SC_NO_PALETTE_LOADING
		load_palette(adp, scp->sc->palette);
#endif
	    }
	    sc_clear_screen(scp);
	    scp->status &= ~UNKNOWN_MODE;
	    lwkt_reltoken(&tty_token);
	    return 0;
#endif /* SC_PIXEL_MODE */

	case KD_GRAPHICS:	/* switch to GRAPHICS (unknown) mode */
	    crit_enter();
	    if ((error = sc_clean_up(scp))) {
		crit_exit();
		lwkt_reltoken(&tty_token);
		return error;
	    }
	    scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
	    crit_exit();
	    lwkt_reltoken(&tty_token);
	    return 0;

	default:
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	/* NOT REACHED */

#ifdef SC_PIXEL_MODE
    case KDRASTER:		/* set pixel (raster) display mode */
	if (ISUNKNOWNSC(scp) || ISTEXTSC(scp)) {
	    lwkt_reltoken(&tty_token);
	    return ENODEV;
	}
	lwkt_reltoken(&tty_token);
	return sc_set_pixel_mode(scp, tp, ((int *)data)[0], ((int *)data)[1], 
				 ((int *)data)[2]);
#endif /* SC_PIXEL_MODE */

    case KDGETMODE:     	/* get current mode of this (virtual) console */
	/* 
	 * From the user program's point of view, KD_PIXEL is the same 
	 * as KD_TEXT... 
	 */
	*data = ISGRAPHSC(scp) ? KD_GRAPHICS : KD_TEXT;
	lwkt_reltoken(&tty_token);
	return 0;

    case KDSBORDER:     	/* set border color of this (virtual) console */
	scp->border = *data;
	if (scp == scp->sc->cur_scp)
	    sc_set_border(scp, scp->border);
	lwkt_reltoken(&tty_token);
	return 0;
    }

    lwkt_reltoken(&tty_token);
    return ENOIOCTL;
}

static LIST_HEAD(, sc_renderer) sc_rndr_list = 
	LIST_HEAD_INITIALIZER(sc_rndr_list);

int
sc_render_add(sc_renderer_t *rndr)
{
	LIST_INSERT_HEAD(&sc_rndr_list, rndr, link);
	return 0;
}

int
sc_render_remove(sc_renderer_t *rndr)
{
	/*
	LIST_REMOVE(rndr, link);
	*/
	return EBUSY;	/* XXX */
}

sc_rndr_sw_t *
sc_render_match(scr_stat *scp, char *name, int model)
{
	const sc_renderer_t **list;
	const sc_renderer_t *p;

	if (!LIST_EMPTY(&sc_rndr_list)) {
		LIST_FOREACH(p, &sc_rndr_list, link) {
			if ((strcmp(p->name, name) == 0) &&
			    (model == p->model)) {
				scp->status &=
				    ~(VR_CURSOR_ON | VR_CURSOR_BLINK);
				return p->rndrsw;
			}
		}
	} else {
		SET_FOREACH(list, scrndr_set) {
			p = *list;
			if ((strcmp(p->name, name) == 0) &&
			    (model == p->model)) {
				scp->status &=
				    ~(VR_CURSOR_ON | VR_CURSOR_BLINK);
				return p->rndrsw;
			}
		}
	}

	return NULL;
}

#define VIRTUAL_TTY(sc, x) ((SC_DEV((sc),(x)) != NULL) ?	\
	(SC_DEV((sc),(x))->si_tty) : NULL)

void
sc_update_render(scr_stat *scp)
{
	sc_rndr_sw_t *rndr;
	sc_term_sw_t *sw;
	struct tty *tp;
	int prev_ysize, new_ysize;
	int error;

	sw = scp->tsw;
	if (sw == NULL) {
		return;
	}

	if (scp->rndr == NULL)
		return;

	if (scp->fbi == scp->sc->fbi)
		return;

	crit_enter();
	scp->fbi = scp->sc->fbi;
	rndr = NULL;
	if (strcmp(sw->te_renderer, "*") != 0) {
		rndr = sc_render_match(scp, sw->te_renderer, scp->model);
	}
	if (rndr == NULL && scp->sc->fbi != NULL) {
		rndr = sc_render_match(scp, "kms", scp->model);
	}
	if (rndr != NULL) {
		scp->rndr = rndr;
		/* Mostly copied from sc_set_text_mode */
		if ((error = sc_clean_up(scp))) {
			crit_exit();
			return;
		}
		new_ysize = 0;
#ifndef SC_NO_HISTORY
		if (scp->history != NULL) {
			sc_hist_save(scp);
			new_ysize = sc_vtb_rows(scp->history);
		}
#endif
		prev_ysize = scp->ysize;
		scp->status |= UNKNOWN_MODE | MOUSE_HIDDEN;
		scp->status &= ~(GRAPHICS_MODE | PIXEL_MODE | MOUSE_VISIBLE);
		scp->model = V_INFO_MM_TEXT;
		scp->xpixel = scp->fbi->width;
		scp->ypixel = scp->fbi->height;

		/*
		 * Assume square pixels for now
		 */
		kprintf("kms console: xpixels %d ypixels %d\n",
			scp->xpixel, scp->ypixel);

		/*
		 * If columns not specified in /boot/loader.conf then
		 * calculate a non-fractional scaling that yields a
		 * reasonable number of rows and columns.
		 */
		if (desired_cols == 0) {
			int nomag = 1;
			while (scp->xpixel / (scp->font_width * nomag) >= 80 &&
			       scp->ypixel / (scp->font_height * nomag) >= 25) {
				++nomag;
			}
			if (nomag > 1)
				--nomag;
			desired_cols = scp->xpixel / (scp->font_width * nomag);
		}
		scp->blk_width = scp->xpixel / desired_cols;
		scp->blk_height = scp->blk_width * scp->font_height /
				  scp->font_width;

		/* scp->xsize = scp->xpixel / scp->blk_width; total possible */
		scp->xsize = desired_cols;
		scp->ysize = scp->ypixel / scp->blk_height;
		scp->xpad = scp->fbi->stride / 4 - scp->xsize * scp->blk_width;

		kprintf("kms console: scale-to %dx%d cols=%d rows=%d\n",
			scp->blk_width, scp->blk_height,
			scp->xsize, scp->ysize);

		/* allocate buffers */
		sc_alloc_scr_buffer(scp, TRUE, TRUE);
		sc_init_emulator(scp, NULL);
#ifndef SC_NO_CUTPASTE
		sc_alloc_cut_buffer(scp, FALSE);
#endif
#ifndef SC_NO_HISTORY
		sc_alloc_history_buffer(scp, new_ysize, prev_ysize, FALSE);
#endif
		crit_exit();
		scp->status &= ~UNKNOWN_MODE;
		tp = VIRTUAL_TTY(scp->sc, scp->index);
		if (tp == NULL)
			return;
		if (tp->t_winsize.ws_col != scp->xsize ||
		    tp->t_winsize.ws_row != scp->ysize) {
			tp->t_winsize.ws_col = scp->xsize;
			tp->t_winsize.ws_row = scp->ysize;
			pgsignal(tp->t_pgrp, SIGWINCH, 1);
		}
		return;
	}
	if (rndr == NULL) {
		rndr = sc_render_match(scp, scp->sc->adp->va_name, scp->model);
	}

	if (rndr != NULL) {
		scp->rndr = rndr;
	}
	crit_exit();
}
