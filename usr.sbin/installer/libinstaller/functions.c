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
 * functions.c
 * Generic functions for installer.
 * $Id: functions.c,v 1.22 2005/02/06 21:05:18 cpressey Exp $
 */

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libutil.h>

#include "libaura/mem.h"
#include "libaura/dict.h"

#include "libdfui/dfui.h"

#include "functions.h"
#include "diskutil.h"
#include "uiutil.h"

/*** INSTALLER CONTEXT CONSTRUCTOR ***/

struct i_fn_args *
i_fn_args_new(const char *os_root, const char *def_tmp_dir, int transport, const char *rendezvous)
{
	struct i_fn_args *a;
	char *filename;

	AURA_MALLOC(a, i_fn_args);

	a->c = NULL;
	a->os_root = aura_strdup(os_root);
	a->cfg_root = "";
	a->name = "";
	a->short_desc = "";
	a->long_desc = "";
	a->result = 0;
	a->log = NULL;
	a->s = NULL;
	a->tmp = NULL;
	a->temp_files = NULL;
	a->cmd_names = NULL;

	asprintf(&filename, "%sinstall.log", def_tmp_dir);
	a->log = fopen(filename, "w");
	free(filename);
	if (a->log == NULL) {
		i_fn_args_free(a);
		return(NULL);
	}

	i_log(a, "Installer started");
	i_log(a, "-----------------");

	i_log(a, "+ Creating DFUI connection on ``%s''\n", rendezvous);

	if ((a->c = dfui_connection_new(transport, rendezvous)) == NULL) {
		i_log(a, "! ERROR: Couldn't create connection on ``%s''\n", rendezvous);
		i_fn_args_free(a);
		return(NULL);
	}

	i_log(a, "+ Connecting on ``%s''\n", rendezvous);

	if (!dfui_be_start(a->c)) {
		i_log(a, "! ERROR: Couldn't connect to frontend on ``%s''\n", rendezvous);
		i_fn_args_free(a);
		return(NULL);
	}

	if ((a->s = storage_new()) == NULL) {
		i_log(a, "! ERROR: Couldn't create storage descriptor");
		i_fn_args_free(a);
		return(NULL);
	}

	a->tmp = def_tmp_dir;	/* XXX temporarily set to this */
	a->temp_files = aura_dict_new(23, AURA_DICT_HASH);
	a->cmd_names = config_vars_new();
	if (!config_vars_read(a, a->cmd_names, CONFIG_TYPE_SH,
	    "usr/share/installer/cmdnames.conf")) {
		i_log(a, "! ERROR: Couldn't read cmdnames config file");
		i_fn_args_free(a);
		return(NULL);
	}

	a->tmp = cmd_name(a, "INSTALLER_TEMP");

	i_log(a, "+ Starting installer state machine");

	return(a);
}

void
i_fn_args_free(struct i_fn_args *a)
{
	if (a != NULL) {
		if (a->temp_files != NULL) {
			temp_files_clean(a);
			aura_dict_free(a->temp_files);
		}
		if (a->cmd_names != NULL) {
			config_vars_free(a->cmd_names);
		}
		if (a->s != NULL) {
			storage_free(a->s);
		}
		if (a->c != NULL) {
			dfui_be_stop(a->c);
		}
		if (a->log != NULL) {
			fclose(a->log);
		}
		AURA_FREE(a, i_fn_args);
	}
}

/*** INSTALLER CONTEXT FUNCTIONS ***/

void
i_log(struct i_fn_args *a, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	if (a->log != NULL) {
		va_start(args, fmt);
		vfprintf(a->log, fmt, args);
		fprintf(a->log, "\n");
		fflush(a->log);
		va_end(args);
	}
	va_end(args);
}

/*** UTILITY ***/

void
abort_backend(void)
{
	exit(1);
}

int
assert_clean(struct dfui_connection *c, const char *name, const char *field,
	     const char *not_allowed)
{
	if (strpbrk(field, not_allowed) != NULL) {
		inform(c, "The %s field may not contain any of the "
		    "following characters:\n\n%s",
		    name, not_allowed);
		return(0);
	} else {
		return(1);
	}
}

/*
 * Expects a leading 0x.
 */
int
hex_to_int(const char *hex, int *result)
{
	int i, a = 0;
	char d;

	if (strncmp(hex, "0x", 2) != 0)
		return(0);

	for (i = 2; hex[i] != '\0'; i++) {
		d = toupper(hex[i]);
		if (isspace(d))
			continue;
		if (isdigit(d))
			a = a * 16 + (d - '0');
		else if (d >= 'A' && d <= 'F')
			a = a * 16 + (d + 10 - 'A');
		else
			return(0);
	}

	*result = a;
	return(1);
}

int
first_non_space_char_is(const char *line, char x)
{
	int i;

	for (i = 0; line[i] != '\0'; i++) {
		if (isspace(line[i]))
			continue;
		if (line[i] == x)
			return(1);
		return(0);
	}

	return(0);
}

const char *
capacity_to_string(long capacity)
{
	static char string[6];

	if (capacity < 0)
		strlcpy(string, "*", 2);
	else
		humanize_number(string, sizeof(string),
		    capacity * 1024 * 1024, "",
		    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);

	return(string);
}

int
string_to_capacity(const char *string, long *capacity)
{
	int error;
	int64_t result;

	if (!strcmp(string, "*")) {
		*capacity = -1;
		return(1);
	}
	error = dehumanize_number(string, &result);
	if (error != 0)
		return(0);
	result /= 1024 * 1024;
	if (result == 0)
		return(0);
	*capacity = result;
	return(1);
}

/*
 * Round a number up to the nearest power of two.
 */
unsigned long
next_power_of_two(unsigned long n)
{
	unsigned long p, op;

	p = 1;
	op = 0;
	while (p < n && p > op) {
		op = p;
		p <<= 1;
	}

	return(p > op ? p : n);
}

/*
 * Returns file name without extension.
 * e.g.
 *	ru.koi8-r.kbd -> ru.koi8-r
 *	README -> README
 *
 * Caller is responsible for freeing the string returned.
 */
char *
filename_noext(const char *filename)
{
	int i;
	char *buffer, *p;

	buffer = aura_strdup(filename);

	if (strlen(filename) == 0) {
		buffer[0] = '\0';
		return(buffer);
	}

	p = strrchr(filename, '.');

	if (p != NULL) {
		i = strlen(filename) - strlen(p);
		buffer[i] = 0;
	}

	return(buffer);
}

/*
 * Temp files
 */

int
temp_file_add(struct i_fn_args *a, const char *filename)
{
	aura_dict_store(a->temp_files, filename, strlen(filename) + 1, "", 1);
	return(1);
}

int
temp_files_clean(struct i_fn_args *a)
{
	void *rk;
	size_t rk_len;
	char *filename;

	aura_dict_rewind(a->temp_files);
	while (!aura_dict_eof(a->temp_files)) {
		aura_dict_get_current_key(a->temp_files, &rk, &rk_len);
		asprintf(&filename, "%s%s", a->tmp, (char *)rk);
		(void)unlink(filename);	/* not much we can do if it fails */
		free(filename);
		aura_dict_next(a->temp_files);
	}
	return(1);
}

/*
 * Command names
 */
const char *
cmd_name(const struct i_fn_args *a, const char *cmd_key)
{
	const char *name;

	name = config_var_get(a->cmd_names, cmd_key);
	if (strcmp(name, "") == 0)
		return("bin/echo");	/* XXX usr/local/sbin/error? */
	else
		return(name);
}
