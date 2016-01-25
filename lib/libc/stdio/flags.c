/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * @(#)flags.c	8.1 (Berkeley) 6/4/93
 * $FreeBSD: src/lib/libc/stdio/flags.c,v 1.10 2007/01/09 00:28:06 imp Exp $
 * $DragonFly: src/lib/libc/stdio/flags.c,v 1.5 2005/01/31 22:29:40 dillon Exp $
 */

#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <errno.h>

#include "local.h"

/*
 * Return the (stdio) flags for a given mode.  Store the flags
 * to be passed to an _open() syscall through *optr.
 * Return 0 on error.
 */
int
__sflags(const char *mode, int *optr)
{
	int ret, m, o;

	/*
	 * Base mode
	 */
	switch (*mode) {
	case 'r':
		/*
		 * Reading
		 */
		ret = __SRD;
		m = O_RDONLY;
		o = 0;
		break;
	case 'w':
		/*
		 * Writing
		 */
		ret = __SWR;
		m = O_WRONLY;
		o = O_CREAT | O_TRUNC;
		break;
	case 'a':
		/*
		 * Append
		 */
		ret = __SWR;
		m = O_WRONLY;
		o = O_CREAT | O_APPEND;
		break;
	default:
		/*
		 * Illegal primary mode, fail.
		 */
		errno = EINVAL;
		return (0);
	}

	/*
	 * Parse modifiers
	 */
	for (++mode; *mode; ++mode) {
		switch(*mode) {
		case '+':
			/*
			 * R+W
			 */
			ret = __SRW;
			m = O_RDWR;
			break;
		case 'b':
			/*
			 * Binary (inherent, no special treatment)
			 */
			break;
		case 'x':
			/*
			 * Exclusive.  Only make sense if opening for
			 * r+w, w, or a, but let open() deal with
			 * any issues (make behavior the same as glibc).
			 */
			o |= O_EXCL;
			break;
		case 'e':
			o |= O_CLOEXEC;
			break;
		default:
			/* ignore unrecognized flag */
			break;
		}
	}
	*optr = m | o;

	return (ret);
}
