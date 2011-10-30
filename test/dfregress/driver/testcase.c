/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include <err.h>

#include <libprop/proplib.h>

#include "parser.h"
#include "testcase.h"
#include "runlist.h"
#include "config.h"
#include "../framework/dfregress.h"

prop_dictionary_t
testcase_from_struct(struct testcase *testcase)
{
	int i, r;
	prop_dictionary_t dict, testcase_dict;
	prop_array_t a;
	char *s;

	testcase_dict = prop_dictionary_create();
	if (testcase_dict == NULL)
		err(1, "could not create testcase dict");
	r = prop_dictionary_set_cstring(testcase_dict, "name", testcase->name);
	if (r == 0)
		err(1, "prop_dictionary operation failed");
	r = prop_dictionary_set_cstring(testcase_dict, "type", testcase->type_str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	a = prop_array_create_with_capacity(testcase->argc+1);
	if (a == NULL)
		err(1, "prop_array_create for argv failed");

	s = strrchr(testcase->name, '/');
	r = prop_array_set_cstring(a, 0, (s == NULL) ? testcase->name : s+1);
	if (r == 0)
		err(1, "prop_array_set_cstring operation failed");

	for (i = 1; i <= testcase->argc; i++) {
		r = prop_array_set_cstring(a, i, testcase->argv[i-1]);
		if (r == 0)
			err(1, "prop_array_set_cstring operation failed");
	}

	r = prop_dictionary_set(testcase_dict, "args", a);
	if (r == 0)
		err(1, "prop_dictionary_set \"args\" failed");

	dict = prop_dictionary_create();
	if (dict == NULL)
		err(1, "could not create dict");

	r = prop_dictionary_set_int32(dict, "timeout_in_secs",
	    (int32_t)testcase->opts.timeout_in_secs);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	r = prop_dictionary_set_uint32(dict, "flags", testcase->opts.flags);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	if (testcase->opts.pre_cmd != NULL) {
		r = prop_dictionary_set_cstring(dict, "pre_cmd",
		    testcase->opts.pre_cmd);
		if (r == 0)
			err(1, "prop_dictionary operation failed");
	}

	if (testcase->opts.post_cmd != NULL) {
		r = prop_dictionary_set_cstring(dict, "post_cmd",
		    testcase->opts.post_cmd);
		if (r == 0)
			err(1, "prop_dictionary operation failed");
	}

	r = prop_dictionary_set_uint32(dict, "runas_uid",
	    (uint32_t)testcase->opts.runas_uid);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	r = prop_dictionary_set_cstring(dict, "make_cmd",
	    (testcase->opts.make_cmd != NULL) ? testcase->opts.make_cmd : "make");
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	r = prop_dictionary_set(testcase_dict, "opts", dict);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return testcase_dict;
}

struct timeval *
testcase_get_timeout(prop_dictionary_t testcase)
{
	static struct timeval tv;
	int32_t val;
	int r;

	r = prop_dictionary_get_int32(prop_dictionary_get(testcase, "opts"),
	    "timeout_in_secs", &val);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	tv.tv_usec = 0;
	tv.tv_sec = (long)val;

	return &tv;
}

int
testcase_get_type(prop_dictionary_t testcase)
{
	const char *type;
	int r;

	r = prop_dictionary_get_cstring_nocopy(testcase, "type", &type);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	if (strcmp(type, "userland") == 0)
		return TESTCASE_TYPE_USERLAND;
	else if (strcmp(type, "kernel") == 0)
		return TESTCASE_TYPE_KERNEL;
	else if (strcmp(type, "buildonly") == 0)
		return TESTCASE_TYPE_BUILDONLY;

	return 0;
}

const char *
testcase_get_type_desc(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(testcase, "type", &str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");
	
	return str;
}

const char *
testcase_get_name(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(testcase, "name", &str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return str;
}

const char **
testcase_get_args(prop_dictionary_t testcase)
{
	/* Sane limit of 63 arguments... who wants more than that? */
	static const char *argv[64];
	unsigned int i, count;
	prop_array_t a;
	int r;

	a = prop_dictionary_get(testcase, "args");
	if (a == NULL)
		err(1, "testcase_get_args NULL array");

	count = prop_array_count(a);

	for (i = 0; i < count; i++) {
		r = prop_array_get_cstring_nocopy(a, i, &argv[i]);
		if (r == 0)
			err(1, "error building argv");
	}

	argv[i] = NULL;

	return argv;
}

uint32_t
testcase_get_flags(prop_dictionary_t testcase)
{
	uint32_t flags;
	int r;

	r = prop_dictionary_get_uint32(prop_dictionary_get(testcase, "opts"),
	    "flags", &flags);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return flags;
}

int
testcase_get_precmd_type(prop_dictionary_t testcase)
{
	uint32_t flags = testcase_get_flags(testcase);

	return (flags & (TESTCASE_INT_PRE | TESTCASE_CUSTOM_PRE));
}

int
testcase_get_postcmd_type(prop_dictionary_t testcase)
{
	uint32_t flags = testcase_get_flags(testcase);

	return (flags & (TESTCASE_INT_POST | TESTCASE_CUSTOM_POST));
}

int
testcase_needs_setuid(prop_dictionary_t testcase)
{
	uint32_t flags = testcase_get_flags(testcase);

	return (flags & TESTCASE_RUN_AS);
}

uid_t
testcase_get_runas_uid(prop_dictionary_t testcase)
{
	uint32_t uid = 0;
	int r;

	r = prop_dictionary_get_uint32(
	    prop_dictionary_get(testcase, "opts"), "runas_uid", &uid);

	return (uid_t)uid;
}

const char *
testcase_get_custom_precmd(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(
	    prop_dictionary_get(testcase, "opts"), "pre_cmd", &str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return str;
}

const char *
testcase_get_custom_postcmd(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(
	    prop_dictionary_get(testcase, "opts"), "pre_cmd", &str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return str;
}

const char *
testcase_get_make_cmd(prop_dictionary_t testcase)
{
	const char *str;
	int r;

	r = prop_dictionary_get_cstring_nocopy(
	    prop_dictionary_get(testcase, "opts"), "make_cmd", &str);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return str;
}

prop_dictionary_t
testcase_get_result_dict(prop_dictionary_t testcase)
{
	prop_dictionary_t result_dict;
	int r;

	result_dict = prop_dictionary_get(testcase, "result");
	if (result_dict == NULL) {
		result_dict = prop_dictionary_create();
		if (result_dict == NULL)
			err(1, "could not allocate new result dict");

		r = prop_dictionary_set(testcase, "result", result_dict);
		if (r == 0)
			err(1, "prop_dictionary operation failed");
	}

	return result_dict;
}

int
testcase_set_build_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "build_buf", buf);
}

int
testcase_set_cleanup_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "cleanup_buf", buf);
}

int
testcase_set_sys_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "sys_buf", buf);
}

int
testcase_set_precmd_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "precmd_buf", buf);
}

int
testcase_set_postcmd_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "postcmd_buf", buf);
}

int
testcase_set_stdout_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "stdout_buf", buf);
}

int
testcase_set_stderr_buf(prop_dictionary_t testcase, const char *buf)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_cstring(dict, "stderr_buf", buf);
}

int
testcase_set_result(prop_dictionary_t testcase, int result)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_int32(dict, "result", result);
}

int
testcase_set_exit_value(prop_dictionary_t testcase, int exitval)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_int32(dict, "exit_value", exitval);
}

int
testcase_set_signal(prop_dictionary_t testcase, int sig)
{
	prop_dictionary_t dict = testcase_get_result_dict(testcase);

	return !prop_dictionary_set_int32(dict, "signal", sig);
}

const char *
testcase_get_build_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "build_buf", &str);

	return str;
}

const char *
testcase_get_cleanup_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "cleanup_buf", &str);

	return str;
}

const char *
testcase_get_sys_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "sys_buf", &str);

	return str;
}

const char *
testcase_get_precmd_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "precmd_buf", &str);

	return str;
}

const char *
testcase_get_postcmd_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "postcmd_buf", &str);

	return str;
}

const char *
testcase_get_stdout_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "stdout_buf", &str);

	return str;
}

const char *
testcase_get_stderr_buf(prop_dictionary_t testcase)
{
	const char *str = "";

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_cstring_nocopy(dict, "stderr_buf", &str);

	return str;
}

int
testcase_get_result(prop_dictionary_t testcase)
{
	int32_t result = RESULT_NOTRUN;

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	prop_dictionary_get_int32(dict, "result", &result);

	return (int)result;
}

const char *
testcase_get_result_desc(prop_dictionary_t testcase)
{
	int result = testcase_get_result(testcase);

	switch(result) {
	case RESULT_TIMEOUT:	return "TIMEOUT";
	case RESULT_SIGNALLED:	return "SIGNALLED";
	case RESULT_NOTRUN:	return "NOT RUN";
	case RESULT_FAIL:	return "FAIL";
	case RESULT_PASS:	return "PASS";
	case RESULT_PREFAIL:	return "PREFAIL";
	case RESULT_POSTFAIL:	return "POSTFAIL";
	case RESULT_BUILDFAIL:	return "BUILDFAIL";
	default:		return "UNKNOWN";
	}
}

int
testcase_get_exit_value(prop_dictionary_t testcase)
{
	int32_t exitval;
	int r;

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	r = prop_dictionary_get_int32(dict, "exit_value", &exitval);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return (int)exitval;
}

int
testcase_get_signal(prop_dictionary_t testcase)
{
	int32_t sig;
	int r;

	prop_dictionary_t dict = testcase_get_result_dict(testcase);
	r = prop_dictionary_get_int32(dict, "signal", &sig);
	if (r == 0)
		err(1, "prop_dictionary operation failed");

	return (int)sig;
}

int
parse_testcase_option(struct testcase_options *opts, char *option)
{
	struct passwd *pwd;
	char	*parameter, *endptr;
	long	lval;
	int	noparam = 0;

	parameter = strchr(option, '=');
	noparam = (parameter == NULL);
	if (!noparam)
	{
		*parameter = '\0';
		++parameter;
	}

	if (strcmp(option, "timeout") == 0) {
		if (noparam)
			syntax_error("The option 'timeout' needs a parameter");
			/* NOTREACHED */

		lval = strtol(parameter, &endptr, 10);
		if (*endptr != '\0')
			syntax_error("The option 'timeout' expects an integer "
			    "parameter, not '%s'", parameter);
			/* NOTREACHED */

		opts->timeout_in_secs = (long int)lval;
	} else if (strcmp(option, "intpre") == 0) {
		opts->flags |= TESTCASE_INT_PRE;
	} else if (strcmp(option, "intpost") == 0) {
		opts->flags |= TESTCASE_INT_POST;
	} else if (strcmp(option, "pre") == 0) {
		if (noparam)
			syntax_error("The option 'pre' needs a parameter");
			/* NOTREACHED */

		opts->flags |= TESTCASE_CUSTOM_PRE;
		opts->pre_cmd = strdup(parameter);
	} else if (strcmp(option, "post") == 0) {
		if (noparam)
			syntax_error("The option 'post' needs a parameter");
			/* NOTREACHED */

		opts->flags |= TESTCASE_CUSTOM_POST;
		opts->post_cmd = strdup(parameter);
	} else if (strcmp(option, "runas") == 0) {
		if (noparam)
			syntax_error("The option 'runas' needs a parameter");
			/* NOTREACHED */

		if ((pwd = getpwnam(parameter))) {
			opts->runas_uid = pwd->pw_uid;
			opts->flags |= TESTCASE_RUN_AS;
		} else {
			syntax_error("invalid user name for 'runas': %s",
			    parameter);
		}
	} else if (strcmp(option, "nobuild") == 0) {
		opts->flags |= TESTCASE_NOBUILD;
	} else if (strcmp(option, "make") == 0) {
		if (noparam)
			syntax_error("The option 'make' needs a parameter");
			/* NOTREACHED */

		opts->make_cmd = strdup(parameter);
	} else if (strcmp(option, "defaults") == 0) {
		/* Valid option, does nothing */
	} else {
		syntax_error("Unknown option: %s", option);
		/* NOTREACHED */
	}

	return 0;
}

void
testcase_entry_parser(void *arg, char **tokens)
{
	prop_array_t runlist;
	prop_dictionary_t testcase_dict;
	struct testcase *testcase;
	char *options[256];
	int i, r, nopts;

	runlist = (prop_array_t)arg;

	testcase = malloc(sizeof(struct testcase));
	if (testcase == NULL)
		err(1, "could not malloc testcase memory");

	bzero(testcase, sizeof(struct testcase));

	entry_check_num_args(tokens, 3);

	testcase->argv = &tokens[3];
	for (testcase->argc = 0; testcase->argv[testcase->argc] != NULL;
	     testcase->argc++)
		;

	nopts = parse_options(tokens[2], options);

	testcase->name = tokens[0];

	if (strcmp(tokens[1], "userland") == 0) {
		testcase->type = TESTCASE_TYPE_USERLAND;
	} else if (strcmp(tokens[1], "kernel") == 0) {
		testcase->type = TESTCASE_TYPE_KERNEL;
	} else if (strcmp(tokens[1], "buildonly") == 0) {
		testcase->type = TESTCASE_TYPE_BUILDONLY;
	} else {
		syntax_error("Unknown type: %s", tokens[1]);
		/* NOTREACHED */
	}

	testcase->type_str = tokens[1];

	config_get_defaults(&testcase->opts);

	for (i = 0; i < nopts; i++)
		parse_testcase_option(&testcase->opts, options[i]);

	if ((testcase->type != TESTCASE_TYPE_USERLAND) &&
	    (testcase->opts.flags & (TESTCASE_INT_PRE | TESTCASE_INT_POST)))
		syntax_error("'intpre' and 'intpost' options are only valid "
		    "with testcase type 'userland'");

	if ((testcase->type == TESTCASE_TYPE_BUILDONLY) &&
	    (testcase->opts.flags & TESTCASE_NOBUILD))
		syntax_error("'nobuild' option is incompatible with type "
		    "'buildonly'");

	testcase_dict = testcase_from_struct(testcase);
	if (testcase->opts.pre_cmd != NULL)
		free(testcase->opts.pre_cmd);
	if (testcase->opts.post_cmd != NULL)
		free(testcase->opts.post_cmd);
	if (testcase->opts.make_cmd != NULL)
		free(testcase->opts.make_cmd);
	free(testcase);

	r = prop_array_add(runlist, testcase_dict);
	if (r == 0)
		err(1, "prop_array_add failed");
}
