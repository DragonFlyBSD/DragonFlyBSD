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
#include <dfregress.h>


static void
usage(void)
{
	fprintf(stderr, "Usage: dfregress [options] <input runlist>\n"
	    "Valid options are:\n"
	    "  -o <output results plist file>\n"
            "  -t <testcase directory>\n"
	    "  -p <pre/post command directory>\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char runlist_file[PATH_MAX];
	char *p, *s;
	int oflag = 0;
	int tflag = 0;
	int ch;

	while ((ch = getopt(argc, argv, "h?o:t:p:")) != -1) {
		switch (ch) {
		case 'o':
			if ((p = realpath(optarg, output_file)) == NULL)
				err(1, "realpath(%s)", optarg);
			oflag = 1;
			break;

		case 't':
			if ((p = realpath(optarg, testcase_dir)) == NULL)
				err(1, "realpath(%s)", optarg);
			tflag = 1;
			break;

		case 'p':
			if ((p = realpath(optarg, prepost_dir)) == NULL)
				err(1, "realpath(%s)", optarg);
			break;

		case '?':
		case 'h':
			usage();
			/* NOTREACHED */
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();
		/* NOTREACHED */

	if ((p = realpath(argv[0], runlist_file)) == NULL)
		err(1, "realpath(%s)", argv[0]);

	if (!tflag) {
		/*
		 * No explicit testcase directory:
		 * Use default testcase directory - the directory where the
		 * runlist is.
		 */
		strcpy(testcase_dir, runlist_file);
		p = strrchr(testcase_dir, '/');
		if (p == NULL) {
			fprintf(stderr, "Unknown error while determining "
			    "default testcase directory. testcase_dir = %s\n",
			    testcase_dir);
			exit(1);
			/* NOTREACHED */
		}

		*p = '\0';
	}

	if (!oflag) {
		/*
		 * No explicit output file:
		 * By default we'll take the basename of the runlist file
		 * and append .plist to it in the cwd; i.e.:
		 * /foo/bar/baz.run -> ./baz.plist
		 */
		p = strrchr(runlist_file, '/');
		if (p == NULL) {
			fprintf(stderr, "Unknown error while determining "
			    "default output file. runlist_file = %s\n",
			    runlist_file);
			exit(1);
			/* NOTREACHED */
		}

		++p;

		s = getcwd(output_file, PATH_MAX);
		if (s == NULL)
			err(1, "getcwd()");
			/* NOTREACHED */

		strcat(output_file, "/");
		strcat(output_file, p);

		if ((p = strrchr(output_file, '.')) != NULL)
			*p = '\0';


		strcat(output_file, ".plist");
	}

	printf("Output plist results:\t%s\n", output_file);
	printf("Runlist:\t\t%s\n", runlist_file);
	printf("Testcase directory:\t%s\n", testcase_dir);
	printf("\n");

	prop_array_t runlist = runlist_load_from_text(runlist_file);
	runlist_iterate(runlist, runlist_run_test, runlist);

	return 0;
}
