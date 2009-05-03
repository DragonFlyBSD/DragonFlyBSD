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
 * curses_util.c
 * $Id: curses_util.c,v 1.7 2005/02/08 07:49:03 cpressey Exp $
 */

#include <ctype.h>
#include <ncurses.h>
#include <panel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curses_util.h"

unsigned int ymax, xmax;
int monochrome = 1;
int allocated_colors = 0;

struct curses_attr {
	int pair_no;
	int bold;
};

struct curses_attr colors_tab[CURSES_COLORS_MAX];

int colors[8] = {
	COLOR_BLACK,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_YELLOW,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_WHITE
};

/*
 * If there is an established color pair with the given fg and bg
 * colors, return it.  Else allocate a new pair with these colors
 * and return that.
 */
static int
curses_colors_find(int fg, int bg)
{
	int pair_no;
	short fge, bge;

	for (pair_no = 0;
	     pair_no <= allocated_colors && pair_no < COLOR_PAIRS;
	     pair_no++) {
		pair_content(pair_no, &fge, &bge);
		if (fg == fge && bg == bge)
			return(pair_no);
	}

	/*
	 * No pair was found, allocate a new one.
	 */
	if (allocated_colors < (COLOR_PAIRS-1)) {
		allocated_colors++;
		init_pair(allocated_colors, fg, bg);
		return(allocated_colors);
	}

	/*
	 * No space to allocate a new one, return error.
	 */
	return(-1);
}

static void
curses_colors_cfg(int role, int fg, int bg, int bold)
{
	int pair_no;

	pair_no = curses_colors_find(fg, bg);
	if (pair_no != -1) {
		colors_tab[role].pair_no = pair_no;
		colors_tab[role].bold = bold;
	} else {
		colors_tab[role].pair_no = 0;
		colors_tab[role].bold = bold;
	}
}

void
curses_colors_init(int force_monochrome)
{
	if (!force_monochrome) {
		if (has_colors()) {
			monochrome = 0;
			start_color();
		}
	}

	/*
	 * By default, make it look kinda like the default libdialog.
	 */
	curses_colors_cfg(CURSES_COLORS_NORMAL,    COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_BACKDROP,  COLOR_WHITE,  COLOR_BLUE,  0);
	curses_colors_cfg(CURSES_COLORS_MENUBAR,   COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_STATUSBAR, COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_BORDER,	   COLOR_WHITE,  COLOR_GREY,  1);
	curses_colors_cfg(CURSES_COLORS_FORMTITLE, COLOR_YELLOW, COLOR_GREY,  1);
	curses_colors_cfg(CURSES_COLORS_LABEL,     COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_CONTROL,   COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_TEXT,      COLOR_BLACK,  COLOR_GREY,  0);
	curses_colors_cfg(CURSES_COLORS_FOCUS,     COLOR_WHITE,  COLOR_BLUE,  1);
	curses_colors_cfg(CURSES_COLORS_SCROLLAREA,COLOR_GREY,   COLOR_BLACK, 0);
	curses_colors_cfg(CURSES_COLORS_SCROLLBAR, COLOR_WHITE,  COLOR_BLUE,  1);
	curses_colors_cfg(CURSES_COLORS_ACCEL,     COLOR_WHITE,  COLOR_GREY,  1);
	curses_colors_cfg(CURSES_COLORS_ACCELFOCUS,COLOR_YELLOW, COLOR_BLUE,  1);
}

void
curses_colors_set(WINDOW *w, int a)
{
	if (!monochrome)
		wattrset(w, COLOR_PAIR(colors_tab[a].pair_no));
	if (colors_tab[a].bold)
		wattron(w, A_BOLD);
	else
		wattroff(w, A_BOLD);
}

void
curses_window_blank(WINDOW *w)
{
	unsigned int i;

	for (i = 0; i <= ymax; i++) {
		wmove(w, i, 0);
		whline(w, ' ', xmax);
	}

	wrefresh(w);
}

void
curses_frame_draw(int x, int y, int width, int height)
{
	int i;

	mvaddch(y, x, ACS_ULCORNER);
	hline(ACS_HLINE, width - 2);
	mvaddch(y, x + width - 1, ACS_URCORNER);

	mvaddch(y + height - 1, x, ACS_LLCORNER);
	hline(ACS_HLINE, width - 2);
	mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);

	move(y + 1, x);
	vline(ACS_VLINE, height - 2);

	move(y + 1, x + width - 1);
	vline(ACS_VLINE, height - 2);

	for (i = y + 1; i < y + height - 1; i++) {
		move(i, x + 1);
		hline(' ', width - 2);
	}
}

void
curses_load_backdrop(WINDOW *w, const char *filename)
{
	FILE *f;
	char line[80];
	int row = 1;
	int my, mx;

	getmaxyx(w, my, mx);
	wclear(w);
	curses_colors_set(w, CURSES_COLORS_BACKDROP);
	curses_window_blank(w);

	if ((f = fopen(filename, "r")) != NULL) {
		while (fgets(line, 79, f) != NULL) {
			if (row > my)
				break;
			if (line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = '\0';
			mvwaddnstr(w, row++, 0, line, mx);
		}
		fclose(f);
	}
}

void
curses_debug_str(const char *s)
{
	char b[256];

	move(1, 0);
	sprintf(b, "[%77s]", s);
	addstr(b);
	refresh();
}

void
curses_debug_int(int i)
{
	char b[256];

	move(1, 0);
	sprintf(b, "[%06d]", i);
	addstr(b);
	refresh();
}

void
curses_debug_key(int i)
{
	char b[256];

	move(1, 0);
	sprintf(b, "[%06d] %s", i, keyname(i));
	addstr(b);
	refresh();
}

void
curses_debug_float(float f)
{
	char b[256];

	move(1, 0);
	sprintf(b, "[%09.3f]", f);
	addstr(b);
	refresh();
}

/*
 * Word wrapping.
 *
 * text:	The text to word-wrap, as one long string.  Spaces will be
 *		compressed, but end-of-line characters will be honoured.
 * line:	A buffer (must be allocated by the caller) to hold a single
 *		line extracted from text.
 * width:	The maximum width of a line.
 * spos:	Pointer to the source position in text.  Should be initially
 *		set to zero, and retained between calls to this function.
 * Returns:	A boolean indicating whether the end of text was reached.
 *		Typically this function should be called repeatedly until
 *		it returns true.
 */
int
extract_wrapped_line(const char *text, char *line, int width, int *spos)
{
	int dpos = 0;
	int saved_spos, saved_dpos;

	for (;;) {
		/*
		 * Skip over whitespace.  If we find a newline or the
		 * end of the text, return a blank line.  Leave *spos
		 * at the position of the 1st non-whitespace character.
		 */
		while (isspace(text[*spos]) && text[*spos] != '\0') {
			if (text[*spos] == '\n') {
				line[dpos] = '\0';
				(*spos)++;
				return(0);
			}
			(*spos)++;
		}

		/*
		 * Save start position and destination position.
		 */
		saved_spos = *spos;
		saved_dpos = dpos;

		/*
		 * Read a word from *spos onward.
		 */
		while (!isspace(text[*spos]) &&
		       text[*spos] != '\0' &&
		       dpos < width) {
			line[dpos++] = text[(*spos)++];
		}

		if (text[*spos] == '\0') {
			/*
			 * End of string - return this word as the last.
			 */
			line[dpos] = '\0';
			return(1);
		} else if (dpos >= width) {
			/*
			 * Last word is too long to fit on this line.
			 */
			if (dpos - saved_dpos >= width) {
				/*
				 * In fact, it's too long to fit on any line!
				 * Truncate it.
				 */
				line[width - 1] = '\0';
				*spos = saved_spos + (dpos - saved_dpos);
				return(0);
			} else {
				/*
				 * Save it for the next pass.
				 */
				*spos = saved_spos;
				line[saved_dpos - 1] = '\0';
				return(0);
			}
		} else {
			line[dpos++] = ' ';
		}
	}
}
