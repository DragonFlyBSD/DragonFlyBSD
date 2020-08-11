/*
 * Copyright (c) 2017-2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
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

#include "hammer2.h"

static int pathdir(const char *path, const char **lastp);

int
cmd_destroy_path(int ac, const char **av)
{
	hammer2_ioc_destroy_t destroy;
	const char *last;
	int i;
	int fd;
	int ecode = 0;

	for (i = 0; i < ac; ++i) {
		bzero(&destroy, sizeof(destroy));
		destroy.cmd = HAMMER2_DELETE_FILE;
		printf("%s\t", av[i]);
		fflush(stdout);
		fd = pathdir(av[i], &last);
		if (fd >= 0) {
			snprintf(destroy.path, sizeof(destroy.path),
				 "%s", last);
			if (ioctl(fd, HAMMER2IOC_DESTROY, &destroy) < 0) {
				printf("%s\n", strerror(errno));
				ecode = 1;
			} else {
				printf("ok\n");
			}
			close(fd);
		} else {
			printf("%s\n", strerror(errno));
			ecode = 1;
		}
	}
	return ecode;
}

static
int
pathdir(const char *path, const char **lastp)
{
	const char *ptr;
	char *npath;
	int fd;

	ptr = path + strlen(path);
	while (ptr > path && ptr[-1] != '/')
		--ptr;
	*lastp = ptr;
	if (ptr == path) {
		fd = open(".", O_RDONLY);
	} else {
		asprintf(&npath, "%*.*s",
			(int)(ptr - path), (int)(ptr - path), path);
		fd = open(npath, O_RDONLY);
		free(npath);
	}

	return fd;
}

int
cmd_destroy_inum(const char *sel_path, int ac, const char **av)
{
	hammer2_ioc_destroy_t destroy;
	int i;
	int fd;
	int ecode = 0;

	fd = hammer2_ioctl_handle(sel_path);
	if (fd < 0)
		return 1;

	printf("deleting inodes on %s\n", sel_path);
	for (i = 0; i < ac; ++i) {
		bzero(&destroy, sizeof(destroy));
		destroy.cmd = HAMMER2_DELETE_INUM;
		destroy.inum = strtoul(av[i], NULL, 0);
		printf("%16jd ", (intmax_t)destroy.inum);
		if (ioctl(fd, HAMMER2IOC_DESTROY, &destroy) < 0) {
			printf("%s\n", strerror(errno));
			ecode = 1;
		} else {
			printf("ok\n");
		}
	}
	close(fd);

	return ecode;
}
