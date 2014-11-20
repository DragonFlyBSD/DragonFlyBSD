/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Chris Pressey <cpressey@catseye.mine.nu>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * test.c
 * Test some libinstaller functions.
 */

#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <sys/stat.h>

#define NEEDS_DFUI_STRUCTURE_DEFINITIONS
#include "libdfui/dfui.h"

#include "libinstaller/functions.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/commands.h"

#ifdef DEBUG
#include "libdfui/dump.h"
#endif
#include "libaura/fspred.h"

void test_storage(struct i_fn_args *);
void libinstaller_backend(struct i_fn_args **);
void libinstaller_frontend(struct dfui_connection **);
void libinstaller_form_dump(struct dfui_form *);

void (*tstate)(struct i_fn_args *) = NULL;

void
test_storage(struct i_fn_args *a)
{
	int r;

	r = survey_storage(a);
	printf("Return code: %d\n", r);
	tstate = NULL;
}

void
libinstaller_backend(struct i_fn_args **a)
{
	struct i_fn_args *ap = *a;

	ap = i_fn_args_new("/", "/tmp/installer/temp",
	    DFUI_TRANSPORT_TCP, "9999");

	tstate = test_storage;

	for (; tstate != NULL; ) {
		tstate(ap);
	}

	i_fn_args_free(ap);
}

void
libinstaller_frontend(struct dfui_connection **c) {
	struct dfui_connection *cp = *c;
	struct dfui_response *r;
	char msgtype;
	void *payload;
	int done = 0;

	usleep(100000);	/* Not really necessary */
	cp = dfui_connection_new(DFUI_TRANSPORT_TCP, "9999");
	dfui_fe_connect(cp);

	while (!done) {
		dfui_fe_receive(cp, &msgtype, &payload);
		switch (msgtype) {
		case DFUI_BE_MSG_PRESENT:
#ifdef DEBUG
			struct dfui_form *f;
			f = (struct dfui_form *)payload;
			dfui_form_dump(f);
#endif
			r = dfui_response_new("dialog", "Cancel");
			dfui_fe_submit(cp, r);
			dfui_response_free(r);
			sleep(1);
			break;
		case DFUI_BE_MSG_PROG_BEGIN:
		case DFUI_BE_MSG_PROG_UPDATE:
		case DFUI_BE_MSG_PROG_END:
			/* Details about the progress can go here */
			dfui_fe_progress_continue(cp);
			break;
		case DFUI_BE_MSG_STOP:
			dfui_fe_confirm_stop(cp);
			done = 1;
			break;
		default:
			printf("msgtype=%c\n", msgtype);
			sleep(1);
		}
	}
	dfui_fe_disconnect(cp);
}

int
main(int argc __unused, char **argv __unused)
{
        struct dfui_connection *c;
	struct i_fn_args *a;
	int r;

	if (!is_dir("/tmp/installer") ||
	    !is_dir("/tmp/installer/temp"))
		errx(1, "Please run 'mkdir -p /tmp/installer/temp'");


	r = fork();
	switch(r) {
	case -1:
		err(1, "Failed to fork");
		/* NOT REACHED */
	case 0:
		libinstaller_backend(&a);
		break; 		/* NOT REACHED */
	default:
		libinstaller_frontend(&c);
	}

	return(0);
}
