/*-
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * Copyright (c) 1992-1998 Søren Schmidt
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
 * $FreeBSD: src/sys/dev/syscons/scterm-sc.c,v 1.4.2.10 2001/06/11 09:05:39 phk Exp $
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/consio.h>
#include <sys/thread2.h>

#include <machine/pc/display.h>

#include "syscons.h"
#include "sctermvar.h"

#ifndef SC_DUMB_TERMINAL

#define MAX_ESC_PAR	5

/* attribute flags */
typedef struct {
	u_short		fg;			/* foreground color */
	u_short		bg;			/* background color */
} color_t;

typedef struct {
	int		flags;
#define SCTERM_BUSY	(1 << 0)
	int		esc;
	int		num_param;
	int		last_param;
	int		param[MAX_ESC_PAR];
	int		saved_xpos;
	int		saved_ypos;
	int		attr_mask;		/* current logical attr mask */
#define NORMAL_ATTR	0x00
#define BLINK_ATTR	0x01
#define BOLD_ATTR	0x02
#define UNDERLINE_ATTR	0x04
#define REVERSE_ATTR	0x08
#define FG_CHANGED	0x10
#define BG_CHANGED	0x20
	int		cur_attr;		/* current hardware attr word */
	color_t		cur_color;		/* current hardware color */
	color_t		std_color;		/* normal hardware color */
	color_t		rev_color;		/* reverse hardware color */
	color_t		dflt_std_color;		/* default normal color */
	color_t		dflt_rev_color;		/* default reverse color */
} term_stat;

static sc_term_init_t	scterm_init;
static sc_term_term_t	scterm_term;
static sc_term_puts_t	scterm_puts;
static sc_term_ioctl_t	scterm_ioctl;
static sc_term_reset_t	scterm_reset;
static sc_term_default_attr_t	scterm_default_attr;
static sc_term_clear_t	scterm_clear;
static sc_term_notify_t	scterm_notify;
static sc_term_input_t	scterm_input;

static sc_term_sw_t sc_term_sc = {
	{ NULL, NULL },
	"sc",					/* emulator name */
	"syscons terminal",			/* description */
	"*",					/* matching renderer, any :-) */
	sizeof(term_stat),			/* softc size */
	0,
	scterm_init,
	scterm_term,
	scterm_puts,
	scterm_ioctl,
	scterm_reset,
	scterm_default_attr,
	scterm_clear,
	scterm_notify,
	scterm_input,
};

SCTERM_MODULE(sc, sc_term_sc);

static term_stat	reserved_term_stat;
static void		scterm_scan_esc(scr_stat *scp, term_stat *tcp,
					u_char c);
static int		mask2attr(term_stat *tcp);

static int
scterm_init(scr_stat *scp, void **softc, int code)
{
	term_stat *tcp;

	if (*softc == NULL) {
		if (reserved_term_stat.flags & SCTERM_BUSY)
			return EINVAL;
		*softc = &reserved_term_stat;
	}
	tcp = *softc;

	switch (code) {
	case SC_TE_COLD_INIT:
		bzero(tcp, sizeof(*tcp));
		tcp->flags = SCTERM_BUSY;
		tcp->esc = 0;
		tcp->saved_xpos = -1;
		tcp->saved_ypos = -1;
		tcp->attr_mask = NORMAL_ATTR;
		/* XXX */
		tcp->dflt_std_color.fg = SC_NORM_ATTR & 0x0f;
		tcp->dflt_std_color.bg = (SC_NORM_ATTR >> 4) & 0x0f;
		tcp->dflt_rev_color.fg = SC_NORM_REV_ATTR & 0x0f;
		tcp->dflt_rev_color.bg = (SC_NORM_REV_ATTR >> 4) & 0x0f;
		tcp->std_color = tcp->dflt_std_color;
		tcp->rev_color = tcp->dflt_rev_color;
		tcp->cur_color = tcp->std_color;
		tcp->cur_attr = mask2attr(tcp);
		++sc_term_sc.te_refcount;
		break;

	case SC_TE_WARM_INIT:
		tcp->esc = 0;
		tcp->saved_xpos = -1;
		tcp->saved_ypos = -1;
#if 0
		tcp->std_color = tcp->dflt_std_color;
		tcp->rev_color = tcp->dflt_rev_color;
#endif
		tcp->cur_color = tcp->std_color;
		tcp->cur_attr = mask2attr(tcp);
		break;
	}

	return 0;
}

static int
scterm_term(scr_stat *scp, void **softc)
{
	if (*softc == &reserved_term_stat) {
		*softc = NULL;
		bzero(&reserved_term_stat, sizeof(reserved_term_stat));
	}
	--sc_term_sc.te_refcount;
	return 0;
}

static void
scterm_scan_esc(scr_stat *scp, term_stat *tcp, u_char c)
{
	static u_char ansi_col[16] = {
		FG_BLACK,     FG_RED,          FG_GREEN,      FG_BROWN,
		FG_BLUE,      FG_MAGENTA,      FG_CYAN,       FG_LIGHTGREY,
		FG_DARKGREY,  FG_LIGHTRED,     FG_LIGHTGREEN, FG_YELLOW,
		FG_LIGHTBLUE, FG_LIGHTMAGENTA, FG_LIGHTCYAN,  FG_WHITE
	};
	sc_softc_t *sc;
	int i, n;

	sc = scp->sc; 
	if (tcp->esc == 1) {	/* seen ESC */
		switch (c) {

		case '7':	/* Save cursor position */
			tcp->saved_xpos = scp->xpos;
			tcp->saved_ypos = scp->ypos;
			break;

		case '8':	/* Restore saved cursor position */
			if (tcp->saved_xpos >= 0 && tcp->saved_ypos >= 0)
				sc_move_cursor(scp, tcp->saved_xpos,
					       tcp->saved_ypos);
			break;

		case '[':	/* Start ESC [ sequence */
			tcp->esc = 2;
			tcp->last_param = -1;
			for (i = tcp->num_param; i < MAX_ESC_PAR; i++)
				tcp->param[i] = 1;
			tcp->num_param = 0;
			return;

		case 'M':	/* Move cursor up 1 line, scroll if at top */
			sc_term_up_scroll(scp, 1, sc->scr_map[0x20],
					  tcp->cur_attr, 0, 0);
			break;
#if 0 /* notyet */
		case 'Q':
			tcp->esc = 4;
			return;
#endif
		case 'c':       /* reset */
			tcp->attr_mask = NORMAL_ATTR;
			tcp->cur_color = tcp->std_color
				       = tcp->dflt_std_color;
			tcp->rev_color = tcp->dflt_rev_color;
			tcp->cur_attr = mask2attr(tcp);
			sc_clear_screen(scp);
			break;

		case '(':	/* iso-2022: designate 94 character set to G0 */
			tcp->esc = 5;
			return;
		}
	} else if (tcp->esc == 2) {	/* seen ESC [ */
		if (c >= '0' && c <= '9') {
			if (tcp->num_param < MAX_ESC_PAR) {
				if (tcp->last_param != tcp->num_param) {
					tcp->last_param = tcp->num_param;
					tcp->param[tcp->num_param] = 0;
				} else {
					tcp->param[tcp->num_param] *= 10;
				}
				tcp->param[tcp->num_param] += c - '0';
				return;
			}
		}
		tcp->num_param = tcp->last_param + 1;
		switch (c) {

		case ';':
			if (tcp->num_param < MAX_ESC_PAR)
				return;
			break;

		case '=':
			tcp->esc = 3;
			tcp->last_param = -1;
			for (i = tcp->num_param; i < MAX_ESC_PAR; i++)
				tcp->param[i] = 1;
			tcp->num_param = 0;
			return;

		case 'A':	/* up n rows */
			sc_term_up(scp, tcp->param[0], 0);
			break;

		case 'B':	/* down n rows */
			sc_term_down(scp, tcp->param[0], 0);
			break;

		case 'C':	/* right n columns */
			sc_term_right(scp, tcp->param[0]);
			break;

		case 'D':	/* left n columns */
			sc_term_left(scp, tcp->param[0]);
			break;

		case 'E':	/* cursor to start of line n lines down */
			n = tcp->param[0];
			if (n < 1)
				n = 1;
			sc_move_cursor(scp, 0, scp->ypos + n);
			break;

		case 'F':	/* cursor to start of line n lines up */
			n = tcp->param[0];
			if (n < 1)
				n = 1;
			sc_move_cursor(scp, 0, scp->ypos - n);
			break;

		case 'f':	/* Cursor move */
		case 'H':
			if (tcp->num_param == 0)
				sc_move_cursor(scp, 0, 0);
			else if (tcp->num_param == 2)
				sc_move_cursor(scp, tcp->param[1] - 1,
					       tcp->param[0] - 1);
			break;

		case 'J':	/* Clear all or part of display */
			if (tcp->num_param == 0)
				n = 0;
			else
				n = tcp->param[0];
			sc_term_clr_eos(scp, n, sc->scr_map[0x20],
					tcp->cur_attr);
			break;

		case 'K':	/* Clear all or part of line */
			if (tcp->num_param == 0)
				n = 0;
			else
				n = tcp->param[0];
			sc_term_clr_eol(scp, n, sc->scr_map[0x20],
					tcp->cur_attr);
			break;

		case 'L':	/* Insert n lines */
			sc_term_ins_line(scp, scp->ypos, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr, 0);
			break;

		case 'M':	/* Delete n lines */
			sc_term_del_line(scp, scp->ypos, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr, 0);
			break;

		case 'P':	/* Delete n chars */
			sc_term_del_char(scp, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr);
			break;

		case '@':	/* Insert n chars */
			sc_term_ins_char(scp, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr);
			break;

		case 'S':	/* scroll up n lines */
			sc_term_del_line(scp, 0, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr, 0);
			break;

		case 'T':	/* scroll down n lines */
			sc_term_ins_line(scp, 0, tcp->param[0],
					 sc->scr_map[0x20], tcp->cur_attr, 0);
			break;

		case 'X':	/* erase n characters in line */
			n = tcp->param[0];
			if (n < 1)
				n = 1;
			if (n > scp->xsize - scp->xpos)
				n = scp->xsize - scp->xpos;
			sc_vtb_erase(&scp->vtb, scp->cursor_pos, n,
				     sc->scr_map[0x20], tcp->cur_attr);
			mark_for_update(scp, scp->cursor_pos);
			mark_for_update(scp, scp->cursor_pos + n - 1);
			break;

		case 'Z':	/* move n tabs backwards */
			sc_term_backtab(scp, tcp->param[0]);
			break;

		case '`':	/* move cursor to column n */
			sc_term_col(scp, tcp->param[0]);
			break;

		case 'a':	/* move cursor n columns to the right */
			sc_term_right(scp, tcp->param[0]);
			break;

		case 'd':	/* move cursor to row n */
			sc_term_row(scp, tcp->param[0]);
			break;

		case 'e':	/* move cursor n rows down */
			sc_term_down(scp, tcp->param[0], 0);
			break;

		case 'm':	/* change attribute */
			if (tcp->num_param == 0) {
				tcp->attr_mask = NORMAL_ATTR;
				tcp->cur_color = tcp->std_color;
				tcp->cur_attr = mask2attr(tcp);
				break;
			}
			for (i = 0; i < tcp->num_param; i++) {
				switch (n = tcp->param[i]) {
				case 0:	/* back to normal */
					tcp->attr_mask = NORMAL_ATTR;
					tcp->cur_color = tcp->std_color;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 1:	/* bold */
					tcp->attr_mask |= BOLD_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 4:	/* underline */
					tcp->attr_mask |= UNDERLINE_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 5:	/* blink */
					tcp->attr_mask |= BLINK_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 7: /* reverse */
					tcp->attr_mask |= REVERSE_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 22: /* remove bold (or dim) */
					tcp->attr_mask &= ~BOLD_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 24: /* remove underline */
					tcp->attr_mask &= ~UNDERLINE_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 25: /* remove blink */
					tcp->attr_mask &= ~BLINK_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 27: /* remove reverse */
					tcp->attr_mask &= ~REVERSE_ATTR;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 30: case 31: /* set ansi fg color */
				case 32: case 33: case 34:
				case 35: case 36: case 37:
					tcp->attr_mask |= FG_CHANGED;
					tcp->cur_color.fg = ansi_col[n - 30];
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 39: /* restore fg color back to normal */
					tcp->attr_mask &= ~(FG_CHANGED|BOLD_ATTR);
					tcp->cur_color.fg = tcp->std_color.fg;
					tcp->cur_attr = mask2attr(tcp);
					break;
				case 40: case 41: /* set ansi bg color */
				case 42: case 43: case 44:
				case 45: case 46: case 47:
					tcp->attr_mask |= BG_CHANGED;
					tcp->cur_color.bg = ansi_col[n - 40];
					tcp->cur_attr = mask2attr(tcp);
		    			break;
				case 49: /* restore bg color back to normal */
					tcp->attr_mask &= ~BG_CHANGED;
					tcp->cur_color.bg = tcp->std_color.bg;
					tcp->cur_attr = mask2attr(tcp);
					break;
				}
			}
			break;

		case 's':	/* Save cursor position */
			tcp->saved_xpos = scp->xpos;
			tcp->saved_ypos = scp->ypos;
			break;

		case 'u':	/* Restore saved cursor position */
			if (tcp->saved_xpos >= 0 && tcp->saved_ypos >= 0)
				sc_move_cursor(scp, tcp->saved_xpos,
					       tcp->saved_ypos);
			break;

		case 'x':
			if (tcp->num_param == 0)
				n = 0;
			else
				n = tcp->param[0];
			switch (n) {
			case 0: /* reset colors and attributes back to normal */
				tcp->attr_mask = NORMAL_ATTR;
				tcp->cur_color = tcp->std_color
					       = tcp->dflt_std_color;
				tcp->rev_color = tcp->dflt_rev_color;
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 1:	/* set ansi background */
				tcp->attr_mask &= ~BG_CHANGED;
				tcp->cur_color.bg = tcp->std_color.bg
						  = ansi_col[tcp->param[1] & 0x0f];
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 2:	/* set ansi foreground */
				tcp->attr_mask &= ~FG_CHANGED;
				tcp->cur_color.fg = tcp->std_color.fg
						  = ansi_col[tcp->param[1] & 0x0f];
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 3: /* set adapter attribute directly */
				tcp->attr_mask &= ~(FG_CHANGED | BG_CHANGED);
				tcp->cur_color.fg = tcp->std_color.fg
						  = tcp->param[1] & 0x0f;
				tcp->cur_color.bg = tcp->std_color.bg
						  = (tcp->param[1] >> 4) & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 5: /* set ansi reverse background */
				tcp->rev_color.bg = ansi_col[tcp->param[1] & 0x0f];
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 6: /* set ansi reverse foreground */
				tcp->rev_color.fg = ansi_col[tcp->param[1] & 0x0f];
				tcp->cur_attr = mask2attr(tcp);
				break;
			case 7: /* set adapter reverse attribute directly */
				tcp->rev_color.fg = tcp->param[1] & 0x0f;
				tcp->rev_color.bg = (tcp->param[1] >> 4) & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
				break;
			}
			break;

		case 'z':	/* switch to (virtual) console n */
			if (tcp->num_param == 1)
				sc_switch_scr(sc, tcp->param[0]);
			break;
		}
	} else if (tcp->esc == 3) {	/* seen ESC [0-9]+ = */
		if (c >= '0' && c <= '9') {
			if (tcp->num_param < MAX_ESC_PAR) {
				if (tcp->last_param != tcp->num_param) {
					tcp->last_param = tcp->num_param;
					tcp->param[tcp->num_param] = 0;
				} else {
					tcp->param[tcp->num_param] *= 10;
				}
				tcp->param[tcp->num_param] += c - '0';
				return;
			}
		}
		tcp->num_param = tcp->last_param + 1;
		switch (c) {

		case ';':
			if (tcp->num_param < MAX_ESC_PAR)
				return;
			break;

		case 'A':   /* set display border color */
			if (tcp->num_param == 1) {
				scp->border=tcp->param[0] & 0xff;
				if (scp == sc->cur_scp)
					sc_set_border(scp, scp->border);
			}
			break;

		case 'B':   /* set bell pitch and duration */
			if (tcp->num_param == 2) {
				scp->bell_pitch = tcp->param[0];
				scp->bell_duration = 
				    (tcp->param[1] * hz + 99) / 100;
			}
			break;

		case 'C':   /* set cursor type & shape */
			crit_enter();
			if (!ISGRAPHSC(sc->cur_scp))
				sc_remove_cursor_image(sc->cur_scp);
			if (tcp->num_param == 1) {
				if (tcp->param[0] & 0x01)
					sc->flags |= SC_BLINK_CURSOR;
				else
					sc->flags &= ~SC_BLINK_CURSOR;
				if (tcp->param[0] & 0x02) 
					sc->flags |= SC_CHAR_CURSOR;
				else
					sc->flags &= ~SC_CHAR_CURSOR;
			} else if (tcp->num_param == 2) {
				sc->cursor_base = scp->font_height 
						- (tcp->param[1] & 0x1F) - 1;
				sc->cursor_height = (tcp->param[1] & 0x1F) 
						- (tcp->param[0] & 0x1F) + 1;
				if (sc->cursor_base < 0)
					sc->cursor_base = 0;

				if (sc->cursor_height < 1) {
					sc->cursor_height = 1;
				} else if (sc->cursor_height >
					   scp->font_height -
					   scp->cursor_base) {
					sc->cursor_height = scp->font_height -
							    scp->cursor_base;
				}
			}
			/* 
			 * The cursor shape is global property; 
			 * all virtual consoles are affected. 
			 * Update the cursor in the current console...
			 */
			if (!ISGRAPHSC(sc->cur_scp)) {
				sc_set_cursor_image(sc->cur_scp);
				sc_draw_cursor_image(sc->cur_scp);
			}
			crit_exit();
			break;

		case 'F':   /* set adapter foreground */
			if (tcp->num_param == 1) {
				tcp->attr_mask &= ~FG_CHANGED;
				tcp->cur_color.fg = tcp->std_color.fg
						  = tcp->param[0] & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
			}
			break;

		case 'G':   /* set adapter background */
			if (tcp->num_param == 1) {
				tcp->attr_mask &= ~BG_CHANGED;
				tcp->cur_color.bg = tcp->std_color.bg
						  = tcp->param[0] & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
			}
			break;

		case 'H':   /* set adapter reverse foreground */
			if (tcp->num_param == 1) {
				tcp->rev_color.fg = tcp->param[0] & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
			}
			break;

		case 'I':   /* set adapter reverse background */
			if (tcp->num_param == 1) {
				tcp->rev_color.bg = tcp->param[0] & 0x0f;
				tcp->cur_attr = mask2attr(tcp);
			}
			break;
		}
#if 0 /* notyet */
	} else if (tcp->esc == 4) {	/* seen ESC Q */
		/* to be filled */
#endif
	} else if (tcp->esc == 5) {	/* seen ESC ( */
		switch (c) {
		case 'B':   /* iso-2022: desginate ASCII into G0 */
			break;
		/* other items to be filled */
		default:
			break;
		}
	}
	tcp->esc = 0;
}

static void
scterm_puts(scr_stat *scp, u_char *buf, int len)
{
	term_stat *tcp;

	tcp = scp->ts;
outloop:
	scp->sc->write_in_progress++;

	if (tcp->esc) {
		scterm_scan_esc(scp, tcp, *buf);
		buf++;
		len--;
	} else {
		switch (*buf) {
		case 0x1b:
			tcp->esc = 1;
			tcp->num_param = 0;
			buf++;
			len--;
			break;
		default:
			sc_term_gen_print(scp, &buf, &len, tcp->cur_attr);
			break;
		}
	}

	sc_term_gen_scroll(scp, scp->sc->scr_map[0x20], tcp->cur_attr);

	scp->sc->write_in_progress--;
	if (len)
		goto outloop;
}

static int
scterm_ioctl(scr_stat *scp, struct tty *tp, u_long cmd, caddr_t data,
	     int flag)
{
	term_stat *tcp = scp->ts;
	vid_info_t *vi;

	switch (cmd) {
	case GIO_ATTR:      	/* get current attributes */
		/* FIXME: */
		*(int*)data = (tcp->cur_attr >> 8) & 0xff;
		return 0;
	case CONS_GETINFO:  	/* get current (virtual) console info */
		vi = (vid_info_t *)data;
		if (vi->size != sizeof(struct vid_info))
			return EINVAL;
		vi->mv_norm.fore = tcp->std_color.fg;
		vi->mv_norm.back = tcp->std_color.bg;
		vi->mv_rev.fore = tcp->rev_color.fg;
		vi->mv_rev.back = tcp->rev_color.bg;
		/*
		 * The other fields are filled by the upper routine. XXX
		 */
		return ENOIOCTL;
	}
	return ENOIOCTL;
}

static int
scterm_reset(scr_stat *scp, int code)
{
	/* FIXME */
	return 0;
}

static void
scterm_default_attr(scr_stat *scp, int color, int rev_color)
{
	term_stat *tcp = scp->ts;

	tcp->dflt_std_color.fg = color & 0x0f;
	tcp->dflt_std_color.bg = (color >> 4) & 0x0f;
	tcp->dflt_rev_color.fg = rev_color & 0x0f;
	tcp->dflt_rev_color.bg = (rev_color >> 4) & 0x0f;
	tcp->std_color = tcp->dflt_std_color;
	tcp->rev_color = tcp->dflt_rev_color;
	tcp->cur_color = tcp->std_color;
	tcp->cur_attr = mask2attr(tcp);
}

static void
scterm_clear(scr_stat *scp)
{
	term_stat *tcp = scp->ts;

	sc_move_cursor(scp, 0, 0);
	sc_vtb_clear(&scp->vtb, scp->sc->scr_map[0x20], tcp->cur_attr);
	mark_all(scp);
}

static void
scterm_notify(scr_stat *scp, int event)
{
	switch (event) {
	case SC_TE_NOTIFY_VTSWITCH_IN:
		break;
	case SC_TE_NOTIFY_VTSWITCH_OUT:
		break;
	}
}

static int
scterm_input(scr_stat *scp, int c, struct tty *tp)
{
	return FALSE;
}

/*
 * Calculate hardware attributes word using logical attributes mask and
 * hardware colors
 */

/* FIXME */
static int
mask2attr(term_stat *tcp)
{
	int attr, mask = tcp->attr_mask;

	if (mask & REVERSE_ATTR) {
		attr = ((mask & FG_CHANGED) ?
			tcp->cur_color.bg : tcp->rev_color.fg) |
			(((mask & BG_CHANGED) ?
			tcp->cur_color.fg : tcp->rev_color.bg) << 4);
	} else
		attr = tcp->cur_color.fg | (tcp->cur_color.bg << 4);

	/* XXX: underline mapping for Hercules adapter can be better */
	if (mask & (BOLD_ATTR | UNDERLINE_ATTR))
		attr ^= 0x08;
	if (mask & BLINK_ATTR)
		attr ^= 0x80;

	return (attr << 8);
}

#endif /* SC_DUMB_TERMINAL */
