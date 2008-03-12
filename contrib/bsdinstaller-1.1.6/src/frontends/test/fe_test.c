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
 * fe_test.c
 * $Id: fe_test.c,v 1.11 2005/04/05 20:39:22 cpressey Exp $
 * This code was derived in part from:
 * $_DragonFly: src/test/caps/server.c,v 1.4 2004/03/06 22:15:00 dillon Exp $
 * and is therefore also subject to the license conditions on that file.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "dfui/dfui.h"
#include "dfui/dump.h"

volatile sig_atomic_t caught_signal;

static void
signal_handler(int signo)
{
	caught_signal = signo;
}

#define BAR "----------------------------------------------------------------------"

static void
display_form(struct dfui_form *f)
{
	struct dfui_field *fi;
	struct dfui_action *a;

	printf("," BAR "\n");
	for (fi = dfui_form_field_get_first(f); fi != NULL;
	     fi = dfui_field_get_next(fi)) {
		printf("| %30s %c[ %-30s ]\n",
		    dfui_info_get_name(dfui_field_get_info(fi)),
		    dfui_field_property_is(fi, "control", "checkbox") ? '+' : ' ',
		    "(value)"
		);
	}
	printf("+" BAR "\n");
	printf("| Actions: ");
	for (a = dfui_form_action_get_first(f); a != NULL;
	     a = dfui_action_get_next(a)) {
		printf("[ %s ]  ",
		    dfui_info_get_name(dfui_action_get_info(a))
		);
	}
	printf("\n");
	printf("`" BAR "\n");
}

static void
present_form(struct dfui_form *f, char *action)
{
	int done = 0;
	struct dfui_action *a;
	
	display_form(f);
	while (!done) {
		printf("Select an action> ");
		fgets(action, 80, stdin);
		action[strlen(action) - 1] = '\0';
		if ((a = dfui_form_action_find(f, action)) != NULL) {
			done = 1;
		} else {
			printf("Invalid option.\n");
		}
	}
}

static void
show_progress(struct dfui_progress *pr)
{
	printf("  %d%c done...\n", dfui_progress_get_amount(pr), '%');
}

static void
abort_frontend(struct dfui_connection *c)
{
	printf("*** ABORTING.\n");
	dfui_fe_abort(c);
	exit(1);
}

/*
 * dfui frontend example test thing
 */
int
main(int argc __unused, char **argv __unused)
{
	struct dfui_connection *c;
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_progress *pr;
	struct dfui_property *gp;
	void *payload;
	char action[80];

	int done = 0;
	char msgtype;

	c = dfui_connection_new(DFUI_TRANSPORT_TCP, "9999");

	printf("Connecting to backend.\n");
	dfui_fe_connect(c);
	signal(SIGINT, signal_handler);

	while (!done) {
		dfui_fe_receive(c, &msgtype, &payload);
		switch (msgtype) {
		case DFUI_BE_MSG_PRESENT:
			printf("Got a form from the backend.\n");
			f = (struct dfui_form *)payload;
			present_form(f, action);
			if (caught_signal) abort_frontend(c);
			r = dfui_response_new(dfui_form_get_id(f), action);
			printf("Sending our response to the backend.\n");
			dfui_fe_submit(c, r);
			dfui_form_free(f);
			dfui_response_free(r);
			break;
		case DFUI_BE_MSG_PROG_BEGIN:
			printf("Got a progress bar begin from the backend.\n");
			pr = (struct dfui_progress *)payload;
			caught_signal = 0;
			show_progress(pr);
			dfui_fe_progress_continue(c);
			dfui_progress_free(pr);
			break;
		case DFUI_BE_MSG_PROG_UPDATE:
			printf("Got a progress bar update from the backend.\n");
			pr = (struct dfui_progress *)payload;
			show_progress(pr);
			dfui_progress_free(pr);
			if (caught_signal) {
				printf("* Cancelling ...\n");
				dfui_fe_progress_cancel(c);
				caught_signal = 0;
			} else {
				dfui_fe_progress_continue(c);
			}
			break;
		case DFUI_BE_MSG_PROG_END:
			printf("Got a progress bar end from the backend.\n");
			dfui_fe_progress_continue(c);
			break;
		case DFUI_BE_MSG_SET_GLOBAL:
			gp = (struct dfui_property *)payload;
			printf("Got a global setting change from the backend: %s = %s\n",
			    dfui_property_get_name(gp), dfui_property_get_value(gp));
			dfui_fe_confirm_set_global(c);
			dfui_property_free(gp);
			break;
		case DFUI_BE_MSG_STOP:
			printf("Got a STOP message from the backend.\n");
			dfui_fe_confirm_stop(c);
			done = 1;
			break;
		}
	}
	printf("Frontend done.\n");
	dfui_fe_disconnect(c);
	
	signal(SIGINT, SIG_DFL);

	exit(0);
}
