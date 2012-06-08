/*-
 * Copyright (c) 1993, 1994
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#) Copyright (c) 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)mount_ufs.c	8.4 (Berkeley) 4/26/95
 * $FreeBSD: src/sbin/mount/mount_ufs.c,v 1.16.2.3 2001/08/01 08:27:29 obrien Exp $
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <mntopts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vfs/ufs/ufsmount.h>

#include "extern.h"

static void ufs_usage(void);

static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_ASYNC,
	MOPT_FORCE,
	MOPT_SYNC,
	MOPT_UPDATE,
	MOPT_IGNORE,
	MOPT_TRIM,
	MOPT_NULL
};

int mount_ufs(int, const char **);

int
mount_ufs(int argc, const char **argv)
{
	struct ufs_args args;
	int ch, mntflags;
	const char *fs_name;
	struct vfsconf vfc;
	int error = 0;

	mntflags = 0;
	optind = optreset = 1;		/* Reset for parse of new argv. */
	while ((ch = getopt(argc, __DECONST(char **, argv), "o:")) != -1)
		switch (ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, 0);
			break;
		case '?':
		default:
			ufs_usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		ufs_usage();

        args.fspec = __DECONST(char *, argv[0]);	/* The name of the device file. */
	fs_name = argv[1];		/* The mount point. */

#define DEFAULT_ROOTUID	-2
	args.export.ex_root = DEFAULT_ROOTUID;
	if (mntflags & MNT_RDONLY)
		args.export.ex_flags = MNT_EXRDONLY;
	else
		args.export.ex_flags = 0;

	if (mntflags & MNT_TRIM){
		char sysctl_name[64];
		int trim_enabled = 0;
		size_t olen = sizeof(trim_enabled);
		char *dev_name = strdup(args.fspec);
		dev_name = strtok(dev_name + strlen("/dev/da"),"s");
		sprintf(sysctl_name, "kern.cam.da.%s.trim_enabled", dev_name);
		sysctlbyname(sysctl_name, &trim_enabled, &olen, NULL, 0);
		if(errno == ENOENT) {
			printf("Device:%s does not support the TRIM command\n",
			    args.fspec);
			ufs_usage();
		}
		if(!trim_enabled) {
			printf("Online TRIM selected, but sysctl (%s) "
			    "is not enabled\n",sysctl_name);
			ufs_usage();
		}
	}

	error = getvfsbyname("ufs", &vfc);
	if (error && vfsisloadable("ufs")) {
		if (vfsload("ufs")) {
			warn("vfsload(ufs)");
			return (1);
		}
		endvfsent(); /* flush old table */
		error = getvfsbyname("ufs", &vfc);
	}
	if (error) {
		warnx("ufs filesystem is not available");
		return (1);
	}

	if (mount(vfc.vfc_name, fs_name, mntflags, &args) < 0) {
		switch (errno) {
		case EMFILE:
			warnx("%s on %s: mount table full",
				args.fspec, fs_name);
			break;
		case EINVAL:
			if (mntflags & MNT_UPDATE)
				warnx(
		"%s on %s: specified device does not match mounted device",
					args.fspec, fs_name);
			else
				warnx("%s on %s: incorrect super block",
					args.fspec, fs_name);
			break;
		default:
			warn("%s", args.fspec);
			break;
		}
		return (1);
	}
	return (0);
}

static void
ufs_usage(void)
{
	fprintf(stderr, "usage: mount_ufs [-o options] special node\n");
	exit(1);
}
