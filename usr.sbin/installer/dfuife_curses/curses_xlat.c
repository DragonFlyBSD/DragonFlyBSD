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
 * curses_xlat.c
 * Translate DFUI forms to curses forms.
 * $Id: curses_xlat.c,v 1.20 2005/02/08 21:39:42 cpressey Exp $
 */

#include <sys/time.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "libaura/mem.h"

#include "libdfui/dfui.h"
#include "libdfui/dump.h"

#include "curses_form.h"
#include "curses_widget.h"
#include "curses_util.h"
#include "curses_xlat.h"

#define MAX(a, b) (a > b ? a : b)
#define MIN(a, b) (a < b ? a : b)

/*** CALLBACKS ***/

static struct timeval last_update;
static unsigned int last_y;

/*
 * Callback to give to curses_widget_set_click_cb, for buttons
 * that remove the same row of widgets that they are on.
 */
static int
cb_click_remove_row(struct curses_widget *w)
{
	struct curses_form *cf = w->form;
	struct curses_widget *few;
	int id = w->user_id;

	/*
	 * Since we're going to be deleting the widget with
	 * the focus, first move the focus onto a widget
	 * that we won't be deleting.
	 */
	do {
		if (cf->widget_focus == NULL)
			cf->widget_focus = cf->widget_head;
		while (cf->widget_focus->user_id == id)
			cf->widget_focus = cf->widget_focus->prev;
	} while (cf->widget_focus == NULL);

	/*
	 * Delete all widgets with the same id as the focused one.
	 */
	for (few = cf->widget_head; few != NULL; few = few->next) {
		if (few->user_id == id) {
			curses_form_widget_remove(few);
			/*
			 * Reset the iterator, as the previous command
			 * may have obliterated the current widget.
			 */
			few = cf->widget_head;
		}
	}

	/*
	 * Slide the remaining widgets up a row.
	 */
	for (few = cf->widget_head; few != NULL; few = few->next) {
		if (few->user_id > id) {
			/*
			 * Slide the rows below the deleted row up one row.
			 */
			few->user_id--;
			few->y--;
		} else if (few->user_id == -1) {
			/*
			 * Slide the buttons, too.
			 */
			few->y--;
		}
	}

	cf->int_height--;

	/*
	 * Now that the widgets are deleted, make sure the focus is
	 * on a usable widget (not a label.)
	 */
	curses_form_focus_skip_forward(cf);
	cf->want_y = cf->widget_focus->y;

	/*
	 * Repaint the form.  XXX Might not be necessary anymore?
	 */
	curses_form_draw(cf);
	curses_form_refresh(cf);
	return(0);
}

/*
 * Callback to give to curses_widget_set_click_cb, for textboxes
 * that pop up a list of options from which the user can select.
 */
static int
cb_click_select_option(struct curses_widget *w)
{
	struct dfui_field *fi = w->userdata;
	struct dfui_option *o;
	struct curses_form *cf;
	struct curses_widget *button, *cw;

	cf = curses_form_new("* select *");

	for (o = dfui_field_option_get_first(fi); o != NULL;
	     o = dfui_option_get_next(o)) {
		button = curses_form_widget_add(cf, 1,
		    cf->height++, 0, CURSES_BUTTON,
		    dfui_option_get_value(o), 0, CURSES_WIDGET_WIDEN);
		curses_widget_set_click_cb(button, cb_click_close_form);
	}

	curses_form_finalize(cf);

	curses_form_draw(cf);
	curses_form_refresh(cf);
	cw = curses_form_frob(cf);

	curses_textbox_set_text(w, cw->text);

	curses_form_free(cf);
	curses_form_refresh(NULL);

	return(0);
}

/*
 * XXX this should maybe be in libdfui.
 */
static struct dfui_dataset *
create_default_dataset(const struct dfui_form *f)
{
	struct dfui_dataset *ds;
	struct dfui_field *fi;

	ds = dfui_dataset_new();
	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		dfui_dataset_celldata_add(ds,
		     dfui_field_get_id(fi), "");
	}

	return(ds);
}

/*
 * Callback to give to curses_widget_set_click_cb, for buttons
 * that insert a row of widgets before the row that they are on.
 */
static int
cb_click_insert_row(struct curses_widget *w)
{
	struct curses_form *cf = w->form;
	struct curses_widget *few, *lw;
	int id = w->user_id;
	int top = w->y;
	struct dfui_dataset *ds;
	struct curses_form_userdata *cfu = cf->userdata;

	/*
	 * Find the last widget in the tab order that is of the prev row.
	 */

	for (lw = w; lw != NULL; lw = lw->prev) {
		if (lw->user_id == id - 1)
			break;
	}

	/*
	 * Slide widgets below the row we're going to insert, down.
	 */
	for (few = cf->widget_head; few != NULL; few = few->next) {
		if (few->user_id >= id) {
			/*
			 * Slide the rows below the deleted row up one row.
			 */
			few->user_id++;
			few->y++;
		} else if (few->user_id == -1) {
			/*
			 * Slide the buttons, too.
			 */
			few->y++;
		}
	}
	cf->int_height++;

	/*
	 * Insert a new row of widgets.
	 */
	ds = create_default_dataset(cfu->f);
	curses_form_create_widget_row(cf, lw, ds, 1, top, id);
	dfui_dataset_free(ds);

	/*
	 * Repaint the form.
	 */
	curses_form_widget_ensure_visible(cf->widget_focus);
	cf->want_y = cf->widget_focus->y;
	curses_form_draw(cf);
	curses_form_refresh(cf);
	return(0);
}

/*
 * Create a row of widgets in a multiple=true form.
 * Returns the x position of the "Ins" button, if any.
 */
int
curses_form_create_widget_row(struct curses_form *cf, struct curses_widget *cw,
	const struct dfui_dataset *ds, int left, int top, int row)
{
	struct curses_widget *xbox, *button;
	struct dfui_field *fi;
	struct dfui_celldata *cd;
	const char *value;
	int col = 0, ins_x = left;
	struct curses_form_userdata *cfu = cf->userdata;
	const struct dfui_form *f = cfu->f;

	/*
	 * Create one input underneath each field heading.
	 */
	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		cd = dfui_dataset_celldata_find(ds, dfui_field_get_id(fi));
		value = dfui_celldata_get_value(cd);
		if (cw == NULL) {
			if (dfui_field_property_is(fi, "control", "checkbox")) {
				xbox = curses_form_widget_add(cf,
				    left, top, 4, CURSES_CHECKBOX, "", 0, 0);
				xbox->amount = (value[0] == 'Y' ? 1 : 0);
			} else {
				xbox = curses_form_widget_add(cf, left, top,
				    cfu->widths[col] - 1, CURSES_TEXTBOX,
				    value, 256, 0);
			}
		} else {
			if (dfui_field_property_is(fi, "control", "checkbox")) {
				xbox = curses_form_widget_insert_after(cw,
				    left, top, 4, CURSES_CHECKBOX, "", 0, 0);
				xbox->amount = (value[0] == 'Y' ? 1 : 0);
			} else {
				xbox = curses_form_widget_insert_after(cw,
				    left, top, cfu->widths[col] - 1,
				    CURSES_TEXTBOX, value, 256, 0);
			}
			cw = xbox;
		}
		curses_widget_tooltip_set(xbox,
		    dfui_info_get_short_desc(dfui_field_get_info(fi)));
		xbox->user_id = row;
		xbox->userdata = fi;

		if (dfui_field_property_is(fi, "editable", "false"))
			xbox->editable = 0;
		if (dfui_field_property_is(fi, "obscured", "true"))
			xbox->obscured = 1;

		if (dfui_field_option_get_first(fi) != NULL) {
			curses_widget_set_click_cb(xbox, cb_click_select_option);
		}

		left += cfu->widths[col++];
	}

	/*
	 * If this is an extensible form,
	 * create buttons for each dataset.
	 */
	if (dfui_form_is_extensible(f)) {
		if (cw == NULL) {
			button = curses_form_widget_add(cf, left,
			    top, 0, CURSES_BUTTON, "Ins", 0,
			    CURSES_WIDGET_WIDEN);
		} else {
			button = curses_form_widget_insert_after(cw, left,
			    top, 0, CURSES_BUTTON, "Ins", 0,
			    CURSES_WIDGET_WIDEN);
			cw = button;
		}
		ins_x = left;
		button->user_id = row;
		curses_widget_set_click_cb(button, cb_click_insert_row);

		left += button->width + 1;

		if (cw == NULL) {
			button = curses_form_widget_add(cf, left,
			    top, 0, CURSES_BUTTON, "Del", 0,
			    CURSES_WIDGET_WIDEN);
		} else {
			button = curses_form_widget_insert_after(cw, left,
			    top, 0, CURSES_BUTTON, "Del", 0,
			    CURSES_WIDGET_WIDEN);
			cw = button;
		}
		button->user_id = row;
		curses_widget_set_click_cb(button, cb_click_remove_row);
	}

	return(ins_x);
}

static struct curses_widget *
center_buttons(struct curses_form *cf, struct curses_widget *row_start, int is_menu)
{
	struct curses_widget *w;
	int row_width, row_offset;

	/*
	 * Center the previous row of buttons on the form
	 * if this is not a menu.
	 */
	if (!is_menu) {
		/* Find the width of all buttons on the previous row. */
		row_width = 0;
		for (w = row_start; w != NULL; w = w->next) {
			row_width += w->width + 2;
		}

		/*
		 * Adjust the x position of each of button by
		 * a calculated offset.
		 */
		row_offset = (cf->width - row_width) / 2;
		for (w = row_start; w != NULL; w = w->next) {
			w->x += row_offset;
		}

		/*
		 * Mark the next button we will create
		 * as the first button of a row.
		 */
		row_start = NULL;
	}

	return(row_start);
}

/*
 * Create a row of buttons, one for each action, at
 * the bottom of a curses_form.
 */
static void
create_buttons(const struct dfui_form *f, struct curses_form *cf, int is_menu)
{
	struct curses_widget *w;
	char name[80];
	struct dfui_action *a;
	struct curses_widget *row_start = NULL;
	int left_acc = 1;
	const char *accel;

	for (a = dfui_form_action_get_first(f); a != NULL;
	     a = dfui_action_get_next(a)) {
		strlcpy(name, dfui_info_get_name(dfui_action_get_info(a)), 70);

		dfui_debug("creating button `%s' (%d) @ %d / %d\n",
			name, strlen(name), left_acc, cf->width);

		/*
		 * Check for overflow.  If the next button would appear
		 * off the right side of the form, start putting buttons
		 * on the next row.  Or, if this is a menu, always put the
		 * next button on the next line.
		 */
		if (is_menu ||
		    ((left_acc + strlen(name) + 6) > cf->width &&
		    left_acc > 1)) {
			row_start = center_buttons(cf, row_start, is_menu);
			cf->height++;
			left_acc = 1;
		}

		w = curses_form_widget_add(cf, left_acc,
		    cf->height, 0, CURSES_BUTTON, name, 0, CURSES_WIDGET_WIDEN);
		curses_widget_tooltip_set(w,
		    dfui_info_get_short_desc(dfui_action_get_info(a)));

		accel = dfui_action_property_get(a, "accelerator");
		if (strlen(accel) > 0) {
			if (strcmp(accel, "ESC") == 0) {
				w->accel = '\e';
			} else {
				w->accel = toupper(accel[0]);
			}
		}

		left_acc += (w->width + 2);
		w->user_id = -1;
		w->userdata = a;
		curses_widget_set_click_cb(w, cb_click_close_form);
		if (row_start == NULL)
			row_start = w;
	}

	center_buttons(cf, row_start, is_menu);
}

static void
set_help(const struct dfui_form *f, struct curses_form *cf)
{
	const char *help_text;

	help_text = dfui_info_get_long_desc(dfui_form_get_info(f));
	if (cf->help_text != NULL) {
		free(cf->help_text);
	}
	if (strlen(help_text) > 0) {
		cf->help_text = aura_strdup(help_text);
	} else {
		cf->help_text = NULL;
	}
}

/*** FORM TRANSLATORS ***/

static struct curses_form *
curses_form_construct_from_dfui_form_single(const struct dfui_form *f)
{
	struct curses_form *cf;
	struct curses_form_userdata *cfu;
	const char *min_width_str;
	unsigned int desc_width, min_width = 0;
	unsigned int len, max_label_width, total_label_width;
	unsigned int max_button_width, total_button_width;
	struct dfui_field *fi;
	struct dfui_action *a;
	struct curses_widget *label, *xbox;
	struct dfui_celldata *cd;
	const char *value;
	int is_menu;

	dfui_debug("-----\nconstructing single form: %s\n",
	    dfui_info_get_name(dfui_form_get_info(f)));

	is_menu = dfui_form_property_is(f, "role", "menu");
	cf = curses_form_new(dfui_info_get_name(dfui_form_get_info(f)));
	AURA_MALLOC(cfu, curses_form_userdata);
	cfu->f = f;
	cf->userdata = cfu;
	cf->cleanup = 1;

	set_help(f, cf);

	/* Calculate offsets for nice positioning of labels and buttons. */

	/*
	 * Determine the widths of the widest field and the widest
	 * button, and the total widths of all fields and all buttons.
	 */

	max_label_width = 0;
	total_label_width = 0;
	max_button_width = 0;
	total_button_width = 0;

	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		len = MIN(60, strlen(dfui_info_get_name(dfui_field_get_info(fi))));
		if (len > max_label_width)
			max_label_width = len;
		total_label_width += (len + 2);
	}
	for (a = dfui_form_action_get_first(f); a != NULL;
	     a = dfui_action_get_next(a)) {
		len = strlen(dfui_info_get_name(dfui_action_get_info(a)));
		if (len > max_button_width)
			max_button_width = len;
		total_button_width += (len + 6);
	}

	if (total_label_width > (xmax - 2))
		total_label_width = (xmax - 2);		/* XXX scroll/wrap? */

	/* Take the short description and turn it into a set of labels. */

	if ((min_width_str = dfui_form_property_get(f, "minimum_width")) != NULL)
		min_width = atoi(min_width_str);

	desc_width = 40;
	desc_width = MAX(desc_width, min_width);
	if (is_menu) {
		desc_width = MAX(desc_width, max_button_width);
	} else {
		desc_width = MAX(desc_width, total_button_width);
	}
	desc_width = MAX(desc_width, max_label_width);  /* XXX + max_field_width */
	desc_width = MIN(desc_width, xmax - 4); /* -2 for borders, -2 for spaces */

	dfui_debug("min width: %d\n", min_width);
	dfui_debug("button width: %d\n", total_button_width);
	dfui_debug("label width: %d\n", total_label_width);
	dfui_debug("resulting width: %d\n", desc_width);
	dfui_debug("form width: %d\n", cf->width);

	cf->height = curses_form_descriptive_labels_add(cf,
	    dfui_info_get_short_desc(dfui_form_get_info(f)),
	    1, cf->height + 1, desc_width);

	dfui_debug("form width now: %d\n", cf->width);

	if (!is_menu)
		cf->height++;

	/*
	 * Add one label and one textbox (or other control) to a
	 * curses_form for each field in the dfui_form.  Each set of
	 * labels and controls is added one row below the previous set.
	 */
	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		label = curses_form_widget_add(cf, 1,
		    cf->height, max_label_width, CURSES_LABEL,
		    dfui_info_get_name(dfui_field_get_info(fi)), 0, 0);

		cd = dfui_dataset_celldata_find(dfui_form_dataset_get_first(f),
		    dfui_field_get_id(fi));

		value = dfui_celldata_get_value(cd);

		if (dfui_field_property_is(fi, "control", "checkbox")) {
			xbox = curses_form_widget_add(cf,
			    max_label_width + 3,
			    cf->height, 4, CURSES_CHECKBOX, "", 0, 0);
			xbox->amount = (value[0] == 'Y' ? 1 : 0);
		} else {
			xbox = curses_form_widget_add(cf,
			    max_label_width + 3,
			    cf->height, 20, CURSES_TEXTBOX, value, 256, 0);
		}
		curses_widget_tooltip_set(xbox,
		    dfui_info_get_short_desc(dfui_field_get_info(fi)));
		xbox->user_id = 1;
		xbox->userdata = fi;

		if (dfui_field_property_is(fi, "editable", "false"))
			xbox->editable = 0;
		if (dfui_field_property_is(fi, "obscured", "true"))
			xbox->obscured = 1;

		if (dfui_field_option_get_first(fi) != NULL) {
			curses_widget_set_click_cb(xbox, cb_click_select_option);
		}

		cf->height++;
	}

	if (dfui_form_field_get_first(f) != NULL)
		cf->height++;

	create_buttons(f, cf, is_menu);

	cf->height++;

	curses_form_finalize(cf);

	return(cf);
}

static struct curses_form *
curses_form_construct_from_dfui_form_multiple(const struct dfui_form *f)
{
	struct curses_form *cf;
	struct curses_form_userdata *cfu;
	const char *min_width_str;
	unsigned int desc_width, min_width = 0;
	unsigned int len, max_label_width, total_label_width;
	unsigned int max_button_width, total_button_width;
	struct dfui_field *fi;
	struct dfui_action *a;
	struct curses_widget *label, *button;
	struct dfui_dataset *ds;
	const char *name;
	int left_acc, top_acc;
	int row = 1, col = 0, ins_x = 1, is_menu = 0;

	dfui_debug("-----\nconstructing multiple form: %s\n",
	    dfui_info_get_name(dfui_form_get_info(f)));

	cf = curses_form_new(dfui_info_get_name(dfui_form_get_info(f)));
	AURA_MALLOC(cfu, curses_form_userdata);
	cfu->f = f;
	cf->userdata = cfu;
	cf->cleanup = 1;

	set_help(f, cf);

	/* Calculate offsets for nice positioning of labels and buttons. */

	/*
	 * Determine the widths of the widest field and the widest
	 * button, and the total widths of all fields and all buttons.
	 */

	max_label_width = 0;
	total_label_width = 0;
	max_button_width = 0;
	total_button_width = 0;

	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		len = MIN(60, strlen(dfui_info_get_name(dfui_field_get_info(fi))));
		if (len > max_label_width)
			max_label_width = len;
		total_label_width += (len + 2);
	}
	for (a = dfui_form_action_get_first(f); a != NULL;
	     a = dfui_action_get_next(a)) {
		len = strlen(dfui_info_get_name(dfui_action_get_info(a)));
		if (len > max_button_width)
			max_button_width = len;
		total_button_width += (len + 6);
	}

	/* Take the short description and turn it into a set of labels. */

	if ((min_width_str = dfui_form_property_get(f, "minimum_width")) != NULL)
		min_width = atoi(min_width_str);

	desc_width = 40;
	desc_width = MAX(desc_width, min_width);
	desc_width = MAX(desc_width, total_button_width);
	desc_width = MAX(desc_width, total_label_width);
	desc_width = MIN(desc_width, xmax - 3);

	dfui_debug("min width: %d\n", min_width);
	dfui_debug("button width: %d\n", total_button_width);
	dfui_debug("label width: %d\n", total_label_width);
	dfui_debug("resulting width: %d\n", desc_width);
	dfui_debug("form width: %d\n", cf->width);

	cf->height = curses_form_descriptive_labels_add(cf,
	    dfui_info_get_short_desc(dfui_form_get_info(f)),
	    1, cf->height + 1, desc_width);

	dfui_debug("form width now: %d\n", cf->width);

	/* Add the fields. */

	top_acc = cf->height + 1;
	cf->height += dfui_form_dataset_count(f) + 2;

	/*
	 * Create the widgets for a multiple=true form.  For each field
	 * in the form, a label containing the field's name, which serves
	 * as a heading, is created.  Underneath these labels, for each
	 * dataset in the form, a row of input widgets (typically textboxes)
	 * is added.  Non-action, manipulation buttons are also added to
	 * the right of each row.
	 */
	left_acc = 1;
	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		/*
		 * Create a label to serve as a heading for the column.
		 */
		name = dfui_info_get_name(dfui_field_get_info(fi));
		label = curses_form_widget_add(cf, left_acc,
		    top_acc, 0, CURSES_LABEL, name, 0,
		    CURSES_WIDGET_WIDEN);
		cfu->widths[col++] = label->width + 2;
		left_acc += (label->width + 2);
	}

	/*
	 * Create a row of widgets for each dataset.
	 */
	top_acc++;
	for (ds = dfui_form_dataset_get_first(f); ds != NULL;
	     ds = dfui_dataset_get_next(ds)) {
		ins_x = curses_form_create_widget_row(cf, NULL, ds,
		     1, top_acc++, row++);
	}

	/*
	 * Finally, create an 'Add' button to add a new row
	 * if this is an extensible form.
	 */
	if (dfui_form_is_extensible(f)) {
		button = curses_form_widget_add(cf,
		    ins_x, top_acc, 0,
		    CURSES_BUTTON, "Add", 0, CURSES_WIDGET_WIDEN);
		button->user_id = row;
		curses_widget_set_click_cb(button, cb_click_insert_row);
		cf->height++;
	}

	cf->height++;

	/* Add the buttons. */

	create_buttons(f, cf, is_menu);

	cf->height++;

	curses_form_finalize(cf);

	return(cf);
}

struct curses_form *
curses_form_construct_from_dfui_form(const struct dfui_form *f)
{
	if (dfui_form_is_multiple(f))
		return(curses_form_construct_from_dfui_form_multiple(f));
	else
		return(curses_form_construct_from_dfui_form_single(f));
}

#define FIFTY_EIGHT_SPACES "                                                          "

static void
strcpy_max(char *dest, const char *src, unsigned int max)
{
	unsigned int i;

	strncpy(dest, src, max);
	if (strlen(src) > max) {
		strcpy(dest + (max - 3), "...");
	} else {
		strncpy(dest + strlen(src),
		    FIFTY_EIGHT_SPACES, max - strlen(src));
	}
	for (i = 0; i < strlen(dest); i++) {
		if (isspace(dest[i]))
			dest[i] = ' ';
	}
}

struct curses_form *
curses_form_construct_from_dfui_progress(const struct dfui_progress *pr,
					 struct curses_widget **pbar,
					 struct curses_widget **plab,
					 struct curses_widget **pcan)
{
	struct curses_form *cf;
	const char *desc;

	desc = dfui_info_get_short_desc(dfui_progress_get_info(pr));

	cf = curses_form_new(dfui_info_get_name(dfui_progress_get_info(pr)));

	cf->width = 60;
	cf->height = 6;

	if (dfui_progress_get_streaming(pr)) {
		cf->height = 20;
	}

	*plab = curses_form_widget_add(cf, 0, 1, 58,
	    CURSES_LABEL, FIFTY_EIGHT_SPACES, 0, CURSES_WIDGET_CENTER);
	strcpy_max((*plab)->text, desc, 58);
	*pbar = curses_form_widget_add(cf, 0, 3, 40,
	    CURSES_PROGRESS, "", 0, CURSES_WIDGET_CENTER);
	*pcan = curses_form_widget_add(cf, 0, 5, 0,
	    CURSES_BUTTON, "Cancel", 0,
	    CURSES_WIDGET_CENTER | CURSES_WIDGET_WIDEN);
	(*pbar)->amount = dfui_progress_get_amount(pr);

	last_y = (*pbar)->y + 2;

	curses_form_finalize(cf);

	gettimeofday(&last_update, NULL);

	return(cf);
}

void
curses_widgets_update_from_dfui_progress(const struct dfui_progress *pr,
					 struct curses_widget *pbar,
					 struct curses_widget *plab,
					 struct curses_widget *pcan)
{
	const char *short_desc;
	struct timeval now;
	long msec_diff;
	struct curses_widget *w;
	int short_desc_changed;

	gettimeofday(&now, NULL);
	msec_diff = (now.tv_sec - last_update.tv_sec) * 1000 +
		    (now.tv_usec - last_update.tv_usec) / 1000;

	short_desc = dfui_info_get_short_desc(dfui_progress_get_info(pr));
	short_desc_changed = (strncmp(plab->text, short_desc, MIN(55, strlen(short_desc))) != 0);

	if (msec_diff < 100 && !dfui_progress_get_streaming(pr) && !short_desc_changed)
		return;

	if (dfui_progress_get_amount(pr) != pbar->amount ||
	    short_desc_changed ||
	    dfui_progress_get_streaming(pr)) {
		strcpy_max(plab->text, short_desc, 58);
		curses_widget_draw(plab);
		pbar->amount = dfui_progress_get_amount(pr);
		curses_widget_draw(pbar);
		if (dfui_progress_get_streaming(pr)) {
			/* add a label with the text */
			w = curses_form_widget_add(pbar->form, 0, ++last_y, 58,
			    CURSES_LABEL, FIFTY_EIGHT_SPACES, 0, CURSES_WIDGET_CENTER);
			strcpy_max(w->text, dfui_progress_get_msg_line(pr), 58);
			if (last_y >= pbar->form->int_height) {
				pbar->form->int_height = last_y + 1;
			}
			curses_form_widget_ensure_visible(w);
			curses_widget_draw(w);
		}
	} else {
		curses_progress_spin(pbar);
	}
	wmove(pcan->form->win, pcan->y + 1, pcan->x + pcan->width + 1);
	curses_form_refresh(NULL);
	last_update = now;
}

static const char *
curses_widget_xlat_value(const struct curses_widget *cw)
{
	if (cw->type == CURSES_TEXTBOX)
		return(cw->text);
	else if (cw->type == CURSES_CHECKBOX)
		return(cw->amount ? "Y" : "N");
	else
		return("");
}

static struct dfui_response *
response_construct_from_curses_form_single(const struct dfui_form *f,
					   const struct curses_form *cf,
					   const struct curses_widget *cw)
{
	struct dfui_response *r = NULL;
	struct dfui_action *selected = NULL;
	struct dfui_dataset *ds = NULL;
	const char *id;
	const char *value;

	selected = cw->userdata;
	r = dfui_response_new(dfui_form_get_id(f),
			      dfui_action_get_id(selected));
	ds = dfui_dataset_new();
	for (cw = cf->widget_head; cw != NULL; cw = cw->next) {
		if (cw->user_id > 0) {
			id = dfui_field_get_id((struct dfui_field *)cw->userdata);
			value = curses_widget_xlat_value(cw);
			dfui_dataset_celldata_add(ds, id, value);
		}
	}
	dfui_response_dataset_add(r, ds);

	return(r);
}

static struct dfui_response *
response_construct_from_curses_form_multiple(const struct dfui_form *f,
					     const struct curses_form *cf,
					     const struct curses_widget *cw)
{
	struct dfui_response *r = NULL;
	struct dfui_action *selected = NULL;
	struct dfui_dataset *ds = NULL;
	const char *id;
	const char *value;
	int row = 0;
	int rows = 100; /* XXX obviously we'd prefer something more efficient here! */
	int cds_added = 0;

	selected = cw->userdata;
	r = dfui_response_new(dfui_form_get_id(f),
			      dfui_action_get_id(selected));

	/* Create one dataset per row. */
	for (row = 1; row < rows; row++) {
		ds = dfui_dataset_new();
		cds_added = 0;
		for (cw = cf->widget_head; cw != NULL; cw = cw->next) {
			if (cw->user_id == row &&
			    (cw->type == CURSES_TEXTBOX || cw->type == CURSES_CHECKBOX)) {
				id = dfui_field_get_id((struct dfui_field *)cw->userdata);
				value = curses_widget_xlat_value(cw);
				dfui_dataset_celldata_add(ds, id, value);
				cds_added += 1;
			}
		}
		if (cds_added > 0) {
			dfui_response_dataset_add(r, ds);
		} else {
			dfui_dataset_free(ds);
		}
	}

	return(r);
}

struct dfui_response *
response_construct_from_curses_form(const struct dfui_form *f,
				    const struct curses_form *cf,
				    const struct curses_widget *cw)
{
	if (dfui_form_is_multiple(f))
		return(response_construct_from_curses_form_multiple(f, cf, cw));
	else
		return(response_construct_from_curses_form_single(f, cf, cw));
}
