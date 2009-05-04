/*
 * Copyright (c)2004 Cat's Eye Technologies.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Cat's Eye Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * curses_bar.c
 * $Id: curses_bar.c,v 1.7 2005/02/08 06:03:21 cpressey Exp $
 */

#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>

#include "libaura/mem.h"

#include "curses_util.h"
#include "curses_bar.h"

struct curses_bar *
curses_bar_new(unsigned int x, unsigned int y,
		unsigned int width, unsigned int height,
		int colors, int flags)
{
	struct curses_bar *b;

	AURA_MALLOC(b, curses_bar);

	if (flags & CURSES_BAR_WIDEN)
		width = xmax;
	if (flags & CURSES_BAR_BOTTOM)
		y = ymax - 1;

	b->x = x;
	b->y = y;
	b->width = width;
	b->height = height;
	b->colors = colors;

	if ((b->win = newwin(height, width, y, x)) == NULL) {
		AURA_FREE(b, curses_bar);
		return(NULL);
	}

	curses_colors_set(b->win, colors);
	curses_window_blank(b->win);

	if ((b->pan = new_panel(b->win)) == NULL) {
		delwin(b->win);
		AURA_FREE(b, curses_bar);
		return(NULL);
	}

	return(b);
}

void
curses_bar_free(struct curses_bar *b)
{
	if (b != NULL) {
		if (b->pan != NULL) {
			del_panel(b->pan);
			if (b->win != NULL) {
				delwin(b->win);
			}
		}
		AURA_FREE(b, curses_bar);
	}

	update_panels();
	doupdate();
}

void
curses_bar_set_text(struct curses_bar *b, const char *text)
{
	int spaces;

	curses_colors_set(b->win, b->colors);
	mvwaddstr(b->win, 0, 0, text);
	spaces = b->width - strlen(text);
	whline(b->win, ' ', spaces);
}
