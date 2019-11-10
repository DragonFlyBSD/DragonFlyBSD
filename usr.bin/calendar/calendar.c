/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
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
 * @(#)calendar.c  8.3 (Berkeley) 3/25/94
 * $FreeBSD: head/usr.bin/calendar/calendar.c 326025 2017-11-20 19:49:47Z pfg $
 */

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <locale.h>
#include <login_cap.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "calendar.h"

struct passwd	*pw;
bool		doall = false;
bool		debug = false;
static char	*DEBUG = NULL;
static time_t	f_time = 0;
double		UTCOffset;
double		EastLongitude;

static void	usage(void) __dead2;
static double	get_utcoffset(void);

int
main(int argc, char *argv[])
{
	int	f_dayAfter = 0;		/* days after current date */
	int	f_dayBefore = 0;	/* days before current date */
	int	Friday = 5;		/* day before weekend */

	int ch;
	struct tm tp1, tp2;

	setlocale(LC_ALL, "");
	UTCOffset = get_utcoffset();
	EastLongitude = UTCOffset * 15;

	while ((ch = getopt(argc, argv, "-A:aB:D:dF:f:hl:t:U:W:?")) != -1) {
		switch (ch) {
		case '-':		/* backward compatible */
		case 'a':
			if (getuid()) {
				errno = EPERM;
				err(1, NULL);
			}
			doall = true;
			break;

		case 'W': /* we don't need no steenking Fridays */
			Friday = -1;
			/* FALLTHROUGH */
		case 'A': /* days after current date */
			f_dayAfter = atoi(optarg);
			if (f_dayAfter < 0)
				errx(1, "number of days must be positive");
			break;

		case 'B': /* days before current date */
			f_dayBefore = atoi(optarg);
			if (f_dayBefore < 0)
				errx(1, "number of days must be positive");
			break;

		case 'D': /* debug output of sun and moon info */
			DEBUG = optarg;
			break;

		case 'd': /* debug output of current date */
			debug = true;
			break;

		case 'F': /* Change the time: When does weekend start? */
			Friday = atoi(optarg);
			break;

		case 'f': /* other calendar file */
			calendarFile = optarg;
			break;

		case 'l': /* Change longitudal position */
			EastLongitude = strtod(optarg, NULL);
			UTCOffset = EastLongitude / 15;
			break;

		case 't': /* other date, for tests */
			f_time = Mktime(optarg);
			break;

		case 'U': /* Change UTC offset */
			UTCOffset = strtod(optarg, NULL);
			EastLongitude = UTCOffset * 15;
			break;

		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	/* use current time */
	if (f_time <= 0)
		time(&f_time);

	settimes(f_time, f_dayBefore, f_dayAfter, Friday, &tp1, &tp2);
	generatedates(&tp1, &tp2);

	/*
	 * FROM now on, we are working in UTC.
	 * This will only affect moon and sun related events anyway.
	 */
	if (setenv("TZ", "UTC", 1) != 0)
		errx(1, "setenv: %s", strerror(errno));
	tzset();

	if (debug)
		dumpdates();

	if (DEBUG != NULL) {
		dodebug(DEBUG);
		exit(0);
	}

	if (doall) {
		while ((pw = getpwent()) != NULL) {
			pid_t pid;

			if (chdir(pw->pw_dir) == -1)
				continue;

			pid = fork();
			if (pid < 0)
				err(1, "fork");
			if (pid == 0) {
				login_cap_t *lc;

				lc = login_getpwclass(pw);
				if (setusercontext(lc, pw, pw->pw_uid,
						   LOGIN_SETALL) != 0)
					errx(1, "setusercontext");

				cal();
				exit(0);
			}
		}
	} else {
		cal();
	}

	return (0);
}


static void __dead2
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n",
	    "usage: calendar [-A days] [-a] [-B days] [-D sun|moon] [-d]",
	    "		     [-F friday] [-f calendarfile] [-l longitude]",
	    "		     [-t dd[.mm[.year]]] [-U utcoffset] [-W days]"
	    );
	exit(1);
}


/*
 * Calculate the timezone difference between here and UTC.
 *
 * Return the offset hour from UTC.
 */
static double
get_utcoffset(void)
{
	time_t t;
	struct tm tm;
	long utcoffset;

	time(&t);
	localtime_r(&t, &tm);
	utcoffset = tm.tm_gmtoff;

	return (utcoffset / FSECSPERHOUR);
}
