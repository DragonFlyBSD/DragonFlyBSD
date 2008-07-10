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
 * $DragonFly: src/sbin/hammer/cmd_snapshot.c,v 1.7 2008/07/10 18:47:22 mneumann Exp $
 */

#include "hammer.h"
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define DEFAULT_SNAPSHOT_NAME "snap-%Y%m%d-%H%M"

static void snapshot_usage(int exit_code);

/*
 * snapshot <softlink-dir-in-filesystem>
 * snapshot <filesystem> <softlink-dir>
 */
void
hammer_cmd_snapshot(char **av, int ac)
{
	const char *filesystem;
	const char *softlink_dir;
	char *softlink_fmt;
	struct statfs buf;
	struct stat st;
	struct hammer_ioc_synctid synctid;
	char *from;
	char *to;

	if (ac == 1) {
		filesystem = NULL;
		softlink_dir = av[0];
	} else if (ac == 2) {
		filesystem = av[0];
		softlink_dir = av[1];
	} else {
		snapshot_usage(1);
	}

	if (stat(softlink_dir, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) 
			err(2, "File %s already exists", softlink_dir);

		if (filesystem == NULL) {
			if (statfs(softlink_dir, &buf) != 0) {
				err(2, "Unable to determine filesystem of %s",
				    softlink_dir);
			}
			filesystem = buf.f_mntonname;
		}

		softlink_fmt = malloc(strlen(softlink_dir) + 1 + 1 +
		                      sizeof(DEFAULT_SNAPSHOT_NAME));
		if (softlink_fmt == NULL)
			err(2, "Failed to allocate string");
	
		strcpy(softlink_fmt, softlink_dir);
		if (softlink_fmt[strlen(softlink_fmt)-1] != '/')
			strcat(softlink_fmt, "/");
		strcat(softlink_fmt, DEFAULT_SNAPSHOT_NAME); 
	} else {
		softlink_fmt = strdup(softlink_dir);

		if (filesystem == NULL) {
			/* 
			 * strip-off last '/path' segment to get the softlink
			 * directory, which we need to determine the filesystem
			 * we are on.
			 */
			char *pos = strrchr(softlink_fmt, '/');
			if (pos != NULL)
				*pos = '\0';

			if (stat(softlink_fmt, &st) != 0 ||
			    !S_ISDIR(st.st_mode)) {
				err(2, "Unable to determine softlink dir %s",
				    softlink_fmt);
			}
			if (statfs(softlink_fmt, &buf) != 0) {
				err(2, "Unable to determine filesystem of %s",
				    softlink_fmt);
			}
			filesystem = buf.f_mntonname;

			/* restore '/' */
			if (pos != NULL)
				*pos = '/';
		}
	}
	
	/*
	 * Synctid 
	 */
	bzero(&synctid, sizeof(synctid));
	synctid.op = HAMMER_SYNCTID_SYNC2;
	int fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(2, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_SYNCTID, &synctid) < 0)
		err(2, "Synctid %s failed", filesystem);
	close(fd);

	asprintf(&from, "%s/@@0x%016llx", filesystem, synctid.tid); 
	if (from == NULL)
		err(2, "Couldn't generate string");
	
	int sz = strlen(softlink_fmt) + 50;
	to = malloc(sz);
	if (to == NULL)
		err(2, "Failed to allocate string");
	
	time_t t = time(NULL);
	if (strftime(to, sz, softlink_fmt, localtime(&t)) == 0)
		err(2, "String buffer too small");
	
	if (symlink(from, to) != 0)
		err(2, "Unable to symlink %s to %s", from, to);

	printf("%s\n", to);

	free(softlink_fmt);
	free(from);
	free(to);
}

static
void
snapshot_usage(int exit_code)
{
	fprintf(stderr, "hammer snapshot <snapshot-dir-in-filesystem>\n");
	fprintf(stderr, "hammer snapshot <filesystem> <snapshot-dir>\n");
	exit(exit_code);
}
