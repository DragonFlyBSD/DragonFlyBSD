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
 * $DragonFly: src/sbin/hammer/cmd_synctid.c,v 1.1 2008/05/13 20:49:34 dillon Exp $
 */

#include "hammer.h"

static void synctid_usage(int exit_code);

/*
 * synctid <filesystem> [quick]
 */
void
hammer_cmd_synctid(char **av, int ac)
{
	struct hammer_ioc_synctid synctid;
	const char *filesystem;
	int fd;

	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;

	if (ac == 0 || ac > 2)
		synctid_usage(1);
	filesystem = av[0];
	if (ac == 2) {
		if (strcmp(av[1], "quick") == 0)
			synctid.op = HAMMER_SYNCTID_SYNC1;
		else
			synctid_usage(1);
	}
	fd = open(filesystem, O_RDONLY);
	if (fd < 0) {
		err(1, "Unable to open %s", filesystem);
		/* not reached */
	}
	if (ioctl(fd, HAMMERIOC_SYNCTID, &synctid) < 0) {
		err(1, "Synctid %s failed", filesystem);
		/* not reached */
	} else {
		printf("0x%016jx\n", (uintmax_t)synctid.tid);
	}
	close(fd);
}

static
void
synctid_usage(int exit_code)
{
	fprintf(stderr, "hammer synctid <filesystem> [quick]\n");
	exit(exit_code);
}

