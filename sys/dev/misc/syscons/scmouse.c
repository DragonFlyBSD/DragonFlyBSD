/*-
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
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
 *
 * $FreeBSD: src/sys/dev/syscons/scmouse.c,v 1.12.2.3 2001/07/28 12:51:47 yokota Exp $
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <machine/console.h>
#include <sys/mouse.h>

#include "syscons.h"

#ifdef SC_TWOBUTTON_MOUSE
#define SC_MOUSE_PASTEBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#define SC_MOUSE_EXTENDBUTTON	MOUSE_BUTTON2DOWN	/* not really used */
#else
#define SC_MOUSE_PASTEBUTTON	MOUSE_BUTTON2DOWN	/* middle button */
#define SC_MOUSE_EXTENDBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#endif /* SC_TWOBUTTON_MOUSE */

#define SC_WAKEUP_DELTA		20

#ifndef SC_NO_SYSMOUSE

static int		cut_buffer_size;
static u_char		*cut_buffer;

/* local functions */
static void sc_mouse_init(void *);
static void sc_mouse_uninit(void *);

static void set_mouse_pos(scr_stat *scp);
#ifndef SC_NO_CUTPASTE
static int skip_spc_right(scr_stat *scp, int p);
static int skip_spc_left(scr_stat *scp, int p);
static void mouse_cut(scr_stat *scp);
static void mouse_cut_start(scr_stat *scp);
static void mouse_cut_end(scr_stat *scp);
static void mouse_cut_word(scr_stat *scp);
static void mouse_cut_line(scr_stat *scp);
static void mouse_cut_extend(scr_stat *scp);
static void mouse_paste(scr_stat *scp);
#endif /* SC_NO_CUTPASTE */

#ifndef SC_NO_CUTPASTE
/* allocate a cut buffer */
void
sc_alloc_cut_buffer(scr_stat *scp, int wait)
{
    u_char *p;

    if ((cut_buffer == NULL)
	|| (cut_buffer_size < scp->xsize * scp->ysize + 1)) {
	p = cut_buffer;
	cut_buffer = NULL;
	if (p != NULL)
	    kfree(p, M_SYSCONS);
	cut_buffer_size = scp->xsize * scp->ysize + 1;
	p = kmalloc(cut_buffer_size, M_SYSCONS, (wait) ? M_WAITOK : M_NOWAIT);
	if (p != NULL)
	    p[0] = '\0';
	cut_buffer = p;
    }
}
#endif /* SC_NO_CUTPASTE */

/* move mouse */
void
sc_mouse_move(scr_stat *scp, int x, int y)
{
    crit_enter();
    scp->mouse_xpos = scp->mouse_oldxpos = x;
    scp->mouse_ypos = scp->mouse_oldypos = y;
    if (scp->font_height <= 0)
	scp->mouse_pos = scp->mouse_oldpos = 0;
    else
	scp->mouse_pos = scp->mouse_oldpos = 
	    (y / scp->font_height - scp->yoff) * scp->xsize +
	    x / scp->font_width - scp->xoff;
    scp->status |= MOUSE_MOVED;
    crit_exit();
}

/* adjust mouse position */
static void
set_mouse_pos(scr_stat *scp)
{
    if (scp->mouse_xpos < scp->xoff * scp->font_width)
	scp->mouse_xpos = scp->xoff * scp->font_width;
    if (scp->mouse_ypos < scp->yoff * scp->font_height)
	scp->mouse_ypos = scp->yoff * scp->font_height;
    if (ISGRAPHSC(scp)) {
        if (scp->mouse_xpos > scp->xpixel - 1)
	    scp->mouse_xpos = scp->xpixel - 1;
        if (scp->mouse_ypos > scp->ypixel - 1)
	    scp->mouse_ypos = scp->ypixel - 1;
	return;
    } else {
	if (scp->mouse_xpos > (scp->xsize + scp->xoff) * scp->font_width - 1)
	    scp->mouse_xpos = (scp->xsize + scp->xoff) * scp->font_width - 1;
	if (scp->mouse_ypos > (scp->ysize + scp->yoff) * scp->font_height - 1)
	    scp->mouse_ypos = (scp->ysize + scp->yoff) * scp->font_height - 1;
    }

    if (scp->mouse_xpos != scp->mouse_oldxpos ||
	scp->mouse_ypos != scp->mouse_oldypos) {
	scp->status |= MOUSE_MOVED;
    	scp->mouse_pos =
	    (scp->mouse_ypos / scp->font_height - scp->yoff) * scp->xsize +
	    scp->mouse_xpos / scp->font_width - scp->xoff;
#ifndef SC_NO_CUTPASTE
	if ((scp->status & MOUSE_VISIBLE) && (scp->status & MOUSE_CUTTING))
	    mouse_cut(scp);
#endif
    }
}

#ifndef SC_NO_CUTPASTE

void
sc_draw_mouse_image(scr_stat *scp)
{
    if (ISGRAPHSC(scp))
	return;

    atomic_add_int(&scp->sc->videoio_in_progress, 1);
    (*scp->rndr->draw_mouse)(scp, scp->mouse_xpos, scp->mouse_ypos, TRUE);
    scp->mouse_oldpos = scp->mouse_pos;
    scp->mouse_oldxpos = scp->mouse_xpos;
    scp->mouse_oldypos = scp->mouse_ypos;
    scp->status |= MOUSE_VISIBLE;
    atomic_add_int(&scp->sc->videoio_in_progress, -1);
}

void
sc_remove_mouse_image(scr_stat *scp)
{
    int size;
    int i;

    if (ISGRAPHSC(scp))
	return;

    atomic_add_int(&scp->sc->videoio_in_progress, 1);
    (*scp->rndr->draw_mouse)(scp,
			     (scp->mouse_oldpos%scp->xsize + scp->xoff) *
			      scp->font_width,
			     (scp->mouse_oldpos/scp->xsize + scp->yoff) *
			      scp->font_height,
			     FALSE);
    size = scp->xsize * scp->ysize;
    i = scp->mouse_oldpos;
    mark_for_update(scp, i);
    mark_for_update(scp, i);
    if (i + scp->xsize + 1 < size) {
	mark_for_update(scp, i + scp->xsize + 1);
    } else if (i + scp->xsize < size) {
	mark_for_update(scp, i + scp->xsize);
    } else if (i + 1 < size) {
	mark_for_update(scp, i + 1);
    }
    scp->status &= ~MOUSE_VISIBLE;
    atomic_add_int(&scp->sc->videoio_in_progress, -1);
}

int
sc_inside_cutmark(scr_stat *scp, int pos)
{
    int start;
    int end;

    if (scp->mouse_cut_end < 0)
	return FALSE;
    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	start = scp->mouse_cut_start;
	end = scp->mouse_cut_end;
    } else {
	start = scp->mouse_cut_end;
	end = scp->mouse_cut_start - 1;
    }
    return ((start <= pos) && (pos <= end));
}

void
sc_remove_cutmarking(scr_stat *scp)
{
    crit_enter();
    if (scp->mouse_cut_end >= 0) {
	mark_for_update(scp, scp->mouse_cut_start);
	mark_for_update(scp, scp->mouse_cut_end);
    }
    scp->mouse_cut_start = scp->xsize*scp->ysize;
    scp->mouse_cut_end = -1;
    crit_exit();
    scp->status &= ~MOUSE_CUTTING;
}

void
sc_remove_all_cutmarkings(sc_softc_t *sc)
{
    scr_stat *scp;
    int i;

    /* delete cut markings in all vtys */
    for (i = 0; i < sc->vtys; ++i) {
	scp = SC_STAT(sc->dev[i]);
	if (scp == NULL)
	    continue;
	sc_remove_cutmarking(scp);
    }
}

void
sc_remove_all_mouse(sc_softc_t *sc)
{
    scr_stat *scp;
    int i;

    for (i = 0; i < sc->vtys; ++i) {
	scp = SC_STAT(sc->dev[i]);
	if (scp == NULL)
	    continue;
	if (scp->status & MOUSE_VISIBLE) {
	    scp->status &= ~MOUSE_VISIBLE;
	    mark_all(scp);
	}
    }
}

#define IS_SPACE_CHAR(c)	(((c) & 0xff) == ' ')

/* skip spaces to right */
static int
skip_spc_right(scr_stat *scp, int p)
{
    int c;
    int i;

    for (i = p % scp->xsize; i < scp->xsize; ++i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	++p;
    }
    return i;
}

/* skip spaces to left */
static int
skip_spc_left(scr_stat *scp, int p)
{
    int c;
    int i;

    for (i = p-- % scp->xsize - 1; i >= 0; --i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	--p;
    }
    return i;
}

/* copy marked region to the cut buffer */
static void
mouse_cut(scr_stat *scp)
{
    int start;
    int end;
    int from;
    int to;
    int blank;
    int c;
    int p;
    int i;

    start = scp->mouse_cut_start;
    if (scp->mouse_pos >= start) {
	from = start;
	to = end = scp->mouse_pos;
    } else {
	from = end = scp->mouse_pos;
	to = start - 1;
    }
    for (p = from, i = blank = 0; p <= to; ++p) {
	cut_buffer[i] = sc_vtb_getc(&scp->vtb, p);
	/* remember the position of the last non-space char */
	if (!IS_SPACE_CHAR(cut_buffer[i++]))
	    blank = i;		/* the first space after the last non-space */
	/* trim trailing blank when crossing lines */
	if ((p % scp->xsize) == (scp->xsize - 1)) {
	    cut_buffer[blank] = '\r';
	    i = blank + 1;
	}
    }
    cut_buffer[i] = '\0';

    /* scan towards the end of the last line */
    --p;
    for (i = p % scp->xsize; i < scp->xsize; ++i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	++p;
    }
    /* if there is nothing but blank chars, trim them, but mark towards eol */
    if (i >= scp->xsize) {
	if (end >= start)
	    to = end = p - 1;
	else
	    to = start = p;
	cut_buffer[blank++] = '\r';
	cut_buffer[blank] = '\0';
    }

    /* remove the current marking */
    crit_enter();
    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	mark_for_update(scp, scp->mouse_cut_start);
	mark_for_update(scp, scp->mouse_cut_end);
    } else if (scp->mouse_cut_end >= 0) {
	mark_for_update(scp, scp->mouse_cut_end);
	mark_for_update(scp, scp->mouse_cut_start);
    }

    /* mark the new region */
    scp->mouse_cut_start = start;
    scp->mouse_cut_end = end;
    mark_for_update(scp, from);
    mark_for_update(scp, to);
    crit_exit();
}

/* a mouse button is pressed, start cut operation */
static void
mouse_cut_start(scr_stat *scp) 
{
    int i;
    int j;

    if (scp->status & MOUSE_VISIBLE) {
	i = scp->mouse_cut_start;
	j = scp->mouse_cut_end;
	sc_remove_all_cutmarkings(scp->sc);
	if (scp->mouse_pos == i && i == j) {
	    cut_buffer[0] = '\0';
	} else if (skip_spc_right(scp, scp->mouse_pos) >= scp->xsize) {
	    /* if the pointer is on trailing blank chars, mark towards eol */
	    i = skip_spc_left(scp, scp->mouse_pos) + 1;
	    crit_enter();
	    scp->mouse_cut_start =
	        (scp->mouse_pos / scp->xsize) * scp->xsize + i;
	    scp->mouse_cut_end =
	        (scp->mouse_pos / scp->xsize + 1) * scp->xsize - 1;
	    crit_exit();
	    cut_buffer[0] = '\r';
	    cut_buffer[1] = '\0';
	    scp->status |= MOUSE_CUTTING;
	} else {
	    crit_enter();
	    scp->mouse_cut_start = scp->mouse_pos;
	    scp->mouse_cut_end = scp->mouse_cut_start;
	    crit_exit();
	    cut_buffer[0] = sc_vtb_getc(&scp->vtb, scp->mouse_cut_start);
	    cut_buffer[1] = '\0';
	    scp->status |= MOUSE_CUTTING;
	}
    	mark_all(scp);	/* this is probably overkill XXX */
    }
}

/* end of cut operation */
static void
mouse_cut_end(scr_stat *scp) 
{
    if (scp->status & MOUSE_VISIBLE)
	scp->status &= ~MOUSE_CUTTING;
}

/* copy a word under the mouse pointer */
static void
mouse_cut_word(scr_stat *scp)
{
    int start;
    int end;
    int sol;
    int eol;
    int c;
    int i;
    int j;

    /*
     * Because we don't have locale information in the kernel,
     * we only distinguish space char and non-space chars.  Punctuation
     * chars, symbols and other regular chars are all treated alike.
     */
    if (scp->status & MOUSE_VISIBLE) {
	/* remove the current cut mark */
	crit_enter();
	if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	    mark_for_update(scp, scp->mouse_cut_start);
	    mark_for_update(scp, scp->mouse_cut_end);
	} else if (scp->mouse_cut_end >= 0) {
	    mark_for_update(scp, scp->mouse_cut_end);
	    mark_for_update(scp, scp->mouse_cut_start);
	}
	scp->mouse_cut_start = scp->xsize*scp->ysize;
	scp->mouse_cut_end = -1;
	crit_exit();

	sol = (scp->mouse_pos / scp->xsize) * scp->xsize;
	eol = sol + scp->xsize;
	c = sc_vtb_getc(&scp->vtb, scp->mouse_pos);
	if (IS_SPACE_CHAR(c)) {
	    /* blank space */
	    for (j = scp->mouse_pos; j >= sol; --j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (!IS_SPACE_CHAR(c))
		    break;
	    }
	    start = ++j;
	    for (j = scp->mouse_pos; j < eol; ++j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (!IS_SPACE_CHAR(c))
		    break;
	    }
	    end = j - 1;
	} else {
	    /* non-space word */
	    for (j = scp->mouse_pos; j >= sol; --j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (IS_SPACE_CHAR(c))
		    break;
	    }
	    start = ++j;
	    for (j = scp->mouse_pos; j < eol; ++j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (IS_SPACE_CHAR(c))
		    break;
	    }
	    end = j - 1;
	}

	/* copy the found word */
	for (i = 0, j = start; j <= end; ++j)
	    cut_buffer[i++] = sc_vtb_getc(&scp->vtb, j);
	cut_buffer[i] = '\0';
	scp->status |= MOUSE_CUTTING;

	/* mark the region */
	crit_enter();
	scp->mouse_cut_start = start;
	scp->mouse_cut_end = end;
	mark_for_update(scp, start);
	mark_for_update(scp, end);
	crit_exit();
    }
}

/* copy a line under the mouse pointer */
static void
mouse_cut_line(scr_stat *scp)
{
    int i;
    int j;

    if (scp->status & MOUSE_VISIBLE) {
	/* remove the current cut mark */
	crit_enter();
	if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	    mark_for_update(scp, scp->mouse_cut_start);
	    mark_for_update(scp, scp->mouse_cut_end);
	} else if (scp->mouse_cut_end >= 0) {
	    mark_for_update(scp, scp->mouse_cut_end);
	    mark_for_update(scp, scp->mouse_cut_start);
	}

	/* mark the entire line */
	scp->mouse_cut_start =
	    (scp->mouse_pos / scp->xsize) * scp->xsize;
	scp->mouse_cut_end = scp->mouse_cut_start + scp->xsize - 1;
	mark_for_update(scp, scp->mouse_cut_start);
	mark_for_update(scp, scp->mouse_cut_end);
	crit_exit();

	/* copy the line into the cut buffer */
	for (i = 0, j = scp->mouse_cut_start; j <= scp->mouse_cut_end; ++j)
	    cut_buffer[i++] = sc_vtb_getc(&scp->vtb, j);
	cut_buffer[i++] = '\r';
	cut_buffer[i] = '\0';
	scp->status |= MOUSE_CUTTING;
    }
}

/* extend the marked region to the mouse pointer position */
static void
mouse_cut_extend(scr_stat *scp) 
{
    int start;
    int end;

    if ((scp->status & MOUSE_VISIBLE) && !(scp->status & MOUSE_CUTTING)
	&& (scp->mouse_cut_end >= 0)) {
	if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	    start = scp->mouse_cut_start;
	    end = scp->mouse_cut_end;
	} else {
	    start = scp->mouse_cut_end;
	    end = scp->mouse_cut_start - 1;
	}
	crit_enter();
	if (scp->mouse_pos > end) {
	    scp->mouse_cut_start = start;
	    scp->mouse_cut_end = end;
	} else if (scp->mouse_pos < start) {
	    scp->mouse_cut_start = end + 1;
	    scp->mouse_cut_end = start;
	} else {
	    if (scp->mouse_pos - start > end + 1 - scp->mouse_pos) {
		scp->mouse_cut_start = start;
		scp->mouse_cut_end = end;
	    } else {
		scp->mouse_cut_start = end + 1;
		scp->mouse_cut_end = start;
	    }
	}
	crit_exit();
	mouse_cut(scp);
	scp->status |= MOUSE_CUTTING;
    }
}

/* paste cut buffer contents into the current vty */
static void
mouse_paste(scr_stat *scp) 
{
    if (scp->status & MOUSE_VISIBLE)
	sc_paste(scp, cut_buffer, strlen(cut_buffer));
}

#endif /* SC_NO_CUTPASTE */

static void
sc_mouse_exit1_proc(struct proc *p)
{
    scr_stat *scp;

    scp = p->p_drv_priv;
    KKASSERT(scp != NULL);

    get_mplock();
    KKASSERT(scp->mouse_proc == p);
    KKASSERT(scp->mouse_pid == p->p_pid);

    scp->mouse_signal = 0;
    scp->mouse_proc = NULL;
    scp->mouse_pid = 0;
    rel_mplock();

    PRELE(p);
    p->p_flags &= ~P_SCMOUSE;
    p->p_drv_priv = NULL;
}

/*
 * sc_mouse_exit1:
 *
 *	Handle exit1 for processes registered as MOUSE_MODE handlers.
 *	We must remove a process hold, established when MOUSE_MODE
 *	was enabled.
 */
static void
sc_mouse_exit1(struct thread *td)
{
    struct proc *p;

    p = td->td_proc;
    KKASSERT(p != NULL);

    if ((p->p_flags & P_SCMOUSE) == 0)
	return;


    sc_mouse_exit1_proc(p);
}

int
sc_mouse_ioctl(struct tty *tp, u_long cmd, caddr_t data, int flag)
{
    mouse_info_t *mouse;
    scr_stat *cur_scp;
    scr_stat *scp;
    int f;

    scp = SC_STAT(tp->t_dev);

    switch (cmd) {

    case CONS_MOUSECTL:		/* control mouse arrow */
	mouse = (mouse_info_t*) data;
	cur_scp = scp->sc->cur_scp;

	switch (mouse->operation) {
	/*
	 * Setup a process to receive signals on mouse events.
	 */
	case MOUSE_MODE:
	    get_mplock();

	    if (!ISSIGVALID(mouse->u.mode.signal)) {
		/* Setting MOUSE_MODE w/ an invalid signal is used to disarm */
		if (scp->mouse_proc == curproc) {
		    sc_mouse_exit1_proc(curproc);
		    rel_mplock();
		    return 0;
		} else {
		    rel_mplock();
		    return EINVAL;
		}
	    } else {
		/* Only one mouse process per syscons */
		if (scp->mouse_proc) {
		    rel_mplock();
		    return EINVAL;
		}

		/* Only one syscons signal source per process */
		if (curproc->p_flags & P_SCMOUSE) {
		    rel_mplock();
		    return EINVAL;
		}

	        /*
	         * Process is stabilized by a hold, which is removed from
	         * sc_mouse_exit1. scp's mouse_{signal,proc,pid} fields
	         * are synchronized by the MP Lock.
	         */
	        scp->mouse_signal = mouse->u.mode.signal;
	        scp->mouse_proc = curproc;
	        scp->mouse_pid = curproc->p_pid;
	        curproc->p_flags |= P_SCMOUSE;
		KKASSERT(curproc->p_drv_priv == NULL);
	        curproc->p_drv_priv = scp;
	        PHOLD(curproc);

	        rel_mplock();
	        return 0;
            }
	    /*NOTREACHED*/
	    break;

	case MOUSE_SHOW:
	    crit_enter();
	    if (!(scp->sc->flags & SC_MOUSE_ENABLED)) {
		scp->sc->flags |= SC_MOUSE_ENABLED;
		cur_scp->status &= ~MOUSE_HIDDEN;
		if (!ISGRAPHSC(cur_scp))
		    mark_all(cur_scp);
		crit_exit();
		return 0;
	    } else {
		crit_exit();
		return EINVAL;
	    }
	    break;

	case MOUSE_HIDE:
	    crit_enter();
	    if (scp->sc->flags & SC_MOUSE_ENABLED) {
		scp->sc->flags &= ~SC_MOUSE_ENABLED;
		sc_remove_all_mouse(scp->sc);
		crit_exit();
		return 0;
	    } else {
		crit_exit();
		return EINVAL;
	    }
	    break;

	case MOUSE_MOVEABS:
	    crit_enter();
	    scp->mouse_xpos = mouse->u.data.x;
	    scp->mouse_ypos = mouse->u.data.y;
	    set_mouse_pos(scp);
	    crit_exit();
	    break;

	case MOUSE_MOVEREL:
	    crit_enter();
	    scp->mouse_xpos += mouse->u.data.x;
	    scp->mouse_ypos += mouse->u.data.y;
	    set_mouse_pos(scp);
	    crit_exit();
	    break;

	case MOUSE_GETINFO:
	    mouse->u.data.x = scp->mouse_xpos;
	    mouse->u.data.y = scp->mouse_ypos;
	    mouse->u.data.z = 0;
	    mouse->u.data.buttons = scp->mouse_buttons;
	    return 0;

	case MOUSE_ACTION:
	case MOUSE_MOTION_EVENT:
	    /* send out mouse event on /dev/sysmouse */
#if 0
	    /* this should maybe only be settable from /dev/consolectl SOS */
	    if (SC_VTY(tp->t_dev) != SC_CONSOLECTL)
		return ENOTTY;
#endif
	    crit_enter();
	    if (mouse->u.data.x != 0 || mouse->u.data.y != 0) {
		cur_scp->mouse_xpos += mouse->u.data.x;
		cur_scp->mouse_ypos += mouse->u.data.y;
		set_mouse_pos(cur_scp);
	    }
	    f = 0;
	    if (mouse->operation == MOUSE_ACTION) {
		f = cur_scp->mouse_buttons ^ mouse->u.data.buttons;
		cur_scp->mouse_buttons = mouse->u.data.buttons;
	    }
	    crit_exit();

	    if (sysmouse_event(mouse) == 0)
		return 0;

	    /* 
	     * If any buttons are down or the mouse has moved a lot, 
	     * stop the screen saver.
	     */
	    if (((mouse->operation == MOUSE_ACTION) && mouse->u.data.buttons)
		|| (mouse->u.data.x*mouse->u.data.x
			+ mouse->u.data.y*mouse->u.data.y
			>= SC_WAKEUP_DELTA*SC_WAKEUP_DELTA)) {
		sc_touch_scrn_saver();
	    }

	    cur_scp->status &= ~MOUSE_HIDDEN;

	    get_mplock();
	    if (cur_scp->mouse_signal) {
		KKASSERT(cur_scp->mouse_proc != NULL);
		ksignal(cur_scp->mouse_proc, cur_scp->mouse_signal);
		rel_mplock();
	        break;
	    }
	    rel_mplock();

	    if (ISGRAPHSC(cur_scp) || (cut_buffer == NULL))
		break;

#ifndef SC_NO_CUTPASTE
	    if ((mouse->operation == MOUSE_ACTION) && f) {
		/* process button presses */
		if (cur_scp->mouse_buttons & MOUSE_BUTTON1DOWN)
		    mouse_cut_start(cur_scp);
		else
		    mouse_cut_end(cur_scp);
		if (cur_scp->mouse_buttons & MOUSE_BUTTON2DOWN ||
		    cur_scp->mouse_buttons & MOUSE_BUTTON3DOWN)
		    mouse_paste(cur_scp);
	    }
#endif /* SC_NO_CUTPASTE */
	    break;

	case MOUSE_BUTTON_EVENT:
	    if ((mouse->u.event.id & MOUSE_BUTTONS) == 0)
		return EINVAL;
	    if (mouse->u.event.value < 0)
		return EINVAL;
#if 0
	    /* this should maybe only be settable from /dev/consolectl SOS */
	    if (SC_VTY(tp->t_dev) != SC_CONSOLECTL)
		return ENOTTY;
#endif
	    if (mouse->u.event.value > 0)
		cur_scp->mouse_buttons |= mouse->u.event.id;
	    else
		cur_scp->mouse_buttons &= ~mouse->u.event.id;

	    if (sysmouse_event(mouse) == 0)
		return 0;

	    /* if a button is held down, stop the screen saver */
	    if (mouse->u.event.value > 0)
		sc_touch_scrn_saver();

	    cur_scp->status &= ~MOUSE_HIDDEN;

	    get_mplock();
	    if (cur_scp->mouse_signal) {
		KKASSERT(cur_scp->mouse_proc != NULL);
		ksignal(cur_scp->mouse_proc, cur_scp->mouse_signal);
		rel_mplock();
	        break;
	    }
	    rel_mplock();

	    if (ISGRAPHSC(cur_scp) || (cut_buffer == NULL))
		break;

#ifndef SC_NO_CUTPASTE
	    switch (mouse->u.event.id) {
	    case MOUSE_BUTTON1DOWN:
	        switch (mouse->u.event.value % 4) {
		case 0:	/* up */
		    mouse_cut_end(cur_scp);
		    break;
		case 1: /* single click: start cut operation */
		    mouse_cut_start(cur_scp);
		    break;
		case 2:	/* double click: cut a word */
		    mouse_cut_word(cur_scp);
		    mouse_cut_end(cur_scp);
		    break;
		case 3:	/* triple click: cut a line */
		    mouse_cut_line(cur_scp);
		    mouse_cut_end(cur_scp);
		    break;
		}
		break;
	    case SC_MOUSE_PASTEBUTTON:
	        switch (mouse->u.event.value) {
		case 0:	/* up */
		    break;
		default:
		    mouse_paste(cur_scp);
		    break;
		}
		break;
	    case SC_MOUSE_EXTENDBUTTON:
	        switch (mouse->u.event.value) {
		case 0:	/* up */
		    if (!(cur_scp->mouse_buttons & MOUSE_BUTTON1DOWN))
		        mouse_cut_end(cur_scp);
		    break;
		default:
		    mouse_cut_extend(cur_scp);
		    break;
		}
		break;
	    }
#endif /* SC_NO_CUTPASTE */
	    break;

	case MOUSE_MOUSECHAR:
	    if (mouse->u.mouse_char < 0) {
		mouse->u.mouse_char = scp->sc->mouse_char;
	    } else {
		if (mouse->u.mouse_char >= (unsigned char)-1 - 4)
		    return EINVAL;
		crit_enter();
		sc_remove_all_mouse(scp->sc);
#ifndef SC_NO_FONT_LOADING
		if (ISTEXTSC(cur_scp) && (cur_scp->font != NULL))
		    sc_load_font(cur_scp, 0, cur_scp->font_height,
				 cur_scp->font,
				 cur_scp->sc->mouse_char, 4);
#endif
		scp->sc->mouse_char = mouse->u.mouse_char;
		crit_exit();
	    }
	    break;

	default:
	    return EINVAL;
	}

	return 0;
    }

    return ENOIOCTL;
}

void
sc_mouse_init(void *unused)
{
    at_exit(sc_mouse_exit1);
}

void
sc_mouse_uninit(void *unused)
{
}

SYSINIT(sc_mouse_init, SI_SUB_DRIVERS, SI_ORDER_ANY, sc_mouse_init, NULL);
SYSUNINIT(sc_mouse_uninit, SI_SUB_DRIVERS, SI_ORDER_ANY, sc_mouse_uninit, NULL);

#endif /* SC_NO_SYSMOUSE */
