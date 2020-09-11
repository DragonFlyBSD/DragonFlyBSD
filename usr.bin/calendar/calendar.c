/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Aaron LI <aly@aaronly.me>
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <grp.h>  /* required on Linux for initgroups() */
#include <locale.h>
#include <math.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "calendar.h"
#include "basics.h"
#include "chinese.h"
#include "dates.h"
#include "days.h"
#include "gregorian.h"
#include "io.h"
#include "julian.h"
#include "moon.h"
#include "nnames.h"
#include "parsedata.h"
#include "sun.h"
#include "utils.h"


struct cal_options Options = {
	.time = 0.5,  /* noon */
	.allmode = false,
	.debug = 0,
};

/* paths to search for calendar files for inclusion */
const char *calendarDirs[] = {
	".",  /* i.e., '~/.calendar' */
	CALENDAR_ETCDIR,
	CALENDAR_DIR,
	NULL,
};

/* currently selected calendar to use */
struct calendar *Calendar;

/* all supported calendars */
static struct calendar calendars[] = {
	{  /* the default */
		.id = CAL_GREGORIAN,
		.name = "Gregorian",
		.format_date = NULL,
		.find_days_ymd = find_days_ymd,
		.find_days_dom = find_days_dom,
		.find_days_month = find_days_month,
		.find_days_mdow = find_days_mdow,
	},
	{
		.id = CAL_JULIAN,
		.name = "Julian",
		.format_date = julian_format_date,
		.find_days_ymd = julian_find_days_ymd,
		.find_days_dom = julian_find_days_dom,
		.find_days_month = julian_find_days_month,
		.find_days_mdow = NULL,
	},
	{
		.id = CAL_CHINESE,
		.name = "Chinese",
		.format_date = chinese_format_date,
		.find_days_ymd = chinese_find_days_ymd,
		.find_days_dom = chinese_find_days_dom,
		.find_days_month = NULL,
		.find_days_mdow = NULL,
	},
};

/* user's calendar home directory (relative to $HOME) */
static const char *calendarHome = ".calendar";
/* default calendar file to use if exists in current dir or ~/.calendar */
static const char *calendarFile = "calendar";
/* system-wide calendar file to use if user doesn't have one */
static const char *calendarFileSys = CALENDAR_ETCDIR "/default";
/* don't send mail if this file exists in ~/.calendar */
static const char *calendarNoMail = "nomail";
/* maximum time in seconds that 'calendar -a' can spend for each user */
static const int user_timeout = 10;
/* maximum time in seconds that 'calendar -a' can spend in total */
static const int total_timeout = 3600;

static bool	cd_home(const char *home);
static int	get_fixed_of_today(void);
static double	get_time_of_now(void);
static int	get_utc_offset(void);
static void	handle_sigchld(int signo __unused);
static void	print_datetime(double t, const struct location *loc);
static void	print_location(const struct location *loc, bool warn);
static void	usage(const char *progname) __dead2;


bool
set_calendar(const char *name)
{
	struct calendar *cal;

	if (name == NULL) {
		Calendar = &calendars[0];
		return true;
	}

	for (size_t i = 0; i < nitems(calendars); i++) {
		cal = &calendars[i];
		if (strcasecmp(name, cal->name) == 0) {
			Calendar = cal;
			return true;
		}
	}

	warnx("%s: unknown calendar: |%s|", __func__, name);
	return false;
}


int
main(int argc, char *argv[])
{
	bool	L_flag = false;
	int	ret = 0;
	int	days_before = 0;
	int	days_after = 0;
	int	Friday = 5;  /* days before weekend */
	int	dow;
	int	ch, utc_offset;
	struct passwd *pw;
	struct location loc = { 0 };
	const char *show_info = NULL;
	const char *calfile = NULL;
	const char *calhome = NULL;
	const char *optstring;
	FILE *fp = NULL;

	Options.location = &loc;
	Options.time = get_time_of_now();
	Options.today = get_fixed_of_today();
	loc.zone = get_utc_offset() / (3600.0 * 24.0);

	optstring = "-A:aB:dF:f:hH:L:l:s:T:t:U:W:";
	while ((ch = getopt(argc, argv, optstring)) != -1) {
		switch (ch) {
		case '-':		/* backward compatible */
		case 'a':
			if (getuid() != 0)
				errx(1, "must be root to run with '-a'");
			Options.allmode = true;
			break;

		case 'W': /* don't need to specially deal with Fridays */
			Friday = -1;
			/* FALLTHROUGH */
		case 'A': /* days after current date */
			days_after = (int)strtol(optarg, NULL, 10);
			if (days_after < 0)
				errx(1, "number of days must be positive");
			break;

		case 'B': /* days before current date */
			days_before = (int)strtol(optarg, NULL, 10);
			if (days_before < 0)
				errx(1, "number of days must be positive");
			break;

		case 'd': /* show debug information */
			Options.debug++;
			break;

		case 'F': /* change when the weekend starts */
			Friday = (int)strtol(optarg, NULL, 10);
			break;

		case 'f': /* other calendar file */
			calfile = optarg;
			if (strcmp(optarg, "-") == 0)
				calfile = "/dev/stdin";
			break;

		case 'H': /* calendar home directory */
			calhome = optarg;
			break;

		case 'L': /* location */
			if (!parse_location(optarg, &loc.latitude,
					    &loc.longitude, &loc.elevation)) {
				errx(1, "invalid location: |%s|", optarg);
			}
			L_flag = true;
			break;

		case 's': /* show info of specified category */
			show_info = optarg;
			break;

		case 'T': /* specify time of day */
			if (!parse_time(optarg, &Options.time))
				errx(1, "invalid time: |%s|", optarg);
			break;

		case 't': /* specify date */
			if (!parse_date(optarg, &Options.today))
				errx(1, "invalid date: |%s|", optarg);
			break;

		case 'U': /* specify timezone */
			if (!parse_timezone(optarg, &utc_offset))
				errx(1, "invalid timezone: |%s|", optarg);
			loc.zone = utc_offset / (3600.0 * 24.0);
			break;

		case 'h':
		default:
			usage(argv[0]);
		}
	}

	if (argc > optind)
		usage(argv[0]);

	if (Options.allmode && calfile != NULL)
		errx(1, "flags -a and -f cannot be used together");
	if (Options.allmode && calhome != NULL)
		errx(1, "flags -a and -H cannot be used together");

	if (!L_flag)
		loc.longitude = loc.zone * 360.0;

	/* Friday displays Monday's events */
	dow = dayofweek_from_fixed(Options.today);
	if (days_after == 0 && Friday != -1)
		days_after = (dow == Friday) ? 3 : 1;

	Options.day_begin = Options.today - days_before;
	Options.day_end = Options.today + days_after;
	generate_dates();
	set_calendar(NULL);

	setlocale(LC_ALL, "");
	set_nnames();

	if (setenv("TZ", "UTC", 1) != 0)
		err(1, "setenv");
	tzset();
	/* We're in UTC from now on */

	if (show_info != NULL) {
		double t = Options.today + Options.time;
		if (strcmp(show_info, "chinese") == 0) {
			show_chinese_calendar(Options.today);
		} else if (strcmp(show_info, "julian") == 0) {
			show_julian_calendar(Options.today);
		} else if (strcmp(show_info, "moon") == 0) {
			print_datetime(t, Options.location);
			print_location(Options.location, !L_flag);
			show_moon_info(t, Options.location);
		} else if (strcmp(show_info, "sun") == 0) {
			print_datetime(t, Options.location);
			print_location(Options.location, !L_flag);
			show_sun_info(t, Options.location);
		} else {
			errx(1, "unknown -s value: |%s|", show_info);
		}

		exit(0);
	}

	if (Options.allmode) {
		pid_t kid, deadkid, gkid;
		time_t t;
		bool reaped;
		int kidstat, runningkids;
		unsigned int sleeptime;

		if (signal(SIGCHLD, handle_sigchld) == SIG_ERR)
			err(1, "signal");
		runningkids = 0;
		t = time(NULL);

		while ((pw = getpwent()) != NULL) {
			/*
			 * Enter '~/.calendar' and only try 'calendar'
			 */
			if (!cd_home(pw->pw_dir))
				continue;
			if (access(calendarNoMail, F_OK) == 0)
				continue;
			if ((fp = fopen(calendarFile, "r")) == NULL)
				continue;

			sleeptime = user_timeout;
			kid = fork();
			if (kid < 0) {
				warn("fork");
				continue;
			}
			if (kid == 0) {
				gkid = getpid();
				if (setpgid(gkid, gkid) == -1)
					err(1, "setpgid");
				if (setgid(pw->pw_gid) == -1)
					err(1, "setgid(%u)", pw->pw_gid);
				if (initgroups(pw->pw_name, pw->pw_gid) == -1)
					err(1, "initgroups(%s)", pw->pw_name);
				if (setuid(pw->pw_uid) == -1)
					err(1, "setuid(%u)", pw->pw_uid);

				ret = cal(fp);
				fclose(fp);
				_exit(ret);
			}
			/*
			 * Parent: wait a reasonable time, then kill child
			 * if necessary.
			 */
			runningkids++;
			reaped = false;
			do {
				sleeptime = sleep(sleeptime);
				/*
				 * Note that there is the possibility, if the
				 * sleep stops early due to some other signal,
				 * of the child terminating and not getting
				 * detected during the next sleep.  In that
				 * unlikely worst case, we just sleep too long
				 * for that user.
				 */
				for (;;) {
					deadkid = waitpid(-1, &kidstat, WNOHANG);
					if (deadkid <= 0)
						break;
					runningkids--;
					if (deadkid == kid) {
						reaped = true;
						sleeptime = 0;
					}
				}
			} while (sleeptime);

			if (!reaped) {
				/*
				 * It doesn't really matter if the kill fails;
				 * there is only one more zombie now.
				 */
				gkid = getpgid(kid);
				if (gkid != getpgrp())
					killpg(gkid, SIGTERM);
				else
					kill(kid, SIGTERM);
				warnx("user %s (uid %u) did not finish in time "
				      "(%d seconds)",
				      pw->pw_name, pw->pw_uid, user_timeout);
			}

			if (time(NULL) - t > total_timeout) {
				errx(2, "'calendar -a' timed out (%d seconds); "
					"stop at user %s (uid %u)",
					total_timeout, pw->pw_name, pw->pw_uid);
			}
		}

		for (;;) {
			deadkid = waitpid(-1, &kidstat, WNOHANG);
			if (deadkid <= 0)
				break;
			runningkids--;
		}
		if (runningkids) {
			warnx("%d child processes still running when "
			      "'calendar -a' finished", runningkids);
		}

	} else {
		if (calfile && (fp = fopen(calfile, "r")) == NULL)
			errx(1, "Cannot open calendar file: '%s'", calfile);

		/* try 'calendar' in current directory */
		if (fp == NULL)
			fp = fopen(calendarFile, "r");

		if (calhome) {
			if (chdir(calhome) == -1)
				errx(1, "Cannot enter home: '%s'", calhome);
			/* try 'calendar' in home directory */
			if (fp == NULL)
				fp = fopen(calendarFile, "r");
		} else if (cd_home(NULL)) {  /* try to enter '~/.calendar' */
			/* try 'calendar' in home directory */
			if (fp == NULL)
				fp = fopen(calendarFile, "r");
		} else {
			DPRINTF("Fallback to enter '%s'\n", calendarDirs[1]);
			/* fallback to '/etc/calendar' as home directory */
			if (chdir(calendarDirs[1]) == -1)
				errx(1, "Cannot enter directory: '%s'",
				     calendarDirs[1]);
		}

		/* fallback to '/etc/calendar/default' */
		if (fp == NULL) {
			warnx("No user's calendar file; "
			      "fallback to system default: '%s'",
			      calendarFileSys);
			fp = fopen(calendarFileSys, "r");
			if (fp == NULL)
				errx(1, "Cannot find calendar file");
		}

		ret = cal(fp);
		fclose(fp);
	}

	free_dates();
	return (ret);
}


static void
handle_sigchld(int signo __unused)
{
	/* empty; just let the main() to reap the child */
}

static double
get_time_of_now(void)
{
	time_t now;
	struct tm tm;

	now = time(NULL);
	tzset();
	localtime_r(&now, &tm);

	return (tm.tm_hour + tm.tm_min/60.0 + tm.tm_sec/3600.0) / 24.0;
}

static int
get_fixed_of_today(void)
{
	time_t now;
	struct tm tm;
	struct date gdate;

	now = time(NULL);
	tzset();
	localtime_r(&now, &tm);
	date_set(&gdate, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

	return fixed_from_gregorian(&gdate);
}

static int
get_utc_offset(void)
{
	time_t now;
	struct tm tm;

	now = time(NULL);
	tzset();
	localtime_r(&now, &tm);

	return tm.tm_gmtoff;
}

static bool
cd_home(const char *home)
{
	char path[MAXPATHLEN];

	if (home == NULL) {
		home = getenv("HOME");
		if (home == NULL || *home == '\0') {
			warnx("Cannot get '$HOME'");
			return false;
		}
	}

	snprintf(path, sizeof(path), "%s/%s", home, calendarHome);
	if (chdir(path) == -1) {
		DPRINTF("Cannot enter home directory: '%s'\n", path);
		return false;
	}

	return true;
}

static void
print_datetime(double t, const struct location *loc)
{
	struct date date;
	char buf[64];

	gregorian_from_fixed(floor(t), &date);
	printf("Gregorian date: %d-%02d-%02d\n",
	       date.year, date.month, date.day);

	format_time(buf, sizeof(buf), t);
	printf("Time: %s", buf);
	if (loc != NULL) {
		format_zone(buf, sizeof(buf), loc->zone);
		printf(" %s\n", buf);
	} else {
		printf("\n");
	}
}

static void
print_location(const struct location *loc, bool warn)
{
	char buf[64];

	format_location(buf, sizeof(buf), loc);
	printf("Location: %s%s\n", buf,
	       warn ? "  [WARNING: use '-L' to specify]" : "");
}

static void __dead2
usage(const char *progname)
{
	fprintf(stderr,
		"usage:\n"
		"%s [-A days] [-a] [-B days] [-d] [-F friday]\n"
		"\t[-f calendar_file] [-H calendar_home]\n"
		"\t[-L latitude,longitude[,elevation]] [-s category]\n"
		"\t[-T hh:mm[:ss]] [-t [[[CC]YY]MM]DD] [-U ±hh[[:]mm]] [-W days]\n",
		progname);
	exit(1);
}
