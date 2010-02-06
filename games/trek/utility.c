/*-
 * Copyright (c) 1980, 1993
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
 * @(#)utility.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/trek/utility.c,v 1.5 1999/11/30 03:49:55 billf Exp $
 * $DragonFly: src/games/trek/utility.c,v 1.4 2006/09/07 21:19:45 pavalos Exp $
 */

#include <errno.h>
#include <stdarg.h>
#include "trek.h"

/*
**  ASSORTED UTILITY ROUTINES
*/

/*
**  BLOCK MOVE
**
**	Moves a block of storage of length `l' bytes from the data
**	area pointed to by `a' to the area pointed to by `b'.
**	Returns the address of the byte following the `b' field.
**	Overflow of `b' is not tested.
*/

char *
bmove(const void *a, void *b, size_t l)
{
	return((char *)memcpy(b, a, l) + l);
}


/*
**  STRING EQUALITY TEST
**	null-terminated strings `a' and `b' are tested for
**	absolute equality.
**	returns one if equal, zero otherwise.
*/

bool
sequal(const char *a, const char *b)
{
	return(!strcmp(a, b));
}


/*
**  SYSTEM ERROR
*/

void
syserr(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("\n\07TREK SYSERR: ");
	vprintf(fmt, ap);
	printf("\n");
	if (errno)
		printf("\tsystem error %d\n", errno);
	va_end(ap);
	exit(1);
}
