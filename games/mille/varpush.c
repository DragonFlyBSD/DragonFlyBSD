/*-
 * Copyright (c) 1982, 1993
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
 * @(#)varpush.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/mille/varpush.c,v 1.6 1999/12/12 06:17:25 billf Exp $
 */

#include <errno.h>
#include <paths.h>
#include "mille.h"

/*
 * @(#)varpush.c	1.1 (Berkeley) 4/1/82
 */

/*
 *	push variables around via the routine func() on the file
 * channel file.  func() is either readv or writev.
 */
bool
varpush(int file, ssize_t (*func)(int, const struct iovec *, int))
{
	int		temp;
	const struct iovec iov[] = {
		{ (void *) &Debug,	sizeof Debug },
		{ (void *) &Finished,	sizeof Finished },
		{ (void *) &Order,	sizeof Order },
		{ (void *) &End,	sizeof End },
		{ (void *) &On_exit,	sizeof On_exit },
		{ (void *) &Handstart,	sizeof Handstart },
		{ (void *) &Numgos,	sizeof Numgos },
		{ (void *)  Numseen,	sizeof Numseen },
		{ (void *) &Play,	sizeof Play },
		{ (void *) &Window,	sizeof Window },
		{ (void *)  Deck,	sizeof Deck },
		{ (void *) &Discard,	sizeof Discard },
		{ (void *)  Player,	sizeof Player }
	};

	if (((func)(file, iov, sizeof(iov) /sizeof(iov[0]))) < 0) {
		error(strerror(errno));
		return FALSE;
	}

	if (func == readv) {
		if ((read(file, (void *) &temp, sizeof temp)) < 0) {
			error(strerror(errno));
			return FALSE;
		}
		Topcard = &Deck[temp];
#ifdef DEBUG
		if (Debug) {
			char	buf[80];
over:
			printf("Debug file:");
			fgets(buf, sizeof(buf), stdin);
			if ((outf = fopen(buf, "w")) == NULL) {
				perror(buf);
				goto over;
			}
			if (strcmp(buf, _PATH_DEVNULL) != 0)
				setbuf(outf, NULL);
		}
#endif
	} else {
		temp = Topcard - Deck;
		if ((write(file, (void *) &temp, sizeof temp)) < 0) {
			error(strerror(errno));
			return FALSE;
		}
	}
	return TRUE;
}
