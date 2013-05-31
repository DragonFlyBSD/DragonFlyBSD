/*
 * Copyright (c) 1988, 1993, 1994
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
 * @(#) Copyright (c) 1988, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)sleep.c	8.3 (Berkeley) 4/2/94
 * $FreeBSD: src/bin/sleep/sleep.c,v 1.9.2.1 2001/08/01 05:23:25 obrien Exp $
 * $DragonFly: src/bin/sleep/sleep.c,v 1.7 2005/02/27 15:57:15 liamfoy Exp $
 */

#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static void usage(void);

int
main(int argc, char **argv)
{
	struct timespec time_to_sleep;
	long l;
	int ch, neg;
	char *p;

	setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "")) != -1)
		switch(ch) {
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		/* NOTREACHED */
	}

	p = argv[0];

	/* Skip over leading whitespaces. */
	while (isspace(*p))
		++p;

	/* Check for optional `+' or `-' sign. */
	neg = 0;
	if (*p == '-') {
		neg = 1;
		++p;
	}
	else if (*p == '+')
		++p;

	/* Calculate seconds. */
	if (isdigit(*p)) {
		l = strtol(p, &p, 10);
		if (l > INT_MAX) {
			/*
			 * Avoid overflow when `seconds' is huge.  This assumes
			 * that the maximum value for a time_t is <= INT_MAX.
			 */
			l = INT_MAX;
		}
	} else
		l = 0;
	time_to_sleep.tv_sec = (time_t)l;

	/* Calculate nanoseconds. */
	time_to_sleep.tv_nsec = 0;

	if (*p == '.') {		/* Decimal point. */
		l = 100000000L;
		do {
			if (isdigit(*++p))
				time_to_sleep.tv_nsec += (*p - '0') * l;
			else
				break;
		} while (l /= 10);
	}

	if (!neg && (time_to_sleep.tv_sec > 0 || time_to_sleep.tv_nsec > 0))
		nanosleep(&time_to_sleep, NULL);

	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: sleep seconds\n");
	exit(1);
}
