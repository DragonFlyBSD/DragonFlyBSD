/*-
 * Copyright (c) 1980, 1992, 1993
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
 * @(#) Copyright (c) 1980, 1992, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)main.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/systat/main.c,v 1.11.2.1 2001/06/06 20:26:01 tmm Exp $
 * $DragonFly: src/usr.bin/systat/main.c,v 1.7 2008/11/10 04:59:45 swildner Exp $
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/time.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <nlist.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "systat.h"
#include "extern.h"

static struct nlist namelist[] = {
#define X_FIRST		0
#define	X_HZ		0
	{ .n_name = "_hz" },
#define	X_STATHZ		1
	{ .n_name = "_stathz" },
	{ .n_name = "" }
};
static int     dellave;

kvm_t *kd;
sig_t	sigtstpdfl;
double avenrun[3];
int     col;
double	naptime = 5.0;
int     verbose = 1;                    /* to report kvm read errs */
int     hz, stathz;
double	hertz;
char    c;
char    *namp;
char    hostname[MAXHOSTNAMELEN];
WINDOW  *wnd;
int     CMDLINE;

static	WINDOW *wload;			/* one line window for load average */

int
main(int argc, char **argv)
{
	char errbuf[_POSIX2_LINE_MAX];

	(void) setlocale(LC_TIME, "");

	argc--, argv++;
	while (argc > 0) {
		if (argv[0][0] == '-') {
			struct cmdtab *p;

			p = lookup(&argv[0][1]);
			if (p == (struct cmdtab *)-1)
				errx(1, "%s: ambiguous request", &argv[0][1]);
			if (p == NULL)
				errx(1, "%s: unknown request", &argv[0][1]);
			curcmd = p;
		} else {
			naptime = strtod(argv[0], NULL);
			if (naptime <= 0.0)
				naptime = 5.0;
		}
		argc--, argv++;
	}
	kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
	if (kd == NULL) {
		error("%s", errbuf);
		exit(1);
	}
	signal(SIGINT, die);
	signal(SIGQUIT, die);
	signal(SIGTERM, die);

	/*
	 * Initialize display.  Load average appears in a one line
	 * window of its own.  Current command's display appears in
	 * an overlapping sub-window of stdscr configured by the display
	 * routines to minimize update work by curses.
	 */
	initscr();
	CMDLINE = LINES - 1;
	wnd = (*curcmd->c_open)();
	if (wnd == NULL) {
		warnx("couldn't initialize display");
		die(0);
	}
	wload = newwin(1, 0, 3, 20);
	if (wload == NULL) {
		warnx("couldn't set up load average window");
		die(0);
	}
	if (kvm_nlist(kd, namelist)) {
		nlisterr(namelist);
		exit(1);
	}
	if (namelist[X_FIRST].n_type == 0)
		errx(1, "couldn't read namelist");
	gethostname(hostname, sizeof (hostname));
	NREAD(X_HZ, &hz, sizeof(hz));
	NREAD(X_STATHZ, &stathz, sizeof(stathz));
	hertz = stathz ? stathz : hz;
	(*curcmd->c_init)();
	curcmd->c_flags |= CF_INIT;
	labels();

	dellave = 0.0;

	signal(SIGALRM, display);
	display(0);
	noecho();
	crmode();
	keyboard();
	/*NOTREACHED*/

	return EXIT_SUCCESS;
}

void
labels(void)
{
	if (curcmd->c_flags & CF_LOADAV) {
		mvaddstr(2, 20,
		    "/0   /1   /2   /3   /4   /5   /6   /7   /8   /9   /10");
		mvaddstr(3, 5, "Load Average");
	}
	(*curcmd->c_label)();
#ifdef notdef
	mvprintw(21, 25, "CPU usage on %s", hostname);
#endif
	refresh();
}

void
display(int signo __unused)
{
	int i, j;
	struct itimerval ctv;

	/* Get the load average over the last minute. */
	(void) getloadavg(avenrun, sizeof(avenrun) / sizeof(avenrun[0]));
	(*curcmd->c_fetch)();
	if (curcmd->c_flags & CF_LOADAV) {
		j = 5.0*avenrun[0] + 0.5;
		dellave = avenrun[0];
		c = '|';
		wmove(wload, 0, 0); wclrtoeol(wload);
		for (i = (j > 50) ? 50 : j; i > 0; i--)
			waddch(wload, c);
		if (j > 50)
			wprintw(wload, " %4.1f", avenrun[0]);
	}
	(*curcmd->c_refresh)();
	if (curcmd->c_flags & CF_LOADAV)
		wrefresh(wload);
	wrefresh(wnd);
	move(CMDLINE, col);
	refresh();
	ctv.it_interval.tv_sec = 0;
	ctv.it_interval.tv_usec = 0;
	ctv.it_value.tv_sec = (int)naptime;
	ctv.it_value.tv_usec = (naptime - (double)(int)naptime) * 1000000.0;
	setitimer(ITIMER_REAL, &ctv, NULL);
}

void
load(void)
{

	(void) getloadavg(avenrun, sizeof(avenrun)/sizeof(avenrun[0]));
	mvprintw(CMDLINE, 0, "%4.1f %4.1f %4.1f",
	    avenrun[0], avenrun[1], avenrun[2]);
	clrtoeol();
}

void
die(int signo __unused)
{
	move(CMDLINE, 0);
	clrtoeol();
	refresh();
	endwin();
	exit(0);
}

#include <stdarg.h>

void
error(const char *fmt, ...)
{
	va_list ap;
	char buf[255];
	int oy, ox;
	va_start(ap, fmt);

	if (wnd) {
		getyx(stdscr, oy, ox);
		(void) vsnprintf(buf, sizeof(buf), fmt, ap);
		clrtoeol();
		standout();
		mvaddstr(CMDLINE, 0, buf);
		standend();
		move(oy, ox);
		refresh();
	} else {
		(void) vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

void
nlisterr(struct nlist *n_list)
{
	int i, n;

	n = 0;
	clear();
	mvprintw(2, 10, "systat: nlist: can't find following symbols:");
	for (i = 0;
	    n_list[i].n_name != NULL && *n_list[i].n_name != '\0'; i++)
		if (n_list[i].n_value == 0)
			mvprintw(2 + ++n, 10, "%s", n_list[i].n_name);
	move(CMDLINE, 0);
	clrtoeol();
	refresh();
	endwin();
	exit(1);
}
