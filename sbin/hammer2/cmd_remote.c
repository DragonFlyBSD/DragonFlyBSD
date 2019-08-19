/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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

#include "hammer2.h"

int
cmd_remote_connect(const char *sel_path, const char *url)
{
	hammer2_ioc_remote_t remote;
	int ecode = 0;
	int fd;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&remote, sizeof(remote));
	remote.copyid = -1;
	remote.fd = -1;
	if (strlen(url) >= sizeof(remote.copy1.path)) {
		fprintf(stderr, "hammer2: connect: Path too long\n");
		close(fd);
		return(1);
	}
	snprintf((char*)remote.copy1.path, sizeof(remote.copy1.path), "%s",
		 url);
	if (ioctl(fd, HAMMER2IOC_REMOTE_ADD, &remote) < 0) {
		perror("ioctl");
		ecode = 1;
	}
	close(fd);
	return ecode;
}

int
cmd_remote_disconnect(const char *sel_path, const char *url)
{
	hammer2_ioc_remote_t remote;
	int ecode = 0;
	int fd;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&remote, sizeof(remote));
	remote.copyid = -1;
	remote.fd = -1;
	if (strlen(url) >= sizeof(remote.copy1.path)) {
		fprintf(stderr, "hammer2: disconnect: Path too long\n");
		close(fd);
		return(1);
	}
	snprintf((char*)remote.copy1.path, sizeof(remote.copy1.path), "%s",
		 url);
	if (ioctl(fd, HAMMER2IOC_REMOTE_DEL, &remote) < 0) {
		perror("ioctl");
		ecode = 1;
	}
	close(fd);
	return ecode;
}

int
cmd_remote_status(const char *sel_path, int all_opt __unused)
{
	hammer2_ioc_remote_t remote;
	int ecode = 0;
	int count = 0;
	int fd;

	if ((fd = hammer2_ioctl_handle(sel_path)) < 0)
		return(1);
	bzero(&remote, sizeof(remote));

	while ((remote.copyid = remote.nextid) >= 0) {
		if (ioctl(fd, HAMMER2IOC_REMOTE_SCAN, &remote) < 0) {
			perror("ioctl");
			ecode = 1;
			break;
		}
		if (remote.copy1.copyid == 0)
			continue;
		if (count == 0)
			printf("CPYID LABEL           STATUS PATH\n");
		printf("%5d %-15s %c%c%c.%02x %s\n",
			remote.copy1.copyid,
			remote.copy1.label,
			'-', '-', '-',
			remote.copy1.priority,
			remote.copy1.path);
		++count;
	}
	if (count == 0)
		printf("No linkages found\n");
	return (ecode);
}
