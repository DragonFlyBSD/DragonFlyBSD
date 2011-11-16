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

#define TESTCASE_TYPE_USERLAND	0x01
#define TESTCASE_TYPE_KERNEL	0x02
#define TESTCASE_TYPE_BUILDONLY	0x03

#define TESTCASE_INT_PRE	0x001
#define TESTCASE_INT_POST	0x002
#define TESTCASE_CUSTOM_PRE	0x004
#define TESTCASE_CUSTOM_POST	0x008
#define TESTCASE_NOBUILD	0x010
#define TESTCASE_RUN_AS		0x020


struct testcase_options {
	long int	timeout_in_secs;
	uint32_t	flags;
	uid_t		runas_uid;

	char		*pre_cmd;
	char		*post_cmd;
	char		*make_cmd;
};

struct testcase_result {
	int		result;
	int		exit_value;
	int		signal;
	int		core_dumped;

	char		*stdout_buf;
	char		*stderr_buf;

	struct rusage	rusage;
};

struct testcase {
	char		*name;
	char		**argv;
	int		argc;
	char		*type_str;
	int		type;

	struct testcase_options	opts;
	struct testcase_result	results;
};

prop_dictionary_t testcase_from_struct(struct testcase *testcase);

struct timeval *testcase_get_timeout(prop_dictionary_t testcase);
int testcase_get_type(prop_dictionary_t testcase);
const char *testcase_get_type_desc(prop_dictionary_t testcase);
const char *testcase_get_name(prop_dictionary_t testcase);
const char **testcase_get_args(prop_dictionary_t testcase);
uint32_t testcase_get_flags(prop_dictionary_t testcase);
int testcase_get_precmd_type(prop_dictionary_t testcase);
int testcase_get_postcmd_type(prop_dictionary_t testcase);
int testcase_needs_setuid(prop_dictionary_t testcase);
uid_t testcase_get_runas_uid(prop_dictionary_t testcase);
const char *testcase_get_custom_precmd(prop_dictionary_t testcase);
const char *testcase_get_custom_postcmd(prop_dictionary_t testcase);
const char *testcase_get_make_cmd(prop_dictionary_t testcase);
prop_dictionary_t testcase_get_result_dict(prop_dictionary_t testcase);
int testcase_set_build_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_cleanup_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_sys_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_precmd_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_postcmd_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_stdout_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_stderr_buf(prop_dictionary_t testcase, const char *buf);
int testcase_set_stdout_buf_from_file(prop_dictionary_t testcase, const char *file);
int testcase_set_stderr_buf_from_file(prop_dictionary_t testcase, const char *file);
int testcase_set_result(prop_dictionary_t testcase, int result);
int testcase_set_exit_value(prop_dictionary_t testcase, int exitval);
int testcase_set_signal(prop_dictionary_t testcase, int sig);
const char *testcase_get_build_buf(prop_dictionary_t testcase);
const char *testcase_get_cleanup_buf(prop_dictionary_t testcase);
const char *testcase_get_sys_buf(prop_dictionary_t testcase);
const char *testcase_get_precmd_buf(prop_dictionary_t testcase);
const char *testcase_get_postcmd_buf(prop_dictionary_t testcase);
const char *testcase_get_stdout_buf(prop_dictionary_t testcase);
const char *testcase_get_stderr_buf(prop_dictionary_t testcase);
int testcase_get_result(prop_dictionary_t testcase);
const char *testcase_get_result_desc(prop_dictionary_t testcase);
int testcase_get_exit_value(prop_dictionary_t testcase);
int testcase_get_signal(prop_dictionary_t testcase);

int parse_testcase_option(struct testcase_options *opts, char *option);
void testcase_entry_parser(void *arg, char **tokens);
