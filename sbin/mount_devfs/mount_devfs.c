/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/param.h>
#include <sys/mount.h>
#if 0
#include <vfs/devfs/devfs.h>
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "mntopts.h"

struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_NULL
};

static void	usage(void);

int
main(int argc, char **argv)
{
	/*
	 * XXX: The type of args definitely should NOT be int, change whenever
	 *	we need real arguments passed on to vfs_mount() in devfs
	 */
	int args;
	int ch, mntflags;
	char mntpoint[MAXPATHLEN];
	/* char target[MAXPATHLEN]; */
	struct vfsconf vfc;
	int error;

	/* XXX: Handle some real mountflags, e.g. UPDATE... */
	mntflags = 0;

	/* XXX: add support for some real options to mount_devfs */
	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch(ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, 0);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	/* resolve mount point with realpath(3) */
	checkpath(argv[0], mntpoint);


	error = getvfsbyname("devfs", &vfc);
	if (error && vfsisloadable("devfs")) {
		if(vfsload("devfs"))
			err(EX_OSERR, "vfsload(devfs)");
		endvfsent();
		error = getvfsbyname("devfs", &vfc);
	}
	if (error)
		errx(EX_OSERR, "devfs filesystem is not available");

	/* XXX: put something useful in args or get rid of it! */
	if (mount(vfc.vfc_name, mntpoint, mntflags, &args))
		err(1, NULL);
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mount_devfs [-o options] mount_point\n");
	exit(1);
}
