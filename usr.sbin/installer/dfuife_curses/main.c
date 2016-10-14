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
 * main.c
 * Main program for dfuife_curses.
 * $Id: main.c,v 1.21 2005/03/25 04:51:10 cpressey Exp $
 */

#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#include "libdfui/lang.h"
#define _(String) gettext (String)
extern int _nl_msg_cat_cntr;
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"

#include "libdfui/dfui.h"
#ifdef DEBUG
#include "libdfui/dump.h"
#endif
#include "libdfui/system.h"

#include "curses_form.h"
#include "curses_widget.h"
#include "curses_bar.h"
#include "curses_util.h"
#include "curses_xlat.h"

/*** GLOBALS ***/

struct curses_bar *menubar, *statusbar;

/*** SIGNAL HANDLING ***/

#ifdef SIGNAL_HANDLING
volatile sig_atomic_t caught_signal;

void signal_handler(int signo)
{
	caught_signal = signo;
}

void
abort_frontend(struct dfui_connection *c)
{
	dfui_fe_abort(c);

	clear();
	refresh();
	endwin();
	exit(1);
}
#endif

static struct dfui_response *
#ifdef SIGNAL_HANDLING
present_form(struct dfui_connection *c, struct dfui_form *f)
#else
present_form(struct dfui_connection *c __unused, struct dfui_form *f)
#endif
{
	struct dfui_response *r = NULL;
	struct curses_form *cf;
	struct curses_widget *cw;

	cf = curses_form_construct_from_dfui_form(f);
	curses_form_draw(cf);
	curses_form_refresh(cf);
	cw = curses_form_frob(cf);
#ifdef SIGNAL_HANDLING
	if (caught_signal) abort_frontend(c);
#endif
	r = response_construct_from_curses_form(f, cf, cw);
	curses_form_free(cf);
	curses_form_refresh(NULL);
	return(r);
}

static void
usage(char **argv)
{
	fprintf(stderr, _("Usage: %s "
	    "[-b backdrop] [-r rendezvous] [-t npipe|tcp]\n"),
	    argv[0]);
	exit(1);
}

/*
 * dfuife_curses
 * DFUI Curses frontend.
 */
int
main(int argc, char **argv)
{
	struct dfui_connection *c;
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_progress *pr;
	struct dfui_property *gp;

	struct curses_form *pf = NULL;
	struct curses_widget *pbar = NULL, *plab = NULL, *pcan = NULL;
	struct curses_widget *w;

	void *payload = NULL;
	int done = 0;
	char msgtype;
	int opt;
	char last_message[80];
	int is_streaming = 0;
	int ch;
	char *bdropfn = NULL;
	char *rendezvous = NULL;
	int transport = 0;
	int force_monochrome = 0;

	/*
	 * Get command-line arguments.
	 */
	while ((opt = getopt(argc, argv, "b:mr:t:")) != -1) {
		switch(opt) {
		case 'b':
			bdropfn = aura_strdup(optarg);
			break;
		case 'm':
			force_monochrome = 1;
			break;
		case 'r':
			rendezvous = aura_strdup(optarg);
			break;
		case 't':
			transport = user_get_transport(optarg);
			break;
		case '?':
		default:
			usage(argv);
		}
	}
	argc -= optind;
	argv += optind;

	if (!transport)
		transport = user_get_transport("tcp");

	if (rendezvous == NULL) {
		if (transport == DFUI_TRANSPORT_TCP) {
			rendezvous = aura_strdup("9999");
		} else {
			rendezvous = aura_strdup("test");
		}
	}

#ifdef ENABLE_NLS
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
#endif

	/*
	 * Set up screen.
	 */

	initscr();

#ifdef SIGNAL_HANDLING
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
#endif

	curses_colors_init(force_monochrome);
	cbreak();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	curs_set(0);

	getmaxyx(stdscr, ymax, xmax);

	if (bdropfn == NULL) {
		curses_colors_set(stdscr, CURSES_COLORS_BACKDROP);
		curses_window_blank(stdscr);
	} else {
		curses_load_backdrop(stdscr, bdropfn);
	}

	menubar = curses_bar_new(0, 0, 0, 1, CURSES_COLORS_MENUBAR,
				 CURSES_BAR_WIDEN);
	statusbar = curses_bar_new(0, 0, 0, 1, CURSES_COLORS_STATUSBAR,
				   CURSES_BAR_WIDEN | CURSES_BAR_BOTTOM);

	curses_bar_set_text(menubar, _("F10=Refresh Display"));
	curses_bar_set_text(statusbar, _("Waiting for backend..."));

	update_panels();
	doupdate();

#ifdef DEBUG
	dfui_debug_file = fopen("/tmp/dfuife_curses_debug.log", "w");
#endif

	c = dfui_connection_new(transport, rendezvous);
	dfui_fe_connect(c);

	curses_bar_set_text(statusbar, _("Connected"));

	while (!done) {
		dfui_fe_receive(c, &msgtype, &payload);
		switch (msgtype) {
		case DFUI_BE_MSG_PRESENT:
			f = (struct dfui_form *)payload;
			r = present_form(c, f);
#ifdef SIGNAL_HANDLING
			if (caught_signal) abort_frontend(c);
#endif
			dfui_fe_submit(c, r);
			dfui_form_free(f);
			dfui_response_free(r);
			break;
		case DFUI_BE_MSG_PROG_BEGIN:
			pr = (struct dfui_progress *)payload;
			if (pf != NULL)
				curses_form_free(pf);
			is_streaming = dfui_progress_get_streaming(pr);
			strncpy(last_message, dfui_info_get_short_desc(
			    dfui_progress_get_info(pr)), 79);
			pf = curses_form_construct_from_dfui_progress(pr,
			    &pbar, &plab, &pcan);
			curses_form_draw(pf);
			curses_form_refresh(pf);
			dfui_progress_free(pr);
			nodelay(stdscr, TRUE);
			dfui_fe_progress_continue(c);
			break;
		case DFUI_BE_MSG_PROG_UPDATE:
			pr = (struct dfui_progress *)payload;
			if (pf != NULL) {
				curses_widgets_update_from_dfui_progress(pr,
				    pbar, plab, pcan);
			}
			dfui_progress_free(pr);
			ch = getch();
			if (ch == ' ' || ch == '\n' || ch == '\r') {
				dfui_fe_progress_cancel(c);
			} else if (ch == KEY_F(10)) {
				redrawwin(stdscr);
				curses_form_refresh(NULL);
				dfui_fe_progress_continue(c);
			} else {
				dfui_fe_progress_continue(c);
			}
			break;
		case DFUI_BE_MSG_PROG_END:
			if (pf != NULL) {
				if (is_streaming) {
					w = curses_form_widget_add(pf, 0, pf->int_height, 0,
					    CURSES_BUTTON, "OK", -1,
					    CURSES_WIDGET_CENTER | CURSES_WIDGET_WIDEN);
					pf->int_height++;
					curses_widget_set_click_cb(w, cb_click_close_form);
					pf->widget_focus = w;
					curses_form_widget_ensure_visible(w);
					curses_widget_draw(w);
					curses_form_refresh(pf);
					curses_form_frob(pf);
				}
				curses_form_free(pf);
				curses_form_refresh(NULL);
			}
			pf = NULL;
			plab = pbar = pcan = NULL;
			nodelay(stdscr, FALSE);
			dfui_fe_progress_continue(c);
			break;
		case DFUI_BE_MSG_SET_GLOBAL:
			gp = (struct dfui_property *)payload;

#ifdef ENABLE_NLS
			/*
			 * Check for a change to the "lang" setting...
			 */
			if (strcmp(dfui_property_get_name(gp), "lang") == 0) {
				set_lang_envars(dfui_property_get_value(gp));

				/* let gettext know about changes */
				++_nl_msg_cat_cntr;

				/* BEGIN: reinit curses to use new TERM */
				curses_bar_free(menubar);
				curses_bar_free(statusbar);

				endwin();
				newterm(getenv("TERM"), stdout, stdin);

				curses_colors_init(force_monochrome);
				cbreak();
				noecho();
				nonl();
				keypad(stdscr, TRUE);
				curs_set(0);

				update_panels();
				doupdate();

				if (bdropfn == NULL) {
					curses_colors_set(stdscr,
					    CURSES_COLORS_BACKDROP);
					curses_window_blank(stdscr);
				} else {
					curses_load_backdrop(stdscr, bdropfn);
				}

				menubar = curses_bar_new(0, 0, 0, 1,
				    CURSES_COLORS_MENUBAR, CURSES_BAR_WIDEN);
				statusbar = curses_bar_new(0, 0, 0, 1,
				    CURSES_COLORS_STATUSBAR,
				    CURSES_BAR_WIDEN | CURSES_BAR_BOTTOM);
				/* END: reinit curses to use new TERM */

				curses_bar_set_text(menubar,
					_("F1=Help F10=Refresh Display"));
			}
#endif

			dfui_fe_confirm_set_global(c);
			dfui_property_free(gp);
			break;
		case DFUI_BE_MSG_STOP:
			dfui_fe_confirm_stop(c);
			done = 1;
			break;
		}
	}

	dfui_fe_disconnect(c);

#ifdef DEBUG
	fclose(dfui_debug_file);
#endif

	curses_bar_free(menubar);
	curses_bar_free(statusbar);

	clear();
	refresh();
	endwin();

	if (rendezvous != NULL)
		free(rendezvous);
	if (bdropfn != NULL)
		free(bdropfn);

	exit(0);
}
