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

/*
 * Obtain a file descriptor that the caller can execute ioctl()'s on.
 */
int
hammer2_ioctl_handle(const char *sel_path)
{
	struct hammer2_ioc_version info;
	int fd;

	if (sel_path == NULL)
		sel_path = ".";

	fd = open(sel_path, O_RDONLY, 0);
	if (fd < 0) {
		fprintf(stderr, "hammer2: Unable to open %s: %s\n",
			sel_path, strerror(errno));
		return(-1);
	}
	if (ioctl(fd, HAMMER2IOC_GET_VERSION, &info) < 0) {
		fprintf(stderr, "hammer2: '%s' is not a hammer2 filesystem\n",
			sel_path);
		close(fd);
		return(-1);
	}
	return (fd);
}

void
hammer2_disconnect(void *(*func)(void *), void *arg)
{
	pthread_t thread = NULL;
	pid_t pid;
	int ttyfd;

	/*
	 * Do not disconnect in debug mode
	 */
	if (DebugOpt) {
                pthread_create(&thread, NULL, func, arg);
		NormalExit = 0;
		return;
	}

	/*
	 * Otherwise disconnect us.  Double-fork to get rid of the ppid
	 * association and disconnect the TTY.
	 */
	if ((pid = fork()) < 0) {
		fprintf(stderr, "hammer2: fork(): %s\n", strerror(errno));
		exit(1);
	}
	if (pid > 0) {
		while (waitpid(pid, NULL, 0) != pid)
			;
		return;		/* parent returns */
	}

	/*
	 * Get rid of the TTY/session before double-forking to finish off
	 * the ppid.
	 */
	ttyfd = open("/dev/null", O_RDWR);
	if (ttyfd >= 0) {
		if (ttyfd != 0)
			dup2(ttyfd, 0);
		if (ttyfd != 1)
			dup2(ttyfd, 1);
		if (ttyfd != 2)
			dup2(ttyfd, 2);
		if (ttyfd > 2)
			close(ttyfd);
	}

	ttyfd = open("/dev/tty", O_RDWR);
	if (ttyfd >= 0) {
		ioctl(ttyfd, TIOCNOTTY, 0);
		close(ttyfd);
	}
	setsid();

	/*
	 * Second fork to disconnect ppid (the original parent waits for
	 * us to exit).
	 */
	if ((pid = fork()) < 0) {
		_exit(2);
	}
	if (pid > 0)
		_exit(0);

	/*
	 * The double child
	 */
	setsid();
	pthread_create(&thread, NULL, func, arg);
	pthread_exit(NULL);
	_exit(2);	/* NOT REACHED */
}
