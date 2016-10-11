/*
 * Copyright (c) 1980, 1987, 1993
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
 * @(#) Copyright (c) 1980, 1987, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)users.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/users/users.c,v 1.8 2002/09/04 23:29:09 dwmalone Exp $
 * $DragonFly: src/usr.bin/users/users.c,v 1.5 2007/10/02 16:38:10 swildner Exp $
 */

#include <sys/types.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "utmpentry.h"

typedef char   namebuf[32];

static int scmp(const void *, const void *);
static void usage(void);

int
main(int argc, char **argv)
{
	struct utmpentry *ep = NULL;	/* avoid gcc warnings */
	namebuf *names = NULL;
	int ncnt = 0;
	int nmax = 0;
	int cnt;
	int ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch(ch) {
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	getutentries(NULL, &ep);
	for (; ep; ep = ep->next) {
		if (ncnt >= nmax) {
			nmax += 32;
			names = realloc(names,
				sizeof (*names) * nmax);
			if (names == NULL)
				err(1, "realloc");
		}
		strcpy(names[ncnt], ep->name);
		++ncnt;
	}
	if (ncnt) {
		qsort(names, ncnt, 32, scmp);
		printf("%.*s", 32, names[0]);
		for (cnt = 1; cnt < ncnt; ++cnt) {
			if (strcmp(names[cnt], names[cnt - 1]))
				printf(" %.*s", 32, names[cnt]);
		}
		printf("\n");
	}
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: users\n");
	exit(1);
}
	
int
scmp(const void *p, const void *q)
{
	return(strncmp(p, q, 32));
}
