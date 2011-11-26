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

#include <err.h>

#include <libprop/proplib.h>

#include "parser.h"
#include "testcase.h"
#include "runlist.h"
#include <dfregress.h>

static int count_by_result[10];

static void
print_row(char c)
{
	int i;

	for (i = 0; i < 80; i++)
		printf("%c", c);
	printf("\n");
}

static void
print_header(void)
{
	int i, j;

	i = printf("Test case");
	for (j = 60 - i; j > 0; j--)
		printf(" ");
	printf("Result\n");

	print_row('-');
}

static char *get_arg_string(prop_dictionary_t testcase)
{
	static char buf[2048];
	const char **argv;
	int i;

	buf[0] = '\0';

	argv = testcase_get_args(testcase);

	/* Skip the first argument, since it's just the test's name */
	for (i = 1; argv[i] != NULL; i++) {
		strcat(buf, argv[i]);
		if (argv[i+1] != NULL)
			strcat(buf, " ");
	}

	return (i > 1) ? buf : NULL;
}

static void
print_summary(prop_array_t runlist)
{
	float total_run;
	float total_tests;

	total_tests = 0.0 + prop_array_count(runlist);
	total_run = 0.0 + total_tests - count_by_result[RESULT_BUILDFAIL] -
	    count_by_result[RESULT_PREFAIL] - count_by_result[RESULT_NOTRUN] -
	    count_by_result[RESULT_UNKNOWN];

	printf("\n\n");
	print_row('=');
	printf("Summary:\n\n");


	printf("Tests not built:\t%d\n", count_by_result[RESULT_BUILDFAIL]);

	printf("Tests not run:\t\t%.0f\n", total_tests - total_run);

	printf("Tests pre-failed:\t%d\n", count_by_result[RESULT_PREFAIL]);

	printf("Tests post-failed:\t%d\n", count_by_result[RESULT_POSTFAIL]);

	printf("Tests passed:\t\t%d\n", count_by_result[RESULT_PASS]);

	printf("Tests failed:\t\t%d\n", count_by_result[RESULT_FAIL]);

	printf("Tests crashed:\t\t%d\n", count_by_result[RESULT_SIGNALLED]);

	printf("Tests timed out:\t%d\n", count_by_result[RESULT_TIMEOUT]);


	printf("------\n");

	printf("Run rate:\t\t%.2f\n", total_run/total_tests);
	printf("Pass rate:\t\t%.2f\n", count_by_result[RESULT_PASS]/total_run);
}

static int
runlist_parse_summary(void *arg __unused, prop_dictionary_t testcase)
{
	char *args;
	int i, j;

	++count_by_result[testcase_get_result(testcase)];
	args = get_arg_string(testcase);

	i = printf("%s", testcase_get_name(testcase));
	if (args != NULL)
		i+= printf(" (%s)", args);

	for (j = 60 - i; j > 0; j--)
		printf(" ");

	printf("%s\n", testcase_get_result_desc(testcase));

	return 0;
}

static int
runlist_parse_detail(void *arg __unused, prop_dictionary_t testcase)
{
	char *args;

	args = get_arg_string(testcase);

	printf("\n");
	print_row('=');

	printf("Test: %s\n", testcase_get_name(testcase));
	if (args != NULL)
		printf("Command line arguments: %s\n", args);

	printf("Type: %s\n", testcase_get_type_desc(testcase));
	printf("Result: %s\n", testcase_get_result_desc(testcase));

	switch (testcase_get_result(testcase)) {
	case RESULT_FAIL:
		printf("Exit code: %d\n", testcase_get_exit_value(testcase));
		break;

	case RESULT_SIGNALLED:
		printf("Signal: %d\n", testcase_get_signal(testcase));
		break;
	};

	print_row('-');
	printf("driver sysbuf:\n%s\n", testcase_get_sys_buf(testcase));
	print_row('-');
	printf("build log:\n%s\n", testcase_get_build_buf(testcase));
	print_row('-');
	printf("'pre' log:\n%s\n", testcase_get_precmd_buf(testcase));
	print_row('-');
	printf("testcase stdout:\n%s\n", testcase_get_stdout_buf(testcase));
	print_row('-');
	printf("testcase stderr:\n%s\n", testcase_get_stderr_buf(testcase));
	print_row('-');
	printf("'post' log:\n%s\n", testcase_get_postcmd_buf(testcase));
	print_row('-');
	printf("cleanup log:\n%s\n", testcase_get_cleanup_buf(testcase));

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: dfr2text <foo.plist>\n");
		exit(1);
	}

	prop_array_t runlist = runlist_load(argv[1]);

	memset(count_by_result, 0, sizeof(count_by_result));
	print_header();
	runlist_iterate(runlist, runlist_parse_summary, runlist);
	print_summary(runlist);
	printf("\n\nDETAILED RESULTS:\n");

	runlist_iterate(runlist, runlist_parse_detail, runlist);
	return 0;
}

