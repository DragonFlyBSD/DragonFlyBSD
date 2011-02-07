/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jef Poskanzer and Craig Leres of the Lawrence Berkeley Laboratory.
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
 * @(#) Copyright (c) 1989, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)write.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/write/write.c,v 1.12 1999/08/28 01:07:48 peter Exp $
 * $DragonFly: src/usr.bin/write/write.c,v 1.7 2005/08/30 22:02:36 liamfoy Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <ctype.h>
#include <err.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "utmpentry.h"

static void	done(int);
static void	do_write(const char *, const char *, uid_t, int *);
static void	usage(void);
static int	term_chk(const char *, int *, time_t *, int);
static void	wr_fputs(unsigned char *s);
static void	search_utmp(const char *, char *, size_t, const char *, uid_t);
static int	utmp_chk(const char *, const char *);

int
main(int argc, char **argv)
{
	time_t atime;
	uid_t myuid;
	int msgsok, mymsgok, myttyfd;
	char tty[MAXPATHLEN], *mytty;

	setlocale(LC_CTYPE, "");

	/* check that sender has write enabled */
	if (isatty(fileno(stdin)))
		myttyfd = fileno(stdin);
	else if (isatty(fileno(stdout)))
		myttyfd = fileno(stdout);
	else if (isatty(fileno(stderr)))
		myttyfd = fileno(stderr);
	else
		errx(1, "can't find your tty");
	if (!(mytty = ttyname(myttyfd)))
		errx(1, "can't find your tty's name");
	if (!strncmp(mytty, _PATH_DEV, strlen(_PATH_DEV)))
		mytty += strlen(_PATH_DEV);
	if (term_chk(mytty, &mymsgok, &atime, 1))
		exit(1);
	if (!mymsgok)
		warnx("you have write permission turned off (man mesg)");

	myuid = getuid();

	/* check args */
	switch (argc) {
	case 2:
		search_utmp(argv[1], tty, sizeof tty, mytty, myuid);
		do_write(tty, mytty, myuid, &mymsgok);
		break;
	case 3:
		if (!strncmp(argv[2], _PATH_DEV, strlen(_PATH_DEV)))
			argv[2] += strlen(_PATH_DEV);
		if (utmp_chk(argv[1], argv[2]))
			errx(1, "%s is not logged in on %s", argv[1], argv[2]);
		if (term_chk(argv[2], &msgsok, &atime, 1))
			exit(1);
		if (myuid && !msgsok)
			errx(1, "%s has messages disabled on %s", argv[1], argv[2]);
		do_write(argv[2], mytty, myuid, &mymsgok);
		break;
	default:
		usage();
	}
	done(0);
	return (0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: write user [tty]\n");
	exit(1);
}

/*
 * utmp_chk - checks that the given user is actually logged in on
 *     the given tty
 */
static int
utmp_chk(const char *user, const char *tty)
{
	struct utmpentry *ep;

	getutentries(NULL, &ep);

	for (; ep; ep = ep->next) {
		if (strcmp(user, ep->name) == 0 &&
		    strcmp(tty, ep->line) == 0) {
			return(0);
		}
	}

	return(1);
}

/*
 * search_utmp - search utmp for the "best" terminal to write to
 *
 * Ignores terminals with messages disabled, and of the rest, returns
 * the one with the most recent access time.  Returns as value the number
 * of the user's terminals with messages enabled, or -1 if the user is
 * not logged in at all.
 *
 * Special case for writing to yourself - ignore the terminal you're
 * writing from, unless that's the only terminal with messages enabled.
 */
static void
search_utmp(const char *user, char *tty, size_t ttyl, const char *mytty, uid_t myuid)
{
	struct utmpentry *ep;
	time_t bestatime, atime;
	int nloggedttys, nttys, msgsok, user_is_me;

	getutentries(NULL, &ep);

	nloggedttys = nttys = 0;
	bestatime = 0;
	user_is_me = 0;
	for (; ep; ep = ep->next)
		if (strcmp(user, ep->name) == 0) {
			++nloggedttys;
			if (term_chk(ep->line, &msgsok, &atime, 0))
				continue;	/* bad term? skip */
			if (myuid && !msgsok)
				continue;	/* skip ttys with msgs off */
			if (strcmp(ep->line, mytty) == 0) {
				user_is_me = 1;
				continue;	/* don't write to yourself */
			}
			++nttys;
			if (atime > bestatime) {
				bestatime = atime;
				strlcpy(tty, ep->line, ttyl);
			}
		}

	if (nloggedttys == 0)
		errx(1, "%s is not logged in", user);
	if (nttys == 0) {
		if (user_is_me) {		/* ok, so write to yourself! */
			strlcpy(tty, mytty, ttyl);
			return;
		}
		errx(1, "%s has messages disabled", user);
	} else if (nttys > 1) {
		warnx("%s is logged in more than once; writing to %s", user, tty);
	}
}

/*
 * term_chk - check that a terminal exists, and get the message bit
 *     and the access time
 */
static int
term_chk(const char *tty, int *msgsokP, time_t *atimeP, int showerror)
{
	struct stat s;
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, tty);
	if (stat(path, &s) < 0) {
		if (showerror)
			warn("%s", path);
		return(1);
	}
	*msgsokP = (s.st_mode & S_IWGRP) != 0;	/* group write bit */
	*atimeP = s.st_atime;
	return(0);
}

/*
 * do_write - actually make the connection
 */
static void
do_write(const char *tty, const char *mytty, uid_t myuid, int *mymsgok)
{
	const char *login;
	char *nows;
	struct passwd *pwd;
	time_t now;
	char path[MAXPATHLEN], host[MAXHOSTNAMELEN], line[512];

	/* Determine our login name before we reopen() stdout */
	if ((login = getlogin()) == NULL) {
		if ((pwd = getpwuid(myuid)))
			login = pwd->pw_name;
		else
			login = "???";
	}

	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, tty);
	if ((freopen(path, "w", stdout)) == NULL)
		err(1, "%s", path);

	signal(SIGINT, done);
	signal(SIGHUP, done);

	/* print greeting */
	if (gethostname(host, sizeof(host)) < 0)
		strlcpy(host, "???", sizeof host);
	now = time(NULL);
	nows = ctime(&now);
	nows[16] = '\0';
	printf("\r\n\007\007\007Message from %s@%s on %s at %s (%s %s replies)...\r\n",
	    login, host, mytty, nows + 11, login, *mymsgok == 0 ? "does not accept" : "accepts");

	while (fgets(line, sizeof(line), stdin) != NULL)
		wr_fputs(line);
}

/*
 * done - cleanup and exit
 */
static void
done(__unused int n)
{
	printf("EOF\r\n");
	exit(0);
}

/*
 * wr_fputs - like fputs(), but makes control characters visible and
 *     turns \n into \r\n
 */
static void
wr_fputs(unsigned char *s)
{

#define	PUTC(c)	if (putchar(c) == EOF) err(1, NULL);

	for (; *s != '\0'; ++s) {
		if (*s == '\n') {
			PUTC('\r');
		} else if (((*s & 0x80) && *s < 0xA0) ||
			   /* disable upper controls */
			   (!isprint(*s) && !isspace(*s) &&
			    *s != '\a' && *s != '\b')
			  ) {
			if (*s & 0x80) {
				*s &= ~0x80;
				PUTC('M');
				PUTC('-');
			}
			if (iscntrl(*s)) {
				*s ^= 0x40;
				PUTC('^');
			}
		}
		PUTC(*s);
	}
	return;
#undef PUTC
}
