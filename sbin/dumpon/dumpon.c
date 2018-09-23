/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)swapon.c	8.1 (Berkeley) 6/5/93
 * $FreeBSD: src/sbin/dumpon/dumpon.c,v 1.10.2.2 2001/07/30 10:30:05 dd Exp $
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fstab.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sysexits.h>

static const char *sc_name = "kern.dumpdev";

static struct stat *get_dumpdev(void);
static void usage(void) __dead2;

int
main(int argc, char **argv)
{
	int ch;
	struct stat stab;
	struct stat *stab_old;
	char *path, *p;
	bool is_dumpoff;

	if (strstr((p = strrchr(argv[0], '/')) ? p+1 : argv[0],
		   "dumpoff") != 0) {
		is_dumpoff = true;
	} else {
		is_dumpoff = false;
	}

	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			/* backward compatibility only */
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((is_dumpoff && argc != 0) || (!is_dumpoff && argc != 1))
		usage();

	path = argv[0];
	if (is_dumpoff || strcmp(path, "off") == 0) {
		stab.st_rdev = NODEV;
	} else {
		path = getdevpath(path, 0);
		if (stat(path, &stab) != 0)
			err(EX_OSFILE, "%s", path);

		if (!S_ISCHR(stab.st_mode)) {
			errx(EX_USAGE,
			     "%s: must specify a character disk device",
			     path);
		}
	}

	stab_old = get_dumpdev();

	if (stab.st_rdev == stab_old->st_rdev) {
		if (stab.st_rdev == NODEV) {
			printf("dumpon: crash dumps already disabled.\n");
		} else {
			printf("dumpon: crash dumps already configured "
			       "to the given device.\n");
		}
	} else if (stab.st_rdev == NODEV || stab_old->st_rdev == NODEV) {
		if (sysctlbyname(sc_name, NULL, NULL,
				 &stab.st_rdev, sizeof stab.st_rdev) != 0) {
			err(EX_OSERR, "sysctl: %s", sc_name);
		}
		if (stab.st_rdev == NODEV) {
			printf("dumpon: crash dumps disabled\n");
		} else {
			printf("dumpon: crash dumps to %s (%lu, %#lx)\n",
			       path,
			       (unsigned long)major(stab.st_rdev),
			       (unsigned long)minor(stab.st_rdev));
		}
	} else {
		warnx("crash dumps already configured "
		      "to another device (%lu, %#lx)",
		      (unsigned long)major(stab.st_rdev),
		      (unsigned long)minor(stab.st_rdev));
		errx(EX_USAGE, "you need to run 'dumpoff' first.");
	}

	return 0;
}


static struct stat *
get_dumpdev(void)
{
	struct stat *stab;
	size_t len;

	if ((stab = malloc(sizeof(*stab))) == NULL)
		err(EX_OSERR, "malloc");

	memset(stab, 0, sizeof(*stab));
	len = sizeof(stab->st_rdev);
	if (sysctlbyname(sc_name, &stab->st_rdev, &len, NULL, 0) != 0)
		err(EX_OSERR, "sysctl: %s", sc_name);

	return stab;
}

static void
usage(void)
{
	fprintf(stderr,
		"usage: dumpon [-v] special_file\n"
		"       dumpon [-v] off\n"
		"       dumpoff [-v]\n");
	exit(EX_USAGE);
}
