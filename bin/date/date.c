/*
 * Copyright (c) 1985, 1987, 1988, 1993
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
 * @(#)date.c	8.2 (Berkeley) 4/28/95
 * $FreeBSD: src/bin/date/date.c,v 1.47 2005/01/10 08:39:21 imp Exp $
 */

#include <sys/param.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <locale.h>
#include <libutil.h>
#include <utmpx.h>

#include "vary.h"

#ifndef	TM_YEAR_BASE
#define	TM_YEAR_BASE	1900
#endif

static time_t tval;

static void badformat(void);
static void setthetime(const char *, const char *, int);
static void usage(void);

static const struct iso8601_fmt {
	const char *refname;
	const char *format;
} iso8601_fmts[] = {
	{ "date", "%Y-%m-%d" },
	{ "hours", "%Y-%m-%dT%H" },
	{ "minutes", "%Y-%m-%dT%H:%M" },
	{ "seconds", "%Y-%m-%dT%H:%M:%S" },
};
static const struct iso8601_fmt *iso8601_selected;

static const char *rfc2822_format = "%a, %d %b %Y %T %z";

int
main(int argc, char **argv)
{
	int ch, rflag;
	int Iflag, jflag, Rflag;
	const char *format;
	char buf[1024], tzbuf[8];
	char *fmt;
	char *tmp;
	struct vary *v;
	const struct vary *badv;
	struct tm lt;
	size_t i;

	v = NULL;
	fmt = NULL;
	setlocale(LC_TIME, "");
	rflag = 0;
	Iflag = jflag = Rflag = 0;
	while ((ch = getopt(argc, argv, "d:f:I::jnRr:uv:")) != -1) {
		switch (ch) {
		case 'f':
			fmt = optarg;
			break;
		case 'I':		/* ISO 8601 datetime format */
			if (Rflag)
				errx(1, "multiple output formats specified");
			Iflag = 1;
			if (optarg == NULL) {
				iso8601_selected = iso8601_fmts;
				break;
			}
			for (i = 0; i < nitems(iso8601_fmts); i++) {
				if (strncmp(optarg, iso8601_fmts[i].refname,
					    strlen(optarg)) == 0)
					break;
			}
			if (i == nitems(iso8601_fmts))
				errx(1, "invalid argument '%s' for -I", optarg);
			iso8601_selected = &iso8601_fmts[i];
			break;
		case 'j':
			jflag = 1;	/* don't set time */
			break;
		case 'n':		/* don't set network */
			break;
		case 'R':		/* RFC 2822 datetime format */
			if (Iflag)
				errx(1, "multiple output formats specified");
			Rflag = 1;
			break;
		case 'r':		/* user specified seconds */
			rflag = 1;
			tval = strtoll(optarg, &tmp, 0);
			if (*tmp != 0)
				usage();
			break;
		case 'u':		/* do everything in UTC */
			if (setenv("TZ", "UTC0", 1) != 0)
				err(1, "setenv: cannot set TZ=UTC0");
			break;
		case 'd':
		case 'v':
			if (strncmp(optarg, "TZ=", 3) == 0) {
				tmp = strdup(optarg);
				size_t tzlen;

				for (tzlen = 0; !isspace(tmp[tzlen]); ++tzlen)
					;
				if (tmp[0] == '"' && tmp[tzlen - 1] == '"') {
					tmp[tzlen - 1] = 0;
					setenv("TZ", tmp + 1, 1);
				} else {
					tmp[tzlen] = 0;
					setenv("TZ", tmp, 1);
				}
				while (isspace(tmp[tzlen]))
					++tzlen;
				if (tmp[tzlen])
					v = vary_append(v, optarg + tzlen);
			} else {
				v = vary_append(v, optarg);
			}
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (!rflag && time(&tval) == -1)
		err(1, "time");

	format = "%+";

	if (Rflag)
		format = rfc2822_format;
	if (Iflag)
		format = iso8601_selected->format;

	/* allow the operands in any order */
	if (*argv && **argv == '+') {
		if (Iflag || Rflag)
			errx(1, "multiple output formats specified");
		format = *argv + 1;
		++argv;
	}

	if (*argv) {
		setthetime(fmt, *argv, jflag);
		++argv;
	} else if (fmt != NULL)
		usage();

	if (*argv && **argv == '+') {
		if (Iflag || Rflag)
			errx(1, "multiple output formats specified");
		format = *argv + 1;
	}

	badv = vary_apply(v, tval, &lt);
	if (badv) {
		fprintf(stderr, "%s: Cannot apply date adjustment\n",
			badv->arg);
		vary_destroy(v);
		usage();
	}
	vary_destroy(v);

	if (format == rfc2822_format) {
		/*
		 * When using RFC 2822 datetime format, don't honor the
		 * locale.
		 */
		setlocale(LC_TIME, "C");
	}

	strftime(buf, sizeof(buf), format, &lt);
	if (Iflag && iso8601_selected > iso8601_fmts) {
		strftime(tzbuf, sizeof(tzbuf), "%z", &lt);
		memmove(&tzbuf[4], &tzbuf[3], 3);
		tzbuf[3] = ':';
		strlcat(buf, tzbuf, sizeof(buf));
	}
	printf("%s\n", buf);
	if (fflush(stdout) != 0)
		err(1, "stdout");
	exit(EXIT_SUCCESS);
}

#define	ATOI2(s)	((s) += 2, ((s)[-2] - '0') * 10 + ((s)[-1] - '0'))

static void
setthetime(const char *fmt, const char *p, int jflag)
{
	struct tm *lt;
	struct timeval tv;
	const char *dot, *t;
	int century;

	if (fmt != NULL) {
		lt = localtime(&tval);
		t = strptime(p, fmt, lt);
		if (t == NULL) {
			fprintf(stderr, "Failed conversion of ``%s''"
				" using format ``%s''\n", p, fmt);
			badformat();
		} else if (*t != '\0')
			fprintf(stderr, "Warning: Ignoring %ld extraneous"
				" characters in date string (%s)\n",
				(long) strlen(t), t);
	} else {
		for (t = p, dot = NULL; *t; ++t) {
			if (isdigit(*t))
				continue;
			if (*t == '.' && dot == NULL) {
				dot = t;
				continue;
			}
			badformat();
		}

		lt = localtime(&tval);

		if (dot != NULL) {			/* .ss */
			dot++; /* *dot++ = '\0'; */
			if (strlen(dot) != 2)
				badformat();
			lt->tm_sec = ATOI2(dot);
			if (lt->tm_sec > 61)
				badformat();
		} else
			lt->tm_sec = 0;

		century = 0;
		/* if p has a ".ss" field then let's pretend it's not there */
		switch (strlen(p) - ((dot != NULL) ? 3 : 0)) {
		case 12:				/* cc */
			lt->tm_year = ATOI2(p) * 100 - TM_YEAR_BASE;
			century = 1;
			/* FALLTHROUGH */
		case 10:				/* yy */
			if (century)
				lt->tm_year += ATOI2(p);
			else {				/* hack for 2000 ;-} */
				lt->tm_year = ATOI2(p);
				if (lt->tm_year < 69)
					lt->tm_year += 2000 - TM_YEAR_BASE;
				else
					lt->tm_year += 1900 - TM_YEAR_BASE;
			}
			/* FALLTHROUGH */
		case 8:					/* mm */
			lt->tm_mon = ATOI2(p);
			if (lt->tm_mon > 12)
				badformat();
			--lt->tm_mon;		/* time struct is 0 - 11 */
			/* FALLTHROUGH */
		case 6:					/* dd */
			lt->tm_mday = ATOI2(p);
			if (lt->tm_mday > 31)
				badformat();
			/* FALLTHROUGH */
		case 4:					/* HH */
			lt->tm_hour = ATOI2(p);
			if (lt->tm_hour > 23)
				badformat();
			/* FALLTHROUGH */
		case 2:					/* MM */
			lt->tm_min = ATOI2(p);
			if (lt->tm_min > 59)
				badformat();
			break;
		default:
			badformat();
		}
	}

	/* Let mktime() decide whether summer time is in effect. */
	lt->tm_isdst = -1;

	/* convert broken-down time to GMT clock time */
	if ((tval = mktime(lt)) == -1)
		errx(1, "nonexistent time");

	if (!jflag) {
		logwtmpx("|", "date", "", 0, OLD_TIME);
		tv.tv_sec = tval;
		tv.tv_usec = 0;
		if (settimeofday(&tv, NULL))
			err(1, "settimeofday (timeval)");
		logwtmpx("{", "date", "", 0, NEW_TIME);

		if ((p = getlogin()) == NULL)
			p = "???";
		syslog(LOG_AUTH | LOG_NOTICE, "date set by %s", p);
	}
}

static void
badformat(void)
{
	warnx("illegal time format");
	usage();
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n",
	    "usage: date [-jnRu] [-r seconds] [-v[+|-]val[ymwdHMS]] ... ",
	    "            "
	    "[-I [date | hours | minutes | seconds]]"
	    "            "
	    "[-f fmt date | [[[[[cc]yy]mm]dd]HH]MM[.ss]] [+format]");
	exit(1);
}
