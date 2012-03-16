/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Donn Seeley at Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Copyright (c) 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)init.c	8.1 (Berkeley) 7/15/93
 * $FreeBSD: src/sbin/init/init.c,v 1.38.2.8 2001/10/22 11:27:32 des Exp $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <ttyent.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <err.h>

#include <stdarg.h>

#include "pathnames.h"

static int	setctty(const char *);
static void	runcom(char **);

/*
 * The mother of all processes.
 */
int
main(int argc __unused, char **argv)
{
	/* Dispose of random users. */
	if (getuid() != 0)
		errx(1, "%s", strerror(EPERM));

	runcom(argv);
	return 1;
}

static int
setctty(const char *name)
{
	int fd;

	revoke(name);
	if ((fd = open(name, O_RDWR)) == -1) {
		exit(1);
	}

	if (login_tty(fd) == -1) {
		exit(1);
	}

	return fd;
}


static void
runcom(char **argv_orig)
{
	pid_t pid, wpid;
	int status, fd, error;
	const char *argv[4];
	struct sigaction sa;

	fd = -1;

	if ((pid = fork()) == 0) {
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_IGN;
		sigaction(SIGTSTP, &sa, NULL);
		sigaction(SIGHUP, &sa, NULL);

		setctty(_PATH_CONSOLE);

		argv[0] = "sh";
		argv[1] = _PATH_RUNCOM;
		argv[2] = "autoboot" ;
		argv[3] = NULL;

		sigprocmask(SIG_SETMASK, &sa.sa_mask, NULL);

		execv(_PATH_BSHELL, __DECONST(char **, argv));
		exit(1);	/* force single user mode */
	}

	do {
		wpid = waitpid(-1, &status, WUNTRACED);
	} while (wpid != pid);

	error = chroot_kernel("/new_root");
	if (error)
		exit(1);

	error = chroot("/new_root");
	if (error)
		exit(1);

	execv("/sbin/init", __DECONST(char **, argv_orig));
}

