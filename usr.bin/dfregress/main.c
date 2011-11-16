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
	fprintf(stderr, "Usage: dfregress -o <output plist file> \n"
            "  -t <testcase directory>\n"
	    "  -p <pre/post command directory>\n"
	    "  -r <input runlist file>\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char runlist_file[PATH_MAX];
	char *p;
	int ch;

	while ((ch = getopt(argc, argv, "h?o:r:t:p:")) != -1) {
		switch (ch) {
		case 'o':
			if ((p = realpath(optarg, output_file)) == NULL)
				err(1, "realpath(%s)", optarg);
			break;

		case 't':
			if ((p = realpath(optarg, testcase_dir)) == NULL)
				err(1, "realpath(%s)", optarg);
			break;

		case 'r':
			if ((p = realpath(optarg, runlist_file)) == NULL)
				err(1, "realpath(%s)", optarg);
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

	prop_array_t runlist = runlist_load_from_text(runlist_file);
	runlist_iterate(runlist, runlist_run_test, runlist);

	return 0;
}
