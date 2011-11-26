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
 * curses_util.h
 * $Id: curses_util.h,v 1.5 2005/02/08 07:49:03 cpressey Exp $
 */

#ifndef __CURSES_UTIL_H
#define __CURSES_UTIL_H

#include <ncurses.h>
#include <panel.h>

extern unsigned int ymax, xmax;		/* Size of the user's terminal. */
extern int monochrome;

#define	CURSES_COLORS_NORMAL	0	/* usual text inside forms */
#define	CURSES_COLORS_BACKDROP	1	/* backdrop behind all forms */
#define	CURSES_COLORS_MENUBAR	2	/* line at top of screen */
#define	CURSES_COLORS_STATUSBAR	3	/* line at bottom of screen */
#define	CURSES_COLORS_BORDER	4	/* border of forms */
#define	CURSES_COLORS_FORMTITLE	5	/* title of forms */
#define	CURSES_COLORS_LABEL	6	/* labels of controls */
#define	CURSES_COLORS_CONTROL	7	/* ambient parts of a control */
#define	CURSES_COLORS_TEXT	8	/* text in a control */
#define	CURSES_COLORS_FOCUS	9	/* control, when it has focus */
#define	CURSES_COLORS_SCROLLAREA 10	/* background of scrollbar */
#define	CURSES_COLORS_SCROLLBAR	11	/* foreground of scrollbar */
#define	CURSES_COLORS_ACCEL	12	/* shortcut keys in labels */
#define	CURSES_COLORS_ACCELFOCUS 13	/* shortcut keys, with focus */

#define	CURSES_COLORS_MAX	14

#define	COLOR_GREY		COLOR_WHITE
#define	COLOR_BROWN		COLOR_YELLOW

void			 curses_colors_init(int);
void			 curses_colors_set(WINDOW *, int);

void			 curses_window_blank(WINDOW *);
void			 curses_frame_draw(int, int, int, int);
void			 curses_load_backdrop(WINDOW *, const char *);

void			 curses_debug_str(const char *);
void			 curses_debug_int(int);
void			 curses_debug_key(int);
void			 curses_debug_float(float);

int			 extract_wrapped_line(const char *, char *, int, int *);

#endif /* !__CURSES_UTIL_H */
