/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
#include <sys/diskslice.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/syslimits.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <uuid.h>
#include <err.h>
#include <assert.h>
#include <ctype.h>
#include <mntopts.h>

#define MOPT_UPDATE         { "update",     0, MNT_UPDATE, 0 }
#define PLATFORM_LEN	16

static struct mntopt mopts[] = { MOPT_STDOPTS, MOPT_UPDATE, MOPT_NULL };

static void usage(void);

int
main(int ac, char **av)
{
	struct vfsconf vfc;
	int mount_flags = 0;
	int error;
	int ch;
	int init_flags = 0;
	char *mountpt, *hostdir;
	size_t vsize;
	char platform[PLATFORM_LEN] = {0};

	mount_flags = 0;

	while ((ch = getopt(ac, av, "o:u")) != -1) {
		switch(ch) {
                case 'u':
                        init_flags |= MNT_UPDATE;
                        break;

		case 'o':
			getmntopts(optarg, mopts, &mount_flags, NULL);
			break;
		default:
			usage();
			/* not reached */
		}
	}
	ac -= optind;
	av += optind;
	mount_flags |= init_flags;

	/*
	 * Check we're in a vkernel or abort.
	 */
	vsize = PLATFORM_LEN;
	error = sysctlbyname("hw.platform", &platform, &vsize, NULL,0);
	if (error)
		errx(1, "Failed to get hw.platform sysctl");

	if (strnstr(platform, "vkernel", PLATFORM_LEN) == NULL)
		errx(1, "dirfs is only available for vkernels.");

        /*
         * Only the mount point need be specified in update mode.
         */
        if (init_flags & MNT_UPDATE) {
                if (ac != 1) {
                        usage();
                        /* not reached */
                }
                mountpt = av[0];
                if (mount(vfc.vfc_name, mountpt, mount_flags, NULL))
                        err(1, "mountpoint %s", mountpt);
                exit(0);
        }

	if (ac < 2) {
		usage();
		/* not reached */
	}

	hostdir = av[0];
	mountpt = av[1];

	/*
	 * Load the dirfs module if necessary (this bit stolen from
	 * mount_null).
	 */
	error = getvfsbyname("dirfs", &vfc);
	if (error && vfsisloadable("dirfs")) {
		if (vfsload("dirfs") != 0)
			err(1, "vfsload(dirfs)");
		endvfsent();
		error = getvfsbyname("dirfs", &vfc);
	}
	if (error)
		errx(1, "dirfs filesystem is not available");

	error = mount(vfc.vfc_name, mountpt, mount_flags, hostdir);
	if (error)
		err(1, "failed to mount %s on %s", hostdir, mountpt);

	exit (0);
}

static
void
usage(void)
{
	fprintf(stderr, "usage: mount_dirfs [-u] [-o options] "
			"hostdir dir\n");
	exit(1);
}
