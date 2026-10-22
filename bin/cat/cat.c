/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
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
 * @(#)cat.c	8.2 (Berkeley) 4/27/95
 * $FreeBSD: src/bin/cat/cat.c,v 1.14.2.8 2002/06/29 05:09:26 tjr Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int bflag, eflag, nflag, sflag, tflag, vflag, rval;
static const char *filename;

static void	scanfiles(char **, int);
static void	cook_cat(FILE *);
static void	raw_cat(int);

static void
usage(void)
{
	fprintf(stderr,
		"usage: cat [-benstuv] [file ...]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int ch;

	setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "benstuv")) != -1)
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'u':
			setbuf(stdout, NULL);
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argv += optind;

	if (bflag || eflag || nflag || sflag || tflag || vflag)
		scanfiles(argv, 1);
	else
		scanfiles(argv, 0);
	if (fclose(stdout))
		err(1, "stdout");
	exit(rval);
	/* NOTREACHED */
}

static void
scanfiles(char **argv, int cooked)
{
	char *path;
	FILE *fp;
	int fd;
	int i;

	i = 0;

	while ((path = argv[i]) != NULL || i == 0) {
		if (path == NULL || strcmp(path, "-") == 0) {
			filename = "stdin";
			fd = STDIN_FILENO;
		} else {
			filename = path;
			fd = open(path, O_RDONLY);
		}
		if (fd < 0) {
			warn("%s", path);
			rval = 1;
		} else if (cooked) {
			if (fd == STDIN_FILENO) {
				cook_cat(stdin);
			} else {
				fp = fdopen(fd, "r");
				if (fp == NULL)
					err(1, "%s", path);
				cook_cat(fp);
				fclose(fp);
			}
		} else {
			raw_cat(fd);
			if (fd != STDIN_FILENO)
				close(fd);
		}
		if (path == NULL)
			break;
		++i;
	}
}

static void
cook_cat(FILE *fp)
{
	int ch, gobble, line, prev;

	/* Reset EOF condition on stdin. */
	if (fp == stdin && feof(stdin))
		clearerr(stdin);

	line = gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (sflag) {
				if (ch == '\n') {
					if (gobble)
						continue;
					gobble = 1;
				} else
					gobble = 0;
			}
			if (nflag) {
				if (!bflag || ch != '\n') {
					fprintf(stdout, "%6d\t", ++line);
					if (ferror(stdout))
						break;
				} else if (eflag) {
					fprintf(stdout, "%6s\t", "");
					if (ferror(stdout))
						break;
				}
			}
		}
		if (ch == '\n') {
			if (eflag && putchar('$') == EOF)
				break;
		} else if (ch == '\t') {
			if (tflag) {
				if (putchar('^') == EOF || putchar('I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			if (!isascii(ch) && !isprint(ch)) {
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (putchar('^') == EOF ||
				    putchar(ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
		}
		if (putchar(ch) == EOF)
			break;
	}
	if (ferror(fp)) {
		warn("%s", filename);
		rval = 1;
		clearerr(fp);
	}
	if (ferror(stdout))
		err(1, "stdout");
}

static void
raw_cat(int rfd)
{
	int off;
	size_t nbsize;
	ssize_t nr, nw;
	struct stat rst;
	static struct stat ost;
	static size_t bsize;
	static char *buf;

	/*
	 * Figure out the block size to use.  Use the larger of stdout vs
	 * the passed descriptor.  The minimum blocksize we use is BUFSIZ.
	 */
	if (ost.st_blksize == 0) {
		if (fstat(STDOUT_FILENO, &ost))
			err(1, "<stdout>");
		if (ost.st_blksize < BUFSIZ)
			ost.st_blksize = BUFSIZ;
	}
	/*
	 * note: rst.st_blksize can be 0, but it is handled ok.
	 */
	if (fstat(rfd, &rst))
		err(1, "%s", filename);

	nbsize = MAX(ost.st_blksize, rst.st_blksize);
	if (bsize != nbsize) {
		bsize = nbsize;
		if ((buf = realloc(buf, bsize)) == NULL)
			err(1, "malloc failed");
	}
	while ((nr = read(rfd, buf, bsize)) > 0) {
		for (off = 0; nr; nr -= nw, off += nw) {
			nw = write(STDOUT_FILENO, buf + off, nr);
			if (nw < 0)
				err(1, "stdout");
		}
	}
	if (nr < 0) {
		warn("%s", filename);
		rval = 1;
	}
}
