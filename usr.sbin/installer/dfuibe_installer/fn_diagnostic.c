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
 * fn_diagnostic.c
 * Diagnostic functions for installer.
 * $Id: fn_diagnostic.c,v 1.21 2005/03/13 01:53:58 cpressey Exp $
 */

#include <sys/types.h>

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

#include "libaura/mem.h"
#include "libaura/buffer.h"

#include "libdfui/dfui.h"

#include "libinstaller/commands.h"
#include "libinstaller/confed.h"
#include "libinstaller/diskutil.h"
#include "libinstaller/functions.h"
#include "libinstaller/package.h"
#include "libinstaller/uiutil.h"

#include "fn.h"
#include "pathnames.h"

/*** DIAGNOSTIC FUNCTIONS ***/

void
fn_show_dmesg(struct i_fn_args *a)
{
	struct aura_buffer *e;
	struct dfui_form *f;
	struct dfui_response *r;

	e = aura_buffer_new(1024);
	aura_buffer_cat_file(e, "%s%s", a->os_root, cmd_name(a, "DMESG_BOOT"));

	f = dfui_form_create(
	    "dmesg",
	    _("System Startup Messages (dmesg)"),
	    aura_buffer_buf(e),
	    "",

	    "p", "role", "informative",
	    "p", "minimum_width", "72",
	    "p", "monospaced", "true",

	    "a", "ok", _("OK"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	dfui_form_free(f);
	dfui_response_free(r);

	aura_buffer_free(e);
}

void
fn_show_pciconf(struct i_fn_args *a)
{
	struct aura_buffer *e;
	struct dfui_form *f;
	struct dfui_response *r;

	e = aura_buffer_new(1024);
	aura_buffer_cat_pipe(e, "pciconf -l -v");

	f = dfui_form_create(
	    "pciconf",
	    _("PCI Devices"),
	    aura_buffer_buf(e),
	    "",

	    "p", "role", "informative",
	    "p", "minimum_width", "72",
	    "p", "monospaced", "true",

	    "a", "ok", _("OK"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	dfui_form_free(f);
	dfui_response_free(r);

	aura_buffer_free(e);
}

void
fn_show_pnpinfo(struct i_fn_args *a)
{
	struct aura_buffer *e;
	struct dfui_form *f;
	struct dfui_response *r;

	e = aura_buffer_new(1024);
	aura_buffer_cat_pipe(e, "pnpinfo");

	f = dfui_form_create(
	    "pnpinfo",
	    _("ISA PnP Devices"),
	    aura_buffer_buf(e),
	    "",

	    "p", "role", "informative",
	    "p", "minimum_width", "72",
	    "p", "monospaced", "true",

	    "a", "ok", _("OK"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	dfui_form_free(f);
	dfui_response_free(r);

	aura_buffer_free(e);
}

void
fn_show_natacontrol(struct i_fn_args *a)
{
	struct aura_buffer *e;
	struct dfui_form *f;
	struct dfui_response *r;

	e = aura_buffer_new(1024);
	aura_buffer_cat_pipe(e, "natacontrol list");

	f = dfui_form_create(
	    "natacontrol",
	    _("ATA Devices"),
	    aura_buffer_buf(e),
	    "",

	    "p", "role", "informative",
	    "p", "minimum_width", "72",
	    "p", "monospaced", "true",

	    "a", "ok", _("OK"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	dfui_form_free(f);
	dfui_response_free(r);

	aura_buffer_free(e);
}

void
show_ifconfig(struct dfui_connection *c, char *ifname)
{
	struct aura_buffer *e;

	e = aura_buffer_new(1024);
	aura_buffer_cat_pipe(e, "/sbin/ifconfig %s", ifname);
	inform(c, "%s", aura_buffer_buf(e));
	aura_buffer_free(e);
}
