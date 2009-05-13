/*
 * Copyright (c)2004 The DragonFly Project.  All rights reserved.
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
 *   Neither the name of the DragonFly Project nor the names of its
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
 * uiutil.c
 * $Id: uiutil.c,v 1.9 2005/03/04 21:26:20 cpressey Exp $
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdfui/dfui.h"

#include "functions.h"
#include "uiutil.h"

void
inform(struct dfui_connection *c, const char *fmt, ...)
{
	struct dfui_form *f;
	struct dfui_response *r;
	va_list args;
	char *message;

	va_start(args, fmt);
	vasprintf(&message, fmt, args);
	va_end(args);

	f = dfui_form_create(
	    "inform",
	    "Information",
	    message,
	    "",

	    "p", "role", "informative",

	    "a", "ok", "OK", "", "",
	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	free(message);
	dfui_form_free(f);
	dfui_response_free(r);
}

int
confirm_dangerous_action(struct dfui_connection *c, const char *fmt, ...)
{
	struct dfui_form *f;
	struct dfui_response *r;
	va_list args;
	char *message;
	int result = 0;

	va_start(args, fmt);
	vasprintf(&message, fmt, args);
	va_end(args);

	f = dfui_form_create(
	    "confirm_dangerous_action",
	    "Are you absolutely sure?",
	    message,
	    "",

	    "p", "role", "alert",

	    "a", "ok", "OK", "", "",
	    "a", "cancel", "Cancel", "", "",
	    NULL
	);

	if (!dfui_be_present(c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0)
		result = 1;

	free(message);
	dfui_form_free(f);
	dfui_response_free(r);

	return(result);
}
