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
#include <limits.h>
#include <fcntl.h>
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
#include "userland.h"
#include "kernel.h"
#include <dfregress.h>

char output_file[PATH_MAX+1];
char testcase_dir[PATH_MAX+1];
char prepost_dir[PATH_MAX+1];

prop_array_t
runlist_load_from_text(const char *runlist_file)
{
	prop_array_t runlist;

	runlist = prop_array_create_with_capacity(2048);

	process_file(runlist_file, testcase_entry_parser, runlist, NULL);

	return runlist;
}

prop_array_t
runlist_load(const char *runlist_file)
{
	return prop_array_internalize_from_file(runlist_file);
}

int
runlist_save(const char *runlist_file, prop_array_t runlist)
{
	return !prop_array_externalize_to_file(runlist, runlist_file);
}

int
runlist_iterate(prop_array_t runlist, runlist_iterator_t iterator, void *arg)
{
	prop_object_iterator_t it;
	prop_dictionary_t testcase;
	int r = 0;

	it = prop_array_iterator(runlist);
	if (it == NULL)
		err(1, "could not get runlist iterator");

	while ((testcase = prop_object_iterator_next(it)) != NULL) {
		r = iterator(arg, testcase);
		if (r != 0)
			break;
	}

	prop_object_iterator_release(it);
	return r;
}

int
runlist_run_test(void *arg, prop_dictionary_t testcase)
{
	prop_array_t runlist = (prop_array_t)arg;
	struct testcase_result tr;
	char testcase_path[FILENAME_MAX];
	char testcase_dir_only[FILENAME_MAX];
	char prepost_path[FILENAME_MAX];
	char errbuf[FILENAME_MAX*2];
	int r, nopre, nopost;
	char *str;

	sprintf(testcase_path, "%s/%s", testcase_dir,
	    testcase_get_name(testcase));
	strcpy(testcase_dir_only, testcase_path);
	str = strrchr(testcase_dir_only, '/');
	if (str != NULL)
		*str = '\0';

	printf("Running testcase %s... ", testcase_get_name(testcase));
	fflush(stdout);

	/* Switch to testcase directory */
	r = chdir(testcase_dir_only);
	if (r < 0) {
		sprintf(errbuf, "could not switch working directory to %s: %s\n",
		    testcase_dir_only, strerror(errno));
		testcase_set_result(testcase, RESULT_PREFAIL);
		testcase_set_sys_buf(testcase, errbuf);
		goto out;
	}

	/* build unless nobuild flag is set */
	if ((testcase_get_flags(testcase) & TESTCASE_NOBUILD) == 0) {
		r = run_simple_cmd(testcase_get_make_cmd(testcase), NULL,
		    errbuf, sizeof(errbuf), &tr);
		if (r != 0) {
			testcase_set_sys_buf(testcase, errbuf);
			testcase_set_result(testcase, RESULT_PREFAIL);
			goto out;
		}

		if (tr.stdout_buf != NULL) {
			testcase_set_build_buf(testcase, tr.stdout_buf);
			free(tr.stdout_buf);
		}

		if (tr.result != RESULT_PASS) {
			if (testcase_get_type(testcase)
			    == TESTCASE_TYPE_BUILDONLY)
				testcase_set_result(testcase, tr.result);
			else
				testcase_set_result(testcase, RESULT_BUILDFAIL);

			testcase_set_exit_value(testcase, tr.exit_value);
			testcase_set_signal(testcase, tr.signal);

			goto out;
		}
	}


	/* Pre-execution run */
	switch (testcase_get_precmd_type(testcase)) {
	case TESTCASE_INT_PRE:
		/* Test case has internal but explicit PRE - code */
		r = run_simple_cmd(testcase_path, "pre", errbuf,
		    sizeof(errbuf), &tr);
		nopre = 0;
		break;

	case TESTCASE_CUSTOM_PRE:
		/* Test case uses external and explicit PRE command */
		sprintf(prepost_path, "%s/%s", prepost_dir,
		    testcase_get_custom_precmd(testcase));

		r = run_simple_cmd(prepost_path, NULL, errbuf, sizeof(errbuf),
		    &tr);
		nopre = 0;
		break;

	default:
		nopre = 1;
		r = 0;
		break;
	}

	if (!nopre) {
		if (r != 0) {
			testcase_set_sys_buf(testcase, errbuf);
			testcase_set_result(testcase, RESULT_PREFAIL);
			goto out;
		}

		if (tr.stdout_buf != NULL) {
			testcase_set_precmd_buf(testcase, tr.stdout_buf);
			free(tr.stdout_buf);
		}

		if (tr.result != RESULT_PASS) {
			testcase_set_result(testcase, RESULT_PREFAIL);
			goto out;
		}
	}

	switch (testcase_get_type(testcase)) {
	case TESTCASE_TYPE_BUILDONLY:
		testcase_set_result(testcase, RESULT_PASS);
		testcase_set_exit_value(testcase, 0);
		break;

	case TESTCASE_TYPE_USERLAND:
		/* Main testcase execution */
		r = run_userland(testcase_path, testcase_get_args(testcase),
		    testcase_needs_setuid(testcase),
		    testcase_get_runas_uid(testcase),
		    testcase_get_timeout(testcase), 0, errbuf, sizeof(errbuf),
		    &tr);

		if (r == 0) {
			testcase_set_result(testcase, tr.result);
			testcase_set_exit_value(testcase, tr.exit_value);
			testcase_set_signal(testcase, tr.signal);

			if (tr.stdout_buf != NULL) {
				testcase_set_stdout_buf(testcase, tr.stdout_buf);
				free(tr.stdout_buf);
			}

			if (tr.stderr_buf != NULL) {
				testcase_set_stderr_buf(testcase, tr.stderr_buf);
				free(tr.stderr_buf);
			}
		} else {
			/* driver/monitor error */
			testcase_set_sys_buf(testcase, errbuf);
		}

		break;

	case TESTCASE_TYPE_KERNEL:
		run_kernel(testcase_path, testcase);
		break;
	}


	/* Post-execution run */
	switch (testcase_get_postcmd_type(testcase)) {
	case TESTCASE_INT_POST:
		/* Test case has internal but explicit POST - code */
		r = run_simple_cmd(testcase_path, "post", errbuf,
		    sizeof(errbuf), &tr);
		nopost = 0;
		break;

	case TESTCASE_CUSTOM_POST:
		/* Test case uses external and explicit POST command */
		sprintf(prepost_path, "%s/%s", prepost_dir,
		    testcase_get_custom_postcmd(testcase));

		r = run_simple_cmd(prepost_path, NULL, errbuf, sizeof(errbuf),
		    &tr);
		nopost = 0;
		break;

	default:
		r = 0;
		nopost = 1;
		break;
	}

	if (!nopost) {
		if (r != 0) {
			testcase_set_sys_buf(testcase, errbuf);
			testcase_set_result(testcase, RESULT_POSTFAIL);
			goto out;
		}

		if (tr.stdout_buf != NULL) {
			testcase_set_postcmd_buf(testcase, tr.stdout_buf);
			free(tr.stdout_buf);
		}

		if (tr.result != RESULT_PASS) {
			testcase_set_result(testcase, RESULT_POSTFAIL);
			goto out;
		}
	}



out:
	/* clean build unless nobuild flag is set */
	if ((testcase_get_flags(testcase) & TESTCASE_NOBUILD) == 0) {
		r = run_simple_cmd(testcase_get_make_cmd(testcase), "clean",
		    errbuf, sizeof(errbuf), &tr);

		if (tr.stdout_buf != NULL) {
			testcase_set_cleanup_buf(testcase, tr.stdout_buf);
			free(tr.stdout_buf);
		}

		if (r != 0)
			testcase_set_cleanup_buf(testcase, errbuf);
	}


	/* ... and save results */
	runlist_save(output_file, runlist);

	printf("done.\n");
	return 0;
}
