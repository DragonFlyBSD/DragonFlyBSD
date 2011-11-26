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

/* Test lockf(3) with overlapping ranges */

/* Provoked:
	lock order reversal:
	 1st 0xc50057a0 vnode interlock (vnode interlock) @ kern/kern_lockf.c:190
	 2nd 0xc14710e8 system map (system map) @ vm/vm_kern.c:296
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <strings.h>
#include <errno.h>
#include <err.h>

#include <stress.h>

char file[128];
int fd;

int
setup(int nb)
{
	int i;
	char buf[1024];

	sprintf(file, "lockf.%d", getpid());
	if ((fd = open(file,O_CREAT | O_TRUNC | O_RDWR, 0600)) == -1)
		err(1, "creat(%s)", file);
	bzero(buf, sizeof(buf));
	for (i = 0; i < 1024; i++)
		if (write(fd, &buf, sizeof(buf)) != sizeof(buf))
			err(1, "write");
	close(fd);
        return (0);
}

void
cleanup(void)
{
	unlink(file);
}

int
test(void)
{
	int i;
	off_t pos;
	off_t size;

	if ((fd = open(file, O_RDWR, 0600)) == -1)
		err(1, "open(%s)", file);

	for (i = 0; i < 1024; i++) {
		pos = random_int(0, 1024 * 1024 - 1);
		if (lseek(fd, pos, SEEK_SET) == -1)
			err(1, "lseek");
		size = random_int(1, 1024 * 1024 - pos);
		if (size > 64)
			size = 64;
		if (lockf(fd, F_LOCK, size) == -1)
			err(1, "lockf(%s, F_LOCK)", file);
		size = random_int(1, size);
		if (lockf(fd, F_ULOCK, size) == -1)
			err(1, "lockf(%s, F_ULOCK)", file);

	}
	close(fd);

        return (0);
}
