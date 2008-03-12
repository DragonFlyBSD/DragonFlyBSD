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
 * This code was derived in part from:
 * $_DragonFly: src/test/caps/client.c,v 1.3 2004/03/31 20:27:34 dillon Exp $
 * and is therefore also subject to the license conditions on that file.
 *
 * $Id: be_test.c,v 1.39 2005/02/08 22:58:00 cpressey Exp $
 */

#include <err.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#include "dfui/lang.h"
#define _(String) gettext (String)
extern int _nl_msg_cat_cntr;
#else
#define _(String) (String)
#endif

#include "dfui/dfui.h"
#include "dfui/dump.h"
#include "dfui/system.h"
#include "dfui/lang.h"

#include "aura/mem.h"
#include "aura/buffer.h"

static void	 usage(char **);
static void	 abort_backend(void);

static int	 show_streaming_progress_bar(struct dfui_connection *);
static int	 show_typical_progress_bar(struct dfui_connection *);
static void	 show_typical_form(struct dfui_connection *);
static void	 show_multi_form(struct dfui_connection *, int);
static void	 show_typical_confirm(struct dfui_connection *);
static void	 show_typical_alert(struct dfui_connection *);
static void	 show_typical_menu(struct dfui_connection *);
static void	 show_scrolling_form(struct dfui_connection *);
static void	 show_set_global_form(struct dfui_connection *);
static void	 show_unimplemented(struct dfui_connection *);
static int	 show_main_menu(struct dfui_connection *);
#ifdef ENABLE_NLS
static char	*show_lang_menu(struct dfui_connection *);
#endif

struct dfui_dataset *ds = NULL;
char mountpoint[16][256];
char capacity[16][256];
int rows;

static void
abort_backend(void)
{
	fprintf(stderr, _("Backend was aborted by DFUI_FE_ABORT message from frontend.\n"));
	exit(1);
}

#ifdef ENABLE_NLS
static char *
show_lang_menu(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;
	char *id;

	fprintf(stderr, _("Language menu section entered.\n"));

	f = dfui_form_create(
	    "main_menu",
	    _("DFUI Test Suite - Language Choice"),
	    _("You can choose localisation from list."),
	    "",

	    "p", "role", "menu",

	    "a", "default", "default",
	    "Do not choose any language", "",
	    "a", "ru", "Russian",
	    "Russian KOI8-R", "",
	    NULL
	);

	fprintf(stderr, _("Presenting language menu to frontend.\n"));
	if (!dfui_be_present(c, f, &r))
		abort_backend();
	fprintf(stderr, _("Language menu presented.\n"));

	if (dfui_response_get_action_id(r) == NULL) {
		fprintf(stderr, _("Invalid response from frontend. Aborting.\n"));
		abort_backend();
	}
	id = aura_strdup(dfui_response_get_action_id(r));

	fprintf(stderr, _("Language menu section finished (%s).\n"), id);

	dfui_form_free(f);
	dfui_response_free(r);

	if(strcmp(id, "default") == 0)
		return(id);

	/* set keymap, scrnmap, fonts */
	if(!set_lang_syscons(id))
		return(NULL);

	/* set envars */
	if(!set_lang_envars(id))
		return(NULL);

	return(id);
}
#endif

/*** DRIVERS ***/

static int
show_streaming_progress_bar(struct dfui_connection *c)
{
	struct dfui_progress *pr;
	char msg_text[256];
	int cancelled = 0;
	int i;
	
	fprintf(stderr, _("Streaming progress bar section entered.\n"));

	pr = dfui_progress_new(dfui_info_new(
	    _("Streaming Progress Indicator"),
	    _("This is a streaming progress indicator.  You may cause it "
	    "to be aborted (which should be acknowledged after a brief delay.)"),
	    ""),
	    0);

	dfui_progress_set_streaming(pr, 1);

	fprintf(stderr, _("Begin streaming progress bar.\n"));
	if (!dfui_be_progress_begin(c, pr))
		abort_backend();

	for (i = 1; i <= 20; i++) {
		sleep(1);
		dfui_progress_set_amount(pr, i);
		fprintf(stderr, _("Update streaming progress bar.\n"));
		dfui_info_set_short_desc(dfui_progress_get_info(pr), _("Update streaming progress bar."));
		snprintf(msg_text, 256, _("This is line #%d.\n"), i);
		dfui_progress_set_msg_line(pr, msg_text);
		if (!dfui_be_progress_update(c, pr, &cancelled))
			abort_backend();
		if (cancelled) {
			fprintf(stderr, _("Streaming Progress bar was cancelled!\n"));
			break;
		}
	}

	fprintf(stderr, _("End streaming progress bar.\n"));
	if (!dfui_be_progress_end(c))
		abort_backend();
	dfui_progress_free(pr);

	fprintf(stderr, _("Streaming progress bar section finished.\n"));
	return(!cancelled);
}

static int
show_typical_progress_bar(struct dfui_connection *c)
{
	struct dfui_progress *pr;
	int i;
	int cancelled = 0;

	fprintf(stderr, _("Typical progress bar section entered.\n"));

	pr = dfui_progress_new(
		dfui_info_new(
			_("Typical Progress Indicator"),
			_("This is a typical progress indicator.  You may cause it "
			"to be aborted (which should be acknowledged after a brief delay.)"),
			""),
		0);

	fprintf(stderr, _("Begin typical progress bar.\n"));
	if (!dfui_be_progress_begin(c, pr))
		abort_backend();
	
	for (i = 1; i <= 10; i++) {
		sleep(1);
		dfui_progress_set_amount(pr, i * 10);
		fprintf(stderr, _("Update typical progress bar.\n"));
		if (!dfui_be_progress_update(c, pr, &cancelled))
			abort_backend();
		if (cancelled) {
			fprintf(stderr, _("Progress bar was cancelled!\n"));
			break;
		}
	}

	fprintf(stderr, _("End typical progress bar.\n"));
	if (!dfui_be_progress_end(c))
		abort_backend();
	dfui_progress_free(pr);

	fprintf(stderr, _("Typical progress bar section finished.\n"));
	return(!cancelled);
}

static void
show_typical_form(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *new_ds;
	struct dfui_celldata *cd;

	fprintf(stderr, _("Typical form section entered.\n"));

	f = dfui_form_create(
	    "typical_form",
	    _("Typical Form"),
	    _("This is a typical DFUI form.  One field has a hint "
	    "that suggests that it can be rendered as a checkbox."),
	    "",

	    "f", "system_name", _("System Name"),
	    _("Enter the name of this system"), "",
	    "f", "is_name_server", _("Name Server?"),
	    _("Will this system be used as a DNS server?"), "",
	    "p", "control", "checkbox",

	    "f", "logged_in_as", _("Logged in as"),
	    _("A non-editable field"), "",
	    "p", "editable", "false",

	    "f", "password", "Password",
	    _("Enter your mock password here"), "",
	    "p", "obscured", "true",

	    "f", "machine_role", _("Machine Role"),
	    _("Select a general role for this machine to play"), "",
	    "p", "editable", "false",
	    "o", _("Workstation"), "o", _("Server"), "o", _("Laptop"),

	    "a", "ok", _("OK"), _("Accept these values"), "",
	    "a", "cancel",_("Cancel"), _("Cancel and return to previous form"), "",
	    NULL
	);

	dfui_form_dataset_add(f, dfui_dataset_dup(ds));

	fprintf(stderr, _("Present typical form.\n"));
	if (!dfui_be_present(c, f, &r))
		abort_backend();
	fprintf(stderr, _("Typical form was presented.\n"));

	new_ds = dfui_response_dataset_get_first(r);

	cd = dfui_dataset_celldata_find(new_ds, "system_name");
	if (cd == NULL) {
		fprintf(stderr, _("system_name WAS NOT PRESENT IN RESPONSE.\n"));
		dfui_dataset_celldata_add(new_ds, "system_name", "NULL");
	} else {
		fprintf(stderr, "system_name = %s\n", dfui_celldata_get_value(cd));
	}

	cd = dfui_dataset_celldata_find(new_ds, "is_name_server");
	if (cd == NULL) {
		fprintf(stderr, _("is_name_server WAS NOT PRESENT IN RESPONSE.\n"));
		dfui_dataset_celldata_add(new_ds, "is_name_server", "N");
	} else {
		fprintf(stderr, "is_name_server = %s\n", dfui_celldata_get_value(cd));
	}

	cd = dfui_dataset_celldata_find(new_ds, "logged_in_as");
	if (cd == NULL) {
		fprintf(stderr, _("logged_in_as WAS NOT PRESENT IN RESPONSE.\n"));
		dfui_dataset_celldata_add(new_ds, "logged_in_as", "NULL");
	} else {
		fprintf(stderr, "logged_in_as = %s\n", dfui_celldata_get_value(cd));
	}

	cd = dfui_dataset_celldata_find(new_ds, "password");
	if (cd == NULL) {
		fprintf(stderr, _("password WAS NOT PRESENT IN RESPONSE.\n"));
		dfui_dataset_celldata_add(new_ds, "password", "");
	} else {
		fprintf(stderr, "password = %s\n", dfui_celldata_get_value(cd));
	}

	cd = dfui_dataset_celldata_find(new_ds, "machine_role");
	if (cd == NULL) {
		fprintf(stderr, _("machine_role WAS NOT PRESENT IN RESPONSE.\n"));
		dfui_dataset_celldata_add(new_ds, "machine_role", _("Server"));
	} else {
		fprintf(stderr, "machine_role = %s\n", dfui_celldata_get_value(cd));
	}

	fprintf(stderr, _("The action that selected was '%s'.\n"), 
	    dfui_response_get_action_id(r));

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		dfui_dataset_free(ds);
		ds = dfui_dataset_dup(new_ds);
	}

	fprintf(stderr, _("Typical form section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_multi_form(struct dfui_connection *c, int extensible)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *slice_ds;
	int i;

	fprintf(stderr, _("Multi-dataset, %s form section entered.\n"),
	    extensible ? "extensible" : "non-extensible");

	f = dfui_form_create(
	    "multi_form",
	    _("Multi-Dataset Form"),
	    _("This is a form with not just one set of data, but several."),
	    "",

	    "f", "mountpoint", _("Mountpoint"),
	    _("Where this filesystem will be logically located"), "",
	    "f", "capacity", _("Capacity"),
	    _("How many megabytes of data this slice will hold"), "",

	    "a", "ok", _("OK"), _("Accept these values"), "",
	    "a", "cancel", _("Cancel"), _("Cancel and return to previous form"), "",
	    NULL
	);

	dfui_form_set_multiple(f, 1);
	dfui_form_set_extensible(f, extensible);

	for (i = 0; i < rows; i++) {
		slice_ds = dfui_dataset_new();
		dfui_dataset_celldata_add(slice_ds,
		    "mountpoint", mountpoint[i]);
		dfui_dataset_celldata_add(slice_ds,
		    "capacity", capacity[i]);
		dfui_form_dataset_add(f, slice_ds);
	}

	fprintf(stderr, _("Present multi-dataset form.\n"));
	if (!dfui_be_present(c, f, &r))
		abort_backend();
	fprintf(stderr, _("Multi-dataset form was presented.\n"));

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		const char *value;
		struct dfui_celldata *cd;

		i = 0;
		for (slice_ds = dfui_response_dataset_get_first(r);
		     slice_ds != NULL;
		     slice_ds = dfui_dataset_get_next(slice_ds)) {
			cd = dfui_dataset_celldata_find(slice_ds, "mountpoint");
			if (cd == NULL) {
				fprintf(stderr, _("Response dataset #%d DID NOT INCLUDE mountpoint\n"), i);
			} else {
				value = dfui_celldata_get_value(cd);
				fprintf(stderr, _("Response dataset #%d, mountpoint=`%s'\n"), i, value);
				strncpy(mountpoint[i], value, 255);
			}
			cd = dfui_dataset_celldata_find(slice_ds, "capacity");
			if (cd == NULL) {
				fprintf(stderr, _("Response dataset #%d DID NOT INCLUDE capacity\n"), i);
			} else {
				value = dfui_celldata_get_value(cd);
				fprintf(stderr, _("Response dataset #%d, capacity=`%s'\n"), i, value);
				strncpy(capacity[i], value, 255);
			}
			i++;
			if (i == 16)
				break;
		}

		rows = i;
	}

	fprintf(stderr, _("Multi-dataset form section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_typical_confirm(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;

	fprintf(stderr, _("Typical confirmation section entered.\n"));

	f = dfui_form_create(
	    "typical_confirm",
	    _("Confirm Reset"),
	    _("Are you sure you wish to reset some settings or other "
	    "to the factory defaults?"),
	    "",
	    "p", "role", "confirm",

	    "a", "yes", _("Yes"), _("Reset settings"), "",
	    "a", "no", _("No"), _("Keep current settings"), "",
		NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	fprintf(stderr, _("Typical confirmation section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_typical_alert(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;

	fprintf(stderr, _("Typical alert section entered.\n"));

	f = dfui_form_create(
	    "typical_alert",
	    _("Confirm Self-Injury"),
	    _("Warning: Pistol is loaded! "
	    "Are you SURE you wish to shoot yourself in the foot?"),
	    "",
	    "p", "role", "alert",

	    "a", "yes", _("Yes"), _("Pull the trigger"), "",
	    "a", "no", _("No"), _("Make the smart choice"), "",
	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	fprintf(stderr, _("Typical alert section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_typical_menu(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;

	fprintf(stderr, _("Typical menu section entered.\n"));

	f = dfui_form_create(
	    "typical_menu",
	    _("Typical Menu"),
	    _("Pick one."),
	    _("Note that each of these menu items is associated with an "
	      "'accelerator', or shortcut key.  Pressing that key should "
	      "automatically select the associated item."),

	    "p", "role", "menu",

	    "a", "beef", _("Beef"),       "Beef comes from cows", "",
	    "p", "accelerator", "B",
	    "a", "chicken", _("Chicken"), "Chickens come from coops", "",
	    "p", "accelerator", "C",
	    "a", "tofu", _("Tofu"),       "Tofu comes from beans", "",
	    "p", "accelerator", "T",
	    "a", "cancel", _("Cancel"),   "Cancel this selection", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	fprintf(stderr, _("Typical menu section finished; %s was selected.\n"),
	    dfui_response_get_action_id(r));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_scrolling_form(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct aura_buffer *song;
	char *one_verse;
	int bottles;
	int size = 0;

	song = aura_buffer_new(16384);
	one_verse = malloc(2048);
	for (bottles = 99; bottles > 0; bottles--) {
		sprintf(one_verse, _("%d bottles of beer on the wall,\n"
		    "%d bottles of beer,\n"
		    "Take one down, pass it around,\n"
		    "%d bottles of beer on the wall.\n\n"),
		    bottles, bottles, bottles - 1);
		size += strlen(one_verse);
		aura_buffer_cat(song, one_verse);
	}

	fprintf(stderr, _("Scrolling form section entered (%d bytes.)\n"), size);

	f = dfui_form_create(
	    "beer",
	    _("Sing Along Everybody!"),
	    aura_buffer_buf(song),
	    "",

	    "a", "ok", _("OK"), _("No more beer"), "",
	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	fprintf(stderr, _("Scrolling form section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
	aura_buffer_free(song);
	free(one_verse);
}

static void
show_set_global_form(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *gds;

	fprintf(stderr, _("Set-global form section entered.\n"));

	f = dfui_form_create(
	    "set_global_form",
	    _("Set-Global Form"),
	    _("Please enter the name and new value of the global "
	      "setting of the frontend that you wish to change."),
	    "",

	    "f", "name", _("Setting's Name"), "", "",
	    "f", "value", _("Setting's new Value"), "", "",

	    "a", "ok", _("OK"), _("Accept these values"), "",
	    "a", "cancel",_("Cancel"), _("Cancel and return to previous form"), "",
	    NULL
	);

	gds = dfui_dataset_new();
	dfui_dataset_celldata_add(gds, "name", "lang");
	dfui_dataset_celldata_add(gds, "value", "ru");
	dfui_form_dataset_add(f, gds);

	fprintf(stderr, _("Present set-global form.\n"));
	if (!dfui_be_present(c, f, &r))
		abort_backend();
	fprintf(stderr, _("Set-global form was presented.\n"));

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		struct dfui_celldata *cd;
		struct dfui_dataset *rds;
		const char *name = "", *value = "";
		int cancelled = 0;

		rds = dfui_response_dataset_get_first(r);

		if ((cd = dfui_dataset_celldata_find(rds, "name")) != NULL)
			name = dfui_celldata_get_value(cd);
		if ((cd = dfui_dataset_celldata_find(rds, "value")) != NULL)
			value = dfui_celldata_get_value(cd);

		fprintf(stderr, _("Setting global setting '%s'='%s' in frontend.\n"),
		    name, value);
		dfui_be_set_global_setting(c, name, value, &cancelled);
		if (cancelled) {
			fprintf(stderr, _("Global setting was cancelled "
					  "(not supported or non-critical "
					  "failure.)\n"));
		} else {
			fprintf(stderr, _("Global setting was set.\n"));
		}
	} else {
		fprintf(stderr, _("Cancel selected, no global settings changed.\n"));
	}

	fprintf(stderr, _("Set-global form section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static void
show_unimplemented(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;

	fprintf(stderr, _("Not yet implemented section entered.\n"));

	f = dfui_form_create(
	    "nyi",
	    _("Not Yet Implemented"),
	    _("The feature you have chosen is not yet implemented."),
	    "",
	    "p", "role", "alert",

	    "a", "ok", _("OK"), _("Proceed"), "",
	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	fprintf(stderr, _("Not yet implemented section finished.\n"));
	dfui_form_free(f);
	dfui_response_free(r);
}

static int
show_main_menu(struct dfui_connection *c)
{
	struct dfui_form *f;
	struct dfui_response *r;
	const char *id;
	int done = 0;

	fprintf(stderr, _("Main menu section entered.\n"));

	f = dfui_form_create(
	    "main_menu",
	    _("DFUI Test Suite - Main Menu"),
	    _("This backend is a test suite for the DFUI abstract user interface "
	    "layer.  It exercises most of the features of the DFUI protocol. "
	    "It also demonstrates how a backend program, containing "
	    "only the application logic, can send an abstract representation "
	    "of a form to the frontend, which can interpret it any way it "
	    "wishes, and send a response back to the backend."),
	    "",

	    "p", "role", "menu",

	    "a", "menu", _("Typical Menu"),
	    _("Display a typical menu"), "",
	    "a", "confirm", _("Typical Confirmation"),
	    _("Display a typical confirmation dialog box"), "",
	    "a", "alert", _("Typical Alert"),
	    _("Display a typical alert dialog box"), "",
	    "a", "form", _("Typical Form"),
	    _("Display a typical form"), "",
	    "a", "multi", _("Multi-Dataset Form"),
	    _("Display a form with more than one dataset"), "",
	    "a", "extensible", _("Extensible Form"),
	    _("Display a form with variable number of datasets"), "",
	    "a", "progress", _("Progress Bar"),
	    _("Display a typical progress bar"), "",
	    "a", "streaming_progress_bar", _("Streaming Progress Bar"),
	    _("Display a streaming progress bar"), "",
	    "a", "scroll", _("Scrolling Form"),
	    _("Display a form too large to be fully viewed"), "",
	    "a", "set_global", _("Set Frontend Settings"),
	    _("Change global properties of the frontend, from the backend"), "",
	    "a", "exit", _("Exit"), _("Leave this Test Suite"), "",
	    NULL
	);

	fprintf(stderr, _("Presenting main menu to frontend.\n"));
	if (!dfui_be_present(c, f, &r))
		abort_backend();
	fprintf(stderr, _("Main menu presented.\n"));

	id = dfui_response_get_action_id(r);
	if (id == NULL) {
		fprintf(stderr, _("Invalid response from frontend. Aborting.\n"));
		abort_backend();
	}
	
	if (strcmp(id, "exit") == 0)
		done = 1;
	else if (strcmp(id, "progress") == 0)
		show_typical_progress_bar(c);
	else if (strcmp(id, "form") == 0)
		show_typical_form(c);
	else if (strcmp(id, "multi") == 0)
		show_multi_form(c, 0);
	else if (strcmp(id, "extensible") == 0)
		show_multi_form(c, 1);
	else if (strcmp(id, "confirm") == 0)
		show_typical_confirm(c);
	else if (strcmp(id, "alert") == 0)
		show_typical_alert(c);
	else if (strcmp(id, "menu") == 0)
		show_typical_menu(c);
	else if (strcmp(id, "scroll") == 0)
		show_scrolling_form(c);
	else if (strcmp(id, "streaming_progress_bar") == 0)
		show_streaming_progress_bar(c);
	else if (strcmp(id, "set_global") == 0)
		show_set_global_form(c);
	else
		show_unimplemented(c);

	fprintf(stderr, _("Main menu section finished.\n"));

	dfui_form_free(f);
	dfui_response_free(r);
	
	return(done);
}

static void
usage(char **argv)
{
	fprintf(stderr, _("Usage: %s [-r rendezvous] [-t caps|npipe|tcp]\n"), argv[0]);
	exit(1);
}

/*
 * DFUI Test Suite backend.
 */
int
main(int argc, char **argv)
{
	struct dfui_connection *c;
	int done = 0;
	int opt;
	char *rendezvous = NULL;
	int transport = 0;
#ifdef ENABLE_NLS
	const char *lang = "default";
	int cancelled = 0;
#endif

	/*
	 * XXX Get the transport from environment var, if available.
	 */

	/*
	 * Get command-line arguments.
	 */
	while ((opt = getopt(argc, argv, "r:t:")) != -1) {
		switch(opt) {
		case 'r':
			rendezvous = strdup(optarg);
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
		if (transport == DFUI_TRANSPORT_TCP)
			rendezvous = strdup("9999");
		else
			rendezvous = strdup("test");
	}

#ifdef ENABLE_NLS
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
#endif

	fprintf(stderr, _("Creating connection, transport = %d, rendezvous = %s.\n"),
		transport, rendezvous);

	if ((c = dfui_connection_new(transport, rendezvous)) == NULL) {
		free(rendezvous);
		err(1, "dfui_connection_new()");
	}

	fprintf(stderr, _("Waiting for connection from frontend.\n"));

	if (!dfui_be_start(c)) {
		free(rendezvous);
		err(1, "dfui_be_start()");
	}

	fprintf(stderr, _("Creating global data.\n"));

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "system_name", "typhoon");
	dfui_dataset_celldata_add(ds, "is_name_server", "N");
	dfui_dataset_celldata_add(ds, "logged_in_as", "uucp");
	dfui_dataset_celldata_add(ds, "password", "uucp");
	dfui_dataset_celldata_add(ds, "machine_role", _("Workstation"));

	strcpy(mountpoint[0], "/");
	strcpy(capacity[0], "200M");
	strcpy(mountpoint[1], "swap");
	strcpy(capacity[1], "256M");
	strcpy(mountpoint[2], "/usr");
	strcpy(capacity[2], "4.5G");
	rows = 3;

#ifdef ENABLE_NLS
	if (geteuid() != 0) {
		fprintf(stderr, _("User UID is not 0, leave `default' language.\n"));
	} else {
		lang = show_lang_menu(c);
	}

	if (lang != NULL) {
		dfui_be_set_global_setting(c, "lang", lang, &cancelled);

		/* XXX if (!cancelled) ... ? */

		/* let gettext know about changes */
		++_nl_msg_cat_cntr;
	}
#endif

	fprintf(stderr, _("Entering main menu loop.\n"));

	while (!done)
		done = show_main_menu(c);

	fprintf(stderr, _("Main menu loop finished, freeing global data.\n"));

	dfui_dataset_free(ds);
	
	fprintf(stderr, _("Disconnecting from frontend.\n"));

	dfui_be_stop(c);

	fprintf(stderr, _("Exiting.\n"));

	free(rendezvous);
	exit(0);
}
