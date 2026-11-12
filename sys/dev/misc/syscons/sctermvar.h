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
 * $FreeBSD: src/sys/dev/syscons/sctermvar.h,v 1.1.2.2 2001/07/28 12:51:47 yokota Exp $
 * $DragonFly: src/sys/dev/misc/syscons/sctermvar.h,v 1.4 2005/05/27 20:57:40 swildner Exp $
 */

#ifndef _DEV_SYSCONS_SCTERMVAR_H_
#define _DEV_SYSCONS_SCTERMVAR_H_

/*
 * building blocks for terminal emulator modules.
 */

static __inline void	sc_term_ins_line(scr_stat *scp, int y, int n, int ch,
					 int attr, int tail);
static __inline void	sc_term_del_line(scr_stat *scp, int y, int n, int ch,
					 int attr, int tail);
static __inline void	sc_term_ins_char(scr_stat *scp, int n, int ch,
					 int attr);
static __inline void	sc_term_del_char(scr_stat *scp, int n, int ch,
					 int attr);
static __inline void	sc_term_col(scr_stat *scp, int n);
static __inline void	sc_term_row(scr_stat *scp, int n);
static __inline void	sc_term_up(scr_stat *scp, int n, int head);
static __inline void	sc_term_down(scr_stat *scp, int n, int tail);
static __inline void	sc_term_left(scr_stat *scp, int n);
static __inline void	sc_term_right(scr_stat *scp, int n);
static __inline void	sc_term_up_scroll(scr_stat *scp, int n, int ch,
					  int attr, int head, int tail);
static __inline void	sc_term_down_scroll(scr_stat *scp, int n, int ch,
					    int attr, int head, int tail);
static __inline void	sc_term_clr_eos(scr_stat *scp, int n, int ch, int attr);
static __inline void	sc_term_clr_eol(scr_stat *scp, int n, int ch, int attr);
static __inline void	sc_term_tab(scr_stat *scp, int n);
static __inline void	sc_term_backtab(scr_stat *scp, int n);
static __inline void	sc_term_respond(scr_stat *scp, u_char *s);
static __inline void	sc_term_gen_print(scr_stat *scp, u_char **buf, int *len,
					  int attr);
static __inline void	sc_term_gen_scroll(scr_stat *scp, int ch, int attr);

static __inline void
sc_term_ins_line(scr_stat *scp, int y, int n, int ch, int attr, int tail)
{
	if (tail <= 0)
		tail = scp->ysize;
	if (n < 1)
		n = 1;
	if (n > tail - y)
		n = tail - y;
	sc_vtb_ins(&scp->vtb, y*scp->xsize, n*scp->xsize, ch, attr);
	if (scp->uside != NULL)
		sc_utf8_ins(scp, y * scp->xsize, n * scp->xsize);
	mark_for_update(scp, y*scp->xsize);
	mark_for_update(scp, scp->xsize*tail - 1);
}

static __inline void
sc_term_del_line(scr_stat *scp, int y, int n, int ch, int attr, int tail)
{
	if (tail <= 0)
		tail = scp->ysize;
	if (n < 1)
		n = 1;
	if (n > tail - y)
		n = tail - y;
	sc_vtb_delete(&scp->vtb, y*scp->xsize, n*scp->xsize, ch, attr);
	if (scp->uside != NULL)
		sc_utf8_delete(scp, y * scp->xsize, n * scp->xsize);
	mark_for_update(scp, y*scp->xsize);
	mark_for_update(scp, scp->xsize*tail - 1);
}

static __inline void
sc_term_ins_char(scr_stat *scp, int n, int ch, int attr)
{
	int count;

	if (n < 1)
		n = 1;
	if (n > scp->xsize - scp->xpos)
		n = scp->xsize - scp->xpos;
	count = scp->xsize - (scp->xpos + n);
	sc_vtb_move(&scp->vtb, scp->cursor_pos, scp->cursor_pos + n, count);
	sc_vtb_erase(&scp->vtb, scp->cursor_pos, n, ch, attr);
	if (scp->uside != NULL) {
		sc_utf8_move(scp, scp->cursor_pos, scp->cursor_pos + n, count);
		sc_utf8_clear(scp, scp->cursor_pos, n);
	}
	mark_for_update(scp, scp->cursor_pos);
	mark_for_update(scp, scp->cursor_pos + n + count - 1);
}

static __inline void
sc_term_del_char(scr_stat *scp, int n, int ch, int attr)
{
	int count;

	if (n < 1)
		n = 1;
	if (n > scp->xsize - scp->xpos)
		n = scp->xsize - scp->xpos;
	count = scp->xsize - (scp->xpos + n);
	sc_vtb_move(&scp->vtb, scp->cursor_pos + n, scp->cursor_pos, count);
	sc_vtb_erase(&scp->vtb, scp->cursor_pos + count, n, ch, attr);
	if (scp->uside != NULL) {
		sc_utf8_move(scp, scp->cursor_pos + n, scp->cursor_pos, count);
		sc_utf8_clear(scp, scp->cursor_pos + count, n);
	}
	mark_for_update(scp, scp->cursor_pos);
	mark_for_update(scp, scp->cursor_pos + n + count - 1);
}

static __inline void
sc_term_col(scr_stat *scp, int n)
{
	if (n < 1)
		n = 1;
	sc_move_cursor(scp, n - 1, scp->ypos);
}

static __inline void
sc_term_row(scr_stat *scp, int n)
{
	if (n < 1)
		n = 1;
	sc_move_cursor(scp, scp->xpos, n - 1);
}

static __inline void
sc_term_up(scr_stat *scp, int n, int head)
{
	if (n < 1)
		n = 1;
	n = imin(n, scp->ypos - head);
	if (n <= 0)
		return;
	sc_move_cursor(scp, scp->xpos, scp->ypos - n);
}

static __inline void
sc_term_down(scr_stat *scp, int n, int tail)
{
	if (tail <= 0)
		tail = scp->ysize;
	if (n < 1)
		n = 1;
	n = imin(n, tail - scp->ypos - 1);
	if (n <= 0)
		return;
	sc_move_cursor(scp, scp->xpos, scp->ypos + n);
}

static __inline void
sc_term_left(scr_stat *scp, int n)
{
	if (n < 1)
		n = 1;
	sc_move_cursor(scp, scp->xpos - n, scp->ypos);
}

static __inline void
sc_term_right(scr_stat *scp, int n)
{
	if (n < 1)
		n = 1;
	sc_move_cursor(scp, scp->xpos + n, scp->ypos);
}

static __inline void
sc_term_up_scroll(scr_stat *scp, int n, int ch, int attr, int head, int tail)
{
	if (tail <= 0)
		tail = scp->ysize;
	if (n < 1)
		n = 1;
	if (n <= scp->ypos - head) {
		sc_move_cursor(scp, scp->xpos, scp->ypos - n);
	} else {
		sc_term_ins_line(scp, head, n - (scp->ypos - head), 
				 ch, attr, tail);
		sc_move_cursor(scp, scp->xpos, head);
	}
}

static __inline void
sc_term_down_scroll(scr_stat *scp, int n, int ch, int attr, int head, int tail)
{
	if (tail <= 0)
		tail = scp->ysize;
	if (n < 1)
		n = 1;
	if (n < tail - scp->ypos) {
		sc_move_cursor(scp, scp->xpos, scp->ypos + n);
	} else {
		sc_term_del_line(scp, head, n - (tail - scp->ypos) + 1, 
				 ch, attr, tail);
		sc_move_cursor(scp, scp->xpos, tail - 1);
	}
}

static __inline void
sc_term_clr_eos(scr_stat *scp, int n, int ch, int attr)
{
	int count;

	switch (n) {
	case 0: /* clear form cursor to end of display */
		count = scp->xsize * scp->ysize - scp->cursor_pos;
		sc_vtb_erase(&scp->vtb, scp->cursor_pos, count, ch, attr);
		sc_utf8_clear(scp, scp->cursor_pos, count);
		mark_for_update(scp, scp->cursor_pos);
		mark_for_update(scp, scp->xsize * scp->ysize - 1);
		sc_remove_cutmarking(scp);
		break;
	case 1: /* clear from beginning of display to cursor */
		count = scp->cursor_pos + 1;
		sc_vtb_erase(&scp->vtb, 0, count, ch, attr);
		sc_utf8_clear(scp, 0, count);
		mark_for_update(scp, 0);
		mark_for_update(scp, scp->cursor_pos);
		sc_remove_cutmarking(scp);
		break;
	case 2: /* clear entire display */
		count = scp->xsize * scp->ysize;
		sc_vtb_erase(&scp->vtb, 0, count, ch, attr);
		sc_utf8_clear(scp, 0, count);
		mark_for_update(scp, 0);
		mark_for_update(scp, scp->xsize * scp->ysize - 1);
		sc_remove_cutmarking(scp);
		break;
	}
}


static __inline void
sc_term_clr_eol(scr_stat *scp, int n, int ch, int attr)
{
	int count;
	int at;

	switch (n) {
	case 0: /* clear form cursor to end of line */
		count = scp->xsize - scp->xpos;
		sc_vtb_erase(&scp->vtb, scp->cursor_pos, count, ch, attr);
		sc_utf8_clear(scp, scp->cursor_pos, count);
		mark_for_update(scp, scp->cursor_pos);
		mark_for_update(scp, scp->cursor_pos +
		    scp->xsize - 1 - scp->xpos);
		break;
	case 1: /* clear from beginning of line to cursor */
		at = scp->cursor_pos - scp->xpos;
		count = scp->xpos + 1;
		sc_vtb_erase(&scp->vtb, at, count, ch, attr);
		sc_utf8_clear(scp, at, count);
		mark_for_update(scp, scp->ypos * scp->xsize);
		mark_for_update(scp, scp->cursor_pos);
		break;
	case 2: /* clear entire line */
		at = scp->cursor_pos - scp->xpos;
		count = scp->xsize;
		sc_vtb_erase(&scp->vtb, at, count, ch, attr);
		sc_utf8_clear(scp, at, count);
		mark_for_update(scp, scp->ypos * scp->xsize);
		mark_for_update(scp, (scp->ypos + 1) * scp->xsize - 1);
		break;
	}
}


static __inline void
sc_term_tab(scr_stat *scp, int n)
{
	int ypos;
	int i;

	if (n < 1)
		n = 1;
	i = (scp->xpos & ~7) + 8*n;
	if (i >= scp->xsize) {
		ypos = scp->ypos;
		if (ypos >= scp->ysize - 1) {
			scp->xpos = 0;
			scp->ypos = ypos + 1;
			scp->cursor_pos = scp->ypos*scp->xsize;
		} else {
			sc_move_cursor(scp, 0, ypos + 1);
		}
	} else {
		sc_move_cursor(scp, i, scp->ypos);
	}
}

static __inline void
sc_term_backtab(scr_stat *scp, int n)
{
	int i;

	if (n < 1)
		n = 1;
	if ((i = scp->xpos & ~7) == scp->xpos)
		i -= 8*n;
	else
		i -= 8*(n - 1);
	if (i < 0)
		i = 0;
	sc_move_cursor(scp, i, scp->ypos);
}

static __inline void
sc_term_respond(scr_stat *scp, u_char *s)
{
	sc_paste(scp, s, strlen(s));	/* XXX: not correct, don't use rmap */
}

static __inline void
sc_term_gen_print(scr_stat *scp, u_char **buf, int *len, int attr)
{
	uint16_t *p;
	u_char *ptr;
	u_char *map;
 	int cnt;
	int l;
	int i;

	ptr = *buf;
	l = *len;

	if (PRINTABLE(*ptr)) {
		p = scp->vtb.vtb_buffer + scp->cursor_pos;
		map = scp->sc->scr_map;

		cnt = imin(l, scp->xsize - scp->xpos);
		i = cnt;
		do {
			p = sc_vtb_putchar(&scp->vtb, p, map[*ptr], attr);
			++ptr;
			--i;
		} while ((i > 0) && PRINTABLE(*ptr));

		l -= cnt - i;
		mark_for_update(scp, scp->cursor_pos);
		{
			int written = cnt - i;
			int at = scp->cursor_pos;

			/*
			 * gen_print owns vtb; drop any stale uside glyphs in the
			 * written span so KMS cannot paint leftover history cells.
			 */
			if (scp->uside != NULL && written > 0)
				sc_utf8_clear(scp, at, written);

			scp->xpos += written;
			if (scp->xpos >= scp->xsize) {
				scp->xpos = 0;
				scp->ypos++;
				/* we may have to scroll the screen */
			}
			scp->cursor_pos = scp->ypos * scp->xsize + scp->xpos;
			if (written > 0)
				mark_for_update(scp, at + written - 1);
		}
	} else {
		switch(*ptr) {
		case 0x07:
			sc_bell(scp, scp->bell_pitch, scp->bell_duration);
			break;

		case 0x08:	/* non-destructive backspace */
			/*
			 * xpos/ypos are authoritative. Relative cursor_pos-- can leave
			 * the write head stuck when cursor_pos and xpos desync; libedit
			 * history refresh relies on \b / CR to reseek within the line.
			 */
			if (scp->xpos > 0) {
				scp->xpos--;
			} else if (scp->ypos > 0) {
				scp->ypos--;
				scp->xpos = scp->xsize - 1;
			}
			scp->cursor_pos = scp->ypos * scp->xsize + scp->xpos;
			break;

		case 0x09:	/* non-destructive tab */
			sc_term_tab(scp, 1);
			/* we may have to scroll the screen */
#if 0
			mark_for_update(scp, scp->cursor_pos);
			scp->cursor_pos += (8 - scp->xpos % 8u);
			mark_for_update(scp, scp->cursor_pos);
			scp->xpos += (8 - scp->xpos % 8u);
			if (scp->xpos >= scp->xsize) {
				scp->xpos = 0;
				scp->ypos++;
			}
#endif
			break;

		case 0x0a:	/* newline, same pos */
			scp->ypos++;
			scp->cursor_pos = scp->ypos * scp->xsize + scp->xpos;
			/* we may have to scroll the screen */
			break;

		case 0x0c:	/* form feed, clears screen */
			sc_clear_screen(scp);
			break;

		case 0x0d:	/* return, return to pos 0 */
			/*
			 * Absolute CR.  Never cursor_pos -= xpos: if the two fields
			 * desync, relative CR leaves the write head at the end of the
			 * line and shell history appears to append instead of replace.
			 */
			scp->xpos = 0;
			scp->cursor_pos = scp->ypos * scp->xsize;
			break;
		}
		ptr++; l--;
	}

	*buf = ptr;
	*len = l;
}

/*
 * Handle scrolling, take care to ensure that we don't implode the
 * fields if we happen to be multi-entrant during a panic.
 */
static __inline void
sc_term_gen_scroll(scr_stat *scp, int ch, int attr)
{
	int pos;
	int ypos;

	pos = scp->cursor_pos;
	cpu_ccfence();
	/* do we have to scroll ?? */
	if (pos >= scp->ysize*scp->xsize) {
		sc_remove_cutmarking(scp);		/* XXX */
#ifndef SC_NO_HISTORY
		if (scp->history != NULL)
			sc_hist_save_one_line(scp, 0);	/* XXX */
#endif
		sc_vtb_delete(&scp->vtb, 0, scp->xsize, ch, attr);
		if (scp->uside != NULL)
			sc_utf8_delete(scp, 0, scp->xsize);
		scp->cursor_pos = pos - scp->xsize;
		ypos = scp->ypos - 1;
		if (ypos <= 0)
			ypos = 0;
		scp->ypos = ypos;
		mark_all(scp);
	}
}

#endif /* _DEV_SYSCONS_SCTERMVAR_H_ */
