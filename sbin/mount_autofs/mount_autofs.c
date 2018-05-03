/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/cdefs.h>

#include <err.h>
#include <mntopts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <vfs/autofs/autofs_mount.h>

#include "mount_autofs.h"

static const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_NULL,
};

static void	usage(void) __dead2;

int
main(int argc, char **argv)
{
	return mount_autofs(argc, argv);
}

void
mount_autofs_parseargs(int argc, char *argv[], void *v, int *mntflags,
	char *canon_dev, char *canon_dir)
{
	int ch;
	struct autofs_mount_info *am = v;

	*mntflags = 0;
	while ((ch = getopt(argc, argv, "f:o:O:p:")) != -1)
		switch (ch) {
		case 'f':
			strlcpy(__DECONST(char*, am->from), optarg, MAXPATHLEN);
			break;
		case 'o':
			getmntopts(optarg, mopts, mntflags, 0);
			break;
		case 'O':
			strlcpy(__DECONST(char*, am->master_options), optarg,
			    MAXPATHLEN);
			break;
		case 'p':
			strlcpy(__DECONST(char*, am->master_prefix), optarg,
			    MAXPATHLEN);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	strlcpy(canon_dev, argv[0], MAXPATHLEN);
	if (realpath(argv[1], canon_dir) == NULL)    /* Check mounton path */
		err(EXIT_FAILURE, "realpath %s", canon_dir);
	if (strncmp(argv[1], canon_dir, MAXPATHLEN)) {
		warnx("\"%s\" is a relative path.", argv[1]);
		warnx("using \"%s\" instead.", canon_dir);
	}
}

int
mount_autofs(int argc, char *argv[])
{
	char canon_dev[MAXPATHLEN], canon_dir[MAXPATHLEN];
	char from[MAXPATHLEN], master_options[MAXPATHLEN];
	char master_prefix[MAXPATHLEN];
	int mntflags, error;
	struct vfsconf vfc;
	struct autofs_mount_info am = {
		.from = from,
		.master_options = master_options,
		.master_prefix = master_prefix,
	};

	mount_autofs_parseargs(argc, argv, &am, &mntflags,
	    canon_dev, canon_dir);

	error = getvfsbyname("autofs", &vfc);
	if (error && vfsisloadable("autofs")) {
		if(vfsload("autofs"))
			err(EX_OSERR, "vfsload(%s)", "autofs");
		endvfsent();
		error = getvfsbyname("autofs", &vfc);
	}
	if (error)
		errx(EX_OSERR, "%s filesystem not available", "autofs");

	if (mount(vfc.vfc_name, canon_dir, mntflags, &am) == -1)
		err(EXIT_FAILURE, "autofs on %s", canon_dir);

	return EXIT_SUCCESS;
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "Usage: %s [-o options] [-O master_options] [-p master_prefix] "
		"[-f <from>] autofs mount_point\n", getprogname());
	exit(EXIT_FAILURE);
}
