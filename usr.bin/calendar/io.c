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
 * $FreeBSD: head/usr.bin/calendar/io.c 327117 2017-12-23 21:04:32Z eadler $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <time.h>
#include <unistd.h>

#include "calendar.h"

struct iovec header[] = {
	{ __DECONST(char *, "From: "), 6 },
	{ NULL, 0 },
	{ __DECONST(char *, " (Reminder Service)\nTo: "), 24 },
	{ NULL, 0 },
	{ __DECONST(char *, "\nSubject: "), 10 },
	{ NULL, 0 },
	{ __DECONST(char *, "'s Calendar\nPrecedence: bulk\n"),  29 },
	{ __DECONST(char *, "Auto-Submitted: auto-generated\n\n"), 32 },
};

enum {
	T_OK = 0,
	T_ERR,
	T_PROCESS,
};

struct fixs neaster, npaskha, ncny, nfullmoon, nnewmoon;
struct fixs nmarequinox, nsepequinox, njunsolstice, ndecsolstice;

const char *calendarFile = "calendar"; /* default calendar file */
static const char *calendarHomes[] = {".calendar", "/usr/share/calendar"};
static const char *calendarNoMail = "nomail"; /* don't sent mail if file exist */

static char path[MAXPATHLEN];

static StringList *definitions = NULL;
static struct event *events[MAXCOUNT];
static char *extradata[MAXCOUNT];

static FILE	*cal_fopen(const char *file);
static bool	 cal_parse(FILE *in, FILE *out);
static void	 closecal(FILE *fp);
static FILE	*opencalin(void);
static FILE	*opencalout(void);
static int	 token(char *line, FILE *out, bool *skip);
static void	 trimlr(char **buf);


static void
trimlr(char **buf)
{
	char *walk = *buf;
	char *last;

	while (isspace(*walk))
		walk++;
	if (*walk != '\0') {
		last = walk + strlen(walk) - 1;
		while (last > walk && isspace(*last))
			last--;
		*(last+1) = 0;
	}

	*buf = walk;
}

static FILE *
cal_fopen(const char *file)
{
	FILE *fp = NULL;
	char *cwd = NULL;
	char *home;
	char cwdpath[MAXPATHLEN];
	unsigned int i;

	if (!doall) {
		home = getenv("HOME");
		if (home == NULL || *home == '\0')
			errx(1, "Cannot get home directory");
		if (chdir(home) != 0)
			errx(1, "Cannot enter home directory: \"%s\"", home);
	}

	if (getcwd(cwdpath, sizeof(cwdpath)) != NULL)
		cwd = cwdpath;
	else
		warnx("Cannot get current working directory");

	for (i = 0; i < nitems(calendarHomes); i++) {
		if (chdir(calendarHomes[i]) != 0)
			continue;

		if ((fp = fopen(file, "r")) != NULL)
			break;
	}

	if (cwd && chdir(cwdpath) != 0)
		warnx("Cannot back to original directory: \"%s\"", cwdpath);

	if (fp == NULL)
		warnx("Cannot open calendar file \"%s\"", file);

	return (fp);
}

static int
token(char *line, FILE *out, bool *skip)
{
	char *walk, c, a;

	if (strncmp(line, "endif", 5) == 0) {
		*skip = false;
		return (T_OK);
	}

	if (*skip)
		return (T_OK);

	if (strncmp(line, "include", 7) == 0) {
		walk = line + 7;

		trimlr(&walk);

		if (*walk == '\0') {
			warnx("Expecting arguments after #include");
			return (T_ERR);
		}

		if (*walk != '<' && *walk != '\"') {
			warnx("Excecting '<' or '\"' after #include");
			return (T_ERR);
		}

		a = *walk;
		walk++;
		c = walk[strlen(walk) - 1];

		switch(c) {
		case '>':
			if (a != '<') {
				warnx("Unterminated include expecting '\"'");
				return (T_ERR);
			}
			break;
		case '\"':
			if (a != '\"') {
				warnx("Unterminated include expecting '>'");
				return (T_ERR);
			}
			break;
		default:
			warnx("Unterminated include expecting '%c'",
			    a == '<' ? '>' : '\"' );
			return (T_ERR);
		}
		walk[strlen(walk) - 1] = '\0';

		if (!cal_parse(cal_fopen(walk), out))
			return (T_ERR);

		return (T_OK);
	}

	if (strncmp(line, "define", 6) == 0) {
		if (definitions == NULL)
			definitions = sl_init();
		walk = line + 6;
		trimlr(&walk);

		if (*walk == '\0') {
			warnx("Expecting arguments after #define");
			return (T_ERR);
		}

		sl_add(definitions, strdup(walk));
		return (T_OK);
	}

	if (strncmp(line, "ifndef", 6) == 0) {
		walk = line + 6;
		trimlr(&walk);

		if (*walk == '\0') {
			warnx("Expecting arguments after #ifndef");
			return (T_ERR);
		}

		if (definitions != NULL && sl_find(definitions, walk) != NULL)
			*skip = true;

		return (T_OK);
	}

	return (T_PROCESS);
}

static bool
cal_parse(FILE *in, FILE *out)
{
	char *line = NULL;
	char *buf;
	size_t linecap = 0;
	ssize_t linelen;
	ssize_t l;
	static int d_first = -1;
	static int count = 0;
	int i;
	int month[MAXCOUNT];
	int day[MAXCOUNT];
	int year[MAXCOUNT];
	bool skip = false;
	char dbuf[80];
	char *pp, p;
	struct tm tm;
	int flags;

	/* Unused */
	tm.tm_sec = 0;
	tm.tm_min = 0;
	tm.tm_hour = 0;
	tm.tm_wday = 0;

	if (in == NULL)
		return (false);

	while ((linelen = getline(&line, &linecap, in)) > 0) {
		if (*line == '#') {
			switch (token(line+1, out, &skip)) {
			case T_ERR:
				free(line);
				return (false);
			case T_OK:
				continue;
			case T_PROCESS:
				break;
			default:
				break;
			}
		}

		if (skip)
			continue;

		buf = line;
		for (l = linelen;
		     l > 0 && isspace((unsigned char)buf[l - 1]);
		     l--)
			;
		buf[l] = '\0';
		if (buf[0] == '\0')
			continue;

		/* Parse special definitions: LANG, Easter, Paskha etc */
		if (strncmp(buf, "LANG=", 5) == 0) {
			setlocale(LC_ALL, buf + 5);
			d_first = (*nl_langinfo(D_MD_ORDER) == 'd');
			setnnames();
			continue;
		}

#define	REPLACE(string, slen, struct_) \
		if (strncasecmp(buf, (string), (slen)) == 0 && buf[(slen)]) {	\
			if (struct_.name != NULL)				\
				free(struct_.name);				\
			if ((struct_.name = strdup(buf + (slen))) == NULL)	\
				errx(1, "cannot allocate memory");		\
			struct_.len = strlen(buf + (slen));			\
			continue;						\
		}

		REPLACE("Easter=", 7, neaster);
		REPLACE("Paskha=", 7, npaskha);
		REPLACE("ChineseNewYear=", 15, ncny);
		REPLACE("NewMoon=", 8, nnewmoon);
		REPLACE("FullMoon=", 9, nfullmoon);
		REPLACE("MarEquinox=", 11, nmarequinox);
		REPLACE("SepEquinox=", 11, nsepequinox);
		REPLACE("JunSolstice=", 12, njunsolstice);
		REPLACE("DecSolstice=", 12, ndecsolstice);
#undef	REPLACE

		if (strncmp(buf, "SEQUENCE=", 9) == 0) {
			setnsequences(buf + 9);
			continue;
		}

		/*
		 * If the line starts with a tab, the data has to be
		 * added to the previous line
		 */
		if (buf[0] == '\t') {
			for (i = 0; i < count; i++)
				event_continue(events[i], buf);
			continue;
		}

		/* Get rid of leading spaces (non-standard) */
		while (isspace((unsigned char)buf[0]))
			memcpy(buf, buf + 1, strlen(buf));

		/* No tab in the line, then not a valid line */
		if ((pp = strchr(buf, '\t')) == NULL)
			continue;

		/* Trim spaces in front of the tab */
		while (isspace((unsigned char)pp[-1]))
			pp--;

		p = *pp;
		*pp = '\0';
		if ((count = parsedaymonth(buf, year, month, day, &flags,
		    extradata)) == 0)
			continue;
		*pp = p;
		if (count < 0) {
			/* Show error status based on return value */
			if (debug)
				fprintf(stderr, "Ignored: %s\n", buf);
			if (count == -1)
				continue;
			count = -count + 1;
		}

		/* Find the last tab */
		while (pp[1] == '\t')
			pp++;

		if (d_first < 0)
			d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

		for (i = 0; i < count; i++) {
			tm.tm_mon = month[i] - 1;
			tm.tm_mday = day[i];
			tm.tm_year = year[i] - 1900;
			strftime(dbuf, sizeof(dbuf),
			    d_first ? "%e %b" : "%b %e", &tm);
			if (debug)
				fprintf(stderr, "got %s\n", pp);
			events[i] = event_add(year[i], month[i], day[i], dbuf,
			    ((flags &= F_VARIABLE) != 0) ? 1 : 0, pp,
			    extradata[i]);
		}
	}

	/*
	 * Reset to the default locale, so that one calendar file that changed
	 * the locale (by defining the "LANG" variable) does not interfere the
	 * following calendar files without the "LANG" definition.
	 */
	setlocale(LC_ALL, "");
	setnnames();

	free(line);
	fclose(in);
	return (true);
}

void
cal(void)
{
	FILE *fpin;
	FILE *fpout;
	int i;

	for (i = 0; i < MAXCOUNT; i++)
		extradata[i] = (char *)calloc(1, 20);

	if ((fpin = opencalin()) == NULL)
		return;

	if ((fpout = opencalout()) == NULL) {
		fclose(fpin);
		return;
	}

	if (!cal_parse(fpin, fpout))
		return;

	event_print_all(fpout);
	closecal(fpout);
}

static FILE *
opencalin(void)
{
	struct stat sbuf;
	FILE *fpin = NULL;

	/* open up calendar file */
	if ((fpin = fopen(calendarFile, "r")) == NULL) {
		if (doall) {
			if (chdir(calendarHomes[0]) != 0)
				return (NULL);
			if (stat(calendarNoMail, &sbuf) == 0)
				return (NULL);
			if ((fpin = fopen(calendarFile, "r")) == NULL)
				return (NULL);
		} else {
			fpin = cal_fopen(calendarFile);
		}
	}

	if (fpin == NULL) {
		errx(1, "No calendar file: \"%s\" or \"~/%s/%s\"",
				calendarFile, calendarHomes[0], calendarFile);
	}

	return (fpin);
}

static FILE *
opencalout(void)
{
	int fd;

	/* not reading all calendar files, just set output to stdout */
	if (!doall)
		return (stdout);

	/* set output to a temporary file, so if no output don't send mail */
	snprintf(path, sizeof(path), "%s/_calXXXXXX", _PATH_TMP);
	if ((fd = mkstemp(path)) < 0)
		return (NULL);
	return (fdopen(fd, "w+"));
}

static void
closecal(FILE *fp)
{
	struct stat sbuf;
	int nread, pdes[2], status;
	char buf[1024];

	if (!doall)
		return;

	rewind(fp);
	if (fstat(fileno(fp), &sbuf) || !sbuf.st_size)
		goto done;
	if (pipe(pdes) < 0)
		goto done;

	switch (fork()) {
	case -1:
		/* error */
		close(pdes[0]);
		close(pdes[1]);
		goto done;
	case 0:
		/* child -- set stdin to pipe output */
		if (pdes[0] != STDIN_FILENO) {
			dup2(pdes[0], STDIN_FILENO);
			close(pdes[0]);
		}
		close(pdes[1]);
		execl(_PATH_SENDMAIL, "sendmail", "-i", "-t", "-F",
		    "\"Reminder Service\"", (char *)NULL);
		warn(_PATH_SENDMAIL);
		_exit(1);
	}
	/* parent -- write to pipe input */
	close(pdes[0]);

	header[1].iov_base = header[3].iov_base = pw->pw_name;
	header[1].iov_len = header[3].iov_len = strlen(pw->pw_name);
	writev(pdes[1], header, 8);
	while ((nread = read(fileno(fp), buf, sizeof(buf))) > 0)
		write(pdes[1], buf, nread);
	close(pdes[1]);

done:
	fclose(fp);
	unlink(path);
	while (wait(&status) >= 0)
		;
}
