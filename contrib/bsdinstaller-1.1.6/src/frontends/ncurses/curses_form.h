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
 * curses_form.h
 * $Id: curses_form.h,v 1.4 2005/02/08 05:54:44 cpressey Exp $
 */

#ifndef __CURSES_FORM_H
#define __CURSES_FORM_H

#include <ncurses.h>
#include <panel.h>

#include "curses_widget.h"

struct curses_form {
	struct curses_widget *widget_head;	/* chain of widgets in form */
	struct curses_widget *widget_tail;
	struct curses_widget *widget_focus;	/* currently selected widget */

	PANEL *pan;
	WINDOW *win;

	unsigned int left;	/* column of left edge of form, 0-based */
	unsigned int top;	/* row of top edge of form, 0-based */
	unsigned int height;	/* height of form in rows, not incl border */
	unsigned int width;	/* width of form in columns, not incl border */

	unsigned int x_offset;	/* first displayed col (horiz scrolling) */
	unsigned int y_offset;	/* first displayed row (vertical scrolling) */
	unsigned int int_width;	/* internal width (horizontal scrolling) */
	unsigned int int_height; /* internal height (vertical scrolling) */

	unsigned int want_x;	/* For up/down row: remember where we */
	unsigned int want_y;	/* "want" to be, for natural navigation */

	char *title;		/* text displayed in title bar of form */

	void *userdata;		/* misc pointer */
	int cleanup;		/* are we responsible for freeing userdata? */

	char *help_text;	/* if non-NULL, text shown in help window */
};

struct curses_form	*curses_form_new(const char *);
void			 curses_form_free(struct curses_form *);
struct curses_widget	*curses_form_widget_add(struct curses_form *,
						unsigned int, unsigned int,
						unsigned int, widget_t,
						const char *,
						unsigned int, unsigned int);
struct curses_widget	*curses_form_widget_insert_after(struct curses_widget *,
						unsigned int, unsigned int,
						unsigned int, widget_t,
						const char *,
						unsigned int, unsigned int);
void			 curses_form_widget_remove(struct curses_widget *);
struct curses_widget	*curses_form_widget_at(struct curses_form *,
						unsigned int, unsigned int);
int			 curses_form_widget_first_row(struct curses_form *);
int			 curses_form_widget_last_row(struct curses_form *);
struct curses_widget	*curses_form_widget_first_on_row(struct curses_form *,
						unsigned int);
struct curses_widget	*curses_form_widget_closest_on_row(struct curses_form *,
						unsigned int, unsigned int);
int			curses_form_widget_count_above(struct curses_form *,
							struct curses_widget *);
int			 curses_form_widget_count_below(struct curses_form *,
							struct curses_widget *);
int			 curses_form_descriptive_labels_add(struct curses_form *,
				const char *, unsigned int, unsigned int, unsigned int);
void			 curses_form_finalize(struct curses_form *);
void			 curses_form_draw(struct curses_form *);
void			 curses_form_refresh(struct curses_form *);
void			 curses_form_focus_skip_forward(struct curses_form *);
void			 curses_form_focus_skip_backward(struct curses_form *);
void			 curses_form_advance(struct curses_form *);
void			 curses_form_retreat(struct curses_form *);
void			 curses_form_advance_row(struct curses_form *);
void			 curses_form_retreat_row(struct curses_form *);
void			 curses_form_scroll_to(struct curses_form *, unsigned int, unsigned int);
void			 curses_form_scroll_delta(struct curses_form *, int, int);
int			 curses_form_widget_is_visible(struct curses_widget *);
void			 curses_form_widget_ensure_visible(struct curses_widget *);
struct curses_widget	*curses_form_frob(struct curses_form *);

int			 cb_click_close_form(struct curses_widget *);

#endif /* !__CURSES_FORM_H */
