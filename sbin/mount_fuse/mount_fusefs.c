/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <vfs/fuse/fuse_mount.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <mntopts.h>
#include <err.h>

#define MOPT_FUSE_LINUX_OPTS \
	{ "default_permissions", 0, FUSE_MOUNT_DEFAULT_PERMISSIONS, 1 }, \
	{ "allow_other", 0, FUSE_MOUNT_ALLOW_OTHER, 1 }, \
	{ "max_read=", 0, FUSE_MOUNT_MAX_READ, 1 }, \
	{ "subtype=", 0, FUSE_MOUNT_SUBTYPE, 1 }

/* XXX */
#define MOPT_FUSE_LINUX_IGNORE_OPTS \
	{ "fsname=", 0, 0, 1 }, \
	{ "fd=", 0, 0, 1 }, \
	{ "rootmode=", 0, 0, 1 }, \
	{ "user_id=", 0, 0, 1 }, \
	{ "group_id=", 0, 0, 1 }, \
	\
	{ "auto_unmount", 0, 0, 1 }, \
	{ "blkdev", 0, 0, 1 }, \
	{ "blksize=", 0, 0, 1 }, \
	{ "context=", 0, 0, 1 }, \
	{ "fscontext=", 0, 0, 1 }, \
	{ "defcontext=", 0, 0, 1 }, \
	{ "rootcontext=", 0, 0, 1 }, \
	{ "user=", 0, 0, 1 }, \
	{ "-r", 0, 0, 1 }, \
	{ "ro", 0, 0, 1 }, \
	{ "rw", 0, 0, 1 }, \
	{ "suid", 0, 0, 1 }, \
	{ "nosuid", 0, 0, 1 }, \
	{ "dev", 0, 0, 1 }, \
	{ "nodev", 0, 0, 1 }, \
	{ "exec", 0, 0, 1 }, \
	{ "noexec", 0, 0, 1 }, \
	{ "async", 0, 0, 1 }, \
	{ "sync", 0, 0, 1 }, \
	{ "dirsync", 0, 0, 1 }, \
	{ "atime", 0, 0, 1 }, \
	{ "noatime", 0, 0, 1 }

static struct mntopt mopts[] = {
	MOPT_FUSE_LINUX_OPTS,
	MOPT_FUSE_LINUX_IGNORE_OPTS,
	MOPT_STDOPTS,
	MOPT_NULL
};

static void
usage(void)
{
	fprintf(stderr, "usage: mount_fusefs [-o options] fd mountpoint\n");
	exit(1);
}

static char*
get_optval(const char *ptr)
{
	char *ret = strdup(ptr);
	const char *end = strstr(ptr, ",");

	if (!end)
		return ret;

	ret[(int)(end - ptr)] = '\0';
	return ret;
}

/*
 * e.g.
 * argv[0] = "mount_fusefs"
 * argv[1] = "-o"
 * argv[2] = "max_read=...,subtype=hello"
 * argv[3] = "3"
 * argv[4] = "/mnt/fuse"
 * argv[5] = "(null)"
 */
int
main(int argc, char **argv)
{
	struct fuse_mount_info args;
	struct vfsconf vfc;
	struct stat st;
	const char *fdstr, *mntpt;
	char *ep, mntpath[MAXPATHLEN], fusedev[64];
	int error, c, fd, mntflags;

	mntflags = 0;
	memset(&args, 0, sizeof(args));

	while ((c = getopt_long(argc, argv, "ho:", NULL, NULL)) != -1) {
		switch(c) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &args.flags);
			if (args.flags & FUSE_MOUNT_MAX_READ) {
				char *p = strstr(optarg, "max_read=");
				if (p) {
					p = get_optval(p + 9);
					args.max_read = strtol(p, NULL, 0);
					free(p);
				}
			}
			if (args.flags & FUSE_MOUNT_SUBTYPE) {
				char *p = strstr(optarg, "subtype=");
				if (p) {
					p = get_optval(p + 8);
					args.subtype = strdup(p);
					free(p);
				}
			}
			break;
		case 'h':
		default:
			usage(); /* exit */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	fdstr = argv[0];
	mntpt = argv[1];
	checkpath(mntpt, mntpath);

	fd = strtol(fdstr, &ep, 10);
	if (fd <= 0 || *ep != '\0')
		err(1, "Invalid FUSE fd %s", fdstr);

	if (fstat(fd, &st) == -1)
		err(1, "Failed to stat FUSE fd %d", fd);
	strcpy(fusedev, "/dev/");
	devname_r(st.st_rdev, S_IFCHR, fusedev + strlen(fusedev),
		sizeof(fusedev) - strlen(fusedev));
	if (stat(fusedev, &st) == -1)
		err(1, "Failed to stat FUSE device %s", fusedev);
	if (strncmp(fusedev, "/dev/fuse", 9))
		err(1, "Invalid FUSE device %s", fusedev);
	args.fd = fd;
	args.from = strdup(fusedev);

	error = getvfsbyname("fuse", &vfc);
	if (error && vfsisloadable("fuse")) {
		if(vfsload("fuse"))
			err(1, "vfsload(%s)", "fuse");
		endvfsent();
		error = getvfsbyname("fuse", &vfc);
	}
	if (error)
		errx(1, "%s filesystem not available", "fuse");

	if (mount(vfc.vfc_name, mntpath, mntflags, &args) == -1)
		err(1, "mount");

	return 0;
}
