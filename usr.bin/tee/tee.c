/*
 * Copyright (c) 1988, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#) Copyright (c) 1988, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)tee.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/tee/tee.c,v 1.4 1999/08/28 01:06:21 peter Exp $
 * $DragonFly: src/usr.bin/tee/tee.c,v 1.4 2004/08/15 17:05:06 joerg Exp $
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct desc_list {
	SLIST_ENTRY(desc_list) link;
	int fd;
	const char *name;
};
SLIST_HEAD(, desc_list) desc_head;

static void	add(int, const char *);
static void	usage(void);

int
main(int argc, char **argv)
{
	struct desc_list *p;
	int n, fd, rval, wval, flags;
	char *bp, *buf;
	int append, ch, exitval;
#define	BSIZE (8 * 1024)

	append = 0;
	while ((ch = getopt(argc, argv, "ai")) != -1)
		switch(ch) {
		case 'a':
			append = 1;
			break;
		case 'i':
			if (signal(SIGINT, SIG_IGN) == SIG_ERR)
				err(1, "signal");
			break;
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

	if ((buf = malloc(BSIZE)) == NULL)
		err(1, "malloc");

	add(STDOUT_FILENO, "stdout");

	if (append)
		flags = O_WRONLY|O_CREAT|O_APPEND;
	else
		flags = O_WRONLY|O_CREAT|O_TRUNC;

	for (exitval = 0; *argv != NULL; ++argv) {
		if ((fd = open(*argv, flags, DEFFILEMODE)) < 0) {
			warn("%s", *argv);
			exitval = 1;
		} else {
			add(fd, *argv);
		}
	}

	while ((rval = read(STDIN_FILENO, buf, BSIZE)) > 0) {
		SLIST_FOREACH(p, &desc_head, link) {
			n = rval;
			bp = buf;
			do {
				if ((wval = write(p->fd, bp, n)) == -1) {
					warn("%s", p->name);
					exitval = 1;
					break;
				}
				bp += wval;
			} while (n -= wval);
		}
	}
	if (rval < 0)
		err(1, "read");
	exit(exitval);
}

static void
usage(void)
{
	fprintf(stderr, "usage: tee [-ai] [file ...]\n");
	exit(1);
}

static void
add(int fd, const char *name)
{
	struct desc_list *p;

	p = malloc(sizeof(struct desc_list));
	if (p == NULL)
		err(1, "malloc");
	p->fd = fd;
	p->name = name;
	SLIST_INSERT_HEAD(&desc_head, p, link);
}
