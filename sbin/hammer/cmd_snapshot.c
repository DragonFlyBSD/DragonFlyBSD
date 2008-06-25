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
 * $DragonFly: src/sbin/hammer/cmd_snapshot.c,v 1.1 2008/06/25 13:10:06 mneumann Exp $
 */

#include "hammer.h"
#include <sys/param.h>
#include <sys/mount.h>
#include <unistd.h>

static void snapshot_usage(int exit_code);

/*
 * snapshot <softlink-dir> [<filesystem>]
 */
void
hammer_cmd_snapshot(char **av, int ac)
{
	char *softlink_dir;
	const char *filesystem;
	struct statfs buf;
	struct hammer_ioc_synctid synctid;
	int fd;
	char *from;
	char *to;

	if (ac == 0 || ac > 2)
		snapshot_usage(1);

	softlink_dir = av[0];

	if (ac > 1) 
		filesystem = av[1];
	else if (statfs(softlink_dir, &buf) == 0)
		filesystem = buf.f_mntonname;
	else
		err(2, "Unable to determine filesystem of %s", softlink_dir);

	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(2, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_SYNCTID, &synctid) < 0) {
		err(2, "Synctid %s failed", filesystem);
	} else {
		asprintf(&from, "%s@@0x%016llx", filesystem, synctid.tid); 

		if (from == NULL)
			err(2, "Couldn't generate string");

		if (softlink_dir[strlen(softlink_dir)-1] == '/')
			asprintf(&to, "%s0x%016llx", softlink_dir, synctid.tid);
		else
			asprintf(&to, "%s/0x%016llx", softlink_dir, synctid.tid);
		
		if (to == NULL)
			err(2, "Couldn't generate string");

		if (symlink(from, to) != 0)
			err(2, "Unable to symlink %s to %s", from, to);

		printf("%s\n", to);
	}
	close(fd);
}

static
void
snapshot_usage(int exit_code)
{
	fprintf(stderr, "hammer snapshot <softlink-dir> [<filesystem>]\n");
	exit(exit_code);
}

