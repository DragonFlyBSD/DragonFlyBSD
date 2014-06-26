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
#include <sys/devfs.h>

#include <err.h>
#include <mntopts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#if 0
#define MOPT_UPDATE         { "update",     0, MNT_UPDATE, 0 }
#endif
#define MOPT_DEVFSOPTS		\
	{ "ruleset=", 0, DEVFS_MNT_RULESET, 1 },	\
	{ "jail", 0, DEVFS_MNT_JAIL, 1 }


struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_DEVFSOPTS,
#if 0
	MOPT_UPDATE,
#endif
	MOPT_NULL
};

static void	usage(void);

int
main(int argc, char **argv)
{
	struct statfs sfb;
	struct devfs_mount_info info;
	int ch, mntflags;
	char *ptr, *mntto;
	char mntpoint[MAXPATHLEN];
	char rule_file[MAXPATHLEN];
	struct vfsconf vfc;
	int error;
	info.flags = 0;
	int i,k;
	int mounted = 0;

	mntflags = 0;

	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch(ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &info.flags);
			ptr = strstr(optarg, "ruleset=");
			if (ptr) {
				ptr += 8;
				for (i = 0, k = 0;
				    (i < MAXPATHLEN) && (ptr[i] != '\0') && (ptr[i] != ',');
				    i++) {
					rule_file[k++] = ptr[i];

				}
				rule_file[k] = '\0';
			}
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
	switch(argc) {
	case 1:
		mntto = argv[0];
		break;
	case 2:
		mntto = argv[1];
		break;
	default:
		mntto = NULL;
		usage();
		/* NOTREACHED */
	}

	checkpath(mntto, mntpoint);

	error = getvfsbyname("devfs", &vfc);
	if (error && vfsisloadable("devfs")) {
		if(vfsload("devfs"))
			err(EX_OSERR, "vfsload(devfs)");
		endvfsent();
		error = getvfsbyname("devfs", &vfc);
	}
	if (error)
		errx(EX_OSERR, "devfs filesystem is not available");

	error = statfs(mntpoint, &sfb);

	if (error)
		err(EX_OSERR, "could not statfs() the mount point");

	if ((!strcmp(sfb.f_fstypename, "devfs")) &&
	    (!strcmp(sfb.f_mntfromname, "devfs"))) {
		mounted = 1;
	}

	if (!mounted) {
		if (mount(vfc.vfc_name, mntpoint, mntflags, &info))
			err(1, NULL);
	} else {
		if (fork() == 0) {
			execlp("devfsctl", "devfsctl",
			    "-m", mntpoint,
				"-c",
				"-r",
				NULL);
		}
	}

	if (info.flags & DEVFS_MNT_RULESET) {
		if (fork() == 0) {
			execlp("devfsctl", "devfsctl",
			    "-m", mntpoint,
				"-a",
				"-f", rule_file,
			    NULL);
		}
	}
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mount_devfs [-o options] [mount_from] mount_point\n");
	exit(1);
}
