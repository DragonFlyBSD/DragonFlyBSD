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
 * curses_widget.h
 * $Id: curses_widget.h,v 1.5 2005/02/08 07:49:03 cpressey Exp $
 */

#include "curses_form.h"

#ifndef __CURSES_WIDGET_H
#define __CURSES_WIDGET_H

typedef enum {
	CURSES_LABEL,
	CURSES_TEXTBOX,
	CURSES_BUTTON,
	CURSES_PROGRESS,
	CURSES_CHECKBOX,
	CURSES_LISTBOX 
} widget_t;

struct curses_widget {
	struct curses_widget *next;	/* chain of widgets in form */
	struct curses_widget *prev;

	struct curses_form *form;	/* form in which widget lives */

	unsigned int x;		/* x pos of widget in form, 0 = left edge */
	unsigned int y;		/* y pos of widget in form, 0 = top edge */
	unsigned int width;	/* width of widget */
	unsigned int type;	/* CURSES_* type of widget */
	char *text;		/* for labels, textboxes, buttons */
	unsigned int size;	/* for textboxes, allocated size of text */
	unsigned int curpos;	/* for textboxes, cursor position */
	unsigned int offset;	/* for textboxes, first displayed char */
	int editable;		/* for textboxes, text is editable */
	int obscured;		/* for textboxes, text is ****'ed out */
	int amount;		/* for progress bars, sliders, checkboxes */
	int spin;		/* for progress bars */
	char *tooltip;		/* short help text displayed on statusbar */
	char accel;		/* 'accelerator' - shortcut key */
	int flags;		/* flags */

	int user_id;		/* misc integer */
	void *userdata;		/* misc pointer */

				/* callback for when widget is clicked */
				/* for buttons */
	int (*click_cb)(struct curses_widget *);
};

#define CURSES_WIDGET_CENTER	1	/* auto center this widget? */
#define CURSES_WIDGET_WIDEN	2	/* auto widen this widget? */

struct curses_widget	*curses_widget_new(unsigned int, unsigned int,
					   unsigned int, widget_t,
					   const char *,
					   unsigned int, unsigned int);
void			 curses_widget_free(struct curses_widget *);
void			 curses_widget_draw(struct curses_widget *);
void			 curses_widget_draw_tooltip(struct curses_widget *);
int			 curses_widget_can_take_focus(struct curses_widget *);
void			 curses_widget_tooltip_set(struct curses_widget *, const char *);
int			 curses_widget_set_click_cb(struct curses_widget *,
						    int (*)(struct curses_widget *));
int			 curses_widget_click(struct curses_widget *);

int			 curses_textbox_advance_char(struct curses_widget *);
int			 curses_textbox_retreat_char(struct curses_widget *);
int			 curses_textbox_home(struct curses_widget *);
int			 curses_textbox_end(struct curses_widget *);
int			 curses_textbox_insert_char(struct curses_widget *, char);
int			 curses_textbox_backspace_char(struct curses_widget *);
int			 curses_textbox_delete_char(struct curses_widget *);
int			 curses_textbox_set_text(struct curses_widget *, const char *);

int			 curses_checkbox_toggle(struct curses_widget *);

int			 curses_progress_spin(struct curses_widget *);

#endif /* !__CURSES_WIDGET_H */
