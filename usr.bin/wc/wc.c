/*
 * Copyright (c) 1980, 1987, 1991, 1993
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
 * @(#) Copyright (c) 1980, 1987, 1991, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)wc.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: head/usr.bin/wc/wc.c 281617 2015-04-16 21:44:35Z bdrewery $
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static uintmax_t tlinect, twordct, tcharct, tlongline;
static int doline, doword, dochar, domulti, dolongline;
static volatile sig_atomic_t siginfo;

static void	show_cnt(const char *file, uintmax_t linect, uintmax_t wordct,
		    uintmax_t charct, uintmax_t llct);
static int	cnt(const char *);
static void	usage(void);

#ifdef SIGINFO
static void
siginfo_handler(int sig __unused)
{
	siginfo = 1;
}
#endif

static void
reset_siginfo(void)
{
#ifdef SIGINFO
	signal(SIGINFO, SIG_DFL);
#endif
	siginfo = 0;
}

int
main(int argc, char *argv[])
{
	int ch, errors, total;

	setlocale(LC_CTYPE, "");

	while ((ch = getopt(argc, argv, "clmwL")) != -1) {
		switch (ch) {
		case 'l':
			doline = 1;
			break;
		case 'w':
			doword = 1;
			break;
		case 'c':
			dochar = 1;
			domulti = 0;
			break;
		case 'L':
			dolongline = 1;
			break;
		case 'm':
			domulti = 1;
			dochar = 0;
			break;
		case '?':
		default:
			usage();
		}
	}
	argv += optind;
	argc -= optind;

#ifdef SIGINFO
	signal(SIGINFO, siginfo_handler);
#endif

	/* Wc's flags are on by default. */
	if (doline + doword + dochar + domulti + dolongline == 0)
		doline = doword = dochar = 1;

	errors = 0;
	total = 0;
	if (*argv == NULL) {
		if (cnt(NULL) != 0)
			++errors;
	} else {
		do {
			if (cnt(*argv) != 0)
				++errors;
			++total;
		} while(*++argv);
	}

	if (total > 1)
		show_cnt("total", tlinect, twordct, tcharct, tlongline);
	exit(errors == 0 ? 0 : 1);
}

static void
show_cnt(const char *file, uintmax_t linect, uintmax_t wordct,
    uintmax_t charct, uintmax_t llct)
{
	FILE *out;

	if (!siginfo)
		out = stdout;
	else {
		out = stderr;
		siginfo = 0;
	}

	if (doline)
		fprintf(out, " %7ju", linect);
	if (doword)
		fprintf(out, " %7ju", wordct);
	if (dochar || domulti)
		fprintf(out, " %7ju", charct);
	if (dolongline)
		fprintf(out, " %7ju", llct);
	if (file != NULL)
		fprintf(out, " %s\n", file);
	else
		fprintf(out, "\n");
}

static int
cnt(const char *file)
{
	struct stat sb;
	uintmax_t linect, wordct, charct, llct, tmpll;
	int fd, len, warned;
	size_t clen;
	short gotsp;
	u_char *p;
	u_char buf[MAXBSIZE];
	wchar_t wch;
	mbstate_t mbs;

	linect = wordct = charct = llct = tmpll = 0;
	if (file == NULL) {
		fd = STDIN_FILENO;
	} else {
		if ((fd = open(file, O_RDONLY)) < 0) {
			warn("%s: open", file);
			return (1);
		}
		if (doword || (domulti && MB_CUR_MAX != 1))
			goto word;
		/*
		 * Line counting is split out because it's a lot faster to get
		 * lines than to get words, since the word count requires some
		 * logic.
		 */
		if (doline) {
			while ((len = read(fd, buf, MAXBSIZE))) {
				if (len == -1) {
					warn("%s: read", file);
					close(fd);
					return (1);
				}
				if (siginfo) {
					show_cnt(file, linect, wordct, charct,
					    llct);
				}
				charct += len;
				for (p = buf; len--; ++p) {
					if (*p == '\n') {
						if (tmpll > llct)
							llct = tmpll;
						tmpll = 0;
						++linect;
					} else
						tmpll++;
				}
			}
			reset_siginfo();
			tlinect += linect;
			if (dochar)
				tcharct += charct;
			if (dolongline) {
				if (llct > tlongline)
					tlongline = llct;
			}
			show_cnt(file, linect, wordct, charct, llct);
			close(fd);
			return (0);
		}
		/*
		 * If all we need is the number of characters and it's a
		 * regular file, just stat the puppy.
		 */
		if (dochar || domulti) {
			if (fstat(fd, &sb)) {
				warn("%s: fstat", file);
				close(fd);
				return (1);
			}
			/* Pseudo-filesystems advertize a zero size. */
			if (S_ISREG(sb.st_mode) && sb.st_size > 0) {
				reset_siginfo();
				charct = sb.st_size;
				show_cnt(file, linect, wordct, charct, llct);
				tcharct += charct;
				close(fd);
				return (0);
			}
		}
	}

	/* Do it the hard way... */
word:	gotsp = 1;
	warned = 0;
	memset(&mbs, 0, sizeof(mbs));
	while ((len = read(fd, buf, MAXBSIZE)) != 0) {
		if (len == -1) {
			warn("%s: read", file != NULL ? file : "stdin");
			close(fd);
			return (1);
		}
		p = buf;
		while (len > 0) {
			if (siginfo)
				show_cnt(file, linect, wordct, charct, llct);
			if (!domulti || MB_CUR_MAX == 1) {
				clen = 1;
				wch = (unsigned char)*p;
			} else if ((clen = mbrtowc(&wch, p, len, &mbs)) ==
			    (size_t)-1) {
				if (!warned) {
					errno = EILSEQ;
					warn("%s",
					    file != NULL ? file : "stdin");
					warned = 1;
				}
				memset(&mbs, 0, sizeof(mbs));
				clen = 1;
				wch = (unsigned char)*p;
			} else if (clen == (size_t)-2)
				break;
			else if (clen == 0)
				clen = 1;
			charct++;
			if (wch != L'\n')
				tmpll++;
			len -= clen;
			p += clen;
			if (wch == L'\n') {
				if (tmpll > llct)
					llct = tmpll;
				tmpll = 0;
				++linect;
			}
			if (iswspace(wch))
				gotsp = 1;
			else if (gotsp) {
				gotsp = 0;
				++wordct;
			}
		}
	}
	reset_siginfo();
	if (domulti && MB_CUR_MAX > 1)
		if (mbrtowc(NULL, NULL, 0, &mbs) == (size_t)-1 && !warned)
			warn("%s", file != NULL ? file : "stdin");
	if (doline)
		tlinect += linect;
	if (doword)
		twordct += wordct;
	if (dochar || domulti)
		tcharct += charct;
	if (dolongline) {
		if (llct > tlongline)
			tlongline = llct;
	}
	show_cnt(file, linect, wordct, charct, llct);
	close(fd);
	return (0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: wc [-Lclmw] [file ...]\n");
	exit(1);
}
