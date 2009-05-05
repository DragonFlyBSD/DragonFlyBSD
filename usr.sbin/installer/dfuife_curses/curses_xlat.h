/*
 * curses_xlat.h
 * $Id: curses_xlat.h,v 1.3 2005/02/08 05:54:44 cpressey Exp $
 */

#ifndef __CURSES_XLAT_H
#define __CURSES_XLAT_H

/*
 * Info structure attached to each curses form's userdata pointer.
 * Lets us get back to the underlying dfui form and track columns widths.
 */
struct curses_form_userdata {
	const struct dfui_form *f;
	int widths[256];
};

int			 curses_form_create_widget_row(struct curses_form *,
				struct curses_widget *, const struct dfui_dataset *,
				int, int, int);
struct curses_form	*curses_form_construct_from_dfui_form(const struct dfui_form *);
struct curses_form	*curses_form_construct_from_dfui_progress(const struct dfui_progress *,
								  struct curses_widget **,
								  struct curses_widget **,
								  struct curses_widget **);
void			 curses_widgets_update_from_dfui_progress(const struct dfui_progress *,
								  struct curses_widget *,
								  struct curses_widget *,
								  struct curses_widget *);
struct dfui_response	*response_construct_from_curses_form(const struct dfui_form *,
							     const struct curses_form *,
							     const struct curses_widget *);

#endif /* !__CURSES_XLAT_H */
