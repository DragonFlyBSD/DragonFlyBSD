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

#include "aura/mem.h"
#include "aura/buffer.h"

#include "dfui/dfui.h"

#include "installer/commands.h"
#include "installer/confed.h"
#include "installer/diskutil.h"
#include "installer/functions.h"
#include "installer/package.h"
#include "installer/uiutil.h"

#include "fn.h"
#include "pathnames.h"

/*** DIAGNOSTIC FUNCTIONS ***/

void
fn_memtest(struct i_fn_args *a)
{
	struct dfui_form *f;
	struct dfui_response *r;
	struct dfui_dataset *ds, *new_ds;
	struct commands *cmds;
	struct command *cmd;
	const char *memtestsize;

	f = dfui_form_create(
	    "memtest",
	    _("Memory test"),
	    _("Memory test - Enter the size in values such as 400M, 1G."),
	    "",

	    "f", "memtestsize", _("Memory test size"),
	    _("Enter the amount of memory you would like to check:"), "",

	    "a", "ok", _("OK"), "", "",
	    "a", "cancel", _("Cancel Memory Test"), "", "",
	    "p", "accelerator", "ESC",

	    NULL
	);

	ds = dfui_dataset_new();
	dfui_dataset_celldata_add(ds, "memtestsize", "");
	dfui_form_dataset_add(f, ds);

	if (!dfui_be_present(a->c, f, &r))
		abort_backend();

	if (strcmp(dfui_response_get_action_id(r), "ok") == 0) {
		new_ds = dfui_response_dataset_get_first(r);
		memtestsize = dfui_dataset_get_value(new_ds, "memtestsize");
		cmds = commands_new();
		cmd = command_add(cmds,
		    "cd %s && %s%s %s 1 --log",
		    a->tmp,
		    a->os_root, cmd_name(a, "MEMTEST"),
		    memtestsize);
		command_set_log_mode(cmd, COMMAND_LOG_QUIET);
		cmd = command_add(cmds,
		    "%s%s -E -v '^Unable to malloc' %smemtest.log > %smemtest.log.new",
		    a->os_root, cmd_name(a, "GREP"),
		    a->tmp, a->tmp);
		cmd = command_add(cmds, "%s%s %smemtest.log.new %smemtest.log",
		    a->os_root, cmd_name(a, "MV"),
		    a->tmp, a->tmp);
		cmd = command_add(cmds,
		    "%s%s -E -v '^Allocated.*failed' %smemtest.log > %smemtest.log.new",
		    a->os_root, cmd_name(a, "GREP"),
		    a->tmp, a->tmp);
		cmd = command_add(cmds, "%s%s %smemtest.log.new %smemtest.log",
		    a->os_root, cmd_name(a, "MV"),
		    a->tmp, a->tmp);
 		if (commands_execute(a, cmds)) {
			commands_free(cmds);
			view_memtest_log(a);
			cmds = commands_new();
			cmd = command_add(cmds, "%s%s -f %smemtest.log",
			    a->os_root, cmd_name(a, "RM"),
			    a->tmp);
			commands_execute(a, cmds);
		} else {
			inform(a->c, _("Memory test could not be run."));
		}
		commands_free(cmds);
	}

	dfui_form_free(f);
	dfui_response_free(r);
}

void
view_memtest_log(struct i_fn_args *a)
{
	struct aura_buffer *error_log;
	struct dfui_form *f;
	struct dfui_response *r;

	error_log = aura_buffer_new(1024);
	aura_buffer_cat_file(error_log, "%smemtest.log", a->tmp);

	f = dfui_form_create(
	    "error_log",
	    _("Error Log"),
	    aura_buffer_buf(error_log),
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

	aura_buffer_free(error_log);
}

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
fn_show_atacontrol(struct i_fn_args *a)
{
	struct aura_buffer *e;
	struct dfui_form *f;
	struct dfui_response *r;

	e = aura_buffer_new(1024);
	aura_buffer_cat_pipe(e, "atacontrol list");

	f = dfui_form_create(
	    "atacontrol",
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
	inform(c, aura_buffer_buf(e));
	aura_buffer_free(e);
}
