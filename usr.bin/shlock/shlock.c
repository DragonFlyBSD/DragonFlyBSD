/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * $DragonFly: src/usr.bin/shlock/shlock.c,v 1.1 2005/07/23 19:47:15 joerg Exp $
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BUFSIZE		16

static int	create_lock(const char *, pid_t, int, int);
static int	check_lock(const char *, int, int);
static void	usage(void);

int
main(int argc, char **argv)
{
	int ch, debug = 0, uucpstyle = 0;
	const char *file = NULL;
	char *endptr;
	pid_t pid = -1;
	long tmp_pid;

	while ((ch = getopt(argc, argv, "df:p:u")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			file = optarg;
			break;
		case 'p':
			errno = 0;
			tmp_pid = strtol(optarg, &endptr, 10);
			if (*endptr != '\0' || errno ||
			    tmp_pid < 1 || (pid = tmp_pid) != tmp_pid)
				errx(1, "invalid pid specified");
			break;
		case 'u':
			uucpstyle = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (file == NULL)
		usage();

	if (pid != -1)
		return(create_lock(file, pid, uucpstyle, debug));
	else
		return(check_lock(file, uucpstyle, debug));
}

static int
create_lock(const char *file, pid_t pid, int uucpstyle, int debug)
{
	char buf[BUFSIZE], tmpf[PATH_MAX];
	char *dir;
	int fd, ret;

	ret = snprintf(buf, sizeof(buf), "%ld\n", (long)pid);
	if (ret >= (int)sizeof(buf) || ret == -1)
		err(1, "snprintf() failed"); /* Must not happen. */

	if ((dir = dirname(file)) == NULL)
		err(1, "dirname() failed");

	ret = snprintf(tmpf, sizeof(tmpf), "%s/shlock%ld", dir, (long)getpid());
	if (ret >= (int)sizeof(tmpf) || ret == -1)
		err(1, "snprintf failed");

	if (debug) {
		printf("%s: trying lock file %s for process %ld\n",
		       getprogname(), file, (long)pid);
	}

	while ((fd = open(tmpf, O_RDWR | O_CREAT | O_EXCL, 0644)) == -1){
		if (errno != EEXIST)
			err(1, "could not create tempory lock file");
		if (debug)
			warnx("temporary lock file %s existed already", tmpf);
		if (unlink(tmpf) && errno != ENOENT) {
			err(1, "could not remove old temporary lock file %s",
			    tmpf);
		}
		/* Try again. */
	}

	if ((uucpstyle && write(fd, &pid, sizeof(pid)) != sizeof(pid)) ||
	    (!uucpstyle && write(fd, buf, strlen(buf)) != (int)strlen(buf))) {
		warn("could not write PID to temporary lock file");
		close(fd);

		if (unlink(tmpf))
			err(1, "could not remove temporary lock file %s", tmpf);

		return(1);		
	}

	close(fd);

	while (link(tmpf, file)) {
		if (errno != EEXIST) {
			if (unlink(tmpf)) {
				err(1,
				    "could not remove temporary lock file %s",
				    tmpf);
			}
			err(1, "could not create lock file");
		}
		if (check_lock(file, uucpstyle, debug) == 0) {
			if (unlink(tmpf)) {
				err(1,
				    "could not remove temporary lock file %s",
				    tmpf);
			}
			return(1); /* Lock file is valid. */
		}
		if (unlink(file) == 0) {
			printf("%s: stale lock file removed\n", getprogname());
			continue;
		}
		if (unlink(tmpf)) {
			err(1, "could not remove temporary lock file %s",
			    tmpf);
		}
		err(1, "could not remove stale lock file");
	}

	if (debug)
		printf("%s: lock successfully obtained\n", getprogname());

	if (unlink(tmpf))
		warn("could not remove temporary lock file %s", tmpf);

	return(0);
}

static int
check_lock(const char *file, int uucpstyle, int debug)
{
	char buf[BUFSIZE];
	int fd;
	ssize_t len;
	pid_t pid;

	if ((fd = open(file, O_RDONLY)) == -1) {
		switch (errno) {
		case ENOENT:
			return(1); /* File doesn't exist. */
		default:
			/*
			 * Something went wrong, bail out as
			 * if the lock existed.
			 */
			err(1, "could not open lock file");
		}
	}

	len = read(fd, buf, uucpstyle ? sizeof(pid_t) : sizeof(buf));
	close(fd);

	if (len < 0) {
		if (debug)
			warn("could not read lock file");
		return(1);
	}
	if (len == 0) {
		if (debug)
			warnx("found empty lock file");
		return(1);
	}
	if (uucpstyle) {
		if (len != sizeof(pid_t)) {
			if (debug)
				warnx("invalid lock file format");
			return(1);
		}
		memcpy(&pid, buf, sizeof(pid_t));
	} else {
		char *endptr;
		long tmp_pid;

		if (len == BUFSIZE) {
			if (debug)
				warnx("invalid lock file format");
			return(1);
		}

		buf[BUFSIZE - 1] = '\0';
		errno = 0;
		tmp_pid = strtol(buf, &endptr, 10);
		if ((*endptr != '\0' && *endptr != '\n') || errno ||
		    tmp_pid < 1 || (pid = tmp_pid) != tmp_pid) {
			if (debug)
				warnx("invalid lock file format");
			return(1);
		}
	}

	if (kill(pid, 0) == 0)
		return(0); /* Process is alive. */

	switch (errno) {
	case ESRCH:
		return(1); /* Process is dead. */
	case EPERM:
		return(0); /* Process is alive. */
	default:
		return(0); /* Something else, assume alive. */
	}
}

static void
usage(void)
{
	fprintf(stderr, "%s [-u] [-d] [-p pid] -f file\n", getprogname());
	exit(1);
}
