/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vfs/hammer2/hammer2_mount.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dmsg.h>
#include <mntopts.h>

static int cluster_connect(const char *volume);
static void usage(const char *ctl, ...);

static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	{ "update", 0, MNT_UPDATE, 0 },
	{ "local", 0, HMNT2_LOCAL, 1 },
	MOPT_NULL
};

/*
 * Usage: mount_hammer2 [volume] [mtpt]
 */
int
main(int ac, char *av[])
{
	struct hammer2_mount_info info;
	struct vfsconf vfc;
	char *mountpt;
	char *devpath;
	int error;
	int ch;
	int mount_flags;
	int init_flags;

	bzero(&info, sizeof(info));
	mount_flags = 0;
	init_flags = 0;

	while ((ch = getopt(ac, av, "o:u")) != -1) {
		switch(ch) {
		case 'o':
			getmntopts(optarg, mopts, &mount_flags, &info.hflags);
			break;
		case 'u':
			init_flags |= MNT_UPDATE;
			break;
		default:
			usage("unknown option: -%c", ch);
			/* not reached */
		}
	}
	ac -= optind;
	av += optind;
	mount_flags |= init_flags;

	error = getvfsbyname("hammer2", &vfc);
	if (error) {
		fprintf(stderr, "hammer2 vfs not loaded\n");
		exit(1);
	}

	/*
	 * Only the mount point need be specified in update mode.
	 */
	if (init_flags & MNT_UPDATE) {
		if (ac != 1) {
			usage("missing parameter (mountpoint)");
			/* not reached */
		}
		mountpt = av[0];
		if (mount(vfc.vfc_name, mountpt, mount_flags, &info))
			usage("mount %s: %s", mountpt, strerror(errno));
		exit(0);
	}

	/*
	 * New mount
	 */
	if (ac != 2) {
		usage("missing parameter(s) (dev@LABEL mountpt)");
		/* not reached */
	}

	devpath = strdup(av[0]);
	mountpt = av[1];

	if (strchr(devpath, '@') == NULL) {
		fprintf(stderr,
			"hammer2_mount: no @LABEL specified: \"%s\"\n"
			"typical labels are @LOCAL, @ROOT\n",
			devpath);
		exit(1);
	}

	/*
	 * Connect to the cluster controller.  This handles both remote
	 * mounts and device cache/master/slave mounts.
	 *
	 * When doing remote mounts that are allowed to run in the background
	 * the mount program will fork, detach, print a message, and exit(0)
	 * the originator while retrying in the background.
	 */
	info.cluster_fd = cluster_connect(devpath);
	if (info.cluster_fd < 0) {
		fprintf(stderr,
			"hammer2_mount: cluster_connect(%s) failed\n",
			devpath);
		exit(1);
	}

	/*
	 * Try to mount it, prefix if necessary.
	 */
	if (devpath[0] != '/' && devpath[0] != '@') {
		char *p2;
		asprintf(&p2, "/dev/%s", devpath);
		free(devpath);
		devpath = p2;
	}
	info.volume = devpath;

	error = mount(vfc.vfc_name, mountpt, mount_flags, &info);
	if (error < 0) {
		if (errno == ERANGE) {
			fprintf(stderr,
				"%s integrated with %s\n",
				info.volume, mountpt);
		} else {
			perror("mount: ");
			exit(1);
		}
	}
	free(devpath);

	/*
	 * XXX fork a backgrounded reconnector process to handle connection
	 *     failures. XXX
	 */

	return (0);
}

/*
 * Connect to the cluster controller.  We can connect to a local or remote
 * cluster controller, depending.  For a multi-node cluster we always want
 * to connect to the local controller and let it maintain the connections
 * to the multiple remote nodes.
 */
static
int
cluster_connect(const char *volume __unused)
{
	struct sockaddr_in lsin;
	int fd;

	/*
	 * This starts the hammer2 service if it isn't already running,
	 * so we can connect to it.
	 */
	system("/sbin/hammer2 -q service");

	/*
	 * Connect us to the service but leave the rest to the kernel.
	 * If the connection is lost during the mount
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return(-1);
	}
	bzero(&lsin, sizeof(lsin));
	lsin.sin_family = AF_INET;
	lsin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	lsin.sin_port = htons(DMSG_LISTEN_PORT);

	if (connect(fd, (struct sockaddr *)&lsin, sizeof(lsin)) < 0) {
		close(fd);
		fprintf(stderr, "mount_hammer2: unable to connect to "
				"cluster controller\n");
		return(-1);
	}

	return(fd);
}

static
void
usage(const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	fprintf(stderr, "hammer2_mount: ");
	vfprintf(stderr, ctl, va);
	va_end(va);
	fprintf(stderr, "\n");
	fprintf(stderr, " hammer2_mount -u [-o opts] mountpt\n");
	fprintf(stderr, " hammer2_mount [-o opts] dev@LABEL mountpt\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "options:\n"
			" <standard_mount_opts>\n"
			" local\t- disable PFS clustering for whole device\n"
	);
	exit(1);
}
