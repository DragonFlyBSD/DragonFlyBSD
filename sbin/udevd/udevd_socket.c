/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
#include <sys/device.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libprop/proplib.h>
#include <sys/udev.h>
#include "udevd.h"

int
send_xml(int s, char *xml)
{
	ssize_t r,n;
	size_t sz;

	sz = strlen(xml) + 1;

	r = send(s, &sz, sizeof(sz), 0);
	if (r <= 0)
		return r;

	r = 0;
	while (r < (ssize_t)sz) {
		n = send(s, xml+r, sz-r, 0);
		if (n <= 0)
			return n;
		r += n;
	}

	return r;
}

int
read_xml(int s, char **buf)
{
	char *xml;
	size_t sz;
	int n, r;

	*buf = NULL;

	n = recv(s, &sz, sizeof(sz), MSG_WAITALL);
	if ((n <= 0) || (sz > 12*1024*1024)) /* Arbitrary limit */
		return n;

	xml = malloc(sz+2);	
	r = 0;
	while (r < (ssize_t)sz) {
		n = recv(s, xml+r, sz-r, MSG_WAITALL);
		if (n <= 0) {
			free(xml);
			return n;
		}
		r += n;
	}

	*buf = xml;
	return r;
}

int
unblock_descriptor(int s)
{
	int flags, ret;

	flags = fcntl(s, F_GETFL, 0);
	ret = fcntl(s, F_SETFL, flags | O_NONBLOCK);
	return ret;
}

int
block_descriptor(int s)
{
	int flags, ret;

	flags = fcntl(s, F_GETFL, 0);
	ret = fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
	return ret;
}

int
init_local_server(const char *sockfile, int socktype, int nonblock)
{
	mode_t msk;
	int s;
	struct sockaddr_un un_addr;

	if ((s = socket(AF_UNIX, socktype, 0)) < 0)
		return -1;

	memset(&un_addr, 0, sizeof(un_addr));
	un_addr.sun_family = AF_UNIX;
	strncpy(un_addr.sun_path, sockfile, SOCKFILE_NAMELEN);
	un_addr.sun_path[SOCKFILE_NAMELEN - 1] = '\0';

	/*
	 * DO NOT change `un_addr.sun_path' to `sockfile' here,
	 * since `sockfile' may have been truncated by above strncpy(3).
	 */
	unlink(un_addr.sun_path);

	if (nonblock && unblock_descriptor(s) < 0) {
		close(s);
		return -1;
	}

	msk = umask(S_IXUSR|S_IXGRP|S_IXOTH);
	if (bind(s, (struct sockaddr *)&un_addr, sizeof(un_addr)) < 0) {
		close(s);
		return -1;
	}
	umask(msk);	 /* Restore the original mask */

	if (socktype == SOCK_STREAM && listen(s, LOCAL_BACKLOG) < 0) {
		close(s);
		return -1;
	}

	return s;
}
