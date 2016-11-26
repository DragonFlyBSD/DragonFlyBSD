/*
 * Copyright (c) 1989, 1991, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
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
 * $FreeBSD: src/lib/libc/gen/getcwd.c,v 1.18 1999/09/28 13:24:13 marcel Exp $
 *
 * @(#)getcwd.c	8.5 (Berkeley) 2/7/95
 */

#include <sys/param.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int	__getcwd(char *pt, size_t size);

char *
getcwd(char *pt, size_t size)
{
	int allocated = 0, ret;

	/*
	 * If no buffer specified by the user, allocate one as necessary.
	 */
	if (pt == NULL) {
		size = MAXPATHLEN;
		if ((pt = malloc(MAXPATHLEN)) == NULL)
			return(NULL);
		allocated = 1;
	}
	ret = __getcwd(pt, size);
	if (ret == 0)
		return (pt);
	if (allocated)
		free(pt);
	return (NULL);
}

char *
getwd(char *buf)
{
	char *p;

	if ((p = getcwd(buf, MAXPATHLEN)) != 0)
		return(p);
	strcpy(buf, strerror(errno));
	return(NULL);
}
