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
 * confed.c
 * Functions for working with configuration files.
 * Inspired by (but not derived from) sysinstall's variable.c
 * $Id: confed.c,v 1.16 2005/02/06 21:05:18 cpressey Exp $
 */

#include <sys/stat.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libaura/mem.h"
#include "libaura/dict.h"
#include "libdfui/system.h"

#include "confed.h"
#include "commands.h"
#include "functions.h"

/*
 * Create a new, empty set of in-memory config variable settings.
 */
struct config_vars *
config_vars_new(void)
{
	struct config_vars *cvs;

	AURA_MALLOC(cvs, config_vars);

	cvs->d = aura_dict_new(1, AURA_DICT_SORTED_LIST);

	return(cvs);
}

/*
 * Deallocate the memory used by a set of config variable settings.
 */
void
config_vars_free(struct config_vars *cvs)
{
	if (cvs == NULL)
		return;

	aura_dict_free(cvs->d);

	AURA_FREE(cvs, config_vars);
}

/*
 * Get the value of a configuration variable in a set of settings
 * and return it, or a (constant) 0-length string if not found.
 */
const char *
config_var_get(const struct config_vars *cvs, const char *name)
{
	void *rv;
	size_t rv_len;

	aura_dict_fetch(cvs->d, name, strlen(name) + 1, &rv, &rv_len);
	if (rv == NULL)
		return("");
	else
		return(rv);
}

/*
 * Set the value of a configuration variable.  If the named variable
 * does not exist within the given set, a new one is created.
 */
int
config_var_set(struct config_vars *cvs, const char *name, const char *value)
{
	aura_dict_store(cvs->d,
	    name, strlen(name) + 1,
	    value, strlen(value) + 1);
	return(1);
}

/*
 * Write a set of configuration variable settings to a file.
 */
int
config_vars_write(const struct config_vars *cvs, int config_type,
    const char *fmt, ...)
{
	FILE *f;
	va_list args;
	char *filename;
	void *rk, *rv;
	size_t rk_len, rv_len;

	va_start(args, fmt);
	vasprintf(&filename, fmt, args);
	va_end(args);

	if ((f = fopen(filename, "a")) == NULL)
		return(0);

	switch (config_type) {
	case CONFIG_TYPE_SH:

		aura_dict_rewind(cvs->d);
		while (!aura_dict_eof(cvs->d)) {
			aura_dict_get_current_key(cvs->d, &rk, &rk_len),
			aura_dict_fetch(cvs->d, rk, rk_len, &rv, &rv_len);
			fprintf(f, "%s=\"%s\"\t# via installer configuration\n",
			    (char *)rk, (char *)rv);
			aura_dict_next(cvs->d);
		}
		break;
	case CONFIG_TYPE_RESOLV:
		aura_dict_rewind(cvs->d);
		while (!aura_dict_eof(cvs->d)) {
			aura_dict_get_current_key(cvs->d, &rk, &rk_len),
			aura_dict_fetch(cvs->d, rk, rk_len, &rv, &rv_len);
			fprintf(f, "%s\t\t%s\n", (char *)rk, (char *)rv);
			aura_dict_next(cvs->d);
		}
		break;
	default:
		fclose(f);
		return(0);
	}

	fclose(f);
	return(1);
}

/*
 * Read variables from a file.
 * Returns 1 if the variables could be read successfully, 0 if not.
 */
int
config_vars_read(struct i_fn_args *a, struct config_vars *cvs,
		 int config_type __unused, const char *fmt, ...)
{
	struct commands *cmds;
	char *filename, *tmp_filename, line[1024], *value;
	FILE *f, *script;
	va_list args;

	va_start(args, fmt);
	vasprintf(&filename, fmt, args);
	va_end(args);

	asprintf(&tmp_filename, "%sextract_vars", a->tmp);
	script = fopen(tmp_filename, "w");
	free(tmp_filename);
	if (script == NULL)
		return(0);

	fprintf(script, "set | %susr/bin/sort >%senv.before\n", a->os_root, a->tmp);
	fprintf(script, ". %s%s\n", a->os_root, filename);
	fprintf(script, "set | %susr/bin/sort >%senv.after\n", a->os_root, a->tmp);
	fprintf(script, "%susr/bin/comm -1 -3 %senv.before %senv.after | \\\n",
	    a->os_root, a->tmp, a->tmp);
	fprintf(script, "    %susr/bin/awk -F= '{ print $1 }' | \\\n", a->os_root);
	fprintf(script, "    while read __VARNAME; do\n");
	fprintf(script, "        echo -n ${__VARNAME}=\n");
	fprintf(script, "        eval echo ' $'${__VARNAME}\n");
	fprintf(script, "    done\n");
	fprintf(script, "%sbin/rm -f %senv.before %senv.after\n",
	    a->os_root, a->tmp, a->tmp);
	fclose(script);

	cmds = commands_new();
	command_add(cmds, "%sbin/sh %sextract_vars >%sextracted_vars.txt",
	    a->os_root, a->tmp, a->tmp);
	temp_file_add(a, "extracted_vars.txt");
	if (!commands_execute(a, cmds)) {
		commands_free(cmds);
		return(0);
	}
	commands_free(cmds);

	/*
	 * Delete the script immediately.
	 */
	asprintf(&tmp_filename, "%sextract_vars", a->tmp);
	(void)unlink(tmp_filename);	/* not much we can do if it fails */
	free(tmp_filename);

	asprintf(&tmp_filename, "%sextracted_vars.txt", a->tmp);
	f = fopen(tmp_filename, "r");
	free(tmp_filename);
	if (f == NULL)
		return(0);
	while (fgets(line, 1024, f) != NULL) {
		if (strlen(line) > 0)
			line[strlen(line) - 1] = '\0';
		/* split line at first = */
		for (value = line; *value != '=' && *value != '\0'; value++)
			;
		if (*value == '\0')
			break;
		*value = '\0';
		value++;
		config_var_set(cvs, line, value);
	}
	fclose(f);

	return(1);
}
