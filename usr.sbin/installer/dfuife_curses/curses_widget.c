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
 * curses_widget.c
 * $Id: curses_widget.c,v 1.12 2005/02/08 07:49:03 cpressey Exp $
 */

#include <ctype.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "libaura/mem.h"

#include "curses_form.h"
#include "curses_widget.h"
#include "curses_bar.h"
#include "curses_util.h"

#ifdef DEBUG
extern FILE *dfui_debug_file;
#endif

extern struct curses_bar *statusbar;

/*** WIDGETS ***/

/*
 * Create a new curses_widget, outside of the context of any particular
 * curses_form.
 */
struct curses_widget *
curses_widget_new(unsigned int x, unsigned int y,
		  unsigned int width, widget_t type,
		  const char *text, unsigned int size,
		  unsigned int flags)
{
	struct curses_widget *w;

	AURA_MALLOC(w, curses_widget);

	w->form = NULL;

	if (flags & CURSES_WIDGET_WIDEN) {
		switch (type) {
		case CURSES_TEXTBOX:
			width = strlen(text) + 2;
			break;
		case CURSES_BUTTON:
			width = strlen(text) + 4;
			break;
		default:
			width = strlen(text);
			break;
		}
	}

	w->x = x;
	w->y = y;
	w->width = width;
	w->type = type;
	w->next = NULL;
	w->prev = NULL;
	w->flags = flags;
	w->accel = '\0';

	if (w->type == CURSES_TEXTBOX) {
		w->size = size;
		w->text = aura_malloc(size, "w->text");
		strcpy(w->text, text);
	} else {
		w->text = aura_strdup(text);
		w->size = strlen(text) + 1;
		/* size argument is ignored */
	}

	w->curpos = strlen(w->text);
	w->editable = 1;
	w->obscured = 0;
	w->offset = 0;
	w->amount = 0;
	w->spin = 0;
	w->tooltip = NULL;
	w->user_id = 0;
	w->userdata = NULL;

	w->click_cb = NULL;

	return(w);
}

/*
 * Free the memory allocated for a curses_widget.  Note that this does
 * NOT free any allocated memory at the widget's userdata pointer.
 */
void
curses_widget_free(struct curses_widget *w)
{
	if (w->tooltip != NULL)
		free(w->tooltip);
	free(w->text);
	AURA_FREE(w, curses_widget);
}

void
curses_widget_tooltip_set(struct curses_widget *w, const char *tooltip)
{
	if (w->tooltip != NULL)
		free(w->tooltip);
	w->tooltip = aura_strdup(tooltip);
}

/*
 * Draw a curses_widget to the window of its associated curses_form.
 * Note that this does not refresh the screen.
 */
void
curses_widget_draw(struct curses_widget *w)
{
	unsigned int wx, wy, x, len, i, charpos, cutoff, fchpos;
	char p[5];
	char bar_char;
	struct curses_form *cf = w->form;

	wx = w->x + 1 - cf->x_offset;
	wy = w->y + 1 - cf->y_offset;

	/*
	 * If the widget is outside the clipping rectangle of the
	 * form, don't draw it.
	 */
	if (!curses_form_widget_is_visible(w))
		return;

	wmove(cf->win, wy, wx);

	curses_colors_set(cf->win, w == cf->widget_focus ?
	    CURSES_COLORS_FOCUS : CURSES_COLORS_CONTROL);

	switch (w->type) {
	case CURSES_LABEL:
		curses_colors_set(cf->win, CURSES_COLORS_LABEL);	 /* XXX conditional on... */
		waddnstr(cf->win, w->text, w->width);
		curs_set(0);
		break;
	case CURSES_TEXTBOX:
		waddstr(cf->win, "[");
		curses_colors_set(cf->win, CURSES_COLORS_TEXT);		/* XXX focus ? */
		charpos = w->offset;
		len = strlen(w->text);
		for (x = 0; x < w->width - 2; x++) {
			if (charpos < len) {
				waddch(cf->win, w->obscured ?
				    '*' : w->text[charpos]);
				charpos++;
			} else {
				waddch(cf->win, ' ');
			}
		}
		curses_colors_set(cf->win, w == cf->widget_focus ?
		    CURSES_COLORS_FOCUS : CURSES_COLORS_CONTROL);
		waddstr(cf->win, "]");
		/*
		 * Position the cursor to where it's expected.
		 */
		if (w->curpos - w->offset < w->width - 2) {
			wmove(cf->win, w->y + 1 - cf->y_offset,
			      w->x + 2 - cf->x_offset + w->curpos - w->offset);
		} else {
			wmove(cf->win, w->y + 1 - cf->y_offset,
			      w->x + 2 - cf->x_offset + w->width - 2);
		}
		curs_set(1);
		break;
	case CURSES_BUTTON:
		waddstr(cf->win, "< ");
		waddnstr(cf->win, w->text, w->width - 4);
		waddstr(cf->win, " >");
		if (isprint(w->accel)) {
			for (i = 0; w->text[i] != '\0'; i++) {
				if (toupper(w->text[i]) == w->accel) {
					wmove(cf->win, wy, wx + 2 + i);
					curses_colors_set(cf->win, w == cf->widget_focus ?
					    CURSES_COLORS_ACCELFOCUS : CURSES_COLORS_ACCEL);
					waddch(cf->win, w->text[i]);
					break;
				}
			}
		}
		curs_set(0);
		break;
	case CURSES_PROGRESS:
		waddstr(cf->win, "[");
		snprintf(p, 5, "%3d%%", w->amount);
		cutoff = (w->amount * (w->width - 2)) / 100;
		fchpos = ((w->width - 2) / 2) - 2;
		for (x = 0; x < w->width - 2; x++) {
			if (x < cutoff) {
				curses_colors_set(cf->win, CURSES_COLORS_FOCUS);/* XXX status ? */
				bar_char = monochrome ? '=' : ' ';
			} else {
				curses_colors_set(cf->win, CURSES_COLORS_CONTROL);
				bar_char = ' ';
			}

			if (x >= fchpos && x <= fchpos + 3)
				waddch(cf->win, p[x - fchpos]);
			else
				waddch(cf->win, bar_char);
		}
		waddstr(cf->win, "]");
		curs_set(0);
		break;
	case CURSES_CHECKBOX:
		waddstr(cf->win, "[");
		waddstr(cf->win, w->amount ? "X" : " ");
		waddstr(cf->win, "]");
		curs_set(0);
		break;
	}
}

void
curses_widget_draw_tooltip(struct curses_widget *w)
{
	if (w->tooltip != NULL)
		curses_bar_set_text(statusbar, w->tooltip);
}

/*
 * Returns non-zero if the given widget can take control focus
 * (i.e. if it is not a label, progress bar, or other inert element.)
 */
int
curses_widget_can_take_focus(struct curses_widget *w)
{
	if (w->type == CURSES_LABEL || w->type == CURSES_PROGRESS)
		return(0);
	return(1);
}

int
curses_widget_set_click_cb(struct curses_widget *w,
			   int (*callback)(struct curses_widget *))
{
	w->click_cb = callback;
	return(1);
}

/*
 * Returns:
 *   -1 to indicate that the widget is not clickable.
 *    0 to indicate that the widget clicked.
 *    1 to indicate that the widget clicked and that its form should close.
 */
int
curses_widget_click(struct curses_widget *w)
{
	if ((w->type != CURSES_BUTTON && w->type != CURSES_TEXTBOX) ||
	    w->click_cb == NULL)
		return(-1);

	return(w->click_cb(w));
}

/*** TEXTBOX WIDGETS ***/

int
curses_textbox_advance_char(struct curses_widget *w)
{
	if (w->text[w->curpos] != '\0') {
		w->curpos++;
		if ((w->curpos - w->offset) >= (w->width - 2))
			w->offset++;
		curses_widget_draw(w);
		curses_form_refresh(w->form);
		return(1);
	} else {
		return(0);
	}
}

int
curses_textbox_retreat_char(struct curses_widget *w)
{
	if (w->curpos > 0) {
		w->curpos--;
		if (w->curpos < w->offset)
			w->offset--;
		curses_widget_draw(w);
		curses_form_refresh(w->form);
		return(1);
	} else {
		return(0);
	}
}

int
curses_textbox_home(struct curses_widget *w)
{
	w->curpos = 0;
	w->offset = 0;
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

int
curses_textbox_end(struct curses_widget *w)
{
	w->curpos = strlen(w->text);
	while ((w->curpos - w->offset) >= (w->width - 2)) {
		w->offset++;
	}
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

int
curses_textbox_insert_char(struct curses_widget *w, char key)
{
	unsigned int len, i;

	if (!w->editable)
		return(0);

	len = strlen(w->text);
	if (len == (w->size - 1))
		return(0);

	w->text[len + 1] = '\0';
	for (i = len; i > w->curpos; i--)
		w->text[i] = w->text[i - 1];
	w->text[w->curpos++] = key;
	if ((w->curpos - w->offset) >= (w->width - 2))
		w->offset++;
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

int
curses_textbox_backspace_char(struct curses_widget *w)
{
	int len, i;

	if (!w->editable)
		return(0);

	len = strlen(w->text);
	if (w->curpos == 0)
		return(0);

	for (i = w->curpos; i <= len; i++)
		w->text[i - 1] = w->text[i];
	w->curpos--;
	if (w->curpos < w->offset)
		w->offset--;
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

int
curses_textbox_delete_char(struct curses_widget *w)
{
	unsigned int len, i;

	if (!w->editable)
		return(0);

	len = strlen(w->text);
	if (w->curpos == len)
		return(0);

	for (i = w->curpos + 1; i <= len; i++)
		w->text[i - 1] = w->text[i];
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

int
curses_textbox_set_text(struct curses_widget *w, const char *text)
{
	strlcpy(w->text, text, w->size);
	w->curpos = strlen(w->text);
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

/*** CHECKBOX WIDGETS ***/

int
curses_checkbox_toggle(struct curses_widget *w)
{
	if (!w->editable)
		return(0);
	w->amount = !w->amount;
	curses_widget_draw(w);
	curses_form_refresh(w->form);
	return(1);
}

/*** PROGRESS WIDGETS ***/

char spinny[5] = "/-\\|";

int
curses_progress_spin(struct curses_widget *w)
{
	struct curses_form *cf = w->form;
	int wx, wy;

	if (w->type != CURSES_PROGRESS)
		return(0);

	wx = w->x + 1 - cf->x_offset;
	wy = w->y + 1 - cf->y_offset;

	w->spin = (w->spin + 1) % 4;
	curses_colors_set(cf->win, CURSES_COLORS_FOCUS);/* XXX status ? */
	mvwaddch(cf->win, wy, wx + 1, spinny[w->spin]);

	return(1);
}
