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
 * $DragonFly: src/bin/notty/notty.c,v 1.2 2008/06/24 21:13:26 thomas Exp $
 */
/*
 * NOTTY.C - program to disconnect a program from the tty and close
 *           stdin, stdout, and stderr (-012 to specify which descriptors
 *           to leave open).
 *
 * NOTTY [-012] <command>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static void usage(void);

int
main(int ac, char **av)
{
	const char *opts = "";
	int ttyfd;
	int fd;

	if (ac == 1)
		usage();

	if (av[1]) {
		if (av[1][0] == '-') {
			opts = av[1];
			++av;
		}
	}


	ttyfd = open("/dev/null", O_RDWR);

	if (strchr(opts, '0') == NULL && ttyfd != 0)
		dup2(ttyfd, 0);
	if (strchr(opts, '1') == NULL && ttyfd != 1)
		dup2(ttyfd, 1);
	if (strchr(opts, '2') == NULL && ttyfd != 2)
		dup2(ttyfd, 2);

	if (ttyfd > 2)
		close(ttyfd);

	fd = open("/dev/tty", O_RDWR);
	if (fd >= 0) {
		ioctl(fd, TIOCNOTTY, 0);
		close(fd);
	} 

	if (fork() == 0) {
		setsid();
		exit(execvp(av[1], av + 1));
	}
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "notty [-012] command args ...\n");
	exit(1);
}

