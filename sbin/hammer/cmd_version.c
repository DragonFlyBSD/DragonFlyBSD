/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 *
 * $DragonFly: src/sbin/hammer/cmd_version.c,v 1.2 2008/11/13 02:23:53 dillon Exp $
 */
/*
 * Get and set the HAMMER filesystem version.
 */

#include "hammer.h"

/*
 * version <filesystem>
 */
void
hammer_cmd_get_version(char **av, int ac)
{
	struct hammer_ioc_version version;
	char wip[16];
	int fd;

	if (ac != 1)
		errx(1, "hammer version - expected a single <fs> path arg");
        fd = open(av[0], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "hammer version: unable to access %s: %s\n",
			av[0], strerror(errno));
		exit(1);
	}

	/*
	 * version.cur_version must be set to 0 to retrieve current version
	 * info.
	 */
	bzero(&version, sizeof(version));
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0) {
		fprintf(stderr, "hammer version ioctl: %s\n", strerror(errno));
		exit(1);
	}
	snprintf(wip, 16, "%d", version.wip_version);
	printf("min=%d wip=%s max=%d current=%d description=\"%s\"\n",
		version.min_version,
	       (version.wip_version > version.max_version) ? "none" : wip,
		version.max_version,
		version.cur_version,
		version.description
	);

	/*
	 * Loop over available versions to display description.
	 */
	if (QuietOpt == 0) {
		version.cur_version = 1;
		printf("available versions:\n");
		while (ioctl(fd, HAMMERIOC_GET_VERSION, &version) == 0) {
			printf("    %d\t%s\t%s\n",
				version.cur_version,
				(version.cur_version < version.wip_version ?
					"NORM" : "WIP "),
				version.description);
			++version.cur_version;
		}
	}
	close(fd);
}

/*
 * version-upgrade <filesystem> <version> [force]
 */
void
hammer_cmd_set_version(char **av, int ac)
{
	struct hammer_ioc_version version;
	int fd;
	int overs;
	int nvers;

	if (ac < 2 || ac > 3 || (ac == 3 && strcmp(av[2], "force") != 0)) {
		fprintf(stderr,
			"hammer version-upgrade: expected <fs> vers# [force]\n");
		exit(1);
	}

        fd = open(av[0], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "hammer version-upgrade: unable to access %s: %s\n",
			av[0], strerror(errno));
		exit(1);
	}

	bzero(&version, sizeof(version));
	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0) {
		fprintf(stderr, "hammer ioctl: %s\n", strerror(errno));
		exit(1);
	}
	overs = version.cur_version;

	version.cur_version = strtol(av[1], NULL, 0);
	nvers = version.cur_version;

	if (ioctl(fd, HAMMERIOC_GET_VERSION, &version) < 0) {
		fprintf(stderr, "hammer ioctl: %s\n", strerror(errno));
		exit(1);
	}
	if (version.cur_version >= version.wip_version && ac != 3) {
		fprintf(stderr,
			"The requested version is a work-in-progress"
			" and requires the 'force' directive\n");
		exit(1);
	}
	if (ioctl(fd, HAMMERIOC_SET_VERSION, &version) < 0) {
		fprintf(stderr, "hammer version-upgrade ioctl: %s\n",
			strerror(errno));
		exit(1);
	}
	if (version.head.error) {
		fprintf(stderr, "hammer version-upgrade ioctl: %s\n",
			strerror(version.head.error));
		exit(1);
	}
	printf("hammer version-upgrade: succeeded\n");
	if (overs < 3 && nvers >= 3) {
		printf("NOTE!  Please run 'hammer cleanup' to convert the\n"
		       "<pfs>/snapshots directories to the new meta-data\n"
		       "format.  Once converted configuration data will\n"
		       "no longer resides in <pfs>/snapshots and you can\n"
		       "even rm -rf it entirely if you want.\n");
	}

	close(fd);
}

