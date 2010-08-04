/*-
 * Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/* Test lockf(3) */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <err.h>

#include <stress.h>

char file[128];
int fd;
pid_t	pid;

int
get(void) {
	int sem;
	if (lockf(fd, F_LOCK, 0) == -1)
		err(1, "lockf(%s, F_LOCK)", file);
	if (read(fd, &sem, sizeof(sem)) != sizeof(sem))
		err(1, "get: read(%d)", fd);
	if (lseek(fd, 0, SEEK_SET) == -1)
		err(1, "lseek");
	if (lockf(fd, F_ULOCK, 0) == -1)
		err(1, "lockf(%s, F_ULOCK)", file);
	return (sem);
}

void
incr(void) {
	int sem;
	if (lockf(fd, F_LOCK, 0) == -1)
		err(1, "lockf(%s, F_LOCK)", file);
	if (read(fd, &sem, sizeof(sem)) != sizeof(sem))
		err(1, "incr: read(%d)", fd);
	if (lseek(fd, 0, SEEK_SET) == -1)
		err(1, "lseek");
	sem++;
	if (write(fd, &sem, sizeof(sem)) != sizeof(sem))
		err(1, "incr: read");
	if (lseek(fd, 0, SEEK_SET) == -1)
		err(1, "lseek");
	if (lockf(fd, F_ULOCK, 0) == -1)
		err(1, "lockf(%s, F_ULOCK)", file);
}
int
setup(int nb)
{
        return (0);
}

void
cleanup(void)
{
}

int
test(void)
{
	int i;
	int sem = 0;

	sprintf(file, "lockf.0.%d", getpid());
	if ((fd = open(file,O_CREAT | O_TRUNC | O_RDWR, 0600)) == -1)
		err(1, "creat(%s)", file);
	if (write(fd, &sem, sizeof(sem)) != sizeof(sem))
		err(1, "write");
	if (lseek(fd, 0, SEEK_SET) == -1)
		err(1, "lseek");

	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(2);
	}

	if (pid == 0) {	/* child */
		for (i = 0; i < 100; i++) {
			while ((get() & 1) == 0)
				;
			if (op->verbose > 2)
				printf("Child  %d, sem = %d\n", i, get()),
					fflush(stdout);
			incr();
		}
		exit(0);
	} else {	/* parent */
		for (i = 0; i < 100; i++) {
			while ((get() & 1) == 1)
				;
			if (op->verbose > 2)
				printf("Parent %d, sem = %d\n", i, get()),
					fflush(stdout);
			incr();
		}
	}
	close(fd);
	waitpid(pid, &i, 0);
	unlink(file);

        return (0);
}
