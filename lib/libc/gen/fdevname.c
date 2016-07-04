/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
#include "namespace.h"
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <paths.h>
#include <errno.h>
#include <limits.h>
#include <machine/stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "reentrant.h"
#include "un-namespace.h"

#include "libc_private.h"

static char fdevname_buf[sizeof(_PATH_DEV) + NAME_MAX];

static once_t		fdevname_init_once = ONCE_INITIALIZER;
static thread_key_t	fdevname_key;
static int		fdevname_keycreated = 0;

int
fdevname_r(int fd, char *buf, size_t len)
{
	struct stat	sb;
	struct fiodname_args fa;

	*buf = '\0';

	/* Must be a valid file descriptor */
	if (_fstat(fd, &sb))
		return (EBADF);

	/* Must be a character device */
	if (!S_ISCHR(sb.st_mode))
		return (EINVAL);

	fa.len = len;
	fa.name = buf;
	if (_ioctl(fd, FIODNAME, &fa) == -1) {
		return ERANGE;
	}
	return (0);
}

static void
fdevname_keycreate(void)
{
	fdevname_keycreated = (thr_keycreate(&fdevname_key, free) == 0);
}

char *
fdevname(int fd)
{
	char	*buf;
	int	error;

	if (thr_main() != 0)
		buf = fdevname_buf;
	else {
		if (thr_once(&fdevname_init_once, fdevname_keycreate) != 0 ||
		    !fdevname_keycreated)
			return (NULL);
		if ((buf = thr_getspecific(fdevname_key)) == NULL) {
			if ((buf = malloc(sizeof fdevname_buf)) == NULL)
				return (NULL);
			if (thr_setspecific(fdevname_key, buf) != 0) {
				free(buf);
				return (NULL);
			}
		}
	}

	if (((error = fdevname_r(fd, buf, sizeof fdevname_buf))) != 0) {
		errno = error;
		return (NULL);
	}
	return (buf);
}
