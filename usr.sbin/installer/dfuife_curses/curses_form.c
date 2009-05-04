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
 * curses_form.c
 * $Id: curses_form.c,v 1.13 2005/02/08 22:56:06 cpressey Exp $
 */

#include <ctype.h>
#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"

#include "libdfui/dump.h"

#include "curses_form.h"
#include "curses_widget.h"
#include "curses_util.h"

/*** FORMS ***/

/*
 * Create a new curses form with the given title text.
 */
struct curses_form *
curses_form_new(const char *title)
{
	struct curses_form *cf;

	AURA_MALLOC(cf, curses_form);

	cf->win = NULL;
	cf->pan = NULL;

	cf->widget_head = NULL;
	cf->widget_tail = NULL;
	cf->widget_focus = NULL;
	cf->height = 0;
	cf->width = strlen(title) + 4;
	cf->x_offset = 0;
	cf->y_offset = 0;
	cf->int_width = 0;
	cf->int_height = 0;
	cf->want_x = 0;
	cf->want_y = 0;

	cf->title = aura_strdup(title);

	cf->userdata = NULL;
	cf->cleanup = 0;

	cf->help_text = NULL;

	return(cf);
}

/*
 * Deallocate the memory used for the given curses form and all of the
 * widgets it contains.  Note that this only frees any data at the form's
 * userdata pointer IFF cleanup is non-zero.  Also, it does not cause the
 * screen to be refreshed - call curses_form_refresh(NULL) afterwards to
 * make the form disappear.
 */
void
curses_form_free(struct curses_form *cf)
{
	struct curses_widget *w, *t;

	w = cf->widget_head;
	while (w != NULL) {
		t = w->next;
		curses_widget_free(w);
		w = t;
	}

	if (cf->help_text != NULL) {
		free(cf->help_text);
	}

	if (cf->cleanup && cf->userdata != NULL) {
		free(cf->userdata);
	}

	if (cf->win != NULL) {
		del_panel(cf->pan);
		delwin(cf->win);
	}

	free(cf->title);
	AURA_FREE(cf, curses_form);
}

/*
 * Prepare the widget for being placed in the form.  This implements
 * automatically positioning the widget and automatically resizing
 * the form if the widget is too large to fit.
 */
static void
curses_form_widget_prepare(struct curses_form *cf, struct curses_widget *w)
{
	/*
	 * Link the widget to the form.
	 */
	w->form = cf;

	/*
	 * Auto-position the widget to the center of the form,
	 * if requested.
	 */
	if (w->flags & CURSES_WIDGET_CENTER)
		w->x = (cf->width - w->width) / 2;

	/*
	 * If the widget's right edge exceeds the width of
	 * the form, expand the form.
	 */
	dfui_debug("w->x=%d w->width=%d cf->width=%d : ",
	    w->x, w->width, cf->width);
	if ((w->x + w->width + 1) > cf->width)
		cf->width = w->x + w->width + 1;
	dfui_debug("new cf->width=%d\n", cf->width);
}

/*
 * Create a new curses_widget and add it to an existing curses_form.
 * If the width of the widget is larger than will fit on the form, the
 * form will be expanded, unless it would be expanded larger than the
 * screen, in which case the widget is shrunk.
 */
struct curses_widget *
curses_form_widget_add(struct curses_form *cf,
			unsigned int x, unsigned int y,
			unsigned int width, widget_t type,
			const char *text, unsigned int size,
			unsigned int flags)
{
	struct curses_widget *w;

	w = curses_widget_new(x, y, width, type, text, size, flags);
	curses_form_widget_prepare(cf, w);

	if (cf->widget_head == NULL) {
		cf->widget_head = w;
	} else {
		cf->widget_tail->next = w;
		w->prev = cf->widget_tail;
	}
	cf->widget_tail = w;

	return(w);
}

/*
 * Create a new curses_widget and add it after an existing curses_widget
 * in an existing curses_form.
 */
struct curses_widget *
curses_form_widget_insert_after(struct curses_widget *cw,
			unsigned int x, unsigned int y,
			unsigned int width, widget_t type,
			const char *text, unsigned int size,
			unsigned int flags)
{
	struct curses_widget *w;

	w = curses_widget_new(x, y, width, type, text, size, flags);
	curses_form_widget_prepare(cw->form, w);

	w->prev = cw;
	w->next = cw->next;
	if (cw->next == NULL)
		cw->form->widget_tail = w;
	else
		cw->next->prev = w;
	cw->next = w;

	return(w);
}

/*
 * Unlink a widget from a form.  Does not deallocate the widget.
 */
void
curses_form_widget_remove(struct curses_widget *w)
{
	if (w->prev == NULL)
		w->form->widget_head = w->next;
	else
		w->prev->next = w->next;

	if (w->next == NULL)
		w->form->widget_tail = w->prev;
	else
		w->next->prev = w->prev;

	w->next = NULL;
	w->prev = NULL;
	w->form = NULL;
}

int
curses_form_descriptive_labels_add(struct curses_form *cf, const char *text,
				   unsigned int x, unsigned int y,
				   unsigned int width)
{
	struct curses_widget *w;
	int done = 0;
	int pos = 0;
	char *line;

	line = aura_malloc(width + 1, "descriptive line");
	while (!done) {
		done = extract_wrapped_line(text, line, width, &pos);
		dfui_debug("line = `%s', done = %d, width = %d, form width = %d : ",
		   line, done, width, cf->width);
		w = curses_form_widget_add(cf, x, y++, 0,
		    CURSES_LABEL, line, 0, CURSES_WIDGET_WIDEN);
		dfui_debug("now %d\n", cf->width);
	}
	free(line);
	return(y);
}

void
curses_form_finalize(struct curses_form *cf)
{
	if (cf->widget_head != NULL) {
		cf->widget_focus = cf->widget_head;
		curses_form_focus_skip_forward(cf);
		cf->want_x = cf->widget_focus->x + cf->widget_focus->width / 2;
		cf->want_y = cf->widget_focus->y;
	} else {
		cf->widget_focus = NULL;
	}

	cf->left = (xmax - cf->width) / 2;
	cf->top  = (ymax - cf->height) / 2;

	/*
	 * Set the internal width and height.
	 */

	cf->int_width = cf->width;
	cf->int_height = cf->height;

	/*
	 * Limit form size to physical screen dimensions.
	 */
	if (cf->width > (xmax - 2)) {
		cf->width = xmax - 2;
		cf->left = 1;
	}
	if (cf->height > (ymax - 2)) {
		cf->height = ymax - 2;
		cf->top = 1;
	}
	if (cf->top < 1)
		cf->top = 1;
	if (cf->left < 1)
		cf->left = 1;

	cf->win = newwin(cf->height + 2, cf->width + 2, cf->top - 1, cf->left - 1);
	if (cf->win == NULL)
		fprintf(stderr, "Could not allocate %dx%d window @ %d,%d\n",
		    cf->width + 2, cf->height + 2, cf->left - 1, cf->top - 1);

	cf->pan = new_panel(cf->win);
}

/*
 * Render the given form (and all of the widgets it contains) in the
 * curses backing store.  Does not cause the form to be displayed.
 */
void
curses_form_draw(struct curses_form *cf)
{
	struct curses_widget *w;
	float sb_factor = 0.0;
	size_t sb_off = 0, sb_size = 0;

	curses_colors_set(cf->win, CURSES_COLORS_NORMAL);
	curses_window_blank(cf->win);

	curses_colors_set(cf->win, CURSES_COLORS_BORDER);
	/* draw_frame(cf->left - 1, cf->top - 1, cf->width + 2, cf->height + 2); */
	wborder(cf->win, 0, 0, 0, 0, 0, 0, 0, 0);

	/*
	 * If the internal dimensions of the form exceed the physical
	 * dimensions, draw scrollbar(s) as appropriate.
	 */
	if (cf->int_height > cf->height) {
		sb_factor = (float)cf->height / (float)cf->int_height;
		sb_size = cf->height * sb_factor;
		if (sb_size == 0) sb_size = 1;
		sb_off = cf->y_offset * sb_factor;
		curses_colors_set(cf->win, CURSES_COLORS_SCROLLAREA);
		mvwvline(cf->win, 1, cf->width + 1, ACS_CKBOARD, cf->height);
		curses_colors_set(cf->win, CURSES_COLORS_SCROLLBAR);
		mvwvline(cf->win, 1 + sb_off, cf->width + 1, ACS_BLOCK, sb_size);
	}

	if (cf->int_width > cf->width) {
		sb_factor = (float)cf->width / (float)cf->int_width;
		sb_size = cf->width * sb_factor;
		if (sb_size == 0) sb_size = 1;
		sb_off = cf->x_offset * sb_factor;
		curses_colors_set(cf->win, CURSES_COLORS_SCROLLAREA);
		mvwhline(cf->win, cf->height + 1, 1, ACS_CKBOARD, cf->width);
		curses_colors_set(cf->win, CURSES_COLORS_SCROLLBAR);
		mvwhline(cf->win, cf->height + 1, 1 + sb_off, ACS_BLOCK, sb_size);
	}

	curses_colors_set(cf->win, CURSES_COLORS_BORDER);

	/*
	 * Render the title bar text.
	 */
	wmove(cf->win, 0, (cf->width - strlen(cf->title)) / 2 - 1);
	waddch(cf->win, ACS_RTEE);
	waddch(cf->win, ' ');
	curses_colors_set(cf->win, CURSES_COLORS_FORMTITLE);
	waddstr(cf->win, cf->title);
	curses_colors_set(cf->win, CURSES_COLORS_BORDER);
	waddch(cf->win, ' ');
	waddch(cf->win, ACS_LTEE);

	/*
	 * Render a "how to get help" reminder.
	 */
	if (cf->help_text != NULL) {
		static const char *help_msg = "Press F1 for Help";

		wmove(cf->win, cf->height + 1,
		      (cf->width - strlen(help_msg)) / 2 - 1);
		waddch(cf->win, ACS_RTEE);
		waddch(cf->win, ' ');
		curses_colors_set(cf->win, CURSES_COLORS_FORMTITLE);
		waddstr(cf->win, help_msg);
		curses_colors_set(cf->win, CURSES_COLORS_BORDER);
		waddch(cf->win, ' ');
		waddch(cf->win, ACS_LTEE);
	}

	/*
	 * Render the widgets.
	 */
	for (w = cf->widget_head; w != NULL; w = w->next) {
		curses_widget_draw(w);
	}

	/* to put the cursor there */
	curses_widget_draw_tooltip(cf->widget_focus);
	curses_widget_draw(cf->widget_focus);
}

/*
 * Cause the given form to be displayed (if it was not displayed previously)
 * or refreshed (if it was previously displayed.)  Passing NULL to this
 * function causes all visible forms to be refreshed.
 *
 * (Implementation note: the argument is actually irrelevant - all visible
 * forms will be refreshed when any form is displayed or refreshed - but
 * client code should not rely on this behaviour.)
 */
void
curses_form_refresh(struct curses_form *cf __unused)
{
	update_panels();
	doupdate();
}

void
curses_form_focus_skip_forward(struct curses_form *cf)
{
	while (!curses_widget_can_take_focus(cf->widget_focus)) {
		cf->widget_focus = cf->widget_focus->next;
		if (cf->widget_focus == NULL)
			cf->widget_focus = cf->widget_head;
	}
	curses_form_widget_ensure_visible(cf->widget_focus);
}

void
curses_form_focus_skip_backward(struct curses_form *cf)
{
	while (!curses_widget_can_take_focus(cf->widget_focus)) {
		cf->widget_focus = cf->widget_focus->prev;
		if (cf->widget_focus == NULL)
			cf->widget_focus = cf->widget_tail;
	}
	curses_form_widget_ensure_visible(cf->widget_focus);
}

void
curses_form_advance(struct curses_form *cf)
{
	struct curses_widget *w;

	w = cf->widget_focus;
	cf->widget_focus = cf->widget_focus->next;
	if (cf->widget_focus == NULL)
		cf->widget_focus = cf->widget_head;
	curses_form_focus_skip_forward(cf);
	cf->want_x = cf->widget_focus->x + cf->widget_focus->width / 2;
	cf->want_y = cf->widget_focus->y;
	curses_widget_draw(w);
	curses_widget_draw_tooltip(cf->widget_focus);
	curses_widget_draw(cf->widget_focus);
	curses_form_refresh(cf);
#ifdef DEBUG
	curses_debug_int(cf->widget_focus->user_id);
#endif
}

void
curses_form_retreat(struct curses_form *cf)
{
	struct curses_widget *w;

	w = cf->widget_focus;
	cf->widget_focus = cf->widget_focus->prev;
	if (cf->widget_focus == NULL)
		cf->widget_focus = cf->widget_tail;
	curses_form_focus_skip_backward(cf);
	cf->want_x = cf->widget_focus->x + cf->widget_focus->width / 2;
	cf->want_y = cf->widget_focus->y;
	curses_widget_draw(w);
	curses_widget_draw_tooltip(cf->widget_focus);
	curses_widget_draw(cf->widget_focus);
	curses_form_refresh(cf);
#ifdef DEBUG
	curses_debug_int(cf->widget_focus->user_id);
#endif
}

/*
 * Returns the widget at (x, y) within a form, or NULL if
 * there is no widget at that location.
 */
struct curses_widget *
curses_form_widget_at(struct curses_form *cf, unsigned int x, unsigned int y)
{
	struct curses_widget *w;

	for (w = cf->widget_head; w != NULL; w = w->next) {
		if (y == w->y && x >= w->x && x <= (w->x + w->width))
			return(w);
	}

	return(NULL);
}

/*
 * Returns the first (focusable) widget on
 * the topmost row of the form.
 */
int
curses_form_widget_first_row(struct curses_form *cf)
{
	struct curses_widget *w;

	for (w = cf->widget_head; w != NULL; w = w->next) {
		if (curses_widget_can_take_focus(w))
			return(w->y);
	}

	return(0);
}

/*
 * Returns the first (focusable) widget on
 * the bottommost row of the form.
 */
int
curses_form_widget_last_row(struct curses_form *cf)
{
	struct curses_widget *w;
	unsigned int best_y = 0;

	for (w = cf->widget_head; w != NULL; w = w->next) {
		if (curses_widget_can_take_focus(w) && w->y > best_y) {
			best_y = w->y;
		}
	}

	return(best_y);
}

/*
 * Returns the first (focusable) widget on row y.
 */
struct curses_widget *
curses_form_widget_first_on_row(struct curses_form *cf, unsigned int y)
{
	struct curses_widget *w;

	for (w = cf->widget_head; w != NULL; w = w->next) {
		if (curses_widget_can_take_focus(w) && y == w->y)
			return(w);
	}

	return(NULL);
}

/*
 * Returns the (focusable) widget on row y closest to x.
 */
struct curses_widget *
curses_form_widget_closest_on_row(struct curses_form *cf,
				  unsigned int x, unsigned int y)
{
	struct curses_widget *w, *best = NULL;
	int dist, best_dist = 999;

	w = curses_form_widget_first_on_row(cf, y);
	if (w == NULL)
		return(NULL);

	for (best = w; w != NULL && w->y == y; w = w->next) {
		if (!curses_widget_can_take_focus(w))
			continue;
		dist = (w->x + w->width / 2) - x;
		if (dist < 0) dist *= -1;
		if (dist < best_dist) {
			best_dist = dist;
			best = w;
		}
	}

	return(best);
}

/*
 * Returns the number of (focusable) widgets with y values less than
 * (above) the given widget.
 */
int
curses_form_widget_count_above(struct curses_form *cf,
				struct curses_widget *w)
{
	struct curses_widget *lw;
	int count = 0;

	for (lw = cf->widget_head; lw != NULL; lw = lw->next) {
		if (curses_widget_can_take_focus(lw) && lw->y < w->y)
			count++;
	}

	return(count);
}

/*
 * Returns the number of (focusable) widgets with y values greater than
 * (below) the given widget.
 */
int
curses_form_widget_count_below(struct curses_form *cf,
				struct curses_widget *w)
{
	struct curses_widget *lw;
	int count = 0;

	for (lw = cf->widget_head; lw != NULL; lw = lw->next) {
		if (curses_widget_can_take_focus(lw) && lw->y > w->y)
			count++;
	}

	return(count);
}

/*
 * Move to the next widget whose y is greater than the
 * current want_y, and whose x is closest to want_x.
 */
void
curses_form_advance_row(struct curses_form *cf)
{
	struct curses_widget *w, *c;
	int wy;

	w = cf->widget_focus;
	if (curses_form_widget_count_below(cf, w) > 0) {
		wy = cf->want_y + 1;
	} else {
		wy = curses_form_widget_first_row(cf);
	}
	do {
		c = curses_form_widget_closest_on_row(cf,
		    cf->want_x, wy++);
	} while (c == NULL);

	cf->widget_focus = c;
	curses_form_focus_skip_forward(cf);
	cf->want_y = cf->widget_focus->y;
	curses_widget_draw(w);
	curses_widget_draw_tooltip(cf->widget_focus);
	curses_widget_draw(cf->widget_focus);
	curses_form_refresh(cf);
}

/*
 * Move to the next widget whose y is less than the
 * current want_y, and whose x is closest to want_x.
 */
void
curses_form_retreat_row(struct curses_form *cf)
{
	struct curses_widget *w, *c;
	int wy;

	w = cf->widget_focus;
	if (curses_form_widget_count_above(cf, w) > 0) {
		wy = cf->want_y - 1;
	} else {
		wy = curses_form_widget_last_row(cf);
	}
	do {
		c = curses_form_widget_closest_on_row(cf,
		    cf->want_x, wy--);
	} while (c == NULL);

	cf->widget_focus = c;
	curses_form_focus_skip_backward(cf);
	cf->want_y = cf->widget_focus->y;
	curses_widget_draw(w);
	curses_widget_draw_tooltip(cf->widget_focus);
	curses_widget_draw(cf->widget_focus);
	curses_form_refresh(cf);
}

void
curses_form_scroll_to(struct curses_form *cf,
		      unsigned int x_off, unsigned int y_off)
{
	cf->x_offset = x_off;
	cf->y_offset = y_off;
}

void
curses_form_scroll_delta(struct curses_form *cf, int dx, int dy)
{
	unsigned int x_off, y_off;

	if (dx < 0 && (unsigned int)-dx > cf->x_offset) {
		x_off = 0;
	} else {
		x_off = cf->x_offset + dx;
	}
	if (x_off > (cf->int_width - cf->width))
		x_off = cf->int_width - cf->width;

	if (dy < 0 && (unsigned int)-dy > cf->y_offset) {
		y_off = 0;
	} else {
		y_off = cf->y_offset + dy;
	}
	if (y_off > (cf->int_height - cf->height))
		y_off = cf->int_height - cf->height;

	curses_form_scroll_to(cf, x_off, y_off);
}

static void
curses_form_refocus_after_scroll(struct curses_form *cf, int dx, int dy)
{
	struct curses_widget *w;

	w = curses_form_widget_closest_on_row(cf,
	    cf->widget_focus->x + dx, cf->widget_focus->y + dy);

	if (w != NULL) {
		cf->widget_focus = w;
		cf->want_x = w->x + w->width / 2;
		cf->want_y = w->y;
	}
}

int
curses_form_widget_is_visible(struct curses_widget *w)
{
	unsigned int wx, wy;

	wx = w->x + 1 - w->form->x_offset;
	wy = w->y + 1 - w->form->y_offset;

	if (wy < 1 || wy > w->form->height)
		return(0);

	return(1);
}

void
curses_form_widget_ensure_visible(struct curses_widget *w)
{
	unsigned int wx, wy;
	int dx = 0, dy = 0;

	/*
	 * If a textbox's offset is such that we can't see
	 * the cursor inside, adjust it.
	 */
	if (w->type == CURSES_TEXTBOX) {
		if (w->curpos - w->offset >= w->width - 2)
			w->offset = w->curpos - (w->width - 3);
		if (w->offset > w->curpos)
			w->offset = w->curpos;
	}

	if (curses_form_widget_is_visible(w))
		return;

	wx = w->x + 1 - w->form->x_offset;
	wy = w->y + 1 - w->form->y_offset;

	if (wy < 1)
		dy = -1 * (1 - wy);
	else if (wy > w->form->height)
		dy = (wy - w->form->height);

	curses_form_scroll_delta(w->form, dx, dy);
	curses_form_draw(w->form);
	curses_form_refresh(w->form);
}

static void
curses_form_show_help(const char *text)
{
	struct curses_form *cf;
	struct curses_widget *w;

	cf = curses_form_new(_("Help"));

	cf->height = curses_form_descriptive_labels_add(cf, text, 1, 1, 72);
	cf->height += 1;
	w = curses_form_widget_add(cf, 0, cf->height++, 0,
	    CURSES_BUTTON, _("OK"), 0, CURSES_WIDGET_WIDEN);
	curses_widget_set_click_cb(w, cb_click_close_form);

	curses_form_finalize(cf);

	curses_form_draw(cf);
	curses_form_refresh(cf);
	curses_form_frob(cf);
	curses_form_free(cf);
}

#define CTRL(c) (char)(c - 'a' + 1)

struct curses_widget *
curses_form_frob(struct curses_form *cf)
{
	int key;

	flushinp();
	for (;;) {
		key = getch();
		switch(key) {
		case KEY_DOWN:
		case CTRL('n'):
			curses_form_advance_row(cf);
			break;
		case KEY_UP:
		case CTRL('p'):
			curses_form_retreat_row(cf);
			break;
		case '\t':
			curses_form_advance(cf);
			break;
		case KEY_RIGHT:
		case CTRL('f'):
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				if (!curses_textbox_advance_char(cf->widget_focus))
					curses_form_advance(cf);
			} else
				curses_form_advance(cf);
			break;
		case KEY_LEFT:
		case CTRL('b'):
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				if (!curses_textbox_retreat_char(cf->widget_focus))
					curses_form_retreat(cf);
			} else
				curses_form_retreat(cf);
			break;
		case '\n':
		case '\r':
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				switch (curses_widget_click(cf->widget_focus)) {
				case -1:
					curses_form_advance(cf);
					break;
				case 0:
					break;
				case 1:
					/* this would be pretty rare */
					return(cf->widget_focus);
				}
			} else if (cf->widget_focus->type == CURSES_BUTTON) {
				switch (curses_widget_click(cf->widget_focus)) {
				case -1:
					beep();
					break;
				case 0:
					break;
				case 1:
					return(cf->widget_focus);
				}
			} else if (cf->widget_focus->type == CURSES_CHECKBOX) {
				curses_checkbox_toggle(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case '\b':
		case KEY_BACKSPACE:
		case 127:		/* why is this not KEY_BACKSPACE on xterm?? */
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				curses_textbox_backspace_char(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case KEY_DC:
		case CTRL('k'):
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				curses_textbox_delete_char(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case KEY_HOME:
		case CTRL('a'):
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				curses_textbox_home(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case KEY_END:
		case CTRL('e'):
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				curses_textbox_end(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case KEY_NPAGE:
		case CTRL('g'):
			curses_form_scroll_delta(cf, 0, cf->height - 1);
			curses_form_refocus_after_scroll(cf, 0, cf->height - 1);
			curses_form_draw(cf);
			curses_form_refresh(cf);
			break;
		case KEY_PPAGE:
		case CTRL('t'):
			curses_form_scroll_delta(cf, 0, -1 * (cf->height - 1));
			curses_form_refocus_after_scroll(cf, 0, -1 * (cf->height - 1));
			curses_form_draw(cf);
			curses_form_refresh(cf);
			break;
		case ' ':
			if (cf->widget_focus->type == CURSES_TEXTBOX) {
				/* XXX if non-editable, click it */
				curses_textbox_insert_char(cf->widget_focus, ' ');
			} else if (cf->widget_focus->type == CURSES_BUTTON) {
				switch (curses_widget_click(cf->widget_focus)) {
				case -1:
					beep();
					break;
				case 0:
					break;
				case 1:
					return(cf->widget_focus);
				}
			} else if (cf->widget_focus->type == CURSES_CHECKBOX) {
				curses_checkbox_toggle(cf->widget_focus);
			} else {
				beep();
			}
			break;
		case KEY_F(1):		/* why does this not work in xterm??? */
		case CTRL('w'):
			if (cf->help_text != NULL) {
				curses_form_show_help(cf->help_text);
				curses_form_refresh(cf);
			}
			break;
		case KEY_F(10):
		case CTRL('l'):
			redrawwin(stdscr);
			curses_form_refresh(NULL);
			break;
		default:
			if (isprint(key) && cf->widget_focus->type == CURSES_TEXTBOX) {
				curses_textbox_insert_char(cf->widget_focus, (char)key);
			} else {
				struct curses_widget *cw;

				for (cw = cf->widget_head; cw != NULL; cw = cw->next) {
					if (toupper(key) == cw->accel) {
						/*
						 * To just refocus:
						 */
						/*
						cf->widget_focus = cw;
						curses_form_widget_ensure_visible(cw);
						curses_form_draw(cf);
						curses_form_refresh(cf);
						*/
						/*
						 * To actually activate:
						 */
						switch (curses_widget_click(cw)) {
						case -1:
							beep();
							break;
						case 0:
							break;
						case 1:
							return(cw);
						}

						break;
					}
				}
#ifdef DEBUG
				curses_debug_key(key);
#endif
				beep();
			}
			break;
		}
	}
}

/*** GENERIC CALLBACKS ***/

/*
 * Callback to give to curses_button_set_click_cb, for buttons
 * that simply close the form they are in when they are clicked.
 * These usually map to dfui actions.
 */
int
cb_click_close_form(struct curses_widget *w __unused)
{
	return(1);
}
