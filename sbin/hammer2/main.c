/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
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

#include "hammer2.h"

static void usage(int code);

int DebugOpt;
int NormalExit = 1;	/* if set to 0 main() has to pthread_exit() */

int
main(int ac, char **av)
{
	const char *sel_path = NULL;
	const char *uuid_str = NULL;
	int pfs_type = HAMMER2_PFSTYPE_NONE;
	int quick_opt = 0;
	int all_opt = 0;
	int ecode = 0;
	int ch;

	/*
	 * Core options
	 */
	while ((ch = getopt(ac, av, "aqs:t:u:")) != -1) {
		switch(ch) {
		case 'a':
			all_opt = 1;
			break;
		case 'q':
			/*
			 * Quick mode - do not block verifying certain
			 * operations such as (connect).
			 */
			quick_opt = 1;
			break;
		case 's':
			sel_path = optarg;
			break;
		case 't':
			/*
			 * set node type for mkpfs
			 */
			if (strcasecmp(optarg, "ADMIN") == 0) {
				pfs_type = HAMMER2_PFSTYPE_ADMIN;
			} else if (strcasecmp(optarg, "CACHE") == 0) {
				pfs_type = HAMMER2_PFSTYPE_CACHE;
			} else if (strcasecmp(optarg, "COPY") == 0) {
				pfs_type = HAMMER2_PFSTYPE_COPY;
			} else if (strcasecmp(optarg, "SLAVE") == 0) {
				pfs_type = HAMMER2_PFSTYPE_SLAVE;
			} else if (strcasecmp(optarg, "SOFT_SLAVE") == 0) {
				pfs_type = HAMMER2_PFSTYPE_SOFT_SLAVE;
			} else if (strcasecmp(optarg, "SOFT_MASTER") == 0) {
				pfs_type = HAMMER2_PFSTYPE_SOFT_MASTER;
			} else if (strcasecmp(optarg, "MASTER") == 0) {
				pfs_type = HAMMER2_PFSTYPE_MASTER;
			} else {
				fprintf(stderr, "-t: Unrecognized node type\n");
				usage(1);
			}
			break;
		case 'u':
			/*
			 * set uuid for mkpfs, else one will be generated
			 * (required for all except the MASTER node_type)
			 */
			uuid_str = optarg;
			break;
		case 'd':
			DebugOpt = 1;
			break;
		default:
			fprintf(stderr, "Unknown option: %c\n", ch);
			usage(1);
			/* not reached */
			break;
		}
	}

	/*
	 * Adjust, then process the command
	 */
	ac -= optind;
	av += optind;
	if (ac < 1) {
		fprintf(stderr, "Missing command\n");
		usage(1);
		/* not reached */
	}

	if (strcmp(av[0], "connect") == 0) {
		/*
		 * Add cluster connection
		 */
		if (ac < 2) {
			fprintf(stderr, "connect: missing argument\n");
			usage(1);
		}
		ecode = cmd_remote_connect(sel_path, av[1]);
	} else if (strcmp(av[0], "disconnect") == 0) {
		/*
		 * Remove cluster connection
		 */
		if (ac < 2) {
			fprintf(stderr, "disconnect: missing argument\n");
			usage(1);
		}
		ecode = cmd_remote_disconnect(sel_path, av[1]);
	} else if (strcmp(av[0], "status") == 0) {
		/*
		 * Get status of PFS and its connections (-a for all PFSs)
		 */
		ecode = cmd_remote_status(sel_path, all_opt);
	} else if (strcmp(av[0], "mkpfs") == 0) {
		/*
		 * Create new PFS using pfs_type
		 */
	} else if (strcmp(av[0], "snapshot") == 0) {
		/*
		 * Create snapshot with optional pfs_type and optional
		 * label override.
		 */
	} else if (strcmp(av[0], "helper") == 0) {
		/*
		 * Typically run as a daemon, this multi-threaded helper
		 * subsystem manages socket communications for the
		 * filesystem.
		 */
		ecode = cmd_helper(sel_path);
	} else {
		fprintf(stderr, "Unrecognized command: %s\n", av[0]);
		usage(1);
	}

	/*
	 * In DebugMode we may wind up starting several pthreads in the
	 * original process, in which case we have to let them run and
	 * not actually exit.
	 */
	if (NormalExit) {
		return (ecode);
	} else {
		pthread_exit(NULL);
		_exit(2);	/* NOT REACHED */
	}
}

static
void
usage(int code)
{
	fprintf(stderr,
		"hammer2 [-s path] command...\n"
		"    -s path            Select filesystem\n"
	);
	exit(code);
}
